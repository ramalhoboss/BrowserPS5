// BrowserPS5's dark mode for sites (main.cpp, extensions_apply(); docs/07-
// navegacao-e-definicoes.md), in the main frame from the page's start: a
// light page is turned round (its colours inverted, the hues kept), its
// pictures and videos turned back. A page that is dark already is left as it
// is, once its colours are known.
function () {
  'use strict';
  if (window.__bps5dark) return;
  window.__bps5dark = true;
  const style = document.createElement('style');
  style.id = 'bps5-dark';
  style.textContent =
    'html { filter: invert(.9) hue-rotate(180deg) !important; background: #fff !important; }\n' +
    'img, video, picture, canvas, iframe, embed, object, svg image, [style*="background-image"]' +
    ' { filter: invert(1) hue-rotate(180deg) !important; }';
  (document.head || document.documentElement).appendChild(style);

  // The page's own background: dark (luminance under .4) or light.
  function luminance(color) {
    const m = /rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)/.exec(color || '');
    if (!m || (m[4] !== undefined && Number(m[4]) < 0.5)) return null;
    const c = [m[1], m[2], m[3]].map((v) => {
      v = Number(v) / 255;
      return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
    });
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  }
  function check() {
    const body = document.body ? luminance(getComputedStyle(document.body).backgroundColor) : null;
    const root = luminance(getComputedStyle(document.documentElement).backgroundColor);
    const l = body !== null ? body : root !== null ? root : 1;  // none: white
    style.disabled = l < 0.4;
  }
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', check);
  } else {
    check();
  }
  window.addEventListener('load', check);
}
