import QtQuick
import QtQml
import QtQuick.Effects
import PersonaNative

Scope {
    id: clockScope

    readonly property real vw: 500 / 26.0417

    Instantiator {
        model: Shell.config.clock === false ? 0 : Shell.screens.length
        delegate: PanelWindow {
            id: clockWindow

            screenIndex: index
            layer: 0
            useWorkArea: true
            anchorTop: true
            anchorRight: true
            wantedWidth: 500
            wantedHeight: 200
            focusable: false
            clickThrough: true
            visible: true

            Item {
                id: clockRoot
                anchors.fill: parent

                Item {
                    id: waveContainerOuter
                    z: 1
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.rightMargin: -clockScope.vw * 1.5625
                    width: clockScope.vw * 26.0417
                    height: clockScope.vw * 7.2917
                    clip: true

                    Repeater {
                        model: [Shell.asset("img/wave1.png"), Shell.asset("img/wave2.png"), Shell.asset("img/wave3.png"), Shell.asset("img/wave4.png")]
                        delegate: Item {
                            id: wavePairDelegate
                            required property var modelData
                            required property int index
                            width: waveContainerOuter.width * 2
                            height: waveContainerOuter.height

                            Image {
                                source: wavePairDelegate.modelData
                                width: waveContainerOuter.width
                                height: waveContainerOuter.height
                                fillMode: Image.Stretch
                                x: 0
                            }
                            Image {
                                source: wavePairDelegate.modelData
                                width: waveContainerOuter.width
                                height: waveContainerOuter.height
                                fillMode: Image.Stretch
                                x: waveContainerOuter.width
                            }

                            NumberAnimation on x {
                                from: 0
                                to: -waveContainerOuter.width
                                duration: 10000
                                loops: Animation.Infinite
                                running: true
                            }
                        }
                    }
                }

                WaveTextLayer {
                    z: 5
                    waveSource: Shell.asset("img/wave8.png")
                }

                Text {
                    id: timeText
                    z: 50
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.rightMargin: clockScope.vw * 12
                    anchors.topMargin: clockScope.vw * 0.2
                    text: Time.time
                    font.family: Fonts.yahei
                    font.pixelSize: clockScope.vw * 6
                    font.weight: Font.Bold
                    font.letterSpacing: -clockScope.vw * 0.21
                    color: "#ffffff"
                    style: Text.Raised
                    styleColor: "#d11300b8"
                }

                Text {
                    id: weekdayText
                    z: 50
                    anchors.top: parent.top
                    anchors.topMargin: clockScope.vw * 3.5
                    anchors.right: parent.right
                    anchors.rightMargin: clockScope.vw * 9
                    width: clockScope.vw * 2
                    text: Time.weekday
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Fonts.condensed
                    font.pixelSize: clockScope.vw * 1.45
                    font.weight: Font.Normal
                    font.letterSpacing: -clockScope.vw * 0.1
                    color: "#ffffff"
                    style: Text.Outline
                    styleColor: "#0000ff"
                }

                Item {
                    id: phaseName
                    z: 50
                    anchors.top: parent.top
                    anchors.topMargin: clockScope.vw * 2
                    anchors.right: parent.right
                    anchors.rightMargin: clockScope.vw * 6
                    width: clockScope.vw * 5
                    height: clockScope.vw * 1.9

                    Row {
                        anchors.centerIn: parent
                        spacing: clockScope.vw * 0.35

                        BatteryIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: BatteryInfo.available
                            size: clockScope.vw * 1.5
                            color: "#fffb9f"
                            level: BatteryInfo.percentage
                            charging: BatteryInfo.isCharging
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: BatteryInfo.available ? BatteryInfo.percentageString : "AC"
                            font.family: Fonts.mono
                            font.pixelSize: clockScope.vw * 1.5
                            font.weight: Font.Bold
                            color: "#fffb9f"
                            style: Text.Raised
                            styleColor: "#635400d9"
                        }
                    }
                }

                Text {
                    id: daytimeText
                    z: 50
                    anchors.top: parent.top
                    anchors.topMargin: clockScope.vw * 5
                    anchors.right: parent.right
                    anchors.rightMargin: clockScope.vw * 6.5
                    width: clockScope.vw * 5.5
                    text: Time.daytime
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Fonts.yahei
                    font.pixelSize: clockScope.vw * 1.3
                    font.weight: Font.Bold
                    font.letterSpacing: -clockScope.vw * 0.1042
                    color: "#00dbff"
                    style: Text.Outline
                    styleColor: "#002c90"
                }

                Rectangle {
                    id: circle1
                    z: 60
                    anchors.top: parent.top
                    anchors.topMargin: clockScope.vw * 2.34375
                    anchors.right: parent.right
                    anchors.rightMargin: clockScope.vw * 2.0833
                    width: clockScope.vw * 4.1667
                    height: width
                    radius: width / 2
                    color: "#ffd200"
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: "#402b00"
                        shadowHorizontalOffset: clockScope.vw * 0.0521
                        shadowVerticalOffset: clockScope.vw * 0.0521
                        shadowBlur: clockScope.vw * 0.05
                    }
                }

                Rectangle {
                    id: circle2
                    z: 61
                    anchors.top: parent.top
                    anchors.topMargin: clockScope.vw * 2.4479
                    anchors.right: parent.right
                    anchors.rightMargin: clockScope.vw * 2.1875
                    width: clockScope.vw * 3.9583
                    height: width
                    radius: width / 2
                    color: "#ffe500"
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: "#fff72a"
                        shadowBlur: clockScope.vw * 0.3
                    }
                }

                Item {
                    id: moonSphere
                    z: 62
                    anchors.top: parent.top
                    anchors.topMargin: clockScope.vw * 2.4479
                    anchors.right: parent.right
                    anchors.rightMargin: clockScope.vw * 2.16
                    width: clockScope.vw * 3.958
                    height: width
                    clip: true

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: ShaderEffectSource {
                            sourceItem: Rectangle {
                                width: moonSphere.width
                                height: moonSphere.height
                                radius: moonSphere.width / 2
                                color: "white"
                            }
                        }
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }

                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        x: 0
                        color: Time.moonPhaseDegree < 180 ? "#ffe700" : "#302c24"
                    }
                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        x: parent.width / 2
                        color: Time.moonPhaseDegree < 180 ? "#302c24" : "#ffe700"
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: parent.height
                        radius: width / 2
                        transform: Scale {
                            origin.x: moonSphere.width / 2
                            origin.y: moonSphere.height / 2
                            xScale: Math.cos(Time.moonPhaseDegree * Math.PI / 180)
                            yScale: 1
                        }
                        color: Math.cos(Time.moonPhaseDegree * Math.PI / 180) >= 0 ? "#302c24" : "#ffe700"
                    }
                }
            }
        }
    }

    component WaveTextLayer: Item {
        id: waveTextLayer
        required property url waveSource

        anchors.top: parent.top
        anchors.topMargin: -clockScope.vw * 0.5
        anchors.right: parent.right
        anchors.rightMargin: -clockScope.vw * 1.5625
        width: clockScope.vw * 26.0417
        height: clockScope.vw * 6.25
        clip: true

        Item {
            id: scrollingBg
            width: waveTextLayer.width * 2
            height: waveTextLayer.height

            Image {
                source: waveTextLayer.waveSource
                width: waveTextLayer.width
                height: waveTextLayer.height
                fillMode: Image.Stretch
                x: 0
            }
            Image {
                source: waveTextLayer.waveSource
                width: waveTextLayer.width
                height: waveTextLayer.height
                fillMode: Image.Stretch
                x: waveTextLayer.width
            }

            NumberAnimation on x {
                from: 0
                to: -waveTextLayer.width
                duration: 10000
                loops: Animation.Infinite
                running: true
            }
        }

        Text {
            id: textMask
            anchors.top: parent.top
            anchors.right: parent.right
            text: Time.date
            font.family: Fonts.yahei
            font.pixelSize: clockScope.vw * 6.75
            font.weight: Font.Bold
            font.letterSpacing: -clockScope.vw * 0.75
            color: "white"
            visible: false
        }

        ShaderEffectSource {
            id: textMaskSource
            sourceItem: textMask
            hideSource: true
            visible: false
        }

        MultiEffect {
            source: scrollingBg
            anchors.fill: scrollingBg
            maskEnabled: true
            maskSource: textMaskSource
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
    }
}
