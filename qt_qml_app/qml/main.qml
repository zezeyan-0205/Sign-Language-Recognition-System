import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
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
    property var signController
    property var doctorReplyList: []
    property bool doctorReplyPopupVisible: false
    property string doctorReplyPopupText: ""
    property string doctorReplyPopupTime: ""
    property bool patientExpanded: true
    property bool compactPatient: width <= 1100 || height <= 650
    property bool recognizing: signController.currentWord === "识别中..."

    Theme {
        id: theme
    }

    function buttonColor(enabled, activeColor) {
        return enabled ? activeColor : "#aeb8c2"
    }

    function businessState() {
        if (!signController.running)
            return "waiting"
        if (hasFinalSentence())
            return "complete"
        if (signController.collectProgress > 0 || signController.currentWord === "识别中...")
            return "collecting"
        return "ready"
    }

    function stateAccent() {
        return theme.stateColor(businessState())
    }

    function bottomAssistText() {
        var state = businessState()
        if (state === "waiting")
            return "医生端启动后，此处将进入识别就绪状态。"
        if (state === "ready")
            return "点击开始后进行手语表达。"
        if (state === "collecting")
            return "识别错误时可点击删除，完成后点击整理。"
        if (state === "complete")
            return "点击开始后可继续表达下一句，或点击清空重新开始。"
        return "请根据页面状态进行操作。"
    }

    function cleanSentence() {
        var text = signController.sentenceText.replace("整理句子：", "")
        return text.length === 0 || text === "-" ? "待整理" : text
    }

    function hasFinalSentence() {
        var text = cleanSentence()
        return (
            text !== "待整理" &&
            text !== "表达中..." &&
            text !== "正在整理..." &&
            text.indexOf("请先") !== 0 &&
            text.indexOf("暂无") !== 0
        )
    }

    function cleanTokens() {
        var text = signController.tokensText.replace("已收集词汇：", "")
        return text.length === 0 || text === "-" ? "待识别" : text
    }

    function patientHint() {
        if (!signController.running)
            return "等待医生启动系统"
        if (signController.currentWord.indexOf("点击开始按钮") >= 0 ||
            signController.currentWord.indexOf("点击绿色按钮") >= 0)
            return "点击开始按钮开始识别"
        if (signController.currentWord === "等待中...")
            return "请把手放入框内"
        if (signController.currentWord === "识别中...")
            return "请保持动作"
        return "已识别"
    }

    function patientReplyText() {
        if (doctorReplyList.length === 0)
            return "暂无医生回复"
        var lines = []
        for (var i = doctorReplyList.length - 1; i >= 0 && lines.length < 3; --i) {
            var item = doctorReplyList[i]
            lines.push(item.time + " 医生：" + item.text)
        }
        return lines.join("\n")
    }

    function showDoctorReplyPopup(reply) {
        doctorReplyPopupText = reply.text
        doctorReplyPopupTime = reply.time
        doctorReplyPopupVisible = true
    }

    function isRecognizedWord(text) {
        return (
            text.length > 0 &&
            text !== "等待中..." &&
            text !== "识别中..." &&
            text.indexOf("点击开始按钮") < 0 &&
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
        color: "#e3e9ef"

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#edf2f6" }
                GradientStop { position: 1.0; color: "#d7dfe7" }
            }
            opacity: 0.96
        }
    }

    Rectangle {
        id: patientTitleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: compactPatient ? 40 : 48
        color: "#eef4f8"
        border.width: 2
        border.color: theme.blueDeep
        z: 10

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: compactPatient ? 12 : 20
            spacing: 10

            Rectangle {
                width: compactPatient ? 10 : 12
                height: width
                radius: width / 2
                color: stateAccent()
                anchors.verticalCenter: parent.verticalCenter
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Text {
                    text: "患者端手语表达"
                    color: theme.doctorText
                    font.pixelSize: compactPatient ? 16 : 20
                    font.bold: true
                }

                Text {
                    visible: !compactPatient
                    text: "门诊辅助沟通终端"
                    color: theme.doctorMuted
                    font.pixelSize: 12
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 8
            spacing: 8

            MedicalButton {
                width: compactPatient ? 38 : 46
                height: compactPatient ? 28 : 32
                label: "—"
                fontSize: compactPatient ? 15 : 18
                activeColor: "#6f8da5"
                onClicked: patientWindow.showMinimized()
            }
            MedicalButton {
                width: compactPatient ? 38 : 46
                height: compactPatient ? 28 : 32
                label: patientWindow.patientExpanded ? "□" : "▣"
                fontSize: compactPatient ? 15 : 18
                activeColor: "#6f8da5"
                onClicked: {
                    patientWindow.patientExpanded = !patientWindow.patientExpanded
                    patientWindow.placeWindowOnScreen(patientWindow, "DSI", 1, patientWindow.patientExpanded)
                }
            }
            MedicalButton {
                width: compactPatient ? 38 : 46
                height: compactPatient ? 28 : 32
                label: "X"
                fontSize: compactPatient ? 15 : 18
                activeColor: theme.warning
                onClicked: Qt.quit()
            }
        }
    }

    Item {
        anchors.fill: parent
        anchors.leftMargin: compactPatient ? 10 : 18
        anchors.rightMargin: compactPatient ? 10 : 18
        anchors.topMargin: compactPatient ? 50 : 64
        anchors.bottomMargin: compactPatient ? 10 : 16

        // Left clinical preview layer: keep the camera dominant while matching the blue-white-gray theme.
        MedicalCard {
            id: cameraPanel
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.width * 0.50
            cardColor: "#f7fbff"
            gradientTop: "#ffffff"
            gradientBottom: "#edf5fb"
            useGradient: true
            lineColor: theme.doctorLine
            padding: compactPatient ? 8 : 12
            clip: true

            content: [
                Item {
                    anchors.fill: parent

                    CameraView {
                        id: patientCameraView
                        objectName: "patientCameraView"
                        anchors.fill: parent
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: !patientCameraView.hasFrame
                        color: "#000000"

                        Text {
                            id: cameraWaitingText
                            anchors.centerIn: parent
                            width: parent.width - 44
                            text: "摄像头画面等待中"
                            color: "#ffffff"
                            opacity: 0.88
                            font.pixelSize: compactPatient ? 28 : 38
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: compactPatient ? 48 : 66
                        radius: theme.radiusSmall
                        color: "#d9eef9"

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 36
                            text: theme.stateHint(businessState())
                            color: theme.doctorText
                            font.pixelSize: compactPatient ? 16 : 24
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            ]
        }

        Column {
            anchors.left: cameraPanel.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: compactPatient ? 8 : 14
            spacing: compactPatient ? 7 : 12

            // Primary clinical status: this is the first thing patients should read.
            StatusBanner {
                id: currentStateCard
                width: parent.width
                height: compactPatient ? 104 : 150
                title: theme.stateTitle(businessState())
                hint: ""
                value: signController.currentWord
                accent: stateAccent()

                onValueChanged: {
                    if (patientWindow.isRecognizedWord(value))
                        wordRecognizedFlash.restart()
                }

                SequentialAnimation {
                    id: wordRecognizedFlash
                    NumberAnimation { target: currentStateCard; property: "scale"; to: 1.018; duration: 120; easing.type: Easing.OutCubic }
                    NumberAnimation { target: currentStateCard; property: "scale"; to: 1.0; duration: 180; easing.type: Easing.OutCubic }
                }
            }

            // Sentence output uses a calmer card to separate final meaning from live recognition.
            MedicalCard {
                id: sentenceCard
                width: parent.width
                height: compactPatient ? 104 : 150
                cardColor: businessState() === "complete" ? "#dcefe8" : "#e4edf2"
                lineColor: businessState() === "complete" ? "#76b99d" : "#aebfc9"
                padding: compactPatient ? 10 : 16

                content: [
                    Column {
                        anchors.fill: parent
                        spacing: compactPatient ? 4 : 8

                        Text {
                            width: parent.width
                            text: "整理结果"
                            color: theme.doctorMuted
                            font.pixelSize: compactPatient ? 15 : 20
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }

                        Text {
                            id: sentenceResultText
                            width: parent.width
                            height: parent.height - (compactPatient ? 24 : 34)
                            text: cleanSentence()
                            color: theme.textDark
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
                            PropertyAction { target: sentenceResultText; property: "opacity"; value: 0.45 }
                            NumberAnimation { target: sentenceResultText; property: "opacity"; to: 1.0; duration: 360; easing.type: Easing.OutCubic }
                        }
                    }
                ]
            }

            MedicalCard {
                width: parent.width
                height: compactPatient ? 44 : 60
                cardColor: "#f7fbff"
                gradientTop: "#ffffff"
                gradientBottom: "#edf5fb"
                useGradient: true
                lineColor: theme.doctorLine
                padding: compactPatient ? 8 : 12

                content: [
                    Row {
                        anchors.fill: parent
                        spacing: 10

                        StatusPill {
                            width: compactPatient ? 86 : 110
                            height: parent.height
                            text: "识别词"
                            accent: theme.wait
                        }

                        Text {
                            width: parent.width - (compactPatient ? 96 : 120)
                            height: parent.height
                            text: cleanTokens()
                            color: theme.doctorText
                            font.pixelSize: compactPatient ? 16 : 23
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                ]
            }

            Item {
                width: parent.width
                height: compactPatient
                    ? Math.max(4, parent.height - 104 - 104 - 44 - 34 - 136 - 110 - parent.spacing * 7)
                    : Math.max(12, parent.height - 150 - 150 - 60 - 44 - 214 - 150 - parent.spacing * 7)
            }

            MedicalProgressBar {
                width: parent.width
                height: compactPatient ? 34 : 44
                value: signController.collectProgress
                accent: stateAccent()
            }

            Column {
                width: parent.width
                height: compactPatient ? 136 : 214
                spacing: compactPatient ? 8 : 12

                Row {
                    width: parent.width
                    height: compactPatient ? 66 : 104
                    spacing: compactPatient ? 8 : 14

                    MedicalButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: signController.recordingActive ? "暂停" : (signController.recordingPaused ? "继续" : "开始")
                        fontSize: compactPatient ? 24 : 34
                        primaryAction: true
                        activeColor: signController.recordingActive ? "#d89a35" : "#2f9fd0"
                        inactiveColor: theme.disabled
                        enabled: signController.running
                        onClicked: {
                            if (signController.recordingActive) {
                                signController.pauseRecording()
                            } else if (signController.recordingPaused) {
                                signController.resumeRecording()
                            } else {
                                signController.startRecording()
                            }
                        }
                    }
                    MedicalButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: "整理"
                        fontSize: compactPatient ? 24 : 34
                        primaryAction: true
                        activeColor: "#176f9f"
                        inactiveColor: theme.disabled
                        enabled: signController.running
                        onClicked: signController.stopRecording()
                    }
                }

                Row {
                    width: parent.width
                    height: compactPatient ? 62 : 98
                    spacing: compactPatient ? 8 : 14

                    MedicalButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: "删除"
                        fontSize: compactPatient ? 21 : 30
                        activeColor: "#7f8ea0"
                        inactiveColor: theme.disabled
                        enabled: signController.running
                        onClicked: signController.deleteLastToken()
                    }
                    MedicalButton {
                        width: parent.width / 2 - (compactPatient ? 4 : 7)
                        height: parent.height
                        label: "清空"
                        fontSize: compactPatient ? 21 : 30
                        activeColor: "#5f6b76"
                        inactiveColor: theme.disabled
                        enabled: signController.running
                        onClicked: signController.clearExpression()
                    }
                }
            }

            MedicalCard {
                id: patientTipCard
                width: parent.width
                height: compactPatient ? 110 : 150
                cardColor: Qt.rgba(stateAccent().r, stateAccent().g, stateAccent().b, 0.12)
                lineColor: Qt.rgba(stateAccent().r, stateAccent().g, stateAccent().b, 0.38)
                padding: compactPatient ? 10 : 15

                content: [
                    Column {
                        anchors.fill: parent
                        spacing: compactPatient ? 3 : 7

                        Text {
                            width: parent.width
                            text: "医生回复"
                            color: theme.doctorText
                            font.pixelSize: compactPatient ? 15 : 20
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            height: parent.height - (compactPatient ? 22 : 30)
                            text: patientWindow.patientReplyText()
                            color: theme.doctorSubText
                            font.pixelSize: compactPatient ? 12 : 16
                            font.bold: patientWindow.doctorReplyList.length > 0
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                        }
                    }
                ]
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: patientWindow.doctorReplyPopupVisible
        color: "#66000000"
        z: 80

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Rectangle {
            width: compactPatient ? Math.min(parent.width - 80, 620) : Math.min(parent.width - 160, 760)
            height: compactPatient ? 310 : 390
            anchors.centerIn: parent
            radius: 12
            color: "#ffffff"
            border.width: 2
            border.color: "#2d8dc2"

            Column {
                anchors.fill: parent
                anchors.margins: compactPatient ? 22 : 30
                spacing: compactPatient ? 14 : 20

                Text {
                    width: parent.width
                    text: "医生回复"
                    color: "#15324a"
                    font.pixelSize: compactPatient ? 26 : 34
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: patientWindow.doctorReplyPopupTime.length > 0
                          ? patientWindow.doctorReplyPopupTime + " 医生发送"
                          : "医生发送"
                    color: "#6b7b88"
                    font.pixelSize: compactPatient ? 15 : 18
                    horizontalAlignment: Text.AlignHCenter
                }

                Rectangle {
                    width: parent.width
                    height: compactPatient ? 120 : 160
                    radius: 8
                    color: "#f1f7fb"
                    border.width: 1
                    border.color: "#c7ddea"

                    Text {
                        anchors.fill: parent
                        anchors.margins: compactPatient ? 16 : 22
                        text: patientWindow.doctorReplyPopupText.length > 0
                              ? patientWindow.doctorReplyPopupText
                              : "暂无医生回复"
                        color: "#17324d"
                        font.pixelSize: compactPatient ? 24 : 30
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }

                MedicalButton {
                    width: parent.width
                    height: compactPatient ? 58 : 72
                    label: "确认收到"
                    fontSize: compactPatient ? 22 : 28
                    activeColor: "#2d8dc2"
                    primaryAction: true
                    onClicked: patientWindow.doctorReplyPopupVisible = false
                }
            }
        }
    }

    Window {
        id: doctorWindow
        width: 1440
        height: 860
        visible: false
        flags: Qt.Window | Qt.FramelessWindowHint
        title: "医生端电子病历工作台"
        color: theme.bgDoctor
        property bool doctorExpanded: true
        property string manualRecord: ""
        property string recordDraft: ""
        property string recordFilePath: ""
        property string recordStatus: "病历草稿待生成"
        property string lastSavedAt: "尚未保存"
        property string exportedAt: "尚未导出"
        property bool recordDirty: false
        property bool suppressRecordDirty: false
        property string doctorToast: ""
        property string doctorToastMessage: ""
        property bool doctorToastVisible: false
        property bool confirmDialogVisible: false
        property string confirmDialogTitle: ""
        property string confirmDialogMessage: ""
        property string confirmDialogAction: ""
        property var complaintList: []
        property string lastWrittenComplaint: ""
        property string patientName: ""
        property string patientGender: ""
        property string patientAge: ""
        property string recordNo: ""
        property string currentHistory: ""
        property string doctorNote: ""
        property string treatmentPlan: ""
        property string doctorSignature: ""
        property string doctorReplyDraft: ""
        property string currentDepartment: "general"
        property int nextComplaintReplyIndex: 0
        property bool disconnectedWarning: false
        property int navWidth: 108
        property int currentPageIndex: 0
        property int archiveSelectedIndex: 0
        property string archiveSearchText: ""
        property var savedArchiveRecords: []
        property string placeholderPageTitle: "业务页面"
        property string placeholderPageHint: "该功能页已接入导航，后续可继续扩展真实业务内容。"
        property var navItems: [
            { "title": "工作台首页", "icon": "qrc:/qml/icons/nav_home.svg" },
            { "title": "病历档案", "icon": "qrc:/qml/icons/nav_record.svg" },
            { "title": "手语实时识别", "icon": "qrc:/qml/icons/nav_sign.svg" }
        ]

        Component.onCompleted: {
            patientWindow.placeWindowOnScreen(doctorWindow, "HDMI", 0, true)
            refreshArchiveRecords()
        }

        onDoctorToastChanged: {
            if (doctorToast.length > 0) {
                doctorToastMessage = doctorToast
                doctorToastVisible = true
                doctorToastTimer.restart()
            }
        }

        Timer {
            id: doctorToastTimer
            interval: 1000
            repeat: false
            onTriggered: {
                doctorWindow.doctorToastVisible = false
                doctorToastClearTimer.restart()
            }
        }

        Timer {
            id: doctorToastClearTimer
            interval: 240
            repeat: false
            onTriggered: {
                if (!doctorWindow.doctorToastVisible) {
                    doctorWindow.doctorToast = ""
                    doctorWindow.doctorToastMessage = ""
                }
            }
        }

        Connections {
            target: signController

            function onStatusChanged() {
                if (signController.statusText === "错误")
                    doctorWindow.disconnectedWarning = true
                else if (signController.statusText === "已停止" || signController.statusText === "未启动")
                    doctorWindow.disconnectedWarning = false
            }
        }

        Rectangle {
            anchors.fill: parent
            color: theme.bgDoctor

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#f8fcff" }
                    GradientStop { position: 1.0; color: "#e5eff7" }
                }
                opacity: 0.94
            }
        }

        Rectangle {
            id: doctorSidebar
            width: doctorWindow.navWidth
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: "#2f6f9f"
            z: 20

            Column {
                width: doctorWindow.navWidth
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 24
                spacing: 26

                Rectangle {
                    width: 62
                    height: 62
                    radius: 31
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: "#e8f3fb"
                    border.width: 1
                    border.color: "#8cb6d3"

                    Image {
                        anchors.centerIn: parent
                        width: 38
                        height: 38
                        source: "qrc:/qml/icons/nav_doctor.svg"
                        fillMode: Image.PreserveAspectFit
                    }
                }

                Column {
                    width: doctorWindow.navWidth
                    spacing: 12

                    Repeater {
                        model: doctorWindow.navItems

                        SidebarIconButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            iconSource: modelData.icon
                            active: doctorWindow.currentPageIndex === index
                            onClicked: doctorWindow.selectDoctorPage(index)
                        }
                    }
                }
            }

            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 24
                spacing: 12

                SidebarIconButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    iconSource: "qrc:/qml/icons/nav_settings.svg"
                    active: settingsDialog.opened
                    onClicked: settingsDialog.opened = true
                }

                SidebarIconButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    iconSource: "qrc:/qml/icons/nav_power.svg"
                    active: false
                    onClicked: Qt.quit()
                }
            }
        }

        StackView {
            id: doctorStack
            anchors.left: doctorSidebar.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            initialItem: doctorHomeComponent
            replaceEnter: Transition {
                NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 120 }
            }
            replaceExit: Transition {
                NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 80 }
            }
        }

        Row {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 18
            anchors.rightMargin: 18
            height: 42
            spacing: 8
            z: 90

            MedicalButton {
                width: 46
                height: 42
                label: "—"
                fontSize: 18
                activeColor: theme.secondaryButton
                onClicked: doctorWindow.showMinimized()
            }
            MedicalButton {
                width: 46
                height: 42
                label: doctorWindow.doctorExpanded ? "□" : "▣"
                fontSize: 18
                activeColor: theme.secondaryButton
                onClicked: {
                    doctorWindow.doctorExpanded = !doctorWindow.doctorExpanded
                    patientWindow.placeWindowOnScreen(doctorWindow, "HDMI", 0, doctorWindow.doctorExpanded)
                }
            }
            MedicalButton {
                width: 46
                height: 42
                label: "X"
                fontSize: 18
                activeColor: theme.warning
                onClicked: Qt.quit()
            }
        }

        Component {
            id: doctorHomeComponent

            Item {
                anchors.fill: parent

        MedicalCard {
            id: doctorTopBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 18
            height: 86
            cardColor: theme.bgElevated
            gradientTop: theme.bgElevatedTop
            gradientBottom: theme.bgElevatedBottom
            useGradient: true
            lineColor: theme.doctorLine
            padding: 16
            z: 10

            content: [
                Row {
                    anchors.fill: parent
                    spacing: 18

                    Column {
                        width: parent.width * 0.48
                        height: parent.height
                        spacing: 6

                        Row {
                            width: parent.width
                            height: 34
                            spacing: 10

                            Rectangle {
                                width: 13
                                height: 13
                                radius: 7
                                color: doctorWindow.doctorStateColor()
                                anchors.verticalCenter: parent.verticalCenter

                                Behavior on color {
                                    ColorAnimation { duration: 220 }
                                }
                            }

                            Text {
                                width: parent.width - 30
                                text: "医生端电子病历工作台"
                                color: theme.doctorText
                                font.pixelSize: 24
                                font.bold: true
                                elide: Text.ElideRight
                            }
                        }

		                            Text {
		                                width: parent.width
		                                text: "门诊手语沟通病历"
                            visible: true
                            color: theme.doctorSubText
                            font.pixelSize: 17
                            elide: Text.ElideRight
                        }
                    }

	                    Item {
	                        width: parent.width - parent.width * 0.48 - 18
	                        height: parent.height

	                        Text {
	                            anchors.right: departmentSelector.left
	                            anchors.rightMargin: 12
	                            anchors.verticalCenter: parent.verticalCenter
	                            width: Math.max(90, parent.width - homeServiceButtons.width - departmentSelector.width - 218)
	                            text: doctorWindow.homeStatusLine()
	                            color: theme.doctorText
	                            font.pixelSize: 18
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
	                            elide: Text.ElideRight
	                        }

                            Rectangle {
                                id: departmentSelector
                                anchors.right: homeServiceButtons.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                width: 150
                                height: 48
                                radius: 8
                                color: "#eef6fb"
                                border.width: 1
                                border.color: departmentMenu.visible ? "#2f8fb8" : "#bfd0dc"
                                z: 20

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    spacing: 8

                                    Text {
                                        width: parent.width - 24
                                        height: parent.height
                                        text: doctorWindow.departmentTitle(doctorWindow.currentDepartment)
                                        color: "#17324d"
                                        font.pixelSize: 17
                                        font.bold: true
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: 16
                                        height: parent.height
                                        text: departmentMenu.visible ? "▲" : "▼"
                                        color: "#557184"
                                        font.pixelSize: 13
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: departmentMenu.visible = !departmentMenu.visible
                                }
                            }

                            Rectangle {
                                id: departmentMenu
                                anchors.top: departmentSelector.bottom
                                anchors.right: departmentSelector.right
                                anchors.topMargin: 6
                                width: departmentSelector.width
                                height: visible ? 132 : 0
                                radius: 8
                                color: "#ffffff"
                                border.width: 1
                                border.color: "#bfd0dc"
                                visible: false
                                clip: true
                                z: 30

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 4

                                    Repeater {
                                        model: [
                                            { "label": "全科", "value": "general" },
                                            { "label": "消化科", "value": "digestive" },
                                            { "label": "耳鼻喉科", "value": "ent" }
                                        ]

                                        Rectangle {
                                            width: parent.width
                                            height: 36
                                            radius: 7
                                            color: doctorWindow.currentDepartment === modelData.value ? "#dceff8" : "#ffffff"

                                            Text {
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                anchors.rightMargin: 10
                                                text: modelData.label
                                                color: doctorWindow.currentDepartment === modelData.value ? "#176b90" : "#315064"
                                                font.pixelSize: 15
                                                font.bold: doctorWindow.currentDepartment === modelData.value
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: {
                                                    doctorWindow.selectDepartment(modelData.value)
                                                    departmentMenu.visible = false
                                                }
                                            }
                                        }
                                    }
                                }
                            }

	                        Row {
	                            id: homeServiceButtons
                            anchors.right: parent.right
                            anchors.rightMargin: 172
                            anchors.verticalCenter: parent.verticalCenter
                            width: 330
                            height: 48
                            spacing: 10

                            MedicalButton {
                                width: (parent.width - 10) / 2
                                height: parent.height
                                label: "启动识别"
                                fontSize: 20
                                primaryAction: true
                                activeColor: theme.blueDeep
                                enabled: !signController.running
                                onClicked: signController.startSystem()
                            }

                            MedicalButton {
                                width: (parent.width - 10) / 2
                                height: parent.height
                                label: "停止识别"
                                fontSize: 20
                                activeColor: "#a87878"
                                enabled: signController.running
                                opacity: signController.running ? 1.0 : 0.22
                                onClicked: signController.stopSystem()
                            }
                        }
                    }
                }
            ]
        }

        Item {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            anchors.topMargin: 112
            anchors.bottomMargin: 22

            Row {
                anchors.fill: parent
                spacing: 14

                MedicalCard {
                    id: doctorRecordPage
                    width: parent.width * 0.45
                    height: parent.height
                    cardColor: theme.card
                    gradientTop: theme.cardTop
                    gradientBottom: theme.cardBottom
                    useGradient: true
                    lineColor: "#8fa6b5"
                    padding: 24
                    opacity: signController.running ? 1.0 : 0.86

                    Behavior on opacity {
                        NumberAnimation { duration: 220 }
                    }

                    content: [
                        Column {
                            anchors.fill: parent
                            spacing: 12

                            Row {
                                width: parent.width
                                height: 48
                                spacing: 12

                                Column {
                                    width: parent.width
                                    height: parent.height
                                    spacing: 0

                                    Text {
                                        width: parent.width
                                        text: "当前患者主诉"
                                        color: "#17324d"
                                        font.pixelSize: 32
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                }
                            }

                            Rectangle {
                                width: parent.width
                                height: 1
                                color: "#cad6df"
                            }

	                            Column {
	                                width: parent.width
	                                spacing: 8

	                                Text {
	                                    width: parent.width
	                                    text: "患者表达"
                                    color: "#667085"
                                    font.pixelSize: 20
                                    font.bold: true
	                                    elide: Text.ElideRight
	                                }

		                                Rectangle {
		                                    width: parent.width
		                                    height: 270
	                                    radius: 12
	                                    color: "#f6fbff"
	                                    border.width: 1
	                                    border.color: "#bdd3e4"

	                                    Text {
	                                        anchors.fill: parent
	                                        anchors.leftMargin: 20
	                                        anchors.rightMargin: 8
	                                        anchors.topMargin: 24
	                                        anchors.bottomMargin: 18
	                                        text: doctorWindow.chiefComplaintText()
	                                        color: "#0b4f6c"
	                                        font.pixelSize: doctorWindow.hasChiefComplaint() ? 36 : 34
	                                        font.bold: true
	                                        verticalAlignment: Text.AlignTop
	                                        wrapMode: Text.WordWrap
	                                    }
	                                }
	                            }

                            Text {
                                width: parent.width
                                text: "来源：患者手语识别，已整理为自然语言。"
                                color: "#667085"
                                font.pixelSize: 17
                                wrapMode: Text.WordWrap
                            }

			                            MedicalButton {
			                                width: parent.width
			                                height: 66
		                                label: "写入病历"
		                                fontSize: 22
		                                primaryAction: true
		                                activeColor: theme.blueDeep
		                                onClicked: doctorWindow.fillFromComplaint()
		                            }

			                            Row {
			                                width: parent.width
			                                height: 66
		                                spacing: 10

		                                MedicalButton {
		                                    width: (parent.width - parent.spacing * 2) / 3
		                                    height: parent.height
		                                    label: "重新整理"
		                                    fontSize: 18
		                                    activeColor: "#6d7d8a"
		                                    onClicked: doctorWindow.recomposeComplaint()
		                                }

	                                MedicalButton {
		                                    width: (parent.width - parent.spacing * 2) / 3
		                                    height: parent.height
		                                    label: "新建病历"
		                                    fontSize: 18
		                                    activeColor: "#6d7d8a"
		                                    onClicked: doctorWindow.requestNewRecord()
		                                }

	                                Rectangle {
	                                    width: (parent.width - parent.spacing * 2) / 3
	                                    height: parent.height
	                                    radius: 8
	                                    color: "transparent"
	                                    border.width: 1
	                                    border.color: "#c96b6b"
	                                    opacity: clearArea.pressed ? 0.72 : (clearArea.containsMouse ? 1.0 : 0.86)

	                                    Text {
		                                        anchors.centerIn: parent
		                                        text: "清空内容"
		                                        color: "#b75a5a"
		                                        font.pixelSize: 18
		                                        font.bold: true
		                                    }

                                    MouseArea {
                                        id: clearArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: doctorWindow.requestClearAllContent()
                                    }
	                                }
	                            }

					                            Item {
					                                width: parent.width
					                                height: Math.max(0, parent.height - 48 - 1 - 298 - 20 - 66 - 66 - 300 - 84)
					                            }

					                            Rectangle {
					                                width: parent.width
					                                height: 300
			                                radius: 8
			                                color: "#f2f7fb"
	                                border.width: 1
	                                border.color: "#c8d8e4"

	                                Column {
	                                    anchors.fill: parent
	                                    anchors.margins: 12
	                                    spacing: 8

			                                    Text {
			                                        width: parent.width
			                                        text: "医生回复"
			                                        color: "#17324d"
			                                        font.pixelSize: 20
			                                        font.bold: true
			                                        elide: Text.ElideRight
			                                    }

				                                    Row {
			                                        width: parent.width
			                                        height: 58
		                                        spacing: 10

	                                        Rectangle {
	                                            width: parent.width - 130
	                                            height: parent.height
	                                            radius: 8
	                                            color: "#ffffff"
	                                            border.width: 1
	                                            border.color: "#b8cad8"

	                                            TextInput {
	                                                id: doctorReplyInput
	                                                anchors.fill: parent
	                                                anchors.leftMargin: 12
	                                                anchors.rightMargin: 12
		                                                text: doctorWindow.doctorReplyDraft
		                                                color: "#17324d"
		                                                font.pixelSize: 18
	                                                verticalAlignment: TextInput.AlignVCenter
	                                                clip: true
	                                                onTextChanged: doctorWindow.doctorReplyDraft = text
	                                                onAccepted: {
	                                                    if (doctorWindow.doctorReplyDraft.trim().length > 0)
	                                                        doctorWindow.sendDoctorReply()
	                                                }
	                                            }

	                                            Text {
	                                                anchors.fill: parent
	                                                anchors.leftMargin: 12
	                                                anchors.rightMargin: 12
		                                                text: "请输入给患者的文字说明..."
		                                                color: "#8aa0b2"
		                                                font.pixelSize: 17
	                                                verticalAlignment: Text.AlignVCenter
	                                                elide: Text.ElideRight
	                                                visible: doctorReplyInput.text.length === 0
	                                            }
	                                        }

	                                        MedicalButton {
	                                            width: 120
		                                            height: parent.height
		                                            label: "发送给患者"
		                                            fontSize: 16
	                                            activeColor: theme.blueDeep
	                                            inactiveColor: "#b8c4cf"
	                                            enabled: doctorWindow.doctorReplyDraft.trim().length > 0
		                                            onClicked: doctorWindow.sendDoctorReply()
		                                        }
		                                    }

                                                Flow {
                                                    width: parent.width
                                                    height: 78
                                                    spacing: 8

                                                    Repeater {
                                                        model: doctorWindow.departmentTemplates()

                                                        Rectangle {
                                                            width: Math.min(150, Math.max(92, modelData.length * 16 + 28))
                                                            height: 34
                                                            radius: 8
                                                            color: "#e7f0f6"
                                                            border.width: 1
                                                            border.color: "#c4d3de"

                                                            Text {
                                                                anchors.centerIn: parent
                                                                text: modelData
                                                                color: "#315064"
                                                                font.pixelSize: 14
                                                                font.bold: true
                                                            }

                                                            MouseArea {
                                                                anchors.fill: parent
                                                                onClicked: doctorWindow.useDoctorReplyTemplate(modelData)
                                                            }
                                                        }
                                                    }
                                                }

				                                    Text {
				                                        width: parent.width
				                                        height: 36
			                                        text: doctorWindow.latestDoctorReplyText()
			                                        color: "#586575"
			                                        font.pixelSize: 16
	                                        wrapMode: Text.WordWrap
	                                        maximumLineCount: 2
	                                        elide: Text.ElideRight
	                                    }
	                                }
	                            }
                        }
                    ]
                }

	                Item {
	                    width: parent.width - doctorRecordPage.width - parent.spacing
	                    height: parent.height

	                    Rectangle {
		                        id: homePaper
		                        width: parent.width * 0.92
		                        height: parent.height * 0.88
	                        anchors.horizontalCenter: parent.horizontalCenter
	                        anchors.top: parent.top
	                        anchors.topMargin: 4
	                        color: "#ffffff"
	                        radius: 4
	                        border.width: 1
                        border.color: "#dbe2ea"

                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.topMargin: 6
                            z: -1
                            radius: 4
                            color: "#16000000"
                        }

	                        Column {
		                            anchors.fill: parent
		                            anchors.margins: 28
		                            anchors.bottomMargin: 34
		                            spacing: 5

	                            Text {
	                                width: parent.width
	                                text: "门诊手语沟通病历"
	                                color: "#17324d"
	                                font.pixelSize: 29
	                                font.bold: true
	                                horizontalAlignment: Text.AlignHCenter
	                            }

		                            Row {
		                                width: parent.width
		                                height: 24
		                                spacing: 14

			                                Text { text: "姓名："; color: "#34495e"; font.pixelSize: 16; verticalAlignment: Text.AlignVCenter }
			                                Item {
			                                    width: 96
			                                    height: parent.height
			                                    TextInput { anchors.fill: parent; text: doctorWindow.patientName; color: "#1e2f3d"; font.pixelSize: 16; horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter; selectByMouse: true; onTextChanged: { doctorWindow.patientName = text; if (activeFocus) doctorWindow.markRecordDirty() } }
			                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#000000" }
			                                }

			                                Text { text: "性别："; color: "#34495e"; font.pixelSize: 16; verticalAlignment: Text.AlignVCenter }
			                                Item {
			                                    width: 58
			                                    height: parent.height
			                                    TextInput { anchors.fill: parent; text: doctorWindow.patientGender; color: "#1e2f3d"; font.pixelSize: 16; horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter; selectByMouse: true; onTextChanged: { doctorWindow.patientGender = text; if (activeFocus) doctorWindow.markRecordDirty() } }
			                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#000000" }
			                                }

			                                Text { text: "年龄："; color: "#34495e"; font.pixelSize: 16; verticalAlignment: Text.AlignVCenter }
			                                Item {
			                                    width: 58
			                                    height: parent.height
			                                    TextInput { anchors.fill: parent; text: doctorWindow.patientAge; color: "#1e2f3d"; font.pixelSize: 16; horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter; selectByMouse: true; onTextChanged: { doctorWindow.patientAge = text; if (activeFocus) doctorWindow.markRecordDirty() } }
			                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#000000" }
			                                }

			                                Text { text: "病历号："; color: "#34495e"; font.pixelSize: 16; verticalAlignment: Text.AlignVCenter }
			                                Item {
			                                    width: 112
			                                    height: parent.height
			                                    TextInput { anchors.fill: parent; text: doctorWindow.recordNo; color: "#1e2f3d"; font.pixelSize: 16; horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter; selectByMouse: true; onTextChanged: { doctorWindow.recordNo = text; if (activeFocus) doctorWindow.markRecordDirty() } }
			                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#000000" }
			                                }
		                            }

	                            Text {
	                                width: parent.width
	                                text: "科室：门诊      就诊类型：初诊      日期：" + doctorWindow.currentDateText()
	                                color: "#34495e"
	                                font.pixelSize: 17
	                            }

		                            Rectangle { width: parent.width; height: 1; color: "#000000" }

                            Text {
                                width: parent.width
                                text: "患者主诉："
                                color: "#17324d"
                                font.pixelSize: 19
                                font.bold: true
                            }

		                            Item {
		                                width: parent.width
		                                height: 172

			                                Text {
			                                    anchors.fill: parent
			                                    text: doctorWindow.paperChiefComplaintText()
			                                    color: doctorWindow.hasRecordComplaint() ? "#1e2f3d" : "#9aaaba"
			                                    font.pixelSize: 15
			                                    font.bold: false
			                                    lineHeightMode: Text.FixedHeight
			                                    lineHeight: 20
			                                    wrapMode: Text.WordWrap
			                                    clip: true
			                                }

		                                Repeater {
		                                    model: 8

		                                    Rectangle {
		                                        width: parent.width
		                                        height: 1
		                                        y: 22 + index * 20
		                                        color: "#000000"
		                                    }
		                                }
		                            }

		                            Rectangle { width: parent.width; height: 1; color: "#000000" }

			                            Text { width: parent.width; text: "现病史："; color: "#17324d"; font.pixelSize: 19; font.bold: true }
			                            Item {
			                                width: parent.width
			                                height: 92

			                                Repeater {
			                                    model: 4

			                                    Rectangle {
			                                        width: parent.width
		                                        height: 1
		                                        y: 22 + index * 20
			                                        color: "#000000"
			                                    }
			                                }

			                                TextEdit {
			                                    id: currentHistoryEdit
			                                    anchors.fill: parent
			                                    text: doctorWindow.currentHistory
			                                    color: "#1e2f3d"
			                                    font.pixelSize: 15
			                                    horizontalAlignment: TextEdit.AlignLeft
			                                    wrapMode: TextEdit.Wrap
			                                    selectByMouse: true
			                                    clip: true
			                                    onTextChanged: { doctorWindow.currentHistory = text; if (activeFocus) doctorWindow.markRecordDirty() }
			                                }

			                                Text {
			                                    anchors.left: parent.left
			                                    anchors.right: parent.right
			                                    anchors.top: parent.top
			                                    text: "待医生补充"
			                                    color: "#9aaaba"
			                                    font.pixelSize: 16
			                                    horizontalAlignment: Text.AlignLeft
			                                    visible: currentHistoryEdit.text.length === 0
			                                }
			                            }

		                            Text { width: parent.width; text: "医生备注："; color: "#17324d"; font.pixelSize: 19; font.bold: true }
			                            Item {
			                                width: parent.width
			                                height: 70

		                                Repeater {
		                                    model: 3

			                                    Rectangle {
		                                        width: parent.width
		                                        height: 1
		                                        y: 22 + index * 22
			                                        color: "#000000"
			                                    }
		                                }

		                                TextEdit {
		                                    id: doctorNoteEdit
		                                    anchors.fill: parent
		                                    text: doctorWindow.doctorNote
		                                    color: "#1e2f3d"
		                                    font.pixelSize: 15
		                                    horizontalAlignment: TextEdit.AlignLeft
		                                    wrapMode: TextEdit.Wrap
		                                    selectByMouse: true
		                                    clip: true
		                                    onTextChanged: { doctorWindow.doctorNote = text; if (activeFocus) doctorWindow.markRecordDirty() }
		                                }

		                                Text {
		                                    anchors.left: parent.left
		                                    anchors.right: parent.right
		                                    anchors.top: parent.top
		                                    text: "待医生补充"
		                                    color: "#9aaaba"
		                                    font.pixelSize: 16
		                                    horizontalAlignment: Text.AlignLeft
		                                    visible: doctorNoteEdit.text.length === 0
		                                }
		                            }

			                            Text { width: parent.width; text: "处理意见："; color: "#17324d"; font.pixelSize: 19; font.bold: true }
			                            Item {
			                                width: parent.width
			                                height: 70

			                                Repeater {
			                                    model: 3

		                                    Rectangle {
		                                        width: parent.width
		                                        height: 1
		                                        y: 22 + index * 22
			                                        color: "#000000"
			                                    }
			                                }

			                                TextEdit {
			                                    id: treatmentPlanEdit
			                                    anchors.fill: parent
			                                    text: doctorWindow.treatmentPlan
			                                    color: "#1e2f3d"
			                                    font.pixelSize: 15
			                                    horizontalAlignment: TextEdit.AlignLeft
			                                    wrapMode: TextEdit.Wrap
			                                    selectByMouse: true
			                                    clip: true
			                                    onTextChanged: { doctorWindow.treatmentPlan = text; if (activeFocus) doctorWindow.markRecordDirty() }
			                                }

			                                Text {
			                                    anchors.left: parent.left
			                                    anchors.right: parent.right
			                                    anchors.top: parent.top
			                                    text: "待医生补充"
			                                    color: "#9aaaba"
			                                    font.pixelSize: 16
			                                    horizontalAlignment: Text.AlignLeft
			                                    visible: treatmentPlanEdit.text.length === 0
			                                }
			                            }

		                            Item { width: parent.width; height: 10 }

		                            Row {
		                                width: parent.width
		                                height: 28
		                                spacing: 8

			                                Text {
			                                    width: 78
			                                    text: "医生签名："
			                                    color: "#34495e"
			                                    font.pixelSize: 16
			                                    verticalAlignment: Text.AlignVCenter
			                                }

			                                Item {
			                                    width: 130
			                                    height: parent.height
		                                    TextInput {
		                                        anchors.fill: parent
		                                        text: doctorWindow.doctorSignature
		                                        color: "#1e2f3d"
		                                        font.pixelSize: 16
		                                        horizontalAlignment: TextInput.AlignHCenter
		                                        verticalAlignment: TextInput.AlignVCenter
		                                        selectByMouse: true
		                                        onTextChanged: { doctorWindow.doctorSignature = text; if (activeFocus) doctorWindow.markRecordDirty() }
		                                    }
		                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#000000" }
			                                }

			                                Text {
			                                    width: parent.width - 78 - 130 - parent.spacing * 2
			                                    text: "日期：" + doctorWindow.currentDateText()
			                                    color: "#34495e"
	                                    font.pixelSize: 16
	                                    horizontalAlignment: Text.AlignRight
	                                    verticalAlignment: Text.AlignVCenter
	                                }
	                            }

	                        }
	                    }

	                    Row {
	                        anchors.horizontalCenter: homePaper.horizontalCenter
	                        anchors.top: homePaper.bottom
	                        anchors.topMargin: 16
	                        width: homePaper.width
	                        height: 48
	                        spacing: 12

                        MedicalButton {
                            width: (parent.width - parent.spacing) / 2
                            height: parent.height
                            label: "保存病历"
                            fontSize: 20
                            activeColor: theme.blueDeep
                            onClicked: doctorWindow.saveRecord()
                        }

                        MedicalButton {
                            width: (parent.width - parent.spacing) / 2
	                            height: parent.height
	                            label: "导出病历"
	                            fontSize: 18
	                            activeColor: "#9cafbd"
	                            onClicked: doctorWindow.exportRecord()
	                        }
                    }
                }
            }
        }

            }
        }

        Component {
            id: doctorRecordsComponent

            Item {
                anchors.fill: parent

                Row {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 18

                    MedicalCard {
                        width: parent.width * 0.35
                        height: parent.height
                        cardColor: theme.bgElevated
                        gradientTop: theme.bgElevatedTop
                        gradientBottom: theme.bgElevatedBottom
                        useGradient: true
                        lineColor: theme.doctorLine
                        padding: 22

                        content: [
                            Column {
                                anchors.fill: parent
                                spacing: 16

                                Row {
                                    width: parent.width
                                    height: 52
                                    spacing: 12

                                    Column {
                                        width: parent.width - 132
                                        height: parent.height
                                        spacing: 3

                                        Text {
                                            width: parent.width
                                            text: "病历档案"
                                            color: theme.doctorText
                                            font.pixelSize: 30
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            width: parent.width
                                            text: "当前草稿与历史记录"
                                            color: theme.doctorSubText
                                            font.pixelSize: 16
                                            elide: Text.ElideRight
                                        }
                                    }

                                    MedicalButton {
                                        width: 120
                                        height: 46
                                        label: "新建病历"
                                        fontSize: 16
                                        activeColor: theme.blueDeep
                                        onClicked: doctorWindow.requestNewRecord()
                                    }
                                }

                                Rectangle {
                                    width: parent.width
                                    height: 132
                                    radius: 12
                                    color: "#ffffff"
                                    border.width: 1
                                    border.color: "#bad2e5"

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 16
                                        spacing: 8

                                        Text { width: parent.width; text: "当前草稿"; color: "#17324d"; font.pixelSize: 20; font.bold: true; elide: Text.ElideRight }
                                        Text { width: parent.width; text: doctorWindow.recordStatus; color: "#667085"; font.pixelSize: 15; elide: Text.ElideRight }
                                        Text { width: parent.width; text: "主诉：" + doctorWindow.paperChiefComplaintText(); color: "#20303f"; font.pixelSize: 17; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: "历史记录"
                                    color: theme.doctorText
                                    font.pixelSize: 22
                                    font.bold: true
                                }

                                Repeater {
                                    model: [
                                        { "time": doctorWindow.currentDateText() + " " + doctorWindow.timeText(), "complaint": doctorWindow.paperChiefComplaintText(), "status": doctorWindow.lastSavedAt === "尚未保存" ? "待保存" : "已保存" },
                                        { "time": "2026-06-20 09:12", "complaint": "我头疼。", "status": "已导出" }
                                    ]

                                    Rectangle {
                                        width: parent.width
                                        height: 112
                                        radius: 12
                                        color: "#f7fbff"
                                        border.width: 1
                                        border.color: "#d7e1e8"

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            spacing: 6

                                            Text { width: parent.width; text: modelData.time; color: "#667085"; font.pixelSize: 14; elide: Text.ElideRight }
                                            Text { width: parent.width; text: "门诊手语沟通病历"; color: "#17324d"; font.pixelSize: 18; font.bold: true; elide: Text.ElideRight }
                                            Text { width: parent.width; text: "主诉：" + modelData.complaint; color: "#374151"; font.pixelSize: 15; maximumLineCount: 2; wrapMode: Text.WordWrap; elide: Text.ElideRight }
                                            Text { width: parent.width; text: "状态：" + modelData.status; color: "#2386a7"; font.pixelSize: 14; elide: Text.ElideRight }
                                        }
                                    }
                                }
                            }
                        ]
                    }

                    Item {
                        width: parent.width * 0.65 - parent.spacing
                        height: parent.height

                        Rectangle {
                            id: archivePaper
                            width: Math.min(parent.width - 60, parent.height * 0.72)
                            height: parent.height - 82
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            color: "#ffffff"
                            radius: 4
                            border.width: 1
                            border.color: "#d6dde5"

                            Column {
                                anchors.fill: parent
                                anchors.margins: 34
                                spacing: 14

                                Text { width: parent.width; text: "门诊手语沟通病历"; color: "#17324d"; font.pixelSize: 28; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                                Text { width: parent.width; text: "姓名：______    性别：______    年龄：______"; color: "#34495e"; font.pixelSize: 17 }
                                Text { width: parent.width; text: "科室：门诊      日期：" + doctorWindow.currentDateText(); color: "#34495e"; font.pixelSize: 17 }
                                Rectangle { width: parent.width; height: 1; color: "#cfd8e3" }
                                Text { width: parent.width; text: "患者主诉："; color: "#17324d"; font.pixelSize: 19; font.bold: true }
	                                Text { width: parent.width; height: 90; text: doctorWindow.paperChiefComplaintText(); color: doctorWindow.hasRecordComplaint() ? "#20303f" : "#aab6c2"; font.pixelSize: 15; lineHeightMode: Text.FixedHeight; lineHeight: 20; clip: true; wrapMode: Text.WordWrap }
                                Rectangle { width: parent.width; height: 1; color: "#cfd8e3" }
                                Text { width: parent.width; text: "现病史："; color: "#17324d"; font.pixelSize: 19; font.bold: true }
                                Rectangle { width: parent.width; height: 1; color: "#dfe6ee" }
                                Rectangle { width: parent.width; height: 1; color: "#dfe6ee" }
                                Rectangle { width: parent.width; height: 1; color: "#dfe6ee" }
                                Text { width: parent.width; text: "医生备注："; color: "#17324d"; font.pixelSize: 19; font.bold: true }
                                Rectangle { width: parent.width; height: 1; color: "#dfe6ee" }
                                Rectangle { width: parent.width; height: 1; color: "#dfe6ee" }
                            }
                        }

                        Row {
                            anchors.horizontalCenter: archivePaper.horizontalCenter
                            anchors.top: archivePaper.bottom
                            anchors.topMargin: 16
                            width: archivePaper.width
                            height: 56
                            spacing: 12

                            MedicalButton { width: (parent.width - parent.spacing * 2) / 3; height: parent.height; label: "继续编辑"; fontSize: 18; activeColor: theme.secondaryButton; onClicked: doctorWindow.selectDoctorPage(0) }
                            MedicalButton { width: (parent.width - parent.spacing * 2) / 3; height: parent.height; label: "保存病历"; fontSize: 18; activeColor: theme.blueDeep; onClicked: doctorWindow.saveRecord() }
                            MedicalButton { width: (parent.width - parent.spacing * 2) / 3; height: parent.height; label: "导出病历"; fontSize: 18; activeColor: theme.secondaryButton; onClicked: doctorWindow.exportRecord() }
                        }
                    }
                }
            }
        }

        Component {
            id: doctorRecordsWorkbookComponent

            Item {
                anchors.fill: parent

                Column {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    MedicalCard {
                        width: parent.width - 190
                        height: 82
                        cardColor: theme.bgElevated
                        gradientTop: theme.bgElevatedTop
                        gradientBottom: theme.bgElevatedBottom
                        useGradient: true
                        lineColor: theme.doctorLine
                        padding: 16

                        content: [
                            Row {
                                anchors.fill: parent
                                spacing: 18

                                Column {
                                    width: parent.width - 288
                                    height: parent.height
                                    spacing: 4

                                    Text {
                                        width: parent.width
                                        text: "病历档案"
                                        color: theme.doctorText
                                        font.pixelSize: 28
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: "管理当前草稿与历史病历记录"
                                        color: theme.doctorSubText
                                        font.pixelSize: 16
                                        elide: Text.ElideRight
                                    }
                                }

                                Rectangle {
                                    width: 270
                                    height: 48
                                    radius: 10
                                    color: "#ffffff"
                                    border.width: 1
                                    border.color: "#c8d8e5"
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 14
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "搜索主诉或日期"
                                        color: "#9aa8b5"
                                        font.pixelSize: 15
                                        visible: archiveSearchInput.text.length === 0
                                    }

                                    TextInput {
                                        id: archiveSearchInput
                                        anchors.fill: parent
                                        anchors.leftMargin: 14
                                        anchors.rightMargin: 14
                                        text: doctorWindow.archiveSearchText
                                        color: "#17324d"
                                        font.pixelSize: 16
                                        verticalAlignment: TextInput.AlignVCenter
                                        clip: true
                                        onTextChanged: doctorWindow.archiveSearchText = text
                                    }
                                }

                            }
                        ]
                    }

                    Row {
                        width: parent.width
                        height: parent.height - 96
                        spacing: 18

                        MedicalCard {
                            width: parent.width * 0.40
                            height: parent.height
                            cardColor: theme.bgElevated
                            gradientTop: theme.bgElevatedTop
                            gradientBottom: theme.bgElevatedBottom
                            useGradient: true
                            lineColor: theme.doctorLine
                            padding: 18

                            content: [
                                Column {
                                    anchors.fill: parent
                                    spacing: 12

                                    Text {
                                        width: parent.width
                                        text: "当前草稿"
                                        color: theme.doctorText
                                        font.pixelSize: 20
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        width: parent.width
                                        height: 112
                                        radius: 12
                                        color: doctorWindow.archiveEffectiveIndex() === 0 ? "#e8f3fb" : "#ffffff"
                                        border.width: 1
                                        border.color: doctorWindow.archiveEffectiveIndex() === 0 ? "#8cb6d3" : "#d3e0ea"

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 5
                                            radius: 3
                                            color: theme.blueDeep
                                            visible: doctorWindow.archiveEffectiveIndex() === 0
                                        }

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            anchors.leftMargin: 18
                                            spacing: 7

                                            Text {
                                                width: parent.width
                                                text: doctorWindow.hasArchiveDraft() ? "状态：" + doctorWindow.archiveDraftStatus() : "暂无未保存病历"
                                                color: "#17324d"
                                                font.pixelSize: 17
                                                font.bold: true
                                                elide: Text.ElideRight
                                            }

                                            Text {
                                                width: parent.width
                                                text: doctorWindow.hasArchiveDraft() ? "生成时间：" + doctorWindow.archiveDraftTime() : "点击右上角新建病历，或等待患者表达后写入。"
                                                color: "#667085"
                                                font.pixelSize: 14
                                                elide: Text.ElideRight
                                            }

                                            Text {
                                                width: parent.width
                                                text: doctorWindow.hasArchiveDraft() ? doctorWindow.archiveDraftSummary() : ""
                                                color: "#20303f"
                                                font.pixelSize: 15
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: doctorWindow.selectArchiveRecord(0)
                                        }
                                    }

                                    Text {
                                        width: parent.width
                                        text: "历史病历"
                                        color: theme.doctorText
                                        font.pixelSize: 20
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        width: parent.width
                                        height: 34
                                        radius: 8
                                        color: "#eaf2f8"
                                        border.width: 1
                                        border.color: "#d2e0eb"

                                        Row {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12

                                            Text { width: parent.width * 0.32; height: parent.height; text: "时间"; color: "#526678"; font.pixelSize: 13; font.bold: true; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { width: parent.width * 0.20; height: parent.height; text: "状态"; color: "#526678"; font.pixelSize: 13; font.bold: true; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            Text { width: parent.width * 0.48; height: parent.height; text: "主诉摘要"; color: "#526678"; font.pixelSize: 13; font.bold: true; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                        }
                                    }

                                    Flickable {
                                        id: archiveHistoryList
                                        width: parent.width
                                        height: parent.height - 112 - 34 - 72
                                        clip: true
                                        contentHeight: archiveHistoryColumn.height
                                        boundsBehavior: Flickable.StopAtBounds

                                        Column {
                                            id: archiveHistoryColumn
                                            width: archiveHistoryList.width
                                            spacing: 8

                                            Repeater {
                                                model: doctorWindow.archiveHistoryRecords()

                                                Rectangle {
                                                    property bool rowHovered: false
                                                    width: archiveHistoryColumn.width
                                                    height: 58
                                                    radius: 9
                                                    color: doctorWindow.archiveEffectiveIndex() === index + 1 ? "#e3f0fa" : (rowHovered ? "#f4f9fd" : "#ffffff")
                                                    border.width: 1
                                                    border.color: doctorWindow.archiveEffectiveIndex() === index + 1 ? "#9fc3dc" : "#d9e4ec"

                                                    Rectangle {
                                                        anchors.left: parent.left
                                                        anchors.top: parent.top
                                                        anchors.bottom: parent.bottom
                                                        width: 4
                                                        radius: 2
                                                        color: theme.blueDeep
                                                        visible: doctorWindow.archiveEffectiveIndex() === index + 1
                                                    }

                                                    Row {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 12
                                                        anchors.rightMargin: 12

                                                        Text {
                                                            width: parent.width * 0.32
                                                            height: parent.height
                                                            text: modelData.time
                                                            color: doctorWindow.archiveEffectiveIndex() === index + 1 ? "#17324d" : "#667085"
                                                            font.pixelSize: 13
                                                            verticalAlignment: Text.AlignVCenter
                                                            elide: Text.ElideRight
                                                        }

                                                        Text {
                                                            width: parent.width * 0.20
                                                            height: parent.height
                                                            text: modelData.status
                                                            color: doctorWindow.archiveEffectiveIndex() === index + 1 ? theme.blueDeep : "#2386a7"
                                                            font.pixelSize: 13
                                                            font.bold: doctorWindow.archiveEffectiveIndex() === index + 1
                                                            verticalAlignment: Text.AlignVCenter
                                                            elide: Text.ElideRight
                                                        }

                                                        Text {
                                                            width: parent.width * 0.48
                                                            height: parent.height
                                                            text: modelData.complaint
                                                            color: doctorWindow.archiveEffectiveIndex() === index + 1 ? "#17324d" : "#374151"
                                                            font.pixelSize: 14
                                                            verticalAlignment: Text.AlignVCenter
                                                            elide: Text.ElideRight
                                                        }
                                                    }

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        hoverEnabled: true
                                                        onClicked: doctorWindow.selectArchiveRecord(index + 1)
                                                        onEntered: rowHovered = true
                                                        onExited: rowHovered = false
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            ]
                        }

                        Item {
                            width: parent.width * 0.60 - parent.spacing
                            height: parent.height

                            Rectangle {
                                anchors.centerIn: archivePaper
                                width: archivePaper.width + 12
                                height: archivePaper.height + 12
                                radius: 7
                                color: "#14000000"
                            }

                            Rectangle {
                                id: archivePaper
                                width: Math.min(parent.width - 34, parent.height * 0.72)
                                height: parent.height - 76
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                color: "#ffffff"
                                radius: 5
                                border.width: 1
                                border.color: "#d6dde5"

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 34
                                    spacing: 11

                                    Text { width: parent.width; text: "门诊手语沟通病历"; color: "#17324d"; font.pixelSize: 27; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                                    Text {
                                        width: parent.width
                                        text: "姓名：" + doctorWindow.archiveField("name", "______") +
                                              "    性别：" + doctorWindow.archiveField("gender", "______") +
                                              "    年龄：" + doctorWindow.archiveField("age", "______") +
                                              "    病历号：" + doctorWindow.archiveField("record_no", "______")
                                        color: "#34495e"
                                        font.pixelSize: 16
                                        elide: Text.ElideRight
                                    }
                                    Text { width: parent.width; text: "科室：门诊      日期：" + doctorWindow.archiveSelectedDate(); color: "#34495e"; font.pixelSize: 16 }
                                    Rectangle { width: parent.width; height: 1; color: "#d9e1ea" }
                                    Text { width: parent.width; text: "患者主诉："; color: "#17324d"; font.pixelSize: 18; font.bold: true }

	                                    Text {
	                                        width: parent.width
	                                        height: 92
	                                        text: doctorWindow.archiveSelectedComplaint()
	                                        color: doctorWindow.archiveHasSelection() ? "#20303f" : "#aab6c2"
	                                        font.pixelSize: 15
	                                        lineHeightMode: Text.FixedHeight
	                                        lineHeight: 20
	                                        clip: true
	                                        wrapMode: Text.WordWrap
	                                    }

                                    Rectangle { width: parent.width; height: 1; color: "#d9e1ea" }
                                    Text { width: parent.width; text: "现病史："; color: "#17324d"; font.pixelSize: 18; font.bold: true }
                                    Text {
                                        width: parent.width
                                        height: 50
                                        text: doctorWindow.archiveField("current_history", "待医生补充")
                                        color: doctorWindow.archiveField("current_history", "").length > 0 ? "#20303f" : "#a8b4bf"
                                        font.pixelSize: 16
                                        wrapMode: Text.WordWrap
                                        clip: true
                                    }
                                    Rectangle { width: parent.width; height: 1; color: "#e1e8ef" }
                                    Rectangle { width: parent.width; height: 1; color: "#e1e8ef" }
                                    Text { width: parent.width; text: "医生备注："; color: "#17324d"; font.pixelSize: 18; font.bold: true }
                                    Text {
                                        width: parent.width
                                        height: 50
                                        text: doctorWindow.archiveField("doctor_note", "待医生补充")
                                        color: doctorWindow.archiveField("doctor_note", "").length > 0 ? "#20303f" : "#a8b4bf"
                                        font.pixelSize: 16
                                        wrapMode: Text.WordWrap
                                        clip: true
                                    }
                                    Rectangle { width: parent.width; height: 1; color: "#e1e8ef" }
                                    Rectangle { width: parent.width; height: 1; color: "#e1e8ef" }
                                    Text { width: parent.width; text: "处理意见："; color: "#17324d"; font.pixelSize: 18; font.bold: true }
                                    Text {
                                        width: parent.width
                                        height: 50
                                        text: doctorWindow.archiveField("treatment_plan", "待医生补充")
                                        color: doctorWindow.archiveField("treatment_plan", "").length > 0 ? "#20303f" : "#a8b4bf"
                                        font.pixelSize: 16
                                        wrapMode: Text.WordWrap
                                        clip: true
                                    }
                                    Rectangle { width: parent.width; height: 1; color: "#e1e8ef" }
                                    Text {
                                        width: parent.width
                                        text: "医生签名：" + doctorWindow.archiveField("doctor_signature", "______")
                                        color: "#34495e"
                                        font.pixelSize: 16
                                    }
                                }
                            }

                            Row {
                                anchors.horizontalCenter: archivePaper.horizontalCenter
                                anchors.top: archivePaper.bottom
                                anchors.topMargin: 12
                                width: archivePaper.width
                                height: 54
                                spacing: 10

                                MedicalButton { width: (parent.width - parent.spacing * 3) / 4; height: parent.height; label: "继续编辑"; fontSize: 16; activeColor: theme.secondaryButton; onClicked: doctorWindow.loadArchiveRecordForEditing() }
                                MedicalButton { width: (parent.width - parent.spacing * 3) / 4; height: parent.height; label: "保存病历"; fontSize: 16; activeColor: theme.blueDeep; primaryAction: true; onClicked: doctorWindow.saveArchiveSelection() }
                                MedicalButton { width: (parent.width - parent.spacing * 3) / 4; height: parent.height; label: "导出病历"; fontSize: 16; activeColor: theme.secondaryButton; onClicked: doctorWindow.exportArchiveSelection() }
                                MedicalButton { width: (parent.width - parent.spacing * 3) / 4; height: parent.height; label: "删除记录"; fontSize: 16; activeColor: "#a87878"; onClicked: doctorWindow.requestDeleteArchiveRecord() }
                            }
                        }
                    }
                }
            }
        }

        Component {
            id: doctorSignLiveComponent

            Item {
                anchors.fill: parent

                Row {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 18

                    MedicalCard {
                        width: parent.width * 0.66
                        height: parent.height
                        cardColor: "#ddeaf3"
                        gradientTop: "#e9f3fa"
                        gradientBottom: "#d8e6f0"
                        useGradient: true
                        lineColor: "#9cbfd8"
                        padding: 10

                        content: [
                            Item {
                                anchors.fill: parent

                                CameraView {
                                    id: doctorCameraView
                                    objectName: "doctorCameraView"
                                    anchors.fill: parent

                                    Component.onCompleted: {
                                        signController.bindDoctorCameraView(doctorCameraView)
                                    }

                                    Component.onDestruction: {
                                        signController.bindDoctorCameraView(null)
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 10
                                    color: "#000000"
                                    visible: !doctorCameraView.hasFrame
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: doctorCameraView.hasFrame ? "" : "患者实时画面等待中"
                                    color: "#ffffff"
                                    opacity: 0.9
                                    font.pixelSize: 34
                                    font.bold: true
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 54
                                    color: "#dd1f5f86"
                                    radius: 8

                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 18
                                        anchors.rightMargin: 18
                                        text: "患者实时画面 · 手语实时识别"
                                        color: "#ffffff"
                                        font.pixelSize: 20
                                        font.bold: true
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        ]
                    }

                    Column {
                        width: parent.width * 0.34 - parent.spacing
                        height: parent.height
                        spacing: 16

                        MedicalCard {
                            width: parent.width
                            height: 150
                            cardColor: theme.bgElevated
                            gradientTop: theme.bgElevatedTop
                            gradientBottom: theme.bgElevatedBottom
                            useGradient: true
                            lineColor: theme.doctorLine
                            padding: 18

                            content: [
                                Column {
                                    anchors.fill: parent
                                    spacing: 12

                                    Text {
                                        width: parent.width
                                        text: "实时识别"
                                        color: theme.doctorText
                                        font.pixelSize: 24
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: signController.currentWord
                                        color: theme.blueDeep
                                        font.pixelSize: 42
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: signController.confidenceText
                                        color: theme.doctorSubText
                                        font.pixelSize: 17
                                        elide: Text.ElideRight
                                    }
                                }
                            ]
                        }

                        MedicalCard {
                            width: parent.width
                            height: 170
                            cardColor: theme.bgElevated
                            gradientTop: theme.bgElevatedTop
                            gradientBottom: theme.bgElevatedBottom
                            useGradient: true
                            lineColor: theme.doctorLine
                            padding: 18

                            content: [
                                Column {
                                    anchors.fill: parent
                                    spacing: 10

                                    Text {
                                        width: parent.width
                                        text: "识别词流"
                                        color: theme.doctorText
                                        font.pixelSize: 22
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        height: parent.height - 34
                                        text: patientWindow.cleanTokens()
                                        color: theme.doctorSubText
                                        font.pixelSize: 24
                                        font.bold: true
                                        wrapMode: Text.WordWrap
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            ]
                        }

                        MedicalCard {
                            width: parent.width
                            height: parent.height - 150 - 170 - parent.spacing * 2
                            cardColor: theme.bgElevated
                            gradientTop: theme.bgElevatedTop
                            gradientBottom: theme.bgElevatedBottom
                            useGradient: true
                            lineColor: theme.doctorLine
                            padding: 18

                            content: [
                                Column {
                                    anchors.fill: parent
                                    spacing: 16

                                    Text {
                                        width: parent.width
                                        text: "运行指标"
                                        color: theme.doctorText
                                        font.pixelSize: 22
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    StatusPill {
                                        width: parent.width
                                        height: 56
                                        text: doctorWindow.doctorStateText()
                                        accent: doctorWindow.doctorStateColor()
                                    }

                                    Text {
                                        width: parent.width
                                        text: signController.fpsText
                                        color: theme.blueDeep
                                        font.pixelSize: 24
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: signController.latencyText
                                        color: doctorWindow.latencyColor()
                                        font.pixelSize: 20
                                        font.bold: true
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        width: parent.width
                                        text: signController.composerText
                                        color: theme.doctorMuted
                                        font.pixelSize: 18
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            ]
                        }
                    }
                }
            }
        }

        Component {
            id: doctorPlaceholderComponent

            Item {
                id: placeholderPage
                anchors.fill: parent

                MedicalCard {
                    anchors.fill: parent
                    anchors.margins: 22
                    cardColor: theme.bgElevated
                    gradientTop: theme.bgElevatedTop
                    gradientBottom: theme.bgElevatedBottom
                    useGradient: true
                    lineColor: theme.doctorLine
                    padding: 34

                    content: [
                        Column {
                            anchors.fill: parent
                            spacing: 18

                            Text {
                                width: parent.width
                                text: doctorWindow.placeholderPageTitle
                                color: theme.doctorText
                                font.pixelSize: 34
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: doctorWindow.placeholderPageHint
                                color: theme.doctorSubText
                                font.pixelSize: 20
                                wrapMode: Text.WordWrap
                            }

                            Rectangle {
                                width: parent.width
                                height: 1
                                color: theme.doctorLine
                            }

                            Row {
                                width: parent.width
                                height: 72
                                spacing: 14

                                Repeater {
                                    model: doctorWindow.placeholderActions(doctorWindow.currentPageIndex)

                                    MedicalButton {
                                        width: (parent.width - parent.spacing * 2) / 3
                                        height: parent.height
                                        label: modelData.label
                                        fontSize: 20
                                        activeColor: modelData.color
                                        enabled: modelData.enabled
                                        onClicked: doctorWindow.handlePlaceholderAction(modelData.action)
                                    }
                                }
                            }

                            Row {
                                width: parent.width
                                height: 132
                                spacing: 16

                                Repeater {
                                    model: doctorWindow.placeholderMetrics(doctorWindow.currentPageIndex)

                                    MedicalCard {
                                        width: (parent.width - parent.spacing * 2) / 3
                                        height: parent.height
                                        cardColor: "#f7fbff"
                                        gradientTop: "#ffffff"
                                        gradientBottom: "#edf5fb"
                                        useGradient: true
                                        lineColor: theme.doctorLine
                                        padding: 18

                                        content: [
                                            Column {
                                                anchors.fill: parent
                                                spacing: 10

                                                Text {
                                                    width: parent.width
                                                    text: modelData.title
                                                    color: theme.doctorMuted
                                                    font.pixelSize: 17
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    width: parent.width
                                                    text: modelData.value
                                                    color: theme.doctorText
                                                    font.pixelSize: 26
                                                    font.bold: true
                                                    wrapMode: Text.WordWrap
                                                }
                                            }
                                        ]
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                text: "页面状态：" + doctorWindow.recordStatus
                                color: theme.blueDeep
                                font.pixelSize: 20
                                font.bold: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    ]
                }
            }
        }

        DoctorSettingsDialog {
            id: settingsDialog
            anchors.fill: parent
            themeRef: theme
            onReconnectRequested: {
                if (!signController.running)
                    signController.startSystem()
            }
            onResetScreensRequested: doctorWindow.handlePlaceholderAction("reset_screens")
            onPatientFullscreenRequested: doctorWindow.handlePlaceholderAction("patient_full")
            onDoctorFullscreenRequested: doctorWindow.handlePlaceholderAction("doctor_full")
            onToggleWorkerRequested: doctorWindow.handlePlaceholderAction("restart_worker")
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - doctorWindow.navWidth - 96, 520)
            height: 70
            radius: 12
            color: "#dd17324d"
            border.width: 1
            border.color: "#8fc7e0"
            visible: doctorWindow.doctorToastMessage.length > 0
            opacity: doctorWindow.doctorToastVisible ? 1.0 : 0.0
            scale: doctorWindow.doctorToastVisible ? 1.0 : 0.97
            z: 130

            Behavior on opacity {
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }

            Behavior on scale {
                NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
            }

            Text {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                text: doctorWindow.doctorToastMessage
                color: "white"
                font.pixelSize: 18
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: doctorWindow.confirmDialogVisible
            color: "#66000000"
            z: 125

            Rectangle {
                width: 460
                height: 230
                anchors.centerIn: parent
                radius: 14
                color: "#ffffff"
                border.width: 1
                border.color: "#cfdbe5"

                Column {
                    anchors.fill: parent
                    anchors.margins: 26
                    spacing: 18

                    Text {
                        width: parent.width
                        text: doctorWindow.confirmDialogTitle
                        color: "#17324d"
                        font.pixelSize: 24
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        width: parent.width
                        height: 70
                        text: doctorWindow.confirmDialogMessage
                        color: "#4b5b68"
                        font.pixelSize: 17
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    Row {
                        width: parent.width
                        height: 48
                        spacing: 14

                        MedicalButton {
                            width: (parent.width - parent.spacing) / 2
                            height: parent.height
                            label: "取消"
                            fontSize: 18
                            activeColor: theme.secondaryButton
                            onClicked: doctorWindow.cancelConfirm()
                        }

                        MedicalButton {
                            width: (parent.width - parent.spacing) / 2
                            height: parent.height
                            label: "确认"
                            fontSize: 18
                            activeColor: "#a87878"
                            onClicked: doctorWindow.acceptConfirm()
                        }
                    }
                }
            }
        }

        Rectangle {
            id: disconnectDialog
            anchors.fill: parent
            visible: doctorWindow.disconnectedWarning
            color: "#99000000"
            z: 120

            Rectangle {
                width: 460
                height: 220
                radius: 14
                anchors.centerIn: parent
                color: "#eef4f7"
                border.color: "#b56b65"

                Column {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18

                    Text {
                        width: parent.width
                        text: "系统连接异常"
                        color: "#8d3f3a"
                        font.pixelSize: 28
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: "检测到识别服务未运行，请重新连接设备后继续。"
                        color: "#374151"
                        font.pixelSize: 18
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        width: parent.width
                        height: 54
                        spacing: 12

                        MedicalButton {
                            width: 170
                            height: parent.height
                            label: "一键重连"
                            fontSize: 18
                            activeColor: theme.blueDeep
                            onClicked: {
                                doctorWindow.disconnectedWarning = false
                                signController.startSystem()
                            }
                        }

                        MedicalButton {
                            width: 140
                            height: parent.height
                            label: "关闭"
                            fontSize: 18
                            activeColor: theme.neutralButton
                            onClicked: doctorWindow.disconnectedWarning = false
                        }
                    }
                }
            }
        }

        function selectDoctorPage(index) {
            currentPageIndex = index
            if (index === 0) {
                doctorStack.replace(doctorHomeComponent)
                return
            }
            if (index === 1) {
                doctorStack.replace(doctorRecordsWorkbookComponent)
                return
            }
            if (index === 2) {
                doctorStack.replace(doctorSignLiveComponent)
                return
            }

            placeholderPageTitle = navItems[index].title
            placeholderPageHint = doctorWindow.placeholderHint(index)
            doctorStack.replace(doctorPlaceholderComponent)
        }

        function departmentTitle(department) {
            if (department === "digestive")
                return "消化科"
            if (department === "ent")
                return "耳鼻喉科"
            return "全科"
        }

        function selectDepartment(department) {
            currentDepartment = department
            signController.setDepartment(department)
            doctorToast = "已切换至" + departmentTitle(department)
        }

        function departmentTemplates() {
            if (currentDepartment === "digestive") {
                return ["哪里不舒服？", "拉了几次？", "持续多久了？", "有没有发烧？", "需要忌口多久？", "什么时候复查？"]
            }
            if (currentDepartment === "ent") {
                return ["哪里不舒服？", "有没有鼻出血？", "有没有流鼻涕？", "有没有咳嗽？", "头疼多久了？", "有没有发烧？"]
            }
            return ["哪里不舒服？", "什么时候开始的？", "持续多长时间？", "还有其他症状吗？", "需要帮助吗？"]
        }

        function useDoctorReplyTemplate(text) {
            doctorReplyDraft = text
        }

        function placeholderHint(index) {
            if (index === 1)
                return "管理当前病历草稿与导出记录。"
            return "请选择需要的操作。"
        }

        function placeholderActions(index) {
            if (index === 1) {
                return [
                    { "label": "新建病历", "action": "new_record", "color": theme.secondaryButton, "enabled": true },
                    { "label": "保存病历", "action": "save_record", "color": theme.blueDeep, "enabled": true },
                    { "label": "导出病历", "action": "export_record", "color": theme.blueDeep, "enabled": true }
                ]
            }
            return [
                { "label": "返回首页", "action": "home", "color": theme.blueDeep, "enabled": true },
                { "label": "打开设置", "action": "settings", "color": theme.secondaryButton, "enabled": true },
                { "label": "刷新状态", "action": "sync_status", "color": theme.neutralButton, "enabled": true }
            ]
        }

        function placeholderMetrics(index) {
            if (index === 1) {
                return [
                    { "title": "当前草稿", "value": recordDraft.length > 0 ? "已生成" : "未生成" },
                    { "title": "最近保存", "value": lastSavedAt },
                    { "title": "最近导出", "value": exportedAt }
                ]
            }
            return [
                { "title": "系统", "value": signController.running ? "在线" : "离线" },
                { "title": "词流", "value": patientWindow.cleanTokens() },
                { "title": "主诉", "value": patientWindow.cleanSentence() }
            ]
        }

        function handlePlaceholderAction(action) {
            if (action === "new_record") {
                requestNewRecord()
            } else if (action === "save_record") {
                saveRecord()
            } else if (action === "export_record") {
                exportRecord()
            } else if (action === "restart_worker") {
                if (signController.running) {
                    signController.stopSystem()
                    recordStatus = "已发送识别服务停止指令，再次点击可启动"
                    doctorToast = "识别服务停止中"
                } else {
                    signController.startSystem()
                    recordStatus = "已发送识别服务启动指令"
                    doctorToast = "识别服务启动中"
                }
            } else if (action === "clear_notice") {
                recordStatus = "运行提示已清空"
                doctorToast = "提示已清空"
            } else if (action === "reset_screens") {
                patientWindow.placeWindowOnScreen(patientWindow, "DSI", 1, true)
                patientWindow.placeWindowOnScreen(doctorWindow, "HDMI", 0, true)
                doctorExpanded = true
                recordStatus = "双屏窗口已重置"
                doctorToast = "双屏已重置"
            } else if (action === "patient_full") {
                patientWindow.patientExpanded = true
                patientWindow.placeWindowOnScreen(patientWindow, "DSI", 1, true)
                doctorToast = "患者端已全屏"
            } else if (action === "doctor_full") {
                doctorExpanded = true
                patientWindow.placeWindowOnScreen(doctorWindow, "HDMI", 0, true)
                doctorToast = "医生端已全屏"
            } else if (action === "settings") {
                settingsDialog.opened = true
            } else {
                selectDoctorPage(0)
            }
        }

        function doctorStateText() {
            return theme.stateTitle(patientWindow.businessState())
        }

        function doctorStateColor() {
            return theme.stateColor(patientWindow.businessState())
        }

        function hasChiefComplaint() {
            var text = patientWindow.cleanSentence()
            return text !== "待整理" && text !== "表达中..." && text !== "正在整理..." &&
                   text.indexOf("请先") !== 0 && text.indexOf("暂无") !== 0
        }

        function chiefComplaintText() {
            return hasChiefComplaint() ? patientWindow.cleanSentence() : "暂无主诉内容\n等待患者手语输入"
        }

	        function paperChiefComplaintText() {
	            return hasRecordComplaint() ? recordComplaintText() : "待患者表达后自动生成。"
	        }

	        function hasRecordComplaint() {
	            return complaintList.length > 0
	        }

        function recordComplaintText() {
            return complaintList.join("\n")
        }

        function normalizeDoctorQuestion(text) {
            var question = text.trim()
            while (question.length > 0) {
                var last = question.charAt(question.length - 1)
                if (last === "?" || last === "？" || last === "。" || last === "：" || last === ":" ||
                    last === "，" || last === "," || last === "！" || last === "!")
                    question = question.substring(0, question.length - 1)
                else
                    break
            }
            return question
        }

        function pendingDoctorQuestion() {
            if (nextComplaintReplyIndex < 0)
                nextComplaintReplyIndex = 0
            if (patientWindow.doctorReplyList.length === 0)
                return ""
            var latestIndex = patientWindow.doctorReplyList.length - 1
            if (latestIndex < nextComplaintReplyIndex)
                return ""
            return normalizeDoctorQuestion(patientWindow.doctorReplyList[latestIndex].text)
        }

        function tokenSummaryText() {
            var text = patientWindow.cleanTokens()
            return text === "待识别" ? "等待患者手语输入" : text
        }

        function homeStatusLine() {
            var state = !signController.running ? "待启动" : (hasChiefComplaint() ? "已生成主诉" : "识别中")
            var device = signController.statusText === "错误" ? "设备异常" : "设备正常"
            var record = recordDirty ? "病历待保存" : (exportedAt !== "尚未导出" ? "已导出" : (lastSavedAt === "尚未保存" ? "病历待保存" : "已保存"))
            return "● " + state + "｜" + device + "｜" + record
        }

        function markRecordDirty() {
            if (suppressRecordDirty)
                return
            recordDirty = true
            exportedAt = "尚未导出"
            recordStatus = "病历待保存"
        }

        function composerCleanText() {
            var text = signController.composerText
            text = text.replace("整理来源：", "")
            text = text.replace("整理方式：", "")
            return text.length === 0 || text === "-" ? "待整理" : text
        }

        function timeText() {
            var date = new Date()
            function pad(value) {
                return value < 10 ? "0" + value : "" + value
            }
            return pad(date.getHours()) + ":" + pad(date.getMinutes()) + ":" + pad(date.getSeconds())
        }

        function currentDateText() {
            var date = new Date()
            function pad(value) {
                return value < 10 ? "0" + value : "" + value
            }
            return date.getFullYear() + "-" + pad(date.getMonth() + 1) + "-" + pad(date.getDate())
        }

        function refreshArchiveRecords() {
            savedArchiveRecords = signController.loadMedicalRecords()
        }

        function recordJsonText(status) {
            var complaint = hasRecordComplaint() ? recordComplaintText() : patientWindow.cleanSentence()
            var data = {
                "created_at": currentDateText() + " " + timeText().substring(0, 5),
                "status": status,
                "name": patientName,
                "gender": patientGender,
                "age": patientAge,
                "record_no": recordNo,
                "complaint": complaint,
                "current_history": currentHistory,
                "doctor_note": doctorNote,
                "treatment_plan": treatmentPlan,
                "doctor_signature": doctorSignature,
                "tokens": patientWindow.cleanTokens(),
                "symptom": symptomText(),
                "onset": onsetText(),
                "composer": composerCleanText()
            }
            return JSON.stringify(data)
        }

        function archiveRecordJsonText(record, status) {
            var data = {
                "created_at": record.created_at && record.created_at.length > 0 ? record.created_at : record.time,
                "status": status,
                "name": record.name ? record.name : "",
                "gender": record.gender ? record.gender : "",
                "age": record.age ? record.age : "",
                "record_no": record.record_no ? record.record_no : "",
                "complaint": record.complaint ? record.complaint : "",
                "current_history": record.current_history ? record.current_history : "",
                "doctor_note": record.doctor_note ? record.doctor_note : "",
                "treatment_plan": record.treatment_plan ? record.treatment_plan : "",
                "doctor_signature": record.doctor_signature ? record.doctor_signature : ""
            }
            return JSON.stringify(data)
        }

        function archiveRecordPlainText(record) {
            var lines = []
            lines.push("姓名：" + (record.name ? record.name : ""))
            lines.push("性别：" + (record.gender ? record.gender : ""))
            lines.push("年龄：" + (record.age ? record.age : ""))
            lines.push("病历号：" + (record.record_no ? record.record_no : ""))
            lines.push("主诉：" + (record.complaint ? record.complaint : ""))
            lines.push("现病史：" + (record.current_history ? record.current_history : "待医生补充"))
            lines.push("医生备注：" + (record.doctor_note ? record.doctor_note : "待医生补充"))
            lines.push("处理意见：" + (record.treatment_plan ? record.treatment_plan : "待医生补充"))
            lines.push("医生签名：" + (record.doctor_signature ? record.doctor_signature : ""))
            return lines.join("；")
        }

		        function buildRecordText() {
		            var sentence = hasRecordComplaint() ? recordComplaintText() : patientWindow.cleanSentence()
		            var tokens = patientWindow.cleanTokens()
		            var symptom = doctorWindow.symptomText()
		            var onset = doctorWindow.onsetText()
	            var lines = []
	            lines.push("姓名：" + (patientName.length ? patientName : ""))
	            lines.push("性别：" + (patientGender.length ? patientGender : ""))
	            lines.push("年龄：" + (patientAge.length ? patientAge : ""))
	            lines.push("病历号：" + (recordNo.length ? recordNo : ""))
	            lines.push("主诉：" + sentence)
	            lines.push("现病史：" + (currentHistory.length ? currentHistory : "待医生补充"))
	            lines.push("医生备注：" + (doctorNote.length ? doctorNote : "待医生补充"))
	            lines.push("处理意见：" + (treatmentPlan.length ? treatmentPlan : "待医生补充"))
	            lines.push("医生签名：" + (doctorSignature.length ? doctorSignature : ""))
	            lines.push("识别词：" + tokens)
	            lines.push("主要症状：" + symptom)
	            lines.push("起病时间：" + onset)
            lines.push("整理来源：" + doctorWindow.composerCleanText())
            return lines.join("；")
        }

        function archiveHistoryRecords() {
            var query = archiveSearchText.trim()
            if (query.length === 0)
                return savedArchiveRecords

            var filtered = []
            for (var i = 0; i < savedArchiveRecords.length; ++i) {
                var record = savedArchiveRecords[i]
                var haystack = record.time + " " +
                               record.status + " " +
                               record.complaint + " " +
                               record.name + " " +
                               record.record_no + " " +
                               record.gender + " " +
                               record.age
                if (haystack.indexOf(query) >= 0)
                    filtered.push(record)
            }
            return filtered
        }

	        function hasArchiveDraft() {
	            return recordDraft.length > 0 || hasRecordComplaint()
        }

        function archiveEffectiveIndex() {
            if (archiveSelectedIndex === 0 && !hasArchiveDraft())
                return archiveHistoryRecords().length > 0 ? 1 : -1
            return archiveSelectedIndex
        }

        function selectArchiveRecord(index) {
            if (index === 0 && !hasArchiveDraft()) {
                doctorToast = "暂无未保存病历"
                archiveSelectedIndex = archiveHistoryRecords().length > 0 ? 1 : -1
                return
            }
            archiveSelectedIndex = index
        }

        function selectArchivePath(path) {
            var records = archiveHistoryRecords()
            for (var i = 0; i < records.length; ++i) {
                if (records[i].path === path) {
                    archiveSelectedIndex = i + 1
                    return
                }
            }
            archiveSelectedIndex = hasArchiveDraft() ? 0 : (records.length > 0 ? 1 : -1)
        }

        function archiveDraftStatus() {
            if (recordDirty)
                return "待保存"
            if (exportedAt !== "尚未导出")
                return "已导出"
            return lastSavedAt === "尚未保存" ? "待保存" : "已保存"
        }

        function archiveDraftTime() {
            return currentDateText() + " " + (lastSavedAt === "尚未保存" ? timeText() : lastSavedAt)
        }

	        function archiveDraftSummary() {
	            return paperChiefComplaintText()
	        }

        function archiveHasSelection() {
            return archiveEffectiveIndex() >= 0
        }

        function archiveSelectedRecord() {
            var index = archiveEffectiveIndex()
            if (index === 0) {
                return {
                    "time": archiveDraftTime(),
                    "status": archiveDraftStatus(),
                    "complaint": archiveDraftSummary(),
                    "saved_at": lastSavedAt === "尚未保存" ? "" : lastSavedAt,
                    "exported_at": exportedAt === "尚未导出" ? "" : exportedAt,
                    "updated_at": archiveDraftTime(),
                    "plain_text": recordDraft,
                    "path": recordFilePath,
                    "name": patientName,
                    "gender": patientGender,
                    "age": patientAge,
                    "record_no": recordNo,
                    "current_history": currentHistory,
                    "doctor_note": doctorNote,
                    "treatment_plan": treatmentPlan,
                    "doctor_signature": doctorSignature
                }
            }
            var records = archiveHistoryRecords()
            if (index > 0 && index - 1 < records.length)
                return records[index - 1]
            return {
                "time": "",
                "status": "",
                "complaint": "请选择左侧病历记录。",
                "saved_at": "",
                "exported_at": "",
                "updated_at": "",
                "plain_text": "",
                "path": "",
                "name": "",
                "gender": "",
                "age": "",
                "record_no": "",
                "current_history": "",
                "doctor_note": "",
                "treatment_plan": "",
                "doctor_signature": ""
            }
        }

        function loadArchiveRecordForEditing() {
            if (!archiveHasSelection()) {
                doctorToast = "请先选择病历记录"
                return
            }

            var record = archiveSelectedRecord()
            suppressRecordDirty = true
            patientName = doctorWindow.archiveField("name", "")
            patientGender = doctorWindow.archiveField("gender", "")
            patientAge = doctorWindow.archiveField("age", "")
            recordNo = doctorWindow.archiveField("record_no", "")
            currentHistory = doctorWindow.archiveField("current_history", "")
            doctorNote = doctorWindow.archiveField("doctor_note", "")
            treatmentPlan = doctorWindow.archiveField("treatment_plan", "")
            doctorSignature = doctorWindow.archiveField("doctor_signature", "")

            var complaint = doctorWindow.archiveSelectedComplaint()
            if (complaint === "请选择左侧病历记录。" || complaint === "待患者表达后自动生成。") {
                complaintList = []
                lastWrittenComplaint = ""
            } else {
                complaintList = complaint.split("\n")
                lastWrittenComplaint = complaintList.length > 0 ? complaintList[complaintList.length - 1] : ""
            }

            recordFilePath = record.path ? record.path : ""
            recordDraft = record.plain_text && record.plain_text.length > 0 ? record.plain_text : buildRecordText()
            lastSavedAt = record.saved_at && record.saved_at.length > 0 ? record.saved_at : record.time
            exportedAt = record.exported_at && record.exported_at.length > 0 ? record.exported_at : "尚未导出"
            nextComplaintReplyIndex = patientWindow.doctorReplyList.length
            recordDirty = false
            suppressRecordDirty = false
            recordStatus = record.status && record.status.length > 0 ? "已加载历史病历：" + record.status : "已加载历史病历"
            doctorToast = "已加载病历，可继续编辑"
            selectDoctorPage(0)
        }

        function archiveSelectedComplaint() {
            if (!archiveHasSelection())
                return "请选择左侧病历记录。"
            return archiveSelectedRecord().complaint
        }

        function archiveField(name, fallback) {
            var record = archiveSelectedRecord()
            var value = record[name]
            if (value === undefined || value === null || String(value).length === 0)
                return fallback
            return value
        }

        function archiveSelectedDate() {
            var record = archiveSelectedRecord()
            return record.time.length >= 10 ? record.time.substring(0, 10) : currentDateText()
        }

        function deleteArchiveRecord() {
            if (archiveEffectiveIndex() === 0) {
                clearRecord()
                archiveSelectedIndex = archiveHistoryRecords().length > 0 ? 1 : -1
                return
            }
            var record = archiveSelectedRecord()
            if (record.path && signController.deleteMedicalRecord(record.path)) {
                refreshArchiveRecords()
                archiveSelectedIndex = archiveHistoryRecords().length > 0 ? 1 : -1
                doctorToast = "历史病历已删除"
            } else {
                doctorToast = "删除失败，请检查文件权限"
            }
        }

        function requestConfirm(action, title, message) {
            confirmDialogAction = action
            confirmDialogTitle = title
            confirmDialogMessage = message
            confirmDialogVisible = true
        }

        function cancelConfirm() {
            confirmDialogVisible = false
            confirmDialogAction = ""
        }

        function acceptConfirm() {
            var action = confirmDialogAction
            confirmDialogVisible = false
            confirmDialogAction = ""
            if (action === "new_record") {
                newRecord()
            } else if (action === "clear_all") {
                clearAllContent()
            } else if (action === "delete_archive") {
                deleteArchiveRecord()
            }
        }

        function requestNewRecord() {
            if (recordDirty || hasRecordComplaint() || patientName.length > 0 || recordFilePath.length > 0) {
                requestConfirm("new_record", "确认新建病历？", "当前病历内容将从首页清空。未保存的修改不会保留。")
                return
            }
            newRecord()
        }

        function requestClearAllContent() {
            requestConfirm("clear_all", "确认清空内容？", "当前识别内容和病历草稿都会被清空。")
        }

        function requestDeleteArchiveRecord() {
            if (!archiveHasSelection()) {
                doctorToast = "请先选择病历记录"
                return
            }
            requestConfirm("delete_archive", "确认删除病历？", "删除后会移除该病历记录及对应导出文件。")
        }

        function saveArchiveSelection() {
            if (archiveEffectiveIndex() === 0) {
                saveRecord()
                return
            }

            if (!archiveHasSelection()) {
                doctorToast = "请先选择病历记录"
                return
            }

            var record = archiveSelectedRecord()
            if (!record.path || record.path.length === 0) {
                doctorToast = "当前记录无法保存"
                return
            }

            var path = signController.saveMedicalRecord(
                archiveRecordJsonText(record, "已保存"),
                archiveRecordPlainText(record),
                record.path)
            if (path.length === 0) {
                doctorToast = "保存失败，请检查目录权限"
                return
            }
            refreshArchiveRecords()
            selectArchivePath(path)
            doctorToast = "病历已保存"
        }

        function exportArchiveSelection() {
            if (archiveEffectiveIndex() === 0) {
                exportRecord()
                return
            }

            if (!archiveHasSelection()) {
                doctorToast = "请先选择病历记录"
                return
            }

            var record = archiveSelectedRecord()
            if (!record.path || record.path.length === 0) {
                doctorToast = "请先保存病历再导出"
                return
            }

            var path = signController.exportMedicalRecord(
                archiveRecordJsonText(record, "已导出"),
                archiveRecordPlainText(record),
                record.path)
            if (path.length === 0) {
                doctorToast = "导出失败，请检查目录权限"
                return
            }
            refreshArchiveRecords()
            selectArchivePath(record.path)
            doctorToast = "病历已导出"
        }

	        function newRecord() {
	            suppressRecordDirty = true
	            signController.clearExpression()
	            manualRecord = ""
	            recordDraft = ""
	            recordFilePath = ""
	            complaintList = []
	            lastWrittenComplaint = ""
	            patientName = ""
	            patientGender = ""
	            patientAge = ""
	            recordNo = ""
	            currentHistory = ""
	            doctorNote = ""
	            treatmentPlan = ""
	            doctorSignature = ""
	            patientWindow.doctorReplyList = []
	            patientWindow.doctorReplyPopupVisible = false
	            patientWindow.doctorReplyPopupText = ""
	            patientWindow.doctorReplyPopupTime = ""
	            nextComplaintReplyIndex = 0
	            recordStatus = "已新建空白病历，可继续采集手语主诉"
            lastSavedAt = "尚未保存"
            exportedAt = "尚未导出"
            recordDirty = false
            suppressRecordDirty = false
            archiveSelectedIndex = 0
            doctorToast = "已新建病历"
        }

        function saveRecord() {
            recordDraft = buildRecordText()
            var path = signController.saveMedicalRecord(recordJsonText("已保存"), recordDraft, recordFilePath)
            if (path.length === 0) {
                doctorToast = "保存失败，请检查目录权限"
                return
            }
            recordFilePath = path
            lastSavedAt = timeText()
            recordStatus = "病历已保存"
            recordDirty = false
            refreshArchiveRecords()
            doctorToast = "病历已保存"
        }

        function exportRecord() {
            if (recordFilePath.length === 0) {
                doctorToast = "请先保存病历再导出"
                recordStatus = "病历尚未保存，无法导出"
                return
            }
            recordDraft = buildRecordText()
            var path = signController.exportMedicalRecord(recordJsonText("已导出"), recordDraft, recordFilePath)
            if (path.length === 0) {
                doctorToast = "导出失败，请检查目录权限"
                return
            }
            exportedAt = timeText()
            recordStatus = "病历已导出"
            recordDirty = false
            refreshArchiveRecords()
            doctorToast = "病历已导出"
        }

	        function clearRecord() {
	            suppressRecordDirty = true
	            manualRecord = ""
	            recordDraft = ""
	            recordFilePath = ""
	            complaintList = []
	            lastWrittenComplaint = ""
	            patientName = ""
	            patientGender = ""
	            patientAge = ""
	            recordNo = ""
	            currentHistory = ""
	            doctorNote = ""
	            treatmentPlan = ""
	            doctorSignature = ""
	            nextComplaintReplyIndex = patientWindow.doctorReplyList.length
	            recordStatus = "病历草稿已清空"
            exportedAt = "尚未导出"
            recordDirty = false
            suppressRecordDirty = false
            doctorToast = "已清空病历草稿"
        }

	        function fillFromComplaint() {
	            var currentComplaint = hasChiefComplaint() ? patientWindow.cleanSentence().trim() : ""
	            if (currentComplaint.length === 0 || currentComplaint === "待整理") {
	                recordStatus = "暂无主诉内容，等待患者手语输入"
	                doctorToast = "暂无可写入内容"
	                return
	            }
	            var question = pendingDoctorQuestion()
	            var recordLine = question.length > 0 ? question + "：" + currentComplaint : currentComplaint
		            if (recordLine === lastWrittenComplaint || complaintList.indexOf(recordLine) >= 0) {
		                doctorToast = "当前主诉已写入"
		                return
		            }
	            complaintList = complaintList.concat([recordLine])
	            lastWrittenComplaint = recordLine
	            if (question.length > 0)
	                nextComplaintReplyIndex = patientWindow.doctorReplyList.length
	            recordDraft = buildRecordText()
	            lastSavedAt = "尚未保存"
	            recordDirty = true
	            recordStatus = "主诉已追加写入病历，病历待保存"
	            doctorToast = "已追加写入病历"
	        }

        function fillFromTokens() {
            manualRecord = patientWindow.cleanTokens()
            recordDraft = buildRecordText()
            recordStatus = "识别词流已填入病历草稿"
            doctorToast = "已填入识别词流"
        }

        function recomposeComplaint() {
            if (!signController.running) {
                doctorToast = "请先启动识别服务"
                return
            }
            if (hasChiefComplaint()) {
                recordStatus = "已保留当前主诉，可让患者重新表达后再次整理"
                doctorToast = "请患者重新表达后点击整理"
            } else {
                doctorToast = "等待患者手语输入"
            }
        }

	        function clearAllContent() {
	            signController.clearExpression()
	            clearRecord()
	            recordStatus = "内容已清空，等待患者手语输入"
	            doctorToast = "内容已清空"
	        }

	        function sendDoctorReply() {
	            var text = doctorReplyDraft.trim()
	            if (text.length === 0) {
	                doctorToast = "请输入回复内容"
	                return
	            }
	            var reply = {
	                "time": timeText().substring(0, 5),
	                "text": text
	            }
	            patientWindow.doctorReplyList = patientWindow.doctorReplyList.concat([reply])
	            patientWindow.showDoctorReplyPopup(reply)
	            doctorReplyDraft = ""
	            doctorToast = "已发送给患者"
	        }

	        function latestDoctorReplyText() {
	            if (patientWindow.doctorReplyList.length === 0)
	                return "最近发送：-"
	            var item = patientWindow.doctorReplyList[patientWindow.doctorReplyList.length - 1]
	            return "已发送：" + item.text
	        }

        function latencyColor() {
            var match = signController.latencyText.match(/帧龄 ([0-9.]+) ms/)
            if (match && Number(match[1]) > 180)
                return theme.latencyWarn
            if (signController.latencyText.indexOf("-") >= 0)
                return theme.doctorMuted
            return theme.blue
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
