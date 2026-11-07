// =============================================================================
//  service/test/service.test.js - le Worker du service des licences, essaye
//  dans le VRAI runtime (workerd, par outils/service/lancer-local.sh), par
//  HTTP : chaque route du contrat (docs/SERVICE-LICENCES.md) et ses refus
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import {
    DATE_ISO,
    appel,
    bloc,
    blocSignePar,
    certificat,
    copieAilleurs,
    demandeJson,
    hasard,
    iso,
    lancerService,
    licenceJson,
    lignesEcrites,
    lireBloc,
    nouveauCode,
    nouvelleCle,
    reponses,
    sha256Hex,
} from "./aides.js";

const JOUR = 86400;
const now = () => Math.floor(Date.now() / 1000);

const editeur = nouvelleCle();
const editeur2 = nouvelleCle(); // une deuxieme cle de l'editeur (une rotation)
const intrus = nouvelleCle();
let svc;
let cleService = "";
const codes = []; // les codes employes : jamais dans une reponse

function pc() {
    return nouvelleCle();
}

async function deposerLicence(opts, extra = {}, signataire = editeur) {
    const texte = bloc("licence", licenceJson(opts), signataire);
    return appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: texte, ...extra } });
}

async function activer(cle, code, { nom, ip, demandeur } = {}) {
    const qui = demandeur || cle;
    const demande = bloc("demande", demandeJson(qui, { station: cle.station, nom: "PC d'essai" }), qui);
    return appel(svc, "POST", "/v1/activer", { cle, ip, corps: { code, demande, ...(nom !== undefined ? { nom } : {}) } });
}

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.ok(r.type.startsWith("application/json"), "une erreur est du JSON");
    assert.equal(r.json.erreur, code, r.texte);
    assert.equal(typeof r.json.message, "string");
    assert.ok(r.json.message.length > 5);
}

before(async () => {
    svc = await lancerService({ cles: [editeur.hex, editeur2.hex] });
});

after(async () => {
    if (svc) {
        await svc.arreter();
        fs.rmSync(svc.etat, { recursive: true, force: true });
    }
});

describe("l'etat public, avant tout certificat", () => {
    it("GET /v1/etat", async () => {
        const r = await appel(svc, "GET", "/v1/etat");
        assert.equal(r.statut, 200);
        assert.ok(r.type.startsWith("application/json"));
        assert.deepEqual(Object.keys(r.json), ["service", "version", "heure", "cle_service", "certifie", "registre"]);
        assert.equal(r.json.service, "xpg-licences");
        assert.equal(r.json.version, "1.35.0");
        assert.ok(Math.abs(r.json.heure - now()) < 30);
        assert.match(r.json.cle_service, /^[0-9a-f]{64}$/);
        assert.equal(r.json.certifie, false);
        assert.equal(r.json.registre, 0);
        cleService = r.json.cle_service;
    });

    it("sans certificat : l'heure 503, le certificat 404, le registre 404, la Decouverte 503", async () => {
        erreur(await appel(svc, "GET", "/v1/heure?n=" + hasard(16)), 503, "certificat");
        erreur(await appel(svc, "GET", "/v1/certificat"), 404, "certificat");
        erreur(await appel(svc, "GET", "/v1/registre"), 404, "registre");
        erreur(await appel(svc, "POST", "/v1/decouverte", { cle: pc(), corps: { machine: hasard(16) } }), 503, "certificat");
    });

    it("une adresse inconnue 404, une methode fausse 405", async () => {
        erreur(await appel(svc, "GET", "/v1/rien"), 404, "route");
        erreur(await appel(svc, "DELETE", "/v1/etat"), 405, "methode");
        erreur(await appel(svc, "GET", "/v1/activer"), 405, "methode");
    });
});

describe("les appels signes", () => {
    it("sans signature : 401", async () => {
        erreur(await appel(svc, "GET", "/admin/cle-service"), 401, "signature");
        erreur(await appel(svc, "POST", "/v1/renouveler", { corps: {} }), 401, "signature");
    });

    it("une cle qui n'est pas celle de l'editeur : 403", async () => {
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: intrus }), 403, "editeur");
    });

    it("une signature fausse, un autre chemin, un autre corps : 401", async () => {
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: editeur, signature: "ab".repeat(64) }), 401, "signature");
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: editeur, signeChemin: "/admin/stats" }), 401, "signature");
        erreur(await appel(svc, "POST", "/v1/renouveler", { cle: pc(), corps: {}, signeCorps: '{"x":1}' }), 401, "signature");
        // une signature d'une autre cle, presentee avec la cle de l'editeur
        const autre = nouvelleCle();
        const r = await appel(svc, "GET", "/admin/cle-service", { cle: { ...editeur, signer: autre.signer } });
        erreur(r, 401, "signature");
    });

    it("des en-tetes illisibles : 401", async () => {
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: editeur, nonce: "XYZ" }), 401, "signature");
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: editeur, nonce: hasard(16).toUpperCase() }), 401, "signature");
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: editeur, date: "12a" }), 401, "heure");
    });

    it("une heure a plus de 600 s : 401 (et l'heure du service)", async () => {
        const r = await appel(svc, "GET", "/admin/cle-service", { cle: editeur, date: now() - 700 });
        erreur(r, 401, "heure");
        assert.ok(Math.abs(r.json.heure - now()) < 30);
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: editeur, date: now() + 700 }), 401, "heure");
        const ok = await appel(svc, "GET", "/admin/cle-service", { cle: editeur, date: now() - 500 });
        assert.equal(ok.statut, 200, ok.texte);
    });

    it("un nonce rejoue : 401", async () => {
        const n = hasard(16);
        const r1 = await appel(svc, "GET", "/admin/cle-service", { cle: editeur, nonce: n });
        assert.equal(r1.statut, 200);
        erreur(await appel(svc, "GET", "/admin/cle-service", { cle: editeur, nonce: n }), 401, "rejeu");
        // le meme nonce, meme signe par un autre : refuse aussi
        erreur(await appel(svc, "POST", "/v1/renouveler", { cle: pc(), corps: {}, nonce: n }), 401, "rejeu");
    });

    it("GET /admin/cle-service : la cle, pas encore de certificat ; la deuxieme cle de l'editeur est crue", async () => {
        const r = await appel(svc, "GET", "/admin/cle-service", { cle: editeur2 });
        assert.equal(r.statut, 200);
        assert.deepEqual(r.json, { cle: cleService, certificat: null });
    });
});

describe("le certificat de service", () => {
    it("refuse : une autre cle, un autre signataire, expire, illisible", async () => {
        const autre = nouvelleCle();
        erreur(await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: certificat(editeur, autre.hex) } }), 409, "cle");
        erreur(await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: certificat(intrus, cleService) } }), 400, "signature");
        erreur(
            await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: certificat(editeur, cleService, { emis: now() - 100 * JOUR, expire: now() - JOUR }) } }),
            400,
            "expire",
        );
        erreur(await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: "pas un bloc" } }), 400, "document");
        const tronque = certificat(editeur, cleService).slice(0, 80);
        erreur(await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: tronque } }), 400, "document");
        erreur(await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: {} }), 400, "champ");
    });

    it("accepte : le certificat de l'editeur pour la cle du service", async () => {
        const c = certificat(editeur, cleService);
        const r = await appel(svc, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: c } });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.cle, cleService);
        const g = await appel(svc, "GET", "/v1/certificat");
        assert.equal(g.statut, 200);
        assert.ok(g.type.startsWith("text/plain"));
        assert.equal(g.texte, c);
        assert.ok(blocSignePar(g.texte, editeur.hex));
        const e = await appel(svc, "GET", "/v1/etat");
        assert.equal(e.json.certifie, true);
        const k = await appel(svc, "GET", "/admin/cle-service", { cle: editeur });
        assert.equal(k.json.certificat, c);
    });
});

describe("l'heure signee", () => {
    it("GET /v1/heure?n= : le bloc heure, signe par la cle du service", async () => {
        const n = hasard(16);
        const r = await appel(svc, "GET", "/v1/heure?n=" + n);
        assert.equal(r.statut, 200, r.texte);
        assert.ok(r.type.startsWith("text/plain"));
        const b = lireBloc(r.texte);
        assert.equal(b.genre, "heure");
        assert.equal(b.reste, "");
        assert.ok(blocSignePar(r.texte, cleService));
        const d = JSON.parse(b.json);
        assert.equal(b.json, JSON.stringify({ version: 1, heure: d.heure, n }), "le JSON exact, sans espace");
        assert.match(d.heure, DATE_ISO);
        assert.ok(Math.abs(Date.parse(d.heure) / 1000 - now()) < 30);
    });

    it("un nonce mal ecrit : 400", async () => {
        for (const q of ["", "?n=", "?n=" + hasard(16).toUpperCase(), "?n=" + hasard(15), "?n=" + hasard(17), "?n=" + "g".repeat(32), "?n=" + hasard(16) + "&n=" + hasard(16)])
            erreur(await appel(svc, "GET", "/v1/heure" + q), 400, "nonce");
    });
});

describe("le registre", () => {
    const registre = (numero, cle = editeur, sans = "FREE") =>
        bloc(
            "registre",
            JSON.stringify({ format: "xpg-registry", version: 1, number: numero, issued_at: iso(now()), unlicensed_edition: sans, features: [], editions: [] }, null, 2) + "\n",
            cle,
        );
    it("depose, relu, plus ancien refuse, le meme accepte", async () => {
        const r5 = registre(5);
        assert.equal((await appel(svc, "PUT", "/admin/registre", { cle: editeur, corps: { registre: r5 } })).statut, 200);
        const g = await appel(svc, "GET", "/v1/registre");
        assert.equal(g.statut, 200);
        assert.equal(g.texte, r5);
        assert.equal((await appel(svc, "GET", "/v1/etat")).json.registre, 5);
        erreur(await appel(svc, "PUT", "/admin/registre", { cle: editeur, corps: { registre: registre(4) } }), 409, "ancien");
        erreur(await appel(svc, "PUT", "/admin/registre", { cle: editeur, corps: { registre: registre(5, editeur, "AUTRE") } }), 409, "ancien");
        assert.equal((await appel(svc, "PUT", "/admin/registre", { cle: editeur, corps: { registre: r5 } })).statut, 200);
        erreur(await appel(svc, "PUT", "/admin/registre", { cle: editeur, corps: { registre: registre(9, intrus) } }), 400, "signature");
        assert.equal((await appel(svc, "PUT", "/admin/registre", { cle: editeur, corps: { registre: registre(6) } })).statut, 200);
        assert.equal((await appel(svc, "GET", "/v1/etat")).json.registre, 6);
    });
});

// Les licences des essais.
const L1 = { id: "XPG-2026-00001", type: "temporary", max: 2 };
const C1 = nouveauCode();
codes.push(C1);
const pc1 = pc();
const pc2 = pc();
const pc3 = pc();
const station = pc1.station; // l'empreinte de l'installation de Windows de pc1

// Les appels d'un PC qui portent sa station.
const renouveler = (cle, st = cle.station) => appel(svc, "POST", "/v1/renouveler", { cle, corps: st === null ? {} : { station: st } });
const jeton = (cle, licence, st = cle.station) => appel(svc, "POST", "/v1/flottante/jeton", { cle, corps: st === null ? { licence } : { licence, station: st } });
const rendre = (cle, licence, st = cle.station) => appel(svc, "POST", "/v1/flottante/rendre", { cle, corps: st === null ? { licence } : { licence, station: st } });
const liberer = (cle, licence, st = cle.station) => appel(svc, "POST", "/v1/liberer", { cle, corps: st === null ? { licence } : { licence, station: st } });
const retirer = (licence, cle) => appel(svc, "POST", "/admin/activations/retirer", { cle: editeur, corps: { licence, poste: cle.hex } });
async function licenceChezLeService(id) {
    const l = await appel(svc, "GET", "/admin/licences", { cle: editeur });
    return l.json.licences.find((x) => x.licence === id);
}

describe("les licences en ligne", () => {
    it("deposer : refus (signataire, empreinte du code, bail)", async () => {
        const opts = { ...L1, emission: now() - 60, expire: now() + 365 * JOUR };
        erreur(await deposerLicence(opts, { code_hash: C1.hash }, intrus), 400, "signature");
        erreur(await deposerLicence(opts, { code_hash: "ABC" }), 400, "champ");
        erreur(await deposerLicence(opts, { code_hash: C1.hash, bail_jours: 0 }), 400, "champ");
        erreur(await deposerLicence(opts, { code_hash: C1.hash, active: "oui" }), 400, "champ");
        const pasLicence = bloc("licence", JSON.stringify({ format: "xpg-registry", version: 1 }), editeur);
        erreur(await appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: pasLicence } }), 400, "document");
        // une licence liee par l'editeur (sans service, avec un PC) n'est pas une licence en ligne
        erreur(await deposerLicence({ ...opts, service: null }), 400, "document");
        const lie = [{ key: nouvelleCle().hex, name: "PC", activated_at: iso(now()) }];
        erreur(await deposerLicence({ ...opts, devices: lie }), 400, "document");
    });

    it("deposer : la licence, son code (l'empreinte), son bail", async () => {
        L1.emission = now() - 60;
        L1.expire = now() + 365 * JOUR;
        const r = await deposerLicence(L1, { code_hash: C1.hash, bail_jours: 30, active: true });
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(r.json, {
            licence: L1.id,
            emission: iso(L1.emission),
            type: "temporary",
            postes_max: 2,
            bail_jours: 30,
            active: true,
            code: true,
            activations: [],
            jetons: [],
        });
        assert.ok(!r.texte.includes(C1.hash), "l'empreinte du code ne ressort jamais");
    });

    it("un code deja pris par une autre licence : 409", async () => {
        erreur(await deposerLicence({ id: "XPG-2026-00099", emission: now(), expire: now() + JOUR }, { code_hash: C1.hash }), 409, "code");
    });
});

describe("l'activation par code", () => {
    let premiere;
    it("active : la licence, l'activation signee par le service, le certificat", async () => {
        // le code lu avec souplesse : minuscules, espaces, O pour 0, L pour 1
        const souple = " " + C1.code.toLowerCase().replace(/-/g, " ").replace(/0/g, "o").replace(/1/g, "l") + " ";
        const r = await activer(pc1, souple);
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(Object.keys(r.json), ["licence", "activation", "certificat", "message"]);
        const lic = lireBloc(r.json.licence);
        assert.equal(lic.genre, "licence");
        assert.ok(blocSignePar(r.json.licence, editeur.hex));
        assert.equal(lic.reste, "", "un seul bloc : la licence");
        assert.ok(blocSignePar(r.json.certificat, editeur.hex));
        const a = lireBloc(r.json.activation);
        assert.equal(a.genre, "activation");
        assert.ok(blocSignePar(r.json.activation, cleService), "signee par la cle du service");
        const d = JSON.parse(a.json);
        assert.deepEqual(Object.keys(d), ["version", "licence", "emission", "poste", "station", "type", "numero", "emis", "jusqu_a"]);
        assert.equal(a.json, JSON.stringify(d), "sans espace");
        assert.equal(d.version, 1);
        assert.equal(d.licence, L1.id);
        assert.equal(d.emission, iso(L1.emission));
        assert.equal(d.poste, pc1.hex);
        assert.equal(d.station, station);
        assert.equal(d.type, "fixe");
        assert.ok(Number.isInteger(d.numero) && d.numero >= 1);
        assert.match(d.emis, DATE_ISO);
        assert.match(d.jusqu_a, DATE_ISO);
        assert.equal(Date.parse(d.jusqu_a) - Date.parse(d.emis), 30 * JOUR * 1000);
        assert.match(r.json.message, /activée/);
        premiere = d;
    });

    it("deja active : la meme activation, renouvelee (un numero neuf), une seule place", async () => {
        const r = await activer(pc1, C1.code);
        assert.equal(r.statut, 200, r.texte);
        const d = JSON.parse(lireBloc(r.json.activation).json);
        assert.ok(d.numero > premiere.numero);
        assert.equal(d.station, station);
        const r2 = await activer(pc2, C1.code);
        assert.equal(r2.statut, 200, r2.texte);
        assert.equal(JSON.parse(lireBloc(r2.json.activation).json).station, pc2.station);
        erreur(await activer(pc3, C1.code), 409, "complet");
    });

    it("refus : code inconnu, illisible, demande d'un autre PC, demande absente, demande sans station", async () => {
        // (un PC neuf a chaque fois : 5 activations par heure et par PC)
        erreur(await activer(pc(), nouveauCode().code), 403, "code");
        erreur(await activer(pc(), "XPG-1234"), 403, "code");
        erreur(await activer(pc(), "XPG-UUUU-UUUU-UUUU-UUUU"), 403, "code");
        // une demande signee par un autre PC
        erreur(await activer(pc(), C1.code, { demandeur: pc2 }), 400, "demande");
        // une demande qui porte une autre cle, signee par ce PC
        const p = pc();
        const d = bloc("demande", demandeJson(nouvelleCle()), p);
        erreur(await appel(svc, "POST", "/v1/activer", { cle: p, corps: { code: C1.code, demande: d } }), 400, "demande");
        erreur(await appel(svc, "POST", "/v1/activer", { cle: pc(), corps: { code: C1.code } }), 400, "demande");
        erreur(await appel(svc, "POST", "/v1/activer", { cle: pc(), corps: { code: C1.code, demande: "rien" } }), 400, "demande");
        erreur(await appel(svc, "POST", "/v1/activer", { cle: pc(), corps: "{pas du json" }), 400, "json");
        // une demande sans l'empreinte de l'installation de Windows : refusee, avant meme le code
        const q = pc();
        const sans = bloc("demande", demandeJson(q, { station: null }), q);
        const r = await appel(svc, "POST", "/v1/activer", { cle: q, corps: { code: C1.code, demande: sans } });
        erreur(r, 400, "demande");
        assert.match(r.json.message, /station/);
    });
});

describe("renouveler, liberer, retirer", () => {
    it("renouveler : l'activation renouvelee, le certificat, pas de liste de revocation, pas de poste", async () => {
        const r = await renouveler(pc1);
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(Object.keys(r.json), ["activations", "retirees", "motifs", "certificat", "poste"]);
        assert.equal(r.json.activations.length, 1);
        assert.deepEqual(r.json.retirees, []);
        assert.deepEqual(r.json.motifs, {});
        assert.equal(r.json.poste, null);
        assert.ok(blocSignePar(r.json.certificat, editeur.hex));
        const a = r.json.activations[0];
        assert.ok(blocSignePar(a.activation, cleService));
        assert.equal(lireBloc(a.licence).genre, "licence");
        const d = JSON.parse(lireBloc(a.activation).json);
        assert.equal(d.licence, L1.id);
        assert.equal(d.poste, pc1.hex);
        assert.equal(d.station, station, "la station de la demande est gardee");
    });

    it("renouveler sans station, ou mal ecrite : 400", async () => {
        erreur(await renouveler(pc1, null), 400, "champ");
        erreur(await renouveler(pc1, "ABC"), 400, "champ");
        erreur(await renouveler(pc1, pc1.station.toUpperCase()), 400, "champ");
    });

    it("un PC sans activation : des listes vides", async () => {
        const r = await renouveler(pc());
        assert.equal(r.statut, 200);
        assert.deepEqual(r.json.activations, []);
        assert.deepEqual(r.json.retirees, []);
    });

    it("liberer : le poste est rendu (un transfert) ; ce PC n'est plus renouvele ; un autre prend la place", async () => {
        const l = await liberer(pc2, L1.id);
        assert.equal(l.statut, 200, l.texte);
        assert.equal(l.json.place, "rendue");
        assert.equal(l.json.transferts, 1);
        assert.equal(l.json.transferts_permis, 2);
        assert.match(l.json.message, /rendue/);
        const r = await renouveler(pc2);
        assert.deepEqual(r.json.activations, []);
        assert.deepEqual(r.json.retirees, [L1.id]);
        assert.deepEqual(r.json.motifs, { [L1.id]: "liberee" });
        assert.equal((await activer(pc3, C1.code)).statut, 200);
        erreur(await appel(svc, "POST", "/v1/liberer", { cle: pc2, corps: {} }), 400, "champ");
        // deux fois : sans effet (la place reste rendue)
        assert.equal((await liberer(pc2, L1.id)).json.place, "rendue");
    });

    it("retirer (l'editeur) : le PC est retire, la place rendue", async () => {
        erreur(await appel(svc, "POST", "/admin/activations/retirer", { cle: editeur, corps: { licence: L1.id, poste: nouvelleCle().hex } }), 404, "activation");
        erreur(await appel(svc, "POST", "/admin/activations/retirer", { cle: pc3, corps: { licence: L1.id, poste: pc3.hex } }), 403, "editeur");
        assert.equal((await retirer(L1.id, pc3)).statut, 200);
        const r = await renouveler(pc3);
        assert.deepEqual(r.json.retirees, [L1.id]);
        assert.deepEqual(r.json.motifs, { [L1.id]: "retiree" });
        const e = await licenceChezLeService(L1.id);
        const etats = Object.fromEntries(e.activations.map((a) => [a.poste, a.etat]));
        assert.deepEqual(etats, { [pc1.hex]: "active", [pc2.hex]: "liberee", [pc3.hex]: "retiree" });
        const a1 = e.activations.find((a) => a.poste === pc1.hex);
        assert.equal(a1.nom, "PC d'essai");
        assert.equal(a1.station, station);
        assert.equal(a1.place_jusqu_a, null);
        for (const k of ["activee", "vue", "jusqu_a"]) assert.match(a1[k], DATE_ISO);
    });

    it("une licence desactivee : le code est refuse, ses PC ne sont plus renouveles ; reactivee, ils le sont", async () => {
        assert.equal((await deposerLicence(L1, { active: false })).statut, 200);
        erreur(await activer(pc2, C1.code), 403, "code");
        let r = await renouveler(pc1);
        assert.deepEqual(r.json.retirees, [L1.id]);
        assert.deepEqual(r.json.motifs, { [L1.id]: "desactivee" });
        assert.equal((await deposerLicence(L1, { active: true })).statut, 200);
        r = await renouveler(pc1);
        assert.equal(r.json.activations.length, 1, "le code est garde quand code_hash est absent");
        assert.equal((await activer(pc2, C1.code)).statut, 200);
    });

    it("une emission plus recente : renvoyee au renouvellement ; une plus ancienne : refusee", async () => {
        const ancienne = L1.emission;
        L1.emission = now();
        const nouvelle = bloc("licence", licenceJson(L1), editeur);
        assert.equal((await appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: nouvelle } })).statut, 200);
        const r = await renouveler(pc1);
        assert.equal(r.json.activations[0].licence, nouvelle);
        assert.equal(JSON.parse(lireBloc(r.json.activations[0].activation).json).emission, iso(L1.emission));
        erreur(await deposerLicence({ ...L1, emission: ancienne }), 409, "ancien");
    });

    it("le fichier complet de l'editeur (licence + registre) : seule la licence est gardee", async () => {
        const id = "XPG-2026-00002";
        const lic = bloc("licence", licenceJson({ id, emission: now(), expire: now() + JOUR }), editeur);
        const reg = bloc("registre", JSON.stringify({ format: "xpg-registry", version: 1, number: 7, issued_at: iso(now()) }), editeur);
        const r = await appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: lic + reg } });
        assert.equal(r.statut, 200, r.texte);
        const c = nouveauCode();
        codes.push(c);
        assert.equal((await deposerLicence({ id, emission: now() + 1, expire: now() + JOUR }, { code_hash: c.hash })).statut, 200);
        const a = await activer(pc(), c.code);
        assert.equal(a.statut, 200, a.texte);
        assert.equal(lireBloc(a.json.licence).reste, "");
    });
});

describe("la station : une cle de PC recopiee sur une autre installation de Windows", () => {
    const LS = { id: "XPG-2026-S0001", max: 2 };
    const CS = nouveauCode();
    codes.push(CS);
    const FS = { id: "XPG-2026-S0002", type: "floating", max: 1 };
    const CFS = nouveauCode();
    codes.push(CFS);
    const a = pc();
    const copie = copieAilleurs(a);
    let numeroA = 0;

    it("la copie ne s'active pas a la place de l'original (403 activation) ; l'original reste", async () => {
        assert.equal((await deposerLicence({ ...LS, emission: now() - 60, expire: now() + 100 * JOUR }, { code_hash: CS.hash })).statut, 200);
        const r = await activer(a, CS.code);
        assert.equal(r.statut, 200, r.texte);
        numeroA = JSON.parse(lireBloc(r.json.activation).json).numero;
        const c = await activer(copie, CS.code);
        erreur(c, 403, "activation");
        assert.match(c.json.message, /autre poste/);
        const e = await licenceChezLeService(LS.id);
        assert.equal(e.activations.length, 1);
        assert.equal(e.activations[0].station, a.station, "la station de l'original est gardee");
    });

    it("renouveler avec une autre station : pas renouvelee (retiree pour la copie, motif autre_poste), l'original l'est", async () => {
        const avant = (await licenceChezLeService(LS.id)).activations[0];
        const r = await renouveler(copie);
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(r.json.activations, []);
        assert.deepEqual(r.json.retirees, [LS.id]);
        assert.deepEqual(r.json.motifs, { [LS.id]: "autre_poste" });
        const apres = (await licenceChezLeService(LS.id)).activations[0];
        assert.deepEqual(apres, avant, "rien n'a change chez le service");
        const o = await renouveler(a);
        assert.equal(o.json.activations.length, 1);
        assert.ok(JSON.parse(lireBloc(o.json.activations[0].activation).json).numero > numeroA);
    });

    it("liberer avec une autre station : 403, rien ne change", async () => {
        erreur(await liberer(copie, LS.id), 403, "activation");
        assert.equal((await licenceChezLeService(LS.id)).activations[0].etat, "active");
    });

    it("le jeton flottant : la station de l'activation, sinon 403 ; sans station 400 ; rendre de meme", async () => {
        assert.equal((await deposerLicence({ ...FS, emission: now() - 60, expire: now() + 100 * JOUR }, { code_hash: CFS.hash })).statut, 200);
        assert.equal((await activer(a, CFS.code)).statut, 200);
        erreur(await jeton(a, FS.id, null), 400, "champ");
        erreur(await jeton(a, FS.id, "123"), 400, "champ");
        const c = await jeton(copie, FS.id);
        erreur(c, 403, "activation");
        assert.match(c.json.message, /autre poste/);
        assert.equal((await jeton(a, FS.id)).statut, 200);
        erreur(await rendre(copie, FS.id), 403, "activation");
        erreur(await rendre(a, FS.id, null), 400, "champ");
        assert.equal((await licenceChezLeService(FS.id)).jetons.length, 1, "la copie n'a pas rendu le jeton de l'original");
        assert.equal((await rendre(a, FS.id)).statut, 200);
        assert.equal((await licenceChezLeService(FS.id)).jetons.length, 0);
    });

    it("des copies de la cle d'un PC flottant ne partagent pas son jeton", async () => {
        assert.equal((await jeton(a, FS.id)).statut, 200);
        // la copie ne peut ni prendre un jeton, ni reprendre celui de l'original
        erreur(await jeton(copieAilleurs(a), FS.id), 403, "activation");
        erreur(await jeton(copie, FS.id), 403, "activation");
        // un autre PC (sa propre activation) : le seul jeton est pris
        const b = pc();
        assert.equal((await activer(b, CFS.code)).statut, 200);
        erreur(await jeton(b, FS.id), 409, "complet");
        assert.equal((await rendre(a, FS.id)).statut, 200);
    });

    it("l'original libere sa place (un transfert) : la copie peut alors s'activer, comme un autre PC", async () => {
        assert.equal((await liberer(a, LS.id)).json.place, "rendue");
        const r = await activer(copie, CS.code);
        assert.equal(r.statut, 200, r.texte);
        assert.equal(JSON.parse(lireBloc(r.json.activation).json).station, copie.station);
        const o = await renouveler(a);
        assert.deepEqual(o.json.motifs, { [LS.id]: "autre_poste" }, "l'original n'a plus cette activation");
        assert.equal(o.json.activations.length, 1, "sa licence flottante, la sienne, est renouvelee");
    });
});

describe("les transferts : un PC libere rend sa place tout de suite, deux fois par mois (ou autant que de postes)", () => {
    const T = { id: "XPG-2026-T0100", type: "perpetual", max: 1 };
    const CT = nouveauCode();
    codes.push(CT);
    const [A, B, C, D] = [pc(), pc(), pc(), pc()];
    let finC = "";

    it("deux liberations : la place revient tout de suite", async () => {
        assert.equal((await deposerLicence({ ...T, emission: now() - 60 }, { code_hash: CT.hash })).statut, 200);
        assert.equal((await activer(A, CT.code)).statut, 200);
        let l = await liberer(A, T.id);
        assert.deepEqual([l.json.place, l.json.transferts, l.json.transferts_permis], ["rendue", 1, 2]);
        assert.equal((await activer(B, CT.code)).statut, 200);
        l = await liberer(B, T.id);
        assert.deepEqual([l.json.place, l.json.transferts, l.json.transferts_permis], ["rendue", 2, 2]);
    });

    it("la troisieme : la place reste prise jusqu'a la fin du bail du PC libere ; le refus dit quand elle revient", async () => {
        const c = await activer(C, CT.code);
        assert.equal(c.statut, 200, c.texte);
        finC = JSON.parse(lireBloc(c.json.activation).json).jusqu_a;
        const l = await liberer(C, T.id);
        assert.equal(l.statut, 200, l.texte);
        assert.equal(l.json.place, "gardee");
        assert.equal(l.json.jusqu_a, finC);
        assert.equal(l.json.transferts, 2);
        assert.match(l.json.message, /reste prise jusqu'à la fin de son bail/);
        const r = await renouveler(C);
        assert.deepEqual(r.json.motifs, { [T.id]: "liberee" }, "C n'est plus renouvele");
        const d = await activer(D, CT.code);
        erreur(d, 409, "complet");
        assert.equal(d.json.libre_le, finC);
        const jj = finC.slice(8, 10) + "/" + finC.slice(5, 7) + "/" + finC.slice(0, 4);
        assert.ok(d.json.message.includes("Une place revient le " + jj), d.json.message);
        const e = await licenceChezLeService(T.id);
        assert.equal(e.activations.find((x) => x.poste === C.hex).place_jusqu_a, finC);
        // une copie de la cle de C (une autre installation) ne reprend pas la place gardee
        erreur(await activer(copieAilleurs(C), CT.code), 409, "complet");
    });

    it("le PC libere lui-meme (la meme installation) reprend sa place", async () => {
        const c = await activer(C, CT.code);
        assert.equal(c.statut, 200, c.texte);
        const e = await licenceChezLeService(T.id);
        assert.equal(e.activations.find((x) => x.poste === C.hex).place_jusqu_a, null);
        // de nouveau libere : plus de transfert permis, la place reste prise
        assert.equal((await liberer(C, T.id)).json.place, "gardee");
    });

    it("retire par l'editeur : la place revient tout de suite (sa decision)", async () => {
        assert.equal((await retirer(T.id, C)).statut, 200);
        assert.equal((await activer(D, CT.code)).statut, 200);
        assert.equal((await licenceChezLeService(T.id)).activations.find((x) => x.poste === C.hex).place_jusqu_a, null);
    });

    it("desactivee par l'editeur : les places gardees reviennent", async () => {
        assert.equal((await liberer(D, T.id)).json.place, "gardee");
        erreur(await activer(A, CT.code), 409, "complet");
        assert.equal((await deposerLicence({ ...T, emission: now() - 60 }, { active: false })).statut, 200);
        assert.equal((await deposerLicence({ ...T, emission: now() - 60 }, { active: true })).statut, 200);
        assert.equal((await activer(A, CT.code)).statut, 200);
    });

    it("une licence de 3 postes : 3 transferts permis", async () => {
        const id = "XPG-2026-T0103";
        const c = nouveauCode();
        codes.push(c);
        assert.equal((await deposerLicence({ id, type: "perpetual", max: 3, emission: now() - 60 }, { code_hash: c.hash })).statut, 200);
        const places = [];
        for (let i = 0; i < 4; i++) {
            const p = pc();
            assert.equal((await activer(p, c.code)).statut, 200);
            places.push((await liberer(p, id)).json);
        }
        assert.deepEqual(
            places.map((x) => [x.place, x.transferts_permis]),
            [
                ["rendue", 3],
                ["rendue", 3],
                ["rendue", 3],
                ["gardee", 3],
            ],
        );
    });
});

describe("les lignes d'activation : un maximum par licence, qui ne gene pas les autres", () => {
    it("flottante (1 jeton) : 50 activations, puis 409 limite ; une autre licence s'active encore", async () => {
        const id = "XPG-2026-M0001";
        const c = nouveauCode();
        codes.push(c);
        assert.equal((await deposerLicence({ id, type: "floating", max: 1, emission: now() - 60 }, { code_hash: c.hash })).statut, 200);
        for (let i = 0; i < 50; i++) {
            const r = await activer(pc(), c.code);
            assert.equal(r.statut, 200, i + " : " + r.texte);
        }
        const r = await activer(pc(), c.code);
        erreur(r, 409, "limite");
        assert.match(r.json.message, /50 activations/);
        const autre = nouveauCode();
        codes.push(autre);
        assert.equal((await deposerLicence({ id: "XPG-2026-M0002", type: "floating", max: 1, emission: now() - 60 }, { code_hash: autre.hash })).statut, 200);
        assert.equal((await activer(pc(), autre.code)).statut, 200);
    });

    it("fixe (1 poste) : 20 lignes (10 x postes + 10), meme retirees, puis 409 limite", async () => {
        const id = "XPG-2026-M0003";
        const c = nouveauCode();
        codes.push(c);
        assert.equal((await deposerLicence({ id, type: "perpetual", max: 1, emission: now() - 60 }, { code_hash: c.hash })).statut, 200);
        for (let i = 0; i < 20; i++) {
            const p = pc();
            assert.equal((await activer(p, c.code)).statut, 200, "activation " + i);
            assert.equal((await retirer(id, p)).statut, 200);
        }
        erreur(await activer(pc(), c.code), 409, "limite");
    });
});

describe("les revocations", () => {
    const L5 = { id: "XPG-2026-00005", max: 3 };
    const C5 = nouveauCode();
    codes.push(C5);
    const a = pc();
    const b = pc();
    const sans = pc();
    const raison = "Motif prive de l'editeur";
    const revocations = (numero, revoked) =>
        bloc("revocations", JSON.stringify({ format: "xpg-revocations", version: 1, number: numero, issued_at: iso(now()), revoked }, null, 2) + "\n", editeur);

    it("un PC revoque : plus renouvele (motif revoquee) ; la liste de l'editeur ne sort jamais", async () => {
        L5.emission = now() - 10;
        L5.expire = now() + 100 * JOUR;
        assert.equal((await deposerLicence(L5, { code_hash: C5.hash })).statut, 200);
        assert.equal((await activer(a, C5.code)).statut, 200);
        assert.equal((await activer(b, C5.code)).statut, 200);
        const autreClient = nouvelleCle();
        const liste = revocations(1, [
            { license_id: L5.id, device: a.hex, revoked_at: iso(now()), reason: raison },
            { license_id: "XPG-2026-AUTRE", device: autreClient.hex, revoked_at: iso(now()), reason: raison },
        ]);
        assert.equal((await appel(svc, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: liste } })).statut, 200);
        let r = await renouveler(a);
        assert.deepEqual(r.json.retirees, [L5.id]);
        assert.deepEqual(r.json.motifs, { [L5.id]: "revoquee" });
        assert.ok(!("revocations" in r.json), "pas de liste de revocation dans la reponse");
        r = await renouveler(b);
        assert.equal(r.json.activations.length, 1);
        assert.ok(!("revocations" in r.json));
        for (const p of [a, b, pc1, sans]) {
            const x = await renouveler(p);
            assert.ok(!x.texte.includes(raison) && !x.texte.includes(autreClient.hex) && !x.texte.includes("XPG-2026-AUTRE"), "rien de la liste ne sort");
        }
        erreur(await activer(a, C5.code), 410, "revoquee");
        const e = await licenceChezLeService(L5.id);
        assert.equal(e.activations.find((x) => x.poste === a.hex).etat, "revoquee");
    });

    it("toute la licence revoquee : son code est refuse (410), une liste plus ancienne refusee", async () => {
        const liste = revocations(2, [
            { license_id: L5.id, device: a.hex, revoked_at: iso(now()), reason: "Essai" },
            { license_id: L5.id, revoked_at: iso(now()), reason: "Fin de contrat" },
        ]);
        assert.equal((await appel(svc, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: liste } })).statut, 200);
        erreur(await activer(pc(), C5.code), 410, "revoquee");
        const r = await renouveler(b);
        assert.deepEqual(r.json.retirees, [L5.id]);
        assert.deepEqual(r.json.motifs, { [L5.id]: "revoquee" });
        erreur(await appel(svc, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: revocations(1, []) } }), 409, "ancien");
        erreur(await appel(svc, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: "x" } }), 400, "document");
    });

    it("issued_before : une emission plus recente n'est plus revoquee", async () => {
        const id = "XPG-2026-00006";
        const c = nouveauCode();
        codes.push(c);
        const t = now();
        assert.equal((await deposerLicence({ id, max: 1, emission: t - 100, expire: t + JOUR }, { code_hash: c.hash })).statut, 200);
        const liste = revocations(3, [
            { license_id: L5.id, revoked_at: iso(t), reason: "Fin de contrat" },
            { license_id: id, revoked_at: iso(t), reason: "Remplacee", issued_before: iso(t - 50) },
        ]);
        assert.equal((await appel(svc, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: liste } })).statut, 200);
        erreur(await activer(pc(), c.code), 410, "revoquee");
        assert.equal((await deposerLicence({ id, max: 1, emission: t, expire: t + JOUR })).statut, 200);
        assert.equal((await activer(pc(), c.code)).statut, 200);
    });
});

describe("une licence expiree", () => {
    it("410 expiree", async () => {
        const c = nouveauCode();
        codes.push(c);
        const t = now();
        assert.equal((await deposerLicence({ id: "XPG-2020-00001", emission: t - 400 * JOUR, expire: t - 30 * JOUR }, { code_hash: c.hash })).statut, 200);
        erreur(await activer(pc(), c.code), 410, "expiree");
    });
});

describe("les licences flottantes", () => {
    const F = { id: "XPG-2026-F0001", type: "floating", max: 1 };
    const CF = nouveauCode();
    codes.push(CF);
    const p5 = pc();
    const p6 = pc();
    it("l'activation flottante (plus de PC que de jetons)", async () => {
        F.emission = now();
        F.expire = now() + 100 * JOUR;
        assert.equal((await deposerLicence(F, { code_hash: CF.hash })).statut, 200);
        for (const p of [p5, p6]) {
            const r = await activer(p, CF.code);
            assert.equal(r.statut, 200, r.texte);
            assert.equal(JSON.parse(lireBloc(r.json.activation).json).type, "flottante");
        }
    });

    it("le jeton (15 min), complet avec libre_dans, rendu puis repris", async () => {
        const r = await jeton(p5, F.id);
        assert.equal(r.statut, 200, r.texte);
        assert.ok(blocSignePar(r.json.jeton, cleService));
        const b = lireBloc(r.json.jeton);
        assert.equal(b.genre, "jeton");
        const d = JSON.parse(b.json);
        assert.deepEqual(Object.keys(d), ["version", "licence", "poste", "emis", "jusqu_a"]);
        assert.equal(b.json, JSON.stringify(d));
        assert.equal(d.licence, F.id);
        assert.equal(d.poste, p5.hex);
        assert.equal(Date.parse(d.jusqu_a) - Date.parse(d.emis), 900 * 1000);
        // le meme PC reprend son jeton
        assert.equal((await jeton(p5, F.id)).statut, 200);
        const c = await jeton(p6, F.id);
        erreur(c, 409, "complet");
        assert.ok(c.json.libre_dans > 800 && c.json.libre_dans <= 900, c.texte);
        assert.deepEqual(
            (await licenceChezLeService(F.id)).jetons.map((j) => j.poste),
            [p5.hex],
        );
        assert.equal((await rendre(p5, F.id)).statut, 200);
        assert.equal((await jeton(p6, F.id)).statut, 200);
    });

    it("sans activation flottante : 403", async () => {
        erreur(await jeton(pc(), F.id), 403, "activation");
        erreur(await jeton(pc1, L1.id), 403, "activation");
        erreur(await jeton(p5, "XPG-INCONNUE"), 403, "activation");
        erreur(await appel(svc, "POST", "/v1/flottante/jeton", { cle: p5, corps: {} }), 400, "champ");
    });

    it("liberer rend aussi le jeton (une flottante n'a pas de place a garder)", async () => {
        const l = await liberer(p6, F.id);
        assert.equal(l.statut, 200);
        assert.equal(l.json.place, "rendue");
        assert.equal(l.json.transferts, undefined, "pas de transfert compte pour une flottante");
        assert.equal((await jeton(p5, F.id)).statut, 200);
    });
});

describe("la Decouverte", () => {
    it("le premier debut de la machine, garde ; jours = DECOUVERTE_JOURS", async () => {
        const machine = hasard(16);
        const p = pc();
        const r = await appel(svc, "POST", "/v1/decouverte", { cle: p, corps: { machine } });
        assert.equal(r.statut, 200, r.texte);
        assert.ok(r.type.startsWith("text/plain"));
        assert.ok(blocSignePar(r.texte, cleService));
        const b = lireBloc(r.texte);
        assert.equal(b.genre, "decouverte");
        const d = JSON.parse(b.json);
        assert.equal(b.json, JSON.stringify({ version: 1, poste: p.hex, machine, debut: d.debut, jours: 30 }));
        assert.ok(Math.abs(Date.parse(d.debut) / 1000 - now()) < 30);
        await new Promise((ok) => setTimeout(ok, 1100));
        const autre = pc();
        const r2 = await appel(svc, "POST", "/v1/decouverte", { cle: autre, corps: { machine } });
        const d2 = JSON.parse(lireBloc(r2.texte).json);
        assert.equal(d2.debut, d.debut, "la machine garde son premier debut");
        assert.equal(d2.poste, autre.hex);
    });

    it("une machine mal ecrite : 400", async () => {
        for (const machine of [undefined, "", hasard(15), hasard(16).toUpperCase(), 12])
            erreur(await appel(svc, "POST", "/v1/decouverte", { cle: pc(), corps: { machine } }), 400, "champ");
    });
});

describe("les demandes manuelles", () => {
    const p8 = pc();
    let n1;
    it("deposer : 201 et un numero ; une seule en attente par PC", async () => {
        const r = await appel(svc, "POST", "/v1/demandes", { cle: p8, corps: { cle: editeur.id, scelle: Buffer.from("scelle 1").toString("base64") } });
        assert.equal(r.statut, 201, r.texte);
        n1 = r.json.numero;
        assert.ok(Number.isInteger(n1) && n1 >= 1);
        const r2 = await appel(svc, "POST", "/v1/demandes", { cle: p8, corps: { cle: editeur2.id, scelle: "c2NlbGxl\nIDI=" } });
        assert.equal(r2.statut, 201);
        assert.equal(r2.json.numero, n1 + 1);
        const l = await appel(svc, "GET", "/admin/demandes?depuis=0", { cle: editeur });
        assert.equal(l.statut, 200);
        const mine = l.json.demandes.filter((d) => d.poste === p8.poste);
        assert.deepEqual(
            mine.map((d) => [d.numero, d.etat]),
            [
                [n1, "remplacee"],
                [n1 + 1, "attente"],
            ],
        );
        assert.deepEqual(Object.keys(mine[0]), ["numero", "poste", "cle", "scelle", "etat", "cree", "maj"]);
        assert.equal(mine[1].scelle, "c2NlbGxl\nIDI=");
        assert.equal(mine[1].cle, editeur2.id);
        const depuis = await appel(svc, "GET", "/admin/demandes?depuis=" + n1, { cle: editeur });
        assert.deepEqual(
            depuis.json.demandes.map((d) => d.numero),
            [n1 + 1],
        );
    });

    it("refus : une cle inconnue de l'editeur, un sceau illisible", async () => {
        erreur(await appel(svc, "POST", "/v1/demandes", { cle: pc(), corps: { cle: intrus.id, scelle: "AAAA" } }), 400, "cle");
        erreur(await appel(svc, "POST", "/v1/demandes", { cle: pc(), corps: { cle: editeur.id, scelle: "<pas du base64>" } }), 400, "champ");
        erreur(await appel(svc, "POST", "/v1/demandes", { cle: pc(), corps: { cle: editeur.id, scelle: "A".repeat(64 * 1024 + 4) } }), 400, "champ");
        erreur(await appel(svc, "GET", "/admin/demandes?depuis=x", { cle: editeur }), 400, "champ");
    });

    it("annuler (le PC), traiter (l'editeur)", async () => {
        erreur(await appel(svc, "POST", "/v1/demandes/annuler", { cle: pc(), corps: { numero: n1 + 1 } }), 404, "demande");
        assert.equal((await appel(svc, "POST", "/v1/demandes/annuler", { cle: p8, corps: { numero: n1 + 1 } })).statut, 200);
        assert.equal((await appel(svc, "POST", "/v1/demandes/annuler", { cle: p8, corps: { numero: n1 + 1 } })).statut, 200, "deux fois : sans effet");
        erreur(await appel(svc, "POST", "/admin/demandes/traiter", { cle: editeur, corps: { numero: n1 + 1 } }), 409, "etat");
        const r = await appel(svc, "POST", "/v1/demandes", { cle: p8, corps: { cle: editeur.id, scelle: "AAAA" } });
        assert.equal((await appel(svc, "POST", "/admin/demandes/traiter", { cle: editeur, corps: { numero: r.json.numero } })).statut, 200);
        erreur(await appel(svc, "POST", "/v1/demandes/annuler", { cle: p8, corps: { numero: r.json.numero } }), 409, "etat");
        erreur(await appel(svc, "POST", "/admin/demandes/traiter", { cle: editeur, corps: { numero: 999999 } }), 404, "demande");
        const l = await appel(svc, "GET", "/admin/demandes", { cle: editeur });
        assert.equal(l.json.demandes.find((d) => d.numero === r.json.numero).etat, "traitee");
    });
});

describe("l'etat d'un PC (bloc poste)", () => {
    const p9 = pc();
    const poste = (numero, id = p9.poste, cle = editeur) =>
        bloc("poste", JSON.stringify({ version: 1, poste: id, numero, emis: iso(now()), etat: "acceptee", scelle: "AAAA" }, null, 2) + "\n", cle);
    it("depose par l'editeur, lu par le PC (et au renouvellement)", async () => {
        let r = await appel(svc, "GET", "/v1/poste", { cle: p9 });
        assert.deepEqual(r.json, { poste: null });
        const e1 = poste(1);
        assert.equal((await appel(svc, "PUT", "/admin/postes", { cle: editeur, corps: { poste: p9.poste, etat: e1 } })).statut, 200);
        r = await appel(svc, "GET", "/v1/poste", { cle: p9 });
        assert.equal(r.json.poste, e1);
        r = await renouveler(p9);
        assert.equal(r.json.poste, e1);
        r = await appel(svc, "GET", "/v1/poste", { cle: pc() });
        assert.equal(r.json.poste, null, "un autre PC ne le lit pas");
    });

    it("refus : plus ancien, un autre poste, un autre signataire", async () => {
        assert.equal((await appel(svc, "PUT", "/admin/postes", { cle: editeur, corps: { poste: p9.poste, etat: poste(2) } })).statut, 200);
        erreur(await appel(svc, "PUT", "/admin/postes", { cle: editeur, corps: { poste: p9.poste, etat: poste(1) } }), 409, "ancien");
        erreur(await appel(svc, "PUT", "/admin/postes", { cle: editeur, corps: { poste: p9.poste, etat: poste(3, hasard(16)) } }), 400, "poste");
        erreur(await appel(svc, "PUT", "/admin/postes", { cle: editeur, corps: { poste: p9.poste, etat: poste(3, p9.poste, intrus) } }), 400, "signature");
        erreur(await appel(svc, "PUT", "/admin/postes", { cle: editeur, corps: { poste: "123", etat: poste(3) } }), 400, "champ");
    });
});

describe("l'archive de l'editeur", () => {
    it("vide, ecrite, relue, conflit, effacement", async () => {
        let r = await appel(svc, "GET", "/admin/archive", { cle: editeur });
        assert.deepEqual(r.json, { version: 0, fichiers: [] });
        const a = "contenu chiffré A";
        r = await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 0, fichiers: { "a.txt": a, "dossier/b.txt": "B" } } });
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(r.json, { version: 1 });
        r = await appel(svc, "GET", "/admin/archive", { cle: editeur });
        assert.deepEqual(r.json, {
            version: 1,
            fichiers: [
                { chemin: "a.txt", sha256: sha256Hex(Buffer.from(a)), taille: Buffer.byteLength(a) },
                { chemin: "dossier/b.txt", sha256: sha256Hex("B"), taille: 1 },
            ],
        });
        r = await appel(svc, "GET", "/admin/archive/fichier?chemin=" + encodeURIComponent("dossier/b.txt"), { cle: editeur });
        assert.equal(r.statut, 200);
        assert.equal(r.texte, "B");
        r = await appel(svc, "GET", "/admin/archive/fichier?chemin=a.txt", { cle: editeur });
        assert.equal(r.texte, a);
        const conflit = await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 0, fichiers: { "a.txt": "X" } } });
        erreur(conflit, 409, "conflit");
        assert.equal(conflit.json.version, 1);
        r = await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 1, fichiers: { "a.txt": null } } });
        assert.deepEqual(r.json, { version: 2 });
        r = await appel(svc, "GET", "/admin/archive", { cle: editeur });
        assert.deepEqual(
            r.json.fichiers.map((f) => f.chemin),
            ["dossier/b.txt"],
        );
        erreur(await appel(svc, "GET", "/admin/archive/fichier?chemin=a.txt", { cle: editeur }), 404, "fichier");
    });

    it("refus : chemins, taille d'un fichier, corps trop gros, pas l'editeur", async () => {
        for (const c of ["../x", "/abs", "a//b", "a/./b", "a\\b", "x/"])
            erreur(await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 2, fichiers: { [c]: "x" } } }), 400, "chemin");
        erreur(await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 2, fichiers: { "gros.txt": "x".repeat(1.5 * 1024 * 1024 + 1) } } }), 413, "taille");
        // 1,5 Mio pile : accepte
        const r = await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 2, fichiers: { "pile.txt": "y".repeat(1.5 * 1024 * 1024) } } });
        assert.equal(r.statut, 200, r.texte);
        erreur(await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 3, fichiers: { "trop.txt": "z".repeat(2 * 1024 * 1024) } } }), 413, "taille");
        erreur(await appel(svc, "GET", "/admin/archive", { cle: intrus }), 403, "editeur");
        erreur(await appel(svc, "POST", "/admin/archive", { cle: editeur, corps: { base: 3, fichiers: [] } }), 400, "champ");
    });
});

describe("les limites", () => {
    it("un corps de plus de 2 Mio : 413", async () => {
        erreur(await appel(svc, "POST", "/v1/renouveler", { cle: pc(), corps: '{"x":"' + "a".repeat(2 * 1024 * 1024) + '"}' }), 413, "taille");
    });

    it("devant le service (le limiteur de Cloudflare) : 60 appels publics par minute et par adresse, puis 429 ; une autre adresse passe", async () => {
        // les fenetres du limiteur suivent l'horloge (une minute pile) : commencer au debut d'une minute
        const s = (Date.now() / 1000) % 60;
        if (s > 45) await new Promise((ok) => setTimeout(ok, (61 - s) * 1000));
        const ip = "203.0.113.7";
        for (let i = 0; i < 60; i++) {
            const chemin = i % 3 === 0 ? "/v1/etat" : "/v1/heure?n=" + hasard(16);
            assert.equal((await appel(svc, "GET", chemin, { ip })).statut, 200, "appel " + i);
        }
        const r = await appel(svc, "GET", "/v1/heure?n=" + hasard(16), { ip });
        erreur(r, 429, "trop");
        assert.equal(r.json.reessayer_dans, 60);
        assert.equal(r.entetes.get("retry-after"), "60");
        erreur(await appel(svc, "GET", "/v1/certificat", { ip }), 429, "trop");
        assert.equal((await appel(svc, "GET", "/v1/heure?n=" + hasard(16), { ip: "203.0.113.8" })).statut, 200);
        // les routes signees ont leur propre compte (120 par minute) : cette adresse renouvelle encore
        assert.equal((await appel(svc, "POST", "/v1/renouveler", { cle: pc1, ip, corps: { station: pc1.station } })).statut, 200);
    });

    it("par adresse : 10 activations par heure", async () => {
        const ip = "203.0.113.9";
        for (let i = 0; i < 10; i++) erreur(await activer(pc(), nouveauCode().code, { ip }), 403, "code");
        erreur(await activer(pc(), C1.code, { ip }), 429, "trop");
    });

    it("par PC : 5 activations par heure, 30 renouvellements par heure, 5 demandes par jour", async () => {
        const p = pc();
        for (let i = 0; i < 5; i++) erreur(await activer(p, nouveauCode().code), 403, "code");
        erreur(await activer(p, C1.code), 429, "trop");
        const q = pc();
        for (let i = 0; i < 30; i++) assert.equal((await renouveler(q)).statut, 200);
        erreur(await renouveler(q), 429, "trop");
        const d = pc();
        for (let i = 0; i < 5; i++) assert.equal((await appel(svc, "POST", "/v1/demandes", { cle: d, corps: { cle: editeur.id, scelle: "AAAA" } })).statut, 201);
        const r = await appel(svc, "POST", "/v1/demandes", { cle: d, corps: { cle: editeur.id, scelle: "AAAA" } });
        erreur(r, 429, "trop");
        assert.ok(r.json.reessayer_dans <= 86400);
    });
});

describe("les lignes ecrites par appel (le plan gratuit : 100 000 par jour)", () => {
    const W = { id: "XPG-2026-W0001", type: "floating", max: 5 };
    const CW = nouveauCode();
    codes.push(CW);
    const p = pc();
    it("public : rien (etat, heure, certificat, registre)", async () => {
        for (const chemin of ["/v1/etat", "/v1/heure?n=" + hasard(16), "/v1/certificat", "/v1/registre"]) {
            const r = await appel(svc, "GET", chemin);
            assert.equal(r.statut, 200, chemin);
            assert.equal(lignesEcrites(r), 0, chemin);
        }
    });

    it("renouveler : 3 lignes (nonce, bail, numero) ; le jeton : 2 (nonce, jeton) ; un refus de signature : 0", async () => {
        assert.equal((await deposerLicence({ ...W, emission: now() - 60 }, { code_hash: CW.hash })).statut, 200);
        const a = await activer(p, CW.code);
        assert.equal(a.statut, 200);
        assert.ok(lignesEcrites(a) <= 9, "activer : " + lignesEcrites(a));
        const r = await renouveler(p);
        assert.equal(r.json.activations.length, 1);
        assert.equal(lignesEcrites(r), 3);
        for (let i = 0; i < 2; i++) {
            const j = await jeton(p, W.id);
            assert.equal(j.statut, 200);
            assert.equal(lignesEcrites(j), 2, "jeton " + i);
        }
        assert.equal(lignesEcrites(await appel(svc, "POST", "/v1/renouveler", { cle: p, corps: { station: p.station }, signature: "ab".repeat(64) })), 0);
        assert.equal(lignesEcrites(await appel(svc, "POST", "/v1/flottante/jeton", { cle: p, corps: { licence: W.id, station: p.station }, date: now() - 3600 })), 0);
    });

    it("un appel refuse par une limite : 0 ligne", async () => {
        const q = pc();
        for (let i = 0; i < 30; i++) await renouveler(q);
        const r = await renouveler(q);
        erreur(r, 429, "trop");
        assert.equal(lignesEcrites(r), 0);
    });
});

describe("les compteurs et les secrets", () => {
    it("GET /admin/stats", async () => {
        const r = await appel(svc, "GET", "/admin/stats", { cle: editeur });
        assert.equal(r.statut, 200, r.texte);
        assert.ok(r.json.licences.total >= 6);
        assert.ok(r.json.activations.active >= 1);
        assert.ok(r.json.activations.revoquee >= 1);
        assert.ok(r.json.demandes.traitee >= 1);
        assert.ok(r.json.decouvertes >= 1);
        assert.equal(r.json.appels_jour.jour, iso(now()).slice(0, 10));
        assert.ok(r.json.appels_jour.total > 100);
        assert.ok(r.json.appels_jour.routes["GET /v1/heure"] >= 40);
        assert.ok(r.json.appels_jour.routes["POST /v1/renouveler"] >= 30);
        // le compteur des lignes d'activation (tenu a chaque ecriture) : le meme nombre que la table
        const parEtat = ["active", "liberee", "retiree", "revoquee"].reduce((n, k) => n + r.json.activations[k], 0);
        assert.equal(r.json.activations.lignes, parEtat);
        assert.ok(r.json.demandes.octets > 0);
    });

    it("aucune reponse ne contient un code, son empreinte, ni la cle privee du service", async () => {
        assert.ok(reponses.length > 150);
        for (const c of codes)
            for (const r of reponses) {
                assert.ok(!r.includes(c.norm) && !r.includes(c.code) && !r.includes(c.hash), "un code ou son empreinte est sorti");
            }
        for (const r of reponses) assert.ok(!/pkcs8|cle_privee|302e020100300506032b657004/i.test(r), "la cle privee est sortie");
    });
});
