#include "session.h"
#include "backend.h"
#include "sources.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QCoreApplication>
#include <QThread>
#include <signal.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

// Microphone capture is on unless turned off; desktop audio is opt-in.
static bool selected(const QJsonObject &s, const QString &id) { return s.value(id + "Enabled").toBool(id == "microphone"); }
static QJsonObject rational(double value, double rate = 60) { return {{"OTIO_SCHEMA", "RationalTime.1"}, {"value", value * rate}, {"rate", rate}}; }
static QJsonObject range(double start, double duration) { return {{"OTIO_SCHEMA", "TimeRange.1"}, {"start_time", rational(start)}, {"duration", rational(duration)}}; }
// If the app crashes, ask only its own child to finalize rather than orphaning capture.
static void finalizeWithParent(QProcess &process) { process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGINT); if (getppid() == 1) _exit(1); }); }
static QJsonObject videoEntry(const QString &id, const QString &type, const QString &source, const QString &name, const QString &track) {
    return {{"id", id}, {"kind", "Video"}, {"type", type}, {"source", source}, {"name", name}, {"track", track}, {"state", "starting"}, {"file", id + ".partial.mkv"}};
}

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
    auto reject = [&](const QString &message) { if (error) *error = message; return false; };
    const auto video = Sources::enabled(s);
    if (video.isEmpty() && !selected(s, "microphone") && !selected(s, "desktop")) return reject(tr("Enable at least one source."));
    if (selected(s, "microphone") && (s["audio"].toString().isEmpty() || s["audio"] == "none")) return reject(tr("Select a microphone, or turn its source off."));
    if (selected(s, "desktop") && s["desktopAudio"].toString().isEmpty()) return reject(tr("Select a desktop audio source."));
    for (const auto &value : video) {
        const auto source = value.toObject();
        if (source["kind"] != "screen") continue;
        if (source["target"].toString().isEmpty()) return reject(tr("%1: select a screen or window.").arg(Sources::label(source)));
        if (!QStringList{"high", "very_high", "ultra"}.contains(source["quality"].toString())) return reject(tr("%1: choose a supported screen quality preset.").arg(Sources::label(source)));
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
    : QObject(parent), m_screenProgram(screenProgram), m_audioProgram(audioProgram), m_healthProgram(healthProgram) {
    finalizeWithParent(m_probe);
    for (int i = 0; i < 2; ++i) {
        finalizeWithParent(m_audio[i]);
        connect(&m_audio[i], &QProcess::readyReadStandardError, this, [this, i] { readAudio(i); });
        connect(&m_audio[i], &QProcess::finished, this, [this, i](int code) { audioFinished(i, code); });
        connect(&m_audio[i], &QProcess::errorOccurred, this, [this, i](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) audioFinished(i, 1);
        });
    }
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

SessionRecorder::~SessionRecorder() { shutdown(); clearScreens(); }

SessionRecorder::Screen *SessionRecorder::addScreen(const QJsonObject &source, const QString &stem, const QString &tokenPath) {
    auto screen = new Screen(m_healthProgram);
    screen->source = source["id"].toString(); screen->name = Sources::label(source); screen->stem = screen->entry = stem;
    screen->target = source["target"].toString(); screen->fps = source["fps"].toInt(60); screen->quality = source["quality"].toString("very_high");
    screen->tokenPath = tokenPath;
    finalizeWithParent(screen->process);
    connect(&screen->process, &QProcess::started, this, [this, screen] {
        if (m_stopping || screen->recovering) {
            if (screen->process.processId() > 0) ::kill(screen->process.processId(), SIGINT);
            return;
        }
        if (screen->target == "portal") screen->watch.start(screen->process.processId());
    });
    connect(&screen->watch, &CaptureHealth::failed, this, [this, screen](const QString &reason) { screenFailure(screen, reason); });
    connect(&screen->watch, &CaptureHealth::unavailable, this, [this, screen] {
        if (!m_busy || m_stopping || screen->recovering) return;
        monitoringUnavailable(screen); save(); emit changed();
    });
    connect(&screen->process, &QProcess::readyReadStandardOutput, this, [this, screen] {
        const auto bytes = screen->process.readAllStandardOutput();
        if (!bytes.isEmpty()) screen->bytesUs = monotonicUs();
        emit screenData(screen->source, bytes);
    });
    connect(&screen->process, &QProcess::readyReadStandardError, this, [this, screen] { readScreenLog(screen); });
    connect(&screen->process, &QProcess::finished, this, [this, screen](int code) { screenFinished(screen, code); });
    connect(&screen->process, &QProcess::errorOccurred, this, [this, screen](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) screenFinished(screen, 1); });
    m_screens.append(screen);
    return screen;
}

void SessionRecorder::clearScreens() {
    for (const auto screen : m_screens) {
        // Their signals capture the pointer, so cut them before the children go.
        screen->process.disconnect(this); screen->watch.disconnect(this);
        screen->watch.stop();
        if (screen->process.state() != QProcess::NotRunning) { screen->process.kill(); screen->process.waitForFinished(1000); }
        delete screen;
    }
    m_screens.clear();
}

void SessionRecorder::readScreenLog(Screen *screen) {
    // Portal restore tokens are capabilities, so their diagnostics are not kept.
    for (const auto &line : screen->process.readAllStandardError().split('\n'))
        if (!line.contains("token") && (line.contains("error") || line.contains("failed") || line.contains("denied") || line.contains("Error"))) screen->log = (screen->log + line + '\n').right(8000);
}

QString SessionRecorder::interruptionWarning() const {
    QStringList parts{m_interruptionWarning};
    for (const auto screen : m_screens) parts << screen->interruptionWarning;
    parts.removeAll(QString());
    return parts.join(' ');
}

QString SessionRecorder::monitorWarning() const {
    QStringList parts{m_monitorWarning};
    for (const auto screen : m_screens) parts << screen->monitorWarning;
    parts.removeAll(QString());
    return parts.join(' ');
}

QString SessionRecorder::warning() const {
    QStringList parts{interruptionWarning(), monitorWarning(), m_timingWarning, m_warning};
    parts.removeAll(QString());
    return parts.join(' ');
}

QJsonObject SessionRecorder::entry(const QString &id) const {
    for (const auto &v : m_manifest["sources"].toArray()) if (v.toObject()["id"] == id) return v.toObject();
    return {};
}

void SessionRecorder::updateEntry(const QString &id, const QJsonObject &value) {
    auto sources = m_manifest["sources"].toArray();
    for (int i = 0; i < sources.size(); ++i) if (sources[i].toObject()["id"] == id) { sources[i] = value; break; }
    m_manifest["sources"] = sources;
}

// Every file still being written has its first timestamp. A screen segment
// that was replaced after an interruption no longer counts.
bool SessionRecorder::allStarted() const {
    for (const auto &value : m_manifest["sources"].toArray()) {
        const auto s = value.toObject();
        if (s["type"] == "screen") {
            bool current = false;
            for (const auto screen : m_screens) if (screen->entry == s["id"].toString()) current = true;
            if (!current) continue;
        }
        if (!s.contains("firstMonotonicUs")) return false;
    }
    return true;
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
    // The overall screen health is the worst of the screens being recorded.
    static const QStringList severity{"inactive", "live", "unavailable", "starting", "recovering", "failed"};
    QVariantList screens; QString health = "inactive"; int retries = 0; bool reconnecting = false;
    for (const auto screen : m_screens) {
        screens.append(QVariantMap{{"source", screen->source}, {"entry", screen->entry}, {"health", screen->health},
            {"launched", screen->launched}, {"recoveryAttempts", screen->retries}});
        if (severity.indexOf(screen->health) > severity.indexOf(health)) health = screen->health;
        retries = qMax(retries, screen->retries);
        if (screen->health == "recovering" || (screen->retries > 0 && screen->health == "starting")) reconnecting = true;
    }
    return {{"state", m_state}, {"recording", m_busy}, {"error", m_error}, {"warning", warning()},
        {"output", m_directory}, {"elapsedSeconds", first ? qMax(0.0, (end - first) / 1000000.0) : 0},
        {"screenHealth", health}, {"screenRecoveryAttempts", retries}, {"screenReconnecting", reconnecting}, {"screens", screens},
        {"bytes", double(size)}, {"sources", m_manifest["sources"].toArray().toVariantList()}, {"session", m_manifest.toVariantMap()}};
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

bool SessionRecorder::start(const QJsonObject &settings, const QHash<QString, QString> &portalTokens) {
    if (m_busy) return false;
    auto fail = [this](const QString &message) { m_error = message; m_state = "error"; emit changed(); return false; };
    QString error;
    if (!validateSources(settings, &error)) return fail(error);
    if (QStandardPaths::findExecutable("ffprobe").isEmpty() || QStandardPaths::findExecutable(m_audioProgram).isEmpty())
        return fail(tr("FFmpeg and ffprobe are required."));
    if (!Sources::enabled(settings, "screen").isEmpty() && QStandardPaths::findExecutable(m_screenProgram).isEmpty())
        return fail(tr("GPU Screen Recorder is required for screen capture."));
    const QString root = settings["directory"].toString();
    if (!QDir::isAbsolutePath(root) || !QDir().mkpath(root) || QStorageInfo(root).bytesAvailable() < 512LL * 1024 * 1024)
        return fail(tr("Choose a writable output folder with at least 512 MiB free."));
    clearScreens(); m_cameraEntries.clear(); m_camerasRunning.clear();
    stopPreview(); m_settings = settings; m_error.clear(); m_stopping = false; m_sourcesLaunched = false;
    m_warning.clear(); m_monitorWarning.clear(); m_interruptionWarning.clear(); m_timingWarning.clear();
    m_epochMono = monotonicUs(); m_epochReal = QDateTime::currentMSecsSinceEpoch() * 1000;
    m_started = m_epochMono;
    const QString name = "take-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz");
    if (!QDir(root).mkdir(name)) return fail(tr("Could not create a new session folder."));
    m_directory = QDir(root).filePath(name);
    // Cameras, then screens, then audio: the track order of the editing timeline.
    const auto video = Sources::list(settings);
    const auto stems = Sources::fileStems(video);
    QJsonArray sources; bool portal = false;
    for (const QString &kind : {QString("camera"), QString("screen")}) for (int i = 0; i < video.size(); ++i) {
        const auto source = video[i].toObject(); const QString id = source["id"].toString();
        if (source["kind"] != kind || !source["enabled"].toBool()) continue;
        sources.append(videoEntry(stems[i], kind, id, Sources::label(source), stems[i]));
        if (kind == "camera") { m_cameraEntries[id] = stems[i]; continue; }
        if (addScreen(source, stems[i], portalTokens.value(id))->target == "portal") portal = true;
    }
    for (const QString &id : {QString("microphone"), QString("desktop")})
        if (selected(settings, id)) sources.append(QJsonObject{{"id", id}, {"kind", "Audio"}, {"type", id}, {"state", "starting"}, {"file", id + ".partial.wav"}});
    m_manifest = {{"schemaVersion", 1}, {"application", "Sourceroll"}, {"state", "preparing"}, {"ownerPid", double(getpid())},
        {"startedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"epochRealtimeUs", double(m_epochReal)},
        {"epochMonotonicUs", double(m_epochMono)}, {"sources", sources}, {"settings", settings}};
    m_busy = true; m_state = portal ? "waiting-screen" : "starting"; save();
    if (m_screens.isEmpty()) launchSources(); else launchScreens();
    emit changed(); return true;
}

// Screens come first: picking one can take a while, and the other sources
// should not record that wait. Each portal capture may ask for permission, so
// those start one at a time.
void SessionRecorder::launchScreens() {
    for (const auto screen : m_screens) {
        if (!m_busy || m_stopping) return;
        if (screen->launched || (screen->target == "portal" && portalPending(screen))) continue;
        launchScreen(screen);
    }
}

// Another portal capture has been started and has not delivered a frame, so its
// permission prompt may still be open. This holds for a first capture and for a
// replacement after an interruption alike.
bool SessionRecorder::portalPending(const Screen *except) const {
    for (const auto screen : m_screens)
        if (screen != except && screen->target == "portal" && screen->launched && !screen->recovering
            && !entry(screen->entry).contains("firstMonotonicUs")) return true;
    return false;
}

void SessionRecorder::launchScreen(Screen *screen) {
    screen->launched = true; screen->recovering = false; screen->log.clear(); screen->health = "starting";
    screen->launchUs = screen->progressUs = screen->bytesUs = monotonicUs(); screen->size = 0;
    screen->process.setWorkingDirectory(m_directory);
    QStringList args{"-w", screen->target, "-f", QString::number(screen->fps),
        "-fm", "cfr", "-k", "h264", "-q", screen->quality, "-fallback-cpu-encoding", "yes", "-write-first-frame-ts", "yes",
        "-keyint", "1", "-c", "tee", "-o", screenOutput(entry(screen->entry)["file"].toString())};
    if (!screen->tokenPath.isEmpty() && screen->target == "portal")
        args << "-restore-portal-session" << "yes" << "-portal-session-token-filepath" << screen->tokenPath;
    emit screenStarting(screen->source); screen->process.start(m_screenProgram, args);
}

void SessionRecorder::monitoringUnavailable(Screen *screen) {
    screen->health = "unavailable";
    screen->monitorWarning = tr("%1: connection monitoring is unavailable. Check its preview while recording.").arg(screen->name);
}

void SessionRecorder::screenFailure(Screen *screen, const QString &reason) {
    if (!m_busy || m_stopping || screen->recovering) return;
    screen->watch.stop();
    const auto detectedUs = monotonicUs();
    qint64 firstUs = 0;
    for (const auto &value : m_manifest["sources"].toArray()) {
        const auto stamp = qint64(value.toObject()["firstMonotonicUs"].toDouble());
        if (stamp && (!firstUs || stamp < firstUs)) firstUs = stamp;
    }
    const double offset = qMax(0.0, (detectedUs - (firstUs ? firstUs : m_epochMono)) / 1000000.0);
    const QString timecode = recordingTimecode(offset);
    auto events = m_manifest["screenInterruptions"].toArray();
    events.append(QJsonObject{{"monotonicUs", double(detectedUs)}, {"source", screen->entry}, {"track", screen->stem}, {"name", screen->name},
        {"reason", reason}, {"recordingOffsetSeconds", offset}, {"recordingTimecode", timecode},
        {"detectedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    m_manifest["screenInterruptions"] = events;
    auto segment = entry(screen->entry); segment["interrupted"] = true; segment["state"] = "interrupted";
    segment["interruptedMonotonicUs"] = double(detectedUs); updateEntry(screen->entry, segment);
    if (screen->retries >= 3) {
        screen->health = "failed";
        stop(tr("%1 capture could not recover at %2 after three retries. Existing files have been kept. %3").arg(screen->name, timecode, reason)); return;
    }
    ++screen->retries; screen->recovering = true; screen->health = "recovering";
    screen->interruptionWarning = tr("%1 capture interrupted at %2. Reconnecting (attempt %3 of 3); other sources continue. There will be a gap in its track.").arg(screen->name, timecode).arg(screen->retries);
    screen->retryUs = monotonicUs();
    if (screen->process.processId() > 0) ::kill(screen->process.processId(), SIGINT);
    save(); emit changed();
}

void SessionRecorder::screenResumed(Screen *screen, qint64 monoUs) {
    auto events = m_manifest["screenInterruptions"].toArray();
    for (int i = events.size() - 1; i >= 0; --i) {
        auto event = events[i].toObject();
        if (event["track"].toString() != screen->stem) continue;
        screen->interruptionWarning = tr("%1 capture resumed. Check the interruption at %2 before editing.").arg(screen->name, event["recordingTimecode"].toString());
        event["resumedMonotonicUs"] = double(monoUs); events[i] = event;
        break;
    }
    m_manifest["screenInterruptions"] = events; save();
}

void SessionRecorder::launchSources() {
    if (m_sourcesLaunched || !m_busy || m_stopping) return;
    m_sourcesLaunched = true; m_sourcesStarted = monotonicUs();
    if (!allStarted()) m_state = "starting";
    for (const auto &value : m_manifest["sources"].toArray()) {
        const auto camera = value.toObject(); const QString source = camera["source"].toString();
        if (camera["type"] != "camera" || !m_busy || m_stopping) continue;
        m_camerasRunning.insert(source);
        emit cameraStartRequested(source, QDir(m_directory).filePath(camera["file"].toString()));
    }
    if (!m_busy || m_stopping) return;
    if (selected(m_settings, "microphone")) startAudio(0, m_settings["audio"].toString(), QDir(m_directory).filePath(entry("microphone")["file"].toString()));
    if (selected(m_settings, "desktop")) startAudio(1, m_settings["desktopAudio"].toString(), QDir(m_directory).filePath(entry("desktop")["file"].toString()));
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

void SessionRecorder::cameraTimestamp(const QString &source, qint64 realUs) {
    if (m_busy && realUs && m_cameraEntries.contains(source)) timestamp(m_cameraEntries[source], m_epochMono + realUs - m_epochReal, realUs);
}

void SessionRecorder::timestamp(const QString &id, qint64 monoUs, qint64 realUs) {
    auto s = entry(id); if (s.isEmpty() || s.contains("firstMonotonicUs")) return;
    s["firstMonotonicUs"] = double(monoUs); s["firstRealtimeUs"] = double(realUs); s["state"] = "recording"; updateEntry(id, s); save();
    // A timestamp that arrives while the take is stopping is still kept, but must not turn it back into a recording.
    if (!m_stopping && allStarted()) { m_state = "recording"; m_manifest["state"] = "recording"; save(); }
    emit changed();
}

void SessionRecorder::cameraFinished(const QString &source, int code, const QString &error) {
    m_camerasRunning.remove(source);
    const QString name = entry(m_cameraEntries.value(source))["name"].toString(tr("Camera"));
    if (m_busy && !m_stopping) stop(tr("%1 capture stopped unexpectedly. %2").arg(name, error));
    else if (code && m_busy && m_error.isEmpty()) m_error = tr("%1 did not stop cleanly. The saved file will be checked.").arg(name);
}

void SessionRecorder::screenFinished(Screen *screen, int code) {
    if (m_shutdown) return;
    screen->watch.stop(); emit screenData(screen->source, screen->process.readAllStandardOutput()); emit screenStopped(screen->source);
    readScreenLog(screen);
    if (!m_busy || screen->recovering) return;
    if (!m_stopping) {
        const QString log = QString::fromUtf8(screen->log.right(1600));
        if (entry(screen->entry).contains("firstMonotonicUs") || screen->retries) screenFailure(screen, tr("Screen recorder exited. %1").arg(log));
        else stop(tr("%1 capture stopped. %2").arg(screen->name, log));
    } else if (code != 0 && code != 255 && m_error.isEmpty()) m_error = tr("%1 did not stop cleanly; the file will be checked.").arg(screen->name);
}

void SessionRecorder::audioFinished(int index, int code) {
    if (m_shutdown) return;
    readAudio(index);
    const QString id = index == 0 ? "microphone" : "desktop";
    if (!m_busy) {
        if (m_preview && code) { m_warning = tr("%1 preview failed. Check its selected device.").arg(id); emit changed(); }
        return;
    }
    if (!m_stopping) stop(tr("%1 capture stopped. %2").arg(id, QString::fromUtf8(m_audioLog[index].right(1600))));
    else if (code != 0 && code != 255 && m_error.isEmpty()) m_error = tr("%1 did not stop cleanly; the file will be checked.").arg(id);
}

bool SessionRecorder::workersRunning() const {
    for (const auto screen : m_screens) if (screen->process.state() != QProcess::NotRunning) return true;
    return !m_camerasRunning.isEmpty() || m_audio[0].state() != QProcess::NotRunning || m_audio[1].state() != QProcess::NotRunning;
}

void SessionRecorder::stop(const QString &reason) {
    if (!m_busy || m_stopping) return;
    for (const auto screen : m_screens) { screen->watch.stop(); screen->recovering = false; }
    m_error = reason; m_stopping = true; m_state = "stopping";
    m_manifest["stoppedMonotonicUs"] = double(monotonicUs()); m_manifest["state"] = "stopping"; save();
    m_stopClock.start();
    // Request all stops together; sequential waits would lengthen later sources.
    const auto cameras = m_camerasRunning;
    for (const auto &camera : cameras) emit cameraStopRequested(camera);
    for (auto &p : m_audio) if (p.state() != QProcess::NotRunning) p.write("q\n");
    for (const auto screen : m_screens) if (screen->process.processId() > 0) ::kill(screen->process.processId(), SIGINT);
    emit changed();
}

void SessionRecorder::pollScreen(Screen *screen) {
    const auto now = monotonicUs();
    if (screen->recovering) {
        if (screen->process.state() != QProcess::NotRunning) {
            if (now - screen->retryUs > 8000000) screen->process.kill();
        } else if (now - screen->retryUs > 1000000) {
            // A replacement can ask for permission again, so it waits its turn like a first capture.
            if (screen->target == "portal" && portalPending(screen)) return;
            screen->entry = QString("%1-%2").arg(screen->stem).arg(screen->retries + 1, 3, 10, QChar('0'));
            auto sources = m_manifest["sources"].toArray();
            sources.append(videoEntry(screen->entry, "screen", screen->source, screen->name, screen->stem));
            m_manifest["sources"] = sources; save(); launchScreen(screen);
        }
        return;
    }
    if (!screen->launched) return;
    const QString file = entry(screen->entry)["file"].toString();
    if (!entry(screen->entry).contains("firstMonotonicUs")) {
        QFile ts(QDir(m_directory).filePath(screenOutput(file) + ".ts"));
        if (ts.open(QIODevice::ReadOnly)) {
            const auto values = QString::fromUtf8(ts.read(4096)).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (int i = 0; i + 1 < values.size(); ++i) {
                bool ok = false; const auto mono = values[i].toLongLong(&ok); if (!ok || mono < 1) continue;
                const auto real = values[i + 1].toLongLong(&ok); if (!ok || real < 1000000000000000LL) continue;
                timestamp(screen->entry, mono, real);
                ts.close(); ts.rename(QDir(m_directory).filePath(file + ".ts"));
                if (screen->retries) screenResumed(screen, mono);
                break;
            }
        }
    }
    const auto size = QFileInfo(QDir(m_directory).filePath(file)).size();
    if (size > screen->size) { screen->size = size; screen->progressUs = now; }
    if (entry(screen->entry).contains("firstMonotonicUs")) {
        if (screen->target != "portal" || screen->watch.linked()) screen->health = "live";
        else if (now - screen->launchUs > 10000000 && screen->health != "unavailable") { monitoringUnavailable(screen); save(); }
        // The muxer can buffer small CFR frames for a still desktop. Only
        // treat silence on BOTH outputs as a stall; a dropped preview
        // branch alone must never interrupt the primary file.
        if (now - screen->progressUs > 10000000 && now - screen->bytesUs > 10000000)
            screenFailure(screen, tr("The screen recorder stopped producing data."));
    } else if (screen->retries && now - screen->launchUs > 20000000) screenFailure(screen, tr("The replacement screen capture did not deliver a frame."));
}

void SessionRecorder::poll() {
    if (!m_busy || m_shutdown) return;
    if (m_state == "validating") {
        if (m_stopClock.isValid() && m_stopClock.elapsed() > 10000 && m_probe.state() != QProcess::NotRunning) { m_probe.kill(); }
        return;
    }
    for (const auto screen : m_screens) if (!m_stopping) pollScreen(screen);
    if (!m_stopping) launchScreens();
    if (!m_stopping) {
        const qint64 now = monotonicUs(), elapsed = now - m_epochMono;
        // The other sources start once every screen is delivering frames: not while one
        // is still waiting for its first, and not while one is reconnecting. Once they
        // have started, a later interruption no longer holds anything back.
        bool awaited = false, portalAwaited = false; QString overdue;
        for (const auto screen : m_screens) {
            if (screen->launched && !screen->recovering && entry(screen->entry).contains("firstMonotonicUs")) continue;
            awaited = true;
            const bool portal = screen->target == "portal";
            if (portal) portalAwaited = true;
            // These deadlines are for a first capture; a reconnection has its own.
            if (!screen->launched || screen->retries) continue;
            if (portal && now - screen->launchUs > 120000000) overdue = tr("Screen selection timed out or was cancelled.");
            else if (!portal && now - screen->launchUs > 20000000) overdue = tr("A source did not deliver its first frame or audio packet.");
        }
        if (!awaited) launchSources();
        else if (!m_sourcesLaunched) m_state = portalAwaited ? "waiting-screen" : "starting";
        if (m_sourcesLaunched && allStarted() && m_state != "recording") { m_state = "recording"; m_manifest["state"] = "recording"; save(); }
        if (!overdue.isEmpty()) stop(overdue);
        else if (m_state == "starting" && m_sourcesLaunched && now - m_sourcesStarted > 20000000) stop(tr("A source did not deliver its first frame or audio packet."));
        if (QStorageInfo(m_directory).bytesAvailable() < 256LL * 1024 * 1024) stop(tr("Low disk space. The session was stopped to protect its files."));
        const auto drift = (QDateTime::currentMSecsSinceEpoch() * 1000 - m_epochReal) - elapsed;
        if (qAbs(drift) > 250000) m_timingWarning = tr("The system clock changed during capture. Check alignment before editing.");
    }
    if (m_stopping && workersRunning()) {
        if (m_stopClock.elapsed() > 8000) {
            QList<QProcess *> children{&m_audio[0], &m_audio[1]};
            for (const auto screen : m_screens) children << &screen->process;
            for (QProcess *p : children) if (p->state() != QProcess::NotRunning) {
                if (m_stopClock.elapsed() > 10000) p->kill(); else p->terminate();
            }
            const auto cameras = m_camerasRunning;
            for (const auto &camera : cameras) emit cameraStopRequested(camera);
        }
    } else if (m_stopping) {
        m_state = "validating"; m_manifest["state"] = "validating"; m_validationIndex = 0; save(); validateNext();
    }
    emit changed();
}

void SessionRecorder::save() {
    if (m_directory.isEmpty()) return;
    m_manifest["error"] = m_error; m_manifest["warning"] = warning();
    m_manifest["warnings"] = QJsonObject{{"interruption", interruptionWarning()}, {"monitoring", monitorWarning()}, {"timing", m_timingWarning}, {"other", m_warning}};
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
        // Segments of one source share its track. Manifests from before sources
        // had names only mark that through the "screen-002" style of id.
        const QString id = s["id"].toString();
        const QString trackId = s.contains("track") ? s["track"].toString() : id.startsWith("screen-") ? "screen" : id;
        const double offset = (s["firstMonotonicUs"].toDouble() - first) / 1000000;
        const double duration = s["durationSeconds"].toDouble();
        if (!indices.contains(trackId)) {
            indices[trackId] = tracks.size();
            tracks.append(QJsonObject{{"OTIO_SCHEMA", "Track.1"}, {"name", s["name"].toString(trackId)}, {"kind", s["kind"]}, {"children", QJsonArray{}}});
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
        // With more than one screen in the take, say which one each time belongs to.
        QSet<QString> screenTracks;
        for (const auto &value : m_manifest["sources"].toArray()) if (value.toObject()["type"] == "screen") screenTracks.insert(value.toObject()["track"].toString());
        QStringList times;
        for (const auto &value : interruptions) {
            const auto event = value.toObject();
            const QString time = event.contains("recordingTimecode") ? event["recordingTimecode"].toString() : tr("unknown time");
            times << (screenTracks.size() > 1 ? QString("%1 %2").arg(event["name"].toString(), time) : time);
        }
        for (const auto screen : m_screens) screen->interruptionWarning.clear();
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
        stopPreview(); clearScreens(); m_cameraEntries.clear(); m_camerasRunning.clear();
        m_manifest = manifest; m_directory = directory.filePath(name); m_started = 0; m_error.clear();
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
    for (QProcess *p : {&m_audio[0], &m_audio[1]}) if (p->state() != QProcess::NotRunning) {
        p->write("q\n");
        if (!p->waitForFinished(6000)) { p->kill(); p->waitForFinished(1000); }
    }
    for (const auto screen : m_screens) if (screen->process.state() != QProcess::NotRunning) {
        ::kill(screen->process.processId(), SIGINT);
        if (!screen->process.waitForFinished(6000)) { screen->process.kill(); screen->process.waitForFinished(1000); }
    }
    if (m_busy) { m_manifest["state"] = "interrupted"; save(); }
    if (m_probe.state() != QProcess::NotRunning) { m_probe.kill(); m_probe.waitForFinished(1000); }
}
