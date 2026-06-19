import QtQuick 2.15
import QtQuick.Window 2.15
import SignLanguage 1.0

Window {
    id: patientWindow
    width: 1280
    height: 720
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "患者端手语表达"
    color: "#f3f6f9"

    property color ink: "#15202b"
    property color panel: "#ffffff"
    property color primary: "#1f8a4c"
    property color danger: "#b73434"
    property bool patientExpanded: true

    function buttonColor(enabled, activeColor) {
        return enabled ? activeColor : "#aeb8c2"
    }

    function screenByName(partialName, fallbackIndex) {
        for (var i = 0; i < Qt.application.screens.length; ++i) {
            if (Qt.application.screens[i].name.indexOf(partialName) >= 0)
                return Qt.application.screens[i]
        }
        if (Qt.application.screens.length > fallbackIndex)
            return Qt.application.screens[fallbackIndex]
        return Qt.application.screens.length > 0 ? Qt.application.screens[0] : null
    }

    function screenX(targetScreen) {
        if (targetScreen && targetScreen.virtualX !== undefined)
            return targetScreen.virtualX
        return targetScreen && targetScreen.name.indexOf("DSI") >= 0 ? 1920 : 0
    }

    function screenY(targetScreen) {
        if (targetScreen && targetScreen.virtualY !== undefined)
            return targetScreen.virtualY
        return 0
    }

    function placeWindowOnScreen(targetWindow, partialName, fallbackIndex, expanded) {
        var targetScreen = screenByName(partialName, fallbackIndex)
        if (!targetScreen)
            return

        targetWindow.showNormal()
        targetWindow.visibility = Window.Windowed
        targetWindow.screen = targetScreen
        var sx = screenX(targetScreen)
        var sy = screenY(targetScreen)
        var sw = targetScreen.width
        var sh = targetScreen.height
        if (expanded) {
            targetWindow.x = sx
            targetWindow.y = sy
            targetWindow.width = sw
            targetWindow.height = sh
        } else {
            targetWindow.width = Math.max(640, Math.floor(sw * 0.82))
            targetWindow.height = Math.max(420, Math.floor(sh * 0.82))
            targetWindow.x = sx + Math.floor((sw - targetWindow.width) / 2)
            targetWindow.y = sy + Math.floor((sh - targetWindow.height) / 2)
        }
        targetWindow.visible = true
        targetWindow.raise()
    }

    Component.onCompleted: {
        placeWindowOnScreen(patientWindow, "HDMI", 0, true)
    }

    Rectangle {
        anchors.fill: parent
        color: "#f3f6f9"
    }

    Rectangle {
        id: patientTitleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 44
        color: "#17212b"
        z: 10

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            text: "患者端手语表达"
            color: "white"
            font.pixelSize: 20
            font.bold: true
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 8
            spacing: 8

            BigButton {
                width: 44
                height: 32
                label: "—"
                fontSize: 18
                color: "#3f4b57"
                onClicked: patientWindow.showMinimized()
            }
            BigButton {
                width: 44
                height: 32
                label: patientWindow.patientExpanded ? "□" : "▣"
                fontSize: 18
                color: "#3f4b57"
                onClicked: {
                    patientWindow.patientExpanded = !patientWindow.patientExpanded
                    patientWindow.placeWindowOnScreen(patientWindow, "HDMI", 0, patientWindow.patientExpanded)
                }
            }
            BigButton {
                width: 44
                height: 32
                label: "X"
                fontSize: 18
                color: danger
                onClicked: Qt.quit()
            }
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: 24
        anchors.topMargin: 68
        spacing: 24

        Rectangle {
            width: parent.width * 0.52
            height: parent.height
            color: "#101820"
            radius: 8
            clip: true

            CameraView {
                id: patientCameraView
                objectName: "patientCameraView"
                anchors.fill: parent
                anchors.margins: 10
            }

            Text {
                anchors.centerIn: parent
                visible: !patientCameraView.hasFrame
                text: "摄像头画面等待中"
                color: "#dbe4ec"
                font.pixelSize: 34
                font.bold: true
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 54
                color: "#b0000000"

                Text {
                    anchors.centerIn: parent
                    text: signController.statusText
                    color: "white"
                    font.pixelSize: 24
                    font.bold: true
                }
            }
        }

        Column {
            width: parent.width * 0.48 - 24
            height: parent.height
            spacing: 16

            Rectangle {
                width: parent.width
                height: 168
                color: panel
                radius: 8
                border.color: "#ccd6df"

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    text: signController.currentWord
                    color: primary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 86
                    font.bold: true
                    elide: Text.ElideRight
                }
            }

            Row {
                width: parent.width
                height: 48
                spacing: 12

                MetricBox { width: parent.width / 3 - 8; height: parent.height; text: signController.confidenceText }
                MetricBox { width: parent.width / 3 - 8; height: parent.height; text: signController.fpsText }
                MetricBox { width: parent.width / 3 - 8; height: parent.height; text: signController.composerText }
            }

            Rectangle {
                width: parent.width
                height: 46
                color: "#eef5ff"
                radius: 6
                border.color: "#c3d4e6"

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    text: signController.latencyText
                    color: "#2d4059"
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                width: parent.width
                height: 58
                color: "#fffdf2"
                radius: 6
                border.color: "#e3d798"

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    text: signController.tokensText
                    color: "#2d3436"
                    font.pixelSize: 23
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                width: parent.width
                height: 118
                color: "#f3fbff"
                radius: 8
                border.color: "#b8d3e1"

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 34
                    text: signController.sentenceText
                    color: "#0b4f6c"
                    font.pixelSize: 34
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                width: parent.width
                height: 44
                color: "#dce5ec"
                radius: 6
                clip: true

                Rectangle {
                    width: parent.width * signController.collectProgress / 100.0
                    height: parent.height
                    color: primary
                }

                Text {
                    anchors.centerIn: parent
                    text: "采集进度 " + signController.collectProgress + "%"
                    color: signController.collectProgress > 48 ? "white" : "#20303f"
                    font.pixelSize: 22
                    font.bold: true
                }
            }

            Row {
                width: parent.width
                height: 84
                spacing: 14

                BigButton {
                    width: parent.width / 3 - 10
                    height: parent.height
                    label: "开始"
                    color: buttonColor(signController.running, primary)
                    enabled: signController.running
                    onClicked: signController.startRecording()
                }
                BigButton {
                    width: parent.width / 3 - 10
                    height: parent.height
                    label: "结束"
                    color: buttonColor(signController.running, "#1f6fb2")
                    enabled: signController.running
                    onClicked: signController.stopRecording()
                }
                BigButton {
                    width: parent.width / 3 - 10
                    height: parent.height
                    label: "清空"
                    color: buttonColor(signController.running, "#6f7b87")
                    enabled: signController.running
                    onClicked: signController.clearExpression()
                }
            }

            Row {
                width: parent.width
                height: 72
                spacing: 14

                BigButton {
                    width: parent.width / 2 - 7
                    height: parent.height
                    label: "启动系统"
                    color: buttonColor(!signController.running, primary)
                    enabled: !signController.running
                    onClicked: signController.startSystem()
                }
                BigButton {
                    width: parent.width / 2 - 7
                    height: parent.height
                    label: "停止系统"
                    color: buttonColor(signController.running, danger)
                    enabled: signController.running
                    onClicked: signController.stopSystem()
                }
            }
        }
    }

    Window {
        id: doctorWindow
        width: 940
        height: 760
        visible: false
        flags: Qt.Window | Qt.FramelessWindowHint
        title: "医生端电子病历"
        color: "#eef3f7"
        property bool doctorExpanded: true

        Component.onCompleted: {
            patientWindow.placeWindowOnScreen(doctorWindow, "DSI", 1, true)
        }

        Rectangle {
            id: doctorTitleBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 42
            color: "#17324d"
            z: 10

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                text: "医生端电子病历"
                color: "white"
                font.pixelSize: 18
                font.bold: true
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 8
                spacing: 7

                BigButton {
                    width: 40
                    height: 30
                    label: "—"
                    fontSize: 18
                    color: "#42566a"
                    onClicked: doctorWindow.showMinimized()
                }
                BigButton {
                    width: 40
                    height: 30
                    label: doctorWindow.doctorExpanded ? "□" : "▣"
                    fontSize: 18
                    color: "#42566a"
                    onClicked: {
                        doctorWindow.doctorExpanded = !doctorWindow.doctorExpanded
                        patientWindow.placeWindowOnScreen(doctorWindow, "DSI", 1, doctorWindow.doctorExpanded)
                    }
                }
                BigButton {
                    width: 40
                    height: 30
                    label: "X"
                    fontSize: 18
                    color: "#b73434"
                    onClicked: Qt.quit()
                }
            }
        }

        Flickable {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            anchors.topMargin: 62
            anchors.bottomMargin: 18
            clip: true
            contentWidth: width
            contentHeight: doctorContent.height

            Column {
                id: doctorContent
                width: parent.width
                spacing: 16

                Text {
                    width: parent.width
                    height: 48
                    text: "门诊无障碍沟通记录"
                    color: "#17324d"
                    font.pixelSize: 32
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }

                RecordCard {
                    width: parent.width
                    height: 70
                    title: "实时识别"
                    value: signController.currentWord + "    " + signController.confidenceText
                }

                RecordCard {
                    width: parent.width
                    height: 128
                    title: "患者主诉"
                    value: signController.sentenceText.replace("整理句子：", "")
                    valueSize: 32
                    accent: "#0b4f6c"
                }

                RecordCard {
                    width: parent.width
                    height: 86
                    title: "识别词"
                    value: signController.tokensText.replace("已收集词汇：", "")
                }

                Row {
                    width: parent.width
                    height: 86
                    spacing: 16

                    RecordCard {
                        width: parent.width / 2 - 8
                        height: parent.height
                        title: "症状"
                        value: doctorWindow.symptomText()
                    }
                    RecordCard {
                        width: parent.width / 2 - 8
                        height: parent.height
                        title: "时间"
                        value: signController.tokensText.indexOf("昨天") >= 0 ? "昨天" : "待确认"
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 150
                    color: "#fffdf7"
                    radius: 8
                    border.color: "#e0d7aa"

                    Text {
                        anchors.fill: parent
                        anchors.margins: 18
                        text: "医生回复：请稍等，我帮您登记。"
                        color: "#374151"
                        font.pixelSize: 28
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 104
                    color: "#ffffff"
                    radius: 8
                    border.color: "#d8dee6"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8

                        Text {
                            width: parent.width
                            text: "系统状态：" + signController.statusText
                            color: "#374151"
                            font.pixelSize: 20
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: signController.latencyText
                            color: "#586575"
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        function symptomText() {
            var text = signController.tokensText
            if (text.indexOf("发烧") >= 0)
                return "发烧"
            if (text.indexOf("咳嗽") >= 0)
                return "咳嗽"
            if (text.indexOf("头") >= 0 && text.indexOf("疼痛") >= 0)
                return "头疼"
            if (text.indexOf("疼痛") >= 0)
                return "疼痛"
            return "待确认"
        }
    }
}
