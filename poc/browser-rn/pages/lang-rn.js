
// 2026-11-14: BrowserPS5 RN's own texts in English (browser-rn/pages/lang-rn.js: build-rn.sh puts this at the
// end of the copy's lang.js, so v1's file stays as it is). The settings' RN-only switches and lists were shown
// in Portuguese in an English browser.
Object.assign(LANG_EN, {
  "Na página de cada jogo, «Enviar para o Discord» manda a captura para um canal teu. Cola aqui o webhook do canal: no Discord, Definições do servidor → Integrações → Webhooks → Copiar URL do webhook.": "On each game's page, \"Send to Discord\" sends the capture to a channel of yours. Paste the channel's webhook here: in Discord, Server Settings → Integrations → Webhooks → Copy Webhook URL.",
  "Nenhum webhook guardado.": "No webhook kept.",
  "Webhook guardado": "Webhook kept",
  "Guardar o webhook": "Keep the webhook",
  "Esquecer": "Forget",
  "Isso não é o endereço de um webhook do Discord": "That is not a Discord webhook address",
  "Webhook esquecido": "Webhook forgotten",
  'Comando como teclado': 'Controller as keyboard',
  'Guardar no jogo': 'Keep with the game',
  'O teu jogo': 'Your game',
  'As notas e as páginas que guardas com o jogo, escritas com o teclado do telemóvel. Aparecem na página do jogo na TV.': 'The notes and pages you keep with the game, typed with the phone\'s keyboard. They show on the game\'s page on the TV.',
  'Ainda não há jogos nesta consola.': 'No games on this console yet.',
  'Jogo': 'Game',
  'Notas': 'Notes',
  'Códigos, mapas, o que falta fazer…': 'Codes, maps, what is left to do…',
  'Guardar as notas': 'Save the notes',
  'Páginas guardadas': 'Pages kept',
  'Nenhuma página guardada com este jogo.': 'No pages kept with this game.',
  'Endereço de um guia ou de um mapa': 'Address of a guide or a map',
  'Guardar a página da TV': 'Keep the page on the TV',
  'Na TV': 'On the TV',
  'Tirar': 'Remove',
  'Notas guardadas': 'Notes saved',
  'Notas compridas demais': 'Notes too long',
  'Guardada no jogo': 'Kept with the game',
  'Tirada do jogo': 'Removed from the game',
  'Só endereços da web (http ou https)': 'Web addresses only (http or https)',
  'Páginas leves ao lado do jogo': 'Light pages beside the game',
  '«Abrir ao lado do jogo» mostra a página no browser do próprio sistema, mais lento do que este e num painel estreito. Ligado: os sites mandam a versão de telemóvel, mais leve e feita para essa largura':
    '"Open beside the game" shows the page in the system\'s own browser, slower than this one and in a narrow panel. On: sites send their phone version, lighter and made for that width',
  'Teclas de setas': 'Arrow keys',
  'Para jogos, apresentações, leitores de vídeo e páginas feitas para TV':
    'For games, slide shows, video players and pages made for TVs',
  'Notificações dos sites': 'Site notifications',
  'Nenhum site pediu ainda. Quando um site quiser mostrar notificações, o browser pergunta: Permitir ou Bloquear.':
    'No site has asked yet. When a site wants to show notifications, the browser asks: Allow or Block.',
  'Notificações permitidas': 'Notifications allowed',
  'Notificações bloqueadas': 'Notifications blocked',
  'Esquecer': 'Forget',
  'Transferências rápidas': 'Fast downloads',
  'à espera da rede, continua sozinha': 'waiting for the network, goes on by itself',
  'Interrompida': 'Interrupted',
  'Pausar': 'Pause',
  'Continuar de onde ficou': 'Go on from where it stopped',
  'Tirar da lista': 'Remove from the list',
  'Tirar da lista? Carrega outra vez para confirmar': 'Remove from the list? Press again to confirm',
  'Apagar o ficheiro? Carrega outra vez para confirmar': 'Delete the file? Press again to confirm',
  'Os ficheiros grandes (200 MB ou mais) vêm por várias ligações ao mesmo tempo, até 8. Se o servidor não deixar, ou se não ficar mais rápido, o browser volta sozinho a uma só':
    'Large files (200 MB or more) come over several connections at once, up to 8. If the server does not allow it, or it is not any faster, the browser goes back to one by itself',
});

// More languages than the two the pages were born with (2026-11-14: Spanish first; then French, German, Italian
// and Dutch, the ones XPS5 has). A language is a table (langTable(), called by lang-more.js, which build-rn.sh
// puts after this file): its dictionary by the Portuguese text, its patterns, its days and months. What a
// table has not comes in English. The four functions below take the place of lang.js's (the same names,
// declared again later in the same script): with 'pt' and 'en' they do exactly what those did.
const LANG_MORE = {};
const LANG_LAZY = {};  // code -> the function that gives its table (lang-more.js): asked the first time it is wanted

function langLazy(code, make) {
  LANG_LAZY[code] = make;
}

// Is there a table for this language? (Made from its function the first time.)
function langWake(code) {
  if (!Object.prototype.hasOwnProperty.call(LANG_MORE, code) && Object.prototype.hasOwnProperty.call(LANG_LAZY, code))
    langTable(code, LANG_LAZY[code]());
  return Object.prototype.hasOwnProperty.call(LANG_MORE, code);
}

function langTable(code, table) {
  LANG_MORE[code] = {
    dict: table.dict || {},
    patterns: (table.patterns || []).map(([pt, to]) => {
      const re = pt.split(/(%s|%d)/).map(p => p === '%s' ? '([\\s\\S]+?)' : p === '%d' ? '(\\d+)'
        : p.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('');
      return [new RegExp('^' + re + '$'), to];
    }),
    days: table.days,
    months: table.months,
    day: table.day,
  };
}

// t in one language's dictionary or patterns (a pattern's parts through part()), or null.
function langIn(dict, patterns, t, part) {
  let out = Object.prototype.hasOwnProperty.call(dict, t) ? dict[t] : null;
  for (let i = 0; out === null && i < patterns.length; i++) {
    const m = patterns[i][0].exec(t);
    if (m) {
      let n = 1;
      out = patterns[i][1].replace(/%[sd]/g, () => part(m[n++]));
    }
  }
  return out;
}

// The text in the page's language (its spaces at both ends kept), or null: in LANG's table, else in English.
function langEn(text) {
  const t = text.trim();
  if (!t) return null;
  const more = LANG_MORE[LANG];
  const english = p => Object.prototype.hasOwnProperty.call(LANG_EN, p) ? LANG_EN[p] : p;
  let out = null;
  if (more)
    out = langIn(more.dict, more.patterns, t, p => Object.prototype.hasOwnProperty.call(more.dict, p) ? more.dict[p] : english(p));
  if (out === null) out = langIn(LANG_EN, LANG_EN_PATTERNS, t, english);
  if (out === null) return null;
  return text.slice(0, text.length - text.trimStart().length) + out + text.slice(text.trimEnd().length);
}

function T(text, ...args) {
  const context = /^[a-z]+\|/.exec(text);
  const plain = context ? text.slice(context[0].length) : text;
  let s = LANG !== 'pt' ? (langEn(text) ?? (context ? langEn(plain) : null)) : null;
  if (s === null) s = plain;
  let n = 0;
  return args.length ? s.replace(/%[sd]/g, () => String(args[n++])) : s;
}

function langDay(d) {
  const more = LANG_MORE[LANG];
  if (more && more.days && more.months && more.day)
    return more.day.replace('%w', more.days[d.getDay()]).replace('%d', d.getDate()).replace('%m', more.months[d.getMonth()]);
  return LANG !== 'pt'
    ? LANG_DAYS.en[d.getDay()] + ', ' + d.getDate() + ' ' + LANG_MONTHS.en[d.getMonth()]
    : LANG_DAYS.pt[d.getDay()] + ', ' + d.getDate() + ' de ' + LANG_MONTHS.pt[d.getMonth()];
}

// 'pt', 'en' or a table's language. From one language that is not Portuguese to another: back to the
// Portuguese the page is written in first.
function setLang(lang) {
  lang = lang === 'en' || langWake(lang) ? lang : 'pt';
  if (lang === LANG) return;
  if (LANG !== 'pt') {
    if (langObserver) langObserver.disconnect();
    langObserver = null;
    LANG = 'pt';
    langRestore(document.documentElement);
  }
  LANG = lang;
  document.documentElement.lang = lang;
  if (lang !== 'pt') {
    langNode(document.documentElement);
    langObserver = new MutationObserver(records => {
      for (const r of records) {
        if (r.type === 'childList') r.addedNodes.forEach(langNode);
        else if (r.type === 'characterData') langNode(r.target);
        else if (LANG_ATTRS.includes(r.attributeName)) langNode(r.target);
      }
    });
    langObserver.observe(document.documentElement, {
      childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: LANG_ATTRS,
    });
  }
  // For the pages' texts put together before (settings.html, start.html).
  document.dispatchEvent(new Event('langchange'));
}
