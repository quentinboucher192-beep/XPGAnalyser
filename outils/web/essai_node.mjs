// =============================================================================
//  outils/web/essai_node.mjs - 1.12.31 : le moteur wasm de l'IHM, sous Node
// -----------------------------------------------------------------------------
//  Sans navigateur : charge ihm-web.wasm (un module WASI "reactor"), lui donne un
//  petit projet d'essai (une archive .xihm), demarre l'IHM, l'avance de quelques
//  cycles, et verifie qu'un script cyclique fait monter un compteur, qu'une
//  consigne saisie se relit, et que le journal se remplit.
//
//    node essai_node.mjs <ihm-web.wasm> <essai.xihm>
//
//  Sort 0 si tout va, 1 sinon. (Les essais ctest le sautent si la chaine wasm
//  n'a pas produit le .wasm.)
// =============================================================================
import { readFileSync } from 'node:fs';
import { WASI } from 'node:wasi';
import process from 'node:process';

const wasmPath = process.argv[2];
const xihmPath = process.argv[3];
if (!wasmPath || !xihmPath) {
  console.error('usage: node essai_node.mjs <ihm-web.wasm> <essai.xihm>');
  process.exit(2);
}

const wasi = new WASI({ version: 'preview1', args: [], env: {}, preopens: {} });

// Les imports env que le module peut demander. Les scripts DETACHES (WAIT) sont des
// coroutines Asyncify : le moteur les suspend et les reprend par ces fonctions, qui
// pilotent les exports asyncify_* du module (voir src/web/WebFiber.cpp).
let EX = null;
const envImports = {
  xpg_host_sleep: (buf) => {
    const st = EX.asyncify_get_state();
    if (st === 0) EX.asyncify_start_unwind(buf);   // suspendre
    else if (st === 2) EX.asyncify_stop_rewind();  // repris : continuer apres le WAIT
  },
  xpg_fiber_stop_unwind: () => EX.asyncify_stop_unwind(),
  xpg_fiber_start_rewind: (buf) => EX.asyncify_start_rewind(buf),
  xpg_fiber_state: () => EX.asyncify_get_state(),
  xpg_get_sp: () => EX.__stack_pointer.value,
  xpg_set_sp: (v) => { EX.__stack_pointer.value = v; },
};

const bytes = readFileSync(wasmPath);
const module = await WebAssembly.compile(bytes);
const need = WebAssembly.Module.imports(module);
const imports = { wasi_snapshot_preview1: wasi.wasiImport, env: {} };
const missing = [];
for (const im of need) {
  if (im.module === 'env' && im.kind === 'function') {
    imports.env[im.name] = envImports[im.name] || (() => 0);
    if (!envImports[im.name]) missing.push(im.name);
  }
}
// Ce que l'edition des liens a laisse sans definition (des fonctions du bureau que la page
// n'appelle pas) : la liste, pour la voir changer d'une version a l'autre.
console.log('fonctions laissees au dehors du module : ' + missing.length + (missing.length ? ' (' + missing.slice(0, 12).join(', ')
            + (missing.length > 12 ? ', ...' : '') + ')' : ''));
const instance = await WebAssembly.instantiate(module, imports);
EX = instance.exports;
wasi.initialize(instance); // reactor : _initialize, pas _start

const ex = instance.exports;
const mem = () => new Uint8Array(ex.memory.buffer);
const dec = new TextDecoder();
const enc = new TextEncoder();

function cstr(ptr) {
  const m = mem();
  let end = ptr;
  while (m[end] !== 0) end++;
  return dec.decode(m.subarray(ptr, end));
}
function pushBytes(buf) {
  const ptr = ex.xpg_malloc(buf.length);
  mem().set(buf, ptr);
  return ptr;
}
function pushStr(s) {
  const b = enc.encode(s + '\0');
  const ptr = ex.xpg_malloc(b.length);
  mem().set(b, ptr);
  return ptr;
}

let failures = 0;
function check(cond, label) {
  console.log((cond ? 'ok   ' : 'ECHEC') + ' : ' + label);
  if (!cond) failures++;
}

console.log('version du moteur :', cstr(ex.xpg_version()));

const xihm = readFileSync(xihmPath);
const zp = pushBytes(xihm);
const loaded = ex.xpg_load(zp, xihm.length);
ex.xpg_free(zp);
check(loaded === 1, 'le projet se charge (' + cstr(ex.xpg_last_error()) + ')');
if (!loaded) process.exit(1);

ex.xpg_start(0);
check(ex.xpg_running() === 1, 'l\'IHM tourne apres start()');
check(ex.xpg_current_view() !== 0, 'une vue de demarrage est ouverte (id=' + ex.xpg_current_view() + ')');

function read(name) {
  const p = pushStr(name);
  const r = cstr(ex.xpg_read(p));
  ex.xpg_free(p);
  return r;
}
function write(name, value) {
  const pn = pushStr(name), pv = pushStr(value);
  const r = ex.xpg_write(pn, pv);
  ex.xpg_free(pn); ex.xpg_free(pv);
  return r;
}

const before = parseInt(read('Compteur'), 10) || 0;
let t = 0;
for (let i = 0; i < 20; i++) { t += 0.1; ex.xpg_tick(t); }
const after = parseInt(read('Compteur'), 10) || 0;
check(after > before, 'le script cyclique fait monter Compteur (' + before + ' -> ' + after + ')');

// 1.13.0 (lot J3) : JSON et std::regex tournent DANS la page (le script Donnees, cyclique).
check(read('Gaz') === 'Helium', 'JSON_LIRE dans la page : Gaz = ' + read('Gaz') + ' (attendu Helium)');
check(parseFloat(read('Pression').replace(',', '.')) === 7.5, 'JSON_NOMBRE dans la page : Pression = ' + read('Pression') + ' (attendu 7.5)');
// Un motif invalide leve E_REGEX, rattrape par TRY : la page ne plante pas (std::regex sans exceptions en WASM).
check(read('CodeErr') === 'E_REGEX', 'un regex invalide leve E_REGEX, rattrape, sans planter la page (CodeErr = ' + read('CodeErr') + ')');

check(write('Consigne', '25,5') === 1, 'une consigne se saisit (Consigne := 25,5)');
check(read('Consigne') === '25.5', 'la consigne se relit (' + read('Consigne') + ')');

// L'alarme Compteur > 50 : on avance assez pour la lever.
for (let i = 0; i < 400; i++) { t += 0.1; ex.xpg_tick(t); }
const journal = ex.xpg_journal ? cstr(ex.xpg_journal()) : '';
check(journal.length > 0, 'le journal se remplit (' + journal.split('\n').length + ' lignes)');
check(journal.includes('Compteur au-dessus de 50'), 'l\'alarme Seuil (Compteur > 50) apparait');

// Le script DETACHE Chrono : WHILE TRUE ... IF Marche THEN Secondes+1 ... WAIT(100 ms).
// Il doit tourner EN MEME TEMPS que l'IHM (qui continue son cycle) : Asyncify.
check(ex.xpg_running_scripts() === 1, 'le script detache Chrono tourne (suspendu sur son WAIT)');
check(write('Marche', 'TRUE') === 1, 'Marche := TRUE');
const s0 = parseInt(read('Secondes'), 10) || 0, c0 = parseInt(read('Compteur'), 10) || 0;
for (let i = 0; i < 30; i++) { t += 0.1; ex.xpg_tick(t); }      // 3 s
const s1 = parseInt(read('Secondes'), 10) || 0, c1 = parseInt(read('Compteur'), 10) || 0;
check(s1 - s0 >= 20 && s1 - s0 <= 40, 'le WAIT detache laisse compter Secondes (' + s0 + ' -> ' + s1 + ', ~30 attendus)');
check(c1 - c0 >= 25, 'pendant ce temps l\'IHM continue (Compteur ' + c0 + ' -> ' + c1 + ')');
write('Marche', 'FALSE');
const s2 = parseInt(read('Secondes'), 10) || 0;
for (let i = 0; i < 10; i++) { t += 0.1; ex.xpg_tick(t); }
check(parseInt(read('Secondes'), 10) === s2, 'Marche := FALSE : Secondes s\'arrete (' + s2 + ')');

ex.xpg_stop(t);
check(ex.xpg_running_scripts() === 0, 'l\'arret de l\'IHM arrete le script detache');
console.log(failures === 0 ? 'TOUT VA' : (failures + ' ECHEC(S)'));
process.exit(failures === 0 ? 0 : 1);
