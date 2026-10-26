#pragma once
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

    // The 8-way facing of a world movement vector. The sprite angles are
    // evenly spaced on screen, and +x in world space runs down-right on screen
    // (south-east), so the vector is rotated 45 degrees before classifying.
    static Direction WorldVectorToFacing(Fixed32 dx, Fixed32 dy) {
        return VectorToDirection(dx - dy, dx + dy);
    }

    static Direction VectorToDirection(Fixed32 dx, Fixed32 dy) {
        Fixed32 adx = Abs(dx);
        Fixed32 ady = Abs(dy);
        
        bool right = dx.value > 0;
        bool down  = dy.value > 0;
        
        // Cardinal when one axis is more than tan(67.5 degrees) ~ 2.41 times
        // the other: adx * 100 > ady * 241.
        
        Fixed32 threshold_x = adx * Fixed32(100);
        Fixed32 threshold_y = ady * Fixed32(241);
        
        if (threshold_x > threshold_y) {
            // Primarily horizontal
            return right ? Direction::East : Direction::West;
        }
        
        Fixed32 threshold_y2 = ady * Fixed32(100);
        Fixed32 threshold_x2 = adx * Fixed32(241);
        
        if (threshold_y2 > threshold_x2) {
            // Primarily vertical
            return down ? Direction::South : Direction::North;
        }
        
        // Diagonal
        if (right && down)  return Direction::SouthEast;
        if (!right && down) return Direction::SouthWest;
        if (right && !down) return Direction::NorthEast;
        return Direction::NorthWest;
    }
};

} // namespace Core
