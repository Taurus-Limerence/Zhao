#pragma once

#include <QImage>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// All operations run on the owning worker thread. No UI or camera handles cross it.
class CameraBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void scan() = 0;
    virtual void open(const QString &id) = 0;
    virtual void close() = 0;
    // Exposure is microseconds; gain is the analog gain percent (100 = 1x);
    // offset is the black level; binning is the n of n*n (1 disables it).
    // Target temperature is °C; fan speed -1 = model default, 0 = off, [1, max];
    // heat level runs 0..max; gain mode: 0 = LCG, 1 = HCG, 2 = HDR.
    virtual void setExposure(unsigned microseconds) = 0;
    virtual void setGain(unsigned gain) = 0;
    virtual void setOffset(int offset) = 0;
    virtual void setBinning(int factor) = 0;
    virtual void setCoolerEnabled(bool enabled) = 0;
    virtual void setTargetTemperature(double celsius) = 0;
    virtual void setFanSpeed(int speed) = 0;
    virtual void setHeatLevel(int level) = 0;
    virtual void setGainMode(int mode) = 0;
    virtual void setLowNoise(bool enabled) = 0;
    // Live preview: continuous=true loops exposures until stopPreview();
    // continuous=false takes a single exposure and stops automatically.
    virtual void startPreview(bool continuous) = 0;
    virtual void stopPreview() = 0;
    // Resolution index into the "resolutions" list reported in the connection
    // information; unsupported backends keep the default no-op.
    virtual void setResolution(int index) { Q_UNUSED(index); }
    // Arm the next frame to be delivered as full-resolution 16-bit raw via
    // rawStillReady; the save pipeline lives above this layer.
    virtual void requestStill() { emit stillFailed(QStringLiteral("相机后端未实现RAW截存")); }
signals:
    void rawStillReady(QVector<quint16> data, int width, int height, QVariantMap metadata);
    void stillFailed(const QString &message);
    void scanFinished(const QVariantList &cameras, const QString &error);
    void connectionFinished(bool connected, const QVariantMap &information, const QString &error);
    void connectionLost(const QString &error);
    // state keys: exposureUs/exposureMinUs/exposureMaxUs, gain/gainMin/gainMax,
    // offset/offsetMax, binning, tecEnabled/canTec/targetTemp/tempMin/tempMax,
    // currentTemp/canTemp/tecPower, fanSpeed/fanMax/canFan, heatLevel/heatMax/canHeat,
    // gainMode/canGainMode, lowNoise/canLowNoise, previewing. Temperatures are °C
    // doubles, everything else integral; canXxx flags gate the matching controls.
    void stateChanged(const QVariantMap &state);
    // A parameter write the camera refused; transient, unlike connectionLost.
    void commandFailed(const QString &message);
    // Downscaled RGB frame from the live preview, emitted on the SDK thread.
    void frameReady(const QImage &frame);
    // Full-resolution 16-bit grey frame for guiding analytics (guide mode).
    // Display previews must use frameReady; guiding math uses this.
    void rawFrameReady(QVector<quint16> data, int width, int height, double exposureSeconds);
};
