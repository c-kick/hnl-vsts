// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 hnldesign/c_kick/Klaas Leussink

// Writes FLAC, for the sounds that are built into HNL Sampler. Every block of
// 4096 samples takes the cheapest of: one value, the samples as they are, a
// fixed predictor of order 0 to 4, or a linear predictor of order 1 to 12
// worked out for the block; what the predictor leaves is Rice coded in up to
// 64 partitions. Channels are coded each on their own. The file carries the
// MD5 of its samples, so any FLAC decoder can check what it read.
// Header-only, no dependencies. Not part of the plugin: the plugin only reads.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hnl {
namespace flacenc {

struct BitWriter {
    std::vector<uint8_t> bytes;
    uint64_t acc = 0;
    int held = 0; // bits in acc not yet written

    void put(uint64_t value, int bits) {
        while (bits > 0) {
            const int take = std::min(bits, 32);
            const uint64_t part = (value >> (bits - take)) & ((uint64_t(1) << take) - 1);
            acc = (acc << take) | part;
            held += take;
            bits -= take;
            while (held >= 8) {
                bytes.push_back(static_cast<uint8_t>(acc >> (held - 8)));
                held -= 8;
            }
        }
    }
    void putSigned(int64_t value, int bits) { put(static_cast<uint64_t>(value) & ((bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1)), bits); }
    void unary(uint32_t zeros) {
        for (; zeros >= 32; zeros -= 32) put(0, 32);
        put(1, static_cast<int>(zeros) + 1);
    }
    void align() {
        if (held > 0) put(0, 8 - held);
    }
};

inline uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = static_cast<uint8_t>((c & 0x80) ? ((c << 1) ^ 0x07) : (c << 1));
    }
    return c;
}
inline uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c = static_cast<uint16_t>(c ^ (p[i] << 8));
        for (int k = 0; k < 8; ++k) c = static_cast<uint16_t>((c & 0x8000) ? ((c << 1) ^ 0x8005) : (c << 1));
    }
    return c;
}

/** MD5 (RFC 1321) of `data`. */
inline void md5(const std::vector<uint8_t>& data, uint8_t out[16]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::vector<uint8_t> m = data;
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 0; i < 8; ++i) m.push_back(static_cast<uint8_t>(bitLength >> (8 * i)));
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    for (size_t at = 0; at < m.size(); at += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; ++i) w[i] = m[at + 4 * i] | (m[at + 4 * i + 1] << 8) | (m[at + 4 * i + 2] << 16) | (static_cast<uint32_t>(m[at + 4 * i + 3]) << 24);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            const uint32_t x = a + f + K[i] + w[g];
            a = d;
            d = c;
            c = b;
            b = b + ((x << S[i]) | (x >> (32 - S[i])));
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    for (int i = 0; i < 16; ++i) out[i] = static_cast<uint8_t>(h[i / 4] >> (8 * (i % 4)));
}

/** How a block's residual is best split and coded, and what that costs in bits. */
struct RicePlan {
    int order = 0;           // 2^order partitions
    std::vector<int> params; // one per partition
    uint64_t bits = 0;
};

inline uint32_t zigzag(int64_t r) { return static_cast<uint32_t>((static_cast<uint64_t>(r) << 1) ^ static_cast<uint64_t>(r >> 63)); }

/** `res` holds the residual of samples [predOrder, n) of a block of n. */
inline RicePlan planRice(const std::vector<int64_t>& res, size_t n, int predOrder, int maxParam) {
    RicePlan best;
    best.bits = ~uint64_t(0);
    for (int po = 0; po <= 6; ++po) {
        const size_t parts = size_t(1) << po;
        if (n % parts != 0 || (n >> po) <= static_cast<size_t>(predOrder)) break;
        RicePlan plan;
        plan.order = po;
        plan.bits = 0;
        size_t at = 0;
        for (size_t p = 0; p < parts; ++p) {
            const size_t count = (n >> po) - (p == 0 ? static_cast<size_t>(predOrder) : 0);
            uint64_t sum = 0;
            for (size_t i = 0; i < count; ++i) sum += zigzag(res[at + i]);
            int guess = 0;
            while (guess < maxParam && (static_cast<uint64_t>(count) << (guess + 1)) <= sum) ++guess;
            uint64_t partBest = ~uint64_t(0);
            int partParam = 0;
            for (int k = std::max(0, guess - 1); k <= std::min(maxParam, guess + 2); ++k) {
                uint64_t bits = static_cast<uint64_t>(count) * static_cast<uint64_t>(k + 1);
                for (size_t i = 0; i < count; ++i) bits += zigzag(res[at + i]) >> k;
                if (bits < partBest) { partBest = bits; partParam = k; }
            }
            plan.params.push_back(partParam);
            plan.bits += partBest;
            at += count;
        }
        if (plan.bits < best.bits) best = plan;
    }
    return best;
}

struct Subframe {
    enum Kind { kConstant, kVerbatim, kFixed, kLpc } kind = kVerbatim;
    int order = 0, precision = 0, shift = 0;
    std::vector<int32_t> coefs;
    std::vector<int64_t> residual;
    RicePlan rice;
    uint64_t bits = 0;
};

/** The cheapest way to write block x[0, n) of `bits`-bit samples. */
inline Subframe planSubframe(const int32_t* x, size_t n, int bits) {
    Subframe best;
    best.kind = Subframe::kVerbatim;
    best.bits = static_cast<uint64_t>(n) * static_cast<uint64_t>(bits);
    bool same = true;
    for (size_t i = 1; i < n && same; ++i) same = x[i] == x[0];
    if (same) {
        best.kind = Subframe::kConstant;
        best.bits = static_cast<uint64_t>(bits);
        return best;
    }
    const int riceBits = bits > 16 ? 5 : 4, maxParam = bits > 16 ? 30 : 14;
    auto consider = [&](Subframe& s) {
        s.rice = planRice(s.residual, n, s.order, maxParam);
        if (s.rice.bits == ~uint64_t(0)) return;
        for (int64_t r : s.residual) // what the format and a 32-bit decoder can hold
            if (r > 0x3FFFFFFF || r < -0x3FFFFFFF) return;
        s.bits = static_cast<uint64_t>(s.order) * static_cast<uint64_t>(bits) + 6 + (static_cast<uint64_t>(riceBits) << s.rice.order) + s.rice.bits;
        if (s.kind == Subframe::kLpc) s.bits += 9 + static_cast<uint64_t>(s.order) * static_cast<uint64_t>(s.precision);
        if (s.bits < best.bits) best = s;
    };
    // Fixed predictors.
    static const int kFixed[5][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {2, -1, 0, 0}, {3, -3, 1, 0}, {4, -6, 4, -1}};
    for (int order = 0; order <= 4 && static_cast<size_t>(order) < n; ++order) {
        Subframe s;
        s.kind = Subframe::kFixed;
        s.order = order;
        s.residual.resize(n - static_cast<size_t>(order));
        for (size_t i = static_cast<size_t>(order); i < n; ++i) {
            int64_t pred = 0;
            for (int j = 0; j < order; ++j) pred += static_cast<int64_t>(kFixed[order][j]) * x[i - 1 - static_cast<size_t>(j)];
            s.residual[i - static_cast<size_t>(order)] = x[i] - pred;
        }
        consider(s);
    }
    // Linear prediction: the autocorrelation of the windowed block, then Levinson-Durbin for every order.
    const int maxOrder = 12;
    if (n > static_cast<size_t>(4 * maxOrder)) {
        std::vector<double> w(n);
        for (size_t i = 0; i < n; ++i) {
            const double t = (2.0 * static_cast<double>(i) + 1.0) / static_cast<double>(n) - 1.0;
            w[i] = x[i] * (1.0 - t * t); // Welch window
        }
        double r[maxOrder + 1];
        for (int lag = 0; lag <= maxOrder; ++lag) {
            double sum = 0;
            for (size_t i = static_cast<size_t>(lag); i < n; ++i) sum += w[i] * w[i - static_cast<size_t>(lag)];
            r[lag] = sum;
        }
        if (r[0] > 0) {
            double a[maxOrder + 1] = {0}, err = r[0];
            for (int m = 1; m <= maxOrder && err > 0; ++m) {
                double acc = r[m];
                for (int j = 1; j < m; ++j) acc -= a[j] * r[m - j];
                const double k = acc / err;
                double next[maxOrder + 1];
                for (int j = 1; j < m; ++j) next[j] = a[j] - k * a[m - j];
                next[m] = k;
                for (int j = 1; j <= m; ++j) a[j] = next[j];
                err *= 1.0 - k * k;
                // Whole-number coefficients of 12 bits, so that 16-bit sound never needs more than 32 bits to predict.
                Subframe s;
                s.kind = Subframe::kLpc;
                s.order = m;
                s.precision = bits <= 16 ? 12 : 14;
                double largest = 0;
                for (int j = 1; j <= m; ++j) largest = std::max(largest, std::fabs(a[j]));
                if (largest <= 0) continue;
                int exponent = 0;
                std::frexp(largest, &exponent); // largest = f * 2^exponent, 0.5 <= f < 1
                s.shift = std::clamp(s.precision - 1 - exponent, 0, 15);
                const int32_t top = (1 << (s.precision - 1)) - 1;
                double carry = 0;
                s.coefs.resize(static_cast<size_t>(m));
                for (int j = 1; j <= m; ++j) {
                    carry += a[j] * static_cast<double>(1 << s.shift);
                    const int32_t q = static_cast<int32_t>(std::clamp<double>(std::floor(carry + 0.5), -top - 1, top));
                    s.coefs[static_cast<size_t>(j - 1)] = q;
                    carry -= q;
                }
                s.residual.resize(n - static_cast<size_t>(m));
                for (size_t i = static_cast<size_t>(m); i < n; ++i) {
                    int64_t pred = 0;
                    for (int j = 0; j < m; ++j) pred += static_cast<int64_t>(s.coefs[static_cast<size_t>(j)]) * x[i - 1 - static_cast<size_t>(j)];
                    s.residual[i - static_cast<size_t>(m)] = x[i] - (pred >> s.shift);
                }
                consider(s);
            }
        }
    }
    return best;
}

inline void writeSubframe(BitWriter& w, const Subframe& s, const int32_t* x, size_t n, int bits) {
    w.put(0, 1);
    switch (s.kind) {
    case Subframe::kConstant: w.put(0, 6); break;
    case Subframe::kVerbatim: w.put(1, 6); break;
    case Subframe::kFixed: w.put(8u | static_cast<unsigned>(s.order), 6); break;
    case Subframe::kLpc: w.put(32u | static_cast<unsigned>(s.order - 1), 6); break;
    }
    w.put(0, 1); // no wasted bits
    if (s.kind == Subframe::kConstant) { w.putSigned(x[0], bits); return; }
    if (s.kind == Subframe::kVerbatim) {
        for (size_t i = 0; i < n; ++i) w.putSigned(x[i], bits);
        return;
    }
    for (int i = 0; i < s.order; ++i) w.putSigned(x[i], bits);
    if (s.kind == Subframe::kLpc) {
        w.put(static_cast<unsigned>(s.precision - 1), 4);
        w.putSigned(s.shift, 5);
        for (int32_t c : s.coefs) w.putSigned(c, s.precision);
    }
    const bool wide = bits > 16;
    w.put(wide ? 1 : 0, 2);
    w.put(static_cast<unsigned>(s.rice.order), 4);
    size_t at = 0;
    for (size_t p = 0; p < s.rice.params.size(); ++p) {
        const size_t count = (n >> s.rice.order) - (p == 0 ? static_cast<size_t>(s.order) : 0);
        const int k = s.rice.params[p];
        w.put(static_cast<unsigned>(k), wide ? 5 : 4);
        for (size_t i = 0; i < count; ++i) {
            const uint32_t u = zigzag(s.residual[at + i]);
            w.unary(u >> k);
            if (k > 0) w.put(u & ((1u << k) - 1), k);
        }
        at += count;
    }
}

/**
 * A FLAC file of `channels` (1 or 2) channels of `frames` whole samples each, `bits` (8, 16 or 24) wide.
 * Empty when it cannot be written as asked.
 */
inline std::vector<uint8_t> encode(const int32_t* const* samples, int channels, size_t frames, int bits, uint32_t sampleRate) {
    if (channels < 1 || channels > 2 || frames == 0 || (bits != 8 && bits != 16 && bits != 24) || sampleRate == 0 || sampleRate >= (1u << 20)) return {};
    const size_t kBlock = 4096;
    BitWriter head;
    head.put(0x664C6143, 32); // "fLaC"
    head.put(1, 1);           // the last metadata block
    head.put(0, 7);           // STREAMINFO
    head.put(34, 24);
    head.put(kBlock, 16);
    head.put(kBlock, 16);
    const size_t sizesAt = head.bytes.size();
    head.put(0, 24); // smallest and largest frame: filled in below
    head.put(0, 24);
    head.put(sampleRate, 20);
    head.put(static_cast<unsigned>(channels - 1), 3);
    head.put(static_cast<unsigned>(bits - 1), 5);
    head.put(static_cast<uint64_t>(frames), 36);
    std::vector<uint8_t> pcm;
    pcm.reserve(frames * static_cast<size_t>(channels) * static_cast<size_t>(bits / 8));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            for (int b = 0; b < bits / 8; ++b) pcm.push_back(static_cast<uint8_t>(static_cast<uint32_t>(samples[c][i]) >> (8 * b)));
    uint8_t sum[16];
    md5(pcm, sum);
    for (uint8_t b : sum) head.put(b, 8);
    std::vector<uint8_t> out = head.bytes;

    size_t smallest = ~size_t(0), largest = 0;
    uint64_t number = 0;
    for (size_t at = 0; at < frames; at += kBlock, ++number) {
        const size_t n = std::min(kBlock, frames - at);
        BitWriter w;
        w.put(0xFFF8, 16); // sync, fixed block size
        w.put(n == kBlock ? 12 : 7, 4);
        w.put(sampleRate == 44100 ? 9 : (sampleRate == 48000 ? 10 : 0), 4);
        w.put(static_cast<unsigned>(channels - 1), 4);
        w.put(bits == 8 ? 1 : (bits == 16 ? 4 : 6), 3);
        w.put(0, 1);
        // The frame number, written as UTF-8 does a character.
        if (number < 0x80) w.put(number, 8);
        else {
            int extra = 1;
            while (extra < 6 && (number >> (6 * extra + (6 - extra))) != 0) ++extra;
            w.put(((0xFFu << (7 - extra)) & 0xFF) | (number >> (6 * extra)), 8);
            for (int i = extra - 1; i >= 0; --i) w.put(0x80 | ((number >> (6 * i)) & 0x3F), 8);
        }
        if (n != kBlock) w.put(n - 1, 16);
        w.put(crc8(w.bytes.data(), w.bytes.size()), 8);
        for (int c = 0; c < channels; ++c) {
            const Subframe s = planSubframe(samples[c] + at, n, bits);
            writeSubframe(w, s, samples[c] + at, n, bits);
        }
        w.align();
        w.put(crc16(w.bytes.data(), w.bytes.size()), 16);
        smallest = std::min(smallest, w.bytes.size());
        largest = std::max(largest, w.bytes.size());
        out.insert(out.end(), w.bytes.begin(), w.bytes.end());
    }
    for (int i = 0; i < 3; ++i) {
        out[sizesAt + static_cast<size_t>(i)] = static_cast<uint8_t>(smallest >> (8 * (2 - i)));
        out[sizesAt + 3 + static_cast<size_t>(i)] = static_cast<uint8_t>(largest >> (8 * (2 - i)));
    }
    return out;
}

} // namespace flacenc
} // namespace hnl
