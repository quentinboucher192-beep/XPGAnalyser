// =============================================================================
//  service/test/derogations152.test.js - 1.52.0 : LA DEROGATION << tout >>,
//  ACTIVEE PAR UN CODE (docs/ADMINS.md, § 11 ; src/derogations.js), essayee dans
//  le VRAI runtime (workerd, par outils/service/lancer-local.sh), par HTTP :
//  le format 2 (une duree, une heure limite pour taper le code ; ni le code ni son
//  empreinte dans le document signe), l'empreinte du code a cote, JAMAIS rendue
//  (aucune route, ni la sauvegarde) ; l'activation (le bon code, un code faux, 5
//  codes faux, trop tard, revoquee, un autre admin) ; les lectures du journal et
//  des versions ouvertes seulement pendant qu'elle court (une ligne la premiere
//  fois) ; le format 1 toujours recu ; les lignes du journal (jamais le code).
//  Les codes sont compares, jamais ecrits dans un message d'essai.
// =============================================================================
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, hasard, lancerService, nouvelleCle, reponses, sha256Hex } from "./aides.js";

const editeur = nouvelleCle();
const lea = nouvelleCle(); // l'admin qui recoit la derogation
const hugo = nouvelleCle(); // un autre admin (sans derogation)
const etat = fs.mkdtempSync("/tmp/xpg-derogation152-essai-");
let svc;

const now = () => Math.floor(Date.now() / 1000) + (svc ? svc.ecart : 0);
const empreinte = (id, code) => sha256Hex("XPG-DEROGATION-CODE-1\n" + id + "\n" + code);
// Un code de 8 chiffres (tire ici : l'Admin+ le tire de sa cle ; le service ne voit que l'empreinte).
const tirerCode = () => String(crypto.randomInt(0, 100000000)).padStart(8, "0");
const autreCode = (c) => String((Number(c) + 1 + crypto.randomInt(0, 1000)) % 100000000).padStart(8, "0");
const empreintes = []; // toutes les empreintes envoyees : jamais rendues

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
    assert.ok(typeof r.json.message === "string" && r.json.message.length > 5);
}

function fiche(cle, nom, droits) {
    return { id: cle.id, cle: cle.hex, nom, pc: "PC-" + nom.toUpperCase(), poste: hasard(16), depuis: now() - 100, profil: "Admin LV1", droits, retire: 0, gardes: [] };
}

// Une derogation de format 2 (le texte signe, et l'empreinte du code a cote).
function derogation2({ admin = lea, nom = "Léa Essai", duree = 4 * 3600, emise = now(), pourTaper = 3600, id = hasard(16), droit = "tout", code = tirerCode(), dansDoc } = {}) {
    const doc = { format: 2, id, droit, admin: admin.hex, nom, duree, valable: emise + pourTaper, emise };
    if (dansDoc) doc[dansDoc] = empreinte(id, code);
    const texte = JSON.stringify(doc);
    const h = empreinte(id, code);
    empreintes.push(h);
    return { id, code, corps: { derogation: texte, signature: editeur.signer(Buffer.from("XPG-DEROGATION-1\n" + texte)).toString("hex"), code: h } };
}
function derogation1({ duree = 3600, id = hasard(16) } = {}) {
    const debut = now();
    const texte = JSON.stringify({ format: 1, id, droit: "service", admin: lea.hex, nom: "Léa Essai", debut, fin: debut + duree, emise: debut });
    return { id, corps: { derogation: texte, signature: editeur.signer(Buffer.from("XPG-DEROGATION-1\n" + texte)).toString("hex") } };
}

async function journal() {
    const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: editeur });
    assert.equal(j.statut, 200, j.texte);
    return j.json.lignes.map((t) => JSON.parse(t)).map((l) => ({ ...l, e: JSON.parse(l.entree) }));
}
const deposer = (d) => appel(svc, "PUT", "/admin/derogation", { cle: editeur, corps: d.corps });
const activer = (cle, id, code) => appel(svc, "POST", "/admin/derogation/activer", { cle, corps: { id, code } });

async function relancer(decalage) {
    if (svc) await svc.arreter();
    svc = await lancerService({ cles: [editeur.hex], decalage, etat, sansLimites: true });
}

describe("la derogation << tout >>, activee par un code (1.52.0)", () => {
    before(async () => {
        await relancer(0);
        const liste = JSON.stringify({
            format: 1,
            numero: 1,
            emise: now(),
            admins: [fiche(lea, "Léa Essai", ["licence_creer", "demandes"]), fiche(hugo, "Hugo Essai", ["offres"])],
        });
        const r = await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } });
        assert.equal(r.statut, 200, r.texte);
    });
    after(async () => {
        if (svc) await svc.arreter();
        fs.rmSync(etat, { recursive: true, force: true });
    });

    it("le format 2 : la duree (10 min a 72 h), l'heure limite du code, emise maintenant ; l'empreinte a cote, jamais dans le document", async () => {
        const sans = derogation2();
        delete sans.corps.code;
        erreur(await deposer(sans), 400, "champ");
        erreur(await deposer(derogation2({ dansDoc: "code" })), 400, "derogation");
        erreur(await deposer(derogation2({ duree: 599 })), 400, "derogation");
        erreur(await deposer(derogation2({ duree: 72 * 3600 + 1 })), 400, "derogation");
        erreur(await deposer(derogation2({ duree: 0 })), 400, "derogation");
        erreur(await deposer(derogation2({ pourTaper: 300 })), 400, "derogation");
        erreur(await deposer(derogation2({ pourTaper: 25 * 3600 })), 400, "derogation");
        erreur(await deposer(derogation2({ emise: now() - 7200 })), 400, "derogation");
        erreur(await deposer(derogation2({ emise: now() + 7200 })), 400, "derogation");
        erreur(await deposer(derogation2({ droit: "service" })), 400, "derogation");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: lea, corps: derogation2().corps }), 403, "droit");
        // les bornes : 10 min et 72 h passent
        assert.equal((await deposer(derogation2({ duree: 600 }))).statut, 201);
        const d = derogation2({ duree: 72 * 3600 });
        const r = await deposer(d);
        assert.equal(r.statut, 201, r.texte);
        const v = r.json.derogation;
        assert.equal(v.etat, "en_attente");
        assert.equal(v.droit, "tout");
        assert.equal(v.duree, 72 * 3600);
        assert.equal(v.essais_restants, 5);
        assert.equal(v.reste, 0);
        assert.equal(v.code, undefined, "jamais l'empreinte du code");
        // un Admin 1.51 lit debut = fin (l'heure limite) : jamais << en cours >> pour lui
        assert.equal(v.debut, v.valable);
        assert.equal(v.fin, v.valable);
        assert.ok(r.json.remplacee, "elle remplace celle d'avant (en attente)");
    });

    it("en attente : l'admin la voit (sans le code), rien ne s'ouvre ; l'etat la dit en attente", async () => {
        const g = await appel(svc, "GET", "/admin/derogation", { cle: lea });
        assert.equal(g.json.derogation.etat, "en_attente");
        assert.equal(g.json.derogation.admin, lea.hex);
        assert.ok(!g.json.derogation.document.includes('"code"'));
        const e = await appel(svc, "GET", "/admin/etat", { cle: lea });
        assert.equal(e.json.derogation.etat, "en_attente");
        assert.equal(e.json.derogation.essais_restants, 5);
        assert.ok(e.json.derogation.valable > now());
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: hugo })).json.derogation, null);
        const u = await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "deployer" } });
        erreur(u, 403, "derogation");
        assert.match(u.json.message, /attend son code/);
        erreur(await appel(svc, "GET", "/admin/journal?depuis=0", { cle: lea }), 403, "droit");
        erreur(await appel(svc, "GET", "/admin/versions", { cle: lea }), 403, "droit");
        erreur(await appel(svc, "POST", "/admin/stats/releve", { cle: lea, corps: {} }), 403, "droit");
    });

    it("l'activer : seulement son admin, 8 chiffres ; un code faux compte un essai (le journal le dit, sans le code)", async () => {
        const cur = (await appel(svc, "GET", "/admin/derogation", { cle: editeur })).json.derogation;
        const d = derogation2({ duree: 4 * 3600 });
        assert.equal((await deposer(d)).statut, 201);
        erreur(await activer(hugo, d.id, d.code), 404, "derogation");
        erreur(await activer(editeur, d.id, d.code), 403, "derogation");
        erreur(await activer(lea, cur.id, d.code), 404, "derogation"); // l'ancienne : remplacee
        erreur(await activer(lea, d.id, "1234"), 400, "champ");
        erreur(await activer(lea, d.id, "4821 7730"), 400, "champ"); // l'Admin envoie les 8 chiffres seuls
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: lea })).json.derogation.essais_restants, 5, "un code mal ecrit ne compte pas");
        const faux = await activer(lea, d.id, autreCode(d.code));
        erreur(faux, 403, "code");
        assert.match(faux.json.message, /Code faux : il te reste 4 essais\./);
        assert.equal(faux.json.essais_restants, 4);
        const lignes = (await journal()).filter((l) => l.e.action === "derogation_code_refuse");
        assert.equal(lignes.length, 1);
        assert.equal(lignes[0].e.detail.essais_restants, 4);
        assert.notEqual(lignes[0].cle, editeur.hex, "ecrite par le service (sa cle)");
        // la bonne : elle court de maintenant a maintenant + 4 h
        const ok = await activer(lea, d.id, d.code);
        assert.equal(ok.statut, 200, ok.texte);
        assert.equal(ok.json.derogation.etat, "active");
        assert.ok(ok.json.derogation.reste > 4 * 3600 - 30 && ok.json.derogation.reste <= 4 * 3600);
        assert.ok(Math.abs(ok.json.derogation.debut - now()) < 30);
        const act = (await journal()).filter((l) => l.e.action === "derogation_activee");
        assert.equal(act.length, 1);
        assert.equal(act[0].e.detail.admin, lea.hex);
        // le meme code encore (un appel repete) : la meme reponse ; un autre : refuse sans essai de plus
        assert.equal((await activer(lea, d.id, d.code)).statut, 200);
        erreur(await activer(lea, d.id, autreCode(d.code)), 409, "deja");
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: lea })).json.derogation.etat, "active");
    });

    it("active : les lectures du journal et des versions s'ouvrent a elle (une ligne la premiere fois), gerer le service aussi", async () => {
        for (let i = 0; i < 3; i++) {
            const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: lea });
            assert.equal(j.statut, 200, j.texte);
            assert.ok(Array.isArray(j.json.lignes));
            assert.equal((await appel(svc, "GET", "/admin/versions", { cle: lea })).statut, 200);
        }
        assert.equal((await appel(svc, "POST", "/admin/stats/releve", { cle: lea, corps: {} })).statut, 200);
        assert.equal((await appel(svc, "POST", "/admin/derogation/utiliser", { cle: lea, corps: { action: "deployer" } })).statut, 200);
        const usages = (await journal()).filter((l) => l.e.action === "derogation_utilisee");
        assert.equal(usages.filter((l) => /GET \/admin\/journal/.test(l.e.detail.geste)).length, 1, "le journal lu trois fois : une ligne");
        assert.equal(usages.filter((l) => /GET \/admin\/versions/.test(l.e.detail.geste)).length, 1, "les versions lues trois fois : une ligne");
        assert.equal(usages.filter((l) => /releve/.test(l.e.detail.geste)).length, 1);
        assert.equal(usages.filter((l) => l.e.detail.geste === "deployer").length, 1);
        // un autre admin : rien ; ce qui reste a l'admin+ : ferme a elle aussi
        erreur(await appel(svc, "GET", "/admin/journal?depuis=0", { cle: hugo }), 403, "droit");
        erreur(await appel(svc, "GET", "/admin/admins/inscrits", { cle: lea }), 403, "droit");
        erreur(await appel(svc, "POST", "/admin/admins/invitation", { cle: lea, corps: {} }), 403, "droit");
        erreur(await appel(svc, "PUT", "/admin/versions", { cle: lea, corps: {} }), 403, "droit");
        erreur(await appel(svc, "GET", "/admin/sauvegarde", { cle: lea }), 403, "droit");
        erreur(await appel(svc, "PUT", "/admin/derogation", { cle: lea, corps: derogation2().corps }), 403, "droit");
        const cur = (await appel(svc, "GET", "/admin/derogation", { cle: lea })).json.derogation;
        erreur(await appel(svc, "POST", "/admin/derogation/revoquer", { cle: lea, corps: { id: cur.id } }), 403, "droit");
    });

    it("revoquee en attente du code : le code ne sert plus", async () => {
        const d = derogation2();
        assert.equal((await deposer(d)).statut, 201);
        const r = await appel(svc, "POST", "/admin/derogation/revoquer", { cle: editeur, corps: { id: d.id } });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.derogation.etat, "revoquee");
        erreur(await activer(lea, d.id, d.code), 409, "revoquee");
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: lea })).json.derogation.etat, "revoquee");
        erreur(await appel(svc, "GET", "/admin/journal?depuis=0", { cle: lea }), 403, "droit");
    });

    it("5 codes faux : annulee (raison code_faux), le bon code ne sert plus ; une ligne par essai, une pour l'annulation", async () => {
        const d = derogation2();
        assert.equal((await deposer(d)).statut, 201);
        for (let i = 1; i <= 4; i++) {
            const r = await activer(lea, d.id, autreCode(d.code));
            erreur(r, 403, "code");
            assert.equal(r.json.essais_restants, 5 - i);
            assert.match(r.json.message, new RegExp("il te reste " + (5 - i) + " essai"));
        }
        const cinq = await activer(lea, d.id, autreCode(d.code));
        erreur(cinq, 403, "code");
        assert.match(cinq.json.message, /annulée/);
        const e = (await appel(svc, "GET", "/admin/etat", { cle: lea })).json.derogation;
        assert.equal(e.etat, "annulee");
        assert.equal(e.raison, "code_faux");
        assert.equal(e.essais_restants, 0);
        erreur(await activer(lea, d.id, d.code), 409, "annulee");
        const j = (await journal()).filter((l) => l.e.objet === d.id);
        assert.equal(j.filter((l) => l.e.action === "derogation_code_refuse").length, 5);
        assert.equal(j.filter((l) => l.e.action === "derogation_annulee").length, 1);
        erreur(await appel(svc, "POST", "/admin/derogation/revoquer", { cle: editeur, corps: { id: d.id } }), 409, "annulee");
    });

    it("le format 1 (un Admin+ d'avant) : toujours recu, tout de suite en cours ; il n'ouvre pas les lectures << tout >>", async () => {
        const d = derogation1();
        const r = await deposer(d);
        assert.equal(r.statut, 201, r.texte);
        assert.equal(r.json.derogation.etat, "active");
        assert.equal(r.json.derogation.format, 1);
        assert.equal((await appel(svc, "POST", "/admin/stats/releve", { cle: lea, corps: {} })).statut, 200);
        const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: lea });
        erreur(j, 403, "droit");
        assert.match(j.json.message, /admin\+/);
        assert.equal((await appel(svc, "POST", "/admin/derogation/revoquer", { cle: editeur, corps: { id: d.id } })).statut, 200);
    });

    it("trop tard : le code pas tape avant l'heure limite, annulee (raison code_perime), dite une fois ; une active finit a son heure", async () => {
        const tard = derogation2({ pourTaper: 600 });
        assert.equal((await deposer(tard)).statut, 201);
        await relancer(700); // l'horloge du service avancee de 11 min 40 (le meme etat)
        const e = (await appel(svc, "GET", "/admin/etat", { cle: lea })).json.derogation;
        assert.equal(e.etat, "annulee");
        assert.equal(e.raison, "code_perime");
        await appel(svc, "GET", "/admin/etat", { cle: editeur });
        erreur(await activer(lea, tard.id, tard.code), 409, "perime");
        const lignes = (await journal()).filter((l) => l.e.action === "derogation_annulee" && l.e.objet === tard.id);
        assert.equal(lignes.length, 1, "dite une seule fois");
        assert.equal(lignes[0].e.detail.raison, "code_perime");
        // une de 10 min activee, puis l'horloge avancee de 11 min : finie, les lectures refermees
        const d = derogation2({ duree: 600 });
        assert.equal((await deposer(d)).statut, 201);
        assert.equal((await activer(lea, d.id, d.code)).statut, 200);
        assert.equal((await appel(svc, "GET", "/admin/versions", { cle: lea })).statut, 200);
        await relancer(700 + 660);
        const f = await appel(svc, "GET", "/admin/versions", { cle: lea });
        erreur(f, 403, "droit");
        assert.match(f.json.message, /pris fin/);
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: lea })).json.derogation.etat, "expiree");
        assert.equal((await journal()).filter((l) => l.e.action === "derogation_expiree" && l.e.objet === d.id).length, 1);
    });

    it("l'empreinte du code n'est jamais rendue : aucune reponse, ni la sauvegarde ; le journal ne porte aucun code", async () => {
        const s = await appel(svc, "GET", "/admin/sauvegarde?table=config", { cle: editeur });
        assert.equal(s.statut, 200, s.texte);
        const ligne = s.json.lignes.find((l) => l.cle === "derogation");
        assert.ok(ligne, "la ligne << derogation >> est dans la sauvegarde");
        assert.equal(JSON.parse(ligne.valeur).code, undefined, "sans l'empreinte du code");
        let fuites = 0;
        for (const r of reponses) for (const h of empreintes) if (r.includes(h)) fuites++;
        assert.equal(fuites, 0, "aucune empreinte de code dans les " + reponses.length + " reponses");
        assert.ok(empreintes.length > 10);
    });
});
