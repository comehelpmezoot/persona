import QtQuick

// Little battery glyph (replaces the Nerd Font battery icons).
Item {
    id: root

    property real size: 20
    property color color: "#fffb9f"
    property real level: 1.0
    property bool charging: false

    width: size * 1.85
    height: size

    Rectangle {
        id: body
        x: 0
        y: root.size * 0.12
        width: root.size * 1.6
        height: root.size * 0.76
        radius: root.size * 0.14
        color: "transparent"
        border.color: root.color
        border.width: Math.max(1, root.size * 0.08)

        Rectangle {
            readonly property real pad: body.border.width + Math.max(1, root.size * 0.06)
            x: pad
            y: pad
            height: parent.height - 2 * pad
            width: Math.max(0, (parent.width - 2 * pad) * Math.max(0, Math.min(1, root.level)))
            radius: Math.max(1, root.size * 0.06)
            color: root.color
        }

        Canvas {
            anchors.centerIn: parent
            width: root.size * 0.5
            height: root.size * 0.7
            visible: root.charging
            onVisibleChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.clearRect(0, 0, width, height);
                ctx.fillStyle = "#0c0f1d";
                ctx.strokeStyle = root.color;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(width * 0.6, 0);
                ctx.lineTo(width * 0.05, height * 0.58);
                ctx.lineTo(width * 0.48, height * 0.58);
                ctx.lineTo(width * 0.38, height);
                ctx.lineTo(width * 0.95, height * 0.38);
                ctx.lineTo(width * 0.52, height * 0.38);
                ctx.closePath();
                ctx.fill();
                ctx.stroke();
            }
        }
    }

    Rectangle {
        x: root.size * 1.6
        y: root.size * 0.36
        width: root.size * 0.18
        height: root.size * 0.28
        radius: 1
        color: root.color
    }
}
