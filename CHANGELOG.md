# Changelog

## 1.0.0-rc12 — 2026-11-17

Since rc11:

- **The DualSense's touch pad as a laptop's.** A finger on the pad moves the cursor (slowly for small steps, fast to cross the screen), a tap clicks, and two fingers scroll the page, which glides on a little after they leave. The sticks and ✕ work as before, pressing the pad is still the tabs, and a web game that has the controller is left alone. The cursor's and the scrolling's speed in *Settings → Controller* apply to it too. A file named `no-touch-cursor` in the title's folder turns it off.
- **The controller's keys now reach games and emulators that read a key's code.** The keys the browser sends to a page for the controller (□ as Enter, the D-pad as the arrow keys, and the per-site keyboard: W A S D, space and the others) came with the right `key` and `keyCode` but with `code` "Unidentified", and a page that reads `event.code`, as many web games and emulators do, did not see them. They now carry the code of that key on a US keyboard (`Enter`, `ArrowLeft`, `KeyX`…). Seen on the console with emulators that run in a page and content that is free to give out: JSNES (an NES in JavaScript) ran two homebrew games at 60 frames a second and now answers Start, and v86 (an x86 PC in WebAssembly) started FreeDOS in under 18 s.
- **The starting screen's rings move.** While the browser starts, a light now runs round the blue ring about the globe and the RN letters (once in 4.2 s) and, softer, the other way round the red one, while both rings glow a little brighter and dimmer. Only the rings' own pixels take the light, so the letters and the globe stay in front of them. It costs about 2 ms a frame; the loading bar is unchanged.
- **A message between the engine's processes could arrive without its memory.** The browser carries the engine's messages over its own transport, where shared memory goes by name; the name was taken away when nothing used it any more. A check that ran while a new piece of memory was being made could take that one's name at once, before the other process opened it: the message came without its data (about 1 run in 40 of the tests), and twice a page then stood still, among them a private tab that never loaded. The check now leaves alone what was made or sent after it began (a test on the PC: 82 to 103 names lost in 20 000 before, none after).
- **About** shows the release that is installed and the day the project began (2 November 2026); it showed the day the browser's own program was built, which need not be the release's.
- Tested: the whole set of tests on the console with this engine, title and browser (15 of 15), with no message that lost its memory and no page that stood still in any run; the touch pad and the starting screen's rings seen with the controller in hand.
- Known: the emulators' sound was not measured, and no game was played through with the controller. Tested on one console, firmware 13.60, only.

## 1.0.0-rc11 — 2026-11-16

Since rc10:

- **Emoji in pages.** The console has no emoji font, and the pages' fonts (DejaVu) have only a few (smileys, hearts, stars): 🎮 👍 🚀 🛒 and most others were boxes. The browser now brings Noto Emoji (monochrome, SIL Open Font License) in its own fonts, which the pages fall back to. Seen on the console.
- **The PS5's own symbols in a game's or an app's name** (characters of the private use area, which only the console's interface can draw) are left out of the name on the start page instead of showing as boxes.

## 1.0.0-rc10 — 2026-11-16

Since rc9 (also on one console only):

- **Your games.** "Your games" beside the recent games opens every game and app of the console as a grid of covers, sorted as the console has them, by name or by trophies (the closest to the platinum first), PS5 or PS4 only, found by a part of the name; each opens its own page. Every PS5 game shows how far you are with a bar, and the top says how many trophies you won and how many platinums are in sight.
- **Homebrew.** "For players" → "Homebrew": the scene's tools (etaHEN, websrv with its ported games, elfldr, ftpsrv, shsrv, klogsrv, the payload SDK) with their newest version read from GitHub, "New" for a fresh one or one that came since you last looked, the release notes and the files (a `.elf` goes to the downloads, where "Run" sends it to the console's loader). It also says which of them run on the console now, opens websrv's launcher when it runs and gives ftpsrv's address for a PC.
- **Pages kept with a game.** On a guide or a map, menu → "Keep with the game" keeps that page with the game you are playing (or played last); the game's page lists them, newest first, each a press away and each with "Remove" (up to 30 a game, in the browser's data folder, `game-pages/<id>.txt`). Private tabs are not kept.
- **Your game on the phone.** The phone's page (Settings → Phone) has a card "Your game": pick one of the start page's games and type its notes with the phone's keyboard, keep a guide's link with it (or the page on the TV), open a kept page on the phone or on the TV, remove one. The game's page on the TV shows the change at once.
- **Captures to Discord.** In a game's page, a capture shown whole has "Send to Discord": it goes to a channel of yours through the channel's webhook (Discord → Server Settings → Integrations → Webhooks → Copy Webhook URL), with the game's name, no sign-in. The webhook is asked for once, in a box over the capture or from the phone's page (card "Discord", easier to paste); it stays in the browser's data folder, readable only by the browser, and is never written to its logs. Files of up to 10 MB (a server without boosts); a webhook deleted in Discord is forgotten by itself.
- **"Play" redone.** In rc9, "Play" on a game's page took the console down: the browser asked for the game from its own process while it was still closing. Now the browser closes exactly as "Exit" does and hands the game to a small payload of its own (sent to the console's payload loader, elfldr, port 9021), which waits until the browser has fully gone and the home screen is free, then starts the game; if the system refuses, it says so on the screen. Without a payload loader, "Play" says so and the browser stays open. Tried on the console: the game started about 6 s after the press, with no harm.
- **Patches: the plugin is looked for.** The switches only write files; etaHEN's patches plugin (`itemzflow_xml_patches`) is what applies them in game. The "Patches" panel now says when the plugin is in neither `/data/etaHEN/plugins` nor a USB drive's `etaHEN/plugins` (and has never run), instead of letting the switches look like they work: a box over the switches, and the steps to get it working ("What is missing", or by themselves when a switch is pressed). etaHEN does not publish the plugin ready-made: it is built from etaHEN-Plugins' source. The switches are still kept, for when the plugin comes.
- Known: the patches plugin is not part of this package (etaHEN publishes it only as source). "Send to Discord" was tried against a stand-in server only, never Discord itself. A private tab that never loaded, seen once in rc9's tests, did not come back in rc10's (15 of 15). Tested on one console, firmware 13.60, only.

## 1.0.0-rc9 — 2026-11-16

Since rc8 (also on one console only):

- **A page for each game.** A press on the cover in "Your game" opens the game's own page over the start page: its background, its cover and version, **Play**, its shortcuts, and **its trophies one by one** (PS5 games), the ones still to win first, each with "How to win it" (a search for the trophy and the game); hidden ones show their name after a press. On top, how far you are (per cent, and what is left of each grade). **Notes** for the game, typed with the PS5's keyboard, kept in the browser's data folder (`notes/<id>.txt`). The game's background also stays, faint, behind the whole start page.
- **Your captures of the game** on its page: its own screenshots and clips, the newest first, eight at a time; a press opens one whole, with the one before and the one after a press away, and a clip plays there. HDR screenshots are counted, not shown.
- **Patches.** Beside "Cheats", "Patches" lists the patches the scene keeps for etaHEN's patches plugin (60 FPS and the like, github.com/illusionyy/PS-Game-Patch) for that very game, its version's first, each with a switch. Turning one on writes the switch where the plugin reads it (`/data/etaHEN/patches/settings/`) and, the first time, puts the game's patch file in `/data/etaHEN/patches/xml/` (PS4 games) or `xml_prospero/` (PS5). The plugin must be installed in etaHEN; patches of another version of the game are shown faded and can only be turned off.
- **The console's health.** "For players" → "Console": the processor's and the chip's temperatures, power use by part, clock, the fan's target, time since it was turned on, the disks, the address; only read, the fan is never touched. A **network test** in the same panel (latency, and download speed for up to 5 s against speed.cloudflare.com).
- **The controller as a keyboard, per site.** Menu → "Controller as keyboard" (arrows, then W A S D, then off), kept for each site: on a site with it on, the touch pad gives the controller to the page and each button is a real key that goes down and up (D-pad and left stick = arrows or WASD, X = space, square/triangle/circle = z/x/c or j/k/l, L1/R1 = q/e, L2 = Shift, R2 = Enter, Options = Esc), for the web games that want a keyboard.
- **The bar after a saved file.** After a cheat or a patch file was saved (an address that becomes a download), the bar showed the file's address over the start page; it shows the page's own again.
- Known: "Play" has not been tried on the console yet (it closes the browser the clean way and then asks the system to start the game); the captures gallery has only been seen with test pictures, the console it was tried on had no captures.

## 1.0.0-rc8 — 2026-11-15

Since rc7 (also on one console only):

- **A browser for players.** A browser on a PC does not know what its user plays; this one reads it from the PS5 itself (only reads). The start page has a "Your game" row: the game open or played last, with its cover, its own background, your trophies of it (PS5 games), and one press to its guide, cheats, trophies, videos, wiki, maps, length, news, who is live with it, X and Reddit; the other recent games are a press away. Under it, "For players": new releases, deals, the month's PS Plus games, news, live streams, games that play in the browser, and a network test.
- **Cheats.** The game's "Cheats" opens a panel with the cheat files the scene keeps for etaHEN's cheats menu (github.com/etaHEN/PS5_Cheats) for that very game, says which version the console has, and "Save" puts the file where etaHEN looks for it (`/data/etaHEN/cheats/`). Nothing else can be saved that way: only a cheat file's name, only from the browser's own start page, only from that repository. The files are not part of this package; they are downloaded when you ask.
- **The PS5's captures when a site asks for a file.** The screenshots and the clips come as one list, the newest first, each with its game's name and its time (the PS5 keeps them many folders deep). HDR screenshots (`.jxr`) are left out for now: no site takes them.
- **The ad blocker's second part.** What uBlock Origin Lite hides inside the pages is now hidden here too (about 29 000 rules, the ones for sites that change their ending included), and a page of another site can no longer take the tab to one of about 46 000 advertising hosts: the tab stays where it is and the bar says so. An address you type always opens. Left out: the rules that need uBlock's own scripts.
- **An address asked for while the page before is still busy.** A page that moves inside itself (Telegram Web when a message comes) cancelled the load of the address just asked for, and the browser stayed where it was. It is asked for again, up to three times.
- Known: with the hiding rules on, the page engine failed twice in the first two runs of the sites test (once it crashed, once it stopped answering) and never again in the seven runs after; if it shows up, the file `no-hide-rules` in the browser's data folder turns the hiding off.

## 1.0.0-rc7 — 2026-11-14

Since rc6 (also on one console only):

- **Closing from the PS5's menu, again.** rc6's browser left a second after the home screen came over it and needed more than a second to be gone, so a quick "Close" on its tile still found it, and the console went down. It now leaves 0.4 s after the home screen and is gone about half a second later (0.9 to 1.4 s in all, measured on the console), also when the home screen comes while it is still starting. Closed from its tile in that time, by hand, it closed with no harm.
- **None of the app's memory in the browser's own process while the PS5's menu is over it.** The browser's process had the app's pictures of the pages and its video ring mapped all the time; it lets go of them when the system's menu, the home screen or the PS5's keyboard comes over the app, and maps them again on the way back.
- **A browser left without its app leaves at once.** If the app goes first (closed by the system, or after a crash of its own), the browser's process no longer goes on by itself for seconds: it keeps what the unfinished downloads need, writes what it did to `last-exit.txt` in its data folder and ends. This path has not run on a console yet.
- Known: the browser cannot stay open behind the PS5's home screen yet. Kept there for a test and closed from its tile, it closed, and the console went down some time later; why is not known. So it leaves by itself, and its tabs come back the next time.

## 1.0.0-rc6 — 2026-11-14

Since rc5 (also on one console only):

- **The PS5's own keyboard.** Text is typed on the system keyboard, with its suggestions and its layout: the title now loads its library itself (firmware 13.60; on another firmware the browser's own keyboard stays). Passwords still get the browser's keyboard, which hides what is typed. "PS5 keyboard" in the settings turns it off.
- **Closing from the PS5's menu.** Closing the browser from its tile on the home screen could take the console down (it always could). The browser now quits by itself, the clean way, a second after the home screen comes over it, so the system finds nothing to close; it also means another game can be started safely. Its tabs come back the next time. The quick menu, a notification or the keyboard do not close it.
- **Behind the PS5's menu** every video pauses, the ones in players embedded from other sites too, and the video decoder has let go of the GPU before anything else happens.

## 1.0.0-rc5 — 2026-11-14

Since rc4 (also on one console only), from the same day's use:

- **An ad blocker with real lists.** Besides its own short list of hosts, the browser now carries uBlock Origin Lite's network rules (ublock-filters, EasyList and Peter Lowe's list: 59 105 rules for the engine's own content blocker). The engine prepares them the first time the browser starts after an install (about half a minute, in the background); after that they only load. "Ads here" in the page's menu still lets a site be.
- **Windows that pages open by themselves.** A window a page's script asked for after a press used to take the tab's own place: the first press on a film's page replaced the film with an advertisement. Now a window of the same site opens in a new tab, and one of another site is not opened (the bar says so). Sign-in pages of the usual services are let through, and the setting that lets sites open windows still does.
- **Pages that went to about:blank after a second.** Sites that look for a browser's developer tools found the browser's own developer console (when it was on in the settings) and sent the page away. The console no longer touches anything a page can watch.

## 1.0.0-rc4 — 2026-11-14

Since rc3 (also on one console only), after its first evening of films on a TV:

- **Back in the Games tab.** The browser is the title `PPSA97024` again. As a media title (`PPSA97025`, rc1 to rc3) it had 4 GB of memory in all, 3 of them for the pages, the films and the video decoder together: the second film of a session could run it out, and the browser closed. As a game it has 11.7 GB. A console that had rc1 to rc3 keeps the old icon in Media as an empty shell: delete it there (Options).
- **A black box over the video** whenever a player showed its subtitles, or the label of its seek bar: fixed in the engine. Any element with a frosted-glass effect (`backdrop-filter`) over a video, or a filter, a mask or an opacity around it, moved the video out of its place.
- **1080p H.264 films** lost their 8 bottom rows and were stretched to fill them.
- A film keeps at most 128 MB of video in memory (it was about 300), and when memory is low the pages' unused objects are collected at once.
- The video decoder lets go of everything it holds on the GPU when the browser closes.

## 1.0.0-rc3 — 2026-11-14

Since rc2 (also on one console only): the same browser, packed more carefully.

- The paths of the build machine, and with them the name of the account it was built on, are out of every binary of the pack (they were in the engine's library and in the title, some 2400 times each). The build now refuses to make a pack that has them.
- Credits: PlutoVG, which draws the starting screen, is named in `THIRD-PARTY-NOTICES.md`.
- `send.ps1` shows a neutral example address.

## 1.0.0-rc2 — 2026-11-14

Since rc1 (a pack that went to one console only):

- **7 languages**: English, Português, Español, Français, Deutsch, Italiano, Nederlands, for the pages, the menus and the browser's own messages; a new install starts in the console's language.
- **Error pages** of its own when a site does not load, and no padlock on an address that never came.
- **Security**: a site could reach the console's own services (FTP, websrv) by asking for a new tab after any press on its page. Closed, with the other ways round the same rule (reload on an error page, "Try again" on a download a site began there, a link from TorBox). The switch that keeps pages away from those services is now put back by the browser if it is missing.
- **Two large downloads at once** from the same page no longer take each other's connections.
- Downloads: a server that gives one connection is no longer asked for a second for ever (the download was given up, its file removed); a cancel the engine does not answer still cancels; a file's name keeps its last letter and, when too long, its extension.
- A title cut in the middle of a letter or of an emoji, or a file named in another encoding, no longer stops the bar, the tabs or the downloads from being updated, nor shows as garbage in the history.
- Torrents and TorBox: a name with an emoji is shown as it is; an answer cut short is no longer read past its end.
- Credits: PS5_Vulkan, ps5-native-app-boilerplate and Parallax Browser are named in `THIRD-PARTY-NOTICES.md`, with what PDF.js carries.

## 1.0.0-rc1 — 2026-11-14

The first release candidate. Everything is new; what stands out:

- **One file to install.** `BrowserPS5.elf` carries the whole browser, packed, and writes it in half a minute with any payload sender. `BrowserPS5-uninstall.elf` takes it off again and keeps your data; `BrowserPS5-uninstall-all.elf` removes that too.
- **In the Media tab.** The browser is a media title (`PPSA97025`), beside your game.
- **The full engine.** WebKit 627.1.7 with the JavaScript JIT, WebGL 1 and 2, Web Audio, HTTP/2, service workers for the sites that need them, a PDF reader.
- **Video.** H.264 and VP9 on the PS5's video decoder, YouTube up to 2160p, and YouTube's TV interface with the D-pad.
- **WhatsApp Web and Telegram Web**, with site notifications as PS5 notifications (the browser asks first: Allow or Block).
- **Downloads.** Up to 8 connections for large files; a network that drops or goes silent no longer loses a download; Pause and Continue; an unfinished download goes on after the browser is closed and opened again.
- **Tabs** that stay loaded, private tabs, the tabs of last time, a start page in the PS5's style, favorites, history, saved logins, find in page, reading mode, translate, an ad blocker and user-script extensions.
- **DualSense**: cursor with a magnet, D-pad navigation, radial menus on the adaptive triggers, haptics, light bar, its own keyboard.
- **Your phone** as keyboard and remote, and *Open beside the game*.
- **Safer on a jailbroken console**: pages cannot reach the console's own local services; certificates are always verified.
- Portuguese and English.

Known limits are in the README, under *What it does not do*.
