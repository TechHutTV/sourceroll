#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>
#include "capturehealth.h"

// Owns a recording session, including every child except the shared camera capture.
class SessionRecorder : public QObject {
    Q_OBJECT
public:
    explicit SessionRecorder(QObject *parent = nullptr, const QString &screenProgram = "gpu-screen-recorder", const QString &audioProgram = "ffmpeg", const QString &healthProgram = "pw-dump");
    ~SessionRecorder() override;
    bool busy() const { return m_busy; }
    QVariantMap status() const;
    bool start(const QJsonObject &settings, const QString &portalTokenPath = {});
    void stop(const QString &reason = {});
    void shutdown();
    void previewAudio(const QJsonObject &settings, bool enabled);
    void cameraTimestamp(qint64 realtimeUs);
    void cameraFinished(int code, const QString &error);
    bool recover(const QString &root);
    static qint64 monotonicUs();
    static QString recordingTimecode(double seconds);
    static qint64 packetTimestamp(const QByteArray &line);
    static QJsonObject timeline(const QJsonObject &manifest, const QString &directory);
    static bool validateSources(const QJsonObject &settings, QString *error);
    static QStringList audioArguments(const QString &source, const QString &file, bool meter);
    static QString screenOutput(const QString &file = "screen.partial.mkv");
signals:
    void screenStarting();
    void screenStopped();
    void screenData(const QByteArray &bytes);
    void changed();
    void cameraStartRequested(const QString &file);
    void cameraStopRequested();
    void levels(double microphone, double desktop);
    void finished();
private:
    void launchSources();
    void launchScreen();
    void screenFailure(const QString &reason);
    void startAudio(int index, const QString &source, const QString &file);
    void readAudio(int index);
    void workerFinished(int index, int code);
    void timestamp(const QString &id, qint64 monoUs, qint64 realUs);
    void poll();
    void save();
    void validateNext();
    void probeFinished(int code);
    void complete();
    void stopPreview();
    QJsonObject source(const QString &id) const;
    void updateSource(const QString &id, const QJsonObject &value);
    bool workersRunning() const;
    QString warning() const;
    QString m_screenProgram, m_audioProgram;
    CaptureHealth m_captureHealth;
    QString m_screenId = "screen", m_portalTokenPath, m_screenHealth = "inactive";
    bool m_screenRecovering = false;
    int m_screenRetries = 0;
    qint64 m_screenLaunchUs = 0, m_retryUs = 0, m_screenProgressUs = 0, m_screenSize = 0, m_screenBytesUs = 0;
    QProcess m_screen, m_audio[2], m_probe;
    QByteArray m_audioLines[2], m_audioLog[2], m_screenLog;
    QJsonObject m_settings, m_manifest;
    QString m_directory, m_state = "idle", m_error, m_warning;
    // Warnings are kept per topic so one cannot erase another; warning() joins them for display.
    QString m_interruptionWarning, m_monitorWarning, m_timingWarning;
    QTimer m_poll, m_meter;
    QElapsedTimer m_stopClock;
    double m_pending[2] = {-90, -90}, m_peak[2] = {-90, -90};
    qint64 m_epochMono = 0, m_epochReal = 0, m_started = 0, m_sourcesStarted = 0;
    bool m_busy = false, m_stopping = false, m_cameraRunning = false, m_sourcesLaunched = false, m_preview = false, m_shutdown = false;
    int m_validationIndex = 0;
};
