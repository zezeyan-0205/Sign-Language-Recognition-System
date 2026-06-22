import QtQuick 2.15

Rectangle {
    id: root
    property string text: ""
    property color accent: "#63c9e8"

    radius: 8
    color: Qt.rgba(accent.r, accent.g, accent.b, 0.16)
    border.width: 1
    border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.48)

    Text {
        anchors.centerIn: parent
        width: parent.width - 12
        text: root.text
        color: "#17324d"
        font.pixelSize: 16
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
