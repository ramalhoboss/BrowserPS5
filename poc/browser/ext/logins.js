// BrowserPS5's saved logins (main.cpp, extensions_apply(); docs/12-contas.md),
// in the main frame of every page from its start, while the setting is on.
// When a form with a filled password is sent (its submit, Enter in the
// password, or a click on its button: many sites log in with a script and
// no submit), the user name and the password go to the app
// (browserps5:login-offer?<user>&<password>, each URL-encoded), which asks in its dialog whether to
// keep them for the page's origin (the app takes the origin from the page,
// never from here). A user name given on one page and the password on the
// next (Google, Microsoft) are put together: the name waits in this tab's
// sessionStorage for a few minutes.
//
// The keyboard fills them back (main.cpp, login_fill()): nothing here reads
// them.
function () {
  'use strict';
  if (window.__bps5logins || window.top !== window) return;
  window.__bps5logins = true;

  const NAME_KEY = '__bps5login_user';
  const NAME_MS = 5 * 60 * 1000;
  let last = '';
  let lastAt = 0;

  function tell(command, query) {
    const f = document.createElement('iframe');
    f.style.cssText = 'display: none';
    f.src = 'browserps5:' + command + '?' + query;
    (document.body || document.documentElement).appendChild(f);
    setTimeout(() => f.remove(), 1000);
  }

  function visible(f) {
    return !f.disabled && f.type !== 'hidden' && (f.offsetWidth > 0 || f.offsetHeight > 0);
  }

  function isName(f) {
    return /^(text|email|tel|)$/.test(f.type) && visible(f) && f.value.trim() !== '' &&
      !/search|captcha|code|otp/i.test((f.name || '') + ' ' + (f.id || '') + ' ' + (f.autocomplete || ''));
  }

  function remember(name) {
    try { sessionStorage.setItem(NAME_KEY, JSON.stringify({name, at: Date.now()})); } catch (e) {}
  }

  function remembered() {
    try {
      const v = JSON.parse(sessionStorage.getItem(NAME_KEY) || 'null');
      return v && Date.now() - v.at < NAME_MS ? String(v.name || '') : '';
    } catch (e) {
      return '';
    }
  }

  // The form (or the page) being sent: a password, and the name before it.
  function grab(root) {
    const inputs = Array.from((root || document).querySelectorAll('input'));
    const passwords = inputs.filter(f => f.type === 'password' && visible(f) && f.value);
    if (!passwords.length) {
      // A first step with only the name (marked as one, an e-mail, or the
      // only field of a form): kept for the next page.
      const names = inputs.filter(isName);
      const named = names.find(f => /username/.test(f.autocomplete || '') || f.type === 'email') ||
                    (root && root.tagName === 'FORM' && names.length === 1 ? names[0] : null);
      if (named) remember(named.value.trim());
      return;
    }
    // Sign-up and change forms have two or three: the first is the one to keep
    // when they agree, the new one (the last) when they do not.
    const pass = passwords.length > 1 && passwords[passwords.length - 1].value === passwords[passwords.length - 2].value
      ? passwords[passwords.length - 1] : passwords[0];
    let name = '';
    const marked = inputs.find(f => /username/.test(f.autocomplete || '') && isName(f));
    if (marked) {
      name = marked.value.trim();
    } else {
      for (let i = inputs.indexOf(pass) - 1; i >= 0; i--) {
        if (isName(inputs[i])) {
          name = inputs[i].value.trim();
          break;
        }
      }
    }
    if (!name) name = remembered();
    const what = name + '\n' + pass.value;
    const now = Date.now();
    if (what === last && now - lastAt < 10000) return;
    last = what;
    lastAt = now;
    tell('login-offer', encodeURIComponent(name) + '&' + encodeURIComponent(pass.value));
  }

  document.addEventListener('submit', e => grab(e.target), true);
  document.addEventListener('keydown', e => {
    const t = e.target;
    if (e.key === 'Enter' && t && t.tagName === 'INPUT') grab(t.form || document);
  }, true);
  document.addEventListener('click', e => {
    const b = e.target && e.target.closest &&
      e.target.closest('button, input[type=submit], input[type=button], [role=button]');
    if (!b) return;
    const form = b.form || b.closest('form');
    // Grab after the page's own handler had the click (it may fill or check).
    setTimeout(() => grab(form || document), 0);
  }, true);
}
