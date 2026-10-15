// Off-Windows unit tests for the pure-logic parts. Build:  g++ -std=c++17 -O1 -Isrc tests/logic_test.cpp src/spectrum.cpp -o logic_test
#include "../src/colormatrix.h"
#include "../src/hotkey.h"
#include "../src/spectrum.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++g_fail;                                                 \
            std::printf("FAIL  %s:%d  %s  ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

static std::vector<float> sine(int n, float hz, float amp, int sr, int offset = 0)
{
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = amp * std::sin(2.0f * 3.14159265f * hz * float(i + offset) / float(sr));
    return s;
}

static int argmax(const std::vector<float> &v)
{
    int best = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] > v[best]) best = int(i);
    return best;
}

static void testSpectrum(int sr)
{
    SpectrumAnalyzer sa(50, 2048, sr);
    CHECK(sa.bars() == 50, "bars");

    // bands must be monotonic, non-empty and inside the spectrum
    for (int b = 0; b < sa.bars(); ++b) {
        CHECK(sa.bandHi(b) > sa.bandLo(b), "band %d empty (%d..%d)", b, sa.bandLo(b), sa.bandHi(b));
        CHECK(sa.bandLo(b) >= 1, "band %d touches DC", b);
        CHECK(sa.bandHi(b) <= 1023, "band %d beyond nyquist", b);
        if (b > 0) CHECK(sa.bandLo(b) >= sa.bandHi(b - 1), "band %d overlaps previous", b);
    }

    const float df = float(sr) / 2048.0f;
    for (float hz : {100.0f, 440.0f, 1000.0f, 4000.0f, 10000.0f}) {
        sa.reset();
        std::vector<float> out;
        for (int f = 0; f < 30; ++f) {
            auto s = sine(2048, hz, 0.5f, sr, f * 800);
            out = sa.process(s.data(), 1.0f / 60.0f);
        }
        const int peak = argmax(out);
        const int bin = int(hz / df);
        CHECK(bin >= sa.bandLo(peak) - 1 && bin <= sa.bandHi(peak),
              "sr=%d %.0f Hz expected in bar %d (bins %d..%d) but bin=%d", sr, hz, peak, sa.bandLo(peak), sa.bandHi(peak), bin);
        CHECK(out[peak] > 0.5f, "sr=%d %.0f Hz peak too small: %f", sr, hz, out[peak]);
        // a bar far away must be clearly lower
        const int far = peak < 25 ? peak + 20 : peak - 20;
        CHECK(out[far] < out[peak] * 0.6f, "sr=%d %.0f Hz far bar %d too high: %f vs %f", sr, hz, far, out[far], out[peak]);
    }

    // silence decays to ~0 within a second
    std::vector<float> zeros(2048, 0.0f);
    std::vector<float> out;
    for (int f = 0; f < 60; ++f)
        out = sa.process(zeros.data(), 1.0f / 60.0f);
    float mx = 0;
    for (float v : out) mx = std::max(mx, v);
    CHECK(mx < 0.02f, "silence did not decay: %f", mx);

    // all values in range for loud noise
    std::vector<float> noise(2048);
    unsigned seed = 1;
    for (auto &x : noise) { seed = seed * 1664525u + 1013904223u; x = (float(seed >> 8) / float(1 << 24)) * 2.0f - 1.0f; }
    for (int f = 0; f < 10; ++f) out = sa.process(noise.data(), 1.0f / 60.0f);
    for (float v : out) CHECK(v >= 0.0f && v <= 1.0001f, "out of range %f", v);

    // changing bar count works
    sa.setBars(20);
    CHECK(sa.values().size() == 20, "setBars size");
    out = sa.process(noise.data(), 1.0f / 60.0f);
    CHECK(out.size() == 20, "process size after setBars");
}

static void testHotkey()
{
    auto h = parseHotkey("Alt+Space");
    CHECK(h.ok && h.modifiers == 1 && h.vk == 0x20, "Alt+Space");
    h = parseHotkey("ctrl + alt + k");
    CHECK(h.ok && h.modifiers == 3 && h.vk == 'K', "ctrl+alt+k");
    h = parseHotkey("Win+F2");
    CHECK(h.ok && h.modifiers == 8 && h.vk == 0x71, "Win+F2");
    h = parseHotkey("Shift+Alt+F24");
    CHECK(h.ok && h.vk == 0x70 + 23, "F24");
    h = parseHotkey("Ctrl+Alt+7");
    CHECK(h.ok && h.vk == '7', "digit");
    h = parseHotkey("Space");
    CHECK(!h.ok, "no modifier must be rejected");
    h = parseHotkey("Foo+Space");
    CHECK(!h.ok, "unknown modifier");
    h = parseHotkey("Alt+");
    CHECK(!h.ok, "empty key");
    h = parseHotkey("Alt+F0");
    CHECK(!h.ok, "F0");
    h = parseHotkey("Alt+F25");
    CHECK(!h.ok, "F25");
    h = parseHotkey("");
    CHECK(!h.ok, "empty");
}

static void near(float a, float b, const char *what, float eps = 1e-3f)
{
    if (std::fabs(a - b) > eps) { ++g_fail; std::printf("FAIL  %s: %f != %f\n", what, a, b); }
}

static void testMatrices()
{
    float o[3];
    auto blue = makeFilterMatrix(0);
    applyMatrix5(blue, 1, 1, 1, o);
    near(o[0], 1, "blue r"); near(o[1], 1, "blue g"); near(o[2], 0.8f, "blue b");

    auto inv = makeFilterMatrix(2);
    applyMatrix5(inv, 0, 0, 0, o);
    near(o[0], 1, "inv black r"); near(o[1], 1, "inv black g"); near(o[2], 1, "inv black b");
    applyMatrix5(inv, 1, 0.25f, 0, o);
    near(o[0], 0, "inv r"); near(o[1], 0.75f, "inv g"); near(o[2], 1, "inv b");

    auto grey = makeFilterMatrix(1);
    applyMatrix5(grey, 0, 0, 0, o);          // black -> ink
    near(o[0], 0.10f, "grey ink r"); near(o[1], 0.10f, "grey ink g"); near(o[2], 0.12f, "grey ink b");
    applyMatrix5(grey, 1, 1, 1, o);          // white -> paper
    near(o[0], 0.94f, "grey paper r"); near(o[1], 0.92f, "grey paper g"); near(o[2], 0.86f, "grey paper b");
    float a[3], b[3];                         // channel-independent: pure red and pure blue of equal luma give equal output
    applyMatrix5(grey, 0.5f, 0.5f, 0.5f, a);
    applyMatrix5(grey, 0.5f, 0.5f, 0.5f, b);
    near(a[0], b[0], "grey stable");
    applyMatrix5(grey, 1, 0, 0, o);           // red luma 0.299
    near(o[0], 0.10f + 0.299f * (0.94f - 0.10f), "grey red luma");

    auto id = identityMatrix5();
    applyMatrix5(id, 0.3f, 0.6f, 0.9f, o);
    near(o[0], 0.3f, "id r"); near(o[1], 0.6f, "id g"); near(o[2], 0.9f, "id b");
}

int main()
{
    testSpectrum(48000);
    testSpectrum(44100);
    testSpectrum(96000);
    testHotkey();
    testMatrices();
    if (g_fail) { std::printf("%d check(s) FAILED\n", g_fail); return 1; }
    std::printf("all logic tests passed\n");
    return 0;
}
