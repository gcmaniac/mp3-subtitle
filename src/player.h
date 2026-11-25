#ifndef PLAYER_H
#define PLAYER_H

#include <windows.h>
#include <stdbool.h>

typedef enum PlayerState {
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED
} PlayerState;

typedef struct Mp3Player {
    bool is_open;
    PlayerState state;
    long long duration_ms;
    int volume_percent; /* 0..100 */
    HWND hwnd_callback;
    wchar_t current_path[MAX_PATH];
    wchar_t last_error[512];
} Mp3Player;

typedef Mp3Player AudioPlayer;

void player_init(Mp3Player *p);
void player_set_callback(Mp3Player *p, HWND hwnd);
bool player_open(Mp3Player *p, const wchar_t *path);
void player_close(Mp3Player *p);

bool player_play(Mp3Player *p);
bool player_play_from(Mp3Player *p, long long pos_ms);
bool player_pause(Mp3Player *p);
bool player_toggle_play_pause(Mp3Player *p);
bool player_stop(Mp3Player *p);
bool player_seek(Mp3Player *p, long long pos_ms);
bool player_set_volume(Mp3Player *p, int vol_percent);

long long player_get_position(Mp3Player *p);
PlayerState player_refresh_state(Mp3Player *p);

bool player_is_supported_audio_extension(const wchar_t *filename);
bool player_find_next_audio_file(const wchar_t *current_path, wchar_t *next_path_out, size_t next_path_chars);
bool player_find_prev_audio_file(const wchar_t *current_path, wchar_t *prev_path_out, size_t prev_path_chars);

#endif /* PLAYER_H */

