#include "StillSaveQueue.h"
#include "FitsWriter.h"
#include <QDir>
#include <QRegularExpression>
#include <QUuid>
#include <QThread>
#include <QElapsedTimer>

bool StillSaveQueue::enqueue(StillRequest &&request)
{
    const qint64 bytes = qint64(request.data.size()) * 2;
    QMutexLocker lock(&m_mutex);
    if (!m_accepting || m_pending >= 4 || bytes > 512LL * 1024 * 1024 - m_bytes)
        return false;
    ++m_pending;
    m_bytes += bytes;
    QMetaObject::invokeMethod(this, [this, request = std::move(request), bytes]() mutable {
        QString target = request.targetName.left(80);
        target.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), "_");
        if (target.isEmpty()) target = QStringLiteral("Untitled");
        const auto stamp = request.capturedAt.isValid() ? request.capturedAt : QDateTime::currentDateTimeUtc();
        const QString name = QStringLiteral("StellaMora_%1_%2_%3_%4_%5.fits")
            .arg(target, request.imageType, QString::number(++m_sequence),
                 stamp.toUTC().toString("yyyyMMdd_HHmmss_zzz"), QUuid::createUuid().toString(QUuid::Id128));
        const QString path = QDir(request.directory).filePath(name);
        request.header.insert("IMAGETYP", request.imageType);
        bool ok = false;
        QElapsedTimer elapsed;
        elapsed.start();
        const bool valid = !request.directory.isEmpty() && request.width > 0 && request.height > 0
            && qint64(request.width) * request.height == request.data.size()
            && QStringList{"LIGHT", "DARK", "FLAT", "BIAS"}.contains(request.imageType);
        if (valid && QDir().mkpath(request.directory)) {
            // Three total attempts; a slow write trips admission once it returns.
            for (int attempt = 0; attempt < 3; ++attempt) {
                ok = FitsWriter::writeMono16(path, request.data, request.width, request.height, request.header);
                if (ok || elapsed.elapsed() >= 30000) break;
                if (attempt < 2) QThread::msleep(1000);
            }
        }
        {
            QMutexLocker lock(&m_mutex);
            --m_pending;
            m_bytes -= bytes;
            if (elapsed.elapsed() >= 30000) m_accepting = false;
        }
        if (!ok) emit saveError(QStringLiteral("FITS保存失败：%1（参数、目录或写盘失败；最多尝试3次）").arg(path));
        if (elapsed.elapsed() >= 30000) emit saveError(QStringLiteral("写盘耗时超过30秒，已停止接收新保存请求，请检查磁盘后重启软件"));
        emit saved(request.role, ok ? path : QString(), ok);
    }, Qt::QueuedConnection);
    return true;
}

int StillSaveQueue::pendingCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_pending;
}

void StillSaveQueue::finish()
{
    { QMutexLocker lock(&m_mutex); m_accepting = false; }
    // FIFO barrier, posted after all accepted writes. Never terminate a writer.
    QMetaObject::invokeMethod(this, [] {}, Qt::BlockingQueuedConnection);
}
