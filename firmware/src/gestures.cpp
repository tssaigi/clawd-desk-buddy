#include "gestures.h"
#include <Arduino.h>
#include <lvgl.h>
#include "clawd_director.h"
#include "sessions_view.h"
#include "colony_view.h"

#define TAP_MAX_PX    14   // movement still counted as a tap
#define SWIPE_MIN_PX  40   // movement that makes a swipe

static lv_indev_t* indev = NULL;
static lv_point_t  start = { 0, 0 };

static void on_pressed(lv_event_t* e) {
    (void)e;
    lv_indev_get_point(indev, &start);
}

static void on_released(lv_event_t* e) {
    (void)e;
    lv_point_t end = start;
    lv_indev_get_point(indev, &end);   // the last pressed point (release keeps it)
    const int dx = end.x - start.x, dy = end.y - start.y;
    const int ax = abs(dx), ay = abs(dy);

    if (colony_view_visible()) {
        // Taps and vertical drags belong to the page (blocks, "< All", scrolling);
        // a right→left swipe goes back to the meters.
        if (ax >= SWIPE_MIN_PX && ax > ay && dx < 0) colony_view_hide();
        return;
    }
    if (sessions_view_visible()) {
        // The list scrolls itself on vertical drags; only a left→right swipe
        // means something here: back to the meters.
        if (ax >= SWIPE_MIN_PX && ax > ay && dx > 0) sessions_view_hide();
        return;
    }
    if (ax <= TAP_MAX_PX && ay <= TAP_MAX_PX) {
        director_tap(start.x);
        return;
    }
    if (director_room() == ROOM_FACE) return;     // the face is up: only a tap sends it back
    if (ay >= SWIPE_MIN_PX && ay > ax && dy < 0) {
        Serial.printf("gesture: swipe up -> face %s\n", director_face_open(start.x) ? "up" : "not now");
        return;
    }
    if (ax >= SWIPE_MIN_PX && ax > ay && dx < 0) {
        Serial.println("gesture: swipe right-to-left -> sessions");
        sessions_view_show();
        return;
    }
    if (ax >= SWIPE_MIN_PX && ax > ay && dx > 0) {
        Serial.println("gesture: swipe left-to-right -> colony");
        colony_view_show();
        return;
    }
    // Swipe down on the meters: nothing.
}

void gestures_init(void) {
    indev = lv_indev_get_next(NULL);
    if (!indev) return;
    director_set_gesture_mode(true);
    lv_indev_add_event_cb(indev, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_indev_add_event_cb(indev, on_released, LV_EVENT_RELEASED, NULL);
}
