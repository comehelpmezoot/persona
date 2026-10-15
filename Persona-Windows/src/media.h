#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>

#include <memory>

struct MediaState {
    bool available = false;
    QString title;
    QString artist;
    bool playing = false;
    bool canPrevious = false;
    bool canNext = false;
    bool canToggle = false;
    QImage art;
};

// "Now playing" for whatever app Windows considers the current media session
// (Spotify, browsers, VLC, ...), via GlobalSystemMediaTransportControls (SMTC).
// Replaces Quickshell.Services.Mpris.
class MediaCtl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed)
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(bool canToggle READ canToggle NOTIFY changed)
public:
    explicit MediaCtl(QObject *parent = nullptr);
    ~MediaCtl() override;

    bool available() const { return m_state.available; }
    QString title() const { return m_state.title; }
    QString artist() const { return m_state.artist; }
    QString artUrl() const;
    bool playing() const { return m_state.playing; }
    bool canPrevious() const { return m_state.canPrevious; }
    bool canNext() const { return m_state.canNext; }
    bool canToggle() const { return m_state.canToggle; }

    Q_INVOKABLE void previous();
    Q_INVOKABLE void next();
    Q_INVOKABLE void togglePlaying();

    QImage art() const; // thread-safe, for the image provider

signals:
    void changed();

private:
    void applyState(const MediaState &state);

    struct Impl;
    std::unique_ptr<Impl> d;

    MediaState m_state; // art is kept separately below
    mutable QMutex m_artMutex;
    QImage m_art;
    int m_artSerial = 0;
};

class MediaArtProvider : public QQuickImageProvider
{
public:
    explicit MediaArtProvider(MediaCtl *ctl)
        : QQuickImageProvider(QQuickImageProvider::Image), m_ctl(ctl) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    MediaCtl *m_ctl;
};
