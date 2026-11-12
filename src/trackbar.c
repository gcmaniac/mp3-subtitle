#define WIN32_LEAN_AND_MEAN
#include "trackbar.h"

#include <commctrl.h>

#define TRACKBAR_SUBCLASS_ID 1

int trackbar_calc_pos_from_x(HWND hwnd, int x) {
    if (!hwnd) {
        return 0;
    }

    int min_val = (int)SendMessageW(hwnd, TBM_GETRANGEMIN, 0, 0);
    int max_val = (int)SendMessageW(hwnd, TBM_GETRANGEMAX, 0, 0);
    if (max_val <= min_val) {
        return min_val;
    }

    RECT ch = {0};
    SendMessageW(hwnd, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
    RECT th = {0};
    SendMessageW(hwnd, TBM_GETTHUMBRECT, 0, (LPARAM)&th);

    long long thumb_w = (th.right > th.left) ? (long long)(th.right - th.left) : 0;
    long long min_c2 = 2LL * ch.left + thumb_w;
    long long max_c2 = 2LL * ch.right - thumb_w;
    long long span2 = max_c2 - min_c2;

    if (span2 <= 0) {
        min_c2 = 2LL * ch.left;
        max_c2 = 2LL * ch.right;
        span2 = max_c2 - min_c2;
    }
    if (span2 <= 0) {
        RECT client = {0};
        GetClientRect(hwnd, &client);
        min_c2 = 2LL * client.left;
        max_c2 = 2LL * client.right;
        span2 = max_c2 - min_c2;
        if (span2 <= 0) {
            return min_val;
        }
    }

    long long rel = 2LL * x - min_c2;
    if (rel <= 0) {
        return min_val;
    }
    if (rel >= span2) {
        return max_val;
    }

    long long range = (long long)max_val - (long long)min_val;
    long long pos = min_val + (rel * range + span2 / 2) / span2;
    if (pos < min_val) pos = min_val;
    if (pos > max_val) pos = max_val;
    return (int)pos;
}

static void trackbar_notify_parent(HWND hwnd, WORD code, int pos) {
    HWND parent = GetParent(hwnd);
    if (parent) {
        SendMessageW(parent, WM_HSCROLL,
                     MAKEWPARAM(code, (WORD)(pos & 0xFFFF)),
                     (LPARAM)hwnd);
    }
}

static LRESULT CALLBACK trackbar_click_subclass_proc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    TrackbarClickState *st = (TrackbarClickState *)(void *)dwRefData;

    switch (msg) {
    case WM_LBUTTONDOWN: {
        if (!IsWindowEnabled(hwnd) || !st) {
            break;
        }
        int x = (int)(short)LOWORD(lParam);
        int pos = trackbar_calc_pos_from_x(hwnd, x);
        st->dragging = true;
        SetCapture(hwnd);
        SendMessageW(hwnd, TBM_SETPOS, TRUE, (LPARAM)pos);
        trackbar_notify_parent(hwnd, TB_THUMBTRACK, pos);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (st && st->dragging) {
            int x = (int)(short)LOWORD(lParam);
            int pos = trackbar_calc_pos_from_x(hwnd, x);
            LRESULT cur = SendMessageW(hwnd, TBM_GETPOS, 0, 0);
            if ((int)cur != pos) {
                SendMessageW(hwnd, TBM_SETPOS, TRUE, (LPARAM)pos);
                trackbar_notify_parent(hwnd, TB_THUMBTRACK, pos);
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (st && st->dragging) {
            int x = (int)(short)LOWORD(lParam);
            int pos = trackbar_calc_pos_from_x(hwnd, x);
            st->dragging = false;
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            SendMessageW(hwnd, TBM_SETPOS, TRUE, (LPARAM)pos);
            trackbar_notify_parent(hwnd, TB_ENDTRACK, pos);
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (st && st->dragging && (HWND)lParam != hwnd) {
            st->dragging = false;
            int pos = (int)SendMessageW(hwnd, TBM_GETPOS, 0, 0);
            trackbar_notify_parent(hwnd, TB_ENDTRACK, pos);
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, trackbar_click_subclass_proc, TRACKBAR_SUBCLASS_ID);
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

bool trackbar_attach_click_seek(HWND hwnd, TrackbarClickState *state) {
    if (!hwnd || !state) {
        return false;
    }
    state->dragging = false;
    return SetWindowSubclass(hwnd, trackbar_click_subclass_proc,
                             TRACKBAR_SUBCLASS_ID, (DWORD_PTR)(void *)state) != FALSE;
}

