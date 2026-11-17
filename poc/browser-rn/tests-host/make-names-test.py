"""The names the browser gives to what it saves, and the hosts it reads out of addresses (main.cpp clean_name(),
unique_path(), download_kind(), url_host(), url_for_log()), tested on the PC (2026-11-14).

A server chooses the name of a download (Content-Disposition), a page chooses the address: both are text from
outside. This cuts the functions out of ../../browser/main.cpp as they are and checks, with AddressSanitizer and
UndefinedBehaviorSanitizer, on fixed cases and on many made by chance:

  - a cleaned name is only a name: no folder in it, nothing the system takes otherwise, no dot or space at its
    ends, never longer than its place; valid UTF-8 stays valid UTF-8 (no letter cut in two);
  - a name that needs no cleaning comes out as it went in (its last letter too, whatever its bytes);
  - a name too long keeps its end (".mp4"), so the file is still what it was;
  - cleaning twice changes nothing more;
  - a host is lower case, has nothing of the path, the port or the user in it, and fits its place.

    python3 make-names-test.py <out.cpp>
"""
import sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, '..', '..', 'browser', 'main.cpp'), encoding='utf-8').read()


def function(head):
    a = src.index(head)
    return src[a:src.index('\n}\n', a) + 3]


parts = [
    function('static bool\nurl_is_web('),
    function('static const char*\nurl_host('),
    function('static const char*\nurl_for_log('),
    function('static bool\nname_ends_with('),
    function('static const char*\ndownload_kind('),
    function('static void\nclean_name('),
    function('static void\nunique_path('),
    function('static const char*\nlang_of_console('),
    function('static size_t\nutf8_char('),
    function('static char*\nutf8_mended('),
]
program = r'''// MADE by make-names-test.py from browser/main.cpp: the functions as they are, with a test around them.
#define APP_RN 1
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>

@PARTS@

static int checks, failed;
static void
check(bool fine, const char* what, const std::string& with) {
  checks++;
  if(!fine) {
    failed++;
    if(failed <= 15) {
      printf("FAIL: %s  [", what);
      for(unsigned char c : with) {
        printf(c >= 0x20 && c < 0x7f && c != '\\' ? "%c" : "\\x%02x", c);
      }
      printf("]\n");
    }
  }
}

static unsigned long long rng = 88172645463325252ull;
static unsigned rnd(void) {
  rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
  return (unsigned)(rng >> 11);
}

// Is it UTF-8 (as a decoder that takes nothing else would say)?
static bool
utf8(const std::string& s) {
  for(size_t i = 0; i < s.size();) {
    const unsigned char c = s[i];
    const size_t n = c < 0x80 ? 1 : c >= 0xc2 && c < 0xe0 ? 2 : c >= 0xe0 && c < 0xf0 ? 3 : c >= 0xf0 && c < 0xf5 ? 4 : 0;
    if(!n || i + n > s.size()) {
      return false;
    }
    for(size_t k = 1; k < n; k++) {
      if(((unsigned char)s[i + k] & 0xc0) != 0x80) {
        return false;
      }
    }
    i += n;
  }
  return true;
}

static std::string
cleaned(const std::string& in, size_t size) {
  char* out = (char*)malloc(size);  // (its own block: a byte past it is seen)
  clean_name(in.c_str(), out, size);
  std::string s = out;
  check(s.size() < size, "a cleaned name does not end inside its place", in);
  free(out);
  return s;
}

static const char* const letters[] = {"a", "Z", "7", "-", "_", "(", ")", " ", ".", "\xc3\xa9", "\xc3\xa7", "\xc3\xa3", "\xe3\x83\x95",
                                      "\xe3\x82\xa1", "\xe4\xb8\xad", "\xf0\x9f\x8e\xae", "\xd0\xb6", "\xce\xa9", "+", "&", "'", "%", "=", ","};
static const char* const ends[] = {".mp4", ".pkg", ".pdf", ".tar.gz", ".user.js", ".7z", ".jpeg", ".torrent", "", "", ".\xc3\xa9", ".webm"};
static const char* const nasty[] = {"/", "\\", ":", "*", "?", "\"", "<", ">", "|", "\x01", "\x1f", "\x7f", "\n", "\t", "..", "../", "..\\"};

static void
one_name(const std::string& in, size_t size, bool made_of_text) {
  const std::string out = cleaned(in, size);
  const size_t most = size > 6 ? size - 6 : 0;
  // what the name itself is: after the last / or \ (as the browser takes it)
  size_t cut = in.find_last_of('/');
  cut = cut == std::string::npos ? 0 : cut + 1;
  const size_t back = in.find_last_of('\\');
  if(back != std::string::npos && back + 1 > cut) {
    cut = back + 1;
  }
  const std::string base = in.substr(cut);

  check(out.size() <= most, "a cleaned name is no longer than its place lets it be", in);
  bool only_name = true;
  for(unsigned char c : out) {
    only_name = only_name && c >= 0x20 && c != 0x7f && !strchr("/\\:*?\"<>|", c);
  }
  check(only_name, "a cleaned name has nothing a folder or the system would take otherwise", in);
  check(out.empty() || (out[0] != '.' && out[0] != ' '), "a cleaned name does not begin with a dot or a space", in);
  check(out.empty() || (out.back() != '.' && out.back() != ' '), "a cleaned name does not end in a dot or a space", in);
  check(out != "." && out != "..", "a cleaned name is not a folder's", in);
  if(made_of_text && utf8(base)) {
    check(utf8(out), "a name of whole letters stays one of whole letters", in);
  }
  check(cleaned(out, size) == out, "cleaning twice changes nothing more", in);
  // one that needs nothing comes out as it went in
  bool plain = !base.empty() && base.size() <= most && base[0] != '.' && base[0] != ' ' && base.back() != '.' && base.back() != ' ';
  for(unsigned char c : base) {
    plain = plain && c >= 0x20 && c != 0x7f && !strchr("/\\:*?\"<>|", c);
  }
  if(plain) {
    check(out == base, "a name that needs no cleaning comes out as it went in", in);
  }
  // one too long keeps its end
  if(made_of_text && base.size() > most && most >= 40) {
    const size_t dot = base.find_last_of('.');
    if(dot != std::string::npos && dot > 0 && base.size() - dot <= 16 && base.size() - dot >= 2 && base.back() != '.' && base.back() != ' ') {
      const std::string end = base.substr(dot);
      bool end_plain = true;
      for(unsigned char c : end) {
        end_plain = end_plain && c > 0x20 && c != 0x7f && !strchr("/\\:*?\"<>|", c);
      }
      if(end_plain) {
        check(out.size() >= end.size() && !out.compare(out.size() - end.size(), end.size(), end), "a name too long keeps its end", in);
        check(out.size() + 12 > most, "a name too long is cut by little under its place (a letter, dots and spaces at the cut)", in);
      }
    }
  }
}

int
main(int argc, char** argv) {
  const int rounds = argc > 2 ? atoi(argv[2]) : 200000;
  char out[256], host[64], path[900];

  // ---- fixed cases
  static const struct { const char* in; const char* want; } names[] = {
    {"report.pdf", "report.pdf"},
    {"resum\xc3\xa9", "resum\xc3\xa9"},                              // (was "resum" before 2026-11-14)
    {"\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab", "\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab"},
    {"\xf0\x9f\x8e\xae", "\xf0\x9f\x8e\xae"},
    {"../../etc/passwd", "passwd"},
    {"..\\..\\boot.ini", "boot.ini"},
    {"/data/../../x/.hidden", "hidden"},
    {"a/b\\c", "c"},
    {"...", ""},
    {" . . ", ""},
    {"", ""},
    {"/", ""},
    {"name. ", "name"},
    {"what?.mp4", "what_.mp4"},
    {"a:b*c\"d<e>f|g", "a_b_c_d_e_f_g"},
    {"line\nbreak\t.txt", "line_break_.txt"},
    {"  spaced out.txt", "spaced out.txt"},
    {".user.js", "user.js"},
  };
  for(const auto& c : names) {
    clean_name(c.in, out, sizeof(out));
    check(!strcmp(out, c.want), "a fixed name", std::string(c.in) + " -> " + out);
    one_name(c.in, sizeof(out), true);
  }
  {  // too long, with an end: the end stays, the letters before it go, whole
    std::string name;
    for(int i = 0; i < 120; i++) {
      name += "\xc3\xa9\xe4\xb8\xad";  // 5 bytes a turn: 600 bytes
    }
    name += ".mp4";
    clean_name(name.c_str(), out, sizeof(out));
    const std::string got = out;
    check(got.size() <= 250 && got.size() >= 246 && utf8(got) && name_ends_with(out, ".mp4") && !strcmp(download_kind(out, ""), "media"),
          "a long video's name is still a video's", got);
  }
  // ---- by chance
  for(int i = 0; i < rounds; i++) {
    std::string name;
    const int kind = rnd() % 10;
    const size_t size = rnd() % 8 == 0 ? 7 + rnd() % 60 : 256;
    if(kind == 0) {  // any bytes at all
      const int n = rnd() % 400;
      for(int k = 0; k < n; k++) {
        const char c = (char)(1 + rnd() % 255);
        name += c;
      }
      one_name(name, size, false);
      continue;
    }
    const int n = kind < 3 ? 60 + rnd() % 280 : 1 + rnd() % 40;  // (some longer than their place)
    if(rnd() % 6 == 0) {
      name += nasty[rnd() % (sizeof(nasty) / sizeof(nasty[0]))];
    }
    for(int k = 0; k < n; k++) {
      name += rnd() % 25 == 0 ? nasty[rnd() % (sizeof(nasty) / sizeof(nasty[0]))] : letters[rnd() % (sizeof(letters) / sizeof(letters[0]))];
    }
    name += ends[rnd() % (sizeof(ends) / sizeof(ends[0]))];
    one_name(name, size, true);
  }
  // ---- the name that is free in a folder
  if(argc > 1 && !chdir(argv[1])) {
    static const struct { const char* have[3]; const char* ask; const char* want; } free_names[] = {
      {{0, 0, 0}, "a.txt", "./a.txt"},
      {{"a.txt", 0, 0}, "a.txt", "./a (2).txt"},
      {{"a.txt", "a (2).txt", 0}, "a.txt", "./a (3).txt"},
      {{"x.user.js", 0, 0}, "x.user.js", "./x (2).user.js"},
      {{"noend", 0, 0}, "noend", "./noend (2)"},
      {{"r\xc3\xa9.tar.gz", 0, 0}, "r\xc3\xa9.tar.gz", "./r\xc3\xa9.tar (2).gz"},
    };
    for(const auto& c : free_names) {
      for(const char* have : c.have) {
        if(have) {
          FILE* f = fopen(have, "w");
          if(f) {
            fclose(f);
          }
        }
      }
      unique_path(".", c.ask, path, sizeof(path));
      check(!strcmp(path, c.want), "a free name beside one that is taken", std::string(c.ask) + " -> " + path);
      for(const char* have : c.have) {
        if(have) {
          unlink(have);
        }
      }
    }
  }
  // ---- hosts
  static const struct { const char* url; const char* want; } hosts[] = {
    {"https://Example.COM/a/b?c#d", "example.com"},
    {"http://example.com:8080/", "example.com"},
    {"https://user:pw@Example.com:8443/x", "example.com"},
    {"https://a@b@c.example/x", "c.example"},
    {"https://example.com", "example.com"},
    {"https://example.com?x=@y", "example.com"},
    {"https://example.com#@frag", "example.com"},
    {"https://example.com/@user", "example.com"},
    {"ftp://example.com/", ""},
    {"about:blank", ""},
    {"https://", ""},
    {"", ""},
    {"javascript:alert(1)//http://x", ""},
  };
  for(const auto& c : hosts) {
    url_host(c.url, host, sizeof(host));
    check(!strcmp(host, c.want), "a fixed host", std::string(c.url) + " -> " + host);
  }
  for(int i = 0; i < rounds; i++) {
    std::string url = rnd() % 2 ? "https://" : "http://";
    const int n = rnd() % 120;
    for(int k = 0; k < n; k++) {
      static const char some[] = "abcXYZ019.-_@:/?#%[]\\ ~";
      url += rnd() % 30 == 0 ? (char)(1 + rnd() % 255) : some[rnd() % (sizeof(some) - 1)];
    }
    const size_t size = rnd() % 4 == 0 ? 1 + rnd() % 20 : sizeof(host);
    char* place = (char*)malloc(size);
    url_host(url.c_str(), place, size);
    const std::string got = place;
    free(place);
    bool fine = got.size() < size || size == 0;
    for(unsigned char c : got) {
      fine = fine && c != '/' && c != '?' && c != '#' && c != '@' && c != ':' && !(c >= 'A' && c <= 'Z');
    }
    check(fine, "a host has nothing of the path, the port or the user in it, in lower case, in its place", url);
    char* log = (char*)malloc(size + 8);
    url_for_log(url.c_str(), log, size + 8);
    check(strlen(log) < size + 8 && (!strchr(log, '?') || strstr(log, "(...)") != 0), "what the log shows of an address has no query in it", url);
    free(log);
  }
  // ---- a script that is not UTF-8 is mended (run_script()): what the engine would refuse whole now arrives
  {
    auto strict = [](const std::string& s) {  // UTF-8 as the engine reads it: decoded, and its value checked
      for(size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        const size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if(!n || i + n > s.size()) {
          return false;
        }
        unsigned long v = n == 1 ? c : c & (0xff >> (n + 1));
        for(size_t k = 1; k < n; k++) {
          if(((unsigned char)s[i + k] & 0xc0) != 0x80) {
            return false;
          }
          v = v << 6 | ((unsigned char)s[i + k] & 0x3f);
        }
        static const unsigned long least[] = {0, 0, 0x80, 0x800, 0x10000};
        if(v < least[n] || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) {
          return false;
        }
        i += n;
      }
      return true;
    };
    static const struct { const char* in; const char* want; } mends[] = {
      {"setTitle(\"caf\xc3\")", "setTitle(\"caf\xef\xbf\xbd\")"},              // a letter cut in two: its quote and bracket arrive
      {"x('\xe4\xb8')", "x('\xef\xbf\xbd\xef\xbf\xbd')"},
      {"can\xe7\xe3o.mp4", "can\xef\xbf\xbd\xef\xbf\xbdo.mp4"},                // a name in Latin-1
      {"\xc0\xaf", "\xef\xbf\xbd\xef\xbf\xbd"},                                // an overlong "/"
      {"\xed\xa0\x80", "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},            // half a surrogate pair
      {"\xf4\x90\x80\x80", "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},  // over U+10FFFF
    };
    for(const auto& c : mends) {
      char* got = utf8_mended(c.in);
      check(got && !strcmp(got, c.want), "a fixed script, mended", c.in);
      free(got);
    }
    check(!utf8_mended("") && !utf8_mended("plain") && !utf8_mended("caf\xc3\xa9 \xe4\xb8\xad \xf0\x9f\x8e\xae \xef\xbf\xbd"),
          "a script that is UTF-8 is not copied", "");
    for(int i = 0; i < rounds; i++) {
      std::string text;
      const int n = rnd() % 80;
      for(int k = 0; k < n; k++) {
        if(rnd() % 4 == 0) {
          text += (char)(1 + rnd() % 255);
        } else {
          text += letters[rnd() % (sizeof(letters) / sizeof(letters[0]))];
        }
      }
      if(rnd() % 3 == 0 && !text.empty()) {  // (cut anywhere, as a place too short cuts)
        text.resize(rnd() % text.size());
      }
      char* exact = (char*)malloc(text.size() + 1);  // (its own block: a byte read past its 0 is seen)
      memcpy(exact, text.c_str(), text.size() + 1);
      char* got = utf8_mended(exact);
      free(exact);
      check(strict(text) == !got, "a script is mended when, and only when, it is not UTF-8", text);
      if(got) {
        const std::string mended = got;
        check(strict(mended), "a mended script is UTF-8", text);
        std::string plain_in, plain_out;  // what is plain text arrives as it was, in its order
        for(unsigned char c : text) {
          if(c < 0x80) {
            plain_in += (char)c;
          }
        }
        for(unsigned char c : mended) {
          if(c < 0x80) {
            plain_out += (char)c;
          }
        }
        check(plain_in == plain_out && mended.size() <= text.size() * 3, "what is plain in a script arrives as it was", text);
      }
      free(got);
    }
  }
  // ---- the console's language: one the browser has, for any number at all
  static const struct { int console; const char* want; } langs[] = {
    {0, "en"}, {1, "en"}, {2, "fr"}, {3, "es"}, {4, "de"}, {5, "it"}, {6, "nl"}, {7, "pt"}, {8, "en"}, {17, "pt"}, {18, "en"},
    {20, "es"}, {22, "fr"}, {-1, "en"}, {99, "en"}, {0x7fffffff, "en"},
  };
  for(const auto& c : langs) {
    check(!strcmp(lang_of_console(c.console), c.want), "the browser's language for the console's", std::to_string(c.console));
  }
  printf("%s: %d checks, %d failed (names and hosts)\n", failed ? "FAIL" : "PASS", checks, failed);
  return failed ? 1 : 0;
}
'''
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(program.replace('@PARTS@', '\n'.join(parts)))
print('names test written: %s' % sys.argv[1])
