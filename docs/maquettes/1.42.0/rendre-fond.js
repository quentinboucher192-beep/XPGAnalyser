// Rend des pages de fond (sans les bulles) : node rendre-fond.js <dossier> <page.html>...
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const dir = process.argv[2];
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1600, height: 1000 } });
  for (const f of process.argv.slice(3)) {
    await p.goto('file://' + path.join(dir, f));
    await p.evaluate(() => document.fonts.ready);
    await p.evaluate(() => document.querySelectorAll('.annot,.mk').forEach(e => e.remove()));
    await p.waitForTimeout(150);
    await p.screenshot({ path: path.join(dir, 'v', 'fond-' + f.replace('.html', '.png')) });
    console.log('fond', f);
  }
  await b.close();
})();
