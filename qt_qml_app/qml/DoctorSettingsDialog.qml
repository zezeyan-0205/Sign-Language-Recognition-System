import QtQuick 2.15

Rectangle {
    id: root
    property bool opened: false
    property var themeRef
    property int currentPage: 0
    property int recognitionLevel: 1
    property int collectPolicy: 1
    property int composerPolicy: 0
    property int templateMode: 0
    property bool autoFillRecord: true
    property int exportFormat: 0
    property int patientDisplayMode: 0
    property int doctorDisplayMode: 0
    property bool lowLightMode: true
    property int cameraChannel: 21
    property int inputMode: 1
    property bool roiVisible: true
    property bool autoReconnect: true
    property bool verboseLog: false
    property bool performanceMode: true
    property string savedMessage: "设置尚未保存"
    property string feedbackMessage: ""

    signal reconnectRequested()
    signal resetScreensRequested()
    signal patientFullscreenRequested()
    signal doctorFullscreenRequested()
    signal toggleWorkerRequested()

    visible: opened
    opacity: opened ? 1.0 : 0.0
    color: "#99000000"
    z: 100

    Behavior on opacity {
        NumberAnimation { duration: 180 }
    }

    onFeedbackMessageChanged: {
        if (feedbackMessage.length > 0)
            feedbackTimer.restart()
    }

    Timer {
        id: feedbackTimer
        interval: 2200
        repeat: false
        onTriggered: root.feedbackMessage = ""
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.opened = false
    }

    Rectangle {
        id: dialog
        width: Math.min(parent.width - 120, 900)
        height: Math.min(parent.height - 100, 650)
        anchors.centerIn: parent
        radius: themeRef ? themeRef.radiusLarge : 14
        color: "#eef5fb"
        border.width: 1
        border.color: "#bad2e5"
        scale: root.opened ? 1.0 : 0.96

        Behavior on scale {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#ffffff" }
                GradientStop { position: 1.0; color: "#e8f2fa" }
            }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 18

            Row {
                width: parent.width
                height: 52

                Column {
                    width: parent.width - 70
                    height: parent.height
                    spacing: 4

                    Text {
                        width: parent.width
                        text: "全局设置"
                        color: "#17324d"
                        font.pixelSize: 28
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: "识别、病历、显示、摄像头与系统参数"
                        color: "#607285"
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    width: 48
                    height: 42
                    radius: 8
                    color: closeArea.containsMouse ? "#b56b65" : "#6f8da5"

                    Text {
                        anchors.centerIn: parent
                        text: "X"
                        color: "white"
                        font.pixelSize: 18
                        font.bold: true
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.opened = false
                    }
                }
            }

            Row {
                width: parent.width
                height: parent.height - 70
                spacing: 18

                Column {
                    width: 230
                    height: parent.height
                    spacing: 10

                    Repeater {
                        model: [
                            "手语识别参数",
                            "病历模板配置",
                            "界面显示设置",
                            "硬件摄像头配置",
                            "系统通用设置"
                        ]

                        Rectangle {
                            width: parent.width
                            height: 62
                            radius: 8
                            color: root.currentPage === index ? "#2f8fb6" : "#e8f3fb"
                            border.width: 1
                            border.color: root.currentPage === index ? "#78d2ee" : "#bad2e5"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 12
                                text: modelData
                                color: root.currentPage === index ? "white" : "#17324d"
                                font.pixelSize: 18
                                font.bold: root.currentPage === index
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.currentPage = index
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width - 248
                    height: parent.height
                    radius: 14
                    color: "#eef4f7"
                    border.width: 1
                    border.color: "#8fa6b5"

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#f4f8fc" }
                            GradientStop { position: 1.0; color: "#e8eff8" }
                        }
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 18

                        Text {
                            width: parent.width
                            text: pageTitle()
                            color: "#17324d"
                            font.pixelSize: 26
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: pageSubtitle() + "    " + savedMessage
                            color: "#667085"
                            font.pixelSize: 17
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: "#cad6df"
                        }

                        Flickable {
                            id: settingsList
                            width: parent.width
                            height: 56 * 4 + 10 * 3
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: width
                            contentHeight: settingsColumn.height

                            Column {
                                id: settingsColumn
                                width: settingsList.width
                                spacing: 10

                                Repeater {
                                    model: pageItems()

                                    Rectangle {
                                        id: settingRow
                                        property bool hovered: false
                                        width: parent.width
                                        height: 56
                                        radius: 10
                                        color: "#f8fbfd"
                                        border.width: 1
                                        border.color: "#d7e1e8"

                                        Row {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 12

                                            Rectangle {
                                                width: 12
                                                height: 12
                                                radius: 6
                                                color: "#63c9e8"
                                                anchors.verticalCenter: parent.verticalCenter
                                            }

                                            Column {
                                                width: parent.width - 146
                                                anchors.verticalCenter: parent.verticalCenter
                                                spacing: 4

                                                Text {
                                                    width: parent.width
                                                    text: modelData.title
                                                    color: "#20303f"
                                                    font.pixelSize: 17
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    width: parent.width
                                                    text: modelData.value
                                                    color: "#667085"
                                                    font.pixelSize: 14
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Rectangle {
                                                width: 118
                                                height: 38
                                                radius: 8
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: settingRow.hovered ? "#2f8fb6" : "#dbeaf4"
                                                border.width: 1
                                                border.color: settingRow.hovered ? "#78d2ee" : "#bad2e5"

                                                Text {
                                                    anchors.centerIn: parent
                                                    width: parent.width - 12
                                                    text: itemActionLabel(index)
                                                    color: settingRow.hovered ? "white" : "#17324d"
                                                    font.pixelSize: 15
                                                    font.bold: true
                                                    horizontalAlignment: Text.AlignHCenter
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onContainsMouseChanged: settingRow.hovered = containsMouse
                                            onClicked: handleItemClick(index)
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                width: 4
                                height: parent.height
                                radius: 2
                                color: "#d7e1e8"
                                visible: settingsList.contentHeight > settingsList.height

                                Rectangle {
                                    width: parent.width
                                    radius: 2
                                    color: "#7faecb"
                                    y: settingsList.visibleArea.yPosition * parent.height
                                    height: Math.max(28, settingsList.visibleArea.heightRatio * parent.height)
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 36
                            radius: 8
                            color: root.feedbackMessage.length > 0 ? "#dbeef8" : "transparent"
                            border.width: root.feedbackMessage.length > 0 ? 1 : 0
                            border.color: "#9cc6dd"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                text: root.feedbackMessage.length > 0 ? root.feedbackMessage : "提示：设置保存在当前会话；识别输入、摄像头等参数需要重启识别服务后生效，不需要重启整机。"
                                color: root.feedbackMessage.length > 0 ? "#17324d" : "#667085"
                                font.pixelSize: 14
                                font.bold: root.feedbackMessage.length > 0
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }

                        Row {
                            width: parent.width
                            height: 54
                            spacing: 12

                            Rectangle {
                                width: 160
                                height: parent.height
                                radius: 8
                                color: "#2f8fb6"

                                Text {
                                    anchors.centerIn: parent
                                    text: root.currentPage === 4 ? "一键重连" : "保存设置"
                                    color: "white"
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (root.currentPage === 4)
                                            root.reconnectRequested()
                                        saveSettings()
                                    }
                                }
                            }

                            Rectangle {
                                width: 160
                                height: parent.height
                                radius: 8
                                color: "#6f7c87"

                                Text {
                                    anchors.centerIn: parent
                                    text: "恢复默认"
                                    color: "white"
                                    font.pixelSize: 18
                                    font.bold: true
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: restoreDefaults()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function pageTitle() {
        var titles = ["手语识别参数", "病历模板配置", "界面显示设置", "硬件摄像头配置", "系统通用设置"]
        return titles[currentPage]
    }

    function pageSubtitle() {
        var subtitles = [
            "调整采集阈值、识别灵敏度和整理策略，便于不同患者动作习惯适配。",
            "配置主诉模板、备注字段和导出习惯，减少医生重复录入。",
            "调整双屏显示、字号和低亮度模式，适配诊室长时间使用。",
            "选择摄像头通道、预览分辨率和 ROI 提示框，用于设备调试。",
            "管理模型服务、日志和设备重连，保证演示链路稳定运行。"
        ]
        return subtitles[currentPage]
    }

    function pageItems() {
        var pages = [
            [
                { "title": "识别灵敏度", "value": ["保守模式，减少误识别", "标准模式，兼顾速度与稳定", "灵敏模式，适合动作幅度较小患者"][recognitionLevel] },
                { "title": "采集进度策略", "value": ["按有效帧推进", "识别完成后自动补满至 100%", "按真实时间平滑显示"][collectPolicy] },
                { "title": "句子整理来源", "value": ["优先 RKLLM，异常时回退规则整理", "仅规则整理，演示更稳定", "仅 RKLLM，便于测试模型效果"][composerPolicy] }
            ],
            [
                { "title": "主诉模板", "value": ["主诉 + 识别词流 + 医生备注", "简洁主诉模板", "完整门诊记录模板"][templateMode] },
                { "title": "病历导出", "value": ["TXT 文本", "JSON 结构化", "HIS 对接预留"][exportFormat] },
                { "title": "自动填充", "value": autoFillRecord ? "结束识别后提示填入病历" : "手动点击填入病历" }
            ],
            [
                { "title": "患者端屏幕", "value": ["DSI 触控屏，蓝白灰患者界面", "HDMI 大屏患者界面"][patientDisplayMode] },
                { "title": "医生端屏幕", "value": ["HDMI 电子病历工作台", "DSI 调试预览"][doctorDisplayMode] },
                { "title": "双屏窗口", "value": "重置 HDMI 医生端和 DSI 患者端位置" },
                { "title": "患者端全屏", "value": "将患者端重新铺满 DSI 屏幕" },
                { "title": "医生端全屏", "value": "将医生端重新铺满 HDMI 屏幕" },
                { "title": "低亮度模式", "value": lowLightMode ? "开启，适合诊室长时间观看" : "关闭，提高屏幕亮度" }
            ],
            [
                { "title": "摄像头通道", "value": "/dev/video" + cameraChannel },
                { "title": "识别输入", "value": ["640x360 快速模式", "1280x720 精度模式", "960x540 均衡模式"][inputMode] },
                { "title": "ROI 提示框", "value": roiVisible ? "显示，引导患者把手放入识别区" : "隐藏，画面更简洁" }
            ],
            [
                { "title": "RKLLM 服务", "value": autoReconnect ? "自动重连已开启" : "仅手动重连" },
                { "title": "识别服务", "value": "启动或停止后台 Python 识别进程" },
                { "title": "运行日志", "value": verboseLog ? "详细日志，便于调试" : "标准日志，减少终端噪声" },
                { "title": "性能模式", "value": performanceMode ? "启动脚本锁定高性能频率" : "系统默认频率" }
            ]
        ]
        return pages[currentPage]
    }

    function itemActionLabel(index) {
        if (currentPage === 0 && index < 3)
            return "切换"
        if (currentPage === 1 && index < 2)
            return "切换"
        if (currentPage === 1 && index === 2)
            return autoFillRecord ? "关闭" : "开启"
        if (currentPage === 2 && index < 2)
            return "切换"
        if (currentPage === 2 && index === 2)
            return "重置"
        if (currentPage === 2 && index === 3)
            return "执行"
        if (currentPage === 2 && index === 4)
            return "执行"
        if (currentPage === 2 && index === 5)
            return lowLightMode ? "关闭" : "开启"
        if (currentPage === 3 && index === 0)
            return "下一个"
        if (currentPage === 3 && index === 1)
            return "切换"
        if (currentPage === 3 && index === 2)
            return roiVisible ? "隐藏" : "显示"
        if (currentPage === 4 && index === 0)
            return autoReconnect ? "关闭" : "开启"
        if (currentPage === 4 && index === 1)
            return "切换"
        if (currentPage === 4 && index === 2)
            return verboseLog ? "标准" : "详细"
        if (currentPage === 4 && index === 3)
            return performanceMode ? "关闭" : "开启"
        return "调整"
    }

    function handleItemClick(index) {
        var changedTitle = pageItems()[index].title
        if (currentPage === 0 && index === 0)
            recognitionLevel = (recognitionLevel + 1) % 3
        else if (currentPage === 0 && index === 1)
            collectPolicy = (collectPolicy + 1) % 3
        else if (currentPage === 0 && index === 2)
            composerPolicy = (composerPolicy + 1) % 3
        else if (currentPage === 1 && index === 0)
            templateMode = (templateMode + 1) % 3
        else if (currentPage === 1 && index === 1)
            exportFormat = (exportFormat + 1) % 3
        else if (currentPage === 1 && index === 2)
            autoFillRecord = !autoFillRecord
        else if (currentPage === 2 && index === 0)
            patientDisplayMode = (patientDisplayMode + 1) % 2
        else if (currentPage === 2 && index === 1)
            doctorDisplayMode = (doctorDisplayMode + 1) % 2
        else if (currentPage === 2 && index === 2)
            resetScreensRequested()
        else if (currentPage === 2 && index === 3)
            patientFullscreenRequested()
        else if (currentPage === 2 && index === 4)
            doctorFullscreenRequested()
        else if (currentPage === 2 && index === 5)
            lowLightMode = !lowLightMode
        else if (currentPage === 3 && index === 0)
            cameraChannel = cameraChannel === 21 ? 0 : cameraChannel + 1
        else if (currentPage === 3 && index === 1)
            inputMode = (inputMode + 1) % 3
        else if (currentPage === 3 && index === 2)
            roiVisible = !roiVisible
        else if (currentPage === 4 && index === 0)
            autoReconnect = !autoReconnect
        else if (currentPage === 4 && index === 1)
            toggleWorkerRequested()
        else if (currentPage === 4 && index === 2)
            verboseLog = !verboseLog
        else if (currentPage === 4 && index === 3)
            performanceMode = !performanceMode

        savedMessage = "设置已修改，等待保存"
        if (currentPage === 2 && index >= 2 && index <= 4)
            feedbackMessage = "已执行：" + changedTitle
        else if (currentPage === 4 && index === 1)
            feedbackMessage = "已执行：" + changedTitle
        else
            feedbackMessage = "已修改：" + changedTitle + "。如为摄像头或识别输入参数，请重启识别服务后生效。"
    }

    function saveSettings() {
        savedMessage = "设置已保存到当前会话"
        feedbackMessage = "设置已保存到当前会话。无需重启整机。"
    }

    function restoreDefaults() {
        recognitionLevel = 1
        collectPolicy = 1
        composerPolicy = 0
        templateMode = 0
        exportFormat = 0
        autoFillRecord = true
        patientDisplayMode = 0
        doctorDisplayMode = 0
        lowLightMode = true
        cameraChannel = 21
        inputMode = 1
        roiVisible = true
        autoReconnect = true
        verboseLog = false
        performanceMode = true
        savedMessage = "已恢复默认设置"
        feedbackMessage = "已恢复默认设置。需要时请重新启动识别服务。"
    }
}
