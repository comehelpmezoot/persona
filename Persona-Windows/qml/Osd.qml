import QtQuick
import QtQuick.Layouts
import PersonaNative

// The pill-shaped on-screen display shared by the volume and brightness popups.
Scope {
    id: root

    property string iconName: "volume"
    property real level: 0.0
    property bool shown: false
    property int screenIndex: 0

    PanelWindow {
        id: win

        screenIndex: root.screenIndex
        visible: root.shown
        layer: 2
        anchorTop: true
        marginTop: win.scr.height / 9
        wantedWidth: 550
        wantedHeight: 50
        clickThrough: true
        focusable: false

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Colors.color2

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 15
                    rightMargin: 15
                }
                spacing: 10

                Icon {
                    name: root.iconName
                    color: "#ffffff"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 10
                    radius: 20
                    color: Colors.color3

                    Rectangle {
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                        }
                        implicitWidth: parent.width * Math.max(0, Math.min(1, root.level))
                        radius: parent.radius
                        color: "#ffffff"
                    }
                }

                Text {
                    text: Math.round(root.level * 100) + "%"
                    color: "#ffffff"
                    font.pixelSize: 20
                    font.family: Fonts.montserrat
                    font.weight: Font.Bold
                    Layout.minimumWidth: 50
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }
}
