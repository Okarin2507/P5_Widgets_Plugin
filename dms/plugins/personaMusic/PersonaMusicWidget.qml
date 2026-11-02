import QtQuick
import QtQuick.Shapes
import Quickshell.Services.Mpris
import qs.Common
import qs.Services
import qs.Modules.Plugins

// Persona 5 "Futaba dialogue" music player.
// Needs, next to this file:
//   assets/futaba_frame.png   assets/banner.png
//   fonts/personafont.ttf     fonts/PersonifiedUXRegular.ttf   (fonts optional)

DesktopPluginComponent {
    id: root

    // ================= COLORS (edit these) =================
    readonly property color cText: "#ffffff"          // letters / idle fill
    readonly property color cInk: "#0a0d10"           // idle outline of the buttons
    readonly property color cBlack: "#000000"         // tag + time text, card shadow
    readonly property color cHotFill: "#000000"       // button letters on hover
    readonly property color cHotEdge: "#ffffff"       // button outline on hover
    readonly property color cSeekFill: "#e60012"      // progress bar fill
    readonly property color cSeekTrack: "#1c1f24"     // progress bar empty part
    readonly property color cShuffleOff: "#5fb4dc"
    readonly property color cShuffleOn: "#d8f6ff"
    // =======================================================

    readonly property real designW: 980
    readonly property real designH: 545

    minWidth: 390
    minHeight: 217
    widgetWidth: 780
    widgetHeight: 434
    property real defaultWidth: 780
    property real defaultHeight: 434

    // ---------- Player ----------
    readonly property var player: {
        const list = Mpris.players.values;
        for (let i = 0; i < list.length; i++) {
            if (list[i].isPlaying) return list[i];
        }
        for (let j = 0; j < list.length; j++) {
            if (list[j].identity === "Spotify") return list[j];
        }
        return list.length > 0 ? list[0] : null;
    }
    readonly property bool playing: player ? player.isPlaying : false
    readonly property real pos: player ? player.position : 0
    readonly property real len: player ? player.length : 0
    property bool showRemaining: false

    function fmt(s) {
        const t = Math.max(0, Math.floor(s || 0));
        const m = Math.floor(t / 60);
        const sec = t % 60;
        return (m < 10 ? "0" : "") + m + ":" + (sec < 10 ? "0" : "") + sec;
    }

    Timer {
        interval: 500
        repeat: true
        running: root.playing
        onTriggered: root.player.positionChanged()
    }

    FontLoader { id: fPersona; source: Qt.resolvedUrl("fonts/personafont.ttf") }
    FontLoader { id: fUx; source: Qt.resolvedUrl("fonts/PersonifiedUXRegular.ttf") }

    // ---------- Cut-out lettering ----------
    // Every letter gets its own small tilt / size / baseline shift (the ransom-note look).
    // edge > 0 adds a spiky sticker outline made of a ring of offset copies.
    component Jitter: Item {
        id: j
        property string text: ""
        property string family: ""
        property real pixelSize: 30
        property bool bold: false
        property color color: "#ffffff"
        property color edgeColor: "#000000"
        property real edge: 0
        property int rings: 14
        property real jit: 1.0
        property real spacing: 0
        property real seed: 0
        property real pad: 0
        property int maxChars: 0
        property var inv: []
        property color boxColor: "#000000"
        property color boxTextColor: "#ffffff"
        property color boxBorder: "#ffffff"

        readonly property string shown: (maxChars > 0 && text.length > maxChars)
            ? text.slice(0, maxChars - 3) + "..." : text

        implicitWidth: row.width + pad * 2
        implicitHeight: pixelSize * 1.3 + pad * 2

        function rnd(i, k) {
            const x = Math.sin((i + 1) * 12.9898 + k * 78.233 + j.seed) * 43758.5453;
            return x - Math.floor(x);
        }

        Row {
            id: row
            x: j.pad
            y: j.pad
            spacing: j.spacing
            Repeater {
                model: j.shown.length
                Item {
                    id: ch
                    readonly property string c: j.shown.charAt(index)
                    readonly property bool boxed: j.inv.indexOf(index) >= 0
                    width: glyph.implicitWidth
                    height: glyph.implicitHeight
                    rotation: (j.rnd(index, 1) - 0.5) * 14 * j.jit
                    y: (j.rnd(index, 2) - 0.5) * j.pixelSize * 0.22 * j.jit
                    scale: 1 + (j.rnd(index, 3) - 0.5) * 0.22 * j.jit
                    transformOrigin: Item.Center

                    Rectangle {
                        visible: ch.boxed
                        anchors.fill: parent
                        anchors.margins: -4
                        color: j.boxColor
                        border.color: j.boxBorder
                        border.width: 2
                    }
                    Repeater {
                        model: (j.edge > 0 && !ch.boxed) ? j.rings : 0
                        Text {
                            readonly property real a: index * 2 * Math.PI / j.rings
                            readonly property real rr: j.edge * (index % 2 === 0 ? 1.0 : 0.45)
                            x: Math.cos(a) * rr
                            y: Math.sin(a) * rr
                            text: ch.c
                            font.family: j.family
                            font.pixelSize: j.pixelSize
                            font.bold: j.bold
                            color: j.edgeColor
                        }
                    }
                    Text {
                        id: glyph
                        text: ch.c
                        font.family: j.family
                        font.pixelSize: j.pixelSize
                        font.bold: j.bold
                        color: ch.boxed ? j.boxTextColor : j.color
                    }
                }
            }
        }
    }

    component Btn: Item {
        id: b
        property string label: ""
        property string family: ""
        property real size: 42
        property real seed: 0
        property color idleFill: "#ffffff"
        property color idleEdge: "#0a0d10"
        property color hotFill: "#000000"
        property color hotEdge: "#ffffff"
        property var boxed: []
        property color boxIdle: "#000000"
        property color boxIdleText: "#ffffff"
        property color boxHot: "#ffffff"
        property color boxHotText: "#000000"
        signal activated()
        width: jt.implicitWidth
        height: jt.implicitHeight
        scale: ma.pressed ? 0.92 : (ma.containsMouse ? 1.1 : 1.0)
        Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }
        Jitter {
            id: jt
            text: b.label
            family: b.family
            pixelSize: b.size
            pad: 10
            seed: b.seed
            rings: 14
            color: ma.containsMouse ? b.hotFill : b.idleFill
            edgeColor: ma.containsMouse ? b.hotEdge : b.idleEdge
            edge: ma.containsMouse ? 6.5 : 3.2
            inv: b.boxed
            boxColor: ma.containsMouse ? b.boxHot : b.boxIdle
            boxTextColor: ma.containsMouse ? b.boxHotText : b.boxIdleText
            boxBorder: ma.containsMouse ? b.boxHotText : b.boxIdleText
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: b.activated()
        }
    }

    // ---------- Track change animation ----------
    Connections {
        target: root.player
        ignoreUnknownSignals: true
        function onTrackTitleChanged() { trackIn.restart(); }
    }
    ParallelAnimation {
        id: trackIn
        NumberAnimation { target: textGroup; property: "opacity"; from: 0; to: 1; duration: 380 }
        NumberAnimation { target: textGroup; property: "x"; from: 290; to: 235; duration: 380; easing.type: Easing.OutCubic }
        NumberAnimation { target: albumCard; property: "opacity"; from: 0; to: 1; duration: 380 }
    }

    // ================= Stage =================
    Item {
        id: stage
        width: root.designW
        height: root.designH
        scale: Math.min(root.width / root.designW, root.height / root.designH)
        transformOrigin: Item.TopLeft

        Item {
            id: canvas
            x: -25
            y: -85

            // ---------- Album card (right of Futaba, tucked behind the banner) ----------
            Item {
                id: albumCard
                x: 535; y: 138
                width: 213; height: 213
                rotation: 17
                scale: cardMa.containsMouse ? 1.05 : 1.0
                Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }
                Rectangle { x: 7; y: 7; width: parent.width; height: parent.height; color: root.cBlack }
                Rectangle { anchors.fill: parent; color: "#16181c" }
                Text {
                    anchors.centerIn: parent
                    visible: art.status !== Image.Ready
                    text: "NO ART"
                    font.family: fPersona.name
                    font.pixelSize: 36
                    color: "#555a63"
                }
                Image {
                    id: art
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    mipmap: true
                    asynchronous: true
                    sourceSize: Qt.size(900, 900)
                    source: root.player && root.player.trackArtUrl ? root.player.trackArtUrl : ""
                }
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.color: root.cInk
                    border.width: 2
                }
                MouseArea {
                    id: cardMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (root.player && root.player.canRaise) root.player.raise()
                }
            }

            // ---------- Banner (artwork + everything that sits on it) ----------
            Item {
                id: bannerBox
                x: 240; y: 240
                width: 1223; height: 344
                scale: 0.601
                rotation: 3
                transformOrigin: Item.TopLeft

                Image {
                    anchors.fill: parent
                    source: Qt.resolvedUrl("assets/banner.png")
                    smooth: true
                    mipmap: true
                    sourceSize: Qt.size(1223, 344)
                }

                // name tag text (the white tag built into the banner art)
                Jitter {
                    x: 268 - width / 2
                    y: 78 - height / 2
                    rotation: -15
                    text: "Futaba"
                    family: fUx.name
                    pixelSize: 40
                    bold: true
                    color: root.cBlack
                    jit: 0.9
                    seed: 3
                    inv: [2]
                    boxColor: root.cBlack
                    boxTextColor: "#ffffff"
                }

                // "Now playing ..." text
                Item {
                    id: textGroup
                    x: 235; y: 140
                    property real centerX: 385   // horizontal center of the text, inside the group
                    rotation: 2.5
                    transformOrigin: Item.TopLeft
                    Jitter {
                        x: textGroup.centerX - width / 2
                        text: root.player ? ("Now playing " + (root.player.trackTitle || "Unknown")) : "Nothing playing"
                        family: fUx.name
                        pixelSize: 32
                        bold: true
                        color: root.cText
                        jit: 0.55
                        seed: 7
                        maxChars: 34
                    }
                    Jitter {
                        x: textGroup.centerX - width / 2
                        y: 40
                        text: root.player ? ("by " + (root.player.trackArtist || "Unknown artist")) : "Open Spotify"
                        family: fUx.name
                        pixelSize: 32
                        bold: true
                        color: root.cText
                        jit: 0.55
                        seed: 11
                        maxChars: 34
                    }
                }

                // seek bar (click to seek)
                Item {
                    id: bar
                    x: 300; y: 240
                    width: 640; height: 12
                    rotation: 5.2
                    transformOrigin: Item.TopLeft
                    Rectangle {
                        anchors.fill: parent
                        color: root.cSeekTrack
                        border.color: "#ffffff"
                        border.width: 2
                    }
                    Rectangle {
                        x: 2; y: 2
                        height: parent.height - 4
                        width: root.len > 0 ? (parent.width - 4) * Math.min(1, root.pos / root.len) : 0
                        color: root.cSeekFill
                    }
                    Rectangle {
                        visible: seekMa.containsMouse && root.len > 0
                        x: Math.min(parent.width - width, Math.max(0, parent.width * Math.min(1, root.pos / Math.max(1, root.len)) - width / 2))
                        y: -8
                        width: 12; height: 28
                        color: "#ffffff"
                    }
                    MouseArea {
                        id: seekMa
                        anchors.fill: parent
                        anchors.topMargin: -18
                        anchors.bottomMargin: -18
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function (mouse) {
                            if (root.player && root.player.canSeek && root.len > 0)
                                root.player.position = root.len * Math.max(0, Math.min(1, mouse.x / width));
                        }
                    }
                }

                // time, inside the white wedge (click: elapsed <-> remaining)
                Jitter {
                    id: timeText
                    x: 1124 - width / 2
                    y: 188 - height / 2
                    rotation: -24
                    scale: timeMa.containsMouse ? 1.1 : 1.0
                    Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutBack } }
                    text: (root.showRemaining ? "-" : "") + root.fmt(root.showRemaining ? Math.max(0, root.len - root.pos) : root.pos)
                    family: fUx.name
                    pixelSize: 30
                    bold: true
                    color: root.cBlack
                    jit: 0.8
                    seed: 5
                }
                MouseArea {
                    id: timeMa
                    x: 1000; y: 100
                    width: 220; height: 170
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.showRemaining = !root.showRemaining
                }
            }

            // ---------- Futaba frame (on top of the banner tag) ----------
            Image {
                id: frame
                x: 33; y: 95
                width: 352; height: 272
                source: Qt.resolvedUrl("assets/futaba_frame.png")
                smooth: true
                mipmap: true
                sourceSize: Qt.size(1311, 1012)
                scale: frameMa.containsMouse ? 1.03 : 1.0
                Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }
                MouseArea {
                    id: frameMa
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }

            // ---------- Controls ----------
            Btn {
                x: 62; y: 452
                rotation: -4
                label: "PREV. SONG"
                boxed: [2, 8]
                family: fPersona.name
                seed: 1
                idleFill: root.cText
                idleEdge: root.cInk
                hotFill: root.cHotFill
                hotEdge: root.cHotEdge
                onActivated: if (root.player) root.player.previous()
            }
            Btn {
                x: 366; y: 488
                rotation: -6
                label: root.playing ? "PAUSE" : "PLAY"
                boxed: [1]
                family: fPersona.name
                seed: 2
                idleFill: root.cText
                idleEdge: root.cInk
                hotFill: root.cHotFill
                hotEdge: root.cHotEdge
                onActivated: if (root.player) root.player.togglePlaying()
            }
            Btn {
                x: 596; y: 512
                rotation: 2
                label: "NEXT SONG"
                boxed: [2, 6]
                family: fPersona.name
                seed: 4
                idleFill: root.cText
                idleEdge: root.cInk
                hotFill: root.cHotFill
                hotEdge: root.cHotEdge
                onActivated: if (root.player) root.player.next()
            }

            // ---------- Shuffle ----------
            Item {
                id: shuf
                x: 910; y: 548
                width: 74; height: 52
                readonly property bool active: root.player && root.player.shuffleSupported ? root.player.shuffle : false
                readonly property bool lit: shufMa.pressed || active
                readonly property color fillC: lit ? "#000000" : root.cText
                readonly property color edgeC: lit ? "#ffffff" : "#000000"
                readonly property real edgeW: lit ? 20 : 12
                // layers, bottom to top: black rim, rough edge (ring of offset copies), fill
                readonly property var layers: {
                    const L = [];
                    if (lit) L.push({ c: "#000000", w: 30, hw: 24, dx: 0, dy: 0 });
                    const ring = 8;
                    for (let k = 0; k < ring; k++) {
                        const a = k * 2 * Math.PI / ring;
                        L.push({ c: edgeC, w: edgeW, hw: edgeW - 3, dx: Math.cos(a) * 2.6, dy: Math.sin(a) * 2.6 });
                    }
                    L.push({ c: fillC, w: 7, hw: 0, dx: 0, dy: 0 });
                    return L;
                }
                opacity: root.player && root.player.shuffleSupported ? 1.0 : 0.4
                rotation: shufMa.pressed ? -11 : (shufMa.containsMouse ? -7 : -4)
                scale: shufMa.pressed ? 1.3 : (shufMa.containsMouse ? 1.18 : 1.0)
                Behavior on scale { NumberAnimation { duration: 170; easing.type: Easing.OutBack; easing.overshoot: 3 } }
                Behavior on rotation { NumberAnimation { duration: 170; easing.type: Easing.OutBack; easing.overshoot: 3 } }

                Repeater {
                    model: shuf.layers
                    Shape {
                        x: modelData.dx
                        y: modelData.dy
                        width: shuf.width
                        height: shuf.height
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: modelData.c; strokeWidth: modelData.w; fillColor: "transparent"
                            capStyle: ShapePath.SquareCap; joinStyle: ShapePath.MiterJoin; miterLimit: 8
                            startX: 2; startY: 10
                            PathLine { x: 22; y: 10 }
                            PathLine { x: 44; y: 40 }
                            PathLine { x: 56; y: 40 }
                        }
                        ShapePath {
                            strokeColor: modelData.c; strokeWidth: modelData.w; fillColor: "transparent"
                            capStyle: ShapePath.SquareCap; joinStyle: ShapePath.MiterJoin; miterLimit: 8
                            startX: 2; startY: 40
                            PathLine { x: 22; y: 40 }
                            PathLine { x: 44; y: 10 }
                            PathLine { x: 56; y: 10 }
                        }
                        ShapePath {
                            strokeColor: modelData.hw > 0 ? modelData.c : "transparent"; strokeWidth: modelData.hw
                            fillColor: modelData.c
                            joinStyle: ShapePath.MiterJoin; miterLimit: 8
                            startX: 54; startY: 0
                            PathLine { x: 72; y: 10 }
                            PathLine { x: 54; y: 20 }
                            PathLine { x: 54; y: 0 }
                        }
                        ShapePath {
                            strokeColor: modelData.hw > 0 ? modelData.c : "transparent"; strokeWidth: modelData.hw
                            fillColor: modelData.c
                            joinStyle: ShapePath.MiterJoin; miterLimit: 8
                            startX: 54; startY: 30
                            PathLine { x: 72; y: 40 }
                            PathLine { x: 54; y: 50 }
                            PathLine { x: 54; y: 30 }
                        }
                    }
                }
                MouseArea {
                    id: shufMa
                    anchors.fill: parent
                    anchors.margins: -10
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (root.player && root.player.shuffleSupported) root.player.shuffle = !root.player.shuffle
                }
            }
        }
    }
}

