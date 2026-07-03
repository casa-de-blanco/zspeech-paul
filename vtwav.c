#include <windows.h>
#include <stdio.h>

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

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: vtwav.exe <text> <output.wav>\n");
        return 2;
    }
    const char *text = argv[1];
    const char *outfile = argv[2];
    const char *db_path = "C:\\Program Files\\VW\\VT\\Paul\\M16";
    const char *license_path = "C:\\Program Files\\VW\\VT\\Paul\\M16\\data-common\\verify\\verification.txt";
    int speakerID = 1; /* 0 = "Kate" (not installed in this package), 1 = Paul */

    HMODULE hModule = LoadLibraryA("vt_eng.dll");
    if (!hModule) {
        fprintf(stderr, "LoadLibrary(vt_eng.dll) failed: %lu\n", GetLastError());
        return 1;
    }

    pFunc_VT_LOADTTS Func_LOADTTS = (pFunc_VT_LOADTTS)GetProcAddress(hModule, "VT_LOADTTS_ENG");
    pFunc_VT_UNLOADTTS Func_UNLOADTTS = (pFunc_VT_UNLOADTTS)GetProcAddress(hModule, "VT_UNLOADTTS_ENG");
    pFunc_VT_TextToFile Func_TextToFile = (pFunc_VT_TextToFile)GetProcAddress(hModule, "VT_TextToFile_ENG");
    if (!Func_LOADTTS || !Func_UNLOADTTS || !Func_TextToFile) {
        fprintf(stderr, "GetProcAddress failed\n");
        return 1;
    }

    /* Note: VT_LOADTTS_ENG returns 0 on success here (not 1 -- inconsistent
     * with VT_TextToFile_ENG's 1=success convention, confirmed by disassembly
     * of VT_LOADTTS_EXT_ENG). Only negative values are real errors. */
    short ret = Func_LOADTTS(NULL, speakerID, (char*)db_path, (char*)license_path);
    if (ret < 0) {
        fprintf(stderr, "VT_LOADTTS_ENG failed: %d\n", ret);
        return 1;
    }

    short ret2 = Func_TextToFile(VT_FILE_FMT_S16PCM_WAVE, (char*)text, (char*)outfile, speakerID,
                                  -1, -1, -1, -1, -1, -1);

    Func_UNLOADTTS(speakerID);

    if (ret2 != 1) {
        fprintf(stderr, "VT_TextToFile_ENG failed: %d\n", ret2);
        return 1;
    }

    return 0;
}
