.pragma library

// Source ids come from the recorder: lower-case letters, digits and hyphens.
function validId(id) { return typeof id === "string" && /^[a-z0-9][a-z0-9-]{0,31}$/.test(id) }

// The screens and cameras the recorder reports, checked field by field. A recorder
// from before sources were a list (0.5.0) reports one screen and one camera instead;
// those are marked legacy, because that recorder only knows its earlier commands.
function parseSources(value, screenRecording, cameraRecording) {
    if (!Array.isArray(value.videoSources)) return [
        { id: "screen", kind: "screen", name: "", legacy: true, enabled: value.screenEnabled === true, picker: true,
          previewState: String(value.screenPreviewState || "inactive").substring(0, 32),
          selected: value.screenSelected === true, selecting: value.screenSelecting === true, recording: screenRecording },
        { id: "camera", kind: "camera", name: "", legacy: true, enabled: value.cameraEnabled !== false, picker: false,
          previewState: String(value.cameraPreviewState || "inactive").substring(0, 32),
          selected: true, selecting: false, recording: cameraRecording }
    ]
    const sources = []
    for (const source of value.videoSources.slice(0, 16)) {
        if (!source || typeof source !== "object" || !validId(source.id)) continue
        if (source.kind !== "screen" && source.kind !== "camera") continue
        // Text fields of the wrong type fall back to their defaults rather than being shown as converted junk.
        sources.push({
            id: source.id, kind: source.kind, legacy: false,
            name: typeof source.name === "string" ? source.name.substring(0, 80) : "",
            enabled: source.enabled === true, picker: source.picker === true,
            previewState: typeof source.previewState === "string" && source.previewState !== "" ? source.previewState.substring(0, 32) : "inactive",
            selected: source.selected === true, selecting: source.selecting === true, recording: source.recording === true
        })
    }
    return sources
}

// The command that opens the picker for a screen. The earlier recorder has one screen and takes no id.
function chooseCommand(source) {
    if (!source || !validId(source.id)) return null
    return source.legacy ? ["sourceroll", "choose-screen", "--json"] : ["sourceroll", "choose-screen", source.id, "--json"]
}

// The command that switches a screen or camera on or off. Switching on a picked
// screen means choosing it. The earlier recorder has one switch per kind instead of per source.
function switchCommand(source, enabled) {
    if (!source || !validId(source.id)) return null
    if (enabled && source.kind === "screen" && source.picker) return chooseCommand(source)
    if (source.legacy) return ["sourceroll", "configure", "--" + source.kind + "-enabled", enabled ? "true" : "false", "--json"]
    return ["sourceroll", "source", "set", source.id, "--enabled", enabled ? "true" : "false", "--json"]
}

function find(sources, id) {
    for (const source of sources || []) if (source.id === id) return source
    return ({})
}

// The source to show: the one picked in the panel while it is switched on,
// otherwise the first that is.
function monitored(sources, picked) {
    const chosen = find(sources, picked)
    if (chosen.enabled) return chosen.id
    for (const source of sources || []) if (source.enabled) return source.id
    return ""
}

function parseStatus(text) {
    if (typeof text !== "string" || text.length > 65536) return null
    try {
        const value = JSON.parse(text)
        if (!value || typeof value !== "object" || typeof value.recording !== "boolean") return null
        const screenRecording = value.screenHealth !== "recovering" && value.screenHealth !== "failed" && value.screenHealth !== "starting" && (value.state === "recording" || value.state === "starting") && Array.isArray(value.sources) && value.sources.slice(0, 32).some(function(s) { return s && (s.id === "screen" || /^screen-[0-9]+$/.test(s.id)) && s.state === "recording" })
        const cameraRecording = (value.state === "recording" || value.state === "starting") && Array.isArray(value.sources) && value.sources.slice(0, 32).some(function(s) { return s && s.id === "camera" && s.state === "recording" })
        return {
            running: value.running === true,
            recording: value.recording,
            cameraEnabled: value.cameraEnabled !== false,
            screenEnabled: value.screenEnabled === true,
            microphoneEnabled: value.microphoneEnabled !== false,
            desktopEnabled: value.desktopEnabled === true,
            screenSelected: value.screenSelected === true,
            screenSelecting: value.screenSelecting === true,
            screenPreviewState: String(value.screenPreviewState || "inactive").substring(0, 32),
            cameraPreviewState: String(value.cameraPreviewState || "inactive").substring(0, 32),
            screenRecording: screenRecording,
            cameraRecording: cameraRecording,
            videoSources: parseSources(value, screenRecording, cameraRecording),
            displayWarning: String(value.displayWarning || "").substring(0,2000),
            warning: String(value.warning || "").substring(0,2000),
            state: String(value.state || "idle").substring(0, 32),
            elapsed: Math.max(0, Math.min(359999, Number(value.elapsedSeconds) || 0)),
            peakDb: Math.max(-90, Math.min(0, Number.isFinite(Number(value.peakDb)) ? Number(value.peakDb) : -90)),
            error: String(value.error || "").substring(0, 2000),
            output: String(value.output || "").substring(0, 4096)
        }
    } catch (_) { return null }
}

// Omarchy-owned tooltips/buttons can interpret rich text, so encode all dynamic text.
function safeText(value) {
    return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;")
}

function duration(seconds) {
    const value = Math.floor(Math.max(0, seconds || 0))
    return Math.floor(value / 60).toString().padStart(2, "0") + ":" + (value % 60).toString().padStart(2, "0")
}

function parseMeter(text) {
    if (typeof text !== "string" || text.length > 512) return null
    try {
        const value = JSON.parse(text)
        if (value.event !== "meter" || typeof value.peakDb !== "number" || !Number.isFinite(value.peakDb)) return null
        return Math.max(-90, Math.min(0, value.peakDb))
    } catch (_) { return null }
}

function parsePreview(text, source) {
    if (typeof text !== "string" || text.length > 1000100) return null
    try {
        const value = JSON.parse(text)
        if (value.event !== "preview" || typeof value.jpeg !== "string" || value.jpeg.length > 1000000) return null
        const id = value.source || "camera"
        if (!validId(id) || id !== (source || "camera")) return null
        if (!value.jpeg.startsWith('/9j/') || !/^[A-Za-z0-9+/]+={0,2}$/.test(value.jpeg)) return null
        return "data:image/jpeg;base64," + value.jpeg
    } catch (_) { return null }
}

function parseDesktopMeter(text) {
    if (typeof text !== "string" || text.length > 512) return null
    try {
        const value = JSON.parse(text)
        if (value.event !== "meter" || typeof value.desktopPeakDb !== "number" || !Number.isFinite(value.desktopPeakDb)) return null
        return Math.max(-90, Math.min(0, value.desktopPeakDb))
    } catch (_) { return null }
}
