import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Harmonica

/// 曲库页：本地 `songs/dhs` 与社区清单两档。
///
/// 时长两边都由 core 的同一个解析器算（本地扫盘、社区把谱子读进内存），
/// 界面不另写一套时值算法。下载和 sha256 校验也都在 C++ 那边（`Community`）。
Item {
    id: root

    property var songs: []
    property int tab: 0

    function refresh() {
        root.songs = Backend.listSongs()
    }

    function clock(ms) {
        const secs = Math.max(0, Math.round(ms / 1000))
        return Math.floor(secs / 60) + ":" + ("0" + (secs % 60)).slice(-2)
    }

    function askDownload(title) {
        // 本地已有同名曲目时会先问一句：覆盖是直接盖掉的，没有后悔药
        pending.songTitle = title
        if (Community.hasLocal(title)) {
            pending.open()
        } else {
            Community.download(title)
        }
    }

    Component.onCompleted: {
        refresh()
        Community.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ------------------------------------------------------------ 工具栏
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TabBar {
                id: tabs
                Layout.alignment: Qt.AlignVCenter
                onCurrentIndexChanged: root.tab = currentIndex
                TabButton { text: "本地曲库" }
                TabButton { text: "社区" }
            }

            Button {
                text: Session.playing || Session.countingDown ? "重播" : "演奏"
                visible: root.tab === 0
                enabled: Backend.selectedFile.length > 0 && !Session.playing && !Session.countingDown
                onClicked: Session.start(Backend.selectedFile, false)
            }
            Button {
                text: "干跑"
                visible: root.tab === 0
                enabled: Backend.selectedFile.length > 0 && !Session.playing && !Session.countingDown
                onClicked: Session.start(Backend.selectedFile, true)
            }
            Button {
                text: "停止"
                enabled: Session.playing || Session.countingDown
                onClicked: Session.stop()
            }
            Button {
                text: root.tab === 0 ? "刷新" : "重新拉取"
                onClicked: root.tab === 0 ? root.refresh() : Community.refresh()
            }
            Button { text: "打开曲谱目录"; visible: root.tab === 0; onClicked: Backend.openSongsFolder() }
            Button { text: "显示悬浮窗"; onClicked: Backend.showOverlay() }

            Item { Layout.fillWidth: true }

            Text {
                text: "F9 开始 / 重播 · F10 停止"
                color: Backend.mutedText
                font.pixelSize: 12
            }
        }

        Text {
            visible: root.tab === 0 ? Backend.dirError.length > 0 : Community.status.length > 0
            text: root.tab === 0 ? "曲谱目录不可用：" + Backend.dirError : Community.status
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

        // ------------------------------------------------------------ 列表
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Qt.rgba(1, 1, 1, 0.03)
            border.color: Backend.border

            StackLayout {
                anchors.fill: parent
                anchors.margins: 6
                currentIndex: root.tab

                // ---- 本地
                ListView {
                    id: localList
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
                        width: localList.width
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
                                text: root.clock(modelData.playsMs)
                                color: Backend.windowText
                                font.pixelSize: 13
                            }
                        }
                    }
                }

                // ---- 社区
                ListView {
                    id: netList
                    clip: true
                    model: Community.songs

                    Text {
                        anchors.centerIn: parent
                        visible: Community.songs.length === 0
                        text: Community.busy ? "正在读…" : "社区还没有曲谱，或拉不到清单"
                        color: Backend.mutedText
                        font.pixelSize: 13
                    }

                    delegate: Rectangle {
                        required property var modelData
                        width: netList.width
                        height: 46
                        radius: 6

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    text: modelData.title.length > 0 ? modelData.title : "(没有标题)"
                                    color: modelData.error.length > 0 ? Backend.accent : Backend.windowText
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: modelData.error.length > 0
                                          ? modelData.error
                                          : (modelData.hasDuration ? root.clock(modelData.playsMs) : "时长读取中…")
                                    color: Backend.mutedText
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            ToolButton {
                                text: Preview.playing ? "停下" : "试听"
                                enabled: modelData.error.length === 0
                                onClicked: {
                                    if (Preview.playing) {
                                        Preview.stop()
                                    } else {
                                        Preview.play(Community.textOf(modelData.title))
                                    }
                                }
                            }
                            ToolButton {
                                text: "下载"
                                enabled: modelData.downloadable && !Community.busy
                                onClicked: root.askDownload(modelData.title)
                            }
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

    Dialog {
        id: pending
        property string songTitle: ""

        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "本地已经有这首"
        anchors.centerIn: parent

        Label {
            text: "继续下载会覆盖它：" + pending.songTitle
            color: Backend.windowText
        }

        onAccepted: Community.download(pending.songTitle)
    }
}
