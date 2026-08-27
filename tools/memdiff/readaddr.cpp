// readaddr.cpp -- dev-only, standalone. Reads a 4-byte int32 at a given virtual
// address out of one or more saved memdiff .snap files (the same [u32 regionCount]
// then per-region [u32 base][u32 size][size bytes] format LoadSnapshot in main.cpp
// already reads live process memory into). Built for the motion-blur menu/loading/
// cutscene shared-root investigation (known_issues.md issue #100) -- correlating
// clcState (0x00B36218) across a batch of livedump_NNN.snap captures without
// needing the game running or a live debugger attach.
//
// Usage: readaddr.exe <addrHex> <file1.snap> [file2.snap ...]

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

namespace {

struct Region {
    uint32_t base;
    uint32_t size;
    std::vector<uint8_t> data;
};

bool LoadSnapshot(std::vector<Region>& regions, const char* path)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return false;
    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, f) != 1) { fclose(f); return false; }
    regions.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        Region& r = regions[i];
        if (fread(&r.base, sizeof(r.base), 1, f) != 1) { fclose(f); return false; }
        if (fread(&r.size, sizeof(r.size), 1, f) != 1) { fclose(f); return false; }
        r.data.resize(r.size);
        if (r.size > 0 && fread(r.data.data(), 1, r.size, f) != r.size) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: readaddr.exe <addrHex> <file1.snap> [file2.snap ...]\n");
        return 1;
    }
    uint32_t target = static_cast<uint32_t>(strtoul(argv[1], nullptr, 16));

    for (int i = 2; i < argc; i++) {
        std::vector<Region> regions;
        if (!LoadSnapshot(regions, argv[i])) {
            printf("%s\tLOAD_FAILED\n", argv[i]);
            continue;
        }
        bool found = false;
        for (const auto& r : regions) {
            if (target >= r.base && target + 4 <= r.base + r.size) {
                uint32_t off = target - r.base;
                int32_t val = 0;
                memcpy(&val, r.data.data() + off, 4);
                printf("%s\tval=%d\thex=0x%08X\n", argv[i], val, static_cast<uint32_t>(val));
                found = true;
                break;
            }
        }
        if (!found) {
            printf("%s\tNOT_MAPPED\n", argv[i]);
        }
    }
    return 0;
}
