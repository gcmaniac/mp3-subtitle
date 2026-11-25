#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "icons_data.h"
#include "player.h"
#include "playlist.h"
#include "prefs.h"
#include "resource.h"
#include "srt.h"
#include "trackbar.h"

#define PANEL_BASE_HEIGHT 132
#define SUB_SLOT_COUNT    3

enum {
    SUB_PRIMARY   = 0, /* Center */
    SUB_SECONDARY = 1, /* Bottom */
    SUB_TERTIARY  = 2  /* Top */
};

typedef struct SubSlot {
    HWND label;
    HWND combo;
    HWND btn_load;
    SrtSubtitle sub;
    wchar_t name[MAX_PATH];
    wchar_t cue_text[2048];
    bool is_custom;
} SubSlot;

typedef struct AppState {
    HWND hwnd;
    HWND btn_open;
    HWND btn_prev;
    HWND btn_play;
    HWND btn_stop;
    HWND btn_next;
    HWND chk_autonext;
    HWND btn_toggle_playlist;
    HWND slider_seek;
    HWND label_time;
    HWND chk_remember;
    HWND label_vol;
    HWND slider_vol;

    HWND lbl_playlist_title;
    HWND btn_close_playlist;
    HWND list_playlist;

    HICON icon_open;
    HICON icon_prev;
    HICON icon_play;
    HICON icon_pause;
    HICON icon_stop;
    HICON icon_next;
    HICON icon_playlist;
    HICON icon_close;

    SubSlot slots[SUB_SLOT_COUNT];

    HFONT ui_font;
    UINT dpi;

    Mp3Player player;
    Playlist playlist;
    SrtTrackList tracks;

    bool is_seeking;
    bool remember_pos;
    bool auto_next;
    bool show_playlist;
    bool user_stopped;
    bool track_ended_handled;
    long long preview_seek_ms;
    long long display_pos_ms;
    long long display_dur_ms;
    DWORD last_save_tick;
    int wheel_accum;
    TrackbarClickState seek_drag;
    TrackbarClickState vol_drag;
} AppState;

static AppState g_app;

static void open_audio_file(const wchar_t *audio_path);
static void on_track_ended(void);
static void play_prev_audio_file(void);
static void play_next_audio_file(void);
static void toggle_playlist_visibility(void);
static void update_playlist_ui(void);
static int get_playlist_width(void);
static void layout_controls(void);
static void recreate_button_icons(void);
static void create_main_menu(HWND hwnd);

static int scale_dpi(int px) {
    return MulDiv(px, (int)g_app.dpi, 96);
}

static int get_playlist_width(void) {
    return g_app.show_playlist ? scale_dpi(250) : 0;
}

static int get_stage_height(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int h = rc.bottom - rc.top;
    int panel_h = scale_dpi(PANEL_BASE_HEIGHT);
    return (h > panel_h) ? (h - panel_h) : 0;
}

static const wchar_t *get_filename_part(const wchar_t *path) {
    if (!path) return L"";
    const wchar_t *s1 = wcsrchr(path, L'\\');
    const wchar_t *s2 = wcsrchr(path, L'/');
    const wchar_t *sep = (s1 && s2) ? (s1 > s2 ? s1 : s2) : (s1 ? s1 : s2);
    return sep ? (sep + 1) : path;
}

static void format_time_ms(long long ms, wchar_t *buf, size_t buf_chars) {
    if (ms < 0) ms = 0;
    long long total_sec = ms / 1000;
    long long hours = total_sec / 3600;
    long long mins = (total_sec % 3600) / 60;
    long long secs = total_sec % 60;
    if (hours > 0) {
        _snwprintf(buf, buf_chars, L"%lld:%02lld:%02lld", hours, mins, secs);
    } else {
        _snwprintf(buf, buf_chars, L"%02lld:%02lld", mins, secs);
    }
}

static bool any_combo_dropped(void) {
    for (int i = 0; i < SUB_SLOT_COUNT; i++) {
        if (g_app.slots[i].combo &&
            SendMessageW(g_app.slots[i].combo, CB_GETDROPPEDSTATE, 0, 0)) {
            return true;
        }
    }
    return false;
}

static void update_window_title(void) {
    if (g_app.player.is_open) {
        wchar_t title[MAX_PATH + 64];
        _snwprintf(title, sizeof(title) / sizeof(title[0]),
                   L"%ls - Audio Subtitle Player",
                   get_filename_part(g_app.player.current_path));
        SetWindowTextW(g_app.hwnd, title);
    } else {
        SetWindowTextW(g_app.hwnd, L"Audio Subtitle Player");
    }
}

static void update_play_button_text(void) {
    if (g_app.btn_play) {
        HICON icon = (g_app.player.state == PLAYER_PLAYING) ? g_app.icon_pause : g_app.icon_play;
        SendMessageW(g_app.btn_play, BM_SETIMAGE, IMAGE_ICON, (LPARAM)icon);
    }
}

static void invalidate_stage(void) {
    RECT rc;
    GetClientRect(g_app.hwnd, &rc);
    rc.bottom = get_stage_height(g_app.hwnd);
    rc.right = rc.right - rc.left - get_playlist_width();
    InvalidateRect(g_app.hwnd, &rc, FALSE);
}

static void invalidate_stage_header(void) {
    RECT rc;
    GetClientRect(g_app.hwnd, &rc);
    int stage_h = get_stage_height(g_app.hwnd);
    int header_h = scale_dpi(46);
    if (stage_h < header_h) header_h = stage_h;
    rc.bottom = header_h;
    rc.right = rc.right - rc.left - get_playlist_width();
    InvalidateRect(g_app.hwnd, &rc, FALSE);
}

static void save_current_position(bool reset_if_ended) {
    if (!g_app.player.is_open || !g_app.remember_pos) {
        return;
    }
    long long pos = player_get_position(&g_app.player);
    if (reset_if_ended && g_app.player.duration_ms > 0 &&
        pos >= g_app.player.duration_ms - 1000) {
        pos = 0;
    }
    prefs_save_file_position(g_app.player.current_path, pos);
    g_app.last_save_tick = GetTickCount();
}

static void update_time_and_subtitle(void) {
    long long pos = 0;
    long long dur = 0;

    if (g_app.player.is_open) {
        PlayerState prev_state = g_app.player.state;
        player_refresh_state(&g_app.player);
        if (prev_state != g_app.player.state) {
            update_play_button_text();
            invalidate_stage();
            if (g_app.player.state == PLAYER_STOPPED) {
                save_current_position(true);
                if (prev_state == PLAYER_PLAYING && !g_app.user_stopped &&
                    g_app.player.duration_ms > 0 &&
                    g_app.display_pos_ms >= g_app.player.duration_ms - 1500) {
                    on_track_ended();
                }
            }
        }
        dur = g_app.player.duration_ms;
        pos = g_app.is_seeking ? g_app.preview_seek_ms : player_get_position(&g_app.player);

        if (g_app.remember_pos && g_app.player.state == PLAYER_PLAYING) {
            DWORD now = GetTickCount();
            if (now - g_app.last_save_tick >= 2000) {
                save_current_position(false);
            }
        }
    }

    g_app.display_pos_ms = pos;
    g_app.display_dur_ms = dur;

    long long rem = (dur > pos) ? (dur - pos) : 0;
    wchar_t rem_str[32], cur_str[32], dur_str[32], label_str[96];
    format_time_ms(rem, rem_str, 32);
    format_time_ms(pos, cur_str, 32);
    format_time_ms(dur, dur_str, 32);
    _snwprintf(label_str, sizeof(label_str) / sizeof(label_str[0]),
               L"%ls / %ls / %ls", rem_str, cur_str, dur_str);

    wchar_t old_label[96] = {0};
    GetWindowTextW(g_app.label_time, old_label, 96);
    if (wcscmp(old_label, label_str) != 0) {
        SetWindowTextW(g_app.label_time, label_str);
        invalidate_stage_header();
    }

    if (!g_app.is_seeking) {
        int slider_pos = 0;
        if (dur > 0) {
            slider_pos = (int)((pos * 10000LL) / dur);
            if (slider_pos < 0) slider_pos = 0;
            if (slider_pos > 10000) slider_pos = 10000;
        }
        LRESULT cur_thumb = SendMessageW(g_app.slider_seek, TBM_GETPOS, 0, 0);
        if ((int)cur_thumb != slider_pos) {
            SendMessageW(g_app.slider_seek, TBM_SETPOS, TRUE, (LPARAM)slider_pos);
        }
    }

    bool cue_changed = false;
    for (int i = 0; i < SUB_SLOT_COUNT; i++) {
        SubSlot *sl = &g_app.slots[i];
        wchar_t new_cue[2048] = {0};
        if (g_app.player.is_open && sl->sub.count > 0) {
            srt_get_active_text(&sl->sub, pos, new_cue, sizeof(new_cue) / sizeof(new_cue[0]));
        }
        if (wcscmp(sl->cue_text, new_cue) != 0) {
            wcsncpy(sl->cue_text, new_cue, sizeof(sl->cue_text) / sizeof(sl->cue_text[0]) - 1);
            sl->cue_text[sizeof(sl->cue_text) / sizeof(sl->cue_text[0]) - 1] = L'\0';
            cue_changed = true;
        }
    }
    if (cue_changed) {
        invalidate_stage();
    }
}

static void populate_slot_combo(int slot, int select_index) {
    if (slot < 0 || slot >= SUB_SLOT_COUNT) return;
    SubSlot *sl = &g_app.slots[slot];

    SendMessageW(sl->combo, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_app.tracks.count; i++) {
        SendMessageW(sl->combo, CB_ADDSTRING, 0, (LPARAM)g_app.tracks.names[i]);
    }
    if (sl->is_custom && sl->name[0] != L'\0') {
        SendMessageW(sl->combo, CB_ADDSTRING, 0, (LPARAM)sl->name);
    }
    SendMessageW(sl->combo, CB_ADDSTRING, 0, (LPARAM)L"(None / Off)");

    int total = (int)SendMessageW(sl->combo, CB_GETCOUNT, 0, 0);
    if (select_index < 0 || select_index >= total) {
        select_index = total - 1;
    }
    SendMessageW(sl->combo, CB_SETCURSEL, (WPARAM)select_index, 0);
}

static void clear_slot_subtitle(int slot) {
    if (slot < 0 || slot >= SUB_SLOT_COUNT) return;
    SubSlot *sl = &g_app.slots[slot];
    srt_free_subtitle(&sl->sub);
    sl->is_custom = false;
    sl->name[0] = L'\0';
    sl->cue_text[0] = L'\0';
}

static bool load_slot_subtitle(int slot, const wchar_t *srt_path, bool is_custom) {
    if (slot < 0 || slot >= SUB_SLOT_COUNT) return false;
    SrtSubtitle new_sub;
    if (!srt_load_file(srt_path, &new_sub)) {
        return false;
    }
    SubSlot *sl = &g_app.slots[slot];
    srt_free_subtitle(&sl->sub);
    sl->sub = new_sub;
    sl->is_custom = is_custom;
    wcsncpy(sl->name, get_filename_part(srt_path), MAX_PATH - 1);
    sl->name[MAX_PATH - 1] = L'\0';
    sl->cue_text[0] = L'\0';
    update_time_and_subtitle();
    invalidate_stage();
    return true;
}

static void open_audio_file(const wchar_t *audio_path) {
    if (!audio_path || !audio_path[0]) {
        return;
    }
    save_current_position(true);

    g_app.user_stopped = false;
    g_app.track_ended_handled = false;

    if (!player_open(&g_app.player, audio_path)) {
        MessageBoxW(g_app.hwnd,
                    g_app.player.last_error[0] ? g_app.player.last_error : L"Failed to open audio file.",
                    L"Playback Error", MB_OK | MB_ICONERROR);
        return;
    }

    playlist_scan_folder(&g_app.playlist, audio_path);
    update_playlist_ui();

    for (int i = 0; i < SUB_SLOT_COUNT; i++) {
        clear_slot_subtitle(i);
    }
    srt_free_track_list(&g_app.tracks);

    srt_discover_tracks(audio_path, &g_app.tracks);

    /* Primary defaults to the alphabetically first matching .srt / .lang.srt */
    if (g_app.tracks.count > 0 && load_slot_subtitle(SUB_PRIMARY, g_app.tracks.paths[0], false)) {
        populate_slot_combo(SUB_PRIMARY, 0);
    } else {
        populate_slot_combo(SUB_PRIMARY, (int)g_app.tracks.count);
    }
    populate_slot_combo(SUB_SECONDARY, (int)g_app.tracks.count);
    populate_slot_combo(SUB_TERTIARY, (int)g_app.tracks.count);

    EnableWindow(g_app.btn_play, TRUE);
    EnableWindow(g_app.btn_stop, TRUE);
    EnableWindow(g_app.btn_prev, TRUE);
    EnableWindow(g_app.btn_next, TRUE);
    EnableWindow(g_app.slider_seek, TRUE);

    long long resume_ms = 0;
    if (g_app.remember_pos) {
        resume_ms = prefs_load_file_position(audio_path);
        if (g_app.player.duration_ms > 0 && resume_ms >= g_app.player.duration_ms - 1000) {
            resume_ms = 0;
        }
    }
    if (resume_ms > 0) {
        player_play_from(&g_app.player, resume_ms);
    } else {
        player_play(&g_app.player);
    }
    g_app.last_save_tick = GetTickCount();

    prefs_add_history(audio_path);
    create_main_menu(g_app.hwnd);

    update_play_button_text();
    update_window_title();
    update_time_and_subtitle();
    invalidate_stage();
}

static void on_track_ended(void) {
    if (g_app.track_ended_handled) {
        return;
    }
    g_app.track_ended_handled = true;

    if (g_app.user_stopped) {
        return;
    }

    if (g_app.auto_next && g_app.player.is_open) {
        wchar_t next_path[MAX_PATH];
        if (player_find_next_audio_file(g_app.player.current_path, next_path, MAX_PATH)) {
            open_audio_file(next_path);
            return;
        }
    }

    /* Last file in folder or auto-next disabled: perform a clean stop & rewind to start */
    if (g_app.player.is_open) {
        player_stop(&g_app.player);
        if (g_app.remember_pos) {
            prefs_save_file_position(g_app.player.current_path, 0);
        }
    }
    update_play_button_text();
    update_time_and_subtitle();
    invalidate_stage();
}

static void update_playlist_ui(void) {
    if (!g_app.list_playlist) return;

    wchar_t title_buf[64];
    _snwprintf(title_buf, sizeof(title_buf) / sizeof(title_buf[0]),
               L"Playlist (%d)", (int)g_app.playlist.count);
    if (g_app.lbl_playlist_title) {
        SetWindowTextW(g_app.lbl_playlist_title, title_buf);
    }

    int cur_count = (int)SendMessageW(g_app.list_playlist, LB_GETCOUNT, 0, 0);
    if (cur_count != (int)g_app.playlist.count) {
        SendMessageW(g_app.list_playlist, WM_SETREDRAW, FALSE, 0);
        SendMessageW(g_app.list_playlist, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < g_app.playlist.count; i++) {
            SendMessageW(g_app.list_playlist, LB_ADDSTRING, 0, (LPARAM)g_app.playlist.names[i]);
        }
        SendMessageW(g_app.list_playlist, WM_SETREDRAW, TRUE, 0);
    }

    if (g_app.playlist.current_index >= 0) {
        SendMessageW(g_app.list_playlist, LB_SETCURSEL, (WPARAM)g_app.playlist.current_index, 0);
        int top = g_app.playlist.current_index > 3 ? g_app.playlist.current_index - 3 : 0;
        SendMessageW(g_app.list_playlist, LB_SETTOPINDEX, (WPARAM)top, 0);
    }
}

static void toggle_playlist_visibility(void) {
    g_app.show_playlist = !g_app.show_playlist;
    prefs_set_show_playlist(g_app.show_playlist);

    int sw = g_app.show_playlist ? SW_SHOW : SW_HIDE;
    if (g_app.lbl_playlist_title) ShowWindow(g_app.lbl_playlist_title, sw);
    if (g_app.btn_close_playlist) ShowWindow(g_app.btn_close_playlist, sw);
    if (g_app.list_playlist) ShowWindow(g_app.list_playlist, sw);

    HMENU hMenu = GetMenu(g_app.hwnd);
    if (hMenu) {
        CheckMenuItem(hMenu, IDM_VIEW_PLAYLIST,
                      MF_BYCOMMAND | (g_app.show_playlist ? MF_CHECKED : MF_UNCHECKED));
    }

    layout_controls();
    InvalidateRect(g_app.hwnd, NULL, TRUE);
}

static void play_prev_audio_file(void) {
    if (!g_app.player.is_open) return;
    wchar_t prev_path[MAX_PATH];
    if (player_find_prev_audio_file(g_app.player.current_path, prev_path, MAX_PATH)) {
        open_audio_file(prev_path);
    } else {
        /* Rewind to start if already on the first file */
        player_seek(&g_app.player, 0);
        save_current_position(false);
        update_time_and_subtitle();
    }
}

static void play_next_audio_file(void) {
    if (!g_app.player.is_open) return;
    wchar_t next_path[MAX_PATH];
    if (player_find_next_audio_file(g_app.player.current_path, next_path, MAX_PATH)) {
        open_audio_file(next_path);
    } else {
        /* Stop and rewind to start if on the last file */
        player_stop(&g_app.player);
        if (g_app.remember_pos) {
            prefs_save_file_position(g_app.player.current_path, 0);
        }
        update_play_button_text();
        update_time_and_subtitle();
        invalidate_stage();
    }
}

static void show_open_audio_dialog(void) {
    wchar_t file_buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.hwnd;
    ofn.lpstrFilter =
        L"All Supported Audio Files (*.mp3;*.ogg;*.wav;*.wma;*.m4a;*.aac;*.flac;...)\0*.mp3;*.ogg;*.oga;*.opus;*.wav;*.wma;*.m4a;*.aac;*.flac;*.mid;*.midi;*.aif;*.aiff;*.mp2;*.mpa\0"
        L"OGG Audio (*.ogg)\0*.ogg\0"
        L"MP3 Audio (*.mp3)\0*.mp3\0"
        L"Waveform Audio (*.wav)\0*.wav\0"
        L"Windows Media Audio (*.wma)\0*.wma\0"
        L"M4A / AAC Audio (*.m4a;*.aac)\0*.m4a;*.aac\0"
        L"FLAC Audio (*.flac)\0*.flac\0"
        L"Ogg Vorbis / Opus Audio (*.ogg;*.oga;*.opus)\0*.ogg;*.oga;*.opus\0"
        L"MIDI Sequences (*.mid;*.midi)\0*.mid;*.midi\0"
        L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = L"Open Audio File";

    if (GetOpenFileNameW(&ofn)) {
        open_audio_file(file_buf);
    }
}

static void show_open_srt_dialog_for_slot(int slot) {
    static const wchar_t *titles[SUB_SLOT_COUNT] = {
        L"Load Primary Subtitle (.srt) - Center",
        L"Load Secondary Subtitle (.srt) - Bottom",
        L"Load Tertiary Subtitle (.srt) - Top"
    };
    wchar_t file_buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.hwnd;
    ofn.lpstrFilter = L"SubRip Subtitles (*.srt)\0*.srt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle = titles[slot];

    if (GetOpenFileNameW(&ofn)) {
        if (load_slot_subtitle(slot, file_buf, true)) {
            populate_slot_combo(slot, (int)g_app.tracks.count);
        } else {
            MessageBoxW(g_app.hwnd, L"Failed to load subtitle (.srt) file.",
                        L"Subtitle Error", MB_OK | MB_ICONERROR);
        }
    }
}

static void on_slot_combo_changed(int slot) {
    if (slot < 0 || slot >= SUB_SLOT_COUNT) return;
    SubSlot *sl = &g_app.slots[slot];
    int sel = (int)SendMessageW(sl->combo, CB_GETCURSEL, 0, 0);
    if (sel < 0) return;

    if ((size_t)sel < g_app.tracks.count) {
        load_slot_subtitle(slot, g_app.tracks.paths[sel], false);
    } else if (sl->is_custom && (size_t)sel == g_app.tracks.count) {
        /* Keep currently loaded custom subtitle */
    } else {
        clear_slot_subtitle(slot);
        invalidate_stage();
    }
}

static void recreate_ui_font(void) {
    if (g_app.ui_font) {
        DeleteObject(g_app.ui_font);
    }
    int font_h = -MulDiv(9, (int)g_app.dpi, 72);
    g_app.ui_font = CreateFontW(font_h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HWND ctrls[] = {
        g_app.btn_open, g_app.btn_play, g_app.btn_stop, g_app.chk_autonext,
        g_app.btn_toggle_playlist, g_app.lbl_playlist_title, g_app.btn_close_playlist,
        g_app.list_playlist,
        g_app.label_time, g_app.chk_remember, g_app.label_vol,
        g_app.slots[0].label, g_app.slots[0].combo, g_app.slots[0].btn_load,
        g_app.slots[1].label, g_app.slots[1].combo, g_app.slots[1].btn_load,
        g_app.slots[2].label, g_app.slots[2].combo, g_app.slots[2].btn_load
    };
    for (size_t i = 0; i < sizeof(ctrls) / sizeof(ctrls[0]); i++) {
        if (ctrls[i]) {
            SendMessageW(ctrls[i], WM_SETFONT, (WPARAM)g_app.ui_font, TRUE);
        }
    }
}

static void layout_controls(void) {
    RECT rc;
    GetClientRect(g_app.hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int stage_h = get_stage_height(g_app.hwnd);

    int pl_w = get_playlist_width();
    int main_w = w - pl_w;
    if (main_w < scale_dpi(480)) {
        main_w = scale_dpi(480);
    }

    int pad = scale_dpi(12);
    int gap = scale_dpi(8);

    /* Row 1: Seek bar + time label + remember-position checkbox */
    int row1_y = stage_h + scale_dpi(10);
    int row1_h = scale_dpi(28);
    int time_w = scale_dpi(175);
    int chk_w = scale_dpi(118);
    int seek_w = main_w - pad * 2 - time_w - chk_w - gap * 2;
    if (seek_w < scale_dpi(100)) seek_w = scale_dpi(100);

    MoveWindow(g_app.slider_seek, pad, row1_y, seek_w, row1_h, TRUE);
    MoveWindow(g_app.label_time, pad + seek_w + gap, row1_y + scale_dpi(2),
               time_w, scale_dpi(24), TRUE);
    MoveWindow(g_app.chk_remember, pad + seek_w + gap + time_w + gap, row1_y + scale_dpi(2),
               chk_w, scale_dpi(24), TRUE);

    /* Row 2: Transport buttons on left, Volume on right */
    int row2_y = stage_h + scale_dpi(44);
    int btn_h = scale_dpi(32);
    int x = pad;
    int w_btn = scale_dpi(36);
    int w_play = scale_dpi(44);
    int gap_btn = scale_dpi(4);
    int group_gap = scale_dpi(10);
    int w_autonext = scale_dpi(92);

    /* Open */
    MoveWindow(g_app.btn_open, x, row2_y, w_btn, btn_h, TRUE);
    x += w_btn + group_gap;

    /* Prev, Play, Stop, Next */
    MoveWindow(g_app.btn_prev, x, row2_y, w_btn, btn_h, TRUE);
    x += w_btn + gap_btn;

    MoveWindow(g_app.btn_play, x, row2_y, w_play, btn_h, TRUE);
    x += w_play + gap_btn;

    MoveWindow(g_app.btn_stop, x, row2_y, w_btn, btn_h, TRUE);
    x += w_btn + gap_btn;

    MoveWindow(g_app.btn_next, x, row2_y, w_btn, btn_h, TRUE);
    x += w_btn + group_gap;

    /* Playlist toggle */
    MoveWindow(g_app.btn_toggle_playlist, x, row2_y, w_btn, btn_h, TRUE);
    x += w_btn + group_gap;

    /* Auto next checkbox */
    MoveWindow(g_app.chk_autonext, x, row2_y + scale_dpi(4), w_autonext, scale_dpi(24), TRUE);

    int w_vol_lbl = scale_dpi(34);
    int w_vol_slider = scale_dpi(140);
    int vol_x = main_w - pad - w_vol_slider;
    int vol_lbl_x = vol_x - w_vol_lbl - scale_dpi(4);

    MoveWindow(g_app.label_vol, vol_lbl_x, row2_y + scale_dpi(4), w_vol_lbl, scale_dpi(24), TRUE);
    MoveWindow(g_app.slider_vol, vol_x, row2_y + scale_dpi(2), w_vol_slider, scale_dpi(28), TRUE);

    /* Row 3: Three equal columns for Primary (Center), Secondary (Bottom), Tertiary (Top) */
    int row3_y = stage_h + scale_dpi(84);
    int col_gap = scale_dpi(12);
    int total_w = main_w - pad * 2 - col_gap * 2;
    int col_w = total_w / 3;
    if (col_w < scale_dpi(180)) col_w = scale_dpi(180);

    int w_lbl = scale_dpi(68);
    int w_browse = scale_dpi(36);
    int combo_w = col_w - w_lbl - w_browse - scale_dpi(8);
    if (combo_w < scale_dpi(60)) combo_w = scale_dpi(60);

    for (int i = 0; i < SUB_SLOT_COUNT; i++) {
        int cx = pad + i * (col_w + col_gap);
        MoveWindow(g_app.slots[i].label, cx, row3_y + scale_dpi(3), w_lbl, scale_dpi(24), TRUE);
        MoveWindow(g_app.slots[i].combo, cx + w_lbl + scale_dpi(4), row3_y + scale_dpi(2),
                   combo_w, scale_dpi(220), TRUE);
        MoveWindow(g_app.slots[i].btn_load, cx + w_lbl + scale_dpi(4) + combo_w + scale_dpi(4),
                   row3_y, w_browse, btn_h, TRUE);
    }

    /* Playlist sidebar controls */
    if (g_app.show_playlist) {
        int pl_x = main_w + scale_dpi(1);
        int pl_actual_w = w - pl_x;
        if (pl_actual_w > 0) {
            MoveWindow(g_app.lbl_playlist_title,
                       pl_x + scale_dpi(8), scale_dpi(6),
                       pl_actual_w - scale_dpi(42), scale_dpi(24), TRUE);
            MoveWindow(g_app.btn_close_playlist,
                       pl_x + pl_actual_w - scale_dpi(30), scale_dpi(6),
                       scale_dpi(24), scale_dpi(24), TRUE);
            MoveWindow(g_app.list_playlist,
                       pl_x + scale_dpi(4), scale_dpi(34),
                       pl_actual_w - scale_dpi(8), h - scale_dpi(38), TRUE);
        }
    }
}

/* v_align: -1 = top (Tertiary), 0 = center (Primary), +1 = bottom (Secondary) */
static void draw_subtitle_box(HDC mem_dc, int w, int content_top, int content_h,
                              const wchar_t *text, int pt_size, COLORREF text_color,
                              int v_align) {
    if (!text || text[0] == L'\0' || content_h <= 0) return;

    HFONT sub_font = CreateFontW(-MulDiv(pt_size, (int)g_app.dpi, 72), 0, 0, 0, FW_BOLD,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                                 L"Segoe UI");
    HFONT old_f = (HFONT)SelectObject(mem_dc, sub_font);

    int margin_x = w / 14;
    if (margin_x < scale_dpi(24)) margin_x = scale_dpi(24);
    RECT calc_rc = {margin_x, 0, w - margin_x, content_h};
    DrawTextW(mem_dc, text, -1, &calc_rc,
              DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

    int text_w = calc_rc.right - calc_rc.left;
    int text_h = calc_rc.bottom - calc_rc.top;
    int pad_x = scale_dpi(22);
    int pad_y = scale_dpi(10);
    int total_h = text_h + pad_y * 2;

    int box_left = (w - text_w) / 2 - pad_x;
    int box_right = (w + text_w) / 2 + pad_x;
    if (box_left < margin_x / 2) box_left = margin_x / 2;
    if (box_right > w - margin_x / 2) box_right = w - margin_x / 2;

    int box_top = content_top;
    if (v_align < 0) {
        box_top = content_top + scale_dpi(8);
    } else if (v_align == 0) {
        box_top = content_top + (content_h - total_h) / 2;
    } else {
        box_top = content_top + content_h - total_h - scale_dpi(8);
    }
    if (box_top < content_top) box_top = content_top;
    int box_bottom = box_top + total_h;
    if (box_bottom > content_top + content_h) {
        box_bottom = content_top + content_h;
    }

    RECT box_rc = {box_left, box_top, box_right, box_bottom};
    HBRUSH pill_br = CreateSolidBrush(RGB(10, 12, 16));
    HPEN pill_pen = CreatePen(PS_SOLID, 1, RGB(46, 54, 70));
    HBRUSH old_br = (HBRUSH)SelectObject(mem_dc, pill_br);
    HPEN old_p = (HPEN)SelectObject(mem_dc, pill_pen);
    RoundRect(mem_dc, box_rc.left, box_rc.top, box_rc.right, box_rc.bottom,
              scale_dpi(12), scale_dpi(12));
    SelectObject(mem_dc, old_br);
    SelectObject(mem_dc, old_p);
    DeleteObject(pill_br);
    DeleteObject(pill_pen);

    RECT txt_rc = {box_rc.left + pad_x / 2, box_rc.top + pad_y,
                   box_rc.right - pad_x / 2, box_rc.bottom - pad_y};
    RECT shadow_rc = txt_rc;
    int sh_off = scale_dpi(2);
    OffsetRect(&shadow_rc, sh_off, sh_off);
    SetTextColor(mem_dc, RGB(0, 0, 0));
    DrawTextW(mem_dc, text, -1, &shadow_rc,
              DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

    SetTextColor(mem_dc, text_color);
    DrawTextW(mem_dc, text, -1, &txt_rc,
              DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

    SelectObject(mem_dc, old_f);
    DeleteObject(sub_font);
}

static void paint_stage(HDC hdc, int w, int stage_h) {
    if (w <= 0 || stage_h <= 0) return;

    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, stage_h);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, mem_bmp);

    /* Deep cinema background */
    RECT stage_rc = {0, 0, w, stage_h};
    HBRUSH bg_brush = CreateSolidBrush(RGB(18, 20, 26));
    FillRect(mem_dc, &stage_rc, bg_brush);
    DeleteObject(bg_brush);

    /* Top header strip */
    int header_h = scale_dpi(46);
    if (stage_h > header_h) {
        RECT hdr_rc = {0, 0, w, header_h};
        HBRUSH hdr_brush = CreateSolidBrush(RGB(26, 29, 38));
        FillRect(mem_dc, &hdr_rc, hdr_brush);
        DeleteObject(hdr_brush);

        HPEN div_pen = CreatePen(PS_SOLID, 1, RGB(44, 49, 62));
        HPEN old_pen = (HPEN)SelectObject(mem_dc, div_pen);
        MoveToEx(mem_dc, 0, header_h - 1, NULL);
        LineTo(mem_dc, w, header_h - 1);
        SelectObject(mem_dc, old_pen);
        DeleteObject(div_pen);

        int hdr_font_h = -MulDiv(10, (int)g_app.dpi, 72);
        HFONT hdr_font = CreateFontW(hdr_font_h, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT old_font = (HFONT)SelectObject(mem_dc, hdr_font);
        SetBkMode(mem_dc, TRANSPARENT);

        int pad = scale_dpi(18);
        RECT left_rc = {pad, 0, w / 2, header_h};
        SetTextColor(mem_dc, RGB(226, 232, 240));
        if (g_app.player.is_open) {
            const wchar_t *state_tag =
                (g_app.player.state == PLAYER_PLAYING) ? L"PLAYING" :
                (g_app.player.state == PLAYER_PAUSED) ? L"PAUSED" : L"STOPPED";
            long long rem_ms = (g_app.display_dur_ms > g_app.display_pos_ms)
                                   ? (g_app.display_dur_ms - g_app.display_pos_ms)
                                   : 0;
            wchar_t rem_t[32], cur_t[32], dur_t[32];
            format_time_ms(rem_ms, rem_t, 32);
            format_time_ms(g_app.display_pos_ms, cur_t, 32);
            format_time_ms(g_app.display_dur_ms, dur_t, 32);
            wchar_t left_txt[MAX_PATH + 96];
            _snwprintf(left_txt, sizeof(left_txt) / sizeof(left_txt[0]),
                       L"[%ls]  %ls / %ls / %ls  |  %ls",
                       state_tag, rem_t, cur_t, dur_t,
                       get_filename_part(g_app.player.current_path));
            DrawTextW(mem_dc, left_txt, -1, &left_rc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
            DrawTextW(mem_dc, L"Audio Subtitle Player  |  00:00 / 00:00 / 00:00", -1, &left_rc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        RECT right_rc = {w / 2, 0, w - pad, header_h};
        SetTextColor(mem_dc, RGB(148, 163, 184));
        wchar_t right_txt[MAX_PATH * 3 + 128];
        if (g_app.player.is_open) {
            const wchar_t *p_name = g_app.slots[SUB_PRIMARY].name[0] ? g_app.slots[SUB_PRIMARY].name : L"-";
            const wchar_t *s_name = g_app.slots[SUB_SECONDARY].name[0] ? g_app.slots[SUB_SECONDARY].name : L"-";
            const wchar_t *t_name = g_app.slots[SUB_TERTIARY].name[0] ? g_app.slots[SUB_TERTIARY].name : L"-";
            _snwprintf(right_txt, sizeof(right_txt) / sizeof(right_txt[0]),
                       L"Vol: %d%%  |  Pri: %ls  |  Sec: %ls  |  Ter: %ls",
                       g_app.player.volume_percent, p_name, s_name, t_name);
        } else {
            _snwprintf(right_txt, sizeof(right_txt) / sizeof(right_txt[0]),
                       L"Vol: %d%%", g_app.player.volume_percent);
        }
        DrawTextW(mem_dc, right_txt, -1, &right_rc,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SelectObject(mem_dc, old_font);
        DeleteObject(hdr_font);
    }

    /* Main stage content */
    SetBkMode(mem_dc, TRANSPARENT);
    int content_top = header_h + scale_dpi(12);
    int content_h = stage_h - content_top - scale_dpi(12);

    if (!g_app.player.is_open) {
        int title_pt = stage_h / 26;
        if (title_pt < 14) title_pt = 14;
        if (title_pt > 24) title_pt = 24;
        HFONT f_main = CreateFontW(-MulDiv(title_pt, (int)g_app.dpi, 72), 0, 0, 0, FW_SEMIBOLD,
                                   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                                   L"Segoe UI");
        HFONT f_sub = CreateFontW(-MulDiv(11, (int)g_app.dpi, 72), 0, 0, 0, FW_NORMAL,
                                  FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                                  L"Segoe UI");

        HFONT old_f = (HFONT)SelectObject(mem_dc, f_main);
        SetTextColor(mem_dc, RGB(203, 213, 225));
        RECT msg_rc = {scale_dpi(32), content_top + content_h / 3, w - scale_dpi(32), stage_h};
        DrawTextW(mem_dc, L"Open an audio file (Ctrl+O) or drag & drop audio here",
                  -1, &msg_rc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

        SelectObject(mem_dc, f_sub);
        SetTextColor(mem_dc, RGB(130, 142, 162));
        RECT hint_rc = {scale_dpi(32), content_top + content_h / 3 + scale_dpi(54), w - scale_dpi(32), stage_h};
        DrawTextW(mem_dc,
                  L"Subtitles matching <filename>.srt or <filename>.<lang>.srt auto-load alphabetically into Primary.\n"
                  L"Positions: Tertiary = Top  |  Primary = Center  |  Secondary = Bottom\n"
                  L"Click Display / Space = Play/Pause  |  Left/Right = Seek 5s  |  Up/Down or Scroll = Volume 5%",
                  -1, &hint_rc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);

        SelectObject(mem_dc, old_f);
        DeleteObject(f_main);
        DeleteObject(f_sub);
    } else {
        int pri_pt = stage_h / 20;
        if (pri_pt < 16) pri_pt = 16;
        if (pri_pt > 40) pri_pt = 40;

        int sec_pt = (pri_pt * 78) / 100;
        if (sec_pt < 13) sec_pt = 13;
        if (sec_pt > 30) sec_pt = 30;

        int ter_pt = (pri_pt * 72) / 100;
        if (ter_pt < 12) ter_pt = 12;
        if (ter_pt > 28) ter_pt = 28;

        /* Tertiary: Top */
        draw_subtitle_box(mem_dc, w, content_top, content_h,
                          g_app.slots[SUB_TERTIARY].cue_text,
                          ter_pt, RGB(125, 211, 252), -1);

        /* Primary: Center */
        draw_subtitle_box(mem_dc, w, content_top, content_h,
                          g_app.slots[SUB_PRIMARY].cue_text,
                          pri_pt, RGB(250, 250, 252), 0);

        /* Secondary: Bottom */
        draw_subtitle_box(mem_dc, w, content_top, content_h,
                          g_app.slots[SUB_SECONDARY].cue_text,
                          sec_pt, RGB(253, 224, 71), +1);

        bool any_loaded = (g_app.slots[SUB_PRIMARY].sub.count > 0 ||
                           g_app.slots[SUB_SECONDARY].sub.count > 0 ||
                           g_app.slots[SUB_TERTIARY].sub.count > 0);
        if (!any_loaded) {
            HFONT f_note = CreateFontW(-MulDiv(11, (int)g_app.dpi, 72), 0, 0, 0, FW_NORMAL,
                                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                                       L"Segoe UI");
            HFONT old_f = (HFONT)SelectObject(mem_dc, f_note);
            SetTextColor(mem_dc, RGB(120, 132, 152));
            RECT note_rc = {scale_dpi(24), stage_h - scale_dpi(44), w - scale_dpi(24), stage_h - scale_dpi(12)};
            DrawTextW(mem_dc,
                      L"No subtitle loaded. Select or load Primary, Secondary, or Tertiary .srt files below.",
                      -1, &note_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(mem_dc, old_f);
            DeleteObject(f_note);
        }
    }

    BitBlt(hdc, 0, 0, w, stage_h, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
}

static void create_main_menu(HWND hwnd) {
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hPlay = CreatePopupMenu();

    AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN, L"&Open Audio...\tCtrl+O");

    /* Submenu for Recent Files / History reminder */
    HMENU hRecent = CreatePopupMenu();
    int hist_count = prefs_get_history_count();
    if (hist_count == 0) {
        AppendMenuW(hRecent, MF_STRING | MF_GRAYED, 0, L"(No Recent History)");
    } else {
        for (int i = 0; i < hist_count; i++) {
            wchar_t r_path[MAX_PATH] = {0};
            if (prefs_get_history_item(i, r_path, MAX_PATH)) {
                const wchar_t *fn = get_filename_part(r_path);
                long long saved_pos = prefs_load_file_position(r_path);
                wchar_t item_text[MAX_PATH + 64];
                int num = (i + 1) % 10;
                if (saved_pos > 0) {
                    wchar_t pos_str[32];
                    format_time_ms(saved_pos, pos_str, 32);
                    _snwprintf(item_text, sizeof(item_text) / sizeof(item_text[0]),
                               L"&%d. %ls  [Pos: %ls]", num, fn, pos_str);
                } else {
                    _snwprintf(item_text, sizeof(item_text) / sizeof(item_text[0]),
                               L"&%d. %ls", num, fn);
                }
                AppendMenuW(hRecent, MF_STRING, IDM_FILE_RECENT_BASE + i, item_text);
            }
        }
        AppendMenuW(hRecent, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hRecent, MF_STRING, IDM_FILE_CLEAR_RECENT, L"&Clear History");
    }
    AppendMenuW(hFile, MF_POPUP, (UINT_PTR)hRecent, L"Recent &History (Pengingat Riwayat)");

    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_FILE_LOADPRI, L"Load &Primary Subtitle (.srt)...");
    AppendMenuW(hFile, MF_STRING, IDM_FILE_LOADSEC, L"Load &Secondary Subtitle (.srt)...");
    AppendMenuW(hFile, MF_STRING, IDM_FILE_LOADTER, L"Load &Tertiary Subtitle (.srt)...");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT, L"E&xit\tAlt+F4");

    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_TOGGLE, L"&Play / Pause\tSpace");
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_STOP, L"&Stop");
    AppendMenuW(hPlay, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_PREVFILE, L"Pre&vious File\tCtrl+Left");
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_NEXTFILE, L"Ne&xt File\tCtrl+Right");
    AppendMenuW(hPlay, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_BACK5, L"Seek &Backward 5s\tLeft");
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_FWD5, L"Seek &Forward 5s\tRight");

    HMENU hView = CreatePopupMenu();
    AppendMenuW(hView, MF_STRING | (g_app.show_playlist ? MF_CHECKED : MF_UNCHECKED),
                IDM_VIEW_PLAYLIST, L"&Playlist\tCtrl+P");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPlay, L"&Playback");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

    HMENU oldMenu = GetMenu(hwnd);
    SetMenu(hwnd, hMenu);
    if (oldMenu) {
        DestroyMenu(oldMenu);
    }
}

static HICON create_icon_from_mask_32(const unsigned char *mask_32, int target_sz) {
    if (target_sz < 8) target_sz = 8;
    HDC hdc_screen = GetDC(NULL);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = target_sz;
    bi.bmiHeader.biHeight = target_sz; /* bottom-up DIB */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    RGBQUAD *pixels = NULL;
    HBITMAP hColor = CreateDIBSection(hdc_screen, &bi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    ReleaseDC(NULL, hdc_screen);
    if (!hColor || !pixels) return NULL;

    COLORREF text_color = GetSysColor(COLOR_BTNTEXT);
    BYTE r = GetRValue(text_color);
    BYTE g = GetGValue(text_color);
    BYTE b = GetBValue(text_color);

    for (int y = 0; y < target_sz; y++) {
        int dib_y = target_sz - 1 - y;
        float sy = ((float)y + 0.5f) * 32.0f / (float)target_sz - 0.5f;
        if (sy < 0.0f) sy = 0.0f;
        if (sy > 31.0f) sy = 31.0f;
        int y0 = (int)sy;
        int y1 = (y0 < 31) ? y0 + 1 : 31;
        float fy = sy - (float)y0;

        for (int x = 0; x < target_sz; x++) {
            float sx = ((float)x + 0.5f) * 32.0f / (float)target_sz - 0.5f;
            if (sx < 0.0f) sx = 0.0f;
            if (sx > 31.0f) sx = 31.0f;
            int x0 = (int)sx;
            int x1 = (x0 < 31) ? x0 + 1 : 31;
            float fx = sx - (float)x0;

            float v00 = (float)mask_32[y0 * 32 + x0];
            float v01 = (float)mask_32[y0 * 32 + x1];
            float v10 = (float)mask_32[y1 * 32 + x0];
            float v11 = (float)mask_32[y1 * 32 + x1];

            float v0 = v00 + fx * (v01 - v00);
            float v1 = v10 + fx * (v11 - v10);
            float val = v0 + fy * (v1 - v0);
            if (val < 0.0f) val = 0.0f;
            if (val > 255.0f) val = 255.0f;
            BYTE a = (BYTE)(val + 0.5f);

            RGBQUAD *p = &pixels[dib_y * target_sz + x];
            p->rgbRed   = (BYTE)((r * a) / 255);
            p->rgbGreen = (BYTE)((g * a) / 255);
            p->rgbBlue  = (BYTE)((b * a) / 255);
            p->rgbReserved = a;
        }
    }

    HBITMAP hMask = CreateBitmap(target_sz, target_sz, 1, 1, NULL);
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hColor;
    ii.hbmMask = hMask;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hColor);
    DeleteObject(hMask);
    return hIcon;
}

static void recreate_button_icons(void) {
    if (g_app.icon_open)     { DestroyIcon(g_app.icon_open);     g_app.icon_open = NULL; }
    if (g_app.icon_prev)     { DestroyIcon(g_app.icon_prev);     g_app.icon_prev = NULL; }
    if (g_app.icon_play)     { DestroyIcon(g_app.icon_play);     g_app.icon_play = NULL; }
    if (g_app.icon_pause)    { DestroyIcon(g_app.icon_pause);    g_app.icon_pause = NULL; }
    if (g_app.icon_stop)     { DestroyIcon(g_app.icon_stop);     g_app.icon_stop = NULL; }
    if (g_app.icon_next)     { DestroyIcon(g_app.icon_next);     g_app.icon_next = NULL; }
    if (g_app.icon_playlist) { DestroyIcon(g_app.icon_playlist); g_app.icon_playlist = NULL; }
    if (g_app.icon_close)    { DestroyIcon(g_app.icon_close);    g_app.icon_close = NULL; }

    int sz = scale_dpi(18);
    if (sz < 14) sz = 14;

    int sz_close = scale_dpi(12);
    if (sz_close < 10) sz_close = 10;

    g_app.icon_open     = create_icon_from_mask_32(g_icon_open_32, sz);
    g_app.icon_prev     = create_icon_from_mask_32(g_icon_prev_32, sz);
    g_app.icon_play     = create_icon_from_mask_32(g_icon_play_32, sz);
    g_app.icon_pause    = create_icon_from_mask_32(g_icon_pause_32, sz);
    g_app.icon_stop     = create_icon_from_mask_32(g_icon_stop_32, sz);
    g_app.icon_next     = create_icon_from_mask_32(g_icon_next_32, sz);
    g_app.icon_playlist = create_icon_from_mask_32(g_icon_playlist_32, sz);
    g_app.icon_close    = create_icon_from_mask_32(g_icon_close_32, sz_close);

    if (g_app.btn_open) {
        SendMessageW(g_app.btn_open, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_app.icon_open);
    }
    if (g_app.btn_prev) {
        SendMessageW(g_app.btn_prev, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_app.icon_prev);
    }
    if (g_app.btn_play) {
        HICON cur_play = (g_app.player.state == PLAYER_PLAYING) ? g_app.icon_pause : g_app.icon_play;
        SendMessageW(g_app.btn_play, BM_SETIMAGE, IMAGE_ICON, (LPARAM)cur_play);
    }
    if (g_app.btn_stop) {
        SendMessageW(g_app.btn_stop, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_app.icon_stop);
    }
    if (g_app.btn_next) {
        SendMessageW(g_app.btn_next, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_app.icon_next);
    }
    if (g_app.btn_toggle_playlist) {
        SendMessageW(g_app.btn_toggle_playlist, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_app.icon_playlist);
    }
    if (g_app.btn_close_playlist) {
        SendMessageW(g_app.btn_close_playlist, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_app.icon_close);
    }
}

static void init_tooltips(HWND hwnd) {
    HWND hwnd_tip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL,
                                    WS_POPUP | TTS_ALWAYSTIP,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    hwnd, NULL, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE), NULL);
    if (!hwnd_tip) return;

    TOOLINFOW ti = {0};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = hwnd;

    ti.uId = (UINT_PTR)g_app.btn_open;
    ti.lpszText = (LPWSTR)L"Open Audio (Ctrl+O)";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = (UINT_PTR)g_app.btn_prev;
    ti.lpszText = (LPWSTR)L"Previous File (Ctrl+Left)";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = (UINT_PTR)g_app.btn_play;
    ti.lpszText = (LPWSTR)L"Play / Pause (Space)";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = (UINT_PTR)g_app.btn_stop;
    ti.lpszText = (LPWSTR)L"Stop";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = (UINT_PTR)g_app.btn_next;
    ti.lpszText = (LPWSTR)L"Next File (Ctrl+Right)";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = (UINT_PTR)g_app.btn_toggle_playlist;
    ti.lpszText = (LPWSTR)L"Toggle Playlist (Ctrl+P)";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = (UINT_PTR)g_app.chk_autonext;
    ti.lpszText = (LPWSTR)L"Play next file in folder automatically";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);

    ti.uId = (UINT_PTR)g_app.btn_close_playlist;
    ti.lpszText = (LPWSTR)L"Close Playlist";
    SendMessageW(hwnd_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

static void create_controls(HWND hwnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

    g_app.btn_open = CreateWindowExW(0, L"BUTTON", L"",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_ICON,
                                     0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_OPEN, hInst, NULL);
    g_app.btn_prev = CreateWindowExW(0, L"BUTTON", L"",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_ICON | WS_DISABLED,
                                     0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_PREV, hInst, NULL);
    g_app.btn_play = CreateWindowExW(0, L"BUTTON", L"",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_ICON | WS_DISABLED,
                                     0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_PLAYPAUSE, hInst, NULL);
    g_app.btn_stop = CreateWindowExW(0, L"BUTTON", L"",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_ICON | WS_DISABLED,
                                     0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_STOP, hInst, NULL);
    g_app.btn_next = CreateWindowExW(0, L"BUTTON", L"",
                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_ICON | WS_DISABLED,
                                     0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_NEXT, hInst, NULL);

    g_app.chk_autonext = CreateWindowExW(0, L"BUTTON", L"Auto next",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         0, 0, 0, 0, hwnd, (HMENU)IDC_CHK_AUTONEXT, hInst, NULL);
    SendMessageW(g_app.chk_autonext, BM_SETCHECK,
                 g_app.auto_next ? BST_CHECKED : BST_UNCHECKED, 0);

    g_app.btn_toggle_playlist = CreateWindowExW(0, L"BUTTON", L"",
                                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_ICON,
                                                0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_TOGGLE_PL, hInst, NULL);

    int sw = g_app.show_playlist ? SW_SHOW : SW_HIDE;
    g_app.lbl_playlist_title = CreateWindowExW(0, L"STATIC", L"Playlist",
                                               WS_CHILD | sw | SS_LEFT | SS_CENTERIMAGE,
                                               0, 0, 0, 0, hwnd, (HMENU)IDC_LBL_PL_TITLE, hInst, NULL);

    g_app.btn_close_playlist = CreateWindowExW(0, L"BUTTON", L"",
                                               WS_CHILD | sw | BS_PUSHBUTTON | BS_ICON,
                                               0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_PL_CLOSE, hInst, NULL);

    recreate_button_icons();
    init_tooltips(hwnd);

    g_app.list_playlist = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                          WS_CHILD | sw | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                          0, 0, 0, 0, hwnd, (HMENU)IDC_LIST_PLAYLIST, hInst, NULL);

    g_app.slider_seek = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | WS_DISABLED,
                                        0, 0, 0, 0, hwnd, (HMENU)IDC_SLIDER_SEEK, hInst, NULL);
    SendMessageW(g_app.slider_seek, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(g_app.slider_seek, TBM_SETRANGEMAX, TRUE, 10000);
    trackbar_attach_click_seek(g_app.slider_seek, &g_app.seek_drag);

    g_app.label_time = CreateWindowExW(0, L"STATIC", L"00:00 / 00:00 / 00:00",
                                       WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                                       0, 0, 0, 0, hwnd, (HMENU)IDC_LABEL_TIME, hInst, NULL);
    g_app.chk_remember = CreateWindowExW(0, L"BUTTON", L"Remember pos",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         0, 0, 0, 0, hwnd, (HMENU)IDC_CHK_REMEMBER, hInst, NULL);
    SendMessageW(g_app.chk_remember, BM_SETCHECK,
                 g_app.remember_pos ? BST_CHECKED : BST_UNCHECKED, 0);

    g_app.label_vol = CreateWindowExW(0, L"STATIC", L"Vol:",
                                      WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                                      0, 0, 0, 0, hwnd, (HMENU)IDC_LABEL_VOL, hInst, NULL);
    g_app.slider_vol = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                       WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                       0, 0, 0, 0, hwnd, (HMENU)IDC_SLIDER_VOL, hInst, NULL);
    SendMessageW(g_app.slider_vol, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(g_app.slider_vol, TBM_SETRANGEMAX, FALSE, 100);
    SendMessageW(g_app.slider_vol, TBM_SETPOS, TRUE, g_app.player.volume_percent);
    trackbar_attach_click_seek(g_app.slider_vol, &g_app.vol_drag);

    static const wchar_t *slot_labels[SUB_SLOT_COUNT] = {
        L"Primary:", L"Secondary:", L"Tertiary:"
    };
    static const UINT_PTR lbl_ids[SUB_SLOT_COUNT] = {
        IDC_LABEL_SUB_PRI, IDC_LABEL_SUB_SEC, IDC_LABEL_SUB_TER
    };
    static const UINT_PTR cmb_ids[SUB_SLOT_COUNT] = {
        IDC_COMBO_SUB_PRI, IDC_COMBO_SUB_SEC, IDC_COMBO_SUB_TER
    };
    static const UINT_PTR btn_ids[SUB_SLOT_COUNT] = {
        IDC_BTN_LOAD_PRI, IDC_BTN_LOAD_SEC, IDC_BTN_LOAD_TER
    };

    for (int i = 0; i < SUB_SLOT_COUNT; i++) {
        g_app.slots[i].label = CreateWindowExW(0, L"STATIC", slot_labels[i],
                                               WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                                               0, 0, 0, 0, hwnd, (HMENU)lbl_ids[i], hInst, NULL);
        g_app.slots[i].combo = CreateWindowExW(0, L"COMBOBOX", L"",
                                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                               0, 0, 0, 0, hwnd, (HMENU)cmb_ids[i], hInst, NULL);
        g_app.slots[i].btn_load = CreateWindowExW(0, L"BUTTON", L"...",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  0, 0, 0, 0, hwnd, (HMENU)btn_ids[i], hInst, NULL);
        populate_slot_combo(i, 0);
    }

    recreate_ui_font();
}

static void adjust_volume(int delta) {
    int v = g_app.player.volume_percent + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (v != g_app.player.volume_percent) {
        player_set_volume(&g_app.player, v);
        SendMessageW(g_app.slider_vol, TBM_SETPOS, TRUE, v);
        invalidate_stage();
    }
}

static void seek_relative(long long delta_ms) {
    if (!g_app.player.is_open) return;
    g_app.user_stopped = false;
    g_app.track_ended_handled = false;
    long long pos = player_get_position(&g_app.player) + delta_ms;
    player_seek(&g_app.player, pos);
    save_current_position(false);
    update_time_and_subtitle();
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_app.hwnd = hwnd;
        player_set_callback(&g_app.player, hwnd);
        g_app.dpi = GetDpiForWindow(hwnd);
        if (g_app.dpi == 0) g_app.dpi = 96;
        create_main_menu(hwnd);
        create_controls(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, IDT_PLAYBACK_TIMER, 40, NULL);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        UINT dpi = g_app.dpi ? g_app.dpi : 96;
        mmi->ptMinTrackSize.x = MulDiv(760, (int)dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(480, (int)dpi, 96);
        return 0;
    }

    case WM_SYSCOLORCHANGE:
        recreate_button_icons();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_DPICHANGED: {
        g_app.dpi = HIWORD(wParam);
        recreate_ui_font();
        recreate_button_icons();
        RECT *suggested = (RECT *)lParam;
        SetWindowPos(hwnd, NULL,
                     suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout_controls();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_SIZE:
        layout_controls();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int stage_h = get_stage_height(hwnd);
        int pl_w = get_playlist_width();
        int main_w = w - pl_w;
        if (main_w < scale_dpi(480)) main_w = scale_dpi(480);

        paint_stage(hdc, main_w, stage_h);

        RECT panel_rc = {0, stage_h, main_w, h};
        FillRect(hdc, &panel_rc, GetSysColorBrush(COLOR_BTNFACE));

        HPEN top_pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
        HPEN old_pen = (HPEN)SelectObject(hdc, top_pen);
        MoveToEx(hdc, 0, stage_h, NULL);
        LineTo(hdc, main_w, stage_h);

        if (g_app.show_playlist) {
            RECT pl_rc = {main_w, 0, w, h};
            FillRect(hdc, &pl_rc, GetSysColorBrush(COLOR_BTNFACE));

            MoveToEx(hdc, main_w, 0, NULL);
            LineTo(hdc, main_w, h);
        }

        SelectObject(hdc, old_pen);
        DeleteObject(top_pen);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDT_PLAYBACK_TIMER) {
            update_time_and_subtitle();
        }
        return 0;

    case MM_MCINOTIFY:
        if (wParam == MCI_NOTIFY_SUCCESSFUL) {
            on_track_ended();
        }
        return 0;

    case WM_HSCROLL: {
        HWND src = (HWND)lParam;
        if (src == g_app.slider_seek && g_app.player.is_open) {
            WORD code = LOWORD(wParam);
            LRESULT pos_val = SendMessageW(g_app.slider_seek, TBM_GETPOS, 0, 0);
            long long target_ms = (g_app.player.duration_ms * (long long)pos_val) / 10000LL;
            if (code == TB_THUMBTRACK) {
                g_app.is_seeking = true;
                g_app.preview_seek_ms = target_ms;
                update_time_and_subtitle();
            } else if (code == TB_THUMBPOSITION || code == TB_ENDTRACK ||
                       code == TB_PAGEUP || code == TB_PAGEDOWN ||
                       code == TB_LINEUP || code == TB_LINEDOWN ||
                       code == TB_TOP || code == TB_BOTTOM) {
                g_app.is_seeking = false;
                player_seek(&g_app.player, target_ms);
                save_current_position(false);
                update_time_and_subtitle();
                if (code == TB_ENDTRACK) {
                    SetFocus(hwnd);
                }
            }
        } else if (src == g_app.slider_vol) {
            int vol = (int)SendMessageW(g_app.slider_vol, TBM_GETPOS, 0, 0);
            player_set_volume(&g_app.player, vol);
            invalidate_stage();
            if (LOWORD(wParam) == TB_ENDTRACK) {
                SetFocus(hwnd);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int y = (int)(short)HIWORD(lParam);
        int stage_h = get_stage_height(hwnd);

        if (y >= 0 && y < stage_h) {
            SetFocus(hwnd);
            if (g_app.player.is_open) {
                player_toggle_play_pause(&g_app.player);
                if (g_app.player.state == PLAYER_PAUSED) {
                    save_current_position(false);
                }
                update_play_button_text();
                invalidate_stage();
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (any_combo_dropped()) {
            break;
        }
        g_app.wheel_accum += GET_WHEEL_DELTA_WPARAM(wParam);
        while (g_app.wheel_accum >= WHEEL_DELTA) {
            adjust_volume(5);
            g_app.wheel_accum -= WHEEL_DELTA;
        }
        while (g_app.wheel_accum <= -WHEEL_DELTA) {
            adjust_volume(-5);
            g_app.wheel_accum += WHEEL_DELTA;
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD notify = HIWORD(wParam);

        if (notify == LBN_DBLCLK && id == IDC_LIST_PLAYLIST) {
            int sel = (int)SendMessageW(g_app.list_playlist, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && (size_t)sel < g_app.playlist.count) {
                open_audio_file(g_app.playlist.paths[sel]);
            }
            return 0;
        }

        if (notify == CBN_SELCHANGE) {
            if (id == IDC_COMBO_SUB_PRI) {
                on_slot_combo_changed(SUB_PRIMARY);
                SetFocus(hwnd);
                return 0;
            } else if (id == IDC_COMBO_SUB_SEC) {
                on_slot_combo_changed(SUB_SECONDARY);
                SetFocus(hwnd);
                return 0;
            } else if (id == IDC_COMBO_SUB_TER) {
                on_slot_combo_changed(SUB_TERTIARY);
                SetFocus(hwnd);
                return 0;
            }
        }

        if (id >= IDM_FILE_RECENT_BASE && id < IDM_FILE_RECENT_BASE + PREFS_MAX_HISTORY) {
            int idx = id - IDM_FILE_RECENT_BASE;
            wchar_t recent_path[MAX_PATH] = {0};
            if (prefs_get_history_item(idx, recent_path, MAX_PATH)) {
                if (GetFileAttributesW(recent_path) == INVALID_FILE_ATTRIBUTES) {
                    MessageBoxW(hwnd, L"File does not exist or has been moved.",
                                L"File Not Found", MB_OK | MB_ICONWARNING);
                } else {
                    open_audio_file(recent_path);
                }
            }
            SetFocus(hwnd);
            return 0;
        }

        if (id == IDM_FILE_CLEAR_RECENT) {
            prefs_clear_history();
            create_main_menu(hwnd);
            SetFocus(hwnd);
            return 0;
        }

        switch (id) {
        case IDC_BTN_OPEN:
        case IDM_FILE_OPEN:
            show_open_audio_dialog();
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_TOGGLE_PL:
        case IDM_VIEW_PLAYLIST:
        case IDC_BTN_PL_CLOSE:
            toggle_playlist_visibility();
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_PREV:
        case IDM_PLAY_PREVFILE:
            play_prev_audio_file();
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_NEXT:
        case IDM_PLAY_NEXTFILE:
            play_next_audio_file();
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_LOAD_PRI:
        case IDM_FILE_LOADPRI:
            show_open_srt_dialog_for_slot(SUB_PRIMARY);
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_LOAD_SEC:
        case IDM_FILE_LOADSEC:
            show_open_srt_dialog_for_slot(SUB_SECONDARY);
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_LOAD_TER:
        case IDM_FILE_LOADTER:
            show_open_srt_dialog_for_slot(SUB_TERTIARY);
            SetFocus(hwnd);
            return 0;

        case IDC_CHK_REMEMBER:
            g_app.remember_pos =
                (SendMessageW(g_app.chk_remember, BM_GETCHECK, 0, 0) == BST_CHECKED);
            prefs_set_remember_position(g_app.remember_pos);
            if (g_app.remember_pos) {
                save_current_position(false);
            }
            SetFocus(hwnd);
            return 0;

        case IDC_CHK_AUTONEXT:
            g_app.auto_next =
                (SendMessageW(g_app.chk_autonext, BM_GETCHECK, 0, 0) == BST_CHECKED);
            prefs_set_auto_next(g_app.auto_next);
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_PLAYPAUSE:
        case IDM_PLAY_TOGGLE:
            if (g_app.player.is_open) {
                g_app.user_stopped = false;
                g_app.track_ended_handled = false;
                player_toggle_play_pause(&g_app.player);
                if (g_app.player.state == PLAYER_PAUSED) {
                    save_current_position(false);
                }
                update_play_button_text();
                invalidate_stage();
            }
            SetFocus(hwnd);
            return 0;

        case IDC_BTN_STOP:
        case IDM_PLAY_STOP:
            g_app.user_stopped = true;
            if (g_app.player.is_open) {
                player_stop(&g_app.player);
                if (g_app.remember_pos) {
                    prefs_save_file_position(g_app.player.current_path, 0);
                }
                update_play_button_text();
                update_time_and_subtitle();
                invalidate_stage();
            }
            SetFocus(hwnd);
            return 0;

        case IDM_PLAY_BACK5:
            seek_relative(-5000);
            return 0;

        case IDM_PLAY_FWD5:
            seek_relative(5000);
            return 0;

        case IDM_FILE_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        switch (wParam) {
        case VK_SPACE:
            SendMessageW(hwnd, WM_COMMAND, IDM_PLAY_TOGGLE, 0);
            return 0;
        case VK_LEFT:
            seek_relative(-5000);
            return 0;
        case VK_RIGHT:
            seek_relative(5000);
            return 0;
        case VK_UP:
            adjust_volume(5);
            return 0;
        case VK_DOWN:
            adjust_volume(-5);
            return 0;
        }
        break;

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        wchar_t path[MAX_PATH] = {0};
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH) > 0) {
            const wchar_t *ext = wcsrchr(path, L'.');
            if (ext && _wcsicmp(ext, L".srt") == 0) {
                POINT pt = {0, 0};
                DragQueryPoint(hDrop, &pt);
                int stage_h = get_stage_height(hwnd);
                int target_slot = SUB_PRIMARY;
                if (stage_h > 0) {
                    if (pt.y < stage_h / 3) {
                        target_slot = SUB_TERTIARY;
                    } else if (pt.y >= (stage_h * 2) / 3 && pt.y < stage_h) {
                        target_slot = SUB_SECONDARY;
                    }
                }
                if (load_slot_subtitle(target_slot, path, true)) {
                    populate_slot_combo(target_slot, (int)g_app.tracks.count);
                }
            } else {
                open_audio_file(path);
            }
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, IDT_PLAYBACK_TIMER);
        save_current_position(true);
        player_close(&g_app.player);
        if (g_app.icon_open)     { DestroyIcon(g_app.icon_open);     g_app.icon_open = NULL; }
        if (g_app.icon_prev)     { DestroyIcon(g_app.icon_prev);     g_app.icon_prev = NULL; }
        if (g_app.icon_play)     { DestroyIcon(g_app.icon_play);     g_app.icon_play = NULL; }
        if (g_app.icon_pause)    { DestroyIcon(g_app.icon_pause);    g_app.icon_pause = NULL; }
        if (g_app.icon_stop)     { DestroyIcon(g_app.icon_stop);     g_app.icon_stop = NULL; }
        if (g_app.icon_next)     { DestroyIcon(g_app.icon_next);     g_app.icon_next = NULL; }
        if (g_app.icon_playlist) { DestroyIcon(g_app.icon_playlist); g_app.icon_playlist = NULL; }
        if (g_app.icon_close)    { DestroyIcon(g_app.icon_close);    g_app.icon_close = NULL; }
        playlist_free(&g_app.playlist);
        for (int i = 0; i < SUB_SLOT_COUNT; i++) {
            srt_free_subtitle(&g_app.slots[i].sub);
        }
        srt_free_track_list(&g_app.tracks);
        if (g_app.ui_font) {
            DeleteObject(g_app.ui_font);
            g_app.ui_font = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    ZeroMemory(&g_app, sizeof(g_app));
    playlist_init(&g_app.playlist);
    player_init(&g_app.player);
    prefs_init();
    g_app.remember_pos = prefs_get_remember_position();
    g_app.auto_next = prefs_get_auto_next();
    g_app.show_playlist = prefs_get_show_playlist();

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    const wchar_t *class_name = L"Mp3SubtitlePlayerClass";
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    if (!wc.hIcon) {
        wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!wc.hIconSm) {
        wc.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
    }
    wc.hbrBackground = NULL;
    wc.lpszClassName = class_name;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* WS_OVERLAPPEDWINDOW ensures restore yields a standard resizable window */
    HWND hwnd = CreateWindowExW(
        0,
        class_name,
        L"Audio Subtitle Player",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 680,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"Failed to create main window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Maximized by default */
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    /* Open file passed on command line if present */
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc >= 2 && argv[1][0] != L'\0') {
            open_audio_file(argv[1]);
        }
        LocalFree(argv);
    }

    ACCEL accels[] = {
        {FCONTROL | FVIRTKEY, 'O', IDM_FILE_OPEN},
        {FCONTROL | FVIRTKEY, 'P', IDM_VIEW_PLAYLIST},
        {FCONTROL | FVIRTKEY, VK_LEFT, IDM_PLAY_PREVFILE},
        {FCONTROL | FVIRTKEY, VK_RIGHT, IDM_PLAY_NEXTFILE},
    };
    HACCEL hAccel = CreateAcceleratorTableW(accels, sizeof(accels) / sizeof(accels[0]));

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (hAccel && TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            continue;
        }
        /* Handle player shortcuts globally unless a subtitle dropdown is open */
        if (msg.message == WM_KEYDOWN && !any_combo_dropped()) {
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                if (msg.wParam == 'P' || msg.wParam == 'p') {
                    toggle_playlist_visibility();
                    continue;
                } else if (msg.wParam == VK_LEFT) {
                    play_prev_audio_file();
                    continue;
                } else if (msg.wParam == VK_RIGHT) {
                    play_next_audio_file();
                    continue;
                }
            }
            if (msg.wParam == VK_RETURN && GetFocus() == g_app.list_playlist) {
                int sel = (int)SendMessageW(g_app.list_playlist, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && (size_t)sel < g_app.playlist.count) {
                    open_audio_file(g_app.playlist.paths[sel]);
                }
                continue;
            }
            if (msg.wParam == VK_SPACE) {
                SendMessageW(hwnd, WM_COMMAND, IDM_PLAY_TOGGLE, 0);
                continue;
            } else if (msg.wParam == VK_LEFT) {
                seek_relative(-5000);
                continue;
            } else if (msg.wParam == VK_RIGHT) {
                seek_relative(5000);
                continue;
            } else if (msg.wParam == VK_UP) {
                adjust_volume(5);
                continue;
            } else if (msg.wParam == VK_DOWN) {
                adjust_volume(-5);
                continue;
            }
        }
        /* Route mouse wheel globally to volume control unless dropdown is open */
        if (msg.message == WM_MOUSEWHEEL && !any_combo_dropped()) {
            SendMessageW(hwnd, WM_MOUSEWHEEL, msg.wParam, msg.lParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hAccel) {
        DestroyAcceleratorTable(hAccel);
    }
    return (int)msg.wParam;
}
