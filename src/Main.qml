import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 940; height: 760
    minimumWidth: 860; minimumHeight: 720
    visible: initialVisible
    title: qsTr("Source Record")
    color: "#101216"
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    property var state: backend.status
    property var prefs: backend.settings
    property bool helpVisible: false
    property bool allowQuit: false
    property bool busy: state.recording || state.state === "stopping"
    function indexOf(items, id) {
        for (let i = 0; i < items.length; ++i) if (items[i].id === id) return i
        return -1
    }
    function duration(seconds) {
        let s = Math.floor(seconds || 0)
        return Math.floor(s / 3600).toString().padStart(2, "0") + ":" + Math.floor(s % 3600 / 60).toString().padStart(2, "0") + ":" + (s % 60).toString().padStart(2, "0")
    }
    function stateLabel() {
        if (state.state === "starting") return qsTr("Starting…")
        if (state.state === "recording") return qsTr("Recording")
        if (state.state === "stopping") return qsTr("Saving…")
        if (state.state === "preview") return qsTr("Live preview")
        if (state.state === "preview-starting") return qsTr("Opening camera…")
        if (state.state === "error") return qsTr("Needs attention")
        return qsTr("Camera idle")
    }
    onVisibleChanged: backend.setWindowVisible(visible)
    Component.onCompleted: backend.setWindowVisible(visible)
    onClosing: function(close) { close.accepted = win.allowQuit; if (!win.allowQuit) win.hide() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !folder.activeFocus; onActivated: backend.toggle() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !folder.activeFocus; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !folder.activeFocus; onActivated: quitDialog.open() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible ? win.helpVisible = false : win.hide() }

    component PlainLabel: Label { textFormat: Text.PlainText; color: "#e7e9ee" }
    component Caption: PlainLabel { color: "#959eaf"; font.pixelSize: 12 }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 24; spacing: 14
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 3
                    PlainLabel { text: qsTr("Source Record"); font.pixelSize: 25; font.weight: Font.DemiBold }
                    Caption { text: qsTr("Your camera, saved separately. OBS stays independent.") }
                }
                Item { Layout.fillWidth: true }
                Rectangle { width: 8; height: 8; radius: 4; color: win.state.recording ? "#ff606a" : theme.accent }
                PlainLabel { text: win.stateLabel(); color: win.state.recording ? "#ff8a92" : "#bbc2cf" }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 18
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 240
                        color: "#080a0d"; radius: theme.radius; clip: true
                        Image {
                            id: preview
                            anchors.fill: parent; fillMode: Image.PreserveAspectFit
                            cache: false
                            source: backend.frameNumber > 0 ? "image://preview/frame/" + backend.frameNumber : ""
                            visible: win.state.preview && backend.frameNumber > 0
                        }
                        PlainLabel {
                            anchors.centerIn: parent
                            visible: !preview.visible
                            text: win.state.state === "error" ? qsTr("Camera unavailable") : qsTr("Waiting for camera…")
                            color: "#778295"
                        }
                        Rectangle {
                            anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 12
                            width: liveLabel.implicitWidth + 18; height: 28; radius: Math.min(4, theme.radius)
                            color: "#ce14171d"; visible: preview.visible
                            PlainLabel { id: liveLabel; anchors.centerIn: parent; font.pixelSize: 11; text: win.state.recording ? qsTr("REC · %1").arg(win.duration(win.state.elapsedSeconds)) : qsTr("PREVIEW · NOT RECORDING") }
                        }
                    }
                    RowLayout {
                        PlainLabel { text: win.duration(win.state.elapsedSeconds); font.family: "monospace"; font.pixelSize: 30 }
                        Item { Layout.fillWidth: true }
                        Caption {
                            text: { let lang = translator.language; return qsTr("%1 MiB").arg(Number((win.state.bytes || 0) / 1048576).toLocaleString(Qt.locale(), 'f', 1)) }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 5
                        RowLayout {
                            Caption { text: qsTr("Microphone level") }
                            Item { Layout.fillWidth: true }
                            Caption { text: win.prefs.audio === "none" ? qsTr("No audio") : (win.state.peakDb <= -90 ? "−∞ dBFS" : Math.round(win.state.peakDb) + " dBFS") }
                        }
                        Rectangle {
                            Layout.fillWidth: true; height: 7; color: "#272d36"; radius: Math.min(4, theme.radius)
                            Rectangle { width: parent.width * Math.max(0, Math.min(1, (win.state.peakDb + 60) / 60)); height: parent.height; radius: parent.radius; color: win.state.peakDb > -3 ? "#ff606a" : theme.accent }
                        }
                        Caption { text: qsTr("Meter only — microphone is not played through speakers."); wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 290; Layout.fillHeight: true
                    color: "#191d24"; radius: theme.radius
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 6
                        PlainLabel { text: qsTr("Capture settings"); font.weight: Font.DemiBold; font.pixelSize: 15 }
                        Caption { text: qsTr("Camera") }
                        ComboBox {
                            implicitHeight: 42; font.pixelSize: 13
                            Layout.fillWidth: true; enabled: !win.busy; model: backend.cameras; textRole: "label"
                            currentIndex: win.indexOf(backend.cameras, win.prefs.camera)
                            displayText: currentIndex < 0 ? qsTr("Select camera") : currentText
                            onActivated: backend.setSetting("camera", model[currentIndex].id)
                        }
                        Caption { text: qsTr("Resolution and frame rate") }
                        ComboBox {
                            implicitHeight: 42; font.pixelSize: 13
                            Layout.fillWidth: true; enabled: !win.busy; model: backend.modes; textRole: "label"
                            currentIndex: win.indexOf(backend.modes, win.prefs.mode)
                            displayText: currentIndex < 0 ? qsTr("Select mode") : currentText
                            onActivated: backend.setSetting("mode", model[currentIndex].id)
                        }
                        Caption { text: qsTr("Audio source") }
                        ComboBox {
                            implicitHeight: 42; font.pixelSize: 13
                            Layout.fillWidth: true; enabled: !win.busy; model: backend.microphones; textRole: "label"
                            currentIndex: win.indexOf(backend.microphones, win.prefs.audio)
                            displayText: currentIndex < 0 ? qsTr("Select microphone") : currentText
                            onActivated: backend.setSetting("audio", model[currentIndex].id)
                        }
                        Caption { text: qsTr("Video storage") }
                        ComboBox {
                            implicitHeight: 42; font.pixelSize: 13
                            Layout.fillWidth: true; enabled: !win.busy
                            model: [qsTr("Original camera stream"), qsTr("H.264 · smaller files")]
                            currentIndex: win.prefs.encoding === "h264" ? 1 : 0
                            onActivated: backend.setSetting("encoding", currentIndex === 0 ? "original" : "h264")
                        }
                        Caption { text: qsTr("Original MJPEG / H.264 is saved without re-encoding. Raw camera modes use high-quality H.264. Audio is uncompressed."); wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Item { Layout.fillHeight: true; Layout.minimumHeight: 2 }
                        Button { text: qsTr("Refresh devices"); implicitHeight: 38; Layout.fillWidth: true; enabled: !win.busy; onClicked: backend.refreshDevices() }
                    }
                }
            }
            Rectangle {
                visible: win.state.error !== ""; Layout.fillWidth: true; implicitHeight: errorText.implicitHeight + 20
                color: "#392126"; radius: theme.radius
                PlainLabel { id: errorText; anchors.fill: parent; anchors.margins: 10; text: win.state.error || ""; wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight; color: "#ffa2a9" }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: folder; Layout.fillWidth: true; enabled: !win.busy
                    text: win.prefs.directory || ""; placeholderText: qsTr("Output folder")
                    onEditingFinished: { if (text !== win.prefs.directory) backend.setSetting("directory", text) }
                }
                Button { text: qsTr("Open folder"); onClicked: backend.openFolder() }
            }
            Caption { Layout.fillWidth: true; visible: !!win.state.output; text: qsTr("File: %1").arg(win.state.output || ""); elide: Text.ElideMiddle }
            RowLayout {
                Layout.fillWidth: true
                Caption { text: qsTr("Space: record / stop    ·    Esc: hide    ·    ?: shortcuts"); Layout.fillWidth: true }
                Button { text: qsTr("Hide to bar"); onClicked: win.hide() }
                Button {
                    text: win.state.recording ? qsTr("Stop and save") : qsTr("Start recording")
                    highlighted: true; enabled: win.state.state !== "stopping"
                    onClicked: backend.toggle()
                }
            }
        }
    }
    Dialog {
        id: quitDialog; anchors.centerIn: parent; modal: true; width: 420
        title: win.state.recording ? qsTr("Stop recording and quit?") : qsTr("Quit Source Record?")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: { win.allowQuit = true; backend.stop(); Qt.quit() }
        contentItem: PlainLabel { text: win.state.recording ? qsTr("The current clip will be saved before quitting.") : qsTr("The bar can start the recorder again."); padding: 14 }
    }
    Dialog {
        visible: win.helpVisible; anchors.centerIn: parent; modal: true; width: 540
        title: qsTr("Keyboard shortcuts")
        standardButtons: Dialog.Close
        onRejected: win.helpVisible = false
        contentItem: PlainLabel { text: qsTr("Space — Start / stop recording\nEsc — Hide window, keep recording\nQ — Quit (confirm first)\n? — Show shortcuts\n\nBar: click to open controls, middle-click to record / stop."); padding: 16 }
    }
}
