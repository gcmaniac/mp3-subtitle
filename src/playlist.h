#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct Playlist {
    wchar_t folder[MAX_PATH];
    wchar_t **paths;       /* full paths to audio files */
    wchar_t **names;       /* filename only */
    size_t count;
    size_t capacity;
    int current_index;     /* -1 if none */
} Playlist;

void playlist_init(Playlist *pl);
void playlist_free(Playlist *pl);
bool playlist_scan_folder(Playlist *pl, const wchar_t *folder_or_filepath);
int playlist_find_index(const Playlist *pl, const wchar_t *path);
const wchar_t *playlist_get_path(const Playlist *pl, size_t index);
const wchar_t *playlist_get_name(const Playlist *pl, size_t index);

#endif /* PLAYLIST_H */
