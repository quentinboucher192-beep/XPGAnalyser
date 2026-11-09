// =============================================================================
//  service/src/relais.js - 1.44.0 : LE RELAIS DES SESSIONS PAR INTERNET
//  (docs/SESSIONS.md, << Par Internet >> ; le contrat : docs/SERVICE-LICENCES.md)
// -----------------------------------------------------------------------------
//  Un Durable Object PAR SESSION (idFromName(localisateur)) : il ne fait que
//  passer des octets CHIFFRES DE BOUT EN BOUT entre l'hote et ses equipiers. Le
//  code de la session n'arrive jamais ici : le localisateur est les 16 premiers
//  octets de SHA-256(code), en hexadecimal (32 caracteres). L'echange CPace et
//  le chiffrement (XChaCha20-Poly1305) se font dans les Studios.
//
//  LES ROUTES (des appels signes par la cle du PC, comme les autres /v1) :
//    POST /v1/relais/ouvrir               (l'hote) {locator, nom, hote, projet} -> {ok}
//    POST /v1/relais/<loc>/frapper        (un equipier) -> {conn, nom, hote, projet}
//    POST /v1/relais/<loc>/envoyer        l'hote : {a: conn, donnees} ou {envois: [{a, donnees}...]}
//                                         l'equipier : {conn, a: "hote", donnees}
//    GET  /v1/relais/<loc>/recevoir?depuis=N[&conn=C][&attente=S]   (attente longue : 25 s au plus)
//                                         -> {messages: [{n, de, donnees, cle?, fin?, raison?}], suivant}
//    POST /v1/relais/<loc>/fermer         l'hote : {} (la session, pour tous) ou {conn} (le renvoyer) ;
//                                         l'equipier : {conn, partir: true}
//
//  CE QUI EST GARDE : en memoire, les boites (une pour l'hote, une par connexion) ;
//  dans le stockage de l'objet, la session seulement (sa cle d'hote, son nom, son
//  activite : ecrite a l'ouverture, puis une fois par heure au plus). Un message est
//  efface des que son destinataire l'a lu (depuis=N : tout ce qui est avant N).
//  Une session s'efface 12 h apres sa derniere activite (l'alarme de l'objet, et
//  a chaque appel). Un objet arrete (aucun appel depuis un moment) perd ses
//  boites : les Studios le voient (le compteur de leurs messages chiffres saute).
//
//  LES LIMITES : 8 membres (l'hote fait la regle) et 12 connexions ouvertes ; 1,5 Mio
//  par appel (decode) ; 16 Mio par boite et 32 Mio pour la session (503 au-dela :
//  reessayer) ; frapper : 10 fois en 10 min par cle, 60 fois par heure pour la
//  session ; 900 appels par minute et par cle. Les nonces vus (20 min) sont en
//  memoire : un objet relance refuse les appels dates d'avant son demarrage.
//  RIEN N'EST ECRIT DANS UN JOURNAL : ni un corps, ni une cle.
// =============================================================================
import { DurableObject } from "cloudflare:workers";

import { Erreur, dec, enc, estHex, reponseErreur, reponseJson, sha256Hex, unhex, verifierEd25519 } from "./outils.js";

// Le corps d'un appel au relais : 1,5 Mio decodes, en base64 (2 Mio) et le JSON autour.
export const CORPS_MAX_RELAIS = 2.5 * 1024 * 1024;

const MAX_ENVOI = 1.5 * 1024 * 1024; // octets decodes par appel
const MAX_BOITE = 16 * 1024 * 1024; // octets decodes en attente dans une boite
const MAX_SESSION = 32 * 1024 * 1024; // dans toutes les boites de la session
const MAX_REPONSE = 4 * 1024 * 1024; // caracteres base64 dans une reponse de recevoir (le reste : l'appel suivant)
const ATTENTE_MAX = 25; // secondes
const GARDE = 12 * 3600; // une session sans activite depuis 12 h s'efface
const MAX_CONNEXIONS = 12; // ouvertes en meme temps (8 membres : l'hote fait la regle)
const SILENCE_INVITE = 90; // un equipier qui ne lit plus depuis 90 s est parti
const SILENCE_HOTE = 120; // l'hote qui ne lit plus depuis 120 s : ses equipiers sont prevenus
const GARDE_FERMEE = 120; // une connexion fermee garde sa boite (le dernier message) 2 min
const FENETRE_APPEL = 600;
const DUREE_NONCE = 1200;
const ECRIRE_ACTIVITE = 3600; // l'activite est ecrite une fois par heure au plus
const BALAYAGE = 5; // les silences sont regardes toutes les 5 s au plus
const MAX_NONCES = 200000;

// [nombre, duree en secondes]
const FRAPPER_CLE = [10, 600];
const FRAPPER_SESSION = [60, 3600];
const APPELS_CLE = [900, 60];

const ROUTE = /^\/v1\/relais\/([0-9a-f]{32})\/(frapper|envoyer|recevoir|fermer)$/;

// Le localisateur d'un appel (pour le Worker : l'objet a joindre) ; null : pas une route du relais. `json` : le corps
// lu (ouvrir porte le localisateur dans son corps).
export function localisateurDe(url, json) {
    if (url.pathname === "/v1/relais/ouvrir") return json && estHex(json.locator, 32) ? json.locator : null;
    const m = ROUTE.exec(url.pathname);
    return m ? m[1] : null;
}

// Un texte affichable : une ligne, `max` caracteres au plus.
function texte(v, max) {
    if (typeof v !== "string") return "";
    return Array.from(v.replace(/[\u0000-\u001f\u007f]/g, " ").trim()).slice(0, max).join("");
}

// La taille decodee d'un base64 standard (avec le bourrage) ; -1 : pas du base64.
function tailleBase64(s) {
    if (typeof s !== "string" || s.length === 0 || s.length % 4 !== 0 || !/^[A-Za-z0-9+/]*={0,2}$/.test(s)) return -1;
    const bourrage = s.endsWith("==") ? 2 : s.endsWith("=") ? 1 : 0;
    return (s.length / 4) * 3 - bourrage;
}

function nouvelleBoite() {
    return { messages: [], prochain: 1, octets: 0, attente: new Set() };
}

function secondes(n) {
    return n >= 120 ? Math.ceil(n / 60) + " min" : n + " s";
}

export class Relais extends DurableObject {
    constructor(ctx, env) {
        super(ctx, env);
        this.s = null; // la session (voir ouvrir)
        this.nonces = new Map(); // nonce -> heure (20 min)
        this.debits = new Map(); // les limites, en memoire
        this.debut = 0; // l'heure du premier appel depuis le demarrage de l'objet
        this.dernierBalayage = 0;
        ctx.blockConcurrencyWhile(async () => {
            const m = await ctx.storage.get("session");
            if (m && typeof m === "object") this.s = this.depuisStockage(m);
        });
    }

    // ================================================================ l'horloge ==
    // Comme le service des licences (licences.js) : DECALAGE_HORLOGE_ESSAI seulement pres de 127.0.0.1.
    maintenant(local) {
        const t = Math.floor(Date.now() / 1000);
        if (!local) return t;
        const d = Number.parseInt(String(this.env.DECALAGE_HORLOGE_ESSAI || "0"), 10);
        return t + (Number.isFinite(d) ? d : 0);
    }

    // ================================================================ la session ==
    depuisStockage(m) {
        return {
            locator: String(m.locator || ""),
            hote: String(m.hote || ""),
            nom: String(m.nom || ""),
            hoteNom: String(m.hoteNom || ""),
            projet: String(m.projet || ""),
            ouverte: Number(m.ouverte) || 0,
            vu: Number(m.vu) || 0,
            vuEcrit: Number(m.vu) || 0,
            hoteVu: Number(m.vu) || 0,
            hoteAbsent: false,
            fermee: false,
            raison: "",
            prochainConn: Number(m.prochainConn) || 1,
            hoteBoite: nouvelleBoite(),
            conns: new Map(),
            frappes: [],
        };
    }

    versStockage(s) {
        return { locator: s.locator, hote: s.hote, nom: s.nom, hoteNom: s.hoteNom, projet: s.projet, ouverte: s.ouverte, vu: s.vu, prochainConn: s.prochainConn };
    }

    async ecrire(s) {
        s.vuEcrit = s.vu;
        await this.ctx.storage.put("session", this.versStockage(s));
    }

    // La session efface (expiree ou fermee) : le stockage, l'alarme ; ceux qui attendent sont reveilles.
    async effacer(raison) {
        const s = this.s;
        if (s) {
            s.fermee = true;
            s.raison = raison;
            this.reveiller(s.hoteBoite);
            for (const c of s.conns.values()) {
                c.fermee = c.fermee || this.maintenantMemoire;
                this.reveiller(c.boite);
            }
        }
        this.s = null;
        await this.ctx.storage.deleteAll();
        try {
            await this.ctx.storage.deleteAlarm();
        } catch {
            // pas d'alarme
        }
    }

    async alarm() {
        const s = this.s;
        if (!s) return;
        const t = Math.floor(Date.now() / 1000);
        if (t - s.vu >= GARDE) await this.effacer("la session a expiré (12 h sans activité)");
        else await this.ctx.storage.setAlarm((s.vu + GARDE) * 1000);
    }

    // ================================================================ les boites ==
    deposer(boite, message) {
        message.n = boite.prochain++;
        boite.messages.push(message);
        boite.octets += message.octets || 0;
        this.reveiller(boite);
    }

    reveiller(boite) {
        for (const f of [...boite.attente]) f();
    }

    // Ce qui est avant `depuis` a ete lu : efface.
    acquitter(boite, depuis) {
        while (boite.messages.length && boite.messages[0].n < depuis) {
            const m = boite.messages.shift();
            boite.octets -= m.octets || 0;
        }
    }

    octetsSession(s) {
        let n = s.hoteBoite.octets;
        for (const c of s.conns.values()) n += c.boite.octets;
        return n;
    }

    attendre(boite, sec) {
        if (sec <= 0) return Promise.resolve();
        return new Promise((resolve) => {
            let minuteur = null;
            const fini = () => {
                if (minuteur !== null) clearTimeout(minuteur);
                boite.attente.delete(fini);
                resolve();
            };
            minuteur = setTimeout(fini, sec * 1000);
            boite.attente.add(fini);
        });
    }

    // Une connexion fermee : son equipier recoit la fin (`pourInvite`), l'hote la voit partir (`pourHote`).
    fermerConn(s, c, raison, now, { pourHote, pourInvite }) {
        if (c.fermee) return;
        c.fermee = now;
        if (pourInvite) this.deposer(c.boite, { de: "hote", fin: true, raison, octets: 0 });
        else this.reveiller(c.boite);
        if (pourHote) this.deposer(s.hoteBoite, { de: c.id, cle: c.cle, fin: true, raison, octets: 0 });
    }

    // Les silences (toutes les 5 s au plus) : un equipier qui ne lit plus, l'hote qui ne lit plus ; les connexions
    // fermees depuis 2 min oubliees.
    balayer(s, now) {
        if (now - this.dernierBalayage < BALAYAGE) return;
        this.dernierBalayage = now;
        for (const c of s.conns.values()) {
            if (!c.fermee && c.boite.attente.size === 0 && now - c.vu > SILENCE_INVITE)
                this.fermerConn(s, c, "plus de nouvelles de ce PC depuis " + SILENCE_INVITE + " s", now, { pourHote: true, pourInvite: true });
        }
        if (!s.hoteAbsent && s.hoteBoite.attente.size === 0 && now - s.hoteVu > SILENCE_HOTE) {
            s.hoteAbsent = true;
            for (const c of s.conns.values()) this.fermerConn(s, c, "l'hôte ne répond plus", now, { pourHote: true, pourInvite: true });
        }
        for (const [id, c] of s.conns) if (c.fermee && now - c.fermee > GARDE_FERMEE) s.conns.delete(id);
    }

    // ================================================================ les appels ==
    async fetch(request) {
        const url = new URL(request.url);
        const local = url.hostname === "127.0.0.1" || url.hostname === "localhost";
        const now = this.maintenant(local);
        if (!this.debut) this.debut = now;
        this.maintenantMemoire = now;
        try {
            const methode = request.method.toUpperCase();
            const corps = new Uint8Array(await request.arrayBuffer());
            const limites = !(local && this.env.LIMITES_ESSAI === "sans");
            const ctx = { request, url, methode, corps, now, limites, cle: "" };
            let action;
            if (url.pathname === "/v1/relais/ouvrir") action = "ouvrir";
            else {
                const m = ROUTE.exec(url.pathname);
                if (!m) throw new Erreur(404, "route", "Cette adresse n'existe pas sur le relais des sessions.");
                action = m[2];
            }
            const attendue = action === "recevoir" ? "GET" : "POST";
            if (methode !== attendue) throw new Erreur(405, "methode", "Cette méthode n'est pas permise sur " + url.pathname + ".");
            ctx.cle = await this.verifierAppel(ctx);
            if (limites) this.debit("appels", APPELS_CLE, ctx.cle, now);
            if (this.s && !this.s.fermee && now - this.s.vu >= GARDE) await this.effacer("la session a expiré (12 h sans activité)");
            if (this.s && !this.s.fermee) this.balayer(this.s, now);
            if (action === "ouvrir") return await this.ouvrir(ctx);
            if (action === "frapper") return await this.frapper(ctx);
            if (action === "envoyer") return await this.envoyer(ctx);
            if (action === "recevoir") return await this.recevoir(ctx);
            return await this.fermer(ctx);
        } catch (e) {
            if (e instanceof Erreur) return reponseErreur(e);
            console.error("erreur interne du relais :", e && e.name ? e.name : "?");
            return reponseErreur(new Erreur(500, "interne", "Le relais des sessions a rencontré une erreur interne. Réessaie plus tard."));
        }
    }

    // L'appel signe (le meme schema que le service des licences : X-XPG-Cle, X-XPG-Date, X-XPG-Nonce,
    // X-XPG-Signature). Les nonces sont tenus en memoire (rien n'est ecrit) ; un objet relance refuse un appel date
    // de plus de 60 s avant son demarrage (un appel rejoue ne passe pas). Rend la cle (hexadecimal).
    async verifierAppel(ctx) {
        const h = ctx.request.headers;
        const cle = h.get("X-XPG-Cle");
        const date = h.get("X-XPG-Date");
        const nonce = h.get("X-XPG-Nonce");
        const signature = h.get("X-XPG-Signature");
        if (!cle || !date || !nonce || !signature) throw new Erreur(401, "signature", "Cet appel doit être signé (en-têtes X-XPG-Cle, X-XPG-Date, X-XPG-Nonce, X-XPG-Signature).");
        if (!estHex(cle, 64)) throw new Erreur(401, "signature", "La clé de l'appel est illisible (64 hexadécimaux minuscules).");
        if (!/^\d{1,12}$/.test(date)) throw new Erreur(401, "heure", "L'heure de l'appel est illisible.");
        if (!estHex(nonce, 32)) throw new Erreur(401, "signature", "Le nonce de l'appel est illisible (32 hexadécimaux minuscules).");
        const sig = unhex(signature, 64);
        if (!sig) throw new Erreur(401, "signature", "La signature de l'appel est illisible.");
        const t = Number(date);
        if (Math.abs(t - ctx.now) > FENETRE_APPEL || t < this.debut - 60)
            throw new Erreur(401, "heure", "L'heure de l'appel est trop loin de celle du service : lis l'heure du service (GET /v1/etat) et signe avec elle.", { heure: ctx.now });
        const chemin = ctx.url.pathname + ctx.url.search;
        const message = "XPG-SERVICE-1\n" + ctx.methode + "\n" + chemin + "\n" + date + "\n" + nonce + "\n" + (await sha256Hex(ctx.corps));
        if (!(await verifierEd25519(unhex(cle), enc.encode(message), sig))) throw new Erreur(401, "signature", "La signature de l'appel est fausse.");
        if (this.nonces.has(nonce)) throw new Erreur(401, "rejeu", "Cet appel a déjà été reçu (nonce déjà vu) : il est refusé.");
        if (this.nonces.size >= MAX_NONCES || this.nonces.size % 1000 === 0) {
            for (const [n, vu] of this.nonces) if (vu < ctx.now - DUREE_NONCE - FENETRE_APPEL) this.nonces.delete(n);
        }
        if (this.nonces.size >= MAX_NONCES) throw new Erreur(503, "occupe", "Le relais reçoit trop d'appels : réessaie dans une minute.", { reessayer_dans: 60 });
        this.nonces.set(nonce, ctx.now);
        return cle;
    }

    // Une limite en memoire : [nombre, duree].
    debit(nom, [limite, duree], qui, now) {
        const fin = (Math.floor(now / duree) + 1) * duree;
        const cle = nom + ":" + qui;
        const d = this.debits.get(cle);
        const n = d && d.fin === fin ? d.n : 0;
        if (n >= limite) {
            const attente = Math.max(1, fin - now);
            throw new Erreur(429, "trop", "Trop d'appels « " + nom + " » : réessaie dans " + secondes(attente) + ".", { reessayer_dans: attente });
        }
        this.debits.set(cle, { fin, n: n + 1 });
        if (this.debits.size > 10000) for (const [k, v] of this.debits) if (v.fin < now) this.debits.delete(k);
    }

    corpsJson(ctx) {
        if (!ctx.corps.length) return {};
        let v;
        try {
            v = JSON.parse(dec.decode(ctx.corps));
        } catch {
            throw new Erreur(400, "json", "Le corps de l'appel n'est pas un JSON lisible.");
        }
        if (!v || typeof v !== "object" || Array.isArray(v)) throw new Erreur(400, "json", "Le corps de l'appel doit être un objet JSON.");
        return v;
    }

    // La session ouverte (pas fermee) ; sinon 404.
    session() {
        const s = this.s;
        if (!s || s.fermee)
            throw new Erreur(404, "absente", "Aucune session ouverte avec ce code : vérifie-le (l'hôte te le donne), ou la session est arrêtée.");
        return s;
    }

    // La connexion d'un equipier, a lui (sa cle).
    connDe(s, ctx, conn) {
        if (!Number.isInteger(conn) || conn < 1) throw new Erreur(400, "champ", "Le champ « conn » manque ou est mal écrit.");
        const c = s.conns.get(conn);
        if (!c) throw new Erreur(410, "fermee", "Cette connexion à la session est fermée.");
        if (c.cle !== ctx.cle) throw new Erreur(403, "connexion", "Cette connexion n'est pas celle de ce PC.");
        return c;
    }

    // ---- POST /v1/relais/ouvrir (l'hote) ----
    async ouvrir(ctx) {
        const b = this.corpsJson(ctx);
        if (!estHex(b.locator, 32)) throw new Erreur(400, "champ", "Le champ « locator » manque ou est mal écrit (32 hexadécimaux minuscules).");
        const nom = texte(b.nom, 200);
        if (!nom) throw new Erreur(400, "champ", "Le champ « nom » manque (le nom de la session).");
        const now = ctx.now;
        if (this.s && !this.s.fermee && this.s.hote !== ctx.cle)
            throw new Erreur(409, "pris", "Ce code est déjà celui d'une autre session : démarre de nouveau (un autre code sera tiré).");
        if (ctx.limites) this.debit("ouvrir", FRAPPER_CLE, ctx.cle, now);
        const avant = this.s && !this.s.fermee ? this.s : null;
        if (avant) {
            // Le meme hote rouvre (il a redemarre) : ses equipiers d'avant sont prevenus (leur boite reste lisible 2 min).
            for (const c of avant.conns.values()) this.fermerConn(avant, c, "l'hôte a redémarré la session", now, { pourHote: false, pourInvite: true });
            this.reveiller(avant.hoteBoite);
        }
        this.s = {
            locator: b.locator,
            hote: ctx.cle,
            nom,
            hoteNom: texte(b.hote, 100),
            projet: texte(b.projet, 200),
            ouverte: now,
            vu: now,
            vuEcrit: now,
            hoteVu: now,
            hoteAbsent: false,
            fermee: false,
            raison: "",
            prochainConn: avant ? avant.prochainConn : 1,
            hoteBoite: nouvelleBoite(),
            conns: avant ? avant.conns : new Map(),
            frappes: avant ? avant.frappes : [],
        };
        await this.ecrire(this.s);
        await this.ctx.storage.setAlarm((Math.floor(Date.now() / 1000) + GARDE) * 1000);
        return reponseJson(200, { ok: true, garde: GARDE, max_connexions: MAX_CONNEXIONS });
    }

    // ---- POST /v1/relais/<loc>/frapper (un equipier) ----
    async frapper(ctx) {
        const s = this.session();
        const now = ctx.now;
        if (s.hoteAbsent || (s.hoteBoite.attente.size === 0 && now - s.hoteVu > SILENCE_HOTE))
            throw new Erreur(503, "hote_absent", "L'hôte de cette session ne répond plus (son PC est hors ligne ?) : réessaie plus tard.", { reessayer_dans: 30 });
        if (ctx.limites) {
            this.debit("frapper", FRAPPER_CLE, ctx.cle, now);
            s.frappes = s.frappes.filter((t) => t > now - FRAPPER_SESSION[1]);
            if (s.frappes.length >= FRAPPER_SESSION[0])
                throw new Erreur(429, "trop", "Trop d'entrées demandées dans cette session : réessaie plus tard.", { reessayer_dans: 600 });
            s.frappes.push(now);
        }
        let ouvertes = 0;
        for (const c of s.conns.values()) if (!c.fermee) ++ouvertes;
        if (ouvertes >= MAX_CONNEXIONS) throw new Erreur(409, "pleine", "La session est pleine (8 personnes au plus).");
        const c = { id: s.prochainConn++, cle: ctx.cle, vu: now, fermee: 0, boite: nouvelleBoite() };
        s.conns.set(c.id, c);
        s.vu = now;
        if (now - s.vuEcrit >= ECRIRE_ACTIVITE) await this.ecrire(s);
        else await this.ctx.storage.put("session", this.versStockage(s)); // le numero de la connexion suivante
        return reponseJson(200, { conn: c.id, nom: s.nom, hote: s.hoteNom, projet: s.projet });
    }

    // ---- POST /v1/relais/<loc>/envoyer ----
    async envoyer(ctx) {
        const s = this.session();
        const b = this.corpsJson(ctx);
        const now = ctx.now;
        s.vu = now;
        if (b.conn !== undefined) {
            // L'equipier : vers l'hote.
            const c = this.connDe(s, ctx, b.conn);
            if (c.fermee) throw new Erreur(410, "fermee", "Cette connexion à la session est fermée.");
            if (b.a !== undefined && b.a !== "hote") throw new Erreur(400, "champ", "Un équipier n'envoie qu'à l'hôte (« a » : \"hote\").");
            const octets = tailleBase64(b.donnees);
            if (octets < 0) throw new Erreur(400, "champ", "Le champ « donnees » manque ou n'est pas du base64.");
            if (octets > MAX_ENVOI) throw new Erreur(413, "taille", "Trop de données dans un appel (1,5 Mio au plus).");
            c.vu = now;
            if (s.hoteBoite.octets + octets > MAX_BOITE || this.octetsSession(s) + octets > MAX_SESSION)
                throw new Erreur(503, "pleine", "La boîte de l'hôte est pleine : réessaie dans un instant.", { reessayer_dans: 1 });
            this.deposer(s.hoteBoite, { de: c.id, cle: c.cle, donnees: b.donnees, octets });
            return reponseJson(200, { ok: true });
        }
        // L'hote : vers une connexion, ou plusieurs.
        if (ctx.cle !== s.hote) throw new Erreur(403, "hote", "Seul l'hôte de la session envoie sans « conn ».");
        s.hoteVu = now;
        s.hoteAbsent = false;
        const envois = b.envois !== undefined ? b.envois : [{ a: b.a, donnees: b.donnees }];
        if (!Array.isArray(envois) || envois.length === 0 || envois.length > 64) throw new Erreur(400, "champ", "Le champ « envois » manque ou est mal écrit.");
        let total = 0;
        const lus = [];
        for (const e of envois) {
            if (!e || typeof e !== "object" || !Number.isInteger(e.a) || e.a < 1) throw new Erreur(400, "champ", "Un envoi de l'hôte nomme sa connexion (« a » : un numéro).");
            const octets = tailleBase64(e.donnees);
            if (octets < 0) throw new Erreur(400, "champ", "Le champ « donnees » manque ou n'est pas du base64.");
            total += octets;
            lus.push({ a: e.a, donnees: e.donnees, octets });
        }
        if (total > MAX_ENVOI) throw new Erreur(413, "taille", "Trop de données dans un appel (1,5 Mio au plus).");
        const refuses = [];
        const inconnues = [];
        let session = this.octetsSession(s);
        for (const e of lus) {
            const c = s.conns.get(e.a);
            if (!c || c.fermee) {
                if (!inconnues.includes(e.a)) inconnues.push(e.a);
                continue;
            }
            // Une boite pleine : cet envoi (et les suivants vers elle, l'ordre compte) sont a refaire.
            if (refuses.includes(e.a) || c.boite.octets + e.octets > MAX_BOITE || session + e.octets > MAX_SESSION) {
                if (!refuses.includes(e.a)) refuses.push(e.a);
                continue;
            }
            this.deposer(c.boite, { de: "hote", donnees: e.donnees, octets: e.octets });
            session += e.octets;
        }
        return reponseJson(200, { ok: true, refuses, inconnues, reessayer_dans: refuses.length ? 1 : 0 });
    }

    // ---- GET /v1/relais/<loc>/recevoir?depuis=N[&conn=C][&attente=S] ----
    async recevoir(ctx) {
        const q = ctx.url.searchParams;
        const nombre = (nom, defaut, max) => {
            const v = q.get(nom);
            if (v === null) return defaut;
            if (!/^\d{1,15}$/.test(v) || Number(v) > max) throw new Erreur(400, "champ", "Le paramètre « " + nom + " » est mal écrit.");
            return Number(v);
        };
        let depuis = nombre("depuis", 0, Number.MAX_SAFE_INTEGER);
        const attente = nombre("attente", ATTENTE_MAX, ATTENTE_MAX);
        const conn = q.get("conn") === null ? null : nombre("conn", 0, Number.MAX_SAFE_INTEGER);
        const s0 = this.s;
        if (!s0) throw new Erreur(404, "absente", "Aucune session ouverte avec ce code : vérifie-le (l'hôte te le donne), ou la session est arrêtée.");
        let boite;
        let c = null;
        const marquer = (now) => {
            if (c) c.vu = now;
            else {
                s0.hoteVu = now;
                s0.hoteAbsent = false;
            }
            s0.vu = Math.max(s0.vu, now);
        };
        if (conn !== null) {
            c = s0.conns.get(conn);
            if (!c) throw new Erreur(410, "fermee", "Cette connexion à la session est fermée.");
            if (c.cle !== ctx.cle) throw new Erreur(403, "connexion", "Cette connexion n'est pas celle de ce PC.");
            boite = c.boite;
        } else {
            if (ctx.cle !== s0.hote) throw new Erreur(403, "hote", "Seul l'hôte de la session lit sans « conn ».");
            if (s0.fermee) throw new Erreur(410, "fermee", "La session est fermée.");
            boite = s0.hoteBoite;
        }
        // Un numero plus loin que la boite : l'objet a ete relance (ses boites sont neuves) - tout est a lire.
        if (depuis > boite.prochain) depuis = 0;
        this.acquitter(boite, depuis);
        marquer(ctx.now);
        const ferme = () => (c ? !!c.fermee || this.s !== s0 : s0.fermee || this.s !== s0);
        if (!boite.messages.length && !ferme()) {
            await this.attendre(boite, attente);
            marquer(this.maintenant(ctx.url.hostname === "127.0.0.1" || ctx.url.hostname === "localhost"));
        }
        if (!boite.messages.length && ferme())
            throw new Erreur(410, "fermee", c ? "Cette connexion à la session est fermée." : "La session est fermée.", { raison: s0.raison || "" });
        const messages = [];
        let taille = 0;
        for (const m of boite.messages) {
            const t = (m.donnees || "").length;
            if (messages.length && taille + t > MAX_REPONSE) break;
            taille += t;
            const out = { n: m.n, de: m.de };
            if (m.donnees) out.donnees = m.donnees;
            if (m.cle) out.cle = m.cle;
            if (m.fin) out.fin = true;
            if (m.raison) out.raison = m.raison;
            messages.push(out);
        }
        const suivant = messages.length ? messages[messages.length - 1].n + 1 : Math.max(depuis, boite.messages.length ? boite.messages[0].n : boite.prochain);
        return reponseJson(200, { messages, suivant });
    }

    // ---- POST /v1/relais/<loc>/fermer ----
    async fermer(ctx) {
        const s = this.session();
        const b = this.corpsJson(ctx);
        const now = ctx.now;
        const raison = texte(b.raison, 200);
        if (b.conn !== undefined && b.partir === true) {
            // L'equipier part.
            const c = this.connDe(s, ctx, b.conn);
            if (!c.fermee) {
                c.fermee = now;
                this.deposer(s.hoteBoite, { de: c.id, cle: c.cle, fin: true, raison: raison || "parti", octets: 0 });
            }
            s.conns.delete(c.id);
            this.reveiller(c.boite);
            return reponseJson(200, { ok: true });
        }
        if (ctx.cle !== s.hote) throw new Erreur(403, "hote", "Seul l'hôte ferme la session (ou renvoie un équipier).");
        s.hoteVu = now;
        if (b.conn !== undefined) {
            // L'hote renvoie un equipier (refuse, code faux) : il recoit la fin.
            if (!Number.isInteger(b.conn) || b.conn < 1) throw new Erreur(400, "champ", "Le champ « conn » est mal écrit.");
            const c = s.conns.get(b.conn);
            if (c) this.fermerConn(s, c, raison || "l'hôte a fermé la connexion", now, { pourHote: false, pourInvite: true });
            return reponseJson(200, { ok: true });
        }
        // L'hote arrete : la fin pour tous, la session effacee (les boites restent lisibles 2 min, en memoire).
        for (const c of s.conns.values()) this.fermerConn(s, c, raison || "l'hôte a arrêté la session", now, { pourHote: false, pourInvite: true });
        s.fermee = true;
        s.raison = raison || "l'hôte a arrêté la session";
        this.reveiller(s.hoteBoite);
        await this.ctx.storage.deleteAll();
        try {
            await this.ctx.storage.deleteAlarm();
        } catch {
            // pas d'alarme
        }
        return reponseJson(200, { ok: true });
    }
}
