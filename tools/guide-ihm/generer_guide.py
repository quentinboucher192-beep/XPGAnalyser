#!/usr/bin/env python3
# =============================================================================
#  tools/guide-ihm/generer_guide.py - le guide de l'IHM, depuis sa source unique
# -----------------------------------------------------------------------------
#    python3 generer_guide.py --cpp  src/hmi/HmiGuideText.cpp     l'aide F1 (C++)
#    python3 generer_guide.py --json guide-ihm.json               le contenu, en JSON
#    python3 generer_guide.py --docx Guide-IHM.docx --captures DOSSIER[,DOSSIER...]
#                             [--pdf Guide-IHM.pdf] [--version "Edition..."]
#                                                                 le guide Word (et PDF), illustre
#
#  Le Word est ecrit par guide_docx.js (Node, paquet npm "docx") ; les captures
#  sont reduites en JPEG (Pillow), d'un cran de plus si le Word depasserait
#  29 Mo (de meme pour le PDF : 30 Mo pour une piece jointe). Le PDF passe par LibreOffice (soffice) et
#  sert aussi a numeroter le sommaire : un premier passage, les numeros de page
#  lus dans le PDF (pdftotext), puis le passage definitif.
#
#  La source est guide-ihm.txt (UTF-8), a cote de ce script ; sa syntaxe est
#  decrite en tete du fichier. Le C++ genere est en ASCII (les accents en \xHH),
#  comme le reste des sources.
# =============================================================================
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# 1.10 (chantier P) : les notations d'un exemple de code. "```ST", "```C",
# "```C++" ouvrent un bloc de code etiquete ; des blocs etiquetes qui se suivent
# forment UN exemple a plusieurs notations (l'aide F1 les montre avec un
# selecteur ST | C | C++, le guide Word les montre l'un apres l'autre).
NOTATIONS = {'st': 'ST', 'c': 'C', 'c++': 'C++', 'cpp': 'C++'}


# ------------------------------------------------------------------ lecture --
def parse(path):
    chapters = []          # [{'name':..., 'topics':[...]}]
    topic = None
    para = []
    in_code = False
    code = []
    code_label = ''

    def flush_para():
        nonlocal para
        if para and topic is not None:
            topic['blocks'].append({'kind': 'paragraph', 'text': ' '.join(para)})
        para = []

    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    for raw in lines:
        line = raw.rstrip()
        if in_code:
            if line.strip() == '```':
                block = {'kind': 'code', 'text': '\n'.join(code)}
                if code_label:
                    block['label'] = code_label      # 1.10 : sa notation (ST, C, C++)
                topic['blocks'].append(block)
                code = []
                in_code = False
            else:
                code.append(raw.rstrip())
            continue
        if line.startswith('#') and not line.startswith('###'):
            continue                                       # un commentaire de la source
        if not line.strip():
            flush_para()
            continue
        if line.startswith('== '):
            flush_para()
            key, _, title = line[3:].partition('|')
            topic = {'key': key.strip(), 'title': title.strip(), 'chapter': chapters[-1]['name'],
                     'summary': '', 'blocks': [], 'see': [], 'words': [], 'places': [], 'shots': [],
                     'kind': '', 'params': [], 'example': '', 'tuto': [], 'since': ''}
            chapters[-1]['topics'].append(topic)
            continue
        if line.startswith('= '):
            flush_para()
            chapters.append({'name': line[2:].strip(), 'topics': []})
            topic = None
            continue
        if topic is None:
            raise SystemExit('ligne hors sujet : ' + line)
        if line.startswith(': '):
            topic['summary'] = line[2:].strip()
            continue
        if line.startswith('@'):
            flush_para()
            tag, _, rest = line.partition(' ')
            rest = rest.strip()
            if tag == '@capture':
                file, _, caption = rest.partition('|')
                topic['shots'].append({'file': file.strip(), 'caption': caption.strip()})
            elif tag in ('@mots', '@lieux', '@voir'):
                items = [x.strip() for x in rest.split(',') if x.strip()]
                topic[{'@mots': 'words', '@lieux': 'places', '@voir': 'see'}[tag]] += items
            elif tag == '@depuis':
                # 1.10 (chantier P) : le sujet est nouveau dans cette version (l'aide
                # l'encadre en orange pour qui vient d'une version plus ancienne).
                topic['since'] = check_version(topic, rest)
            elif tag == '@nouveau':
                # 1.10 : le bloc qui suit a change dans cette version ; un intertitre
                # emporte toute sa section (jusqu'a l'intertitre suivant).
                topic['blocks'].append({'kind': 'mark', 'since': check_version(topic, rest)})
            elif tag == '@objet':
                topic['kind'] = rest
            elif tag == '@exemple':
                topic['example'] = rest
            elif tag == '@tuto':
                # @tuto instant | objet | titre du chapitre | ce que dit la bulle (lot 16)
                parts = [x.strip() for x in re.split(r'\s\|\s', ' ' + rest + ' ')]
                if len(parts) != 4:
                    raise SystemExit('%s : @tuto attend 4 champs : %s' % (topic['key'], rest))
                at, target, title, text = parts
                topic['tuto'].append({'at': float(at.replace(',', '.')), 'target': '' if target == '-' else target,
                                      'title': title, 'text': text})
            elif tag == '@param':
                # @param cle | Libelle | defaut | ce qu'il regle : une ligne du
                # tableau des parametres (pose la ou vient le premier @param).
                parts = [x.strip() for x in re.split(r'\s\|\s', ' ' + rest + ' ')]
                if len(parts) != 4:
                    raise SystemExit('%s : @param attend 4 champs : %s' % (topic['key'], rest))
                key, label, default, text = parts
                topic['params'].append({'key': key, 'label': label, 'def': default, 'text': text})
                blocks = topic['blocks']
                # Le texte long en dernier : c'est la colonne qui se replie.
                row = [label, default if default else '(vide)', text]
                if blocks and blocks[-1]['kind'] == 'table' and blocks[-1].get('params'):
                    blocks[-1]['rows'].append(row)
                else:
                    blocks.append({'kind': 'table', 'params': True,
                                   'rows': [['Paramètre', 'Par défaut', 'Ce qu\'il règle'], row]})
            else:
                raise SystemExit('etiquette inconnue : ' + tag)
            continue
        fence = re.match(r'^```\s*(\S*)$', line.strip())
        if fence:
            flush_para()
            in_code = True
            code_label = ''
            if fence.group(1):
                code_label = NOTATIONS.get(fence.group(1).lower(), '')
                if not code_label:
                    raise SystemExit('%s : notation inconnue apres ``` : %s (ST, C ou C++)' % (topic['key'], fence.group(1)))
            continue
        if line.startswith('### '):
            flush_para()
            topic['blocks'].append({'kind': 'heading', 'text': line[4:].strip()})
            continue
        if line.startswith('- '):
            flush_para()
            topic['blocks'].append({'kind': 'bullet', 'text': line[2:].strip()})
            continue
        m = re.match(r'^(\d+)\. (.*)$', line)
        if m:
            flush_para()
            topic['blocks'].append({'kind': 'step', 'label': m.group(1), 'text': m.group(2).strip()})
            continue
        if line.startswith('> '):
            flush_para()
            topic['blocks'].append({'kind': 'tip', 'text': line[2:].strip()})
            continue
        if line.startswith('! '):
            flush_para()
            topic['blocks'].append({'kind': 'warning', 'text': line[2:].strip()})
            continue
        if line.startswith('|'):
            flush_para()
            cells = [c.strip() for c in split_row(line)]
            blocks = topic['blocks']
            if blocks and blocks[-1]['kind'] == 'table':
                blocks[-1]['rows'].append(cells)
            else:
                blocks.append({'kind': 'table', 'rows': [cells]})
            continue
        para.append(line.strip())
    flush_para()
    # 1.10 : les marques @nouveau se posent sur leur bloc (et sa section).
    for c in chapters:
        for t in c['topics']:
            apply_marks(t)
            check_notations(t)
    # Les renvois doivent designer des sujets qui existent.
    keys = {t['key'] for c in chapters for t in c['topics']}
    for c in chapters:
        for t in c['topics']:
            for s in t['see']:
                if s not in keys:
                    raise SystemExit('%s : renvoi vers un sujet inconnu : %s' % (t['key'], s))
            if not t['summary']:
                raise SystemExit('%s : resume manquant' % t['key'])
    return chapters


def check_version(topic, text):
    # "1.10" ou "1.10.0" : des nombres separes par des points.
    v = text.strip()
    if not re.match(r'^\d+(\.\d+){1,2}$', v):
        raise SystemExit('%s : version illisible : %s' % (topic['key'], text))
    return v


def apply_marks(topic):
    # Une marque {'kind': 'mark'} donne sa version au bloc qui la suit ; si c'est
    # un intertitre, a toute sa section. Les marques disparaissent ensuite.
    out = []
    pending = ''
    section = ''
    for b in topic['blocks']:
        if b['kind'] == 'mark':
            pending = b['since']
            continue
        if b['kind'] == 'heading':
            section = pending
        since = pending or section
        if since:
            b['since'] = since
        pending = ''
        out.append(b)
    if pending:
        raise SystemExit('%s : @nouveau sans bloc apres lui' % topic['key'])
    topic['blocks'] = out


def notation_groups(topic):
    # Les exemples a plusieurs notations : les suites de blocs de code
    # etiquetes (ST, C, C++) qui se suivent. [(premier, dernier)], indices.
    groups = []
    blocks = topic['blocks']
    i = 0
    while i < len(blocks):
        if blocks[i]['kind'] == 'code' and blocks[i].get('label'):
            j = i
            while j + 1 < len(blocks) and blocks[j + 1]['kind'] == 'code' and blocks[j + 1].get('label'):
                j += 1
            groups.append((i, j))
            i = j + 1
        else:
            i += 1
    return groups


def check_notations(topic):
    # Un exemple a plusieurs notations a chaque notation une fois au plus :
    # deux blocs ```C de suite, c'est un oubli (ou deux exemples a separer
    # par une phrase).
    for first, last in notation_groups(topic):
        seen = []
        for b in topic['blocks'][first:last + 1]:
            if b['label'] in seen:
                raise SystemExit('%s : deux blocs ```%s dans le meme exemple (ST, C et C++ une fois chacun)'
                                 % (topic['key'], b['label']))
            seen.append(b['label'])


def split_row(line):
    # "| a | b|c |" : les barres d'un tableau. "Oui|Non" dans une cellule se
    # protege en l'entourant d'espaces : seules " | " et les bords separent.
    body = line.strip()
    if body.startswith('|'):
        body = body[1:]
    if body.endswith('|'):
        body = body[:-1]
    return re.split(r'\s\|\s', ' ' + body + ' ')


# ---------------------------------------------------------------------- C++ --
def cstr(s):
    out = []
    hexa = False
    for ch in s:
        if ord(ch) < 128:
            if hexa and ch in '0123456789abcdefABCDEF':
                out.append('" "')
            hexa = False
            if ch == '"':
                out.append('\\"')
            elif ch == '\\':
                out.append('\\\\')
            elif ch == '\n':
                out.append('\\n')
            elif ch == '\t':
                out.append('\\t')
            else:
                out.append(ch)
        else:
            for b in ch.encode('utf-8'):
                out.append('\\x%02X' % b)
            hexa = True
    return '"' + ''.join(out) + '"'


def cstr_wrapped(s, indent, width=110):
    # Une longue chaine : plusieurs litteraux accoles, coupes aux espaces.
    lit = cstr(s)
    if len(lit) + indent <= width:
        return lit
    parts = []
    cur = ''
    for word in re.split(r'(?<= )', s):
        if cur and len(cstr(cur + word)) + indent > width:
            parts.append(cur)
            cur = word
        else:
            cur += word
    if cur:
        parts.append(cur)
    return ('\n' + ' ' * indent).join(cstr(p) for p in parts)


def block_cpp(b, indent):
    kind = {'heading': 'Heading', 'paragraph': 'Paragraph', 'bullet': 'Bullet', 'step': 'Step',
            'code': 'Code', 'tip': 'Tip', 'warning': 'Warning', 'table': 'Table'}[b['kind']]
    if b['kind'] == 'table':
        text = '\n'.join('\t'.join(r) for r in b['rows'])
    else:
        text = b['text']
    label = b.get('label', '')
    if b.get('since'):
        return '{K::%s, %s, %s, %s}' % (kind, cstr_wrapped(text, indent + 12), cstr(label) if label else '{}', cstr(b['since']))
    return '{K::%s, %s, %s}' % (kind, cstr_wrapped(text, indent + 12), cstr(label) if label else '{}')


def list_cpp(items):
    return '{' + ', '.join(cstr(x) for x in items) + '}'


def emit_cpp(chapters, path):
    lines = []
    lines.append('// ' + '=' * 77)
    lines.append('//  hmi/HmiGuideText.cpp - le contenu du guide de l\'IHM (aide F1)')
    lines.append('// ' + '-' * 77)
    lines.append('//  GENERE par tools/guide-ihm/generer_guide.py depuis tools/guide-ihm/')
    lines.append('//  guide-ihm.txt : NE PAS MODIFIER A LA MAIN. Changer le texte source, puis :')
    lines.append('//      python3 tools/guide-ihm/generer_guide.py --cpp src/hmi/HmiGuideText.cpp')
    lines.append('// ' + '=' * 77)
    lines.append('#include "HmiGuide.hpp"')
    lines.append('#include "../core/Edition.hpp"   // 1.12.2 : les mots de XPGAnalyser IHM')
    lines.append('')
    lines.append('namespace hmi::guide {')
    lines.append('')
    lines.append('const std::vector<Topic>& topics() {')
    lines.append('    using K = BlockKind;')
    lines.append('    static const std::vector<Topic> all = {')
    for c in chapters:
        for t in c['topics']:
            lines.append('        // ---- %s / %s' % (ascii_comment(c['name']), t['key']))
            lines.append('        {%s, %s, %s,' % (cstr(t['key']), cstr(t['title']), cstr(t['chapter'])))
            lines.append('         %s,' % cstr_wrapped(t['summary'], 9))
            lines.append('         {')
            for b in t['blocks']:
                lines.append('             %s,' % block_cpp(b, 13))
            lines.append('         },')
            lines.append('         %s,' % list_cpp(t['see']))
            lines.append('         %s,' % list_cpp(t['words']))
            lines.append('         %s,' % list_cpp(t['places']))
            shots = ', '.join('{%s, %s}' % (cstr(s['file']), cstr(s['caption'])) for s in t['shots'])
            lines.append('         {%s},' % shots)
            params = ',\n           '.join('{%s, %s, %s,\n            %s}' % (cstr(p['key']), cstr(p['label']), cstr(p['def']),
                                                                         cstr_wrapped(p['text'], 12))
                                             for p in t['params'])
            lines.append('         %s,' % cstr(t['kind']))
            lines.append('         {%s},' % params)
            lines.append('         %s,' % cstr_wrapped(t['example'], 9))
            tuto = ',\n           '.join('{%s, %s, %s,\n            %s}' % (repr(float(x['at'])), cstr(x['target']), cstr(x['title']),
                                                                       cstr_wrapped(x['text'], 12))
                                           for x in t['tuto'])
            if t.get('since'):
                lines.append('         {%s}, %s},' % (tuto, cstr(t['since'])))
            else:
                lines.append('         {%s}},' % tuto)
    lines.append('    };')
    lines.append("    // 1.12.2 : XPGAnalyser IHM n'a pas d'automate - ses mots (HmiGuide.cpp, ihmWorded).")
    lines.append('    if (core::hasApi()) return all;')
    lines.append('    static const std::vector<Topic> ihm = ihmWorded(all);')
    lines.append('    return ihm;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace hmi::guide')
    text = '\n'.join(lines) + '\n'
    text.encode('ascii')           # le C++ reste en ASCII
    with open(path, 'w', encoding='ascii', newline='\n') as f:
        f.write(text)


def ascii_comment(s):
    import unicodedata
    return unicodedata.normalize('NFKD', s).encode('ascii', 'ignore').decode('ascii')


# --------------------------------------------------------------------- JSON --
def emit_json(chapters, path):
    with open(path, 'w', encoding='utf-8') as f:
        json.dump({'chapters': chapters}, f, ensure_ascii=False, indent=1)


# ------------------------------------------------------------ Word et PDF --
COVER = 'PNG_156_simulation_tableau_courbe_image.png'


# Lot 16 : le Word et le PDF ont un budget. Chacun doit rester sous 30 Mo (la
# limite d'une piece jointe) et chaque lot ajoute des figures (376 au lot 16).
# Les images partent en JPEG 82 (la qualite du lot 15) ; si le Word depasse le
# budget, elles sont refaites un cran plus bas (78, 74, 70, puis - lot 20, 434
# figures - 66 et 62, et - lot 21 - 58 et 54) ; de meme pour les images du PDF
# (80, 74, 68, 62, puis 56 et 50). La pagination ne depend pas de la qualite.
# --qualite N commence au premier cran qui ne depasse pas N (on sait d'un lot a
# l'autre ou le Word tient : autant ne pas refaire les crans du dessus).
SIZE_BUDGET = 29 * 1024 * 1024
QUALITIES = (82, 78, 74, 70, 66, 62, 58, 54)
PDF_QUALITIES = (80, 74, 68, 62, 56, 50)


def prepare_images(chapters, dirs, out_dir, quality=QUALITIES[0]):
    from PIL import Image
    wanted = [COVER] + [s['file'] for c in chapters for t in c['topics'] for s in t['shots']]
    missing = []
    for name in dict.fromkeys(wanted):
        src = next((os.path.join(d, name) for d in dirs if os.path.exists(os.path.join(d, name))), None)
        if not src:
            missing.append(name)
            continue
        im = Image.open(src).convert('RGB')
        if im.width > 1600:
            im = im.resize((1600, round(im.height * 1600 / im.width)), Image.LANCZOS)
        im.save(os.path.join(out_dir, name[:-4] + '.jpg'), 'JPEG', quality=quality, optimize=True)
    return missing


def megabytes(path):
    return os.path.getsize(path) / 1048576.0


def soffice_pdf(docx, out_dir, quality=PDF_QUALITIES[0]):
    import subprocess
    import tempfile
    # Lot 15 : les images du PDF en JPEG 80, 220 ppp au plus - le PDF reste sous
    # 30 Mo (la limite d'une piece jointe) ; les captures y restent nettes.
    pdf_filter = ('pdf:writer_pdf_Export:{"ReduceImageResolution":{"type":"boolean","value":"true"},'
                  '"MaxImageResolution":{"type":"long","value":"220"},"Quality":{"type":"long","value":"%d"}}' % quality)
    args = ['--headless', '--convert-to', pdf_filter, '--outdir', out_dir, docx]
    helper = '/mnt/skills/public/docx/scripts'
    if os.path.isdir(helper):
        sys.path.insert(0, helper)
        try:
            from office.soffice import run_soffice
            run_soffice(args, check=True, capture_output=True)
            return os.path.join(out_dir, os.path.basename(docx)[:-5] + '.pdf')
        except ImportError:
            pass
    with tempfile.TemporaryDirectory() as profile:
        env = dict(os.environ, SAL_USE_VCLPLUGIN='svp')
        subprocess.run(['soffice', '-env:UserInstallation=file://' + profile] + args, check=True, capture_output=True, env=env)
    return os.path.join(out_dir, os.path.basename(docx)[:-5] + '.pdf')


def pdf_pages(pdf, chapters):
    import subprocess
    text = subprocess.run(['pdftotext', '-layout', pdf, '-'], check=True, capture_output=True).stdout.decode('utf-8', 'replace')
    pages = text.split('\f')
    found = {}
    def last_page(label):
        hit = None
        for i, page in enumerate(pages):
            for line in page.split('\n'):
                if line.strip().startswith(label):
                    hit = i + 1
        return hit
    for ci, c in enumerate(chapters):
        found['c%d' % (ci + 1)] = last_page('%d. %s' % (ci + 1, c['name']))
        for ti, t in enumerate(c['topics']):
            found[t['key']] = last_page('%d.%d %s' % (ci + 1, ti + 1, t['title']))
    return found


def fix_bookmarks(path):
    # docx (npm) numerote tous les signets 1 : Word veut des numeros uniques.
    # Nos signets ne s'imbriquent pas : le n-ieme debut et la n-ieme fin vont
    # ensemble.
    import zipfile
    with zipfile.ZipFile(path) as z:
        items = [(i, z.read(i.filename)) for i in z.infolist()]
    out = []
    for info, data in items:
        if info.filename == 'word/document.xml':
            xml = data.decode('utf-8')
            count = {'start': 0, 'end': 0}
            def renumber(kind):
                def f(m):
                    count[kind] += 1
                    return m.group(1) + 'w:id="%d"' % count[kind]
                return f
            xml = re.sub(r'(<w:bookmarkStart\b[^>]*?)w:id="\d+"', renumber('start'), xml)
            xml = re.sub(r'(<w:bookmarkEnd\b[^>]*?)w:id="\d+"', renumber('end'), xml)
            data = xml.encode('utf-8')
        out.append((info, data))
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for info, data in out:
            z.writestr(info, data)


def build_docx(chapters, out, captures, version, pdf, start=None):
    import shutil
    import subprocess
    import tempfile
    dirs = [d for d in captures.split(',') if d]
    js = os.path.join(HERE, 'guide_docx.js')
    with tempfile.TemporaryDirectory() as tmp:
        data = os.path.join(tmp, 'guide.json')
        emit_json(chapters, data)
        images = os.path.join(tmp, 'images')
        os.makedirs(images)
        docx = os.path.join(tmp, os.path.basename(out))
        for quality in [q for q in QUALITIES if start is None or q <= start] or [QUALITIES[-1]]:
            missing = prepare_images(chapters, dirs, images, quality)
            subprocess.run(['node', js, data, images, docx, '', version], check=True)
            fix_bookmarks(docx)
            print('Word en JPEG %d : %.1f Mo' % (quality, megabytes(docx)))
            if os.path.getsize(docx) <= SIZE_BUDGET:
                break
        for m in missing:
            print('capture introuvable : ' + m)
        if pdf:
            # Premier passage : ou tombe chaque sujet ; second : le sommaire numerote.
            first = soffice_pdf(docx, tmp)
            pages = pdf_pages(first, chapters)
            unknown = [k for k, v in pages.items() if not v]
            if unknown:
                print('pages non trouvees : ' + ', '.join(unknown))
            with open(os.path.join(tmp, 'pages.json'), 'w', encoding='utf-8') as f:
                json.dump(pages, f)
            subprocess.run(['node', js, data, images, docx, os.path.join(tmp, 'pages.json'), version], check=True)
            fix_bookmarks(docx)
            for quality in [q for q in PDF_QUALITIES if start is None or q <= start] or [PDF_QUALITIES[-1]]:
                final = soffice_pdf(docx, tmp, quality)
                print('PDF en JPEG %d : %.1f Mo' % (quality, megabytes(final)))
                if os.path.getsize(final) <= SIZE_BUDGET:
                    break
            again = pdf_pages(final, chapters)
            if again != pages:
                print('attention : la pagination a bouge entre les deux passages')
            shutil.copyfile(final, pdf)
            print('PDF : %s' % pdf)
        shutil.copyfile(docx, out)
        print('Word : %s' % out)


# --------------------------------------------------------------------- main --
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', default=os.path.join(HERE, 'guide-ihm.txt'))
    ap.add_argument('--cpp')
    ap.add_argument('--json')
    ap.add_argument('--docx')
    ap.add_argument('--captures', default='')
    ap.add_argument('--version', default='')
    ap.add_argument('--pdf')
    ap.add_argument('--qualite', type=int)
    a = ap.parse_args()
    chapters = parse(a.source)
    n = sum(len(c['topics']) for c in chapters)
    if a.cpp:
        emit_cpp(chapters, a.cpp)
        print('C++ : %s (%d sujets)' % (a.cpp, n))
    if a.json:
        emit_json(chapters, a.json)
        print('JSON : %s' % a.json)
    if a.docx:
        build_docx(chapters, a.docx, a.captures, a.version, a.pdf, a.qualite)
    if not (a.cpp or a.json or a.docx):
        print('%d chapitres, %d sujets' % (len(chapters), n))


if __name__ == '__main__':
    sys.path.insert(0, HERE)
    main()
