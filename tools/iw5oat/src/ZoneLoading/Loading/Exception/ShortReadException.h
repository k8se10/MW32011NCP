#pragma once
#include "LoadingException.h"
#include <cstddef>
#include <string>

// MW32011NCP / iw5oat, 2026-09-16: added while chasing the still-open
// "invalid block 15"-class corruption (fastfile_format_research.md SS5.28-
// SS5.37 in the parent repo). Every real content read in ZoneInputStream.cpp
// calls ILoadingStream::Load(dst, size), which returns the ACTUAL number of
// bytes read (it can legitimately be less than requested on a real
// end-of-stream) -- but every call site silently discarded that return
// value, so a genuine short read (the underlying decompression/processor
// chain running out of data mid-struct, for whatever reason) left the
// destination buffer's un-filled tail as whatever bytes were already there
// (stale content from an earlier read/allocation) with NO error at the
// actual point of failure. That silent corruption would only surface much
// later, as a confusing, far-removed "tried to reference invalid block N"
// exception once the stale/garbage bytes got misinterpreted as a real zone
// pointer -- exactly the shape this whole investigation kept re-finding.
// This exception converts a short read into a loud, immediately-thrown,
// precisely-located error instead, carrying the real block/context name and
// the exact requested-vs-actual byte counts -- whether or not a short read
// turns out to be the actual root cause of any specific still-open case, a
// short read should never be allowed to silently corrupt data either way.
class ShortReadException final : public LoadingException
{
    std::string m_context;
    size_t m_requested;
    size_t m_actual;

public:
    ShortReadException(std::string context, size_t requested, size_t actual);

    std::string DetailedMessage() override;
    char const* what() const noexcept override;
};
