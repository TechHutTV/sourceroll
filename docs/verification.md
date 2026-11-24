# Local verification

Verified on the recording computer on 2026-11-24.

- qmake6 / GCC build succeeds.
- Qt Test: 9 cases pass, including settings persistence, unavailable camera errors, source argument handling, no-audio capture construction, stream copy versus encoding, theme/language watching, actual QML loading, Space start/stop, Escape hide, close versus quit, and English/Danish window rendering without QML warnings.
- Plugin manifest passes `omarchy plugin validate`.
- Plugin JavaScript tests cover malformed/oversized status data, markup boundaries, elapsed formatting, and PlainText usage for status/error/path output.
- Real Canon V4L2 source detected: MJPEG 1920×1080 at 30/1 fps.
- Real Original-mode capture with Shure MV7+ audio inspected using ffprobe: MJPEG 1920×1080 at 30 fps, PCM s16le 48 kHz audio, finalized MKV. Audio present, preview frames received, microphone meter responds.
- Separate H.264/no-audio capture inspected using ffprobe: H.264 1920×1080 at 30 fps, no audio stream, readable finalized MKV after SIGTERM. Resident exits cleanly.
- Cold CLI startup returns promptly while the detached recorder remains available; no inherited stdout pipe holds the caller open.
- Original-mode camera stream measured about 1.2 GB per minute. H.264 sample was about 4.5 MB for 2.53 seconds of this scene; size depends on motion and scene detail.

Re-run software tests with `bin/test`. To explicitly test a connected camera, first quit the app, then run `python3 tests/hardware_smoke.py --record`. It uses a temporary output directory, restores settings, and verifies shutdown. It does not manipulate OBS.

The monitoring preview is 640 pixels wide at 8 fps. This does not resize or drop frames from the separate native camera output. The meter is visual only; no microphone loopback is enabled.
