#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QVariantMap style READ style NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QVariantMap colors() const { return m_colors; }
    QVariantMap style() const { return m_style; }
    bool dark() const { return m_dark; }

    static QString readAccent(const QString &path);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QVariantMap m_colors, m_style;
    bool m_dark = true;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
