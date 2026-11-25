#include "screenfeed.h"

#include <QFile>
#include <QFileInfo>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

QStringList ScreenFeed::arguments(const QString &target, const QString &tokenPath) {
    QStringList args{"-w", target, "-f", "15", "-k", "h264", "-q", "medium", "-fallback-cpu-encoding", "yes", "-keyint", "1"};
    if (target == "portal") args << "-restore-portal-session" << "yes" << "-portal-session-token-filepath" << tokenPath;
    return args << "-c" << "mpegts" << "-o" << "/dev/stdout";
}

ScreenFeed::ScreenFeed(const QString &tokenPath, QObject *parent, const QString &program)
    : QObject(parent), m_program(program), m_tokenPath(tokenPath) {
    m_process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGINT); if (getppid() == 1) _exit(1); });
    m_wait.setSingleShot(true);
    connect(&m_wait, &QTimer::timeout, this, [this] {
        if (m_hasData || !running()) return;
        const bool portal = m_portal;
        stop();
        m_error = portal ? tr("Screen selection timed out. Choose the source again.")
            : tr("This monitor did not deliver a preview. Use the screen/window picker if it cannot be captured directly.");
        emit changed();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = m_process.readAllStandardOutput();
        if (bytes.isEmpty() || m_stopping) return;
        m_wait.stop(); m_hasData = true; m_selected = true; m_monitor.feed(bytes); emit changed();
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        // Portal restore tokens are capabilities, so do not retain the recorder's token diagnostics.
        for (const auto &line : m_process.readAllStandardError().split('\n'))
            if (!line.contains("token") && (line.contains("error") || line.contains("failed") || line.contains("denied"))) m_log = (m_log + line + '\n').right(2000);
    });
    connect(&m_process, &QProcess::finished, this, [this](int code) {
        m_wait.stop();
        if (m_stopping) return;
        m_monitor.stop();
        const QString log = QString::fromUtf8(m_log.right(1200));
        m_error = m_portal ? tr("Screen sharing ended or was cancelled. Choose the source again. %1").arg(log) : tr("Screen preview stopped. %1").arg(log);
        if (code) m_selected = false;
        emit changed();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_wait.stop(); m_error = tr("Screen preview could not start: %1").arg(m_process.errorString()); m_monitor.stop(); emit changed();
    });
}

ScreenFeed::~ScreenFeed() { stop(); }

void ScreenFeed::start(const QString &target, bool choose) {
    const bool portal = target == "portal";
    if (!choose && (running() || (portal && !QFileInfo::exists(m_tokenPath)))) return;
    stop();
    if (choose && portal) { QFile::remove(m_tokenPath); m_selected = false; }
    m_portal = portal; m_hasData = false; m_error.clear(); m_log.clear();
    m_monitor.setEnabled(true); m_monitor.start();
    m_process.start(m_program, arguments(target, m_tokenPath));
    // The picker waits on a person; a monitor should answer almost at once.
    m_wait.start(portal ? 120000 : 15000);
    emit changed();
}

void ScreenFeed::stop() {
    m_wait.stop();
    if (!running()) return;
    m_stopping = true;
    if (m_process.processId() > 0) ::kill(m_process.processId(), SIGINT);
    if (!m_process.waitForFinished(3000)) { m_process.kill(); m_process.waitForFinished(1000); }
    m_monitor.stop(); m_stopping = false;
}

void ScreenFeed::forget() {
    stop(); m_selected = false; m_error.clear();
    QFile::remove(m_tokenPath);
}
