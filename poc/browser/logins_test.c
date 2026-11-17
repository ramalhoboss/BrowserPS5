/*   Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
 *
 * logins.c on the build machine (make test): origins, saving and loading
 * (0600, escapes), a new password for a user, "never", the keyboard's list
 * (newest first, at most LOGINS_OFFERED) and the settings' list (no
 * password in it).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logins.h"

static int failed;

#define CHECK(what, cond)                \
  do {                                   \
    if(cond) {                           \
      printf("PASS %s\n", what);         \
    } else {                             \
      printf("FAIL %s\n", what);         \
      failed = 1;                        \
    }                                    \
  } while(0)

int
main(void) {
  char o[256], json[1024], user[256], pass[256];
  char path[] = "/tmp/logins_testXXXXXX";
  close(mkstemp(path));
  unlink(path);

  /* Origins. */
  CHECK("https origin", !logins_origin("https://Accounts.Example.com/login?x=1", o, sizeof(o)) &&
                            !strcmp(o, "https://accounts.example.com"));
  CHECK("default port away", !logins_origin("https://a.pt:443/", o, sizeof(o)) && !strcmp(o, "https://a.pt"));
  CHECK("other port kept", !logins_origin("http://a.pt:8080/x", o, sizeof(o)) && !strcmp(o, "http://a.pt:8080"));
  CHECK("user@ is not the host", !logins_origin("https://evil.com@bank.pt/", o, sizeof(o)) &&
                                     !strcmp(o, "https://bank.pt"));
  CHECK("no file:", logins_origin("file:///data/x.html", o, sizeof(o)) == -1);
  CHECK("no browserps5:", logins_origin("browserps5:go", o, sizeof(o)) == -1);

  /* Saving. */
  CHECK("empty password refused", logins_put("https://a.pt", "ana", "", 1) == -1);
  CHECK("new", logins_put("https://a.pt", "ana", "p\tw\\1\n", 10) == 1);
  CHECK("same", logins_put("https://a.pt", "ana", "p\tw\\1\n", 11) == 0);
  CHECK("known", logins_known("https://a.pt", "ana", "p\tw\\1\n"));
  CHECK("new password", logins_put("https://a.pt", "ana", "second", 12) == 2 &&
                            logins_known("https://a.pt", "ana", "second"));
  CHECK("second user", logins_put("https://a.pt", "bruno@x.pt", "b", 20) == 1);
  CHECK("third", logins_put("https://a.pt", "carla", "c", 15) == 1);
  CHECK("fourth", logins_put("https://a.pt", "dina", "d", 5) == 1);
  CHECK("other site", logins_put("https://b.pt", "\"quoted\" <x>", "q", 7) == 1);
  CHECK("count", logins_count("https://a.pt") == 4 && logins_count("https://b.pt") == 1 &&
                     logins_count("https://c.pt") == 0);

  /* The keyboard's list: the newest 3, newest first. */
  CHECK("users json", logins_users_json("https://a.pt", json, sizeof(json)) == 3 &&
                          !strcmp(json, "[\"bruno@x.pt\",\"carla\",\"ana\"]"));
  CHECK("users json escaped", logins_users_json("https://b.pt", json, sizeof(json)) == 1 &&
                                  !strcmp(json, "[\"\\\"quoted\\\" \\u003cx>\"]"));
  CHECK("get 0", !logins_get("https://a.pt", 0, user, sizeof(user), pass, sizeof(pass)) &&
                     !strcmp(user, "bruno@x.pt") && !strcmp(pass, "b"));
  CHECK("get 2", !logins_get("https://a.pt", 2, user, sizeof(user), pass, sizeof(pass)) &&
                     !strcmp(user, "ana") && !strcmp(pass, "second"));
  CHECK("get out of range", logins_get("https://a.pt", 3, user, sizeof(user), pass, sizeof(pass)) == -1);
  CHECK("no list elsewhere", logins_users_json("https://c.pt", json, sizeof(json)) == 0 && !strcmp(json, "[]"));

  /* Never. */
  CHECK("never", logins_set_never("https://c.pt", 30) == 1 && logins_never("https://c.pt") &&
                     !logins_never("https://a.pt"));
  CHECK("never twice", logins_set_never("https://c.pt", 31) == 0);

  /* The file. */
  CHECK("saved", !logins_save(path));
  struct stat st;
  CHECK("file 0600", !stat(path, &st) && (st.st_mode & 0777) == 0600);
  logins_clear();
  CHECK("cleared", logins_count("https://a.pt") == 0);
  CHECK("loaded", logins_load(path) == 6);
  CHECK("escapes back", logins_put("https://a.pt", "ana", "second", 40) == 0 &&
                            logins_known("https://b.pt", "\"quoted\" <x>", "q") &&
                            logins_never("https://c.pt"));
  CHECK("tab in a password back", logins_put("https://d.pt", "u", "a\tb", 1) == 1 && !logins_save(path) &&
                                      logins_load(path) == 7 && logins_known("https://d.pt", "u", "a\tb"));

  /* Saved after all: no longer never. */
  CHECK("saved after never", logins_put("https://c.pt", "z", "z", 50) == 1 && !logins_never("https://c.pt"));

  /* The settings' list: no passwords. */
  char* list = logins_list_json();
  CHECK("list", list && strstr(list, "{\"origin\": \"https://a.pt\", \"user\": \"ana\", \"time\": 40, \"never\": false}"));
  CHECK("list without passwords", list && !strstr(list, "second") && !strstr(list, "\"q\""));
  free(list);
  logins_set_never("https://e.pt", 60);
  list = logins_list_json();
  CHECK("list never", list && strstr(list, "{\"origin\": \"https://e.pt\", \"user\": \"\", \"time\": 60, \"never\": true}"));
  free(list);

  /* Removing. */
  CHECK("remove user", !logins_remove("https://a.pt", "carla") && logins_count("https://a.pt") == 3);
  CHECK("remove never", !logins_remove("https://e.pt", 0) && !logins_never("https://e.pt"));
  CHECK("remove missing", logins_remove("https://a.pt", "nobody") == -1);

  /* A bad line is skipped. */
  FILE* f = fopen(path, "w");
  fputs("X\tjunk\nL\thttps://f.pt\tu\t\t1\nL\thttps://g.pt\tu\tp\t2\n", f);
  fclose(f);
  CHECK("bad lines skipped", logins_load(path) == 1 && logins_known("https://g.pt", "u", "p"));

  unlink(path);
  printf(failed ? "logins_test: FAIL\n" : "logins_test: all passed\n");
  return failed;
}
