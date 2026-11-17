/* Is a text JSON a page can take (tests-host, 2026-11-14)? The browser hands its lists to its pages as JSON inside a
   script: one byte that is not UTF-8, or a string that does not end, and the page gets nothing at all. */
#pragma once
#include <string.h>

static int
utf8_valid(const char* text) {
  const unsigned char* s = (const unsigned char*)text;
  while(*s) {
    const int n = *s < 0x80 ? 1 : *s >= 0xc2 && *s < 0xe0 ? 2 : *s >= 0xe0 && *s < 0xf0 ? 3 : *s >= 0xf0 && *s < 0xf5 ? 4 : 0;
    if(!n) {
      return 0;
    }
    for(int k = 1; k < n; k++) {
      if((s[k] & 0xc0) != 0x80) {
        return 0;
      }
    }
    s += n;
  }
  return 1;
}

static const char* json_value(const char* p, int depth);

static const char*
json_space(const char* p) {
  while(*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') {
    p++;
  }
  return p;
}

static const char*
json_string(const char* p) {
  if(*p != '"') {
    return 0;
  }
  for(p++; *p != '"'; p++) {
    if((unsigned char)*p < 0x20) {  /* (the text's end too) */
      return 0;
    }
    if(*p == '\\') {
      p++;
      if(*p == 'u') {
        for(int k = 1; k <= 4; k++) {
          if(!strchr("0123456789abcdefABCDEF", p[k]) || !p[k]) {
            return 0;
          }
        }
        p += 4;
      } else if(!*p || !strchr("\"\\/bfnrt", *p)) {
        return 0;
      }
    }
  }
  return p + 1;
}

static const char*
json_value(const char* p, int depth) {
  p = json_space(p);
  if(depth > 40) {
    return 0;
  }
  if(*p == '"') {
    return json_string(p);
  }
  if(*p == '{' || *p == '[') {
    const char close = *p == '{' ? '}' : ']';
    const int object = *p == '{';
    p = json_space(p + 1);
    if(*p == close) {
      return p + 1;
    }
    for(;;) {
      if(object) {
        p = json_string(json_space(p));
        if(!p) {
          return 0;
        }
        p = json_space(p);
        if(*p != ':') {
          return 0;
        }
        p++;
      }
      p = json_value(p, depth + 1);
      if(!p) {
        return 0;
      }
      p = json_space(p);
      if(*p == close) {
        return p + 1;
      }
      if(*p != ',') {
        return 0;
      }
      p++;
    }
  }
  if(!strncmp(p, "true", 4)) {
    return p + 4;
  }
  if(!strncmp(p, "false", 5)) {
    return p + 5;
  }
  if(!strncmp(p, "null", 4)) {
    return p + 4;
  }
  const char* start = p;
  if(*p == '-') {
    p++;
  }
  if(*p < '0' || *p > '9') {
    return 0;
  }
  while((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-') {
    p++;
  }
  return p > start ? p : 0;
}

/* 1: one JSON value and nothing after it (its strings' bytes over 0x7f may be anything: a page read from a file
   takes those as U+FFFD, and a script goes through run_script(), which mends them). */
static int
json_shape(const char* text) {
  const char* end = text ? json_value(text, 0) : 0;
  return end && !*json_space(end);
}

/* 1: one JSON value and nothing after it, all of it UTF-8. */
static int
json_valid(const char* text) {
  return text && utf8_valid(text) && json_shape(text);
}
