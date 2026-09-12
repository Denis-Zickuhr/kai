# Spec 08: Cross-Platform Build & Packaging

## 1. Project layout
```text
kai/
├── CMakeLists.txt
├── specs/
├── src/
│   ├── main.cpp
│   ├── core/           # ConfigManager, EnvironmentManager, Models
│   ├── engine/          # ProcessRunner, ExecutionPipeline, HttpRunner
│   ├── ui/              # MainWindow, output panel, PTY terminal, project selector
│   ├── ipc/              # CLI client + IPC server (single instance)
│   └── utils/           # ThemeManager, Logger, i18n
├── assets/               # icons, themes, language packs, help content
├── tests/                # Qt Test suites
└── packaging/            # Docker build images, NSIS installer script
```

## 2. CMake configuration
- **Standard:** C++20.
- **Required Qt 6 modules:** `Core`, `Gui`, `Widgets`, `Network`, `Svg`,
  `Test`.
- **Bundled via `FetchContent`:** QHotkey (global shortcuts) and libvterm
  (interactive terminal), both built as static libraries.

## 3. Building locally (Linux)
```bash
./build.sh              # builds to build/bin/kai
./build.sh install      # installs to ~/.local/bin, assets to ~/.local/share/kai
```

## 4. Packaging via Docker

The root `docker-compose.yml` defines two one-off build services that read
the source read-only and write finished artifacts to `./dist`:

```bash
docker compose run --rm build-linux                          # AppImage
KAI_BUILD_INSTALLER=1 docker compose run --rm build-windows   # portable folder + installer
```

- **`build-linux`** (`packaging/docker/Dockerfile.linux`): builds natively
  on Ubuntu, then packages a self-contained `Kai-x86_64.AppImage` via
  `linuxdeploy`.
- **`build-windows`** (`packaging/docker/Dockerfile.windows`, based on
  `stateoftheartio/qt6:*-mingw-aqt`): cross-compiles with MinGW, deploys
  Qt's DLLs via `windeployqt` under wine, and — when
  `KAI_BUILD_INSTALLER=1` — generates the installer with **NSIS**
  (`makensis`, run natively, no wine needed) from
  `packaging/windows/kai.nsi`. Build output and ccache live in named
  Docker volumes so repeated builds are incremental instead of starting
  from scratch (and, under WSL2, don't keep growing the Docker VM's disk
  image).
- A legacy native-Windows build path also exists
  (`build-windows.bat` + Inno Setup, `packaging/windows/kai.iss`) but isn't
  the maintained one — the Docker + NSIS path above is what actually ships
  releases.

See [`BUILD-WINDOWS.md`](../BUILD-WINDOWS.md) for the full walkthrough,
including WSL2 disk-usage notes.

## 5. Tests

```bash
cd build && ctest --output-on-failure
```

Qt Test suites cover models, variable resolution, the execution pipeline,
import parsers, UI components (built and run under
`QT_QPA_PLATFORM=offscreen` where no display is available) and language
pack parity between `assets/i18n/en.json` and `assets/i18n/pt.json`.
