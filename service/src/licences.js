// =============================================================================
//  service/src/licences.js - 1.35.0 : le Durable Object << principal >> du
//  service des licences (SQLite) : toutes les routes du contrat
//  (docs/SERVICE-LICENCES.md)
// -----------------------------------------------------------------------------
//  LES TABLES
//    config        la cle du service (creee ici au premier appel ; la partie
//                  secrete ne sort jamais), le certificat, le registre, la
//                  liste de revocation (privee), les compteurs (numeros)
//    licences      les licences en ligne : le bloc signe par l'editeur (la
//                  derniere emission), le type, les postes, le bail, l'EMPREINTE
//                  du code (jamais le code), active
//    activations   une ligne par (licence, PC) : numero, bail (jusqu_a), etat
//    jetons        les jetons des licences flottantes (15 min)
//    decouvertes   la premiere fois qu'une machine a ete vue (gardee a vie)
//    demandes      les demandes manuelles (scellees pour l'editeur)
//    postes        l'etat d'un PC (bloc << poste >> signe par l'editeur)
//    archive       l'archive privee de l'editeur (chiffree par lui) et sa version
//    nonces        les nonces vus (20 min : un appel rejoue est refuse)
//    debits        les compteurs des limites (par adresse, par PC)
//    stats         les appels du jour
//
//  LES ECRITURES (le plan gratuit compte les lignes ecrites, 100 000 par jour) :
//  un appel ecrit 2 a 4 petites lignes (statistique, nonce, compteurs de
//  limite) en plus de son travail ; les petites tables sont WITHOUT ROWID (une
//  ecriture par ligne, sans index a part).
//
//  LES APPELS SONT TRAITES UN PAR UN (une file) : un numero, une place, une
//  version de l'archive ne sont jamais donnes deux fois.
//  RIEN N'EST ECRIT DANS UN JOURNAL : ni un corps, ni un code, ni une cle.
// =============================================================================
import { DurableObject } from "cloudflare:workers";

import { BlocIllisible, ecrireBlocLu, lireBlocs, signerBloc, trouverBloc, verifierBloc } from "./blocs.js";
import {
    DocumentIllisible,
    estIdLicence,
    lireCertificat,
    lireDemande,
    lireLicence,
    lirePoste,
    lireRegistre,
    lireRevocations,
    normaliserCode,
    trouverRevocation,
} from "./documents.js";
import {
    ED25519,
    Erreur,
    cleIdDe,
    dec,
    egalConstant,
    enc,
    estHex,
    hex,
    iso,
    jourDe,
    posteDe,
    reponseErreur,
    reponseJson,
    reponseTexte,
    sha256Hex,
    unhex,
    verifierEd25519,
} from "./outils.js";

const VERSION = "1.35.0";
const SCHEMA = 1;
const JOUR = 86400;
const HEURE = 3600;
const FENETRE_APPEL = 600; // +-600 s
const DUREE_NONCE = 1200; // 20 min
const DUREE_JETON = 900; // 15 min
const BAIL_DEFAUT = 30;

const MAX_LICENCES = 10000;
const MAX_ACTIVATIONS = 100000;
const MAX_DEMANDES_ATTENTE = 1000;
const MAX_SCELLE = 256 * 1024;
const GARDE_DEMANDES = 90 * JOUR; // une demande close (annulee, remplacee, traitee) est effacee apres 90 jours
const MAX_DECOUVERTES = 1000000;
const MAX_POSTES = 100000;
const MAX_FICHIER = 1.5 * 1024 * 1024;
const MAX_FICHIERS = 20000;
const MAX_ARCHIVE = 512 * 1024 * 1024;

const DROITS = { activation: "activation", flottante: "flottante", heure: "heure", decouverte: "decouverte" };

// Les limites (le plan gratuit, et les abus) : [nom, nombre, duree en secondes].
const PAR_IP = {
    activer: ["activer", 10, HEURE],
    decouverte: ["decouverte", 10, HEURE],
    heure: ["heure", 120, HEURE],
    demandes: ["demandes", 10, HEURE],
};
const PAR_PC = {
    activer: ["activer", 5, HEURE],
    renouveler: ["renouveler", 30, HEURE],
    jeton: ["jeton", 60, HEURE],
    demandes: ["demandes", 5, JOUR],
};

// signe : "pc" (la cle du PC), "editeur" (une cle de PUBLISHER_KEYS) ; absent : public.
const ROUTES = {
    "GET /v1/etat": { f: "etat" },
    "GET /v1/heure": { f: "heure", ip: PAR_IP.heure },
    "GET /v1/certificat": { f: "lireCertificat" },
    "GET /v1/registre": { f: "lireRegistre" },
    "POST /v1/activer": { f: "activer", signe: "pc", ip: PAR_IP.activer, pc: PAR_PC.activer },
    "POST /v1/renouveler": { f: "renouveler", signe: "pc", pc: PAR_PC.renouveler },
    "POST /v1/liberer": { f: "liberer", signe: "pc" },
    "POST /v1/decouverte": { f: "decouverte", signe: "pc", ip: PAR_IP.decouverte },
    "POST /v1/flottante/jeton": { f: "prendreJeton", signe: "pc", pc: PAR_PC.jeton },
    "POST /v1/flottante/rendre": { f: "rendreJeton", signe: "pc" },
    "POST /v1/demandes": { f: "deposerDemande", signe: "pc", ip: PAR_IP.demandes, pc: PAR_PC.demandes },
    "POST /v1/demandes/annuler": { f: "annulerDemande", signe: "pc" },
    "GET /v1/poste": { f: "lirePoste", signe: "pc" },
    "GET /admin/cle-service": { f: "cleService", signe: "editeur" },
    "PUT /admin/certificat": { f: "deposerCertificat", signe: "editeur" },
    "PUT /admin/registre": { f: "deposerRegistre", signe: "editeur" },
    "PUT /admin/revocations": { f: "deposerRevocations", signe: "editeur" },
    "PUT /admin/licences": { f: "deposerLicence", signe: "editeur" },
    "GET /admin/licences": { f: "listerLicences", signe: "editeur" },
    "POST /admin/activations/retirer": { f: "retirerActivation", signe: "editeur" },
    "GET /admin/demandes": { f: "listerDemandes", signe: "editeur" },
    "POST /admin/demandes/traiter": { f: "traiterDemande", signe: "editeur" },
    "PUT /admin/postes": { f: "deposerPoste", signe: "editeur" },
    "GET /admin/archive": { f: "listerArchive", signe: "editeur" },
    "GET /admin/archive/fichier": { f: "lireFichier", signe: "editeur" },
    "POST /admin/archive": { f: "ecrireArchive", signe: "editeur" },
    "GET /admin/stats": { f: "stats", signe: "editeur" },
};
const CHEMINS = new Set(Object.keys(ROUTES).map((r) => r.slice(r.indexOf(" ") + 1)));

// ---- les erreurs courantes ----
const mal = (champ, phrase) => new Erreur(400, "champ", phrase || "Le champ « " + champ + " » manque ou est mal écrit.");

function dureeTexte(s) {
    if (s < 120) return s + " secondes";
    if (s < 2 * HEURE) return Math.ceil(s / 60) + " minutes";
    return Math.ceil(s / HEURE) + " heures";
}

function entierPositif(v) {
    return typeof v === "number" && Number.isInteger(v) && v >= 0 && v <= Number.MAX_SAFE_INTEGER;
}

// Un nom de PC affichable : une ligne, 100 caracteres au plus, jamais une formule de tableur en tete.
function nomPropre(s) {
    if (typeof s !== "string") return "";
    let n = s.replace(/[\u0000-\u001f\u007f]/g, " ").trim();
    while (n && "=+-@".includes(n[0])) n = n.slice(1).trimStart();
    return Array.from(n).slice(0, 100).join("");
}

function cheminValide(c) {
    return (
        typeof c === "string" &&
        c.length > 0 &&
        enc.encode(c).length <= 255 &&
        !/[\u0000-\u001f\u007f\\]/.test(c) &&
        !c.startsWith("/") &&
        c.split("/").every((p) => p !== "" && p !== "." && p !== "..")
    );
}

export class Licences extends DurableObject {
    constructor(ctx, env) {
        super(ctx, env);
        this.sql = ctx.storage.sql;
        this.file = Promise.resolve();
        this.dernierMenage = 0;
        ctx.blockConcurrencyWhile(async () => {
            this.creerTables();
            await this.chargerCle();
        });
    }

    // ================================================================ la base ==
    creerTables() {
        this.sql.exec(`
            CREATE TABLE IF NOT EXISTS config (cle TEXT PRIMARY KEY, valeur TEXT NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS licences (
                id TEXT PRIMARY KEY, bloc TEXT NOT NULL, emission INTEGER NOT NULL, type TEXT NOT NULL,
                postes_max INTEGER NOT NULL, expire INTEGER NOT NULL, bail_jours INTEGER NOT NULL,
                code_hash TEXT, active INTEGER NOT NULL, maj INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS activations (
                licence TEXT NOT NULL, poste TEXT NOT NULL, nom TEXT NOT NULL, station TEXT NOT NULL, type TEXT NOT NULL,
                numero INTEGER NOT NULL, activee INTEGER NOT NULL, vue INTEGER NOT NULL, emis INTEGER NOT NULL,
                jusqu_a INTEGER NOT NULL, etat TEXT NOT NULL, PRIMARY KEY (licence, poste));
            CREATE INDEX IF NOT EXISTS activations_poste ON activations (poste);
            CREATE TABLE IF NOT EXISTS jetons (
                licence TEXT NOT NULL, poste TEXT NOT NULL, emis INTEGER NOT NULL, jusqu_a INTEGER NOT NULL,
                PRIMARY KEY (licence, poste)) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS decouvertes (machine TEXT PRIMARY KEY, debut INTEGER NOT NULL, poste TEXT NOT NULL, vue INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS demandes (
                numero INTEGER PRIMARY KEY, poste TEXT NOT NULL, cle TEXT NOT NULL, scelle TEXT NOT NULL,
                etat TEXT NOT NULL, cree INTEGER NOT NULL, maj INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS demandes_poste ON demandes (poste, etat);
            CREATE INDEX IF NOT EXISTS demandes_etat ON demandes (etat);
            CREATE TABLE IF NOT EXISTS postes (poste TEXT PRIMARY KEY, bloc TEXT NOT NULL, numero INTEGER NOT NULL, maj INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS archive (
                chemin TEXT PRIMARY KEY, contenu TEXT NOT NULL, sha256 TEXT NOT NULL, taille INTEGER NOT NULL, version INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS nonces (nonce TEXT PRIMARY KEY, vu INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS debits (cle TEXT NOT NULL, fin INTEGER NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (cle, fin)) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS stats (jour TEXT NOT NULL, route TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (jour, route)) WITHOUT ROWID;
        `);
        if (!this.config("schema")) this.ecrireConfig("schema", String(SCHEMA));
    }

    lignes(q, ...a) {
        return this.sql.exec(q, ...a).toArray();
    }
    ligne(q, ...a) {
        const r = this.sql.exec(q, ...a).toArray();
        return r.length ? r[0] : null;
    }
    nombre(q, ...a) {
        const r = this.ligne(q, ...a);
        return r ? Number(Object.values(r)[0]) || 0 : 0;
    }
    config(cle) {
        const r = this.ligne("SELECT valeur FROM config WHERE cle = ?", cle);
        return r ? r.valeur : null;
    }
    ecrireConfig(cle, valeur) {
        if (valeur === null) this.sql.exec("DELETE FROM config WHERE cle = ?", cle);
        else this.sql.exec("INSERT INTO config (cle, valeur) VALUES (?, ?) ON CONFLICT (cle) DO UPDATE SET valeur = excluded.valeur", cle, valeur);
    }
    // Un compteur croissant (les numeros des activations, des demandes, la version de l'archive).
    suivant(nom) {
        const n = Number(this.config(nom) || "0") + 1;
        this.ecrireConfig(nom, String(n));
        return n;
    }

    // ================================================================ les cles ==
    async chargerCle() {
        let prive = this.config("cle_privee");
        let publique = this.config("cle_publique");
        if (!prive || !publique) {
            const paire = await crypto.subtle.generateKey(ED25519, true, ["sign", "verify"]);
            prive = hex(new Uint8Array(await crypto.subtle.exportKey("pkcs8", paire.privateKey)));
            publique = hex(new Uint8Array(await crypto.subtle.exportKey("raw", paire.publicKey)));
            this.ecrireConfig("cle_privee", prive);
            this.ecrireConfig("cle_publique", publique);
        }
        this.clePrivee = await crypto.subtle.importKey("pkcs8", unhex(prive), ED25519, false, ["sign"]);
        this.clePub = publique;
        this.clePubOctets = unhex(publique);
        this.cleId = await cleIdDe(this.clePubOctets);
    }

    async signer(genre, objet) {
        return signerBloc(genre, JSON.stringify(objet), this.cleId, async (m) => new Uint8Array(await crypto.subtle.sign(ED25519, this.clePrivee, m)));
    }

    // Les cles de l'editeur (PUBLISHER_KEYS) : [{hex, octets, id}].
    async clesEditeur() {
        const brut = String(this.env.PUBLISHER_KEYS || "");
        if (this.clesBrut !== brut) {
            const cles = [];
            for (const x of brut.split(",")) {
                const h = x.trim().toLowerCase();
                if (!estHex(h, 64)) continue;
                const octets = unhex(h);
                cles.push({ hex: h, octets, id: await cleIdDe(octets) });
            }
            this.clesBrut = brut;
            this.cles = cles;
        }
        return this.cles;
    }

    async signeParEditeur(bloc) {
        for (const k of await this.clesEditeur()) if (await verifierBloc(bloc, k.octets, k.id)) return true;
        return false;
    }

    // Un bloc signe par l'editeur, lu dans `texte` ; leve une Erreur 400 sinon.
    async blocEditeur(texte, genre, quoi) {
        if (typeof texte !== "string" || !texte) throw mal(quoi);
        let bloc;
        try {
            bloc = trouverBloc(texte, genre);
        } catch (e) {
            throw new Erreur(400, "document", "Le document « " + genre + " » est illisible : " + e.message + ".");
        }
        if (!(await this.signeParEditeur(bloc)))
            throw new Erreur(400, "signature", "Le document « " + genre + " » n'est pas signé par une clé de l'éditeur que ce service connaît (PUBLISHER_KEYS).");
        return bloc;
    }

    // Le certificat en vigueur (signe par l'editeur, pour la cle de ce service, pas expire), ou null.
    async certificat(now) {
        const bloc = this.config("certificat");
        if (!bloc) return null;
        const repere = bloc + "|" + String(this.env.PUBLISHER_KEYS || "");
        if (!this.certCache || this.certCache.repere !== repere) {
            let c = null;
            try {
                const b = trouverBloc(bloc, "service");
                if (await this.signeParEditeur(b)) {
                    const d = lireCertificat(b.json);
                    if (d.cle === this.clePub) c = { bloc, ...d };
                }
            } catch {
                c = null;
            }
            this.certCache = { repere, c };
        }
        const c = this.certCache.c;
        return c && now <= c.expire ? c : null;
    }

    async exigerCertificat(now, droit) {
        const c = await this.certificat(now);
        if (!c) throw new Erreur(503, "certificat", "Le service n'est pas encore certifié par l'éditeur (ou son certificat a expiré). Réessaie plus tard.");
        if (!c.droits.includes(droit)) throw new Erreur(503, "certificat", "Le certificat du service ne lui donne pas le droit « " + droit + " ».");
        return c;
    }

    // La liste de revocation (lue une fois, gardee en memoire).
    revocations() {
        const n = this.config("revocations_numero");
        if (!n) return null;
        if (!this.revCache || this.revCache.numero !== n) {
            this.revCache = { numero: n, liste: JSON.parse(this.config("revocations_liste") || "null") };
        }
        return this.revCache.liste;
    }
    revoquee(licence, poste) {
        return trouverRevocation(this.revocations(), licence.id, poste, licence.emission);
    }

    // ================================================================ l'horloge ==
    // `local` : l'appel vise 127.0.0.1 ou localhost (le service lance sur un poste, pour les essais).
    maintenant(local) {
        const t = Math.floor(Date.now() / 1000);
        // DECALAGE_HORLOGE_ESSAI : SEULEMENT pour les essais (le temps qui passe : un bail, un jeton), et seulement sur
        // ce poste : en ligne, l'adresse n'est jamais 127.0.0.1, la variable n'y fait rien.
        if (!local) return t;
        const d = Number.parseInt(String(this.env.DECALAGE_HORLOGE_ESSAI || "0"), 10);
        return t + (Number.isFinite(d) ? d : 0);
    }

    // ================================================================ les appels ==
    async fetch(request) {
        // Un appel a la fois (voir l'en-tete).
        const suite = this.file.then(() => this.traiter(request));
        this.file = suite.catch(() => {});
        return suite;
    }

    async traiter(request) {
        const url = new URL(request.url);
        const local = url.hostname === "127.0.0.1" || url.hostname === "localhost";
        const now = this.maintenant(local);
        try {
            const methode = request.method.toUpperCase();
            const route = ROUTES[methode + " " + url.pathname];
            if (!route) {
                if (CHEMINS.has(url.pathname)) throw new Erreur(405, "methode", "Cette méthode n'est pas permise sur " + url.pathname + ".");
                throw new Erreur(404, "route", "Cette adresse n'existe pas sur le service des licences.");
            }
            this.menage(now);
            this.compter(now, methode + " " + url.pathname);
            const corps = new Uint8Array(await request.arrayBuffer());
            const ip = request.headers.get("X-XPG-IP") || "inconnue";
            // LIMITES_ESSAI = "sans" : les limites levees, SEULEMENT sur ce poste (les essais qui enchainent les appels).
            const limites = !(local && this.env.LIMITES_ESSAI === "sans");
            if (route.ip && limites) this.debit("ip", route.ip, ip, now);
            const ctx = { request, url, methode, corps, ip, now, appelant: null };
            if (route.signe) ctx.appelant = await this.verifierAppel(ctx, route.signe === "editeur");
            if (route.pc && limites) this.debit("pc", route.pc, ctx.appelant.cle, now);
            return await this[route.f](ctx);
        } catch (e) {
            if (e instanceof Erreur) return reponseErreur(e);
            // Rien du corps ni de l'appel dans le journal : seulement le genre de l'erreur.
            console.error("erreur interne :", e && e.name ? e.name : "?");
            return reponseErreur(new Erreur(500, "interne", "Le service a rencontré une erreur interne. Réessaie plus tard."));
        }
    }

    // Les menages (toutes les 5 minutes au plus) : nonces, jetons, compteurs, statistiques.
    menage(now) {
        if (now - this.dernierMenage < 300) return;
        this.dernierMenage = now;
        this.sql.exec("DELETE FROM nonces WHERE vu < ?", now - DUREE_NONCE - FENETRE_APPEL);
        this.sql.exec("DELETE FROM jetons WHERE jusqu_a < ?", now - HEURE);
        this.sql.exec("DELETE FROM debits WHERE fin < ?", now);
        this.sql.exec("DELETE FROM stats WHERE jour < ?", jourDe(now - 60 * JOUR));
        this.sql.exec("DELETE FROM demandes WHERE etat != 'attente' AND maj < ?", now - GARDE_DEMANDES);
    }

    compter(now, route) {
        this.sql.exec("INSERT INTO stats (jour, route, n) VALUES (?, ?, 1) ON CONFLICT (jour, route) DO UPDATE SET n = n + 1", jourDe(now), route);
    }

    debit(portee, [nom, limite, duree], qui, now) {
        const fin = (Math.floor(now / duree) + 1) * duree;
        const cle = portee + ":" + nom + ":" + qui;
        const r = this.ligne("SELECT n FROM debits WHERE cle = ? AND fin = ?", cle, fin);
        if (r && r.n >= limite) {
            const attente = Math.max(1, fin - now);
            throw new Erreur(429, "trop", "Trop d'appels « " + nom + " » : réessaie dans " + dureeTexte(attente) + ".", { reessayer_dans: attente });
        }
        this.sql.exec("INSERT INTO debits (cle, fin, n) VALUES (?, ?, 1) ON CONFLICT (cle, fin) DO UPDATE SET n = n + 1", cle, fin);
    }

    // L'appel signe : X-XPG-Cle, X-XPG-Date, X-XPG-Nonce, X-XPG-Signature (Service.hpp, verifyRequest).
    async verifierAppel(ctx, admin) {
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
        if (admin && !(await this.clesEditeur()).some((k) => k.hex === cle))
            throw new Erreur(403, "editeur", "Cette clé n'est pas une clé de l'éditeur de ce service (PUBLISHER_KEYS).");
        const t = Number(date);
        if (Math.abs(t - ctx.now) > FENETRE_APPEL)
            throw new Erreur(401, "heure", "L'heure de l'appel est trop loin de celle du service : lis l'heure du service (GET /v1/etat) et signe avec elle.", { heure: ctx.now });
        const chemin = ctx.url.pathname + ctx.url.search;
        const message = "XPG-SERVICE-1\n" + ctx.methode + "\n" + chemin + "\n" + date + "\n" + nonce + "\n" + (await sha256Hex(ctx.corps));
        const octets = unhex(cle);
        if (!(await verifierEd25519(octets, enc.encode(message), sig))) throw new Erreur(401, "signature", "La signature de l'appel est fausse.");
        if (this.ligne("SELECT 1 AS x FROM nonces WHERE nonce = ?", nonce)) throw new Erreur(401, "rejeu", "Cet appel a déjà été reçu (nonce déjà vu) : il est refusé.");
        this.sql.exec("INSERT INTO nonces (nonce, vu) VALUES (?, ?)", nonce, ctx.now);
        return { cle, octets, poste: await posteDe(octets) };
    }

    // Le corps JSON d'un appel (un objet) ; vide : {}.
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

    // ================================================================ les licences ==
    licence(id) {
        const r = this.ligne("SELECT * FROM licences WHERE id = ?", id);
        return r ? this.licenceDe(r) : null;
    }
    licenceDe(r) {
        return {
            id: r.id,
            bloc: r.bloc,
            emission: r.emission,
            type: r.type,
            postesMax: r.postes_max,
            expire: r.expire,
            bail: r.bail_jours,
            codeHash: r.code_hash,
            active: !!r.active,
        };
    }
    // La licence dont l'empreinte du code est `hash` : toutes sont comparees, a temps constant.
    licenceParCode(hash) {
        let trouvee = null;
        for (const r of this.lignes("SELECT id, code_hash FROM licences WHERE code_hash IS NOT NULL")) {
            if (egalConstant(r.code_hash, hash) && !trouvee) trouvee = r.id;
        }
        return trouvee ? this.licence(trouvee) : null;
    }

    // Les places prises d'une licence : les activations actives, dans leur bail, non revoquees (sauf `sauf`).
    placesPrises(lic, now, sauf) {
        let n = 0;
        for (const r of this.lignes("SELECT poste, jusqu_a FROM activations WHERE licence = ? AND etat = 'active'", lic.id))
            if (r.poste !== sauf && r.jusqu_a > now && !this.revoquee(lic, r.poste)) n++;
        return n;
    }

    // Signer l'activation de ce PC (un numero neuf, le bail de la licence a partir de maintenant).
    async signerActivation(lic, poste, station, now) {
        const numero = this.suivant("numero_activation");
        const jusqu = now + lic.bail * JOUR;
        const type = lic.type === "floating" ? "flottante" : "fixe";
        this.sql.exec(
            "UPDATE activations SET numero = ?, emis = ?, jusqu_a = ?, vue = ?, type = ? WHERE licence = ? AND poste = ?",
            numero,
            now,
            jusqu,
            now,
            type,
            lic.id,
            poste,
        );
        const doc = { version: 1, licence: lic.id, emission: iso(lic.emission), poste };
        if (station) doc.station = station;
        doc.type = type;
        doc.numero = numero;
        doc.emis = iso(now);
        doc.jusqu_a = iso(jusqu);
        return { bloc: await this.signer("activation", doc), jusqu };
    }

    // ================================================================ publiques ==
    async etat(ctx) {
        const c = await this.certificat(ctx.now);
        return reponseJson(200, {
            service: "xpg-licences",
            version: VERSION,
            heure: ctx.now,
            cle_service: this.clePub || null,
            certifie: !!c,
            registre: Number(this.config("registre_numero") || "0"),
        });
    }

    async heure(ctx) {
        const n = ctx.url.searchParams.getAll("n");
        if (n.length !== 1 || !estHex(n[0], 32)) throw new Erreur(400, "nonce", "Le nonce « n » doit être fait de 32 hexadécimaux minuscules.");
        await this.exigerCertificat(ctx.now, DROITS.heure);
        return reponseTexte(200, await this.signer("heure", { version: 1, heure: iso(ctx.now), n: n[0] }));
    }

    async lireCertificat() {
        const bloc = this.config("certificat");
        if (!bloc) throw new Erreur(404, "certificat", "Le service n'a pas encore de certificat de l'éditeur.");
        return reponseTexte(200, bloc);
    }

    async lireRegistre() {
        const bloc = this.config("registre");
        if (!bloc) throw new Erreur(404, "registre", "Le service n'a pas encore de registre.");
        return reponseTexte(200, bloc);
    }

    // ================================================================ le PC ==
    async activer(ctx) {
        const corps = this.corpsJson(ctx);
        const { cle, octets } = ctx.appelant;
        if (typeof corps.code !== "string") throw mal("code");
        if (typeof corps.demande !== "string" || corps.demande.length > 64 * 1024) throw new Erreur(400, "demande", "La demande d'activation manque.");
        if (corps.nom !== undefined && typeof corps.nom !== "string") throw mal("nom");
        // La demande : signee par CE PC, pour sa cle.
        let demande;
        try {
            const bloc = trouverBloc(corps.demande, "demande");
            if (!(await verifierBloc(bloc, octets, await cleIdDe(octets)))) throw new DocumentIllisible("elle n'est pas signée par la clé de ce PC");
            demande = lireDemande(bloc.json);
        } catch (e) {
            if (!(e instanceof DocumentIllisible) && !(e instanceof BlocIllisible)) throw e;
            throw new Erreur(400, "demande", "La demande d'activation est refusée : " + e.message + ".");
        }
        if (demande.cle !== cle) throw new Erreur(400, "demande", "La demande d'activation est refusée : elle porte la clé d'un autre PC.");
        const cert = await this.exigerCertificat(ctx.now, DROITS.activation);
        const norm = normaliserCode(corps.code);
        // Toujours le meme travail, que le code soit lisible ou non.
        const hash = await sha256Hex("xpg-code/1|" + (norm || "?"));
        const lic = norm ? this.licenceParCode(hash) : null;
        if (!lic || !lic.active) throw new Erreur(403, "code", "Ce code d'activation n'est pas valable (inconnu ou désactivé). Vérifie-le, ou demande-le à l'éditeur.");
        if (this.revoquee(lic, cle)) throw new Erreur(410, "revoquee", "Cette licence est révoquée : elle ne peut plus être activée.");
        if (lic.expire && ctx.now > lic.expire) throw new Erreur(410, "expiree", "Cette licence a expiré le " + jourDe(lic.expire) + ".");
        const avant = this.ligne("SELECT * FROM activations WHERE licence = ? AND poste = ?", lic.id, cle);
        const tientSaPlace = avant && avant.etat === "active" && avant.jusqu_a > ctx.now;
        if (lic.type !== "floating" && !tientSaPlace && this.placesPrises(lic, ctx.now, cle) >= lic.postesMax)
            throw new Erreur(409, "complet", "Cette licence est déjà activée sur " + lic.postesMax + " PC (le maximum). Libère un PC, ou demande à l'éditeur.");
        if (!avant && this.nombre("SELECT COUNT(*) AS n FROM activations") >= MAX_ACTIVATIONS)
            throw new Erreur(507, "limite", "Le service a atteint son nombre maximal d'activations.");
        const nom = nomPropre(corps.nom !== undefined ? corps.nom : demande.nom);
        const activee = avant && avant.etat === "active" ? avant.activee : ctx.now;
        this.sql.exec(
            `INSERT INTO activations (licence, poste, nom, station, type, numero, activee, vue, emis, jusqu_a, etat)
             VALUES (?, ?, ?, ?, ?, 0, ?, ?, ?, ?, 'active')
             ON CONFLICT (licence, poste) DO UPDATE SET nom = excluded.nom, station = excluded.station, activee = excluded.activee,
                 vue = excluded.vue, etat = 'active'`,
            lic.id,
            cle,
            nom,
            demande.station,
            lic.type === "floating" ? "flottante" : "fixe",
            activee,
            ctx.now,
            ctx.now,
            ctx.now,
        );
        const a = await this.signerActivation(lic, cle, demande.station, ctx.now);
        const message =
            lic.type === "floating"
                ? "La licence flottante " + lic.id + " est activée sur ce PC (bail jusqu'au " + jourDe(a.jusqu) + ")."
                : "La licence " + lic.id + " est activée sur ce PC jusqu'au " + jourDe(a.jusqu) + " ; elle se renouvelle à chaque connexion.";
        return reponseJson(200, { licence: lic.bloc, activation: a.bloc, certificat: cert.bloc, message });
    }

    async renouveler(ctx) {
        this.corpsJson(ctx);
        const { cle, poste } = ctx.appelant;
        const cert = await this.exigerCertificat(ctx.now, DROITS.activation);
        const activations = [];
        const retirees = [];
        let concerne = false;
        const liste = this.revocations();
        for (const r of this.lignes("SELECT * FROM activations WHERE poste = ? ORDER BY licence", cle)) {
            if (liste && liste.revoquees.some((x) => x.licence === r.licence)) concerne = true;
            const lic = this.licence(r.licence);
            if (!lic || r.etat !== "active" || !lic.active || this.revoquee(lic, cle)) {
                retirees.push(r.licence);
                if (lic && this.revoquee(lic, cle)) this.sql.exec("DELETE FROM jetons WHERE licence = ? AND poste = ?", r.licence, cle);
                continue;
            }
            // Un bail echu dont la place a ete reprise par un autre PC : ce PC est retire.
            if (r.jusqu_a <= ctx.now && lic.type !== "floating" && this.placesPrises(lic, ctx.now, cle) >= lic.postesMax) {
                this.sql.exec("UPDATE activations SET etat = 'retiree', vue = ? WHERE licence = ? AND poste = ?", ctx.now, r.licence, cle);
                retirees.push(r.licence);
                continue;
            }
            const a = await this.signerActivation(lic, cle, r.station, ctx.now);
            activations.push({ licence: lic.bloc, activation: a.bloc });
        }
        const etatPoste = this.ligne("SELECT bloc FROM postes WHERE poste = ?", poste);
        return reponseJson(200, {
            activations,
            retirees,
            certificat: cert.bloc,
            revocations: concerne ? this.config("revocations") : null,
            poste: etatPoste ? etatPoste.bloc : null,
        });
    }

    async liberer(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence");
        const { cle } = ctx.appelant;
        this.sql.exec("UPDATE activations SET etat = 'liberee', vue = ? WHERE licence = ? AND poste = ? AND etat = 'active'", ctx.now, corps.licence, cle);
        this.sql.exec("DELETE FROM jetons WHERE licence = ? AND poste = ?", corps.licence, cle);
        return reponseJson(200, {});
    }

    async decouverte(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.machine, 32)) throw mal("machine", "Le champ « machine » doit être fait de 32 hexadécimaux minuscules.");
        await this.exigerCertificat(ctx.now, DROITS.decouverte);
        let r = this.ligne("SELECT debut FROM decouvertes WHERE machine = ?", corps.machine);
        if (!r) {
            if (this.nombre("SELECT COUNT(*) AS n FROM decouvertes") >= MAX_DECOUVERTES)
                throw new Erreur(507, "limite", "Le service a atteint son nombre maximal de Découvertes.");
            this.sql.exec("INSERT INTO decouvertes (machine, debut, poste, vue) VALUES (?, ?, ?, ?)", corps.machine, ctx.now, ctx.appelant.cle, ctx.now);
            r = { debut: ctx.now };
        } else {
            this.sql.exec("UPDATE decouvertes SET vue = ? WHERE machine = ?", ctx.now, corps.machine);
        }
        let jours = Number.parseInt(String(this.env.DECOUVERTE_JOURS || "30"), 10);
        if (!Number.isFinite(jours) || jours < 1 || jours > 366) jours = 30;
        const bloc = await this.signer("decouverte", { version: 1, poste: ctx.appelant.cle, machine: corps.machine, debut: iso(r.debut), jours });
        return reponseTexte(200, bloc);
    }

    async prendreJeton(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence");
        const { cle } = ctx.appelant;
        const lic = this.licence(corps.licence);
        const act = lic ? this.ligne("SELECT * FROM activations WHERE licence = ? AND poste = ?", lic.id, cle) : null;
        if (!lic || !act || act.etat !== "active" || lic.type !== "floating" || !lic.active)
            throw new Erreur(403, "activation", "Ce PC n'a pas d'activation flottante pour cette licence.");
        if (this.revoquee(lic, cle)) throw new Erreur(410, "revoquee", "Cette licence est révoquée.");
        if (lic.expire && ctx.now > lic.expire) throw new Erreur(410, "expiree", "Cette licence a expiré le " + jourDe(lic.expire) + ".");
        if (act.jusqu_a <= ctx.now) throw new Erreur(403, "activation", "Le bail de l'activation de ce PC est échu : renouvelle-la d'abord.");
        await this.exigerCertificat(ctx.now, DROITS.flottante);
        const autres = this.lignes("SELECT jusqu_a FROM jetons WHERE licence = ? AND poste != ? AND jusqu_a > ?", lic.id, cle, ctx.now);
        if (autres.length >= lic.postesMax) {
            const libre = Math.max(1, Math.min(...autres.map((x) => x.jusqu_a)) - ctx.now);
            throw new Erreur(409, "complet", "Les " + lic.postesMax + " jetons de cette licence sont pris : un se libère dans " + dureeTexte(libre) + ".", {
                libre_dans: libre,
            });
        }
        const jusqu = ctx.now + DUREE_JETON;
        this.sql.exec(
            "INSERT INTO jetons (licence, poste, emis, jusqu_a) VALUES (?, ?, ?, ?) ON CONFLICT (licence, poste) DO UPDATE SET emis = excluded.emis, jusqu_a = excluded.jusqu_a",
            lic.id,
            cle,
            ctx.now,
            jusqu,
        );
        this.sql.exec("UPDATE activations SET vue = ? WHERE licence = ? AND poste = ?", ctx.now, lic.id, cle);
        const jeton = await this.signer("jeton", { version: 1, licence: lic.id, poste: cle, emis: iso(ctx.now), jusqu_a: iso(jusqu) });
        return reponseJson(200, { jeton });
    }

    async rendreJeton(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence");
        this.sql.exec("DELETE FROM jetons WHERE licence = ? AND poste = ?", corps.licence, ctx.appelant.cle);
        return reponseJson(200, {});
    }

    async deposerDemande(ctx) {
        const corps = this.corpsJson(ctx);
        if (typeof corps.cle !== "string" || !/^xpg-[0-9a-f]{16}$/.test(corps.cle)) throw mal("cle");
        if (!(await this.clesEditeur()).some((k) => k.id === corps.cle))
            throw new Erreur(400, "cle", "Cette demande est scellée pour une clé que l'éditeur n'emploie pas sur ce service.");
        if (typeof corps.scelle !== "string" || !corps.scelle || corps.scelle.length > MAX_SCELLE || !/^[A-Za-z0-9+/=\r\n]+$/.test(corps.scelle))
            throw mal("scelle", "Le champ « scelle » doit être du base64 (256 Kio au plus).");
        const poste = ctx.appelant.poste;
        const autres = this.nombre("SELECT COUNT(*) AS n FROM demandes WHERE etat = 'attente' AND poste != ?", poste);
        if (autres >= MAX_DEMANDES_ATTENTE) throw new Erreur(507, "limite", "Trop de demandes attendent l'éditeur : réessaie plus tard.");
        this.sql.exec("UPDATE demandes SET etat = 'remplacee', maj = ? WHERE poste = ? AND etat = 'attente'", ctx.now, poste);
        const numero = this.suivant("numero_demande");
        this.sql.exec(
            "INSERT INTO demandes (numero, poste, cle, scelle, etat, cree, maj) VALUES (?, ?, ?, ?, 'attente', ?, ?)",
            numero,
            poste,
            corps.cle,
            corps.scelle,
            ctx.now,
            ctx.now,
        );
        return reponseJson(201, { numero });
    }

    async annulerDemande(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entierPositif(corps.numero) || corps.numero < 1) throw mal("numero");
        const r = this.ligne("SELECT etat FROM demandes WHERE numero = ? AND poste = ?", corps.numero, ctx.appelant.poste);
        if (!r) throw new Erreur(404, "demande", "Cette demande n'existe pas, ou n'est pas celle de ce PC.");
        if (r.etat === "attente") this.sql.exec("UPDATE demandes SET etat = 'annulee', maj = ? WHERE numero = ?", ctx.now, corps.numero);
        else if (r.etat !== "annulee")
            throw new Erreur(409, "etat", r.etat === "traitee" ? "L'éditeur a déjà traité cette demande." : "Cette demande a été remplacée par une plus récente.");
        return reponseJson(200, {});
    }

    async lirePoste(ctx) {
        const r = this.ligne("SELECT bloc FROM postes WHERE poste = ?", ctx.appelant.poste);
        return reponseJson(200, { poste: r ? r.bloc : null });
    }

    // ================================================================ l'editeur ==
    async cleService() {
        return reponseJson(200, { cle: this.clePub, certificat: this.config("certificat") });
    }

    async deposerCertificat(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocEditeur(corps.certificat, "service", "certificat");
        let c;
        try {
            c = lireCertificat(bloc.json);
        } catch (e) {
            throw new Erreur(400, "document", "Le certificat est illisible : " + e.message + ".");
        }
        if (c.cle !== this.clePub) throw new Erreur(409, "cle", "Ce certificat est fait pour une autre clé que celle de ce service : lis la clé du service (GET /admin/cle-service) et signe-la.");
        if (c.expire <= ctx.now) throw new Erreur(400, "expire", "Ce certificat a déjà expiré.");
        this.ecrireConfig("certificat", ecrireBlocLu(bloc));
        this.certCache = null;
        return reponseJson(200, { cle: c.cle, expire: iso(c.expire), droits: c.droits });
    }

    async deposerRegistre(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocEditeur(corps.registre, "registre", "registre");
        let r;
        try {
            r = lireRegistre(bloc.json);
        } catch (e) {
            throw new Erreur(400, "document", "Le registre est illisible : " + e.message + ".");
        }
        const texte = ecrireBlocLu(bloc);
        const actuel = Number(this.config("registre_numero") || "0");
        if (r.numero < actuel || (r.numero === actuel && texte !== this.config("registre")))
            throw new Erreur(409, "ancien", "Ce registre (n° " + r.numero + ") n'est pas plus récent que celui du service (n° " + actuel + ").", { numero: actuel });
        this.ecrireConfig("registre", texte);
        this.ecrireConfig("registre_numero", String(r.numero));
        return reponseJson(200, { registre: r.numero });
    }

    async deposerRevocations(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocEditeur(corps.revocations, "revocations", "revocations");
        let liste;
        try {
            liste = lireRevocations(bloc.json);
        } catch (e) {
            throw new Erreur(400, "document", "La liste de révocation est illisible : " + e.message + ".");
        }
        const texte = ecrireBlocLu(bloc);
        const actuel = Number(this.config("revocations_numero") || "0");
        if (liste.numero < actuel || (liste.numero === actuel && texte !== this.config("revocations")))
            throw new Erreur(409, "ancien", "Cette liste (n° " + liste.numero + ") n'est pas plus récente que celle du service (n° " + actuel + ").", { numero: actuel });
        this.ctx.storage.transactionSync(() => {
            this.ecrireConfig("revocations", texte);
            this.ecrireConfig("revocations_liste", JSON.stringify(liste));
            this.ecrireConfig("revocations_numero", String(liste.numero));
        });
        this.revCache = null;
        // Les jetons des PC revoques sont rendus tout de suite (leurs activations ne sont plus renouvelees).
        for (const j of this.lignes("SELECT licence, poste FROM jetons")) {
            const lic = this.licence(j.licence);
            if (lic && this.revoquee(lic, j.poste)) this.sql.exec("DELETE FROM jetons WHERE licence = ? AND poste = ?", j.licence, j.poste);
        }
        return reponseJson(200, { revocations: liste.numero });
    }

    async deposerLicence(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocEditeur(corps.licence, "licence", "licence");
        let doc;
        try {
            doc = lireLicence(bloc.json);
        } catch (e) {
            throw new Erreur(400, "document", "La licence est illisible : " + e.message + ".");
        }
        if (corps.code_hash !== undefined && corps.code_hash !== null && !estHex(corps.code_hash, 64))
            throw mal("code_hash", "Le champ « code_hash » doit être l'empreinte du code (64 hexadécimaux minuscules), ou null.");
        if (corps.bail_jours !== undefined && !(Number.isInteger(corps.bail_jours) && corps.bail_jours >= 1 && corps.bail_jours <= 3660))
            throw mal("bail_jours", "Le champ « bail_jours » doit être un nombre de jours entre 1 et 3660.");
        if (corps.active !== undefined && typeof corps.active !== "boolean") throw mal("active");
        const avant = this.licence(doc.id);
        if (avant && doc.emission < avant.emission)
            throw new Erreur(409, "ancien", "Cette émission de la licence est plus ancienne que celle du service (" + iso(avant.emission) + ").");
        if (!avant && this.nombre("SELECT COUNT(*) AS n FROM licences") >= MAX_LICENCES)
            throw new Erreur(507, "limite", "Le service a atteint son nombre maximal de licences.");
        const codeHash = corps.code_hash === undefined ? (avant ? avant.codeHash : null) : corps.code_hash;
        if (codeHash && this.ligne("SELECT id FROM licences WHERE code_hash = ? AND id != ?", codeHash, doc.id))
            throw new Erreur(409, "code", "Ce code est déjà celui d'une autre licence : crée un code neuf.");
        const bail = corps.bail_jours !== undefined ? corps.bail_jours : avant ? avant.bail : BAIL_DEFAUT;
        const active = corps.active !== undefined ? corps.active : avant ? avant.active : true;
        this.sql.exec(
            `INSERT INTO licences (id, bloc, emission, type, postes_max, expire, bail_jours, code_hash, active, maj)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
             ON CONFLICT (id) DO UPDATE SET bloc = excluded.bloc, emission = excluded.emission, type = excluded.type,
                 postes_max = excluded.postes_max, expire = excluded.expire, bail_jours = excluded.bail_jours,
                 code_hash = excluded.code_hash, active = excluded.active, maj = excluded.maj`,
            doc.id,
            ecrireBlocLu(bloc),
            doc.emission,
            doc.type,
            doc.postesMax,
            doc.expire,
            bail,
            codeHash,
            active ? 1 : 0,
            ctx.now,
        );
        return reponseJson(200, this.descriptionLicence(this.licence(doc.id), ctx.now));
    }

    descriptionLicence(lic, now) {
        const activations = this.lignes("SELECT * FROM activations WHERE licence = ? ORDER BY activee, poste", lic.id).map((r) => ({
            poste: r.poste,
            nom: r.nom,
            station: r.station,
            activee: iso(r.activee),
            vue: iso(r.vue),
            jusqu_a: iso(r.jusqu_a),
            etat: r.etat === "active" && this.revoquee(lic, r.poste) ? "revoquee" : r.etat,
        }));
        const jetons = this.lignes("SELECT poste, jusqu_a FROM jetons WHERE licence = ? AND jusqu_a > ? ORDER BY jusqu_a", lic.id, now).map((r) => ({
            poste: r.poste,
            jusqu_a: iso(r.jusqu_a),
        }));
        return {
            licence: lic.id,
            emission: iso(lic.emission),
            type: lic.type,
            postes_max: lic.postesMax,
            bail_jours: lic.bail,
            active: lic.active,
            code: !!lic.codeHash,
            activations,
            jetons,
        };
    }

    async listerLicences(ctx) {
        const licences = this.lignes("SELECT * FROM licences ORDER BY id").map((r) => this.descriptionLicence(this.licenceDe(r), ctx.now));
        return reponseJson(200, { licences });
    }

    async retirerActivation(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence");
        if (!estHex(corps.poste, 64)) throw mal("poste", "Le champ « poste » doit être la clé du PC (64 hexadécimaux minuscules).");
        const r = this.ligne("SELECT etat FROM activations WHERE licence = ? AND poste = ?", corps.licence, corps.poste);
        if (!r) throw new Erreur(404, "activation", "Ce PC n'a pas d'activation pour cette licence.");
        this.sql.exec("UPDATE activations SET etat = 'retiree' WHERE licence = ? AND poste = ?", corps.licence, corps.poste);
        this.sql.exec("DELETE FROM jetons WHERE licence = ? AND poste = ?", corps.licence, corps.poste);
        return reponseJson(200, {});
    }

    async listerDemandes(ctx) {
        const d = ctx.url.searchParams.get("depuis");
        if (d !== null && !/^\d{1,15}$/.test(d)) throw new Erreur(400, "champ", "« depuis » doit être un numéro de demande.");
        const depuis = d === null ? 0 : Number(d);
        const demandes = this.lignes("SELECT * FROM demandes WHERE numero > ? ORDER BY numero LIMIT 1000", depuis).map((r) => ({
            numero: r.numero,
            poste: r.poste,
            cle: r.cle,
            scelle: r.scelle,
            etat: r.etat,
            cree: iso(r.cree),
            maj: iso(r.maj),
        }));
        return reponseJson(200, { demandes });
    }

    async traiterDemande(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entierPositif(corps.numero) || corps.numero < 1) throw mal("numero");
        const r = this.ligne("SELECT etat FROM demandes WHERE numero = ?", corps.numero);
        if (!r) throw new Erreur(404, "demande", "Cette demande n'existe pas.");
        if (r.etat === "attente") this.sql.exec("UPDATE demandes SET etat = 'traitee', maj = ? WHERE numero = ?", ctx.now, corps.numero);
        else if (r.etat !== "traitee")
            throw new Erreur(409, "etat", r.etat === "annulee" ? "Le PC a annulé cette demande." : "Le PC a remplacé cette demande par une plus récente.", { etat: r.etat });
        return reponseJson(200, {});
    }

    async deposerPoste(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.poste, 32)) throw mal("poste", "Le champ « poste » doit être l'identifiant du poste (32 hexadécimaux minuscules).");
        const bloc = await this.blocEditeur(corps.etat, "poste", "etat");
        let p;
        try {
            p = lirePoste(bloc.json);
        } catch (e) {
            throw new Erreur(400, "document", "L'état du PC est illisible : " + e.message + ".");
        }
        if (p.poste !== corps.poste) throw new Erreur(400, "poste", "Cet état est celui d'un autre poste.");
        const texte = ecrireBlocLu(bloc);
        const avant = this.ligne("SELECT numero, bloc FROM postes WHERE poste = ?", corps.poste);
        if (avant && (p.numero < avant.numero || (p.numero === avant.numero && texte !== avant.bloc)))
            throw new Erreur(409, "ancien", "Cet état (n° " + p.numero + ") n'est pas plus récent que celui du service (n° " + avant.numero + ").", { numero: avant.numero });
        if (!avant && this.nombre("SELECT COUNT(*) AS n FROM postes") >= MAX_POSTES) throw new Erreur(507, "limite", "Le service a atteint son nombre maximal de postes.");
        this.sql.exec(
            "INSERT INTO postes (poste, bloc, numero, maj) VALUES (?, ?, ?, ?) ON CONFLICT (poste) DO UPDATE SET bloc = excluded.bloc, numero = excluded.numero, maj = excluded.maj",
            corps.poste,
            texte,
            p.numero,
            ctx.now,
        );
        return reponseJson(200, {});
    }

    // ================================================================ l'archive ==
    async listerArchive() {
        const fichiers = this.lignes("SELECT chemin, sha256, taille FROM archive ORDER BY chemin").map((r) => ({ chemin: r.chemin, sha256: r.sha256, taille: r.taille }));
        return reponseJson(200, { version: Number(this.config("archive_version") || "0"), fichiers });
    }

    async lireFichier(ctx) {
        const chemin = ctx.url.searchParams.get("chemin");
        if (!cheminValide(chemin)) throw mal("chemin");
        const r = this.ligne("SELECT contenu FROM archive WHERE chemin = ?", chemin);
        if (!r) throw new Erreur(404, "fichier", "Ce fichier n'est pas dans l'archive.");
        return reponseTexte(200, r.contenu);
    }

    async ecrireArchive(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entierPositif(corps.base)) throw mal("base");
        if (!corps.fichiers || typeof corps.fichiers !== "object" || Array.isArray(corps.fichiers)) throw mal("fichiers");
        const version = Number(this.config("archive_version") || "0");
        if (corps.base !== version)
            throw new Erreur(409, "conflit", "L'archive a changé depuis ta dernière lecture (version " + version + ") : relis-la, puis recommence.", { version });
        const changements = [];
        for (const [chemin, texte] of Object.entries(corps.fichiers)) {
            if (!cheminValide(chemin)) throw new Erreur(400, "chemin", "Un chemin de l'archive est mal écrit.");
            if (texte === null) {
                changements.push({ chemin, texte: null });
                continue;
            }
            if (typeof texte !== "string") throw new Erreur(400, "fichiers", "Le contenu d'un fichier doit être un texte, ou null.");
            const octets = enc.encode(texte);
            if (octets.length > MAX_FICHIER) throw new Erreur(413, "taille", "Le fichier « " + chemin + " » est trop gros (1,5 Mio au plus).");
            changements.push({ chemin, texte, taille: octets.length, sha256: await sha256Hex(octets) });
        }
        const nouvelle = version + 1;
        this.ctx.storage.transactionSync(() => {
            for (const c of changements) {
                if (c.texte === null) this.sql.exec("DELETE FROM archive WHERE chemin = ?", c.chemin);
                else
                    this.sql.exec(
                        "INSERT INTO archive (chemin, contenu, sha256, taille, version) VALUES (?, ?, ?, ?, ?) ON CONFLICT (chemin) DO UPDATE SET contenu = excluded.contenu, sha256 = excluded.sha256, taille = excluded.taille, version = excluded.version",
                        c.chemin,
                        c.texte,
                        c.sha256,
                        c.taille,
                        nouvelle,
                    );
            }
            const t = this.ligne("SELECT COUNT(*) AS n, COALESCE(SUM(taille), 0) AS o FROM archive");
            // trop gros : l'erreur annule la transaction (rien n'est ecrit)
            if (t.n > MAX_FICHIERS || t.o > MAX_ARCHIVE) throw new Erreur(507, "limite", "L'archive serait trop grosse (" + MAX_FICHIERS + " fichiers, 512 Mio au plus).");
            this.ecrireConfig("archive_version", String(nouvelle));
        });
        return reponseJson(200, { version: nouvelle });
    }

    // ================================================================ les compteurs ==
    async stats(ctx) {
        const jour = jourDe(ctx.now);
        const parEtat = (table) => {
            const o = {};
            for (const r of this.lignes(`SELECT etat, COUNT(*) AS n FROM ${table} GROUP BY etat`)) o[r.etat] = r.n;
            return o;
        };
        const activations = { active: 0, liberee: 0, retiree: 0, revoquee: 0, ...parEtat("activations") };
        // Les revoquees (deduites de la liste) ne sont pas des actives.
        let revoquees = 0;
        for (const r of this.lignes("SELECT licence, poste FROM activations WHERE etat = 'active'")) {
            const lic = this.licence(r.licence);
            if (lic && this.revoquee(lic, r.poste)) revoquees++;
        }
        activations.active -= revoquees;
        activations.revoquee += revoquees;
        const routes = {};
        let total = 0;
        for (const r of this.lignes("SELECT route, n FROM stats WHERE jour = ? ORDER BY route", jour)) {
            routes[r.route] = r.n;
            total += r.n;
        }
        const archive = this.ligne("SELECT COUNT(*) AS n, COALESCE(SUM(taille), 0) AS o FROM archive");
        return reponseJson(200, {
            heure: ctx.now,
            licences: {
                total: this.nombre("SELECT COUNT(*) AS n FROM licences"),
                actives: this.nombre("SELECT COUNT(*) AS n FROM licences WHERE active = 1"),
                avec_code: this.nombre("SELECT COUNT(*) AS n FROM licences WHERE code_hash IS NOT NULL"),
            },
            activations,
            jetons: this.nombre("SELECT COUNT(*) AS n FROM jetons WHERE jusqu_a > ?", ctx.now),
            demandes: { attente: 0, annulee: 0, remplacee: 0, traitee: 0, ...parEtat("demandes") },
            decouvertes: this.nombre("SELECT COUNT(*) AS n FROM decouvertes"),
            postes: this.nombre("SELECT COUNT(*) AS n FROM postes"),
            archive: { version: Number(this.config("archive_version") || "0"), fichiers: archive.n, octets: archive.o },
            appels_jour: { jour, total, routes },
        });
    }
}
