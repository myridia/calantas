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

`build.sh` configures CMake, builds Release and launches `build/Release/bin/calantas`. NAppGUI is expected in `/usr/local/nappgui` on Linux/macOS and `C:/nappgui` on Windows; `NAPPGUI_DIR` overrides the SDK prefix. CMake also defaults `nappgui_DIR` to `<prefix>/cmake`. Setup scripts clone NAppGUI source into `~/.cache/calantas/nappgui_src`, not into the repository. The XMPP backend is built only when libstrophe is detected; set `-DCALANTAS_ENABLE_XMPP=OFF` to disable detection. The Telegram backend is built only when TDLib's `tdjson` is detected; set `-DCALANTAS_ENABLE_TELEGRAM=OFF` to disable detection.

## Structure
- `src/main.c` — `osmain_sync` entry point and 0.25s ticker
- `src/gui.c` / `src/gui.h` — buddy list, conversation windows, menus and dialogs
- `src/backend.c` / `src/backend.h` — account/buddy/message model and text state persistence
- `src/xmpp.c` / `src/xmpp.h` — optional libstrophe backend with worker thread and event queue
- `src/telegram.c` / `src/telegram.h` — optional TDLib JSON backend with worker thread and event queue
- `src/res/` — icons
- `CMakeLists.txt` — NAppGUI package lookup and project setup
- `src/CMakeLists.txt` — `nap_desktop_app`
- `ask.sh` / `ask.d/` — install/build/run/clean tasks with built-in fallbacks when `ask.d/` is missing; task 6 cleans `build/` and `dist/`; task 7 installs XMPP libraries; task 8 installs NAppGUI plus all XMPP, Telegram and ActivityPub dependencies
- `build.sh` / `run.sh` — direct build and launch, including macOS `.app` bundles
- `.github/workflows/control.yml` — single CI entry point with mcclim-style tag resolution and Linux, macOS and Windows jobs; runs for repository owners `myridia` and `veto8`
- `.github/workflows/linux.yml` — Linux build and release with optional `tag`
- `.github/workflows/mac.yml` — macOS ARM build and DMG release with optional `tag`
- `.github/workflows/windows.yml` — Windows build and release with optional `tag`
- `plan.md` — saved implementation plan and roadmap
- `calantas.svg` — README logo

## Architecture
- The GUI depends only on `backend.h`.
- The XMPP backend uses libstrophe on a worker thread; it handles TLS/SASL login, roster, presence and direct messages, and pushes events to the GUI thread.
- The Telegram backend uses TDLib's JSON C API on a worker thread; it handles phone/bot-token/code/2FA login, chats and text messages, and pushes events to the GUI thread.
- `account.json` keeps a list of accounts with name, protocol, JID/identity, password/code, server, Telegram API ID/hash and auto-connect flag. `cal_backend_store()` / `cal_backend_restore()` keep status, buddies and recent messages in `state-xmpp.txt` / `state-xmpp-NAME.txt` or `state-telegram.txt` / `state-telegram-NAME.txt` under the user config directory. Secrets are stored in plain text with user-only permissions on Unix.
- `calantas_update()` polls backend events on the NAppGUI ticker.
- Conversation windows are one per buddy, created on demand; they track the buddy handle so roster reordering does not retarget them.
- Planned backends: IRC, ActivityPub via Mastodon API, then raw ActivityPub.
- No libpurple3 adapter.

## Conventions
- No comments in code unless asked.
- No git commands; the user commits.
- No build, lint or run by the agent; the user builds and verifies.
- Apply `chown -R 1000:33` and `chmod -R g+w` after editing.
