/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * The starting screen (splash.h): NK's (BrowserPS5's poc/neko/shell/
 * native_ui.c, native_splash_draw) with the title's font and " WK".
 * 2026-11-09 19:4x: in the colours of this app's icon and background, not
 * the theme's: a blue-black ground, the
 * globe's wires electric blue, the orbit and the comet neon magenta going
 * to blue at the tail, "WK" from magenta to blue.
 * 2026-11-10 22:50 (BrowserPS5 RN): with /app0/splash-rn.raw (RN's home
 * background, 1920x1080 RGB, only in RN's title) the screen is that picture,
 * its RN logo on the right, and on the left "BrowserPS5 RN" and the line in
 * the logo's neon red and blue. v1's title
 * has no such file and keeps the comet.
 */
#include "splash.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plutovg.h"

#ifndef FONT_PATH  // (a test on the PC gives its own)
#define FONT_PATH "/app0/fonts/DejaVuSans-Bold.ttf"
#endif
#define GROUND 0x07061au
#define BLUE 0x2f6dffu  // the icon's globe and the orbit's tail
#define PINK 0xff2ed1u  // the orbit's neon
#define PART_W 720  // the part that moves, in the middle
#define PART_H 440
#ifndef RN_PICTURE
#define RN_PICTURE "/app0/splash-rn.raw"
#endif
#define RN_RED 0xff2e4au  // the RN logo's neon
#define RN_X 160  // RN: the part on the left, beside the logo
#define RN_Y 300

static uint32_t *screen;
static uint32_t *part;
static int screen_w, screen_h;
static plutovg_font_face_t *font;
static uint32_t *ground;  // RN: its background picture (screen-sized); 0: the plain ground

static void argb(plutovg_canvas_t *c, uint32_t v, float alpha)
{
    plutovg_canvas_set_rgba(c, ((v >> 16) & 0xff) / 255.f, ((v >> 8) & 0xff) / 255.f,
                            (v & 0xff) / 255.f, ((v >> 24) & 0xff) / 255.f * alpha);
}

static void gradient(plutovg_canvas_t *c, float x1, float y1, float x2, float y2, uint32_t a,
                     uint32_t b)
{
    plutovg_gradient_stop_t s[2];
    const uint32_t v[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        s[i].offset = (float)i;
        s[i].color.r = ((v[i] >> 16) & 0xff) / 255.f;
        s[i].color.g = ((v[i] >> 8) & 0xff) / 255.f;
        s[i].color.b = (v[i] & 0xff) / 255.f;
        s[i].color.a = ((v[i] >> 24) & 0xff) / 255.f;
    }
    plutovg_canvas_set_linear_gradient(c, x1, y1, x2, y2, PLUTOVG_SPREAD_METHOD_PAD, s, 2, 0);
}

static uint32_t lighter(uint32_t v, float f)
{
    uint32_t out = v & 0xff000000u;
    for (int s = 0; s < 24; s += 8) {
        const float c = (float)((v >> s) & 0xff);
        out |= (uint32_t)(c + (255 - c) * f + 0.5f) << s;
    }
    return out;
}

// a to b, f of the way (0..1), with a's alpha
static uint32_t mix(uint32_t a, uint32_t b, float f)
{
    uint32_t out = a & 0xff000000u;
    for (int s = 0; s < 24; s += 8) {
        const float x = (float)((a >> s) & 0xff), y = (float)((b >> s) & 0xff);
        out |= (uint32_t)(x + (y - x) * f + 0.5f) << s;
    }
    return out;
}

static void dot(plutovg_canvas_t *c, float x, float y, float r, uint32_t v, float alpha)
{
    argb(c, v, alpha);
    plutovg_canvas_arc(c, x, y, r, 0, 6.2831853f, false);
    plutovg_canvas_fill(c);
}

static float text_w(float size, const char *s)
{
    return font ? plutovg_font_face_text_extents(font, size, s, (int)strlen(s),
                                                 PLUTOVG_TEXT_ENCODING_UTF8, 0)
                : 0;
}

static void text(plutovg_canvas_t *c, float size, float x, float y, uint32_t v, const char *s)
{
    if (!font)
        return;
    argb(c, v, 1);
    plutovg_canvas_set_font(c, font, size);
    plutovg_canvas_fill_text(c, s, (int)strlen(s), PLUTOVG_TEXT_ENCODING_UTF8, x, y);
}

static void draw(uint32_t *pixels, int w, int h, double t, double progress)
{
    plutovg_surface_t *s = plutovg_surface_create_for_data((unsigned char *)pixels, w, h, w * 4);
    plutovg_canvas_t *c = s ? plutovg_canvas_create(s) : 0;
    if (!c) {
        plutovg_surface_destroy(s);
        return;
    }
    const float PI2 = 6.2831853f, cx = (float)w / 2, cy = 150, r = 58;
    const uint32_t P = 0xff000000u | PINK, B = 0xff000000u | BLUE, G = 0xff000000u | GROUND,
                   WHITE = 0xffffffffu;
    argb(c, G, 1);
    plutovg_canvas_rect(c, 0, 0, (float)w, (float)h);
    plutovg_canvas_fill(c);
    {  // a violet haze behind the globe, as the background's (gone before the edges)
        plutovg_gradient_stop_t stops[2] = {{0, {0.30f, 0.10f, 0.55f, 0.30f}}, {1, {0.30f, 0.10f, 0.55f, 0}}};
        plutovg_canvas_set_radial_gradient(c, cx, cy, 145, cx, cy, 0, PLUTOVG_SPREAD_METHOD_PAD, stops, 2, 0);
        plutovg_canvas_rect(c, 0, 0, (float)w, (float)h);
        plutovg_canvas_fill(c);
    }

    /* As the app's icon: a wireframe globe with a tilted orbit round it; the
     * comet runs along the orbit, behind the globe on the top half (dimmer,
     * smaller) and in front of it on the bottom half. */
    const float oa = 165, ob = 50, tilt = -0.26f, ct = cosf(tilt), st = sinf(tilt);
#define ORBIT_X(q) (cx + oa * cosf(q) * ct - ob * sinf(q) * st)
#define ORBIT_Y(q) (cy + oa * cosf(q) * st + ob * sinf(q) * ct)
    const float head = (float)fmod(t * PI2 * 0.6 + 0.35 * sin(t * 2.1), PI2), tail = 2.1f;
    static const float widths[] = {28, 17, 9, 4.5f}, alphas[] = {0.05f, 0.11f, 0.32f, 1.0f};
    plutovg_canvas_set_line_cap(c, PLUTOVG_LINE_CAP_BUTT);
    for (int front = 0; front < 2; front++) {
        for (int glow = 0; glow < 2; glow++) {  // the orbit, a neon line (a wide faint one under it)
            argb(c, P, (glow ? 0.42f : 0.07f) * (front ? 1 : 0.7f));
            plutovg_canvas_set_line_width(c, glow ? 2.5f : 9);
            for (int k = 0; k <= 48; k++) {
                const float q = (front ? 0 : PI2 / 2) + PI2 / 2 * (float)k / 48;
                if (k)
                    plutovg_canvas_line_to(c, ORBIT_X(q), ORBIT_Y(q));
                else
                    plutovg_canvas_move_to(c, ORBIT_X(q), ORBIT_Y(q));
            }
            plutovg_canvas_stroke(c);
        }
        // the comet's pieces on this half; wide faint strokes under thin bright ones make the glow
        for (int g = 0; g < 4; g++) {
            for (int k = 0; k < 18; k++) {
                const float f = 1.0f - (float)k / 18, a1 = head - tail * (float)k / 18,
                            a0 = head - tail * (float)(k + 1) / 18;
                const float depth = sinf((a0 + a1) / 2);  // > 0: in front
                if ((depth > 0) != front)
                    continue;
                const float near = 0.78f + 0.22f * depth, dim = depth > 0 ? 1 : 0.6f;
                const uint32_t A = mix(B, P, f);  // magenta at the head, blue at the tail
                argb(c, g == 3 ? lighter(A, 0.45f * f) : A, alphas[g] * f * f * dim);
                plutovg_canvas_set_line_width(c, widths[g] * near);
                for (int p = 0; p <= 3; p++) {
                    const float q = a0 + (a1 + 0.012f - a0) * (float)p / 3;
                    if (p)
                        plutovg_canvas_line_to(c, ORBIT_X(q), ORBIT_Y(q));
                    else
                        plutovg_canvas_move_to(c, ORBIT_X(q), ORBIT_Y(q));
                }
                plutovg_canvas_stroke(c);
            }
        }
        if (!front) {  // the globe over the orbit's back half: the ground inside, thin lines
            argb(c, G, 0.88f);
            plutovg_canvas_arc(c, cx, cy, r, 0, PI2, false);
            plutovg_canvas_fill(c);
            argb(c, B, 0.12f);  // the globe's neon: a glow, then the line
            plutovg_canvas_set_line_width(c, 10);
            plutovg_canvas_arc(c, cx, cy, r, 0, PI2, false);
            plutovg_canvas_stroke(c);
            argb(c, lighter(B, 0.2f), 0.95f);
            plutovg_canvas_set_line_width(c, 2.4f);
            plutovg_canvas_arc(c, cx, cy, r, 0, PI2, false);
            plutovg_canvas_stroke(c);
            argb(c, B, 0.70f);
            plutovg_canvas_set_line_width(c, 1.6f);
            for (int m = 1; m <= 2; m++) {  // meridians
                plutovg_canvas_ellipse(c, cx, cy, r * (m == 1 ? 0.36f : 0.74f), r);
                plutovg_canvas_stroke(c);
            }
            plutovg_canvas_move_to(c, cx, cy - r);
            plutovg_canvas_line_to(c, cx, cy + r);
            for (int p = -1; p <= 1; p++) {  // parallels
                const float py = cy + r * 0.5f * (float)p, hw = sqrtf(r * r - (py - cy) * (py - cy));
                plutovg_canvas_move_to(c, cx - hw, py);
                plutovg_canvas_line_to(c, cx + hw, py);
            }
            plutovg_canvas_stroke(c);
        }
    }
    const float depth = sinf(head), near = 0.78f + 0.22f * depth, dim = depth > 0 ? 1 : 0.6f;
    const float hx = ORBIT_X(head), hy = ORBIT_Y(head);  // the head: a ball of light
#undef ORBIT_X
#undef ORBIT_Y
    dot(c, hx, hy, 20 * near, P, 0.09f * dim);
    dot(c, hx, hy, 12 * near, P, 0.22f * dim);
    dot(c, hx, hy, 7 * near, lighter(P, 0.4f), 0.75f * dim);
    dot(c, hx, hy, 3.5f * near, lighter(P, 0.85f), dim);

    // the name: "BrowserPS5" white, " WK" from magenta to blue
    const float size = 54, nw = text_w(size, "BrowserPS5"), kw = text_w(size, " WK");
    float ascent = 0, descent = 0;
    if (font)
        plutovg_font_face_get_metrics(font, size, &ascent, &descent, 0, 0);
    const float x = cx - (nw + kw) / 2, y = 290 + (ascent + descent) / 2;
    text(c, size, x, y, WHITE, "BrowserPS5");
    if (font) {
        gradient(c, x + nw, 0, x + nw + kw, 0, P, B);
        plutovg_canvas_set_font(c, font, size);
        plutovg_canvas_fill_text(c, " WK", 3, PLUTOVG_TEXT_ENCODING_UTF8, x + nw, y);
    }

    // the line under it, filling as the browser comes
    const float lw = 280, lx = cx - lw / 2, ly = 350,
                p = (float)(progress < 0 ? 0 : progress > 1 ? 1 : progress) * lw;
    argb(c, WHITE, 0.10f);
    plutovg_canvas_round_rect(c, lx, ly, lw, 4, 2, 2);
    plutovg_canvas_fill(c);
    if (p > 1) {
        gradient(c, lx, 0, lx + p, 0, B, P);
        plutovg_canvas_round_rect(c, lx, ly, p, 4, 2, 2);
        plutovg_canvas_fill(c);
        dot(c, lx + p, ly + 2, 9, P, 0.2f);
        dot(c, lx + p, ly + 2, 4, lighter(P, 0.7f), 0.9f);
    }
    plutovg_canvas_destroy(c);
    plutovg_surface_destroy(s);
}

// RN: the name, a word under it and the line, over the picture (part is
// the picture's piece at RN_X, RN_Y when this starts).
static void draw_rn(uint32_t *pixels, int w, int h, double t, double progress)
{
    plutovg_surface_t *s = plutovg_surface_create_for_data((unsigned char *)pixels, w, h, w * 4);
    plutovg_canvas_t *c = s ? plutovg_canvas_create(s) : 0;
    if (!c) {
        plutovg_surface_destroy(s);
        return;
    }
    const uint32_t R = 0xff000000u | RN_RED, B = 0xff000000u | BLUE, WHITE = 0xffffffffu;
    const float cx = (float)w / 2;
    {  // a dark haze under the words, so the circuits behind stay quiet
        plutovg_gradient_stop_t stops[2] = {{0, {0.0f, 0.0f, 0.02f, 0.62f}}, {1, {0.0f, 0.0f, 0.02f, 0}}};
        plutovg_canvas_set_radial_gradient(c, cx, 215, 340, cx, 215, 0, PLUTOVG_SPREAD_METHOD_PAD, stops, 2, 0);
        plutovg_canvas_rect(c, 0, 0, (float)w, (float)h);
        plutovg_canvas_fill(c);
    }
    // "BrowserPS5" white, " RN" from red to blue (as the logo's orbit)
    const float size = 68, nw = text_w(size, "BrowserPS5"), kw = text_w(size, " RN");
    float ascent = 0, descent = 0;
    if (font)
        plutovg_font_face_get_metrics(font, size, &ascent, &descent, 0, 0);
    const float x = cx - (nw + kw) / 2, y = 175 + (ascent + descent) / 2;
    text(c, size, x, y, WHITE, "BrowserPS5");
    if (font) {
        gradient(c, x + nw, 0, x + nw + kw, 0, R, B);
        plutovg_canvas_set_font(c, font, size);
        plutovg_canvas_fill_text(c, " RN", 3, PLUTOVG_TEXT_ENCODING_UTF8, x + nw, y);
    }
    // the line, filling as the browser comes; a light runs along what is left
    const float lw = 400, lx = cx - lw / 2, ly = 262,
                p = (float)(progress < 0 ? 0 : progress > 1 ? 1 : progress) * lw;
    argb(c, WHITE, 0.12f);
    plutovg_canvas_round_rect(c, lx, ly, lw, 5, 2.5f, 2.5f);
    plutovg_canvas_fill(c);
    const float run = (float)fmod(t * 0.7, 1.0), rx = lx + p + (lw - p) * run;
    if (lw - p > 12)
        dot(c, rx, ly + 2.5f, 6, B, 0.35f * sinf(run * 3.14159f));
    if (p > 1) {
        gradient(c, lx, 0, lx + p, 0, B, R);
        plutovg_canvas_round_rect(c, lx, ly, p, 5, 2.5f, 2.5f);
        plutovg_canvas_fill(c);
        const float pulse = 0.75f + 0.25f * sinf((float)t * 5);
        dot(c, lx + p, ly + 2.5f, 16, R, 0.14f * pulse);
        dot(c, lx + p, ly + 2.5f, 9, R, 0.28f * pulse);
        dot(c, lx + p, ly + 2.5f, 4.5f, lighter(R, 0.7f), 0.95f);
    }
    plutovg_canvas_destroy(c);
    plutovg_surface_destroy(s);
}

/* RN (2026-11-16): the two rings round the logo, as the picture draws them (ellipses fitted to its own pixels,
 * browser-rn/art/pic0.raw: centre, half axes, tilt), and a light running along each: round the blue ring once in
 * 4.2 s, round the red one the other way in 6.5 s and softer, while both rings breathe a little. Only the rings'
 * own pixels (and their glow) take the light, so where a letter or the globe is in front of a ring, it stays in
 * front. Each picture writes those pixels again from the picture: nothing builds up. */
typedef struct {
    float cx, cy, a, b, tilt;
} ring_shape;
static const ring_shape RINGS[2] = {
    {1341.1f, 525.4f, 352.3f, 158.7f, 2.62603f},  // blue (150.46 degrees)
    {1342.2f, 531.4f, 322.7f, 137.7f, 2.64104f},  // red (151.32 degrees)
};
#define RING_BINS 1024  // places along a ring
typedef struct {
    uint32_t at;     // in the screen
    uint16_t bin;    // where along its ring
    uint8_t ring;    // 0 blue, 1 red
    uint8_t weight;  // how much of the light it takes
} ring_px;
static ring_px *ring_pxs;
static size_t ring_n;

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

static void rings_find(void)
{
    const int x0 = 975, x1 = 1710, y0 = 250, y1 = 800;  // round the blue ring and its glow
    size_t cap = 1 << 16;
    ring_n = 0;
    ring_pxs = malloc(cap * sizeof *ring_pxs);
    if (!ring_pxs || screen_w != 1920 || screen_h != 1080)
        return;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const uint32_t v = ground[(size_t)y * screen_w + x];
            const float r = (float)((v >> 16) & 0xff), g = (float)((v >> 8) & 0xff), b = (float)(v & 0xff);
            for (int k = 0; k < 2; k++) {
                const float hue = k ? clamp01((r - fmaxf(g, b) - 25) / 110) * clamp01((r - 100) / 120)
                                    : clamp01((b - fmaxf(r, g) - 25) / 110) * clamp01((b - 80) / 120);
                if (hue < 0.02f)
                    continue;
                if (k && hypotf((float)x - 1337, (float)y - 350) < 150)
                    break;  // the globe's own red lines are not the ring
                const ring_shape *s = &RINGS[k];
                const float dx = (float)x - s->cx, dy = (float)y - s->cy, ct = cosf(s->tilt), st = sinf(s->tilt);
                const float u = (dx * ct + dy * st) / s->a, w = (-dx * st + dy * ct) / s->b;
                const float off = (sqrtf(u * u + w * w) - 1) / 0.085f, weight = hue * expf(-off * off);
                if (weight < 0.03f)
                    break;
                if (ring_n == cap) {
                    ring_px *more = realloc(ring_pxs, cap * 2 * sizeof *ring_pxs);
                    if (!more)
                        return;
                    ring_pxs = more;
                    cap *= 2;
                }
                const float phi = atan2f(w, u);  // -pi..pi
                ring_pxs[ring_n++] = (ring_px){(uint32_t)((size_t)y * screen_w + x),
                                               (uint16_t)((int)((phi + 3.14159265f) / 6.2831853f * RING_BINS) &
                                                          (RING_BINS - 1)),
                                               (uint8_t)k, (uint8_t)(weight * 255 + 0.5f)};
                break;
            }
        }
    }
}

static void rings_light(double t)
{
    if (!ring_n)
        return;
    static float light[2][RING_BINS];
    const float PI2 = 6.2831853f;
    const float in = clamp01((float)t / 1.2f), ease = in * in * (3 - 2 * in);  // the light comes in softly
    const float breath = 0.07f + 0.06f * sinf((float)t * PI2 / 3.6f);
    static const float period[2] = {4.2f, 6.5f}, way[2] = {1, -1}, strength[2] = {1.15f, 0.6f};
    for (int k = 0; k < 2; k++) {
        const float head = (float)fmod(t / period[k], 1.0) * PI2 * way[k] + (k ? 3.14159f : 0);
        for (int i = 0; i < RING_BINS; i++) {
            const float phi = ((float)i + 0.5f) / RING_BINS * PI2 - 3.14159265f;
            float behind = (float)fmod((head - phi) * way[k], PI2);  // along the way it goes, behind the head
            if (behind < 0)
                behind += PI2;
            const float ahead = PI2 - behind;
            const float glint = expf(-(behind / 0.22f) * (behind / 0.22f)) + 0.3f * expf(-behind / 0.7f) +
                                expf(-(ahead / 0.08f) * (ahead / 0.08f));
            light[k][i] = ease * strength[k] * glint + breath;
        }
    }
    static const float tint[2][3] = {{110, 170, 255}, {255, 105, 135}};  // the light's colour on each ring
    for (size_t i = 0; i < ring_n; i++) {
        const ring_px *p = &ring_pxs[i];
        const uint32_t v = ground[p->at];
        const float f = light[p->ring][p->bin] * (float)p->weight / 255;
        uint32_t out = 0xff000000u;
        for (int c = 0; c < 3; c++) {
            const float now = (float)((v >> (16 - 8 * c)) & 0xff) + f * tint[p->ring][c];
            out |= (uint32_t)(now > 255 ? 255 : now) << (16 - 8 * c);
        }
        screen[p->at] = out;
    }
}

// RN's picture into ground (ARGB), when it is there and of the screen's size.
static void ground_load(int width, int height)
{
    const size_t n = (size_t)width * height;
    FILE *f = fopen(RN_PICTURE, "rb");
    if (!f)
        return;
    unsigned char *rgb = malloc(n * 3);
    ground = malloc(n * 4);
    if (rgb && ground && fread(rgb, 1, n * 3, f) == n * 3) {
        for (size_t i = 0; i < n; i++)
            ground[i] = 0xff000000u | (uint32_t)rgb[i * 3] << 16 | (uint32_t)rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
    } else {
        free(ground);
        ground = 0;
    }
    free(rgb);
    fclose(f);
}

int splash_open(int width, int height)
{
    if (width < PART_W || height < PART_H)
        return -1;
    font = plutovg_font_face_load_from_file(FONT_PATH, 0);
    screen = malloc((size_t)width * height * 4);
    part = malloc((size_t)PART_W * PART_H * 4);
    if (!screen || !part) {
        splash_close();
        return -1;
    }
    screen_w = width;
    screen_h = height;
    ground_load(width, height);
    if (ground && (RN_X + PART_W > width || RN_Y + PART_H > height)) {
        free(ground);
        ground = 0;
    }
    if (ground) {
        memcpy(screen, ground, (size_t)width * height * 4);
        rings_find();
    } else
        for (size_t i = 0; i < (size_t)width * height; i++)
            screen[i] = 0xff000000u | GROUND;
    return 0;
}

const uint32_t *splash_frame(double t, double progress)
{
    if (!screen)
        return 0;
    if (ground) {  // RN: the picture's piece, then the words and the line over it
        for (int y = 0; y < PART_H; y++)
            memcpy(part + (size_t)y * PART_W, ground + (size_t)(RN_Y + y) * screen_w + RN_X, PART_W * 4);
        draw_rn(part, PART_W, PART_H, t, progress);
        rings_light(t);
    } else {
        draw(part, PART_W, PART_H, t, progress);
    }
    const int x0 = ground ? RN_X : (screen_w - PART_W) / 2, y0 = ground ? RN_Y : (screen_h - PART_H) / 2;
    for (int y = 0; y < PART_H; y++)
        memcpy(screen + (size_t)(y0 + y) * screen_w + x0, part + (size_t)y * PART_W, PART_W * 4);
    return screen;
}

void splash_close(void)
{
    free(screen);
    free(part);
    free(ground);
    free(ring_pxs);
    screen = part = ground = 0;
    ring_pxs = 0;
    ring_n = 0;
    if (font)
        plutovg_font_face_destroy(font);
    font = 0;
}
