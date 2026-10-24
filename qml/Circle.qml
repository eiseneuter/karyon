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
    property bool isPerformanceMode: (typeof performanceMode !== "undefined") ? performanceMode : false

    onIsPerformanceModeChanged: {
        ringCanvas.requestPaint();
        if (ringTopEdgeCanvas.visible) {
            ringTopEdgeCanvas.requestPaint();
        }
    }

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
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: root.updateClockAndDate()
    }

    property int systemVolume: (typeof volumeLevel !== "undefined") ? volumeLevel : 50
    property int systemMaxVolume: (typeof maxVolumeLevel !== "undefined") ? maxVolumeLevel : 100
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

        // Drilldown NUR innerhalb des gezeichneten Drilldown-Streifens (outerR - 15 bis outerR) auslösen.
        // Der Bereich außerhalb des Segments (z.B. Tooltip-Bereich) darf KEINEN Drilldown auslösen,
        // damit geöffnete Ring-2-Segmente nicht schon beim bloßen Vorbeistreifen geschlossen werden.
        if (dist < outerR - 15 || dist > outerR) return false;

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
        // Vorherige Auswahl über die windowId merken (Indizes können sich durch den Neuaufbau verschieben)
        var prevRing = selectedRing;
        var prevId = "";
        if (prevRing === 1 && segmentList1 && selectedIndex >= 0 && selectedIndex < segmentList1.length) {
            prevId = segmentList1[selectedIndex].windowId || "";
        } else if (prevRing === 2 && segmentList2 && selectedIndex >= 0 && selectedIndex < segmentList2.length) {
            prevId = segmentList2[selectedIndex].windowId || "";
        } else if (prevRing === 3 && segmentList3 && selectedIndex >= 0 && selectedIndex < segmentList3.length) {
            prevId = segmentList3[selectedIndex].windowId || "";
        }

        segmentList1 = s1;
        segmentList2 = s2;
        segmentList3 = s3;
        hasRing2Active = r2Active;
        hasRing3Active = r3Active;

        // Auswahl ausschließlich anhand der aktuellen Zielposition neu bestimmen (einheitliche Regel)
        selectedRing = -1;
        selectedIndex = -1;
        updateSelection();

        if (selectedRing === -1) {
            // Kein Treffer an der Cursorposition: vorheriges Segment (gleiche windowId) wiederfinden
            var restoreList = (prevRing === 1) ? s1 : (prevRing === 2 && r2Active) ? s2 : (prevRing === 3 && r3Active) ? s3 : null;
            if (restoreList && prevId !== "") {
                for (var i = 0; i < restoreList.length; i++) {
                    if ((restoreList[i].windowId || "") === prevId) {
                        selectedRing = prevRing;
                        selectedIndex = i;
                        break;
                    }
                }
            }
        }
        if (selectedRing === -1) {
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

    property real lastAimTimestamp: 0
    property real pendingAimX: 0
    property real pendingAimY: 0
    property bool hasPendingAim: false

    Timer {
        id: aimThrottleTimer
        interval: root.isPerformanceMode ? 33 : 11
        repeat: false
        onTriggered: {
            if (root.hasPendingAim) {
                root.hasPendingAim = false;
                root.lastAimTimestamp = Date.now();
                root.applyAimOffset(root.pendingAimX, root.pendingAimY);
            }
        }
    }

    function setAimOffset(ox, oy) {
        var now = Date.now();
        var minInterval = root.isPerformanceMode ? 33 : 11;
        if (now - lastAimTimestamp >= minInterval) {
            aimThrottleTimer.stop();
            hasPendingAim = false;
            lastAimTimestamp = now;
            applyAimOffset(ox, oy);
        } else {
            pendingAimX = ox;
            pendingAimY = oy;
            hasPendingAim = true;
            if (!aimThrottleTimer.running) {
                var remaining = Math.max(1, minInterval - (now - lastAimTimestamp));
                aimThrottleTimer.interval = remaining;
                aimThrottleTimer.start();
            }
        }
    }

    function applyAimOffset(ox, oy) {
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

        // EINHEITLICHE ZIELSTRAHL-REGEL:
        // Ein unsichtbarer Zielstrahl erstreckt sich vom Hub-Zentrum durch den Cursor über den gesamten Bildschirm.
        // 1. Entlang des Zielstrahls (Winkel) werden direkte Treffer auf allen aktiven Ringen geprüft (Winkelspanne + Fuge).
        // 2. Gibt es direkte Treffer: Unter allen im Strahl liegenden Segmenten wird dasjenige gewählt,
        //    das dem Cursor radial am nächsten ist:
        //    - Befindet sich der Cursor physisch in einem Ring: dieses Segment (radialer Abstand 0).
        //    - Befindet sich der Cursor außerhalb der Ringe: das äußerste vorhandene Segment im Zielstrahl
        //      (bei 3 Ringen Ring 3; wo Ring 3 aufhört, aber Ring 2 noch Segmente hat, Ring 2; usw.).
        //    - Befindet sich der Cursor im Hub: Ring 1 (dem Hub am nächsten), bzw. Ring 2 falls Ring 1 leer ist.
        // 3. Gibt es im Zielstrahl keinen direkten Treffer (z.B. seitlicher oder unterer freier Raum):
        //    Der Zielstrahl erfasst auf den aktiven Ringen das im Winkel nächstgelegene Segment
        //    (stetige Projektion an den Bogenrändern – strikt KEIN Peilungsverlust beim Durchqueren oder Verlassen/Eintreten!).
        // 4. Liegt der Cursor innerhalb einer winzigen 3px-Zone um das exakte Zentrum (0, 0):
        //    Die bestehende Auswahl bleibt stabil erhalten, um Subpixel-Jitter am mathematischen Pol zu vermeiden.
        var gapPx = (typeof segGap !== "undefined") ? segGap : 4;
        var deadR = (typeof deadzoneRadius !== "undefined") ? Math.min(deadzoneRadius, 3.0) : 3.0;

        if (dist >= deadR) {
            function findDirectHit(list, count, ringInnerR, skipMedia) {
                if (count <= 0) return -1;
                // Halbe Fugenbreite (inkl. 8px Cluster-Fuge auf Außenringen) + Rundungsreserve
                var tol = Math.max((gapPx / Math.max(1, ringInnerR)) / 2.0 + 0.005,
                                   (8.0 * (typeof overlayScale !== "undefined" ? overlayScale : 1.0) / Math.max(1, ringInnerR)) / 2.0 + 0.005);
                var best = -1;
                var bestExcess = 1e9;
                for (var n = 0; n < count; n++) {
                    var s = list[n];
                    if (!s) continue;
                    if (skipMedia && volumeScrolledThisSession && (s.segmentType === "media" || (s.windowId && s.windowId.indexOf("__MEDIA_") === 0))) {
                        continue;
                    }
                    var d = angle - s.angle;
                    while (d > Math.PI) d -= 2 * Math.PI;
                    while (d < -Math.PI) d += 2 * Math.PI;
                    var excess = Math.abs(d) - (s.span / 2.0);
                    if (excess <= tol && excess < bestExcess) {
                        bestExcess = excess;
                        best = n;
                    }
                }
                return best;
            }

            var numSegs3 = (hasRing3Active && segmentList3 && segmentList3.length) ? segmentList3.length : 0;
            var numSegs2 = (hasRing2Active && segmentList2 && segmentList2.length) ? segmentList2.length : 0;
            var numSegs1 = (segmentList1 && segmentList1.length) ? segmentList1.length : 0;

            var hit3 = findDirectHit(segmentList3, numSegs3, innerR3, false);
            var hit2 = findDirectHit(segmentList2, numSegs2, innerR2, false);
            var hit1 = findDirectHit(segmentList1, numSegs1, innerR1, true);

            var directCandidates = [];
            if (hit3 >= 0) directCandidates.push({ ring: 3, hit: hit3, inner: innerR3, outer: outerR3 });
            if (hit2 >= 0) directCandidates.push({ ring: 2, hit: hit2, inner: innerR2, outer: outerR2 });
            if (hit1 >= 0) directCandidates.push({ ring: 1, hit: hit1, inner: innerR1, outer: outerR1 });

            if (directCandidates.length > 0) {
                var bestRing = -1;
                var bestIndex = -1;
                var minRDist = 1e9;

                for (var q = 0; q < directCandidates.length; q++) {
                    var cand = directCandidates[q];
                    var rDist = 0;
                    if (dist < cand.inner) {
                        rDist = cand.inner - dist;
                    } else if (dist > cand.outer) {
                        rDist = dist - cand.outer;
                    } else {
                        rDist = 0;
                    }

                    if (rDist < minRDist) {
                        minRDist = rDist;
                        bestRing = cand.ring;
                        bestIndex = cand.hit;
                    }
                }

                if (bestRing !== -1) {
                    foundRing = bestRing;
                    foundIndex = bestIndex;
                }
            } else {
                // Freier Raum ohne direkten Treffer: Zielstrahl erfasst das im Winkel nächstgelegene Segment
                function findClosestSegment(list, count, skipMedia) {
                    if (count <= 0) return { index: -1, angleDiff: 1e9 };
                    var bestIdx = -1;
                    var minDiff = 1e9;
                    for (var n = 0; n < count; n++) {
                        var s = list[n];
                        if (!s) continue;
                        if (skipMedia && volumeScrolledThisSession && (s.segmentType === "media" || (s.windowId && s.windowId.indexOf("__MEDIA_") === 0))) {
                            continue;
                        }
                        var d = angle - s.angle;
                        while (d > Math.PI) d -= 2 * Math.PI;
                        while (d < -Math.PI) d += 2 * Math.PI;
                        var diff = Math.abs(d);
                        if (diff < minDiff) {
                            minDiff = diff;
                            bestIdx = n;
                        }
                    }
                    return { index: bestIdx, angleDiff: minDiff };
                }

                var c3 = findClosestSegment(segmentList3, numSegs3, false);
                var c2 = findClosestSegment(segmentList2, numSegs2, false);
                var c1 = findClosestSegment(segmentList1, numSegs1, true);

                var fallbackCandidates = [];
                if (c3.index >= 0) fallbackCandidates.push({ ring: 3, hit: c3.index, angleDiff: c3.angleDiff, inner: innerR3, outer: outerR3 });
                if (c2.index >= 0) fallbackCandidates.push({ ring: 2, hit: c2.index, angleDiff: c2.angleDiff, inner: innerR2, outer: outerR2 });
                if (c1.index >= 0) fallbackCandidates.push({ ring: 1, hit: c1.index, angleDiff: c1.angleDiff, inner: innerR1, outer: outerR1 });

                if (fallbackCandidates.length > 0) {
                    var globalMinAngleDiff = 1e9;
                    for (var i = 0; i < fallbackCandidates.length; i++) {
                        if (fallbackCandidates[i].angleDiff < globalMinAngleDiff) {
                            globalMinAngleDiff = fallbackCandidates[i].angleDiff;
                        }
                    }

                    var bestFallbackRing = -1;
                    var bestFallbackIndex = -1;
                    var minFallbackRDist = 1e9;

                    for (var j = 0; j < fallbackCandidates.length; j++) {
                        var fc = fallbackCandidates[j];
                        if (fc.angleDiff <= globalMinAngleDiff + 0.20) {
                            var fbRDist = 0;
                            if (dist < fc.inner) {
                                fbRDist = fc.inner - dist;
                            } else if (dist > fc.outer) {
                                fbRDist = dist - fc.outer;
                            } else {
                                fbRDist = 0;
                            }

                            if (fbRDist < minFallbackRDist) {
                                minFallbackRDist = fbRDist;
                                bestFallbackRing = fc.ring;
                                bestFallbackIndex = fc.hit;
                            }
                        }
                    }

                    if (bestFallbackRing !== -1) {
                        foundRing = bestFallbackRing;
                        foundIndex = bestFallbackIndex;
                    }
                }
            }
        }

        // Befindet sich unter dem Zielstrahl ein gültiger Treffer, Auswahl aktualisieren.
        // Andernfalls (freier Raum oder Totzone) bleibt der Fokus unverändert!
        if (foundRing !== -1) {
            if (selectedRing !== foundRing || selectedIndex !== foundIndex) {
                selectedRing = foundRing;
                selectedIndex = foundIndex;
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

    Connections {
        target: (typeof overlayController !== "undefined") ? overlayController : null
        function onOverlayShown() {
            root.updateClockAndDate();
            overlayMouseArea.initialPointerX = -1;
            overlayMouseArea.initialPointerY = -1;
        }
        function onOverlayHidden() {
            overlayMouseArea.initialPointerX = -1;
            overlayMouseArea.initialPointerY = -1;
            aimThrottleTimer.stop();
            root.hasPendingAim = false;
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
            property int maxVol: root.systemMaxVolume
            property bool muted: root.systemMuted
            property bool isHovered: root.isCursorInVolumeArea
            property int vStep: root.currentVolumeStep

            onVolChanged: requestPaint()
            onMaxVolChanged: requestPaint()
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
                ctx.fillStyle = isHovered ? "rgba(255, 255, 255, 0.035)" : "rgba(255, 255, 255, 0.015)";
                ctx.fillRect(0, chordY, width, height - chordY);

                // 3. Dynamische Anzahl vertikaler Balken entsprechend des Volume Steps und maxVol
                var hs = r / 75.0;
                var effectiveMax = Math.max(100, maxVol);
                var curStep = Math.max(1, Math.min(20, root.currentVolumeStep));
                var numBars = Math.round(effectiveMax / curStep);
                var bar100 = Math.round(100.0 / curStep);
                var totalW = 126.0 * hs;
                var slotW = totalW / numBars;
                var gap = (numBars > 50) ? (0.4 * hs) : ((numBars > 25) ? (1.0 * hs) : (Math.max(1.5 * hs, slotW * 0.25)));
                var barW = Math.max(0.6 * hs, slotW - gap);
                var startX = cx - totalW / 2.0;

                var isLight = (typeof karyonTheme !== "undefined") && karyonTheme.isLight;
                var activeBars = Math.round((Math.max(0, Math.min(effectiveMax, vol)) / effectiveMax) * numBars);

                var activeCol = muted ? "rgba(255, 71, 87, 0.40)" : (isLight ? "rgba(24, 28, 36, 0.35)" : "rgba(255, 255, 255, 0.28)");
                var inactiveCol = isLight ? "rgba(24, 28, 36, 0.08)" : "rgba(255, 255, 255, 0.05)";
                // Akzentuierte Übersteuerungs-/Amplified-Farbe für Bereiche über 100% (Warmes Bernstein/Orange)
                var activeOverdriveCol = muted ? "rgba(255, 71, 87, 0.40)" : (isLight ? "rgba(230, 126, 34, 0.70)" : "rgba(255, 159, 67, 0.65)");
                var inactiveOverdriveCol = isLight ? "rgba(230, 126, 34, 0.14)" : "rgba(255, 159, 67, 0.10)";

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

                    var isOverdrive = (i >= bar100);
                    if (i < activeBars) {
                        ctx.fillStyle = isOverdrive ? activeOverdriveCol : activeCol;
                    } else {
                        ctx.fillStyle = isOverdrive ? inactiveOverdriveCol : inactiveCol;
                    }
                    ctx.fillRect(bx, topY, barW, barH);
                }

                // Subtile 100%-Markierung, falls der Balken über 100% hinausgeht
                if (effectiveMax > 100 && bar100 < numBars) {
                    var tickX = startX + bar100 * slotW - (gap / 2.0);
                    ctx.fillStyle = isLight ? "rgba(24, 28, 36, 0.30)" : "rgba(255, 255, 255, 0.30)";
                    ctx.fillRect(tickX - 0.5 * hs, chordY, 1.0 * hs, 3.5 * hs);
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

                var isOver = (vol > 100);
                var spkColor = muted ? "#ff4757" : (isOver ? (isLight ? "#d35400" : "#ff9f43") : (isLight ? "#181c24" : "#ffffff"));

                if (isHovered && !muted) {
                    // Prozentanzeige beim Hovern / Scrollen im Badge (in Datumsgrau, zentriert und ohne '%')
                    var dateGray = (typeof karyonTheme !== "undefined" && karyonTheme.textMuted) ? karyonTheme.textMuted : "#9faec0";
                    ctx.font = Math.round(11 * hs) + "px 'Noto Sans', sans-serif";
                    ctx.textAlign = "center";
                    ctx.textBaseline = "middle";
                    ctx.fillStyle = dateGray;
                    ctx.fillText(vol, spkCx, spkCy);
                } else {
                    // Lautsprechersymbol exakt zentriert im Badge
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
                }

                ctx.restore();
            }
        }
    }

    // Ring-Canvas für Ring 1, Ring 2 und Ring 3
    Canvas {
        id: ringCanvas
        anchors.fill: parent
        antialiasing: !root.isPerformanceMode

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

                        // 4. Normale Top-Linie (2.5px am Außenrand) beibehalten (im Performance-Modus übernimmt ringTopEdgeCanvas das AA-Zeichnen)
                        if (!root.isPerformanceMode) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, outerR - 1, a1, a2, false);
                            ctx.strokeStyle = (typeof karyonTheme !== "undefined") ? karyonTheme.identWindows : "#2ed573";
                            ctx.lineWidth = 2.5;
                            ctx.stroke();
                        }

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
                            if (!root.isPerformanceMode) {
                                ctx.beginPath();
                                ctx.arc(cx, cy, outerR - 1, a1, a2, false);
                                ctx.strokeStyle = topColor;
                                ctx.lineWidth = 2.5;
                                ctx.stroke();
                            }
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

    // Antialiased Top-Edge Canvas im Performance-Modus (zeichnet NUR die 2.5px Oberkanten mit AA)
    Canvas {
        id: ringTopEdgeCanvas
        anchors.fill: parent
        visible: root.isPerformanceMode
        antialiasing: true

        property var segmentData1: segmentList1
        property var segmentData2: segmentList2
        property var segmentData3: segmentList3
        property bool ring2Active: hasRing2Active
        property bool ring3Active: hasRing3Active

        onSegmentData1Changed: requestPaint()
        onSegmentData2Changed: requestPaint()
        onSegmentData3Changed: requestPaint()
        onRing2ActiveChanged: requestPaint()
        onRing3ActiveChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()

        Connections {
            target: (typeof karyonTheme !== "undefined") ? karyonTheme : null
            function onThemeChanged() {
                if (ringTopEdgeCanvas.visible) {
                    ringTopEdgeCanvas.requestPaint();
                }
            }
        }

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();

            var cx = width / 2;
            var cy = height / 2;
            var innerR1 = circleRadius + ringGap;
            var outerR1 = innerR1 + ringDepth;

            function drawEdges(segList, outerR) {
                var numSegs = (segList && segList.length) ? segList.length : 0;
                for (var i = 0; i < numSegs; i++) {
                    var seg = segList[i];
                    if (!seg) continue;
                    var topColor = "";
                    var hasProgress = (seg.isRealWindow || seg.segmentType === "window") &&
                                      (typeof seg.progressPercent === "number") &&
                                      (seg.progressPercent >= 0 && seg.progressPercent <= 100);
                    if (hasProgress || seg.segmentType === "window") {
                        topColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identWindows : "#2ed573";
                    } else if (seg.segmentType === "file") {
                        topColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identFiles : "#ff9f43";
                    } else if (seg.segmentType === "app") {
                        topColor = (typeof karyonTheme !== "undefined") ? karyonTheme.identApps : "#37d0ff";
                    }
                    if (topColor !== "") {
                        var a1 = seg.angle - seg.span / 2;
                        var a2 = seg.angle + seg.span / 2;
                        ctx.beginPath();
                        ctx.arc(cx, cy, outerR - 1, a1, a2, false);
                        ctx.strokeStyle = topColor;
                        ctx.lineWidth = 2.5;
                        ctx.stroke();
                    }
                }
            }

            // Ring 1
            drawEdges(segmentData1, outerR1);

            // Ring 2
            if (ring2Active && segmentData2 && segmentData2.length > 0) {
                var innerR2 = outerR1 + ring2Gap;
                var outerR2 = innerR2 + ring2Depth;
                drawEdges(segmentData2, outerR2);
            }

            // Ring 3
            if (ring3Active && segmentData3 && segmentData3.length > 0) {
                var innerR2_3 = outerR1 + ring2Gap;
                var outerR2_3 = innerR2_3 + ring2Depth;
                var innerR3 = outerR2_3 + ring3Gap;
                var outerR3 = innerR3 + ring3Depth;
                drawEdges(segmentData3, outerR3);
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
                visible: (modelData && !modelData.isTextSegment && !!modelData.iconSource)
                source: (modelData && modelData.iconSource) ? modelData.iconSource : ""
                width: iconSize
                height: iconSize
                smooth: true
                anchors.centerIn: parent
            }

            Text {
                id: segText1
                visible: (modelData && !!modelData.isTextSegment)
                text: (modelData && modelData.label) ? modelData.label : ""
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
                visible: (modelData && !modelData.isTextSegment && !!modelData.iconSource)
                source: (modelData && modelData.iconSource) ? modelData.iconSource : ""
                width: iconSize
                height: iconSize
                smooth: true
                anchors.centerIn: parent
            }

            Text {
                id: segText2
                visible: (modelData && !!modelData.isTextSegment)
                text: (modelData && modelData.label) ? modelData.label : ""
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
            source: (modelData && modelData.iconSource) ? modelData.iconSource : ""
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

        property int initialPointerX: -1
        property int initialPointerY: -1

        onPositionChanged: function(mouse) {
            if (overlayDropArea.containsDrag) return;

            // Erstes synthetisches Event beim Einblenden des Overviews ignorieren
            if (initialPointerX < 0) {
                initialPointerX = mouse.x;
                initialPointerY = mouse.y;
                return;
            }

            var dx = mouse.x - initialPointerX;
            var dy = mouse.y - initialPointerY;
            // Sobald sich der Compositor-Cursor tatsächlich vom Ursprungspunkt wegbewegt,
            // liegt echte Mausbewegung vom Fenstermanager vor:
            if ((dx * dx + dy * dy) >= 4) {
                if (typeof overlayController !== "undefined" && overlayController) {
                    overlayController.reportQmlPointerMoved();
                }
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
