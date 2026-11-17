# BrowserPS5 RN: the languages

The browser's pages are written in Portuguese. English is in `poc/browser/lang.js` (`LANG_EN`, by the Portuguese text), with RN's own texts in `../pages/lang-rn.js`. The other languages (Spanish, French, German, Italian, Dutch, since 2026-11-14) are here.

| File | What it is |
| --- | --- |
| `<code>.json` | A language: `texts` (the Portuguese text → its translation), `patterns` (texts with `%s`/`%d`), `days`, `months`, `day`. **This is what to edit.** |
| `meta.json` | Each language's name, days, months and how a day is written |
| `<code>.txt` | The numbered list a language was first written from (`from-list.py`); kept for reference |
| `from-list.py` | `python from-list.py --list` prints the numbered English texts; `python from-list.py <code>` turns `<code>.txt` into `<code>.json` |
| `make-lang-more.py` | `*.json` → `../pages/lang-more.js`, which `build-rn.sh` puts at the end of the copy's `lang.js` |
| `../pages/lang-rn.js` | The machinery: a language's table is a function, called the first time it becomes the page's language; what a table has not comes in English |
| `../pages/lang_c.h` | What the app itself says (its notices, the questions' windows, the page of a site that did not load): one row per English text, five columns (es, fr, de, it, nl) |

## After any change

```bash
python make-lang-more.py
```

```bash
node lang_more_test.js
```

```bash
python lang_c_test.py
```

- `lang_more_test.js`: Portuguese and English are exactly what `lang.js` alone gives; every table's texts are texts of the pages, none empty, none that the page would translate again, with the same `%s`/`%d`.
- `lang_c_test.py`: every `LM("...")` of `main.cpp` has its row in `lang_c.h`, with the same marks.
- `node pages_syntax_test.js <folder of *.built.html>`: every script of the built pages parses.

## A new text in the pages

Write it in Portuguese in the page, add its English to `lang.js` (or `lang-rn.js` for RN's own), and its translation to each `<code>.json` (a text that is missing there shows in English). For a text the app writes by itself in C: `lang_en() ? LM("English") : "Português"`, and a row in `lang_c.h`.

## A new language

A new `<code>.json` and its entry in `meta.json`; the code in `settings.c` (`langs[]`), in `main.cpp` (`lang_code()`, `lang_more()`'s columns, the languages sites are told) and a sixth column in `lang_c.h`; its choice in `settings.html` and `start.html`; its words in `keyboard.html` (`COMMON_MORE`) and `ext/linkmenu.js`.
