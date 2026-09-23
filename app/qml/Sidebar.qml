import QtQuick
import QtQuick.Layouts
import Harmonica

/// 左侧导航条。三个入口写死在这里。
///
/// 根是普通 Item 而不是 ColumnLayout：Qt Quick Layouts 里，一个 Layout 直接挂在另一个
/// Layout 下面时，它自己写的 Layout.preferredWidth / Layout.fillHeight 一律不生效，
/// 改成从它的子项继承 fill 标记（实测：子项有 Layout.fillWidth 就把整条侧栏横向撑满，
/// 页面被挤成三分之一宽，而侧栏自己的 fillHeight 反而丢了）。包一层 Item 才按写的来。
Item {
    id: root

    property var model: []
    property int current: 0
    signal activated(int index)

    Layout.preferredWidth: 168
    Layout.fillHeight: true

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        spacing: 2

        Repeater {
            model: root.model

            delegate: Rectangle {
                required property int index
                required property var modelData

                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: 6
                color: index === root.current ? Qt.rgba(1, 1, 1, 0.06) : "transparent"

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.activated(index)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 3
                        Layout.preferredHeight: 16
                        radius: 2
                        color: index === root.current ? Backend.accent : "transparent"
                    }
                    Text {
                        text: modelData.label
                        color: index === root.current ? Backend.windowText : Backend.mutedText
                        font.pixelSize: 14
                        font.weight: index === root.current ? Font.DemiBold : Font.Normal
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // 没有这一项，多出来的高度会被当成条目之间的空隙摊掉，三个入口就会从顶到底散开
        Item { Layout.fillHeight: true }
    }
}
