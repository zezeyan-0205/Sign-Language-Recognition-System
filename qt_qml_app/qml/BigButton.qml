import QtQuick 2.15

Rectangle {
    id: root
    property alias label: labelText.text
    property int fontSize: 28
    property bool visualPressed: false
    signal clicked()

    radius: 8
    opacity: enabled ? 1.0 : 0.65
    scale: visualPressed ? 0.96 : 1.0

    Behavior on color {
        ColorAnimation { duration: 180 }
    }

    Behavior on opacity {
        NumberAnimation { duration: 160 }
    }

    Behavior on scale {
        NumberAnimation {
            duration: 90
            easing.type: Easing.OutCubic
        }
    }

    Text {
        id: labelText
        anchors.centerIn: parent
        width: parent.width - 8
        color: "white"
        font.pixelSize: root.fontSize
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    SequentialAnimation {
        id: pressFeedback
        PropertyAction {
            target: root
            property: "visualPressed"
            value: true
        }
        PauseAnimation { duration: 90 }
        PropertyAction {
            target: root
            property: "visualPressed"
            value: false
        }
    }

    TapHandler {
        id: tapHandler
        enabled: root.enabled
        gesturePolicy: TapHandler.WithinBounds
        onPressedChanged: {
            if (pressed) {
                pressFeedback.stop()
                root.visualPressed = true
            } else {
                root.visualPressed = false
            }
        }
        onTapped: {
            pressFeedback.restart()
            root.clicked()
        }
    }
}
