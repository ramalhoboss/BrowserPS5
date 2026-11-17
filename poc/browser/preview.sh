#!/usr/bin/env bash
#   Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
#
# The browser's own pages on a PC, as the TV shows them (1920x1080; docs/07):
# the bar (chrome.html) at the top, the start page or the settings under it,
# and the menu, the dialog, the keyboard or a radial menu over them as main.cpp places
# them, with sample data (favorites, history, an extension, downloads). The
# pages are the built ones (make: theme.css and icons.svg put in). Writes a
# folder (default ./preview) to serve over HTTP, the panels being frames of
# the same origin:
#
#   bash preview.sh [FOLDER]
#   python3 -m http.server 8765 --directory FOLDER
#   http://127.0.0.1:8765/?page=start|settings&panel=menu|dialog|folder|keys|radial-go|radial-page|tabs
#                         &section=aspeto&theme=rubi&pick=3&media=1&lang=en
#
# The app's commands (browserps5:...) go nowhere; the screen's log lists them.

set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
out=${1:-$here/preview}
mkdir -p "$out"

for page in chrome keyboard menu dialog radial tabs start settings phone; do
    awk -v dir="$here" -f "$here/include.awk" "$here/$page.html" > "$out/$page.html"
done

# The data main.cpp writes into its pages, as JSON in a JavaScript string.
now=$(date +%s)
settings_json='{"settings": {"theme": "meia-noite", "startup": "home", "home_kind": "start", "home_url": "", "search": "duckduckgo", "zoom": 100, "favbar": 1, "javascript": 1, "images": 1, "popups": 0, "autoplay": 1, "desktop": 1, "third_party_cookies": 1, "adblock": 1, "dark_sites": 0, "fullscreen": 1, "dpad": "elements", "cursor_speed": 100, "scroll_speed": 100, "magnet": 1, "sounds": 1, "welcomed": 1, "phone": 1, "ask_downloads": 1, "logins": 1, "torbox_magnets": 0, "disk_cache": 1, "web_in_title": 1, "downloads_dir": "/data/downloads", "pkg_dir": "/data/pkg", "media_dir": "/data/media", "last_url": "", "haptics": 1, "triggers": 1, "lightbar": 1, "ad_allow": "rtp.pt sapo.pt"}, "season": "outono", "history": [['$now', "https://www.rtp.pt/noticias/", "RTP Notícias"], ['$((now - 600))', "https://www.youtube.com/", "YouTube"], ['$((now - 86400))', "https://pt.wikipedia.org/wiki/PlayStation_5", "PlayStation 5 – Wikipédia"]], "favorites": [{"url": "https://www.youtube.com/", "title": "YouTube"}, {"url": "https://www.rtp.pt/", "title": "RTP"}, {"url": "https://pt.wikipedia.org/", "title": "Wikipédia"}], "extensions": [{"id": 1, "name": "Modo leitura", "version": "1.2", "description": "Tira o que distrai dos artigos", "matches": ["https://*/*"], "on": true}], "pending": null, "about": {"version": "0.7 (pré-visualização)", "engine": "WebKit, porta PlayStation"}, "section": "geral", "downloads": [{"id": 1, "name": "homebrew.pkg", "path": "", "received": 734003200, "total": 1288490188, "state": "running", "speed": 3565158, "left": 156, "stalled": false, "retry": false}, {"id": 2, "name": "trailer.mp4", "path": "/data/media/trailer.mp4", "received": 52428800, "total": 52428800, "state": "done", "kind": "media", "unzip": "none", "unzip_percent": -1, "unzip_dir": "", "speed": 0, "left": -1, "stalled": false, "retry": false}, {"id": 4, "name": "fotos-ferias.zip", "path": "/data/downloads/fotos-ferias.zip", "received": 84934656, "total": 84934656, "state": "done", "kind": "file", "unzip": "done", "unzip_percent": -1, "unzip_dir": "/data/downloads/fotos-ferias", "speed": 0, "left": -1, "stalled": false, "retry": false}, {"id": 5, "name": "mapas.zip", "path": "/data/downloads/mapas.zip", "received": 9437184, "total": 9437184, "state": "done", "kind": "file", "unzip": "none", "unzip_percent": -1, "unzip_dir": "", "speed": 0, "left": -1, "stalled": false, "retry": false}, {"id": 3, "name": "ubuntu-24.04-desktop-amd64.iso", "path": "", "received": 0, "total": 0, "state": "failed", "speed": 0, "left": -1, "stalled": false, "retry": true}], "logins": [{"origin": "https://github.com", "user": "ana.silva", "time": 1793900000, "never": false}, {"origin": "http://router.local", "user": "", "time": 1793800000, "never": true}], "torrents": true, "ps5settings": true, "web_title": true, "unzip": true, "torbox": {"key": true, "hint": "3456"}}'
start_json='{"favorites": [{"url": "https://www.youtube.com/", "title": "YouTube"}, {"url": "https://www.rtp.pt/", "title": "RTP"}, {"url": "https://pt.wikipedia.org/", "title": "Wikipédia"}, {"url": "https://www.publico.pt/", "title": "Público"}, {"url": "https://www.twitch.tv/", "title": "Twitch"}], "recent": [{"url": "https://www.rtp.pt/noticias/", "title": "RTP Notícias"}, {"url": "https://github.com/", "title": "GitHub"}], "theme": "meia-noite", "engine": "DuckDuckGo", "welcomed": 1, "restore": 3}'
quote() {  # JSON into the page's "..." (as main.cpp's script_string)
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}
put() {  # the mark in file $1 replaced by $3, as text (no gsub: its \ and &)
    DATA="$3" awk -v mark="$2" '{ i = index($0, mark); if (i) $0 = substr($0, 1, i - 1) ENVIRON["DATA"] substr($0, i + length(mark)); print }' "$1" > "$1.tmp" && mv "$1.tmp" "$1"
}
sq=$(quote "$settings_json")
st=$(quote "$start_json")
put "$out/settings.html" "@SETTINGS@" "$sq"
cp "$out/start.html" "$out/welcome.html"  # the first start (?page=welcome)
put "$out/start.html" "@START@" "$st"
put "$out/welcome.html" "@START@" "$(quote "${start_json:0:${#start_json}-1}, \"welcomed\": 0}")"

cat > "$out/index.html" <<'EOF'
<!doctype html>
<meta charset="utf-8">
<title>BrowserPS5: pré-visualização</title>
<style>
  html, body { margin: 0; background: #000; }
  #screen { position: relative; width: 1920px; height: 1080px; overflow: hidden; background: #000; }
  iframe { position: absolute; border: 0; display: block; }
  #dim { position: absolute; inset: 0; background: rgba(0,0,0,.5); display: none; }
  #radial { border-radius: 50%; opacity: .93; box-shadow: 0 16px 48px rgba(0,0,0,.55); }
  .panel { border-radius: 26px; box-shadow: 0 18px 60px rgba(0,0,0,.55); }
  #log { font: 14px monospace; color: #9ab; padding: 8px; white-space: pre; }
</style>
<div id="screen">
  <iframe id="page" style="left:0;top:152px;width:1920px;height:928px"></iframe>
  <iframe id="bar" src="chrome.html" style="left:0;top:0;width:1920px;height:152px"></iframe>
  <div id="dim"></div>
  <iframe id="menu" class="panel" src="menu.html" style="left:1342px;top:92px;width:560px;height:948px;display:none"></iframe>
  <iframe id="dialog" class="panel" src="dialog.html" style="left:340px;top:150px;width:1240px;height:720px;display:none"></iframe>
  <iframe id="keys" src="keyboard.html" style="left:0;top:640px;width:1920px;height:440px;display:none"></iframe>
  <iframe id="radial" src="radial.html" style="left:600px;top:180px;width:720px;height:720px;display:none"></iframe>
  <iframe id="switcher" src="tabs.html" style="left:0;top:0;width:1920px;height:1080px;display:none"></iframe>
</div>
<div id="log"></div>
<script>
const q = new URLSearchParams(location.search);
const theme = q.get('theme') || 'meia-noite';
const lang = q.get('lang') || 'pt';  // lang.js (docs/13-idioma.md): &lang=en
const panel = q.get('panel') || '';
const $ = (id) => document.getElementById(id);
const log = (s) => { $('log').textContent += s + '\n'; };
const page = q.get('page') || 'start';
$('page').src = page + '.html' + (q.get('section') ? '#' + q.get('section') : '');
const places = [
  {label: 'Transferências', path: '/data/downloads', icon: 'arrow-download', free: 412e9},
  {label: 'Pacotes (PKG)', path: '/data/pkg', icon: 'box', free: 412e9},
  {label: 'Multimédia', path: '/data/media', icon: 'video-clip', free: 412e9},
  {label: 'Armazenamento interno', path: '/data', icon: 'storage', free: 412e9},
  {label: 'USB 1', path: '/mnt/usb0', icon: 'usb-stick', free: 61e9},
];
function hook(frame, ready) {
  frame.addEventListener('load', () => {
    const w = frame.contentWindow;
    // What the page sends the app.
    w.document.addEventListener('click', (e) => {
      const a = e.target.closest && e.target.closest('a[href^="browserps5:"]');
      if (a) { e.preventDefault(); log(frame.id + ': ' + decodeURIComponent(a.href)); }
    }, true);
    if (w.setLang) w.setLang(lang);
    try { ready(w); } catch (e) { log(frame.id + ': ' + e); }
  });
}
hook($('bar'), (w) => {
  w.setTheme(theme);
  w.setFavorites(JSON.stringify([{url: 'https://www.youtube.com/', title: 'YouTube'},
    {url: 'https://www.rtp.pt/', title: 'RTP'}, {url: 'https://pt.wikipedia.org/', title: 'Wikipédia'}]));
  const media = q.get('media') === '1';  // a video opened by itself: the bar's Guardar
  w.update({url: media ? 'https://media.example.org/trailers/trailer-4k.mp4'
                       : page === 'start' ? 'file:///user/homebrew/browserps5/data/start.html?1'
                                          : 'file:///user/homebrew/browserps5/data/settings.html?2#geral',
            title: media ? 'trailer-4k.mp4' : page === 'start' ? 'Início' : 'Definições', canGoBack: true,
            canGoForward: false, loading: false, progress: 1, zoom: media ? 125 : 100, media: media});
  w.setDownloads({count: 2, active: 1, percent: 57});
  w.setMenuOpen(panel === 'menu');
});
hook($('page'), (w) => {
  if (w.document.documentElement) w.document.documentElement.dataset.theme = theme;
  if (w.setPhone && q.get('section') === 'telemovel')
    w.setPhone({on: 1, url: 'http://192.168.1.51:8090/', pin: '482915', seconds: 287, count: 1});
  if (w.setSystem && q.get('section') === 'sistema')
    w.setSystem({firmware: '13.60', ip: '192.168.1.51', up: 20345, cpu: 61, soc: 58,
                 mem_total: 17179869184, mem_free: 5368709120,
                 disks: [{label: 'Armazenamento interno', path: '/data', icon: 'storage',
                          total: 848256040960, free: 312475648000},
                         {label: 'USB 1', path: '/mnt/usb0', icon: 'usb-stick',
                          total: 61530439680, free: 4294967296}],
                 browser: {version: '0.7 (Nov  5 2026)', up: 1834, tabs: 3, ads: 412, downloads: 1}});
});
hook($('menu'), (w) => {
  w.show({theme, zoom: 100, desktop: true, dark_sites: false, adblock: true, favorite: true,
          canFavorite: true});
});
hook($('dialog'), (w) => {
  w.setTheme(theme);
  if (panel === 'folder')
    w.dialog.folder({key: 'pkg_dir', title: 'Pasta dos pacotes (PKG)', dir: '/data/pkg', places,
                     note: 'Os ficheiros .pkg transferidos ficam aqui.'});
  else
    w.dialog.save({id: 1, name: 'homebrew.pkg', size: 1288490188, host: 'github.com', kind: 'pkg',
                   dir: '/data/pkg', places, theme});
  w.dialog.folders(panel === 'folder' ? '/data/pkg' : '/data/pkg', ['jogos', 'homebrew', 'temas']);
  w.dialog.progress(734003200, 1288490188);
});
hook($('keys'), (w) => {
  if (w.setTheme) w.setTheme(theme);
  w.kb.open({kind: 'bar', enter: 'go', text: ''});
});
hook($('radial'), (w) => {
  w.setTheme(theme);
  w.show({ring: panel === 'radial-page' ? 'page' : 'go', theme, zoom: 110, adblock: true,
          dark_sites: false, fullscreen: false, canFavorite: true, canBack: true,
          canForward: false, canAds: true});
  if (q.get('pick') !== null) w.pick(+q.get('pick'));
});
hook($('switcher'), (w) => {
  w.show({theme, current: 0, sel: +(q.get('pick') || 0), max: 6, tabs: [
    {title: 'Início', host: 'BrowserPS5', picture: 'tab-1.png'},
    {title: 'Definições', host: 'BrowserPS5', picture: 'tab-2.png'},
    {title: 'RTP Notícias', host: 'www.rtp.pt'},
  ]});
});
if (panel === 'menu') $('menu').style.display = 'block';
if (panel === 'dialog' || panel === 'folder') { $('dialog').style.display = 'block'; $('dim').style.display = 'block'; }
if (panel === 'keys') $('keys').style.display = 'block';
if (panel === 'tabs') $('switcher').style.display = 'block';
if (panel.startsWith('radial-')) { $('radial').style.display = 'block'; $('dim').style.display = 'block'; $('dim').style.background = 'rgba(0,0,0,.47)'; }
</script>
EOF
echo "$out"

# The phone's page (phone.html) as a paired phone sees it: phone-paired.html,
# with fetch() answering as the console would (?pair=1 for the PIN screen).
awk '/^<script>$/ && !done { print "<script>"; print "if (!/pair=1/.test(location.search)) { try { localStorage.setItem(\x27bps5-token\x27, \x270123456789abcdef0123456789abcdef\x27); } catch (e) {} } else { try { localStorage.removeItem(\x27bps5-token\x27); } catch (e) {} }"; print "const FS = path => { const p = new URLSearchParams(path.split(\x27?\x27)[1] || \x27\x27).get(\x27path\x27); return !p ? {places: [{kind: \x27downloads\x27, path: \x27/data/downloads\x27}, {kind: \x27media\x27, path: \x27/data/media\x27}, {kind: \x27usb\x27, path: \x27/mnt/usb0\x27}]} : p == \x27/data/downloads\x27 ? {entries: [{name: \x27Fotos\x27, dir: true, size: 0, time: 1762380000}, {name: \x27Transferências\x27, dir: true, size: 0, time: 1762380000}, {name: \x27fotos.zip\x27, dir: false, size: 9437184, time: 1762390000}, {name: \x27notas.txt\x27, dir: false, size: 2048, time: 1762390000}, {name: \x27trailer.mp4\x27, dir: false, size: 52428800, time: 1762410000}, {name: \x27nome�.bin\x27, dir: false, size: 10, time: 0, bad: true}], more: 0} : {entries: [], more: 0}; };"; print "window.fetch = async (path) => new Response(JSON.stringify(path.startsWith(\x27/api/state\x27) ? {title: \x27RTP Notícias\x27, url: \x27https://www.rtp.pt/noticias/\x27, theme: new URLSearchParams(location.search).get(\x27theme\x27) || \x27meia-noite\x27} : path.startsWith(\x27/api/downloads\x27) ? {downloads: [{id: 1, name: \x27homebrew.pkg\x27, state: \x27running\x27, received: 734003200, total: 1288490188, speed: 3565158, left: 156, kind: \x27pkg\x27, unzip: \x27none\x27}, {id: 2, name: \x27trailer.mp4\x27, state: \x27done\x27, received: 52428800, total: 52428800, kind: \x27media\x27, unzip: \x27none\x27}, {id: 3, name: \x27fotos.zip\x27, state: \x27done\x27, received: 9437184, total: 9437184, kind: \x27file\x27, unzip: \x27running\x27, unzip_percent: 42}, {id: 4, name: \x27ubuntu.iso\x27, state: \x27failed\x27, kind: \x27file\x27, unzip: \x27none\x27, retry: true}]} : path.startsWith(\x27/api/files\x27) ? FS(path) : path.startsWith(\x27/api/ticket\x27) ? {ticket: \x270123456789abcdef0123456789abcdef\x27} : {}), {status: 200});"; print "</script>"; done = 1 } { print }' "$out/phone.html" > "$out/phone-paired.html"
# Both at a phone's width, side by side (phones.html): headless Chrome will
# not make a window narrower than 500 px.
cat > "$out/phones.html" <<'PHONES'
<!doctype html><meta charset="utf-8"><title>Telemóvel</title>
<style>body { margin: 0; background: #2a2d36; display: flex; gap: 40px; padding: 40px; align-items: flex-start; }
iframe { border: 0; border-radius: 34px; box-shadow: 0 20px 60px rgba(0,0,0,.5); background: #000; }</style>
<iframe src="phone-paired.html?pair=1" width="390" height="844"></iframe>
<iframe src="phone-paired.html" width="390" height="1480"></iframe>
PHONES
