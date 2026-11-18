# BrowserPS5 RN — a real web browser for your PS5

BrowserPS5 RN is a full web browser for jailbroken PS5 consoles: its own port of a current WebKit, with the JavaScript JIT, drawn on the console's GPU, driven with the DualSense. It is a **native title** (`PPSA97024`) in the home screen's **Games** tab. It is not the console's hidden system browser: the engine, the interface and every feature below come with it.

> [!WARNING]
> **Read first.** BrowserPS5 RN is an unofficial homebrew project. It is **not affiliated with, endorsed by or connected to Sony Interactive Entertainment, Apple, the WebKit project or any site it opens**. It needs a jailbroken PS5. **Use it at your own risk**: you alone are responsible for your console and for the accounts you sign in to. It was tested on firmware 13.60 only.

---

## Contents

- [Features](#features)
- [Technical specifications](#technical-specifications)
- [Requirements](#requirements)
- [Installation](#installation)
- [Controls](#controls)
- [Downloads](#downloads)
- [Updating and uninstalling](#updating-and-uninstalling)
- [Files on the console](#files-on-the-console)
- [Privacy and security](#privacy-and-security)
- [What it does not do](#what-it-does-not-do)
- [Troubleshooting](#troubleshooting)
- [Building from source](#building-from-source)
- [Licences](#licences)

---

## Features

### A browser, the way you know one

- **Tabs** that stay loaded (up to 6), **private tabs** that write nothing, a button to reopen the tab you just closed, and **the tabs of last time** when you open it again.
- **Start page** in the style of the PS5's home screen, with your favorites as tiles, and a **favorites bar** under the address.
- **Address bar** with search (Google, DuckDuckGo, Bing, Ecosia, Startpage or Brave Search) and suggestions from your favorites and history.
- **Find in page**, **reading mode**, **translate the page**, zoom, desktop or mobile version of a site, dark mode for sites, full screen, and a **QR code** of the page's address to carry on with it on a phone.
- A menu on any link or picture (hold ✕): open in a new tab, in a private tab, save the picture.
- **History**, **saved logins** that fill themselves in, and "Clear browsing data".
- **Ad blocker** built on WebKit's own content rules, with a list of sites where ads stay on, and **extensions** as user scripts (a Greasy Fork manager is included).
- **PDF reader** (PDF.js): a PDF a page links to opens in the browser.
- **Clear error pages** when a site does not load: what happened, in your language, with a button to try again. There is no way past a certificate that fails.
- **7 languages**: English, Português, Español, Français, Deutsch, Italiano and Nederlands, for the pages, the menus and the browser's own messages. A new install starts in your console's language. Sites are told the language you chose.
- **Updates by themselves**: a newer release is fetched in the background and installed the next time you open the browser (*Updating and uninstalling*, below).

### Video, sound and graphics

- **YouTube up to 2160p**, with H.264 and VP9 decoded by the PS5's own video hardware.
- **YouTube's TV interface** (`youtube.com/tv`) from a tile on the start page, driven with the D-pad like the console's own app.
- **WebGL 1 and 2**, **Web Audio**, Media Source Extensions and the Media Session API.

### Messaging sites

- **WhatsApp Web** and **Telegram Web** work: QR code linking, chats, files.
- **Site notifications become PS5 notifications.** The first time a site wants to send them, the browser asks: Allow or Block. Settings → Privacy lists the sites you answered.
- **Send files to a page** from the console: your PS5 screenshots and video clips, your downloads, a USB drive.

### Downloads that finish

- **Several connections at once** for large files (200 MB or more, up to 8): about three times as fast as one connection on the same line.
- **A failing network does not lose a download.** Connections that drop or go silent are replaced, for as long as it takes, and the download goes on from the bytes it already has.
- **Pause and continue**, and **continue after closing the browser**: an unfinished download is listed as *Interrupted* the next time, with a button to go on from where it stopped.
- Choose the folder for each file, or let each type go to its own folder; USB drives are offered; `.zip` files can be extracted; videos, music and PDFs open in the browser.

### Made for the PS5

- **DualSense all the way**: a cursor with a magnet for links, the D-pad jumping between links and buttons, radial menus on L2 and R2 with a click in the adaptive triggers, haptics, the light bar in the theme's colour.
- **The PS5's own keyboard** for text (firmware 13.60), with its suggestions and layout; passwords, and other firmwares, get the browser's own keyboard, with the PS5 keyboard's shortcuts and accents on a long press.
- **16 themes**, six of them seasonal and switched by the calendar.
- **Open beside the game**: sends the page you are on to the PS5's own side card, next to your game.
- **Your phone as keyboard and remote**: pair it with a code or a QR code, type on it, send links and files to the console, and see the console's downloads. Your game's notes and kept pages can be written on it too.

### For players and homebrew

- **"Your game" on the start page**: the game open or played last, with its cover, its background and your trophies of it, and one press to its guide, videos, wiki, maps, news, who is live with it, X and Reddit. The browser only reads what the PS5 keeps.
- **A page for each game**: its trophies one by one (the ones still to win first, each with a search for how to win it), how far you are, **Play** (the browser closes and the game starts by itself; it needs the ELF loader on port 9021), your **notes** for the game, **your captures** of it, and the **pages you kept with it** (on a guide or a map, menu → "Keep with the game").
- **Your games**: every game of the console as a grid of covers, by recent, by name or by trophies (the closest to the platinum first), PS5 or PS4 only, with how far you are on each.
- **Homebrew**: the scene's tools (etaHEN, websrv, elfldr, ftpsrv, shsrv, klogsrv, the payload SDK) with their newest release read from GitHub, what is new since you last looked, and which of them run on the console now.
- **Cheats and patches for etaHEN**: the cheat files of github.com/etaHEN/PS5_Cheats and the patches (60 FPS and the like) of github.com/illusionyy/PS-Game-Patch for that very game, saved where etaHEN reads them, with a switch for each patch. Nothing is downloaded until you ask. The browser only puts the files in place: cheats are turned on in game from etaHEN's own cheats menu, and patches need etaHEN's patches plugin (`itemzflow_xml_patches`). etaHEN does not publish that plugin ready-made (it is built from [etaHEN-Plugins](https://github.com/etaHEN/etaHEN-Plugins/tree/main/Plugin_samples/itemzflow_xml_patches)); when it is not on the console, the panel says so and shows the steps to get it working. The switches are kept either way.
- **Captures to Discord**: a screenshot or clip of a game, from its page, to a channel of yours through its webhook, with the game's name. No sign-in; the webhook stays on the console.
- **The console's health**: temperatures, power use, clock, the fan's target, uptime, disks, and a network test. Only read: nothing is changed.
- **The controller as a keyboard, per site**: arrows or W A S D, space, Enter and more, for web games that want a keyboard; the touch pad hands the controller to the page.
- **Run a payload you downloaded**: a finished `.elf` in Downloads can be sent to the console's ELF loader (port 9021), after you confirm.

---

## Technical specifications

| | |
| --- | --- |
| **Type** | Native PS5 title, game category (`applicationCategoryType` 0), title ID `PPSA97024` |
| **Engine** | WebKit 627.1.7 (trunk of November 2026), ported to the PS5: WebCore, JavaScriptCore with its JIT tiers, WebAssembly |
| **Processes** | The interface and the network process run as payloads; the pages' WebProcess runs inside the title, with its memory and its GPU |
| **Drawing** | Pages composited on the console's GPU through Mesa (zink over RADV); 1080p interface |
| **Video** | H.264 and VP9 decoded by the PS5's video decoder, up to 3840×2160; sound through the console's audio out |
| **Network** | curl with HTTP/2; certificates **always verified** against Mozilla's CA list, which the installer brings |
| **Memory** | 1 GiB of flexible memory for the pages (the most the system grants a title) |
| **Languages** | English, Português, Español, Français, Deutsch, Italiano, Nederlands |
| **Install size** | `BrowserPS5.elf` 111 MiB; about 450 MB on the console |
| **Tested on** | PS5 firmware 13.60 with etaHEN and ShadowMountPlus |

---

## Requirements

- A **jailbroken PS5** with an **ELF loader on port 9021** (etaHEN starts one). It must be running **every time the browser is opened**, not only to install it: the browser is two programs, the title you open and its interface, and at every start the title hands the interface (`browser.elf`) to the loader on `127.0.0.1:9021`. With no loader answering, the title shows its loading screen and closes. It was tested with the loader etaHEN starts.
- **ShadowMountPlus** running: it puts titles installed in `/data/homebrew` on the home screen. Without it the browser's files are installed but no tile appears.
- An **internet connection**. A network cable is worth it for large downloads.
- To send the ELF from a PC: any payload sender, or the `send.ps1` (Windows PowerShell) that comes in the zip.

---

## Installation

1. **Unzip** `BrowserPS5-<version>.zip`.
2. **Check the files** (optional, recommended). In PowerShell, in the unzipped folder:

   ```powershell
   Get-FileHash .\BrowserPS5.elf -Algorithm SHA256
   ```

   The result must match the line for `BrowserPS5.elf` in `SHA256SUMS.txt`.
3. **Load your jailbreak** on the PS5 and make sure the ELF loader (port 9021) and ShadowMountPlus are running.
4. **Run `BrowserPS5.elf` once**, the way you run any other payload:

   - **Payload Manager** (or another manager on the console): add `BrowserPS5.elf` to it and run it. It is an installer: run it once, and **do not put it in a list that loads at every start**, or the browser is installed again each time the console starts.
   - **From a PC**: send it to port 9021 with the payload sender you already use (some setups only let the console itself use the loader: use the manager on the console then). On Windows, the included `send.ps1` does it:

   ```powershell
   .\send.ps1 -Ps5 <your PS5's IP> -Elf .\BrowserPS5.elf
   ```

   The PS5's IP is in *Settings → Network → Connection Status → View Connection Status*. If PowerShell refuses to run the script: `powershell -ExecutionPolicy Bypass -File .\send.ps1 -Ps5 <IP> -Elf .\BrowserPS5.elf`.
5. **Wait for the two notifications** on the PS5: *"installing, about half a minute"* and then *"is installed"*. Do not open the browser in between.
6. **Open BrowserPS5 RN** from **Games** on the home screen. ShadowMountPlus adds it about 15 seconds after the install. The icon and background may only show after the PS5 restarts.

The installer writes about 450 MB and takes half a minute. It writes only inside `/data/homebrew` and `/user/homebrew`, closes no app, and changes no system setting.

---

## Controls

| Control | What it does |
| --- | --- |
| **Left stick** | Move the cursor |
| **Right stick** | Scroll the page |
| **✕** | Click · hold on a link or picture for its menu |
| **D-pad** | Jump to the next link or button (or scroll, or send arrow keys: *Settings → Controller*) |
| **○** | Back · close a menu |
| **L1 / R1** | Back / forward |
| **△** | Type an address |
| **□** | Enter |
| **L2 / R2** | Radial menus: *Go to* and *Page* |
| **Touch pad, a finger** | Move the cursor, as on a laptop: slowly for small steps, fast to go far · **tap** to click |
| **Touch pad, two fingers** | Scroll the page with them; it glides on a little after they leave, and a finger on the pad stops it |
| **Touch pad, pressed** | Tabs · on a site with games, hands the controller to the game |
| **Options** | Menu: favorites, history, find, reading mode, translate, private tab, zoom, full screen, settings, quit |

Every one of these is listed in *Settings → Controller*, with the cursor's speed, haptics, sounds and the light bar.

**In a game played with keys** (*Menu → Controller as keyboard*, kept for each site): the left stick and the D-pad are the direction keys, the right stick is the mouse, R2 the mouse's button and R3 Enter; ✕ is space, □ △ ○ keys of their own, L1 Q, R1 E, L2 Shift and Options Esc.

**On YouTube's page for televisions**, ○ is the page's own "back": from a film to the screen it was opened from, and from the page's first screen out to the browser.

---

## Downloads

- Files go to `/data/downloads`, videos, music and pictures to `/data/media`, and `.pkg` files to `/data/pkg`. Each folder can be changed in *Settings → Downloads*, and the browser can ask where to save every file, like Windows. **The browser installs no package**: a `.pkg` is only saved.
- *Fast downloads* (on by default) brings large files over several connections. If the server does not allow it, or it is not faster, the browser goes back to one connection by itself.
- While a download waits for the network it says so in the list, and goes on by itself when the network returns.
- **Pause**, **Continue**, **Try again** and **Delete** are on each line of *Settings → Downloads*. Delete only removes files inside the download folders.
- An unfinished download is kept for 14 days. *Remove from the list* deletes what came of it.

---

## Updating and uninstalling

**Updating**: from 1.0.0-rc14 on the browser updates itself. A few seconds after the first page of a session it asks GitHub for this project's list of releases (every 6 hours at most). When there is a newer one, its installer is downloaded in the background while you browse and checked against the size and the SHA-256 the release gives. The next time you open the browser it says so, closes, installs (about half a minute) and opens again. Your favorites, history, settings, logins and unfinished downloads stay. If an update does not install, the browser tries once more at a later start and then leaves that release alone: install it by hand.

To turn this off, make an empty file named `no-updates` in `/user/homebrew/browserps5-rn/data` (by FTP). 1.0.0-rc13 and earlier do not update themselves.

**By hand**: send the newer `BrowserPS5.elf` the same way as the first time, **with the browser closed**.

If the browser is open, the installer writes nothing and says so in a notification: close the browser (Options → Close application on its tile) and send it again.

**Uninstalling**: send `BrowserPS5-uninstall.elf` the same way:

```powershell
.\send.ps1 -Ps5 <your PS5's IP> -Elf .\BrowserPS5-uninstall.elf
```

It asks ShadowMountPlus to take the tile off the home screen and deletes what the installer wrote. **Your favorites, history, settings and logins stay**, for the next install. `BrowserPS5-uninstall-all.elf` removes them too. The files you downloaded are never touched. If the tile is still on the home screen afterwards, delete it there (Options → Delete).

---

## Files on the console

| Path | What is there |
| --- | --- |
| `/data/homebrew/PPSA97024/` | The title: `eboot.bin`, the browser itself (`browser.elf`), fonts, `sce_sys/` (icon, backgrounds, `param.json`), and its own logs |
| `/user/homebrew/lib/libWebKitFull.so.SOVERSION` | The engine's library |
| `/user/homebrew/browserps5-rn/full/`, `pdfjs/`, `adblock/` | The engine's processes, the PDF reader and the ad blocker's lists |
| `/user/homebrew/browserps5-rn/update/` | The browser's own updates: a newer release's installer while it waits, and what happened to the last one (`update-log.txt`) |
| `/user/homebrew/browserps5-rn/data/` | **Yours**: favorites, history, settings, saved logins, cookies and site data, extensions, unfinished downloads' maps |
| `/user/homebrew/lib/libEGL.so.1`, `libvulkan.so.1` | Mesa, shared with other homebrew: written only if the console has none |
| `/user/homebrew/etc/`, `/user/homebrew/share/fonts/` | Certificates (Mozilla's CA list) and fonts, shared the same way |
| `/data/downloads`, `/data/media`, `/data/pkg` | What you download (the folders can be changed) |

---

## Privacy and security

- Everything you do stays **on your console**. BrowserPS5 RN has no telemetry, no account and no server of its own; it talks to the sites you open and to the search engine you chose, and, to update itself, it asks GitHub for this project's list of releases (see *Updating and uninstalling*; it can be turned off).
- HTTPS certificates are **always verified**. There is no "accept any certificate" mode.
- **Pages cannot reach the console's own services.** A jailbroken PS5 listens on its local network (FTP, the ELF loader, other tools) with no password; a web page that tried to connect to them is refused. Only an address you type yourself may open them.
- Private tabs keep their cookies and site data in memory only, and leave no history, no saved session and no saved login.
- Saved logins are kept on the console, in the browser's data folder. Anyone with access to the console's files (FTP on a jailbroken console) can read that folder: treat the console as you would an unlocked PC.
- An unfinished download keeps its address in the browser's data folder until it finishes or is removed from the list.
- The browser changes no PS5 system setting, and installs nothing but its own updates.

---

## What it does not do

- **No DRM video**: Netflix, Disney+, Prime Video, Spotify's web player and the like do not play.
- **No calls, camera or microphone** (no WebRTC), and no text-to-speech.
- Up to **6 tabs**, sharing 1 GiB: a very heavy page may be loaded again when you come back to it.
- *Open beside the game* shows the page in the **PS5's own side card**, a much smaller browser than this one: simple pages only.
- A download can only go on from where it stopped if the server gives parts of a file, and while its link is still valid.
- Tested on firmware **13.60** only. A user reports it running on 11.20, another that it does not start on 12.20: reports from other firmwares are welcome (*Reporting a problem*, below).

---

## Troubleshooting

| What you see | What to do |
| --- | --- |
| No BrowserPS5 RN tile after installing | ShadowMountPlus is not running, or has not scanned yet: start it and wait about 15 s |
| The tile has no icon or background | Restart the PS5 once |
| The installer's notifications never come | Make sure the ELF loader listens on port 9021 (load your jailbreak again) and that the IP is the PS5's |
| The loading screen shows and the browser closes, or *"Game or App Error"* comes right after starting it | Make sure the ELF loader answers on port 9021 (load etaHEN or your loader again) and open the browser again. If it still closes, please report it with the report of `BrowserPS5-check.elf` or `browser-title.txt` (below), your console and how you load your payloads |
| *"is open: nothing was installed"* | Close BrowserPS5 RN (Options → Close application) and send the installer again |
| No page loads, not even the start page (1.0.0-rc13 and earlier) | Restart the console, and install this release: it no longer happens |
| A site says the browser is not supported | Try *Menu → Desktop version*; some sites only accept the browsers they know by name |
| A video service will not play | It needs DRM, which BrowserPS5 RN does not have |
| A download stays at *waiting for the network* | The console lost its connection: it goes on by itself when the network is back |
| Downloads are slow | Use a network cable: over Wi-Fi the console takes about 27 MB/s at most |
| The cursor moves or clicks when a finger brushes the touch pad | Put an empty file named `no-touch-cursor` in `/data/homebrew/PPSA97024/` (by FTP): the pad is then only its press |

The browser's own logs are in `/data/homebrew/PPSA97024/` (`browser-run.txt`, `browser-title.txt`, `web-stderr.txt`). They are kept for the last four sessions and help when reporting a problem.

**Reporting a problem**: send `BrowserPS5-check.elf` the same way as the installer. It only reads: the firmware, what is installed, whether the ELF loader answers, and how the last start went. The report comes out in your payload sender's output and is saved as `/data/homebrew/PPSA97024/BrowserPS5-check.txt`; pages' addresses are left out of it. Paste it into the issue.

---

## Building from source

Everything is built on Linux (or WSL) with the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk):

| Step | Script |
| --- | --- |
| The engine (WebKit with this project's patches) | `poc/webkit/build-webkit.sh`, `poc/browser-rn/engine-rebuild-rn.sh full` |
| The title (the pages' process, video, input) | `poc/browser-rn/title-full-rn.sh` |
| The browser (interface, downloads, settings) | `poc/browser-rn/menus-full-rn.sh` |
| The release (this zip) | `poc/release-rn/build-release-rn.sh` |

The patches to WebKit are in `poc/webkit/`, each with the reason it exists.

The title also needs [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) built beside it (`~/browserps5-build/ext/PS5_Vulkan`): its RADV for the PS5, its GPU broker and its tools for making the PS5 executable.

---

## Licences

BrowserPS5 RN is free software under the **GNU General Public License, version 3 or later** (`LICENSE`). It comes with **no warranty**.

It is built on WebKit and on many other open projects; they keep their own licences, listed in `THIRD-PARTY-NOTICES.md`.

Pages reach the PS5's GPU through **RADV on the PS5**, the work of Mihawk ([PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)) on the foundation of BlackBearReloaded's [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate). The engine's locks, and the idea of keeping pages away from the console's own services, come from rikleigh's [Parallax Browser](https://github.com/rikleigh/parallax-browser). Thank you.

PlayStation, PS5 and DualSense are trademarks of Sony Interactive Entertainment. WebKit is a trademark of Apple Inc. YouTube, WhatsApp and Telegram are trademarks of their owners. BrowserPS5 RN is independent of all of them.
