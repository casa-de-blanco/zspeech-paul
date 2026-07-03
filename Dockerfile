# Copy your own legally-obtained VT-Paul-M16 installer files into this
# directory first (see README.md), then:
#   docker build --platform linux/amd64 -t zspeech-paul .

FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive \
    WINEARCH=win32 \
    WINEPREFIX=/wine \
    WINEDEBUG=-all \
    DISPLAY=:99

# xvfb/fluxbox/xdotool: drive the one-time InstallShield GUI installer.
# gcc-mingw-w64-i686: compile vtwav.c into a Windows PE exe.
# None of this is needed at runtime -- it stays out of the final stage.
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        wine wine32 \
        xvfb fluxbox xdotool \
        gcc-mingw-w64-i686 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY setup.exe ISSetup.dll layout.bin setup.ini setup.inx data1.cab data1.hdr data2.cab \
     0x0409.ini 0x0411.ini 0x0412.ini setup.bmp \
     /installer/

COPY install-automation.sh /install-automation.sh
RUN chmod +x /install-automation.sh && /install-automation.sh

# Placeholder cache/scratch files vt_eng.dll fails to auto-create on first
# run under Wine (not a license issue -- confirmed no online activation is
# required at all; these are just empty files the engine expects to exist).
RUN VT="/wine/drive_c/Program Files/VW/VT/Paul/M16" && \
    touch "$VT/data-common/verify/verification.txt" \
          "$VT/data-paul/M16/class.idx" \
          "$VT/data-paul/M16/classhp.idx" \
          "$VT/db_build.date"

# vt_eng.dll's real API: plain C exports (VT_LOADTTS_ENG, VT_TextToFile_ENG),
# not SAPI5. Calling it directly skips Wine's SAPI layer and any GUI/dialogs
# entirely -- see CLAUDE.md for how these signatures were recovered.
COPY vtwav.c /tmp/vtwav.c
RUN i686-w64-mingw32-gcc -O2 -o "/wine/drive_c/Program Files/VW/VT/Paul/M16/bin/vtwav.exe" /tmp/vtwav.c

# Drop install tree files unused by headless synthesis (GUI editor tools,
# help files, fonts, sample text, unused MS Speech SDK/balcon that an
# earlier SAPI5 approach installed before vt_eng.dll's real API was found).
RUN VT="/wine/drive_c/Program Files/VW/VT/Paul/M16" && \
    rm -f "$VT/bin/VTEditor_ENG.exe" "$VT/bin"/UserDicEng* "$VT/bin"/*.chm \
          "$VT/bin"/*.ttf "$VT/bin"/sample_*.txt "$VT/bin/Verification Center.lnk" && \
    rm -rf "/wine/drive_c/Program Files/Microsoft Speech SDK 5.1" \
           "/wine/drive_c/Program Files/Common Files/Microsoft Shared/Speech" \
           "/wine/drive_c/balcon" \
           "/wine/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/Microsoft Speech SDK 5.1" \
           "/wine/drive_c/users/root/AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Microsoft Speech SDK 5.1"


FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    WINEARCH=win32 \
    WINEPREFIX=/wine \
    WINEDEBUG=-all

# Only wine itself is needed at runtime: vt_eng.dll and vtwav.exe are both
# Windows PE binaries, and nothing else on Linux can execute them.
RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends wine wine32 && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /wine /wine

COPY entrypoint.sh /entrypoint.sh
COPY synth.sh /usr/local/bin/synth
RUN chmod +x /entrypoint.sh /usr/local/bin/synth

ENTRYPOINT ["/entrypoint.sh"]
