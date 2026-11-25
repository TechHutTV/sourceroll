---
name: sourceroll
description: Control Source Roll sessions containing separate screen, camera, microphone, and desktop-audio files.
---

Inspect with `sourceroll choose-screen --json  # Opens the native picker and previews without saving video.
sourceroll status --json`, `devices --json`, and `settings --json`. Open settings with `sourceroll open`. Configure source switches with `configure --screen-enabled true --camera-enabled true --microphone-enabled true --desktop-enabled false --json`; use IDs from devices for the selected camera, mode, audio inputs, and screen target.

Only start recording when requested. `sourceroll start --json` acknowledges preparation, not readiness: wait for state `recording`. A native portal picker may need the user to select a screen/window. `stop --json` begins coordinated stop and file checks; wait for recording false, inspect state/error and each source. Session files, offsets and the OTIO timeline are in the reported output folder. Never claim incomplete files are successful recordings. `recover --json` checks a previous incomplete take without recapturing.

Closing the main window hides it and preserves recording. The app and widget share capture; never kill unrelated FFmpeg or GPU Screen Recorder processes. Do not interrupt an active take for upgrades or migration. Existing Source Record preferences import once into Source Roll without moving recordings.
