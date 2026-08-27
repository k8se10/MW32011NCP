// dumpregion.cpp -- dev-only, standalone. Prints a hex+ASCII dump of up to <len>
// bytes starting at <addr> from a saved memdiff .snap file, streaming (seek+read,
// same technique as diffsnap.cpp) so it never loads the whole snapshot into RAM.
// Built to inspect the actual contents of specific freed regions identified by
// diffsnap.exe (known_issues.md issue #103, the memdiff-livedump crash
// investigation) -- what subsystem owned the ~34MB+~800 other regions freed in
// the mass-deallocation event right before iw5sp.exe's crash/hang.
//
// Usage: dumpregion.exe <addrHex> <lenDec> <file.snap>

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 4) {
        printf("Usage: dumpregion.exe <addrHex> <lenDec> <file.snap>\n");
        return 1;
    }
    uint32_t addr = static_cast<uint32_t>(strtoul(argv[1], nullptr, 16));
    uint32_t len = static_cast<uint32_t>(strtoul(argv[2], nullptr, 10));
    const char* path = argv[3];

    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) { printf("Failed to open %s\n", path); return 1; }

    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, f) != 1) { printf("bad header\n"); fclose(f); return 1; }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t base = 0, size = 0;
        if (fread(&base, sizeof(base), 1, f) != 1) break;
        if (fread(&size, sizeof(size), 1, f) != 1) break;
        long dataOff = ftell(f);
        if (addr >= base && addr < base + size) {
            uint32_t offInRegion = addr - base;
            uint32_t available = size - offInRegion;
            uint32_t toRead = len < available ? len : available;
            printf("Found in region base=0x%08X size=0x%X (%.2f MB) -- reading 0x%X bytes at +0x%X\n",
                base, size, size / (1024.0 * 1024.0), toRead, offInRegion);
            _fseeki64(f, static_cast<long long>(dataOff) + offInRegion, SEEK_SET);
            std::vector<uint8_t> buf(toRead);
            size_t got = fread(buf.data(), 1, toRead, f);
            for (size_t row = 0; row < got; row += 16) {
                printf("0x%08X: ", addr + static_cast<uint32_t>(row));
                size_t rowLen = (got - row) < 16 ? (got - row) : 16;
                for (size_t k = 0; k < 16; k++) {
                    if (k < rowLen) printf("%02X ", buf[row + k]); else printf("   ");
                }
                printf(" ");
                for (size_t k = 0; k < rowLen; k++) {
                    uint8_t c = buf[row + k];
                    putchar((c >= 0x20 && c < 0x7f) ? c : '.');
                }
                printf("\n");
            }
            fclose(f);
            return 0;
        }
        _fseeki64(f, static_cast<long long>(dataOff) + size, SEEK_SET);
    }
    printf("Address 0x%08X not found in any region of %s\n", addr, path);
    fclose(f);
    return 1;
}
