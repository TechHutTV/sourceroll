---
name: sourceroll
description: Control Source Roll sessions containing separate files for each screen, camera, microphone, and desktop-audio source.
---

Inspect with `sourceroll status --json`, `devices --json`, and `settings --json`. Open settings with `sourceroll open`.

Screens and cameras are a list. `settings` and `status` report them under `videoSources`, each with an `id` such as `screen`, `screen-2` or `camera`. Add one with `source add screen|camera`, change it with `source set ID`, and delete it with `source remove ID`. Options: `--name NAME`, `--enabled true|false`; for screens `--target portal|MONITOR --fps 15-120 --quality high|very_high|ultra`; for cameras `--device PATH --mode MODE --encoding original|h264`. `choose-screen [ID] --json` opens the native picker for a screen and previews it without saving video. Switch audio with `configure --microphone-enabled true --desktop-enabled false --json`. Use IDs from `devices` for cameras, modes, audio inputs, and monitors. The earlier `configure` options (`--screen-enabled`, `--camera-enabled`, `--screen-target`, `--camera`, and so on) still work and act on the first screen and camera.

Only start recording when requested. `sourceroll start --json` acknowledges preparation, not readiness: wait for state `recording`. A native portal picker may need the user to select a screen/window. `stop --json` begins coordinated stop and file checks; wait for recording false, inspect state/error and each source. Session files, offsets and the OTIO timeline are in the reported output folder. Each screen and camera is saved under its name (`left-monitor.mkv`); unnamed ones are `screen.mkv`, `camera.mkv`, `screen-2.mkv`. Never claim incomplete files are successful recordings. `recover --json` checks a previous incomplete take without recapturing.

Closing the main window hides it and preserves recording. The app and widget share capture; never kill unrelated FFmpeg or GPU Screen Recorder processes. Do not interrupt an active take for upgrades or migration. Existing Source Record preferences import once into Source Roll without moving recordings.
