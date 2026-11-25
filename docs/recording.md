# Recording guide

## Record a take

Search **Source Roll** in the app launcher (Super + Space). Switch on the sources you need and select their devices. Switching on a screen that uses the picker opens the native Wayland screen/window chooser and starts a live preview before recording. Click the empty preview pane of such a screen to choose a source directly. Use **Choose screen/window** to change an existing selection. Recording asks the portal to restore that selection; the portal can request approval again if restoration is unavailable or permission has changed. A screen source can instead capture a monitor directly: pick the monitor under **Screen or window**. It previews and records without a prompt, but depends on GPU and display wiring. Use the chooser if a monitor cannot be captured directly.

The system chooser still uses snapshots. Source Roll leaves the shared picker unchanged and shows a live screen preview after you approve a source. Choosing or previewing a screen does not save a recording.

The app shows preparation, screen-selection, recording, stopping, and file-validation states. A take is reported as saved only after its files pass validation. Settings are locked during capture and finalization. Turning a source off excludes it from the take; it does not mute or modify the system device.

A take folder contains the selected outputs:

```text
take-YYYY-MM-DD_HH-mm-ss-mmm/
  screen.mkv
  left-monitor.mkv
  camera.mkv
  microphone.wav
  desktop.wav
  session.json
  timeline.otio
```

Only sources that are switched on produce files, and every screen and camera has its own. Files have `.partial` in their names until validated. Microphone and desktop audio are separate uncompressed PCM WAV files, with RF64 support for long takes.

Original camera mode preserves supported MJPEG/H.264 streams without re-encoding. H.264 mode produces smaller files; raw YUYV cameras also use H.264. Available camera resolutions and rates come from the device. Screen capture uses GPU Screen Recorder with H.264 at the source's native resolution, 30/60 fps controls, and High / Very high / Ultra quality presets. Very high is the default; higher quality generally uses more storage. CPU encoding is a fallback when hardware encoding is unavailable. Original MJPEG can produce large files; watch disk space. Frame rate and quality are set per screen; mode and storage per camera.

## Several screens and cameras

The app starts with one screen and one camera. **Add screen** and **Add camera** under Sources add more, up to eight video sources in total, and **Remove** deletes one. Each source has its own switch, preview pane, and capture settings, and records to its own file and timeline track.

Type a name in the field at the top of a source's settings. The name labels its switch and preview, its track in the timeline, and its file: "Left monitor" is saved as `left-monitor.mkv`. Unnamed sources are called Screen, Camera, Screen 2, and so on, and are saved as `screen.mkv`, `camera.mkv`, `screen-2.mkv`. A name already in use gets a number added, and a name cannot take the place of `microphone.wav` or `desktop.wav`. Names are locked while a take is running, like the other source settings; a new name applies to later takes only.

Screens that use the picker each keep their own approved selection. When a take starts they are started one at a time, so only one permission prompt can be open; the other sources start once every screen is delivering frames. Two camera sources cannot use the same device. **Add camera** takes the first connected camera that no other source uses, including one plugged in a moment ago; when none is free it says so instead of adding an empty source. Camera lists are brought up to date each time one is opened, and **Refresh devices** rescans everything.

Every extra screen is another full-resolution encode and file, so GPU load and disk use grow with each one. Two USB cameras at a high resolution can need more bandwidth than one USB controller provides; if the second does not start, try a lower mode or another port.

## Timing and editing

Source preparation and permission selection take different amounts of time. Source Roll records the camera and audio device timestamps and the screen recorder's first-frame timestamp, maps them to a common monotonic clock, and saves each source's start offset in `session.json`. Files use local media timestamps, and `timeline.otio` places validated clips on separate tracks with the measured offsets.

OpenTimelineIO support varies by editor. The JSON manifest remains available for editors or scripts that do not import OTIO. These offsets describe capture timing; camera processing and microphone DSP can introduce physical latency that needs calibration or adjustment in an editor. Use a visible/audible synchronization cue when frame-accurate alignment matters.

A system clock jump is reported as a timing warning. If timing metadata is missing after an interruption, files are preserved and manual alignment is requested rather than inventing offsets.

## Preview and widget

The app shows a preview pane for every source that is switched on. **Screens / Cameras / All** narrow them down by kind, and **Stack / Side by side** sets their layout; the panes scroll when more are stacked than fit. The bar popup shows one source at a time, with a tab for each when more than one is switched on. These monitor controls do not change which sources are saved. Switching on a screen that uses the picker opens it; the separate **Choose screen/window** control changes the selection while idle. Click the bar icon for previews, source switches, audio meters, and recording controls. Middle-click toggles recording; right-click opens the full app. The app and widget share each camera's capture and a 640-pixel-wide preview at up to 30 fps. Each screen preview is up to 15 fps at 640 pixels wide. During recording it decodes a copy of the exact encoded screen stream, including window-only captures; it does not capture a different display or reread a growing recording file. The saved file is the primary output, and a separate bounded FIFO branch can drop preview packets without stopping file capture. Preview scaling does not resize the saved video. The widget retains the previous frame while the next one loads.

Closing the main window hides it and keeps an active take recording. When idle, closing the last preview viewer releases capture devices. Screen/window permission is requested when you choose a source, or when starting screen capture without a restorable selection. A private, temporary portal restore token belongs only to this app session; it is not written into settings or recordings. Hiding the last idle viewer releases screen capture, and reopening can restore the approved source; a monitor captured directly previews again by itself. No preview video is saved. Preview decoders retry after failure and report stale/error states; a static image is not treated as a freeze as long as frames keep arriving. The microphone and desktop meters show recent peak dBFS, update independently of general status, and do not play input audio through the speakers. Keep peaks below 0 dBFS.

The app follows Omarchy's palette and control tokens live, including light/dark colors, accent, borders, font size, and Hyprland rounding. Switching themes does not restart recording.

| Shortcut | Action |
| --- | --- |
| Space | Record enabled sources / stop and save |
| Esc | Hide to bar; recording continues |
| Q | Confirm quit; stop and save active sources |
| ? | Show shortcuts |

## Failure handling and recovery

Screen capture has a separate recovery path. For portal capture, Source Roll runs `pw-dump` to watch only PipeWire clients/nodes/links belonging to its recorder process. A link/node error (including buffer negotiation failures), a lost connection, a recorder exit after startup, or ten seconds without either file growth or encoded output triggers recovery. Image similarity is never used: an unchanged desktop alone does not trigger a restart. A pipeline that keeps writing duplicate frames while reporting a healthy connection cannot be distinguished from a still desktop by these checks.

Recovery finalizes the affected screen file and reconnects into `screen-002.mkv`, then `screen-003.mkv` and `screen-004.mkv` if necessary; a named screen keeps its name, as in `left-monitor-002.mkv`. The other sources keep running, including any other screens. Each screen has at most three retries per take, with a twenty-second first-frame deadline per replacement. Portal restoration may require permission again. Stop cancels any pending retry; exhausting retries stops and preserves the whole take with an error. Failed segments retain their partial files.

The app, bar popup, and desktop notifications report interruptions and recovery with a recording-relative `HH:MM:SS.mmm` timecode, for example `00:11:29.123`. Recovery notices name the screen and retain that timecode, and the final saved warning lists every interruption time, with the screen's name when the take has more than one. These mark detection; inspect a few seconds before each marker too, because detection can follow the first missed frame. `session.json` retains `screenInterruptions` with monotonic failure/resumption timestamps, `recordingOffsetSeconds`, `recordingTimecode`, UTC `detectedAtUtc`, and `hasScreenGaps` on the session; the timeline puts each screen's validated segments on that screen's track with measured gaps. Gaps represent missing footage, not footage recovered afterward. Check interrupted segments for a frozen tail before editing. Validation checks readability, not visual freshness. Saved takes with interruptions retain a warning.

Opening OmaControl during screen recording shows a warning in the app and bar popup and sends a desktop notification, even when Source Roll is hidden. This checks OmaControl's layer surface, so it covers the whole settings window, not just its Displays tab. Source Roll does not change OmaControl or prevent display changes. If PipeWire monitoring is unavailable or the recorder cannot be identified, a warning explicitly reports the reduced coverage. Direct-monitor capture retains the process/file-progress checks but has no PipeWire connection monitoring.

If an audio source exits or a camera stops delivering frames, all sources are asked to stop. Low disk space also stops the take. Workers are tracked individually; unrelated recorder processes are never killed. Partial files and metadata are preserved when a source fails. Source files are validated with ffprobe before being renamed, and the take reports incomplete if validation fails.

After an interrupted run, choose **Recover interrupted take** in the app or run `sourceroll recover --json`. Recovery checks the most recent incomplete take in the configured output folder, preserves unreadable partial files, and rebuilds timing/timeline data when enough metadata exists. It never overwrites a final file. A normal quit waits for capture finalization; after a crash, parent-death signals ask workers to finalize and recovery handles the remaining metadata.

## Build and install

Requires Arch/Omarchy, Qt 6 base/declarative/Wayland/SVG, qmake6, make, a C++ compiler, Python 3, FFmpeg/ffprobe, pactl, GPU Screen Recorder, PipeWire (`pw-dump`), and a working Wayland screen-cast portal.

```sh
bin/build
bin/test
bin/check-plugin
bin/install
```

`bin/install` builds an Arch package and installs the user widget. Password entry uses the native terminal prompt. Plugin installation backs up previous files and bar configuration, then restarts the shell to clear cached QML. The recorder runs separately, so a shell restart keeps active takes recording. No files under `/usr/share/omarchy` are edited.

Settings are saved atomically in `~/.config/sourceroll/settings.json`. New installations default to `~/Videos/Sourceroll`. Settings from before screens and cameras were a list are read as one screen and one camera, and are rewritten in the new form the next time a setting changes; a version of the app from before that change would then fall back to its defaults for them. Existing Source Record settings are imported once when no Source Roll settings exist; their chosen recording folder is preserved. Widget migration retains bar placement and backs up the previous widget. Existing recordings are not moved or deleted.

There is one resident app per user, protected by a lock and a user-only local socket. The widget starts it in the background as needed. There is no network service, account, telemetry, or automatic recording at login.

The user plugin lives in `plugin/`, with its own [installation, dependency, and removal notes](../plugin/README.md). It uses Omarchy's bar-widget contract and standard panel lifecycle. `local.sourceroll` is a local development ID. Marketplace submission would require a permanent namespaced ID and a public plugin repository with `manifest.json` at its root; this app repository is not currently a marketplace submission.

## Command line

```sh
sourceroll open
sourceroll devices --json
sourceroll settings --json
sourceroll configure --microphone-enabled true --desktop-enabled false --json
sourceroll source add screen --name "Left monitor" --target DP-3 --json
sourceroll source set screen-2 --fps 60 --quality very_high --enabled true --json
sourceroll source set camera --encoding h264 --json
sourceroll source remove screen-2 --json
sourceroll choose-screen screen --json
sourceroll status --json
sourceroll start --json
sourceroll stop --json
sourceroll recover --json
sourceroll check --json
sourceroll skill install
```

Use device, mode, and monitor IDs returned by `devices`, and the source IDs listed under `videoSources` by `settings` and `status`. `choose-screen` without an ID opens the picker for the first screen. The earlier `configure` options for a single screen and camera (`--screen-enabled`, `--camera-enabled`, `--screen-target`, `--screen-fps`, `--screen-quality`, `--camera`, `--mode`, `--encoding`) still work: they act on the first screen and camera, and switching a kind off switches off every source of it. `start` acknowledges preparation; wait for `state: recording` before assuming every source is ready. `stop` begins coordinated stopping and validation; wait for `recording: false` and inspect `state`, `error`, and `sources` for the result. Screen selection may require an interactive native picker. CLI inspection needs no display client, but capture needs a running desktop session.

## Remove

Stop the take and quit the app with Q, then:

```sh
omarchy plugin disable local.sourceroll
sudo pacman -R sourceroll
```

Remove `~/.config/omarchy/plugins/local.sourceroll` if no longer wanted. Settings, recording folders, and migration backups are kept.

## Current scope

Up to eight screens, windows, and V4L2 cameras, one microphone, and one desktop audio source can be recorded per take. Discrete MJPEG, H.264, and YUYV camera modes are supported. More than one microphone, live compositing, streaming, transitions, and headphone monitoring are outside this version.

Licensed under the GNU General Public License, version 3 or later. Theme and language integration follow Omarchy's Omacut/Monologue templates, which are MIT licensed; their notice is kept in NOTICE. English and Danish catalogs are included, with live theme/language changes and English fallback.
