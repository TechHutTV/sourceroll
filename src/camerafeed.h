#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QImage>
#include <QElapsedTimer>
#include <QTimer>

// One camera's capture. A recording and its preview share a single process,
// because a V4L2 device can only be opened once.
class CameraFeed : public QObject {
    Q_OBJECT
public:
    explicit CameraFeed(QObject *parent = nullptr, const QString &program = "ffmpeg");
    ~CameraFeed() override;
    void preview(const QJsonObject &source);
    void record(const QJsonObject &source, const QString &file);
    // Ask the capture to finish and write its trailers; finished() follows.
    void requestStop();
    // The same, but wait for it.
    void stop();
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    bool recording() const { return m_recording; }
    // inactive, waiting (no frame yet), live or stalled.
    QString state() const;
    QImage image() const { return m_image; }
    QByteArray jpeg() const { return m_jpeg; }
    int frameNumber() const { return m_frameNumber; }
    static QStringList arguments(const QJsonObject &source, const QString &output, bool recording);
    static bool valid(const QJsonObject &source, QString *error);
signals:
    void frameReady();
    void changed();
    // The device's own clock for the first recorded packets.
    void timestamp(qint64 realtimeUs);
    // No first frame after ten seconds (starting), or none for six seconds later on.
    void stalled(bool starting);
    void finished(int code, bool failed, const QString &log, bool recording);
private:
    void launch(const QJsonObject &source, const QString &file);
    void consumeVideo();
    void consumeMessages();
    QProcess m_process;
    QString m_program;
    QTimer m_check, m_kill;
    QElapsedTimer m_started, m_lastFrame;
    QByteArray m_frames, m_messages, m_log, m_jpeg;
    QImage m_image;
    int m_frameNumber = 0;
    bool m_recording = false, m_stopping = false, m_reported = false;
};
