#include "CameraView.hpp"

#include <QColor>
#include <QMutexLocker>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <algorithm>

CameraView::CameraView(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
}

bool CameraView::hasFrame() const
{
    QMutexLocker lock(&mutex_);
    return has_frame_;
}

void CameraView::setFrame(const QImage& image)
{
    const bool had_frame = hasFrame();
    {
        QMutexLocker lock(&mutex_);
        frame_ = image.copy();
        has_frame_ = !frame_.isNull();
    }

    if (had_frame != hasFrame()) {
        emit hasFrameChanged();
    }
    update();
}

void CameraView::clearFrame()
{
    setFrame(QImage());
}

void CameraView::paint(QPainter* painter)
{
    QImage frame;
    {
        QMutexLocker lock(&mutex_);
        frame = frame_;
    }

    painter->fillRect(boundingRect(), QColor(16, 24, 32));
    if (frame.isNull()) {
        return;
    }

    const QSizeF item_size = boundingRect().size();
    const QSizeF image_size = frame.size();
    const qreal scale = std::min(item_size.width() / image_size.width(), item_size.height() / image_size.height());
    const QSizeF draw_size(image_size.width() * scale, image_size.height() * scale);
    const QPointF top_left(
        (item_size.width() - draw_size.width()) * 0.5,
        (item_size.height() - draw_size.height()) * 0.5);

    painter->drawImage(QRectF(top_left, draw_size), frame);
}
