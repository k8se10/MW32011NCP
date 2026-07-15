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
#include <cstring>
#include <vector>
#include <algorithm>
#include <iterator>

namespace {

// A full pointer scan re-reads the whole snapshot memory per candidate, so it must
// only ever run when there are few enough candidates left for that to finish quickly.
constexpr size_t kMaxPointerScanCandidates = 50;

struct Region {
    uintptr_t base;
    size_t size;
    std::vector<uint8_t> data;
};

struct Snapshot {
    std::vector<Region> regions; // kept sorted ascending by base (scan order guarantees this)
};

// Simple on-disk format for named/labeled snapshots: [u32 regionCount] then per region
// [u32 base][u32 size][size bytes]. Lets the user manually navigate to a specific,
// labeled state (e.g. "pause menu open") and snapshot it on demand, then have any two
// labeled snapshots diffed later -- much more precise than inferring state purely from
// blind key-press timing, which risks picking up coincidental background activity
// unrelated to the actual state change (see re_notes/known_issues.md #2, the Steam-data
// false lead from a timing-only ESC-press correlation).
bool SaveSnapshot(const Snapshot& snap, const char* path)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return false;
    uint32_t count = static_cast<uint32_t>(snap.regions.size());
    fwrite(&count, sizeof(count), 1, f);
    for (const auto& r : snap.regions) {
        uint32_t base = static_cast<uint32_t>(r.base);
        uint32_t size = static_cast<uint32_t>(r.size);
        fwrite(&base, sizeof(base), 1, f);
        fwrite(&size, sizeof(size), 1, f);
        fwrite(r.data.data(), 1, r.data.size(), f);
    }
    fclose(f);
    return true;
}

bool LoadSnapshot(Snapshot& snap, const char* path)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return false;
    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, f) != 1) { fclose(f); return false; }
    snap.regions.clear();
    snap.regions.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, size = 0;
        if (fread(&base, sizeof(base), 1, f) != 1) { fclose(f); return false; }
        if (fread(&size, sizeof(size), 1, f) != 1) { fclose(f); return false; }
        Region r;
        r.base = base;
        r.size = size;
        r.data.resize(size);
        if (size > 0 && fread(r.data.data(), 1, size, f) != size) { fclose(f); return false; }
        snap.regions.push_back(std::move(r));
    }
    fclose(f);
    return true;
}

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

// Pointer scan: given a candidate byte address whose VALUE correlates with a real
// input toggle (found by the transition-narrowing loop above), this finds every
// 4-byte-aligned dword anywhere in the snapshot that equals the BASE of the memory
// region containing that address. For a moving heap allocation, any hit low enough to
// be a static .data/.bss global (not another heap/stack address) is a candidate
// "stable pointer to the block" -- dereferencing it at runtime gives the current heap
// base without ever hardcoding the heap address itself.
void PointerScanForCandidate(const Snapshot& snap, uintptr_t candidateAddr)
{
    const Region* target = FindRegion(snap, candidateAddr);
    if (!target) {
        printf("  (pointer scan: containing region not found for 0x%08zX)\n", candidateAddr);
        return;
    }
    uintptr_t base = target->base;
    printf("  Pointer-scanning for references to containing region base 0x%08zX (size 0x%zX)...\n",
           base, target->size);

    std::vector<uintptr_t> hits;
    for (const auto& r : snap.regions) {
        if (r.size < 4) continue;
        size_t limit = r.size - 4;
        for (size_t i = 0; i <= limit; i += 4) {
            uint32_t v;
            memcpy(&v, &r.data[i], 4);
            if (v == static_cast<uint32_t>(base)) {
                hits.push_back(r.base + i);
            }
        }
    }

    printf("  Found %zu reference(s):\n", hits.size());
    for (uintptr_t h : hits) {
        printf("    0x%08zX%s\n", h, h < 0x02000000 ? "  <-- low/static, likely dereferenceable from our own code" : "");
    }
    if (hits.empty()) {
        printf("    (none -- base may not be stored as a raw pointer anywhere scannable,\n"
               "     or it's reached via a multi-level chain not visible in one scan)\n");
    }
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

// Edge-sequence mode: for one-shot commands (weapnext, togglemenu, toggleprone) that
// don't have a sustained "held" state for the held/released diff method above to lock
// onto. Reuses that EXACT same proven matching algorithm, though -- if you're cycling
// between exactly 2 weapons, weapnext alternates a weapon-index-like value between two
// fixed states on every press, exactly like a held/released toggle just triggered by
// discrete presses instead of a continuous hold. Press 1/2 seed the two expected values
// (evens must match press 2's value, odds press 1's), then every later press narrows
// the set exactly like the original loop does.
//
// Uses GetAsyncKeyState's low bit ("pressed since the last call", not "currently down")
// so a quick tap can't be missed while TakeSnapshot (a few hundred ms) is in progress --
// an earlier version used the high bit only and silently dropped most real presses.
void RunEdgeSequenceMode(HANDLE proc, int vk, const char* label)
{
    const int kMaxPresses = 14;
    printf("================================================================\n");
    printf(" EDGE-SEQUENCE mode for one-shot command '%s' (VK=0x%02X).\n"
           " Works best with exactly 2 weapons equipped, so it alternates between\n"
           " two states. Press %s exactly %d times, naturally, pausing briefly\n"
           " between each -- the tool exits on its own once it counts %d presses.\n",
           label, vk, label, kMaxPresses, kMaxPresses);
    printf("================================================================\n\n");

    GetAsyncKeyState(vk); // clear any stale "pressed since last call" latch before we start

    std::vector<Candidate> candidates;
    bool seeded = false;
    Snapshot seedA, seedB;
    int presses = 0;

    while (presses < kMaxPresses) {
        if (!(GetAsyncKeyState(vk) & 0x0001)) {
            Sleep(15);
            continue;
        }
        presses++;
        bool isOddPress = (presses % 2) == 1;
        printf("Press %d detected -- snapshotting...\n", presses);
        Snapshot snap = TakeSnapshot(proc);

        if (!seeded) {
            if (isOddPress) seedA = std::move(snap); else seedB = std::move(snap);
            if (presses == 2) {
                printf("Have both phases -- building initial candidate set...\n");
                for (const auto& ra : seedA.regions) {
                    const Region* rb = FindRegion(seedB, ra.base);
                    if (!rb || rb->size != ra.size) continue;
                    for (size_t i = 0; i < ra.size; i++) {
                        if (ra.data[i] != rb->data[i]) {
                            candidates.push_back({ ra.base + i, ra.data[i], rb->data[i] });
                        }
                    }
                }
                printf("Initial candidates: %zu\n", candidates.size());
                seeded = true;
            }
            continue;
        }

        size_t before = candidates.size();
        std::vector<Candidate> next;
        next.reserve(candidates.size());
        for (const auto& c : candidates) {
            uint8_t actual;
            if (!GetByte(snap, c.addr, actual)) continue;
            uint8_t want = isOddPress ? c.downVal : c.upVal;
            if (actual == want) next.push_back(c);
        }
        candidates = std::move(next);
        printf("  candidates: %zu -> %zu\n", before, candidates.size());
    }

    if (presses < 3) {
        printf("\nNot enough presses captured (got %d, need at least 3) -- try again.\n", presses);
        return;
    }

    printf("\n================ FINAL EDGE-SEQUENCE CANDIDATES (%zu) ================\n",
           candidates.size());
    for (const auto& c : candidates) {
        printf("  0x%08zX : odd-press=0x%02X even-press=0x%02X\n", c.addr, c.downVal, c.upVal);
    }
    if (candidates.empty()) {
        printf("  (none survived -- try again with more presses, or you may have more\n"
               "   or fewer than 2 weapons equipped, breaking the alternating-pair\n"
               "   assumption this mode relies on)\n");
    } else if (candidates.size() > kMaxPointerScanCandidates) {
        printf("\n(skipping pointer scan -- %zu candidates is too many to scan\n"
               " individually in reasonable time; run again with more presses to\n"
               " narrow further, or inspect the list above by hand)\n", candidates.size());
    } else {
        printf("\n================ POINTER SCAN ================\n");
        Snapshot finalSnap = TakeSnapshot(proc);
        for (const auto& c : candidates) {
            printf("\nCandidate 0x%08zX:\n", c.addr);
            PointerScanForCandidate(finalSnap, c.addr);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    // Optional args: <vkHex> <label> [edge]. Defaults to VK_RBUTTON / "ADS" (right
    // mouse, the default +toggleads_throw bind). For Sprint: memdiff.exe 10 Sprint
    // (VK_SHIFT). For one-shot commands with no sustained held state (weapnext,
    // togglemenu, toggleprone): memdiff.exe 31 weapnext edge (VK '1', edge-sequence
    // mode instead of held/released -- see RunEdgeSequenceMode).
    //
    // Special mode: memdiff.exe scan <addr1> [addr2 ...] -- pointer-scans specific
    // addresses directly against a fresh snapshot, without needing a new press
    // sequence. Used to dig into a subset of candidates already found by a previous
    // run (e.g. to check whether any of a large cluster of identical-looking
    // candidates has a distinctly stable/static reference, without re-running the
    // whole correlation phase).
    setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered -- lets progress be checked by
                                         // reading the redirected output file mid-run,
                                         // instead of only ever seeing it after exit

    if (argc >= 3 && _stricmp(argv[1], "scan") == 0) {
        printf("memdiff scan mode -- pointer-scanning %d address(es)\n", argc - 2);
        DWORD pid = FindProcessId(L"iw5sp.exe");
        if (pid == 0) {
            printf("iw5sp.exe not found -- launch the game first.\n");
            return 1;
        }
        HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!proc) {
            printf("OpenProcess failed (%lu) -- run this tool as Administrator.\n", GetLastError());
            return 1;
        }
        printf("Taking snapshot...\n");
        Snapshot snap = TakeSnapshot(proc);
        for (int i = 2; i < argc; i++) {
            uintptr_t addr = static_cast<uintptr_t>(strtoull(argv[i], nullptr, 16));
            printf("\nAddress 0x%08zX:\n", addr);
            PointerScanForCandidate(snap, addr);
        }
        CloseHandle(proc);
        printf("\nDone.\n");
        return 0;
    }

    // Special mode: memdiff.exe dump <addr> <length> -- hex + ASCII dump of raw bytes
    // at a live address, e.g. to look for an embedded name/label string next to a
    // pointer slot found by "scan" mode.
    if (argc >= 4 && _stricmp(argv[1], "dump") == 0) {
        uintptr_t addr = static_cast<uintptr_t>(strtoull(argv[2], nullptr, 16));
        size_t len = static_cast<size_t>(strtoul(argv[3], nullptr, 10));
        DWORD pid = FindProcessId(L"iw5sp.exe");
        if (pid == 0) {
            printf("iw5sp.exe not found -- launch the game first.\n");
            return 1;
        }
        HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!proc) {
            printf("OpenProcess failed (%lu) -- run this tool as Administrator.\n", GetLastError());
            return 1;
        }
        std::vector<uint8_t> buf(len);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addr), buf.data(), len, &bytesRead)) {
            printf("ReadProcessMemory failed (%lu)\n", GetLastError());
            CloseHandle(proc);
            return 1;
        }
        for (size_t row = 0; row < bytesRead; row += 16) {
            printf("0x%08zX: ", addr + row);
            size_t rowLen = (bytesRead - row) < 16 ? (bytesRead - row) : 16;
            for (size_t i = 0; i < 16; i++) {
                if (i < rowLen) printf("%02X ", buf[row + i]); else printf("   ");
            }
            printf(" ");
            for (size_t i = 0; i < rowLen; i++) {
                uint8_t c = buf[row + i];
                putchar((c >= 0x20 && c < 0x7f) ? c : '.');
            }
            printf("\n");
        }
        CloseHandle(proc);
        return 0;
    }

    // Special mode: memdiff.exe snapshot <name> -- takes ONE snapshot right now and
    // saves it to "<name>.snap". Meant for manual, labeled state capture: navigate to
    // an exact state in-game (e.g. "pause menu open"), then run this to save it, move
    // to the next state, save again under a different name, etc.
    if (argc >= 3 && _stricmp(argv[1], "snapshot") == 0) {
        DWORD pid = FindProcessId(L"iw5sp.exe");
        if (pid == 0) {
            printf("iw5sp.exe not found -- launch the game first.\n");
            return 1;
        }
        HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!proc) {
            printf("OpenProcess failed (%lu) -- run this tool as Administrator.\n", GetLastError());
            return 1;
        }
        printf("Taking snapshot...\n");
        Snapshot snap = TakeSnapshot(proc);
        char path[MAX_PATH];
        sprintf_s(path, "%s.snap", argv[2]);
        if (SaveSnapshot(snap, path)) {
            printf("Saved %s (%zu regions)\n", path, snap.regions.size());
        } else {
            printf("Failed to save %s\n", path);
        }
        CloseHandle(proc);
        return 0;
    }

    // Special mode: memdiff.exe diff <nameA> <nameB> -- loads two saved snapshots and
    // reports every byte address that differs between them, with both values. No
    // narrowing/candidate logic -- meant for comparing two precisely labeled states
    // (e.g. "pause_open.snap" vs "pause_closed.snap") where the state difference is
    // already known and controlled, unlike the press-timing-based modes above.
    if (argc >= 4 && _stricmp(argv[1], "diff") == 0) {
        char pathA[MAX_PATH], pathB[MAX_PATH];
        sprintf_s(pathA, "%s.snap", argv[2]);
        sprintf_s(pathB, "%s.snap", argv[3]);
        Snapshot a, b;
        if (!LoadSnapshot(a, pathA)) { printf("Failed to load %s\n", pathA); return 1; }
        if (!LoadSnapshot(b, pathB)) { printf("Failed to load %s\n", pathB); return 1; }
        printf("Diffing %s (%zu regions) vs %s (%zu regions)...\n",
               pathA, a.regions.size(), pathB, b.regions.size());
        size_t diffCount = 0;
        constexpr size_t kMaxPrint = 300;
        for (const auto& rb : b.regions) {
            const Region* ra = FindRegion(a, rb.base);
            if (!ra || ra->size != rb.size) continue;
            for (size_t i = 0; i < rb.size; i++) {
                if (ra->data[i] != rb.data[i]) {
                    diffCount++;
                    if (diffCount <= kMaxPrint) {
                        printf("  0x%08zX : %s=0x%02X %s=0x%02X\n", rb.base + i,
                               argv[2], ra->data[i], argv[3], rb.data[i]);
                    }
                }
            }
        }
        printf("\nTotal differing bytes: %zu%s\n", diffCount,
               diffCount > kMaxPrint ? " (only first 300 printed)" : "");
        return 0;
    }

    // Special mode: memdiff.exe correlate <name1> <name2> <name3> ... -- loads an
    // alternating sequence of labeled snapshots (odd positions = state A, even
    // positions = state B, e.g. closed1 open1 closed2 open2 closed3 open3) and applies
    // the SAME proven candidate-narrowing algorithm as the live held/released mode
    // above, just against pre-saved, manually-confirmed states instead of inferring
    // state from key-press timing. Combines the precision of labeled snapshots (no
    // risk of a coincidental background-activity correlation, see the Steam-data false
    // lead in re_notes/known_issues.md #2) with the statistical robustness repeated
    // transitions give against this game's very high per-frame memory volatility
    // (13M+ bytes differ between two otherwise-identical "closed" snapshots just
    // seconds apart -- a single before/after diff alone is hopeless here).
    if (argc >= 4 && _stricmp(argv[1], "correlate") == 0) {
        int n = argc - 2;
        std::vector<Snapshot> snaps(n);
        for (int i = 0; i < n; i++) {
            char path[MAX_PATH];
            sprintf_s(path, "%s.snap", argv[2 + i]);
            if (!LoadSnapshot(snaps[i], path)) {
                printf("Failed to load %s\n", path);
                return 1;
            }
            printf("Loaded %s (%s, %zu regions)\n", path, (i % 2 == 0) ? "state A" : "state B",
                   snaps[i].regions.size());
        }

        std::vector<Candidate> candidates;
        for (const auto& ra : snaps[0].regions) {
            const Region* rb = FindRegion(snaps[1], ra.base);
            if (!rb || rb->size != ra.size) continue;
            for (size_t i = 0; i < ra.size; i++) {
                if (ra.data[i] != rb->data[i]) {
                    candidates.push_back({ ra.base + i, ra.data[i], rb->data[i] });
                }
            }
        }
        printf("Initial candidates (from %s vs %s): %zu\n", argv[2], argv[3], candidates.size());

        for (int i = 2; i < n; i++) {
            bool isStateA = (i % 2 == 0);
            size_t before = candidates.size();
            std::vector<Candidate> next;
            next.reserve(candidates.size());
            for (const auto& c : candidates) {
                uint8_t actual;
                if (!GetByte(snaps[i], c.addr, actual)) continue;
                uint8_t want = isStateA ? c.downVal : c.upVal;
                if (actual == want) next.push_back(c);
            }
            candidates = std::move(next);
            printf("  after %s (%s): %zu -> %zu\n", argv[2 + i], isStateA ? "state A" : "state B",
                   before, candidates.size());
        }

        printf("\n================ FINAL CORRELATE CANDIDATES (%zu) ================\n",
               candidates.size());
        for (const auto& c : candidates) {
            printf("  0x%08zX : stateA=0x%02X stateB=0x%02X\n", c.addr, c.downVal, c.upVal);
        }
        if (!candidates.empty() && candidates.size() <= kMaxPointerScanCandidates) {
            printf("\n================ POINTER SCAN ================\n");
            for (const auto& c : candidates) {
                printf("\nCandidate 0x%08zX:\n", c.addr);
                PointerScanForCandidate(snaps.back(), c.addr);
            }
        } else if (!candidates.empty()) {
            printf("\n(skipping pointer scan -- %zu candidates is too many)\n", candidates.size());
        }
        return 0;
    }

    int vk = VK_RBUTTON;
    const char* label = "ADS";
    bool edgeMode = false;
    if (argc >= 2) vk = static_cast<int>(strtol(argv[1], nullptr, 16));
    if (argc >= 3) label = argv[2];
    if (argc >= 4 && _stricmp(argv[3], "edge") == 0) edgeMode = true;

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

    if (edgeMode) {
        RunEdgeSequenceMode(proc, vk, label);
        CloseHandle(proc);
        printf("\nDone.\n");
        return 0;
    }

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
    } else if (candidates.size() > kMaxPointerScanCandidates) {
        printf("\n(skipping pointer scan -- %zu candidates is too many to scan\n"
               " individually in reasonable time; keep toggling for more confidence,\n"
               " or inspect the list above by hand)\n", candidates.size());
    } else {
        printf("\n================ POINTER SCAN ================\n");
        printf("Taking a fresh snapshot to scan for stable references to each candidate's\n"
               "containing memory region (moving-heap-address -> static-pointer lookup)...\n");
        Snapshot finalSnap = TakeSnapshot(proc);
        for (const auto& c : candidates) {
            printf("\nCandidate 0x%08zX:\n", c.addr);
            PointerScanForCandidate(finalSnap, c.addr);
        }
    }

    CloseHandle(proc);
    printf("\nDone.\n");
    return 0;
}
