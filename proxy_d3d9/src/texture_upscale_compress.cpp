#include "texture_upscale_compress.h"

#include <windows.h>
#include <compressapi.h>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "Cabinet.lib")

namespace TextureUpscaleCompress
{
namespace
{
    // 2MB chunks -- large enough that per-chunk Compress()/Decompress() call
    // overhead stays negligible relative to real work, small enough that
    // even a single 4096x4096-with-full-mip-chain texture (~20-25MB) splits
    // into a real double-digit chunk count, giving every worker thread
    // genuine, roughly-even work.
    constexpr uint32_t kChunkSize = 2u * 1024u * 1024u;
    // Deliberately modest, not hardware_concurrency -- this already runs
    // INSIDE one of this project's own texture-upscale-cache worker threads
    // (configurable up to 16 concurrently, textureUpscaleWorkerThreads), so
    // spawning a large additional thread count per call would oversubscribe
    // real CPU cores system-wide rather than genuinely speed anything up.
    // Reduced from 4 to 2 (2026-10-01) after a live crash in an entirely
    // unrelated subsystem (bink2w64.dll, the game's own vendored video
    // codec -- crashed on its own background video-decode thread during
    // main-menu intro playback, null-pointer-style read, zero mod code
    // anywhere in the stack). Not provably caused by this feature, but a
    // real, plausible contributing factor: this project's own
    // textureUpscaleWorkerThreads can run up to 16 concurrent worker
    // threads, each of which could spawn up to kMaxCompressThreads more --
    // real resource-diag logs from the same session already showed system
    // RAM at 92% load with ~2GB free during heavy caching, real evidence of
    // severe system-wide pressure before this crash. Halved as a
    // conservative mitigation (still genuinely multi-threaded) rather than
    // a confirmed fix for a bug in third-party code this project has no
    // source for and can't patch directly.
    constexpr int kMaxCompressThreads = 2;

    // "Middle-out" chunk processing order (2026-10-01, direct request --
    // acknowledged up front as inspired by a fictional reference, but
    // implemented as something real and honest about what it does: this
    // does NOT improve the compression RATIO (each chunk is still
    // compressed/decompressed fully independently -- there's no real
    // mechanism by which compressing the middle chunk first could inform
    // or shrink the edge chunks, any claim otherwise would be the fictional
    // part). What IS real: the ORDER chunks get claimed and processed in.
    // Rather than strictly sequential index 0,1,2,...,N-1 (left-to-right),
    // this builds an index permutation starting at the middle chunk and
    // alternating outward (mid, mid-1, mid+1, mid-2, mid+2, ...). Real,
    // legitimate motivation this genuinely serves: for a large mip-pyramid
    // texture, the MIDDLE of the on-disk layout tends to land on one of the
    // mid-sized mip levels -- processing it first means if a caller were
    // ever changed to consume partial/early results (not something this
    // project does today, but a real, honest future hook this leaves open),
    // a representative middle-detail chunk would be ready before either
    // extreme (smallest/coarsest or largest/finest). Built once per call,
    // not per-thread.
    void BuildMiddleOutOrder(uint32_t chunkCount, uint32_t* order)
    {
        uint32_t mid = chunkCount / 2;
        uint32_t pos = 0;
        order[pos++] = mid;
        uint32_t lo = mid, hi = mid;
        while (pos < chunkCount) {
            if (lo > 0) order[pos++] = --lo;
            if (pos < chunkCount && hi + 1 < chunkCount) order[pos++] = ++hi;
        }
    }

    struct ChunkResult
    {
        uint8_t* data; // malloc'd by CompressBuffer's own worker, owned by
            // the caller (CompressBuffer) until it's copied into the final
            // concatenated buffer and freed.
        uint32_t size;
    };

    struct CompressJobContext
    {
        const uint8_t* srcData;
        uint32_t srcSize;
        uint32_t chunkCount;
        const uint32_t* order; // middle-out claim order, see BuildMiddleOutOrder
        volatile LONG nextChunk;
        volatile LONG anyFailure; // 0 = all good so far, 1 = at least one
            // chunk's real compression failed -- checked, not relied on to
            // stop other threads early (each chunk is independent and cheap
            // enough that racing to finish anyway is simpler and still
            // correct; the whole result is discarded on any failure).
        ChunkResult* results; // chunkCount entries, each written by exactly
            // one thread (its own claimed chunk index), never contended.
    };

    DWORD WINAPI CompressChunkThreadProc(LPVOID param)
    {
        CompressJobContext* ctx = static_cast<CompressJobContext*>(param);

        COMPRESSOR_HANDLE hCompressor = nullptr;
        if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &hCompressor)) {
            InterlockedExchange(&ctx->anyFailure, 1);
            return 0;
        }

        for (;;) {
            LONG claimPos = InterlockedIncrement(&ctx->nextChunk) - 1;
            if (static_cast<uint32_t>(claimPos) >= ctx->chunkCount) break;
            uint32_t chunkIndex = ctx->order[claimPos]; // middle-out, not left-to-right

            uint32_t offset = static_cast<uint32_t>(chunkIndex) * kChunkSize;
            uint32_t thisChunkSize = (offset + kChunkSize <= ctx->srcSize)
                ? kChunkSize : (ctx->srcSize - offset);
            const uint8_t* chunkSrc = ctx->srcData + offset;

            SIZE_T neededSize = 0;
            Compress(hCompressor, const_cast<uint8_t*>(chunkSrc), thisChunkSize, nullptr, 0, &neededSize);
            if (neededSize == 0) { InterlockedExchange(&ctx->anyFailure, 1); continue; }

            uint8_t* compressed = static_cast<uint8_t*>(malloc(neededSize));
            if (!compressed) { InterlockedExchange(&ctx->anyFailure, 1); continue; }

            SIZE_T actualSize = 0;
            BOOL ok = Compress(hCompressor, const_cast<uint8_t*>(chunkSrc), thisChunkSize,
                compressed, neededSize, &actualSize);
            if (!ok || actualSize == 0) {
                free(compressed);
                InterlockedExchange(&ctx->anyFailure, 1);
                continue;
            }

            ctx->results[chunkIndex].data = compressed;
            ctx->results[chunkIndex].size = static_cast<uint32_t>(actualSize);
        }

        CloseCompressor(hCompressor);
        return 0;
    }

    struct DecompressJobContext
    {
        const uint8_t* srcData; // points at the first chunk's compressed bytes
        const uint32_t* chunkCompressedSizes;
        const uint32_t* chunkCompressedOffsets; // precomputed running offsets,
            // chunkCount entries, since chunks are variable-size and each
            // thread needs to seek directly to its own claimed chunk without
            // scanning every earlier one.
        uint8_t* dstData; // the real output buffer -- each chunk writes into
            // its own distinct, non-overlapping region, safe without locking.
        uint32_t chunkCount;
        const uint32_t* order; // middle-out claim order, see BuildMiddleOutOrder
        uint32_t chunkUncompressedSize;
        uint32_t totalUncompressedSize;
        volatile LONG nextChunk;
        volatile LONG anyFailure;
    };

    DWORD WINAPI DecompressChunkThreadProc(LPVOID param)
    {
        DecompressJobContext* ctx = static_cast<DecompressJobContext*>(param);

        DECOMPRESSOR_HANDLE hDecompressor = nullptr;
        if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &hDecompressor)) {
            InterlockedExchange(&ctx->anyFailure, 1);
            return 0;
        }

        for (;;) {
            LONG claimPos = InterlockedIncrement(&ctx->nextChunk) - 1;
            if (static_cast<uint32_t>(claimPos) >= ctx->chunkCount) break;
            uint32_t chunkIndex = ctx->order[claimPos]; // middle-out, not left-to-right

            uint32_t dstOffset = static_cast<uint32_t>(chunkIndex) * ctx->chunkUncompressedSize;
            uint32_t thisChunkUncompressedSize = (dstOffset + ctx->chunkUncompressedSize <= ctx->totalUncompressedSize)
                ? ctx->chunkUncompressedSize : (ctx->totalUncompressedSize - dstOffset);

            const uint8_t* chunkSrc = ctx->srcData + ctx->chunkCompressedOffsets[chunkIndex];
            uint32_t chunkSrcSize = ctx->chunkCompressedSizes[chunkIndex];

            SIZE_T actualSize = 0;
            BOOL ok = Decompress(hDecompressor, const_cast<uint8_t*>(chunkSrc), chunkSrcSize,
                ctx->dstData + dstOffset, thisChunkUncompressedSize, &actualSize);
            if (!ok || actualSize != thisChunkUncompressedSize) {
                InterlockedExchange(&ctx->anyFailure, 1);
            }
        }

        CloseDecompressor(hDecompressor);
        return 0;
    }

    // Runs `threadProc` across min(kMaxCompressThreads, chunkCount) real
    // threads and waits for all of them -- shared by both CompressBuffer and
    // DecompressBuffer below, the only difference between the two is which
    // thread proc and context type gets passed in.
    void RunChunkThreads(LPTHREAD_START_ROUTINE threadProc, LPVOID context, uint32_t chunkCount)
    {
        int threadCount = static_cast<int>(chunkCount) < kMaxCompressThreads
            ? static_cast<int>(chunkCount) : kMaxCompressThreads;
        if (threadCount < 1) threadCount = 1;

        HANDLE handles[kMaxCompressThreads];
        for (int i = 0; i < threadCount; ++i) {
            handles[i] = CreateThread(nullptr, 0, threadProc, context, 0, nullptr);
        }
        WaitForMultipleObjects(threadCount, handles, TRUE, INFINITE);
        for (int i = 0; i < threadCount; ++i) {
            if (handles[i]) CloseHandle(handles[i]);
        }
    }
}

uint8_t* CompressBuffer(const uint8_t* data, uint32_t size, uint32_t* outCompressedSize)
{
    if (outCompressedSize) *outCompressedSize = 0;
    if (!data || size == 0) return nullptr;

    uint32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    if (chunkCount == 0) chunkCount = 1;

    ChunkResult* results = static_cast<ChunkResult*>(calloc(chunkCount, sizeof(ChunkResult)));
    if (!results) return nullptr;
    uint32_t* order = static_cast<uint32_t*>(malloc(sizeof(uint32_t) * chunkCount));
    if (!order) { free(results); return nullptr; }
    BuildMiddleOutOrder(chunkCount, order);

    CompressJobContext ctx{};
    ctx.srcData = data;
    ctx.srcSize = size;
    ctx.chunkCount = chunkCount;
    ctx.order = order;
    ctx.nextChunk = 0;
    ctx.anyFailure = 0;
    ctx.results = results;

    RunChunkThreads(CompressChunkThreadProc, &ctx, chunkCount);
    free(order);

    if (ctx.anyFailure) {
        for (uint32_t i = 0; i < chunkCount; ++i) free(results[i].data);
        free(results);
        return nullptr;
    }

    // Real chunk-table format: chunkCount, chunkUncompressedSize (kChunkSize --
    // every chunk except possibly the last, which DecompressBuffer derives
    // from the real total uncompressed size it's given separately), each
    // chunk's real compressed size, then the chunks themselves concatenated.
    uint32_t tableBytes = sizeof(uint32_t) * 2 + sizeof(uint32_t) * chunkCount;
    uint32_t totalCompressedPayload = 0;
    for (uint32_t i = 0; i < chunkCount; ++i) totalCompressedPayload += results[i].size;

    uint8_t* out = static_cast<uint8_t*>(malloc(tableBytes + totalCompressedPayload));
    if (!out) {
        for (uint32_t i = 0; i < chunkCount; ++i) free(results[i].data);
        free(results);
        return nullptr;
    }

    uint8_t* cursor = out;
    memcpy(cursor, &chunkCount, sizeof(uint32_t)); cursor += sizeof(uint32_t);
    uint32_t chunkUncompressedSize = kChunkSize;
    memcpy(cursor, &chunkUncompressedSize, sizeof(uint32_t)); cursor += sizeof(uint32_t);
    for (uint32_t i = 0; i < chunkCount; ++i) {
        memcpy(cursor, &results[i].size, sizeof(uint32_t));
        cursor += sizeof(uint32_t);
    }
    for (uint32_t i = 0; i < chunkCount; ++i) {
        memcpy(cursor, results[i].data, results[i].size);
        cursor += results[i].size;
        free(results[i].data);
    }
    free(results);

    if (outCompressedSize) *outCompressedSize = tableBytes + totalCompressedPayload;
    return out;
}

uint8_t* DecompressBuffer(const uint8_t* data, uint32_t compressedSize, uint32_t uncompressedSize)
{
    if (!data || compressedSize == 0 || uncompressedSize == 0) return nullptr;
    if (compressedSize < sizeof(uint32_t) * 2) return nullptr;

    uint32_t chunkCount = 0, chunkUncompressedSize = 0;
    memcpy(&chunkCount, data, sizeof(uint32_t));
    memcpy(&chunkUncompressedSize, data + sizeof(uint32_t), sizeof(uint32_t));
    if (chunkCount == 0 || chunkUncompressedSize == 0) return nullptr;

    uint32_t tableBytes = sizeof(uint32_t) * 2 + sizeof(uint32_t) * chunkCount;
    if (compressedSize < tableBytes) return nullptr; // real, truncated/corrupt
        // table -- chunkCount itself could be garbage from a corrupt file;
        // this bounds check keeps the per-chunk-size read loop below safe.

    const uint32_t* chunkCompressedSizes = reinterpret_cast<const uint32_t*>(data + sizeof(uint32_t) * 2);

    uint32_t* chunkCompressedOffsets = static_cast<uint32_t*>(malloc(sizeof(uint32_t) * chunkCount));
    if (!chunkCompressedOffsets) return nullptr;
    uint32_t runningOffset = tableBytes;
    uint64_t runningTotal = tableBytes; // 64-bit to catch a real overflow
        // from corrupt per-chunk sizes before it wraps a 32-bit offset.
    for (uint32_t i = 0; i < chunkCount; ++i) {
        chunkCompressedOffsets[i] = runningOffset;
        runningTotal += chunkCompressedSizes[i];
        runningOffset = static_cast<uint32_t>(runningTotal);
    }
    if (runningTotal != compressedSize) {
        // Real, definitive corruption check -- the chunk table's own
        // declared sizes must account for exactly the real file size, same
        // "no silent partial trust" standard as this project's own
        // fileSizeForPicmip[0] validation elsewhere.
        free(chunkCompressedOffsets);
        return nullptr;
    }

    uint8_t* out = static_cast<uint8_t*>(malloc(uncompressedSize));
    if (!out) { free(chunkCompressedOffsets); return nullptr; }
    uint32_t* order = static_cast<uint32_t*>(malloc(sizeof(uint32_t) * chunkCount));
    if (!order) { free(out); free(chunkCompressedOffsets); return nullptr; }
    BuildMiddleOutOrder(chunkCount, order);

    DecompressJobContext ctx{};
    ctx.srcData = data;
    ctx.chunkCompressedSizes = chunkCompressedSizes;
    ctx.chunkCompressedOffsets = chunkCompressedOffsets;
    ctx.dstData = out;
    ctx.chunkCount = chunkCount;
    ctx.order = order;
    ctx.chunkUncompressedSize = chunkUncompressedSize;
    ctx.totalUncompressedSize = uncompressedSize;
    ctx.nextChunk = 0;
    ctx.anyFailure = 0;

    RunChunkThreads(DecompressChunkThreadProc, &ctx, chunkCount);
    free(chunkCompressedOffsets);
    free(order);

    if (ctx.anyFailure) { free(out); return nullptr; }
    return out;
}
}
