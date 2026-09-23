import QtQuick
import QtQuick.Layouts
import Harmonica

/// 演奏时浮在游戏上面那条小窗。
///
/// 无边框 + 置顶，并且由 C++ 打上 WS_EX_NOACTIVATE —— 点它不会把焦点从游戏抢走。
/// 这条是刚需：焦点一旦离开游戏，后面的音就全废了。窗口属性必须在它第一次显示之前
/// 打好（见 app/main.cpp），所以这里 `visible: false`，由后端决定什么时候露出来。
Window {
    id: root

    objectName: "overlay"
    title: "演奏中"
    visible: false
    width: 420
    height: 96
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

    // 进度是演奏线程里那颗原子数（权威值），后端每 100ms 读一次推给属性；
    // 这里只做显示层的换算，不算任何时间。
    readonly property bool active: Session.playing || Session.countingDown
    readonly property int remainingSeconds: Math.max(0, Math.round(Session.remainingMs / 1000))

    Rectangle {
        anchors.fill: parent
        anchors.margins: 6
        radius: 10
        color: Qt.rgba(Backend.windowBackground.r, Backend.windowBackground.g,
                       Backend.windowBackground.b, 0.92)
        border.color: Backend.border
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: Session.countingDown
                          ? "准备中，切回游戏…"
                          : (root.active ? Session.title + (Session.dryRun ? "（干跑）" : "") : "空闲")
                    color: Backend.windowText
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: "F10 停止 · F9 开始 / 重播"
                    color: Backend.mutedText
                    font.pixelSize: 11
                }
            }

            Text {
                text: Session.countingDown
                      ? Session.countdownSecs
                      : (root.active ? "-" + root.remainingSeconds + "s" : "")
                color: Backend.accent
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
        }
    }
}
