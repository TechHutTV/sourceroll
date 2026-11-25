#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSaveFile>
#include <QBuffer>
#include <QQmlComponent>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QDir>
#include "backend.h"
#include "camerafeed.h"
#include "screenfeed.h"
#include "session.h"
#include "sources.h"
#include "theme.h"
#include "translator.h"

// Stands in for the recorder behind the real window: the same properties and
// calls, a list of sources that can be edited, and every preview "live".
class DemoBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cameras READ cameras CONSTANT)
    Q_PROPERTY(QVariantList microphones READ microphones CONSTANT)
    Q_PROPERTY(QVariantList screens READ screens CONSTANT)
    Q_PROPERTY(QVariantList desktopSources READ desktopSources CONSTANT)
    Q_PROPERTY(double desktopPeakDb READ desktopPeakDb CONSTANT)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(double peakDb READ peakDb CONSTANT)
    Q_PROPERTY(int sourceLimit READ sourceLimit CONSTANT)
public:
    QVariantList cameras() const {
        return {QVariantMap{{"id", "demo"}, {"label", "Studio Camera"}, {"modes", QVariantList{QVariantMap{{"id", "1080"}, {"label", "1920 × 1080 · 30 fps · MJPEG"}}}}},
            QVariantMap{{"id", "overhead"}, {"label", "Overhead Camera"}, {"modes", QVariantList{QVariantMap{{"id", "720"}, {"label", "1280 × 720 · 60 fps · MJPEG"}}, QVariantMap{{"id", "480"}, {"label", "640 × 480 · 30 fps · YUYV422"}}}}},
            QVariantMap{{"id", "refused"}, {"label", "Busy Camera"}, {"modes", QVariantList{}}}};
    }
    QVariantList microphones() const { return {QVariantMap{{"id", "mic"}, {"label", "Studio Microphone"}}}; }
    QVariantList screens() const { return {QVariantMap{{"id", "portal"}, {"label", "Screen/window picker"}}, QVariantMap{{"id", "DP-3"}, {"label", "DP-3 · Direct monitor capture"}}}; }
    QVariantList desktopSources() const { return {QVariantMap{{"id", "desktop"}, {"label", "Desktop audio"}}}; }
    double desktopPeakDb() const { return -28; }
    int sourceLimit() const { return Sources::maximum; }
    QVariantMap settings() const { return {{"audio", "mic"}, {"directory", "/Videos/Sourceroll"}, {"microphoneEnabled", true}, {"desktopEnabled", false}, {"monitorLayout", monitorLayout}, {"videoSources", sources}}; }
    QVariantMap status() const {
        QVariantList video;
        for (const auto &value : sources) {
            const auto source = value.toMap(); const bool on = source["enabled"].toBool();
            video.append(QVariantMap{{"id", source["id"]}, {"kind", source["kind"]}, {"name", source["name"]}, {"enabled", on}, {"previewState", on ? "live" : "inactive"},
                {"previewError", ""}, {"selected", true}, {"selecting", false}, {"recording", recording && on}});
        }
        return {{"state", recording ? "recording" : "preview"}, {"recording", recording}, {"preview", true}, {"videoSources", video}, {"elapsedSeconds", 83}, {"bytes", 155000000}, {"peakDb", -18}, {"error", ""}, {"warning", warning}, {"displayWarning", displayWarning}, {"output", "/Videos/Sourceroll/take-demo"}};
    }
    double peakDb() const { return -18; }
    QVariantMap source(const QString &id) const { for (const auto &value : sources) if (value.toMap()["id"] == id) return value.toMap(); return {}; }
    Q_INVOKABLE int frame(const QString &) const { return 1; }
    Q_INVOKABLE void setWindowVisible(bool) {}
    Q_INVOKABLE void toggle() { recording = !recording; emit statusChanged(); }
    Q_INVOKABLE void stop() { recording = false; emit statusChanged(); }
    Q_INVOKABLE void openFolder() {}
    Q_INVOKABLE void refreshDevices() {}
    Q_INVOKABLE void rescanCameras() { ++rescans; }
    Q_INVOKABLE bool recoverSession() { return true; }
    Q_INVOKABLE bool chooseScreen(const QString &id = {}) { chosen << id; emit screenChoiceRequested(); return true; }
    Q_INVOKABLE void shutdown() {}
    Q_INVOKABLE bool setSetting(const QString &key, const QVariant &value) { if (key == "monitorLayout") { monitorLayout = value.toString(); emit settingsChanged(); } return true; }
    Q_INVOKABLE QString addSource(const QString &kind) {
        const QString id = Sources::newId(QJsonArray::fromVariantList(sources), kind);
        if (kind == "screen") sources.append(QVariantMap{{"id", id}, {"kind", kind}, {"name", ""}, {"enabled", true}, {"target", "portal"}, {"fps", 60}, {"quality", "very_high"}});
        else sources.append(QVariantMap{{"id", id}, {"kind", kind}, {"name", ""}, {"enabled", true}, {"device", "overhead"}, {"mode", "720"}, {"encoding", "original"}});
        emit settingsChanged(); emit statusChanged(); return id;
    }
    Q_INVOKABLE bool removeSource(const QString &id) {
        for (int i = 0; i < sources.size(); ++i) if (sources[i].toMap()["id"] == id) { sources.removeAt(i); emit settingsChanged(); emit statusChanged(); return true; }
        return false;
    }
    Q_INVOKABLE bool setSourceSetting(const QString &id, const QString &key, const QVariant &value) {
        // A device the recorder turns down, so the list has to fall back to the stored one.
        if (value == "refused") return false;
        for (int i = 0; i < sources.size(); ++i) {
            auto source = sources[i].toMap();
            if (source["id"] != id) continue;
            source[key] = key == "name" ? QVariant(Sources::cleanName(value.toString())) : value;
            if (key == "device") source["mode"] = value == "overhead" ? "720" : "1080";
            sources[i] = source; emit settingsChanged(); emit statusChanged(); return true;
        }
        return false;
    }
    bool recording = false;
    QString warning, displayWarning, monitorLayout;
    QStringList chosen;
    int rescans = 0;
    QVariantList sources{
        QVariantMap{{"id", "screen"}, {"kind", "screen"}, {"name", ""}, {"enabled", true}, {"target", "portal"}, {"fps", 60}, {"quality", "very_high"}},
        QVariantMap{{"id", "camera"}, {"kind", "camera"}, {"name", ""}, {"enabled", true}, {"device", "demo"}, {"mode", "1080"}, {"encoding", "original"}}};
signals:
    void statusChanged();
    void settingsChanged();
    void screenChoiceRequested();
    void sourceFrame(const QString &source, int number);
};

class DemoPreview : public QQuickImageProvider {
public:
    DemoPreview() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        QImage image(640, 360, QImage::Format_RGB32);
        for (int y = 0; y < 360; ++y) for (int x = 0; x < 640; ++x) {
            const bool circle = (x - 320)*(x - 320)+(y - 180)*(y - 180) < 95*95;
            image.setPixelColor(x,y,id.startsWith("screen") ? ((x / 80 + y / 45) % 2 ? QColor("#343944") : QColor("#20242b")) : circle ? QColor("#ffd60a") : QColor(18+x/18, 24+y/15, 38+x/25));
        }
        *size = image.size(); return image;
    }
};

// The app's own preview provider lives in main.cpp; this serves the same images to the window under test.
class BackendPreview : public QQuickImageProvider {
public:
    explicit BackendPreview(Backend *backend) : QQuickImageProvider(Image), m_backend(backend) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        auto image = m_backend->sourceImage(id.section('/', 0, 0));
        if (image.isNull()) { image = QImage(64, 36, QImage::Format_RGB32); image.fill(Qt::black); }
        *size = image.size(); return image;
    }
private:
    Backend *m_backend;
};

class RecorderTests : public QObject {
    Q_OBJECT
private:
    QJsonObject settings() {
        return {{"camera", "/dev/video0"}, {"mode", "1920x1080@30/1:mjpeg"}, {"audio", "studio mic;$(false)"}, {"directory", "/tmp/my clips"}, {"encoding", "original"}};
    }
    QJsonObject camera() {
        return {{"id", "camera"}, {"kind", "camera"}, {"name", ""}, {"enabled", true}, {"device", "/dev/video0"}, {"mode", "1920x1080@30/1:mjpeg"}, {"encoding", "original"}};
    }
    void write(const QString &path, const QByteArray &contents) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(contents), contents.size());
    }
    // Items made by a Repeater are in the visual tree but not the object tree, so findChild misses them.
    static QQuickItem *visualChild(QQuickItem *root, const QString &name) {
        if (!root) return nullptr;
        if (root->objectName() == name) return root;
        for (const auto child : root->childItems()) if (const auto found = visualChild(child, name)) return found;
        return nullptr;
    }
    bool script(const QString &path, const QByteArray &contents) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.flush()
            && file.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    }
    // Stands in for GPU Screen Recorder. As a preview it writes an encoded stream to
    // stdout, after a pause and a restore token when asked for the picker. As a
    // recording it also saves a playable file and its first-frame timestamp.
    QString fakeScreenRecorder(const QTemporaryDir &dir) {
        const auto fixture = dir.filePath("fixture.mkv"), stream = dir.filePath("stream.ts");
        QProcess encoder;
        encoder.start("ffmpeg", {"-v","error","-f","lavfi","-i","testsrc2=size=64x64:rate=10","-t","0.5","-c:v","libx264",fixture});
        if (!encoder.waitForFinished(5000) || encoder.exitCode()) return {};
        encoder.start("ffmpeg", {"-v","error","-f","lavfi","-i","testsrc2=size=320x180:rate=15","-t","2","-c:v","libx264","-preset","ultrafast","-g","15","-f","mpegts",stream});
        if (!encoder.waitForFinished(5000) || encoder.exitCode()) return {};
        const auto program = dir.filePath("fake-gpu-screen-recorder");
        return script(program, QByteArray(R"(#!/usr/bin/python3
import sys,time,pathlib,shutil,signal
args=sys.argv[1:]; out=args[args.index('-o')+1]; target=args[args.index('-w')+1]
signal.signal(signal.SIGINT,lambda *a: sys.exit(0))
stream=open(')") + stream.toUtf8() + R"(','rb').read()
if out == '/dev/stdout':
    if target == 'portal' and not pathlib.Path(args[args.index('-portal-session-token-filepath')+1]).exists():
        time.sleep(.6)
        pathlib.Path(args[args.index('-portal-session-token-filepath')+1]).write_text('token')
    if target == 'BROKEN': sys.exit(3)
else:
    name=out.split(']')[1].split('|')[0]
    shutil.copyfile(')" + fixture.toUtf8() + R"(',name)
    pathlib.Path(out+'.ts').write_text(str(time.monotonic_ns()//1000)+' '+str(time.time_ns()//1000))
while True:
    sys.stdout.buffer.write(stream); sys.stdout.buffer.flush()
    time.sleep(1.5)
)") ? program : QString();
    }
private slots:
    void initTestCase() { QQuickStyle::setStyle("Material"); } // once, before any window loads
    void recordingTimecodesIncludeMillisecondsAndLongTakes() {
        QCOMPARE(SessionRecorder::recordingTimecode(689.123), "00:11:29.123");
        QCOMPARE(SessionRecorder::recordingTimecode(59.9999), "00:01:00.000");
        QCOMPARE(SessionRecorder::recordingTimecode(90061.007), "25:01:01.007");
        QCOMPARE(SessionRecorder::recordingTimecode(-1), "00:00:00.000");
    }
    void pipewireHealthTracksOnlyRecorderAndHandlesSplitUpdates() {
        CaptureHealth health; health.reset(1234); QSignalSpy failures(&health, &CaptureHealth::failed);
        const QByteArray graph = R"([{"id":10,"type":"PipeWire:Interface:Client","info":{"props":{"application.process.id":1234}}},{"id":20,"type":"PipeWire:Interface:Node","info":{"props":{"client.id":10},"state":"running"}},{"id":30,"type":"PipeWire:Interface:Link","info":{"input-node-id":20,"output-node-id":50,"state":"active"}},{"id":31,"type":"PipeWire:Interface:Link","info":{"input-node-id":99,"output-node-id":50,"state":"error"}}])";
        for (char c : graph) health.ingest(QByteArray(1, c));
        QVERIFY(health.linked()); QCOMPARE(failures.count(), 0);
        // Encoded pictures can remain identical indefinitely. Graph updates,
        // including unrelated failures, must not restart a healthy still desktop.
        health.ingest(R"([{"id":31,"info":{"state":"error"}}][{"id":20,"info":{"state":"running"}}])");
        QCOMPARE(failures.count(), 0);
        health.ingest(R"json([{"id":30,"info":{"state":"error","error":"alloc buffers: Invalid argument (-22)"}}])json");
        QCOMPARE(failures.count(), 1); QVERIFY(failures[0][0].toString().contains("buffers"));
        health.ingest(R"([{"id":30,"info":null}])"); QCOMPARE(failures.count(), 1);
        health.reset(1234); health.ingest(graph); QVERIFY(health.linked());
        health.ingest(R"([{"id":30,"info":null}])"); QCOMPARE(failures.count(), 2);
        health.reset(7777); health.ingest(graph); QVERIFY(!health.linked());
    }
    void omaControlWarningRecognizesLayerSurface() {
        QVERIFY(Backend::omaControlOpen(R"({"DP-3":{"levels":{"3":[{"namespace":"omacontrol"}]}}})"));
        QVERIFY(!Backend::omaControlOpen(R"({"DP-3":{"levels":{"3":[{"namespace":"omarchy-bar"}]}}})"));
        QVERIFY(!Backend::omaControlOpen("invalid"));
        QVERIFY(!Backend::omaControlOpen(QByteArray(1024 * 1024 + 1, 'x')));
    }
    void screenSegmentsShareTrackWithMeasuredGap() {
        QJsonObject manifest{{"sources", QJsonArray{
            QJsonObject{{"id","screen"},{"kind","Video"},{"file","screen.mkv"},{"state","saved"},{"firstMonotonicUs",10000000},{"durationSeconds",3}},
            QJsonObject{{"id","camera"},{"kind","Video"},{"file","camera.mkv"},{"state","saved"},{"firstMonotonicUs",10000000},{"durationSeconds",10}},
            QJsonObject{{"id","screen-002"},{"kind","Video"},{"file","screen-002.mkv"},{"state","saved"},{"firstMonotonicUs",15000000},{"durationSeconds",5}},
            QJsonObject{{"id","screen-003"},{"kind","Video"},{"file","screen-003.partial.mkv"},{"state","incomplete"}}
        }}};
        const auto tracks = SessionRecorder::timeline(manifest, "/tmp/take")["tracks"].toObject()["children"].toArray();
        QCOMPARE(tracks.size(), 2);
        const auto children = tracks[0].toObject()["children"].toArray(); QCOMPARE(children.size(), 3);
        QCOMPARE(children[1].toObject()["OTIO_SCHEMA"].toString(), "Gap.1");
        QCOMPARE(children[1].toObject()["source_range"].toObject()["duration"].toObject()["value"].toDouble(), 120.0);
        QCOMPARE(children[2].toObject()["name"].toString(), "screen-002");
    }
    void screenRecoveryPreservesCameraAndCanBeStopped_data() {
        QTest::addColumn<bool>("stopDuringRetry"); QTest::addColumn<bool>("linkFailure");
        QTest::newRow("resume") << false << false;
        QTest::newRow("user-stop") << true << false;
        QTest::newRow("live-process-link-failure") << false << true;
    }
    void screenRecoveryPreservesCameraAndCanBeStopped() {
        QFETCH(bool, stopDuringRetry); QFETCH(bool, linkFailure);
        QTemporaryDir dir;
        QProcess encoder; const auto fixture = dir.filePath("fixture.mkv");
        encoder.start("ffmpeg", {"-v","error","-f","lavfi","-i","testsrc2=size=64x64:rate=10","-t","0.5","-c:v","libx264",fixture});
        QVERIFY(encoder.waitForFinished(5000)); QCOMPARE(encoder.exitCode(), 0);
        const auto program = dir.filePath("fake-screen");
        write(program, QByteArray("#!/usr/bin/python3\nimport sys,time,pathlib,shutil,signal\nargs=sys.argv[1:]\nout=args[args.index('-o')+1]\nname=out.split(']')[1].split('|')[0]\nshutil.copyfile('") + fixture.toUtf8() + R"(',name)
pathlib.Path(out+'.ts').write_text(str(time.monotonic_ns()//1000)+' '+str(time.time_ns()//1000))
signal.signal(signal.SIGINT,lambda *a: sys.exit(0))
if name == 'screen.partial.mkv':
 time.sleep(1)
 sys.exit(1)
while True: time.sleep(.05)
)");
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const auto monitor = dir.filePath("fake-pw-dump");
        if (linkFailure) {
            QFile script(program); QVERIFY(script.open(QIODevice::ReadOnly)); auto code = script.readAll(); script.close();
            code.replace("import sys,time,pathlib,shutil,signal", "import sys,time,pathlib,shutil,signal,os");
            code.replace("if name == 'screen.partial.mkv':", "if False:");
            code.replace("while True:", QByteArray("pathlib.Path('") + dir.filePath("pid").toUtf8() + "').write_text(str(os.getpid())+' '+name)\nwhile True:");
            write(program, code);
            write(monitor, QByteArray("#!/usr/bin/python3\nimport json,time,pathlib,os\np=pathlib.Path('") + dir.filePath("pid").toUtf8() + R"(')
while True:
 try:
  pid,name=p.read_text().split()
  os.kill(int(pid),0)
  break
 except (FileNotFoundError,ProcessLookupError,ValueError): time.sleep(.02)
print(json.dumps([{'id':20,'type':'PipeWire:Interface:Node','info':{'props':{'application.process.id':int(pid)},'state':'running'}},{'id':30,'type':'PipeWire:Interface:Link','info':{'input-node-id':20,'output-node-id':50,'state':'active'}}]),flush=True)
if name == 'screen.partial.mkv':
 time.sleep(.6)
 print(json.dumps([{'id':30,'info':{'state':'error','error':'alloc buffers: Invalid argument (-22)'}}]),flush=True)
while True: time.sleep(.1)
)");
            QVERIFY(QFile::setPermissions(monitor,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        }
        SessionRecorder session(nullptr, program, "ffmpeg", linkFailure ? monitor : QString("pw-dump"));
        QSignalSpy starts(&session, &SessionRecorder::cameraStartRequested), stops(&session, &SessionRecorder::cameraStopRequested);
        connect(&session, &SessionRecorder::cameraStartRequested, this, [&](const QString &source, const QString &file) {
            QVERIFY(QFile::copy(fixture, file)); session.cameraTimestamp(source, QDateTime::currentMSecsSinceEpoch()*1000);
        });
        connect(&session, &SessionRecorder::cameraStopRequested, this, [&](const QString &source) { session.cameraFinished(source, 0, {}); });
        QJsonObject selected{{"directory",dir.path()},{"cameraEnabled",true},{"screenEnabled",true},
            {"microphoneEnabled",false},{"desktopEnabled",false},{"screenTarget",linkFailure ? "portal" : "DP-3"}};
        QVERIFY(session.start(selected));
        QTRY_COMPARE_WITH_TIMEOUT(session.status()["screenRecoveryAttempts"].toInt(),1,5000);
        QCOMPARE(starts.count(), 1); QCOMPARE(stops.count(), 0);
        QVERIFY(session.status()["warning"].toString().contains("Reconnecting"));
        const auto event = session.status()["session"].toMap()["screenInterruptions"].toList().first().toMap();
        const auto timecode = event["recordingTimecode"].toString();
        QVERIFY(!timecode.isEmpty()); QVERIFY(event["recordingOffsetSeconds"].toDouble() > 0);
        QVERIFY(QDateTime::fromString(event["detectedAtUtc"].toString(), Qt::ISODateWithMs).isValid());
        QVERIFY(session.status()["warning"].toString().contains(timecode));
        if (!stopDuringRetry) {
            QTRY_COMPARE_WITH_TIMEOUT(session.status()["screenHealth"].toString(),"live",5000);
            QCOMPARE(session.status()["sources"].toList().size(), 3);
        }
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),5000);
        QCOMPARE(starts.count(), 1); QCOMPARE(stops.count(), 1);
        QCOMPARE(session.status()["state"].toString(), "saved");
        QVERIFY(QFileInfo::exists(session.status()["output"].toString()+"/screen.mkv"));
        QVERIFY(session.status()["session"].toMap()["hasScreenGaps"].toBool());
        QVERIFY(session.status()["warning"].toString().contains(timecode));
        if (!stopDuringRetry) QVERIFY(QFileInfo::exists(session.status()["output"].toString()+"/screen-002.mkv"));
        const auto count = session.status()["sources"].toList().size(); QTest::qWait(1200);
        QCOMPARE(session.status()["sources"].toList().size(), count); // no delayed restart after Stop
    }
    void bufferedFileWithLiveEncodedOutputIsNotAStall() {
        QTemporaryDir dir; const auto program = dir.filePath("buffered-screen");
        write(program, R"(#!/usr/bin/python3
import sys,time,pathlib,signal
args=sys.argv[1:]; out=args[args.index('-o')+1]
pathlib.Path(out+'.ts').write_text(str(time.monotonic_ns()//1000)+' '+str(time.time_ns()//1000))
signal.signal(signal.SIGINT,lambda *a:sys.exit(0))
while True:
 sys.stdout.buffer.write(b'encoded-packet')
 sys.stdout.buffer.flush()
 time.sleep(.1)
)");
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        SessionRecorder session(nullptr,program);
        QVERIFY(session.start({{"directory",dir.path()},{"cameraEnabled",false},{"screenEnabled",true},
            {"microphoneEnabled",false},{"desktopEnabled",false},{"screenTarget","DP-3"}}));
        QTRY_COMPARE(session.status()["state"].toString(),"recording");
        QTest::qWait(11000);
        QVERIFY(session.busy()); QCOMPARE(session.status()["screenRecoveryAttempts"].toInt(),0);
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),5000);
    }
    void recoveryRetriesAreBounded() {
        QTemporaryDir dir; const auto program = dir.filePath("fake-screen");
        write(program, R"(#!/usr/bin/python3
import sys,time,pathlib
args=sys.argv[1:]; out=args[args.index('-o')+1]
pathlib.Path(out+'.ts').write_text(str(time.monotonic_ns()//1000)+' '+str(time.time_ns()//1000))
time.sleep(.3)
sys.exit(1)
)");
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        SessionRecorder session(nullptr,program);
        QVERIFY(session.start({{"directory",dir.path()},{"cameraEnabled",false},{"screenEnabled",true},
            {"microphoneEnabled",false},{"desktopEnabled",false},{"screenTarget","DP-3"}}));
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(),10000);
        QCOMPARE(session.status()["screenRecoveryAttempts"].toInt(),3);
        QCOMPARE(session.status()["screenHealth"].toString(),"failed");
        QVERIFY(session.status()["error"].toString().contains("three retries"));
        QCOMPARE(session.status()["session"].toMap()["screenInterruptions"].toList().size(),4);
    }
    void videoSourcesMigrateAndNameTheirFiles() {
        const QJsonObject old{{"camera","/dev/video0"},{"mode","1920x1080@30/1:mjpeg"},{"encoding","h264"},{"cameraEnabled",false},
            {"screenEnabled",true},{"screenTarget","DP-3"},{"screenFps",30},{"screenQuality","ultra"},{"audio","mic"},{"directory","/tmp"}};
        const auto migrated = Sources::migrated(old);
        QVERIFY(!migrated.contains("camera")); QVERIFY(!migrated.contains("screenTarget")); QCOMPARE(migrated["audio"].toString(), "mic");
        const auto list = Sources::list(migrated); QCOMPARE(list.size(), 2);
        const auto screen = Sources::find(list, "screen"), camera = Sources::find(list, "camera");
        QCOMPARE(screen["target"].toString(), "DP-3"); QCOMPARE(screen["fps"].toInt(), 30); QCOMPARE(screen["quality"].toString(), "ultra");
        QVERIFY(screen["enabled"].toBool()); QVERIFY(!camera["enabled"].toBool());
        QCOMPARE(camera["device"].toString(), "/dev/video0"); QCOMPARE(camera["encoding"].toString(), "h264");
        QCOMPARE(Sources::migrated(migrated), migrated);
        QCOMPARE(Sources::enabled(migrated).size(), 1); QCOMPARE(Sources::enabled(migrated, "camera").size(), 0);
        QCOMPARE(Sources::newId(list, "screen"), "screen-2"); QCOMPARE(Sources::newId(list, "camera"), "camera-2");
        QCOMPARE(Sources::label(QJsonObject{{"id","camera-3"},{"kind","camera"},{"name",""}}), "Camera 3");
        QCOMPARE(Sources::label(QJsonObject{{"id","screen"},{"kind","screen"},{"name","Left"}}), "Left");
        QCOMPARE(Sources::cleanName("  ../Left\tmonitor\n "), ".. Left monitor");
        QCOMPARE(Sources::cleanName(QString(60, 'x')).size(), 40);
        QCOMPARE(Sources::uniqueName(list, "camera"), "camera 2");
        QCOMPARE(Sources::uniqueName(list, "camera", "camera"), "camera");
        // Names become file names: unique, safe, and clear of audio files and reconnected segments.
        auto named = [](const QString &id, const QString &name) { return QJsonObject{{"id",id},{"kind",id.startsWith("screen") ? "screen" : "camera"},{"name",name}}; };
        const auto stems = Sources::fileStems({named("screen","Left monitor"), named("screen-2","Left Monitor!"), named("camera",""),
            named("camera-2","Camera 002"), named("screen-3","Microphone"), named("screen-4","../.."), named("camera-3","Skærm 2")});
        QCOMPARE(stems, (QStringList{"left-monitor","left-monitor-2","camera","camera-002-2","microphone-2","screen-4","skærm-2"}));
        // A hand-edited list is repaired rather than trusted.
        const auto repaired = Sources::sanitized({QJsonObject{{"id","screen"},{"kind","screen"}},
            QJsonObject{{"id","screen"},{"kind","screen"},{"fps",999},{"quality","custom;unsafe"}},
            QJsonObject{{"id","../x"},{"kind","camera"},{"name","a/b"}}, QJsonObject{{"id","mic"},{"kind","microphone"}}});
        QCOMPARE(repaired.size(), 3);
        QCOMPARE(repaired[1].toObject()["id"].toString(), "screen-2"); QCOMPARE(repaired[1].toObject()["fps"].toInt(), 120);
        QCOMPARE(repaired[1].toObject()["quality"].toString(), "very_high"); QCOMPARE(repaired[1].toObject()["target"].toString(), "portal");
        QCOMPARE(repaired[2].toObject()["id"].toString(), "camera"); QCOMPARE(repaired[2].toObject()["name"].toString(), "a b");
        QJsonArray many; for (int i = 0; i < 20; ++i) many.append(QJsonObject{{"id",QString("camera-%1").arg(i + 2)},{"kind","camera"}});
        QCOMPARE(Sources::sanitized(many).size(), Sources::maximum);
    }
    void severalScreensAndCamerasRecordAsSeparateNamedFiles() {
        QTemporaryDir dir;
        QProcess encoder; const auto fixture = dir.filePath("fixture.mkv");
        encoder.start("ffmpeg", {"-v","error","-f","lavfi","-i","testsrc2=size=64x64:rate=10","-t","0.5","-c:v","libx264",fixture});
        QVERIFY(encoder.waitForFinished(5000)); QCOMPARE(encoder.exitCode(), 0);
        // Each fake capture records its arguments; only "Left monitor" drops out, once.
        const auto program = dir.filePath("fake-screen");
        write(program, QByteArray("#!/usr/bin/python3\nimport sys,time,pathlib,shutil,signal\nargs=sys.argv[1:]\nout=args[args.index('-o')+1]\nname=out.split(']')[1].split('|')[0]\nshutil.copyfile('") + fixture.toUtf8() + R"(',name)
pathlib.Path(name+'.args').write_text('\n'.join(args))
pathlib.Path(out+'.ts').write_text(str(time.monotonic_ns()//1000)+' '+str(time.time_ns()//1000))
signal.signal(signal.SIGINT,lambda *a: sys.exit(0))
if name == 'left-monitor.partial.mkv':
 time.sleep(1)
 sys.exit(1)
while True: time.sleep(.05)
)");
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        SessionRecorder session(nullptr, program);
        QStringList cameraFiles, screenData;
        connect(&session, &SessionRecorder::cameraStartRequested, this, [&](const QString &source, const QString &file) {
            cameraFiles << source + "=" + QFileInfo(file).fileName();
            QVERIFY(QFile::copy(fixture, file)); session.cameraTimestamp(source, QDateTime::currentMSecsSinceEpoch()*1000);
        });
        connect(&session, &SessionRecorder::cameraStopRequested, this, [&](const QString &source) { session.cameraFinished(source, 0, {}); });
        connect(&session, &SessionRecorder::screenStarting, this, [&](const QString &source) { screenData << source; });
        const QJsonObject settings{{"directory",dir.path()},{"microphoneEnabled",false},{"desktopEnabled",false},{"videoSources",QJsonArray{
            QJsonObject{{"id","screen"},{"kind","screen"},{"name",""},{"enabled",true},{"target","DP-3"},{"fps",30},{"quality","high"}},
            QJsonObject{{"id","screen-2"},{"kind","screen"},{"name","Left monitor"},{"enabled",true},{"target","HDMI-A-1"},{"fps",60},{"quality","very_high"}},
            QJsonObject{{"id","camera"},{"kind","camera"},{"name",""},{"enabled",true}},
            QJsonObject{{"id","camera-2"},{"kind","camera"},{"name","Overhead"},{"enabled",true}},
            QJsonObject{{"id","camera-3"},{"kind","camera"},{"name","Unused"},{"enabled",false}}}}};
        QVERIFY(session.start(settings));
        QTRY_COMPARE_WITH_TIMEOUT(session.status()["screenRecoveryAttempts"].toInt(), 1, 5000);
        QCOMPARE(cameraFiles, (QStringList{"camera=camera.partial.mkv", "camera-2=overhead.partial.mkv"}));
        QVERIFY(session.status()["warning"].toString().startsWith("Left monitor capture interrupted"));
        QTRY_COMPARE_WITH_TIMEOUT(session.status()["screenHealth"].toString(), "live", 5000);
        QCOMPARE(session.status()["state"].toString(), "recording"); QVERIFY(!session.status()["screenReconnecting"].toBool());
        QVERIFY(session.status()["warning"].toString().startsWith("Left monitor capture resumed"));
        // Only the interrupted screen was restarted; the other kept its first capture.
        QCOMPARE(screenData, (QStringList{"screen", "screen-2", "screen-2"}));
        QHash<QString, int> attempts;
        for (const auto &value : session.status()["screens"].toList()) attempts[value.toMap()["source"].toString()] = value.toMap()["recoveryAttempts"].toInt();
        QCOMPARE(attempts.value("screen"), 0); QCOMPARE(attempts.value("screen-2"), 1);
        const auto take = session.status()["output"].toString();
        auto arguments = [&](const QString &name) { QFile file(take + "/" + name + ".args"); return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split('\n') : QStringList(); };
        const auto first = arguments("screen.partial.mkv"), second = arguments("left-monitor.partial.mkv");
        QCOMPARE(first[first.indexOf("-w") + 1], "DP-3"); QCOMPARE(first[first.indexOf("-f") + 1], "30"); QCOMPARE(first[first.indexOf("-q") + 1], "high");
        QCOMPARE(second[second.indexOf("-w") + 1], "HDMI-A-1"); QCOMPARE(second[second.indexOf("-f") + 1], "60");
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 5000);
        QCOMPARE(session.status()["state"].toString(), "saved");
        for (const QString &file : {QString("camera.mkv"), QString("overhead.mkv"), QString("screen.mkv"), QString("left-monitor.mkv"), QString("left-monitor-002.mkv")})
            QVERIFY2(QFileInfo::exists(take + "/" + file), qPrintable(file));
        QVERIFY(!QFileInfo::exists(take + "/unused.mkv"));
        // With two screens the saved warning says which one each time belongs to.
        QVERIFY(session.status()["warning"].toString().contains("at Left monitor 00:"));
        QFile otio(take + "/timeline.otio"); QVERIFY(otio.open(QIODevice::ReadOnly));
        const auto tracks = QJsonDocument::fromJson(otio.readAll()).object()["tracks"].toObject()["children"].toArray();
        QStringList names; for (const auto &track : tracks) names << track.toObject()["name"].toString();
        QCOMPARE(names, (QStringList{"Camera", "Overhead", "Screen", "Left monitor"}));
        const auto left = tracks[3].toObject()["children"].toArray();
        QCOMPARE(left.last().toObject()["name"].toString(), "left-monitor-002");
        QCOMPARE(tracks[2].toObject()["children"].toArray().last().toObject()["name"].toString(), "screen");
    }
    void portalScreensAskForPermissionOneAtATime() {
        QTemporaryDir dir;
        QProcess encoder; const auto fixture = dir.filePath("fixture.mkv");
        encoder.start("ffmpeg", {"-v","error","-f","lavfi","-i","testsrc2=size=64x64:rate=10","-t","0.5","-c:v","libx264",fixture});
        QVERIFY(encoder.waitForFinished(5000)); QCOMPARE(encoder.exitCode(), 0);
        // Stands in for a capture that waits on the picker before its first frame.
        const auto program = dir.filePath("fake-portal-screen");
        write(program, QByteArray("#!/usr/bin/python3\nimport sys,time,pathlib,shutil,signal\nargs=sys.argv[1:]\nout=args[args.index('-o')+1]\nname=out.split(']')[1].split('|')[0]\n") + R"(pathlib.Path(name+'.args').write_text('\n'.join(args))
pathlib.Path(name+'.launched').write_text(str(time.monotonic_ns()//1000))
signal.signal(signal.SIGINT,lambda *a: sys.exit(0))
time.sleep(.7)
shutil.copyfile(')" + fixture.toUtf8() + R"(',name)
pathlib.Path(out+'.ts').write_text(str(time.monotonic_ns()//1000)+' '+str(time.time_ns()//1000))
while True: time.sleep(.05)
)");
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const auto monitor = dir.filePath("fake-pw-dump");
        write(monitor, "#!/bin/sh\nexec sleep 60\n");
        QVERIFY(QFile::setPermissions(monitor,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        SessionRecorder session(nullptr, program, "ffmpeg", monitor);
        const QJsonObject settings{{"directory",dir.path()},{"microphoneEnabled",false},{"desktopEnabled",false},{"videoSources",QJsonArray{
            QJsonObject{{"id","screen"},{"kind","screen"},{"name",""},{"enabled",true},{"target","portal"},{"fps",60},{"quality","very_high"}},
            QJsonObject{{"id","screen-2"},{"kind","screen"},{"name",""},{"enabled",true},{"target","portal"},{"fps",60},{"quality","very_high"}}}}};
        QVERIFY(session.start(settings, {{"screen", dir.filePath("token-a")}, {"screen-2", dir.filePath("token-b")}}));
        QCOMPARE(session.status()["state"].toString(), "waiting-screen");
        const auto take = session.status()["output"].toString();
        auto text = [&](const QString &name) { QFile file(take + "/" + name); return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString(); };
        QTRY_VERIFY_WITH_TIMEOUT(!text("screen.partial.mkv.launched").isEmpty(), 3000);
        QVERIFY(text("screen-2.partial.mkv.launched").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(session.status()["state"].toString(), "recording", 8000);
        qint64 firstFrame = 0;
        for (const auto &value : session.status()["sources"].toList()) if (value.toMap()["id"] == "screen") firstFrame = value.toMap()["firstMonotonicUs"].toLongLong();
        QVERIFY(firstFrame > 0); QVERIFY(text("screen-2.partial.mkv.launched").toLongLong() >= firstFrame);
        QVERIFY(text("screen.partial.mkv.args").split('\n').contains(dir.filePath("token-a")));
        QVERIFY(text("screen-2.partial.mkv.args").split('\n').contains(dir.filePath("token-b")));
        session.stop(); QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 5000);
        QCOMPARE(session.status()["state"].toString(), "saved");
        QVERIFY(QFileInfo::exists(take + "/screen.mkv")); QVERIFY(QFileInfo::exists(take + "/screen-2.mkv"));
    }
    void cameraFeedSharesOneCaptureBetweenPreviewAndRecording() {
        QTemporaryDir dir;
        QImage picture(64, 36, QImage::Format_RGB32); picture.fill(QColor("#e68e0d"));
        QVERIFY(picture.save(dir.filePath("frame.jpg"), "JPEG"));
        // Stands in for FFmpeg: JPEG frames on stdout, a device timestamp when recording, "q" to quit.
        const auto program = dir.filePath("fake-ffmpeg");
        QVERIFY(script(program, QByteArray(R"(#!/usr/bin/python3
import sys,time,threading
args=sys.argv[1:]; recording='matroska' in args
jpeg=open(')") + dir.filePath("frame.jpg").toUtf8() + R"(','rb').read()
if '/dev/broken' in args: sys.exit(4)
if recording: open(args[args.index('matroska')+1],'wb').write(b'video')
def frames():
    first=True
    while True:
        sys.stdout.buffer.write(jpeg); sys.stdout.buffer.flush()
        if recording and first: sys.stderr.write('demuxer -> ist_index:0:0 type:video pkt_pts_time:%f\n' % time.time()); sys.stderr.flush()
        first=False; time.sleep(.05)
threading.Thread(target=frames,daemon=True).start()
for line in sys.stdin:
    if line.strip() == 'q': sys.exit(0)
)"));
        CameraFeed feed(nullptr, program);
        QSignalSpy stamps(&feed, &CameraFeed::timestamp), ended(&feed, &CameraFeed::finished);
        QCOMPARE(feed.state(), "inactive");
        feed.preview(camera());
        QTRY_COMPARE_WITH_TIMEOUT(feed.state(), "live", 5000);
        QVERIFY(feed.frameNumber() > 0); QCOMPARE(feed.image().width(), 64); QVERIFY(!feed.recording());
        feed.stop();
        QCOMPARE(feed.state(), "inactive"); QCOMPARE(ended.count(), 1); QVERIFY(!ended[0][1].toBool()); QVERIFY(!ended[0][3].toBool());
        QCOMPARE(stamps.count(), 0);
        feed.record(camera(), dir.filePath("take.mkv"));
        QVERIFY(feed.recording()); QCOMPARE(feed.frameNumber(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(stamps.count() > 0, 5000);
        QVERIFY(stamps[0][0].toLongLong() > 1000000000000000LL); QVERIFY(QFileInfo::exists(dir.filePath("take.mkv")));
        feed.requestStop();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 2, 5000);
        QCOMPARE(ended[1][0].toInt(), 0); QVERIFY(!ended[1][1].toBool()); QVERIFY(ended[1][3].toBool()); QVERIFY(!feed.recording());
        // A capture that ends by itself is reported as a failure, with what it was doing.
        auto broken = camera(); broken["device"] = "/dev/broken";
        feed.preview(broken);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 3, 5000);
        QCOMPARE(ended[2][0].toInt(), 4); QVERIFY(ended[2][1].toBool()); QVERIFY(!ended[2][3].toBool());
    }
    void cameraSourcesAreCheckedOneByOne() {
        QString error; auto source = camera();
        QVERIFY(CameraFeed::valid(source, &error));
        source["name"] = "Overhead"; source["device"] = "video0";
        QVERIFY(!CameraFeed::valid(source, &error)); QVERIFY(error.startsWith("Overhead:"));
        source = camera(); source["mode"] = "1080p; rm anything"; QVERIFY(!CameraFeed::valid(source, &error));
        source = camera(); source["encoding"] = "raw"; QVERIFY(!CameraFeed::valid(source, &error));
        // Two enabled sources cannot share one device; a disabled one does not count.
        auto second = camera(); second["id"] = "camera-2"; second["name"] = "Overhead";
        QJsonObject shared{{"audio","mic"},{"directory","/tmp"},{"videoSources",QJsonArray{camera(), second}}};
        QVERIFY(!Backend::validSettings(shared, &error)); QVERIFY(error.startsWith("Overhead uses a camera"));
        second["enabled"] = false; shared["videoSources"] = QJsonArray{camera(), second};
        QVERIFY(Backend::validSettings(shared, &error));
        second["enabled"] = true; second["device"] = "/dev/video1"; shared["videoSources"] = QJsonArray{camera(), second};
        QVERIFY(Backend::validSettings(shared, &error));
    }
    void screenFeedPreviewsAMonitorOrAnApprovedSelection() {
        QTemporaryDir dir; const auto program = fakeScreenRecorder(dir); QVERIFY(!program.isEmpty());
        const auto token = dir.filePath("restore-token");
        const auto portal = ScreenFeed::arguments("portal", token), direct = ScreenFeed::arguments("DP-3", token);
        QCOMPARE(portal[portal.indexOf("-portal-session-token-filepath") + 1], token);
        QCOMPARE(direct[direct.indexOf("-w") + 1], "DP-3"); QVERIFY(!direct.contains(token)); QCOMPARE(direct.last(), "/dev/stdout");
        ScreenFeed feed(token, nullptr, program);
        // Nothing has been approved, so restoring must not open the picker by itself.
        feed.start("portal", false); QVERIFY(!feed.running()); QVERIFY(!feed.selected());
        // A monitor needs no approval and previews straight away.
        feed.start("DP-3", false); QVERIFY(feed.running()); QVERIFY(!feed.selecting());
        QTRY_VERIFY_WITH_TIMEOUT(feed.monitor().frameNumber() > 0, 8000);
        QCOMPARE(feed.state(), "live"); QVERIFY(feed.selected());
        // Choosing opens the picker; once approved, the same selection restores without it.
        feed.start("portal", true); QVERIFY(feed.selecting()); QVERIFY(!feed.selected());
        QTRY_VERIFY_WITH_TIMEOUT(!feed.selecting(), 5000);
        QVERIFY(feed.selected()); QVERIFY(QFileInfo::exists(token));
        feed.stop(); QVERIFY(!feed.running()); QVERIFY(feed.selected());
        feed.start("portal", false); QVERIFY(feed.running());
        QTRY_VERIFY_WITH_TIMEOUT(feed.monitor().frameNumber() > 0, 8000);
        feed.forget(); QVERIFY(!feed.running()); QVERIFY(!feed.selected()); QVERIFY(!QFileInfo::exists(token));
        // A capture that exits by itself leaves an error instead of a blank pane.
        feed.start("BROKEN", false);
        QTRY_COMPARE_WITH_TIMEOUT(feed.state(), "error", 5000); QVERIFY(feed.error().contains("stopped"));
    }
    void settingsBecomeASourceListAndEarlierKeysStillWork() {
        QTemporaryDir dir; const auto path = dir.filePath("settings.json");
        write(path, R"({"camera":"/dev/video7","mode":"1280x720@30/1:mjpeg","encoding":"h264","cameraEnabled":true,"screenEnabled":true,
            "screenTarget":"DP-3","screenFps":30,"screenQuality":"ultra","audio":"mic","microphoneEnabled":false,"directory":"/tmp/takes"})");
        auto sourceOf = [](const Backend &backend, const QString &id) {
            for (const auto &value : backend.settings()["videoSources"].toList()) if (value.toMap()["id"] == id) return value.toMap();
            return QVariantMap();
        };
        {
            Backend backend(path);
            QCOMPARE(backend.settings()["videoSources"].toList().size(), 2);
            QCOMPARE(sourceOf(backend, "screen")["target"].toString(), "DP-3"); QCOMPARE(sourceOf(backend, "screen")["fps"].toInt(), 30);
            QCOMPARE(sourceOf(backend, "screen")["quality"].toString(), "ultra"); QVERIFY(sourceOf(backend, "screen")["enabled"].toBool());
            QCOMPARE(sourceOf(backend, "camera")["device"].toString(), "/dev/video7"); QCOMPARE(sourceOf(backend, "camera")["encoding"].toString(), "h264");
            // Scripts that read the earlier keys still find the first screen and camera.
            QCOMPARE(backend.settings()["screenTarget"].toString(), "DP-3"); QCOMPARE(backend.settings()["camera"].toString(), "/dev/video7");
            QCOMPARE(backend.settings()["mode"].toString(), "1280x720@30/1:mjpeg"); QVERIFY(backend.settings()["screenEnabled"].toBool());
            // Opening the app does not rewrite the file; the first change does.
            QFile untouched(path); QVERIFY(untouched.open(QIODevice::ReadOnly)); QVERIFY(untouched.readAll().contains("screenTarget")); untouched.close();

            const auto second = backend.addSource("screen"); QCOMPARE(second, "screen-2");
            QVERIFY(sourceOf(backend, second)["enabled"].toBool()); QCOMPARE(sourceOf(backend, second)["target"].toString(), "portal");
            QVERIFY(backend.setSourceSetting(second, "name", "  Left/monitor\n")); QCOMPARE(sourceOf(backend, second)["name"].toString(), "Left monitor");
            QVERIFY(backend.setSourceSetting(second, "target", "HDMI-A-1"));
            QVERIFY(backend.setSourceSetting(second, "fps", 500)); QCOMPARE(sourceOf(backend, second)["fps"].toInt(), 120);
            QVERIFY(!backend.setSourceSetting(second, "quality", "custom;unsafe"));
            QVERIFY(!backend.setSourceSetting(second, "target", ""));
            QVERIFY(!backend.setSourceSetting(second, "device", "/dev/video0"));
            QVERIFY(!backend.setSourceSetting("missing", "enabled", true));
            QVERIFY(!backend.setSourceSetting("camera", "encoding", "raw"));
            QCOMPARE(backend.addSource("microphone"), QString());
            const auto third = backend.addSource("screen");
            QVERIFY(backend.setSourceSetting(third, "name", "left monitor")); QCOMPARE(sourceOf(backend, third)["name"].toString(), "left monitor 2");
            QVERIFY(backend.setSourceSetting(third, "name", "")); QCOMPARE(sourceOf(backend, third)["name"].toString(), "");

            // The earlier single-source keys act on the first of each kind.
            QVERIFY(backend.setSetting("screenFps", 60));
            QCOMPARE(sourceOf(backend, "screen")["fps"].toInt(), 60); QCOMPARE(sourceOf(backend, second)["fps"].toInt(), 120);
            QVERIFY(backend.setSetting("screenEnabled", false));
            for (const QString &id : {QString("screen"), second, third}) QVERIFY(!sourceOf(backend, id)["enabled"].toBool());
            QVERIFY(!backend.status()["screenEnabled"].toBool());
            QVERIFY(backend.setSetting("screenEnabled", true));
            QVERIFY(sourceOf(backend, "screen")["enabled"].toBool()); QVERIFY(!sourceOf(backend, second)["enabled"].toBool());
            QVERIFY(backend.setSetting("encoding", "original")); QCOMPARE(sourceOf(backend, "camera")["encoding"].toString(), "original");

            // What each source is doing is reported in list order, under its shown name.
            const auto states = backend.status()["videoSources"].toList(); QCOMPARE(states.size(), 4);
            QStringList names; for (const auto &state : states) names << state.toMap()["name"].toString();
            QCOMPARE(names, (QStringList{"Screen", "Camera", "Left monitor", "Screen 3"}));
            QCOMPARE(states[0].toMap()["previewState"].toString(), "inactive"); QVERIFY(!states[0].toMap()["recording"].toBool());
            QVERIFY(states[2].toMap()["selected"].toBool()); QVERIFY(!states[3].toMap()["selected"].toBool());
            QVERIFY(!states[2].toMap()["picker"].toBool()); QVERIFY(states[3].toMap()["picker"].toBool());

            // The command line reaches the same operations.
            auto reply = backend.handle({{"command","source-add"},{"kind","screen"},{"settings",QJsonObject{{"name","Right"},{"target","DP-3"},{"enabled",false}}}});
            QVERIFY(reply["ok"].toBool()); const auto added = reply["id"].toString(); QCOMPARE(added, "screen-4");
            QCOMPARE(sourceOf(backend, added)["name"].toString(), "Right"); QVERIFY(!sourceOf(backend, added)["enabled"].toBool());
            QVERIFY(backend.handle({{"command","source-set"},{"id",added},{"settings",QJsonObject{{"quality","high"}}}})["ok"].toBool());
            QCOMPARE(sourceOf(backend, added)["quality"].toString(), "high");
            QVERIFY(!backend.handle({{"command","source-set"},{"id",added},{"settings",QJsonObject{{"colour","red"}}}})["ok"].toBool());
            QVERIFY(!backend.handle({{"command","source-set"},{"id",added}})["ok"].toBool());
            QVERIFY(backend.handle({{"command","source-remove"},{"id",added}})["ok"].toBool());
            QVERIFY(!backend.handle({{"command","source-remove"},{"id",added}})["ok"].toBool());
            QVERIFY(backend.removeSource(third)); QVERIFY(sourceOf(backend, third).isEmpty());

            // A camera source is only added for a camera that is connected and free. However many
            // this machine has, adding stops with an explanation rather than an empty source.
            QVERIFY(backend.status()["error"].toString().isEmpty());
            for (int i = 0; i < Sources::maximum && !backend.addSource("camera").isEmpty(); ++i) {}
            if (backend.settings()["videoSources"].toList().size() < Sources::maximum) QVERIFY(backend.status()["error"].toString().contains("amera"));
            for (const auto &value : backend.settings()["videoSources"].toList()) if (value.toMap()["kind"] == "camera") QVERIFY(!value.toMap()["device"].toString().isEmpty());
            // The command line may name the device outright, and a first device switches a source on.
            if (backend.settings()["videoSources"].toList().size() < Sources::maximum) {
                reply = backend.handle({{"command","source-add"},{"kind","camera"},{"settings",QJsonObject{{"device","/dev/video9"},{"name","Overhead"}}}});
                QVERIFY(reply["ok"].toBool()); QCOMPARE(sourceOf(backend, reply["id"].toString())["device"].toString(), "/dev/video9");
                QVERIFY(sourceOf(backend, reply["id"].toString())["enabled"].toBool());
                QVERIFY(backend.removeSource(reply["id"].toString()));
            }
            while (!backend.addSource("screen").isEmpty()) {}
            QCOMPARE(backend.settings()["videoSources"].toList().size(), Sources::maximum);
        }
        QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto stored = QJsonDocument::fromJson(saved.readAll()).object();
        QVERIFY(!stored.contains("screenTarget")); QVERIFY(!stored.contains("camera")); QCOMPARE(stored["videoSources"].toArray().size(), Sources::maximum);
        Backend reloaded(path);
        QCOMPARE(reloaded.settings()["videoSources"].toList().size(), Sources::maximum);
        QCOMPARE(sourceOf(reloaded, "screen-2")["name"].toString(), "Left monitor"); QCOMPARE(sourceOf(reloaded, "screen-2")["target"].toString(), "HDMI-A-1");
    }
    void twoScreensPreviewAndRecordThroughTheBackend() {
        QTemporaryDir dir; const auto program = fakeScreenRecorder(dir); QVERIFY(!program.isEmpty());
        const auto path = dir.filePath("settings.json");
        write(path, QJsonDocument(QJsonObject{{"directory",dir.filePath("takes")},{"audio","none"},{"microphoneEnabled",false},{"desktopEnabled",false},{"videoSources",QJsonArray{
            QJsonObject{{"id","screen"},{"kind","screen"},{"name",""},{"enabled",true},{"target","DP-3"},{"fps",60},{"quality","very_high"}},
            QJsonObject{{"id","screen-2"},{"kind","screen"},{"name","Left monitor"},{"enabled",true},{"target","HDMI-A-1"},{"fps",60},{"quality","very_high"}},
            QJsonObject{{"id","camera"},{"kind","camera"},{"name",""},{"enabled",false},{"device",""},{"mode",""},{"encoding","original"}}}}}).toJson());
        QHash<QString, int> frames; // outlives the backend, which still reports frames while shutting down
        Backend backend(path, nullptr, program);
        auto state = [&](const QString &id) {
            for (const auto &value : backend.status()["videoSources"].toList()) if (value.toMap()["id"] == id) return value.toMap();
            return QVariantMap();
        };
        connect(&backend, &Backend::sourceFrame, this, [&](const QString &id, int number) { frames[id] = qMax(frames[id], number); });
        // Opening the window previews both monitors without asking for anything.
        backend.setWindowVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(backend.frame("screen") > 0 && backend.frame("screen-2") > 0, 10000);
        QCOMPARE(state("screen")["previewState"].toString(), "live"); QCOMPARE(state("screen-2")["previewState"].toString(), "live");
        QVERIFY(!state("screen")["selecting"].toBool()); QVERIFY(!backend.status()["screenSelecting"].toBool());
        QCOMPARE(backend.status()["state"].toString(), "preview");
        QVERIFY(!backend.sourceImage("screen-2").isNull()); QVERIFY(backend.sourceJpeg("screen").startsWith(QByteArray::fromHex("ffd8")));
        QVERIFY(frames.value("screen") > 0); QVERIFY(frames.value("screen-2") > 0);

        QVERIFY(backend.start());
        // Sources are locked while a take is running.
        QVERIFY(!backend.setSourceSetting("screen-2", "name", "Other")); QCOMPARE(backend.addSource("screen"), QString()); QVERIFY(!backend.removeSource("screen"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.status()["state"].toString(), "recording", 10000);
        QTRY_VERIFY_WITH_TIMEOUT(state("screen")["recording"].toBool() && state("screen-2")["recording"].toBool(), 5000);
        QCOMPARE(state("screen-2")["health"].toString(), "live"); QVERIFY(!state("camera")["recording"].toBool());
        // Each preview now shows the stream its own recorder is saving.
        QTRY_VERIFY_WITH_TIMEOUT(backend.frame("screen") > 0 && backend.frame("screen-2") > 0, 10000);
        backend.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.status()["recording"].toBool(), 10000);
        QCOMPARE(backend.status()["state"].toString(), "saved");
        const auto take = backend.status()["output"].toString();
        QVERIFY(QFileInfo::exists(take + "/screen.mkv")); QVERIFY(QFileInfo::exists(take + "/left-monitor.mkv"));
        QVERIFY(!state("screen")["recording"].toBool()); QCOMPARE(state("screen")["health"].toString(), "inactive");
        // Afterwards the previews come back by themselves.
        QTRY_VERIFY_WITH_TIMEOUT(state("screen")["previewState"].toString() == "live" && state("screen-2")["previewState"].toString() == "live", 10000);
        // A camera that is not set up yet is reported, without holding back the other previews;
        // switching it off again clears the report.
        const auto unset = backend.handle({{"command","source-add"},{"kind","camera"},{"settings",QJsonObject{{"device","not-a-device"}}}})["id"].toString();
        QVERIFY(!unset.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(backend.status()["error"].toString().contains("select a camera"), 3000);
        QCOMPARE(state("screen")["previewState"].toString(), "live");
        QVERIFY(backend.setSourceSetting(unset, "enabled", false));
        QTRY_VERIFY_WITH_TIMEOUT(backend.status()["error"].toString().isEmpty(), 3000);
        QVERIFY(backend.removeSource(unset));
        // Turning one source off stops only its preview.
        QVERIFY(backend.setSourceSetting("screen-2", "enabled", false));
        QCOMPARE(state("screen-2")["previewState"].toString(), "inactive"); QCOMPARE(state("screen")["previewState"].toString(), "live");
        backend.setWindowVisible(false);
        QCOMPARE(state("screen")["previewState"].toString(), "inactive");
    }
    void screenMonitorDecodesActualStreamAndDetectsStaleness() {
        QTemporaryDir dir; const auto file = dir.filePath("screen.ts");
        QProcess encoder; encoder.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=15", "-t", "3", "-c:v", "libx264", "-preset", "ultrafast", "-g", "15", "-f", "mpegts", file});
        QVERIFY(encoder.waitForFinished(5000)); QCOMPARE(encoder.exitCode(), 0);
        QFile stream(file); QVERIFY(stream.open(QIODevice::ReadOnly));
        ScreenMonitor monitor; monitor.setEnabled(true); monitor.start();
        QCOMPARE(monitor.state(), "waiting");
        monitor.feed(stream.readAll());
        QTRY_VERIFY_WITH_TIMEOUT(monitor.frameNumber() > 5, 5000);
        QCOMPARE(monitor.image().width(), 640); QVERIFY(monitor.jpeg().startsWith(QByteArray::fromHex("ffd8")));
        QCOMPARE(monitor.state(), "live");
        QTRY_COMPARE_WITH_TIMEOUT(monitor.state(), "stalled", 5000);
        monitor.setEnabled(false); QCOMPARE(monitor.state(), "inactive"); QVERIFY(monitor.jpeg().isEmpty());
        monitor.stop(); monitor.start(); QCOMPARE(monitor.frameNumber(), 0); QVERIFY(monitor.image().isNull());
        monitor.stop();
    }
    void migrationKeepsExistingPreferences() {
        QTemporaryDir dir;
        const auto old = dir.filePath("old.json"), target = dir.filePath("new/settings.json");
        write(old, "{\"directory\":\"/Videos/My takes\",\"audio\":\"custom-microphone\"}");
        QVERIFY(Backend::importLegacySettings(old, target));
        QFile imported(target); QVERIFY(imported.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(imported.readAll()).object()["directory"].toString(), "/Videos/My takes"); imported.close();
        write(old, "{\"directory\":\"/different\"}");
        QVERIFY(Backend::importLegacySettings(old, target));
        QVERIFY(imported.open(QIODevice::ReadOnly)); QVERIFY(imported.readAll().contains("My takes")); imported.close();
        write(dir.filePath("broken.json"), "not json"); QString error;
        QVERIFY(!Backend::importLegacySettings(dir.filePath("broken.json"), dir.filePath("bad.json"), &error));
        QVERIFY(!QFileInfo::exists(dir.filePath("bad.json")));
    }
    void sourceSelectionAndDeviceTimestamps() {
        QJsonObject selected{{"cameraEnabled",false},{"screenEnabled",false},{"microphoneEnabled",false},{"desktopEnabled",false}};
        QString error; QVERIFY(!SessionRecorder::validateSources(selected,&error));
        selected["screenEnabled"] = true; selected["screenTarget"] = "portal";
        QVERIFY(SessionRecorder::validateSources(selected,&error));
        selected["screenQuality"] = "custom;unsafe"; QVERIFY(!SessionRecorder::validateSources(selected,&error));
        selected["screenQuality"] = "ultra"; QVERIFY(SessionRecorder::validateSources(selected,&error));
        selected["microphoneEnabled"] = true; selected["audio"] = "none";
        QVERIFY(!SessionRecorder::validateSources(selected,&error));
        QCOMPARE(SessionRecorder::packetTimestamp("[video] demuxer -> ist_index:0:0 type:video pkt_pts_time:1795556332.248325"),1795556332248325LL);
        QCOMPARE(SessionRecorder::packetTimestamp("demuxer+ffmpeg -> pkt_pts_time:0.2"),0LL);
    }
    void editingTimelinePreservesIndependentOffsets() {
        QJsonObject manifest{{"sources",QJsonArray{
            QJsonObject{{"id","screen"},{"kind","Video"},{"file","screen.mkv"},{"state","saved"},{"firstMonotonicUs",10000000},{"durationSeconds",3}},
            QJsonObject{{"id","camera"},{"kind","Video"},{"file","camera.mkv"},{"state","saved"},{"firstMonotonicUs",10200000},{"durationSeconds",2.8}}
        }}};
        const auto timeline = SessionRecorder::timeline(manifest,"/tmp/test take");
        const auto tracks=timeline["tracks"].toObject()["children"].toArray(); QCOMPARE(tracks.size(),2);
        const auto gap=tracks[1].toObject()["children"].toArray()[0].toObject(); QCOMPARE(gap["OTIO_SCHEMA"].toString(),"Gap.1");
        QCOMPARE(gap["source_range"].toObject()["duration"].toObject()["value"].toDouble(),12.0);
    }
    void screenPermissionFailureKeepsSessionMetadata() {
        QTemporaryDir dir; auto program=dir.filePath("screen-denied");
        write(program,"#!/bin/sh\necho 'Screen sharing permission denied' >&2\nexit 2\n");
        QVERIFY(QFile::setPermissions(program,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        SessionRecorder session(nullptr,program);
        QJsonObject selected{{"directory",dir.path()},{"cameraEnabled",false},{"screenEnabled",true},{"microphoneEnabled",false},{"desktopEnabled",false},{"screenTarget","portal"}};
        QVERIFY(session.start(selected)); QTRY_VERIFY(!session.busy());
        QCOMPARE(session.status()["state"].toString(),"error");
        QVERIFY(session.status()["error"].toString().contains("permission denied"));
        QFile metadata(session.status()["output"].toString()+"/session.json"); QVERIFY(metadata.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(metadata.readAll()).object()["state"].toString(),"incomplete");
    }
    void cameraFailureStopsOtherSelectedWorkers() {
        QTemporaryDir dir; const auto program = dir.filePath("fake-audio");
        write(program, "#!/bin/sh\nwhile read -r line; do [ \"$line\" = q ] && exit 0; done\n");
        QVERIFY(QFile::setPermissions(program, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        SessionRecorder session(nullptr, "gpu-screen-recorder", program);
        QSignalSpy camera(&session, &SessionRecorder::cameraStartRequested);
        QJsonObject selected{{"directory",dir.path()},{"cameraEnabled",true},{"screenEnabled",false},
            {"microphoneEnabled",true},{"audio","fake-mic"},{"desktopEnabled",true},{"desktopAudio","fake-monitor"}};
        QVERIFY(session.start(selected)); QCOMPARE(camera.count(), 1);
        session.cameraFinished("camera", 1, "Camera disconnected");
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 5000);
        QCOMPARE(session.status()["state"].toString(), "error");
        QVERIFY(session.status()["error"].toString().contains("Camera disconnected"));
        QCOMPARE(session.status()["sources"].toList().size(), 3);
    }
    void validatesAndRecoversAnInterruptedTake() {
        QTemporaryDir dir;
        const QString video=dir.filePath("fixture.mkv");
        QProcess ffmpeg; ffmpeg.start("ffmpeg",{"-v","error","-f","lavfi","-i","testsrc2=size=64x64:rate=10","-t","0.5","-c:v","libx264",video});
        QVERIFY(ffmpeg.waitForFinished(5000)); QCOMPARE(ffmpeg.exitCode(),0);
        QDir(dir.path()).mkdir("take-interrupted"); const QString take=dir.filePath("take-interrupted");
        QVERIFY(QFile::copy(video,take+"/screen.partial.mkv"));
        QJsonObject manifest{{"schemaVersion",1},{"state","recording"},{"ownerPid",0},{"sources",QJsonArray{QJsonObject{{"id","screen"},{"kind","Video"},{"state","recording"},{"file","screen.partial.mkv"},{"firstMonotonicUs",1000000}}}}};
        write(take+"/session.json",QJsonDocument(manifest).toJson());
        SessionRecorder recovery; QVERIFY(recovery.recover(dir.path())); QTRY_VERIFY(!recovery.busy());
        QCOMPARE(recovery.status()["state"].toString(),"saved"); QVERIFY(QFileInfo::exists(take+"/screen.mkv"));
        QVERIFY(QFileInfo::exists(take+"/timeline.otio"));
    }
    void validatesModesAndPaths() {
        auto s = settings(); QString error;
        QVERIFY(Backend::validSettings(s, &error));
        s["mode"] = "1920x1080@30/0:mjpeg"; QVERIFY(!Backend::validSettings(s, &error));
        s["mode"] = "1080p; rm anything"; QVERIFY(!Backend::validSettings(s, &error));
        s = settings(); s["directory"] = "relative"; QVERIFY(!Backend::validSettings(s, &error));
    }
    void nativeVideoCopiedAndInputsRemainLiteral() {
        auto args = CameraFeed::arguments(camera(), "/tmp/my clips/a.mkv", true);
        QVERIFY(args.contains("-n"));
        QCOMPARE(args[args.indexOf("-c:v") + 1], "copy");
        QVERIFY(args.contains("/tmp/my clips/a.mkv"));
        QVERIFY(!args.contains("pulse"));
        QVERIFY(args.contains("abs"));
        const auto audio = SessionRecorder::audioArguments("studio mic;$(false)", "/tmp/audio.wav", true);
        QVERIFY(audio.contains("studio mic;$(false)"));
        QVERIFY(audio.contains("pcm_s16le"));
        QCOMPARE(args.count("/dev/video0"), 1);
        QCOMPARE(args[args.indexOf("-vf") + 1], "fps=30,scale=640:-2");
    }
    void silentRecordingHasNoAudioInput() {
        auto args = CameraFeed::arguments(camera(), "/tmp/a.mkv", true);
        QVERIFY(!args.contains("pulse")); QVERIFY(!args.contains("1:a:0"));
        auto preview = CameraFeed::arguments(camera(), {}, false);
        QVERIFY(!preview.contains("matroska")); QVERIFY(preview.contains("image2pipe"));
    }
    void meterParsesLiveMetadata() {
        QVERIFY(!Backend::parsePeak("not a meter"));
        QCOMPARE(*Backend::parsePeak("[Parsed_ametadata_1 @ 0xabcd] lavfi.astats.Overall.Peak_level=-18.25"), -18.25);
        QCOMPARE(*Backend::parsePeak("lavfi.astats.Overall.Peak_level=-inf"), -90.0);
        QCOMPARE(*Backend::parsePeak("lavfi.astats.Overall.Peak_level=2"), 0.0);
        QVERIFY(!Backend::parsePeak("lavfi.astats.Overall.Peak_level=nan"));
        QVERIFY(!Backend::parsePeak("lavfi.astats.Overall.Peak_level=invalid"));
    }
    void fullPaletteFollowsThemeReplacements() {
        QTemporaryDir dir; QDir(dir.path()).mkpath("theme");
        const QString path = dir.filePath("theme/colors.toml");
        write(path, "mode = \"dark\"\naccent = \"#e68e0d\"\nbackground = \"#121212\"\nforeground = \"#bebebe\"\n");
        write(dir.filePath("theme/shell.toml"), "[font]\nbase-size = 14\n[controls]\nnormal-fill-alpha = 0.12\n");
        Theme theme(dir.path());
        QCOMPARE(theme.colors()["background"].value<QColor>(), QColor("#121212"));
        QCOMPARE(theme.style()["fontSize"].toDouble(), 14.0);
        QSignalSpy spy(&theme, &Theme::changed);
        QSaveFile replacement(path); QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write("mode = \"light\"\naccent = \"#e68e0d\"\nbackground = \"#fafafa\"\nforeground = \"#242424\"\n"); QVERIFY(replacement.commit());
        QTRY_VERIFY(!theme.dark());
        QCOMPARE(theme.colors()["background"].value<QColor>(), QColor("#fafafa"));
        QCOMPARE(theme.colors()["foreground"].value<QColor>(), QColor("#242424"));
        QVERIFY(spy.count() > 0);
        write(dir.filePath("theme/shell.toml"), "[font]\nbase-size = 16\n");
        QTRY_COMPARE(theme.style()["fontSize"].toDouble(), 16.0);
    }
    void sharedPreviewDataCanRenderWithoutAnotherCamera() {
        QImage image(64, 36, QImage::Format_RGB32); image.fill(QColor("#e68e0d"));
        QByteArray jpeg; QBuffer buffer(&jpeg); QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "JPEG"));
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("cameraFrame", "data:image/jpeg;base64," + QString::fromLatin1(jpeg.toBase64()));
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nImage { source: cameraFrame; cache: false }", QUrl());
        QScopedPointer<QObject> output(component.create()); QVERIFY2(output, qPrintable(component.errorString()));
        QTRY_COMPARE(output->property("status").toInt(), 1);
        QCOMPARE(output->property("implicitWidth").toInt(), 64);
    }
    void rawAndCompressedModesEncodeCorrectly() {
        auto s = camera(); s["mode"] = "1920x1080@30/1:yuyv422";
        auto args = CameraFeed::arguments(s, "/tmp/a.mkv", true);
        QCOMPARE(args[args.indexOf("-c:v") + 1], "libx264");
        s = camera(); s["encoding"] = "h264";
        args = CameraFeed::arguments(s, "/tmp/a.mkv", true);
        QVERIFY(args.contains("18")); QVERIFY(!args.contains("copy"));
    }
    void settingsAreAtomicAndMissingDeviceFails() {
        QTemporaryDir dir; Backend backend(dir.filePath("settings.json"));
        QVERIFY(backend.setSetting("audio", "none"));
        QFile file(dir.filePath("settings.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["audio"].toString(), "none");
        QVERIFY(!backend.setSetting("unknown", "value"));
        QVERIFY(!backend.setSetting("monitorLayout", "diagonal")); QVERIFY(backend.setSetting("monitorLayout", "stacked"));
        QCOMPARE(backend.settings()["monitorLayout"].toString(), "stacked");
        QVERIFY(backend.setSetting("camera", "/dev/sourceroll-does-not-exist"));
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
    void twoRealMonitorsRecordTogether() {
        // Opt-in, because it captures this machine's own monitors: a few seconds of each,
        // into a temporary folder that is removed when the test ends.
        if (qEnvironmentVariable("SOURCEROLL_HARDWARE") != "monitors") QSKIP("Set SOURCEROLL_HARDWARE=monitors to record two real monitors for a few seconds.");
        QTemporaryDir dir; const auto path = dir.filePath("settings.json");
        write(path, QJsonDocument(QJsonObject{{"directory",dir.filePath("takes")},{"audio","none"},{"microphoneEnabled",false},{"desktopEnabled",false},{"videoSources",QJsonArray{}}}).toJson());
        Backend backend(path);
        QStringList monitors;
        for (const auto &screen : backend.screens()) if (screen.toMap()["id"] != "portal") monitors << screen.toMap()["id"].toString();
        if (monitors.size() < 2) QSKIP("Needs two connected monitors.");
        const auto first = backend.addSource("screen"), second = backend.addSource("screen");
        QVERIFY(backend.setSourceSetting(first, "target", monitors[0])); QVERIFY(backend.setSourceSetting(second, "target", monitors[1]));
        QVERIFY(backend.setSourceSetting(second, "name", "Second monitor")); QVERIFY(backend.setSourceSetting(second, "fps", 30));
        auto state = [&](const QString &id) {
            for (const auto &value : backend.status()["videoSources"].toList()) if (value.toMap()["id"] == id) return value.toMap();
            return QVariantMap();
        };
        // Both monitors preview without a permission prompt.
        backend.setWindowVisible(true);
        QTRY_VERIFY_WITH_TIMEOUT(backend.frame(first) > 0 && backend.frame(second) > 0, 20000);
        QVERIFY(!backend.status()["screenSelecting"].toBool());
        QVERIFY(backend.start());
        QTRY_COMPARE_WITH_TIMEOUT(backend.status()["state"].toString(), "recording", 30000);
        QTest::qWait(3000);
        QVERIFY2(backend.status()["warning"].toString().isEmpty(), qPrintable(backend.status()["warning"].toString()));
        QVERIFY(state(first)["recording"].toBool()); QVERIFY(state(second)["recording"].toBool());
        QCOMPARE(state(first)["health"].toString(), "live"); QCOMPARE(state(second)["health"].toString(), "live");
        QVERIFY(backend.frame(first) > 0); QVERIFY(backend.frame(second) > 0);
        backend.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.status()["recording"].toBool(), 30000);
        QVERIFY2(backend.status()["error"].toString().isEmpty(), qPrintable(backend.status()["error"].toString()));
        QCOMPARE(backend.status()["state"].toString(), "saved");
        const auto take = backend.status()["output"].toString();
        QVERIFY(QFileInfo::exists(take + "/screen.mkv")); QVERIFY(QFileInfo::exists(take + "/second-monitor.mkv")); QVERIFY(QFileInfo::exists(take + "/timeline.otio"));
        for (const auto &value : backend.status()["sources"].toList()) {
            const auto source = value.toMap(); const auto video = source["probe"].toMap()["streams"].toList().value(0).toMap();
            QCOMPARE(source["state"].toString(), "saved"); QVERIFY(source["durationSeconds"].toDouble() > 2);
            qInfo().noquote() << QString("%1: %2 x %3, %4 s, starts at %5 s").arg(source["file"].toString()).arg(video["width"].toInt()).arg(video["height"].toInt())
                .arg(source["durationSeconds"].toDouble(), 0, 'f', 2).arg(source["startOffsetSeconds"].toDouble(), 0, 'f', 3);
        }
        backend.setWindowVisible(false);
    }
    void realWindowDrivesTheRealBackend() {
        // The window's other test runs against a stand-in. This one checks the window
        // and the real backend agree on names, calls and signals, end to end.
        QTemporaryDir dir; const auto program = fakeScreenRecorder(dir); QVERIFY(!program.isEmpty());
        write(dir.filePath("language"), "en\n");
        const auto path = dir.filePath("settings.json");
        write(path, QJsonDocument(QJsonObject{{"directory",dir.filePath("takes")},{"audio","none"},{"microphoneEnabled",false},{"desktopEnabled",false},{"videoSources",QJsonArray{
            QJsonObject{{"id","screen"},{"kind","screen"},{"name",""},{"enabled",true},{"target","DP-3"},{"fps",60},{"quality","very_high"}},
            QJsonObject{{"id","camera"},{"kind","camera"},{"name",""},{"enabled",false},{"device",""},{"mode",""},{"encoding","original"}}}}}).toJson());
        Theme theme(dir.path()); Translator translator(dir.filePath("language"));
        Backend backend(path, nullptr, program);
        QQmlApplicationEngine engine;
        engine.addImageProvider("preview", new BackendPreview(&backend));
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("initialVisible", true);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) { for (const auto &e : errors) warnings << e.toString(); });
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->requestActivate(); QTest::qWait(300);
        auto item = [&](const QString &name) { return visualChild(window->contentItem(), name); };
        auto press = [&](const QString &name) { auto button = item(name); QVERIFY2(button, qPrintable(name)); QVERIFY(QMetaObject::invokeMethod(button, "clicked")); };
        // Showing the window starts the preview of the enabled monitor; the camera is off and has no pane.
        QVERIFY(item("monitor-screen")); QVERIFY(item("monitor-screen")->isVisible()); QVERIFY(!item("monitor-camera")->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(item("monitor-screen")->property("imageSource").toString().startsWith("image://preview/screen/"), 10000);
        QTRY_COMPARE(item("monitor-screen")->property("feedState").toString(), "live");
        QCOMPARE(item("cameraChoice-camera")->property("count").toInt(), backend.cameras().size());
        // Adding a screen opens its picker and, once approved, shows it next to the first.
        press("addScreen");
        QTRY_VERIFY(item("monitor-screen-2"));
        QVERIFY(backend.status()["screenSelecting"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!item("monitor-screen-2")->property("imageSource").toString().isEmpty(), 10000);
        QVERIFY(!backend.status()["screenSelecting"].toBool());
        // A name typed in the window reaches the settings, the pane and, later, the file.
        item("name-screen-2")->forceActiveFocus();
        for (const char key : QByteArray("Left monitor")) QTest::keyClick(window, key);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(item("monitor-screen-2")->property("sourceLabel").toString(), "Left monitor");
        QTRY_VERIFY(!window->property("typing").toBool());
        // Point it at a monitor instead: the preview follows without another prompt. (A picked
        // screen only counts as recording once PipeWire shows its link, which a stand-in never has.)
        QVERIFY(backend.setSourceSetting("screen-2", "target", "HDMI-A-1"));
        QTRY_COMPARE_WITH_TIMEOUT(item("monitor-screen-2")->property("feedState").toString(), "live", 10000);
        QVERIFY(!backend.status()["screenSelecting"].toBool());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE_WITH_TIMEOUT(backend.status()["state"].toString(), "recording", 15000);
        QTRY_VERIFY_WITH_TIMEOUT(item("monitor-screen")->property("recording").toBool() && item("monitor-screen-2")->property("recording").toBool(), 5000);
        QVERIFY(!item("name-screen-2")->isEnabled()); QVERIFY(!item("addScreen")->isEnabled());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(!backend.status()["recording"].toBool(), 15000);
        QCOMPARE(backend.status()["state"].toString(), "saved");
        const auto take = backend.status()["output"].toString();
        QVERIFY(QFileInfo::exists(take + "/screen.mkv")); QVERIFY(QFileInfo::exists(take + "/left-monitor.mkv"));
        QTRY_VERIFY(item("name-screen-2")->isEnabled());
        press("remove-screen-2");
        QTRY_VERIFY(!item("monitor-screen-2")); QVERIFY(backend.settings()["videoSources"].toList().size() == 2);
        window->setProperty("allowQuit", true); QVERIFY(window->close());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void realQmlKeyboardHideAndScreenshots() {
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
        auto item = [&](const QString &name) { return visualChild(window->contentItem(), name); };
        auto click = [&](QQuickItem *target) { QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint()); };
        // Controls further down the source list can sit below the fold, so they are pressed directly.
        auto press = [&](const QString &name) { auto button = item(name); QVERIFY2(button, qPrintable(name)); QVERIFY(QMetaObject::invokeMethod(button, "clicked")); };
        auto screenMonitor = item("monitor-screen"); QVERIFY2(screenMonitor, qPrintable(warnings.join('\n')));
        auto cameraMonitor = item("monitor-camera"); QVERIFY(cameraMonitor);
        QVERIFY(screenMonitor->isVisible()); QVERIFY(cameraMonitor->isVisible()); QVERIFY(!item("monitorEmpty")->isVisible());
        QCOMPARE(screenMonitor->property("imageSource").toString(), "image://preview/screen/1");
        const auto screenImage = screenMonitor->property("imageSource");
        screenMonitor->setProperty("imageSource", "");
        QSignalSpy selection(&backend, &DemoBackend::screenChoiceRequested);
        click(screenMonitor);
        QCOMPARE(selection.count(), 1); QCOMPARE(backend.chosen.last(), "screen");
        screenMonitor->setProperty("imageSource", screenImage);
        for (const QString &view : {QString("screen"), QString("camera"), QString("all")}) {
            auto button = item("monitor" + view.left(1).toUpper() + view.mid(1)); QVERIFY(button);
            click(button);
            QTRY_COMPARE(window->property("monitorView").toString(), view);
            QCOMPARE(screenMonitor->isVisible(), view != "camera");
            QCOMPARE(cameraMonitor->isVisible(), view != "screen");
        }
        auto layoutButton = item("monitorLayout"); QVERIFY(layoutButton);
        QVERIFY(screenMonitor->y() == cameraMonitor->y()); // wide window starts side by side
        for (const QString &layout : {QString("stacked"), QString("side")}) {
            click(layoutButton);
            QTRY_COMPARE(backend.monitorLayout, layout);
            QTRY_COMPARE(screenMonitor->y() == cameraMonitor->y(), layout == "side");
            QCOMPARE(screenMonitor->x() == cameraMonitor->x(), layout == "stacked");
        }
        // Each camera source lists the devices and, below, the modes of the one it uses.
        auto cameraChoice = item("cameraChoice-camera"); QVERIFY(cameraChoice);
        QCOMPARE(cameraChoice->property("currentIndex").toInt(), 0);
        QCOMPARE(backend.rescans, 0);
        click(cameraChoice);
        QTRY_COMPARE(backend.rescans, 1); // opening the list looks for cameras plugged in since
        QTest::qWait(50); QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        auto popup = cameraChoice->property("popup").value<QObject *>(); QVERIFY(popup);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_COMPARE(backend.source("camera")["device"].toString(), "overhead");
        QTRY_COMPARE(cameraChoice->property("currentIndex").toInt(), 1);
        QCOMPARE(backend.source("camera")["mode"].toString(), "720");
        // A choice the recorder turns down falls back to the stored one.
        QTest::qWait(250); click(cameraChoice);
        QTest::qWait(50); QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_COMPARE(cameraChoice->property("currentIndex").toInt(), 1);
        QCOMPARE(backend.source("camera")["device"].toString(), "overhead");
        QTest::qWait(250); window->contentItem()->forceActiveFocus();

        // Add a second screen: it gets a pane, a switch and its own settings, and opens its picker.
        press("addScreen");
        QTRY_VERIFY(item("monitor-screen-2"));
        QCOMPARE(backend.chosen.last(), "screen-2");
        QCOMPARE(item("monitor-screen-2")->property("sourceLabel").toString(), "Screen 2");
        QCOMPARE(item("switch-screen-2")->property("text").toString(), "Screen 2");
        QVERIFY(item("source-screen-2")); QVERIFY(item("monitor-screen-2")->isVisible());
        screenMonitor = item("monitor-screen"); cameraMonitor = item("monitor-camera"); QVERIFY(screenMonitor); QVERIFY(cameraMonitor);
        // Typing a name is not a shortcut: Space, Q and ? stay in the field.
        auto name = item("name-screen-2"); QVERIFY(name);
        QCOMPARE(name->property("hint").toString(), "Screen 2");
        name->forceActiveFocus(); QTRY_VERIFY(window->property("typing").toBool());
        for (const char key : QByteArray("Left monitor Q?")) QTest::keyClick(window, key);
        QVERIFY(!backend.recording); QVERIFY(!window->property("helpVisible").toBool());
        QVERIFY(!window->findChild<QObject *>("quitDialog")->property("visible").toBool());
        QCOMPARE(name->property("text").toString(), "Left monitor Q?");
        for (int i = 0; i < 3; ++i) QTest::keyClick(window, Qt::Key_Backspace);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(backend.source("screen-2")["name"].toString(), "Left monitor");
        QTRY_COMPARE(item("monitor-screen-2")->property("sourceLabel").toString(), "Left monitor");
        QCOMPARE(item("switch-screen-2")->property("text").toString(), "Left monitor");
        QTRY_VERIFY(!window->property("typing").toBool()); // Enter gave the keyboard back
        // Its switch takes it out of the take and the monitor, and brings it back.
        click(item("switch-screen-2"));
        QTRY_VERIFY(!backend.source("screen-2")["enabled"].toBool()); QTRY_VERIFY(!item("monitor-screen-2")->isVisible());
        const int picks = backend.chosen.size();
        click(item("switch-screen-2"));
        QTRY_VERIFY(backend.source("screen-2")["enabled"].toBool()); QTRY_VERIFY(item("monitor-screen-2")->isVisible());
        QCOMPARE(backend.chosen.size(), picks + 1); // a picked screen has to be approved again
        // The view buttons filter by kind, whatever the number of sources.
        click(item("monitorScreen"));
        QTRY_VERIFY(!cameraMonitor->isVisible()); QVERIFY(screenMonitor->isVisible()); QVERIFY(item("monitor-screen-2")->isVisible());
        click(item("monitorAll")); QTRY_VERIFY(cameraMonitor->isVisible());

        QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(backend.recording);
        QTRY_VERIFY(item("monitor-screen-2")->property("recording").toBool());
        auto displayWarning = item("displayWarning"); QVERIFY(displayWarning);
        auto captureWarning = item("captureWarning"); QVERIFY(captureWarning);
        QVERIFY(!displayWarning->isVisible());
        backend.displayWarning = "OmaControl is open during screen recording.";
        backend.warning = "Screen capture interrupted. Reconnecting…"; emit backend.statusChanged();
        QTRY_VERIFY(displayWarning->isVisible()); QTRY_VERIFY(captureWarning->isVisible());
        QCOMPARE(displayWarning->property("text").toString(), backend.displayWarning);
        backend.displayWarning.clear(); backend.warning.clear(); emit backend.statusChanged();
        QTRY_VERIFY(!displayWarning->isVisible()); QTRY_VERIFY(!captureWarning->isVisible());
        QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(!backend.recording);
        auto screenshot = window->grabWindow(); QVERIFY(!screenshot.isNull());
        QString path = qEnvironmentVariable("SOURCE_RECORD_SCREENSHOT");
        if (!path.isEmpty()) QVERIFY(screenshot.save(path));
        write(dir.filePath("language"), "da\n"); QTRY_COMPARE(translator.language(), "da"); QTest::qWait(150);
        QCOMPARE(item("switch-camera")->property("text").toString(), "Kamera"); // unnamed sources follow the language
        QCOMPARE(item("switch-screen-2")->property("text").toString(), "Left monitor");
        if (!path.isEmpty()) QVERIFY(window->grabWindow().save(QFileInfo(path).absolutePath() + "/screenshot-da.png"));
        // Removing sources takes their panes with them, down to an empty monitor.
        press("remove-screen-2");
        QTRY_VERIFY(!item("monitor-screen-2")); QVERIFY(!item("source-screen-2")); QVERIFY(item("monitor-screen"));
        press("remove-screen"); press("remove-camera");
        QTRY_VERIFY(!item("monitor-camera")); QTRY_VERIFY(item("monitorEmpty")->isVisible());
        QVERIFY(!item("monitorLayout")->isVisible());
        press("addCamera");
        QTRY_VERIFY(item("monitor-camera")); QVERIFY(!item("monitorEmpty")->isVisible());
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
#include "sourceroll_tests.moc"
