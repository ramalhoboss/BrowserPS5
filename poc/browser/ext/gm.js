// BrowserPS5: what a user script finds around it (settings.c, ext_wrap();
// docs/07-navegacao-e-definicoes.md). Called with the script's own
// description (info: id, name, version, description, matches, excludes) and
// the script as a function of the GM_* names; runs it only where its @match
// and @include lines say and its @exclude lines do not. The GM_* functions
// are those of Greasemonkey and Tampermonkey that most scripts use; their
// values live in the site's localStorage, under the script's id, and
// GM_xmlhttpRequest is fetch(), so it reaches only what the site may.
function (info, run) {
  'use strict';
  // Never in the browser's own pages (file:, the start page and the
  // settings), which can change what the browser does.
  if (location.protocol === 'file:' || location.protocol === 'about:') return;
  const here = location.href.split('#')[0];
  const esc = (s) => s.replace(/[.+?^${}()|[\]\\]/g, '\\$&');

  // A @match pattern (scheme://host/path), an @include glob, or /regex/.
  function pattern(p) {
    if (p === '*' || p === '<all_urls>' || p === 'http*://*' || p === '*://*/*')
      return /^(https?|file):/;
    const last = p.lastIndexOf('/');
    if (p.length > 2 && p[0] === '/' && last > 0) {
      try {
        return new RegExp(p.slice(1, last), p.slice(last + 1).replace(/[^imsu]/g, ''));
      } catch (e) {
        return null;
      }
    }
    const m = /^(\*|https?|file|http\*):\/\/(\*|\*\.[^/]+|[^/*]*)(\/.*)?$/.exec(p);
    if (m) {
      const scheme = m[1] === '*' || m[1] === 'http*' ? 'https?' : m[1];
      const host = m[2] === '*' ? '[^/]*'
                 : m[2].startsWith('*.') ? '([^/]*\\.)?' + esc(m[2].slice(2)) : esc(m[2]);
      const path = (m[3] || '/*').split('*').map(esc).join('.*');
      return new RegExp('^' + scheme + ':\\/\\/' + host + '(:\\d+)?' + path + '$');
    }
    return new RegExp('^' + p.split('*').map(esc).join('.*') + '$');
  }
  const test = (list) => list.some((p) => {
    const r = pattern(p);
    return r && r.test(here);
  });
  if ((info.matches.length && !test(info.matches)) || test(info.excludes))
    return;

  const prefix = 'bps5us:' + info.id + ':';
  let store = null;
  try {
    store = window.localStorage;
  } catch (e) {
  }
  const memory = {};
  const GM_getValue = (key, value) => {
    const v = store ? store.getItem(prefix + key) : memory[key];
    if (v === null || v === undefined) return value;
    try {
      return JSON.parse(v);
    } catch (e) {
      return value;
    }
  };
  const GM_setValue = (key, value) => {
    const v = JSON.stringify(value);
    if (store) {
      try {
        store.setItem(prefix + key, v);
      } catch (e) {
        memory[key] = v;
      }
    } else {
      memory[key] = v;
    }
  };
  const GM_deleteValue = (key) => {
    if (store) store.removeItem(prefix + key);
    delete memory[key];
  };
  const GM_listValues = () => {
    const keys = Object.keys(memory);
    if (store)
      for (let i = 0; i < store.length; i++) {
        const k = store.key(i);
        if (k && k.startsWith(prefix)) keys.push(k.slice(prefix.length));
      }
    return keys;
  };
  const GM_addStyle = (css) => {
    const style = document.createElement('style');
    style.textContent = css;
    (document.head || document.documentElement).appendChild(style);
    return style;
  };
  const GM_xmlhttpRequest = (d) => {
    const controller = window.AbortController ? new AbortController() : null;
    const options = {method: d.method || 'GET', headers: d.headers || {}, body: d.data,
                     credentials: d.anonymous ? 'omit' : 'include'};
    if (controller) options.signal = controller.signal;
    fetch(d.url, options).then((r) => r.text().then((text) => {
      const response = {
        status: r.status, statusText: r.statusText, readyState: 4, finalUrl: r.url,
        responseText: text, responseHeaders: Array.from(r.headers).map((h) => h.join(': ')).join('\r\n'),
        response: text,
      };
      if (d.responseType === 'json') {
        try {
          response.response = JSON.parse(text);
        } catch (e) {
          response.response = null;
        }
      }
      if (d.onload) d.onload(response);
      if (d.onloadend) d.onloadend(response);
    })).catch((e) => {
      if (d.onerror) d.onerror({error: String(e), status: 0});
      if (d.onloadend) d.onloadend({error: String(e), status: 0});
    });
    return {abort: () => controller && controller.abort()};
  };
  const GM_openInTab = (url) => {
    location.href = url;
    return {close() {}};
  };
  const GM_setClipboard = () => {};
  const GM_notification = (d) => console.log('[' + info.name + ']', typeof d === 'string' ? d : d && d.text);
  const GM_registerMenuCommand = () => 0;
  const GM_unregisterMenuCommand = () => {};
  const GM_getResourceText = () => '';
  const GM_getResourceURL = () => '';
  const GM_log = (...a) => console.log('[' + info.name + ']', ...a);
  const GM_info = {
    script: {name: info.name, version: info.version, description: info.description,
             matches: info.matches, excludes: info.excludes},
    scriptHandler: 'BrowserPS5', version: '1.0',
  };
  const later = (f) => (...a) => Promise.resolve().then(() => f(...a));
  const GM = {
    info: GM_info, getValue: later(GM_getValue), setValue: later(GM_setValue),
    deleteValue: later(GM_deleteValue), listValues: later(GM_listValues),
    addStyle: later(GM_addStyle), xmlHttpRequest: GM_xmlhttpRequest,
    openInTab: GM_openInTab, setClipboard: later(GM_setClipboard),
    notification: GM_notification, registerMenuCommand: GM_registerMenuCommand,
    getResourceUrl: later(GM_getResourceURL),
  };
  try {
    run.call(window, GM_info, GM_addStyle, GM_getValue, GM_setValue, GM_deleteValue,
             GM_listValues, GM_xmlhttpRequest, GM_openInTab, GM_setClipboard,
             GM_notification, GM_registerMenuCommand, GM_unregisterMenuCommand,
             GM_getResourceText, GM_getResourceURL, GM_log, window, GM);
  } catch (e) {
    console.error('[BrowserPS5] ' + info.name + ': ' + e);
  }
}
