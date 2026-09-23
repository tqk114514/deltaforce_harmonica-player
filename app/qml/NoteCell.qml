import QtQuick
import Harmonica

/// 谱面上的一个音。画什么由 `Editor.placed` 那份排版结果决定，
/// 这里不重算时值、也不判断该不该共用减时线。
Item {
    id: cell

    required property var info
    property bool picked: false
    property bool tieSource: false
    property bool sharedWithNext: false

    signal clicked()
    signal doubleClicked()

    implicitWidth: 26 + (info.holdBars || 0) * 11 + (info.dot ? 6 : 0)
    implicitHeight: 62

    MouseArea {
        anchors.fill: parent
        anchors.margins: -1
        cursorShape: Qt.PointingHandCursor
        onClicked: cell.clicked()
        onDoubleClicked: cell.doubleClicked()
    }

    // 选中 / 连音线起点的底色
    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        radius: 4
        color: cell.picked ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        border.color: cell.tieSource ? Backend.accent : (cell.picked ? Backend.border : "transparent")
        border.width: cell.tieSource || cell.picked ? 1 : 0
    }

    // 小节线：新小节开头画一条竖线，右上角标小节号
    Rectangle {
        visible: info.measureStart
        x: -2
        y: 8
        width: 1
        height: parent.height - 16
        color: Backend.border
    }
    Text {
        visible: info.measureStart
        x: -1
        y: -12
        text: "小" + (info.measure + 1)
        color: Backend.mutedText
        font.pixelSize: 9
    }

    // 反复记号
    Text {
        visible: info.rstart || info.rend
        x: 0
        y: -14
        text: info.rstart ? "‖:" : ":‖"
        color: Backend.accent
        font.pixelSize: 11
    }
    // 转调标记
    Text {
        visible: (info.modLabel || "").length > 0
        x: 10
        y: -14
        text: info.modLabel
        color: Backend.accent
        font.pixelSize: 10
    }

    // 倚音：数字左上角的小音符，自己的八度点画在它上面（低音在下面），
    // 底下那条短横是它自己的减时线，右边的折线指向主音
    Item {
        id: graceBox
        visible: cell.hasGrace
        x: -12
        y: 2
        width: 14
        height: 26

        property var grace: cell.info.grace

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 6
            text: (graceBox.grace.sharp ? "#" : "") + (graceBox.grace.octave >= 2 ? 1 : graceBox.grace.degree)
            color: Backend.mutedText
            font.pixelSize: 11
        }
        // 高八度点（超高音两个竖排）
        Repeater {
            model: graceBox.grace && graceBox.grace.octave > 0 ? graceBox.grace.octave : 0
            delegate: Rectangle {
                required property int index
                width: 2
                height: 2
                radius: 1
                color: Backend.mutedText
                anchors.horizontalCenter: parent.horizontalCenter
                y: 2 - index * 4
            }
        }
        Repeater {
            model: graceBox.grace && graceBox.grace.octave < 0 ? -graceBox.grace.octave : 0
            delegate: Rectangle {
                required property int index
                width: 2
                height: 2
                radius: 1
                color: Backend.mutedText
                anchors.horizontalCenter: parent.horizontalCenter
                y: 19 + index * 4
            }
        }
        // 它自己的减时线
        Repeater {
            model: graceBox.grace ? (graceBox.grace.dur === 32 ? 3 : graceBox.grace.dur === 16 ? 2 : 1) : 0
            delegate: Rectangle {
                required property int index
                x: 1
                y: 22 + index * 2
                width: parent.width - 2
                height: 1
                color: Backend.mutedText
            }
        }
    }
    property bool hasGrace: !!(info.grace)
    Canvas {
        id: graceTail
        visible: cell.hasGrace
        x: 0
        y: 26
        width: 12
        height: 14
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = Backend.mutedText
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(width, height / 2)
            ctx.lineTo(0, height)
            ctx.stroke()
        }
    }

    // 主音
    Text {
        id: main
        x: 6
        y: 14
        text: info.rest ? "0" : (info.sharp ? "#" : "") + (info.octave >= 2 ? 1 : info.degree)
        color: info.rest ? Backend.mutedText : (cell.picked ? Backend.windowText : Backend.windowText)
        font.pixelSize: 20
    }

    // 附点
    Rectangle {
        visible: info.dot
        x: main.x + main.implicitWidth + 2
        y: 24
        width: 3
        height: 3
        radius: 1.5
        color: Backend.windowText
    }

    // 增时线：音符右边的横杠，一条一拍
    Repeater {
        model: info.holdBars || 0
        delegate: Rectangle {
            required property int index
            x: main.x + main.implicitWidth + (info.dot ? 9 : 4) + index * 11
            y: 25
            width: 9
            height: 2
            color: Backend.windowText
        }
    }

    // 八度点：高音在上（超高音竖排两个），低音在下
    Repeater {
        model: info.octave > 0 ? info.octave : 0
        delegate: Rectangle {
            required property int index
            width: 3
            height: 3
            radius: 1.5
            color: Backend.windowText
            x: main.x + main.implicitWidth / 2 - 1.5
            y: 8 - index * 5
        }
    }
    Repeater {
        model: info.octave < 0 ? -info.octave : 0
        delegate: Rectangle {
            required property int index
            width: 3
            height: 3
            radius: 1.5
            color: Backend.windowText
            x: main.x + main.implicitWidth / 2 - 1.5
            y: 38 + index * 5
        }
    }

    // 减时线：同一拍内、层数相同的相邻音共用一条线 —— 该不该共用是排版算好的
    Repeater {
        model: info.beamLevels || 0
        delegate: Rectangle {
            required property int index
            x: cell.info.beamSharedWithPrev ? cell.width * 0.4 - 4 : -2
            width: cell.width - x + (cell.sharedWithNext ? 6 : -2)
            y: 46 + index * 4
            height: 1.6
            color: Backend.windowText
        }
    }
}
