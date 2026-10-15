import QtQuick
import PersonaNative

// The animated Persona 3 Reload wallpaper: ripple -> rain/stars -> composite (bars, visualizer,
// foreground) -> mouse parallax. One instance per monitor.
//
// On Windows the window is re-parented into the desktop's "WorkerW" window so it renders
// behind the desktop icons, exactly like a live wallpaper.
PanelWindow {
    id: root

    property real mouseOffsetX: 0.0
    property real mouseOffsetY: 0.0
    // false while a fullscreen game / video is running
    readonly property bool animating: !Shell.animationsPaused

    layer: 0
    opaque: true
    staticGeometry: true
    autoStyle: false
    focusable: false
    anchorLeft: true
    anchorRight: true
    anchorTop: true
    anchorBottom: true
    windowed: Shell.windowedMode
    visible: true

    function attachToDesktop() {
        if (Shell.windowedMode)
            return;
        if (!Shell.attachWallpaper(root, root.screenIndex)) {
            console.warn("Wallpaper: could not attach to the desktop; falling back to the desktop layer");
            Shell.styleWindow(root, 0, false);
        }
    }

    Timer {
        id: attachTimer
        interval: 300
        onTriggered: root.attachToDesktop()
    }
    onVisibleChanged: {
        if (visible) {
            attachTimer.interval = 300;
            attachTimer.restart();
        }
    }
    Connections {
        target: Shell
        function onExplorerRestarted() {
            attachTimer.interval = 2500; // give the new explorer.exe time to build its windows
            attachTimer.restart();
        }
        function onScreensChanged() {
            attachTimer.interval = 800;
            attachTimer.restart();
        }
    }

    // The wallpaper sits behind the desktop icons and never receives mouse events, so the
    // cursor is polled instead of using a MouseArea.
    Timer {
        interval: 16
        repeat: true
        running: root.visible && root.animating
        onTriggered: {
            var p = Shell.cursorPos();
            var s = root.scr;
            var lx = p.x - s.x;
            var ly = p.y - s.y;
            if (lx >= 0 && ly >= 0 && lx < s.width && ly < s.height) {
                root.mouseOffsetX = (lx / s.width - 0.5) * 2.0;
                root.mouseOffsetY = (ly / s.height - 0.5) * 2.0;
            }
        }
    }

    Image {
        id: bgRaw
        source: Shell.asset("p3r imgs/bg.png")
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        visible: false
    }

    Image {
        id: cloudMaskRaw
        source: Shell.asset("Depth masks/cloudmask.png")
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        visible: false
    }

    Image {
        id: normalMapRipple
        source: Shell.asset("Depth masks/normalmaps/waterripplenormal.png")
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        visible: false
    }

    Image {
        id: barsRaw
        source: Shell.asset("p3r imgs/bars.png")
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        visible: false
    }

    Image {
        id: depthMapRaw
        source: Shell.asset("Depth masks/makotodepth.png")
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        visible: false
    }

    // ── Stage 0a: Ripple ──
    ShaderEffect {
        id: s0_bg_clouds
        anchors.fill: parent
        visible: true

        property var source: bgRaw
        property var normalMap: normalMapRipple
        property var depthMask: cloudMaskRaw
        property real time: 0
        property real flowStrength: 0.006
        property real speed: 2.5
        property real frequency: 1.0

        NumberAnimation on time {
            from: 0
            to: 10
            duration: 800000
            loops: Animation.Infinite
            running: true
            paused: !root.animating
        }

        vertexShader: "qrc:/shaders/ripple/ripple.vert.qsb"
        fragmentShader: "qrc:/shaders/ripple/ripple.frag.qsb"
    }

    ShaderEffectSource {
        id: s0_clouds_out
        sourceItem: s0_bg_clouds
        anchors.fill: parent
        visible: false
        hideSource: true
        live: true
    }

    // ── Stage 0b: Stars/Rain ──
    ShaderEffect {
        id: s0_bg_stars
        anchors.fill: parent
        visible: false

        property var source: s0_clouds_out
        property real time: 0
        property real strength: 50
        property real speed: 5.5
        property real frequency: 10.0

        NumberAnimation on time {
            from: 0
            to: 1000
            duration: 500000
            loops: Animation.Infinite
            running: true
            paused: !root.animating
        }

        vertexShader: "qrc:/shaders/stars/stars.vert.qsb"
        fragmentShader: "qrc:/shaders/stars/stars.frag.qsb"
    }

    // ── Stage 1: Composite ──
    Item {
        id: s1_composite
        anchors.fill: parent
        visible: false

        ShaderEffectSource {
            id: s0_bg_out
            sourceItem: s0_bg_stars
            anchors.fill: parent
            hideSource: true
            live: true
        }

        ShaderEffect {
            id: s1_bars_motion
            anchors.fill: parent

            property var source: barsRaw
            property real time: 0
            property real speed: 1

            NumberAnimation on time {
                from: 0
                to: 10000
                duration: 10000000
                loops: Animation.Infinite
                running: true
                paused: !root.animating
            }

            vertexShader: "qrc:/shaders/motion/motion.vert.qsb"
            fragmentShader: "qrc:/shaders/motion/motion.frag.qsb"
        }

        CavaVisualizer {
            id: s1_cava
            running: root.animating && Shell.config.cava !== false
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                topMargin: 0
            }
            height: 555
        }

        Image {
            id: s1_fg
            source: Shell.asset("p3r imgs/fg.png")
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            smooth: true
            mipmap: true
        }
    }

    // ── Composite output ──
    ShaderEffectSource {
        id: s1_out
        sourceItem: s1_composite
        anchors.fill: parent
        visible: false
        hideSource: true
        live: true
    }

    // ── Stage 2: Parallax ──
    ShaderEffect {
        id: s2_parallax
        anchors.fill: parent
        visible: true

        property var source: s1_out
        property real offsetX: root.mouseOffsetX
        property real offsetY: root.mouseOffsetY
        property real parallaxStrength: 0.03
        property real aspectRatio: width / height
        property var depthMap: depthMapRaw

        vertexShader: "qrc:/shaders/parallax/parallax.vert.qsb"
        fragmentShader: "qrc:/shaders/parallax/parallax.frag.qsb"
    }
}
