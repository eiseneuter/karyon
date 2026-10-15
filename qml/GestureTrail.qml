import QtQuick

Item {
    id: root
    anchors.fill: parent

    property real startX: 0.0
    property real startY: 0.0
    property real curX: 0.0
    property real curY: 0.0
    property real globalFade: 0.0
    property bool isFinished: false

    readonly property real maxTrailDist: 160.0 // Etwas kompakterer, feinerer Leuchtschweif (160px)
    readonly property int numInstances: 32     // Ausgewogene Punktdichte für eine elegante Spur

    Timer {
        id: autoFinishTimer
        interval: 220
        repeat: false
        onTriggered: {
            root.finishTrail();
        }
    }

    function startTrail(sx, sy) {
        fadeAnim.stop();
        autoFinishTimer.restart();
        isFinished = false;
        startX = sx;
        startY = sy;
        curX = sx;
        curY = sy;
        globalFade = 1.0;
        canvas.requestPaint();
    }

    function updateCurPos(cx, cy) {
        if (isFinished) return;
        curX = cx;
        curY = cy;
        canvas.requestPaint();
    }

    function finishTrail() {
        if (isFinished) return;
        autoFinishTimer.stop();
        isFinished = true;
        fadeAnim.restart();
    }

    function cancelTrail() {
        autoFinishTimer.stop();
        fadeAnim.stop();
        isFinished = true;
        globalFade = 0.0;
        startX = 0;
        startY = 0;
        curX = 0;
        curY = 0;
        canvas.requestPaint();
    }

    NumberAnimation {
        id: fadeAnim
        target: root
        property: "globalFade"
        from: 1.0
        to: 0.0
        duration: 200
        easing.type: Easing.OutQuad
        onFinished: {
            root.startX = 0;
            root.startY = 0;
            root.curX = 0;
            root.curY = 0;
            canvas.requestPaint();
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            if (root.globalFade <= 0.005) return;

            var sx = root.startX;
            var sy = root.startY;
            var cx = root.curX;
            var cy = root.curY;

            var dx = cx - sx;
            var dy = cy - sy;
            var dist = Math.sqrt(dx * dx + dy * dy);

            var angle = 0.0;
            var trailLen = 0.0;
            if (dist > 1.0) {
                angle = Math.atan2(dy, dx);
                trailLen = Math.min(root.maxTrailDist, dist);
            }

            var n = root.numInstances;

            ctx.save();

            // Eleganter Leuchtschweif mit sichtbarerem Kopf und weichem Auslauf (von hinten nach vorne)
            for (var i = n - 1; i >= 0; --i) {
                var t = i / (n - 1); // 0.0 (Spitze an der Maus) bis 1.0 (Schweifende)

                // Wenn die Maus noch kaum bewegt wurde, nur die Kopfinstanz zeichnen
                if (dist < 3.0 && i > 0) continue;

                var offset = trailLen * t;
                var px = cx - Math.cos(angle) * offset;
                var py = cy - Math.sin(angle) * offset;

                // Dimension: Kopf ca. 4.6px Radius, nach hinten sanft abnehmend bis ca. 1.8px
                var r = 4.6 * (1.0 - 0.60 * t);

                // Angenehm leuchtende Deckkraft
                var alpha = (1.0 - 0.70 * t) * 0.75 * root.globalFade;

                // Unscharfer Radialgradient (lebendiges cyanblaues Leuchten)
                var grad = ctx.createRadialGradient(px, py, 0, px, py, r);
                grad.addColorStop(0.0,  "rgba(240, 252, 255, " + (0.85 * alpha) + ")");
                grad.addColorStop(0.35, "rgba(100, 215, 255, " + (0.58 * alpha) + ")");
                grad.addColorStop(0.70, "rgba(45, 150, 255, "  + (0.28 * alpha) + ")");
                grad.addColorStop(1.0,  "rgba(20, 100, 255, 0.0)");

                ctx.fillStyle = grad;
                ctx.beginPath();
                ctx.arc(px, py, r, 0, Math.PI * 2);
                ctx.fill();
            }

            ctx.restore();
        }
    }
}
