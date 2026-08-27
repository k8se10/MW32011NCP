// diffsnap.cpp -- dev-only, standalone. Streaming byte-diff between two memdiff
// .snap files WITHOUT loading either fully into RAM at once (unlike memdiff.exe's
// own "diff" mode, which loads both complete snapshots as in-memory Region vectors
// -- confirmed this session to fail outright, exit code 127/no output, once
// combined snapshot size gets into the multi-GB range on this 32-bit tool).
//
// Pass 1 per file: read only the region INDEX (base+size, 8 bytes/region) and each
// region's byte OFFSET within the file, skipping over the actual data with fseek.
// Pass 2: for each region present in BOTH files at the same base+size, stream-
// compare in small chunks (64KB), reporting differing byte ranges (not every
// single differing byte -- this engine has enormous per-frame volatility, so
// coalescing into contiguous ranges is what's actually readable) plus regions
// present in one file but missing/resized in the other (a real, distinct signal
// -- e.g. a deallocated/unmapped region -- that memdiff.exe's own diff mode
// silently skips via its `if (!ra || ra->size != rb.size) continue;` check).
//
// Usage: diffsnap.exe <fileA.snap> <fileB.snap> [maxRangesToPrint=200]

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace {

struct RegionIndex {
    uint32_t base;
    uint32_t size;
    long fileOffset; // offset of this region's DATA bytes within the .snap file
};

bool LoadIndex(const char* path, std::vector<RegionIndex>& out)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return false;
    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, f) != 1) { fclose(f); return false; }
    out.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        RegionIndex& r = out[i];
        if (fread(&r.base, sizeof(r.base), 1, f) != 1) { fclose(f); return false; }
        if (fread(&r.size, sizeof(r.size), 1, f) != 1) { fclose(f); return false; }
        r.fileOffset = ftell(f);
        if (_fseeki64(f, static_cast<long long>(r.size), SEEK_CUR) != 0) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

const RegionIndex* FindByBase(const std::vector<RegionIndex>& idx, uint32_t base)
{
    for (const auto& r : idx) if (r.base == base) return &r;
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: diffsnap.exe <fileA.snap> <fileB.snap> [maxRangesToPrint=200]\n");
        return 1;
    }
    const char* pathA = argv[1];
    const char* pathB = argv[2];
    size_t maxRanges = (argc >= 4) ? static_cast<size_t>(strtoul(argv[3], nullptr, 10)) : 200;

    std::vector<RegionIndex> idxA, idxB;
    if (!LoadIndex(pathA, idxA)) { printf("Failed to index %s\n", pathA); return 1; }
    if (!LoadIndex(pathB, idxB)) { printf("Failed to index %s\n", pathB); return 1; }
    printf("Indexed %s (%zu regions) and %s (%zu regions)\n", pathA, idxA.size(), pathB, idxB.size());

    FILE* fa = nullptr; FILE* fb = nullptr;
    fopen_s(&fa, pathA, "rb");
    fopen_s(&fb, pathB, "rb");
    if (!fa || !fb) { printf("reopen failed\n"); return 1; }

    // Regions present in A but missing (or resized) in B, and vice versa.
    size_t onlyInA = 0, onlyInB = 0, sizeMismatch = 0;
    for (const auto& ra : idxA) {
        const RegionIndex* rb = FindByBase(idxB, ra.base);
        if (!rb) { onlyInA++; continue; }
        if (rb->size != ra.size) sizeMismatch++;
    }
    for (const auto& rb : idxB) {
        if (!FindByBase(idxA, rb.base)) onlyInB++;
    }
    printf("Regions only in A (missing/unmapped in B): %zu\n", onlyInA);
    printf("Regions only in B (newly mapped since A):  %zu\n", onlyInB);
    printf("Regions present in both but resized:       %zu\n", sizeMismatch);

    if (onlyInA > 0) {
        printf("\n-- sample of regions only in A (first 30) --\n");
        size_t shown = 0;
        for (const auto& ra : idxA) {
            if (FindByBase(idxB, ra.base)) continue;
            printf("  0x%08X size=0x%X (%.2f MB)\n", ra.base, ra.size, ra.size / (1024.0 * 1024.0));
            if (++shown >= 30) break;
        }
    }
    if (onlyInB > 0) {
        printf("\n-- sample of regions only in B (first 30) --\n");
        size_t shown = 0;
        for (const auto& rb : idxB) {
            if (FindByBase(idxA, rb.base)) continue;
            printf("  0x%08X size=0x%X (%.2f MB)\n", rb.base, rb.size, rb.size / (1024.0 * 1024.0));
            if (++shown >= 30) break;
        }
    }

    // Byte-level streaming diff of matching regions -- coalesce into contiguous
    // differing ranges, chunked reads so peak memory stays at 2*64KB regardless
    // of how large the underlying region or the whole snapshot is.
    printf("\n-- streaming byte diff of matching regions --\n");
    constexpr size_t kChunk = 64 * 1024;
    std::vector<uint8_t> bufA(kChunk), bufB(kChunk);
    size_t rangesPrinted = 0;
    uint64_t totalDiffBytes = 0;
    bool inRange = false;
    uint32_t rangeStart = 0;
    uint32_t rangeEnd = 0;

    auto FlushRange = [&]() {
        if (!inRange) return;
        if (rangesPrinted < maxRanges) {
            printf("  0x%08X - 0x%08X (%u bytes differ)\n", rangeStart, rangeEnd, rangeEnd - rangeStart + 1);
        }
        rangesPrinted++;
        inRange = false;
    };

    for (const auto& ra : idxA) {
        const RegionIndex* rb = FindByBase(idxB, ra.base);
        if (!rb || rb->size != ra.size) continue;
        _fseeki64(fa, ra.fileOffset, SEEK_SET);
        _fseeki64(fb, rb->fileOffset, SEEK_SET);
        uint32_t remaining = ra.size;
        uint32_t off = 0;
        while (remaining > 0) {
            size_t n = remaining < kChunk ? remaining : kChunk;
            if (fread(bufA.data(), 1, n, fa) != n) break;
            if (fread(bufB.data(), 1, n, fb) != n) break;
            for (size_t i = 0; i < n; i++) {
                if (bufA[i] != bufB[i]) {
                    totalDiffBytes++;
                    uint32_t addr = ra.base + off + static_cast<uint32_t>(i);
                    if (inRange && addr == rangeEnd + 1) {
                        rangeEnd = addr;
                    } else {
                        FlushRange();
                        inRange = true;
                        rangeStart = rangeEnd = addr;
                    }
                }
            }
            off += static_cast<uint32_t>(n);
            remaining -= static_cast<uint32_t>(n);
        }
        FlushRange(); // ranges don't span region boundaries
    }

    printf("\nTotal differing bytes: %llu across %zu contiguous ranges%s\n",
        static_cast<unsigned long long>(totalDiffBytes), rangesPrinted,
        rangesPrinted > maxRanges ? " (only first N printed, see argv[3])" : "");

    fclose(fa);
    fclose(fb);
    return 0;
}
