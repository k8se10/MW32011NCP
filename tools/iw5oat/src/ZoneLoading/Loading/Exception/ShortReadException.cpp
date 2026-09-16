#include "ShortReadException.h"

ShortReadException::ShortReadException(std::string context, const size_t requested, const size_t actual)
    : m_context(std::move(context)),
      m_requested(requested),
      m_actual(actual)
{
}

std::string ShortReadException::DetailedMessage()
{
    return "Short read while loading zone content (" + m_context + "): requested " + std::to_string(m_requested)
        + " bytes, actually read " + std::to_string(m_actual) + " bytes -- the underlying stream ran out of data.";
}

char const* ShortReadException::what() const noexcept
{
    return "Short read while loading zone content.";
}
