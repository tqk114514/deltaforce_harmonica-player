import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Harmonica

/// 简谱编辑器。
///
/// 页面只管录入和画：时值、延音线合并、反复展开、转调折算、倚音借时值、
/// 小节与减时线的排版，全在 `Editor` / `app/scoredoc` 那一份实现里。
Item {
    id: root

    property var cells: []

    focus: true

    function refreshCells() {
        cells = Editor.placed
    }

    Component.onCompleted: {
        refreshCells()
        if (Editor.hasAutosave() && Editor.count > 0) Editor.restoreAutosave()
    }

    onVisibleChanged: if (visible) focus = true


    // 键盘录入：数字 1..7 是音高，0 是休止，# 升号，- 增时线，
    // 上下换八度、左右换时值、Backspace 删、t 连音线、Esc 取消
    Keys.onPressed: (event) => {
        const key = event.key
        const text = event.text
        if (text >= "1" && text <= "7") {
            Editor.append(parseInt(text))
        } else if (text === "0") {
            Editor.append(0)
        } else if (text === "#") {
            Editor.curSharp = !Editor.curSharp
            Editor.applyToSelection()
        } else if (text === "-") {
            Editor.appendHold()
        } else if (key === Qt.Key_Up) {
            Editor.curOctave = Math.min(2, Editor.curOctave + 1)
            Editor.applyToSelection()
        } else if (key === Qt.Key_Down) {
            Editor.curOctave = Math.max(-1, Editor.curOctave - 1)
            Editor.applyToSelection()
        } else if (key === Qt.Key_Left) {
            Editor.moveSelected(-1)
        } else if (key === Qt.Key_Right) {
            Editor.moveSelected(1)
        } else if (key === Qt.Key_Backspace) {
            Editor.removeSelected()
        } else if (text === "t" || text === "T") {
            if (Editor.tieFrom >= 0) Editor.cancelTie()
            else if (Editor.selected >= 0) Editor.pickTieTarget(Editor.selected)
        } else if (key === Qt.Key_Escape) {
            Editor.cancelTie()
        } else {
            return
        }
        event.accepted = true
    }

    Connections {
        target: Editor
        function onDocumentChanged() {
            root.cells = Editor.placed
            tieCanvas.requestPaint()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // -------------------------------------------------------- 文件与谱头
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                Layout.preferredWidth: 170
                placeholderText: "曲名"
                text: Editor.title
                onEditingFinished: Editor.title = text
            }
            RowLayout {
                spacing: 2
                Text { text: "BPM"; color: Backend.mutedText; font.pixelSize: 11 }
                SpinBox {
                    from: 20
                    to: 400
                    value: Editor.bpm
                    onValueModified: Editor.bpm = value
                }
            }
            RowLayout {
                spacing: 2
                Text { text: "拍号"; color: Backend.mutedText; font.pixelSize: 11 }
                ComboBox {
                    model: ["4/4", "3/4", "2/4"]
                    currentIndex: Editor.meter === 3 ? 1 : (Editor.meter === 2 ? 2 : 0)
                    onActivated: (i) => Editor.meter = [4, 3, 2][i]
                }
            }
            CheckBox {
                text: "小节线"
                checked: Editor.barlines
                onToggled: Editor.barlines = checked
            }

            Item { Layout.fillWidth: true }

            Button {
                text: Preview.playing ? "停下" : "试听"
                onClicked: {
                    if (Preview.playing) Preview.stop()
                    else Preview.play(Editor.dhsText())
                }
            }
            Button { text: "清空"; onClicked: clearDialog.open() }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                id: projectPicker
                Layout.preferredWidth: 200
                model: Editor.projects
                displayText: currentText.length > 0 ? currentText : "选一份工程…"
            }
            Button {
                text: "打开工程"
                enabled: projectPicker.currentText.length > 0
                onClicked: Editor.openProject(projectPicker.currentText)
            }
            Button { text: "自动保存" ; onClicked: Editor.autosave() }
            ComboBox {
                id: dhsPicker
                Layout.preferredWidth: 160
                model: Backend.listSongs().map(function (s) { return s.file })
                displayText: currentText.length > 0 ? currentText : "打开 .dhs…"
            }
            Button {
                text: "导入"
                enabled: dhsPicker.currentText.length > 0
                onClicked: Editor.openDhs(dhsPicker.currentText)
            }
            Button {
                text: "保存为…"
                onClicked: { saveDialog.open() }
            }
            Button {
                text: "导出 .dhs"
                onClicked: exportDialog.open()
            }
            Item { Layout.fillWidth: true }
        }

        // -------------------------------------------------------- 录入工具
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Repeater {
                model: [4, 8, 16, 32]
                delegate: Button {
                    required property int modelData
                    text: ({ 4: "四分", 8: "八分", 16: "十六分", 32: "三十二分" })[modelData]
                    highlighted: Editor.curDur === modelData
                    onClicked: {
                        Editor.curDur = modelData
                        Editor.applyToSelection()
                    }
                }
            }
            Button {
                text: "附点"
                highlighted: Editor.curDot
                onClicked: { Editor.curDot = !Editor.curDot; Editor.applyToSelection() }
            }
            Button {
                text: "升号"
                highlighted: Editor.curSharp
                onClicked: { Editor.curSharp = !Editor.curSharp; Editor.applyToSelection() }
            }
            Button { text: "休止"; onClicked: Editor.append(0) }
            Button { text: "延长一拍 -"; onClicked: Editor.appendHold() }
            Button { text: "收一拍"; enabled: Editor.selected >= 0; onClicked: Editor.removeHold() }
            Repeater {
                model: [[-1, "低音"], [0, "中音"], [1, "高音"], [2, "超高音"]]
                delegate: Button {
                    required property var modelData
                    text: modelData[1]
                    highlighted: Editor.curOctave === modelData[0]
                    onClicked: {
                        Editor.curOctave = modelData[0]
                        Editor.applyToSelection()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Button {
                text: Editor.tieFrom >= 0 ? "连音线：选终点" : "连音线"
                highlighted: Editor.tieFrom >= 0
                onClicked: {
                    if (Editor.tieFrom >= 0) Editor.cancelTie()
                    else if (Editor.selected >= 0) Editor.pickTieTarget(Editor.selected)
                }
            }
            Button {
                text: "‖: 反复开始"
                enabled: Editor.selected >= 0
                highlighted: Editor.selected >= 0 && (root.cells[Editor.selected] || {}).rstart === true
                onClicked: Editor.toggleRepeatStart()
            }
            Button {
                text: ":‖ 反复结束"
                enabled: Editor.selected >= 0
                highlighted: Editor.selected >= 0 && (root.cells[Editor.selected] || {}).rend === true
                onClicked: Editor.toggleRepeatEnd()
            }
            Button { text: "转调"; onClicked: modDialog.open() }
            Button { text: "倚音"; enabled: Editor.selected >= 0; onClicked: graceDialog.open() }
            Button { text: "删倚音"; enabled: Editor.selected >= 0; onClicked: Editor.clearGrace() }
            Item { Layout.fillWidth: true }
        }

        // -------------------------------------------------------- 谱面
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Qt.rgba(1, 1, 1, 0.03)
            border.color: Backend.border
            clip: true

            ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true

                Item {
                    width: flow.implicitWidth
                    height: flow.implicitHeight + 28

                    Flow {
                        id: flow
                        y: 14
                        width: parent.parent.width - 16
                        spacing: 2

                        Repeater {
                            model: root.cells

                            delegate: NoteCell {
                                id: cell
                                required property var modelData
                                required property int index
                                info: modelData
                                picked: index === Editor.selected
                                tieSource: index === Editor.tieFrom
                                sharedWithNext: index + 1 < root.cells.length
                                                && root.cells[index + 1].beamSharedWithPrev
                                                && root.cells[index + 1].beamLevels === modelData.beamLevels
                                onClicked: Editor.select(index)
                                onDoubleClicked: Editor.pickTieTarget(index)
                            }
                        }
                    }

                    // 连音线画在音符上方。弧的端点用每个格子的几何位置，
                    // 所以哪两个音连着仍然是数据说了算，这里只负责画
                    Canvas {
                        id: tieCanvas
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = Qt.rgba(Backend.accent.r, Backend.accent.g,
                                                     Backend.accent.b, 0.9)
                            ctx.lineWidth = 1.6
                            for (let i = 0; i < flow.count; i += 1) {
                                const from = flow.itemAt(i)
                                if (!from || !from.info) continue
                                const links = root.cells[i].links || []
                                for (const to of links) {
                                    const other = flow.itemAt(to)
                                    if (!other) continue
                                    const x1 = from.x + from.width / 2
                                    const x2 = other.x + other.width / 2
                                    const y = Math.min(from.y, other.y) - 2
                                    ctx.beginPath()
                                    ctx.moveTo(x1, y)
                                    ctx.quadraticCurveTo((x1 + x2) / 2, y - 16, x2, y)
                                    ctx.stroke()
                                }
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: root.cells.length === 0
                text: "点上面的时值和八度，然后敲数字 1..7 录入（0 是休止）"
                color: Backend.mutedText
                font.pixelSize: 13
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: Editor.status
                color: Backend.accent
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text {
                text: "选中：" + (Editor.selected >= 0 ? (Editor.selected + 1) : "无")
                      + "  共 " + Editor.count + " 个音"
                color: Backend.mutedText
                font.pixelSize: 12
            }
        }
    }

    // -------------------------------------------------------- 对话框
    Dialog {
        id: modDialog
        modal: true
        title: "转调"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: 6
            TextField {
                id: modField
                Layout.fillWidth: true
                placeholderText: "前3=后5，或直接填半音数"
            }
            Text {
                text: "简谱是首调记法，「前 a=后 b」本身就含了升降多少半音，不需要知道原调。会自动对齐到本小节第一个音。"
                color: Backend.mutedText
                font.pixelSize: 11
                Layout.preferredWidth: 320
                wrapMode: Text.WordWrap
            }
        }
        onAccepted: Editor.setMod(modField.text)
    }

    Dialog {
        id: graceDialog
        modal: true
        title: "倚音"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        property int degree: 1
        property int octave: 0
        property bool sharp: false
        property int dur: 16

        ColumnLayout {
            spacing: 8

            RowLayout {
                spacing: 4
                Repeater {
                    model: [1, 2, 3, 4, 5, 6, 7]
                    delegate: Button {
                        required property int modelData
                        text: modelData
                        highlighted: graceDialog.degree === modelData
                        onClicked: { graceDialog.degree = modelData; graceDialog.preview() }
                    }
                }
            }
            RowLayout {
                spacing: 4
                Repeater {
                    model: [[-1, "低"], [0, "中"], [1, "高"], [2, "超高"]]
                    delegate: Button {
                        required property var modelData
                        text: modelData[1]
                        highlighted: graceDialog.octave === modelData[0]
                        onClicked: { graceDialog.octave = modelData[0]; graceDialog.preview() }
                    }
                }
                Button {
                    text: "#"
                    highlighted: graceDialog.sharp
                    onClicked: { graceDialog.sharp = !graceDialog.sharp; graceDialog.preview() }
                }
            }
            RowLayout {
                spacing: 4
                Repeater {
                    model: [8, 16, 32]
                    delegate: Button {
                        required property int modelData
                        text: ({ 8: "八分", 16: "十六分", 32: "三十二分" })[modelData]
                        highlighted: graceDialog.dur === modelData
                        onClicked: { graceDialog.dur = modelData; graceDialog.preview() }
                    }
                }
            }
            Text {
                id: graceHintText
                text: "演奏时倚音先响，占的是主音前面的一段时间 —— 最多借主音的一半，总时长不变"
                color: Backend.mutedText
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        function preview() {
            graceHintText.text = Editor.graceHint(graceDialog.degree, graceDialog.octave,
                                                   graceDialog.sharp, graceDialog.dur)
        }
        onAccepted: Editor.setGrace(graceDialog.degree, graceDialog.octave, graceDialog.sharp,
                                     graceDialog.dur)
    }

    Dialog {
        id: clearDialog
        modal: true
        title: "清空谱面？"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "当前 " + Editor.count + " 个音都会被删掉，没有后悔药。" }
        onAccepted: Editor.clearAll()
    }

    Dialog {
        id: saveDialog
        modal: true
        title: "保存工程"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField {
            id: saveName
            placeholderText: "曲子.score.json"
            text: Editor.title.length > 0 ? Editor.title.replace(/\s+/g, "_") + ".score.json" : ""
        }
        onAccepted: Editor.saveProject(saveName.text)
    }

    Dialog {
        id: exportDialog
        modal: true
        title: "导出 .dhs 进曲库"
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField {
            id: exportName
            placeholderText: "曲子.dhs"
            text: Editor.title.length > 0 ? Editor.title.replace(/\s+/g, "_") + ".dhs" : ""
        }
        onAccepted: Editor.exportDhs(exportName.text)
    }
}
