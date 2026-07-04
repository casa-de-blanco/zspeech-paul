#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Direct C API for NeoSpeech VoiceText's vt_eng.dll, bypassing SAPI5/GUI entirely.
 * Reverse-engineered from vt_eng.dll disassembly + the equivalent public
 * VT_*_JPN sample (github.com/loftkun/VoiceText_Sample_Win) which shares the
 * same API shape across NeoSpeech's per-language engine DLLs. Cdecl convention,
 * confirmed via disassembly (plain `ret`, caller cleans the stack).
 */

typedef short (*pFunc_VT_LOADTTS)(HWND hWnd, int nSpeakerID, char *db_path, char *licensefile);
typedef void  (*pFunc_VT_UNLOADTTS)(int nSpeakerID);
typedef short (*pFunc_VT_TextToFile)(int fmt, char *tts_text, char *filename, int nSpeakerID,
                                      int pitch, int speed, int volume, int pause,
                                      int dictidx, int texttype);

/* fmt values probed empirically against VT_TextToFile_ENG's internal 0-9
 * dispatch table: 4 = 16-bit PCM WAV (RIFF/WAVE, mono, 16kHz) -- the only
 * one of the 10 that produces a real WAV container instead of headerless
 * raw PCM or A-law/u-law WAV.
 */
#define VT_FILE_FMT_S16PCM_WAVE 4
#define WAV_HEADER_SIZE 44

/* VTML tags (<vtml_phoneme>, <vtml_pause>, <vtml_pitch>, ...) embedded in
 * the text argument are parsed unconditionally by VT_TextToFile_ENG --
 * confirmed empirically by probing every texttype value from -1 to 3 with
 * a <vtml_pause time="5000"/> tag and a phoneme override on a one-letter
 * word: both produced identical, correctly-lengthened output at every
 * texttype value tested (pause added ~5s of audio; the phoneme override
 * produced ~3x the duration of the plain letter). texttype does not gate
 * VTML parsing, so it's left at -1 (engine default) below like the other
 * unused optional params -- no separate "enable VTML" value exists to set.
 */

/* This install is stuck in demo mode (see CLAUDE.md -- no local fix found:
 * no license file ships with the installer, no serial-entry screen exists,
 * the "Verification Center" is a dead link to NeoSpeech's defunct web
 * portal, and the DLL's license-related exports are read-only parsers, not
 * activation calls). Every synthesis call prepends a spoken disclaimer
 * drawn from a small fixed set of variants before the requested text.
 * Since synthesis of identical text+params is byte-deterministic (verified
 * empirically: repeated captures of the same variant are byte-for-byte
 * identical), the fix is to capture each variant once as a reference clip
 * and strip it by exact prefix match at synthesis time -- see
 * strip_watermark() below and the Dockerfile step that populates
 * bin/refwm/ at build time.
 */
/* Overridable at compile time via -DVOICE_NAME=..., -DVOICE_MODEL=...,
 * -DSPEAKER_ID=..., -DDLL_NAME=..., -DEXPORT_SUFFIX=..., -DBIN_SUBDIR=...
 * (see Dockerfile). Defaults match Paul; Violeta overrides all six --
 * this is parameterized for exactly these two voices, not a general
 * N-voice framework (see CLAUDE.md). */
#ifndef VOICE_NAME
#define VOICE_NAME "Paul"
#endif
#ifndef VOICE_MODEL
#define VOICE_MODEL "M16"
#endif
#ifndef SPEAKER_ID
#define SPEAKER_ID 1
#endif
#ifndef DLL_NAME
#define DLL_NAME "vt_eng.dll"
#endif
#ifndef EXPORT_SUFFIX
#define EXPORT_SUFFIX "ENG"
#endif
#ifndef BIN_SUBDIR
#define BIN_SUBDIR "bin"
#endif

#define REFWM_DIR "C:\\Program Files\\VW\\VT\\" VOICE_NAME "\\" VOICE_MODEL "\\" BIN_SUBDIR "\\refwm\\"
#define REFWM_MAX 64

static unsigned char *read_file(const char *path, long *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char*)malloc(len > 0 ? len : 1);
    if (!buf) { fclose(f); return NULL; }
    if (len > 0 && fread(buf, 1, len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *out_len = len;
    return buf;
}

/* Strip a known demo-watermark prefix from a just-synthesized WAV file, in
 * place. Matches the PCM payload against bin/refwm/refwm_0.pcm,
 * refwm_1.pcm, ... (sequential, stops at the first missing index) and cuts
 * off the longest exact-prefix match. Fails loudly (nonzero return) if no
 * reference matches -- e.g. an unrecognized watermark variant not captured
 * at build time -- rather than silently shipping watermarked or corrupted
 * audio.
 */
static int strip_watermark(const char *path)
{
    long raw_len;
    unsigned char *raw = read_file(path, &raw_len);
    if (!raw || raw_len < WAV_HEADER_SIZE) {
        fprintf(stderr, "strip_watermark: failed to read %s\n", path);
        free(raw);
        return -1;
    }
    unsigned char *pcm = raw + WAV_HEADER_SIZE;
    long pcm_len = raw_len - WAV_HEADER_SIZE;

    long best_ref_len = -1;
    for (int i = 0; i < REFWM_MAX; i++) {
        char refpath[512];
        snprintf(refpath, sizeof(refpath), "%srefwm_%d.pcm", REFWM_DIR, i);
        long ref_len;
        unsigned char *ref = read_file(refpath, &ref_len);
        if (!ref) break; /* sequential files with no gaps; stop at first missing one */
        if (ref_len <= pcm_len && memcmp(pcm, ref, ref_len) == 0 && ref_len > best_ref_len) {
            best_ref_len = ref_len;
        }
        free(ref);
    }

    if (best_ref_len < 0) {
        fprintf(stderr, "strip_watermark: no known watermark reference matched %s -- "
                        "engine likely played an uncaptured variant; regenerate "
                        "bin/refwm/ reference clips (see Dockerfile)\n", path);
        free(raw);
        return -1;
    }

    long new_pcm_len = pcm_len - best_ref_len;
    unsigned char *new_pcm = pcm + best_ref_len;

    unsigned char header[WAV_HEADER_SIZE];
    memcpy(header, raw, WAV_HEADER_SIZE);
    unsigned int riff_size = 36 + (unsigned int)new_pcm_len;
    unsigned int data_size = (unsigned int)new_pcm_len;
    memcpy(header + 4, &riff_size, 4);
    memcpy(header + 40, &data_size, 4);

    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "strip_watermark: cannot rewrite %s\n", path);
        free(raw);
        return -1;
    }
    fwrite(header, 1, WAV_HEADER_SIZE, out);
    fwrite(new_pcm, 1, (size_t)new_pcm_len, out);
    fclose(out);
    free(raw);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: vtwav.exe <text> <output.wav>\n");
        fprintf(stderr, "       vtwav.exe --capture-watermark <output.pcm>  (build-time only)\n");
        return 2;
    }
    const char *db_path = "C:\\Program Files\\VW\\VT\\" VOICE_NAME "\\" VOICE_MODEL;
    const char *license_path = "C:\\Program Files\\VW\\VT\\" VOICE_NAME "\\" VOICE_MODEL "\\data-common\\verify\\verification.txt";
    int speakerID = SPEAKER_ID; /* 0 = "Kate" (not installed in this package), 1 = Paul */

    HMODULE hModule = LoadLibraryA(DLL_NAME);
    if (!hModule) {
        fprintf(stderr, "LoadLibrary(%s) failed: %lu\n", DLL_NAME, GetLastError());
        return 1;
    }

    pFunc_VT_LOADTTS Func_LOADTTS = (pFunc_VT_LOADTTS)GetProcAddress(hModule, "VT_LOADTTS_" EXPORT_SUFFIX);
    pFunc_VT_UNLOADTTS Func_UNLOADTTS = (pFunc_VT_UNLOADTTS)GetProcAddress(hModule, "VT_UNLOADTTS_" EXPORT_SUFFIX);
    pFunc_VT_TextToFile Func_TextToFile = (pFunc_VT_TextToFile)GetProcAddress(hModule, "VT_TextToFile_" EXPORT_SUFFIX);
    if (!Func_LOADTTS || !Func_UNLOADTTS || !Func_TextToFile) {
        fprintf(stderr, "GetProcAddress failed\n");
        return 1;
    }

    /* Note: VT_LOADTTS_ENG returns 0 on success here (not 1 -- inconsistent
     * with VT_TextToFile_ENG's 1=success convention, confirmed by disassembly
     * of VT_LOADTTS_EXT_ENG). Only negative values are real errors. */
    short ret = Func_LOADTTS(NULL, speakerID, (char*)db_path, (char*)license_path);
    if (ret < 0) {
        fprintf(stderr, "VT_LOADTTS_" EXPORT_SUFFIX " failed: %d\n", ret);
        return 1;
    }

    if (strcmp(argv[1], "--capture-watermark") == 0) {
        /* Build-time only: whitespace-only input still triggers the demo
         * watermark but has no real text to speak afterward, so the
         * output is a pure, isolated watermark clip. Save just its PCM
         * payload (strip the WAV header) for later exact-prefix matching
         * in strip_watermark() above. */
        const char *outfile = argv[2];
        short ret2 = Func_TextToFile(VT_FILE_FMT_S16PCM_WAVE, " ", (char*)outfile, speakerID,
                                      -1, -1, -1, -1, -1, -1);
        Func_UNLOADTTS(speakerID);
        if (ret2 != 1) {
            fprintf(stderr, "VT_TextToFile_" EXPORT_SUFFIX " (capture) failed: %d\n", ret2);
            return 1;
        }
        long len;
        unsigned char *raw = read_file(outfile, &len);
        if (!raw || len < WAV_HEADER_SIZE) {
            fprintf(stderr, "capture: bad output %s\n", outfile);
            free(raw);
            return 1;
        }
        FILE *out = fopen(outfile, "wb");
        if (!out) { fprintf(stderr, "capture: cannot rewrite %s\n", outfile); free(raw); return 1; }
        fwrite(raw + WAV_HEADER_SIZE, 1, (size_t)(len - WAV_HEADER_SIZE), out);
        fclose(out);
        free(raw);
        return 0;
    }

    const char *text = argv[1];
    const char *outfile = argv[2];

    short ret2 = Func_TextToFile(VT_FILE_FMT_S16PCM_WAVE, (char*)text, (char*)outfile, speakerID,
                                  -1, -1, -1, -1, -1, -1);

    Func_UNLOADTTS(speakerID);

    if (ret2 != 1) {
        fprintf(stderr, "VT_TextToFile_" EXPORT_SUFFIX " failed: %d\n", ret2);
        return 1;
    }

    if (strip_watermark(outfile) != 0) {
        return 1;
    }

    return 0;
}
