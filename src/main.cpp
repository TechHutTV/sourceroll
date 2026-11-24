#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QThread>
#include <QFile>
#include <QSocketNotifier>
#include <QDir>
#include <cstdio>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include "backend.h"
#include "theme.h"
#include "translator.h"

static int terminationPipe[2] = {-1, -1};
static void requestTermination(int) {
    const char byte = 1;
    if (terminationPipe[1] >= 0) { const auto ignored = ::write(terminationPipe[1], &byte, 1); (void)ignored; }
}

class PreviewProvider : public QQuickImageProvider {
public:
    explicit PreviewProvider(Backend *backend) : QQuickImageProvider(Image), m_backend(backend) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        auto image = m_backend->previewImage();
        if (image.isNull()) { image = QImage(640, 360, QImage::Format_RGB32); image.fill(QColor("#12151a")); }
        *size = image.size(); return image;
    }
private:
    Backend *m_backend;
};

static QJsonObject send(const QJsonObject &request) {
    QLocalSocket socket;
    socket.connectToServer(Backend::socketPath());
    if (!socket.waitForConnected(300)) return {};
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(500);
    QByteArray response;
    while (!response.contains('\n') && response.size() < 1024 * 1024) {
        if (!socket.bytesAvailable() && !socket.waitForReadyRead(12000)) break;
        response += socket.readAll();
    }
    return QJsonDocument::fromJson(response.trimmed()).object();
}

static int cli(const QStringList &args) {
    QString command = args.value(1, "status");
    if (command == "--help" || command == "help") {
        puts("Source Record — independent camera recording\n\nsource-record open\nsource-record status|start|stop|toggle|settings|devices [--json]\nsource-record configure --camera PATH --mode MODE --audio SOURCE --directory PATH --encoding original|h264 [--json]\nsource-record check [--json]\nsource-record skill install\n\nThe app and bar share one recorder. Closing the window keeps recording.\nRecordings use MKV; original MJPEG/H.264 camera streams are copied without re-encoding.");
        return 0;
    }
    if (command == "skill" && args.value(2) == "install") {
        const QString directory = QDir::homePath() + "/.codex/skills/source-record";
        QDir().mkpath(directory);
        QFile source(":/SKILL.md");
        if (!source.open(QIODevice::ReadOnly)) return 1;
        QFile target(directory + "/SKILL.md");
        if (target.exists()) { fputs("Skill already exists; refusing to overwrite.\n", stderr); return 1; }
        if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0) return 1;
        puts(qPrintable(target.fileName())); return 0;
    }
    if (command == "check") {
        auto tool = [](const QString &name) { return !QStandardPaths::findExecutable(name).isEmpty(); };
        QJsonObject result{{"ok", tool("ffmpeg") && tool("pactl")}, {"ffmpeg", tool("ffmpeg")}, {"pactl", tool("pactl")}};
        puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
        return result["ok"].toBool() ? 0 : 1;
    }
    if (!QStringList{"open", "status", "start", "stop", "toggle", "settings", "devices", "configure"}.contains(command)) {
        fputs("Unknown command. Run source-record --help.\n", stderr); return 1;
    }
    QJsonObject request{{"command", command}};
    if (command == "configure") {
        QJsonObject settings;
        for (int i = 2; i < args.size(); ++i) {
            const QString key = args[i].mid(2);
            if (args[i] == "--json") continue;
            if (!QStringList{"camera", "mode", "audio", "directory", "encoding"}.contains(key) || i + 1 >= args.size()) {
                fputs("Invalid configuration option.\n", stderr); return 1;
            }
            settings[key] = args[++i];
        }
        request["settings"] = settings;
    }
    auto response = send(request);
    if (response.isEmpty() && (command == "status" || command == "stop")) {
        response = {{"ok", true}, {"running", false}, {"recording", false}, {"state", "idle"}, {"elapsedSeconds", 0}, {"peakDb", -90}, {"error", ""}};
    } else if (response.isEmpty()) {
        QProcess resident;
        resident.setProgram(QCoreApplication::applicationFilePath());
        resident.setArguments({"--background"});
        // A resident must not inherit the CLI's stdout pipe, or bar collectors never finish.
        resident.setStandardInputFile(QProcess::nullDevice());
        resident.setStandardOutputFile(QProcess::nullDevice());
        const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/source-record";
        QDir().mkpath(stateDir);
        resident.setStandardErrorFile(stateDir + "/app.log", QIODevice::Append);
        if (!resident.startDetached()) { fputs("Could not start Source Record.\n", stderr); return 1; }
        for (int i = 0; i < 60 && response.isEmpty(); ++i) { QThread::msleep(100); response = send(request); }
    }
    if (response.isEmpty()) { fputs("Could not reach Source Record.\n", stderr); return 1; }
    puts(QJsonDocument(response).toJson(QJsonDocument::Compact).constData());
    if (!response["ok"].toBool()) { fputs(qPrintable(response["error"].toString() + '\n'), stderr); return 1; }
    return 0;
}

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString("open");
    const bool gui = command == "open" || command == "--background" || argc == 1;
    if (!gui) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("source-record");
        return cli(app.arguments());
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("source-record"); app.setApplicationVersion("0.1.2");
    app.setDesktopFileName("source-record");
    app.setWindowIcon(QIcon::fromTheme("source-record"));
    app.setQuitOnLastWindowClosed(false);
    // Deliver termination on Qt's event loop so the recorder can finish its file.
    if (pipe2(terminationPipe, O_NONBLOCK | O_CLOEXEC) != 0) return 1;
    struct sigaction action{};
    action.sa_handler = requestTermination; sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr); sigaction(SIGTERM, &action, nullptr);
    QSocketNotifier termination(terminationPipe[0], QSocketNotifier::Read);
    if (!send({{"command", "open"}}).isEmpty()) return 0;
    QLockFile lock(Backend::socketPath() + ".lock");
    if (!lock.tryLock(100)) return 0;
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(Backend::socketPath());
    if (!server.listen(Backend::socketPath())) return 1;
    QQuickStyle::setStyle("Material");
    Translator translator; app.installTranslator(&translator);
    Theme theme;
    Backend backend;
    QQmlApplicationEngine engine;
    engine.addImageProvider("preview", new PreviewProvider(&backend));
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("initialVisible", command != "--background");
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    QObject::connect(&server, &QLocalServer::newConnection, &backend, [&] {
        while (auto socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QLocalSocket::readyRead, &backend, [&, socket] {
                if (socket->bytesAvailable() > 65536) { socket->abort(); return; }
                if (!socket->canReadLine()) return;
                const auto request = QJsonDocument::fromJson(socket->readLine(65536)).object();
                const auto response = backend.handle(request);
                socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
                socket->flush(); socket->disconnectFromServer();
            });
        }
    });
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(&termination, &QSocketNotifier::activated, &app, [&] {
        window->setProperty("allowQuit", true);
        backend.stop();
        app.quit();
    });
    QObject::connect(&backend, &Backend::openRequested, window, [window] { window->show(); window->raise(); window->requestActivate(); });
    return app.exec();
}
