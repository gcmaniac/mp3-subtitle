#include "prefs.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static wchar_t g_ini_path[MAX_PATH] = {0};

static void make_path_key(const wchar_t *path, wchar_t *key_buf, size_t key_chars) {
    uint64_t hash = 14695981039346656037ULL;
    if (path) {
        for (const wchar_t *p = path; *p; p++) {
            wchar_t c = towlower(*p);
            if (c == L'/') {
                c = L'\\';
            }
            hash ^= (uint64_t)(c & 0xFF);
            hash *= 1099511628211ULL;
            hash ^= (uint64_t)((c >> 8) & 0xFF);
            hash *= 1099511628211ULL;
        }
    }
    _snwprintf(key_buf, key_chars, L"pos_%016llx", (unsigned long long)hash);
}

void prefs_init_with_path(const wchar_t *ini_path) {
    if (!ini_path || !ini_path[0]) {
        g_ini_path[0] = L'\0';
        return;
    }
    wcsncpy(g_ini_path, ini_path, MAX_PATH - 1);
    g_ini_path[MAX_PATH - 1] = L'\0';
}

void prefs_init(void) {
    wchar_t local_app[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app, MAX_PATH);
    if (n > 0 && n < MAX_PATH - 40) {
        wchar_t dir[MAX_PATH];
        _snwprintf(dir, MAX_PATH, L"%ls\\Mp3SubtitlePlayer", local_app);
        CreateDirectoryW(dir, NULL);
        _snwprintf(g_ini_path, MAX_PATH, L"%ls\\state.ini", dir);
        return;
    }

    wchar_t exe_path[MAX_PATH] = {0};
    DWORD m = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    if (m > 0 && m < MAX_PATH) {
        wchar_t *slash = wcsrchr(exe_path, L'\\');
        if (slash) {
            slash[1] = L'\0';
            _snwprintf(g_ini_path, MAX_PATH, L"%lsmp3-subtitle.ini", exe_path);
            return;
        }
    }

    wcscpy(g_ini_path, L".\\mp3-subtitle.ini");
}

bool prefs_get_remember_position(void) {
    if (!g_ini_path[0]) return false;
    UINT val = GetPrivateProfileIntW(L"Settings", L"RememberPosition", 1, g_ini_path);
    return val != 0;
}

void prefs_set_remember_position(bool enabled) {
    if (!g_ini_path[0]) return;
    WritePrivateProfileStringW(L"Settings", L"RememberPosition",
                               enabled ? L"1" : L"0", g_ini_path);
}

long long prefs_load_file_position(const wchar_t *mp3_path) {
    if (!g_ini_path[0] || !mp3_path || !mp3_path[0]) return 0;
    wchar_t key[32];
    make_path_key(mp3_path, key, 32);

    wchar_t val_buf[64] = {0};
    GetPrivateProfileStringW(L"Positions", key, L"0", val_buf, 64, g_ini_path);
    long long pos = _wcstoi64(val_buf, NULL, 10);
    return pos > 0 ? pos : 0;
}

void prefs_save_file_position(const wchar_t *mp3_path, long long pos_ms) {
    if (!g_ini_path[0] || !mp3_path || !mp3_path[0]) return;
    if (pos_ms < 0) pos_ms = 0;

    wchar_t key[32];
    make_path_key(mp3_path, key, 32);

    wchar_t val_buf[64];
    _snwprintf(val_buf, 64, L"%lld", pos_ms);
    WritePrivateProfileStringW(L"Positions", key, val_buf, g_ini_path);
}

