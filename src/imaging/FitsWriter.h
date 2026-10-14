#pragma once

#include <QImage>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

// 极简 FITS 写入器：8 位、每颜色平面一个 IMAGE HDU（R/G/B 依次排列），
// 头部与数据均按 2880 字节对齐。自定义键值写入首个 HDU。
class FitsWriter
{
public:
    static bool write(const QString &path, const QImage &frame,
                      const QMap<QString, QString> &header = QMap<QString, QString>());

    // 16 位灰度原始帧（叠加/校准工作流用）：单个 HDU，BITPIX=16，FITS 的
    // 有符号约定经 BZERO=32768 映射无符号数据；数据按 FITS 规范写大端。
    static bool writeMono16(const QString &path, const QVector<quint16> &data,
                            int width, int height,
                            const QMap<QString, QString> &header = QMap<QString, QString>());
};
