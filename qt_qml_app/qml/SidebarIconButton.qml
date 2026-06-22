import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    property bool active: false
    property string iconSource: ""

    width: 88
    height: 78
    display: AbstractButton.IconOnly
    hoverEnabled: true
    icon.source: iconSource
    icon.width: 42
    icon.height: 42
    icon.color: active ? "#ffffff" : (hovered ? "#e5f3fb" : "#bfd3e2")

    background: Rectangle {
        anchors.centerIn: parent
        width: 68
        height: 68
        radius: 14
        color: root.active ? "#1f5f8f" : "transparent"
    }
}
