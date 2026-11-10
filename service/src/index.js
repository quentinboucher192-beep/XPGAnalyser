// =============================================================================
//  service/src/index.js - 1.35.0 : LE SERVICE DES LICENCES XPG (le Worker)
// -----------------------------------------------------------------------------
//  Le contrat : docs/SERVICE-LICENCES.md. Le Worker ne garde rien : il lit le
//  corps (2 Mio au plus), ajoute l'adresse de l'appelant (CF-Connecting-IP) et
//  passe tout a UN Durable Object, << principal >>, qui tient les tables SQLite
//  (src/licences.js). Un seul objet : les appels sont traites l'un apres
//  l'autre, les numeros et les places ne se croisent jamais.
//  1.44.0 : le corps passe tel quel (des octets : aussi un morceau de fichier,
//  PUT /admin/fichiers/morceau) ; un lot d'une restauration : 8 Mio au plus.
//  1.44.0 : LE RELAIS DES SESSIONS PAR INTERNET (/v1/relais/*, src/relais.js) :
//  un AUTRE Durable Object, un par session (idFromName(localisateur)) ; le corps
//  d'un envoi : 2,5 Mio au plus ; son limiteur : LIMITE_RELAIS (ouvrir : LIMITE_PC).
//  1.47.0 : LE LIEU DE L'APPEL (le globe, stats.js) : la ville que Cloudflare donne (request.cf : ville, region, pays,
//  latitude et longitude arrondies, centre de donnees) passe a l'objet dans X-XPG-Lieu, que le Worker efface toujours de
//  l'appel recu ; jamais l'adresse IP. En local, seul X-XPG-Lieu-Essai (127.0.0.1, localhost) place un appel.
// =============================================================================
import { CORPS_MAX, CORPS_MAX_RESTAURATION, Erreur, dec, reponseErreur } from "./outils.js";
import { CORPS_MAX_RELAIS, localisateurDe } from "./relais.js";
import { MAX_CORPS_WEBHOOK } from "./boutique.js";
import { lieuDeLAppel } from "./stats.js";

export { Licences } from "./licences.js";
export { Relais } from "./relais.js";

// Le corps, tel quel (des octets : un JSON, ou les octets d'un morceau de fichier, 1.44.0), `max` octets au plus.
async function lireCorps(request, max) {
    const trop = () => new Erreur(413, "taille", "Le corps de l'appel est trop gros (" + max / (1024 * 1024) + " Mio au plus).");
    const annonce = request.headers.get("Content-Length");
    if (annonce !== null && Number(annonce) > max) throw trop();
    if (!request.body) return new Uint8Array(0);
    const lecteur = request.body.getReader();
    const morceaux = [];
    let n = 0;
    for (;;) {
        const { done, value } = await lecteur.read();
        if (done) break;
        n += value.length;
        if (n > max) {
            try {
                await lecteur.cancel();
            } catch {
                // rien
            }
            throw trop();
        }
        morceaux.push(value);
    }
    const out = new Uint8Array(n);
    let i = 0;
    for (const m of morceaux) {
        out.set(m, i);
        i += m.length;
    }
    return out;
}

// Les routes publiques (sans signature) : LIMITE_PUBLIQUE ; les autres routes /v1 : LIMITE_PC (1.44.0 : les morceaux
// et le lien d'un fichier aussi - un telechargement fait un appel par Mio).
// 1.46.0 : la boutique (le catalogue) et les deux petites pages ou Stripe renvoie le navigateur ; le webhook de Stripe
// (/v1/boutique/stripe) reste sous LIMITE_PC.
const PUBLIQUES = new Set([
    "/v1/etat",
    "/v1/heure",
    "/v1/certificat",
    "/v1/registre",
    "/v1/admins",
    "/v1/version",
    "/v1/fichiers/manifeste",
    "/v1/boutique",
    "/v1/boutique/merci",
    "/v1/boutique/annule",
]);

// Le limiteur de Cloudflare (par adresse, par minute ; une liaison absente : rien). Faux : trop d'appels.
async function passe(env, url, ip) {
    // LIMITES_ESSAI = "sans" : SEULEMENT sur ce poste (127.0.0.1), pour les essais qui enchainent les appels.
    const local = url.hostname === "127.0.0.1" || url.hostname === "localhost";
    if (local && env.LIMITES_ESSAI === "sans") return true;
    let limiteur = null;
    if (PUBLIQUES.has(url.pathname)) limiteur = env.LIMITE_PUBLIQUE;
    // 1.44.0 : le relais des sessions (une attente longue et des envois frequents) a son propre limiteur ; ouvrir une
    // session reste sous celui des PC.
    else if (url.pathname.startsWith("/v1/relais/") && url.pathname !== "/v1/relais/ouvrir") limiteur = env.LIMITE_RELAIS || env.LIMITE_PC;
    else if (url.pathname.startsWith("/v1/")) limiteur = env.LIMITE_PC;
    if (!limiteur || typeof limiteur.limit !== "function") return true;
    try {
        const { success } = await limiteur.limit({ key: ip });
        return success !== false;
    } catch {
        return true; // le limiteur en panne : l'objet garde ses propres limites
    }
}

export default {
    async fetch(request, env) {
        try {
            const url = new URL(request.url);
            const ip = request.headers.get("CF-Connecting-IP") || "inconnue";
            if (!(await passe(env, url, ip)))
                throw new Erreur(429, "trop", "Trop d'appels de ton adresse : réessaie dans une minute.", { reessayer_dans: 60 });
            const methode = request.method.toUpperCase();
            const relais = url.pathname.startsWith("/v1/relais/");
            // 1.44.0 : un lot d'une restauration peut porter une ligne plus grosse qu'une page de la sauvegarde ; un envoi
            // du relais, 1,5 Mio en base64.
            // 1.46.0 : un evenement de Stripe (le webhook), 256 Kio au plus.
            const max =
                url.pathname === "/admin/restauration/lot"
                    ? CORPS_MAX_RESTAURATION
                    : relais
                      ? CORPS_MAX_RELAIS
                      : url.pathname === "/v1/boutique/stripe"
                        ? MAX_CORPS_WEBHOOK
                        : CORPS_MAX;
            const corps = methode === "GET" || methode === "HEAD" ? null : await lireCorps(request, max);
            const entetes = new Headers(request.headers);
            // L'adresse de l'appelant : celle que Cloudflare donne (jamais un en-tete pose par le client).
            entetes.set("X-XPG-IP", ip);
            if (relais) {
                // 1.44.0 : l'objet de CETTE session (le localisateur : dans l'adresse, ou dans le corps pour ouvrir).
                let json = null;
                if (url.pathname === "/v1/relais/ouvrir" && corps && corps.length) {
                    try {
                        json = JSON.parse(dec.decode(corps));
                    } catch {
                        throw new Erreur(400, "json", "Le corps de l'appel n'est pas un JSON lisible.");
                    }
                }
                const loc = localisateurDe(url, json);
                if (!loc) {
                    if (url.pathname === "/v1/relais/ouvrir")
                        throw new Erreur(400, "champ", "Le champ « locator » manque ou est mal écrit (32 hexadécimaux minuscules).");
                    throw new Erreur(404, "route", "Cette adresse n'existe pas sur le relais des sessions.");
                }
                if (!env.RELAIS) throw new Erreur(503, "relais", "Le relais des sessions par Internet n'est pas en service sur ce service des licences.");
                const objetRelais = env.RELAIS.get(env.RELAIS.idFromName(loc));
                return await objetRelais.fetch(new Request(request.url, { method: methode, headers: entetes, body: corps }));
            }
            // 1.47.0 : le lieu de l'appel (le globe) - pose ici seulement, jamais par le client.
            entetes.delete("X-XPG-Lieu");
            const lieu = lieuDeLAppel(request, url);
            entetes.delete("X-XPG-Lieu-Essai");
            if (lieu) entetes.set("X-XPG-Lieu", lieu);
            const objet = env.LICENCES.get(env.LICENCES.idFromName("principal"));
            return await objet.fetch(new Request(request.url, { method: methode, headers: entetes, body: corps }));
        } catch (e) {
            if (e instanceof Erreur) return reponseErreur(e);
            return reponseErreur(new Erreur(500, "interne", "Le service a rencontré une erreur interne. Réessaie plus tard."));
        }
    },
};
