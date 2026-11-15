#include "sessions_view.h"
#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "theme.h"
#include "clawd_anim.h"
#include "mini_clawd.h"
#include "clawd_director.h"
#include "hal/board_caps.h"

LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);

#define MAX_SESS      10
#define TITLE_LEN     40
#define BOX_H         70
#define BOX_GAP       6
#define HEADER_H      30
#define SPRITE_CELL   2            // art cells → pixels
#define SPRITE_W      84           // sprite slot in the box
#define SPRITE_H      60
#define STALE_MS      20000        // no list from the bridge for this long: say so
#define SLIDE_MS      220

struct Row {
    char state[12];
    int  ctx;
    char title[TITLE_LEN];
};

struct Box {
    lv_obj_t*       box;
    lv_obj_t*       img;
    lv_obj_t*       title;
    lv_obj_t*       action;
    lv_obj_t*       bar;
    lv_obj_t*       ctx;
    MiniClawd       sprite;
};

static lv_obj_t* page = NULL;
static lv_obj_t* header_count = NULL;
static lv_obj_t* list = NULL;
static lv_obj_t* note = NULL;
static Box       boxes[MAX_SESS];
static int       box_count = 0;
static Row       pending[MAX_SESS];
static int       pending_count = 0;
static Row       rows[MAX_SESS];
static int       row_count = 0;
static uint32_t  last_list_ms = 0;     // 0 = never
static bool      shown = false;
static int       screen_w = 320, screen_h = 240;

// ─── Sprites ─────────────────────────────────────────────────────────────────
struct StateInfo { const char* state; const char* anim; const char* words; bool animate; };
static const StateInfo STATES[] = {
    { "thinking",   "cloud",         "Thinking",          true  },
    { "coding",     "laptop",        "Writing code",      true  },
    { "reading",    "magnifier",     "Reading files",     true  },
    { "searching",  "magnifier",     "Searching the web", true  },
    { "delegating", "pointing",      "Running helpers",   true  },
    { "asking",     "waving",        "Asking you",        true  },
    { "done",       "jumping happy", "Done",              true  },
    { "idle",       "walking",       "Idle",              false },   // frame 0 is the idle pose
};

static const StateInfo* info_for(const char* state) {
    for (const auto& s : STATES) if (strcmp(s.state, state) == 0) return &s;
    return &STATES[sizeof(STATES) / sizeof(STATES[0]) - 1];
}

// ─── Boxes ───────────────────────────────────────────────────────────────────
static void make_box(Box* b) {
    const int w = screen_w - 16;
    b->box = lv_obj_create(list);
    lv_obj_set_size(b->box, w, BOX_H);
    lv_obj_set_style_bg_color(b->box, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(b->box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b->box, 0, 0);
    lv_obj_set_style_radius(b->box, 10, 0);
    lv_obj_set_style_pad_all(b->box, 0, 0);
    lv_obj_clear_flag(b->box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b->box, LV_OBJ_FLAG_EVENT_BUBBLE);   // drags scroll the list

    mini_clawd_init(&b->sprite, b->box, SPRITE_W, SPRITE_H, SPRITE_CELL);
    lv_obj_set_pos(b->sprite.img, 4, (BOX_H - SPRITE_H) / 2);

    const int tx = SPRITE_W + 10, tw = w - tx - 8;
    b->title = lv_label_create(b->box);
    lv_obj_set_style_text_font(b->title, &font_styrene_16, 0);
    lv_obj_set_style_text_color(b->title, THEME_TEXT, 0);
    lv_label_set_long_mode(b->title, LV_LABEL_LONG_DOT);
    lv_obj_set_size(b->title, tw, lv_font_get_line_height(&font_styrene_16));   // one line, "..." past it
    lv_obj_set_pos(b->title, tx, 7);

    b->action = lv_label_create(b->box);
    lv_obj_set_style_text_font(b->action, &font_styrene_14, 0);
    lv_obj_set_style_text_color(b->action, THEME_ACCENT, 0);
    lv_obj_set_pos(b->action, tx, 28);

    b->bar = lv_bar_create(b->box);
    lv_obj_set_size(b->bar, tw - 52, 8);
    lv_obj_set_pos(b->bar, tx, 52);
    lv_bar_set_range(b->bar, 0, 100);
    lv_obj_set_style_bg_color(b->bar, THEME_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b->bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(b->bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(b->bar, 4, LV_PART_INDICATOR);

    b->ctx = lv_label_create(b->box);
    lv_obj_set_style_text_font(b->ctx, &font_styrene_12, 0);
    lv_obj_set_style_text_color(b->ctx, THEME_DIM, 0);
    lv_obj_set_pos(b->ctx, tx + tw - 46, 48);

}

static void fill_box(Box* b, const Row* r) {
    const StateInfo* si = info_for(r->state);
    lv_label_set_text(b->title, r->title);
    lv_label_set_text(b->action, si->words);
    const int ctx = r->ctx < 0 ? 0 : r->ctx > 100 ? 100 : r->ctx;
    lv_bar_set_value(b->bar, ctx, LV_ANIM_OFF);
    // Green while there's room, amber past 70 %, red past 90 %.
    lv_color_t col = ctx >= 90 ? THEME_RED : ctx >= 70 ? THEME_AMBER : THEME_GREEN;
    lv_obj_set_style_bg_color(b->bar, col, LV_PART_INDICATOR);
    lv_label_set_text_fmt(b->ctx, r->ctx < 0 ? "ctx --" : "ctx %d%%", ctx);
    mini_clawd_set(&b->sprite, si->anim, si->animate);
}

static void rebuild(void) {
    while (box_count < row_count) make_box(&boxes[box_count++]);
    for (int i = 0; i < box_count; i++) {
        if (i < row_count) {
            lv_obj_clear_flag(boxes[i].box, LV_OBJ_FLAG_HIDDEN);
            fill_box(&boxes[i], &rows[i]);
        } else {
            lv_obj_add_flag(boxes[i].box, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_label_set_text_fmt(header_count, "%d", row_count);
}

static void update_note(void) {
    const uint32_t now = millis();
    const char* msg = NULL;
    if (!last_list_ms || now - last_list_ms > STALE_MS) msg = "Waiting for the Mac bridge\n(USB cable)";
    else if (row_count == 0) msg = "No active sessions";
    // The note replaces the content (a stale list would look live).
    if (msg) {
        lv_label_set_text(note, msg);
        lv_obj_clear_flag(note, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(note, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(list, LV_OBJ_FLAG_HIDDEN);
    }
}

// ─── Page ────────────────────────────────────────────────────────────────────
void sessions_view_init(void) {
    const BoardCaps& caps = board_caps();
    screen_w = caps.width;
    screen_h = caps.height;

    page = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page, screen_w, screen_h);
    lv_obj_set_pos(page, screen_w, 0);
    lv_obj_set_style_bg_color(page, THEME_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(page);
    lv_obj_set_style_text_font(title, &font_styrene_20, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT, 0);
    lv_label_set_text(title, "Sessions");
    lv_obj_set_pos(title, 10, 4);

    header_count = lv_label_create(page);
    lv_obj_set_style_text_font(header_count, &font_styrene_16, 0);
    lv_obj_set_style_text_color(header_count, THEME_ACCENT, 0);
    lv_label_set_text(header_count, "0");
    lv_obj_align(header_count, LV_ALIGN_TOP_RIGHT, -12, 7);

    list = lv_obj_create(page);
    lv_obj_set_size(list, screen_w, screen_h - HEADER_H);
    lv_obj_set_pos(list, 0, HEADER_H);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_hor(list, 8, 0);
    lv_obj_set_style_pad_ver(list, 4, 0);
    lv_obj_set_style_pad_row(list, BOX_GAP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

    note = lv_label_create(page);
    lv_obj_set_style_text_font(note, &font_styrene_16, 0);
    lv_obj_set_style_text_color(note, THEME_DIM, 0);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(note, LV_ALIGN_CENTER, 0, 10);

    update_note();
}

static void slide_x(void* obj, int32_t v) { lv_obj_set_x((lv_obj_t*)obj, v); }
static void hide_done(lv_anim_t* a) { lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN); }

static void slide(int32_t from, int32_t to, bool hide_after) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, page);
    lv_anim_set_exec_cb(&a, slide_x);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (hide_after) lv_anim_set_completed_cb(&a, hide_done);
    lv_anim_start(&a);
}

void sessions_view_show(void) {
    if (!page || shown) return;
    shown = true;
    update_note();
    lv_obj_move_foreground(page);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    slide(screen_w, 0, false);
}

void sessions_view_hide(void) {
    if (!page || !shown) return;
    shown = false;
    slide(lv_obj_get_x(page), screen_w, true);
}

bool sessions_view_visible(void) { return shown; }

void sessions_view_tick(void) {
    if (!page) return;
    // An alert needs the meters: the page gets out of the way.
    if (shown && strcmp(director_beat_name(), "attention") == 0) sessions_view_hide();
    if (!shown) return;
    static uint32_t note_at = 0;
    const uint32_t now = millis();
    if (now - note_at > 1000) { note_at = now; update_note(); }
    for (int i = 0; i < row_count && i < box_count; i++) mini_clawd_tick(&boxes[i].sprite, now);
}

// ─── Data ────────────────────────────────────────────────────────────────────
void sessions_clear(void) { pending_count = 0; }

void sessions_add(const char* state, int ctx_pct, const char* title) {
    if (pending_count >= MAX_SESS) return;
    Row* r = &pending[pending_count++];
    strncpy(r->state, state, sizeof(r->state) - 1);
    r->state[sizeof(r->state) - 1] = 0;
    r->ctx = ctx_pct;
    strncpy(r->title, title && *title ? title : "Session", TITLE_LEN - 1);
    r->title[TITLE_LEN - 1] = 0;
}

void sessions_commit(void) {
    memcpy(rows, pending, sizeof(Row) * pending_count);
    row_count = pending_count;
    last_list_ms = millis();
    if (!last_list_ms) last_list_ms = 1;
    if (page) { rebuild(); update_note(); }
}
