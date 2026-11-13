// =============================================================================
//  service/test/invitations.test.js - 1.49.0 : l'invitation par codes ; 1.51.0 :
//  PLUS DE DEMANDE D'ADMIN (docs/ADMINS.md, par. 4 et 12 ; src/admins.js),
//  essayee dans le VRAI runtime (workerd, par outils/service/lancer-local.sh),
//  par HTTP : l'admin+ cree un code (POST /admin/admins/invitation : 48 h, une
//  seule fois, le service n'en garde que le SHA-256) ; XPG Admin s'inscrit avec
//  lui (POST /v1/admins/inscription : le code servi et la cle inscrite d'un coup) ;
//  l'Admin+ voit les inscrits (GET /admin/admins/inscrits) et les signe d'office.
//  Un code inconnu, expire (l'horloge du service avancee de 49 h :
//  XPG_DECALAGE_HORLOGE_ESSAI), deja servi : 403 << invitation >> et sa phrase.
//  Une demande d'avant (un XPG Admin 1.50 avec son code, en attente de ses
//  6 chiffres) est une inscription : signee d'office comme les autres ; une
//  demande d'avant SANS code ne l'est jamais (close au grand menage).
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, hasard, lancerService, nouvelleCle, sha256Hex } from "./aides.js";

const editeur = nouvelleCle();
const lea = nouvelleCle(); // un admin de la liste (pas l'admin+)
const nina = nouvelleCle(); // invitee : elle s'inscrit avec le code
const hugo = nouvelleCle(); // essaie le code de Nina (deja servi), puis un code expire
const paul = nouvelleCle(); // un XPG Admin 1.50 : sa demande avec code (POST /v1/admins/demande), jamais confirmee
const etat = fs.mkdtempSync("/tmp/xpg-invitation-essai-");
let svc;
let numeroListe = 1;

const now = () => Math.floor(Date.now() / 1000) + (svc ? svc.ecart : 0);

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
    assert.ok(typeof r.json.message === "string" && r.json.message.length > 5);
}
async function relancer(decalage) {
    if (svc) await svc.arreter();
    svc = await lancerService({ cles: [editeur.hex], decalage, etat, sansLimites: true });
}
const inscription = (invitation, extra = {}) => ({
    pc: "PC-ESSAI",
    poste: hasard(16),
    logiciel: "XPG-Admin 1.51.0",
    ...(invitation === undefined ? {} : { invitation }),
    ...extra,
});
const inviter = (corps, cle = editeur) => appel(svc, "POST", "/admin/admins/invitation", { cle, corps });
const fiche = (cle, nom, droits) => ({ id: cle.id, cle: cle.hex, nom, pc: "PC-" + nom.toUpperCase(), poste: hasard(16), depuis: now() - 100, profil: "Admin LV1", droits, retire: 0, gardes: [] });
// L'Admin+ signe d'office la liste qui porte les inscrits (ce que fait Workspace::admitInvitedAdmins).
async function publier(admins) {
    const liste = JSON.stringify({ format: 1, numero: ++numeroListe, emise: now(), admins });
    return appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } });
}

before(async () => {
    await relancer(0);
    const liste = JSON.stringify({ format: 1, numero: 1, emise: now(), admins: [fiche(lea, "Léa Essai", ["demandes"])] });
    const r = await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } });
    assert.equal(r.statut, 200, r.texte);
});

after(async () => {
    if (svc) await svc.arreter();
    fs.rmSync(etat, { recursive: true, force: true });
});

describe("l'invitation par codes, sans demande d'admin (1.51.0)", () => {
    let code = "";
    let id = "";

    it("creer : l'admin+ seul ; 201 {id, code INV-XXXX-XXXX, expire = 48 h} ; le code n'est nulle part dans la config", async () => {
        erreur(await inviter({ nom: "Nina Essai", courriel: "nina@exemple.invalid", profil: "Admin LV1" }, lea), 403, "droit");
        erreur(await inviter({ nom: "Nina Essai", courriel: "nina@exemple.invalid" }, nina), 403, "editeur");
        erreur(await inviter({ nom: "", courriel: "nina@exemple.invalid" }), 400, "champ");
        erreur(await inviter({ nom: "Nina Essai", courriel: "pas une adresse" }), 400, "champ");
        const r = await inviter({ nom: "Nina Essai", courriel: "nina@exemple.invalid", profil: "Admin LV2" });
        assert.equal(r.statut, 201, r.texte);
        assert.match(r.json.code, /^INV-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}$/);
        assert.match(r.json.id, /^inv-[0-9a-f]{16}$/);
        assert.ok(Math.abs(r.json.expire - (now() + 48 * 3600)) < 30, "48 h");
        code = r.json.code;
        id = r.json.id;
        const s = await appel(svc, "GET", "/admin/sauvegarde?table=config", { cle: editeur });
        assert.equal(s.statut, 200, s.texte);
        assert.ok(!s.texte.includes(code) && !s.texte.includes(code.replace(/-/g, "")), "le code lui-meme n'est jamais garde");
        assert.ok(s.texte.includes(sha256Hex(code)), "seulement son SHA-256");
    });

    it("un code inconnu (ou mal ecrit) : 403 << Ce code d'invitation est inconnu. >>, rien n'est inscrit", async () => {
        for (const faux of ["INV-0000-0000", "pas un code", "INV-ABCD"]) {
            const r = await appel(svc, "POST", "/v1/admins/inscription", { cle: hugo, corps: inscription(faux) });
            erreur(r, 403, "invitation");
            assert.equal(r.json.message, "Ce code d'invitation est inconnu.");
        }
        erreur(await appel(svc, "POST", "/v1/admins/inscription", { cle: hugo, corps: inscription(12) }), 400, "champ");
        erreur(await appel(svc, "GET", "/v1/admins/inscription", { cle: hugo }), 404, "inscription");
        assert.deepEqual((await appel(svc, "GET", "/admin/admins/inscrits", { cle: editeur })).json.inscrits, []);
    });

    it("le bon code : inscrite d'un coup (le nom et le courriel de l'invitation), listee chez l'admin+ ; minuscules et sans tirets acceptes ; la meme cle peut le renvoyer", async () => {
        const tape = code.toLowerCase().replace(/-/g, " ");
        const r = await appel(svc, "POST", "/v1/admins/inscription", { cle: nina, corps: inscription(tape) });
        assert.equal(r.statut, 201, r.texte);
        assert.equal(r.json.etat, "inscrit");
        assert.deepEqual(r.json.invitation, { id, nom: "Nina Essai", courriel: "nina@exemple.invalid", profil: "Admin LV2" });
        const l = await appel(svc, "GET", "/admin/admins/inscrits", { cle: editeur });
        assert.equal(l.statut, 200, l.texte);
        const q = l.json.inscrits.find((d) => d.cle === nina.hex);
        assert.ok(q && q.invitation, l.texte);
        assert.equal(q.nom, "Nina Essai", "le nom de l'invitation (rien a taper)");
        assert.equal(q.courriel, "nina@exemple.invalid");
        assert.equal(q.invitation.id, id);
        assert.equal(q.invitation.profil, "Admin LV2");
        assert.equal(q.invitation.par, editeur.id);
        assert.ok(q.invitation.invitee > 0 && q.invitation.servie > 0);
        assert.ok(!l.texte.includes(sha256Hex(code)), "l'empreinte du code ne sort pas");
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: editeur })).json.admins_inscrits, 1);
        // la meme cle (le meme PC) le renvoie : une reponse perdue
        const again = await appel(svc, "POST", "/v1/admins/inscription", { cle: nina, corps: inscription(code) });
        assert.equal(again.statut, 201, again.texte);
        assert.equal(again.json.invitation.id, id);
        // le journal : aucune ligne << demande_admin >>, jamais le code
        const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: editeur });
        assert.ok(!j.json.lignes.some((t) => JSON.parse(JSON.parse(t).entree).action === "demande_admin"));
        assert.ok(!j.texte.includes(code));
    });

    it("deja servi : 403 << Ce code d'invitation a déjà servi. >> pour une autre cle (une seule fois)", async () => {
        const r = await appel(svc, "POST", "/v1/admins/inscription", { cle: hugo, corps: inscription(code) });
        erreur(r, 403, "invitation");
        assert.equal(r.json.message, "Ce code d'invitation a déjà servi.");
        // par la route d'avant aussi
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: hugo, corps: { nom: "Hugo", courriel: "hugo@exemple.invalid", ...inscription(code) } }), 403, "invitation");
    });

    it("une demande d'avant (XPG Admin 1.50, son code, ses 6 chiffres jamais tapes) est une inscription : l'Admin+ la signe d'office", async () => {
        const r = await inviter({ nom: "Paul Essai", courriel: "paul@exemple.invalid", profil: "Admin LV1" });
        assert.equal(r.statut, 201, r.texte);
        const d = await appel(svc, "POST", "/v1/admins/demande", { cle: paul, corps: { nom: "Paul Essai", courriel: "paul@exemple.invalid", ...inscription(r.json.code) } });
        assert.equal(d.statut, 201, d.texte);
        assert.equal(d.json.etat, "attente", "la reponse d'avant (l'ancien Admin la comprend)");
        assert.equal((await appel(svc, "GET", "/v1/admins/inscription", { cle: paul })).json.etat, "inscrit");
        const l = (await appel(svc, "GET", "/admin/admins/inscrits", { cle: editeur })).json.inscrits;
        assert.deepEqual(l.map((x) => x.cle).sort(), [nina.hex, paul.hex].sort());
        // l'Admin+ signe d'office une liste qui porte les deux : admis, l'ancien Admin lit << acceptee >>
        const p = await publier([fiche(lea, "Léa Essai", ["demandes"]), fiche(nina, "Nina Essai", ["licence_creer", "demandes"]), fiche(paul, "Paul Essai", ["demandes"])]);
        assert.equal(p.statut, 200, p.texte);
        assert.equal(p.json.inscrits_admis, 2);
        assert.equal((await appel(svc, "GET", "/v1/admins/inscription", { cle: nina })).json.etat, "admis");
        assert.equal((await appel(svc, "GET", "/v1/admins/demande", { cle: paul })).json.etat, "acceptee");
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: nina })).json.role, "admin");
        assert.deepEqual((await appel(svc, "GET", "/admin/admins/inscrits", { cle: editeur })).json.inscrits, []);
        assert.equal((await appel(svc, "GET", "/admin/etat", { cle: editeur })).json.admins_inscrits, 0);
    });

    it("une demande d'avant SANS code : 410 (mettre a jour, un code d'invitation) ; jamais listee", async () => {
        const camille = nouvelleCle();
        const r = await appel(svc, "POST", "/v1/admins/demande", { cle: camille, corps: { nom: "Camille Essai", courriel: "camille@exemple.invalid", ...inscription() } });
        erreur(r, 410, "mise_a_jour");
        assert.match(r.json.message, /code d'invitation/);
        assert.deepEqual((await appel(svc, "GET", "/admin/admins/inscrits", { cle: editeur })).json.inscrits, []);
    });

    it("expire : 48 h plus tard (l'horloge avancee de 49 h), 403 << Ce code d'invitation a expiré (48 h). >> ; les admis le restent", async () => {
        const r = await inviter({ nom: "Hugo Essai", courriel: "hugo@exemple.invalid", profil: "" });
        assert.equal(r.statut, 201, r.texte);
        await relancer(49 * 3600);
        const e = await appel(svc, "POST", "/v1/admins/inscription", { cle: hugo, corps: inscription(r.json.code) });
        erreur(e, 403, "invitation");
        assert.equal(e.json.message, "Ce code d'invitation a expiré (48 h).");
        erreur(await appel(svc, "GET", "/v1/admins/inscription", { cle: hugo }), 404, "inscription");
        assert.equal((await appel(svc, "GET", "/v1/admins/inscription", { cle: nina })).json.etat, "admis");
    });
});
