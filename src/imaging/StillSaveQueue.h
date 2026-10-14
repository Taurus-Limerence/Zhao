#pragma once
#include <QObject>
#include <QMutex>
#include <QMap>
#include <QDateTime>
#include <QVector>

struct StillRequest {
    int role = 1;
    QString directory;
    QString imageType = QStringLiteral("LIGHT");
    QString targetName;
    QDateTime capturedAt;
    QVector<quint16> data;
    int width = 0;
    int height = 0;
    QMap<QString, QString> header;
};

// Admission counts queued events AND the active write before posting frame data.
class StillSaveQueue : public QObject {
    Q_OBJECT
public:
    explicit StillSaveQueue(QObject *parent = nullptr) : QObject(parent) {}
    bool enqueue(StillRequest &&request);
    void finish(); // stop admission and drain accepted work, from outside the worker
    int pendingCount() const;
signals:
    void saved(int role, const QString &path, bool ok);
    void saveError(const QString &message);
private:
    mutable QMutex m_mutex;
    int m_pending = 0;
    qint64 m_bytes = 0;
    bool m_accepting = true;
    quint64 m_sequence = 0;
};
