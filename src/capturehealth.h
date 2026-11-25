#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QHash>

// Watches graph state, not image similarity: a still desktop is healthy.
class CaptureHealth : public QObject {
    Q_OBJECT
public:
    explicit CaptureHealth(QObject *parent = nullptr, const QString &program = "pw-dump");
    ~CaptureHealth() override;
    void start(qint64 recorderPid);
    void stop();
    void ingest(const QByteArray &bytes);
    void reset(qint64 recorderPid);
    bool linked() const { return m_linked; }
signals:
    void failed(const QString &reason);
    void unavailable();
private:
    void inspect();
    QProcess m_dump;
    QString m_program;
    QByteArray m_buffer;
    QHash<int, QJsonObject> m_objects;
    qint64 m_pid = 0;
    bool m_linked = false, m_reported = false;
};
