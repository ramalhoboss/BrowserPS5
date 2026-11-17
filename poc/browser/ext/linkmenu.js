// Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
//
// BrowserPS5 RN's link menu (2026-11-11; main.cpp, extensions_apply()), in
// each page's main frame from its start: what a right click is on a desktop
// and a long press on a phone. Cross held still on a link or a picture for
// two thirds of a second opens a small menu there: the link in a new tab or
// in a private one, the picture by itself or kept as a file. The release
// that follows is not a click on the link. A press outside closes it, and
// so do a scroll and circle (the app is told with browserps5:overlay?1 from
// a frame of its own, as ext/fullscreen.js tells it, and calls
// __bps5overlayClose()).
//
// The tabs are asked for as tabs_js asks (browserps5:newtab?<address>, right
// after a press: the menu's own); the private one with browserps5:newprivate.
// A picture is kept with a link to it with "download", which the app's
// downloads take.
function (lang) {
  'use strict';
  if (window.__bps5link) return;
  window.__bps5link = true;
  var HOLD = 650, MOVE = 10;
  var en = lang !== 'pt';
  // 2026-11-14: BrowserPS5 RN's other languages (the app gives the language's code: lang_code()).
  var more = {
    es: {tab: 'Abrir en una pestaña nueva', priv: 'Abrir en una pestaña privada', here: 'Abrir aquí', picture: 'Abrir la imagen', keep: 'Guardar la imagen', close: 'Cerrar'},
    fr: {tab: 'Ouvrir dans un nouvel onglet', priv: 'Ouvrir dans un onglet privé', here: 'Ouvrir ici', picture: 'Ouvrir l\'image', keep: 'Enregistrer l\'image', close: 'Fermer'},
    de: {tab: 'In neuem Tab öffnen', priv: 'In privatem Tab öffnen', here: 'Hier öffnen', picture: 'Bild öffnen', keep: 'Bild speichern', close: 'Schließen'},
    it: {tab: 'Apri in una nuova scheda', priv: 'Apri in una scheda privata', here: 'Apri qui', picture: 'Apri l\'immagine', keep: 'Salva l\'immagine', close: 'Chiudi'},
    nl: {tab: 'Openen in een nieuw tabblad', priv: 'Openen in een privétabblad', here: 'Hier openen', picture: 'De afbeelding openen', keep: 'De afbeelding opslaan', close: 'Sluiten'}
  };
  var words = more[lang] || (en
    ? {tab: 'Open in a new tab', priv: 'Open in a private tab', here: 'Open here', picture: 'Open the picture', keep: 'Save the picture', close: 'Close'}
    : {tab: 'Abrir num separador novo', priv: 'Abrir num separador privado', here: 'Abrir aqui', picture: 'Abrir a imagem', keep: 'Guardar a imagem', close: 'Fechar'});
  var timer = 0, at = null, host = null, held = false, swallow = false;

  function ask(command) {  // the app, from a frame of its own: the page never navigates
    try {
      var f = document.createElement('iframe');
      f.style.cssText = 'display: none';
      f.src = 'browserps5:' + command;
      (document.body || document.documentElement).appendChild(f);
      setTimeout(function () { f.remove(); }, 1000);
    } catch (e) {}
  }

  function web(url) {
    return /^https?:/i.test(url || '') ? url : '';
  }

  // What is under the press: a link, a picture, or both (a picture in a link).
  function find(e) {
    var el = e.target;
    if (!el || !el.closest) return null;
    if (el.closest('input, textarea, select, [contenteditable=""], [contenteditable=true]')) return null;
    var a = el.closest('a[href]');
    var img = el.tagName === 'IMG' ? el : null;
    var href = a ? web(a.href) : '';
    var src = img ? web(img.currentSrc || img.src) : '';
    if (!href && !src) return null;
    var text = a ? (a.textContent || a.title || '').replace(/\s+/g, ' ').trim() : (img.alt || '');
    return {href: href, src: src, text: text};
  }

  function close() {
    if (!host) return;
    host.remove();
    host = null;
    ask('overlay?0');
  }
  window.__bps5overlayClose = function () {  // circle, from the app
    if (host) { close(); return true; }
    // the reading mode, when it is the one open (ext/reader.js)
    return window.__bps5readerClose ? window.__bps5readerClose() : false;
  };

  function open(what, x, y) {
    close();
    host = document.createElement('div');
    host.id = 'bps5-linkmenu';
    host.style.cssText = 'all: initial; position: fixed; left: 0; top: 0; width: 0; height: 0; z-index: 2147483647;';
    var root = host.attachShadow({mode: 'open'});
    var style = document.createElement('style');
    style.textContent =
      ':host { all: initial; }' +
      '#m { position: fixed; min-width: 360px; max-width: 560px; padding: 10px; border-radius: 18px; box-sizing: border-box;' +
      ' background: #16203a; color: #fff; font: 500 21px/1.25 system-ui, "SST", sans-serif;' +
      ' box-shadow: 0 18px 50px rgba(0, 0, 0, .6), inset 0 0 0 1px rgba(255, 255, 255, .16); }' +
      '#t { padding: 6px 14px 10px; font-size: 16px; color: rgba(255, 255, 255, .62); overflow: hidden; white-space: nowrap; text-overflow: ellipsis; }' +
      'button { all: unset; display: block; width: 100%; box-sizing: border-box; padding: 13px 16px; border-radius: 12px; cursor: pointer; }' +
      'button:hover, button:focus { background: #fff; color: #0a1124; }' +
      'button.q { color: rgba(255, 255, 255, .62); } button.q:hover { color: #0a1124; }';
    var m = document.createElement('div');
    m.id = 'm';
    var title = document.createElement('div');
    title.id = 't';
    title.textContent = (what.text ? what.text.slice(0, 60) + ' · ' : '') + (what.href || what.src).replace(/^https?:\/\/(www\.)?/, '').slice(0, 80);
    m.appendChild(title);
    function item(text, act, cls) {
      var b = document.createElement('button');
      b.textContent = text;
      if (cls) b.className = cls;
      b.addEventListener('click', function (e) {
        e.preventDefault();
        e.stopPropagation();
        close();
        if (act) act();
      });
      m.appendChild(b);
    }
    if (what.href) {
      item(words.tab, function () { ask('newtab?' + encodeURIComponent(what.href)); });
      item(words.priv, function () { ask('newprivate?' + encodeURIComponent(what.href)); });
      item(words.here, function () { location.href = what.href; });
    }
    if (what.src) {
      item(words.picture, function () { ask('newtab?' + encodeURIComponent(what.src)); });
      item(words.keep, function () {
        var a = document.createElement('a');
        a.href = what.src;
        a.download = '';
        (document.body || document.documentElement).appendChild(a);
        a.click();
        a.remove();
      });
    }
    item(words.close, null, 'q');
    root.append(style, m);
    document.documentElement.appendChild(host);
    // Next to the press, inside the page.
    var w = m.offsetWidth, h = m.offsetHeight;
    var left = Math.max(12, Math.min(x + 14, innerWidth - w - 12));
    var top = Math.max(12, Math.min(y + 14, innerHeight - h - 12));
    m.style.left = left + 'px';
    m.style.top = top + 'px';
    ask('overlay?1');
  }

  function cancel() {
    if (timer) {
      clearTimeout(timer);
      timer = 0;
    }
  }

  addEventListener('mousedown', function (e) {
    cancel();
    held = false;
    if (host && e.target === host) return;  // a press in the menu: its buttons'
    close();
    if (e.button !== 0) return;
    var what = find(e);
    if (!what) return;
    at = {x: e.clientX, y: e.clientY};
    timer = setTimeout(function () {
      timer = 0;
      held = true;
      swallow = true;  // the release of this press is not a click on the link
      open(what, at.x, at.y);
    }, HOLD);
  }, true);
  addEventListener('mousemove', function (e) {
    if (timer && at && Math.abs(e.clientX - at.x) + Math.abs(e.clientY - at.y) > MOVE) cancel();
  }, true);
  addEventListener('mouseup', function () {
    cancel();
    if (held) {  // the click, if one comes, comes before any timer
      held = false;
      setTimeout(function () { swallow = false; }, 0);
    }
  }, true);
  addEventListener('click', function (e) {
    if (!swallow || (host && e.target === host)) return;
    swallow = false;
    e.preventDefault();
    e.stopImmediatePropagation();
  }, true);
  addEventListener('dragstart', function (e) {
    cancel();
    if (host || held) e.preventDefault();
  }, true);
  addEventListener('scroll', function () { cancel(); close(); }, true);
  addEventListener('keydown', function (e) { if (e.key === 'Escape') close(); }, true);
  addEventListener('pagehide', function () { cancel(); if (host) { host.remove(); host = null; } });
}
