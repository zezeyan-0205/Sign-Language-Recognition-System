#ifndef SIGN_LANGUAGE_QML_CAMERA_VIEW_HPP
#define SIGN_LANGUAGE_QML_CAMERA_VIEW_HPP

#include <QImage>
#include <QMutex>
#include <QQuickPaintedItem>

class CameraView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)

public:
    explicit CameraView(QQuickItem* parent = nullptr);

    bool hasFrame() const;
    void paint(QPainter* painter) override;

public slots:
    void setFrame(const QImage& image);

signals:
    void hasFrameChanged();

private:
    mutable QMutex mutex_;
    QImage frame_;
    bool has_frame_ = false;
};

#endif
