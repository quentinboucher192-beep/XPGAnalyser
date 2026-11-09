// =============================================================================
//  service/test/relais.test.js - 1.44.0 : LE RELAIS DES SESSIONS PAR INTERNET
//  (src/relais.js), essaye dans le VRAI runtime (workerd, par
//  outils/service/lancer-local.sh), par HTTP : ouvrir, frapper, envoyer,
//  recevoir (l'attente longue), fermer ; les droits, les tailles, les limites,
//  les rejeux ; la session gardee a la relance, effacee apres 12 h
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, hasard, lancerService, nouvelleCle } from "./aides.js";

let svc;
const hote = nouvelleCle();
const lea = nouvelleCle();
const marc = nouvelleCle();
const intrus = nouvelleCle();
const b64 = (s) => Buffer.from(s).toString("base64");
const deB64 = (s) => Buffer.from(s, "base64").toString("utf8");

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
    assert.ok(typeof r.json.message === "string" && r.json.message.length > 5);
}

const ouvrir = (cle, locator, extra = {}) => appel(svc, "POST", "/v1/relais/ouvrir", { cle, corps: { locator, nom: "Mise en service Armoire_Gaz", hote: "Camille", projet: "Armoire_Gaz", ...extra } });
const frapper = (cle, locator) => appel(svc, "POST", "/v1/relais/" + locator + "/frapper", { cle, corps: {} });
const envoyer = (cle, locator, corps) => appel(svc, "POST", "/v1/relais/" + locator + "/envoyer", { cle, corps });
const recevoir = (cle, locator, { depuis = 0, conn, attente = 0 } = {}) =>
    appel(svc, "GET", "/v1/relais/" + locator + "/recevoir?depuis=" + depuis + (conn !== undefined ? "&conn=" + conn : "") + "&attente=" + attente, { cle });
const fermer = (cle, locator, corps = {}) => appel(svc, "POST", "/v1/relais/" + locator + "/fermer", { cle, corps });

before(async () => {
    // Les limites du relais restent (chaque appel vient d'une adresse tiree au hasard : le limiteur de Cloudflare ne
    // gene pas).
    svc = await lancerService({});
});

after(async () => {
    if (svc) {
        await svc.arreter();
        fs.rmSync(svc.etat, { recursive: true, force: true });
    }
});

describe("ouvrir une session (l'hote)", () => {
    it("un appel non signe, un localisateur mal ecrit, sans nom : refuses", async () => {
        const loc = hasard(16);
        erreur(await appel(svc, "POST", "/v1/relais/ouvrir", { corps: { locator: loc, nom: "x" } }), 401, "signature");
        erreur(await ouvrir(hote, "XYZ"), 400, "champ");
        erreur(await ouvrir(hote, loc.toUpperCase()), 400, "champ");
        erreur(await ouvrir(hote, loc, { nom: "" }), 400, "champ");
        erreur(await appel(svc, "GET", "/v1/relais/" + loc + "/autre", { cle: hote }), 404, "route");
        erreur(await appel(svc, "GET", "/v1/relais/" + loc + "/frapper", { cle: hote }), 405, "methode");
    });

    it("ouverte ; le meme localisateur par une autre cle : 409 ; par le meme hote : rouverte", async () => {
        const loc = hasard(16);
        const r = await ouvrir(hote, loc);
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.ok, true);
        erreur(await ouvrir(intrus, loc), 409, "pris");
        const f = await frapper(lea, loc);
        assert.equal(f.statut, 200, f.texte);
        assert.equal((await ouvrir(hote, loc)).statut, 200);
        // l'equipier d'avant est prevenu, puis sa connexion est fermee
        const m = await recevoir(lea, loc, { conn: f.json.conn });
        assert.equal(m.statut, 200, m.texte);
        assert.equal(m.json.messages.length, 1);
        assert.equal(m.json.messages[0].fin, true);
        assert.match(m.json.messages[0].raison, /redémarré/);
        erreur(await recevoir(lea, loc, { conn: f.json.conn, depuis: m.json.suivant }), 410, "fermee");
    });
});

describe("frapper, envoyer, recevoir", () => {
    const loc = hasard(16);
    let conn = 0;
    let conn2 = 0;

    it("frapper une session inconnue : 404 ; frapper : une connexion, le nom de la session", async () => {
        erreur(await frapper(lea, hasard(16)), 404, "absente");
        assert.equal((await ouvrir(hote, loc)).statut, 200);
        erreur(await appel(svc, "POST", "/v1/relais/" + loc + "/frapper", { corps: {} }), 401, "signature");
        const r = await frapper(lea, loc);
        assert.equal(r.statut, 200, r.texte);
        assert.ok(Number.isInteger(r.json.conn) && r.json.conn >= 1);
        assert.equal(r.json.nom, "Mise en service Armoire_Gaz");
        assert.equal(r.json.hote, "Camille");
        assert.equal(r.json.projet, "Armoire_Gaz");
        conn = r.json.conn;
        const r2 = await frapper(marc, loc);
        assert.equal(r2.statut, 200);
        conn2 = r2.json.conn;
        assert.notEqual(conn, conn2);
    });

    it("l'equipier envoie a l'hote : l'hote lit (de, cle), acquitte, la boite se vide", async () => {
        const e = await envoyer(lea, loc, { conn, a: "hote", donnees: b64("bonjour") });
        assert.equal(e.statut, 200, e.texte);
        const r = await recevoir(hote, loc);
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.messages.length, 1);
        const m = r.json.messages[0];
        assert.equal(m.de, conn);
        assert.equal(m.cle, lea.hex);
        assert.equal(deB64(m.donnees), "bonjour");
        assert.equal(r.json.suivant, m.n + 1);
        // pas acquitte : relu
        assert.equal((await recevoir(hote, loc)).json.messages.length, 1);
        const r2 = await recevoir(hote, loc, { depuis: r.json.suivant });
        assert.equal(r2.json.messages.length, 0);
        assert.equal(r2.json.suivant, r.json.suivant);
        // deja acquitte : plus jamais rendu
        assert.equal((await recevoir(hote, loc, { depuis: 0 })).json.messages.length, 0);
    });

    it("l'hote envoie a une connexion, et a plusieurs d'un appel (envois) ; une connexion inconnue est dite", async () => {
        const e = await envoyer(hote, loc, { a: conn, donnees: b64("a Lea") });
        assert.equal(e.statut, 200, e.texte);
        assert.deepEqual(e.json.refuses, []);
        assert.deepEqual(e.json.inconnues, []);
        const lot = await envoyer(hote, loc, { envois: [{ a: conn, donnees: b64("1") }, { a: conn2, donnees: b64("2") }, { a: 999, donnees: b64("3") }] });
        assert.equal(lot.statut, 200, lot.texte);
        assert.deepEqual(lot.json.inconnues, [999]);
        const l = await recevoir(lea, loc, { conn });
        assert.deepEqual(
            l.json.messages.map((m) => [m.de, deB64(m.donnees)]),
            [
                ["hote", "a Lea"],
                ["hote", "1"],
            ],
        );
        const m = await recevoir(marc, loc, { conn: conn2 });
        assert.deepEqual(
            m.json.messages.map((x) => deB64(x.donnees)),
            ["2"],
        );
        await recevoir(lea, loc, { conn, depuis: l.json.suivant });
        await recevoir(marc, loc, { conn: conn2, depuis: m.json.suivant });
    });

    it("l'attente longue : la reponse part des qu'un message arrive ; sinon vide apres l'attente", async () => {
        const t0 = Date.now();
        const attente = recevoir(lea, loc, { conn, depuis: 0, attente: 15 });
        await new Promise((r) => setTimeout(r, 400));
        assert.equal((await envoyer(hote, loc, { a: conn, donnees: b64("vite") })).statut, 200);
        const r = await attente;
        assert.equal(r.statut, 200, r.texte);
        assert.ok(Date.now() - t0 < 8000, "reveille par le message");
        assert.equal(deB64(r.json.messages[r.json.messages.length - 1].donnees), "vite");
        const t1 = Date.now();
        const vide = await recevoir(lea, loc, { conn, depuis: r.json.suivant, attente: 1 });
        assert.equal(vide.statut, 200);
        assert.equal(vide.json.messages.length, 0);
        assert.ok(Date.now() - t1 >= 900, "l'attente demandee");
        erreur(await recevoir(lea, loc, { conn, attente: 26 }), 400, "champ");
    });

    it("les droits : la connexion d'un autre, l'hote seulement", async () => {
        erreur(await envoyer(intrus, loc, { conn, a: "hote", donnees: b64("x") }), 403, "connexion");
        erreur(await recevoir(intrus, loc, { conn }), 403, "connexion");
        erreur(await recevoir(intrus, loc), 403, "hote");
        erreur(await envoyer(lea, loc, { a: conn2, donnees: b64("x") }), 403, "hote");
        erreur(await envoyer(lea, loc, { conn, a: conn2, donnees: b64("x") }), 400, "champ");
        erreur(await fermer(intrus, loc), 403, "hote");
        erreur(await fermer(intrus, loc, { conn, partir: true }), 403, "connexion");
        erreur(await envoyer(lea, loc, { conn: 777, a: "hote", donnees: b64("x") }), 410, "fermee");
    });

    it("un appel rejoue (le meme nonce) : refuse ; une heure trop loin : refusee", async () => {
        const nonce = hasard(16);
        assert.equal((await recevoir(hote, loc, { depuis: 0 })).statut, 200);
        const a = await appel(svc, "GET", "/v1/relais/" + loc + "/recevoir?depuis=0&attente=0", { cle: hote, nonce });
        assert.equal(a.statut, 200, a.texte);
        erreur(await appel(svc, "GET", "/v1/relais/" + loc + "/recevoir?depuis=0&attente=0", { cle: hote, nonce }), 401, "rejeu");
        erreur(await appel(svc, "GET", "/v1/relais/" + loc + "/recevoir?depuis=0&attente=0", { cle: hote, date: Math.floor(Date.now() / 1000) + svc.ecart - 3600 }), 401, "heure");
    });

    it("les tailles : 1,5 Mio par appel, du base64 ; une boite pleine (16 Mio) : refusee jusqu'a la lecture", async () => {
        erreur(await envoyer(lea, loc, { conn, a: "hote", donnees: "pas du base64 !" }), 400, "champ");
        erreur(await envoyer(lea, loc, { conn, a: "hote", donnees: Buffer.alloc(1.5 * 1024 * 1024 + 3, 7).toString("base64") }), 413, "taille");
        const gros = Buffer.alloc(1.5 * 1024 * 1024, 9).toString("base64");
        let refus = null;
        let envoyes = 0;
        for (let i = 0; i < 12 && !refus; i++) {
            const e = await envoyer(hote, loc, { a: conn2, donnees: gros });
            assert.equal(e.statut, 200, e.texte);
            if (e.json.refuses.length) refus = e.json;
            else envoyes++;
        }
        assert.ok(refus, "une boite de 16 Mio deborde");
        assert.deepEqual(refus.refuses, [conn2]);
        assert.ok(refus.reessayer_dans >= 1);
        assert.equal(envoyes, 10);
        // Marc lit (une reponse : 4 Mio de base64 au plus), acquitte : la boite se vide, l'envoi repasse.
        let depuis = 0;
        let lus = 0;
        for (let i = 0; i < 20 && lus < envoyes; i++) {
            const r = await recevoir(marc, loc, { conn: conn2, depuis });
            assert.equal(r.statut, 200);
            assert.ok(r.json.messages.length >= 1 && r.json.messages.length <= 2, "une reponse bornee");
            lus += r.json.messages.length;
            depuis = r.json.suivant;
        }
        assert.equal(lus, envoyes);
        await recevoir(marc, loc, { conn: conn2, depuis });
        const e = await envoyer(hote, loc, { a: conn2, donnees: gros });
        assert.deepEqual(e.json.refuses, []);
    });

    it("fermer : l'equipier part (l'hote le voit) ; l'hote renvoie ; l'hote arrete (tous le voient, puis 410)", async () => {
        let h = await recevoir(hote, loc);
        let depuis = h.json.suivant;
        assert.equal((await fermer(marc, loc, { conn: conn2, partir: true })).statut, 200);
        h = await recevoir(hote, loc, { depuis });
        assert.equal(h.json.messages.length, 1);
        assert.equal(h.json.messages[0].de, conn2);
        assert.equal(h.json.messages[0].fin, true);
        depuis = h.json.suivant;
        erreur(await recevoir(marc, loc, { conn: conn2 }), 410, "fermee");
        // un troisieme equipier, renvoye par l'hote
        const z = nouvelleCle();
        const f = await frapper(z, loc);
        assert.equal(f.statut, 200);
        assert.equal((await fermer(hote, loc, { conn: f.json.conn, raison: "code faux" })).statut, 200);
        const zr = await recevoir(z, loc, { conn: f.json.conn });
        assert.equal(zr.json.messages[0].fin, true);
        assert.equal(zr.json.messages[0].raison, "code faux");
        erreur(await envoyer(z, loc, { conn: f.json.conn, a: "hote", donnees: b64("x") }), 410, "fermee");
        // l'hote arrete : Lea recoit la fin (en attente longue), puis 410 ; la session n'existe plus
        const attente = recevoir(lea, loc, { conn, depuis: 0, attente: 15 });
        await new Promise((r) => setTimeout(r, 300));
        assert.equal((await fermer(hote, loc, { raison: "Camille a arrêté la session" })).statut, 200);
        const l = await attente;
        assert.equal(l.statut, 200, l.texte);
        const fin = l.json.messages[l.json.messages.length - 1];
        assert.equal(fin.fin, true);
        assert.equal(fin.raison, "Camille a arrêté la session");
        erreur(await recevoir(lea, loc, { conn, depuis: l.json.suivant }), 410, "fermee");
        erreur(await frapper(nouvelleCle(), loc), 404, "absente");
        erreur(await recevoir(hote, loc), 410, "fermee");
    });
});

describe("les limites", () => {
    it("12 connexions ouvertes au plus ; frapper : 10 fois en 10 min par cle", async () => {
        const loc = hasard(16);
        assert.equal((await ouvrir(hote, loc)).statut, 200);
        for (let i = 0; i < 12; i++) assert.equal((await frapper(nouvelleCle(), loc)).statut, 200);
        erreur(await frapper(nouvelleCle(), loc), 409, "pleine");
        const loc2 = hasard(16);
        assert.equal((await ouvrir(hote, loc2)).statut, 200);
        const k = nouvelleCle();
        for (let i = 0; i < 10; i++) {
            const f = await frapper(k, loc2);
            assert.equal(f.statut, 200, f.texte);
            await fermer(k, loc2, { conn: f.json.conn, partir: true });
        }
        const trop = await frapper(k, loc2);
        erreur(trop, 429, "trop");
        assert.ok(trop.json.reessayer_dans > 0);
    });
});

describe("la session gardee a la relance, effacee apres 12 h sans activite", () => {
    it("relance (+1 h) : la session est connue (l'hote absent : 503) ; relance (+13 h) : effacee (404)", async () => {
        const loc = hasard(16);
        assert.equal((await ouvrir(hote, loc)).statut, 200);
        const etat = svc.etat;
        await svc.arreter();
        svc = await lancerService({ etat, decalage: 3600 });
        erreur(await frapper(lea, loc), 503, "hote_absent");
        // l'hote revient (il lit) : on frappe de nouveau
        assert.equal((await recevoir(hote, loc)).statut, 200);
        assert.equal((await frapper(lea, loc)).statut, 200);
        await svc.arreter();
        svc = await lancerService({ etat, decalage: 3600 + 13 * 3600 });
        erreur(await frapper(lea, loc), 404, "absente");
        erreur(await recevoir(hote, loc), 404, "absente");
    });
});
