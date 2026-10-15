import QtQuick
import PersonaNative

Item {
    id: root
    clip: false

    // Pause the audio capture (and all drawing) when false, e.g. while a game is fullscreen.
    property bool running: true

    CavaMonitor {
        id: cava
        bars: 50
        active: root.running
    }

    Canvas {
        id: canvas
        clip: false
        anchors.fill: parent
        renderTarget: Canvas.Image
        renderStrategy: Canvas.Threaded

        Connections {
            target: cava
            function onValuesChanged() {
                canvas.requestPaint();
            }
        }
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            drawMountainWave(ctx, cava.values, true);
            drawMountainWave(ctx, cava.values, false);
        }
        function drawMountainWave(ctx, data, isShadow) {
            if (!data || data.length < 2)
                return;
            var hPad = width * 0.03;
            var drawW = width - 2 * hPad;
            var barWidth = drawW / (data.length - 1);
            function xOf(i) {
                return hPad + i * barWidth;
            }

            var baseline = height;

            function barTopY(i) {
                return baseline - data[i] * baseline * 0.9;
            }

            var gradient = ctx.createLinearGradient(0, 0, width, 0);
            gradient.addColorStop(0.0, Qt.rgba(1, 1, 1, 0.12));
            gradient.addColorStop(0.3, Qt.rgba(1, 1, 1, 0.25));
            gradient.addColorStop(0.5, Qt.rgba(1, 1, 1, 0.30));
            gradient.addColorStop(0.7, Qt.rgba(1, 1, 1, 0.25));
            gradient.addColorStop(1.0, Qt.rgba(1, 1, 1, 0.12));

            ctx.beginPath();
            if (isShadow) {
                ctx.globalAlpha = 0.3;
                ctx.save();
                ctx.translate(0, -10);
                ctx.scale(1.02, 1.05);
            } else {
                ctx.globalAlpha = 1.0;
            }
            ctx.fillStyle = gradient;
            ctx.moveTo(xOf(0), baseline);
            ctx.lineTo(xOf(0), barTopY(0));
            for (var i = 0; i < data.length - 1; i++) {
                var xC = xOf(i), yC = barTopY(i);
                var xN = xOf(i + 1), yN = barTopY(i + 1);
                ctx.quadraticCurveTo(xC, yC, (xC + xN) / 2, (yC + yN) / 2);
            }
            var last = data.length - 1;
            ctx.lineTo(xOf(last), barTopY(last));
            ctx.lineTo(xOf(last), baseline);
            ctx.lineTo(xOf(0), baseline);
            ctx.closePath();
            ctx.fill();
            if (isShadow)
                ctx.restore();
        }
    }
}
