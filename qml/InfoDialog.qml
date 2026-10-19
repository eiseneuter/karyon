import QtQuick

Item {
    id: root
    width: 520
    height: 560

    readonly property color accentColor: (typeof karyonTheme !== "undefined") ? karyonTheme.accentColor : "#0d7cd6"
    readonly property color accentHoverColor: (typeof karyonTheme !== "undefined") ? karyonTheme.accentHoverColor : "#298fe6"

    focus: true
    Component.onCompleted: root.forceActiveFocus()
    Keys.onEscapePressed: infoController.closeDialog()
    Keys.onReturnPressed: infoController.closeDialog()
    Keys.onEnterPressed: infoController.closeDialog()

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 12
        color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBg : "#1e2227"
        border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBorder : "#353b45"
        border.width: 1

        // Header mit Icon, Titel und Drag-Möglichkeit
        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 40

            MouseArea {
                anchors.fill: parent
                onPressed: infoController.startWindowDrag()
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    width: 22
                    height: 22
                    source: "qrc:/qt/qml/karyon/resources/karyon.svg"
                    smooth: true
                    anchors.verticalCenter: parent.verticalCenter
                }

                Row {
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: "Karyon"
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Text {
                        text: "Quick Guide & Info"
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#96a2b4"
                        font.pixelSize: 11
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // Close button (X) top right
            Rectangle {
                width: 26
                height: 26
                radius: 5
                color: closeXMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : "transparent"
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: closeXMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                    font.pixelSize: 13
                    font.bold: true
                }

                MouseArea {
                    id: closeXMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: infoController.closeDialog()
                }
            }
        }

        // Trennlinie
        Rectangle {
            id: divider
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#282c34"
        }

        // Scrollbarer Anleitungsbereich (knappe Stichpunkte, keine Fließtextsätze)
        Flickable {
            id: scrollArea
            anchors.top: divider.bottom
            anchors.bottom: footerDivider.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 14
            contentWidth: width
            contentHeight: guideCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            clip: true

            Column {
                id: guideCol
                width: parent.width
                spacing: 9

                // 1. BASIC CONTROLS
                Column {
                    width: parent.width
                    spacing: 3

                    Text {
                        text: "BASIC CONTROLS"
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Row {
                        spacing: 6
                        width: parent.width
                        Text {
                            text: "• Hold Trigger:"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"
                            font.pixelSize: 11
                            font.bold: true
                        }
                        Text {
                            width: parent.width - 95
                            wrapMode: Text.WordWrap
                            text: "Open overlay • Aim & release to trigger. RMB is default. Keep in mind: Karyon is designed for use of RMB! Some custom trigger buttons do not work well or at all."
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 11
                        }
                    }
                    Row {
                        spacing: 6
                        Text { text: "• Mouse Wheel in Hub:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Switch categories (Windows, Apps, Files)"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                    Row {
                        spacing: 6
                        Text { text: "• Left-Click:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Cancel & close overlay without action"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                }

                // 2. WINDOW CONTROL
                Column {
                    width: parent.width
                    spacing: 3

                    Text {
                        text: "WINDOW CONTROL (ON SEGMENT)"
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Row {
                        spacing: 6
                        Text { text: "• Middle-Click (MMB):"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Close targeted window"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                    Row {
                        spacing: 6
                        Text { text: "• Wheel Up:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Maximize / Restore targeted window"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                    Row {
                        spacing: 6
                        Text { text: "• Wheel Down:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Minimize / Restore targeted window"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                }

                // 3. PINNING
                Column {
                    width: parent.width
                    spacing: 3

                    Text {
                        text: "PINNING (APPS & FILES)"
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Row {
                        spacing: 6
                        Text { text: "• Pin / Unpin:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Aim at needle icon and release RMB"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                    Row {
                        spacing: 6
                        Text { text: "• Supported for pinning:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Primary window instances, Applications, Recent Files"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                }

                // 4. MEDIA & VOLUME
                Column {
                    width: parent.width
                    spacing: 3

                    Text {
                        text: "MEDIA & VOLUME (BELOW HUB)"
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Row {
                        spacing: 6
                        Text { text: "• Media Buttons:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Previous Track • Play / Pause • Next Track"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                    Row {
                        spacing: 6
                        Text { text: "• Volume (Hub bottom) / Mute (entire Hub):"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Mouse wheel changes volume • Middle-click toggles mute"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                }

                // 5. LIVE TRAY STATUS
                Column {
                    width: parent.width
                    spacing: 3

                    Text {
                        text: "LIVE TRAY STATUS & NOTIFICATIONS"
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Row {
                        spacing: 6
                        Text { text: "• Live Status:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Window segments mirror live tray icons (badges, notification indicators)"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                }

                // 6. MOUSE GESTURES
                Column {
                    width: parent.width
                    spacing: 3

                    Text {
                        text: "MOUSE GESTURES"
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Row {
                        spacing: 6
                        Text { text: "• Execute:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Hold RMB + fast swipe (8 directions) • Release RMB to trigger"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                    Row {
                        spacing: 6
                        Text { text: "• Cancel:"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"; font.pixelSize: 11; font.bold: true }
                        Text { text: "Left-click while swiping cancels gesture"; color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"; font.pixelSize: 11 }
                    }
                }
            }
        }

        // Trennlinie vor Footer
        Rectangle {
            id: footerDivider
            anchors.bottom: footer.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#282c34"
        }

        // Footer mit Versions- & Projekthinweis sowie OK-Button
        Item {
            id: footer
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 40

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    text: "Karyon 3.1 • © 2026 eisen"
                    color: (typeof karyonTheme !== "undefined") ? karyonTheme.textMuted : "#96a2b4"
                    font.pixelSize: 11
                    font.bold: true
                }

                Text {
                    text: "https://github.com/eiseneuter/karyon"
                    color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSubtle : "#6c788c"
                    font.pixelSize: 10
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 75
                height: 28
                radius: 4
                color: okMa.containsMouse ? root.accentHoverColor : root.accentColor

                Text {
                    anchors.centerIn: parent
                    text: "OK"
                    color: "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: okMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: infoController.closeDialog()
                }
            }
        }
    }
}
