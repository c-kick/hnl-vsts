// Tests of common/dsp/Safety.hpp and the guards in common/dsp/Filters.hpp.
// Built twice by run.sh: plainly, and with the flags the plugins are built with.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../dsp/Filters.hpp"
#include "../dsp/Safety.hpp"

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Made from bits, so that the compiler cannot reason about them.
static float fromBits(uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
static double fromBits64(uint64_t bits) {
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

// Not inlined: the compiler does not know that the guard changes what a multiplication gives,
// and would otherwise feel free to move the multiplication out from under it.
__attribute__((noinline)) static double product(double a, double b) {
    volatile double x = a, y = b;
    return x * y;
}

int main() {
    const float nan = fromBits(0x7fc00000u), negNan = fromBits(0xffc00001u);
    const float inf = fromBits(0x7f800000u), negInf = fromBits(0xff800000u);
    const float denormal = fromBits(0x00000100u), largest = fromBits(0x7f7fffffu);

    check(!hnl::isFinite(nan) && !hnl::isFinite(negNan), "isFinite(float): NaN is not");
    check(!hnl::isFinite(inf) && !hnl::isFinite(negInf), "isFinite(float): infinity is not");
    check(hnl::isFinite(0.0f) && hnl::isFinite(-1.5f) && hnl::isFinite(denormal) && hnl::isFinite(largest), "isFinite(float): numbers are");
    check(!hnl::isFinite(fromBits64(0x7ff8000000000000ull)) && !hnl::isFinite(fromBits64(0xfff0000000000000ull)), "isFinite(double): NaN and infinity are not");
    check(hnl::isFinite(0.0) && hnl::isFinite(-1e300) && hnl::isFinite(fromBits64(1ull)), "isFinite(double): numbers are");

    check(hnl::cleanParam(nan, -1.0f, 2.0f, 0.5f) == 0.5f && hnl::cleanParam(inf, -1.0f, 2.0f, 0.5f) == 0.5f, "cleanParam: not a number gives the default");
    check(hnl::cleanParam(1e9f, -1.0f, 2.0f, 0.5f) == 2.0f && hnl::cleanParam(-1e9f, -1.0f, 2.0f, 0.5f) == -1.0f, "cleanParam: out of range gives the nearest end");
    check(hnl::cleanParam(1.25f, -1.0f, 2.0f, 0.5f) == 1.25f, "cleanParam: a value in range is kept");

    float block[8] = {0.25f, nan, -0.5f, inf, negInf, denormal, negNan, 1.0f};
    check(!hnl::isClean(block, 8) && hnl::isClean(block, 1), "isClean: finds what is not a number");
    const uint32_t bad = hnl::cleanInput(block, 8);
    check(bad == 4 && hnl::isClean(block, 8), "cleanInput: four bad samples replaced");
    check(block[0] == 0.25f && block[1] == 0.0f && block[2] == -0.5f && block[3] == 0.0f && block[4] == 0.0f && block[6] == 0.0f && block[7] == 1.0f,
          "cleanInput: by silence, the rest untouched");

#if defined(__SSE2__) || defined(_M_X64)
    {
        const unsigned before = _mm_getcsr();
        _mm_setcsr(before & ~0x8040u); // as a host that sets nothing
        const double outside = product(1e-300, 1e-10);
        double inside;
        unsigned during;
        {
            hnl::ScopedNoDenormals guard;
            during = _mm_getcsr();
            inside = product(1e-300, 1e-10);
        }
        check(outside != 0.0 && inside == 0.0, "ScopedNoDenormals: a result too small to matter is zero inside");
        check((during & 0x8040u) == 0x8040u && (_mm_getcsr() & 0x8040u) == 0u, "ScopedNoDenormals: the caller's setting is back afterwards");
        _mm_setcsr(before);
    }
#endif

    {
        hnl::Delay empty; // never given a length
        empty.push(1.0);
        check(empty.peek() == 0.0 && empty.process(0.5) == 0.5, "Delay without a length: safe, passes the signal");
    }

    std::printf("%s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
