#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <memory>
#include <thread>

// ---------------------------------------------------------------------------
// Default playback device volume (replaces Quickshell.Services.Pipewire)
// ---------------------------------------------------------------------------
class AudioCtl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
    explicit AudioCtl(QObject *parent = nullptr);
    ~AudioCtl() override;
    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool available() const { return m_available; }
    void setVolume(double v);
    void setMuted(bool m);
signals:
    void volumeChanged();
    void mutedChanged();
    void availableChanged();
    // Emitted when the volume/mute state changed after the first reading
    // (i.e. not at start-up and not when the default device is switched).
    void userAdjusted();
private:
    void poll();
    bool refreshEndpoint();

    struct Endpoint; // holds the COM pointer (defined in services.cpp)
    QTimer m_timer;
    std::unique_ptr<Endpoint> m_ep;
    QString m_deviceId;
    double m_volume = 0.0;
    bool m_muted = false;
    bool m_available = false;
    bool m_baselineSet = false;
    int m_tick = 0;
};

// ---------------------------------------------------------------------------
// Laptop panel brightness through WMI (replaces /sys/class/backlight polling)
// ---------------------------------------------------------------------------
class BrightnessCtl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
    explicit BrightnessCtl(QObject *parent = nullptr);
    ~BrightnessCtl() override;
    int brightness() const { return m_brightness; }
    bool available() const { return m_available; }
signals:
    void brightnessChanged();
    void availableChanged();
    void userAdjusted();
private:
    void run();
    void onSample(int value);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    int m_brightness = 0;
    bool m_available = false;
    bool m_baselineSet = false;
};

// ---------------------------------------------------------------------------
// Battery (replaces UPower)
// ---------------------------------------------------------------------------
class BatteryInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(double percentage READ percentage NOTIFY changed)
    Q_PROPERTY(bool isCharging READ isCharging NOTIFY changed)
    Q_PROPERTY(bool isPluggedIn READ isPluggedIn NOTIFY changed)
    Q_PROPERTY(bool isFull READ isFull NOTIFY changed)
    Q_PROPERTY(bool isLow READ isLow NOTIFY changed)
    Q_PROPERTY(bool isCritical READ isCritical NOTIFY changed)
    Q_PROPERTY(QString percentageString READ percentageString NOTIFY changed)
    Q_PROPERTY(QString timeRemainingString READ timeRemainingString NOTIFY changed)
public:
    explicit BatteryInfo(QObject *parent = nullptr);
    bool available() const { return m_available; }
    double percentage() const { return m_percentage; }
    bool isCharging() const { return m_charging; }
    bool isPluggedIn() const { return m_plugged; }
    bool isFull() const { return m_full; }
    bool isLow() const { return m_available && m_percentage <= 0.20; }
    bool isCritical() const { return m_available && m_percentage <= 0.10; }
    QString percentageString() const;
    QString timeRemainingString() const;
signals:
    void changed();
private:
    void poll();
    QTimer m_timer;
    bool m_available = false;
    double m_percentage = 0.0;
    bool m_charging = false;
    bool m_plugged = false;
    bool m_full = false;
    int m_secondsLeft = -1;
};

// ---------------------------------------------------------------------------
// CPU / RAM / disk / OS / users (replaces /proc/stat, /proc/meminfo, df, who)
// ---------------------------------------------------------------------------
class SysInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY changed)
    Q_PROPERTY(double memUsage READ memUsage NOTIFY changed)
    Q_PROPERTY(QString memText READ memText NOTIFY changed)
    Q_PROPERTY(double diskUsage READ diskUsage NOTIFY changed)
    Q_PROPERTY(QString diskText READ diskText NOTIFY changed)
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString loggedInUsers READ loggedInUsers NOTIFY changed)
public:
    explicit SysInfo(QObject *parent = nullptr);
    bool active() const { return m_active; }
    void setActive(bool a);
    double cpuUsage() const { return m_cpu; }
    double memUsage() const { return m_memTotal > 0 ? m_memUsed / m_memTotal : 0.0; }
    QString memText() const;
    double diskUsage() const { return m_diskTotal > 0 ? m_diskUsed / m_diskTotal : 0.0; }
    QString diskText() const;
    QString osName() const { return m_osName; }
    QString loggedInUsers() const { return m_users; }
signals:
    void activeChanged();
    void changed();
private:
    void sampleFast();
    void sampleSlow();
    QTimer m_fast;
    QTimer m_slow;
    bool m_active = false;
    double m_cpu = 0.0;
    double m_memUsed = 0.0, m_memTotal = 1.0;
    double m_diskUsed = 0.0, m_diskTotal = 1.0;
    QString m_osName;
    QString m_users;
    quint64 m_lastIdle = 0, m_lastTotal = 0;
};

// ---------------------------------------------------------------------------
// Wi-Fi networks (replaces nmcli)
// ---------------------------------------------------------------------------
class NetInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    Q_PROPERTY(bool wifiAvailable READ wifiAvailable NOTIFY networksChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY networksChanged)
    Q_PROPERTY(QString networkName READ networkName NOTIFY networksChanged)
public:
    explicit NetInfo(QObject *parent = nullptr);
    QVariantList networks() const { return m_networks; }
    bool wifiAvailable() const { return m_wifiAvailable; }
    bool connected() const { return m_connected; }
    QString networkName() const { return m_networkName; }
    Q_INVOKABLE void scanNetworks(); // ask the adapter to rescan, then refresh
    Q_INVOKABLE void update();       // just refresh the list
signals:
    void networksChanged();
private:
    QVariantList m_networks;
    bool m_wifiAvailable = false;
    bool m_connected = false;
    QString m_networkName;
};

// ---------------------------------------------------------------------------
// Bluetooth devices (replaces Quickshell.Bluetooth / BlueZ)
// ---------------------------------------------------------------------------
class BluetoothInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QVariantList friendlyDeviceList READ friendlyDeviceList NOTIFY changed)
public:
    explicit BluetoothInfo(QObject *parent = nullptr);
    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    bool connected() const { return m_connected; }
    QVariantList friendlyDeviceList() const { return m_devices; }
    Q_INVOKABLE void refresh();
signals:
    void changed();
private:
    bool m_available = false;
    bool m_enabled = false;
    bool m_connected = false;
    QVariantList m_devices;
};

// ---------------------------------------------------------------------------
// Power menu actions (replaces loginctl)
// ---------------------------------------------------------------------------
class PowerCtl : public QObject
{
    Q_OBJECT
public:
    explicit PowerCtl(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void lock();
    Q_INVOKABLE void sleep();
};

// ---------------------------------------------------------------------------
// Whole-screen colour filters (replaces Hyprland's screen_shader)
// ---------------------------------------------------------------------------
class ScreenFilter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int active READ active NOTIFY activeChanged) // -1 = none, 0 blue, 1 grey, 2 invert
public:
    explicit ScreenFilter(QObject *parent = nullptr);
    ~ScreenFilter() override;
    int active() const { return m_active; }
    Q_INVOKABLE void toggle(int kind);
    Q_INVOKABLE void clear();
signals:
    void activeChanged();
private:
    bool apply(int kind);
    bool m_initialized = false;
    int m_active = -1;
};
