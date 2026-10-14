#include "PlateSolver.h"
#include "PlateSolveResult.h"
#include "imaging/FitsWriter.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

QVariantMap PlateSolveJob::run(const QString &executable, const QString &file,
    const QVector<quint16> &pixels, QSize size, double fov,
    const std::shared_ptr<std::atomic_bool> &cancel, int timeoutMs)
{
    auto fail = [](QString message) -> QVariantMap { return {{"error",message}}; };
    if (cancel->load()) return fail(QStringLiteral("解析已取消"));
    if (!QFileInfo(executable).isFile()) return fail(QStringLiteral("请先选择 ASTAP 可执行文件，并安装星表"));
    QTemporaryDir workspace(QDir::tempPath()+"/StellaMora-solve-XXXXXX");
    if (!workspace.isValid()) return fail(QStringLiteral("无法创建解析临时目录"));
    const QString input = workspace.filePath("image.fits");
    if (!file.isEmpty()) {
        QString error;
        size = PlateSolveResult::fitsSize(file,&error);
        if (!size.isValid()) return fail(error);
        if (!QFile::copy(file,input)) return fail(QStringLiteral("无法复制 FITS 到解析临时目录"));
    } else if (!FitsWriter::writeMono16(input,pixels,size.width(),size.height())) {
        return fail(QStringLiteral("无法准备当前 RAW 照片"));
    }
    if (cancel->load()) return fail(QStringLiteral("解析已取消"));
    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    process.setProcessChannelMode(QProcess::MergedChannels);
    // Blind search avoids treating the selected catalogue target as measured position.
    process.start(executable,{"-f",input,"-r","180","-fov",QString::number(fov,'g',10),"-z","0"});
    if (!process.waitForStarted(3000)) return fail(QStringLiteral("无法启动 ASTAP：")+process.errorString());
    QElapsedTimer timer; timer.start();
    while (!process.waitForFinished(50)) {
        process.readAll();
        if (cancel->load() || timer.elapsed() >= timeoutMs) {
            process.kill(); process.waitForFinished(3000);
            return fail(cancel->load() ? QStringLiteral("解析已取消") : QStringLiteral("解析超时，请检查星表、星点及视场参数"));
        }
    }
    if (cancel->load()) return fail(QStringLiteral("解析已取消"));
    QFile output(workspace.filePath("image.ini"));
    if (!output.open(QIODevice::ReadOnly) || output.size()>1024*1024)
        return fail(QStringLiteral("ASTAP 未生成有效结果（退出码 %1），请检查星表安装").arg(process.exitCode()));
    QString error;
    auto result = PlateSolveResult::fromAstap(output.readAll(),size,&error);
    if (result.isEmpty()) return fail(error);
    if (process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0)
        return fail(QStringLiteral("ASTAP 异常退出（%1），未采用结果").arg(process.exitCode()));
    return result;
}

PlateSolver::PlateSolver(QObject *parent) : QObject(parent) {
    m_executable = QSettings().value("plateSolver/executable").toString();
    if (m_executable.isEmpty()) {
        m_executable = QStandardPaths::findExecutable("astap_cli");
        if (m_executable.isEmpty()) m_executable = QStandardPaths::findExecutable("astap");
    }
    connect(&m_watcher,&QFutureWatcher<QVariantMap>::finished,this,[this] {
        const auto result = m_watcher.result();
        m_busy = false;
        if (m_cancel->load() || result.contains("error")) {
            m_status = m_cancel->load() ? QStringLiteral("解析已取消") : QStringLiteral("解析失败");
            emit notice(m_cancel->load() ? m_status : result.value("error").toString());
        } else {
            m_result = result;
            m_result.insert("source",m_source);
            m_result.insert("solvedAt",QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
            m_status = QStringLiteral("解析成功 · J2000");
            if (!m_centeringOwned) emit notice(QStringLiteral("照片解析完成；未移动或同步赤道仪")
                +(result.value("warning").toString().isEmpty() ? QString() : "\n"+result.value("warning").toString()));
        }
        emit changed();
    });
}
PlateSolver::~PlateSolver() {
    if (m_cancel) m_cancel->store(true);
    m_watcher.waitForFinished();
}
void PlateSolver::setExecutable(const QString &path) {
    if (m_busy || m_centeringOwned) return;
    const QUrl url(path);
    m_executable = url.isLocalFile() ? url.toLocalFile() : path;
    QSettings().setValue("plateSolver/executable",m_executable);
    emit executableChanged();
}
void PlateSolver::setCurrentFrame(QVector<quint16> pixels, int width, int height, const QVariantMap &metadata) {
    if (width<=0 || height<=0 || qint64(width)*height!=pixels.size()) return;
    m_pixels = std::move(pixels); m_size = {width,height};
    QString captured = metadata.value("DATE-OBS").toString();
    if (captured.isEmpty()) captured = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_currentLabel = QStringLiteral("主相机 RAW · %1").arg(captured);
    emit currentChanged();
}
bool PlateSolver::solveFile(const QUrl &file) {
    if (m_centeringOwned) return false;
    if (!file.isLocalFile()) { emit notice(QStringLiteral("请选择本地 FITS 文件")); return false; }
    return start(file.toLocalFile(),0);
}
bool PlateSolver::captureAndSolve(double seconds, double fov) {
    if (m_centeringOwned && !m_centeringDispatch) return false;
    if (m_busy) return false;
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 3600
        || !std::isfinite(fov) || fov < 0 || fov > 180) {
        emit notice(QStringLiteral("请输入有效曝光时间（0～3600 秒）和视场")); return false;
    }
    if (!QFileInfo(m_executable).isFile()) {
        emit notice(QStringLiteral("请先配置 ASTAP 程序")); return false;
    }
    m_result.clear(); m_busy = true; m_waitingCapture = true; m_captureFov = fov;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_status = QStringLiteral("正在拍摄解析照片…");
    emit changed(); emit captureRequested(seconds);
    return m_busy;
}
void PlateSolver::acceptCapture(QVector<quint16> pixels, int width, int height, const QVariantMap &metadata) {
    if (!m_waitingCapture) return;
    if (m_cancel->load()) { failCapture({}); return; }
    if (width <= 0 || height <= 0 || qint64(width)*height != pixels.size()) {
        failCapture(QStringLiteral("解析照片 RAW 数据无效")); return;
    }
    setCurrentFrame(std::move(pixels),width,height,metadata);
    m_waitingCapture = false; m_busy = false;
    start({},m_captureFov);
}
void PlateSolver::failCapture(const QString &message) {
    if (!m_waitingCapture) return;
    m_waitingCapture = false; m_busy = false;
    m_status = m_cancel->load() ? QStringLiteral("解析已取消") : QStringLiteral("拍摄失败");
    emit notice(m_cancel->load() ? m_status : message);
    emit changed();
}
bool PlateSolver::solveCurrent(double fov) {
    if (m_centeringOwned) return false;
    if (!currentAvailable()) { emit notice(QStringLiteral("尚无主相机已完成的 RAW 照片")); return false; }
    if (!std::isfinite(fov) || fov<0 || fov>180) return false;
    return start({},fov);
}
bool PlateSolver::start(const QString &file, double fov) {
    if (m_busy) return false;
    m_result.clear(); m_busy = true; m_status = QStringLiteral("正在解析…");
    m_source = file.isEmpty() ? m_currentLabel : file;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    emit changed();
    m_watcher.setFuture(QtConcurrent::run(PlateSolveJob::run,m_executable,file,
        file.isEmpty() ? m_pixels : QVector<quint16>{},m_size,fov,m_cancel,120000));
    return true;
}
void PlateSolver::cancel() {
    if (m_busy) {
        const bool alreadyCancelled = m_cancel->exchange(true);
        m_status = QStringLiteral("正在取消…"); emit changed();
        if (m_waitingCapture && !alreadyCancelled) emit cancelCaptureRequested();
    }
}
