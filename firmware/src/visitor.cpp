#include "visitor.h"
#include <Arduino.h>
#include "mini_clawd.h"
#include "mascot.h"

// He's drawn at the corner Clawd's own scale (mascot_cell), so he is the same
// size on every page. The slot fits the widest and tallest scenes he plays
// (pointing is 28 cells wide, jumping 30 tall).
#define SLOT_CELLS_W  30
#define SLOT_CELLS_H  31
#define FOLLOW_MS     4000   // after you leave his page he lingers this long, then follows

enum VState { V_OUT, V_ENTER, V_WALK, V_PAUSE, V_ACT, V_LEAVE };

struct Visitor {
    lv_obj_t* page;
    int       side;
    int       w, h;
    int       slot_w, slot_h, cell;
    MiniClawd m;
    VState    st;
    float     x;            // slot left edge on the page, px
    float     target;
    int       edge;         // off-page x on the meters' side
    bool      facing_left;
    uint32_t  last_ms;
    uint32_t  until_ms;     // end of a pause or an act
};

static Visitor vis[2];
static int     vis_count = 0;

// The corner Clawd's walk: 6 cells per 450 ms gait loop at 100 %.
static float walk_px_s(const Visitor* v) { return 6.0f * v->cell / 0.45f; }

static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + esp_random() % (hi - lo + 1); }

void visitor_attach(lv_obj_t* page, int side, int page_w, int page_h) {
    if (!page || vis_count >= 2) return;
    Visitor* v = &vis[vis_count++];
    v->page = page;
    v->side = side;
    v->w = page_w;
    v->h = page_h;
    v->cell = mascot_cell() > 0 ? mascot_cell() : 2;
    v->slot_w = SLOT_CELLS_W * v->cell;
    v->slot_h = SLOT_CELLS_H * v->cell;
    // The right-hand page meets the meters at its left edge, and the other way round.
    v->edge = side > 0 ? -v->slot_w : page_w;
    v->x = v->edge;
    v->st = V_OUT;
    mini_clawd_init(&v->m, page, v->slot_w, v->slot_h, v->cell);
    lv_obj_add_flag(v->m.img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v->m.img, LV_OBJ_FLAG_CLICKABLE);
}

static void face(Visitor* v, bool left) {
    v->facing_left = left;
    mini_clawd_set_mirror(&v->m, left);
}

static void start_walk(Visitor* v, float target, VState st) {
    v->target = target;
    v->st = st;
    face(v, target < v->x);
    mini_clawd_set(&v->m, "walking", true);
}

// Somewhere new along the bottom of the page, at least a third of the page away.
static void wander(Visitor* v) {
    const int lo = 0, hi = v->w - v->slot_w;
    float t = v->x;
    for (int i = 0; i < 4 && fabsf(t - v->x) < v->w / 3; i++) t = rnd(lo, hi);
    start_walk(v, t, V_WALK);
}

static void pause_or_act(Visitor* v, uint32_t now) {
    static const char* const ACTS[] = { "waving", "jumping", "dancing", "pointing", "jumping happy", "waving" };
    if (esp_random() % 100 < 60) {
        mini_clawd_set(&v->m, ACTS[esp_random() % 6], true);
        v->st = V_ACT;
        v->until_ms = now + rnd(2000, 3200);
    } else {
        mini_clawd_set(&v->m, "walking", false);              // the idle pose
        v->st = V_PAUSE;
        v->until_ms = now + rnd(1000, 2500);
    }
}

// Walk toward target at speed; true once there.
static bool step_to(Visitor* v, float speed, float dt) {
    const float d = v->target - v->x, step = speed * dt;
    if (fabsf(d) <= step) { v->x = v->target; return true; }
    v->x += d > 0 ? step : -step;
    return false;
}

static void tick_one(Visitor* v, bool page_visible, uint32_t now) {
    const bool here = mascot_visit_side() == v->side;
    if (here && page_visible) mascot_visit_extend(FOLLOW_MS);   // you followed him: he stays with you

    const float dt = (now - v->last_ms) / 1000.0f;
    v->last_ms = now;
    const float speed = walk_px_s(v);

    if (v->st == V_OUT) {
        if (!here) return;
        v->x = v->edge;
        lv_obj_clear_flag(v->m.img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(v->m.img);
        // In a few steps from the meters' side, then he roams.
        start_walk(v, v->side > 0 ? v->slot_w / 2 : v->w - v->slot_w * 3 / 2, V_ENTER);
    } else if (!here && v->st != V_LEAVE) {
        start_walk(v, v->edge, V_LEAVE);                       // visit over: back the way he came
    } else {
        switch (v->st) {
        case V_ENTER:
        case V_WALK:
            if (step_to(v, speed, dt)) pause_or_act(v, now);
            break;
        case V_PAUSE:
        case V_ACT:
            if ((int32_t)(now - v->until_ms) >= 0) wander(v);
            break;
        case V_LEAVE:
            if (here) { wander(v); break; }                     // called back while leaving
            if (step_to(v, speed * 1.5f, dt)) {
                v->st = V_OUT;
                lv_obj_add_flag(v->m.img, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            break;
        default:
            break;
        }
    }
    lv_obj_set_pos(v->m.img, (int)v->x, v->h - v->slot_h);
    mini_clawd_tick(&v->m, now);
}

void visitor_tick(bool left_page_visible, bool right_page_visible) {
    const uint32_t now = millis();
    for (int i = 0; i < vis_count; i++) {
        Visitor* v = &vis[i];
        if (!v->last_ms) v->last_ms = now;
        tick_one(v, v->side > 0 ? right_page_visible : left_page_visible, now);
    }
}
