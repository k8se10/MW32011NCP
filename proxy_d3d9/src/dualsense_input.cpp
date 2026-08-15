#include "dualsense_input.h"

#include <windows.h>
#include <initguid.h>
#include <hidclass.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <malloc.h> // _malloca/_freea, same stack-alloc pattern the reference
                     // implementation uses for the variable-length device path buffer

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

// Sony's real, confirmed DualSense USB vendor/product ID -- cross-checked against a
// real, working, MIT-licensed Windows implementation (github.com/Ohjurot/DualSense-
// Windows, IO.cpp's own enumDevices()), not guessed.
constexpr USHORT kSonyVendorId = 0x054C;
constexpr USHORT kDualSenseProductId = 0x0CE6;

// USB input report is a fixed 64 bytes including the leading report-ID byte (byte 0 =
// 0x01); this matches HIDP_CAPS::InputReportByteLength for a USB-connected DualSense,
// used below to distinguish it from a Bluetooth-connected one (78 bytes) without
// needing to ask the device which transport it's on. Bluetooth is NOT implemented
// here -- see this file's own bottom comment for why, and what BT support would need.
constexpr USHORT kUsbInputReportLength = 64;

HANDLE g_deviceHandle = INVALID_HANDLE_VALUE;
bool g_openAttempted = false; // one open attempt per "session" until it fails --
                                // see DualSense_EnsureOpen's own comment for the
                                // actual retry policy.
DWORD g_lastOpenAttemptTickMs = 0;
constexpr DWORD kReopenRetryIntervalMs = 3000; // don't hammer SetupDi enumeration
                                                 // every single poll tick (~4ms) if
                                                 // no DualSense is present at all --
                                                 // this is the common case for most
                                                 // players, who have an Xbox pad.

// Real DualSense HID report byte offsets (USB, report ID INCLUDED as byte 0 -- Windows'
// ReadFile on a HID device with report IDs always returns the report ID as the buffer's
// first byte, per HidD_* documented behavior), derived by taking the reference
// implementation's own offsets (which operate on a buffer with the report ID already
// stripped, i.e. their offset N == this project's byte N+1) and adding 1 back:
//   left stick X/Y, right stick X/Y : bytes 1-4
//   L2/R2 analog                    : bytes 5-6
//   sequence counter                : byte 7
//   D-pad (low nibble) + Square/Cross/Circle/Triangle (high nibble) : byte 8
//   L1/R1/L2btn/R2btn/Create/Options/L3/R3                          : byte 9
//   PS/touchpad-click/mute                                          : byte 10
//   accelerometer X/Y/Z (int16 LE each)  : bytes 16-21 (reference offset 0x0F -> +1 = 0x10)
//   gyroscope X/Y/Z (int16 LE each)      : bytes 22-27 (reference offset 0x15 -> +1 = 0x16)
// Source: github.com/Ohjurot/DualSense-Windows, DS5_Input.cpp::evaluateHidInputBuffer
// (MIT licensed; fetched and cross-checked 2026-08-11 -- not copied verbatim, this is
// an independent implementation against the same confirmed offsets). NOT independently
// verified against real hardware by this project -- flagged Preview/WIP, see this
// file's header comment.
constexpr int kOffsetLeftX = 1;
constexpr int kOffsetLeftY = 2;
constexpr int kOffsetRightX = 3;
constexpr int kOffsetRightY = 4;
constexpr int kOffsetLeftTrigger = 5;
constexpr int kOffsetRightTrigger = 6;
constexpr int kOffsetButtonsAndDpad = 8;
constexpr int kOffsetButtonsA = 9;
constexpr int kOffsetButtonsB = 10;
constexpr int kOffsetAccel = 16; // 3x int16 LE: X, Y, Z
constexpr int kOffsetGyro = 22;  // 3x int16 LE: X, Y, Z

int16_t ReadInt16LE(const unsigned char* buf, int offset)
{
    return static_cast<int16_t>(static_cast<uint16_t>(buf[offset]) | (static_cast<uint16_t>(buf[offset + 1]) << 8));
}

void CloseDevice()
{
    if (g_deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_deviceHandle);
        g_deviceHandle = INVALID_HANDLE_VALUE;
    }
}

// Enumerates every HID device interface (same GUID_DEVINTERFACE_HID + SetupDiXxx
// pattern the reference implementation uses) looking for a VID/PID match whose real
// USB input report length is exactly 64 bytes (rules out a Bluetooth-connected
// DualSense, which reports 78 -- not handled by this pass, see bottom comment).
// Returns an opened, ready-to-read device handle, or INVALID_HANDLE_VALUE if none
// found/reachable.
HANDLE TryOpenDualSense()
{
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
                        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
                            caps.InputReportByteLength == kUsbInputReportLength) {
                            matched = true;
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

    g_deviceHandle = TryOpenDualSense();
    if (g_deviceHandle != INVALID_HANDLE_VALUE) {
        LogFromController("[dualsense] raw HID device opened (VID_054C&PID_0CE6, USB, 64-byte report confirmed)");
    }
    return g_deviceHandle != INVALID_HANDLE_VALUE;
}

bool DualSense_Poll(DualSenseRawState& outState)
{
    outState = DualSenseRawState{};
    if (g_deviceHandle == INVALID_HANDLE_VALUE) return false;

    unsigned char buf[kUsbInputReportLength] = {};
    DWORD bytesRead = 0;
    if (!ReadFile(g_deviceHandle, buf, sizeof(buf), &bytesRead, nullptr) || bytesRead < kUsbInputReportLength) {
        LogFromController("[dualsense] ReadFile failed -- device likely unplugged, closing handle");
        CloseDevice();
        return false;
    }

    outState.leftStickX = static_cast<int16_t>(static_cast<int>(buf[kOffsetLeftX]) - 128);
    outState.leftStickY = static_cast<int16_t>(static_cast<int>(buf[kOffsetLeftY]) - 128);
    outState.rightStickX = static_cast<int16_t>(static_cast<int>(buf[kOffsetRightX]) - 128);
    outState.rightStickY = static_cast<int16_t>(static_cast<int>(buf[kOffsetRightY]) - 128);
    outState.leftTrigger = buf[kOffsetLeftTrigger];
    outState.rightTrigger = buf[kOffsetRightTrigger];
    outState.buttonsAndDpad = buf[kOffsetButtonsAndDpad];
    outState.buttonsA = buf[kOffsetButtonsA];
    outState.buttonsB = buf[kOffsetButtonsB];
    outState.accelX = ReadInt16LE(buf, kOffsetAccel + 0);
    outState.accelY = ReadInt16LE(buf, kOffsetAccel + 2);
    outState.accelZ = ReadInt16LE(buf, kOffsetAccel + 4);
    outState.gyroX = ReadInt16LE(buf, kOffsetGyro + 0);
    outState.gyroY = ReadInt16LE(buf, kOffsetGyro + 2);
    outState.gyroZ = ReadInt16LE(buf, kOffsetGyro + 4);
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

// ---- Bluetooth: deliberately not implemented this pass -----------------------------
//
// A Bluetooth-connected DualSense uses report ID 0x31, a 78-byte input report with
// every offset above shifted by +1 extra byte (2 header bytes instead of 1 before the
// stick data starts, per the reference implementation's own
// evaluateHidInputBuffer(&hidBuffer[2], ...) call for BT vs [1] for USB), and writing
// ANY output report (rumble, lightbar) over Bluetooth requires a CRC32 checksum
// appended to the buffer or the controller silently ignores it. None of that is
// implemented here -- USB-only for this first pass, since that's the simpler, lower-
// risk path to verify live first. A BT-connected DualSense currently falls through to
// "no controller found" exactly as it did before this file existed; extending
// TryOpenDualSense's InputReportByteLength check to also accept 78 and adjusting the
// read-offsets by device connection type is the concrete next step if a live tester
// specifically needs Bluetooth support.
