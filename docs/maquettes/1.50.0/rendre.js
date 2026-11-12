// Rend chaque planche HTML de la maquette 1.50.0 en PNG : 1600 px de large, la hauteur de la page.
// node rendre.js <dossier> 1   -> v/  (pour verifier)
// node rendre.js <dossier> 2   -> hd/ (a livrer, @2x)
// node rendre.js <dossier> 2 03 -> seulement les planches qui commencent par 03
const { chromium } = require('playwright');
const path = require('path');
const fs = require('fs');
(async () => {
  const dir = path.resolve(process.argv[2] || __dirname), scale = Number(process.argv[3] || 1), only = process.argv.slice(4);
  const pages = fs.readdirSync(dir).filter(f => /^\d\d-.*\.html$/.test(f) && (!only.length || only.some(o => f.startsWith(o))));
  const b = await chromium.launch();
  const ctx = await b.newContext({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: scale });
  for (const f of pages) {
    const p = await ctx.newPage();
    p.on('console', m => console.log(f, 'console:', m.text()));
    p.on('pageerror', e => console.log(f, 'ERREUR:', e.message));
    await p.goto('file://' + path.join(dir, f));
    await p.evaluate(() => document.fonts.ready);
    await p.waitForTimeout(120);
    const h = await p.evaluate(() => Math.ceil(document.documentElement.scrollHeight));
    const w = await p.evaluate(() => Math.ceil(document.documentElement.scrollWidth));
    if (w > 1600) console.log(f, 'ATTENTION : plus large que 1600 px :', w);
    await p.setViewportSize({ width: 1600, height: h });
    const out = path.join(dir, (scale > 1 ? 'hd/' : 'v/') + f.replace('.html', '.png'));
    fs.mkdirSync(path.dirname(out), { recursive: true });
    await p.screenshot({ path: out, fullPage: true });
    console.log('ok', out, '1600 x', h, '@' + scale);
    await p.close();
  }
  await b.close();
})();
