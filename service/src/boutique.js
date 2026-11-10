// =============================================================================
//  service/src/boutique.js - 1.46.0 : LA BOUTIQUE ET LES COMMANDES (docs/BOUTIQUE.md)
//  - le catalogue de la boutique (les montants encaisses, en centimes TTC) regle
//    par l'admin+ (PUT /admin/boutique), lu par les Studios (GET /v1/boutique) ;
//  - les commandes des PC (POST /v1/boutique/commande) : une page de paiement
//    HEBERGEE PAR STRIPE (Checkout, mode payment, la carte seulement, sans client
//    Stripe ni carte gardee : redemandee a chaque achat) ;
//  - le webhook de Stripe (POST /v1/boutique/stripe : signe par Stripe,
//    HMAC-SHA256, compare en temps constant, 5 minutes au plus d'ecart, chaque
//    evenement traite une fois) : payee, expiree, echec, remboursee ;
//  - les commandes vues par l'editeur et les admins (droit << demandes >>) :
//    prendre, livree, a revoir ; rembourser (l'admin+).
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

// Les evenements que le point d'arrivee du webhook recoit (l'Admin les demande a Stripe : Brancher Stripe...).
export const EVENEMENTS_STRIPE = [
    "checkout.session.completed",
    "checkout.session.async_payment_succeeded",
    "checkout.session.async_payment_failed",
    "checkout.session.expired",
    "charge.refunded",
];

// Les etats d'une commande (docs/BOUTIQUE.md, par. 4).
export const ETATS_COMMANDE = ["a_payer", "payee", "livree", "expiree", "annulee", "echec_paiement", "remboursee", "a_revoir"];

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
`;

// Les tables de la boutique dans la sauvegarde (sauvegarde.js) : jamais les secrets de Stripe (ils ne sont pas en table).
export const TABLES_SAUVEGARDE_BOUTIQUE = [
    ["boutique", ["cle"]],
    ["commandes", ["numero"]],
    ["commandes_scelles", ["numero", "cle"]],
    ["stripe_evenements", ["id"]],
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
            return o;
        });
        const avant = this.reglagesBoutique();
        const numero = (Number(avant.numero) || 0) + 1;
        const r = { active: corps.active, livraison_auto: corps.livraison_auto === undefined ? true : corps.livraison_auto, devise, articles, numero };
        this.x("INSERT INTO boutique (cle, valeur) VALUES ('reglages', ?) ON CONFLICT (cle) DO UPDATE SET valeur = excluded.valeur", JSON.stringify(r));
        this.signalerChangement(); // GET /v1/etat << boutique >> peut changer
        return reponseJson(200, { numero });
    },

    // GET /admin/boutique (admin+, admin) : le reglage, et l'etat de Stripe (jamais la cle).
    async lireBoutiqueAdmin() {
        const r = this.reglagesBoutique();
        return reponseJson(200, { ...r, stripe: this.etatStripe(), ouverte: this.boutiqueOuverte() });
    },

    // GET /v1/boutique (publique) : ce qui s'achete. Stripe pas branche : fermee, aucun article.
    async lireBoutiquePublique() {
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
            o.montant = a.montant;
            o.libelle = a.libelle;
            return o;
        });
        return reponseJson(200, { active: true, devise: r.devise, mode: s.mode, articles });
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
        const entetes = { Authorization: "Bearer " + cle, "Content-Type": "application/x-www-form-urlencoded" };
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
            throw new Erreur(502, "stripe", "Stripe refuse (" + quoi + ") : " + t, { stripe_statut: r.status });
        }
        return { statut: r.status, json: json || {} };
    },

    // ============================================================ le PC ==
    // Une commande -> ce que le PC en voit.
    commandePourPc(c) {
        const o = { numero: c.numero, article: c.article, libelle: c.libelle, montant: c.montant, devise: c.devise, etat: c.etat, mode: c.mode, cree: c.cree, maj: c.maj };
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
        if (!this.boutiqueOuverte())
            throw new Erreur(409, "boutique", "La boutique en ligne est fermée (l'éditeur ne vend pas en ligne pour l'instant) : demande ta licence sans paiement.");
        const reglages = this.reglagesBoutique();
        if (typeof corps.article !== "string") throw mal("article");
        const art = reglages.articles.find((a) => a.id === corps.article);
        if (!art) throw new Erreur(404, "article", "Cet article n'est pas (ou plus) en vente en ligne : relis la boutique.");
        if (!texteLigne(corps.societe, 1, 120) || !corps.societe.trim()) throw mal("societe", "Le nom de ta société (une ligne, 120 caractères au plus) manque.");
        if (corps.nom !== undefined && !texteLigne(corps.nom, 0, 120)) throw mal("nom", "Le nom : une ligne, 120 caractères au plus.");
        if (!courrielLisible(corps.courriel)) throw mal("courriel", "Ton courriel est illisible : Stripe y envoie la facture.");
        let existante = "";
        if (art.genre === "option") {
            if (!estIdLicence(corps.licence_existante))
                throw mal("licence_existante", "Une option s'ajoute à ta licence : « licence_existante » doit être son identifiant.");
            existante = corps.licence_existante;
        }
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
        const enCours = this.ligne("SELECT * FROM commandes WHERE poste = ? AND etat = 'a_payer' AND expire > ? ORDER BY numero DESC LIMIT 1", poste, ctx.now);
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
        const mode = modeStripe(this.env.STRIPE_SECRET_KEY);
        const numero = this.suivant("numero_commande");
        const expire = ctx.now + DUREE_PAIEMENT;
        const base = ctx.url.origin;
        // La page de paiement (docs/BOUTIQUE.md, par. 4) : la carte seulement, jamais de client Stripe ni de carte gardee.
        const params = [
            ["mode", "payment"],
            ["line_items[0][price_data][currency]", reglages.devise],
            ["line_items[0][price_data][unit_amount]", art.montant],
            ["line_items[0][price_data][product_data][name]", art.libelle],
            ["line_items[0][quantity]", 1],
            ["payment_method_types[0]", "card"],
            ["customer_email", corps.courriel],
            ["client_reference_id", numero],
            ["metadata[commande]", numero],
            ["invoice_creation[enabled]", "true"],
            ["locale", "fr"],
            ["expires_at", expire],
            ["success_url", base + "/v1/boutique/merci?c=" + numero],
            ["cancel_url", base + "/v1/boutique/annule?c=" + numero],
        ];
        // La ligne d'abord (le numero est pris), puis Stripe ; un refus de Stripe : la ligne s'en va.
        this.x(
            `INSERT INTO commandes (numero, poste, cle_pc, article, genre, edition, option, jours, montant, devise, libelle, societe, nom, courriel,
                licence_existante, scelle_cle, scelle, octets, etat, mode, cree, maj, expire, paye, stripe_session, stripe_url, stripe_paiement,
                stripe_remboursement, recu_url, licence, erreur, prise, prise_par)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'a_payer', ?, ?, ?, ?, 0, '', '', '', '', '', '', '', 0, '')`,
            numero,
            poste,
            ctx.appelant.cle,
            art.id,
            art.genre,
            art.edition || "",
            art.option || "",
            art.genre === "option" ? art.jours : 0,
            art.montant,
            reglages.devise,
            art.libelle,
            corps.societe.trim(),
            (corps.nom || "").trim(),
            corps.courriel,
            existante,
            corps.cle,
            corps.scelle,
            octets,
            mode || "test",
            ctx.now,
            ctx.now,
            expire,
        );
        let session;
        try {
            session = (await this.appelStripe(ctx, "POST", "/v1/checkout/sessions", params, "commande-" + numero, "la page de paiement")).json;
            if (typeof session.id !== "string" || typeof session.url !== "string" || !/^https?:\/\//.test(session.url))
                throw new Erreur(502, "stripe", "Stripe a rendu une page de paiement illisible.");
        } catch (e) {
            this.x("DELETE FROM commandes WHERE numero = ?", numero);
            throw e;
        }
        this.x("UPDATE commandes SET stripe_session = ?, stripe_url = ? WHERE numero = ?", session.id.slice(0, 255), session.url.slice(0, 2000), numero);
        for (const s of autres) this.x("INSERT INTO commandes_scelles (numero, cle, scelle) VALUES (?, ?, ?)", numero, s.cle, s.scelle);
        this.ajouter("n_commandes", 1);
        this.ajouter("octets_commandes", octets);
        this.signalerCommande();
        return reponseJson(201, { numero, url: session.url, expire, montant: art.montant, devise: reglages.devise, mode: mode || "test" });
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
        this.signalerCommande();
    },

    sessionCloses(ctx, obj, etat) {
        const c = this.commandeDeSession(obj);
        if (!c || c.etat !== "a_payer") return;
        this.x("UPDATE commandes SET etat = ?, maj = ? WHERE numero = ?", etat, ctx.now, c.numero);
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
        const { c } = this.commandeOuErreur(ctx);
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
        this.x("UPDATE commandes SET etat = 'livree', licence = ?, erreur = '', maj = ?, prise = 0, prise_par = '' WHERE numero = ?", corps.licence, ctx.now, c.numero);
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
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, etat: "a_revoir" });
    },

    // POST /admin/commandes/rembourser {"numero"} (admin+) : le remboursement demande a Stripe ; la commande passe
    // << remboursee >> au webhook (charge.refunded).
    async rembourserCommande(ctx) {
        const { c } = this.commandeOuErreur(ctx);
        if (!["payee", "livree", "a_revoir"].includes(c.etat) || !c.stripe_paiement)
            throw new Erreur(409, "etat", "La commande n° " + c.numero + " n'a pas de paiement à rembourser (« " + c.etat + " »).", { etat: c.etat });
        const r = (await this.appelStripe(ctx, "POST", "/v1/refunds", [["payment_intent", c.stripe_paiement]], "remboursement-" + c.numero, "le remboursement")).json;
        const id = typeof r.id === "string" ? r.id.slice(0, 255) : "";
        this.x("UPDATE commandes SET stripe_remboursement = ?, maj = ? WHERE numero = ?", id, ctx.now, c.numero);
        this.signalerCommande();
        return reponseJson(200, { numero: c.numero, remboursement: id, etat: c.etat });
    },

    // ============================================================ le menage ==
    // Toutes les 5 minutes : une page de paiement finie depuis 5 minutes sans nouvelle de Stripe -> expiree.
    menageBoutique(now) {
        const n = this.nombre("SELECT COUNT(*) AS n FROM commandes WHERE etat = 'a_payer' AND expire < ?", now - 300);
        if (!n) return;
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

