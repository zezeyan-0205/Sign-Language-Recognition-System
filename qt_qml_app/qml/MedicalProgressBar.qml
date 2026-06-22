import QtQuick 2.15

Rectangle {
    id: root
    property int value: 0
    property color accent: "#63c9e8"
    property string label: "采集进度"

    radius: 8
    color: "#e0edf6"
    clip: true
    border.width: 1
    border.color: "#bad2e5"

    Rectangle {
        width: parent.width * Math.max(0, Math.min(root.value, 100)) / 100.0
        height: parent.height
        radius: root.radius
        color: root.accent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.lighter(root.accent, 1.12) }
            GradientStop { position: 1.0; color: Qt.darker(root.accent, 1.12) }
        }

        Behavior on width {
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
        }
    }

    Text {
        anchors.centerIn: parent
        text: root.label + " " + root.value + "%"
        color: root.value > 48 ? "#ffffff" : "#17324d"
        font.pixelSize: 18
        font.bold: true
    }
}
