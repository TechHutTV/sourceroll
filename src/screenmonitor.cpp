#include "screenmonitor.h"
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

QStringList ScreenMonitor::arguments() {
    return {"-hide_banner", "-loglevel", "warning", "-nostdin", "-fflags", "nobuffer", "-flags", "low_delay",
        "-probesize", "32768", "-analyzeduration", "0", "-f", "mpegts", "-i", "pipe:0", "-an",
        "-vf", "fps=15,scale=640:-2", "-c:v", "mjpeg", "-q:v", "5", "-threads", "2", "-f", "image2pipe", "pipe:1"};
}
ScreenMonitor::ScreenMonitor(QObject *parent) : QObject(parent) {
    m_decoder.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); if (getppid() == 1) _exit(1); });
    connect(&m_decoder, &QProcess::readyReadStandardOutput, this, &ScreenMonitor::consume);
    connect(&m_decoder, &QProcess::readyReadStandardError, this, [this] { m_log = (m_log + m_decoder.readAllStandardError()).right(2000); });
    connect(&m_decoder, &QProcess::finished, this, [this](int code) {
        if (!m_stopping && m_active && m_enabled) { m_error = tr("Screen preview decoder stopped (%1). %2").arg(code).arg(QString::fromUtf8(m_log)); emit frameReady(); }
    });
    connect(&m_decoder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { m_error = tr("Screen preview could not start: %1").arg(m_decoder.errorString()); emit frameReady(); }
    });
}
ScreenMonitor::~ScreenMonitor() { stopDecoder(); }
void ScreenMonitor::stopDecoder() {
    m_stopping = true;
    if (m_decoder.state() != QProcess::NotRunning) {
        m_decoder.kill(); m_decoder.waitForFinished(1000);
    }
    m_stopping = false; m_frames.clear();
}
void ScreenMonitor::launch() {
    if (!m_active || !m_enabled || m_decoder.state() != QProcess::NotRunning) return;
    m_error.clear(); m_frames.clear(); m_log.clear(); m_lastFrame.invalidate(); m_started.start();
    m_decoder.start("ffmpeg", arguments());
}
void ScreenMonitor::setEnabled(bool enabled) {
    if (enabled == m_enabled) return;
    m_enabled = enabled;
    if (enabled) launch(); else { stopDecoder(); m_image = {}; m_jpeg.clear(); m_frameNumber = 0; emit frameReady(); }
}
void ScreenMonitor::start() {
    stopDecoder(); m_active = true; m_hasInput = false; m_image = {}; m_jpeg.clear(); m_frameNumber = 0; m_error.clear();
    launch(); emit frameReady();
}
void ScreenMonitor::stop() {
    m_active = false; stopDecoder(); m_image = {}; m_jpeg.clear(); m_error.clear(); emit frameReady();
}
void ScreenMonitor::feed(const QByteArray &bytes) {
    if (!m_active || !m_enabled || bytes.isEmpty()) return;
    if (!m_hasInput) { m_hasInput = true; m_started.start(); }
    if (m_decoder.state() == QProcess::NotRunning && m_started.isValid() && m_started.elapsed() > 2000) launch();
    if (m_decoder.state() != QProcess::NotRunning && m_decoder.bytesToWrite() > 2 * 1024 * 1024) {
        // Drop this disposable decoder instead of letting it delay the recorder.
        stopDecoder(); launch();
    }
    if (m_decoder.state() != QProcess::NotRunning) m_decoder.write(bytes);
}
void ScreenMonitor::consume() {
    m_frames += m_decoder.readAllStandardOutput();
    if (m_frames.size() > 2 * 1024 * 1024) m_frames.clear();
    for (;;) {
        const auto begin = m_frames.indexOf(QByteArray::fromHex("ffd8"));
        if (begin < 0) { m_frames = m_frames.right(1); break; }
        if (begin) m_frames.remove(0, begin);
        const auto end = m_frames.indexOf(QByteArray::fromHex("ffd9"), 2);
        if (end < 0) break;
        const auto jpeg = m_frames.left(end + 2); m_frames.remove(0, end + 2);
        const auto image = QImage::fromData(jpeg, "JPEG");
        if (image.isNull()) continue;
        m_image = image; m_jpeg = jpeg; ++m_frameNumber; m_lastFrame.start(); emit frameReady();
    }
}
QString ScreenMonitor::state() const {
    if (!m_active || !m_enabled) return "inactive";
    if (!m_error.isEmpty()) return "error";
    if (!m_hasInput) return "waiting";
    if (m_lastFrame.isValid()) return m_lastFrame.elapsed() > 3000 ? "stalled" : "live";
    return m_started.isValid() && m_started.elapsed() > 6000 ? "stalled" : "waiting";
}
