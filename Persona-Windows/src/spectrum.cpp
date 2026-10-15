#include "spectrum.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kLowHz = 40.0f;
constexpr float kHighHz = 14000.0f;
} // namespace

SpectrumAnalyzer::SpectrumAnalyzer(int bars, int fftSize, int sampleRate)
    : m_n(fftSize), m_bars(std::max(2, bars)), m_sr(sampleRate)
{
    // fftSize must be a power of two
    int n = 1;
    while (n < m_n)
        n <<= 1;
    m_n = n;

    m_window.resize(m_n);
    for (int i = 0; i < m_n; ++i)
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * float(i) / float(m_n - 1)));
    m_buf.resize(m_n);
    rebuildBands();
}

void SpectrumAnalyzer::setBars(int bars)
{
    bars = std::max(2, bars);
    if (bars == m_bars)
        return;
    m_bars = bars;
    rebuildBands();
}

void SpectrumAnalyzer::setSampleRate(int sampleRate)
{
    if (sampleRate <= 0 || sampleRate == m_sr)
        return;
    m_sr = sampleRate;
    rebuildBands();
}

void SpectrumAnalyzer::reset()
{
    std::fill(m_state.begin(), m_state.end(), 0.0f);
    std::fill(m_out.begin(), m_out.end(), 0.0f);
    m_sens = 1.0f;
}

void SpectrumAnalyzer::rebuildBands()
{
    m_lo.assign(m_bars, 0);
    m_hi.assign(m_bars, 1);
    m_tilt.assign(m_bars, 0.0f);
    m_state.assign(m_bars, 0.0f);
    m_out.assign(m_bars, 0.0f);

    const float nyquist = float(m_sr) * 0.5f;
    const float fHigh = std::min(kHighHz, nyquist * 0.92f);
    const float df = float(m_sr) / float(m_n);
    const int maxBin = m_n / 2 - 1;
    const float ratio = std::pow(fHigh / kLowHz, 1.0f / float(m_bars));

    int prevHi = 1; // never use the DC bin
    for (int b = 0; b < m_bars; ++b) {
        const float f0 = kLowHz * std::pow(ratio, float(b));
        const float f1 = kLowHz * std::pow(ratio, float(b + 1));
        int lo = std::max(prevHi, int(std::floor(f0 / df)));
        int hi = std::max(lo + 1, int(std::ceil(f1 / df)));
        lo = std::min(lo, maxBin - 1);
        hi = std::min(std::max(hi, lo + 1), maxBin);
        m_lo[b] = lo;
        m_hi[b] = hi; // exclusive upper bound... clamp keeps it >= lo+1
        prevHi = hi;

        const float fc = std::sqrt(f0 * f1);
        // Music rolls off towards the treble; lift it ~4 dB/octave above 100 Hz.
        m_tilt[b] = 4.0f * std::max(0.0f, std::log2(fc / 100.0f));
    }
}

void SpectrumAnalyzer::fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    // bit reversal
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * kPi / float(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k];
                const std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

const std::vector<float> &SpectrumAnalyzer::process(const float *samples, float dt)
{
    dt = std::min(0.1f, std::max(0.001f, dt));

    for (int i = 0; i < m_n; ++i)
        m_buf[i] = std::complex<float>(samples[i] * m_window[i], 0.0f);
    fft(m_buf);

    const float scale = 4.0f / float(m_n); // Hann window: full-scale sine -> ~1.0 (approximately amplitude)
    const float fall = std::exp(-dt * 8.0f);

    std::vector<float> raw(m_bars, 0.0f);
    float frameMax = 0.0f;
    for (int b = 0; b < m_bars; ++b) {
        float peak = 0.0f;
        for (int k = m_lo[b]; k < m_hi[b]; ++k)
            peak = std::max(peak, std::abs(m_buf[k]) * scale);
        const float db = 20.0f * std::log10(peak + 1e-7f);
        float v = (db + 70.0f + m_tilt[b]) / 55.0f;
        v = std::min(1.5f, std::max(0.0f, v));
        raw[b] = v;
        frameMax = std::max(frameMax, v);
    }

    // simple auto-sensitivity, like cava's "autosens"
    const float peakNow = frameMax * m_sens;
    if (peakNow > 1.05f)
        m_sens *= 0.96f;
    else if (peakNow < 0.6f && frameMax > 0.02f)
        m_sens = std::min(2.0f, m_sens * 1.004f);

    for (int b = 0; b < m_bars; ++b) {
        const float v = std::min(1.0f, raw[b] * m_sens);
        m_state[b] = std::max(v, m_state[b] * fall);
    }

    // light spatial smoothing so the "mountain" outline looks organic
    for (int b = 0; b < m_bars; ++b) {
        const float l = m_state[b > 0 ? b - 1 : b];
        const float r = m_state[b < m_bars - 1 ? b + 1 : b];
        m_out[b] = 0.2f * l + 0.6f * m_state[b] + 0.2f * r;
    }
    return m_out;
}
