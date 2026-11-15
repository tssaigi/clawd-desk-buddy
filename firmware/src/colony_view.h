#pragma once
#include <stdint.h>

// The colony page (the Crab Colony on the device): every live Claude Code
// session with its agents (the main Claude plus each helper agent).
//   - One session: its agents, large, straight away.
//   - Several: one block per session whose picture is all its agents' Clawds
//     side by side; tapping a block opens that session large.
//   - The open session stays open when you swipe away and come back. Only when
//     its agents stop working does the page go back to the blocks. "‹ All" in
//     the header goes back by hand.
// It sits left of the meters (gestures.h).
//
// Data from the host bridge over serial (main.cpp):
//   col clear
//   col s <sid> <working 0|1> <title...>     one per session
//   col a <sid> <state> <name...>            one per agent, after its session
//   col done
// state is a Crab Colony state: idle thinking working reading searching moving
// talking arguing celebrating error sleeping.

void colony_view_init(void);
void colony_view_tick(void);
void colony_view_show(void);
void colony_view_hide(void);
bool colony_view_visible(void);

void colony_clear(void);
void colony_add_session(const char* sid, bool working, const char* title);
void colony_add_agent(const char* sid, const char* state, const char* name);
void colony_commit(void);
void colony_open(const char* sid);   // QA: open a session large, as a tap on its block does
