import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui
import "Model.js" as Model

Ui.BarWidget {
    id: root
    moduleName: "local.sourceroll"
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    readonly property bool opened: panelLoader.item ? panelLoader.item.opened === true : false
    readonly property bool popoutSwitchClosing: panelLoader.item ? panelLoader.item.popoutSwitchClosing === true : false
    property var status: ({recording: false, state: "idle", videoSources: [], microphoneEnabled: true, desktopEnabled: false, elapsed: 0, peakDb: -90, error: "", output: ""})
    property bool unavailable: false
    property bool changing: false
    property real livePeak: -90
    property real desktopPeak: -90
    // The screens and cameras set up in the app, switched on or not.
    readonly property var sources: status.videoSources || []
    // The source the panel shows: the one picked there, else the first that is switched on.
    property string pickedSource: ""
    readonly property string monitorSource: Model.monitored(sources, pickedSource)
    // Its newest frame. Only this source is asked for, so the others stay off the wire.
    property string previewSource: ""
    onMonitorSourceChanged: { previewSource = ""; subscribePreview() }
    function subscribePreview() {
        if (!previewSocket.connected) return
        previewSocket.write(JSON.stringify({command: "subscribe-preview", source: monitorSource}) + "\n"); previewSocket.flush()
    }
    function open() { if (panelLoader.item) panelLoader.item.open() }
    function close() { if (panelLoader.item) panelLoader.item.close() }
    function toggle() { if (panelLoader.item) panelLoader.item.toggle() }
    function closeForPopoutSwitch() { if (panelLoader.item) panelLoader.item.closeForPopoutSwitch() }
    function injectPanel() {
        if (!panelLoader.item) return
        panelLoader.item.bar = root.bar
        panelLoader.item.anchorItem = button
        panelLoader.item.hostWidget = root
    }
    onBarChanged: injectPanel()
    Loader {
        id: panelLoader
        active: true; visible: false
        source: Qt.resolvedUrl("Panel.qml")
        onLoaded: { root.injectPanel(); Qt.callLater(root.injectPanel) }
    }
    onOpenedChanged: {
        if (opened) { action("ensure"); refresh() }
        else { previewSocket.connected = false; previewSource = "" }
    }
    function refresh() { if (!poll.running) poll.running = true }
    function action(command) {
        if (control.running || changing) return
        changing = true
        control.command = ["sourceroll", command, "--json"]
        control.running = true
    }
    function run(command) {
        if (control.running || root.status.recording) return
        root.changing = true
        control.command = command
        control.running = true
    }
    // Microphone and desktop audio.
    function setSource(key, enabled) { run(["sourceroll", "configure", "--" + key + "-enabled", enabled ? "true" : "false", "--json"]) }
    // A screen or camera. The model picks the command the running recorder understands.
    function setVideoSource(source, enabled) { const command = Model.switchCommand(source, enabled); if (command) run(command) }
    function chooseScreen(source) { const command = Model.chooseCommand(source); if (command) run(command) }
    Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }
    Socket {
        id: previewSocket
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/sourceroll.sock"
        onConnectionStateChanged: {
            if (connected) root.subscribePreview()
            else root.previewSource = ""
        }
        parser: SplitParser {
            splitMarker: "\n"
            onRead: function(data) {
                // An older app sends every source; keep only the one on show.
                const frame = root.monitorSource === "" ? null : Model.parsePreview(String(data), root.monitorSource)
                if (frame !== null) root.previewSource = frame
            }
        }
    }
    Socket {
        id: meterSocket
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/sourceroll.sock"
        onConnectionStateChanged: {
            if (connected) { write('{"command":"subscribe-meter"}\n'); flush() }
            else { root.livePeak = -90; root.desktopPeak = -90 }
        }
        parser: SplitParser {
            splitMarker: "\n"
            onRead: function(data) { const peak = Model.parseMeter(String(data)); if (peak !== null) root.livePeak = peak; const desktop = Model.parseDesktopMeter(String(data)); if (desktop !== null) root.desktopPeak = desktop }
        }
    }
    Timer {
        interval: 2000; running: true; repeat: true; triggeredOnStart: true
        onTriggered: {
            if (root.status.running && !meterSocket.connected) meterSocket.connected = true
            if (root.opened && root.status.running && !previewSocket.connected) previewSocket.connected = true
        }
    }
    Timer { interval: root.unavailable ? 15000 : root.opened ? 400 : 1000; running: true; repeat: true; triggeredOnStart: true; onTriggered: root.refresh() }
    Process {
        id: poll
        property bool received: false
        command: ["sourceroll", "status", "--json"]
        onRunningChanged: { if (running) received = false; else if (!received) root.unavailable = true }
        stdout: StdioCollector {
            onStreamFinished: {
                let value = Model.parseStatus(text)
                poll.received = true
                if (value) {
                    root.status = value; root.unavailable = false
                    if ((Model.find(value.videoSources, root.monitorSource).previewState || "inactive") === "inactive") root.previewSource = ""
                    if (value.running && !meterSocket.connected) meterSocket.connected = true
                    if (!value.running && meterSocket.connected) meterSocket.connected = false
                    if (root.opened && value.running && !previewSocket.connected) previewSocket.connected = true
                    if (!value.running && previewSocket.connected) previewSocket.connected = false
                }
                else root.unavailable = true
            }
        }
        onExited: function(code) { if (code !== 0) root.unavailable = true }
    }
    Process {
        id: control
        onRunningChanged: if (!running) root.changing = false
        stdout: StdioCollector { onStreamFinished: { let value = Model.parseStatus(text); if (value) root.status = value } }
        onExited: function(code) { root.changing = false; if (code !== 0) root.unavailable = true; root.refresh() }
    }
    Ui.BarIconButton {
        id: button
        anchors.fill: parent
        bar: root.bar
        text: "󰄀"
        active: root.status.recording
        slotSize: root.status.recording && !root.vertical ? Style.space(98) : Style.bar.iconSlot
        labelText: root.status.recording && !root.vertical ? Model.duration(root.status.elapsed) : ""
        tooltipText: Model.safeText([strings.t("bar.tooltip"), root.status.warning || "", root.status.displayWarning || ""].filter(function(s) { return s !== "" }).join("\n"))
        onPressed: function(b) {
            if (b === Qt.MiddleButton) root.action("toggle")
            else if (b === Qt.RightButton) root.action("open")
            else root.toggle()
        }
    }
}
