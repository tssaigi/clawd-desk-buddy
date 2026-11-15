#pragma once
#include <lvgl.h>

// Clawd visiting a side page (mascot_visit): when he runs off the meters'
// right edge he turns up on the sessions page, off the left edge on the
// colony page, drawn at the same scale as on the meters. He walks in from the
// page edge nearest the meters and roams along the bottom: walks, stops, waves,
// jumps, dances, points; when his visit ends he walks back out. While you are on
// his page he stays (you followed him); once you leave, he follows you home.

// side: +1 the page right of the meters (sessions), -1 the one left (colony).
void visitor_attach(lv_obj_t* page, int side, int page_w, int page_h);
// Per loop, with which side pages are on screen.
void visitor_tick(bool left_page_visible, bool right_page_visible);
