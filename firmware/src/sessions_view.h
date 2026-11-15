#pragma once
#include <stdint.h>

// The sessions page: one box per live Claude Code session, each with a small
// Clawd acting out what that session is doing, its name, the action in words
// and how full its context is. It slides over the meters from the right
// (gestures.h) and scrolls vertically when the boxes don't fit.
//
// Data comes from the host bridge over serial (main.cpp):
//   sess clear                    start a new list
//   sess add <state> <ctx%> <title...>
//   sess done                     show it
// state is a director_status state (thinking, coding, reading, searching,
// delegating, asking, done, idle).

void sessions_view_init(void);   // after director_init(): the page sits on top of the meters
void sessions_view_tick(void);   // per loop: sprite frames, auto-close on attention
void sessions_view_show(void);
void sessions_view_hide(void);
bool sessions_view_visible(void);

void sessions_clear(void);
void sessions_add(const char* state, int ctx_pct, const char* title);
void sessions_commit(void);
