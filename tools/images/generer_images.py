#!/usr/bin/env python3
# =============================================================================
#  tools/images/generer_images.py - la bibliotheque d'images de l'IHM (1.14.1, lot N6)
# -----------------------------------------------------------------------------
#    python3 tools/images/generer_images.py --cpp src/hmi/HmiImageLibraryData.cpp      le C++ embarque
#    python3 tools/images/generer_images.py --svg DOSSIER                              un .svg par image
#    python3 tools/images/generer_images.py --verifier src/hmi/HmiImageLibraryData.cpp le C++ est-il a jour ?
#
#  La source est images.txt (UTF-8), a cote de ce script ; sa syntaxe est decrite
#  en tete du fichier. Chaque image y est DESSINEE (des traits, des disques, des
#  chemins dans une grille de 24 x 24) : aucune image ni police d'icones d'ailleurs.
#  Le C++ genere est en ASCII (les accents en \xHH), comme le reste des sources ;
#  les images y sont des SVG (nanosvg les dessine nets a toute taille), embarques
#  dans l'executable : l'installateur n'a rien de plus a livrer.
# =============================================================================
import argparse
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, 'images.txt')

INK = '#E6EAF0'          # l'encre des dessins monochromes (hmi::imagelib::kInk) : remplacee a l'import
PLACE = 8.0 / 3.0        # une grille de 24 se pose en 64 pixels

COLORS = {'encre': INK}


class Fail(Exception):
    pass


# ------------------------------------------------------------------ lecture --
def parse(path):
    cats = []            # [(nom, [image, ...])]
    images = {}          # cle -> image
    cur = None
    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    for no, raw in enumerate(lines, 1):
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if line.startswith('@couleur '):
            _, name, value = line.split()
            COLORS[name] = value.upper()
            continue
        if line.startswith('== '):
            parts = [p.strip() for p in line[3:].split('|')]
            if len(parts) < 3:
                raise Fail('ligne %d : == cle | Nom | mots [| LxH]' % no)
            key, label, words = parts[0], parts[1], parts[2]
            w, h = 24, 24
            if len(parts) > 3 and parts[3]:
                m = re.fullmatch(r'(\d+)x(\d+)', parts[3])
                if not m:
                    raise Fail('ligne %d : grille %r (LxH)' % (no, parts[3]))
                w, h = int(m.group(1)), int(m.group(2))
            if key in images:
                raise Fail('ligne %d : %s deja definie' % (no, key))
            if not re.fullmatch(r'_?[a-z0-9]+(-[a-z0-9]+)*', key):
                raise Fail('ligne %d : cle %r (minuscules, chiffres, tirets)' % (no, key))
            if not cats:
                raise Fail('ligne %d : une image avant la premiere categorie' % no)
            cur = {'key': key, 'label': label, 'words': words, 'w': w, 'h': h, 'cat': cats[-1][0],
                   'elems': [], 'line': no}
            images[key] = cur
            cats[-1][1].append(cur)
            continue
        if line.startswith('= '):
            cats.append((line[2:].strip(), []))
            cur = None
            continue
        if raw[:1] in (' ', '\t'):
            if cur is None:
                raise Fail('ligne %d : une forme hors d\'une image' % no)
            cur['elems'].append((line.strip(), no))
            continue
        raise Fail('ligne %d : incomprise : %s' % (no, line))
    return cats, images


# ------------------------------------------------------------------ dessin --
def num(v):
    s = ('%.3f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def color(value, no):
    if value.startswith('#'):
        if not re.fullmatch(r'#[0-9A-Fa-f]{6}', value):
            raise Fail('ligne %d : couleur %s' % (no, value))
        if value.upper() == INK:
            raise Fail('ligne %d : %s est l\'encre (ecrire coul=encre)' % (no, value))
        return value.upper()
    if value not in COLORS:
        raise Fail('ligne %d : couleur inconnue %s' % (no, value))
    return COLORS[value]


def split_opts(text):
    body, _, opts = text.partition(';')
    out = {}
    for o in opts.split():
        k, _, v = o.partition('=')
        out[k] = v
    return body.strip(), out


def gear_path(cx, cy, r, n):
    depth = r * 0.24
    ri = r - depth
    pitch = 2 * math.pi / n
    pts = []
    for i in range(n):
        a = -math.pi / 2 + i * pitch
        for (rad, da) in ((ri, -pitch * 0.30), (r, -pitch * 0.18), (r, pitch * 0.18), (ri, pitch * 0.30)):
            pts.append((cx + rad * math.cos(a + da), cy + rad * math.sin(a + da)))
    return 'M' + ' L'.join('%s %s' % (num(x), num(y)) for x, y in pts) + 'Z'


def arrow_head(tip, direction, size):
    # Les deux barbes d'une pointe en `tip` qui va dans `direction` (un vecteur unitaire).
    dx, dy = direction
    out = []
    for a in (math.radians(140), math.radians(-140)):
        bx = dx * math.cos(a) - dy * math.sin(a)
        by = dx * math.sin(a) + dy * math.cos(a)
        out.append((tip[0] + bx * size, tip[1] + by * size))
    return 'M%s %sL%s %sL%s %s' % (num(out[0][0]), num(out[0][1]), num(tip[0]), num(tip[1]), num(out[1][0]), num(out[1][1]))


def arc_arrow(cx, cy, r, a0, a1, head):
    # Un arc de a0 a a1 (degres, 0 a droite, 90 en bas ; a1 > a0 : sens horaire), sa pointe en a1.
    p0 = (cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0)))
    p1 = (cx + r * math.cos(math.radians(a1)), cy + r * math.sin(math.radians(a1)))
    sweep = 1 if a1 > a0 else 0
    large = 1 if abs(a1 - a0) > 180 else 0
    t = math.radians(a1)
    d = (-math.sin(t), math.cos(t)) if sweep else (math.sin(t), -math.cos(t))
    return 'M%s %sA%s %s 0 %d %d %s %s%s' % (num(p0[0]), num(p0[1]), num(r), num(r), large, sweep, num(p1[0]), num(p1[1]),
                                          arrow_head(p1, d, head))


def star_points(cx, cy, R, r, n, start=-90.0):
    pts = []
    for i in range(2 * n):
        rad = R if i % 2 == 0 else r
        a = math.radians(start + i * 180.0 / n)
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    return pts


def sector(cx, cy, r1, r2, a0, a1):
    def p(rad, a):
        a = math.radians(a)
        return num(cx + rad * math.cos(a)), num(cy + rad * math.sin(a))
    x0, y0 = p(r2, a0)
    x1, y1 = p(r2, a1)
    x2, y2 = p(r1, a1)
    x3, y3 = p(r1, a0)
    return 'M%s %s A%s %s 0 0 1 %s %sL%s %s A%s %s 0 0 0 %s %sZ' % (
        x0, y0, num(r2), num(r2), x1, y1, x2, y2, num(r1), num(r1), x3, y3)


# Les chiffres des pastilles et des compteurs : dessines au trait dans une case de
# 6 x 10 (le trait centre sur le bord), puis mis a l'echelle.
DIGITS = {
    '0': 'M3 0a3 3 0 0 0-3 3v4a3 3 0 0 0 6 0V3a3 3 0 0 0-3-3z',
    '1': 'M1.2 2.2L3.6 0v10',
    '2': 'M.3 2.6a2.8 2.8 0 0 1 5.5.4c0 1.6-1 2.5-2.4 3.7L.2 10H6',
    '3': 'M.6 0h5.1L3 3.9a3.05 3.05 0 1 1-2.8 4.6',
    '4': 'M4.6 10V0L0 7.1h6',
    '5': 'M5.6 0H1.2L.7 4.3a3.1 3.1 0 1 1-.5 4.4',
    '6': 'M4.6.3C2 .9 0 3.3 0 6.6a3.05 3.05 0 1 0 1-2.2',
    '7': 'M0 0h6L2.2 10',
    '8': 'M3 4.6A2.3 2.3 0 1 1 3 0a2.3 2.3 0 0 1 0 4.6zm0 0a2.7 2.7 0 1 0 0 5.4 2.7 2.7 0 0 0 0-5.4z',
    '9': 'M1.4 9.7C4 9.1 6 6.7 6 3.4a3.05 3.05 0 1 0-1 2.2',
}


class Ctx:
    def __init__(self, images):
        self.images = images
        self.stack = []


def render_image(ctx, img, stroke_mul=1.0, recolor=None):
    if img['key'] in ctx.stack:
        raise Fail('ligne %d : %s se contient' % (img['line'], img['key']))
    ctx.stack.append(img['key'])
    out = []
    for text, no in img['elems']:
        out.extend(render_elem(ctx, img, text, no, stroke_mul))
    ctx.stack.pop()
    svg = ''.join(out)
    if recolor:
        for a, b in recolor.items():
            svg = svg.replace(a, b)
    return svg


def include(ctx, key, no, cx, cy, size, stroke_mul, recolor):
    if key not in ctx.images:
        raise Fail('ligne %d : image inconnue %s' % (no, key))
    inner = ctx.images[key]
    s = size / max(inner['w'], inner['h'])
    tx = cx - inner['w'] * s / 2
    ty = cy - inner['h'] * s / 2
    body = render_image(ctx, inner, stroke_mul, recolor)
    return '<g transform="translate(%s %s) scale(%s)">%s</g>' % (num(tx), num(ty), num(s), body)


def panel(ctx, kind, key, no):
    noir, blanc = COLORS['noir'], COLORS['blanc']
    out = []
    if kind == 'danger':
        out.append('<path d="M12 2.4L22.6 20.9H1.4Z" fill="%s" stroke="%s" stroke-width="1.7"/>' % (COLORS['jaune'], noir))
        if key == '-':
            out.append('<path d="M12 8.6v6.2" fill="none" stroke="%s" stroke-width="2.4"/>' % noir)
            out.append('<circle cx="12" cy="17.9" r="1.35" fill="%s"/>' % noir)
        else:
            out.append(include(ctx, key, no, 12, 14.6, 9.6, 1.45, {INK: noir}))
    elif kind == 'interdit':
        rouge = COLORS['rouge']
        out.append('<circle cx="12" cy="12" r="10.3" fill="%s"/>' % blanc)
        if key != '-':
            out.append(include(ctx, key, no, 12, 12, 12.5, 1.3, {INK: noir}))
        out.append('<circle cx="12" cy="12" r="9.2" fill="none" stroke="%s" stroke-width="2.6"/>' % rouge)
        out.append('<path d="M5.5 5.5L18.5 18.5" fill="none" stroke="%s" stroke-width="2.6" stroke-linecap="butt"/>' % rouge)
    elif kind == 'obligation':
        out.append('<circle cx="12" cy="12" r="10.5" fill="%s"/>' % COLORS['bleu'])
        if key == '-':
            out.append('<path d="M12 6.5v7.5" fill="none" stroke="%s" stroke-width="2.6"/>' % blanc)
            out.append('<circle cx="12" cy="17.3" r="1.5" fill="%s"/>' % blanc)
        else:
            out.append(include(ctx, key, no, 12, 12, 14.5, 1.25, {INK: blanc}))
    elif kind in ('secours', 'incendie'):
        fond = COLORS['vert'] if kind == 'secours' else COLORS['rouge']
        out.append('<rect x="1.5" y="1.5" width="21" height="21" rx="2.2" fill="%s"/>' % fond)
        out.append(include(ctx, key, no, 12, 12, 16, 1.2, {INK: blanc}))
    return out


def render_elem(ctx, img, text, no, stroke_mul):
    body, opts = split_opts(text)
    verb, _, rest = body.partition(' ')
    args = rest.split()
    col = color(opts['coul'], no) if 'coul' in opts else INK
    width = float(opts.get('ep', '2')) * stroke_mul
    extra = ''
    if 'op' in opts:
        extra += ' opacity="%s"' % opts['op']
    if 'tirets' in opts:
        extra += ' stroke-dasharray="%s"' % opts['tirets'].replace(',', ' ')
    if opts.get('bout') == 'carre':
        extra += ' stroke-linecap="butt"'
    if opts.get('regle') == 'pairimpair':
        extra += ' fill-rule="evenodd"'
    stroke = 'fill="none" stroke="%s" stroke-width="%s"%s' % (col, num(width), extra)
    fill = 'fill="%s"%s' % (col, extra)
    if 'bord' in opts:
        fill += ' stroke="%s" stroke-width="%s"' % (color(opts['bord'], no), num(float(opts.get('epb', '1')) * stroke_mul))
    f = [float(a) for a in args] if verb in ('cercle', 'disque', 'rect', 'pave', 'ligne', 'ellipse', 'ovale',
                                              'engrenage', 'etoile', 'etoile-pleine', 'trefle', 'arcfleche', 'fleche',
                                              'etoiles-cercle', 'grille-disques') else None
    out = []
    try:
        if verb == 'trait':
            out.append('<path d="%s" %s/>' % (rest, stroke))
        elif verb == 'plein':
            out.append('<path d="%s" %s/>' % (rest, fill))
        elif verb == 'cercle':
            out.append('<circle cx="%s" cy="%s" r="%s" %s/>' % (num(f[0]), num(f[1]), num(f[2]), stroke))
        elif verb == 'disque':
            out.append('<circle cx="%s" cy="%s" r="%s" %s/>' % (num(f[0]), num(f[1]), num(f[2]), fill))
        elif verb in ('rect', 'pave'):
            r = ' rx="%s"' % num(f[4]) if len(f) > 4 else ''
            out.append('<rect x="%s" y="%s" width="%s" height="%s"%s %s/>' % (
                num(f[0]), num(f[1]), num(f[2]), num(f[3]), r, stroke if verb == 'rect' else fill))
        elif verb == 'ligne':
            out.append('<line x1="%s" y1="%s" x2="%s" y2="%s" %s/>' % (num(f[0]), num(f[1]), num(f[2]), num(f[3]), stroke))
        elif verb in ('ellipse', 'ovale'):
            out.append('<ellipse cx="%s" cy="%s" rx="%s" ry="%s" %s/>' % (
                num(f[0]), num(f[1]), num(f[2]), num(f[3]), stroke if verb == 'ellipse' else fill))
        elif verb in ('poly', 'contour', 'polygone'):
            pts = ' '.join(a.replace(',', ' ') for a in args)
            tag = 'polyline' if verb == 'poly' else 'polygon'
            out.append('<%s points="%s" %s/>' % (tag, pts, fill if verb == 'polygone' else stroke))
        elif verb == 'engrenage':
            out.append('<path d="%s" %s/>' % (gear_path(f[0], f[1], f[2], int(f[3])), stroke))
            if len(f) > 4 and f[4] > 0:
                out.append('<circle cx="%s" cy="%s" r="%s" %s/>' % (num(f[0]), num(f[1]), num(f[4]), stroke))
        elif verb in ('etoile', 'etoile-pleine'):
            pts = star_points(f[0], f[1], f[2], f[3], int(f[4]), f[5] if len(f) > 5 else -90.0)
            p = ' '.join('%s %s' % (num(x), num(y)) for x, y in pts)
            out.append('<polygon points="%s" %s/>' % (p, stroke if verb == 'etoile' else fill))
        elif verb == 'trefle':
            cx, cy, r1, r2 = f[0], f[1], f[2], f[3]
            for a in (-90, 30, 150):
                out.append('<path d="%s" %s/>' % (sector(cx, cy, r1, r2, a - 30, a + 30), fill))
            out.append('<circle cx="%s" cy="%s" r="%s" %s/>' % (num(cx), num(cy), num(r1 * 0.62), fill))
        elif verb == 'arcfleche':
            out.append('<path d="%s" %s/>' % (arc_arrow(f[0], f[1], f[2], f[3], f[4], f[5] if len(f) > 5 else 4.5), stroke))
        elif verb == 'fleche':
            ln = math.hypot(f[2] - f[0], f[3] - f[1])
            d = ((f[2] - f[0]) / ln, (f[3] - f[1]) / ln)
            out.append('<path d="M%s %sL%s %s%s" %s/>' % (num(f[0]), num(f[1]), num(f[2]), num(f[3]),
                                                       arrow_head((f[2], f[3]), d, f[4] if len(f) > 4 else 5.5), stroke))
        elif verb == 'etoiles-cercle':
            cx, cy, R, n, rs = f[0], f[1], f[2], int(f[3]), f[4]
            for k in range(n):
                a = math.radians(-90 + k * 360.0 / n)
                pts = star_points(cx + R * math.cos(a), cy + R * math.sin(a), rs, rs * 0.4, 5)
                out.append('<polygon points="%s" %s/>' % (' '.join('%s %s' % (num(x), num(y)) for x, y in pts), fill))
        elif verb == 'grille-disques':
            x0, y0, dx, dy, nx, ny, r = f[0], f[1], f[2], f[3], int(f[4]), int(f[5]), f[6]
            for j in range(ny):
                for i in range(nx):
                    out.append('<circle cx="%s" cy="%s" r="%s" %s/>' % (num(x0 + i * dx), num(y0 + j * dy), num(r), fill))
        elif verb == 'chiffre':
            digits, x, y, h = args[0], float(args[1]), float(args[2]), float(args[3])
            s = h / 10.0
            adv = 6.0 + 2.6
            total = (len(digits) * adv - 2.6) * s
            x0 = x - total / 2
            for i, d in enumerate(digits):
                if d not in DIGITS:
                    raise Fail('ligne %d : chiffre %r' % (no, d))
                tx = x0 + i * adv * s
                out.append('<g transform="translate(%s %s) scale(%s)"><path d="%s" fill="none" stroke="%s" stroke-width="%s"%s/></g>'
                           % (num(tx), num(y - h / 2), num(s), DIGITS[d], col, num(width / s), extra))
        elif verb == 'image':
            key = args[0]
            dx = float(args[1]) if len(args) > 1 else 0.0
            dy = float(args[2]) if len(args) > 2 else 0.0
            sc = float(args[3]) if len(args) > 3 else 1.0
            if key not in ctx.images:
                raise Fail('ligne %d : image inconnue %s' % (no, key))
            recolor = {INK: col} if 'coul' in opts else None
            body2 = render_image(ctx, ctx.images[key], stroke_mul * float(opts.get('fep', '1')), recolor)
            out.append('<g transform="translate(%s %s) scale(%s)">%s</g>' % (num(dx), num(dy), num(sc), body2))
        elif verb in ('danger', 'interdit', 'obligation', 'secours', 'incendie'):
            out.extend(panel(ctx, verb, args[0] if args else '-', no))
        elif verb == 'brut':
            out.append(rest)
        else:
            raise Fail('ligne %d : forme inconnue %s' % (no, verb))
    except (IndexError, ValueError):
        raise Fail('ligne %d : arguments de %s' % (no, verb))
    if 'tourne' in opts:
        cx, cy = img['w'] / 2, img['h'] / 2
        if 'centre' in opts:
            cx, cy = (float(v) for v in opts['centre'].split(','))
        group = ''.join(out)
        out = ['<g transform="rotate(%s %s %s)">%s</g>' % (a, num(cx), num(cy), group) for a in opts['tourne'].split(',')]
    return out


def build(cats, images):
    ctx = Ctx(images)
    result = []
    for cat, imgs in cats:
        if cat.startswith('_'):
            continue
        for img in imgs:
            if img['key'].startswith('_'):
                continue
            body = render_image(ctx, img)
            pw, ph = round(img['w'] * PLACE), round(img['h'] * PLACE)
            svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
                   '<g stroke-linecap="round" stroke-linejoin="round">%s</g></svg>') % (pw, ph, img['w'], img['h'], body)
            colors = set(c.upper() for c in re.findall(r'#[0-9A-Fa-f]{6}', body))
            ink = INK in colors
            mono = colors == {INK}
            result.append({'key': img['key'], 'label': img['label'], 'cat': cat, 'words': img['words'],
                           'w': pw, 'h': ph, 'ink': ink, 'mono': mono, 'svg': svg})
    return result


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
            else:
                out.append(ch)
        else:
            for b in ch.encode('utf-8'):
                out.append('\\x%02X' % b)
            hexa = True
    return '"' + ''.join(out) + '"'


def cstr_wrapped(s, indent, width=118):
    lit = cstr(s)
    if len(lit) + indent <= width:
        return lit
    parts = []
    cur = ''
    for piece in re.split(r'(?<=>)', s):
        if cur and len(cstr(cur + piece)) + indent > width:
            parts.append(cur)
            cur = piece
        else:
            cur += piece
    if cur:
        parts.append(cur)
    return ('\n' + ' ' * indent).join(cstr(p) for p in parts)


def to_cpp(result):
    lines = []
    lines.append('// =============================================================================')
    lines.append('//  hmi/HmiImageLibraryData.cpp - les images de la bibliotheque d\'images (1.14.1, lot N6)')
    lines.append('// -----------------------------------------------------------------------------')
    lines.append('//  GENERE par tools/images/generer_images.py depuis tools/images/images.txt :')
    lines.append('//  NE PAS MODIFIER A LA MAIN. Changer la description, puis :')
    lines.append('//      python3 tools/images/generer_images.py --cpp src/hmi/HmiImageLibraryData.cpp')
    lines.append('//  %d images, dessinees pour l\'application (aucune image ni police d\'icones d\'ailleurs).' % len(result))
    lines.append('// =============================================================================')
    lines.append('#include "HmiAssets.hpp"')
    lines.append('')
    lines.append('namespace hmi::imagelib {')
    lines.append('')
    lines.append('const std::vector<BuiltInImage>& builtInData() {')
    lines.append('    static const std::vector<BuiltInImage> all = {')
    for r in result:
        lines.append('        // ---- %s' % r['key'])
        lines.append('        {%s, %s, %s,' % (cstr(r['key']), cstr(r['label']), cstr(r['cat'])))
        lines.append('         %s,' % cstr_wrapped(r['words'], 9))
        lines.append('         %s, %s, %d, %d,' % ('true' if r['ink'] else 'false', 'true' if r['mono'] else 'false', r['w'], r['h']))
        lines.append('         %s},' % cstr_wrapped(r['svg'], 9))
    lines.append('    };')
    lines.append('    return all;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace hmi::imagelib')
    lines.append('')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cpp')
    ap.add_argument('--svg')
    ap.add_argument('--verifier')
    ap.add_argument('--source', default=SOURCE)
    a = ap.parse_args()
    try:
        cats, images = parse(a.source)
        result = build(cats, images)
    except Fail as e:
        sys.exit('images.txt : %s' % e)
    if a.svg:
        os.makedirs(a.svg, exist_ok=True)
        for r in result:
            with open(os.path.join(a.svg, r['key'] + '.svg'), 'w', encoding='utf-8') as f:
                f.write(r['svg'])
        with open(os.path.join(a.svg, 'index.tsv'), 'w', encoding='utf-8') as f:
            for r in result:
                f.write('%s\t%s\t%s\t%d\t%d\t%s\n' % (r['key'], r['label'], r['cat'], r['w'], r['h'],
                                                     'mono' if r['mono'] else 'encre' if r['ink'] else 'couleur'))
    text = to_cpp(result)
    if a.cpp:
        with open(a.cpp, 'w', encoding='ascii', newline='\n') as f:
            f.write(text)
    if a.verifier:
        with open(a.verifier, encoding='ascii') as f:
            if f.read() != text:
                sys.exit('%s n\'est pas a jour : python3 tools/images/generer_images.py --cpp %s' % (a.verifier, a.verifier))
    print('%d images, %d categories' % (len(result), len([c for c in cats if not c[0].startswith('_')])))


if __name__ == '__main__':
    main()
