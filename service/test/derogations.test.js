// =============================================================================
//  service/test/derogations.test.js - 1.47.0 : la derogation << Gerer le
//  service >> (docs/ADMINS.md, § 11 ; src/derogations.js), essayee dans le VRAI
//  runtime (workerd, par outils/service/lancer-local.sh), par HTTP : signee par
//  la cle de l'editeur, courte (72 h au plus), tenue par le service A SON HEURE
//  (XPG_DECALAGE_HORLOGE_ESSAI avance son horloge : la fin), revocable, ecrite au
//  journal (utilisee, finie), dans la sauvegarde.
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, hasard, lancerService, nouvelleCle } from "./aides.js";

const editeur = nouvelleCle();
const lea = nouvelleCle(); // l'admin qui recoit la derogation
const marc = nouvelleCle(); // un autre admin (sans derogation)
const yan = nouvelleCle(); // un admin retire
const etat = fs.mkdtempSync("/tmp/xpg-derogation-essai-");
let svc;

const now = () => Math.floor(Date.now() / 1000) + (svc ? svc.ecart : 0);

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
    assert.ok(typeof r.json.message === "string" && r.json.message.length > 5);
}

function fiche(cle, nom, droits, retire = 0) {
    return { id: cle.id, cle: cle.hex, nom, pc: "PC-" + nom.toUpperCase(), poste: hasard(16), depuis: now() - 100, profil: "Admin LV1", droits, retire, gardes: [] };
}

// La derogation (le texte JSON) et sa signature par `par` (la cle de l'editeur).
function derogation({ admin = lea, nom = "Léa Essai", debut = now(), duree = 3600, droit = "service", id = hasard(16), par = editeur } = {}) {
    const texte = JSON.stringify({ format: 1, id, droit, admin: admin.hex, nom, debut, fin: debut + duree, emise: debut });
    return { id, corps: { derogation: texte, signature: par.signer(Buffer.from("XPG-DEROGATION-1\n" + texte)).toString("hex") } };
}

async function journal() {
    const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: editeur });
    assert.equal(j.statut, 200, j.texte);
    return j.json.lignes.map((t) => JSON.parse(t)).map((l) => ({ ...l, e: JSON.parse(l.entree) }));
}

async function relancer(decalage) {
    if (svc) await svc.arreter();
    svc = await lancerService({ cles: [editeur.hex], decalage, etat, sansLimites: true });
}

describe("la derogation << Gerer le service >> (1.47.0)", () => {
    before(async () => {
        await relancer(0);
        const liste = JSON.stringify({
            format: 1,
            numero: 1,
            emise: now(),
            admins: [fiche(lea, "Léa Essai", ["licence_creer", "demandes"]), fiche(marc, "Hugo Essai", ["offres"]), fiche(yan, "Nina Essai", [], now() - 50)],
        });
        const r = await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } });
        assert.equal(r.statut, 200, r.texte);
    });
    after(async () => {
        if (svc) await svc.arreter();
        fs.rmSync(etat, { recursive: true, force: true });
    });

    it("sans derogation : un admin ne gere pas le service (403 << derogation >>, la phrase le dit)", async () => {
        const r = await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "deployer" } });
        erreur(r, 403, "derogation");
        assert.match(r.json.message, /admin\+ seul gère le service/);
        const s = await appel(svc, "POST", "/admin/stats/releve", { cle: lea, corps: {} });
        erreur(s, 403, "derogation");
        const g = await appel(svc, "GET", "/admin/derogation", { cle: lea });
        assert.equal(g.statut, 200);
        assert.equal(g.json.derogation, null);
        const e = await appel(svc, "GET", "/admin/etat", { cle: lea });
        assert.equal(e.json.derogation, null);
        // une route de l'admin+ sans << derogation >> reste fermee, avec la phrase de toujours
        erreur(await appel(svc, "GET", "/admin/journal?depuis=0", { cle: lea }), 403, "droit");
    });

    it("l'accorder : seulement l'admin+, signee par sa cle, courte (72 h au plus), maintenant, a un admin en place", async () => {
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: lea, corps: derogation().corps }), 403, "droit");
        const fausse = derogation();
        fausse.corps.signature = derogation().corps.signature; // la signature d'un autre texte
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: fausse.corps }), 400, "signature");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ par: lea }).corps }), 400, "signature");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ duree: 73 * 3600 }).corps }), 400, "derogation");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ duree: 100 * 365 * 86400 }).corps }), 400, "derogation");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ duree: 60 }).corps }), 400, "derogation");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ debut: now() - 7200 }).corps }), 400, "derogation");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ debut: now() + 7200 }).corps }), 400, "derogation");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ droit: "offres" }).corps }), 400, "derogation");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ admin: nouvelleCle() }).corps }), 404, "admin");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: derogation({ admin: yan, nom: "Nina Essai" }).corps }), 409, "retire");
        const d = derogation({ duree: 3600 });
        const r = await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: d.corps });
        assert.equal(r.statut, 201, r.texte);
        assert.equal(r.json.derogation.etat, "active");
        assert.equal(r.json.derogation.admin, lea.hex);
        assert.ok(r.json.derogation.reste > 3500 && r.json.derogation.reste <= 3600);
        assert.equal(r.json.derogation.par, editeur.hex);
    });

    it("en cours : l'admin la voit (sa derogation, son document signe), un autre admin non ; GET /admin/etat la dit", async () => {
        const g = await appel(svc, "GET", "/admin/derogation", { cle: lea });
        assert.equal(g.json.derogation.etat, "active");
        assert.ok(g.json.derogation.document.includes(lea.hex));
        assert.equal(g.json.derogation.signature.length, 128);
        assert.equal((await appel(svc, "GET", "/admin/derogation", { cle: marc })).json.derogation, null);
        assert.equal((await appel(svc, "GET", "/admin/derogation", { cle: editeur })).json.derogation.admin, lea.hex);
        const e = await appel(svc, "GET", "/admin/etat", { cle: lea });
        assert.equal(e.json.derogation.etat, "active");
        assert.equal(e.json.derogation.document, undefined, "l'etat ne porte pas le document");
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: marc })).json.derogation, null);
    });

    it("l'utiliser : 200 tant qu'elle court, une ligne << derogation_utilisee >> au journal (le service la signe) ; pas un autre admin", async () => {
        const r = await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "deployer", detail: "essai (--essai)" } });
        assert.equal(r.statut, 200, r.texte);
        assert.ok(r.json.derogation.reste > 0);
        erreur(await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "tout_faire" } }), 400, "champ");
        erreur(await appel(svc, "POST", "/admin/derogation/utiliser", { cle: marc, corps: { action: "deployer" } }), 403, "derogation");
        // une route de l'admin+ marquee << derogation >> s'ouvre a elle
        const s = await appel(svc, "POST", "/admin/stats/releve", { cle: lea, corps: {} });
        assert.equal(s.statut, 200, s.texte);
        erreur(await appel(svc, "POST", "/admin/stats/releve", { cle: marc, corps: {} }), 403, "derogation");
        const lignes = (await journal()).filter((l) => l.e.action === "derogation_utilisee");
        assert.equal(lignes.length, 2, "deux usages, deux lignes");
        assert.equal(lignes[0].e.detail.geste, "deployer");
        assert.equal(lignes[0].e.detail.admin, lea.hex);
        assert.match(lignes[1].e.detail.geste, /releve/);
        assert.notEqual(lignes[0].cle, editeur.hex, "ecrite par le service (sa cle)");
        // l'admin n'accorde ni ne revoque
        const cur = (await appel(svc, "GET", "/admin/derogation", { cle: editeur })).json.derogation;
        erreur(await appel(svc, "POST", "/admin/derogation/revoquer", { cle: lea, corps: { id: cur.id } }), 403, "droit");
    });

    it("revoquee avant la fin : refusee aussitot, la phrase le dit", async () => {
        const cur = (await appel(svc, "GET", "/admin/derogation", { cle: editeur })).json.derogation;
        erreur(await appel(svc, "POST", "/admin/derogation/revoquer", { cle: editeur, corps: { id: hasard(16) } }), 404, "derogation");
        const r = await appel(svc, "POST", "/admin/derogation/revoquer", { cle: editeur, corps: { id: cur.id } });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.derogation.etat, "revoquee");
        const u = await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "deployer" } });
        erreur(u, 403, "derogation");
        assert.match(u.json.message, /révoquée/);
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: lea })).json.derogation.etat, "revoquee");
        // revoquer deux fois : la meme reponse
        assert.equal((await appel(svc, "POST", "/admin/derogation/revoquer", { cle: editeur, corps: { id: cur.id } })).statut, 200);
    });

    it("une nouvelle remplace l'ancienne ; a son heure, le service la dit finie (une ligne << derogation_expiree >>, une fois)", async () => {
        const d1 = derogation({ duree: 4 * 3600 });
        assert.equal((await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: d1.corps })).statut, 201);
        const d2 = derogation({ duree: 3600 });
        const r = await appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: d2.corps });
        assert.equal(r.statut, 201);
        assert.equal(r.json.remplacee, d1.id);
        assert.equal((await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "adresse" } })).statut, 200);
        // l'horloge du service avancee d'une heure et 100 s (le meme etat) : elle a pris fin
        await relancer(3700);
        const u = await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "deployer" } });
        erreur(u, 403, "derogation");
        assert.match(u.json.message, /pris fin/);
        erreur(await appel(svc, "POST", "/admin/stats/releve", { cle: lea, corps: {} }), 403, "derogation");
        const e = await appel(svc, "GET", "/admin/etat", { cle: lea });
        assert.equal(e.json.derogation.etat, "expiree");
        assert.equal(e.json.derogation.reste, 0);
        await appel(svc, "GET", "/admin/etat", { cle: editeur });
        const fins = (await journal()).filter((l) => l.e.action === "derogation_expiree");
        assert.equal(fins.length, 1, "la fin dite une seule fois");
        assert.equal(fins[0].e.objet, d2.id);
        erreur(await appel(svc, "POST", "/admin/derogation/revoquer", { cle: editeur, corps: { id: d2.id } }), 409, "expiree");
    });

    it("elle part avec la sauvegarde (la table config)", async () => {
        const s = await appel(svc, "GET", "/admin/sauvegarde?table=config", { cle: editeur });
        assert.equal(s.statut, 200, s.texte);
        const ligne = s.json.lignes.find((l) => (l.cle || l[0]) === "derogation" || JSON.stringify(l).includes('"derogation"'));
        assert.ok(ligne, "la ligne << derogation >> est dans la sauvegarde");
    });
});
