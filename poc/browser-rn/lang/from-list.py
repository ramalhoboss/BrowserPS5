"""A translator's numbered list -> lang/<code>.json (2026-11-14).

    python from-list.py es        reads es.txt and meta.json, writes es.json
    python from-list.py --list    prints the numbered English texts to translate (and the patterns, P0...)

The list's numbers are the order of the texts in ../../browser/lang.js (LANG_EN) followed by ../pages/lang-rn.js's
own; "P<n>" lines are lang.js's patterns. A line "123<TAB>the translation". The .json is keyed by the Portuguese
text, so it stays right when texts are added or moved; the list is only how a language is first written.
"""
import re, json, os, sys, io

HERE = os.path.dirname(os.path.abspath(__file__))
STR = r"'((?:[^'\\]|\\.)*)'"


def unq(s):
    return s.replace("\\'", "'").replace('\\\\', '\\')


def texts():
    lang = open(os.path.join(HERE, '..', '..', 'browser', 'lang.js'), encoding='utf-8').read()
    a = lang.index('const LANG_EN = {')
    b = lang.index('const LANG_EN_PATTERNS')
    c = lang.index('].map(([pt, en])')
    pairs = [(unq(m.group(1)), unq(m.group(2))) for m in re.finditer(STR + r"\s*:\s*" + STR, lang[a:b])]
    rn = open(os.path.join(HERE, '..', 'pages', 'lang-rn.js'), encoding='utf-8').read()
    rn = rn[:rn.index('const LANG_MORE')] if 'const LANG_MORE' in rn else rn
    pairs += [(unq(m.group(1)), unq(m.group(2))) for m in re.finditer(STR + r"\s*:\s*" + STR, rn)]
    pats = [(unq(m.group(1)), unq(m.group(2))) for m in re.finditer(r"\[" + STR + r",\s*" + STR + r"\]", lang[b:c])]
    return pairs, pats


def main():
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
    pairs, pats = texts()
    if sys.argv[1] == '--list':
        for i, (pt, en) in enumerate(pairs):
            print('%d\t%s' % (i, en))
        for i, (pt, en) in enumerate(pats):
            print('P%d\t%s' % (i, en))
        return
    code = sys.argv[1]
    meta = json.load(open(os.path.join(HERE, 'meta.json'), encoding='utf-8'))[code]
    out = {'name': meta['name'], 'days': meta['days'], 'months': meta['months'], 'day': meta['day'], 'texts': {}, 'patterns': []}
    seen = set()
    for line in open(os.path.join(HERE, code + '.txt'), encoding='utf-8').read().split('\n'):
        if not line.strip():
            continue
        number, _, text = line.partition('\t')
        text = text.strip()
        assert text, 'no text in line ' + number
        assert number not in seen, 'twice: ' + number
        seen.add(number)
        if number.startswith('P'):
            pt = pats[int(number[1:])][0]
            out['patterns'].append([pt, text])
        else:
            pt = pairs[int(number)][0]
            assert pt not in out['texts'] or out['texts'][pt] == text, 'two translations of ' + pt
            out['texts'][pt] = text
    json.dump(out, open(os.path.join(HERE, code + '.json'), 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent=1)
    print('%s.json: %d of %d texts, %d of %d patterns' % (code, len(out['texts']), len(set(p for p, _ in pairs)), len(out['patterns']), len(pats)))


main()
