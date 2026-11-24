#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QDir>
#include "backend.h"
#include "theme.h"
#include "translator.h"

class DemoBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cameras READ cameras CONSTANT)
    Q_PROPERTY(QVariantList microphones READ microphones CONSTANT)
    Q_PROPERTY(QVariantList modes READ modes CONSTANT)
    Q_PROPERTY(QVariantMap settings READ settings CONSTANT)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(int frameNumber READ frameNumber CONSTANT)
public:
    QVariantList cameras() const { return {QVariantMap{{"id", "demo"}, {"label", "Studio Camera"}}}; }
    QVariantList microphones() const { return {QVariantMap{{"id", "mic"}, {"label", "Studio Microphone"}}}; }
    QVariantList modes() const { return {QVariantMap{{"id", "1080"}, {"label", "1920 × 1080 · 30 fps · MJPEG"}}}; }
    QVariantMap settings() const { return {{"camera", "demo"}, {"audio", "mic"}, {"mode", "1080"}, {"directory", "/Videos/Source Record"}, {"encoding", "original"}}; }
    QVariantMap status() const { return {{"state", recording ? "recording" : "preview"}, {"recording", recording}, {"preview", true}, {"elapsedSeconds", 83}, {"bytes", 155000000}, {"peakDb", -18}, {"error", ""}, {"output", "/Videos/Source Record/camera-demo.mkv"}}; }
    int frameNumber() const { return 1; }
    Q_INVOKABLE void setWindowVisible(bool) {}
    Q_INVOKABLE void toggle() { recording = !recording; emit statusChanged(); }
    Q_INVOKABLE void stop() { recording = false; emit statusChanged(); }
    Q_INVOKABLE void openFolder() {}
    Q_INVOKABLE void refreshDevices() {}
    Q_INVOKABLE bool setSetting(const QString &, const QVariant &) { return true; }
    bool recording = false;
signals:
    void statusChanged();
};

class DemoPreview : public QQuickImageProvider {
public:
    DemoPreview() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        QImage image(640, 360, QImage::Format_RGB32);
        for (int y = 0; y < 360; ++y) for (int x = 0; x < 640; ++x) {
            const bool circle = (x - 320)*(x - 320)+(y - 180)*(y - 180) < 95*95;
            image.setPixelColor(x,y,circle ? QColor("#ffd60a") : QColor(18+x/18, 24+y/15, 38+x/25));
        }
        *size = image.size(); return image;
    }
};

class RecorderTests : public QObject {
    Q_OBJECT
private:
    QJsonObject settings() {
        return {{"camera", "/dev/video0"}, {"mode", "1920x1080@30/1:mjpeg"}, {"audio", "studio mic;$(false)"}, {"directory", "/tmp/my clips"}, {"encoding", "original"}};
    }
    void write(const QString &path, const QByteArray &contents) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(contents), contents.size());
    }
private slots:
    void validatesModesAndPaths() {
        auto s = settings(); QString error;
        QVERIFY(Backend::validSettings(s, &error));
        s["mode"] = "1920x1080@30/0:mjpeg"; QVERIFY(!Backend::validSettings(s, &error));
        s["mode"] = "1080p; rm anything"; QVERIFY(!Backend::validSettings(s, &error));
        s = settings(); s["directory"] = "relative"; QVERIFY(!Backend::validSettings(s, &error));
    }
    void nativeVideoCopiedAndInputsRemainLiteral() {
        auto args = Backend::ffmpegArguments(settings(), "/tmp/my clips/a.mkv", true);
        QVERIFY(args.contains("-n"));
        QCOMPARE(args[args.indexOf("-c:v") + 1], "copy");
        QVERIFY(args.contains("/tmp/my clips/a.mkv"));
        QVERIFY(args.contains("studio mic;$(false)"));
        QVERIFY(args.contains("pcm_s16le"));
        QCOMPARE(args.count("/dev/video0"), 1);
    }
    void silentRecordingHasNoAudioInput() {
        auto s = settings(); s["audio"] = "none";
        auto args = Backend::ffmpegArguments(s, "/tmp/a.mkv", true);
        QVERIFY(!args.contains("pulse")); QVERIFY(!args.contains("1:a:0"));
        auto preview = Backend::ffmpegArguments(s, {}, false);
        QVERIFY(!preview.contains("matroska")); QVERIFY(preview.contains("image2pipe"));
    }
    void rawAndCompressedModesEncodeCorrectly() {
        auto s = settings(); s["mode"] = "1920x1080@30/1:yuyv422";
        auto args = Backend::ffmpegArguments(s, "/tmp/a.mkv", true);
        QCOMPARE(args[args.indexOf("-c:v") + 1], "libx264");
        s = settings(); s["encoding"] = "h264";
        args = Backend::ffmpegArguments(s, "/tmp/a.mkv", true);
        QVERIFY(args.contains("18")); QVERIFY(!args.contains("copy"));
    }
    void settingsAreAtomicAndMissingDeviceFails() {
        QTemporaryDir dir; Backend backend(dir.filePath("settings.json"));
        QVERIFY(backend.setSetting("audio", "none"));
        QFile file(dir.filePath("settings.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["audio"].toString(), "none");
        QVERIFY(!backend.setSetting("unknown", "value"));
        QVERIFY(backend.setSetting("camera", "/dev/source-record-does-not-exist"));
        QVERIFY(!backend.start()); QCOMPARE(backend.status()["state"].toString(), "error");
        QVERIFY(!backend.status()["recording"].toBool());
    }
    void themeAndLanguageFollowFiles() {
        QTemporaryDir dir; QDir(dir.path()).mkpath("theme");
        write(dir.filePath("theme/colors.toml"), "accent = \"#112233\"\n");
        Theme theme(dir.path()); QCOMPARE(theme.accent(), "#112233");
        write(dir.filePath("language"), "da\n");
        write(dir.filePath("da.json"), "{\"Open\":\"Åbn\"}");
        Translator translator(dir.filePath("language"), dir.path());
        QCOMPARE(translator.language(), "da"); QCOMPARE(translator.translate("Main", "Open"), "Åbn");
        write(dir.filePath("language"), "en\n");
        QTRY_COMPARE(translator.language(), "en");
        QVERIFY(translator.translate("Main", "Open").isNull());
    }
    void realQmlKeyboardHideAndScreenshots() {
        QQuickStyle::setStyle("Material");
        QTemporaryDir dir; write(dir.filePath("language"), "en\n");
        Theme theme(dir.path()); Translator translator(dir.filePath("language"));
        qApp->installTranslator(&translator);
        DemoBackend backend;
        QQmlApplicationEngine engine;
        engine.addImageProvider("preview", new DemoPreview);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("initialVisible", true);
        connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) { for (const auto &e : errors) warnings << e.toString(); });
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->requestActivate(); QTest::qWait(300);
        QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(backend.recording);
        QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(!backend.recording);
        auto screenshot = window->grabWindow(); QVERIFY(!screenshot.isNull());
        QString path = qEnvironmentVariable("SOURCE_RECORD_SCREENSHOT");
        if (!path.isEmpty()) QVERIFY(screenshot.save(path));
        write(dir.filePath("language"), "da\n"); QTRY_COMPARE(translator.language(), "da"); QTest::qWait(150);
        if (!path.isEmpty()) QVERIFY(window->grabWindow().save(QFileInfo(path).absolutePath() + "/screenshot-da.png"));
        QTest::keyClick(window, Qt::Key_Escape); QTRY_VERIFY(!window->isVisible());
        window->show(); QTest::qWait(50);
        QVERIFY(!window->close()); QVERIFY(!window->isVisible());
        window->show(); window->setProperty("allowQuit", true);
        QVERIFY(window->close());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        qApp->removeTranslator(&translator);
        QGuiApplication::setLayoutDirection(Qt::LeftToRight);
    }
};

QTEST_MAIN(RecorderTests)
#include "source-record_tests.moc"
