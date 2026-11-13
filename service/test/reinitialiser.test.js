// =============================================================================
//  service/test/reinitialiser.test.js - 1.49.0 : REPARTIR DE ZERO
//  (POST /admin/reinitialiser, docs/ADMINS.md par. 13) contre le VRAI Worker
//  et un FAUX Stripe sur ce poste (tests/outils/faux_stripe.mjs)
// -----------------------------------------------------------------------------
//  Un monde d'essai par les routes ordinaires (licences, PC actives, une
//  Decouverte, une demande, un etat de poste, l'archive, un admin et son
//  trousseau, un admin inscrit par son code, une invitation, le journal, un message et son
//  fichier joint, une version et son fichier, le catalogue et une commande PAYEE,
//  le releve des statistiques), puis : les refus (un admin, sans le mot, sans
//  sauvegarde, une liste qui n'est pas vide ou pas plus recente, le registre
//  signe par l'admin), la remise a zero (tout ce qui est efface l'est, tout ce
//  qui est garde est la : le registre, les versions, les produits de Stripe, le
//  certificat), le Studio qui renouvelle (la licence est inconnue), et la
//  sauvegarde faite avant qui remet tout. Les secrets : essai_jeton_...
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { lancerFauxStripe } from "../../tests/outils/faux_stripe.mjs";
import { TABLES_EFFACEES, TABLES_GARDEES } from "../src/reinitialiser.js";
import { appel, bloc, certificat, demandeJson, hasard, iso, lancerService, licenceJson, nouveauCode, nouvelleCle, sha256Hex } from "./aides.js";

const now = () => Math.floor(Date.now() / 1000);
const JOUR = 86400;
const CLE = "essai_jeton_sk_test_" + hasard(12);
const WEBHOOK = "essai_jeton_whsec_" + hasard(12);

const editeur = nouvelleCle();
const lea = nouvelleCle(); // un admin (Lea Essai)
const pc = nouvelleCle(); // un Studio active
const pc2 = nouvelleCle(); // un Studio en Decouverte, qui commande
let A;
let stripe;
let cleService = "";
let certAvant = "";
let versionAvant = "";
let sauvegarde = null; // {tables: {nom: [lignes]}, schema}
const V = { id: hasard(16), octets: Buffer.from("un installateur d'essai\n".repeat(100)) };
const J = { id: hasard(16), octets: Buffer.from("une piece jointe d'essai\n".repeat(20)) };
const CATALOGUE = {
    active: true,
    livraison_auto: true,
    devise: "eur",
    articles: [{ id: "ed:PROFESSIONAL", genre: "edition", edition: "PROFESSIONAL", montant: 49000, libelle: "Licence PROFESSIONAL - 1 an" }],
};

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
}
const ok = (r, s = 200) => assert.equal(r.statut, s, r.texte);
const etat = async () => (await appel(A, "GET", "/v1/etat")).json;
const listeAdmins = (numero, admins) => bloc("admins", JSON.stringify({ format: 1, numero, emise: now(), admins }), editeur);
const revocations = (numero, revoked = []) => bloc("revocations", JSON.stringify({ format: "xpg-revocations", version: 1, number: numero, issued_at: iso(now()), revoked }), editeur);
const registre = (numero, cle) =>
    bloc("registre", JSON.stringify({ format: "xpg-registry", version: 1, number: numero, issued_at: iso(now()), editions: [{ id: "PROFESSIONAL" }] }, null, 2) + "\n", cle);

// Toutes les lignes de chaque table (le sommaire, puis les pages) : ce que fait l'Admin avant de repartir de zero.
async function sauvegarder() {
    const s = await appel(A, "GET", "/admin/sauvegarde", { cle: editeur });
    ok(s);
    const tables = {};
    for (const t of s.json.tables) {
        tables[t.nom] = [];
        let depuis = "";
        for (;;) {
            const r = await appel(A, "GET", "/admin/sauvegarde?table=" + t.nom + (depuis ? "&depuis=" + depuis : ""), { cle: editeur });
            ok(r);
            tables[t.nom].push(...r.json.lignes);
            if (!r.json.suivant) break;
            depuis = r.json.suivant;
        }
    }
    return { schema: s.json.schema, tables, comptes: Object.fromEntries(s.json.tables.map((t) => [t.nom, t.lignes])) };
}

before(async () => {
    stripe = await lancerFauxStripe({ cle: CLE });
    A = await lancerService({ cles: [editeur.hex], sansLimites: true, stripe: { cle: CLE, webhook: WEBHOOK, base: stripe.base } });
    cleService = (await etat()).cle_service;
});

after(async () => {
    if (A) {
        await A.arreter();
        fs.rmSync(A.etat, { recursive: true, force: true });
    }
    if (stripe) await stripe.arreter();
});

describe("un monde d'essai, par les routes ordinaires", () => {
    it("le certificat, le registre (signe par l'admin), une revocation, un admin, son trousseau, un admin inscrit (1.51.0), une invitation, le journal", async () => {
        ok(await appel(A, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: certificat(editeur, cleService) } }));
        ok(await appel(A, "PUT", "/admin/registre", { cle: editeur, corps: { registre: registre(3, editeur) } }));
        ok(await appel(A, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: revocations(2, [{ license_id: "XPG-2026-Z0001" }]) } }));
        const fiche = { id: lea.id, cle: lea.hex, nom: "Léa Essai", pc: "PC-LEA", poste: "", depuis: now() - 100, profil: "Admin LV1", droits: ["licence_creer", "offres", "demandes"], retire: 0, gardes: [] };
        ok(await appel(A, "PUT", "/admin/admins", { cle: editeur, corps: { admins: listeAdmins(5, [fiche]) } }));
        // l'admin signe le registre (le droit << offres >>) : la remise a zero devra le re-signer par l'editeur
        ok(await appel(A, "PUT", "/admin/registre", { cle: lea, corps: { registre: registre(4, lea) } }));
        ok(await appel(A, "PUT", "/admin/trousseau", { cle: editeur, corps: { cle: lea.hex, scelle: "c2NlbGzDqQ==" } }));
        // 1.51.0 : un admin inscrit par son code d'invitation (pas encore signe par l'Admin+), et une invitation pas servie
        const invHugo = await appel(A, "POST", "/admin/admins/invitation", { cle: editeur, corps: { nom: "Hugo Essai", courriel: "hugo@exemple.invalid", profil: "Admin LV1" } });
        ok(invHugo, 201);
        ok(await appel(A, "POST", "/v1/admins/inscription", { cle: nouvelleCle(), corps: { invitation: invHugo.json.code, pc: "PC-HUGO", poste: hasard(16), logiciel: "1.51.0" } }), 201);
        ok(await appel(A, "POST", "/admin/admins/invitation", { cle: editeur, corps: { nom: "Nina Essai", courriel: "nina@exemple.invalid", profil: "Admin LV1" } }), 201);
        const texte = JSON.stringify({ quand: now(), action: "connexion", objet: "", resume: "s'est connectée", detail: {}, nonce: hasard(16) });
        ok(await appel(A, "POST", "/admin/journal", { cle: lea, corps: { entree: texte, signature: lea.signer(Buffer.from("XPG-JOURNAL-1\n" + texte)).toString("hex") } }), 201);
    });

    it("des licences (de l'editeur, de l'admin), un PC active, une Decouverte, une demande, un etat de poste, l'archive", async () => {
        const c = nouveauCode();
        ok(await appel(A, "PUT", "/admin/licences", { cle: editeur, corps: { licence: bloc("licence", licenceJson({ id: "XPG-2026-R0001", max: 3, emission: now() - 60, expire: now() + 200 * JOUR }), editeur), code_hash: c.hash } }));
        ok(await appel(A, "PUT", "/admin/licences", { cle: lea, corps: { licence: bloc("licence", licenceJson({ id: "XPG-2026-R0002", max: 1, emission: now() - 60, expire: now() + 100 * JOUR }), lea) } }));
        ok(await appel(A, "POST", "/v1/activer", { cle: pc, corps: { code: c.code, demande: bloc("demande", demandeJson(pc), pc), nom: "PC Atelier Exemple" } }));
        ok(await appel(A, "POST", "/v1/decouverte", { cle: pc2, corps: { machine: hasard(16) } }));
        ok(await appel(A, "POST", "/v1/demandes", { cle: pc2, corps: { cle: editeur.id, scelle: "QUFBQQ==", scelles: [{ cle: lea.id, scelle: "TEVB" }] }, entetes: { "X-XPG-Logiciel": "IHM-Studio/1.49.0" } }), 201);
        ok(await appel(A, "PUT", "/admin/postes", { cle: editeur, corps: { poste: pc.poste, etat: bloc("poste", JSON.stringify({ version: 1, poste: pc.poste, numero: 1, emis: iso(now()) }), editeur) } }));
        ok(await appel(A, "POST", "/admin/archive", { cle: editeur, corps: { base: 0, fichiers: { "licences/XPG-2026-R0001.xpglic": "chiffre d'essai", "profils-admins.json": '{"profils":[]}' } } }));
    });

    it("une version et son installateur ; un message a un PC et son fichier joint", async () => {
        const manifeste = (f, nom, sorte, version, acces) =>
            bloc("fichier", JSON.stringify({ format: 1, id: f.id, nom, taille: f.octets.length, sha256: sha256Hex(f.octets), sorte, version, acces, morceau: 1048576, emis: now() }), editeur);
        ok(await appel(A, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(V, "IHM-Studio-Setup-1.49.0.exe", "studios", "1.49.0", "public") } }), 201);
        ok(await appel(A, "PUT", "/admin/fichiers/morceau?id=" + V.id + "&n=0", { cle: editeur, corps: V.octets }));
        const v = { format: 1, version: "1.49.0", publiee: now(), importante: false, notes: [], fichiers: [{ id: V.id, nom: "IHM-Studio-Setup-1.49.0.exe", taille: V.octets.length, sha256: sha256Hex(V.octets), sorte: "studios", pour: "tous" }] };
        ok(await appel(A, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: bloc("version", JSON.stringify(v), editeur) } }), 201);
        ok(await appel(A, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(J, "piece-jointe.txt", "autre", "", "jeton") } }), 201);
        ok(await appel(A, "PUT", "/admin/fichiers/morceau?id=" + J.id + "&n=0", { cle: editeur, corps: J.octets }));
        ok(await appel(A, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: J.id, duree: 3600 } }), 201);
        ok(await appel(A, "PUT", "/admin/messages", { cle: editeur, corps: { poste: pc.poste, bloc: bloc("message", JSON.stringify({ format: 1, poste: pc.poste, titre: "Bonjour", scelle: "QUFBQQ==" }), editeur) } }), 201);
        assert.equal((await appel(A, "GET", "/v1/messages?depuis=0", { cle: pc })).json.messages.length, 1);
    });

    it("le catalogue (ses produits chez Stripe) et une commande PAYEE ; le releve des statistiques", async () => {
        const b = await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: CATALOGUE });
        ok(b);
        assert.equal(b.json.stripe_catalogue.etat, "ok", b.texte);
        const scelle = Buffer.from("scelle d'essai").toString("base64");
        const c = await appel(A, "POST", "/v1/boutique/commande", {
            cle: pc2,
            corps: { article: "ed:PROFESSIONAL", societe: "Atelier Exemple", nom: "Camille Essai", courriel: "camille@exemple.invalid", cle: editeur.id, scelle },
        });
        ok(c, 201);
        const s = [...stripe.sessions.values()].find((x) => x.numero === c.json.numero);
        const e = await fetch(stripe.base + "/__essai/evenement", {
            method: "POST",
            body: JSON.stringify({ url: A.base + "/v1/boutique/stripe", secret: WEBHOOK, type: "checkout.session.completed", session: s.id, id: "evt_essai_" + hasard(4) }),
        });
        assert.equal((await e.json()).statut, 200);
        const mes = (await appel(A, "GET", "/v1/boutique/commandes", { cle: pc2 })).json.commandes;
        assert.equal(mes[0].etat, "payee");
        ok(await appel(A, "POST", "/admin/stats/releve", { cle: editeur, corps: {} }));
        certAvant = (await appel(A, "GET", "/v1/certificat")).texte;
        versionAvant = (await appel(A, "GET", "/v1/version")).texte;
    });
});

describe("POST /admin/reinitialiser : les refus (rien n'est efface)", () => {
    it("chaque table de la sauvegarde est decidee : effacee ou gardee (une table neuve oblige a choisir)", () => {
        assert.deepEqual(TABLES_GARDEES, ["config", "boutique", "versions", "fichiers"]);
        assert.deepEqual(TABLES_EFFACEES, [
            "licences",
            "activations",
            "liberations",
            "jetons",
            "decouvertes",
            "demandes",
            "demandes_scelles",
            "postes",
            "archive",
            "stats_jour",
            "admins",
            "admins_demandes",
            "trousseau",
            "journal",
            "fichiers_jetons",
            "messages",
            "logiciels",
            "commandes",
            "commandes_scelles",
            "stripe_evenements",
            "commande_lignes",
            "stats_faits",
            "stats_quotidien",
            "stats_compteurs",
            "versions_vues",
            "lieux",
        ]);
    });

    it("un admin : 403 ; sans le mot REPARTIR : 400 ; sans sauvegarde faite dans l'heure : 409 sauvegarde", async () => {
        const e = await etat();
        const corps = { confirmation: "REPARTIR", sauvegarde: "a".repeat(64), admins: listeAdmins(e.admins + 1, []), revocations: revocations(e.revocations + 1), registre: registre(5, editeur) };
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: lea, corps }), 403, "droit");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: pc, corps }), 403, "editeur");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...corps, confirmation: undefined } }), 400, "confirmation");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...corps, confirmation: "repartir" } }), 400, "confirmation");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...corps, sauvegarde: "pas-une-empreinte" } }), 400, "champ");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps }), 409, "sauvegarde");
        assert.equal((await appel(A, "GET", "/admin/licences", { cle: editeur })).json.licences.length, 2, "rien n'est efface");
    });

    it("apres la sauvegarde : une liste des admins qui n'est pas vide, ou pas plus recente ; une revocation qui reste ; le registre de l'admin", async () => {
        sauvegarde = await sauvegarder();
        assert.equal(sauvegarde.comptes.licences, 2);
        const e = await etat();
        const base = { confirmation: "REPARTIR", sauvegarde: sha256Hex("le fichier .xpgsauve"), admins: listeAdmins(e.admins + 1, []), revocations: revocations(e.revocations + 1) };
        const fiche = { id: lea.id, cle: lea.hex, nom: "Léa Essai", pc: "", poste: "", depuis: now(), profil: "", droits: [], retire: 0, gardes: [] };
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...base, admins: listeAdmins(e.admins + 1, [fiche]) } }), 400, "admins");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...base, admins: listeAdmins(e.admins, []) } }), 409, "ancien");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...base, revocations: revocations(e.revocations) } }), 409, "ancien");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...base, revocations: revocations(e.revocations + 1, [{ license_id: "XPG-2026-Z0001" }]) } }), 400, "revocations");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...base, admins: bloc("admins", JSON.stringify({ format: 1, numero: e.admins + 1, emise: now(), admins: [] }), lea) } }), 400, "signature");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: base }), 409, "registre");
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...base, registre: registre(e.registre, editeur) } }), 409, "ancien");
        assert.equal((await appel(A, "GET", "/admin/licences", { cle: editeur })).json.licences.length, 2, "rien n'est efface");
        assert.equal((await appel(A, "GET", "/admin/etat", { cle: lea })).statut, 200, "l'admin est encore la");
    });
});

describe("la remise a zero", () => {
    let avant;
    let reponse;
    it("200 : ce qui est efface, ce qui est garde ; les numeros montent", async () => {
        avant = await etat();
        const corps = {
            confirmation: "REPARTIR",
            sauvegarde: sha256Hex("le fichier .xpgsauve"),
            admins: listeAdmins(avant.admins + 1, []),
            revocations: revocations(avant.revocations + 1),
            registre: registre(avant.registre + 1, editeur),
        };
        const r = await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps });
        ok(r);
        reponse = r.json;
        assert.equal(r.json.reinitialise, true);
        assert.equal(r.json.journal, 1, "la premiere ligne du journal neuf");
        assert.equal(r.json.effacees.licences, 2);
        assert.equal(r.json.effacees.activations, 1);
        assert.equal(r.json.effacees.admins, 1);
        assert.equal(r.json.effacees.commandes, 1);
        assert.ok(r.json.effacees.stats_faits > 0 && r.json.effacees.stats_quotidien > 0);
        assert.equal(r.json.fichiers_joints, 1);
        assert.equal(r.json.gardees.versions, 1);
        assert.equal(r.json.gardees.fichiers, 1);
        assert.ok(r.json.gardees.boutique >= 2, "le reglage et le catalogue de Stripe");
        const e = await etat();
        assert.equal(e.admins, avant.admins + 1);
        assert.equal(e.revocations, avant.revocations + 1);
        assert.equal(e.registre, avant.registre + 1);
        assert.ok(e.changements > avant.changements, "les Studios se relisent");
        assert.equal(e.cle_service, cleService);
        // le mot de passe de la sauvegarde ne sert qu'une fois : une seconde remise a zero en demande une autre
        erreur(await appel(A, "POST", "/admin/reinitialiser", { cle: editeur, corps: { ...corps, admins: listeAdmins(avant.admins + 2, []), revocations: revocations(avant.revocations + 2), registre: undefined } }), 409, "sauvegarde");
    });

    it("le journal repart : UNE ligne, n 1, precedent vide, << reinitialisation >>, signee par le service", async () => {
        const j = await appel(A, "GET", "/admin/journal?depuis=0", { cle: editeur });
        ok(j);
        assert.equal(j.json.lignes.length, 1);
        assert.equal(j.json.dernier, 1);
        const l = JSON.parse(j.json.lignes[0]);
        assert.equal(l.numero, 1);
        assert.equal(l.precedent, "");
        assert.equal(l.cle, cleService);
        const entree = JSON.parse(l.entree);
        assert.equal(entree.action, "reinitialisation");
        assert.match(entree.resume, /^réinitialisation par l'admin\+/);
        assert.equal(entree.detail.sauvegarde, sha256Hex("le fichier .xpgsauve"));
    });

    it("tout ce qui est efface l'est (toutes ces tables : 0 ligne) ; la config n'a plus la derogation, les invitations", async () => {
        const s = await sauvegarder(); // (le sommaire ajoute sa ligne << sauvegarde >> au journal)
        for (const t of TABLES_EFFACEES) {
            if (t === "journal") assert.equal(s.comptes[t], 2, "la ligne de la remise a zero, puis celle de cette sauvegarde");
            else if (t === "stats_jour" || t === "stats_compteurs" || t === "logiciels" || t === "versions_vues" || t === "lieux") continue; // ces appels-ci se comptent deja
            else assert.equal(s.comptes[t], 0, t);
        }
        const config = Object.fromEntries(s.tables.config.map((l) => [l.cle, l.valeur]));
        for (const k of ["derogation", "admins_invitations", "stats_exclus"]) assert.equal(config[k], undefined, k);
        for (const k of ["n_licences", "n_activations", "n_decouvertes", "n_postes", "n_attente", "n_commandes"]) assert.equal(config[k], "0", k);
        assert.equal(config.cle_publique, cleService, "la cle du service reste");
        assert.equal(JSON.parse(config.revocations_liste).revoquees.length, 0);
        assert.equal((await appel(A, "GET", "/admin/licences", { cle: editeur })).json.licences.length, 0);
        assert.equal((await appel(A, "GET", "/admin/demandes?depuis=0", { cle: editeur })).json.demandes.length, 0);
        assert.equal((await appel(A, "GET", "/admin/commandes", { cle: editeur })).json.commandes.length, 0);
        assert.equal((await appel(A, "GET", "/admin/archive", { cle: editeur })).json.fichiers.length, 0);
        assert.equal((await appel(A, "GET", "/admin/admins/inscrits", { cle: editeur })).json.inscrits.length, 0);
        const adm = await appel(A, "GET", "/admin/admins", { cle: editeur });
        assert.deepEqual(adm.json.admins, []);
        assert.equal(adm.json.numero, avant.admins + 1);
        erreur(await appel(A, "GET", "/v1/fichiers/manifeste?id=" + J.id), 404, "fichier");
        assert.ok(reponse.effacees.messages === 1 && reponse.effacees.fichiers_jetons === 1);
    });

    it("tout ce qui est garde est la : le certificat, le registre (re-signe), la version et son fichier, le catalogue et ses produits chez Stripe", async () => {
        assert.equal((await appel(A, "GET", "/v1/certificat")).texte, certAvant);
        assert.equal((await appel(A, "GET", "/v1/version")).texte, versionAvant);
        const g = await appel(A, "GET", "/v1/fichiers/morceau?id=" + V.id + "&n=0", { binaire: true });
        ok(g);
        assert.ok(g.octets.equals(V.octets), "l'installateur de la version");
        const reg = await appel(A, "GET", "/v1/registre");
        ok(reg);
        assert.ok(reg.texte.includes('"number": ' + (avant.registre + 1)), "le registre re-signe par l'editeur");
        assert.ok(reg.texte.includes("SIGNATURE ed25519 " + editeur.id));
        const b = await appel(A, "GET", "/admin/boutique", { cle: editeur });
        ok(b);
        assert.equal(b.json.articles.length, 1);
        assert.equal(b.json.articles[0].stripe_produit, "xpg-ed-PROFESSIONAL");
        assert.equal(b.json.stripe_catalogue.etat, "ok");
        assert.equal(b.json.ouverte, true);
        assert.ok(stripe.produits.get("xpg-ed-PROFESSIONAL").active, "le produit chez Stripe n'est pas touche");
        assert.equal((await appel(A, "GET", "/v1/boutique")).json.articles.length, 1, "la boutique vend toujours");
        assert.equal((await appel(A, "GET", "/admin/versions", { cle: editeur })).json.versions.length, 1);
    });

    it("le Studio active : son renouvellement ne connait plus sa licence (il la perd) ; son code ne vaut plus ; ses messages, rien de neuf", async () => {
        const r = await appel(A, "POST", "/v1/renouveler", { cle: pc, corps: { station: pc.station } });
        ok(r);
        assert.deepEqual(r.json.activations, [], "aucune activation : la licence XPG-2026-R0001 est inconnue du service");
        assert.ok(!r.json.retirees.includes("XPG-2026-R0001") || r.json.motifs["XPG-2026-R0001"] === "inconnue");
        assert.equal(r.json.poste, null, "son etat de poste est efface");
        assert.equal((await appel(A, "GET", "/v1/messages?depuis=0", { cle: pc })).json.messages.length, 0);
        erreur(await appel(A, "POST", "/v1/flottante/jeton", { cle: pc, corps: { licence: "XPG-2026-R0001", station: pc.station } }), 403, "activation");
        assert.equal((await appel(A, "GET", "/v1/boutique/commandes", { cle: pc2 })).json.commandes.length, 0);
    });

    it("l'admin n'est plus admin (403) ; la liste publique est vide et plus recente", async () => {
        const r = await appel(A, "GET", "/admin/etat", { cle: lea });
        assert.equal(r.statut, 403, r.texte);
        const l = await appel(A, "GET", "/v1/admins");
        ok(l);
        assert.ok(l.texte.includes('"numero":' + (avant.admins + 1)) && l.texte.includes('"admins":[]'), l.texte);
    });
});

describe("la sauvegarde faite avant remet tout (Restaurer)", () => {
    it("restauree dans le meme service : les licences, l'activation, l'admin, la commande reviennent", async () => {
        ok(await appel(A, "POST", "/admin/restauration/debut", { cle: editeur, corps: { schema: sauvegarde.schema, remplacer: true } }));
        for (const [t, lignes] of Object.entries(sauvegarde.tables))
            if (lignes.length) ok(await appel(A, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: t, lignes } }));
        const f = await appel(A, "POST", "/admin/restauration/fin", { cle: editeur });
        ok(f);
        assert.equal(f.json.cle_service, cleService);
        assert.equal((await appel(A, "GET", "/admin/licences", { cle: editeur })).json.licences.length, 2);
        const r = await appel(A, "POST", "/v1/renouveler", { cle: pc, corps: { station: pc.station } });
        ok(r);
        assert.equal(r.json.activations.length, 1, "le Studio retrouve son bail");
        assert.equal((await appel(A, "GET", "/admin/etat", { cle: lea })).statut, 200, "l'admin est revenu");
        assert.equal((await appel(A, "GET", "/admin/commandes", { cle: editeur })).json.commandes.length, 1);
        assert.equal((await appel(A, "GET", "/v1/messages?depuis=0", { cle: pc })).json.messages.length, 1);
    });
});
