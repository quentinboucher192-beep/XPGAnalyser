// Rend chaque page HTML de la maquette en PNG (1600x1000, x2 pour la livraison ; x1 pour la verification)
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const dir = process.argv[2], scale = Number(process.argv[3] || 1), only = process.argv.slice(4);
  const fs = require('fs');
  const pages = fs.readdirSync(dir).filter(f => /^\d\d-.*\.html$/.test(f) && (!only.length || only.some(o => f.startsWith(o))));
  const b = await chromium.launch();
  const ctx = await b.newContext({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: scale });
  for (const f of pages) {
    const p = await ctx.newPage();
    p.on('console', m => console.log(f, 'console:', m.text()));
    p.on('pageerror', e => console.log(f, 'ERREUR:', e.message));
    await p.goto('file://' + path.join(dir, f));
    await p.evaluate(() => document.fonts.ready);
    await p.waitForTimeout(150);
    const h = await p.evaluate(() => Number(document.body.dataset.h || 0));
    if (h) await p.setViewportSize({ width: 1600, height: h });
    const out = path.join(dir, (scale > 1 ? 'hd/' : 'v/') + f.replace('.html', '.png'));
    fs.mkdirSync(path.dirname(out), { recursive: true });
    await p.screenshot({ path: out });
    console.log('ok', out);
    await p.close();
  }
  await b.close();
})();
