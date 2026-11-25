#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include "screenmonitor.h"

// One screen source's preview before recording: its own capture child and,
// for the picker, its own restore token. During a take the recorder's stream
// feeds the same decoder, so the preview always shows what is being saved.
class ScreenFeed : public QObject {
    Q_OBJECT
public:
    explicit ScreenFeed(const QString &tokenPath, QObject *parent = nullptr, const QString &program = "gpu-screen-recorder");
    ~ScreenFeed() override;
    // choose opens the picker again; otherwise an approved selection is restored.
    void start(const QString &target, bool choose);
    void stop();
    // Stop and drop the selection, so the next preview has to ask again.
    void forget();
    void setError(const QString &error) { m_error = error; }
    void markSelected() { m_selected = true; }
    ScreenMonitor &monitor() { return m_monitor; }
    QString tokenPath() const { return m_tokenPath; }
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    bool selected() const { return m_selected; }
    // The picker is open and nothing has been approved yet.
    bool selecting() const { return running() && m_portal && !m_hasData; }
    QString state() const { return m_error.isEmpty() ? m_monitor.state() : QString("error"); }
    QString error() const { return m_error.isEmpty() ? m_monitor.error() : m_error; }
    static QStringList arguments(const QString &target, const QString &tokenPath);
signals:
    void changed();
private:
    ScreenMonitor m_monitor;
    QProcess m_process;
    QTimer m_wait;
    QString m_program, m_tokenPath, m_error;
    QByteArray m_log;
    bool m_portal = true, m_selected = false, m_stopping = false, m_hasData = false;
};
