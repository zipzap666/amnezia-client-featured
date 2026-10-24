import QtQuick
import QtQuick.Layouts
import Style 1.0
import "../Controls2/TextTypes"

ColumnLayout {
    id: root
    property var samples: []
    property real now: 0
    property int seconds: 60
    property var formatRate: function(value) { return value.toFixed(0) + " B/s" }
    readonly property color inColor: AmneziaStyle.color.goldenApricot
    readonly property color outColor: AmneziaStyle.color.softViolet
    property real peak: 1
    spacing: 4

    function redraw() {
        var maximum = 1
        for (var i = 0; i < samples.length; ++i) {
            var s = samples[i]
            if (s.valid && s.time >= now - seconds * 1000)
                maximum = Math.max(maximum, s.rx, s.tx)
        }
        peak = maximum * 1.15
        plot.requestPaint()
    }
    onSamplesChanged: redraw()
    onNowChanged: redraw()
    onSecondsChanged: redraw()
    ParagraphTextType {
        Layout.fillWidth: true
        text: root.formatRate(root.peak)
        font.pixelSize: 12
    }
    Canvas {
        id: plot
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        onWidthChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = AmneziaStyle.color.charcoalGray
            ctx.lineWidth = 1
            for (var g = 0; g <= 4; ++g) {
                var y = 2 + (height - 4) * g / 4
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
            for (var direction = 0; direction < 2; ++direction) {
                ctx.strokeStyle = direction === 0 ? root.inColor : root.outColor
                ctx.lineWidth = 2
                ctx.beginPath()
                var previous = -1
                for (var i = 0; i < root.samples.length; ++i) {
                    var s = root.samples[i]
                    if (s.time < root.now - root.seconds * 1000) continue
                    if (!s.valid) { previous = -1; continue }
                    var x = width * (1 - (root.now - s.time) / (root.seconds * 1000))
                    var v = direction === 0 ? s.rx : s.tx
                    var py = height - 2 - (height - 4) * v / root.peak
                    if (previous < 0 || s.time - previous > 5000) ctx.moveTo(x, py)
                    else ctx.lineTo(x, py)
                    previous = s.time
                }
                ctx.stroke()
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        ParagraphTextType { text: "−" + root.seconds + qsTr(" s"); font.pixelSize: 12 }
        Item { Layout.fillWidth: true }
        ParagraphTextType { text: qsTr("Now"); font.pixelSize: 12 }
    }
}
