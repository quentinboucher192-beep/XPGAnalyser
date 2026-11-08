// =============================================================================
//  service/test/temps.test.js - le temps qui passe, dans le VRAI runtime : le
//  service est relance sur le meme etat avec son horloge avancee
//  (XPG_DECALAGE_HORLOGE_ESSAI) - un jeton qui expire, un bail renouvele, un
//  bail echu dont la place est reprise (le PC est retire), la Decouverte qui
//  garde son premier debut, un certificat expire ; une licence finie qui n'est
//  plus renouvelee (et le bail qui s'arrete a sa fin) ; les transferts sur 30
//  jours glissants ; le menage (activations finies, demandes closes)
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import { after, before, describe, it } from "node:test";

import { appel, bloc, certificat, demandeJson, hasard, iso, lancerService, licenceJson, lireBloc, nouveauCode, nouvelleCle } from "./aides.js";

const JOUR = 86400;
const now = () => Math.floor(Date.now() / 1000);

const editeur = nouvelleCle();
const etat = fs.mkdtempSync(path.join(os.tmpdir(), "xpg-service-temps-"));
let svc = null;
let cleService = "";

async function relancer(decalage, jours, sansLimites) {
    if (svc) await svc.arreter();
    svc = await lancerService({ cles: [editeur.hex], decalage, jours, etat, sansLimites });
    const e = await appel(svc, "GET", "/v1/etat");
    assert.ok(Math.abs(e.json.heure - (now() + decalage)) < 30, "l'horloge du service est avancee");
    if (cleService) assert.equal(e.json.cle_service, cleService, "la cle du service est gardee d'un lancement a l'autre");
    cleService = e.json.cle_service;
    return e.json;
}

async function activer(cle, code) {
    const demande = bloc("demande", demandeJson(cle), cle);
    return appel(svc, "POST", "/v1/activer", { cle, corps: { code, demande } });
}

const activation = (r) => JSON.parse(lireBloc(r).json);
const renouveler = (cle) => appel(svc, "POST", "/v1/renouveler", { cle, corps: { station: cle.station } });
const jeton = (cle, licence) => appel(svc, "POST", "/v1/flottante/jeton", { cle, corps: { licence, station: cle.station } });
const liberer = (cle, licence) => appel(svc, "POST", "/v1/liberer", { cle, corps: { licence, station: cle.station } });
async function licenceChezLeService(id) {
    const l = await appel(svc, "GET", "/admin/licences", { cle: editeur });
    return l.json.licences.find((x) => x.licence === id);
}
async function deposer(lic, code, expire) {
    const t = now();
    const texte = bloc("licence", licenceJson({ ...lic, emission: t - 60, expire }), editeur);
    const r = await appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: texte, code_hash: code.hash, bail_jours: 30 } });
    assert.equal(r.statut, 200, r.texte);
}

const L = { id: "XPG-2026-T0001", max: 1 };
const CL = nouveauCode();
const F = { id: "XPG-2026-T0002", type: "floating", max: 1 };
const CF = nouveauCode();
const A = nouvelleCle();
const B = nouvelleCle();
const C = nouvelleCle();
const E = nouvelleCle();
const D = nouvelleCle();
const machine = hasard(16);
let debut0 = "";
// une licence qui finit dans 5 jours (G l'active) ; une licence d'un poste pour les transferts (P1..P4)
const X = { id: "XPG-2026-T0003", max: 1 };
const CX = nouveauCode();
const G = nouvelleCle();
let finX = 0;
const Y = { id: "XPG-2026-T0004", type: "perpetual", max: 1 };
const CY = nouveauCode();
const [P1, P2, P3, P4] = [nouvelleCle(), nouvelleCle(), nouvelleCle(), nouvelleCle()];
// les demandes manuelles : H en depose deux (la premiere est remplacee)
const H = nouvelleCle();
let demande1 = 0;
let demande2 = 0;

before(async () => {
    await relancer(0);
});

after(async () => {
    if (svc) await svc.arreter();
    fs.rmSync(etat, { recursive: true, force: true });
});

describe("le temps qui passe", () => {
    it("au debut : le certificat, deux licences, A active, C prend un jeton, D commence sa Decouverte", async () => {
        const t = now();
        assert.equal((await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: certificat(editeur, cleService, { expire: t + 365 * JOUR }) } })).statut, 200);
        for (const [lic, code] of [
            [L, CL],
            [F, CF],
        ]) {
            const texte = bloc("licence", licenceJson({ ...lic, emission: t - 60, expire: t + 2 * 365 * JOUR }), editeur);
            const r = await appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: texte, code_hash: code.hash, bail_jours: 30 } });
            assert.equal(r.statut, 200, r.texte);
        }
        const a = await activer(A, CL.code);
        assert.equal(a.statut, 200, a.texte);
        const d = activation(a.json.activation);
        assert.equal(Date.parse(d.jusqu_a) / 1000 - Date.parse(d.emis) / 1000, 30 * JOUR);
        for (const p of [C, E]) assert.equal((await activer(p, CF.code)).statut, 200);
        assert.equal((await jeton(C, F.id)).statut, 200);
        const c = await jeton(E, F.id);
        assert.equal(c.statut, 409);
        const r = await appel(svc, "POST", "/v1/decouverte", { cle: D, corps: { machine } });
        assert.equal(r.statut, 200, r.texte);
        debut0 = JSON.parse(lireBloc(r.texte).json).debut;
    });

    it("au debut : une licence qui finit dans 5 jours (G l'active)", async () => {
        finX = now() + 5 * JOUR;
        await deposer(X, CX, finX);
        const g = await activer(G, CX.code);
        assert.equal(g.statut, 200, g.texte);
    });

    it("au debut : trois transferts sur une licence d'un poste - le troisieme garde sa place jusqu'a la fin de son bail", async () => {
        await deposer(Y, CY, 0);
        for (const p of [P1, P2]) {
            assert.equal((await activer(p, CY.code)).statut, 200);
            assert.equal((await liberer(p, Y.id)).json.place, "rendue");
        }
        assert.equal((await activer(P3, CY.code)).statut, 200);
        assert.equal((await liberer(P3, Y.id)).json.place, "gardee");
        assert.equal((await activer(P4, CY.code)).statut, 409);
    });

    it("au debut : deux demandes manuelles de H (la premiere est remplacee)", async () => {
        const scelle = Buffer.from("x".repeat(3000)).toString("base64");
        demande1 = (await appel(svc, "POST", "/v1/demandes", { cle: H, corps: { cle: editeur.id, scelle } })).json.numero;
        demande2 = (await appel(svc, "POST", "/v1/demandes", { cle: H, corps: { cle: editeur.id, scelle } })).json.numero;
        assert.ok(demande1 > 0 && demande2 === demande1 + 1);
    });

    it("20 minutes plus tard : le jeton de C a expire, E prend le sien ; un appel signe a l'ancienne heure est refuse", async () => {
        await relancer(1200);
        const r = await jeton(E, F.id);
        assert.equal(r.statut, 200, r.texte);
        const l = await appel(svc, "GET", "/admin/licences", { cle: editeur });
        assert.deepEqual(
            l.json.licences.find((x) => x.licence === F.id).jetons.map((j) => j.poste),
            [E.hex],
            "seuls les jetons en cours sont montres",
        );
        const vieux = await appel(svc, "GET", "/admin/cle-service", { cle: editeur, date: now() });
        assert.equal(vieux.statut, 401);
        assert.equal(vieux.json.erreur, "heure");
    });

    it("le decalage de l'horloge n'est ecoute que pour 127.0.0.1 ou localhost (en ligne, il ne fait rien)", async () => {
        const u = new URL(svc.base);
        const lire = (hote) =>
            new Promise((ok, ko) => {
                http.get({ host: u.hostname, port: u.port, path: "/v1/etat", headers: { Host: hote } }, (r) => {
                    let t = "";
                    r.on("data", (d) => (t += d));
                    r.on("end", () => ok(JSON.parse(t)));
                }).on("error", ko);
            });
        assert.ok(Math.abs((await lire("localhost:" + u.port)).heure - (now() + 1200)) < 30);
        assert.ok(Math.abs((await lire("licences.example.com")).heure - now()) < 30, "une autre adresse : l'heure vraie");
    });

    it("10 jours plus tard : A renouvelle son bail (30 jours a partir de maintenant) ; B ne trouve pas de place", async () => {
        await relancer(10 * JOUR);
        const r = await renouveler(A);
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.activations.length, 1);
        const d = activation(r.json.activations[0].activation);
        assert.ok(Math.abs(Date.parse(d.jusqu_a) / 1000 - (now() + 40 * JOUR)) < 30, d.jusqu_a);
        const b = await activer(B, CL.code);
        assert.equal(b.statut, 409);
        assert.equal(b.json.erreur, "complet");
    });

    it("10 jours plus tard : la licence de G a fini - plus renouvelee (motif expiree), plus activable", async () => {
        const r = await renouveler(G);
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(r.json.activations, []);
        assert.deepEqual(r.json.retirees, [X.id]);
        assert.deepEqual(r.json.motifs, { [X.id]: "expiree" });
        const g = await activer(G, CX.code);
        assert.equal(g.statut, 410);
        assert.equal(g.json.erreur, "expiree");
        // une emission plus recente, deposee par l'editeur, la prolonge : de nouveau renouvelee
        await deposer(X, CX, now() + 10 * JOUR + 60 * JOUR);
        const re = await renouveler(G);
        assert.equal(re.json.activations.length, 1, re.texte);
    });

    it("10 jours plus tard : la demande remplacee depuis plus de 7 jours est effacee ; celle qui attend reste", async () => {
        const l = await appel(svc, "GET", "/admin/demandes?depuis=0", { cle: editeur });
        const numeros = l.json.demandes.map((d) => d.numero);
        assert.ok(!numeros.includes(demande1), "la remplacee est effacee");
        assert.ok(numeros.includes(demande2), "celle qui attend reste");
        const st = await appel(svc, "GET", "/admin/stats", { cle: editeur });
        assert.equal(st.json.demandes.remplacee, 0);
        assert.equal(st.json.demandes.octets, l.json.demandes.reduce((n, d) => n + d.scelle.length, 0), "le compteur des octets suit");
    });

    it("31 jours plus tard : le bail de A court encore ; la Decouverte garde son premier debut ; DECOUVERTE_JOURS", async () => {
        await relancer(31 * JOUR, 14, true);
        assert.equal((await activer(B, CL.code)).statut, 409);
        const D2 = nouvelleCle();
        const r = await appel(svc, "POST", "/v1/decouverte", { cle: D2, corps: { machine } });
        assert.equal(r.statut, 200, r.texte);
        const d = JSON.parse(lireBloc(r.texte).json);
        assert.equal(d.debut, debut0);
        assert.equal(d.jours, 14);
        assert.equal(d.poste, D2.hex);
        const neuve = JSON.parse(lireBloc((await appel(svc, "POST", "/v1/decouverte", { cle: D2, corps: { machine: hasard(16) } })).texte).json);
        assert.ok(Math.abs(Date.parse(neuve.debut) / 1000 - (now() + 31 * JOUR)) < 30, "une machine neuve commence maintenant");
        // LIMITES_ESSAI=sans (ce lancement) : plus de 10 Decouvertes de la meme adresse passent
        for (let i = 0; i < 12; i++)
            assert.equal((await appel(svc, "POST", "/v1/decouverte", { cle: D2, ip: "192.0.2.1", corps: { machine: hasard(16) } })).statut, 200);
    });

    it("les limites reviennent au lancement suivant (sans XPG_LIMITES_ESSAI)", async () => {
        await relancer(31 * JOUR + 60);
        let dernier = 0;
        for (let i = 0; i < 11; i++) dernier = (await appel(svc, "POST", "/v1/decouverte", { cle: nouvelleCle(), ip: "192.0.2.2", corps: { machine: hasard(16) } })).statut;
        assert.equal(dernier, 429);
    });

    it("31 jours plus tard : les transferts d'il y a plus de 30 jours ne comptent plus ; la place gardee est revenue", async () => {
        const p4 = await activer(P4, CY.code);
        assert.equal(p4.statut, 200, p4.texte);
        const l = await liberer(P4, Y.id);
        assert.equal(l.json.place, "rendue");
        assert.equal(l.json.transferts, 1, "une fenetre de 30 jours glissants");
    });

    it("41 jours plus tard : le bail de A est echu, B prend la place ; A, revenu, est retire", async () => {
        await relancer(41 * JOUR);
        const b = await activer(B, CL.code);
        assert.equal(b.statut, 200, b.texte);
        // 1.38.0 : A (son bail echu) ne tient plus de place - B ne voit que lui, une place prise sur une
        const vb = await renouveler(B);
        assert.deepEqual([vb.json.postes[L.id].utilises, vb.json.postes[L.id].liste.map((x) => x.cle)], [1, [B.hex]]);
        const r = await renouveler(A);
        assert.deepEqual(r.json.activations, []);
        assert.deepEqual(r.json.retirees, [L.id]);
        assert.deepEqual(r.json.motifs, { [L.id]: "retiree" });
        const l = await appel(svc, "GET", "/admin/licences", { cle: editeur });
        const e = l.json.licences.find((x) => x.licence === L.id);
        assert.deepEqual(Object.fromEntries(e.activations.map((x) => [x.poste, x.etat])), { [A.hex]: "retiree", [B.hex]: "active" });
        const a = await activer(A, CL.code);
        assert.equal(a.statut, 409, "la place est prise par B");
        // le bail echu d'un PC dont la place est libre est renouvele : E (flottante) n'a pas de place a perdre
        const re = await renouveler(E);
        assert.equal(re.json.activations.length, 1);
        assert.ok(Math.abs(Date.parse(activation(re.json.activations[0].activation).jusqu_a) / 1000 - (now() + 71 * JOUR)) < 30);
    });

    it("50 jours plus tard : les activations finies depuis plus de 7 jours sont effacees (le compteur suit)", async () => {
        await relancer(50 * JOUR);
        const e = await licenceChezLeService(L.id);
        assert.deepEqual(
            e.activations.map((x) => [x.poste, x.etat]),
            [[B.hex, "active"]],
            "A (retire, bail fini le 40e jour) est efface",
        );
        const y = await licenceChezLeService(Y.id);
        assert.deepEqual(
            y.activations.map((x) => x.poste),
            [P4.hex],
            "les PC liberes de Y dont le bail a fini il y a plus de 7 jours sont effaces ; P4 (bail jusqu'au 61e jour) reste",
        );
        const r = await renouveler(A);
        assert.deepEqual([r.json.activations, r.json.retirees], [[], []], "A, revenu : le service ne le connait plus");
        const st = await appel(svc, "GET", "/admin/stats", { cle: editeur });
        const parEtat = ["active", "liberee", "retiree", "revoquee"].reduce((n, k) => n + st.json.activations[k], 0);
        assert.equal(st.json.activations.lignes, parEtat, "le compteur des lignes d'activation suit le menage");
    });

    it("plus d'un an plus tard : le certificat a expire - le service le dit (503), l'etat aussi", async () => {
        await relancer(400 * JOUR);
        const e = await appel(svc, "GET", "/v1/etat");
        assert.equal(e.json.certifie, false);
        assert.equal((await appel(svc, "GET", "/v1/heure?n=" + hasard(16))).statut, 503);
        const r = await renouveler(B);
        assert.equal(r.statut, 503);
        assert.equal(r.json.erreur, "certificat");
        // un certificat neuf (signe a l'heure du service) : tout repart
        const t = now() + 400 * JOUR;
        const c = certificat(editeur, cleService, { emis: t - 60, expire: t + 365 * JOUR });
        assert.equal((await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: c } })).statut, 200);
        assert.equal((await renouveler(B)).statut, 200);
        assert.equal(iso(t).length, 20);
    });
});
