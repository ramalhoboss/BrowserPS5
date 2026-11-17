/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
   session.c on the PC (make test): the text of the tabs, a file written and
   read back, and files with bad, long and Windows lines. */

#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures, passes;

#define CHECK(c, ...) do { if(c) { passes++; printf("PASS "); } else { failures++; \
  printf("FAIL line %d: ", __LINE__); } printf(__VA_ARGS__); printf("\n"); } while(0)

static char path[256];
static char urls[SESSION_TABS_MAX + 2][SESSION_URL_MAX];

static void
put(const char* text) {
  FILE* f = fopen(path, "w");
  if(f) {
    fputs(text, f);
    fclose(f);
  }
}

int
main(void) {
  char text[8192];
  int current = -5, count;

  snprintf(path, sizeof(path), "/tmp/session_test_%d.txt", (int)getpid());
  unlink(path);

  // The text: only web addresses, the one in front counted among them.
  {
    const char* tabs[] = {"https://www.youtube.com/", NULL, "about:blank", "http://a.pt/x?q=1",
                          "file:///data/a.mp4", "https://b.pt/\nhttps://evil/", "HTTPS://C.PT/"};
    size_t n = session_text(tabs, 7, 3, text, sizeof(text));
    CHECK(n == strlen(text) && !strcmp(text, "current 1\nhttps://www.youtube.com/\nhttp://a.pt/x?q=1\nHTTPS://C.PT/\n"),
          "the text keeps the 3 web addresses, the 2nd in front");
    n = session_text(tabs, 7, 2, text, sizeof(text));
    CHECK(!strncmp(text, "current 0\n", 10), "a tab in front that is not kept: 0");
    n = session_text(tabs, 7, 99, text, sizeof(text));
    CHECK(!strncmp(text, "current 0\n", 10), "a tab in front past the end: 0");
    n = session_text(tabs, 7, -1, text, sizeof(text));
    CHECK(!strncmp(text, "current 0\n", 10), "none in front: 0");
    n = session_text(tabs, 7, 0, text, 20);
    CHECK(n == 0 && !text[0], "too small: 0 and empty");
    n = session_text(tabs, 0, 0, text, sizeof(text));
    CHECK(n == 10 && !strcmp(text, "current 0\n"), "no tabs: only the line of the one in front");
    const char* bare[] = {"http://", "https://", "ftp://x/"};
    session_text(bare, 3, 0, text, sizeof(text));
    CHECK(!strcmp(text, "current 0\n"), "a scheme with nothing after and ftp are left out");
  }
  {
    char* tab_long = (char*)malloc(SESSION_URL_MAX + 10);
    memcpy(tab_long, "https://x/", 10);
    memset(tab_long + 10, 'a', SESSION_URL_MAX);
    tab_long[SESSION_URL_MAX] = 0;  // SESSION_URL_MAX characters: one too many
    const char* tabs[] = {tab_long, "https://ok/"};
    char* big = (char*)malloc(3 * SESSION_URL_MAX);
    session_text(tabs, 2, 1, big, 3 * SESSION_URL_MAX);
    CHECK(!strcmp(big, "current 0\nhttps://ok/\n"), "an address too long is left out");
    tab_long[SESSION_URL_MAX - 1] = 0;  // the longest that fits
    session_text(tabs, 2, 1, big, 3 * SESSION_URL_MAX);
    CHECK(strlen(big) == 10 + SESSION_URL_MAX + 12 && !strncmp(big, "current 1\n", 10), "the longest address is kept");
    CHECK(session_write(path, big) == 0, "written");
    count = session_read(path, urls, SESSION_TABS_MAX, &current);
    CHECK(count == 2 && current == 1 && strlen(urls[0]) == SESSION_URL_MAX - 1 && !strcmp(urls[1], "https://ok/"),
          "the longest address read back");
    free(big);
    free(tab_long);
  }
  {
    const char* tabs[SESSION_TABS_MAX + 4];
    char names[SESSION_TABS_MAX + 4][32];
    for(int i = 0; i < SESSION_TABS_MAX + 4; i++) {
      snprintf(names[i], sizeof(names[i]), "https://t%d/", i);
      tabs[i] = names[i];
    }
    session_text(tabs, SESSION_TABS_MAX + 4, SESSION_TABS_MAX + 2, text, sizeof(text));
    int lines = 0;
    for(char* p = text; *p; p++) {
      lines += *p == '\n';
    }
    CHECK(lines == SESSION_TABS_MAX + 1 && !strncmp(text, "current 0\n", 10), "at most %d tabs, one in front past them: 0",
          SESSION_TABS_MAX);
  }

  // Written and read back.
  {
    const char* tabs[] = {"https://a.pt/", "https://b.pt/página", "http://c.pt/"};
    session_text(tabs, 3, 2, text, sizeof(text));
    CHECK(session_write(path, text) == 0, "written");
    char fresh[300];
    snprintf(fresh, sizeof(fresh), "%s.new", path);
    CHECK(access(fresh, F_OK) != 0, "no .new left behind");
    count = session_read(path, urls, SESSION_TABS_MAX, &current);
    CHECK(count == 3 && current == 2 && !strcmp(urls[0], "https://a.pt/") && !strcmp(urls[1], "https://b.pt/página")
          && !strcmp(urls[2], "http://c.pt/"), "read back: 3, the 3rd in front");
    count = session_read(path, urls, 2, &current);
    CHECK(count == 2 && current == 0, "read only 2: the one in front is not there, 0");
  }
  CHECK(session_write("/nonexistent-dir/session.txt", "x") == -1, "a folder that is not there: -1");

  // Files made by hand.
  put("current 1\r\nhttps://a.pt/\r\nhttps://b.pt/\r\n");
  count = session_read(path, urls, SESSION_TABS_MAX, &current);
  CHECK(count == 2 && current == 1 && !strcmp(urls[1], "https://b.pt/"), "Windows lines");
  put("https://a.pt/\nhttps://b.pt/");
  count = session_read(path, urls, SESSION_TABS_MAX, &current);
  CHECK(count == 2 && current == 0 && !strcmp(urls[1], "https://b.pt/"), "no line of the one in front, no last line end");
  put("current 7\nhttps://a.pt/\n");
  count = session_read(path, urls, SESSION_TABS_MAX, &current);
  CHECK(count == 1 && current == 0, "one in front not there: 0");
  put("current -3\nhttps://a.pt/\nhttps://b.pt/\n");
  count = session_read(path, urls, SESSION_TABS_MAX, &current);
  CHECK(count == 2 && current == 0, "a negative one in front: 0");
  put("current 1\njavascript:alert(1)\n\nfile:///etc/passwd\nhttps://a.pt/\ncurrent 0\nhttps://b\x01.pt/\nhttps://c.pt/\n");
  count = session_read(path, urls, SESSION_TABS_MAX, &current);
  CHECK(count == 2 && current == 1 && !strcmp(urls[0], "https://a.pt/") && !strcmp(urls[1], "https://c.pt/"),
        "bad lines left out, a 2nd 'current' line is not a tab");
  {
    FILE* f = fopen(path, "w");
    fputs("current 1\nhttps://long/", f);
    for(int i = 0; i < SESSION_URL_MAX + 100; i++) {
      fputc('b', f);
    }
    fputs("\nhttps://after/\n", f);
    fclose(f);
    count = session_read(path, urls, SESSION_TABS_MAX, &current);
    CHECK(count == 1 && !strcmp(urls[0], "https://after/") && current == 0, "a line too long is left out whole");
  }
  {
    FILE* f = fopen(path, "w");
    fputs("current 0\nhttps://long/", f);
    for(int i = 0; i < SESSION_URL_MAX + 100; i++) {
      fputc('b', f);
    }
    fclose(f);
    count = session_read(path, urls, SESSION_TABS_MAX, &current);
    CHECK(count == 0 && current == 0, "a line too long at the end without a line end is left out");
  }
  put("");
  count = session_read(path, urls, SESSION_TABS_MAX, &current);
  CHECK(count == 0 && current == 0, "an empty file: 0");
  unlink(path);
  current = 9;
  count = session_read(path, urls, SESSION_TABS_MAX, &current);
  CHECK(count == 0 && current == 0, "no file: 0");

  printf("%d passed, %d failed\n", passes, failures);
  if(!failures) {
    printf("session: all passed\n");
  }
  return failures != 0;
}
