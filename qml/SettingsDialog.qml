import QtQuick

Item {
    id: root
    width: 580
    height: 660

    readonly property color accentColor: (typeof karyonTheme !== "undefined") ? karyonTheme.accentColor : "#0d7cd6"
    readonly property color accentHoverColor: (typeof karyonTheme !== "undefined") ? karyonTheme.accentHoverColor : "#298fe6"

    focus: true
    Component.onCompleted: root.forceActiveFocus()
    Keys.forwardTo: keyRecorderModal.visible ? [keyRecorderModal] : []
    Keys.onEscapePressed: {
        if (keyRecorderModal.visible) {
            keyRecorderModal.close();
        } else {
            settingsController.hideSettings();
        }
    }

    function activeCategoryCount() {
        var count = 0;
        if (settingsController.categoryWindowsEnabled) count++;
        if (settingsController.categoryAppsEnabled) count++;
        if (settingsController.categoryFilesEnabled) count++;
        return count;
    }

    function formatSampleDate(fmtIdx, sep) {
        var d = new Date(1999, 11, 24, 23, 59, 0);
        var locName = (settingsController && settingsController.detectedLocaleName) ? settingsController.detectedLocaleName : "";
        var loc = locName ? Qt.locale(locName) : Qt.locale();
        var s = (sep === "/" || sep === "-") ? sep : ".";
        var dd = ("0" + d.getDate()).slice(-2);
        var mm = ("0" + (d.getMonth() + 1)).slice(-2);
        var yy = ("0" + (d.getFullYear() % 100)).slice(-2);
        var ddd = d.toLocaleDateString(loc, "ddd").replace(/\.+$/, "") + ".";
        var dddd = d.toLocaleDateString(loc, "dddd");
        var mmm = d.toLocaleDateString(loc, "MMM").replace(/\.+$/, "") + ".";
        var mmmm = d.toLocaleDateString(loc, "MMMM");

        switch (fmtIdx) {
        case 0:  return dd + s + mm + s + yy;
        case 1:  return ddd + " " + dd + s + mm + s + yy;
        case 2:  return dddd + " " + dd + s + mm + s + yy;
        case 3:  return ddd + " " + dd + ". " + mmm + " " + yy;
        case 4:  return dddd + " " + dd + ". " + mmm + " " + yy;
        case 5:  return ddd + " " + dd + ". " + mmmm + " " + yy;
        case 6:  return dd + ". " + mmm + " " + yy;
        case 7:  return dd + ". " + mmmm + " " + yy;
        case 8:  return yy + s + mm + s + dd;
        case 9:  return ddd + " " + yy + s + mm + s + dd;
        case 10: return dddd + " " + yy + s + mm + s + dd;
        case 11: return ddd + " " + yy + " " + mmm + " " + dd;
        case 12: return dddd + " " + yy + " " + mmm + " " + dd;
        case 13: return ddd + " " + yy + " " + mmmm + " " + dd;
        case 14: return yy + " " + mmm + " " + dd;
        case 15: return yy + " " + mmmm + " " + dd;
        default: return dd + s + mm + s + yy;
        }
    }

    function formatSampleTime(fmtIdx) {
        var d = new Date(1999, 11, 24, 23, 59, 0);
        var h = d.getHours();
        var m = ("0" + d.getMinutes()).slice(-2);
        if (fmtIdx === 1) {
            var ampm = (h < 12) ? "am" : "pm";
            var h12 = h % 12;
            if (h12 === 0) h12 = 12;
            return ("0" + h12).slice(-2) + ":" + m + " " + ampm;
        }
        return ("0" + h).slice(-2) + ":" + m;
    }

    // WheelHandler über dem gesamten Dialog verschluckt jegliche Mausrad-Eingaben
    WheelHandler {
        onWheel: function(event) {
            event.accepted = true;
        }
    }

    // Wiederverwendbare Checkbox im Session-Dialog-Stil
    component KaryonCheckBox: Item {
        id: cbRoot
        property bool checked: false
        property string text: ""
        property string tooltip: ""
        signal toggled(bool newState)

        implicitWidth: cbRow.implicitWidth
        implicitHeight: 20
        opacity: enabled ? 1.0 : 0.45

        Row {
            id: cbRow
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                id: cbBox
                width: 15
                height: 15
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: cbRoot.checked ? root.accentColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                border.color: cbMa.containsMouse ? (cbRoot.checked ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#5c6370")) : (cbRoot.checked ? root.accentColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"))
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    color: "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                    visible: cbRoot.checked
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: cbRoot.text
                color: cbRoot.checked ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                font.pixelSize: 11
                font.bold: true
            }
        }

        MouseArea {
            id: cbMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: cbRoot.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onEntered: {
                if (cbRoot.tooltip.length > 0 && !card.isDraggingSlider) {
                    var pt = mapToItem(card, width / 2, 0);
                    sliderTooltip.show(cbRoot.tooltip, pt.x, pt.y);
                }
            }
            onExited: sliderTooltip.hide()
            onClicked: {
                if (cbRoot.enabled) {
                    cbRoot.toggled(!cbRoot.checked);
                }
            }
        }
    }

    // Wiederverwendbares Dropdown für Gesten-Aktionen
    component KaryonDropdown: Item {
        id: ddRoot
        property string label: ""
        property string actionId: ""
        property string directionKey: ""
        property string actionText: settingsController.actionDisplayForId(actionId, directionKey)
        signal selected(string newId)

        width: parent ? parent.width : 260
        height: 24

        Row {
            anchors.fill: parent
            spacing: 6

            Text {
                width: 70
                anchors.verticalCenter: parent.verticalCenter
                text: ddRoot.label
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#e8ecf4"
                font.pixelSize: 12
                font.bold: true
            }

            Rectangle {
                id: ddBox
                width: parent.width - 76
                height: 24
                radius: 4
                color: ddMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#2c313a") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                border.color: ddMa.containsMouse ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8

                    Text {
                        width: parent.width - 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: ddRoot.actionText
                        color: ddRoot.actionId === "none" ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textMuted : "#96a2b4") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff")
                        font.pixelSize: 11
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▾"
                        color: root.accentColor
                        font.pixelSize: 11
                    }
                }

                MouseArea {
                    id: ddMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var pt = card.mapFromItem(ddBox, 0, ddBox.height + 2);
                        dropdownOverlay.show(pt.x, pt.y, ddBox.width, ddRoot.actionId, ddRoot.directionKey, function(chosenId) {
                            if (chosenId === "custom_app") {
                                settingsController.selectCustomApp(ddRoot.directionKey);
                            } else if (chosenId === "custom_key") {
                                keyRecorderModal.openForDirection(ddRoot.directionKey, ddRoot.label);
                            } else {
                                ddRoot.selected(chosenId);
                            }
                        });
                    }
                }
            }
        }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        anchors.margins: 4
        radius: 12
        color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBg : "#1e2227"
        border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBorder : "#353b45"
        border.width: 1

        property bool isDraggingSlider: false

        // Header mit Icon, Titel und Drag-Möglichkeit
        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 42

            MouseArea {
                anchors.fill: parent
                onPressed: settingsController.startWindowDrag()
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Image {
                    width: 24
                    height: 24
                    source: "qrc:/qt/qml/karyon/resources/karyon.svg"
                    smooth: true
                    anchors.verticalCenter: parent.verticalCenter
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1

                    Row {
                        spacing: 8

                        Text {
                            text: "Karyon Settings"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"
                            font.pixelSize: 14
                            font.bold: true
                        }

                        Text {
                            text: "Version 3.2"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textMuted : "#96a2b4"
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        text: "Hold trigger button to open overlay • Aim & release to trigger action"
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSubtle : "#b8c0cc"
                        font.pixelSize: 11
                    }
                }
            }

            // Close button (X) top right
            Rectangle {
                id: closeXBtn
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
                    onClicked: settingsController.hideSettings()
                }
            }
        }

        // Trennlinie
        Rectangle {
            id: divider
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#282c34"
        }

        // Schieberegler in 2 Spalten
        Row {
            id: slidersRow
            anchors.top: divider.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 18

            // Linke Spalte Slider
            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 6

                // 0. Trigger Selector (Button "Select trigger" + Display text)
                Column {
                    id: triggerCol
                    width: parent.width
                    spacing: 2

                    readonly property string tipText: "Select mouse button or keyboard key/combination to trigger Karyon"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Trigger Button / Key"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(triggerCol.tipText, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Row {
                        width: parent.width
                        height: 16
                        spacing: 8

                        Rectangle {
                            id: selectTriggerBtn
                            width: 82
                            height: 18
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 4
                            color: selectTriggerMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                            border.color: selectTriggerMa.containsMouse ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "Select trigger"
                                color: selectTriggerMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                                font.pixelSize: 10
                                font.bold: true
                            }

                            MouseArea {
                                id: selectTriggerMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onEntered: {
                                    if (!card.isDraggingSlider) {
                                        var pt = mapToItem(card, width / 2, 0);
                                        sliderTooltip.show("Record a mouse button or key combination to trigger Karyon", pt.x, pt.y);
                                    }
                                }
                                onExited: sliderTooltip.hide()
                                onClicked: {
                                    triggerRecorderModal.open();
                                }
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - selectTriggerBtn.width - 8
                            text: settingsController.triggerDisplayName
                            color: root.accentColor
                            font.pixelSize: 11
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }
                }

                // 1. Trigger Hold
                Column {
                    id: sliderCol1
                    width: parent.width
                    spacing: 2

                    readonly property string sliderTip: "Hold duration to show overlay / time to perform a common right click"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Trigger Hold"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            id: holdValText
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsController.triggerHoldMs + " ms" + (settingsController.triggerHoldMs === 120 ? " (def.)" : "")
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol1.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Item {
                        id: holdSlider
                        width: parent.width
                        height: 16

                        property real minVal: 50
                        property real maxVal: 800
                        property real curVal: settingsController.triggerHoldMs

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1

                            Rectangle {
                                height: parent.height
                                radius: 2
                                color: root.accentColor
                                width: Math.max(0, Math.min(parent.width, (holdSlider.curVal - holdSlider.minVal) / (holdSlider.maxVal - holdSlider.minVal) * parent.width))
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: holdMa.containsMouse || holdMa.pressed ? "#ffffff" : "#f0f0f0"
                            border.color: root.accentColor
                            border.width: 2
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, (holdSlider.curVal - holdSlider.minVal) / (holdSlider.maxVal - holdSlider.minVal) * (parent.width - width)))
                        }

                        MouseArea {
                            id: holdMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateFromMouse(mouseX) {
                                var ratio = Math.max(0, Math.min(1, mouseX / holdSlider.width));
                                var rawVal = holdSlider.minVal + ratio * (holdSlider.maxVal - holdSlider.minVal);
                                settingsController.triggerHoldMs = Math.round(rawVal / 5) * 5;
                            }

                            onPressedChanged: {
                                if (pressed) {
                                    card.isDraggingSlider = true;
                                    sliderTooltip.hide();
                                } else {
                                    card.isDraggingSlider = false;
                                }
                            }

                            onPressed: function(mouse) {
                                sliderTooltip.hide();
                                updateFromMouse(mouse.x);
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) {
                                    sliderTooltip.hide();
                                    updateFromMouse(mouse.x);
                                }
                            }
                            onEntered: {
                                if (pressed || card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol1.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }
                }

                // 2. Overlay Mouse Speed
                Column {
                    id: sliderCol2
                    width: parent.width
                    spacing: 2

                    readonly property string sliderTip: "Mouse cursor speed while showing overlay"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Overlay Mouse Speed"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            id: speedValText
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                var spd = settingsController.overlayMouseSpeed;
                                var isDef = Math.abs(spd - (-0.5)) < 0.02;
                                return (spd >= 0 ? "+" : "") + spd.toFixed(2) + (isDef ? " (def.)" : "");
                            }
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol2.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Item {
                        id: speedSlider
                        width: parent.width
                        height: 16

                        property real minVal: -1.0
                        property real maxVal: 1.0
                        property real curVal: settingsController.overlayMouseSpeed

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1

                            Rectangle {
                                height: parent.height
                                radius: 2
                                color: root.accentColor
                                width: Math.max(0, Math.min(parent.width, (speedSlider.curVal - speedSlider.minVal) / (speedSlider.maxVal - speedSlider.minVal) * parent.width))
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: speedMa.containsMouse || speedMa.pressed ? "#ffffff" : "#f0f0f0"
                            border.color: root.accentColor
                            border.width: 2
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, (speedSlider.curVal - speedSlider.minVal) / (speedSlider.maxVal - speedSlider.minVal) * (parent.width - width)))
                        }

                        MouseArea {
                            id: speedMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateFromMouse(mouseX) {
                                var ratio = Math.max(0, Math.min(1, mouseX / speedSlider.width));
                                var rawVal = speedSlider.minVal + ratio * (speedSlider.maxVal - speedSlider.minVal);
                                settingsController.overlayMouseSpeed = Math.round(rawVal * 20) / 20;
                            }

                            onPressedChanged: {
                                if (pressed) {
                                    card.isDraggingSlider = true;
                                    sliderTooltip.hide();
                                } else {
                                    card.isDraggingSlider = false;
                                }
                            }

                            onPressed: function(mouse) {
                                sliderTooltip.hide();
                                updateFromMouse(mouse.x);
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) {
                                    sliderTooltip.hide();
                                    updateFromMouse(mouse.x);
                                }
                            }
                            onEntered: {
                                if (pressed || card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol2.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }
                }
            }

            // Rechte Spalte: Sliders
            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 6

                // 5. Max Recent Apps
                Column {
                    id: sliderCol5
                    width: parent.width
                    spacing: 2

                    readonly property string sliderTip: "Maximum number of recently closed applications to show"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Max Recent Apps"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            id: maxAppsValText
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsController.maxRecentApps + (settingsController.maxRecentApps === 12 ? " (def.)" : "")
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol5.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Item {
                        id: maxAppsSlider
                        width: parent.width
                        height: 16

                        property real minVal: 1
                        property real maxVal: 30
                        property real curVal: settingsController.maxRecentApps

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1

                            Rectangle {
                                height: parent.height
                                radius: 2
                                color: root.accentColor
                                width: Math.max(0, Math.min(parent.width, (maxAppsSlider.curVal - maxAppsSlider.minVal) / (maxAppsSlider.maxVal - maxAppsSlider.minVal) * parent.width))
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: maxAppsMa.containsMouse || maxAppsMa.pressed ? "#ffffff" : "#f0f0f0"
                            border.color: root.accentColor
                            border.width: 2
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, (maxAppsSlider.curVal - maxAppsSlider.minVal) / (maxAppsSlider.maxVal - maxAppsSlider.minVal) * (parent.width - width)))
                        }

                        MouseArea {
                            id: maxAppsMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateFromMouse(mouseX) {
                                var ratio = Math.max(0, Math.min(1, mouseX / maxAppsSlider.width));
                                var rawVal = maxAppsSlider.minVal + ratio * (maxAppsSlider.maxVal - maxAppsSlider.minVal);
                                settingsController.maxRecentApps = Math.round(rawVal);
                            }

                            onPressedChanged: {
                                if (pressed) {
                                    card.isDraggingSlider = true;
                                    sliderTooltip.hide();
                                } else {
                                    card.isDraggingSlider = false;
                                }
                            }

                            onPressed: function(mouse) {
                                sliderTooltip.hide();
                                updateFromMouse(mouse.x);
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) {
                                    sliderTooltip.hide();
                                    updateFromMouse(mouse.x);
                                }
                            }
                            onEntered: {
                                if (pressed || card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol5.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }
                }

                // 6. Max Recent Files
                Column {
                    id: sliderCol6
                    width: parent.width
                    spacing: 2

                    readonly property string sliderTip: "Maximum number of recently opened files to show"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Max Recent Files"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            id: maxFilesValText
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsController.maxRecentFiles + (settingsController.maxRecentFiles === 12 ? " (def.)" : "")
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol6.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Item {
                        id: maxFilesSlider
                        width: parent.width
                        height: 16

                        property real minVal: 1
                        property real maxVal: 30
                        property real curVal: settingsController.maxRecentFiles

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1

                            Rectangle {
                                height: parent.height
                                radius: 2
                                color: root.accentColor
                                width: Math.max(0, Math.min(parent.width, (maxFilesSlider.curVal - maxFilesSlider.minVal) / (maxFilesSlider.maxVal - maxFilesSlider.minVal) * parent.width))
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: maxFilesMa.containsMouse || maxFilesMa.pressed ? "#ffffff" : "#f0f0f0"
                            border.color: root.accentColor
                            border.width: 2
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, (maxFilesSlider.curVal - maxFilesSlider.minVal) / (maxFilesSlider.maxVal - maxFilesSlider.minVal) * (parent.width - width)))
                        }

                        MouseArea {
                            id: maxFilesMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateFromMouse(mouseX) {
                                var ratio = Math.max(0, Math.min(1, mouseX / maxFilesSlider.width));
                                var rawVal = maxFilesSlider.minVal + ratio * (maxFilesSlider.maxVal - maxFilesSlider.minVal);
                                settingsController.maxRecentFiles = Math.round(rawVal);
                            }

                            onPressedChanged: {
                                if (pressed) {
                                    card.isDraggingSlider = true;
                                    sliderTooltip.hide();
                                } else {
                                    card.isDraggingSlider = false;
                                }
                            }

                            onPressed: function(mouse) {
                                sliderTooltip.hide();
                                updateFromMouse(mouse.x);
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) {
                                    sliderTooltip.hide();
                                    updateFromMouse(mouse.x);
                                }
                            }
                            onEntered: {
                                if (pressed || card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol6.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }
                }

                // 7. Volume Steps
                Column {
                    id: sliderCol7
                    width: parent.width
                    spacing: 2

                    readonly property string sliderTip: "Volume steps in percent"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Volume Steps"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            id: volStepValText
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsController.volumeStep + "%" + (settingsController.volumeStep === 3 ? " (def.)" : "")
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol7.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Item {
                        id: volStepSlider
                        width: parent.width
                        height: 16

                        property real minVal: 1
                        property real maxVal: 20
                        property real curVal: settingsController.volumeStep

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1

                            Rectangle {
                                height: parent.height
                                radius: 2
                                color: root.accentColor
                                width: Math.max(0, Math.min(parent.width, (volStepSlider.curVal - volStepSlider.minVal) / (volStepSlider.maxVal - volStepSlider.minVal) * parent.width))
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: volStepMa.containsMouse || volStepMa.pressed ? "#ffffff" : "#f0f0f0"
                            border.color: root.accentColor
                            border.width: 2
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, (volStepSlider.curVal - volStepSlider.minVal) / (volStepSlider.maxVal - volStepSlider.minVal) * (parent.width - width)))
                        }

                        MouseArea {
                            id: volStepMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateFromMouse(mouseX) {
                                var ratio = Math.max(0, Math.min(1, mouseX / volStepSlider.width));
                                var rawVal = volStepSlider.minVal + ratio * (volStepSlider.maxVal - volStepSlider.minVal);
                                settingsController.volumeStep = Math.round(rawVal);
                            }

                            onPressedChanged: {
                                if (pressed) {
                                    card.isDraggingSlider = true;
                                    sliderTooltip.hide();
                                } else {
                                    card.isDraggingSlider = false;
                                }
                            }

                            onPressed: function(mouse) {
                                sliderTooltip.hide();
                                updateFromMouse(mouse.x);
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) {
                                    sliderTooltip.hide();
                                    updateFromMouse(mouse.x);
                                }
                            }
                            onEntered: {
                                if (pressed || card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderCol7.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }
                }
            }
        }

        // Trennlinie unter den Schiebereglern (Overlay Transparency)
        Rectangle {
            id: slidersDivider
            anchors.top: slidersRow.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#282c34"
        }

        // Checkboxen in 2 Spalten auf einheitlichen horizontalen Linien
        Row {
            id: checkboxesRow
            anchors.top: slidersDivider.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 18

            // Linke Spalte Checkboxen
            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 6

                // Hub Section Header
                Text {
                    text: "HUB DISPLAY"
                    color: root.accentColor
                    font.pixelSize: 11
                    font.bold: true
                }

                // Checkboxen: Clock, Date, Volume
                Item {
                    width: parent.width
                    height: 20

                    KaryonCheckBox {
                        x: 0
                        text: "Clock"
                        checked: settingsController.showClock
                        tooltip: "Show time in Karyon hub"
                        onToggled: function(val) {
                            settingsController.showClock = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 82
                        text: "Date"
                        checked: settingsController.showDate
                        tooltip: "Show date in Karyon hub"
                        onToggled: function(val) {
                            settingsController.showDate = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 164
                        text: "Volume"
                        checked: settingsController.showVolumeControl
                        tooltip: "Show volume control in Karyon hub"
                        onToggled: function(val) {
                            settingsController.showVolumeControl = val;
                        }
                    }
                }

                // Datumsformat: Button "Switch Date" + Trennzeichen-Button "." + Beispieldatum (24.12.26)
                // Datumsformat: Button "Switch Date" + Button Trennzeichen + Beispieldatum
                Row {
                    width: parent.width
                    height: 20
                    spacing: 5

                    Rectangle {
                        id: switchDateBtn
                        width: 74
                        height: 20
                        radius: 4
                        color: switchDateMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                        border.color: switchDateMa.containsMouse ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "Date format"
                            color: switchDateMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                            font.pixelSize: 10
                            font.bold: true
                        }

                        MouseArea {
                            id: switchDateMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: {
                                if (!card.isDraggingSlider) {
                                    var pt = mapToItem(card, width / 2, 0);
                                    sliderTooltip.show("Cycle date format in Karyon hub", pt.x, pt.y);
                                }
                            }
                            onExited: sliderTooltip.hide()
                            onClicked: {
                                settingsController.dateFormatIndex = (settingsController.dateFormatIndex + 1) % 16;
                            }
                        }
                    }

                    Rectangle {
                        id: dateSepBtn
                        width: 20
                        height: 20
                        radius: 4
                        color: dateSepMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                        border.color: dateSepMa.containsMouse ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: settingsController.dateSeparator
                            color: dateSepMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                            font.pixelSize: 11
                            font.bold: true
                        }

                        MouseArea {
                            id: dateSepMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: {
                                if (!card.isDraggingSlider) {
                                    var pt = mapToItem(card, width / 2, 0);
                                    sliderTooltip.show("Cycle date separator (. / -)", pt.x, pt.y);
                                }
                            }
                            onExited: sliderTooltip.hide()
                            onClicked: {
                                var cur = settingsController.dateSeparator;
                                if (cur === ".") {
                                    settingsController.dateSeparator = "/";
                                } else if (cur === "/") {
                                    settingsController.dateSeparator = "-";
                                } else {
                                    settingsController.dateSeparator = ".";
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width - switchDateBtn.width - dateSepBtn.width - 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.formatSampleDate(settingsController.dateFormatIndex, settingsController.dateSeparator)
                        color: root.accentColor
                        font.pixelSize: 11
                        minimumPixelSize: 8
                        fontSizeMode: Text.HorizontalFit
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }

                // Zeitformat: Button "Switch Time" + Beispielzeit (23:59)
                Row {
                    width: parent.width
                    height: 20
                    spacing: 5

                    Rectangle {
                        id: switchTimeBtn
                        width: 74
                        height: 20
                        radius: 4
                        color: switchTimeMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                        border.color: switchTimeMa.containsMouse ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "Time format"
                            color: switchTimeMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                            font.pixelSize: 10
                            font.bold: true
                        }

                        MouseArea {
                            id: switchTimeMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: {
                                if (!card.isDraggingSlider) {
                                    var pt = mapToItem(card, width / 2, 0);
                                    sliderTooltip.show("Cycle time format in Karyon hub", pt.x, pt.y);
                                }
                            }
                            onExited: sliderTooltip.hide()
                            onClicked: {
                                settingsController.timeFormatIndex = (settingsController.timeFormatIndex + 1) % 2;
                            }
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.formatSampleTime(settingsController.timeFormatIndex)
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }
                }

                // Game Mode & Performance Mode Checkboxen
                Item {
                    width: parent.width
                    height: 20

                    KaryonCheckBox {
                        x: 0
                        text: "Game Mode"
                        checked: settingsController.gameMode
                        tooltip: "Bypasses overlay and gestures in games"
                        onToggled: function(val) {
                            settingsController.gameMode = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 100
                        text: "Performance Mode"
                        checked: settingsController.performanceMode
                        tooltip: "Caps overlay to 30 FPS and antialiases only top edges for lower CPU usage"
                        onToggled: function(val) {
                            settingsController.performanceMode = val;
                        }
                    }
                }
            }

            // Rechte Spalte Checkboxen
            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 6

                // Kategorien Header
                Text {
                    text: "CATEGORIES"
                    color: root.accentColor
                    font.pixelSize: 11
                    font.bold: true
                }

                // Checkboxen: Windows, Apps, Files
                Item {
                    width: parent.width
                    height: 20

                    KaryonCheckBox {
                        x: 0
                        text: "Windows"
                        checked: settingsController.categoryWindowsEnabled
                        enabled: !checked || root.activeCategoryCount() > 1
                        tooltip: "Enable Windows category"
                        onToggled: function(val) {
                            if (!val && root.activeCategoryCount() <= 1) return;
                            settingsController.categoryWindowsEnabled = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 82
                        text: "Apps"
                        checked: settingsController.categoryAppsEnabled
                        enabled: !checked || root.activeCategoryCount() > 1
                        tooltip: "Enable Recent Apps category"
                        onToggled: function(val) {
                            if (!val && root.activeCategoryCount() <= 1) return;
                            settingsController.categoryAppsEnabled = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 164
                        text: "Files"
                        checked: settingsController.categoryFilesEnabled
                        enabled: !checked || root.activeCategoryCount() > 1
                        tooltip: "Enable Recent Files category"
                        onToggled: function(val) {
                            if (!val && root.activeCategoryCount() <= 1) return;
                            settingsController.categoryFilesEnabled = val;
                        }
                    }
                }

                // Karyon Icons Header
                Text {
                    text: "KARYON ICONS"
                    color: root.accentColor
                    font.pixelSize: 11
                    font.bold: true
                }

                // Zeile 1: Desktop, Tray, Menu
                Item {
                    width: parent.width
                    height: 20

                    KaryonCheckBox {
                        x: 0
                        text: "Desktop"
                        checked: settingsController.showDesktop
                        tooltip: "Show Desktop segment in Windows"
                        onToggled: function(val) {
                            settingsController.showDesktop = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 82
                        text: "Tray"
                        checked: settingsController.showTray
                        tooltip: "Show System Tray segment in Apps"
                        onToggled: function(val) {
                            settingsController.showTray = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 164
                        text: "Menu"
                        checked: settingsController.showMenu
                        tooltip: "Show App Menu (Start Menu) segment in Apps"
                        onToggled: function(val) {
                            settingsController.showMenu = val;
                        }
                    }
                }

                // Zeile 2: Favorites, Session, Media
                Item {
                    width: parent.width
                    height: 20

                    KaryonCheckBox {
                        x: 0
                        text: "Favorites"
                        checked: settingsController.showFavorites
                        tooltip: "Show Favorites segment in Apps (replaces Menu when Menu is off)"
                        onToggled: function(val) {
                            settingsController.showFavorites = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 82
                        text: "Session"
                        checked: settingsController.showSession
                        tooltip: "Show Session segment in Apps"
                        onToggled: function(val) {
                            settingsController.showSession = val;
                        }
                    }

                    KaryonCheckBox {
                        x: 164
                        text: "Media"
                        checked: settingsController.showMediaControls
                        tooltip: "Show media controls under Karyon hub"
                        onToggled: function(val) {
                            settingsController.showMediaControls = val;
                        }
                    }
                }
            }
        }

        // Trennlinie vor der neuen Sektion (Scale, Transparency, Color Schemes, Base Color)
        Rectangle {
            id: appearanceDivider
            anchors.top: checkboxesRow.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#282c34"
        }

        // Neue Sektion ohne Überschrift
        Column {
            id: appearanceSection
            anchors.top: appearanceDivider.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 8

            // 1. Schieberegler in 2 Spalten: Overlay Scale (links) & Overlay Transparency (rechts)
            Row {
                width: parent.width
                spacing: 18

                // Linke Spalte: Overlay Scale
                Column {
                    id: sliderColScale
                    width: (parent.width - parent.spacing) / 2
                    spacing: 2

                    readonly property string sliderTip: "Overall visual scale factor for the radial overlay"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Overlay Scale"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            id: scaleValText
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                var s = settingsController.overlayScale;
                                var isDef = Math.abs(s - 1.0) < 0.02;
                                return s.toFixed(1) + "x" + (isDef ? " (def.)" : "");
                            }
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderColScale.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Item {
                        id: scaleSlider
                        width: parent.width
                        height: 16

                        property real minVal: 0.8
                        property real maxVal: 1.5
                        property real curVal: settingsController.overlayScale

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1

                            Rectangle {
                                height: parent.height
                                radius: 2
                                color: root.accentColor
                                width: Math.max(0, Math.min(parent.width, (scaleSlider.curVal - scaleSlider.minVal) / (scaleSlider.maxVal - scaleSlider.minVal) * parent.width))
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: scaleMa.containsMouse || scaleMa.pressed ? "#ffffff" : "#f0f0f0"
                            border.color: root.accentColor
                            border.width: 2
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, (scaleSlider.curVal - scaleSlider.minVal) / (scaleSlider.maxVal - scaleSlider.minVal) * (parent.width - width)))
                        }

                        MouseArea {
                            id: scaleMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateFromMouse(mouseX) {
                                var ratio = Math.max(0, Math.min(1, mouseX / scaleSlider.width));
                                var rawVal = scaleSlider.minVal + ratio * (scaleSlider.maxVal - scaleSlider.minVal);
                                var stepped = Math.round(rawVal * 10) / 10;
                                settingsController.overlayScale = Math.max(scaleSlider.minVal, Math.min(scaleSlider.maxVal, stepped));
                            }

                            onPressedChanged: {
                                if (pressed) {
                                    card.isDraggingSlider = true;
                                    sliderTooltip.hide();
                                } else {
                                    card.isDraggingSlider = false;
                                }
                            }

                            onPressed: function(mouse) {
                                sliderTooltip.hide();
                                updateFromMouse(mouse.x);
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) {
                                    sliderTooltip.hide();
                                    updateFromMouse(mouse.x);
                                }
                            }
                            onEntered: {
                                if (pressed || card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderColScale.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }
                }

                // Rechte Spalte: Overlay Transparency
                Column {
                    id: sliderColTrans
                    width: (parent.width - parent.spacing) / 2
                    spacing: 2

                    readonly property string sliderTip: "Background opacity (0% = completely opaque)"

                    Item {
                        width: parent.width
                        height: 16

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Overlay Transparency"
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8ecf4"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            id: transValText
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                var tr = Math.round(settingsController.overlayTransparency);
                                return tr + "%" + (tr === 4 ? " (def.)" : "");
                            }
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                if (card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderColTrans.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }

                    Item {
                        id: transSlider
                        width: parent.width
                        height: 16

                        property real minVal: 0.0
                        property real maxVal: 20.0
                        property real curVal: settingsController.overlayTransparency

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1

                            Rectangle {
                                height: parent.height
                                radius: 2
                                color: root.accentColor
                                width: Math.max(0, Math.min(parent.width, (transSlider.curVal - transSlider.minVal) / (transSlider.maxVal - transSlider.minVal) * parent.width))
                            }
                        }

                        Rectangle {
                            width: 12
                            height: 12
                            radius: 6
                            color: transMa.containsMouse || transMa.pressed ? "#ffffff" : "#f0f0f0"
                            border.color: root.accentColor
                            border.width: 2
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, (transSlider.curVal - transSlider.minVal) / (transSlider.maxVal - transSlider.minVal) * (parent.width - width)))
                        }

                        MouseArea {
                            id: transMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateFromMouse(mouseX) {
                                var ratio = Math.max(0, Math.min(1, mouseX / transSlider.width));
                                var rawVal = transSlider.minVal + ratio * (transSlider.maxVal - transSlider.minVal);
                                var stepped = Math.round(rawVal);
                                settingsController.overlayTransparency = Math.max(transSlider.minVal, Math.min(transSlider.maxVal, stepped));
                            }

                            onPressedChanged: {
                                if (pressed) {
                                    card.isDraggingSlider = true;
                                    sliderTooltip.hide();
                                } else {
                                    card.isDraggingSlider = false;
                                }
                            }

                            onPressed: function(mouse) {
                                sliderTooltip.hide();
                                updateFromMouse(mouse.x);
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed) {
                                    sliderTooltip.hide();
                                    updateFromMouse(mouse.x);
                                }
                            }
                            onEntered: {
                                if (pressed || card.isDraggingSlider) return;
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show(sliderColTrans.sliderTip, pt.x, pt.y);
                            }
                            onExited: sliderTooltip.hide()
                        }
                    }
                }
            }

            // 2. Color Schemes: 6 Checkboxen nebeneinander
            Row {
                width: parent.width
                spacing: 10

                Repeater {
                    model: [
                        { id: 0, name: "Charcoal", tip: "Classic Karyon carbon charcoal & cobalt blue (Default)", dotColor: "#22262d", dotBorder: "#0d7cd6" },
                        { id: 1, name: "Slate", tip: "Nordic arctic slate & frost cyan", dotColor: "#222936", dotBorder: "#88c0d0" },
                        { id: 2, name: "Emerald", tip: "Deep boreal pine & radiant emerald", dotColor: "#142e22", dotBorder: "#10b981" },
                        { id: 3, name: "Amethyst", tip: "Deep royal amethyst & electric lavender", dotColor: "#271b36", dotBorder: "#a855f7" },
                        { id: 4, name: "Cappuccino", tip: "Warm roasted espresso & caramel mocha", dotColor: "#312117", dotBorder: "#e0a96d" },
                        { id: 5, name: "Crimson", tip: "Velvet wine & radiant ruby rose", dotColor: "#33141f", dotBorder: "#f43f5e" }
                    ]

                    Row {
                        spacing: 4
                        anchors.verticalCenter: parent.verticalCenter

                        KaryonCheckBox {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            checked: settingsController.colorSchemeIndex === modelData.id
                            tooltip: modelData.tip
                            onToggled: function(val) {
                                settingsController.selectColorScheme(modelData.id);
                            }
                        }

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: modelData.dotColor
                            border.color: modelData.dotBorder
                            border.width: 1
                        }
                    }
                }
            }

            // 3. Base Color: 2 Buttons (Dialog & Screen Eyedropper) + Vorschau-Swatch
            Row {
                width: parent.width
                height: 24
                spacing: 10

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Base Color:"
                    color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                    font.pixelSize: 11
                    font.bold: true
                }

                // Button 1: Farbwähler-Dialog
                Rectangle {
                    id: baseColorDlgBtn
                    width: 136
                    height: 24
                    radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: baseColorDlgMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                    border.color: baseColorDlgMa.containsMouse ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Text {
                            text: "🎨"
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: "Choose Color..."
                            color: baseColorDlgMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                            font.pixelSize: 11
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: baseColorDlgMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: {
                            if (!card.isDraggingSlider) {
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show("Open intelligent color picker dialog to customize base color", pt.x, pt.y);
                            }
                        }
                        onExited: sliderTooltip.hide()
                        onClicked: {
                            sliderTooltip.hide();
                            settingsController.openColorDialog();
                        }
                    }
                }

                // Button 2: Screen Color Picker
                Rectangle {
                    id: screenPickerBtn
                    width: 136
                    height: 24
                    radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: screenPickerMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                    border.color: screenPickerMa.containsMouse ? root.accentHoverColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Text {
                            text: "💉"
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: "Pick from Screen"
                            color: screenPickerMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                            font.pixelSize: 11
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: screenPickerMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: {
                            if (!card.isDraggingSlider) {
                                var pt = mapToItem(card, width / 2, 0);
                                sliderTooltip.show("Pick any pixel color directly from your screen", pt.x, pt.y);
                            }
                        }
                        onExited: sliderTooltip.hide()
                        onClicked: {
                            sliderTooltip.hide();
                            settingsController.pickScreenColor();
                        }
                    }
                }
            }
        }

        // Trennlinie vor Gesten-Sektion
        Rectangle {
            id: gesturesDivider
            anchors.top: appearanceSection.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#282c34"
        }

        // Gesten-Sektion (2 Spalten, exakt wie im Layout)
        Column {
            id: gesturesSection
            anchors.top: gesturesDivider.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            spacing: 6

            Row {
                width: parent.width
                spacing: 12

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "MOUSE GESTURES (TRIGGER + FAST SWIPE)"
                    color: root.accentColor
                    font.pixelSize: 11
                    font.bold: true
                }

                Item { width: 1; height: 1 }

                KaryonCheckBox {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Enable gestures"
                    checked: settingsController.gesturesEnabled
                    onToggled: function(c) { settingsController.gesturesEnabled = c; }
                }

                KaryonCheckBox {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Show trail"
                    checked: settingsController.showGestureTrail
                    enabled: settingsController.gesturesEnabled
                    opacity: settingsController.gesturesEnabled ? 1.0 : 0.45
                    onToggled: function(c) { settingsController.showGestureTrail = c; }
                }
            }

            Row {
                width: parent.width
                spacing: 18
                opacity: settingsController.gesturesEnabled ? 1.0 : 0.45
                enabled: settingsController.gesturesEnabled

                // Linke Spalte Gesten: Left, Up, Left-Up, Left-Down
                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 5

                    KaryonDropdown {
                        label: "Left"
                        directionKey: "left"
                        actionId: settingsController.gestureLeft
                        onSelected: function(id) { settingsController.gestureLeft = id; }
                    }

                    KaryonDropdown {
                        label: "Up"
                        directionKey: "up"
                        actionId: settingsController.gestureUp
                        onSelected: function(id) { settingsController.gestureUp = id; }
                    }

                    KaryonDropdown {
                        label: "Left-Up"
                        directionKey: "Left-Up"
                        actionId: settingsController.gestureLeftUp
                        onSelected: function(id) { settingsController.gestureLeftUp = id; }
                    }

                    KaryonDropdown {
                        label: "Left-Down"
                        directionKey: "Left-Down"
                        actionId: settingsController.gestureLeftDown
                        onSelected: function(id) { settingsController.gestureLeftDown = id; }
                    }
                }

                // Rechte Spalte Gesten: Right, Down, Right-Up, Right-Down
                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 5

                    KaryonDropdown {
                        label: "Right"
                        directionKey: "right"
                        actionId: settingsController.gestureRight
                        onSelected: function(id) { settingsController.gestureRight = id; }
                    }

                    KaryonDropdown {
                        label: "Down"
                        directionKey: "down"
                        actionId: settingsController.gestureDown
                        onSelected: function(id) { settingsController.gestureDown = id; }
                    }

                    KaryonDropdown {
                        label: "Right-Up"
                        directionKey: "Right-Up"
                        actionId: settingsController.gestureRightUp
                        onSelected: function(id) { settingsController.gestureRightUp = id; }
                    }

                    KaryonDropdown {
                        label: "Right-Down"
                        directionKey: "Right-Down"
                        actionId: settingsController.gestureRightDown
                        onSelected: function(id) { settingsController.gestureRightDown = id; }
                    }
                }
            }
        }

        // Trennlinie vor Footer
        Rectangle {
            id: footerDivider
            anchors.bottom: footerRow.top
            anchors.bottomMargin: 14
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            height: 1
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.dividerColor : "#282c34"
        }

        // Footer mit bündig angeordneten Buttons unter den Gesten-Spalten
        Row {
            id: footerRow
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            height: 30
            spacing: 18
            z: 10

            // Linke Button-Gruppe: bündig unter "Left-Down"
            Row {
                id: footerLeftRow
                width: (footerRow.width - footerRow.spacing) / 2
                height: parent.height
                spacing: 8

                // Permissions Button
                Rectangle {
                    id: permsBtn
                    width: Math.floor((footerLeftRow.width - footerLeftRow.spacing) * 0.55)
                    height: 30
                    radius: 5
                    color: permsMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                    border.color: settingsController.hasInputPermissions ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451") : "#e06c75"
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 6

                        Rectangle {
                            width: 7
                            height: 7
                            radius: 3.5
                            anchors.verticalCenter: parent.verticalCenter
                            color: settingsController.hasInputPermissions ? "#98c379" : "#e06c75"
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: settingsController.hasInputPermissions ? "Revoke Permissions" : "Set Permissions"
                            color: permsMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }

                    MouseArea {
                        id: permsMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: {
                            var tip = settingsController.hasInputPermissions ?
                                "Revoke input subsystem permissions" :
                                "Grant input subsystem permissions via polkit";
                            var pt = mapToItem(card, width / 2, 0);
                            sliderTooltip.show(tip, pt.x, pt.y);
                        }
                        onExited: sliderTooltip.hide()
                        onClicked: {
                            sliderTooltip.hide();
                            if (settingsController.hasInputPermissions) {
                                settingsController.revokeInputPermissions();
                            } else {
                                settingsController.requestInputPermissions();
                            }
                        }
                    }
                }

                // Reset Defaults Button
                Rectangle {
                    id: resetBtn
                    width: footerLeftRow.width - footerLeftRow.spacing - permsBtn.width
                    height: 30
                    radius: 5
                    color: resetMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                    border.color: resetMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#4b5263") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Reset Defaults"
                        color: resetMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                        font.pixelSize: 12
                        font.bold: true
                    }

                    MouseArea {
                        id: resetMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsController.resetDefaults()
                    }
                }
            }

            // Rechte Button-Gruppe: bündig unter "Right-Down"
            Row {
                id: footerRightRow
                width: (footerRow.width - footerRow.spacing) / 2
                height: parent.height
                spacing: 8

                // Quit Button (Rot)
                Rectangle {
                    id: quitBtn
                    width: Math.floor((footerRightRow.width - footerRightRow.spacing) * 0.40)
                    height: 30
                    radius: 5
                    color: quitMa.containsMouse ? Qt.lighter("#e06c75", 1.15) : "#e06c75"

                    Text {
                        anchors.centerIn: parent
                        text: "Quit"
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    MouseArea {
                        id: quitMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsController.quitApp()
                    }
                }

                // Save Button (Akzentfarbe)
                Rectangle {
                    id: saveBtn
                    width: footerRightRow.width - footerRightRow.spacing - quitBtn.width
                    height: 30
                    radius: 5
                    color: saveMa.containsMouse ? root.accentHoverColor : root.accentColor

                    Text {
                        anchors.centerIn: parent
                        text: "Save"
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    MouseArea {
                        id: saveMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsController.hideSettings()
                    }
                }
            }
        }

        // Tooltip Card für Slider & Optionen
        Rectangle {
            id: sliderTooltip
            visible: false
            z: 300
            color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBg : "#181a1f"
            border.color: root.accentColor
            border.width: 1
            radius: 4
            width: Math.min(300, tipText.implicitWidth + 16)
            height: tipText.implicitHeight + 12

            property string pendingText: ""
            property real pendingX: 0
            property real pendingY: 0

            Text {
                id: tipText
                anchors.centerIn: parent
                width: parent.width - 16
                wrapMode: Text.WordWrap
                text: ""
                color: (typeof karyonTheme !== "undefined" && karyonTheme.isLight) ? "#000000" : "#e8ecf4"
                font.pixelSize: 11
                lineHeight: 1.25
            }

            Timer {
                id: tipShowTimer
                interval: 180
                onTriggered: {
                    tipText.text = sliderTooltip.pendingText;
                    var tx = Math.max(12, Math.min(card.width - sliderTooltip.width - 12, sliderTooltip.pendingX - sliderTooltip.width / 2));
                    var ty = sliderTooltip.pendingY - sliderTooltip.height - 6;
                    if (ty < 42) {
                        ty = sliderTooltip.pendingY + 22;
                    }
                    sliderTooltip.x = tx;
                    sliderTooltip.y = ty;
                    sliderTooltip.visible = true;
                }
            }

            function show(msg, itemX, itemY) {
                if (card.isDraggingSlider) return;
                if (!msg || msg.length === 0) return;
                tipShowTimer.stop();
                pendingText = msg;
                pendingX = itemX;
                pendingY = itemY;
                tipShowTimer.start();
            }

            function hide() {
                tipShowTimer.stop();
                visible = false;
            }
        }

        // Dropdown Popup Overlay (auf oberster Ebene im Card)
        Item {
            id: dropdownOverlay
            anchors.fill: parent
            z: 100
            visible: false

            property real popupX: 0
            property real popupY: 0
            property real popupWidth: 160
            property string currentActionId: ""
            property string targetDirection: ""
            property var onSelectCallback: null

            property var actionIds: settingsController.gestureActionIds()
            property var actionNames: settingsController.gestureActionNames()

            function show(x, y, w, curId, dirKey, callback) {
                actionIds = settingsController.gestureActionIds();
                actionNames = settingsController.gestureActionNames();
                popupX = Math.min(x, card.width - w - 8);
                var expectedH = Math.min(220, actionIds.length * 24 + 8);
                if (y + expectedH > card.height - 10) {
                    popupY = y - 26 - expectedH;
                } else {
                    popupY = y;
                }
                popupWidth = Math.max(w, 160);
                currentActionId = curId;
                targetDirection = dirKey || "";
                onSelectCallback = callback;
                visible = true;

                // Zum aktuell ausgewählten Element scrollen
                var idx = actionIds.indexOf(curId);
                if (idx >= 0) {
                    dropdownList.positionViewAtIndex(idx, ListView.Beginning);
                } else {
                    dropdownList.contentY = 0;
                }
            }

            function close() {
                visible = false;
                onSelectCallback = null;
            }

            // Vollflächiger Klickfänger zum Schließen bei Klick außerhalb
            MouseArea {
                anchors.fill: parent
                onClicked: dropdownOverlay.close()
            }

            Rectangle {
                x: dropdownOverlay.popupX
                y: dropdownOverlay.popupY
                width: dropdownOverlay.popupWidth
                height: Math.min(220, dropdownList.contentHeight + 8)
                radius: 5
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBg : "#1e2227"
                border.color: root.accentColor
                border.width: 1
                clip: true

                WheelHandler {
                    onWheel: function(event) {
                        if (event.angleDelta.y > 0) {
                            dropdownList.contentY = Math.max(0, dropdownList.contentY - 44);
                        } else {
                            dropdownList.contentY = Math.min(Math.max(0, dropdownList.contentHeight - dropdownList.height), dropdownList.contentY + 44);
                        }
                        event.accepted = true;
                    }
                }

                ListView {
                    id: dropdownList
                    anchors.fill: parent
                    anchors.margins: 4
                    anchors.rightMargin: 6
                    model: dropdownOverlay.actionIds
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        width: dropdownList.width
                        height: 24
                        radius: 3
                        color: itemMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#2c313a") : (modelData === dropdownOverlay.currentActionId ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34") : "transparent")

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            elide: Text.ElideRight
                            text: settingsController.actionDisplayForId(modelData, dropdownOverlay.targetDirection)
                            color: modelData === dropdownOverlay.currentActionId ? root.accentColor : (itemMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"))
                            font.pixelSize: 11
                            font.bold: modelData === dropdownOverlay.currentActionId
                        }

                        MouseArea {
                            id: itemMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var cb = dropdownOverlay.onSelectCallback;
                                var chosen = modelData;
                                dropdownOverlay.close();
                                if (cb) {
                                    cb(chosen);
                                }
                            }
                        }
                    }
                }

                // Subtiler Scrollbalken
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    width: 3
                    radius: 1.5
                    color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                    visible: dropdownList.contentHeight > dropdownList.height

                    Rectangle {
                        width: parent.width
                        height: Math.max(14, parent.height * (dropdownList.height / Math.max(1, dropdownList.contentHeight)))
                        y: Math.min(parent.height - height, Math.max(0, (dropdownList.contentY / Math.max(1, dropdownList.contentHeight - dropdownList.height)) * (parent.height - height)))
                        radius: 1.5
                        color: root.accentColor
                    }
                }
            }
        }

        // Tasten-Aufnahme-Modal für Custom Key
        Rectangle {
            id: keyRecorderModal
            anchors.fill: parent
            color: "#d0000000"
            z: 200
            visible: false
            focus: visible

            property string targetDirection: ""
            property string targetLabel: ""
            property string recordedKeyName: ""
            property string recordedKeySeq: ""
            property bool keyValid: false

            Keys.enabled: visible
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                var res = settingsController.processKeyEvent(event.key, event.modifiers, event.text);
                if (res.name && res.name.length > 0) {
                    keyRecorderModal.recordedKeyName = res.name;
                    keyRecorderModal.recordedKeySeq = res.sequence || "";
                    keyRecorderModal.keyValid = res.valid;
                }
                event.accepted = true;
            }

            function openForDirection(dirKey, label) {
                targetDirection = dirKey;
                targetLabel = label;
                recordedKeyName = "";
                recordedKeySeq = "";
                keyValid = false;
                settingsController.isRecordingKey = true;
                visible = true;
                keyRecorderModal.forceActiveFocus();
            }

            function close() {
                settingsController.isRecordingKey = false;
                visible = false;
                root.forceActiveFocus();
            }

            MouseArea {
                anchors.fill: parent
                onClicked: keyRecorderModal.close()
            }

            Rectangle {
                width: 380
                height: 190
                radius: 10
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBg : "#1e2227"
                border.color: root.accentColor
                border.width: 1
                anchors.centerIn: parent

                MouseArea {
                    anchors.fill: parent
                    onClicked: keyRecorderModal.forceActiveFocus()
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "SET KEY FOR GESTURE: " + keyRecorderModal.targetLabel.toUpperCase()
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Text {
                        text: "Press the desired key or shortcut\n(e.g. Ctrl+C, F11, Alt+Tab, Super+D...)"
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                        font.pixelSize: 11
                        lineHeight: 1.3
                    }

                    Rectangle {
                        width: parent.width
                        height: 38
                        radius: 6
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                        border.color: keyRecorderModal.keyValid ? root.accentColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                        border.width: 1.5

                        Text {
                            anchors.centerIn: parent
                            text: keyRecorderModal.recordedKeyName.length > 0 ? keyRecorderModal.recordedKeyName : "Press a key..."
                            color: keyRecorderModal.keyValid ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textMuted : "#96a2b4")
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        spacing: 10

                        Rectangle {
                            width: 80
                            height: 26
                            radius: 4
                            color: cancelKeyMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: "Cancel"
                                color: cancelKeyMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                                font.pixelSize: 10
                                font.bold: true
                            }
                            MouseArea {
                                id: cancelKeyMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: keyRecorderModal.close()
                            }
                        }

                        Rectangle {
                            width: 80
                            height: 26
                            radius: 4
                            color: keyRecorderModal.keyValid ? (saveKeyMa.containsMouse ? root.accentHoverColor : root.accentColor) : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#353b45")
                            opacity: keyRecorderModal.keyValid ? 1.0 : 0.4
                            Text {
                                anchors.centerIn: parent
                                text: "Save"
                                color: "#ffffff"
                                font.pixelSize: 10
                                font.bold: true
                            }
                            MouseArea {
                                id: saveKeyMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: keyRecorderModal.keyValid ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (keyRecorderModal.keyValid) {
                                        settingsController.setCustomKey(keyRecorderModal.targetDirection, keyRecorderModal.recordedKeyName, keyRecorderModal.recordedKeySeq);
                                        keyRecorderModal.close();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Modal-Dialog für Trigger-Aufnahme (Mausbutton oder Tastenkombination)
        Item {
            id: triggerRecorderModal
            anchors.fill: parent
            visible: false
            z: 9999
            focus: visible

            property string capturedType: ""
            property int capturedButton: 0
            property string capturedKeySeq: ""
            property string capturedDisplayName: ""
            property bool triggerValid: false

            Timer {
                id: startRecTimer
                interval: 80
                repeat: false
                onTriggered: {
                    if (triggerRecorderModal.visible) {
                        settingsController.isRecordingTrigger = true;
                    }
                }
            }

            Keys.enabled: visible
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Escape) {
                    triggerRecorderModal.close();
                    event.accepted = true;
                    return;
                }
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && triggerRecorderModal.triggerValid) {
                    settingsController.setTrigger(
                        triggerRecorderModal.capturedType,
                        triggerRecorderModal.capturedButton,
                        triggerRecorderModal.capturedKeySeq,
                        triggerRecorderModal.capturedDisplayName
                    );
                    triggerRecorderModal.close();
                    event.accepted = true;
                    return;
                }
                var res = settingsController.processKeyEvent(event.key, event.modifiers, event.text);
                if (res.name && res.name.length > 0) {
                    triggerRecorderModal.capturedType = "keyboard";
                    triggerRecorderModal.capturedButton = 0;
                    triggerRecorderModal.capturedKeySeq = res.sequence || "";
                    triggerRecorderModal.capturedDisplayName = res.name;
                    triggerRecorderModal.triggerValid = res.valid;
                }
                event.accepted = true;
            }

            Connections {
                target: settingsController
                function onTriggerCaptured(type, button, keySeq, displayName) {
                    if (triggerRecorderModal.visible) {
                        if (type === "mouse" && button === 272) return; // Left Click ignorieren
                        triggerRecorderModal.capturedType = type;
                        triggerRecorderModal.capturedButton = button;
                        triggerRecorderModal.capturedKeySeq = keySeq;
                        triggerRecorderModal.capturedDisplayName = displayName;
                        triggerRecorderModal.triggerValid = true;
                    }
                }
            }

            function open() {
                capturedType = "";
                capturedButton = 0;
                capturedKeySeq = "";
                capturedDisplayName = "";
                triggerValid = false;
                visible = true;
                triggerRecorderModal.forceActiveFocus();
                startRecTimer.start();
            }

            function close() {
                startRecTimer.stop();
                settingsController.isRecordingTrigger = false;
                visible = false;
                root.forceActiveFocus();
            }

            MouseArea {
                anchors.fill: parent
                onClicked: triggerRecorderModal.close()
            }

            Rectangle {
                width: 380
                height: 190
                radius: 10
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.cardBg : "#1e2227"
                border.color: root.accentColor
                border.width: 1
                anchors.centerIn: parent

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    onPressed: function(mouse) {
                        triggerRecorderModal.forceActiveFocus();
                        if (mouse.button !== Qt.LeftButton) {
                            var res = settingsController.processMouseEvent(mouse.button);
                            if (res.valid) {
                                triggerRecorderModal.capturedType = "mouse";
                                triggerRecorderModal.capturedButton = res.code;
                                triggerRecorderModal.capturedKeySeq = "";
                                triggerRecorderModal.capturedDisplayName = res.name;
                                triggerRecorderModal.triggerValid = true;
                            }
                        }
                    }
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    Text {
                        text: "SELECT TRIGGER BUTTON OR SHORTCUT"
                        color: root.accentColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Text {
                        text: "Click Right / Middle / Side button or press any shortcut\n(Left Click is reserved for Save / Cancel)"
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de"
                        font.pixelSize: 11
                        lineHeight: 1.3
                    }

                    Rectangle {
                        width: parent.width
                        height: 38
                        radius: 6
                        color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34"
                        border.color: triggerRecorderModal.triggerValid ? root.accentColor : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451")
                        border.width: 1.5

                        Text {
                            anchors.centerIn: parent
                            text: triggerRecorderModal.capturedDisplayName.length > 0 ? triggerRecorderModal.capturedDisplayName : "Click mouse button or press key..."
                            color: triggerRecorderModal.triggerValid ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textMuted : "#96a2b4")
                            font.pixelSize: 13
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.AllButtons
                            cursorShape: Qt.PointingHandCursor
                            onPressed: function(mouse) {
                                triggerRecorderModal.forceActiveFocus();
                                if (mouse.button !== Qt.LeftButton) {
                                    var res = settingsController.processMouseEvent(mouse.button);
                                    if (res.valid) {
                                        triggerRecorderModal.capturedType = "mouse";
                                        triggerRecorderModal.capturedButton = res.code;
                                        triggerRecorderModal.capturedKeySeq = "";
                                        triggerRecorderModal.capturedDisplayName = res.name;
                                        triggerRecorderModal.triggerValid = true;
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        spacing: 10

                        Rectangle {
                            width: 80
                            height: 26
                            radius: 4
                            color: cancelTriggerMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.controlHoverBg : "#353b45") : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#282c34")
                            border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.controlBorder : "#3e4451"
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: "Cancel"
                                color: cancelTriggerMa.containsMouse ? ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff") : ((typeof karyonTheme !== "undefined") ? karyonTheme.textSecondary : "#cdd3de")
                                font.pixelSize: 10
                                font.bold: true
                            }
                            MouseArea {
                                id: cancelTriggerMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: triggerRecorderModal.close()
                            }
                        }

                        Rectangle {
                            width: 80
                            height: 26
                            radius: 4
                            color: triggerRecorderModal.triggerValid ? (saveTriggerMa.containsMouse ? root.accentHoverColor : root.accentColor) : ((typeof karyonTheme !== "undefined") ? karyonTheme.controlBg : "#353b45")
                            opacity: triggerRecorderModal.triggerValid ? 1.0 : 0.4
                            Text {
                                anchors.centerIn: parent
                                text: "Save"
                                color: "#ffffff"
                                font.pixelSize: 10
                                font.bold: true
                            }
                            MouseArea {
                                id: saveTriggerMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: triggerRecorderModal.triggerValid ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: {
                                    if (triggerRecorderModal.triggerValid) {
                                        settingsController.setTrigger(
                                            triggerRecorderModal.capturedType,
                                            triggerRecorderModal.capturedButton,
                                            triggerRecorderModal.capturedKeySeq,
                                            triggerRecorderModal.capturedDisplayName
                                        );
                                        triggerRecorderModal.close();
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

