#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Screens and cameras are kept as one list. Each entry has a stable id, used
// for settings and state, and an optional name, used for labels and files:
//   {"id": "screen-2", "kind": "screen", "name": "Left monitor", "enabled": true,
//    "target": "DP-3", "fps": 60, "quality": "very_high"}
//   {"id": "camera", "kind": "camera", "name": "", "enabled": true,
//    "device": "/dev/video0", "mode": "1920x1080@30/1:mjpeg", "encoding": "original"}
namespace Sources {
constexpr int maximum = 8;

// The "videoSources" list, or the single screen and camera of earlier settings.
QJsonArray list(const QJsonObject &settings);
QJsonArray enabled(const QJsonObject &settings, const QString &kind = {});
QJsonObject find(const QJsonArray &sources, const QString &id);
QJsonObject first(const QJsonArray &sources, const QString &kind);

// Settings with a checked "videoSources" list and without the earlier keys.
QJsonObject migrated(QJsonObject settings);
QJsonArray sanitized(const QJsonArray &sources);

QString newId(const QJsonArray &sources, const QString &kind);
int number(const QString &id);
// The name shown for a source: its own, or "Screen", "Camera 2" and so on.
QString label(const QJsonObject &source);
QString cleanName(const QString &name);
QString uniqueName(const QJsonArray &sources, const QString &wanted, const QString &exceptId = {});

// File names without extension, one per source and in list order. They never
// collide with each other, the audio files, or a reconnected segment ("-002").
QString fileStem(const QJsonObject &source);
QStringList fileStems(const QJsonArray &sources);
}
