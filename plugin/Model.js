.pragma library

function parseStatus(text) {
    if (typeof text !== "string" || text.length > 65536) return null
    try {
        const value = JSON.parse(text)
        if (!value || typeof value !== "object" || typeof value.recording !== "boolean") return null
        return {
            running: value.running === true,
            recording: value.recording,
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
