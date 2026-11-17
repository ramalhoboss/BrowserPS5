"""BrowserPS5 RN's other languages (2026-11-14): lang/<code>.json -> pages/lang-more.js, the tables build-rn.sh
puts at the end of the copy's lang.js (after pages/lang-rn.js, which has langTable()).

A language's file: {"name": its own name, "days": [7, from Sunday], "months": [12], "day": how a day is written
("%w, %d de %m"), "texts": {the Portuguese text: its translation}, "patterns": [[the Portuguese pattern, its
translation], ...]}. A text or a pattern that is not there comes in English.

    python make-lang-more.py        (in this folder; then: node lang_more_test.js)
"""
import json, os, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
out = ['',
       '// BrowserPS5 RN: the tables of the other languages. MADE by lang/make-lang-more.py from lang/*.json:',
       '// change those, not this file.']
codes = []
for path in sorted(glob.glob(os.path.join(HERE, '??.json'))):
    code = os.path.basename(path)[:2]
    table = json.load(open(path, encoding='utf-8'))
    codes.append(code)
    js = {'dict': table.get('texts', {}), 'patterns': table.get('patterns', []), 'days': table['days'],
          'months': table['months'], 'day': table['day']}
    out.append("langLazy('%s', function () { return %s; });" % (code, json.dumps(js, ensure_ascii=False, separators=(',', ':'))))
# The choices of the languages that have a table are shown (settings.html, start.html: hidden otherwise).
out.append("for (const b of document.querySelectorAll('.choice[data-rn][data-v], #langs [data-rn][data-lang]'))")
out.append("  if (Object.prototype.hasOwnProperty.call(LANG_LAZY, b.dataset.v || b.dataset.lang)) b.style.display = b.classList.contains('choice') ? 'flex' : 'inline-block';")
out.append('')
open(os.path.join(HERE, '..', 'pages', 'lang-more.js'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print('lang-more.js:', ', '.join(codes) or 'no language', '(%d bytes)' % len('\n'.join(out).encode('utf-8')))
