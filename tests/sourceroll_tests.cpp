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
#include "session.h"
#include "theme.h"
#include "translator.h"

class DemoBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cameras READ cameras CONSTANT)
    Q_PROPERTY(QVariantList microphones READ microphones CONSTANT)
    Q_PROPERTY(QVariantList screens READ screens CONSTANT)
    Q_PROPERTY(QVariantList desktopSources READ desktopSources CONSTANT)
    Q_PROPERTY(double desktopPeakDb READ desktopPeakDb CONSTANT)
    Q_PROPERTY(QVariantList modes READ modes CONSTANT)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(int frameNumber READ frameNumber CONSTANT)
    Q_PROPERTY(int screenFrameNumber READ frameNumber CONSTANT)
    Q_PROPERTY(double peakDb READ peakDb CONSTANT)
public:
    QVariantList cameras() const { return {QVariantMap{{"id", "demo"}, {"label", "Studio Camera"}}}; }
    QVariantList microphones() const { return {QVariantMap{{"id", "mic"}, {"label", "Studio Microphone"}}}; }
    QVariantList screens() const { return {QVariantMap{{"id", "portal"}, {"label", "Choose screen or window"}}}; }
    QVariantList desktopSources() const { return {QVariantMap{{"id", "desktop"}, {"label", "Desktop audio"}}}; }
    double desktopPeakDb() const { return -28; }
    QVariantList modes() const { return {QVariantMap{{"id", "1080"}, {"label", "1920 × 1080 · 30 fps · MJPEG"}}}; }
    QVariantMap settings() const { return {{"camera", "demo"}, {"audio", "mic"}, {"mode", "1080"}, {"directory", "/Videos/Sourceroll"}, {"encoding", "original"}, {"cameraEnabled", true}, {"screenEnabled", true}, {"microphoneEnabled", true}, {"desktopEnabled", false}, {"screenTarget", "portal"}, {"screenFps", 60}, {"monitorLayout", monitorLayout}}; }
    QVariantMap status() const { return {{"state", recording ? "recording" : "preview"}, {"recording", recording}, {"preview", true}, {"screenPreviewState", "live"}, {"cameraPreviewState", "live"}, {"elapsedSeconds", 83}, {"bytes", 155000000}, {"peakDb", -18}, {"error", ""}, {"warning", warning}, {"displayWarning", displayWarning}, {"output", "/Videos/Sourceroll/take-demo"}}; }
    int frameNumber() const { return 1; }
    double peakDb() const { return -18; }
    Q_INVOKABLE void setWindowVisible(bool) {}
    Q_INVOKABLE void toggle() { recording = !recording; emit statusChanged(); }
    Q_INVOKABLE void stop() { recording = false; emit statusChanged(); }
    Q_INVOKABLE void openFolder() {}
    Q_INVOKABLE void refreshDevices() {}
    Q_INVOKABLE bool recoverSession() { return true; }
    Q_INVOKABLE bool chooseScreen() { emit screenChoiceRequested(); return true; }
    Q_INVOKABLE void shutdown() {}
    Q_INVOKABLE bool setSetting(const QString &key, const QVariant &value) { if (key == "monitorLayout") { monitorLayout = value.toString(); emit settingsChanged(); } return true; }
    bool recording = false;
    QString warning, displayWarning, monitorLayout;
signals:
    void statusChanged();
    void settingsChanged();
    void screenChoiceRequested();
};

class DemoPreview : public QQuickImageProvider {
public:
    DemoPreview() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        QImage image(640, 360, QImage::Format_RGB32);
        for (int y = 0; y < 360; ++y) for (int x = 0; x < 640; ++x) {
            const bool circle = (x - 320)*(x - 320)+(y - 180)*(y - 180) < 95*95;
            image.setPixelColor(x,y,id.startsWith("screen/") ? ((x / 80 + y / 45) % 2 ? QColor("#343944") : QColor("#20242b")) : circle ? QColor("#ffd60a") : QColor(18+x/18, 24+y/15, 38+x/25));
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
        connect(&session, &SessionRecorder::cameraStartRequested, this, [&](const QString &file) {
            QVERIFY(QFile::copy(fixture, file)); session.cameraTimestamp(QDateTime::currentMSecsSinceEpoch()*1000);
        });
        connect(&session, &SessionRecorder::cameraStopRequested, this, [&] { session.cameraFinished(0, {}); });
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
        session.cameraFinished(1, "Camera disconnected");
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
        auto args = Backend::ffmpegArguments(settings(), "/tmp/my clips/a.mkv", true);
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
        auto s = settings(); s["audio"] = "none";
        auto args = Backend::ffmpegArguments(s, "/tmp/a.mkv", true);
        QVERIFY(!args.contains("pulse")); QVERIFY(!args.contains("1:a:0"));
        auto preview = Backend::ffmpegArguments(s, {}, false);
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
        auto screenMonitor = window->findChild<QQuickItem *>("screenMonitor"); QVERIFY(screenMonitor);
        auto cameraMonitor = window->findChild<QQuickItem *>("cameraMonitor"); QVERIFY(cameraMonitor);
        const auto screenImage = screenMonitor->property("imageSource");
        screenMonitor->setProperty("imageSource", "");
        QSignalSpy selection(&backend, &DemoBackend::screenChoiceRequested);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, screenMonitor->mapToScene(QPointF(screenMonitor->width()/2, screenMonitor->height()/2)).toPoint());
        QCOMPARE(selection.count(), 1);
        screenMonitor->setProperty("imageSource", screenImage);
        for (const QString &view : {QString("screen"), QString("camera"), QString("both")}) {
            auto button = window->findChild<QQuickItem *>("monitor" + view.left(1).toUpper() + view.mid(1)); QVERIFY(button);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
            QTRY_COMPARE(window->property("monitorView").toString(), view);
            QCOMPARE(screenMonitor->isVisible(), view != "camera");
            QCOMPARE(cameraMonitor->isVisible(), view != "screen");
        }
        auto layoutButton = window->findChild<QQuickItem *>("monitorLayout"); QVERIFY(layoutButton);
        QVERIFY(screenMonitor->y() == cameraMonitor->y()); // wide window starts side by side
        for (const QString &layout : {QString("stacked"), QString("side")}) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, layoutButton->mapToScene(QPointF(layoutButton->width()/2, layoutButton->height()/2)).toPoint());
            QTRY_COMPARE(backend.monitorLayout, layout);
            QTRY_COMPARE(screenMonitor->y() == cameraMonitor->y(), layout == "side");
            QCOMPARE(screenMonitor->x() == cameraMonitor->x(), layout == "stacked");
        }
        auto cameraChoice = window->findChild<QQuickItem *>("cameraChoice"); QVERIFY(cameraChoice);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, cameraChoice->mapToScene(QPointF(cameraChoice->width()/2, cameraChoice->height()/2)).toPoint());
        QTest::qWait(50); QTest::keyClick(window, Qt::Key_Down); QTest::keyClick(window, Qt::Key_Return);
        auto popup = cameraChoice->property("popup").value<QObject *>(); QVERIFY(popup);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTest::qWait(250); window->contentItem()->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space); QTRY_VERIFY(backend.recording);
        auto displayWarning = window->findChild<QQuickItem *>("displayWarning"); QVERIFY(displayWarning);
        auto captureWarning = window->findChild<QQuickItem *>("captureWarning"); QVERIFY(captureWarning);
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
#include "sourceroll_tests.moc"
