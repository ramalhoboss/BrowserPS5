// BrowserPS5 as a user script manager for the script sites (main.cpp,
// extensions_apply(); docs/10-achados-do-desktop.md, 6). Greasy Fork's
// "Install" asks the page for a manager (window.external.Tampermonkey,
// Violentmonkey or FireMonkey) and, finding none, showed the "get
// Tampermonkey" help instead of the script. Here it finds one with
// Violentmonkey's two calls, answered from the scripts installed in the
// browser, so "Install" goes to the .user.js, which the browser offers on
// its settings page as before (main.cpp, extension_offer()), and the button
// says "Update" or "Reinstall" for one that is there. Called with that list:
// [{name, version}]. Main frame, from the page's start, on these sites only.
function (installed) {
  'use strict';
  if (!/(^|\.)(greasyfork|sleazyfork)\.org$/.test(location.hostname)) return;
  const version = (name) => {
    const found = installed.find((s) => s.name === name);
    return found ? found.version || '0' : null;
  };
  const manager = Object.freeze({
    name: 'BrowserPS5',
    getVersion: () => Promise.resolve('2.0.0'),
    isInstalled: (name) => Promise.resolve(version(String(name))),
  });
  try {
    if (!window.external || typeof window.external !== 'object') {
      Object.defineProperty(window, 'external', {value: {}, configurable: true, writable: true});
    }
    Object.defineProperty(window.external, 'Violentmonkey', {value: manager, configurable: true});
  } catch (e) {
    // A page that took window.external first: its own help shows, as before.
  }
}
