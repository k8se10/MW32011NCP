#pragma once

#include "Loading/Exception/InvalidLookupPositionException.h"
#include "Loading/ILoadingStream.h"
#include "Utils/Logging/Log.h"
#include "Utils/MemoryManager.h"
#include "Utils/ProgressCallback.h"
#include "Zone/Stream/IZoneStream.h"
#include "Zone/XBlock.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

// #define DEBUG_OFFSETS 1

class ZoneStreamFillReadAccessor
{
public:
    ZoneStreamFillReadAccessor(void* blockBuffer, size_t bufferSize, unsigned pointerByteCount, size_t offset);

    [[nodiscard]] ZoneStreamFillReadAccessor AtOffset(size_t offset) const;
    [[nodiscard]] size_t Offset() const;
    [[nodiscard]] void* BlockBuffer(size_t offset) const;

    template<typename T> void Fill(T& value, const size_t offset) const
    {
        assert(offset + sizeof(T) <= m_buffer_size);

        value = *reinterpret_cast<const T*>(static_cast<const char*>(m_block_buffer) + offset);
    }

    template<typename T, size_t S> void FillArray(T (&value)[S], const size_t offset) const
    {
        assert(offset + sizeof(T) * S <= m_buffer_size);

        std::memcpy(value, static_cast<const char*>(m_block_buffer) + offset, sizeof(T) * S);
    }

    template<typename T> void FillPtr(T*& value, const size_t offset) const
    {
        assert(offset + m_pointer_byte_count <= m_buffer_size);
        assert(m_pointer_byte_count <= sizeof(uintptr_t));

        value = nullptr;
        std::memcpy(&value, static_cast<const char*>(m_block_buffer) + offset, m_pointer_byte_count);
    }

private:
    void* m_block_buffer;
    size_t m_buffer_size;
    unsigned m_pointer_byte_count;
    size_t m_offset;
};

template<typename T> class MaybePointerFromLookup
{
public:
    explicit MaybePointerFromLookup(void* ptr)
        : m_valid(true),
          m_ptr(ptr),
          m_block(0),
          m_offset(0)
    {
    }

    MaybePointerFromLookup(void* ptr, const block_t block, const size_t offset)
        : m_valid(false),
          m_ptr(ptr),
          m_block(block),
          m_offset(offset)
    {
    }

    explicit MaybePointerFromLookup(const MaybePointerFromLookup<void>& other)
        : m_valid(other.m_valid),
          m_ptr(static_cast<T*>(other.m_ptr)),
          m_block(other.m_block),
          m_offset(other.m_offset)
    {
    }

    [[nodiscard]] T* Expect() const
    {
        if (!m_valid)
        {
            // MW32011NCP / iw5oat, 2026-09-16: was an unconditional throw, aborting
            // the whole zone load on any unresolved lookup -- see
            // re_notes/x64_migration/fastfile_format_research.md SS5.41 (parent
            // repo) for the full rationale. Every real caller of ConvertOffsetToPointerLookup
            // (this function's own real-world usage, confirmed by direct inspection
            // of several generated *_load_db.cpp call sites) already gates its own
            // dereference behind `if (field) { ... }`, matching the exact
            // "0/null is a normal, valid, absent-reference outcome" convention this
            // whole format already uses everywhere else -- the same shape
            // ConvertOffsetToAliasLookup's own already-proven-safe (full 41-zone
            // sweep, zero new crashes) graceful degradation relies on. Warn and
            // return null instead of throwing, rather than aborting the entire
            // remaining zone over one genuine forward reference.
            con::warn("Zone tried to lookup at block {}, offset {} that was not recorded -- "
                      "leaving the reference null and continuing (MaybePointerFromLookup::Expect).",
                      static_cast<int>(m_block), m_offset);
            return nullptr;
        }

        return static_cast<T*>(m_ptr);
    }

    /**
     * The original linker does an annoying optimization where ConvertOffsetToPointer makes structs
     * reuse data across non-matching types.
     * E.g. a pointer array reuses memory of a scriptstring array.
     * Since cross-platform the sizes of the types do not match anymore, this has to be handled differently.
     * The scenario seems to realistically only happen when the data is nulled so just alloc a nulled memory block.
     * If this strategy fails, in the future it might need to realloc and load existing block data with fill.
     */
    [[nodiscard]] T* OrNulled(const size_t gameSize, const size_t size, MemoryManager& memory) const
    {
        if (m_valid)
            return static_cast<T*>(m_ptr);

        auto* result = static_cast<T*>(memory.AllocRaw(size));

        // We expect the original game buffer to also have been nulled
        assert(gameSize < size);
        assert(memcmp(result, m_ptr, gameSize) == 0);

        return result;
    }

    bool m_valid;
    void* m_ptr;
    block_t m_block;
    size_t m_offset;
};

class ZoneInputStream : public IZoneStream
{
public:
    /**
     * \brief Returns the configured bits that make up a pointer.
     */
    [[nodiscard]] virtual unsigned GetPointerBitCount() const = 0;

    /**
     * \brief Retrieves the new read position in the current block by aligning the position with the specified value and then returning the current read
     * position in the block.
     * \param align The alignment value that the read position is aligned with before being returned. This should typically be the alignment of the struct that
     * should be read afterward.
     * \return A pointer to the memory in which following load calls will load data to.
     */
    virtual void* Alloc(unsigned align) = 0;

    /**
     * \copydoc ZoneInputStream#Alloc(unsigned)
     */
    template<typename T> T* Alloc(const unsigned align)
    {
        return static_cast<T*>(Alloc(align));
    }

    virtual void* AllocOutOfBlock(unsigned align, size_t size) = 0;

    /**
     * \copydoc ZoneInputStream#AllocOutOfBlock(unsigned)
     */
    template<typename T> T* AllocOutOfBlock(const unsigned align, const size_t arraySize = 1u)
    {
        return static_cast<T*>(AllocOutOfBlock(align, sizeof(T) * arraySize));
    }

    /**
     * \brief Loads data from the underlying stream without considering the current block or advancing the block position.
     * The data is read directly to the specified location instead of block memory.
     * \param dst The memory location to load data to.
     * \param size The amount of data to load.
     */
    virtual void LoadDataRaw(void* dst, size_t size) = 0;

    /**
     * \brief Loads data with the current blocks read operation into its block memory.
     * Depending on the block type, the underlying stream might be read from or not.
     * The current block position is advanced by the number of bytes read.
     * The destination must be inside the current block's memory space, otherwise an exception is thrown.
     * \param dst The destination where the data is loaded to. Must be inside the current block's memory bounds.
     * \param size The amount of data to load.
     */
    virtual void LoadDataInBlock(void* dst, size_t size) = 0;
    virtual void LoadNullTerminated(void* dst) = 0;
    virtual ZoneStreamFillReadAccessor LoadWithFill(size_t size) = 0;
    virtual ZoneStreamFillReadAccessor AppendToFill(size_t appendSize) = 0;
    virtual ZoneStreamFillReadAccessor GetLastFill() = 0;

    template<typename T> void Load(T* dst)
    {
        LoadDataInBlock(const_cast<void*>(reinterpret_cast<const void*>(dst)), sizeof(T));
    }

    template<typename T> void Load(T* dst, const size_t count)
    {
        LoadDataInBlock(const_cast<void*>(reinterpret_cast<const void*>(dst)), count * sizeof(T));
    }

    template<typename T> void LoadPartial(T* dst, const size_t size)
    {
        LoadDataInBlock(const_cast<void*>(reinterpret_cast<const void*>(dst)), size);
    }

    virtual void* InsertPointerNative() = 0;

    template<typename T> T** InsertPointerNative()
    {
        return static_cast<T**>(InsertPointerNative());
    }

    virtual uintptr_t InsertPointerAliasLookup() = 0;
    virtual void SetInsertedPointerAliasLookup(uintptr_t lookupEntry, void* value) = 0;

    virtual void* ConvertOffsetToPointerNative(const void* offset) = 0;

    template<typename T> T* ConvertOffsetToPointerNative(T* offset)
    {
        return static_cast<T*>(ConvertOffsetToPointerNative(static_cast<const void*>(offset)));
    }

    /**
     * \brief Attempts to resolve an already-resolved (non-FOLLOWING/INSERT) offset-encoded
     * zone pointer to a NATIVE STRING specifically, for the narrow case where the standard
     * (stream-native-pointer-width) decode would fail bounds validation but the raw offset
     * value's own top bits above 32 are exactly zero -- the confirmed empirical signature of
     * a pointer genuinely encoded at 32-bit width on the wire rather than this stream's
     * configured native width (see re_notes/x64_migration/fastfile_format_research.md SS5.16
     * in the parent repo for the full root-cause trail; found via MaterialPixelShader::name in
     * code_post_gfx.ff, an interned/shared shader-filename cross-reference).
     *
     * Deliberately scoped to STRING resolution only, not the general pointer-conversion path:
     * unlike an arbitrary struct pointer, a resolved string can be validated (a real
     * null-terminated, printable run of bytes within the target block's real remaining size)
     * before being trusted, which keeps this safe to try as a fallback without risking silently
     * accepting garbage for a case that's actually a different, unrelated bug (e.g. a genuine
     * forward reference into a block that simply hasn't been written yet -- confirmed to also
     * exhibit the same "top 32 bits zero" shape for hamburg.ff/common.ff's own, separate
     * failures per SS5.17, where naively trusting the 32-bit decode would read real, in-bounds,
     * but all-zero/unwritten memory as if it were valid).
     *
     * Only ever consulted when the standard ConvertOffsetToPointerNative resolution for this
     * exact offset would throw -- never overrides an already-successful standard resolution.
     * Returns nullptr (not a thrown exception) if the fallback itself doesn't produce a
     * validated string, so the caller can fall through to the original, honestly-failing
     * ConvertOffsetToPointerNative call to get the real exception.
     */
    virtual const char* TryConvertOffsetToStringPointerNative(const void* offset) = 0;

    virtual void* ConvertOffsetToAliasNative(const void* offset) = 0;

    template<typename T> T* ConvertOffsetToAliasNative(T* offset)
    {
        return static_cast<T*>(ConvertOffsetToAliasNative(static_cast<const void*>(offset)));
    }

    /**
     * \brief Adds a lookup from a block pointer to out of block data
     * \param redirectTo A pointer to the out of block data to redirect to
     * \param redirectFrom A pointer to the block data to redirect from
     */
    virtual void AddPointerLookup(void* redirectTo, const void* redirectFrom) = 0;

    virtual MaybePointerFromLookup<void> ConvertOffsetToPointerLookup(const void* offset) = 0;

    template<typename T> MaybePointerFromLookup<T> ConvertOffsetToPointerLookup(T* offset)
    {
        return MaybePointerFromLookup<T>(ConvertOffsetToPointerLookup(static_cast<const void*>(offset)));
    }

    virtual void* ConvertOffsetToAliasLookup(const void* offset) = 0;

    template<typename T> T* ConvertOffsetToAliasLookup(T* offset)
    {
        return static_cast<T*>(ConvertOffsetToAliasLookup(static_cast<const void*>(offset)));
    }

#ifdef DEBUG_OFFSETS
    virtual void DebugOffsets(size_t assetIndex) const = 0;
#endif

    static std::unique_ptr<ZoneInputStream> Create(unsigned pointerBitCount,
                                                   unsigned blockBitCount,
                                                   std::vector<XBlock*>& blocks,
                                                   block_t insertBlock,
                                                   ILoadingStream& stream,
                                                   MemoryManager& memory,
                                                   std::optional<std::unique_ptr<ProgressCallback>> progressCallback);
};
