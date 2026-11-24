![Source Record](docs/screenshot.png)

# Source Record

A small camera recorder for Omarchy, powered by FFmpeg. Record a camera and selected microphone to timestamped MKV files, with a live preview, microphone peak meter, elapsed time, and file size. A bar widget provides live preview and recording controls.

Original mode preserves supported MJPEG and H.264 camera streams without re-encoding. The smaller H.264 option uses CRF 18; raw YUYV modes also use H.264. Audio is saved as uncompressed PCM. Available capture resolutions and frame rates come from the selected camera. The app and widget share a 640-pixel-wide preview at up to 30 fps; preview scaling does not resize the saved recording.

File size depends on the camera format, scene, and encoding settings. Original MJPEG can produce large files; choose H.264 for smaller recordings.

## Use

Search **Source Record** in the app launcher (Super + Space). Select a camera, supported resolution/frame rate, microphone (or No audio), encoding and output folder. Review the selected camera mode and audio source before recording. A configured missing device causes a visible error instead of silently switching inputs. No sound is played through the speakers.

Click the bar camera icon for a live camera preview, microphone meter, and start/stop controls. The popup uses the recorder’s existing 30 fps preview feed; opening it never starts a second camera capture. With no recording active and the app hidden, closing the popup releases the camera. Middle-click toggles recording; right-click opens the app. Closing or hiding the window keeps an active recording running. When idle, hiding releases the camera and microphone. The bar shows elapsed time and turns urgent while recording. Other applications may need to release the camera before capture can start.

The meter shows recent peak dBFS, not loudness. It updates on a separate 40 ms timer with a fast attack and brief release, and the bar receives readings over a persistent user-only local socket. PulseAudio capture uses a small requested buffer. Keep speech below 0 dBFS to avoid clipping. MKV tolerates interruptions better than a conventional MP4, but always stop normally to finalize the index. Do not unplug the camera during a take.

The full app follows Omarchy's `colors.toml` and `shell.toml` live: background, foreground, secondary text, accent, error color, light/dark mode, control fills and borders, font size, and control height. Flat controls use the system monospace font and Hyprland rounding. Switching themes replaces the palette without restarting or interrupting recording.

| Shortcut | Action |
| --- | --- |
| Space | Start / stop and save |
| Esc | Hide to bar; recording continues |
| Q | Quit after confirmation; save active recording |
| ? | Show shortcuts |

## Build and install

Requires Arch/Omarchy, Qt 6 base/declarative/Wayland/SVG, qmake6, make, a C++ compiler, FFmpeg and pactl. Video discovery uses the Linux V4L2 API.

```sh
bin/build
bin/test
bin/install
```

`bin/install` builds an Arch package and uses normal sudo authentication, then installs/enables the user-owned plugin. Nothing under /usr/share/omarchy is edited. The source of the bar plugin lives in `plugin/`; run `bin/install-plugin` after editing it. Installation backs up an existing plugin directory before replacing it and restarts the shell to clear cached QML. The recorder runs separately and keeps active recordings running. The bar configuration is managed through `omarchy plugin enable`.

Settings are saved atomically in `~/.config/source-record/settings.json`. Recordings default to the configured Videos folder under `Source Record`. One per-user resident app owns the FFmpeg child and listens on a user-only local socket; a lock prevents duplicate recorders. There is no network service, account, telemetry, or automatic recording at login.

```sh
source-record open
source-record devices --json
source-record settings --json
source-record status --json
source-record start --json
source-record stop --json
source-record configure --audio none --encoding h264 --json
source-record check --json
source-record skill install
```

Use the device and mode IDs returned by `devices`. A start response means the process is starting; check status for `recording` or `error`. Stop waits for FFmpeg to finalize and reports its saved output. Settings cannot change while recording. CLI commands do not require a display client; starting the resident recorder needs a running desktop session. The bundled agent skill can be installed explicitly with the command above.

## Remove

Stop the camera recording first, quit the app with Q, then:

```sh
omarchy plugin disable local.source-record
sudo pacman -R source-record
```

Remove `~/.config/omarchy/plugins/local.source-record` if no longer wanted. Settings and recordings are kept. No existing screen-recording shortcuts are changed.

## Scope

Discrete V4L2 MJPEG, H.264 and YUYV camera modes are supported. Other formats and stepwise-only modes are not listed. The original stream option preserves the camera's USB output; it cannot recover detail the camera discarded internally. Monitoring is visual and metered; headphone monitoring and synchronization with other recorders are outside this version.

MIT licensed. Theme and language integration follow Omarchy's Omacut/Monologue templates, with attribution in LICENSE. English and Danish catalogs are included with live language/theme switching and English fallback.
