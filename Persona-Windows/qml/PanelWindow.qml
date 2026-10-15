import QtQuick
import QtQml
import QtQuick.Window
import PersonaNative

// Emulates Quickshell's PanelWindow / WlrLayershell on Windows.
//
//   layer 0  desktop  - above the wallpaper, below normal windows (widgets)
//   layer 1  top      - above normal windows
//   layer 2  overlay  - above normal windows
//
// Anchors / margins / implicit size behave like layer-shell: anchored to both sides of an
// axis = stretch, anchored to one side = pinned there, anchored to none = centred.
Window {
    id: win

    property int layer: 1
    property int screenIndex: 0
    property bool anchorTop: false
    property bool anchorBottom: false
    property bool anchorLeft: false
    property bool anchorRight: false
    property real marginTop: 0
    property real marginBottom: 0
    property real marginLeft: 0
    property real marginRight: 0
    property real wantedWidth: 100
    property real wantedHeight: 100

    // Place inside the work area (above/below the taskbar) instead of the whole monitor.
    property bool useWorkArea: false
    // Can take keyboard focus (launcher, full-screen menus). Everything else never steals focus.
    property bool focusable: false
    // Mouse clicks go to whatever is underneath.
    property bool clickThrough: false
    // Opaque black background instead of a transparent one (wallpaper).
    property bool opaque: false
    // Apply geometry once instead of binding it (used by the wallpaper, which is re-parented
    // into the desktop and then positioned natively).
    property bool staticGeometry: false
    // Skip the automatic layer styling (the wallpaper does its own).
    property bool autoStyle: true
    // Debug: ordinary 1280x720 window (--windowed).
    property bool windowed: false

    readonly property var fallbackScreen: ({
        x: 0, y: 0, width: 1920, height: 1080, wx: 0, wy: 0, ww: 1920, wh: 1080
    })
    readonly property var scr: Shell.screens.length > 0
        ? Shell.screens[Math.max(0, Math.min(screenIndex, Shell.screens.length - 1))]
        : fallbackScreen

    readonly property real areaX: useWorkArea ? scr.wx : scr.x
    readonly property real areaY: useWorkArea ? scr.wy : scr.y
    readonly property real areaW: useWorkArea ? scr.ww : scr.width
    readonly property real areaH: useWorkArea ? scr.wh : scr.height

    readonly property real targetW: windowed ? 1280
        : ((anchorLeft && anchorRight) ? Math.max(1, areaW - marginLeft - marginRight) : wantedWidth)
    readonly property real targetH: windowed ? 720
        : ((anchorTop && anchorBottom) ? Math.max(1, areaH - marginTop - marginBottom) : wantedHeight)
    readonly property real targetX: windowed ? areaX + (areaW - 1280) / 2
        : (anchorLeft ? areaX + marginLeft
            : (anchorRight ? areaX + areaW - targetW - marginRight
                : areaX + (areaW - targetW) / 2))
    readonly property real targetY: windowed ? areaY + (areaH - 720) / 2
        : (anchorTop ? areaY + marginTop
            : (anchorBottom ? areaY + areaH - targetH - marginBottom
                : areaY + (areaH - targetH) / 2))

    Binding { target: win; property: "x"; value: win.targetX; when: !win.staticGeometry }
    Binding { target: win; property: "y"; value: win.targetY; when: !win.staticGeometry }
    Binding { target: win; property: "width"; value: win.targetW; when: !win.staticGeometry }
    Binding { target: win; property: "height"; value: win.targetH; when: !win.staticGeometry }

    color: opaque ? "black" : "transparent"
    title: "Persona Shell"

    flags: windowed ? Qt.Window
        : (Qt.FramelessWindowHint | Qt.Tool
            | (layer >= 1 ? Qt.WindowStaysOnTopHint : 0)
            | (focusable ? 0 : Qt.WindowDoesNotAcceptFocus)
            | (clickThrough ? Qt.WindowTransparentForInput : 0))

    Component.onCompleted: {
        if (staticGeometry) {
            x = targetX;
            y = targetY;
            width = targetW;
            height = targetH;
        }
    }

    Timer {
        id: styleTimer
        interval: 60
        onTriggered: {
            if (win.visible && win.autoStyle && !win.windowed)
                Shell.styleWindow(win, win.layer, win.focusable);
        }
    }
    onVisibleChanged: {
        if (visible)
            styleTimer.restart();
    }
}
