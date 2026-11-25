#include "backend.h"
#include "camerafeed.h"
#include "screenfeed.h"
#include "session.h"
#include "sources.h"

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
#include <QSet>
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

Backend::Backend(const QString &configFile, QObject *parent, const QString &screenProgram, const QString &cameraProgram) : QObject(parent),
    m_portalCache(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/sourceroll-screen-XXXXXX"),
    m_screenProgram(screenProgram), m_cameraProgram(cameraProgram) {
    m_configFile = configFile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.json" : configFile;
    QString migrationError;
    if (configFile.isEmpty()) importLegacySettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/multitake/settings.json", m_configFile, &migrationError);
    if (configFile.isEmpty()) importLegacySettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/source-record/settings.json", m_configFile, &migrationError);
    QFile file(m_configFile);
    if (file.open(QIODevice::ReadOnly)) m_settings = QJsonDocument::fromJson(file.read(65536)).object();
    if (!m_settings.contains("directory")) m_settings["directory"] = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Sourceroll";
    if (!m_settings.contains("microphoneEnabled")) m_settings["microphoneEnabled"] = m_settings["audio"] != "none";
    if (!m_settings.contains("desktopEnabled")) m_settings["desktopEnabled"] = false;
    // Earlier settings described one screen and one camera. They become the
    // first two entries of the list, and are written back on the next change.
    m_settings = Sources::migrated(m_settings);
    m_session = new SessionRecorder(this, m_screenProgram);
    syncFeeds();
    connect(m_session, &SessionRecorder::screenStarting, this, [this](const QString &id) { if (const auto feed = m_screenFeeds.value(id)) feed->monitor().start(); });
    connect(m_session, &SessionRecorder::screenStopped, this, [this](const QString &id) { if (const auto feed = m_screenFeeds.value(id)) feed->monitor().stop(); });
    connect(m_session, &SessionRecorder::screenData, this, [this](const QString &id, const QByteArray &bytes) {
        const auto feed = m_screenFeeds.value(id);
        if (!feed) return;
        feed->monitor().feed(bytes);
        if (!bytes.isEmpty()) feed->markSelected();
    });
    connect(m_session, &SessionRecorder::changed, this, &Backend::statusChanged);
    connect(m_session, &SessionRecorder::levels, this, [this](double mic, double desktop) { m_peak = mic; m_desktopPeak = desktop; emit peakChanged(); });
    connect(m_session, &SessionRecorder::cameraStartRequested, this, [this](const QString &id, const QString &file) {
        if (const auto feed = m_cameraFeeds.value(id)) feed->record(Sources::find(Sources::list(m_settings), id), file);
        else m_session->cameraFinished(id, 1, tr("Its camera source no longer exists."));
    });
    connect(m_session, &SessionRecorder::cameraStopRequested, this, [this](const QString &id) { if (const auto feed = m_cameraFeeds.value(id)) feed->requestStop(); });
    connect(m_session, &SessionRecorder::finished, this, [this] { if (!m_shuttingDown && viewed()) m_restart.start(); });
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &Backend::statusChanged);
    m_tick.start();
    m_restart.setSingleShot(true); m_restart.setInterval(250);
    connect(&m_restart, &QTimer::timeout, this, [this] { if (viewed() && !m_session->busy()) launch(); });
    m_displayPoll.setInterval(1000);
    connect(&m_displayPoll, &QTimer::timeout, this, [this] {
        if (!m_session->busy() || Sources::enabled(m_settings, "screen").isEmpty()) { m_displaySettingsOpen = false; return; }
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
    if (configFile.isEmpty()) connect(this, &Backend::statusChanged, this, [this] {
        const auto s = status();
        QString notice = s["error"].toString();
        if (notice.isEmpty()) notice = s["warning"].toString();
        if (!s["displayWarning"].toString().isEmpty()) notice += (notice.isEmpty() ? QString() : QString("\n")) + s["displayWarning"].toString();
        if (notice == m_lastNotice) return;
        m_lastNotice = notice;
        if (notice.isEmpty()) return;
        auto message = QDBusMessage::createMethodCall("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "Notify");
        message.setArguments({"Source Roll", uint(0), "sourceroll", tr("Recording needs attention"), notice.toHtmlEscaped(),
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
    m_restart.stop(); m_tick.stop();
    stopScreenPreviews();
    m_session->stop(); stopPreviews(); m_session->shutdown();
    for (const auto feed : m_screenFeeds) feed->monitor().stop();
}

// Keep one feed per source in the list, and drop those of removed sources.
void Backend::syncFeeds() {
    const auto sources = Sources::list(m_settings);
    for (const auto &value : sources) {
        const auto source = value.toObject(); const QString id = source["id"].toString();
        if (source["kind"] == "camera" && !m_cameraFeeds.contains(id)) {
            const auto feed = new CameraFeed(this, m_cameraProgram);
            m_cameraFeeds[id] = feed;
            connect(feed, &CameraFeed::frameReady, this, [this, id, feed] { emit sourceFrame(id, feed->frameNumber()); });
            connect(feed, &CameraFeed::changed, this, &Backend::statusChanged);
            connect(feed, &CameraFeed::timestamp, this, [this, id](qint64 stamp) { m_session->cameraTimestamp(id, stamp); });
            connect(feed, &CameraFeed::stalled, this, [this, id, feed](bool starting) {
                const QString name = label(id);
                if (m_session->busy()) { m_session->stop(starting ? tr("%1 did not deliver its first frame.").arg(name) : tr("%1 stopped delivering frames.").arg(name)); return; }
                feed->stop();
                fail(starting ? tr("%1 did not deliver a frame. Check that it is connected and not in use by another app.").arg(name)
                    : tr("%1 stopped delivering frames. Check the device before recording again.").arg(name));
            });
            connect(feed, &CameraFeed::finished, this, [this, id](int code, bool failed, const QString &log, bool recording) {
                if (recording) m_session->cameraFinished(id, code, log.right(1000));
                else if (failed && !m_shuttingDown) fail(tr("%1 preview failed. %2").arg(label(id), log.right(1400)));
                emit statusChanged();
            });
        } else if (source["kind"] == "screen" && !m_screenFeeds.contains(id)) {
            const auto feed = new ScreenFeed(m_portalCache.filePath("restore-token-" + id), this, m_screenProgram);
            m_screenFeeds[id] = feed;
            feed->monitor().setEnabled(viewed());
            connect(&feed->monitor(), &ScreenMonitor::frameReady, this, [this, id, feed] {
                emit sourceFrame(id, feed->monitor().frameNumber());
                // The first picture makes the preview live; say so now rather than at the next tick.
                if (feed->monitor().frameNumber() == 1) emit statusChanged();
            });
            connect(feed, &ScreenFeed::changed, this, &Backend::statusChanged);
        }
    }
    for (auto it = m_cameraFeeds.begin(); it != m_cameraFeeds.end();) {
        if (Sources::find(sources, it.key())["kind"] == "camera") { ++it; continue; }
        it.value()->stop(); delete it.value(); it = m_cameraFeeds.erase(it);
    }
    for (auto it = m_screenFeeds.begin(); it != m_screenFeeds.end();) {
        if (Sources::find(sources, it.key())["kind"] == "screen") { ++it; continue; }
        it.value()->forget(); delete it.value(); it = m_screenFeeds.erase(it);
    }
}

QString Backend::firstSource(const QString &kind) const { return Sources::first(Sources::list(m_settings), kind)["id"].toString(); }
QString Backend::label(const QString &id) const { return Sources::label(Sources::find(Sources::list(m_settings), id)); }

int Backend::frame(const QString &id) const {
    if (const auto feed = m_cameraFeeds.value(id)) return feed->frameNumber();
    if (const auto feed = m_screenFeeds.value(id)) return feed->monitor().frameNumber();
    return 0;
}

QImage Backend::sourceImage(const QString &id) const {
    if (const auto feed = m_cameraFeeds.value(id)) return feed->image();
    if (const auto feed = m_screenFeeds.value(id)) return feed->monitor().image();
    return {};
}

QByteArray Backend::sourceJpeg(const QString &id) const {
    if (const auto feed = m_cameraFeeds.value(id)) return feed->jpeg();
    if (const auto feed = m_screenFeeds.value(id)) return feed->monitor().jpeg();
    return {};
}

int Backend::sourceLimit() const { return Sources::maximum; }

void Backend::refreshDevices() {
    if (m_session && m_session->busy()) return;
    if (viewed()) stopPreviews();
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
    // A camera source without a device takes the first one no other source uses.
    auto video = Sources::list(m_settings);
    for (int i = 0; i < video.size(); ++i) {
        auto source = video[i].toObject();
        if (source["kind"] != "camera") continue;
        if (source["device"].toString().isEmpty()) source["device"] = unusedCamera(video);
        if (source["mode"].toString().isEmpty()) source["mode"] = defaultMode(source["device"].toString(), true);
        video[i] = source;
    }
    m_settings["videoSources"] = video;
    if (!m_settings.contains("audio")) {
        m_settings["audio"] = "default";
        for (const auto &mic : m_microphones) if (mic.toMap()["label"].toString().contains("Shure", Qt::CaseInsensitive)) m_settings["audio"] = mic.toMap()["id"].toString();
    }
    if (!m_settings.contains("desktopAudio") && !m_desktopSources.isEmpty()) {
        const auto preferred = QString::fromUtf8(query("pactl", {"get-default-sink"})).trimmed() + ".monitor";
        m_settings["desktopAudio"] = preferred;
    }
    emit devicesChanged(); emit settingsChanged();
    if (viewed()) { dismissError(); m_restart.start(); }
}

QVariantList Backend::modesOf(const QString &device) const {
    for (const auto &camera : m_cameras) if (camera.toMap()["id"].toString() == device) return camera.toMap()["modes"].toList();
    return {};
}

QString Backend::defaultMode(const QString &device, bool preferFullHd) const {
    const auto choices = modesOf(device);
    if (choices.isEmpty()) return {};
    if (preferFullHd) for (const auto &choice : choices) {
        const auto mode = choice.toMap();
        if (mode["size"] == "1920x1080" && mode["format"] == "mjpeg") return mode["id"].toString();
    }
    return choices.first().toMap()["id"].toString();
}

QString Backend::unusedCamera(const QJsonArray &sources) const {
    QStringList taken;
    for (const auto &value : sources) if (value.toObject()["kind"] == "camera") taken << value.toObject()["device"].toString();
    for (const auto &camera : m_cameras) if (!taken.contains(camera.toMap()["id"].toString())) return camera.toMap()["id"].toString();
    return {};
}

bool Backend::validSettings(const QJsonObject &s, QString *error) {
    auto reject = [&](const QString &message) { if (error) *error = message; return false; };
    const auto cameras = Sources::enabled(s, "camera");
    if (cameras.isEmpty()) return QDir::isAbsolutePath(s["directory"].toString()) && SessionRecorder::validateSources(s, error);
    QStringList devices;
    for (const auto &value : cameras) {
        const auto camera = value.toObject();
        if (!CameraFeed::valid(camera, error)) return false;
        // A V4L2 device opens once, so two sources cannot share it.
        const QString resolved = QFileInfo(camera["device"].toString()).canonicalFilePath();
        const QString device = resolved.isEmpty() ? camera["device"].toString() : resolved;
        if (devices.contains(device)) return reject(tr("%1 uses a camera that another source already records. Choose a different camera for it.").arg(Sources::label(camera)));
        devices << device;
    }
    if (s["audio"].toString().isEmpty()) return reject(tr("Select an audio source or No audio."));
    if (!QDir::isAbsolutePath(s["directory"].toString())) return reject(tr("The output folder must be an absolute path."));
    return true;
}

QVariantMap Backend::settings() const {
    QVariantMap result = m_settings.toVariantMap();
    const auto sources = Sources::list(m_settings);
    const auto screen = Sources::first(sources, "screen"), camera = Sources::first(sources, "camera");
    result["screenEnabled"] = !Sources::enabled(m_settings, "screen").isEmpty();
    result["cameraEnabled"] = !Sources::enabled(m_settings, "camera").isEmpty();
    result["screenTarget"] = screen["target"].toString("portal"); result["screenFps"] = screen["fps"].toInt(60);
    result["screenQuality"] = screen["quality"].toString("very_high");
    result["camera"] = camera["device"].toString(); result["mode"] = camera["mode"].toString();
    result["encoding"] = camera["encoding"].toString("original");
    return result;
}

QVariantMap Backend::status() const {
    QVariantMap result = m_session ? m_session->status() : QVariantMap();
    const bool busy = m_session && m_session->busy();
    const bool hasSession = !result.value("output").toString().isEmpty();
    const QString sessionState = result.value("state").toString();
    const bool capturing = busy && (sessionState == "recording" || sessionState == "starting");
    QHash<QString, QVariantMap> recorded;
    for (const auto &value : result.value("screens").toList()) recorded[value.toMap()["source"].toString()] = value.toMap();
    QSet<QString> writing;
    for (const auto &value : result.value("sources").toList()) if (value.toMap()["state"] == "recording") writing.insert(value.toMap()["source"].toString());
    // What each source is doing right now, in list order, for the window and the bar.
    QVariantList video; QVariantMap firstScreen, firstCamera;
    bool selecting = false, live = false, opening = false, previewing = false;
    for (const auto &value : Sources::list(m_settings)) {
        const auto source = value.toObject(); const QString id = source["id"].toString();
        QVariantMap entry{{"id", id}, {"kind", source["kind"].toString()}, {"name", Sources::label(source)}, {"enabled", source["enabled"].toBool()}};
        if (source["kind"] == "screen") {
            const auto feed = m_screenFeeds.value(id);
            const bool portal = source["target"] == "portal";
            const QString reported = recorded.value(id).value("health").toString();
            const QString health = busy && !reported.isEmpty() ? reported : QString("inactive");
            // Either the preview's picker is open, or the take is waiting on this screen's.
            const bool choosing = (feed && feed->selecting())
                || (busy && sessionState == "waiting-screen" && portal && recorded.value(id).value("launched").toBool() && !writing.contains(id));
            entry["previewState"] = feed ? feed->state() : QString("inactive"); entry["previewError"] = feed ? feed->error() : QString();
            entry["selected"] = !portal || (feed && feed->selected()); entry["selecting"] = choosing; entry["health"] = health;
            entry["recording"] = capturing && writing.contains(id) && !QStringList{"recovering", "failed", "starting"}.contains(health);
            if (choosing) selecting = true;
            if (firstScreen.isEmpty()) firstScreen = entry;
        } else {
            const auto feed = m_cameraFeeds.value(id);
            entry["previewState"] = feed ? feed->state() : QString("inactive"); entry["previewError"] = QString();
            entry["recording"] = capturing && writing.contains(id);
            if (feed && feed->running()) previewing = true;
            if (entry["previewState"] == "waiting") opening = true;
            if (firstCamera.isEmpty()) firstCamera = entry;
        }
        if (entry["previewState"] == "live") live = true;
        video.append(entry);
    }
    if (!busy && !hasSession) { result["state"] = live ? "preview" : opening ? "preview-starting" : "idle"; result["error"] = m_error; }
    if (!busy && !m_error.isEmpty()) { result["state"] = "error"; result["error"] = m_error; }
    result["displayWarning"] = busy && !Sources::enabled(m_settings, "screen").isEmpty() && m_displaySettingsOpen
        ? tr("OmaControl is open during screen recording. Changing display settings can interrupt capture. Wait until recording has finished before changing displays.") : QString();
    result["running"] = true; result["recording"] = busy;
    result["preview"] = previewing;
    result["peakDb"] = m_peak; result["desktopPeakDb"] = m_desktopPeak;
    result["videoSources"] = video;
    result["screenSelecting"] = selecting;
    // The first screen and camera under their earlier names, for scripts and older widgets.
    result["screenPreviewState"] = firstScreen.value("previewState", "inactive"); result["screenPreviewError"] = firstScreen.value("previewError", QString());
    result["screenSelected"] = firstScreen.value("selected", false);
    result["cameraPreviewState"] = firstCamera.value("previewState", "inactive");
    result["camera"] = Sources::first(Sources::list(m_settings), "camera")["device"].toString(); result["audio"] = m_settings["audio"];
    result["cameraEnabled"] = !Sources::enabled(m_settings, "camera").isEmpty(); result["screenEnabled"] = !Sources::enabled(m_settings, "screen").isEmpty();
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
    if (m_shuttingDown || m_session->busy()) return false;
    // The single-source keys act on the first screen or camera, adding one if there is none.
    static const QHash<QString, QString> perSource{{"camera", "device"}, {"mode", "mode"}, {"encoding", "encoding"},
        {"screenTarget", "target"}, {"screenFps", "fps"}, {"screenQuality", "quality"}};
    const QString kind = key.startsWith("screen") ? "screen" : "camera";
    if (key == "screenEnabled" || key == "cameraEnabled") return setKindEnabled(kind, value.toBool());
    if (perSource.contains(key)) {
        const QString id = firstSource(kind).isEmpty() ? addSource(kind) : firstSource(kind);
        return !id.isEmpty() && setSourceSetting(id, perSource[key], value);
    }
    if (!QStringList{"audio", "directory", "microphoneEnabled", "desktopEnabled", "desktopAudio"}.contains(key)) return false;
    m_session->previewAudio(m_settings, false);
    if (key.endsWith("Enabled")) m_settings[key] = value.toBool();
    else m_settings[key] = value.toString().left(4096);
    emit settingsChanged();
    const bool saved = saveSettings();
    if (saved && viewed()) m_restart.start();
    return saved;
}

// Turning a kind off silences every source of it; turning it on enables the first.
bool Backend::setKindEnabled(const QString &kind, bool enabled) {
    bool ok = true;
    if (!enabled) {
        for (const auto &value : Sources::enabled(m_settings, kind)) if (!setSourceSetting(value.toObject()["id"].toString(), "enabled", false)) ok = false;
        return ok;
    }
    const QString id = firstSource(kind).isEmpty() ? addSource(kind) : firstSource(kind);
    return !id.isEmpty() && setSourceSetting(id, "enabled", true);
}

bool Backend::setSourceSetting(const QString &id, const QString &key, const QVariant &value) {
    if (m_shuttingDown || m_session->busy()) return false;
    auto sources = Sources::list(m_settings);
    int index = -1;
    for (int i = 0; i < sources.size(); ++i) if (sources[i].toObject()["id"] == id) index = i;
    if (index < 0) return false;
    auto source = sources[index].toObject();
    const bool screen = source["kind"] == "screen", camera = !screen;
    const QString text = value.toString().left(4096);
    if (key == "name") source["name"] = Sources::uniqueName(sources, Sources::cleanName(text), id);
    else if (key == "enabled") source["enabled"] = value.toBool();
    else if (screen && key == "target" && !text.isEmpty()) source["target"] = text;
    else if (screen && key == "fps") source["fps"] = qBound(15, value.toInt(), 120);
    else if (screen && key == "quality" && QStringList{"high", "very_high", "ultra"}.contains(text)) source["quality"] = text;
    else if (camera && key == "device") { source["device"] = text; source["mode"] = defaultMode(text, false); }
    else if (camera && key == "mode") source["mode"] = text;
    else if (camera && key == "encoding" && (text == "original" || text == "h264")) source["encoding"] = text;
    else return false;
    if (source == sources[index].toObject()) return true;
    sources[index] = source; m_settings["videoSources"] = sources;
    // Only this source's own preview restarts; the others keep running.
    if (camera && key != "name" && key != "encoding") { if (const auto feed = m_cameraFeeds.value(id)) feed->stop(); }
    if (screen && ((key == "enabled" && !value.toBool()) || key == "target")) { if (const auto feed = m_screenFeeds.value(id)) feed->forget(); }
    emit settingsChanged();
    const bool saved = saveSettings();
    if (saved && viewed()) m_restart.start();
    return saved;
}

QString Backend::addSource(const QString &kind) {
    if (m_shuttingDown || m_session->busy() || (kind != "screen" && kind != "camera")) return {};
    auto sources = Sources::list(m_settings);
    if (sources.size() >= Sources::maximum) return {};
    const QString id = Sources::newId(sources, kind);
    QJsonObject source{{"id", id}, {"kind", kind}, {"name", ""}, {"enabled", true}};
    if (kind == "screen") { source["target"] = "portal"; source["fps"] = 60; source["quality"] = "very_high"; }
    else {
        // Without a free camera it starts switched off, so it cannot block a take.
        const QString device = unusedCamera(sources);
        source["device"] = device; source["mode"] = defaultMode(device, true); source["encoding"] = "original"; source["enabled"] = !device.isEmpty();
    }
    sources.append(source); m_settings["videoSources"] = sources;
    syncFeeds(); emit settingsChanged();
    if (!saveSettings()) return {};
    if (viewed()) m_restart.start();
    return id;
}

bool Backend::removeSource(const QString &id) {
    if (m_shuttingDown || m_session->busy()) return false;
    QJsonArray kept;
    const auto sources = Sources::list(m_settings);
    for (const auto &value : sources) if (value.toObject()["id"] != id) kept.append(value);
    if (kept.size() == sources.size()) return false;
    m_settings["videoSources"] = kept;
    syncFeeds(); emit settingsChanged(); emit statusChanged();
    return saveSettings();
}

void Backend::fail(const QString &message) { m_error = message; emit statusChanged(); }
void Backend::dismissError() { m_error.clear(); emit statusChanged(); }

void Backend::launch() {
    m_restart.stop();
    if (m_shuttingDown || m_session->busy()) return;
    m_session->previewAudio(m_settings, true);
    startScreenPreviews();
    const auto cameras = Sources::enabled(m_settings, "camera");
    QString error;
    if (!cameras.isEmpty() && !validSettings(m_settings, &error)) { fail(error); return; }
    bool started = false;
    for (const auto &value : cameras) {
        const auto camera = value.toObject();
        const auto feed = m_cameraFeeds.value(camera["id"].toString());
        if (!feed || feed->running()) continue;
        feed->preview(camera); started = true;
    }
    if (started) m_error.clear();
    emit statusChanged();
}

bool Backend::start() {
    if (m_shuttingDown || m_session->busy()) return false;
    for (const auto feed : m_screenFeeds) if (feed->selecting()) { fail(tr("Finish choosing the screen before starting a take.")); return false; }
    QString error;
    if (!SessionRecorder::validateSources(m_settings, &error) || !validSettings(m_settings, &error)) { fail(error); return false; }
    for (const auto &value : Sources::enabled(m_settings, "camera")) {
        const auto camera = value.toObject();
        bool supported = false;
        for (const auto &mode : modesOf(camera["device"].toString())) if (mode.toMap()["id"].toString() == camera["mode"].toString()) supported = true;
        if (!supported) { fail(tr("%1: the selected camera mode is unavailable.").arg(Sources::label(camera))); return false; }
    }
    if (!saveSettings()) return false;
    stopScreenPreviews(); stopPreviews(); m_error.clear();
    QHash<QString, QString> tokens;
    for (auto it = m_screenFeeds.begin(); it != m_screenFeeds.end(); ++it) { it.value()->setError({}); tokens[it.key()] = it.value()->tokenPath(); }
    return m_session->start(m_settings, tokens);
}

void Backend::stopScreenPreviews() { for (const auto feed : m_screenFeeds) feed->stop(); }

// An approved selection, or a monitor captured directly, shows again by itself.
// Only chooseScreen() opens the picker.
void Backend::startScreenPreviews() {
    if (m_shuttingDown || m_session->busy() || !viewed() || !m_portalCache.isValid()) return;
    for (const auto &value : Sources::enabled(m_settings, "screen")) {
        const auto screen = value.toObject(); const QString target = screen["target"].toString();
        const auto feed = m_screenFeeds.value(screen["id"].toString());
        if (feed && !target.isEmpty() && (target != "portal" || feed->selected())) feed->start(target, false);
    }
}

bool Backend::chooseScreen(const QString &requested) {
    if (m_shuttingDown || m_session->busy()) return false;
    const QString id = !requested.isEmpty() ? requested : firstSource("screen").isEmpty() ? addSource("screen") : firstSource("screen");
    const auto feed = m_screenFeeds.value(id);
    if (!feed) return false;
    if (!m_portalCache.isValid()) { feed->setError(tr("Could not create a private screen-sharing session folder.")); emit statusChanged(); return false; }
    // One picker at a time, so it is always clear which source is being chosen.
    for (auto it = m_screenFeeds.begin(); it != m_screenFeeds.end(); ++it) if (it.key() != id && it.value()->selecting()) return false;
    const auto source = Sources::find(Sources::list(m_settings), id);
    if (!source["enabled"].toBool()) setSourceSetting(id, "enabled", true);
    if (source["target"] != "portal") setSourceSetting(id, "target", "portal");
    feed->start("portal", true); return true;
}

// Stop the camera and audio previews, asking every camera first so they finish together.
void Backend::stopPreviews() {
    m_restart.stop();
    if (!m_session->busy()) m_session->previewAudio(m_settings, false);
    for (const auto feed : m_cameraFeeds) feed->requestStop();
    for (const auto feed : m_cameraFeeds) feed->stop();
}

void Backend::stop() { if (m_session->busy()) m_session->stop(); }
void Backend::toggle() { if (m_session->busy()) stop(); else start(); }

void Backend::setWindowVisible(bool visible) {
    if (m_shuttingDown) return;
    m_visible = visible;
    for (const auto feed : m_screenFeeds) feed->monitor().setEnabled(viewed());
    if (m_session->busy()) return;
    if (!viewed()) { stopScreenPreviews(); stopPreviews(); }
    else m_restart.start();
}
void Backend::addPreviewClient() {
    ++m_previewClients;
    for (const auto feed : m_screenFeeds) feed->monitor().setEnabled(true);
    if (!m_session->busy()) m_restart.start();
}
void Backend::removePreviewClient() {
    m_previewClients = qMax(0, m_previewClients - 1);
    for (const auto feed : m_screenFeeds) feed->monitor().setEnabled(viewed());
    if (!viewed() && !m_session->busy()) { stopScreenPreviews(); stopPreviews(); }
}
bool Backend::recoverSession() { if (m_session->busy()) return false; stopPreviews(); return m_session->recover(m_settings["directory"].toString()); }

void Backend::openFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings["directory"].toString())); }

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

QJsonObject Backend::handle(const QJsonObject &request) {
    const auto command = request["command"].toString();
    const auto options = request["settings"].toObject();
    bool ok = true; QString added;
    if (command == "start") ok = start();
    else if (command == "stop") stop();
    else if (command == "toggle") toggle();
    else if (command == "recover") ok = recoverSession();
    else if (command == "choose-screen") ok = chooseScreen(request["id"].toString());
    else if (command == "open") emit openRequested();
    else if (command == "configure") {
        for (auto it = options.begin(); it != options.end(); ++it) if (!setSetting(it.key(), it.value().toVariant())) ok = false;
    } else if (command == "source-add") {
        added = addSource(request["kind"].toString()); ok = !added.isEmpty();
        if (ok) for (auto it = options.begin(); it != options.end(); ++it) if (!setSourceSetting(added, it.key(), it.value().toVariant())) ok = false;
    } else if (command == "source-set") {
        ok = !options.isEmpty();
        for (auto it = options.begin(); it != options.end(); ++it) if (!setSourceSetting(request["id"].toString(), it.key(), it.value().toVariant())) ok = false;
    } else if (command == "source-remove") ok = removeSource(request["id"].toString());
    else if (command != "status" && command != "settings" && command != "devices" && command != "ensure") ok = false;
    QJsonObject result = QJsonObject::fromVariantMap(status());
    result["ok"] = ok;
    if (!added.isEmpty()) result["id"] = added;
    if (command == "settings" || command == "configure" || command.startsWith("source-")) result["settings"] = QJsonObject::fromVariantMap(settings());
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
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) return fail(tr("Could not create the Source Roll settings folder."));
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly) || output.write(doc.toJson()) < 0) return fail(tr("Could not import previous settings."));
    if (QFileInfo::exists(target)) { output.cancelWriting(); return true; }
    return output.commit() || fail(tr("Could not save imported settings."));
}
