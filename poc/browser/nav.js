// BrowserPS5: the D-pad's way through a page (docs/07-navegacao-e-
// definicoes.md). The app evaluates "(" + this + ")(cmd, x, y, fresh)" in
// the bar or the page, with cmd one of "up", "down", "left", "right" (move),
// "refresh" (where is the chosen element now), "near" (the magnet: what is
// under the cursor or nearest it, when the stick stops) or "clear"; (x, y) is the
// cursor in the page's CSS pixels; fresh is 1 when the cursor moved since
// the last choice, so the move starts from the cursor and not from the
// element chosen before. The answer is one line:
//   "sel X Y W H KIND"  the element chosen, its box in the viewport;
//                       KIND is text, link, media or button
//   "scroll"            the page scrolled a step, nothing new chosen
//   "edge DIR"          nothing more that way and no scrolling left
//   "none"              nothing chosen
(function (cmd, cx, cy, fresh) {
  'use strict';
  const st = window.__bps5nav || (window.__bps5nav = { el: null, until: 0, box: null });
  const vw = window.innerWidth, vh = window.innerHeight;
  const SELECTOR = [
    'a[href]', 'area[href]', 'button', 'input:not([type=hidden])', 'select',
    'textarea', 'summary', 'video[controls]', 'label[for]',
    '[contenteditable=""]', '[contenteditable="true"]', '[onclick]',
    '[tabindex]:not([tabindex="-1"])', '[role=button]', '[role=link]',
    '[role=checkbox]', '[role=radio]', '[role=switch]', '[role=tab]',
    '[role=menuitem]', '[role=option]', '[role=combobox]', '[role=textbox]',
    '[role=searchbox]', '[role=slider]', '[role=treeitem]',
  ].join(',');
  const TEXT_TYPES = /^(text|search|email|url|password|number|tel|date|time|datetime-local|month|week)$/;
  const MARGIN = 56;  // kept between a chosen element and the viewport's edge

  function kind(el) {
    const tag = el.tagName;
    if (tag === 'TEXTAREA' || el.isContentEditable
        || (tag === 'INPUT' && TEXT_TYPES.test(el.type || 'text'))
        || el.getAttribute('role') === 'textbox' || el.getAttribute('role') === 'searchbox')
      return 'text';
    if (tag === 'VIDEO') return 'media';
    if (tag === 'A' || tag === 'AREA' || el.getAttribute('role') === 'link') return 'link';
    return 'button';
  }

  // Can be seen and clicked: big enough, shown, enabled and, when in view,
  // not under something else (a dialog, a sticky bar).
  function usable(el, r) {
    if (r.width < 4 || r.height < 4) return false;
    if (el.disabled || el.getAttribute('aria-hidden') === 'true') return false;
    const s = getComputedStyle(el);
    if (s.visibility !== 'visible' || s.pointerEvents === 'none' || parseFloat(s.opacity) < 0.05)
      return false;
    const points = [[0.5, 0.5], [0.25, 0.5], [0.75, 0.5]];
    let inView = 0;
    for (const [fx, fy] of points) {
      const x = r.left + r.width * fx, y = r.top + r.height * fy;
      if (x < 0 || x >= vw || y < 0 || y >= vh) continue;
      inView++;
      const top = document.elementFromPoint(x, y);
      if (!top || top === el || el.contains(top) || top.contains(el)) return true;
      // A label over an input, or the input's own parts.
      if (top.tagName === 'LABEL' && top.control === el) return true;
    }
    return inView === 0;  // out of view: cannot tell, take it
  }

  function edges(r) {
    return { l: r.left, t: r.top, r: r.right, b: r.bottom,
             cx: r.left + r.width / 2, cy: r.top + r.height / 2,
             w: r.width, h: r.height };
  }

  // How far a box is from "from" going dir; Infinity when it is not that way.
  function score(dir, from, c) {
    let primary, gap, align;
    if (dir === 'down' || dir === 'up') {
      const down = dir === 'down';
      if (down ? !(c.cy > from.cy + 1 && c.b > from.b + 1)
               : !(c.cy < from.cy - 1 && c.t < from.t - 1)) return Infinity;
      primary = Math.max(0, down ? c.t - from.b : from.t - c.b);
      gap = Math.max(0, c.l - from.r, from.l - c.r);
      align = Math.abs(c.cx - from.cx);
    } else {
      const right = dir === 'right';
      if (right ? !(c.cx > from.cx + 1 && c.r > from.r + 1)
                : !(c.cx < from.cx - 1 && c.l < from.l - 1)) return Infinity;
      primary = Math.max(0, right ? c.l - from.r : from.l - c.r);
      gap = Math.max(0, c.t - from.b, from.t - c.b);
      align = Math.abs(c.cy - from.cy);
    }
    return primary + 2.5 * gap + 0.25 * align;
  }

  // The element that scrolls an element's content: the page itself unless
  // an ancestor scrolls (an inner list, a web app's main pane).
  function scroller(el) {
    for (let p = el && el.parentElement; p && p !== document.body && p !== document.documentElement;
         p = p.parentElement) {
      const s = getComputedStyle(p);
      if (/(auto|scroll)/.test(s.overflowY + s.overflowX)
          && (p.scrollHeight > p.clientHeight + 2 || p.scrollWidth > p.clientWidth + 2))
        return p;
    }
    return null;
  }

  // Scrolls the window smoothly by (dx, dy), as far as it goes; the real
  // amount back.
  function scrollWindow(dx, dy) {
    const se = document.scrollingElement || document.documentElement;
    const maxX = Math.max(0, se.scrollWidth - vw), maxY = Math.max(0, se.scrollHeight - vh);
    const x = Math.min(maxX, Math.max(0, window.scrollX + dx));
    const y = Math.min(maxY, Math.max(0, window.scrollY + dy));
    const real = { dx: x - window.scrollX, dy: y - window.scrollY };
    if (real.dx || real.dy) window.scrollTo({ left: x, top: y, behavior: 'smooth' });
    return real;
  }

  function report(el, r) {
    st.el = el;
    st.box = [r.left, r.top, r.width, r.height];
    return 'sel ' + st.box.map(Math.round).join(' ') + ' ' + kind(el);
  }

  // The element in view again, after a smooth scroll that ends in a moment:
  // its box at the end, so the ring and the cursor do not wait.
  function bringIntoView(el, r) {
    const inner = scroller(el);
    if (inner) {
      el.scrollIntoView({ block: 'nearest', inline: 'nearest' });
      return el.getBoundingClientRect();
    }
    let dy = 0, dx = 0;
    if (r.bottom > vh - MARGIN) dy = Math.min(r.top - MARGIN, r.bottom - (vh - MARGIN));
    else if (r.top < MARGIN) dy = r.top - MARGIN;
    if (r.right > vw) dx = r.right - vw + 16;
    else if (r.left < 0) dx = r.left - 16;
    if (!dx && !dy) return r;
    const real = scrollWindow(dx, dy);
    st.until = performance.now() + 600;
    return new DOMRect(r.left - real.dx, r.top - real.dy, r.width, r.height);
  }

  // A step of scrolling that way when there is nothing to choose; false
  // when nothing scrolls any more.
  function step(dir) {
    const dy = dir === 'down' ? vh * 0.6 : dir === 'up' ? -vh * 0.6 : 0;
    const dx = dir === 'right' ? vw * 0.6 : dir === 'left' ? -vw * 0.6 : 0;
    const inner = scroller(st.el || document.elementFromPoint(vw / 2, vh / 2));
    if (inner) {
      const before = [inner.scrollLeft, inner.scrollTop];
      inner.scrollBy({ left: dx, top: dy, behavior: 'instant' });
      if (inner.scrollLeft !== before[0] || inner.scrollTop !== before[1]) {
        st.until = 0;  // already there: "refresh" reads the real box
        return true;
      }
    }
    const real = scrollWindow(dx, dy);
    if (!real.dx && !real.dy) return false;
    // The chosen element, where it will be when the scroll ends.
    if (st.el && st.box) {
      st.box = [st.box[0] - real.dx, st.box[1] - real.dy, st.box[2], st.box[3]];
      st.until = performance.now() + 600;
    }
    return true;
  }

  if (cmd === 'clear') {
    st.el = null;
    return 'none';
  }
  if (cmd === 'refresh') {
    if (!st.el || !st.el.isConnected) return 'none';
    if (performance.now() < st.until && st.box) {
      return 'sel ' + st.box.map(Math.round).join(' ') + ' ' + kind(st.el);
    }
    const r = st.el.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) return 'none';
    return report(st.el, r);
  }
  // The magnet (the stick stopped at the cursor): what is under it, or the
  // nearest clickable within MAGNET pixels of it, its whole box in view.
  if (cmd === 'near') {
    const MAGNET = 40;
    // 2026-11-09: no
    // magnet on a video's player, its seek bar or any slider; near a thin
    // bar the cursor jumped to its middle, and a press there seeks.
    for (const v of document.querySelectorAll('video')) {
      const r = v.getBoundingClientRect();
      if (r.width > 0 && cx >= r.left && cx < r.right && cy >= r.top && cy < r.bottom + MAGNET)
        return 'none';
    }
    const NO_MAGNET = '[role=slider],input[type=range],[aria-valuenow],[class*=progress],[class*=seek],[class*=scrub]';
    const under = document.elementFromPoint(cx, cy);
    if (under && under.closest && under.closest(NO_MAGNET)) return 'none';
    const hit = under && under.closest && under.closest(SELECTOR);
    if (hit) {
      const r = hit.getBoundingClientRect();
      return usable(hit, r) ? report(hit, r) : 'none';
    }
    let best = null, bestD = MAGNET, bestBox = null;
    for (const el of document.querySelectorAll(SELECTOR)) {
      const r = el.getBoundingClientRect();
      if (r.right < cx - MAGNET || r.left > cx + MAGNET || r.bottom < cy - MAGNET
          || r.top > cy + MAGNET) continue;
      if (r.top < 0 || r.left < 0 || r.bottom > vh || r.right > vw) continue;
      if (el.closest(NO_MAGNET)) continue;
      const dx = Math.max(r.left - cx, 0, cx - r.right);
      const dy = Math.max(r.top - cy, 0, cy - r.bottom);
      const d = Math.hypot(dx, dy);
      if (d >= bestD || !usable(el, r)) continue;
      best = el;
      bestD = d;
      bestBox = r;
    }
    return best ? report(best, bestBox) : 'none';
  }

  // Where the move starts: the chosen element, or the cursor (and what is
  // under it, if that can be chosen).
  let current = !fresh && st.el && st.el.isConnected ? st.el : null;
  if (!current) {
    const under = document.elementFromPoint(cx, cy);
    const near = under && under.closest && under.closest(SELECTOR);
    if (near && usable(near, near.getBoundingClientRect())) current = near;
  }
  const from = current ? edges(current.getBoundingClientRect())
                       : { l: cx, r: cx, t: cy, b: cy, cx: cx, cy: cy, w: 0, h: 0 };

  let best = null, bestScore = Infinity, bestBox = null;
  const range = { t: -vh * 1.6, b: vh * 2.6, l: -vw, r: vw * 2 };
  for (const el of document.querySelectorAll(SELECTOR)) {
    if (current && (el === current || el.contains(current) || current.contains(el))) continue;
    const r = el.getBoundingClientRect();
    if (r.bottom < range.t || r.top > range.b || r.right < range.l || r.left > range.r) continue;
    const s = score(cmd, from, edges(r));
    if (s >= bestScore) continue;
    if (!usable(el, r)) continue;
    best = el;
    bestScore = s;
    bestBox = r;
  }

  // Near enough: choose it and bring it into view. Far, or none: scroll a
  // step (the next press finds what came into view), or say there is no
  // more that way.
  const far = vh * 0.85;
  if (best && bestScore <= far) {
    return report(best, bringIntoView(best, bestBox));
  }
  if (step(cmd)) return 'scroll';
  if (best) return report(best, bringIntoView(best, bestBox));
  return 'edge ' + cmd;
})
