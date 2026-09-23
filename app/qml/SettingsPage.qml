import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Harmonica

/// 设置与校准。
///
/// 界面上认的键名和配置文件里认的**完全一样**（都走 core 的 `parseKeyName` /
/// `parseButton`），所以手改文件和界面改是同一套规则：界面上填不进去的，
/// 文件里也写不出来。校验和写回都在 `Backend.saveKeyConfig()`，这里只收值。
Item {
    id: root

    property var draft: Backend.keyConfig

    function loadFromDisk() {
        root.draft = Backend.keyConfig
    }

    function collectDegrees() {
        const out = []
        for (let i = 0; i < degreeRepeater.count; i += 1) out.push(degreeRepeater.itemAt(i).text)
        return out
    }

    function save() {
        Backend.saveKeyConfig({
            degrees: root.collectDegrees(),
            highDo: highDoField.text,
            low: lowBox.currentText,
            high: highBox.currentText,
            sharp: sharpBox.currentText,
            mouseLeadMs: leadSpin.value,
            noteGapMs: gapSpin.value,
        })
        root.draft = Backend.keyConfig
    }

    Component.onCompleted: loadFromDisk()
    onVisibleChanged: if (visible) loadFromDisk()

    ScrollView {
        anchors.fill: parent
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 14

            // ---------------------------------------------------------------- 键位
            GroupBox {
                Layout.fillWidth: true
                title: "键位映射"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Text {
                        text: "简谱 1..7 对应的按键（游戏口琴界面的默认布局）"
                        color: Backend.mutedText
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }

                    GridLayout {
                        columns: 8
                        columnSpacing: 6
                        rowSpacing: 6

                        Repeater {
                            id: degreeRepeater
                            model: 7

                            delegate: ColumnLayout {
                                required property int index
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: String(index + 1)
                                    color: Backend.mutedText
                                    font.pixelSize: 11
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 56
                                    horizontalAlignment: Text.AlignHCenter
                                    text: String((root.draft.degrees || [])[index] || "")
                                    maximumLength: 12
                                }
                            }
                        }
                    }

                    RowLayout {
                        spacing: 8

                        Text {
                            text: "第 8 键（超高音 do）"
                            color: Backend.mutedText
                            font.pixelSize: 12
                        }
                        TextField {
                            id: highDoField
                            Layout.preferredWidth: 90
                            text: String(root.draft.highDo || "")
                        }
                        Text {
                            text: "可写 COMMA / PERIOD / SLASH / 单个字母或数字"
                            color: Backend.mutedText
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        spacing: 14

                        ColumnLayout {
                            spacing: 2
                            Text { text: "低音区"; color: Backend.mutedText; font.pixelSize: 11 }
                            ComboBox {
                                id: lowBox
                                model: ["left", "middle", "right", "none"]
                                currentIndex: Math.max(0, ["left", "middle", "right", "none"]
                                                       .indexOf(String(root.draft.low || "none")))
                            }
                        }
                        ColumnLayout {
                            spacing: 2
                            Text { text: "高音区"; color: Backend.mutedText; font.pixelSize: 11 }
                            ComboBox {
                                id: highBox
                                model: ["left", "middle", "right", "none"]
                                currentIndex: Math.max(0, ["left", "middle", "right", "none"]
                                                       .indexOf(String(root.draft.high || "none")))
                            }
                        }
                        ColumnLayout {
                            spacing: 2
                            Text { text: "升半音"; color: Backend.mutedText; font.pixelSize: 11 }
                            ComboBox {
                                id: sharpBox
                                model: ["left", "middle", "right", "none"]
                                currentIndex: Math.max(0, ["left", "middle", "right", "none"]
                                                       .indexOf(String(root.draft.sharp || "none")))
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        text: "某一档设成 none，那一档的音区会按中音区发出来，音高不对 —— 这是为了避开游戏自己的绑定，不是修漏音。"
                        color: Backend.mutedText
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ---------------------------------------------------------------- 时序
            GroupBox {
                Layout.fillWidth: true
                title: "时序"

                RowLayout {
                    anchors.fill: parent
                    spacing: 18

                    ColumnLayout {
                        spacing: 2
                        Text { text: "修饰键提前量 mouse_lead_ms"; color: Backend.mutedText; font.pixelSize: 11 }
                        SpinBox {
                            id: leadSpin
                            from: 0
                            to: 2000
                            value: Number(root.draft.mouseLeadMs || 0)
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        Text { text: "松键间隔 note_gap_ms"; color: Backend.mutedText; font.pixelSize: 11 }
                        SpinBox {
                            id: gapSpin
                            from: 0
                            to: 2000
                            value: Number(root.draft.noteGapMs || 0)
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            RowLayout {
                spacing: 10
                Button { text: "保存"; onClicked: root.save() }
                Button { text: "放弃改动"; onClicked: root.loadFromDisk() }
                Text {
                    visible: Backend.settingsError.length > 0
                    text: Backend.settingsError
                    color: Backend.accent
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                text: "配置写在 " + Backend.configFile
                      + (Backend.elevated ? "" : "  ⚠ 当前没有管理员权限，按键进不了游戏")
                color: Backend.elevated ? Backend.mutedText : Backend.accent
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            // ---------------------------------------------------------------- 校准
            GroupBox {
                Layout.fillWidth: true
                title: "键位校准（44 个音位）"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        text: "把 44 个音位按顺序各输入一遍，让你在游戏的「按键设置」里照着改。"
                              + "它和演奏走同一条动作表管道，所以校准听着对、演奏就对。"
                        color: Backend.mutedText
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        spacing: 10
                        Button {
                            text: "开始校准"
                            enabled: !Session.playing && !Session.countingDown
                            onClicked: Session.startCalibration()
                        }
                        Button {
                            text: "停止"
                            enabled: Session.playing || Session.countingDown
                            onClicked: Session.stop()
                        }
                        Text {
                            visible: Session.calibration && Session.currentStep >= 0
                            text: "第 " + (Session.currentStep + 1) + " / " + Session.calibrationSteps.length
                                  + " 个："
                                  + (Session.calibrationSteps[Session.currentStep] || {}).group
                            color: Backend.accent
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }
                    }

                    ColumnLayout {
                        spacing: 0
                        Repeater {
                            model: Session.calibrationSteps

                            delegate: RowLayout {
                                required property var modelData
                                required property int index
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 8
                                    color: index === Session.currentStep ? Backend.accent : "transparent"
                                    border.color: Backend.border
                                }
                                Text {
                                    text: (index + 1) + "."
                                    color: Backend.mutedText
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 28
                                }
                                Text {
                                    text: modelData.group
                                    color: Backend.windowText
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 150
                                }
                                Text {
                                    text: modelData.buttons.length > 0
                                          ? modelData.buttons.join(" + ") + " + " + modelData.key
                                          : modelData.key
                                    color: Backend.mutedText
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
