---
name: source-record
description: Control Source Record, the independent FFmpeg webcam recorder with Omarchy bar integration.
---

Use `source-record status --json`, `devices --json`, and `settings --json` for inspection.
Use `source-record open` for preview and settings. Use `start --json`, `stop --json`, or `toggle --json` only when recording is requested. Wait for status `recording` before claiming capture started; inspect `error` and the saved output using ffprobe afterward.
`source-record configure --camera PATH --mode MODE --audio SOURCE --directory ABSOLUTE_PATH --encoding original|h264 --json` changes settings while idle. Read supported IDs from devices; never silently substitute a missing microphone. Original MJPEG/H.264 is stream-copied to MKV; audio is PCM. OBS remains independent. Closing the app window hides it and preserves recording. Never stop an unrelated FFmpeg process.
