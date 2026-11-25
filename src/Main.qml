import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1120; height: 850
    minimumWidth: 980; minimumHeight: 760
    visible: initialVisible
    title: qsTr("Source Roll")
    color: theme.colors.background
    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.colors.background
    Material.foreground: theme.colors.foreground
    Material.primary: theme.colors.background
    font.family: "monospace"
    font.pixelSize: theme.style.fontSize
    Material.roundedScale: theme.radius
    property var state: backend.status
    property var prefs: backend.settings
    // Screens and cameras, in the order they were added.
    readonly property var videoSources: prefs.videoSources || []
    // "all", or the one kind of source to show: "screen" or "camera".
    property string monitorView: "all"
    readonly property int monitorCount: {
        let count = 0
        for (let i = 0; i < videoSources.length; ++i) if (monitored(videoSources[i])) ++count
        return count
    }
    // Follows the saved choice; until one is made, narrow windows stack automatically.
    property bool monitorStacked: prefs.monitorLayout === "stacked" || (prefs.monitorLayout !== "side" && monitorGrid.width < 620)
    // Keys typed into a text field must not act as shortcuts.
    readonly property bool typing: activeFocusItem instanceof TextInput
    property bool helpVisible: false
    property bool allowQuit: false
    property bool busy: state.recording || state.state === "stopping" || state.state === "validating"
    function indexOf(items, id) {
        for (let i = 0; i < items.length; ++i) if (items[i].id === id) return i
        return -1
    }
    function duration(seconds) {
        let s = Math.floor(seconds || 0)
        return Math.floor(s / 3600).toString().padStart(2, "0") + ":" + Math.floor(s % 3600 / 60).toString().padStart(2, "0") + ":" + (s % 60).toString().padStart(2, "0")
    }
    function monitored(source) { return !!source.enabled && (monitorView === "all" || monitorView === source.kind) }
    // A source's own name, or "Screen", "Camera 2" and so on until it is given one.
    function sourceLabel(source) { return source.name || defaultLabel(source) }
    function defaultLabel(source) {
        const number = Number((/-([0-9]+)$/.exec(source.id || "") || [0, 1])[1])
        const kind = source.kind === "screen" ? qsTr("Screen") : qsTr("Camera")
        return number > 1 ? kind + " " + number : kind
    }
    // What the recorder reports for one source: its preview, picker and recording state.
    function sourceState(id) {
        const all = state.videoSources || []
        for (let i = 0; i < all.length; ++i) if (all[i].id === id) return all[i]
        return ({})
    }
    function cameraModes(device) {
        const index = indexOf(backend.cameras, device)
        return index < 0 ? [] : (backend.cameras[index].modes || [])
    }
    function chooseCamera(index) {
        const section = sections.itemAt(index)
        if (!section) return
        const view = sourcesScroll.contentItem
        view.contentY = Math.max(0, Math.min(section.y, view.contentHeight - view.height))
        section.openDevices()
    }
    function stateLabel() {
        if (state.screenSelecting) return qsTr("Choose a screen or window…")
        if (state.state === "waiting-screen") return qsTr("Choose a screen or window…")
        if (state.state === "validating") return qsTr("Checking saved files…")
        if (state.state === "saved") return state.session && state.session.hasScreenGaps ? qsTr("Saved with screen interruptions") : qsTr("Session saved")
        if ((state.state === "recording" || state.state === "starting") && state.screenReconnecting) return qsTr("Reconnecting screen…")
        if (state.state === "starting") return qsTr("Starting…")
        if (state.state === "recording") return qsTr("Recording")
        if (state.state === "stopping") return qsTr("Saving…")
        if (state.state === "preview") return qsTr("Live preview")
        if (state.state === "preview-starting") return qsTr("Opening camera…")
        if (state.state === "error") return qsTr("Needs attention")
        return qsTr("Idle")
    }
    onVisibleChanged: backend.setWindowVisible(visible)
    Component.onCompleted: backend.setWindowVisible(visible)
    onClosing: function(close) { close.accepted = win.allowQuit; if (!win.allowQuit) win.hide() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.state.screenSelecting; onActivated: backend.toggle() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: quitDialog.open() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible ? win.helpVisible = false : win.hide() }

    component PlainLabel: Label { textFormat: Text.PlainText; color: theme.colors.foreground }
    component Caption: PlainLabel { color: theme.colors.secondary; font.pixelSize: theme.style.fontSize }


    component SourceSwitch: CheckBox {
        id: toggle
        font.family: win.font.family; font.pixelSize: theme.style.fontSize
        implicitHeight: 30; spacing: 8
        enabled: !win.busy
        indicator: Rectangle {
            x: toggle.leftPadding; y: (toggle.height - height) / 2
            width: 15; height: 15; radius: Math.min(3, theme.radius)
            color: toggle.checked ? theme.accent : theme.style.normalFill
            border.width: 1; border.color: toggle.checked ? theme.accent : theme.style.normalBorder
            Text { anchors.centerIn: parent; text: toggle.checked ? "✓" : ""; textFormat: Text.PlainText; color: theme.accentForeground; font.pixelSize: 12 }
        }
        contentItem: Text {
            text: toggle.text; textFormat: Text.PlainText; font: toggle.font
            color: toggle.enabled ? theme.colors.foreground : theme.colors.secondary
            leftPadding: 23; verticalAlignment: Text.AlignVCenter
        }
    }

    component RecordButton: Button {
        id: control
        topInset: 0; bottomInset: 0; leftInset: 0; rightInset: 0
        implicitHeight: theme.style.controlHeight + 4
        implicitWidth: label.implicitWidth + 24
        leftPadding: 12; rightPadding: 12
        hoverEnabled: true
        font.family: win.font.family
        font.pixelSize: theme.style.fontSize
        contentItem: Text {
            id: label
            text: control.text; textFormat: Text.PlainText; font: control.font
            color: !control.enabled ? theme.colors.secondary : control.highlighted ? theme.accent : theme.colors.foreground
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: theme.radius
            color: control.down ? theme.style.pressedFill : control.hovered || control.activeFocus ? theme.style.hoverFill : control.highlighted ? Qt.alpha(theme.accent, 0.1) : theme.style.normalFill
            border.width: theme.style.borderWidth
            border.color: control.highlighted ? theme.accent : control.activeFocus ? theme.style.focusBorder : control.hovered ? theme.style.hoverBorder : theme.style.normalBorder
            opacity: control.enabled ? 1 : 0.5
        }
    }

    component RecordCombo: ComboBox {
        id: control
        // The entry the settings call for. The shown entry follows it, including
        // after the list is replaced or a choice is turned down.
        property int selected: -1
        // The list is about to open: the moment to bring it up to date.
        signal opening()
        function sync() { if (currentIndex !== selected) currentIndex = selected }
        onSelectedChanged: sync()
        onModelChanged: sync()
        onCountChanged: sync()
        onActivated: Qt.callLater(sync)
        Component.onCompleted: sync()
        implicitHeight: theme.style.controlHeight + 4
        leftPadding: 10; rightPadding: 28
        hoverEnabled: true
        font.family: win.font.family
        font.pixelSize: theme.style.fontSize
        contentItem: Text {
            text: control.displayText; textFormat: Text.PlainText; font: control.font
            color: control.enabled ? theme.colors.foreground : theme.colors.secondary
            elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
        indicator: Text {
            x: control.mirrored ? 10 : control.width - width - 10
            y: (control.height - height) / 2
            text: "⌄"; textFormat: Text.PlainText
            color: theme.colors.secondary; font: control.font
        }
        background: Rectangle {
            radius: theme.radius
            color: control.down ? theme.style.pressedFill : control.hovered ? theme.style.hoverFill : theme.style.normalFill
            border.width: theme.style.borderWidth
            border.color: control.activeFocus ? theme.accent : control.hovered ? theme.style.hoverBorder : theme.style.normalBorder
            opacity: control.enabled ? 1 : 0.5
        }
        delegate: ItemDelegate {
            id: option
            required property int index
            width: control.width - 2; height: theme.style.controlHeight + 6
            highlighted: control.highlightedIndex === index
            contentItem: Text {
                text: control.textAt(option.index); textFormat: Text.PlainText
                color: option.highlighted ? theme.accent : theme.colors.foreground
                font: control.font; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { radius: theme.radius; color: option.highlighted ? theme.style.hoverFill : "transparent" }
        }
        popup: Popup {
            onAboutToShow: control.opening()
            y: control.height + 4; width: control.width
            padding: 1
            implicitHeight: Math.min(contentItem.implicitHeight + 2, 260)
            contentItem: ListView {
                clip: true; implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle { color: theme.colors.background; radius: theme.radius; border.width: 1; border.color: theme.accent }
        }
    }

    component FolderField: TextField {
        id: control
        // Shown while the field is empty.
        property string hint: ""
        // Enter finishes the entry and hands the keyboard back to the shortcuts.
        onAccepted: focus = false
        Text {
            anchors.fill: parent; anchors.leftMargin: control.leftPadding; anchors.rightMargin: control.rightPadding
            visible: control.text === "" && control.preeditText === ""
            text: control.hint; textFormat: Text.PlainText; font: control.font
            color: theme.colors.secondary; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
        topInset: 0; bottomInset: 0; leftInset: 0; rightInset: 0
        implicitHeight: theme.style.controlHeight + 8
        font.family: win.font.family; font.pixelSize: theme.style.fontSize
        color: theme.colors.foreground; placeholderTextColor: theme.colors.secondary
        selectionColor: theme.colors.selection; selectedTextColor: theme.colors.foreground
        leftPadding: 10; rightPadding: 10
        background: Rectangle {
            radius: theme.radius
            color: theme.style.normalFill
            border.width: theme.style.borderWidth
            border.color: control.activeFocus ? theme.accent : theme.style.normalBorder
            opacity: control.enabled ? 1 : 0.5
        }
    }

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
                    PlainLabel { text: qsTr("Source Roll"); font.pixelSize: theme.style.fontSize + 7; font.weight: Font.DemiBold }
                }
                Item { Layout.fillWidth: true }
                Rectangle { width: 8; height: 8; radius: 4; color: win.state.recording ? theme.colors.urgent : theme.accent }
                PlainLabel { text: win.stateLabel(); color: win.state.recording ? theme.colors.urgent : theme.colors.secondary }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 18
                Flow {
                    Layout.fillWidth: true; spacing: 18
                    Repeater {
                        model: win.videoSources.length
                        SourceSwitch {
                            required property int index
                            readonly property var source: win.videoSources[index] || ({})
                            objectName: "switch-" + source.id
                            // Switching on a picked screen opens its picker, and only one can be open.
                            enabled: !win.busy && !(win.state.screenSelecting && !source.enabled && source.kind === "screen" && source.target === "portal")
                            text: win.sourceLabel(source); checked: !!source.enabled
                            onClicked: {
                                backend.setSourceSetting(source.id, "enabled", checked)
                                // A picked screen has to be approved again; a monitor previews by itself.
                                if (checked && source.kind === "screen" && source.target === "portal") backend.chooseScreen(source.id)
                            }
                        }
                    }
                    SourceSwitch { text: qsTr("Microphone"); checked: !!win.prefs.microphoneEnabled; onClicked: backend.setSetting("microphoneEnabled", checked) }
                    SourceSwitch { text: qsTr("Desktop audio"); checked: !!win.prefs.desktopEnabled; onClicked: backend.setSetting("desktopEnabled", checked) }
                }
                Caption { text: qsTr("Separate files · one take"); Layout.alignment: Qt.AlignTop; topPadding: 6 }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 18
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Caption { text: qsTr("Monitor") }
                        Item { Layout.fillWidth: true }
                        RecordButton { objectName: "monitorScreen"; text: qsTr("Screens"); highlighted: win.monitorView === "screen"; onClicked: win.monitorView = "screen" }
                        RecordButton { objectName: "monitorCamera"; text: qsTr("Cameras"); highlighted: win.monitorView === "camera"; onClicked: win.monitorView = "camera" }
                        RecordButton { objectName: "monitorAll"; text: qsTr("All"); highlighted: win.monitorView === "all"; onClicked: win.monitorView = "all" }
                        RecordButton {
                            objectName: "monitorLayout"; visible: win.monitorCount > 1
                            text: win.monitorStacked ? qsTr("Side by side") : qsTr("Stack")
                            Accessible.description: qsTr("Switch the monitor layout")
                            onClicked: backend.setSetting("monitorLayout", win.monitorStacked ? "side" : "stacked")
                        }
                    }
                    Flickable {
                        id: monitorArea
                        // Panes share the space; when they would get too small, the area scrolls instead.
                        readonly property int rows: Math.max(1, Math.ceil(win.monitorCount / monitorGrid.columns))
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 240
                        clip: true; boundsBehavior: Flickable.StopAtBounds
                        contentWidth: width; contentHeight: Math.max(height, rows * 110 + (rows - 1) * 8)
                        interactive: contentHeight > height
                        ScrollIndicator.vertical: ScrollIndicator {}
                    GridLayout {
                        id: monitorGrid
                        width: monitorArea.width; height: monitorArea.contentHeight
                        // Side by side, up to four panes sit two across; beyond that, three.
                        columns: win.monitorStacked ? 1 : win.monitorCount > 4 ? 3 : 2
                        rowSpacing: 8; columnSpacing: 8
                        Repeater {
                            model: win.videoSources.length
                            SourceMonitor {
                                id: pane
                                required property int index
                                readonly property var source: win.videoSources[index] || ({})
                                readonly property var live: win.sourceState(source.id)
                                readonly property bool isScreen: source.kind === "screen"
                                // The newest frame of this source; the backend announces each one.
                                property int frame: 0
                                objectName: "monitor-" + source.id
                                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 110
                                visible: win.monitored(source)
                                sourceLabel: win.sourceLabel(source)
                                feedState: live.previewState || "inactive"
                                imageSource: frame > 0 && feedState !== "inactive" ? "image://preview/" + source.id + "/" + frame : ""
                                recording: !!live.recording
                                // An empty pane opens the picker, or the camera list. A monitor captured
                                // directly has nothing to pick.
                                canChoose: !win.busy && (isScreen ? source.target === "portal" && !win.state.screenSelecting : true)
                                onChooseRequested: isScreen ? backend.chooseScreen(source.id) : win.chooseCamera(index)
                                emptyText: !isScreen ? (win.state.state === "error" ? qsTr("Camera unavailable") : qsTr("Waiting for camera…"))
                                    : live.selecting ? qsTr("Choose a screen or window…")
                                    : win.state.recording || source.target !== "portal" ? qsTr("Waiting for screen preview…") : qsTr("Choose a screen/window to preview")
                                Component.onCompleted: frame = backend.frame(source.id)
                                Connections {
                                    target: backend
                                    function onSourceFrame(id, number) { if (id === pane.source.id) pane.frame = number }
                                }
                            }
                        }
                        Rectangle {
                            objectName: "monitorEmpty"
                            visible: win.monitorCount === 0
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 110
                            color: theme.colors.preview; radius: theme.radius
                            border.width: 1; border.color: theme.colors.muted
                            PlainLabel {
                                anchors.centerIn: parent; width: parent.width - 24
                                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                                text: win.videoSources.length === 0 ? qsTr("Add a screen or camera under Sources.") : qsTr("No screen or camera is switched on for this view.")
                            }
                        }
                    }
                    }
                    RowLayout {
                        PlainLabel { text: win.duration(win.state.elapsedSeconds); font.family: "monospace"; font.pixelSize: theme.style.fontSize + 14 }
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
                            Caption { text: win.prefs.audio === "none" ? qsTr("No audio") : (backend.peakDb <= -90 ? "−∞ dBFS" : Math.round(backend.peakDb) + " dBFS") }
                        }
                        Rectangle {
                            Layout.fillWidth: true; height: 7; color: theme.colors.muted; radius: Math.min(4, theme.radius)
                            Rectangle { width: parent.width * Math.max(0, Math.min(1, (backend.peakDb + 60) / 60)); height: parent.height; radius: parent.radius; color: backend.peakDb > -3 ? theme.colors.urgent : theme.accent }
                        }
                        Caption { text: qsTr("Meter only — microphone is not played through speakers."); wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; visible: !!win.prefs.desktopEnabled; spacing: 5
                        Caption { text: qsTr("Desktop audio level") }
                        Rectangle {
                            Layout.fillWidth: true; height: 7; color: theme.colors.muted
                            Rectangle { width: parent.width * Math.max(0, Math.min(1, (backend.desktopPeakDb + 60) / 60)); height: parent.height; color: backend.desktopPeakDb > -3 ? theme.colors.urgent : theme.accent }
                        }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 365; Layout.fillHeight: true
                    color: theme.colors.background; radius: theme.radius
                    border.width: 1; border.color: theme.colors.muted
                    ScrollView {
                        id: sourcesScroll
                        anchors.fill: parent; anchors.margins: 16
                        clip: true; contentWidth: availableWidth
                        ColumnLayout {
                        width: parent.width; spacing: 8
                        PlainLabel { text: qsTr("Sources"); font.weight: Font.DemiBold; font.pixelSize: theme.style.fontSize + 1 }
                        Repeater {
                            id: sections
                            model: win.videoSources.length
                            ColumnLayout {
                                id: section
                                required property int index
                                readonly property var source: win.videoSources[index] || ({})
                                readonly property var live: win.sourceState(source.id)
                                readonly property bool isScreen: source.kind === "screen"
                                readonly property var modes: win.cameraModes(source.device)
                                function openDevices() { deviceChoice.popup.open() }
                                objectName: "source-" + source.id
                                Layout.fillWidth: true; spacing: 8
                                Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 2; implicitHeight: 1; color: theme.colors.muted }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    FolderField {
                                        objectName: "name-" + section.source.id
                                        Layout.fillWidth: true; enabled: !win.busy
                                        text: section.source.name || ""
                                        // Unnamed, it shows and saves under its kind: "Screen", "Camera 2".
                                        hint: win.defaultLabel(section.source)
                                        Accessible.name: qsTr("Source name")
                                        onEditingFinished: { if (text !== (section.source.name || "")) backend.setSourceSetting(section.source.id, "name", text) }
                                    }
                                    RecordButton {
                                        objectName: "remove-" + section.source.id
                                        text: qsTr("Remove"); enabled: !win.busy
                                        onClicked: backend.removeSource(section.source.id)
                                    }
                                }
                                Caption { visible: section.isScreen; text: qsTr("Screen or window") }
                                RecordCombo {
                                    visible: section.isScreen
                                    Layout.fillWidth: true; enabled: !win.busy && !!section.source.enabled
                                    model: backend.screens; textRole: "label"
                                    selected: win.indexOf(backend.screens, section.source.target)
                                    onActivated: backend.setSourceSetting(section.source.id, "target", model[currentIndex].id)
                                }
                                RecordButton {
                                    objectName: "choose-" + section.source.id
                                    visible: section.isScreen
                                    Layout.fillWidth: true; enabled: !win.busy && !win.state.screenSelecting
                                    text: qsTr("Choose screen/window")
                                    onClicked: backend.chooseScreen(section.source.id)
                                }
                                Caption { visible: section.isScreen; text: qsTr("Screen frame rate") }
                                RecordCombo {
                                    visible: section.isScreen
                                    Layout.fillWidth: true; enabled: !win.busy && !!section.source.enabled
                                    model: [qsTr("30 fps"), qsTr("60 fps")]
                                    selected: section.source.fps >= 60 ? 1 : 0
                                    onActivated: backend.setSourceSetting(section.source.id, "fps", currentIndex === 0 ? 30 : 60)
                                }
                                Caption { visible: section.isScreen; text: qsTr("Screen quality") }
                                RecordCombo {
                                    visible: section.isScreen
                                    Layout.fillWidth: true; enabled: !win.busy && !!section.source.enabled
                                    model: [qsTr("High"), qsTr("Very high"), qsTr("Ultra")]
                                    selected: ["high", "very_high", "ultra"].indexOf(section.source.quality || "very_high")
                                    onActivated: backend.setSourceSetting(section.source.id, "quality", ["high", "very_high", "ultra"][currentIndex])
                                }
                                Caption { visible: !section.isScreen; text: qsTr("Camera") }
                                RecordCombo {
                                    id: deviceChoice
                                    objectName: "cameraChoice-" + section.source.id
                                    visible: !section.isScreen
                                    Layout.fillWidth: true; enabled: !win.busy; model: backend.cameras; textRole: "label"
                                    selected: win.indexOf(backend.cameras, section.source.device)
                                    displayText: selected < 0 ? qsTr("Select camera") : currentText
                                    // Cameras come and go; show the ones connected right now.
                                    onOpening: backend.rescanCameras()
                                    onActivated: backend.setSourceSetting(section.source.id, "device", model[currentIndex].id)
                                }
                                Caption { visible: !section.isScreen; text: qsTr("Resolution and frame rate") }
                                RecordCombo {
                                    visible: !section.isScreen
                                    Layout.fillWidth: true; enabled: !win.busy; model: section.modes; textRole: "label"
                                    selected: win.indexOf(section.modes, section.source.mode)
                                    displayText: selected < 0 ? qsTr("Select mode") : currentText
                                    onActivated: backend.setSourceSetting(section.source.id, "mode", model[currentIndex].id)
                                }
                                Caption { visible: !section.isScreen; text: qsTr("Video storage") }
                                RecordCombo {
                                    visible: !section.isScreen
                                    Layout.fillWidth: true; enabled: !win.busy
                                    model: [qsTr("Original camera stream"), qsTr("H.264 · smaller files")]
                                    selected: section.source.encoding === "h264" ? 1 : 0
                                    onActivated: backend.setSourceSetting(section.source.id, "encoding", currentIndex === 0 ? "original" : "h264")
                                }
                                PlainLabel {
                                    Layout.fillWidth: true; wrapMode: Text.Wrap
                                    visible: !!section.live.previewError
                                    text: section.live.previewError || ""
                                    color: theme.accent; font.pixelSize: theme.style.fontSize - 1
                                }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 2; implicitHeight: 1; color: theme.colors.muted }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            RecordButton {
                                objectName: "addScreen"
                                Layout.fillWidth: true; text: qsTr("Add screen")
                                enabled: !win.busy && !win.state.screenSelecting && win.videoSources.length < backend.sourceLimit
                                onClicked: { const id = backend.addSource("screen"); if (id) backend.chooseScreen(id) }
                            }
                            RecordButton {
                                objectName: "addCamera"
                                Layout.fillWidth: true; text: qsTr("Add camera")
                                enabled: !win.busy && win.videoSources.length < backend.sourceLimit
                                onClicked: backend.addSource("camera")
                            }
                        }
                        Caption { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Screens are saved at their native resolution as H.264; higher quality uses more storage. Original MJPEG / H.264 camera streams are saved without re-encoding, and raw camera modes use high-quality H.264.") }
                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 2; implicitHeight: 1; color: theme.colors.muted }
                        Caption { text: qsTr("Microphone") }
                        RecordCombo {
                            Layout.fillWidth: true; enabled: !win.busy; model: backend.microphones; textRole: "label"
                            selected: win.indexOf(backend.microphones, win.prefs.audio)
                            displayText: selected < 0 ? qsTr("Select microphone") : currentText
                            onActivated: backend.setSetting("audio", model[currentIndex].id)
                        }
                        Caption { text: qsTr("Desktop audio source") }
                        RecordCombo {
                            Layout.fillWidth: true; enabled: !win.busy && !!win.prefs.desktopEnabled
                            model: backend.desktopSources; textRole: "label"
                            selected: win.indexOf(backend.desktopSources, win.prefs.desktopAudio)
                            onActivated: backend.setSetting("desktopAudio", model[currentIndex].id)
                        }
                        Caption { text: qsTr("Each enabled source is saved as its own file, named after the source. Audio is uncompressed. Session timing and an editing timeline are included."); wrapMode: Text.Wrap; Layout.fillWidth: true }
                        RecordButton { text: qsTr("Refresh devices"); Layout.fillWidth: true; enabled: !win.busy; onClicked: backend.refreshDevices() }
                        RecordButton { text: qsTr("Recover interrupted take"); Layout.fillWidth: true; enabled: !win.busy; onClicked: backend.recoverSession() }
                        }
                    }
                }
            }
            Rectangle {
                visible: !!win.state.error; Layout.fillWidth: true; implicitHeight: errorText.implicitHeight + 20
                color: Qt.tint(theme.colors.background, Qt.alpha(theme.colors.urgent, 0.12)); radius: theme.radius
                PlainLabel { id: errorText; anchors.fill: parent; anchors.margins: 10; text: win.state.error || ""; wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight; color: theme.colors.urgent }
            }
            Caption { objectName: "displayWarning"; text: win.state.displayWarning || ""; visible: text !== ""; wrapMode: Text.Wrap; Layout.fillWidth: true; color: theme.accent }
            Caption { objectName: "captureWarning"; text: win.state.warning || ""; visible: text !== ""; wrapMode: Text.Wrap; Layout.fillWidth: true; color: theme.accent }
            Caption {
                Layout.fillWidth: true; visible: win.busy
                text: (win.state.sources || []).map(function(s) { return s.id + ": " + s.state }).join("    ")
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                FolderField {
                    id: folder; Layout.fillWidth: true; enabled: !win.busy
                    text: win.prefs.directory || ""; Accessible.name: qsTr("Output folder")
                    onEditingFinished: { if (text !== win.prefs.directory) backend.setSetting("directory", text) }
                }
                RecordButton { text: qsTr("Open folder"); onClicked: backend.openFolder() }
            }
            Caption { Layout.fillWidth: true; visible: !!win.state.output; text: qsTr("File: %1").arg(win.state.output || ""); elide: Text.ElideMiddle }
            RowLayout {
                Layout.fillWidth: true
                Caption { text: qsTr("Space: record / stop    ·    Esc: hide    ·    ?: shortcuts"); Layout.fillWidth: true }
                RecordButton { text: qsTr("Hide to bar"); onClicked: win.hide() }
                RecordButton {
                    text: win.state.recording ? qsTr("Stop and save") : qsTr("Record enabled sources")
                    highlighted: true; enabled: win.state.state !== "stopping" && !win.state.screenSelecting
                    onClicked: backend.toggle()
                }
            }
        }
    }
    Dialog {
        id: quitDialog; objectName: "quitDialog"; anchors.centerIn: parent; modal: true; width: 420
        title: win.state.recording ? qsTr("Stop recording and quit?") : qsTr("Quit Source Roll?")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: { win.allowQuit = true; backend.shutdown(); Qt.quit() }
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
