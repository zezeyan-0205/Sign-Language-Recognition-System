import QtQuick 2.15

Rectangle {
    property string title: ""
    property string value: "-"
    property int valueSize: 24
    property color accent: "#20303f"

    color: "#ffffff"
    radius: 8
    border.color: "#d8dee6"

    Column {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        Text {
            width: parent.width
            text: title
            color: "#667085"
            font.pixelSize: 18
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            height: parent.height - 30
            text: value.length === 0 ? "-" : value
            color: accent
            font.pixelSize: valueSize
            font.bold: true
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
        }
    }
}
