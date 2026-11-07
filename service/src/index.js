// =============================================================================
//  service/src/index.js - 1.35.0 : LE SERVICE DES LICENCES XPG (le Worker)
// -----------------------------------------------------------------------------
//  Le contrat : docs/SERVICE-LICENCES.md. Le Worker ne garde rien : il lit le
//  corps (2 Mio au plus), ajoute l'adresse de l'appelant (CF-Connecting-IP) et
//  passe tout a UN Durable Object, << principal >>, qui tient les tables SQLite
//  (src/licences.js). Un seul objet : les appels sont traites l'un apres
//  l'autre, les numeros et les places ne se croisent jamais.
// =============================================================================
import { CORPS_MAX, Erreur, reponseErreur } from "./outils.js";

export { Licences } from "./licences.js";

async function lireCorps(request) {
    const annonce = request.headers.get("Content-Length");
    if (annonce !== null && Number(annonce) > CORPS_MAX) throw new Erreur(413, "taille", "Le corps de l'appel est trop gros (2 Mio au plus).");
    if (!request.body) return new Uint8Array(0);
    const lecteur = request.body.getReader();
    const morceaux = [];
    let n = 0;
    for (;;) {
        const { done, value } = await lecteur.read();
        if (done) break;
        n += value.length;
        if (n > CORPS_MAX) {
            try {
                await lecteur.cancel();
            } catch {
                // rien
            }
            throw new Erreur(413, "taille", "Le corps de l'appel est trop gros (2 Mio au plus).");
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

// Les routes publiques (sans signature) : LIMITE_PUBLIQUE ; les autres routes /v1 : LIMITE_PC.
const PUBLIQUES = new Set(["/v1/etat", "/v1/heure", "/v1/certificat", "/v1/registre"]);

// Le limiteur de Cloudflare (par adresse, par minute ; une liaison absente : rien). Faux : trop d'appels.
async function passe(env, url, ip) {
    // LIMITES_ESSAI = "sans" : SEULEMENT sur ce poste (127.0.0.1), pour les essais qui enchainent les appels.
    const local = url.hostname === "127.0.0.1" || url.hostname === "localhost";
    if (local && env.LIMITES_ESSAI === "sans") return true;
    let limiteur = null;
    if (PUBLIQUES.has(url.pathname)) limiteur = env.LIMITE_PUBLIQUE;
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
            const corps = methode === "GET" || methode === "HEAD" ? null : await lireCorps(request);
            const entetes = new Headers(request.headers);
            // L'adresse de l'appelant : celle que Cloudflare donne (jamais un en-tete pose par le client).
            entetes.set("X-XPG-IP", ip);
            const objet = env.LICENCES.get(env.LICENCES.idFromName("principal"));
            return await objet.fetch(new Request(request.url, { method: methode, headers: entetes, body: corps }));
        } catch (e) {
            if (e instanceof Erreur) return reponseErreur(e);
            return reponseErreur(new Erreur(500, "interne", "Le service a rencontré une erreur interne. Réessaie plus tard."));
        }
    },
};
