#!/bin/sh

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$SCRIPT_DIR"

show_menu() {
  printf "%s\n" '  ┌─────┬──────────────────────────────────────────────┐'
  printf "%s\n" '  │  ID │ Description                                  │'
  printf "%s\n" '  ├─────┼──────────────────────────────────────────────┤'
  printf "%s\n" '  │  1  │ Install NAppGUI - Windows (C:/nappgui)       │'
  printf "%s\n" '  │  2  │ Install NAppGUI - macOS (/usr/local/nappgui) │'
  printf "%s\n" '  │  3  │ Install NAppGUI - Linux (/usr/local/nappgui) │'
  printf "%s\n" '  │  4  │ Build & run app (./build.sh)                 │'
  printf "%s\n" '  │  5  │ Run app (./run.sh)                           │'
  printf "%s\n" '  │  6  │ Clean build files (build/, dist/)            │'
  printf "%s\n" '  │  7  │ Install libstrophe + XMPP dependencies       │'
  printf "%s\n" '  │  8  │ Install all dependencies (SDK + backends)    │'
  printf "%s\n" '  │  0  │ Exit                                         │'
  printf "%s\n" '  └─────┴──────────────────────────────────────────────┘'
}

i_install_nappgui_darwin() {
  NAPPGUI_SRC="${NAPPGUI_SRC:-$HOME/.cache/calantas/nappgui_src}"
  mkdir -p "$(dirname "$NAPPGUI_SRC")" || return 1
  if [ ! -d "$NAPPGUI_SRC" ]; then
    git clone --depth 1 https://github.com/frang75/nappgui_src.git "$NAPPGUI_SRC" || return 1
  fi

  cd "$NAPPGUI_SRC" || return 1

  OPENSSL_ARG=""
  if command -v brew >/dev/null 2>&1; then
    brew install openssl@3 || return 1
    OPENSSL_ROOT=$(brew --prefix openssl@3 2>/dev/null || true)
    if [ -n "$OPENSSL_ROOT" ]; then
      OPENSSL_ARG="-DOPENSSL_ROOT_DIR=$OPENSSL_ROOT"
    fi
  fi

  cmake -G Xcode -S . -B build -DNAPPGUI_DEMO=NO -DDEPLOYMENT_TARGET_OSX=14.0 $OPENSSL_ARG || return 1
  cmake --build build --config Release -j 4 || return 1
  sudo cmake --install build --config Release --prefix /usr/local/nappgui || return 1

  if [ ! -f "/usr/local/nappgui/cmake/nappgui-config.cmake" ]; then
    printf "  NAppGUI install failed: /usr/local/nappgui/cmake/nappgui-config.cmake was not created\n" >&2
    return 1
  fi
}

i_install_nappgui_linux() {
  sudo apt-get update || return 1
  sudo apt-get install build-essential git cmake libgtk-3-dev mesa-common-dev libglu1-mesa-dev -y || return 1

  NAPPGUI_SRC="${NAPPGUI_SRC:-$HOME/.cache/calantas/nappgui_src}"
  mkdir -p "$(dirname "$NAPPGUI_SRC")" || return 1
  if [ ! -d "$NAPPGUI_SRC" ]; then
    git clone --depth 1 https://github.com/frang75/nappgui_src.git "$NAPPGUI_SRC" || return 1
  fi

  cd "$NAPPGUI_SRC" || return 1
  cmake -S . -B build -DNAPPGUI_DEMO=NO -DCMAKE_BUILD_TYPE=Release || return 1
  cmake --build build -j 4 || return 1
  sudo cmake --install build --config Release --prefix /usr/local/nappgui || return 1

  if [ ! -f "/usr/local/nappgui/cmake/nappgui-config.cmake" ]; then
    printf "  NAppGUI install failed: /usr/local/nappgui/cmake/nappgui-config.cmake was not created\n" >&2
    return 1
  fi
}

i_install_nappgui_windows() {
  NAPPGUI_SRC="${NAPPGUI_SRC:-$HOME/.cache/calantas/nappgui_src}"
  mkdir -p "$(dirname "$NAPPGUI_SRC")" || return 1
  if [ ! -d "$NAPPGUI_SRC" ]; then
    git clone --depth 1 https://github.com/frang75/nappgui_src.git "$NAPPGUI_SRC" || return 1
  fi

  cd "$NAPPGUI_SRC" || return 1
  cmake -S . -B build -DNAPPGUI_DEMO=NO || return 1
  cmake --build build --config Release -j 4 || return 1
  cmake --install build --config Release --prefix C:/nappgui || return 1

  if [ ! -f "C:/nappgui/cmake/nappgui-config.cmake" ]; then
    printf "  NAppGUI install failed: C:/nappgui/cmake/nappgui-config.cmake was not created\n" >&2
    return 1
  fi
}

i_launch_app() {
  cd "$ROOT" || return 1
  if [ -x "build/Release/bin/calantas" ]; then
    "build/Release/bin/calantas"
  elif [ -x "build/Release/bin/calantas.exe" ]; then
    "build/Release/bin/calantas.exe"
  elif [ -x "build/Release/bin/calantas.app/Contents/MacOS/calantas" ]; then
    "build/Release/bin/calantas.app/Contents/MacOS/calantas"
  elif [ -d "build/Release/bin/calantas.app" ]; then
    open "build/Release/bin/calantas.app"
  else
    printf "  No calantas binary found. Build first with task 4.\n"
    return 1
  fi
}

i_build_app() {
  cd "$ROOT" || return 1
  rm -rf build dist

  case "$(uname)" in
    Darwin) NAPPGUI_PREFIX="${NAPPGUI_DIR:-/usr/local/nappgui}" ;;
    MINGW*|MSYS*|CYGWIN*) NAPPGUI_PREFIX="${NAPPGUI_DIR:-C:/nappgui}" ;;
    *) NAPPGUI_PREFIX="${NAPPGUI_DIR:-/usr/local/nappgui}" ;;
  esac

  if [ "$(uname)" = "Darwin" ]; then
    OPENSSL_ARG=""
    if command -v brew >/dev/null 2>&1; then
      OPENSSL_ROOT=$(brew --prefix openssl@3 2>/dev/null || true)
      if [ -n "$OPENSSL_ROOT" ]; then
        OPENSSL_ARG="-DOPENSSL_ROOT_DIR=$OPENSSL_ROOT"
      fi
    fi

    cmake -G Xcode -S . -B build -DCMAKE_INSTALL_PREFIX="$NAPPGUI_PREFIX" -Dnappgui_DIR="$NAPPGUI_PREFIX/cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDEPLOYMENT_TARGET_OSX=14.0 $OPENSSL_ARG || return 1
    cmake --build build --config Release || return 1
  else
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$NAPPGUI_PREFIX" -Dnappgui_DIR="$NAPPGUI_PREFIX/cmake" || return 1
    cmake --build build || return 1
  fi

  i_launch_app
}

i_clean_build() {
  cd "$ROOT" || return 1
  rm -rf build dist
  printf "  Build files removed.\n"
}

i_install_libstrophe() {
  path="$SCRIPT_DIR/ask.d/install_libstrophe.sh"
  if [ -f "$path" ]; then
    if [ -x "$path" ]; then
      "$path"
    else
      sh "$path"
    fi
    return $?
  fi

  case "$(uname -s)" in
    Darwin)
      brew install pkg-config libstrophe openssl@3 expat
      ;;
    Linux)
      if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y pkg-config libstrophe-dev libssl-dev libexpat1-dev zlib1g-dev
      else
        printf "Install libstrophe, OpenSSL, Expat, zlib and pkg-config manually.\n"
        return 1
      fi
      ;;
    *)
      printf "Install libstrophe, OpenSSL, Expat, zlib and pkg-config manually.\n"
      return 1
      ;;
  esac
}

i_install_all() {
  path="$SCRIPT_DIR/ask.d/install_all.sh"
  if [ -f "$path" ]; then
    if [ -x "$path" ]; then
      "$path"
    else
      sh "$path"
    fi
    return $?
  fi

  case "$(uname -s)" in
    Darwin)
      brew install cmake git pkg-config openssl@3 expat libstrophe curl cjson tdlib
      run_task 2 install_nappgui_macos.sh
      ;;
    Linux)
      if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y build-essential git cmake pkg-config libgtk-3-dev mesa-common-dev libglu1-mesa-dev libstrophe-dev libssl-dev libexpat1-dev zlib1g-dev libcurl4-openssl-dev libcjson-dev gperf
        run_task 3 install_nappgui_linux.sh
      else
        printf "Install NAppGUI, libstrophe, TDLib, libcurl, cJSON, OpenSSL, Expat, zlib and pkg-config manually.\n"
        return 1
      fi
      ;;
    MINGW*|MSYS*|CYGWIN*)
      vcpkg install libstrophe:x64-windows openssl:x64-windows expat:x64-windows zlib:x64-windows curl:x64-windows cjson:x64-windows tdlib:x64-windows
      run_task 1 install_nappgui_windows.sh
      ;;
    *)
      printf "Install NAppGUI, libstrophe, TDLib, libcurl, cJSON, OpenSSL, Expat, zlib and pkg-config manually.\n"
      return 1
      ;;
  esac
}

run_task() {
  id="$1"
  script="$2"
  path="$SCRIPT_DIR/ask.d/$script"

  if [ -f "$path" ]; then
    if [ -x "$path" ]; then
      "$path"
    else
      sh "$path"
    fi
    return $?
  fi

  case "$id" in
    1) i_install_nappgui_windows ;;
    2) i_install_nappgui_darwin ;;
    3) i_install_nappgui_linux ;;
    4) i_build_app ;;
    5) i_launch_app ;;
    6) i_clean_build ;;
    7) i_install_libstrophe ;;
    8) i_install_all ;;
    *)
      printf "  Missing task script: ask.d/%s\n" "$script"
      return 1
      ;;
  esac
}

printf "\n  calantas — Task Runner\n\n"

while true; do
  show_menu

  printf "  Enter Task ID: "
  read task || exit 0

  if [ "$task" = "0" ]; then
    printf "  Goodbye!\n"
    exit 0
  elif [ "$task" = "1" ]; then
    printf "  ...installing NAppGUI for Windows\n"
    run_task 1 install_nappgui_windows.sh
  elif [ "$task" = "2" ]; then
    printf "  ...installing NAppGUI for macOS\n"
    run_task 2 install_nappgui_macos.sh
  elif [ "$task" = "3" ]; then
    printf "  ...installing NAppGUI for Linux\n"
    run_task 3 install_nappgui_linux.sh
  elif [ "$task" = "4" ]; then
    printf "  ...building and running the app\n"
    run_task 4 build.sh
  elif [ "$task" = "5" ]; then
    printf "  ...running the app\n"
    run_task 5 run.sh
  elif [ "$task" = "6" ]; then
    printf "  ...cleaning build files\n"
    run_task 6 clean.sh
  elif [ "$task" = "7" ]; then
    printf "  ...installing libstrophe and XMPP dependencies\n"
    run_task 7 install_libstrophe.sh
  elif [ "$task" = "8" ]; then
    printf "  ...installing all dependencies\n"
    run_task 8 install_all.sh
  else
    printf "  Unknown task: %s\n" "$task"
  fi

  sleep 2
  printf "\n"
done
