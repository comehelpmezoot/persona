#include "apps.h"

#include <QDebug>
#include <QFuture>
#include <QHash>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSet>
#include <QtConcurrent>

#include <algorithm>

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

// COM must be initialised on every thread that talks to the shell.
struct ComScope {
    ComScope() : ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope()
    {
        if (ok)
            CoUninitialize();
    }
    bool ok;
};

QString takeString(PWSTR s)
{
    QString result = s ? QString::fromWCharArray(s) : QString();
    if (s)
        CoTaskMemFree(s);
    return result;
}

QList<AppEntry> enumerateApps()
{
    QList<AppEntry> result;
    ComScope com;

    ComPtr<IShellItem> folder;
    if (FAILED(SHCreateItemFromParsingName(L"shell:AppsFolder", nullptr, IID_PPV_ARGS(&folder)))) {
        qWarning() << "Apps: cannot open shell:AppsFolder";
        return result;
    }
    ComPtr<IEnumShellItems> items;
    if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))))
        return result;

    QSet<QString> seen;
    ComPtr<IShellItem> item;
    ULONG fetched = 0;
    while (items->Next(1, &item, &fetched) == S_OK && fetched == 1) {
        PWSTR rawName = nullptr;
        PWSTR rawParse = nullptr;
        item->GetDisplayName(SIGDN_NORMALDISPLAY, &rawName);
        item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &rawParse);
        const QString name = takeString(rawName).trimmed();
        QString parse = takeString(rawParse);
        item.Reset();

        if (name.isEmpty() || parse.isEmpty())
            continue;
        if (name.startsWith(QStringLiteral("Uninstall"), Qt::CaseInsensitive))
            continue;
        if (!parse.startsWith(QStringLiteral("shell:AppsFolder"), Qt::CaseInsensitive))
            parse = QStringLiteral("shell:AppsFolder\\") + parse;

        const QString key = name.toLower() + QLatin1Char('|') + parse.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);

        AppEntry e;
        e.name = name;
        e.parsingName = parse;
        e.store = parse.contains(QLatin1Char('!'));
        result.append(e);
    }

    std::sort(result.begin(), result.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return result;
}

QImage imageFromHBitmap(HBITMAP bitmap)
{
    BITMAP bm;
    if (!GetObjectW(bitmap, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return QImage();
    const int w = bm.bmWidth;
    const int h = bm.bmHeight;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    HDC dc = GetDC(nullptr);
    const int lines = GetDIBits(dc, bitmap, 0, UINT(h), image.bits(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != h)
        return QImage();

    // Some icons come back without any alpha information: make them opaque.
    bool anyAlpha = false;
    for (int y = 0; y < h && !anyAlpha; ++y) {
        const quint32 *row = reinterpret_cast<const quint32 *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (row[x] >> 24) {
                anyAlpha = true;
                break;
            }
        }
    }
    if (!anyAlpha) {
        for (int y = 0; y < h; ++y) {
            quint32 *row = reinterpret_cast<quint32 *>(image.scanLine(y));
            for (int x = 0; x < w; ++x)
                row[x] |= 0xFF000000u;
        }
    }
    return image;
}

QImage loadShellIcon(const QString &parsingName, int pixels)
{
    thread_local ComScope com; // image provider threads are long-lived
    Q_UNUSED(com);

    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(reinterpret_cast<PCWSTR>(parsingName.utf16()), nullptr,
                                           IID_PPV_ARGS(&factory))))
        return QImage();

    HBITMAP bitmap = nullptr;
    SIZE size = {pixels, pixels};
    const HRESULT hr = factory->GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
    if (FAILED(hr) || !bitmap)
        return QImage();
    QImage image = imageFromHBitmap(bitmap);
    DeleteObject(bitmap);
    return image;
}

} // namespace

// ============================================================================

AppCatalog::AppCatalog(QObject *parent)
    : QObject(parent)
{
}

AppCatalog::~AppCatalog()
{
    m_generation = -1; // discard any result still in flight
    m_future.waitForFinished();
}

void AppCatalog::refresh()
{
    if (m_loading)
        return;
    m_loading = true;
    emit loadingChanged();

    const int request = ++m_generation;
    m_future = QtConcurrent::run([this, request] {
        const QList<AppEntry> entries = enumerateApps();
        QMetaObject::invokeMethod(
            this, [this, entries, request] { applyEntries(entries, request); }, Qt::QueuedConnection);
    });
}

void AppCatalog::applyEntries(const QList<AppEntry> &entries, int request)
{
    m_loading = false;
    emit loadingChanged();
    if (request != m_generation.load())
        return; // superseded

    QStringList parsing;
    parsing.reserve(entries.size());
    for (const AppEntry &e : entries)
        parsing << e.parsingName;

    {
        QMutexLocker lock(&m_mutex);
        // Same apps as before? Keep the old list (and icon URLs) so the UI doesn't flicker.
        if (parsing == m_parsingNames && !m_list.isEmpty())
            return;
        m_parsingNames = parsing;
        ++m_currentGeneration;
    }

    QVariantList list;
    list.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        QVariantMap m;
        m[QStringLiteral("name")] = entries[i].name;
        m[QStringLiteral("id")] = entries[i].parsingName;
        m[QStringLiteral("kind")] = entries[i].store ? QStringLiteral("Store app") : QStringLiteral("Desktop app");
        m[QStringLiteral("icon")] = QStringLiteral("image://appicon/%1/%2").arg(m_currentGeneration).arg(i);
        list.append(m);
    }
    m_list = list;
    emit listChanged();
}

bool AppCatalog::launch(const QString &parsingName)
{
    if (parsingName.isEmpty())
        return false;
    const HINSTANCE r = ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(parsingName.utf16()),
                                      nullptr, nullptr, SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<INT_PTR>(r) > 32;
    if (!ok)
        qWarning() << "Apps: could not launch" << parsingName << "code" << reinterpret_cast<INT_PTR>(r);
    return ok;
}

QString AppCatalog::parsingNameFor(int generation, int index) const
{
    QMutexLocker lock(&m_mutex);
    if (generation != m_currentGeneration || index < 0 || index >= m_parsingNames.size())
        return QString();
    return m_parsingNames.at(index);
}

QImage AppIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    QImage image;
    if (parts.size() == 2) {
        const int generation = parts[0].toInt();
        const int index = parts[1].toInt();
        const QString parsing = m_catalog->parsingNameFor(generation, index);
        if (!parsing.isEmpty()) {
            static QHash<QString, QImage> cache;
            static QMutex cacheMutex;
            {
                QMutexLocker lock(&cacheMutex);
                image = cache.value(parsing);
            }
            if (image.isNull()) {
                image = loadShellIcon(parsing, 96);
                if (!image.isNull()) {
                    QMutexLocker lock(&cacheMutex);
                    cache.insert(parsing, image);
                }
            }
        }
    }
    if (!image.isNull() && requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
