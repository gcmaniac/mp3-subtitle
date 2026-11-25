#include "playlist.h"
#include "player.h"

#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_playlist_names(const void *a, const void *b) {
    const wchar_t *const *sa = (const wchar_t *const *)a;
    const wchar_t *const *sb = (const wchar_t *const *)b;
    return StrCmpLogicalW(*sa, *sb);
}

static wchar_t *pl_clone_str(const wchar_t *s) {
    if (!s) return NULL;
    size_t len = wcslen(s) + 1;
    wchar_t *p = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (p) {
        memcpy(p, s, len * sizeof(wchar_t));
    }
    return p;
}

void playlist_init(Playlist *pl) {
    if (!pl) return;
    memset(pl, 0, sizeof(*pl));
    pl->current_index = -1;
}

void playlist_free(Playlist *pl) {
    if (!pl) return;
    if (pl->paths) {
        for (size_t i = 0; i < pl->count; i++) {
            free(pl->paths[i]);
        }
        free(pl->paths);
        pl->paths = NULL;
    }
    if (pl->names) {
        for (size_t i = 0; i < pl->count; i++) {
            free(pl->names[i]);
        }
        free(pl->names);
        pl->names = NULL;
    }
    pl->count = 0;
    pl->capacity = 0;
    pl->folder[0] = L'\0';
    pl->current_index = -1;
}

int playlist_find_index(const Playlist *pl, const wchar_t *path) {
    if (!pl || !path || !path[0] || pl->count == 0) {
        return -1;
    }

    /* First try matching full path */
    for (size_t i = 0; i < pl->count; i++) {
        if (_wcsicmp(pl->paths[i], path) == 0) {
            return (int)i;
        }
    }

    /* Fallback: match filename part */
    const wchar_t *s1 = wcsrchr(path, L'\\');
    const wchar_t *s2 = wcsrchr(path, L'/');
    const wchar_t *sep = (s1 && s2) ? (s1 > s2 ? s1 : s2) : (s1 ? s1 : s2);
    const wchar_t *fname = sep ? (sep + 1) : path;

    for (size_t i = 0; i < pl->count; i++) {
        if (_wcsicmp(pl->names[i], fname) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool playlist_scan_folder(Playlist *pl, const wchar_t *folder_or_filepath) {
    if (!pl || !folder_or_filepath || !folder_or_filepath[0]) {
        return false;
    }

    /* Extract folder and current filename */
    wchar_t dir[MAX_PATH];
    const wchar_t *slash1 = wcsrchr(folder_or_filepath, L'\\');
    const wchar_t *slash2 = wcsrchr(folder_or_filepath, L'/');
    const wchar_t *slash = (slash1 && slash2) ? (slash1 > slash2 ? slash1 : slash2) : (slash1 ? slash1 : slash2);

    const wchar_t *cur_filename = NULL;
    if (slash) {
        size_t dir_len = (size_t)(slash - folder_or_filepath);
        if (dir_len == 2 && folder_or_filepath[1] == L':') {
            dir_len = 3; /* Keep root slash e.g. "C:\" */
        }
        if (dir_len >= MAX_PATH) return false;
        wcsncpy(dir, folder_or_filepath, dir_len);
        dir[dir_len] = L'\0';
        cur_filename = slash + 1;
    } else {
        /* Check if folder_or_filepath is a directory itself */
        DWORD attr = GetFileAttributesW(folder_or_filepath);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            wcsncpy(dir, folder_or_filepath, MAX_PATH - 1);
            dir[MAX_PATH - 1] = L'\0';
            cur_filename = NULL;
        } else {
            wcscpy(dir, L".");
            cur_filename = folder_or_filepath;
        }
    }

    /* If folder is the same and we already have tracks, just update current index */
    if (pl->count > 0 && _wcsicmp(pl->folder, dir) == 0) {
        if (cur_filename && cur_filename[0]) {
            pl->current_index = playlist_find_index(pl, folder_or_filepath);
        }
        return true;
    }

    /* Different folder or empty playlist: re-scan directory */
    wchar_t search_pattern[MAX_PATH];
    size_t dlen = wcslen(dir);
    if (dlen > 0 && (dir[dlen - 1] == L'\\' || dir[dlen - 1] == L'/')) {
        _snwprintf(search_pattern, MAX_PATH, L"%ls*.*", dir);
    } else {
        _snwprintf(search_pattern, MAX_PATH, L"%ls\\*.*", dir);
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }

    size_t capacity = 32;
    size_t count = 0;
    wchar_t **names = (wchar_t **)malloc(capacity * sizeof(wchar_t *));
    if (!names) {
        FindClose(hFind);
        return false;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!player_is_supported_audio_extension(fd.cFileName)) {
            continue;
        }

        if (count >= capacity) {
            size_t new_cap = capacity * 2;
            wchar_t **new_names = (wchar_t **)realloc(names, new_cap * sizeof(wchar_t *));
            if (!new_names) {
                break;
            }
            names = new_names;
            capacity = new_cap;
        }

        names[count] = pl_clone_str(fd.cFileName);
        if (names[count]) {
            count++;
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    if (count == 0) {
        free(names);
        return false;
    }

    /* Natural sort by filename */
    qsort(names, count, sizeof(wchar_t *), compare_playlist_names);

    /* Allocate full paths */
    wchar_t **paths = (wchar_t **)malloc(count * sizeof(wchar_t *));
    if (!paths) {
        for (size_t i = 0; i < count; i++) free(names[i]);
        free(names);
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        wchar_t full[MAX_PATH];
        if (dlen > 0 && (dir[dlen - 1] == L'\\' || dir[dlen - 1] == L'/')) {
            _snwprintf(full, MAX_PATH, L"%ls%ls", dir, names[i]);
        } else {
            _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, names[i]);
        }
        paths[i] = pl_clone_str(full);
    }

    /* Free previous playlist */
    playlist_free(pl);

    pl->names = names;
    pl->paths = paths;
    pl->count = count;
    pl->capacity = capacity;
    wcsncpy(pl->folder, dir, MAX_PATH - 1);
    pl->folder[MAX_PATH - 1] = L'\0';

    if (cur_filename && cur_filename[0]) {
        pl->current_index = playlist_find_index(pl, folder_or_filepath);
    } else {
        pl->current_index = -1;
    }

    return true;
}

const wchar_t *playlist_get_path(const Playlist *pl, size_t index) {
    if (!pl || index >= pl->count || !pl->paths) return NULL;
    return pl->paths[index];
}

const wchar_t *playlist_get_name(const Playlist *pl, size_t index) {
    if (!pl || index >= pl->count || !pl->names) return NULL;
    return pl->names[index];
}
