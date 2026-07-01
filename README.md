# HDMI Selector

A small Windows utility to switch which connected display is active — e.g.
flip between your **PC monitor** and your **TV** without unplugging any cables.
Both are plugged into your PC's video outputs; this app changes which one
Windows drives.

* **Primary action — exclusive switch:** click a display and it becomes the
  *only* active screen (the other turns off). This is the 99% use case.
* **Advanced alternatives:** *Extend* (one big desktop across both) and
  *Duplicate* (mirror the same image) live in the **Advanced** menu so they
  don't clutter the main view.
* **System-tray control:** a tray icon with a right-click menu lets you switch
  displays (and reach the Advanced modes) without opening the window. Closing
  the window hides to the tray; quit from **File → Exit** or the tray menu.
* **Live control center:** the app polls the display configuration and
  refreshes automatically when a display is plugged in/out or changed
  elsewhere, so the view always reflects reality.
* **Light / dark theme:** **View → Theme** offers System (follows the OS
  light/dark setting and updates live), Light, or Dark. The choice is
  remembered; `HDMI_THEME=system|light|dark` overrides it for one run.
* **LAN REST API:** the same actions are exposed over HTTP so you can switch
  from your phone, another PC, or a home-automation system.

## Run at startup (not a service)

Enable **File → "Start with Windows"** (also in the tray menu) to launch the app
automatically at login, straight to the tray.

This is deliberately a **login-time app, not a Windows service.** Services run in
the non-interactive Session 0, where they can neither change the desktop's
display configuration (`SetDisplayConfig` targets the caller's session) nor show
a tray icon — so a service could host the REST API but couldn't actually switch
anything. Auto-start uses the per-user `Run` registry key, so **no admin rights**
are needed and it's fully reversible from the same toggle.

## Is this safe? (anti-cheat / performance)

* **Anti-cheat:** yes, safe. The app only calls standard Windows display APIs
  (`SetDisplayConfig` / `QueryDisplayConfig`) and runs a local HTTP server. It
  does not touch, read, or inject into any game process, so anti-cheats (EAC,
  BattlEye, Vanguard, …) have nothing to flag.
* **Performance:** negligible. A switch is a single OS call; the REST listener
  idles at ~0% CPU and a few MB of RAM. There is no polling of games.
* **Networking:** the REST server binds to `127.0.0.1` (local only) by default.
  To use it over the LAN, bind to `0.0.0.0` **and set a token** (see below) so
  nothing else on the network can flip your screens.

## Architecture

```
GUI (wxWidgets) ─┐
                 ├─> DisplayManager ──> IDisplayBackend ─┬─> WindowsBackend (real, CCD API)
REST server ─────┘   (thread-safe,                       └─> MockBackend    (tests / non-Windows)
                      validation)
```

Display control is hidden behind `IDisplayBackend`, so the core logic and the
REST API can be built and tested on any platform using the in-memory
`MockBackend`; the real switching (`WindowsBackend`) is compiled only on
Windows.

## Building

### Prerequisites
* A C++17 compiler and **CMake ≥ 3.16**.
* **Windows GUI only:** [wxWidgets](https://www.wxwidgets.org/) (core + base).
  If wxWidgets isn't found, everything except `hdmi_gui` still builds.
* Vendored, header-only dependencies (already in `third_party/`): cpp-httplib
  and nlohmann/json — nothing to install.

### Windows (full app + GUI) — using the preset
```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
# -> build/windows-release/Release/hdmi_gui.exe     (desktop app + tray + REST)
# -> build/windows-release/Release/hdmi_server.exe  (headless REST server only)

# Optional: install into a prefix (bin/ + README)
cmake --install build/windows-release --prefix C:/Apps/HdmiSelector
```
The `.exe` icon is embedded from `resources/app.ico`; the window and tray icon
come from `resources/app.xpm`. If CMake can't find wxWidgets, set
`-DwxWidgets_ROOT_DIR=C:/path/to/wxWidgets` (or build with `-DHDMI_BUILD_GUI=OFF`
to skip the GUI).

### Linux / macOS (core + REST + tests, Mock backend)
```bash
cmake --preset linux-core
cmake --build --preset linux-core
ctest --preset linux-core
./build/linux-core/hdmi_server --port 8420   # serves the Mock backend
```

## REST API

Default bind: `127.0.0.1:8420`. Auth is off unless a token is configured.

| Method & path | Description |
|---|---|
| `GET /api/health` | `{ "status": "ok", "backend": "windows" }` |
| `GET /api/displays` | List all displays with active/primary/geometry |
| `POST /api/switch` | Body `{ "mode": "exclusive\|extend\|duplicate", "ids": ["..."] }` |
| `POST /api/displays/{id}/activate` | Exclusive-activate one display (convenience) |

`id` values come from `GET /api/displays`. When a token is set, send it as the
`X-Auth-Token` header or a `?token=` query parameter.

### Examples
```bash
# List displays
curl http://127.0.0.1:8420/api/displays

# Switch to the TV only (exclusive)
curl -X POST http://127.0.0.1:8420/api/switch \
     -H "Content-Type: application/json" \
     -d '{"mode":"exclusive","ids":["<tv-id>"]}'

# Convenience: activate one display
curl -X POST http://127.0.0.1:8420/api/displays/<monitor-id>/activate
```

### Exposing to the LAN
```powershell
# Headless server on the LAN with a token
hdmi_server.exe --lan --port 8420 --token MY_SECRET

# GUI app: configure via environment variables
setx HDMI_HOST 0.0.0.0
setx HDMI_TOKEN MY_SECRET
```
Then, from another device: `curl -H "X-Auth-Token: MY_SECRET" http://<pc-ip>:8420/api/displays`.

## Notes / limitations
* Stable display ids use the monitor device path, which survives reboots.
* The `WindowsBackend` compiles and runs on Windows only. The exclusive-switch
  path is the primary supported flow; extend/duplicate are best-effort over the
  same API.
* This was scaffolded and its core/REST layers tested on Linux; do the final
  build and hardware verification on your Windows machine.
