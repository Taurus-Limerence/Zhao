#include <QtTest>
#include "devices/ToupcamParams.h"
#include "devices/UsbIdentity.h"

class CameraParamsTest final : public QObject
{
    Q_OBJECT
private slots:
    void tecTargetRangeDecodesSignedHalves() {
        // Min -35 (-3.5 °C) in the low half, max 500 (50.0 °C) in the high half.
        const unsigned packed = (unsigned(static_cast<unsigned short>(500)) << 16)
                              | unsigned(static_cast<unsigned short>(-35));
        QCOMPARE(CamParams::tecTargetRangeMin(packed), -35);
        QCOMPARE(CamParams::tecTargetRangeMax(packed), 500);
    }
    void tecTargetRangeKeepsNegativeMaxNegative() {
        const unsigned packed = (unsigned(static_cast<unsigned short>(-100)) << 16)
                              | unsigned(static_cast<unsigned short>(-400));
        QCOMPARE(CamParams::tecTargetRangeMin(packed), -400);
        QCOMPARE(CamParams::tecTargetRangeMax(packed), -100);
    }
    void binningEncodesSaturatingAdd() {
        QCOMPARE(CamParams::binningOptionValue(1), 0x01);
        QCOMPARE(CamParams::binningOptionValue(2), 0x02);
        QCOMPARE(CamParams::binningOptionValue(3), 0x03);
        QCOMPARE(CamParams::binningOptionValue(4), 0x04);
        QCOMPARE(CamParams::binningOptionValue(8), 0x08);
        QCOMPARE(CamParams::binningOptionValue(0), 0x01);
        QCOMPARE(CamParams::binningOptionValue(-2), 0x01);
        QCOMPARE(CamParams::binningOptionValue(99), 0x08);
    }
    void binningDecodesKnownEncodings() {
        QCOMPARE(CamParams::binningFactor(0x01), 1);
        QCOMPARE(CamParams::binningFactor(0x04), 4);   // saturating add
        QCOMPARE(CamParams::binningFactor(0x44), 4);   // RAW add
        QCOMPARE(CamParams::binningFactor(0x84), 4);   // average
        QCOMPARE(CamParams::binningFactor(0x10), 1);   // unknown
        QCOMPARE(CamParams::binningFactor(0), 1);
    }
    void blackLevelMaxScalesWithBitDepth() {
        QCOMPARE(CamParams::blackLevelMaxForBitDepth(8), 31);
        QCOMPARE(CamParams::blackLevelMaxForBitDepth(10), 124);
        QCOMPARE(CamParams::blackLevelMaxForBitDepth(12), 496);
        QCOMPARE(CamParams::blackLevelMaxForBitDepth(14), 1984);
        QCOMPARE(CamParams::blackLevelMaxForBitDepth(16), 7936);
        QCOMPARE(CamParams::blackLevelMaxForBitDepth(0), 31);
        QCOMPARE(CamParams::blackLevelMaxForBitDepth(20), 7936);
    }
    void tecPowerPercentStaysWithinBounds() {
        QCOMPARE(CamParams::tecPowerPercent(0, 100), 0.0);
        QCOMPARE(CamParams::tecPowerPercent(59, 100), 59.0);
        QCOMPARE(CamParams::tecPowerPercent(150, 100), 100.0);
        QCOMPARE(CamParams::tecPowerPercent(-5, 100), 0.0);
        QCOMPARE(CamParams::tecPowerPercent(59, 0), 0.0);
    }
    void temperatureConversionsPreserveTenths() {
        QCOMPARE(CamParams::temperatureToTenths(-10.0), -100);
        QCOMPARE(CamParams::temperatureToTenths(-5.5), -55);
        QCOMPARE(CamParams::temperatureToTenths(0.0), 0);
        QCOMPARE(CamParams::temperatureToTenths(12.5), 125);
        QCOMPARE(CamParams::tenthsToCelsius(-350), -35.0);
        QCOMPARE(CamParams::tenthsToCelsius(125), 12.5);
    }
    void clampedBoundsValues() {
        QCOMPARE(CamParams::clamped(5, 0, 10), 5);
        QCOMPARE(CamParams::clamped(-1, 0, 10), 0);
        QCOMPARE(CamParams::clamped(11, 0, 10), 10);
        QCOMPARE(CamParams::clamped(150u, 100u, 500u), 150u);
        QCOMPARE(CamParams::clamped(50u, 100u, 500u), 100u);
    }
    void usbIdentityParsesNinaStyleSdkId() {
        // NINA 同款规则（ToupTekAlikeFocuser.cs）：vid_(hex)&pid_(hex)#(尾段)
        const UsbIdentity parsed = parseSdkUsbIdentity(
            QStringLiteral("vid_0547&pid_14ad#5&274d72c6&0&6"));
        QCOMPARE(parsed.vendorId, QStringLiteral("0547"));
        QCOMPARE(parsed.productId, QStringLiteral("14ad"));
        QCOMPARE(parsed.instanceId, QStringLiteral("5&274d72c6&0&6"));
        const UsbIdentity upper = parseSdkUsbIdentity(
            QStringLiteral("VID_0547&PID_678C#74d72c68&0&18"));
        QCOMPARE(upper.vendorId, QStringLiteral("0547"));
        QCOMPARE(upper.productId, QStringLiteral("678C"));
        QCOMPARE(upper.instanceId, QStringLiteral("74d72c68&0&18"));
        // 无 vid/pid 内嵌的 ID（实例尾段形态）解析失败，各字段为空。
        const UsbIdentity bare = parseSdkUsbIdentity(QStringLiteral("74d72c68&0&18"));
        QVERIFY(bare.vendorId.isEmpty());
        QVERIFY(bare.productId.isEmpty());
        QVERIFY(bare.instanceId.isEmpty());
    }
};

QTEST_GUILESS_MAIN(CameraParamsTest)
#include "tst_cameraparams.moc"
