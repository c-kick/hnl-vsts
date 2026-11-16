// Guards against what a host may hand a plugin: samples and parameter values
// that are not numbers, and arithmetic on numbers too small to matter.
//
// The plugins are built with -ffast-math, which lets the compiler assume
// that NaN and infinity never occur and remove tests such as std::isfinite
// or x != x. The tests here look at the bits instead, which it cannot remove.
// Header-only, no dependencies.
#pragma once

#include <cstdint>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace hnl {

/** False for NaN and for plus or minus infinity. */
inline bool isFinite(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline bool isFinite(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
}

/** A parameter value as the engine may use it: within its range, the default when it is not a number. */
inline float cleanParam(float value, float min, float max, float def) {
    if (!isFinite(value)) return def;
    return value < min ? min : (value > max ? max : value);
}

/** Replaces samples that are not numbers by silence. Returns how many there were. */
inline uint32_t cleanInput(float* samples, uint32_t count) {
    uint32_t bad = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!isFinite(samples[i])) {
            samples[i] = 0.0f;
            ++bad;
        }
    return bad;
}

/** True when every sample is a number. */
inline bool isClean(const float* samples, uint32_t count) {
    uint32_t all = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &samples[i], sizeof(bits));
        all |= (bits & 0x7f800000u) == 0x7f800000u ? 1u : 0u;
    }
    return all == 0;
}

/**
 * While one of these exists, numbers too small to matter count as zero
 * (FTZ and DAZ). Without it a filter that rings out into silence gets many
 * times slower on x86. Put one at the top of every process().
 */
struct ScopedNoDenormals {
#if defined(__SSE2__) || defined(_M_X64)
    unsigned saved = _mm_getcsr();
    ScopedNoDenormals() { _mm_setcsr(saved | 0x8040); }
    ~ScopedNoDenormals() { _mm_setcsr(saved); }
#else
    ScopedNoDenormals() {} // so that it is not an unused variable elsewhere
#endif
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
};

} // namespace hnl
