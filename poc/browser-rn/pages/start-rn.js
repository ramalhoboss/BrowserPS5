// BrowserPS5 RN (2026-11-11): the start page as the PS5's home screen
// (start-rn.css). Put at the end of start.html's script by ../build-rn.sh.
// Under the row of tiles, the name and the address of the one under the
// cursor (or the D-pad's), and a light in its colour behind the page: at the
// start the first favorite, as the home screen shows its first game. Each
// tile also asks its site for its own icon (apple-touch-icon.png, then
// favicon.ico) and shows it over the letter when it comes.
(() => {
  const grid = document.getElementById('grid');
  const hub = document.createElement('div');
  hub.id = 'hub';
  const title = document.createElement('div');
  title.className = 't';
  const sub = document.createElement('div');
  sub.className = 'u';
  hub.append(title, sub);
  grid.after(hub);

  // The avatar's background is a gradient of two colours: the first one,
  // kept in the element once its icon came (a small icon gets a light tile).
  const colourOf = (avatar) => {
    if (!avatar.dataset.colour) {
      const colour = /(rgba?|hsla?)\([^)]*\)/.exec(avatar.style.background || '');
      avatar.dataset.colour = colour ? colour[0] : '#2a5fd0';
    }
    return avatar.dataset.colour;
  };

  let shown = null;
  const show = (tile) => {
    if (tile === shown)
      return;
    shown = tile;
    if (!tile) {
      hub.classList.remove('on');
      document.body.style.removeProperty('--rn-site');
      return;
    }
    const avatar = tile.querySelector('.avatar');
    title.textContent = tile.querySelector('.name').textContent;
    sub.textContent = '';
    const key = document.createElement('kbd');
    key.textContent = '✕';
    sub.append(key, tile.querySelector('.host').textContent);
    hub.classList.add('on');
    // The avatar's background is a gradient of two colours: the first one.
    document.body.style.setProperty('--rn-site', colourOf(avatar));
  };
  const pick = (e) => {
    const t = e.target && e.target.closest ? e.target.closest('.tile:not(.add)') : null;
    if (t)
      show(t);
  };
  grid.addEventListener('mouseover', pick);
  grid.addEventListener('focusin', pick);

  // The sites' own icons.
  const icons = () => {
    for (const tile of grid.querySelectorAll('.tile:not(.add)')) {
      const avatar = tile.querySelector('.avatar');
      if (!avatar || avatar.dataset.icon)
        continue;
      avatar.dataset.icon = '1';
      let origin;
      try { origin = new URL(tile.dataset.url || tile.href).origin; } catch (e) { continue; }
      const img = new Image();
      const tries = [origin + '/apple-touch-icon.png', origin + '/favicon.ico'];
      img.onload = () => {
        if (img.naturalWidth < 16)
          return img.onerror();
        colourOf(avatar);
        avatar.textContent = '';
        if (img.naturalWidth < 96) {  // a small favicon: in the middle of a light tile, not stretched
          img.className = 'small';
          avatar.style.background = 'linear-gradient(160deg, #ffffff, #dfe5f1)';
        }
        avatar.style.position = 'relative';
        avatar.appendChild(img);
      };
      img.onerror = () => {
        if (tries.length)
          img.src = tries.shift();
      };
      img.referrerPolicy = 'no-referrer';
      img.src = tries.shift();
    }
  };

  // The console's own services (2026-11-11; main.cpp start_open(),
  // data.services: what answers on the console itself, e.g. the scene's
  // websrv with its homebrew launcher on 127.0.0.1:8080): a tile each after
  // the favorites, before the "add" one. Not favorites: nothing to remove,
  // and gone while the favorites are edited (start-rn.css).
  // 2026-11-11 (webkit/security): the engine lets nothing connect to the
  // console itself but the user's own navigation, and a link of this page is
  // not one: what is on the console opens through the app (go_to()).
  const own = url => 'browserps5:go?' + encodeURIComponent(url);
  const local = () => {
    for (const tile of grid.querySelectorAll('a.tile:not(.add):not(.service)')) {
      if (tile.dataset.url || !/^http:\/\/(127\.0\.0\.1|localhost|\[::1\])(:\d+)?\//i.test(tile.href))
        continue;
      tile.dataset.url = tile.href;
      tile.href = own(tile.dataset.url);
    }
  };

  const services = () => {
    const list = Array.isArray(data.services) ? data.services : [];
    const add = grid.querySelector('.tile.add');
    for (const s of list) {
      if (!s || typeof s.url !== 'string' || !/^http:\/\/127\.0\.0\.1:\d+\//.test(s.url))
        continue;
      if (grid.querySelector('.tile.service[data-url="' + s.url + '"]'))
        continue;
      const a = el('a', 'tile service');
      a.href = own(s.url);
      a.dataset.url = s.url;
      const picture = avatar(String(s.name || 'PS5'));
      picture.dataset.icon = '1';  // its letter stays: no icon is asked for
      picture.style.background = 'linear-gradient(160deg, #0070d1, #00439c)';
      const name = el('div', 'name', String(s.name || ''));
      name.setAttribute('translate', 'no');
      a.append(picture, name, el('div', 'host', String(s.note || '')));
      grid.insertBefore(a, add);
    }
  };

  // 2026-11-11 22:45: YouTube's own page for televisions and consoles
  // (youtube.com/tv), which this browser is given under its own name
  // (main.cpp user_agent_for()) and which the controller drives
  // (dpad_keys_now(): the D-pad is the arrow keys there, cross is Enter). A
  // tile of the browser's, after the favorites: 2160p starts by itself.
  const tv = () => {
    if (grid.querySelector('.tile.tv'))
      return;
    const pt = /^pt/i.test(navigator.language || '');
    const a = el('a', 'tile service tv');
    a.href = 'https://www.youtube.com/tv';
    a.dataset.url = a.href;
    const picture = avatar('YouTube');
    picture.dataset.icon = '1';  // its letter stays: no icon is asked for
    picture.style.background = 'linear-gradient(160deg, #ff0033, #a80022)';
    const name = el('div', 'name', pt ? 'YouTube na TV' : 'YouTube TV');
    name.setAttribute('translate', 'no');
    a.append(picture, name, el('div', 'host', pt ? 'D-pad e \u2715, como numa televis\u00e3o' : 'D-pad and \u2715, as on a television'));
    grid.insertBefore(a, grid.querySelector('.tile.add'));
  };

  const refresh = () => {
    services();
    tv();
    local();
    if (!shown || !shown.isConnected) {
      shown = null;
      show(grid.querySelector('.tile:not(.add)'));
    }
    icons();
  };
  refresh();
  new MutationObserver(refresh).observe(grid, {childList: true});
})();

// 2026-11-14: "your game". A
// browser on a PC does not know what its user plays; this one is told by the app (main.cpp games_json(),
// data.games: the game open behind the browser first, then the ones played last, each with its name, its icon
// and the icon's colour, from the PS5's own data about them, read only). Under the favorites: the game, and one
// press to its guide, its trophies, its videos, its wiki, its maps, how long it takes, its news, who is live
// with it, what X and Reddit say. The other recent games are a press away.
(() => {
  // (2026-11-16) The PS5's own symbols in a name (the private use area, U+E000-U+F8FF: "★SHOP APPKG★")
  // have no font here and were boxes: left out of the name shown.
  const shownName = g => {
    if (g && typeof g.name === 'string')
      g.name = g.name.replace(/[-]/g, '').replace(/\s{2,}/g, ' ').trim();
    return g;
  };
  const games = (Array.isArray(data.games) ? data.games : []).map(shownName).filter(g => g && typeof g.name === 'string' && g.name);
  if (!games.length)
    return;
  const TEXTS = {
    pt: {title: 'O teu jogo', open: 'Aberto agora', played: 'Jogado', there: 'Na consola', guide: 'Guia', trophies: 'Troféus',
         videos: 'Vídeos', wiki: 'Wiki', maps: 'Mapas', time: 'Duração', news: 'Notícias', live: 'Em direto', others: 'Outros jogos',
         today: 'hoje', yesterday: 'ontem', days: 'há {n} dias', guideQ: 'guia passo a passo', mapQ: 'mapa interativo', cheats: 'Cheats', cheatsQ: 'cheats códigos truques',
         cFiles: 'Ficheiros para o etaHEN', cLoading: 'A procurar no repositório do etaHEN…', cNone: 'Ainda não há ficheiros de cheats para este jogo no repositório do etaHEN.', cFailed: 'Não consegui ler o repositório do etaHEN (sem rede?).', cSave: 'Guardar na consola', cThere: 'Na consola', cAsked: 'Pedido', cYours: 'a tua versão', cWeb: 'Códigos e truques na web', cClose: 'Fechar', cNoEta: 'Não encontrei a pasta do etaHEN nesta consola: o ficheiro vai para as Transferências.', cHow: 'Depois abre o menu de cheats do etaHEN com o jogo a correr. O ficheiro tem de ser o da versão do teu jogo.', cVersion: 'Versão do teu jogo',
         hPlay: 'Jogar', hTrophies: 'Troféus', hMissing: 'Por ganhar', hWon: 'Ganhos', hHidden: 'Troféu oculto', hShow: 'Mostrar', hHow: 'Como ganhar', hLoading: 'A ler os troféus…', hNone: 'Este jogo não tem troféus lidos nesta consola.', hHowQ: 'troféu como ganhar', hGrades: ['Bronze', 'Prata', 'Ouro', 'Platina'],
         hNotes: 'As tuas notas', hNotesPh: 'Códigos, coordenadas, o que falta fazer… Ficam guardadas com este jogo.', hNotesKept: 'Notas guardadas', hPins: 'Páginas guardadas', hUnpin: 'Tirar', hPinsNone: 'Num guia ou num mapa, abre o menu e escolhe «Guardar no jogo»: a página fica aqui.',
         patches: 'Patches', patchesQ: 'patch 60 fps', pFiles: 'Patches para o etaHEN', pLoading: 'A procurar na lista de patches…', pNone: 'Ainda não há patches para este jogo na lista da comunidade (PS-Game-Patch).', pFailed: 'Não consegui ler a lista de patches (sem rede?).', pOn: 'Ligado', pOff: 'Desligado', pAny: 'qualquer versão', pFor: 'para a versão {v}', pBy: 'por {a}', pWeb: 'Patches na web', pHow: 'Um patch ligado é aplicado pelo plugin de patches do etaHEN quando abres o jogo; cada patch serve para uma versão do jogo. Liga só o que queres.', pNoEta: 'Não encontrei a pasta do etaHEN nesta consola: os patches não podem ser ligados aqui.', pNoPlugin: 'Os interruptores ficam guardados nesta consola, mas quem aplica os patches no jogo é o plugin itemzflow_xml_patches do etaHEN, e ele não está aqui.', pPlugT: 'Falta o plugin de patches do etaHEN', pPlugSteps: ['O etaHEN não o publica já feito: compila-se a partir do código do etaHEN (etaHEN-Plugins, pasta Plugin_samples/itemzflow_xml_patches) com o SDK de plugins dele.', 'Copia o ficheiro .plugin para /data/etaHEN/plugins (pelo FTP do etaHEN) ou para etaHEN/plugins numa pen USB.', 'Arranca-o no toolbox do etaHEN, em Plugins, ou volta a carregar o etaHEN.', 'Liga aqui os patches que queres e abre o jogo: este painel deixa de mostrar este aviso.'], pPlugWeb: 'Ver o plugin no GitHub', pPlugHow: 'O que falta',
         hShots: 'As tuas capturas', hMore: 'Mostrar mais', hHdr: '{n} em HDR, que o browser ainda não mostra', hPrev: 'Anterior', hNext: 'Seguinte',
         hDiscord: 'Enviar para o Discord', hHook: 'Webhook do Discord', hHookHelp: 'Cola o endereço do webhook do canal: no Discord, Definições do servidor → Integrações → Webhooks → Copiar URL do webhook. Fica guardado neste browser e serve para os próximos envios.', hHookSave: 'Guardar e enviar', hHookForget: 'Esquecer o webhook', hHookCancel: 'Cancelar',
         lib: 'Os teus jogos', libLoading: 'A ler os teus jogos…', libNone: 'Não encontrei jogos nesta consola.', libNoMatch: 'Nenhum jogo com esse nome.', libRecent: 'Recentes', libAz: 'A–Z', libEvery: 'Todos', libFind: 'Procurar um jogo', libTrophies: 'Troféus', libSum: '{w} troféus ganhos · {p} platinas à vista'},
    en: {title: 'Your game', open: 'Open now', played: 'Played', there: 'On the console', guide: 'Guide', trophies: 'Trophies',
         videos: 'Videos', wiki: 'Wiki', maps: 'Maps', time: 'Length', news: 'News', live: 'Live', others: 'Other games',
         today: 'today', yesterday: 'yesterday', days: '{n} days ago', guideQ: 'walkthrough guide', mapQ: 'interactive map', cheats: 'Cheats', cheatsQ: 'cheats codes',
         cFiles: 'Files for etaHEN', cLoading: 'Looking in etaHEN\'s repository…', cNone: 'There are no cheat files for this game in etaHEN\'s repository yet.', cFailed: 'etaHEN\'s repository could not be read (no network?).', cSave: 'Save to the console', cThere: 'On the console', cAsked: 'Asked for', cYours: 'your version', cWeb: 'Codes and tricks on the web', cClose: 'Close', cNoEta: 'etaHEN\'s folder was not found on this console: the file goes to the downloads.', cHow: 'Then open etaHEN\'s cheats menu with the game running. The file has to be the one of your game\'s version.', cVersion: 'Your game\'s version',
         hPlay: 'Play', hTrophies: 'Trophies', hMissing: 'Still to win', hWon: 'Won', hHidden: 'Hidden trophy', hShow: 'Show', hHow: 'How to win it', hLoading: 'Reading the trophies…', hNone: 'No trophies were read for this game on this console.', hHowQ: 'trophy guide how to get', hGrades: ['Bronze', 'Silver', 'Gold', 'Platinum'],
         hNotes: 'Your notes', hNotesPh: 'Codes, coordinates, what is left to do… They are kept with this game.', hNotesKept: 'Notes kept', hPins: 'Pages kept', hUnpin: 'Remove', hPinsNone: 'On a guide or a map, open the menu and choose “Keep with the game”: the page stays here.',
         patches: 'Patches', patchesQ: '60 fps patch', pFiles: 'Patches for etaHEN', pLoading: 'Looking in the patch list…', pNone: 'There are no patches for this game in the community\'s list (PS-Game-Patch) yet.', pFailed: 'The patch list could not be read (no network?).', pOn: 'On', pOff: 'Off', pAny: 'any version', pFor: 'for version {v}', pBy: 'by {a}', pWeb: 'Patches on the web', pHow: 'A patch that is on is applied by etaHEN\'s game patches plugin when you start the game; each patch is for one version of the game. Turn on only what you want.', pNoEta: 'etaHEN\'s folder was not found on this console: patches cannot be turned on here.', pNoPlugin: 'The switches are kept on this console, but what applies the patches in game is etaHEN’s itemzflow_xml_patches plugin, and it is not here.', pPlugT: 'etaHEN’s patches plugin is missing', pPlugSteps: ['etaHEN does not publish it ready-made: it is built from etaHEN’s code (etaHEN-Plugins, folder Plugin_samples/itemzflow_xml_patches) with its plugin SDK.', 'Copy the .plugin file to /data/etaHEN/plugins (through etaHEN’s FTP) or to etaHEN/plugins on a USB drive.', 'Start it from etaHEN’s toolbox, under Plugins, or load etaHEN again.', 'Turn on the patches you want here and open the game: this panel stops showing this notice.'], pPlugWeb: 'See the plugin on GitHub', pPlugHow: 'What is missing',
         hShots: 'Your captures', hMore: 'Show more', hHdr: '{n} in HDR, which the browser does not show yet', hPrev: 'Previous', hNext: 'Next',
         hDiscord: 'Send to Discord', hHook: 'Discord webhook', hHookHelp: 'Paste the channel’s webhook address: in Discord, Server Settings → Integrations → Webhooks → Copy Webhook URL. It stays in this browser for the next sends.', hHookSave: 'Save and send', hHookForget: 'Forget the webhook', hHookCancel: 'Cancel',
         lib: 'Your games', libLoading: 'Reading your games…', libNone: 'No games were found on this console.', libNoMatch: 'No game with that name.', libRecent: 'Recent', libAz: 'A–Z', libEvery: 'All', libFind: 'Find a game', libTrophies: 'Trophies', libSum: '{w} trophies won · {p} platinums in sight'},
    es: {title: 'Tu juego', open: 'Abierto ahora', played: 'Jugado', there: 'En la consola', guide: 'Guía', trophies: 'Trofeos',
         videos: 'Vídeos', wiki: 'Wiki', maps: 'Mapas', time: 'Duración', news: 'Noticias', live: 'En directo', others: 'Otros juegos',
         today: 'hoy', yesterday: 'ayer', days: 'hace {n} días', guideQ: 'guía paso a paso', mapQ: 'mapa interactivo', cheats: 'Trucos', cheatsQ: 'trucos códigos',
         cFiles: 'Archivos para etaHEN', cLoading: 'Buscando en el repositorio de etaHEN…', cNone: 'Aún no hay archivos de trucos para este juego en el repositorio de etaHEN.', cFailed: 'No se pudo leer el repositorio de etaHEN (¿sin red?).', cSave: 'Guardar en la consola', cThere: 'En la consola', cAsked: 'Pedido', cYours: 'tu versión', cWeb: 'Códigos y trucos en la web', cClose: 'Cerrar', cNoEta: 'No se encontró la carpeta de etaHEN en esta consola: el archivo va a las descargas.', cHow: 'Después abre el menú de trucos de etaHEN con el juego en marcha. El archivo debe ser el de la versión de tu juego.', cVersion: 'Versión de tu juego',
         hPlay: 'Jugar', hTrophies: 'Trofeos', hMissing: 'Por ganar', hWon: 'Ganados', hHidden: 'Trofeo oculto', hShow: 'Mostrar', hHow: 'Cómo ganarlo', hLoading: 'Leyendo los trofeos…', hNone: 'No se leyeron trofeos de este juego en esta consola.', hHowQ: 'trofeo cómo conseguir', hGrades: ['Bronce', 'Plata', 'Oro', 'Platino'],
         hNotes: 'Tus notas', hNotesPh: 'Códigos, coordenadas, lo que falta por hacer… Se guardan con este juego.', hNotesKept: 'Notas guardadas', hPins: 'Páginas guardadas', hUnpin: 'Quitar', hPinsNone: 'En una guía o un mapa, abre el menú y elige «Guardar en el juego»: la página queda aquí.',
         patches: 'Parches', patchesQ: 'parche 60 fps', pFiles: 'Parches para etaHEN', pLoading: 'Buscando en la lista de parches…', pNone: 'Aún no hay parches para este juego en la lista de la comunidad (PS-Game-Patch).', pFailed: 'No se pudo leer la lista de parches (¿sin red?).', pOn: 'Activado', pOff: 'Desactivado', pAny: 'cualquier versión', pFor: 'para la versión {v}', pBy: 'por {a}', pWeb: 'Parches en la web', pHow: 'Un parche activado lo aplica el plugin de parches de etaHEN al abrir el juego; cada parche sirve para una versión del juego. Activa solo lo que quieras.', pNoEta: 'No se encontró la carpeta de etaHEN en esta consola: aquí no se pueden activar parches.', pNoPlugin: 'Los interruptores quedan guardados en esta consola, pero quien aplica los parches en el juego es el plugin itemzflow_xml_patches de etaHEN, y no está aquí.', pPlugT: 'Falta el plugin de parches de etaHEN', pPlugSteps: ['etaHEN no lo publica ya hecho: se compila a partir del código de etaHEN (etaHEN-Plugins, carpeta Plugin_samples/itemzflow_xml_patches) con su SDK de plugins.', 'Copia el archivo .plugin a /data/etaHEN/plugins (por el FTP de etaHEN) o a etaHEN/plugins en un USB.', 'Arráncalo desde el toolbox de etaHEN, en Plugins, o vuelve a cargar etaHEN.', 'Activa aquí los parches que quieras y abre el juego: este panel deja de mostrar este aviso.'], pPlugWeb: 'Ver el plugin en GitHub', pPlugHow: 'Qué falta',
         hShots: 'Tus capturas', hMore: 'Mostrar más', hHdr: '{n} en HDR, que el navegador aún no muestra', hPrev: 'Anterior', hNext: 'Siguiente',
         hDiscord: 'Enviar a Discord', hHook: 'Webhook de Discord', hHookHelp: 'Pega la dirección del webhook del canal: en Discord, Ajustes del servidor → Integraciones → Webhooks → Copiar URL del webhook. Se guarda en este navegador para los próximos envíos.', hHookSave: 'Guardar y enviar', hHookForget: 'Olvidar el webhook', hHookCancel: 'Cancelar',
         lib: 'Tus juegos', libLoading: 'Leyendo tus juegos…', libNone: 'No se encontraron juegos en esta consola.', libNoMatch: 'Ningún juego con ese nombre.', libRecent: 'Recientes', libAz: 'A–Z', libEvery: 'Todos', libFind: 'Buscar un juego', libTrophies: 'Trofeos', libSum: '{w} trofeos ganados · {p} platinos a la vista'},
    fr: {title: 'Ton jeu', open: 'Ouvert maintenant', played: 'Joué', there: 'Sur la console', guide: 'Guide', trophies: 'Trophées',
         videos: 'Vidéos', wiki: 'Wiki', maps: 'Cartes', time: 'Durée', news: 'Actualités', live: 'En direct', others: 'Autres jeux',
         today: "aujourd'hui", yesterday: 'hier', days: 'il y a {n} jours', guideQ: 'guide soluce', mapQ: 'carte interactive', cheats: 'Astuces', cheatsQ: 'codes astuces triche',
         cFiles: 'Fichiers pour etaHEN', cLoading: 'Recherche dans le dépôt d\'etaHEN…', cNone: 'Il n\'y a pas encore de fichiers de triche pour ce jeu dans le dépôt d\'etaHEN.', cFailed: 'Le dépôt d\'etaHEN n\'a pas pu être lu (pas de réseau ?).', cSave: 'Enregistrer sur la console', cThere: 'Sur la console', cAsked: 'Demandé', cYours: 'ta version', cWeb: 'Codes et astuces sur le web', cClose: 'Fermer', cNoEta: 'Le dossier d\'etaHEN n\'a pas été trouvé sur cette console : le fichier va dans les téléchargements.', cHow: 'Ouvre ensuite le menu de triche d\'etaHEN avec le jeu lancé. Le fichier doit être celui de la version de ton jeu.', cVersion: 'Version de ton jeu',
         hPlay: 'Jouer', hTrophies: 'Trophées', hMissing: 'À gagner', hWon: 'Gagnés', hHidden: 'Trophée caché', hShow: 'Afficher', hHow: 'Comment le gagner', hLoading: 'Lecture des trophées…', hNone: 'Aucun trophée n\'a été lu pour ce jeu sur cette console.', hHowQ: 'trophée comment obtenir', hGrades: ['Bronze', 'Argent', 'Or', 'Platine'],
         hNotes: 'Tes notes', hNotesPh: 'Codes, coordonnées, ce qui reste à faire… Elles sont gardées avec ce jeu.', hNotesKept: 'Notes enregistrées', hPins: 'Pages gardées', hUnpin: 'Retirer', hPinsNone: 'Sur un guide ou une carte, ouvre le menu et choisis « Garder avec le jeu » : la page reste ici.',
         patches: 'Patchs', patchesQ: 'patch 60 fps', pFiles: 'Patchs pour etaHEN', pLoading: 'Recherche dans la liste des patchs…', pNone: 'Il n\'y a pas encore de patchs pour ce jeu dans la liste de la communauté (PS-Game-Patch).', pFailed: 'La liste des patchs n\'a pas pu être lue (pas de réseau ?).', pOn: 'Activé', pOff: 'Désactivé', pAny: 'toute version', pFor: 'pour la version {v}', pBy: 'par {a}', pWeb: 'Patchs sur le web', pHow: 'Un patch activé est appliqué par le plugin de patchs d\'etaHEN au lancement du jeu ; chaque patch vaut pour une version du jeu. N\'active que ce que tu veux.', pNoEta: 'Le dossier d\'etaHEN n\'a pas été trouvé sur cette console : les patchs ne peuvent pas être activés ici.', pNoPlugin: 'Les interrupteurs sont gardés sur cette console, mais c’est le plugin itemzflow_xml_patches d’etaHEN qui applique les patchs en jeu, et il n’est pas ici.', pPlugT: 'Il manque le plugin de patchs d’etaHEN', pPlugSteps: ['etaHEN ne le publie pas tout fait : il se compile à partir du code d’etaHEN (etaHEN-Plugins, dossier Plugin_samples/itemzflow_xml_patches) avec son SDK de plugins.', 'Copie le fichier .plugin dans /data/etaHEN/plugins (par le FTP d’etaHEN) ou dans etaHEN/plugins sur une clé USB.', 'Lance-le depuis la toolbox d’etaHEN, dans Plugins, ou recharge etaHEN.', 'Active ici les patchs voulus et ouvre le jeu : ce panneau n’affichera plus cet avis.'], pPlugWeb: 'Voir le plugin sur GitHub', pPlugHow: 'Ce qui manque',
         hShots: 'Tes captures', hMore: 'Afficher plus', hHdr: '{n} en HDR, que le navigateur n\'affiche pas encore', hPrev: 'Précédent', hNext: 'Suivant',
         hDiscord: 'Envoyer sur Discord', hHook: 'Webhook Discord', hHookHelp: 'Colle l’adresse du webhook du salon : dans Discord, Paramètres du serveur → Intégrations → Webhooks → Copier l’URL du webhook. Elle reste dans ce navigateur pour les prochains envois.', hHookSave: 'Enregistrer et envoyer', hHookForget: 'Oublier le webhook', hHookCancel: 'Annuler',
         lib: 'Tes jeux', libLoading: 'Lecture de tes jeux…', libNone: 'Aucun jeu trouvé sur cette console.', libNoMatch: 'Aucun jeu avec ce nom.', libRecent: 'Récents', libAz: 'A–Z', libEvery: 'Tous', libFind: 'Chercher un jeu', libTrophies: 'Trophées', libSum: '{w} trophées gagnés · {p} platines en vue'},
    de: {title: 'Dein Spiel', open: 'Jetzt geöffnet', played: 'Gespielt', there: 'Auf der Konsole', guide: 'Guide', trophies: 'Trophäen',
         videos: 'Videos', wiki: 'Wiki', maps: 'Karten', time: 'Spieldauer', news: 'News', live: 'Live', others: 'Andere Spiele',
         today: 'heute', yesterday: 'gestern', days: 'vor {n} Tagen', guideQ: 'Komplettlösung', mapQ: 'interaktive Karte', cheats: 'Cheats', cheatsQ: 'Cheats Codes',
         cFiles: 'Dateien für etaHEN', cLoading: 'Suche im etaHEN-Repository…', cNone: 'Für dieses Spiel gibt es im etaHEN-Repository noch keine Cheat-Dateien.', cFailed: 'Das etaHEN-Repository konnte nicht gelesen werden (kein Netz?).', cSave: 'Auf der Konsole speichern', cThere: 'Auf der Konsole', cAsked: 'Angefordert', cYours: 'deine Version', cWeb: 'Codes und Tricks im Web', cClose: 'Schließen', cNoEta: 'Der etaHEN-Ordner wurde auf dieser Konsole nicht gefunden: Die Datei kommt zu den Downloads.', cHow: 'Öffne danach das Cheat-Menü von etaHEN bei laufendem Spiel. Die Datei muss zur Version deines Spiels passen.', cVersion: 'Version deines Spiels',
         hPlay: 'Spielen', hTrophies: 'Trophäen', hMissing: 'Noch offen', hWon: 'Erhalten', hHidden: 'Versteckte Trophäe', hShow: 'Anzeigen', hHow: 'So bekommst du sie', hLoading: 'Trophäen werden gelesen…', hNone: 'Für dieses Spiel wurden auf dieser Konsole keine Trophäen gelesen.', hHowQ: 'Trophäe Guide freischalten', hGrades: ['Bronze', 'Silber', 'Gold', 'Platin'],
         hNotes: 'Deine Notizen', hNotesPh: 'Codes, Koordinaten, was noch zu tun ist… Sie bleiben bei diesem Spiel.', hNotesKept: 'Notizen gespeichert', hPins: 'Gespeicherte Seiten', hUnpin: 'Entfernen', hPinsNone: 'Öffne auf einem Guide oder einer Karte das Menü und wähle „Beim Spiel speichern“: Die Seite bleibt hier.',
         patches: 'Patches', patchesQ: '60 fps Patch', pFiles: 'Patches für etaHEN', pLoading: 'Suche in der Patch-Liste…', pNone: 'Für dieses Spiel gibt es in der Community-Liste (PS-Game-Patch) noch keine Patches.', pFailed: 'Die Patch-Liste konnte nicht gelesen werden (kein Netz?).', pOn: 'An', pOff: 'Aus', pAny: 'jede Version', pFor: 'für Version {v}', pBy: 'von {a}', pWeb: 'Patches im Web', pHow: 'Ein eingeschalteter Patch wird vom Patch-Plugin von etaHEN beim Start des Spiels angewendet; jeder Patch gilt für eine Version des Spiels. Schalte nur ein, was du willst.', pNoEta: 'Der etaHEN-Ordner wurde auf dieser Konsole nicht gefunden: Patches können hier nicht eingeschaltet werden.', pNoPlugin: 'Die Schalter bleiben auf dieser Konsole gespeichert, aber die Patches wendet im Spiel das etaHEN-Plugin itemzflow_xml_patches an, und das ist hier nicht vorhanden.', pPlugT: 'Das Patch-Plugin von etaHEN fehlt', pPlugSteps: ['etaHEN veröffentlicht es nicht fertig: Es wird aus dem Code von etaHEN (etaHEN-Plugins, Ordner Plugin_samples/itemzflow_xml_patches) mit dessen Plugin-SDK gebaut.', 'Kopiere die .plugin-Datei nach /data/etaHEN/plugins (über das FTP von etaHEN) oder nach etaHEN/plugins auf einem USB-Stick.', 'Starte es in der Toolbox von etaHEN unter Plugins oder lade etaHEN neu.', 'Schalte hier die gewünschten Patches ein und öffne das Spiel: Dieses Panel zeigt den Hinweis dann nicht mehr.'], pPlugWeb: 'Plugin auf GitHub ansehen', pPlugHow: 'Was fehlt',
         hShots: 'Deine Aufnahmen', hMore: 'Mehr anzeigen', hHdr: '{n} in HDR, die der Browser noch nicht anzeigt', hPrev: 'Zurück', hNext: 'Weiter',
         hDiscord: 'An Discord senden', hHook: 'Discord-Webhook', hHookHelp: 'Füge die Webhook-Adresse des Kanals ein: in Discord Servereinstellungen → Integrationen → Webhooks → Webhook-URL kopieren. Sie bleibt in diesem Browser für die nächsten Sendungen.', hHookSave: 'Speichern und senden', hHookForget: 'Webhook vergessen', hHookCancel: 'Abbrechen',
         lib: 'Deine Spiele', libLoading: 'Deine Spiele werden gelesen…', libNone: 'Auf dieser Konsole wurden keine Spiele gefunden.', libNoMatch: 'Kein Spiel mit diesem Namen.', libRecent: 'Zuletzt', libAz: 'A–Z', libEvery: 'Alle', libFind: 'Spiel suchen', libTrophies: 'Trophäen', libSum: '{w} Trophäen gewonnen · {p} Platin in Sicht'},
    it: {title: 'Il tuo gioco', open: 'Aperto ora', played: 'Giocato', there: 'Sulla console', guide: 'Guida', trophies: 'Trofei',
         videos: 'Video', wiki: 'Wiki', maps: 'Mappe', time: 'Durata', news: 'Notizie', live: 'In diretta', others: 'Altri giochi',
         today: 'oggi', yesterday: 'ieri', days: '{n} giorni fa', guideQ: 'guida completa', mapQ: 'mappa interattiva', cheats: 'Trucchi', cheatsQ: 'trucchi codici',
         cFiles: 'File per etaHEN', cLoading: 'Ricerca nel repository di etaHEN…', cNone: 'Non ci sono ancora file di trucchi per questo gioco nel repository di etaHEN.', cFailed: 'Impossibile leggere il repository di etaHEN (senza rete?).', cSave: 'Salva sulla console', cThere: 'Sulla console', cAsked: 'Richiesto', cYours: 'la tua versione', cWeb: 'Codici e trucchi sul web', cClose: 'Chiudi', cNoEta: 'La cartella di etaHEN non è stata trovata su questa console: il file va nei download.', cHow: 'Poi apri il menu dei trucchi di etaHEN con il gioco in esecuzione. Il file deve essere quello della versione del tuo gioco.', cVersion: 'Versione del tuo gioco',
         hPlay: 'Gioca', hTrophies: 'Trofei', hMissing: 'Da vincere', hWon: 'Vinti', hHidden: 'Trofeo nascosto', hShow: 'Mostra', hHow: 'Come ottenerlo', hLoading: 'Lettura dei trofei…', hNone: 'Nessun trofeo letto per questo gioco su questa console.', hHowQ: 'trofeo come ottenere', hGrades: ['Bronzo', 'Argento', 'Oro', 'Platino'],
         hNotes: 'Le tue note', hNotesPh: 'Codici, coordinate, cosa resta da fare… Restano con questo gioco.', hNotesKept: 'Note salvate', hPins: 'Pagine salvate', hUnpin: 'Togli', hPinsNone: 'Su una guida o una mappa, apri il menu e scegli «Salva nel gioco»: la pagina resta qui.',
         patches: 'Patch', patchesQ: 'patch 60 fps', pFiles: 'Patch per etaHEN', pLoading: 'Ricerca nell\'elenco delle patch…', pNone: 'Non ci sono ancora patch per questo gioco nell\'elenco della comunità (PS-Game-Patch).', pFailed: 'Impossibile leggere l\'elenco delle patch (senza rete?).', pOn: 'Attiva', pOff: 'Disattivata', pAny: 'qualsiasi versione', pFor: 'per la versione {v}', pBy: 'di {a}', pWeb: 'Patch sul web', pHow: 'Una patch attiva viene applicata dal plugin delle patch di etaHEN all\'avvio del gioco; ogni patch vale per una versione del gioco. Attiva solo ciò che vuoi.', pNoEta: 'La cartella di etaHEN non è stata trovata su questa console: qui le patch non si possono attivare.', pNoPlugin: 'Gli interruttori restano salvati su questa console, ma ad applicare le patch nel gioco è il plugin itemzflow_xml_patches di etaHEN, che qui non c’è.', pPlugT: 'Manca il plugin delle patch di etaHEN', pPlugSteps: ['etaHEN non lo pubblica già pronto: si compila dal codice di etaHEN (etaHEN-Plugins, cartella Plugin_samples/itemzflow_xml_patches) con il suo SDK per plugin.', 'Copia il file .plugin in /data/etaHEN/plugins (tramite l’FTP di etaHEN) o in etaHEN/plugins su una chiavetta USB.', 'Avvialo dal toolbox di etaHEN, in Plugins, oppure ricarica etaHEN.', 'Attiva qui le patch che vuoi e apri il gioco: questo pannello non mostrerà più questo avviso.'], pPlugWeb: 'Vedi il plugin su GitHub', pPlugHow: 'Cosa manca',
         hShots: 'Le tue catture', hMore: 'Mostra altre', hHdr: '{n} in HDR, che il browser non mostra ancora', hPrev: 'Precedente', hNext: 'Successiva',
         hDiscord: 'Invia su Discord', hHook: 'Webhook di Discord', hHookHelp: 'Incolla l’indirizzo del webhook del canale: in Discord, Impostazioni server → Integrazioni → Webhook → Copia URL webhook. Resta in questo browser per i prossimi invii.', hHookSave: 'Salva e invia', hHookForget: 'Dimentica il webhook', hHookCancel: 'Annulla',
         lib: 'I tuoi giochi', libLoading: 'Lettura dei tuoi giochi…', libNone: 'Nessun gioco trovato su questa console.', libNoMatch: 'Nessun gioco con quel nome.', libRecent: 'Recenti', libAz: 'A–Z', libEvery: 'Tutti', libFind: 'Cerca un gioco', libTrophies: 'Trofei', libSum: '{w} trofei vinti · {p} platini in vista'},
    nl: {title: 'Jouw game', open: 'Nu geopend', played: 'Gespeeld', there: 'Op de console', guide: 'Gids', trophies: 'Trofeeën',
         videos: "Video's", wiki: 'Wiki', maps: 'Kaarten', time: 'Speelduur', news: 'Nieuws', live: 'Live', others: 'Andere games',
         today: 'vandaag', yesterday: 'gisteren', days: '{n} dagen geleden', guideQ: 'walkthrough gids', mapQ: 'interactieve kaart', cheats: 'Cheats', cheatsQ: 'cheats codes',
         cFiles: 'Bestanden voor etaHEN', cLoading: 'Zoeken in de etaHEN-repository…', cNone: 'Er zijn nog geen cheatbestanden voor deze game in de etaHEN-repository.', cFailed: 'De etaHEN-repository kon niet worden gelezen (geen netwerk?).', cSave: 'Opslaan op de console', cThere: 'Op de console', cAsked: 'Aangevraagd', cYours: 'jouw versie', cWeb: 'Codes en trucs op het web', cClose: 'Sluiten', cNoEta: 'De map van etaHEN is niet gevonden op deze console: het bestand gaat naar de downloads.', cHow: 'Open daarna het cheatmenu van etaHEN terwijl de game draait. Het bestand moet bij de versie van je game horen.', cVersion: 'Versie van je game',
         hPlay: 'Spelen', hTrophies: 'Trofeeën', hMissing: 'Nog te winnen', hWon: 'Gewonnen', hHidden: 'Verborgen trofee', hShow: 'Tonen', hHow: 'Zo win je hem', hLoading: 'Trofeeën lezen…', hNone: 'Er zijn voor deze game geen trofeeën gelezen op deze console.', hHowQ: 'trofee gids behalen', hGrades: ['Brons', 'Zilver', 'Goud', 'Platina'],
         hNotes: 'Jouw notities', hNotesPh: 'Codes, coördinaten, wat er nog te doen is… Ze blijven bij deze game.', hNotesKept: 'Notities bewaard', hPins: 'Bewaarde pagina\'s', hUnpin: 'Weghalen', hPinsNone: 'Open op een gids of een kaart het menu en kies „Bij de game bewaren”: de pagina blijft hier.',
         patches: 'Patches', patchesQ: '60 fps patch', pFiles: 'Patches voor etaHEN', pLoading: 'Zoeken in de patchlijst…', pNone: 'Er zijn nog geen patches voor deze game in de lijst van de community (PS-Game-Patch).', pFailed: 'De patchlijst kon niet worden gelezen (geen netwerk?).', pOn: 'Aan', pOff: 'Uit', pAny: 'elke versie', pFor: 'voor versie {v}', pBy: 'door {a}', pWeb: 'Patches op het web', pHow: 'Een patch die aan staat wordt door de patchplugin van etaHEN toegepast wanneer je de game start; elke patch is voor één versie van de game. Zet alleen aan wat je wilt.', pNoEta: 'De map van etaHEN is niet gevonden op deze console: patches kunnen hier niet worden aangezet.', pNoPlugin: 'De schakelaars blijven op deze console bewaard, maar de patches worden in de game toegepast door de etaHEN-plugin itemzflow_xml_patches, en die staat hier niet.', pPlugT: 'De patch-plugin van etaHEN ontbreekt', pPlugSteps: ['etaHEN publiceert hem niet kant-en-klaar: hij wordt gebouwd uit de code van etaHEN (etaHEN-Plugins, map Plugin_samples/itemzflow_xml_patches) met de plugin-SDK.', 'Kopieer het .plugin-bestand naar /data/etaHEN/plugins (via de FTP van etaHEN) of naar etaHEN/plugins op een USB-stick.', 'Start hem in de toolbox van etaHEN, onder Plugins, of laad etaHEN opnieuw.', 'Zet hier de patches aan die je wilt en open de game: dit paneel toont deze melding dan niet meer.'], pPlugWeb: 'Bekijk de plugin op GitHub', pPlugHow: 'Wat ontbreekt',
         hShots: 'Jouw opnamen', hMore: 'Meer tonen', hHdr: '{n} in HDR, die de browser nog niet toont', hPrev: 'Vorige', hNext: 'Volgende',
         hDiscord: 'Naar Discord sturen', hHook: 'Discord-webhook', hHookHelp: 'Plak het webhookadres van het kanaal: in Discord Serverinstellingen → Integraties → Webhooks → Webhook-URL kopiëren. Het blijft in deze browser voor de volgende keren.', hHookSave: 'Opslaan en sturen', hHookForget: 'Webhook vergeten', hHookCancel: 'Annuleren',
         lib: 'Jouw games', libLoading: 'Je games worden gelezen…', libNone: 'Geen games gevonden op deze console.', libNoMatch: 'Geen game met die naam.', libRecent: 'Recent', libAz: 'A–Z', libEvery: 'Alle', libFind: 'Zoek een game', libTrophies: 'Trofeeën', libSum: '{w} trofeeën gewonnen · {p} platina in zicht'},
  };
  const t = TEXTS[String(data.lang || 'pt')] || TEXTS.en;
  const q = encodeURIComponent;
  const linksOf = g => [
    [t.guide, 'https://www.google.com/search?q=' + q(g.name + ' ' + t.guideQ)],
    // (2026-11-14) the game's cheats: codes and tricks
    [t.cheats, 'https://www.google.com/search?q=' + q(g.name + ' ' + (g.ps4 ? 'PS4' : 'PS5') + ' ' + t.cheatsQ)],
    // (2026-11-15) the game's patches (60 fps and the like): its own panel, then the web's (patchesPanel())
    [t.patches, 'https://www.google.com/search?q=' + q(g.name + ' ' + (g.ps4 ? 'PS4' : 'PS5') + ' ' + t.patchesQ)],
    [t.trophies, 'https://psnprofiles.com/search/guides?q=' + q(g.name)],
    [t.videos, 'https://www.youtube.com/results?search_query=' + q(g.name + ' gameplay')],
    [t.wiki, 'https://www.fandom.com/?s=' + q(g.name)],
    [t.maps, 'https://www.google.com/search?q=' + q(g.name + ' ' + t.mapQ)],
    [t.time, 'https://howlongtobeat.com/?q=' + q(g.name)],
    [t.news, 'https://news.google.com/search?q=' + q(g.name)],
    [t.live, 'https://www.twitch.tv/search?term=' + q(g.name)],
    ['X', 'https://x.com/search?q=' + q(g.tag || g.name)],
    ['Reddit', 'https://www.reddit.com/search/?q=' + q(g.name)],
  ];
  // The icon's colour, lifted when it is too dark to light a dark page.
  const rgbOf = g => {
    let c = Number(g.colour) || 0;
    if (!c)
      return '42, 95, 208';
    let r = c >> 16 & 255, gr = c >> 8 & 255, b = c & 255;
    const top = Math.max(r, gr, b, 1);
    if (top < 170) {
      const k = 170 / top;
      r = Math.round(r * k); gr = Math.round(gr * k); b = Math.round(b * k);
    }
    return r + ', ' + gr + ', ' + b;
  };
  const when = g => {
    if (g.open)
      return t.open;
    const played = Number(g.played) || 0, now = Number(data.now) || 0;
    if (!played || !now || played > now + 86400)
      return t.there;
    const days = Math.floor((now - played) / 86400);
    return t.played + ' ' + (days <= 0 ? t.today : days === 1 ? t.yesterday : t.days.replace('{n}', String(days)));
  };
  const picture = (g, cls) => {
    const box = el('span', cls, g.name.trim().charAt(0).toUpperCase());
    if (g.icon) {
      const img = new Image();
      img.alt = '';
      img.onload = () => { box.textContent = ''; box.append(img); };
      img.src = g.icon;
    }
    return box;
  };

  const box = el('section');
  box.id = 'games-box';
  const head = el('h2', '', t.title);
  const card = el('div', 'g-card');
  const others = el('div', 'g-others');
  box.append(head, card);

  // 2026-11-15. The game's cheats:
  // etaHEN's cheats menu reads cheat files by the game's id and version, and the scene keeps them in one public
  // repository (github.com/etaHEN/PS5_Cheats) with an index of each kind: "<id>_<version>.<kind>=<name>" a line.
  // The indexes are read here, the files of THIS game listed, and "Save" asks the app for one by its name
  // (browserps5:cheat?<name>: main.cpp cheat_ask() downloads it itself and puts it in etaHEN's folder).
  const CHEATS_REPO = 'https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/';
  let cheatIndex = null;
  const loadCheats = () => cheatIndex || (cheatIndex = Promise.all(['json', 'shn', 'mc4'].map(kind =>
    fetch(CHEATS_REPO + kind + '.txt').then(r => (r.ok ? r.text() : Promise.reject(new Error('HTTP ' + r.status)))))).then(texts => {
      const out = [];
      for (const text of texts) {
        for (const line of text.split('\n')) {
          const m = /^([A-Z]{4}[0-9]{5})_([0-9A-Za-z.]+)[.](json|shn|mc4)=(.*)$/.exec(line.trim());
          if (m)
            out.push({file: m[1] + '_' + m[2] + '.' + m[3], id: m[1], version: m[2], kind: m[3], name: m[4]});
        }
      }
      return out;
    }).catch(e => { cheatIndex = null; throw e; }));
  // "01.026.000" and "1.026.000" are the same version: each part as a number.
  const sameVersion = (a, b) => {
    const parts = v => String(v || '').split('.').map(x => parseInt(x, 10) || 0);
    const x = parts(a), y = parts(b);
    while (x.length && !x[x.length - 1]) x.pop();
    while (y.length && !y[y.length - 1]) y.pop();
    return x.length > 0 && x.length === y.length && x.every((n, i) => n === y[i]);
  };
  const cheatsPanel = (g, webUrl) => {
    const old = document.getElementById('cheats-panel');
    if (old)
      old.remove();
    const panel = el('div');
    panel.id = 'cheats-panel';
    panel.style.setProperty('--g-rgb', rgbOf(g));
    const card = el('div', 'c-card');
    const close = el('a', 'g-chip c-close', t.cClose);
    close.href = '#';
    close.addEventListener('click', ev => { ev.preventDefault(); panel.remove(); });
    panel.addEventListener('click', ev => { if (ev.target === panel) panel.remove(); });
    const title = el('h3', '', t.cheats + ' \u00b7 ' + g.name);
    title.setAttribute('translate', 'no');
    const list = el('div', 'c-list', t.cLoading);
    const how = el('p', 'c-how', (g.version ? t.cVersion + ': ' + g.version + '. ' : '') + t.cHow + (Number(data.etahen) ? '' : ' ' + t.cNoEta));
    const web = el('a', 'g-chip', t.cWeb);
    web.href = webUrl;
    const foot = el('div', 'c-foot');
    foot.append(web, close);
    card.append(title, el('h4', '', t.cFiles), list, how, foot);
    panel.append(card);
    document.body.append(panel);
    close.focus();
    const have = new Set(Array.isArray(g.cheats) ? g.cheats : []);
    loadCheats().then(all => {
      if (!panel.isConnected)
        return;
      const mine = all.filter(c => c.id === g.id).sort((a, b) => (sameVersion(b.version, g.version) - sameVersion(a.version, g.version))
        || b.version.localeCompare(a.version, undefined, {numeric: true}) || a.kind.localeCompare(b.kind));
      list.textContent = '';
      if (!mine.length) {
        list.textContent = t.cNone;
        return;
      }
      for (const c of mine) {
        const row = el('div', 'c-row');
        const what = el('span', 'c-what', c.version + ' \u00b7 ' + c.kind.toUpperCase());
        if (sameVersion(c.version, g.version))
          what.append(el('b', '', t.cYours));
        const save = el('a', 'g-chip', have.has(c.file) ? t.cThere : t.cSave);
        save.href = 'browserps5:cheat?' + c.file;
        save.addEventListener('click', () => { save.textContent = t.cAsked; });
        row.append(what, save);
        list.append(row);
      }
      const first = list.querySelector('a.g-chip');
      if (first)
        first.focus();
    }).catch(() => {
      if (panel.isConnected)
        list.textContent = t.cFailed;
    });
  };

  // 2026-11-15 (docs/32, "60 fps patches beside the cheats"). The game's patches: etaHEN's game patches plugin
  // (github.com/etaHEN/etaHEN-Plugins, itemzflow_xml_patches) applies, when a game starts, the patches of
  // /data/etaHEN/patches/<xml|xml_prospero>/<id>.xml whose switch is on (settings/0x<number>.txt, "1" or "0");
  // the scene keeps the patch files in one public repository (github.com/illusionyy/PS-Game-Patch), a file by
  // game. Which file serves which game came with the browser (BPS5_PATCH_INDEX, patches/make-index.js); a game
  // that is not there is looked for among the files the repository got since. The file is read HERE to list its
  // patches; a switch asks the app (browserps5:patch-set?...), which writes it and answers (bps5PatchSet()), and
  // when one goes on the app fetches the file itself under the name the plugin reads (browserps5:patch?...).
  // What the console has is asked of the app too (browserps5:patches?..., answered in bps5Patches()).
  const PATCH_REPO = 'https://raw.githubusercontent.com/illusionyy/PS-Game-Patch/main/patches/';
  const PATCH_LIST = 'https://api.github.com/repos/illusionyy/PS-Game-Patch/git/trees/main?recursive=1';
  // etaHEN's patches plugin, as source only (2026-11-16: no release of it anywhere)
  const PATCH_PLUGIN_WEB = 'https://github.com/etaHEN/etaHEN-Plugins/tree/main/Plugin_samples/itemzflow_xml_patches';
  const patchIndex = window.BPS5_PATCH_INDEX || {files: [], ids: {}};
  // The plugin's own number for a patch: its djb2 with xor, 64 bits, over the bytes as signed chars (1023 at most).
  const patchHash = text => {
    let hi = 0, lo = 5381;
    for (const b of new TextEncoder().encode(String(text)).subarray(0, 1023)) {
      const l = lo * 33;
      hi = (hi * 33 + Math.floor(l / 4294967296)) % 4294967296;
      lo = ((l % 4294967296) ^ (b < 128 ? b : (0xffffff00 | b))) >>> 0;
    }
    return hi.toString(16).padStart(8, '0') + lo.toString(16).padStart(8, '0');
  };
  window.bps5PatchHash = patchHash;
  let patchNew = null;
  const patchFileOf = g => {
    const kind = g.ps4 ? 'xml/' : 'xml_prospero/';
    const known = (patchIndex.ids[g.id] || []).map(i => patchIndex.files[i]).filter(f => typeof f === 'string' && f.startsWith(kind));
    if (known.length)
      return Promise.resolve(known[0]);
    if (!patchNew) {
      const have = new Set(patchIndex.files);
      patchNew = fetch(PATCH_LIST).then(r => (r.ok ? r.json() : Promise.reject(new Error('HTTP ' + r.status)))).then(list => {
        const fresh = (Array.isArray(list.tree) ? list.tree : []).map(e => String((e && e.path) || ''))
          .filter(p => /^patches\/xml(_prospero)?\/[A-Za-z0-9_.-]+\.xml$/.test(p)).map(p => p.slice(8)).filter(f => !have.has(f)).slice(0, 40);
        return Promise.all(fresh.map(f => fetch(PATCH_REPO + f).then(r => (r.ok ? r.text() : '')).catch(() => '').then(text => [f, text])));
      }).then(texts => {
        const out = {};
        for (const [f, text] of texts) {
          for (const m of text.matchAll(/<ID>\s*([A-Z]{4}[0-9]{5})\s*<\/ID>/g))
            (out[m[1]] = out[m[1]] || []).push(f);
        }
        return out;
      }).catch(() => { patchNew = null; return {}; });
    }
    return patchNew.then(more => (more[g.id] || []).filter(f => f.startsWith(kind))[0] || '');
  };
  const patchesIn = (g, file, text) => {
    const doc = new DOMParser().parseFromString(text, 'application/xml');
    if (doc.getElementsByTagName('parsererror').length)
      throw new Error('not XML');
    const where = '/data/etaHEN/patches/' + file.split('/')[0] + '/' + g.id + '.xml';
    const seen = new Set(), out = [];
    for (const m of Array.from(doc.getElementsByTagName('Metadata'))) {
      const a = n => m.getAttribute(n) || '';
      // (a note's line breaks are written out in these files, a backslash and an n: a space here)
      const p = {name: a('Name'), note: a('Note').replace(/\\n/g, ' ').replace(/\s+/g, ' ').trim(), author: a('Author'), version: a('AppVer'),
                 hash: patchHash(a('Title') + a('Name') + a('AppVer') + where + a('AppElf'))};
      if (p.name && !seen.has(p.hash)) {
        seen.add(p.hash);
        out.push(p);
      }
    }
    return out;
  };
  let patchState = null;
  const patchPaint = r => {
    const st = patchState;
    r.chip.textContent = r.on ? t.pOn : t.pOff;
    r.chip.classList.toggle('on', r.on);
    r.chip.classList.toggle('p-no', !st || !st.etahen || (!r.fits && !r.on));
  };
  window.bps5Patches = (id, s) => {  // the app: is the game's file on the console, and which switches are on
    const st = patchState;
    if (!st || st.id !== id || !st.panel.isConnected || !s)
      return;
    st.etahen = Number(s.etahen) > 0;
    let i = 0;
    for (const r of st.rows.values()) {
      r.on = String(s.on || '').charAt(i++) === '1';
      patchPaint(r);
    }
    // (2026-11-16) without etaHEN's patches plugin the switches do nothing: said, plainly (an app of before says
    // nothing of it: s.plugin undefined)
    // At the top, over the switches (the console's probe of 16:26: at the foot it was out of sight), with what is
    // missing and how to get there (the user, 16:5x: "quando pressionado dá as instruções do que falta"): etaHEN does
    // not publish the plugin ready-made, so the steps say where it comes from, where it goes and how it starts.
    st.note.textContent = st.how + (st.etahen ? '' : ' ' + t.pNoEta);
    st.plugin = s.plugin;
    st.warn.hidden = !(st.etahen && s.plugin === 0);
    if (!st.warn.hidden && !st.warn.firstChild) {
      // Short at first (the switches stay in sight); the steps open with "What is missing", or by themselves when
      // a switch is pressed (patchPluginSteps())
      const steps = el('ol', 'p-steps');
      for (const step of t.pPlugSteps)
        steps.append(el('li', '', step));
      const web = el('a', 'g-chip', t.pPlugWeb);
      web.href = PATCH_PLUGIN_WEB;
      const more = el('div', 'p-more');
      more.hidden = true;
      more.append(steps, web);
      const open = el('a', 'g-chip p-open', t.pPlugHow);
      open.href = '#';
      open.addEventListener('click', ev => { ev.preventDefault(); patchPluginSteps(st, true); });
      st.warn.append(el('b', '', t.pPlugT), el('p', '', t.pNoPlugin), open, more);
    }
  };
  // The plugin's steps open (the box's own button, focus to the link; or a switch pressed, focus stays on it)
  const patchPluginSteps = (st, fromBox) => {
    const more = st.warn.querySelector('.p-more'), open = st.warn.querySelector('.p-open');
    if (!more)
      return;
    more.hidden = false;
    if (open)
      open.hidden = true;
    st.warn.scrollIntoView({block: 'nearest', behavior: 'smooth'});
    st.warn.classList.remove('p-flash');
    void st.warn.offsetWidth;
    st.warn.classList.add('p-flash');
    if (fromBox)
      more.querySelector('a').focus();
  };
  window.bps5PatchSet = (id, hash, on) => {  // the app: this switch is written
    const st = patchState, r = st && st.id === id ? st.rows.get(hash) : null;
    if (!r)
      return;
    r.on = !!on;
    patchPaint(r);
    // One went on: the file the plugin reads is fetched, once a panel (again, when the console had one: it must
    // be the file these switches were made from).
    if (r.on && !st.fetched) {
      st.fetched = true;
      setTimeout(() => { location.href = 'browserps5:patch?' + id + '=' + st.file; }, 250);
    }
  };
  const patchesPanel = (g, webUrl) => {
    for (const id of ['cheats-panel', 'patches-panel']) {
      const old = document.getElementById(id);
      if (old)
        old.remove();
    }
    patchState = null;
    const panel = el('div');
    panel.id = 'patches-panel';
    panel.style.setProperty('--g-rgb', rgbOf(g));
    const card = el('div', 'c-card');
    const close = el('a', 'g-chip c-close', t.cClose);
    close.href = '#';
    close.addEventListener('click', ev => { ev.preventDefault(); panel.remove(); });
    panel.addEventListener('click', ev => { if (ev.target === panel) panel.remove(); });
    const title = el('h3', '', t.patches + ' \u00b7 ' + g.name);
    title.setAttribute('translate', 'no');
    const list = el('div', 'c-list', t.pLoading);
    const how = (g.version ? t.cVersion + ': ' + g.version + '. ' : '') + t.pHow;
    const note = el('p', 'c-how', how + (Number(data.etahen) ? '' : ' ' + t.pNoEta));
    const warn = el('div', 'c-how p-warn');
    warn.hidden = true;
    const web = el('a', 'g-chip', t.pWeb);
    web.href = webUrl;
    const foot = el('div', 'c-foot');
    foot.append(web, close);
    card.append(title, warn, el('h4', '', t.pFiles), list, note, foot);
    panel.append(card);
    document.body.append(panel);
    close.focus();
    const any = p => /^(mask|all)/.test(p.version);
    const fits = p => any(p) || sameVersion(p.version, g.version);
    patchFileOf(g).then(file => (file ? fetch(PATCH_REPO + file).then(r => (r.ok ? r.text() : Promise.reject(new Error('HTTP ' + r.status))))
      .then(text => [file, patchesIn(g, file, text)]) : ['', []])).then(([file, all]) => {
      if (!panel.isConnected)
        return;
      list.textContent = '';
      if (!all.length) {
        list.textContent = t.pNone;
        return;
      }
      all.sort((a, b) => (fits(b) - fits(a)) || a.name.localeCompare(b.name));
      const rows = new Map();
      for (const p of all) {
        const row = el('div', 'c-row p-row' + (fits(p) ? '' : ' p-other'));
        const what = el('span', 'c-what');
        const name = el('span', 'p-name', p.name);
        name.setAttribute('translate', 'no');
        if (fits(p) && !any(p))
          name.append(el('b', '', t.cYours));
        what.append(name, el('span', 'p-note', [p.note, any(p) ? t.pAny : t.pFor.replace('{v}', p.version),
                                              p.author ? t.pBy.replace('{a}', p.author) : ''].filter(Boolean).join(' \u00b7 ')));
        const chip = el('a', 'g-chip p-switch', t.pOff);
        chip.href = '#';
        const r = {p, chip, on: false, fits: fits(p)};
        chip.addEventListener('click', ev => {
          ev.preventDefault();
          const st = patchState;
          if (st && st.etahen && (r.fits || r.on))  // (one of another version can only be turned off)
            location.href = 'browserps5:patch-set?' + g.id + '=' + st.kind + '=' + p.hash + '=' + (r.on ? 0 : 1);
          // No plugin: the switch is still written (it works once the plugin comes), and what is missing is shown
          if (st && st.warn && !st.warn.hidden)
            patchPluginSteps(st, false);
        });
        row.append(what, chip);
        list.append(row);
        rows.set(p.hash, r);
      }
      patchState = {id: g.id, kind: file.split('/')[0], file, rows, panel, note, warn, how, etahen: Number(data.etahen) > 0, fetched: false};
      for (const r of rows.values())
        patchPaint(r);
      const first = list.querySelector('a.g-chip');
      if (first)
        first.focus();
      location.href = 'browserps5:patches?' + g.id + '=' + patchState.kind + '=' + Array.from(rows.keys()).join(',');
    }).catch(() => {
      if (panel.isConnected)
        list.textContent = t.pFailed;
    });
  };
  // (any game by its id: the probe shows a game that has patches, whatever the console's games are)
  window.bps5PatchesFor = (id, ps4, version, name) => {
    if (/^[A-Z]{4}[0-9]{5}$/.test(String(id)))
      patchesPanel({id: String(id), name: String(name || id), ps4: !!ps4, version: String(version || ''), colour: 0}, 'https://www.google.com/search?q=' + q(String(name || id) + ' ' + t.patchesQ));
  };

  // 2026-11-15 (docs/32, "a page for each game"): the game hub, over the page when the game's cover is pressed.
  // The game with its own background, "Play" (the browser leaves and the game starts: main.cpp game_play()), the
  // row's links, and its trophies ONE BY ONE as the PS5 has them (asked of the app with browserps5:game?<id>,
  // answered in bps5Game()): the ones still to win first, each a press away from how to win it.
  let hubGame = null, hubList = null, hubNotes = null, hubSum = null, hubPins = null;
  const trophyRow = (g, tr) => {
    const row = el('div', 't-row' + (tr.h ? ' have' : ''));
    const grade = el('span', 't-g g' + (tr.g | 0));
    grade.title = t.hGrades[tr.g | 0] || '';
    const words = el('div', 't-w');
    const masked = tr.s && !tr.h;
    const name = el('div', 't-n', masked ? t.hHidden : tr.n || '#' + tr.i);
    const detail = el('div', 't-d', masked ? '' : tr.d || '');
    name.setAttribute('translate', 'no');
    words.append(name, detail);
    row.append(grade, words);
    if (!tr.h) {
      const how = el('a', 'g-chip', masked ? t.hShow : t.hHow);
      const url = 'https://www.google.com/search?q=' + q('"' + (tr.n || '') + '" ' + g.name + ' ' + t.hHowQ);
      how.href = masked ? '#' : url;
      how.addEventListener('click', ev => {
        if (how.getAttribute('href') !== '#')
          return;
        ev.preventDefault();  // a hidden one: its name first, the guide at the next press
        name.textContent = tr.n || '#' + tr.i;
        detail.textContent = tr.d || '';
        how.textContent = t.hHow;
        how.href = url;
      });
      row.append(how);
    }
    return row;
  };
  // 2026-11-15 (docs/32, "the captures of each game"): the game's own screenshots and clips in its page, as the
  // app lists them with its trophies (main.cpp game_hub(): the newest first, file addresses). A few at a time
  // (each screenshot is a 4K picture to the engine); a press opens one whole, with the one before and the one
  // after a press away; a clip plays there.
  const SHOTS_STEP = 8;
  let hubShots = null;
  const shotWhen = s => {
    const d = new Date((Number(s.t) || 0) * 1000);
    return isNaN(d.getTime()) || !Number(s.t) ? '' : d.toLocaleDateString(undefined, {day: '2-digit', month: '2-digit', year: 'numeric'}) + ' ' +
      d.toLocaleTimeString(undefined, {hour: '2-digit', minute: '2-digit'});
  };
  const shotView = (all, at) => {
    const old = document.getElementById('shot-view');
    if (old)
      old.shut();
    const s = all[at];
    if (!s)
      return;
    const view = el('div');
    view.id = 'shot-view';
    let media;
    if (s.v) {
      media = document.createElement('video');
      media.controls = true;
      media.autoplay = true;
      media.playsInline = true;
    } else {
      media = new Image();
      media.alt = '';
    }
    media.src = s.u;
    view.shut = () => {  // (a clip stops with its view)
      if (s.v) {
        media.pause();
        media.removeAttribute('src');
        media.load();
      }
      view.remove();
    };
    const chip = (label, off, fn) => {
      const a = el('a', 'g-chip' + (off ? ' s-off' : ''), label);
      a.href = '#';
      a.addEventListener('click', ev => { ev.preventDefault(); fn(); });
      return a;
    };
    const prev = chip(t.hPrev, at <= 0, () => shotView(all, at - 1));
    const next = chip(t.hNext, at >= all.length - 1, () => shotView(all, at + 1));
    const close = chip(t.cClose, false, () => view.shut());
    // 2026-11-16 (docs/32, idea 4): to a Discord channel by its webhook (main.cpp shot_share(); bps5HookAsk() below
    // when the app has no webhook yet)
    const discord = chip(t.hDiscord, false, () => {
      if (hubGame)
        location.href = 'browserps5:shot-share?' + hubGame.id + '=' + encodeURIComponent(s.u);
    });
    discord.classList.add('s-discord');
    const bar = el('div', 's-bar');
    bar.append(prev, el('span', 's-when', [shotWhen(s), (at + 1) + ' / ' + all.length].filter(Boolean).join(' \u00b7 ')), next, discord, close);
    view.append(media, bar);
    view.addEventListener('click', ev => { if (ev.target === view) view.shut(); });
    document.body.append(view);
    (at < all.length - 1 ? next : close).focus();
  };
  // The app has no Discord webhook (or Discord forgot it): a box over the capture to paste one; it goes back with
  // the capture (main.cpp shot_share(): "&hook=").
  window.bps5HookAsk = (id, url) => {
    const view = document.getElementById('shot-view');
    if (!view || !hubGame || hubGame.id !== id)
      return;
    const old = view.querySelector('.s-hook');
    if (old)
      old.remove();
    const box = el('form', 's-hook');
    const field = document.createElement('input');
    field.type = 'url';
    field.spellcheck = false;
    field.autocomplete = 'off';
    field.placeholder = 'https://discord.com/api/webhooks/…';
    field.setAttribute('aria-label', t.hHook);
    const save = el('button', 'g-chip', t.hHookSave);
    save.type = 'submit';
    const button = (label, fn) => {
      const b = el('button', 'g-chip', label);
      b.type = 'button';
      b.addEventListener('click', fn);
      return b;
    };
    const cancel = button(t.hHookCancel, () => { box.remove(); const d = view.querySelector('.s-discord'); if (d) d.focus(); });
    const forget = button(t.hHookForget, () => { location.href = 'browserps5:discord-forget'; box.remove(); });
    const row = el('div', 's-hook-row');
    row.append(save, cancel, forget);
    box.append(el('b', '', t.hHook), el('p', '', t.hHookHelp), field, row);
    box.addEventListener('submit', ev => {
      ev.preventDefault();
      const hook = field.value.trim();
      if (!hook)
        return field.focus();
      location.href = 'browserps5:shot-share?' + id + '=' + encodeURIComponent(url) + '&hook=' + encodeURIComponent(hook);
      box.remove();
    });
    view.append(box);
    field.focus();
    // (the probe of 15:46 found it not focused: the box had just come into a view that was still showing) again
    // once it is drawn
    requestAnimationFrame(() => { if (box.isConnected && document.activeElement !== field) field.focus(); });
  };
  const showShots = (all, hdr) => {
    const [title, grid] = hubShots;
    grid.textContent = '';
    title.textContent = t.hShots + (all.length ? ' \u00b7 ' + all.length : '');
    if (hdr)
      title.append(el('span', 'h-hdr', t.hHdr.replace('{n}', String(hdr))));
    title.style.display = grid.style.display = all.length || hdr ? '' : 'none';
    let shown = 0;
    const more = el('a', 'g-chip h-more', t.hMore);
    more.href = '#';
    const add = () => {
      for (const s of all.slice(shown, shown + SHOTS_STEP)) {
        const at = shown++;
        const tile = el('a', 'h-shot' + (s.v ? ' v' : ''));
        tile.href = '#';
        if (!s.v) {
          const img = new Image();
          img.alt = '';
          img.decoding = 'async';
          img.src = s.u;
          tile.append(img);
        }
        tile.append(el('span', '', shotWhen(s)));
        tile.addEventListener('click', ev => { ev.preventDefault(); shotView(all, at); });
        grid.insertBefore(tile, more.parentNode === grid ? more : null);
      }
      if (shown >= all.length)
        more.remove();
    };
    more.addEventListener('click', ev => { ev.preventDefault(); add(); });
    if (all.length > SHOTS_STEP)
      grid.append(more);
    add();
  };
  // 2026-11-16 (docs/32, "pages kept with a game"): the guides and maps the user kept with the game from the menu
  // ("Guardar no jogo": main.cpp game_pin_current()), each a press away, each taken away with its button.
  const showPins = pins => {
    const box = hubPins;
    box.textContent = '';
    const host = u => { try { return new URL(u).hostname.replace(/^www\./, ''); } catch (e) { return ''; } };
    const hint = () => box.append(el('div', 'h-pins-none', t.hPinsNone));
    const ok = (Array.isArray(pins) ? pins : []).filter(p => p && /^https?:\/\//i.test(String(p.u)));
    for (const p of ok) {
      const row = el('div', 'h-pin-row');
      const a = el('a', 'h-pin');
      a.href = p.u;
      const words = el('b', '', String(p.t || '').trim() || host(p.u) || p.u);
      a.append(words, el('span', '', host(p.u)));
      const x = el('a', 'g-chip h-unpin', t.hUnpin);
      x.href = '#';
      x.addEventListener('click', ev => {
        ev.preventDefault();
        location.href = 'browserps5:game-unpin?' + hubGame.id + '=' + encodeURIComponent(p.u);
        const next = row.nextElementSibling || row.previousElementSibling;
        row.remove();
        if (!box.querySelector('.h-pin'))
          hint();
        else if (next)
          (next.querySelector('.h-pin') || next).focus();
      });
      row.append(a, x);
      box.append(row);
    }
    if (!ok.length)
      hint();
  };
  window.bps5Game = (id, list, note, shots, hdr, pins) => {
    if (!hubGame || hubGame.id !== id || !hubList || !hubList.isConnected)
      return;
    if (hubPins)
      showPins(pins);
    if (hubShots)
      showShots((Array.isArray(shots) ? shots : []).filter(s => s && typeof s.u === 'string' && s.u), Number(hdr) || 0);
    if (hubNotes && typeof note === 'string' && document.activeElement !== hubNotes)
      hubNotes.value = note;
    hubList.textContent = '';
    if (!Array.isArray(list) || !list.length) {
      hubList.append(el('div', 't-none', t.hNone));
      return;
    }
    const missing = list.filter(x => !x.h), won = list.filter(x => x.h);
    if (hubSum) {  // how far the user is: won of all, the share, and what is left by grade
      const left = [3, 2, 1, 0].map(g => [g, missing.filter(x => (x.g | 0) === g).length]).filter(x => x[1]);
      hubSum.textContent = '';
      hubSum.append(el('span', 'h-pct', Math.floor(won.length * 100 / list.length) + '%'), el('span', '', won.length + ' / ' + list.length));
      for (const [g, n] of left) {
        const dot = el('span', 't-g g' + g);
        dot.title = t.hGrades[g];
        hubSum.append(dot, el('span', '', String(n)));
      }
    }
    for (const [label, part] of [[t.hMissing, missing], [t.hWon, won]]) {
      if (!part.length)
        continue;
      hubList.append(el('h4', '', label + ' \u00b7 ' + part.length));
      for (const tr of part)
        hubList.append(trophyRow(hubGame, tr));
    }
  };
  const gameHub = g => {
    const old = document.getElementById('game-hub');
    if (old)
      old.remove();
    const hub = el('div');
    hub.id = 'game-hub';
    hub.style.setProperty('--g-rgb', rgbOf(g));
    const card = el('div', 'h-card');
    if (g.picture)
      card.style.backgroundImage = 'linear-gradient(180deg, rgba(10, 12, 22, .55) 0, rgba(14, 17, 28, .96) 330px, #10131c 520px), url("' + g.picture + '")';
    const head = el('div', 'h-head');
    const cover = el('span', 'h-cover');
    cover.append(picture(g, 'g-pic'));
    const info = el('div', 'h-info');
    const name = el('div', 'h-name', g.name);
    name.setAttribute('translate', 'no');
    const state = el('div', 'g-state');
    state.append(el('span', '', when(g) + (g.ps4 ? ' \u00b7 PS4' : '') + (g.version ? ' \u00b7 ' + g.version : '')));
    const links = el('div', 'g-links');
    const play = el('a', 'g-chip h-play', t.hPlay);
    play.href = 'browserps5:play?' + g.id;
    links.append(play);
    for (const [label, url] of linksOf(g)) {
      const a = el('a', 'g-chip', label);
      a.href = url;
      if (label === t.cheats)
        a.addEventListener('click', ev => { ev.preventDefault(); cheatsPanel(g, url); });
      if (label === t.patches)
        a.addEventListener('click', ev => { ev.preventDefault(); patchesPanel(g, url); });
      links.append(a);
    }
    const close = el('a', 'g-chip h-close', t.cClose);
    close.href = '#';
    close.addEventListener('click', ev => { ev.preventDefault(); hub.remove(); });
    hub.addEventListener('click', ev => { if (ev.target === hub) hub.remove(); });
    links.append(close);
    info.append(state, name, links);
    head.append(cover, info);
    const list = el('div', 't-list');
    list.append(el('div', 't-none', t.hLoading));
    // The user's notes of this game, kept by the app with the game (main.cpp game_note_save()).
    const notes = document.createElement('textarea');
    notes.className = 'h-notes';
    notes.placeholder = t.hNotesPh;
    notes.maxLength = 1500;
    notes.rows = 3;
    const kept = el('span', 'h-kept', '');
    const keep = () => {
      location.href = 'browserps5:note-save?' + g.id + '=' + encodeURIComponent(notes.value.slice(0, 1500));
      kept.textContent = t.hNotesKept;
      setTimeout(() => { kept.textContent = ''; }, 2500);
    };
    notes.addEventListener('change', keep);
    const notesHead = el('h3', '', t.hNotes);
    notesHead.append(kept);
    const sum = el('div', 'h-sum');
    const trophiesHead = el('h3', '', t.hTrophies);
    trophiesHead.append(sum);
    // the game's captures (showShots()): there once the app says it has some
    const shotsHead = el('h3', '', t.hShots);
    const shots = el('div', 'h-shots');
    shotsHead.style.display = shots.style.display = 'none';
    // the pages kept with the game (showPins())
    const pinsHead = el('h3', '', t.hPins);
    const pins = el('div', 'h-pins');
    card.append(head, pinsHead, pins, shotsHead, shots, notesHead, notes, trophiesHead, list);
    hubShots = [shotsHead, shots];
    hubPins = pins;
    hub.append(card);
    document.body.append(hub);
    hubGame = g;
    hubList = list;
    hubNotes = notes;
    hubSum = sum;
    play.focus();
    location.href = 'browserps5:game?' + g.id;
  };

  // 2026-11-16 (docs/32, "the whole library in your games"): every game of the console as a grid of covers. The app
  // gives them when asked (main.cpp library_tell(): browserps5:library -> bps5Library(list, now)), each as the row
  // has it, so each opens its own page (gameHub()) over the grid. Sorted as the console has them (open, then played
  // last) or by name, all or PS5 or PS4 only, and found by a part of the name. The covers load as they come in view.
  let libBox = null;
  const gameLibrary = () => {
    const old = document.getElementById('game-lib');
    if (old)
      old.remove();
    const lib = el('div');
    lib.id = 'game-lib';
    const card = el('div', 'l-card');
    const title = el('h2', 'l-title', t.lib);
    const tools = el('div', 'l-tools');
    const find = document.createElement('input');
    find.type = 'search';
    find.className = 'l-find';
    find.placeholder = t.libFind;
    find.maxLength = 60;
    let order = 'recent', kind = 'all', list = null;
    const chips = [];
    const chip = (label, group, value) => {
      const a = el('a', 'g-chip', label);
      a.href = '#';
      a.dataset.group = group;
      a.dataset.value = value;
      a.addEventListener('click', ev => {
        ev.preventDefault();
        if (group === 'order')
          order = value;
        else
          kind = value;
        paint();
      });
      chips.push(a);
      return a;
    };
    const first = chip(t.libRecent, 'order', 'recent');
    // (and by the share of the trophies won: the closest to the platinum first, the finished ones after them)
    tools.append(first, chip(t.libAz, 'order', 'az'), chip(t.libTrophies, 'order', 'trophies'), el('span', 'l-gap'), chip(t.libEvery, 'kind', 'all'), chip('PS5', 'kind', 'ps5'),
                 chip('PS4', 'kind', 'ps4'), find);
    const close = el('a', 'g-chip l-close', t.cClose);
    close.href = '#';
    close.addEventListener('click', ev => { ev.preventDefault(); lib.remove(); });
    tools.append(close);
    const grid = el('div', 'l-grid');
    grid.append(el('div', 't-none', t.libLoading));
    const paint = () => {
      for (const c of chips)
        c.classList.toggle('on', (c.dataset.group === 'order' ? order : kind) === c.dataset.value);
      if (!list)
        return;
      const q = find.value.trim().toLowerCase();
      let shown = list.filter(g => (kind === 'all' || (kind === 'ps4') === !!g.ps4) && (!q || g.name.toLowerCase().includes(q)));
      const share = g => (Array.isArray(g.trophies) && g.trophies[1] > 0 ? g.trophies[0] / g.trophies[1] : -1);
      if (order === 'az')
        shown = shown.slice().sort((a, b) => a.name.localeCompare(b.name));
      else if (order === 'trophies')
        shown = shown.slice().sort((a, b) => {
          const x = share(a), y = share(b), xa = x >= 1 ? -0.5 : x, ya = y >= 1 ? -0.5 : y;
          return ya - xa || a.name.localeCompare(b.name);
        });
      title.textContent = t.lib + ' \u00b7 ' + list.length;
      const won = list.reduce((n, g) => n + (Array.isArray(g.trophies) ? Number(g.trophies[0]) || 0 : 0), 0);
      if (won) {
        const near = list.filter(g => share(g) >= 0.5 && share(g) < 1).length;
        title.append(el('span', 'l-sum', t.libSum.replace('{w}', String(won)).replace('{p}', String(near))));
      }
      grid.textContent = '';
      for (const g of shown) {
        const a = el('a', 'l-game');
        a.href = '#';
        a.title = g.name;
        const pic = el('span', 'g-pic', g.name.trim().charAt(0).toUpperCase());
        if (g.icon) {
          const img = document.createElement('img');
          img.alt = '';
          img.loading = 'lazy';
          img.decoding = 'async';
          // (in the page from the start, over the letter: a lazy picture is only loaded once it is in view)
          img.onload = () => pic.classList.add('in');
          img.onerror = () => img.remove();
          img.src = g.icon;
          pic.append(img);
        }
        const name = el('span', 'l-name', g.name);
        name.setAttribute('translate', 'no');
        a.append(pic, name);
        if (share(g) >= 0) {  // how far the user is: a bar on the cover and the share
          const pct = Math.floor(share(g) * 100);
          const bar = el('span', 'l-bar' + (pct >= 100 ? ' done' : ''));
          const fill = el('span', '');
          fill.style.width = pct + '%';
          bar.append(fill);
          pic.append(bar);
          a.append(el('span', 'l-pct', pct + '% \u00b7 ' + g.trophies[0] + '/' + g.trophies[1]));
        }
        if (g.open)
          a.append(el('span', 'l-open'));
        a.addEventListener('click', ev => { ev.preventDefault(); gameHub(g); });
        grid.append(a);
      }
      if (!shown.length)
        grid.append(el('div', 't-none', list.length ? t.libNoMatch : t.libNone));
    };
    find.addEventListener('input', paint);
    lib.games = items => {
      list = items;
      paint();
      const tile = grid.querySelector('.l-game');
      if (tile && (document.activeElement === first || !lib.contains(document.activeElement)))
        tile.focus();
    };
    lib.addEventListener('click', ev => { if (ev.target === lib) lib.remove(); });
    card.append(title, tools, grid);
    lib.append(card);
    document.body.append(lib);
    libBox = lib;
    paint();
    first.focus();
    location.href = 'browserps5:library';
  };
  window.bps5Library = items => {
    if (libBox && libBox.isConnected)
      libBox.games((Array.isArray(items) ? items : []).map(shownName).filter(g => g && typeof g.id === 'string' && typeof g.name === 'string' && g.name));
  };

  let shownGame = null;
  const showGame = g => {
    if (g === shownGame)
      return;
    shownGame = g;
    box.style.setProperty('--g-rgb', rgbOf(g));
    // (2026-11-15) and behind the whole start page, faint: the browser takes the look of the game played.
    let back = document.getElementById('game-bg');
    if (!back) {
      back = el('div');
      back.id = 'game-bg';
      document.body.prepend(back);
    }
    back.style.backgroundImage = g.picture ? 'url("' + g.picture + '")' : '';
    // The game's own background picture behind its card, faint (the PS5's home screen does the same with a game).
    card.style.backgroundImage = g.picture
      ? 'linear-gradient(105deg, rgba(8, 10, 20, .86) 0%, rgba(8, 10, 20, .66) 45%, rgba(8, 10, 20, .44) 100%), url("' + g.picture + '")'
      : '';
    card.classList.toggle('pictured', !!g.picture);
    card.textContent = '';
    const cover = el('a', 'g-cover');
    cover.href = linksOf(g)[0][1];
    cover.addEventListener('click', ev => { ev.preventDefault(); gameHub(g); });  // the game's own page
    cover.append(picture(g, 'g-pic'));
    const info = el('div', 'g-info');
    const state = el('div', 'g-state');
    if (g.open)
      state.append(el('span', 'dot'));
    state.append(el('span', '', when(g) + (g.ps4 ? ' · PS4' : '')));
    // The user's trophies of it (games.c, from the PS5's own record of them): how many of how many.
    if (Array.isArray(g.trophies) && g.trophies[1] > 0) {
      // (a drawing, not the emoji: the console has no font with it)
      const cup = el('span', 'g-cup');
      cup.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path fill="currentColor" d="M7 3h10v2h3v3a4 4 0 0 1-3.3 3.9A5 5 0 0 1 13 14.9V18h3v3H8v-3h3v-3.1A5 5 0 0 1 7.3 11.9 4 4 0 0 1 4 8V5h3V3zm0 4H6v1a2 2 0 0 0 1 1.7V7zm10 0v2.7A2 2 0 0 0 18 8V7h-1z"/></svg>';
      cup.append(el('span', '', g.trophies[0] + ' / ' + g.trophies[1]));
      cup.title = t.trophies;
      state.append(cup);
    }
    const name = el('div', 'g-name', g.name);
    name.setAttribute('translate', 'no');
    const links = el('div', 'g-links');
    for (const [label, url] of linksOf(g)) {
      const a = el('a', 'g-chip', label);
      a.href = url;
      if (label === t.cheats)  // its own panel: etaHEN's cheat files for this game, then the web's codes
        a.addEventListener('click', ev => { ev.preventDefault(); cheatsPanel(g, url); });
      if (label === t.patches)
        a.addEventListener('click', ev => { ev.preventDefault(); patchesPanel(g, url); });
      links.append(a);
    }
    info.append(state, name, links);
    card.append(cover, info);
    for (const o of others.querySelectorAll('.g-other'))
      o.classList.toggle('on', o.dataset.id === g.id);
  };

  if (games.length > 1) {
    others.append(el('span', 'lbl', t.others));
    for (const g of games) {
      const a = el('a', 'g-other');
      a.href = '#';
      a.title = g.name;
      a.dataset.id = g.id;
      a.append(picture(g, 'g-pic'));
      const pick = ev => { ev.preventDefault(); showGame(g); };
      a.addEventListener('click', pick);
      a.addEventListener('focus', () => showGame(g));
      others.append(a);
    }
  }
  // (2026-11-16) every game of the console, a press away (gameLibrary())
  const every = el('a', 'g-chip g-lib', t.lib);
  every.href = '#';
  every.addEventListener('click', ev => { ev.preventDefault(); gameLibrary(); });
  others.append(every);
  box.append(others);
  showGame(games[0]);
  const after = document.getElementById('recent-box');
  if (after)
    after.before(box);
  else
    (document.querySelector('main') || document.body).append(box);
})();

// 2026-11-14 (a gaming browser, docs/32): "for players", under the game. What someone with a controller in the
// hand goes looking for, one press away: what comes out, what is on sale, the news, who is live, games that play
// in the browser itself (they get the controller: ext/gamepad.js), and how the network is doing for playing.
// Not favorites: nothing to remove, and gone while the favorites are edited. Always there, also on a console
// with no game yet.
(() => {
  if (document.getElementById('players-box'))
    return;
  const TEXTS = {
    pt: {title: 'Para jogadores', releases: 'Lançamentos', deals: 'Promoções', plus: 'PS Plus do mês', news: 'Notícias',
         live: 'Em direto', web: 'Jogos no browser', net: 'Teste de rede', plusQ: 'PS Plus jogos do mês', newsUrl: 'https://www.eurogamer.pt/',
         console: 'Consola', hlTitle: 'Saúde da consola', hlCpu: 'Processador', hlSoc: 'Chip', hlPower: 'Consumo', hlFreq: 'Frequência', hlFan: 'Alvo da ventoinha', hlUp: 'Ligada há', hlDisks: 'Armazenamento', hlNet: 'Rede', hlFree: 'livres de', hlNone: 'sem leitura', hlMem: 'Memória', hlClose: 'Fechar', hlNoNet: 'sem rede',
         hlTest: 'Testar a rede', hlTesting: 'A medir…', hlPing: 'Latência', hlDown: 'Descarga', hlTestFail: 'Não consegui medir (sem rede?)'},
    en: {title: 'For players', releases: 'New releases', deals: 'Deals', plus: 'PS Plus this month', news: 'News',
         live: 'Live', web: 'Games in the browser', net: 'Network test', plusQ: 'PS Plus monthly games', newsUrl: 'https://www.ign.com/',
         console: 'Console', hlTitle: 'Console health', hlCpu: 'Processor', hlSoc: 'Chip', hlPower: 'Power', hlFreq: 'Frequency', hlFan: 'Fan target', hlUp: 'On for', hlDisks: 'Storage', hlNet: 'Network', hlFree: 'free of', hlNone: 'no reading', hlMem: 'Memory', hlClose: 'Close', hlNoNet: 'no network',
         hlTest: 'Test the network', hlTesting: 'Measuring…', hlPing: 'Latency', hlDown: 'Download', hlTestFail: 'Could not measure (no network?)'},
    es: {title: 'Para jugadores', releases: 'Lanzamientos', deals: 'Ofertas', plus: 'PS Plus del mes', news: 'Noticias',
         live: 'En directo', web: 'Juegos en el navegador', net: 'Prueba de red', plusQ: 'PS Plus juegos del mes', newsUrl: 'https://www.eurogamer.es/',
         console: 'Consola', hlTitle: 'Salud de la consola', hlCpu: 'Procesador', hlSoc: 'Chip', hlPower: 'Consumo', hlFreq: 'Frecuencia', hlFan: 'Objetivo del ventilador', hlUp: 'Encendida hace', hlDisks: 'Almacenamiento', hlNet: 'Red', hlFree: 'libres de', hlNone: 'sin lectura', hlMem: 'Memoria', hlClose: 'Cerrar', hlNoNet: 'sin red',
         hlTest: 'Probar la red', hlTesting: 'Midiendo…', hlPing: 'Latencia', hlDown: 'Descarga', hlTestFail: 'No se pudo medir (¿sin red?)'},
    fr: {title: 'Pour les joueurs', releases: 'Sorties', deals: 'Promotions', plus: 'PS Plus du mois', news: 'Actualités',
         live: 'En direct', web: 'Jeux dans le navigateur', net: 'Test réseau', plusQ: 'PS Plus jeux du mois', newsUrl: 'https://www.jeuxvideo.com/',
         console: 'Console', hlTitle: 'Santé de la console', hlCpu: 'Processeur', hlSoc: 'Puce', hlPower: 'Consommation', hlFreq: 'Fréquence', hlFan: 'Cible du ventilateur', hlUp: 'Allumée depuis', hlDisks: 'Stockage', hlNet: 'Réseau', hlFree: 'libres sur', hlNone: 'pas de lecture', hlMem: 'Mémoire', hlClose: 'Fermer', hlNoNet: 'pas de réseau',
         hlTest: 'Tester le réseau', hlTesting: 'Mesure…', hlPing: 'Latence', hlDown: 'Téléchargement', hlTestFail: 'Mesure impossible (pas de réseau ?)'},
    de: {title: 'Für Spieler', releases: 'Neuerscheinungen', deals: 'Angebote', plus: 'PS Plus des Monats', news: 'News',
         live: 'Live', web: 'Spiele im Browser', net: 'Netzwerktest', plusQ: 'PS Plus Spiele des Monats', newsUrl: 'https://www.eurogamer.de/',
         console: 'Konsole', hlTitle: 'Zustand der Konsole', hlCpu: 'Prozessor', hlSoc: 'Chip', hlPower: 'Leistung', hlFreq: 'Frequenz', hlFan: 'Lüfterziel', hlUp: 'Eingeschaltet seit', hlDisks: 'Speicher', hlNet: 'Netzwerk', hlFree: 'frei von', hlNone: 'kein Messwert', hlMem: 'Speicherchips', hlClose: 'Schließen', hlNoNet: 'kein Netzwerk',
         hlTest: 'Netzwerk testen', hlTesting: 'Messung…', hlPing: 'Latenz', hlDown: 'Download', hlTestFail: 'Messung nicht möglich (kein Netz?)'},
    it: {title: 'Per i giocatori', releases: 'Uscite', deals: 'Offerte', plus: 'PS Plus del mese', news: 'Notizie',
         live: 'In diretta', web: 'Giochi nel browser', net: 'Test di rete', plusQ: 'PS Plus giochi del mese', newsUrl: 'https://www.eurogamer.it/',
         console: 'Console', hlTitle: 'Salute della console', hlCpu: 'Processore', hlSoc: 'Chip', hlPower: 'Consumo', hlFreq: 'Frequenza', hlFan: 'Obiettivo della ventola', hlUp: 'Accesa da', hlDisks: 'Archiviazione', hlNet: 'Rete', hlFree: 'liberi di', hlNone: 'nessuna lettura', hlMem: 'Memoria', hlClose: 'Chiudi', hlNoNet: 'nessuna rete',
         hlTest: 'Prova la rete', hlTesting: 'Misura…', hlPing: 'Latenza', hlDown: 'Download', hlTestFail: 'Impossibile misurare (senza rete?)'},
    nl: {title: 'Voor gamers', releases: 'Nieuwe games', deals: 'Aanbiedingen', plus: 'PS Plus deze maand', news: 'Nieuws',
         live: 'Live', web: 'Games in de browser', net: 'Netwerktest', plusQ: 'PS Plus games van de maand', newsUrl: 'https://www.eurogamer.nl/',
         console: 'Console', hlTitle: 'Gezondheid van de console', hlCpu: 'Processor', hlSoc: 'Chip', hlPower: 'Verbruik', hlFreq: 'Frequentie', hlFan: 'Doel van de ventilator', hlUp: 'Aan sinds', hlDisks: 'Opslag', hlNet: 'Netwerk', hlFree: 'vrij van', hlNone: 'geen meting', hlMem: 'Geheugen', hlClose: 'Sluiten', hlNoNet: 'geen netwerk',
         hlTest: 'Netwerk testen', hlTesting: 'Meten…', hlPing: 'Vertraging', hlDown: 'Download', hlTestFail: 'Meten lukte niet (geen netwerk?)'},
  };
  const t = TEXTS[String(data.lang || 'pt')] || TEXTS.en;
  const links = [
    [t.releases, 'https://www.ign.com/upcoming/games/ps5'],
    [t.deals, 'https://psdeals.net/'],
    [t.plus, 'https://www.google.com/search?q=' + encodeURIComponent(t.plusQ)],
    [t.news, t.newsUrl],
    [t.live, 'https://www.twitch.tv/directory'],
    [t.web, 'https://poki.com/'],
    ['itch.io', 'https://itch.io/games/html5'],
    [t.net, 'https://www.speedtest.net/'],
  ];
  const box = el('section');
  box.id = 'players-box';
  const row = el('div', 'p-links');
  for (const [label, url] of links) {
    const a = el('a', 'g-chip', label);
    a.href = url;
    row.append(a);
  }
  // 2026-11-15 (docs/32, "the console's health"): a link of the row that opens a panel over the page with the
  // console's temperatures, power, fan, drives and address, as the app reads them (main.cpp health_tell(), asked
  // with browserps5:health about every two seconds while the panel is open, answered in bps5Health()).
  const health = el('a', 'g-chip p-health', t.console);
  health.href = '#';
  row.append(health);
  let healthTimer = 0, healthBody = null;
  const size = bytes => {
    const gib = bytes / 1073741824;
    return gib >= 100 ? Math.round(gib) + ' GB' : gib >= 1 ? gib.toFixed(1) + ' GB' : Math.round(bytes / 1048576) + ' MB';
  };
  const lasted = seconds => {
    const d = Math.floor(seconds / 86400), h = Math.floor(seconds % 86400 / 3600), m = Math.floor(seconds % 3600 / 60);
    return (d ? d + ' d ' : '') + (d || h ? h + ' h ' : '') + m + ' min';
  };
  const tile = (label, value, note, heat) => {
    const box = el('div', 'hl-tile' + (heat ? ' ' + heat : ''));
    box.append(el('div', 'hl-l', label), el('div', 'hl-v', value), el('div', 'hl-n', note || ''));
    return box;
  };
  const heatOf = c => (c < 0 ? '' : c >= 85 ? 'hot' : c >= 70 ? 'warm' : 'cool');
  window.bps5Health = h => {
    if (!healthBody || !healthBody.isConnected || !h)
      return;
    const deg = c => (c < 0 ? t.hlNone : c + ' \u00b0C');
    const watts = mw => (mw < 0 ? '' : (mw / 1000).toFixed(1) + ' W');
    const total = h.gpu_mw < 0 ? -1 : h.gpu_mw + h.cpu_mw + h.mem_mw;
    const tiles = el('div', 'hl-tiles');
    tiles.append(
      tile(t.hlCpu, deg(h.cpu), h.mhz > 0 ? h.mhz + ' MHz' : '', heatOf(h.cpu)),
      tile(t.hlSoc, deg(h.soc), '', heatOf(h.soc)),
      tile(t.hlPower, total < 0 ? t.hlNone : watts(total), total < 0 ? '' : 'GPU ' + watts(h.gpu_mw) + ' \u00b7 CPU ' + watts(h.cpu_mw) + (h.mem_mw > 0 ? ' \u00b7 ' + t.hlMem + ' ' + watts(h.mem_mw) : '')),
      tile(t.hlFan, deg(h.fan), ''),
      tile(t.hlUp, lasted(Number(h.up) || 0), ''),
      tile(t.hlNet, h.ip || t.hlNoNet, h.net || ''));
    const disks = el('div', 'hl-disks');
    const seenDisks = new Set();
    for (const d of Array.isArray(h.disks) ? h.disks : []) {
      if (seenDisks.has(d.t + ':' + d.f))  // (/data is on /user's drive: the same numbers are the same drive)
        continue;
      seenDisks.add(d.t + ':' + d.f);
      const used = d.t > 0 ? Math.max(0, Math.min(1, 1 - d.f / d.t)) : 0;
      const line = el('div', 'hl-disk');
      const bar = el('div', 'hl-bar');
      const fill = el('span');
      fill.style.width = Math.round(used * 100) + '%';
      bar.append(fill);
      line.append(el('div', 'hl-p', d.p), bar, el('div', 'hl-s', size(d.f) + ' ' + t.hlFree + ' ' + size(d.t)));
      disks.append(line);
    }
    healthBody.textContent = '';
    healthBody.append(tiles, el('h4', '', t.hlDisks), disks);
  };
  health.addEventListener('click', ev => {
    ev.preventDefault();
    const old = document.getElementById('health-panel');
    if (old)
      old.remove();
    const panel = el('div');
    panel.id = 'health-panel';
    const card = el('div', 'hl-card');
    const close = el('a', 'g-chip hl-close', t.hlClose);
    close.href = '#';
    const shut = () => { clearInterval(healthTimer); healthTimer = 0; panel.remove(); };
    close.addEventListener('click', e => { e.preventDefault(); shut(); });
    panel.addEventListener('click', e => { if (e.target === panel) shut(); });
    const head = el('div', 'hl-head');
    // A network test for playing: how long an answer takes (the median of 6 small ones) and how fast a big file
    // comes (up to 5 s of it), against a public measuring service that lets any page read it.
    const test = el('a', 'g-chip hl-test', t.hlTest);
    test.href = '#';
    const result = el('span', 'hl-result', '');
    let testing = false;
    test.addEventListener('click', async e => {
      e.preventDefault();
      if (testing)
        return;
      testing = true;
      result.textContent = t.hlTesting;
      try {
        const base = 'https://speed.cloudflare.com/__down?bytes=';
        const times = [];
        for (let i = 0; i < 6; i++) {
          const at = performance.now();
          await (await fetch(base + '0&r=' + Math.random(), {cache: 'no-store'})).arrayBuffer();
          times.push(performance.now() - at);
        }
        times.sort((a, b) => a - b);
        const ping = Math.round((times[2] + times[3]) / 2);
        const started = performance.now();
        const reader = (await fetch(base + '100000000&r=' + Math.random(), {cache: 'no-store'})).body.getReader();
        let bytes = 0;
        for (;;) {
          const part = await reader.read();
          if (part.done)
            break;
          bytes += part.value.length;
          if (performance.now() - started > 5000) {
            reader.cancel();
            break;
          }
        }
        const seconds = (performance.now() - started) / 1000;
        const mbit = bytes * 8 / seconds / 1e6;
        result.textContent = t.hlPing + ' ' + ping + ' ms \u00b7 ' + t.hlDown + ' ' + (mbit >= 100 ? Math.round(mbit) : mbit.toFixed(1)) + ' Mb/s';
      } catch (err) {
        result.textContent = t.hlTestFail;
      }
      testing = false;
    });
    const right = el('div', 'hl-right');
    right.append(result, test, close);
    head.append(el('h3', '', t.hlTitle), right);
    healthBody = el('div', 'hl-body');
    card.append(head, healthBody);
    panel.append(card);
    document.body.append(panel);
    close.focus();
    const ask = () => { if (panel.isConnected) location.href = 'browserps5:health'; else shut(); };
    ask();
    clearInterval(healthTimer);
    healthTimer = setInterval(ask, 2000);
  });
  box.append(el('h2', '', t.title), row);
  const games = document.getElementById('games-box'), recent = document.getElementById('recent-box');
  if (games)
    games.after(box);
  else if (recent)
    recent.before(box);
  else
    (document.querySelector('main') || document.body).append(box);
})();

// 2026-11-16 (docs/32): "Homebrew" in the
// players' row. The tools of the PS5's homebrew scene and their newest versions, read where they are published
// (GitHub's public interface, which lets any page read it: api.github.com/repos/<owner>/<name>/releases/latest),
// with the date, "New" for a version of the last two weeks or one that came since the panel was last opened, its
// notes, its page, and its files: a .elf one goes to the downloads, where "Run" sends it to the console's loader
// (main.cpp download_run_payload(), pressed twice). Read again when the panel opens and the last reading is an hour
// old (kept in the page's storage when there is one: GitHub answers 60 times an hour to a console).
(() => {
  const row = document.querySelector('#players-box .p-links');
  if (!row)
    return;
  const TEXTS = {
    pt: {hb: 'Homebrew', title: 'A cena homebrew', loading: 'A ler as versões no GitHub…', failed: 'Não consegui ler o GitHub (sem rede, ou demasiados pedidos: tenta daqui a uma hora).',
         fresh: 'Novo', since: 'Novo desde a última vez', notes: 'Notas', page: 'Ver no GitHub', files: 'Ficheiros', close: 'Fechar', refresh: 'Atualizar',
         read: 'Lido às {t}', how: 'Um ficheiro .elf vai para as Transferências; aí, "Executar" (carregar duas vezes) manda-o ao carregador de payloads da consola.',
         eta: 'O ativador de homebrew: cheats, patches, plugins e o menu de ferramentas', web: 'Lançador de homebrew no browser, com jogos portados (Doom, Diablo, Duke Nukem 3D...)',
         elf: 'Carregador de payloads ELF na porta 9021', ftp: 'Servidor de FTP para os ficheiros da consola', sh: 'Linha de comandos na rede (telnet)',
         klog: 'Registo do sistema pela rede', sdk: 'O SDK para fazer payloads', running: 'A correr na consola · porta {p}', there: 'Instalado na consola', launch: 'Abrir o lançador', ftpUse: 'No PC: ftp://{ip}:2121'},
    en: {hb: 'Homebrew', title: 'The homebrew scene', loading: 'Reading the versions on GitHub…', failed: 'GitHub could not be read (no network, or too many requests: try again in an hour).',
         fresh: 'New', since: 'New since last time', notes: 'Notes', page: 'See on GitHub', files: 'Files', close: 'Close', refresh: 'Refresh',
         read: 'Read at {t}', how: 'A .elf file goes to the downloads; there, "Run" (pressed twice) sends it to the console\'s payload loader.',
         eta: 'The homebrew enabler: cheats, patches, plugins and the toolbox menu', web: 'Homebrew launcher in the browser, with ported games (Doom, Diablo, Duke Nukem 3D...)',
         elf: 'ELF payload loader on port 9021', ftp: 'FTP server for the console\'s files', sh: 'Command line over the network (telnet)',
         klog: 'The system\'s log over the network', sdk: 'The SDK to make payloads', running: 'Running on the console · port {p}', there: 'Installed on the console', launch: 'Open the launcher', ftpUse: 'On a PC: ftp://{ip}:2121'},
    es: {hb: 'Homebrew', title: 'La escena homebrew', loading: 'Leyendo las versiones en GitHub…', failed: 'No se pudo leer GitHub (sin red o demasiadas peticiones: inténtalo dentro de una hora).',
         fresh: 'Nuevo', since: 'Nuevo desde la última vez', notes: 'Notas', page: 'Ver en GitHub', files: 'Archivos', close: 'Cerrar', refresh: 'Actualizar',
         read: 'Leído a las {t}', how: 'Un archivo .elf va a las descargas; allí, "Ejecutar" (pulsado dos veces) lo envía al cargador de payloads de la consola.',
         eta: 'El activador de homebrew: trucos, parches, plugins y el menú de herramientas', web: 'Lanzador de homebrew en el navegador, con juegos portados (Doom, Diablo, Duke Nukem 3D...)',
         elf: 'Cargador de payloads ELF en el puerto 9021', ftp: 'Servidor FTP para los archivos de la consola', sh: 'Línea de comandos por la red (telnet)',
         klog: 'El registro del sistema por la red', sdk: 'El SDK para crear payloads', running: 'Funcionando en la consola · puerto {p}', there: 'Instalado en la consola', launch: 'Abrir el lanzador', ftpUse: 'En un PC: ftp://{ip}:2121'},
    fr: {hb: 'Homebrew', title: 'La scène homebrew', loading: 'Lecture des versions sur GitHub…', failed: 'Impossible de lire GitHub (pas de réseau, ou trop de demandes : réessaie dans une heure).',
         fresh: 'Nouveau', since: 'Nouveau depuis la dernière fois', notes: 'Notes', page: 'Voir sur GitHub', files: 'Fichiers', close: 'Fermer', refresh: 'Actualiser',
         read: 'Lu à {t}', how: 'Un fichier .elf va dans les téléchargements ; là, « Exécuter » (appuyé deux fois) l\'envoie au chargeur de payloads de la console.',
         eta: 'L\'activateur de homebrew : astuces, patchs, plugins et le menu d\'outils', web: 'Lanceur de homebrew dans le navigateur, avec des jeux portés (Doom, Diablo, Duke Nukem 3D...)',
         elf: 'Chargeur de payloads ELF sur le port 9021', ftp: 'Serveur FTP pour les fichiers de la console', sh: 'Ligne de commande par le réseau (telnet)',
         klog: 'Le journal du système par le réseau', sdk: 'Le SDK pour créer des payloads', running: 'En marche sur la console · port {p}', there: 'Installé sur la console', launch: 'Ouvrir le lanceur', ftpUse: 'Sur un PC : ftp://{ip}:2121'},
    de: {hb: 'Homebrew', title: 'Die Homebrew-Szene', loading: 'Versionen auf GitHub werden gelesen…', failed: 'GitHub konnte nicht gelesen werden (kein Netz oder zu viele Anfragen: in einer Stunde erneut versuchen).',
         fresh: 'Neu', since: 'Neu seit dem letzten Mal', notes: 'Hinweise', page: 'Auf GitHub ansehen', files: 'Dateien', close: 'Schließen', refresh: 'Aktualisieren',
         read: 'Gelesen um {t}', how: 'Eine .elf-Datei kommt zu den Downloads; dort schickt „Ausführen“ (zweimal gedrückt) sie an den Payload-Loader der Konsole.',
         eta: 'Der Homebrew-Enabler: Cheats, Patches, Plugins und das Werkzeugmenü', web: 'Homebrew-Launcher im Browser, mit portierten Spielen (Doom, Diablo, Duke Nukem 3D...)',
         elf: 'ELF-Payload-Loader auf Port 9021', ftp: 'FTP-Server für die Dateien der Konsole', sh: 'Kommandozeile über das Netz (telnet)',
         klog: 'Das Systemprotokoll über das Netz', sdk: 'Das SDK zum Erstellen von Payloads', running: 'Läuft auf der Konsole · Port {p}', there: 'Auf der Konsole installiert', launch: 'Launcher öffnen', ftpUse: 'Am PC: ftp://{ip}:2121'},
    it: {hb: 'Homebrew', title: 'La scena homebrew', loading: 'Lettura delle versioni su GitHub…', failed: 'Impossibile leggere GitHub (senza rete o troppe richieste: riprova tra un\'ora).',
         fresh: 'Nuovo', since: 'Nuovo dall\'ultima volta', notes: 'Note', page: 'Vedi su GitHub', files: 'File', close: 'Chiudi', refresh: 'Aggiorna',
         read: 'Letto alle {t}', how: 'Un file .elf va nei download; lì, "Esegui" (premuto due volte) lo invia al caricatore di payload della console.',
         eta: 'L\'attivatore di homebrew: trucchi, patch, plugin e il menu degli strumenti', web: 'Launcher di homebrew nel browser, con giochi portati (Doom, Diablo, Duke Nukem 3D...)',
         elf: 'Caricatore di payload ELF sulla porta 9021', ftp: 'Server FTP per i file della console', sh: 'Riga di comando in rete (telnet)',
         klog: 'Il registro di sistema in rete', sdk: 'L\'SDK per creare payload', running: 'In esecuzione sulla console · porta {p}', there: 'Installato sulla console', launch: 'Apri il launcher', ftpUse: 'Su un PC: ftp://{ip}:2121'},
    nl: {hb: 'Homebrew', title: 'De homebrewscene', loading: 'Versies op GitHub lezen…', failed: 'GitHub kon niet worden gelezen (geen netwerk of te veel verzoeken: probeer het over een uur opnieuw).',
         fresh: 'Nieuw', since: 'Nieuw sinds de vorige keer', notes: 'Notities', page: 'Bekijken op GitHub', files: 'Bestanden', close: 'Sluiten', refresh: 'Vernieuwen',
         read: 'Gelezen om {t}', how: 'Een .elf-bestand gaat naar de downloads; daar stuurt "Uitvoeren" (twee keer drukken) het naar de payloadlader van de console.',
         eta: 'De homebrew-enabler: cheats, patches, plugins en het gereedschapsmenu', web: 'Homebrew-launcher in de browser, met geporte games (Doom, Diablo, Duke Nukem 3D...)',
         elf: 'ELF-payloadlader op poort 9021', ftp: 'FTP-server voor de bestanden van de console', sh: 'Opdrachtregel via het netwerk (telnet)',
         klog: 'Het systeemlogboek via het netwerk', sdk: 'De SDK om payloads te maken', running: 'Draait op de console · poort {p}', there: 'Geïnstalleerd op de console', launch: 'Launcher openen', ftpUse: 'Op een pc: ftp://{ip}:2121'},
  };
  const t = TEXTS[String(data.lang || 'pt')] || TEXTS.en;
  // (2026-11-16) the port each tool listens on when it runs: the app says which answer (main.cpp services_tell())
  const PORTS = {'ps5-payload-dev/elfldr': 9021, 'ps5-payload-dev/websrv': 8080, 'ps5-payload-dev/ftpsrv': 2121,
                 'ps5-payload-dev/shsrv': 2323, 'ps5-payload-dev/klogsrv': 3232};
  let services = null, hbRepaint = null;
  window.bps5Services = s => {
    services = s && typeof s === 'object' ? s : null;
    if (hbRepaint)
      hbRepaint();
  };
  const REPOS = [
    ['etaHEN/etaHEN', 'etaHEN', 'eta'],
    ['ps5-payload-dev/websrv', 'websrv', 'web'],
    ['ps5-payload-dev/elfldr', 'elfldr', 'elf'],
    ['ps5-payload-dev/ftpsrv', 'ftpsrv', 'ftp'],
    ['ps5-payload-dev/shsrv', 'shsrv', 'sh'],
    ['ps5-payload-dev/klogsrv', 'klogsrv', 'klog'],
    ['ps5-payload-dev/sdk', 'PS5 Payload SDK', 'sdk'],
  ];
  const KEY = 'bps5.homebrew', FRESH_MS = 3600000, NEW_DAYS = 14;
  const load = () => { try { return JSON.parse(localStorage.getItem(KEY) || 'null') || {}; } catch (e) { return {}; } };
  const keep = v => { try { localStorage.setItem(KEY, JSON.stringify(v)); } catch (e) { /* no storage here */ } };
  let memory = {};
  // A release's notes as plain text: the markdown's marks taken out, a few lines.
  const plain = md => String(md || '').replace(/\r/g, '').replace(/!\[[^\]]*\]\([^)]*\)/g, '').replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/^#+\s*/gm, '').replace(/[*_`]+/g, '').replace(/\n{3,}/g, '\n\n').trim().slice(0, 900);
  const take = r => ({tag: String(r.tag_name || ''), name: String(r.name || ''), date: String(r.published_at || ''), url: String(r.html_url || ''),
    body: plain(r.body), files: (Array.isArray(r.assets) ? r.assets : []).slice(0, 30).map(a => ({n: String(a.name || ''), u: String(a.browser_download_url || ''), s: Number(a.size) || 0}))
      .filter(a => a.n && /^https:\/\/github\.com\//.test(a.u))});
  const read = async force => {
    const saved = Object.assign({}, memory, load());
    if (!force && saved.at && Date.now() - saved.at < FRESH_MS && saved.list)
      return saved;
    const list = {};
    let got = 0;
    await Promise.all(REPOS.map(async ([repo]) => {
      try {
        const r = await fetch('https://api.github.com/repos/' + repo + '/releases/latest', {headers: {Accept: 'application/vnd.github+json'}});
        if (r.ok) {
          list[repo] = take(await r.json());
          got++;
        }
      } catch (e) { /* this one is left out */ }
    }));
    if (!got)
      return saved.list ? saved : null;
    const now = Object.assign({}, saved, {at: Date.now(), list: Object.assign({}, saved.list || {}, list)});
    memory = now;
    keep(now);
    return now;
  };
  const size = n => (n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB');
  const day = iso => { const d = new Date(iso); return isNaN(d.getTime()) ? '' : d.toLocaleDateString(undefined, {day: '2-digit', month: '2-digit', year: 'numeric'}); };
  const chip = el('a', 'g-chip p-homebrew', t.hb);
  chip.href = '#';
  row.append(chip);
  chip.addEventListener('click', async ev => {
    ev.preventDefault();
    const old = document.getElementById('hb-panel');
    if (old)
      old.remove();
    const panel = el('div', '');
    panel.id = 'hb-panel';
    const card = el('div', 'hb-card');
    const head = el('div', 'hb-head');
    const close = el('a', 'g-chip', t.close);
    close.href = '#';
    close.addEventListener('click', e => { e.preventDefault(); panel.remove(); });
    panel.addEventListener('click', e => { if (e.target === panel) panel.remove(); });
    const again = el('a', 'g-chip', t.refresh);
    again.href = '#';
    const when = el('span', 'hb-when', '');
    const right = el('div', 'hb-right');
    right.append(when, again, close);
    head.append(el('h3', '', t.title), right);
    const body = el('div', 'hb-body');
    card.append(head, el('div', 'hb-how', t.how), body);
    panel.append(card);
    document.body.append(panel);
    close.focus();
    const seen = (load().seen) || memory.seen || {};
    // What runs on the console now, on the tools shown: a tool that listens (the app's answer, bps5Services()),
    // etaHEN by its folder (data.etahen); websrv's own homebrew launcher a press away; ftpsrv's address for a PC.
    // Put again on the same rows when the app answers, without reading GitHub again.
    const marks = () => {
      for (const item of body.querySelectorAll('.hb-item')) {
        for (const old of item.querySelectorAll('.hb-on, .hb-go, .hb-use'))
          old.remove();
        const repo = item.dataset.repo, port = PORTS[repo];
        const on = port ? !!(services && services.ports && Number(services.ports[port])) : repo === 'etaHEN/etaHEN' && !!Number(data.etahen);
        if (!on)
          continue;
        item.querySelector('.hb-top').append(el('span', 'hb-on', port ? t.running.replace('{p}', String(port)) : t.there));
        if (port === 8080) {
          const go = el('a', 'g-chip hb-go', t.launch);
          go.href = 'http://127.0.0.1:8080/';
          item.querySelector('.hb-links').prepend(go);
        }
        if (port === 2121 && services.ip)
          item.querySelector('.hb-what').after(el('div', 'hb-use', t.ftpUse.replace('{ip}', services.ip)));
      }
    };
    hbRepaint = () => { if (panel.isConnected) marks(); };
    const show = async force => {
      body.textContent = '';
      body.append(el('div', 'hb-none', t.loading));
      const got = await read(force);
      if (!panel.isConnected)
        return;
      body.textContent = '';
      if (!got || !got.list) {
        body.append(el('div', 'hb-none', t.failed));
        return;
      }
      const at = new Date(got.at);
      when.textContent = t.read.replace('{t}', at.toLocaleTimeString(undefined, {hour: '2-digit', minute: '2-digit'}));
      const nextSeen = {};
      for (const [repo, label, what] of REPOS) {
        const r = got.list[repo];
        if (!r || !r.tag)
          continue;
        nextSeen[repo] = r.tag;
        const age = (Date.now() - new Date(r.date).getTime()) / 86400000;
        const isNew = seen[repo] && seen[repo] !== r.tag ? t.since : age >= 0 && age <= NEW_DAYS ? t.fresh : '';
        const item = el('div', 'hb-item');
        const top = el('div', 'hb-top');
        const name = el('span', 'hb-name', label);
        name.setAttribute('translate', 'no');
        top.append(name, el('span', 'hb-tag', r.tag), el('span', 'hb-date', day(r.date)));
        if (isNew)
          top.append(el('span', 'hb-new', isNew));
        item.dataset.repo = repo;
        item.append(top, el('div', 'hb-what', t[what] || ''));
        const links = el('div', 'hb-links');
        const page = el('a', 'g-chip', t.page);
        page.href = r.url || 'https://github.com/' + repo + '/releases';
        links.append(page);
        if (r.body) {
          const notes = el('a', 'g-chip', t.notes);
          notes.href = '#';
          const text = el('div', 'hb-notes', r.body);
          text.style.display = 'none';
          text.setAttribute('translate', 'no');
          notes.addEventListener('click', e => { e.preventDefault(); text.style.display = text.style.display ? '' : 'none'; });
          links.append(notes);
          item.append(links, text);
        } else {
          item.append(links);
        }
        // its files: the payloads first (a .elf or .bin), then the others
        const files = r.files.slice().sort((a, b) => (/\.(elf|bin)$/i.test(b.n) ? 1 : 0) - (/\.(elf|bin)$/i.test(a.n) ? 1 : 0));
        if (files.length) {
          const box = el('div', 'hb-files');
          box.append(el('span', 'hb-fl', t.files));
          for (const f of files.slice(0, 12)) {
            const a = el('a', 'hb-file' + (/\.elf$/i.test(f.n) ? ' elf' : ''), f.n + ' \u00b7 ' + size(f.s));
            a.href = f.u;
            a.setAttribute('translate', 'no');
            box.append(a);
          }
          item.append(box);
        }
        body.append(item);
      }
      marks();
      const v = Object.assign({}, load(), memory, {seen: nextSeen});
      memory = v;
      keep(v);
    };
    again.addEventListener('click', e => { e.preventDefault(); location.href = 'browserps5:services'; show(true); });
    show(false);
    location.href = 'browserps5:services';
  });
})();
