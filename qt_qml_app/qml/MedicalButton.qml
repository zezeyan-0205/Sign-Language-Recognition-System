import QtQuick 2.15

Rectangle {
    id: root
    property string label: ""
    property int fontSize: 28
    property color activeColor: "#63c9e8"
    property color inactiveColor: "#6f7c87"
    property bool primaryAction: false
    property bool visualPressed: false
    property bool feedbackHeld: false
    property bool hovered: false
    signal clicked()

    radius: 8
    opacity: enabled ? 1.0 : 0.48
    scale: (visualPressed || feedbackHeld) ? 0.94 : 1.0
    color: enabled ? (hovered ? Qt.lighter(activeColor, 1.08) : activeColor) : inactiveColor

    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: (root.visualPressed || root.feedbackHeld)
                   ? Qt.darker(root.color, 1.08)
                   : Qt.lighter(root.color, root.primaryAction ? 1.16 : 1.06)
        }
        GradientStop {
            position: 1.0
            color: (root.visualPressed || root.feedbackHeld)
                   ? Qt.darker(root.color, 1.18)
                   : Qt.darker(root.color, 1.10)
        }
    }

    border.width: 1
    border.color: enabled ? ((visualPressed || feedbackHeld) ? "#44ffffff" : "#70ffffff") : "#22ffffff"

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 1
        height: parent.height * 0.42
        radius: root.radius - 1
        color: "#22ffffff"
        opacity: enabled && !(root.visualPressed || root.feedbackHeld) ? 1.0 : 0.28
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "#18000000"
        opacity: (root.visualPressed || root.feedbackHeld) ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 90 }
        }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 12
        text: root.label
        color: "white"
        font.pixelSize: root.fontSize
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        z: 2
    }

    SequentialAnimation {
        id: pressFeedback
        PropertyAction { target: root; property: "feedbackHeld"; value: true }
        PauseAnimation { duration: 160 }
        PropertyAction { target: root; property: "feedbackHeld"; value: false }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        onEntered: root.hovered = true
        onExited: {
            root.hovered = false
            root.visualPressed = false
        }
        onPressed: root.visualPressed = true
        onReleased: root.visualPressed = false
        onCanceled: root.visualPressed = false
        onClicked: {
            pressFeedback.restart()
            root.clicked()
        }
    }

    Behavior on color {
        ColorAnimation { duration: 180 }
    }

    Behavior on opacity {
        NumberAnimation { duration: 160 }
    }

    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }
}
