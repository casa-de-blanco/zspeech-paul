#!/bin/bash
set -e

# Synthesis via vtwav.exe needs no display at all -- Xvfb/x11vnc are only
# needed for a one-time interactive reinstall of VT-Paul-M16 (InstallShield
# GUI), not for normal `synth` calls. Start on demand with:
#   docker exec <container> bash -c 'rm -f /tmp/.X99-lock /tmp/.X11-unix/X99; Xvfb :99 -screen 0 1024x768x16 & x11vnc -display :99 -forever -nopw -shared &'

if [ "$#" -eq 0 ]; then
  exec sleep infinity
else
  exec "$@"
fi
