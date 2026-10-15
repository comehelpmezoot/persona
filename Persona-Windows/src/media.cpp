// NOTE: the C++/WinRT headers must be included before any Qt header (Qt's
// "emit"/"signals"/"slots" keyword macros upset some WinRT headers).
#ifdef PERSONA_WINRT
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#endif

#include "media.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>

#ifdef PERSONA_WINRT

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;

struct MediaCtl::Impl {
    MediaCtl *owner = nullptr;

    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    GlobalSystemMediaTransportControlsSession session{nullptr};
    winrt::event_token managerToken{};
    winrt::event_token propsToken{};
    winrt::event_token playbackToken{};
    bool managerSubscribed = false;

    std::mutex mutex; // guards `session` and serialises refreshes
    std::thread worker;
    std::mutex wakeMutex;
    std::condition_variable wake;
    bool stop = false;

    void start()
    {
        worker = std::thread([this] { run(); });
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(wakeMutex);
            stop = true;
        }
        wake.notify_all();
        if (worker.joinable())
            worker.join();
    }

    void run()
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            managerToken = manager.CurrentSessionChanged([this](auto &&, auto &&) { rebind(); });
            managerSubscribed = true;
            rebind();
        } catch (winrt::hresult_error const &e) {
            qWarning() << "Media (SMTC) unavailable:" << QString::fromWCharArray(e.message().c_str());
        } catch (...) {
            qWarning() << "Media (SMTC) unavailable";
        }

        {
            std::unique_lock<std::mutex> lock(wakeMutex);
            wake.wait(lock, [this] { return stop; });
        }

        try {
            std::lock_guard<std::mutex> lock(mutex);
            if (session) {
                session.MediaPropertiesChanged(propsToken);
                session.PlaybackInfoChanged(playbackToken);
                session = nullptr;
            }
            if (managerSubscribed && manager) {
                manager.CurrentSessionChanged(managerToken);
                managerSubscribed = false;
            }
            manager = nullptr;
        } catch (...) {
        }
    }

    void rebind()
    {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            if (session) {
                session.MediaPropertiesChanged(propsToken);
                session.PlaybackInfoChanged(playbackToken);
                session = nullptr;
            }
            if (manager)
                session = manager.GetCurrentSession();
            if (session) {
                propsToken = session.MediaPropertiesChanged([this](auto &&, auto &&) { refresh(); });
                playbackToken = session.PlaybackInfoChanged([this](auto &&, auto &&) { refresh(); });
            }
        } catch (...) {
        }
        refreshLocked();
    }

    void refresh()
    {
        std::lock_guard<std::mutex> lock(mutex);
        refreshLocked();
    }

    void refreshLocked()
    {
        MediaState state;
        try {
            if (session) {
                state.available = true;
                auto props = session.TryGetMediaPropertiesAsync().get();
                state.title = QString::fromWCharArray(props.Title().c_str());
                state.artist = QString::fromWCharArray(props.Artist().c_str());

                auto info = session.GetPlaybackInfo();
                state.playing = info.PlaybackStatus()
                                == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                auto controls = info.Controls();
                state.canPrevious = controls.IsPreviousEnabled();
                state.canNext = controls.IsNextEnabled();
                state.canToggle = controls.IsPlayPauseToggleEnabled();

                auto thumbnail = props.Thumbnail();
                if (thumbnail) {
                    auto stream = thumbnail.OpenReadAsync().get();
                    const uint64_t size = stream.Size();
                    if (size > 0 && size < 16u * 1024u * 1024u) {
                        DataReader reader(stream.GetInputStreamAt(0));
                        reader.LoadAsync(static_cast<uint32_t>(size)).get();
                        std::vector<uint8_t> bytes(static_cast<size_t>(size));
                        reader.ReadBytes(bytes);
                        state.art.loadFromData(bytes.data(), int(bytes.size()));
                    }
                    stream.Close();
                }
            }
        } catch (winrt::hresult_error const &) {
            // session vanished while we were reading it; report what we have
        } catch (...) {
        }

        MediaCtl *target = owner;
        QMetaObject::invokeMethod(
            target, [target, state] { target->applyState(state); }, Qt::QueuedConnection);
    }

    void control(int kind)
    {
        GlobalSystemMediaTransportControlsSession s{nullptr};
        {
            std::lock_guard<std::mutex> lock(mutex);
            s = session;
        }
        if (!s)
            return;
        try {
            if (kind == 0)
                s.TrySkipPreviousAsync();
            else if (kind == 1)
                s.TrySkipNextAsync();
            else
                s.TryTogglePlayPauseAsync();
        } catch (...) {
        }
    }
};

MediaCtl::MediaCtl(QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->owner = this;
    d->start();
}

MediaCtl::~MediaCtl()
{
    d->shutdown();
}

void MediaCtl::previous() { d->control(0); }
void MediaCtl::next() { d->control(1); }
void MediaCtl::togglePlaying() { d->control(2); }

#else // !PERSONA_WINRT: media controls disabled at build time

struct MediaCtl::Impl {};

MediaCtl::MediaCtl(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {}
MediaCtl::~MediaCtl() = default;
void MediaCtl::previous() {}
void MediaCtl::next() {}
void MediaCtl::togglePlaying() {}

#endif

// ---------------------------------------------------------------------------

QString MediaCtl::artUrl() const
{
    QMutexLocker lock(&m_artMutex);
    if (m_art.isNull())
        return QString();
    return QStringLiteral("image://mediaart/%1").arg(m_artSerial);
}

QImage MediaCtl::art() const
{
    QMutexLocker lock(&m_artMutex);
    return m_art;
}

void MediaCtl::applyState(const MediaState &state)
{
    bool changed = false;
    {
        QMutexLocker lock(&m_artMutex);
        if (state.art != m_art) {
            m_art = state.art;
            ++m_artSerial;
            changed = true;
        }
    }
    if (state.available != m_state.available || state.title != m_state.title
        || state.artist != m_state.artist || state.playing != m_state.playing
        || state.canPrevious != m_state.canPrevious || state.canNext != m_state.canNext
        || state.canToggle != m_state.canToggle)
        changed = true;

    m_state.available = state.available;
    m_state.title = state.title;
    m_state.artist = state.artist;
    m_state.playing = state.playing;
    m_state.canPrevious = state.canPrevious;
    m_state.canNext = state.canNext;
    m_state.canToggle = state.canToggle;

    if (changed)
        emit changed();
}

QImage MediaArtProvider::requestImage(const QString &, QSize *size, const QSize &)
{
    const QImage image = m_ctl->art();
    if (size)
        *size = image.size();
    return image;
}
