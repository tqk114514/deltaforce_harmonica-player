import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Harmonica

ApplicationWindow {
    id: root

    objectName: "main"
    title: Backend.mainTitle
    visible: true
    width: 1080
    height: 720
    minimumWidth: 880
    minimumHeight: 560
    color: Backend.windowBackground

    property int page: 0

    // 页面顺序要和 Sidebar 的点击一致；todo 文案就是这一轮还没落地的东西
    readonly property var pages: [
        {
            label: "曲库",
            todo: "本地 " + Backend.songsDir + " 与社区清单两档切换、试听与下载、"
                  + "演奏与干跑、悬浮窗实时进度、F9 / F10 热键。轮③ 落地。"
        },
        {
            label: "简谱编辑器",
            todo: "录入音符、时值与八度、连音线、反复、转调、倚音、小节线，"
                  + "保存 .score.json 工程、导出 .dhs。规格在 README「简谱编辑器」一节。轮⑤ 落地。"
        },
        {
            label: "设置与校准",
            todo: "改键位与时序并写回 harmonica.ini（认的键名和文件里完全一样），"
                  + "以及 44 个音位的键位校准 —— 它和演奏走同一条动作表管道。轮④ 落地。"
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                model: root.pages
                current: root.page
                onActivated: (index) => root.page = index
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 20
                spacing: 16

                Text {
                    text: root.pages[root.page].label
                    color: Backend.windowText
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }

                PagePlaceholder {
                    title: "还没接上"
                    body: root.pages[root.page].todo
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }

        // 状态栏：出问题时最先要看的就是这两行 —— 没提权时按键一个都进不了游戏，
        // 目录建不出来时曲库会空，都得在界面上说清楚，而不是让人猜。
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Qt.rgba(1, 1, 1, 0.02)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 18

                Text {
                    text: "程序目录 " + Backend.baseDir
                    color: Backend.mutedText
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Text {
                    visible: Backend.dirError.length > 0
                    text: "曲谱目录：" + Backend.dirError
                    color: Backend.accent
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    visible: Backend.configError.length > 0
                    text: "配置文件：" + Backend.configError
                    color: Backend.accent
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    text: Backend.elevated
                          ? "管理员已提权 · v" + Backend.version
                          : "未提权：按键进不了游戏，请以管理员身份运行"
                    color: Backend.elevated ? Backend.mutedText : Backend.accent
                    font.pixelSize: 12
                }
            }
        }
    }
}
