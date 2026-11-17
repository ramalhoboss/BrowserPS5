// BrowserPS5's languages (docs/13-idioma.md), put in every one of the
// browser's own pages by include.awk. The pages are written in Portuguese,
// the browser's first language. setLang('en') puts a page in English:
//   - every text of the page, and the title, placeholder and aria-label of
//     every element, found in LANG_EN (exactly, or by one of the patterns,
//     whose %s stands for any text and %d for a number) is swapped, now and
//     whenever the page's scripts (or the app, as with the bar's messages)
//     write one: a MutationObserver, so the page's own code stays as it is;
//   - an element with data-en gets that as its content (static HTML of our
//     own, for a sentence split by elements);
//   - nothing inside translate="no" (what the user types, the sites' names);
//   - T(text, ...args) gives the English of a text the scripts put together
//     (each %s or %d in it is the next of args; "context|text" picks one of
//     two meanings of the same Portuguese), langDay() a date, and LANG says
//     which language a page is in.
// setLang('pt') puts the Portuguese back. The app sends setLang() to the
// panels (main.cpp, lang_apply()) and gives it to its pages in their data;
// in Portuguese, nothing here runs at all.
var LANG = 'pt';

const LANG_EN = {
  // The bar, the menu, the radial menus, the tabs
  'Voltar': 'Back',
  'Avançar': 'Forward',
  'Recarregar': 'Reload',
  'Início': 'Home',
  'Pesquisar ou escrever um endereço': 'Search or type an address',
  'Guardar este ficheiro': 'Save this file',
  'Guardar': 'Save',
  'Repor o zoom': 'Reset zoom',
  'Guardar nos favoritos': 'Add to favorites',
  'Transferências': 'Downloads',
  'Separadores': 'Tabs',
  'Extensões': 'Extensions',
  'Menu': 'Menu',
  'Os favoritos aparecem aqui. Carrega na estrela do endereço para guardar uma página.':
    'Favorites show up here. Press the star in the address to keep a page.',
  'Todos os favoritos': 'All favorites',
  'Favoritos': 'Favorites',
  'Histórico': 'History',
  'Definições': 'Settings',
  'Telemóvel': 'Phone',
  'Adicionar aos favoritos': 'Add to favorites',
  'Tirar dos favoritos': 'Remove from favorites',
  'Ecrã inteiro': 'Full screen',
  'Sair do ecrã inteiro': 'Leave full screen',
  'Abrir ao lado do jogo': 'Open next to the game',
  'Bloquear anúncios': 'Block ads',
  'Modo escuro nos sites': 'Dark mode on sites',
  'Versão para computador': 'Desktop version',
  'Limpar dados de navegação': 'Clear browsing data',
  'Sair do BrowserPS5': 'Quit BrowserPS5',
  'Tema': 'Theme',
  'Zoom': 'Zoom',
  'Maior': 'Bigger',
  'Mais pequeno': 'Smaller',
  'Meia-noite': 'Midnight',
  'Grafite': 'Graphite',
  'Claro': 'Light',
  'Oceano': 'Ocean',
  'Ametista': 'Amethyst',
  'Rubi': 'Ruby',
  'Floresta': 'Forest',
  'Champanhe': 'Champagne',
  'Néon': 'Neon',
  'Estação (automático)': 'Season (automatic)',
  'Inverno': 'Winter',
  'Primavera': 'Spring',
  'Verão': 'Summer',
  'Outono': 'Autumn',
  'Natal': 'Christmas',
  'Ir para': 'Go to',
  'Página': 'Page',
  'Pesquisar': 'Search',
  'Ampliar': 'Zoom in',
  'Reduzir': 'Zoom out',
  'Adicionar favorito': 'Add favorite',
  'Tirar favorito': 'Remove favorite',
  'Anúncios aqui': 'Ads here',
  'Modo escuro': 'Dark mode',
  'Ligado': 'On',
  'Desligado': 'Off',
  'Bloqueados': 'Blocked',
  'Permitidos': 'Allowed',
  'Agora não': 'Not now',
  'Aponta com o manípulo ou o D-pad': 'Point with the stick or the D-pad',
  'Solta para escolher': 'Release to choose',
  '○ fechar': '○ close',
  'Novo separador': 'New tab',
  'Reabrir o separador fechado': 'Reopen the closed tab',
  'Fechar separador': 'Close tab',
  'Abrir': 'Open',
  'Atual': 'Current',
  'Página de início': 'Start page',
  'R1 em qualquer altura': 'R1 at any time',
  'Sem imagem ainda': 'No picture yet',

  // The keyboard
  'Endereço': 'Address',
  'Endereço ou pesquisa': 'Address or search',
  'Pesquisa': 'Search',
  'Texto': 'Text',
  'Número': 'Number',
  'Telefone': 'Phone',
  'Palavra-passe': 'Password',
  'Ir': 'Go',
  'Seguinte': 'Next',
  'Enviar': 'Send',
  'espaço': 'space',
  'fechar': 'close',
  'mostrar': 'show',
  'ocultar': 'hide',
  '↵ linha': '↵ line',
  '✥ escolher · ✕ escrever · ○ apagar': '✥ choose · ✕ type · ○ delete',
  '△ fechar · R1 confirmar': '△ close · R1 confirm',

  // The dialog
  'Guardar como': 'Save as',
  'Nome': 'Name',
  'Cancelar': 'Cancel',
  'A transferir…': 'Downloading…',
  'Escolher uma pasta': 'Choose a folder',
  'Escolher esta pasta': 'Choose this folder',
  'Nova pasta': 'New folder',
  'Pasta acima': 'Folder up',
  'Escolher um ficheiro': 'Choose a file',
  '%s diz': '%s says',
  'A página diz': 'The page says',
  'Executar o payload': 'Run the payload',
  'Executar este payload na consola? Carrega outra vez para confirmar': 'Run this payload on the console? Press again to confirm',
  'Escolher ficheiros': 'Choose files',
  'Esta pasta está vazia': 'This folder is empty',
  'Para enviar à página que o pediu': 'To send to the page that asked for it',
  'A página aceita: %s': 'The page takes: %s',
  '%s ficheiro escolhido': '%s file chosen',
  '%s ficheiros escolhidos': '%s files chosen',
  'Pacotes (PKG)': 'Packages (PKG)',
  'Multimédia': 'Media',
  'Armazenamento interno': 'Internal storage',
  'Pasta dos pacotes (PKG)': 'Packages folder (PKG)',
  'Pasta da multimédia': 'Media folder',
  'Pasta das transferências': 'Downloads folder',
  'Os ficheiros .pkg transferidos ficam aqui.': 'Downloaded .pkg files go here.',
  'Vídeos, música e imagens transferidos ficam aqui.': 'Downloaded videos, music and pictures go here.',
  'Os outros ficheiros transferidos ficam aqui.': 'Other downloaded files go here.',
  'Guardar a palavra-passe?': 'Save the password?',
  'Atualizar a palavra-passe?': 'Update the password?',
  'Atualizar': 'Update',
  'Nunca neste site': 'Never on this site',
  'Sem nome de utilizador': 'No user name',
  'Fica só nesta consola, no ficheiro do browser. Da próxima vez, no teclado deste site aparece «Entrar como …» e o browser preenche o nome e a palavra-passe por ti.':
    'It stays on this console only, in the browser\'s file. Next time, the keyboard on this site shows “Sign in as …” and the browser fills in the name and the password for you.',
  'Este site não usa ligação segura (https): a palavra-passe viajou sem cifra.':
    'This site doesn\'t use a secure connection (https): the password travelled unencrypted.',
  'de %s': 'from %s',
  'em %s': 'on %s',
  '%s% · %s de %s': '%s% · %s of %s',
  'Transferido · %s': 'Downloaded · %s',

  // The start page
  'Bem-vindo ao BrowserPS5': 'Welcome to BrowserPS5',
  'Escolhe um estilo para começar. Mudas tudo quando quiseres nas definições.':
    'Pick a style to start with. You can change everything in the settings whenever you like.',
  'Escolher depois': 'Choose later',
  'Cinema': 'Cinema',
  'Ciborgue': 'Cyborg',
  'Escuro e dourado, sem sons da interface. Para ver filmes e séries no sofá, com o mínimo à frente.':
    'Dark and golden, with no interface sounds. For films and series from the sofa, with as little as possible in the way.',
  'Néon azul e magenta, com sons a cada salto e o íman do cursor. Para quem explora a web a fundo.':
    'Blue and magenta neon, with a sound on every jump and the cursor magnet. For exploring the web in depth.',
  'Bom dia': 'Good morning',
  'Boa tarde': 'Good afternoon',
  'Boa noite': 'Good evening',
  'Escolher o motor de pesquisa': 'Choose the search engine',
  'Visitados recentemente': 'Recently visited',
  'Guardar páginas em cache no disco': 'Keep pages cached on the disk',
  'Os sites já visitados abrem mais depressa, até 1 GB na consola. Conta a partir da próxima vez que o browser abrir':
    'Sites you visited before open faster, up to 1 GB on the console. Takes effect the next time the browser opens',
  'Desempenho': 'Performance',
  'Páginas dentro da app do browser': 'Pages inside the browser app',
  'Mais núcleos para as páginas: abrem até 40% mais depressa. Desliga se alguma página falhar. Conta a partir da próxima vez que o browser abrir':
    'More cores for the pages: they open up to 40% faster. Turn it off if a page fails. Takes effect the next time the browser opens',
  'Reabrir os separadores da última vez': 'Reopen the tabs from last time',
  'Reabrir a página da última vez': 'Reopen the page from last time',
  'Editar': 'Edit',
  'Concluído': 'Done',
  'Apagar todos': 'Delete all',
  'Apagar': 'Delete',
  'Carrega outra vez para apagar todos': 'Press again to delete all',
  'Abre uma página e carrega na estrela do endereço para a guardar aqui':
    'Open a page and press the star in the address to keep it here',
  'escrever um endereço': 'type an address',
  'menus rápidos': 'quick menus',
  'separadores (touch pad)': 'tabs (touch pad)',
  'saltar entre elementos': 'jump between elements',
  'menu e definições': 'menu and settings',
  'abrir': 'open',

  // The settings: the sections
  'Geral': 'General',
  'Aspeto': 'Appearance',
  'Comando': 'Controller',
  'Conteúdo dos sites': 'Site content',
  'Privacidade e segurança': 'Privacy and security',
  'Sistema': 'System',
  'Acerca': 'About',
  'TorBox': 'TorBox',
  'As alterações ficam guardadas logo.': 'Changes are saved right away.',
  '○ voltar à página anterior': '○ back to the previous page',
  'Idioma · Language': 'Language · Idioma',

  // General
  'Por onde o browser começa e onde pesquisa.': 'Where the browser starts and where it searches.',
  'Ao iniciar': 'On start',
  'Abrir a página inicial': 'Open the home page',
  'Continuar onde fiquei': 'Continue where I left off',
  'Os separadores que estavam abertos': 'The tabs that were open',
  'Sempre a mesma, todas as vezes': 'The same one, every time',
  'Página inicial': 'Home page',
  'Início do BrowserPS5': 'BrowserPS5 home',
  'Favoritos, pesquisa e sites recentes': 'Favorites, search and recent sites',
  'Um endereço': 'An address',
  'Escolhe abaixo': 'Choose below',
  'Escreve-o abaixo': 'Type it below',
  'Motor de pesquisa': 'Search engine',
  'Motor': 'Engine',
  'https://www.exemplo.pt/': 'https://www.example.com/',
  'O de origem. Pode pedir uma verificação': 'The original. It may ask for a check',
  'Não guarda o que pesquisas': 'Doesn\'t keep what you search for',
  'Da Microsoft': 'From Microsoft',
  'Planta árvores': 'Plants trees',
  'Resultados do Google, em privado': 'Google results, privately',
  'Índice próprio, privado': 'Its own index, private',
  'Barra': 'Bar',
  'Mostrar os favoritos na barra': 'Show favorites in the bar',
  'A fila de favoritos debaixo do endereço': 'The row of favorites under the address',
  'Zoom das páginas': 'Page zoom',

  // Appearance
  'O tema muda a barra, o menu, o teclado e as páginas do browser.':
    'The theme changes the bar, the menu, the keyboard and the browser\'s pages.',
  'Sons da interface': 'Interface sounds',
  'Um toque discreto ao saltar entre elementos e ao abrir menus':
    'A discreet sound when jumping between elements and opening menus',
  'Íman do cursor': 'Cursor magnet',
  'Quando o manípulo pára, o cursor encaixa no link ou botão mais perto':
    'When the stick stops, the cursor snaps to the nearest link or button',
  'Modo cinema': 'Cinema mode',
  'Num vídeo em ecrã inteiro: o cursor desaparece quando o comando descansa, sem sons nem vibração, a luz do comando baixa':
    'In a full-screen video: the cursor hides while the controller rests, with no sounds or vibration, and the controller\'s light dims',

  // Controller
  'Como o DualSense anda pelas páginas.': 'How the DualSense moves around pages.',
  'Botões de direção': 'Direction buttons',
  'Saltar entre elementos': 'Jump between elements',
  'Links, botões e caixas de texto, com destaque': 'Links, buttons and text boxes, highlighted',
  'Deslocar a página': 'Scroll the page',
  'Como o manípulo direito': 'Like the right stick',
  'Velocidade do cursor': 'Cursor speed',
  'Velocidade do deslocamento': 'Scroll speed',
  'Lenta': 'Slow',
  'Calma': 'Relaxed',
  'Normal': 'Normal',
  'Rápida': 'Fast',
  'Muito rápida': 'Very fast',
  'Vibração': 'Vibration',
  'O comando vibra de leve ao saltar entre elementos, abrir menus e escolher':
    'The controller vibrates lightly when jumping between elements, opening menus and choosing',
  'Gatilhos adaptativos': 'Adaptive triggers',
  'L2 e R2 dão um clique a meio, como botões, para os menus radiais':
    'L2 and R2 click halfway, like buttons, for the radial menus',
  'Barra de luz do tema': 'Theme light bar',
  'Luz néon do cursor': 'Cursor neon light',
  'Teclado da PS5': 'PS5 keyboard',
  'Procurar': 'Find',
  'Leitura': 'Reader',
  'Traduzir': 'Translate',
  'Código QR': 'QR code',
  'Privado': 'Private',
  'Abrir no telemóvel': 'Open on the phone',
  'Aponta a câmara do telemóvel a este código': 'Point the phone\'s camera at this code',
  'Escreve com o teclado do sistema em vez do teclado do browser. A página pausa enquanto ele está aberto': 'Type with the system keyboard instead of the browser\'s own. The page pauses while it is open',
  'Uma bola de luz na cor do tema segue o cursor, com um pequeno atraso': 'A ball of light in the theme colour follows the cursor, a little behind',
  'A luz do comando acende na cor de destaque do tema': 'The controller\'s light glows in the theme\'s accent colour',
  'Comando nos jogos da web': 'Controller in web games',
  'Num site com jogos, um clique no touch pad dá o comando ao jogo; outro devolve-o ao cursor':
    'On a site with games, a click on the touch pad gives the controller to the game; another gives it back to the cursor',
  'Botões': 'Buttons',
  'Mover o cursor': 'Move the cursor',
  'Saltar para o elemento seguinte': 'Jump to the next element',
  'Clicar': 'Click',
  'Escrever um endereço': 'Type an address',
  'Voltar · fechar o menu': 'Back · close the menu',
  'Separadores · num jogo, o comando': 'Tabs · in a game, the controller',
  'Menu (ecrã inteiro, sair)': 'Menu (full screen, quit)',
  'Consola de programador (se ligada)': 'Developer console (if on)',

  // Site content
  'O que as páginas podem fazer. Vale para a próxima página que abrir.':
    'What pages are allowed to do. Applies to the next page that opens.',
  'JavaScript': 'JavaScript',
  'Quase todos os sites precisam dele': 'Almost every site needs it',
  'Imagens': 'Images',
  'Desligado, as páginas abrem mais depressa': 'Off, pages open faster',
  'Janelas pop-up': 'Pop-up windows',
  'Os sites podem abrir janelas por si': 'Sites can open windows by themselves',
  'Reprodução automática': 'Autoplay',
  'Vídeos e som começam sem carregar em nada': 'Videos and sound start without pressing anything',
  'Desligado, os sites mostram a versão para telemóvel': 'Off, sites show their mobile version',
  'Ecrã inteiro nos vídeos': 'Full screen in videos',
  'O botão de ecrã inteiro dos sites passa a funcionar, sem a barra (○ sai)':
    'Sites\' full-screen button works, without the bar (○ leaves)',
  'Escurece os sites claros, mantendo as fotografias e os vídeos':
    'Darkens light sites, keeping the photos and videos as they are',
  'Consola de programador': 'Developer console',
  'R3 (carregar no manípulo direito) mostra as mensagens e os erros da página e corre JavaScript':
    'R3 (pressing the right stick) shows the page\'s messages and errors and runs JavaScript',

  // Privacy and security
  'O que fica guardado na consola, e o que os sites podem guardar.':
    'What is kept on the console, and what sites may keep.',
  'Proteção': 'Protection',
  'Bloqueador de anúncios': 'Ad blocker',
  'Esconde anúncios, não carrega os scripts, quadros e imagens das redes de publicidade e dos rastreadores':
    'Hides ads and doesn\'t load the scripts, frames and images of ad networks and trackers',
  'Esconde anúncios e não abre os quadros das redes de publicidade':
    'Hides ads and doesn\'t open ad networks\' frames',
  'Permitir cookies de terceiros': 'Allow third-party cookies',
  'Desligado: só o site que está aberto guarda cookies': 'Off: only the open site keeps cookies',
  'Sites com anúncios permitidos': 'Sites with ads allowed',
  'Nenhum. Num site, carrega em R2 e escolhe "Anúncios aqui" para os permitir só nesse site.':
    'None. On a site, press R2 and choose "Ads here" to allow them on that site only.',
  'Voltar a bloquear': 'Block again',
  'Palavras-passe': 'Passwords',
  'Oferecer para guardar palavras-passe': 'Offer to save passwords',
  'Depois de entrares num site, o browser pergunta se a guarda; nesse site, o teclado mostra «Entrar como …». Ficam só nesta consola':
    'After you sign in to a site, the browser asks whether to keep the password; on that site, the keyboard shows “Sign in as …”. They stay on this console only',
  'Ainda não há palavras-passe guardadas.': 'No saved passwords yet.',
  'Nunca guardar neste site': 'Never save on this site',
  'Limpar dados': 'Clear data',
  'Limpar': 'Clear',
  'Cookies e dados dos sites': 'Cookies and site data',
  'Sai das contas na maioria dos sites': 'Signs you out of most sites',
  'Imagens e ficheiros em cache': 'Cached images and files',
  'As páginas abrem um pouco mais devagar da próxima vez': 'Pages open a little slower next time',
  'Dados limpos': 'Data cleared',
  'Histórico limpo': 'History cleared',

  // Favorites, history, downloads
  'As páginas guardadas com a estrela do endereço. O ✕ de cada uma apaga-a.':
    'The pages kept with the star in the address. The ✕ on each one deletes it.',
  'Ainda não há favoritos. Abre uma página e carrega na estrela do endereço.':
    'No favorites yet. Open a page and press the star in the address.',
  'Favoritos apagados': 'Favorites deleted',
  'As páginas abertas nesta consola, das mais recentes para as mais antigas.':
    'The pages opened on this console, newest first.',
  'Procurar no histórico': 'Search history',
  'Nada encontrado.': 'Nothing found.',
  'O histórico está vazio.': 'History is empty.',
  'Hoje': 'Today',
  'Ontem': 'Yesterday',
  'Os ficheiros que os sites dão a guardar. Cada tipo tem a sua pasta, e o browser pergunta sempre onde guardar, como o Windows.':
    'The files sites give you to save. Each type has its own folder, and the browser always asks where to save, like Windows.',
  'Onde guardar': 'Where to save',
  'Perguntar sempre onde guardar': 'Always ask where to save',
  'Como no Windows. Desligado, cada ficheiro vai logo para a pasta do seu tipo':
    'As on Windows. Off, each file goes straight to the folder for its type',
  'Pasta dos pacotes (.pkg)': 'Packages folder (.pkg)',
  'Pasta dos vídeos, músicas e imagens': 'Videos, music and pictures folder',
  'Ainda não houve transferências.': 'No downloads yet.',
  '%s% de %s': '%s% of %s',
  'A transferir': 'Downloading',
  'À espera da pasta': 'Waiting for the folder',
  'Concluída': 'Done',
  'Falhou': 'Failed',
  'Cancelada': 'Cancelled',
  'parada': 'stalled',
  'faltam %s': '%s left',
  'Voltar a tentar': 'Try again',
  'Extrair': 'Extract',
  'Transferências da consola': 'The console\'s downloads',
  'Ficheiros da consola': 'The console\'s files',
  'Vê o que está nas pastas das transferências e nas pens USB, e traz para o telemóvel.':
    'See what is in the download folders and on USB drives, and bring it to the phone.',
  'Locais': 'Places',
  'Nenhuma pasta disponível agora.': 'No folder available right now.',
  'Esta pasta está vazia.': 'This folder is empty.',
  'Guardar no telemóvel': 'Save to phone',
  'Ver': 'View',
  'Fechar': 'Close',
  'Apagar esta pasta': 'Delete this folder',
  'Apagado': 'Deleted',
  'A pasta não está vazia': 'The folder is not empty',
  'Não foi possível apagar': 'Could not delete',
  'A guardar no telemóvel': 'Saving to the phone',
  'Esta pasta já não está disponível': 'This folder is no longer available',
  'Não foi possível abrir o ficheiro': 'Could not open the file',
  'Nome que o telemóvel não consegue pedir': 'A name the phone cannot ask for',
  'A extrair · %s%': 'Extracting · %s%',
  'Não foi possível extrair': 'Could not extract',
  'A extrair o ZIP': 'Extracting the ZIP',
  'Já está a extrair um ZIP': 'A ZIP is being extracted already',
  'Não foi possível criar a pasta para o ZIP': 'The folder for the ZIP could not be made',
  'Este ZIP está estragado ou tem várias partes': 'This ZIP is broken or in several parts',
  'Não há espaço para extrair este ZIP': 'There is no room to extract this ZIP',
  'Não foi possível escrever os ficheiros do ZIP': 'The ZIP\'s files could not be written',
  'Não foi possível abrir o ZIP': 'The ZIP could not be opened',
  'O ficheiro já não está lá': 'The file is not there any more',
  'A transferir de novo': 'Downloading again',
  'Torrents': 'Torrents',
  'Abrir os torrents': 'Open the torrents',
  'Os links magnet e os ficheiros .torrent vão para o Transmission, que continua com o browser fechado. Ficam em /data/downloads/torrents':
    'Magnet links and .torrent files go to Transmission, which keeps going with the browser closed. They end up in /data/downloads/torrents',

  // Extensions
  'Instaladas': 'Installed',
  'Do BrowserPS5': 'BrowserPS5\'s own',
  'Nenhuma extensão instalada.': 'No extensions installed.',
  'Abre um script e carrega em «Instalar este script».': 'Open a script and press “Install this script”.',
  'Procurar extensões no Greasy Fork': 'Find extensions on Greasy Fork',
  'Instalar extensão?': 'Install extension?',
  'Instalar': 'Install',
  'A instalar…': 'Installing…',
  'Remover': 'Remove',
  'Extensão removida': 'Extension removed',
  'Guardado': 'Saved',
  'O bloqueador não faz nada aqui nem nos endereços dentro dele':
    'The blocker does nothing here or on the addresses inside it',
  'Ligar': 'Turn on',
  'Desligar': 'Turn off',
  'todos os sites': 'all sites',

  // Phone (the settings' section)
  'Usa o telemóvel como teclado e comando, abre nele um link para ver na TV ou envia vídeos e ficheiros para a consola. Basta estar na mesma rede.':
    'Use the phone as a keyboard and controller, open a link on it to watch on the TV, or send videos and files to the console. It just needs the same network.',
  'Permitir o telemóvel': 'Allow the phone',
  'Só telemóveis da rede de casa, e só com o código da TV': 'Only phones on the home network, and only with the code from the TV',
  'O telemóvel está desligado': 'The phone is off',
  'Liga-o abaixo para ver o código.': 'Turn it on below to see the code.',
  'A consola não tem endereço na rede. Liga-a ao router e volta aqui.':
    'The console has no network address. Connect it to the router and come back here.',
  'Aponta a câmara do telemóvel ao código, ou abre': 'Point the phone\'s camera at the code, or open',
  'Escreve este código no telemóvel:': 'Type this code on the phone:',
  'Ligações': 'Connections',
  'Nenhum telemóvel ligado': 'No phone connected',
  '1 telemóvel ligado': '1 phone connected',
  'O browser esquece-os quando fecha': 'The browser forgets them when it closes',
  'Esquecer todos': 'Forget all',
  'Os telemóveis foram esquecidos': 'The phones were forgotten',
  'Telemóvel ligado. O código mudou para o próximo.': 'Phone connected. The code changed to the next one.',

  // System
  'A consola agora: temperatura, memória, espaço livre e rede. Atualiza sozinho a cada 2 segundos.':
    'The console right now: temperature, memory, free space and network. Updates by itself every 2 seconds.',
  'Processador': 'Processor',
  'Chip principal': 'Main chip',
  'Temperatura indisponível': 'Temperature unavailable',
  'Memória livre': 'Free memory',
  'Ligada há': 'On for',
  'Endereço na rede': 'Network address',
  'Sem rede': 'No network',
  'Consola': 'Console',
  'Firmware': 'Firmware',
  'Desconhecido': 'Unknown',
  'Memória do sistema': 'System memory',
  'Indisponível': 'Unavailable',
  'Temperaturas': 'Temperatures',
  'processador %s °C': 'processor %s °C',
  'chip principal %s °C': 'main chip %s °C',
  'A consola não as dá a este browser': 'The console doesn\'t give them to this browser',
  'Armazenamento': 'Storage',
  'Nenhum disco a que o browser chegue': 'No drive the browser can reach',
  'Este browser': 'This browser',
  'Esta sessão': 'This session',
  'Aberto há': 'Open for',
  'Separadores abertos': 'Open tabs',
  '1 separador': '1 tab',
  'Anúncios bloqueados': 'Ads blocked',
  'Nenhum desde que abriu': 'None since it opened',
  '1 desde que abriu': '1 since it opened',
  'Transferências a decorrer': 'Downloads in progress',
  'Nenhuma': 'None',
  '1 transferência': '1 download',
  'Desenho e vídeo': 'Drawing and video',
  'Composição no GPU da consola (Mesa: zink e RADV), JavaScript com JIT':
    'Compositing on the console\'s GPU (Mesa: zink and RADV), JavaScript with JIT',
  '%s livres de %s': '%s free of %s',
  '%s livres': '%s free',
  'Definições da PS5': 'PS5 settings',
  'Rede': 'Network',
  'Wi-Fi, cabo e a ligação à internet': 'Wi-Fi, cable and the internet connection',
  'Ecrã e vídeo': 'Screen and video',
  'Resolução, 4K, HDR e frequência': 'Resolution, 4K, HDR and refresh rate',
  'Armazenamento da consola': 'Console storage',
  'O espaço da consola e dos discos': 'The space on the console and its drives',
  'Comandos': 'Controllers',
  'O DualSense: vibração, gatilhos e luz': 'The DualSense: vibration, triggers and light',
  'Aparece uma notificação: carrega em «Abrir» nela. O browser fica em pausa por trás e volta como estava.':
    'A notification shows up: press “Open” on it. The browser pauses behind it and comes back as it was.',
  'Carrega em «Abrir» na notificação para ir às definições da PS5':
    'Press “Open” in the notification to go to the PS5\'s settings',

  // TorBox
  'A tua conta TorBox: os torrents e os links de ficheiros são transferidos nos servidores do TorBox, e os ficheiros prontos vêm para a consola pelas Transferências do browser.':
    'Your TorBox account: torrents and file links are downloaded on TorBox\'s servers, and the finished files come to the console through the browser\'s Downloads.',
  'Conta': 'Account',
  'Sem chave': 'No key',
  'A chave da API está em torbox.app, Settings. Fica só nesta consola e nunca aparece nos registos':
    'The API key is on torbox.app, Settings. It stays on this console only and never appears in the logs',
  'Chave da API (podes colá-la a partir do telemóvel)': 'API key (you can paste it from the phone)',
  'Apagar a chave': 'Delete the key',
  'O browser esquece a conta': 'The browser forgets the account',
  'Mandar os torrents para o TorBox': 'Send torrents to TorBox',
  'Os links magnet e os ficheiros .torrent vão para o TorBox em vez do Transmission da consola':
    'Magnet links and .torrent files go to TorBox instead of the console\'s Transmission',
  'Adicionar': 'Add',
  'Um link magnet, ou o endereço de um ficheiro': 'A magnet link, or a file\'s address',
  'No TorBox': 'On TorBox',
  'Abrir torbox.app': 'Open torbox.app',
  'Guarda a chave da tua conta para ver o que lá tens.': 'Save your account\'s key to see what you have there.',
  'Ainda não há nada no TorBox.': 'Nothing on TorBox yet.',
  'O TorBox não respondeu.': 'TorBox didn\'t answer.',
  'O TorBox não é usado para o YouTube': 'TorBox isn\'t used for YouTube',
  'Primeiro a chave da conta': 'The account key first',
  'Pronto': 'Ready',
  'A transferir no TorBox': 'Downloading on TorBox',
  'Em pausa': 'Paused',
  'A ler o torrent': 'Reading the torrent',
  'A verificar': 'Checking',
  'Em fila': 'Queued',
  'Sem fontes': 'No sources',
  'Erro': 'Error',
  'À espera': 'Waiting',
  'Sem nome': 'No name',
  'ficheiro': 'file',
  'Trazer para a consola': 'Bring to the console',

  // About
  'Um browser a sério para a PS5, feito sobre o WebKit.': 'A real browser for the PS5, built on WebKit.',
  'Software livre · GPL-3.0': 'Free software · GPL-3.0',
  'Ícones e letra': 'Icons and font',
  'Fluent UI System Icons da Microsoft (licença MIT); a letra é a da própria consola':
    'Microsoft\'s Fluent UI System Icons (MIT licence); the font is the console\'s own',

  // The phone's page
  'BrowserPS5 · Telemóvel': 'BrowserPS5 · Phone',
  'no teu telemóvel': 'on your phone',
  'Ligar à PS5': 'Connect to the PS5',
  'Escreve o código de 6 algarismos que aparece na TV, em Definições → Telemóvel.':
    'Type the 6-digit code shown on the TV, in Settings → Phone.',
  'Código': 'Code',
  'O telemóvel e a consola têm de estar na mesma rede.': 'The phone and the console must be on the same network.',
  'A ligar…': 'Connecting…',
  'São 6 algarismos.': 'It\'s 6 digits.',
  'Código errado. Falta 1 tentativa.': 'Wrong code. 1 try left.',
  'Demasiadas tentativas. Abre outra vez Definições → Telemóvel na TV para ter um código novo.':
    'Too many tries. Open Settings → Phone on the TV again to get a new code.',
  'Esse código já não serve. Abre Definições → Telemóvel na TV para ver o atual.':
    'That code no longer works. Open Settings → Phone on the TV to see the current one.',
  'A consola já não conhece este telemóvel. Liga-o outra vez.': 'The console no longer knows this phone. Connect it again.',
  'Não chego à consola. Está ligada e na mesma rede?': 'Can\'t reach the console. Is it on and on the same network?',
  'phone|Ligado': 'Connected',
  'phone|Desligado': 'Disconnected',
  'Sem ligação': 'No connection',
  'Ligado à PS5': 'Connected to the PS5',
  'Ligação perdida': 'Connection lost',
  'Na TV agora': 'On the TV now',
  'Página do browser': 'Browser page',
  'Abrir na TV': 'Open on the TV',
  'Um endereço ou o que queres pesquisar. Abre num separador novo.':
    'An address or what you want to search for. Opens in a new tab.',
  'A abrir na TV': 'Opening on the TV',
  'A TV não aceitou': 'The TV didn\'t accept it',
  'Escrever na TV': 'Type on the TV',
  'Vai para a caixa de texto que está escolhida na TV. Bom para palavras-passe e textos compridos.':
    'Goes into the text box chosen on the TV. Good for passwords and long texts.',
  'Escreve aqui': 'Type here',
  'Carregar em Enter no fim': 'Press Enter at the end',
  'Escrito na TV': 'Typed on the TV',
  'Texto comprido demais': 'Text too long',
  'Não foi': 'Didn\'t work',
  'O que a página na TV faz a seguir.': 'What the page on the TV does next.',
  'Topo': 'Top',
  'Subir': 'Up',
  'Descer': 'Down',
  'Fundo': 'Bottom',
  'Enviar ficheiros para a consola': 'Send files to the console',
  'Vídeos, músicas, imagens ou outros ficheiros. Os .pkg vão para a pasta dos pacotes; nada é instalado.':
    'Videos, music, pictures or other files. .pkg files go to the packages folder; nothing is installed.',
  'Escolher ficheiros': 'Choose files',
  'Pelo tipo': 'By type',
  'Vídeos e música': 'Videos and music',
  'Pen USB': 'USB drive',
  'Ao lado do jogo': 'Next to the game',
  'Sem espaço': 'No space',
  'Sem pen USB': 'No USB drive',
  'Pasta indisponível': 'Folder unavailable',
  'Desligar este telemóvel': 'Disconnect this phone',
  'Novo': 'New',

  // The app's messages (main.cpp, torrent.c, torbox.c), in the bar
  'A abrir os torrents...': 'Opening the torrents...',
  'A chave do TorBox foi apagada': 'The TorBox key was deleted',
  'A consola é para as páginas da web': 'The console is for web pages',
  'A entregar ao TorBox...': 'Handing it to TorBox...',
  'A entregar o torrent ao Transmission...': 'Handing the torrent to Transmission...',
  'A pedir o ficheiro ao TorBox...': 'Asking TorBox for the file...',
  'A trazer o ficheiro do TorBox...': 'Bringing the file from TorBox...',
  'Aberto a partir do telemóvel': 'Opened from the phone',
  'Adicionado ao TorBox': 'Added to TorBox',
  'Anúncios bloqueados neste site': 'Ads blocked on this site',
  'Anúncios permitidos neste site': 'Ads allowed on this site',
  'Carrega em «Abrir» na notificação: a página abre no cartão do sistema, ao lado do jogo':
    'Press “Open” in the notification: the page opens in the system card, next to the game',
  'Chave do TorBox guardada': 'TorBox key saved',
  'Endereço longo demais para o cartão do sistema': 'Address too long for the system card',
  'Esta chave não parece do TorBox: copia-a de torbox.app, Settings, API':
    'This doesn\'t look like a TorBox key: copy it from torbox.app, Settings, API',
  'Este ficheiro .torrent não se lê': 'This .torrent file can\'t be read',
  'Este site usa o comando: clica no touch pad para jogar': 'This site uses the controller: click the touch pad to play',
  'Falta a chave do TorBox: põe-na em Definições, TorBox': 'The TorBox key is missing: put it in Settings, TorBox',
  'Falta o torrentd.elf: instala o pacote dos torrents': 'torrentd.elf is missing: install the torrents package',
  'Ficheiro recebido do telemóvel': 'File received from the phone',
  'Há sites demais na lista de exceções': 'Too many sites in the exceptions list',
  'Liga a consola em Definições → Conteúdo dos sites': 'Turn on the console in Settings → Site content',
  'Modo jogo: o comando é do jogo. Touch pad para sair': 'Game mode: the controller belongs to the game. Touch pad to leave',
  'Não deu para abrir os torrents': 'Couldn\'t open the torrents',
  'Não deu para guardar a palavra-passe': 'Couldn\'t save the password',
  'Não deu para pedir a lista ao TorBox': 'Couldn\'t ask TorBox for the list',
  'Não deu para pedir ao TorBox': 'Couldn\'t ask TorBox',
  'Não deu para pedir o ficheiro ao TorBox': 'Couldn\'t ask TorBox for the file',
  'Não deu para pedir o torrent': 'Couldn\'t ask for the torrent',
  'O TorBox não respondeu (rede?)': 'TorBox didn\'t answer (network?)',
  'O TorBox não é usado para isto (YouTube, ou não é um link)': 'TorBox isn\'t used for this (YouTube, or not a link)',
  'O TorBox pede para esperar: muitos pedidos seguidos': 'TorBox asks to wait: too many requests in a row',
  'O TorBox recusou a chave: vê-a em Definições, TorBox': 'TorBox refused the key: check it in Settings, TorBox',
  'O Transmission não respondeu': 'Transmission didn\'t answer',
  'O Transmission não respondeu ao pedido': 'Transmission didn\'t answer the request',
  'O carregador de payloads não recebeu o Transmission': 'The payload loader didn\'t take Transmission',
  'O carregador de payloads não respondeu': 'The payload loader didn\'t answer',
  'O comando voltou ao cursor': 'The controller is back on the cursor',
  'O sistema não mostrou a notificação': 'The system didn\'t show the notification',
  'Recarrega a página para a consola a ver': 'Reload the page for the console to see it',
  'Resposta estranha do Transmission': 'Odd answer from Transmission',
  'Sem memória': 'Out of memory',
  'Sem memória para o pedido ao TorBox': 'Out of memory for the TorBox request',
  'Só as páginas da web abrem ao lado do jogo': 'Only web pages open next to the game',
  'Um ficheiro do telemóvel não chegou inteiro': 'A file from the phone didn\'t arrive whole',
};

// Texts with a part that changes: %s any text, %d a number.
const LANG_EN_PATTERNS = [
  ['Extraído para %s; %d ficaram de fora', 'Extracted to %s; %d left out'],
  ['Extraído para %s', 'Extracted to %s'],
  ['Um ficheiro do ZIP está estragado: %s', 'A file in the ZIP is broken: %s'],
  ['Apagar «%s»? Não dá para desfazer.', 'Delete “%s”? This cannot be undone.'],
  ['e mais %d', 'and %d more'],
  ['Pen USB %d', 'USB drive %d'],
  ['Entrar como %s', 'Sign in as %s'],
  ['Pesquisar no %s ou escrever um endereço', 'Search %s or type an address'],
  ['Página inicial: %s', 'Home page: %s'],
  ['Os anúncios voltam a ser bloqueados em %s', 'Ads are blocked again on %s'],
  ['Funciona em: %s', 'Runs on: %s'],
  ['%d página(s) guardada(s)', '%d page(s) saved'],
  ['%d telemóveis ligados', '%d phones connected'],
  ['%d separadores', '%d tabs'],
  ['%d desde que abriu', '%d since it opened'],
  ['%d transferências', '%d downloads'],
  ['O código muda daqui a %s', 'The code changes in %s'],
  ['Chave guardada (acaba em %s)', 'Key saved (ends in %s)'],
  ['E mais %d ficheiros: em torbox.app', 'And %d more files: on torbox.app'],
  ['%d de %d (o máximo)', '%d of %d (the most)'],
  ['%d de %d', '%d of %d'],
  ['%d abertos', '%d open'],
  ['A transferir: %d%', 'Downloading: %d%'],
  ['Código errado. Faltam %d tentativas.', 'Wrong code. %d tries left.'],
  ['Na TV agora · %s', 'On the TV now · %s'],
  ['Na consola · %s', 'On the console · %s'],
  ['Disco externo %d', 'External drive %d'],
  ['Torrent a transferir: %s', 'Downloading torrent: %s'],
  ['Este torrent já está na lista: %s', 'This torrent is already in the list: %s'],
  ['Palavra-passe guardada: %s', 'Password saved: %s'],
  ['Palavra-passe atualizada: %s', 'Password updated: %s'],
  ['Não volto a perguntar em %s', 'I won\'t ask again on %s'],
  ['Chegou do telemóvel: %s', 'Arrived from the phone: %s'],
  ['No TorBox: %s', 'On TorBox: %s'],
  ['O TorBox não aceitou: %s', 'TorBox didn\'t accept it: %s'],
  ['O TorBox respondeu %d', 'TorBox answered %d'],
  ['O Transmission recusou: %s', 'Transmission refused: %s'],
].map(([pt, en]) => {
  const re = pt.split(/(%s|%d)/).map(p => p === '%s' ? '([\\s\\S]+?)' : p === '%d' ? '(\\d+)'
    : p.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('');
  return [new RegExp('^' + re + '$'), en];
});

const LANG_DAYS = {
  pt: ['domingo', 'segunda-feira', 'terça-feira', 'quarta-feira', 'quinta-feira', 'sexta-feira', 'sábado'],
  en: ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'],
};
const LANG_MONTHS = {
  pt: ['janeiro', 'fevereiro', 'março', 'abril', 'maio', 'junho', 'julho', 'agosto', 'setembro',
       'outubro', 'novembro', 'dezembro'],
  en: ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September',
       'October', 'November', 'December'],
};
const LANG_ATTRS = ['title', 'placeholder', 'aria-label'];

// The English of text (its spaces at both ends kept), or null.
function langEn(text) {
  const t = text.trim();
  if (!t) return null;
  let en = Object.prototype.hasOwnProperty.call(LANG_EN, t) ? LANG_EN[t] : null;
  for (let i = 0; en === null && i < LANG_EN_PATTERNS.length; i++) {
    const m = LANG_EN_PATTERNS[i][0].exec(t);
    if (m) {
      let n = 1;
      en = LANG_EN_PATTERNS[i][1].replace(/%[sd]/g, () => {
        const part = m[n++];
        return Object.prototype.hasOwnProperty.call(LANG_EN, part) ? LANG_EN[part] : part;
      });
    }
  }
  if (en === null) return null;
  return text.slice(0, text.length - text.trimStart().length) + en + text.slice(text.trimEnd().length);
}

function T(text, ...args) {
  const context = /^[a-z]+\|/.exec(text);
  const plain = context ? text.slice(context[0].length) : text;
  let s = LANG === 'en' ? (langEn(text) ?? (context ? langEn(plain) : null)) : null;
  if (s === null) s = plain;
  let n = 0;
  return args.length ? s.replace(/%[sd]/g, () => String(args[n++])) : s;
}

// "sábado, 6 de março", "Saturday, 6 March".
function langDay(d) {
  return LANG === 'en'
    ? LANG_DAYS.en[d.getDay()] + ', ' + d.getDate() + ' ' + LANG_MONTHS.en[d.getMonth()]
    : LANG_DAYS.pt[d.getDay()] + ', ' + d.getDate() + ' de ' + LANG_MONTHS.pt[d.getMonth()];
}

// What was swapped, to put it back: text node -> [pt, en]; element ->
// {attribute: [pt, en]}.
const langTexts = new WeakMap();
const langElements = new WeakMap();
let langObserver = null;

function langSkip(n) {
  const e = n.nodeType === 1 ? n : n.parentElement;
  return !e || e.closest('script, style, [translate="no"]') !== null;
}

function langNode(n) {
  if (n.nodeType === 3) {
    if (langSkip(n)) return;
    const en = langEn(n.data);
    if (en !== null && en !== n.data) {
      langTexts.set(n, [n.data, en]);
      n.data = en;
    }
    return;
  }
  if (n.nodeType !== 1 || langSkip(n)) return;
  const done = langElements.get(n) || {};
  if (n.hasAttribute('data-en') && !done.html) {
    done.html = [n.innerHTML, n.getAttribute('data-en')];
    langElements.set(n, done);
    n.innerHTML = done.html[1];
    return;
  }
  for (const a of LANG_ATTRS) {
    const v = n.getAttribute(a);
    const en = v ? langEn(v) : null;
    if (en !== null && en !== v) {
      done[a] = [v, en];
      langElements.set(n, done);
      n.setAttribute(a, en);
    }
  }
  for (let c = n.firstChild; c; c = c.nextSibling) langNode(c);
}

function langRestore(n) {
  if (n.nodeType === 3) {
    const was = langTexts.get(n);
    if (was && n.data === was[1]) n.data = was[0];
    langTexts.delete(n);
    return;
  }
  if (n.nodeType !== 1) return;
  const done = langElements.get(n);
  if (done) {
    langElements.delete(n);
    if (done.html) {
      n.innerHTML = done.html[0];
      return;
    }
    for (const a of LANG_ATTRS) {
      if (done[a] && n.getAttribute(a) === done[a][1]) n.setAttribute(a, done[a][0]);
    }
  }
  for (let c = n.firstChild; c; c = c.nextSibling) langRestore(c);
}

function setLang(lang) {
  lang = lang === 'en' ? 'en' : 'pt';
  if (lang === LANG) return;
  LANG = lang;
  document.documentElement.lang = lang;
  if (lang === 'en') {
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
  } else {
    if (langObserver) langObserver.disconnect();
    langObserver = null;
    langRestore(document.documentElement);
  }
  // For the pages' texts put together before (settings.html, start.html).
  document.dispatchEvent(new Event('langchange'));
}
