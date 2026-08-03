// xenia_probe -- LB+RB bookmark-triggered memory snapshotter for Xenia (Xbox 360 emulator).
//
// Purpose: MW32011NCP has had to approximate several console-only values on the PC build
// (look acceleration ramp, ADS slowdown curve, deadzone, vibration timing/intensity) since
// the PC binary never had native controller support to read real values FROM. Xenia running
// the real Xbox 360 build of MW3 is a ground-truth reference for all of these, and for the
// real native controller-options menu's own layout/design (2026-08-03 request: a "modernised
// stylised version of the original").
//
// Xenia is a 64-bit process; the guest's 512MB physical RAM is mapped at multiple aliased
// virtual addresses inside Xenia's own address space (confirmed live, 2026-08-03: three
// distinct 512MB PAGE_READWRITE regions read back byte-identical at the same relative
// offset). memdiff.exe (this project's existing tool) can't be reused as-is: it's a 32-bit
// build hardcoded to iw5sp.exe and to 32-bit addresses/offsets.
//
// CRITICAL: Xbox 360 is PowerPC, big-endian. A live x64 Windows process (this tool, and
// Xenia's own host process) is little-endian. Raw bytes read from the guest RAM alias are
// in the GUEST's byte order, not the host's -- a 32-bit value the game sees as decimal 10
// is stored as bytes 00 00 00 0A, not 0A 00 00 00. Every multi-byte interpretation this tool
// prints does an explicit byte-swap before treating raw bytes as a number.
//
// Usage:
//   xenia_probe.exe watch [outPrefix]
//     Finds xenia_canary.exe, polls a real XInput controller (shared/non-exclusive read --
//     does not interfere with Xenia's own input handling) for an LB+RB rising edge, and
//     saves a full snapshot of the guest RAM region to <outPrefix>_NNN.snap on each press.
//     Play naturally on Xenia; hit LB+RB whenever you want a moment bookmarked (e.g. "the
//     sensitivity slider is at 5 right now", "I just entered ADS", "the mantle prompt is up").
//     Note what each numbered snapshot corresponds to as you go -- this tool has no way to
//     know what LB+RB means to you at that moment, only that you pressed it.
//
//   xenia_probe.exe diff <snapA> <snapB>
//     Byte-level diff between two snapshots (paths without the .snap extension), printing
//     every changed offset as: raw bytes (both orders shown), byte-swapped uint32, and
//     byte-swapped float, so a real console value doesn't have to be manually re-derived
//     by hand every time.

#include <windows.h>
#include <xinput.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

// ---- Paired screenshot capture (2026-08-03, user request: "just ss like we did on pc") ----
//
// Rather than relying on the user narrating what each LB+RB bookmark means, capture a real
// screenshot of Xenia's own window at the exact same moment as the memory snapshot -- the
// screenshot documents the visual/menu state directly, no manual notes needed.

struct FindWindowContext {
    DWORD pid;
    HWND result;
};

BOOL CALLBACK EnumWindowsForPid(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<FindWindowContext*>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != ctx->pid) return TRUE; // keep enumerating
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindowTextLengthA(hwnd) == 0) return TRUE; // skip untitled helper windows
    ctx->result = hwnd;
    return FALSE; // found it, stop
}

HWND FindMainWindowForProcess(DWORD pid)
{
    FindWindowContext ctx{ pid, nullptr };
    EnumWindows(EnumWindowsForPid, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

// Writes a captured HBITMAP straight to a 24-bit BMP file -- no GDI+/WIC dependency needed
// for something this simple.
bool SaveHBitmapAsBmp(HBITMAP hbmp, HDC hdc, int width, int height, const char* path)
{
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height; // negative = top-down DIB, matches natural screen row order
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    DWORD rowSize = ((width * 3 + 3) / 4) * 4; // rows padded to 4-byte boundary, BMP requirement
    DWORD imageSize = rowSize * height;
    std::vector<uint8_t> pixels(imageSize);

    if (!GetDIBits(hdc, hbmp, 0, height, pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS)) {
        return false;
    }

    BITMAPFILEHEADER bf{};
    bf.bfType = 0x4D42; // 'BM'
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfSize = bf.bfOffBits + imageSize;

    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return false;
    fwrite(&bf, sizeof(bf), 1, f);
    fwrite(&bi, sizeof(bi), 1, f);
    fwrite(pixels.data(), 1, pixels.size(), f);
    fclose(f);
    return true;
}

bool CaptureWindow(HWND hwnd, const char* path)
{
    if (!hwnd) return false;
    RECT rect{};
    if (!GetClientRect(hwnd, &rect)) return false;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;

    HDC hdcWindow = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hbmp = CreateCompatibleBitmap(hdcWindow, width, height);
    HGDIOBJ oldObj = SelectObject(hdcMem, hbmp);

    // PW_RENDERFULLCONTENT (Windows 8.1+): needed for GPU-rendered content (D3D/Vulkan
    // swapchains) that plain BitBlt often captures as black -- Xenia renders via GPU.
    BOOL printed = PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);
    bool saved = false;
    if (printed) {
        saved = SaveHBitmapAsBmp(hbmp, hdcMem, width, height, path);
    }

    SelectObject(hdcMem, oldObj);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);
    return saved;
}

// Found empirically 2026-08-03 against this session's live xenia_canary.exe -- one of three
// confirmed-identical 512MB aliases of the guest's physical RAM. May shift between Xenia
// versions/runs; if this tool reports the region unreadable, re-scan (see re_notes -- the
// three candidates were found as the only PAGE_READWRITE, MEM_PRIVATE regions sized exactly
// 0x20000000 bytes).
constexpr uint64_t kDefaultGuestRamBase = 0x1C0000000ULL;
constexpr uint64_t kGuestRamSize = 0x20000000ULL; // 512MB, the Xbox 360's real physical RAM size

DWORD FindProcessId(const wchar_t* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool ReadGuestRam(HANDLE proc, uint64_t base, std::vector<uint8_t>& out)
{
    out.resize(static_cast<size_t>(kGuestRamSize));
    SIZE_T bytesRead = 0;
    BOOL ok = ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base), out.data(), out.size(), &bytesRead);
    if (!ok || bytesRead != out.size()) {
        printf("ReadProcessMemory failed or short read (ok=%d, read=%zu of %zu, err=%lu)\n",
            ok, static_cast<size_t>(bytesRead), out.size(), GetLastError());
        return false;
    }
    return true;
}

bool SaveSnapshot(const std::vector<uint8_t>& data, const char* path)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return false;
    size_t written = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return written == data.size();
}

bool LoadSnapshot(std::vector<uint8_t>& data, const char* path)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }
    data.resize(static_cast<size_t>(size));
    size_t readBytes = fread(data.data(), 1, data.size(), f);
    fclose(f);
    return readBytes == data.size();
}

uint32_t Swap32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

int RunWatch(const std::string& outPrefix)
{
    printf("xenia_probe (watch mode) -- LB+RB bookmarks a guest-RAM snapshot\n");
    printf("Looking for xenia_canary.exe...\n");

    DWORD pid = 0;
    while (pid == 0) {
        pid = FindProcessId(L"xenia_canary.exe");
        if (pid == 0) {
            pid = FindProcessId(L"xenia.exe"); // non-canary build, just in case
        }
        if (pid == 0) {
            printf("  not found yet, retrying in 2s (launch Xenia now)...\n");
            Sleep(2000);
        }
    }
    printf("Found Xenia, PID %lu\n", pid);

    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) {
        printf("OpenProcess failed (%lu) -- run this tool as Administrator.\n", GetLastError());
        return 1;
    }

    // Sanity check: confirm the guest RAM region is actually readable before starting the
    // watch loop, so a bad base address fails loudly and immediately rather than silently
    // saving 512MB of garbage/zeros on every future bookmark.
    {
        std::vector<uint8_t> probe;
        if (!ReadGuestRam(proc, kDefaultGuestRamBase, probe)) {
            printf("Guest RAM base 0x%llX is not readable -- re-scan for the real alias "
                "(see this tool's own top comment) and pass it as a 3rd command-line arg.\n",
                static_cast<unsigned long long>(kDefaultGuestRamBase));
            CloseHandle(proc);
            return 1;
        }
        printf("Guest RAM confirmed readable at 0x%llX (%llu MB).\n",
            static_cast<unsigned long long>(kDefaultGuestRamBase),
            static_cast<unsigned long long>(kGuestRamSize / (1024 * 1024)));
    }

    HWND xeniaWindow = FindMainWindowForProcess(pid);
    if (xeniaWindow) {
        printf("Xenia window found -- each bookmark will also save a paired screenshot.\n");
    } else {
        printf("WARNING: couldn't find Xenia's window -- bookmarks will save memory only,\n"
               "         no screenshot. (Menu/glyph state can't be visually reviewed.)\n");
    }

    printf("================================================================\n");
    printf(" Play naturally. Hold LB+RB together on the controller whenever\n"
           " you want THIS MOMENT bookmarked -- memory + a screenshot are both\n"
           " saved automatically, no notes needed. Ctrl+C to stop.\n");
    printf("================================================================\n\n");

    int snapIndex = 0;
    bool lastChordHeld = false;

    while (true) {
        XINPUT_STATE state{};
        DWORD res = XInputGetState(0, &state); // shared/non-exclusive read -- does not
                                                 // interfere with Xenia's own polling of
                                                 // the same physical controller
        bool chordHeld = (res == ERROR_SUCCESS) &&
            (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) &&
            (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);

        if (chordHeld && !lastChordHeld) {
            ++snapIndex;
            char snapPath[512], imgPath[512];
            sprintf_s(snapPath, "%s_%03d.snap", outPrefix.c_str(), snapIndex);
            sprintf_s(imgPath, "%s_%03d.bmp", outPrefix.c_str(), snapIndex);
            printf("[%03d] LB+RB pressed -- capturing...\n", snapIndex);

            // Screenshot first (near-instant) so it lines up as closely as possible with
            // the moment the button was actually pressed -- the 512MB memory read below
            // takes measurably longer.
            bool gotImage = xeniaWindow && CaptureWindow(xeniaWindow, imgPath);

            std::vector<uint8_t> data;
            bool gotSnap = ReadGuestRam(proc, kDefaultGuestRamBase, data) && SaveSnapshot(data, snapPath);

            if (gotSnap) {
                printf("[%03d] memory saved to %s (%llu MB)%s\n", snapIndex, snapPath,
                    static_cast<unsigned long long>(data.size() / (1024 * 1024)),
                    gotImage ? "" : " -- screenshot FAILED, memory only");
            } else {
                printf("[%03d] FAILED to snapshot/save memory -- Xenia may have exited or\n"
                       "      the guest RAM base shifted.\n", snapIndex);
            }
            if (gotImage) {
                printf("[%03d] screenshot saved to %s\n", snapIndex, imgPath);
            }
        }
        lastChordHeld = chordHeld;
        Sleep(16); // ~60Hz poll, matches this project's own established controller-poll rate
    }
}

int RunDiff(const std::string& pathA, const std::string& pathB)
{
    std::vector<uint8_t> a, b;
    std::string fileA = pathA + ".snap";
    std::string fileB = pathB + ".snap";
    if (!LoadSnapshot(a, fileA.c_str())) { printf("Failed to load %s\n", fileA.c_str()); return 1; }
    if (!LoadSnapshot(b, fileB.c_str())) { printf("Failed to load %s\n", fileB.c_str()); return 1; }
    if (a.size() != b.size()) {
        printf("Snapshot size mismatch: %s=%zu bytes, %s=%zu bytes\n",
            fileA.c_str(), a.size(), fileB.c_str(), b.size());
        return 1;
    }

    printf("Diffing %s vs %s (%zu bytes each)...\n\n", fileA.c_str(), fileB.c_str(), a.size());

    size_t diffCount = 0;
    constexpr size_t kMaxPrinted = 500; // enough to eyeball, not enough to flood the console
                                          // on a genuinely large-scale change (e.g. a full
                                          // level transition) -- rerun on a narrower bookmark
                                          // pair if this cap is hit
    for (size_t i = 0; i + 4 <= a.size(); ++i) {
        if (a[i] == b[i]) continue;

        uint32_t rawA, rawB;
        memcpy(&rawA, &a[i], 4);
        memcpy(&rawB, &b[i], 4);
        uint32_t beA = Swap32(rawA); // guest data is big-endian; this host is little-endian
        uint32_t beB = Swap32(rawB);
        float floatA, floatB;
        memcpy(&floatA, &beA, 4);
        memcpy(&floatB, &beB, 4);

        if (diffCount < kMaxPrinted) {
            printf("+0x%08zX : %02X -> %02X | as-BE-uint32: %u -> %u | as-BE-float: %g -> %g\n",
                i, a[i], b[i], beA, beB, floatA, floatB);
        }
        ++diffCount;
    }

    printf("\nTotal changed bytes: %zu%s\n", diffCount,
        diffCount > kMaxPrinted ? " (truncated printout -- narrow the bookmark pair for a cleaner diff)" : "");
    return 0;
}

// findexact -- scans N snapshots for 4-byte-aligned offsets whose byte-swapped uint32
// value matches an exact expected value in EACH corresponding snapshot. Built for
// triple-matching a known HUD value (e.g. Armor 250/250/201) across three bookmarks,
// since a raw pairwise diff of two live-gameplay snapshots is worthless -- tens of
// millions of bytes change every few seconds from audio/physics/animation/streaming
// alone, unrelated to the one value being tracked.
int RunFindExact(const std::vector<std::string>& paths, const std::vector<uint32_t>& expected)
{
    if (paths.size() != expected.size() || paths.size() < 2) {
        printf("findexact needs >=2 snapshot/value pairs\n");
        return 1;
    }
    std::vector<std::vector<uint8_t>> snaps(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        std::string file = paths[i] + ".snap";
        if (!LoadSnapshot(snaps[i], file.c_str())) { printf("Failed to load %s\n", file.c_str()); return 1; }
        printf("Loaded %s (%zu bytes), expecting BE-uint32 == %u\n", file.c_str(), snaps[i].size(), expected[i]);
    }
    size_t minSize = snaps[0].size();
    for (auto& s : snaps) minSize = (s.size() < minSize) ? s.size() : minSize;

    printf("\nScanning %zu bytes (UNALIGNED, widths 1/2/4) for exact matches across all %zu snapshots...\n",
        minSize, snaps.size());
    size_t matches1 = 0, matches2 = 0, matches4 = 0;
    for (size_t off = 0; off + 4 <= minSize; ++off) {
        // width 1 (byte)
        bool m1 = true;
        for (size_t i = 0; i < snaps.size() && m1; ++i) {
            if (snaps[i][off] != static_cast<uint8_t>(expected[i])) m1 = false;
        }
        if (m1) {
            printf("MATCH(u8)  at +0x%08zX\n", off);
            ++matches1;
        }
        // width 2 (big-endian uint16)
        bool m2 = true;
        for (size_t i = 0; i < snaps.size() && m2; ++i) {
            uint16_t raw; memcpy(&raw, &snaps[i][off], 2);
            uint16_t be = static_cast<uint16_t>((raw << 8) | (raw >> 8));
            if (be != static_cast<uint16_t>(expected[i])) m2 = false;
        }
        if (m2) {
            printf("MATCH(u16) at +0x%08zX\n", off);
            ++matches2;
        }
        // width 4 (big-endian uint32), now unaligned too
        bool m4 = true;
        for (size_t i = 0; i < snaps.size() && m4; ++i) {
            uint32_t raw; memcpy(&raw, &snaps[i][off], 4);
            if (Swap32(raw) != expected[i]) m4 = false;
        }
        if (m4) {
            printf("MATCH(u32) at +0x%08zX\n", off);
            ++matches4;
        }
        // width 4 raw (NOT byte-swapped) -- sanity check in case this particular field is
        // written by something that keeps host/little-endian order despite the guest CPU
        // being big-endian overall (e.g. a value copied in from an already-swapped source)
        bool m4raw = true;
        for (size_t i = 0; i < snaps.size() && m4raw; ++i) {
            uint32_t raw; memcpy(&raw, &snaps[i][off], 4);
            if (raw != expected[i]) m4raw = false;
        }
        if (m4raw) {
            printf("MATCH(u32-RAW-no-swap) at +0x%08zX\n", off);
        }
    }
    printf("\nTotal matches: u8=%zu u16=%zu u32=%zu\n", matches1, matches2, matches4);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered -- this tool's status/bookmark lines
                                          // need to appear live when stdout is redirected to
                                          // a file (background run), not sit in a stdio
                                          // buffer until exit
    if (argc >= 2 && _stricmp(argv[1], "watch") == 0) {
        std::string prefix = (argc >= 3) ? argv[2] : "xenia_mark";
        return RunWatch(prefix);
    }
    if (argc >= 4 && _stricmp(argv[1], "diff") == 0) {
        return RunDiff(argv[2], argv[3]);
    }
    if (argc >= 2 && _stricmp(argv[1], "findexact") == 0) {
        // Usage: findexact <snap1> <val1> <snap2> <val2> ...
        std::vector<std::string> paths;
        std::vector<uint32_t> expected;
        for (int i = 2; i + 1 < argc; i += 2) {
            paths.push_back(argv[i]);
            expected.push_back(static_cast<uint32_t>(strtoul(argv[i + 1], nullptr, 10)));
        }
        return RunFindExact(paths, expected);
    }
    printf("Usage:\n");
    printf("  xenia_probe.exe watch [outPrefix]\n");
    printf("  xenia_probe.exe diff <snapA> <snapB>\n");
    printf("  xenia_probe.exe findexact <snap1> <val1> [<snap2> <val2> ...]\n");
    return 1;
}
