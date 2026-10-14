#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QtMath>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QTimer>
#include "FocusMetrics.h"

class DeviceManager;
class AutoFocusController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList samples READ samples NOTIFY changed)
    Q_PROPERTY(QVariantList fitCurve READ fitCurve NOTIFY changed)
    Q_PROPERTY(int bestPosition READ bestPosition NOTIFY changed)
    Q_PROPERTY(double bestHfr READ bestHfr NOTIFY changed)
    Q_PROPERTY(double verifiedHfr READ verifiedHfr NOTIFY changed)
    Q_PROPERTY(double rSquared READ rSquared NOTIFY changed)
    Q_PROPERTY(int starCount READ starCount NOTIFY changed)
public:
    explicit AutoFocusController(DeviceManager *devices,QObject *parent=nullptr);
    ~AutoFocusController() override;
    QVariantMap settings() const { return m_settings; }
    bool busy() const { return m_phase!=Idle; }
    bool succeeded() const { return m_succeeded; }
    QString status() const { return m_status; }
    QVariantList samples() const;
    QVariantList fitCurve() const { return m_fitCurve; }
    int bestPosition() const { return m_bestPosition; }
    double bestHfr() const { return m_bestHfr; }
    double verifiedHfr() const { return m_verifiedHfr; }
    double rSquared() const { return m_rSquared; }
    int starCount() const { return m_starCount; }
    Q_INVOKABLE bool start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setSetting(const QString &key,const QVariant &value);
signals:
    void changed();
    void settingsChanged();
    void notice(const QString &message);
private:
    friend class SequenceController;
    bool startForSequence();
    QString validationError(bool forSequence=false) const;
    enum Phase { Idle,Moving,Settling,Capturing,Analyzing,Measured,Stopping };
    enum MovePurpose { ScanApproach,ScanPoint,BestApproach,BestPoint,ReturnOriginal };
    void tick();
    void move(int position,MovePurpose purpose);
    void capture();
    void acceptMeasurement();
    void stop(const QString &reason,bool returnToOriginal);
    void finish(bool success,const QString &message);
    bool cameraSettled() const;
    bool focuserStopped() const;
    DeviceManager *m_devices;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    QFutureWatcher<FocusMetrics::Measurement> m_analysis;
    QVariantMap m_settings{{"step",80},{"points",9},{"exposure",3.0},{"frames",1},{"backlash",0}};
    QVector<FocusMetrics::Sample> m_samples;
    QVector<FocusMetrics::Measurement> m_frames;
    QVariantList m_fitCurve;
    Phase m_phase=Idle;
    MovePurpose m_movePurpose=ScanPoint;
    QString m_status,m_stopReason;
    bool m_succeeded=false,m_returnToOriginal=false,m_sequenceRun=false;
    unsigned m_originalExposure=0;
    int m_original=0,m_first=0,m_target=0,m_bestPosition=-1,m_starCount=0;
    double m_bestHfr=qQNaN(),m_verifiedHfr=qQNaN(),m_rSquared=qQNaN();
};
