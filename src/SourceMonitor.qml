import QtQuick

Rectangle {
    id: pane
    required property string sourceLabel
    required property string imageSource
    required property string feedState
    required property string emptyText
    required property bool recording
    property bool sourceEnabled: true
    property bool canChoose: false
    signal chooseRequested()
    readonly property bool stale: feedState === "stalled" || feedState === "error"
    color: theme.colors.preview
    radius: theme.radius
    border.width: 1
    border.color: stale ? theme.accent : theme.colors.muted
    clip: true
    Image {
        id: feed
        anchors.fill: parent; anchors.margins: 1; anchors.topMargin: 34
        source: pane.imageSource
        visible: source !== "" && (pane.feedState === "live" || pane.stale)
        fillMode: Image.PreserveAspectFit
        cache: false; retainWhileLoading: true
    }
    MouseArea {
        anchors.fill: parent
        enabled: pane.canChoose && (pane.imageSource === "" || pane.feedState === "inactive")
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: pane.chooseRequested()
    }
    Text {
        anchors.centerIn: parent; width: parent.width - 24
        visible: !feed.visible || pane.stale
        textFormat: Text.PlainText
        text: pane.stale ? qsTr("Preview stalled — check the source") : pane.emptyText
        wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
        color: theme.colors.foreground; font.family: "monospace"; font.pixelSize: theme.style.fontSize
        style: Text.Outline; styleColor: theme.colors.background
    }
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 34; color: theme.colors.background
        Text {
            anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
            text: pane.sourceLabel; textFormat: Text.PlainText
            font.family: "monospace"; font.pixelSize: theme.style.fontSize
            color: theme.colors.foreground
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: pane.stale ? qsTr("STALE") : pane.recording ? qsTr("REC") : pane.feedState === "live" ? qsTr("LIVE · PREVIEW") : ""
            font.family: "monospace"; font.pixelSize: theme.style.fontSize - 1
            color: pane.recording || pane.stale ? theme.accent : theme.colors.secondary
        }
    }
}
