// =============================================================================
//  service/test/versions149.test.js - 1.49.0 : LES VERSIONS A TROIS SORTES
//  (docs/ADMINS.md, par. 12) - les Studios (pour tous), l'Admin classique
//  (XPG Admin, << admin >> : pour les admins), l'Admin+ (<< admin_plus >> : la
//  cle de l'editeur seule le telecharge ; jamais un lien, jamais un admin ni un
//  client). Un bloc d'avant (sorte << admin >> seule) reste valable. Le vrai
//  runtime (workerd, outils/service/lancer-local.sh), par HTTP.
// =============================================================================
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, hasard, lancerService, lireBloc, nouvelleCle, sha256Hex } from "./aides.js";

const now = () => Math.floor(Date.now() / 1000);
const editeur = nouvelleCle();
const camille = nouvelleCle(); // un admin de la liste (Camille Essai)
const intrus = nouvelleCle();
const EMIS = now();
let svc;

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
}

const fiche = (cle, nom, droits) => ({ id: cle.id, cle: cle.hex, nom, pc: "PC-ESSAI", poste: hasard(16), depuis: now() - 100, profil: "Admin LV1", droits, retire: 0, gardes: [] });
const manifeste = (id, octets, sorte, acces, nom) =>
    bloc("fichier", JSON.stringify({ format: 1, id, nom, taille: octets.length, sha256: sha256Hex(octets), sorte, version: "1.49.0", acces, morceau: 1048576, emis: EMIS }), editeur);
async function envoyer(x, sorte, acces, nom) {
    const r = await appel(svc, "POST", "/admin/fichiers", { cle: editeur, corps: { manifeste: manifeste(x.id, x.octets, sorte, acces, nom) } });
    assert.equal(r.statut, 201, r.texte);
    const d = await appel(svc, "PUT", "/admin/fichiers/morceau?id=" + x.id + "&n=0", { cle: editeur, corps: x.octets });
    assert.equal(d.statut, 200, d.texte);
    assert.equal(d.json.complet, true);
}
const morceau = (id, opts = {}) => appel(svc, "GET", "/v1/fichiers/morceau?id=" + id + "&n=0" + (opts.jeton ? "&jeton=" + opts.jeton : ""), { ...opts, binaire: true });
const jetonAdmin = (signataire) => Buffer.from(bloc("jetonadmin", JSON.stringify({ cle: signataire.hex, poste: hasard(16), expire: now() + 30 * 86400 }), signataire)).toString("base64");
const f = (x, sorte, pour, nom) => ({ id: x.id, nom, taille: x.octets.length, sha256: sha256Hex(x.octets), sorte, pour });
const version = (v, fichiers, extra = {}) =>
    bloc("version", JSON.stringify({ format: 1, version: v, publiee: now(), importante: false, notes: ["Essai 1.49.0"], fichiers, ...extra }), editeur);

const S = { id: hasard(16), octets: crypto.randomBytes(4000) }; // API-IHM-Studio-Setup (public)
const A = { id: hasard(16), octets: crypto.randomBytes(3000) }; // XPG-Admin-Setup (les admins)
const P = { id: hasard(16), octets: crypto.randomBytes(5000) }; // XPG-License-Admin-Setup-...-EDITEUR (l'admin+)

before(async () => {
    svc = await lancerService({ cles: [editeur.hex], sansLimites: true });
    const liste = bloc("admins", JSON.stringify({ format: 1, numero: 1, emise: now(), admins: [fiche(camille, "Camille Essai", ["licence_creer"])] }), editeur);
    assert.equal((await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: liste } })).statut, 200);
    await envoyer(S, "studios", "public", "API-IHM-Studio-Setup-1.49.0.exe");
    await envoyer(A, "admin", "admins", "XPG-Admin-Setup-1.49.0.exe");
    await envoyer(P, "admin_plus", "admins", "XPG-License-Admin-Setup-1.49.0-EDITEUR.exe");
});

after(async () => {
    if (svc) {
        await svc.arreter();
        fs.rmSync(svc.etat, { recursive: true, force: true });
    }
});

describe("1.49.0 : l'installateur de l'Admin+ (sorte admin_plus)", () => {
    it("telechargeable avec la seule signature de la cle de l'editeur (un appel signe, ou le jeton d'admin qu'elle a signe)", async () => {
        const r = await morceau(P.id, { cle: editeur });
        assert.equal(r.statut, 200, r.texte);
        assert.ok(r.octets.equals(P.octets));
        const t = await morceau(P.id, { entetes: { "X-XPG-Jeton": jetonAdmin(editeur) } });
        assert.equal(t.statut, 200, t.texte);
        assert.ok(t.octets.equals(P.octets));
    });

    it("jamais un admin, un client, un inconnu, un lien", async () => {
        erreur(await morceau(P.id), 403, "acces");
        erreur(await morceau(P.id, { cle: camille }), 403, "acces");
        erreur(await morceau(P.id, { cle: intrus }), 403, "acces");
        erreur(await morceau(P.id, { entetes: { "X-XPG-Jeton": jetonAdmin(camille) } }), 403, "acces");
        erreur(await morceau(P.id, { entetes: { "X-XPG-Jeton": jetonAdmin(intrus) } }), 403, "jeton");
        erreur(await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: P.id, duree: 3600, une_fois: true } }), 403, "acces");
        erreur(await appel(svc, "POST", "/admin/fichiers/jeton", { cle: editeur, corps: { id: P.id, duree: 86400 } }), 403, "acces");
        erreur(await morceau(P.id, { jeton: hasard(16) }), 403, "acces");
        // son manifeste reste public (le Studio de l'admin+ le verifie)
        assert.equal((await appel(svc, "GET", "/v1/fichiers/manifeste?id=" + P.id)).statut, 200);
    });

    it("l'Admin classique (sorte admin) : les admins (leur jeton, leur appel signe), pas un client", async () => {
        assert.equal((await morceau(A.id, { entetes: { "X-XPG-Jeton": jetonAdmin(camille) } })).statut, 200);
        assert.equal((await morceau(A.id, { cle: camille })).statut, 200);
        erreur(await morceau(A.id), 403, "acces");
        assert.equal((await morceau(S.id)).statut, 200, "les Studios : tout le monde");
    });
});

describe("1.49.0 : la version a trois sortes", () => {
    it("une sorte admin_plus refusee dans fichiers_admin_plus si elle n'en est pas une ; un fichier admin_plus absent : 409", async () => {
        erreur(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.49.0", [f(S, "studios", "tous", "API-IHM-Studio-Setup-1.49.0.exe")], {
            fichiers_admin_plus: [f(A, "admin", "admins", "XPG-Admin-Setup-1.49.0.exe")] }) } }), 400, "document");
        erreur(await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.49.0", [f(S, "studios", "tous", "API-IHM-Studio-Setup-1.49.0.exe")], {
            fichiers_admin_plus: [{ ...f(P, "admin_plus", "admin_plus", "XPG-License-Admin-Setup-1.49.0-EDITEUR.exe"), id: hasard(16) }] }) } }), 409, "fichier");
        erreur(await appel(svc, "PUT", "/admin/versions", { cle: camille, corps: { bloc: version("1.49.0", [f(S, "studios", "tous", "API-IHM-Studio-Setup-1.49.0.exe")]) } }), 403, "droit");
    });

    it("un bloc d'avant (studios + admin) reste valable", async () => {
        const r = await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: version("1.48.1", [f(S, "studios", "tous", "API-IHM-Studio-Setup-1.49.0.exe"), f(A, "admin", "admins", "XPG-Admin-Setup-1.49.0.exe")]) } });
        assert.equal(r.statut, 201, r.texte);
    });

    it("Studios + Admin dans fichiers, l'Admin+ dans fichiers_admin_plus : publiee ; ce que lit un Studio d'avant (fichiers) n'a jamais l'Admin+", async () => {
        const texte = version("1.49.0", [f(S, "studios", "tous", "API-IHM-Studio-Setup-1.49.0.exe"), f(A, "admin", "admins", "XPG-Admin-Setup-1.49.0.exe")], {
            fichiers_admin_plus: [f(P, "admin_plus", "admin_plus", "XPG-License-Admin-Setup-1.49.0-EDITEUR.exe")],
        });
        const r = await appel(svc, "PUT", "/admin/versions", { cle: editeur, corps: { bloc: texte } });
        assert.equal(r.statut, 201, r.texte);
        const pub = await appel(svc, "GET", "/v1/version");
        assert.equal(pub.statut, 200);
        assert.equal(pub.texte, texte);
        const doc = JSON.parse(lireBloc(pub.texte).json);
        assert.deepEqual(doc.fichiers.map((x) => x.sorte), ["studios", "admin"], "un Studio d'avant : studios et admin seulement");
        assert.deepEqual(doc.fichiers_admin_plus.map((x) => [x.sorte, x.pour]), [["admin_plus", "admin_plus"]]);
        assert.equal((await appel(svc, "GET", "/v1/etat")).json.version_publiee, "1.49.0");
        const l = await appel(svc, "GET", "/admin/versions", { cle: editeur });
        assert.deepEqual(l.json.versions[0].fichiers.map((x) => [x.sorte, x.pour, x.present]), [["studios", "tous", true], ["admin", "admins", true], ["admin_plus", "admin_plus", true]]);
    });
});
