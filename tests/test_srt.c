#include <windows.h>
#include <commctrl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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

    /* Non-matching mp3 */
    _snwprintf(mp3_path, MAX_PATH, L"%ls\\NoSub.mp3", test_dir);
    assert(srt_discover_tracks(mp3_path, &list));
    assert(list.count == 0);
    srt_free_track_list(&list);

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

int wmain(void) {
    test_discovery_alphabetical();
    test_srt_parse_and_lookup();
    test_prefs_remember_position();
    test_trackbar_click_to_seek();
    wprintf(L"All tests passed.\n");
    return 0;
}

