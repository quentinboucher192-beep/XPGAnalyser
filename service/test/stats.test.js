// =============================================================================
//  service/test/stats.test.js - 1.47.0 : LES STATISTIQUES ET LE GLOBE
//  (docs/STATISTIQUES.md) contre le VRAI Worker et un FAUX Stripe sur ce poste
// -----------------------------------------------------------------------------
//  Le lieu de chaque appel (X-XPG-Lieu-Essai, seulement pres de 127.0.0.1 ; un
//  X-XPG-Lieu pose par le client est efface ; jamais l'adresse IP) ; les faits
//  (licences, PC, Decouvertes, demandes et leur issue, commandes livrees) ; les
//  courbes (GET /admin/stats/serie : les mesures, les divisions, les filtres, la
//  comparaison, les droits, les erreurs) ; le releve du jour (pris une fois,
//  POST /admin/stats/releve) et son emploi pour un niveau du passe ; le temps qui
//  passe (le service relance deux jours plus tard) ; les mises a jour (l'adoption,
//  le delai, les PC pas a jour) ; les jours et les heures des admins ; le globe et
//  le direct ; les lignes ecrites ; la sauvegarde et la restauration (du schema 5,
//  et d'une sauvegarde du schema 4).
//  Des noms d'essai seulement (Atelier Exemple, Societe d'essai, Lea Essai...).
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { after, before, describe, it } from "node:test";

import { lancerFauxStripe } from "../../tests/outils/faux_stripe.mjs";
import { appel, bloc, certificat, demandeJson, hasard, iso, lancerService, lignesEcrites, nouveauCode, nouvelleCle, sha256Hex } from "./aides.js";

const JOUR = 86400;
const now = () => Math.floor(Date.now() / 1000);
const CLE_STRIPE = "essai_jeton_sk_test_" + hasard(12);
const WEBHOOK = "essai_jeton_whsec_" + hasard(12);

const editeur = nouvelleCle();
const lea = nouvelleCle(); // admin : demandes, licences
const hugo = nouvelleCle(); // admin : licence_creer seulement
const etatA = fs.mkdtempSync(path.join(os.tmpdir(), "xpg-service-stats-"));
let A = null;
let B = null;
let stripe = null;
let cleServiceA = "";
let t0 = 0; // l'heure du service au debut (jour 0)

const LIEUX = {
    lyon: "ville=Lyon&region=Auvergne-Rh%C3%B4ne-Alpes&pays=FR&lat=45.764&lon=4.8357&colo=CDG",
    paris: "ville=Paris&region=%C3%8Ele-de-France&pays=FR&lat=48.8566&lon=2.3522&colo=CDG",
    toulouse: "ville=Toulouse&region=Occitanie&pays=FR&lat=43.6047&lon=1.4442&colo=MRS",
    bruxelles: "ville=Bruxelles&region=Bruxelles-Capitale&pays=BE&lat=50.8503&lon=4.3517&colo=BRU",
    nantes: "ville=Nantes&region=Pays+de+la+Loire&pays=FR&lat=47.2184&lon=-1.5536&colo=CDG",
};
const IPS = [];
// Un PC : sa cle, son lieu (X-XPG-Lieu-Essai), son logiciel, son adresse (gardee nulle part).
function pc(lieu, logiciel = "IHM-Studio/1.46.0", autres = {}) {
    const c = nouvelleCle();
    c.ip = "203.0.113." + (IPS.length + 10);
    IPS.push(c.ip);
    c.entetes = { "X-XPG-Logiciel": logiciel, ...autres };
    if (lieu) c.entetes["X-XPG-Lieu-Essai"] = LIEUX[lieu];
    return c;
}
const P1 = pc("lyon");
const P2 = pc("lyon");
const P3 = pc("paris", "API-Studio/1.47.0");
const P4 = pc("lyon");
const P5 = pc("toulouse");
const P6 = pc("bruxelles");
const P7 = pc("lyon");
// un X-XPG-Lieu pose par le client (et un X-XPG-Lieu-Essai illisible) : jamais pris
const P8 = pc(null, "IHM-Studio/1.46.0", { "X-XPG-Lieu": "ville=Faux&pays=US&lat=1&lon=1", "X-XPG-Lieu-Essai": "pays=nulle-part" });
const P9 = pc("nantes");
const L1 = "XPG-2026-T0001";
const L2 = "XPG-2026-T0002";
const L3 = "XPG-2026-T0003";
const C1 = nouveauCode();
const C2 = nouveauCode();
const C3 = nouveauCode();

const ok = (r, s = 200) => assert.equal(r.statut, s, r.texte);
function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
}
const de = (svc, cle, methode, chemin, corps, plus = {}) => appel(svc, methode, chemin, { cle, corps, entetes: { ...(cle.entetes || {}), ...plus }, ip: cle.ip });
const jourDe = (t) => new Date(t * 1000).toISOString().slice(0, 10);
const heureService = (svc) => now() + svc.ecart;

function licenceJson({ id, type = "temporary", max = 2, emission, expire, client, offre, options }) {
    const o = {
        format: "xpg-license",
        version: 1,
        license_id: id,
        customer: client,
        edition: offre,
        type,
        service: "http://127.0.0.1:8787",
        max_devices: max,
        features: [{ id: "ai_generation" }],
        issued_at: iso(emission),
        expires_at: expire ? iso(expire) : null,
        offline_grace_days: 0,
        devices: [],
        manifest: 1,
        nonce: hasard(16),
    };
    if (options) o.options = options;
    return JSON.stringify(o, null, 2) + "\n";
}

async function publierVersion(svc, version, publiee) {
    const octets = Buffer.from("installateur d'essai " + version + "\n");
    const id = hasard(16);
    const nom = "IHM-Studio-Setup-" + version + ".exe";
    const manifeste = bloc(
        "fichier",
        JSON.stringify({ format: 1, id, nom, taille: octets.length, sha256: sha256Hex(octets), sorte: "studios", version, acces: "public", morceau: 1048576, emis: now() }),
        editeur,
    );
    ok(await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste } }), 201);
    ok(await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + id + "&n=0", { cle: editeur, corps: octets }));
    const v = { format: 1, version, publiee, importante: false, notes: [], fichiers: [{ id, nom, taille: octets.length, sha256: sha256Hex(octets), sorte: "studios", pour: "tous" }] };
    ok(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: bloc("version", JSON.stringify(v), editeur) } }), 201);
}

const activer = (svc, p, code, nom) => de(svc, p, "POST", "/v1/activer", { code, demande: bloc("demande", demandeJson(p), p), nom });
const serie = (svc, cle, q) => appel(svc, "GET", "/admin/stats/serie?" + new URLSearchParams(q).toString(), { cle, entetes: cle.entetes });

// Toutes les lignes d'une table du service (la sauvegarde, page par page).
async function table(svc, nom) {
    const out = [];
    let depuis = "";
    for (;;) {
        const r = await appel(svc, "GET", "/admin/sauvegarde?table=" + nom + (depuis ? "&depuis=" + depuis : ""), { cle: editeur });
        ok(r);
        out.push(...r.json.lignes);
        if (!r.json.suivant) return out;
        depuis = r.json.suivant;
    }
}

before(async () => {
    stripe = await lancerFauxStripe({ cle: CLE_STRIPE });
    A = await lancerService({ cles: [editeur.hex], sansLimites: true, etat: etatA, stripe: { cle: CLE_STRIPE, webhook: WEBHOOK, base: stripe.base } });
    cleServiceA = (await appel(A, "GET", "/v1/etat")).json.cle_service;
    t0 = heureService(A);
    editeur.entetes = { "X-XPG-Lieu-Essai": LIEUX.lyon, "X-XPG-Logiciel": "XPG-License-Admin/1.47.0" };
    lea.entetes = { "X-XPG-Lieu-Essai": LIEUX.toulouse, "X-XPG-Logiciel": "XPG-License-Admin/1.47.0" };
    hugo.entetes = {};
});

after(async () => {
    for (const s of [A, B]) if (s) await s.arreter();
    fs.rmSync(etatA, { recursive: true, force: true });
    if (B) fs.rmSync(B.etat, { recursive: true, force: true });
    if (stripe) await stripe.arreter();
});

describe("jour 0 : le service se remplit", () => {
    it("la version 1.48.0 ; les admins, le certificat, le registre, deux versions publiees", async () => {
        assert.equal((await appel(A, "GET", "/v1/etat")).json.version, "1.48.0");
        const fiche = (cle, nom, droits) => ({ id: cle.id, cle: cle.hex, nom, pc: "PC-" + nom, poste: "", depuis: t0 - 100, profil: "Admin LV1", droits, retire: 0, gardes: [] });
        const liste = JSON.stringify({
            format: 1,
            numero: 1,
            emise: t0,
            admins: [fiche(lea, "Léa Essai", ["demandes", "licence_creer", "licence_prolonger", "licence_option"]), fiche(hugo, "Hugo Essai", ["licence_creer"])],
        });
        ok(await de(A, editeur, "PUT", "/admin/admins", { admins: bloc("admins", liste, editeur) }));
        ok(await de(A, editeur, "PUT", "/admin/certificat", { certificat: certificat(editeur, cleServiceA) }));
        const reg = bloc("registre", JSON.stringify({ format: "xpg-registry", version: 1, number: 1, issued_at: iso(t0), editions: [{ id: "PROFESSIONAL" }] }, null, 2) + "\n", editeur);
        ok(await de(A, editeur, "PUT", "/admin/registre", { registre: reg }));
        await publierVersion(A, "1.46.0", t0 - 10 * JOUR);
        await publierVersion(A, "1.47.0", t0 + 600); // publiee dans 10 minutes (l'adoption se lit plus tard)
    });

    it("les licences : deux par l'admin+, une par Lea ; une prolongee avec une option ; les faits", async () => {
        ok(await de(A, editeur, "PUT", "/admin/licences", { licence: bloc("licence", licenceJson({ id: L1, max: 4, emission: t0 - 60, expire: t0 + 200 * JOUR, client: "Atelier Exemple", offre: "PROFESSIONAL" }), editeur), code_hash: C1.hash }));
        ok(await de(A, lea, "PUT", "/admin/licences", { licence: bloc("licence", licenceJson({ id: L2, max: 1, emission: t0 - 60, expire: t0 + 20 * JOUR, client: "Société d'essai", offre: "PERSONAL" }), lea), code_hash: C2.hash }));
        ok(
            await de(A, editeur, "PUT", "/admin/licences", {
                licence: bloc("licence", licenceJson({ id: L3, type: "floating", max: 2, emission: t0 - 60, expire: t0 + 100 * JOUR, client: "Atelier Exemple", offre: "ENTERPRISE" }), editeur),
                code_hash: C3.hash,
            }),
        );
        // L1 prolongee (+200 jours) avec une option
        const opt = [{ id: "ai_advanced", label: "Assistant IA avancé", features: [], from: iso(t0), until: null }];
        ok(await de(A, editeur, "PUT", "/admin/licences", { licence: bloc("licence", licenceJson({ id: L1, max: 4, emission: t0 - 30, expire: t0 + 400 * JOUR, client: "Atelier Exemple", offre: "PROFESSIONAL", options: opt }), editeur) }));
        const f = await table(A, "stats_faits");
        const genres = f.map((x) => x.genre);
        assert.deepEqual(genres.filter((g) => g.startsWith("licence_")).sort(), ["licence_nouvelle", "licence_nouvelle", "licence_nouvelle", "licence_option", "licence_prolongee"]);
        const p = f.find((x) => x.genre === "licence_prolongee");
        assert.equal(p.valeur, 200, "les jours gagnes");
        assert.equal(p.client, "Atelier Exemple");
        assert.equal(f.find((x) => x.genre === "licence_nouvelle" && x.licence === L2).admin, lea.id, "deposee par Lea");
        assert.deepEqual(JSON.parse(f.find((x) => x.genre === "licence_option").detail).options, ["ai_advanced"]);
    });

    it("les PC (chacun dans sa ville), les jetons, les Decouvertes, une liberation, un retrait, une revocation", async () => {
        ok(await activer(A, P1, C1.code, "ATELIER-PC1"));
        ok(await activer(A, P2, C1.code, "ATELIER-PC2"));
        ok(await activer(A, P3, C2.code, "ESSAI-PC3"));
        ok(await activer(A, P4, C3.code, "ATELIER-PC4"));
        ok(await activer(A, P5, C3.code, "ATELIER-PC5"));
        ok(await de(A, P4, "POST", "/v1/flottante/jeton", { licence: L3, station: P4.station }));
        ok(await de(A, P5, "POST", "/v1/flottante/jeton", { licence: L3, station: P5.station }));
        ok(await de(A, P6, "POST", "/v1/decouverte", { machine: hasard(16) }));
        ok(await de(A, P7, "POST", "/v1/decouverte", { machine: hasard(16) }));
        ok(await activer(A, P7, C1.code, "ATELIER-PC7")); // sa Decouverte : convertie
        ok(await activer(A, P8, C1.code, "SANS-LIEU-PC8"));
        ok(await de(A, P2, "POST", "/v1/liberer", { licence: L1, station: P2.station }));
        ok(await activer(A, P9, C1.code, "NANTES-PC9"));
        ok(await de(A, editeur, "POST", "/admin/activations/retirer", { licence: L1, poste: P9.hex }));
        const revs = bloc(
            "revocations",
            JSON.stringify({ format: "xpg-revocations", version: 1, number: 1, issued_at: iso(t0), revoked: [{ license_id: L2, revoked_at: iso(t0), reason: "Fin de contrat" }] }),
            editeur,
        );
        ok(await de(A, editeur, "PUT", "/admin/revocations", { revocations: revs }));
        const f = await table(A, "stats_faits");
        const n = (g) => f.filter((x) => x.genre === g).length;
        assert.equal(n("pc_active"), 8);
        assert.equal(n("pc_libere"), 1);
        assert.equal(n("pc_retire"), 1);
        assert.equal(f.find((x) => x.genre === "pc_retire").admin, editeur.id, "retire par l'admin+");
        assert.equal(n("decouverte_convertie"), 1);
        assert.equal(f.find((x) => x.genre === "decouverte_convertie").poste, P7.hex);
        assert.equal(n("licence_revoquee"), 1);
        assert.equal(f.find((x) => x.genre === "pc_active" && x.poste === P1.hex).admin, editeur.id, "un PC actif : le signataire de sa licence");
    });

    it("le lieu : la ville seulement (arrondie), jamais un lieu pose par le client, jamais l'adresse IP ; une ecriture par heure au plus", async () => {
        const lieux = await table(A, "lieux");
        const p1 = lieux.find((l) => l.cle === P1.hex);
        assert.deepEqual(
            { ...p1, vu: 0 },
            { cle: P1.hex, poste: P1.poste, logiciel: "IHM-Studio", ville: "Lyon", region: "Auvergne-Rhône-Alpes", pays: "FR", lat: 45.8, lon: 4.8, colo: "CDG", vu: 0 },
        );
        assert.equal(lieux.find((l) => l.cle === P8.hex), undefined, "X-XPG-Lieu du client : efface par le Worker ; X-XPG-Lieu-Essai illisible : rien");
        assert.equal(lieux.find((l) => l.cle === editeur.hex).ville, "Lyon");
        // le meme lieu dans l'heure : rien de plus a ecrire (renouveler : ses 3 lignes)
        const r = await de(A, P1, "POST", "/v1/renouveler", { station: P1.station });
        ok(r);
        assert.equal(lignesEcrites(r), 3);
        // aucune adresse IP dans tout le service
        const s = await appel(A, "GET", "/admin/sauvegarde", { cle: editeur });
        let tout = "";
        for (const t of s.json.tables) tout += JSON.stringify(await table(A, t.nom));
        for (const ip of IPS) assert.ok(!tout.includes(ip), "l'adresse " + ip + " n'est nulle part");
        for (const t of ["stats_faits", "stats_quotidien", "stats_compteurs", "versions_vues", "lieux"])
            assert.ok(s.json.tables.some((x) => x.nom === t), t + " est dans la sauvegarde");
    });

    it("les demandes : recues, traitees avec leur issue (une acceptee, une refusee), une qui attend ; une issue illisible : 400", async () => {
        const deposer = async (p) => {
            const r = await de(A, p, "POST", "/v1/demandes", { cle: editeur.id, scelle: "QUFBQQ==", scelles: [{ cle: lea.id, scelle: "TEVB" }] });
            ok(r, 201);
            return r.json.numero;
        };
        const n3 = await deposer(P3);
        const n4 = await deposer(P4);
        await deposer(P6);
        erreur(await de(A, editeur, "POST", "/admin/demandes/traiter", { numero: n3, issue: "peut-etre" }), 400, "champ");
        ok(await de(A, editeur, "POST", "/admin/demandes/traiter", { numero: n3, issue: "acceptee" }));
        ok(await de(A, lea, "POST", "/admin/demandes/traiter", { numero: n4, issue: "refusee" }));
        const f = (await table(A, "stats_faits")).filter((x) => x.genre === "demande_traitee");
        assert.equal(f.length, 2);
        assert.deepEqual(f.map((x) => [x.admin, JSON.parse(x.detail).issue]), [
            [editeur.id, "acceptee"],
            [lea.id, "refusee"],
        ]);
        assert.ok(f.every((x) => x.valeur >= 0 && x.valeur < 600), "le delai");
        const r = (await table(A, "stats_faits")).find((x) => x.genre === "demande_recue" && x.valeur === n4);
        assert.equal(r.client, "Atelier Exemple", "le client du PC (sa licence)");
    });

    it("les ventes : deux commandes payees (une offre livree par Lea, une option livree par l'admin+), l'option remboursee", async () => {
        const CATALOGUE = {
            active: true,
            livraison_auto: true,
            devise: "eur",
            articles: [
                { id: "ed:PROFESSIONAL", genre: "edition", edition: "PROFESSIONAL", montant: 49000, libelle: "Licence PROFESSIONAL - 1 an" },
                { id: "opt:ai_advanced:365", genre: "option", option: "ai_advanced", jours: 365, montant: 9900, libelle: "Option IA - 1 an" },
            ],
        };
        ok(await de(A, editeur, "PUT", "/admin/boutique", CATALOGUE));
        const commander = async (p, extra) => {
            const r = await de(A, p, "POST", "/v1/boutique/commande", {
                societe: "Atelier Exemple",
                nom: "Camille Essai",
                courriel: "camille@exemple.invalid",
                cle: editeur.id,
                scelle: Buffer.from("scelle d'essai").toString("base64"),
                ...extra,
            });
            ok(r, 201);
            const s = [...stripe.sessions.values()].find((x) => x.numero === r.json.numero);
            const e = await (
                await fetch(stripe.base + "/__essai/evenement", {
                    method: "POST",
                    body: JSON.stringify({ url: A.base + "/v1/boutique/stripe", secret: WEBHOOK, type: "checkout.session.completed", session: s.id }),
                })
            ).json();
            assert.equal(e.statut, 200, e.corps);
            return { numero: r.json.numero, session: s.id };
        };
        const c1 = await commander(P1, { article: "ed:PROFESSIONAL" });
        const c2 = await commander(P4, { article: "opt:ai_advanced:365", licence_existante: L3 });
        ok(await de(A, lea, "POST", "/admin/commandes/livree", { numero: c1.numero, licence: "XPG-2026-T0004" }));
        ok(await de(A, editeur, "POST", "/admin/commandes/livree", { numero: c2.numero, licence: L3 }));
        ok(await de(A, editeur, "POST", "/admin/commandes/rembourser", { numero: c2.numero }));
        const e = await (
            await fetch(stripe.base + "/__essai/evenement", { method: "POST", body: JSON.stringify({ url: A.base + "/v1/boutique/stripe", secret: WEBHOOK, type: "charge.refunded", session: c2.session }) })
        ).json();
        assert.equal(e.statut, 200, e.corps);
        const f = (await table(A, "stats_faits")).filter((x) => x.genre === "commande_livree");
        assert.deepEqual(f.map((x) => [x.admin, x.valeur, x.offre]), [
            [lea.id, 49000, "PROFESSIONAL"],
            [editeur.id, 9900, "options"],
        ]);
    });
});

describe("jour 0 : les courbes (GET /admin/stats/serie)", () => {
    const auj = () => jourDe(t0);
    const semaine = () => ({ du: jourDe(t0 - 6 * JOUR), au: jourDe(t0), pas: "jour" });

    it("les licences, les PC, les Decouvertes, les jetons : le point d'aujourd'hui", async () => {
        const mesures = [
            "licences_nouvelles",
            "licences_prolongees",
            "licences_options",
            "licences_revoquees",
            "licences_actives",
            "pc_actives",
            "pc_liberes",
            "pc_retires",
            "pc_actifs",
            "decouvertes_commencees",
            "decouvertes_converties",
            "decouvertes_en_cours",
            "jetons_pic",
            "jetons_plafond",
            "licences_au_plafond",
        ];
        const r = await serie(A, editeur, { mesures: mesures.join(","), ...semaine() });
        ok(r);
        assert.equal(r.json.version, "1.47.0");
        assert.equal(r.json.periodes.length, 7);
        assert.deepEqual(r.json.periodes.at(-1), { du: auj(), au: auj() });
        assert.equal(r.json.avant.periodes.length, 7, "la periode d'avant, alignee");
        assert.equal(r.json.avant.au, jourDe(t0 - 7 * JOUR));
        const attendu = {
            licences_nouvelles: 3,
            licences_prolongees: 1,
            licences_options: 1,
            licences_revoquees: 1,
            licences_actives: 2,
            pc_actives: 8,
            pc_liberes: 1,
            pc_retires: 1,
            pc_actifs: 5,
            decouvertes_commencees: 2,
            decouvertes_converties: 1,
            decouvertes_en_cours: 1,
            jetons_pic: 2,
            jetons_plafond: 2,
            licences_au_plafond: 1,
        };
        for (const [m, v] of Object.entries(attendu)) {
            const x = r.json.mesures[m];
            assert.equal(x.points.at(-1), v, m + " aujourd'hui : " + JSON.stringify(x));
            assert.equal(x.avant.total, ["niveau", "max"].includes(x.genre) ? (x.genre === "max" ? null : 0) : 0, m + " : rien la semaine d'avant");
            assert.equal(x.variation, null, m);
        }
        assert.equal(r.json.mesures.licences_actives.genre, "niveau");
        assert.equal(r.json.mesures.licences_actives.source, "S+N");
        assert.equal(r.json.mesures.pc_actives.total, 8);
        assert.equal(r.json.mesures.pc_actifs.total, 5, "un niveau : son dernier point");
    });

    it("les divisions (par offre, par admin) et les filtres (un client, un admin)", async () => {
        let r = await serie(A, editeur, { mesures: "pc_actifs,licences_nouvelles,ventes_montant", ...semaine(), par: "offre", comparer: "rien" });
        ok(r);
        assert.equal(r.json.avant, undefined);
        const parts = (m) => Object.fromEntries(r.json.mesures[m].parts.map((p) => [p.cle, p.total]));
        assert.deepEqual(parts("pc_actifs"), { PROFESSIONAL: 3, ENTERPRISE: 2 });
        assert.deepEqual(parts("licences_nouvelles"), { PROFESSIONAL: 1, PERSONAL: 1, ENTERPRISE: 1 });
        assert.deepEqual(parts("ventes_montant"), { PROFESSIONAL: 49000, options: 9900 });
        r = await serie(A, editeur, { mesures: "licences_nouvelles,pc_actifs,demandes_traitees,ventes_montant", ...semaine(), client: "  atelier   EXEMPLE " });
        ok(r);
        assert.equal(r.json.mesures.licences_nouvelles.total, 2, "L1 et L3");
        assert.equal(r.json.mesures.pc_actifs.total, 5);
        assert.equal(r.json.mesures.demandes_traitees.total, 1, "la demande de P4 (Atelier Exemple)");
        assert.equal(r.json.mesures.ventes_montant.total, 58900);
        r = await serie(A, editeur, { mesures: "licences_nouvelles,demandes_traitees,ventes_montant,jetons_pic", ...semaine(), admin: lea.id });
        ok(r);
        assert.equal(r.json.mesures.licences_nouvelles.total, 1, "L2, deposee par Lea");
        assert.equal(r.json.mesures.demandes_traitees.total, 1);
        assert.equal(r.json.mesures.ventes_montant.total, 49000, "la commande qu'elle a livree");
        assert.deepEqual(r.json.mesures.jetons_pic, { refuse: "filtre", filtre: "admin" });
        r = await serie(A, editeur, { mesures: "demandes_traitees", ...semaine(), par: "admin" });
        assert.deepEqual(Object.fromEntries(r.json.mesures.demandes_traitees.parts.map((p) => [p.cle, p.total])), { [editeur.id]: 1, [lea.id]: 1 });
    });

    it("les ventes et les demandes : les montants, le panier, le cumul, les remboursements, les delais", async () => {
        const r = await serie(A, editeur, {
            mesures:
                "ventes_montant,ventes_commandes,ventes_panier,ventes_cumul,ventes_remboursements,ventes_remboursements_nombre,licences_vendues_en_ligne,licences_signees_main,licences_expirees,licences_echeances_montant,demandes_recues,demandes_traitees,demandes_acceptees,demandes_refusees,demandes_attente,demandes_delai_max,demandes_plus_ancienne",
            ...semaine(),
        });
        ok(r);
        const t = (m) => r.json.mesures[m].total;
        assert.equal(t("ventes_montant"), 58900);
        assert.equal(r.json.mesures.ventes_montant.unite, "centimes");
        assert.equal(t("ventes_commandes"), 2);
        assert.equal(t("ventes_panier"), 29450);
        assert.equal(t("ventes_cumul"), 58900);
        assert.equal(t("ventes_remboursements"), 9900);
        assert.equal(t("ventes_remboursements_nombre"), 1);
        assert.equal(t("licences_vendues_en_ligne"), 1);
        assert.equal(t("licences_signees_main"), 3, "L1, L2, L3 : aucune ne vient d'une commande");
        assert.equal(t("licences_expirees"), 0, "aucune ne finit cette semaine");
        assert.equal(t("demandes_recues"), 3);
        assert.equal(t("demandes_traitees"), 2);
        assert.equal(t("demandes_acceptees"), 1);
        assert.equal(t("demandes_refusees"), 1);
        assert.equal(t("demandes_attente"), 1);
        assert.ok(t("demandes_delai_max") >= 0 && t("demandes_delai_max") < 600);
        assert.ok(t("demandes_plus_ancienne") >= 0 && t("demandes_plus_ancienne") < 600);
        // les echeances : la periode dans le futur (L2 finit dans 20 jours ; PERSONAL n'est pas au catalogue : 0 EUR)
        const e = await serie(A, editeur, { mesures: "licences_expirees,licences_echeances_montant", du: auj(), au: jourDe(t0 + 90 * JOUR), pas: "semaine", par: "offre", comparer: "rien" });
        ok(e);
        assert.equal(e.json.mesures.licences_expirees.total, 1);
        assert.equal(e.json.mesures.licences_echeances_montant.total, 0);
        const i = e.json.periodes.findIndex((x) => x.du <= jourDe(t0 + 20 * JOUR) && jourDe(t0 + 20 * JOUR) <= x.au);
        assert.equal(e.json.mesures.licences_expirees.points[i], 1, "dans la semaine de sa fin");
        // l'an dernier : rien
        const a = await serie(A, editeur, { mesures: "ventes_montant", ...semaine(), comparer: "an" });
        assert.equal(Number(a.json.avant.du.slice(0, 4)), Number(semaine().du.slice(0, 4)) - 1);
        assert.equal(a.json.mesures.ventes_montant.avant.total, 0);
    });

    it("les droits d'un admin (Hugo, sans << demandes >>) ; les erreurs", async () => {
        const r = await serie(A, hugo, { mesures: "ventes_montant,demandes_traitees,licences_nouvelles", ...semaine() });
        ok(r);
        assert.deepEqual(r.json.mesures.ventes_montant, { refuse: "droit", droits: ["demandes"] });
        assert.deepEqual(r.json.mesures.demandes_traitees, { refuse: "droit", droits: ["demandes"] });
        assert.equal(r.json.mesures.licences_nouvelles.total, 3);
        erreur(await serie(A, editeur, { mesures: "inconnue" }), 400, "champ");
        erreur(await serie(A, editeur, { mesures: "" }), 400, "champ");
        erreur(await serie(A, editeur, { mesures: "pc_actifs", pas: "heure" }), 400, "champ");
        erreur(await serie(A, editeur, { mesures: "pc_actifs", du: auj(), au: jourDe(t0 - JOUR) }), 400, "champ");
        erreur(await serie(A, editeur, { mesures: "pc_actifs", du: jourDe(t0 - 500 * JOUR), au: auj(), pas: "jour" }), 400, "champ");
        erreur(await serie(A, editeur, { mesures: "pc_actifs", par: "couleur" }), 400, "champ");
        erreur(await serie(A, editeur, { mesures: "pc_actifs", du: "2026-02-30" }), 400, "champ");
        erreur(await appel(A, "GET", "/admin/stats/serie?mesures=pc_actifs", { cle: P1 }), 403, "editeur");
        erreur(await appel(A, "GET", "/admin/stats/serie?mesures=pc_actifs"), 401, "signature");
    });

    it("le releve du jour : pris au premier appel du jour, une seule fois ; POST /admin/stats/releve (l'admin+)", async () => {
        const r = await de(A, editeur, "POST", "/admin/stats/releve", {});
        ok(r);
        assert.equal(r.json.jour, auj());
        assert.equal(r.json.pris, false, "deja pris au premier appel du jour (le service venait de demarrer)");
        assert.equal(r.json.releve.pc_actifs.total, 0, "le service etait vide");
        erreur(await de(A, lea, "POST", "/admin/stats/releve", {}), 403, "droit");
        assert.equal((await table(A, "stats_quotidien")).length, 1);
    });

    it("les mises a jour, les admins, le globe et le direct", async () => {
        // un appel de Lea et de Hugo (Hugo : sans lieu)
        ok(await de(A, lea, "GET", "/admin/etat"));
        ok(await de(A, hugo, "GET", "/admin/etat"));
        const g = await de(A, editeur, "GET", "/admin/globe");
        ok(g);
        const v = Object.fromEntries(g.json.villes.map((x) => [x.id, x]));
        assert.deepEqual(Object.keys(v).sort(), ["BE|Bruxelles", "FR|Lyon", "FR|Nantes", "FR|Paris", "FR|Toulouse"]);
        const lyon = v["FR|Lyon"];
        assert.deepEqual(
            [lyon.ville, lyon.region, lyon.pays, lyon.lat, lyon.lon, lyon.colo, lyon.pc, lyon.clients, lyon.admins, lyon.actifs, lyon.pas_a_jour, lyon.commandes, lyon.decouvertes, lyon.demandes],
            ["Lyon", "Auvergne-Rhône-Alpes", "FR", 45.8, 4.8, "CDG", 3, 1, 1, 4, 1, 2, 0, 0],
        );
        assert.deepEqual(lyon.noms_clients, ["Atelier Exemple"]);
        assert.deepEqual(lyon.ids_admins, [editeur.id]);
        assert.deepEqual(lyon.postes.sort(), [P1.hex, P4.hex, P7.hex].sort());
        assert.deepEqual([v["FR|Toulouse"].pc, v["FR|Toulouse"].admins, v["FR|Toulouse"].ids_admins], [1, 1, [lea.id]]);
        assert.deepEqual([v["BE|Bruxelles"].decouvertes, v["BE|Bruxelles"].demandes, v["BE|Bruxelles"].pc], [1, 1, 0]);
        assert.deepEqual([v["FR|Paris"].pc, v["FR|Paris"].actifs], [0, 1], "P3 : sa licence revoquee");
        assert.deepEqual(g.json.totaux, { villes: 5, pays: 2, clients: 1, pc: 4, actifs: 8, pas_a_jour: 1, admins: 2, decouvertes: 1, commandes: 2, demandes: 1 });
        assert.deepEqual(g.json.sans_lieu, { pc: 1, admins: 1 }, "P8 (un lieu pose par le client) et Hugo");
        assert.equal(g.json.version_derniere, "1.47.0");
        // Hugo (sans << demandes >>) : le calque des demandes vaut 0
        const gh = await de(A, hugo, "GET", "/admin/globe?periode=7");
        ok(gh);
        assert.equal(gh.json.totaux.demandes, 0);
        assert.equal(gh.json.periode, 7);
        erreur(await de(A, editeur, "GET", "/admin/globe?periode=0"), 400, "champ");
        // le direct
        const d = await de(A, editeur, "GET", "/admin/globe/direct?depuis=" + (t0 - 5));
        ok(d);
        assert.equal(d.json.actifs, 9, "P1 a P9 (les admins ne comptent pas)");
        const villes = new Set(d.json.connexions.map((x) => x.ville + ":" + x.genre));
        for (const x of ["FR|Lyon:studio", "FR|Paris:studio", "FR|Toulouse:admin", "BE|Bruxelles:studio", "FR|Lyon:admin"]) assert.ok(villes.has(x), x);
        assert.equal((await de(A, editeur, "GET", "/admin/globe/direct?depuis=" + (heureService(A) + 10))).json.connexions.length, 0);
        // les admins : leurs jours et leurs heures
        const a = await de(A, editeur, "GET", "/admin/stats/admins");
        ok(a);
        const ad = Object.fromEntries(a.json.admins.map((x) => [x.id, x]));
        assert.deepEqual(Object.keys(ad).sort(), [editeur.id, hugo.id, lea.id].sort());
        assert.equal(ad[editeur.id].role, "admin+");
        assert.equal(ad[lea.id].nom, "Léa Essai");
        assert.deepEqual(ad[lea.id].jours, [auj()]);
        assert.ok(ad[lea.id].appels >= 4 && ad[lea.id].heures.reduce((s, n) => s + n, 0) === ad[lea.id].appels);
        assert.ok([new Date(t0 * 1000).getUTCHours(), new Date(heureService(A) * 1000).getUTCHours()].includes(ad[lea.id].debut_habituel));
        assert.deepEqual([ad[lea.id].ville, ad[lea.id].pays, ad[lea.id].logiciel, ad[lea.id].logiciel_version], ["Toulouse", "FR", "XPG-License-Admin", "1.47.0"]);
        assert.deepEqual([ad[hugo.id].ville, ad[hugo.id].appels > 0], ["", true]);
        const al = await de(A, lea, "GET", "/admin/stats/admins");
        assert.deepEqual(
            al.json.admins.map((x) => x.id),
            [lea.id],
            "un admin ne voit que lui",
        );
        const s = await serie(A, lea, { mesures: "admin_appels,admin_jours", ...semaine(), par: "admin" });
        assert.deepEqual(
            s.json.mesures.admin_jours.parts.map((p) => p.cle),
            [lea.id],
            "les heures d'un admin : les siennes seulement",
        );
        // les mises a jour (aujourd'hui : 1.47.0 n'est pas encore publiee)
        const m = await de(A, editeur, "GET", "/admin/stats/mises-a-jour");
        ok(m);
        assert.deepEqual(m.json.derniere, { version: "1.47.0", publiee: t0 + 600 });
        assert.deepEqual(
            m.json.versions.map((x) => [x.version, x.studios, x.admins]),
            [
                ["1.47.0", 1, 2],
                ["1.46.0", 8, 0],
            ],
        );
        assert.deepEqual(m.json.a_jour, { studios: 1, admins: 2 });
        assert.equal(m.json.pas_a_jour_total, 8);
        assert.equal(m.json.pas_a_jour.find((x) => x.cle === P1.hex).nom, "ATELIER-PC1");
        assert.equal(m.json.pas_a_jour.find((x) => x.cle === P1.hex).client, "Atelier Exemple");
        assert.ok(!m.json.adoption.some((x) => x.version === "1.47.0"), "1.47.0 : publiee dans 10 minutes");
        // 1.46.0 (publiee il y a 10 jours) : ses 11 premiers jours, les PC vus seulement aujourd'hui (le jour 10)
        const a46 = m.json.adoption.find((x) => x.version === "1.46.0" && x.logiciel === "studios");
        assert.deepEqual([a46.base, a46.jours.length, a46.jours.at(-1), a46.jours.at(-2)], [9, 11, 9, 0]);
    });
});

describe("deux jours plus tard (le service relance, son horloge avancee)", () => {
    before(async () => {
        await A.arreter();
        A = await lancerService({ cles: [editeur.hex], sansLimites: true, etat: etatA, decalage: 2 * JOUR });
    });

    it("le releve du jour : pris au demarrage (les niveaux d'hier soir) ; POST /admin/stats/releve ne le refait pas", async () => {
        const r = await de(A, editeur, "POST", "/admin/stats/releve", {});
        ok(r);
        assert.equal(r.json.jour, jourDe(t0 + 2 * JOUR));
        assert.equal(r.json.pris, false);
        const rel = r.json.releve;
        assert.equal(rel.pc_actifs.total, 5);
        assert.deepEqual(rel.pc_actifs.parts.offre, { PROFESSIONAL: 3, ENTERPRISE: 2 });
        assert.equal(rel.licences_actives.total, 2);
        assert.equal(rel.demandes_attente.total, 1);
        assert.equal(rel.decouvertes_en_cours.total, 1);
        assert.equal(rel.jetons_plafond.total, 2);
        assert.equal(rel.pc_a_jour.total, 3, "P3, l'admin+ et Lea : sur 1.47.0");
        assert.equal(rel.version_derniere, "1.47.0");
        assert.equal((await table(A, "stats_quotidien")).length, 2);
    });

    it("P1 passe en 1.47.0, se libere et se reactive : le releve garde le niveau d'hier, la reconstitution ne le peut pas", async () => {
        P1.entetes["X-XPG-Logiciel"] = "IHM-Studio/1.47.0";
        ok(await de(A, P1, "POST", "/v1/renouveler", { station: P1.station }));
        ok(await de(A, P1, "POST", "/v1/liberer", { licence: L1, station: P1.station }));
        ok(await activer(A, P1, C1.code, "ATELIER-PC1"));
        const q = { mesures: "pc_actifs,pc_actives,pc_liberes,maj_installees", du: jourDe(t0), au: jourDe(t0 + 2 * JOUR), pas: "jour" };
        const r = await serie(A, editeur, q);
        ok(r);
        // le jour 0 : reconstitue (la reactivation de P1 aujourd'hui efface son debut : 4) ; hier soir : le releve (5)
        assert.deepEqual(r.json.mesures.pc_actifs.points, [4, 5, 5]);
        assert.deepEqual(r.json.mesures.pc_actives.points, [8, 0, 1]);
        assert.deepEqual(r.json.mesures.pc_liberes.points, [1, 0, 1]);
        assert.deepEqual(r.json.mesures.maj_installees.points.at(-1), 1);
        // un filtre : pas de releve, reconstitue (P1 n'y est actif qu'a partir d'aujourd'hui)
        const f = await serie(A, editeur, { ...q, mesures: "pc_actifs", client: "Atelier Exemple" });
        assert.deepEqual(f.json.mesures.pc_actifs.points, [4, 4, 5]);
        // la comparaison a la periode d'avant : la variation
        const c = await serie(A, editeur, { mesures: "pc_actives", du: jourDe(t0 + JOUR), au: jourDe(t0 + 2 * JOUR), pas: "jour" });
        assert.equal(c.json.mesures.pc_actives.total, 1);
        assert.equal(c.json.mesures.pc_actives.avant.total, 8);
        assert.equal(c.json.mesures.pc_actives.variation, -87.5);
        assert.equal(c.json.releves_depuis, jourDe(t0));
    });

    it("les mises a jour : l'adoption de 1.47.0 jour apres jour, le delai, la part a jour, les PC pas a jour", async () => {
        const m = await de(A, editeur, "GET", "/admin/stats/mises-a-jour?du=" + jourDe(t0) + "&pas=jour");
        ok(m);
        const s = m.json.adoption.find((x) => x.version === "1.47.0" && x.logiciel === "studios");
        assert.equal(s.base, 9, "les PC des Studios vus");
        assert.deepEqual(s.jours, [1, 2], "P3 des le premier jour, P1 le deuxieme");
        const ad = m.json.adoption.find((x) => x.version === "1.47.0" && x.logiciel === "admins");
        assert.deepEqual([ad.base, ad.jours], [2, [2, 2]]);
        const dl = m.json.delais.find((x) => x.version === "1.47.0" && x.logiciel === "studios");
        assert.equal(dl.nombre, 1, "P1 avait une version plus ancienne");
        assert.ok(Math.abs(dl.median - (2 * JOUR - 600)) < 900, "le delai : ~2 jours moins 10 minutes (" + dl.median + ")");
        assert.deepEqual(m.json.a_jour, { studios: 2, admins: 2 });
        assert.equal(m.json.pas_a_jour_total, 7);
        assert.deepEqual(m.json.part.studios.vus, [9, 9, 9]);
        assert.deepEqual(m.json.part.studios.a_jour, [1, 1, 2], "le releve d'hier soir, puis P1 aujourd'hui");
        assert.deepEqual(m.json.part.admins.a_jour, [2, 2, 2]);
        erreur(await de(A, editeur, "GET", "/admin/stats/mises-a-jour?jours=0"), 400, "champ");
        const ma = await de(A, editeur, "GET", "/admin/stats/mises-a-jour?logiciel=admins");
        assert.deepEqual(Object.keys(ma.json.part), ["admins"]);
    });

    it("les admins : un jour de plus pour l'admin+", async () => {
        const a = await de(A, editeur, "GET", "/admin/stats/admins?du=" + jourDe(t0));
        ok(a);
        const e = a.json.admins.find((x) => x.id === editeur.id);
        assert.deepEqual(e.jours, [jourDe(t0), jourDe(t0 + 2 * JOUR)]);
        erreur(await de(A, editeur, "GET", "/admin/stats/admins?du=" + jourDe(t0 - 800 * JOUR)), 400, "champ");
    });
});

describe("la sauvegarde et la restauration", () => {
    let sommaire = null;
    let lignesA = null;
    const STATS = ["stats_faits", "stats_quotidien", "versions_vues", "lieux"];

    before(async () => {
        B = await lancerService({ cles: [editeur.hex], sansLimites: true, decalage: 2 * JOUR });
    });

    async function restaurer(tables, schema) {
        ok(await appel(B, "POST", "/admin/restauration/debut", { cle: editeur, corps: { schema, remplacer: true } }));
        for (const [nom, lignes] of Object.entries(tables)) for (let i = 0; i < lignes.length; i += 500) ok(await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: nom, lignes: lignes.slice(i, i + 500) } }));
        const f = await appel(B, "POST", "/admin/restauration/fin", { cle: editeur });
        ok(f);
        return f.json;
    }

    it("B (neuf) rend les memes tables, les memes courbes et le meme globe que A", async () => {
        const s = await appel(A, "GET", "/admin/sauvegarde", { cle: editeur });
        ok(s);
        assert.equal(s.json.schema, 5);
        sommaire = s.json.tables.map((t) => t.nom);
        lignesA = {};
        for (const t of sommaire) lignesA[t] = await table(A, t);
        await restaurer(lignesA, 5);
        for (const t of STATS) assert.deepEqual(await table(B, t), lignesA[t], t);
        const q = { mesures: "pc_actifs,licences_nouvelles,ventes_montant,demandes_traitees", du: jourDe(t0), au: jourDe(t0 + 2 * JOUR), pas: "jour", par: "offre" };
        const ra = await serie(A, editeur, q);
        const rb = await serie(B, editeur, q);
        for (const m of q.mesures.split(",")) assert.deepEqual(rb.json.mesures[m].points, ra.json.mesures[m].points, m);
        const ga = (await appel(A, "GET", "/admin/globe", { cle: editeur })).json;
        const gb = (await appel(B, "GET", "/admin/globe", { cle: editeur })).json;
        assert.deepEqual(
            gb.villes.map((v) => [v.id, v.pc, v.clients, v.admins]),
            ga.villes.map((v) => [v.id, v.pc, v.clients, v.admins]),
        );
    });

    it("une sauvegarde du schema 4 (sans les tables des statistiques) : prise ; les versions vues refaites, le debut des releves note", async () => {
        const sans = {};
        for (const [t, l] of Object.entries(lignesA)) {
            if (STATS.includes(t) || t === "stats_compteurs") continue;
            sans[t] = t === "config" ? l.filter((x) => x.cle !== "stats_debut").map((x) => (x.cle === "schema" ? { ...x, valeur: "4" } : x)) : l;
        }
        await restaurer(sans, 4);
        assert.equal((await table(B, "stats_faits")).length, 0);
        const vv = await table(B, "versions_vues");
        assert.equal(vv.length, lignesA.logiciels.length, "une par cle vue (sa version d'alors)");
        const r = await serie(B, editeur, { mesures: "pc_actifs,licences_nouvelles", du: jourDe(t0), au: jourDe(t0 + 2 * JOUR), pas: "jour", comparer: "rien" });
        ok(r);
        assert.equal(r.json.releves_depuis, jourDe(t0 + 2 * JOUR), "les releves commencent a la restauration");
        assert.equal(r.json.mesures.licences_nouvelles.total, 3, "reconstituees (leur emission)");
        assert.deepEqual(r.json.mesures.pc_actifs.points, [4, 4, 5], "reconstitue (P1 reactive aujourd'hui)");
        const config = Object.fromEntries((await table(B, "config")).map((x) => [x.cle, x.valeur]));
        assert.equal(config.schema, "5");
    });
});
