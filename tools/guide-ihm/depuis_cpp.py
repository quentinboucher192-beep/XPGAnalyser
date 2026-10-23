#!/usr/bin/env python3
# =============================================================================
#  tools/guide-ihm/depuis_cpp.py - 1.12.27 : LE GUIDE, DU C++ VERS LE TEXTE
# -----------------------------------------------------------------------------
#  guide-ihm.txt est la source de l'aide F1 (generer_guide.py --cpp en fait
#  src/hmi/HmiGuideText.cpp). Quand le C++ a ete retouche a la main, ce script
#  remet le texte a jour depuis lui, sans perdre les commentaires (#) du texte :
#
#      python3 tools/guide-ihm/depuis_cpp.py src/hmi/HmiGuideText.cpp
#          reecrit tools/guide-ihm/guide-ihm.txt depuis le C++
#      python3 tools/guide-ihm/depuis_cpp.py src/hmi/HmiGuideText.cpp --verifier
#          dit si le texte et le C++ disent la meme chose (code 1 sinon)
#
#  Les deux se comparent par leur contenu (chapitres, sujets, blocs, mots...),
#  pas par leur mise en page.
# =============================================================================
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import generer_guide as gg  # noqa: E402

KINDS = {'Heading': 'heading', 'Paragraph': 'paragraph', 'Bullet': 'bullet', 'Step': 'step', 'Code': 'code',
         'Tip': 'tip', 'Warning': 'warning', 'Table': 'table'}
PARAM_HEAD = ['Paramètre', 'Par défaut', 'Ce qu\'il règle']


# ------------------------------------------------------------- le C++ lu --
def tokens(text):
    """Les jetons du tableau : chaines (accolees), nombres, noms, { } , ."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in ' \t\r\n':
            i += 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j + 1
        elif c == '"':
            raw = bytearray()
            while i < n and text[i] == '"':
                i += 1
                while text[i] != '"':
                    ch = text[i]
                    if ch == '\\':
                        e = text[i + 1]
                        if e == 'x':
                            m = re.match(r'[0-9A-Fa-f]{1,2}', text[i + 2:])
                            raw.append(int(m.group(0), 16))
                            i += 2 + len(m.group(0))
                            continue
                        raw += {'n': b'\n', 't': b'\t', '"': b'"', '\\': b'\\', "'": b"'"}[e]
                        i += 2
                        continue
                    raw += ch.encode('utf-8')
                    i += 1
                i += 1
                # les litteraux accoles (espaces, retours, commentaires entre eux)
                j = i
                while j < n:
                    if text[j] in ' \t\r\n':
                        j += 1
                    elif text.startswith('//', j):
                        k = text.find('\n', j)
                        j = n if k < 0 else k + 1
                    else:
                        break
                if j < n and text[j] == '"':
                    i = j
            yield ('s', raw.decode('utf-8'))
        elif c in '{},':
            yield (c, c)
            i += 1
        else:
            m = re.match(r'[A-Za-z_][A-Za-z0-9_:]*|[-+]?[0-9]+(\.[0-9]*)?([eE][-+]?[0-9]+)?', text[i:])
            if not m:
                raise SystemExit('C++ : jeton illisible pres de : %r' % text[i:i + 40])
            yield ('n', m.group(0))
            i += len(m.group(0))


def tree(toks):
    """Une liste { ... } en listes Python ; les chaines et les noms en feuilles."""
    out = []
    for kind, val in toks:
        if kind == '{':
            out.append(tree(toks))
        elif kind == '}':
            return out
        elif kind == ',':
            continue
        else:
            out.append(val)
    return out


def lire_cpp(path):
    with open(path, encoding='ascii') as f:
        text = f.read()
    start = text.index('static const std::vector<Topic> all = {')
    end = text.index('    };\n', start)
    body = text[start + len('static const std::vector<Topic> all = {'):end]
    items = tree(iter(list(tokens(body))))
    chapters = []
    for t in items:
        key, title, chapter, summary, blocks, see, words, places, shots = t[:9]
        kind, params, example, tuto = t[9:13]
        since = t[13] if len(t) > 13 else ''
        topic = {'key': key, 'title': title, 'chapter': chapter, 'summary': summary, 'blocks': [],
                 'see': list(see), 'words': list(words), 'places': list(places),
                 'shots': [{'file': s[0], 'caption': s[1]} for s in shots],
                 'kind': kind, 'params': [{'key': p[0], 'label': p[1], 'def': p[2], 'text': p[3]} for p in params],
                 'example': example,
                 'tuto': [{'at': float(x[0]), 'target': x[1], 'title': x[2], 'text': x[3]} for x in tuto],
                 'since': since}
        for b in blocks:
            k = KINDS[b[0].split('::')[1]]
            text_b = b[1]
            label = b[2] if len(b) > 2 and isinstance(b[2], str) else ''
            blk = {'kind': k}
            if k == 'table':
                blk['rows'] = [r.split('\t') for r in text_b.split('\n')]
            else:
                blk['text'] = text_b
            if label:
                blk['label'] = label
            if len(b) > 3 and b[3]:
                blk['since'] = b[3]
            topic['blocks'].append(blk)
        # Les @param font a la fois la liste des parametres et le tableau du sujet. Retouches
        # a la main, les deux ont pu diverger : la liste garde ses lignes (et son ordre), le
        # texte le plus complet des deux l'emporte ; le tableau se refait depuis elle.
        if topic['params']:
            for blk in topic['blocks']:
                if blk['kind'] == 'table' and blk['rows'] and blk['rows'][0] == PARAM_HEAD:
                    body = [r for r in blk['rows'][1:] if len(r) == 3]
                    table = {r[0]: r for r in body}
                    same_count = len(body) == len(topic['params'])
                    for i, prm in enumerate(topic['params']):
                        # par son libelle ; un libelle renomme d'un cote : a la meme place
                        r = table.get(prm['label']) or (body[i] if same_count else None)
                        if r and r[2] != prm['text'] and len(r[2]) > len(prm['text']):
                            prm['text'] = r[2]
                    rows = [[prm['label'], prm['def'] if prm['def'] else '(vide)', prm['text']] for prm in topic['params']]
                    if blk['rows'][1:] != rows:
                        PARAMS.append(key)
                    blk['rows'] = [PARAM_HEAD] + rows
                    break
        if not chapters or chapters[-1]['name'] != chapter:
            chapters.append({'name': chapter, 'topics': []})
        chapters[-1]['topics'].append(topic)
    return chapters


PARAMS = []       # les sujets dont le tableau des parametres a ete refait depuis la liste


# ------------------------------------------------------------ le texte ecrit --
def commentaires(path):
    """Les commentaires du texte : l'en-tete, puis ceux de chaque sujet (par cle)."""
    head, per = [], {}
    key = None
    if not os.path.exists(path):
        return head, per
    with open(path, encoding='utf-8') as f:
        for raw in f.read().split('\n'):
            line = raw.rstrip()
            if line.startswith('== '):
                key = line[3:].partition('|')[0].strip()
                continue
            if line.startswith('= '):
                key = None
                continue
            if line.startswith('#') and not line.startswith('###'):
                (per.setdefault(key, []) if key else head).append(line)
    return head, per


def ligne_bloc(b):
    k = b['kind']
    if k == 'heading':
        return ['### ' + b['text']]
    if k == 'bullet':
        return ['- ' + b['text']]
    if k == 'step':
        return ['%s. %s' % (b.get('label', '1'), b['text'])]
    if k == 'tip':
        return ['> ' + b['text']]
    if k == 'warning':
        return ['! ' + b['text']]
    if k == 'table':
        return ['| ' + ' | '.join(r) + ' |' for r in b['rows']]
    if k == 'code':
        lab = b.get('label', '')
        return ['```' + lab] + b['text'].split('\n') + ['```']
    # un paragraphe : une ligne ; garde-fou, une ligne qui ressemble a autre chose
    t = b['text']
    if re.match(r'^(### |- |\d+\. |> |! |\||@|=|:|#|```)', t):
        raise SystemExit('paragraphe ambigu dans le texte : %r' % t[:60])
    return [t]


def ecrire_txt(chapters, path):
    head, per = commentaires(path)
    out = list(head) + ['']
    for c in chapters:
        out.append('= ' + c['name'])
        out.append('')
        for t in c['topics']:
            out.append('== %s | %s' % (t['key'], t['title']))
            out.append(': ' + t['summary'])
            out += per.get(t['key'], [])
            if t['since']:
                out.append('@depuis ' + t['since'])
            if t['words']:
                out.append('@mots ' + ', '.join(t['words']))
            if t['places']:
                out.append('@lieux ' + ', '.join(t['places']))
            if t['see']:
                out.append('@voir ' + ', '.join(t['see']))
            for s in t['shots']:
                out.append('@capture %s | %s' % (s['file'], s['caption']))
            if t['kind']:
                out.append('@objet ' + t['kind'])
            if t['example']:
                out.append('@exemple ' + t['example'])
            for x in t['tuto']:
                at = ('%g' % x['at']).replace('.', ',')
                out.append('@tuto %s | %s | %s | %s' % (at, x['target'] or '-', x['title'], x['text']))
            section = ''
            params_done = False
            prev_kind = None
            for b in t['blocks']:
                since = b.get('since', '')
                if b['kind'] == 'heading':
                    section = since
                    if since:
                        out.append('@nouveau ' + since)
                elif since != section:
                    # sans version dans une section nouvelle : « @nouveau - » (il est d'avant)
                    out.append('@nouveau ' + (since if since else '-'))
                # le tableau des @param : il se redit en @param, a sa place
                if (b['kind'] == 'table' and t['params'] and not params_done and b['rows'] and b['rows'][0] == PARAM_HEAD):
                    rows = b['rows'][1:]
                    want = [[p['label'], p['def'] if p['def'] else '(vide)', p['text']] for p in t['params']]
                    if rows != want:
                        raise SystemExit('%s : le tableau des parametres ne suit pas les @param' % t['key'])
                    for p in t['params']:
                        out.append('@param %s | %s | %s | %s' % (p['key'], p['label'], p['def'], p['text']))
                    params_done = True
                    prev_kind = 'params'
                    continue
                # deux tableaux, deux paragraphes ou deux codes de suite : une ligne vide entre eux
                if prev_kind == b['kind'] and b['kind'] in ('table', 'paragraph'):
                    out.append('')
                out += ligne_bloc(b)
                prev_kind = b['kind']
            if t['params'] and not params_done:
                raise SystemExit('%s : des @param sans leur tableau' % t['key'])
            out.append('')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out).rstrip('\n') + '\n')


# ----------------------------------------------------------- la comparaison --
def aplati(chapters):
    """Le contenu, sans la mise en page : une liste de sujets, chacun un dict."""
    out = []
    for c in chapters:
        for t in c['topics']:
            d = dict(t)
            d['chapter'] = c['name']
            d['blocks'] = [{k: v for k, v in b.items() if k != 'params'} for b in t['blocks']]
            d['tuto'] = [dict(x, at=round(float(x['at']), 3)) for x in t['tuto']]
            out.append(d)
    return out


def differences(a, b, limite=20):
    fa, fb = aplati(a), aplati(b)
    diffs = []
    if [t['key'] for t in fa] != [t['key'] for t in fb]:
        diffs.append('les sujets ne sont pas les memes (ou pas dans le meme ordre)')
    for ta, tb in zip(fa, fb):
        for k in ta:
            if ta[k] != tb.get(k):
                diffs.append('%s : %s differe' % (ta['key'], k))
                break
        if len(diffs) >= limite:
            break
    return diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cpp')
    ap.add_argument('--txt', default=os.path.join(HERE, 'guide-ihm.txt'))
    ap.add_argument('--verifier', action='store_true')
    a = ap.parse_args()
    depuis_cpp = lire_cpp(a.cpp)
    n = sum(len(c['topics']) for c in depuis_cpp)
    if a.verifier:
        diffs = differences(gg.parse(a.txt), depuis_cpp)
        if diffs:
            print('le texte et le C++ different :')
            for d in diffs:
                print('  ' + d)
            sys.exit(1)
        print('le texte et le C++ disent la meme chose (%d sujets)' % n)
        return
    ecrire_txt(depuis_cpp, a.txt)
    relu = gg.parse(a.txt)
    diffs = differences(relu, depuis_cpp)
    if diffs:
        print('ATTENTION : le texte ecrit ne se relit pas comme le C++ :')
        for d in diffs:
            print('  ' + d)
        sys.exit(1)
    if PARAMS:
        print('note : tableau des parametres refait pour %s' % ', '.join(PARAMS))
    print('texte : %s (%d sujets, relu sans difference)' % (a.txt, n))


if __name__ == '__main__':
    main()
