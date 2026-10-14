#pragma once

#include <QRegularExpression>
#include <QString>

// USB identity for a Toupcam SDK device id. On Windows the SDK hands out ids
// like "vid_0547&pid_14ad#5&274d72c6&0&6" — the same string NINA regex-parses
// (ToupTekAlikeFocuser.cs IdExtractorRegex) for its Vendor/Product/Device
// display; the tail after '#' is the Windows instance leaf.
struct UsbIdentity
{
    QString vendorId;   // "0547" when known
    QString productId;  // "14ad" when known
    QString instanceId; // tail after '#'; the SDK id verbatim when unknown
};

// NINA-compatible parser: vid_(hex)&pid_(hex)#(tail), case-insensitive. Pure
// and header-only so the parsing rule stays unit-testable without hardware.
inline UsbIdentity parseSdkUsbIdentity(const QString &deviceId)
{
    static const QRegularExpression pattern(
        QStringLiteral("vid_([0-9a-fA-F]+)&pid_([0-9a-fA-F]+)#([^\\\\]+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(deviceId);
    if (!match.hasMatch()) return {};
    return {match.captured(1), match.captured(2), match.captured(3)};
}

// Full resolution: SDK id parse first, Windows registry walk as fallback for
// ids that do not embed vid/pid. Empty vendorId/productId mean "unknown";
// instanceId then carries the SDK id unchanged.
UsbIdentity usbIdentityForDeviceId(const QString &deviceId);
