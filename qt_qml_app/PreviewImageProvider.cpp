#include "PreviewImageProvider.hpp"

#include <QMutexLocker>

PreviewImageProvider::PreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage PreviewImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    Q_UNUSED(id);

    QMutexLocker lock(&mutex_);
    QImage result = image_;
    if (result.isNull()) {
        result = QImage(16, 16, QImage::Format_RGB32);
        result.fill(Qt::black);
    }

    if (size) {
        *size = result.size();
    }
    if (requestedSize.isValid()) {
        return result.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return result;
}

void PreviewImageProvider::setImage(const QImage& image)
{
    QMutexLocker lock(&mutex_);
    image_ = image.copy();
}
