#include "camerafeed.h"
#include "session.h"
#include "sources.h"

#include <QRegularExpression>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

CameraFeed::CameraFeed(QObject *parent, const QString &program) : QObject(parent), m_program(program) {
    // If the app crashes, ask only its own child to finalize rather than orphaning capture.
    m_process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGINT); if (getppid() == 1) _exit(1); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CameraFeed::consumeVideo);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CameraFeed::consumeMessages);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_check.stop(); m_kill.stop(); consumeMessages();
        const bool recording = m_recording, failed = !m_stopping && (code != 0 || status != QProcess::NormalExit);
        m_recording = false;
        emit finished(code, failed, QString::fromUtf8(m_log), recording);
        emit changed();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_check.stop(); m_kill.stop();
        const bool recording = m_recording; m_recording = false;
        emit finished(1, !m_stopping, tr("FFmpeg could not start: %1").arg(m_process.errorString()), recording);
        emit changed();
    });
    m_check.setInterval(500);
    connect(&m_check, &QTimer::timeout, this, [this] {
        if (m_reported || !running()) return;
        if (!m_frameNumber && m_started.elapsed() > 10000) { m_reported = true; emit stalled(true); }
        else if (m_frameNumber && m_lastFrame.elapsed() > 6000) { m_reported = true; emit stalled(false); }
    });
    m_kill.setSingleShot(true); m_kill.setInterval(8000);
    connect(&m_kill, &QTimer::timeout, this, [this] { if (running()) m_process.kill(); });
}

CameraFeed::~CameraFeed() { stop(); }

QStringList CameraFeed::arguments(const QJsonObject &source, const QString &output, bool recording) {
    const QString mode = source["mode"].toString();
    const QString size = mode.section('@', 0, 0), rate = mode.section('@', 1).section(':', 0, 0), format = mode.section(':', 1);
    QStringList args{"-hide_banner", "-nostats", "-loglevel", "info", "-stats_period", "0.5", "-progress", "pipe:2", "-n",
        "-thread_queue_size", "512", "-f", "v4l2", "-input_format", format, "-video_size", size, "-framerate", rate, "-i", source["device"].toString()};
    if (recording) {
        args.insert(1, "-debug_ts");
        args.insert(args.indexOf("-f"), "-timestamps");
        args.insert(args.indexOf("-timestamps") + 1, "abs");
        args << "-map" << "0:v:0";
        if (source["encoding"] == "original" && (format == "mjpeg" || format == "h264")) args << "-c:v" << "copy";
        else args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "18" << "-pix_fmt" << "yuv420p";
        args << "-an" << "-f" << "matroska" << output;
    }
    // One input feeds both the untouched recording and a small monitoring image.
    args << "-map" << "0:v:0" << "-an" << "-vf" << "fps=30,scale=640:-2" << "-c:v" << "mjpeg" << "-q:v" << "5" << "-threads" << "2" << "-f" << "image2pipe" << "pipe:1";
    return args;
}

bool CameraFeed::valid(const QJsonObject &source, QString *error) {
    const QString name = Sources::label(source);
    auto reject = [&](const QString &message) { if (error) *error = message; return false; };
    if (!source["device"].toString().startsWith("/dev/")) return reject(tr("%1: select a camera.").arg(name));
    static const QRegularExpression mode("^[0-9]{2,5}x[0-9]{2,5}@[0-9]{1,8}/[0-9]{1,8}:(mjpeg|yuyv422|h264)$");
    if (!mode.match(source["mode"].toString()).hasMatch()) return reject(tr("%1: select a supported resolution and frame rate.").arg(name));
    const auto rate = source["mode"].toString().section('@', 1).section(':', 0, 0);
    if (rate.section('/', 0, 0).toInt() == 0 || rate.section('/', 1, 1).toInt() == 0) return reject(tr("%1: invalid frame rate.").arg(name));
    if (source["encoding"] != "original" && source["encoding"] != "h264") return reject(tr("%1: unknown recording format.").arg(name));
    return true;
}

void CameraFeed::launch(const QJsonObject &source, const QString &file) {
    stop();
    m_frames.clear(); m_messages.clear(); m_log.clear(); m_image = {}; m_jpeg.clear(); m_frameNumber = 0;
    m_lastFrame.invalidate(); m_started.start(); m_reported = false; m_recording = !file.isEmpty();
    emit frameReady();
    m_process.start(m_program, arguments(source, file, m_recording));
    m_check.start(); emit changed();
}

void CameraFeed::preview(const QJsonObject &source) { launch(source, {}); }
void CameraFeed::record(const QJsonObject &source, const QString &file) { launch(source, file); }

void CameraFeed::requestStop() {
    if (!running()) return;
    // FFmpeg's own quit command lets it write the Matroska index and trailers. The
    // recorder repeats the request while it waits, so the deadline runs from the first.
    m_process.write("q\n");
    if (!m_kill.isActive()) m_kill.start();
}

void CameraFeed::stop() {
    m_check.stop();
    if (!running()) return;
    m_stopping = true;
    m_process.write("q\n"); m_process.waitForBytesWritten(500);
    if (!m_process.waitForFinished(5000)) {
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) { m_process.kill(); m_process.waitForFinished(1000); }
    }
    m_stopping = false;
}

QString CameraFeed::state() const {
    if (!running()) return "inactive";
    if (m_lastFrame.isValid() && m_lastFrame.elapsed() > 3000) return "stalled";
    return m_frameNumber > 0 ? "live" : "waiting";
}

void CameraFeed::consumeVideo() {
    m_frames += m_process.readAllStandardOutput();
    if (m_frames.size() > 8 * 1024 * 1024) m_frames.clear();
    for (;;) {
        const auto begin = m_frames.indexOf(QByteArray::fromHex("ffd8"));
        if (begin < 0) { if (m_frames.size() > 1) m_frames = m_frames.right(1); break; }
        if (begin > 0) m_frames.remove(0, begin);
        const auto end = m_frames.indexOf(QByteArray::fromHex("ffd9"), 2);
        if (end < 0) break;
        const auto jpeg = m_frames.left(end + 2);
        const QImage image = QImage::fromData(jpeg, "JPEG");
        m_frames.remove(0, end + 2);
        if (image.isNull()) continue;
        const bool first = !m_frameNumber;
        m_image = image; m_jpeg = jpeg; m_lastFrame.start(); ++m_frameNumber; emit frameReady();
        if (first) emit changed();
    }
}

void CameraFeed::consumeMessages() {
    const QByteArray bytes = m_process.readAllStandardError();
    m_log = (m_log + bytes).right(4096);
    m_messages += bytes;
    for (;;) {
        const auto end = m_messages.indexOf('\n'); if (end < 0) break;
        const auto line = m_messages.left(end).trimmed(); m_messages.remove(0, end + 1);
        if (!m_recording) continue;
        if (const auto stamp = SessionRecorder::packetTimestamp(line)) emit timestamp(stamp);
    }
    if (m_messages.size() > 65536) m_messages.clear();
}
