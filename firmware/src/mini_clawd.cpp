#include "mini_clawd.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

static void render(MiniClawd* m) {
    if (!m->buf || !m->anim) return;
    const ClawdAnim* a = m->anim;
    uint16_t* color = (uint16_t*)m->buf;
    uint8_t*  alpha = m->buf + m->w * m->h * 2;
    memset(color, 0, m->w * m->h * 2);
    memset(alpha, 0, m->w * m->h);
    int c = m->cell;
    while (c > 1 && (a->w * c > m->w || a->h * c > m->h)) c--;
    const int ox = (m->w - a->w * c) / 2, oy = m->h - a->h * c;
    const uint8_t* src = clawd_frame(a, m->frame);
    for (int gy = 0; gy < a->h; gy++) {
        for (int gx = 0; gx < a->w; gx++) {
            const uint8_t code = src[gy * a->w + (m->mirror ? a->w - 1 - gx : gx)];
            if (!code || code >= CLAWD_PALETTE_SIZE) continue;
            const uint16_t col = a->palette[code];
            for (int dy = 0; dy < c; dy++) {
                const int y = oy + gy * c + dy;
                if (y < 0 || y >= m->h) continue;
                for (int dx = 0; dx < c; dx++) {
                    const int x = ox + gx * c + dx;
                    if (x < 0 || x >= m->w) continue;
                    color[y * m->w + x] = col;
                    alpha[y * m->w + x] = 255;
                }
            }
        }
    }
    lv_image_set_src(m->img, &m->dsc);
    lv_obj_invalidate(m->img);
}

void mini_clawd_init(MiniClawd* m, lv_obj_t* parent, int w, int h, int cell) {
    memset(m, 0, sizeof(*m));
    m->w = w; m->h = h; m->cell = cell;
    const size_t bytes = (size_t)w * h * 3;
    m->buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!m->buf) m->buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    m->dsc.header.w = w;
    m->dsc.header.h = h;
    m->dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    m->dsc.header.stride = w * 2;
    m->dsc.data = m->buf;
    m->dsc.data_size = bytes;
    m->img = lv_image_create(parent);
    lv_obj_set_size(m->img, w, h);
    lv_obj_add_flag(m->img, LV_OBJ_FLAG_EVENT_BUBBLE);
}

void mini_clawd_set(MiniClawd* m, const char* anim, bool animate) {
    const ClawdAnim* a = anim ? clawd_anim(anim) : m->anim;
    if (a == m->anim && animate == m->animate) return;
    m->anim = a;
    m->animate = animate;
    m->frame = 0;
    m->frame_at = millis();
    render(m);
}

void mini_clawd_tick(MiniClawd* m, uint32_t now) {
    if (!m->animate || !m->anim) return;
    const uint16_t hold = m->anim->holds[m->frame] ? m->anim->holds[m->frame] : 100;
    if (now - m->frame_at < hold) return;
    m->frame_at = now;
    uint16_t next = m->frame + 1;
    if (next > m->anim->loop_end || next >= m->anim->frame_count) next = m->anim->loop_start;
    m->frame = next;
    render(m);
}

void mini_clawd_set_mirror(MiniClawd* m, bool mirror) {
    if (m->mirror == mirror) return;
    m->mirror = mirror;
    render(m);
}

void mini_clawd_free(MiniClawd* m) {
    if (m->buf) heap_caps_free(m->buf);
    m->buf = NULL;
    m->img = NULL;
    m->anim = NULL;
}
