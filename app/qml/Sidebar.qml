import QtQuick
import QtQuick.Layouts
import Harmonica

/// 左侧导航条。三个入口写死在这里 —— 页面本身在轮③④⑤ 逐个落地。
ColumnLayout {
    id: root

    property var model: []
    property int current: 0
    signal activated(int index)

    spacing: 2
    Layout.preferredWidth: 168
    Layout.fillHeight: true

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 8
    }

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
}
