// =============================================================================
//  service/test/admins.test.js - 1.44.0 : plusieurs admins, le journal, les
//  fichiers, les messages a un PC, les versions (docs/ADMINS.md), essayes dans
//  le VRAI runtime (workerd, par outils/service/lancer-local.sh), par HTTP
// =============================================================================
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, hasard, iso, lancerService, licenceJson, nouvelleCle, sha256Hex, verifierSignature } from "./aides.js";

const now = () => Math.floor(Date.now() / 1000);

const editeur = nouvelleCle();
const lea = nouvelleCle(); // licence_creer, demandes
const marc = nouvelleCle(); // offres (puis retire)
const anne = nouvelleCle(); // annonces, licence_prolonger, licence_revoquer, poste_retirer
const intrus = nouvelleCle();
const zoe = nouvelleCle(); // demande a devenir admin (acceptee : << demandes >>)
const yan = nouvelleCle(); // demande a devenir admin (refusee)
const EMIS = now(); // les documents ecrits deux fois (manifestes, messages) : le meme texte
const posteDe = () => hasard(16); // l'identifiant d'une machine (systemMachineId)
const machines = new Map([lea, marc, anne].map((k) => [k.hex, posteDe()]));
let svc;
let cleService = "";
let numeroListe = 0;

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
    assert.ok(typeof r.json.message === "string" && r.json.message.length > 5);
}

// Une fiche de la liste des admins (§ 3).
function fiche(cle, nom, droits, { retire = 0, gardes = [] } = {}) {
    return {
        id: cle.id,
        cle: cle.hex,
        nom,
        pc: "PC-" + nom.toUpperCase(),
        poste: machines.get(cle.hex) || "",
        depuis: now() - 100,
        profil: "Admin LV1",
        droits,
        retire,
        gardes,
    };
}
const listeJson = (numero, admins) => JSON.stringify({ format: 1, numero, emise: now(), admins });
async function publier(admins, { numero, signataire = editeur, appelant = editeur } = {}) {
    const n = numero || ++numeroListe;
    return appel(svc, "PUT", "/admin/admins", { cle: appelant, corps: { admins: bloc("admins", listeJson(n, admins), signataire) } });
}
const liste = (extra = []) => [fiche(lea, "Léa", ["licence_creer", "demandes"]), fiche(marc, "Marc", ["offres"]), fiche(anne, "Anne", ["annonces", "licence_prolonger", "licence_revoquer", "poste_retirer"]), ...extra];

// Une entree du journal, signee par `cle` (§ 6).
function entree(cle, action, extra = {}) {
    const texte = JSON.stringify({ quand: now(), action, objet: "XPG-2026-00412", resume: "a signé la licence", detail: { client: "Atelier d'essai" }, ...extra, nonce: extra.nonce || hasard(16) });
    return { entree: texte, signature: cle.signer(Buffer.from("XPG-JOURNAL-1\n" + texte)).toString("hex") };
}

const registreJson = (numero, parts) =>
    JSON.stringify({ format: "xpg-registry", version: 1, number: numero, issued_at: iso(now()), unlicensed_edition: "FREE", features: [], editions: [], ...parts }, null, 2) + "\n";
let numeroRegistre = 0;

before(async () => {
    svc = await lancerService({ cles: [editeur.hex], sansLimites: true });
    cleService = (await appel(svc, "GET", "/v1/etat")).json.cle_service;
});

after(async () => {
    if (svc) {
        await svc.arreter();
        fs.rmSync(svc.etat, { recursive: true, force: true });
    }
});

describe("la liste des admins (genre admins, signee par la cle de l'editeur)", () => {
    it("avant toute liste : GET /v1/admins 404, l'etat dit 0", async () => {
        erreur(await appel(svc, "GET", "/v1/admins"), 404, "admins");
        const e = (await appel(svc, "GET", "/v1/etat")).json;
        assert.equal(e.admins, 0);
        assert.equal(e.version_publiee, "");
        assert.equal(e.version, "1.50.0", "`version` reste celle du service");
    });

    it("publier : 200 ; GET /v1/admins rend le bloc tel quel ; l'etat dit son numero", async () => {
        const texte = bloc("admins", listeJson(2, liste()), editeur);
        numeroListe = 2;
        const r = await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: texte } });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.admins, 2);
        const p = await appel(svc, "GET", "/v1/admins");
        assert.equal(p.statut, 200);
        assert.equal(p.texte, texte);
        assert.equal((await appel(svc, "GET", "/v1/etat")).json.admins, 2);
        // le meme texte de nouveau : 200, rien ne change
        assert.equal((await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: texte } })).statut, 200);
    });

    it("anti-retour : un numero plus petit, ou le meme avec un autre texte : 409", async () => {
        erreur(await publier(liste(), { numero: 1 }), 409, "ancien");
        const r = await publier(liste().slice(0, 1), { numero: 2 });
        erreur(r, 409, "ancien");
        assert.equal(r.json.numero, 2);
    });

    it("une liste mal faite : 400 (identifiant, droit inconnu, retire avec des droits, trop d'admins, empreinte gardee)", async () => {
        const mauvais = [
            [{ ...fiche(lea, "Léa", []), id: marc.id }],
            [fiche(lea, "Léa", ["tout_faire"])],
            [fiche(lea, "Léa", ["demandes", "demandes"])],
            [fiche(lea, "Léa", ["demandes"], { retire: now() })],
            [fiche(lea, "Léa", [], { gardes: ["xyz"] })],
            [fiche(lea, "Léa", []), fiche(lea, "Léa 2", [])],
            Array.from({ length: 33 }, (_, i) => fiche(nouvelleCle(), "Admin " + i, [])),
        ];
        for (const admins of mauvais) erreur(await publier(admins, { numero: 50 }), 400, "document");
        erreur(await publier(liste(), { numero: 50, signataire: lea }), 400, "signature");
    });

    it("seul l'admin+ la publie : un admin 403 droit, une cle inconnue 403 editeur", async () => {
        erreur(await publier(liste(), { numero: 60, appelant: lea }), 403, "droit");
        erreur(await publier(liste(), { numero: 60, appelant: intrus }), 403, "editeur");
    });

    it("GET /admin/admins (un admin aussi) : nom, id, PC, profil, droits, retire", async () => {
        const r = await appel(svc, "GET", "/admin/admins", { cle: lea });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.numero, 2);
        assert.deepEqual(
            r.json.admins.map((a) => [a.nom, a.id, a.cle, a.droits, a.retire]).sort(),
            [
                ["Anne", anne.id, anne.hex, ["annonces", "licence_prolonger", "licence_revoquer", "poste_retirer"], 0],
                ["Léa", lea.id, lea.hex, ["licence_creer", "demandes"], 0],
                ["Marc", marc.id, marc.hex, ["offres"], 0],
            ].sort(),
        );
        const l = r.json.admins.find((a) => a.cle === lea.hex);
        assert.equal(l.pc, "PC-LÉA");
        assert.equal(l.poste, machines.get(lea.hex));
        assert.equal(l.profil, "Admin LV1");
        assert.equal(l.derniere, 0);
        assert.deepEqual(r.json.editeurs.map((e) => e.cle), [editeur.hex]);
        erreur(await appel(svc, "GET", "/admin/admins", { cle: intrus }), 403, "editeur");
    });
});

describe("les routes d'un admin, selon ses droits (§ 6)", () => {
    const L = "XPG-2026-A0001";
    const licence = (signataire, emission = now() - 60) => bloc("licence", licenceJson({ id: L, emission, expire: now() + 100 * 86400 }), signataire);

    it("GET /admin/etat : le role et les droits de qui appelle", async () => {
        const a = await appel(svc, "GET", "/admin/etat", { cle: lea });
        assert.equal(a.statut, 200, a.texte);
        assert.equal(a.json.role, "admin");
        assert.deepEqual(a.json.droits, ["licence_creer", "demandes"]);
        assert.equal(a.json.admins, 2);
        const p = await appel(svc, "GET", "/admin/etat", { cle: editeur });
        assert.equal(p.json.role, "admin+");
        assert.equal(p.json.droits.length, 13);
        // les routes sans droit : ouvertes a tout admin
        for (const chemin of ["/admin/licences", "/admin/stats", "/admin/archive", "/admin/cle-service"]) assert.equal((await appel(svc, "GET", chemin, { cle: marc })).statut, 200, chemin);
    });

    it("une licence nouvelle : licence_creer (Lea oui, Anne non) ; signee par la cle qui appelle", async () => {
        erreur(await appel(svc, "PUT", "/admin/licences", { cle: anne, corps: { licence: licence(anne) } }), 403, "droit");
        erreur(await appel(svc, "PUT", "/admin/licences", { cle: marc, corps: { licence: licence(marc) } }), 403, "droit");
        erreur(await appel(svc, "PUT", "/admin/licences", { cle: lea, corps: { licence: licence(editeur) } }), 400, "signature");
        const r = await appel(svc, "PUT", "/admin/licences", { cle: lea, corps: { licence: licence(lea) } });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.licence, L);
    });

    it("une licence deja la : l'un de licence_creer, licence_prolonger, licence_option (Anne prolonge)", async () => {
        const r = await appel(svc, "PUT", "/admin/licences", { cle: anne, corps: { licence: licence(anne, now() - 10) } });
        assert.equal(r.statut, 200, r.texte);
        erreur(await appel(svc, "PUT", "/admin/licences", { cle: marc, corps: { licence: licence(marc, now()) } }), 403, "droit");
        // l'admin+ : comme avant (sa cle)
        assert.equal((await appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: licence(editeur, now()) } })).statut, 200);
    });

    it("le registre : le droit de chaque partie qui change (editions : offres ; announcements : annonces ; une autre partie : offres)", async () => {
        const offres = { editions: [{ id: "PRO", label: "Pro" }] };
        // Marc (offres) : les editions
        let r = await appel(svc, "PUT", "/admin/registre", { cle: marc, corps: { registre: bloc("registre", registreJson(++numeroRegistre, offres), marc) } });
        assert.equal(r.statut, 200, r.texte);
        // Marc : une annonce, non
        r = await appel(svc, "PUT", "/admin/registre", {
            cle: marc,
            corps: { registre: bloc("registre", registreJson(numeroRegistre + 1, { ...offres, announcements: [{ id: "a1", title: "Nouveau", text: "…" }] }), marc) },
        });
        erreur(r, 403, "droit");
        assert.deepEqual(r.json.droits, ["annonces"]);
        // Anne (annonces) : l'annonce, oui
        r = await appel(svc, "PUT", "/admin/registre", {
            cle: anne,
            corps: { registre: bloc("registre", registreJson(++numeroRegistre, { ...offres, announcements: [{ id: "a1", title: "Nouveau", text: "…" }] }), anne) },
        });
        assert.equal(r.statut, 200, r.texte);
        // Anne : les editions et une option a la fois, non (il faut les deux droits)
        r = await appel(svc, "PUT", "/admin/registre", {
            cle: anne,
            corps: { registre: bloc("registre", registreJson(numeroRegistre + 1, { editions: [], options: [{ id: "o1" }] }), anne) },
        });
        erreur(r, 403, "droit");
        // Marc : une autre partie (les fonctions) demande << offres >>
        r = await appel(svc, "PUT", "/admin/registre", {
            cle: marc,
            corps: { registre: bloc("registre", registreJson(++numeroRegistre, { ...offres, announcements: [{ id: "a1", title: "Nouveau", text: "…" }], features: [{ id: "f1" }] }), marc) },
        });
        assert.equal(r.statut, 200, r.texte);
        // Lea n'a aucun droit du registre
        erreur(await appel(svc, "PUT", "/admin/registre", { cle: lea, corps: { registre: bloc("registre", registreJson(numeroRegistre + 1, offres), lea) } }), 403, "droit");
    });

    it("revocations : licence_revoquer ; retirer un PC : poste_retirer ; les demandes : demandes ; un poste : demandes ou un droit de licence", async () => {
        const revs = (cle) => bloc("revocations", JSON.stringify({ format: "xpg-revocations", version: 1, number: 1, issued_at: iso(now()), revoked: [] }), cle);
        erreur(await appel(svc, "PUT", "/admin/revocations", { cle: lea, corps: { revocations: revs(lea) } }), 403, "droit");
        assert.equal((await appel(svc, "PUT", "/admin/revocations", { cle: anne, corps: { revocations: revs(anne) } })).statut, 200);
        erreur(await appel(svc, "POST", "/admin/activations/retirer", { cle: lea, corps: { licence: L, poste: intrus.hex } }), 403, "droit");
        erreur(await appel(svc, "POST", "/admin/activations/retirer", { cle: anne, corps: { licence: L, poste: intrus.hex } }), 404, "activation");
        erreur(await appel(svc, "GET", "/admin/demandes", { cle: marc }), 403, "droit");
        assert.equal((await appel(svc, "GET", "/admin/demandes", { cle: lea })).statut, 200);
        erreur(await appel(svc, "POST", "/admin/demandes/traiter", { cle: anne, corps: { numero: 1 } }), 403, "droit");
        const poste = (cle) => bloc("poste", JSON.stringify({ version: 1, poste: "ab".repeat(16), numero: 1, emis: iso(now()) }), cle);
        erreur(await appel(svc, "PUT", "/admin/postes", { cle: marc, corps: { poste: "ab".repeat(16), etat: poste(marc) } }), 403, "droit");
        assert.equal((await appel(svc, "PUT", "/admin/postes", { cle: lea, corps: { poste: "ab".repeat(16), etat: poste(lea) } })).statut, 200);
    });

    it("les routes de l'admin+ seul : 403 droit pour un admin", async () => {
        for (const [m, chemin, corps] of [
            ["GET", "/admin/journal"],
            ["GET", "/admin/admins/demandes"],
            ["PUT", "/admin/certificat", { certificat: "x" }],
            ["PUT", "/admin/trousseau", { cle: lea.hex, scelle: "AAAA" }],
            ["GET", "/admin/versions"],
            ["GET", "/admin/sauvegarde"],
        ])
            erreur(await appel(svc, m, chemin, { cle: anne, corps }), 403, "droit");
    });
});

describe("le trousseau (la cle de synchro scellee pour chaque admin)", () => {
    it("deposer (admin+) puis lire (l'admin lui-meme)", async () => {
        assert.equal((await appel(svc, "PUT", "/admin/trousseau", { cle: editeur, corps: { cle: lea.hex, scelle: "c2NlbGzDqQ==" } })).statut, 200);
        assert.equal((await appel(svc, "PUT", "/admin/trousseau", { cle: editeur, corps: { cle: marc.hex, scelle: "bWFyYw==" } })).statut, 200);
        const r = await appel(svc, "GET", "/admin/trousseau", { cle: lea });
        assert.equal(r.statut, 200);
        assert.deepEqual(r.json, { scelle: "c2NlbGzDqQ==" });
        erreur(await appel(svc, "GET", "/admin/trousseau", { cle: anne }), 404, "trousseau");
        erreur(await appel(svc, "PUT", "/admin/trousseau", { cle: editeur, corps: { cle: intrus.hex, scelle: "AAAA" } }), 409, "admin");
        erreur(await appel(svc, "PUT", "/admin/trousseau", { cle: editeur, corps: { cle: lea.hex, scelle: "<pas du base64>" } }), 400, "champ");
        erreur(await appel(svc, "PUT", "/admin/trousseau", { cle: editeur, corps: { cle: lea.hex, scelle: "A".repeat(4100) } }), 400, "champ");
    });
});

describe("un admin retire (§ 3, § 6)", () => {
    it("sa cle : 403 retire, et une ligne << signature_refusee >> au journal, signee par le service ; son scelle est efface", async () => {
        const r = await publier(liste().map((a) => (a.cle === marc.hex ? { ...a, droits: [], retire: now() } : a)));
        assert.equal(r.statut, 200, r.texte);
        erreur(await appel(svc, "GET", "/admin/etat", { cle: marc }), 403, "retire");
        erreur(await appel(svc, "GET", "/admin/licences", { cle: marc }), 403, "retire");
        const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: editeur });
        assert.equal(j.statut, 200);
        const lignes = j.json.lignes.map((t) => JSON.parse(t));
        const refus = lignes.filter((l) => JSON.parse(l.entree).action === "signature_refusee");
        assert.equal(refus.length, 1, "une ligne par cle et par heure au plus");
        const e = JSON.parse(refus[0].entree);
        assert.deepEqual(Object.keys(e), ["quand", "action", "objet", "resume", "detail", "nonce"]);
        assert.equal(e.objet, marc.id);
        assert.equal(e.detail.cle, marc.hex);
        assert.equal(refus[0].cle, cleService);
        assert.ok(verifierSignature(cleService, "XPG-JOURNAL-1\n" + refus[0].entree, Buffer.from(refus[0].signature, "hex")));
        // un admin retire puis remis : son scelle d'avant est parti
        assert.equal((await publier(liste())).statut, 200);
        erreur(await appel(svc, "GET", "/admin/trousseau", { cle: marc }), 404, "trousseau");
        assert.equal((await appel(svc, "GET", "/admin/trousseau", { cle: lea })).statut, 200, "les autres gardent le leur");
    });
});

describe("devenir admin : la demande d'admin (§ 4)", () => {
    const corps = (nom) => ({ nom, courriel: nom.toLowerCase() + "@exemple.invalid", pc: "PC-" + nom.toUpperCase(), poste: hasard(16), logiciel: "XPG-License-Admin 1.44.0" });

    it("deposer : 201, l'empreinte (24 hexadecimaux en 6 groupes) ; une ligne << demande_admin >>", async () => {
        const r = await appel(svc, "POST", "/v1/admins/demande", { cle: zoe, corps: corps("Zoé") });
        assert.equal(r.statut, 201, r.texte);
        const attendue = sha256Hex(zoe.pub).slice(0, 24).toUpperCase().match(/.{4}/g).join(" ");
        assert.equal(r.json.empreinte, attendue);
        assert.match(r.json.empreinte, /^[0-9A-F]{4}( [0-9A-F]{4}){5}$/);
        assert.equal(r.json.etat, "attente");
        const e = await appel(svc, "GET", "/v1/admins/demande?cle=" + zoe.hex, { cle: zoe });
        assert.equal(e.json.etat, "attente");
        erreur(await appel(svc, "GET", "/v1/admins/demande?cle=" + yan.hex, { cle: zoe }), 400, "champ");
        erreur(await appel(svc, "GET", "/v1/admins/demande", { cle: yan }), 404, "demande");
        const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: editeur });
        const d = j.json.lignes.map((t) => JSON.parse(t)).find((l) => JSON.parse(l.entree).action === "demande_admin");
        assert.ok(d);
        assert.equal(JSON.parse(d.entree).detail.cle, zoe.hex);
        assert.ok(!d.entree.includes("exemple.invalid"), "le courriel n'est pas ecrit au journal");
    });

    it("mal faite : 400 ; une cle deja admin : 409 deja", async () => {
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: yan, corps: { ...corps("Yan"), courriel: "pas une adresse" } }), 400, "champ");
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: yan, corps: { ...corps("Yan"), poste: "123" } }), 400, "champ");
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: yan, corps: { ...corps("Yan"), nom: "" } }), 400, "champ");
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: lea, corps: corps("Léa") }), 409, "deja");
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: editeur, corps: corps("Éditeur") }), 409, "deja");
    });

    it("l'admin+ les voit (empreinte, nom, courriel, PC) ; un admin non", async () => {
        assert.equal((await appel(svc, "POST", "/v1/admins/demande", { cle: yan, corps: corps("Yan") })).statut, 201);
        const r = await appel(svc, "GET", "/admin/admins/demandes", { cle: editeur });
        assert.equal(r.statut, 200);
        const z = r.json.demandes.find((d) => d.cle === zoe.hex);
        assert.equal(z.nom, "Zoé");
        assert.equal(z.courriel, "zoé@exemple.invalid");
        assert.equal(z.pc, "PC-ZOÉ");
        assert.equal(z.id, zoe.id);
        assert.match(z.empreinte, /^[0-9A-F]{4}( [0-9A-F]{4}){5}$/);
        assert.ok(Number.isInteger(z.recue));
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: editeur })).json.admins_demandes, 2);
    });

    it("refuser : etat refusee ; accepter = la publier dans la liste : etat acceptee, la demande close", async () => {
        assert.equal((await appel(svc, "POST", "/admin/admins/demande/refuser", { cle: editeur, corps: { cle: yan.hex } })).statut, 200);
        assert.equal((await appel(svc, "GET", "/v1/admins/demande?cle=" + yan.hex, { cle: yan })).json.etat, "refusee");
        erreur(await appel(svc, "POST", "/admin/admins/demande/refuser", { cle: editeur, corps: { cle: intrus.hex } }), 404, "demande");
        const r = await publier(liste([fiche(zoe, "Zoé", ["demandes"])]));
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.demandes_acceptees, 1);
        assert.equal((await appel(svc, "GET", "/v1/admins/demande?cle=" + zoe.hex, { cle: zoe })).json.etat, "acceptee");
        assert.deepEqual((await appel(svc, "GET", "/admin/admins/demandes", { cle: editeur })).json.demandes, []);
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: zoe })).json.role, "admin", "Zoe est admin");
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: zoe, corps: corps("Zoé") }), 409, "deja");
    });

    // 1.47.0 : la liste << Demandes d'admin >> de l'Admin (docs/ADMINS.md, par. 4) - les closes aussi ; plusieurs acceptees
    // d'un coup : UNE liste (numero + 1) qui les porte toutes, chaque demande close.
    it("?toutes=1 : les closes aussi (etat, maj) ; deux acceptees par une seule liste", async () => {
        const t = await appel(svc, "GET", "/admin/admins/demandes?toutes=1", { cle: editeur });
        assert.equal(t.statut, 200, t.texte);
        const etats = Object.fromEntries(t.json.demandes.map((d) => [d.cle, d.etat]));
        assert.equal(etats[yan.hex], "refusee");
        assert.equal(etats[zoe.hex], "acceptee");
        assert.ok(t.json.demandes.every((d) => Number.isInteger(d.maj) && Number.isInteger(d.recue)));
        assert.deepEqual((await appel(svc, "GET", "/admin/admins/demandes", { cle: editeur })).json.demandes, [], "sans ?toutes : les seules en attente");
        erreur(await appel(svc, "GET", "/admin/admins/demandes?toutes=1", { cle: lea }), 403, "droit");
        const [a1, a2] = [nouvelleCle(), nouvelleCle()];
        for (const [k, n] of [
            [a1, "Hugo"],
            [a2, "Nina"],
        ])
            assert.equal((await appel(svc, "POST", "/v1/admins/demande", { cle: k, corps: corps(n) })).statut, 201);
        const avant = (await appel(svc, "GET", "/v1/etat")).json.admins;
        const r = await publier(liste([fiche(zoe, "Zoé", ["demandes"]), fiche(a1, "Hugo", ["licence_creer"]), fiche(a2, "Nina", [])]));
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.demandes_acceptees, 2, "les deux closes par la meme liste");
        assert.equal((await appel(svc, "GET", "/v1/etat")).json.admins, avant + 1, "une seule liste de plus");
        for (const k of [a1, a2]) assert.equal((await appel(svc, "GET", "/v1/admins/demande?cle=" + k.hex, { cle: k })).json.etat, "acceptee");
        assert.deepEqual((await appel(svc, "GET", "/admin/admins/demandes", { cle: editeur })).json.demandes, []);
    });
});

describe("la demande en ligne scellee pour plusieurs cles (§ 6)", () => {
    const pc = nouvelleCle();
    const pc2 = nouvelleCle();
    let n1;
    let n2;
    it("deposer : cle/scelle (l'editeur) et scelles (les admins qui ont le droit << demandes >>)", async () => {
        const r = await appel(svc, "POST", "/v1/demandes", {
            cle: pc,
            corps: { cle: editeur.id, scelle: "cG91ciBsJ8OpZGl0ZXVy", scelles: [{ cle: lea.id, scelle: "cG91ciBMw6lh" }] },
        });
        assert.equal(r.statut, 201, r.texte);
        n1 = r.json.numero;
        // un Studio d'avant : cle/scelle seulement
        const r2 = await appel(svc, "POST", "/v1/demandes", { cle: pc2, corps: { cle: editeur.id, scelle: "QUFBQQ==" } });
        assert.equal(r2.statut, 201);
        n2 = r2.json.numero;
    });

    it("refus : un admin sans le droit << demandes >>, une cle inconnue, deux fois la meme cle", async () => {
        const p = nouvelleCle();
        erreur(await appel(svc, "POST", "/v1/demandes", { cle: p, corps: { cle: editeur.id, scelle: "AAAA", scelles: [{ cle: anne.id, scelle: "AAAA" }] } }), 400, "cle");
        erreur(await appel(svc, "POST", "/v1/demandes", { cle: p, corps: { cle: editeur.id, scelle: "AAAA", scelles: [{ cle: intrus.id, scelle: "AAAA" }] } }), 400, "cle");
        erreur(
            await appel(svc, "POST", "/v1/demandes", { cle: p, corps: { cle: editeur.id, scelle: "AAAA", scelles: [{ cle: lea.id, scelle: "AAAA" }, { cle: lea.id, scelle: "AAAA" }] } }),
            400,
            "champ",
        );
        erreur(await appel(svc, "POST", "/v1/demandes", { cle: p, corps: { cle: editeur.id, scelle: "AAAA", scelles: [{ cle: lea.id, scelle: "<non>" }] } }), 400, "champ");
    });

    it("GET /admin/demandes : a chaque cle SON scelle ; sans scelle pour elle, la demande est listee sans contenu", async () => {
        const l = await appel(svc, "GET", "/admin/demandes?depuis=" + (n1 - 1), { cle: lea });
        assert.equal(l.statut, 200, l.texte);
        const a = l.json.demandes.find((d) => d.numero === n1);
        assert.equal(a.scelle, "cG91ciBMw6lh");
        assert.equal(a.cle, lea.id);
        const b = l.json.demandes.find((d) => d.numero === n2);
        assert.equal(b.scelle, null);
        assert.equal(b.pour_un_autre, true);
        const z = (await appel(svc, "GET", "/admin/demandes?depuis=" + (n1 - 1), { cle: zoe })).json.demandes.find((d) => d.numero === n1);
        assert.equal(z.scelle, null, "Zoe a le droit, mais la demande n'est pas scellee pour elle");
        const e = (await appel(svc, "GET", "/admin/demandes?depuis=" + (n1 - 1), { cle: editeur })).json.demandes;
        const ea = e.find((d) => d.numero === n1);
        assert.deepEqual(Object.keys(ea), ["numero", "poste", "cle", "scelle", "etat", "cree", "maj"], "l'admin+ : comme avant");
        assert.equal(ea.scelle, "cG91ciBsJ8OpZGl0ZXVy");
        assert.equal(ea.cle, editeur.id);
    });
});

describe("le journal des admins (§ 6)", () => {
    let premier;

    it("un admin ecrit : 201 et un numero ; la meme entree de nouveau : 200, le meme numero", async () => {
        const e = entree(lea, "licence_creer", { pc: "PC-LÉA", poste: machines.get(lea.hex), logiciel: "1.44.0" });
        const r = await appel(svc, "POST", "/admin/journal", { cle: lea, corps: e });
        assert.equal(r.statut, 201, r.texte);
        premier = r.json.numero;
        const r2 = await appel(svc, "POST", "/admin/journal", { cle: lea, corps: e });
        assert.equal(r2.statut, 200);
        assert.equal(r2.json.numero, premier);
        const r3 = await appel(svc, "POST", "/admin/journal", { cle: editeur, corps: entree(editeur, "admin_ajouter") });
        assert.equal(r3.statut, 201);
        assert.equal(r3.json.numero, premier + 1);
        const g = (await appel(svc, "GET", "/admin/admins", { cle: editeur })).json;
        assert.ok(g.admins.find((a) => a.cle === lea.hex).derniere > 0, "la derniere action de Lea");
        assert.ok(g.editeurs[0].derniere > 0);
    });

    it("refus : une signature d'une autre cle, une action inconnue ou du service, sans nonce, illisible", async () => {
        const e = entree(lea, "licence_creer");
        erreur(await appel(svc, "POST", "/admin/journal", { cle: anne, corps: e }), 400, "signature");
        for (const action of ["inconnue", "autre_pc", "signature_refusee", "demande_admin", "sauvegarde"])
            erreur(await appel(svc, "POST", "/admin/journal", { cle: lea, corps: entree(lea, action) }), 400, "entree");
        const sansNonce = '{"quand":1,"action":"connexion"}';
        erreur(
            await appel(svc, "POST", "/admin/journal", { cle: lea, corps: { entree: sansNonce, signature: lea.signer(Buffer.from("XPG-JOURNAL-1\n" + sansNonce)).toString("hex") } }),
            400,
            "entree",
        );
        erreur(await appel(svc, "POST", "/admin/journal", { cle: lea, corps: { entree: "x".repeat(17 * 1024), signature: "ab".repeat(64) } }), 400, "champ");
        erreur(await appel(svc, "POST", "/admin/journal", { cle: intrus, corps: entree(intrus, "connexion") }), 403, "editeur");
    });

    it("une entree d'un autre PC que celui de la liste : le service ecrit << autre_pc >> juste apres", async () => {
        const r = await appel(svc, "POST", "/admin/journal", { cle: anne, corps: entree(anne, "licence_prolonger", { pc: "PORTABLE", poste: hasard(16) }) });
        assert.equal(r.statut, 201, r.texte);
        const j = await appel(svc, "GET", "/admin/journal?depuis=" + r.json.numero, { cle: editeur });
        assert.equal(j.json.lignes.length, 1);
        const l = JSON.parse(j.json.lignes[0]);
        assert.equal(l.numero, r.json.numero + 1);
        assert.equal(l.cle, cleService);
        const e = JSON.parse(l.entree);
        assert.equal(e.action, "autre_pc");
        assert.equal(e.objet, anne.id);
        assert.equal(e.detail.ligne, r.json.numero);
        assert.equal(e.detail.poste_liste, machines.get(anne.hex));
        // le meme PC que la liste : rien de plus
        const r2 = await appel(svc, "POST", "/admin/journal", { cle: anne, corps: entree(anne, "connexion", { poste: machines.get(anne.hex) }) });
        assert.equal(r2.json.numero, r.json.numero + 2);
        assert.equal(j.json.dernier, r.json.numero + 1);
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: lea })).json.journal, r2.json.numero);
    });

    it("la chaine : numero 1, 2, 3… ; precedent = SHA-256 de la ligne d'avant ; chaque entree signee par sa cle", async () => {
        const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: editeur });
        assert.equal(j.statut, 200);
        let avant = null;
        for (const [i, texte] of j.json.lignes.entries()) {
            const l = JSON.parse(texte);
            assert.deepEqual(Object.keys(l), ["numero", "precedent", "recu", "cle", "entree", "signature"]);
            assert.equal(l.numero, i + 1);
            assert.equal(l.precedent, avant === null ? "" : sha256Hex(avant));
            assert.ok(verifierSignature(l.cle, "XPG-JOURNAL-1\n" + l.entree, Buffer.from(l.signature, "hex")), "ligne " + l.numero);
            avant = texte;
        }
        assert.equal(j.json.dernier, j.json.lignes.length);
        const suite = await appel(svc, "GET", "/admin/journal?depuis=" + (j.json.dernier - 1), { cle: editeur });
        assert.deepEqual(suite.json.lignes, [j.json.lignes.at(-1)]);
        erreur(await appel(svc, "GET", "/admin/journal?depuis=x", { cle: editeur }), 400, "champ");
    });
});

describe("les fichiers (§ 6)", () => {
    const fichier = (taille) => crypto.randomBytes(taille);
    const manifeste = (id, octets, { acces = "admins", sorte = "admin", nom = "XPG-License-Admin-Setup-1.44.0.exe", sha = sha256Hex(octets) } = {}) =>
        bloc(
            "fichier",
            JSON.stringify({ format: 1, id, nom, taille: octets.length, sha256: sha, sorte, version: "1.44.0", acces, morceau: 1048576, emis: EMIS }),
            editeur,
        );
    async function envoyer(id, octets, opts) {
        const r = await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(id, octets, opts) } });
        assert.equal(r.statut, 201, r.texte);
        let d;
        for (let n = 0; n * 1048576 < octets.length; n++) {
            d = await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + id + "&n=" + n, { cle: editeur, corps: octets.subarray(n * 1048576, (n + 1) * 1048576) });
            assert.equal(d.statut, 200, d.texte);
        }
        assert.equal(d.json.complet, true);
    }
    const morceau = (id, n, opts = {}) => appel(svc, "GET", "/v1/fichiers/morceau?id=" + id + "&n=" + n + (opts.jeton ? "&jeton=" + opts.jeton : ""), { ...opts, binaire: true });
    const jetonAdmin = (signataire, { cle = signataire.hex, expire = now() + 30 * 86400 } = {}) =>
        Buffer.from(bloc("jetonadmin", JSON.stringify({ cle, poste: hasard(16), expire }), signataire)).toString("base64");

    const A = { id: hasard(16), octets: fichier(2 * 1048576 + 12345) }; // acces admins, 3 morceaux
    const P = { id: hasard(16), octets: fichier(5000) }; // public
    const J = { id: hasard(16), octets: fichier(1048576 + 7) }; // jeton, 2 morceaux

    it("le manifeste (admin+), puis les morceaux : 1 Mio chacun, le dernier le reste ; complet au dernier", async () => {
        const r = await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(A.id, A.octets) } });
        assert.equal(r.statut, 201, r.texte);
        assert.deepEqual(r.json, { id: A.id, morceaux: 3, recus: 0, complet: false, manquants: [0, 1, 2] });
        assert.equal((await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(A.id, A.octets) } })).statut, 200, "le meme : repris");
        erreur(await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(A.id, P.octets) } }), 409, "existe");
        erreur(await appel(svc, "POST", "/admin/fichiers", { cle: lea, corps: { manifeste: manifeste(hasard(16), A.octets) } }), 403, "droit");
        erreur(await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(hasard(16), A.octets, { nom: "../setup.exe" }) } }), 400, "document");
        erreur(await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + A.id + "&n=0", { cle: editeur, corps: A.octets.subarray(0, 1000) }), 400, "taille");
        erreur(await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + A.id + "&n=3", { cle: editeur, corps: A.octets.subarray(0, 1000) }), 400, "n");
        let d = await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + A.id + "&n=2", { cle: editeur, corps: A.octets.subarray(2 * 1048576) });
        assert.equal(d.statut, 200, d.texte);
        assert.deepEqual(d.json.manquants, [0, 1]);
        erreur(await appel(svc, "GET", "/v1/fichiers/manifeste?id=" + A.id), 404, "fichier");
        d = await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + A.id + "&n=0", { cle: editeur, corps: A.octets.subarray(0, 1048576) });
        assert.equal(d.json.complet, false);
        d = await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + A.id + "&n=1", { cle: editeur, corps: A.octets.subarray(1048576, 2 * 1048576) });
        assert.equal(d.statut, 200, d.texte);
        assert.equal(d.json.complet, true);
        const m = await appel(svc, "GET", "/v1/fichiers/manifeste?id=" + A.id);
        assert.equal(m.statut, 200);
        assert.equal(m.texte, manifeste(A.id, A.octets));
    });

    it("un fichier dont le SHA-256 n'est pas celui du manifeste : 409 empreinte, ses morceaux effaces", async () => {
        const id = hasard(16);
        const o = fichier(3000);
        assert.equal((await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(id, o, { sha: sha256Hex("autre chose") }) } })).statut, 201);
        erreur(await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + id + "&n=0", { cle: editeur, corps: o }), 409, "empreinte");
        const r = await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(id, o, { sha: sha256Hex("autre chose") }) } });
        assert.deepEqual(r.json.manquants, [0]);
    });

    it("acces admins : un appel signe par un admin, ou le jeton d'un admin (X-XPG-Jeton) ; sinon 403", async () => {
        erreur(await morceau(A.id, 0), 403, "acces");
        const r = await morceau(A.id, 1, { cle: lea });
        assert.equal(r.statut, 200);
        assert.equal(r.type, "application/octet-stream");
        assert.ok(r.octets.equals(A.octets.subarray(1048576, 2 * 1048576)));
        assert.ok((await morceau(A.id, 2, { cle: editeur })).octets.equals(A.octets.subarray(2 * 1048576)));
        erreur(await morceau(A.id, 0, { cle: nouvelleCle() }), 403, "acces");
        const t = await morceau(A.id, 0, { entetes: { "X-XPG-Jeton": jetonAdmin(anne) } });
        assert.equal(t.statut, 200);
        assert.ok(t.octets.equals(A.octets.subarray(0, 1048576)));
        erreur(await morceau(A.id, 0, { entetes: { "X-XPG-Jeton": jetonAdmin(anne, { expire: now() - 10 }) } }), 403, "jeton");
        erreur(await morceau(A.id, 0, { entetes: { "X-XPG-Jeton": jetonAdmin(intrus) } }), 403, "jeton");
        erreur(await morceau(A.id, 0, { entetes: { "X-XPG-Jeton": jetonAdmin(intrus, { cle: anne.hex }) } }), 403, "jeton");
        erreur(await morceau(A.id, 0, { entetes: { "X-XPG-Jeton": "pas un jeton" } }), 403, "jeton");
    });

    it("acces public : tout le monde", async () => {
        await envoyer(P.id, P.octets, { acces: "public", sorte: "studios", nom: "IHM-Studio-Setup-1.44.1.exe" });
        const r = await morceau(P.id, 0);
        assert.equal(r.statut, 200);
        assert.ok(r.octets.equals(P.octets));
        erreur(await morceau(P.id, 1), 400, "n");
    });

    it("acces jeton : un lien tire par l'admin+ ; << une seule fois >> : efface au dernier morceau", async () => {
        await envoyer(J.id, J.octets, { acces: "jeton", sorte: "autre", nom: "Notes 1.44.1.pdf" });
        erreur(await morceau(J.id, 0), 403, "acces");
        erreur(await appel(svc, "POST", "/admin/fichiers/jeton", { cle: lea, corps: { id: J.id, duree: 3600, une_fois: true } }), 403, "droit");
        // 1.44.0 : un lien d'une seule fois, 48 h au plus ; un fichier joint (plusieurs fois), 30 jours au plus
        erreur(await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: J.id, duree: 200000, une_fois: true } }), 400, "champ");
        erreur(await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: J.id, duree: 3000000 } }), 400, "champ");
        assert.equal((await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: J.id, duree: 2592000 } })).statut, 201);
        const t = await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: J.id, duree: 3600, une_fois: true } });
        assert.equal(t.statut, 201, t.texte);
        assert.match(t.json.jeton, /^[0-9a-f]{32}$/);
        erreur(await morceau(A.id, 0, { jeton: t.json.jeton }), 403, "jeton");
        assert.ok((await morceau(J.id, 0, { jeton: t.json.jeton })).octets.equals(J.octets.subarray(0, 1048576)));
        assert.ok((await morceau(J.id, 0, { jeton: t.json.jeton })).octets.equals(J.octets.subarray(0, 1048576)), "pas encore le dernier");
        assert.ok((await morceau(J.id, 1, { jeton: t.json.jeton })).octets.equals(J.octets.subarray(1048576)));
        erreur(await morceau(J.id, 0, { jeton: t.json.jeton }), 403, "jeton");
        erreur(await morceau(J.id, 0, { jeton: hasard(16) }), 403, "jeton");
    });

    it("GET /v1/fichiers/lien?jeton= : le fichier entier (un navigateur) ; une seule fois", async () => {
        const t = await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: A.id, duree: 600, une_fois: true } });
        const r = await appel(svc, "GET", "/v1/fichiers/lien?jeton=" + t.json.jeton, { binaire: true });
        assert.equal(r.statut, 200);
        assert.equal(r.entetes.get("content-disposition"), 'attachment; filename="XPG-License-Admin-Setup-1.44.0.exe"');
        assert.equal(r.octets.length, A.octets.length);
        assert.equal(sha256Hex(r.octets), sha256Hex(A.octets));
        erreur(await appel(svc, "GET", "/v1/fichiers/lien?jeton=" + t.json.jeton), 403, "jeton");
        // un lien de plusieurs fois : encore
        const u = await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: P.id, duree: 600 } });
        for (let i = 0; i < 2; i++) assert.ok((await appel(svc, "GET", "/v1/fichiers/lien?jeton=" + u.json.jeton, { binaire: true })).octets.equals(P.octets));
    });

    describe("les versions (§ 6)", () => {
        const version = (v, fichiers, extra = {}) =>
            bloc("version", JSON.stringify({ format: 1, version: v, publiee: now(), importante: false, notes: ["Exports PDF : plus rapides"], fichiers, ...extra }), editeur);
        const f = (x, sorte, pour, nom) => ({ id: x.id, nom, taille: x.octets.length, sha256: sha256Hex(x.octets), sorte, pour });
        const fichiersV = () => [f(P, "studios", "tous", "IHM-Studio-Setup-1.44.1.exe"), f(A, "admin", "admins", "XPG-License-Admin-Setup-1.44.0.exe")];

        it("avant : GET /v1/version 404", async () => {
            erreur(await appel(svc, "GET", "/v1/version"), 404, "version");
        });

        it("publier (admin+) : ses fichiers entiers, de la bonne taille et du bon SHA-256", async () => {
            erreur(await appel(svc, "PUT", "/admin/versions", { cle: lea, corps: { bloc: version("1.44.1", fichiersV()) } }), 403, "droit");
            erreur(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.44.1", [{ ...fichiersV()[0], taille: 1 }]) } }), 409, "fichier");
            erreur(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.44.1", [{ ...fichiersV()[0], id: hasard(16) }]) } }), 409, "fichier");
            erreur(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.44", fichiersV()) } }), 400, "document");
            const texte = version("1.44.9", fichiersV());
            const r = await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: texte } });
            assert.equal(r.statut, 201, r.texte);
            assert.equal((await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: texte } })).statut, 200, "le meme bloc");
            const p = await appel(svc, "GET", "/v1/version");
            assert.equal(p.statut, 200);
            assert.equal(p.texte, texte);
            assert.equal((await appel(svc, "GET", "/v1/etat")).json.version_publiee, "1.44.9");
        });

        it("plus recente seulement (1.44.10 > 1.44.9, compare en nombres)", async () => {
            erreur(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.44.9", fichiersV().slice(0, 1)) } }), 409, "ancien");
            erreur(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.44.2", fichiersV()) } }), 409, "ancien");
            assert.equal((await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.44.10", fichiersV().slice(0, 1), { importante: true }) } })).statut, 201);
            assert.equal((await appel(svc, "GET", "/v1/etat")).json.version_publiee, "1.44.10");
            assert.equal(JSON.parse(/\n([\s\S]*)SIGNATURE/.exec((await appel(svc, "GET", "/v1/version")).texte)[1]).importante, true);
        });

        it("X-XPG-Logiciel : le logiciel de chaque cle ; GET /admin/versions : les versions, et combien de PC a jour", async () => {
            const p1 = nouvelleCle();
            const p2 = nouvelleCle();
            const p3 = nouvelleCle();
            const lire = (cle, logiciel) => appel(svc, "GET", "/v1/messages?depuis=0", { cle, entetes: { "X-XPG-Logiciel": logiciel } });
            assert.equal((await lire(p1, "IHM-Studio/1.44.0")).statut, 200);
            assert.equal((await lire(p2, "API-Studio/1.44.0")).statut, 200);
            assert.equal((await lire(p3, "IHM-Studio/1.43.0")).statut, 200);
            assert.equal((await lire(nouvelleCle(), "IHM Studio 1.44 (illisible)")).statut, 200, "un en-tete illisible : ignore");
            assert.equal((await appel(svc, "GET", "/admin/etat", { cle: lea, entetes: { "X-XPG-Logiciel": "XPG-License-Admin/1.44.0" } })).statut, 200);
            const r = await appel(svc, "GET", "/admin/versions", { cle: editeur });
            assert.equal(r.statut, 200, r.texte);
            assert.deepEqual(
                r.json.versions.map((v) => v.version),
                ["1.44.10", "1.44.9"],
            );
            assert.deepEqual(r.json.versions[1].fichiers.map((x) => x.present), [true, true]);
            assert.equal(r.json.logiciels.par_version["1.44.0"], 3);
            assert.equal(r.json.logiciels.par_version["1.43.0"], 1);
            assert.deepEqual(r.json.logiciels.par_logiciel["IHM-Studio"], { "1.43.0": 1, "1.44.0": 1 });
            assert.equal(r.json.logiciels.total, 4);
            const l = r.json.logiciels.admins.find((a) => a.cle === lea.hex);
            assert.deepEqual([l.logiciel, l.version], ["XPG-License-Admin", "1.44.0"]);
            const g = (await appel(svc, "GET", "/admin/admins", { cle: anne })).json.admins.find((a) => a.cle === lea.hex);
            assert.deepEqual([g.logiciel, g.version], ["XPG-License-Admin", "1.44.0"]);
            // le meme logiciel de nouveau, dans l'heure : rien n'est ecrit
            const n = await lire(p1, "IHM-Studio/1.44.0");
            assert.equal(Number(n.entetes.get("x-xpg-lignes-ecrites")), 1, "le nonce seulement");
            const m = await lire(p1, "IHM-Studio/1.44.1");
            // 1.47.0 : + la premiere fois que cette cle est vue avec cette version (versions_vues, stats.js)
            assert.equal(Number(m.entetes.get("x-xpg-lignes-ecrites")), 3, "le nonce, le logiciel et sa version vue");
        });
    });
});

describe("les messages a un PC (§ 6)", () => {
    const p = nouvelleCle();
    const autre = nouvelleCle();
    const message = (poste, titre) => bloc("message", JSON.stringify({ format: 1, poste, titre, de: "L'éditeur", emis: EMIS, scelle: "c2NlbGzDqSBwb3VyIGxlIFBD" }), editeur);

    it("deposer (admin+) : 201 et un numero ; un message pour un autre poste : 400", async () => {
        const r = await appel(svc, "PUT", "/admin/messages", { cle: editeur, corps: { poste: p.poste, bloc: message(p.poste, "Bonjour") } });
        assert.equal(r.statut, 201, r.texte);
        assert.ok(r.json.numero >= 1);
        erreur(await appel(svc, "PUT", "/admin/messages", { cle: editeur, corps: { poste: p.poste, bloc: message(autre.poste, "Bonjour") } }), 400, "poste");
        erreur(await appel(svc, "PUT", "/admin/messages", { cle: lea, corps: { poste: p.poste, bloc: message(p.poste, "Bonjour") } }), 403, "droit");
        erreur(
            await appel(svc, "PUT", "/admin/messages", { cle: editeur, corps: { poste: p.poste, bloc: bloc("message", JSON.stringify({ format: 1, poste: p.poste, scelle: "x" }), lea) } }),
            400,
            "signature",
        );
    });

    it("GET /v1/messages?depuis=N (signe par le PC) : les siens seulement ; 50 au plus par PC", async () => {
        const r = await appel(svc, "GET", "/v1/messages?depuis=0", { cle: p });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.messages.length, 1);
        assert.equal(r.json.messages[0].bloc, message(p.poste, "Bonjour"));
        const n = r.json.messages[0].numero;
        assert.deepEqual((await appel(svc, "GET", "/v1/messages?depuis=0", { cle: autre })).json.messages, []);
        assert.deepEqual((await appel(svc, "GET", "/v1/messages?depuis=" + n, { cle: p })).json.messages, []);
        erreur(await appel(svc, "GET", "/v1/messages?depuis=0"), 401, "signature");
        for (let i = 0; i < 52; i++) assert.equal((await appel(svc, "PUT", "/admin/messages", { cle: editeur, corps: { poste: p.poste, bloc: message(p.poste, "n° " + i) } })).statut, 201);
        const tous = (await appel(svc, "GET", "/v1/messages?depuis=0", { cle: p })).json.messages;
        assert.equal(tous.length, 50);
        assert.ok(tous[0].numero > n + 2, "les plus anciens sont partis");
    });
});
