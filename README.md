<img src="calantas.svg" alt="Calantas Messenger" width="120">

# Calantas Messenger

A basic desktop messenger with the classic Pidgin-style buddy list, written in C with NAppGUI. Calantas uses its own backend interface and does not include `libpurple3`, GTK or Libadwaita.

Calantas uses XMPP through libstrophe for TLS login, roster, presence and direct messages. Multiple XMPP accounts can be saved with their own markers; account details, buddies and recent messages are saved between runs. XMPP credentials live in a permanent `account.json` file.

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

Clean the build output:

```bash
./ask.sh
```

Choose task **6**, or remove `build/` and `dist/` directly.

The binary is written to `build/Release/bin/calantas`.

NAppGUI is expected in `/usr/local/nappgui` on Linux/macOS and `C:/nappgui` on Windows. Override the SDK location with `NAPPGUI_DIR=/path/to/nappgui`. Setup tasks clone the NAppGUI source into `~/.cache/calantas/nappgui_src`, outside the repository, so the project stays clean.

The XMPP backend is optional at build time. CMake detects libstrophe through `pkg-config` or a normal library search. Without it, Calantas still builds but cannot connect.

Run task **7** in `./ask.sh` to install only the XMPP dependencies, or task **8** to install everything Calantas needs. Task 8 installs NAppGUI plus libstrophe, TDLib, libcurl, cJSON, OpenSSL, Expat, zlib, pkg-config and the build tools for the current platform. IRC needs no extra library.

Manual equivalents:

```bash
# Debian/Ubuntu
sudo apt install build-essential git cmake pkg-config libgtk-3-dev mesa-common-dev libglu1-mesa-dev libstrophe-dev libssl-dev libexpat1-dev zlib1g-dev libcurl4-openssl-dev libcjson-dev gperf

# Fedora
sudo dnf install gcc gcc-c++ git cmake pkgconf-pkg-config gtk3-devel mesa-libGL-devel mesa-libGLU-devel libstrophe-devel openssl-devel expat-devel zlib-devel libcurl-devel cjson-devel gperf

# macOS
brew install cmake git pkg-config openssl@3 expat libstrophe curl cjson tdlib
```

Ubuntu and Debian do not ship TDLib as a normal development package, so task 8 builds TDLib from source into `~/.cache/calantas/tdlib_src` and installs `libtdjson` when it is not already available.

On Windows, install the vcpkg packages:

```text
vcpkg install libstrophe:x64-windows openssl:x64-windows expat:x64-windows zlib:x64-windows curl:x64-windows cjson:x64-windows tdlib:x64-windows
```

Then pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`. Use `-DCALANTAS_ENABLE_XMPP=OFF` to disable XMPP detection; the app will build but cannot connect. If libstrophe is missing but you want configuration to fail instead of disabling XMPP, pass `-DCALANTAS_REQUIRE_XMPP=ON`.

## Interface

- Buddy list with status, name, address and status text
- Search/filter field
- Message and Add Buddy buttons
- One conversation window per buddy
- Manage accounts dialog with a table of saved accounts and Add, Edit, Delete and Close buttons
- XMPP account dialog with marker, JID, password, optional server, startup auto-connect and a connection test button
- Accounts menu with one **Manage account** entry; the active account has a `*` marker in the table
- Status menu: Available, Away, Busy, Offline
- Contacts menu with online-only and all-buddies views
- About dialog and platform-style app menu
- One state file per account marker (`state-xmpp.txt` for `XMPP`, otherwise `state-xmpp-MARKER.txt`) for account name, status, buddies and recent messages
- XMPP worker thread with roster, presence and direct-message events

## Structure

- `src/main.c` — `osmain_sync` entry point
- `src/gui.c` / `src/gui.h` — NAppGUI windows, buddy list, conversations, menus and dialogs
- `src/backend.c` / `src/backend.h` — messenger model and text state persistence
- `src/xmpp.c` / `src/xmpp.h` — libstrophe backend and worker-thread event queue
- `src/res/` — application icons
- `CMakeLists.txt` — top-level NAppGUI project
- `src/CMakeLists.txt` — desktop app target
- `build.sh` — configure, build and run
- `run.sh` — run an existing build
- `ask.sh` / `ask.d/` — task runner; task 8 installs the full SDK and backend dependency set
- `.github/workflows/control.yml` — single CI entry point with mcclim-style tag resolution
- `.github/workflows/{linux,mac,windows}.yml` — reusable platform build and release workflows with optional `tag`
- `plan.md` — saved implementation plan and roadmap
- `calantas.svg` — project logo

## Backend plan

The GUI talks only to `backend.h`, so real protocols can be added without changing the interface:

| Backend | Status |
|---|---|
| XMPP + libstrophe | Implemented, optional build |
| IRC | Planned |
| Telegram + TDLib | Planned |
| ActivityPub via Mastodon API | Planned |
| Raw ActivityPub | Planned later |

Open **Accounts → Manage account** to see the saved accounts in a table. **Add** opens an XMPP account dialog where you enter a marker, JID and password; **Edit** changes the selected account; **Delete** removes it; **Close** returns to the buddy list. Double-click a row to switch to that account. Each marker is a separate saved account/server with its own roster and recent messages. The marker, JID, password, optional server override and auto-connect flag are kept in `account.json`; roster and recent messages stay in `state-xmpp-MARKER.txt`. The password is stored in plain text with user-only file permissions on Linux/macOS; it is not encrypted. The active account is prefixed with `*` in the table. Check **Connect automatically on startup (XMPP)** to connect with the active saved account when Calantas starts. On first run, credentials from an older `state-xmpp.txt` are migrated into `account.json`. Use **Test** in the XMPP account dialog to verify the JID, password and server before saving.

There is no libpurple3 adapter and no plan to add one.

## License

GPL-3.0. See `LICENSE`.
