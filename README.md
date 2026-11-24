![Sourceroll](docs/screenshot.png)

# Sourceroll

A focused recorder for Omarchy. Select a screen or window, camera, microphone, and desktop audio, then record the enabled sources together. Each source gets its own file for editing afterward. The app and bar widget provide camera preview, audio meters, source switches, recording status, and one start/stop control.

## Record a take

Search **Sourceroll** in the app launcher (Super + Space). Enable the sources you need and select their devices. Enabling the Screen source opens the native Wayland screen/window chooser and starts a live preview before recording. Click an empty Screen preview pane to choose a source directly, including in Both view. Use **Choose screen/window** to change an existing selection. Recording asks the portal to restore that selection; the portal can request approval again if restoration is unavailable or permission has changed. Direct monitor capture is also available, but depends on GPU and display wiring. Use the chooser if a monitor cannot be captured directly.

The system chooser still uses snapshots. Sourceroll leaves the shared picker unchanged and shows a live screen preview after you approve a source. Choosing or previewing a screen does not save a recording.

The app shows preparation, screen-selection, recording, stopping, and file-validation states. A take is reported as saved only after its files pass validation. Settings are locked during capture and finalization. Turning a source off excludes it from the take; it does not mute or modify the system device.

A take folder contains the selected outputs:

```text
take-YYYY-MM-DD_HH-mm-ss-mmm/
  screen.mkv
  camera.mkv
  microphone.wav
  desktop.wav
  session.json
  timeline.otio
```

Only enabled sources produce files. Files have `.partial` in their names until validated. Screen and camera video remain separate. Microphone and desktop audio are separate uncompressed PCM WAV files, with RF64 support for long takes.

Original camera mode preserves supported MJPEG/H.264 streams without re-encoding. H.264 mode produces smaller files; raw YUYV cameras also use H.264. Available camera resolutions and rates come from the device. Screen capture uses GPU Screen Recorder with H.264 at the source's native resolution, 30/60 fps controls, and High / Very high / Ultra quality presets. Very high is the default; higher quality generally uses more storage. CPU encoding is a fallback when hardware encoding is unavailable. Original MJPEG can produce large files; watch disk space.

## Timing and editing

Source preparation and permission selection take different amounts of time. Sourceroll records the camera and audio device timestamps and the screen recorder's first-frame timestamp, maps them to a common monotonic clock, and saves each source's start offset in `session.json`. Files use local media timestamps, and `timeline.otio` places validated clips on separate tracks with the measured offsets.

OpenTimelineIO support varies by editor. The JSON manifest remains available for editors or scripts that do not import OTIO. These offsets describe capture timing; camera processing and microphone DSP can introduce physical latency that needs calibration or adjustment in an editor. Use a visible/audible synchronization cue when frame-accurate alignment matters.

A system clock jump is reported as a timing warning. If timing metadata is missing after an interruption, files are preserved and manual alignment is requested rather than inventing offsets.

## Preview and widget

The app offers **Screen / Camera / Both** monitoring views; the bar popup switches between **Screen / Camera**. These monitor controls do not change which sources are saved. Turning on the Screen source opens the picker; the separate **Choose screen/window** control changes the selection while idle. Click the bar icon for previews, source switches, audio meters, and recording controls. Middle-click toggles recording; right-click opens the full app. The app and widget share one camera capture and a 640-pixel-wide preview at up to 30 fps. The screen preview is up to 15 fps at 640 pixels wide. During recording it decodes a copy of the exact encoded screen stream, including window-only captures; it does not capture a different display or reread a growing recording file. The saved file is the primary output, and a separate bounded FIFO branch can drop preview packets without stopping file capture. Preview scaling does not resize the saved video. The widget retains the previous frame while the next one loads.

Closing the main window hides it and keeps an active take recording. When idle, closing the last preview viewer releases capture devices. Screen/window permission is requested when you choose a source, or when starting screen capture without a restorable selection. A private, temporary portal restore token belongs only to this app session; it is not written into settings or recordings. Hiding the last idle viewer releases screen capture, and reopening can restore the approved source. No preview video is saved. Preview decoders retry after failure and report stale/error states; a static image is not treated as a freeze as long as frames keep arriving. The microphone and desktop meters show recent peak dBFS, update independently of general status, and do not play input audio through the speakers. Keep peaks below 0 dBFS.

The app follows Omarchy's palette and control tokens live, including light/dark colors, accent, borders, font size, and Hyprland rounding. Switching themes does not restart recording.

| Shortcut | Action |
| --- | --- |
| Space | Record enabled sources / stop and save |
| Esc | Hide to bar; recording continues |
| Q | Confirm quit; stop and save active sources |
| ? | Show shortcuts |

## Failure handling and recovery

If an enabled source exits or the camera stops delivering frames, all sources are asked to stop. Low disk space also stops the take. Workers are tracked individually; unrelated recorder processes are never killed. Partial files and metadata are preserved when a source fails. Source files are validated with ffprobe before being renamed, and the take reports incomplete if validation fails.

After an interrupted run, choose **Recover interrupted take** in the app or run `sourceroll recover --json`. Recovery checks the most recent incomplete take in the configured output folder, preserves unreadable partial files, and rebuilds timing/timeline data when enough metadata exists. It never overwrites a final file. A normal quit waits for capture finalization; after a crash, parent-death signals ask workers to finalize and recovery handles the remaining metadata.

## Build and install

Requires Arch/Omarchy, Qt 6 base/declarative/Wayland/SVG, qmake6, make, a C++ compiler, Python 3, FFmpeg/ffprobe, pactl, GPU Screen Recorder, and a working Wayland screen-cast portal.

```sh
bin/build
bin/test
bin/check-plugin
bin/install
```

`bin/install` builds an Arch package and installs the user widget. Password entry uses the native terminal prompt. Plugin installation backs up previous files and bar configuration, then restarts the shell to clear cached QML. The recorder runs separately, so a shell restart keeps active takes recording. No files under `/usr/share/omarchy` are edited.

Settings are saved atomically in `~/.config/sourceroll/settings.json`. New installations default to `~/Videos/Sourceroll`. Existing Source Record settings are imported once when no Sourceroll settings exist; their chosen recording folder is preserved. Widget migration retains bar placement and backs up the previous widget. Existing recordings are not moved or deleted.

There is one resident app per user, protected by a lock and a user-only local socket. The widget starts it in the background as needed. There is no network service, account, telemetry, or automatic recording at login.

The user plugin lives in `plugin/`, with its own [installation, dependency, and removal notes](plugin/README.md). It uses Omarchy's bar-widget contract and standard panel lifecycle. `local.sourceroll` is a local development ID. Marketplace submission would require a permanent namespaced ID and a public plugin repository with `manifest.json` at its root; this app repository is not currently a marketplace submission.

## Command line

```sh
sourceroll open
sourceroll devices --json
sourceroll settings --json
sourceroll configure --screen-enabled true --camera-enabled true --microphone-enabled true --desktop-enabled false --json
sourceroll configure --screen-target portal --screen-fps 60 --screen-quality very_high --json
sourceroll choose-screen --json
sourceroll status --json
sourceroll start --json
sourceroll stop --json
sourceroll recover --json
sourceroll check --json
sourceroll skill install
```

Use device and mode IDs returned by `devices`. `start` acknowledges preparation; wait for `state: recording` before assuming every source is ready. `stop` begins coordinated stopping and validation; wait for `recording: false` and inspect `state`, `error`, and `sources` for the result. Screen selection may require an interactive native picker. CLI inspection needs no display client, but capture needs a running desktop session.

## Remove

Stop the take and quit the app with Q, then:

```sh
omarchy plugin disable local.sourceroll
sudo pacman -R sourceroll
```

Remove `~/.config/omarchy/plugins/local.sourceroll` if no longer wanted. Settings, recording folders, and migration backups are kept.

## Current scope

One screen/window, one V4L2 camera, one microphone, and one desktop audio source can be selected per take. Discrete MJPEG, H.264, and YUYV camera modes are supported. Arbitrary camera counts, live compositing, streaming, transitions, and headphone monitoring are outside this version.

MIT licensed. Theme and language integration follow Omarchy's Omacut/Monologue templates, with attribution in LICENSE. English and Danish catalogs are included, with live theme/language changes and English fallback.
