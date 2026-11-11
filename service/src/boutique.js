// =============================================================================
//  service/src/boutique.js - 1.46.0 : LA BOUTIQUE ET LES COMMANDES (docs/BOUTIQUE.md)
//  - le catalogue de la boutique (les montants encaisses, en centimes TTC) regle
//    par l'admin+ (PUT /admin/boutique), lu par les Studios (GET /v1/boutique) ;
//    1.46.1 : le montant d'un article suit une reduction (le montant reduit
//    jusqu'a sa fin, puis le montant de base, sans rien faire) ; chaque article
//    est un PRODUIT chez Stripe, avec ses PRIX (crees par le service : il a la
//    cle) - la page de paiement emploie le prix ;
//  - les commandes des PC (POST /v1/boutique/commande) : une page de paiement
//    HEBERGEE PAR STRIPE (Checkout, mode payment, la carte seulement, sans client
//    Stripe ni carte gardee : redemandee a chaque achat) ; 1.46.1 : l'API de
//    Stripe d'aujourd'hui (Stripe-Version fixee, allowed_payment_method_types au
//    lieu de payment_method_types, Link cache) ;
//  - le webhook de Stripe (POST /v1/boutique/stripe : signe par Stripe,
//    HMAC-SHA256, compare en temps constant, 5 minutes au plus d'ecart, chaque
//    evenement traite une fois) : payee, expiree, echec, remboursee ;
//  - les commandes vues par l'editeur et les admins (droit << demandes >>) :
//    prendre, livree, a revoir ; rembourser (l'admin+ ; 1.48.1 : un article seul).
//  LES SECRETS DE STRIPE (STRIPE_SECRET_KEY, STRIPE_WEBHOOK_SECRET) sont des
//  secrets du Worker : jamais dans une table, une sauvegarde, une reponse, un
//  journal. STRIPE_API_BASE (un faux Stripe des essais) n'est ecoute que pour un
//  appel a 127.0.0.1 ou localhost (comme DECALAGE_HORLOGE_ESSAI).
// -----------------------------------------------------------------------------
//  Les routes sont des methodes du Durable Object (licences.js les ajoute a sa
//  classe : `this` est l'objet, ses tables et ses aides).
//  LES TABLES (sauvegardees : sauvegarde.js)
//    boutique           le reglage (cle "reglages" : le JSON du catalogue et son
//                       numero) et l'heure du dernier evenement de Stripe recu
//    commandes          une ligne par commande (les dates : secondes Unix)
//    commandes_scelles  la demande scellee aussi pour d'autres cles (les admins
//                       qui ont le droit << demandes >>), comme demandes_scelles
//    stripe_evenements  les evenements deja traites (leur id, 30 jours)
//  1.46.1 : la table boutique garde aussi (cle "stripe_catalogue") ce que le
//  service a cree chez Stripe : le produit et les prix de chaque article (leurs
//  identifiants, jamais un secret) et le resultat de la derniere mise a jour.
// =============================================================================
import { Erreur, cleIdDe, egalConstant, enc, entier, estBase64, estHex, hex, mal, reponseJson } from "./outils.js";
import { estIdLicence } from "./documents.js";

const JOUR = 86400;
export const DUREE_PAIEMENT = 30 * 60 + 60; // la page de paiement : 30 minutes (le minimum de Stripe), et une minute de marge
const TOLERANCE_WEBHOOK = 300; // 5 minutes
export const MAX_CORPS_WEBHOOK = 256 * 1024;
const GARDE_EVENEMENTS = 30 * JOUR;
const GARDE_CLOSES = 30 * JOUR; // une commande expiree, annulee, refusee : effacee apres 30 jours
const GARDE_SCELLES = 7 * JOUR; // le scelle d'une commande livree ou remboursee : efface apres 7 jours
const PRISE = 300; // une commande prise par un Admin l'est pour 5 minutes
const MAX_ARTICLES = 200;
const MAX_COMMANDES = 100000;
const MAX_A_PAYER = 1000;
const MAX_OCTETS_COMMANDES = 50 * 1024 * 1024;
const MAX_SCELLE = 64 * 1024;
const MAX_SCELLES = 33;
const MONTANT_MIN = 50; // 0,50 EUR
const MONTANT_MAX = 100000000; // 1 000 000 EUR
const LISTE_PC = 50;
const LISTE_ADMIN = 500;
const DELAI_STRIPE = 15000; // ms
const FIN_MAX = 4102444800; // 2100-01-01 : la fin d'une reduction (secondes Unix)
const MAX_APPELS_CATALOGUE = 40; // les appels a Stripe d'une mise a jour du catalogue (Cloudflare : 50 sous-requetes)

// 1.46.1 : LA VERSION DE L'API DE STRIPE, fixee sur chaque appel (l'en-tete Stripe-Version) et sur le point d'arrivee
// du webhook (api_version, cree par l'Admin : Brancher Stripe). Sans elle, Stripe applique la version du compte : un
// compte neuf est en 2026-09-30.endive ou plus, qui REFUSE payment_method_types sur une page de paiement (400). Les
// champs lus ici (la session : id, url, payment_status, amount_total, currency, payment_intent, invoice, metadata,
// client_reference_id ; le paiement rembourse : payment_intent, refunded, amount, amount_refunded ; la facture :
// hosted_invoice_url) sont les memes dans cette version. La meme valeur : shop::kStripeVersion (l'Admin).
export const VERSION_STRIPE = "2026-09-30.endive";

// Les evenements que le point d'arrivee du webhook recoit (l'Admin les demande a Stripe : Brancher Stripe...).
export const EVENEMENTS_STRIPE = [
    "checkout.session.completed",
    "checkout.session.async_payment_succeeded",
    "checkout.session.async_payment_failed",
    "checkout.session.expired",
    "charge.refunded",
];

// Les etats d'une commande (docs/BOUTIQUE.md, par. 4).
export const ETATS_COMMANDE = ["a_payer", "payee", "livree", "expiree", "annulee", "echec_paiement", "remboursee", "a_revoir", "a_valider", "refusee"];
// 1.48.0 : le panier (docs/BOUTIQUE.md, par. 10) - les lignes d'une commande, et ce qui est fini pour une ligne.
const MAX_LIGNES = 20;
const LIGNE_FINIE = ["livree", "refusee", "remboursee", "expiree", "annulee", "echec_paiement"];

export const TABLES_BOUTIQUE_SQL = `
    CREATE TABLE IF NOT EXISTS boutique (cle TEXT PRIMARY KEY, valeur TEXT NOT NULL) WITHOUT ROWID;
    CREATE TABLE IF NOT EXISTS commandes (
        numero INTEGER PRIMARY KEY, poste TEXT NOT NULL, cle_pc TEXT NOT NULL, article TEXT NOT NULL, genre TEXT NOT NULL,
        edition TEXT NOT NULL, option TEXT NOT NULL, jours INTEGER NOT NULL, montant INTEGER NOT NULL, devise TEXT NOT NULL,
        libelle TEXT NOT NULL, societe TEXT NOT NULL, nom TEXT NOT NULL, courriel TEXT NOT NULL, licence_existante TEXT NOT NULL,
        scelle_cle TEXT NOT NULL, scelle TEXT NOT NULL, octets INTEGER NOT NULL, etat TEXT NOT NULL, mode TEXT NOT NULL,
        cree INTEGER NOT NULL, maj INTEGER NOT NULL, expire INTEGER NOT NULL, paye INTEGER NOT NULL,
        stripe_session TEXT NOT NULL, stripe_url TEXT NOT NULL, stripe_paiement TEXT NOT NULL, stripe_remboursement TEXT NOT NULL,
        recu_url TEXT NOT NULL, licence TEXT NOT NULL, erreur TEXT NOT NULL, prise INTEGER NOT NULL, prise_par TEXT NOT NULL);
    CREATE INDEX IF NOT EXISTS commandes_poste ON commandes (poste, numero);
    CREATE INDEX IF NOT EXISTS commandes_etat ON commandes (etat);
    CREATE INDEX IF NOT EXISTS commandes_maj ON commandes (maj);
    CREATE INDEX IF NOT EXISTS commandes_paiement ON commandes (stripe_paiement);
    CREATE TABLE IF NOT EXISTS commandes_scelles (numero INTEGER NOT NULL, cle TEXT NOT NULL, scelle TEXT NOT NULL, PRIMARY KEY (numero, cle)) WITHOUT ROWID;
    CREATE TABLE IF NOT EXISTS stripe_evenements (id TEXT PRIMARY KEY, recu INTEGER NOT NULL, type TEXT NOT NULL) WITHOUT ROWID;
    CREATE TABLE IF NOT EXISTS commande_lignes (
        numero INTEGER NOT NULL, ligne INTEGER NOT NULL, article TEXT NOT NULL, genre TEXT NOT NULL, edition TEXT NOT NULL,
        option TEXT NOT NULL, jours INTEGER NOT NULL, montant INTEGER NOT NULL, libelle TEXT NOT NULL, licence_existante TEXT NOT NULL,
        avec_ligne INTEGER NOT NULL, etat TEXT NOT NULL, licence TEXT NOT NULL, erreur TEXT NOT NULL, maj INTEGER NOT NULL,
        PRIMARY KEY (numero, ligne)) WITHOUT ROWID;
`;

// Les tables de la boutique dans la sauvegarde (sauvegarde.js) : jamais les secrets de Stripe (ils ne sont pas en table).
export const TABLES_SAUVEGARDE_BOUTIQUE = [
    ["boutique", ["cle"]],
    ["commandes", ["numero"]],
    ["commandes_scelles", ["numero", "cle"]],
    ["stripe_evenements", ["id"]],
    ["commande_lignes", ["numero", "ligne"]], // 1.48.0 : les lignes d'une commande (le panier)
];

// Un texte d'une ligne, sans caractere de controle, entre min et max caracteres.
function texteLigne(v, min, max) {
    if (typeof v !== "string" || /[\u0000-\u001f\u007f]/.test(v)) return false;
    const n = Array.from(v).length;
    return n >= min && n <= max;
}

// Un courriel lisible (comme gateway::plausibleEmail) : un seul @, un domaine avec un point, 254 caracteres au plus.
function courrielLisible(s) {
    if (typeof s !== "string" || s.length > 254 || /[\s\u0000-\u001f\u007f<>"]/.test(s)) return false;
    const at = s.indexOf("@");
    if (at < 1 || at !== s.lastIndexOf("@")) return false;
    const domaine = s.slice(at + 1);
    return domaine.length >= 3 && domaine.includes(".") && !domaine.startsWith(".") && !domaine.endsWith(".");
}

const estEdition = (s) => typeof s === "string" && /^[A-Z][A-Z0-9_]{0,31}$/.test(s);
const estOption = (s) => typeof s === "string" && /^[a-z0-9_]{1,40}$/.test(s);

// Le mode de Stripe d'apres le prefixe de la cle : sk_live_ / rk_live_ -> "reel", toute autre (sk_test_, rk_test_, et
// les valeurs des essais, essai_jeton_...) -> "test" ; pas de cle : null.
export function modeStripe(cle) {
    if (typeof cle !== "string" || !cle) return null;
    return /^(sk|rk)_live_/.test(cle) ? "reel" : "test";
}

// 1.46.1 : le montant d'un article a l'heure `now` : le montant reduit tant que la reduction court (sa fin pas encore
// passee, comme le prix reduit du registre dans les Studios), sinon le montant de base.
export function montantActuel(a, now) {
    return reductionEnCours(a, now) ? a.montant_reduit : a.montant;
}
export function reductionEnCours(a, now) {
    return Number.isSafeInteger(a.montant_reduit) && Number.isSafeInteger(a.fin_reduction) && now < a.fin_reduction;
}

// 1.46.1 : l'identifiant du produit Stripe d'un article (choisi par le service : on le retrouve sans rien chercher) -
// "ed:PROFESSIONAL" -> "xpg-ed-PROFESSIONAL", "opt:ai_advanced:365" -> "xpg-opt-ai_advanced-365".
export function produitStripe(id) {
    return "xpg-" + String(id).replace(/:/g, "-");
}

// Un texte rendu par Stripe, sans rien qui ressemble a une cle (on ne sait jamais) : 300 caracteres au plus.
function propre(t) {
    return String(t || "")
        .replace(/\b(sk|rk|pk)_(live|test)_[A-Za-z0-9]+/g, "[masqué]")
        .replace(/\bwhsec_[A-Za-z0-9]+/g, "[masqué]")
        .replace(/\bessai_jeton_[A-Za-z0-9_]+/g, "[masqué]")
        .replace(/[\u0000-\u001f\u007f]/g, " ")
        .slice(0, 300);
}

// Le corps application/x-www-form-urlencoded d'un appel a Stripe (l'ordre des cles garde).
function formulaire(params) {
    return params.map(([k, v]) => encodeURIComponent(k) + "=" + encodeURIComponent(String(v))).join("&");
}

// L'en-tete Stripe-Signature : "t=<unix>,v1=<hexa>[,v1=...]" -> {t, v1: [...]} ; null : illisible.
export function lireSignatureStripe(entete) {
    if (typeof entete !== "string" || entete.length > 4096) return null;
    let t = null;
    const v1 = [];
    for (const morceau of entete.split(",")) {
        const i = morceau.indexOf("=");
        if (i < 0) continue;
        const k = morceau.slice(0, i).trim();
        const v = morceau.slice(i + 1).trim();
        if (k === "t" && /^\d{1,12}$/.test(v)) t = Number(v);
        else if (k === "v1" && estHex(v.toLowerCase(), 64)) v1.push(v.toLowerCase());
    }
    return t === null || !v1.length ? null : { t, v1 };
}

async function hmacHex(secret, octets) {
    const k = await crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
    return hex(new Uint8Array(await crypto.subtle.sign("HMAC", k, octets)));
}

// Le montant lisible : 49000 -> "490,00 €" (la devise eur ; une autre : son code).
function montantTexte(c, devise) {
    const e = Math.floor(c / 100);
    const s = String(e).replace(/\B(?=(\d{3})+(?!\d))/g, " ") + "," + String(c % 100).padStart(2, "0");
    return devise === "eur" ? s + " €" : s + " " + devise.toUpperCase();
}

const PAGE_STYLE =
    "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif;background:#f4f5fb;color:#1e2235;margin:0}" +
    "main{max-width:520px;margin:12vh auto;background:#fff;border:1px solid #e3e6f0;border-radius:14px;padding:32px 36px}" +
    "h1{font-size:22px;margin:0 0 12px}p{line-height:1.5;margin:8px 0;color:#454b63}.n{color:#7b8199;font-size:14px}";

function pageHtml(titre, lignes, numero) {
    const echap = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    const corps =
        "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" +
        "<title>" + echap(titre) + "</title><style>" + PAGE_STYLE + "</style></head><body><main><h1>" + echap(titre) + "</h1>" +
        lignes.map((l) => "<p>" + echap(l) + "</p>").join("") +
        (numero ? "<p class=\"n\">Commande n° " + numero + "</p>" : "") +
        "</main></body></html>";
    return new Response(corps, {
        status: 200,
        headers: {
            "Content-Type": "text/html; charset=utf-8",
            "Cache-Control": "no-store",
            "X-Content-Type-Options": "nosniff",
            "Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'",
            "Referrer-Policy": "no-referrer",
        },
    });
}

// Les methodes ajoutees au Durable Object (licences.js).
export const RoutesBoutique = {
    // ============================================================ le reglage ==
    reglagesBoutique() {
        const r = this.ligne("SELECT valeur FROM boutique WHERE cle = 'reglages'");
        let v = null;
        try {
            v = r ? JSON.parse(r.valeur) : null;
        } catch {
            v = null;
        }
        if (!v || typeof v !== "object") v = { active: false, livraison_auto: true, devise: "eur", articles: [], numero: 0 };
        return v;
    },

    // L'etat de Stripe vu par le service (jamais la cle) : branche (la cle secrete est la), le mode, le webhook (son secret
    // est la), le dernier evenement recu (seconde Unix, ou null).
    etatStripe() {
        const cle = this.env.STRIPE_SECRET_KEY;
        const r = this.ligne("SELECT valeur FROM boutique WHERE cle = 'dernier_evenement'");
        return {
            branche: typeof cle === "string" && cle.length > 0,
            mode: modeStripe(cle),
            webhook: typeof this.env.STRIPE_WEBHOOK_SECRET === "string" && this.env.STRIPE_WEBHOOK_SECRET.length > 0,
            dernier_evenement: r ? Number(r.valeur) || null : null,
        };
    },

    // La boutique est-elle ouverte (le reglage actif, Stripe branche et son webhook, au moins un article) ?
    boutiqueOuverte() {
        const s = this.etatStripe();
        const r = this.reglagesBoutique();
        return !!(r.active && s.branche && s.webhook && r.articles.length);
    },

    // Un changement d'une commande : le compteur de GET /admin/etat (<< commandes >>).
    signalerCommande() {
        return this.suivant("commandes_changements");
    },

    // PUT /admin/boutique (admin+) : le catalogue entier ; son numero monte.
    async deposerBoutique(ctx) {
        const corps = this.corpsJson(ctx);
        if (typeof corps.active !== "boolean") throw mal("active");
        if (corps.livraison_auto !== undefined && typeof corps.livraison_auto !== "boolean") throw mal("livraison_auto");
        const devise = corps.devise === undefined ? "eur" : corps.devise;
        if (typeof devise !== "string" || !/^[a-z]{3}$/.test(devise)) throw mal("devise", "Le champ « devise » doit être un code de devise en 3 lettres minuscules (« eur »).");
        if (!Array.isArray(corps.articles) || corps.articles.length > MAX_ARTICLES)
            throw mal("articles", "Le champ « articles » doit être une liste (" + MAX_ARTICLES + " articles au plus).");
        const vus = new Set();
        const articles = corps.articles.map((a, i) => {
            const quoi = "L'article n° " + (i + 1);
            if (!a || typeof a !== "object" || Array.isArray(a)) throw mal("articles", quoi + " n'est pas un objet.");
            let id;
            const o = { id: "", genre: a.genre };
            if (a.genre === "edition") {
                if (!estEdition(a.edition)) throw mal("articles", quoi + " : « edition » doit être l'identifiant d'une offre du registre (PROFESSIONAL).");
                id = "ed:" + a.edition;
                o.edition = a.edition;
            } else if (a.genre === "option") {
                if (!estOption(a.option)) throw mal("articles", quoi + " : « option » doit être l'identifiant d'une option du registre.");
                if (!entier(a.jours, 0, 3660)) throw mal("articles", quoi + " : « jours » doit être la durée de l'option en jours (0 : jusqu'à la fin de la licence).");
                id = "opt:" + a.option + ":" + a.jours;
                o.option = a.option;
                o.jours = a.jours;
            } else {
                throw mal("articles", quoi + " : « genre » vaut « edition » ou « option ».");
            }
            if (a.id !== undefined && a.id !== id) throw mal("articles", quoi + " : son identifiant doit être « " + id + " ».");
            if (vus.has(id)) throw mal("articles", quoi + " : « " + id + " » est déjà dans le catalogue.");
            vus.add(id);
            o.id = id;
            if (!entier(a.montant, MONTANT_MIN, MONTANT_MAX))
                throw mal("articles", quoi + " : « montant » doit être en centimes TTC, de " + MONTANT_MIN + " (0,50 €) à " + MONTANT_MAX + " (1 000 000 €).");
            o.montant = a.montant;
            if (!texteLigne(a.libelle, 1, 120)) throw mal("articles", quoi + " : « libelle » (le nom sur la page de paiement et la facture) : une ligne, 120 caractères au plus.");
            o.libelle = a.libelle;
            // 1.46.1 : un montant libre (tape par l'editeur, il ne suit pas le prix affiche du registre) ; une reduction (le
            // montant reduit jusqu'a sa fin, puis le montant de base - le service choisit a chaque commande).
            if (a.libre !== undefined && typeof a.libre !== "boolean") throw mal("articles", quoi + " : « libre » vaut true ou false.");
            if (a.libre !== undefined) o.libre = a.libre; // garde tel quel : un reglage d'avant la 1.46.1 n'en a pas
            if (a.montant_reduit !== undefined || a.fin_reduction !== undefined) {
                if (!entier(a.montant_reduit, MONTANT_MIN, MONTANT_MAX))
                    throw mal("articles", quoi + " : « montant_reduit » doit être en centimes TTC, de " + MONTANT_MIN + " (0,50 €) à " + MONTANT_MAX + " (1 000 000 €).");
                if (!entier(a.fin_reduction, 1, FIN_MAX)) throw mal("articles", quoi + " : « fin_reduction » doit être la fin de la réduction (secondes Unix).");
                o.montant_reduit = a.montant_reduit;
                o.fin_reduction = a.fin_reduction;
            }
            return o;
        });
        const avant = this.reglagesBoutique();
        const numero = (Number(avant.numero) || 0) + 1;
        const r = { active: corps.active, livraison_auto: corps.livraison_auto === undefined ? true : corps.livraison_auto, devise, articles, numero };
        this.x("INSERT INTO boutique (cle, valeur) VALUES ('reglages', ?) ON CONFLICT (cle) DO UPDATE SET valeur = excluded.valeur", JSON.stringify(r));
        this.signalerChangement(); // GET /v1/etat << boutique >> peut changer
        // 1.46.1 : les produits et les prix chez Stripe (Stripe branche) ; un refus de Stripe n'empeche rien (la page de
        // paiement donne alors le montant elle-meme) : il est rendu et garde pour l'Admin.
        const stripe_catalogue = await this.synchroniserStripe(ctx);
        return reponseJson(200, { numero, stripe_catalogue });
    },

    // 1.46.1 : POST /admin/boutique/stripe (admin+) - les produits et les prix chez Stripe mis a jour tout de suite (l'Admin
    // l'appelle apres Brancher Stripe : une cle neuve, un autre compte peut-etre).
    async catalogueStripe(ctx) {
        return reponseJson(200, { stripe_catalogue: await this.synchroniserStripe(ctx) });
    },

    // GET /admin/boutique (admin+, admin) : le reglage, et l'etat de Stripe (jamais la cle). 1.46.1 : chaque article dit
    // son montant du moment (`montant_actuel`) et son produit Stripe (`stripe_produit`, s'il est cree) ;
    // `stripe_catalogue` : la derniere mise a jour des produits chez Stripe.
    async lireBoutiqueAdmin(ctx) {
        const r = this.reglagesBoutique();
        const c = this.catalogueStripeGarde();
        const mode = modeStripe(this.env.STRIPE_SECRET_KEY);
        const articles = r.articles.map((a) => {
            const o = { ...a, montant_actuel: montantActuel(a, ctx.now) };
            const s = c.mode === mode && c.articles[a.id];
            if (s && s.produit) o.stripe_produit = s.produit;
            return o;
        });
        return reponseJson(200, { ...r, articles, stripe: this.etatStripe(), ouverte: this.boutiqueOuverte(), stripe_catalogue: c.etat || null });
    },

    // GET /v1/boutique (publique) : ce qui s'achete. Stripe pas branche : fermee, aucun article. 1.46.1 : `montant` est
    // le montant du moment (le reduit tant que la reduction court : les Studios 1.46.0 le montrent tel quel, << Acheter -
    // 12,00 EUR TTC >>) ; une reduction en cours dit aussi `montant_base` et `fin_reduction`.
    async lireBoutiquePublique(ctx) {
        const r = this.reglagesBoutique();
        const s = this.etatStripe();
        if (!this.boutiqueOuverte()) return reponseJson(200, { active: false, devise: r.devise || "eur", mode: s.mode, articles: [] });
        const articles = r.articles.map((a) => {
            const o = { id: a.id, genre: a.genre };
            if (a.genre === "edition") o.edition = a.edition;
            else {
                o.option = a.option;
                o.jours = a.jours;
            }
            o.montant = montantActuel(a, ctx.now);
            o.libelle = a.libelle;
            if (reductionEnCours(a, ctx.now)) {
                o.montant_base = a.montant;
                o.fin_reduction = a.fin_reduction;
            }
            return o;
        });
        return reponseJson(200, { active: true, devise: r.devise, mode: s.mode, articles });
    },

    // ============================================================ les produits chez Stripe (1.46.1) ==
    // Ce que le service a cree chez Stripe : {mode, articles: {<id>: {produit, nom, defaut, prix: {"eur:6000": "price_..."}}},
    // etat: {etat, message, quand, produits}} ; un autre mode (une cle de test, puis une reelle) : tout est a refaire.
    catalogueStripeGarde() {
        const r = this.ligne("SELECT valeur FROM boutique WHERE cle = 'stripe_catalogue'");
        let v = null;
        try {
            v = r ? JSON.parse(r.valeur) : null;
        } catch {
            v = null;
        }
        if (!v || typeof v !== "object" || !v.articles || typeof v.articles !== "object") v = { mode: null, articles: {}, etat: null };
        return v;
    },
    garderCatalogueStripe(c) {
        this.x("INSERT INTO boutique (cle, valeur) VALUES ('stripe_catalogue', ?) ON CONFLICT (cle) DO UPDATE SET valeur = excluded.valeur", JSON.stringify(c));
    },

    // Les produits et les prix chez Stripe, d'apres le catalogue : un PRODUIT par article (son identifiant : produitStripe,
    // son nom : le libelle, metadata[xpg_article]), un PRIX par montant (celui de base ; le reduit tant que la reduction
    // court) - un prix ne change jamais chez Stripe : un montant neuf, un prix neuf, l'ancien archive ; le prix de base est
    // le prix par defaut du produit. Un article retire : son produit archive. Des cles d'idempotence partout (un appel
    // repete ne cree rien deux fois). Rend {etat: ok|partiel|refus|panne|pas_branche, message, quand, produits}.
    async synchroniserStripe(ctx) {
        const cle = this.env.STRIPE_SECRET_KEY;
        let c = this.catalogueStripeGarde();
        const fini = (etat, message, produits) => {
            c.etat = { etat, message: message || "", quand: ctx.now, produits: produits || 0 };
            this.garderCatalogueStripe(c);
            return c.etat;
        };
        if (!cle) return fini("pas_branche", "Stripe n'est pas branché sur ce service : les produits se créeront une fois branché.");
        if (this.catalogueEnCours) return { etat: "en_cours", message: "Une mise à jour des produits chez Stripe est déjà en cours.", quand: ctx.now, produits: 0 };
        this.catalogueEnCours = true;
        try {
            const r = this.reglagesBoutique();
            const mode = modeStripe(cle);
            if (c.mode !== mode) c = { mode, articles: {}, etat: null };
            let appels = 0;
            const appel = async (methode, chemin, params, idem, quoi) => {
                appels++;
                return (await this.appelStripe(ctx, methode, chemin, params, idem, quoi)).json;
            };
            const absent = (e) => e instanceof Erreur && e.extra && e.extra.stripe_statut === 404;
            const voulusIds = new Set(r.articles.map((a) => a.id));
            let partiel = false;
            let faits = 0;
            try {
                for (const a of r.articles) {
                    if (appels + 5 > MAX_APPELS_CATALOGUE) {
                        partiel = true;
                        break;
                    }
                    const pid = produitStripe(a.id);
                    let s = c.articles[a.id];
                    // 1. le produit : mis a jour s'il existe deja chez Stripe (une sauvegarde restauree, un autre service),
                    //    cree sinon
                    if (!s || s.produit !== pid) {
                        const p = [
                            ["name", a.libelle],
                            ["active", "true"],
                            ["metadata[xpg_article]", a.id],
                        ];
                        try {
                            await appel("POST", "/v1/products/" + encodeURIComponent(pid), p, "", "mettre à jour le produit « " + a.libelle + " »");
                        } catch (e) {
                            if (!absent(e)) throw e;
                            await appel("POST", "/v1/products", [["id", pid], ...p], "xpg-produit-" + pid + "-" + r.numero, "créer le produit « " + a.libelle + " »");
                        }
                        s = c.articles[a.id] = { produit: pid, nom: a.libelle, defaut: "", prix: {} };
                    }
                    // 2. les prix : le montant de base, et le reduit tant que la reduction court
                    const voulus = [[a.montant, false]];
                    if (reductionEnCours(a, ctx.now) && a.montant_reduit !== a.montant) voulus.push([a.montant_reduit, true]);
                    const cles = new Set();
                    for (const [m, reduit] of voulus) {
                        const k = r.devise + ":" + m;
                        cles.add(k);
                        if (s.prix[k]) continue;
                        const nom = montantTexte(m, r.devise) + " TTC" + (reduit ? " (réduction)" : "");
                        const prix = await appel(
                            "POST",
                            "/v1/prices",
                            [
                                ["product", pid],
                                ["currency", r.devise],
                                ["unit_amount", m],
                                ["tax_behavior", "inclusive"],
                                ["nickname", nom],
                                ["metadata[xpg_article]", a.id],
                            ],
                            "xpg-prix-" + pid + "-" + k + "-" + r.numero,
                            "créer le prix " + nom + " de « " + a.libelle + " »",
                        );
                        if (typeof prix.id !== "string" || !prix.id || prix.id.length > 255) throw new Erreur(502, "stripe", "Stripe a rendu un prix illisible.");
                        s.prix[k] = prix.id;
                        this.garderCatalogueStripe(c);
                    }
                    // 3. le produit : son nom (le libelle a change), son prix par defaut (le montant de base)
                    const defaut = s.prix[r.devise + ":" + a.montant];
                    if (s.nom !== a.libelle || s.defaut !== defaut) {
                        await appel(
                            "POST",
                            "/v1/products/" + encodeURIComponent(pid),
                            [
                                ["name", a.libelle],
                                ["active", "true"],
                                ["default_price", defaut],
                            ],
                            "",
                            "mettre à jour le produit « " + a.libelle + " »",
                        );
                        s.nom = a.libelle;
                        s.defaut = defaut;
                    }
                    // 4. les prix qui ne servent plus (un ancien montant, une reduction finie) : archives (jamais effaces chez
                    //    Stripe) - apres le nouveau prix par defaut (Stripe n'archive pas le prix par defaut d'un produit)
                    for (const [k, id] of Object.entries(s.prix)) {
                        if (cles.has(k)) continue;
                        try {
                            await appel("POST", "/v1/prices/" + encodeURIComponent(id), [["active", "false"]], "", "archiver l'ancien prix de « " + a.libelle + " »");
                        } catch (e) {
                            if (!absent(e)) throw e;
                        }
                        delete s.prix[k];
                    }
                    this.garderCatalogueStripe(c);
                    faits++;
                }
                // 5. les articles retires du catalogue : leur produit archive
                for (const [id, s] of Object.entries(c.articles)) {
                    if (voulusIds.has(id)) continue;
                    if (partiel || appels + 1 > MAX_APPELS_CATALOGUE) {
                        partiel = true;
                        break;
                    }
                    try {
                        await appel("POST", "/v1/products/" + encodeURIComponent(s.produit), [["active", "false"]], "", "archiver le produit « " + (s.nom || id) + " »");
                    } catch (e) {
                        if (!absent(e)) throw e;
                    }
                    delete c.articles[id];
                    this.garderCatalogueStripe(c);
                }
            } catch (e) {
                if (!(e instanceof Erreur)) throw e;
                const refus = !!(e.extra && e.extra.stripe_statut);
                let m = e.message;
                if (e.extra && (e.extra.stripe_statut === 401 || e.extra.stripe_statut === 403))
                    m += " — Ta clé restreinte doit avoir « Products : écriture » et « Prices : écriture » (dashboard.stripe.com › Développeurs › Clés API).";
                return fini(refus ? "refus" : "panne", m + " La vente en ligne marche quand même : la page de paiement donne le montant elle-même.", faits);
            }
            if (partiel)
                return fini("partiel", faits + " produit(s) à jour chez Stripe ; les autres au prochain enregistrement (Stripe ne prend que " + MAX_APPELS_CATALOGUE + " appels à la fois).", faits);
            return fini("ok", faits + " produit(s) à jour chez Stripe.", faits);
        } finally {
            this.catalogueEnCours = false;
        }
    },

    // Le prix Stripe du montant d'un article (cree par synchroniserStripe pour ce mode) ; "" : aucun (la page de paiement
    // donne alors le montant elle-meme : price_data).
    prixStripe(art, devise, montant) {
        const c = this.catalogueStripeGarde();
        if (c.mode !== modeStripe(this.env.STRIPE_SECRET_KEY)) return "";
        const s = c.articles[art.id];
        const id = s && s.prix ? s.prix[devise + ":" + montant] : "";
        return typeof id === "string" ? id : "";
    },
    oublierPrixStripe(art, devise, montant) {
        const c = this.catalogueStripeGarde();
        const s = c.articles[art.id];
        if (s && s.prix) {
            delete s.prix[devise + ":" + montant];
            if (s.defaut && !Object.values(s.prix).includes(s.defaut)) s.defaut = "";
            this.garderCatalogueStripe(c);
        }
    },

    // ============================================================ Stripe ==
    // Un appel a l'API de Stripe (form-urlencoded, la cle secrete en Bearer, une cle d'idempotence). {statut, json} ;
    // une panne : Erreur 502 << stripe >> (le texte de Stripe, sans secret).
    async appelStripe(ctx, methode, chemin, params, idem, quoi) {
        const cle = this.env.STRIPE_SECRET_KEY;
        if (!cle) throw new Erreur(409, "boutique", "Stripe n'est pas branché sur ce service (Admin › Boutique › Brancher Stripe…).");
        let base = "https://api.stripe.com";
        // Un faux Stripe (les essais) : SEULEMENT pour un appel a ce poste (127.0.0.1 ou localhost), et vers ce poste.
        const essai = String(this.env.STRIPE_API_BASE || "");
        if (ctx.local && /^http:\/\/(127\.0\.0\.1|localhost)(:\d{1,5})?$/.test(essai)) base = essai;
        const entetes = { Authorization: "Bearer " + cle, "Content-Type": "application/x-www-form-urlencoded", "Stripe-Version": VERSION_STRIPE };
        if (idem) entetes["Idempotency-Key"] = idem;
        let r;
        try {
            r = await fetch(base + chemin, {
                method: methode,
                headers: entetes,
                body: methode === "GET" ? undefined : formulaire(params || []),
                signal: AbortSignal.timeout(DELAI_STRIPE),
            });
        } catch {
            throw new Erreur(502, "stripe", "Stripe ne répond pas (" + quoi + ") : réessaie dans un moment.");
        }
        let json = null;
        try {
            json = await r.json();
        } catch {
            json = null;
        }
        if (r.status < 200 || r.status >= 300) {
            const t = json && json.error && json.error.message ? propre(json.error.message) : "réponse " + r.status;
            const extra = { stripe_statut: r.status };
            // 1.46.1 : le code et le champ de l'erreur (un prix inconnu : la page de paiement se refait avec le montant)
            if (json && json.error && typeof json.error.code === "string") extra.stripe_code = json.error.code.slice(0, 80);
            if (json && json.error && typeof json.error.param === "string") extra.stripe_champ = json.error.param.slice(0, 120);
            throw new Erreur(502, "stripe", "Stripe refuse (" + quoi + ") : " + t, extra);
        }
        return { statut: r.status, json: json || {} };
    },

    // ============================================================ le PC ==
    // Une commande -> ce que le PC en voit.
    // 1.48.0 : les lignes d'une commande ; une commande d'avant la 1.48 (sans lignes) : une ligne, deduite de la commande.
    lignesDe(c) {
        const l = this.lignes("SELECT * FROM commande_lignes WHERE numero = ? ORDER BY ligne", c.numero);
        if (l.length) return l;
        const etat = c.etat === "a_revoir" ? "a_revoir" : c.etat;
        return [
            {
                numero: c.numero,
                ligne: 1,
                article: c.article,
                genre: c.genre,
                edition: c.edition,
                option: c.option,
                jours: c.jours,
                montant: c.montant,
                libelle: c.libelle,
                licence_existante: c.licence_existante,
                avec_ligne: 0,
                etat,
                licence: c.licence,
                erreur: c.erreur,
                maj: c.maj,
                deduite: true,
            },
        ];
    },
    ligneJson(l) {
        const o = { ligne: l.ligne, article: l.article, genre: l.genre };
        if (l.genre === "edition") o.edition = l.edition;
        else {
            o.option = l.option;
            o.jours = l.jours;
            if (l.licence_existante) o.licence_existante = l.licence_existante;
            if (l.avec_ligne) o.avec_ligne = l.avec_ligne;
        }
        o.libelle = l.libelle;
        o.montant = l.montant;
        o.etat = l.etat;
        if (l.licence) o.licence = l.licence;
        if (l.erreur) o.erreur = l.erreur;
        return o;
    },
    // L'etat d'une commande d'apres ses lignes (toutes finies : livree, ou refusee si aucune n'est livree) ; null : pas fini.
    // 1.48.1 : toutes remboursees (un article a la fois) : remboursee, comme le remboursement de toute la commande.
    etatFini(numero) {
        const l = this.lignes("SELECT etat, licence FROM commande_lignes WHERE numero = ?", numero);
        if (!l.length || !l.every((x) => LIGNE_FINIE.includes(x.etat))) return null;
        if (l.every((x) => x.etat === "remboursee")) return { etat: "remboursee", licence: "" };
        const livree = l.find((x) => x.etat === "livree");
        return livree ? { etat: "livree", licence: livree.licence } : { etat: "refusee", licence: "" };
    },

    commandePourPc(c) {
        const o = { numero: c.numero, article: c.article, libelle: c.libelle, montant: c.montant, devise: c.devise, etat: c.etat, mode: c.mode, cree: c.cree, maj: c.maj };
        o.paiement = c.mode !== "demande"; // 1.48.0
        o.lignes = this.lignesDe(c).map((l) => this.ligneJson(l));
        if (c.licence) o.licence = c.licence;
        if (c.recu_url) o.recu_url = c.recu_url;
        if (c.etat === "a_payer") {
            o.url = c.stripe_url;
            o.expire = c.expire;
        }
        return o;
    },

    // POST /v1/boutique/commande (le PC) : la commande, puis la page de paiement de Stripe.
    async passerCommande(ctx) {
        const corps = this.corpsJson(ctx);
        // 1.48.0 : le panier - des lignes, payees (une page de Stripe) ou une demande sans paiement ; l'ancien corps
        // (<< article >> seul, les Studios 1.46/1.47) : une commande payee d'une ligne.
        if (corps.paiement !== undefined && typeof corps.paiement !== "boolean") throw mal("paiement");
        const paye = corps.paiement !== false;
        if (paye && !this.boutiqueOuverte())
            throw new Erreur(409, "boutique", "La boutique en ligne est fermée (l'éditeur ne vend pas en ligne pour l'instant) : demande ta licence sans paiement.");
        const reglages = this.reglagesBoutique();
        let demandees;
        if (corps.lignes !== undefined) {
            if (!Array.isArray(corps.lignes) || !corps.lignes.length || corps.lignes.length > MAX_LIGNES)
                throw mal("lignes", "Le champ « lignes » doit être une liste de 1 à " + MAX_LIGNES + " articles.");
            demandees = corps.lignes;
        } else {
            if (typeof corps.article !== "string") throw mal("article");
            demandees = [{ article: corps.article, licence_existante: corps.licence_existante }];
        }
        const vus = new Set();
        const lignesCmd = demandees.map((d, i) => {
            const quoi = "La ligne n° " + (i + 1);
            if (!d || typeof d !== "object" || typeof d.article !== "string") throw mal("lignes", quoi + " n'a pas d'« article ».");
            if (vus.has(d.article)) throw mal("lignes", quoi + " : « " + d.article + " » est deux fois dans la commande.");
            vus.add(d.article);
            let m = /^ed:([A-Z][A-Z0-9_]{0,31})$/.exec(d.article);
            const l = { ligne: i + 1, article: d.article, genre: "", edition: "", option: "", jours: 0, montant: 0, libelle: "", licence_existante: "", avec_ligne: 0, art: null };
            if (m) {
                l.genre = "edition";
                l.edition = m[1];
            } else if ((m = /^opt:([a-z0-9_]{1,40}):(\d{1,4})$/.exec(d.article)) && Number(m[2]) <= 3660) {
                l.genre = "option";
                l.option = m[1];
                l.jours = Number(m[2]);
            } else throw mal("lignes", quoi + " : « article » vaut ed:<OFFRE> ou opt:<option>:<jours>.");
            const art = reglages.articles.find((a) => a.id === d.article);
            if (paye) {
                if (!art) throw new Erreur(404, "article", "« " + d.article + " » n'est pas (ou plus) en vente en ligne : relis la boutique.", { article: d.article });
                l.art = art;
                l.montant = montantActuel(art, ctx.now);
                l.libelle = art.libelle;
            } else {
                if (d.libelle !== undefined && !texteLigne(d.libelle, 1, 120)) throw mal("lignes", quoi + " : « libelle » : une ligne, 120 caractères au plus.");
                l.libelle = d.libelle || (art ? art.libelle : d.article);
            }
            if (l.genre === "option") {
                if (d.avec_ligne !== undefined) {
                    if (!entier(d.avec_ligne, 1, demandees.length) || d.avec_ligne === i + 1 || !/^ed:/.test(String((demandees[d.avec_ligne - 1] || {}).article)))
                        throw mal("lignes", quoi + " : « avec_ligne » doit être le n° d'une ligne de licence (ed:…) de la commande.");
                    l.avec_ligne = d.avec_ligne;
                } else if (estIdLicence(d.licence_existante)) {
                    l.licence_existante = d.licence_existante;
                } else {
                    throw mal(corps.lignes === undefined ? "licence_existante" : "lignes", quoi + " : une option s'ajoute à une licence : « licence_existante » (son identifiant) ou « avec_ligne ».");
                }
            }
            return l;
        });
        // Une seule licence (ed:) par commande : un PC a une licence, et son etat << poste >> ne porte qu'un code a la fois.
        if (lignesCmd.filter((l) => l.genre === "edition").length > 1) throw mal("lignes", "Une seule licence (ed:…) par commande : un PC a une licence.");
        if (!texteLigne(corps.societe, 1, 120) || !corps.societe.trim()) throw mal("societe", "Le nom de ta société (une ligne, 120 caractères au plus) manque.");
        if (corps.nom !== undefined && !texteLigne(corps.nom, 0, 120)) throw mal("nom", "Le nom : une ligne, 120 caractères au plus.");
        if (!courrielLisible(corps.courriel)) throw mal("courriel", "Ton courriel est illisible : Stripe y envoie la facture.");
        const art = lignesCmd[0].art || { id: lignesCmd[0].article, genre: lignesCmd[0].genre, edition: lignesCmd[0].edition, option: lignesCmd[0].option, jours: lignesCmd[0].jours, libelle: lignesCmd[0].libelle };
        const existante = lignesCmd[0].licence_existante;
        // La demande du PC, scellee pour la cle de l'editeur (et les admins qui ont le droit << demandes >>), comme
        // POST /v1/demandes.
        if (typeof corps.cle !== "string" || !/^xpg-[0-9a-f]{16}$/.test(corps.cle)) throw mal("cle");
        if (!(await this.clesEditeur()).some((k) => k.id === corps.cle))
            throw new Erreur(400, "cle", "Cette commande est scellée pour une clé que l'éditeur n'emploie pas sur ce service.");
        if (!estBase64(corps.scelle, MAX_SCELLE)) throw mal("scelle", "Le champ « scelle » doit être du base64 (64 Kio au plus).");
        const autres = [];
        if (corps.scelles !== undefined) {
            if (!Array.isArray(corps.scelles) || corps.scelles.length > MAX_SCELLES)
                throw mal("scelles", "Le champ « scelles » doit être une liste de {cle, scelle} (" + MAX_SCELLES + " au plus).");
            const vues = new Set([corps.cle]);
            for (const s of corps.scelles) {
                if (!s || typeof s !== "object" || typeof s.cle !== "string" || !/^xpg-[0-9a-f]{16}$/.test(s.cle) || vues.has(s.cle))
                    throw mal("scelles", "Une entrée de « scelles » n'a pas l'identifiant d'une clé (xpg-…), ou il y est deux fois.");
                vues.add(s.cle);
                if (!estBase64(s.scelle, MAX_SCELLE)) throw mal("scelles", "Un scellé de « scelles » doit être du base64 (64 Kio au plus).");
                if (!(await this.clePourDemandes(s.cle)))
                    throw new Erreur(400, "cle", "Cette commande est scellée pour une clé qui n'est ni celle de l'éditeur, ni celle d'un admin qui traite les demandes.");
                autres.push(s);
            }
        }
        const poste = ctx.appelant.poste;
        // Une commande a payer de ce PC, pas encore expiree : la meme page de paiement (pas deux achats en meme temps).
        const enCours = paye ? this.ligne("SELECT * FROM commandes WHERE poste = ? AND etat = 'a_payer' AND expire > ? ORDER BY numero DESC LIMIT 1", poste, ctx.now) : null;
        if (enCours)
            throw new Erreur(409, "en_cours", "Une commande de ce PC attend déjà son paiement (n° " + enCours.numero + ") : termine-la ou annule-la.", {
                numero: enCours.numero,
                url: enCours.stripe_url,
                expire: enCours.expire,
            });
        const octets = corps.scelle.length + autres.reduce((n, s) => n + s.scelle.length, 0);
        if (this.compte("n_commandes") >= MAX_COMMANDES) throw new Erreur(507, "limite", "Le service a atteint son nombre maximal de commandes.");
        if (this.nombre("SELECT COUNT(*) AS n FROM commandes WHERE etat = 'a_payer'") >= MAX_A_PAYER)
            throw new Erreur(507, "limite", "Trop de commandes attendent leur paiement : réessaie dans une demi-heure.");
        if (this.compte("octets_commandes") + octets > MAX_OCTETS_COMMANDES) throw new Erreur(507, "limite", "Le service garde déjà trop de commandes : réessaie plus tard.");
        const mode = paye ? modeStripe(this.env.STRIPE_SECRET_KEY) || "test" : "demande"; // 1.48.0 : << demande >> : sans paiement
        const numero = this.suivant("numero_commande");
        const expire = paye ? ctx.now + DUREE_PAIEMENT : 0;
        const base = ctx.url.origin;
        const montant = lignesCmd.reduce((n, l) => n + l.montant, 0);
        const libelle = lignesCmd.length === 1 ? lignesCmd[0].libelle : lignesCmd[0].libelle + " + " + (lignesCmd.length - 1) + " article(s)";
        // La commande d'abord (le numero est pris), puis Stripe ; un refus de Stripe : elle s'en va.
        this.x(
            `INSERT INTO commandes (numero, poste, cle_pc, article, genre, edition, option, jours, montant, devise, libelle, societe, nom, courriel,
                licence_existante, scelle_cle, scelle, octets, etat, mode, cree, maj, expire, paye, stripe_session, stripe_url, stripe_paiement,
                stripe_remboursement, recu_url, licence, erreur, prise, prise_par)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, '', '', '', '', '', '', '', 0, '')`,
            numero,
            poste,
            ctx.appelant.cle,
            art.id,
            art.genre,
            art.edition || "",
            art.option || "",
            art.genre === "option" ? art.jours : 0,
            montant,
            reglages.devise,
            libelle.slice(0, 200),
            corps.societe.trim(),
            (corps.nom || "").trim(),
            corps.courriel,
            existante,
            corps.cle,
            corps.scelle,
            octets,
            paye ? "a_payer" : "a_valider",
            mode,
            ctx.now,
            ctx.now,
            expire,
        );
        for (const l of lignesCmd)
            this.x(
                `INSERT INTO commande_lignes (numero, ligne, article, genre, edition, option, jours, montant, libelle, licence_existante, avec_ligne, etat, licence, erreur, maj)
                 VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, '', '', ?)`,
                numero, l.ligne, l.article, l.genre, l.edition, l.option, l.jours, l.montant, l.libelle, l.licence_existante, l.avec_ligne, paye ? "a_payer" : "a_valider", ctx.now,
            );
        let session = null;
        if (paye) {
            // La page de paiement (docs/BOUTIQUE.md, par. 4 et 10) : une line_items[i] par ligne (le prix Stripe de son
            // montant, sinon price_data) ; la carte seulement, jamais de client Stripe ni de carte gardee (1.46.1).
            const elements = (sansPrix) => {
                const out = [];
                lignesCmd.forEach((l, i) => {
                    const prix = sansPrix ? "" : this.prixStripe(l.art, reglages.devise, l.montant);
                    if (prix) out.push(["line_items[" + i + "][price]", prix]);
                    else
                        out.push(
                            ["line_items[" + i + "][price_data][currency]", reglages.devise],
                            ["line_items[" + i + "][price_data][unit_amount]", l.montant],
                            ["line_items[" + i + "][price_data][product_data][name]", l.libelle],
                        );
                    out.push(["line_items[" + i + "][quantity]", 1]);
                });
                return out;
            };
            const params = (sansPrix) => [
                ["mode", "payment"],
                ...elements(sansPrix),
                ["allowed_payment_method_types[0]", "card"],
                ["wallet_options[link][display]", "never"],
                ["customer_email", corps.courriel],
                ["client_reference_id", numero],
                ["metadata[commande]", numero],
                ["metadata[article]", lignesCmd.map((l) => l.article).join(",").slice(0, 490)],
                ["invoice_creation[enabled]", "true"],
                ["locale", "fr"],
                ["expires_at", expire],
                ["success_url", base + "/v1/boutique/merci?c=" + numero],
                ["cancel_url", base + "/v1/boutique/annule?c=" + numero],
            ];
            try {
                const page = (sansPrix, idem) => this.appelStripe(ctx, "POST", "/v1/checkout/sessions", params(sansPrix), idem, "la page de paiement");
                const avecPrix = lignesCmd.some((l) => this.prixStripe(l.art, reglages.devise, l.montant));
                try {
                    session = (await page(false, "commande-" + numero)).json;
                } catch (e) {
                    // 1.46.1 : un prix qui n'est plus chez Stripe : la page se refait avec les montants donnes, les prix oublies.
                    const x = e instanceof Erreur && e.extra ? e.extra : null;
                    if (!avecPrix || !x || !(x.stripe_statut >= 400 && x.stripe_statut < 500) || !(x.stripe_code === "resource_missing" || /^line_items/.test(x.stripe_champ || "")))
                        throw e;
                    for (const l of lignesCmd) this.oublierPrixStripe(l.art, reglages.devise, l.montant);
                    session = (await page(true, "commande-" + numero + "-montant")).json;
                }
                if (typeof session.id !== "string" || typeof session.url !== "string" || !/^https?:\/\//.test(session.url))
                    throw new Erreur(502, "stripe", "Stripe a rendu une page de paiement illisible.");
            } catch (e) {
                this.x("DELETE FROM commande_lignes WHERE numero = ?", numero);
                this.x("DELETE FROM commandes WHERE numero = ?", numero);
                throw e;
            }
            this.x("UPDATE commandes SET stripe_session = ?, stripe_url = ? WHERE numero = ?", session.id.slice(0, 255), session.url.slice(0, 2000), numero);
        }
        for (const s of autres) this.x("INSERT INTO commandes_scelles (numero, cle, scelle) VALUES (?, ?, ?)", numero, s.cle, s.scelle);
        this.ajouter("n_commandes", 1);
        this.ajouter("octets_commandes", octets);
        this.signalerCommande();
        const r = { numero, montant, devise: reglages.devise, mode: paye ? mode : modeStripe(this.env.STRIPE_SECRET_KEY) || "test", etat: paye ? "a_payer" : "a_valider" };
        if (session) {
            r.url = session.url;
            r.expire = expire;
        }
        r.lignes = this.lignesDe({ numero }).map((l) => this.ligneJson(l));
        return reponseJson(201, r);
    },

    // GET /v1/boutique/commandes (le PC) : les siennes, les plus recentes d'abord (50 au plus).
    async commandesDuPc(ctx) {
        const commandes = this.lignes("SELECT * FROM commandes WHERE poste = ? ORDER BY numero DESC LIMIT ?", ctx.appelant.poste, LISTE_PC).map((c) =>
            this.commandePourPc(c),
        );
        return reponseJson(200, { commandes });
    },

    // POST /v1/boutique/annuler (le PC) {"numero"} : sa commande a payer passe << annulee >> (la page de paiement est
    // expiree chez Stripe ; un paiement fait quand meme : le webhook la fait passer << payee >>).
    async annulerCommande(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entier(corps.numero, 1, Number.MAX_SAFE_INTEGER)) throw mal("numero");
        const c = this.ligne("SELECT * FROM commandes WHERE numero = ? AND poste = ?", corps.numero, ctx.appelant.poste);
        if (!c) throw new Erreur(404, "commande", "Cette commande n'existe pas, ou n'est pas celle de ce PC.");
        if (c.etat === "annulee") return reponseJson(200, {});
        // 1.48.0 : une demande (sans paiement) dont aucune ligne n'est encore livree s'annule aussi
        if (c.etat === "a_valider" && !this.lignes("SELECT 1 AS x FROM commande_lignes WHERE numero = ? AND etat = 'livree'", c.numero).length) {
            this.x("UPDATE commandes SET etat = 'annulee', maj = ? WHERE numero = ?", ctx.now, c.numero);
            this.x("UPDATE commande_lignes SET etat = 'annulee', maj = ? WHERE numero = ? AND etat IN ('a_valider', 'a_revoir')", ctx.now, c.numero);
            this.signalerCommande();
            return reponseJson(200, {});
        }
        if (c.etat !== "a_payer") throw new Erreur(409, "etat", "Cette commande n'attend plus son paiement (« " + c.etat + " ») : elle ne s'annule plus ici.", { etat: c.etat });
        if (c.stripe_session) {
            try {
                await this.appelStripe(ctx, "POST", "/v1/checkout/sessions/" + encodeURIComponent(c.stripe_session) + "/expire", [], "annulation-" + c.numero, "fermer la page de paiement");
            } catch (e) {
                // Stripe refuse (la page est deja expiree, ou payee : le webhook le dira) : annulee ici quand meme ; Stripe ne
                // repond pas : rien n'est fait.
                if (!(e instanceof Erreur) || !e.extra || !e.extra.stripe_statut) throw e;
            }
        }
        this.x("UPDATE commandes SET etat = 'annulee', maj = ? WHERE numero = ? AND etat = 'a_payer'", ctx.now, c.numero);
        this.x("UPDATE commande_lignes SET etat = 'annulee', maj = ? WHERE numero = ? AND etat = 'a_payer'", ctx.now, c.numero); // 1.48.0
        this.signalerCommande();
        return reponseJson(200, {});
    },

    // GET /v1/boutique/merci?c=N et /v1/boutique/annule?c=N : les deux petites pages ou Stripe renvoie le navigateur.
    numeroDePage(ctx) {
        const c = ctx.url.searchParams.get("c");
        return c && /^\d{1,12}$/.test(c) ? Number(c) : 0;
    },
    async pageMerci(ctx) {
        return pageHtml(
            "Paiement reçu",
            ["Merci ! Retourne dans ton Studio : ta licence arrive dès que l'éditeur la délivre (d'elle-même quand son Admin est ouvert).", "Stripe t'envoie la facture par courriel. Tu peux fermer cette page."],
            this.numeroDePage(ctx),
        );
    },
    async pageAnnule(ctx) {
        return pageHtml(
            "Paiement annulé",
            ["Aucun montant n'a été débité. Tu peux fermer cette page et revenir dans ton Studio (une nouvelle commande y redemande ta carte)."],
            this.numeroDePage(ctx),
        );
    },

    // ============================================================ le webhook ==
    // POST /v1/boutique/stripe : signe par Stripe (Stripe-Signature), jamais par une cle XPG.
    async webhookStripe(ctx) {
        const secret = this.env.STRIPE_WEBHOOK_SECRET;
        if (!secret) throw new Erreur(400, "webhook", "Le webhook de Stripe n'est pas branché sur ce service.");
        if (ctx.corps.length > MAX_CORPS_WEBHOOK) throw new Erreur(413, "taille", "Le corps de l'événement est trop gros (256 Kio au plus).");
        const sig = lireSignatureStripe(ctx.request.headers.get("Stripe-Signature"));
        if (!sig) throw new Erreur(400, "signature", "L'en-tête Stripe-Signature manque ou est illisible.");
        // L'heure de Stripe est l'heure vraie (jamais celle, decalee, des essais).
        if (Math.abs(sig.t - Math.floor(Date.now() / 1000)) > TOLERANCE_WEBHOOK)
            throw new Erreur(400, "signature", "La signature de Stripe est trop ancienne (ou trop loin de l'heure du service).");
        const attendue = await hmacHex(secret, new Uint8Array([...enc.encode(String(sig.t) + "."), ...ctx.corps]));
        let bonne = false;
        for (const v of sig.v1) if (egalConstant(v, attendue)) bonne = true;
        if (!bonne) throw new Erreur(400, "signature", "La signature de Stripe est fausse.");
        let ev;
        try {
            ev = JSON.parse(new TextDecoder().decode(ctx.corps));
        } catch {
            throw new Erreur(400, "json", "L'événement de Stripe n'est pas un JSON lisible.");
        }
        if (!ev || typeof ev !== "object" || typeof ev.id !== "string" || !ev.id || ev.id.length > 255 || typeof ev.type !== "string")
            throw new Erreur(400, "json", "L'événement de Stripe n'a pas d'identifiant ou de type.");
        this.x("INSERT INTO boutique (cle, valeur) VALUES ('dernier_evenement', ?) ON CONFLICT (cle) DO UPDATE SET valeur = excluded.valeur", String(ctx.now));
        if (this.ligne("SELECT 1 AS x FROM stripe_evenements WHERE id = ?", ev.id)) return reponseJson(200, { recu: true, deja: true });
        const obj = ev.data && ev.data.object && typeof ev.data.object === "object" ? ev.data.object : {};
        if (ev.type === "checkout.session.completed" || ev.type === "checkout.session.async_payment_succeeded") {
            if (ev.type === "checkout.session.async_payment_succeeded" || obj.payment_status === "paid") await this.paiementRecu(ctx, obj);
        } else if (ev.type === "checkout.session.async_payment_failed") {
            this.sessionCloses(ctx, obj, "echec_paiement");
        } else if (ev.type === "checkout.session.expired") {
            this.sessionCloses(ctx, obj, "expiree");
        } else if (ev.type === "charge.refunded") {
            this.remboursementRecu(ctx, obj);
        }
        this.x("INSERT INTO stripe_evenements (id, recu, type) VALUES (?, ?, ?)", ev.id, ctx.now, ev.type.slice(0, 80));
        return reponseJson(200, { recu: true });
    },

    // La commande d'une session de Checkout (son numero dans les metadonnees, et la meme session) ; null : aucune.
    commandeDeSession(obj) {
        const n = Number((obj.metadata && obj.metadata.commande) || obj.client_reference_id);
        if (!Number.isSafeInteger(n) || n < 1 || typeof obj.id !== "string") return null;
        const c = this.ligne("SELECT * FROM commandes WHERE numero = ?", n);
        return c && c.stripe_session === obj.id ? c : null;
    },

    async paiementRecu(ctx, obj) {
        const c = this.commandeDeSession(obj);
        if (!c || !["a_payer", "expiree", "annulee", "echec_paiement"].includes(c.etat)) return;
        const paiement = typeof obj.payment_intent === "string" ? obj.payment_intent.slice(0, 255) : "";
        const bon = obj.amount_total === c.montant && String(obj.currency || "").toLowerCase() === c.devise;
        const etat = bon ? "payee" : "a_revoir";
        const erreur = bon
            ? ""
            : "Le montant payé (" + (Number.isSafeInteger(obj.amount_total) ? montantTexte(obj.amount_total, String(obj.currency || "").toLowerCase()) : "?") +
              ") n'est pas celui de la commande (" + montantTexte(c.montant, c.devise) + ") : vérifie dans Stripe avant de livrer.";
        // Le recu : la facture de Stripe (invoice_creation), si elle se lit tout de suite ; sinon rien (la facture part par mail).
        let recu = "";
        if (typeof obj.invoice === "string" && /^in_[A-Za-z0-9]+$/.test(obj.invoice)) {
            try {
                const f = (await this.appelStripe(ctx, "GET", "/v1/invoices/" + obj.invoice, null, "", "lire la facture")).json;
                if (typeof f.hosted_invoice_url === "string" && /^https:\/\//.test(f.hosted_invoice_url)) recu = f.hosted_invoice_url.slice(0, 2000);
            } catch {
                recu = "";
            }
        }
        this.x(
            "UPDATE commandes SET etat = ?, paye = ?, maj = ?, stripe_paiement = ?, recu_url = ?, erreur = ? WHERE numero = ?",
            etat,
            ctx.now,
            ctx.now,
            paiement,
            recu,
            erreur,
            c.numero,
        );
        // 1.48.0 : les lignes a payer (ou closes sans paiement) : payees (ou a revoir avec la commande)
        this.x("UPDATE commande_lignes SET etat = ?, erreur = ?, maj = ? WHERE numero = ? AND etat IN ('a_payer', 'expiree', 'annulee', 'echec_paiement')", bon ? "payee" : "a_revoir", erreur, ctx.now, c.numero);
        this.signalerCommande();
    },

    sessionCloses(ctx, obj, etat) {
        const c = this.commandeDeSession(obj);
        if (!c || c.etat !== "a_payer") return;
        this.x("UPDATE commandes SET etat = ?, maj = ? WHERE numero = ?", etat, ctx.now, c.numero);
        this.x("UPDATE commande_lignes SET etat = ?, maj = ? WHERE numero = ? AND etat = 'a_payer'", etat, ctx.now, c.numero); // 1.48.0
        this.signalerCommande();
    },

    // charge.refunded : remboursee EN ENTIER -> << remboursee >> (la licence livree n'est pas revoquee d'office : l'Admin
    // le signale, l'editeur decide).
    remboursementRecu(ctx, obj) {
        if (typeof obj.payment_intent !== "string" || !obj.payment_intent) return;
        const entier = obj.refunded === true || (Number.isSafeInteger(obj.amount_refunded) && Number.isSafeInteger(obj.amount) && obj.amount_refunded >= obj.amount);
        if (!entier) return;
        const c = this.ligne("SELECT * FROM commandes WHERE stripe_paiement = ? LIMIT 1", obj.payment_intent);
        if (!c || !["payee", "livree", "a_revoir"].includes(c.etat)) return;
        this.x("UPDATE commandes SET etat = 'remboursee', maj = ?, prise = 0, prise_par = '' WHERE numero = ?", ctx.now, c.numero);
        this.x("UPDATE commande_lignes SET etat = 'remboursee', maj = ? WHERE numero = ? AND etat <> 'refusee'", ctx.now, c.numero); // 1.48.0
        this.signalerCommande();
    },

    // ============================================================ l'editeur et les admins ==
    // Une commande -> ce qu'en voit l'Admin (toute la commande, et le scelle de la cle qui appelle).
    async commandePourAdmin(ctx, c, idAppelant) {
        const o = {
            numero: c.numero,
            poste: c.poste,
            cle_pc: c.cle_pc,
            article: c.article,
            genre: c.genre,
            montant: c.montant,
            devise: c.devise,
            libelle: c.libelle,
            societe: c.societe,
            nom: c.nom,
            courriel: c.courriel,
            etat: c.etat,
            mode: c.mode,
            cree: c.cree,
            maj: c.maj,
            expire: c.expire,
            paye: c.paye || null,
            stripe_session: c.stripe_session,
            stripe_paiement: c.stripe_paiement,
            stripe_remboursement: c.stripe_remboursement,
            recu_url: c.recu_url,
            licence: c.licence,
            erreur: c.erreur,
            prise: c.prise && c.prise > ctx.now - PRISE ? c.prise : null,
        };
        if (c.genre === "edition") o.edition = c.edition;
        else {
            o.option = c.option;
            o.jours = c.jours;
            o.licence_existante = c.licence_existante;
        }
        if (ctx.appelant.role === "admin") {
            const s = this.ligne("SELECT scelle FROM commandes_scelles WHERE numero = ? AND cle = ?", c.numero, idAppelant);
            o.cle = idAppelant;
            o.scelle = s ? s.scelle : null;
            if (!s) o.pour_un_autre = true;
        } else {
            o.cle = c.scelle_cle;
            o.scelle = c.scelle || null;
        }
        o.paiement = c.mode !== "demande"; // 1.48.0
        o.lignes = this.lignesDe(c).map((l) => this.ligneJson(l));
        return o;
    },

    // GET /admin/commandes?depuis=<maj> (admin+ ; admin : le droit << demandes >>) : les commandes changees apres `depuis`
    // (toutes sans lui), dans l'ordre de leur changement ; 500 au plus (`suite` : il y en a d'autres).
    async listerCommandes(ctx) {
        const d = ctx.url.searchParams.get("depuis");
        if (d !== null && !/^\d{1,12}$/.test(d)) throw new Erreur(400, "champ", "« depuis » doit être une heure (secondes Unix).");
        const depuis = d === null ? -1 : Number(d);
        const id = ctx.appelant.role === "admin" ? await cleIdDe(ctx.appelant.octets) : "";
        const lignes = this.lignes("SELECT * FROM commandes WHERE maj > ? ORDER BY maj, numero LIMIT ?", depuis, LISTE_ADMIN + 1);
        const suite = lignes.length > LISTE_ADMIN;
        const commandes = [];
        for (const c of lignes.slice(0, LISTE_ADMIN)) commandes.push(await this.commandePourAdmin(ctx, c, id));
        return reponseJson(200, {
            commandes,
            suite,
            numero: this.compte("commandes_changements"),
            a_livrer: this.nombre("SELECT COUNT(*) AS n FROM commandes WHERE etat = 'payee'"),
            a_valider: this.nombre("SELECT COUNT(*) AS n FROM commande_lignes WHERE etat IN ('a_valider', 'a_revoir')"), // 1.48.0
        });
    },

    commandeOuErreur(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entier(corps.numero, 1, Number.MAX_SAFE_INTEGER)) throw mal("numero");
        const c = this.ligne("SELECT * FROM commandes WHERE numero = ?", corps.numero);
        if (!c) throw new Erreur(404, "commande", "La commande n° " + corps.numero + " n'existe pas.");
        return { corps, c };
    },

    // Prise par un autre Admin il y a moins de 5 minutes ?
    priseParUnAutre(ctx, c) {
        return c.prise > ctx.now - PRISE && c.prise_par && c.prise_par !== ctx.appelant.cle;
    },

    // POST /admin/commandes/prendre {"numero"} : cet Admin la livre (5 minutes) ; une payee (ou a revoir) seulement.
    async prendreCommande(ctx) {
        const { corps, c } = this.commandeOuErreur(ctx);
        // 1.48.0 : une commande de plusieurs lignes, ou une demande, ne se prend que ligne par ligne (un Admin 1.48)
        if (corps.lignes !== true && (c.mode === "demande" || this.nombre("SELECT COUNT(*) AS n FROM commande_lignes WHERE numero = ?", c.numero) > 1))
            throw new Erreur(409, "lignes", "La commande n° " + c.numero + " a plusieurs articles (ou est une demande) : livre-la avec l'Admin 1.48.", { etat: c.etat });
        if (corps.lignes === true && c.etat === "a_valider") {
            if (this.priseParUnAutre(ctx, c)) throw new Erreur(409, "prise", "Un autre Admin valide la commande n° " + c.numero + " en ce moment.", { etat: c.etat });
            this.x("UPDATE commandes SET prise = ?, prise_par = ? WHERE numero = ?", ctx.now, ctx.appelant.cle, c.numero);
            return reponseJson(200, { numero: c.numero, prise: ctx.now, etat: c.etat });
        }
        if (c.etat !== "payee" && c.etat !== "a_revoir")
            throw new Erreur(409, "etat", "La commande n° " + c.numero + " n'est pas à livrer (« " + c.etat + " »).", { etat: c.etat, licence: c.licence || null });
        if (this.priseParUnAutre(ctx, c)) throw new Erreur(409, "prise", "Un autre Admin livre la commande n° " + c.numero + " en ce moment.", { etat: c.etat });
        this.x("UPDATE commandes SET prise = ?, prise_par = ? WHERE numero = ?", ctx.now, ctx.appelant.cle, c.numero);
        return reponseJson(200, { numero: c.numero, prise: ctx.now, etat: c.etat });
    },

    // POST /admin/commandes/livree {"numero","licence"} : payee (ou a revoir) -> livree ; une seule fois.
    async commandeLivree(ctx) {
        const { corps, c } = this.commandeOuErreur(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence", "Le champ « licence » doit être l'identifiant de la licence livrée.");
        if (c.etat !== "payee" && c.etat !== "a_revoir")
            throw new Erreur(409, "etat", "La commande n° " + c.numero + " n'est pas à livrer (« " + c.etat + " »).", { etat: c.etat, licence: c.licence || null });
        if (this.priseParUnAutre(ctx, c)) throw new Erreur(409, "prise", "Un autre Admin livre la commande n° " + c.numero + ".", { etat: c.etat });
        if (c.mode === "demande" || this.nombre("SELECT COUNT(*) AS n FROM commande_lignes WHERE numero = ?", c.numero) > 1)
            throw new Erreur(409, "lignes", "La commande n° " + c.numero + " a plusieurs articles : chacun se livre (POST /admin/commandes/ligne).", { etat: c.etat });
        this.x("UPDATE commandes SET etat = 'livree', licence = ?, erreur = '', maj = ?, prise = 0, prise_par = '' WHERE numero = ?", corps.licence, ctx.now, c.numero);
        this.x("UPDATE commande_lignes SET etat = 'livree', licence = ?, erreur = '', maj = ? WHERE numero = ?", corps.licence, ctx.now, c.numero); // 1.48.0
        // 1.47.0 : le fait (stats.js) - qui l'a livree (les ventes par admin)
        this.fait(ctx.now, "commande_livree", {
            licence: corps.licence,
            offre: c.genre === "edition" ? c.edition : "options",
            client: c.societe,
            admin: await this.idAppelant(ctx),
            poste: c.cle_pc,
            valeur: c.montant,
            detail: { numero: c.numero, genre: c.genre },
        });
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, etat: "livree", licence: corps.licence });
    },

    // POST /admin/commandes/a-revoir {"numero","erreur"} : la livraison est impossible (l'Admin dit pourquoi).
    async commandeARevoir(ctx) {
        const { corps, c } = this.commandeOuErreur(ctx);
        if (typeof corps.erreur !== "string" || !corps.erreur.trim()) throw mal("erreur", "Dis pourquoi la commande est à revoir (« erreur »).");
        if (c.etat !== "payee" && c.etat !== "a_revoir") throw new Erreur(409, "etat", "La commande n° " + c.numero + " n'est pas à livrer (« " + c.etat + " »).", { etat: c.etat });
        const texte = Array.from(corps.erreur.replace(/[\u0000-\u001f\u007f]/g, " ").trim()).slice(0, 500).join("");
        this.x("UPDATE commandes SET etat = 'a_revoir', erreur = ?, maj = ?, prise = 0, prise_par = '' WHERE numero = ?", texte, ctx.now, c.numero);
        this.x("UPDATE commande_lignes SET etat = 'a_revoir', erreur = ?, maj = ? WHERE numero = ? AND etat IN ('payee', 'a_revoir')", texte, ctx.now, c.numero); // 1.48.0
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, etat: "a_revoir" });
    },

    // 1.48.0 : POST /admin/commandes/ligne {"numero","ligne","action","licence"?,"raison"?} (droit << demandes >>) - une
    // ligne livree (sa licence), refusee ou a revoir (la raison) ; la commande finie quand toutes ses lignes le sont.
    async commandeLigne(ctx) {
        const { corps, c } = this.commandeOuErreur(ctx);
        if (!entier(corps.ligne, 1, MAX_LIGNES)) throw mal("ligne");
        if (!["livree", "refusee", "a_revoir"].includes(corps.action)) throw mal("action", "« action » vaut livree, refusee ou a_revoir.");
        // une commande d'avant la 1.48 (sans lignes) : sa ligne 1 est creee d'apres elle
        if (!this.nombre("SELECT COUNT(*) AS n FROM commande_lignes WHERE numero = ?", c.numero)) {
            const l = this.lignesDe(c)[0];
            this.x(
                `INSERT INTO commande_lignes (numero, ligne, article, genre, edition, option, jours, montant, libelle, licence_existante, avec_ligne, etat, licence, erreur, maj)
                 VALUES (?, 1, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?)`,
                c.numero, l.article, l.genre, l.edition, l.option, l.jours, l.montant, l.libelle, l.licence_existante, l.etat, l.licence || "", l.erreur || "", ctx.now,
            );
        }
        const l = this.ligne("SELECT * FROM commande_lignes WHERE numero = ? AND ligne = ?", c.numero, corps.ligne);
        if (!l) throw new Erreur(404, "ligne", "La commande n° " + c.numero + " n'a pas de ligne n° " + corps.ligne + ".");
        const aValider = ["payee", "a_valider", "a_revoir"].includes(l.etat) && ["payee", "a_valider", "a_revoir"].includes(c.etat);
        if (!aValider)
            throw new Erreur(409, "etat", "La ligne n° " + l.ligne + " de la commande n° " + c.numero + " n'est pas à valider (« " + l.etat + " »).", { etat: l.etat, licence: l.licence || null });
        if (this.priseParUnAutre(ctx, c)) throw new Erreur(409, "prise", "Un autre Admin valide la commande n° " + c.numero + " en ce moment.", { etat: l.etat });
        let etat = corps.action;
        if (corps.action === "livree") {
            if (!estIdLicence(corps.licence)) throw mal("licence", "Le champ « licence » doit être l'identifiant de la licence livrée.");
            this.x("UPDATE commande_lignes SET etat = 'livree', licence = ?, erreur = '', maj = ? WHERE numero = ? AND ligne = ?", corps.licence, ctx.now, c.numero, l.ligne);
            this.fait(ctx.now, "commande_livree", {
                licence: corps.licence,
                offre: l.genre === "edition" ? l.edition : "options",
                client: c.societe,
                admin: await this.idAppelant(ctx),
                poste: c.cle_pc,
                valeur: l.montant,
                detail: { numero: c.numero, ligne: l.ligne, genre: l.genre },
            });
        } else {
            if (typeof corps.raison !== "string" || !corps.raison.trim()) throw mal("raison", "Dis pourquoi (« raison »).");
            const texte = Array.from(corps.raison.replace(/[\u0000-\u001f\u007f]/g, " ").trim()).slice(0, 500).join("");
            this.x("UPDATE commande_lignes SET etat = ?, erreur = ?, maj = ? WHERE numero = ? AND ligne = ?", etat, texte, ctx.now, c.numero, l.ligne);
            // une commande payee a revoir : la commande aussi (comme avant la 1.48 : l'Admin et la liste le disent)
            if (etat === "a_revoir" && c.etat === "payee") this.x("UPDATE commandes SET etat = 'a_revoir', erreur = ? WHERE numero = ?", texte, c.numero);
        }
        const fini = this.etatFini(c.numero);
        let etatCommande = c.etat;
        if (fini) {
            etatCommande = fini.etat;
            this.x("UPDATE commandes SET etat = ?, licence = ?, erreur = '', maj = ?, prise = 0, prise_par = '' WHERE numero = ?", fini.etat, fini.licence, ctx.now, c.numero);
        } else {
            this.x("UPDATE commandes SET maj = ? WHERE numero = ?", ctx.now, c.numero);
        }
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, ligne: l.ligne, etat, etat_commande: etatCommande });
    },

    // POST /admin/commandes/rembourser {"numero"} (admin+) : le remboursement demande a Stripe ; la commande passe
    // << remboursee >> au webhook (charge.refunded).
    // 1.48.1 : {"numero","ligne"} - UN article : rembourserLigne.
    async rembourserCommande(ctx) {
        const { corps, c } = this.commandeOuErreur(ctx);
        if (corps.ligne !== undefined) return this.rembourserLigne(ctx, corps, c);
        if (!["payee", "livree", "a_revoir"].includes(c.etat) || !c.stripe_paiement)
            throw new Erreur(409, "etat", "La commande n° " + c.numero + " n'a pas de paiement à rembourser (« " + c.etat + " »).", { etat: c.etat });
        const r = (await this.appelStripe(ctx, "POST", "/v1/refunds", [["payment_intent", c.stripe_paiement]], "remboursement-" + c.numero, "le remboursement")).json;
        const id = typeof r.id === "string" ? r.id.slice(0, 255) : "";
        this.x("UPDATE commandes SET stripe_remboursement = ?, maj = ? WHERE numero = ?", id, ctx.now, c.numero);
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, remboursement: id, etat: c.etat });
    },

    // 1.48.1 : UN ARTICLE rembourse (docs/BOUTIQUE.md, par. 10) - Stripe rembourse le montant de cette ligne sur le meme
    // paiement (amount, une cle d'idempotence par commande et par ligne) ; la ligne passe << remboursee >> des que Stripe
    // l'accepte (le webhook ne dit pas quelle ligne : charge.refunded d'un remboursement partiel est ignore, comme avant) ;
    // les autres lignes ne bougent pas ; la commande suit ses lignes (toutes remboursees : << remboursee >>). Comme pour
    // toute la commande, la licence ou l'option livree n'est pas revoquee d'office (l'Admin le dit, l'editeur decide).
    // Deux fois la meme ligne : 409 << etat >> (jamais un second remboursement).
    async rembourserLigne(ctx, corps, c) {
        if (!entier(corps.ligne, 1, MAX_LIGNES)) throw mal("ligne");
        if (c.mode === "demande" || !c.stripe_paiement || !["payee", "livree", "a_revoir"].includes(c.etat))
            throw new Erreur(409, "etat", "La commande n° " + c.numero + " n'a pas de paiement à rembourser (« " + c.etat + " »).", { etat: c.etat });
        if (c.stripe_remboursement)
            throw new Erreur(409, "etat", "Le remboursement de toute la commande n° " + c.numero + " est déjà demandé à Stripe.", { etat: c.etat });
        // une commande d'avant la 1.48 (sans lignes) : sa ligne 1 est creee d'apres elle (comme commandeLigne)
        if (!this.nombre("SELECT COUNT(*) AS n FROM commande_lignes WHERE numero = ?", c.numero)) {
            const d = this.lignesDe(c)[0];
            this.x(
                `INSERT INTO commande_lignes (numero, ligne, article, genre, edition, option, jours, montant, libelle, licence_existante, avec_ligne, etat, licence, erreur, maj)
                 VALUES (?, 1, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?)`,
                c.numero, d.article, d.genre, d.edition, d.option, d.jours, d.montant, d.libelle, d.licence_existante, d.etat, d.licence || "", d.erreur || "", ctx.now,
            );
        }
        const l = this.ligne("SELECT * FROM commande_lignes WHERE numero = ? AND ligne = ?", c.numero, corps.ligne);
        if (!l) throw new Erreur(404, "ligne", "La commande n° " + c.numero + " n'a pas de ligne n° " + corps.ligne + ".");
        if (l.etat === "remboursee")
            throw new Erreur(409, "etat", "La ligne n° " + l.ligne + " de la commande n° " + c.numero + " est déjà remboursée.", { etat: l.etat, licence: l.licence || null });
        if (!["payee", "a_revoir", "livree", "refusee"].includes(l.etat) || !Number.isSafeInteger(l.montant) || l.montant <= 0)
            throw new Erreur(409, "etat", "La ligne n° " + l.ligne + " de la commande n° " + c.numero + " n'a pas de paiement à rembourser (« " + l.etat + " »).", { etat: l.etat });
        const params = [["payment_intent", c.stripe_paiement], ["amount", String(l.montant)], ["metadata[commande]", String(c.numero)], ["metadata[ligne]", String(l.ligne)]];
        const r = (await this.appelStripe(ctx, "POST", "/v1/refunds", params, "remboursement-" + c.numero + "-ligne-" + l.ligne, "le remboursement d'un article")).json;
        if (r.status === "failed" || r.status === "canceled")
            throw new Erreur(502, "stripe", "Stripe n'a pas remboursé la ligne n° " + l.ligne + " de la commande n° " + c.numero + " (« " + r.status + " »).");
        const id = typeof r.id === "string" ? r.id.slice(0, 255) : "";
        this.x("UPDATE commande_lignes SET etat = 'remboursee', maj = ? WHERE numero = ? AND ligne = ?", ctx.now, c.numero, l.ligne);
        const fini = this.etatFini(c.numero);
        let etatCommande = c.etat;
        if (fini) {
            etatCommande = fini.etat;
            this.x("UPDATE commandes SET etat = ?, licence = ?, maj = ?, prise = 0, prise_par = '' WHERE numero = ?", fini.etat, fini.licence || c.licence, ctx.now, c.numero);
        } else {
            this.x("UPDATE commandes SET maj = ? WHERE numero = ?", ctx.now, c.numero);
        }
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, ligne: l.ligne, remboursement: id, montant: l.montant, etat: "remboursee", etat_commande: etatCommande });
    },

    // 1.48.1 : POST /admin/commandes/supprimer {"numero"} (droit << demandes >>) - effacer une commande SANS ARGENT : expiree,
    // annulee, paiement refuse, ou une demande (sans paiement) refusee ; jamais une commande payee (meme remboursee, ou dont
    // les articles ont ete refuses) : elle reste pour la comptabilite (409 << payee >>) ; jamais une commande qui attend
    // (a payer : le client paie peut-etre ; a valider ; livree : 409 << etat >>). Ses lignes et ses scelles partent avec elle.
    async supprimerCommande(ctx) {
        const { c } = this.commandeOuErreur(ctx);
        const lignes = this.lignes("SELECT etat FROM commande_lignes WHERE numero = ?", c.numero);
        const argent = c.stripe_paiement || c.paye || c.stripe_remboursement || lignes.some((l) => ["payee", "livree", "remboursee", "a_revoir"].includes(l.etat) && c.mode !== "demande");
        if (argent && c.mode !== "demande")
            throw new Erreur(409, "payee", "Une commande payée reste pour la comptabilité : rembourse-la, elle reste dans la liste (n° " + c.numero + ").", { etat: c.etat });
        if (!["expiree", "annulee", "echec_paiement", "refusee"].includes(c.etat))
            throw new Erreur(409, "etat", "La commande n° " + c.numero + " ne s'efface pas (« " + c.etat + " ») : seulement une commande expirée, annulée, refusée ou dont le paiement a échoué.", { etat: c.etat });
        this.x("DELETE FROM commandes_scelles WHERE numero = ?", c.numero);
        this.x("DELETE FROM commande_lignes WHERE numero = ?", c.numero);
        this.x("DELETE FROM commandes WHERE numero = ?", c.numero);
        this.ajouter("n_commandes", -1);
        this.ajouter("octets_commandes", -Number(c.octets || 0));
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, supprimee: true });
    },

    // ============================================================ le menage ==
    // Toutes les 5 minutes : une page de paiement finie depuis 5 minutes sans nouvelle de Stripe -> expiree.
    menageBoutique(now) {
        const n = this.nombre("SELECT COUNT(*) AS n FROM commandes WHERE etat = 'a_payer' AND expire < ?", now - 300);
        if (!n) return;
        this.x("UPDATE commande_lignes SET etat = 'expiree', maj = ? WHERE etat = 'a_payer' AND numero IN (SELECT numero FROM commandes WHERE etat = 'a_payer' AND expire < ?)", now, now - 300); // 1.48.0
        this.x("UPDATE commandes SET etat = 'expiree', maj = ? WHERE etat = 'a_payer' AND expire < ?", now, now - 300);
        this.signalerCommande();
    },

    // Une fois par jour : les evenements de Stripe de plus de 30 jours ; les commandes closes sans paiement de plus de
    // 30 jours ; le scelle d'une commande livree ou remboursee depuis 7 jours (il ne sert plus).
    grandMenageBoutique(now) {
        this.x("DELETE FROM stripe_evenements WHERE recu < ?", now - GARDE_EVENEMENTS);
        const closes = "etat IN ('expiree', 'annulee', 'echec_paiement') AND maj < ?";
        const d = this.ligne("SELECT COUNT(*) AS n, COALESCE(SUM(octets), 0) AS o FROM commandes WHERE " + closes, now - GARDE_CLOSES);
        if (d && d.n) {
            this.x("DELETE FROM commandes_scelles WHERE numero IN (SELECT numero FROM commandes WHERE " + closes + ")", now - GARDE_CLOSES);
            this.x("DELETE FROM commande_lignes WHERE numero IN (SELECT numero FROM commandes WHERE " + closes + ")", now - GARDE_CLOSES); // 1.48.0
            this.x("DELETE FROM commandes WHERE " + closes, now - GARDE_CLOSES);
            this.ajouter("n_commandes", -Number(d.n));
            this.ajouter("octets_commandes", -Number(d.o));
        }
        const finies = "etat IN ('livree', 'remboursee') AND maj < ? AND octets > 0";
        const f = this.ligne("SELECT COUNT(*) AS n, COALESCE(SUM(octets), 0) AS o FROM commandes WHERE " + finies, now - GARDE_SCELLES);
        if (f && f.n) {
            this.x("DELETE FROM commandes_scelles WHERE numero IN (SELECT numero FROM commandes WHERE " + finies + ")", now - GARDE_SCELLES);
            this.x("UPDATE commandes SET scelle = '', octets = 0 WHERE " + finies, now - GARDE_SCELLES);
            this.ajouter("octets_commandes", -Number(f.o));
        }
    },
};

