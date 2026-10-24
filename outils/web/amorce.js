// =============================================================================
//  outils/web/amorce.js - 1.12.31 : le demarrage de la page web EN MARCHE
// -----------------------------------------------------------------------------
//  Lit ce que la page embarque (le moteur wasm compresse, la police, l'archive du
//  projet), lance l'IHM (XpgWebHost.run) et tient la barre du bas a jour : le
//  mode, les equipements (pas relies dans cette version de la page), l'utilisateur,
//  la vue ; les listes du projet (alarmes, variables, scripts, fiche) et le
//  telechargement du projet.
// =============================================================================
(function () {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const etat = $('etat');
  const say = (t) => { etat.textContent = t; etat.hidden = false; };

  // ---- les listes du projet (la documentation de la 1.12.30) -----------------
  //  Une page a part (alarmes, variables, scripts, fiche), dans un cadre : son style
  //  et ses scripts ne touchent pas l'IHM en marche.
  const doc = $('doc'), cadre = $('doc-cadre');
  const titres = { alarmes: 'Alarmes', variables: 'Variables IHM', scripts: 'Scripts et fonctions', fiche: 'Fiche du projet' };
  function showDoc(id) {
    const block = $('xpg-doc');
    if (!block) return;
    $('doc-titre').textContent = titres[id] || id;
    const go = () => { try { cadre.contentWindow.location.hash = '#' + id; } catch (_) {} };
    if (!cadre.getAttribute('data-pret')) {
      cadre.setAttribute('data-pret', '1');
      cadre.addEventListener('load', go, { once: true });
      cadre.srcdoc = new TextDecoder().decode(XpgWebHost.fromBase64(block.textContent));
    } else {
      go();
    }
    doc.hidden = false;
  }
  document.querySelectorAll('[data-doc]').forEach((b) => b.addEventListener('click', () => showDoc(b.getAttribute('data-doc'))));
  $('doc-fermer').addEventListener('click', () => { doc.hidden = true; });
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !doc.hidden) { doc.hidden = true; e.stopPropagation(); } }, true);

  // ---- telecharger le projet (comme la 1.12.30) ------------------------------
  const dl = $('dl');
  if (dl) dl.addEventListener('click', () => {
    const s = $('xpg-archive');
    if (!s) return;
    const bytes = XpgWebHost.fromBase64(s.textContent);
    const url = URL.createObjectURL(new Blob([bytes], { type: 'application/zip' }));
    const a = document.createElement('a');
    a.href = url; a.download = s.getAttribute('data-nom') || 'projet.xihm';
    document.body.appendChild(a); a.click();
    setTimeout(() => { URL.revokeObjectURL(url); a.remove(); }, 2000);
  });

  // ---- la memoire du navigateur : les variables remanentes, par projet --------
  //  Comme le poste d'exploitation sur son disque : les variables marquees Remanente
  //  sont gardees (IndexedDB, dans ce navigateur) et rendues a l'ouverture suivante.
  const archiveTag = $('xpg-archive');
  const memKey = 'ihm:' + ((archiveTag && archiveTag.getAttribute('data-nom')) || document.title);
  function db() {
    return new Promise((ok, ko) => {
      try {
        const r = indexedDB.open('xpg-ihm', 1);
        r.onupgradeneeded = () => r.result.createObjectStore('remanence');
        r.onsuccess = () => ok(r.result);
        r.onerror = () => ko(r.error);
      } catch (e) { ko(e); }
    });
  }
  async function memRead() {
    try {
      const d = await db();
      return await new Promise((ok) => {
        const q = d.transaction('remanence').objectStore('remanence').get(memKey);
        q.onsuccess = () => ok(typeof q.result === 'string' ? q.result : '');
        q.onerror = () => ok('');
      });
    } catch (_) { return ''; }
  }
  async function memWrite(text) {
    try { const d = await db(); d.transaction('remanence', 'readwrite').objectStore('remanence').put(text, memKey); } catch (_) {}
  }

  async function bytesOf(id) {
    const el = $(id);
    if (!el || !el.textContent.trim()) return null;
    let b = XpgWebHost.fromBase64(el.textContent);
    if (el.getAttribute('data-gzip') === '1') b = await XpgWebHost.gunzip(b);
    return b;
  }

  async function start() {
    try {
      if (typeof WebAssembly !== 'object' || typeof DecompressionStream !== 'function') {
        say('Ce navigateur ne peut pas faire tourner l\'IHM (il faut un navigateur récent : Edge, Chrome, Firefox ou Safari de 2023 ou plus). '
            + 'La documentation du projet reste lisible : Alarmes, Variables, Scripts, Fiche.');
        return;
      }
      say('L\'IHM démarre…');
      const wasm = await bytesOf('xpg-moteur');
      const archive = await bytesOf('xpg-archive');
      const font = await bytesOf('xpg-police');
      if (!wasm || !archive) { say('Cette page ne porte pas le moteur de l\'IHM.'); return; }
      const retained = await memRead();
      const api = await XpgWebHost.run({
        canvas: $('ihm-canvas'), wasm, font, archive, retained,
        onError: (t) => say('L\'IHM s\'est arrêtée : ' + t),
        onStatus: (a) => {
          $('vue').textContent = a.view() || '—';
          $('user').textContent = a.user() || 'personne';
          // Les equipements : pas relies dans cette version de la page (le dire, sans faire semblant).
          const n = a.equipments().length;
          const box = $('equip');
          const text = n ? n + ' équipement' + (n > 1 ? 's' : '') + ' : non relié' + (n > 1 ? 's' : '') + ' dans cette page' : '';
          if (box.textContent !== text) {
            box.innerHTML = '';
            if (n) {
              const s = document.createElement('span');
              s.className = 'e';
              s.innerHTML = '<span class="pt"></span>';
              s.appendChild(document.createTextNode(text));
              s.title = 'Les variables liées à un équipement (Modbus TCP) gardent leur valeur initiale dans la page : '
                      + 'ses vues, ses variables IHM, ses scripts et ses alarmes tournent. Pour les vrais appareils : IHM Studio, ou le poste d\'exploitation.';
              box.appendChild(s);
            }
          }
        },
      });
      etat.hidden = true;
      window.XpgIhm = api;           // les essais (Playwright) et la console s'en servent
      // Les remanentes : gardees quand elles changent (la ligne de la date ne compte pas), et en partant.
      let kept = retained.replace(/^prise .*$/m, '');
      const keep = () => {
        const t = api.retained();
        if (!t || t.replace(/^prise .*$/m, '') === kept) return;
        kept = t.replace(/^prise .*$/m, '');
        memWrite(t);
      };
      setInterval(keep, 2000);
      window.addEventListener('pagehide', keep);
      document.addEventListener('visibilitychange', () => { if (document.hidden) keep(); });
    } catch (e) {
      console.error(e);
      say('L\'IHM ne démarre pas : ' + (e && e.message ? e.message : e));
    }
  }
  start();
})();
