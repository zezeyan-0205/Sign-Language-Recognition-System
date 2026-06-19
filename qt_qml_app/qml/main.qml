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
    property bool compactPatient: width <= 1100 || height <= 650
    property bool recognizing: signController.currentWord === "识别中..."

    function buttonColor(enabled, activeColor) {
        return enabled ? activeColor : "#aeb8c2"
    }

    function cleanSentence() {
        var text = signController.sentenceText.replace("整理句子：", "")
        return text.length === 0 || text === "-" ? "待整理" : text
    }

    function cleanTokens() {
        var text = signController.tokensText.replace("已收集词汇：", "")
        return text.length === 0 || text === "-" ? "待识别" : text
    }

    function patientHint() {
        if (!signController.running)
            return "等待医生启动系统"
        if (signController.currentWord.indexOf("点击绿色按钮") >= 0)
            return "点击绿色按钮开始识别"
        if (signController.currentWord === "等待中...")
            return "请把手放入框内"
        if (signController.currentWord === "识别中...")
            return "请保持动作"
        return "已识别"
    }

    function isRecognizedWord(text) {
        return (
            text.length > 0 &&
            text !== "等待中..." &&
            text !== "识别中..." &&
            text.indexOf("点击绿色按钮") < 0
        )
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
        placeWindowOnScreen(patientWindow, "DSI", 1, true)
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
        height: compactPatient ? 36 : 44
        color: "#17212b"
        z: 10

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: compactPatient ? 10 : 18
            text: "患者端手语表达"
            color: "white"
            font.pixelSize: compactPatient ? 16 : 20
            font.bold: true
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 8
            spacing: 8

            BigButton {
                width: compactPatient ? 36 : 44
                height: compactPatient ? 28 : 32
                label: "—"
                fontSize: compactPatient ? 15 : 18
                color: "#3f4b57"
                onClicked: patientWindow.showMinimized()
            }
            BigButton {
                width: compactPatient ? 36 : 44
                height: compactPatient ? 28 : 32
                label: patientWindow.patientExpanded ? "□" : "▣"
                fontSize: compactPatient ? 15 : 18
                color: "#3f4b57"
                onClicked: {
                    patientWindow.patientExpanded = !patientWindow.patientExpanded
                    patientWindow.placeWindowOnScreen(patientWindow, "DSI", 1, patientWindow.patientExpanded)
                }
            }
            BigButton {
                width: compactPatient ? 36 : 44
                height: compactPatient ? 28 : 32
                label: "X"
                fontSize: compactPatient ? 15 : 18
                color: danger
                onClicked: Qt.quit()
            }
        }
    }

    Item {
        anchors.fill: parent
        anchors.leftMargin: compactPatient ? 8 : 14
        anchors.rightMargin: compactPatient ? 8 : 14
        anchors.topMargin: compactPatient ? 40 : 58
        anchors.bottomMargin: compactPatient ? 6 : 12

        Rectangle {
            id: cameraPanel
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * 0.50
            height: parent.height
            color: "#101820"
            radius: 8
            clip: true

            CameraView {
                id: patientCameraView
                objectName: "patientCameraView"
                anchors.fill: parent
                anchors.margins: compactPatient ? 6 : 10
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
                height: compactPatient ? 44 : 64
                color: "#b0000000"

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 36
                    text: patientHint()
                    color: "white"
                    font.pixelSize: compactPatient ? 18 : 28
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
        }

        Column {
            anchors.left: cameraPanel.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: compactPatient ? 6 : 12
            anchors.topMargin: 0
            spacing: compactPatient ? 5 : 10

            Rectangle {
                id: currentStateCard
                width: parent.width
                height: compactPatient ? 88 : 145
                color: recognizing ? "#e8fff0" : "#f7fff9"
                radius: 8
                border.color: recognizing ? primary : "#b8dac6"

                SequentialAnimation on opacity {
                    running: patientWindow.recognizing
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.82; duration: 650; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 650; easing.type: Easing.InOutQuad }
                }

                Behavior on color {
                    ColorAnimation { duration: 260 }
                }

                Behavior on border.color {
                    ColorAnimation { duration: 260 }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: compactPatient ? 8 : 18
                    spacing: compactPatient ? 2 : 8

                    Text {
                        width: parent.width
                        height: compactPatient ? 20 : 34
                        text: patientHint()
                        color: ink
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: compactPatient ? 17 : 26
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        id: currentWordText
                        width: parent.width
                        height: parent.height - (compactPatient ? 22 : 42)
                        text: signController.currentWord
                        color: primary
                        scale: 1.0
                        font.pixelSize: signController.currentWord.length > 8 ? (compactPatient ? 24 : 36) : (compactPatient ? 38 : 64)
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight

                        onTextChanged: {
                            if (patientWindow.isRecognizedWord(text))
                                wordRecognizedFlash.restart()
                        }
                    }

                    SequentialAnimation {
                        id: wordRecognizedFlash
                        NumberAnimation {
                            target: currentWordText
                            property: "scale"
                            to: 1.08
                            duration: 120
                            easing.type: Easing.OutCubic
                        }
                        NumberAnimation {
                            target: currentWordText
                            property: "scale"
                            to: 1.0
                            duration: 180
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            Rectangle {
                id: sentenceCard
                width: parent.width
                height: compactPatient ? 96 : 145
                color: "#f3fbff"
                radius: 8
                border.color: "#b8d3e1"

                Behavior on color {
                    ColorAnimation { duration: 240 }
                }

                Behavior on border.color {
                    ColorAnimation { duration: 240 }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: compactPatient ? 8 : 18
                    spacing: compactPatient ? 2 : 8

                    Text {
                        width: parent.width
                        height: compactPatient ? 18 : 30
                        text: "整理结果"
                        color: "#667085"
                        font.pixelSize: compactPatient ? 15 : 20
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    Text {
                        id: sentenceResultText
                        width: parent.width
                        height: parent.height - (compactPatient ? 20 : 38)
                        text: cleanSentence()
                        color: "#0b4f6c"
                        opacity: 1.0
                        font.pixelSize: compactPatient ? 20 : 30
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap

                        onTextChanged: {
                            if (text !== "待整理")
                                sentenceFadeIn.restart()
                        }
                    }

                    SequentialAnimation {
                        id: sentenceFadeIn
                        PropertyAction {
                            target: sentenceResultText
                            property: "opacity"
                            value: 0.45
                        }
                        NumberAnimation {
                            target: sentenceResultText
                            property: "opacity"
                            to: 1.0
                            duration: 360
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: compactPatient ? 36 : 64
                color: "#fffdf2"
                radius: 8
                border.color: "#e3d798"

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 30
                    text: "识别词：" + cleanTokens()
                    color: "#303846"
                    font.pixelSize: compactPatient ? 15 : 24
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            Item {
                width: parent.width
                height: compactPatient
                    ? Math.max(4, parent.height - 88 - 96 - 36 - 30 - 136 - 92 - spacing * 7)
                    : Math.max(12, parent.height - 145 - 145 - 64 - 46 - 214 - 132 - spacing * 7)
            }

            Rectangle {
                width: parent.width
                height: compactPatient ? 30 : 46
                color: "#dce5ec"
                radius: 6
                clip: true

                Rectangle {
                    id: progressFill
                    width: parent.width * signController.collectProgress / 100.0
                    height: parent.height
                    color: primary

                    Behavior on width {
                        NumberAnimation {
                            duration: 180
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: "采集进度 " + signController.collectProgress + "%"
                    color: signController.collectProgress > 48 ? "white" : "#20303f"
                    font.pixelSize: compactPatient ? 15 : 22
                    font.bold: true
                }
            }

            Column {
                width: parent.width
                height: compactPatient ? 136 : 214
                spacing: compactPatient ? 6 : 12

                Row {
                    width: parent.width
                    height: compactPatient ? 66 : 104
                    spacing: compactPatient ? 8 : 14

                    BigButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: "开始"
                        fontSize: compactPatient ? 24 : 34
                        color: buttonColor(signController.running, primary)
                        enabled: signController.running
                        onClicked: signController.startRecording()
                    }
                    BigButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: "结束"
                        fontSize: compactPatient ? 24 : 34
                        color: buttonColor(signController.running, "#1f6fb2")
                        enabled: signController.running
                        onClicked: signController.stopRecording()
                    }
                }

                Row {
                    width: parent.width
                    height: compactPatient ? 64 : 98
                    spacing: compactPatient ? 8 : 14

                    BigButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: "删除"
                        fontSize: compactPatient ? 22 : 31
                        color: buttonColor(signController.running, "#c97b1d")
                        enabled: signController.running
                        onClicked: signController.deleteLastToken()
                    }
                    BigButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: "清空"
                        fontSize: compactPatient ? 22 : 31
                        color: buttonColor(signController.running, "#6f7b87")
                        enabled: signController.running
                        onClicked: signController.clearExpression()
                    }
                }
            }

            Rectangle {
                id: patientTipCard
                width: parent.width
                height: compactPatient ? 92 : 132
                color: signController.running ? "#eef6ff" : "#f7fbff"
                radius: 8
                border.color: signController.running ? "#c8d8ec" : "#d9e6f2"

                Behavior on color {
                    ColorAnimation { duration: 240 }
                }

                Behavior on border.color {
                    ColorAnimation { duration: 240 }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: compactPatient ? 10 : 16
                    spacing: compactPatient ? 4 : 8

                    Text {
                        width: parent.width
                        text: signController.running ? "操作提示" : "准备开始"
                        color: "#17324d"
                        font.pixelSize: compactPatient ? 16 : 22
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        height: parent.height - (compactPatient ? 26 : 36)
                        text: signController.running
                              ? "点击开始后，请把手放在左侧画面中间区域并保持动作。"
                              : "请等待医生启动系统，随后点击开始进行手语识别。"
                        color: "#405367"
                        font.pixelSize: compactPatient ? 14 : 20
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
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
            patientWindow.placeWindowOnScreen(doctorWindow, "HDMI", 0, true)
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
                        patientWindow.placeWindowOnScreen(doctorWindow, "HDMI", 0, doctorWindow.doctorExpanded)
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
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            anchors.topMargin: 56
            anchors.bottomMargin: 14
            clip: true
            contentWidth: width
            contentHeight: doctorContent.height

            Column {
                id: doctorContent
                width: parent.width
                spacing: 14

                Row {
                    width: parent.width
                    height: 56
                    spacing: 14

                    Text {
                        width: parent.width - 300
                        height: parent.height
                        text: "门诊无障碍沟通记录"
                        color: "#17324d"
                        font.pixelSize: 30
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    BigButton {
                        width: 136
                        height: parent.height
                        label: "启动系统"
                        fontSize: 22
                        color: buttonColor(!signController.running, patientWindow.primary)
                        enabled: !signController.running
                        onClicked: signController.startSystem()
                    }

                    BigButton {
                        width: 136
                        height: parent.height
                        label: "停止系统"
                        fontSize: 22
                        color: buttonColor(signController.running, patientWindow.danger)
                        enabled: signController.running
                        onClicked: signController.stopSystem()
                    }
                }

                Rectangle {
                    id: doctorRecordPage
                    width: parent.width
                    height: Math.max(500, parent.parent.height - 150)
                    color: "#ffffff"
                    radius: 8
                    border.color: "#d8dee6"
                    opacity: signController.running ? 1.0 : 0.92

                    Behavior on opacity {
                        NumberAnimation { duration: 220 }
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 28
                        spacing: 18

                        Row {
                            width: parent.width
                            height: 28

                            Text { width: parent.width * 0.24; text: "患者：手语沟通"; color: "#667085"; font.pixelSize: 17; elide: Text.ElideRight }
                            Text { width: parent.width * 0.20; text: "科室：门诊"; color: "#667085"; font.pixelSize: 17; elide: Text.ElideRight }
                            Text { width: parent.width * 0.24; text: "时间：" + doctorWindow.onsetText(); color: "#667085"; font.pixelSize: 17; elide: Text.ElideRight }
                            Text {
                                width: parent.width * 0.32
                                text: "整理：" + signController.composerText.replace("整理方式：", "")
                                color: "#667085"
                                font.pixelSize: 17
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: "#e5eaf0"
                        }

                        Text {
                            width: parent.width
                            text: "患者主诉"
                            color: "#667085"
                            font.pixelSize: 20
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            id: doctorChiefComplaint
                            width: parent.width
                            height: 160
                            text: patientWindow.cleanSentence()
                            color: "#0b4f6c"
                            font.pixelSize: 44
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap

                            Behavior on opacity {
                                NumberAnimation { duration: 180 }
                            }
                        }

                        Row {
                            width: parent.width
                            height: 62

                            Column {
                                width: parent.width * 0.28
                                spacing: 5
                                Text { width: parent.width; text: "主要症状"; color: "#8a95a3"; font.pixelSize: 16; elide: Text.ElideRight }
                                Text { width: parent.width; text: doctorWindow.symptomText(); color: "#20303f"; font.pixelSize: 26; font.bold: true; elide: Text.ElideRight }
                            }

                            Column {
                                width: parent.width * 0.28
                                spacing: 5
                                Text { width: parent.width; text: "实时识别"; color: "#8a95a3"; font.pixelSize: 16; elide: Text.ElideRight }
                                Text { width: parent.width; text: signController.currentWord; color: "#1f8a4c"; font.pixelSize: 26; font.bold: true; elide: Text.ElideRight }
                            }

                            Column {
                                width: parent.width * 0.44
                                spacing: 5
                                Text { width: parent.width; text: "识别词流"; color: "#8a95a3"; font.pixelSize: 16; elide: Text.ElideRight }
                                Text { width: parent.width; text: patientWindow.cleanTokens(); color: "#303846"; font.pixelSize: 24; font.bold: true; elide: Text.ElideRight }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: "#e5eaf0"
                        }

                        Text {
                            width: parent.width
                            text: "病历记录"
                            color: "#17324d"
                            font.pixelSize: 22
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: "主诉：" + patientWindow.cleanSentence()
                            color: "#374151"
                            font.pixelSize: 22
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            width: parent.width
                            text: "备注：请结合患者动作和识别词流确认。如表达不清，可请患者重新表达。"
                            color: "#586575"
                            font.pixelSize: 20
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 58
                    color: signController.running ? "#eef3f7" : "#f5f7fa"
                    radius: 8
                    border.color: "#d8dee6"

                    Behavior on color {
                        ColorAnimation { duration: 220 }
                    }

                    Text {
                        anchors.fill: parent
                        anchors.margins: 14
                        text: signController.confidenceText + "    " + signController.fpsText + "    " + signController.latencyText
                        color: "#586575"
                        font.pixelSize: 18
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
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

        function onsetText() {
            var text = signController.tokensText
            if (text.indexOf("昨天") >= 0)
                return "昨天"
            return "待确认"
        }
    }
}
