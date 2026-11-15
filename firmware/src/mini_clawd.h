#pragma once
#include <stdint.h>
#include <lvgl.h>
#include "clawd_anim.h"

// A small animated Clawd in a fixed slot, for the sessions and colony pages:
// one official scene scaled by a whole cell size, feet on the slot's bottom
// edge, centered, transparent around the art.
struct MiniClawd {
    lv_obj_t*        img;
    lv_image_dsc_t   dsc;
    uint8_t*         buf;      // planar RGB565A8, w x h
    int              w, h, cell;
    const ClawdAnim* anim;
    bool             animate;
    uint16_t         frame;
    uint32_t         frame_at;
};

// Create the slot (w x h px) on parent; cell is the preferred scale, shrunk
// per scene when the art would not fit.
void mini_clawd_init(MiniClawd* m, lv_obj_t* parent, int w, int h, int cell);
// Show a scene by name (NULL keeps the current one). animate = false holds
// frame 0 (the idle pose for the core scenes).
void mini_clawd_set(MiniClawd* m, const char* anim, bool animate);
// Advance the animation by its frame holds; call while the slot is visible.
void mini_clawd_tick(MiniClawd* m, uint32_t now);
void mini_clawd_free(MiniClawd* m);   // before deleting the parent
