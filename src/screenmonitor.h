#pragma once
#include <QObject>
#include <QProcess>
#include <QImage>
#include <QElapsedTimer>

// Decodes a disposable copy of the recorded screen stream, never a second capture.
class ScreenMonitor : public QObject {
    Q_OBJECT
public:
    explicit ScreenMonitor(QObject *parent = nullptr);
    ~ScreenMonitor() override;
    void setEnabled(bool enabled);
    void start();
    void stop();
    void feed(const QByteArray &bytes);
    QImage image() const { return m_image; }
    QByteArray jpeg() const { return m_jpeg; }
    int frameNumber() const { return m_frameNumber; }
    QString state() const;
    QString error() const { return m_error; }
    static QStringList arguments();
signals:
    void frameReady();
private:
    void launch();
    void stopDecoder();
    void consume();
    QProcess m_decoder;
    QByteArray m_frames, m_jpeg, m_log;
    QImage m_image;
    QElapsedTimer m_lastFrame, m_started;
    QString m_error;
    int m_frameNumber = 0;
    bool m_active = false, m_enabled = false, m_stopping = false, m_hasInput = false;
};
