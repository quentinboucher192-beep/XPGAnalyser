// =============================================================================
//  tools/guide-ihm/guide_docx.js - le guide de l'IHM au format Word
// -----------------------------------------------------------------------------
//    node guide_docx.js guide-ihm.json DOSSIER_IMAGES Guide-IHM.docx [pages.json] [edition]
//
//  Le contenu vient de guide-ihm.json (generer_guide.py --json), les captures
//  du dossier d'images (des JPEG prepares par generer_guide.py --docx, qui
//  appelle ce script deux fois : la seconde avec les numeros de page lus dans
//  le PDF, pour le sommaire).
//
//  Il faut le paquet npm "docx" (npm install docx).
// =============================================================================
'use strict';
const fs = require('fs');
const path = require('path');
const {
    AlignmentType, BorderStyle, Bookmark, Document, Footer, Header, HeadingLevel, ImageRun, InternalHyperlink,
    LevelFormat, Packer, PageBreak, PageNumber, Paragraph, ShadingType, Table, TableCell, TableRow, TabStopType,
    TabStopPosition, TextRun, WidthType,
} = require('docx');

const [, , jsonPath, imagesDir, outPath, pagesPath, editionArg] = process.argv;
if (!jsonPath || !imagesDir || !outPath) {
    console.error('usage : node guide_docx.js guide-ihm.json DOSSIER_IMAGES Guide-IHM.docx [pages.json] [edition]');
    process.exit(2);
}
const guide = JSON.parse(fs.readFileSync(jsonPath, 'utf8'));
const pages = pagesPath && fs.existsSync(pagesPath) ? JSON.parse(fs.readFileSync(pagesPath, 'utf8')) : {};
const edition = editionArg || '';

// ---- la page : A4, marges de 2 cm ------------------------------------------------
const PAGE_W = 11906, PAGE_H = 16838, MARGIN = 1134;
const CONTENT = PAGE_W - 2 * MARGIN;             // 9638 DXA
const IMG_W = 600;                               // pixels a 96 dpi : 15,9 cm
const BLUE = '1F4E79', ACCENT = '2E75B6', GREY = '595959', AMBER = 'B7791F';
const BODY = 'Calibri', MONO = 'Consolas';

// ---- le texte : **gras** et `code` ------------------------------------------------
function runs(text, base = {}) {
    const out = [];
    const re = /(\*\*[^*]+\*\*|`[^`]+`)/g;
    let at = 0, m;
    while ((m = re.exec(text)) !== null) {
        if (m.index > at) out.push(new TextRun({ text: text.slice(at, m.index), ...base }));
        const t = m[0];
        if (t.startsWith('**')) out.push(new TextRun({ text: t.slice(2, -2), bold: true, ...base }));
        else out.push(new TextRun({ text: t.slice(1, -1), font: MONO, size: 19,
                                    shading: { type: ShadingType.CLEAR, fill: 'EEF1F4', color: 'auto' }, ...base }));
        at = m.index + t.length;
    }
    if (at < text.length) out.push(new TextRun({ text: text.slice(at), ...base }));
    return out;
}

function para(text, opts = {}) {
    return new Paragraph({ children: runs(text, opts.run || {}), spacing: { after: 120, line: 276 }, ...opts.para });
}

// ---- les numeros : chapitres 1, 2... ; sujets 1.1, 1.2... --------------------------
const numbered = [];
guide.chapters.forEach((c, ci) => {
    c.number = String(ci + 1);
    c.topics.forEach((t, ti) => { t.number = `${ci + 1}.${ti + 1}`; numbered.push(t); });
});
const byKey = Object.fromEntries(numbered.map((t) => [t.key, t]));

// ---- les images --------------------------------------------------------------------
let figure = 0;
function image(file, caption) {
    const jpg = path.join(imagesDir, file.replace(/\.png$/i, '.jpg'));
    if (!fs.existsSync(jpg)) {
        console.error('capture absente : ' + file);
        return [];
    }
    figure += 1;
    const h = Math.round(IMG_W * 1080 / 1920);
    return [
        new Paragraph({
            alignment: AlignmentType.CENTER, spacing: { before: 160, after: 60 }, keepNext: true,
            children: [new ImageRun({ type: 'jpg', data: fs.readFileSync(jpg), transformation: { width: IMG_W, height: h },
                                      altText: { title: caption, description: caption, name: file } })],
        }),
        new Paragraph({
            alignment: AlignmentType.CENTER, spacing: { after: 220 },
            children: [new TextRun({ text: `Figure ${figure} – ${caption}`, italics: true, size: 18, color: GREY })],
        }),
    ];
}

// ---- un tableau ----------------------------------------------------------------------
function table(rows) {
    const n = Math.max(...rows.map((r) => r.length));
    const longest = Array.from({ length: n }, (_, k) => Math.max(...rows.map((r) => (r[k] || '').length), 4));
    // La derniere colonne prend le reste ; les autres, leur texte le plus long (bornes).
    // (la premiere colonne, en gras, un peu plus large : ses mots ne se coupent pas)
    const weights = longest.map((l, k) => (k === n - 1 ? Math.min(Math.max(l, 30), 60)
                                                       : (Math.min(Math.max(l, 8), 40) + 2) * (k === 0 ? 1.15 : 1)));
    const total = weights.reduce((a, b) => a + b, 0);
    const widths = weights.map((w) => Math.floor(CONTENT * w / total));
    widths[n - 1] += CONTENT - widths.reduce((a, b) => a + b, 0);
    const border = { style: BorderStyle.SINGLE, size: 4, color: 'C9D3DE' };
    const borders = { top: border, bottom: border, left: border, right: border };
    return new Table({
        width: { size: CONTENT, type: WidthType.DXA },
        columnWidths: widths,
        rows: rows.map((r, ri) => new TableRow({
            tableHeader: ri === 0,
            cantSplit: true,
            children: Array.from({ length: n }, (_, k) => new TableCell({
                width: { size: widths[k], type: WidthType.DXA },
                borders,
                shading: ri === 0 ? { type: ShadingType.CLEAR, fill: 'DCE6F2', color: 'auto' }
                                  : (ri % 2 === 0 ? { type: ShadingType.CLEAR, fill: 'F6F8FA', color: 'auto' } : undefined),
                margins: { top: 60, bottom: 60, left: 100, right: 100 },
                children: [new Paragraph({ spacing: { after: 0 },
                                           children: runs(r[k] || '', { size: 19, bold: ri === 0 || k === 0 }) })],
            })),
        })),
    });
}

// ---- les blocs d'un sujet ----------------------------------------------------------
let stepGroup = 0;
function blocks(t) {
    const out = [];
    let previous = '';
    for (const b of t.blocks) {
        switch (b.kind) {
            case 'heading':
                out.push(new Paragraph({ heading: HeadingLevel.HEADING_3, keepNext: true, children: [new TextRun(b.text)] }));
                break;
            case 'paragraph':
                out.push(para(b.text));
                break;
            case 'bullet':
                out.push(new Paragraph({ numbering: { reference: 'puces', level: 0 }, spacing: { after: 60 }, children: runs(b.text) }));
                break;
            case 'step':
                if (previous !== 'step') stepGroup += 1;
                out.push(new Paragraph({ numbering: { reference: 'etapes', level: 0, instance: stepGroup },
                                         spacing: { after: 60 }, children: runs(b.text) }));
                break;
            case 'code': {
                // 1.10 : un exemple a plusieurs notations (ST, C, C++) : sur le
                // papier, pas de selecteur - chaque notation, son nom au-dessus.
                if (b.label) {
                    out.push(new Paragraph({ keepNext: true, spacing: { before: previous === 'code' ? 60 : 120, after: 0 },
                                             children: [new TextRun({ text: b.label, bold: true, color: ACCENT, size: 16 })] }));
                }
                const lines = b.text.split('\n');
                lines.forEach((l, i) => out.push(new Paragraph({
                    keepNext: i < lines.length - 1, keepLines: true,
                    spacing: { before: i === 0 ? 120 : 0, after: i === lines.length - 1 ? 160 : 0, line: 240 },
                    shading: { type: ShadingType.CLEAR, fill: 'F4F6F8', color: 'auto' },
                    border: { left: { style: BorderStyle.SINGLE, size: 18, color: ACCENT, space: 6 } },
                    indent: { left: 170 },
                    children: [new TextRun({ text: l.length ? l : ' ', font: MONO, size: 17 })],
                })));
                break;
            }
            case 'tip':
            case 'warning': {
                const warn = b.kind === 'warning';
                out.push(new Paragraph({
                    spacing: { before: 120, after: 160 },
                    shading: { type: ShadingType.CLEAR, fill: warn ? 'FFF4E0' : 'EAF2FB', color: 'auto' },
                    border: { left: { style: BorderStyle.SINGLE, size: 24, color: warn ? AMBER : ACCENT, space: 8 } },
                    indent: { left: 170, right: 113 },
                    children: [new TextRun({ text: warn ? 'Attention : ' : 'Astuce : ', bold: true, color: warn ? AMBER : ACCENT }),
                               ...runs(b.text)],
                }));
                break;
            }
            case 'table':
                out.push(table(b.rows));
                out.push(new Paragraph({ spacing: { after: 120 }, children: [] }));
                break;
            default:
                break;
        }
        previous = b.kind;
    }
    return out;
}

// ---- le sommaire (statique : les numeros de page viennent du premier passage) -------
function toc() {
    const out = [new Paragraph({ spacing: { after: 240 }, children: [new TextRun({ text: 'Sommaire', bold: true, size: 40, color: BLUE })] })];
    const line = (label, anchor, page, level) => new Paragraph({
        tabStops: [{ type: TabStopType.RIGHT, position: CONTENT, leader: 'dot' }],
        indent: { left: level === 1 ? 0 : 400 },
        spacing: { before: level === 1 ? 160 : 20, after: 20 },
        children: [new InternalHyperlink({ anchor, children: [new TextRun({ text: label, bold: level === 1,
                                                                            color: level === 1 ? BLUE : '262626' })] }),
                   new TextRun({ text: `\t${page || '…'}`, bold: level === 1 })],
    });
    for (const c of guide.chapters) {
        out.push(line(`${c.number}. ${c.name}`, `c${c.number}`, pages[`c${c.number}`], 1));
        for (const t of c.topics) out.push(line(`${t.number} ${t.title}`, `t_${t.key}`, pages[t.key], 2));
    }
    return out;
}

// ---- le document ------------------------------------------------------------------
const children = [];
// La couverture : le logo de l'application (lot 12), puis le titre.
{
    const logo = path.join(__dirname, '..', '..', 'resources', 'logo', 'xpg_analyzer_256.png');
    if (fs.existsSync(logo))
        children.push(new Paragraph({ spacing: { before: 1200, after: 240 }, children: [new ImageRun({
            type: 'png', data: fs.readFileSync(logo), transformation: { width: 110, height: 110 },
            altText: { title: 'XPG Analyzer', description: 'Le logo de l’application', name: 'xpg_analyzer_256.png' } })] }));
}
children.push(new Paragraph({ spacing: { before: 400, after: 200 },
                              children: [new TextRun({ text: 'Guide de l’IHM', bold: true, size: 72, color: BLUE })] }));
children.push(new Paragraph({ spacing: { after: 480 }, children: [new TextRun({
    text: 'Module IHM intégré : concevoir, programmer, superviser et tester l’interface opérateur',
    size: 30, color: GREY })] }));
children.push(...image('PNG_156_simulation_tableau_courbe_image.png', 'Une vue en marche : tableau à cases dynamiques, courbe à deux plumes, image animée.').slice(0, 1));
figure = 0;                                       // la couverture ne compte pas
children.push(new Paragraph({ spacing: { before: 480, after: 120 }, children: [new TextRun({
    text: 'Ce guide reprend, page pour page, l’aide F1 de l’application (le volet Aide de l’IHM), illustrée de captures faites dans l’application en marche.',
    size: 22, color: GREY })] }));
if (edition)
    children.push(new Paragraph({ children: [new TextRun({ text: edition, size: 22, color: GREY })] }));
children.push(new Paragraph({ children: [new PageBreak()] }));
children.push(...toc());

for (const c of guide.chapters) {
    children.push(new Paragraph({
        heading: HeadingLevel.HEADING_1, pageBreakBefore: true,
        children: [new Bookmark({ id: `c${c.number}`, children: [new TextRun(`${c.number}. ${c.name}`)] })],
    }));
    for (const t of c.topics) {
        children.push(new Paragraph({
            heading: HeadingLevel.HEADING_2, keepNext: true,
            children: [new Bookmark({ id: `t_${t.key}`, children: [new TextRun(`${t.number} ${t.title}`)] })],
        }));
        children.push(new Paragraph({
            spacing: { after: 200, line: 288 },
            border: { left: { style: BorderStyle.SINGLE, size: 18, color: ACCENT, space: 8 } },
            indent: { left: 170 },
            children: runs(t.summary, { italics: true, size: 23, color: '404040' }),
        }));
        const shots = t.shots || [];
        if (shots.length) children.push(...image(shots[0].file, shots[0].caption));
        children.push(...blocks(t));
        for (const s of shots.slice(1)) children.push(...image(s.file, s.caption));
        if (t.see && t.see.length) {
            const links = [new TextRun({ text: 'Voir aussi : ', bold: true, color: GREY, size: 20 })];
            t.see.forEach((k, i) => {
                const o = byKey[k];
                if (!o) return;
                if (i) links.push(new TextRun({ text: ' · ', color: GREY, size: 20 }));
                links.push(new InternalHyperlink({ anchor: `t_${k}`,
                                                   children: [new TextRun({ text: `${o.number} ${o.title}`, color: ACCENT, size: 20 })] }));
            });
            children.push(new Paragraph({ spacing: { before: 120, after: 360 }, children: links }));
        }
    }
}

const doc = new Document({
    creator: 'Module IHM',
    title: 'Guide de l’IHM',
    description: 'Le guide du module IHM : l’aide F1, illustrée.',
    styles: {
        default: { document: { run: { font: BODY, size: 21 } } },
        paragraphStyles: [
            { id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true,
              run: { size: 40, bold: true, color: BLUE, font: BODY },
              paragraph: { spacing: { before: 0, after: 320 }, outlineLevel: 0,
                           border: { bottom: { style: BorderStyle.SINGLE, size: 12, color: ACCENT, space: 6 } } } },
            { id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true,
              run: { size: 30, bold: true, color: BLUE, font: BODY },
              paragraph: { spacing: { before: 360, after: 140 }, outlineLevel: 1 } },
            { id: 'Heading3', name: 'Heading 3', basedOn: 'Normal', next: 'Normal', quickFormat: true,
              run: { size: 24, bold: true, color: ACCENT, font: BODY },
              paragraph: { spacing: { before: 220, after: 100 }, outlineLevel: 2 } },
        ],
    },
    numbering: {
        config: [
            { reference: 'puces', levels: [{ level: 0, format: LevelFormat.BULLET, text: '•', alignment: AlignmentType.LEFT,
                                             style: { paragraph: { indent: { left: 540, hanging: 280 } } } }] },
            { reference: 'etapes', levels: [{ level: 0, format: LevelFormat.DECIMAL, text: '%1.', alignment: AlignmentType.LEFT,
                                              style: { paragraph: { indent: { left: 540, hanging: 320 } } } }] },
        ],
    },
    sections: [{
        properties: { page: { size: { width: PAGE_W, height: PAGE_H },
                              margin: { top: MARGIN, bottom: MARGIN, left: MARGIN, right: MARGIN, header: 567, footer: 567 } },
                      titlePage: true },
        headers: {
            default: new Header({ children: [new Paragraph({ alignment: AlignmentType.RIGHT,
                children: [new TextRun({ text: 'Guide de l’IHM', size: 16, color: '8C8C8C' })] })] }),
            first: new Header({ children: [new Paragraph({ children: [] })] }),
        },
        footers: {
            default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER,
                children: [new TextRun({ children: ['Page ', PageNumber.CURRENT, ' / ', PageNumber.TOTAL_PAGES], size: 16, color: '8C8C8C' })] })] }),
            first: new Footer({ children: [new Paragraph({ children: [] })] }),
        },
        children,
    }],
});

Packer.toBuffer(doc).then((buf) => {
    fs.writeFileSync(outPath, buf);
    console.log(`Word : ${outPath} (${numbered.length} sujets, ${figure} figures)`);
});
