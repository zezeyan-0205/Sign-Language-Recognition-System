import QtQuick 2.15

Rectangle {
    id: root
    property alias content: contentItem.data
    property color cardColor: "#eef4f7"
    property color gradientTop: cardColor
    property color gradientBottom: cardColor
    property color lineColor: "#314454"
    property int padding: 18
    property bool useGradient: false

    color: useGradient ? "transparent" : cardColor
    radius: 14
    border.width: 1
    border.color: lineColor

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        visible: root.useGradient
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.gradientTop }
            GradientStop { position: 1.0; color: root.gradientBottom }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: parent.radius - 1
        color: "transparent"
        border.width: 1
        border.color: "#24ffffff"
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.min(52, parent.height * 0.30)
        radius: parent.radius
        color: "#18ffffff"
    }

    Item {
        id: contentItem
        anchors.fill: parent
        anchors.margins: root.padding
    }

    Behavior on color {
        ColorAnimation { duration: 260 }
    }

    Behavior on border.color {
        ColorAnimation { duration: 260 }
    }
}
