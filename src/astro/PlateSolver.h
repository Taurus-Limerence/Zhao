#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <QUrl>
#include <QVariantMap>
#include <QSize>
#include <atomic>
#include <memory>

namespace PlateSolveJob {
QVariantMap run(const QString &executable, const QString &file, const QVector<quint16> &pixels,
                QSize size, double fov, const std::shared_ptr<std::atomic_bool> &cancel,
                int timeoutMs = 120000);
}

class PlateSolver : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString executable READ executable WRITE setExecutable NOTIFY executableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool currentAvailable READ currentAvailable NOTIFY currentChanged)
    Q_PROPERTY(QString currentLabel READ currentLabel NOTIFY currentChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
public:
    explicit PlateSolver(QObject *parent = nullptr);
    ~PlateSolver() override;
    QString executable() const { return m_executable; }
    void setExecutable(const QString &path);
    bool busy() const { return m_busy; }
    bool currentAvailable() const { return !m_pixels.isEmpty(); }
    QString currentLabel() const { return m_currentLabel; }
    QString status() const { return m_status; }
    QVariantMap result() const { return m_result; }
    void setCurrentFrame(QVector<quint16> pixels, int width, int height, const QVariantMap &metadata);
    Q_INVOKABLE bool solveFile(const QUrl &file);
    Q_INVOKABLE bool solveCurrent(double fov);
    Q_INVOKABLE bool captureAndSolve(double seconds, double fov);
    void acceptCapture(QVector<quint16> pixels, int width, int height, const QVariantMap &metadata);
    void failCapture(const QString &message);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void currentChanged();
    void executableChanged();
    void notice(const QString &message);
    void captureRequested(double seconds);
    void cancelCaptureRequested();
private:
    friend class CenteringController;
    bool m_centeringOwned = false;
    bool m_centeringDispatch = false;
    bool start(const QString &file, double fov);
    QString m_executable, m_currentLabel, m_source;
    QString m_status = QStringLiteral("未解析");
    bool m_busy = false;
    bool m_waitingCapture = false;
    double m_captureFov = 0;
    QVector<quint16> m_pixels;
    QSize m_size;
    QVariantMap m_result;
    QFutureWatcher<QVariantMap> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
