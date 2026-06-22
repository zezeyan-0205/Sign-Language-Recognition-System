import QtQuick 2.15

MedicalCard {
    id: root
    property string title: ""
    property string hint: ""
    property string value: ""
    property color accent: "#63c9e8"

    cardColor: Qt.rgba(accent.r, accent.g, accent.b, 0.18)
    gradientTop: "#ffffff"
    gradientBottom: Qt.rgba(accent.r, accent.g, accent.b, 0.14)
    useGradient: true
    lineColor: Qt.rgba(accent.r, accent.g, accent.b, 0.50)
    padding: 16

    content: [
        Column {
            anchors.fill: parent
            spacing: 6

            Row {
                width: parent.width
                height: 28
                spacing: 10

                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: root.accent
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    width: parent.width - 26
                    text: root.title
                    color: "#17324d"
                    font.pixelSize: 20
                    font.bold: true
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Text {
                width: parent.width
                text: root.value
                color: "#0b4f6c"
                font.pixelSize: root.value.length > 8 ? 32 : 44
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                height: parent.height - (root.hint.length > 0 ? 66 : 36)
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: root.hint.length > 0
                height: visible ? implicitHeight : 0
                text: root.hint
                color: "#607285"
                font.pixelSize: 15
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    ]
}
