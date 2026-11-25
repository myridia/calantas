# Calantas plan

## Goal

Build a basic desktop messenger with the classic Pidgin-style buddy list, using NAppGUI. The GUI is custom C code and talks only to `backend.h`.

Decisions that must stay fixed:

- No `libpurple3`.
- No GTK, Libadwaita or Finch code.
- NAppGUI is the only GUI toolkit.
- Backends are added one by one behind `backend.h`.
- The first real protocol target is XMPP, not a Pidgin backend.
- Raw ActivityPub comes after the Mastodon-compatible API path.

## Current state

Implemented:

- NAppGUI CMake project following `hello_c_nappgui`.
- `osmain_sync` app ticker.
- Classic buddy list window with search/filter.
- One conversation window per buddy.
- Account dialog and Add Buddy dialog.
- Status menu: Available, Away, Busy, Offline.
- Contacts menu: online-only/all buddies.
- About dialog and platform-style menus.
- Mock/local backend with initial buddies, presence, sent messages and delayed local replies.
- Linux, macOS and Windows GitHub Actions workflows with release uploads.
- README logo, `AGENTS.md`, build scripts, `ask.sh` and project-map entry.
- `ask.sh` fallbacks for missing `ask.d/` scripts.
- macOS build support in `build.sh` and `run.sh`, including `.app` bundles.

Verified by the user:

- Linux CMake configure and build succeeded.
- App launched with the GTK3/X11 NAppGUI backend.

Not verified yet:

- Windows build and run.
- macOS build and run.
- GitHub Actions runs.

## Architecture

```text
src/main.c       osmain_sync entry point
src/gui.c        NAppGUI windows, buddy list, conversations, dialogs, menus
src/backend.c    messenger model and mock/local backend
src/backend.h    only interface the GUI is allowed to use
```

Rules:

- GUI code must not know which backend is active.
- Backend events are polled in `calantas_update()` on the 0.25s ticker.
- Network work in real backends must run off the GUI thread.
- Real backends push events into a queue; the GUI drains that queue.
- Do not block NAppGUI callbacks.

## Roadmap

1. Test Windows manually.
   - Install NAppGUI with `ask.sh` task 1.
   - Build with MSVC/CMake.
   - Check buddy list, search, add buddy, conversations, menus and close.

2. Test macOS manually.
   - Install NAppGUI with `ask.sh` task 2.
   - Build with Xcode/CMake.
   - Check buddy list, conversations, Apple menu and Quit.

3. Add local persistence.
   - Save account name, status, buddies and recent messages.
   - Keep it small and text-based first.
   - Store under the user config directory.

4. Add XMPP core with libstrophe.
   - JID/password account.
   - TLS/SASL login.
   - Roster.
   - Presence/status.
   - Direct messages.
   - Worker thread plus event queue.

5. Add IRC.
   - Server/nick/channel account model.
   - Channels as conversations.
   - Simple send/receive.

6. Add Telegram with TDLib.
   - Use the TDLib JSON C API.
   - Poll updates on a worker/ticker boundary.
   - Phone/code/2FA login.
   - Contacts, chats and messages.

7. Add ActivityPub via Mastodon-compatible API.
   - Actor handle as buddy.
   - Follow/following list.
   - Direct messages as `direct` visibility statuses.
   - Timelines as optional conversation views.

8. Add raw ActivityPub later.
   - WebFinger.
   - Actor JSON.
   - Inbox/outbox.
   - `Create`, `Follow`, `Accept`, `Like`, `Announce`.
   - HTTP signatures and local key storage.
   - Only do this after the Mastodon API backend works.

9. Packaging later.
   - Debian package after Linux backend work settles.
   - MSIX after Windows build is stable.
   - Optional macOS Intel workflow after ARM is stable.

## Explicitly out of scope

- `libpurple3` adapter.
- Porting Pidgin protocol plugins.
- GTK/Libadwaita UI.
- Full Pidgin feature parity.
- File transfers, plugins and preferences in the first version.
- End-to-end encryption claims for ActivityPub direct messages.

## Build commands

```bash
./ask.sh
./build.sh
./run.sh
```

Binary:

```text
build/Release/bin/calantas
```

CMake defaults `nappgui_DIR` to `/usr/local/nappgui/cmake` on Linux/macOS and `C:/nappgui/cmake` on Windows. Set `NAPPGUI_DIR` to override the SDK prefix.

## Workflow files

```text
.github/workflows/control.yml
.github/workflows/linux.yml
.github/workflows/mac.yml
.github/workflows/windows.yml
```

`control.yml` is the single entry point and follows the mcclim settings: it accepts an optional `tag` input, uses the pushed tag when present, and falls back to `v0.1.0`. Leaf workflows are reusable with `workflow_call`, accept an optional `tag`, skip release when no tag resolves, and can also be dispatched directly.

## Agent rules for this project

- No git commands; the user commits.
- No build, lint or run by the agent.
- No comments in code unless asked.
- Apply `chown -R 1000:33` and `chmod -R g+w` after edits.
- Keep all responses in English and address the user as Master.
