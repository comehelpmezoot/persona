# Persona Shell for Windows

A native Windows port of [Yujonpradhananga/Persona-Quickshell](https://github.com/Yujonpradhananga/Persona-Quickshell):
the Persona 3 Reload desktop - animated parallax wallpaper with rain, ripples and an audio
visualizer, the tilted music capsule, the clock, the left-edge drawer (calendar, system stats,
screen filters, power menu), volume / brightness pop-ups and the app launcher.

It is **not an emulation layer**. Quickshell only exists for Linux (Wayland/X11), so the Linux-specific
parts were replaced with Windows APIs and everything visual - the QML, the shaders, the animations - is
carried over. Qt Quick runs natively on Windows (Direct3D 11).

> **Status - please read.** The audio spectrum analyzer, hotkey parser and colour-filter matrices are
> unit-tested (`ctest`), and the QML is statically checked (`tools/check_qml.py`: bracket balance, every
> asset path exists, every type/native member used is defined). The Windows-specific C++ (desktop
> attachment, WASAPI, WMI, SMTC, Wi-Fi, Bluetooth, Apps folder, Magnification) was written against the
> documented APIs but **could not be compiled or run** where this was authored (no Windows, no Qt there).
> Expect to fix a compile error or two on first build, and to tune a few things on your machine.
> Everything writes to `%APPDATA%\PersonaShell\log.txt` - send me that file and the compiler output and
> the remaining issues are quick to fix.

## Requirements

* Windows 10 (2004+) or Windows 11, a Direct3D 11 GPU
* **Qt 6.6 or newer** (6.8 LTS recommended), the **MSVC 2022 64-bit** kit, with these components ticked in the
  Qt installer: *Qt Multimedia* and *Qt Shader Tools* (Qt SVG, Qt Quick Controls and Qt Quick Effects are part of the base install)
* Visual Studio 2022 (or Build Tools) with the *Desktop development with C++* workload and a Windows 10/11 SDK
* CMake 3.21+
* The **`Assets` folder from the original repo** (not duplicated here - it is 60 MB of images and video)

## Build

Put this folder next to the original repo so it finds the assets on its own:

```
Projects\
  Persona-Quickshell-main\Assets\...   <- original repo
  Persona-Windows\                      <- this folder
```

Then, in PowerShell:

```powershell
cd Persona-Windows
.\build.ps1 -QtDir C:\Qt\6.8.2\msvc2022_64 -Run
```

Other places for the assets: `-Assets D:\somewhere\Assets`, an `Assets` folder next to `PersonaShell.exe`, or the
`PERSONA_ASSETS` environment variable. If the C++/WinRT media part fails to compile on your toolchain:
`.\build.ps1 -NoSmtc` (everything else still works, the music capsule just stays on "No Media").

Manual build: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<Qt>` then
`cmake --build build --config Release` and `cmake --build build --config Release --target deploy`.

## No install: let GitHub build the exe

Don't want Visual Studio and Qt on your PC? Put this folder in a (free) GitHub repository and open
**Actions -> Build Windows exe -> Run workflow**. About 15 minutes later the run page has a **PersonaShell-Windows**
download: unzip it and double-click `PersonaShell.exe` (the original theme's `Assets` are bundled in). The workflow is
`.github/workflows/build.yml`.

## Using it

| What | How |
|---|---|
| App launcher | **Alt+Space** (global), the tray icon, or `PersonaShell.exe --toggle-launcher` |
| Calendar / Stats / Shaders / Power | Move the mouse to the **left screen edge**, the ball slides out; click it for the menu |
| Music capsule | Bottom-left. Click the disc = play/pause, arrows = previous/next. Works with whatever Windows calls the current media session (Spotify, browser, VLC...) |
| Quit / restart / autostart / log | Tray icon (the `:3` badge) |
| Test without touching your desktop | `PersonaShell.exe --windowed` (wallpaper in a normal 1280x720 window) |

Command line: `--toggle-launcher`, `--open-launcher`, `--close-launcher`, `--quit`, `--windowed`.

`%APPDATA%\PersonaShell\config.json` (created on first run):

```json
{ "hotkey": "Alt+Space", "wallpaper": true, "cava": true, "capsule": true,
  "clock": true, "drawer": true, "assetRoot": "" }
```

Hotkeys look like `Alt+Space`, `Ctrl+Alt+K`, `Win+F2` (at least one modifier; `Win+R` etc. are reserved by Windows).

### Fonts

The original used Linux fonts. Each role falls back to something that ships with Windows (Microsoft YaHei, Bahnschrift,
Segoe UI, Consolas). For the exact look, drop **Montserrat**, **Bebas Neue** and **ProggyClean** `.ttf`/`.otf` files into a
`fonts` folder next to `PersonaShell.exe` (or inside `Assets\fonts`) - they are loaded automatically. All icons are drawn
as vectors, so no icon font is required.

## What replaced what

| Linux (original) | Windows (this port) |
|---|---|
| Quickshell `PanelWindow` / layer-shell / `Variants` per monitor | `PanelWindow.qml`: frameless Qt windows with the same anchors/margins/size semantics; Win32 z-order "desktop / top / overlay" layers |
| Wallpaper surface (`WlrLayer.Background`) | Window re-parented into the desktop's `WorkerW`, i.e. **behind the desktop icons**. Both known desktop layouts are handled (Windows 10 / early Windows 11, and 24H2+); if the attach fails it falls back to a desktop-level window and logs why |
| Mouse parallax from hover events | Cursor polled at 60 Hz (the wallpaper sits behind the icons and gets no mouse input) |
| `Qt6-Cava-plugin` (PipeWire/cava) | `CavaMonitor`: WASAPI loopback capture + FFT, same QML interface (`bars`, `active`, `values`) |
| MPRIS media player | Windows media sessions (SMTC) incl. album art, prev/next/play-pause |
| PipeWire volume | WASAPI default-device endpoint volume + mute |
| `/sys/class/backlight` | WMI `WmiMonitorBrightness` (laptop panels; external monitors expose nothing, so no OSD there) |
| UPower battery | `GetSystemPowerStatus` (desktops show "AC") |
| `/proc/stat`, `/proc/meminfo`, `df`, `who` | `GetSystemTimes`, `GlobalMemoryStatusEx`, `GetDiskFreeSpaceEx`, WTS sessions |
| `nmcli` Wi-Fi list | WLAN API (SSID, signal, connected) |
| BlueZ | Win32 Bluetooth API (paired / remembered / connected devices) |
| `DesktopEntries` launcher | The shell's *Apps* folder: classic **and** Store apps, real icons, launches via `shell:AppsFolder` |
| `qs ipc call searchapp toggle` + compositor keybind | `RegisterHotKey` + `PersonaShell.exe --toggle-launcher` over a named pipe |
| `loginctl poweroff / reboot / terminate-session` | `shutdown /s`, `/r`, `/l` |
| Hyprland `screen_shader` (blue light / grey / invert) | Windows Magnification API full-screen colour matrices |
| `.qsb` shaders compiled for Linux | Rebuilt from the GLSL sources for Direct3D (`qt_add_shaders`) |

## Differences you will notice

* **Greyscale filter**: the original was an e-ink shader with paper grain and dithering. A colour matrix cannot do
  grain, so you get the same grey-to-"paper" tone mapping without the noise. Blue light and inversion are identical.
* **Launcher** closes when it loses focus (Windows has no exclusive keyboard focus), is limited to the monitor your mouse
  is on, sorts prefix matches first, and fixes a bug where a query longer than an app name still matched it.
* **Widgets** (clock, capsule) sit just above the wallpaper and below normal windows, *above* the desktop icons like Rainmeter
  skins do, and respect the taskbar. The clock is click-through.
* The wallpaper **pauses while a fullscreen game/video is running**, so it costs no GPU time behind a game.
* Removed: the stray debug number the original drew in the top-left of the wallpaper, the unused Hyprland workspace
  service, a never-triggered transition in the drawer. The "today" ring in the calendar keeps the position the original
  actually rendered (its anchor expressions were invalid and ignored by QML).
* Bluetooth lists classic paired devices; BLE-only peripherals may not appear (Win32 API limitation).

## Troubleshooting

* **Nothing starts / window flashes**: open `%APPDATA%\PersonaShell\log.txt`.
* **Black or missing wallpaper, or it covers the icons**: run `--windowed` to check the rendering itself; the log says whether
  the desktop attach worked ("Wallpaper attached ..."). Rendering problems: try `set QSG_RHI_BACKEND=d3d12` (or `opengl`).
* **Shaders look wrong / first start is slow**: the HLSL is compiled by Windows on first use. Build once with
  `-DPERSONA_PRECOMPILE_SHADERS=ON` (needs `fxc.exe` on PATH, i.e. a VS Developer prompt) to bake it in.
* **Launcher hotkey does nothing**: another program owns it (the log says so). Change `"hotkey"`.
* **Compile error in `media.cpp`**: build with `-NoSmtc`.
* **Videos don't play**: install the *Qt Multimedia* component and re-run the `deploy` target (it ships the FFmpeg backend).
* **Windows' own volume/brightness pop-up shows too**: that is the system's; it cannot be disabled from an app.

## Layout

```
src/        C++: shell.cpp (windows, desktop attach, hotkey), services.cpp, wmi_brightness.cpp,
            connectivity.cpp, cava.cpp + spectrum.cpp, media.cpp, apps.cpp, main.cpp
qml/        the ported UI (flat module; PanelWindow.qml emulates Quickshell's PanelWindow)
shaders/    GLSL sources, compiled to Direct3D/OpenGL/Vulkan at build time
tests/      logic_test.cpp (spectrum, hotkeys, colour matrices)
tools/      check_qml.py (static QML checks)
```

Original theme, art and QML by Yujonpradhananga; Persona is a trademark of Atlus. This port only adapts the code.
