#!/bin/sh
set -e

cd "$(dirname "$0")"

if [ -x "build/Release/bin/calantas" ]; then
  exec "build/Release/bin/calantas"
elif [ -x "build/Release/bin/calantas.exe" ]; then
  exec "build/Release/bin/calantas.exe"
elif [ -x "build/Release/bin/calantas.app/Contents/MacOS/calantas" ]; then
  exec "build/Release/bin/calantas.app/Contents/MacOS/calantas"
elif [ -d "build/Release/bin/calantas.app" ]; then
  open "build/Release/bin/calantas.app"
else
  printf "No calantas binary found. Run ./build.sh first.\n"
  exit 1
fi
