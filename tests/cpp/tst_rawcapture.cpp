#include <QtTest>
#include "devices/ToupcamCamera.h"

namespace {
#ifdef Q_OS_WIN
#define SDKCALL __stdcall
#else
#define SDKCALL
#endif
QMap<unsigned, int> options;
bool deliverFrame = true;
bool rejectTrigger = false;
PTOUPCAM_EVENT_CALLBACK eventCallback = nullptr;
void *eventContext = nullptr;
QList<int> triggers;
int starts = 0, stops = 0;
unsigned short gainReadback = 173, gainWritten = 0;
bool rejectGainRead = false, rejectGainWrite = false;
int gainReads = 0;
HRESULT SDKCALL readGain(HToupcam, unsigned short *value) {
    ++gainReads; *value=gainReadback; return rejectGainRead ? E_FAIL : S_OK;
}
HRESULT SDKCALL writeGain(HToupcam, unsigned short value) {
    gainWritten=value; return rejectGainWrite ? E_FAIL : S_OK;
}
HRESULT SDKCALL getOption(HToupcam, unsigned key, int *value) { *value=options.value(key); return 0; }
HRESULT SDKCALL putOption(HToupcam, unsigned key, int value) { options[key]=value; return 0; }
HRESULT SDKCALL getAuto(HToupcam, int *value) { *value=0; return 0; }
HRESULT SDKCALL putAuto(HToupcam, int) { return 0; }
HRESULT SDKCALL getSize(HToupcam, unsigned *value) { *value=0; return 0; }
HRESULT SDKCALL putSize(HToupcam, unsigned) { return 0; }
HRESULT SDKCALL rawFormat(HToupcam, unsigned *code, unsigned *bits) { *code=0; *bits=16; return 0; }
HRESULT SDKCALL exposure(HToupcam, unsigned *value) { *value=10000; return 0; }
HRESULT SDKCALL gain(HToupcam, unsigned short *value) { *value=100; return 0; }
HRESULT SDKCALL temperature(HToupcam, short *value) { *value=200; return 0; }
HRESULT SDKCALL stop(HToupcam) { ++stops; return 0; }
void SDKCALL closeCamera(HToupcam) {}
HRESULT SDKCALL start(HToupcam, PTOUPCAM_EVENT_CALLBACK callback, void *context) {
    eventCallback=callback; eventContext=context;
    ++starts;
    return 0;
}
HRESULT SDKCALL trigger(HToupcam, unsigned short count) {
    triggers.append(count);
    if (count && rejectTrigger) return E_FAIL;
    if (count && options.value(TOUPCAM_OPTION_TRIGGER)!=1) return E_FAIL;
    if (count && deliverFrame) eventCallback(TOUPCAM_EVENT_IMAGE,eventContext);
    return 0;
}
HRESULT SDKCALL pull(HToupcam, void *buffer, int, int, int, ToupcamFrameInfoV3 *info) {
    info->width=2; info->height=1;
    auto *pixels=static_cast<quint16 *>(buffer); pixels[0]=123; pixels[1]=456;
    return 0;
}
}

class RawCaptureTest : public QObject {
    Q_OBJECT
private slots:
    void gainPublishesActualReadback_data() {
        QTest::addColumn<int>("actual");
        QTest::addColumn<bool>("readFails");
        QTest::addColumn<bool>("writeFails");
        QTest::newRow("normal") << 173 << false << false;
        QTest::newRow("mismatch") << 170 << false << false;
        QTest::newRow("get-failure") << 173 << true << false;
        QTest::newRow("put-failure") << 173 << false << true;
    }
    void gainPublishesActualReadback() {
        QFETCH(int, actual);
        QFETCH(bool, readFails);
        QFETCH(bool, writeFails);
        gainReadback=static_cast<unsigned short>(actual); gainWritten=0; gainReads=0;
        rejectGainRead=readFails; rejectGainWrite=writeFails;
        ToupcamCamera camera;
        camera.m_handle=reinterpret_cast<HToupcam>(quintptr(1));
        camera.m_putExpoAGain=&writeGain; camera.m_getExpoAGain=&readGain;
        camera.m_close=&closeCamera;
        QSignalSpy states(&camera,&CameraBackend::stateChanged);
        QSignalSpy errors(&camera,&CameraBackend::commandFailed);
        camera.setGain(173);
        QCOMPARE(gainWritten,173);
        QCOMPARE(gainReads,writeFails ? 0 : 1);
        QCOMPARE(errors.size(),readFails || writeFails ? 1 : 0);
        QCOMPARE(states.size(),readFails || writeFails ? 0 : 1);
        if (!states.isEmpty()) QCOMPARE(states[0][0].toMap().value("gain").toUInt(),unsigned(actual));
    }
    void sensorDimensionsUseNativePixelsAndPitch() {
        ToupcamCamera camera;
        ToupcamModelV2 model{};
        model.res[0].width = 3840; model.res[0].height = 2160;
        model.xpixsz = 2.0f; model.ypixsz = 3.0f;
        ToupcamDeviceV2 device{}; device.model = &model;
        const auto info = camera.describeDevice(device);
        QCOMPARE(info.value("sensorWidthMm").toDouble(), 7.68);
        QCOMPARE(info.value("sensorHeightMm").toDouble(), 6.48);
    }
    void shortExposureFromTriggerMode_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("reject");
        QTest::newRow("continuous") << 0 << false << false;
        QTest::newRow("software") << 1 << false << false;
        QTest::newRow("external") << 2 << false << false;
        QTest::newRow("cancel") << 1 << true << false;
        QTest::newRow("trigger-failure") << 1 << false << true;
    }
    void shortExposureFromTriggerMode() {
        QFETCH(int, mode);
        QFETCH(bool, cancel);
        QFETCH(bool, reject);
        rejectTrigger = reject;
        deliverFrame = !cancel;
        triggers.clear();
        starts = stops = 0;
        ToupcamCamera camera;
        camera.m_handle=reinterpret_cast<HToupcam>(quintptr(1));
        camera.m_flags=TOUPCAM_FLAG_MONO | TOUPCAM_FLAG_TRIGGER_SOFTWARE;
        camera.m_sensorWidth=2; camera.m_sensorHeight=1;
        camera.m_getOption=&getOption; camera.m_putOption=&putOption;
        camera.m_getAutoExpoEnable=&getAuto; camera.m_putAutoExpoEnable=&putAuto;
        camera.m_getEsize=&getSize; camera.m_putEsize=&putSize;
        camera.m_getRawFormat=&rawFormat; camera.m_getExpoTime=&exposure;
        camera.m_getExpoAGain=&gain; camera.m_getTemperature=&temperature;
        camera.m_startPullMode=&start; camera.m_stop=&stop;
        camera.m_trigger=&trigger;
        camera.m_close=&closeCamera; camera.m_pullImageV3=&pull;
        options={{TOUPCAM_OPTION_TRIGGER,mode},{TOUPCAM_OPTION_RAW,0},
                 {TOUPCAM_OPTION_BITDEPTH,0},{TOUPCAM_OPTION_BINNING,1}};
        QSignalSpy frames(&camera,&CameraBackend::rawStillReady);
        QSignalSpy errors(&camera,&CameraBackend::stillFailed);
        camera.requestStill();
        if (!reject) QCOMPARE(triggers,QList<int>({1}));
        if (cancel || reject) {
            if (cancel) camera.stopPreview();
            // A late image after cancellation must not be delivered as a new still.
            eventCallback(TOUPCAM_EVENT_IMAGE,eventContext);
            QCoreApplication::processEvents();
            QCOMPARE(errors.count(),1);
            QCOMPARE(frames.count(),0);
            QCOMPARE(triggers,QList<int>({1,0}));
            rejectTrigger=false; deliverFrame=true;
            camera.requestStill();
            QTRY_COMPARE_WITH_TIMEOUT(frames.count(),1,1000);
            QCOMPARE(starts,2);
            camera.stopPreview();
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(frames.count(),1,1000);
            QCOMPARE(errors.count(),0);
            QCOMPARE(frames[0][0].value<QVector<quint16>>(),QVector<quint16>({123,456}));
            camera.requestStill();
            QTRY_COMPARE_WITH_TIMEOUT(frames.count(),2,1000);
            QCOMPARE(triggers,QList<int>({1,1}));
            QCOMPARE(starts,1);
            QCOMPARE(stops,0);
            camera.startPreview(true);
            QCOMPARE(starts,2);
            QCOMPARE(stops,1);
            camera.stopPreview();
        }
        QCOMPARE(options.value(TOUPCAM_OPTION_TRIGGER),mode);
        QCOMPARE(options.value(TOUPCAM_OPTION_RAW),0);
        QCOMPARE(options.value(TOUPCAM_OPTION_BITDEPTH),0);
        QVERIFY(!camera.m_stillTimer->isActive());
        QVERIFY(!camera.m_stillPending);
    }
};
QTEST_MAIN(RawCaptureTest)
#include "tst_rawcapture.moc"
