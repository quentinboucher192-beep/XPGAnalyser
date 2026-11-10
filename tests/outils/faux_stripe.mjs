// =============================================================================
//  tests/outils/faux_stripe.mjs - 1.46.0 : UN FAUX STRIPE SUR CE POSTE, pour les
//  essais de la boutique (service/test/boutique.test.js, les essais C++ de
//  l'Admin). JAMAIS le vrai Stripe : ses adresses de paiement et de facture sont
//  en .invalid ; aucune carte, aucun argent.
// -----------------------------------------------------------------------------
//  Ce qu'il imite (l'API de Stripe, form-urlencoded, Authorization: Bearer) :
//    POST   /v1/checkout/sessions               une page de paiement (cs_test_N)
//    POST   /v1/checkout/sessions/:id/expire    la fermer
//    POST   /v1/refunds                         un remboursement (payment_intent)
//    GET    /v1/invoices/:id                    une facture (hosted_invoice_url)
//    GET    /v1/webhook_endpoints               les points d'arrivee du webhook
//    POST   /v1/webhook_endpoints               en creer un (son secret : essai_jeton_whsec_...)
//    DELETE /v1/webhook_endpoints/:id           en effacer un
//  Une cle donnee (--cle, ou `cle`) : un autre Bearer -> 401, comme Stripe.
//  Ce que les essais lisent ou font (pas Stripe) :
//    GET  /__essai/requetes    tout ce qui a ete recu (jamais la cle : "auth" dit
//                              seulement "bonne", "fausse" ou "absente")
//    GET  /__essai/sessions    les pages de paiement creees
//    POST /__essai/regler      {"panne":"checkout"|""} : Stripe refuse les pages
//    POST /__essai/evenement   {"url","secret","type","session","montant"?,
//                              "devise"?,"id"?,"t"?} : un evenement signe comme
//                              Stripe (Stripe-Signature, HMAC-SHA256) envoye au
//                              webhook du service ; rend {statut, corps}
//
//    node faux_stripe.mjs [--port N] [--cle essai_jeton_sk_...]
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

export async function lancerFauxStripe({ port = 0, cle = "" } = {}) {
    const requetes = [];
    const sessions = new Map();
    const points = new Map();
    const idem = new Map();
    let n = 0;
    let panne = "";
    const reglage = { cle };

    const repondre = (res, statut, objet) => {
        const corps = JSON.stringify(objet);
        res.writeHead(statut, { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(corps) });
        res.end(corps);
    };
    const erreur = (res, statut, message) => repondre(res, statut, { error: { message, type: "invalid_request_error" } });

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
                            objet = { id: "ch_" + s.id, object: "charge", payment_intent: s.payment_intent, amount: s.montant, amount_refunded: s.montant, refunded: true };
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
                requetes.push({
                    methode: req.method,
                    chemin,
                    params: form.paires,
                    auth: etat,
                    idem: req.headers["idempotency-key"] || "",
                    type: req.headers["content-type"] || "",
                });
                if (etat !== "bonne") return erreur(res, 401, "Invalid API Key provided: essai_****");
                const cleIdem = req.headers["idempotency-key"] ? req.method + " " + chemin + " " + req.headers["idempotency-key"] : "";
                if (cleIdem && idem.has(cleIdem)) return repondre(res, 200, idem.get(cleIdem));
                const garder = (objet) => {
                    if (cleIdem) idem.set(cleIdem, objet);
                    return repondre(res, 200, objet);
                };
                if (req.method === "POST" && chemin === "/v1/checkout/sessions") {
                    if (panne === "checkout") return erreur(res, 400, "Essai : Stripe refuse cette page de paiement.");
                    const p = form.objet;
                    n++;
                    const id = "cs_test_essai" + n;
                    const s = {
                        id,
                        numero: Number(p["metadata[commande]"]),
                        client_reference_id: p.client_reference_id,
                        metadata: { commande: p["metadata[commande]"] },
                        montant: Number(p["line_items[0][price_data][unit_amount]"]),
                        devise: p["line_items[0][price_data][currency]"],
                        libelle: p["line_items[0][price_data][product_data][name]"],
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
                    if (!pi || ![...sessions.values()].some((s) => s.payment_intent === pi)) return erreur(res, 400, "No such payment_intent");
                    return garder({ id: "re_essai_" + pi, object: "refund", payment_intent: pi, status: "succeeded" });
                }
                m = /^\/v1\/invoices\/([^/]+)$/.exec(chemin);
                if (req.method === "GET" && m) return repondre(res, 200, { id: m[1], object: "invoice", hosted_invoice_url: "https://invoice.stripe.invalid/i/" + m[1] });
                if (req.method === "GET" && chemin === "/v1/webhook_endpoints") return repondre(res, 200, { object: "list", data: [...points.values()].map(({ secret, ...p }) => p), has_more: false });
                if (req.method === "POST" && chemin === "/v1/webhook_endpoints") {
                    const evenements = form.paires.filter(([k]) => /^enabled_events\[\d*\]$/.test(k)).map(([, v]) => v);
                    if (!form.objet.url || !evenements.length) return erreur(res, 400, "Missing required param: url / enabled_events.");
                    n++;
                    const p = { id: "we_essai" + n, object: "webhook_endpoint", url: form.objet.url, enabled_events: evenements, status: "enabled" };
                    points.set(p.id, { ...p, secret: "essai_jeton_whsec_" + crypto.randomBytes(12).toString("hex") });
                    return garder(points.get(p.id));
                }
                m = /^\/v1\/webhook_endpoints\/([^/]+)$/.exec(chemin);
                if (req.method === "DELETE" && m) {
                    if (!points.delete(decodeURIComponent(m[1]))) return erreur(res, 404, "No such webhook endpoint");
                    return repondre(res, 200, { id: m[1], object: "webhook_endpoint", deleted: true });
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
    const f = await lancerFauxStripe({ port: Number(arg("--port") || 0), cle: arg("--cle") });
    console.log("faux stripe pret sur le port " + f.base.slice(f.base.lastIndexOf(":") + 1));
    const fin = () => f.arreter().then(() => process.exit(0));
    process.on("SIGTERM", fin);
    process.on("SIGINT", fin);
}
