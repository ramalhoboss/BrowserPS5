/* Copyright (C) 2026 BrowserPS5 contributors

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

// The cursors and the D-pad's ring (overlay.h). Each shape is a signed
// distance field (negative inside, in pixels): its edge is smooth for free
// (coverage = 0.5 - distance), the outline is a band of distances and the
// shadow is the same field moved down and softened.

#include "overlay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// The shapes are drawn on a grid of "points"; a point is SCALE pixels.
#define SCALE      1.3f
#define MARGIN     6     // pixels around the shape, for the shadow
#define OUTLINE    1.5f  // pixels
#define SHADOW_DX  1.2f
#define SHADOW_DY  2.2f

static float
clampf(float v, float lo, float hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static float
smooth(float edge0, float edge1, float x) {
  float t = clampf((x - edge0) / (edge1 - edge0), 0, 1);
  return t * t * (3 - 2 * t);
}

// Inigo Quilez's signed distance to a polygon.
static float
sd_polygon(const float (*v)[2], int n, float px, float py) {
  float dx = px - v[0][0], dy = py - v[0][1];
  float d = dx * dx + dy * dy;
  float s = 1;

  for(int i = 0, j = n - 1; i < n; j = i, i++) {
    float ex = v[j][0] - v[i][0], ey = v[j][1] - v[i][1];
    float wx = px - v[i][0], wy = py - v[i][1];
    float t = clampf((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1);
    float bx = wx - ex * t, by = wy - ey * t;
    float b = bx * bx + by * by;
    if(b < d) {
      d = b;
    }
    int c0 = py >= v[i][1];
    int c1 = py < v[j][1];
    int c2 = ex * wy > ey * wx;
    if((c0 && c1 && c2) || (!c0 && !c1 && !c2)) {
      s = -s;
    }
  }
  return s * sqrtf(d);
}

static float
sd_capsule(float px, float py, float ax, float ay, float bx, float by,
           float r) {
  float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
  float h = clampf((pax * bax + pay * bay) / (bax * bax + bay * bay), 0, 1);
  float qx = pax - bax * h, qy = pay - bay * h;
  return sqrtf(qx * qx + qy * qy) - r;
}

static float
sd_round_box(float px, float py, float cx, float cy, float hx, float hy,
             float r) {
  float qx = fabsf(px - cx) - hx + r;
  float qy = fabsf(py - cy) - hy + r;
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy;
  return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

// The arrow, its tip at (0, 0), in points.
static const float arrow[][2] = {
  {0.0f, 0.0f}, {0.0f, 22.5f}, {5.2f, 17.6f}, {8.9f, 25.8f},
  {12.2f, 24.4f}, {8.6f, 16.4f}, {15.6f, 16.4f},
};

// The hand pointing up, its fingertip near (8, 0): one capsule a finger, a
// rounded box for the palm.
#define FINGERS 5
static const float fingers[FINGERS][5] = {  // ax, ay, bx, by, r
  {8.0f, 3.4f, 8.0f, 15.0f, 3.3f},    // index
  {12.7f, 11.6f, 12.7f, 17.0f, 3.1f}, // middle
  {16.9f, 12.8f, 16.9f, 18.0f, 3.0f}, // ring
  {20.7f, 14.6f, 20.7f, 19.0f, 2.7f}, // little
  {6.4f, 23.4f, 2.4f, 17.4f, 2.9f},   // thumb
};

// The distance to the shape at (px, py) points; *line is the distance to
// the lines between the fingers (the hand only), large when none.
static float
shape(cursor_kind_t kind, float px, float py, float* line) {
  *line = 1e9f;
  if(kind == CURSOR_ARROW) {
    return sd_polygon(arrow, sizeof(arrow) / sizeof(*arrow), px, py) - 0.6f;
  }
  float palm = sd_round_box(px, py, 14.4f, 22.6f, 8.5f, 6.4f, 4.4f);
  float d = palm;
  for(int i = 0; i < FINGERS; i++) {
    const float* f = fingers[i];
    float fd = sd_capsule(px, py, f[0], f[1], f[2], f[3], f[4]);
    if(fd < d) {
      d = fd;
    }
    // Where a finger meets its neighbour or the palm, above the palm's
    // middle: a line, as on a drawn hand.
    if(i < 4 && py < 19.5f && fabsf(fd) < *line) {
      *line = fabsf(fd);
    }
  }
  return d;
}

static uint32_t
mix(uint32_t a, uint32_t b, float t) {  // 0xRRGGBB, a at 0, b at 1
  uint32_t out = 0;
  for(int s = 0; s < 24; s += 8) {
    float ca = (float)((a >> s) & 0xff), cb = (float)((b >> s) & 0xff);
    out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
  }
  return out;
}

int
cursor_make(cursor_image_t* image, cursor_kind_t kind, uint32_t fill) {
  float extent_x = kind == CURSOR_ARROW ? 16.5f : 24.0f;
  float extent_y = kind == CURSOR_ARROW ? 26.5f : 29.5f;
  float origin_x = kind == CURSOR_ARROW ? 0.0f : 8.0f;   // the hot point
  float origin_y = kind == CURSOR_ARROW ? 0.0f : 0.1f;
  const uint32_t outline = 0x101418;

  image->width = (int)(extent_x * SCALE) + 2 * MARGIN;
  image->height = (int)(extent_y * SCALE) + 2 * MARGIN;
  image->hot_x = MARGIN + (int)(origin_x * SCALE);
  image->hot_y = MARGIN + (int)(origin_y * SCALE);
  image->pixels = (uint32_t*)calloc((size_t)image->width * image->height, 4);
  if(!image->pixels) {
    return -1;
  }
  for(int y = 0; y < image->height; y++) {
    for(int x = 0; x < image->width; x++) {
      float px = (x + 0.5f - MARGIN) / SCALE;
      float py = (y + 0.5f - MARGIN) / SCALE;
      float line;
      float d = shape(kind, px, py, &line) * SCALE;
      float unused;
      float ds = shape(kind, px - SHADOW_DX / SCALE, py - SHADOW_DY / SCALE,
                       &unused) * SCALE;

      float cover = clampf(0.5f - d, 0, 1);
      float inner = clampf(0.5f - (d + OUTLINE), 0, 1);
      uint32_t color = mix(outline, fill, inner);
      if(line < 1e8f) {
        float l = clampf(0.5f - (line * SCALE - 0.55f), 0, 1) * inner;
        color = mix(color, outline, l * 0.9f);
      }
      float shadow = 0.38f * smooth(3.5f, -1.0f, ds);
      float alpha = cover + shadow * (1 - cover);
      if(alpha <= 0.002f) {
        continue;
      }
      // Over the shadow (black): only the colour's share is left.
      uint32_t rgb = mix(0x000000, color, cover / alpha);
      image->pixels[(size_t)y * image->width + x] =
        ((uint32_t)(alpha * 255 + 0.5f) << 24) | rgb;
    }
  }
  return 0;
}

// The shadow: the picture's alpha, blurred (three box blurs, about a
// Gaussian) and moved down and right, under the picture.
#define BLUR_RADIUS 2

static void
box_blur(float* v, float* tmp, int w, int h) {
  for(int pass = 0; pass < 3; pass++) {
    for(int y = 0; y < h; y++) {  // across
      for(int x = 0; x < w; x++) {
        float sum = 0;
        for(int k = -BLUR_RADIUS; k <= BLUR_RADIUS; k++) {
          int xx = x + k;
          sum += xx >= 0 && xx < w ? v[y * w + xx] : 0;
        }
        tmp[y * w + x] = sum / (2 * BLUR_RADIUS + 1);
      }
    }
    for(int y = 0; y < h; y++) {  // down
      for(int x = 0; x < w; x++) {
        float sum = 0;
        for(int k = -BLUR_RADIUS; k <= BLUR_RADIUS; k++) {
          int yy = y + k;
          sum += yy >= 0 && yy < h ? tmp[yy * w + x] : 0;
        }
        v[y * w + x] = sum / (2 * BLUR_RADIUS + 1);
      }
    }
  }
}

int
cursor_with_shadow(cursor_image_t* image, int width, int height, int hot_x,
                   int hot_y, const uint32_t* pixels) {
  const int dx = 1, dy = 2;
  int w = width + 2 * MARGIN, h = height + 2 * MARGIN;
  float* shadow = (float*)calloc((size_t)w * h, sizeof(float));
  float* tmp = (float*)calloc((size_t)w * h, sizeof(float));
  uint32_t* out = (uint32_t*)calloc((size_t)w * h, 4);

  if(!shadow || !tmp || !out) {
    free(shadow);
    free(tmp);
    free(out);
    return -1;
  }
  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x++) {
      shadow[(y + MARGIN + dy) * w + x + MARGIN + dx] =
        (pixels[(size_t)y * width + x] >> 24) / 255.0f;
    }
  }
  box_blur(shadow, tmp, w, h);
  for(int y = 0; y < h; y++) {
    for(int x = 0; x < w; x++) {
      float sa = 0.42f * shadow[y * w + x];
      int sx = x - MARGIN, sy = y - MARGIN;
      uint32_t p = sx >= 0 && sx < width && sy >= 0 && sy < height
        ? pixels[(size_t)sy * width + sx] : 0;
      float a = (p >> 24) / 255.0f;
      float alpha = a + sa * (1 - a);
      if(alpha <= 0.002f) {
        continue;
      }
      uint32_t rgb = mix(0x000000, p & 0xffffff, a / alpha);
      out[(size_t)y * w + x] = ((uint32_t)(alpha * 255 + 0.5f) << 24) | rgb;
    }
  }
  free(shadow);
  free(tmp);
  image->width = w;
  image->height = h;
  image->hot_x = hot_x + MARGIN;
  image->hot_y = hot_y + MARGIN;
  image->pixels = out;
  return 0;
}

// The alpha of something a/255 opaque over the frame's pixel d. The frame
// is premultiplied, and transparent where the title puts a video under the
// page (docs/08-4k.md, D3): what is drawn over that place keeps it showing
// through as much as it should (a dimmed dialog, the cursor's soft edge).
static inline uint32_t
over_alpha(uint32_t d, unsigned a) {
  return (a + (d >> 24) * (255 - a) / 255) << 24;
}

static void
blend(uint32_t* dst, uint32_t rgb, float alpha) {
  uint32_t d = *dst;
  uint32_t out = over_alpha(d, (unsigned)(alpha * 255 + 0.5f));
  for(int s = 0; s < 24; s += 8) {
    float cd = (float)((d >> s) & 0xff), cs = (float)((rgb >> s) & 0xff);
    out |= (uint32_t)(cd + (cs - cd) * alpha + 0.5f) << s;
  }
  *dst = out;
}

void
cursor_draw(uint32_t* frame, int width, int height,
            const cursor_image_t* image, int x, int y) {
  int x0 = x - image->hot_x;
  int y0 = y - image->hot_y;

  if(!image->pixels) {
    return;
  }
  for(int row = 0; row < image->height; row++) {
    int fy = y0 + row;
    if(fy < 0 || fy >= height) {
      continue;
    }
    for(int col = 0; col < image->width; col++) {
      int fx = x0 + col;
      uint32_t p = image->pixels[(size_t)row * image->width + col];
      uint32_t a = p >> 24;
      if(fx < 0 || fx >= width || !a) {
        continue;
      }
      uint32_t* dst = frame + (size_t)fy * width + fx;
      if(a == 255) {
        *dst = p;
      } else {
        blend(dst, p & 0xffffff, a / 255.0f);
      }
    }
  }
}

void
light_draw(uint32_t* frame, int width, int height, const uint8_t* alpha,
           const uint32_t* rgb, int d, int x0, int y0, unsigned strength) {
  for(int row = 0; row < d; row++) {
    const int fy = y0 + row;
    if(fy < 0 || fy >= height) {
      continue;
    }
    for(int col = 0; col < d; col++) {
      const int fx = x0 + col;
      const unsigned a = alpha[row * d + col] * strength >> 8;
      if(fx < 0 || fx >= width || !a) {
        continue;
      }
      blend(frame + (size_t)fy * width + fx, rgb[row * d + col] & 0xffffff, (float)a / 255.0f);
    }
  }
}

// The ring, a neon tube: a stroke RING_GAP pixels outside the element whose
// middle is nearly white (the hot core), a wide glow of the accent colour
// outside it and a faint one inside.
#define RING_GAP         3.0f
#define RING_STROKE      4.0f
#define RING_RADIUS      12.0f
#define RING_GLOW        18.0f
#define RING_INNER_GLOW  7.0f

// The colour a quarter of the way (or more) to white.
static uint32_t
lighter(uint32_t rgb, float f) {
  uint32_t out = 0;
  for(int s = 0; s < 24; s += 8) {
    float c = (float)((rgb >> s) & 0xff);
    out |= (uint32_t)(c + (255 - c) * f + 0.5f) << s;
  }
  return out;
}

static void
ring_pixel(uint32_t* frame, int width, int px, int py, float cx, float cy,
           float hx, float hy, uint32_t color, uint32_t core, float strength) {
  float d = sd_round_box(px + 0.5f, py + 0.5f, cx, cy, hx, hy, RING_RADIUS);
  float half = RING_STROKE / 2;
  uint32_t* dst = frame + (size_t)py * width + px;

  if(d < -half - RING_INNER_GLOW || d > half + RING_GLOW) {
    return;
  }
  if(d > half) {  // the outer glow, fading out like light does
    float g = 1 - (d - half) / RING_GLOW;
    blend(dst, color, strength * (0.22f * g + 0.38f * g * g * g));
    return;
  }
  if(d < -half) {  // the inner glow
    float g = 1 - (-d - half) / RING_INNER_GLOW;
    blend(dst, color, strength * 0.16f * g * g);
    return;
  }
  // The tube: the accent at its edges, the core in its middle.
  float edge = clampf(half + 0.5f - fabsf(d), 0, 1);
  float mid = 1 - fabsf(d) / half;
  blend(dst, color, edge * strength);
  if(mid > 0.25f) {
    blend(dst, core, (mid - 0.25f) / 0.75f * 0.7f * strength);
  }
}

void
ring_draw(uint32_t* frame, int width, int height, float x, float y, float w,
          float h, int clip_top, int clip_bottom, uint32_t color, float strength) {
  // The ring's centre line, then everything it may touch.
  float hx = w / 2.0f + RING_GAP + RING_STROKE / 2;
  float hy = h / 2.0f + RING_GAP + RING_STROKE / 2;
  float cx = x + w / 2.0f, cy = y + h / 2.0f;
  int reach = (int)(RING_STROKE / 2 + RING_GLOW + RING_INNER_GLOW + 2);
  uint32_t core = lighter(color, 0.75f);

  strength = clampf(strength, 0, 1);
  if(strength <= 0.004f) {
    return;
  }
  int x0 = (int)floorf(cx - hx) - reach, x1 = (int)ceilf(cx + hx) + reach;
  int y0 = (int)floorf(cy - hy) - reach, y1 = (int)ceilf(cy + hy) + reach;
  // Rows nearer than this to the top or bottom go across the whole width
  // (the corners); the others only near the left and right sides.
  int band = 2 * reach + (int)RING_RADIUS + 2;

  if(clip_top < 0) {
    clip_top = 0;
  }
  if(clip_bottom > height) {
    clip_bottom = height;
  }
  if(y0 < clip_top) {
    y0 = clip_top;
  }
  if(y1 > clip_bottom) {
    y1 = clip_bottom;
  }
  int left = x0 < 0 ? 0 : x0;
  int right = x1 > width ? width : x1;
  for(int py = y0; py < y1; py++) {
    float top_y = cy - hy, bottom_y = cy + hy;
    if(py < top_y + band && py > top_y - band - 1) {
      for(int px = left; px < right; px++) {
        ring_pixel(frame, width, px, py, cx, cy, hx, hy, color, core, strength);
      }
    } else if(py < bottom_y + band && py > bottom_y - band - 1) {
      for(int px = left; px < right; px++) {
        ring_pixel(frame, width, px, py, cx, cy, hx, hy, color, core, strength);
      }
    } else {
      int l1 = x0 + band, r0 = x1 - band;
      for(int px = left; px < right && px < l1; px++) {
        ring_pixel(frame, width, px, py, cx, cy, hx, hy, color, core, strength);
      }
      for(int px = r0 > left ? r0 : left; px < right; px++) {
        if(px >= l1) {
          ring_pixel(frame, width, px, py, cx, cy, hx, hy, color, core, strength);
        }
      }
    }
  }
}


// ------------------------------------------------------------------ panels

int
panel_make(panel_t* panel, int w, int h, int radius, int margin, int drop,
           float strength) {
  int sw = w + 2 * margin, sh = h + 2 * margin;

  panel->w = w;
  panel->h = h;
  panel->margin = margin;
  panel->coverage = (uint8_t*)malloc((size_t)w * h);
  panel->shadow = (uint8_t*)malloc((size_t)sw * sh);
  panel->full_left = (int*)malloc(sizeof(int) * h);
  panel->full_right = (int*)malloc(sizeof(int) * h);
  if(!panel->coverage || !panel->shadow || !panel->full_left || !panel->full_right) {
    return -1;
  }
  float hx = w / 2.0f, hy = h / 2.0f;
  for(int row = 0; row < h; row++) {
    panel->full_left[row] = w;
    panel->full_right[row] = 0;
    for(int col = 0; col < w; col++) {
      float d = sd_round_box(col + 0.5f, row + 0.5f, hx, hy, hx, hy, (float)radius);
      float a = clampf(0.5f - d, 0, 1);
      uint8_t c = (uint8_t)(a * 255 + 0.5f);
      panel->coverage[(size_t)row * w + col] = c;
      if(c == 255) {
        if(col < panel->full_left[row]) {
          panel->full_left[row] = col;
        }
        panel->full_right[row] = col + 1;
      }
    }
    if(panel->full_right[row] <= panel->full_left[row]) {
      panel->full_left[row] = panel->full_right[row] = 0;
    }
  }
  // The shadow: the same box a little lower, its edge softened over the
  // margin.
  float blur = margin * 0.8f;
  for(int row = 0; row < sh; row++) {
    for(int col = 0; col < sw; col++) {
      float d = sd_round_box(col + 0.5f, row + 0.5f, sw / 2.0f, sh / 2.0f + drop,
                             hx - blur / 3, hy - blur / 3, (float)radius + blur / 3);
      float a = strength * (1 - smooth(-blur / 2, blur, d));
      panel->shadow[(size_t)row * sw + col] = (uint8_t)(clampf(a, 0, 1) * 255 + 0.5f);
    }
  }
  return 0;
}


// (s a + d b) / 255, rounded, for each of the three colours; a + b <= 255.
// Red and blue go two at a time, each in 16 bits of its own: until
// 2026-11-05 14:15 they were divided by 255 as one number, the rest of red
// fell into blue, and the shadow round the menu came out as a blotchy blue
// ring (the TV, and the UI test's screenshot of 11:21).
static inline uint32_t
mix255(uint32_t s, unsigned a, uint32_t d, unsigned b) {
  uint32_t rb = (s & 0xff00ff) * a + (d & 0xff00ff) * b + 0x800080;
  rb = ((rb + ((rb >> 8) & 0xff00ff)) >> 8) & 0xff00ff;
  uint32_t g = ((s >> 8) & 0xff) * a + ((d >> 8) & 0xff) * b + 0x80;
  g = (g + (g >> 8)) >> 8;
  return rb | g << 8;
}


// a over the frame's pixel, both opaque colours, a out of 255.
static inline void
blend_px(uint32_t* dst, uint32_t src, unsigned a) {
  uint32_t d = *dst;
  *dst = over_alpha(d, a) | mix255(src, a, d, 255 - a);
}


static inline void
darken_px(uint32_t* dst, unsigned a) {
  uint32_t d = *dst;
  *dst = over_alpha(d, a) | mix255(0, 0, d, 255 - a);
}


void
panel_draw_glass(uint32_t* frame, int width, int height, const panel_t* panel,
                 int x, int y, const uint32_t* pixels, int stride, unsigned alpha) {
  int m = panel->margin;
  int sw = panel->w + 2 * m;

  for(int sy = 0; sy < panel->h + 2 * m; sy++) {
    int fy = y - m + sy;
    if(fy < 0 || fy >= height) {
      continue;
    }
    uint32_t* line = frame + (size_t)fy * width;
    const uint8_t* shadow = panel->shadow + (size_t)sy * sw;
    int row = sy - m;
    int inside = row >= 0 && row < panel->h;
    int full_left = inside ? panel->full_left[row] + m : sw;
    int full_right = inside ? panel->full_right[row] + m : sw;
    for(int sx = 0; sx < sw; sx++) {
      if(sx == full_left && full_right > full_left) {
        // The panel's own pixels, whole.
        int fx = x - m + sx;
        int n = full_right - full_left;
        int from = 0;
        if(fx < 0) {
          from = -fx;
        }
        if(fx + n > width) {
          n = width - fx;
        }
        if(n > from) {
          const uint32_t* src = pixels + (size_t)row * stride + (sx - m);
          if(alpha >= 255) {
            for(int i = from; i < n; i++) {
              line[fx + i] = src[i] | 0xff000000;
            }
          } else {
            for(int i = from; i < n; i++) {
              blend_px(line + fx + i, src[i], alpha);
            }
          }
        }
        sx = full_right - 1;
        continue;
      }
      int fx = x - m + sx;
      if(fx < 0 || fx >= width) {
        continue;
      }
      unsigned c = 0;
      int col = sx - m;
      if(inside && col >= 0 && col < panel->w) {
        c = panel->coverage[(size_t)row * panel->w + col];
      }
      if(c < 255 && shadow[sx]) {
        darken_px(line + fx, shadow[sx] * (255 - c) / 255 * alpha / 255);
      }
      if(c) {
        blend_px(line + fx, pixels[(size_t)row * stride + col], c * alpha / 255);
      }
    }
  }
}


void
panel_draw(uint32_t* frame, int width, int height, const panel_t* panel,
           int x, int y, const uint32_t* pixels, int stride) {
  panel_draw_glass(frame, width, height, panel, x, y, pixels, stride, 255);
}


void
frame_dim(uint32_t* frame, size_t count) {
  for(size_t i = 0; i < count; i++) {
    frame[i] = ((frame[i] >> 25) + 128) << 24 | ((frame[i] >> 1) & 0x7f7f7f);
  }
}


void
frame_darken(uint32_t* frame, size_t count, unsigned a) {
  if(!a) {
    return;
  }
  a = a > 255 ? 255 : a;
  unsigned keep = 255 - a;
  for(size_t i = 0; i < count; i++) {
    uint32_t d = frame[i];
    uint32_t rb = ((d & 0xff00ff) * keep >> 8) & 0xff00ff;
    uint32_t g = ((d & 0x00ff00) * keep >> 8) & 0x00ff00;
    frame[i] = over_alpha(d, a) | rb | g;
  }
}


void
frame_blend(uint32_t* frame, const uint32_t* src, size_t count, unsigned a) {
  if(a >= 255) {  // opaque: no video shows through the tabs' switcher
    for(size_t i = 0; i < count; i++) {
      frame[i] = src[i] | 0xff000000;
    }
    return;
  }
  for(size_t i = 0; i < count; i++) {
    blend_px(frame + i, src[i], a);
  }
}


void
bitmap_draw(uint32_t* frame, int width, int height, const uint32_t* pixels, int w, int h,
            int left, int top) {
  for(int row = 0; row < h; row++) {
    const int fy = top + row;
    if(fy < 0 || fy >= height) {
      continue;
    }
    for(int col = 0; col < w; col++) {
      const int fx = left + col;
      const uint32_t p = pixels[(size_t)row * w + col];
      const uint32_t a = p >> 24;
      if(fx < 0 || fx >= width || !a) {
        continue;
      }
      uint32_t* dst = frame + (size_t)fy * width + fx;
      if(a == 255) {
        *dst = p;
        continue;
      }
      const uint32_t d = *dst;
      uint32_t out = 0;
      for(int sft = 0; sft < 32; sft += 8) {  // both premultiplied: source over
        uint32_t v = ((p >> sft) & 0xff) + ((d >> sft) & 0xff) * (255 - a) / 255;
        out |= (v > 255 ? 255 : v) << sft;
      }
      *dst = out;
    }
  }
}
