# AGENTS.md — SRC-Link Plugin for OBS Studio

This file provides a brief overview for AI agents. For detailed project documentation, coding guidelines, architecture, and build instructions, see **[CLAUDE.md](CLAUDE.md)**.

## Quick Reference

- **Language**: C++17 with Qt 6
- **Build**: CMake ≥ 3.16
- **Target OBS**: ≥ 30.1.0
- **License**: GPLv2+
- **Maintainer**: OPENSPHERE Inc.

## Key Files

| File | Purpose |
|------|---------|
| `src/plugin-main.cpp` | Plugin entry point (module load/unload) |
| `src/api-client.hpp` / `.cpp` | `SRCLinkApiClient`: central orchestrator, OAuth, REST API, WebSocket |
| `src/api-websocket.hpp` / `.cpp` | `SRCLinkWebSocketClient`: control API WebSocket client |
| `src/request-invoker.hpp` / `.cpp` | HTTP request sequencing with OAuth2 token management |
| `src/schema.hpp` | JSON data schemas (Account, Party, Stage, Uplink, Downlink, etc.) |
| `src/settings.hpp` / `.cpp` | `SRCLinkSettingsStore`: persistent settings (OBS profile INI) |
| `src/utils.hpp` / `.cpp` | Utility functions, encoder helpers, network helpers |
| `src/sources/ingress-link-source.hpp` / `.cpp` | Receiver/downlink source (SRT decode) |
| `src/outputs/egress-link-output.hpp` / `.cpp` | Transmitter/uplink output (SRT/RTMP encode) |
| `src/ws-portal/ws-portal-client.hpp` / `.cpp` | obs-websocket protocol tunneling client |
| `src/ws-portal/event-handler.hpp` / `.cpp` | OBS event broadcasting to WebSocket clients |
| `src/UI/*.hpp` / `.cpp` / `.ui` | Qt dialogs and dock widgets |
| `data/locale/*.ini` | Locale strings (en-US, ja-JP) |
| `data/presets/` | Hardware encoder presets (NVENC, QSV, AMF, Apple, x264) |
| `lib/` | Bundled libraries (nlohmann/json, o2, obs-websocket) |

## Essential Rules

1. Follow OBS plugin conventions (`obs_log()`, `obs_module_text()`, RAII wrappers)
2. All user-facing strings must be localized in `data/locale/en-US.ini` (primary) and `ja-JP.ini`
3. Thread safety required — use mutexes for shared state, `Qt::QueuedConnection` for UI updates
4. Code style enforced by `.clang-format` (120 columns, 4-space indent, C++17)
5. No automated tests — manual testing in OBS Studio required
6. OAuth tokens are sensitive — never log tokens
7. UI layouts defined in `.ui` files (Qt Designer) — `AUTOMOC`, `AUTOUIC`, `AUTORCC` enabled

For full details on architecture, coding guidelines, build instructions, CI/CD, and common tasks, refer to **[CLAUDE.md](CLAUDE.md)**.
