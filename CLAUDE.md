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
  install + synthesis working with zero network access. The empty
  placeholder files (`verification.txt`, `class.idx`, `classhp.idx`,
  `db_build.date`) the Dockerfile pre-creates are **not** what's causing
  the demo watermark — investigated and ruled out (see below).
- **Every synthesis call prepends a spoken demo/trial disclaimer** before
  the requested text (confirmed via STT transcription of generated output
  — exact wording varies per call, e.g. "This is a demo of the Voice Text
  English TTS system." / "You do not have a valid verification file..." /
  "This version is strictly for demonstration purposes only..."). This
  install is not fully licensed, and — as far as this investigation could
  determine — **there is no way to get `vt_eng.dll` itself to stop
  producing the disclaimer** with only the files in this installer
  package (see the license-mechanism dead ends below). What *is* fixed:
  `vtwav.exe` now strips the disclaimer from its output before writing
  the final file, by exact-matching and cutting a pre-captured reference
  clip off the front of the synthesized audio — see "Watermark removal"
  below. `synth`'s output is clean; only the underlying engine call still
  runs in demo mode internally.
  - `data-common/verify/verification.txt` content *does* affect which
    disclaimer variant plays (tested: absent vs. empty vs. garbage content
    all produce different watermark wording/length), so the file is real
    and read — but nothing in this package ever writes a valid one. Tested
    exhaustively: a completely untouched fresh install (no Dockerfile
    workaround applied at all) never crashes and never auto-creates
    `verification.txt`, whether synthesizing via the minimal `vtwav.exe`
    wrapper *or* the full `VTEditor_ENG.exe` GUI's own "Read" (playback)
    command — confirmed by diffing the entire Wine prefix for
    newly-written files across a synthesis call. No serial/license-key
    entry screen exists anywhere in the InstallShield wizard (stepped
    through it screenshot-by-screenshot). The Start Menu's "Verification
    Center" shortcut (`bin/Verification Center.lnk`) is not a local tool —
    it's a shortcut to `iexplore.exe` pointing at NeoSpeech's (now-defunct,
    company acquired) web portal, i.e. exactly the "contact our sales
    support team" the watermark itself references.
  - `vt_eng.dll` exports `VT_CheckLicense_ENG`, `VT_GetLicenseInfo_ENG`,
    `VT_GetLicenseComment_ENG` (disassembly, pefile-verified export
    RVAs — objdump's raw name-pointer-table order is alphabetical and
    does *not* line up positionally with the address table, easy to get
    this wrong) — these are read-only parsers of an *existing* license
    string, not activation calls. `VT_INIT_ENG` is a dead stub
    (unconditionally returns -1). `VT_VerifyTTS_ENG` is a thin 4-arg
    passthrough to an internal function of unclear purpose. None of the
    five populates `verification.txt` or flips any licensed-mode state.
  - Conclusion: this installer package (the 12 files in `README.md`'s
    list) is everything NeoSpeech shipped for this download, and it
    appears to be an evaluation/demo distribution — a real license likely
    required a separate certificate/key artifact from NeoSpeech (common
    for enterprise TTS engines of this era) that either was never part of
    what's preserved here or is unrecoverable now that NeoSpeech is
    defunct. **Do not attempt to hand-craft `verification.txt` content by
    reverse-engineering its expected format** — that would be forging
    license data (keygen territory), not restoring a real license, and is
    out of scope regardless of how this package was obtained. If a genuine
    license certificate/key file surfaces separately, that would remove
    the *need* for the watermark-stripping workaround below, but isn't
    required for `synth` to produce clean output.

## Watermark removal

Since the watermark can't be turned off at the source, `vtwav.exe` strips
it after the fact. This works because synthesis is byte-deterministic:
the same text + params always produces byte-identical PCM output, and the
demo disclaimer is drawn from a small fixed set of variants (8 observed
across ~140 samples during investigation — first 5 appeared within the
first 8 tries, all 8 by ~40) rather than being generated per-call from
whatever text was requested.

- **Capture** (Dockerfile, build time only): whitespace-only input (`" "`)
  still triggers the watermark, but there's no real text to speak
  afterward, so the output is a pure, isolated watermark clip. `vtwav.exe
  --capture-watermark <out.pcm>` synthesizes `" "` and saves just the raw
  PCM payload (WAV header stripped). The Dockerfile calls this 100 times,
  dedupes by content hash, and saves the unique results as
  `bin/refwm/refwm_0.pcm`, `refwm_1.pcm`, ... (8 files as of this writing).
- **Strip** (`vtwav.exe`, every real synthesis call): after
  `VT_TextToFile_ENG` writes the WAV file, `strip_watermark()` in
  `vtwav.c` reads it back, checks the PCM payload's prefix against each
  `refwm_N.pcm` for an exact byte match, cuts off the longest match, and
  rewrites the WAV header's size fields for the shorter file. If *no*
  reference matches — e.g. a rare 9th variant this build never captured —
  it fails loudly (nonzero exit, clear stderr message) instead of
  silently shipping watermarked or corrupted audio. If that ever happens,
  regenerate `bin/refwm/` (bump the capture loop's iteration count if a
  new variant keeps evading it).
- Verified end-to-end with STT transcription of `synth` output across
  multiple distinct sentences: no watermark wording present, only the
  requested text.

## VTML / pronunciation control

`vt_eng.dll` supports **VTML** (VoiceText Markup Language) -- an XML-ish
tag set NeoSpeech documented for controlling pronunciation, phonemes,
pauses, and prosody inline in the text passed to `VT_TextToFile_ENG`.
Confirmed via the official NeoSpeech "VTML Tag Set User's Guide v3.9",
found through eas.tools/tts-docs.html →
`assets/tts_backend_codes/vtml.pdf` (that page 403s to non-browser HTTP
clients incl. WebFetch -- use a browser-UA `curl` to fetch it).

- **VTML tags are parsed unconditionally by `synth`/`vtwav.exe` today --
  no code change was needed.** `VT_TextToFile_ENG`'s `texttype` param
  (the 10th arg) looked like the obvious candidate for an "enable VTML
  parsing" switch, but empirical testing found it has **no effect**:
  probing every value from `-1` to `3` with a `<vtml_pause time="5000"/>`
  tag and a phoneme override on a one-letter word produced byte-identical
  results at every value (pause added ~5s of audio; the phoneme override
  produced ~3x the duration of the plain letter, both regardless of
  `texttype`). So `vtwav.c` keeps `texttype=-1` like the other unused
  optional params -- there's no separate value to set.
- Key tags: `<vtml_phoneme alphabet="..." ph="...">word</vtml_phoneme>`,
  `<vtml_pause time="msec"/>`, `<vtml_pitch value="50-200">...</vtml_pitch>`,
  `<vtml_speed value="50-400">...</vtml_speed>`,
  `<vtml_volume value="0-500">...</vtml_volume>`,
  `<vtml_sub alias="...">text</vtml_sub>`,
  `<vtml_sayas interpret-as="..." format="...">text</vtml_sayas>`,
  `<vtml_break level="0-3"/>`, `<vtml_partofsp part="...">text</vtml_partofsp>`.
- `alphabet="x-cmu"` (CMU/ARPAbet with stress digits, e.g.
  `"T AH0 K EY1 M AH"` for "Tekamah") is the most human-writable phoneme
  alphabet for English; `ipa` (decimal Unicode codepoints) is also
  supported but far less writable by hand. `x-worldbet`/`x-sampa`/`x-sapi`
  are also accepted for English; `x-pentax`/`x-pinyin` are Japanese/Chinese
  -only, not usable here.
- Per-tag limits (English synthesizer): enclosed text/`alias`/`ph` max 512
  bytes; `ph` value max 64 phonetic symbols.
- The engine's built-in text-normalization already correctly expands
  numbers, dates, times, currency, measures (`mph`, `°F`), addresses, and
  common abbreviations on its own (see the VTML guide's Appendix B) -- so
  VTML is only needed for proper nouns/acronyms the normalizer gets wrong
  and for deliberate pacing/emphasis, not for numeric/unit formatting.
- Verified end-to-end against the actual published
  `registry.verde.zoe/library/zspeech-paul:1.1.0` image: a mixed
  `<vtml_pause>` + `<vtml_phoneme>` narration string round-tripped through
  `synth` into a valid, watermark-stripped, correctly-lengthened WAV (see
  git history for the exact test if this needs re-verifying).

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
  Reads the proprietary installer files from a `VOICE_DIR` build-context
  subdirectory (default `paul/`, not repo root) via a whole-directory
  `COPY` -- each voice's installer file set can differ (see Violeta below).
- `install-automation.sh` — xdotool-driven InstallShield automation
  (Welcome → License → Destination → poll for ~490MB copy → Finish).
- `vtwav.c` — the direct `vt_eng.dll` caller, compiled at build time.
- `synth.sh` / `entrypoint.sh` — the runtime `synth "text" /out.wav` CLI.

### Voice parameterization (Paul + Violeta, not a general framework)

`Dockerfile`/`install-automation.sh`/`synth.sh`/`vtwav.c` take
`VOICE_DIR`/`VOICE_NAME`/`VOICE_MODEL`/`SPEAKER_ID`/`DLL_NAME`/
`EXPORT_SUFFIX`/`BIN_SUBDIR`/`EXPECTED_INSTALL_MB` as build args. `Taskfile.yml`
picks the right values for `task build VOICE=paul` / `VOICE=violeta` — see
its `vars:` block for the exact values. This is deliberately scoped to
these two voices, not a speculative N-voice framework: Violeta's package
turned out to differ from Paul's in more than just naming, discovered by
directly inspecting its InstallShield cabinets with `unshield`/`strings`
(both installed via `brew` for this investigation) rather than by running
the installer:

- Engine DLL is `vt_spa_violeta16.dll` (fully voice-specific filename, not
  a generic `vt_spa.dll`), confirmed via `unshield l violeta/data1.cab`.
  It ships a real SAPI5 wrapper too (`vtspasapi50.dll`) — unlike Paul,
  where the SAPI5 branding is cosmetic (see top of this file) — but the
  direct-DLL approach still applies; we just never touch `vtspasapi50.dll`.
- Installs under `App_Executables\lib\`, not `App_Executables\bin\` like
  Paul — hence `BIN_SUBDIR`.
- Export function suffix is `_SPA` (`EXPORT_SUFFIX`) — confirmed by
  `i686-w64-mingw32-objdump -p vt_spa_violeta16.dll`'s export table
  against a real completed install (`VT_LOADTTS_SPA`, `VT_UNLOADTTS_SPA`,
  `VT_TextToFile_SPA` all present), not just inferred by analogy.
- Single-language installer: `setup.ini`'s `[Languages]` section is
  `Supported=0x0409` only, and unlike Paul's `data1.cab` (which contains
  `<Support>0x0411/0x0412 String Tables>` entries — confirmed via
  `unshield l`, and why Paul needs those two extra `.ini` files at all),
  Violeta's cab has none. So Violeta's `violeta/` directory only needs
  `0x0409.ini`, not all three.
- Its installer file set spans a real second disk (`data3.cab`, listed in
  `layout.bin`'s own manifest, readable via `strings`) — received as
  separate `Disk1`/`Disk2` folders and flattened into one `violeta/`
  directory before building. Confirmed no disk-swap dialog appears once
  flattened (same as Paul's own `data1.cab`/`data2.cab` pair) — a plain
  `install-automation.sh` run (no disk-swap handling) completed the copy.
- **Real install destination is `C:\Program Files\VW\VT\Violeta\M16-SAPI5`
  — not `...\Violeta\M16`.** The InstallShield product name
  (`VT-Violeta-M16-SAPI5`) carries a `-SAPI5` suffix into the actual
  install dir that Paul's product name (`VT-Paul-M16`) does not. This was
  the reason the first real build attempt failed silently (the copy-size
  poll loop watched the wrong path and saw 0MB for the entire ~19-minute
  install, resending keystrokes that were already landing correctly on
  the real wizard). Found by driving a debug container's installer
  interactively (`xdotool` + `xwd`/`convert` screenshots piped through
  `docker cp`) instead of guessing again — the Destination Folder screen
  shows the real path directly. `VOICE_MODEL=M16-SAPI5` in `Taskfile.yml`
  reflects this. Note the install's inner `data-violeta/` subtree still
  uses the plain `M16` (no `-SAPI5`) for its nested model dir — the
  Dockerfile's placeholder-touch step strips any `-VOICE_MODEL` suffix
  (`${VOICE_MODEL%%-*}`) to account for this split.
- `SPEAKER_ID=1` confirmed correct by successfully running `vtwav.exe`
  against the real install (`VT_LOADTTS_SPA`/`VT_TextToFile_SPA` both
  succeeded, produced a valid 16-bit PCM WAV).
- Two other copies (`paul2/`, `violeta2/`) were investigated and discarded:
  `paul2/` was a different, incomplete InstallShield packaging of the
  exact same Paul voice data (byte-identical `data1.cab`/`data2.cab`
  sizes); `violeta2/` was missing the engine DLL and GUI tools entirely in
  both its cabs, with a `merged-gen.dat` (the core voice model) only ~8%
  the size of `violeta/`'s — not installable, not just a smaller edition.

## Verification

`docker run -d --name zspeech-paul -v out:/output zspeech-paul && docker exec zspeech-paul synth "test" /output/test.wav`
then check the result is a valid, non-silent RIFF/WAVE 16-bit PCM file
(`file` command should say exactly that; a silent/near-empty file usually
means the `db_path`/`licensefile`/speaker-ID combination broke again).
Also spot-check that the output contains *only* the requested text — no
demo-disclaimer wording at the start (see "Watermark removal" above; a
build's `bin/refwm/` reference set can in principle miss a rare variant,
in which case `synth` fails loudly rather than producing a watermarked
file, but worth an occasional listen/transcription check anyway).

Final built image: 992MB (down from an earlier 4.75GB `docker commit`-based
prototype that still had the abandoned SAPI5/balcon/winetricks approach and
build tooling baked in).

## Where this is actually used

Consumed by `zoe-infra-v2`'s `weather-report-generator` Argo WorkflowTemplate
(`paul-tts` template) for narration. That repo can't build this image via
its in-cluster `buildkit` WorkflowTemplate — the Dockerfile needs the
`.gitignore`d proprietary installer files — so it's built and pushed
locally to `registry.verde.zoe/library/zspeech-paul:<tag>` instead. See
that repo's `kubernetes/apps/argo-workflows/manifests/weather-report-generator/CLAUDE.md`
for the consumer-side details.
