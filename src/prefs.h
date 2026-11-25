#ifndef PREFS_H
#define PREFS_H

#include <stdbool.h>
#include <wchar.h>

void prefs_init(void);
void prefs_init_with_path(const wchar_t *ini_path);

bool prefs_get_remember_position(void);
void prefs_set_remember_position(bool enabled);

bool prefs_get_auto_next(void);
void prefs_set_auto_next(bool enabled);

bool prefs_get_show_playlist(void);
void prefs_set_show_playlist(bool show);

long long prefs_load_file_position(const wchar_t *mp3_path);
void prefs_save_file_position(const wchar_t *mp3_path, long long pos_ms);

#define PREFS_MAX_HISTORY 10

int prefs_get_history_count(void);
bool prefs_get_history_item(int index, wchar_t *path_buf, size_t path_chars);
void prefs_add_history(const wchar_t *path);
void prefs_clear_history(void);

#endif /* PREFS_H */

