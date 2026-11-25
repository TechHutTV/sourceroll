#include "sources.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

namespace {
const QStringList reservedStems{"microphone", "desktop", "session", "timeline"};
const QStringList qualities{"high", "very_high", "ultra"};

bool validId(const QString &id) {
    static const QRegularExpression pattern("^[a-z0-9][a-z0-9-]{0,31}$");
    return pattern.match(id).hasMatch();
}

// A reconnected screen is saved as "<stem>-002" and up.
bool segmentOf(const QString &name, const QString &stem) {
    static const QRegularExpression suffix("^-[0-9]{3}$");
    return name.startsWith(stem) && suffix.match(name.mid(stem.size())).hasMatch();
}

QString slug(const QString &name) {
    QString out;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber()) out += c;
        else if (!out.isEmpty() && !out.endsWith('-')) out += '-';
    }
    out = out.left(48);
    while (out.endsWith('-')) out.chop(1);
    return out;
}
}

QJsonArray Sources::list(const QJsonObject &s) {
    if (s.contains("videoSources")) return s["videoSources"].toArray();
    return {
        QJsonObject{{"id", "screen"}, {"kind", "screen"}, {"name", ""}, {"enabled", s["screenEnabled"].toBool(false)},
            {"target", s["screenTarget"].toString()}, {"fps", s["screenFps"].toInt(60)}, {"quality", s["screenQuality"].toString("very_high")}},
        QJsonObject{{"id", "camera"}, {"kind", "camera"}, {"name", ""}, {"enabled", s["cameraEnabled"].toBool(true)},
            {"device", s["camera"].toString()}, {"mode", s["mode"].toString()}, {"encoding", s["encoding"].toString("original")}}};
}

QJsonArray Sources::enabled(const QJsonObject &settings, const QString &kind) {
    QJsonArray result;
    for (const auto &value : list(settings)) {
        const auto source = value.toObject();
        if (source["enabled"].toBool() && (kind.isEmpty() || source["kind"] == kind)) result.append(source);
    }
    return result;
}

QJsonObject Sources::find(const QJsonArray &sources, const QString &id) {
    for (const auto &value : sources) if (value.toObject()["id"] == id) return value.toObject();
    return {};
}

QJsonObject Sources::first(const QJsonArray &sources, const QString &kind) {
    for (const auto &value : sources) if (value.toObject()["kind"] == kind) return value.toObject();
    return {};
}

QJsonArray Sources::sanitized(const QJsonArray &sources) {
    QJsonArray result;
    for (const auto &value : sources) {
        if (result.size() >= maximum) break;
        const auto given = value.toObject();
        const QString kind = given["kind"].toString();
        if (kind != "screen" && kind != "camera") continue;
        QString id = given["id"].toString();
        if (!validId(id) || !find(result, id).isEmpty()) id = newId(result, kind);
        QJsonObject source{{"id", id}, {"kind", kind}, {"enabled", given["enabled"].toBool()}};
        source["name"] = uniqueName(result, cleanName(given["name"].toString()), id);
        if (kind == "screen") {
            source["target"] = given["target"].toString().isEmpty() ? "portal" : given["target"].toString().left(4096);
            source["fps"] = qBound(15, given["fps"].toInt(60), 120);
            source["quality"] = qualities.contains(given["quality"].toString()) ? given["quality"].toString() : "very_high";
        } else {
            source["device"] = given["device"].toString().left(4096);
            source["mode"] = given["mode"].toString().left(64);
            source["encoding"] = given["encoding"] == "h264" ? "h264" : "original";
        }
        result.append(source);
    }
    return result;
}

QJsonObject Sources::migrated(QJsonObject settings) {
    settings["videoSources"] = sanitized(list(settings));
    for (const char *key : {"camera", "mode", "encoding", "cameraEnabled", "screenEnabled", "screenTarget", "screenFps", "screenQuality"})
        settings.remove(key);
    return settings;
}

QString Sources::newId(const QJsonArray &sources, const QString &kind) {
    for (int n = 1;; ++n) {
        const QString id = n == 1 ? kind : QString("%1-%2").arg(kind).arg(n);
        if (find(sources, id).isEmpty()) return id;
    }
}

int Sources::number(const QString &id) {
    static const QRegularExpression tail("-([0-9]+)$");
    const auto match = tail.match(id);
    return match.hasMatch() ? qMax(1, match.captured(1).toInt()) : 1;
}

QString Sources::label(const QJsonObject &source) {
    const QString name = source["name"].toString();
    if (!name.isEmpty()) return name;
    const QString kind = source["kind"] == "screen" ? QCoreApplication::translate("Sources", "Screen") : QCoreApplication::translate("Sources", "Camera");
    const int n = number(source["id"].toString());
    return n > 1 ? QString("%1 %2").arg(kind).arg(n) : kind;
}

QString Sources::cleanName(const QString &name) {
    QString clean;
    for (const QChar c : name) clean += c.isPrint() && c != '/' && c != '\\' ? c : QChar(' ');
    return clean.simplified().left(40).trimmed();
}

QString Sources::uniqueName(const QJsonArray &sources, const QString &wanted, const QString &exceptId) {
    if (wanted.isEmpty()) return {};
    QSet<QString> taken;
    for (const auto &value : sources) if (value.toObject()["id"] != exceptId) taken.insert(label(value.toObject()).toCaseFolded());
    QString name = wanted;
    for (int n = 2; taken.contains(name.toCaseFolded()); ++n) name = QString("%1 %2").arg(wanted).arg(n);
    return name;
}

QString Sources::fileStem(const QJsonObject &source) {
    const QString named = slug(source["name"].toString());
    return named.isEmpty() ? source["id"].toString() : named;
}

QStringList Sources::fileStems(const QJsonArray &sources) {
    QStringList stems;
    auto clashes = [&](const QString &stem) {
        for (const auto &taken : reservedStems + stems)
            if (stem == taken || segmentOf(stem, taken) || segmentOf(taken, stem)) return true;
        return false;
    };
    for (const auto &value : sources) {
        const QString wanted = fileStem(value.toObject());
        QString stem = wanted;
        for (int n = 2; clashes(stem); ++n) stem = QString("%1-%2").arg(wanted).arg(n);
        stems << stem;
    }
    return stems;
}
