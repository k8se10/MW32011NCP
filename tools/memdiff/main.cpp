// memdiff — dev-only diagnostic tool (not part of the shipped mod).
//
// Manual mode: YOU toggle ADS (right mouse button) naturally in-game, at your own pace,
// as many times as you like. The tool watches GetAsyncKeyState(VK_RBUTTON) and takes a
// full memory snapshot on every press/release edge, then narrows down to bytes whose
// value is *consistently* one thing while ADS is held and consistently a different thing
// while it's not -- across every real transition you make, not a fixed synthetic timing
// window. This sidesteps the earlier automated-hold approach's problem: real gameplay
// (asset streaming, checkpoint reloads, taking damage) doesn't happen on our schedule, so
// letting a human drive it naturally and just watching is more robust than us guessing
// hold/settle durations.
//
// Used for task #10 (usercmd.buttons mapping) to locate ADS (+toggleads_throw) state,
// which static analysis of the known usercmd-pipeline functions ruled out (see
// re_notes/iw5sp.md, "ADS -- ruled out of this struct entirely").

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace {

struct Region {
    uintptr_t base;
    size_t size;
    std::vector<uint8_t> data;
};

struct Snapshot {
    std::vector<Region> regions; // kept sorted ascending by base (scan order guarantees this)
};

DWORD FindProcessId(const wchar_t* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

struct FindWindowCtx {
    DWORD pid;
    HWND result;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    FindWindowCtx* ctx = reinterpret_cast<FindWindowCtx*>(lParam);
    DWORD winPid = 0;
    GetWindowThreadProcessId(hwnd, &winPid);
    if (winPid == ctx->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        ctx->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindMainWindow(DWORD pid)
{
    FindWindowCtx ctx{ pid, nullptr };
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

bool ShouldScan(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_NOACCESS) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    if (mbi.RegionSize > 32 * 1024 * 1024) return false;
    return true;
}

Snapshot TakeSnapshot(HANDLE proc)
{
    Snapshot snap;
    uintptr_t addr = 0x00010000;
    const uintptr_t kMaxAddr = 0x7FFF0000;
    size_t totalBytes = 0;
    const size_t kTotalCap = 400ull * 1024 * 1024;

    while (addr < kMaxAddr) {
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T res = VirtualQueryEx(proc, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi));
        if (res == 0) break;

        if (ShouldScan(mbi) && totalBytes + mbi.RegionSize <= kTotalCap) {
            Region r;
            r.base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            r.size = mbi.RegionSize;
            r.data.resize(r.size);
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(proc, mbi.BaseAddress, r.data.data(), r.size, &bytesRead) && bytesRead == r.size) {
                totalBytes += r.size;
                snap.regions.push_back(std::move(r)); // ascending base order guaranteed by scan order
            }
        }

        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return snap;
}

// Binary search over ascending-sorted regions.
const Region* FindRegion(const Snapshot& snap, uintptr_t addr)
{
    auto it = std::upper_bound(snap.regions.begin(), snap.regions.end(), addr,
        [](uintptr_t a, const Region& r) { return a < r.base; });
    if (it == snap.regions.begin()) return nullptr;
    --it;
    if (addr >= it->base && addr < it->base + it->size) return &(*it);
    return nullptr;
}

bool GetByte(const Snapshot& snap, uintptr_t addr, uint8_t& out)
{
    const Region* r = FindRegion(snap, addr);
    if (!r) return false;
    out = r->data[addr - r->base];
    return true;
}

constexpr uintptr_t kInLevelFlagAddr = 0x00A98ACC;

bool IsInLevel(HANDLE proc)
{
    int32_t val = 0;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(kInLevelFlagAddr), &val, sizeof(val), &bytesRead)) {
        return false;
    }
    return bytesRead == sizeof(val) && val > 0;
}

struct Candidate {
    uintptr_t addr;
    uint8_t downVal;
    uint8_t upVal;
};

} // namespace

int main(int argc, char** argv)
{
    // Optional args: <vkHex> <label>. Defaults to VK_RBUTTON / "ADS" (right mouse,
    // the default +toggleads_throw bind). For Sprint, run with: memdiff.exe 10 Sprint
    // (VK_SHIFT is 0x10, the default +breath_sprint bind).
    int vk = VK_RBUTTON;
    const char* label = "ADS";
    if (argc >= 2) vk = static_cast<int>(strtol(argv[1], nullptr, 16));
    if (argc >= 3) label = argv[2];

    printf("memdiff (manual mode) -- toggle %s yourself, I watch and correlate (VK=0x%02X)\n", label, vk);
    printf("Looking for iw5sp.exe...\n");

    DWORD pid = 0;
    while (pid == 0) {
        pid = FindProcessId(L"iw5sp.exe");
        if (pid == 0) {
            printf("  not found yet, retrying in 2s (launch the game now)...\n");
            Sleep(2000);
        }
    }
    printf("Found iw5sp.exe, PID %lu\n", pid);

    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) {
        printf("OpenProcess failed (%lu) -- run this tool as Administrator.\n", GetLastError());
        return 1;
    }

    printf("\nWaiting for a level to actually be loaded...\n");
    while (!IsInLevel(proc)) {
        Sleep(500);
    }
    printf("Level detected. Ready.\n\n");
    printf("================================================================\n");
    printf(" Toggle %s naturally, as many times as you like -- short taps\n"
           " or long holds, doesn't matter. I'll watch and take a snapshot\n"
           " on every press/release. Aim for at least 6-8 toggles for a\n"
           " solid result. Press F11 at any time to stop early and see the\n"
           " current results.\n", label);
    printf("================================================================\n\n");

    bool lastDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
    printf("Starting state: %s is currently %s\n", label, lastDown ? "HELD" : "released");

    std::vector<Candidate> candidates;
    bool seeded = false;
    Snapshot seedDown, seedUp;
    bool haveSeedDown = lastDown, haveSeedUp = !lastDown;
    if (lastDown) seedDown = TakeSnapshot(proc); else seedUp = TakeSnapshot(proc);

    int transitions = 0;
    const int kMaxTransitions = 24;

    while (transitions < kMaxTransitions) {
        if (GetAsyncKeyState(VK_F11) & 0x8000) {
            printf("\nF11 pressed -- stopping early.\n");
            break;
        }
        bool nowDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (nowDown == lastDown) {
            Sleep(15);
            continue;
        }
        lastDown = nowDown;
        transitions++;
        printf("Transition %d: %s now %s -- snapshotting...\n", transitions, label, nowDown ? "HELD" : "released");
        Snapshot snap = TakeSnapshot(proc);

        if (!seeded) {
            if (nowDown && !haveSeedDown) { seedDown = std::move(snap); haveSeedDown = true; }
            else if (!nowDown && !haveSeedUp) { seedUp = std::move(snap); haveSeedUp = true; }
            if (haveSeedDown && haveSeedUp) {
                printf("Have both a HELD and released snapshot -- building initial candidate set...\n");
                for (const auto& rd : seedDown.regions) {
                    const Region* ru = FindRegion(seedUp, rd.base);
                    if (!ru || ru->size != rd.size) continue;
                    for (size_t i = 0; i < rd.size; i++) {
                        if (rd.data[i] != ru->data[i]) {
                            candidates.push_back({ rd.base + i, rd.data[i], ru->data[i] });
                        }
                    }
                }
                printf("Initial candidates: %zu\n", candidates.size());
                seeded = true;
            }
            continue;
        }

        uint8_t expected;
        size_t before = candidates.size();
        std::vector<Candidate> next;
        next.reserve(candidates.size());
        for (const auto& c : candidates) {
            uint8_t actual;
            if (!GetByte(snap, c.addr, actual)) continue; // region gone this snapshot, drop
            uint8_t want = nowDown ? c.downVal : c.upVal;
            if (actual == want) next.push_back(c);
        }
        candidates = std::move(next);
        printf("  candidates: %zu -> %zu\n", before, candidates.size());

        if (candidates.size() <= 30) {
            printf("  (narrow enough to inspect -- feel free to press F11 to stop, or keep\n"
                   "   toggling for even more confidence)\n");
        }
    }

    printf("\n================ FINAL CANDIDATES (%zu) ================\n", candidates.size());
    for (const auto& c : candidates) {
        printf("  0x%08zX : held=0x%02X released=0x%02X\n", c.addr, c.downVal, c.upVal);
    }
    if (candidates.empty()) {
        printf("  (none survived -- either too few transitions, or ADS state isn't at a\n"
               "   fixed address at all)\n");
    }

    CloseHandle(proc);
    printf("\nDone.\n");
    return 0;
}
