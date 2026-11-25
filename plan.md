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
- Local persistence of account name, own status, buddies and recent messages in text state files under the user config directory.
- Optional XMPP backend with libstrophe: TLS/SASL login, roster, presence, direct messages, worker thread and event queue.
- Saved XMPP JID and password with automatic connect when the XMPP backend is selected.
- Setup task for the full SDK/backend dependency set, including TDLib and the HTTP/JSON libraries needed later.

Verified by the user:

- Linux CMake configure and build succeeded.
- App launched with the GTK3/X11 NAppGUI backend.
- macOS build succeeded before the XMPP changes.
- Windows CI build succeeded before the XMPP changes.
- Linux XMPP-enabled build succeeded after changing `target_link_libraries()` to the plain signature.
- CMake reported `XMPP backend: enabled (PkgConfig::PC_STROPHE)` on Linux and macOS.
- XMPP login succeeded; outgoing direct messages were sendable.
- Incoming direct messages did not appear before the libstrophe handler return-type fix.

Not verified yet:

- The libstrophe handler return-type fix for receiving messages.
- Saved password, restored password and automatic connect on selecting the XMPP backend.
- macOS rebuild with the updated `src/CMakeLists.txt`.
- Manual UI checks on macOS and Windows.
- Full GitHub Actions release uploads.

## Architecture

```text
src/main.c       osmain_sync entry point
src/gui.c        NAppGUI windows, buddy list, conversations, dialogs, menus
src/backend.c    messenger model, mock/local backend and state files
src/backend.h    only interface the GUI is allowed to use
src/xmpp.c       optional libstrophe worker thread and event queue
src/xmpp.h       private interface between backend.c and the XMPP backend
```

Rules:

- GUI code must not know which backend is active.
- Backend events are polled in `calantas_update()` on the 0.25s ticker.
- Network work in real backends must run off the GUI thread.
- Real backends push events into a queue; the GUI drains that queue.
- Do not block NAppGUI callbacks.

## Roadmap

1. Test Windows manually. Done: CI build passes; manual UI check still pending.
   - Check buddy list, search, add buddy, conversations, menus and close.

2. Test macOS manually. Done: build passes; manual UI check still pending.
   - Check buddy list, conversations, Apple menu and Quit.

3. Add local persistence. Done.
   - Account name, own status, buddies and recent messages are saved as text.
   - State files: `state.txt` for local and `state-xmpp.txt` for XMPP under `~/.config/calantas` on Linux/macOS or `%APPDATA%\Calantas` on Windows. The XMPP password is stored there in plain text with user-only permissions on Unix.

4. Add XMPP core with libstrophe. Implemented, not verified by the user yet.
   - JID/password account in the Account dialog.
   - TLS/SASL login through libstrophe.
   - Roster, presence and own status.
   - Direct messages.
   - Worker thread plus event queue.
   - Optional build: the app builds without libstrophe and disables the XMPP menu item.

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

`ask.sh` task 6 cleans `build/` and `dist/` without removing the saved state file. Task 7 installs libstrophe and its OpenSSL, Expat, zlib and pkg-config dependencies for the XMPP backend. Task 8 installs NAppGUI plus the full dependency set: XMPP/libstrophe, Telegram/TDLib, ActivityPub/libcurl and cJSON, OpenSSL, Expat, zlib, pkg-config and build tools. IRC needs no extra library.

Binary:

```text
build/Release/bin/calantas
```

CMake defaults `nappgui_DIR` to `/usr/local/nappgui/cmake` on Linux/macOS and `C:/nappgui/cmake` on Windows. Set `NAPPGUI_DIR` to override the SDK prefix. Setup scripts clone the NAppGUI source into `~/.cache/calantas/nappgui_src` so the repository stays clean. The XMPP backend is enabled only when libstrophe is found; use `-DCALANTAS_ENABLE_XMPP=OFF` to force a local-only build.

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

## Session checkpoint — 2026-11-25

Where we stopped:

- All edits are saved in `/home/veto/webs/berg/calantas`. Nothing was committed; the user handles git.
- The last user-tested Linux build had XMPP enabled and connected successfully. Outgoing direct messages worked; incoming messages did not appear.
- Cause found: libstrophe `xmpp_handler` callbacks must return `int` and return nonzero to stay registered. The message, presence and IQ handlers were `void`. They were changed to `int` and now return `1`.
- The user then asked to save passwords. Password persistence was added:
  - `Backend` stores `password[128]`.
  - State file writes and restores `P|...`.
  - `cal_backend_account_password()` was added to `backend.h`.
  - The Account dialog prefills the saved XMPP password.
  - Selecting the XMPP backend auto-connects when a saved JID and password exist.
  - Password is plain text with `0600` permissions on Unix; no keyring/encryption.
- The CMake mix of keyword and plain `target_link_libraries()` was fixed by using NAppGUI's plain signature:
  ```cmake
  target_link_libraries(calantas ${CALANTAS_XMPP_TARGET})
  ```
- `ask.sh` task 8 was added: full dependency install for NAppGUI, libstrophe, TDLib, libcurl, cJSON, OpenSSL, Expat, zlib, pkg-config and build tools. `ask.d/install_all.sh` handles apt, dnf, pacman, Homebrew and vcpkg.
- macOS still has a stale `src/CMakeLists.txt`: its configure output showed the old `PRIVATE` link error. The Linux file is correct; the user must copy the updated files to `/Users/veto/webs/calantas`.

Next steps when returning:

1. On Linux, run task **6 Clean** then task **4 Build & run**. Confirm configure still shows:
   ```text
   XMPP backend: enabled (PkgConfig::PC_STROPHE)
   ```
2. Select **Accounts → Use XMPP Backend**. With the saved JID/password it should auto-connect and show `Connecting to XMPP`, then `XMPP connected`.
3. Open the conversation window for the test buddy. Send from the other XMPP client and verify Calantas shows the incoming message and status line.
4. Verify roster and presence again after the handler fix.
5. On macOS, copy these updated files from Linux before rebuilding:
   ```text
   src/CMakeLists.txt
   src/xmpp.c
   src/backend.c
   src/backend.h
   src/gui.c
   ask.sh
   ask.d/
   README.md
   AGENTS.md
   plan.md
   ```
   Then run task **6**, then task **4**.
6. If incoming messages still fail, next investigation points are:
   - confirm the other client sends to the Calantas JID and roster subscription is accepted both ways;
   - confirm the status line changes to `Message received`;
   - add a temporary libstrophe logger to `xmpp.c` if needed;
   - check `~/.config/calantas/state-xmpp.txt` for `J|`, `P|`, `R|` and buddy records.
7. After successful tests, update the "Verified by the user" and "Not verified yet" lists near the top of this file.
