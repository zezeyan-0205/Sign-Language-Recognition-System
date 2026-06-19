#include "CameraView.hpp"
#include "SignController.hpp"

#include <QByteArray>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", QByteArray("software"));
    }
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", QByteArray("basic"));
    }

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    qmlRegisterType<CameraView>("SignLanguage", 1, 0, "CameraView");

    SignController controller;
    engine.rootContext()->setContextProperty(QStringLiteral("signController"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    if (auto* cameraView = engine.rootObjects().first()->findChild<CameraView*>(QStringLiteral("patientCameraView"))) {
        controller.setCameraView(cameraView);
    }

    return app.exec();
}
