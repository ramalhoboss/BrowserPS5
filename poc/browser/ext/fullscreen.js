// BrowserPS5's full screen for videos (main.cpp, extensions_apply(); docs/
// 07-navegacao-e-definicoes.md), in every frame from the page's start. This
// WebKit has no full screen of its own, so the sites' full screen buttons
// did nothing: requestFullscreen() now makes the element cover the page, and
// the page asks the app (browserps5:fullscreen?1, from a frame of its own
// so the page itself never navigates) to hide the bar and give the page the
// whole screen. Circle ends it (the app calls __bps5exitFullscreen()).
function () {
  'use strict';
  if (window.__bps5full) return;
  window.__bps5full = true;
  // 2026-11-09 18:3x: WebKit's own full screen is on (main.cpp,
  // WKPreferencesSetFullScreenEnabled; view_enter_fullscreen): the sites'
  // buttons get it as in any browser, and none of the below is put. Only the
  // app's menu ("Ecrã inteiro") and circle, through the real API.
  if (typeof Element.prototype.requestFullscreen === 'function') {
    window.__bps5exitFullscreen = () => {
      if (!document.fullscreenElement) return false;
      document.exitFullscreen().catch(() => {});
      return true;
    };
    window.__bps5enterFullscreen = () => {
      const target = videoTarget();
      if (!target) return false;
      target.requestFullscreen().catch(() => {});
      return true;
    };
    return;
  }
  const COVER = 'position: fixed !important; inset: 0 !important; left: 0 !important;' +
    ' top: 0 !important; width: 100vw !important; height: 100vh !important;' +
    ' max-width: none !important; max-height: none !important; margin: 0 !important;' +
    ' z-index: 2147483647 !important; background: #000 !important; transform: none !important;' +
    ' border: 0 !important; border-radius: 0 !important;';
  let element = null;
  let saved = null;
  let rootOverflow = '';

  function tell(on) {
    if (window.top !== window) return;  // the main frame's says it
    const f = document.createElement('iframe');
    f.style.cssText = 'display: none';
    f.src = 'browserps5:fullscreen?' + (on ? 1 : 0);
    (document.body || document.documentElement).appendChild(f);
    setTimeout(() => f.remove(), 1000);
  }
  // The on...fullscreenchange handlers are gone too (with this WebKit's full
  // screen names): kept here and called as a browser would, for the element
  // and the document (18:1x: YouTube asked for <html>, got it, and did not
  // change to its full screen).
  const handlers = new WeakMap();
  function handler(object, name) {
    const h = handlers.get(object);
    return h ? h[name] : null;
  }
  function fire(target) {
    for (const type of ['fullscreenchange', 'webkitfullscreenchange']) {
      const event = new Event(type, {bubbles: true});
      target.dispatchEvent(event);
      for (const object of target === document ? [document] : [target, document]) {
        // (one set while the names were gone is a plain property of its own)
        const own = Object.getOwnPropertyDescriptor(object, 'on' + type);
        const h = handler(object, 'on' + type) || (own && own.value);
        if (typeof h === 'function') {
          try {
            h.call(object, event);
          } catch (e) {
          }
        }
      }
    }
    window.dispatchEvent(new Event('resize'));
  }
  function enter(target) {
    if (element === target) return Promise.resolve();
    if (element) exit();
    element = target;
    saved = element.getAttribute('style');
    element.setAttribute('style', (saved ? saved + ';' : '') + COVER);
    rootOverflow = document.documentElement.style.overflow;
    document.documentElement.style.overflow = 'hidden';
    tell(true);
    // As a browser does: the event a moment later, then the promise (YouTube
    // asks, then waits for the change; an event inside the call it missed)
    return later(target);
  }
  function later(target) {
    return new Promise((resolve) => setTimeout(() => {
      fire(target);
      resolve();
    }, 0));
  }
  function exit() {
    if (!element) return Promise.resolve();
    const was = element;
    element = null;
    if (saved === null) was.removeAttribute('style');
    else was.setAttribute('style', saved);
    document.documentElement.style.overflow = rootOverflow;
    tell(false);
    return later(was);
  }
  const getter = (name, get) => {
    try {
      Object.defineProperty(Document.prototype, name, {get, configurable: true});
    } catch (e) {
    }
  };
  // 2026-11-09 18:0x: what is put on Document and Element at the page's start
  // is gone by the time the page's scripts look (this WebKit drops the names
  // of its full screen, which is off, after the start; a probe on YouTube saw
  // none of them, and YouTube said the browser has no full screen). Put
  // again as the page loads, and checked for 30 s.
  function install() {
    if (Object.prototype.hasOwnProperty.call(Document.prototype, 'fullscreenEnabled')
        && Object.prototype.hasOwnProperty.call(Element.prototype, 'requestFullscreen')) return;
    getter('fullscreenElement', () => element);
    getter('webkitFullscreenElement', () => element);
    getter('webkitCurrentFullScreenElement', () => element);
    getter('fullscreenEnabled', () => true);
    getter('webkitFullscreenEnabled', () => true);
    getter('fullscreen', () => !!element);
    getter('webkitIsFullScreen', () => !!element);
    Element.prototype.requestFullscreen = function () { return enter(this); };
    Element.prototype.webkitRequestFullscreen = function () { enter(this); };
    Element.prototype.webkitRequestFullScreen = Element.prototype.webkitRequestFullscreen;
    Document.prototype.exitFullscreen = function () { return exit(); };
    Document.prototype.webkitExitFullscreen = function () { exit(); };
    Document.prototype.webkitCancelFullScreen = Document.prototype.webkitExitFullscreen;
    for (const name of ['onfullscreenchange', 'onwebkitfullscreenchange', 'onfullscreenerror',
                        'onwebkitfullscreenerror']) {
      for (const proto of [Document.prototype, Element.prototype]) {
        try {
          Object.defineProperty(proto, name, {
            configurable: true,
            get() { return handler(this, name); },
            set(f) {
              let h = handlers.get(this);
              if (!h) handlers.set(this, h = {});
              h[name] = typeof f === 'function' ? f : null;
            },
          });
        } catch (e) {
        }
      }
    }
  }
  install();
  document.addEventListener('readystatechange', install);
  document.addEventListener('DOMContentLoaded', install);
  window.addEventListener('load', install);
  let checks = 0;
  const timer = setInterval(() => {
    install();
    if (++checks >= 30) clearInterval(timer);
  }, 1000);
  if (window.HTMLVideoElement) {
    HTMLVideoElement.prototype.webkitEnterFullscreen = function () { enter(this); };
    HTMLVideoElement.prototype.webkitExitFullscreen = function () { exit(); };
    try {
      Object.defineProperty(HTMLVideoElement.prototype, 'webkitSupportsFullscreen',
                            {get: () => true, configurable: true});
    } catch (e) {
    }
  }
  // From the app (circle): true when there was a full screen to end.
  window.__bps5exitFullscreen = () => {
    if (!element) return false;
    exit();
    return true;
  };
  // From the app (the menu's "Ecrã inteiro", options then cross): the video
  // shown, a playing one first, covers the page with its player, the
  // outermost box around it of the same size (where the site keeps its
  // controls), as the site's own full screen button would. False when the
  // page shows no video: the app then gives the page itself the screen.
  window.__bps5enterFullscreen = () => {
    const target = videoTarget();
    if (!target) return false;
    enter(target);
    return true;
  };

  // The video shown, a playing one first, with the outermost box around it
  // of the same size (where the site keeps its controls); null: none.
  function videoTarget() {
    let video = null;
    let best = 0;
    for (const v of document.querySelectorAll('video')) {
      const r = v.getBoundingClientRect();
      const score = r.width * r.height * (v.paused ? 1 : 4);
      if (r.width >= 64 && r.height >= 36 && score > best) {
        video = v;
        best = score;
      }
    }
    if (!video) return null;
    const r = video.getBoundingClientRect();
    let target = video;
    for (let p = video.parentElement; p && p !== document.body && p !== document.documentElement;
         p = p.parentElement) {
      const q = p.getBoundingClientRect();
      if (Math.abs(q.width - r.width) > 4 || Math.abs(q.height - r.height) > 4) break;
      target = p;
    }
    return target;
  }
}
