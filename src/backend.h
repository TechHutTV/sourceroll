#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QJsonObject>
#include <QImage>
#include <optional>
#include "screenmonitor.h"
#include <QTemporaryDir>
class SessionRecorder;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList microphones READ microphones NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList modes READ modes NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(int frameNumber READ frameNumber NOTIFY frameReady)
    Q_PROPERTY(int screenFrameNumber READ screenFrameNumber NOTIFY screenFrameReady)
    Q_PROPERTY(double peakDb READ peakDb NOTIFY peakChanged)
    Q_PROPERTY(double desktopPeakDb READ desktopPeakDb NOTIFY peakChanged)
    Q_PROPERTY(QVariantList screens READ screens NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList desktopSources READ desktopSources NOTIFY devicesChanged)
public:
    explicit Backend(const QString &configFile = {}, QObject *parent = nullptr);
    ~Backend() override;
    QVariantList cameras() const { return m_cameras; }
    QVariantList microphones() const { return m_microphones; }
    QVariantList modes() const;
    QVariantMap settings() const { return m_settings.toVariantMap(); }
    QVariantMap status() const;
    int frameNumber() const { return m_frameNumber; }
    double peakDb() const { return m_peak; }
    double desktopPeakDb() const { return m_desktopPeak; }
    QVariantList screens() const { return m_screens; }
    QVariantList desktopSources() const { return m_desktopSources; }
    QImage previewImage() const { return m_image; }
    QByteArray previewJpeg() const { return m_previewJpeg; }
    int screenFrameNumber() const { return m_screenMonitor.frameNumber(); }
    QImage screenImage() const { return m_screenMonitor.image(); }
    QByteArray screenJpeg() const { return m_screenMonitor.jpeg(); }
    void addPreviewClient();
    void removePreviewClient();
    QJsonObject handle(const QJsonObject &request);
    static QVariantList cameraModes(const QString &device);
    static QStringList ffmpegArguments(const QJsonObject &settings, const QString &output, bool recording);
    static bool validSettings(const QJsonObject &settings, QString *error);
    static QString socketPath();
    static bool omaControlOpen(const QByteArray &layers);
    static std::optional<double> parsePeak(const QByteArray &line);
    static bool importLegacySettings(const QString &legacy, const QString &target, QString *error = nullptr);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE bool setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void setWindowVisible(bool visible);
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE bool recoverSession();
    Q_INVOKABLE bool chooseScreen();
signals:
    void devicesChanged();
    void settingsChanged();
    void statusChanged();
    void frameReady();
    void screenFrameReady();
    void peakChanged();
    void openRequested();
private:
    void launch(bool recording);
    void consumeVideo();
    void consumeMessages();
    void finish(int code, QProcess::ExitStatus exitStatus);
    void stopProcess();
    void stopScreenPreview();
    void startScreenPreview(bool choose);
    bool saveSettings();
    void fail(const QString &message);
    QProcess m_process, m_displayQuery;
    QTimer m_displayPoll;
    bool m_displaySettingsOpen = false;
    QString m_lastNotice;
    ScreenMonitor m_screenMonitor;
    QProcess m_screenPreviewProcess;
    QTemporaryDir m_portalCache;
    QByteArray m_screenPreviewLog;
    QString m_screenSelectionError;
    bool m_screenSelected = false, m_screenPreviewStopping = false, m_screenPreviewHasData = false;
    SessionRecorder *m_session = nullptr;
    QTimer m_tick;
    QTimer m_meter;
    QTimer m_restart;
    QTimer m_watchdog;
    QTimer m_screenSelectTimer;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_lastFrame;
    QString m_configFile;
    QJsonObject m_settings;
    QVariantList m_cameras, m_microphones, m_screens, m_desktopSources;
    QByteArray m_frames, m_messages, m_log;
    QByteArray m_previewJpeg;
    QImage m_image;
    QString m_state = "idle", m_error, m_output, m_lastOutput;
    double m_peak = -90;
    double m_pendingPeak = -90;
    double m_desktopPeak = -90;
    qint64 m_recordedMs = 0;
    int m_frameNumber = 0;
    int m_previewClients = 0;
    bool m_recording = false, m_visible = false, m_stopping = false, m_shuttingDown = false;
};
