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

## Several screens and cameras (0.6.0)

Verified on the recording computer on 2026-11-24.

- Qt Test: 45 cases pass, with one opt-in hardware case skipped by default. `bin/build` succeeds; `bin/check-plugin` validates the manifest and lints both widget files with no warnings; the plugin JavaScript tests pass.
- Source list: earlier single-screen/single-camera settings become the first two entries without loss; a hand-edited list is repaired (unknown kinds, duplicate or unsafe ids, out-of-range values, more than eight entries); names are cleaned and kept unique; file names derived from names stay clear of each other, of `microphone.wav` and `desktop.wav`, and of reconnected segments such as `camera-002`.
- Recorder, with stand-in capture programs: two screens and two cameras record into separately named files and timeline tracks, with a disabled source left out; one screen drops out and reconnects into `left-monitor-002.mkv` while the other screen is never restarted; the saved warning names the interrupted screen. Two picker screens start one after the other, each with its own restore token. The earlier recovery, stall, retry-limit, permission-failure, and camera-failure cases still pass unchanged.
- Camera feed, with a stand-in FFmpeg: preview and recording share one process, the device timestamp is reported only while recording, a requested stop reports a recording that ended normally, and a capture that exits by itself is reported as a failure.
- Screen feed, with a stand-in recorder: a monitor previews without approval; restoring never opens the picker by itself; choosing does, and the approved selection then restores; forgetting removes the token; a capture that exits leaves an error.
- Backend: migration on load without rewriting the file until a change; add, rename, switch, and remove; refused values; the eight-source limit; the earlier settings keys and `configure` options acting on the first screen and camera; the `source-add`, `source-set`, and `source-remove` requests; two screens previewed, recorded, saved, and previewed again through the backend, with sources locked during the take.
- Window: against a stand-in backend, adding a screen opens its picker and adds a pane, switch, and settings; a name typed with a space, Q, and ? in it does not trigger shortcuts and Enter returns the keyboard; switches, kind filters, layout, a refused camera choice, removal down to an empty monitor, and Danish labels for unnamed sources. Against the real backend with a stand-in recorder: preview on show, add and approve a screen, rename by typing, record with Space, settings locked while recording, files saved under the typed name, no QML warnings. English and Danish screenshots use synthetic sources.
- Widget model: every reported source is checked field by field, malformed entries are dropped, the list is capped, an app without a source list still yields one screen and one camera, and preview frames are accepted only for the source on show.
- Real monitors: two GPU Screen Recorder previews ran at once on HDMI-A-1 and DP-3 without a permission prompt. `SOURCEROLL_HARDWARE=monitors` then previewed and recorded both through the backend: `screen.mkv` 1080x1920 and `second-monitor.mkv` 2560x1440, 3.13 s each, start offsets 0.000 s and 0.051 s, both validated, no warnings. The files were in a temporary folder and were removed.
- Installed build, two real cameras: a Canon at 1920x1080 MJPEG 30 fps and an OBSBOT Tiny 2 at 1920x1080 MJPEG 60 fps previewed together with no error, alongside two picker screens. The second camera's first frame arrived about four seconds after it was switched on, and the first camera's preview was not interrupted. A take with both cameras was not recorded as part of this check.
- Installed build: package 0.6.0-1 installed over 0.5.0-1 and read the earlier settings as one screen and one camera. The widget loaded in the restarted shell and its popup opened with no errors logged.
- Found while testing on hardware and fixed: a camera added before the app had noticed it was created without a device and switched off, and stayed off after a refresh. Adding a camera now looks for new cameras first, and declines with a message when none is free. `bin/install-plugin` moved the widget in the bar on reinstall; the earlier placement was restored by hand and the installer is unchanged.
- Review fixes, each with a test that fails without the fix: a capture that ignores the stop request is ended eight seconds after the first request although the request is repeated; a timestamp arriving while a take stops is kept without turning it back into a recording; a replacement picker capture waits while another picker capture is still unanswered; the other sources wait until no screen is reconnecting; an add request with a refused option, or a save that fails, leaves no source behind, and remove, change, and choose leave everything as it was when the save fails; a camera that is not set up does not keep a working one from its device; a camera already used under its other path is not offered as free; a screen that is writing shows as recording while another is being chosen; a picked screen cannot be switched on while another picker is open. The widget model picks 0.5.0's own commands for a recorder without a source list.
- Command line and socket, against a separate instance with its own runtime and settings folders: an option without a value is refused, also before `--json`; a name with a space and a `--name=VALUE` starting with `--` are accepted; an add with a refused option leaves the list unchanged; three requests sent in one write on one connection are all handled, in order.
- Not verified here: recording, as opposed to previewing, from two real cameras, and the USB bandwidth limit; the order of the prompts and restoring two selections when a take starts with two picker screens; the widget's appearance, which has lint, model tests, and an error-free load only; and `tests/hardware_smoke.py`, which was updated for the source list but not rerun because it needs the resident app stopped.

## AUR package and releases

- Recipe: `bin/aur-package --local` wrote `PKGBUILD` and `.SRCINFO` with the checksum of a tarball of the commit, and `makepkg` built `sourceroll-0.6.0-1` from them. The package holds the app, launcher entry, icon, license, README, widget files, and the widget installer with `sourceroll-install-widget` linked to it.
- Clean build: the release workflow's package job ran on GitHub in an `archlinux:base-devel` container from a throwaway branch. With only the recipe's `depends` installed, plus Git, Node.js, Python, a font and namcap, it passed 44 tests with 2 skipped (the opt-in monitor test, and the camera path test, which needs a camera), then built the package. namcap named no missing dependency; its warnings are the Omarchy shell's QML modules used by the widget, Python as an optional dependency, and the programs the app runs, which it cannot see.
- Tests without a GPU: the suite also passes locally with `QT_QUICK_BACKEND=software`, which the workflow sets.
- Publishing script, against a local bare repository standing in for the AUR: the first push creates the history with `PKGBUILD` and `.SRCINFO` only, a second run with nothing new pushes nothing, a raised `pkgrel` pushes a second commit, and a checksum that is not the tarball's is refused before anything is cloned. Trial files from `--local` are refused the same way while the tag does not exist.
- Version check: `bin/check-release` prints 0.6.0, and fails naming the file when a copy of the version differs.
- Draft notes: Release Drafter ran in dry-run mode from the same throwaway branch and proposed a draft named and tagged `v0.6.0` listing pull requests 1 to 3. Without the first-commit baseline it proposed "No changes", because no release exists to compare with.
- Widget installer, against a scratch home folder and stand-in `omarchy` commands: a first install asks for the right of the bar; a widget already on the bar, under this name or the former one, is enabled without a placement, which the shell's enable code leaves where it is; run through the packaged link it takes the files beside it; without Omarchy it stops before writing anything. The reinstall case fails on the earlier installer.
- Not verified: a real release. Publishing the draft, attaching the package to it, and the push to the AUR with the pinned host key and the secret have not run, because that needs a published tag and an AUR account. The host key in the workflow was read with `ssh-keyscan` and its fingerprint matches the one on the AUR home page. The real `omarchy plugin enable` was read, not run, for the reinstall case.

## Screen connection recovery (PR branch)

- Added PipeWire graph fixtures for the recorder's client/node/link, partial JSON updates, unrelated failures, missing links, and repeated error events.
- Process integration tests cover a live recorder with a failing link, worker exit and recovery, Stop during retry, retry exhaustion, preserving the camera worker, and a buffered file with continuing encoded output. Generated media is validated with ffprobe; no real recording is used as a test fixture.
- Timeline checks verify that replacement screen segments share one track with measured gaps, while unreadable segments remain preserved outside the timeline.
- Timecode tests cover millisecond precision, minute rollover, and takes over 24 hours. Recovery tests verify timestamps remain in the final warning and metadata.
- QML tests check that display and capture warnings appear and disappear. Plugin parsing tests cover recovery indicators and display warnings.
- Headless tests run with `QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Basic bin/test` to avoid the desktop GTK platform theme trying to open a display. `bin/build` and `bin/check-plugin` are also required.
- Live desktop verification is still pending: this environment cannot connect to the desktop PipeWire socket. On a disposable take, open OmaControl, confirm the warning with Sourceroll hidden, change a display setting, and verify any detected failure creates a new screen segment while audio/camera continue. Confirm recovery uses the previously approved source, then verify Stop during reconnection and inspect timeline gaps. Do not use an important recording for this check.
- Monitoring checks graph errors and output progress, not visual changes. A silent producer that keeps emitting duplicate frames through an otherwise healthy graph is not detectable by this implementation.
