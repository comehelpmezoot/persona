#pragma once

#include <QFuture>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <atomic>

struct AppEntry {
    QString name;
    QString parsingName; // e.g. "shell:AppsFolder\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"
    bool store = false;
};

// Everything the Start menu lists (classic + Store apps), via the shell's
// virtual "Apps" folder. Replaces Quickshell's DesktopEntries.
class AppCatalog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    explicit AppCatalog(QObject *parent = nullptr);
    ~AppCatalog() override;

    QVariantList list() const { return m_list; }
    bool loading() const { return m_loading; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool launch(const QString &parsingName);

    // Used by the icon provider (any thread).
    QString parsingNameFor(int generation, int index) const;

signals:
    void listChanged();
    void loadingChanged();

private:
    void applyEntries(const QList<AppEntry> &entries, int generation);

    QVariantList m_list;
    bool m_loading = false;
    std::atomic<int> m_generation{0};
    mutable QMutex m_mutex;
    QStringList m_parsingNames; // index -> parsing name for the current generation
    int m_currentGeneration = 0;
    QFuture<void> m_future;
};

class AppIconProvider : public QQuickImageProvider
{
public:
    explicit AppIconProvider(AppCatalog *catalog)
        : QQuickImageProvider(QQuickImageProvider::Image), m_catalog(catalog) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    AppCatalog *m_catalog;
};
