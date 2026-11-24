#include "backend.h"

#include <QDateTime>
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

static QByteArray query(const QString &program, const QStringList &args) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(2500)) { process.kill(); process.waitForFinished(500); return {}; }
    return process.exitCode() == 0 ? process.readAllStandardOutput() : QByteArray();
}

QString Backend::socketPath() {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/source-record.sock";
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

Backend::Backend(const QString &configFile, QObject *parent) : QObject(parent) {
    // If the app crashes, ask only its own child to finalize rather than orphaning capture.
    m_process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGINT); if (getppid() == 1) _exit(1); });
    m_configFile = configFile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.json" : configFile;
    QFile file(m_configFile);
    if (file.open(QIODevice::ReadOnly)) m_settings = QJsonDocument::fromJson(file.read(65536)).object();
    if (!m_settings.contains("directory")) m_settings["directory"] = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Source Record";
    if (!m_settings.contains("encoding")) m_settings["encoding"] = "original";
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Backend::consumeVideo);
    connect(&m_process, &QProcess::readyReadStandardError, this, &Backend::consumeMessages);
    connect(&m_process, &QProcess::finished, this, &Backend::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { m_recording = false; fail(tr("FFmpeg could not start: %1").arg(m_process.errorString())); }
    });
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if ((m_state == "recording" || m_state == "preview") && m_lastFrame.isValid() && m_lastFrame.elapsed() > 6000) {
            stopProcess();
            fail(tr("The camera stopped delivering frames. The clip was closed; check the device before recording again."));
        }
        emit statusChanged();
    });
    m_tick.start();
    m_restart.setSingleShot(true); m_restart.setInterval(250);
    connect(&m_restart, &QTimer::timeout, this, [this] { if (m_visible && !m_recording && m_process.state() == QProcess::NotRunning) launch(false); });
    m_watchdog.setInterval(10000);
    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (m_state == "starting" || m_state == "preview-starting") {
            stopProcess();
            fail(tr("The camera did not deliver a frame. Check that it is connected and not in use by another app."));
        }
    });
    refreshDevices();
}

Backend::~Backend() { m_shuttingDown = true; m_restart.stop(); stopProcess(); }

void Backend::refreshDevices() {
    if (m_recording) return;
    if (m_visible) stopProcess();
    m_cameras.clear(); m_microphones.clear();
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
        if (s["name"].toString().endsWith(".monitor")) continue;
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
    emit devicesChanged(); emit settingsChanged();
    if (m_visible) { dismissError(); m_restart.start(); }
}

QVariantList Backend::modes() const {
    for (const auto &camera : m_cameras) if (camera.toMap()["id"].toString() == m_settings["camera"].toString()) return camera.toMap()["modes"].toList();
    return {};
}

bool Backend::validSettings(const QJsonObject &s, QString *error) {
    auto reject = [&](const QString &message) { if (error) *error = message; return false; };
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
    bool audio = s["audio"] != "none";
    if (audio) args << "-thread_queue_size" << "512" << "-f" << "pulse" << "-i" << s["audio"].toString();
    const QString meter = "astats=metadata=1:reset=1,ametadata=print:key=lavfi.astats.Overall.Peak_level";
    if (recording) {
        args << "-map" << "0:v:0";
        if (s["encoding"] == "original" && (format == "mjpeg" || format == "h264")) args << "-c:v" << "copy";
        else args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "18" << "-pix_fmt" << "yuv420p";
        if (audio) args << "-map" << "1:a:0" << "-af" << meter << "-c:a" << "pcm_s16le";
        args << "-f" << "matroska" << output;
    } else if (audio) {
        args << "-map" << "1:a:0" << "-af" << meter << "-f" << "null" << "/dev/null";
    }
    // One input feeds both the untouched recording and a small monitoring image.
    args << "-map" << "0:v:0" << "-an" << "-vf" << "fps=8,scale=640:-2" << "-c:v" << "mjpeg" << "-q:v" << "5" << "-threads" << "2" << "-f" << "image2pipe" << "pipe:1";
    return args;
}

QVariantMap Backend::status() const {
    const bool busy = m_state == "recording" || m_state == "starting" || m_state == "stopping";
    const qint64 elapsed = busy && m_elapsed.isValid() ? m_elapsed.elapsed() : m_recordedMs;
    const QString file = busy ? m_output : m_lastOutput;
    return {{"running", true}, {"recording", m_recording}, {"state", m_state}, {"error", m_error}, {"output", file},
        {"elapsedSeconds", elapsed / 1000.0}, {"bytes", double(QFileInfo(file).size())}, {"peakDb", m_peak},
        {"preview", m_process.state() != QProcess::NotRunning}, {"camera", m_settings["camera"].toString()}, {"audio", m_settings["audio"].toString()}};
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
    if (m_recording || m_state == "stopping") return false;
    if (!QStringList{"camera", "mode", "audio", "directory", "encoding"}.contains(key)) return false;
    stopProcess();
    m_settings[key] = value.toString().left(4096);
    if (key == "camera") m_settings["mode"] = modes().isEmpty() ? "" : modes().first().toMap()["id"].toString();
    emit settingsChanged();
    const bool saved = saveSettings();
    if (saved && m_visible) m_restart.start();
    return saved;
}

void Backend::fail(const QString &message) { m_error = message; m_state = "error"; emit statusChanged(); }
void Backend::dismissError() { m_error.clear(); if (!m_recording) m_state = "idle"; emit statusChanged(); }

void Backend::launch(bool recording) {
    m_restart.stop();
    QString error;
    if (!validSettings(m_settings, &error)) { fail(error); return; }
    if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) { fail(tr("FFmpeg is not installed.")); return; }
    bool supported = false;
    for (const auto &mode : modes()) if (mode.toMap()["id"].toString() == m_settings["mode"].toString()) supported = true;
    if (!supported) { fail(tr("The selected camera mode is unavailable. Refresh devices and select a supported mode.")); return; }
    if (recording) {
        const QString dir = m_settings["directory"].toString();
        if (!QDir().mkpath(dir)) { fail(tr("Could not create the output folder.")); return; }
        QStorageInfo storage(dir);
        if (storage.isValid() && storage.bytesAvailable() < 512LL * 1024 * 1024) { fail(tr("Less than 512 MiB is free in the output folder.")); return; }
        m_output = QDir(dir).filePath("camera-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz") + ".mkv");
        if (!saveSettings()) return;
    }
    m_frames.clear(); m_messages.clear(); m_log.clear(); m_peak = -90; m_error.clear();
    m_recording = recording; m_recordedMs = 0;
    m_state = recording ? "starting" : "preview-starting";
    if (recording) m_elapsed.start();
    m_process.start("ffmpeg", ffmpegArguments(m_settings, m_output, recording));
    m_watchdog.start(); emit statusChanged();
}

bool Backend::start() {
    if (m_recording || m_state == "stopping") return false;
    stopProcess(); launch(true); return m_recording;
}

void Backend::stopProcess() {
    m_watchdog.stop(); m_restart.stop();
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

void Backend::stop() {
    if (!m_recording) return;
    stopProcess();
    if (m_visible && m_error.isEmpty()) m_restart.start();
}

void Backend::toggle() { if (m_recording) stop(); else start(); }

void Backend::setWindowVisible(bool visible) {
    m_visible = visible;
    if (!visible && !m_recording) stopProcess();
    else if (visible && m_process.state() == QProcess::NotRunning && m_error.isEmpty()) m_restart.start();
}

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
        QImage image = QImage::fromData(m_frames.left(end + 2), "JPEG");
        m_frames.remove(0, end + 2);
        if (image.isNull()) continue;
        m_image = image; m_lastFrame.start(); ++m_frameNumber; emit frameReady();
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
        const QByteArray key("lavfi.astats.Overall.Peak_level=");
        const auto offset = line.indexOf(key);
        if (offset >= 0) {
            bool ok = false; const double peak = line.mid(offset + key.size()).toDouble(&ok);
            m_peak = ok && std::isfinite(peak) ? qBound(-90.0, peak, 0.0) : -90;
        }
    }
    if (m_messages.size() > 65536) m_messages.clear();
}

void Backend::finish(int code, QProcess::ExitStatus exitStatus) {
    m_watchdog.stop(); consumeMessages();
    const bool wasRecording = m_recording;
    if (wasRecording) {
        m_recordedMs = m_elapsed.elapsed();
        if (QFileInfo(m_output).size() > 0) m_lastOutput = m_output;
    }
    m_recording = false; m_peak = -90; m_state = "idle";
    if (!m_shuttingDown && (code != 0 || exitStatus != QProcess::NormalExit))
        fail(tr("Capture failed. Check the camera and microphone.\n%1").arg(QString::fromUtf8(m_log.right(1800))));
    else if (wasRecording && QFileInfo(m_output).size() == 0)
        fail(tr("No video was saved. Check the output folder and camera."));
    emit statusChanged();
}

QJsonObject Backend::handle(const QJsonObject &request) {
    const auto command = request["command"].toString();
    bool ok = true;
    if (command == "start") ok = start();
    else if (command == "stop") stop();
    else if (command == "toggle") toggle();
    else if (command == "open") emit openRequested();
    else if (command == "configure") {
        const auto options = request["settings"].toObject();
        for (auto it = options.begin(); it != options.end(); ++it) if (!setSetting(it.key(), it.value().toVariant())) ok = false;
    } else if (command != "status" && command != "settings" && command != "devices") ok = false;
    QJsonObject result = QJsonObject::fromVariantMap(status());
    result["ok"] = ok;
    if (command == "settings" || command == "configure") result["settings"] = m_settings;
    if (command == "devices") { result["cameras"] = QJsonArray::fromVariantList(m_cameras); result["microphones"] = QJsonArray::fromVariantList(m_microphones); }
    return result;
}
