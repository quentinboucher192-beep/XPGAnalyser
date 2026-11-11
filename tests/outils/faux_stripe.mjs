// =============================================================================
//  tests/outils/faux_stripe.mjs - 1.46.0 : UN FAUX STRIPE SUR CE POSTE, pour les
//  essais de la boutique (service/test/boutique.test.js, les essais C++ de
//  l'Admin). JAMAIS le vrai Stripe : ses adresses de paiement et de facture sont
//  en .invalid ; aucune carte, aucun argent.
// -----------------------------------------------------------------------------
//  Ce qu'il imite (l'API de Stripe, form-urlencoded, Authorization: Bearer) :
//    1.46.1 : COMME LE VRAI STRIPE D'AUJOURD'HUI - la version de l'API est celle
//    de l'en-tete Stripe-Version, sinon celle du compte (`versionCompte`, par
//    defaut 2026-09-30.endive, comme un compte neuf) ; a partir de
//    2026-09-30.endive, une page de paiement avec payment_method_types est
//    REFUSEE (400, le texte du vrai Stripe) ; allowed_payment_method_types et
//    wallet_options sont gardes avec la session ; les produits et les prix.
//    POST   /v1/checkout/sessions               une page de paiement (cs_test_N) : line_items[0][price]
//                                               (un prix actif) ou [price_data]
//    POST   /v1/checkout/sessions/:id/expire    la fermer
//    POST   /v1/refunds                         un remboursement (payment_intent) ; 1.48.1 : amount (un remboursement
//                                               partiel ; plus que ce qui reste : 400 amount_too_large ; tout rendu :
//                                               400 charge_already_refunded), metadata[...]
//    GET    /v1/invoices/:id                    une facture (hosted_invoice_url)
//    GET    /v1/webhook_endpoints               les points d'arrivee du webhook
//    POST   /v1/webhook_endpoints               en creer un (son secret : essai_jeton_whsec_...)
//    DELETE /v1/webhook_endpoints/:id           en effacer un
//    POST   /v1/products                        1.46.1 : un produit (son id peut etre donne)
//    GET    /v1/products/:id, POST /v1/products/:id   le lire, le changer (404 resource_missing s'il n'existe pas)
//    POST   /v1/prices                          un prix (product, currency, unit_amount, tax_behavior...)
//    POST   /v1/prices/:id                      le changer (active=false : archive ; jamais le prix par
//                                               defaut d'un produit)
//  Une cle donnee (--cle, ou `cle`) : un autre Bearer -> 401, comme Stripe.
//  Ce que les essais lisent ou font (pas Stripe) :
//    GET  /__essai/requetes    tout ce qui a ete recu (jamais la cle : "auth" dit
//                              seulement "bonne", "fausse" ou "absente")
//    GET  /__essai/sessions    les pages de paiement creees
//    POST /__essai/regler      {"panne":"checkout"|"produits"|""} : Stripe refuse les
//                              pages ; 1.46.1 : << produits >> - une cle restreinte
//                              sans le droit d'ecrire les produits et les prix (403)
//    GET  /__essai/produits    1.46.1 : les produits et les prix crees
//    POST /__essai/evenement   {"url","secret","type","session","montant"?,
//                              "devise"?,"id"?,"t"?} : un evenement signe comme
//                              Stripe (Stripe-Signature, HMAC-SHA256) envoye au
//                              webhook du service ; rend {statut, corps}
//
//    node faux_stripe.mjs [--port N] [--cle essai_jeton_sk_...] [--version-compte 2026-09-30.endive]
//  ecrit << faux stripe pret sur le port N >> puis attend (SIGTERM l'arrete).
// =============================================================================
import crypto from "node:crypto";
import http from "node:http";
import { fileURLToPath } from "node:url";

// La signature d'un evenement, comme Stripe : "t=<unix>,v1=<HMAC-SHA256(secret, t + "." + corps), hexa>".
export function signerEvenement(secret, corps, t = Math.floor(Date.now() / 1000)) {
    const v1 = crypto.createHmac("sha256", secret).update(String(t) + "." + corps).digest("hex");
    return "t=" + t + ",v1=" + v1;
}

// Le corps form-urlencoded -> [[cle, valeur]] (l'ordre garde) et {cle: valeur}.
function lireFormulaire(texte) {
    const paires = [];
    for (const morceau of String(texte || "").split("&")) {
        if (!morceau) continue;
        const i = morceau.indexOf("=");
        const k = decodeURIComponent((i < 0 ? morceau : morceau.slice(0, i)).replace(/\+/g, " "));
        const v = i < 0 ? "" : decodeURIComponent(morceau.slice(i + 1).replace(/\+/g, " "));
        paires.push([k, v]);
    }
    return { paires, objet: Object.fromEntries(paires) };
}

// 1.46.1 : la version a partir de laquelle Stripe refuse payment_method_types sur une page de paiement.
export const VERSION_SANS_PMT = "2026-09-30.endive";
export const MESSAGE_PMT =
    "The `payment_method_types` parameter is no longer supported when creating a Checkout Session. Payment methods are now managed from your Dashboard settings.";
// "2026-09-30.endive" -> "2026-09-30" (les versions se comparent par leur date).
const dateDe = (v) => String(v || "").slice(0, 10);

export async function lancerFauxStripe({ port = 0, cle = "", versionCompte = VERSION_SANS_PMT } = {}) {
    const requetes = [];
    const sessions = new Map();
    const points = new Map();
    const produits = new Map();
    const prix = new Map();
    const idem = new Map();
    let n = 0;
    let panne = "";
    const reglage = { cle, versionCompte };

    const repondre = (res, statut, objet) => {
        const corps = JSON.stringify(objet);
        res.writeHead(statut, { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(corps) });
        res.end(corps);
    };
    const erreur = (res, statut, message, code, param) => {
        const e = { message, type: "invalid_request_error" };
        if (code) e.code = code;
        if (param) e.param = param;
        return repondre(res, statut, { error: e });
    };
    // Les champs d'un objet du formulaire : "metadata[k]" -> {k: v}.
    const sousObjet = (paires, nom) => {
        const o = {};
        for (const [k, v] of paires) {
            const m = new RegExp("^" + nom + "\\[([^\\]]+)\\]$").exec(k);
            if (m) o[m[1]] = v;
        }
        return o;
    };

    const serveur = http.createServer((req, res) => {
        let brut = "";
        req.on("data", (d) => (brut += d));
        req.on("end", async () => {
            try {
                const url = new URL(req.url, "http://127.0.0.1");
                const chemin = url.pathname;
                // ---- ce que les essais lisent ou font ----
                if (chemin.startsWith("/__essai/")) {
                    if (req.method === "GET" && chemin === "/__essai/requetes") return repondre(res, 200, { requetes });
                    if (req.method === "GET" && chemin === "/__essai/sessions") return repondre(res, 200, { sessions: [...sessions.values()] });
                    if (req.method === "GET" && chemin === "/__essai/produits") return repondre(res, 200, { produits: [...produits.values()], prix: [...prix.values()] });
                    if (req.method === "POST" && chemin === "/__essai/regler") {
                        const o = JSON.parse(brut || "{}");
                        panne = String(o.panne || "");
                        return repondre(res, 200, { panne });
                    }
                    if (req.method === "POST" && chemin === "/__essai/evenement") {
                        const o = JSON.parse(brut || "{}");
                        const s = sessions.get(o.session) || { id: o.session, metadata: {}, montant: 0, devise: "eur" };
                        let objet;
                        if (o.type === "charge.refunded") {
                            // 1.48.1 : ce qui a ete rembourse (un remboursement partiel : refunded faux) ; sans remboursement suivi : tout
                            const rendu = s.rembourse ? s.rembourse : s.montant;
                            objet = { id: "ch_" + s.id, object: "charge", payment_intent: s.payment_intent, amount: s.montant, amount_refunded: rendu, refunded: rendu >= s.montant };
                        } else {
                            objet = {
                                id: s.id,
                                object: "checkout.session",
                                amount_total: o.montant !== undefined ? o.montant : s.montant,
                                currency: o.devise || s.devise,
                                client_reference_id: s.client_reference_id,
                                metadata: s.metadata,
                                payment_intent: s.payment_intent,
                                invoice: s.invoice,
                                payment_status: o.type === "checkout.session.expired" ? "unpaid" : "paid",
                                status: o.type === "checkout.session.expired" ? "expired" : "complete",
                            };
                        }
                        const ev = JSON.stringify({ id: o.id || "evt_essai_" + crypto.randomBytes(8).toString("hex"), object: "event", type: o.type, data: { object: objet } });
                        const r = await fetch(o.url, {
                            method: "POST",
                            headers: { "Content-Type": "application/json", "Stripe-Signature": signerEvenement(o.secret, ev, o.t) },
                            body: ev,
                        });
                        return repondre(res, 200, { statut: r.status, corps: await r.text(), evenement: JSON.parse(ev).id });
                    }
                    return erreur(res, 404, "inconnu");
                }
                // ---- l'API de Stripe ----
                const auth = req.headers.authorization || "";
                const etat = !auth ? "absente" : reglage.cle && auth !== "Bearer " + reglage.cle ? "fausse" : "bonne";
                const form = lireFormulaire(brut);
                const version = String(req.headers["stripe-version"] || "");
                requetes.push({
                    methode: req.method,
                    chemin,
                    params: form.paires,
                    auth: etat,
                    idem: req.headers["idempotency-key"] || "",
                    type: req.headers["content-type"] || "",
                    version,
                });
                if (etat !== "bonne") return erreur(res, 401, "Invalid API Key provided: essai_****");
                const cleIdem = req.headers["idempotency-key"] ? req.method + " " + chemin + " " + req.headers["idempotency-key"] : "";
                if (cleIdem && idem.has(cleIdem)) return repondre(res, 200, idem.get(cleIdem));
                const garder = (objet) => {
                    if (cleIdem) idem.set(cleIdem, objet);
                    return repondre(res, 200, objet);
                };
                // La version de l'API de cet appel : l'en-tete, sinon celle du compte.
                const versionAppel = version || reglage.versionCompte;
                if (req.method === "POST" && chemin === "/v1/checkout/sessions") {
                    if (panne === "checkout") return erreur(res, 400, "Essai : Stripe refuse cette page de paiement.");
                    const p = form.objet;
                    // 1.46.1 : comme le vrai Stripe depuis 2026-09-30.endive
                    if (form.paires.some(([k]) => /^payment_method_types(\[|$)/.test(k)) && dateDe(versionAppel) >= dateDe(VERSION_SANS_PMT))
                        return erreur(res, 400, MESSAGE_PMT, "", "payment_method_types");
                    // 1.48.0 : plusieurs line_items (un panier) : le montant est leur somme
                    let montant = 0, devise, libelle, prixId = "";
                    const elements = [];
                    for (let i = 0; p["line_items[" + i + "][price]"] !== undefined || p["line_items[" + i + "][price_data][unit_amount]"] !== undefined; i++) {
                        const k = "line_items[" + i + "]";
                        let e;
                        if (p[k + "[price]"] !== undefined) {
                            const pr = prix.get(p[k + "[price]"]);
                            if (!pr) return erreur(res, 400, "No such price: '" + p[k + "[price]"] + "'", "resource_missing", k + "[price]");
                            if (!pr.active) return erreur(res, 400, "The price specified is inactive. This field only accepts active prices.", "", k + "[price]");
                            e = { montant: pr.unit_amount, devise: pr.currency, libelle: (produits.get(pr.product) || {}).name, prix: pr.id };
                        } else {
                            e = { montant: Number(p[k + "[price_data][unit_amount]"]), devise: p[k + "[price_data][currency]"], libelle: p[k + "[price_data][product_data][name]"], prix: "" };
                        }
                        e.quantite = Number(p[k + "[quantity]"] || 1);
                        elements.push(e);
                        montant += e.montant * e.quantite;
                    }
                    if (!elements.length) return erreur(res, 400, "Missing required param: line_items.", "parameter_missing", "line_items");
                    devise = elements[0].devise;
                    libelle = elements.map((e) => e.libelle).join(" + ");
                    prixId = elements.map((e) => e.prix).join(",");
                    n++;
                    const id = "cs_test_essai" + n;
                    const s = {
                        id,
                        numero: Number(p["metadata[commande]"]),
                        client_reference_id: p.client_reference_id,
                        metadata: sousObjet(form.paires, "metadata"),
                        montant,
                        devise,
                        libelle,
                        prix: prixId || "",
                        elements,
                        moyens: form.paires.filter(([k]) => /^allowed_payment_method_types\[\d*\]$/.test(k)).map(([, v]) => v),
                        link: p["wallet_options[link][display]"] || "auto",
                        version: versionAppel,
                        payment_intent: "pi_essai" + n,
                        invoice: "in_essai" + n,
                        expires_at: Number(p.expires_at),
                        statut: "open",
                    };
                    sessions.set(id, s);
                    return garder({
                        id,
                        object: "checkout.session",
                        url: "https://checkout.stripe.invalid/pay/" + id,
                        expires_at: s.expires_at,
                        amount_total: s.montant,
                        currency: s.devise,
                        payment_status: "unpaid",
                        status: "open",
                    });
                }
                let m = /^\/v1\/checkout\/sessions\/([^/]+)\/expire$/.exec(chemin);
                if (req.method === "POST" && m) {
                    const s = sessions.get(decodeURIComponent(m[1]));
                    if (!s) return erreur(res, 404, "No such checkout.session");
                    if (s.statut !== "open") return erreur(res, 400, "Only Checkout Sessions with a status of open can be expired.");
                    s.statut = "expired";
                    return garder({ id: s.id, object: "checkout.session", status: "expired" });
                }
                if (req.method === "POST" && chemin === "/v1/refunds") {
                    const pi = form.objet.payment_intent;
                    const s = [...sessions.values()].find((x) => x.payment_intent === pi);
                    if (!pi || !s) return erreur(res, 400, "No such payment_intent");
                    // 1.48.1 : un remboursement PARTIEL (amount, en centimes) ; jamais plus que ce qui reste (comme Stripe)
                    const deja = s.rembourse || 0;
                    const reste = s.montant - deja;
                    if (reste <= 0) return erreur(res, 400, "Charge ch_" + s.id + " has already been refunded.", "charge_already_refunded");
                    let montant = reste;
                    if (form.objet.amount !== undefined) {
                        montant = Number(form.objet.amount);
                        if (!Number.isSafeInteger(montant) || montant < 1) return erreur(res, 400, "Invalid positive integer", "", "amount");
                        if (montant > reste)
                            return erreur(res, 400, "Refund amount (" + montant + ") is greater than unrefunded amount on charge (" + reste + ")", "amount_too_large", "amount");
                    }
                    s.rembourse = deja + montant;
                    s.remboursements = (s.remboursements || 0) + 1;
                    const id = form.objet.amount === undefined ? "re_essai_" + pi : "re_essai_" + pi + "_" + s.remboursements;
                    return garder({ id, object: "refund", payment_intent: pi, amount: montant, currency: s.devise, metadata: sousObjet(form.paires, "metadata"), status: "succeeded" });
                }
                m = /^\/v1\/invoices\/([^/]+)$/.exec(chemin);
                if (req.method === "GET" && m) return repondre(res, 200, { id: m[1], object: "invoice", hosted_invoice_url: "https://invoice.stripe.invalid/i/" + m[1] });
                if (req.method === "GET" && chemin === "/v1/webhook_endpoints") return repondre(res, 200, { object: "list", data: [...points.values()].map(({ secret, ...p }) => p), has_more: false });
                if (req.method === "POST" && chemin === "/v1/webhook_endpoints") {
                    const evenements = form.paires.filter(([k]) => /^enabled_events\[\d*\]$/.test(k)).map(([, v]) => v);
                    if (!form.objet.url || !evenements.length) return erreur(res, 400, "Missing required param: url / enabled_events.");
                    if (form.objet.api_version !== undefined && !/^\d{4}-\d{2}-\d{2}(\.[a-z]+)?$/.test(form.objet.api_version))
                        return erreur(res, 400, "Invalid api_version: " + form.objet.api_version, "", "api_version");
                    n++;
                    const p = {
                        id: "we_essai" + n,
                        object: "webhook_endpoint",
                        url: form.objet.url,
                        enabled_events: evenements,
                        status: "enabled",
                        api_version: form.objet.api_version || null,
                    };
                    points.set(p.id, { ...p, secret: "essai_jeton_whsec_" + crypto.randomBytes(12).toString("hex") });
                    return garder(points.get(p.id));
                }
                m = /^\/v1\/webhook_endpoints\/([^/]+)$/.exec(chemin);
                if (req.method === "DELETE" && m) {
                    if (!points.delete(decodeURIComponent(m[1]))) return erreur(res, 404, "No such webhook endpoint");
                    return repondre(res, 200, { id: m[1], object: "webhook_endpoint", deleted: true });
                }
                // ---- 1.46.1 : les produits et les prix ----
                if (/^\/v1\/(products|prices)(\/|$)/.test(chemin) && panne === "produits")
                    return erreur(
                        res,
                        403,
                        "The provided key 'rk_test_*********essai' does not have the required permissions for this endpoint on account 'acct_essai'. Having the 'rak_product_write' permission would allow this request to continue.",
                    );
                const objetProduit = (x) => ({ object: "product", ...x });
                if (req.method === "POST" && chemin === "/v1/products") {
                    const p = form.objet;
                    if (!p.name) return erreur(res, 400, "Missing required param: name.", "parameter_missing", "name");
                    const id = p.id || "prod_essai" + ++n;
                    if (produits.has(id)) return erreur(res, 400, "Product already exists.", "resource_already_exists", "id");
                    const x = { id, name: p.name, active: p.active !== "false", metadata: sousObjet(form.paires, "metadata"), default_price: null };
                    produits.set(id, x);
                    return garder(objetProduit(x));
                }
                m = /^\/v1\/products\/([^/]+)$/.exec(chemin);
                if (m) {
                    const x = produits.get(decodeURIComponent(m[1]));
                    if (!x) return erreur(res, 404, "No such product: '" + decodeURIComponent(m[1]) + "'", "resource_missing", "id");
                    if (req.method === "GET") return repondre(res, 200, objetProduit(x));
                    if (req.method === "POST") {
                        const p = form.objet;
                        if (p.default_price !== undefined) {
                            const pr = prix.get(p.default_price);
                            if (!pr || pr.product !== x.id) return erreur(res, 400, "No such price: '" + p.default_price + "'", "resource_missing", "default_price");
                            if (!pr.active) return erreur(res, 400, "The default price must be active.", "", "default_price");
                            x.default_price = pr.id;
                        }
                        if (p.name !== undefined) x.name = p.name;
                        if (p.active !== undefined) x.active = p.active !== "false";
                        Object.assign(x.metadata, sousObjet(form.paires, "metadata"));
                        return garder(objetProduit(x));
                    }
                }
                if (req.method === "POST" && chemin === "/v1/prices") {
                    const p = form.objet;
                    if (!p.product || !produits.has(p.product)) return erreur(res, 400, "No such product: '" + (p.product || "") + "'", "resource_missing", "product");
                    if (!/^[a-z]{3}$/.test(p.currency || "")) return erreur(res, 400, "Missing required param: currency.", "parameter_missing", "currency");
                    if (!/^\d+$/.test(p.unit_amount || "")) return erreur(res, 400, "Missing required param: unit_amount.", "parameter_missing", "unit_amount");
                    const x = {
                        id: "price_essai" + ++n,
                        object: "price",
                        product: p.product,
                        currency: p.currency,
                        unit_amount: Number(p.unit_amount),
                        tax_behavior: p.tax_behavior || "unspecified",
                        nickname: p.nickname || null,
                        active: true,
                        type: "one_time",
                        metadata: sousObjet(form.paires, "metadata"),
                    };
                    prix.set(x.id, x);
                    return garder(x);
                }
                m = /^\/v1\/prices\/([^/]+)$/.exec(chemin);
                if (m) {
                    const x = prix.get(decodeURIComponent(m[1]));
                    if (!x) return erreur(res, 404, "No such price: '" + decodeURIComponent(m[1]) + "'", "resource_missing", "id");
                    if (req.method === "GET") return repondre(res, 200, x);
                    if (req.method === "POST") {
                        if (form.objet.active === "false") {
                            const pr = produits.get(x.product);
                            if (pr && pr.default_price === x.id) return erreur(res, 400, "This price cannot be archived because it is the default price of its product.", "", "active");
                            x.active = false;
                        } else if (form.objet.active === "true") x.active = true;
                        if (form.objet.nickname !== undefined) x.nickname = form.objet.nickname;
                        return garder(x);
                    }
                }
                return erreur(res, 404, "Unrecognized request URL (" + req.method + " " + chemin + ")");
            } catch (e) {
                return erreur(res, 500, "faux stripe : " + (e && e.message ? e.message : "?"));
            }
        });
    });
    await new Promise((resolve) => serveur.listen(port, "127.0.0.1", resolve));
    const base = "http://127.0.0.1:" + serveur.address().port;
    return {
        base,
        requetes,
        sessions,
        points,
        produits,
        prix,
        reglage,
        regler: (p) => (panne = p || ""),
        arreter: () => new Promise((resolve) => serveur.close(() => resolve())),
    };
}

// Lance seul (les essais C++) : << faux stripe pret sur le port N >>.
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
    const arg = (nom) => {
        const i = process.argv.indexOf(nom);
        return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : "";
    };
    const f = await lancerFauxStripe({ port: Number(arg("--port") || 0), cle: arg("--cle"), versionCompte: arg("--version-compte") || VERSION_SANS_PMT });
    console.log("faux stripe pret sur le port " + f.base.slice(f.base.lastIndexOf(":") + 1));
    const fin = () => f.arreter().then(() => process.exit(0));
    process.on("SIGTERM", fin);
    process.on("SIGINT", fin);
}
