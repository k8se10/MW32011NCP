#include "dualsense_input.h"

#include <windows.h>
#include <initguid.h>
#include <hidclass.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <malloc.h> // _malloca/_freea, same stack-alloc pattern the reference
                     // implementation uses for the variable-length device path buffer
#include <cstring> // memcpy (DualSense_SetVibration's output-buffer assembly)
#include <cstdio> // sprintf_s (BT input CRC-mismatch diagnostic logging)

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

// Sony's real, confirmed DualSense USB vendor/product ID -- cross-checked against a
// real, working, MIT-licensed Windows implementation (github.com/Ohjurot/DualSense-
// Windows, IO.cpp's own enumDevices()), not guessed.
constexpr USHORT kSonyVendorId = 0x054C;
constexpr USHORT kDualSenseProductId = 0x0CE6;

// USB input report is a fixed 64 bytes including the leading report-ID byte (byte 0 =
// 0x01); this matches HIDP_CAPS::InputReportByteLength for a USB-connected DualSense.
// A Bluetooth-connected DualSense instead reports 78 bytes (report ID 0x31) -- both
// are accepted by TryOpenDualSense below (2026-08-16, live tester has a BT unit),
// distinguished purely by this length, same as the reference implementation's own
// enumDevices() (github.com/Ohjurot/DualSense-Windows, IO.cpp).
constexpr USHORT kUsbInputReportLength = 64;
constexpr USHORT kBtInputReportLength = 78;
constexpr USHORT kMaxInputReportLength = kBtInputReportLength; // read buffer sizing

HANDLE g_deviceHandle = INVALID_HANDLE_VALUE;
bool g_isBluetooth = false; // which of the two report shapes above g_deviceHandle matched
bool g_openAttempted = false; // one open attempt per "session" until it fails --
                                // see DualSense_EnsureOpen's own comment for the
                                // actual retry policy.
DWORD g_lastOpenAttemptTickMs = 0;
constexpr DWORD kReopenRetryIntervalMs = 3000; // don't hammer SetupDi enumeration
                                                 // every single poll tick (~4ms) if
                                                 // no DualSense is present at all --
                                                 // this is the common case for most
                                                 // players, who have an Xbox pad.

// Real DualSense HID report byte offsets, expressed PAYLOAD-RELATIVE (i.e. relative to
// the reference implementation's own evaluateHidInputBuffer() argument, which is a
// pointer already advanced past every leading transport-framing byte). The reference's
// IO.cpp calls that function on `&hidBuffer[1]` for USB and `&hidBuffer[2]` for
// Bluetooth (one extra leading byte over the wire on BT before the same payload
// shape starts) -- DualSense_Poll below adds the correct base (1 or 2) itself
// depending on g_isBluetooth, so these constants don't need a separate BT variant:
//   left stick X/Y, right stick X/Y : bytes 0-3
//   L2/R2 analog                    : bytes 4-5
//   sequence counter                : byte 6
//   D-pad (low nibble) + Square/Cross/Circle/Triangle (high nibble) : byte 7
//   L1/R1/L2btn/R2btn/Create/Options/L3/R3                          : byte 8
//   PS/touchpad-click/mute                                          : byte 9
//   accelerometer X/Y/Z (int16 LE each)  : bytes 15-20 (reference offset 0x0F)
//   gyroscope X/Y/Z (int16 LE each)      : bytes 21-26 (reference offset 0x15)
// Source: github.com/Ohjurot/DualSense-Windows, DS5_Input.cpp::evaluateHidInputBuffer
// and IO.cpp::getDeviceInputState (MIT licensed; fetched and cross-checked 2026-08-11,
// BT framing re-confirmed 2026-08-16 -- not copied verbatim, this is an independent
// implementation against the same confirmed offsets). NOT independently verified
// against real hardware by this project -- flagged Preview/WIP, see this file's
// header comment.
constexpr int kOffsetLeftX = 0;
constexpr int kOffsetLeftY = 1;
constexpr int kOffsetRightX = 2;
constexpr int kOffsetRightY = 3;
constexpr int kOffsetLeftTrigger = 4;
constexpr int kOffsetRightTrigger = 5;
constexpr int kOffsetButtonsAndDpad = 7;
constexpr int kOffsetButtonsA = 8;
constexpr int kOffsetButtonsB = 9;
constexpr int kOffsetAccel = 15; // 3x int16 LE: X, Y, Z
constexpr int kOffsetGyro = 21;  // 3x int16 LE: X, Y, Z

int16_t ReadInt16LE(const unsigned char* buf, int offset)
{
    return static_cast<int16_t>(static_cast<uint16_t>(buf[offset]) | (static_cast<uint16_t>(buf[offset + 1]) << 8));
}

uint32_t ReadUint32LE(const unsigned char* buf, int offset)
{
    return static_cast<uint32_t>(buf[offset]) | (static_cast<uint32_t>(buf[offset + 1]) << 8) |
           (static_cast<uint32_t>(buf[offset + 2]) << 16) | (static_cast<uint32_t>(buf[offset + 3]) << 24);
}

// Standard reflected CRC-32 (poly 0xEDB88320 -- zlib/IEEE 802.3/PKZIP), bit-by-bit
// form. `crc` is the running register, passed in and out uncomplemented so it can be
// chained across multiple calls (see ComputeDualSenseCrc32 below).
uint32_t Crc32StandardUpdate(uint32_t crc, const unsigned char* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc;
}

// 2026-08-16 (real BT hardware live-tested, both sticks "way fucked up"): this
// project's FIRST attempt at this CRC ported the byte offsets/framing from
// Ohjurot/DualSense-Windows (as documented elsewhere in this file) but used THAT
// project's own community-reimplemented seed/table trick for the checksum math
// itself, which turned out to be unverifiable from the outside (its 256-entry
// table isn't the plain standard one -- table[0] != 0 -- meaning it bakes the
// seed's pre-complement into the table itself; plausible but NOT independently
// checkable without also transcribing and trusting that whole table). Replaced
// with the SAME formula Sony's own mainline Linux kernel driver uses
// (drivers/hid/hid-playstation.c, ps_check_crc32/dualsense_send_output_report,
// Copyright Sony Interactive Entertainment) -- authoritative rather than a
// community reimplementation, and unambiguous: hash a single fixed "transport
// direction" seed byte from a standard init of 0xFFFFFFFF (0xA1 = this is an
// INPUT report being validated, 0xA2 = this is an OUTPUT report being written,
// 0xA3 = a feature report -- not used here), continue hashing the real report
// bytes (report ID onward, EXCLUDING the CRC's own trailing 4 bytes) from that
// point, then bitwise-complement the final result. Verified bit-exact against
// Ohjurot's own hardcoded output seed constant before switching (0xEADA2D49 ==
// complement of this formula's running register after just the 0xA2 seed byte --
// the two aren't a coincidence, they're the same real protocol, just exposed two
// different ways), so this isn't a blind swap, but this formula is the one this
// project can actually verify byte-by-byte against a real, authoritative source
// rather than trust unseen table contents.
uint32_t ComputeDualSenseCrc32(unsigned char transportSeedByte, const unsigned char* data, size_t len)
{
    uint32_t crc = Crc32StandardUpdate(0xFFFFFFFFu, &transportSeedByte, 1);
    crc = ~Crc32StandardUpdate(crc, data, len);
    return crc;
}

constexpr unsigned char kCrcSeedInputReport = 0xA1;
constexpr unsigned char kCrcSeedOutputReport = 0xA2;

void CloseDevice()
{
    if (g_deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_deviceHandle);
        g_deviceHandle = INVALID_HANDLE_VALUE;
    }
}

// Enumerates every HID device interface (same GUID_DEVINTERFACE_HID + SetupDiXxx
// pattern the reference implementation uses) looking for a VID/PID match whose real
// input report length is EITHER 64 (USB) or 78 (Bluetooth) bytes -- outIsBluetooth
// reports which one matched. Returns an opened, ready-to-read device handle, or
// INVALID_HANDLE_VALUE if none found/reachable.
HANDLE TryOpenDualSense(bool& outIsBluetooth)
{
    outIsBluetooth = false;
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr,
                                              DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (!devInfo || devInfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    HANDLE result = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);
    for (DWORD ifIndex = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_HID, ifIndex, &ifData); ++ifIndex) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize == 0 || requiredSize > 1024) continue;

        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(_malloca(requiredSize));
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, requiredSize, nullptr, nullptr)) {
            HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h && h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attrs = {};
                attrs.Size = sizeof(attrs);
                bool matched = false;
                if (HidD_GetAttributes(h, &attrs) && attrs.VendorID == kSonyVendorId && attrs.ProductID == kDualSenseProductId) {
                    PHIDP_PREPARSED_DATA preparsed = nullptr;
                    if (HidD_GetPreparsedData(h, &preparsed)) {
                        HIDP_CAPS caps = {};
                        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                            if (caps.InputReportByteLength == kUsbInputReportLength) {
                                matched = true;
                                outIsBluetooth = false;
                            } else if (caps.InputReportByteLength == kBtInputReportLength) {
                                matched = true;
                                outIsBluetooth = true;
                            }
                        }
                        HidD_FreePreparsedData(preparsed);
                    }
                }
                if (matched) {
                    result = h;
                } else {
                    CloseHandle(h);
                }
            }
        }
        _freea(detail);
        if (result != INVALID_HANDLE_VALUE) break;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

} // namespace

bool DualSense_EnsureOpen()
{
    if (g_deviceHandle != INVALID_HANDLE_VALUE) return true;

    DWORD now = GetTickCount();
    if (g_openAttempted && (now - g_lastOpenAttemptTickMs) < kReopenRetryIntervalMs) return false;
    g_openAttempted = true;
    g_lastOpenAttemptTickMs = now;

    g_deviceHandle = TryOpenDualSense(g_isBluetooth);
    if (g_deviceHandle != INVALID_HANDLE_VALUE) {
        // BT-specific handshake (2026-08-16): the reference implementation's own
        // initDeviceContext reads feature report 0x05 once, right after opening,
        // for any Bluetooth-connected DualSense -- confirmed necessary via that
        // project's own source (github.com/Ohjurot/DualSense-Windows, IO.cpp) to
        // get the controller sending its FULL 78-byte extended report at all,
        // rather than staying in a reduced legacy/DS4-compatible report shape
        // that wouldn't match the offsets this file reads. USB needs no
        // equivalent step. Failure here isn't treated as fatal -- some real
        // firmware/driver combinations may already be in extended mode -- but is
        // logged so a live BT test that comes back with garbage stick/button
        // values has an immediate first place to look.
        if (g_isBluetooth) {
            unsigned char featureBuf[64] = {};
            featureBuf[0] = 0x05;
            if (!HidD_GetFeature(g_deviceHandle, featureBuf, sizeof(featureBuf))) {
                LogFromController("[dualsense] BT extended-report handshake (feature report 0x05) FAILED -- continuing anyway, but input may be malformed");
            }
        }
        LogFromController(g_isBluetooth
            ? "[dualsense] raw HID device opened (VID_054C&PID_0CE6, Bluetooth, 78-byte report confirmed)"
            : "[dualsense] raw HID device opened (VID_054C&PID_0CE6, USB, 64-byte report confirmed)");
    }
    return g_deviceHandle != INVALID_HANDLE_VALUE;
}

bool DualSense_IsOpen()
{
    return g_deviceHandle != INVALID_HANDLE_VALUE;
}

bool DualSense_Poll(DualSenseRawState& outState)
{
    outState = DualSenseRawState{};
    if (g_deviceHandle == INVALID_HANDLE_VALUE) return false;

    const USHORT reportLength = g_isBluetooth ? kBtInputReportLength : kUsbInputReportLength;
    const int payloadBase = g_isBluetooth ? 2 : 1; // see kOffsetLeftX's own comment

    unsigned char buf[kMaxInputReportLength] = {};
    DWORD bytesRead = 0;
    if (!ReadFile(g_deviceHandle, buf, reportLength, &bytesRead, nullptr) || bytesRead < reportLength) {
        LogFromController("[dualsense] ReadFile failed -- device likely unplugged, closing handle");
        CloseDevice();
        return false;
    }

    // Live-reported 2026-08-16 (real BT hardware, AFTER the Y-axis fix below was
    // already live): sticks still "way fucked up" -- genuinely unusable, not just
    // inverted. Root cause: this project was never validating the CRC32 Sony's own
    // BT input reports carry in their last 4 bytes (drivers/hid/hid-playstation.c's
    // ps_check_crc32 REJECTS any BT input report that fails this check -- Bluetooth
    // HID reports can and do arrive corrupted/truncated in the real world, and
    // without this check this project was blindly parsing whatever garbage bytes
    // ReadFile handed back as if they were real stick positions). USB carries no
    // such checksum (wired HID has no equivalent transport-level corruption risk
    // this protocol needs to guard against) -- BT only.
    if (g_isBluetooth) {
        uint32_t expectedCrc = ReadUint32LE(buf, reportLength - 4);
        uint32_t actualCrc = ComputeDualSenseCrc32(kCrcSeedInputReport, buf, reportLength - 4);
        if (actualCrc != expectedCrc) {
            static int s_crcFailLogCount = 0;
            if (s_crcFailLogCount < 20) {
                ++s_crcFailLogCount;
                char msg[128];
                sprintf_s(msg, "[dualsense] BT input report CRC mismatch (#%d) -- expected 0x%08X, got 0x%08X, discarding this report",
                    s_crcFailLogCount, expectedCrc, actualCrc);
                LogFromController(msg);
            }
            // Deliberately NOT closing the device -- a single corrupted report over
            // Bluetooth isn't "unplugged," it's noise. Returning false here just
            // skips using THIS report's data (caller keeps its last-known state /
            // shows a momentary disconnected tick); DualSense_IsOpen() still stays
            // true, so the next tick tries a fresh read normally.
            return false;
        }
    }

    // Live-reported 2026-08-16 (real BT hardware): "look up down and move forward
    // back are inverted" -- both Y axes. Root cause: raw HID joystick convention is
    // Y-increases-DOWNWARD (byte 255 = stick pushed toward the player), the opposite
    // sign of XInput's own sThumbLY/sThumbRY (which the driver already normalizes to
    // Y-increases-UP/forward, the convention ShapeStick and every downstream consumer
    // in this project assume, since the XInput branch of this same poll loop needs no
    // such correction). The reference implementation negates BOTH stick Y axes for
    // exactly this reason (DS5_Input.cpp::evaluateHidInputBuffer: `(byte - 127) * -1`
    // for leftStick.y/rightStick.y, vs. plain `byte - 128` for both X axes) -- this
    // file's own header comment already said as much ("one direction inverted for
    // Y") but the code itself never actually applied it until now. X axes correctly
    // need no such correction (confirmed live: left/right wasn't reported inverted).
    const unsigned char* p = buf + payloadBase;
    outState.leftStickX = static_cast<int16_t>(static_cast<int>(p[kOffsetLeftX]) - 128);
    outState.leftStickY = static_cast<int16_t>(-(static_cast<int>(p[kOffsetLeftY]) - 128));
    outState.rightStickX = static_cast<int16_t>(static_cast<int>(p[kOffsetRightX]) - 128);
    outState.rightStickY = static_cast<int16_t>(-(static_cast<int>(p[kOffsetRightY]) - 128));
    outState.leftTrigger = p[kOffsetLeftTrigger];
    outState.rightTrigger = p[kOffsetRightTrigger];
    outState.buttonsAndDpad = p[kOffsetButtonsAndDpad];
    outState.buttonsA = p[kOffsetButtonsA];
    outState.buttonsB = p[kOffsetButtonsB];
    outState.accelX = ReadInt16LE(p, kOffsetAccel + 0);
    outState.accelY = ReadInt16LE(p, kOffsetAccel + 2);
    outState.accelZ = ReadInt16LE(p, kOffsetAccel + 4);
    outState.gyroX = ReadInt16LE(p, kOffsetGyro + 0);
    outState.gyroY = ReadInt16LE(p, kOffsetGyro + 2);
    outState.gyroZ = ReadInt16LE(p, kOffsetGyro + 4);
    outState.connected = true;
    return true;
}

unsigned short DualSense_ToXInputButtons(const DualSenseRawState& state)
{
    // XINPUT_GAMEPAD_* bit values (from xinput.h) reproduced here as plain constants
    // so this file doesn't need to include xinput.h for six #defines.
    constexpr unsigned short kXInputDpadUp = 0x0001, kXInputDpadDown = 0x0002,
        kXInputDpadLeft = 0x0004, kXInputDpadRight = 0x0008,
        kXInputStart = 0x0010, kXInputBack = 0x0020,
        kXInputLeftThumb = 0x0040, kXInputRightThumb = 0x0080,
        kXInputLeftShoulder = 0x0100, kXInputRightShoulder = 0x0200,
        kXInputA = 0x1000, kXInputB = 0x2000, kXInputX = 0x4000, kXInputY = 0x8000;

    unsigned short out = 0;

    // D-pad: low nibble of buttonsAndDpad is a HAT switch, per the reference
    // implementation's own switch statement (0=Up,1=RightUp,2=Right,3=RightDown,
    // 4=Down,5=LeftDown,6=Left,7=LeftUp,8/0xF=released).
    switch (state.buttonsAndDpad & 0x0F) {
        case 0x0: out |= kXInputDpadUp; break;
        case 0x1: out |= kXInputDpadUp | kXInputDpadRight; break;
        case 0x2: out |= kXInputDpadRight; break;
        case 0x3: out |= kXInputDpadRight | kXInputDpadDown; break;
        case 0x4: out |= kXInputDpadDown; break;
        case 0x5: out |= kXInputDpadDown | kXInputDpadLeft; break;
        case 0x6: out |= kXInputDpadLeft; break;
        case 0x7: out |= kXInputDpadLeft | kXInputDpadUp; break;
        default: break; // released / center
    }

    // Face buttons (high nibble of buttonsAndDpad): DualSense Cross/Circle/Square/
    // Triangle map to the Xbox-layout A/B/X/Y positions they occupy on the same
    // physical corners of the pad -- matches how Steam Input's own default Xbox
    // emulation maps them, and this project's downstream button-mapping code already
    // assumes an XInput-shaped layout regardless of the glyph art style shown
    // (GlyphStyle::PlayStation already exists purely as icon art -- see mod_config.h).
    if (state.buttonsAndDpad & 0x20) out |= kXInputA; // Cross
    if (state.buttonsAndDpad & 0x40) out |= kXInputB; // Circle
    if (state.buttonsAndDpad & 0x10) out |= kXInputX; // Square
    if (state.buttonsAndDpad & 0x80) out |= kXInputY; // Triangle

    if (state.buttonsA & 0x01) out |= kXInputLeftShoulder;  // L1
    if (state.buttonsA & 0x02) out |= kXInputRightShoulder; // R1
    if (state.buttonsA & 0x10) out |= kXInputBack;          // Create/Share
    if (state.buttonsA & 0x20) out |= kXInputStart;         // Options
    if (state.buttonsA & 0x40) out |= kXInputLeftThumb;     // L3
    if (state.buttonsA & 0x80) out |= kXInputRightThumb;    // R3
    // buttonsA bits 0x04 (L2 digital)/0x08 (R2 digital) intentionally unused --
    // this project reads the ANALOG L2/R2 values (kOffsetLeftTrigger/RightTrigger)
    // for trigger state, same convention as real XInput triggers.

    // buttonsB (PS/touchpad-click/mute) has no XInput equivalent -- see this
    // function's own header comment in dualsense_input.h.

    return out;
}

bool DualSense_HasGyro()
{
    return g_deviceHandle != INVALID_HANDLE_VALUE;
}

namespace {

// ---- Output report (rumble) -- 2026-08-16, closes the gap the original 2026-08-11
// pass explicitly deferred (Controller_SetVibration silently no-ops for a DualSense --
// see its own comment in controller_input.cpp). USB and Bluetooth need entirely
// different framing, not just an offset shift like the input side.
//
// USB: 48-byte buffer, buf[0] = report ID 0x02, payload from createHidOutputBuffer
// starts at buf[1].
// Bluetooth: buf[0] = 0x31, buf[1] = 0x02 (sub-type), payload starts at buf[2], then a
// CRC32 checksum over buf[0..73] (74 bytes) is written little-endian at buf[0x4A..0x4D]
// (bytes 74-77) -- the BT firmware silently DROPS any output report that fails this
// check, unlike a malformed USB one which typically still applies partially. The
// reference implementation sends the full 547-byte buffer length to WriteFile for BT
// even though only the first 78 bytes carry real content (rest zero-padded) -- matched
// here exactly rather than "optimized" down, since 547 is what's actually proven
// working, not a number this project derived itself.
// Source: github.com/Ohjurot/DualSense-Windows, IO.cpp::setDeviceOutputState,
// DS5_Output.cpp::createHidOutputBuffer, DS_CRC32.cpp (MIT licensed; byte offsets and
// the CRC32 table+seed fetched and cross-checked directly against that project's own
// source this pass, not reconstructed from memory -- this is exactly the kind of
// hardware-wire-protocol constant this project's own standing rule says never to
// guess). Independent implementation against those confirmed values, not copied code.
constexpr int kUsbOutputReportLength = 48;
constexpr int kBtOutputReportLength = 547; // see comment above -- yes, really 547
constexpr int kBtOutputCrcCoveredLength = 74;
constexpr int kBtOutputCrcOffset = 0x4A;

// Payload-relative offsets (relative to createHidOutputBuffer's own buffer start --
// buf+1 for USB, buf+2 for BT, same pattern as the input side above).
constexpr int kOutPayloadFeatureFlag0 = 0x00;
constexpr int kOutPayloadFeatureFlag1 = 0x01;
constexpr int kOutPayloadRightRumble = 0x02; // weak/high-freq motor
constexpr int kOutPayloadLeftRumble = 0x03;  // strong/low-freq motor
constexpr int kOutPayloadLength = 0x2F; // covers every field createHidOutputBuffer
                                          // touches (up to lightbar blue at 0x2E) --
                                          // this project only ever sets the rumble
                                          // bytes, but sizing the zeroed staging
                                          // buffer to the real payload shape (rather
                                          // than just kOutPayloadRightRumble+2) keeps
                                          // headroom for anything added here later
                                          // (LEDs/lightbar/triggers) without a resize.

// Deliberately narrower than the reference implementation's own hardcoded 0xFF/0xF7
// (which claims EVERY feature -- LEDs, lightbar, mic LED, triggers -- on every single
// report). This project only wants rumble: 0x01 is the documented DualSense output
// "valid flag0" bit for compatible-mode rumble motors (the classic weak/strong ERM
// pair this file's leftRumble/rightRumble bytes drive) -- widely cross-referenced
// community convention (Linux hid-playstation driver and multiple independent
// DualSense libraries agree on this bit), kept separate from the reference's own
// blanket value specifically so a rumble report doesn't ALSO blank the player's
// current lightbar color/player-LED state to black on every single pulse, which the
// reference's literal values would do given this project sends zeroed LED/lightbar
// bytes (never having populated them). flag1 = 0x00 makes no LED/lightbar/mic-LED
// claim at all, so the controller keeps whatever those are already showing.
constexpr unsigned char kOutFeatureFlag0RumbleOnly = 0x01;
constexpr unsigned char kOutFeatureFlag1None = 0x00;

} // namespace

bool DualSense_SetVibration(uint8_t leftMotor, uint8_t rightMotor)
{
    if (g_deviceHandle == INVALID_HANDLE_VALUE) return false;

    unsigned char payload[kOutPayloadLength] = {};
    payload[kOutPayloadFeatureFlag0] = kOutFeatureFlag0RumbleOnly;
    payload[kOutPayloadFeatureFlag1] = kOutFeatureFlag1None;
    payload[kOutPayloadRightRumble] = rightMotor;
    payload[kOutPayloadLeftRumble] = leftMotor;

    bool ok;
    if (g_isBluetooth) {
        unsigned char buf[kBtOutputReportLength] = {};
        buf[0] = 0x31;
        buf[1] = 0x02;
        memcpy(buf + 2, payload, sizeof(payload));

        uint32_t crc = ComputeDualSenseCrc32(kCrcSeedOutputReport, buf, kBtOutputCrcCoveredLength);
        buf[kBtOutputCrcOffset + 0] = static_cast<unsigned char>((crc >> 0) & 0xFF);
        buf[kBtOutputCrcOffset + 1] = static_cast<unsigned char>((crc >> 8) & 0xFF);
        buf[kBtOutputCrcOffset + 2] = static_cast<unsigned char>((crc >> 16) & 0xFF);
        buf[kBtOutputCrcOffset + 3] = static_cast<unsigned char>((crc >> 24) & 0xFF);

        DWORD written = 0;
        ok = WriteFile(g_deviceHandle, buf, sizeof(buf), &written, nullptr) != 0;
    } else {
        unsigned char buf[kUsbOutputReportLength] = {};
        buf[0] = 0x02;
        memcpy(buf + 1, payload, sizeof(payload));

        DWORD written = 0;
        ok = WriteFile(g_deviceHandle, buf, sizeof(buf), &written, nullptr) != 0;
    }

    if (!ok) {
        LogFromController("[dualsense] rumble WriteFile failed -- device likely unplugged, closing handle");
        CloseDevice();
        return false;
    }
    return true;
}
