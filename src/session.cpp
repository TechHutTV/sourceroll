#include "session.h"
#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QCoreApplication>
#include <QThread>
#include <signal.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

static bool selected(const QJsonObject &s, const QString &id) { return s.value(id + "Enabled").toBool(id == "camera" || id == "microphone"); }
static QJsonObject rational(double value, double rate = 60) { return {{"OTIO_SCHEMA", "RationalTime.1"}, {"value", value * rate}, {"rate", rate}}; }
static QJsonObject range(double start, double duration) { return {{"OTIO_SCHEMA", "TimeRange.1"}, {"start_time", rational(start)}, {"duration", rational(duration)}}; }

QString SessionRecorder::screenOutput(const QString &file) {
    // The file branch remains primary. The FIFO preview branch may drop packets
    // or fail independently, so a slow viewer cannot stall the saved recording.
    return "[f=matroska]" + file + "|[f=mpegts:onfail=ignore:use_fifo=1:fifo_options=drop_pkts_on_overflow=1]pipe:1";
}

qint64 SessionRecorder::monotonicUs() { timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000; }

QString SessionRecorder::recordingTimecode(double seconds) {
    const qint64 ms = qMax<qint64>(0, qRound64(seconds * 1000));
    return QString("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QChar('0'))
        .arg((ms / 60000) % 60, 2, 10, QChar('0')).arg((ms / 1000) % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}

qint64 SessionRecorder::packetTimestamp(const QByteArray &line) {
    // Read the device's original absolute timestamp, before FFmpeg normalizes the file to zero.
    if (!line.contains("demuxer ->") || !line.contains("pkt_pts_time:")) return 0;
    static const QRegularExpression pattern("pkt_pts_time:([0-9]+(?:\\.[0-9]+)?)");
    const auto match = pattern.match(QString::fromUtf8(line));
    if (!match.hasMatch()) return 0;
    const double value = match.captured(1).toDouble();
    return value > 1000000000 ? qRound64(value * 1000000) : 0;
}

bool SessionRecorder::validateSources(const QJsonObject &s, QString *error) {
    if (!selected(s, "camera") && !selected(s, "screen") && !selected(s, "microphone") && !selected(s, "desktop")) {
        if (error) *error = tr("Enable at least one source.");
        return false;
    }
    if (selected(s, "microphone") && (s["audio"].toString().isEmpty() || s["audio"] == "none")) {
        if (error) *error = tr("Select a microphone, or turn its source off.");
        return false;
    }
    if (selected(s, "desktop") && s["desktopAudio"].toString().isEmpty()) {
        if (error) *error = tr("Select a desktop audio source.");
        return false;
    }
    if (selected(s, "screen") && s["screenTarget"].toString().isEmpty()) {
        if (error) *error = tr("Select a screen source.");
        return false;
    }
    if (selected(s, "screen") && s.contains("screenQuality") && !QStringList{"high", "very_high", "ultra"}.contains(s["screenQuality"].toString())) {
        if (error) *error = tr("Choose a supported screen quality preset.");
        return false;
    }
    return true;
}

QStringList SessionRecorder::audioArguments(const QString &source, const QString &file, bool meter) {
    QStringList args{"-hide_banner", "-nostats", "-loglevel", "info", "-n"};
    if (!file.isEmpty()) args << "-debug_ts";
    args << "-f" << "pulse" << "-fragment_size" << "4096" << "-sample_rate" << "48000" << "-wallclock" << "1" << "-i" << source;
    if (meter) args << "-af" << "astats=metadata=1:reset=1,ametadata=print:key=lavfi.astats.Overall.Peak_level";
    if (file.isEmpty()) args << "-f" << "null" << "/dev/null";
    else args << "-c:a" << "pcm_s16le" << "-rf64" << "auto" << file;
    return args;
}

SessionRecorder::SessionRecorder(QObject *parent, const QString &screenProgram, const QString &audioProgram, const QString &healthProgram)
    : QObject(parent), m_screenProgram(screenProgram), m_audioProgram(audioProgram), m_captureHealth(nullptr, healthProgram) {
    auto child = [](QProcess &p) { p.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGINT); if (getppid() == 1) _exit(1); }); };
    child(m_screen); child(m_probe);
    connect(&m_screen, &QProcess::started, this, [this] {
        if (m_stopping || m_screenRecovering) {
            if (m_screen.processId() > 0) ::kill(m_screen.processId(), SIGINT);
            return;
        }
        if (m_settings["screenTarget"] == "portal") m_captureHealth.start(m_screen.processId());
    });
    connect(&m_captureHealth, &CaptureHealth::failed, this, &SessionRecorder::screenFailure);
    connect(&m_captureHealth, &CaptureHealth::unavailable, this, [this] {
        if (!m_busy || m_stopping || m_screenRecovering) return;
        m_screenHealth = "unavailable";
        m_monitorWarning = tr("Screen connection monitoring is unavailable. Check the screen preview while recording.");
        save(); emit changed();
    });
    connect(&m_screen, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = m_screen.readAllStandardOutput();
        if (!bytes.isEmpty()) m_screenBytesUs = monotonicUs();
        emit screenData(bytes);
    });
    for (int i = 0; i < 2; ++i) {
        child(m_audio[i]);
        connect(&m_audio[i], &QProcess::readyReadStandardError, this, [this, i] { readAudio(i); });
        connect(&m_audio[i], &QProcess::finished, this, [this, i](int code) { workerFinished(i, code); });
        connect(&m_audio[i], &QProcess::errorOccurred, this, [this, i](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) workerFinished(i, 1);
        });
    }
    connect(&m_screen, &QProcess::readyReadStandardError, this, [this] {
        for (const auto &line : m_screen.readAllStandardError().split('\n'))
            if (!line.contains("token") && (line.contains("error") || line.contains("failed") || line.contains("denied") || line.contains("Error"))) m_screenLog = (m_screenLog + line + '\n').right(8000);
    });
    connect(&m_screen, &QProcess::finished, this, [this](int code) { workerFinished(2, code); });
    connect(&m_screen, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) workerFinished(2, 1); });
    connect(&m_probe, &QProcess::finished, this, [this](int code) { probeFinished(code); });
    connect(&m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) probeFinished(1); });
    m_poll.setInterval(100); connect(&m_poll, &QTimer::timeout, this, &SessionRecorder::poll); m_poll.start();
    m_meter.setInterval(40);
    connect(&m_meter, &QTimer::timeout, this, [this] {
        bool changedLevel = false;
        for (int i = 0; i < 2; ++i) {
            double value = qMax(m_pending[i], qMax(-90.0, m_peak[i] - 20)); m_pending[i] = -90;
            if (value != m_peak[i]) { m_peak[i] = value; changedLevel = true; }
        }
        if (changedLevel) emit levels(m_peak[0], m_peak[1]);
    });
    m_meter.start();
}

SessionRecorder::~SessionRecorder() { shutdown(); }

QString SessionRecorder::warning() const {
    QStringList parts;
    for (const auto &part : {m_interruptionWarning, m_monitorWarning, m_timingWarning, m_warning}) if (!part.isEmpty()) parts << part;
    return parts.join(' ');
}

QJsonObject SessionRecorder::source(const QString &id) const {
    for (const auto &v : m_manifest["sources"].toArray()) if (v.toObject()["id"] == (id == "screen" ? m_screenId : id)) return v.toObject();
    return {};
}

void SessionRecorder::updateSource(const QString &id, const QJsonObject &value) {
    auto sources = m_manifest["sources"].toArray();
    for (int i = 0; i < sources.size(); ++i) if (sources[i].toObject()["id"] == (id == "screen" ? m_screenId : id)) { sources[i] = value; break; }
    m_manifest["sources"] = sources;
}

QVariantMap SessionRecorder::status() const {
    qint64 size = 0;
    for (const auto &v : m_manifest["sources"].toArray()) {
        const auto s = v.toObject();
        const auto file = QDir(m_directory).filePath(s["file"].toString());
        size += QFileInfo(file).size();
    }
    qint64 first = 0;
    for (const auto &v : m_manifest["sources"].toArray()) {
        const auto stamp = qint64(v.toObject()["firstMonotonicUs"].toDouble());
        if (stamp && (!first || stamp < first)) first = stamp;
    }
    const auto end = m_busy ? monotonicUs() : qint64(m_manifest["stoppedMonotonicUs"].toDouble(first));
    return {{"state", m_state}, {"recording", m_busy}, {"error", m_error}, {"warning", warning()},
        {"output", m_directory}, {"elapsedSeconds", first ? qMax(0.0, (end - first) / 1000000.0) : 0},
        {"screenHealth", m_screenHealth}, {"screenRecoveryAttempts", m_screenRetries}, {"bytes", double(size)}, {"sources", m_manifest["sources"].toArray().toVariantList()}, {"session", m_manifest.toVariantMap()}};
}

void SessionRecorder::stopPreview() {
    m_preview = false;
    for (auto &p : m_audio) if (p.state() != QProcess::NotRunning) {
        p.write("q\n"); if (!p.waitForFinished(1000)) { p.kill(); p.waitForFinished(1000); }
    }
    m_pending[0] = m_pending[1] = m_peak[0] = m_peak[1] = -90; emit levels(-90, -90);
}

void SessionRecorder::previewAudio(const QJsonObject &settings, bool enabled) {
    if (m_busy || m_shutdown) return;
    if (!enabled) { stopPreview(); return; }
    if (m_preview) return;
    m_preview = true;
    if (selected(settings, "microphone") && settings["audio"] != "none") startAudio(0, settings["audio"].toString(), {});
    if (selected(settings, "desktop")) startAudio(1, settings["desktopAudio"].toString(), {});
}

bool SessionRecorder::start(const QJsonObject &settings, const QString &portalTokenPath) {
    if (m_busy) return false;
    QString error;
    if (!validateSources(settings, &error)) { m_error = error; m_state = "error"; emit changed(); return false; }
    if (QStandardPaths::findExecutable("ffprobe").isEmpty() || QStandardPaths::findExecutable(m_audioProgram).isEmpty()) {
        m_error = tr("FFmpeg and ffprobe are required."); m_state = "error"; emit changed(); return false;
    }
    if (selected(settings, "screen") && QStandardPaths::findExecutable(m_screenProgram).isEmpty()) {
        m_error = tr("GPU Screen Recorder is required for screen capture."); m_state = "error"; emit changed(); return false;
    }
    const QString root = settings["directory"].toString();
    if (!QDir::isAbsolutePath(root) || !QDir().mkpath(root) || QStorageInfo(root).bytesAvailable() < 512LL * 1024 * 1024) {
        m_error = tr("Choose a writable output folder with at least 512 MiB free."); m_state = "error"; emit changed(); return false;
    }
    m_captureHealth.stop(); m_screenId = "screen"; m_screenRecovering = false; m_screenRetries = 0;
    m_screenHealth = selected(settings, "screen") ? "starting" : "inactive"; m_portalTokenPath = portalTokenPath;
    stopPreview(); m_settings = settings; m_error.clear(); m_warning.clear(); m_monitorWarning.clear(); m_interruptionWarning.clear(); m_timingWarning.clear(); m_stopping = false; m_sourcesLaunched = false;
    m_epochMono = monotonicUs(); m_epochReal = QDateTime::currentMSecsSinceEpoch() * 1000;
    m_started = m_epochMono;
    const QString name = "take-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz");
    if (!QDir(root).mkdir(name)) { m_error = tr("Could not create a new session folder."); m_state = "error"; emit changed(); return false; }
    m_directory = QDir(root).filePath(name);
    QJsonArray sources;
    for (const QString &id : {QString("camera"), QString("screen"), QString("microphone"), QString("desktop")}) {
        if (!selected(settings, id)) continue;
        const bool video = id == "camera" || id == "screen";
        sources.append(QJsonObject{{"id", id}, {"kind", video ? "Video" : "Audio"}, {"state", "starting"},
            {"file", id + ".partial." + (video ? "mkv" : "wav")}});
    }
    m_manifest = {{"schemaVersion", 1}, {"application", "Sourceroll"}, {"state", "preparing"}, {"ownerPid", double(getpid())},
        {"startedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"epochRealtimeUs", double(m_epochReal)},
        {"epochMonotonicUs", double(m_epochMono)}, {"sources", sources}, {"settings", settings}};
    m_busy = true; m_state = selected(settings, "screen") && settings["screenTarget"] == "portal" ? "waiting-screen" : "starting"; save();
    if (selected(settings, "screen")) launchScreen(); else launchSources();
    emit changed(); return true;
}

void SessionRecorder::launchScreen() {
    m_screenRecovering = false; m_screenLog.clear(); m_screenHealth = "starting";
    m_screenLaunchUs = m_screenProgressUs = m_screenBytesUs = monotonicUs(); m_screenSize = 0;
    m_screen.setWorkingDirectory(m_directory);
    QStringList args{"-w", m_settings["screenTarget"].toString(), "-f", QString::number(m_settings["screenFps"].toInt(60)),
        "-fm", "cfr", "-k", "h264", "-q", m_settings["screenQuality"].toString("very_high"), "-fallback-cpu-encoding", "yes", "-write-first-frame-ts", "yes",
        "-keyint", "1", "-c", "tee", "-o", screenOutput(source("screen")["file"].toString())};
    if (!m_portalTokenPath.isEmpty() && m_settings["screenTarget"] == "portal")
        args << "-restore-portal-session" << "yes" << "-portal-session-token-filepath" << m_portalTokenPath;
    emit screenStarting(); m_screen.start(m_screenProgram, args);
}

void SessionRecorder::screenFailure(const QString &reason) {
    if (!m_busy || m_stopping || m_screenRecovering) return;
    m_captureHealth.stop();
    const auto detectedUs = monotonicUs();
    qint64 firstUs = 0;
    for (const auto &value : m_manifest["sources"].toArray()) {
        const auto stamp = qint64(value.toObject()["firstMonotonicUs"].toDouble());
        if (stamp && (!firstUs || stamp < firstUs)) firstUs = stamp;
    }
    const double offset = qMax(0.0, (detectedUs - (firstUs ? firstUs : m_epochMono)) / 1000000.0);
    const QString timecode = recordingTimecode(offset);
    auto events = m_manifest["screenInterruptions"].toArray();
    events.append(QJsonObject{{"monotonicUs", double(detectedUs)}, {"source", m_screenId}, {"reason", reason},
        {"recordingOffsetSeconds", offset}, {"recordingTimecode", timecode},
        {"detectedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    m_manifest["screenInterruptions"] = events;
    auto segment = source("screen"); segment["interrupted"] = true; segment["state"] = "interrupted";
    segment["interruptedMonotonicUs"] = double(detectedUs); updateSource("screen", segment);
    if (m_screenRetries >= 3) {
        m_screenHealth = "failed";
        stop(tr("Screen capture could not recover at %1 after three retries. Existing files have been kept. %2").arg(timecode, reason)); return;
    }
    ++m_screenRetries; m_screenRecovering = true; m_screenHealth = "recovering";
    m_interruptionWarning = tr("Screen capture interrupted at %1. Reconnecting (attempt %2 of 3); camera and audio continue. There will be a gap in the screen track.").arg(timecode).arg(m_screenRetries);
    m_retryUs = monotonicUs();
    if (m_screen.processId() > 0) ::kill(m_screen.processId(), SIGINT);
    save(); emit changed();
}

void SessionRecorder::launchSources() {
    if (m_sourcesLaunched || !m_busy || m_stopping) return;
    m_sourcesLaunched = true; m_sourcesStarted = monotonicUs(); m_state = "starting";
    if (selected(m_settings, "camera")) { m_cameraRunning = true; emit cameraStartRequested(QDir(m_directory).filePath(source("camera")["file"].toString())); }
    if (selected(m_settings, "microphone")) startAudio(0, m_settings["audio"].toString(), QDir(m_directory).filePath(source("microphone")["file"].toString()));
    if (selected(m_settings, "desktop")) startAudio(1, m_settings["desktopAudio"].toString(), QDir(m_directory).filePath(source("desktop")["file"].toString()));
}

void SessionRecorder::startAudio(int index, const QString &device, const QString &file) {
    m_audioLines[index].clear(); m_audioLog[index].clear();
    m_audio[index].start(m_audioProgram, audioArguments(device, file, true));
}

void SessionRecorder::readAudio(int index) {
    auto bytes = m_audio[index].readAllStandardError();
    m_audioLog[index] = (m_audioLog[index] + bytes).right(4000); m_audioLines[index] += bytes;
    for (;;) {
        const auto end = m_audioLines[index].indexOf('\n'); if (end < 0) break;
        const auto line = m_audioLines[index].left(end); m_audioLines[index].remove(0, end + 1);
        if (const auto peak = Backend::parsePeak(line)) m_pending[index] = qMax(m_pending[index], *peak);
        const auto stamp = packetTimestamp(line);
        if (m_busy && stamp) timestamp(index == 0 ? "microphone" : "desktop", m_epochMono + stamp - m_epochReal, stamp);
    }
    if (m_audioLines[index].size() > 65536) m_audioLines[index].clear();
}

void SessionRecorder::cameraTimestamp(qint64 realUs) { if (m_busy && realUs) timestamp("camera", m_epochMono + realUs - m_epochReal, realUs); }

void SessionRecorder::timestamp(const QString &id, qint64 monoUs, qint64 realUs) {
    auto s = source(id); if (s.isEmpty() || s.contains("firstMonotonicUs")) return;
    s["firstMonotonicUs"] = double(monoUs); s["firstRealtimeUs"] = double(realUs); s["state"] = "recording"; updateSource(id, s); save();
    bool ready = true;
    for (const auto &value : m_manifest["sources"].toArray()) if ((value.toObject()["id"] == m_screenId || !value.toObject()["id"].toString().startsWith("screen")) && !value.toObject().contains("firstMonotonicUs")) ready = false;
    if (ready) { m_state = "recording"; m_manifest["state"] = "recording"; save(); }
    emit changed();
}

void SessionRecorder::cameraFinished(int code, const QString &error) {
    m_cameraRunning = false;
    if (m_busy && !m_stopping) stop(tr("Camera capture stopped unexpectedly. %1").arg(error));
    else if (code && m_busy && m_error.isEmpty()) m_error = tr("The camera did not stop cleanly. The saved file will be checked.");
}

void SessionRecorder::workerFinished(int index, int code) {
    if (m_shutdown) return;
    if (index == 2) { m_captureHealth.stop(); emit screenData(m_screen.readAllStandardOutput()); emit screenStopped(); }
    if (index == 2) {
        for (const auto &line : m_screen.readAllStandardError().split('\n'))
            if (!line.contains("token") && (line.contains("error") || line.contains("failed") || line.contains("denied") || line.contains("Error"))) m_screenLog = (m_screenLog + line + '\n').right(8000);
    }
    else readAudio(index);
    const QString id = index == 2 ? "screen" : index == 0 ? "microphone" : "desktop";
    if (!m_busy) {
        if (m_preview && code) { m_warning = tr("%1 preview failed. Check its selected device.").arg(id); emit changed(); }
        return;
    }
    if (index == 2 && m_screenRecovering) return;
    if (!m_stopping) {
        const QString log = QString::fromUtf8(index == 2 ? m_screenLog.right(1600) : m_audioLog[index].right(1600));
        if (index == 2 && (source("screen").contains("firstMonotonicUs") || m_screenRetries)) screenFailure(tr("Screen recorder exited. %1").arg(log));
        else stop(tr("%1 capture stopped. %2").arg(id, log));
    } else if (code != 0 && code != 255 && m_error.isEmpty()) m_error = tr("%1 did not stop cleanly; the file will be checked.").arg(id);
}

bool SessionRecorder::workersRunning() const {
    return m_cameraRunning || m_screen.state() != QProcess::NotRunning || m_audio[0].state() != QProcess::NotRunning || m_audio[1].state() != QProcess::NotRunning;
}

void SessionRecorder::stop(const QString &reason) {
    if (!m_busy || m_stopping) return;
    m_captureHealth.stop(); m_screenRecovering = false;
    m_error = reason; m_stopping = true; m_state = "stopping";
    m_manifest["stoppedMonotonicUs"] = double(monotonicUs()); m_manifest["state"] = "stopping"; save();
    m_stopClock.start();
    // Request all stops together; sequential waits would lengthen later sources.
    if (m_cameraRunning) emit cameraStopRequested();
    for (auto &p : m_audio) if (p.state() != QProcess::NotRunning) p.write("q\n");
    if (m_screen.processId() > 0) ::kill(m_screen.processId(), SIGINT);
    emit changed();
}

void SessionRecorder::poll() {
    if (!m_busy || m_shutdown) return;
    if (m_state == "validating") {
        if (m_stopClock.isValid() && m_stopClock.elapsed() > 10000 && m_probe.state() != QProcess::NotRunning) { m_probe.kill(); }
        return;
    }
    if (!m_stopping && m_screenRecovering) {
        if (m_screen.state() != QProcess::NotRunning) {
            if (monotonicUs() - m_retryUs > 8000000) m_screen.kill();
        } else if (monotonicUs() - m_retryUs > 1000000) {
            m_screenId = QString("screen-%1").arg(m_screenRetries + 1, 3, 10, QChar('0'));
            auto sources = m_manifest["sources"].toArray();
            sources.append(QJsonObject{{"id", m_screenId}, {"kind", "Video"}, {"state", "starting"}, {"file", m_screenId + ".partial.mkv"}});
            m_manifest["sources"] = sources; save(); launchScreen();
        }
    }
    if (!m_stopping && !m_screenRecovering && selected(m_settings, "screen") && !source("screen").contains("firstMonotonicUs")) {
        QFile ts(QDir(m_directory).filePath(screenOutput(source("screen")["file"].toString()) + ".ts"));
        if (ts.open(QIODevice::ReadOnly)) {
            const auto values = QString::fromUtf8(ts.read(4096)).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (int i = 0; i + 1 < values.size(); ++i) {
                bool ok = false; const auto mono = values[i].toLongLong(&ok); if (!ok || mono < 1) continue;
                const auto real = values[i + 1].toLongLong(&ok); if (!ok || real < 1000000000000000LL) continue;
                timestamp("screen", mono, real);
                ts.close(); ts.rename(QDir(m_directory).filePath(source("screen")["file"].toString() + ".ts"));
                if (m_screenRetries) {
                    auto events = m_manifest["screenInterruptions"].toArray();
                    auto event = events.last().toObject();
                    m_interruptionWarning = tr("Screen capture resumed. Check the interruption at %1 before editing.").arg(event["recordingTimecode"].toString());
                    event["resumedMonotonicUs"] = double(mono); events.last() = event;
                    m_manifest["screenInterruptions"] = events; save();
                }
                launchSources(); break;
            }
        }
    }
    if (!m_stopping && !m_screenRecovering && selected(m_settings, "screen")) {
        const auto now = monotonicUs();
        const auto size = QFileInfo(QDir(m_directory).filePath(source("screen")["file"].toString())).size();
        if (size > m_screenSize) { m_screenSize = size; m_screenProgressUs = now; }
        if (source("screen").contains("firstMonotonicUs")) {
            if (m_settings["screenTarget"] != "portal" || m_captureHealth.linked()) m_screenHealth = "live";
            else if (now - m_screenLaunchUs > 10000000 && m_screenHealth != "unavailable") {
                m_screenHealth = "unavailable";
                m_monitorWarning = tr("Screen connection monitoring is unavailable. Check the screen preview while recording."); save();
            }
            // The muxer can buffer small CFR frames for a still desktop. Only
            // treat silence on BOTH outputs as a stall; a dropped preview
            // branch alone must never interrupt the primary file.
            if (now - m_screenProgressUs > 10000000 && now - m_screenBytesUs > 10000000)
                screenFailure(tr("The screen recorder stopped producing data."));
        } else if (m_screenRetries && now - m_screenLaunchUs > 20000000) screenFailure(tr("The replacement screen capture did not deliver a frame."));
    }
    if (!m_stopping) {
        const qint64 elapsed = monotonicUs() - m_epochMono;
        bool ready = m_sourcesLaunched;
        for (const auto &v : m_manifest["sources"].toArray()) if ((v.toObject()["id"] == m_screenId || !v.toObject()["id"].toString().startsWith("screen")) && !v.toObject().contains("firstMonotonicUs")) ready = false;
        if (ready && m_state != "recording") { m_state = "recording"; m_manifest["state"] = "recording"; save(); }
        if (m_state == "waiting-screen" && elapsed > 120000000) stop(tr("Screen selection timed out or was cancelled."));
        else if (m_state == "starting" && ((m_sourcesLaunched && monotonicUs() - m_sourcesStarted > 20000000) || (!m_sourcesLaunched && elapsed > 20000000))) stop(tr("A source did not deliver its first frame or audio packet."));
        if (QStorageInfo(m_directory).bytesAvailable() < 256LL * 1024 * 1024) stop(tr("Low disk space. The session was stopped to protect its files."));
        const auto drift = (QDateTime::currentMSecsSinceEpoch() * 1000 - m_epochReal) - elapsed;
        if (qAbs(drift) > 250000) m_timingWarning = tr("The system clock changed during capture. Check alignment before editing.");
    }
    if (m_stopping && workersRunning()) {
        if (m_stopClock.elapsed() > 8000) {
            for (QProcess *p : {&m_screen, &m_audio[0], &m_audio[1]}) if (p->state() != QProcess::NotRunning) {
                if (m_stopClock.elapsed() > 10000) p->kill(); else p->terminate();
            }
            if (m_cameraRunning) emit cameraStopRequested();
        }
    } else if (m_stopping) {
        m_state = "validating"; m_manifest["state"] = "validating"; m_validationIndex = 0; save(); validateNext();
    }
    emit changed();
}

void SessionRecorder::save() {
    if (m_directory.isEmpty()) return;
    m_manifest["error"] = m_error; m_manifest["warning"] = warning();
    m_manifest["warnings"] = QJsonObject{{"interruption", m_interruptionWarning}, {"monitoring", m_monitorWarning}, {"timing", m_timingWarning}, {"other", m_warning}};
    QSaveFile file(QDir(m_directory).filePath("session.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(m_manifest).toJson()) < 0 || !file.commit()) {
        if (m_error.isEmpty()) m_error = tr("Could not save session metadata. Check disk space and permissions.");
        if (m_busy && !m_stopping) QTimer::singleShot(0, this, [this] { stop(m_error); });
    }
}

void SessionRecorder::validateNext() {
    const auto sources = m_manifest["sources"].toArray();
    if (m_validationIndex >= sources.size()) { complete(); return; }
    const auto s = sources[m_validationIndex].toObject();
    const QString file = QDir(m_directory).filePath(s["file"].toString());
    m_stopClock.restart();
    m_probe.start("ffprobe", {"-v", "error", "-show_entries", "stream=codec_type,codec_name,width,height,r_frame_rate,sample_rate,channels:format=duration,start_time,size", "-of", "json", file});
}

void SessionRecorder::probeFinished(int code) {
    if (!m_busy || m_state != "validating") return;
    auto sources = m_manifest["sources"].toArray();
    if (m_validationIndex >= sources.size()) return;
    auto s = sources[m_validationIndex].toObject();
    const auto probe = QJsonDocument::fromJson(m_probe.readAllStandardOutput()).object();
    const auto duration = probe["format"].toObject()["duration"].toString().toDouble();
    bool expectedStream = false;
    for (const auto &v : probe["streams"].toArray()) if (v.toObject()["codec_type"] == (s["kind"] == "Video" ? "video" : "audio")) expectedStream = true;
    if (code != 0 || !expectedStream || duration <= 0) {
        s["state"] = "incomplete"; s["validationError"] = QString::fromUtf8(m_probe.readAllStandardError()).left(2000);
        if (m_error.isEmpty()) m_error = tr("One or more files could not be validated. Partial files have been kept.");
    } else {
        s["durationSeconds"] = duration; s["probe"] = probe;
        QString final = s["file"].toString(); final.replace(".partial.", ".");
        if (final != s["file"].toString()) {
            if (QFile::rename(QDir(m_directory).filePath(s["file"].toString()), QDir(m_directory).filePath(final))) s["file"] = final;
            else m_warning = tr("A validated file kept its partial filename because it could not be renamed.");
        }
        s["state"] = "saved";
    }
    sources[m_validationIndex] = s; m_manifest["sources"] = sources; ++m_validationIndex; save();
    QTimer::singleShot(0, this, &SessionRecorder::validateNext);
}

QJsonObject SessionRecorder::timeline(const QJsonObject &manifest, const QString &directory) {
    qint64 first = 0;
    for (const auto &v : manifest["sources"].toArray()) {
        const auto s = v.toObject(); const auto t = qint64(s["firstMonotonicUs"].toDouble());
        if (s["state"] == "saved" && t && (!first || t < first)) first = t;
    }
    if (!first) return {};
    QJsonArray tracks;
    QHash<QString, int> indices;
    QHash<QString, double> ends;
    for (const auto &v : manifest["sources"].toArray()) {
        const auto s = v.toObject();
        if (s["state"] != "saved" || !s.contains("firstMonotonicUs")) continue;
        const QString id = s["id"].toString(), trackId = id.startsWith("screen-") ? "screen" : id;
        const double offset = (s["firstMonotonicUs"].toDouble() - first) / 1000000;
        const double duration = s["durationSeconds"].toDouble();
        if (!indices.contains(trackId)) {
            indices[trackId] = tracks.size();
            tracks.append(QJsonObject{{"OTIO_SCHEMA", "Track.1"}, {"name", trackId}, {"kind", s["kind"]}, {"children", QJsonArray{}}});
        }
        auto track = tracks[indices[trackId]].toObject(); auto children = track["children"].toArray();
        if (offset > ends[trackId]) children.append(QJsonObject{{"OTIO_SCHEMA", "Gap.1"}, {"source_range", range(0, offset - ends[trackId])}});
        // A finalized CFR segment may extend slightly past the next start. Trim
        // its tail to retain measured timing instead of shifting later clips.
        if (offset < ends[trackId] && !children.isEmpty()) {
            auto previous = children.last().toObject(); auto sourceRange = previous["source_range"].toObject();
            const double oldDuration = sourceRange["duration"].toObject()["value"].toDouble() / 60;
            sourceRange["duration"] = rational(qMax(0.0, oldDuration - (ends[trackId] - offset)));
            previous["source_range"] = sourceRange; children.last() = previous;
        }
        children.append(QJsonObject{{"OTIO_SCHEMA", "Clip.2"}, {"name", id}, {"source_range", range(0, duration)},
            {"media_references", QJsonObject{{"DEFAULT_MEDIA", QJsonObject{{"OTIO_SCHEMA", "ExternalReference.1"}, {"target_url", QUrl::fromLocalFile(QDir(directory).filePath(s["file"].toString())).toString()}, {"available_range", range(0, duration)}}}}},
            {"active_media_reference_key", "DEFAULT_MEDIA"}});
        track["children"] = children; tracks[indices[trackId]] = track; ends[trackId] = offset + duration;
    }
    return {{"OTIO_SCHEMA", "Timeline.1"}, {"name", QFileInfo(directory).fileName()}, {"global_start_time", rational(0)},
        {"tracks", QJsonObject{{"OTIO_SCHEMA", "Stack.1"}, {"children", tracks}}}};
}

void SessionRecorder::complete() {
    const auto interruptions = m_manifest["screenInterruptions"].toArray();
    if (!interruptions.isEmpty() && m_error.isEmpty()) {
        QStringList times;
        for (const auto &value : interruptions) {
            const auto event = value.toObject();
            times << (event.contains("recordingTimecode") ? event["recordingTimecode"].toString() : tr("unknown time"));
        }
        m_interruptionWarning = tr("Saved with screen interruptions at %1. Check these points and the timeline gaps before editing.").arg(times.join(", "));
    }
    qint64 first = 0; bool timingMissing = false;
    auto sources = m_manifest["sources"].toArray();
    for (const auto &v : sources) { const auto t = qint64(v.toObject()["firstMonotonicUs"].toDouble()); if (t && (!first || t < first)) first = t; }
    for (int i = 0; i < sources.size(); ++i) {
        auto s = sources[i].toObject();
        if (s.contains("firstMonotonicUs")) s["startOffsetSeconds"] = (s["firstMonotonicUs"].toDouble() - first) / 1000000;
        else { s["timingUnknown"] = true; timingMissing = true; }
        sources[i] = s;
    }
    m_manifest["sources"] = sources;
    const QString missing = tr("Some timing information is missing. Align those files manually.");
    if (timingMissing && !m_timingWarning.contains(missing)) m_timingWarning = QStringList{m_timingWarning, missing}.join(' ').trimmed();
    const auto edit = timeline(m_manifest, m_directory);
    if (!edit.isEmpty()) {
        QSaveFile file(QDir(m_directory).filePath("timeline.otio"));
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(edit).toJson()); if (file.commit()) m_manifest["timeline"] = "timeline.otio"; }
    }
    m_busy = false; m_stopping = false; m_state = m_error.isEmpty() ? "saved" : "error";
    m_manifest["state"] = m_error.isEmpty() ? "complete" : "incomplete";
    m_manifest["hasScreenGaps"] = !m_manifest["screenInterruptions"].toArray().isEmpty(); save(); emit changed(); emit finished();
}

bool SessionRecorder::recover(const QString &root) {
    if (m_busy) return false;
    QDir directory(root);
    const auto folders = directory.entryList({"take-*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const auto &name : folders) {
        QFile file(directory.filePath(name + "/session.json")); if (!file.open(QIODevice::ReadOnly)) continue;
        auto manifest = QJsonDocument::fromJson(file.read(1024 * 1024)).object();
        if (manifest["schemaVersion"] != 1 || manifest["sources"].toArray().isEmpty() || manifest["state"] == "complete") continue;
        const auto pid = qint64(manifest["ownerPid"].toDouble());
        if (pid != getpid() && pid > 0 && ::kill(pid, 0) == 0) continue;
        bool safe = true;
        for (const auto &v : manifest["sources"].toArray()) { const auto name = v.toObject()["file"].toString(); if (name.isEmpty() || name != QFileInfo(name).fileName()) safe = false; }
        if (!safe) continue;
        stopPreview(); m_manifest = manifest; m_directory = directory.filePath(name); m_started = 0; m_error.clear();
        const auto parts = manifest["warnings"].toObject();
        m_interruptionWarning = parts["interruption"].toString(); m_monitorWarning = parts["monitoring"].toString(); m_timingWarning = parts["timing"].toString();
        m_warning = manifest.contains("warnings") ? parts["other"].toString() : manifest["warning"].toString();
        m_busy = true; m_stopping = false; m_state = "validating"; m_validationIndex = 0; save(); validateNext(); emit changed(); return true;
    }
    m_warning = tr("No interrupted session was found in this folder."); emit changed(); return false;
}

void SessionRecorder::shutdown() {
    if (m_shutdown) return;
    if (m_busy && !m_stopping) stop();
    // The camera owner has already requested its stop. Keep processing completion
    // and validation events so a normal quit produces a complete session.
    QElapsedTimer deadline; deadline.start();
    while (m_busy && deadline.elapsed() < 18000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    m_shutdown = true;
    for (QProcess *p : {&m_screen, &m_audio[0], &m_audio[1]}) if (p->state() != QProcess::NotRunning) {
        if (p != &m_screen) p->write("q\n"); else ::kill(p->processId(), SIGINT);
        if (!p->waitForFinished(6000)) { p->kill(); p->waitForFinished(1000); }
    }
    if (m_busy) { m_manifest["state"] = "interrupted"; save(); }
    if (m_probe.state() != QProcess::NotRunning) { m_probe.kill(); m_probe.waitForFinished(1000); }
}
