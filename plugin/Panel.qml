import QtQuick
import QtQuick.Layouts
import qs.Commons
import qs.Commons as Commons
import qs.Ui as Ui
import "Model.js" as Model

Ui.Panel {
    id: root
    moduleName: "local.multitake"
    manageIpc: false
    property var anchorItem: null
    property var hostWidget: null
    readonly property var status: hostWidget ? hostWidget.status : ({recording: false, state: "idle", cameraEnabled: true, screenEnabled: false, microphoneEnabled: true, desktopEnabled: false, elapsed: 0, error: "", output: ""})
    readonly property bool changing: hostWidget ? hostWidget.changing : false
    readonly property bool unavailable: hostWidget ? hostWidget.unavailable : false
    readonly property real livePeak: hostWidget ? hostWidget.livePeak : -90
    readonly property real desktopPeak: hostWidget ? hostWidget.desktopPeak : -90
    readonly property string previewSource: hostWidget ? hostWidget.previewSource : ""
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
                Text { textFormat: Text.PlainText; text: "Multitake"; color: Commons.Color.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body + 3; font.bold: true }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: width * 9 / 16
                    color: Commons.Color.background
                    border.width: 1; border.color: Qt.alpha(Commons.Color.foreground, 0.2)
                    radius: Style.cornerRadius; clip: true
                    Image {
                        anchors.fill: parent; anchors.margins: 1
                        source: root.previewSource; cache: false
                        asynchronous: true; retainWhileLoading: true
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        anchors.centerIn: parent; width: parent.width - 24
                        visible: root.previewSource === ""; textFormat: Text.PlainText
                        text: !root.status.cameraEnabled ? strings.t("preview.off") : root.status.error ? strings.t("preview.unavailable") : strings.t("preview.opening")
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        color: Commons.Color.foreground; font.family: Style.font.family; font.pixelSize: Style.font.body
                    }
                }
                Text {
                    Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
                    text: root.unavailable ? strings.t("unavailable") : root.status.state === "waiting-screen" ? strings.t("choose.screen") : root.status.state === "validating" ? strings.t("validating") : root.status.recording ? strings.t("recording").arg(Model.duration(root.status.elapsed)) : strings.t("idle")
                    color: root.status.recording ? Commons.Color.urgent : Commons.Color.foreground
                    font.family: Style.font.family; font.pixelSize: Style.font.body
                }
                GridLayout {
                    Layout.fillWidth: true; columns: 2
                    Ui.Button { Layout.fillWidth: true; text: strings.t("source.screen"); selected: root.status.screenEnabled; enabled: !root.changing && !root.status.recording; onClicked: root.hostWidget.setSource("screen", !root.status.screenEnabled) }
                    Ui.Button { Layout.fillWidth: true; text: strings.t("source.camera"); selected: root.status.cameraEnabled; enabled: !root.changing && !root.status.recording; onClicked: root.hostWidget.setSource("camera", !root.status.cameraEnabled) }
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
                    Layout.fillWidth: true; textFormat: Text.PlainText; elide: Text.ElideMiddle
                    visible: root.status.output !== ""; text: root.status.output
                    color: Commons.Color.foreground; opacity: 0.6; font.family: Style.font.family; font.pixelSize: Style.font.body - 2
                }
                Ui.Button {
                    Layout.fillWidth: true; text: strings.t(root.status.recording ? "stop" : "start")
                    bordered: true; enabled: !root.changing && root.status.state !== "stopping"
                    onClicked: root.hostWidget.action(root.status.recording ? "stop" : "start")
                }
                Ui.Button { Layout.fillWidth: true; text: strings.t("open"); bordered: true; onClicked: { root.hostWidget.action("open"); root.hostWidget.close() } }
            }
        }
    }
}
