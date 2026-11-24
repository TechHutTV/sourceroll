import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "Model.js" as Model

BarWidget {
    id: root
    moduleName: "local.source-record"
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight
    property bool opened: false
    property var status: ({recording: false, state: "idle", elapsed: 0, peakDb: -90, error: "", output: ""})
    property bool unavailable: false
    property bool changing: false
    property real livePeak: -90
    function open() { opened = true; refresh() }
    function close() { opened = false }
    function refresh() { if (!poll.running) poll.running = true }
    function action(command) {
        if (control.running || changing) return
        changing = true
        control.command = ["source-record", command, "--json"]
        control.running = true
    }
    Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }
    Socket {
        id: meterSocket
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/source-record.sock"
        onConnectionStateChanged: {
            if (connected) { write('{"command":"subscribe-meter"}\n'); flush() }
            else root.livePeak = -90
        }
        parser: SplitParser {
            splitMarker: "\n"
            onRead: function(data) { const peak = Model.parseMeter(String(data)); if (peak !== null) root.livePeak = peak }
        }
    }
    Timer { interval: 2000; running: true; repeat: true; triggeredOnStart: true; onTriggered: if (root.status.running && !meterSocket.connected) meterSocket.connected = true }
    Timer { interval: root.unavailable ? 15000 : root.opened ? 400 : 1000; running: true; repeat: true; triggeredOnStart: true; onTriggered: root.refresh() }
    Process {
        id: poll
        property bool received: false
        command: ["source-record", "status", "--json"]
        onRunningChanged: { if (running) received = false; else if (!received) root.unavailable = true }
        stdout: StdioCollector {
            onStreamFinished: {
                let value = Model.parseStatus(text)
                poll.received = true
                if (value) {
                    root.status = value; root.unavailable = false
                    if (value.running && !meterSocket.connected) meterSocket.connected = true
                    if (!value.running && meterSocket.connected) meterSocket.connected = false
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
    BarIconButton {
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
            else root.opened ? root.close() : root.open()
        }
    }
    KeyboardPanel {
        anchorItem: button
        bar: root.bar
        owner: root
        open: root.opened
        contentWidth: Style.space(340)
        contentHeight: content.implicitHeight + padding * 2
        ColumnLayout {
            id: content
            width: parent.width
            spacing: Style.space(12)
            Text { textFormat: Text.PlainText; text: "Source Record"; color: Commons.Color.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body + 3; font.bold: true }
            Text {
                Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                text: root.unavailable ? strings.t("unavailable") : root.status.recording ? strings.t("recording").arg(Model.duration(root.status.elapsed)) : strings.t("idle")
                color: root.status.recording ? Commons.Color.urgent : Commons.Color.foreground
                font.family: Style.font.family; font.pixelSize: Style.font.body
            }
            Rectangle {
                Layout.fillWidth: true; height: Style.space(5); color: Commons.Color.foreground; opacity: 0.8
                Rectangle { width: parent.width * Math.max(0, (root.livePeak + 60) / 60); height: parent.height; color: root.livePeak > -3 ? Commons.Color.urgent : Commons.Color.accent }
            }
            Text {
                Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                visible: root.status.error !== ""; text: root.status.error
                color: Commons.Color.urgent; font.family: Style.font.family; font.pixelSize: Style.font.body
            }
            Text {
                Layout.fillWidth: true; textFormat: Text.PlainText; elide: Text.ElideMiddle
                visible: root.status.output !== ""; text: root.status.output
                color: Commons.Color.foreground; opacity: 0.6; font.family: Style.font.family; font.pixelSize: Style.font.body - 2
            }
            Button {
                Layout.fillWidth: true; text: strings.t(root.status.recording ? "stop" : "start")
                bordered: true; enabled: !root.changing && root.status.state !== "stopping"
                onClicked: root.action(root.status.recording ? "stop" : "start")
            }
            Button { Layout.fillWidth: true; text: strings.t("open"); bordered: true; onClicked: { root.action("open"); root.close() } }
            Text { Layout.fillWidth: true; textFormat: Text.PlainText; text: strings.t("independent"); wrapMode: Text.Wrap; color: Commons.Color.foreground; opacity: 0.6; font.family: Style.font.family; font.pixelSize: Style.font.body - 2 }
        }
    }
}
