"""What the start page may ask the app to fetch or write for etaHEN (main.cpp cheat_ask(), patch_ask(), patch_set(),
patches_tell()), tested on the PC (2026-11-15).

These commands (browserps5:cheat?..., patch?..., patch-set?..., patches?...) are the only way a page makes the
browser write into etaHEN's folders, so their checks are what keeps a wrong or hostile text from becoming a path
or an address. This cuts that code out of ../../browser/main.cpp as it is (with etaHEN's folder moved into the
test's own folder) and checks, with AddressSanitizer and UndefinedBehaviorSanitizer, on fixed cases and on many
made by chance:

  - what is right is taken: the address is the repository's, the folder and the name the ones etaHEN reads;
  - whatever is asked, an address that is loaded is ALWAYS that repository's and a plain file name, the folder
    one of the known ones, the name a game's id and its ending;
  - a switch that is written is ALWAYS "0x<16 hex>.txt" in the settings folder, holding "1" or "0", and nothing
    else appears anywhere (the test's folder is walked at the end, and the folder above it);
  - what the page is told back is always the same shape (a call with a game's id, true/false, 0s and 1s);
  - asked by another page than the browser's own start page, nothing happens; without etaHEN's folder, nothing
    is fetched or written.

    python3 make-asks-test.py <out.cpp>
"""
import sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, '..', '..', 'browser', 'main.cpp'), encoding='utf-8').read()


def function(head):
    a = src.index(head)
    return src[a:src.index('\n}\n', a) + 3]


a = src.index('#define CHEATS_REPO "')
b = src.index('\nstatic void games_stale(void);\n', a)
block = src[a:b]
home = '#define CHEATS_HOME "/data/etaHEN"'
assert block.count(home) == 1, "CHEATS_HOME is not where it was"
block = block.replace(home, '#define CHEATS_HOME "etahen"')
for name in ('cheat_ask(', 'patch_ask(', 'patch_set(', 'patches_tell(', 'patch_parts(', 'patch_number_ok('):
    assert name in block, name

program = r'''// MADE by make-asks-test.py from browser/main.cpp: the code as it is, with a test around it.
#define APP_RN 1
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <regex>
#include <string>
#include <vector>

// What the cut code leans on, as plain as can be.
#define PAGE 0
#define START_URL "browserps5:start"
#define POC_LOG(...) do { logs++; } while(0)
static unsigned logs;
static double poc_now_ms(void) { return 1000.0; }
static struct { char downloads_dir[64]; } settings = {"downloads"};
struct text_t { std::string s; };
static void text_add(text_t* t, const char* s) { t->s += s; }
static void __attribute__((format(printf, 2, 3)))
text_printf(text_t* t, const char* format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  t->s += buffer;
}
static void text_json(text_t* t, const char* s) {
  t->s += '"';
  for(const unsigned char* c = (const unsigned char*)s; *c; c++) {
    char u[8];
    if(*c == '"' || *c == '\\' || *c < 0x20) {
      snprintf(u, sizeof(u), "\\u%04x", *c);
      t->s += u;
    } else {
      t->s += (char)*c;
    }
  }
  t->s += '"';
}
static std::string told;   // the last script for the page
static unsigned tolds;
static void text_run(int, text_t* t) { told = t->s; tolds++; t->s.clear(); }

@SAME@
@BLOCK@

static bool own = true;
static std::string loaded;  // the last address asked of the engine
static unsigned loads;
static void load_url(const char* url) { loaded = url; loads++; }
static bool own_page_shown(const char*) { return own; }

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

static const std::regex cheat_url("https://raw\\.githubusercontent\\.com/etaHEN/PS5_Cheats/main/(json|shn|mc4)/[A-Z]{4}[0-9]{5}_[0-9A-Za-z.]+\\.(json|shn|mc4)");
static const std::regex cheat_name("[A-Z]{4}[0-9]{5}_[0-9A-Za-z.]+\\.(json|shn|mc4)");
static const std::regex patch_url("https://raw\\.githubusercontent\\.com/illusionyy/PS-Game-Patch/main/patches/(xml|xml_prospero)/[A-Za-z0-9_-][A-Za-z0-9_.-]*\\.xml");
static const std::regex patch_name("[A-Z]{4}[0-9]{5}\\.xml");
static const std::regex set_told("window\\.bps5PatchSet && bps5PatchSet\\(\"[A-Z]{4}[0-9]{5}\", \"[0-9a-f]{16}\", (true|false), (true|false)\\)");
static const std::regex state_told("window\\.bps5Patches && bps5Patches\\(\"[A-Z]{4}[0-9]{5}\", \\{saved: (true|false), etahen: [0-2], on: \"[01]{0,400}\"\\}\\)");
static const std::regex switch_name("0x[0-9a-f]{16}\\.txt");

static std::string
file_text(const std::string& path) {
  std::string out;
  FILE* f = fopen(path.c_str(), "rb");
  if(f) {
    char buffer[64];
    size_t n;
    while((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
      out.append(buffer, n);
    }
    fclose(f);
  }
  return out;
}

// Whatever was asked: what was loaded and what the page was told are of the known shapes.
static void
after(const char* what, const std::string& arg, unsigned loads_before, unsigned tolds_before, int kind) {
  if(loads != loads_before) {
    const bool cheat = kind == 0;
    check(std::regex_match(loaded, cheat ? cheat_url : patch_url) && loaded.find("..") == std::string::npos, "an address that is not the repository's was loaded", arg + " -> " + loaded);
    check(std::regex_match(std::string(cheat_want.name), cheat ? cheat_name : patch_name), "the name the file gets is not a cheat's or a game's", arg + " -> " + cheat_want.name);
    const std::string dir = cheat_want.dir;
    check(cheat ? dir == "etahen/cheats/json" || dir == "etahen/cheats/shn" || dir == "etahen/cheats/mc4" || dir == "downloads"
                : dir == "etahen/patches/xml" || dir == "etahen/patches/xml_prospero", "the folder the file goes to is not one of etaHEN's", arg + " -> " + dir);
    check(loaded == cheat_want.url, "the address loaded is not the one waited for", arg);
    check(strlen(cheat_want.url) < sizeof(cheat_want.url) - 1, "the address filled its place (cut?)", arg);
  }
  if(tolds != tolds_before) {
    check(std::regex_match(told, kind == 2 ? set_told : state_told), what, arg + " -> " + told);
  }
}

// Every file and folder under dir is one this code may make.
static void
walk(const std::string& dir, const std::string& shown) {
  DIR* d = opendir(dir.c_str());
  if(!d) {
    return;
  }
  while(struct dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if(name == "." || name == "..") {
      continue;
    }
    const std::string path = dir + "/" + name, as = shown + "/" + name;
    struct stat st;
    if(stat(path.c_str(), &st)) {
      continue;
    }
    if(S_ISDIR(st.st_mode)) {
      check(as == "/etahen" || as == "/etahen/cheats" || as == "/etahen/patches" || as == "/etahen/patches/settings", "a folder that should not be there", as);
      walk(path, as);
    } else {
      const std::string text = file_text(path);
      check(shown == "/etahen/patches/settings" && std::regex_match(name, switch_name) && (text == "0\n" || text == "1\n"),
            "a file that should not be there, or a switch that does not hold 0 or 1", as + " = " + text);
    }
  }
  closedir(d);
}

static const char* const ids[] = {"CUSA00900", "PPSA01342", "CUSA00001", "PPSA99999", "cusa00900", "CUSA0090", "CUSA009000", "CUSAA0900", "", "CUSA00900\n", "\xc3\x87USA00900"};
static const char* const kinds[] = {"xml", "xml_prospero", "xml_prosper", "XML", "xml/", "", "json", "settings", "..", "xml_prospero_"};
static const char* const names[] = {"Bloodborne-Orbis.xml", "DemonSouls.xml", "a.xml", ".xml", "..xml", "a..xml", ".a.xml", "a.xm", "a.xmlx", "a b.xml", "a/b.xml", "../a.xml",
                                    "..%2F..%2Fetc%2Fpasswd.xml", "a.xml/", "a.xml\n", "a\\b.xml", "a.xml?x=1", "a.xml#x", "\xc3\xa9.xml", "A_b-c.9.xml", "settings/0x0000000000000000.txt"};
static const char* const numbers[] = {"00000000b5505e57", "0000000000000000", "ffffffffffffffff", "00000000B5505E57", "00000000b5505e5", "00000000b5505e570", "../../../../etc",
                                      "0x00000000b5505e", "", "00000000b5505e5g", "0000000 b5505e57"};
static const char* const bits[] = {"=", "==", "/", ",", "=1", "=0", "=2", "=1x", "%00", "%2F", "..", "\x01", "\x7f", "\xff", " ", "\t", "?", "#", "&"};
#define PICK(list) list[rnd() % (sizeof(list) / sizeof(list[0]))]

static std::string
made(void) {
  std::string s;
  switch(rnd() % 6) {
  case 0: s = std::string(PICK(ids)) + "=" + PICK(kinds) + "/" + PICK(names); break;
  case 1: s = std::string(PICK(ids)) + "=" + PICK(kinds) + "=" + PICK(numbers) + PICK(bits); break;
  case 2:
    s = std::string(PICK(ids)) + "=" + PICK(kinds) + "=";
    for(unsigned i = rnd() % 5; i; i--) {
      s += std::string(PICK(numbers)) + (rnd() % 5 ? "," : PICK(bits));
    }
    break;
  case 3: s = std::string(PICK(ids)) + "_" + (rnd() % 2 ? "01.00" : "01.026.000") + (rnd() % 3 ? ".json" : rnd() % 2 ? ".shn" : PICK(bits)); break;
  case 4:
    for(unsigned i = 1 + rnd() % 6; i; i--) {
      switch(rnd() % 5) {
      case 0: s += PICK(ids); break;
      case 1: s += PICK(kinds); break;
      case 2: s += PICK(names); break;
      case 3: s += PICK(numbers); break;
      default: s += PICK(bits);
      }
    }
    break;
  default:
    for(unsigned i = rnd() % 300; i; i--) {
      s += (char)(1 + rnd() % 255);
    }
  }
  if(rnd() % 40 == 0) {
    s += std::string(rnd() % 900, rnd() % 2 ? 'a' : '=');
  }
  return s;
}

static void
ask_all(const std::string& arg) {
  // (each on a copy that ends where the text ends: a byte read past it is seen)
  char* copy = strdup(arg.c_str());
  unsigned l = loads, t = tolds;
  cheat_ask(copy);
  after("", arg, l, t, 0);
  l = loads; t = tolds;
  patch_ask(copy);
  after("", arg, l, t, 1);
  l = loads; t = tolds;
  patch_set(copy);
  after("what the page is told of a switch is not of the known shape", arg, l, t, 2);
  l = loads; t = tolds;
  patches_tell(copy);
  after("what the page is told of a game's patches is not of the known shape", arg, l, t, 3);
  free(copy);
}

int
main(int argc, char** argv) {
  if(argc < 3 || chdir(argv[1])) {
    printf("asks_test <an empty folder inside an empty folder> <rounds>\n");
    return 2;
  }
  const long rounds = atol(argv[2]);

  // 1. Without etaHEN's folder: nothing is fetched for the patches, nothing is written.
  unsigned l = loads, t = tolds;
  patch_ask("CUSA00900=xml/Bloodborne-Orbis.xml");
  check(loads == l, "a patch file was fetched without etaHEN's folder", "");
  patch_set("CUSA00900=xml=00000000b5505e57=1");
  check(tolds == t + 1 && told == "window.bps5PatchSet && bps5PatchSet(\"CUSA00900\", \"00000000b5505e57\", false, false)", "a switch without etaHEN's folder: not told as not written", told);
  struct stat st;
  check(stat("etahen", &st) != 0, "etaHEN's folder was made by the browser", "");
  cheat_ask("CUSA01765_01.00.json");  // (a cheat file then goes to the downloads)
  check(loads == l + 1 && !strcmp(cheat_want.dir, "downloads"), "a cheat file without etaHEN's folder does not go to the downloads", cheat_want.dir);

  // 2. With it: what is right is taken.
  mkdir("etahen", 0777);
  l = loads;
  patch_ask("CUSA00900=xml/Bloodborne-Orbis.xml");
  check(loads == l + 1 && loaded == "https://raw.githubusercontent.com/illusionyy/PS-Game-Patch/main/patches/xml/Bloodborne-Orbis.xml" &&
        !strcmp(cheat_want.dir, "etahen/patches/xml") && !strcmp(cheat_want.name, "CUSA00900.xml"), "a PS4 game's patch file is not asked for as it should", loaded);
  check(cheat_wanted(loaded.c_str()), "the address just asked for is not the one waited for", loaded);
  l = loads;
  patch_ask("PPSA01342=xml_prospero/DemonSouls.xml");
  check(loads == l + 1 && loaded == "https://raw.githubusercontent.com/illusionyy/PS-Game-Patch/main/patches/xml_prospero/DemonSouls.xml" &&
        !strcmp(cheat_want.dir, "etahen/patches/xml_prospero") && !strcmp(cheat_want.name, "PPSA01342.xml"), "a PS5 game's patch file is not asked for as it should", loaded);
  l = loads;
  patch_ask((std::string("CUSA00900=xml_prospero/") + std::string(96, 'n') + ".xml").c_str());  // the longest name taken
  check(loads == l + 1 && loaded.size() == 72 + 13 + 100 && loaded.size() < sizeof(cheat_want.url), "the longest patch file name is not taken whole", loaded);
  l = loads;
  patch_ask((std::string("CUSA00900=xml_prospero/") + std::string(97, 'n') + ".xml").c_str());
  check(loads == l, "a patch file name longer than the longest was taken", "");
  t = tolds;
  patch_set("CUSA00900=xml=00000000b5505e57=1");
  check(tolds == t + 1 && told == "window.bps5PatchSet && bps5PatchSet(\"CUSA00900\", \"00000000b5505e57\", true, false)" &&
        file_text("etahen/patches/settings/0x00000000b5505e57.txt") == "1\n", "a switch turned on is not written and told", told);
  patches_tell("CUSA00900=xml=00000000b5505e57,00000000b5505e58");
  check(told == "window.bps5Patches && bps5Patches(\"CUSA00900\", {saved: false, etahen: 1, on: \"10\"})", "the switches of a game are not told as they are", told);
  patch_set("CUSA00900=xml=00000000b5505e57=0");
  check(told == "window.bps5PatchSet && bps5PatchSet(\"CUSA00900\", \"00000000b5505e57\", false, false)" &&
        file_text("etahen/patches/settings/0x00000000b5505e57.txt") == "0\n", "a switch turned off is not written and told", told);
  mkdir("etahen/patches/xml", 0777);
  fclose(fopen("etahen/patches/xml/CUSA00900.xml", "w"));
  patches_tell("CUSA00900=xml=00000000b5505e57");
  check(told == "window.bps5Patches && bps5Patches(\"CUSA00900\", {saved: true, etahen: 1, on: \"0\"})", "a game's file on the console is not told", told);
  patches_tell("CUSA00900=xml_prospero=");
  check(told == "window.bps5Patches && bps5Patches(\"CUSA00900\", {saved: false, etahen: 1, on: \"\"})", "a game with no switches asked is not told as such", told);
  unlink("etahen/patches/xml/CUSA00900.xml");
  rmdir("etahen/patches/xml");
  {
    std::string many = "CUSA00900=xml=";
    for(int i = 0; i < 450; i++) {
      many += "00000000b5505e57,";
    }
    patches_tell(many.c_str());
    check(std::regex_match(told, state_told), "more switches than the most asked for: not told in the known shape", told);
  }

  // 3. Asked by another page: nothing.
  own = false;
  l = loads; t = tolds;
  cheat_ask("CUSA01765_01.00.json");
  patch_ask("CUSA00900=xml/Bloodborne-Orbis.xml");
  patch_set("CUSA00900=xml=00000000b5505e58=1");
  patches_tell("CUSA00900=xml=00000000b5505e57");
  check(loads == l && tolds == t && stat("etahen/patches/settings/0x00000000b5505e58.txt", &st) != 0, "another page than the start page was answered", "");
  own = true;

  // 4. What is wrong is not taken (one by one, so the one that is says which).
  static const char* const wrong_files[] = {"CUSA00900=xml/../a.xml", "CUSA00900=xml/..%2F..%2Fetc%2Fpasswd.xml", "CUSA00900=xmlx/a.xml", "cusa00900=xml/a.xml",
    "CUSA0090=xml/a.xml", "CUSA00900=xml/a.xm", "CUSA00900=xml/.xml", "CUSA00900=xml/.a.xml", "CUSA00900=xml/a/b.xml", "CUSA00900=xml/a b.xml", "CUSA00900=xml_prospero",
    "", "=", "CUSA00900", "CUSA00900=", "CUSA00900=xml", "CUSA00900=xml/", "CUSA00900=/a.xml", "CUSA00900=xml/a.xml?b", "CUSA00900=xml/a..xml", "CUSA00900=settings/a.xml",
    "CUSA00900=xml/a.xml\n", "CUSA00900=xml/\xc3\xa9.xml", "CUSA00900=xml=00000000b5505e57=1", "CUSA00900=xml//a.xml", "CUSA00900=xml/a\\b.xml"};
  for(const char* w : wrong_files) {
    l = loads;
    patch_ask(w);
    check(loads == l, "a wrong patch file was asked of the engine", w);
  }
  static const char* const wrong_switches[] = {"CUSA00900=xml=00000000B5505E57=1", "CUSA00900=xml=00000000b5505e57=2", "CUSA00900=xml=00000000b5505e57=", "CUSA00900=xml=00000000b5505e57",
    "CUSA00900=xml=00000000b5505e5=1", "CUSA00900=xml=00000000b5505e570=1", "CUSA00900=xml=../../../../etc=1", "CUSA00900=xml=00000000b5505e57=1x", "CUSA00900=xml=00000000b5505e57=11",
    "CUSA00900=json=00000000b5505e57=1", "cusa00900=xml=00000000b5505e57=1", "CUSA00900=xml=0x000000b5505e57=1", "CUSA00900=xml==1", "", "=", "===", "CUSA00900=xml=/../../../x=1",
    "CUSA00900=xml=00000000b5505e5\n=1", "CUSA00900=xml=00000000b5505e57=1=1"};
  const std::string before = file_text("etahen/patches/settings/0x00000000b5505e57.txt");
  for(const char* w : wrong_switches) {
    t = tolds;
    patch_set(w);
    check(tolds == t && file_text("etahen/patches/settings/0x00000000b5505e57.txt") == before, "a wrong switch was taken", w);
  }

  // 5. Many made by chance, with etaHEN's cheats folder there or not.
  for(long i = 0; i < rounds; i++) {
    if(i == rounds / 2) {
      mkdir("etahen/cheats", 0777);
    }
    own = rnd() % 16 != 0;
    const unsigned l0 = loads, t0 = tolds;
    const std::string arg = made();
    ask_all(arg);
    if(!own) {
      check(loads == l0 && tolds == t0, "another page than the start page was answered", arg);
    }
  }
  own = true;
  walk(".", "");
  {  // and nothing beside the test's own folder (a ".." that got through would land there)
    int beside = 0;
    DIR* d = opendir("..");
    while(d) {
      struct dirent* e = readdir(d);
      if(!e) {
        break;
      }
      beside += strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    }
    if(d) {
      closedir(d);
    }
    check(beside == 1, "something was made beside the test's own folder", std::to_string(beside));
  }
  printf("%s: what the start page may ask for etaHEN: %d checks, %d failed (%ld made by chance; %u addresses asked of the engine, %u answers to the page)\n",
         failed ? "FAIL" : "PASS", checks, failed, rounds, loads, tolds);
  return failed ? 1 : 0;
}
'''

program = program.replace('@SAME@', function('static bool\nsame_address(')).replace('@BLOCK@', block)
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(program)
