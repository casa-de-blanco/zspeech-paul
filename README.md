# zspeech-paul

Generate WAV audio from NeoSpeech VoiceText's "Paul" voice in a Linux
container. Headless — no GUI, no display, no SAPI5 needed at runtime.

## Requirements

You need your own legally-obtained copy of the `VT-Paul-M16` installer
(InstallShield package). It is **not included in this repo** — it's
proprietary NeoSpeech/VoiceWare software, licensed as a runtime engine
that (per its own EULA) prohibits redistributing generated audio output
without a separate agreement. Place these files in the repo root before
building:

```
setup.exe  ISSetup.dll  layout.bin  setup.ini  setup.inx
data1.cab  data1.hdr  data2.cab
0x0409.ini  0x0411.ini  0x0412.ini  setup.bmp
```

(`.gitignore` already excludes these — don't commit them.)

Build needs `linux/amd64`. On Apple Silicon, enable Docker Desktop's
"Use Rosetta for x86/amd64 emulation" (Settings → General) first — plain
QEMU emulation is dramatically slower for this build's dpkg-heavy Debian
package installs.

## Build

```bash
docker build --platform linux/amd64 -t zspeech-paul .
```

This runs a one-time automated InstallShield install (via Xvfb + fluxbox +
xdotool) inside the build, so it takes a while (large voice database
copy). No interaction needed.

## Use

```bash
docker run --platform linux/amd64 -d --name zspeech-paul -v zspeech-output:/output zspeech-paul
docker exec zspeech-paul synth "Text to speak" /output/out.wav
docker cp zspeech-paul:/output/out.wav ./out.wav
```

Output: 16-bit PCM WAV, mono, 16kHz.

## How it works / background

See `CLAUDE.md` for the full technical writeup — what `vt_eng.dll`
actually is, why Wine is unavoidable, how the undocumented C API was
recovered, and Wine-specific gotchas hit along the way.
