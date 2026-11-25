#include <windows.h>
#include <commctrl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "player.h"
#include "playlist.h"
#include "prefs.h"
#include "srt.h"
#include "trackbar.h"

static void write_bytes_file(const wchar_t *path, const unsigned char *data, size_t len) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, (DWORD)len, &written, NULL);
    assert(ok && written == len);
    CloseHandle(h);
}

static void create_empty_file(const wchar_t *path) {
    write_bytes_file(path, (const unsigned char *)"", 0);
}

static void test_discovery_alphabetical(void) {
    wchar_t tmp_root[MAX_PATH];
    DWORD r = GetTempPathW(MAX_PATH, tmp_root);
    assert(r > 0 && r < MAX_PATH);

    wchar_t test_dir[MAX_PATH];
    _snwprintf(test_dir, MAX_PATH, L"%lsmp3sub_test_%lu", tmp_root, GetCurrentProcessId());
    CreateDirectoryW(test_dir, NULL);

    const wchar_t *files[] = {
        L"My Track.mp3",
        L"My Track.id.srt",
        L"My Track.en.srt",
        L"My Track.ar.srt",
        L"My Track.srt",
        L"My Track_remix.srt",
        L"My Track 2.en.srt",
        L"Other.en.srt"
    };
    wchar_t path_buf[MAX_PATH];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        _snwprintf(path_buf, MAX_PATH, L"%ls\\%ls", test_dir, files[i]);
        create_empty_file(path_buf);
    }

    wchar_t mp3_path[MAX_PATH];
    _snwprintf(mp3_path, MAX_PATH, L"%ls\\My Track.mp3", test_dir);

    SrtTrackList list;
    assert(srt_discover_tracks(mp3_path, &list));
    assert(list.count == 4);
    assert(_wcsicmp(list.names[0], L"My Track.ar.srt") == 0);
    assert(_wcsicmp(list.names[1], L"My Track.en.srt") == 0);
    assert(_wcsicmp(list.names[2], L"My Track.id.srt") == 0);
    assert(_wcsicmp(list.names[3], L"My Track.srt") == 0);
    srt_free_track_list(&list);

    /* Test with other audio extensions: .wav, .m4a, .flac, .wma */
    const wchar_t *exts[] = { L"wav", L"m4a", L"flac", L"wma", L"aac", L"ogg" };
    for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
        _snwprintf(mp3_path, MAX_PATH, L"%ls\\My Track.%ls", test_dir, exts[e]);
        assert(srt_discover_tracks(mp3_path, &list));
        assert(list.count == 4);
        assert(_wcsicmp(list.names[0], L"My Track.ar.srt") == 0);
        assert(_wcsicmp(list.names[1], L"My Track.en.srt") == 0);
        assert(_wcsicmp(list.names[2], L"My Track.id.srt") == 0);
        assert(_wcsicmp(list.names[3], L"My Track.srt") == 0);
        srt_free_track_list(&list);
    }

    /* Cleanup */
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        _snwprintf(path_buf, MAX_PATH, L"%ls\\%ls", test_dir, files[i]);
        DeleteFileW(path_buf);
    }
    RemoveDirectoryW(test_dir);
    wprintf(L"[PASS] test_discovery_alphabetical\n");
}

static void test_srt_parse_and_lookup(void) {
    wchar_t tmp_root[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp_root);
    wchar_t srt_path[MAX_PATH];
    _snwprintf(srt_path, MAX_PATH, L"%lsmp3sub_parse_%lu.srt", tmp_root, GetCurrentProcessId());

    const unsigned char utf8_srt[] =
        "\xEF\xBB\xBF"
        "2\r\n"
        "00:00:05.500 --> 00:00:08,000 X1:0\r\n"
        "{\\an8}<i>Second</i> cue\r\n"
        "line two\r\n"
        "\r\n"
        "1\r\n"
        "00:00:01,000 --> 00:00:03,000\r\n"
        "First cue!\r\n"
        "\r\n"
        "3\r\n"
        "00:00:07,000 --> 00:00:09,000\r\n"
        "Overlap cue\r\n";

    write_bytes_file(srt_path, utf8_srt, sizeof(utf8_srt) - 1);

    SrtSubtitle sub;
    assert(srt_load_file(srt_path, &sub));
    assert(sub.count == 3);
    /* Sorted by start_ms */
    assert(sub.cues[0].start_ms == 1000 && sub.cues[0].end_ms == 3000);
    assert(sub.cues[1].start_ms == 5500 && sub.cues[1].end_ms == 8000);
    assert(sub.cues[2].start_ms == 7000 && sub.cues[2].end_ms == 9000);

    wchar_t buf[512];
    assert(!srt_get_active_text(&sub, 500, buf, 512));
    assert(srt_get_active_text(&sub, 1000, buf, 512));
    assert(wcscmp(buf, L"First cue!") == 0);
    assert(!srt_get_active_text(&sub, 3000, buf, 512));

    assert(srt_get_active_text(&sub, 6000, buf, 512));
    assert(wcscmp(buf, L"Second cue\nline two") == 0);

    assert(srt_get_active_text(&sub, 7500, buf, 512));
    assert(wcscmp(buf, L"Second cue\nline two\nOverlap cue") == 0);

    srt_free_subtitle(&sub);
    DeleteFileW(srt_path);
    wprintf(L"[PASS] test_srt_parse_and_lookup\n");
}

static void test_prefs_remember_position(void) {
    wchar_t tmp_root[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp_root);
    wchar_t ini_path[MAX_PATH];
    _snwprintf(ini_path, MAX_PATH, L"%lsmp3sub_prefs_%lu.ini", tmp_root, GetCurrentProcessId());
    DeleteFileW(ini_path);

    prefs_init_with_path(ini_path);
    assert(prefs_get_remember_position() == true); /* default enabled */

    prefs_set_remember_position(false);
    assert(prefs_get_remember_position() == false);

    prefs_set_remember_position(true);
    assert(prefs_get_remember_position() == true);

    prefs_save_file_position(L"D:\\Music\\Song One.mp3", 45678);
    assert(prefs_load_file_position(L"d:\\music\\song one.mp3") == 45678);
    assert(prefs_load_file_position(L"D:/Music/Song One.mp3") == 45678);
    assert(prefs_load_file_position(L"D:\\Music\\Other.mp3") == 0);

    DeleteFileW(ini_path);
    wprintf(L"[PASS] test_prefs_remember_position\n");
}

typedef struct TbTestLog {
    int thumbtrack_count;
    int endtrack_count;
    int last_code;
    int last_pos;
} TbTestLog;

static LRESULT CALLBACK tb_test_parent_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HSCROLL) {
        TbTestLog *log = (TbTestLog *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        HWND tb = (HWND)lParam;
        if (log && tb) {
            WORD code = LOWORD(wParam);
            log->last_code = (int)code;
            log->last_pos = (int)SendMessageW(tb, TBM_GETPOS, 0, 0);
            if (code == TB_THUMBTRACK) {
                log->thumbtrack_count++;
            } else if (code == TB_ENDTRACK) {
                log->endtrack_count++;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void test_trackbar_click_to_seek(void) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = tb_test_parent_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TbTestParentCls";
    RegisterClassW(&wc);

    TbTestLog log = {0};
    HWND parent = CreateWindowExW(0, L"TbTestParentCls", L"test", WS_OVERLAPPEDWINDOW,
                                  0, 0, 1000, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
    assert(parent != NULL);
    SetWindowLongPtrW(parent, GWLP_USERDATA, (LONG_PTR)&log);

    HWND tb = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                              WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                              10, 10, 800, 30, parent, (HMENU)100, GetModuleHandleW(NULL), NULL);
    assert(tb != NULL);
    SendMessageW(tb, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(tb, TBM_SETRANGEMAX, TRUE, 10000);

    TrackbarClickState drag_state = {0};
    assert(trackbar_attach_click_seek(tb, &drag_state));

    RECT ch = {0};
    SendMessageW(tb, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
    int cy = (ch.top + ch.bottom) / 2;

    /* Click at 5%, 12.5%, 35%, 50%, 75%, 95% along the channel (pos starts at 0) */
    const double fracs[] = { 0.05, 0.125, 0.35, 0.50, 0.75, 0.95 };
    for (size_t i = 0; i < sizeof(fracs) / sizeof(fracs[0]); i++) {
        SendMessageW(tb, TBM_SETPOS, TRUE, 0);
        int click_x = ch.left + (int)((ch.right - ch.left) * fracs[i]);

        int prev_tt = log.thumbtrack_count;
        int prev_et = log.endtrack_count;
        SendMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(click_x, cy));
        assert(log.thumbtrack_count == prev_tt + 1);
        SendMessageW(tb, WM_LBUTTONUP, 0, MAKELPARAM(click_x, cy));
        assert(log.endtrack_count == prev_et + 1);
        assert(log.last_code == TB_ENDTRACK);

        RECT th = {0};
        SendMessageW(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
        int thumb_center = (th.left + th.right) / 2;
        int diff = abs(thumb_center - click_x);
        assert(diff <= 1);
    }

    /* Drag from 10% to 62% */
    int x_start = ch.left + (int)((ch.right - ch.left) * 0.10);
    int x_mid = ch.left + (int)((ch.right - ch.left) * 0.40);
    int x_end = ch.left + (int)((ch.right - ch.left) * 0.62);
    int tt_before = log.thumbtrack_count;
    SendMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x_start, cy));
    SendMessageW(tb, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x_mid, cy));
    SendMessageW(tb, WM_LBUTTONUP, 0, MAKELPARAM(x_end, cy));
    assert(log.thumbtrack_count >= tt_before + 2);
    RECT th = {0};
    SendMessageW(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
    assert(abs(((th.left + th.right) / 2) - x_end) <= 1);

    /* Clamp left and right extremes */
    SendMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-20, cy));
    SendMessageW(tb, WM_LBUTTONUP, 0, MAKELPARAM(-20, cy));
    assert(SendMessageW(tb, TBM_GETPOS, 0, 0) == 0);

    SendMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(950, cy));
    SendMessageW(tb, WM_LBUTTONUP, 0, MAKELPARAM(950, cy));
    assert(SendMessageW(tb, TBM_GETPOS, 0, 0) == 10000);

    /* Disabled trackbar ignores click-seek */
    SendMessageW(tb, TBM_SETPOS, TRUE, 5000);
    EnableWindow(tb, FALSE);
    SendMessageW(tb, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ch.left + 30, cy));
    SendMessageW(tb, WM_LBUTTONUP, 0, MAKELPARAM(ch.left + 30, cy));
    assert(SendMessageW(tb, TBM_GETPOS, 0, 0) == 5000);

    DestroyWindow(parent);
    UnregisterClassW(L"TbTestParentCls", GetModuleHandleW(NULL));
    wprintf(L"[PASS] test_trackbar_click_to_seek\n");
}

static void test_prefs_auto_next(void) {
    wchar_t tmp_root[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp_root);
    wchar_t ini_path[MAX_PATH];
    _snwprintf(ini_path, MAX_PATH, L"%lsmp3sub_autonext_%lu.ini", tmp_root, GetCurrentProcessId());
    DeleteFileW(ini_path);

    prefs_init_with_path(ini_path);
    assert(prefs_get_auto_next() == true); /* default enabled */

    prefs_set_auto_next(false);
    assert(prefs_get_auto_next() == false);

    prefs_set_auto_next(true);
    assert(prefs_get_auto_next() == true);

    DeleteFileW(ini_path);
    wprintf(L"[PASS] test_prefs_auto_next\n");
}

static void test_find_next_audio_file(void) {
    assert(player_is_supported_audio_extension(L"test.ogg") == true);
    assert(player_is_supported_audio_extension(L"test.mp3") == true);
    assert(player_is_supported_audio_extension(L"test.wav") == true);
    assert(player_is_supported_audio_extension(L"test.txt") == false);
    assert(player_is_supported_audio_extension(L"test.srt") == false);

    wchar_t tmp_root[MAX_PATH];
    DWORD r = GetTempPathW(MAX_PATH, tmp_root);
    assert(r > 0 && r < MAX_PATH);

    wchar_t test_dir[MAX_PATH];
    _snwprintf(test_dir, MAX_PATH, L"%lsmp3sub_next_test_%lu", tmp_root, GetCurrentProcessId());
    CreateDirectoryW(test_dir, NULL);

    const wchar_t *files[] = {
        L"track01.mp3",
        L"track02.ogg",
        L"track03.wav",
        L"track10.mp3",
        L"track01.srt",
        L"notes.txt"
    };
    wchar_t pbuf[MAX_PATH];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        _snwprintf(pbuf, MAX_PATH, L"%ls\\%ls", test_dir, files[i]);
        create_empty_file(pbuf);
    }

    wchar_t cur_path[MAX_PATH];
    wchar_t next_path[MAX_PATH];
    wchar_t expected[MAX_PATH];

    /* track01.mp3 -> track02.ogg */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track01.mp3", test_dir);
    _snwprintf(expected, MAX_PATH, L"%ls\\track02.ogg", test_dir);
    assert(player_find_next_audio_file(cur_path, next_path, MAX_PATH));
    assert(_wcsicmp(next_path, expected) == 0);

    /* track02.ogg -> track03.wav */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track02.ogg", test_dir);
    _snwprintf(expected, MAX_PATH, L"%ls\\track03.wav", test_dir);
    assert(player_find_next_audio_file(cur_path, next_path, MAX_PATH));
    assert(_wcsicmp(next_path, expected) == 0);

    /* track03.wav -> track10.mp3 (natural sort: 3 before 10) */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track03.wav", test_dir);
    _snwprintf(expected, MAX_PATH, L"%ls\\track10.mp3", test_dir);
    assert(player_find_next_audio_file(cur_path, next_path, MAX_PATH));
    assert(_wcsicmp(next_path, expected) == 0);

    /* track10.mp3 -> no next file (returns false) */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track10.mp3", test_dir);
    assert(!player_find_next_audio_file(cur_path, next_path, MAX_PATH));

    /* Test player_find_prev_audio_file */
    wchar_t prev_path[MAX_PATH];

    /* track10.mp3 -> track03.wav */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track10.mp3", test_dir);
    _snwprintf(expected, MAX_PATH, L"%ls\\track03.wav", test_dir);
    assert(player_find_prev_audio_file(cur_path, prev_path, MAX_PATH));
    assert(_wcsicmp(prev_path, expected) == 0);

    /* track03.wav -> track02.ogg */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track03.wav", test_dir);
    _snwprintf(expected, MAX_PATH, L"%ls\\track02.ogg", test_dir);
    assert(player_find_prev_audio_file(cur_path, prev_path, MAX_PATH));
    assert(_wcsicmp(prev_path, expected) == 0);

    /* track02.ogg -> track01.mp3 */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track02.ogg", test_dir);
    _snwprintf(expected, MAX_PATH, L"%ls\\track01.mp3", test_dir);
    assert(player_find_prev_audio_file(cur_path, prev_path, MAX_PATH));
    assert(_wcsicmp(prev_path, expected) == 0);

    /* track01.mp3 -> no prev file (returns false) */
    _snwprintf(cur_path, MAX_PATH, L"%ls\\track01.mp3", test_dir);
    assert(!player_find_prev_audio_file(cur_path, prev_path, MAX_PATH));

    /* Clean up */
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        _snwprintf(pbuf, MAX_PATH, L"%ls\\%ls", test_dir, files[i]);
        DeleteFileW(pbuf);
    }
    RemoveDirectoryW(test_dir);

    wprintf(L"[PASS] test_find_next_audio_file\n");
}

static void test_prefs_show_playlist(void) {
    wchar_t tmp_root[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp_root);
    wchar_t ini_path[MAX_PATH];
    _snwprintf(ini_path, MAX_PATH, L"%lsmp3sub_showpl_%lu.ini", tmp_root, GetCurrentProcessId());
    DeleteFileW(ini_path);

    prefs_init_with_path(ini_path);
    assert(prefs_get_show_playlist() == true); /* default enabled */

    prefs_set_show_playlist(false);
    assert(prefs_get_show_playlist() == false);

    prefs_set_show_playlist(true);
    assert(prefs_get_show_playlist() == true);

    DeleteFileW(ini_path);
    wprintf(L"[PASS] test_prefs_show_playlist\n");
}

static void test_prefs_history(void) {
    wchar_t tmp_root[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp_root);
    wchar_t ini_path[MAX_PATH];
    _snwprintf(ini_path, MAX_PATH, L"%lsmp3sub_hist_%lu.ini", tmp_root, GetCurrentProcessId());
    DeleteFileW(ini_path);

    prefs_init_with_path(ini_path);
    assert(prefs_get_history_count() == 0);

    /* Add items */
    prefs_add_history(L"D:\\Audio\\Track01.mp3");
    assert(prefs_get_history_count() == 1);
    wchar_t buf[MAX_PATH];
    assert(prefs_get_history_item(0, buf, MAX_PATH));
    assert(wcscmp(buf, L"D:\\Audio\\Track01.mp3") == 0);

    prefs_add_history(L"D:\\Audio\\Track02.ogg");
    assert(prefs_get_history_count() == 2);
    assert(prefs_get_history_item(0, buf, MAX_PATH));
    assert(wcscmp(buf, L"D:\\Audio\\Track02.ogg") == 0);
    assert(prefs_get_history_item(1, buf, MAX_PATH));
    assert(wcscmp(buf, L"D:\\Audio\\Track01.mp3") == 0);

    /* Adding existing item (even with different slash/casing) should move it to top */
    prefs_add_history(L"d:/audio/track01.mp3");
    assert(prefs_get_history_count() == 2);
    assert(prefs_get_history_item(0, buf, MAX_PATH));
    assert(_wcsicmp(buf, L"d:/audio/track01.mp3") == 0);
    assert(prefs_get_history_item(1, buf, MAX_PATH));
    assert(wcscmp(buf, L"D:\\Audio\\Track02.ogg") == 0);

    /* Test clamp to 10 items */
    for (int i = 0; i < 15; i++) {
        wchar_t p[MAX_PATH];
        _snwprintf(p, MAX_PATH, L"D:\\Audio\\Bulk_%d.mp3", i);
        prefs_add_history(p);
    }
    assert(prefs_get_history_count() == 10);
    assert(prefs_get_history_item(0, buf, MAX_PATH));
    assert(wcscmp(buf, L"D:\\Audio\\Bulk_14.mp3") == 0);

    /* Clear history */
    prefs_clear_history();
    assert(prefs_get_history_count() == 0);
    assert(!prefs_get_history_item(0, buf, MAX_PATH));

    DeleteFileW(ini_path);
    wprintf(L"[PASS] test_prefs_history\n");
}

static void test_playlist_scan_and_navigation(void) {
    wchar_t tmp_root[MAX_PATH];
    DWORD r = GetTempPathW(MAX_PATH, tmp_root);
    assert(r > 0 && r < MAX_PATH);

    wchar_t test_dir[MAX_PATH];
    _snwprintf(test_dir, MAX_PATH, L"%lsmp3sub_pl_test_%lu", tmp_root, GetCurrentProcessId());
    CreateDirectoryW(test_dir, NULL);

    const wchar_t *files[] = {
        L"10_track.mp3",
        L"02_track.ogg",
        L"01_track.wav",
        L"readme.txt"
    };
    wchar_t pbuf[MAX_PATH];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        _snwprintf(pbuf, MAX_PATH, L"%ls\\%ls", test_dir, files[i]);
        create_empty_file(pbuf);
    }

    Playlist pl;
    playlist_init(&pl);

    wchar_t target_file[MAX_PATH];
    _snwprintf(target_file, MAX_PATH, L"%ls\\02_track.ogg", test_dir);

    assert(playlist_scan_folder(&pl, target_file));
    assert(pl.count == 3); /* skips readme.txt */
    assert(pl.current_index == 1); /* 01_track.wav is 0, 02_track.ogg is 1 */

    /* Verify natural sorting */
    assert(_wcsicmp(playlist_get_name(&pl, 0), L"01_track.wav") == 0);
    assert(_wcsicmp(playlist_get_name(&pl, 1), L"02_track.ogg") == 0);
    assert(_wcsicmp(playlist_get_name(&pl, 2), L"10_track.mp3") == 0);

    /* Verify index lookup */
    wchar_t lookup_file[MAX_PATH];
    _snwprintf(lookup_file, MAX_PATH, L"%ls\\10_track.mp3", test_dir);
    assert(playlist_find_index(&pl, lookup_file) == 2);
    assert(playlist_find_index(&pl, L"10_track.mp3") == 2);
    assert(playlist_find_index(&pl, L"nonexistent.mp3") == -1);

    /* Free playlist */
    playlist_free(&pl);
    assert(pl.count == 0);
    assert(pl.paths == NULL);

    /* Clean up files */
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        _snwprintf(pbuf, MAX_PATH, L"%ls\\%ls", test_dir, files[i]);
        DeleteFileW(pbuf);
    }
    RemoveDirectoryW(test_dir);

    wprintf(L"[PASS] test_playlist_scan_and_navigation\n");
}

int wmain(void) {
    test_discovery_alphabetical();
    test_srt_parse_and_lookup();
    test_prefs_remember_position();
    test_prefs_auto_next();
    test_prefs_show_playlist();
    test_prefs_history();
    test_find_next_audio_file();
    test_playlist_scan_and_navigation();
    test_trackbar_click_to_seek();
    wprintf(L"All tests passed.\n");
    return 0;
}

