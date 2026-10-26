#pragma once
#include <cstdint>
#include <compare>

namespace Core {

// 32.32 fixed point, for a simulation that is deterministic across CPUs and
// platforms. No float or double may be used in the simulation loop.
class Fixed32 {
public:
    int64_t value;
    static constexpr int FRACTIONAL_BITS = 32;
    static constexpr int64_t ONE = 1LL << FRACTIONAL_BITS;

    constexpr Fixed32() : value(0) {}
    constexpr explicit Fixed32(int64_t raw_val, bool /*is_raw*/) : value(raw_val) {}
    constexpr Fixed32(int i) : value(static_cast<int64_t>(i) << FRACTIONAL_BITS) {}
    
    // For loading static data only; never during a simulation tick.
    constexpr explicit Fixed32(float f) : value(static_cast<int64_t>(f * ONE)) {} 

    constexpr Fixed32 operator+(const Fixed32& other) const { return Fixed32(value + other.value, true); }
    constexpr Fixed32 operator-(const Fixed32& other) const { return Fixed32(value - other.value, true); }
    
    constexpr Fixed32 operator*(const Fixed32& other) const {
        // 128-bit intermediate product, so it cannot overflow.
        __int128_t prod = static_cast<__int128_t>(value) * other.value;
        return Fixed32(static_cast<int64_t>(prod >> FRACTIONAL_BITS), true);
    }
    
    constexpr Fixed32 operator/(const Fixed32& other) const {
        __int128_t num = static_cast<__int128_t>(value) << FRACTIONAL_BITS;
        return Fixed32(static_cast<int64_t>(num / other.value), true);
    }

    auto operator<=>(const Fixed32&) const = default;

    // For rendering only.
    float ToFloat() const {
        return static_cast<float>(value) / static_cast<float>(ONE);
    }
    
    int ToInt() const {
        return static_cast<int>(value >> FRACTIONAL_BITS);
    }
};

} // namespace Core
