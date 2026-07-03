#!/bin/bash
# usage: synth "text to speak" /output/out.wav
set -e

TEXT="$1"
OUT="$2"

if [ -z "$TEXT" ] || [ -z "$OUT" ]; then
  echo "usage: synth \"text\" /output/out.wav" >&2
  exit 1
fi

BINDIR="$WINEPREFIX/drive_c/Program Files/VW/VT/Paul/M16/bin"
WINOUT="Z:$(echo "$OUT" | tr / '\\')"

cd "$BINDIR"
exec wine vtwav.exe "$TEXT" "$WINOUT"
