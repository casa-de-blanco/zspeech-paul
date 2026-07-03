#!/bin/bash
# One-time automated install of VT-Paul-M16 via the InstallShield GUI wizard.
# Needs a real window manager (fluxbox) -- bare Xvfb causes Wine dialogs to
# render at unusable off-screen coordinates.
set -e

log() { echo "[$(date +%T)] $*"; }

rm -f /tmp/.X99-lock /tmp/.X11-unix/X99
log "starting Xvfb"
Xvfb :99 -screen 0 1024x768x16 &
sleep 3
log "starting fluxbox"
DISPLAY=:99 fluxbox >/tmp/fluxbox.log 2>&1 &
sleep 3

log "wineboot --init"
DISPLAY=:99 timeout 60 wine wineboot --init
log "wineboot done"

log "launching setup.exe"
DISPLAY=:99 wine /installer/setup.exe &
log "setup.exe launched (pid $!)"

# InstallShield's extraction + first-window paint time is variable (seen
# both ~10s and 20s+ in testing) -- wait for the wizard window to actually
# exist rather than a fixed sleep, since a keypress sent before the window
# is focused/ready is silently lost and desyncs every screen after it.
log "waiting for wizard window to appear"
for i in $(seq 1 30); do
  if DISPLAY=:99 xdotool search --name "InstallShield Wizard" >/dev/null 2>&1; then
    log "  wizard window found after ${i}s"
    break
  fi
  sleep 1
done
sleep 3

# Keystrokes sent from within this script sometimes silently fail to reach
# the wizard window (root cause not fully pinned down -- reproduced even
# outside a Dockerfile RUN step, so it's not a build-step-specific quirk;
# the *same* keystroke sent as a fresh, separate invocation after enough
# real time has passed always works). So: click through with generous
# waits, then treat "no file growth yet" as a signal to just resend the
# whole click sequence again rather than assuming something is stuck.
click_through() {
  log "sending Return (Welcome -> Next)"
  DISPLAY=:99 timeout 10 xdotool key Return
  sleep 10
  log "sending alt+y (License -> Yes)"
  DISPLAY=:99 timeout 10 xdotool key alt+y
  sleep 10
  log "sending Return (Destination -> Next)"
  DISPLAY=:99 timeout 10 xdotool key Return
  sleep 10
}

TARGET="/wine/drive_c/Program Files/VW/VT/Paul/M16"
click_through

log "polling for file copy to reach ~480MB (resending click sequence if no progress)"
attempt=1
for i in $(seq 1 120); do
  SIZE=$(du -sm "$TARGET" 2>/dev/null | cut -f1 || echo 0)
  log "  poll $i (attempt $attempt): ${SIZE:-0}MB"
  if [ "${SIZE:-0}" -ge 480 ]; then
    break
  fi
  # After ~40s of zero progress, assume the click sequence was lost and
  # resend it -- harmless if we're actually already past those screens
  # (Return/alt+y land on nothing relevant once the Setup Status screen
  # is showing).
  if [ "${SIZE:-0}" -eq 0 ] && [ $((i % 8)) -eq 0 ]; then
    attempt=$((attempt + 1))
    log "  no progress yet, resending click sequence (attempt $attempt)"
    click_through
  fi
  sleep 5
done
sleep 10  # let the wizard reach the Finish screen after the last file write

log "sending Return (Finish)"
DISPLAY=:99 timeout 10 xdotool key Return
sleep 5

log "verifying install"
test -f "$TARGET/bin/vt_eng.dll"
log "done"
