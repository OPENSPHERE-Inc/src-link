# CLAUDE.md — SRC-Link Plugin for OBS Studio

## Project Overview

**SRC-Link** (Secure Reliable Controllable Link) is an OBS Studio plugin that transforms OBS into a
video transmitter/receiver system. It enables multiple independent video/audio streams via SRT protocol
(guest mode), receiving clean feeds from multiple sources (host mode), WebSocket Portal for remote OBS
control, and connection management with team operations.
It is developed and maintained by **OPENSPHERE Inc.** under the GPLv2+ license.

- Repository language: **C++ 17** with **Qt 6** for UI
- Build system: **CMake** (≥ 3.16, ≤ 3.26)
- Based on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
- Target OBS Studio version: **≥ 30.1.0** (Qt6, x64 / ARM64 / Apple Silicon)
- Current version: **0.7.6**

---

## Repository Layout

```
src-link/
├── CMakeLists.txt              # Top-level CMake project definition
├── CMakePresets.json            # CMake presets (windows-x64, macos, linux-x86_64, linux-aarch64, CI variants)
├── buildspec.json               # Build specification (name, version, dependencies, UUIDs)
├── build.ps1                    # Local Windows build script (RelWithDebInfo)
├── build-debug.ps1              # Debug build script
├── build-staging.ps1            # Staging build script
├── build-test.ps1               # Test build script
├── Frameworks.cmake.in          # macOS framework configuration
├── .clang-format                # C++ code style (clang-format ≥ 16)
├── .cmake-format.json           # CMake code style
├── .github/
│   ├── actions/                 # Reusable CI actions (build, packaging, format-check, code-signing)
│   └── workflows/               # CI workflows (build, format check, release)
├── cmake/                       # CMake helpers & platform-specific modules
├── build-aux/                   # Auxiliary build scripts (format runners)
├── data/
│   ├── locale/                  # Translation files (en-US.ini, ja-JP.ini)
│   ├── presets/                 # Encoder presets (NVENC, QSV, AMF, Apple, x264)
│   ├── *.jpg                    # Placeholder images (filler, connecting, ports-error, unreachable)
│   └── oauth-reply-*.html       # OAuth callback pages (en-US, ja-JP)
├── src/
│   ├── plugin-main.cpp          # Plugin entry point (module load/unload)
│   ├── api-client.hpp / .cpp    # SRCLinkApiClient: central orchestrator, OAuth, REST API, WebSocket
│   ├── api-websocket.hpp / .cpp # SRCLinkWebSocketClient: control API WebSocket client
│   ├── request-invoker.hpp / .cpp # HTTP request sequencing (OAuth2 via o2 library)
│   ├── settings.hpp / .cpp      # SRCLinkSettingsStore: persistent settings (OBS profile INI)
│   ├── schema.hpp               # JSON data schemas (Account, Party, Stage, Uplink, Downlink, etc.)
│   ├── utils.hpp / .cpp         # Utility functions, encoder helpers, network helpers
│   ├── plugin-support.h         # Auto-generated plugin support header
│   ├── plugin-support.c.in      # Template for plugin-support.c
│   ├── sources/
│   │   ├── ingress-link-source.hpp / .cpp  # Receiver/downlink source (SRT decode)
│   │   ├── audio-capture.hpp / .cpp        # Audio buffer management
│   │   └── image-renderer.hpp / .cpp       # Placeholder image rendering
│   ├── outputs/
│   │   ├── egress-link-output.hpp / .cpp   # Transmitter/uplink output (SRT/RTMP encode)
│   │   └── audio-source.hpp / .cpp         # Audio mixer integration
│   ├── UI/
│   │   ├── settings-dialog.hpp / .cpp / .ui     # OAuth login & settings
│   │   ├── output-dialog.hpp / .cpp / .ui        # Encoder configuration
│   │   ├── egress-link-dock.hpp / .cpp / .ui     # Uplink status dock
│   │   ├── egress-link-connection-widget.hpp / .cpp / .ui  # Per-connection status widget
│   │   ├── ws-portal-dock.hpp / .cpp / .ui       # WebSocket Portal dock
│   │   ├── redeem-invite-code-dialog.hpp / .cpp / .ui  # Invitation code redemption
│   │   ├── resources.qrc        # Qt resource file
│   │   └── images/              # Icons and images
│   └── ws-portal/
│       ├── ws-portal-client.hpp / .cpp    # obs-websocket protocol tunneling client
│       └── event-handler.hpp / .cpp       # OBS event broadcasting to WebSocket clients
├── lib/
│   ├── json/                    # nlohmann/json (bundled)
│   ├── o2/                      # OAuth2 client library (bundled)
│   └── obs-websocket/           # OBS WebSocket library (bundled)
├── shared/
│   ├── properties-view/         # Custom OBS properties UI helpers
│   └── qt/                      # Qt utilities (vertical-scroll-area, wrappers)
├── srtrelay/                    # SRT relay server docker config
├── mediamtx/                    # MediaMTX relay server docker config
└── release/                     # Build output directory
```

---

## Build Instructions

### Prerequisites

| Platform | Toolchain |
|----------|-----------|
| Windows  | Visual Studio 17 2022, CMake ≥ 3.16, Qt 6 |
| macOS    | Xcode ≤ 16.4.0 (macOS SDK ≤ 15.5), CMake ≥ 3.16, Qt 6 |
| Linux    | GCC / Clang, CMake ≥ 3.16, Qt 6 |

> **Note (macOS):** This project requires **Xcode 16.4.0 (macOS 15.5 SDK) or below**. Xcode 26 / macOS SDK 26 and later are currently unsupported due to compatibility constraints with OBS Studio's build dependencies.

OBS Studio sources and pre-built dependencies are fetched automatically via `buildspec.json`
(obs-studio 30.1.2, obs-deps, Qt6).

### Windows (local)

```powershell
# Configure + Build + Install
.\build.ps1

# Debug build
.\build-debug.ps1

# Staging build
.\build-staging.ps1
```

Or manually:

```powershell
cmake --fresh -S . -B build_x64 -G "Visual Studio 17 2022" -A x64
cmake --build build_x64 --config RelWithDebInfo
cmake --install build_x64 --prefix release/Package --config RelWithDebInfo
```

### CMake Presets

Use `cmake --preset <name>` with one of:
- `windows-x64` / `windows-ci-x64`
- `macos` / `macos-ci`
- `linux-x86_64` / `linux-ci-x86_64`
- `linux-aarch64` / `linux-ci-aarch64`

### Build-time Configuration

Optional CMake variables for API endpoint and OAuth configuration:

| Variable | Description |
|----------|-------------|
| `API_SERVER` | SRC-Link REST API base URL |
| `API_WS_SERVER` | SRC-Link WebSocket API base URL |
| `CLIENT_ID` | OAuth2 client ID |
| `CLIENT_SECRET` | OAuth2 client secret |
| `SCHEMA_DEBUG` | Enable JSON schema debug logging |
| `API_DEBUG` | Enable API request/response debug logging |

---

## Architecture & Key Concepts

### Plugin Entry Point

The plugin registers via `obs_module_load()` in `plugin-main.cpp`. It creates:
- A custom OBS source type (`linked_source`) for receiving streams (IngressLinkSource)
- Multiple OBS outputs for transmitting streams (EgressLinkOutput)
- UI docks and dialogs for management
- A central API client (SRCLinkApiClient) for cloud communication

### Core Components

| Component | Class | Description |
|-----------|-------|-------------|
| **API Client** | `SRCLinkApiClient` | Central orchestrator: OAuth2, REST API, WebSocket, state management. Manages parties, stages, uplinks, downlinks, participants, and WebSocket portals. |
| **Receiver (Downlink)** | `IngressLinkSource` | OBS source type (`linked_source`) that receives SRT streams. Manages video/audio decoders, placeholder renderers, hardware decode, buffering, and reconnection. Per-slot resolution and bitrate regulation. |
| **Transmitter (Uplink)** | `EgressLinkOutput` | Creates SRT/RTMP output with dedicated video/audio encoders. Private OBS view and audio context per output. Supports recording, snapshot capture, and statistics monitoring. |
| **API WebSocket** | `SRCLinkWebSocketClient` | WebSocket client for SRC-Link control API. Binary and text message support, resource subscription, RPC invocation, automatic reconnect. |
| **WebSocket Portal** | `WsPortalClient` | Implements obs-websocket protocol tunneling through SRC-Link cloud. RPC request/response proxy for remote OBS control. |
| **Event Handler** | `WsPortalEventHandler` | Singleton OBS event broadcaster for WebSocket Portal clients. |
| **Audio Capture** | `SourceAudioCapture` | Abstract audio buffer with format conversion. Extended by `OutputAudioSource` for OBS audio output binding. |
| **Image Renderer** | `ImageRenderer` | Renders placeholder images (filler, connecting, error states) for sources. |
| **Request Invoker** | `RequestInvoker` / `RequestSequencer` | HTTP request pipelining with OAuth2 token management via o2 library. Sequential queuing and parallel invocation support. |
| **Settings Store** | `SRCLinkSettingsStore` | Persistent key-value settings stored in OBS profile INI. |
| **JSON Schemas** | `schema.hpp` | Typed data classes: `AccountInfo`, `PartyArray`, `StageArray`, `UplinkInfo`, `DownlinkInfo`, `WsPortalArray`, `SubscriptionFeatures`, etc. |

### UI Components

| Widget | Class | Description |
|--------|-------|-------------|
| **Settings Dialog** | `SettingsDialog` | OAuth login, port configuration, guest code management. |
| **Output Dialog** | `OutputDialog` | Encoder selection, bitrate, resolution, video profile, audio codec. |
| **Egress Dock** | `EgressLinkDock` | Uplink status, interlock mode, participant assignment. |
| **Connection Widget** | `EgressLinkConnectionWidget` | Per-connection status visualization within the egress dock. |
| **WS Portal Dock** | `WsPortalDock` | WebSocket Portal status and controls. |
| **Invite Code Dialog** | `RedeemInviteCodeDialog` | Invitation code redemption for joining parties. |

### Threading Model

- **UI Thread**: Qt event loop — main window, dialogs, docks, API client.
- **OBS Callbacks**: Video render, audio filter, video tick — may run on different threads from the UI thread.
- **Audio Thread**: `SourceAudioThread` — dedicated thread for source audio processing in `IngressLinkSource`.
- **Network**: Qt networking runs on the default Qt event loop.

**Synchronization**:
- `QMutex` protects audio buffers in `SourceAudioCapture`.
- `QMutex outputMutex` protects output state in `EgressLinkOutput`.
- `QMetaObject::invokeMethod` with `Qt::QueuedConnection` for thread-safe UI updates from OBS callbacks.
- Settings revisions (`storedSettingsRev` / `activeSettingsRev`) defer restarts to avoid interrupting active outputs.

### Network & API Architecture

**Authentication**:
- OAuth2 flow via the o2 library (browser-based approval).
- Tokens stored in OBS profile settings via `SRCLinkSettingsStore`.
- Automatic token refresh on expiry.
- UUID-based client identification.

**REST API**:
- Base URL configured via `API_SERVER` CMake variable.
- Endpoints: account info, parties, stages, participants, uplinks/downlinks, screenshots, invitation codes.
- Request pipelining: sequential via `RequestSequencer`, parallel via `RequestInvoker`.
- Timeout: 10 seconds.

**WebSocket Protocols**:
- **SRC-Link API WebSocket** (`SRCLinkWebSocketClient`): Binary/text messaging, resource subscription/unsubscription, RPC invocation, automatic reconnect with backoff.
- **OBS WebSocket Tunneling** (`WsPortalClient`): Proxies obs-websocket requests through SRC-Link cloud for remote OBS control.

### OBS API Usage

The plugin heavily uses:
- `obs-module.h` — Module registration, locale, config paths
- `obs-frontend-api.h` — Profile config, scene enumeration, frontend events, dock registration, tools menu
- `obs.hpp` — OBS RAII wrappers (`OBSSourceAutoRelease`, `OBSOutputAutoRelease`, `OBSDataAutoRelease`, etc.)
- `util/deque.h` — Ring buffer for audio samples
- `util/config-file.h` — Profile INI file access
- `util/platform.h` — Platform detection and utilities
- `util/dstr.h` — Dynamic string utilities

---

## Code Style & Formatting

### C++ (clang-format)

- **Standard**: C++17
- **Column limit**: 120
- **Indent**: 4 spaces (tabs not used)
- **Brace style**: Custom — functions use next-line braces; control statements use same-line braces
- **clang-format version**: ≥ 16 required
- Run: `clang-format -i src/**/*.cpp src/**/*.hpp`

### CMake (cmake-format)

- **Line width**: 120
- **Tab size**: 2
- Config: `.cmake-format.json`

### CI Enforcement

Format is checked in CI via `.github/workflows/check-format.yaml` using reusable actions
(`run-clang-format`, `run-cmake-format`). PRs to `master`/`main`/`dev` and pushes to `master`/`dev` are validated.

---

## Coding Guidelines

### General Rules

1. **Follow OBS plugin conventions** — Use `obs_log()` for logging, `OBS_DECLARE_MODULE()` for entry, locale via `obs_module_text()`.
2. **RAII wrappers** — Always use `OBSSourceAutoRelease`, `OBSDataAutoRelease`, etc. instead of manual `obs_*_release()`.
3. **Thread safety** — Any data shared between OBS callbacks and UI must be guarded by mutex. Use `QMetaObject::invokeMethod` with `Qt::QueuedConnection` when calling UI methods from non-UI threads.
4. **Settings migration** — When changing settings schema, add backward-compatible migration code to ensure existing saved settings still load correctly.
5. **Qt MOC** — Classes using `Q_OBJECT` must be in headers. `AUTOMOC`, `AUTOUIC`, `AUTORCC` are enabled.
6. **Qt Designer forms** — UI layouts are defined in `.ui` files. Modify layouts in Qt Designer or by editing the XML directly.

### Naming Conventions

- **Classes**: PascalCase (`SRCLinkApiClient`, `IngressLinkSource`, `EgressLinkOutput`, `WsPortalClient`)
- **Methods**: camelCase (`startOutput`, `stopOutput`, `onSettingsUpdate`, `fetchParties`)
- **Constants/Macros**: UPPER_SNAKE_CASE (`MAX_SERVICES`, `SOURCE_ID`, `DEFAULT_INGRESS_PORT`)
- **Member variables**: camelCase, no prefix (`apiClient`, `videoEncoder`, `outputMutex`)
- **Static callbacks**: camelCase with descriptive prefix (`onFrontendEvent`, `audioFilterCallback`)

### Ternary Operators

- **Avoid multi-line ternary expressions** — clang-format cannot reliably format nested or multi-line ternary operators (`a ? b : c`) in a readable way. Use `if`/`else if`/`else` statements instead when the expression would span multiple lines.

### Error Handling

- Use `obs_log(LOG_ERROR, ...)` for errors, `LOG_WARNING` for warnings, `LOG_INFO` for lifecycle events, `LOG_DEBUG` for verbose tracing.
- Check return values from OBS API calls; handle gracefully (do not crash OBS).
- Never throw C++ exceptions across OBS API boundaries.

### Localization

- All user-facing strings must use `obs_module_text("Key")` or the `QTStr("Key")` helper.
- Add new keys to `data/locale/en-US.ini` (primary) and ideally to `ja-JP.ini`.
- Locale files use INI format: `Key="Value"`.

---

## Testing & Validation

- There are no automated unit tests in this repository currently.
- **Manual testing** is required: load the plugin in OBS Studio and verify uplink/downlink behavior.
- Test on all supported platforms when possible (Windows x64, macOS, Linux x86_64/aarch64).
- Verify OAuth login flow works correctly (browser redirect, token storage, refresh).
- Verify SRT uplink/downlink connections establish and stream properly.
- Verify WebSocket Portal connects and tunnels obs-websocket commands correctly.
- Verify that enabling and then disabling outputs does not cause a crash.
- Verify that shutting down OBS does not crash when outputs are active or inactive.
- Verify that switching scene collections does not crash.
- Verify that no memory leaks are logged on OBS shutdown.
- Verify that mutex does not cause deadlocks.
- Verify audio synchronization between uplink and downlink paths.
- Verify placeholder images render correctly when sources are disconnected.
- Verify encoder fallback logic works across different hardware (NVENC, QSV, AMF, Apple, x264).

---

## CI / CD

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `push.yaml` | Push to `master`/`main`/`dev`/`release/**`, tags | Format check + build + release creation |
| `build-project.yaml` | Called by push/PR workflows | Multi-platform build (Windows, macOS, Linux x86_64, Linux aarch64) |
| `check-format.yaml` | Called by push/PR workflows | clang-format and cmake-format validation |
| `pr-pull.yaml` | Pull requests to `master`/`main`/`dev` | Format check + build validation |
| `dispatch.yaml` | Manual dispatch | On-demand builds |

Release tags follow semver: `X.Y.Z` for stable, `X.Y.Z-beta`/`X.Y.Z-rc` for pre-releases.

---

## Common Tasks for AI Agents

### Adding a New Setting / Feature

1. Add the setting key to the relevant defaults in the source or output class.
2. Add UI controls — either in `.ui` files (Qt Designer) or via OBS properties API (`obs_properties_add_*`).
3. Handle the setting in the appropriate lifecycle methods (`startOutput()` / `stopOutput()` / `updateCallback()`).
4. Add locale strings to `data/locale/en-US.ini` (primary, required) and `ja-JP.ini`.
5. Ensure backward compatibility — existing saved settings must still load correctly.

### Modifying the API Client

- `SRCLinkApiClient` is the central orchestrator for all cloud communication.
- REST API calls go through `RequestInvoker` / `RequestSequencer` with automatic OAuth2 token management.
- State is held in typed collections (`PartyArray`, `StageArray`, etc.) defined in `schema.hpp`.
- Signal/slot pattern is used for event propagation to UI components.

### Modifying the Receiver (Downlink)

- `IngressLinkSource` in `src/sources/` is a custom OBS source type (`linked_source`).
- Manages SRT stream reception, video/audio decoding, and placeholder rendering.
- Audio processing runs on a dedicated `SourceAudioThread`.
- `SourceAudioCapture` manages audio buffering with format conversion.
- `ImageRenderer` handles placeholder images for various connection states.

### Modifying the Transmitter (Uplink)

- `EgressLinkOutput` in `src/outputs/` creates SRT/RTMP output with dedicated encoders.
- Each output has its own OBS view, audio context, and recording pipeline.
- `OutputAudioSource` extends `SourceAudioCapture` for OBS audio output binding.
- Status tracking: active, standby, inactive, reconnecting, error.
- Snapshot capture runs on a configurable interval for the control panel.

### Modifying WebSocket Portal

- `WsPortalClient` in `src/ws-portal/` implements obs-websocket tunneling.
- `WsPortalEventHandler` is a singleton that broadcasts OBS events to connected clients.
- Uses `QMetaObject::invokeMethod` with `Qt::QueuedConnection` for thread-safe event dispatch.

### Modifying UI Components

- Dialogs and docks use Qt Designer `.ui` files for layout.
- `AUTOMOC`, `AUTOUIC`, `AUTORCC` are enabled — Qt meta-object compiler runs automatically.
- Thread-safe updates via `QMetaObject::invokeMethod` when updating UI from non-UI threads.
- `shared/properties-view/` provides custom OBS properties UI helpers.

### Modifying JSON Schemas

- All data models are defined in `schema.hpp` using `TypedJsonArray` with find/filter/every operations.
- Classes: `AccountInfo`, `PartyArray`, `StageArray`, `UplinkInfo`, `DownlinkInfo`, `WsPortalArray`, `SubscriptionFeatures`.
- Serialization uses `QJsonObject` / `QJsonArray`.
- Add validation methods for new schema fields.

---

## Important Warnings

- **Do NOT call `obs_filter_get_parent()` in constructors** — it returns `nullptr` at that point.
- **OAuth tokens are sensitive** — never log tokens. Store only in `SRCLinkSettingsStore`.
- **Settings revisions** (`storedSettingsRev` / `activeSettingsRev`) exist to avoid stopping output during reconnect attempts. Do not bypass this mechanism.
- **Encoder compatibility** — The plugin maps "simple" encoder names to actual encoder IDs, with version-specific fallbacks (OBS 30 vs OBS 31). See `getSimpleVideoEncoder()` in `utils.hpp`.
- **Memory management** — Use OBS RAII wrappers. Raw `bfree()` / `obs_data_release()` calls are error-prone.
- **`.gitignore` uses allowlist pattern** — New top-level files/directories must be explicitly un-ignored with `!` prefix.
- **Port management** — Ingress ports must be within the configured range and not conflict with other instances. The API client manages port allocation.
- **Bundled libraries** (`lib/`) — `nlohmann/json`, `o2`, and `obs-websocket` are vendored. Update with caution.
- **Encoder presets** (`data/presets/`) — Hardware encoder presets are platform-specific. Test on target hardware when modifying.

---

## Agent Teams

### Team Leader Policy

- The team leader must focus exclusively on orchestrating teammates.

### Team Creation Policy

- Team size should be 3–5 members, selected from the cast below based on the task.
- Each teammate must work on different files to avoid edit conflicts.
- Start with research and review, then launch the team for parallel execution.
- **Do not use subagents for tasks that can be handled by agent teams.**

  **Exception**: Use subagents for parallel reviews.

### Agent Cast

#### cpp-sensei — C++ Native Application Specialist

Expert in C++ language specifications and Windows/macOS/Linux native application development.

- C++ implementation work
- C++ coding advice
- Code review from the perspective of C++ language specifications and coding standards
- Multithreading implementation and thread safety advice

#### obs-sensei — OBS Studio Plugin Specialist

Expert in OBS Studio internals, the OBS Studio API, and OBS Studio plugins.

- OBS Studio specification advice
- OBS Studio API selection
- OBS Studio plugin specification advice

#### qt-sensei — Qt Specialist

Expert in Qt framework, GUI application design, implementation, and testing.

- Qt specification advice
- Qt API selection
- Qt GUI construction advice
- Qt object design advice

#### network-sensei — Network Specialist

Expert in network programming, TCP/IP, HTTP, SSL/TLS, WebSocket, socket communication, and streaming protocols such as RTMP/SRT/WebRTC.

- Network programming advice
- Network protocol implementation advice
- Streaming protocol implementation advice
- Security advice
- Network code review

#### translation-sensei — Translation Specialist

Multilingual translator.

- Locale INI file translation
- Document translation

#### av-sensei — Audio/Video/Streaming Specialist

Expert in video, audio, and streaming technologies, media quality, video processing, audio processing, encoders, and broadcast operations.

- Technical advice on video, audio, and streaming
- Quality advice on video, audio, and streaming
- Encoder configuration advice
- Broadcast operations advice

#### devops-sensei — DevOps Specialist

Expert in CI/CD (GitHub Actions), CMake, clang-format, VS Code, Inno Setup, and other development environment and build process tooling.

- GitHub Actions workflow editing and review
- CMake build script editing and review
- Inno Setup build script editing and review

#### python-sensei — Python Specialist

Expert in Python scripting, OBS Studio Script design, implementation, and testing.

- Python implementation work
- Python coding advice
- Code review from the perspective of Python language specifications and coding standards
