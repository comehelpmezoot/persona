// winsock2.h must come before windows.h (bluetoothapis.h pulls in Winsock types)
#include <winsock2.h>
#include <windows.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <wlanapi.h>

#include "services.h"

#include <QDebug>
#include <QHash>

#include <algorithm>

// ============================================================================
// NetInfo (WLAN API)
// ============================================================================

NetInfo::NetInfo(QObject *parent)
    : QObject(parent)
{
    update();
}

void NetInfo::update()
{
    QHash<QString, QVariantMap> bySsid;
    bool available = false;

    HANDLE client = nullptr;
    DWORD negotiatedVersion = 0;
    if (WlanOpenHandle(2, nullptr, &negotiatedVersion, &client) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS && interfaces) {
            for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
                available = true;
                PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
                if (WlanGetAvailableNetworkList(client, &interfaces->InterfaceInfo[i].InterfaceGuid, 0,
                                                nullptr, &list) != ERROR_SUCCESS
                    || !list)
                    continue;
                for (DWORD n = 0; n < list->dwNumberOfItems; ++n) {
                    const WLAN_AVAILABLE_NETWORK &net = list->Network[n];
                    const QString ssid = QString::fromUtf8(reinterpret_cast<const char *>(net.dot11Ssid.ucSSID),
                                                           int(net.dot11Ssid.uSSIDLength));
                    if (ssid.isEmpty())
                        continue;
                    const bool active = (net.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
                    const int strength = int(net.wlanSignalQuality);

                    // Same SSID can show up several times (different security/band): keep the best.
                    auto it = bySsid.find(ssid);
                    if (it != bySsid.end()) {
                        const bool existingActive = it->value(QStringLiteral("active")).toBool();
                        const int existingStrength = it->value(QStringLiteral("strength")).toInt();
                        if (existingActive || (!active && existingStrength >= strength))
                            continue;
                    }
                    QVariantMap m;
                    m[QStringLiteral("ssid")] = ssid;
                    m[QStringLiteral("active")] = active;
                    m[QStringLiteral("strength")] = strength;
                    m[QStringLiteral("secure")] = net.bSecurityEnabled != FALSE;
                    bySsid.insert(ssid, m);
                }
                WlanFreeMemory(list);
            }
            WlanFreeMemory(interfaces);
        }
        WlanCloseHandle(client, nullptr);
    }

    QList<QVariantMap> sorted = bySsid.values();
    std::sort(sorted.begin(), sorted.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const bool aa = a.value(QStringLiteral("active")).toBool();
        const bool ba = b.value(QStringLiteral("active")).toBool();
        if (aa != ba)
            return aa;
        const int sa = a.value(QStringLiteral("strength")).toInt();
        const int sb = b.value(QStringLiteral("strength")).toInt();
        if (sa != sb)
            return sa > sb;
        return a.value(QStringLiteral("ssid")).toString().localeAwareCompare(
                   b.value(QStringLiteral("ssid")).toString()) < 0;
    });

    QVariantList list;
    bool connected = false;
    QString name;
    for (const QVariantMap &m : std::as_const(sorted)) {
        if (m.value(QStringLiteral("active")).toBool()) {
            connected = true;
            name = m.value(QStringLiteral("ssid")).toString();
        }
        list.append(m);
    }

    if (available == m_wifiAvailable && connected == m_connected && name == m_networkName
        && list == m_networks)
        return;
    m_wifiAvailable = available;
    m_connected = connected;
    m_networkName = name;
    m_networks = list;
    emit networksChanged();
}

void NetInfo::scanNetworks()
{
    HANDLE client = nullptr;
    DWORD negotiatedVersion = 0;
    if (WlanOpenHandle(2, nullptr, &negotiatedVersion, &client) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS && interfaces) {
            for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i)
                WlanScan(client, &interfaces->InterfaceInfo[i].InterfaceGuid, nullptr, nullptr, nullptr);
            WlanFreeMemory(interfaces);
        }
        WlanCloseHandle(client, nullptr);
    }
    update();
    QTimer::singleShot(2500, this, &NetInfo::update); // scan results arrive asynchronously
}

// ============================================================================
// BluetoothInfo (classic Bluetooth API: paired / remembered / connected devices)
// ============================================================================

BluetoothInfo::BluetoothInfo(QObject *parent)
    : QObject(parent)
{
    refresh();
}

void BluetoothInfo::refresh()
{
    bool available = false;
    bool enabled = false;
    bool anyConnected = false;
    QList<QVariantMap> devices;

    BLUETOOTH_FIND_RADIO_PARAMS radioParams;
    radioParams.dwSize = sizeof(radioParams);
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND radioFind = BluetoothFindFirstRadio(&radioParams, &radio);
    if (radioFind) {
        available = true;
        enabled = BluetoothIsConnectable(radio) != FALSE;

        BLUETOOTH_DEVICE_SEARCH_PARAMS search;
        ZeroMemory(&search, sizeof(search));
        search.dwSize = sizeof(search);
        search.fReturnAuthenticated = TRUE;
        search.fReturnRemembered = TRUE;
        search.fReturnUnknown = FALSE;
        search.fReturnConnected = TRUE;
        search.fIssueInquiry = FALSE; // an inquiry blocks for seconds; paired devices are what matter
        search.cTimeoutMultiplier = 0;
        search.hRadio = radio;

        BLUETOOTH_DEVICE_INFO info;
        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);
        HBLUETOOTH_DEVICE_FIND deviceFind = BluetoothFindFirstDevice(&search, &info);
        if (deviceFind) {
            do {
                QVariantMap m;
                m[QStringLiteral("name")] = QString::fromWCharArray(info.szName);
                m[QStringLiteral("connected")] = info.fConnected != FALSE;
                m[QStringLiteral("paired")] = (info.fAuthenticated != FALSE) || (info.fRemembered != FALSE);
                if (info.fConnected)
                    anyConnected = true;
                devices.append(m);
                info.dwSize = sizeof(info);
            } while (BluetoothFindNextDevice(deviceFind, &info));
            BluetoothFindDeviceClose(deviceFind);
        }
        if (radio)
            CloseHandle(radio);
        BluetoothFindRadioClose(radioFind);
    }

    std::sort(devices.begin(), devices.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const bool ac = a.value(QStringLiteral("connected")).toBool();
        const bool bc = b.value(QStringLiteral("connected")).toBool();
        if (ac != bc)
            return ac;
        return a.value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.value(QStringLiteral("name")).toString()) < 0;
    });

    QVariantList list;
    for (const QVariantMap &m : std::as_const(devices))
        list.append(m);

    if (available == m_available && enabled == m_enabled && anyConnected == m_connected && list == m_devices)
        return;
    m_available = available;
    m_enabled = enabled;
    m_connected = anyConnected;
    m_devices = list;
    emit changed();
}
