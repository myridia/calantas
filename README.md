<img src="calantas.svg" alt="Calantas Messenger" width="120">

# Calantas Messenger

A basic desktop messenger with the classic Pidgin-style buddy list, written in C with NAppGUI. Calantas uses its own backend interface and does not include `libpurple3`, GTK or Libadwaita.

The first version ships with a mock/local backend: buddies have presence, messages can be sent, and the backend replies locally so the full chat flow can be tested without a network service.

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

## Interface

- Buddy list with status, name, address and status text
- Search/filter field
- Message and Add Buddy buttons
- One conversation window per buddy
- Account dialog
- Status menu: Available, Away, Busy, Offline
- Contacts menu with online-only and all-buddies views
- About dialog and platform-style app menu

## Structure

- `src/main.c` — `osmain_sync` entry point
- `src/gui.c` / `src/gui.h` — NAppGUI windows, buddy list, conversations, menus and dialogs
- `src/backend.c` / `src/backend.h` — messenger model and mock/local backend
- `src/res/` — application icons
- `CMakeLists.txt` — top-level NAppGUI project
- `src/CMakeLists.txt` — desktop app target
- `build.sh` — configure, build and run
- `run.sh` — run an existing build
- `ask.sh` / `ask.d/` — task runner
- `.github/workflows/control.yml` — single CI entry point
- `.github/workflows/{linux,mac,windows}.yml` — platform build and release workflows
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
