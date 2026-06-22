#include "CameraView.hpp"
#include "SignController.hpp"

#include <QByteArray>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QVariant>
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
    engine.setInitialProperties({{QStringLiteral("signController"), QVariant::fromValue(&controller)}});
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    if (auto* cameraView = engine.rootObjects().first()->findChild<CameraView*>(QStringLiteral("patientCameraView"))) {
        controller.setCameraView(cameraView);
    }
    if (auto* cameraView = engine.rootObjects().first()->findChild<CameraView*>(QStringLiteral("doctorCameraView"))) {
        controller.setDoctorCameraView(cameraView);
    }

    return app.exec();
}
