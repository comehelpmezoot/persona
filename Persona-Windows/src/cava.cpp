#include "cava.h"

#include <QDebug>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr size_t kRingSize = 16384;
constexpr int kFftSize = 2048;

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, spelled out so we don't need ksmedia.h / initguid.
const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

std::wstring deviceIdOf(IMMDevice *device)
{
    std::wstring result;
    LPWSTR id = nullptr;
    if (SUCCEEDED(device->GetId(&id)) && id) {
        result = id;
        CoTaskMemFree(id);
    }
    return result;
}

// One shared capture thread feeds every CavaMonitor (there is one per screen).
class LoopbackCapture
{
public:
    static LoopbackCapture &instance()
    {
        static LoopbackCapture capture;
        return capture;
    }

    void acquire()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_users++ == 0) {
            m_stop = false;
            m_thread = std::thread([this] { run(); });
        }
    }

    void release()
    {
        std::thread toJoin;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_users == 0)
                return;
            if (--m_users == 0) {
                m_stop = true;
                toJoin = std::move(m_thread);
            }
        }
        if (toJoin.joinable())
            toJoin.join();
    }

    int sampleRate() const { return m_sampleRate.load(); }

    // Copies the newest `count` mono samples.
    void latest(float *out, int count)
    {
        std::lock_guard<std::mutex> lock(m_ringMutex);
        const size_t n = size_t(count);
        for (size_t i = 0; i < n; ++i)
            out[i] = m_ring[(m_write + kRingSize - n + i) % kRingSize];
    }

private:
    LoopbackCapture() : m_ring(kRingSize, 0.0f) {}
    ~LoopbackCapture()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
    }

    void push(const float *samples, size_t n)
    {
        std::lock_guard<std::mutex> lock(m_ringMutex);
        for (size_t i = 0; i < n; ++i) {
            m_ring[m_write] = samples[i];
            m_write = (m_write + 1) % kRingSize;
        }
    }

    void pushSilence(size_t n)
    {
        n = std::min(n, kRingSize);
        std::lock_guard<std::mutex> lock(m_ringMutex);
        for (size_t i = 0; i < n; ++i) {
            m_ring[m_write] = 0.0f;
            m_write = (m_write + 1) % kRingSize;
        }
    }

    void run()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
            return;
        while (!m_stop) {
            session();
            for (int i = 0; i < 25 && !m_stop; ++i) // retry in ~500 ms (device change, no device...)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CoUninitialize();
    }

    // One capture session on the current default render device. Returns when the
    // device changes, becomes invalid, or we are asked to stop.
    void session()
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&enumerator))))
            return;
        ComPtr<IMMDevice> device;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
            return;
        const std::wstring currentId = deviceIdOf(device.Get());

        ComPtr<IAudioClient> client;
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void **>(client.GetAddressOf()))))
            return;

        WAVEFORMATEX *format = nullptr;
        if (FAILED(client->GetMixFormat(&format)) || !format)
            return;
        struct FormatGuard {
            WAVEFORMATEX *f;
            ~FormatGuard() { CoTaskMemFree(f); }
        } formatGuard{format};

        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0,
                                      format, nullptr)))
            return;

        ComPtr<IAudioCaptureClient> capture;
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                      reinterpret_cast<void **>(capture.GetAddressOf()))))
            return;

        bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const auto *ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
            isFloat = IsEqualGUID(ext->SubFormat, kSubtypeFloat) != FALSE;
        }
        const int channels = std::max<int>(1, format->nChannels);
        const int bytesPerSample = std::max<int>(1, format->wBitsPerSample / 8);
        const int sampleRate = int(format->nSamplesPerSec);
        m_sampleRate = sampleRate;

        if (FAILED(client->Start()))
            return;

        using Clock = std::chrono::steady_clock;
        auto lastData = Clock::now();
        auto lastDeviceCheck = Clock::now();
        std::vector<float> mono;

        while (!m_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(6));

            UINT32 packetFrames = 0;
            HRESULT packetHr = capture->GetNextPacketSize(&packetFrames);
            while (SUCCEEDED(packetHr) && packetFrames > 0) {
                BYTE *data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                    break;

                mono.assign(frames, 0.0f);
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                    for (UINT32 f = 0; f < frames; ++f) {
                        const BYTE *frame = data + size_t(f) * format->nBlockAlign;
                        float acc = 0.0f;
                        for (int c = 0; c < channels; ++c) {
                            const BYTE *s = frame + size_t(c) * bytesPerSample;
                            float v = 0.0f;
                            if (isFloat && bytesPerSample == 4) {
                                v = *reinterpret_cast<const float *>(s);
                            } else if (bytesPerSample == 2) {
                                v = float(*reinterpret_cast<const int16_t *>(s)) / 32768.0f;
                            } else if (bytesPerSample == 3) {
                                int32_t i = int32_t(s[0]) | (int32_t(s[1]) << 8) | (int32_t(s[2]) << 16);
                                if (i & 0x800000)
                                    i |= int32_t(0xFF000000);
                                v = float(i) / 8388608.0f;
                            } else if (bytesPerSample == 4) {
                                v = float(*reinterpret_cast<const int32_t *>(s)) / 2147483648.0f;
                            }
                            acc += v;
                        }
                        mono[f] = acc / float(channels);
                    }
                }
                push(mono.data(), mono.size());
                capture->ReleaseBuffer(frames);
                lastData = Clock::now();
                packetHr = capture->GetNextPacketSize(&packetFrames);
            }
            if (FAILED(packetHr))
                break; // device invalidated

            // Nothing is playing: loopback delivers no packets at all. Feed silence so the bars fall.
            const auto now = Clock::now();
            const auto quiet = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastData).count();
            if (quiet >= 30) {
                pushSilence(size_t(quiet) * size_t(sampleRate) / 1000);
                lastData = now;
            }

            if (now - lastDeviceCheck > std::chrono::seconds(1)) {
                lastDeviceCheck = now;
                ComPtr<IMMDevice> current;
                if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &current))
                    || deviceIdOf(current.Get()) != currentId)
                    break; // default output changed: restart on the new device
            }
        }
        client->Stop();
    }

    std::mutex m_mutex;     // users / thread lifetime
    std::mutex m_ringMutex; // sample ring
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_sampleRate{48000};
    int m_users = 0;
    std::vector<float> m_ring;
    size_t m_write = 0;
};

} // namespace

// ============================================================================

CavaMonitor::CavaMonitor(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CavaMonitor::tick);
    m_samples.assign(kFftSize, 0.0f);
    resetValues();
}

CavaMonitor::~CavaMonitor()
{
    if (m_holdsCapture) {
        m_timer.stop();
        LoopbackCapture::instance().release();
        m_holdsCapture = false;
    }
}

void CavaMonitor::resetValues()
{
    m_values.clear();
    m_values.reserve(m_bars);
    for (int i = 0; i < m_bars; ++i)
        m_values.append(0.0);
    if (m_analyzer)
        m_analyzer->reset();
    m_wasIdle = true;
}

void CavaMonitor::setBars(int bars)
{
    bars = std::min(256, std::max(2, bars));
    if (bars == m_bars)
        return;
    m_bars = bars;
    if (m_analyzer)
        m_analyzer->setBars(bars);
    resetValues();
    emit barsChanged();
    emit valuesChanged();
}

void CavaMonitor::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        LoopbackCapture::instance().acquire();
        m_holdsCapture = true;
        m_clock.restart();
        m_timer.start();
    } else {
        m_timer.stop();
        if (m_holdsCapture) {
            LoopbackCapture::instance().release();
            m_holdsCapture = false;
        }
        resetValues();
        emit valuesChanged();
    }
    emit activeChanged();
}

void CavaMonitor::tick()
{
    LoopbackCapture &capture = LoopbackCapture::instance();
    if (!m_analyzer)
        m_analyzer = std::make_unique<SpectrumAnalyzer>(m_bars, kFftSize, capture.sampleRate());
    m_analyzer->setSampleRate(capture.sampleRate());

    capture.latest(m_samples.data(), kFftSize);

    const float dt = float(m_clock.restart()) / 1000.0f;
    const std::vector<float> &out = m_analyzer->process(m_samples.data(), dt);

    float peak = 0.0f;
    for (float v : out)
        peak = std::max(peak, v);
    const bool idle = peak < 0.002f;
    if (idle && m_wasIdle)
        return; // nothing to redraw
    m_wasIdle = idle;

    QVariantList list;
    list.reserve(int(out.size()));
    for (float v : out)
        list.append(double(v));
    m_values = list;
    emit valuesChanged();
}
