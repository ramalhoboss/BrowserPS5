// BrowserPS5's controller for web games (main.cpp, extensions_apply(); docs/
// 07-navegacao-e-definicoes.md, "Modo jogo"), in every frame from the page's
// start. This WebKit has no Gamepad API of its own, and the console's
// controller belongs to the app (its title reads it). So:
// - navigator.getGamepads() and the gamepadconnected listeners are here; the
//   first time a page uses them it tells the app (browserps5:gamepad?want),
//   which offers the controller: a click on the touch pad gives it to the
//   game, another takes it back.
// - While the game has it, the app sends its state to the main frame
//   (__bps5pad(bits, lx, ly, rx, ry)) about 60 times a second when it
//   changes, and each frame passes it on to the frames inside it (the games
//   in a frame of their own, as on itch.io).
// - navigator.vibrate() and the gamepad's vibrationActuator make the
//   controller vibrate (browserps5:vibrate?ms,strong,weak), at most 20 times
//   a second; the app takes them only while the game has the controller or
//   right after a press.
function () {
  'use strict';
  if (window.__bps5pad) return;

  // libScePad's bits (input.h, INPUT_PAD_*), in the standard mapping's
  // order: cross, circle, square, triangle, L1, R1, L2, R2, (create: the
  // system's), options, L3, R3, up, down, left, right.
  const ORDER = [0x4000, 0x2000, 0x8000, 0x1000, 0x400, 0x800, 0x100, 0x200, 0,
                 0x8, 0x2, 0x4, 0x10, 0x40, 0x80, 0x20, 0];
  const L2 = 6, R2 = 7;

  function tell(command) {
    const f = document.createElement('iframe');
    f.style.cssText = 'display: none';
    f.src = 'browserps5:' + command;
    (document.body || document.documentElement).appendChild(f);
    setTimeout(() => f.remove(), 1000);
  }

  let asked = false;
  function want() {
    if (asked) return;
    asked = true;
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', () => tell('gamepad?want'), {once: true});
    } else {
      tell('gamepad?want');
    }
  }

  let lastBuzz = 0;
  function buzz(ms, strong, weak) {
    const now = Date.now();
    if (now - lastBuzz < 50) return false;
    lastBuzz = now;
    ms = Math.max(0, Math.min(1000, Math.round(+ms || 0)));
    const s = Math.max(0, Math.min(1, +strong || 0)), w = Math.max(0, Math.min(1, +weak || 0));
    tell('vibrate?' + ms + ',' + s.toFixed(2) + ',' + w.toFixed(2));
    return true;
  }

  const actuator = {
    type: 'dual-rumble',
    effects: ['dual-rumble'],
    canPlay: (type) => type === 'dual-rumble',
    playEffect(type, p) {
      p = p || {};
      buzz(p.duration || 200, p.strongMagnitude, p.weakMagnitude);
      return Promise.resolve('complete');
    },
    pulse(value, duration) {
      buzz(duration, value, value);
      return Promise.resolve(true);
    },
    reset() {
      buzz(0, 0, 0);
      return Promise.resolve('complete');
    },
  };

  let pad = null;  // the gamepad while the game has the controller
  const start = performance.now();
  function snapshot(bits, axes) {
    const buttons = ORDER.map((bit, i) => {
      let value = bit && (bits & bit) ? 1 : 0;
      if (i === L2) value = Math.max(value * 0.5, ((bits >>> 21) & 15) / 15);
      if (i === R2) value = Math.max(value * 0.5, ((bits >>> 25) & 15) / 15);
      return {pressed: value > 0.1, touched: value > 0, value};
    });
    return {
      id: 'DualSense Wireless Controller (BrowserPS5)', index: 0, connected: true,
      mapping: 'standard', timestamp: performance.now() - start, axes, buttons,
      vibrationActuator: actuator, hapticActuators: [actuator],
    };
  }

  function fire(type) {
    const e = new Event(type);
    Object.defineProperty(e, 'gamepad', {value: pad || snapshot(0, [0, 0, 0, 0])});
    window.dispatchEvent(e);
  }

  function apply(message) {
    if (message.on === 1 && !pad) {
      pad = snapshot(0, [0, 0, 0, 0]);
      fire('gamepadconnected');
    } else if (message.on === 0 && pad) {
      fire('gamepaddisconnected');
      pad = null;
    } else if (message.state && pad) {
      const [bits, lx, ly, rx, ry] = message.state;
      pad = snapshot(bits >>> 0, [lx, ly, rx, ry]);
    }
    for (let i = 0; i < window.frames.length; i++) {
      try {
        window.frames[i].postMessage({__bps5pad: message}, '*');
      } catch (e) {}
    }
  }

  // From the app (the main frame) and from the frame above (the rest).
  window.__bps5pad = (bits, lx, ly, rx, ry) => apply({state: [bits, lx, ly, rx, ry]});
  window.__bps5pad.connect = (on) => apply({on: on ? 1 : 0});
  window.addEventListener('message', (e) => {
    if (e.source === window.parent && window.parent !== window && e.data && e.data.__bps5pad)
      apply(e.data.__bps5pad);
  });

  const getGamepads = function () {
    want();
    return [pad, null, null, null];
  };
  try {
    Object.defineProperty(Navigator.prototype, 'getGamepads', {value: getGamepads, configurable: true});
    Object.defineProperty(Navigator.prototype, 'webkitGetGamepads', {value: getGamepads, configurable: true});
    Object.defineProperty(Navigator.prototype, 'vibrate', {
      configurable: true,
      value(pattern) {
        const ms = Array.isArray(pattern) ? pattern[0] : pattern;
        return buzz(ms, 1, 1);
      },
    });
  } catch (e) {}

  const add = EventTarget.prototype.addEventListener;
  EventTarget.prototype.addEventListener = function (type, ...rest) {
    if (this === window && (type === 'gamepadconnected' || type === 'gamepaddisconnected')) want();
    return add.call(this, type, ...rest);
  };
  for (const name of ['ongamepadconnected', 'ongamepaddisconnected']) {
    let handler = null;
    try {
      Object.defineProperty(window, name, {
        configurable: true,
        get: () => handler,
        set(f) {
          if (handler) window.removeEventListener(name.slice(2), handler);
          handler = typeof f === 'function' ? f : null;
          if (handler) window.addEventListener(name.slice(2), handler);
        },
      });
    } catch (e) {}
  }
}
