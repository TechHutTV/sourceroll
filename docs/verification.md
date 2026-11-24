# Local verification

Verified on the recording computer on 2026-11-24.

- qmake6 / GCC build succeeds.
- Qt Test: 12 cases pass, including settings persistence, unavailable camera errors, source argument handling, no-audio capture construction, stream copy versus encoding, full theme/language watching, peak parsing, actual QML loading and dropdown interaction, Space start/stop, Escape hide, close versus quit, and English/Danish window rendering without QML warnings.
- Plugin manifest passes `omarchy plugin validate`.
- Plugin JavaScript tests cover malformed/oversized status data, markup boundaries, elapsed formatting, and PlainText usage for status/error/path output.
- Real Canon V4L2 source detected: MJPEG 1920×1080 at 30/1 fps.
- Real Original-mode capture with Shure MV7+ audio inspected using ffprobe: MJPEG 1920×1080 at 30 fps, PCM s16le 48 kHz audio, finalized MKV. Audio present, preview frames received, microphone meter responds.
- Separate H.264/no-audio capture inspected using ffprobe: H.264 1920×1080 at 30 fps, no audio stream, readable finalized MKV after SIGTERM. Resident exits cleanly.
- Cold CLI startup returns promptly while the detached recorder remains available; no inherited stdout pipe holds the caller open.
- Original-mode camera stream measured about 1.2 GB per minute. H.264 sample was about 4.5 MB for 2.53 seconds of this scene; size depends on motion and scene detail.

Re-run software tests with `bin/test`. To explicitly test a connected camera, first quit the app, then run `python3 tests/hardware_smoke.py --record`. It uses a temporary output directory, restores settings, and verifies shutdown. It does not manipulate OBS.

The monitoring preview is 640 pixels wide at 30 fps. This does not resize or drop frames from the separate native camera output. The meter is visual only; no microphone loopback is enabled.

## Installed integration

- Installed Arch package source-record 0.1.1-1; desktop entry validates.
- User plugin local.source-record enabled in the right bar section before the tray. Plugin backups live outside the plugin discovery directory.
- The installed Wayland app opens successfully with the Canon preview and Shure meter. User confirmed the 30 fps preview is smooth, with a slight delay.
- Installed recorder command verified using fps=30 for its 640-pixel preview. Recording remains off; preview intentionally left open for the user.

## Theme and metering update (0.2.0)

- Full palette follows atomic theme-file replacements, including a dark-to-light change with an unchanged accent; shell font-size updates are observed live. Theme tests use temporary directories and leave the desktop theme alone.
- App controls use Omarchy palette values, monospace typography, flat borders and state fills. Generic Material chrome and fixed blue-gray colors were removed.
- Live microphone subscription received 65 readings over 3.08 seconds in a hardware test, with a median 40.1 ms between updates. This measures delivery cadence, not end-to-end acoustic latency.
- App meter updates independently of the 250 ms general status refresh. Bar meter uses a persistent local socket, not subprocess polling. Slow subscribers cannot accumulate an unbounded meter queue.

## Shared widget preview (0.3.0)

- Widget preview uses a user-only local socket carrying bounded JPEG data frames from the existing FFmpeg output. The shell accepts only validated JPEG data URLs, never remote image URLs.
- Preview subscribers are counted; the camera stays open while the app, popup, or recording needs it. Closing the last popup while idle and hidden releases capture.
- A synthetic signal timing test measured about 56 ms from the expected tone onset to a live meter event. The temporary test sink was removed and the selected microphone restored. This is a software timing check, not a physical microphone-to-screen measurement.
- Meter falloff was shortened so both app and tray settle promptly after speech.

- Shared-feed hardware check received valid JPEGs for two simultaneous subscribers (about 15 KB per frame), confirmed the same single FFmpeg PID, and confirmed capture stopped after the last subscriber disconnected with the window hidden. No recording was started.
- Removed the OBS independence footer from the widget at the user’s request.
- Restarted the shell after finding an old cached QML component. The updated widget loads the preview and has no OBS footer.
- Widget image loading is asynchronous and retains the previous frame while the next JPEG is decoded, avoiding a blank frame on each source change. Plugin regression checks enforce that behavior.

## Sourceroll and separate source sessions (0.4.0)

- Renamed app, package, launcher, socket, settings, repository, and local widget to Sourceroll. Legacy settings import only when the new settings file does not exist; existing recording paths and files are preserved.
- Added screen/window, camera, microphone, and desktop audio switches in both interfaces. Sessions keep video in separate MKV files and audio in separate 48 kHz PCM WAV/RF64 files.
- Real portal capture produced four validated outputs: camera 3.561 s, screen 3.433 s, microphone 3.499 s, desktop 3.520 s. Start offsets were 0.166 s, 0 s, 0.093 s, and 0.089 s respectively. The OTIO loaded with OpenTimelineIO and contained four tracks. These timestamps do not calibrate physical camera/microphone latency.
- Direct monitor capture worked once but failed in another multi-GPU discovery state. The native portal remains the tested default. The system picker supplies snapshots; the user chose to leave it unchanged rather than replace it for live thumbnails.
- Unit tests cover permission denial, coordinated failure shutdown, source selection, absolute device timestamps, timeline gaps, and recovery of interrupted files. Low disk checks, bounded logs/IPC frames, tracked children, atomic manifests, and partial preservation are implemented.
- The opt-in hardware smoke test terminates the resident during camera/microphone capture and verifies both media files, duration/timing, a complete manifest, and timeline. It restores settings and waits for process exit. It exposed a repeated termination notifier issue; disabling/draining the notifier and finalizing before Qt exits fixed it.
- Plugin audit against https://plugins.omarchy.org/develop.html: one bar-widget kind, local namespaced ID, matching entry point, separate Panel.qml, standard Panel/KeyboardPanel/PanelKeyCatcher, lifecycle forwarding, theme tokens, translations, license and dependency/removal notes. Manifest validation and QML lint pass with dynamic loader/theme missing-property diagnostics treated as informational. Native lifecycle verification follows installation.
- No marketplace publication was requested or performed. The plugin remains in the app repository's plugin subdirectory with a local ID; publication requires a permanent ID and public plugin repository with its manifest at the root.
- Native install check: the widget was discovered/enabled in its original right-bar placement, summon opened a themed panel with the live shared camera preview and source controls, and hide removed its panel layer. Startup null-host bindings were corrected with defaults; the restarted shell showed no Sourceroll QML errors. Package dependency checks report FFmpeg, ffprobe, pactl, and GPU Screen Recorder available.
- Final installed package is Sourceroll 0.4.0-1; pacman integrity check reports zero altered files. The former Source Record package/launcher was removed after migration. Native disable/re-enable and panel reopening passed, with no Sourceroll runtime QML errors. The app is idle with live preview, not recording.

## Screen monitoring and selection (0.5.0)

- App supports Screen / Camera / Both, with source labels, per-source recording state, and stale-preview warnings. Empty Screen panes open the native picker, including Both view; empty Camera panes open the device selector. Monitor view changes do not change enabled recording sources.
- Widget supports separate Screen / Camera monitor buttons, source switches, and Choose screen/window. Turning Screen on opens the native chooser. A selection preview does not save a take. The shared system picker remains unchanged.
- Screen recording uses a tee output with a primary Matroska file and a disposable MPEG-TS preview branch over the child stdout pipe. The latter uses FFmpeg's FIFO packet-drop mode and onfail=ignore; the decoder has bounded input/output buffers, retries after failure, and can be released while recording continues. No second screen capture is opened during recording.
- Screen previews are 640 pixels wide at up to 15 fps. Original recording dimensions remain native, with selectable High / Very high / Ultra quality and 30/60 fps controls. The camera preview remains at up to 30 fps.
- Idle screen preview uses one temporary capture stream with no video file output. Starting a take stops that stream before starting the recording and asks the portal to restore its selection. Restore capabilities live in a private temporary runtime folder, are not stored in settings/manifests, and token diagnostics are excluded from retained logs. The portal can ask for approval again. A pending source choice has a timeout and blocks recording until resolved.
- Native screen-only recording delivered 46 preview frames and a validated 2560x1440 H.264 MKV. Native preflight test showed the selected source without recording, then restored the selection without another picker and saved all four source files: camera 4.529 s, screen 4.266 s, microphone 4.501 s, desktop 4.501 s. The saved screen file also decoded without errors.
- Automated coverage includes decoding a generated transport stream into previews, stale detection, preview cleanup, real QML Screen/Camera/Both interactions, clicking an empty Screen pane, quality validation, and source-tagged/bounded widget preview IPC. English and Danish screenshots use synthetic sources.
- Final native install: package 0.5.0-1 has zero altered packaged files. The restarted widget opens with Screen/Camera monitor controls, an empty screen chooser target, separate source switches, and no Sourceroll QML runtime errors. The app's Both view stacks naturally in a narrow tiled window. No recording is active. Source and widget changes are kept local; nothing has been published or pushed.
