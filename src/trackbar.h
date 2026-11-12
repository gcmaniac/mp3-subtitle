#ifndef TRACKBAR_H
#define TRACKBAR_H

#include <windows.h>
#include <stdbool.h>

typedef struct TrackbarClickState {
    bool dragging;
} TrackbarClickState;

/* Computes the trackbar position in [min, max] corresponding to client X coordinate. */
int trackbar_calc_pos_from_x(HWND hwnd, int x);

/* Subclasses a horizontal trackbar so left-clicking anywhere on its channel jumps directly there. */
bool trackbar_attach_click_seek(HWND hwnd, TrackbarClickState *state);

#endif

