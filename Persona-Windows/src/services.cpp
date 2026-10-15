#include "services.h"

#include "colormatrix.h"

#include <QDebug>
#include <QProcess>
#include <QSysInfo>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <windows.h>
#include <endpointvolume.h>
#include <magnification.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wtsapi32.h>

using Microsoft::WRL::ComPtr;

// ============================================================================
// AudioCtl
// ============================================================================

struct AudioCtl::Endpoint {
    ComPtr<IAudioEndpointVolume> volume;
};

AudioCtl::AudioCtl(QObject *parent)
    : QObject(parent), m_ep(std::make_unique<Endpoint>())
{
    m_timer.setInterval(80);
    connect(&m_timer, &QTimer::timeout, this, &AudioCtl::poll);
    m_timer.start();
    poll();
}

AudioCtl::~AudioCtl()
{
    m_timer.stop();
    m_ep->volume.Reset();
}

bool AudioCtl::refreshEndpoint()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))))
        return false;

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        m_ep->volume.Reset();
        m_deviceId.clear();
        return false;
    }

    LPWSTR id = nullptr;
    QString deviceId;
    if (SUCCEEDED(device->GetId(&id)) && id) {
        deviceId = QString::fromWCharArray(id);
        CoTaskMemFree(id);
    }
    if (m_ep->volume && deviceId == m_deviceId)
        return true;

    ComPtr<IAudioEndpointVolume> endpoint;
    if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void **>(endpoint.GetAddressOf()))))
        return false;

    m_ep->volume = endpoint;
    m_deviceId = deviceId;
    m_baselineSet = false; // switching device must not pop the OSD
    return true;
}

void AudioCtl::poll()
{
    // Re-resolve the default device once in a while so switching outputs works.
    if (!m_ep->volume || (++m_tick % 25) == 0)
        refreshEndpoint();

    bool available = false;
    float level = 0.0f;
    BOOL muted = FALSE;
    if (m_ep->volume) {
        if (SUCCEEDED(m_ep->volume->GetMasterVolumeLevelScalar(&level))) {
            m_ep->volume->GetMute(&muted);
            available = true;
        } else {
            m_ep->volume.Reset(); // device went away
        }
    }

    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
    if (!available)
        return;

    const bool volumeDiffers = std::abs(double(level) - m_volume) > 0.0005;
    const bool muteDiffers = (muted != FALSE) != m_muted;
    m_volume = level;
    m_muted = (muted != FALSE);

    if (volumeDiffers)
        emit volumeChanged();
    if (muteDiffers)
        emit mutedChanged();
    if (!m_baselineSet) {
        m_baselineSet = true;
        return;
    }
    if (volumeDiffers || muteDiffers)
        emit userAdjusted();
}

void AudioCtl::setVolume(double v)
{
    if (!m_ep->volume)
        return;
    v = std::min(1.0, std::max(0.0, v));
    m_ep->volume->SetMasterVolumeLevelScalar(float(v), nullptr);
}

void AudioCtl::setMuted(bool m)
{
    if (!m_ep->volume)
        return;
    m_ep->volume->SetMute(m ? TRUE : FALSE, nullptr);
}

// ============================================================================
// BatteryInfo
// ============================================================================

BatteryInfo::BatteryInfo(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &BatteryInfo::poll);
    m_timer.start();
    poll();
}

void BatteryInfo::poll()
{
    SYSTEM_POWER_STATUS s;
    if (!GetSystemPowerStatus(&s))
        return;

    const bool noBattery = (s.BatteryFlag & 128) != 0 || s.BatteryFlag == 255;
    const bool available = !noBattery && s.BatteryLifePercent != 255;
    const double pct = available ? double(s.BatteryLifePercent) / 100.0 : 0.0;
    const bool charging = available && (s.BatteryFlag & 8) != 0;
    const bool plugged = s.ACLineStatus == 1;
    const bool full = available && plugged && !charging && s.BatteryLifePercent >= 95;
    const int seconds = (s.BatteryLifeTime == DWORD(-1)) ? -1 : int(s.BatteryLifeTime);

    if (available == m_available && pct == m_percentage && charging == m_charging
        && plugged == m_plugged && full == m_full && seconds == m_secondsLeft)
        return;

    m_available = available;
    m_percentage = pct;
    m_charging = charging;
    m_plugged = plugged;
    m_full = full;
    m_secondsLeft = seconds;
    emit changed();
}

QString BatteryInfo::percentageString() const
{
    return QString::number(int(m_percentage * 100.0 + 0.5)) + QLatin1Char('%');
}

QString BatteryInfo::timeRemainingString() const
{
    if (m_charging || m_secondsLeft <= 0)
        return QString();
    const int h = m_secondsLeft / 3600;
    const int m = (m_secondsLeft % 3600) / 60;
    return QStringLiteral("%1h %2m remaining").arg(h).arg(m);
}

// ============================================================================
// SysInfo
// ============================================================================

namespace {
quint64 fileTimeToU64(const FILETIME &ft)
{
    return (quint64(ft.dwHighDateTime) << 32) | quint64(ft.dwLowDateTime);
}
} // namespace

SysInfo::SysInfo(QObject *parent)
    : QObject(parent)
{
    m_osName = QSysInfo::prettyProductName();
    m_fast.setInterval(2000);
    m_slow.setInterval(30000);
    connect(&m_fast, &QTimer::timeout, this, &SysInfo::sampleFast);
    connect(&m_slow, &QTimer::timeout, this, &SysInfo::sampleSlow);
}

void SysInfo::setActive(bool a)
{
    if (a == m_active)
        return;
    m_active = a;
    if (a) {
        sampleFast();
        sampleSlow();
        m_fast.start();
        m_slow.start();
    } else {
        m_fast.stop();
        m_slow.stop();
    }
    emit activeChanged();
}

void SysInfo::sampleFast()
{
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const quint64 i = fileTimeToU64(idle);
        const quint64 total = fileTimeToU64(kernel) + fileTimeToU64(user); // kernel includes idle
        if (m_lastTotal > 0 && total > m_lastTotal) {
            const double dt = double(total - m_lastTotal);
            const double di = double(i - m_lastIdle);
            m_cpu = std::min(1.0, std::max(0.0, 1.0 - di / dt));
        }
        m_lastIdle = i;
        m_lastTotal = total;
    }

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        m_memTotal = double(ms.ullTotalPhys);
        m_memUsed = double(ms.ullTotalPhys - ms.ullAvailPhys);
    }
    emit changed();
}

void SysInfo::sampleSlow()
{
    const QString drive = qEnvironmentVariable("SystemDrive", QStringLiteral("C:")) + QStringLiteral("\\");
    ULARGE_INTEGER freeToCaller, total, totalFree;
    if (GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(drive.utf16()), &freeToCaller, &total, &totalFree)) {
        m_diskTotal = double(total.QuadPart);
        m_diskUsed = double(total.QuadPart - totalFree.QuadPart);
    }

    // "who | wc -l": sessions that belong to a user
    int users = 0;
    PWTS_SESSION_INFOW sessions = nullptr;
    DWORD count = 0;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        for (DWORD i = 0; i < count; ++i) {
            const WTS_CONNECTSTATE_CLASS st = sessions[i].State;
            if (st != WTSActive && st != WTSDisconnected && st != WTSConnected)
                continue;
            LPWSTR name = nullptr;
            DWORD bytes = 0;
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessions[i].SessionId,
                                            WTSUserName, &name, &bytes)
                && name && *name)
                ++users;
            if (name)
                WTSFreeMemory(name);
        }
        WTSFreeMemory(sessions);
    }
    m_users = QString::number(std::max(1, users));
    emit changed();
}

QString SysInfo::memText() const
{
    return QString::number(m_memUsed / 1073741824.0, 'f', 1) + QStringLiteral(" / ")
           + QString::number(m_memTotal / 1073741824.0, 'f', 1) + QStringLiteral(" GB");
}

QString SysInfo::diskText() const
{
    return QString::number(m_diskUsed / 1073741824.0, 'f', 1) + QStringLiteral(" / ")
           + QString::number(m_diskTotal / 1073741824.0, 'f', 1) + QStringLiteral(" GB");
}

// ============================================================================
// PowerCtl
// ============================================================================

void PowerCtl::shutdown()
{
    QProcess::startDetached(QStringLiteral("shutdown.exe"), {QStringLiteral("/s"), QStringLiteral("/t"), QStringLiteral("0")});
}

void PowerCtl::restart()
{
    QProcess::startDetached(QStringLiteral("shutdown.exe"), {QStringLiteral("/r"), QStringLiteral("/t"), QStringLiteral("0")});
}

void PowerCtl::logout()
{
    QProcess::startDetached(QStringLiteral("shutdown.exe"), {QStringLiteral("/l")});
}

void PowerCtl::lock()
{
    LockWorkStation();
}

void PowerCtl::sleep()
{
    QProcess::startDetached(QStringLiteral("rundll32.exe"),
                            {QStringLiteral("powrprof.dll,SetSuspendState"), QStringLiteral("0,1,0")});
}

// ============================================================================
// ScreenFilter (Magnification API)
// ============================================================================

ScreenFilter::ScreenFilter(QObject *parent)
    : QObject(parent)
{
}

ScreenFilter::~ScreenFilter()
{
    if (m_initialized) {
        const Matrix5 id = identityMatrix5();
        MAGCOLOREFFECT effect;
        std::memcpy(effect.transform, id.data(), sizeof(effect.transform));
        MagSetFullscreenColorEffect(&effect);
        MagUninitialize();
        m_initialized = false;
    }
}

bool ScreenFilter::apply(int kind)
{
    if (!m_initialized) {
        if (!MagInitialize()) {
            qWarning() << "ScreenFilter: MagInitialize failed, error" << GetLastError();
            return false;
        }
        m_initialized = true;
    }
    static_assert(sizeof(MAGCOLOREFFECT) == sizeof(float) * 25, "unexpected MAGCOLOREFFECT layout");
    const Matrix5 m = makeFilterMatrix(kind);
    MAGCOLOREFFECT effect;
    std::memcpy(effect.transform, m.data(), sizeof(effect.transform));
    if (!MagSetFullscreenColorEffect(&effect)) {
        qWarning() << "ScreenFilter: MagSetFullscreenColorEffect failed, error" << GetLastError();
        return false;
    }
    return true;
}

void ScreenFilter::toggle(int kind)
{
    if (m_active == kind) {
        clear();
        return;
    }
    if (apply(kind)) {
        m_active = kind;
        emit activeChanged();
    }
}

void ScreenFilter::clear()
{
    if (m_initialized) {
        const Matrix5 id = identityMatrix5();
        MAGCOLOREFFECT effect;
        std::memcpy(effect.transform, id.data(), sizeof(effect.transform));
        MagSetFullscreenColorEffect(&effect);
    }
    if (m_active != -1) {
        m_active = -1;
        emit activeChanged();
    }
}
