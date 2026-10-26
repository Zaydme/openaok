#pragma once
#include <cstdint>

// Deterministic pseudo-random number generator (xorshift64*): the same seed
// gives the same sequence on every CPU, compiler and platform. Use it, not
// rand(), mt19937 or float-based noise, for anything that affects simulation
// state.

namespace Core {

class Random {
public:
    explicit Random(uint64_t seed) : state(seed ? seed : 1) {}
    
    /**
     * Returns the next pseudo-random 64-bit value.
     */
    uint64_t Next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    
    /**
     * Returns a value in [0, max) — uniform distribution.
     */
    uint32_t NextInt(uint32_t max) {
        if (max == 0) return 0;
        return static_cast<uint32_t>(Next() % max);
    }
    
    /**
     * Returns a value in [min, max] inclusive.
     */
    int32_t NextRange(int32_t min, int32_t max) {
        if (min >= max) return min;
        uint32_t range = static_cast<uint32_t>(max - min + 1);
        return min + static_cast<int32_t>(NextInt(range));
    }
    
    /**
     * Deterministic hash-based noise at grid coordinates.
     * Returns a value in [-1024, +1024] (scaled integer).
     */
    static int32_t HashNoise2D(int32_t x, int32_t y, uint32_t seed) {
        uint32_t n = static_cast<uint32_t>(x) * 73856093u 
                   ^ static_cast<uint32_t>(y) * 19349663u 
                   ^ seed * 83492791u;
        n = (n << 13) ^ n;
        n = n * (n * n * 15731u + 789221u) + 1376312589u;
        // Map to [-1024, +1024]
        return static_cast<int32_t>((n & 0x7FF) - 1024);
    }
    
    /**
     * Smoothed noise: averages a cell with its 8 neighbors.
     * All integer math. Returns value in approx [-1024, +1024].
     */
    static int32_t SmoothedNoise2D(int32_t x, int32_t y, uint32_t seed) {
        int32_t corners = (HashNoise2D(x-1, y-1, seed) + HashNoise2D(x+1, y-1, seed) +
                           HashNoise2D(x-1, y+1, seed) + HashNoise2D(x+1, y+1, seed)) / 16;
        int32_t sides   = (HashNoise2D(x-1, y, seed) + HashNoise2D(x+1, y, seed) +
                           HashNoise2D(x, y-1, seed) + HashNoise2D(x, y+1, seed)) / 8;
        int32_t center  = HashNoise2D(x, y, seed) / 4;
        return corners + sides + center;
    }
    
    /**
     * Multi-octave integer noise for terrain generation, sampled at integer
     * coordinates with no interpolation.
     * 
     * Returns a value in approx [-1024, +1024].
     * 
     * @param octaves Number of noise layers (3-6 typical)
     * @param persistence How much each octave contributes (out of 100, e.g. 50 = 0.5)
     */
    static int32_t OctaveNoise2D(int32_t x, int32_t y, uint32_t seed, int octaves, int persistence) {
        int32_t total = 0;
        int32_t amplitude = 100;
        int32_t max_amplitude = 0;
        int32_t frequency = 1;
        
        for (int i = 0; i < octaves; ++i) {
            total += SmoothedNoise2D(x / frequency, y / frequency, seed + static_cast<uint32_t>(i) * 1000u) * amplitude / 100;
            max_amplitude += amplitude;
            amplitude = amplitude * persistence / 100;
            frequency *= 2;
        }
        
        // Normalize to [-1024, +1024]
        if (max_amplitude > 0) {
            total = total * 1024 / max_amplitude;
        }
        return total;
    }

private:
    uint64_t state;
};

} // namespace Core
