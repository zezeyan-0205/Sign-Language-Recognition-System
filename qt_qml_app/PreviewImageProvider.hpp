#ifndef SIGN_LANGUAGE_QML_PREVIEW_IMAGE_PROVIDER_HPP
#define SIGN_LANGUAGE_QML_PREVIEW_IMAGE_PROVIDER_HPP

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class PreviewImageProvider : public QQuickImageProvider {
public:
    PreviewImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
    void setImage(const QImage& image);

private:
    QMutex mutex_;
    QImage image_;
};

#endif
