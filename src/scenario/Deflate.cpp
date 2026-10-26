#include "scenario/Deflate.h"

#include <algorithm>

namespace Scenario {

namespace {

constexpr int kWindow = 32768;
constexpr int kWindowMask = kWindow - 1;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kHashSize = 1 << kHashBits;
constexpr int kMaxChain = 256;
// A match this long is taken without looking for a longer one.
constexpr int kNiceMatch = 128;

// Bits out, least significant first, as deflate packs them.
struct Bits {
    std::vector<uint8_t> out;
    uint32_t buffer = 0;
    int count = 0;

    void Put(uint32_t value, int n) {
        buffer |= value << count;
        count += n;
        while (count >= 8) {
            out.push_back(static_cast<uint8_t>(buffer & 0xFFu));
            buffer >>= 8;
            count -= 8;
        }
    }
    // A Huffman code, which deflate stores from its most significant bit.
    void Code(uint32_t code, int n) {
        uint32_t reversed = 0;
        for (int i = 0; i < n; ++i) reversed |= ((code >> i) & 1u) << (n - 1 - i);
        Put(reversed, n);
    }
    void Flush() {
        if (count > 0) out.push_back(static_cast<uint8_t>(buffer & 0xFFu));
        buffer = 0;
        count = 0;
    }
};

// The fixed literal/length code (RFC 1951 3.2.6).
void Symbol(Bits& bits, int symbol) {
    if (symbol <= 143) bits.Code(0x30u + static_cast<uint32_t>(symbol), 8);
    else if (symbol <= 255) bits.Code(0x190u + static_cast<uint32_t>(symbol - 144), 9);
    else if (symbol <= 279) bits.Code(static_cast<uint32_t>(symbol - 256), 7);
    else bits.Code(0xC0u + static_cast<uint32_t>(symbol - 280), 8);
}

constexpr int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr int kDistanceBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                   33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr int kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void Match(Bits& bits, int length, int distance) {
    int l = 28;
    while (kLengthBase[l] > length) --l;
    Symbol(bits, 257 + l);
    if (kLengthExtra[l] > 0) bits.Put(static_cast<uint32_t>(length - kLengthBase[l]), kLengthExtra[l]);
    int d = 29;
    while (kDistanceBase[d] > distance) --d;
    bits.Code(static_cast<uint32_t>(d), 5);
    if (kDistanceExtra[d] > 0) {
        bits.Put(static_cast<uint32_t>(distance - kDistanceBase[d]), kDistanceExtra[d]);
    }
}

uint32_t Hash(const uint8_t* p) {
    const uint32_t v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16);
    return (v * 2654435761u) >> (32 - kHashBits);
}

} // namespace

std::vector<uint8_t> Deflate(const uint8_t* data, size_t size) {
    Bits bits;
    bits.out.reserve(size / 3 + 64);
    bits.Put(1, 1);   // the final block
    bits.Put(1, 2);   // fixed Huffman codes

    std::vector<int> head(kHashSize, -1);
    std::vector<int> prev(kWindow, -1);
    const int n = static_cast<int>(size);
    auto insert = [&](int at) {
        if (at + kMinMatch > n) return;
        const uint32_t h = Hash(data + at);
        prev[at & kWindowMask] = head[h];
        head[h] = at;
    };
    // The longest earlier match for the bytes at `at`, within the window.
    auto longest = [&](int at, int& distance) {
        int best = 0;
        if (at + kMinMatch > n) return best;
        const int limit = std::min(kMaxMatch, n - at);
        int candidate = head[Hash(data + at)];
        for (int chain = 0; candidate >= 0 && chain < kMaxChain; ++chain) {
            if (at - candidate > kWindow - 1) break;
            if (data[candidate + best] == data[at + best]) {
                int len = 0;
                while (len < limit && data[candidate + len] == data[at + len]) ++len;
                if (len > best) {
                    best = len;
                    distance = at - candidate;
                    if (len >= kNiceMatch || len == limit) break;
                }
            }
            const int next = prev[candidate & kWindowMask];
            if (next >= candidate) break;
            candidate = next;
        }
        return best >= kMinMatch ? best : 0;
    };

    int at = 0;
    while (at < n) {
        int distance = 0;
        const int length = longest(at, distance);
        if (length == 0) {
            Symbol(bits, data[at]);
            insert(at);
            ++at;
            continue;
        }
        // One step of lazy matching: a longer match starting at the next byte
        // is worth a literal first.
        insert(at);
        int next_distance = 0;
        const int next_length = length < kNiceMatch ? longest(at + 1, next_distance) : 0;
        if (next_length > length) {
            Symbol(bits, data[at]);
            ++at;
            continue;
        }
        Match(bits, length, distance);
        for (int i = 1; i < length; ++i) insert(at + i);
        at += length;
    }
    Symbol(bits, 256);   // the end of the block
    bits.Flush();
    return std::move(bits.out);
}

} // namespace Scenario
