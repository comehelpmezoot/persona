#include "shell.h"

#include "hotkey.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include <windows.h>
#include <shellapi.h>

Shell *Shell::s_instance = nullptr;

namespace {

constexpr int kHotkeyId = 0x5045; // 'PE'
constexpr UINT kModNoRepeat = 0x4000;

HWND hwndOf(QObject *object)
{
    auto *window = qobject_cast<QWindow *>(object);
    if (!window)
        return nullptr;
    return reinterpret_cast<HWND>(window->winId()); // creates the native window if needed
}

// ---- monitors -------------------------------------------------------------

struct MonitorSearch {
    QString device;
    RECT rect{};
    bool found = false;
};

BOOL CALLBACK monitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
    auto *search = reinterpret_cast<MonitorSearch *>(param);
    MONITORINFOEXW info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        if (QString::fromWCharArray(info.szDevice) == search->device) {
            search->rect = info.rcMonitor;
            search->found = true;
            return FALSE;
        }
    }
    return TRUE;
}

// Physical-pixel rectangle of a QScreen (Win32 coordinates).
RECT physicalRectOf(QScreen *screen)
{
    MonitorSearch search;
    search.device = screen->name();
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(&search));
    if (search.found)
        return search.rect;

    const QRect g = screen->geometry();
    const qreal dpr = screen->devicePixelRatio();
    RECT r;
    r.left = LONG(g.x() * dpr);
    r.top = LONG(g.y() * dpr);
    r.right = LONG((g.x() + g.width()) * dpr);
    r.bottom = LONG((g.y() + g.height()) * dpr);
    return r;
}

// ---- desktop (Progman / WorkerW) -------------------------------------------

// The window that hosts the desktop icons: a top-level WorkerW on Windows 10 /
// early Windows 11, Progman itself on Windows 11 24H2 and later.
HWND findIconHost()
{
    HWND host = nullptr;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            if (FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr)) {
                *reinterpret_cast<HWND *>(lp) = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&host));
    if (!host)
        host = FindWindowW(L"Progman", nullptr);
    return host;
}

// The window a wallpaper must be parented to so that it renders *behind* the
// desktop icons.
HWND findWallpaperParent()
{
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman)
        return nullptr;

    // Ask Progman to spawn the "WorkerW behind the icons". Harmless if it exists.
    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);

    // Windows 10 / Windows 11 before 24H2: a top-level WorkerW sibling that comes
    // after the window hosting SHELLDLL_DefView.
    HWND workerw = nullptr;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            if (FindWindowExW(h, nullptr, L"SHELLDLL_DefView", nullptr)) {
                HWND w = FindWindowExW(nullptr, h, L"WorkerW", nullptr);
                if (w) {
                    *reinterpret_cast<HWND *>(lp) = w;
                    return FALSE;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&workerw));
    if (workerw)
        return workerw;

    // Windows 11 24H2+: the WorkerW is a child of Progman.
    HWND child = nullptr;
    while ((child = FindWindowExW(progman, child, L"WorkerW", nullptr)) != nullptr) {
        if (!FindWindowExW(child, nullptr, L"SHELLDLL_DefView", nullptr))
            return child;
    }
    return nullptr;
}

void CALLBACK onForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    if (Shell *shell = Shell::instance())
        QMetaObject::invokeMethod(shell, "reassertDesktopLayer", Qt::QueuedConnection);
}

QString findAssetRoot(const QVariantMap &config, QStringList *tried)
{
    QStringList candidates;
    const QString fromEnv = qEnvironmentVariable("PERSONA_ASSETS");
    if (!fromEnv.isEmpty())
        candidates << fromEnv;
    const QString fromConfig = config.value(QStringLiteral("assetRoot")).toString();
    if (!fromConfig.isEmpty())
        candidates << fromConfig;
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << appDir + QStringLiteral("/Assets")
               << appDir + QStringLiteral("/../Assets")
               << appDir + QStringLiteral("/../../Assets")
               << appDir + QStringLiteral("/../../../Assets")
               << appDir + QStringLiteral("/../Persona-Quickshell-main/Assets")
               << appDir + QStringLiteral("/../../Persona-Quickshell-main/Assets")
               << appDir + QStringLiteral("/../../../Persona-Quickshell-main/Assets")
               << appDir + QStringLiteral("/../../../../Persona-Quickshell-main/Assets");
#ifdef PERSONA_DEFAULT_ASSETS
    candidates << QString::fromUtf8(PERSONA_DEFAULT_ASSETS);
#endif
    for (const QString &c : candidates) {
        const QString clean = QDir::cleanPath(c);
        if (tried)
            tried->append(clean);
        if (QFile::exists(clean + QStringLiteral("/p3r imgs/bg.png")))
            return clean;
    }
    return QString();
}

} // namespace

// ============================================================================

Shell::Shell(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
}

Shell::~Shell()
{
    if (m_hotkeyId)
        UnregisterHotKey(nullptr, m_hotkeyId);
    if (m_winEventHook)
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(m_winEventHook));
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
    if (s_instance == this)
        s_instance = nullptr;
}

bool Shell::init(const QVariantMap &config, bool windowed, QString *error)
{
    m_config = config;
    m_windowed = windowed;

    QStringList tried;
    m_assetRoot = findAssetRoot(config, &tried);
    if (m_assetRoot.isEmpty()) {
        if (error)
            *error = QStringLiteral(
                         "Could not find the Persona 'Assets' folder (looked for 'p3r imgs/bg.png').\n\n"
                         "Put the Assets folder from the original Persona-Quickshell repo next to the "
                         "program, or set the PERSONA_ASSETS environment variable.\n\nSearched:\n")
                     + tried.join(QLatin1Char('\n'));
        return false;
    }
    qInfo().noquote() << "Assets:" << m_assetRoot;

    // Optional fonts: drop Montserrat / Bebas Neue / Proggy / a Nerd Font into a "fonts" folder.
    const QStringList fontDirs = {m_assetRoot + QStringLiteral("/fonts"),
                                  QCoreApplication::applicationDirPath() + QStringLiteral("/fonts")};
    for (const QString &dirPath : fontDirs) {
        const QDir dir(dirPath);
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files);
        for (const QFileInfo &fi : files) {
            if (QFontDatabase::addApplicationFont(fi.absoluteFilePath()) < 0)
                qWarning() << "Could not load font" << fi.absoluteFilePath();
        }
    }

    // Screens
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens)
        connectScreen(s);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *s) {
        connectScreen(s);
        emit screensChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this]() { emit screensChanged(); });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this]() { emit screensChanged(); });

    // Native events: global hotkey and "Explorer restarted"
    QCoreApplication::instance()->installNativeEventFilter(this);
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Pause the heavy wallpaper while a fullscreen game / video is running.
    m_fullscreenTimer.setInterval(2000);
    connect(&m_fullscreenTimer, &QTimer::timeout, this, &Shell::pollFullscreen);
    m_fullscreenTimer.start();

    // Keep desktop-layer widgets just above the desktop.
    m_winEventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                     onForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
    m_reassertTimer.setInterval(3000);
    connect(&m_reassertTimer, &QTimer::timeout, this, &Shell::reassertDesktopLayer);
    m_reassertTimer.start();

    return true;
}

void Shell::connectScreen(QScreen *screen)
{
    connect(screen, &QScreen::geometryChanged, this, [this]() { emit screensChanged(); });
    connect(screen, &QScreen::availableGeometryChanged, this, [this]() { emit screensChanged(); });
}

bool Shell::registerHotkey(const QString &spec)
{
    const HotkeySpec parsed = parseHotkey(spec.toStdString());
    if (!parsed.ok) {
        qWarning().noquote() << "Invalid hotkey" << spec << "- expected something like Alt+Space";
        return false;
    }
    if (m_hotkeyId) {
        UnregisterHotKey(nullptr, m_hotkeyId);
        m_hotkeyId = 0;
    }
    if (!RegisterHotKey(nullptr, kHotkeyId, parsed.modifiers | kModNoRepeat, parsed.vk)) {
        qWarning().noquote() << "RegisterHotKey failed for" << spec << "(error" << GetLastError()
                             << ") - another program probably owns it. Change \"hotkey\" in config.json.";
        return false;
    }
    m_hotkeyId = kHotkeyId;
    qInfo().noquote() << "Launcher hotkey:" << spec;
    return true;
}

QVariantList Shell::screens() const
{
    QVariantList list;
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        const QRect g = s->geometry();
        const QRect a = s->availableGeometry();
        QVariantMap m;
        m[QStringLiteral("name")] = s->name();
        m[QStringLiteral("x")] = g.x();
        m[QStringLiteral("y")] = g.y();
        m[QStringLiteral("width")] = g.width();
        m[QStringLiteral("height")] = g.height();
        m[QStringLiteral("wx")] = a.x();
        m[QStringLiteral("wy")] = a.y();
        m[QStringLiteral("ww")] = a.width();
        m[QStringLiteral("wh")] = a.height();
        m[QStringLiteral("dpr")] = s->devicePixelRatio();
        m[QStringLiteral("primary")] = (s == QGuiApplication::primaryScreen());
        list.append(m);
    }
    return list;
}

QUrl Shell::asset(const QString &relativePath) const
{
    return QUrl::fromLocalFile(m_assetRoot + QLatin1Char('/') + relativePath);
}

QString Shell::pickFont(const QStringList &candidates) const
{
    const QStringList families = QFontDatabase::families();
    for (const QString &wanted : candidates) {
        for (const QString &family : families) {
            if (family.compare(wanted, Qt::CaseInsensitive) == 0)
                return family;
        }
    }
    return candidates.isEmpty() ? QString() : candidates.last();
}

QPointF Shell::cursorPos() const
{
    return QPointF(QCursor::pos());
}

int Shell::cursorScreenIndex() const
{
    const auto screens = QGuiApplication::screens();
    QScreen *at = QGuiApplication::screenAt(QCursor::pos());
    int index = screens.indexOf(at);
    if (index < 0)
        index = screens.indexOf(QGuiApplication::primaryScreen());
    return qMax(0, index);
}

void Shell::quit()
{
    QCoreApplication::quit();
}

// ---- window placement --------------------------------------------------------

void Shell::styleWindow(QObject *window, int layer, bool focusable)
{
    HWND hwnd = hwndOf(window);
    if (!hwnd)
        return;

    if (layer <= 0) {
        m_desktopWindows.insert(reinterpret_cast<quintptr>(hwnd));
        reassertDesktopLayer();
    } else {
        m_desktopWindows.remove(reinterpret_cast<quintptr>(hwnd));
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (focusable)
        activate(window);
}

void Shell::reassertDesktopLayer()
{
    if (m_desktopWindows.isEmpty())
        return;
    HWND host = findIconHost();
    if (!host)
        return;

    // Drop windows that no longer exist.
    for (auto it = m_desktopWindows.begin(); it != m_desktopWindows.end();) {
        if (!IsWindow(reinterpret_cast<HWND>(*it)))
            it = m_desktopWindows.erase(it);
        else
            ++it;
    }

    // Our windows should form one contiguous block directly above the desktop.
    QSet<quintptr> block;
    HWND p = GetWindow(host, GW_HWNDPREV);
    while (p && m_desktopWindows.contains(reinterpret_cast<quintptr>(p))) {
        block.insert(reinterpret_cast<quintptr>(p));
        p = GetWindow(p, GW_HWNDPREV);
    }

    HWND insertAfter = HWND_TOP;
    if (p) {
        const LONG_PTR ex = GetWindowLongPtrW(p, GWL_EXSTYLE);
        insertAfter = (ex & WS_EX_TOPMOST) ? HWND_NOTOPMOST : p;
    }

    for (quintptr h : std::as_const(m_desktopWindows)) {
        if (block.contains(h))
            continue;
        HWND hwnd = reinterpret_cast<HWND>(h);
        if (!IsWindowVisible(hwnd))
            continue;
        SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

bool Shell::attachWallpaper(QObject *object, int screenIndex)
{
    auto *window = qobject_cast<QWindow *>(object);
    const auto screens = QGuiApplication::screens();
    if (!window || screenIndex < 0 || screenIndex >= screens.size())
        return false;

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    HWND parent = findWallpaperParent();
    if (!hwnd || !parent) {
        qWarning() << "Wallpaper: could not find the desktop WorkerW window";
        return false;
    }

    const RECT mon = physicalRectOf(screens[screenIndex]);

    SetParent(hwnd, parent);

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= (WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex &= ~(WS_EX_APPWINDOW | WS_EX_TOPMOST);
    ex |= (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);

    // Child coordinates are relative to the parent's client area.
    POINT origin = {mon.left, mon.top};
    MapWindowPoints(HWND_DESKTOP, parent, &origin, 1);

    SetWindowPos(hwnd, HWND_BOTTOM, origin.x, origin.y, mon.right - mon.left, mon.bottom - mon.top,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    qInfo() << "Wallpaper attached on screen" << screenIndex << "at" << origin.x << origin.y
            << (mon.right - mon.left) << "x" << (mon.bottom - mon.top);
    return true;
}

void Shell::activate(QObject *object)
{
    auto *window = qobject_cast<QWindow *>(object);
    HWND hwnd = hwndOf(object);
    if (!window || !hwnd)
        return;

    // Windows only lets the foreground thread hand out focus; attach to it briefly.
    HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD thisThread = GetCurrentThreadId();
    const bool attach = foregroundThread && foregroundThread != thisThread;
    if (attach)
        AttachThreadInput(thisThread, foregroundThread, TRUE);

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    if (attach)
        AttachThreadInput(thisThread, foregroundThread, FALSE);

    window->requestActivate();
}

void Shell::pollFullscreen()
{
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    bool paused = false;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        paused = (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN
                  || state == QUNS_PRESENTATION_MODE);
    }
    if (paused != m_paused) {
        m_paused = paused;
        emit animationsPausedChanged();
    }
}

bool Shell::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message == WM_HOTKEY && int(msg->wParam) == m_hotkeyId && m_hotkeyId != 0) {
        emit launcherToggleRequested();
        return true;
    }
    if (m_taskbarCreatedMsg && msg->message == m_taskbarCreatedMsg)
        emit explorerRestarted();
    return false;
}
