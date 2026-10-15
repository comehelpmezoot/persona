import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import PersonaNative

// The tilted "now playing" widget in the bottom-left corner. Driven by whatever app Windows
// considers the current media session (Spotify, browser, VLC, ...).
Scope {
    id: capsuleScope

    Instantiator {
        model: Shell.config.capsule === false ? 0 : Shell.screens.length

        delegate: PanelWindow {
            id: pmusicWindow

            screenIndex: index
            layer: 0
            useWorkArea: true
            anchorBottom: true
            anchorLeft: true
            wantedWidth: 800
            wantedHeight: 300
            focusable: false
            visible: true

            Image {
                id: dialogueBox
                source: Shell.asset("components/player.png")
                transformOrigin: Item.Center
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: -170
                anchors.verticalCenterOffset: -15
                width: 500
                height: 500
                rotation: 10
                fillMode: Image.PreserveAspectFit
            }

            Item {
                anchors.centerIn: dialogueBox
                anchors.horizontalCenterOffset: 70
                anchors.verticalCenterOffset: 10
                rotation: 10
                width: 100
                height: 60

                Row {
                    anchors.centerIn: parent
                    spacing: -10
                    // Track info
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        width: 80
                        leftPadding: 20
                        anchors.verticalCenterOffset: -15

                        Item {
                            width: parent.width
                            height: artistMarquee.implicitHeight + 3
                            Marquee {
                                id: artistMarquee
                                anchors.bottom: parent.bottom
                                text: MediaCtl.available ? (MediaCtl.artist || "") : ""
                                maxWidth: 100
                                font: Fonts.text
                                size: 12
                                color: Colors.color3
                                scrollRate: 50
                                pauseDuration: 2000
                                visible: text !== ""
                            }
                        }

                        Item {
                            width: parent.width
                            height: songname.implicitHeight - 20
                            Marquee {
                                id: songname
                                text: MediaCtl.available ? (MediaCtl.title || "No title") : "No Media"
                                maxWidth: 100
                                font: Fonts.text
                                size: 16
                                color: "black"
                                scrollRate: 50
                                pauseDuration: 2000
                            }
                        }
                    }

                    // Disk + prev/next
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        leftPadding: 50
                        Item {
                            width: 106
                            height: 1
                            Item {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30
                                height: 30

                                Icon {
                                    id: prevIcon
                                    anchors.centerIn: parent
                                    name: "prev"
                                    width: 30
                                    height: 30
                                    color: prevMouse.containsMouse ? Colors.color3 : Colors.color15
                                    Behavior on color {
                                        ColorAnimation {
                                            duration: 150
                                        }
                                    }
                                }
                                MouseArea {
                                    id: prevMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (MediaCtl.canPrevious)
                                        MediaCtl.previous()
                                }
                            }

                            Item {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: -5
                                width: 60
                                height: 60

                                Image {
                                    id: imgDisk
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    fillMode: Image.PreserveAspectCrop
                                    source: MediaCtl.available ? MediaCtl.artUrl : ""
                                    smooth: true
                                    mipmap: true
                                    layer.enabled: true
                                    layer.smooth: true
                                    layer.effect: MultiEffect {
                                        antialiasing: true
                                        maskEnabled: true
                                        maskSpreadAtMin: 1.0
                                        maskThresholdMax: 1.0
                                        maskThresholdMin: 0.5
                                        maskSource: Image {
                                            layer.smooth: true
                                            mipmap: true
                                            smooth: true
                                            source: Shell.asset("components/AlbumCover-by-Squirrel-Modeller.svg")
                                        }
                                    }
                                    Behavior on rotation {
                                        NumberAnimation {
                                            duration: diskTimer.interval
                                            easing.type: Easing.Linear
                                        }
                                    }
                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: 300
                                            easing.type: Easing.OutCubic
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (MediaCtl.canToggle)
                                        MediaCtl.togglePlaying()
                                    onEntered: imgDisk.scale = 0.8
                                    onExited: imgDisk.scale = 1.0
                                }
                                Timer {
                                    id: diskTimer
                                    interval: 500
                                    repeat: true
                                    running: MediaCtl.available && MediaCtl.playing
                                    onTriggered: imgDisk.rotation += 3
                                }
                            }

                            Item {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30
                                height: 30

                                Icon {
                                    id: nextIcon
                                    anchors.centerIn: parent
                                    name: "next"
                                    width: 30
                                    height: 30
                                    color: nextMouse.containsMouse ? Colors.color3 : Colors.color15
                                    Behavior on color {
                                        ColorAnimation {
                                            duration: 150
                                        }
                                    }
                                }
                                MouseArea {
                                    id: nextMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (MediaCtl.canNext)
                                        MediaCtl.next()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
