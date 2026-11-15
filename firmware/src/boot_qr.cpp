#include "boot_qr.h"
#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include <string.h>
#include "theme.h"
#include "idle.h"
#include "clawd_director.h"
#include "hal/board_caps.h"

#ifndef CLAWD_BOOT_QR
#define CLAWD_BOOT_QR 1
#endif
#ifndef BOOT_QR_SECONDS
#define BOOT_QR_SECONDS 30
#endif
#ifndef BOOT_QR_AFTER_SLEEP_MIN
#define BOOT_QR_AFTER_SLEEP_MIN 360        // asleep this long (6 h) counts as "turned on after a long time"
#endif

#if CLAWD_BOOT_QR && LV_USE_QRCODE && defined(CLAWD_REPO_URL)

LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_12);

static lv_obj_t* card = NULL;
static uint32_t  shown_at = 0;
static bool      enabled = true;
static bool      was_asleep = false;
static uint32_t  asleep_since = 0;

bool boot_qr_available(void) { return true; }
bool boot_qr_visible(void) { return card != NULL; }
bool boot_qr_enabled(void) { return enabled; }

void boot_qr_set_enabled(bool on) {
    enabled = on;
    Preferences prefs;
    prefs.begin("clawdmeter", false);
    prefs.putUChar("boot_qr", on ? 1 : 0);
    prefs.end();
    if (!on) boot_qr_dismiss();
}

void boot_qr_dismiss(void) {
    if (!card) return;
    lv_obj_delete(card);
    card = NULL;
}

void boot_qr_show(void) {
    if (card) { shown_at = millis(); return; }
    const BoardCaps& caps = board_caps();
    const int w = caps.width, h = caps.height;
    const int side = (w < h ? w : h);
    const int qr = side * 5 / 8;                     // 150 px on a 240 px panel

    // On the screen itself (not lv_layer_top), so the `screenshot` command captures it.
    card = lv_obj_create(lv_screen_active());
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, 0, 0);
    lv_obj_set_style_bg_color(card, THEME_BG, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(card);

    lv_obj_t* title = lv_label_create(card);
    lv_obj_set_style_text_font(title, &font_styrene_16, 0);
    lv_obj_set_style_text_color(title, THEME_TEXT, 0);
    lv_label_set_text(title, "Set me up: scan with your phone");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t* code = lv_qrcode_create(card);
    lv_qrcode_set_size(code, qr);
    lv_qrcode_set_dark_color(code, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(code, lv_color_hex(0xffffff));
    lv_qrcode_set_quiet_zone(code, true);
    lv_qrcode_update(code, CLAWD_REPO_URL, strlen(CLAWD_REPO_URL));
    lv_obj_align(code, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t* url = lv_label_create(card);
    lv_obj_set_style_text_font(url, &font_styrene_12, 0);
    lv_obj_set_style_text_color(url, THEME_ACCENT, 0);
    const char* shown = strstr(CLAWD_REPO_URL, "://");
    lv_label_set_text(url, shown ? shown + 3 : CLAWD_REPO_URL);
    lv_obj_align(url, LV_ALIGN_TOP_MID, 0, 30 + qr + 6);

    lv_obj_t* hint = lv_label_create(card);
    lv_obj_set_style_text_font(hint, &font_styrene_12, 0);
    lv_obj_set_style_text_color(hint, THEME_DIM, 0);
    lv_label_set_text(hint, caps.has_touch ? "Tap to skip. Setup guide and source inside."
                                           : "Setup guide and source inside.");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 30 + qr + 24);

    shown_at = millis();
    Serial.printf("Boot QR: showing %s for %d s\n", CLAWD_REPO_URL, BOOT_QR_SECONDS);
}

void boot_qr_init(void) {
    Preferences prefs;
    prefs.begin("clawdmeter", true);
    enabled = prefs.getUChar("boot_qr", 1) != 0;
    prefs.end();
    if (enabled) boot_qr_show();
}

void boot_qr_tick(void) {
    const uint32_t now = millis();
    // Woken after a long sleep: show it again, as after a power-on.
    const bool asleep = idle_is_asleep();
    if (asleep && !was_asleep) asleep_since = now;
    if (!asleep && was_asleep && enabled &&
        now - asleep_since >= (uint32_t)BOOT_QR_AFTER_SLEEP_MIN * 60000UL) boot_qr_show();
    was_asleep = asleep;

    if (!card) return;
    // An alert needs the screen; otherwise it leaves when its time is up.
    if (now - shown_at >= (uint32_t)BOOT_QR_SECONDS * 1000UL ||
        strcmp(director_beat_name(), "attention") == 0) boot_qr_dismiss();
}

#else   // compiled out

bool boot_qr_available(void) { return false; }
void boot_qr_init(void) {}
void boot_qr_tick(void) {}
bool boot_qr_visible(void) { return false; }
void boot_qr_dismiss(void) {}
void boot_qr_show(void) {}
bool boot_qr_enabled(void) { return false; }
void boot_qr_set_enabled(bool) {}

#endif
