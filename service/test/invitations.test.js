// =============================================================================
//  service/test/invitations.test.js - 1.49.0 : l'invitation par codes
//  (docs/ADMINS.md, par. 12 ; src/admins.js), essayee dans le VRAI runtime
//  (workerd, par outils/service/lancer-local.sh), par HTTP : l'admin+ cree un
//  code (POST /admin/admins/invitation : 48 h, une seule fois, le service n'en
//  garde que le SHA-256) ; la demande d'admin qui le porte le sert ; un code
//  inconnu, expire (l'horloge du service avancee de 49 h : XPG_DECALAGE_HORLOGE_ESSAI),
//  deja servi : 403 << invitation >> et sa phrase. Le code de confirmation (6
//  chiffres) : la meme valeur connue que l'essai C++ (licensing::admins::confirmationCode).
// =============================================================================
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, hasard, lancerService, nouvelleCle, sha256Hex } from "./aides.js";

const editeur = nouvelleCle();
const lea = nouvelleCle(); // un admin de la liste (pas l'admin+)
const nina = nouvelleCle(); // invitee : sa demande sert le code
const hugo = nouvelleCle(); // essaie le code de Nina (deja servi), puis un code expire
const camille = nouvelleCle(); // une demande sans invitation (un Admin d'avant la 1.49)
const etat = fs.mkdtempSync("/tmp/xpg-invitation-essai-");
let svc;

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
const demande = (nom, invitation) => ({
    nom,
    courriel: nom.split(" ")[0].toLowerCase() + "@exemple.invalid",
    pc: "PC-" + nom.split(" ")[0].toUpperCase(),
    poste: hasard(16),
    logiciel: "XPG Admin 1.49.0",
    ...(invitation === undefined ? {} : { invitation }),
});
const inviter = (corps, cle = editeur) => appel(svc, "POST", "/admin/admins/invitation", { cle, corps });

// Le code de confirmation (docs/ADMINS.md, par. 12) : les 8 premiers octets de SHA-256("xpg-confirmation-1" || cle brute),
// big-endian, modulo 1 000 000, << 123 456 >>.
function confirmation(cleHex) {
    const d = crypto.createHash("sha256").update(Buffer.concat([Buffer.from("xpg-confirmation-1"), Buffer.from(cleHex, "hex")])).digest();
    const s = String(Number(d.readBigUInt64BE(0) % 1000000n)).padStart(6, "0");
    return s.slice(0, 3) + " " + s.slice(3);
}

before(async () => {
    await relancer(0);
    const liste = JSON.stringify({
        format: 1,
        numero: 1,
        emise: now(),
        admins: [{ id: lea.id, cle: lea.hex, nom: "Léa Essai", pc: "PC-LEA", poste: hasard(16), depuis: now() - 100, profil: "Admin LV1", droits: ["demandes"], retire: 0, gardes: [] }],
    });
    const r = await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } });
    assert.equal(r.statut, 200, r.texte);
});

after(async () => {
    if (svc) await svc.arreter();
    fs.rmSync(etat, { recursive: true, force: true });
});

describe("l'invitation par codes (1.49.0)", () => {
    let code = "";
    let id = "";

    it("le code de confirmation : la valeur connue (la meme que l'essai C++)", () => {
        let k = "";
        for (let i = 0; i < 32; i++) k += i.toString(16).padStart(2, "0");
        assert.equal(confirmation(k), "763 090");
        assert.match(confirmation(nina.hex), /^\d{3} \d{3}$/);
    });

    it("creer : l'admin+ seul ; 201 {id, code INV-XXXX-XXXX, expire = 48 h} ; le code n'est nulle part dans la config", async () => {
        erreur(await inviter({ nom: "Nina Essai", courriel: "nina@exemple.invalid", profil: "Admin LV1" }, lea), 403, "droit");
        erreur(await inviter({ nom: "Nina Essai", courriel: "nina@exemple.invalid" }, nina), 403, "editeur");
        erreur(await inviter({ nom: "", courriel: "nina@exemple.invalid" }), 400, "champ");
        erreur(await inviter({ nom: "Nina Essai", courriel: "pas une adresse" }), 400, "champ");
        const r = await inviter({ nom: "Nina Essai", courriel: "nina@exemple.invalid", profil: "Admin LV1" });
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

    it("un code inconnu (ou mal ecrit) : 403 << Ce code d'invitation est inconnu. >>, rien n'est depose", async () => {
        for (const faux of ["INV-0000-0000", "pas un code", "INV-ABCD"]) {
            const r = await appel(svc, "POST", "/v1/admins/demande", { cle: hugo, corps: demande("Hugo Essai", faux) });
            erreur(r, 403, "invitation");
            assert.equal(r.json.message, "Ce code d'invitation est inconnu.");
        }
        erreur(await appel(svc, "POST", "/v1/admins/demande", { cle: hugo, corps: demande("Hugo Essai", 12) }), 400, "champ");
        erreur(await appel(svc, "GET", "/v1/admins/demande", { cle: hugo }), 404, "demande");
    });

    it("le servir : la demande liee (reponse et liste de l'admin+) ; minuscules et sans tirets acceptes ; le meme PC peut la renvoyer", async () => {
        const tape = code.toLowerCase().replace(/-/g, " ");
        const r = await appel(svc, "POST", "/v1/admins/demande", { cle: nina, corps: demande("Nina Essai", tape) });
        assert.equal(r.statut, 201, r.texte);
        assert.deepEqual(r.json.invitation, { id, nom: "Nina Essai", profil: "Admin LV1" });
        const l = await appel(svc, "GET", "/admin/admins/demandes", { cle: editeur });
        assert.equal(l.statut, 200, l.texte);
        const q = l.json.demandes.find((d) => d.cle === nina.hex);
        assert.ok(q && q.invitation, l.texte);
        assert.equal(q.invitation.id, id);
        assert.equal(q.invitation.nom, "Nina Essai");
        assert.equal(q.invitation.profil, "Admin LV1");
        assert.ok(q.invitation.invitee > 0);
        assert.ok(!l.texte.includes(sha256Hex(code)), "l'empreinte du code ne sort pas");
        // le meme PC (la meme cle) renvoie sa demande (une reponse perdue) : la meme invitation
        const again = await appel(svc, "POST", "/v1/admins/demande", { cle: nina, corps: demande("Nina Essai", code) });
        assert.equal(again.statut, 201, again.texte);
        assert.equal(again.json.invitation.id, id);
        // le journal du service : la demande dit son invitation (jamais le code)
        const j = await appel(svc, "GET", "/admin/journal?depuis=0", { cle: editeur });
        const lignes = j.json.lignes.map((t) => JSON.parse(JSON.parse(t).entree)).filter((e) => e.action === "demande_admin" && e.detail.cle === nina.hex);
        assert.ok(lignes.length >= 1 && lignes[0].detail.invitation === id);
        assert.ok(!j.texte.includes(code));
    });

    it("deja servi : 403 << Ce code d'invitation a déjà servi. >> pour une autre cle", async () => {
        const r = await appel(svc, "POST", "/v1/admins/demande", { cle: hugo, corps: demande("Hugo Essai", code) });
        erreur(r, 403, "invitation");
        assert.equal(r.json.message, "Ce code d'invitation a déjà servi.");
    });

    it("une demande sans invitation (un Admin d'avant) : acceptee comme avant, listee sans invitation", async () => {
        const r = await appel(svc, "POST", "/v1/admins/demande", { cle: camille, corps: demande("Camille Essai") });
        assert.equal(r.statut, 201, r.texte);
        assert.equal(r.json.invitation, undefined);
        const l = await appel(svc, "GET", "/admin/admins/demandes?toutes=1", { cle: editeur });
        const q = l.json.demandes.find((d) => d.cle === camille.hex);
        assert.ok(q && q.invitation === undefined, l.texte);
    });

    it("expire : 48 h plus tard (l'horloge avancee de 49 h), 403 << Ce code d'invitation a expiré (48 h). >>", async () => {
        const r = await inviter({ nom: "Hugo Essai", courriel: "hugo@exemple.invalid", profil: "" });
        assert.equal(r.statut, 201, r.texte);
        await relancer(49 * 3600);
        const e = await appel(svc, "POST", "/v1/admins/demande", { cle: hugo, corps: demande("Hugo Essai", r.json.code) });
        erreur(e, 403, "invitation");
        assert.equal(e.json.message, "Ce code d'invitation a expiré (48 h).");
        // celle de Nina, servie, reste liee a sa demande
        const l = await appel(svc, "GET", "/admin/admins/demandes", { cle: editeur });
        assert.equal(l.json.demandes.find((d) => d.cle === nina.hex).invitation.id, id);
    });
});
