// =============================================================================
//  service/src/sauvegarde.js - 1.44.0 : LA SAUVEGARDE ET LA RESTAURATION DU
//  SERVICE (Cloudflare en panne, un autre compte : tout revient, a l'identique)
// -----------------------------------------------------------------------------
//  GET /admin/sauvegarde (admin+) : le sommaire (les tables, leurs lignes) ; une
//  ligne << sauvegarde >> au journal. GET /admin/sauvegarde?table=t&depuis=c :
//  les lignes EXACTES de la table t, par pages de 1,5 Mio au plus (une ligne plus
//  grosse part seule), dans l'ordre de sa cle ; `suivant` : le curseur de la page
//  suivante (null : fini). Une valeur binaire : {"$b64":"<base64>"}.
//  Toutes les tables gardees : config (la cle du service comprise : la sauvegarde
//  est un secret), licences, activations, ..., journal, fichiers (le manifeste,
//  pas les octets), messages, versions, logiciels, stats_jour. Pas : nonces,
//  debits (des caches des limites), morceaux (les octets des fichiers : l'admin+
//  les renvoie, il les a).
//  La restauration (admin+) : POST /admin/restauration/debut {schema, remplacer}
//  (vide les tables ; le service ne repond plus qu'a la restauration et a la
//  sauvegarde : 503 << restauration >>), POST /admin/restauration/lot {table,
//  lignes} (les lignes, telles quelles), POST /admin/restauration/fin (la cle du
//  service relue, les fichiers sans octets marques incomplets, une ligne
//  << restauration >> au journal).
//  1.47.0 : les tables des statistiques et du globe (stats.js : les faits, les
//  releves, les compteurs, les versions vues, les lieux). Une sauvegarde du schema
//  3 ou 4 : elles restent vides, les versions vues sont recopiees des logiciels.
// =============================================================================
import { TABLES_SAUVEGARDE_BOUTIQUE } from "./boutique.js";
import { TABLES_SAUVEGARDE_STATS } from "./stats.js";
import { Erreur, depuisBase64, enc, entier, mal, reponseJson, versBase64 } from "./outils.js";

// Les tables sauvegardees et leur cle (l'ordre des pages). L'ordre de la liste : celui de la restauration.
export const TABLES_SAUVEGARDE = [
    ["config", ["cle"]],
    ["licences", ["id"]],
    ["activations", ["licence", "poste"]],
    ["liberations", ["licence", "t", "poste"]],
    ["jetons", ["licence", "poste", "station"]],
    ["decouvertes", ["machine"]],
    ["demandes", ["numero"]],
    ["demandes_scelles", ["numero", "cle"]],
    ["postes", ["poste"]],
    ["archive", ["chemin"]],
    ["stats_jour", ["jour"]],
    ["admins", ["cle"]],
    ["admins_demandes", ["cle"]],
    ["trousseau", ["cle"]],
    ["journal", ["numero"]],
    ["fichiers", ["id"]],
    ["fichiers_jetons", ["hash"]],
    ["messages", ["numero"]],
    ["versions", ["version"]],
    ["logiciels", ["cle"]],
    // 1.46.0 : la boutique (le catalogue), les commandes et leurs scelles, les evenements de Stripe deja traites - jamais
    // les secrets de Stripe (des secrets du Worker, dans aucune table).
    ...TABLES_SAUVEGARDE_BOUTIQUE,
    // 1.47.0 : les statistiques et le globe (jamais une adresse IP : il n'y en a dans aucune table).
    ...TABLES_SAUVEGARDE_STATS,
];
const CLES = new Map(TABLES_SAUVEGARDE);

// La ligne de config qui dit << une restauration est en cours >> : jamais sauvegardee, jamais restauree.
export const CONFIG_RESTAURATION = "restauration";

const PAGE_MAX = 1.5 * 1024 * 1024; // une page de la sauvegarde (octets du JSON), au plus
const LOT_SQL = 256; // les lignes lues d'un coup
const LIGNES_LOT_MAX = 100000;
// 1.46.0 : le plus ancien schema qu'un service de ce schema restaure (une sauvegarde 1.44 dans un service 1.46 : ses
// tables de la boutique restent vides).
const SCHEMA_RESTAURABLE = 3;

// Une valeur de SQLite -> JSON (un binaire : {"$b64"}) ; et l'inverse (undefined : illisible).
function versJson(v) {
    if (v instanceof ArrayBuffer) return { $b64: versBase64(new Uint8Array(v)) };
    return v;
}
function depuisJson(v) {
    if (v === null || typeof v === "string") return v;
    if (typeof v === "number") return Number.isFinite(v) ? v : undefined;
    if (v && typeof v === "object" && !Array.isArray(v) && Object.keys(v).length === 1 && typeof v.$b64 === "string") {
        const o = depuisBase64(v.$b64);
        return o ? o : undefined;
    }
    return undefined;
}

// Le curseur d'une page : la cle de la derniere ligne envoyee, en JSON, en base64 << url >>.
function ecrireCurseur(valeurs) {
    return versBase64(enc.encode(JSON.stringify(valeurs))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
function lireCurseur(texte, n) {
    const o = depuisBase64(texte);
    let v = null;
    try {
        v = o ? JSON.parse(new TextDecoder().decode(o)) : null;
    } catch {
        v = null;
    }
    if (!Array.isArray(v) || v.length !== n || !v.every((x) => typeof x === "string" || (typeof x === "number" && Number.isFinite(x))))
        throw mal("depuis", "« depuis » doit être le curseur rendu par la page précédente (« suivant »).");
    return v;
}

function tableDe(nom) {
    if (typeof nom !== "string" || !CLES.has(nom)) throw new Erreur(400, "table", "Cette table n'est pas dans la sauvegarde.");
    return nom;
}

// Les methodes ajoutees au Durable Object (licences.js).
export const RoutesSauvegarde = {
    // Les colonnes d'une table (telles que SQLite les a).
    colonnesDe(table) {
        return this.sql.exec("SELECT * FROM " + table + " LIMIT 0").columnNames;
    },

    // Le nombre de lignes de chaque table sauvegardee.
    comptesSauvegarde() {
        return TABLES_SAUVEGARDE.map(([nom]) => ({
            nom,
            lignes: this.nombre("SELECT COUNT(*) AS n FROM " + nom + (nom === "config" ? " WHERE cle != ?" : ""), ...(nom === "config" ? [CONFIG_RESTAURATION] : [])),
        }));
    },

    // GET /admin/sauvegarde[?table=…&depuis=…] (admin+).
    async sauvegarde(ctx) {
        const p = ctx.url.searchParams;
        if (p.get("table") === null) {
            // le sommaire : la ligne << sauvegarde >> d'abord (elle est dans le journal sauvegarde)
            if (!this.enRestauration) {
                const avant = this.comptesSauvegarde();
                await this.journalService(ctx.now, "sauvegarde", "", "a exporté la sauvegarde complète du service", {
                    tables: avant.length,
                    lignes: avant.reduce((n, t) => n + t.lignes, 0),
                });
            }
            return reponseJson(200, { schema: this.schema, service_version: this.versionService, tables: this.comptesSauvegarde(), heure: ctx.now });
        }
        const table = tableDe(p.get("table"));
        const cles = CLES.get(table);
        const depuis = p.get("depuis");
        let apres = depuis === null || depuis === "" ? null : lireCurseur(depuis, cles.length);
        const ordre = cles.join(", ");
        const lignes = [];
        let taille = 0;
        let suivant = null;
        for (;;) {
            const conditions = [];
            const args = [];
            if (apres) {
                conditions.push(cles.length === 1 ? cles[0] + " > ?" : "(" + ordre + ") > (" + cles.map(() => "?").join(", ") + ")");
                args.push(...apres);
            }
            if (table === "config") {
                conditions.push("cle != ?");
                args.push(CONFIG_RESTAURATION);
            }
            const lot = this.lignes(
                "SELECT * FROM " + table + (conditions.length ? " WHERE " + conditions.join(" AND ") : "") + " ORDER BY " + ordre + " LIMIT " + LOT_SQL,
                ...args,
            );
            let plein = false;
            for (const r of lot) {
                const l = {};
                for (const [k, v] of Object.entries(r)) l[k] = versJson(v);
                const t = enc.encode(JSON.stringify(l)).length + 1;
                if (lignes.length && taille + t > PAGE_MAX) {
                    plein = true;
                    break;
                }
                lignes.push(l);
                taille += t;
            }
            if (lignes.length) apres = cles.map((k) => lignes[lignes.length - 1][k]);
            if (plein) {
                suivant = ecrireCurseur(apres);
                break;
            }
            if (lot.length < LOT_SQL) break;
        }
        return reponseJson(200, { table, lignes, suivant, schema: this.schema });
    },

    // Le service a-t-il deja des licences, des activations ou une liste des admins ?
    serviceOccupe() {
        return !!(
            this.ligne("SELECT 1 AS x FROM licences LIMIT 1") ||
            this.ligne("SELECT 1 AS x FROM activations LIMIT 1") ||
            this.ligne("SELECT 1 AS x FROM admins LIMIT 1") ||
            this.config("admins_numero")
        );
    },

    // POST /admin/restauration/debut (admin+) {"schema":N,"remplacer":bool} : vide les tables sauvegardees (pas les
    // morceaux des fichiers : la fin les rapproche des fichiers restaures).
    async debutRestauration(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entier(corps.schema, 1, 1000)) throw mal("schema", "Le champ « schema » doit être le numéro du schéma de la sauvegarde.");
        if (corps.remplacer !== undefined && typeof corps.remplacer !== "boolean") throw mal("remplacer");
        if (corps.schema > this.schema || corps.schema < SCHEMA_RESTAURABLE)
            throw new Erreur(409, "schema", "Cette sauvegarde est du schéma " + corps.schema + " ; ce service est du schéma " + this.schema + " : restaure-la dans un service de la même version.", {
                schema: this.schema,
            });
        if (!corps.remplacer && !this.enRestauration && this.serviceOccupe())
            throw new Erreur(409, "pas_vide", "Ce service a déjà des licences, des activations ou une liste des admins : la restauration les remplacerait (« remplacer »: true pour le vouloir).");
        this.ctx.storage.transactionSync(() => {
            for (const [nom] of TABLES_SAUVEGARDE) this.x("DELETE FROM " + nom);
            this.ecrireConfig(CONFIG_RESTAURATION, String(ctx.now));
        });
        this.enRestauration = true;
        this.oublierCaches();
        return reponseJson(200, { schema: this.schema, tables: TABLES_SAUVEGARDE.map(([nom]) => nom) });
    },

    // POST /admin/restauration/lot (admin+) {"table":"…","lignes":[{…}]} : les lignes, telles quelles (une ligne deja
    // la, de la meme cle, est remplacee : un lot renvoye ne double rien). Toutes ou aucune.
    async lotRestauration(ctx) {
        if (!this.enRestauration) throw new Erreur(409, "restauration", "Aucune restauration n'est en cours : commence par POST /admin/restauration/debut.");
        const corps = this.corpsJson(ctx);
        const table = tableDe(corps.table);
        if (!Array.isArray(corps.lignes) || corps.lignes.length > LIGNES_LOT_MAX) throw mal("lignes");
        const colonnes = this.colonnesDe(table);
        const attendues = [...colonnes].sort().join(",");
        const valeurs = corps.lignes.map((l, i) => {
            if (!l || typeof l !== "object" || Array.isArray(l) || Object.keys(l).sort().join(",") !== attendues)
                throw new Erreur(400, "colonnes", "La ligne n° " + (i + 1) + " n'a pas les colonnes de la table « " + table + " » (" + colonnes.join(", ") + ").");
            return colonnes.map((c) => {
                const v = depuisJson(l[c]);
                if (v === undefined) throw new Erreur(400, "valeur", "La ligne n° " + (i + 1) + " : la valeur de « " + c + " » est illisible.");
                return v;
            });
        });
        const q = "INSERT OR REPLACE INTO " + table + " (" + colonnes.join(", ") + ") VALUES (" + colonnes.map(() => "?").join(", ") + ")";
        const iCle = colonnes.indexOf("cle");
        let n = 0;
        this.ctx.storage.transactionSync(() => {
            for (const v of valeurs) {
                if (table === "config" && v[iCle] === CONFIG_RESTAURATION) continue;
                this.x(q, ...v);
                n++;
            }
        });
        return reponseJson(200, { table, lignes: n });
    },

    // POST /admin/restauration/fin (admin+) : la cle du service relue de la config restauree ; les fichiers rapproches
    // de leurs morceaux (une restauration ailleurs n'en a aucun : l'admin+ les renvoie) ; une ligne << restauration >>.
    async finRestauration(ctx) {
        if (!this.enRestauration) throw new Erreur(409, "restauration", "Aucune restauration n'est en cours.");
        let incomplets = 0;
        this.ctx.storage.transactionSync(() => {
            this.ecrireConfig(CONFIG_RESTAURATION, null);
            // des morceaux d'un fichier qui n'est plus la : effaces
            this.x("DELETE FROM morceaux WHERE fichier NOT IN (SELECT id FROM fichiers)");
            for (const f of this.lignes("SELECT id, morceaux, recus, complet FROM fichiers")) {
                const recus = this.nombre("SELECT COUNT(*) AS n FROM morceaux WHERE fichier = ? AND n < ?", f.id, f.morceaux);
                const complet = recus >= f.morceaux && f.complet ? 1 : 0;
                if (recus !== f.recus || complet !== f.complet) this.x("UPDATE fichiers SET recus = ?, complet = ? WHERE id = ?", recus, complet, f.id);
                if (!complet) incomplets++;
            }
            // le compteur des octets, juste (reecrit seulement s'il change : une restauration a l'identique ne touche rien)
            const o = String(this.nombre("SELECT COALESCE(SUM(LENGTH(octets)), 0) AS o FROM morceaux"));
            if (o !== (this.config("octets_morceaux") || "0")) this.ecrireConfig("octets_morceaux", o);
            // 1.46.0 : une sauvegarde d'un schema plus ancien : le schema de ce service (ses tables sont deja la).
            // 1.47.0 : d'avant le schema 5 : les versions vues recopiees des logiciels (comme la migration) ; le debut des
            // releves sera note au prochain appel.
            if (Number(this.config("schema") || "0") < 5)
                this.x("INSERT INTO versions_vues (cle, version, logiciel, premiere) SELECT cle, version, logiciel, vu FROM logiciels WHERE true ON CONFLICT DO NOTHING");
            if (Number(this.config("schema") || "0") < this.schema) this.ecrireConfig("schema", String(this.schema));
        });
        this.enRestauration = false;
        this.oublierCaches();
        await this.chargerCle();
        const tables = this.comptesSauvegarde();
        await this.journalService(ctx.now, "restauration", "", "a restauré le service depuis une sauvegarde", {
            tables: Object.fromEntries(tables.map((t) => [t.nom, t.lignes])),
            fichiers_a_renvoyer: incomplets,
        });
        return reponseJson(200, { tables, fichiers_a_renvoyer: incomplets, cle_service: this.clePub });
    },
};
