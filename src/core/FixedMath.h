#pragma once
#include <cstdint>
#include "core/FixedPoint.h"
#include "core/Types.h"

/**
 * Fixed-point math utilities.
 * All functions are deterministic and use only integer arithmetic.
 * These are safe for use in the simulation loop.
 */

namespace Core {

class FixedMath {
public:
    // === Absolute value ===
    static constexpr Fixed32 Abs(Fixed32 a) {
        return (a.value < 0) ? Fixed32(-a.value, true) : a;
    }

    // === Min / Max ===
    static constexpr Fixed32 Min(Fixed32 a, Fixed32 b) {
        return (a.value < b.value) ? a : b;
    }
    
    static constexpr Fixed32 Max(Fixed32 a, Fixed32 b) {
        return (a.value > b.value) ? a : b;
    }

    // === Clamp ===
    static constexpr Fixed32 Clamp(Fixed32 val, Fixed32 lo, Fixed32 hi) {
        return Min(Max(val, lo), hi);
    }

    // Integer square root, bit by bit, entirely on int64_t.
    static Fixed32 Sqrt(Fixed32 a) {
        if (a.value <= 0) return Fixed32(0);
        
        // In 32.32 fixed point, sqrt(a * 2^32) = sqrt(a) * 2^16: the integer
        // sqrt of the raw value, shifted left by 16.
        int64_t val = a.value;
        
        // Start from the highest power of four not above the value.
        int64_t bit = 1LL << 62;
        while (bit > val) bit >>= 2;
        
        int64_t result = 0;
        while (bit != 0) {
            int64_t trial = result + bit;
            if (val >= trial) {
                val -= trial;
                result = (result >> 1) + bit;
            } else {
                result >>= 1;
            }
            bit >>= 2;
        }
        
        // result = sqrt(a_real) * 2^16; shift by 16 more for 32.32.
        return Fixed32(result << 16, true);
    }

    // Manhattan distance (L1 norm).
    static Fixed32 ManhattanDistance(Fixed32 dx, Fixed32 dy) {
        return Abs(dx) + Abs(dy);
    }

    // Chebyshev distance (L-infinity norm).
    static Fixed32 ChebyshevDistance(Fixed32 dx, Fixed32 dy) {
        return Max(Abs(dx), Abs(dy));
    }

    // Euclidean distance using fixed-point sqrt.
    static Fixed32 Distance(Fixed32 dx, Fixed32 dy) {
        return Sqrt(dx * dx + dy * dy);
    }

    // (dx, dy) normalised and scaled to `magnitude`; (0, 0) for a zero-length
    // vector.
    struct Vec2 { Fixed32 x; Fixed32 y; };
    
    static Vec2 NormalizeAndScale(Fixed32 dx, Fixed32 dy, Fixed32 magnitude) {
        Fixed32 dist = Distance(dx, dy);
        if (dist.value == 0) return {Fixed32(0), Fixed32(0)};
        
        Fixed32 nx = (dx * magnitude) / dist;
        Fixed32 ny = (dy * magnitude) / dist;
        return {nx, ny};
    }

    // The way a world vector points, in 256ths of a turn: 0 along the world's
    // (1, 1), which a unit facing South walks, going round the way the facings
    // do (South, South-West along the world's (0, 1), West, ...). Each value
    // covers the arc from itself to the next, so any number of facings that
    // divides 256 has whole values for its boundaries.
    static uint8_t WorldVectorToHeading(Fixed32 dx, Fixed32 dy) {
        // Turned an eighth, so 0 lies along u.
        __int128_t u = static_cast<__int128_t>(dx.value) + dy.value;
        __int128_t v = static_cast<__int128_t>(dy.value) - dx.value;
        if (u == 0 && v == 0) return 0;
        // Turned back a quarter at a time into [0, 64): turning, unlike
        // mirroring, keeps each arc's lower end its own.
        int base = 0;
        while (!(u > 0 && v >= 0)) {
            const __int128_t t = u;
            u = v;
            v = -t;
            base += 64;
        }
        // Past the eighth, back by an eighth more.
        if (v > u) {
            const __int128_t t = u;
            u = t + v;
            v = v - t;
            base += 32;
        }
        // The 256ths whose tangent the angle has reached.
        int steps = 0;
        while (steps < 32 && (v << 32) >= u * kTan[steps]) ++steps;
        return static_cast<uint8_t>((base + steps) & 0xFF);
    }

    // The nearest of `angles` facings evenly round a turn, the first at
    // heading 0: of eight, a Direction.
    static int Facet(uint8_t heading, int angles) {
        if (angles <= 1) return 0;
        return ((static_cast<int>(heading) * angles + 128) / 256) % angles;
    }

    static uint8_t HeadingOf(Direction facing) {
        return static_cast<uint8_t>(static_cast<int>(facing) * 32);
    }

    static Direction FacingOf(uint8_t heading) {
        return static_cast<Direction>(Facet(heading, 8));
    }

    // The 8-way facing of a world movement vector.
    static Direction WorldVectorToFacing(Fixed32 dx, Fixed32 dy) {
        return FacingOf(WorldVectorToHeading(dx, dy));
    }

private:
    // tan(k * pi / 128) for k = 1..32, in 32.32.
    static constexpr int64_t kTan[32] = {
        105435529, 210998213, 316815821, 423017356,
        529733691, 637098215, 745247522, 854322113,
        964467159, 1075833302, 1188577525, 1302864085,
        1418865542, 1536763871, 1656751707, 1779033704,
        1903828065, 2031368245, 2161904859, 2295707848,
        2433068920, 2574304328, 2719758045, 2869805398,
        3024857252, 3185364841, 3351825376, 3524788567,
        3704864268, 3892731450, 4089148798, 4294967296,
    };
};

} // namespace Core
