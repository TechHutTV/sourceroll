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
