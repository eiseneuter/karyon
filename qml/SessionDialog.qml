import QtQuick

Item {
    id: root
    width: 420
    height: 180

    property string iconSource: ""
    property string titleText: "Shut Down Computer"
    property string subtitleText: "Are you sure you want to shut down?"
    property string actionButtonText: "Shut Down"
    property color actionButtonColor: "#e06c75"

    signal confirmed()
    signal cancelled()

    focus: true
    Component.onCompleted: root.forceActiveFocus()
    Keys.onEscapePressed: root.cancelled()
    Keys.onReturnPressed: root.confirmed()
    Keys.onEnterPressed: root.confirmed()

    Rectangle {
        id: card
        anchors.fill: parent
        anchors.margins: 6
        radius: 14
        color: "#1e2227"
        border.color: "#353b45"
        border.width: 1

        // Header with Icon and Title
        Row {
            id: headerRow
            anchors.top: parent.top
            anchors.topMargin: 24
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.right: parent.right
            anchors.rightMargin: 24
            spacing: 16

            Image {
                id: iconImg
                width: 36
                height: 36
                source: root.iconSource
                smooth: true
                anchors.verticalCenter: parent.verticalCenter
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                width: headerRow.width - iconImg.width - headerRow.spacing

                Text {
                    text: root.titleText
                    color: "#ffffff"
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                    width: parent.width
                }

                Text {
                    text: root.subtitleText
                    color: "#9da5b4"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
        }

        // Action Buttons
        Row {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            anchors.right: parent.right
            anchors.rightMargin: 24
            spacing: 12

            // Cancel Button
            Rectangle {
                id: cancelBtn
                width: 90
                height: 34
                radius: 6
                color: cancelMa.containsMouse ? "#353b45" : "#282c34"
                border.color: cancelMa.containsMouse ? "#4b5263" : "#3e4451"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "Cancel"
                    color: cancelMa.containsMouse ? "#ffffff" : "#abb2bf"
                    font.pixelSize: 12
                    font.bold: true
                }

                MouseArea {
                    id: cancelMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.cancelled()
                }
            }

            // Confirm Action Button
            Rectangle {
                id: confirmBtn
                width: 105
                height: 34
                radius: 6
                color: confirmMa.containsMouse ? Qt.lighter(root.actionButtonColor, 1.15) : root.actionButtonColor

                Text {
                    anchors.centerIn: parent
                    text: root.actionButtonText
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.bold: true
                }

                MouseArea {
                    id: confirmMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.confirmed()
                }
            }
        }
    }
}
