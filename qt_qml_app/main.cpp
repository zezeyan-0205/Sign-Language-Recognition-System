#include "PreviewImageProvider.hpp"
#include "SignController.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    auto* previewProvider = new PreviewImageProvider();
    engine.addImageProvider(QStringLiteral("signpreview"), previewProvider);
    SignController controller(previewProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("signController"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
