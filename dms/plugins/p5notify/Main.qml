import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Common
import qs.Services
import qs.Modules.Plugins

// Persona 5 notification banners as a DMS daemon plugin.
//
// DMS already owns org.freedesktop.Notifications, so this plugin does NOT run a
// NotificationServer. It only watches NotificationService.historyList and draws
// new notifications as P5 banners in its own overlay window.
//
// Do-not-disturb (to hide DMS's stock popups) is deliberately NOT touched here.
// The profile switcher owns it: persona -> DND on, saber/elden-ring -> DND off.
// Keeping it in one place stops the plugin and the script from fighting.
//
// Persona-only gate: the profile switcher writes the active profile slug to
// ~/.config/dms/p5notify-profile. Banners are drawn ONLY while that file says
// "persona", so even if the plugin stays loaded on another profile it stays silent.
PluginComponent {
    id: root

    // ---- Look & feel (same values as the standalone shell.qml) --------------
    // Banner art used (file inside the plugin folder).
    readonly property string bannerFile: "banner2.png"
    readonly property real bannerW: 700
    readonly property int showSeconds: 6
    readonly property int maxBanners: 5

    // Text placement is by CENTER point (fractions of the banner image).
    // banner2.png: title in the white bubble at the top right, body in the black panel.
    // (Old banner.png values: tagCX 0.345, tagCY 0.28, tagW 0.34, tagRot -9.5,
    //  bodyCX 0.565, bodyCY 0.605, bodyW 0.56, bodyH 0.34, bodyRot -4.5)
    readonly property real tagCX: 0.789
    readonly property real tagCY: 0.268
    readonly property real tagW: 0.15
    readonly property real tagRot: 15.7

    readonly property real bodyCX: 0.536
    readonly property real bodyCY: 0.538
    readonly property real bodyW: 0.54
    readonly property real bodyH: 0.30
    readonly property real bodyRot: -1

    readonly property int titleSize: 19
    readonly property int textSize: 19

    // ---- State ---------------------------------------------------------------
    property var seen: ({})
    property int seenCount: 0
    property bool personaActive: false

    property double startedAt: Date.now()

    FileView {
        id: profileFile
        path: Quickshell.env("HOME") + "/.config/dms/p5notify-profile"
        blockLoading: true
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.refreshActive()
    }

    // Never flip to "inactive" just because a read came back empty mid-reload:
    // that used to drop notifications that arrived at that exact moment.
    function refreshActive() {
        var raw = "";
        try {
            raw = String(profileFile.text() || "").trim();
        } catch (e) {
            return;
        }
        if (raw === "")
            return;
        var active = raw === "persona";
        if (active !== root.personaActive) {
            root.personaActive = active;
            console.log("[p5notify] profile=" + raw + " active=" + active);
            if (!active)
                banners.clear();
        }
    }

    FontLoader {
        id: fUx
        source: Qt.resolvedUrl("fonts/PersonifiedUXRegular.ttf")
    }

    FontLoader {
        id: fP5
        source: Qt.resolvedUrl("fonts/Persona5main.ttf")
    }

    // Persona5main is an ALL-CAPS display font with very wide letters, so it is used
    // for the title only. Set this to true to use it for the body text as well.
    readonly property bool p5FontForBody: false
    readonly property bool p5Ready: fP5.status === FontLoader.Ready

    readonly property string titleFont: p5Ready ? fP5.name : fUx.name
    readonly property string bodyFont: (p5FontForBody && p5Ready) ? fP5.name : fUx.name
    // The P5 font is wide, so it needs a smaller size and tighter spacing to fit the strip.
    readonly property int titleFontSize: p5Ready ? 12 : 15
    readonly property real titleSpacing: p5Ready ? -2.5 : 0

    ListModel {
        id: banners
    }

    Connections {
        target: NotificationService

        function onHistoryListChanged() {
            root.refreshActive();
            root.scan(false);
        }
    }

    // Safety net in case the change signal differs on your DMS build.
    // Two passes: a fast one (shortest flash) and a later one in case DMS drew
    // its popup after the first dismiss.
    Timer {
        id: dismissStockTimer
        property int pass: 0
        interval: 30
        repeat: true
        onTriggered: {
            Quickshell.execDetached(["dms", "ipc", "call", "notifications", "dismissAllPopups"]);
            pass++;
            if (pass === 1) {
                interval = 250;
            } else {
                stop();
                pass = 0;
                interval = 30;
            }
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            root.refreshActive();
            root.scan(false);
        }
    }

    // ---- History watching ----------------------------------------------------
    function entryAt(list, i) {
        if (list.get !== undefined && list.count !== undefined)
            return list.get(i);
        return list[i];
    }

    function listLength(list) {
        if (list.count !== undefined)
            return list.count;
        return list.length || 0;
    }

    function keyOf(n) {
        return String(n.id !== undefined ? n.id : "") + "|" + String(n.timestamp) + "|" + String(n.summary) + "|" + String(n.body);
    }

    function ageMs(n) {
        var t = n.timestamp;
        if (t instanceof Date)
            t = t.getTime();
        t = Number(t);
        if (isNaN(t) || t <= 0)
            return 0;
        if (t < 1e11)
            t = t * 1000; // seconds -> ms
        return Date.now() - t;
    }

    function scan(initial) {
        var list = NotificationService.historyList;
        if (!list)
            return;
        var len = listLength(list);
        for (var i = 0; i < len; ++i) {
            var n = entryAt(list, i);
            if (!n)
                continue;
            var k = keyOf(n);
            if (root.seen[k])
                continue;
            root.seen[k] = true;
            root.seenCount++;
            // Don't replay history on startup / late history loading.
            var settling = (Date.now() - root.startedAt) < 10000;
            if (initial || !root.personaActive || (settling && ageMs(n) > 15000)) {
                console.log("[p5notify] skipped: initial=" + initial
                    + " active=" + root.personaActive + " settling=" + settling
                    + " ageMs=" + ageMs(n));
                continue;
            }
            console.log("[p5notify] showing: " + String(n.summary));
            root.show(n);
        }
        // Keep the seen-set from growing forever.
        if (root.seenCount > 1000) {
            var fresh = {};
            for (var j = 0; j < len; ++j) {
                var e = entryAt(list, j);
                if (e)
                    fresh[keyOf(e)] = true;
            }
            root.seen = fresh;
            root.seenCount = len;
        }
    }

    // Regex-free on purpose: some Qt/QML parser builds choke on regex literals
    // ("Unterminated regular expression literal"), so use plain string ops.
    function stripMarkup(s) {
        var str = String(s || "");
        // drop <tags>
        var out = "";
        var depth = 0;
        for (var i = 0; i < str.length; ++i) {
            var c = str.charAt(i);
            if (c === "<") {
                depth++;
            } else if (c === ">" && depth > 0) {
                depth--;
            } else if (depth === 0) {
                out += c;
            }
        }
        // if a "<" never closed, keep the text instead of losing it
        if (depth > 0)
            out = str;
        return out
            .split("&lt;").join("<")
            .split("&gt;").join(">")
            .split("&quot;").join("\"")
            .split("&#39;").join("'")
            .split("&amp;").join("&");
    }

    // ---- Sound (only ever plays from show(), i.e. only in the persona profile) ----
    property bool soundEnabled: true
    readonly property string soundPath:
        decodeURIComponent(Qt.resolvedUrl("sounds/papersound.wav").toString().replace("file://", ""))

    function playSound() {
        if (!soundEnabled)
            return;
        // PipeWire first, then PulseAudio, then ALSA.
        Quickshell.execDetached(["sh", "-c",
            "pw-play \"$1\" 2>/dev/null || paplay \"$1\" 2>/dev/null || aplay -q \"$1\" 2>/dev/null",
            "sh", soundPath]);
    }

    function show(n) {
        var summary = stripMarkup(n.summary);
        var appName = stripMarkup(n.appName);
        var tag = summary !== "" ? summary : (appName !== "" ? appName : "Notification");

        var timeoutMs = root.showSeconds * 1000;
        var t = Number(n.expireTimeout);
        if (!isNaN(t) && t > 0) {
            // Accept either seconds or milliseconds from the service.
            timeoutMs = t < 1000 ? t * 1000 : t;
            timeoutMs = Math.max(3000, Math.min(15000, timeoutMs));
        }

        // Quickshell urgency enum: Low=0, Normal=1, Critical=2
        var critical = Number(n.urgency) === 2;

        if (banners.count >= root.maxBanners)
            banners.remove(banners.count - 1);

        banners.insert(0, {
            "key": root.keyOf(n),
            "tag": tag,
            "body": stripMarkup(n.body),
            "timeoutMs": timeoutMs,
            "critical": critical
        });

        playSound();

        // DMS lets critical notifications through do-not-disturb, so its stock
        // popup would show up behind the banner. Clear stock popups right after.
        // (History is untouched; only the on-screen popup is dismissed.)
        if (critical)
            dismissStockTimer.restart();
    }

    function removeBanner(k) {
        for (var i = 0; i < banners.count; ++i) {
            if (banners.get(i).key === k) {
                banners.remove(i);
                return;
            }
        }
    }

    Component.onCompleted: {
        refreshActive();
        scan(true);
    }

    // ---- UI --------------------------------------------------------------------
    PanelWindow {
        id: win
        // Kept mapped (transparent, click-through when empty) so the overlay
        // doesn't have to be re-created after idle.
        visible: true

        anchors {
            top: true
            right: true
        }

        margins {
            top: 14
            right: 14
        }

        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "dms-plugin:p5notify"
        color: "transparent"

        implicitWidth: root.bannerW + 40
        implicitHeight: Math.max(1, col.implicitHeight + 30)
        mask: Region {
            item: col
        }

        Column {
            id: col
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 10
            spacing: -5

            Repeater {
                model: banners

                delegate: Item {
                    id: card

                    required property string key
                    required property string tag
                    required property string body
                    required property int timeoutMs
                    required property bool critical

                    // One random letter of the title gets the reversed black box.
                    readonly property int boxIndex: {
                        var candidates = [];
                        for (var i = 0; i < tag.length; ++i) {
                            var code = tag.charCodeAt(i);
                            if ((code >= 48 && code <= 57)
                                || (code >= 65 && code <= 90)
                                || (code >= 97 && code <= 122))
                                candidates.push(i);
                        }
                        return candidates.length > 0
                            ? candidates[Math.floor(Math.random() * candidates.length)]
                            : 0;
                    }

                    width: root.bannerW
                    height: img.height

                    transform: [
                        Translate {
                            id: tr
                            x: 120
                        },
                        Rotation {
                            id: wob
                            angle: 0
                            origin.x: card.width * 0.7
                            origin.y: card.height / 2
                        }
                    ]

                    opacity: 0

                    Component.onCompleted: enter.start()

                    // Slide in, then wobble like a sticker slapped onto the screen.
                    SequentialAnimation {
                        id: enter

                        ParallelAnimation {
                            NumberAnimation {
                                target: tr
                                property: "x"
                                to: 0
                                duration: 300
                                easing.type: Easing.OutBack
                            }

                            NumberAnimation {
                                target: card
                                property: "opacity"
                                to: 1
                                duration: 180
                            }
                        }

                        NumberAnimation { target: wob; property: "angle"; to: -3.2; duration: 70; easing.type: Easing.InOutSine }
                        NumberAnimation { target: wob; property: "angle"; to: 2.6; duration: 80; easing.type: Easing.InOutSine }
                        NumberAnimation { target: wob; property: "angle"; to: -1.8; duration: 80; easing.type: Easing.InOutSine }
                        NumberAnimation { target: wob; property: "angle"; to: 1.1; duration: 80; easing.type: Easing.InOutSine }
                        NumberAnimation { target: wob; property: "angle"; to: -0.5; duration: 80; easing.type: Easing.InOutSine }
                        NumberAnimation { target: wob; property: "angle"; to: 0; duration: 80; easing.type: Easing.InOutSine }
                    }

                    Image {
                        id: img
                        width: parent.width
                        height: implicitWidth > 0
                            ? width * implicitHeight / implicitWidth : 160
                        source: Qt.resolvedUrl(root.bannerFile)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }

                    // White upper strip: title with one reversed boxed letter, centered.
                    Item {
                        id: tagBox

                        width: img.width * root.tagW
                        height: 26
                        x: img.width * root.tagCX - width / 2
                        y: img.height * root.tagCY - height / 2
                        rotation: root.tagRot
                        clip: true

                        Row {
                            id: tagRow
                            anchors.horizontalCenter: parent.horizontalCenter
                            height: parent.height
                            spacing: 0

                            Text {
                                id: tagBefore
                                text: card.tag.slice(0, card.boxIndex)
                                width: Math.min(implicitWidth, tagBox.width * 0.6)
                                height: tagRow.height
                                verticalAlignment: Text.AlignVCenter
                                font.family: root.titleFont
                                font.pixelSize: root.titleFontSize
                                font.letterSpacing: root.titleSpacing
                                font.bold: root.p5Ready ? false : true
                                color: "#000000"
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }

                            Rectangle {
                                id: boxedLetter
                                width: boxedText.implicitWidth + 6
                                height: 21
                                anchors.verticalCenter: parent.verticalCenter
                                color: "#000000"

                                Text {
                                    id: boxedText
                                    anchors.centerIn: parent
                                    text: card.tag.length > 0
                                        ? card.tag.charAt(card.boxIndex) : "N"
                                    font.family: root.titleFont
                                    font.pixelSize: root.titleFontSize + 1
                                    font.letterSpacing: root.titleSpacing
                                    font.bold: root.p5Ready ? false : true
                                    color: "#ffffff"
                                    textFormat: Text.PlainText
                                }
                            }

                            Text {
                                id: tagAfter
                                text: card.tag.slice(card.boxIndex + 1)
                                width: Math.min(implicitWidth,
                                    Math.max(0, tagBox.width - tagBefore.width - boxedLetter.width))
                                height: tagRow.height
                                verticalAlignment: Text.AlignVCenter
                                font.family: root.titleFont
                                font.pixelSize: root.titleFontSize
                                font.letterSpacing: root.titleSpacing
                                font.bold: root.p5Ready ? false : true
                                color: "#000000"
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }
                    }

                    // Black lower panel: body in white, centered both ways.
                    Item {
                        width: img.width * root.bodyW
                        height: img.height * root.bodyH
                        x: img.width * root.bodyCX - width / 2
                        y: img.height * root.bodyCY - height / 2
                        rotation: root.bodyRot
                        clip: true

                        Text {
                            anchors.fill: parent
                            visible: text !== ""
                            text: card.body
                            font.family: root.bodyFont
                            font.pixelSize: root.textSize
                            color: "#ffffff"
                            textFormat: Text.PlainText
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                    }

                    Timer {
                        interval: card.timeoutMs
                        running: !card.critical && !ma.containsMouse
                        onTriggered: root.removeBanner(card.key)
                    }

                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.removeBanner(card.key)
                    }
                }
            }
        }
    }
}
