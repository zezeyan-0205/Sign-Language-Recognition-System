import QtQuick 2.15

Rectangle {
    property alias text: metricText.text

    color: "#ffffff"
    radius: 6
    border.color: "#d5dce3"

    Text {
        id: metricText
        anchors.centerIn: parent
        width: parent.width - 14
        color: "#344054"
        font.pixelSize: 18
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
