#pragma once
#include <cstdint>
#include <string>

#include "core/FixedPoint.h"

// How a scenario's text and numbers are stored, both ways: the reader's
// (scenario/Scenario.cpp) and the writer's (scenario/Writer.cpp).
namespace Scenario::Encoding {

// The file's Windows-1252 text as UTF-8, up to its first NUL.
std::string Utf8(const std::string& cp1252);
// UTF-8 as Windows-1252, the inverse: a character Windows-1252 has no byte
// for becomes '?'.
std::string Cp1252(const std::string& utf8);

// A float of the file as fixed point, exactly, from its bits.
Core::Fixed32 FixedFromBits(uint32_t bits);
// Fixed point as the nearest float's bits, rounding to even: the exact
// inverse for any value FixedFromBits gives.
uint32_t BitsFromFixed(Core::Fixed32 value);

} // namespace Scenario::Encoding
