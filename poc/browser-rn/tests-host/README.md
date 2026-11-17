# BrowserPS5 RN: tests on the PC

The parts of the browser that need no console, taken out of `poc/browser` **as they are** (nothing is written again here) and run on the PC with AddressSanitizer and UndefinedBehaviorSanitizer. Since 2026-11-14.

```bash
wsl -e bash -lc "bash build-and-run.sh"
```

`build-and-run.sh [rounds] [scenarios] [first seed] [v] [duo]`: `rounds` is for the first six tests (0 skips them), `scenarios` and `first seed` for the simulation; with `v`, one scenario (that seed) with the browser's own log lines, to see why it failed; with `duo` as the fifth, the scenarios of two downloads at once (alone: `build-and-run.sh 0 1 N v duo`). With neither, both kinds run: `scenarios` of one download, then half as many of two. `bash -lc` so that `PS5_PAYLOAD_SDK` is set (its `zlib.h` is the one `unzip.c` is compiled with); without it the inputs test says SKIPPED.

| Test | What it runs | What it checks |
| --- | --- | --- |
| `make-map-test.py` | `multi_map_text()`, `multi_map_load()`, `error_html_text()` | A map written and read back gives the same missing ranges. Tens of thousands of broken maps (cut short, bytes changed, huge numbers, any bytes): none reads or writes outside its memory, and one that is taken keeps the rules. The text put into the page of a site that did not load is never markup. |
| `make-names-test.py` | `clean_name()`, `unique_path()`, `download_kind()`, `url_host()`, `url_for_log()`, `lang_of_console()`, `utf8_mended()` | A download's name is the server's choice: after cleaning it is only a name (no folder in it, nothing the system takes otherwise, no dot or space at its ends, never longer than its place), valid UTF-8 stays valid, a name that needs nothing comes out as it went in, one too long keeps its end (`.mp4`), cleaning twice changes nothing. A host has nothing of the path, the port or the user in it. Any number the console gives as its language is one the browser has. A script is mended when, and only when, it is not UTF-8, and what is plain in it arrives as it was. |
| `files-fuzz.c` | `settings.c`, `session.c`, `logins.c` whole | The files read at the start (settings, history, the tabs of the last time, saved logins, the extensions' list and scripts), made by the modules' own writers and then broken as a power cut or a full disk would: cut short, bytes changed, any bytes put in, a piece repeated, nothing but chance. No fall; settings read from a broken file save to the same text twice; the tabs read are web addresses that end inside their places; the lists come out as JSON; titles too long for their line are cut between letters. |
| `inputs-fuzz.c` (+ `make-zips.py`, `json-check.h`) | `unzip.c`, `files.c`, `address.c` whole; twice: with the sanitizers, and with glibc's own checks of every place's size | What comes from outside. A `.zip`, whatever its names say (`../`, `/tmp/...`, `C:\`, a link) and however broken, writes only inside the folder it is extracted into, makes no links, and no more bytes than the room it was told there is. A path the phone asks for is taken only if it is really inside a shared folder (links followed) and not in a private one; a Range taken is inside the file; what goes into a header is one line of plain text. A folder's list is JSON whatever its files are called. What is typed becomes an address or a search that ends inside its place; `javascript:` is never an address. |
| `phone-fuzz.c` | `phone.c` and `files.c` whole, started on this machine with one phone paired; the same two builds | The phone's server, asked by someone who is NOT the phone: thousands of requests made from good ones and broken (bytes changed, lengths that lie, headers twice, lines of 60 KiB, requests cut short, tokens that are nearly the right one). Without the phone's token nothing is done on the console and nothing of it is told; no request gets a private folder's file, removes it, or pairs another phone; the server goes on answering the phone after every one of them. |
| `rpc-fuzz.c` | `torrent.c`'s reading of JSON (`torrent_json_string()`, which `torbox.c` reads TorBox's answers with), `torrent_add_json()`, `torrent_add_result()`, `torrent_base64()`; the same two builds | The answers of services, some from the Internet, made of the pieces answers are made of and of anything: a value read ends inside its place, reading never leaves the answer, what is written for the daemon from any link is JSON, base64 has the length it must have. |
| `make-multi-sim.py` + `multi-sim.in.cpp` | The whole downloads' engine (`multi_*`, `download_progress/finished/failed/cancel/pause`, the kept downloads) against a pretend server, a pretend WebKit and a clock of its own | Thousands of seeded scenarios: a server that gives ranges or not, one to any number of connections (changing on the way), busy for a while (503), a link that stops being good (403), a line that carries only so much in all, the line cut or quiet, single connections reset, navigations lost, cancels never answered, the tab busy or closed, the user pausing, cancelling, the browser closing or falling and opening again. **A download that ends "done" has every byte of its file written; one that can go on does end once the line is calm; after a pause or another session every missing byte is asked for; a cancelled one leaves nothing and says "cancelled".** And two downloads at once, begun from the same tab (`run_duo()`): no piece's navigation takes the place of another's in the tab, no byte of one address goes into the other's file, cancelling one leaves the other, both are kept when the browser closes and both go on in the next session. |

## What it found (2026-11-14)

1. **A broken map with huge ranges overflowed a sum** in `multi_map_load()` (undefined behaviour). Numbers over a petabyte are now refused.
2. **A server that gives one connection, after a pause** (seed 2565): one connection brought the file, a second was asked for every 15 s and refused, and at the 21st refusal the download was given up and its file removed with 600 MB in and coming. The fewest connections a server "gives" was taken to be 2; and refusals were counted to 20 though bytes were coming. Fixed in `multi_destination()`.
3. **A cancel the engine never answers** (seed 516658): the download stayed "running" in the list for as long as the browser was open. Now ten seconds with no answer count as cancelled (`multi_tick()`), and a download that ends while its cancel is on the way is cancelled, not saved.
4. **A name ending in a letter of more than one byte lost it** ("resumé" was saved as "resum"; a name in Japanese with no `.ext` lost its last letter), and **a name too long lost its end**, `.mp4` with it. Fixed in `clean_name()` (RN).
5. A size the engine does not tell itself comes from an API of 32 bits: no pieces are cut by it (`download_progress()`).
6. **A script with one byte that is not UTF-8 was dropped whole by the engine** (`WKStringCreateWithUTF8CString()` gives an empty string): a title cut in the middle of a letter, a file named in another encoding, a line of the history broken by a power cut, and a panel (tabs, downloads, the address's suggestions) was told nothing more, with no line in the log. Every script goes through `run_script()`, which now mends it (`utf8_mended()`, RN).
7. **The history cut a title of more than 512 bytes in the middle of a letter.** Now between letters (`put_clean()`, RN).

8. **An answer cut in the middle of `\u12` was read on past its end** (`torrent_json_string()`: `sscanf("%4x")` took what digits there were, and four bytes were skipped all the same), until a quote somewhere in the memory after it; TorBox's answers, from the Internet, are read with it. And the two halves of a letter over U+FFFF (`\ud83c\udfae`, an emoji in a torrent's name) were written as six bytes that are no UTF-8. Fixed in `torrent.c` (RN).

9. **Two downloads in parts at once, from the same tab: the pieces of one took the place of the other's.** The engine keeps one navigation not begun yet a tab; each download asked for its pieces by itself, and the one asked first never came (473 of 1500 scenarios). Eight seconds later it was a failure of its download, which before its speed alone was known went on by one connection for good. Fixed: `multi_page_asked()`, and `multi_claim()` has the others ask when one begins.

10. (Read in the engine's source, not found by a test here.) **`WKStringGetUTF8CString()` writes no ending 0 for a text with a lone surrogate** (a title cut in the middle of an emoji): the place kept what it had after the letters that came. Every text from the engine now goes through `wk_utf8()` (the engine's non-strict form, and an end whatever it answers). The three things the fixes above took for granted of the engine were read there too, and hold.

Nothing in the files read at the start (60 000 rounds of broken files, no fall), nor in `unzip.c`, `files.c` and `address.c` (1.1 million checks), nor in the phone's server (30 000 hostile requests).

One thing left as it is, in code v1 shares: `address_to_url()` writes its last 0 outside a place **shorter than the search engine's address** (every caller's is 2048 bytes or more, so it never happens; the test gives it 80 or more).

## The modules' own unit tests, as RN builds them

```bash
wsl -e bash unit-as-rn.sh
```

`settings_test`, `session_test`, `address_test`, `files_test`, `logins_test`, `phone_test`, `torrent_test`, `torbox_test` (the Makefile's `make test` lines) with `-DAPP_RN`, in a folder of `/tmp`: the fixes behind `APP_RN` must not break what these check. One line fails as it must: `settings_test.c:116` says "fr" is not a language, which is v1's truth (RN has it).

## Reading a failure

`FAIL seed N: ...` — run it alone with the log:

```bash
wsl -e bash build-and-run.sh 0 1 N v
```

The pretend engine's lines start with `[sim]`; the others are the browser's own `POC_LOG` lines, as on the console.

`STOPPED (signal 6) ... at: <function> [<what it was given>]` in the inputs test: glibc stopped the program at a place whose size was told wrong; the line says which call and with what.

What was added to the simulation later (the line's limit, the busy server, the link that stops being good) draws from a second stream of chance, so a seed of before gives the scenario it gave, where none of the new things is drawn for it.

## What it does not cover

The real WebKit and the real network: how the engine really answers a cancel, what a real server does. Those stay with the console tests (`SHOW_DL_DROP`, `SHOW_DL_KEEP`, `SHOW_DL_BIG` for a file of more than 4 GiB).
