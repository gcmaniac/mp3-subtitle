#include "player.h"

#include <mmsystem.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MCI_ALIAS L"mp3sub_player"

static bool mci_exec(Mp3Player *p, const wchar_t *cmd, wchar_t *ret_buf,
                     DWORD ret_chars) {
  if (ret_buf && ret_chars > 0) {
    ret_buf[0] = L'\0';
  }
  HWND cb = p ? p->hwnd_callback : NULL;
  MCIERROR err = mciSendStringW(cmd, ret_buf, ret_chars, cb);
  if (err != 0) {
    if (p) {
      if (!mciGetErrorStringW(
              err, p->last_error,
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
  p->hwnd_callback = NULL;
}

void player_set_callback(Mp3Player *p, HWND hwnd) {
  if (p) {
    p->hwnd_callback = hwnd;
  }
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
  bool opened = false;

  /* Strategy:
   * 1. Try auto-detect open (no type) so Windows MCI routes via registry
   * extension mapping (WaveAudio for .wav, Sequencer for .mid, MPEGVideo for
   * .mp3/.wma/.m4a/.aac, etc.)
   * 2. If that fails, try explicit 'type mpegvideo' (DirectShow / Quartz filter
   * graph, which handles MP3, WMA, M4A, AAC, FLAC, OGG, etc. on modern Windows)
   * 3. If that fails, try explicit 'type waveaudio' (for RIFF wave files)
   */
  _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
             L"open \"%ls\" alias " MCI_ALIAS, path);
  if (mci_exec(NULL, cmd, NULL, 0)) {
    opened = true;
  } else {
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
               L"open \"%ls\" type mpegvideo alias " MCI_ALIAS, path);
    if (mci_exec(NULL, cmd, NULL, 0)) {
      opened = true;
    } else {
      _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
                 L"open \"%ls\" type waveaudio alias " MCI_ALIAS, path);
      if (mci_exec(p, cmd, NULL, 0)) {
        opened = true;
      }
    }
  }

  if (!opened) {
    if (p->last_error[0] == L'\0') {
      /* Capture error from the primary command */
      _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
                 L"open \"%ls\" alias " MCI_ALIAS, path);
      mci_exec(p, cmd, NULL, 0);
    }
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
  wchar_t cmd[128];
  const wchar_t *notify_flag = (p->hwnd_callback != NULL) ? L" notify" : L"";
  if (p->duration_ms > 0 && pos >= p->duration_ms - 50) {
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
               L"play " MCI_ALIAS L" from 0%ls", notify_flag);
  } else {
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"play " MCI_ALIAS L"%ls",
               notify_flag);
  }
  if (!mci_exec(p, cmd, NULL, 0)) {
    return false;
  }
  p->state = PLAYER_PLAYING;
  return true;
}

bool player_play_from(Mp3Player *p, long long pos_ms) {
  if (!p || !p->is_open) {
    return false;
  }
  if (pos_ms < 0)
    pos_ms = 0;
  if (p->duration_ms > 0 && pos_ms >= p->duration_ms - 50) {
    pos_ms = 0;
  }
  wchar_t cmd[128];
  const wchar_t *notify_flag = (p->hwnd_callback != NULL) ? L" notify" : L"";
  _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
             L"play " MCI_ALIAS L" from %lld%ls", pos_ms, notify_flag);
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
    const wchar_t *notify_flag = (p->hwnd_callback != NULL) ? L" notify" : L"";
    _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
               L"play " MCI_ALIAS L" from %lld%ls", pos_ms, notify_flag);
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
  if (vol_percent < 0)
    vol_percent = 0;
  if (vol_percent > 100)
    vol_percent = 100;
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
    if (p)
      p->state = PLAYER_STOPPED;
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

bool player_is_supported_audio_extension(const wchar_t *filename) {
  if (!filename)
    return false;
  const wchar_t *ext = wcsrchr(filename, L'.');
  if (!ext)
    return false;
  static const wchar_t *const audio_exts[] = {
      L".mp3",  L".ogg", L".oga",  L".opus", L".wav",
      L".wma",  L".m4a", L".aac",  L".flac", L".mid",
      L".midi", L".aif", L".aiff", L".mp2",  L".mpa"};
  for (size_t i = 0; i < sizeof(audio_exts) / sizeof(audio_exts[0]); i++) {
    if (_wcsicmp(ext, audio_exts[i]) == 0) {
      return true;
    }
  }
  return false;
}

static int compare_audio_filenames(const void *a, const void *b) {
  const wchar_t *const *str_a = (const wchar_t *const *)a;
  const wchar_t *const *str_b = (const wchar_t *const *)b;
  return StrCmpLogicalW(*str_a, *str_b);
}

static wchar_t *clone_wstr(const wchar_t *s) {
  if (!s)
    return NULL;
  size_t len = wcslen(s) + 1;
  wchar_t *p = (wchar_t *)malloc(len * sizeof(wchar_t));
  if (p) {
    memcpy(p, s, len * sizeof(wchar_t));
  }
  return p;
}

    static bool find_neighbor_audio_file(const wchar_t *current_path,
                                         wchar_t *path_out, size_t path_chars,
                                         int offset) {
      if (!current_path || !current_path[0] || !path_out || path_chars == 0) {
        return false;
      }

      wchar_t dir[MAX_PATH];
      const wchar_t *slash1 = wcsrchr(current_path, L'\\');
      const wchar_t *slash2 = wcsrchr(current_path, L'/');
      const wchar_t *slash = (slash1 && slash2)
                                 ? (slash1 > slash2 ? slash1 : slash2)
                                 : (slash1 ? slash1 : slash2);

      const wchar_t *current_filename = NULL;
      if (slash) {
        size_t dir_len = (size_t)(slash - current_path);
        if (dir_len == 2 && current_path[1] == L':') {
          dir_len = 3; /* Keep root slash e.g. "C:\" */
        }
        if (dir_len >= MAX_PATH) {
          return false;
        }
        wcsncpy(dir, current_path, dir_len);
        dir[dir_len] = L'\0';
        current_filename = slash + 1;
      } else {
        wcscpy(dir, L".");
        current_filename = current_path;
      }

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
      wchar_t **files = (wchar_t **)malloc(capacity * sizeof(wchar_t *));
      if (!files) {
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
          wchar_t **new_files =
              (wchar_t **)realloc(files, new_cap * sizeof(wchar_t *));
          if (!new_files) {
            break;
          }
          files = new_files;
          capacity = new_cap;
        }

        files[count] = clone_wstr(fd.cFileName);
        if (files[count]) {
          count++;
        }
      } while (FindNextFileW(hFind, &fd));

      FindClose(hFind);

      if (count == 0) {
        free(files);
        return false;
      }

      qsort(files, count, sizeof(wchar_t *), compare_audio_filenames);

      int cur_idx = -1;
      for (size_t i = 0; i < count; i++) {
        if (_wcsicmp(files[i], current_filename) == 0) {
          cur_idx = (int)i;
          break;
        }
      }

      bool found = false;
        int target_idx = cur_idx + offset;
        if (cur_idx >= 0 && target_idx >= 0 && (size_t)target_idx < count) {
          const wchar_t *target_file = files[target_idx];
          if (slash) {
            if (dlen > 0 && (dir[dlen - 1] == L'\\' || dir[dlen - 1] == L'/')) {
              _snwprintf(path_out, path_chars, L"%ls%ls", dir, target_file);
            } else {
              _snwprintf(path_out, path_chars, L"%ls\\%ls", dir, target_file);
            }
          } else {
            _snwprintf(path_out, path_chars, L"%ls", target_file);
          }
          found = true;
        }

        for (size_t i = 0; i < count; i++) {
          free(files[i]);
        }
        free(files);

        return found;
      }

      bool player_find_next_audio_file(const wchar_t *current_path,
                                       wchar_t *next_path_out,
                                       size_t next_path_chars) {
        return find_neighbor_audio_file(current_path, next_path_out,
                                        next_path_chars, 1);
      }

      bool player_find_prev_audio_file(const wchar_t *current_path,
                                       wchar_t *prev_path_out,
                                       size_t prev_path_chars) {
        return find_neighbor_audio_file(current_path, prev_path_out,
                                        prev_path_chars, -1);
      }
