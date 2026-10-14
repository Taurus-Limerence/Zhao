#pragma once
#include <QObject>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>
#include <QUrl>
#include <memory>
class DeviceManager;
class CenteringController;
class AutoFocusController;
class SequenceController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY changed)
    Q_PROPERTY(int saved READ saved NOTIFY changed)
    Q_PROPERTY(int exposed READ exposed NOTIFY changed)
    Q_PROPERTY(int failed READ failed NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString preparationStatus READ preparationStatus NOTIFY changed)
    Q_PROPERTY(bool needsCentering READ needsCentering NOTIFY changed)
    Q_PROPERTY(bool needsAutofocus READ needsAutofocus NOTIFY changed)
public:
    explicit SequenceController(DeviceManager *devices, QObject *parent = nullptr, CenteringController *centering = nullptr,
                                AutoFocusController *autofocus = nullptr);
    ~SequenceController() override;
    QVariantMap plan() const { return m_plan; }
    QVariantList rows() const;
    QString state() const { return m_state; }
    QString phase() const { return m_phase; }
    bool active() const { return m_owned; }
    int currentRow() const { return m_row; }
    int saved() const { return m_saved; }
    int exposed() const { return m_exposed; }
    int failed() const { return m_failed; }
    int total() const;
    double progress() const;
    QString preparationStatus() const;
    bool needsCentering() const { return m_plan.value("centerBeforeCapture").toBool() && (!active() || !m_centered); }
    bool needsAutofocus() const { return m_plan.value("focusBeforeCapture").toBool() && (!active() || !m_focused); }
    Q_INVOKABLE void newPlan();
    Q_INVOKABLE void setHeader(const QString &key, const QVariant &value);
    Q_INVOKABLE void addRow();
    Q_INVOKABLE void removeRow(int index);
    Q_INVOKABLE void updateRow(int index, const QString &key, const QVariant &value);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void resetProgress();
    Q_INVOKABLE bool savePlan(const QUrl &url);
    Q_INVOKABLE bool loadPlan(const QUrl &url);
signals:
    void changed();
    void progressChanged();
    void notice(const QString &message);
private:
    QString validate(const QVariantMap &plan, bool hardware) const;
    void advance();
    void prepareAutofocus();
    void prepareFocusGain();
    void prepareChangedFilter();
    bool planFilterOffset(int filter);
    void moveFilterOffset();
    void stopFilterOffset(const QString &error = {});
    bool offsetFocuserStopped() const;
    void tick();
    void capture();
    void result(int role, const QString &path, bool ok);
    void suspend(const QString &message);
    void release(const QString &state);
    DeviceManager *m_devices;
    CenteringController *m_centering;
    AutoFocusController *m_autofocus;
    bool m_centered = false;
    bool m_focused = false;
    int m_focusRow = -1;
    std::shared_ptr<QString> m_focusGainError;
    int m_pendingFilterRow = -1, m_offsetTarget = -1, m_offsetStage = 0;
    int m_offsetMoveTarget = -1;
    bool m_filterAutofocus = false, m_offsetStopping = false;
    QString m_offsetError;
    QElapsedTimer m_offsetStable;
    QVariantMap m_plan;
    QVariantList m_rows;
    QList<int> m_counts;
    QString m_state = "Idle", m_phase;
    bool m_owned = false, m_inFlight = false;
    int m_row = -1, m_saved = 0, m_exposed = 0, m_failed = 0;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
};
