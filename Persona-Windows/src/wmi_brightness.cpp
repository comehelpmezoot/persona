#include "services.h"

#include <QDebug>
#include <QMetaObject>

#include <chrono>

#include <windows.h>
#include <wbemidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

// Owns a BSTR for the duration of a call.
struct Bstr {
    explicit Bstr(const wchar_t *s) : value(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(value); }
    BSTR value;
};

// Returns the panel brightness in percent, or -1 when there is no internal panel
// that exposes brightness through WMI (desktop PCs, external monitors).
int queryBrightness(IWbemServices *services)
{
    Bstr language(L"WQL");
    Bstr query(L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active=TRUE");

    ComPtr<IEnumWbemClassObject> enumerator;
    HRESULT hr = services->ExecQuery(language.value, query.value,
                                     WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                     nullptr, &enumerator);
    if (FAILED(hr) || !enumerator)
        return -1;

    ComPtr<IWbemClassObject> object;
    ULONG returned = 0;
    hr = enumerator->Next(2000, 1, &object, &returned);
    if (FAILED(hr) || returned == 0 || !object)
        return -1;

    VARIANT value;
    VariantInit(&value);
    int result = -1;
    if (SUCCEEDED(object->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr))) {
        VARIANT asInt;
        VariantInit(&asInt);
        if (SUCCEEDED(VariantChangeType(&asInt, &value, 0, VT_I4)))
            result = int(asInt.lVal);
        VariantClear(&asInt);
    }
    VariantClear(&value);
    return result;
}

} // namespace

BrightnessCtl::BrightnessCtl(QObject *parent)
    : QObject(parent)
{
    m_thread = std::thread([this] { run(); });
}

BrightnessCtl::~BrightnessCtl()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

void BrightnessCtl::run()
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initHr)) {
        qWarning() << "Brightness: CoInitializeEx failed" << Qt::hex << quint32(initHr);
        return;
    }

    ComPtr<IWbemLocator> locator;
    ComPtr<IWbemServices> services;

    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IWbemLocator, reinterpret_cast<void **>(locator.GetAddressOf())))) {
        Bstr ns(L"ROOT\\WMI");
        if (SUCCEEDED(locator->ConnectServer(ns.value, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                             &services))) {
            CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                              RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        } else {
            services.Reset();
        }
    }

    int lastReported = -2;
    while (!m_stop && services) {
        const int value = queryBrightness(services.Get());
        if (value != lastReported) {
            lastReported = value;
            QMetaObject::invokeMethod(this, [this, value] { onSample(value); }, Qt::QueuedConnection);
        }
        // sleep ~400 ms in small steps so shutdown stays snappy
        for (int i = 0; i < 8 && !m_stop; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // No panel at all? Re-check rarely instead of hammering WMI.
        if (value < 0) {
            for (int i = 0; i < 100 && !m_stop; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    services.Reset();
    locator.Reset();
    CoUninitialize();
}

void BrightnessCtl::onSample(int value)
{
    const bool available = value >= 0;
    if (available != m_available) {
        m_available = available;
        emit availableChanged();
    }
    if (!available)
        return;

    const bool changed = value != m_brightness;
    m_brightness = value;
    if (changed)
        emit brightnessChanged();
    if (!m_baselineSet) {
        m_baselineSet = true; // first reading: no OSD
        return;
    }
    if (changed)
        emit userAdjusted();
}
