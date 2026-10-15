import QtQuick
import QtQml
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects
import PersonaNative

// The app launcher. Opened with the global hotkey (default Alt+Space), the tray icon, or
// `PersonaShell.exe --toggle-launcher` (the replacement for `qs ipc call searchapp toggle`).
Scope {
    id: root
    property bool forcedOpen: false
    // monitor the launcher is shown on (the one the mouse cursor is on when it opens)
    property int targetScreen: 0

    function openLauncher() {
        root.targetScreen = Shell.cursorScreenIndex();
        Apps.refresh();
        root.forcedOpen = true;
    }
    function closeLauncher() {
        root.forcedOpen = false;
    }

    Connections {
        target: Shell
        function onLauncherToggleRequested() {
            if (root.forcedOpen)
                root.closeLauncher();
            else
                root.openLauncher();
        }
        function onLauncherOpenRequested() {
            root.openLauncher();
        }
        function onLauncherCloseRequested() {
            root.closeLauncher();
        }
    }

    Instantiator {
        model: Shell.screens.length

        delegate: PanelWindow {
            id: window

            screenIndex: index
            visible: root.forcedOpen && index === root.targetScreen
            layer: 1
            useWorkArea: true
            anchorTop: true
            anchorLeft: true
            anchorBottom: true
            wantedWidth: 450
            focusable: true

            function isSubsequence(query, name) {
                let ni = 0;
                for (let si = 0; si < query.length; ++si) {
                    const found = name.indexOf(query[si], ni);
                    if (found < 0)
                        return false;
                    ni = found + 1;
                }
                return true;
            }

            // Prefix matches first, then substring matches, then fuzzy (in-order letters) matches.
            property var filteredApps: {
                const all = Apps.list;
                const q = search ? search.text.toLowerCase().trim() : "";
                if (q === "")
                    return all;
                const scored = [];
                for (let i = 0; i < all.length; ++i) {
                    const name = all[i].name.toLowerCase();
                    let score = -1;
                    if (name.startsWith(q))
                        score = 0;
                    else if (name.indexOf(q) >= 0)
                        score = 1;
                    else if (isSubsequence(q, name))
                        score = 2;
                    if (score >= 0)
                        scored.push({ score: score, index: i });
                }
                scored.sort((a, b) => (a.score - b.score) || (a.index - b.index));
                return scored.map(s => all[s.index]);
            }

            // Windows has no exclusive keyboard focus: close the launcher when it loses focus.
            Timer {
                id: blurCloseTimer
                interval: 150
                repeat: false
                onTriggered: {
                    if (window.visible && !window.active && root.forcedOpen)
                        root.forcedOpen = false;
                }
            }
            onActiveChanged: {
                if (active)
                    blurCloseTimer.stop();
                else if (visible)
                    blurCloseTimer.restart();
            }

            Timer {
                id: focusTimer
                interval: 50
                repeat: false
                onTriggered: {
                    if (window.visible) {
                        Shell.activate(window);
                        search.forceActiveFocus();
                        appList.currentIndex = 0;
                    }
                }
            }

            onVisibleChanged: {
                if (visible) {
                    search.text = "";
                    focusTimer.restart();
                }
            }

            Item {
                id: panelRoot
                anchors.left: parent.left
                anchors.leftMargin: 50
                anchors.verticalCenter: parent.verticalCenter
                width: 380
                height: 700
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    color: "transparent"
                    border.color: "#ffffff"
                    border.width: 3
                    radius: 6
                    transform: Matrix4x4 {
                        matrix: Qt.matrix4x4(1, -0.04, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    color: "#111827"
                    radius: 4
                    opacity: 0.97
                    transform: Matrix4x4 {
                        matrix: Qt.matrix4x4(1, -0.04, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                    }
                }
                Column {
                    anchors.fill: parent
                    anchors.margins: 0
                    spacing: 0
                    Item {
                        width: parent.width
                        height: 80
                        Canvas {
                            anchors.left: parent.left
                            anchors.leftMargin: 20
                            anchors.verticalCenter: parent.verticalCenter
                            width: 160
                            height: 50
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.clearRect(0, 0, width, height);
                                ctx.beginPath();
                                ctx.moveTo(0, 0);
                                ctx.lineTo(width, 0);
                                ctx.lineTo(width - 20, height);
                                ctx.lineTo(0, height);
                                ctx.closePath();
                                ctx.fillStyle = "#1a6aff";
                                ctx.fill();
                            }
                            Text {
                                anchors.centerIn: parent
                                text: "APPS"
                                font.pixelSize: 22
                                font.bold: true
                                color: "#ffffff"
                                font.letterSpacing: 3
                            }
                        }
                        Rectangle {
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            width: 160
                            height: 32
                            color: "transparent"
                            border.color: "#1a6aff"
                            border.width: 1
                            radius: 3

                            TextInput {
                                id: search
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                color: "#ffffff"
                                verticalAlignment: TextInput.AlignVCenter
                                font.pixelSize: 13
                                clip: true

                                Text {
                                    anchors.fill: parent
                                    verticalAlignment: Text.AlignVCenter
                                    text: "Search..."
                                    color: "#44ffffff"
                                    font.pixelSize: 13
                                    visible: search.text === ""
                                }

                                Keys.onEscapePressed: root.closeLauncher()
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_Up) {
                                        appList.currentIndex = Math.max(0, appList.currentIndex - 1);
                                        event.accepted = true;
                                    } else if (event.key === Qt.Key_Down) {
                                        appList.currentIndex = Math.min(filteredApps.length - 1, appList.currentIndex + 1);
                                        event.accepted = true;
                                    }
                                }
                                onAccepted: {
                                    if (filteredApps.length > 0) {
                                        Apps.launch(filteredApps[appList.currentIndex].id);
                                        root.closeLauncher();
                                    }
                                }
                                onTextChanged: appList.currentIndex = 0
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 2
                        color: "#1a6aff"
                        opacity: 0.6
                    }
                    ListView {
                        id: appList
                        width: parent.width
                        height: panelRoot.height - 82
                        clip: true
                        currentIndex: 0
                        model: filteredApps

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            contentItem: Rectangle {
                                implicitWidth: 4
                                color: "#1a6aff"
                                opacity: 0.6
                                radius: 2
                            }
                        }

                        delegate: Item {
                            id: delegateRoot
                            required property var modelData
                            required property int index
                            width: appList.width
                            height: 80

                            property bool isActive: appList.currentIndex === index

                            Rectangle {
                                anchors.fill: parent
                                color: delegateRoot.isActive ? "#ffffff" : "transparent"
                                opacity: delegateRoot.isActive ? 1 : 0
                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 150
                                    }
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 4
                                color: "#1a6aff"
                                visible: delegateRoot.isActive
                            }

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 0
                                spacing: 0
                                Item {
                                    width: 56
                                    height: parent.height

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 48
                                        height: 48
                                        color: delegateRoot.isActive ? "#1a6aff" : "#1e2a3a"
                                        Behavior on color {
                                            ColorAnimation {
                                                duration: 150
                                            }
                                        }

                                        Image {
                                            id: appIcon
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            source: delegateRoot.modelData.icon ? delegateRoot.modelData.icon : ""
                                            sourceSize: Qt.size(96, 96)
                                            fillMode: Image.PreserveAspectFit
                                            smooth: true
                                            asynchronous: true
                                            visible: status === Image.Ready
                                        }

                                        Text {
                                            anchors.centerIn: parent
                                            text: delegateRoot.modelData.name ? delegateRoot.modelData.name.charAt(0).toUpperCase() : "?"
                                            color: "#ffffff"
                                            font.pixelSize: 18
                                            font.bold: true
                                            visible: appIcon.status !== Image.Ready
                                        }
                                    }
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 4
                                    width: parent.width - 36 - 56 - 16

                                    Row {
                                        spacing: 6

                                        Text {
                                            text: delegateRoot.modelData.name || ""
                                            font.pixelSize: 15
                                            font.bold: true
                                            color: delegateRoot.isActive ? "#111111" : "#ffffff"
                                            Behavior on color {
                                                ColorAnimation {
                                                    duration: 150
                                                }
                                            }
                                        }
                                    }

                                    Text {
                                        text: delegateRoot.modelData.kind || ""
                                        font.pixelSize: 12
                                        color: delegateRoot.isActive ? "#333333" : "#7dd4fc"
                                        elide: Text.ElideRight
                                        width: parent.width
                                        Behavior on color {
                                            ColorAnimation {
                                                duration: 150
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 36
                                height: 1
                                color: "#ffffff"
                                opacity: 0.08
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: appList.currentIndex = delegateRoot.index
                                onClicked: {
                                    Apps.launch(delegateRoot.modelData.id);
                                    root.closeLauncher();
                                }
                            }
                        }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                z: -1
                onClicked: root.forcedOpen = false
            }
        }
    }
}
