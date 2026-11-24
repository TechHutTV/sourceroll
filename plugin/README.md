# Multitake bar widget

An Omarchy `bar-widget` for the separately installed Multitake desktop recorder.
`BarWidget.qml` owns the bar button and IPC clients; `Panel.qml` uses Omarchy's
`Panel`, `KeyboardPanel`, and `PanelKeyCatcher` for the popup lifecycle.
The popup is part of the bar widget, so it does not declare a separate panel kind.

## Install and use

Install the Multitake Arch package first, then run `bin/install-plugin` from the
application repository. The installer validates this folder, backs up existing
widget files and bar configuration, preserves placement during the Source Record
rename, enables `local.multitake`, and restarts the existing Omarchy shell.
It does not launch another Quickshell instance.

Click for the camera preview, audio meters, source switches, and record/stop.
Middle-click starts/stops a take; right-click opens the app. Escape closes the
popup. Omarchy shell summon/hide and neighbouring panel switching are supported.

```sh
omarchy-shell shell summon local.multitake '{}'
omarchy-shell shell hide local.multitake
omarchy bar move local.multitake --section right
```

## Dependencies and permissions

Requires Omarchy/Quattro and `multitake` on PATH. Multitake requires Qt 6,
FFmpeg/ffprobe, PulseAudio-compatible audio (including PipeWire-Pulse), GPU Screen
Recorder, and the Wayland desktop portal. The native portal handles screen/window
sharing permission; this plugin does not replace its picker.

The widget uses argument-array CLI calls and the user-only local Multitake socket.
It polls recording status, subscribes to audio peaks, and subscribes to camera
frames only while the popup is open. Preview and recording share the camera.
Opening the popup starts idle monitoring; recording starts only on an explicit
record action. Closing the popup releases its preview subscription. There are no
network requests, downloads, remote builds, separate services, or privileged
commands in the widget. Package installation uses the terminal's private sudo
prompt; widget installation needs no root privileges. Theme and language follow
Omarchy live, with English and Danish catalogs and English fallback.

## Validate and remove

From the application repository, run `bin/check-plugin` and `bin/test`.
The former validates the manifest/layout and lints both QML files against the
installed shell. Quattro's dynamic loader and theme properties are reported as
informational by qmllint; native shell lifecycle checks are also required.

```sh
omarchy plugin disable local.multitake
omarchy plugin remove local.multitake
```

Removing the widget keeps the app, recordings, settings, and migration backups.
The `local.multitake` ID is for local development. Marketplace publication needs
a permanent namespaced ID and a public plugin repository with this folder's
manifest, README, license, QML, model, and catalogs at its root.
