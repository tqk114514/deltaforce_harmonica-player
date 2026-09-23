import QtQuick
import QtQuick.Layouts
import Harmonica

/// 一页还没落地的占位卡片。轮③④⑤ 会把它们一个个换成真界面。
Rectangle {
    id: root

    property string title: ""
    property string body: ""

    Layout.fillWidth: true
    Layout.fillHeight: true
    radius: 10
    color: Qt.rgba(1, 1, 1, 0.03)
    border.color: Backend.border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 14

        Text {
            text: root.title
            color: Backend.windowText
            font.pixelSize: 22
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        Text {
            text: root.body
            color: Backend.mutedText
            font.pixelSize: 14
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillHeight: true }
        Text {
            text: Backend.mainTitle + " · C++/Qt 重写中"
            color: Backend.mutedText
            font.pixelSize: 12
        }
    }
}
