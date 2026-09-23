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

    // 进度不靠事件推 —— 演奏线程里那颗原子数是权威，界面按时轮询；
    // 这里轮询还没接上（轮③），先把状态显示成空闲。
    property string title_: ""
    property int remainingSeconds: 0

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
                    text: root.title_.length > 0 ? root.title_ : "空闲"
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
                text: root.remainingSeconds > 0 ? "-" + root.remainingSeconds + "s" : ""
                color: Backend.accent
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
        }
    }
}
