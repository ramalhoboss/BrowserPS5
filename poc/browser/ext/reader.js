// Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.
//
// BrowserPS5 RN's reading mode (2026-11-11; the menu's "Leitura", main.cpp
// ui_command "reader"): the page's article alone, in large type on a quiet
// background, for a TV three metres away. Run in the page when asked, and
// again to leave. It answers "on", "off", or "none" when the page has no
// article it can find.
//
// The article is the element whose paragraphs hold the most text (each long
// paragraph counts for its parent, and half for its grandparent). A copy of
// it is cleaned (no scripts, forms, menus, share and comment blocks, no
// attributes but the links' and the pictures' addresses) and shown in a
// shadow tree over the page, so the page's styles do not reach it. A-, A+
// and the three papers are kept for the site (localStorage).
(function () {
  'use strict';
  var ID = 'bps5-reader';
  // The app is told when it opens and closes (browserps5:overlay?1, from a
  // frame of its own): circle then closes it instead of going back a page
  // (main.cpp, overlay_input(); ext/linkmenu.js's __bps5overlayClose()).
  function tell(on) {
    try {
      var f = document.createElement('iframe');
      f.style.cssText = 'display: none';
      f.src = 'browserps5:overlay?' + (on ? 1 : 0);
      (document.body || document.documentElement).appendChild(f);
      setTimeout(function () { f.remove(); }, 1000);
    } catch (e) {}
  }
  function leave() {
    var shown = document.getElementById(ID);
    if (!shown)
      return false;
    shown.remove();
    document.documentElement.style.removeProperty('overflow');
    tell(false);
    return true;
  }
  if (leave())
    return 'off';

  function length(el) {
    return (el.textContent || '').replace(/\s+/g, ' ').trim().length;
  }

  // The article.
  var scores = new Map();
  function add(el, n) {
    if (el && el !== document.documentElement)
      scores.set(el, (scores.get(el) || 0) + n);
  }
  var outside = 'nav, footer, aside, form, [role=navigation], [role=complementary], [aria-hidden=true]';
  document.querySelectorAll('p, pre, blockquote').forEach(function (p) {
    var n = length(p);
    if (n < 80 || p.closest(outside))
      return;
    add(p.parentElement, n);
    add(p.parentElement && p.parentElement.parentElement, n / 2);
  });
  var best = null, bestScore = 0;
  scores.forEach(function (score, el) {
    if (score > bestScore) {
      best = el;
      bestScore = score;
    }
  });
  if (!best || bestScore < 500) {
    best = null;
    ['article', '[itemprop=articleBody]', 'main', '[role=main]'].some(function (q) {
      var el = document.querySelector(q);
      if (el && length(el) > 600)
        best = el;
      return !!best;
    });
  }
  if (!best)
    return 'none';
  // The whole article when the best block is most of one.
  var article = best.closest('article, [itemprop=articleBody]');
  if (article && length(article) < length(best) * 2.5)
    best = article;

  // Its pictures: only the big ones, with the address a lazy loader keeps.
  var pictures = [];
  best.querySelectorAll('img').forEach(function (img) {
    var src = img.currentSrc || img.src || img.getAttribute('data-src') || img.getAttribute('data-lazy-src') || '';
    var w = img.naturalWidth || img.width || +img.getAttribute('width') || 0;
    pictures.push(/^https?:/.test(src) && (w >= 300 || !img.complete) ? src : '');
  });

  var copy = best.cloneNode(true);
  var i = 0;
  copy.querySelectorAll('img').forEach(function (img) {
    var src = pictures[i++];
    if (src)
      img.setAttribute('data-keep', src);
  });
  var junk = /(^|[-_ ])(comment|share|social|related|promo|advert|ads?|newsletter|subscribe|cookie|banner|sidebar|footer|nav|menu|breadcrumb|toolbar|popup|modal|paywall|tags?|author-bio|recommend)(s|ed|ing)?([-_ ]|$)/i;
  copy.querySelectorAll('script, style, noscript, iframe, form, button, input, select, textarea, nav, aside, footer, svg, video, audio, canvas, object, embed, template, link, meta, [hidden], [aria-hidden=true]')
    .forEach(function (el) { el.remove(); });
  copy.querySelectorAll('*').forEach(function (el) {
    var name = (el.getAttribute('class') || '') + ' ' + (el.getAttribute('id') || '');
    if (junk.test(name) && length(el) < 400) {
      el.remove();
      return;
    }
    var tag = el.tagName;
    var keep = tag === 'A' ? el.href : tag === 'IMG' ? el.getAttribute('data-keep') : '';
    while (el.attributes.length)
      el.removeAttribute(el.attributes[0].name);
    if (tag === 'A' && /^https?:/.test(keep))
      el.setAttribute('href', keep);
    if (tag === 'IMG') {
      if (keep)
        el.setAttribute('src', keep);
      else
        el.remove();
    }
  });
  // Nothing left in it: gone (twice, for the wrappers of wrappers).
  for (var pass = 0; pass < 2; pass++)
    copy.querySelectorAll('div, span, p, section, figure, li, ul, ol').forEach(function (el) {
      if (!el.querySelector('img') && !length(el))
        el.remove();
    });
  if (length(copy) < 300)
    return 'none';

  var h1 = document.querySelector('h1');
  var title = (h1 && length(h1) > 3 ? h1.textContent : document.title).replace(/\s+/g, ' ').trim();
  var first = copy.querySelector('h1');
  if (first && first.textContent.replace(/\s+/g, ' ').trim() === title)
    first.remove();

  var saved = {};
  try { saved = JSON.parse(localStorage.getItem('bps5-reader') || '{}') || {}; } catch (e) {}
  var size = Math.min(44, Math.max(22, +saved.size || 30));
  var papers = ['night', 'sepia', 'day'];
  var paper = papers.indexOf(saved.paper) >= 0 ? saved.paper : 'night';
  function keep() {
    try { localStorage.setItem('bps5-reader', JSON.stringify({size: size, paper: paper})); } catch (e) {}
  }

  var host = document.createElement('div');
  host.id = ID;
  host.style.cssText = 'all: initial; position: fixed; left: 0; top: 0; width: 100%; height: 100%; z-index: 2147483647;';
  var root = host.attachShadow({mode: 'open'});
  var style = document.createElement('style');
  style.textContent =
    ':host { all: initial; }' +
    '#page { position: absolute; left: 0; top: 0; right: 0; bottom: 0; overflow-y: auto; overflow-x: hidden; font-family: Georgia, "Times New Roman", serif; line-height: 1.62; }' +
    '#page.night { background: #101318; color: #e6e3dc; } #page.sepia { background: #f3e9d2; color: #3b2f1e; } #page.day { background: #fbfbf8; color: #1b1b1b; }' +
    '#bar { position: sticky; top: 0; z-index: 2; display: flex; align-items: center; gap: 12px; padding: 14px 40px; font: 600 20px/1 system-ui, sans-serif; backdrop-filter: none; }' +
    '.night #bar { background: rgba(16, 19, 24, .96); } .sepia #bar { background: rgba(243, 233, 210, .97); } .day #bar { background: rgba(251, 251, 248, .97); }' +
    '#site { flex: 1; opacity: .65; overflow: hidden; white-space: nowrap; text-overflow: ellipsis; }' +
    'button { all: unset; cursor: pointer; min-width: 58px; height: 52px; padding: 0 18px; border-radius: 14px; text-align: center; font: 600 21px/52px system-ui, sans-serif; box-sizing: border-box; }' +
    '.night button { background: rgba(255, 255, 255, .1); color: #fff; } .sepia button, .day button { background: rgba(0, 0, 0, .08); color: inherit; }' +
    'button:hover, button:focus { outline: 3px solid #5b97ff; }' +
    '#wrap { max-width: 1160px; margin: 0 auto; padding: 34px 60px 220px; }' +
    'h1.t { font-size: 1.75em; line-height: 1.18; margin: 0 0 .7em; font-weight: 700; }' +
    'h1, h2, h3, h4 { line-height: 1.25; margin: 1.5em 0 .5em; } h2 { font-size: 1.35em; } h3, h4 { font-size: 1.15em; }' +
    'p, ul, ol, blockquote, pre, figure, table { margin: 0 0 1.05em; } li { margin: .3em 0; }' +
    'img { display: block; max-width: 100%; height: auto; margin: .6em auto; border-radius: 10px; }' +
    'figcaption, small { font-size: .72em; opacity: .7; }' +
    'blockquote { padding-left: 1em; border-left: 4px solid rgba(128, 128, 128, .5); font-style: italic; }' +
    'pre, code { font-family: ui-monospace, Menlo, Consolas, monospace; font-size: .8em; white-space: pre-wrap; }' +
    'table { border-collapse: collapse; font-size: .8em; } td, th { border: 1px solid rgba(128, 128, 128, .4); padding: .3em .6em; }' +
    '.night a { color: #8ab4ff; } .sepia a, .day a { color: #1a55b8; } a { text-decoration: none; } a:hover { text-decoration: underline; }';
  var page = document.createElement('div');
  page.id = 'page';
  var bar = document.createElement('div');
  bar.id = 'bar';
  var site = document.createElement('div');
  site.id = 'site';
  site.textContent = location.hostname.replace(/^www\./, '');
  function button(text, title, act) {
    var b = document.createElement('button');
    b.textContent = text;
    b.title = title;
    b.addEventListener('click', act);
    return b;
  }
  var wrap = document.createElement('div');
  wrap.id = 'wrap';
  function show() {
    page.className = paper;
    wrap.style.fontSize = size + 'px';
    keep();
  }
  bar.append(site,
    button('A−', 'Letra mais pequena', function () { size = Math.max(22, size - 2); show(); }),
    button('A+', 'Letra maior', function () { size = Math.min(44, size + 2); show(); }),
    button('◐', 'Papel', function () { paper = papers[(papers.indexOf(paper) + 1) % papers.length]; show(); }),
    button('✕', 'Sair da leitura', leave));
  var heading = document.createElement('h1');
  heading.className = 't';
  heading.textContent = title;
  wrap.append(heading, copy);
  page.append(bar, wrap);
  root.append(style, page);
  show();
  document.documentElement.appendChild(host);
  document.documentElement.style.setProperty('overflow', 'hidden', 'important');
  window.__bps5readerClose = leave;
  if (!window.__bps5overlayClose)
    window.__bps5overlayClose = function () { return window.__bps5readerClose(); };
  tell(true);
  return 'on';
})()
