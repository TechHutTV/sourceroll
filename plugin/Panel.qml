pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui
import "Model.js" as Model

Ui.Panel {
    id: root
    moduleName: "local.sourceroll"
    manageIpc: false
    property var anchorItem: null
    property var hostWidget: null
    readonly property var status: hostWidget ? hostWidget.status : ({recording: false, state: "idle", videoSources: [], microphoneEnabled: true, desktopEnabled: false, elapsed: 0, error: "", output: ""})
    readonly property bool changing: hostWidget ? hostWidget.changing : false
    readonly property bool unavailable: hostWidget ? hostWidget.unavailable : false
    readonly property real livePeak: hostWidget ? hostWidget.livePeak : -90
    readonly property real desktopPeak: hostWidget ? hostWidget.desktopPeak : -90
    // The screens and cameras set up in the app, and the one on show here.
    readonly property var sources: status.videoSources || []
    readonly property string monitorSource: hostWidget ? hostWidget.monitorSource : ""
    readonly property var monitored: Model.find(sources, monitorSource)
    readonly property bool monitoringScreen: monitored.kind === "screen"
    readonly property int switchedOn: { let count = 0; for (let i = 0; i < sources.length; ++i) if (sources[i].enabled) ++count; return count }
    readonly property string previewSource: hostWidget ? hostWidget.previewSource : ""
    readonly property string feedState: monitored.previewState || "inactive"
    readonly property bool previewStale: feedState === "stalled" || feedState === "error"
    readonly property bool sourceRecording: monitored.recording === true
    // A source's own name; an older app reports none, so fall back to its kind.
    function sourceName(source) { return source.name || strings.t(source.kind === "screen" ? "source.screen" : "source.camera") }
    Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }
    function switchPanel(direction) {
        if (bar && typeof bar.switchPanelFrom === "function") return bar.switchPanelFrom(root.hostWidget || root, direction)
        return false
    }
    Ui.KeyboardPanel {
        id: surface
        anchorItem: root.anchorItem
        bar: root.bar
        owner: root.hostWidget || root
        open: root.opened
        focusTarget: keyCatcher
        contentWidth: surface.fittedContentWidth(Style.space(340))
        contentHeight: surface.fittedContentHeight(panelContent.implicitHeight)
        Ui.PanelKeyCatcher {
            id: keyCatcher
            anchors.fill: parent
            onCloseRequested: root.close()
            onTabRequested: function(direction) { root.switchPanel(direction) }
            ColumnLayout {
                id: panelContent
                width: parent.width
                spacing: Style.space(12)
                Text { textFormat: Text.PlainText; text: "Source Roll"; color: Commons.Color.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body + 3; font.bold: true }
                Text { text: strings.t("monitor"); textFormat: Text.PlainText; visible: root.switchedOn > 1; color: Commons.Color.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body }
                Flow {
                    // One tab per source that is switched on; they only pick what to watch.
                    Layout.fillWidth: true; spacing: Style.space(6)
                    visible: root.switchedOn > 1
                    Repeater {
                        model: root.sources.length
                        Ui.Button {
                            required property int index
                            readonly property var source: root.sources[index] || ({})
                            visible: source.enabled === true
                            width: Math.min(implicitWidth, parent.width)
                            text: root.sourceName(source); selected: source.id === root.monitorSource
                            onClicked: root.hostWidget.pickedSource = source.id
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true; textFormat: Text.PlainText; elide: Text.ElideRight
                    visible: root.monitorSource !== ""
                    text: root.sourceName(root.monitored) + (root.previewStale ? " · " + strings.t("preview.stale") : root.sourceRecording ? " · REC" : root.feedState === "live" ? " · LIVE" : "")
                    color: root.previewStale || root.sourceRecording ? Commons.Color.accent : Commons.Color.foreground
                    font.family: Style.font.family; font.pixelSize: Style.font.body
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: width * 9 / 16
                    color: Commons.Color.background
                    border.width: 1; border.color: Qt.alpha(Commons.Color.foreground, 0.2)
                    radius: Style.cornerRadius; clip: true
                    Image {
                        anchors.fill: parent; anchors.margins: 1
                        source: root.previewSource; cache: false
                        visible: root.previewSource !== "" && (root.feedState === "live" || root.previewStale)
                        asynchronous: true; retainWhileLoading: true
                        fillMode: Image.PreserveAspectFit
                    }
                    MouseArea {
                        // An empty pane leads to the fix: the picker for a picked screen, the app for anything else.
                        anchors.fill: parent
                        enabled: root.previewSource === "" && !root.status.recording && !root.status.screenSelecting
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (root.monitoringScreen && root.monitored.picker) root.hostWidget.chooseScreen(root.monitored)
                            else { root.hostWidget.action("open"); root.hostWidget.close() }
                        }
                    }
                    Text {
                        anchors.centerIn: parent; width: parent.width - 24
                        visible: root.previewSource === "" || root.previewStale || root.feedState === "waiting" || root.feedState === "inactive"; textFormat: Text.PlainText
                        text: root.previewStale ? strings.t("preview.stalled")
                            : root.monitorSource === "" ? strings.t("preview.none")
                            : !root.monitoringScreen ? (root.status.error ? strings.t("preview.unavailable") : strings.t("preview.opening"))
                            : root.monitored.selecting ? strings.t("choose.screen")
                            : root.status.recording || !root.monitored.picker ? strings.t("preview.screen.waiting") : strings.t("preview.screen.idle")
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        style: Text.Outline; styleColor: Commons.Color.background
                        color: Commons.Color.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body
                    }
                }
                Ui.Button {
                    Layout.fillWidth: true
                    visible: root.monitoringScreen
                    text: strings.t("choose.screen.action")
                    enabled: !root.changing && !root.status.recording && !root.status.screenSelecting
                    onClicked: root.hostWidget.chooseScreen(root.monitored)
                }
                Text {
                    Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    text: root.unavailable ? strings.t("unavailable") : (root.status.state === "waiting-screen" || root.status.screenSelecting) ? strings.t("choose.screen") : root.status.state === "validating" ? strings.t("validating") : root.status.recording ? strings.t("recording").arg(Model.duration(root.status.elapsed)) : strings.t("idle")
                    color: root.status.recording ? Commons.Color.urgent : Commons.Color.foreground
                    font.family: Style.font.family; font.pixelSize: Style.font.body
                }
                GridLayout {
                    // The switches choose what a take saves: every screen and camera, then the two audio sources.
                    Layout.fillWidth: true; columns: 2
                    Repeater {
                        model: root.sources.length
                        Ui.Button {
                            required property int index
                            readonly property var source: root.sources[index] || ({})
                            Layout.fillWidth: true
                            text: root.sourceName(source); selected: source.enabled === true
                            enabled: !root.changing && !root.status.recording && !root.status.screenSelecting
                            onClicked: root.hostWidget.setVideoSource(source, !source.enabled)
                        }
                    }
                    Ui.Button { Layout.fillWidth: true; text: strings.t("source.microphone"); selected: root.status.microphoneEnabled; enabled: !root.changing && !root.status.recording; onClicked: root.hostWidget.setSource("microphone", !root.status.microphoneEnabled) }
                    Ui.Button { Layout.fillWidth: true; text: strings.t("source.desktop"); selected: root.status.desktopEnabled; enabled: !root.changing && !root.status.recording; onClicked: root.hostWidget.setSource("desktop", !root.status.desktopEnabled) }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: Style.space(5); color: Qt.alpha(Commons.Color.foreground, 0.15)
                    Rectangle { width: parent.width * Math.max(0, (root.livePeak + 60) / 60); height: parent.height; color: root.livePeak > -3 ? Commons.Color.urgent : Commons.Color.accent }
                }
                Rectangle {
                    visible: root.status.desktopEnabled
                    Layout.fillWidth: true; implicitHeight: Style.space(5); color: Qt.alpha(Commons.Color.foreground, 0.15)
                    Rectangle { width: parent.width * Math.max(0, (root.desktopPeak + 60) / 60); height: parent.height; color: root.desktopPeak > -3 ? Commons.Color.urgent : Commons.Color.accent }
                }
                Text {
                    Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    visible: root.status.error !== ""; text: root.status.error
                    color: Commons.Color.urgent; font.family: Style.font.family; font.pixelSize: Style.font.body
                }
                Text {
                    Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    text: [root.status.warning || "", root.status.displayWarning || ""].filter(function(s) { return s !== "" }).join("\n")
                    visible: text !== ""; color: Commons.Color.accent
                    font.family: Style.font.family; font.pixelSize: Style.font.body
                }
                Text {
                    Layout.fillWidth: true; textFormat: Text.PlainText; elide: Text.ElideMiddle
                    visible: root.status.output !== ""; text: root.status.output
                    color: Commons.Color.foreground; opacity: 0.6; font.family: Style.font.family; font.pixelSize: Style.font.body - 2
                }
                Ui.Button {
                    Layout.fillWidth: true; text: strings.t(root.status.recording ? "stop" : "start")
                    bordered: true; enabled: !root.changing && root.status.state !== "stopping" && !root.status.screenSelecting
                    onClicked: root.hostWidget.action(root.status.recording ? "stop" : "start")
                }
                Ui.Button { Layout.fillWidth: true; text: strings.t("open"); bordered: true; onClicked: { root.hostWidget.action("open"); root.hostWidget.close() } }
            }
        }
    }
}
