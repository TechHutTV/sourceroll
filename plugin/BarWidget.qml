import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui
import "Model.js" as Model

Ui.BarWidget {
    id: root
    moduleName: "local.multitake"
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    readonly property bool opened: panelLoader.item ? panelLoader.item.opened === true : false
    readonly property bool popoutSwitchClosing: panelLoader.item ? panelLoader.item.popoutSwitchClosing === true : false
    property var status: ({recording: false, state: "idle", cameraEnabled: true, screenEnabled: false, microphoneEnabled: true, desktopEnabled: false, elapsed: 0, peakDb: -90, error: "", output: ""})
    property bool unavailable: false
    property bool changing: false
    property real livePeak: -90
    property real desktopPeak: -90
    property string previewSource: ""
    property string screenPreviewSource: ""
    property string monitorSource: status.screenEnabled ? "screen" : "camera"
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
        else { previewSocket.connected = false; previewSource = ""; screenPreviewSource = "" }
    }
    function refresh() { if (!poll.running) poll.running = true }
    function action(command) {
        if (control.running || changing) return
        changing = true
        control.command = ["multitake", command, "--json"]
        control.running = true
    }
    function setSource(key, enabled) {
        if (control.running || root.status.recording) return
        root.changing = true
        control.command = key === "screen" && enabled ? ["multitake", "choose-screen", "--json"] : ["multitake", "configure", "--" + key + "-enabled", enabled ? "true" : "false", "--json"]
        control.running = true
    }
    Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }
    Socket {
        id: previewSocket
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/multitake.sock"
        onConnectionStateChanged: {
            if (connected) { write('{"command":"subscribe-preview"}\n'); flush() }
            else { root.previewSource = ""; root.screenPreviewSource = "" }
        }
        parser: SplitParser {
            splitMarker: "\n"
            onRead: function(data) {
                const camera = Model.parsePreview(String(data), "camera")
                const screen = Model.parsePreview(String(data), "screen")
                if (camera !== null) root.previewSource = camera
                if (screen !== null) root.screenPreviewSource = screen
            }
        }
    }
    Socket {
        id: meterSocket
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/multitake.sock"
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
        command: ["multitake", "status", "--json"]
        onRunningChanged: { if (running) received = false; else if (!received) root.unavailable = true }
        stdout: StdioCollector {
            onStreamFinished: {
                let value = Model.parseStatus(text)
                poll.received = true
                if (value) {
                    root.status = value; root.unavailable = false
                    if (!value.cameraEnabled) root.previewSource = ""
                    if (value.screenPreviewState === "inactive") root.screenPreviewSource = ""
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
        tooltipText: strings.t("bar.tooltip")
        onPressed: function(b) {
            if (b === Qt.MiddleButton) root.action("toggle")
            else if (b === Qt.RightButton) root.action("open")
            else root.toggle()
        }
    }
}
