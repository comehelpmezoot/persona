import QtQuick
import PersonaNative

Scope {
    id: root

    function show() {
        osd.screenIndex = Shell.cursorScreenIndex();
        osd.shown = true;
        hideTimer.restart();
    }

    Connections {
        target: AudioCtl
        function onUserAdjusted() {
            root.show();
        }
    }

    Timer {
        id: hideTimer
        interval: 1000
        onTriggered: osd.shown = false
    }

    Osd {
        id: osd
        iconName: (AudioCtl.muted || Math.round(AudioCtl.volume * 100) === 0) ? "mute" : "volume"
        level: AudioCtl.volume
    }
}
