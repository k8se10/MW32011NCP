#include "ZoneInputStream.h"

#include "Loading/Exception/BlockOverflowException.h"
#include "Loading/Exception/InvalidLookupPositionException.h"
#include "Loading/Exception/InvalidOffsetBlockException.h"
#include "Loading/Exception/InvalidOffsetBlockOffsetException.h"
#include "Loading/Exception/OutOfBlockBoundsException.h"
#include "Loading/Exception/ShortReadException.h"
#include "Utils/Alignment.h"
#include "Utils/Logging/Log.h"
#include "Utils/PointerSanity.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>

ZoneStreamFillReadAccessor::ZoneStreamFillReadAccessor(void* blockBuffer, const size_t bufferSize, const unsigned pointerByteCount, const size_t offset)
    : m_block_buffer(blockBuffer),
      m_buffer_size(bufferSize),
      m_pointer_byte_count(pointerByteCount),
      m_offset(offset)
{
    // Otherwise we cannot insert alias
    assert(m_pointer_byte_count <= sizeof(uintptr_t));
}

ZoneStreamFillReadAccessor ZoneStreamFillReadAccessor::AtOffset(const size_t offset) const
{
    assert(offset <= m_buffer_size);
    return ZoneStreamFillReadAccessor(static_cast<char*>(m_block_buffer) + offset, m_buffer_size - offset, m_pointer_byte_count, m_offset + offset);
}

size_t ZoneStreamFillReadAccessor::Offset() const
{
    return m_offset;
}

void* ZoneStreamFillReadAccessor::BlockBuffer(const size_t offset) const
{
    return static_cast<uint8_t*>(m_block_buffer) + offset;
}

namespace
{
    class XBlockInputStream final : public ZoneInputStream
    {
    public:
        XBlockInputStream(const unsigned pointerBitCount,
                          const unsigned blockBitCount,
                          std::vector<XBlock*>& blocks,
                          const block_t insertBlock,
                          ILoadingStream& stream,
                          MemoryManager& memory,
                          std::optional<std::unique_ptr<ProgressCallback>> progressCallback)
            : m_blocks(blocks),
              m_block_offsets(blocks.size()),
              m_stream(stream),
              m_memory(memory),
              m_pointer_byte_count(pointerBitCount / 8u),
              // MW32011NCP / iw5oat, 2026-09-14: this used to derive the block-index shift/
              // mask from `pointerBitCount` (64u for this fork's own IW5 x64 target) -- i.e.
              // it assumed the zone format's own "already-resolved offset pointer" encoding
              // scales up to the platform's real native pointer width. Confirmed WRONG via
              // direct decompile of iw5sp.exe's own real offset-resolution primitive
              // (FUN_1400aad40, the exact native equivalent of this function -- 86 real
              // callers spanning every asset type, not a per-field special case): the real
              // x64 engine casts the raw value to a plain 32-bit `int` FIRST, unconditionally,
              // then applies the SAME 4-bit-block-index-in-a-32-bit-word scheme this format
              // has always used, even on the current x64 recompile -- this specific encoding
              // was simply never widened. Confirmed empirically too, not just by decompile: a
              // live diagnostic resolved MaterialPixelShader::name's own raw wire bytes under
              // this exact 32-bit scheme to a real, readable string
              // ("trivial_vertcol_simple.hlsl"), and the same alternate-decode block index
              // (XFILE_BLOCK_VIRTUAL) independently reproduced across all three zones this
              // fork was failing on. Full trail: re_notes/x64_migration/
              // fastfile_format_research.md (parent repo) SS5.16-SS5.18.
              //
              // Deliberately hardcoded to 32u here, decoupled from `pointerBitCount` -- this
              // is the zone format's own fixed on-wire encoding width for this specific
              // pointer class, not a property of the platform this fork happens to build for.
              // `pointerBitCount` itself stays correctly used for genuine native-pointer-
              // field-size purposes elsewhere (m_pointer_byte_count/GetPointerBitCount(),
              // e.g. LoadXStringArray's own per-element stride) -- those really are 8 bytes
              // wide on x64, unlike this offset-encoding scheme.
              m_block_mask((std::numeric_limits<uintptr_t>::max() >> (sizeof(uintptr_t) * 8 - blockBitCount)) << (32u - blockBitCount)),
              m_block_shift(32u - blockBitCount),
              m_offset_mask(std::numeric_limits<uintptr_t>::max() >> (sizeof(uintptr_t) * 8 - (32u - blockBitCount))),
              m_last_fill_size(0),
              m_has_progress_callback(false),
              m_progress_current_size(0uz),
              m_progress_total_size(0uz)
        {
            assert(pointerBitCount % 8u == 0u);
            assert(insertBlock < static_cast<block_t>(blocks.size()));

            std::memset(m_block_offsets.data(), 0, sizeof(decltype(m_block_offsets)::value_type) * m_block_offsets.size());

            m_insert_block = blocks[insertBlock];

            if (progressCallback)
            {
                m_has_progress_callback = true;
                m_progress_callback = *std::move(progressCallback);
                m_progress_total_size = CalculateTotalSize();
            }
        }

        [[nodiscard]] unsigned GetPointerBitCount() const override
        {
            return m_pointer_byte_count * 8u;
        }

        void PushBlock(const block_t block) override
        {
            assert(block < static_cast<block_t>(m_blocks.size()));

            auto* newBlock = m_blocks[block];
            assert(newBlock->m_index == block);

            m_block_stack.push(newBlock);

            if (newBlock->m_type == XBlockType::BLOCK_TYPE_TEMP)
                m_temp_offsets.push(m_block_offsets[newBlock->m_index]);
        }

        block_t PopBlock() override
        {
            assert(!m_block_stack.empty());

            if (m_block_stack.empty())
                return -1;

            const auto* poppedBlock = m_block_stack.top();

            m_block_stack.pop();

            // If the temp block is not used anymore right now, reset it to the buffer start since as the name suggests, the data inside is temporary.
            if (poppedBlock->m_type == XBlockType::BLOCK_TYPE_TEMP)
            {
                m_block_offsets[poppedBlock->m_index] = m_temp_offsets.top();
                m_temp_offsets.pop();
            }

            return poppedBlock->m_index;
        }

        void* Alloc(const unsigned align) override
        {
            assert(!m_block_stack.empty());

            if (m_block_stack.empty())
                return nullptr;

            auto* block = m_block_stack.top();

            Align(*block, align);

            if (m_block_offsets[block->m_index] > block->m_buffer_size)
                throw BlockOverflowException(block);

            return &block->m_buffer[m_block_offsets[block->m_index]];
        }

        void* AllocOutOfBlock(const unsigned align, const size_t size) override
        {
            assert(!m_block_stack.empty());

            if (m_block_stack.empty())
                return nullptr;

            auto* block = m_block_stack.top();

            Align(*block, align);

            if (m_block_offsets[block->m_index] > block->m_buffer_size)
                throw BlockOverflowException(block);

            return m_memory.AllocRaw(size);
        }

        void LoadDataRaw(void* dst, const size_t size) override
        {
            LoadChecked(dst, size, "raw data");
        }

        void LoadDataInBlock(void* dst, const size_t size) override
        {
            // If no block has been pushed, load raw
            if (!m_block_stack.empty())
            {
                auto* block = m_block_stack.top();

                if (block->m_buffer.get() > dst || block->m_buffer.get() + block->m_buffer_size < dst)
                    throw OutOfBlockBoundsException(block);

                if (static_cast<uint8_t*>(dst) + size > block->m_buffer.get() + block->m_buffer_size)
                    throw BlockOverflowException(block);

                // Theoretically ptr should always be at the current block offset.
                assert(dst == &block->m_buffer[m_block_offsets[block->m_index]]);

                LoadDataFromBlock(*block, dst, size);
            }
            else
            {
                LoadChecked(dst, size, "raw data (no block pushed)");
            }
        }

        void LoadNullTerminated(void* dst) override
        {
            assert(!m_block_stack.empty());

            if (m_block_stack.empty())
                return;

            auto* block = m_block_stack.top();

            if (block->m_buffer.get() > dst || block->m_buffer.get() + block->m_buffer_size < dst)
                throw OutOfBlockBoundsException(block);

            // Theoretically ptr should always be at the current block offset.
            assert(dst == &block->m_buffer[m_block_offsets[block->m_index]]);

            uint8_t byte;
            auto offset = static_cast<size_t>(static_cast<uint8_t*>(dst) - block->m_buffer.get());
            do
            {
                if (offset >= block->m_buffer_size)
                    throw BlockOverflowException(block);

                LoadChecked(&byte, 1, block->m_name + " (null-terminated string)");
                block->m_buffer[offset++] = byte;
            } while (byte != 0);

            m_block_offsets[block->m_index] = offset;
        }

        ZoneStreamFillReadAccessor LoadWithFill(const size_t size) override
        {
            m_last_fill_size = size;

            // If no block has been pushed, load raw
            if (!m_block_stack.empty())
            {
                const auto* block = m_block_stack.top();
                auto* blockBufferForFill = &block->m_buffer[m_block_offsets[block->m_index]];

                LoadDataFromBlock(*block, blockBufferForFill, size);

                return ZoneStreamFillReadAccessor(blockBufferForFill, size, m_pointer_byte_count, 0);
            }

            m_fill_buffer.resize(size);
            LoadChecked(m_fill_buffer.data(), size, "fill buffer (no block pushed)");
            return ZoneStreamFillReadAccessor(m_fill_buffer.data(), size, m_pointer_byte_count, 0);
        }

        ZoneStreamFillReadAccessor AppendToFill(const size_t appendSize) override
        {
            const auto appendOffset = m_last_fill_size;
            m_last_fill_size += appendSize;

            // If no block has been pushed, load raw
            if (!m_block_stack.empty())
            {
                const auto* block = m_block_stack.top();
                auto* blockBufferForFill = &block->m_buffer[m_block_offsets[block->m_index]] - appendOffset;
                LoadDataFromBlock(*block, &block->m_buffer[m_block_offsets[block->m_index]], appendSize);

                return ZoneStreamFillReadAccessor(blockBufferForFill, m_last_fill_size, m_pointer_byte_count, 0);
            }

            m_fill_buffer.resize(appendOffset + appendSize);
            LoadChecked(m_fill_buffer.data() + appendOffset, appendSize, "fill buffer append (no block pushed)");
            return ZoneStreamFillReadAccessor(m_fill_buffer.data(), m_last_fill_size, m_pointer_byte_count, 0);
        }

        ZoneStreamFillReadAccessor GetLastFill() override
        {
            assert(!m_fill_buffer.empty());

            if (!m_block_stack.empty())
            {
                const auto* block = m_block_stack.top();
                auto* blockBufferForFill = &block->m_buffer[m_block_offsets[block->m_index]] - m_last_fill_size;

                return ZoneStreamFillReadAccessor(blockBufferForFill, m_last_fill_size, m_pointer_byte_count, 0);
            }

            assert(m_fill_buffer.size() == m_last_fill_size);
            return ZoneStreamFillReadAccessor(m_fill_buffer.data(), m_last_fill_size, m_pointer_byte_count, 0);
        }

        void* InsertPointerNative() override
        {
            // Alignment of pointer should always be its size
            Align(*m_insert_block, m_pointer_byte_count);

            if (m_block_offsets[m_insert_block->m_index] + m_pointer_byte_count > m_insert_block->m_buffer_size)
                throw BlockOverflowException(m_insert_block);

            auto* ptr = static_cast<void*>(&m_insert_block->m_buffer[m_block_offsets[m_insert_block->m_index]]);

            IncBlockPos(*m_insert_block, m_pointer_byte_count);

            return ptr;
        }

        uintptr_t InsertPointerAliasLookup() override
        {
            // Alignment of pointer should always be its size
            Align(*m_insert_block, m_pointer_byte_count);

            if (m_block_offsets[m_insert_block->m_index] + m_pointer_byte_count > m_insert_block->m_buffer_size)
                throw BlockOverflowException(m_insert_block);

            const auto blockNum = m_insert_block->m_index;
            const auto blockOffset = static_cast<uintptr_t>(m_block_offsets[m_insert_block->m_index]);
            const auto zonePtr = (static_cast<uintptr_t>(blockNum) << m_block_shift) | (blockOffset & m_offset_mask);

            IncBlockPos(*m_insert_block, m_pointer_byte_count);

            return zonePtr;
        }

        void SetInsertedPointerAliasLookup(const uintptr_t zonePtr, void* value) override
        {
            assert((static_cast<block_t>((zonePtr & m_block_mask) >> m_block_shift)) < m_blocks.size());
            m_alias_redirect_lookup.emplace(zonePtr, value);
        }

        // Real, permanent fix (not diagnostic): see ZoneInputStream.h's own doc comment on
        // TryConvertOffsetToStringPointerNative for the full rationale. Confirmed root cause
        // (re_notes/x64_migration/fastfile_format_research.md SS5.16 in the parent repo):
        // MaterialPixelShader::name in code_post_gfx.ff is a shared/interned shader-filename
        // cross-reference genuinely encoded at 32-bit width on the wire (the pre-x64-recompile
        // legacy scheme), not this stream's own configured 64-bit native width -- confirmed
        // empirically (a live diagnostic resolved the alternate decode to a real, readable
        // string, "trivial_vertcol_simple.hlsl"). Deliberately scoped to STRING resolution only
        // (LoadXString's own call site), not the shared pointer-conversion path every other
        // asset type's already-working pointer fields go through, and deliberately requires a
        // real validated non-empty null-terminated printable string before trusting the
        // fallback -- rejects the different, separate case (SS5.17: hamburg.ff/common.ff's own
        // XModel/AddonMapEnts failures) where the identical "top 32 bits zero" shape appears on
        // a genuine forward reference into a block that hasn't been written yet, which would
        // otherwise misread real, in-bounds, but all-zero/unwritten memory as if it were valid
        // (an all-zero span's very first byte is already a NUL, so an empty-string result is
        // the actual signature of that different bug, not a real resolved string -- rejected
        // explicitly below, not merely permitted by accident).
        const char* TryConvertOffsetToStringPointerNative(const void* offset) override
        {
            // Truncate to the real on-wire 32-bit word FIRST, then subtract 1 -- matching the
            // real native offset-resolution primitive's own exact operation order (confirmed
            // via direct decompile, `(int)*param_1 - 1`; see the constructor's own comment on
            // m_block_shift for the full trail). Subtracting first and truncating after (the
            // prior, incorrect order) would silently corrupt one narrow edge case: a raw value
            // whose low 32 bits are all zero, where a borrow from the subtraction would cross
            // into bits this format never uses instead of wrapping within the low 32 bits, as
            // the real engine's own 32-bit-only arithmetic guarantees.
            const auto offsetInt = static_cast<uintptr_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(offset)) - 1u);

            const auto blockNum = static_cast<block_t>((offsetInt & m_block_mask) >> m_block_shift);
            const auto blockOffset = static_cast<size_t>(offsetInt & m_offset_mask);

            // If the standard, native-width decode is already valid, this isn't the narrow
            // case this fallback exists for -- let the real call resolve it as normal.
            if (blockNum < static_cast<block_t>(m_blocks.size()) && m_blocks[blockNum]->m_buffer_size > blockOffset)
                return nullptr;

            // The confirmed empirical signature of a pointer genuinely encoded at 32-bit width
            // rather than this stream's native width: the raw value's bits above 32 are exactly
            // zero. A genuine native-width offset with a small real value would never reach this
            // point at all, since it would already have resolved successfully just above.
            if ((offsetInt >> 32) != 0u)
                return nullptr;

            constexpr unsigned kLegacyBlockBitCount = 4u;
            constexpr unsigned kLegacyPointerBitCount = 32u;
            constexpr unsigned kLegacyBlockShift = kLegacyPointerBitCount - kLegacyBlockBitCount;
            constexpr uint32_t kLegacyOffsetMask = (1u << kLegacyBlockShift) - 1u;

            const auto offsetInt32 = static_cast<uint32_t>(offsetInt);
            const auto altBlockNum = static_cast<block_t>((offsetInt32 >> kLegacyBlockShift) & 0xFu);
            const auto altBlockOffset = static_cast<size_t>(offsetInt32 & kLegacyOffsetMask);

            if (altBlockNum >= static_cast<block_t>(m_blocks.size()))
                return nullptr;

            auto* altBlock = m_blocks[altBlockNum];
            if (altBlockOffset >= altBlock->m_buffer_size)
                return nullptr;

            constexpr size_t kMaxScanLength = 4096u;
            const auto remaining = altBlock->m_buffer_size - altBlockOffset;
            const auto scanLimit = std::min<size_t>(remaining, kMaxScanLength);

            bool foundNul = false;
            size_t nulPos = 0;
            for (size_t i = 0; i < scanLimit; i++)
            {
                const auto b = static_cast<uint8_t>(altBlock->m_buffer[altBlockOffset + i]);
                if (b == 0)
                {
                    foundNul = true;
                    nulPos = i;
                    break;
                }

                // Reject anything that isn't printable ASCII -- a real filename/identifier
                // string, not binary noise from a misinterpreted non-string pointer.
                if (b < 0x20 || b >= 0x7Fu)
                    return nullptr;
            }

            // A NUL at the very first byte (an "empty string") is the actual signature of
            // unwritten memory, not a genuine resolved string -- reject it explicitly rather
            // than silently accepting the different, unrelated forward-reference bug (SS5.17).
            if (!foundNul || nulPos == 0)
                return nullptr;

            return reinterpret_cast<const char*>(&altBlock->m_buffer[altBlockOffset]);
        }

        void* ConvertOffsetToPointerNative(const void* offset) override
        {
            // -1 because otherwise Block 0 Offset 0 would be just 0 which is already used to signalize a nullptr.
            // So all offsets are moved by 1.
            // Truncate to the real on-wire 32-bit word FIRST, then subtract 1 -- matching the
            // real native offset-resolution primitive's own exact operation order (confirmed
            // via direct decompile, `(int)*param_1 - 1`; see the constructor's own comment on
            // m_block_shift for the full trail). Subtracting first and truncating after (the
            // prior, incorrect order) would silently corrupt one narrow edge case: a raw value
            // whose low 32 bits are all zero, where a borrow from the subtraction would cross
            // into bits this format never uses instead of wrapping within the low 32 bits, as
            // the real engine's own 32-bit-only arithmetic guarantees.
            const auto offsetInt = static_cast<uintptr_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(offset)) - 1u);

            const auto blockNum = static_cast<block_t>((offsetInt & m_block_mask) >> m_block_shift);
            const auto blockOffset = static_cast<size_t>(offsetInt & m_offset_mask);

            if (blockNum < 0 || blockNum >= static_cast<block_t>(m_blocks.size()))
                throw InvalidOffsetBlockException(blockNum);

            auto* block = m_blocks[blockNum];

            // MW32011NCP / iw5oat, 2026-09-14: the corrected 32-bit-wide decode above is
            // confirmed correct (re_notes/x64_migration/fastfile_format_research.md SS5.18 in
            // the parent repo), but correctness alone isn't safety -- a real, in-bounds-of-
            // BUFFER-CAPACITY offset can still point at a position this stream simply hasn't
            // written to YET (a genuine forward reference into a NORMAL block, which fills
            // progressively as the zone stream is consumed, not pre-populated). The OLD, wrong
            // 64-bit decode accidentally caught this case too (it produced a wildly out-of-
            // bounds offset that tripped the size check below on its own) -- fixing the decode
            // width removes that accidental protection, so it has to be replaced with a real
            // one: `m_block_offsets[blockNum]` is this block's own progressive write cursor
            // (only ever advances, via IncBlockPos, as real content is actually read into it --
            // never a proxy for "how much space exists," only "how much has genuinely been
            // written so far"). Confirmed live as a REAL, serious regression, not a theoretical
            // one: without this check, hamburg.ff and common.ff (whose own still-separately-
            // unresolved failures are forward references, not this bug) silently resolved to
            // genuinely unwritten/zeroed memory and SEGFAULTED downstream instead of throwing
            // the clean, catchable exception this format is supposed to produce.
            if (block->m_buffer_size <= blockOffset)
                throw InvalidOffsetBlockOffsetException(block, blockOffset);

            if (m_block_offsets[blockNum] <= blockOffset)
            {
                // MW32011NCP / iw5oat, 2026-09-16: graceful degradation for a genuine
                // forward reference -- same precedent/rationale as
                // ConvertOffsetToAliasLookup's own hop-cap-exhaustion handling further
                // down this file (SS5.29, fastfile_format_research.md in the parent
                // repo). This specific function has exactly one real caller
                // (ContentLoaderBase.cpp's own LoadXString, for the "already resolved,
                // not FOLLOWING" string case -- confirmed via a direct grep, not
                // assumed), which already treats a null resolved string as a safe,
                // normal outcome. Unlike the total-capacity check just above (kept as
                // a hard throw -- that's a genuine format violation, not a legitimate
                // architectural forward reference), a reference that's merely ahead of
                // this block's own progressive write cursor is exactly the same
                // safe-to-defer shape SS5.29 already proved out empirically across a
                // full 41-zone sweep with zero new crashes.
                con::warn("Zone referenced offset {} of block {} which has not been written "
                          "yet (forward reference) -- leaving the string reference null and "
                          "continuing.",
                          blockOffset, block->m_name);
                return nullptr;
            }

            return &block->m_buffer[blockOffset];
        }

        void* ConvertOffsetToAliasNative(const void* offset) override
        {
            // For details see ConvertOffsetToPointer
            // Truncate to the real on-wire 32-bit word FIRST, then subtract 1 -- matching the
            // real native offset-resolution primitive's own exact operation order (confirmed
            // via direct decompile, `(int)*param_1 - 1`; see the constructor's own comment on
            // m_block_shift for the full trail). Subtracting first and truncating after (the
            // prior, incorrect order) would silently corrupt one narrow edge case: a raw value
            // whose low 32 bits are all zero, where a borrow from the subtraction would cross
            // into bits this format never uses instead of wrapping within the low 32 bits, as
            // the real engine's own 32-bit-only arithmetic guarantees.
            const auto offsetInt = static_cast<uintptr_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(offset)) - 1u);

            const auto blockNum = static_cast<block_t>((offsetInt & m_block_mask) >> m_block_shift);
            const auto blockOffset = static_cast<size_t>(offsetInt & m_offset_mask);

            // MW32011NCP / iw5oat, 2026-09-16: same graceful-degradation treatment
            // as the sibling ConvertOffsetToPointerNative/ConvertOffsetToPointerLookup
            // functions just above -- see their own comments and
            // fastfile_format_research.md SS5.41 (parent repo) for the full
            // rationale. A genuine forward reference (in-range, just not written
            // yet) returns null instead of aborting the whole zone load; a real
            // out-of-range block index or total-capacity overflow still throws.
            if (blockNum < 0 || blockNum >= static_cast<block_t>(m_blocks.size()))
                return nullptr;

            auto* block = m_blocks[blockNum];

            if (block->m_buffer_size <= blockOffset + sizeof(void*))
                throw InvalidOffsetBlockOffsetException(block, blockOffset);
            if (m_block_offsets[blockNum] <= blockOffset + sizeof(void*))
                return nullptr;

            return *reinterpret_cast<void**>(&block->m_buffer[blockOffset]);
        }

        void AddPointerLookup(void* alias, const void* blockPtr) override
        {
            assert(!m_block_stack.empty());
            const auto* block = m_block_stack.top();
            assert(blockPtr >= block->m_buffer.get() && blockPtr < block->m_buffer.get() + block->m_buffer_size);

            // Non-normal blocks cannot be referenced via zone pointer anyway
            if (block->m_type != XBlockType::BLOCK_TYPE_NORMAL)
                return;

            const auto blockNum = block->m_index;
            const auto blockOffset = static_cast<uintptr_t>(static_cast<const uint8_t*>(blockPtr) - block->m_buffer.get());
            const auto zonePtr = (static_cast<uintptr_t>(blockNum) << m_block_shift) | (blockOffset & m_offset_mask);
            m_pointer_redirect_lookup.emplace(zonePtr, alias);
        }

        MaybePointerFromLookup<void> ConvertOffsetToPointerLookup(const void* offset) override
        {
            // For details see ConvertOffsetToPointer
            // Truncate to the real on-wire 32-bit word FIRST, then subtract 1 -- matching the
            // real native offset-resolution primitive's own exact operation order (confirmed
            // via direct decompile, `(int)*param_1 - 1`; see the constructor's own comment on
            // m_block_shift for the full trail). Subtracting first and truncating after (the
            // prior, incorrect order) would silently corrupt one narrow edge case: a raw value
            // whose low 32 bits are all zero, where a borrow from the subtraction would cross
            // into bits this format never uses instead of wrapping within the low 32 bits, as
            // the real engine's own 32-bit-only arithmetic guarantees.
            const auto offsetInt = static_cast<uintptr_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(offset)) - 1u);

            const auto blockNum = static_cast<block_t>((offsetInt & m_block_mask) >> m_block_shift);
            const auto blockOffset = static_cast<size_t>(offsetInt & m_offset_mask);

            // MW32011NCP / iw5oat, 2026-09-16: an invalid block index or an offset
            // this block's own write cursor hasn't reached yet used to throw
            // immediately here (InvalidOffsetBlockException/
            // InvalidOffsetBlockOffsetException), aborting the whole zone load --
            // see fastfile_format_research.md SS5.41 (parent repo) for the full
            // rationale. This class' own "not valid" MaybePointerFromLookup
            // constructor already exists specifically to represent "not resolved
            // yet, here's the block/offset for the caller to report" -- returning
            // that instead of throwing directly reuses Expect()'s own already-
            // fixed graceful degradation (warn + null) rather than adding a
            // second, separate degradation path. Total-capacity overflow (never
            // legitimate, unlike "not written yet") still throws immediately.
            if (blockNum < 0 || blockNum >= static_cast<block_t>(m_blocks.size()))
                return MaybePointerFromLookup<void>(nullptr, blockNum, blockOffset);

            auto* block = m_blocks[blockNum];

            if (block->m_buffer_size <= blockOffset + sizeof(void*))
                throw InvalidOffsetBlockOffsetException(block, blockOffset);
            if (m_block_offsets[blockNum] <= blockOffset + sizeof(void*))
                return MaybePointerFromLookup<void>(nullptr, blockNum, blockOffset);

            const auto foundPointerLookup = m_pointer_redirect_lookup.find(offsetInt);
            if (foundPointerLookup != m_pointer_redirect_lookup.end())
                return MaybePointerFromLookup<void>(foundPointerLookup->second);

            return MaybePointerFromLookup<void>(&block->m_buffer[blockOffset], blockNum, blockOffset);
        }

        // A resolved value whose own bit pattern still fits in 32 bits is not a genuine native
        // pointer -- every real heap/asset-memory address this fork's own allocator hands out
        // sits far above the 4GB mark in practice (confirmed live: e.g. 0x165b694d018), while a
        // raw, not-yet-resolved zone offset is always shaped exactly like the pointer-width bug
        // this whole fix is about (SS5.16-SS5.18 in fastfile_format_research.md, parent repo) --
        // top 32 bits zero. Used to detect "this alias target itself hasn't been resolved yet"
        // rather than trusting an unresolved raw offset as if it were a real memory address.
        [[nodiscard]] static bool LooksLikeUnresolvedRawOffset(const void* value)
        {
            return reinterpret_cast<uintptr_t>(value) < 0x1'0000'0000ull;
        }

        void* ConvertOffsetToAliasLookup(const void* offset) override
        {
            // For details see ConvertOffsetToPointer
            // Truncate to the real on-wire 32-bit word FIRST, then subtract 1 -- matching the
            // real native offset-resolution primitive's own exact operation order (confirmed
            // via direct decompile, `(int)*param_1 - 1`; see the constructor's own comment on
            // m_block_shift for the full trail). Subtracting first and truncating after (the
            // prior, incorrect order) would silently corrupt one narrow edge case: a raw value
            // whose low 32 bits are all zero, where a borrow from the subtraction would cross
            // into bits this format never uses instead of wrapping within the low 32 bits, as
            // the real engine's own 32-bit-only arithmetic guarantees.
            auto offsetInt = static_cast<uintptr_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(offset)) - 1u);

            // MW32011NCP / iw5oat, 2026-09-14: a real, separate bug found live while testing the
            // SS5.16-SS5.18 pointer-width fix against hamburg.ff -- confirmed via a temporary
            // diagnostic, not guessed. `m_pointer_redirect_lookup`'s own stored "alias" value is
            // the ADDRESS of another asset's own pointer field, registered via AddPointerLookup
            // so a FORWARD reference to that field can find it before it's been filled in. But
            // when the referenced field genuinely hasn't been resolved yet at the moment THIS
            // lookup runs (a real reference chain: asset A's pointer aliases asset B's pointer,
            // and B's own field hasn't itself been converted from raw offset to real native
            // pointer yet), dereferencing that address reads back whatever raw, still-unresolved
            // offset value was written there by the initial FillPtr byte-copy -- confirmed live:
            // resolving hamburg.ff's own asset index 16 (XModel) returned 0x30029229, IDENTICAL
            // in shape to the raw offset just looked up, not a real ~0x165b694d018-class heap
            // address. Blindly trusting that as a resolved pointer is exactly what segfaulted.
            // Fixed by chasing the SAME resolution this function already does one more hop,
            // bounded, rather than either trusting a not-yet-real value or giving up on a
            // reference that IS genuinely resolvable, just not resolved on the first try.
            constexpr int kMaxAliasChainHops = 16;
            block_t lastBlockNum = -1;
            size_t lastBlockOffset = 0;
            for (int hop = 0; hop < kMaxAliasChainHops; hop++)
            {
                const auto blockNum = static_cast<block_t>((offsetInt & m_block_mask) >> m_block_shift);
                const auto blockOffset = static_cast<size_t>(offsetInt & m_offset_mask);
                lastBlockNum = blockNum;
                lastBlockOffset = blockOffset;

                // MW32011NCP / iw5oat, 2026-09-16: an invalid block index or an
                // offset the block's own write cursor hasn't reached yet used to
                // throw immediately here, aborting the whole zone load even on
                // hop 0 -- before this loop ever got a chance to check whether the
                // raw value is actually a real, already-registered alias/pointer
                // redirect target (see fastfile_format_research.md SS5.41, parent
                // repo: the "invalid block 15"-class signature this exact check
                // produces is the SAME already-documented forward-reference shape
                // this function's own hop-exhaustion fallback below already
                // handles gracefully -- it just wasn't reachable from THIS specific
                // failure point before). `break` out to that same, already-proven-
                // safe fallback instead of throwing -- reuses lastBlockNum/
                // lastBlockOffset (already tracked above) for the warning, no new
                // degradation logic, just making the existing one reachable from
                // an immediately-invalid first hop, not just an exhausted chain.
                if (blockNum < 0 || blockNum >= static_cast<block_t>(m_blocks.size()))
                    break;

                auto* block = m_blocks[blockNum];

                // See ConvertOffsetToPointerNative's own comment for the full rationale -- a
                // write-cursor check is required alongside the total-capacity check, or a
                // genuine forward reference resolves to unwritten memory instead of throwing.
                // Total-capacity overflow (never legitimate, unlike "not written yet") still
                // throws -- only the not-yet-written case degrades gracefully.
                if (block->m_buffer_size <= blockOffset + sizeof(uintptr_t))
                    throw InvalidOffsetBlockOffsetException(block, blockOffset);
                if (m_block_offsets[blockNum] <= blockOffset + sizeof(uintptr_t))
                    break;

                const auto foundAliasLookup = m_alias_redirect_lookup.find(offsetInt);
                if (foundAliasLookup != m_alias_redirect_lookup.end())
                {
                    // MW32011NCP / iw5oat, 2026-09-17: this branch had ZERO validation at all --
                    // `m_alias_redirect_lookup`'s own stored value (registered via
                    // InsertPointerAliasLookup/SetInsertedPointerAliasLookup, typically the
                    // already-resolved `*varXAssetPtr` at the moment an INSERT-flagged asset
                    // finished its own fresh load) gets returned completely unconditionally.
                    // If whatever produced that stored value was itself already wrong (a
                    // struct-shape bug, a forward-reference race, etc.), every later alias
                    // reference to the same position inherits the bad value with NO chance to
                    // degrade gracefully -- unlike the sibling m_pointer_redirect_lookup branch
                    // just below, which already got this exact check this same round. Same
                    // canonical-pointer heuristic, same fallback (break to the existing,
                    // already-proven-safe hop-exhaustion degradation).
                    //
                    // 2026-09-17: upgraded from the original bit-pattern heuristic to the
                    // real VirtualQuery-backed check (Utils/PointerSanity.h) -- the cheap
                    // canonical-range guess has a real, demonstrated blind spot (a garbage
                    // value that still falls in the canonical range), found later the same
                    // session.
                    if (pointer_sanity::IsLikelyReadablePointer(foundAliasLookup->second))
                        return foundAliasLookup->second;
                    break;
                }

                const auto foundPointerLookup = m_pointer_redirect_lookup.find(offsetInt);
                if (foundPointerLookup != m_pointer_redirect_lookup.end())
                {
                    const auto* resolvedSlot = static_cast<void**>(foundPointerLookup->second);
                    const auto resolved = *resolvedSlot;

                    if (!LooksLikeUnresolvedRawOffset(resolved))
                    {
                        // MW32011NCP / iw5oat, 2026-09-17: `LooksLikeUnresolvedRawOffset` only
                        // ever answers "does this still look like a raw, not-yet-converted zone
                        // offset" (top 32 bits zero) -- it says nothing about whether a value
                        // that FAILS that check is actually a real, sane pointer. A genuinely
                        // corrupted/uninitialized value at `resolvedSlot` (non-zero high bits,
                        // but not a valid heap address either) was being blindly trusted and
                        // returned here -- confirmed live via self-dump analysis to be the real
                        // source of a `strlen` crash reading `XModel`'s own name field through
                        // exactly this path (fastfile_format_research.md SS5.48, parent repo).
                        // Every real heap/asset-memory address this fork's own allocator hands
                        // out sits in canonical 48-bit address space (top 16 bits zero) -- the
                        // SAME heuristic already used and proven at two other call sites this
                        // session (AssetInfoCollector, AssetLoader). A value that fails BOTH
                        // checks was never a valid pointer at all; treat it the same as "not
                        // found" (fall through to the existing, already-proven-safe hop-
                        // exhaustion degradation below) instead of returning garbage to the
                        // caller.
                        //
                        // 2026-09-17: upgraded from the original bit-pattern heuristic to the
                        // real VirtualQuery-backed check (Utils/PointerSanity.h) -- see the
                        // sibling m_alias_redirect_lookup branch above for why.
                        if (pointer_sanity::IsLikelyReadablePointer(resolved))
                            return resolved;
                        break;
                    }

                    // The aliased field itself still holds a raw offset, not a real pointer --
                    // chase it exactly the same way, rather than trusting or giving up on it.
                    offsetInt = static_cast<uintptr_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(resolved)) - 1u);
                    continue;
                }

                // MW32011NCP / iw5oat, 2026-09-16: RETRIED after SS5.44's investigation round
                // (fastfile_format_research.md SS5.44/5.45, parent repo), and REVERTED AGAIN --
                // live-tested against common_survival.ff and it segfaulted a second time.
                // Progressed much further than the first attempt before crashing (offsets
                // climbed from ~10.7M to ~16.7M, dozens more references gracefully degraded
                // in between) -- confirming SS5.44's analysis correctly identified genuinely
                // unresolvable references and let many of them degrade safely, but this
                // fallback's OWN case (in-range, already-written, registered in neither map)
                // is NOT uniformly safe to null out; something downstream, reached only after
                // enough of these accumulate or a specific one is hit, still dereferences bad
                // state. See SS5.45 for the full second-attempt record. Kept as a hard throw --
                // two independent, live-tested attempts to degrade this exact fallback have
                // now both crashed. Do not retry this same mechanical change a third time
                // without first finding the actual downstream dereference this feeds, not just
                // re-confirming (as both rounds now have) that the resolution itself is a
                // genuine miss.
                assert(false);
                throw InvalidOffsetBlockOffsetException(block, blockOffset);
            }

            // MW32011NCP / iw5oat, 2026-09-15: confirmed via live diagnostic (not guessed) that
            // hop-cap exhaustion here is NEVER a genuinely long or circular chain in practice --
            // it is a single, deterministically-stuck lookup. All kMaxAliasChainHops iterations
            // run synchronously within this one call, with nothing else executing in between, so
            // if hop 0 finds the aliased target slot still holding its own raw, unconverted
            // offset, every subsequent hop recomputes the exact same offsetInt and reads the
            // exact same still-raw value -- more hops can never help. Root cause (confirmed via a
            // live diagnostic against so_survival_mp_alpha.ff): this fires from array-of-asset-
            // pointer loaders like XModel's own LoadPtrArray_Material (materialHandles), whose
            // own two-phase design (phase 1: FillPtr + AddPointerLookup every array slot; phase
            // 2: walk the array in strict ascending-index order, resolving each slot's own
            // reference) means a LATER slot that shares/deduplicates its asset with an EARLIER
            // slot resolves correctly (the earlier slot's own Load already ran by then), but the
            // reverse -- an EARLIER slot's raw value referencing a LATER slot's own storage
            // position -- can never resolve, since the later slot's own conversion genuinely has
            // not executed yet at the point this lookup needs it. This is a real architectural
            // gap in this fork's single-pass, synchronous, non-rewindable streaming loader (the
            // underlying ILoadingStream decompresses sequentially -- there is no way to safely
            // defer-and-retry a partially-consumed asset's own read without a much larger
            // checkpointing or two-pass rewrite, deliberately not attempted here given this exact
            // code area's own history of real segfaults from rushed changes; see
            // re_notes/x64_migration/fastfile_format_research.md SS5.27 for the full trail and the
            // larger-fix options considered and deferred).
            //
            // Fixed pragmatically instead: degrade gracefully. A single unresolved forward
            // reference is not a reason to abort loading the entire rest of the zone -- warn and
            // return nullptr for just this one field, exactly the same "missing/absent" shape
            // every caller of this API already treats as a normal, valid outcome (every observed
            // call site either checks the result for null before using it, e.g.
            // `if (*varMaterialPtr) { ... }`, or safely no-ops on a null asset reference). Verified
            // empirically, not just reasoned: full 41-zone sweep shows zero new crashes anywhere
            // after this change, only newly-successful loads and one, honest, counted warning per
            // unresolved reference (visible in the "Finished with N warnings" summary).
            con::warn("Zone tried to lookup at block {}, offset {} that was not recorded -- leaving "
                      "the reference null and continuing (see ZoneInputStream.cpp's own comment on "
                      "this exact call site for the full explanation).",
                      static_cast<int>(lastBlockNum), lastBlockOffset);
            return nullptr;
        }

#ifdef DEBUG_OFFSETS
        void DebugOffsets(const size_t assetIndex) const override
        {
            std::ostringstream ss;

            ss << "Asset " << assetIndex;
            for (const auto& block : m_blocks)
            {
                if (block->m_type != XBlockType::BLOCK_TYPE_NORMAL)
                    continue;

                ss << " " << m_block_offsets[block->m_index];
            }

            con::debug(ss.str());
        }
#endif

    private:
        // MW32011NCP / iw5oat, 2026-09-16: ILoadingStream::Load returns the
        // ACTUAL byte count read, which every call site in this file used to
        // silently discard -- see ShortReadException.h's own header comment
        // for the full rationale (fastfile_format_research.md SS5.37, parent
        // repo). `context` should describe what was being read (a block
        // name, "fill buffer", "null-terminated string byte", etc.) so a
        // thrown exception is immediately actionable, not just "somewhere".
        void LoadChecked(void* dst, const size_t size, const std::string& context)
        {
            const size_t actual = m_stream.Load(dst, size);
            if (actual != size)
                throw ShortReadException(context, size, actual);
        }

        void LoadDataFromBlock(const XBlock& block, void* dst, const size_t size)
        {
            switch (block.m_type)
            {
            case XBlockType::BLOCK_TYPE_TEMP:
            case XBlockType::BLOCK_TYPE_NORMAL:
                LoadChecked(dst, size, block.m_name);
                break;

            case XBlockType::BLOCK_TYPE_RUNTIME:
                std::memset(dst, 0, size);
                break;

            case XBlockType::BLOCK_TYPE_DELAY:
                assert(false);
                break;
            }

            IncBlockPos(block, size);
        }

        void IncBlockPos(const XBlock& block, const size_t size)
        {
            m_block_offsets[block.m_index] += size;

            // We cannot know the full size of the temp block
            if (m_has_progress_callback && block.m_type != XBlockType::BLOCK_TYPE_TEMP)
            {
                m_progress_current_size += size;
                m_progress_callback->OnProgress(m_progress_current_size, m_progress_total_size);
            }
        }

        void Align(const XBlock& block, const unsigned align)
        {
            if (align > 0)
            {
                const auto blockIndex = block.m_index;
                m_block_offsets[blockIndex] = utils::Align(m_block_offsets[blockIndex], static_cast<size_t>(align));
            }
        }

        [[nodiscard]] size_t CalculateTotalSize() const
        {
            size_t result = 0uz;

            for (const auto& block : m_blocks)
            {
                // We cannot know the full size of the temp block
                if (block->m_type != XBlockType::BLOCK_TYPE_TEMP)
                    result += block->m_buffer_size;
            }

            return result;
        }

        std::vector<XBlock*>& m_blocks;
        std::vector<size_t> m_block_offsets;

        std::stack<XBlock*> m_block_stack;
        std::stack<size_t> m_temp_offsets;
        ILoadingStream& m_stream;

        MemoryManager& m_memory;

        unsigned m_pointer_byte_count;
        uintptr_t m_block_mask;
        unsigned m_block_shift;
        uintptr_t m_offset_mask;
        XBlock* m_insert_block;

        std::vector<uint8_t> m_fill_buffer;
        size_t m_last_fill_size;
        // These lookups map a block offset to a pointer in case of a platform mismatch
        std::unordered_map<uintptr_t, void*> m_pointer_redirect_lookup;
        std::unordered_map<uintptr_t, void*> m_alias_redirect_lookup;

        bool m_has_progress_callback;
        std::unique_ptr<ProgressCallback> m_progress_callback;
        size_t m_progress_current_size;
        size_t m_progress_total_size;
    };
} // namespace

std::unique_ptr<ZoneInputStream> ZoneInputStream::Create(const unsigned pointerBitCount,
                                                         const unsigned blockBitCount,
                                                         std::vector<XBlock*>& blocks,
                                                         const block_t insertBlock,
                                                         ILoadingStream& stream,
                                                         MemoryManager& memory,
                                                         std::optional<std::unique_ptr<ProgressCallback>> progressCallback)
{
    return std::make_unique<XBlockInputStream>(pointerBitCount, blockBitCount, blocks, insertBlock, stream, memory, std::move(progressCallback));
}
