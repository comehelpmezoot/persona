import QtQuick

// Small vector icons drawn with Canvas, so nothing depends on Material Symbols / Nerd Fonts.
// name: "prev" | "next" | "volume" | "mute" | "brightness"
Canvas {
    id: root

    property string name: "volume"
    property color color: "white"

    width: 24
    height: 24
    antialiasing: true

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.clearRect(0, 0, width, height);
        ctx.scale(width / 24, height / 24);
        ctx.fillStyle = root.color;
        ctx.strokeStyle = root.color;
        ctx.lineWidth = 2;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";

        function speaker() {
            ctx.beginPath();
            ctx.moveTo(3, 9);
            ctx.lineTo(7, 9);
            ctx.lineTo(12, 4);
            ctx.lineTo(12, 20);
            ctx.lineTo(7, 15);
            ctx.lineTo(3, 15);
            ctx.closePath();
            ctx.fill();
        }

        if (name === "prev") {
            ctx.fillRect(4, 5, 2.5, 14);
            ctx.beginPath();
            ctx.moveTo(19, 5);
            ctx.lineTo(8.5, 12);
            ctx.lineTo(19, 19);
            ctx.closePath();
            ctx.fill();
        } else if (name === "next") {
            ctx.fillRect(17.5, 5, 2.5, 14);
            ctx.beginPath();
            ctx.moveTo(5, 5);
            ctx.lineTo(15.5, 12);
            ctx.lineTo(5, 19);
            ctx.closePath();
            ctx.fill();
        } else if (name === "volume") {
            speaker();
            ctx.beginPath();
            ctx.arc(12, 12, 5.5, -Math.PI / 3.2, Math.PI / 3.2, false);
            ctx.stroke();
            ctx.beginPath();
            ctx.arc(12, 12, 9.5, -Math.PI / 3.2, Math.PI / 3.2, false);
            ctx.stroke();
        } else if (name === "mute") {
            speaker();
            ctx.beginPath();
            ctx.moveTo(15.5, 9);
            ctx.lineTo(21.5, 15);
            ctx.moveTo(21.5, 9);
            ctx.lineTo(15.5, 15);
            ctx.stroke();
        } else if (name === "brightness") {
            ctx.beginPath();
            ctx.arc(12, 12, 4.5, 0, Math.PI * 2, false);
            ctx.fill();
            for (var i = 0; i < 8; i++) {
                var a = i * Math.PI / 4;
                ctx.beginPath();
                ctx.moveTo(12 + Math.cos(a) * 7.6, 12 + Math.sin(a) * 7.6);
                ctx.lineTo(12 + Math.cos(a) * 10.6, 12 + Math.sin(a) * 10.6);
                ctx.stroke();
            }
        }
    }
}
