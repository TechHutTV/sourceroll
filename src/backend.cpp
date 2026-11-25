#include "backend.h"
#include "session.h"

#include <QDateTime>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusConnection>
#include <QDesktopServices>
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
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <cmath>
#include <functional>

static QByteArray query(const QString &program, const QStringList &args) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(2500)) { process.kill(); process.waitForFinished(500); return {}; }
    return process.exitCode() == 0 ? process.readAllStandardOutput() : QByteArray();
}

bool Backend::omaControlOpen(const QByteArray &layers) {
    if (layers.size() > 1024 * 1024) return false;
    const auto doc = QJsonDocument::fromJson(layers);
    // hyprctl layers: monitor -> levels -> arrays of layer surfaces.
    std::function<bool(const QJsonValue &, int)> contains = [&](const QJsonValue &v, int depth) {
        if (depth > 8) return false;
        if (v.isObject()) {
            const auto object = v.toObject();
            if (object["namespace"] == "omacontrol") return true;
            for (const auto &child : object) if (contains(child, depth + 1)) return true;
        } else if (v.isArray()) for (const auto &child : v.toArray()) if (contains(child, depth + 1)) return true;
        return false;
    };
    return doc.isObject() && contains(doc.object(), 0);
}

QString Backend::socketPath() {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/sourceroll.sock";
}

QVariantList Backend::cameraModes(const QString &device) {
    QVariantList result;
    int fd = ::open(QFile::encodeName(device).constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return result;
    v4l2_fmtdesc format{}; format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (; ioctl(fd, VIDIOC_ENUM_FMT, &format) == 0; ++format.index) {
        QString input;
        if (format.pixelformat == V4L2_PIX_FMT_MJPEG) input = "mjpeg";
        else if (format.pixelformat == V4L2_PIX_FMT_YUYV) input = "yuyv422";
        else if (format.pixelformat == V4L2_PIX_FMT_H264) input = "h264";
        else continue;
        v4l2_frmsizeenum size{}; size.pixel_format = format.pixelformat;
        for (; ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
            if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) continue;
            v4l2_frmivalenum interval{};
            interval.pixel_format = format.pixelformat;
            interval.width = size.discrete.width; interval.height = size.discrete.height;
            for (; ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0; ++interval.index) {
                if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE || !interval.discrete.numerator) continue;
                const auto fps = double(interval.discrete.denominator) / interval.discrete.numerator;
                if (fps > 120 || fps < 1) continue;
                QString rate = QString::number(interval.discrete.denominator) + "/" + QString::number(interval.discrete.numerator);
                QString dimensions = QString("%1x%2").arg(size.discrete.width).arg(size.discrete.height);
                result.append(QVariantMap{{"id", dimensions + "@" + rate + ":" + input},
                    {"label", QString("%1 × %2 · %3 fps · %4").arg(size.discrete.width).arg(size.discrete.height).arg(fps, 0, 'f', fps == std::floor(fps) ? 0 : 2).arg(input.toUpper())},
                    {"size", dimensions}, {"rate", rate}, {"format", input}});
            }
        }
    }
    ::close(fd);
    return result;
}

Backend::Backend(const QString &configFile, QObject *parent) : QObject(parent),
    m_portalCache(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/sourceroll-screen-XXXXXX") {
    m_screenPreviewProcess.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGINT); if (getppid() == 1) _exit(1); });
    m_screenSelectTimer.setSingleShot(true); m_screenSelectTimer.setInterval(120000);
    connect(&m_screenSelectTimer, &QTimer::timeout, this, [this] {
        if (!m_screenPreviewHasData && m_screenPreviewProcess.state() != QProcess::NotRunning) {
            stopScreenPreview(); m_screenSelectionError = tr("Screen selection timed out. Choose the source again."); emit statusChanged();
        }
    });
    connect(&m_screenPreviewProcess, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = m_screenPreviewProcess.readAllStandardOutput();
        if (!bytes.isEmpty() && !m_screenPreviewStopping) {
            m_screenSelectTimer.stop(); m_screenPreviewHasData = true; m_screenSelected = true; m_screenMonitor.feed(bytes); emit statusChanged();
        }
    });
    connect(&m_screenPreviewProcess, &QProcess::readyReadStandardError, this, [this] {
        // Portal restore tokens are capabilities, so do not retain the recorder's token diagnostics.
        const auto data = m_screenPreviewProcess.readAllStandardError();
        for (const auto &line : data.split('\n')) if (!line.contains("token") && (line.contains("error") || line.contains("failed") || line.contains("denied"))) m_screenPreviewLog = (m_screenPreviewLog + line + '\n').right(2000);
    });
    connect(&m_screenPreviewProcess, &QProcess::finished, this, [this](int code) {
        m_screenSelectTimer.stop();
        if (!m_screenPreviewStopping && !m_shuttingDown) {
            m_screenMonitor.stop();
            m_screenSelectionError = tr("Screen sharing ended or was cancelled. Choose the source again. %1").arg(QString::fromUtf8(m_screenPreviewLog.right(1200)));
            if (code) m_screenSelected = false;
            emit statusChanged();
        }
    });
    connect(&m_screenPreviewProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { m_screenSelectionError = tr("Screen preview could not start: %1").arg(m_screenPreviewProcess.errorString()); m_screenMonitor.stop(); emit statusChanged(); }
    });
    // If the app crashes, ask only its own child to finalize rather than orphaning capture.
    m_process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGINT); if (getppid() == 1) _exit(1); });
    m_configFile = configFile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.json" : configFile;
    QString migrationError;
    if (configFile.isEmpty()) importLegacySettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/multitake/settings.json", m_configFile, &migrationError);
    if (configFile.isEmpty()) importLegacySettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/source-record/settings.json", m_configFile, &migrationError);
    QFile file(m_configFile);
    if (file.open(QIODevice::ReadOnly)) m_settings = QJsonDocument::fromJson(file.read(65536)).object();
    if (!m_settings.contains("directory")) m_settings["directory"] = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Sourceroll";
    if (!m_settings.contains("encoding")) m_settings["encoding"] = "original";
    if (!m_settings.contains("cameraEnabled")) m_settings["cameraEnabled"] = true;
    if (!m_settings.contains("screenEnabled")) m_settings["screenEnabled"] = false;
    if (!m_settings.contains("microphoneEnabled")) m_settings["microphoneEnabled"] = m_settings["audio"] != "none";
    if (!m_settings.contains("desktopEnabled")) m_settings["desktopEnabled"] = false;
    if (!m_settings.contains("screenTarget")) m_settings["screenTarget"] = "portal";
    if (!m_settings.contains("screenFps")) m_settings["screenFps"] = 60;
    if (!m_settings.contains("screenQuality")) m_settings["screenQuality"] = "very_high";
    m_session = new SessionRecorder(this);
    connect(m_session, &SessionRecorder::screenStarting, &m_screenMonitor, &ScreenMonitor::start);
    connect(m_session, &SessionRecorder::screenStopped, &m_screenMonitor, &ScreenMonitor::stop);
    connect(m_session, &SessionRecorder::screenData, &m_screenMonitor, &ScreenMonitor::feed);
    connect(m_session, &SessionRecorder::screenData, this, [this](const QByteArray &bytes) { if (!bytes.isEmpty()) m_screenSelected = true; });
    connect(&m_screenMonitor, &ScreenMonitor::frameReady, this, &Backend::screenFrameReady);
    connect(m_session, &SessionRecorder::changed, this, &Backend::statusChanged);
    connect(m_session, &SessionRecorder::levels, this, [this](double mic, double desktop) { m_peak = mic; m_desktopPeak = desktop; emit peakChanged(); });
    connect(m_session, &SessionRecorder::cameraStartRequested, this, [this](const QString &file) {
        m_lastFrame.invalidate(); m_frameNumber = 0; m_image = {}; m_previewJpeg.clear(); emit frameReady();
        m_output = file; m_recording = true; m_state = "starting"; m_frames.clear(); m_messages.clear(); m_log.clear();
        m_process.start("ffmpeg", ffmpegArguments(m_settings, file, true)); m_watchdog.start();
    });
    connect(m_session, &SessionRecorder::cameraStopRequested, this, [this] {
        if (m_process.state() != QProcess::NotRunning) { m_process.write("q\n");
            QTimer::singleShot(8000, this, [this] { if (m_stopping || m_session->busy()) { if (m_process.state() != QProcess::NotRunning) m_process.kill(); } });
        }
    });
    connect(m_session, &SessionRecorder::finished, this, [this] { m_recording = false; m_state = "idle"; if (!m_shuttingDown && (m_visible || m_previewClients)) { m_restart.start(); if (m_settings["screenEnabled"].toBool() && m_screenSelected) startScreenPreview(false); } });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Backend::consumeVideo);
    connect(&m_process, &QProcess::readyReadStandardError, this, &Backend::consumeMessages);
    connect(&m_process, &QProcess::finished, this, &Backend::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { if (m_session->busy()) { m_session->cameraFinished(1, m_process.errorString()); return; } m_recording = false; fail(tr("FFmpeg could not start: %1").arg(m_process.errorString())); }
    });
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if ((m_state == "recording" || m_state == "preview") && m_lastFrame.isValid() && m_lastFrame.elapsed() > 6000) {
            if (m_session->busy()) m_session->stop(tr("The camera stopped delivering frames."));
            else { stopProcess(); fail(tr("The camera stopped delivering frames. Check the device before recording again.")); }
        }
        emit statusChanged();
    });
    m_tick.start();
    m_restart.setSingleShot(true); m_restart.setInterval(250);
    connect(&m_restart, &QTimer::timeout, this, [this] { if ((m_visible || m_previewClients > 0) && !m_session->busy()) launch(false); });
    m_watchdog.setInterval(10000);
    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (m_state == "starting" || m_state == "preview-starting") {
            if (m_session->busy()) { m_session->stop(tr("The camera did not deliver its first frame.")); return; }
            stopProcess();
            fail(tr("The camera did not deliver a frame. Check that it is connected and not in use by another app."));
        }
    });
    m_displayPoll.setInterval(1000);
    connect(&m_displayPoll, &QTimer::timeout, this, [this] {
        if (!m_session->busy() || !m_settings["screenEnabled"].toBool()) { m_displaySettingsOpen = false; return; }
        if (m_displayQuery.state() != QProcess::NotRunning) { m_displayQuery.kill(); return; }
        m_displayQuery.start("hyprctl", {"-j", "layers"});
    });
    connect(&m_displayQuery, &QProcess::readyReadStandardError, this, [this] { m_displayQuery.readAllStandardError(); });
    connect(&m_displayQuery, &QProcess::finished, this, [this](int code) {
        m_displaySettingsOpen = code == 0 && m_session->busy() && omaControlOpen(m_displayQuery.readAllStandardOutput());
        emit statusChanged();
    });
    m_displayPoll.start();
    // Notifications remain visible when the recording window and bar popup are hidden.
    connect(this, &Backend::statusChanged, this, [this] {
        const auto s = status();
        QString notice = s["error"].toString();
        if (notice.isEmpty()) notice = s["warning"].toString();
        if (!s["displayWarning"].toString().isEmpty()) notice += (notice.isEmpty() ? QString() : QString("\n")) + s["displayWarning"].toString();
        if (notice == m_lastNotice) return;
        m_lastNotice = notice;
        if (notice.isEmpty()) return;
        auto message = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "Notify");
        message.setArguments({"Sourceroll", uint(0), "sourceroll", tr("Recording needs attention"), notice.toHtmlEscaped(),
            QStringList{}, QVariantMap{}, 10000});
        QDBusConnection::sessionBus().asyncCall(message);
    });
    refreshDevices();
    if (!migrationError.isEmpty()) fail(migrationError);
}

Backend::~Backend() { shutdown(); }

void Backend::shutdown() {
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    m_displayPoll.stop(); m_displayQuery.kill(); m_displayQuery.waitForFinished(1000);
    m_restart.stop(); m_tick.stop(); m_watchdog.stop();
    stopScreenPreview();
    m_session->stop(); stopProcess(); m_session->shutdown();
    m_screenMonitor.stop();
}

void Backend::refreshDevices() {
    if (m_session && m_session->busy()) return;
    if (m_visible || m_previewClients > 0) stopProcess();
    m_cameras.clear(); m_microphones.clear(); m_desktopSources.clear(); m_screens.clear();
    m_screens.append(QVariantMap{{"id", "portal"}, {"label", tr("Screen/window picker")}});
    for (const auto &entry : QJsonDocument::fromJson(query("hyprctl", {"-j", "monitors"})).array()) {
        const auto monitor = entry.toObject();
        const auto name = monitor["name"].toString();
        if (!name.isEmpty()) m_screens.append(QVariantMap{{"id", name}, {"label", name + " · " + tr("Direct monitor capture")}});
    }
    QDir directory("/sys/class/video4linux");
    for (const auto &node : directory.entryList({"video*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString path = "/dev/" + node;
        auto available = cameraModes(path);
        if (available.isEmpty()) continue;
        QFile name(directory.filePath(node + "/name"));
        if (!name.open(QIODevice::ReadOnly)) continue;
        QString stablePath = path;
        QDir ids("/dev/v4l/by-id");
        for (const auto &id : ids.entryList(QDir::Files))
            if (QFileInfo(ids.filePath(id)).canonicalFilePath() == QFileInfo(path).canonicalFilePath()) { stablePath = ids.filePath(id); break; }
        m_cameras.append(QVariantMap{{"id", stablePath}, {"label", QString::fromUtf8(name.readAll()).trimmed() + " (" + path + ")"}, {"modes", available}});
    }
    m_microphones.append(QVariantMap{{"id", "none"}, {"label", tr("No audio")}});
    m_microphones.append(QVariantMap{{"id", "default"}, {"label", tr("System default microphone")}});
    const auto sources = QJsonDocument::fromJson(query("pactl", {"-f", "json", "list", "sources"})).array();
    for (const auto &source : sources) {
        const auto s = source.toObject();
        if (s["name"].toString().endsWith(".monitor")) { m_desktopSources.append(QVariantMap{{"id", s["name"].toString()}, {"label", s["description"].toString()}}); continue; }
        m_microphones.append(QVariantMap{{"id", s["name"].toString()}, {"label", s["description"].toString()}});
    }
    if (!m_settings.contains("camera") && !m_cameras.isEmpty()) m_settings["camera"] = m_cameras.first().toMap()["id"].toString();
    if (!m_settings.contains("mode") && !modes().isEmpty()) {
        auto choices = modes(); auto selected = choices.first().toMap();
        for (const auto &choice : choices) {
            auto c = choice.toMap();
            if (c["size"] == "1920x1080" && c["format"] == "mjpeg") { selected = c; break; }
        }
        m_settings["mode"] = selected["id"].toString();
    }
    if (!m_settings.contains("audio")) {
        m_settings["audio"] = "default";
        for (const auto &mic : m_microphones) if (mic.toMap()["label"].toString().contains("Shure", Qt::CaseInsensitive)) m_settings["audio"] = mic.toMap()["id"].toString();
    }
    if (!m_settings.contains("desktopAudio") && !m_desktopSources.isEmpty()) {
        const auto preferred = QString::fromUtf8(query("pactl", {"get-default-sink"})).trimmed() + ".monitor";
        m_settings["desktopAudio"] = preferred;
    }
    emit devicesChanged(); emit settingsChanged();
    if (m_visible || m_previewClients > 0) { dismissError(); m_restart.start(); }
}

QVariantList Backend::modes() const {
    for (const auto &camera : m_cameras) if (camera.toMap()["id"].toString() == m_settings["camera"].toString()) return camera.toMap()["modes"].toList();
    return {};
}

bool Backend::validSettings(const QJsonObject &s, QString *error) {
    auto reject = [&](const QString &message) { if (error) *error = message; return false; };
    if (!s.value("cameraEnabled").toBool(true)) return QDir::isAbsolutePath(s["directory"].toString()) && SessionRecorder::validateSources(s, error);
    if (!s["camera"].toString().startsWith("/dev/")) return reject(tr("Select a camera."));
    static const QRegularExpression mode("^[0-9]{2,5}x[0-9]{2,5}@[0-9]{1,8}/[0-9]{1,8}:(mjpeg|yuyv422|h264)$");
    if (!mode.match(s["mode"].toString()).hasMatch()) return reject(tr("Select a supported resolution and frame rate."));
    auto rate = s["mode"].toString().section('@', 1).section(':', 0, 0);
    if (rate.section('/', 0, 0).toInt() == 0 || rate.section('/', 1, 1).toInt() == 0) return reject(tr("Invalid frame rate."));
    if (s["audio"].toString().isEmpty()) return reject(tr("Select an audio source or No audio."));
    if (!QDir::isAbsolutePath(s["directory"].toString())) return reject(tr("The output folder must be an absolute path."));
    if (s["encoding"] != "original" && s["encoding"] != "h264") return reject(tr("Unknown recording format."));
    return true;
}

QStringList Backend::ffmpegArguments(const QJsonObject &s, const QString &output, bool recording) {
    const QString mode = s["mode"].toString();
    const QString size = mode.section('@', 0, 0), rate = mode.section('@', 1).section(':', 0, 0), format = mode.section(':', 1);
    QStringList args{"-hide_banner", "-nostats", "-loglevel", "info", "-stats_period", "0.5", "-progress", "pipe:2", "-n",
        "-thread_queue_size", "512", "-f", "v4l2", "-input_format", format, "-video_size", size, "-framerate", rate, "-i", s["camera"].toString()};
    if (recording) {
        args.insert(1, "-debug_ts");
        args.insert(args.indexOf("-f"), "-timestamps");
        args.insert(args.indexOf("-timestamps") + 1, "abs");
        args << "-map" << "0:v:0";
        if (s["encoding"] == "original" && (format == "mjpeg" || format == "h264")) args << "-c:v" << "copy";
        else args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "18" << "-pix_fmt" << "yuv420p";
        args << "-an" << "-f" << "matroska" << output;
    }
    // One input feeds both the untouched recording and a small monitoring image.
    args << "-map" << "0:v:0" << "-an" << "-vf" << "fps=30,scale=640:-2" << "-c:v" << "mjpeg" << "-q:v" << "5" << "-threads" << "2" << "-f" << "image2pipe" << "pipe:1";
    return args;
}

QVariantMap Backend::status() const {
    QVariantMap result = m_session ? m_session->status() : QVariantMap();
    const bool busy = m_session && m_session->busy();
    const bool hasSession = !result.value("output").toString().isEmpty();
    if (!busy && !hasSession) { result["state"] = m_state; result["error"] = m_error; }
    if (!busy && !m_error.isEmpty()) { result["state"] = "error"; result["error"] = m_error; }
    result["displayWarning"] = busy && m_settings["screenEnabled"].toBool() && m_displaySettingsOpen
        ? tr("OmaControl is open during screen recording. Changing display settings can interrupt capture. Wait until recording has finished before changing displays.") : QString();
    result["running"] = true; result["recording"] = busy;
    result["preview"] = m_process.state() != QProcess::NotRunning;
    result["peakDb"] = m_peak; result["desktopPeakDb"] = m_desktopPeak;
    result["screenPreviewState"] = m_screenMonitor.state();
    result["screenPreviewError"] = m_screenMonitor.error();
    result["screenSelected"] = m_screenSelected;
    result["screenSelecting"] = m_screenPreviewProcess.state() != QProcess::NotRunning && !m_screenPreviewHasData;
    if (!busy && !m_screenSelectionError.isEmpty()) { result["screenPreviewState"] = "error"; result["screenPreviewError"] = m_screenSelectionError; }
    result["cameraPreviewState"] = m_process.state() == QProcess::NotRunning ? "inactive" :
        m_lastFrame.isValid() && m_lastFrame.elapsed() > 3000 ? "stalled" : m_frameNumber > 0 ? "live" : "waiting";
    result["camera"] = m_settings["camera"]; result["audio"] = m_settings["audio"];
    result["cameraEnabled"] = m_settings["cameraEnabled"]; result["screenEnabled"] = m_settings["screenEnabled"];
    result["microphoneEnabled"] = m_settings["microphoneEnabled"]; result["desktopEnabled"] = m_settings["desktopEnabled"];
    return result;
}

bool Backend::saveSettings() {
    QDir().mkpath(QFileInfo(m_configFile).absolutePath());
    QSaveFile file(m_configFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(m_settings).toJson()) < 0 || !file.commit()) {
        fail(tr("Could not save settings: %1").arg(file.errorString())); return false;
    }
    return true;
}

bool Backend::setSetting(const QString &key, const QVariant &value) {
    // A display-only preference: it may change mid-recording and must not restart the preview.
    if (key == "monitorLayout") {
        if (m_shuttingDown || !QStringList{"side", "stacked"}.contains(value.toString())) return false;
        m_settings[key] = value.toString(); emit settingsChanged(); return saveSettings();
    }
    if (m_shuttingDown || m_session->busy() || m_state == "stopping") return false;
    if (!QStringList{"camera", "mode", "audio", "directory", "encoding", "cameraEnabled", "screenEnabled", "microphoneEnabled", "desktopEnabled", "screenTarget", "screenFps", "screenQuality", "desktopAudio"}.contains(key)) return false;
    if (key == "screenQuality" && !QStringList{"high", "very_high", "ultra"}.contains(value.toString())) return false;
    stopProcess();
    m_session->previewAudio(m_settings, false);
    if (key.endsWith("Enabled")) m_settings[key] = value.toBool();
    else if (key == "screenFps") m_settings[key] = qBound(15, value.toInt(), 120);
    else m_settings[key] = value.toString().left(4096);
    if ((key == "screenEnabled" && !value.toBool()) || key == "screenTarget") {
        stopScreenPreview(); m_screenSelected = false; m_screenSelectionError.clear();
        QFile::remove(m_portalCache.filePath("restore-token"));
    }
    if (key == "camera") m_settings["mode"] = modes().isEmpty() ? "" : modes().first().toMap()["id"].toString();
    emit settingsChanged();
    const bool saved = saveSettings();
    if (saved && (m_visible || m_previewClients > 0)) m_restart.start();
    return saved;
}

void Backend::fail(const QString &message) { m_error = message; m_state = "error"; emit statusChanged(); }
void Backend::dismissError() { m_error.clear(); if (!m_recording) m_state = "idle"; emit statusChanged(); }

void Backend::launch(bool recording) {
    Q_UNUSED(recording)
    m_restart.stop();
    if (m_session->busy()) return;
    m_session->previewAudio(m_settings, true);
    if (!m_settings["cameraEnabled"].toBool(true)) { m_state = "idle"; emit statusChanged(); return; }
    QString error;
    if (!validSettings(m_settings, &error)) { fail(error); return; }
    if (m_process.state() != QProcess::NotRunning) return;
    m_frames.clear(); m_messages.clear(); m_log.clear(); m_error.clear(); m_frameNumber = 0; m_image = QImage(); m_previewJpeg.clear(); emit frameReady();
    m_recording = false; m_state = "preview-starting";
    m_process.start("ffmpeg", ffmpegArguments(m_settings, {}, false));
    m_watchdog.start(); emit statusChanged();
}

bool Backend::start() {
    if (m_shuttingDown || m_session->busy()) return false;
    if (m_screenPreviewProcess.state() != QProcess::NotRunning && !m_screenPreviewHasData) { fail(tr("Finish choosing the screen before starting a take.")); return false; }
    QString error;
    if (!SessionRecorder::validateSources(m_settings, &error) || !validSettings(m_settings, &error)) { fail(error); return false; }
    if (m_settings["cameraEnabled"].toBool(true)) {
        bool supported = false;
        for (const auto &mode : modes()) if (mode.toMap()["id"].toString() == m_settings["mode"].toString()) supported = true;
        if (!supported) { fail(tr("The selected camera mode is unavailable.")); return false; }
    }
    if (!saveSettings()) return false;
    stopScreenPreview(); stopProcess(); m_error.clear(); m_screenSelectionError.clear();
    return m_session->start(m_settings, m_portalCache.filePath("restore-token"));
}

void Backend::stopScreenPreview() {
    m_screenSelectTimer.stop();
    if (m_screenPreviewProcess.state() == QProcess::NotRunning) return;
    m_screenPreviewStopping = true;
    ::kill(m_screenPreviewProcess.processId(), SIGINT);
    if (!m_screenPreviewProcess.waitForFinished(3000)) { m_screenPreviewProcess.kill(); m_screenPreviewProcess.waitForFinished(1000); }
    m_screenMonitor.stop(); m_screenPreviewStopping = false;
}

void Backend::startScreenPreview(bool choose) {
    if (m_shuttingDown || m_session->busy() || !m_portalCache.isValid()) return;
    if (m_settings["screenTarget"] != "portal") return;
    if (!choose && (m_screenPreviewProcess.state() != QProcess::NotRunning || !QFileInfo::exists(m_portalCache.filePath("restore-token")))) return;
    stopScreenPreview();
    if (choose) { QFile::remove(m_portalCache.filePath("restore-token")); m_screenSelected = false; }
    m_screenPreviewHasData = false;
    m_screenSelectionError.clear(); m_screenPreviewLog.clear();
    m_screenMonitor.setEnabled(true); m_screenMonitor.start();
    m_screenPreviewProcess.start("gpu-screen-recorder", {"-w", "portal", "-f", "15", "-k", "h264", "-q", "medium", "-fallback-cpu-encoding", "yes", "-keyint", "1",
        "-restore-portal-session", "yes", "-portal-session-token-filepath", m_portalCache.filePath("restore-token"), "-c", "mpegts", "-o", "/dev/stdout"});
    m_screenSelectTimer.start();
    emit statusChanged();
}

bool Backend::chooseScreen() {
    if (m_shuttingDown || m_session->busy()) return false;
    if (!m_portalCache.isValid()) { m_screenSelectionError = tr("Could not create a private screen-sharing session folder."); emit statusChanged(); return false; }
    if (!m_settings["screenEnabled"].toBool()) setSetting("screenEnabled", true);
    if (m_settings["screenTarget"] != "portal") setSetting("screenTarget", "portal");
    startScreenPreview(true); return true;
}

void Backend::stopProcess() {
    m_watchdog.stop(); m_restart.stop();
    if (!m_session->busy()) m_session->previewAudio(m_settings, false);
    if (m_process.state() == QProcess::NotRunning) return;
    // FFmpeg's own quit command lets it write the Matroska index and trailers.
    m_stopping = true;
    m_state = "stopping"; emit statusChanged();
    m_process.write("q\n"); m_process.waitForBytesWritten(500);
    if (!m_process.waitForFinished(5000)) {
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) { m_process.kill(); m_process.waitForFinished(1000); }
    }
    m_stopping = false;
}

void Backend::stop() { if (m_session->busy()) m_session->stop(); }
void Backend::toggle() { if (m_session->busy()) stop(); else start(); }

void Backend::setWindowVisible(bool visible) {
    if (m_shuttingDown) return;
    m_visible = visible;
    m_screenMonitor.setEnabled(m_visible || m_previewClients > 0);
    if (!m_session->busy()) {
        if (!m_visible && !m_previewClients) stopScreenPreview();
        else if (m_settings["screenEnabled"].toBool() && m_screenSelected) startScreenPreview(false);
    }
    if (m_session->busy()) return;
    if (!visible && m_previewClients == 0) stopProcess();
    else if (visible || m_previewClients > 0) m_restart.start();
}
void Backend::addPreviewClient() { ++m_previewClients; m_screenMonitor.setEnabled(true); if (!m_session->busy()) { m_restart.start(); if (m_settings["screenEnabled"].toBool() && m_screenSelected) startScreenPreview(false); } }
void Backend::removePreviewClient() {
    m_previewClients = qMax(0, m_previewClients - 1);
    m_screenMonitor.setEnabled(m_visible || m_previewClients > 0);
    if (!m_previewClients && !m_visible && !m_session->busy()) stopScreenPreview();
    if (!m_previewClients && !m_visible && !m_session->busy()) stopProcess();
}
bool Backend::recoverSession() { if (m_session->busy()) return false; stopProcess(); return m_session->recover(m_settings["directory"].toString()); }

void Backend::openFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings["directory"].toString())); }

void Backend::consumeVideo() {
    m_frames += m_process.readAllStandardOutput();
    if (m_frames.size() > 8 * 1024 * 1024) m_frames.clear();
    for (;;) {
        const auto begin = m_frames.indexOf(QByteArray::fromHex("ffd8"));
        if (begin < 0) { if (m_frames.size() > 1) m_frames = m_frames.right(1); break; }
        if (begin > 0) m_frames.remove(0, begin);
        const auto end = m_frames.indexOf(QByteArray::fromHex("ffd9"), 2);
        if (end < 0) break;
        const auto jpeg = m_frames.left(end + 2);
        QImage image = QImage::fromData(jpeg, "JPEG");
        m_frames.remove(0, end + 2);
        if (image.isNull()) continue;
        m_image = image; m_previewJpeg = jpeg; m_lastFrame.start(); ++m_frameNumber; emit frameReady();
        if (m_state == "starting" || m_state == "preview-starting") {
            m_watchdog.stop(); m_state = m_recording ? "recording" : "preview"; emit statusChanged();
        }
    }
}

void Backend::consumeMessages() {
    QByteArray bytes = m_process.readAllStandardError();
    m_log = (m_log + bytes).right(4096);
    m_messages += bytes;
    for (;;) {
        auto end = m_messages.indexOf('\n'); if (end < 0) break;
        auto line = m_messages.left(end).trimmed(); m_messages.remove(0, end + 1);
        if (m_recording) { const auto stamp = SessionRecorder::packetTimestamp(line); if (stamp) m_session->cameraTimestamp(stamp); }
    }
    if (m_messages.size() > 65536) m_messages.clear();
}

std::optional<double> Backend::parsePeak(const QByteArray &line) {
    const QByteArray key("lavfi.astats.Overall.Peak_level=");
    const auto offset = line.indexOf(key);
    if (offset < 0) return std::nullopt;
    const auto text = line.mid(offset + key.size()).trimmed();
    if (text == "-inf") return -90;
    bool ok = false; const double peak = text.toDouble(&ok);
    if (!ok || !std::isfinite(peak)) return std::nullopt;
    return qBound(-90.0, peak, 0.0);
}

void Backend::finish(int code, QProcess::ExitStatus exitStatus) {
    m_watchdog.stop(); consumeMessages();
    const bool wasRecording = m_recording; m_recording = false; m_state = "idle";
    if (wasRecording || m_session->busy()) { m_session->cameraFinished(code, QString::fromUtf8(m_log.right(1000))); emit statusChanged(); return; }
    if (!m_shuttingDown && !m_stopping && (code != 0 || exitStatus != QProcess::NormalExit)) fail(tr("Camera preview failed. %1").arg(QString::fromUtf8(m_log.right(1400))));
    emit statusChanged();
}

QJsonObject Backend::handle(const QJsonObject &request) {
    const auto command = request["command"].toString();
    bool ok = true;
    if (command == "start") ok = start();
    else if (command == "stop") stop();
    else if (command == "toggle") toggle();
    else if (command == "recover") ok = recoverSession();
    else if (command == "choose-screen") ok = chooseScreen();
    else if (command == "open") emit openRequested();
    else if (command == "configure") {
        const auto options = request["settings"].toObject();
        for (auto it = options.begin(); it != options.end(); ++it) if (!setSetting(it.key(), it.value().toVariant())) ok = false;
    } else if (command != "status" && command != "settings" && command != "devices" && command != "ensure") ok = false;
    QJsonObject result = QJsonObject::fromVariantMap(status());
    result["ok"] = ok;
    if (command == "settings" || command == "configure") result["settings"] = m_settings;
    if (command == "devices") { result["cameras"] = QJsonArray::fromVariantList(m_cameras); result["microphones"] = QJsonArray::fromVariantList(m_microphones); result["screens"] = QJsonArray::fromVariantList(m_screens); result["desktopSources"] = QJsonArray::fromVariantList(m_desktopSources); }
    return result;
}

bool Backend::importLegacySettings(const QString &legacy, const QString &target, QString *error) {
    if (QFileInfo::exists(target) || !QFileInfo::exists(legacy)) return true;
    QFile input(legacy);
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!input.open(QIODevice::ReadOnly)) return fail(tr("Could not read previous settings."));
    QJsonParseError parseError;
    const auto data = input.read(65537);
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (data.size() > 65536 || parseError.error != QJsonParseError::NoError || !doc.isObject() || doc.object().isEmpty()) return fail(tr("Previous settings are invalid; they were left unchanged."));
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) return fail(tr("Could not create the Sourceroll settings folder."));
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly) || output.write(doc.toJson()) < 0) return fail(tr("Could not import previous settings."));
    if (QFileInfo::exists(target)) { output.cancelWriting(); return true; }
    return output.commit() || fail(tr("Could not save imported settings."));
}
