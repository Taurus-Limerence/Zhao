#pragma once

#include "CameraBackend.h"
#include <QLibrary>
#include <QTimer>
#include <vector>
#include <atomic>
#include <toupcam.h>

class ToupcamCamera final : public CameraBackend
{
public:
    ToupcamCamera();
    ~ToupcamCamera() override;
    void scan() override;
    void open(const QString &id) override;
    void close() override;
    void setExposure(unsigned microseconds) override;
    void setGain(unsigned gain) override;
    void setOffset(int offset) override;
    void setBinning(int factor) override;
    void setCoolerEnabled(bool enabled) override;
    void setTargetTemperature(double celsius) override;
    void setFanSpeed(int speed) override;
    void setHeatLevel(int level) override;
    void setGainMode(int mode) override;
    void setLowNoise(bool enabled) override;
    void startPreview(bool continuous) override;
    void stopPreview() override;
    void setResolution(int index) override;
    void requestStill() override;
    // Guide mode: exposures deliver full-resolution 16-bit grey frames via
    // rawFrameReady (plus a dimmed preview via frameReady) instead of the
    // downscaled RGB stream used for the main-camera display.
    void setGuideMode(bool enabled) { m_guideMode = enabled; }
private:
    friend class RawCaptureTest;
    bool loadSdk(QString &error);
    unsigned listDevices(ToupcamDeviceV2 *devices) const;
    QVariantMap describeDevice(const ToupcamDeviceV2 &device) const;
    void pinEngineConventions();
    QVariantList enumerate();
    void checkPresence();
    void queryRanges();
    void refreshState();
    QVariantMap pollTemperature();
    void pollTemperatureTick();
    void release();
    void processEvent(unsigned event);
    void finishStill(const QString &error = {});
    bool endRawSession();
    QImage downscaledFrame(const unsigned char *data, unsigned width, unsigned height) const;
    QImage guidePreviewFrame(const unsigned char *data, unsigned width, unsigned height) const;
#ifdef _WIN32
    static void __stdcall onEvent(unsigned nEvent, void *ctx);
#else
    static void onEvent(unsigned nEvent, void *ctx);
#endif

    bool tecSupported() const { return m_flags & TOUPCAM_FLAG_TEC_ONOFF; }
    bool temperatureSupported() const { return m_flags & TOUPCAM_FLAG_GETTEMPERATURE; }
    bool fanSupported() const { return m_flags & TOUPCAM_FLAG_FAN; }
    bool heatSupported() const { return m_flags & TOUPCAM_FLAG_HEAT; }
    bool gainModeSupported() const { return m_flags & TOUPCAM_FLAG_CG; }
    bool blackLevelSupported() const { return m_flags & TOUPCAM_FLAG_BLACKLEVEL; }
    bool lowNoiseSupported() const { return m_flags & TOUPCAM_FLAG_LOW_NOISE; }

    QLibrary m_library;
    QTimer *m_presenceTimer;
    QTimer *m_pollTimer;
    HToupcam m_handle = nullptr;
    QString m_id;
    int m_missingScans = 0;
    quint64 m_flags = 0;
    // Cached limits so writes can be clamped without a round trip per command.
    unsigned m_exposureMinUs = 100;
    unsigned m_exposureMaxUs = 3600000000u;
    unsigned m_gainMin = 100;
    unsigned m_gainMax = 10000;
    int m_offsetMax = -1;
    int m_tempMinTenths = -500;
    int m_tempMaxTenths = 300;
    int m_fanMax = -1;
    int m_heatMax = 100;
    double m_reportedTemp = std::numeric_limits<double>::quiet_NaN();
    double m_reportedPower = std::numeric_limits<double>::quiet_NaN();
    bool m_previewing = false;
    bool m_singleShot = false;
    bool m_guideMode = false;
    // 分辨率表（连接时从 model->res 构建）与单帧截存状态。
    int m_resolutionCount = 0;
    QVector<QSize> m_resolutions;
    std::vector<unsigned char> m_stillBuffer;
    bool m_stillPending = false;
    bool m_rawSession = false;
    int m_savedQueueLength = 0;
    bool m_resumePreview = false;
    int m_savedRaw = 0, m_savedDepth = 0, m_savedBinning = 1, m_savedAutoExpo = 0;
    int m_savedTrigger = -1;
    unsigned m_savedResolution = 0;
    QVariantMap m_stillMetadata;
    QTimer *m_stillTimer = nullptr;
    std::atomic<unsigned> m_generation{0};
    std::vector<unsigned char> m_rawBuffer;
    unsigned m_sensorWidth = 0;
    unsigned m_sensorHeight = 0;
    std::vector<unsigned char> m_previewBuffer;
    decltype(&Toupcam_EnumV2) m_enum = nullptr;
    decltype(&Toupcam_Open) m_open = nullptr;
    decltype(&Toupcam_Close) m_close = nullptr;
    decltype(&Toupcam_get_SerialNumber) m_serial = nullptr;
    decltype(&Toupcam_Version) m_version = nullptr;
    decltype(&Toupcam_get_ExpoTime) m_getExpoTime = nullptr;
    decltype(&Toupcam_put_ExpoTime) m_putExpoTime = nullptr;
    decltype(&Toupcam_get_ExpTimeRange) m_getExpTimeRange = nullptr;
    decltype(&Toupcam_get_ExpoAGain) m_getExpoAGain = nullptr;
    decltype(&Toupcam_put_ExpoAGain) m_putExpoAGain = nullptr;
    decltype(&Toupcam_get_ExpoAGainRange) m_getExpoAGainRange = nullptr;
    decltype(&Toupcam_put_Option) m_putOption = nullptr;
    decltype(&Toupcam_get_Option) m_getOption = nullptr;
    decltype(&Toupcam_get_Temperature) m_getTemperature = nullptr;
    decltype(&Toupcam_get_FanMaxSpeed) m_fanMaxSpeed = nullptr;
    decltype(&Toupcam_get_MaxBitDepth) m_maxBitDepth = nullptr;
    decltype(&Toupcam_StartPullModeWithCallback) m_startPullMode = nullptr;
    decltype(&Toupcam_Stop) m_stop = nullptr;
    decltype(&Toupcam_Trigger) m_trigger = nullptr;
    decltype(&Toupcam_PullImageV3) m_pullImageV3 = nullptr;
    decltype(&Toupcam_put_AutoExpoEnable) m_putAutoExpoEnable = nullptr;
    decltype(&Toupcam_put_eSize) m_putEsize = nullptr;
    decltype(&Toupcam_get_eSize) m_getEsize = nullptr;
    decltype(&Toupcam_get_RawFormat) m_getRawFormat = nullptr;
    decltype(&Toupcam_get_AutoExpoEnable) m_getAutoExpoEnable = nullptr;
};
