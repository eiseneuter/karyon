import QtQuick

Item {
    id: root
    property real overlayTransparencyVal: (typeof overlayTransparency !== "undefined" ? overlayTransparency : 4.0)
    opacity: 1.0 - (overlayTransparencyVal / 100.0)
    property int currentVolumeStep: (typeof volumeStep !== "undefined") ? volumeStep : 3
    property bool showClock: (typeof showClock !== "undefined") ? showClock : true
    property bool showDate: (typeof showDate !== "undefined") ? showDate : true
    property bool showVolumeControl: (typeof showVolumeControl !== "undefined") ? showVolumeControl : true
    property int aimMarginVal: (typeof aimMargin !== "undefined") ? aimMargin : 95
    implicitWidth: (circleRadius + ringGap + ringDepth + ring2Gap + ring2Depth + ring3Gap + ring3Depth + aimMarginVal) * 2
    implicitHeight: implicitWidth

    property var segmentList1: (typeof segments !== "undefined" && segments !== null) ? segments : []
    property var segmentList2: (typeof segments2 !== "undefined" && segments2 !== null) ? segments2 : []
    property var segmentList3: (typeof segments3 !== "undefined" && segments3 !== null) ? segments3 : []
    property bool hasRing2Active: (typeof hasRing2 !== "undefined") ? hasRing2 : false
    property bool hasRing3Active: (typeof hasRing3 !== "undefined") ? hasRing3 : false
    property string categoryMode: (typeof categoryMode !== "undefined") ? categoryMode : "windows"

    property string systemLocaleName: (typeof detectedLocaleName !== "undefined" && detectedLocaleName !== "") ? detectedLocaleName : ""
    property int dateFormatIdx: (typeof dateFormatIndex !== "undefined") ? dateFormatIndex : 0
    property string dateSep: (typeof dateSeparator !== "undefined" && dateSeparator !== "") ? dateSeparator : "."
    property int timeFormatIdx: (typeof timeFormatIndex !== "undefined") ? timeFormatIndex : 0
    property string currentTimeStr: ""
    property string currentDateStr: ""

    onDateFormatIdxChanged: updateClockAndDate()
    onDateSepChanged: updateClockAndDate()
    onTimeFormatIdxChanged: updateClockAndDate()

    function formatHubDate(d, loc, fmtIdx, sep) {
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

    function formatHubTime(d, fmtIdx) {
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

    function updateClockAndDate() {
        var now = new Date();
        var loc = root.systemLocaleName ? Qt.locale(root.systemLocaleName) : Qt.locale();
        root.currentTimeStr = formatHubTime(now, root.timeFormatIdx);
        root.currentDateStr = formatHubDate(now, loc, root.dateFormatIdx, root.dateSep);
    }

    Timer {
        id: clockTimer
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: root.updateClockAndDate()
    }

    property int systemVolume: (typeof volumeLevel !== "undefined") ? volumeLevel : 50
    property bool systemMuted: (typeof isMuted !== "undefined") ? isMuted : false

    property int selectedRing: -1    // 1 für Ring 1, 2 für Ring 2, 3 für Ring 3, -1 für keine Auswahl
    property int selectedIndex: -1   // Index im jeweiligen Ring, -1 für keine Auswahl
    property bool isPinHovered: false
    property bool isCounterBadgeHovered: false
    property real aimOffsetX: 0
    property real aimOffsetY: -20
    property bool fallbackCursorActive: (typeof overlayController !== "undefined" && overlayController) ? overlayController.fallbackCursorActive : false

    readonly property bool isCursorInHub: {
        var dx = aimOffsetX;
        var dy = aimOffsetY;
        var dist = Math.sqrt(dx * dx + dy * dy);
        return dist < (typeof circleRadius !== "undefined" ? circleRadius : 75);
    }

    readonly property bool isCursorInVolumeArea: {
        if (!showVolumeControl) return false;
        var dx = aimOffsetX;
        var dy = aimOffsetY;
        var dist = Math.sqrt(dx * dx + dy * dy);
        var r = (typeof circleRadius !== "undefined" ? circleRadius : 75);
        return (dist <= r && dy >= ((r / 3.0) + 7.0));
    }

    readonly property bool isDrilldownActive: {
        var seg = null;
        var outerR = 0;
        if (selectedRing === 1 && selectedIndex >= 0 && segmentList1 && selectedIndex < segmentList1.length) {
            seg = segmentList1[selectedIndex];
            outerR = circleRadius + ringGap + ringDepth;
        } else if (selectedRing === 2 && selectedIndex >= 0 && segmentList2 && selectedIndex < segmentList2.length) {
            seg = segmentList2[selectedIndex];
            outerR = circleRadius + ringGap + ringDepth + ring2Gap + ring2Depth;
        }
        if (!seg || !seg.hasDrilldown) return false;

        var dx = aimOffsetX;
        var dy = aimOffsetY;
        var dist = Math.sqrt(dx * dx + dy * dy);

        if (dist < outerR - 15 || dist > outerR + 35) return false;

        var angle = Math.atan2(dy, dx);
        var diff = angle - seg.angle;
        while (diff > Math.PI) diff -= 2 * Math.PI;
        while (diff < -Math.PI) diff += 2 * Math.PI;
        return Math.abs(diff) <= (seg.span / 2.0);
    }

    function requestPaintCanvas() {
        ringCanvas.requestPaint();
    }

    readonly property var currentSelectedSegment: {
        if (selectedRing === 1 && segmentList1 && selectedIndex >= 0 && selectedIndex < segmentList1.length) {
            return segmentList1[selectedIndex];
        }
        if (selectedRing === 2 && segmentList2 && selectedIndex >= 0 && selectedIndex < segmentList2.length) {
            return segmentList2[selectedIndex];
        }
        if (selectedRing === 3 && segmentList3 && selectedIndex >= 0 && selectedIndex < segmentList3.length) {
            return segmentList3[selectedIndex];
        }
        return null;
    }

    readonly property string currentTooltipText: (currentSelectedSegment && currentSelectedSegment.tooltipText) ? currentSelectedSegment.tooltipText : ""

    property bool volumeScrolledThisSession: false

    readonly property string selectedWindowId: {
        var id = "";
        if (selectedRing === 1 && selectedIndex >= 0 && segmentList1 && selectedIndex < segmentList1.length) {
            id = segmentList1[selectedIndex].windowId || "";
        } else if (selectedRing === 2 && selectedIndex >= 0 && segmentList2 && selectedIndex < segmentList2.length) {
            id = segmentList2[selectedIndex].windowId || "";
        } else if (selectedRing === 3 && selectedIndex >= 0 && segmentList3 && selectedIndex < segmentList3.length) {
            id = segmentList3[selectedIndex].windowId || "";
        }
        if (volumeScrolledThisSession && id.indexOf("__MEDIA_") === 0) {
            return "";
        }
        return id;
    }

    // Nur true, wenn der Cursor physisch auf dem gezeichneten Segment/Icon im Ring liegt
    readonly property bool isCursorOnSegment: {
        if (selectedRing < 1 || selectedIndex < 0) return false;
        var dx = aimOffsetX;
        var dy = aimOffsetY;
        var dist = Math.sqrt(dx * dx + dy * dy);
        var angle = Math.atan2(dy, dx);

        var cRadius = (typeof circleRadius !== "undefined" ? circleRadius : 75);
        var rGap = (typeof ringGap !== "undefined" ? ringGap : 10);
        var rDepth = (typeof ringDepth !== "undefined" ? ringDepth : 46);
        var r2Gap = (typeof ring2Gap !== "undefined" ? ring2Gap : 10);
        var r2Depth = (typeof ring2Depth !== "undefined" ? ring2Depth : 46);
        var r3Gap = (typeof ring3Gap !== "undefined" ? ring3Gap : 10);
        var r3Depth = (typeof ring3Depth !== "undefined" ? ring3Depth : 46);

        var innerR1 = cRadius + rGap;
        var outerR1 = innerR1 + rDepth;
        var innerR2 = outerR1 + r2Gap;
        var outerR2 = innerR2 + r2Depth;
        var innerR3 = outerR2 + r3Gap;
        var outerR3 = innerR3 + r3Depth;

        var innerR = 0;
        var outerR = 0;
        var seg = null;

        if (selectedRing === 1 && segmentList1 && selectedIndex < segmentList1.length) {
            innerR = innerR1;
            outerR = outerR1;
            seg = segmentList1[selectedIndex];
        } else if (selectedRing === 2 && segmentList2 && selectedIndex < segmentList2.length) {
            innerR = innerR2;
            outerR = outerR2;
            seg = segmentList2[selectedIndex];
        } else if (selectedRing === 3 && segmentList3 && selectedIndex < segmentList3.length) {
            innerR = innerR3;
            outerR = outerR3;
            seg = segmentList3[selectedIndex];
        }

        if (!seg) return false;

        // Exakte Winkel-Prüfung: Muss im gezeichneten Winkelbereich des Segments liegen
        var diff = angle - seg.angle;
        while (diff > Math.PI) diff -= 2 * Math.PI;
        while (diff < -Math.PI) diff += 2 * Math.PI;
        if (Math.abs(diff) > (seg.span / 2.0)) return false;

        // Im Hub (dist < innerR): NIE auf dem Segment
        if (dist < innerR) return false;

        // Liegt der Cursor physisch im Ring des Segments?
        if (dist <= outerR) return true;

        // Außerhalb des Rings (dist > outerR): NICHT auf dem Segment/Icon
        return false;
    }

    function setSegmentLists(s1, s2, s3, r2Active, r3Active) {
        segmentList1 = s1;
        segmentList2 = s2;
        segmentList3 = s3;
        hasRing2Active = r2Active;
        hasRing3Active = r3Active;
        if (selectedRing <= 0 || selectedIndex < 0) {
            selectedRing = 1;
            selectedIndex = (s1 && s1.length > 0) ? Math.floor((s1.length - 1) / 2) : 0;
        } else if (selectedRing === 1 && s1 && selectedIndex >= s1.length) {
            selectedIndex = Math.floor((s1.length - 1) / 2);
        } else if (selectedRing === 2 && (!r2Active || !s2 || selectedIndex >= s2.length)) {
            selectedRing = 1;
            selectedIndex = (s1 && s1.length > 0) ? Math.floor((s1.length - 1) / 2) : 0;
        }
        updateSelection();
        ringCanvas.requestPaint();
    }

    function onVolumeScrolled() {
        volumeScrolledThisSession = true;
        if (selectedRing === 1 && segmentList1 && selectedIndex >= 0 && selectedIndex < segmentList1.length) {
            var curSeg = segmentList1[selectedIndex];
            if (curSeg && (curSeg.segmentType === "media" || (curSeg.windowId && curSeg.windowId.indexOf("__MEDIA_") === 0))) {
                selectedRing = -1;
                selectedIndex = -1;
                ringCanvas.requestPaint();
            }
        }
        updateSelection();
    }

    function setAimOffset(ox, oy) {
        aimOffsetX = ox;
        aimOffsetY = oy;
        updateSelection();
        if (typeof overlayController !== "undefined" && overlayController) {
            overlayController.handleAimChanged(ox, oy, isDrilldownActive, selectedWindowId, selectedRing, selectedIndex, isPinHovered, isCursorInVolumeArea, isCounterBadgeHovered);
        }
    }

    function updateSelection() {
        var dx = aimOffsetX;
        var dy = aimOffsetY;
        var dist = Math.sqrt(dx * dx + dy * dy);
        var angle = Math.atan2(dy, dx);

        var innerR1 = circleRadius + ringGap;
        var outerR1 = innerR1 + ringDepth;
        var innerR2 = outerR1 + ring2Gap;
        var outerR2 = innerR2 + ring2Depth;
        var innerR3 = outerR2 + ring3Gap;
        var outerR3 = innerR3 + ring3Depth;

        var foundRing = -1;
        var foundIndex = -1;

        // 1. Physische Segmentprüfung innerhalb der gezeichneten Ringe (Pixelgenau)
        // Prüfe Ring 3 (falls aktiv) — exakte Pixelgrenzen identisch zur Canvas-Zeichnung
        var numSegs3 = (hasRing3Active && segmentList3 && segmentList3.length) ? segmentList3.length : 0;
        if (numSegs3 > 0 && dist >= innerR3 && dist <= outerR3) {
            for (var k = 0; k < numSegs3; k++) {
                var seg3 = segmentList3[k];
                var diff3 = angle - seg3.angle;
                while (diff3 > Math.PI) diff3 -= 2 * Math.PI;
                while (diff3 < -Math.PI) diff3 += 2 * Math.PI;
                if (Math.abs(diff3) <= (seg3.span / 2.0)) {
                    foundRing = 3;
                    foundIndex = k;
                    break;
                }
            }
        }

        // Prüfe Ring 2 (falls aktiv und noch nicht auf Ring 3)
        var numSegs2 = (hasRing2Active && segmentList2 && segmentList2.length) ? segmentList2.length : 0;
        if (foundRing === -1 && numSegs2 > 0 && dist >= innerR2 && dist <= outerR2) {
            for (var j = 0; j < numSegs2; j++) {
                var seg2 = segmentList2[j];
                var diff2 = angle - seg2.angle;
                while (diff2 > Math.PI) diff2 -= 2 * Math.PI;
                while (diff2 < -Math.PI) diff2 += 2 * Math.PI;
                if (Math.abs(diff2) <= (seg2.span / 2.0)) {
                    foundRing = 2;
                    foundIndex = j;
                    break;
                }
            }
        }

        // Prüfe Ring 1 (falls noch nicht auf Ring 2 oder 3)
        // Hub ist keine freie Fläche: Ring 1 ist ab 20px Totzone bis outerR1 anwählbar
        var numSegs1 = (segmentList1 && segmentList1.length) ? segmentList1.length : 0;
        var deadR = (typeof deadzoneRadius !== "undefined") ? deadzoneRadius : 20;
        if (foundRing === -1 && numSegs1 > 0 && dist >= deadR && dist <= outerR1) {
            for (var i = 0; i < numSegs1; i++) {
                var seg1 = segmentList1[i];
                if (volumeScrolledThisSession && (seg1.segmentType === "media" || (seg1.windowId && seg1.windowId.indexOf("__MEDIA_") === 0))) {
                    continue;
                }
                var diff1 = angle - seg1.angle;
                while (diff1 > Math.PI) diff1 -= 2 * Math.PI;
                while (diff1 < -Math.PI) diff1 += 2 * Math.PI;
                if (Math.abs(diff1) <= (seg1.span / 2.0)) {
                    foundRing = 1;
                    foundIndex = i;
                    break;
                }
            }
        }

        // 2. Cursor befindet sich AUSSERHALB des Overlays (dist > outerR):
        // In diesem Fall wird in Cursor-Richtung immer das äußerste existierende Segment gehighlightet!
        if (foundRing === -1 && dist > outerR1) {
            // Prüfe zuerst Ring 3: Ist Ring 3 in dieser Richtung vorhanden und dist > outerR3?
            if (numSegs3 > 0 && dist > outerR3) {
                for (var o3 = 0; o3 < numSegs3; o3++) {
                    var s3 = segmentList3[o3];
                    var d3 = angle - s3.angle;
                    while (d3 > Math.PI) d3 -= 2 * Math.PI;
                    while (d3 < -Math.PI) d3 += 2 * Math.PI;
                    if (Math.abs(d3) <= (s3.span / 2.0)) {
                        foundRing = 3;
                        foundIndex = o3;
                        break;
                    }
                }
            }

            // Falls kein Ring-3-Segment an diesem Winkel existiert: Prüfe Ring 2
            if (foundRing === -1 && numSegs2 > 0 && dist > outerR2) {
                var hasRing3AtAngle = false;
                if (numSegs3 > 0) {
                    for (var c3 = 0; c3 < numSegs3; c3++) {
                        var diffC3 = angle - segmentList3[c3].angle;
                        while (diffC3 > Math.PI) diffC3 -= 2 * Math.PI;
                        while (diffC3 < -Math.PI) diffC3 += 2 * Math.PI;
                        if (Math.abs(diffC3) <= (segmentList3[c3].span / 2.0)) {
                            hasRing3AtAngle = true;
                            break;
                        }
                    }
                }
                if (!hasRing3AtAngle) {
                    for (var o2 = 0; o2 < numSegs2; o2++) {
                        var s2 = segmentList2[o2];
                        var d2 = angle - s2.angle;
                        while (d2 > Math.PI) d2 -= 2 * Math.PI;
                        while (d2 < -Math.PI) d2 += 2 * Math.PI;
                        if (Math.abs(d2) <= (s2.span / 2.0)) {
                            foundRing = 2;
                            foundIndex = o2;
                            break;
                        }
                    }
                }
            }

            // Falls weder auf Ring 3 noch auf Ring 2 ein Segment an diesem Winkel liegt: Ring 1
            if (foundRing === -1 && numSegs1 > 0 && dist > outerR1) {
                var hasOuterAtAngle = false;
                if (numSegs3 > 0) {
                    for (var r3 = 0; r3 < numSegs3; r3++) {
                        var dR3 = angle - segmentList3[r3].angle;
                        while (dR3 > Math.PI) dR3 -= 2 * Math.PI;
                        while (dR3 < -Math.PI) dR3 += 2 * Math.PI;
                        if (Math.abs(dR3) <= (segmentList3[r3].span / 2.0)) {
                            hasOuterAtAngle = true;
                            break;
                        }
                    }
                }
                if (!hasOuterAtAngle && numSegs2 > 0) {
                    for (var r2 = 0; r2 < numSegs2; r2++) {
                        var dR2 = angle - segmentList2[r2].angle;
                        while (dR2 > Math.PI) dR2 -= 2 * Math.PI;
                        while (dR2 < -Math.PI) dR2 += 2 * Math.PI;
                        if (Math.abs(dR2) <= (segmentList2[r2].span / 2.0)) {
                            hasOuterAtAngle = true;
                            break;
                        }
                    }
                }
                if (!hasOuterAtAngle) {
                    for (var o1 = 0; o1 < numSegs1; o1++) {
                        var s1 = segmentList1[o1];
                        if (volumeScrolledThisSession && (s1.segmentType === "media" || (s1.windowId && s1.windowId.indexOf("__MEDIA_") === 0))) {
                            continue;
                        }
                        var d1 = angle - s1.angle;
                        while (d1 > Math.PI) d1 -= 2 * Math.PI;
                        while (d1 < -Math.PI) d1 += 2 * Math.PI;
                        if (Math.abs(d1) <= (s1.span / 2.0)) {
                            foundRing = 1;
                            foundIndex = o1;
                            break;
                        }
                    }
                }
            }
        }

        // SEHR WICHTIG: Befindet sich unter dem Mauszeiger KEIN Segment,
        // bleibt der Fokus (Highlight) unverändert!
        if (foundRing !== -1) {
            if (selectedRing !== foundRing || selectedIndex !== foundIndex) {
                selectedRing = foundRing;
                selectedIndex = foundIndex;
                ringCanvas.requestPaint();
            }
        } else if (isCursorInVolumeArea) {
            if (selectedRing !== -1 || selectedIndex !== -1) {
                selectedRing = -1;
                selectedIndex = -1;
                ringCanvas.requestPaint();
            }
        }

        // Pin-Hover prüfen auf dem aktuell fokussierten Segment
        var hoveredPin = false;
        if (selectedRing >= 1 && selectedIndex >= 0) {
            var activeSeg = null;
            var actInnerR = 0;
            if (selectedRing === 1 && segmentList1 && selectedIndex < segmentList1.length) {
                activeSeg = segmentList1[selectedIndex];
                actInnerR = innerR1;
            } else if (selectedRing === 2 && segmentList2 && selectedIndex < segmentList2.length) {
                activeSeg = segmentList2[selectedIndex];
                actInnerR = innerR2;
            } else if (selectedRing === 3 && segmentList3 && selectedIndex < segmentList3.length) {
                activeSeg = segmentList3[selectedIndex];
                actInnerR = innerR3;
            }

            if (activeSeg && activeSeg.canPin) {
                var a1 = activeSeg.angle - activeSeg.span / 2;
                var pinR = actInnerR + 9.5;
                var pinAngle = a1 + (7.5 / actInnerR);
                var pinX = pinR * Math.cos(pinAngle);
                var pinY = pinR * Math.sin(pinAngle);

                // Relativer Vektor vom Pin-Ursprung (px, py)
                var dpx = dx - pinX;
                var dpy = dy - pinY;

                // In das lokale Koordinatensystem der Pinnadel rotieren (Ausrichtung pinAngle + Math.PI)
                var theta = pinAngle + Math.PI;
                var cosT = Math.cos(theta);
                var sinT = Math.sin(theta);
                var lx = dpx * cosT + dpy * sinT;
                var ly = -dpx * sinT + dpy * cosT;

                // Innendurchmesser des Pinnadelkopfes ohne Rand:
                // Kreisradius 3.0, Linienstärke 1.3 -> Innenradius = 2.35 px (Innendurchmesser = 4.7 px)
                var halfInnerWidth = 3.0 - (1.3 / 2.0); // 2.35 px

                // Gesamtlänge des Pinnadelicons: Oberkante Kopf (-7.0 px) bis Nadelspitze (5.5 px)
                // (inkl. Strichkappen-Toleranz: -7.5 bis 6.0 px)
                var minLx = -7.5;
                var maxLx = 6.0;

                if (lx >= minLx && lx <= maxLx && Math.abs(ly) <= halfInnerWidth) {
                    hoveredPin = true;
                }
            }
        }

        if (isPinHovered !== hoveredPin) {
            isPinHovered = hoveredPin;
            ringCanvas.requestPaint();
        }

        // Counter-Badge Hover prüfen (nur auf Ring 1 bei echten Fenstern mit windowCount > 1, unterhalb des Ladebalkens)
        var hoveredCounter = false;
        if (selectedRing === 1 && segmentList1 && selectedIndex >= 0 && selectedIndex < segmentList1.length) {
            var curSeg = segmentList1[selectedIndex];
            if (curSeg && curSeg.isRealWindow && curSeg.windowCount > 1) {
                var sFact = (typeof overlayScale !== "undefined") ? overlayScale : 1.0;
                var a2 = curSeg.angle + curSeg.span / 2;
                var br = 7.0 * sFact;
                var badgeR = outerR1 - 24.5 * sFact;
                var badgeAngle = a2 - (11.5 * sFact / badgeR);
                var bx = badgeR * Math.cos(badgeAngle);
                var by = badgeR * Math.sin(badgeAngle);

                var dbx = dx - bx;
                var dby = dy - by;
                if (Math.sqrt(dbx * dbx + dby * dby) <= (br + 3.5 * sFact)) {
                    hoveredCounter = true;
                }
            }
        }

        if (isCounterBadgeHovered !== hoveredCounter) {
            isCounterBadgeHovered = hoveredCounter;
            ringCanvas.requestPaint();
        }
    }

    Connections {
        target: (typeof karyonTheme !== "undefined") ? karyonTheme : null
        function onThemeChanged() {
            hubCategoryCanvas.requestPaint();
            hubVolumeCanvas.requestPaint();
            ringCanvas.requestPaint();
        }
    }

    // Hub (Zentralkreis, Durchmesser 150px) mit normalem dunklem Segmenthintergrund
    Rectangle {
        id: hubCircle
        width: circleRadius * 2
        height: width
        radius: circleRadius
        color: (typeof karyonTheme !== "undefined") ? karyonTheme.baseBg : "#21252b"
        border.color: (typeof karyonTheme !== "undefined") ? karyonTheme.ring1Border : "#16181d"
        border.width: 1
        anchors.centerIn: parent

        // 1. Symbol oben im Hub pro Kategorie:
        // - windows: zwei sich überlagernde Fenster, schlicht, stilisiert, grün
        // - apps: 9-Quadrate Symbol des Menüs, in blau
        // - files: zwei sich überlagernde A4 Seiten (eine mit abgeknickter Ecke), schlicht, stilisiert, orange
        Canvas {
            id: hubCategoryCanvas
            property real hubScale: (typeof overlayScale !== "undefined") ? overlayScale : (circleRadius / 75.0)
            width: Math.round(32 * hubScale)
            height: Math.round(24 * hubScale)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Math.round(20 * hubScale)

            property string mode: root.categoryMode
            onModeChanged: requestPaint()

            function drawRoundedRect(ctx, x, y, w, h, r) {
                ctx.beginPath();
                ctx.moveTo(x + r, y);
                ctx.lineTo(x + w - r, y);
                ctx.arcTo(x + w, y, x + w, y + r, r);
                ctx.lineTo(x + w, y + h - r);
                ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
                ctx.lineTo(x + r, y + h);
                ctx.arcTo(x, y + h, x, y + h - r, r);
                ctx.lineTo(x, y + r);
                ctx.arcTo(x, y + r, x + r, y, r);
                ctx.closePath();
            }

            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.save();
                ctx.scale(hubScale, hubScale);

                var winColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identWindows : "#2ed573";
                var appsColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identApps : "#37d0ff";
                var filesColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identFiles : "#ff9f43";
                var maskBg = (typeof karyonTheme !== "undefined") ? karyonTheme.baseBg : "#21252b";

                if (mode === "windows") {
                    ctx.lineWidth = 1.4;
                    ctx.lineCap = "round";
                    ctx.lineJoin = "round";

                    // Hinteres Fenster
                    ctx.strokeStyle = winColor;
                    ctx.fillStyle = "transparent";
                    drawRoundedRect(ctx, 4, 2, 16, 12, 2.0);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(4, 5.5);
                    ctx.lineTo(20, 5.5);
                    ctx.stroke();

                    // Vorderes Fenster (überlagert, mit Hintergrundmaskierung)
                    ctx.fillStyle = maskBg;
                    drawRoundedRect(ctx, 11, 7, 17, 13, 2.0);
                    ctx.fill();
                    ctx.strokeStyle = winColor;
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(11, 10.5);
                    ctx.lineTo(28, 10.5);
                    ctx.stroke();

                } else if (mode === "apps") {
                    // 9-Quadrate Symbol in Kategorie-Farbe
                    ctx.fillStyle = appsColor;
                    var sqSize = 4.8;
                    var step = 6.6;
                    var startX = 6.0;
                    var startY = 2.5;
                    for (var r = 0; r < 3; r++) {
                        for (var c = 0; c < 3; c++) {
                            drawRoundedRect(ctx, startX + c * step, startY + r * step, sqSize, sqSize, 1.0);
                            ctx.fill();
                        }
                    }

                } else if (mode === "files") {
                    ctx.lineWidth = 1.4;
                    ctx.lineCap = "round";
                    ctx.lineJoin = "round";
                    ctx.strokeStyle = filesColor;

                    // Hintere A4-Seite (schlankes A4-Hochformat 11 x 18 px)
                    drawRoundedRect(ctx, 7.0, 1.5, 11.0, 18.0, 1.5);
                    ctx.stroke();

                    // Vordere A4-Seite mit abgeknickter Ecke (Hintergrundmaske, 11 x 18 px)
                    ctx.fillStyle = maskBg;
                    ctx.beginPath();
                    ctx.moveTo(14.0, 4.5);
                    ctx.lineTo(21.0, 4.5);
                    ctx.lineTo(25.0, 8.5);
                    ctx.lineTo(25.0, 22.5);
                    ctx.lineTo(14.0, 22.5);
                    ctx.closePath();
                    ctx.fill();
                    ctx.stroke();

                    // Abgeknickte Falzlinie der vorderen Seite
                    ctx.beginPath();
                    ctx.moveTo(21.0, 4.5);
                    ctx.lineTo(21.0, 8.5);
                    ctx.lineTo(25.0, 8.5);
                    ctx.stroke();
                }

                ctx.restore();
            }
        }

        // 2. Zentrierte Uhrzeit (etwas größer) und Datum (etwas kleiner darunter)
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: hubCategoryCanvas.bottom
            anchors.topMargin: Math.round(8 * ((typeof circleRadius !== "undefined" ? circleRadius : 75) / 75.0))
            spacing: 2

            Text {
                id: hubTimeText
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.round(130 * ((typeof circleRadius !== "undefined" ? circleRadius : 75) / 75.0))
                horizontalAlignment: Text.AlignHCenter
                text: root.currentTimeStr
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff"
                font.pixelSize: Math.round(22 * ((typeof circleRadius !== "undefined" ? circleRadius : 75) / 75.0))
                minimumPixelSize: Math.round(12 * ((typeof circleRadius !== "undefined" ? circleRadius : 75) / 75.0))
                fontSizeMode: Text.HorizontalFit
                font.bold: true
                font.family: "Noto Sans"
                visible: root.showClock
            }

            Text {
                id: hubDateText
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.round(136 * ((typeof circleRadius !== "undefined" ? circleRadius : 75) / 75.0))
                horizontalAlignment: Text.AlignHCenter
                text: root.currentDateStr
                color: (typeof karyonTheme !== "undefined") ? karyonTheme.textMuted : "#9faec0"
                font.pixelSize: Math.round(13.5 * ((typeof circleRadius !== "undefined" ? circleRadius : 75) / 75.0))
                minimumPixelSize: Math.round(8 * ((typeof circleRadius !== "undefined" ? circleRadius : 75) / 75.0))
                fontSizeMode: Text.HorizontalFit
                font.weight: Font.Medium
                font.family: "Noto Sans"
                visible: root.showDate
            }
        }

        // 3. Interaktiver Lautstärkebereich im unteren Drittel des Hubs (füllt das gesamte untere Drittel)
        Canvas {
            id: hubVolumeCanvas
            anchors.fill: parent
            visible: root.showVolumeControl

            property int vol: root.systemVolume
            property bool muted: root.systemMuted
            property bool isHovered: root.isCursorInVolumeArea
            property int vStep: root.currentVolumeStep

            onVolChanged: requestPaint()
            onMutedChanged: requestPaint()
            onIsHoveredChanged: requestPaint()
            onVStepChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.save();

                var cx = width / 2.0;
                var cy = height / 2.0;
                var r = (typeof circleRadius !== "undefined" ? circleRadius : 75);

                // 1. Auf den Hub-Kreis clippen (innerhalb der 1px Kontur)
                ctx.beginPath();
                ctx.arc(cx, cy, r - 1.0, 0, 2 * Math.PI);
                ctx.clip();

                // 2. Unterer Bereich: 7px niedriger als 1/3 (dy >= (r/3 + 7) = 32 px bei r=75)
                var chordY = cy + (r / 3.0) + 7.0;

                // Dezenter Grundton für den Lautstärkebereich
                ctx.fillStyle = "rgba(255, 255, 255, 0.015)";
                ctx.fillRect(0, chordY, width, height - chordY);

                // 3. Dynamische Anzahl vertikaler Balken entsprechend des Volume Steps (1-zu-1 Repräsentation)
                var hs = r / 75.0;
                var volPct = Math.max(0.0, Math.min(1.0, vol / 100.0));
                var curStep = Math.max(1, Math.min(20, root.currentVolumeStep));
                var numBars = Math.round(100.0 / curStep);
                var totalW = 126.0 * hs;
                var slotW = totalW / numBars;
                var gap = (numBars > 50) ? (0.4 * hs) : ((numBars > 25) ? (1.0 * hs) : (Math.max(1.5 * hs, slotW * 0.25)));
                var barW = Math.max(0.6 * hs, slotW - gap);
                var startX = cx - totalW / 2.0;

                var isLight = (typeof karyonTheme !== "undefined") && karyonTheme.isLight;
                var activeBars = Math.round(volPct * numBars);
                var activeCol = muted ? "rgba(255, 71, 87, 0.40)" : (isLight ? "rgba(24, 28, 36, 0.35)" : "rgba(255, 255, 255, 0.28)");
                var inactiveCol = isLight ? "rgba(24, 28, 36, 0.08)" : "rgba(255, 255, 255, 0.05)";

                for (var i = 0; i < numBars; i++) {
                    var bx = startX + i * slotW;
                    var dx = (bx + barW / 2.0) - cx;
                    var maxBottomY = chordY;
                    if (Math.abs(dx) < r) {
                        maxBottomY = cy + Math.sqrt(r * r - dx * dx) - 2.5 * hs;
                    }
                    var topY = chordY + 2.0 * hs;
                    var barH = maxBottomY - topY;
                    if (barH <= 1.0) continue;

                    ctx.fillStyle = (i < activeBars) ? activeCol : inactiveCol;
                    ctx.fillRect(bx, topY, barW, barH);
                }

                // 4. Zentrales Lautsprecher-Badge im unteren Bereich
                var spkCx = cx;
                var spkCy = cy + 51.5 * hs;
                var spkR = 14.5 * hs;

                // Kreisrunder Hintergrund OHNE Kontur/Rahmen
                ctx.beginPath();
                ctx.arc(spkCx, spkCy, spkR, 0, 2 * Math.PI);
                ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.baseBg : "#21252b";
                ctx.fill();

                // Lautsprechersymbol exakt zentriert im Badge
                var spkColor = muted ? "#ff4757" : (isLight ? "#181c24" : "#ffffff");
                ctx.fillStyle = spkColor;
                ctx.strokeStyle = spkColor;
                ctx.lineWidth = 1.4 * hs;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";

                // Geometrische Zentrierung
                var sx = spkCx - 2.0 * hs;
                var sy = spkCy;

                // Lautsprecher-Konus
                ctx.beginPath();
                ctx.moveTo(sx - 5.5 * hs, sy - 3.0 * hs);
                ctx.lineTo(sx - 2.0 * hs, sy - 3.0 * hs);
                ctx.lineTo(sx + 2.5 * hs, sy - 6.5 * hs);
                ctx.lineTo(sx + 2.5 * hs, sy + 6.5 * hs);
                ctx.lineTo(sx - 2.0 * hs, sy + 3.0 * hs);
                ctx.lineTo(sx - 5.5 * hs, sy + 3.0 * hs);
                ctx.closePath();
                ctx.fill();

                if (muted) {
                    // Diagonale Durchstreichung
                    ctx.beginPath();
                    ctx.moveTo(spkCx - 7.5 * hs, spkCy - 6.5 * hs);
                    ctx.lineTo(spkCx + 7.5 * hs, spkCy + 6.5 * hs);
                    ctx.stroke();
                } else {
                    // Dezente Schallwellen
                    ctx.beginPath();
                    ctx.arc(sx + 1.5 * hs, sy, 4.5 * hs, -Math.PI / 3.0, Math.PI / 3.0, false);
                    ctx.stroke();

                    ctx.beginPath();
                    ctx.arc(sx + 1.5 * hs, sy, 8.0 * hs, -Math.PI / 3.5, Math.PI / 3.5, false);
                    ctx.stroke();
                }

                ctx.restore();
            }
        }
    }

    // Ring-Canvas für Ring 1, Ring 2 und Ring 3
    Canvas {
        id: ringCanvas
        anchors.fill: parent

        property var segmentData1: segmentList1
        property var segmentData2: segmentList2
        property var segmentData3: segmentList3
        property int selRing: selectedRing
        property int selIdx: selectedIndex
        property bool ring2Active: hasRing2Active
        property bool ring3Active: hasRing3Active
        property bool isDrillActive: isDrilldownActive

        property bool pinHovered: isPinHovered
        property bool counterHovered: isCounterBadgeHovered
        onSegmentData1Changed: requestPaint()
        onSegmentData2Changed: requestPaint()
        onSegmentData3Changed: requestPaint()
        onSelRingChanged: requestPaint()
        onSelIdxChanged: requestPaint()
        onRing2ActiveChanged: requestPaint()
        onRing3ActiveChanged: requestPaint()
        onIsDrillActiveChanged: requestPaint()
        onPinHoveredChanged: requestPaint()
        onCounterHoveredChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();

            var cx = width / 2;
            var cy = height / 2;
            var innerR1 = circleRadius + ringGap;
            var outerR1 = innerR1 + ringDepth;

            function drawPin(ctx, px, py, isPinned, isHovered, pinAngle) {
                ctx.save();
                ctx.translate(px, py);
                // Rotate so that +X points directly toward the hub center (cx, cy)
                ctx.rotate(pinAngle + Math.PI);

                var isLight = (typeof karyonTheme !== "undefined" && karyonTheme.isLight);
                var col = isHovered
                    ? (isLight ? "#111827" : "#ffffff")
                    : (isPinned
                        ? (isLight ? "#2c313a" : "rgba(255, 255, 255, 0.85)")
                        : (isLight ? "rgba(44, 49, 58, 0.45)" : "rgba(255, 255, 255, 0.35)"));
                if (isHovered) {
                    ctx.shadowColor = isLight ? "rgba(0, 0, 0, 0.3)" : "#ffffff";
                    ctx.shadowBlur = 6;
                }

                ctx.strokeStyle = col;
                ctx.lineWidth = 1.3;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";

                // Round head (circle outline, NO fill)
                ctx.beginPath();
                ctx.arc(-4.0, 0, 3.0, 0, Math.PI * 2);
                ctx.stroke();

                if (!isPinned) {
                    // State 1: Unpinned (Long needle pointing towards hub center)
                    ctx.beginPath();
                    ctx.moveTo(-1.0, 0);
                    ctx.lineTo(5.5, 0);
                    ctx.stroke();
                } else {
                    // State 2: Pinned / Stuck in ("eingesteckt")
                    // Needle is shorter and goes into a small surface line
                    ctx.beginPath();
                    ctx.moveTo(-1.0, 0);
                    ctx.lineTo(2.0, 0);
                    ctx.stroke();

                    // Surface line perpendicular to needle
                    ctx.beginPath();
                    ctx.moveTo(2.0, -3.5);
                    ctx.lineTo(2.0, 3.5);
                    ctx.stroke();
                }

                ctx.restore();
            }

            function drawCounterBadge(ctx, bx, by, br, count, isHovered, scaleFactor) {
                ctx.save();
                ctx.beginPath();
                ctx.arc(bx, by, br, 0, Math.PI * 2, false);

                if (isHovered) {
                    ctx.fillStyle = "#e06c75";
                    ctx.fill();
                    ctx.strokeStyle = "#16181d";
                    ctx.lineWidth = 1.0;
                    ctx.stroke();

                    // Weißes X
                    ctx.beginPath();
                    var d = br * 0.40;
                    ctx.moveTo(bx - d, by - d);
                    ctx.lineTo(bx + d, by + d);
                    ctx.moveTo(bx - d, by + d);
                    ctx.lineTo(bx + d, by - d);
                    ctx.strokeStyle = "#ffffff";
                    ctx.lineWidth = 1.5;
                    ctx.lineCap = "round";
                    ctx.stroke();
                } else {
                    ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.baseBg : "#282c34";
                    ctx.fill();
                    ctx.strokeStyle = (typeof karyonTheme !== "undefined") ? (karyonTheme.isLight ? "rgba(0, 0, 0, 0.25)" : "rgba(255, 255, 255, 0.4)") : "rgba(255, 255, 255, 0.4)";
                    ctx.lineWidth = 1.0;
                    ctx.stroke();

                    ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#ffffff";
                    ctx.font = "bold " + Math.round(8.5 * scaleFactor) + "px sans-serif";
                    ctx.textAlign = "center";
                    ctx.textBaseline = "middle";
                    ctx.fillText(count.toString(), bx, by + 0.5);
                }
                ctx.restore();
            }

            function drawRingSegments(ringNum, segList, innerR, outerR, unselColor, borderCol) {
                var numSegs = (segList && segList.length) ? segList.length : 0;
                for (var i = 0; i < numSegs; i++) {
                    var seg = segList[i];
                    var a1 = seg.angle - seg.span / 2;
                    var a2 = seg.angle + seg.span / 2;

                    ctx.beginPath();
                    ctx.arc(cx, cy, outerR, a1, a2, false);
                    ctx.arc(cx, cy, innerR, a2, a1, true);
                    ctx.closePath();

                    var isSel = (selRing === ringNum && i === selIdx);

                    if (isSel) {
                        ctx.fillStyle = (typeof karyonTheme !== "undefined")
                            ? (seg.isRealWindow ? karyonTheme.windowSelectionBg : karyonTheme.ringSelectionBg)
                            : "#454a55";
                    } else if (seg.isRealWindow) {
                        ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.windowUnselBg : "#2a2f38";
                    } else {
                        ctx.fillStyle = unselColor;
                    }
                    ctx.fill();

                    var hasProgress = (seg.isRealWindow || seg.segmentType === "window") &&
                                      (typeof seg.progressPercent === "number") &&
                                      (seg.progressPercent >= 0 && seg.progressPercent <= 100);

                    if (hasProgress) {
                        // 1. Progress bar background: slightly darker than segment background (+5px höher -> 15px)
                        ctx.beginPath();
                        ctx.arc(cx, cy, outerR, a1, a2, false);
                        ctx.arc(cx, cy, outerR - 15, a2, a1, true);
                        ctx.closePath();
                        ctx.fillStyle = "rgba(0, 0, 0, 0.4)";
                        ctx.fill();

                        // 2. Green charging progress fill
                        if (seg.progressPercent > 0) {
                            var progSpan = seg.span * (seg.progressPercent / 100.0);
                            var progEnd = a1 + progSpan;
                            ctx.beginPath();
                            ctx.arc(cx, cy, outerR, a1, progEnd, false);
                            ctx.arc(cx, cy, outerR - 15, progEnd, a1, true);
                            ctx.closePath();
                            ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.identWindows : "#2ed573";
                            ctx.fill();
                        }

                        // 3. Segment borders
                        ctx.beginPath();
                        ctx.moveTo(cx + outerR * Math.cos(a1), cy + outerR * Math.sin(a1));
                        ctx.lineTo(cx + innerR * Math.cos(a1), cy + innerR * Math.sin(a1));
                        ctx.arc(cx, cy, innerR, a1, a2, false);
                        ctx.lineTo(cx + outerR * Math.cos(a2), cy + outerR * Math.sin(a2));
                        ctx.strokeStyle = borderCol;
                        ctx.lineWidth = 1;
                        ctx.stroke();

                        // 4. Normale Top-Linie (2.5px am Außenrand) beibehalten
                        ctx.beginPath();
                        ctx.arc(cx, cy, outerR - 1, a1, a2, false);
                        ctx.strokeStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.identWindows : "#2ed573";
                        ctx.lineWidth = 2.5;
                        ctx.stroke();

                        // 5. Drilldown indicator (if any)
                        if (seg.hasDrilldown) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, outerR, a1, a2, false);
                            ctx.arc(cx, cy, outerR - 15, a2, a1, true);
                            ctx.closePath();
                            ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.drilldownColor : "rgba(13, 124, 214, 0.30)";
                            ctx.fill();
                        }

                        // 6. White percentage text zentriert im 15px Ladebalken
                        var textR = outerR - 8.5;
                        var tx = cx + textR * Math.cos(seg.angle);
                        var ty = cy + textR * Math.sin(seg.angle);
                        ctx.save();
                        ctx.translate(tx, ty);
                        ctx.rotate(seg.angle + Math.PI / 2);
                        ctx.font = "bold 9px sans-serif";
                        ctx.textAlign = "center";
                        ctx.textBaseline = "middle";
                        ctx.fillStyle = "#ffffff";
                        ctx.shadowColor = "rgba(0, 0, 0, 0.7)";
                        ctx.shadowBlur = 2;
                        ctx.fillText(seg.progressPercent + " %", 0, 0);
                        ctx.restore();
                    } else {
                        // Top Edge color
                        var topColor = "";
                        if (seg.segmentType === "window") {
                            topColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identWindows : "#2ed573";
                        } else if (seg.segmentType === "file") {
                            topColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identFiles : "#ff9f43";
                        } else if (seg.segmentType === "app") {
                            topColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identApps : "#37d0ff";
                        }

                        // Border drawing:
                        // When topColor is present, DO NOT stroke outerR with borderCol so nothing dark peeks above top line!
                        if (topColor !== "") {
                            ctx.beginPath();
                            ctx.moveTo(cx + outerR * Math.cos(a1), cy + outerR * Math.sin(a1));
                            ctx.lineTo(cx + innerR * Math.cos(a1), cy + innerR * Math.sin(a1));
                            ctx.arc(cx, cy, innerR, a1, a2, false);
                            ctx.lineTo(cx + outerR * Math.cos(a2), cy + outerR * Math.sin(a2));
                            ctx.strokeStyle = borderCol;
                            ctx.lineWidth = 1;
                            ctx.stroke();

                            // Drilldown indicator (if any)
                            if (seg.hasDrilldown) {
                                ctx.beginPath();
                                ctx.arc(cx, cy, outerR, a1, a2, false);
                                ctx.arc(cx, cy, outerR - 15, a2, a1, true);
                                ctx.closePath();
                                ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.drilldownColor : "rgba(13, 124, 214, 0.30)";
                                ctx.fill();
                            }

                            // Top Edge 2.5px line at outerR - 1 covering outer boundary up to outerR
                            ctx.beginPath();
                            ctx.arc(cx, cy, outerR - 1, a1, a2, false);
                            ctx.strokeStyle = topColor;
                            ctx.lineWidth = 2.5;
                            ctx.stroke();
                        } else {
                            // Full border with borderCol for segments without topColor (e.g. Show Desktop)
                            ctx.strokeStyle = borderCol;
                            ctx.lineWidth = 1;
                            ctx.stroke();

                            // Drilldown indicator
                            if (seg.hasDrilldown) {
                                ctx.beginPath();
                                ctx.arc(cx, cy, outerR, a1, a2, false);
                                ctx.arc(cx, cy, outerR - 15, a2, a1, true);
                                ctx.closePath();
                                ctx.fillStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.drilldownColor : "rgba(13, 124, 214, 0.30)";
                                ctx.fill();
                            }
                        }
                    }

                    // Pinnadel in bottom-left corner
                    if (seg.canPin) {
                        var pinR = innerR + 9.5;
                        var pinAngle = a1 + (7.5 / innerR);
                        var px = cx + pinR * Math.cos(pinAngle);
                        var py = cy + pinR * Math.sin(pinAngle);
                        var isThisPinHovered = (isSel && root.isPinHovered);
                        drawPin(ctx, px, py, !!seg.isPinned, isThisPinHovered, pinAngle);
                    }

                    // Window Counter Badge (only on Ring 1 for multi-window groups, unterhalb des Ladebalkens)
                    if (ringNum === 1 && seg.isRealWindow && seg.windowCount > 1) {
                        var scaleFactor = (typeof overlayScale !== "undefined") ? overlayScale : 1.0;
                        var br = 7.0 * scaleFactor;
                        var badgeR = outerR - 24.5 * scaleFactor;
                        var badgeAngle = a2 - (11.5 * scaleFactor / badgeR);
                        var bx = cx + badgeR * Math.cos(badgeAngle);
                        var by = cy + badgeR * Math.sin(badgeAngle);
                        var isThisBadgeHovered = (isSel && root.isCounterBadgeHovered);
                        drawCounterBadge(ctx, bx, by, br, seg.windowCount, isThisBadgeHovered, scaleFactor);
                    }
                }
            }

            // 1. Ring 1 zeichnen
            var r1Bg = (typeof karyonTheme !== "undefined") ? karyonTheme.ring1Bg : "#21252b";
            var r1Border = (typeof karyonTheme !== "undefined") ? karyonTheme.ring1Border : "#16181d";
            drawRingSegments(1, segmentList1, innerR1, outerR1, r1Bg, r1Border);

            // 2. Ring 2 zeichnen (falls vorhanden)
            if (ring2Active && segmentData2 && segmentData2.length > 0) {
                var innerR2 = outerR1 + ring2Gap;
                var outerR2 = innerR2 + ring2Depth;
                var r2Bg = (typeof karyonTheme !== "undefined") ? karyonTheme.ring2Bg : "#1e2126";
                var r2Border = (typeof karyonTheme !== "undefined") ? karyonTheme.ring2Border : "#14161a";
                drawRingSegments(2, segmentData2, innerR2, outerR2, r2Bg, r2Border);
            }

            // 3. Ring 3 zeichnen (falls vorhanden)
            if (ring3Active && segmentData3 && segmentData3.length > 0) {
                var innerR2_3 = outerR1 + ring2Gap;
                var outerR2_3 = innerR2_3 + ring2Depth;
                var innerR3 = outerR2_3 + ring3Gap;
                var outerR3 = innerR3 + ring3Depth;
                var r3Bg = (typeof karyonTheme !== "undefined") ? karyonTheme.ring3Bg : "#1b1e23";
                var r3Border = (typeof karyonTheme !== "undefined") ? karyonTheme.ring3Border : "#121417";
                drawRingSegments(3, segmentData3, innerR3, outerR3, r3Bg, r3Border);
            }
        }
    }

    // Symbole / Beschriftungen auf Ring 1
    Repeater {
        model: segmentList1 || []

        Item {
            anchors.centerIn: parent

            transform: Translate {
                x: {
                    var innerR1 = circleRadius + ringGap;
                    var outerR1 = innerR1 + ringDepth;
                    var r1 = (innerR1 + outerR1) / 2;
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    return r1 * Math.cos(ang);
                }
                y: {
                    var innerR1 = circleRadius + ringGap;
                    var outerR1 = innerR1 + ringDepth;
                    var r1 = (innerR1 + outerR1) / 2;
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    return r1 * Math.sin(ang);
                }
            }

            Image {
                id: segIcon1
                visible: !modelData.isTextSegment && !!modelData.iconSource
                source: modelData.iconSource || ""
                width: iconSize
                height: iconSize
                smooth: true
                anchors.centerIn: parent
            }

            Text {
                id: segText1
                visible: !!modelData.isTextSegment
                text: modelData.label || ""
                color: (selectedRing === 1 && selectedIndex === index)
                    ? ((typeof karyonTheme !== "undefined") ? karyonTheme.identApps : "#37d0ff")
                    : ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8eef6")
                font.pixelSize: 10
                font.bold: (selectedRing === 1 && selectedIndex === index)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                width: ringDepth - 4
                anchors.centerIn: parent

                rotation: {
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    var deg = ang * 180 / Math.PI;
                    if (ang < -Math.PI / 2 || ang > Math.PI / 2) {
                        return deg + 180;
                    }
                    return deg;
                }
            }
        }
    }

    // Symbole / Beschriftungen auf Ring 2
    Repeater {
        model: (hasRing2Active && segmentList2) ? segmentList2 : []

        Item {
            anchors.centerIn: parent

            transform: Translate {
                x: {
                    var innerR1 = circleRadius + ringGap;
                    var outerR1 = innerR1 + ringDepth;
                    var innerR2 = outerR1 + ring2Gap;
                    var outerR2 = innerR2 + ring2Depth;
                    var r2 = (innerR2 + outerR2) / 2;
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    return r2 * Math.cos(ang);
                }
                y: {
                    var innerR1 = circleRadius + ringGap;
                    var outerR1 = innerR1 + ringDepth;
                    var innerR2 = outerR1 + ring2Gap;
                    var outerR2 = innerR2 + ring2Depth;
                    var r2 = (innerR2 + outerR2) / 2;
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    return r2 * Math.sin(ang);
                }
            }

            Image {
                id: segIcon2
                visible: !modelData.isTextSegment && !!modelData.iconSource
                source: modelData.iconSource || ""
                width: iconSize
                height: iconSize
                smooth: true
                anchors.centerIn: parent
            }

            Text {
                id: segText2
                visible: !!modelData.isTextSegment
                text: modelData.label || ""
                color: (selectedRing === 2 && selectedIndex === index)
                    ? ((typeof karyonTheme !== "undefined") ? karyonTheme.identApps : "#37d0ff")
                    : ((typeof karyonTheme !== "undefined") ? karyonTheme.textPrimary : "#e8eef6")
                font.pixelSize: Math.max(9, Math.round(10 * (circleRadius / 75.0)))
                font.bold: (selectedRing === 2 && selectedIndex === index)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                width: ring2Depth - 4
                anchors.centerIn: parent

                rotation: {
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    var deg = ang * 180 / Math.PI;
                    if (ang < -Math.PI / 2 || ang > Math.PI / 2) {
                        return deg + 180;
                    }
                    return deg;
                }
            }
        }
    }

    // Fenstersymbole auf Ring 3
    Repeater {
        model: (hasRing3Active && segmentList3) ? segmentList3 : []

        Image {
            id: segIcon3
            source: modelData.iconSource || ""
            width: iconSize
            height: iconSize
            smooth: true
            anchors.centerIn: parent

            transform: Translate {
                x: {
                    var innerR1 = circleRadius + ringGap;
                    var outerR1 = innerR1 + ringDepth;
                    var innerR2 = outerR1 + ring2Gap;
                    var outerR2 = innerR2 + ring2Depth;
                    var innerR3 = outerR2 + ring3Gap;
                    var outerR3 = innerR3 + ring3Depth;
                    var r3 = (innerR3 + outerR3) / 2;
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    return r3 * Math.cos(ang);
                }
                y: {
                    var innerR1 = circleRadius + ringGap;
                    var outerR1 = innerR1 + ringDepth;
                    var innerR2 = outerR1 + ring2Gap;
                    var outerR2 = innerR2 + ring2Depth;
                    var innerR3 = outerR2 + ring3Gap;
                    var outerR3 = innerR3 + ring3Depth;
                    var r3 = (innerR3 + outerR3) / 2;
                    var ang = (modelData && typeof modelData.angle !== "undefined") ? modelData.angle : 0;
                    return r3 * Math.sin(ang);
                }
            }
        }
    }

    // MouseArea für direktes Zeigen / Hovern mit der Maus auf dem Overlay (Normalbetrieb)
    MouseArea {
        id: overlayMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.ArrowCursor

        onEntered: function() {
            if (typeof overlayController !== "undefined" && overlayController) {
                overlayController.reportQmlPointerMoved();
            }
        }

        onPositionChanged: function(mouse) {
            if (overlayDropArea.containsDrag) return;
            if (typeof overlayController !== "undefined" && overlayController) {
                overlayController.reportQmlPointerMoved();
            }
            var cx = root.width / 2;
            var cy = root.height / 2;
            setAimOffset(mouse.x - cx, mouse.y - cy);
        }
    }

    // DropArea für Drag & Drop (wenn eine Datei mit gehaltener LMT über das Overlay gezogen wird)
    DropArea {
        id: overlayDropArea
        anchors.fill: parent
        onEntered: function(drag) {
            drag.accept();
        }
        onPositionChanged: function(drag) {
            drag.accept();
            var cx = root.width / 2;
            var cy = root.height / 2;
            setAimOffset(drag.x - cx, drag.y - cy);
        }
    }

    // Visueller Aiming-Indikator (Zielpunkt / Software-Cursor)
    // Erscheint, wenn KWin/Compositor-Events ausbleiben (z. B. im Spiel bei Pointer-Lock)
    Item {
        id: virtualAimCursor
        visible: fallbackCursorActive
        enabled: false
        z: 95
        x: (root.width / 2) + aimOffsetX - width / 2
        y: (root.height / 2) + aimOffsetY - height / 2
        width: 18
        height: 18

        // Äußerer dunkler Kontrastring
        Rectangle {
            anchors.centerIn: parent
            width: 16
            height: 16
            radius: 8
            color: "transparent"
            border.color: "#000000"
            border.width: 3
            opacity: 0.6
        }

        // Weißer Halo-Ring
        Rectangle {
            anchors.centerIn: parent
            width: 14
            height: 14
            radius: 7
            color: "transparent"
            border.color: "#ffffff"
            border.width: 2
        }

        // Innerer weißer Kern mit feiner dunkler Kontur
        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: "#ffffff"
            border.color: "#000000"
            border.width: 1
        }
    }

    // Statischer Tooltip an der Oberseite des gehighlighteten Segments
    // Schwarze Schrift auf weißem Grund, strikt keine Gleit- oder Fading-Animation
    Rectangle {
        id: tooltipBox
        visible: currentSelectedSegment !== null && currentTooltipText.length > 0
        enabled: false // Verhindert Abfangen von Mausevents
        z: 100

        width: tooltipLabel.implicitWidth + 16
        height: tooltipLabel.implicitHeight + 8
        radius: 4
        color: "#ffffff"
        border.color: "#000000"
        border.width: 1

        property real segAngle: currentSelectedSegment ? currentSelectedSegment.angle : 0
        property real segOuterRadius: {
            if (!currentSelectedSegment) return circleRadius + ringGap + ringDepth;
            var r = circleRadius + ringGap + ringDepth;
            if (currentSelectedSegment.ring >= 2) r += ring2Gap + ring2Depth;
            if (currentSelectedSegment.ring >= 3) r += ring3Gap + ring3Depth;
            return r;
        }

        x: {
            var cx = root.width / 2;
            var absCos = Math.abs(Math.cos(segAngle));
            var absSin = Math.abs(Math.sin(segAngle));
            var dX = absCos > 1e-4 ? (width / 2.0) / absCos : 999999;
            var dY = absSin > 1e-4 ? (height / 2.0) / absSin : 999999;
            var rRect = Math.min(dX, dY);
            var rDist = segOuterRadius + 8 + rRect;
            var rawX = cx + rDist * Math.cos(segAngle) - width / 2.0;
            return Math.max(4, Math.min(root.width - width - 4, rawX));
        }

        y: {
            var cy = root.height / 2;
            var absCos = Math.abs(Math.cos(segAngle));
            var absSin = Math.abs(Math.sin(segAngle));
            var dX = absCos > 1e-4 ? (width / 2.0) / absCos : 999999;
            var dY = absSin > 1e-4 ? (height / 2.0) / absSin : 999999;
            var rRect = Math.min(dX, dY);
            var rDist = segOuterRadius + 8 + rRect;
            var rawY = cy + rDist * Math.sin(segAngle) - height / 2.0;
            return Math.max(4, Math.min(root.height - height - 4, rawY));
        }

        Text {
            id: tooltipLabel
            anchors.centerIn: parent
            text: currentTooltipText
            color: "#000000"
            font.pixelSize: Math.max(10, Math.round(12 * (circleRadius / 75.0)))
            font.bold: false
            renderType: Text.NativeRendering
        }
    }
}
