#ifndef SRT_H
#define SRT_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct SrtCue {
    long long start_ms;
    long long end_ms;
    wchar_t *text;
} SrtCue;

typedef struct SrtSubtitle {
    SrtCue *cues;
    size_t count;
    size_t capacity;
} SrtSubtitle;

typedef struct SrtTrackList {
    wchar_t **paths; /* Full paths, sorted alphabetically by filename */
    wchar_t **names; /* Filenames only, sorted alphabetically */
    size_t count;
} SrtTrackList;

/* Discover matching .srt files for an MP3 path (<name>.srt or <name>.<lang>.srt),
 * sorted alphabetically by filename. Returns true on success (count may be 0). */
bool srt_discover_tracks(const wchar_t *mp3_path, SrtTrackList *out_list);
void srt_free_track_list(SrtTrackList *list);

/* Load and parse an .srt file (UTF-8, UTF-16 LE/BE, or ANSI). */
bool srt_load_file(const wchar_t *srt_path, SrtSubtitle *out_sub);
void srt_free_subtitle(SrtSubtitle *sub);

/* Retrieve active subtitle text at pos_ms. Returns true if non-empty text found. */
bool srt_get_active_text(const SrtSubtitle *sub, long long pos_ms, wchar_t *buf, size_t buf_chars);

#endif /* SRT_H */

