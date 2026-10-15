#pragma once
// Pure C++ (no Qt, no Windows) so it can be unit-tested anywhere.
// Turns a block of mono PCM samples into N log-spaced, smoothed bar heights
// in 0..1 - the same job the Linux "cava" plugin does for the wallpaper.

#include <complex>
#include <vector>

class SpectrumAnalyzer {
public:
    explicit SpectrumAnalyzer(int bars = 50, int fftSize = 2048, int sampleRate = 48000);

    void setBars(int bars);
    void setSampleRate(int sampleRate);
    void reset();

    int fftSize() const { return m_n; }
    int bars() const { return m_bars; }
    int sampleRate() const { return m_sr; }

    // `samples` must point at fftSize() mono samples (-1..1). `dt` is the time
    // since the previous call in seconds (used for the fall-off).
    const std::vector<float> &process(const float *samples, float dt);
    const std::vector<float> &values() const { return m_out; }

    // Exposed for tests: which FFT bin range a bar covers.
    int bandLo(int bar) const { return m_lo[bar]; }
    int bandHi(int bar) const { return m_hi[bar]; }

private:
    void rebuildBands();
    static void fft(std::vector<std::complex<float>> &a);

    int m_n;
    int m_bars;
    int m_sr;
    float m_sens = 1.0f;

    std::vector<float> m_window;
    std::vector<std::complex<float>> m_buf;
    std::vector<int> m_lo, m_hi;
    std::vector<float> m_tilt;
    std::vector<float> m_state; // temporal state (before spatial smoothing)
    std::vector<float> m_out;   // what the UI draws
};
