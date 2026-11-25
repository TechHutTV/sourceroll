#include "capturehealth.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

CaptureHealth::CaptureHealth(QObject *parent, const QString &program) : QObject(parent), m_program(program) {
    m_dump.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); if (getppid() == 1) _exit(1); });
    connect(&m_dump, &QProcess::readyReadStandardOutput, this, [this] { ingest(m_dump.readAllStandardOutput()); });
    connect(&m_dump, &QProcess::readyReadStandardError, this, [this] { m_dump.readAllStandardError(); });
    connect(&m_dump, &QProcess::finished, this, [this] { if (m_pid) { m_linked = false; emit unavailable(); } });
    connect(&m_dump, &QProcess::errorOccurred, this, [this] { if (m_pid) { m_linked = false; emit unavailable(); } });
}
CaptureHealth::~CaptureHealth() { stop(); }
void CaptureHealth::reset(qint64 pid) {
    m_pid = pid; m_objects.clear(); m_buffer.clear(); m_linked = false; m_reported = false;
}
void CaptureHealth::start(qint64 pid) {
    stop(); reset(pid);
    m_dump.start(m_program, {"--monitor", "--no-colors"});
}
void CaptureHealth::stop() {
    m_pid = 0;
    if (m_dump.state() != QProcess::NotRunning) { m_dump.kill(); m_dump.waitForFinished(1000); }
    reset(0);
}
void CaptureHealth::ingest(const QByteArray &bytes) {
    m_buffer += bytes;
    if (m_buffer.size() > 8 * 1024 * 1024) { m_buffer.clear(); emit unavailable(); return; }
    // pw-dump emits concatenated JSON arrays, including partial object updates.
    for (;;) {
        int depth = 0, end = -1; bool quoted = false, escaped = false;
        for (int i = 0; i < m_buffer.size(); ++i) {
            char c = m_buffer[i];
            if (quoted) { if (escaped) escaped = false; else if (c == '\\') escaped = true; else if (c == '"') quoted = false; continue; }
            if (c == '"') quoted = true;
            else if (c == '[' || c == '{') ++depth;
            else if (c == ']' || c == '}') { if (--depth == 0) { end = i; break; } }
        }
        if (end < 0) return;
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(m_buffer.left(end + 1), &error);
        m_buffer.remove(0, end + 1);
        if (error.error != QJsonParseError::NoError || !doc.isArray()) { emit unavailable(); continue; }
        for (const auto &value : doc.array()) {
            const auto patch = value.toObject(); const int id = patch["id"].toInt(-1);
            if (id < 0) continue;
            if (patch.contains("info") && patch["info"].isNull()) { m_objects.remove(id); continue; }
            auto object = m_objects.value(id);
            for (auto i = patch.begin(); i != patch.end(); ++i) {
                if (i.key() == "info") {
                    auto info = object["info"].toObject(); const auto update = i.value().toObject();
                    for (auto j = update.begin(); j != update.end(); ++j) info[j.key()] = j.value();
                    object["info"] = info;
                } else object[i.key()] = i.value();
            }
            m_objects[id] = object;
        }
        inspect();
    }
}
void CaptureHealth::inspect() {
    if (!m_pid || m_reported) return;
    auto number = [](const QJsonValue &v) { return v.toVariant().toLongLong(); };
    QSet<int> clients, nodes;
    for (auto i = m_objects.begin(); i != m_objects.end(); ++i) {
        const auto props = i.value()["info"].toObject()["props"].toObject();
        if (i.value()["type"].toString().endsWith(":Client") && number(props["application.process.id"]) == m_pid) clients.insert(i.key());
    }
    QString reason;
    for (auto i = m_objects.begin(); i != m_objects.end(); ++i) {
        const auto info = i.value()["info"].toObject(), props = info["props"].toObject();
        if (!i.value()["type"].toString().endsWith(":Node")) continue;
        if (number(props["application.process.id"]) == m_pid || clients.contains(int(number(props["client.id"])))) {
            nodes.insert(i.key());
            if (info["state"] == "error") reason = tr("The screen capture node reported an error.");
        }
    }
    bool linked = false;
    for (const auto &object : m_objects) {
        const auto info = object["info"].toObject();
        if (!object["type"].toString().endsWith(":Link") ||
            (!nodes.contains(int(number(info["input-node-id"]))) && !nodes.contains(int(number(info["output-node-id"]))))) continue;
        if (info["state"] == "error") reason = tr("PipeWire could not allocate screen capture buffers.");
        if (info["state"] == "active" || info["state"] == "paused") linked = true;
    }
    if (m_linked && !linked && reason.isEmpty()) reason = tr("The screen capture connection was lost.");
    if (linked) m_linked = true;
    if (!reason.isEmpty()) { m_reported = true; emit failed(reason); }
}
