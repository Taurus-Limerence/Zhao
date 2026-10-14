#include "UsbIdentity.h"

#ifdef Q_OS_WIN
#include <windows.h>

namespace {
// Fallback for SDK ids without embedded vid/pid: the SDK id is the leaf of a
// Windows instance path (USB\VID_0547&PID_14AD\<leaf>); match it against the
// registry and parse VID/PID from the parent key name. Leaves may carry a
// short numeric prefix ("6&74d72c68&0&18"), so accept exact and &-suffixed
// matches.
bool leafMatches(const QString &leaf, const QString &deviceId)
{
    return leaf.compare(deviceId, Qt::CaseInsensitive) == 0
        || leaf.endsWith(QStringLiteral("&") + deviceId, Qt::CaseInsensitive);
}

UsbIdentity registryUsbIdentity(const QString &deviceId)
{
    UsbIdentity found;
    HKEY usbRoot = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\USB",
                      0, KEY_ENUMERATE_SUB_KEYS, &usbRoot) != ERROR_SUCCESS)
        return found;
    wchar_t vidPidName[64]{};
    for (DWORD vidPidIndex = 0; found.vendorId.isEmpty(); ++vidPidIndex) {
        DWORD vidPidLen = 64;
        const LSTATUS vidPidStatus = RegEnumKeyExW(usbRoot, vidPidIndex, vidPidName, &vidPidLen,
                                                   nullptr, nullptr, nullptr, nullptr);
        if (vidPidStatus == ERROR_NO_MORE_ITEMS) break;
        if (vidPidStatus != ERROR_SUCCESS) continue;
        const QString keyName = QString::fromWCharArray(vidPidName, int(vidPidLen));
        const int vidAt = keyName.indexOf(QStringLiteral("VID_"), 0, Qt::CaseInsensitive);
        const int pidAt = keyName.indexOf(QStringLiteral("&PID_"), 0, Qt::CaseInsensitive);
        if (vidAt < 0 || pidAt < 0) continue;
        HKEY vidPidKey = nullptr;
        if (RegOpenKeyExW(usbRoot, vidPidName, 0, KEY_ENUMERATE_SUB_KEYS, &vidPidKey) != ERROR_SUCCESS)
            continue;
        wchar_t leaf[128]{};
        for (DWORD leafIndex = 0; ; ++leafIndex) {
            DWORD leafLen = 128;
            const LSTATUS leafStatus = RegEnumKeyExW(vidPidKey, leafIndex, leaf, &leafLen,
                                                     nullptr, nullptr, nullptr, nullptr);
            if (leafStatus == ERROR_NO_MORE_ITEMS) break;
            if (leafStatus != ERROR_SUCCESS) continue;
            if (!leafMatches(QString::fromWCharArray(leaf, int(leafLen)), deviceId)) continue;
            // USB IDs are fixed 4-hex-digit values: VID_0547&PID_14AD.
            found.vendorId = keyName.mid(vidAt + 4, 4);
            found.productId = keyName.mid(pidAt + 5, 4);
            break;
        }
        RegCloseKey(vidPidKey);
    }
    RegCloseKey(usbRoot);
    return found;
}
} // namespace
#endif

UsbIdentity usbIdentityForDeviceId(const QString &deviceId)
{
    if (deviceId.isEmpty()) return {};
    const UsbIdentity parsed = parseSdkUsbIdentity(deviceId);
    if (!parsed.vendorId.isEmpty()) return parsed;
#ifdef Q_OS_WIN
    UsbIdentity fromRegistry = registryUsbIdentity(deviceId);
    if (!fromRegistry.vendorId.isEmpty()) {
        fromRegistry.instanceId = deviceId; // no embedded tail: keep the SDK id
        return fromRegistry;
    }
#endif
    UsbIdentity unknown;
    unknown.instanceId = deviceId;
    return unknown;
}
