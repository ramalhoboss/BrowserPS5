// BrowserPS5 RN: ask_box.h.
#include "ask_box.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plutovg.h"

#define BOX_W 900
#define PAD 48
#define TEXT_SIZE 30.0f
#define LINE_H 44
#define MAX_LINES 9
#define LINE_BYTES 512

static plutovg_font_face_t* regular;
static plutovg_font_face_t* bold;

static void
rgba(plutovg_canvas_t* c, uint32_t rgb, float a) {
  plutovg_canvas_set_rgba(c, ((rgb >> 16) & 0xff) / 255.f, ((rgb >> 8) & 0xff) / 255.f, (rgb & 0xff) / 255.f, a);
}

static float
width_of(plutovg_font_face_t* face, float size, const char* s, int n) {
  return n > 0 ? plutovg_font_face_text_extents(face, size, s, n, PLUTOVG_TEXT_ENCODING_UTF8, 0) : 0;
}

static void
say(plutovg_canvas_t* c, plutovg_font_face_t* face, float size, float x, float y, uint32_t rgb, float a,
    const char* s) {
  rgba(c, rgb, a);
  plutovg_canvas_set_font(c, face, size);
  plutovg_canvas_fill_text(c, s, (int)strlen(s), PLUTOVG_TEXT_ENCODING_UTF8, x, y);
}

// The bytes of the UTF-8 character at s.
static int
char_bytes(const char* s) {
  const unsigned char c = (unsigned char)*s;
  int n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
  for(int i = 1; i < n; i++) {
    if(((unsigned char)s[i] & 0xc0) != 0x80) {
      return i;
    }
  }
  return n;
}

// The text in lines no wider than max: at spaces, at the text's own line
// ends, and inside a word longer than a line. The count; lines beyond
// MAX_LINES are left out and *more says so.
static int
wrap(const char* text, float max, char lines[MAX_LINES][LINE_BYTES], int* more) {
  int count = 0;
  const char* p = text;
  *more = 0;
  while(*p) {
    if(count == MAX_LINES) {
      *more = 1;
      break;
    }
    const char* fit = p;        // as far as fits
    const char* at_space = 0;   // the last space that fits
    const char* q = p;
    while(*q && *q != '\n') {
      const int n = char_bytes(q);
      if(q + n - p >= LINE_BYTES - 4 || width_of(regular, TEXT_SIZE, p, (int)(q + n - p)) > max) {
        break;
      }
      if(*q == ' ') {
        at_space = q;
      }
      q += n;
      fit = q;
    }
    const char* end = fit;
    const char* next = fit;
    if(*fit == '\n') {
      next = fit + 1;
    } else if(*fit) {  // the line is full
      if(at_space && at_space > p) {
        end = at_space;
        next = at_space + 1;
      } else if(fit == p) {  // not even one character fits: take it anyway
        end = next = p + char_bytes(p);
      }
    }
    size_t n = (size_t)(end - p);
    memcpy(lines[count], p, n);
    lines[count][n] = 0;
    count++;
    p = next;
    while(*p == ' ') {
      p++;
    }
  }
  return count;
}

int
ask_box_draw(ask_box_t* box, const char* fonts, const char* title, const char* text, const char* value,
             const char* ok, const char* cancel, uint32_t accent) {
  static char lines[MAX_LINES][LINE_BYTES];
  char path[512];

  box->pixels = 0;
  if(!regular) {
    snprintf(path, sizeof(path), "%s/DejaVuSans.ttf", fonts);
    regular = plutovg_font_face_load_from_file(path, 0);
    snprintf(path, sizeof(path), "%s/DejaVuSans-Bold.ttf", fonts);
    bold = plutovg_font_face_load_from_file(path, 0);
  }
  if(!regular || !bold) {
    return -1;
  }
  int more = 0;
  const float inner = BOX_W - 2 * PAD;
  int count = wrap(text && *text ? text : " ", inner, lines, &more);
  if(more) {  // the last line shown says there is more
    char* last = lines[MAX_LINES - 1];
    size_t n = strlen(last);
    while(n > 0 && width_of(regular, TEXT_SIZE, last, (int)n) > inner - 40) {
      n--;
      while(n > 0 && ((unsigned char)last[n] & 0xc0) == 0x80) {
        n--;
      }
    }
    snprintf(last + n, LINE_BYTES - n, "\xe2\x80\xa6");
  }
  const int field = value ? 84 : 0;
  const int w = BOX_W, h = PAD + 34 + 26 + count * LINE_H + field + 34 + 52 + PAD - 8;
  plutovg_surface_t* s = plutovg_surface_create(w, h);
  plutovg_canvas_t* c = s ? plutovg_canvas_create(s) : 0;
  if(!c) {
    plutovg_surface_destroy(s);
    return -1;
  }
  // The window: dark glass, a thin edge of light, the theme's colour along its top.
  plutovg_canvas_round_rect(c, 1, 1, w - 2, h - 2, 30, 30);
  rgba(c, 0x10131c, 0.975f);
  plutovg_canvas_fill(c);
  plutovg_canvas_round_rect(c, 1.5f, 1.5f, w - 3, h - 3, 29.5f, 29.5f);
  rgba(c, 0xffffff, 0.14f);
  plutovg_canvas_set_line_width(c, 1.5f);
  plutovg_canvas_stroke(c);
  plutovg_canvas_round_rect(c, PAD, 0.5f, w - 2 * PAD, 4, 2, 2);
  rgba(c, accent, 0.95f);
  plutovg_canvas_fill(c);

  float y = PAD + 26;
  say(c, bold, 25, PAD, y, 0xffffff, 0.62f, title ? title : "");
  y += 26;
  for(int i = 0; i < count; i++) {
    y += LINE_H;
    say(c, regular, TEXT_SIZE, PAD, y - 10, 0xffffff, 0.96f, lines[i]);
  }
  if(value) {  // prompt(): what OK gives (there is no keyboard while the page waits)
    y += 20;
    plutovg_canvas_round_rect(c, PAD, y, inner, 56, 14, 14);
    rgba(c, 0xffffff, 0.08f);
    plutovg_canvas_fill(c);
    char shown[LINE_BYTES];
    size_t n = strlen(value);
    if(n >= sizeof(shown)) {
      n = sizeof(shown) - 1;
    }
    memcpy(shown, value, n);
    shown[n] = 0;
    while(n > 0 && width_of(regular, 27, shown, (int)n) > inner - 40) {
      n--;
      while(n > 0 && ((unsigned char)shown[n] & 0xc0) == 0x80) {
        n--;
      }
      shown[n] = 0;
    }
    say(c, regular, 27, PAD + 20, y + 38, 0xffffff, 0.9f, shown);
    y += 64;
  }
  // What the buttons do, at the right: cross, then circle.
  y = h - PAD + 2;
  char hint[160];
  float x = w - PAD;
  if(cancel) {
    snprintf(hint, sizeof(hint), "\xe2\x97\x8b  %s", cancel);
    x -= width_of(bold, 26, hint, (int)strlen(hint));
    say(c, bold, 26, x, y, 0xffffff, 0.7f, hint);
    x -= 48;
  }
  snprintf(hint, sizeof(hint), "\xe2\x9c\x95  %s", ok ? ok : "OK");
  x -= width_of(bold, 26, hint, (int)strlen(hint));
  say(c, bold, 26, x, y, accent, 1, "\xe2\x9c\x95");
  say(c, bold, 26, x + width_of(bold, 26, "\xe2\x9c\x95  ", 5), y, 0xffffff, 0.96f, ok ? ok : "OK");

  uint32_t* out = (uint32_t*)malloc((size_t)w * h * 4);
  if(out) {
    const unsigned char* data = plutovg_surface_get_data(s);
    const int stride = plutovg_surface_get_stride(s);
    for(int row = 0; row < h; row++) {
      memcpy(out + (size_t)row * w, data + (size_t)row * stride, (size_t)w * 4);
    }
  }
  plutovg_canvas_destroy(c);
  plutovg_surface_destroy(s);
  box->pixels = out;
  box->width = w;
  box->height = h;
  return out ? 0 : -1;
}
