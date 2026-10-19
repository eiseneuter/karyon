import QtQuick

Item {
    id: root
    width: 460
    height: 245

    readonly property color accentColor: (typeof karyonTheme !== "undefined") ? karyonTheme.accentColor : "#0d7cd6"
    readonly property color accentHoverColor: (typeof karyonTheme !== "undefined") ? karyonTheme.accentHoverColor : "#298fe6"
    readonly property color successColor: "#98c379"
    readonly property color errorColor: "#e06c75"

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 10
        color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBg : "#1e2227"
        border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBorder : "#353b45"
        border.width: 1

        // Header mit Icon, Titel und Drag-Möglichkeit
        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 38

            MouseArea {
                anchors.fill: parent
                onPressed: permController.startWindowDrag()
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    width: 20
                    height: 20
                    source: "qrc:/qt/qml/karyon/resources/karyon.svg"
                    smooth: true
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: "Karyon"
                    color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"
                    font.pixelSize: 13
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Trennlinie unter Header
        Rectangle {
            id: headerDivider
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#2c313a"
        }

        Column {
            anchors.top: headerDivider.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footerRow.top
            anchors.margins: 16
            anchors.topMargin: 10
            spacing: 8

            Text {
                text: "Input Permissions Required"
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"
                font.pixelSize: 12
                font.bold: true
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Karyon needs permission to access the Linux input subsystem to monitor mouse gestures and display the radial menu."
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                font.pixelSize: 11
                lineHeight: 1.3
            }

            Rectangle {
                width: parent.width
                height: 1
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#2c313a"
            }

            // Status List
            Column {
                width: parent.width
                spacing: 6

                Row {
                    spacing: 8
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: permController.uinputOk ? root.successColor : root.errorColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "/dev/uinput (Virtual Device Creation): "
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                        font.pixelSize: 10
                    }
                    Text {
                        text: permController.uinputOk ? "Granted" : "Missing"
                        color: permController.uinputOk ? root.successColor : root.errorColor
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                Row {
                    spacing: 8
                    Rectangle {
                        width: 8
                        height: 8
                        radius: 4
                        color: permController.eventOk ? root.successColor : root.errorColor
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "/dev/input/event* (Hardware Mouse Capture): "
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                        font.pixelSize: 10
                    }
                    Text {
                        text: permController.eventOk ? "Granted" : "Missing"
                        color: permController.eventOk ? root.successColor : root.errorColor
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }

            // Error display
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: permController.errorMessage
                color: root.errorColor
                font.pixelSize: 10
                visible: permController.errorMessage.length > 0
            }
        }

        // Footer Buttons
        Row {
            id: footerRow
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.margins: 16
            spacing: 10

            // Exit Button
            Rectangle {
                id: exitBtn
                width: 75
                height: 28
                radius: 4
                color: exitMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                border.color: exitMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.accentHoverColor : "#4b5263") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "Exit"
                    color: exitMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: exitMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: permController.exitApp()
                }
            }

            // Grant Permissions Button
            Rectangle {
                width: 150
                height: 28
                radius: 4
                color: permController.isBusy ? "#282c34" : (grantMa.containsMouse ? root.accentHoverColor : root.accentColor)
                opacity: permController.isBusy ? 0.6 : 1.0

                Text {
                    anchors.centerIn: parent
                    text: permController.isBusy ? "Authenticating..." : "Grant Permissions"
                    color: permController.isBusy ? "#abb2bf" : "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: grantMa
                    anchors.fill: parent
                    hoverEnabled: !permController.isBusy
                    cursorShape: permController.isBusy ? Qt.ArrowCursor : Qt.PointingHandCursor
                    onClicked: {
                        if (!permController.isBusy) {
                            permController.grantPermissions();
                        }
                    }
                }
            }
        }
    }
}
