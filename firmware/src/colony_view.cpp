#include "colony_view.h"
#include <Arduino.h>
#include <lvgl.h>
#include <string.h>
#include <esp_heap_caps.h>
#include "theme.h"
#include "mini_clawd.h"
#include "visitor.h"
#include "clawd_director.h"
#include "hal/board_caps.h"

LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);

#define MAX_COL_SESS   6
#define MAX_AGENTS     8
#define SID_LEN        12
#define NAME_LEN       28
#define HEADER_H       30
#define BLOCK_H        74
#define ICON_W         32          // one agent in a block's picture
#define ICON_H         32
#define CARD_W         148         // one agent in the large view
#define CARD_H         104
#define CARD_SPRITE_W  84
#define CARD_SPRITE_H  60
#define STALE_MS       20000
#define SLIDE_MS       220

struct Agent { char state[14]; char name[NAME_LEN]; };
struct CSess {
    char  sid[SID_LEN];
    bool  working;
    char  title[40];
    Agent agents[MAX_AGENTS];
    int   agent_count;
};

// The tables live in PSRAM (allocated in colony_view_init): the classic
// ESP32's static DRAM is too tight for them.
static CSess* pend = NULL;
static int   pend_count = 0;
static CSess* sess = NULL;
static int   sess_count = 0;
static uint32_t last_list_ms = 0;

static lv_obj_t* page = NULL;
static lv_obj_t* header = NULL;     // title, or "‹ All · title" in the large view
static lv_obj_t* body = NULL;       // scroll container, rebuilt per view
static lv_obj_t* note = NULL;
static bool      shown = false;
static int       screen_w = 320, screen_h = 240;

static char      open_sid[SID_LEN] = "";   // the session shown large ("" = blocks)
static bool      user_chose_list = false;  // "‹ All" pressed: don't jump back into a lone session

// Every sprite on screen, for ticking and freeing.
#define MAX_SPRITES (MAX_COL_SESS * MAX_AGENTS)
static MiniClawd* sprites = NULL;
static int       sprite_count = 0;
static char      built_sig[512] = "";      // what the body was built from

// ─── States ──────────────────────────────────────────────────────────────────
struct StateInfo { const char* state; const char* anim; const char* words; bool animate; };
static const StateInfo STATES[] = {
    { "thinking",    "cloud",         "Thinking",       true  },
    { "working",     "laptop",        "Working",        true  },
    { "reading",     "magnifier",     "Reading",        true  },
    { "searching",   "magnifier",     "On the web",     true  },
    { "moving",      "crab walking",  "Moving files",   true  },
    { "talking",     "pointing",      "Talking",        true  },
    { "arguing",     "waving",        "Arguing",        true  },
    { "celebrating", "jumping happy", "Done",           true  },
    { "error",       "lurking",       "Hit an error",   true  },
    { "sleeping",    "walking",       "Napping",        false },
    { "idle",        "walking",       "Idle",           false },
};
static const StateInfo* info_for(const char* st) {
    for (const auto& s : STATES) if (strcmp(s.state, st) == 0) return &s;
    return &STATES[sizeof(STATES) / sizeof(STATES[0]) - 1];
}

static bool ready(void) { return pend && sess && sprites; }

static const CSess* find(const char* sid) {
    for (int i = 0; i < sess_count; i++) if (strcmp(sess[i].sid, sid) == 0) return &sess[i];
    return NULL;
}

// ─── Building ────────────────────────────────────────────────────────────────
static void free_body(void) {
    for (int i = 0; i < sprite_count; i++) mini_clawd_free(&sprites[i]);
    sprite_count = 0;
    if (body) lv_obj_clean(body);
}

static MiniClawd* new_sprite(lv_obj_t* parent, int w, int h, int cell) {
    if (sprite_count >= MAX_SPRITES) return NULL;
    MiniClawd* m = &sprites[sprite_count++];
    mini_clawd_init(m, parent, w, h, cell);
    return m;
}

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* f, lv_color_t c, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, text);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

static lv_obj_t* panel(lv_obj_t* parent, int w, int h) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_EVENT_BUBBLE);    // drags scroll the body
    return p;
}

static void open_session(const char* sid);
static void show_blocks(void);

static void block_clicked(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < sess_count) open_session(sess[i].sid);
}

static void header_clicked(lv_event_t* e) {
    (void)e;
    if (!open_sid[0] || sess_count < 2) return;      // "‹ All" only means something with several
    user_chose_list = true;
    show_blocks();
}

static void build_blocks(void) {
    lv_label_set_text_fmt(header, "Agents  #d97757 %d#", sess_count);
    const int w = screen_w - 16;
    for (int i = 0; i < sess_count; i++) {
        const CSess* s = &sess[i];
        lv_obj_t* b = panel(body, w, BLOCK_H);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, block_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* t = label(b, &font_styrene_16, THEME_TEXT, s->title);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_size(t, w - 90, lv_font_get_line_height(&font_styrene_16));
        lv_obj_set_pos(t, 10, 5);
        lv_obj_t* n = label(b, &font_styrene_12, s->working ? THEME_ACCENT : THEME_DIM, "");
        lv_label_set_text_fmt(n, "%d agent%s", s->agent_count, s->agent_count == 1 ? "" : "s");
        lv_obj_align(n, LV_ALIGN_TOP_RIGHT, -10, 8);
        // The picture: every agent's Clawd, side by side.
        const int fit = (w - 16) / ICON_W;
        for (int a = 0; a < s->agent_count && a < fit; a++) {
            const StateInfo* si = info_for(s->agents[a].state);
            MiniClawd* m = new_sprite(b, ICON_W, ICON_H, 1);
            if (!m) break;
            lv_obj_set_pos(m->img, 8 + a * ICON_W, BLOCK_H - ICON_H - 6);
            mini_clawd_set(m, si->anim, si->animate);
        }
    }
}

static void build_session(const CSess* s) {
    if (sess_count > 1) lv_label_set_text_fmt(header, "#d97757 < All#  %s", s->title);
    else                lv_label_set_text(header, s->title);
    for (int a = 0; a < s->agent_count; a++) {
        const StateInfo* si = info_for(s->agents[a].state);
        lv_obj_t* c = panel(body, CARD_W, CARD_H);
        MiniClawd* m = new_sprite(c, CARD_SPRITE_W, CARD_SPRITE_H, 2);
        if (m) {
            lv_obj_set_pos(m->img, (CARD_W - CARD_SPRITE_W) / 2, 4);
            mini_clawd_set(m, si->anim, si->animate);
        }
        lv_obj_t* n = label(c, &font_styrene_14, THEME_TEXT, s->agents[a].name);
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_size(n, CARD_W - 12, lv_font_get_line_height(&font_styrene_14));
        lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(n, 6, CARD_SPRITE_H + 6);
        lv_obj_t* act = label(c, &font_styrene_12, THEME_ACCENT, si->words);
        lv_obj_set_width(act, CARD_W - 12);
        lv_obj_set_style_text_align(act, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(act, 6, CARD_SPRITE_H + 24);
    }
}

// A signature of what's on screen: rebuild only when it changes.
static void signature(char* out, size_t n) {
    size_t k = 0;
    out[0] = 0;
    auto put = [&](const char* s) { while (*s && k + 1 < n) out[k++] = *s++; out[k] = 0; };
    put(open_sid[0] ? open_sid : "-");
    put("|");
    for (int i = 0; i < sess_count; i++) {
        const CSess* s = &sess[i];
        if (open_sid[0] && strcmp(s->sid, open_sid) != 0) continue;
        put(s->sid); put(s->working ? "+" : "-"); put(s->title); put(":");
        if (!open_sid[0] && sess_count > 0) { char c[4]; snprintf(c, sizeof(c), "%d", sess_count); put(c); }
        for (int a = 0; a < s->agent_count; a++) { put(s->agents[a].name); put("="); put(s->agents[a].state); put(","); }
        put(";");
    }
}

static void update_note(void) {
    const uint32_t now = millis();
    const char* msg = NULL;
    if (!last_list_ms || now - last_list_ms > STALE_MS) msg = "Waiting for the Mac bridge\n(USB cable)";
    else if (sess_count == 0) msg = "No agents at work";
    // The note replaces the content (a stale list would look live).
    if (msg) {
        lv_label_set_text(note, msg);
        lv_obj_clear_flag(note, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(note, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    }
}

static void rebuild(bool force) {
    if (!page) return;
    char sig[sizeof(built_sig)];
    signature(sig, sizeof(sig));
    if (!force && strcmp(sig, built_sig) == 0) return;
    strncpy(built_sig, sig, sizeof(built_sig) - 1);
    free_body();
    const CSess* s = open_sid[0] ? find(open_sid) : NULL;
    if (s) build_session(s);
    else   build_blocks();
    lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);
    update_note();
}

static void open_session(const char* sid) {
    strncpy(open_sid, sid, SID_LEN - 1);
    open_sid[SID_LEN - 1] = 0;
    user_chose_list = false;
    rebuild(true);
}

void colony_open(const char* sid) { if (ready() && find(sid)) open_session(sid); }

static void show_blocks(void) {
    open_sid[0] = 0;
    rebuild(true);
}

// The open session stays until its agents stop working (or it goes away).
// With a single working session and no "‹ All" press, it opens by itself.
static void choose_view(void) {
    if (open_sid[0]) {
        const CSess* s = find(open_sid);
        if (!s || !s->working) open_sid[0] = 0;
    }
    if (!open_sid[0] && sess_count == 1 && sess[0].working && !user_chose_list) {
        strncpy(open_sid, sess[0].sid, SID_LEN - 1);
    }
    if (sess_count != 1) user_chose_list = false;
}

// ─── Page ────────────────────────────────────────────────────────────────────
static void* ps_calloc(size_t n) {
    void* p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    return p ? p : heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void colony_view_init(void) {
    pend = (CSess*)ps_calloc(sizeof(CSess) * MAX_COL_SESS);
    sess = (CSess*)ps_calloc(sizeof(CSess) * MAX_COL_SESS);
    sprites = (MiniClawd*)ps_calloc(sizeof(MiniClawd) * MAX_SPRITES);
    if (!pend || !sess || !sprites) { Serial.println("colony: no memory"); return; }
    const BoardCaps& caps = board_caps();
    screen_w = caps.width;
    screen_h = caps.height;

    page = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page, screen_w, screen_h);
    lv_obj_set_pos(page, -screen_w, 0);
    lv_obj_set_style_bg_color(page, THEME_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);

    header = lv_label_create(page);
    lv_obj_set_style_text_font(header, &font_styrene_20, 0);
    lv_obj_set_style_text_color(header, THEME_TEXT, 0);
    lv_label_set_recolor(header, true);
    lv_label_set_long_mode(header, LV_LABEL_LONG_DOT);
    lv_obj_set_size(header, screen_w - 20, lv_font_get_line_height(&font_styrene_20));
    lv_obj_set_pos(header, 10, 4);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(header, 8);
    lv_obj_add_event_cb(header, header_clicked, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(header, "Agents");

    body = lv_obj_create(page);
    lv_obj_set_size(body, screen_w, screen_h - HEADER_H);
    lv_obj_set_pos(body, 0, HEADER_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_hor(body, 8, 0);
    lv_obj_set_style_pad_ver(body, 4, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_set_style_pad_column(body, 8, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_ACTIVE);

    note = lv_label_create(page);
    lv_obj_set_style_text_font(note, &font_styrene_16, 0);
    lv_obj_set_style_text_color(note, THEME_DIM, 0);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(note, LV_ALIGN_CENTER, 0, 10);
    update_note();
    visitor_attach(page, -1, screen_w, screen_h);   // ...and here when he runs off the left edge
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

void colony_view_show(void) {
    if (!page || shown) return;
    shown = true;
    choose_view();
    rebuild(false);
    update_note();
    lv_obj_move_foreground(page);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    slide(-screen_w, 0, false);
}

void colony_view_hide(void) {
    if (!page || !shown) return;
    shown = false;
    slide(lv_obj_get_x(page), -screen_w, true);
}

bool colony_view_visible(void) { return shown; }

void colony_view_tick(void) {
    if (!page) return;
    if (shown && strcmp(director_beat_name(), "attention") == 0) colony_view_hide();
    if (!shown) return;
    const uint32_t now = millis();
    static uint32_t note_at = 0;
    if (now - note_at > 1000) { note_at = now; update_note(); }
    for (int i = 0; i < sprite_count; i++) mini_clawd_tick(&sprites[i], now);
}

// ─── Data ────────────────────────────────────────────────────────────────────
void colony_clear(void) { pend_count = 0; }

void colony_add_session(const char* sid, bool working, const char* title) {
    if (!ready() || pend_count >= MAX_COL_SESS) return;
    CSess* s = &pend[pend_count++];
    memset(s, 0, sizeof(*s));
    strncpy(s->sid, sid, SID_LEN - 1);
    s->working = working;
    strncpy(s->title, title && *title ? title : "Session", sizeof(s->title) - 1);
}

void colony_add_agent(const char* sid, const char* state, const char* name) {
    if (!ready()) return;
    for (int i = pend_count - 1; i >= 0; i--) {
        CSess* s = &pend[i];
        if (strcmp(s->sid, sid) != 0) continue;
        if (s->agent_count >= MAX_AGENTS) return;
        Agent* a = &s->agents[s->agent_count++];
        strncpy(a->state, state, sizeof(a->state) - 1);
        strncpy(a->name, name && *name ? name : "Agent", NAME_LEN - 1);
        return;
    }
}

void colony_commit(void) {
    if (!ready()) return;
    memcpy(sess, pend, sizeof(CSess) * pend_count);
    sess_count = pend_count;
    last_list_ms = millis();
    if (!last_list_ms) last_list_ms = 1;
    choose_view();
    rebuild(false);
    if (page) update_note();
}
