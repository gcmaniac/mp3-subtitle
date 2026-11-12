#include "player.h"

#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MCI_ALIAS L"mp3sub_player"

static bool mci_exec(Mp3Player *p, const wchar_t *cmd, wchar_t *ret_buf, DWORD ret_chars) {
    if (ret_buf && ret_chars > 0) {
        ret_buf[0] = L'\0';
    }
    MCIERROR err = mciSendStringW(cmd, ret_buf, ret_chars, NULL);
    if (err != 0) {
        if (p) {
            if (!mciGetErrorStringW(err, p->last_error,
                                    (UINT)(sizeof(p->last_error) / sizeof(p->last_error[0])))) {
                _snwprintf(p->last_error, 512, L"MCI error %lu", (unsigned long)err);
            }
        }
        return false;
    }
    return true;
}

void player_init(Mp3Player *p) {
    if (!p) {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->volume_percent = 80;
    p->state = PLAYER_STOPPED;
}

void player_close(Mp3Player *p) {
    if (!p) {
        return;
    }
    if (p->is_open) {
        mci_exec(NULL, L"close " MCI_ALIAS, NULL, 0);
        p->is_open = false;
    }
    p->state = PLAYER_STOPPED;
    p->duration_ms = 0;
    p->current_path[0] = L'\0';
}

bool player_open(Mp3Player *p, const wchar_t *path) {
    if (!p || !path || !path[0]) {
        return false;
    }
    player_close(p);

    wchar_t cmd[MAX_PATH + 128];
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
               L"open \"%ls\" type mpegvideo alias " MCI_ALIAS, path);
    if (!mci_exec(p, cmd, NULL, 0)) {
        return false;
    }

    p->is_open = true;
    wcsncpy(p->current_path, path, MAX_PATH - 1);
    p->current_path[MAX_PATH - 1] = L'\0';

    mci_exec(p, L"set " MCI_ALIAS L" time format milliseconds", NULL, 0);

    wchar_t len_buf[64] = {0};
    if (mci_exec(p, L"status " MCI_ALIAS L" length", len_buf, 64)) {
        p->duration_ms = _wcstoi64(len_buf, NULL, 10);
        if (p->duration_ms < 0) {
            p->duration_ms = 0;
        }
    }

    player_set_volume(p, p->volume_percent);
    p->state = PLAYER_STOPPED;
    return true;
}

bool player_play(Mp3Player *p) {
    if (!p || !p->is_open) {
        return false;
    }
    long long pos = player_get_position(p);
    if (p->duration_ms > 0 && pos >= p->duration_ms - 50) {
        if (!mci_exec(p, L"play " MCI_ALIAS L" from 0", NULL, 0)) {
            return false;
        }
    } else {
        if (!mci_exec(p, L"play " MCI_ALIAS, NULL, 0)) {
            return false;
        }
    }
    p->state = PLAYER_PLAYING;
    return true;
}

bool player_play_from(Mp3Player *p, long long pos_ms) {
    if (!p || !p->is_open) {
        return false;
    }
    if (pos_ms < 0) pos_ms = 0;
    if (p->duration_ms > 0 && pos_ms >= p->duration_ms - 50) {
        pos_ms = 0;
    }
    wchar_t cmd[128];
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
               L"play " MCI_ALIAS L" from %lld", pos_ms);
    if (!mci_exec(p, cmd, NULL, 0)) {
        return false;
    }
    p->state = PLAYER_PLAYING;
    return true;
}

bool player_pause(Mp3Player *p) {
    if (!p || !p->is_open) {
        return false;
    }
    if (!mci_exec(p, L"pause " MCI_ALIAS, NULL, 0)) {
        return false;
    }
    p->state = PLAYER_PAUSED;
    return true;
}

bool player_toggle_play_pause(Mp3Player *p) {
    if (!p || !p->is_open) {
        return false;
    }
    player_refresh_state(p);
    if (p->state == PLAYER_PLAYING) {
        return player_pause(p);
    }
    return player_play(p);
}

bool player_stop(Mp3Player *p) {
    if (!p || !p->is_open) {
        return false;
    }
    mci_exec(p, L"stop " MCI_ALIAS, NULL, 0);
    mci_exec(p, L"seek " MCI_ALIAS L" to start", NULL, 0);
    p->state = PLAYER_STOPPED;
    return true;
}

bool player_seek(Mp3Player *p, long long pos_ms) {
    if (!p || !p->is_open) {
        return false;
    }
    if (pos_ms < 0) {
        pos_ms = 0;
    }
    if (p->duration_ms > 0 && pos_ms > p->duration_ms) {
        pos_ms = p->duration_ms;
    }

    wchar_t cmd[128];
    if (p->state == PLAYER_PLAYING) {
        _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
                   L"play " MCI_ALIAS L" from %lld", pos_ms);
        return mci_exec(p, cmd, NULL, 0);
    } else {
        _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
                   L"seek " MCI_ALIAS L" to %lld", pos_ms);
        return mci_exec(p, cmd, NULL, 0);
    }
}

bool player_set_volume(Mp3Player *p, int vol_percent) {
    if (!p) {
        return false;
    }
    if (vol_percent < 0) vol_percent = 0;
    if (vol_percent > 100) vol_percent = 100;
    p->volume_percent = vol_percent;
    if (!p->is_open) {
        return true;
    }
    wchar_t cmd[128];
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
               L"setaudio " MCI_ALIAS L" volume to %d", vol_percent * 10);
    return mci_exec(p, cmd, NULL, 0);
}

long long player_get_position(Mp3Player *p) {
    if (!p || !p->is_open) {
        return 0;
    }
    wchar_t pos_buf[64] = {0};
    if (!mci_exec(NULL, L"status " MCI_ALIAS L" position", pos_buf, 64)) {
        return 0;
    }
    long long pos = _wcstoi64(pos_buf, NULL, 10);
    return pos < 0 ? 0 : pos;
}

PlayerState player_refresh_state(Mp3Player *p) {
    if (!p || !p->is_open) {
        if (p) p->state = PLAYER_STOPPED;
        return PLAYER_STOPPED;
    }
    wchar_t mode_buf[64] = {0};
    if (!mci_exec(NULL, L"status " MCI_ALIAS L" mode", mode_buf, 64)) {
        return p->state;
    }
    if (_wcsicmp(mode_buf, L"playing") == 0) {
        p->state = PLAYER_PLAYING;
    } else if (_wcsicmp(mode_buf, L"paused") == 0) {
        p->state = PLAYER_PAUSED;
    } else {
        p->state = PLAYER_STOPPED;
    }
    return p->state;
}

