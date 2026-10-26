#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Raw deflate (RFC 1951, no zlib header or checksum): the form a scenario's
// body is stored in after its plain header, read back with the vendored
// stb_image's inflate.
//
// LZ77 over the 32 KB window with hash chains and one step of lazy matching,
// coded with the fixed Huffman tables in one final block. The compressed
// bytes differ from zlib's; files are compared on their inflated bytes
// (Scenario::Expand).
namespace Scenario {

std::vector<uint8_t> Deflate(const uint8_t* data, size_t size);

} // namespace Scenario
