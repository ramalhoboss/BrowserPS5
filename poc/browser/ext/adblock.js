// BrowserPS5's ad blocker, in each page from its start (main.cpp,
// extensions_apply(); docs/07-navegacao-e-definicoes.md), called with
// {hosts, allow}: the hosts to refuse (ext/adhosts.txt, the trackers too when
// the setting says so) and the sites where it does nothing. With no content
// rules in this WebKit it works from inside the page: the scripts, frames,
// pictures and requests the page adds for those hosts never load (the app
// refuses their frames as well), and the usual ad boxes are hidden. What it
// stopped is counted in window.__bps5ads for the menu.
function (options) {
  'use strict';
  if (window.__bps5ads) return;
  const site = location.hostname.replace(/^www\./, '');
  const under = (host, list) => {
    for (let h = host; h; h = h.slice(h.indexOf('.') + 1 || h.length)) {
      if (list.has(h)) return true;
      if (h.indexOf('.') < 0) break;
    }
    return false;
  };
  const allow = new Set(options.allow || []);
  const stats = window.__bps5ads = {count: 0, hosts: {}, on: !under(site, allow)};
  if (!stats.on) return;
  const hosts = new Set(options.hosts || []);
  if (under(site, hosts)) return;  // the ad network's own site, opened on purpose

  function blocked(url) {
    if (!url || typeof url !== 'string' && !(url instanceof URL)) return false;
    let u;
    try {
      u = new URL(String(url), location.href);
    } catch (e) {
      return false;
    }
    if (!/^https?:$/.test(u.protocol)) return false;
    if (!under(u.hostname, hosts)) return false;
    stats.count++;
    stats.hosts[u.hostname] = (stats.hosts[u.hostname] || 0) + 1;
    return true;
  }

  // Elements: their src (or href, for a stylesheet) before it loads.
  const SRC = [
    [window.HTMLScriptElement, 'src'], [window.HTMLIFrameElement, 'src'],
    [window.HTMLImageElement, 'src'], [window.HTMLEmbedElement, 'src'],
    [window.HTMLSourceElement, 'src'], [window.HTMLLinkElement, 'href'],
  ];
  for (const [type, name] of SRC) {
    if (!type) continue;
    const d = Object.getOwnPropertyDescriptor(type.prototype, name);
    if (!d || !d.set) continue;
    Object.defineProperty(type.prototype, name, {
      configurable: true, enumerable: d.enumerable, get: d.get,
      set(value) {
        if (blocked(value)) {
          if (this.tagName === 'SCRIPT') {
            // As if the network failed: what waits for it goes on.
            setTimeout(() => this.dispatchEvent(new Event('error')), 0);
          }
          return;
        }
        d.set.call(this, value);
      },
    });
  }
  const setAttribute = Element.prototype.setAttribute;
  Element.prototype.setAttribute = function (name, value) {
    const n = String(name).toLowerCase();
    if ((n === 'src' || (n === 'href' && this.tagName === 'LINK')) &&
        /^(SCRIPT|IFRAME|IMG|EMBED|SOURCE|LINK)$/.test(this.tagName) && blocked(value)) {
      if (this.tagName === 'SCRIPT') setTimeout(() => this.dispatchEvent(new Event('error')), 0);
      return;
    }
    return setAttribute.call(this, name, value);
  };

  // Requests from the page's scripts.
  const fetch0 = window.fetch;
  if (fetch0) {
    window.fetch = function (input, init) {
      const url = input && typeof input === 'object' && 'url' in input ? input.url : input;
      if (blocked(url)) return Promise.reject(new TypeError('Failed to fetch'));
      return fetch0.call(this, input, init);
    };
  }
  const open0 = XMLHttpRequest.prototype.open;
  const send0 = XMLHttpRequest.prototype.send;
  XMLHttpRequest.prototype.open = function (method, url, ...rest) {
    this.__bps5blocked = blocked(url);
    return open0.call(this, method, this.__bps5blocked ? 'about:blank' : url, ...rest);
  };
  XMLHttpRequest.prototype.send = function (body) {
    if (this.__bps5blocked) {
      setTimeout(() => this.dispatchEvent(new Event('error')), 0);
      return;
    }
    return send0.call(this, body);
  };
  if (navigator.sendBeacon) {
    const beacon = navigator.sendBeacon.bind(navigator);
    navigator.sendBeacon = (url, data) => blocked(url) ? true : beacon(url, data);
  }
  const open = window.open;
  window.open = function (url, ...rest) {
    return blocked(url) ? null : open.call(this, url, ...rest);
  };

  // HTML a script writes while the page loads (the old ad tags): the
  // scripts, frames and pictures of those hosts lose their address.
  const TAG = /<(script|iframe|img|embed)\b[^>]*?\bsrc\s*=\s*(["']?)([^"'\s>]+)\2/gi;
  const clean = (html) => typeof html !== 'string' || html.indexOf('src') < 0 ? html
    : html.replace(TAG, (tag, name, quote, url) => blocked(url) ? tag.replace(url, '') : tag);
  for (const name of ['write', 'writeln']) {
    const write = Document.prototype[name];
    if (!write) continue;
    Document.prototype[name] = function (...parts) {
      return write.apply(this, parts.map(clean));
    };
  }

  // Frames and scripts already in the page's own HTML, as the parser adds
  // them (some still load: the app refuses their frames).
  // Links too: a link's ping (who clicked, told to a tracker) goes, and a
  // link that is itself an ad (it goes through an ad network) is hidden.
  const SWEPT = 'iframe[src], script[src], img[src], a[href], a[ping]';
  const sweep = (node) => {
    if (node.nodeType !== 1) return;
    const list = node.matches && node.matches(SWEPT) ? [node]
               : node.querySelectorAll ? node.querySelectorAll(SWEPT) : [];
    for (const e of list) {
      if (e.tagName === 'A') {
        if (e.hasAttribute('ping')) e.removeAttribute('ping');
        const href = e.getAttribute('href');
        if (href && /^https?:/i.test(href) && blocked(href))
          e.style.setProperty('display', 'none', 'important');
        continue;
      }
      if (blocked(e.getAttribute('src'))) {
        e.removeAttribute('src');
        e.style.setProperty('display', 'none', 'important');
      }
    }
  };
  new MutationObserver((records) => {
    for (const r of records)
      for (const n of r.addedNodes) sweep(n);
  }).observe(document.documentElement, {childList: true, subtree: true});

  // The usual ad boxes, hidden.
  const css = [
    'ins.adsbygoogle', '.adsbygoogle', '[id^="google_ads_iframe"]', '[id^="div-gpt-ad"]',
    '[id^="gpt-ad"]', '[id^="ad-slot"]', '[class^="ad-slot"]', '[data-ad-slot]',
    '[data-google-query-id]', 'amp-ad', 'amp-embed[type="taboola"]', '.taboola',
    '[id^="taboola-"]', '.trc_rbox_container', '.OUTBRAIN', '.ob-widget', '[data-widget-id^="AR_"]',
    '.ad-banner', '.adBanner', '.banner-ad', '.advert', '.advertisement', '.ad-container',
    '.ad-wrapper', '.ad-unit', '.adunit', '.dfp-ad', '.top-ad', '.sidebar-ad',
    '.sponsored-ad', 'iframe[src*="doubleclick.net"]', 'iframe[src*="googlesyndication.com"]',
    'iframe[id^="google_ads"]', 'div[aria-label="Advertisement"]', 'div[aria-label="Publicidade"]',
    'iframe[name^="google_ads_iframe"]', 'iframe[title="3rd party ad content"]',
    'iframe[title="Advertisement"]', 'iframe[aria-label="Advertisement"]',
    '[data-ad]', '[data-ad-unit]', '[data-adunit]', '[data-ad-client]', '[data-native-ad]',
    '[id^="ezoic-pub-ad"]', '.ezoic-ad', '.adthrive-ad', '[class^="adthrive"]', '.mv-ad-box',
    '[id^="mgid"]', '.mgid', '[id^="rc-widget"]', '.revcontent', '[id*="taboola"]', '.zergnet-widget',
    '.adsbox', '.ad-box', '.adbox', '.ad-placeholder', '.ad-slot', '.ad-banner-container',
    '.GoogleActiveViewElement', '[data-google-av-cxn]', '.pub_300x250', '.pub_728x90',
    '.text-ad', '.textAd', '.text_ad', '.publicidade', '[id^="publicidade"]',
    'section[aria-label="Publicidade"]', 'aside[aria-label="Publicidade"]',
    'section[aria-label="Advertisement"]', 'aside[aria-label="Advertisement"]',
  ].join(',\n') + ' { display: none !important; }';
  const style = document.createElement('style');
  style.id = 'bps5-adblock';
  style.textContent = css;
  (document.head || document.documentElement).appendChild(style);
}
