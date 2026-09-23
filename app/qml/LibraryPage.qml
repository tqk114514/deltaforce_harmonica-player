import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Harmonica

/// 曲库页：扫本地 `songs/dhs`，选一首，演奏或干跑。
///
/// 时长是 core 解析谱面算出来的（`Backend.listSongs()` 里就走那一个解析器），
/// 所以这里显示的时长和真吹出来的一致 —— 界面不许另写一套时值算法。
Item {
    id: root

    property var songs: []
    property string notice: ""

    function refresh() {
        root.songs = Backend.listSongs()
    }

    function remaining(ms) {
        const secs = Math.max(0, Math.round(ms / 1000))
        return Math.floor(secs / 60) + ":" + ("0" + (secs % 60)).slice(-2)
    }

    Component.onCompleted: refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: Session.playing || Session.countingDown ? "重播" : "演奏"
                enabled: Backend.selectedFile.length > 0 && !Session.playing && !Session.countingDown
                onClicked: Session.start(Backend.selectedFile, false)
            }
            Button {
                text: "干跑"
                enabled: Backend.selectedFile.length > 0 && !Session.playing && !Session.countingDown
                onClicked: Session.start(Backend.selectedFile, true)
            }
            Button {
                text: "停止"
                enabled: Session.playing || Session.countingDown
                onClicked: Session.stop()
            }
            Button { text: "刷新"; onClicked: root.refresh() }
            Button { text: "打开曲谱目录"; onClicked: Backend.openSongsFolder() }
            Button {
                text: "显示悬浮窗"
                onClicked: Backend.showOverlay()
            }

            Item { Layout.fillWidth: true }

            Text {
                text: "F9 开始 / 重播 · F10 停止"
                color: Backend.mutedText
                font.pixelSize: 12
            }
        }

        Text {
            visible: Backend.dirError.length > 0
            text: "曲谱目录不可用：" + Backend.dirError
            color: Backend.accent
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }

        Rectangle {
            visible: Session.dropped > 0 && !Session.playing
            Layout.fillWidth: true
            Layout.preferredHeight: statsColumn.height + 16
            radius: 6
            color: Qt.rgba(Backend.accent.r, Backend.accent.g, Backend.accent.b, 0.12)
            border.color: Backend.accent

            ColumnLayout {
                id: statsColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2

                Text {
                    text: "上一场发送 " + Session.sent + " 个事件，其中 " + Session.dropped + " 个被系统丢弃"
                    color: Backend.accent
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
                Text {
                    text: "被丢弃基本只有一个原因：游戏以管理员运行而这个程序没有 —— 把程序以管理员身份再启动一次"
                    color: Backend.mutedText
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Qt.rgba(1, 1, 1, 0.03)
            border.color: Backend.border

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: root.songs

                Text {
                    anchors.centerIn: parent
                    visible: root.songs.length === 0
                    text: "还没有曲谱。把 .dhs 放进 songs\\dhs 再点刷新。"
                    color: Backend.mutedText
                    font.pixelSize: 13
                }

                delegate: Rectangle {
                    required property var modelData
                    required property int index

                    width: list.width
                    height: 46
                    radius: 6
                    color: modelData.file === Backend.selectedFile
                           ? Qt.rgba(1, 1, 1, 0.07) : "transparent"

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Backend.selectedFile = modelData.file
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                text: modelData.error.length > 0
                                      ? modelData.file
                                      : (modelData.title.length > 0 ? modelData.title : modelData.file)
                                color: modelData.error.length > 0 ? Backend.accent : Backend.windowText
                                font.pixelSize: 14
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: modelData.error.length > 0
                                      ? modelData.error
                                      : modelData.file + " · " + modelData.notes + " 个音"
                                color: Backend.mutedText
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Text {
                            visible: modelData.error.length === 0
                            text: "BPM " + Math.round(modelData.bpm)
                            color: Backend.mutedText
                            font.pixelSize: 12
                        }
                        Text {
                            visible: modelData.error.length === 0
                            text: root.remaining(modelData.playsMs)
                            color: Backend.windowText
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: Session.lastError.length > 0
            text: Session.lastError
            color: Backend.accent
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
}
