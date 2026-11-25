#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QHash>
#include <optional>
#include <QTemporaryDir>
class SessionRecorder;
class CameraFeed;
class ScreenFeed;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList microphones READ microphones NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(int sourceLimit READ sourceLimit CONSTANT)
    Q_PROPERTY(double peakDb READ peakDb NOTIFY peakChanged)
    Q_PROPERTY(double desktopPeakDb READ desktopPeakDb NOTIFY peakChanged)
    Q_PROPERTY(QVariantList screens READ screens NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList desktopSources READ desktopSources NOTIFY devicesChanged)
public:
    // A config file or capture programs are only given by tests; such a backend
    // also keeps its notices off the desktop.
    explicit Backend(const QString &configFile = {}, QObject *parent = nullptr,
        const QString &screenProgram = "gpu-screen-recorder", const QString &cameraProgram = "ffmpeg");
    ~Backend() override;
    QVariantList cameras() const { return m_cameras; }
    QVariantList microphones() const { return m_microphones; }
    // Stored settings, plus the earlier single-source keys for the first screen and camera.
    QVariantMap settings() const;
    QVariantMap status() const;
    int sourceLimit() const;
    double peakDb() const { return m_peak; }
    double desktopPeakDb() const { return m_desktopPeak; }
    QVariantList screens() const { return m_screens; }
    QVariantList desktopSources() const { return m_desktopSources; }
    // The live preview of one video source, by its id.
    Q_INVOKABLE int frame(const QString &source) const;
    QImage sourceImage(const QString &source) const;
    QByteArray sourceJpeg(const QString &source) const;
    QString firstSource(const QString &kind) const;
    void addPreviewClient();
    void removePreviewClient();
    QJsonObject handle(const QJsonObject &request);
    static QVariantList cameraModes(const QString &device);
    static bool validSettings(const QJsonObject &settings, QString *error);
    // One of the enabled cameras: its own settings, and no device shared with an earlier one.
    static bool validCamera(const QJsonArray &cameras, int index, QString *error);
    static QString socketPath();
    static bool omaControlOpen(const QByteArray &layers);
    static std::optional<double> parsePeak(const QByteArray &line);
    static bool importLegacySettings(const QString &legacy, const QString &target, QString *error = nullptr);
    Q_INVOKABLE void refreshDevices();
    // Look for cameras that were plugged in or removed, leaving the previews running.
    Q_INVOKABLE void rescanCameras();
    // Recording-wide settings. The earlier keys for "the" screen and camera
    // (camera, mode, screenTarget, ...) still work and act on the first of each.
    Q_INVOKABLE bool setSetting(const QString &key, const QVariant &value);
    // Returns the new source's id, or nothing when it cannot be added. A camera
    // source takes the first connected camera that no other source uses.
    Q_INVOKABLE QString addSource(const QString &kind);
    Q_INVOKABLE bool removeSource(const QString &id);
    Q_INVOKABLE bool setSourceSetting(const QString &id, const QString &key, const QVariant &value);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void setWindowVisible(bool visible);
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE bool recoverSession();
    // Opens the picker for a screen source; without an id, for the first one.
    Q_INVOKABLE bool chooseScreen(const QString &id = {});
signals:
    void devicesChanged();
    void settingsChanged();
    void statusChanged();
    void sourceFrame(const QString &source, int number);
    void peakChanged();
    void openRequested();
private:
    void launch();
    void syncFeeds();
    void stopPreviews();
    void stopScreenPreviews();
    void startScreenPreviews();
    QString createSource(const QString &kind, const QString &device);
    bool setKindEnabled(const QString &kind, bool enabled);
    bool viewed() const { return m_visible || m_previewClients > 0; }
    static QVariantList scanCameras();
    QString unusedCamera(const QJsonArray &sources) const;
    QString defaultMode(const QString &device, bool preferFullHd) const;
    QVariantList modesOf(const QString &device) const;
    QString label(const QString &id) const;
    bool saveSettings();
    void fail(const QString &message);
    QProcess m_displayQuery;
    QTimer m_displayPoll;
    bool m_displaySettingsOpen = false;
    QString m_lastNotice;
    QTemporaryDir m_portalCache;
    SessionRecorder *m_session = nullptr;
    // One feed per video source, by source id.
    QHash<QString, CameraFeed *> m_cameraFeeds;
    QHash<QString, ScreenFeed *> m_screenFeeds;
    QTimer m_tick;
    QTimer m_restart;
    QString m_configFile, m_screenProgram, m_cameraProgram;
    QJsonObject m_settings;
    QVariantList m_cameras, m_microphones, m_screens, m_desktopSources;
    QString m_error;
    double m_peak = -90;
    double m_desktopPeak = -90;
    int m_previewClients = 0;
    bool m_visible = false, m_shuttingDown = false;
};
