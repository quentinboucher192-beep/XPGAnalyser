// =============================================================================
//  outils/web/essai_page.mjs - 1.12.31 : la page web EN MARCHE dans Chromium
// -----------------------------------------------------------------------------
//  Fait la page du projet d'essai (tests/fixtures/web/essai : faire_page.py, le
//  gabarit de l'export), l'ouvre dans Chromium (Playwright) et verifie :
//    aucune erreur JS ; la vue se dessine ; elle change en simulation (le script
//    cyclique) ; un clic de navigation, et le retour ; une popup ouverte et fermee ;
//    une consigne saisie au clavier ; un curseur glisse ; le WAIT d'un script
//    detache ; l'alarme qui apparait ; la documentation du projet (la barre) ; la
//    consigne remanente rendue apres un rechargement (la memoire du navigateur).
//  XPG_CAPTURES=<dossier> : les captures de chaque etape y sont ecrites (et, avec
//  XPG_IMAGES=1, une image toutes les 100 ms pour en faire une animation).
//
//    node essai_page.mjs <racine du depot> [page.html]
//  Une page donnee (celle qu'IHM Studio a exportee) : ouverte, dessinee, sans
//  erreur JS, ses temps mesures - les etapes du projet d'essai sont sautees.
//  Sort 0 si tout va, 1 sinon, 77 si Playwright ou Chromium manque (ctest : saute).
// =============================================================================
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const require = createRequire(import.meta.url);
const root = process.argv[2] || process.cwd();
let chromium;
try {
  ({ chromium } = require(process.env.XPG_PLAYWRIGHT || '/opt/node22/lib/node_modules/playwright'));
} catch (e) {
  console.log('essai saute : Playwright absent');
  process.exit(77);
}
if (!process.env.PLAYWRIGHT_BROWSERS_PATH && fs.existsSync('/opt/pw-browsers')) process.env.PLAYWRIGHT_BROWSERS_PATH = '/opt/pw-browsers';

// ---- la page du projet d'essai (ou celle donnee) ------------------------------
const given = process.argv[3] ? path.resolve(process.argv[3]) : '';
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'xpg-page-'));
const xihm = path.join(tmp, 'essai.xihm');
const html = given || path.join(tmp, 'essai.html');
if (!given) {
execFileSync('python3', ['-c', `
import os, zipfile, sys
src = sys.argv[1]
with zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED) as z:
    for base, _, fs in os.walk(src):
        for f in sorted(fs):
            p = os.path.join(base, f); z.write(p, os.path.relpath(p, src).replace(os.sep, '/'))
`, path.join(root, 'tests', 'fixtures', 'web', 'essai'), xihm]);
execFileSync('python3', [path.join(root, 'outils', 'web', 'faire_page.py'), xihm, html, 'Essai web'], { stdio: 'inherit' });
}

const caps = process.env.XPG_CAPTURES || '';
if (caps) fs.mkdirSync(caps, { recursive: true });
let failures = 0;
const check = (ok, label) => { console.log((ok ? 'ok   ' : 'ECHEC') + ' : ' + label); if (!ok) failures++; };

let browser;
try {
  browser = await chromium.launch({ args: ['--disable-background-networking', '--no-first-run'] });
} catch (e) {
  console.log('essai saute : Chromium ne demarre pas (' + String(e).split('\n')[0] + ')');
  process.exit(77);
}
const page = await (await browser.newContext({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1 })).newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
const shot = async (name) => { if (caps) await page.screenshot({ path: path.join(caps, name) }); };

const t0 = Date.now();
await page.goto('file://' + html);
await page.waitForFunction(() => window.XpgIhm && window.XpgIhm.frames() > 2, null, { timeout: 30000 });
const openMs = Date.now() - t0;
check(true, 'la page s\'ouvre et l\'IHM tourne (' + openMs + ' ms)');
await page.waitForTimeout(400);
await shot('1_ouverture.png');

// La vue se dessine : des pixels qui ne sont pas le noir du fond.
const lit = async () => page.evaluate(() => {
  const c = document.getElementById('ihm-canvas'), x = c.getContext('2d');
  const d = x.getImageData(0, 0, c.width, c.height).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 16) if (d[i] + d[i + 1] + d[i + 2] > 60) n++;
  return n;
});
check((await lit()) > 1000, 'la vue se dessine (des pixels allumes)');

// Une image toutes les 100 ms (XPG_IMAGES=1) : de quoi faire une animation.
let film = null, filmN = 0;
if (caps && process.env.XPG_IMAGES) {
  fs.mkdirSync(path.join(caps, 'film'), { recursive: true });
  film = setInterval(() => { page.screenshot({ path: path.join(caps, 'film', String(filmN++).padStart(4, '0') + '.png') }).catch(() => {}); }, 100);
}
const finish = async () => {
  if (film) clearInterval(film);
  const frameMs = await page.evaluate(() => XpgIhm.lastFrameMs());
  const avgMs = await page.evaluate(() => (XpgIhm.averageFrameMs ? XpgIhm.averageFrameMs() : 0));
  const frames = await page.evaluate(() => XpgIhm.frames());
  console.log('temps d\'ouverture : ' + openMs + ' ms ; une image : ' + avgMs.toFixed(1) + ' ms en moyenne (la derniere '
              + frameMs.toFixed(1) + ' ms, ' + frames + ' images) ; page : ' + (fs.statSync(html).size / 1048576).toFixed(2) + ' Mo');
  check(errors.length === 0, 'aucune erreur JS' + (errors.length ? ' : ' + errors.slice(0, 3).join(' | ') : ''));
  await browser.close();
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log(failures === 0 ? 'TOUT VA' : failures + ' ECHEC(S)');
  process.exit(failures === 0 ? 0 : 1);
};
if (given) {
  // Une page donnee : elle tourne, ses images se suivent ; la vue ouverte et la barre.
  const f0 = await page.evaluate(() => XpgIhm.frames());
  await page.waitForTimeout(1500);
  const f1 = await page.evaluate(() => XpgIhm.frames());
  check(f1 > f0 + 10, 'les images se suivent (' + (f1 - f0) + ' en 1,5 s)');
  console.log('vue ouverte : ' + (await page.evaluate(() => XpgIhm.view())) + ' ; barre : '
              + (await page.evaluate(() => document.getElementById('barre').innerText.replace(/\s+/g, ' '))));
  await shot('page_donnee.png');
  await finish();
}

// Elle change en simulation : le texte "Compteur : N" avance.
const sig = async () => page.evaluate(() => {
  const c = document.getElementById('ihm-canvas'), x = c.getContext('2d');
  const d = x.getImageData(0, 0, Math.min(c.width, 700), Math.min(c.height, 160)).data;
  let h = 0;
  for (let i = 0; i < d.length; i += 4) h = (h * 31 + d[i]) | 0;
  return h;
});
const c1 = await page.evaluate(() => XpgIhm.read('Compteur'));
const s1 = await sig();
await page.waitForTimeout(800);
const c2 = await page.evaluate(() => XpgIhm.read('Compteur'));
const s2 = await sig();
check(Number(c2) > Number(c1), 'le script cyclique fait monter Compteur (' + c1 + ' -> ' + c2 + ')');
check(s1 !== s2, 'le dessin change (le texte de Compteur suit)');

const clickObj = async (name) => {
  const p = await page.evaluate((n) => XpgIhm.objectCenter(n), name);
  if (!p) return false;
  const box = await page.locator('#ihm-canvas').boundingBox();
  await page.mouse.click(box.x + p.x, box.y + p.y);
  await page.waitForTimeout(350);
  return true;
};

// La navigation : Detail, puis Retour.
check(await clickObj('Vers_Detail'), 'le bouton Detail est a l\'ecran');
await page.waitForTimeout(700);           // la transition
check((await page.evaluate(() => XpgIhm.view())) === 'Detail', 'un clic de navigation ouvre la vue Detail');
await shot('2_navigation.png');
check(await clickObj('Retour'), 'le bouton Retour est a l\'ecran');
await page.waitForTimeout(700);
check((await page.evaluate(() => XpgIhm.view())) === 'Accueil', 'Retour ramene a la vue Accueil');

// Une popup : Info l'ouvre, Fermer la ferme.
check(await clickObj('Info'), 'le bouton Info est a l\'ecran');
await page.waitForTimeout(500);
check((await page.evaluate(() => XpgIhm.objectCenter('Fermer_Info'))) !== null, 'la popup Information s\'ouvre');
await shot('3_popup.png');
await clickObj('Fermer_Info');
await page.waitForTimeout(500);
check((await page.evaluate(() => XpgIhm.objectCenter('Fermer_Info'))) === null, 'Fermer referme la popup');

// Une consigne saisie : le champ prend le clavier, on tape 42,5 puis Entree.
check(await clickObj('Saisie_Consigne'), 'le champ de la consigne est a l\'ecran');
await page.keyboard.press('Control+A').catch(() => {});
for (let i = 0; i < 6; i++) await page.keyboard.press('Backspace');
await page.keyboard.type('42,5');
await shot('4_saisie.png');
await page.keyboard.press('Enter');
await page.waitForTimeout(300);
const cons = await page.evaluate(() => XpgIhm.read('Consigne'));
check(Number(cons) === 42.5, 'la consigne saisie au clavier arrive dans la variable (' + cons + ')');

// Un curseur : il glisse, et Vitesse suit.
const slider = await page.evaluate(() => XpgIhm.objectBox('Curseur_Vitesse'));
check(slider !== null, 'le curseur Vitesse est a l\'ecran');
if (slider) {
  const box = await page.locator('#ihm-canvas').boundingBox();
  const y = box.y + slider.y + slider.h / 2;
  await page.mouse.move(box.x + slider.x + slider.w * 0.05, y);
  await page.mouse.down();
  for (let i = 1; i <= 10; i++) { await page.mouse.move(box.x + slider.x + slider.w * (0.05 + 0.075 * i), y); await page.waitForTimeout(30); }
  await page.mouse.up();
  await page.waitForTimeout(300);
  const v = Number(await page.evaluate(() => XpgIhm.read('Vitesse')));
  check(v >= 60, 'le curseur glisse : Vitesse suit (' + v + ' %)');
}

// Le script detache : Marche / Arret, et Secondes compte (WAIT, l'IHM continue).
const sec0 = Number(await page.evaluate(() => XpgIhm.read('Secondes')));
await clickObj('Marche_Bouton');
await page.waitForTimeout(1500);
const sec1 = Number(await page.evaluate(() => XpgIhm.read('Secondes')));
check(sec1 > sec0 + 5, 'le script detache (WAIT) compte pendant que l\'IHM tourne (' + sec0 + ' -> ' + sec1 + ')');
await shot('5_detache.png');

// L'alarme : Compteur passe 50 (5 s apres le demarrage), le bandeau la montre.
await page.waitForFunction(() => Number(XpgIhm.read('Compteur')) > 55, null, { timeout: 20000 }).catch(() => {});
await page.waitForTimeout(400);
const journal = await page.evaluate(() => XpgIhm.journal());
check(journal.includes('Compteur au-dessus de 50'), 'l\'alarme Seuil apparait (le journal la note)');
await shot('6_alarme.png');

// La documentation du projet : Alarmes, dans la barre du bas.
await page.click('button[data-doc="alarmes"]');
await page.waitForTimeout(600);
const docOk = await page.evaluate(() => {
  const f = document.getElementById('doc-cadre');
  const d = f && f.contentDocument;
  return !document.getElementById('doc').hidden && !!d && !!d.getElementById('alarmes') && d.body.innerText.includes('Compteur au-dessus de 50');
});
check(docOk, 'la barre ouvre la documentation du projet : la liste des alarmes');
await shot('7_documentation.png');
await page.keyboard.press('Escape');
await page.waitForTimeout(200);
check(await page.evaluate(() => document.getElementById('doc').hidden), 'Echap referme la documentation');
const bar = await page.evaluate(() => document.getElementById('barre').innerText.replace(/\s+/g, ' '));
console.log('barre : ' + bar);
check(bar.includes('Accueil') && bar.includes('Simulation'), 'la barre dit le mode et la vue ouverte');

// La memoire du navigateur : la consigne (remanente) revient apres un rechargement de la page.
await page.waitForTimeout(2500);            // gardee toutes les 2 s
await page.reload();
await page.waitForFunction(() => window.XpgIhm && window.XpgIhm.frames() > 2, null, { timeout: 30000 });
const consAfter = await page.evaluate(() => XpgIhm.read('Consigne'));
check(Number(consAfter) === 42.5, 'la consigne (remanente) revient apres un rechargement : la memoire du navigateur (' + consAfter + ')');
check((await page.evaluate(() => XpgIhm.journal())).includes('m\u00e9moire du navigateur'), 'le journal dit que les remanentes sont rendues');
await finish();
