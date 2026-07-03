# zspeech-paul

Docker image that generates WAV audio from NeoSpeech VoiceText's "Paul"
voice (`VT-Paul-M16`), on Linux, headless, no GUI/display at runtime.

## What this actually is

`VT-Paul-M16` is a Windows-only, 32-bit InstallShield package containing
NeoSpeech's proprietary TTS engine (`vt_eng.dll`). It is **not** a SAPI5
voice, despite superficially looking like one (it registers Start Menu
shortcuts, ships a "VoiceText Engine for SAPI 5.1" branded installer flow,
etc). `vt_eng.dll` exports plain C functions (`VT_LOADTTS_ENG`,
`VT_TextToFile_ENG`, `VT_PLAYTTS_ENG`, ...) meant to be called directly by
an application — SAPI5 is not involved anywhere in the actual synthesis
path. This was discovered the hard way: an earlier attempt used
`winetricks speechsdk` + `balcon` (SAPI5 CLI tool) and got nowhere, because
the voice was never a SAPI5 voice to begin with. The bundled `VTEditor_ENG.exe`
GUI tool does use this API, but its own "Save as Wave" dialog reliably fails
to render under Wine (dialog window reports valid geometry but never paints,
even with a real window manager) — so this repo bypasses the GUI entirely
and calls `vt_eng.dll` directly via `vtwav.c`, compiled with
`i686-w64-mingw32-gcc`.

## Why Wine is unavoidable

`vt_eng.dll` and `vtwav.exe` are both Windows PE binaries. Linux's ELF
loader can't execute PE format at all, and nothing else provides the Win32
API surface (`kernel32`, CRT, etc.) they call into. Wine (`wine`/`wine32`
packages) is the minimum needed at runtime — there's no path to a
Wine-free image without a from-source reimplementation of the engine,
which isn't possible without NeoSpeech's source (the company was acquired,
no source access exists).

## `vt_eng.dll`'s API (reverse-engineered, undocumented)

No surviving public docs for the `_ENG` engine specifically (NeoSpeech
defunct/acquired). Signatures were recovered by:
1. Objdump-based disassembly of `vt_eng.dll`'s exports to find the plain
   parameter count/pattern (cdecl convention, confirmed by `ret` with no
   stack-cleanup immediate).
2. Finding a public sample for the sibling Japanese engine (`vt_jpn.dll`,
   same API shape, different function suffix) at
   github.com/loftkun/VoiceText_Sample_Win — this gave exact parameter
   names/order without needing to fully reverse-engineer semantics.

Key facts, verified empirically against the actual binary:
- `VT_LOADTTS_ENG(HWND hWnd, int speakerID, char *db_path, char *licensefile)`
  — `hWnd=NULL` is fine (no window needed). `db_path` must be the engine's
  install root (`C:\Program Files\VW\VT\Paul\M16`), used as a base for
  internal path concatenation — passing a file (like `dblist.idx`) or
  reusing it for both `db_path` and `licensefile` breaks in different ways
  (see git history / prior debugging if this regresses). **Returns 0 on
  success here**, not 1 — inconsistent with `VT_TextToFile_ENG`'s 1=success
  convention. Only negative values are real errors; don't gate on `!= 1`.
- Speaker ID `0` = "Kate" (not present in this install, errors on missing
  `data-kate/` files). Speaker ID `1` = Paul. This engine ships as a
  single-speaker package but the speaker-ID space in the DLL itself
  supports more.
- `VT_TextToFile_ENG(int fmt, char *text, char *filename, int speakerID, int pitch, int speed, int volume, int pause, int dictidx, int texttype)`
  — `fmt` selects from a 10-way internal dispatch (0-9), empirically
  determined by generating one file per value and inspecting headers:
  `fmt=4` is 16-bit PCM RIFF/WAVE (what this repo uses). Others produce
  headerless raw PCM, A-law, or u-law WAV — not obviously named/documented,
  don't assume without testing if adding format options later.
- No online license activation exists for this package — confirmed by full
  install + synthesis working with zero network access. The only
  Wine-specific gotchas are a handful of empty placeholder files
  (`verification.txt`, `class.idx`, `classhp.idx`, `db_build.date`) that
  the engine fails to auto-create on first run under Wine and that the
  Dockerfile pre-creates as a workaround — not a license/activation issue.

## Known Wine gotchas hit while building this

- **`wineserver -w` after `wine wineboot --init` hangs indefinitely** if a
  window manager (fluxbox) is running — `explorer.exe`/`services.exe`
  stay resident as normal wine session processes and never exit on their
  own, so `-w` waits forever. Don't add `wineserver -w` calls to
  `install-automation.sh` without a bounded timeout.
- Bare Xvfb with **no window manager** causes InstallShield dialog windows
  to render at unusable off-screen coordinates (e.g. `-127,-83`) — install
  automation needs `fluxbox` (or similar) running, not just Xvfb.
- Dialogs opened by `VTEditor_ENG.exe` (owner-drawn menus/dialogs) render
  as solid black boxes under Wine even with a WM — status bar text on
  hover still updates correctly (useful for menu navigation via keyboard
  if ever needed again), but the dialogs themselves are visually and
  functionally broken. This is why the direct-DLL approach exists instead.
- **`xdotool key` sent from within a long-running script sometimes never
  reaches the target window** — no error, `xdotool` exits 0, but the
  keystroke is just lost. Reproduced consistently in both a `docker build`
  RUN step and a plain `docker exec -d` script run; the *identical*
  keystroke sent as a fresh, separate `xdotool` invocation after enough
  real time passes always works. Root cause not fully pinned down (some
  X11/window-manager focus-state interaction, not a Wine-specific issue).
  `install-automation.sh`'s `click_through` step works around this
  pragmatically: send the Welcome/License/Destination key sequence, then
  if the install directory shows no file growth after ~40s, just resend
  the whole sequence again (safe/idempotent — resending on a screen
  that's already advanced past those controls is a no-op). Don't remove
  this retry wrapper thinking it's unnecessary; it's load-bearing.

## Files

- `Dockerfile` — multi-stage: builder (wine+xvfb+fluxbox+xdotool+mingw,
  runs the one-time GUI install + compiles `vtwav.exe` + prunes unused
  install-tree files), runtime (wine only + the pruned install tree).
- `install-automation.sh` — xdotool-driven InstallShield automation
  (Welcome → License → Destination → poll for ~490MB copy → Finish).
- `vtwav.c` — the direct `vt_eng.dll` caller, compiled at build time.
- `synth.sh` / `entrypoint.sh` — the runtime `synth "text" /out.wav` CLI.

## Verification

`docker run -d --name zspeech-paul -v out:/output zspeech-paul && docker exec zspeech-paul synth "test" /output/test.wav`
then check the result is a valid, non-silent RIFF/WAVE 16-bit PCM file
(`file` command should say exactly that; a silent/near-empty file usually
means the `db_path`/`licensefile`/speaker-ID combination broke again).
