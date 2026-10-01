#pragma once

#include <cstdint>

// Real lossless compression for cached upscaled textures (2026-10-01), direct
// request: "i also feel maybe we need to add far better lossless compression
// to these textures as right now theyre extremely huge files," followed by a
// direct correction to make it genuinely parallel: "proper multithreaded cpu
// based compresion and decompression." A full real mip pyramid
// (texture_upscale_worker.cpp's own "Real mip chain" change) can run 20MB+
// per cached texture for a 4096x4096 source -- real DXT5 block data still
// has meaningful real redundancy (large smooth gradients, repeated block
// patterns) a general-purpose compressor can exploit, even though DXT5
// itself is already a fixed-ratio GPU-native format we can't change (the
// native engine's own LockRect/CreateTexture path needs real, ready-to-use
// DXT5 bytes -- this compresses the ON-DISK cache FILE, never what actually
// gets handed to the GPU).
//
// Uses the native Windows Compression API (compressapi.h, Cabinet.lib) --
// COMPRESS_ALGORITHM_LZMS, the highest-ratio algorithm it offers (comparable
// to LZMA/7-Zip-class compression). A single COMPRESSOR_HANDLE call is
// inherently single-threaded (no native multi-threaded mode exists in this
// Win32 API for one buffer), so genuine parallelism is built on top: the
// buffer is split into independent fixed-size chunks, each compressed (or
// decompressed) on its own real worker thread with its own compressor/
// decompressor handle (handles are not documented thread-safe for concurrent
// use, so each thread gets its own, never shared). A modest, fixed thread
// count (not hardware_concurrency) is used deliberately -- this already runs
// INSIDE one of this project's own texture-upscale-cache worker threads
// (configurable up to 16, textureUpscaleWorkerThreads), so spawning a large
// number of additional threads per call would oversubscribe real CPU cores
// rather than genuinely speed anything up.
namespace TextureUpscaleCompress
{
    // Compresses `data` (real size `size`) into a newly malloc'd buffer
    // containing this module's own real chunk-table format (chunk count,
    // each chunk's real compressed size, then the chunks themselves
    // concatenated) -- opaque to the caller, DecompressBuffer is the only
    // valid way to read it back. Caller owns and must free() the result.
    // Returns nullptr (outSize left at 0) on any real failure (API
    // unavailable, OOM, a chunk's own compression failing) -- the caller's
    // own job should treat this as a real, logged failure, not silently
    // ship an uncompressed fallback (this project's cache format commits to
    // always being compressed once this ships).
    uint8_t* CompressBuffer(const uint8_t* data, uint32_t size, uint32_t* outCompressedSize);

    // Decompresses a buffer produced by CompressBuffer back into a newly
    // malloc'd buffer of exactly `uncompressedSize` bytes (the real original
    // size, which the caller must already know -- see
    // texture_upscale_cache.cpp's own on-disk wrapper header, which stores
    // it alongside the compressed bytes for exactly this reason). Caller
    // owns and must free() the result. Returns nullptr on any real failure
    // (truncated/corrupt data, a chunk's own decompression failing, API
    // unavailable, OOM) -- the caller's own existing "treat as a miss,
    // recapture" discipline already handles this the same way a validation
    // failure does today.
    uint8_t* DecompressBuffer(const uint8_t* data, uint32_t compressedSize, uint32_t uncompressedSize);
}
