// =============================================================================
//  service/test/coherence.test.js - 1.48.1 : LA COHERENCE DES STATISTIQUES
//  (docs/STATISTIQUES.md, par. 15) contre le VRAI Worker sur ce poste
// -----------------------------------------------------------------------------
//  Un petit historique fictif (deux clients, trois licences, des PC dans leurs
//  villes, une demande, trois admins, une version) fait par les routes normales,
//  un jour plus tot ; puis, une a une : un PC retire, une licence supprimee, un
//  client supprime, une option retiree, un admin sorti de la liste, une demande
//  traitee, une version neuve (et une plus ancienne refusee), Reevaluer deux fois,
//  le client remis. Apres CHAQUE pas : les courbes, le globe, le direct, les mises
//  a jour et les admins disent la meme chose entre eux et avec ce qui existe
//  (le point d'aujourd'hui, celui d'hier - le releve corrige -, les villes, la
//  derniere version, l'adoption). Enfin un etat incoherent expres (une sauvegarde
//  restauree avec des lignes de statistiques d'un client supprime et des compteurs
//  orphelins) : Reevaluer liste ses corrections, une seconde fois rien.
//  Des noms d'essai seulement (Atelier Exemple, Societe d'essai, Lea Essai...).
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { after, before, describe, it } from "node:test";

import { appel, bloc, certificat, demandeJson, hasard, iso, lancerService, nouveauCode, nouvelleCle, sha256Hex } from "./aides.js";

const JOUR = 86400;
const now = () => Math.floor(Date.now() / 1000);

const editeur = nouvelleCle();
const lea = nouvelleCle(); // admin : demandes, licences, client_supprimer, licence_revoquer
const hugo = nouvelleCle(); // admin : licence_creer seulement
const etatA = fs.mkdtempSync(path.join(os.tmpdir(), "xpg-service-coherence-"));
let A = null;
let B = null;
let t0 = 0;
let sauvegardeAvant = null; // les tables de A avant les suppressions (l'etat incoherent du dernier pas)

const LIEUX = {
    lyon: "ville=Lyon&region=Auvergne-Rh%C3%B4ne-Alpes&pays=FR&lat=45.764&lon=4.8357&colo=CDG",
    paris: "ville=Paris&region=%C3%8Ele-de-France&pays=FR&lat=48.8566&lon=2.3522&colo=CDG",
    toulouse: "ville=Toulouse&region=Occitanie&pays=FR&lat=43.6047&lon=1.4442&colo=MRS",
    bruxelles: "ville=Bruxelles&region=Bruxelles-Capitale&pays=BE&lat=50.8503&lon=4.3517&colo=BRU",
    nantes: "ville=Nantes&region=Pays+de+la+Loire&pays=FR&lat=47.2184&lon=-1.5536&colo=CDG",
};
function pc(lieu, logiciel = "IHM-Studio/1.46.0") {
    const c = nouvelleCle();
    c.entetes = { "X-XPG-Logiciel": logiciel, "X-XPG-Lieu-Essai": LIEUX[lieu] };
    return c;
}
const P1 = pc("lyon"); // Atelier Exemple, LA1
const P2 = pc("paris"); // Atelier Exemple, LA1 (retire au pas 1)
const P3 = pc("toulouse"); // Societe d'essai, LS1
const P4 = pc("nantes"); // Societe d'essai, LS1
const P5 = pc("lyon", "API-Studio/1.46.0"); // Atelier Exemple, LA2 flottante (supprimee au pas 2)
const P6 = pc("bruxelles"); // une Decouverte, une demande
const PCS = [P1, P2, P3, P4, P5, P6];
const LA1 = "XPG-2026-C0001";
const LA2 = "XPG-2026-C0002";
const LS1 = "XPG-2026-C0003";
const CA1 = nouveauCode();
const CA2 = nouveauCode();
const CS1 = nouveauCode();
const ATELIER = "Atelier Exemple";
const SOCIETE = "Société d'essai";

const ok = (r, s = 200) => assert.equal(r.statut, s, r.texte);
const de = (svc, cle, methode, chemin, corps) => appel(svc, methode, chemin, { cle, corps, entetes: cle.entetes || {} });
const heure = (svc) => now() + svc.ecart;
const jourDe = (t) => new Date(t * 1000).toISOString().slice(0, 10);

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
const deposer = (svc, qui, json, code) => de(svc, qui, "PUT", "/admin/licences", { licence: bloc("licence", json, qui), ...(code ? { code_hash: code.hash } : {}) });
const activer = (svc, p, code, nom) => de(svc, p, "POST", "/v1/activer", { code, demande: bloc("demande", demandeJson(p), p), nom });

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
    return appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: bloc("version", JSON.stringify(v), editeur) } });
}

let numeroRevocations = 0;
const revoquees = [];
async function revoquer(svc, licence) {
    revoquees.push({ license_id: licence, revoked_at: iso(heure(svc)), reason: "Fin de contrat" });
    const doc = { format: "xpg-revocations", version: 1, number: ++numeroRevocations, issued_at: iso(heure(svc)), revoked: revoquees };
    ok(await de(svc, editeur, "PUT", "/admin/revocations", { revocations: bloc("revocations", JSON.stringify(doc), editeur) }));
}
let numeroAdmins = 0;
async function listeAdmins(svc, qui) {
    const fiche = (cle, nom, droits) => ({ id: cle.id, cle: cle.hex, nom, pc: "PC-" + nom, poste: "", depuis: t0 - 100, profil: "Admin LV1", droits, retire: 0, gardes: [] });
    const tous = {
        lea: fiche(lea, "Léa Essai", ["demandes", "licence_creer", "licence_prolonger", "licence_option", "client_supprimer", "licence_revoquer"]),
        hugo: fiche(hugo, "Hugo Essai", ["licence_creer"]),
    };
    const liste = JSON.stringify({ format: 1, numero: ++numeroAdmins, emise: heure(svc), admins: qui.map((k) => tous[k]) });
    ok(await de(svc, editeur, "PUT", "/admin/admins", { admins: bloc("admins", liste, editeur) }));
}
const reevaluer = (svc, qui, corps = {}) => de(svc, qui, "POST", "/admin/stats/reevaluer", corps);

// Tout ce que les vues lisent, et leurs accords entre elles (apres chaque pas).
async function lire(svc) {
    const auj = jourDe(heure(svc));
    const hier = jourDe(heure(svc) - JOUR);
    const q = (o) => "/admin/stats/serie?" + new URLSearchParams(o).toString();
    const s = await de(svc, editeur, "GET", q({ mesures: "licences_actives,pc_actifs,pc_vus,demandes_attente,jetons_plafond", du: hier, au: auj, pas: "jour", comparer: "rien" }));
    ok(s);
    const pcClient = await de(svc, editeur, "GET", q({ mesures: "pc_actifs", du: auj, au: auj, par: "client", comparer: "rien" }));
    ok(pcClient);
    const nouvelles = await de(svc, editeur, "GET", q({ mesures: "licences_nouvelles,ventes_montant", du: hier, au: auj, par: "client", comparer: "rien" }));
    ok(nouvelles);
    const appels = await de(svc, editeur, "GET", q({ mesures: "admin_appels", du: hier, au: auj, par: "admin", comparer: "rien" }));
    ok(appels);
    const g = await de(svc, editeur, "GET", "/admin/globe");
    ok(g);
    const direct = await de(svc, editeur, "GET", "/admin/globe/direct");
    ok(direct);
    const m = await de(svc, editeur, "GET", "/admin/stats/mises-a-jour?du=" + jourDe(heure(svc) - 29 * JOUR) + "&au=" + auj);
    ok(m);
    const a = await de(svc, editeur, "GET", "/admin/stats/admins");
    ok(a);
    const S = s.json.mesures;
    const G = g.json;
    const M = m.json;
    // les accords
    assert.equal(G.totaux.pc + G.sans_lieu.pc, S.pc_actifs.points[1], "globe : ses PC = les PC actifs d'aujourd'hui (un PC par licence)");
    assert.equal(G.totaux.actifs + G.sans_lieu.actifs, direct.json.actifs, "actifs maintenant : le globe et le direct comptent les memes");
    const clientsPc = pcClient.json.mesures.pc_actifs.parts.filter((p) => p.cle && p.total > 0).map((p) => p.cle).sort();
    const clientsGlobe = [...new Set(G.villes.flatMap((v) => v.noms_clients))].sort();
    assert.deepEqual(clientsGlobe, clientsPc, "les clients du globe = ceux qui ont un PC actif");
    assert.equal(G.totaux.clients, clientsPc.length);
    assert.equal(G.totaux.pc, G.villes.reduce((n, v) => n + v.pc, 0), "les totaux du globe = la somme de ses villes");
    assert.equal(G.totaux.demandes, S.demandes_attente.points[1], "globe : les demandes en attente");
    assert.equal(M.derniere.version, G.version_derniere, "la derniere version : la meme partout");
    assert.equal(M.vus.studios, M.versions.reduce((n, v) => n + v.studios, 0));
    assert.equal(M.pas_a_jour_total, M.vus.studios + M.vus.admins - M.a_jour.studios - M.a_jour.admins, "les PC pas a jour : vus moins a jour");
    assert.equal(S.pc_vus.points[1], M.vus.studios + M.vus.admins, "PC vus : la courbe et les mises a jour");
    const nDerniere = M.versions.find((v) => v.version === M.derniere.version);
    assert.equal(M.a_jour.studios, nDerniere ? nDerniere.studios : 0, "a jour : sur la derniere version");
    const ids = a.json.admins.map((x) => x.id);
    for (const p of appels.json.mesures.admin_appels.parts || []) assert.ok(ids.includes(p.cle), "les appels d'un admin de la liste seulement (" + p.cle + ")");
    return { S, G, M, A: a.json, direct: direct.json, nouvelles: nouvelles.json.mesures, villes: new Map(G.villes.map((v) => [v.ville, v])) };
}

before(async () => {
    A = await lancerService({ cles: [editeur.hex], sansLimites: true, etat: etatA });
    const cleService = (await appel(A, "GET", "/v1/etat")).json.cle_service;
    // le releve du jour 0 est pris a ce premier appel : ce qui suit vient apres lui (a la seconde pres)
    await new Promise((r) => setTimeout(r, 1100));
    t0 = heure(A);
    editeur.entetes = { "X-XPG-Lieu-Essai": LIEUX.lyon, "X-XPG-Logiciel": "XPG-License-Admin/1.48.1" };
    lea.entetes = { "X-XPG-Lieu-Essai": LIEUX.toulouse, "X-XPG-Logiciel": "XPG-License-Admin/1.48.1" };
    hugo.entetes = { "X-XPG-Lieu-Essai": LIEUX.paris, "X-XPG-Logiciel": "XPG-License-Admin/1.48.1" };
    await listeAdmins(A, ["lea", "hugo"]);
    ok(await de(A, editeur, "PUT", "/admin/certificat", { certificat: certificat(editeur, cleService) }));
    const reg = bloc("registre", JSON.stringify({ format: "xpg-registry", version: 1, number: 1, issued_at: iso(t0), editions: [{ id: "PROFESSIONAL" }] }, null, 2) + "\n", editeur);
    ok(await de(A, editeur, "PUT", "/admin/registre", { registre: reg }));
    ok(await publierVersion(A, "1.46.0", t0 - 10 * JOUR), 201);
    // jour 0 : les licences, les PC, une Decouverte et une demande, les admins
    ok(await deposer(A, editeur, licenceJson({ id: LA1, max: 3, emission: t0 - 60, expire: t0 + 200 * JOUR, client: ATELIER, offre: "PROFESSIONAL" }), CA1));
    ok(await deposer(A, editeur, licenceJson({ id: LA2, type: "floating", max: 2, emission: t0 - 60, expire: t0 + 100 * JOUR, client: ATELIER, offre: "ENTERPRISE" }), CA2));
    ok(await deposer(A, lea, licenceJson({ id: LS1, max: 2, emission: t0 - 60, expire: t0 + 50 * JOUR, client: SOCIETE, offre: "PERSONAL" }), CS1));
    ok(await activer(A, P1, CA1.code, "ATELIER-PC1"));
    ok(await activer(A, P2, CA1.code, "ATELIER-PC2"));
    ok(await activer(A, P3, CS1.code, "ESSAI-PC3"));
    ok(await activer(A, P4, CS1.code, "ESSAI-PC4"));
    ok(await activer(A, P5, CA2.code, "ATELIER-PC5"));
    ok(await de(A, P5, "POST", "/v1/flottante/jeton", { licence: LA2, station: P5.station }));
    ok(await de(A, P6, "POST", "/v1/decouverte", { machine: hasard(16) }));
    for (const k of [editeur, lea, hugo]) ok(await de(A, k, "GET", "/admin/etat"));
    ok(await de(A, editeur, "GET", "/admin/stats/admins")); // les compteurs (les jetons) ecrits avant l'arret
    // un jour plus tard : le releve d'aujourd'hui (pris au demarrage) dit les niveaux d'hier soir
    await A.arreter();
    A = await lancerService({ cles: [editeur.hex], sansLimites: true, etat: etatA, decalage: JOUR });
    // le releve du jour 1 est pris au demarrage : les revocations qui suivent sont datees apres lui (l'horloge de ce
    // programme et celle du service peuvent differer d'une seconde)
    await new Promise((r) => setTimeout(r, 2100));
    for (const p of PCS.slice(0, 5)) ok(await de(A, p, "POST", "/v1/renouveler", { station: p.station }));
    const d = await de(A, P6, "POST", "/v1/demandes", { cle: editeur.id, scelle: "QUFBQQ==", scelles: [{ cle: lea.id, scelle: "TEVB" }] });
    ok(d, 201);
    P6.demande = d.json.numero;
    for (const k of [editeur, lea, hugo]) ok(await de(A, k, "GET", "/admin/etat"));
});

after(async () => {
    for (const s of [A, B]) if (s) await s.arreter();
    fs.rmSync(etatA, { recursive: true, force: true });
    if (B) fs.rmSync(B.etat, { recursive: true, force: true });
});

describe("la coherence apres chaque suppression ou changement (1.48.1)", () => {
    it("au depart : tout s'accorde ; le releve d'hier compte les 3 licences et les 5 PC", async () => {
        const x = await lire(A);
        assert.deepEqual(x.S.licences_actives.points, [3, 3]);
        assert.deepEqual(x.S.pc_actifs.points, [5, 5]);
        assert.deepEqual(x.S.jetons_plafond.points, [2, 2]);
        assert.equal(x.G.totaux.clients, 2);
        assert.deepEqual([...x.villes.keys()].sort(), ["Bruxelles", "Lyon", "Nantes", "Paris", "Toulouse"]);
        assert.equal(x.villes.get("Lyon").pc, 2);
        assert.equal(x.G.totaux.admins, 3);
        assert.equal(x.G.totaux.demandes, 1);
        assert.equal(x.M.derniere.version, "1.46.0");
        assert.equal(x.A.admins.length, 3);
        // la sauvegarde d'avant les suppressions (le dernier pas en fait un etat incoherent)
        const s = await appel(A, "GET", "/admin/sauvegarde", { cle: editeur });
        ok(s);
        sauvegardeAvant = { schema: s.json.schema, tables: {} };
        for (const t of s.json.tables) {
            const lignes = [];
            let depuis = "";
            for (;;) {
                const r = await appel(A, "GET", "/admin/sauvegarde?table=" + t.nom + (depuis ? "&depuis=" + depuis : ""), { cle: editeur });
                ok(r);
                lignes.push(...r.json.lignes);
                if (!r.json.suivant) break;
                depuis = r.json.suivant;
            }
            sauvegardeAvant.tables[t.nom] = lignes;
        }
    });

    it("un PC retire : il n'est plus actif aujourd'hui ni sur le globe ; hier, il l'etait (c'est arrive)", async () => {
        ok(await de(A, editeur, "POST", "/admin/activations/retirer", { licence: LA1, poste: P2.hex }));
        const x = await lire(A);
        assert.deepEqual(x.S.pc_actifs.points, [5, 4]);
        assert.equal(x.villes.get("Paris").pc, 0);
        assert.equal(x.G.totaux.pc, 4);
    });

    it("une licence supprimee (revoquee, puis Supprimer dans l'Admin) : ni aujourd'hui, ni dans le releve d'hier, ni son PC sur le globe", async () => {
        await revoquer(A, LA2);
        let x = await lire(A);
        assert.deepEqual(x.S.licences_actives.points, [3, 2], "revoquee : finie aujourd'hui, elle existait hier");
        const r = await reevaluer(A, editeur, { supprimer: { licences: [LA2] } });
        ok(r);
        const textes = r.json.corrections.map((c) => c.texte);
        assert.ok(textes.some((t) => t.startsWith("Licences actives, relevé du ") && t.endsWith(": 3 → 2")), textes.join(" | "));
        assert.ok(textes.some((t) => t.startsWith("Plafond des jetons, relevé du ") && t.endsWith(": 2 → 0")), textes.join(" | "));
        assert.ok(r.json.corrections.some((c) => c.groupe === "compteurs" && c.texte.includes(LA2)), "ses jetons retires");
        x = await lire(A);
        assert.deepEqual(x.S.licences_actives.points, [2, 2]);
        assert.deepEqual(x.S.pc_actifs.points, [4, 3]);
        assert.deepEqual(x.S.jetons_plafond.points, [0, 0]);
        assert.equal(x.villes.get("Lyon").pc, 1, "P5 n'est plus sur le globe");
        assert.ok(!(x.nouvelles.licences_nouvelles.parts || []).some((p) => p.total === 3), "elle n'est plus comptee parmi les nouvelles");
        assert.equal(x.nouvelles.licences_nouvelles.total, 2);
    });

    it("un client supprime : un admin sans le droit est refuse ; ensuite il n'est plus nulle part (courbes, globe, releve d'hier)", async () => {
        await revoquer(A, LS1);
        const refus = await reevaluer(A, hugo, { supprimer: { clients: [SOCIETE] } });
        assert.equal(refus.statut, 403, refus.texte);
        assert.equal(refus.json.erreur, "droit");
        ok(await reevaluer(A, hugo, {}), 200);
        const r = await reevaluer(A, lea, { supprimer: { clients: [SOCIETE] } });
        ok(r);
        assert.ok(r.json.corrections.some((c) => c.groupe === "globe" && c.texte.startsWith("Globe : Nantes retirée")), JSON.stringify(r.json.corrections));
        assert.equal(r.json.exclus.clients, 1);
        assert.equal(r.json.exclus.licences, 2, "LA2, et la licence du client");
        const x = await lire(A);
        assert.deepEqual(x.S.licences_actives.points, [1, 1]);
        assert.deepEqual(x.S.pc_actifs.points, [2, 1]);
        assert.equal(x.G.totaux.clients, 1);
        assert.ok(!x.villes.has("Nantes"), "son PC de Nantes n'est plus sur le globe");
        assert.equal(x.villes.get("Toulouse").pc, 0, "Toulouse reste : Lea y est");
        for (const m of ["licences_nouvelles", "ventes_montant"]) assert.ok(!(x.nouvelles[m].parts || []).some((p) => p.cle === SOCIETE), m + " : plus de « " + SOCIETE + " »");
        const fiche = await de(A, editeur, "GET", "/admin/stats/serie?" + new URLSearchParams({ mesures: "licences_nouvelles,pc_actifs", client: SOCIETE, comparer: "rien" }).toString());
        ok(fiche);
        assert.equal(fiche.json.mesures.licences_nouvelles.total, 0, "sa fiche : plus rien");
        assert.ok(!x.M.pas_a_jour.some((p) => p.client === SOCIETE), "ni dans les PC pas a jour");
    });

    it("une option retiree d'une licence (une emission sans elle) : les niveaux ne bougent pas, l'ajout passe reste un fait", async () => {
        const opt = [{ id: "ai_advanced", label: "Assistant IA avancé", features: [], from: iso(heure(A)), until: null }];
        ok(await deposer(A, editeur, licenceJson({ id: LA1, max: 3, emission: heure(A) - 30, expire: t0 + 200 * JOUR, client: ATELIER, offre: "PROFESSIONAL", options: opt })));
        ok(await deposer(A, editeur, licenceJson({ id: LA1, max: 3, emission: heure(A) - 20, expire: t0 + 200 * JOUR, client: ATELIER, offre: "PROFESSIONAL" })));
        const x = await lire(A);
        assert.deepEqual(x.S.licences_actives.points, [1, 1]);
        assert.deepEqual(x.S.pc_actifs.points, [2, 1]);
    });

    it("un admin sorti de la liste : ni dans les admins, ni ses appels, ni sur le globe ; Reevaluer retire ses compteurs", async () => {
        await listeAdmins(A, ["lea"]);
        const x = await lire(A);
        assert.ok(!x.A.admins.some((a) => a.id === hugo.id));
        assert.equal(x.G.totaux.admins, 2);
        assert.equal(x.villes.has("Paris") ? x.villes.get("Paris").admins : 0, 0);
        const r = await reevaluer(A, editeur);
        ok(r);
        assert.ok(r.json.corrections.some((c) => c.groupe === "compteurs" && c.texte.includes(hugo.id)), JSON.stringify(r.json.corrections));
    });

    it("une demande traitee : plus en attente, ni sur le globe", async () => {
        ok(await de(A, editeur, "POST", "/admin/demandes/traiter", { numero: P6.demande, issue: "refusee" }));
        const x = await lire(A);
        assert.deepEqual(x.S.demandes_attente.points, [0, 0]);
        assert.equal(x.G.totaux.demandes, 0);
    });

    it("une version neuve : la derniere partout, personne a jour, puis un PC qui s'y met ; une plus ancienne est refusee", async () => {
        ok(await publierVersion(A, "1.48.1", heure(A) - 60), 201);
        let x = await lire(A);
        assert.equal(x.M.derniere.version, "1.48.1");
        assert.equal(x.G.version_derniere, "1.48.1");
        assert.equal(x.M.a_jour.studios, 0);
        assert.ok(x.M.adoption.some((c) => c.version === "1.48.1" && c.logiciel === "studios"));
        P1.entetes["X-XPG-Logiciel"] = "IHM-Studio/1.48.1";
        ok(await de(A, P1, "POST", "/v1/renouveler", { station: P1.station }));
        x = await lire(A);
        assert.equal(x.M.a_jour.studios, 1);
        const courbe = x.M.adoption.find((c) => c.version === "1.48.1" && c.logiciel === "studios");
        assert.equal(courbe.jours[courbe.jours.length - 1], 1, "l'adoption : P1");
        assert.equal(x.villes.get("Lyon").pas_a_jour, 0, "Lyon : son seul PC actif est a jour");
        const ancienne = await publierVersion(A, "1.47.5", heure(A) - 30);
        assert.equal(ancienne.statut, 409, "une version ne se retire pas : une plus ancienne est refusee");
        x = await lire(A);
        assert.equal(x.M.derniere.version, "1.48.1");
    });

    it("Reevaluer quand tout s'accorde : rien a corriger, deux fois", async () => {
        for (let i = 0; i < 2; i++) {
            const r = await reevaluer(A, lea);
            ok(r);
            assert.equal(r.json.nombre, 0, JSON.stringify(r.json.corrections));
            assert.deepEqual(r.json.corrections, []);
        }
        await lire(A);
    });

    it("le client remis (Restaurer) : de nouveau compte, hier comme avant", async () => {
        const r = await reevaluer(A, lea, { remettre: { clients: [SOCIETE] } });
        ok(r);
        assert.ok(r.json.corrections.some((c) => c.groupe === "releves" && c.texte.startsWith("Licences actives, relevé du ") && c.texte.endsWith(": 1 → 2")), JSON.stringify(r.json.corrections));
        const x = await lire(A);
        assert.deepEqual(x.S.licences_actives.points, [2, 1], "hier : LA1 et LS1 ; aujourd'hui LS1 est revoquee");
        assert.deepEqual(x.S.pc_actifs.points, [4, 1]);
        assert.ok(x.villes.has("Nantes") === false || x.villes.get("Nantes").pc === 0, "son PC n'est plus actif (sa licence revoquee)");
        ok((await reevaluer(A, lea, { supprimer: { clients: [SOCIETE] } })));
        assert.equal((await reevaluer(A, lea)).json.nombre, 0);
    });

    it("un etat incoherent expres (une sauvegarde d'avant, restauree avec ce qui est supprime depuis) : Reevaluer liste ses corrections, puis rien", async () => {
        B = await lancerService({ cles: [editeur.hex], sansLimites: true, decalage: JOUR });
        const t = sauvegardeAvant.tables;
        t.config = t.config.filter((r) => r.cle !== "stats_exclus");
        t.config.push({ cle: "stats_exclus", valeur: JSON.stringify({ clients: ["société d'essai"], licences: [LA2] }) });
        const jour = jourDe(heure(B));
        t.stats_compteurs.push({ jour, mesure: "jetons_pic_licence", cle: "XPG-2026-FANTOME", n: 4 });
        t.stats_compteurs.push({ jour, mesure: "admin_appels", cle: "xpg-00000000000000aa|09", n: 12 });
        ok(await appel(B, "POST", "/admin/restauration/debut", { cle: editeur, corps: { schema: sauvegardeAvant.schema, remplacer: true } }));
        for (const [nom, lignes] of Object.entries(t)) for (let i = 0; i < lignes.length; i += 500) ok(await appel(B, "POST", "/admin/restauration/lot", { cle: editeur, corps: { table: nom, lignes: lignes.slice(i, i + 500) } }));
        ok(await appel(B, "POST", "/admin/restauration/fin", { cle: editeur }));
        const r = await reevaluer(B, editeur);
        ok(r);
        const g = (x) => r.json.corrections.filter((c) => c.groupe === x).map((c) => c.texte);
        assert.ok(g("releves").some((x) => x.startsWith("Licences actives, relevé du ") && x.endsWith(": 3 → 1")), JSON.stringify(r.json.corrections));
        assert.ok(g("releves").some((x) => x.startsWith("PC actifs, relevé du ") && x.endsWith(": 5 → 2")), JSON.stringify(r.json.corrections));
        assert.ok(g("compteurs").some((x) => x.includes("XPG-2026-FANTOME")));
        assert.ok(g("compteurs").some((x) => x.includes("xpg-00000000000000aa")));
        assert.equal(r.json.nombre, r.json.corrections.length);
        const encore = await reevaluer(B, editeur);
        ok(encore);
        assert.equal(encore.json.nombre, 0, JSON.stringify(encore.json.corrections));
    });
});
