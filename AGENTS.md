# AGENTS.md — calantas

## What this is
Calantas is a basic desktop messenger with a classic Pidgin-style buddy list, written in C with NAppGUI. It uses a custom backend interface and does not use libpurple3, GTK or Libadwaita.

## Stack
- C
- NAppGUI 1.7
- CMake
- Cross-platform desktop app: Linux, macOS, Windows

## Build and run
The user builds and runs:

```bash
./ask.sh
./build.sh
./run.sh
```

`build.sh` configures CMake, builds Release and launches `build/Release/bin/calantas`.

## Structure
- `src/main.c` — `osmain_sync` entry point and 0.25s ticker
- `src/gui.c` / `src/gui.h` — buddy list, conversation windows, menus and dialogs
- `src/backend.c` / `src/backend.h` — account/buddy/message model and mock/local backend
- `src/res/` — icons
- `CMakeLists.txt` — NAppGUI package lookup and project setup
- `src/CMakeLists.txt` — `nap_desktop_app`
- `ask.sh` / `ask.d/` — install/build/run tasks with built-in fallbacks when `ask.d/` is missing
- `build.sh` / `run.sh` — direct build and launch, including macOS `.app` bundles
- `.github/workflows/control.yml` — single CI entry point with Linux, macOS and Windows jobs
- `.github/workflows/linux.yml` — Linux build and release
- `.github/workflows/mac.yml` — macOS ARM build and DMG release
- `.github/workflows/windows.yml` — Windows build and release
- `plan.md` — saved implementation plan and roadmap
- `calantas.svg` — README logo

## Architecture
- The GUI depends only on `backend.h`.
- The mock backend supplies buddies, presence, sent messages and local replies.
- `calantas_update()` polls backend events on the NAppGUI ticker.
- Conversation windows are one per buddy, created on demand.
- Planned backends: XMPP with libstrophe, IRC, Telegram with TDLib, ActivityPub via Mastodon API, then raw ActivityPub.
- No libpurple3 adapter.

## Conventions
- No comments in code unless asked.
- No git commands; the user commits.
- No build, lint or run by the agent; the user builds and verifies.
- Apply `chown -R 1000:33` and `chmod -R g+w` after editing.
