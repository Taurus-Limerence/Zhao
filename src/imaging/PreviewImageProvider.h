#pragma once

#include <QQuickImageProvider>
#include "devices/DeviceManager.h"

// Serves the latest live frames to QML Image elements; the id in the URL
// routes the stream ("image://live/frame?<n>" main camera, ".../guide?<n>"
// guide camera) and the counter query busts the cache per received frame.
class PreviewImageProvider final : public QQuickImageProvider
{
public:
    explicit PreviewImageProvider(const DeviceManager *manager)
        : QQuickImageProvider(QQuickImageProvider::Image), m_manager(manager) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        Q_UNUSED(requestedSize)
        const QImage frame = id.startsWith(QStringLiteral("guide"))
            ? m_manager->guidePreviewFrame() : m_manager->previewFrame();
        if (size) *size = frame.size();
        return frame;
    }

private:
    const DeviceManager *m_manager;
};
