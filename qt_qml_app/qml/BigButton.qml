import QtQuick 2.15

Rectangle {
    id: root
    property alias label: labelText.text
    property int fontSize: 28
    signal clicked()

    radius: 8
    opacity: enabled ? 1.0 : 0.65

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

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
