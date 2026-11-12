#ifndef PREFS_H
#define PREFS_H

#include <stdbool.h>
#include <wchar.h>

void prefs_init(void);
void prefs_init_with_path(const wchar_t *ini_path);

bool prefs_get_remember_position(void);
void prefs_set_remember_position(bool enabled);

long long prefs_load_file_position(const wchar_t *mp3_path);
void prefs_save_file_position(const wchar_t *mp3_path, long long pos_ms);

#endif /* PREFS_H */

