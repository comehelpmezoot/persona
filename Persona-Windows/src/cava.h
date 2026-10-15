#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <memory>
#include <vector>

#include "spectrum.h"

// Drop-in replacement for the Linux "CavaMonitor" QML type
// (github.com/Yujonpradhananga/Qt6-Cava-plugin): bars / active / values.
// Audio comes from a WASAPI loopback capture of the default playback device.
class CavaMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int bars READ bars WRITE setBars NOTIFY barsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)
public:
    explicit CavaMonitor(QObject *parent = nullptr);
    ~CavaMonitor() override;

    int bars() const { return m_bars; }
    void setBars(int bars);
    bool active() const { return m_active; }
    void setActive(bool active);
    QVariantList values() const { return m_values; }

signals:
    void barsChanged();
    void activeChanged();
    void valuesChanged();

private:
    void tick();
    void resetValues();

    int m_bars = 50;
    bool m_active = false;
    bool m_holdsCapture = false;
    QVariantList m_values;
    QTimer m_timer;
    std::unique_ptr<SpectrumAnalyzer> m_analyzer;
    std::vector<float> m_samples;
    QElapsedTimer m_clock;
    bool m_wasIdle = true;
};
