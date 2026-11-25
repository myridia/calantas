#!/bin/sh
set -e

cd "$(dirname "$0")"
rm -rf build dist

case "$(uname)" in
  Darwin) NAPPGUI_PREFIX="${NAPPGUI_DIR:-/usr/local/nappgui}" ;;
  MINGW*|MSYS*|CYGWIN*) NAPPGUI_PREFIX="${NAPPGUI_DIR:-C:/nappgui}" ;;
  *) NAPPGUI_PREFIX="${NAPPGUI_DIR:-/usr/local/nappgui}" ;;
esac

if [ "$(uname)" = "Darwin" ]; then
  OPENSSL_ARG=""
  LIBSTROPHE_ARG=""
  if command -v brew >/dev/null 2>&1; then
    OPENSSL_ROOT=$(brew --prefix openssl@3 2>/dev/null || true)
    if [ -n "$OPENSSL_ROOT" ]; then
      OPENSSL_ARG="-DOPENSSL_ROOT_DIR=$OPENSSL_ROOT"
    fi
    LIBSTROPHE_PREFIX=$(brew --prefix libstrophe 2>/dev/null || true)
    if [ -n "$LIBSTROPHE_PREFIX" ]; then
      LIBSTROPHE_ARG="-DCMAKE_PREFIX_PATH=$LIBSTROPHE_PREFIX"
      PKG_CONFIG_PATH="$LIBSTROPHE_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
      export PKG_CONFIG_PATH
    fi
  fi

  cmake -G Xcode -S . -B build -DCMAKE_INSTALL_PREFIX="$NAPPGUI_PREFIX" -Dnappgui_DIR="$NAPPGUI_PREFIX/cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DDEPLOYMENT_TARGET_OSX=14.0 $OPENSSL_ARG $LIBSTROPHE_ARG
  cmake --build build --config Release
else
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$NAPPGUI_PREFIX" -Dnappgui_DIR="$NAPPGUI_PREFIX/cmake"
  cmake --build build
fi

if [ -x "build/Release/bin/calantas" ]; then
  "build/Release/bin/calantas"
elif [ -x "build/Release/bin/calantas.exe" ]; then
  "build/Release/bin/calantas.exe"
elif [ -x "build/Release/bin/calantas.app/Contents/MacOS/calantas" ]; then
  "build/Release/bin/calantas.app/Contents/MacOS/calantas"
elif [ -d "build/Release/bin/calantas.app" ]; then
  open "build/Release/bin/calantas.app"
else
  printf "No calantas binary found.\n"
  exit 1
fi
