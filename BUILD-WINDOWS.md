# Building Kai's Windows installer

This guide describes how to build Kai and produce a Windows installer.
The **maintained path is a Docker-based cross-build from Linux**, which is
what actually produces the artifacts in every release — no Windows
machine, MSVC, or Inno Setup required.

Kai's code is portable Qt 6, `assets/` (themes, language packs, help
content, logo) are resolved at runtime relative to the executable, and
shell execution normally uses `bash -c` — on Windows, use a **Terminal
Target** (Settings → Terminal Targets) to run commands inside WSL. Kai
ships a default one:

```
Name:     WSL
Template: wsl.exe -- bash -lic 'eval "$(echo "$1" | base64 -d)"' kai {{command_b64}}
```

See [`docs/terminal-targets-wsl.md`](docs/terminal-targets-wsl.md) for the
details of that template.

---

## Docker cross-build (maintained path)

### Requirements

- Docker with Compose v2 (`docker compose`).
- Under WSL2/Docker Desktop, wine's shared-memory mapping needs
  `security_opt: seccomp=unconfined` — already set in `docker-compose.yml`.

### Usage

```bash
# Linux artifact (AppImage):
docker compose run --rm build-linux

# Windows artifacts (portable folder always; installer opt-in):
docker compose run --rm build-windows
KAI_BUILD_INSTALLER=1 docker compose run --rm build-windows   # also builds kai-setup.exe
```

Artifacts land in `./dist`:

- `dist/Kai-x86_64.AppImage` — self-contained Linux binary.
- `dist/kai-windows/` — portable folder (`kai.exe` + Qt DLLs + plugins +
  assets). Zip it and it runs anywhere with a double-click.
- `dist/kai-setup.exe` — the Windows installer (only with
  `KAI_BUILD_INSTALLER=1`), built with **NSIS 3 + Modern UI 2** (`makensis`,
  run natively inside the build image — no wine needed for this step) from
  `packaging/windows/kai.nsi`. This is the **official** installer; `kai.iss`
  (Inno Setup) is only the legacy native-Windows path.

#### What the installer does

- **Language picker** (English / Português do Brasil), preselecting the
  system language; the choice is remembered and reused by the uninstaller.
- Pages: welcome, folder, **Preferences** (theme from `assets/themes`, app
  language, start with Windows, start minimized in the tray), **Options**
  (desktop shortcut, add `kai` to the user PATH), install, finish
  ("Open Kai", opt-out).
- Installs per-user (no admin) in `%LocalAppData%\Programs\Kai`; an existing
  install keeps its folder. Kills a running `kai.exe` before copying files.
- **Config**: on a fresh install it writes `%LocalAppData%\kai\settings.json`
  with only `active_theme`, `language`, `start_visible` (false when "start
  minimized") and `autostart`; "start with Windows" also writes the
  `HKCU\...\Run\Kai` value. If `settings.json` already exists (update or
  reinstall) the Preferences page is skipped and the file is never touched.
- **Uninstaller** removes files, shortcuts, the Run value, the PATH entry and
  its registry keys. Settings/data are kept unless you answer Yes to the
  prompt (`/S` keeps them).
- Silent install (`kai-setup.exe /S [/D=C:\dir]`) uses the defaults: Kai Dark,
  English, desktop shortcut, PATH, no autostart, start visible.

The banner/welcome bitmaps and `installer.ico` are generated from
`assets/logo/kai.png` by `python3 packaging/windows/gen-installer-art.py`
(stdlib only) and committed — rerun it if the logo changes. The release
script bumps the version in `kai.nsi` (`APPVERSION`, `VIProductVersion`).

### How the Windows image is built

`packaging/docker/Dockerfile.windows` is based on
`stateoftheartio/qt6:6.6-mingw-aqt` (Qt 6 cross-compiled for
`x86_64-w64-mingw32`, plus wine). It cross-compiles with CMake/Ninja,
deploys Qt's runtime DLLs via `windeployqt` (run under wine), and
optionally packages the NSIS installer.

### Incremental builds & disk usage (important under WSL2)

Build output and ccache live in **named Docker volumes**
(`kai-win-build`, `kai-win-ccache`), not in the container's writable
layer — a container's layer is discarded every run, and under WSL2 that
kind of repeated write only ever grows Docker's disk image (it never
shrinks back down). With named volumes, rebuilds are incremental and the
space used stays stable and inspectable (`docker volume ls`,
`docker system df`).

```bash
# One-time, after a Dockerfile change (rebuilds the image):
docker compose build build-windows

# Regular builds — incremental, much lighter:
docker compose run --rm build-windows

# Start from scratch (e.g. after a toolchain change):
KAI_CLEAN_BUILD=1 docker compose run --rm build-windows
```

Resource limits (`cpus: 4`, `mem_limit: 6g`, `CCACHE_MAXSIZE=2G`) keep a
build from starving the host machine.

### Reclaiming disk space

```bash
docker builder prune -af                        # build cache (regenerable)
docker image prune -f                           # untagged orphan images
docker volume rm kai-win-build kai-win-ccache   # discards the incremental build

# Do NOT use `docker system prune -a` — it would also delete the
# kai-build-windows image itself (a few GB, plus rebuild time).
```

Freeing space inside WSL doesn't hand it back to Windows. To actually
reclaim it, with WSL shut down, from PowerShell:

```powershell
wsl --shutdown
Optimize-VHD -Path "$env:LOCALAPPDATA\Packages\<distro>\LocalState\ext4.vhdx" -Mode Full
```

---

## Native Linux build

```bash
./build.sh              # builds to build/bin/kai
./build.sh install      # installs to ~/.local/bin and ~/.local/share/kai
```

---

## Legacy path: native build on Windows (unmaintained)

A native-Windows build path also exists (`build-windows.bat` + Inno Setup,
`packaging/windows/kai.iss`, a plain installer without the pages above), requiring a local Qt 6 (MSVC) install,
Visual Studio, CMake, and optionally Inno Setup. It isn't the path that
produces release artifacts today and may drift out of sync with the NSIS
installer's content and behavior — the Docker + NSIS path above is the
one to use.

## Portability notes already handled in the code

- **Config & assets:** `QStandardPaths::AppConfigLocation` resolves to
  `%LocalAppData%\kai` on Windows automatically; `assets/` are looked up
  relative to the `.exe`.
- **Global shortcut:** QHotkey has a native Win32 backend, so the global
  shortcut works on Windows.
- **System tray:** supported on Windows.
- **Terminal:** use a WSL terminal target (above) for commands that need a
  Unix shell.
