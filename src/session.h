#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include <QHash>
#include <QSet>
#include "capturehealth.h"

// Owns a recording session, including every child except the camera captures,
// which stay with their previews.
class SessionRecorder : public QObject {
    Q_OBJECT
public:
    explicit SessionRecorder(QObject *parent = nullptr, const QString &screenProgram = "gpu-screen-recorder", const QString &audioProgram = "ffmpeg", const QString &healthProgram = "pw-dump");
    ~SessionRecorder() override;
    bool busy() const { return m_busy; }
    QVariantMap status() const;
    // portalTokens gives each screen source id its portal restore-token file.
    bool start(const QJsonObject &settings, const QHash<QString, QString> &portalTokens = {});
    void stop(const QString &reason = {});
    void shutdown();
    void previewAudio(const QJsonObject &settings, bool enabled);
    void cameraTimestamp(const QString &source, qint64 realtimeUs);
    void cameraFinished(const QString &source, int code, const QString &error);
    bool recover(const QString &root);
    static qint64 monotonicUs();
    static QString recordingTimecode(double seconds);
    static qint64 packetTimestamp(const QByteArray &line);
    static QJsonObject timeline(const QJsonObject &manifest, const QString &directory);
    static bool validateSources(const QJsonObject &settings, QString *error);
    static QStringList audioArguments(const QString &source, const QString &file, bool meter);
    static QString screenOutput(const QString &file = "screen.partial.mkv");
signals:
    void screenStarting(const QString &source);
    void screenStopped(const QString &source);
    void screenData(const QString &source, const QByteArray &bytes);
    void changed();
    void cameraStartRequested(const QString &source, const QString &file);
    void cameraStopRequested(const QString &source);
    void levels(double microphone, double desktop);
    void finished();
private:
    // One screen source in the current take: its recorder child, the segment
    // it is writing, and its own reconnect state.
    struct Screen {
        explicit Screen(const QString &healthProgram) : watch(nullptr, healthProgram) {}
        QString source, name, stem, entry, target, quality, tokenPath, health = "starting";
        QString interruptionWarning, monitorWarning;
        int fps = 60, retries = 0;
        bool launched = false, recovering = false;
        qint64 launchUs = 0, retryUs = 0, progressUs = 0, size = 0, bytesUs = 0;
        QByteArray log;
        QProcess process;
        CaptureHealth watch;
    };
    Screen *addScreen(const QJsonObject &source, const QString &stem, const QString &tokenPath);
    void clearScreens();
    void launchSources();
    void launchScreens();
    bool portalPending(const Screen *except) const;
    void launchScreen(Screen *screen);
    void pollScreen(Screen *screen);
    void screenFailure(Screen *screen, const QString &reason);
    void screenFinished(Screen *screen, int code);
    void screenResumed(Screen *screen, qint64 monoUs);
    void monitoringUnavailable(Screen *screen);
    void readScreenLog(Screen *screen);
    void startAudio(int index, const QString &source, const QString &file);
    void readAudio(int index);
    void audioFinished(int index, int code);
    void timestamp(const QString &entry, qint64 monoUs, qint64 realUs);
    void poll();
    void save();
    void validateNext();
    void probeFinished(int code);
    void complete();
    void stopPreview();
    QJsonObject entry(const QString &id) const;
    void updateEntry(const QString &id, const QJsonObject &value);
    bool allStarted() const;
    bool workersRunning() const;
    QString warning() const;
    QString interruptionWarning() const;
    QString monitorWarning() const;
    QString m_screenProgram, m_audioProgram, m_healthProgram;
    QList<Screen *> m_screens;
    // Camera source id -> the manifest entry it records into.
    QHash<QString, QString> m_cameraEntries;
    QSet<QString> m_camerasRunning;
    QProcess m_audio[2], m_probe;
    QByteArray m_audioLines[2], m_audioLog[2];
    QJsonObject m_settings, m_manifest;
    QString m_directory, m_state = "idle", m_error, m_warning;
    // Warnings are kept per topic so one cannot erase another; warning() joins them for display.
    QString m_interruptionWarning, m_monitorWarning, m_timingWarning;
    QTimer m_poll, m_meter;
    QElapsedTimer m_stopClock;
    double m_pending[2] = {-90, -90}, m_peak[2] = {-90, -90};
    qint64 m_epochMono = 0, m_epochReal = 0, m_started = 0, m_sourcesStarted = 0;
    bool m_busy = false, m_stopping = false, m_sourcesLaunched = false, m_preview = false, m_shutdown = false;
    int m_validationIndex = 0;
};
