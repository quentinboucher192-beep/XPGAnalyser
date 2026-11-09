// =============================================================================
//  service/src/fichiers.js - 1.44.0 : LES FICHIERS, LES MESSAGES A UN PC, LES
//  VERSIONS (docs/ADMINS.md, § 6)
//  - un fichier (un installateur...) est depose par l'admin+ en morceaux de
//    1 Mio : son manifeste (genre << fichier >>, signe par la cle de l'editeur),
//    puis ses morceaux ; le service le dit complet quand tous sont la et que
//    leur SHA-256 est celui du manifeste. Le lire : public, << admins >> (un
//    appel signe par un admin, ou son jeton d'admin : genre << jetonadmin >>),
//    ou << jeton >> (un lien tire par l'admin+, garde hache, 48 h au plus,
//    << une seule fois >> possible) ;
//  - un message a un PC (genre << message >>, signe par la cle de l'editeur,
//    son contenu scelle pour le PC) : le Studio relève les siens ;
//  - une version (genre << version >>, signee par la cle de l'editeur) : la plus
//    recente est publique ; ses fichiers doivent etre deposes, entiers.
// -----------------------------------------------------------------------------
//  LES TABLES
//    fichiers         le manifeste (texte) et ce qu'il dit ; recus (les morceaux
//                     la), complet
//    morceaux         les octets, par (fichier, n) ; n de 0 a morceaux - 1
//    fichiers_jetons  les liens : le SHA-256 du jeton (jamais le jeton), le
//                     fichier, la fin, une_fois
//    messages         numero, poste (deviceId), bloc, recu ; 50 par PC, 30 jours
//    versions         une ligne par version publiee (x, y, z pour l'ordre)
//    logiciels        par cle : le logiciel et la version de ses appels signes
//                     (X-XPG-Logiciel), et quand (licences.js, noterLogiciel)
//  LA PLACE : 1 Gio de morceaux au plus. Les fichiers des 3 dernieres versions
//  sont gardes ; les autres, 30 jours apres leur depot (le menage du jour, et
//  avant chaque nouveau fichier).
// =============================================================================
import { BlocIllisible, ecrireBlocLu, trouverBloc, verifierBloc } from "./blocs.js";
import { DocumentIllisible } from "./documents.js";
import {
    Erreur,
    cleIdDe,
    dec,
    depuisBase64,
    entier,
    estBase64,
    estHex,
    hasardHex,
    hex,
    mal,
    reponseJson,
    reponseTexte,
    sha256Hex,
    unhex,
} from "./outils.js";

const JOUR = 86400;

export const MORCEAU = 1048576; // la taille d'un morceau (le dernier : le reste)
const MAX_TAILLE_FICHIER = 200 * 1024 * 1024;
export const MAX_OCTETS_MORCEAUX = 1024 * 1024 * 1024; // tous les morceaux gardes, au plus
const MAX_FICHIERS = 1000;
const GARDE_FICHIER = 30 * JOUR; // un fichier qu'aucune des 3 dernieres versions ne porte
const VERSIONS_GARDEES = 3;
const MAX_DUREE_LIEN = 48 * 3600;
const MAX_LIENS = 10000;
const MAX_MESSAGES_PC = 50;
export const GARDE_MESSAGE = 30 * JOUR;
const MAX_MESSAGES = 20000;
const MAX_FICHIERS_VERSION = 16;
const DUREE_JETON_ADMIN = 31 * JOUR; // un jeton d'admin (30 jours) : pas plus loin que ca
const MANQUANTS_MAX = 512; // les morceaux manquants rendus, au plus

const SORTES = ["admin", "studios", "autre"];
const ACCES = ["public", "admins", "jeton"];

const ENTETES_OCTETS = { "Content-Type": "application/octet-stream", "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff" };

function objetJson(json, quoi) {
    let v;
    try {
        v = JSON.parse(json);
    } catch {
        throw new DocumentIllisible(quoi + " n'est pas un JSON lisible");
    }
    if (!v || typeof v !== "object" || Array.isArray(v)) throw new DocumentIllisible(quoi + " n'est pas un objet JSON");
    if (v.format !== 1) throw new DocumentIllisible(quoi + " n'est pas du format 1");
    return v;
}

// "X.Y.Z" -> [x, y, z] ; null : illisible.
export function versionEnNombres(s) {
    const m = typeof s === "string" ? /^(\d{1,6})\.(\d{1,6})\.(\d{1,6})$/.exec(s) : null;
    return m ? [Number(m[1]), Number(m[2]), Number(m[3])] : null;
}
export function comparerVersions(a, b) {
    for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

// Un nom de fichier sur : [A-Za-z0-9._ -], 120 caracteres au plus, ni un point ni une espace en tete ou en fin.
function nomSur(s) {
    return typeof s === "string" && /^[A-Za-z0-9_-]([A-Za-z0-9._ -]{0,118}[A-Za-z0-9_-])?$/.test(s);
}

// Le manifeste d'un fichier (genre << fichier >>) : {id, nom, taille, sha256, sorte, version, acces, morceau, morceaux}.
export function lireManifeste(json) {
    const quoi = "le manifeste";
    const v = objetJson(json, quoi);
    if (!estHex(v.id, 32)) throw new DocumentIllisible(quoi + " : l'identifiant est illisible (32 hexadécimaux minuscules)");
    if (!nomSur(v.nom)) throw new DocumentIllisible(quoi + " : le nom est illisible (lettres, chiffres, . _ - et espaces ; 120 caractères au plus)");
    if (!entier(v.taille, 1, MAX_TAILLE_FICHIER)) throw new DocumentIllisible(quoi + " : la taille est illisible (1 octet à 200 Mio)");
    if (!estHex(v.sha256, 64)) throw new DocumentIllisible(quoi + " : l'empreinte SHA-256 est illisible");
    if (!SORTES.includes(v.sorte)) throw new DocumentIllisible(quoi + " : la sorte est inconnue (admin, studios, autre)");
    if (!ACCES.includes(v.acces)) throw new DocumentIllisible(quoi + " : l'accès est inconnu (public, admins, jeton)");
    if (v.morceau !== MORCEAU) throw new DocumentIllisible(quoi + " : un morceau fait " + MORCEAU + " octets");
    if (v.version !== undefined && v.version !== "" && !versionEnNombres(v.version)) throw new DocumentIllisible(quoi + " : la version est illisible (X.Y.Z)");
    if (v.emis !== undefined && !entier(v.emis, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : « emis » est illisible");
    return {
        id: v.id,
        nom: v.nom,
        taille: v.taille,
        sha256: v.sha256,
        sorte: v.sorte,
        version: v.version || "",
        acces: v.acces,
        morceau: v.morceau,
        morceaux: Math.ceil(v.taille / v.morceau),
    };
}

// Une version (genre << version >>) : {version, nombres, publiee, fichiers: [{id, nom, taille, sha256, sorte, pour}]}.
export function lireVersionDoc(json) {
    const quoi = "la version";
    const v = objetJson(json, quoi);
    const nombres = versionEnNombres(v.version);
    if (!nombres) throw new DocumentIllisible(quoi + " : le numéro est illisible (X.Y.Z)");
    if (!entier(v.publiee, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : « publiee » est illisible");
    if (v.importante !== undefined && typeof v.importante !== "boolean") throw new DocumentIllisible(quoi + " : « importante » est illisible");
    if (v.notes !== undefined && (!Array.isArray(v.notes) || v.notes.length > 100 || !v.notes.every((n) => typeof n === "string" && n.length <= 2000)))
        throw new DocumentIllisible(quoi + " : les notes sont illisibles");
    if (!Array.isArray(v.fichiers) || v.fichiers.length > MAX_FICHIERS_VERSION) throw new DocumentIllisible(quoi + " : la liste des fichiers est illisible");
    const ids = new Set();
    const fichiers = v.fichiers.map((f, i) => {
        const ou = quoi + " : le fichier n° " + (i + 1);
        if (!f || typeof f !== "object" || Array.isArray(f)) throw new DocumentIllisible(ou + " n'est pas un objet");
        if (!estHex(f.id, 32) || ids.has(f.id)) throw new DocumentIllisible(ou + " : son identifiant est illisible (ou deux fois)");
        ids.add(f.id);
        if (!nomSur(f.nom)) throw new DocumentIllisible(ou + " : son nom est illisible");
        if (!entier(f.taille, 1, MAX_TAILLE_FICHIER)) throw new DocumentIllisible(ou + " : sa taille est illisible");
        if (!estHex(f.sha256, 64)) throw new DocumentIllisible(ou + " : son empreinte est illisible");
        if (!SORTES.includes(f.sorte)) throw new DocumentIllisible(ou + " : sa sorte est inconnue");
        if (f.pour !== undefined && (typeof f.pour !== "string" || f.pour.length > 20)) throw new DocumentIllisible(ou + " : « pour » est illisible");
        return { id: f.id, nom: f.nom, taille: f.taille, sha256: f.sha256, sorte: f.sorte, pour: f.pour || "" };
    });
    return { version: v.version, nombres, publiee: v.publiee, fichiers };
}

// Un message a un PC (genre << message >>) : {poste}.
export function lireMessageDoc(json) {
    const quoi = "le message";
    const v = objetJson(json, quoi);
    if (!estHex(v.poste, 32)) throw new DocumentIllisible(quoi + " : l'identifiant du poste est illisible");
    if (v.titre !== undefined && (typeof v.titre !== "string" || v.titre.length > 300)) throw new DocumentIllisible(quoi + " : le titre est illisible");
    if (v.de !== undefined && (typeof v.de !== "string" || v.de.length > 200)) throw new DocumentIllisible(quoi + " : « de » est illisible");
    if (v.emis !== undefined && !entier(v.emis, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : « emis » est illisible");
    if (!estBase64(v.scelle, 60 * 1024)) throw new DocumentIllisible(quoi + " : le scellé est illisible (base64)");
    return { poste: v.poste };
}

// Le jeton d'un admin (genre << jetonadmin >>, signe par sa cle) : {cle, poste, expire}.
export function lireJetonAdmin(json) {
    const quoi = "le jeton d'admin";
    let v;
    try {
        v = JSON.parse(json);
    } catch {
        throw new DocumentIllisible(quoi + " n'est pas un JSON lisible");
    }
    if (!v || typeof v !== "object" || Array.isArray(v)) throw new DocumentIllisible(quoi + " n'est pas un objet JSON");
    if (!estHex(v.cle, 64)) throw new DocumentIllisible(quoi + " : la clé est illisible");
    if (v.poste !== undefined && v.poste !== "" && !estHex(v.poste, 32)) throw new DocumentIllisible(quoi + " : le poste est illisible");
    if (!entier(v.expire, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : « expire » est illisible");
    return { cle: v.cle, poste: v.poste || "", expire: v.expire };
}

function idDe(ctx) {
    const id = ctx.url.searchParams.get("id");
    if (!estHex(id, 32)) throw mal("id", "« id » doit être l'identifiant du fichier (32 hexadécimaux minuscules).");
    return id;
}
function numeroMorceau(ctx, f) {
    const n = ctx.url.searchParams.get("n");
    if (n === null || !/^\d{1,6}$/.test(n)) throw mal("n", "« n » doit être le numéro du morceau (0, 1, 2…).");
    if (Number(n) >= f.morceaux) throw new Erreur(400, "n", "Ce fichier n'a que " + f.morceaux + " morceaux (numérotés de 0 à " + (f.morceaux - 1) + ").");
    return Number(n);
}
const tailleMorceau = (f, n) => (n < f.morceaux - 1 ? f.morceau : f.taille - (f.morceaux - 1) * f.morceau);

const pasDeFichier = () => new Erreur(404, "fichier", "Ce fichier n'est pas (ou pas encore entier) sur le service.");
const lienMort = () => new Erreur(403, "jeton", "Ce lien n'est pas (ou plus) valable.");

// Les methodes ajoutees au Durable Object (licences.js).
export const RoutesFichiers = {
    // ============================================================ les fichiers ==
    // Les morceaux manquants d'un fichier (MANQUANTS_MAX au plus).
    manquants(f) {
        const la = new Set(this.lignes("SELECT n FROM morceaux WHERE fichier = ?", f.id).map((r) => r.n));
        const out = [];
        for (let n = 0; n < f.morceaux && out.length < MANQUANTS_MAX; n++) if (!la.has(n)) out.push(n);
        return out;
    },

    descriptionFichier(f) {
        return { id: f.id, morceaux: f.morceaux, recus: f.recus, complet: !!f.complet, manquants: f.complet ? [] : this.manquants(f) };
    },

    // Effacer un fichier, ses morceaux et ses liens.
    effacerFichier(id) {
        const o = this.nombre("SELECT COALESCE(SUM(LENGTH(octets)), 0) AS o FROM morceaux WHERE fichier = ?", id);
        this.x("DELETE FROM morceaux WHERE fichier = ?", id);
        this.x("DELETE FROM fichiers_jetons WHERE fichier = ?", id);
        this.x("DELETE FROM fichiers WHERE id = ?", id);
        this.ajouter("octets_morceaux", -o);
    },

    // Le menage des fichiers : ceux des 3 dernieres versions sont gardes ; les autres, 30 jours apres leur depot.
    purgerFichiers(now) {
        const garder = new Set();
        for (const v of this.lignes("SELECT fichiers FROM versions ORDER BY x DESC, y DESC, z DESC LIMIT ?", VERSIONS_GARDEES))
            for (const f of JSON.parse(v.fichiers)) garder.add(f.id);
        for (const f of this.lignes("SELECT id FROM fichiers WHERE cree < ?", now - GARDE_FICHIER)) if (!garder.has(f.id)) this.effacerFichier(f.id);
        this.x("DELETE FROM fichiers_jetons WHERE expire < ?", now);
    },

    // POST /admin/fichiers (admin+) {"manifeste":"<bloc fichier>"} -> 201 {id, morceaux, recus, complet, manquants}.
    // Le meme manifeste de nouveau : 200 et ce qui manque (un envoi repris).
    async deposerManifeste(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocEditeur(corps.manifeste, "fichier", "manifeste");
        let m;
        try {
            m = lireManifeste(bloc.json);
        } catch (e) {
            if (!(e instanceof DocumentIllisible)) throw e;
            throw new Erreur(400, "document", "Le manifeste est illisible : " + e.message + ".");
        }
        const texte = ecrireBlocLu(bloc);
        const avant = this.ligne("SELECT * FROM fichiers WHERE id = ?", m.id);
        if (avant) {
            if (avant.manifeste !== texte) throw new Erreur(409, "existe", "Un autre fichier porte déjà cet identifiant : tire un identifiant neuf.");
            return reponseJson(200, this.descriptionFichier(avant));
        }
        this.purgerFichiers(ctx.now);
        if (this.nombre("SELECT COUNT(*) AS n FROM fichiers") >= MAX_FICHIERS) throw new Erreur(507, "limite", "Le service garde déjà trop de fichiers.");
        if (this.compte("octets_morceaux") + m.taille > MAX_OCTETS_MORCEAUX)
            throw new Erreur(507, "limite", "Le service n'a plus la place pour ce fichier (1 Gio de fichiers au plus) : les fichiers des anciennes versions partent 30 jours après leur dépôt.");
        this.x(
            `INSERT INTO fichiers (id, manifeste, nom, taille, sha256, sorte, version, acces, morceau, morceaux, recus, complet, cree)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, ?)`,
            m.id,
            texte,
            m.nom,
            m.taille,
            m.sha256,
            m.sorte,
            m.version,
            m.acces,
            m.morceau,
            m.morceaux,
            ctx.now,
        );
        return reponseJson(201, this.descriptionFichier(this.ligne("SELECT * FROM fichiers WHERE id = ?", m.id)));
    },

    // Le SHA-256 d'un fichier entier, morceau par morceau (DigestStream : jamais tout le fichier en memoire) ; null :
    // le runtime ne sait pas le faire ; "" : un morceau manque.
    async empreinteFichier(f) {
        if (typeof crypto.DigestStream !== "function") return null;
        const flux = new crypto.DigestStream("SHA-256");
        const w = flux.getWriter();
        for (let n = 0; n < f.morceaux; n++) {
            const r = this.ligne("SELECT octets FROM morceaux WHERE fichier = ? AND n = ?", f.id, n);
            if (!r) {
                await w.abort();
                return "";
            }
            await w.write(new Uint8Array(r.octets));
        }
        await w.close();
        return hex(new Uint8Array(await flux.digest));
    },

    // PUT /admin/fichiers/morceau?id=…&n=… (admin+) ; le corps : les octets du morceau n (0 a morceaux - 1 ; chacun
    // fait 1 Mio, le dernier le reste). Le dernier arrive : le SHA-256 du fichier est verifie ; faux, ses morceaux sont
    // effaces (409 empreinte : tout est a renvoyer).
    async deposerMorceau(ctx) {
        const id = idDe(ctx);
        const f = this.ligne("SELECT * FROM fichiers WHERE id = ?", id);
        if (!f) throw new Erreur(404, "fichier", "Ce fichier n'a pas de manifeste sur le service : dépose-le d'abord (POST /admin/fichiers).");
        const n = numeroMorceau(ctx, f);
        const attendu = tailleMorceau(f, n);
        if (ctx.corps.length !== attendu) throw new Erreur(400, "taille", "Le morceau " + n + " doit faire " + attendu + " octets (il en fait " + ctx.corps.length + ").");
        if (f.complet) return reponseJson(200, this.descriptionFichier(f));
        const avant = this.ligne("SELECT LENGTH(octets) AS o FROM morceaux WHERE fichier = ? AND n = ?", id, n);
        const d = ctx.corps.length - (avant ? avant.o : 0);
        if (this.compte("octets_morceaux") + d > MAX_OCTETS_MORCEAUX) throw new Erreur(507, "limite", "Le service n'a plus la place pour ce fichier (1 Gio de fichiers au plus).");
        this.x("INSERT OR REPLACE INTO morceaux (fichier, n, octets) VALUES (?, ?, ?)", id, n, ctx.corps);
        this.ajouter("octets_morceaux", d);
        if (!avant) this.x("UPDATE fichiers SET recus = recus + 1 WHERE id = ?", id);
        const g = this.ligne("SELECT * FROM fichiers WHERE id = ?", id);
        if (g.recus >= g.morceaux) {
            const e = await this.empreinteFichier(g);
            if (e === "" || (e !== null && e !== g.sha256)) {
                const o = this.nombre("SELECT COALESCE(SUM(LENGTH(octets)), 0) AS o FROM morceaux WHERE fichier = ?", id);
                this.x("DELETE FROM morceaux WHERE fichier = ?", id);
                this.x("UPDATE fichiers SET recus = 0 WHERE id = ?", id);
                this.ajouter("octets_morceaux", -o);
                throw new Erreur(409, "empreinte", "Le fichier reçu n'a pas le SHA-256 de son manifeste : ses morceaux sont effacés, renvoie-les.");
            }
            this.x("UPDATE fichiers SET complet = 1 WHERE id = ?", id);
            g.complet = 1;
        }
        return reponseJson(200, this.descriptionFichier(g));
    },

    // POST /admin/fichiers/jeton (admin+) {"id","duree":<s, 48 h au plus>,"une_fois":bool} -> {jeton, expire} : un lien
    // (GET /v1/fichiers/lien?jeton=…, ou ?jeton= sur les morceaux). Le service n'en garde que le SHA-256.
    async tirerJetonFichier(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.id, 32)) throw mal("id");
        if (!entier(corps.duree, 60, MAX_DUREE_LIEN)) throw mal("duree", "Le champ « duree » doit être un nombre de secondes, de 60 à 172800 (48 h).");
        if (corps.une_fois !== undefined && typeof corps.une_fois !== "boolean") throw mal("une_fois");
        const f = this.ligne("SELECT complet FROM fichiers WHERE id = ?", corps.id);
        if (!f || !f.complet) throw pasDeFichier();
        if (this.nombre("SELECT COUNT(*) AS n FROM fichiers_jetons WHERE expire >= ?", ctx.now) >= MAX_LIENS)
            throw new Erreur(507, "limite", "Trop de liens sont en cours : réessaie plus tard.");
        const jeton = hasardHex(16);
        const expire = ctx.now + corps.duree;
        this.x(
            "INSERT INTO fichiers_jetons (hash, fichier, expire, une_fois, cree) VALUES (?, ?, ?, ?, ?)",
            await sha256Hex(jeton),
            corps.id,
            expire,
            corps.une_fois ? 1 : 0,
            ctx.now,
        );
        return reponseJson(201, { jeton, expire });
    },

    // Le lien d'un jeton (?jeton=) pour ce fichier ; leve 403 jeton.
    async lien(ctx, id) {
        const j = ctx.url.searchParams.get("jeton");
        if (!estHex(j, 32)) throw lienMort();
        const hash = await sha256Hex(j);
        const r = this.ligne("SELECT * FROM fichiers_jetons WHERE hash = ?", hash);
        if (!r || (id && r.fichier !== id) || r.expire < ctx.now) throw lienMort();
        return r;
    },

    // Le jeton d'un admin (l'en-tete X-XPG-Jeton : le bloc << jetonadmin >>, en base64), signe par un admin de la liste
    // (non retire) ou par une cle de l'editeur, pas expire. Leve 403 jeton.
    async verifierJetonAdmin(texte, now) {
        const refus = (pourquoi) => new Erreur(403, "jeton", "Le jeton d'admin est refusé : " + pourquoi + ".");
        const octets = depuisBase64(texte.trim());
        let j;
        let bloc;
        try {
            if (!octets) throw new BlocIllisible("ce n'est pas du base64");
            bloc = trouverBloc(dec.decode(octets), "jetonadmin");
            j = lireJetonAdmin(bloc.json);
        } catch (e) {
            if (e instanceof BlocIllisible || e instanceof DocumentIllisible || e instanceof TypeError) throw refus(e.message);
            throw e;
        }
        if (j.expire <= now) throw refus("il a expiré");
        if (j.expire > now + DUREE_JETON_ADMIN) throw refus("il dure plus de 31 jours");
        const editeur = (await this.clesEditeur()).some((k) => k.hex === j.cle);
        const a = editeur ? null : this.admin(j.cle);
        if (!editeur && (!a || a.retire)) throw refus("sa clé n'est pas celle d'un admin de la liste");
        const cle = unhex(j.cle);
        if (!(await verifierBloc(bloc, cle, await cleIdDe(cle)))) throw refus("il n'est pas signé par sa clé");
    },

    // Qui lit ce fichier ? Un appel signe par un admin ou l'editeur : tout ; ?jeton= : son fichier ; sinon l'acces du
    // manifeste (public ; admins : aussi le jeton d'un admin). Rend le lien (?jeton=) employe, ou null.
    async exigerAccesFichier(ctx, f) {
        const cle = ctx.request.headers.get("X-XPG-Cle");
        if (cle && ((await this.clesEditeur()).some((k) => k.hex === cle) || this.admin(cle))) {
            ctx.appelant = await this.verifierAppel(ctx, "admin");
            this.x("INSERT INTO nonces (nonce, vu) VALUES (?, ?)", ctx.appelant.nonce, ctx.now);
            this.noterLogiciel(ctx);
            return null;
        }
        if (ctx.url.searchParams.get("jeton") !== null) return this.lien(ctx, f.id);
        if (f.acces === "public") return null;
        const jeton = ctx.request.headers.get("X-XPG-Jeton");
        if (jeton && f.acces === "admins") {
            await this.verifierJetonAdmin(jeton, ctx.now);
            return null;
        }
        throw new Erreur(403, "acces", f.acces === "admins" ? "Ce fichier est réservé aux admins." : "Ce fichier ne se lit que par son lien.");
    },

    fichierComplet(id) {
        const f = this.ligne("SELECT * FROM fichiers WHERE id = ?", id);
        if (!f || !f.complet) throw pasDeFichier();
        return f;
    },

    // GET /v1/fichiers/manifeste?id=… (publique) : le manifeste d'un fichier entier.
    async lireManifesteFichier(ctx) {
        return reponseTexte(200, this.fichierComplet(idDe(ctx)).manifeste);
    },

    // GET /v1/fichiers/morceau?id=…&n=… : les octets du morceau n. Un lien << une seule fois >> sert jusqu'au DERNIER
    // morceau (il est alors efface).
    async lireMorceauFichier(ctx) {
        const f = this.fichierComplet(idDe(ctx));
        const n = numeroMorceau(ctx, f);
        const lien = await this.exigerAccesFichier(ctx, f);
        const r = this.ligne("SELECT octets FROM morceaux WHERE fichier = ? AND n = ?", f.id, n);
        if (!r) throw pasDeFichier();
        if (lien && lien.une_fois && n === f.morceaux - 1) this.x("DELETE FROM fichiers_jetons WHERE hash = ?", lien.hash);
        return new Response(r.octets, { status: 200, headers: { ...ENTETES_OCTETS, "Content-Length": String(r.octets.byteLength) } });
    },

    // GET /v1/fichiers/lien?jeton=… : le fichier entier (un navigateur), lu morceau par morceau. Un lien << une seule
    // fois >> est efface des le debut (un second telechargement, meme en meme temps, est refuse).
    async lienFichier(ctx) {
        const lien = await this.lien(ctx, null);
        const f = this.fichierComplet(lien.fichier);
        if (lien.une_fois) this.x("DELETE FROM fichiers_jetons WHERE hash = ?", lien.hash);
        let n = 0;
        const flux = new ReadableStream({
            pull: (controleur) => {
                if (n >= f.morceaux) {
                    controleur.close();
                    return;
                }
                const r = this.ligne("SELECT octets FROM morceaux WHERE fichier = ? AND n = ?", f.id, n);
                if (!r) {
                    controleur.error(new Error("morceau manquant"));
                    return;
                }
                controleur.enqueue(new Uint8Array(r.octets));
                n++;
            },
        });
        return new Response(flux, {
            status: 200,
            headers: { ...ENTETES_OCTETS, "Content-Disposition": 'attachment; filename="' + f.nom + '"', "X-XPG-Taille": String(f.taille) },
        });
    },

    // ============================================================ les messages ==
    // PUT /admin/messages (admin+) {"poste":"<deviceId>","bloc":"<bloc message>"} -> 201 {numero}. 50 messages par
    // PC (les plus anciens partent), 30 jours.
    async deposerMessage(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.poste, 32)) throw mal("poste", "Le champ « poste » doit être l'identifiant du poste (32 hexadécimaux minuscules).");
        const bloc = await this.blocEditeur(corps.bloc, "message", "bloc");
        let m;
        try {
            m = lireMessageDoc(bloc.json);
        } catch (e) {
            if (!(e instanceof DocumentIllisible)) throw e;
            throw new Erreur(400, "document", "Le message est illisible : " + e.message + ".");
        }
        if (m.poste !== corps.poste) throw new Erreur(400, "poste", "Ce message est pour un autre poste.");
        if (this.nombre("SELECT COUNT(*) AS n FROM messages") >= MAX_MESSAGES) {
            this.x("DELETE FROM messages WHERE recu < ?", ctx.now - GARDE_MESSAGE);
            if (this.nombre("SELECT COUNT(*) AS n FROM messages") >= MAX_MESSAGES) throw new Erreur(507, "limite", "Le service garde déjà trop de messages.");
        }
        const numero = this.suivant("numero_message");
        this.x("INSERT INTO messages (numero, poste, bloc, recu) VALUES (?, ?, ?, ?)", numero, corps.poste, ecrireBlocLu(bloc), ctx.now);
        this.x(
            "DELETE FROM messages WHERE poste = ? AND numero NOT IN (SELECT numero FROM messages WHERE poste = ? ORDER BY numero DESC LIMIT ?)",
            corps.poste,
            corps.poste,
            MAX_MESSAGES_PC,
        );
        return reponseJson(201, { numero });
    },

    // GET /v1/messages?depuis=N (signee par la cle du PC) : les messages de CE poste apres N.
    async lireMessages(ctx) {
        const d = ctx.url.searchParams.get("depuis");
        if (d !== null && !/^\d{1,15}$/.test(d)) throw new Erreur(400, "champ", "« depuis » doit être un numéro de message.");
        const messages = this.lignes(
            "SELECT numero, bloc FROM messages WHERE poste = ? AND numero > ? AND recu >= ? ORDER BY numero LIMIT ?",
            ctx.appelant.poste,
            d === null ? 0 : Number(d),
            ctx.now - GARDE_MESSAGE,
            MAX_MESSAGES_PC,
        ).map((r) => ({ numero: r.numero, bloc: r.bloc }));
        return reponseJson(200, { messages });
    },

    // ============================================================ les versions ==
    derniereVersion() {
        return this.ligne("SELECT * FROM versions ORDER BY x DESC, y DESC, z DESC LIMIT 1");
    },

    // PUT /admin/versions (admin+) {"bloc":"<bloc version>"} -> 201 {version} : plus recente que la derniere publiee
    // (le meme bloc de nouveau : 200) ; chaque fichier doit etre depose, entier, avec la taille et le SHA-256 dits.
    async deposerVersion(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocEditeur(corps.bloc, "version", "bloc");
        let v;
        try {
            v = lireVersionDoc(bloc.json);
        } catch (e) {
            if (!(e instanceof DocumentIllisible)) throw e;
            throw new Erreur(400, "document", "La version est illisible : " + e.message + ".");
        }
        const texte = ecrireBlocLu(bloc);
        const d = this.derniereVersion();
        if (d) {
            const c = comparerVersions(v.nombres, [d.x, d.y, d.z]);
            if (c < 0 || (c === 0 && texte !== d.bloc))
                throw new Erreur(409, "ancien", "La version " + v.version + " n'est pas plus récente que la dernière publiée (" + d.version + ").", { version: d.version });
            if (c === 0) return reponseJson(200, { version: v.version });
        }
        for (const f of v.fichiers) {
            const r = this.ligne("SELECT taille, sha256, complet FROM fichiers WHERE id = ?", f.id);
            if (!r || !r.complet) throw new Erreur(409, "fichier", "Le fichier « " + f.nom + " » n'est pas (encore) entier sur le service : envoie-le d'abord.");
            if (r.taille !== f.taille || r.sha256 !== f.sha256) throw new Erreur(409, "fichier", "Le fichier « " + f.nom + " » déposé n'a pas la taille ou le SHA-256 que dit la version.");
        }
        this.x(
            "INSERT INTO versions (version, x, y, z, bloc, publiee, fichiers, recue) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            v.version,
            v.nombres[0],
            v.nombres[1],
            v.nombres[2],
            texte,
            v.publiee,
            JSON.stringify(v.fichiers),
            ctx.now,
        );
        this.ecrireConfig("version_derniere", v.version);
        return reponseJson(201, { version: v.version });
    },

    // GET /v1/version (publique) : le bloc de la derniere version publiee.
    async lireVersionPublique() {
        const d = this.derniereVersion();
        if (!d) throw new Erreur(404, "version", "Aucune version n'est encore publiée sur le service.");
        return reponseTexte(200, d.bloc);
    },

    // GET /admin/versions (admin+) : les versions publiees (la plus recente d'abord ; pour chaque fichier, s'il est
    // encore sur le service), et les logiciels vus (X-XPG-Logiciel) : par version, par logiciel, ceux des admins.
    async listerVersions() {
        const present = new Set(this.lignes("SELECT id FROM fichiers WHERE complet = 1").map((r) => r.id));
        const versions = this.lignes("SELECT * FROM versions ORDER BY x DESC, y DESC, z DESC").map((r) => ({
            version: r.version,
            publiee: r.publiee,
            fichiers: JSON.parse(r.fichiers).map((f) => ({ ...f, present: present.has(f.id) })),
            bloc: r.bloc,
        }));
        const parVersion = {};
        const parLogiciel = {};
        let total = 0;
        for (const r of this.lignes("SELECT logiciel, version, COUNT(*) AS n FROM logiciels GROUP BY logiciel, version ORDER BY logiciel, version")) {
            parVersion[r.version] = (parVersion[r.version] || 0) + r.n;
            (parLogiciel[r.logiciel] = parLogiciel[r.logiciel] || {})[r.version] = r.n;
            total += r.n;
        }
        const cles = [...(await this.clesEditeur()).map((k) => k.hex), ...this.lignes("SELECT cle FROM admins ORDER BY depuis, cle").map((r) => r.cle)];
        const admins = cles.map((cle) => ({ cle, ...this.logicielDe(cle) }));
        return reponseJson(200, { versions, logiciels: { par_version: parVersion, par_logiciel: parLogiciel, admins, total } });
    },
};
