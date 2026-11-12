#include "srt.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

typedef struct TrackPair {
    wchar_t *path;
    wchar_t *name;
} TrackPair;

static int cmp_track_pair(const void *a, const void *b) {
    const TrackPair *ta = (const TrackPair *)a;
    const TrackPair *tb = (const TrackPair *)b;
    int r = _wcsicmp(ta->name, tb->name);
    if (r != 0) {
        return r;
    }
    return wcscmp(ta->name, tb->name);
}

static wchar_t *dup_wcs(const wchar_t *s) {
    if (!s) {
        return NULL;
    }
    size_t len = wcslen(s);
    wchar_t *copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, (len + 1) * sizeof(wchar_t));
    return copy;
}

static bool matches_mp3_srt_name(const wchar_t *fname, const wchar_t *base, size_t base_len) {
    if (!fname || !base || base_len == 0) {
        return false;
    }
    size_t flen = wcslen(fname);
    if (flen < base_len + 4) {
        return false;
    }
    if (_wcsnicmp(fname, base, base_len) != 0) {
        return false;
    }
    /* Case 1: <base>.srt */
    if (flen == base_len + 4 && _wcsicmp(fname + base_len, L".srt") == 0) {
        return true;
    }
    /* Case 2: <base>.<lang>.srt */
    if (flen > base_len + 5 &&
        fname[base_len] == L'.' &&
        fname[base_len + 1] != L'.' &&
        _wcsicmp(fname + flen - 4, L".srt") == 0) {
        return true;
    }
    return false;
}

bool srt_discover_tracks(const wchar_t *mp3_path, SrtTrackList *out_list) {
    if (!mp3_path || !out_list) {
        return false;
    }
    memset(out_list, 0, sizeof(*out_list));

    const wchar_t *last_slash = wcsrchr(mp3_path, L'\\');
    const wchar_t *last_fslash = wcsrchr(mp3_path, L'/');
    if (last_fslash && (!last_slash || last_fslash > last_slash)) {
        last_slash = last_fslash;
    }

    const wchar_t *fname = last_slash ? (last_slash + 1) : mp3_path;
    size_t dir_len = last_slash ? (size_t)(last_slash - mp3_path + 1) : 0;

    const wchar_t *last_dot = wcsrchr(fname, L'.');
    size_t base_len = last_dot ? (size_t)(last_dot - fname) : wcslen(fname);
    if (base_len == 0) {
        return true;
    }

    wchar_t *base = (wchar_t *)malloc((base_len + 1) * sizeof(wchar_t));
    if (!base) {
        return false;
    }
    memcpy(base, fname, base_len * sizeof(wchar_t));
    base[base_len] = L'\0';

    /* Build search pattern: <dir><base>*.srt */
    size_t pat_cap = dir_len + base_len + 16;
    wchar_t *pattern = (wchar_t *)malloc(pat_cap * sizeof(wchar_t));
    if (!pattern) {
        free(base);
        return false;
    }
    if (dir_len > 0) {
        memcpy(pattern, mp3_path, dir_len * sizeof(wchar_t));
    }
    memcpy(pattern + dir_len, base, base_len * sizeof(wchar_t));
    wcscpy(pattern + dir_len + base_len, L"*.srt");

    TrackPair *items = NULL;
    size_t count = 0;
    size_t cap = 0;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (!matches_mp3_srt_name(fd.cFileName, base, base_len)) {
                continue;
            }
            if (count == cap) {
                size_t new_cap = cap ? cap * 2 : 8;
                TrackPair *tmp = (TrackPair *)realloc(items, new_cap * sizeof(TrackPair));
                if (!tmp) {
                    break;
                }
                items = tmp;
                cap = new_cap;
            }
            size_t fn_len = wcslen(fd.cFileName);
            wchar_t *full = (wchar_t *)malloc((dir_len + fn_len + 1) * sizeof(wchar_t));
            wchar_t *name_copy = dup_wcs(fd.cFileName);
            if (!full || !name_copy) {
                free(full);
                free(name_copy);
                break;
            }
            if (dir_len > 0) {
                memcpy(full, mp3_path, dir_len * sizeof(wchar_t));
            }
            memcpy(full + dir_len, fd.cFileName, (fn_len + 1) * sizeof(wchar_t));
            items[count].path = full;
            items[count].name = name_copy;
            count++;
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    free(pattern);
    free(base);

    if (count > 0) {
        qsort(items, count, sizeof(TrackPair), cmp_track_pair);
        out_list->paths = (wchar_t **)malloc(count * sizeof(wchar_t *));
        out_list->names = (wchar_t **)malloc(count * sizeof(wchar_t *));
        if (!out_list->paths || !out_list->names) {
            free(out_list->paths);
            free(out_list->names);
            out_list->paths = NULL;
            out_list->names = NULL;
            for (size_t i = 0; i < count; i++) {
                free(items[i].path);
                free(items[i].name);
            }
            free(items);
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            out_list->paths[i] = items[i].path;
            out_list->names[i] = items[i].name;
        }
        out_list->count = count;
    }

    free(items);
    return true;
}

void srt_free_track_list(SrtTrackList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->paths[i]);
        free(list->names[i]);
    }
    free(list->paths);
    free(list->names);
    memset(list, 0, sizeof(*list));
}

static wchar_t *decode_bytes_to_wide(const unsigned char *bytes, DWORD size) {
    if (size == 0) {
        return dup_wcs(L"");
    }

    /* UTF-16 LE BOM */
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        DWORD wchar_count = (size - 2) / 2;
        wchar_t *w = (wchar_t *)malloc(((size_t)wchar_count + 1) * sizeof(wchar_t));
        if (!w) {
            return NULL;
        }
        memcpy(w, bytes + 2, wchar_count * sizeof(wchar_t));
        w[wchar_count] = L'\0';
        return w;
    }

    /* UTF-16 BE BOM */
    if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        DWORD wchar_count = (size - 2) / 2;
        wchar_t *w = (wchar_t *)malloc(((size_t)wchar_count + 1) * sizeof(wchar_t));
        if (!w) {
            return NULL;
        }
        for (DWORD i = 0; i < wchar_count; i++) {
            unsigned short hi = bytes[2 + i * 2];
            unsigned short lo = bytes[2 + i * 2 + 1];
            w[i] = (wchar_t)((hi << 8) | lo);
        }
        w[wchar_count] = L'\0';
        return w;
    }

    const char *src = (const char *)bytes;
    int src_len = (int)size;
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;

    /* UTF-8 BOM */
    if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        src += 3;
        src_len -= 3;
        flags = 0;
    }

    if (src_len == 0) {
        return dup_wcs(L"");
    }

    int needed = MultiByteToWideChar(cp, flags, src, src_len, NULL, 0);
    if (needed == 0) {
        cp = CP_ACP;
        flags = 0;
        needed = MultiByteToWideChar(cp, flags, src, src_len, NULL, 0);
        if (needed == 0) {
            return NULL;
        }
    }

    wchar_t *w = (wchar_t *)malloc(((size_t)needed + 1) * sizeof(wchar_t));
    if (!w) {
        return NULL;
    }
    MultiByteToWideChar(cp, flags, src, src_len, w, needed);
    w[needed] = L'\0';
    return w;
}

static bool parse_timestamp_at(const wchar_t **pcursor, long long *out_ms) {
    const wchar_t *p = *pcursor;
    while (*p == L' ' || *p == L'\t') {
        p++;
    }
    if (!iswdigit(*p)) {
        return false;
    }

    /* Read up to 3 colon-separated fields: HH:MM:SS or MM:SS */
    long long parts[3] = {0, 0, 0};
    int part_count = 0;
    while (part_count < 3) {
        if (!iswdigit(*p)) {
            return false;
        }
        long long val = 0;
        while (iswdigit(*p)) {
            val = val * 10 + (*p - L'0');
            p++;
        }
        parts[part_count++] = val;
        if (*p == L':' && part_count < 3) {
            p++;
        } else {
            break;
        }
    }

    if (part_count < 2) {
        return false;
    }

    long long ms = 0;
    if (*p == L',' || *p == L'.') {
        p++;
        int digits = 0;
        long long frac = 0;
        while (iswdigit(*p)) {
            if (digits < 3) {
                frac = frac * 10 + (*p - L'0');
            }
            digits++;
            p++;
        }
        if (digits == 1) {
            frac *= 100;
        } else if (digits == 2) {
            frac *= 10;
        }
        ms = frac;
    }

    long long total_sec = 0;
    if (part_count == 3) {
        total_sec = parts[0] * 3600 + parts[1] * 60 + parts[2];
    } else {
        total_sec = parts[0] * 60 + parts[1];
    }

    *out_ms = total_sec * 1000 + ms;
    *pcursor = p;
    return true;
}

static bool parse_time_range_line(const wchar_t *line, long long *start_ms, long long *end_ms) {
    const wchar_t *arrow = wcsstr(line, L"-->");
    if (!arrow) {
        return false;
    }
    const wchar_t *p = line;
    long long s_ms = 0;
    if (!parse_timestamp_at(&p, &s_ms)) {
        return false;
    }
    while (*p == L' ' || *p == L'\t') {
        p++;
    }
    if (wcsncmp(p, L"-->", 3) != 0) {
        return false;
    }
    p += 3;
    long long e_ms = 0;
    if (!parse_timestamp_at(&p, &e_ms)) {
        return false;
    }
    if (e_ms < s_ms) {
        e_ms = s_ms;
    }
    *start_ms = s_ms;
    *end_ms = e_ms;
    return true;
}

static bool is_digits_only_trimmed(const wchar_t *line) {
    const wchar_t *p = line;
    while (*p == L' ' || *p == L'\t') {
        p++;
    }
    if (!iswdigit(*p)) {
        return false;
    }
    while (iswdigit(*p)) {
        p++;
    }
    while (*p == L' ' || *p == L'\t') {
        p++;
    }
    return *p == L'\0';
}

static bool is_blank_line(const wchar_t *line) {
    const wchar_t *p = line;
    while (*p) {
        if (*p != L' ' && *p != L'\t' && *p != L'\r') {
            return false;
        }
        p++;
    }
    return true;
}

static wchar_t *strip_subtitle_tags_and_trim(const wchar_t *raw) {
    size_t len = wcslen(raw);
    wchar_t *out = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!out) {
        return NULL;
    }
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == L'<') {
            const wchar_t *close = wcschr(raw + i + 1, L'>');
            const wchar_t *nl = wcschr(raw + i + 1, L'\n');
            if (close && (!nl || nl > close) && (size_t)(close - (raw + i)) <= 64) {
                wchar_t next = raw[i + 1];
                if (iswalpha(next) || next == L'/') {
                    /* Handle <br> / <br/> as newline */
                    if (_wcsnicmp(raw + i + 1, L"br", 2) == 0 &&
                        (raw[i + 3] == L'>' || raw[i + 3] == L'/' || raw[i + 3] == L' ')) {
                        out[w++] = L'\n';
                    }
                    i = (size_t)(close - raw);
                    continue;
                }
            }
        } else if (raw[i] == L'{' && raw[i + 1] == L'\\') {
            const wchar_t *close = wcschr(raw + i + 2, L'}');
            const wchar_t *nl = wcschr(raw + i + 2, L'\n');
            if (close && (!nl || nl > close) && (size_t)(close - (raw + i)) <= 48) {
                i = (size_t)(close - raw);
                continue;
            }
        }
        out[w++] = raw[i];
    }
    out[w] = L'\0';

    /* Trim leading and trailing whitespace/newlines */
    size_t start = 0;
    while (out[start] == L' ' || out[start] == L'\t' || out[start] == L'\n') {
        start++;
    }
    size_t end = w;
    while (end > start && (out[end - 1] == L' ' || out[end - 1] == L'\t' || out[end - 1] == L'\n')) {
        end--;
    }
    if (start > 0) {
        memmove(out, out + start, (end - start) * sizeof(wchar_t));
    }
    out[end - start] = L'\0';
    return out;
}

typedef struct CueOrderItem {
    SrtCue cue;
    size_t orig_index;
} CueOrderItem;

static int cmp_cue_order(const void *a, const void *b) {
    const CueOrderItem *ca = (const CueOrderItem *)a;
    const CueOrderItem *cb = (const CueOrderItem *)b;
    if (ca->cue.start_ms < cb->cue.start_ms) return -1;
    if (ca->cue.start_ms > cb->cue.start_ms) return 1;
    if (ca->orig_index < cb->orig_index) return -1;
    if (ca->orig_index > cb->orig_index) return 1;
    return 0;
}

bool srt_load_file(const wchar_t *srt_path, SrtSubtitle *out_sub) {
    if (!srt_path || !out_sub) {
        return false;
    }
    memset(out_sub, 0, sizeof(*out_sub));

    HANDLE hFile = CreateFileW(srt_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER fsize;
    if (!GetFileSizeEx(hFile, &fsize) || fsize.QuadPart < 0 || fsize.QuadPart > 32 * 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }

    DWORD bytes_size = (DWORD)fsize.QuadPart;
    unsigned char *raw = (unsigned char *)malloc(bytes_size + 2);
    if (!raw) {
        CloseHandle(hFile);
        return false;
    }

    DWORD read_bytes = 0;
    BOOL ok = (bytes_size == 0) ? TRUE : ReadFile(hFile, raw, bytes_size, &read_bytes, NULL);
    CloseHandle(hFile);
    if (!ok) {
        free(raw);
        return false;
    }
    raw[read_bytes] = 0;
    raw[read_bytes + 1] = 0;

    wchar_t *text = decode_bytes_to_wide(raw, read_bytes);
    free(raw);
    if (!text) {
        return false;
    }

    /* Split in-place into lines (normalizing \r\n and \r to \n) */
    size_t text_len = wcslen(text);
    size_t line_cap = 128;
    size_t line_count = 0;
    wchar_t **lines = (wchar_t **)malloc(line_cap * sizeof(wchar_t *));
    if (!lines) {
        free(text);
        return false;
    }

    size_t pos = 0;
    while (pos <= text_len) {
        if (line_count == line_cap) {
            size_t new_cap = line_cap * 2;
            wchar_t **tmp = (wchar_t **)realloc(lines, new_cap * sizeof(wchar_t *));
            if (!tmp) {
                break;
            }
            lines = tmp;
            line_cap = new_cap;
        }
        lines[line_count++] = text + pos;
        wchar_t *nl = wcspbrk(text + pos, L"\r\n");
        if (!nl) {
            break;
        }
        if (nl[0] == L'\r' && nl[1] == L'\n') {
            nl[0] = L'\0';
            pos = (size_t)(nl - text + 2);
        } else {
            nl[0] = L'\0';
            pos = (size_t)(nl - text + 1);
        }
    }

    size_t i = 0;
    while (i < line_count) {
        long long start_ms = 0, end_ms = 0;
        if (!parse_time_range_line(lines[i], &start_ms, &end_ms)) {
            i++;
            continue;
        }
        i++;

        /* Collect cue text lines */
        size_t buf_cap = 256;
        size_t buf_len = 0;
        wchar_t *cue_buf = (wchar_t *)malloc(buf_cap * sizeof(wchar_t));
        if (!cue_buf) {
            break;
        }
        cue_buf[0] = L'\0';

        while (i < line_count) {
            if (is_blank_line(lines[i])) {
                i++;
                break;
            }
            /* Handle missing blank line before next cue */
            if (buf_len > 0 && is_digits_only_trimmed(lines[i]) &&
                (i + 1 < line_count) && wcsstr(lines[i + 1], L"-->") != NULL) {
                long long dummy_s, dummy_e;
                if (parse_time_range_line(lines[i + 1], &dummy_s, &dummy_e)) {
                    break;
                }
            }
            if (buf_len > 0 && wcsstr(lines[i], L"-->") != NULL) {
                long long dummy_s, dummy_e;
                if (parse_time_range_line(lines[i], &dummy_s, &dummy_e)) {
                    break;
                }
            }

            size_t llen = wcslen(lines[i]);
            size_t needed = buf_len + (buf_len > 0 ? 1 : 0) + llen + 1;
            if (needed > buf_cap) {
                while (buf_cap < needed) {
                    buf_cap *= 2;
                }
                wchar_t *tmp = (wchar_t *)realloc(cue_buf, buf_cap * sizeof(wchar_t));
                if (!tmp) {
                    break;
                }
                cue_buf = tmp;
            }
            if (buf_len > 0) {
                cue_buf[buf_len++] = L'\n';
            }
            memcpy(cue_buf + buf_len, lines[i], llen * sizeof(wchar_t));
            buf_len += llen;
            cue_buf[buf_len] = L'\0';
            i++;
        }

        wchar_t *clean = strip_subtitle_tags_and_trim(cue_buf);
        free(cue_buf);
        if (clean && clean[0] != L'\0') {
            if (out_sub->count == out_sub->capacity) {
                size_t new_cap = out_sub->capacity ? out_sub->capacity * 2 : 64;
                SrtCue *tmp = (SrtCue *)realloc(out_sub->cues, new_cap * sizeof(SrtCue));
                if (!tmp) {
                    free(clean);
                    break;
                }
                out_sub->cues = tmp;
                out_sub->capacity = new_cap;
            }
            out_sub->cues[out_sub->count].start_ms = start_ms;
            out_sub->cues[out_sub->count].end_ms = end_ms;
            out_sub->cues[out_sub->count].text = clean;
            out_sub->count++;
        } else {
            free(clean);
        }
    }

    free(lines);
    free(text);

    if (out_sub->count > 1) {
        CueOrderItem *items = (CueOrderItem *)malloc(out_sub->count * sizeof(CueOrderItem));
        if (items) {
            for (size_t k = 0; k < out_sub->count; k++) {
                items[k].cue = out_sub->cues[k];
                items[k].orig_index = k;
            }
            qsort(items, out_sub->count, sizeof(CueOrderItem), cmp_cue_order);
            for (size_t k = 0; k < out_sub->count; k++) {
                out_sub->cues[k] = items[k].cue;
            }
            free(items);
        }
    }

    return true;
}

void srt_free_subtitle(SrtSubtitle *sub) {
    if (!sub) {
        return;
    }
    for (size_t i = 0; i < sub->count; i++) {
        free(sub->cues[i].text);
    }
    free(sub->cues);
    memset(sub, 0, sizeof(*sub));
}

bool srt_get_active_text(const SrtSubtitle *sub, long long pos_ms, wchar_t *buf, size_t buf_chars) {
    if (!buf || buf_chars == 0) {
        return false;
    }
    buf[0] = L'\0';
    if (!sub || sub->count == 0 || pos_ms < 0) {
        return false;
    }

    size_t written = 0;
    for (size_t i = 0; i < sub->count; i++) {
        const SrtCue *c = &sub->cues[i];
        if (c->start_ms > pos_ms) {
            break;
        }
        if (pos_ms >= c->start_ms && pos_ms < c->end_ms) {
            size_t tlen = wcslen(c->text);
            size_t sep = (written > 0) ? 1 : 0;
            if (written + sep + tlen >= buf_chars) {
                size_t avail = (buf_chars - 1 > written + sep) ? (buf_chars - 1 - written - sep) : 0;
                if (sep && written + 1 < buf_chars) {
                    buf[written++] = L'\n';
                }
                if (avail > 0) {
                    memcpy(buf + written, c->text, avail * sizeof(wchar_t));
                    written += avail;
                }
                buf[written] = L'\0';
                break;
            }
            if (sep) {
                buf[written++] = L'\n';
            }
            memcpy(buf + written, c->text, tlen * sizeof(wchar_t));
            written += tlen;
            buf[written] = L'\0';
        }
    }

    return written > 0;
}
