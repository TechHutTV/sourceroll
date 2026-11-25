.pragma library

function parseStatus(text) {
    if (typeof text !== "string" || text.length > 65536) return null
    try {
        const value = JSON.parse(text)
        if (!value || typeof value !== "object" || typeof value.recording !== "boolean") return null
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
            screenRecording: value.screenHealth !== "recovering" && value.screenHealth !== "failed" && value.screenHealth !== "starting" && (value.state === "recording" || value.state === "starting") && Array.isArray(value.sources) && value.sources.slice(0, 8).some(function(s) { return s && (s.id === "screen" || /^screen-[0-9]+$/.test(s.id)) && s.state === "recording" }),
            cameraRecording: (value.state === "recording" || value.state === "starting") && Array.isArray(value.sources) && value.sources.slice(0, 4).some(function(s) { return s && s.id === "camera" && s.state === "recording" }),
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
        if ((id !== "camera" && id !== "screen") || id !== (source || "camera")) return null
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
