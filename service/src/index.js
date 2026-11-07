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

export default {
    async fetch(request, env) {
        try {
            const methode = request.method.toUpperCase();
            const corps = methode === "GET" || methode === "HEAD" ? null : await lireCorps(request);
            const entetes = new Headers(request.headers);
            // L'adresse de l'appelant : celle que Cloudflare donne (jamais un en-tete pose par le client).
            entetes.set("X-XPG-IP", request.headers.get("CF-Connecting-IP") || "inconnue");
            const objet = env.LICENCES.get(env.LICENCES.idFromName("principal"));
            return await objet.fetch(new Request(request.url, { method: methode, headers: entetes, body: corps }));
        } catch (e) {
            if (e instanceof Erreur) return reponseErreur(e);
            return reponseErreur(new Erreur(500, "interne", "Le service a rencontré une erreur interne. Réessaie plus tard."));
        }
    },
};
