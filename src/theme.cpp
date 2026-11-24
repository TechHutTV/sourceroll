// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

// Only scalar theme tokens are needed here; ignore comments, arrays and executable content.
static QVariantMap readTokens(const QString &path) {
    QVariantMap values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return values;
    QString section;
    const QRegularExpression entry(R"re(^\s*([\w-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([0-9.]+|true|false))\s*(?:#.*)?$)re");
    for (auto line : QString::fromUtf8(file.read(1024 * 1024)).split('\n')) {
        line = line.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) { section = line.mid(1, line.size() - 2); continue; }
        const auto match = entry.match(line);
        if (!match.hasMatch()) continue;
        const auto value = match.captured(2).isNull() ? (match.captured(3).isNull() ? match.captured(4) : match.captured(3)) : match.captured(2);
        values[(section.isEmpty() ? QString() : section + '.') + match.captured(1)] = value;
    }
    return values;
}

static QColor tokenColor(const QVariantMap &values, const QString &key, const QColor &fallback) {
    const QColor value(values.value(key).toString());
    return value.isValid() ? value : fallback;
}

static QColor withAlpha(QColor color, double alpha) { color.setAlphaF(qBound(0.0, alpha, 1.0)); return color; }

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readAccent(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return "#FFD60A";
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml", m_directory + "/theme/shell.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto palette = readTokens(m_directory + "/theme/colors.toml");
    const auto shell = readTokens(m_directory + "/theme/shell.toml");
    const bool dark = palette.value("mode", "dark").toString() != "light";
    const QColor background = tokenColor(palette, "background", dark ? QColor("#121212") : QColor("#fafafa"));
    const QColor foreground = tokenColor(palette, "foreground", dark ? QColor("#bebebe") : QColor("#242424"));
    const QColor accent = tokenColor(palette, "accent", QColor("#FFD60A"));
    QVariantMap colors{
        {"background", background}, {"foreground", foreground}, {"accent", accent},
        {"surface", tokenColor(palette, "lighter_background", background)},
        {"preview", tokenColor(palette, "darker_background", background.darker(120))},
        {"secondary", tokenColor(palette, "light_foreground", dark ? QColor("#8a8a8d") : QColor("#686868"))},
        {"muted", tokenColor(palette, "muted", dark ? background.lighter(160) : background.darker(120))},
        {"selection", tokenColor(palette, "selection", accent)},
        {"urgent", tokenColor(shell, "bar.active", tokenColor(palette, "red", QColor("#d35f5f")))}
    };
    auto number = [&](const QString &key, double fallback) {
        bool ok = false; const double value = shell.value(key).toString().toDouble(&ok);
        return ok && std::isfinite(value) ? value : fallback;
    };
    auto controlColor = [&](const QString &state) {
        const QString token = shell.value("controls." + state + "-color", "foreground").toString();
        return tokenColor(shell, "controls." + state + "-color", colors.value(token, foreground).value<QColor>());
    };
    const double base = qBound(10.0, number("font.base-size", 12), 24.0);
    QVariantMap style{
        {"fontSize", base}, {"controlHeight", qMax(28.0, number("spacing.control-height", 28 * base / 12))},
        {"normalFill", withAlpha(controlColor("normal"), number("controls.normal-fill-alpha", .04))},
        {"hoverFill", withAlpha(controlColor("hover-cursor"), number("controls.hover-cursor-fill-alpha", .08))},
        {"pressedFill", withAlpha(controlColor("hover-cursor"), number("controls.pressed-fill-alpha", .22))},
        {"normalBorder", withAlpha(tokenColor(shell, "controls.normal-border", foreground), number("controls.normal-border-alpha", .4))},
        {"hoverBorder", withAlpha(tokenColor(shell, "controls.hover-cursor-border", foreground), number("controls.hover-cursor-border-alpha", .25))},
        {"focusBorder", withAlpha(tokenColor(shell, "controls.focus-border", foreground), number("controls.focus-border-alpha", .25))},
        {"borderWidth", qBound(0.0, number("controls.normal-border-width", 1), 4.0)}
    };
    if (colors != m_colors || style != m_style || dark != m_dark) {
        m_colors = colors; m_style = style; m_dark = dark; m_accent = accent.name();
        emit changed();
    }
}
