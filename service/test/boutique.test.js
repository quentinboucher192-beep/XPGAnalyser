// =============================================================================
//  service/test/boutique.test.js - 1.46.0 : LA BOUTIQUE ET LES COMMANDES
//  (docs/BOUTIQUE.md) contre le VRAI Worker et un FAUX Stripe sur ce poste
//  (tests/outils/faux_stripe.mjs : jamais le vrai Stripe, aucune carte)
// -----------------------------------------------------------------------------
//  Le catalogue (admin+), la boutique fermee sans Stripe, une commande et sa page
//  de paiement (la carte seulement, jamais de client Stripe ni de carte gardee),
//  le webhook (signature fausse, trop ancienne, bonne ; rejoue : ignore), payee,
//  expiree, annulee, un montant qui ne va pas (a revoir), l'Admin (lister,
//  prendre, livree deux fois : la seconde refusee ; les droits), le
//  remboursement, Stripe qui refuse, chaque PC ne voit que ses commandes, la
//  sauvegarde porte les tables de la boutique - jamais les secrets de Stripe.
//  1.46.1 : le faux Stripe est celui d'aujourd'hui (2026-09-30.endive : il REFUSE
//  payment_method_types comme le vrai) ; Stripe-Version sur chaque appel ; les
//  produits et les prix crees a l'enregistrement du catalogue (la page de
//  paiement emploie le prix) ; la reduction suivie (le montant reduit jusqu'a sa
//  fin, puis le montant de base, sans rien faire) ; une cle restreinte sans le
//  droit des produits (la vente marche quand meme) ; un prix disparu chez Stripe.
//  Les valeurs des secrets : essai_jeton_... (des essais, jamais une vraie cle).
// =============================================================================
import assert from "node:assert/strict";
import fs from "node:fs";
import { after, before, describe, it } from "node:test";

import { MESSAGE_PMT, lancerFauxStripe, signerEvenement } from "../../tests/outils/faux_stripe.mjs";
import { VERSION_STRIPE } from "../src/boutique.js";
import { appel, bloc, hasard, lancerService, nouvelleCle, reponses } from "./aides.js";

const now = () => Math.floor(Date.now() / 1000);
const CLE = "essai_jeton_sk_test_" + hasard(12);
const WEBHOOK = "essai_jeton_whsec_" + hasard(12);

const editeur = nouvelleCle();
const lea = nouvelleCle(); // admin, droit << demandes >>
const hugo = nouvelleCle(); // admin, sans << demandes >>
const pc1 = nouvelleCle();
const pc2 = nouvelleCle();
let A; // avec Stripe
let B; // sans Stripe
let stripe;

function erreur(r, statut, code) {
    assert.equal(r.statut, statut, r.texte);
    assert.equal(r.json && r.json.erreur, code, r.texte);
}
function fiche(cle, nom, droits) {
    return { id: cle.id, cle: cle.hex, nom, pc: "PC-" + nom.toUpperCase(), poste: "", depuis: now() - 100, profil: "Admin LV1", droits, retire: 0, gardes: [] };
}
const CATALOGUE = {
    active: true,
    livraison_auto: true,
    devise: "eur",
    articles: [
        { id: "ed:PROFESSIONAL", genre: "edition", edition: "PROFESSIONAL", montant: 49000, libelle: "Licence PROFESSIONAL - 1 an" },
        { id: "opt:ai_advanced:365", genre: "option", option: "ai_advanced", jours: 365, montant: 9900, libelle: "Option Assistant IA avancé - 1 an" },
    ],
};
const scelle = (t) => Buffer.from("scelle d'essai : " + t).toString("base64");
const commande = (extra = {}) => ({
    article: "ed:PROFESSIONAL",
    societe: "Client d'essai",
    nom: "Camille Essai",
    courriel: "camille@exemple.invalid",
    cle: editeur.id,
    scelle: scelle("editeur"),
    scelles: [{ cle: lea.id, scelle: scelle("lea") }],
    ...extra,
});
// Un evenement de Stripe envoye au webhook de A par le faux Stripe (signe comme Stripe).
async function evenement(type, session, extra = {}) {
    const r = await fetch(stripe.base + "/__essai/evenement", {
        method: "POST",
        body: JSON.stringify({ url: A.base + "/v1/boutique/stripe", secret: WEBHOOK, type, session, ...extra }),
    });
    return r.json();
}
const sessionDe = (numero) => [...stripe.sessions.values()].find((s) => s.numero === numero);
const commandesDe = async (svc, pc) => (await appel(svc, "GET", "/v1/boutique/commandes", { cle: pc })).json.commandes;
const etatAdmin = async () => (await appel(A, "GET", "/admin/etat", { cle: editeur })).json;
const pages = () => stripe.requetes.filter((q) => q.methode === "POST" && q.chemin === "/v1/checkout/sessions");
const derniere = () => pages()[pages().length - 1];
const parametres = (q) => Object.fromEntries(q.params);
const heureA = () => now() + A.ecart; // l'heure du service A
// Ce qu'une page de paiement ne doit jamais porter : un client Stripe, une carte gardee, payment_method_types.
function sansCarteGardee(q) {
    for (const [k] of q.params) {
        assert.ok(!/^customer(\[|$)/.test(k) && k !== "customer_creation", "jamais de client Stripe : " + k);
        assert.ok(!/setup_future_usage|saved_payment_method_options|subscription|payment_method_collection/.test(k), "jamais de carte gardee : " + k);
        assert.ok(!/^payment_method_types/.test(k), "payment_method_types n'existe plus chez Stripe : " + k);
    }
    const p = parametres(q);
    assert.deepEqual(
        q.params.filter(([k]) => /payment_method_types/.test(k)),
        [["allowed_payment_method_types[0]", "card"]],
        "la carte seulement",
    );
    assert.equal(p["wallet_options[link][display]"], "never", "Link cache (il propose d'enregistrer la carte)");
    assert.equal(q.version, VERSION_STRIPE, "la version de l'API de Stripe fixee");
}

before(async () => {
    stripe = await lancerFauxStripe({ cle: CLE });
    [A, B] = await Promise.all([
        lancerService({ cles: [editeur.hex], sansLimites: true, stripe: { cle: CLE, webhook: WEBHOOK, base: stripe.base } }),
        lancerService({ cles: [editeur.hex], sansLimites: true }),
    ]);
    const liste = JSON.stringify({ format: 1, numero: 1, emise: now(), admins: [fiche(lea, "Léa", ["demandes", "licence_creer"]), fiche(hugo, "Hugo", ["licence_creer"])] });
    const r = await appel(A, "PUT", "/admin/admins", { cle: editeur, corps: { admins: bloc("admins", liste, editeur) } });
    assert.equal(r.statut, 200, r.texte);
});

after(async () => {
    for (const s of [A, B])
        if (s) {
            await s.arreter();
            fs.rmSync(s.etat, { recursive: true, force: true });
        }
    if (stripe) await stripe.arreter();
});

describe("la boutique sans Stripe (B)", () => {
    it("fermee : aucun article, une commande refusee 409 boutique ; le catalogue se regle quand meme", async () => {
        let r = await appel(B, "GET", "/v1/boutique");
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(r.json, { active: false, devise: "eur", mode: null, articles: [] });
        assert.equal((await appel(B, "PUT", "/admin/boutique", { cle: editeur, corps: CATALOGUE })).statut, 200);
        r = await appel(B, "GET", "/admin/boutique", { cle: editeur });
        assert.equal(r.statut, 200, r.texte);
        assert.deepEqual(r.json.stripe, { branche: false, mode: null, webhook: false, dernier_evenement: null });
        assert.equal(r.json.ouverte, false);
        assert.equal((await appel(B, "GET", "/v1/boutique")).json.active, false);
        assert.equal((await appel(B, "GET", "/v1/etat")).json.boutique, false);
        erreur(await appel(B, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande() }), 409, "boutique");
        // le webhook sans secret : refuse
        erreur(await appel(B, "POST", "/v1/boutique/stripe", { corps: "{}", entetes: { "Stripe-Signature": signerEvenement(WEBHOOK, "{}") } }), 400, "webhook");
    });
});

describe("le catalogue (A)", () => {
    it("la version 1.46.0 ; PUT /admin/boutique : l'admin+ seul, les montants et les identifiants verifies", async () => {
        assert.equal((await appel(A, "GET", "/v1/etat")).json.version, "1.52.0");
        assert.equal((await appel(A, "GET", "/v1/boutique")).json.active, false, "pas encore de catalogue : fermee");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: lea, corps: CATALOGUE }), 403, "droit");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: pc1, corps: CATALOGUE }), 403, "editeur");
        const mauvais = (a) => ({ ...CATALOGUE, articles: [{ ...CATALOGUE.articles[0], ...a }] });
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ montant: 49 }) }), 400, "champ");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ montant: 100000001 }) }), 400, "champ");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ id: "ed:AUTRE" }) }), 400, "champ");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ libelle: "" }) }), 400, "champ");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: { ...CATALOGUE, articles: [CATALOGUE.articles[0], CATALOGUE.articles[0]] } }), 400, "champ");
        // 1.46.1 : la reduction et le montant libre verifies
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ montant_reduit: 10 }) }), 400, "champ");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ montant_reduit: 9000 }) }), 400, "champ"); // sans sa fin
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ montant_reduit: 9000, fin_reduction: "demain" }) }), 400, "champ");
        erreur(await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: mauvais({ libre: "oui" }) }), 400, "champ");
        assert.equal(stripe.requetes.length, 0, "un catalogue refuse : rien chez Stripe");
        const r = await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: CATALOGUE });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.numero, 1);
        assert.equal(r.json.stripe_catalogue.etat, "ok", r.texte);
        assert.equal(r.json.stripe_catalogue.produits, 2);
    });

    it("1.46.1 : chaque article est un produit chez Stripe, avec son prix (cles d'idempotence, Stripe-Version)", async () => {
        const pro = stripe.produits.get("xpg-ed-PROFESSIONAL");
        const opt = stripe.produits.get("xpg-opt-ai_advanced-365");
        assert.ok(pro && opt, "les deux produits");
        assert.equal(pro.name, "Licence PROFESSIONAL - 1 an");
        assert.equal(pro.active, true);
        assert.equal(pro.metadata.xpg_article, "ed:PROFESSIONAL");
        const pp = stripe.prix.get(pro.default_price);
        assert.ok(pp && pp.active && pp.unit_amount === 49000 && pp.currency === "eur" && pp.tax_behavior === "inclusive", JSON.stringify(pp));
        assert.equal(pp.metadata.xpg_article, "ed:PROFESSIONAL");
        assert.equal(stripe.prix.get(opt.default_price).unit_amount, 9900);
        for (const q of stripe.requetes) {
            assert.equal(q.version, VERSION_STRIPE, q.chemin);
            assert.equal(q.auth, "bonne");
            if (q.chemin === "/v1/products" || q.chemin === "/v1/prices") assert.match(q.idem, /^xpg-(produit|prix)-/, "une cle d'idempotence");
        }
        // le meme catalogue enregistre de nouveau : rien de plus chez Stripe
        const avant = stripe.requetes.length;
        const r = await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: CATALOGUE });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.stripe_catalogue.etat, "ok");
        assert.equal(stripe.requetes.length, avant, "rien n'a change : aucun appel");
        // POST /admin/boutique/stripe : l'admin+ seul (l'Admin l'appelle apres Brancher Stripe)
        erreur(await appel(A, "POST", "/admin/boutique/stripe", { cle: lea, corps: {} }), 403, "droit");
        const s = await appel(A, "POST", "/admin/boutique/stripe", { cle: editeur, corps: {} });
        assert.equal(s.statut, 200, s.texte);
        assert.equal(s.json.stripe_catalogue.etat, "ok");
        const g = await appel(A, "GET", "/admin/boutique", { cle: editeur });
        assert.equal(g.json.stripe_catalogue.etat, "ok");
        assert.equal(g.json.articles[0].stripe_produit, "xpg-ed-PROFESSIONAL");
        assert.equal(g.json.articles[0].montant_actuel, 49000);
    });

    it("GET /admin/boutique (un admin aussi) : l'etat de Stripe, jamais la cle ; GET /v1/boutique : ce qui s'achete", async () => {
        const r = await appel(A, "GET", "/admin/boutique", { cle: hugo });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.numero, 2, "enregistre deux fois (le meme catalogue, 1.46.1)");
        assert.deepEqual(r.json.stripe, { branche: true, mode: "test", webhook: true, dernier_evenement: null });
        assert.equal(r.json.articles.length, 2);
        assert.ok(!r.texte.includes(CLE) && !r.texte.includes(WEBHOOK));
        const p = await appel(A, "GET", "/v1/boutique");
        assert.equal(p.statut, 200, p.texte);
        assert.deepEqual(p.json, {
            active: true,
            devise: "eur",
            mode: "test",
            articles: [
                { id: "ed:PROFESSIONAL", genre: "edition", edition: "PROFESSIONAL", montant: 49000, libelle: "Licence PROFESSIONAL - 1 an" },
                { id: "opt:ai_advanced:365", genre: "option", option: "ai_advanced", jours: 365, montant: 9900, libelle: "Option Assistant IA avancé - 1 an" },
            ],
        });
        assert.equal((await appel(A, "GET", "/v1/etat")).json.boutique, true);
    });
});

let n1 = 0; // la commande de pc1 (une offre)
describe("une commande et sa page de paiement", () => {
    it("les champs verifies : courriel, societe, article, option sans licence, cle de scellement", async () => {
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande({ courriel: "pas un courriel" }) }), 400, "champ");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande({ societe: "" }) }), 400, "champ");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande({ article: "ed:INCONNUE" }) }), 404, "article");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande({ article: "opt:ai_advanced:365" }) }), 400, "champ");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande({ cle: "xpg-0000000000000000" }) }), 400, "cle");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande({ scelles: [{ cle: hugo.id, scelle: scelle("hugo") }] }) }), 400, "cle");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { corps: commande() }), 401, "signature");
        assert.equal(pages().length, 0, "aucune page de paiement demandee a Stripe");
    });

    it("201 : la page de Stripe ; les parametres : la carte seulement, ni client, ni carte gardee, une facture", async () => {
        const t0 = now();
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande() });
        assert.equal(r.statut, 201, r.texte);
        n1 = r.json.numero;
        assert.ok(n1 >= 1);
        assert.match(r.json.url, /^https:\/\/checkout\.stripe\.invalid\/pay\/cs_test_essai\d+$/);
        assert.equal(r.json.montant, 49000);
        assert.equal(r.json.devise, "eur");
        assert.equal(r.json.mode, "test");
        assert.ok(r.json.expire >= t0 + 1800 && r.json.expire <= now() + 1900, "30 minutes (et une de marge)");
        assert.equal(pages().length, 1);
        const q = derniere();
        assert.equal(q.auth, "bonne", "la cle secrete en Bearer");
        assert.equal(q.idem, "commande-" + n1);
        assert.match(q.type, /^application\/x-www-form-urlencoded/);
        const p = parametres(q);
        assert.equal(p.mode, "payment");
        sansCarteGardee(q);
        assert.equal(p.customer_email, "camille@exemple.invalid");
        // 1.46.1 : le prix du produit Stripe (cree a l'enregistrement du catalogue), plus le montant donne
        assert.equal(p["line_items[0][price]"], stripe.produits.get("xpg-ed-PROFESSIONAL").default_price);
        assert.ok(!q.params.some(([k]) => k.includes("price_data")), "le prix, pas price_data");
        assert.equal(sessionDe(n1).montant, 49000);
        assert.equal(p["metadata[article]"], "ed:PROFESSIONAL");
        assert.equal(p["line_items[0][quantity]"], "1");
        assert.equal(p.client_reference_id, String(n1));
        assert.equal(p["metadata[commande]"], String(n1));
        assert.equal(p["invoice_creation[enabled]"], "true");
        assert.equal(p.locale, "fr");
        assert.equal(p.expires_at, String(r.json.expire));
        assert.equal(p.success_url, A.base + "/v1/boutique/merci?c=" + n1);
        assert.equal(p.cancel_url, A.base + "/v1/boutique/annule?c=" + n1);
    });

    it("une seconde commande pendant que la premiere attend : 409 en_cours, avec sa page", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande() });
        erreur(r, 409, "en_cours");
        assert.equal(r.json.numero, n1);
        assert.match(r.json.url, /^https:\/\/checkout\.stripe\.invalid\//);
    });

    it("chaque PC ne voit que ses commandes", async () => {
        const mes = await commandesDe(A, pc1);
        assert.equal(mes.length, 1);
        assert.equal(mes[0].numero, n1);
        assert.equal(mes[0].etat, "a_payer");
        assert.equal(mes[0].libelle, "Licence PROFESSIONAL - 1 an");
        assert.match(mes[0].url, /^https:\/\//);
        assert.deepEqual(await commandesDe(A, pc2), []);
        erreur(await appel(A, "POST", "/v1/boutique/annuler", { cle: pc2, corps: { numero: n1 } }), 404, "commande");
    });

    it("les deux petites pages ou Stripe renvoie le navigateur", async () => {
        const m = await appel(A, "GET", "/v1/boutique/merci?c=" + n1);
        assert.equal(m.statut, 200);
        assert.match(m.type, /^text\/html/);
        assert.ok(m.texte.includes("Paiement reçu") && m.texte.includes("Commande n° " + n1), m.texte);
        assert.ok(m.entetes.get("content-security-policy").includes("default-src 'none'"));
        const a = await appel(A, "GET", "/v1/boutique/annule?c=<script>");
        assert.equal(a.statut, 200);
        assert.ok(a.texte.includes("Paiement annulé") && !a.texte.includes("<script>"));
    });
});

describe("le webhook de Stripe", () => {
    it("400 : sans signature, une signature fausse, trop ancienne, un autre secret", async () => {
        const corps = JSON.stringify({ id: "evt_faux", type: "checkout.session.completed", data: { object: {} } });
        erreur(await appel(A, "POST", "/v1/boutique/stripe", { corps }), 400, "signature");
        erreur(await appel(A, "POST", "/v1/boutique/stripe", { corps, entetes: { "Stripe-Signature": "t=" + now() + ",v1=" + "0".repeat(64) } }), 400, "signature");
        erreur(await appel(A, "POST", "/v1/boutique/stripe", { corps, entetes: { "Stripe-Signature": signerEvenement(WEBHOOK, corps, now() - 600) } }), 400, "signature");
        erreur(await appel(A, "POST", "/v1/boutique/stripe", { corps, entetes: { "Stripe-Signature": signerEvenement("essai_jeton_autre", corps) } }), 400, "signature");
        // le corps change apres la signature
        erreur(await appel(A, "POST", "/v1/boutique/stripe", { corps: corps + " ", entetes: { "Stripe-Signature": signerEvenement(WEBHOOK, corps) } }), 400, "signature");
        assert.equal((await commandesDe(A, pc1))[0].etat, "a_payer");
        // un evenement inconnu, bien signe : 200 (Stripe ne le renvoie pas)
        const inconnu = JSON.stringify({ id: "evt_inconnu_" + hasard(4), type: "customer.created", data: { object: {} } });
        const r = await appel(A, "POST", "/v1/boutique/stripe", { corps: inconnu, entetes: { "Stripe-Signature": signerEvenement(WEBHOOK, inconnu) } });
        assert.equal(r.statut, 200, r.texte);
    });

    it("bonne signature : payee (le paiement, la facture) ; rejoue : ignore", async () => {
        const avant = await etatAdmin();
        const s = sessionDe(n1);
        const e = await evenement("checkout.session.completed", s.id, { id: "evt_paye_1" });
        assert.equal(e.statut, 200, e.corps);
        const c = (await commandesDe(A, pc1))[0];
        assert.equal(c.etat, "payee");
        assert.equal(c.recu_url, "https://invoice.stripe.invalid/i/" + s.invoice);
        assert.equal(c.url, undefined, "plus de page de paiement");
        const apres = await etatAdmin();
        assert.ok(apres.commandes > avant.commandes, "le compteur des commandes monte");
        assert.equal(apres.a_livrer, 1);
        const re = await evenement("checkout.session.completed", s.id, { id: "evt_paye_1" });
        assert.equal(re.statut, 200);
        assert.equal(JSON.parse(re.corps).deja, true);
        assert.equal((await etatAdmin()).commandes, apres.commandes, "rejoue : rien ne change");
        const b = await appel(A, "GET", "/admin/boutique", { cle: editeur });
        assert.ok(b.json.stripe.dernier_evenement > 0);
    });
});

describe("l'editeur et les admins", () => {
    it("les droits : un admin sans << demandes >> refuse ; un PC refuse", async () => {
        erreur(await appel(A, "GET", "/admin/commandes", { cle: hugo }), 403, "droit");
        erreur(await appel(A, "POST", "/admin/commandes/prendre", { cle: hugo, corps: { numero: n1 } }), 403, "droit");
        erreur(await appel(A, "GET", "/admin/commandes", { cle: pc1 }), 403, "editeur");
        erreur(await appel(A, "POST", "/admin/commandes/rembourser", { cle: lea, corps: { numero: n1 } }), 403, "droit");
    });

    it("lister : toute la commande, le scelle de la cle qui appelle", async () => {
        const r = await appel(A, "GET", "/admin/commandes", { cle: editeur });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.a_livrer, 1);
        const c = r.json.commandes.find((x) => x.numero === n1);
        assert.equal(c.etat, "payee");
        assert.equal(c.poste, pc1.poste);
        assert.equal(c.cle_pc, pc1.hex);
        assert.equal(c.cle, editeur.id);
        assert.equal(c.scelle, scelle("editeur"));
        assert.equal(c.societe, "Client d'essai");
        assert.equal(c.courriel, "camille@exemple.invalid");
        assert.equal(c.edition, "PROFESSIONAL");
        assert.equal(c.stripe_paiement, sessionDe(n1).payment_intent);
        const l = await appel(A, "GET", "/admin/commandes", { cle: lea });
        const cl = l.json.commandes.find((x) => x.numero === n1);
        assert.equal(cl.cle, lea.id);
        assert.equal(cl.scelle, scelle("lea"));
        // depuis : seulement ce qui a change apres
        const d = await appel(A, "GET", "/admin/commandes?depuis=" + (c.maj + 10), { cle: editeur });
        assert.deepEqual(d.json.commandes, []);
    });

    it("prendre, livree ; une seconde livree refusee ; un autre Admin ne prend pas une commande prise", async () => {
        let r = await appel(A, "POST", "/admin/commandes/prendre", { cle: editeur, corps: { numero: n1 } });
        assert.equal(r.statut, 200, r.texte);
        erreur(await appel(A, "POST", "/admin/commandes/prendre", { cle: lea, corps: { numero: n1 } }), 409, "prise");
        erreur(await appel(A, "POST", "/admin/commandes/livree", { cle: lea, corps: { numero: n1, licence: "XPG-2026-B0001" } }), 409, "prise");
        erreur(await appel(A, "POST", "/admin/commandes/livree", { cle: editeur, corps: { numero: n1 } }), 400, "champ");
        r = await appel(A, "POST", "/admin/commandes/livree", { cle: editeur, corps: { numero: n1, licence: "XPG-2026-B0001" } });
        assert.equal(r.statut, 200, r.texte);
        r = await appel(A, "POST", "/admin/commandes/livree", { cle: editeur, corps: { numero: n1, licence: "XPG-2026-B0002" } });
        erreur(r, 409, "etat");
        assert.equal(r.json.licence, "XPG-2026-B0001");
        erreur(await appel(A, "POST", "/admin/commandes/prendre", { cle: editeur, corps: { numero: n1 } }), 409, "etat");
        const c = (await commandesDe(A, pc1))[0];
        assert.equal(c.etat, "livree");
        assert.equal(c.licence, "XPG-2026-B0001");
        assert.equal((await etatAdmin()).a_livrer, 0);
    });
});

describe("expiree, annulee, un montant qui ne va pas, Stripe qui refuse", () => {
    it("une option, puis sa page expiree (le webhook)", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", {
            cle: pc2,
            corps: commande({ article: "opt:ai_advanced:365", licence_existante: "XPG-2026-B0001", societe: "Atelier d'essai", courriel: "lea@exemple.invalid" }),
        });
        assert.equal(r.statut, 201, r.texte);
        const e = await evenement("checkout.session.expired", sessionDe(r.json.numero).id);
        assert.equal(e.statut, 200, e.corps);
        const c = (await commandesDe(A, pc2))[0];
        assert.equal(c.etat, "expiree");
        assert.equal(c.montant, 9900);
        const l = (await appel(A, "GET", "/admin/commandes", { cle: editeur })).json.commandes.find((x) => x.numero === r.json.numero);
        assert.equal(l.option, "ai_advanced");
        assert.equal(l.jours, 365);
        assert.equal(l.licence_existante, "XPG-2026-B0001");
    });

    it("annulee par le PC (la page fermee chez Stripe) ; plus annulable ensuite qu'une fois", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc2, corps: commande() });
        assert.equal(r.statut, 201, r.texte);
        const s = sessionDe(r.json.numero);
        assert.equal((await appel(A, "POST", "/v1/boutique/annuler", { cle: pc2, corps: { numero: r.json.numero } })).statut, 200);
        assert.equal((await commandesDe(A, pc2))[0].etat, "annulee");
        const q = stripe.requetes.find((x) => x.chemin === "/v1/checkout/sessions/" + s.id + "/expire");
        assert.ok(q && q.auth === "bonne" && q.idem === "annulation-" + r.json.numero);
        assert.equal((await appel(A, "POST", "/v1/boutique/annuler", { cle: pc2, corps: { numero: r.json.numero } })).statut, 200);
        // paye quand meme (le client avait paye juste avant) : payee
        const e = await evenement("checkout.session.completed", s.id);
        assert.equal(e.statut, 200);
        assert.equal((await commandesDe(A, pc2))[0].etat, "payee");
        erreur(await appel(A, "POST", "/v1/boutique/annuler", { cle: pc2, corps: { numero: r.json.numero } }), 409, "etat");
    });

    it("un montant paye qui n'est pas celui de la commande : a_revoir, l'Admin le dit ; puis livree apres revue", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc1, corps: commande() });
        assert.equal(r.statut, 201, r.texte);
        const e = await evenement("checkout.session.completed", sessionDe(r.json.numero).id, { montant: 100 });
        assert.equal(e.statut, 200);
        const l = (await appel(A, "GET", "/admin/commandes", { cle: lea })).json.commandes.find((x) => x.numero === r.json.numero);
        assert.equal(l.etat, "a_revoir");
        assert.match(l.erreur, /montant/);
        let a = await appel(A, "POST", "/admin/commandes/a-revoir", { cle: lea, corps: { numero: r.json.numero, erreur: "Vérifié avec Stripe : à rembourser" } });
        assert.equal(a.statut, 200, a.texte);
        erreur(await appel(A, "POST", "/admin/commandes/a-revoir", { cle: lea, corps: { numero: r.json.numero } }), 400, "champ");
        a = await appel(A, "POST", "/admin/commandes/prendre", { cle: lea, corps: { numero: r.json.numero } });
        assert.equal(a.statut, 200, a.texte);
        a = await appel(A, "POST", "/admin/commandes/livree", { cle: lea, corps: { numero: r.json.numero, licence: "XPG-2026-B0003" } });
        assert.equal(a.statut, 200, a.texte);
    });

    it("rembourser (l'admin+) : demande a Stripe, puis remboursee au webhook", async () => {
        const r = await appel(A, "POST", "/admin/commandes/rembourser", { cle: editeur, corps: { numero: n1 } });
        assert.equal(r.statut, 200, r.texte);
        const pi = sessionDe(n1).payment_intent;
        assert.equal(r.json.remboursement, "re_essai_" + pi);
        const q = stripe.requetes.find((x) => x.chemin === "/v1/refunds");
        assert.ok(q && q.auth === "bonne" && q.idem === "remboursement-" + n1);
        assert.deepEqual(q.params, [["payment_intent", pi]]);
        assert.equal((await commandesDe(A, pc1)).find((x) => x.numero === n1).etat, "livree", "remboursee au webhook seulement");
        const e = await evenement("charge.refunded", sessionDe(n1).id);
        assert.equal(e.statut, 200, e.corps);
        assert.equal((await commandesDe(A, pc1)).find((x) => x.numero === n1).etat, "remboursee");
        erreur(await appel(A, "POST", "/admin/commandes/rembourser", { cle: editeur, corps: { numero: n1 } }), 409, "etat");
    });

    it("1.46.1 : le vrai Stripe d'aujourd'hui refuse payment_method_types - le faux aussi (et accepte la page du service)", async () => {
        const essai = async (version) => {
            const entetes = { Authorization: "Bearer " + CLE, "Content-Type": "application/x-www-form-urlencoded" };
            if (version) entetes["Stripe-Version"] = version;
            const corps = "mode=payment&line_items[0][price_data][currency]=eur&line_items[0][price_data][unit_amount]=100&line_items[0][price_data][product_data][name]=x&line_items[0][quantity]=1&payment_method_types[0]=card";
            const r = await fetch(stripe.base + "/v1/checkout/sessions", { method: "POST", headers: entetes, body: corps });
            return { statut: r.status, json: await r.json() };
        };
        const r = await essai("");
        assert.equal(r.statut, 400, "la version du compte (2026-09-30.endive)");
        assert.equal(r.json.error.message, MESSAGE_PMT);
        assert.equal((await essai(VERSION_STRIPE)).statut, 400);
        assert.equal((await essai("2025-09-30.clover")).statut, 200, "une version d'avant : encore accepte");
    });

    it("Stripe refuse la page : 502 stripe, sans secret, et aucune commande ne reste", async () => {
        stripe.regler("checkout");
        const avant = (await commandesDe(A, pc2)).length;
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc2, corps: commande({ societe: "Atelier d'essai" }) });
        stripe.regler("");
        erreur(r, 502, "stripe");
        assert.match(r.json.message, /Stripe refuse/);
        assert.ok(!r.texte.includes(CLE));
        assert.equal((await commandesDe(A, pc2)).length, avant);
    });
});

describe("1.46.1 : la reduction suivie, les produits refuses, un prix disparu", () => {
    const REDUIT = (fin) => ({
        ...CATALOGUE,
        articles: [
            { ...CATALOGUE.articles[0], montant: 60000, montant_reduit: 12000, fin_reduction: fin },
            { ...CATALOGUE.articles[1], libre: true },
        ],
    });
    const passerEtAnnuler = async (pc, extra = {}) => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc, corps: commande(extra) });
        assert.equal(r.statut, 201, r.texte);
        assert.equal((await appel(A, "POST", "/v1/boutique/annuler", { cle: pc, corps: { numero: r.json.numero } })).statut, 200);
        return r.json;
    };

    it("une reduction en cours : le montant reduit partout (le catalogue public, la commande, son prix Stripe)", async () => {
        const fin = heureA() + 3600;
        const r = await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: REDUIT(fin) });
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.stripe_catalogue.etat, "ok", r.texte);
        const pub = (await appel(A, "GET", "/v1/boutique")).json.articles[0];
        assert.equal(pub.montant, 12000, "les Studios 1.46.0 lisent `montant` : Acheter - 120,00 EUR TTC");
        assert.equal(pub.montant_base, 60000);
        assert.equal(pub.fin_reduction, fin);
        const adm = (await appel(A, "GET", "/admin/boutique", { cle: editeur })).json.articles;
        assert.equal(adm[0].montant, 60000);
        assert.equal(adm[0].montant_reduit, 12000);
        assert.equal(adm[0].fin_reduction, fin);
        assert.equal(adm[0].montant_actuel, 12000);
        assert.equal(adm[1].libre, true);
        // chez Stripe : le prix de base (le prix par defaut du produit) et le prix reduit, l'ancien (490,00) archive
        const pro = stripe.produits.get("xpg-ed-PROFESSIONAL");
        const actifs = [...stripe.prix.values()].filter((x) => x.product === pro.id && x.active).map((x) => x.unit_amount).sort((a, b) => a - b);
        assert.deepEqual(actifs, [12000, 60000]);
        assert.equal(stripe.prix.get(pro.default_price).unit_amount, 60000);
        assert.ok([...stripe.prix.values()].some((x) => x.product === pro.id && x.unit_amount === 49000 && !x.active), "l'ancien prix archive");
        // la commande : le prix reduit
        const c = await passerEtAnnuler(pc2, { societe: "Atelier d'essai" });
        assert.equal(c.montant, 12000);
        const q = derniere();
        sansCarteGardee(q);
        assert.equal(stripe.prix.get(parametres(q)["line_items[0][price]"]).unit_amount, 12000);
    });

    it("la fin de la reduction passee : le montant de base, sans rien faire", async () => {
        const fin = heureA() + 2;
        assert.equal((await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: REDUIT(fin) })).statut, 200);
        assert.equal((await appel(A, "GET", "/v1/boutique")).json.articles[0].montant, 12000);
        await new Promise((ok) => setTimeout(ok, 3500));
        const pub = (await appel(A, "GET", "/v1/boutique")).json.articles[0];
        assert.equal(pub.montant, 60000);
        assert.equal(pub.montant_base, undefined);
        const c = await passerEtAnnuler(pc2, { societe: "Atelier d'essai" });
        assert.equal(c.montant, 60000);
        assert.equal(stripe.prix.get(parametres(derniere())["line_items[0][price]"]).unit_amount, 60000);
        // l'enregistrement suivant archive le prix reduit (la reduction est finie)
        assert.equal((await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: REDUIT(fin) })).statut, 200);
        const pro = stripe.produits.get("xpg-ed-PROFESSIONAL");
        assert.ok(![...stripe.prix.values()].some((x) => x.product === pro.id && x.active && x.unit_amount === 12000), "le prix reduit archive");
    });

    it("une cle restreinte sans le droit des produits : le catalogue est garde, l'Admin sait pourquoi, la vente marche (price_data)", async () => {
        stripe.regler("produits");
        const corps = { ...CATALOGUE, articles: [{ ...CATALOGUE.articles[0], montant: 55500 }] };
        const r = await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps });
        stripe.regler("");
        assert.equal(r.statut, 200, r.texte);
        assert.equal(r.json.stripe_catalogue.etat, "refus");
        assert.match(r.json.stripe_catalogue.message, /Products : écriture/);
        assert.match(r.json.stripe_catalogue.message, /marche quand même/);
        assert.ok(!r.texte.includes(CLE));
        assert.equal((await appel(A, "GET", "/admin/boutique", { cle: editeur })).json.stripe_catalogue.etat, "refus");
        const c = await passerEtAnnuler(pc2, { societe: "Atelier d'essai" });
        assert.equal(c.montant, 55500);
        const p = parametres(derniere());
        sansCarteGardee(derniere());
        assert.equal(p["line_items[0][price]"], undefined);
        assert.equal(p["line_items[0][price_data][unit_amount]"], "55500");
        assert.equal(p["line_items[0][price_data][product_data][name]"], "Licence PROFESSIONAL - 1 an");
        // un article retire du catalogue : son produit archive (Stripe le permet de nouveau)
        assert.equal((await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps })).json.stripe_catalogue.etat, "ok");
        assert.equal(stripe.produits.get("xpg-opt-ai_advanced-365").active, false);
        assert.equal(stripe.produits.get("xpg-ed-PROFESSIONAL").active, true);
    });

    it("un prix disparu chez Stripe : la page se refait avec le montant donne (la commande passe)", async () => {
        const pro = stripe.produits.get("xpg-ed-PROFESSIONAL");
        const id = pro.default_price;
        assert.equal(stripe.prix.get(id).unit_amount, 55500);
        stripe.prix.delete(id);
        const avant = pages().length;
        const c = await passerEtAnnuler(pc2, { societe: "Atelier d'essai" });
        assert.equal(c.montant, 55500);
        assert.equal(pages().length, avant + 2, "le prix refuse, puis le montant donne");
        const [a, b] = pages().slice(-2);
        assert.equal(parametres(a)["line_items[0][price]"], id);
        assert.equal(parametres(b)["line_items[0][price_data][unit_amount]"], "55500");
        assert.equal(b.idem, "commande-" + c.numero + "-montant");
        sansCarteGardee(b);
        // remis comme avant pour la suite
        assert.equal((await appel(A, "PUT", "/admin/boutique", { cle: editeur, corps: CATALOGUE })).statut, 200);
    });
});

describe("1.48.0 : le panier (des lignes, payees ou demandees, validees une a une)", () => {
    const pc3 = nouvelleCle();
    const panier = (lignes, extra = {}) => ({ ...commande(), article: undefined, lignes, ...extra });
    const ligneAdmin = (corps, cle = editeur) => appel(A, "POST", "/admin/commandes/ligne", { cle, corps });
    const deAdmin = async (n) => (await appel(A, "GET", "/admin/commandes", { cle: editeur })).json.commandes.find((x) => x.numero === n);

    it("les lignes verifiees : un article deux fois, avec_ligne qui ne vise pas une licence, une option sans licence", async () => {
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier([{ article: "ed:PROFESSIONAL" }, { article: "ed:PROFESSIONAL" }]) }), 400, "champ");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier([{ article: "opt:ai_advanced:365", avec_ligne: 1 }]) }), 400, "champ");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier([{ article: "opt:ai_advanced:365" }]) }), 400, "champ");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier([{ article: "ed:PROFESSIONAL" }, { article: "ed:ENTERPRISE" }], { paiement: false }) }), 400, "champ"); // une licence par commande
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier([{ article: "ed:ENTERPRISE" }]) }), 404, "article");
        erreur(await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier(Array.from({ length: 21 }, (_, i) => ({ article: "ed:E" + i }))) }), 400, "champ");
    });

    it("un panier paye : UNE page de Stripe, une line_items par ligne ; paye : chaque ligne payee ; livree ligne par ligne", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier([{ article: "ed:PROFESSIONAL" }, { article: "opt:ai_advanced:365", avec_ligne: 1 }]) });
        assert.equal(r.statut, 201, r.texte);
        const n = r.json.numero;
        assert.equal(r.json.montant, 49000 + 9900);
        assert.equal(r.json.etat, "a_payer");
        assert.match(r.json.url, /^https:\/\//);
        assert.deepEqual(r.json.lignes.map((l) => [l.ligne, l.article, l.montant, l.etat]), [[1, "ed:PROFESSIONAL", 49000, "a_payer"], [2, "opt:ai_advanced:365", 9900, "a_payer"]]);
        assert.equal(r.json.lignes[1].avec_ligne, 1);
        const q = derniere();
        sansCarteGardee(q);
        const p = parametres(q);
        assert.ok(p["line_items[0][price]"] && p["line_items[1][price]"], "les deux prix Stripe");
        assert.equal(sessionDe(n).montant, 58900, "la page : la somme des lignes");
        // un Admin d'avant la 1.48 ne la livre pas a moitie
        assert.equal((await evenement("checkout.session.completed", sessionDe(n).id)).statut, 200);
        let c = await deAdmin(n);
        assert.equal(c.etat, "payee");
        assert.equal(c.paiement, true);
        assert.deepEqual(c.lignes.map((l) => l.etat), ["payee", "payee"]);
        erreur(await appel(A, "POST", "/admin/commandes/prendre", { cle: editeur, corps: { numero: n } }), 409, "lignes");
        erreur(await appel(A, "POST", "/admin/commandes/livree", { cle: editeur, corps: { numero: n, licence: "XPG-2026-C0001" } }), 409, "lignes");
        assert.equal((await appel(A, "POST", "/admin/commandes/prendre", { cle: editeur, corps: { numero: n, lignes: true } })).statut, 200);
        erreur(await ligneAdmin({ numero: n, ligne: 3, action: "livree", licence: "XPG-2026-C0001" }), 404, "ligne");
        let l = await ligneAdmin({ numero: n, ligne: 1, action: "livree", licence: "XPG-2026-C0001" });
        assert.equal(l.statut, 200, l.texte);
        assert.deepEqual([l.json.etat, l.json.etat_commande], ["livree", "payee"]);
        l = await ligneAdmin({ numero: n, ligne: 2, action: "livree", licence: "XPG-2026-C0001" });
        assert.deepEqual([l.json.etat, l.json.etat_commande], ["livree", "livree"]);
        erreur(await ligneAdmin({ numero: n, ligne: 2, action: "livree", licence: "XPG-2026-C0002" }), 409, "etat");
        c = (await commandesDe(A, pc3)).find((x) => x.numero === n);
        assert.equal(c.etat, "livree");
        assert.deepEqual(c.lignes.map((x) => [x.etat, x.licence]), [["livree", "XPG-2026-C0001"], ["livree", "XPG-2026-C0001"]]);
    });

    it("une demande sans paiement : la meme commande, a valider ; ni Stripe ni boutique en vente ; refusee ou livree ligne par ligne", async () => {
        const avant = stripe.requetes.length;
        const r = await appel(A, "POST", "/v1/boutique/commande", {
            cle: pc3,
            corps: panier([{ article: "ed:ENTERPRISE", libelle: "Licence ENTERPRISE - 1 an" }, { article: "opt:ai_advanced:365", licence_existante: "XPG-2026-C0001" }], { paiement: false }),
        });
        assert.equal(r.statut, 201, r.texte);
        const n = r.json.numero;
        assert.equal(r.json.etat, "a_valider");
        assert.equal(r.json.url, undefined);
        assert.equal(r.json.montant, 0);
        assert.equal(stripe.requetes.length, avant, "aucun appel a Stripe");
        let c = await deAdmin(n);
        assert.equal(c.paiement, false);
        assert.deepEqual(c.lignes.map((l) => [l.article, l.libelle, l.etat]), [["ed:ENTERPRISE", "Licence ENTERPRISE - 1 an", "a_valider"], ["opt:ai_advanced:365", "Option Assistant IA avancé - 1 an", "a_valider"]]);
        assert.equal((await appel(A, "GET", "/admin/commandes", { cle: editeur })).json.a_valider, 2);
        erreur(await appel(A, "POST", "/admin/commandes/prendre", { cle: editeur, corps: { numero: n } }), 409, "lignes");
        erreur(await ligneAdmin({ numero: n, ligne: 1, action: "refusee" }), 400, "champ");
        let l = await ligneAdmin({ numero: n, ligne: 1, action: "refusee", raison: "Pas d'offre ENTERPRISE pour ce client" }, lea);
        assert.equal(l.statut, 200, l.texte);
        assert.equal(l.json.etat_commande, "a_valider");
        l = await ligneAdmin({ numero: n, ligne: 2, action: "livree", licence: "XPG-2026-C0001" }, lea);
        assert.equal(l.json.etat_commande, "livree");
        erreur(await ligneAdmin({ numero: n, ligne: 2, action: "livree", licence: "XPG-2026-C0001" }, hugo), 403, "droit");
        c = (await commandesDe(A, pc3)).find((x) => x.numero === n);
        assert.equal(c.paiement, false);
        assert.deepEqual(c.lignes.map((x) => [x.etat, x.erreur || x.licence]), [["refusee", "Pas d'offre ENTERPRISE pour ce client"], ["livree", "XPG-2026-C0001"]]);
        // une autre demande, annulee par le PC avant d'etre validee
        const d = await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: panier([{ article: "ed:PROFESSIONAL" }], { paiement: false }) });
        assert.equal(d.statut, 201, d.texte);
        assert.equal((await appel(A, "POST", "/v1/boutique/annuler", { cle: pc3, corps: { numero: d.json.numero } })).statut, 200);
        c = (await commandesDe(A, pc3)).find((x) => x.numero === d.json.numero);
        assert.deepEqual([c.etat, c.lignes[0].etat], ["annulee", "annulee"]);
    });

    it("une commande d'avant la 1.48 (un article) : une ligne, livree par la route des lignes aussi", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc3, corps: commande() });
        assert.equal(r.statut, 201, r.texte);
        assert.equal(r.json.lignes.length, 1);
        assert.equal((await evenement("checkout.session.completed", sessionDe(r.json.numero).id)).statut, 200);
        const l = await ligneAdmin({ numero: r.json.numero, ligne: 1, action: "livree", licence: "XPG-2026-C0003" });
        assert.equal(l.json.etat_commande, "livree");
        assert.equal((await deAdmin(r.json.numero)).licence, "XPG-2026-C0003");
    });
});

describe("1.48.1 : rembourser UN article d'une commande payee", () => {
    const pc4 = nouvelleCle();
    const panier = (lignes, extra = {}) => ({ ...commande(), article: undefined, lignes, ...extra });
    const deAdmin = async (n) => (await appel(A, "GET", "/admin/commandes", { cle: editeur })).json.commandes.find((x) => x.numero === n);
    const rembourser = (corps, cle = editeur) => appel(A, "POST", "/admin/commandes/rembourser", { cle, corps });
    const remboursements = () => stripe.requetes.filter((q) => q.methode === "POST" && q.chemin === "/v1/refunds");
    let toutRembourse = 0; // une commande payee dont tous les articles sont rembourses

    it("une ligne : Stripe rembourse son montant (le meme paiement), elle seule passe remboursee ; deux fois : 409 ; toutes : la commande", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc4, corps: panier([{ article: "ed:PROFESSIONAL" }, { article: "opt:ai_advanced:365", avec_ligne: 1 }]) });
        assert.equal(r.statut, 201, r.texte);
        const n = r.json.numero;
        // pas encore payee : rien a rembourser
        erreur(await rembourser({ numero: n, ligne: 2 }), 409, "etat");
        assert.equal((await evenement("checkout.session.completed", sessionDe(n).id)).statut, 200);
        const pi = sessionDe(n).payment_intent;
        let l = await appel(A, "POST", "/admin/commandes/ligne", { cle: editeur, corps: { numero: n, ligne: 1, action: "livree", licence: "XPG-2026-D0001" } });
        assert.equal(l.statut, 200, l.texte);
        l = await appel(A, "POST", "/admin/commandes/ligne", { cle: editeur, corps: { numero: n, ligne: 2, action: "livree", licence: "XPG-2026-D0001" } });
        assert.equal(l.json.etat_commande, "livree");
        // les droits : l'admin+ seul ; une ligne qui n'existe pas
        erreur(await rembourser({ numero: n, ligne: 2 }, lea), 403, "droit");
        erreur(await rembourser({ numero: n, ligne: 3 }), 404, "ligne");
        erreur(await rembourser({ numero: n, ligne: 0 }), 400, "champ");
        const avant = remboursements().length;
        const x = await rembourser({ numero: n, ligne: 2 });
        assert.equal(x.statut, 200, x.texte);
        assert.deepEqual([x.json.ligne, x.json.etat, x.json.etat_commande, x.json.montant], [2, "remboursee", "livree", 9900]);
        assert.match(x.json.remboursement, /^re_essai_/);
        const q = remboursements()[avant];
        assert.ok(q && q.auth === "bonne" && q.idem === "remboursement-" + n + "-ligne-2", JSON.stringify(q));
        const p = parametres(q);
        assert.equal(p.payment_intent, pi, "le meme paiement");
        assert.equal(p.amount, "9900", "le montant de la ligne seulement");
        assert.equal(p["metadata[ligne]"], "2");
        let c = await deAdmin(n);
        assert.equal(c.etat, "livree", "la commande reste livree (la ligne 1 l'est)");
        assert.deepEqual(c.lignes.map((y) => [y.etat, y.licence]), [["livree", "XPG-2026-D0001"], ["remboursee", "XPG-2026-D0001"]], "la licence de la ligne reste dite (pas revoquee d'office)");
        let v = (await commandesDe(A, pc4)).find((y) => y.numero === n);
        assert.deepEqual(v.lignes.map((y) => y.etat), ["livree", "remboursee"], "le PC (Mes achats) : l'article rembourse, l'autre livre");
        // deux fois la meme ligne : 409, aucun second appel a Stripe
        const deja = remboursements().length;
        erreur(await rembourser({ numero: n, ligne: 2 }), 409, "etat");
        assert.equal(remboursements().length, deja, "jamais un second remboursement");
        // le webhook d'un remboursement partiel ne touche pas la commande
        assert.equal((await evenement("charge.refunded", sessionDe(n).id)).statut, 200);
        assert.equal((await deAdmin(n)).etat, "livree");
        // la derniere ligne : la commande remboursee (comme toute la commande)
        const y = await rembourser({ numero: n, ligne: 1 });
        assert.equal(y.statut, 200, y.texte);
        assert.equal(y.json.etat_commande, "remboursee");
        assert.equal(parametres(remboursements()[remboursements().length - 1]).amount, "49000");
        c = await deAdmin(n);
        assert.deepEqual([c.etat, ...c.lignes.map((z) => z.etat)], ["remboursee", "remboursee", "remboursee"]);
        erreur(await rembourser({ numero: n }), 409, "etat");
        erreur(await rembourser({ numero: n, ligne: 1 }), 409, "etat");
        toutRembourse = n;
    });

    it("le faux Stripe : jamais plus que ce qui reste ; toute la commande apres une ligne : le reste", async () => {
        const r = await appel(A, "POST", "/v1/boutique/commande", { cle: pc4, corps: panier([{ article: "ed:PROFESSIONAL" }, { article: "opt:ai_advanced:365", avec_ligne: 1 }]) });
        assert.equal(r.statut, 201, r.texte);
        const n = r.json.numero;
        assert.equal((await evenement("checkout.session.completed", sessionDe(n).id)).statut, 200);
        const pi = sessionDe(n).payment_intent;
        const trop = await fetch(stripe.base + "/v1/refunds", {
            method: "POST",
            headers: { Authorization: "Bearer " + CLE, "Content-Type": "application/x-www-form-urlencoded" },
            body: "payment_intent=" + pi + "&amount=99999999",
        });
        assert.equal(trop.status, 400);
        assert.equal((await trop.json()).error.code, "amount_too_large");
        // une ligne payee pas encore livree se rembourse aussi ; la commande attend l'autre
        const x = await rembourser({ numero: n, ligne: 2 });
        assert.equal(x.statut, 200, x.texte);
        assert.equal(x.json.etat_commande, "payee");
        // toute la commande ensuite (un Admin d'avant la 1.48.1) : Stripe rend le reste, la commande remboursee au webhook
        const t = await rembourser({ numero: n });
        assert.equal(t.statut, 200, t.texte);
        assert.equal(sessionDe(n).rembourse, sessionDe(n).montant, "le faux Stripe : tout est rendu, sans plus");
        erreur(await rembourser({ numero: n, ligne: 1 }), 409, "etat"); // toute la commande deja demandee
        assert.equal((await evenement("charge.refunded", sessionDe(n).id)).statut, 200);
        const c = await deAdmin(n);
        assert.deepEqual([c.etat, ...c.lignes.map((z) => z.etat)], ["remboursee", "remboursee", "remboursee"]);
    });

    it("supprimer : une commande sans argent (refusee, annulee) ; jamais une payee (409 payee) ni une qui attend (409 etat)", async () => {
        const sup = (numero, cle = editeur) => appel(A, "POST", "/admin/commandes/supprimer", { cle, corps: { numero } });
        const d = await appel(A, "POST", "/v1/boutique/commande", { cle: pc4, corps: panier([{ article: "ed:PROFESSIONAL" }], { paiement: false }) });
        assert.equal(d.statut, 201, d.texte);
        erreur(await sup(d.json.numero), 409, "etat"); // a valider : refuse d'abord
        const l = await appel(A, "POST", "/admin/commandes/ligne", { cle: editeur, corps: { numero: d.json.numero, ligne: 1, action: "refusee", raison: "Essai" } });
        assert.equal(l.json.etat_commande, "refusee");
        erreur(await sup(d.json.numero, hugo), 403, "droit");
        let x = await sup(d.json.numero, lea);
        assert.equal(x.statut, 200, x.texte);
        assert.equal(await deAdmin(d.json.numero), undefined, "partie de la liste de l'Admin");
        assert.ok(!(await commandesDe(A, pc4)).some((c) => c.numero === d.json.numero), "et de celle du PC");
        erreur(await sup(d.json.numero), 404, "commande");
        // a payer : le client paie peut-etre en ce moment
        const p = await appel(A, "POST", "/v1/boutique/commande", { cle: pc4, corps: panier([{ article: "ed:PROFESSIONAL" }]) });
        assert.equal(p.statut, 201, p.texte);
        erreur(await sup(p.json.numero), 409, "etat");
        assert.equal((await appel(A, "POST", "/v1/boutique/annuler", { cle: pc4, corps: { numero: p.json.numero } })).statut, 200);
        x = await sup(p.json.numero);
        assert.equal(x.statut, 200, x.texte);
        // payee, meme toute remboursee : elle reste pour la comptabilite
        assert.ok(toutRembourse > 0);
        erreur(await sup(toutRembourse), 409, "payee");
        assert.equal((await deAdmin(toutRembourse)).etat, "remboursee");
    });

    it("une demande sans paiement ne se rembourse pas", async () => {
        const d = await appel(A, "POST", "/v1/boutique/commande", { cle: pc4, corps: panier([{ article: "ed:PROFESSIONAL" }], { paiement: false }) });
        assert.equal(d.statut, 201, d.texte);
        erreur(await rembourser({ numero: d.json.numero, ligne: 1 }), 409, "etat");
    });
});

describe("la sauvegarde", () => {
    it("porte la boutique, les commandes, leurs scelles, les evenements - jamais les secrets de Stripe", async () => {
        const s = await appel(A, "GET", "/admin/sauvegarde", { cle: editeur });
        assert.equal(s.statut, 200, s.texte);
        assert.equal(s.json.schema, 5); // 1.47.0 : les statistiques
        const t = Object.fromEntries(s.json.tables.map((x) => [x.nom, x.lignes]));
        assert.ok(t.boutique >= 2, "le reglage et le dernier evenement");
        assert.ok(t.commandes >= 4);
        assert.ok(t.commandes_scelles >= 4);
        assert.ok(t.stripe_evenements >= 5);
        let tout = "";
        for (const nom of ["config", "boutique", "commandes", "commandes_scelles", "stripe_evenements"]) {
            const r = await appel(A, "GET", "/admin/sauvegarde?table=" + nom, { cle: editeur });
            assert.equal(r.statut, 200, r.texte);
            tout += r.texte;
        }
        assert.ok(tout.includes("Licence PROFESSIONAL - 1 an") && tout.includes("cs_test_essai"));
        assert.ok(!tout.includes(CLE) && !tout.includes(WEBHOOK), "aucun secret de Stripe dans la sauvegarde");
    });

    it("aucune reponse du service n'a porte un secret de Stripe", () => {
        for (const r of reponses) assert.ok(!r.includes(CLE) && !r.includes(WEBHOOK));
    });
});
