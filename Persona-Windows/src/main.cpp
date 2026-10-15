#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QtQml>

#include "apps.h"
#include "cava.h"
#include "media.h"
#include "services.h"
#include "shell.h"

#include <windows.h>

namespace {

QFile *g_logFile = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const char *tag = "INFO";
    switch (type) {
    case QtDebugMsg: tag = "DEBUG"; break;
    case QtInfoMsg: tag = "INFO"; break;
    case QtWarningMsg: tag = "WARN"; break;
    case QtCriticalMsg: tag = "ERROR"; break;
    case QtFatalMsg: tag = "FATAL"; break;
    }
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
                         + QStringLiteral(" [") + QLatin1String(tag) + QStringLiteral("] ") + message
                         + QLatin1Char('\n');
    if (g_logFile && g_logFile->isOpen()) {
        g_logFile->write(line.toUtf8());
        g_logFile->flush();
    }
    OutputDebugStringW(reinterpret_cast<LPCWSTR>(line.utf16()));
    if (type == QtFatalMsg)
        abort();
}

QString dataDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

QVariantMap loadConfig(const QString &path)
{
    QVariantMap config;
    config[QStringLiteral("hotkey")] = QStringLiteral("Alt+Space");
    config[QStringLiteral("wallpaper")] = true;
    config[QStringLiteral("cava")] = true;
    config[QStringLiteral("capsule")] = true;
    config[QStringLiteral("clock")] = true;
    config[QStringLiteral("drawer")] = true;
    config[QStringLiteral("assetRoot")] = QString();

    QFile file(path);
    if (!file.exists()) {
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(QJsonObject::fromVariantMap(config)).toJson(QJsonDocument::Indented));
            file.close();
        }
        return config;
    }
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            const QVariantMap user = doc.object().toVariantMap();
            for (auto it = user.constBegin(); it != user.constEnd(); ++it)
                config[it.key()] = it.value();
        } else {
            qWarning() << "config.json is not valid JSON:" << error.errorString() << "- using defaults";
        }
    }
    return config;
}

QIcon makeTrayIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(QStringLiteral("#0c0f1d")));
    p.setPen(QPen(QColor(QStringLiteral("#1CC9D9")), 4));
    p.drawEllipse(QRectF(4, 4, 56, 56));
    p.setPen(Qt::white);
    QFont font;
    font.setBold(true);
    font.setPixelSize(30);
    p.setFont(font);
    p.drawText(QRectF(0, 0, 64, 62), Qt::AlignCenter, QStringLiteral(":3"));
    p.end();
    return QIcon(pixmap);
}

bool autostartEnabled(const QString &name)
{
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    return run.contains(name);
}

void setAutostart(const QString &name, bool enabled)
{
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (enabled) {
        run.setValue(name, QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
                               + QLatin1Char('"'));
    } else {
        run.remove(name);
    }
}

// Wait for a previous instance (used by "Restart shell").
void waitForProcess(DWORD pid)
{
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h) {
        WaitForSingleObject(h, 10000);
        CloseHandle(h);
    }
}

QString serverName()
{
    return QStringLiteral("PersonaShell-") + qEnvironmentVariable("USERNAME", QStringLiteral("user"));
}

} // namespace

int main(int argc, char *argv[])
{
    // Direct3D 11 is the only RHI backend that handles transparent top-level windows and
    // video well on Windows. Allow an explicit override through QSG_RHI_BACKEND.
    if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic"); // the native style can't be customised
    if (!qEnvironmentVariableIsSet("QT_QUICK_FLICKABLE_WHEEL_DECELERATION"))
        qputenv("QT_QUICK_FLICKABLE_WHEEL_DECELERATION", "10000");

    QCoreApplication::setApplicationName(QStringLiteral("PersonaShell"));
    QCoreApplication::setOrganizationName(QString());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    // ---- arguments ----------------------------------------------------------------
    QString command; // sent to a running instance
    bool windowed = false;
    DWORD waitPid = 0;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        if (a == QLatin1String("--toggle-launcher"))
            command = QStringLiteral("toggle");
        else if (a == QLatin1String("--open-launcher"))
            command = QStringLiteral("open");
        else if (a == QLatin1String("--close-launcher"))
            command = QStringLiteral("close");
        else if (a == QLatin1String("--quit"))
            command = QStringLiteral("quit");
        else if (a == QLatin1String("--windowed"))
            windowed = true;
        else if (a == QLatin1String("--after-pid") && i + 1 < args.size())
            waitPid = args[++i].toULong();
    }
    if (waitPid)
        waitForProcess(waitPid);

    // ---- single instance / remote commands ------------------------------------------
    {
        QLocalSocket socket;
        socket.connectToServer(serverName());
        if (socket.waitForConnected(300)) {
            AllowSetForegroundWindow(ASFW_ANY); // lets the running shell take focus for the launcher
            socket.write((command.isEmpty() ? QStringLiteral("ping") : command).toUtf8() + '\n');
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            return 0;
        }
        if (command == QLatin1String("quit") || command == QLatin1String("close"))
            return 0; // nothing is running
    }

    // ---- logging ------------------------------------------------------------------------
    const QString logPath = dataDir() + QStringLiteral("/log.txt");
    QFile logFile(logPath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        g_logFile = &logFile;
    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "Persona Shell starting; Qt" << qVersion() << "log:" << logPath;

    // ---- config ---------------------------------------------------------------------------
    const QString configPath = dataDir() + QStringLiteral("/config.json");
    const QVariantMap config = loadConfig(configPath);

    // ---- services (declared before the QML engine so they outlive it) ----------------------
    Shell shell;
    QString error;
    if (!shell.init(config, windowed, &error)) {
        qCritical().noquote() << error;
        QMessageBox::critical(nullptr, QStringLiteral("Persona Shell"), error);
        return 1;
    }
    shell.registerHotkey(config.value(QStringLiteral("hotkey")).toString());

    AudioCtl audio;
    BrightnessCtl brightness;
    BatteryInfo battery;
    SysInfo sysInfo;
    NetInfo netInfo;
    BluetoothInfo bluetooth;
    PowerCtl power;
    ScreenFilter screenFilter;
    MediaCtl media;
    AppCatalog apps;
    apps.refresh();

    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "Shell", &shell);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "AudioCtl", &audio);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "BrightnessCtl", &brightness);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "BatteryInfo", &battery);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "SysInfo", &sysInfo);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "NetInfo", &netInfo);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "BluetoothInfo", &bluetooth);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "PowerCtl", &power);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "ScreenFilter", &screenFilter);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "MediaCtl", &media);
    qmlRegisterSingletonInstance("PersonaNative", 1, 0, "Apps", &apps);
    qmlRegisterType<CavaMonitor>("PersonaNative", 1, 0, "CavaMonitor");

    // ---- remote commands (replaces `qs ipc call searchapp toggle`) -------------------------
    QLocalServer server;
    QLocalServer::removeServer(serverName());
    if (!server.listen(serverName()))
        qWarning() << "Could not open the command pipe:" << server.errorString();
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&]() {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::readyRead, client, [client, &shell, &app]() {
                while (client->canReadLine()) {
                    const QString line = QString::fromUtf8(client->readLine()).trimmed();
                    if (line == QLatin1String("toggle"))
                        emit shell.launcherToggleRequested();
                    else if (line == QLatin1String("open"))
                        emit shell.launcherOpenRequested();
                    else if (line == QLatin1String("close"))
                        emit shell.launcherCloseRequested();
                    else if (line == QLatin1String("quit"))
                        QMetaObject::invokeMethod(&app, []() { QCoreApplication::quit(); }, Qt::QueuedConnection);
                }
            });
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });

    // ---- tray icon ------------------------------------------------------------------------------
    QMenu trayMenu;
    QSystemTrayIcon tray;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.setIcon(makeTrayIcon());
        tray.setToolTip(QStringLiteral("Persona Shell"));

        const QString hotkey = config.value(QStringLiteral("hotkey")).toString();
        QAction *launcher = trayMenu.addAction(QStringLiteral("Open launcher (%1)").arg(hotkey));
        QObject::connect(launcher, &QAction::triggered, &shell, &Shell::launcherToggleRequested);
        trayMenu.addSeparator();

        QAction *autostart = trayMenu.addAction(QStringLiteral("Start with Windows"));
        autostart->setCheckable(true);
        autostart->setChecked(autostartEnabled(QStringLiteral("PersonaShell")));
        QObject::connect(autostart, &QAction::toggled, [](bool on) { setAutostart(QStringLiteral("PersonaShell"), on); });

        QAction *openConfig = trayMenu.addAction(QStringLiteral("Open config.json"));
        QObject::connect(openConfig, &QAction::triggered, [configPath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(configPath));
        });
        QAction *openLog = trayMenu.addAction(QStringLiteral("Open log"));
        QObject::connect(openLog, &QAction::triggered, [logPath]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
        });
        trayMenu.addSeparator();

        QAction *restart = trayMenu.addAction(QStringLiteral("Restart shell"));
        QObject::connect(restart, &QAction::triggered, [&app, windowed]() {
            QStringList restartArgs{QStringLiteral("--after-pid"), QString::number(GetCurrentProcessId())};
            if (windowed)
                restartArgs << QStringLiteral("--windowed");
            QProcess::startDetached(QCoreApplication::applicationFilePath(), restartArgs);
            app.quit();
        });
        QAction *quit = trayMenu.addAction(QStringLiteral("Quit"));
        QObject::connect(quit, &QAction::triggered, &app, &QCoreApplication::quit);

        tray.setContextMenu(&trayMenu);
        QObject::connect(&tray, &QSystemTrayIcon::activated, [&shell](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger)
                emit shell.launcherToggleRequested();
        });
        tray.show();
    }

    // ---- QML --------------------------------------------------------------------------------------
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("mediaart"), new MediaArtProvider(&media));
    engine.addImageProvider(QStringLiteral("appicon"), new AppIconProvider(&apps));

    bool loadFailed = false;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [&loadFailed](const QUrl &) {
            loadFailed = true;
            QCoreApplication::exit(2);
        },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Persona/main.qml")));

    if (engine.rootObjects().isEmpty() || loadFailed) {
        const QString text = QStringLiteral("The shell UI failed to load.\n\nDetails were written to:\n%1").arg(logPath);
        qCritical().noquote() << text;
        QMessageBox::critical(nullptr, QStringLiteral("Persona Shell"), text);
        return 2;
    }

    const int code = app.exec();
    qInfo() << "Persona Shell exiting with code" << code;
    g_logFile = nullptr;
    return code;
}
