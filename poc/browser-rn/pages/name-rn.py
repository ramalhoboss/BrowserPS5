#!/usr/bin/env python3
# name-rn.py <the copy's browser folder> (2026-11-18): the app is "BrowserPS5 RN" wherever it names itself to
# people (its name on the PS5's home screen and at its start). The pages are v1's, which says "BrowserPS5"; here,
# in RN's copy only (build-rn.sh runs this after the copy's lang.js has every language at its end), the name gets
# its "RN":
#   - in the texts of the pages listed below: each one must be there, so a page that changed stops the build
#     instead of leaving the old name in it;
#   - in every text of the copy's lang.js: the English and the other languages' tables, what a text is found by
#     and what it becomes alike, so a renamed text still finds its translation.
# What programs are told stays as it is: the controller's id for the pages (ext/gamepad.js), the user scripts'
# handler name (ext/gm.js, ext/manager.js). v1's files are not touched.
import re
import sys

OLD, NEW = 'BrowserPS5', 'BrowserPS5 RN'
PAGES = {
    'menu.html': ['<span class="label">Sair do BrowserPS5</span>'],
    'start.html': ['<h1>Bem-vindo ao BrowserPS5</h1>'],
    'settings.html': ['<b>Início do BrowserPS5</b>',
                      "BrowserPS5's own and the ones you install",
                      'os do BrowserPS5 e os que instalares',
                      '<h2>Do BrowserPS5</h2>',
                      '<b>BrowserPS5</b><span id="about-version">'],
    'phone.html': ['<title>BrowserPS5 · Telemóvel</title>',
                   '<b>BrowserPS5</b><span>no teu telemóvel</span>'],
}


def main(folder):
    for name, texts in PAGES.items():
        path = folder + '/' + name
        page = open(path, encoding='utf-8', newline='').read()
        for text in texts:
            if page.count(text) != 1:
                sys.exit('name-rn.py: %s: "%s" is there %d times, not once' % (name, text, page.count(text)))
            page = page.replace(text, text.replace(OLD, NEW))
        open(path, 'w', encoding='utf-8', newline='').write(page)
    path = folder + '/lang.js'
    lines = open(path, encoding='utf-8', newline='').read().split('\n')
    changed = 0
    for i, line in enumerate(lines):
        if line.lstrip().startswith('//'):
            continue
        lines[i], n = re.subn(OLD + r'(?! RN)', NEW, line)
        changed += n
    if changed < 10:  # five texts, each with its English
        sys.exit('name-rn.py: lang.js: only %d names found' % changed)
    open(path, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
    print('name-rn.py: %d texts of the pages, %d names in lang.js' % (sum(len(t) for t in PAGES.values()), changed))


if __name__ == '__main__':
    main(sys.argv[1])
