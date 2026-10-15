#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QScreen;

// The QML-facing "Shell" singleton. It replaces everything Quickshell/Wayland
// gave the original config: screens, layer-shell style window placement,
// keyboard focus, and the compositor keybind that opened the launcher.
class Shell : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(bool animationsPaused READ animationsPaused NOTIFY animationsPausedChanged)
    Q_PROPERTY(bool windowedMode READ windowedMode CONSTANT)
    Q_PROPERTY(QVariantMap config READ config CONSTANT)
    Q_PROPERTY(QString assetRoot READ assetRoot CONSTANT)

public:
    explicit Shell(QObject *parent = nullptr);
    ~Shell() override;

    static Shell *instance() { return s_instance; }

    // Returns false (and fills `error`) if the assets could not be found.
    bool init(const QVariantMap &config, bool windowed, QString *error);
    bool registerHotkey(const QString &spec);

    QVariantList screens() const;
    bool animationsPaused() const { return m_paused; }
    bool windowedMode() const { return m_windowed; }
    QVariantMap config() const { return m_config; }
    QString assetRoot() const { return m_assetRoot; }

    Q_INVOKABLE QUrl asset(const QString &relativePath) const;
    Q_INVOKABLE QString pickFont(const QStringList &candidates) const;
    Q_INVOKABLE QPointF cursorPos() const;
    Q_INVOKABLE int cursorScreenIndex() const;

    // layer: 0 = desktop (above wallpaper, below apps), 1 = top, 2 = overlay
    Q_INVOKABLE void styleWindow(QObject *window, int layer, bool focusable);
    Q_INVOKABLE bool attachWallpaper(QObject *window, int screenIndex);
    Q_INVOKABLE void activate(QObject *window);
    Q_INVOKABLE void quit();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

public slots:
    void reassertDesktopLayer();

signals:
    void screensChanged();
    void animationsPausedChanged();
    void explorerRestarted();
    void launcherToggleRequested();
    void launcherOpenRequested();
    void launcherCloseRequested();

private:
    void pollFullscreen();
    void connectScreen(QScreen *screen);

    static Shell *s_instance;

    QVariantMap m_config;
    QString m_assetRoot;
    bool m_windowed = false;
    bool m_paused = false;
    int m_hotkeyId = 0;
    unsigned m_taskbarCreatedMsg = 0;
    void *m_winEventHook = nullptr; // HWINEVENTHOOK
    QSet<quintptr> m_desktopWindows; // HWNDs placed on the desktop layer
    QTimer m_fullscreenTimer;
    QTimer m_reassertTimer;
};
