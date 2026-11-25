# Source Roll bar widget

An Omarchy `bar-widget` for the separately installed Source Roll desktop recorder.
`BarWidget.qml` owns the bar button and IPC clients; `Panel.qml` uses Omarchy's
`Panel`, `KeyboardPanel`, and `PanelKeyCatcher` for the popup lifecycle.
The popup is part of the bar widget, so it does not declare a separate panel kind.

## Install and use

Install the Source Roll Arch package first, then run `sourceroll-install-widget`,
which the package provides, or `bin/install-plugin` from a checkout of the
application repository. Both are the same installer. It validates this folder,
backs up existing widget files and bar configuration, enables `local.sourceroll`,
and restarts the existing Omarchy shell. A widget that is already on the bar,
under this name or its former one, keeps its place; a new one goes on the right.
It does not launch another Quickshell instance. Run it again after updating the
package to refresh the widget.

Click for monitoring, audio meters, source switches, and record/stop. The popup lists every screen and camera set up in the app, under the names given there. With more than one switched on, monitor tabs choose which to watch; the source switches choose what to save. Switching on a screen that uses the picker opens the native picker and a live preview before recording; Choose screen/window changes the selection while idle. Sources are added, named, and removed in the app.

The widget and the app are installed separately, so they need not match. With Source Roll 0.5.0, which has one screen and one camera, the widget shows those two and uses that version's own commands; more than one of each needs 0.6.0 or later.
Middle-click starts/stops a take; right-click opens the app. Escape closes the
popup. Omarchy shell summon/hide and neighbouring panel switching are supported.

```sh
omarchy-shell shell summon local.sourceroll '{}'
omarchy-shell shell hide local.sourceroll
omarchy bar move local.sourceroll --section right
```

## Dependencies and permissions

Requires Omarchy/Quattro and `sourceroll` on PATH. Source Roll requires Qt 6,
FFmpeg/ffprobe, PulseAudio-compatible audio (including PipeWire-Pulse), GPU Screen
Recorder, and the Wayland desktop portal. The native portal handles screen/window
sharing permission; this plugin does not replace its picker.

The widget uses argument-array CLI calls and the user-only local Source Roll socket.
It polls recording status, subscribes to audio peaks, and subscribes to the frames of the
one source on show, only while the popup is open. Preview and recording share the camera. Screen monitoring decodes a disposable copy of the recorded stream. Idle screen selection previews the approved source without saving video; the portal may restore the selection for recording.
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
omarchy plugin disable local.sourceroll
omarchy plugin remove local.sourceroll
```

Removing the widget keeps the app, recordings, settings, and migration backups.
The `local.sourceroll` ID is for local development. Marketplace publication needs
a permanent namespaced ID and a public plugin repository with this folder's
manifest, README, license, QML, model, and catalogs at its root.
