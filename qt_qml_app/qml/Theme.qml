import QtQuick 2.15

QtObject {
    id: theme

    // Shared blue-white-gray medical palette for patient and doctor surfaces.
    readonly property color bgDeep: "#eef5fb"
    readonly property color bgDoctor: "#eef5fb"
    readonly property color bgElevated: "#f7fbff"
    readonly property color bgElevatedTop: "#ffffff"
    readonly property color bgElevatedBottom: "#edf5fb"
    readonly property color card: "#eef4f7"
    readonly property color cardTop: "#f4f8fc"
    readonly property color cardBottom: "#e8eff8"
    readonly property color cardAlt: "#e3ebf0"
    readonly property color cardLine: "#314454"
    readonly property color doctorLine: "#bad2e5"
    readonly property color doctorText: "#17324d"
    readonly property color doctorSubText: "#607285"
    readonly property color doctorMuted: "#8aa0b2"
    readonly property color white: "#f6fbff"
    readonly property color textPrimary: "#f7fbff"
    readonly property color textDark: "#12202b"
    readonly property color textSecondary: "#b9c7d3"
    readonly property color textMuted: "#7f91a0"

    readonly property color blue: "#63c9e8"
    readonly property color blueDeep: "#2386a7"
    readonly property color blueButtonTop: "#46a6ca"
    readonly property color blueButtonBottom: "#2f8fb6"
    readonly property color secondaryButton: "#536575"
    readonly property color neutralButton: "#6f7c87"
    readonly property color wait: "#f2d69b"
    readonly property color success: "#9edfc4"
    readonly property color disabled: "#6f7c87"
    readonly property color warning: "#b56b65"
    readonly property color latencyWarn: "#e6c06f"

    readonly property int radiusLarge: 14
    readonly property int radiusSmall: 8
    readonly property int gapLarge: 14
    readonly property int gapSmall: 8
    readonly property int padLarge: 18
    readonly property int padSmall: 10

    readonly property int titleSize: 30
    readonly property int subtitleSize: 20
    readonly property int bodySize: 17
    readonly property int helperSize: 14

    function stateColor(stateName) {
        if (stateName === "waiting")
            return wait
        if (stateName === "ready")
            return blue
        if (stateName === "collecting")
            return blue
        if (stateName === "complete")
            return success
        if (stateName === "warning")
            return warning
        return cardAlt
    }

    function stateTitle(stateName) {
        if (stateName === "waiting")
            return "等待医生启动"
        if (stateName === "ready")
            return "识别就绪"
        if (stateName === "collecting")
            return "采集中"
        if (stateName === "complete")
            return "识别完成"
        return "设备状态"
    }

    function stateHint(stateName) {
        if (stateName === "waiting")
            return "请等待医生端启动系统"
        if (stateName === "ready")
            return "点击开始后把手放入左侧识别框"
        if (stateName === "collecting")
            return "请保持当前手语动作，等待进度完成"
        if (stateName === "complete")
            return "结果已生成，可继续表达或清空重来"
        return "请关注系统提示"
    }
}
