// =============================================================================
//  service/test/sauvegarde.test.js - 1.44.0 : la sauvegarde du service entier
//  (GET /admin/sauvegarde) et sa restauration ailleurs (POST /admin/
//  restauration/debut, lot, fin) : un service A bien rempli, sauvegarde, puis
//  restaure dans un service B neuf (la meme cle de l'editeur) - B rend les
//  MEMES lignes, les memes documents, et un PC y renouvelle son bail
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, blocSignePar, certificat, demandeJson, hasard, iso, lancerService, licenceJson, lireBloc, nouveauCode, nouvelleCle, sha256Hex } from "./aides.js";

const now = () => Math.floor(Date.now() / 1000);
const JOUR = 86400;

const editeur = nouvelleCle();
const lea = nouvelleCle();
const pc = nouvelleCle();
const flottant = nouvelleCle();
let A;
let B;
let cleServiceA = "";
let sommaire = null;
let sauvegardeA = null;
const P = { id: hasard(16), octets: Buffer.from("un petit fichier public\n".repeat(200)) };

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
}

// Toutes les lignes de toutes les tables d'un service, page par page : {table: [lignes]}, et le nombre de pages.
async function exporter(svc, tables) {
    const out = {};
    const pages = {};
    for (const t of tables) {
        out[t] = [];
        pages[t] = 0;
        let depuis = "";
        for (;;) {
            const r = await appel(svc, "GET", "/admin/sauvegarde?table=" + t + (depuis ? "&depuis=" + depuis : ""), { cle: editeur });
            assert.equal(r.statut, 200, r.texte);
            assert.deepEqual(Object.keys(r.json), ["table", "lignes", "suivant", "schema"]);
            assert.ok(Buffer.byteLength(r.texte) <= 1.5 * 1024 * 1024 + 4096 || r.json.lignes.length === 1, "une page : 1,5 Mio au plus (ou une seule ligne)");
            out[t].push(...r.json.lignes);
            pages[t]++;
            if (!r.json.suivant) break;
            depuis = r.json.suivant;
        }
    }
    return { lignes: out, pages };
}

before(async () => {
    [A, B] = await Promise.all([lancerService({ cles: [editeur.hex], sansLimites: true }), lancerService({ cles: [editeur.hex], sansLimites: true })]);
    cleServiceA = (await appel(A, "GET", "/v1/etat")).json.cle_service;
});

after(async () => {
    for (const s of [A, B])
        if (s) {
            await s.arreter();
            fs.rmSync(s.etat, { recursive: true, force: true });
        }
});

describe("le service A, bien rempli", () => {
    it("certificat, registre, revocations, licences, activations, jeton flottant, Decouverte, demandes, poste, archive", async () => {
        const ok = (r, s = 200) => assert.equal(r.statut, s, r.texte);
        ok(await appel(A, "PUT", "/admin/certificat", { cle: editeur, corps: { certificat: certificat(editeur, cleServiceA) } }));
        const reg = bloc("registre", JSON.stringify({ format: "xpg-registry", version: 1, number: 3, issued_at: iso(now()), editions: [{ id: "PRO" }] }, null, 2) + "\n", editeur);
        ok(await appel(A, "PUT", "/admin/registre", { cle: editeur, corps: { registre: reg } }));
        const revs = bloc("revocations", JSON.stringify({ format: "xpg-revocations", version: 1, number: 2, issued_at: iso(now()), revoked: [{ license_id: "XPG-2026-R0001" }] }), editeur);
        ok(await appel(A, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: revs } }));
        const c = nouveauCode();
        ok(await appel(A, "PUT", "/admin/licences", { cle: editeur, corps: { licence: bloc("licence", licenceJson({ id: "XPG-2026-S0001", max: 3, emission: now() - 60, expire: now() + 200 * JOUR }), editeur), code_hash: c.hash } }));
        const f = nouveauCode();
        ok(
            await appel(A, "PUT", "/admin/licences", {
                cle: editeur,
                corps: { licence: bloc("licence", licenceJson({ id: "XPG-2026-S0002", type: "floating", max: 2, emission: now() - 60, expire: now() + 200 * JOUR }), editeur), code_hash: f.hash },
            }),
        );
        for (const [cle, code] of [
            [pc, c.code],
            [flottant, f.code],
        ])
            ok(await appel(A, "POST", "/v1/activer", { cle, corps: { code, demande: bloc("demande", demandeJson(cle), cle), nom: "PC de sauvegarde" } }));
        ok(await appel(A, "POST", "/v1/flottante/jeton", { cle: flottant, corps: { licence: "XPG-2026-S0002", station: flottant.station } }));
        ok(await appel(A, "POST", "/v1/decouverte", { cle: pc, corps: { machine: hasard(16) } }));
        ok(await appel(A, "PUT", "/admin/postes", { cle: editeur, corps: { poste: pc.poste, etat: bloc("poste", JSON.stringify({ version: 1, poste: pc.poste, numero: 1, emis: iso(now()) }), editeur) } }));
        // l'archive : trois gros fichiers (plusieurs pages), un tout petit
        for (const [i, taille] of [700, 700, 1500].entries())
            ok(await appel(A, "POST", "/admin/archive", { cle: editeur, corps: { base: i, fichiers: { ["gros-" + i + ".txt"]: ("é" + hasard(8)).repeat(Math.floor((taille * 1024) / 18)) } } }));
        ok(await appel(A, "POST", "/admin/archive", { cle: editeur, corps: { base: 3, fichiers: { "profils-admins.json": '{"profils":[]}' } } }));
    });

    it("les admins, une demande d'admin, le trousseau, le journal, une demande scellee pour deux cles", async () => {
        const ok = (r, s = 200) => assert.equal(r.statut, s, r.texte);
        const liste = JSON.stringify({
            format: 1,
            numero: 4,
            emise: now(),
            admins: [{ id: lea.id, cle: lea.hex, nom: "Léa", pc: "PC-LEA", poste: hasard(16), depuis: now(), profil: "Admin LV1", droits: ["demandes", "licence_creer"], retire: 0, gardes: [] }],
        });
        ok(await appel(A, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } }));
        ok(await appel(A, "POST", "/v1/admins/demande", { cle: nouvelleCle(), corps: { nom: "Yan", courriel: "yan@exemple.invalid", pc: "PC-YAN", poste: hasard(16), logiciel: "1.44.0" } }), 201);
        ok(await appel(A, "PUT", "/admin/trousseau", { cle: editeur, corps: { cle: lea.hex, scelle: "c2NlbGzDqQ==" } }));
        const texte = JSON.stringify({ quand: now(), action: "connexion", objet: "", resume: "s'est connectée", detail: {}, nonce: hasard(16) });
        ok(await appel(A, "POST", "/admin/journal", { cle: lea, corps: { entree: texte, signature: lea.signer(Buffer.from("XPG-JOURNAL-1\n" + texte)).toString("hex") } }), 201);
        ok(
            await appel(A, "POST", "/v1/demandes", { cle: nouvelleCle(), corps: { cle: editeur.id, scelle: "QUFBQQ==", scelles: [{ cle: lea.id, scelle: "TEVB" }] }, entetes: { "X-XPG-Logiciel": "IHM-Studio/1.44.0" } }),
            201,
        );
    });

    it("un fichier, une version, un message", async () => {
        const ok = (r, s = 200) => assert.equal(r.statut, s, r.texte);
        const manifeste = bloc(
            "fichier",
            JSON.stringify({ format: 1, id: P.id, nom: "IHM-Studio-Setup-1.44.1.exe", taille: P.octets.length, sha256: sha256Hex(P.octets), sorte: "studios", version: "1.44.1", acces: "public", morceau: 1048576, emis: now() }),
            editeur,
        );
        P.manifeste = manifeste;
        ok(await appel(A, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste } }), 201);
        ok(await appel(A, "PUT", "/admin/fichiers/morceau?id=" + P.id + "&n=0", { cle: editeur, corps: P.octets }));
        const v = { format: 1, version: "1.44.1", publiee: now(), importante: false, notes: [], fichiers: [{ id: P.id, nom: "IHM-Studio-Setup-1.44.1.exe", taille: P.octets.length, sha256: sha256Hex(P.octets), sorte: "studios", pour: "tous" }] };
        ok(await appel(A, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: bloc("version", JSON.stringify(v), editeur) } }), 201);
        ok(await appel(A, "PUT", "/admin/messages", { cle: editeur, corps: { poste: pc.poste, bloc: bloc("message", JSON.stringify({ format: 1, poste: pc.poste, titre: "Bonjour", scelle: "QUFBQQ==" }), editeur) } }), 201);
        ok(await appel(A, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: P.id, duree: 3600 } }), 201);
    });
});

describe("la sauvegarde (GET /admin/sauvegarde, admin+)", () => {
    it("le sommaire : le schema, la version, chaque table et ses lignes ; une ligne << sauvegarde >> au journal", async () => {
        erreur(await appel(A, "GET", "/admin/sauvegarde", { cle: lea }), 403, "droit");
        const r = await appel(A, "GET", "/admin/sauvegarde", { cle: editeur });
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(Object.keys(r.json), ["schema", "service_version", "tables", "heure"]);
        assert.equal(r.json.schema, 5); // 1.46.0 : la boutique ; 1.47.0 : les statistiques
        assert.equal(r.json.service_version, "1.49.0");
        sommaire = r.json;
        const n = Object.fromEntries(r.json.tables.map((t) => [t.nom, t.lignes]));
        for (const t of ["nonces", "debits", "morceaux"]) assert.equal(n[t], undefined, t + " n'est pas sauvegardee");
        assert.equal(n.licences, 2);
        assert.equal(n.activations, 2);
        assert.equal(n.jetons, 1);
        assert.equal(n.archive, 4);
        assert.equal(n.admins, 1);
        assert.equal(n.fichiers, 1);
        assert.equal(n.demandes_scelles, 1);
        const j = await appel(A, "GET", "/admin/journal?depuis=0", { cle: editeur });
        const derniere = JSON.parse(j.json.lignes.at(-1));
        assert.equal(JSON.parse(derniere.entree).action, "sauvegarde");
        assert.equal(n.journal, j.json.dernier, "la ligne << sauvegarde >> est dans la sauvegarde");
    });

    it("chaque table, par pages de 1,5 Mio au plus (le curseur << suivant >>) ; la config porte la cle du service", async () => {
        sauvegardeA = await exporter(
            A,
            sommaire.tables.map((t) => t.nom),
        );
        for (const t of sommaire.tables) assert.equal(sauvegardeA.lignes[t.nom].length, t.lignes, t.nom);
        assert.ok(sauvegardeA.pages.archive >= 2, "l'archive tient en plusieurs pages : " + sauvegardeA.pages.archive);
        const config = Object.fromEntries(sauvegardeA.lignes.config.map((l) => [l.cle, l.valeur]));
        assert.equal(config.cle_publique, cleServiceA);
        assert.match(config.cle_privee, /^[0-9a-f]+$/);
        assert.equal(config.schema, "5");
        erreur(await appel(A, "GET", "/admin/sauvegarde?table=nonces", { cle: editeur }), 400, "table");
        erreur(await appel(A, "GET", "/admin/sauvegarde?table=archive&depuis=pas-un-curseur", { cle: editeur }), 400, "champ");
    });
});

describe("la restauration dans un service B neuf (la meme cle de l'editeur)", () => {
    it("B a deja une licence : 409 pas_vide, sauf << remplacer >> ; un autre schema : 409 schema", async () => {
        const r = await appel(B, "PUT", "/admin/licences", { cle: editeur, corps: { licence: bloc("licence", licenceJson({ id: "XPG-2026-B0001", emission: now() - 5 }), editeur) } });
        assert.equal(r.statut, 200, r.texte);
        erreur(await appel(B, "POST", "/admin/restauration/debut", { cle: editeur, corps: { schema: 4 } }), 409, "pas_vide");
        erreur(await appel(B, "POST", "/admin/restauration/debut", { cle: editeur, corps: { schema: 2, remplacer: true } }), 409, "schema");
        // 1.46.0 : une sauvegarde d'un service plus recent (1.47.0 : schema 6) : refusee ; du schema 3 (1.44, sans la boutique) : prise.
        erreur(await appel(B, "POST", "/admin/restauration/debut", { cle: editeur, corps: { schema: 6, remplacer: true } }), 409, "schema");
        erreur(await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: "config", lignes: [] } }), 409, "restauration");
        erreur(await appel(B, "POST", "/admin/restauration/debut", { cle: lea, corps: { schema: 4, remplacer: true } }), 403, "editeur");
        const d = await appel(B, "POST", "/admin/restauration/debut", { cle: editeur, corps: { schema: 4, remplacer: true } });
        assert.equal(d.statut, 200, d.texte);
    });

    it("pendant la restauration : tout le reste repond 503 << restauration >>", async () => {
        erreur(await appel(B, "GET", "/v1/etat"), 503, "restauration");
        erreur(await appel(B, "GET", "/v1/registre"), 503, "restauration");
        erreur(await appel(B, "GET", "/admin/licences", { cle: editeur }), 503, "restauration");
        erreur(await appel(B, "POST", "/v1/renouveler", { cle: pc, corps: { station: pc.station } }), 503, "restauration");
    });

    it("les lots : les lignes telles quelles (une table inconnue, des colonnes fausses : 400)", async () => {
        erreur(await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: "nonces", lignes: [] } }), 400, "table");
        erreur(await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: "licences", lignes: [{ id: "x" }] } }), 400, "colonnes");
        erreur(
            await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: "trousseau", lignes: [{ cle: "x", scelle: { autre: 1 }, maj: 1 }] } }),
            400,
            "valeur",
        );
        for (const t of sommaire.tables) {
            const lignes = sauvegardeA.lignes[t.nom];
            // des lots de 1,5 Mio au plus, comme les pages
            let lot = [];
            let taille = 0;
            const envoyer = async () => {
                const r = await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: t.nom, lignes: lot } });
                assert.equal(r.statut, 200, t.nom + " : " + r.texte);
                assert.equal(r.json.lignes, lot.length);
                lot = [];
                taille = 0;
            };
            for (const l of lignes) {
                const n = Buffer.byteLength(JSON.stringify(l));
                if (lot.length && taille + n > 1.5 * 1024 * 1024) await envoyer();
                lot.push(l);
                taille += n;
            }
            if (lot.length) await envoyer();
        }
        // un lot envoye deux fois ne double rien
        const r = await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: "licences", lignes: sauvegardeA.lignes.licences } });
        assert.equal(r.statut, 200, r.texte);
    });

    it("la fin : les comptes, la cle du service de A, les fichiers a renvoyer ; une ligne << restauration >>", async () => {
        const r = await appel(B, "POST", "/admin/restauration/fin", { cle: editeur });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.cle_service, cleServiceA);
        assert.equal(r.json.fichiers_a_renvoyer, 1, "les octets des fichiers ne sont pas dans la sauvegarde");
        const n = Object.fromEntries(r.json.tables.map((t) => [t.nom, t.lignes]));
        for (const t of sommaire.tables) assert.equal(n[t.nom], t.lignes, t.nom);
        erreur(await appel(B, "POST", "/admin/restauration/fin", { cle: editeur }), 409, "restauration");
    });

    it("B rend EXACTEMENT les lignes de A (le journal : celles de A, puis la ligne << restauration >>, chainee)", async () => {
        const b = await exporter(
            B,
            sommaire.tables.map((t) => t.nom),
        );
        for (const t of sommaire.tables) {
            if (t.nom === "stats_jour") continue; // les appels du jour : une statistique, que B compte aussi
            if (t.nom === "stats_compteurs") continue; // 1.47.0 : les appels des admins par heure : B compte aussi les siens
            let a = sauvegardeA.lignes[t.nom];
            let x = b.lignes[t.nom];
            if (t.nom === "journal") {
                assert.equal(x.length, a.length + 1);
                const fin = JSON.parse(x.at(-1).texte);
                assert.equal(JSON.parse(fin.entree).action, "restauration");
                assert.equal(fin.precedent, sha256Hex(a.at(-1).texte), "la chaine continue");
                assert.equal(fin.cle, cleServiceA);
                x = x.slice(0, -1);
            }
            if (t.nom === "fichiers") {
                // ses octets ne sont pas la : le fichier est a renvoyer (recus 0, complet 0) ; le reste a l'identique
                assert.deepEqual([x[0].recus, x[0].complet], [0, 0]);
                const sans = (l) => ({ ...l, recus: null, complet: null });
                a = a.map(sans);
                x = x.map(sans);
            }
            if (t.nom === "config") {
                // le compteur des octets des fichiers : 0 sur B (aucun morceau)
                const sans = (l) => (l.cle === "octets_morceaux" ? { ...l, valeur: null } : l);
                a = a.map(sans);
                x = x.map(sans);
                assert.equal(b.lignes.config.find((l) => l.cle === "octets_morceaux").valeur, "0");
            }
            assert.deepEqual(x, a, t.nom);
        }
    });

    it("les Studios : le registre, le certificat, la liste des admins, a l'octet pres ; un PC renouvelle son bail (la cle du service de A)", async () => {
        for (const chemin of ["/v1/registre", "/v1/certificat", "/v1/admins", "/v1/version"]) {
            const a = await appel(A, "GET", chemin);
            const b = await appel(B, "GET", chemin);
            assert.equal(b.statut, 200, chemin + " : " + b.texte);
            assert.equal(b.texte, a.texte, chemin);
        }
        const ea = (await appel(A, "GET", "/v1/etat")).json;
        const eb = (await appel(B, "GET", "/v1/etat")).json;
        for (const k of ["cle_service", "certifie", "registre", "revocations", "changements", "admins", "version_publiee"]) assert.deepEqual(eb[k], ea[k], k);
        const r = await appel(B, "POST", "/v1/renouveler", { cle: pc, corps: { station: pc.station } });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.activations.length, 1);
        assert.ok(blocSignePar(r.json.activations[0].activation, cleServiceA), "l'activation est signee par la cle du service de A");
        assert.equal(JSON.parse(lireBloc(r.json.activations[0].activation).json).licence, "XPG-2026-S0001");
        assert.equal(r.json.postes["XPG-2026-S0001"].utilises, 1);
        assert.equal((await appel(B, "GET", "/v1/messages?depuis=0", { cle: pc })).json.messages.length, 1);
        const j = await appel(B, "POST", "/v1/flottante/jeton", { cle: flottant, corps: { licence: "XPG-2026-S0002", station: flottant.station } });
        assert.equal(j.statut, 200, j.texte);
        // un admin de A est admin sur B
        const e = await appel(B, "GET", "/admin/etat", { cle: lea });
        assert.equal(e.json.role, "admin");
        assert.deepEqual((await appel(B, "GET", "/admin/trousseau", { cle: lea })).json, { scelle: "c2NlbGzDqQ==" });
        const d = await appel(B, "GET", "/admin/demandes?depuis=0", { cle: lea });
        assert.equal(d.json.demandes.find((x) => x.scelle === "TEVB").cle, lea.id);
    });

    it("le fichier : son manifeste revient, ses octets se renvoient (le meme manifeste, puis les morceaux manquants)", async () => {
        erreur(await appel(B, "GET", "/v1/fichiers/morceau?id=" + P.id + "&n=0"), 404, "fichier");
        const m = await appel(B, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: P.manifeste } });
        assert.equal(m.statut, 200, m.texte);
        assert.deepEqual(m.json.manquants, [0]);
        const r = await appel(B, "PUT", "/admin/fichiers/morceau?id=" + P.id + "&n=0", { cle: editeur, corps: P.octets });
        assert.equal(r.json.complet, true);
        const g = await appel(B, "GET", "/v1/fichiers/morceau?id=" + P.id + "&n=0", { binaire: true });
        assert.ok(g.octets.equals(P.octets));
        assert.equal(Number((await appel(B, "GET", "/admin/stats", { cle: editeur })).statut), 200);
    });
});
