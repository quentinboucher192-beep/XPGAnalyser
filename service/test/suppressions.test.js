// =============================================================================
//  service/test/suppressions.test.js - 1.48.1 : SUPPRIMER une licence qui ne
//  sert plus (POST /admin/licences/supprimer ; l'Admin : Licences > Supprimer,
//  Clients > Supprimer le client), essaye dans le VRAI runtime (workerd, par
//  outils/service/lancer-local.sh), par HTTP : le droit << licence_revoquer >>,
//  une licence en vigueur refusee (rien n'est efface), revoquee ou finie
//  effacee avec ses activations, une inconnue rendue dans << absentes >> ; les
//  deux lignes du journal (licence_supprimer, pc_supprimer) acceptees.
//  Des licences et des admins d'essai seulement.
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { appel, bloc, hasard, iso, lancerService, licenceJson, nouvelleCle } from "./aides.js";

const JOUR = 86400;
const editeur = nouvelleCle();
const lea = nouvelleCle(); // un admin avec << licence_revoquer >>
const hugo = nouvelleCle(); // un admin sans ce droit
let svc;

const now = () => Math.floor(Date.now() / 1000);

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
    assert.ok(typeof r.json.message === "string" && r.json.message.length > 5);
}

function fiche(cle, nom, droits) {
    return { id: cle.id, cle: cle.hex, nom, pc: "PC-" + nom.split(" ")[0].toUpperCase(), poste: hasard(16), depuis: now() - 100, profil: "Admin LV1", droits, retire: 0, gardes: [] };
}

async function deposer(opts) {
    const r = await appel(svc, "PUT", "/admin/licences", { cle: editeur, corps: { licence: bloc("licence", licenceJson(opts), editeur) } });
    assert.equal(r.statut, 200, r.texte);
}

async function surLeService() {
    const r = await appel(svc, "GET", "/admin/licences", { cle: editeur });
    assert.equal(r.statut, 200, r.texte);
    return r.json.licences.map((x) => x.licence).sort();
}

const supprimer = (licences, cle = editeur) => appel(svc, "POST", "/admin/licences/supprimer", { cle, corps: { licences } });

function entree(cle, action, objet) {
    const texte = JSON.stringify({ format: 1, quand: now(), action, objet, resume: "essai", detail: {}, avant: {}, apres: {}, nonce: hasard(16), poste: "", pc: "" });
    return { entree: texte, signature: cle.signer(Buffer.from("XPG-JOURNAL-1\n" + texte)).toString("hex") };
}

describe("supprimer des licences (1.48.1)", () => {
    const enVigueur = { id: "XPG-2026-00901", emission: now() - 60, expire: now() + 100 * JOUR };
    const revoquee = { id: "XPG-2026-00902", emission: now() - 60, expire: now() + 100 * JOUR };
    const finie = { id: "XPG-2026-00903", emission: now() - 10 * JOUR, expire: now() - JOUR };
    const unPc = { id: "XPG-2026-00904", emission: now() - 60, expire: now() + 100 * JOUR };

    before(async () => {
        svc = await lancerService({ cles: [editeur.hex], sansLimites: true });
        const liste = JSON.stringify({ format: 1, numero: 1, emise: now(), admins: [fiche(lea, "Léa Essai", ["licence_revoquer"]), fiche(hugo, "Hugo Essai", ["licence_creer"])] });
        const r = await appel(svc, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } });
        assert.equal(r.statut, 200, r.texte);
        for (const l of [enVigueur, revoquee, finie, unPc]) await deposer(l);
        const rev = bloc(
            "revocations",
            JSON.stringify(
                {
                    format: "xpg-revocations",
                    version: 1,
                    number: 1,
                    issued_at: iso(now()),
                    revoked: [
                        { license_id: revoquee.id, revoked_at: iso(now()), reason: "Essai" },
                        { license_id: unPc.id, device: nouvelleCle().hex, revoked_at: iso(now()), reason: "Un PC seulement" },
                    ],
                },
                null,
                2,
            ) + "\n",
            editeur,
        );
        const v = await appel(svc, "PUT", "/admin/revocations", { cle: editeur, corps: { revocations: rev } });
        assert.equal(v.statut, 200, v.texte);
    });
    after(async () => {
        if (svc) {
            await svc.arreter();
            fs.rmSync(svc.etat, { recursive: true, force: true });
        }
    });

    it("refus : le droit, un champ mal ecrit, une licence en vigueur (rien n'est efface)", async () => {
        erreur(await supprimer([revoquee.id], hugo), 403, "droit");
        erreur(await supprimer([]), 400, "champ");
        erreur(await supprimer("XPG-2026-00902"), 400, "champ");
        erreur(await supprimer(["pas une licence"]), 400, "champ");
        const r = await supprimer([revoquee.id, enVigueur.id]);
        erreur(r, 409, "en_vigueur");
        assert.match(r.json.message, /XPG-2026-00901/);
        // un PC revoque ne rend pas la licence << revoquee >> : elle sert encore aux autres
        erreur(await supprimer([unPc.id]), 409, "en_vigueur");
        assert.deepEqual(await surLeService(), [enVigueur.id, revoquee.id, finie.id, unPc.id].sort(), "tout est encore la");
    });

    it("revoquee et finie : effacees (un admin avec le droit) ; une inconnue dans << absentes >> ; une seconde fois : absentes", async () => {
        const r = await supprimer([revoquee.id, finie.id, "XPG-2026-00999", revoquee.id], lea);
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(r.json.supprimees.sort(), [revoquee.id, finie.id].sort());
        assert.deepEqual(r.json.absentes, ["XPG-2026-00999"]);
        assert.deepEqual(await surLeService(), [enVigueur.id, unPc.id].sort());
        const again = await supprimer([revoquee.id]);
        assert.equal(again.statut, 200, again.texte);
        assert.deepEqual(again.json, { supprimees: [], absentes: [revoquee.id] });
        // la liste de revocation ne bouge pas : la revocation protege toujours (une emission redeposee resterait refusee)
        const e = await appel(svc, "GET", "/v1/etat");
        assert.equal(e.json.revocations, 1);
    });

    it("le journal : licence_supprimer et pc_supprimer sont des actions connues", async () => {
        for (const action of ["licence_supprimer", "pc_supprimer"]) {
            const r = await appel(svc, "POST", "/admin/journal", { cle: lea, corps: entree(lea, action, "XPG-2026-00902") });
            assert.equal(r.statut, 201, r.texte);
        }
        erreur(await appel(svc, "POST", "/admin/journal", { cle: lea, corps: entree(lea, "tout_effacer", "x") }), 400, "entree");
    });
});
