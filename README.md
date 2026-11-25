<img src="calantas.svg" alt="Calantas Messenger" width="120">

# Calantas Messenger

A basic desktop messenger with the classic Pidgin-style buddy list, written in C with NAppGUI. Calantas uses its own backend interface and does not include `libpurple3`, GTK or Libadwaita.

The first version ships with a mock/local backend: buddies have presence, messages can be sent, and the backend replies locally so the full chat flow can be tested without a network service. Account details, buddies and recent messages are saved between runs.

## Stack

- C17-style C source
- NAppGUI 1.7
- CMake
- Cross-platform: Linux, macOS, Windows

## Build and run

NAppGUI must be installed first. The `ask.sh` tasks can install it and build the app:

```bash
./ask.sh
```

Direct build and run:

```bash
./build.sh
```

Run an existing build:

```bash
./run.sh
```

The binary is written to `build/Release/bin/calantas`.

NAppGUI is expected in `/usr/local/nappgui` on Linux/macOS and `C:/nappgui` on Windows. Override the SDK location with `NAPPGUI_DIR=/path/to/nappgui`. Setup tasks clone the NAppGUI source into `~/.cache/calantas/nappgui_src`, outside the repository, so the project stays clean.

## Interface

- Buddy list with status, name, address and status text
- Search/filter field
- Message and Add Buddy buttons
- One conversation window per buddy
- Account dialog
- Status menu: Available, Away, Busy, Offline
- Contacts menu with online-only and all-buddies views
- About dialog and platform-style app menu
- Local state file for account name, status, buddies and recent messages

## Structure

- `src/main.c` — `osmain_sync` entry point
- `src/gui.c` / `src/gui.h` — NAppGUI windows, buddy list, conversations, menus and dialogs
- `src/backend.c` / `src/backend.h` — messenger model, mock/local backend and text state persistence
- `src/res/` — application icons
- `CMakeLists.txt` — top-level NAppGUI project
- `src/CMakeLists.txt` — desktop app target
- `build.sh` — configure, build and run
- `run.sh` — run an existing build
- `ask.sh` / `ask.d/` — task runner
- `.github/workflows/control.yml` — single CI entry point with mcclim-style tag resolution
- `.github/workflows/{linux,mac,windows}.yml` — reusable platform build and release workflows with optional `tag`
- `plan.md` — saved implementation plan and roadmap
- `calantas.svg` — project logo

## Backend plan

The GUI talks only to `backend.h`, so real protocols can be added without changing the interface:

| Backend | Status |
|---|---|
| Mock/local | Implemented |
| XMPP + libstrophe | Planned |
| IRC | Planned |
| Telegram + TDLib | Planned |
| ActivityPub via Mastodon API | Planned |
| Raw ActivityPub | Planned later |

There is no libpurple3 adapter and no plan to add one.

## License

GPL-3.0. See `LICENSE`.
