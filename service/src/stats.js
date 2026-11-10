// =============================================================================
//  service/src/stats.js - 1.47.0 : LES STATISTIQUES ET LE GLOBE
//  (docs/STATISTIQUES.md)
//  - les courbes (GET /admin/stats/serie) : des mesures sur une periode, par pas
//    (jour, semaine, mois), divisees (offre, client, admin...) et comparees (la
//    periode d'avant, l'an dernier) - calculees des tables d'avant la 1.47 (les
//    licences et leur bloc, les activations, les commandes, les decouvertes...)
//    et de ce que le service releve en plus depuis la 1.47 (les faits, le releve
//    du jour, les compteurs) ;
//  - les mises a jour (GET /admin/stats/mises-a-jour), les jours et les heures
//    des admins (GET /admin/stats/admins), le releve du jour (l'alarme de
//    l'objet, le premier appel du jour, POST /admin/stats/releve) ;
//  - le globe (GET /admin/globe, /admin/globe/direct) : la ville de la DERNIERE
//    connexion de chaque cle (request.cf, lu par le Worker : index.js) ; jamais
//    l'adresse IP, jamais un historique des lieux.
// -----------------------------------------------------------------------------
//  Les routes sont des methodes du Durable Object (licences.js les ajoute a sa
//  classe : `this` est l'objet, ses tables et ses aides).
//  LES TABLES (schema 5, sauvegardees : sauvegarde.js)
//    stats_faits      un fait (une licence deposee, un PC active, une demande
//                     traitee...) : t, genre, licence, offre, client, admin,
//                     poste, valeur, detail ; 2 ans
//    stats_quotidien  le releve d'un jour UTC (les niveaux et leurs divisions) ;
//                     une ligne par jour, jamais refaite ; 2 ans
//    stats_compteurs  (jour, mesure, cle) -> n : le pic des jetons flottants, les
//                     appels des admins par heure UTC ; tenus en memoire, ecrits
//                     une fois par minute au plus ; 2 ans
//    versions_vues    (cle, version) -> logiciel, premiere : la premiere fois
//                     qu'une cle est vue avec une version ; 2 ans
//    lieux            le lieu de la derniere connexion de chaque cle (ville,
//                     region, pays, lat et lon au dixieme de degre, colo) ; reecrit
//                     quand il change, sinon une fois par heure ; un an sans appel
//  EN MEMOIRE SEULEMENT : les cles vues dans les 15 dernieres minutes (<< actifs
//  maintenant >>), les connexions en direct (15 minutes), les compteurs de la
//  derniere minute.
// =============================================================================
import { trouverBloc } from "./blocs.js";
import { trouverRevocation } from "./documents.js";
import { comparerVersions, versionEnNombres } from "./fichiers.js";
import { Erreur, cleIdDe, jourDe, lireIso, mal, reponseJson } from "./outils.js";

const JOUR = 86400;
export const GARDE_STATS = 731 * JOUR; // les faits, les releves, les compteurs, les versions vues : 2 ans
const GARDE_LIEU = 365 * JOUR; // le lieu d'une cle qui n'appelle plus : un an
const ACTIF = 15 * 60; // << actif maintenant >> : vu dans les 15 dernieres minutes
const VU_RECENT = 30 * JOUR; // pc_vus : vue dans les 30 jours
const ECRIRE_LIEU = 3600; // le << vu >> d'un lieu : une ecriture par heure au plus
const ECRIRE_COMPTEURS = 60; // les compteurs : une fois par minute au plus
const MAX_POINTS = 400;
const MAX_MESURES = 20;
const MAX_PARTS = 50;
const DIRECT_DUREE = 15 * 60;
const DIRECT_MAX = 2000;
const DIRECT_RENDUS = 500;
const MAX_MEMOIRE = 200000; // les cles tenues en memoire (vues, lieux), au plus
const MAX_JOURS_ADMINS = 731;
const MAX_NOMS_VILLE = 50;
const MAX_POSTES_VILLE = 200;
const MAX_PAS_A_JOUR = 500;
const STUDIOS = ["IHM-Studio", "API-Studio"];
const LOGICIEL_ADMIN = "XPG-License-Admin";

// Les faits releves (stats_faits.genre).
export const FAITS = [
    "licence_nouvelle",
    "licence_prolongee",
    "licence_option",
    "licence_revoquee",
    "pc_revoque",
    "pc_active",
    "pc_libere",
    "pc_retire",
    "decouverte_convertie",
    "demande_recue",
    "demande_traitee",
    "commande_livree",
];

// Les tables neuves du schema 5 (et deux index sur decouvertes : les Decouvertes en cours, leur conversion).
export const TABLES_STATS_SQL = `
    CREATE TABLE IF NOT EXISTS stats_faits (
        numero INTEGER PRIMARY KEY, t INTEGER NOT NULL, genre TEXT NOT NULL, licence TEXT NOT NULL, offre TEXT NOT NULL,
        client TEXT NOT NULL, admin TEXT NOT NULL, poste TEXT NOT NULL, valeur INTEGER NOT NULL, detail TEXT NOT NULL);
    CREATE INDEX IF NOT EXISTS stats_faits_genre ON stats_faits (genre, t);
    CREATE TABLE IF NOT EXISTS stats_quotidien (jour TEXT PRIMARY KEY, heure INTEGER NOT NULL, donnees TEXT NOT NULL) WITHOUT ROWID;
    CREATE TABLE IF NOT EXISTS stats_compteurs (
        jour TEXT NOT NULL, mesure TEXT NOT NULL, cle TEXT NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (jour, mesure, cle)) WITHOUT ROWID;
    CREATE TABLE IF NOT EXISTS versions_vues (
        cle TEXT NOT NULL, version TEXT NOT NULL, logiciel TEXT NOT NULL, premiere INTEGER NOT NULL, PRIMARY KEY (cle, version)) WITHOUT ROWID;
    CREATE TABLE IF NOT EXISTS lieux (
        cle TEXT PRIMARY KEY, poste TEXT NOT NULL, logiciel TEXT NOT NULL, ville TEXT NOT NULL, region TEXT NOT NULL, pays TEXT NOT NULL,
        lat REAL NOT NULL, lon REAL NOT NULL, colo TEXT NOT NULL, vu INTEGER NOT NULL) WITHOUT ROWID;
    CREATE INDEX IF NOT EXISTS decouvertes_poste ON decouvertes (poste);
    CREATE INDEX IF NOT EXISTS decouvertes_debut ON decouvertes (debut);
`;

// Les tables des statistiques dans la sauvegarde (sauvegarde.js) et leur cle.
export const TABLES_SAUVEGARDE_STATS = [
    ["stats_faits", ["numero"]],
    ["stats_quotidien", ["jour"]],
    ["stats_compteurs", ["jour", "mesure", "cle"]],
    ["versions_vues", ["cle", "version"]],
    ["lieux", ["cle"]],
];

// ============================================================================ les lieux
// Un texte d'une ligne, sans caractere de controle, `max` caracteres au plus ("" : absent ou illisible).
function ligneCourte(v, max) {
    if (typeof v !== "string") return "";
    const s = v.replace(/[\u0000-\u001f\u007f]/g, " ").trim();
    return Array.from(s).slice(0, max).join("");
}

// Le lieu d'un appel, dans l'en-tete X-XPG-Lieu (ecrit par le Worker) ou X-XPG-Lieu-Essai (les essais, 127.0.0.1) : un
// formulaire d'adresse (ville, region, pays, lat, lon, colo). null : pas de lieu lisible. La latitude et la longitude
// sont arrondies au dixieme de degre (~10 km) : la ville, jamais l'adresse.
export function lireLieu(texte) {
    if (typeof texte !== "string" || !texte || texte.length > 1000) return null;
    let p;
    try {
        p = new URLSearchParams(texte);
    } catch {
        return null;
    }
    const pays = String(p.get("pays") || "").toUpperCase();
    if (!/^[A-Z0-9]{2}$/.test(pays)) return null;
    const lat = Number.parseFloat(p.get("lat"));
    const lon = Number.parseFloat(p.get("lon"));
    if (!Number.isFinite(lat) || !Number.isFinite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) return null;
    const colo = String(p.get("colo") || "").toUpperCase();
    return {
        ville: ligneCourte(p.get("ville"), 80),
        region: ligneCourte(p.get("region"), 80),
        pays,
        lat: Math.round(lat * 10) / 10,
        lon: Math.round(lon * 10) / 10,
        colo: /^[A-Z]{3,4}$/.test(colo) ? colo : "",
    };
}

export function ecrireLieu(l) {
    const p = new URLSearchParams();
    for (const k of ["ville", "region", "pays", "lat", "lon", "colo"]) if (l[k] !== undefined && l[k] !== null && l[k] !== "") p.set(k, String(l[k]));
    return p.toString();
}

// Le lieu que Cloudflare donne pour cet appel (request.cf) - jamais l'adresse IP ; en local (127.0.0.1, localhost), le
// faux request.cf de miniflare est ignore : seul X-XPG-Lieu-Essai place un appel. "" : aucun lieu.
export function lieuDeLAppel(request, url) {
    const local = url.hostname === "127.0.0.1" || url.hostname === "localhost";
    let l = null;
    if (local) l = lireLieu(request.headers.get("X-XPG-Lieu-Essai") || "");
    else {
        const cf = request.cf;
        if (cf && typeof cf === "object")
            l = lireLieu(ecrireLieu({ ville: cf.city, region: cf.region, pays: cf.country, lat: cf.latitude, lon: cf.longitude, colo: cf.colo }));
    }
    return l ? ecrireLieu(l) : "";
}

const idVille = (l) => l.pays + "|" + l.ville;
const signatureLieu = (l) => [l.ville, l.region, l.pays, l.lat, l.lon, l.colo].join("|");

// ============================================================================ les jours
const jourTexte = (d) => new Date(d * 86400000).toISOString().slice(0, 10);
function jourNumero(s) {
    const m = typeof s === "string" ? /^(\d{4})-(\d{2})-(\d{2})$/.exec(s) : null;
    if (!m) return null;
    const t = Date.UTC(+m[1], +m[2] - 1, +m[3]);
    if (!Number.isFinite(t)) return null;
    const d = t / 86400000;
    return jourTexte(d) === s ? d : null;
}
const numeroDuJour = (t) => Math.floor(t / JOUR);
// Le meme jour, un an plus tot (le 29 fevrier : le 28).
function anAvant(d) {
    const x = new Date(d * 86400000);
    let j = x.getUTCDate();
    if (x.getUTCMonth() === 1 && j === 29) j = 28;
    return Date.UTC(x.getUTCFullYear() - 1, x.getUTCMonth(), j) / 86400000;
}

// Les periodes du pas : [[du, au], ...] (numeros de jours, compris).
function decouper(du, au, pas) {
    const out = [];
    let d = du;
    while (d <= au) {
        let fin = d;
        if (pas === "semaine") fin = d + (6 - ((new Date(d * 86400000).getUTCDay() + 6) % 7));
        else if (pas === "mois") {
            const x = new Date(d * 86400000);
            fin = Date.UTC(x.getUTCFullYear(), x.getUTCMonth() + 1, 1) / 86400000 - 1;
        }
        fin = Math.min(fin, au);
        out.push([d, fin]);
        if (out.length > MAX_POINTS)
            throw new Erreur(400, "champ", "Trop de points (" + MAX_POINTS + " au plus) : choisis la semaine ou le mois, ou une période plus courte.");
        d = fin + 1;
    }
    return out;
}

// L'indice de la periode qui contient le jour `d` (-1 : aucune).
function indice(per, d) {
    if (!per.length || d < per[0][0] || d > per[per.length - 1][1]) return -1;
    let lo = 0;
    let hi = per.length - 1;
    while (lo < hi) {
        const m = (lo + hi + 1) >> 1;
        if (per[m][0] <= d) lo = m;
        else hi = m - 1;
    }
    return d <= per[lo][1] ? lo : -1;
}

// La periode demandee (du, au : jours) ; defaut : les `jours` derniers jours, aujourd'hui compris.
function periodeDe(p, now, jours) {
    const auj = numeroDuJour(now);
    const du = p.get("du");
    const au = p.get("au");
    let a = au === null || au === "" ? auj : jourNumero(au);
    let d = du === null || du === "" ? null : jourNumero(du);
    if (a === null) throw mal("au", "« au » doit être un jour (AAAA-MM-JJ).");
    if (du !== null && du !== "" && d === null) throw mal("du", "« du » doit être un jour (AAAA-MM-JJ).");
    if (d === null) d = a - jours + 1;
    const min = jourNumero("2000-01-01");
    if (d > a) throw mal("du", "« du » doit être avant « au ».");
    if (d < min || a > auj + 400) throw mal("au", "La période doit être entre le 01/01/2000 et aujourd'hui + 400 jours.");
    return { du: d, au: a };
}

const PAS = ["jour", "semaine", "mois"];
const COMPARER = ["avant", "an", "rien"];
const PAR = ["offre", "option", "article", "admin", "client", "logiciel", "version", "type", "licence"];

// ============================================================================ les aides
const normClient = (s) => String(s || "").trim().replace(/\s+/g, " ").toLowerCase();
function passeLogiciel(filtre, l) {
    if (filtre === "studios") return STUDIOS.includes(l);
    if (filtre === "admins") return l === LOGICIEL_ADMIN;
    return l === filtre;
}
const groupeDe = (l) => (STUDIOS.includes(l) ? "studios" : l === LOGICIEL_ADMIN ? "admins" : "");

function mediane(v) {
    if (!v.length) return null;
    const s = [...v].sort((a, b) => a - b);
    const m = s.length >> 1;
    return s.length % 2 ? s[m] : Math.round((s[m - 1] + s[m]) / 2);
}
const moyenne = (v) => (v.length ? Math.round(v.reduce((a, b) => a + b, 0) / v.length) : null);
const plusGrand = (v) => (v.length ? Math.max(...v) : null);

// Le bloc d'une licence -> ce qu'en disent les statistiques : {client, offre, options (ids), achats [{at, kind}], signataire}.
function infosBloc(texte) {
    try {
        const b = trouverBloc(texte, "licence");
        const v = JSON.parse(b.json);
        const options = Array.isArray(v.options) ? v.options.filter((o) => o && typeof o.id === "string").map((o) => o.id) : [];
        const achats = Array.isArray(v.purchases)
            ? v.purchases
                  .filter((a) => a && typeof a.kind === "string")
                  .map((a) => ({ at: lireIso(a.at) || 0, kind: a.kind }))
                  .filter((a) => a.at)
            : [];
        return { client: ligneCourte(v.customer, 120), offre: ligneCourte(v.edition, 40), options, achats, signataire: b.cleId };
    } catch {
        return { client: "", offre: "", options: [], achats: [], signataire: "" };
    }
}

function versionsTriees(lignes) {
    return lignes
        .map((r) => ({ version: r.version, n: versionEnNombres(r.version), publiee: Number(r.publiee) || 0 }))
        .filter((v) => v.n)
        .sort((a, b) => comparerVersions(a.n, b.n));
}
// Le moment ou une version cesse d'etre la derniere (la publication d'une plus recente) ; Infinity : jamais.
function finDeVersion(publiees, version) {
    const n = versionEnNombres(version);
    if (!n) return -Infinity;
    let fin = Infinity;
    for (const p of publiees) if (comparerVersions(p.n, n) > 0 && p.publiee < fin) fin = p.publiee;
    return fin;
}

// ============================================================================ les donnees d'une requete
// Lues une fois, a la demande (une requete de statistiques en lit beaucoup : jamais deux fois la meme table).
function donnees(o, now) {
    const memo = new Map();
    const une = (nom, f) => {
        if (!memo.has(nom)) memo.set(nom, f());
        return memo.get(nom);
    };
    const d = {
        now,
        debut: o.debutStats(now),
        jours: (() => {
            const j = Number.parseInt(String(o.env.DECOUVERTE_JOURS || "30"), 10);
            return Number.isFinite(j) && j >= 1 && j <= 366 ? j : 30;
        })(),
        faits: (genre) => une("f:" + genre, () => o.lignes("SELECT * FROM stats_faits WHERE genre = ? ORDER BY t, numero", genre)),
        detail: (f) => {
            try {
                return f.detail ? JSON.parse(f.detail) : {};
            } catch {
                return {};
            }
        },
        // Les temps des revocations : "licence|" (toute la licence) ou "licence|poste" -> t (le plus ancien connu).
        revocationsTemps: () =>
            une("revT", () => {
                const m = new Map();
                const mettre = (k, t) => {
                    if (t && (!m.has(k) || t < m.get(k))) m.set(k, t);
                };
                for (const f of d.faits("licence_revoquee")) mettre(f.licence + "|", f.t);
                for (const f of d.faits("pc_revoque")) mettre(f.licence + "|" + f.poste, f.t);
                const bloc = o.config("revocations");
                if (bloc)
                    try {
                        const v = JSON.parse(trouverBloc(bloc, "revocations").json);
                        for (const x of Array.isArray(v.revoked) ? v.revoked : [])
                            if (x && typeof x.license_id === "string") mettre(x.license_id + "|" + (typeof x.device === "string" ? x.device : ""), lireIso(x.revoked_at) || 0);
                    } catch {
                        // une liste illisible : les faits seulement
                    }
                return m;
            }),
        licences: () => une("licences", () => o.licencesPourStats(d)),
        licMap: () => une("licMap", () => new Map(d.licences().map((l) => [l.id, l]))),
        // Les logiciels vus : cle -> {logiciel, version, vu}.
        logiciels: () => une("logiciels", () => new Map(o.lignes("SELECT cle, logiciel, version, vu FROM logiciels").map((r) => [r.cle, r]))),
        activations: () => une("activations", () => o.activationsPourStats(d)),
        // La licence (la plus recemment activee) de chaque cle de PC : cle -> {licence, client, admin, nom, activee}.
        clePc: () =>
            une("clePc", () => {
                const m = new Map();
                for (const a of d.activations()) {
                    const x = m.get(a.poste);
                    if (!x || a.activee > x.activee) m.set(a.poste, { licence: a.licence, client: a.k.client, admin: a.k.admin, offre: a.k.offre, nom: a.nom, activee: a.activee });
                }
                return m;
            }),
        commandes: () =>
            une("commandes", () => {
                const livreurs = new Map();
                for (const f of d.faits("commande_livree")) {
                    const n = d.detail(f).numero;
                    if (Number.isInteger(n)) livreurs.set(n, f.admin);
                }
                return o
                    .lignes("SELECT numero, genre, edition, option, article, montant, societe, etat, paye, maj, licence, licence_existante FROM commandes WHERE paye > 0")
                    .map((c) => ({
                        ...c,
                        k: {
                            offre: c.genre === "edition" ? c.edition : "options",
                            option: c.genre === "option" ? c.option : "",
                            article: c.article,
                            client: c.societe,
                            admin: livreurs.get(c.numero) || "",
                            licence: c.licence || c.licence_existante || "",
                        },
                    }));
            }),
        publiees: () => une("publiees", () => versionsTriees(o.lignes("SELECT version, publiee FROM versions"))),
        versionsVues: () =>
            une("vv", () => {
                const m = new Map();
                for (const r of o.lignes("SELECT cle, version, logiciel, premiere FROM versions_vues ORDER BY cle, premiere")) {
                    if (!m.has(r.cle)) m.set(r.cle, []);
                    m.get(r.cle).push(r);
                }
                return m;
            }),
        compteurs: (mesure, de, a) => o.lignes("SELECT jour, cle, n FROM stats_compteurs WHERE jour >= ? AND jour <= ? AND mesure = ?", jourTexte(de), jourTexte(a), mesure),
        prixOffre: () =>
            une("prix", () => {
                const m = new Map();
                for (const a of o.reglagesBoutique().articles || []) if (a && a.genre === "edition" && Number.isInteger(a.montant)) m.set(a.edition, a.montant);
                return m;
            }),
    };
    return d;
}

// ============================================================================ les mesures
// genre : flux (une somme par periode), max (le plus grand des jours), ratio (calcule sur les valeurs de la periode),
// cumul (depuis le 1er janvier), niveau (a la fin de la periode : des intervalles [debut, fin)), age (la plus ancienne).
// items(d, de, a) : [{t, v, k}] ; intervalles(d) : [{debut, fin, p, k}] ; dims : les divisions et filtres possibles ;
// releve : les divisions gardees dans le releve du jour ; droit : ce qu'il faut a un admin.
const D_VENTES = ["offre", "option", "article", "client", "admin", "licence"];
const D_LIC = ["offre", "client", "admin", "type", "licence"];
const D_PC = ["offre", "client", "admin", "type", "logiciel", "licence"];

const ventes = (d) => d.commandes().map((c) => ({ t: c.paye, v: c.montant, k: c.k }));
const remboursees = (d) => d.commandes().filter((c) => c.etat === "remboursee").map((c) => ({ t: c.maj, v: c.montant, k: c.k }));
const traitees = (d, issue) =>
    d
        .faits("demande_traitee")
        .filter((f) => !issue || d.detail(f).issue === issue)
        .map((f) => ({ t: f.t, v: issue === null ? f.valeur : 1, k: { admin: f.admin, client: f.client, licence: f.licence } }));
const faitsPc = (d, genres) =>
    genres.flatMap((g) =>
        d
            .faits(g)
            .filter((f) => f.t >= d.debut)
            .map((f) => {
                const l = d.licMap().get(f.licence);
                const log = d.logiciels().get(f.poste);
                return {
                    t: f.t,
                    v: 1,
                    k: { offre: f.offre, client: f.client, admin: f.admin || (l ? l.signataire : ""), type: l ? l.type : "", logiciel: log ? log.logiciel : "", licence: f.licence },
                };
            }),
    );

export const MESURES = {
    // ---- les ventes (la boutique)
    ventes_montant: { unite: "centimes", genre: "flux", source: "S", droit: "demandes", dims: D_VENTES, items: ventes },
    ventes_cumul: { unite: "centimes", genre: "cumul", source: "S", droit: "demandes", dims: D_VENTES, items: ventes },
    ventes_commandes: { unite: "nombre", genre: "flux", source: "S", droit: "demandes", dims: D_VENTES, items: (d) => ventes(d).map((x) => ({ ...x, v: 1 })) },
    ventes_panier: { unite: "centimes", genre: "ratio", source: "S", droit: "demandes", dims: D_VENTES, items: ventes, calcul: moyenne },
    ventes_remboursements: { unite: "centimes", genre: "flux", source: "S", droit: "demandes", dims: D_VENTES, items: remboursees },
    ventes_remboursements_nombre: {
        unite: "nombre",
        genre: "flux",
        source: "S",
        droit: "demandes",
        dims: D_VENTES,
        items: (d) => remboursees(d).map((x) => ({ ...x, v: 1 })),
    },
    licences_vendues_en_ligne: {
        unite: "nombre",
        genre: "flux",
        source: "S",
        droit: "demandes",
        dims: ["offre", "client", "admin", "licence"],
        items: (d) =>
            d
                .commandes()
                .filter((c) => c.genre === "edition")
                .map((c) => ({ t: c.paye, v: 1, k: c.k })),
    },
    licences_signees_main: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        droit: "demandes",
        dims: D_LIC,
        items: (d) => {
            const vendues = new Set(d.commandes().filter((c) => c.genre === "edition" && c.licence).map((c) => c.licence));
            return d
                .licences()
                .filter((l) => !vendues.has(l.id))
                .map((l) => ({ t: l.creee, v: 1, k: l.kc }));
        },
    },
    // ---- les licences
    licences_actives: {
        unite: "nombre",
        genre: "niveau",
        source: "S+N",
        dims: D_LIC,
        releve: ["offre", "admin", "type"],
        intervalles: (d) => d.licences().map((l) => ({ debut: l.creee, fin: l.fin, p: 1, k: l.k })),
    },
    licences_nouvelles: { unite: "nombre", genre: "flux", source: "S+N", dims: D_LIC, items: (d) => d.licences().map((l) => ({ t: l.creee, v: 1, k: l.kc })) },
    licences_prolongees: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        dims: D_LIC,
        items: (d) => [
            ...d
                .faits("licence_prolongee")
                .filter((f) => f.t >= d.debut)
                .map((f) => {
                    const l = d.licMap().get(f.licence);
                    return { t: f.t, v: 1, k: { offre: f.offre, client: f.client, admin: f.admin, type: l ? l.type : "", licence: f.licence } };
                }),
            ...d.licences().flatMap((l) => l.achats.filter((a) => a.kind === "extension" && a.at < d.debut).map((a) => ({ t: a.at, v: 1, k: l.k }))),
        ],
    },
    licences_options: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        dims: ["option", ...D_LIC],
        items: (d) => [
            ...d
                .faits("licence_option")
                .filter((f) => f.t >= d.debut)
                .flatMap((f) => {
                    const l = d.licMap().get(f.licence);
                    const ids = Array.isArray(d.detail(f).options) ? d.detail(f).options : [""];
                    return ids.map((id) => ({ t: f.t, v: 1, k: { option: String(id), offre: f.offre, client: f.client, admin: f.admin, type: l ? l.type : "", licence: f.licence } }));
                }),
            ...d.licences().flatMap((l) => l.achats.filter((a) => a.kind === "option" && a.at < d.debut).map((a) => ({ t: a.at, v: 1, k: { ...l.k, option: "" } }))),
        ],
    },
    licences_expirees: {
        unite: "nombre",
        genre: "flux",
        source: "S",
        dims: D_LIC,
        items: (d) =>
            d
                .licences()
                .filter((l) => l.expire > 0)
                .map((l) => ({ t: l.expire, v: 1, k: l.k })),
    },
    licences_echeances_montant: {
        unite: "centimes",
        genre: "flux",
        source: "S",
        dims: D_LIC,
        items: (d) =>
            d
                .licences()
                .filter((l) => l.expire > 0)
                .map((l) => ({ t: l.expire, v: d.prixOffre().get(l.offre) || 0, k: l.k })),
    },
    licences_revoquees: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        dims: D_LIC,
        items: (d) => {
            const out = [];
            const vues = new Set();
            for (const f of d.faits("licence_revoquee")) {
                if (f.t < d.debut) continue;
                vues.add(f.licence);
                const l = d.licMap().get(f.licence);
                out.push({ t: f.t, v: 1, k: { offre: f.offre, client: f.client, admin: f.admin, type: l ? l.type : "", licence: f.licence } });
            }
            // avant la 1.47 : la date de la liste de revocation (revoked_at)
            for (const [cle, t] of d.revocationsTemps()) {
                if (!cle.endsWith("|") || t >= d.debut) continue;
                const id = cle.slice(0, -1);
                const l = d.licMap().get(id);
                if (!vues.has(id)) out.push({ t, v: 1, k: l ? l.k : { offre: "", client: "", admin: "", type: "", licence: id } });
            }
            return out;
        },
    },
    // ---- les demandes en ligne
    demandes_recues: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        droit: "demandes",
        dims: ["client"],
        items: (o, d) => [
            ...d
                .faits("demande_recue")
                .filter((f) => f.t >= d.debut)
                .map((f) => ({ t: f.t, v: 1, k: { client: f.client } })),
            ...o.lignes("SELECT cree FROM demandes WHERE cree < ?", d.debut).map((r) => ({ t: r.cree, v: 1, k: { client: "" } })),
        ],
        avecObjet: true,
    },
    demandes_traitees: { unite: "nombre", genre: "flux", source: "N", droit: "demandes", dims: ["admin", "client", "licence"], items: (d) => traitees(d, "").map((x) => ({ ...x, v: 1 })) },
    demandes_acceptees: { unite: "nombre", genre: "flux", source: "N", droit: "demandes", dims: ["admin", "client", "licence"], items: (d) => traitees(d, "acceptee") },
    demandes_refusees: { unite: "nombre", genre: "flux", source: "N", droit: "demandes", dims: ["admin", "client", "licence"], items: (d) => traitees(d, "refusee") },
    demandes_delai_median: { unite: "secondes", genre: "ratio", source: "N", droit: "demandes", dims: ["admin", "client", "licence"], items: (d) => traitees(d, null), calcul: mediane },
    demandes_delai_moyen: { unite: "secondes", genre: "ratio", source: "N", droit: "demandes", dims: ["admin", "client", "licence"], items: (d) => traitees(d, null), calcul: moyenne },
    demandes_delai_max: { unite: "secondes", genre: "ratio", source: "N", droit: "demandes", dims: ["admin", "client", "licence"], items: (d) => traitees(d, null), calcul: plusGrand },
    demandes_attente: {
        unite: "nombre",
        genre: "niveau",
        source: "S+N",
        droit: "demandes",
        dims: [],
        releve: [],
        intervalles: (d, o) => o.lignes("SELECT cree, maj, etat FROM demandes").map((r) => ({ debut: r.cree, fin: r.etat === "attente" ? Infinity : r.maj, p: 1, k: {} })),
    },
    demandes_plus_ancienne: {
        unite: "secondes",
        genre: "age",
        source: "S+N",
        droit: "demandes",
        dims: [],
        releve: [],
        intervalles: (d, o) => o.lignes("SELECT cree, maj, etat FROM demandes").map((r) => ({ debut: r.cree, fin: r.etat === "attente" ? Infinity : r.maj, p: 1, k: {} })),
    },
    // ---- les PC
    pc_actifs: {
        unite: "nombre",
        genre: "niveau",
        source: "S+N",
        dims: D_PC,
        releve: ["offre", "admin", "type", "logiciel"],
        intervalles: (d) => d.activations().map((a) => ({ debut: a.activee, fin: a.fin, p: 1, k: a.k })),
    },
    pc_actives: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        dims: D_PC,
        items: (d) => [...faitsPc(d, ["pc_active"]), ...d.activations().filter((a) => a.activee < d.debut).map((a) => ({ t: a.activee, v: 1, k: a.k }))],
    },
    pc_liberes: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        dims: D_PC,
        items: (d) => [...faitsPc(d, ["pc_libere"]), ...d.activations().filter((a) => a.etat === "liberee" && a.vue < d.debut).map((a) => ({ t: a.vue, v: 1, k: a.k }))],
    },
    pc_retires: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        dims: D_PC,
        items: (d) => [
            ...faitsPc(d, ["pc_retire", "pc_revoque"]),
            ...d.activations().filter((a) => a.etat === "retiree" && a.vue < d.debut).map((a) => ({ t: a.vue, v: 1, k: a.k })),
        ],
    },
    jetons_pic: {
        unite: "nombre",
        genre: "max",
        source: "N",
        dims: [],
        jours: (d, de, a) => d.compteurs("jetons_pic", de, a).map((r) => ({ t: jourNumero(r.jour) * JOUR, v: r.n, k: {} })),
    },
    jetons_plafond: {
        unite: "nombre",
        genre: "niveau",
        source: "S+N",
        dims: ["offre", "client", "licence"],
        releve: [],
        intervalles: (d) =>
            d
                .licences()
                .filter((l) => l.type === "flottante")
                .map((l) => ({ debut: l.creee, fin: l.fin, p: l.postes, k: l.k })),
    },
    licences_au_plafond: {
        unite: "nombre",
        genre: "flux",
        source: "N",
        dims: ["offre", "client", "licence"],
        jours: (d, de, a) =>
            d
                .compteurs("jetons_pic_licence", de, a)
                .filter((r) => {
                    const l = d.licMap().get(r.cle);
                    return l && r.n >= l.postes;
                })
                .map((r) => ({ t: jourNumero(r.jour) * JOUR, v: 1, k: d.licMap().get(r.cle).k })),
    },
    decouvertes_commencees: {
        unite: "nombre",
        genre: "flux",
        source: "S",
        dims: [],
        jours: (d, de, a, o) => o.lignes("SELECT debut FROM decouvertes WHERE debut >= ? AND debut < ?", de * JOUR, (a + 1) * JOUR).map((r) => ({ t: r.debut, v: 1, k: {} })),
    },
    decouvertes_converties: {
        unite: "nombre",
        genre: "flux",
        source: "S+N",
        dims: [],
        jours: (d, de, a, o) => {
            const out = d
                .faits("decouverte_convertie")
                .filter((f) => f.t >= d.debut)
                .map((f) => ({ t: f.t, v: 1, k: {} }));
            // avant la 1.47 : la premiere activation de la cle apres le debut de sa Decouverte
            if (de * JOUR < d.debut) {
                const premiere = new Map();
                for (const x of d.activations()) if (!premiere.has(x.poste) || x.activee < premiere.get(x.poste)) premiere.set(x.poste, x.activee);
                for (const r of o.lignes("SELECT poste, debut FROM decouvertes WHERE debut < ?", d.debut)) {
                    const t = premiere.get(r.poste);
                    if (t && t >= r.debut && t < d.debut) out.push({ t, v: 1, k: {} });
                }
            }
            return out;
        },
    },
    decouvertes_en_cours: {
        unite: "nombre",
        genre: "niveau",
        source: "S+N",
        dims: [],
        releve: [],
        intervalles: (d, o, de) => {
            const conv = new Map();
            for (const f of d.faits("decouverte_convertie")) if (!conv.has(f.poste)) conv.set(f.poste, f.t);
            const premiere = new Map();
            for (const x of d.activations()) if (!premiere.has(x.poste) || x.activee < premiere.get(x.poste)) premiere.set(x.poste, x.activee);
            return o.lignes("SELECT poste, debut FROM decouvertes WHERE debut >= ?", de * JOUR - d.jours * JOUR).map((r) => {
                let fin = r.debut + d.jours * JOUR;
                const c = conv.get(r.poste) || premiere.get(r.poste);
                if (c && c >= r.debut) fin = Math.min(fin, c);
                return { debut: r.debut, fin, p: 1, k: {} };
            });
        },
    },
    // ---- les mises a jour
    pc_vus: {
        unite: "nombre",
        genre: "niveau",
        source: "S+N",
        dims: ["logiciel", "version", "client", "licence", "admin"],
        releve: ["logiciel", "version"],
        intervalles: (d, o) => o.segmentsVersions(d, false),
    },
    pc_a_jour: {
        unite: "nombre",
        genre: "niveau",
        source: "S+N",
        dims: ["logiciel", "client", "licence", "admin"],
        releve: ["logiciel"],
        intervalles: (d, o) => o.segmentsVersions(d, true),
    },
    maj_installees: {
        unite: "nombre",
        genre: "flux",
        source: "N",
        dims: ["logiciel", "version", "client", "licence"],
        items: (d) => {
            const out = [];
            for (const [cle, lignes] of d.versionsVues()) {
                const pc = d.clePc().get(cle);
                for (const r of lignes)
                    if (r.premiere >= d.debut) out.push({ t: r.premiere, v: 1, k: { logiciel: r.logiciel, version: r.version, client: pc ? pc.client : "", licence: pc ? pc.licence : "" } });
            }
            return out;
        },
    },
    // ---- les admins
    admin_appels: {
        unite: "nombre",
        genre: "flux",
        source: "N",
        dims: ["admin"],
        soi: true,
        jours: (d, de, a) =>
            d.compteurs("admin_appels", de, a).map((r) => ({ t: jourNumero(r.jour) * JOUR, v: r.n, k: { admin: r.cle.slice(0, r.cle.indexOf("|")) } })),
    },
    admin_jours: {
        unite: "nombre",
        genre: "flux",
        source: "N",
        dims: ["admin"],
        soi: true,
        jours: (d, de, a) => {
            const vus = new Set();
            const out = [];
            for (const r of d.compteurs("admin_appels", de, a)) {
                const id = r.cle.slice(0, r.cle.indexOf("|"));
                if (vus.has(r.jour + id)) continue;
                vus.add(r.jour + id);
                out.push({ t: jourNumero(r.jour) * JOUR, v: 1, k: { admin: id } });
            }
            return out;
        },
    },
};

// Les mesures du releve du jour (des niveaux sans filtre) et leurs divisions.
const MESURES_RELEVE = Object.keys(MESURES).filter((n) => MESURES[n].releve);

// Un element passe-t-il les filtres ?
function passe(k, f) {
    if (f.client !== null && normClient(k.client) !== f.client) return false;
    if (f.admin !== null && k.admin !== f.admin) return false;
    if (f.licence !== null && k.licence !== f.licence) return false;
    if (f.logiciel !== null && !passeLogiciel(f.logiciel, k.logiciel || "")) return false;
    return true;
}
const sansFiltre = (f) => f.client === null && f.admin === null && f.licence === null && f.logiciel === null;

// Les intervalles [debut, fin) evalues a chaque instant (croissants) : {points, parts: Map(cle -> points)}.
function evaluer(intervalles, instants, par) {
    const ev = [];
    for (const iv of intervalles) {
        if (!(iv.fin > iv.debut)) continue;
        ev.push([iv.debut, iv.p, iv]);
        if (iv.fin !== Infinity) ev.push([iv.fin, -iv.p, iv]);
    }
    ev.sort((a, b) => a[0] - b[0]);
    const points = new Array(instants.length).fill(0);
    const parts = new Map();
    const cur = new Map();
    let total = 0;
    let j = 0;
    for (let i = 0; i < instants.length; i++) {
        while (j < ev.length && ev[j][0] <= instants[i]) {
            const [, p, iv] = ev[j++];
            total += p;
            if (par) {
                const c = String(iv.k[par] ?? "");
                cur.set(c, (cur.get(c) || 0) + p);
            }
        }
        points[i] = total;
        if (par)
            for (const [c, n] of cur) {
                if (!n && !parts.has(c)) continue;
                if (!parts.has(c)) parts.set(c, new Array(instants.length).fill(0));
                parts.get(c)[i] = n;
            }
    }
    return { points, parts };
}

// ============================================================================ les methodes de l'objet
export const RoutesStats = {
    // L'etat en memoire des statistiques (refait apres une restauration : oublierCaches).
    etatStats() {
        if (!this.stMem)
            this.stMem = {
                vus: new Map(), // cle -> t (le dernier appel signe)
                lieux: new Map(), // cle -> {sig, vu, logiciel}
                direct: [], // [{t, ville, genre}]
                directVu: new Map(), // cle -> t (la derniere connexion rendue en direct)
                compteurs: new Map(), // "jour\u0000mesure\u0000cle" -> {jour, mesure, cle, n, max, sale}
                ecrit: 0,
                ids: new Map(), // cle -> xpg-...
                infos: new Map(), // id licence -> {repere, infos}
            };
        return this.stMem;
    },

    // Le debut des releves (config stats_debut) : ecrit au premier appel de la 1.47.
    debutStats(now) {
        if (this.statsDebut === undefined || this.statsDebut === null) {
            const v = Number(this.config("stats_debut") || "0");
            if (v > 0) this.statsDebut = v;
            else {
                this.ecrireConfig("stats_debut", String(now));
                this.statsDebut = now;
            }
        }
        return this.statsDebut;
    },

    // L'identifiant (xpg-...) de la cle qui appelle ; "" pour un PC.
    async idAppelant(ctx) {
        const a = ctx.appelant;
        if (!a || a.role === "pc") return "";
        if (a.admin && a.admin.id) return a.admin.id;
        return this.idDeCle(a.cle, a.octets);
    },
    async idDeCle(cle, octets) {
        const st = this.etatStats();
        let id = st.ids.get(cle);
        if (!id) {
            id = await cleIdDe(octets);
            if (st.ids.size < 10000) st.ids.set(cle, id);
        }
        return id;
    },

    // Ce que dit le bloc d'une licence (garde en memoire tant que la licence ne change pas).
    infosLicence(id, bloc, maj) {
        const st = this.etatStats();
        const repere = maj + ":" + bloc.length;
        const x = st.infos.get(id);
        if (x && x.repere === repere) return x.infos;
        const infos = infosBloc(bloc);
        if (st.infos.size > 20000) st.infos.clear();
        st.infos.set(id, { repere, infos });
        return infos;
    },

    // ============================================================ les faits ==
    fait(t, genre, { licence = "", offre = "", client = "", admin = "", poste = "", valeur = 0, detail = null } = {}) {
        this.x(
            "INSERT INTO stats_faits (t, genre, licence, offre, client, admin, poste, valeur, detail) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            t,
            genre,
            licence,
            offre,
            client,
            admin,
            poste,
            Math.round(Number(valeur) || 0),
            detail ? JSON.stringify(detail) : "",
        );
    },
    // L'offre, le client et le signataire d'une licence de la table (lic : licenceDe).
    infosDe(lic) {
        return lic ? this.infosLicence(lic.id, lic.bloc, lic.emission) : { client: "", offre: "", options: [], achats: [], signataire: "" };
    },
    // Le client d'un PC (la licence de sa derniere activation).
    clientDuPc(cle) {
        const r = this.ligne("SELECT licence FROM activations WHERE poste = ? ORDER BY activee DESC LIMIT 1", cle);
        const lic = r ? this.licence(r.licence) : null;
        return { licence: r ? r.licence : "", client: this.infosDe(lic).client, offre: this.infosDe(lic).offre };
    },

    // deposerLicence : nouvelle, prolongee (la fin plus loin), une option ajoutee.
    faitsLicence(now, avant, blocNeuf, doc, admin) {
        const neuve = infosBloc(blocNeuf);
        const base = { licence: doc.id, offre: neuve.offre, client: neuve.client, admin };
        if (!avant) {
            this.fait(now, "licence_nouvelle", { ...base, valeur: doc.postesMax, detail: { type: doc.type } });
            return;
        }
        if (doc.emission <= avant.emission) return;
        if (avant.expire !== 0 && (doc.expire === 0 || doc.expire > avant.expire))
            this.fait(now, "licence_prolongee", { ...base, valeur: doc.expire ? Math.round((doc.expire - avant.expire) / JOUR) : 0 });
        const vieilles = this.infosDe(avant).options;
        const ajoutees = neuve.options.filter((o) => !vieilles.includes(o));
        if (ajoutees.length) this.fait(now, "licence_option", { ...base, valeur: ajoutees.length, detail: { options: ajoutees.slice(0, 20) } });
    },

    // deposerRevocations : les entrees neuves de la liste (une licence entiere, ou un PC).
    faitsRevocations(now, ancienne, nouvelle, admin) {
        const cle = (r) => r.licence + "|" + r.poste + "|" + r.avant;
        const avant = new Set((ancienne ? ancienne.revoquees : []).map(cle));
        let n = 0;
        for (const r of nouvelle.revoquees) {
            if (avant.has(cle(r))) continue;
            if (++n > 1000) break; // une liste remplacee d'un coup : les 1000 premieres
            const i = this.infosDe(this.licence(r.licence));
            this.fait(now, r.poste ? "pc_revoque" : "licence_revoquee", { licence: r.licence, offre: i.offre, client: i.client, admin, poste: r.poste });
        }
    },

    // Un PC : active, libere, retire (lic : la licence ; admin : la cle qui l'a fait, ou le signataire de la licence).
    faitPc(now, genre, lic, poste, admin) {
        const i = this.infosDe(lic);
        this.fait(now, genre, { licence: lic ? lic.id : "", offre: i.offre, client: i.client, admin: admin === undefined ? i.signataire : admin, poste });
    },

    // activer : la premiere activation d'une cle qui a une Decouverte -> convertie (une fois par cle).
    faitConversion(now, lic, cle) {
        const d = this.ligne("SELECT debut FROM decouvertes WHERE poste = ? ORDER BY debut LIMIT 1", cle);
        if (!d || d.debut > now) return;
        if (this.ligne("SELECT 1 AS x FROM stats_faits WHERE genre = 'decouverte_convertie' AND poste = ? LIMIT 1", cle)) return;
        const i = this.infosDe(lic);
        this.fait(now, "decouverte_convertie", { licence: lic.id, offre: i.offre, client: i.client, admin: i.signataire, poste: cle, valeur: now - d.debut });
    },

    // ============================================================ les appels ==
    // Chaque appel signe (traiter, apres la signature et le nonce) : le lieu de la cle, << actif maintenant >>, le
    // direct, les appels de l'admin par heure. Une ecriture seulement pour un lieu neuf ou change, ou une fois par heure.
    async noterAppelStats(ctx) {
        const a = ctx.appelant;
        if (!a) return;
        const st = this.etatStats();
        const now = ctx.now;
        if (st.vus.size >= MAX_MEMOIRE && !st.vus.has(a.cle)) this.elaguerStats(now, true);
        st.vus.set(a.cle, now);
        const entete = ctx.request.headers.get("X-XPG-Logiciel") || "";
        const m = /^([A-Za-z-]{1,30})\//.exec(entete);
        let logiciel = m ? m[1] : "";
        if (a.role === "admin" || a.role === "admin+") {
            const id = await this.idAppelant(ctx);
            this.compteur(now, "admin_appels", id + "|" + String(new Date(now * 1000).getUTCHours()).padStart(2, "0"), 1, false);
            if (!logiciel) logiciel = LOGICIEL_ADMIN;
        }
        const l = lireLieu(ctx.request.headers.get("X-XPG-Lieu") || "");
        if (!l) return;
        const genre = a.role !== "pc" || logiciel === LOGICIEL_ADMIN ? "admin" : "studio";
        // le direct : une connexion par cle et par minute au plus (en memoire seulement)
        const dv = st.directVu.get(a.cle) || 0;
        if (now - dv >= 60) {
            st.directVu.set(a.cle, now);
            st.direct.push({ t: now, ville: idVille(l), genre });
            if (st.direct.length > DIRECT_MAX) st.direct.splice(0, st.direct.length - DIRECT_MAX);
        }
        // la table : un lieu neuf, change, ou vu il y a plus d'une heure
        const sig = signatureLieu(l);
        let connu = st.lieux.get(a.cle);
        if (!connu) {
            const r = this.ligne("SELECT ville, region, pays, lat, lon, colo, vu, logiciel FROM lieux WHERE cle = ?", a.cle);
            connu = r ? { sig: signatureLieu(r), vu: r.vu, logiciel: r.logiciel } : null;
        }
        if (!logiciel) logiciel = connu ? connu.logiciel : "";
        if (connu && connu.sig === sig && now - connu.vu < ECRIRE_LIEU && connu.logiciel === logiciel) {
            if (st.lieux.size < MAX_MEMOIRE) st.lieux.set(a.cle, connu);
            return;
        }
        this.x(
            `INSERT INTO lieux (cle, poste, logiciel, ville, region, pays, lat, lon, colo, vu) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
             ON CONFLICT (cle) DO UPDATE SET poste = excluded.poste, logiciel = excluded.logiciel, ville = excluded.ville, region = excluded.region,
                 pays = excluded.pays, lat = excluded.lat, lon = excluded.lon, colo = excluded.colo, vu = excluded.vu`,
            a.cle,
            a.poste,
            logiciel,
            l.ville,
            l.region,
            l.pays,
            l.lat,
            l.lon,
            l.colo,
            now,
        );
        if (st.lieux.size < MAX_MEMOIRE || st.lieux.has(a.cle)) st.lieux.set(a.cle, { sig, vu: now, logiciel });
    },

    // Ce qui est trop vieux en memoire : les cles vues il y a plus de 15 minutes, le direct de plus de 15 minutes.
    elaguerStats(now, tout) {
        const st = this.etatStats();
        for (const [cle, t] of st.vus) if (now - t > ACTIF) st.vus.delete(cle);
        for (const [cle, t] of st.directVu) if (now - t > 60) st.directVu.delete(cle);
        const i = st.direct.findIndex((x) => now - x.t <= DIRECT_DUREE);
        if (i < 0) st.direct.length = 0;
        else if (i > 0) st.direct.splice(0, i);
        if (tout && st.lieux.size >= MAX_MEMOIRE) st.lieux.clear();
    },

    // Un compteur du jour, en memoire : `max` -> le plus grand ; sinon une somme.
    compteur(now, mesure, cle, n, max) {
        const st = this.etatStats();
        const jour = jourDe(now);
        const k = jour + "\u0000" + mesure + "\u0000" + cle;
        let c = st.compteurs.get(k);
        if (!c) {
            if (st.compteurs.size > 50000) return;
            c = { jour, mesure, cle, n: 0, max, sale: false };
            st.compteurs.set(k, c);
        }
        if (max) {
            if (n > c.n) {
                c.n = n;
                c.sale = true;
            }
        } else {
            c.n += n;
            c.sale = true;
        }
    },

    // Les compteurs ecrits (une fois par minute au plus ; `force` : maintenant). Une somme ajoute ce qui est en memoire
    // (puis repart de 0) ; un maximum garde le plus grand.
    ecrireCompteursStats(now, force) {
        const st = this.etatStats();
        if (!force && now - st.ecrit < ECRIRE_COMPTEURS) return;
        st.ecrit = now;
        this.elaguerStats(now, false);
        const jour = jourDe(now);
        for (const [k, c] of st.compteurs) {
            if (c.sale) {
                if (c.max)
                    this.x(
                        "INSERT INTO stats_compteurs (jour, mesure, cle, n) VALUES (?, ?, ?, ?) ON CONFLICT (jour, mesure, cle) DO UPDATE SET n = MAX(n, excluded.n)",
                        c.jour,
                        c.mesure,
                        c.cle,
                        c.n,
                    );
                else
                    this.x(
                        "INSERT INTO stats_compteurs (jour, mesure, cle, n) VALUES (?, ?, ?, ?) ON CONFLICT (jour, mesure, cle) DO UPDATE SET n = n + excluded.n",
                        c.jour,
                        c.mesure,
                        c.cle,
                        c.n,
                    );
                c.sale = false;
                if (!c.max) c.n = 0;
            }
            if (c.jour !== jour || (!c.max && !c.n)) st.compteurs.delete(k);
        }
    },

    // prendreJeton : le pic des jetons pris en meme temps (tous, et ceux de cette licence).
    noterJetons(now, licence, n) {
        this.compteur(now, "jetons_pic", "", this.nombre("SELECT COUNT(*) AS n FROM jetons WHERE jusqu_a > ?", now), true);
        this.compteur(now, "jetons_pic_licence", licence, n, true);
    },

    // noterLogiciel : la premiere fois qu'une cle est vue avec cette version.
    noterVersionVue(cle, logiciel, version, now) {
        this.x("INSERT INTO versions_vues (cle, version, logiciel, premiere) VALUES (?, ?, ?, ?) ON CONFLICT DO NOTHING", cle, version, logiciel, now);
    },

    // Le grand menage (une fois par jour) : ce qui depasse sa duree.
    grandMenageStats(now) {
        const vieux = now - GARDE_STATS;
        this.x("DELETE FROM stats_faits WHERE t < ?", vieux);
        this.x("DELETE FROM stats_quotidien WHERE jour < ?", jourDe(vieux));
        this.x("DELETE FROM stats_compteurs WHERE jour < ?", jourDe(vieux));
        this.x("DELETE FROM versions_vues WHERE premiere < ? AND cle NOT IN (SELECT cle FROM logiciels)", vieux);
        this.x("DELETE FROM lieux WHERE vu < ?", now - GARDE_LIEU);
    },

    // ============================================================ les donnees reconstituees ==
    // Les licences : creee (le premier depot ; avant la 1.47 : le premier achat << offer >>, sinon l'emission), fin
    // (expiree, revoquee en entier, desactivee), les cles des divisions (k : le signataire de l'emission en place ; kc :
    // qui l'a creee).
    licencesPourStats(d) {
        const creees = new Map();
        for (const f of d.faits("licence_nouvelle")) if (!creees.has(f.licence)) creees.set(f.licence, f);
        const revT = d.revocationsTemps();
        const liste = this.revocations();
        return this.lignes("SELECT id, bloc, emission, type, postes_max, expire, active, maj FROM licences").map((r) => {
            const i = this.infosLicence(r.id, r.bloc, r.emission);
            const ev = creees.get(r.id);
            let creee = ev ? ev.t : 0;
            if (!creee) {
                const offres = i.achats.filter((a) => a.kind === "offer").map((a) => a.at);
                creee = Math.min(r.emission, ...offres);
            }
            let fin = r.expire || Infinity;
            if (trouverRevocation(liste, r.id, "", r.emission)) fin = Math.min(fin, revT.get(r.id + "|") || r.maj);
            if (!r.active) fin = Math.min(fin, r.maj);
            const type = r.type === "floating" ? "flottante" : "fixe";
            const createur = ev ? ev.admin : i.signataire;
            return {
                id: r.id,
                client: i.client,
                offre: i.offre,
                type,
                postes: r.postes_max,
                expire: r.expire,
                emission: r.emission,
                signataire: i.signataire,
                achats: i.achats,
                creee,
                fin,
                k: { offre: i.offre, client: i.client, admin: i.signataire, type, licence: r.id },
                kc: { offre: i.offre, client: i.client, admin: createur, type, licence: r.id },
            };
        });
    },

    // Les activations : [activee, fin) (fin : le bail pour une active, la liberation ou le retrait sinon, la revocation).
    activationsPourStats(d) {
        const lic = d.licMap();
        const revT = d.revocationsTemps();
        const liste = this.revocations();
        const logiciels = d.logiciels();
        return this.lignes("SELECT licence, poste, nom, activee, vue, jusqu_a, etat FROM activations").map((r) => {
            const l = lic.get(r.licence);
            let fin = r.etat === "active" ? r.jusqu_a : r.vue;
            if (r.etat === "active" && l && trouverRevocation(liste, r.licence, r.poste, l.emission))
                fin = Math.min(fin, revT.get(r.licence + "|" + r.poste) || revT.get(r.licence + "|") || r.activee);
            const log = logiciels.get(r.poste);
            return {
                ...r,
                fin,
                k: {
                    offre: l ? l.offre : "",
                    client: l ? l.client : "",
                    admin: l ? l.signataire : "",
                    type: l ? l.type : "",
                    logiciel: log ? log.logiciel : "",
                    licence: r.licence,
                },
            };
        });
    },

    // Les versions de chaque cle dans le temps : [premiere, version suivante) dans sa vie [premiere vue, derniere vue +
    // 30 jours). `aJour` : seulement quand la version est la derniere publiee (ou plus recente).
    segmentsVersions(d, aJour) {
        const publiees = d.publiees();
        const vv = d.versionsVues();
        const out = [];
        for (const [cle, l] of d.logiciels()) {
            const lignes = vv.get(cle) || [{ version: l.version, logiciel: l.logiciel, premiere: l.vu }];
            const finVie = l.vu + VU_RECENT;
            const pc = d.clePc().get(cle);
            for (let i = 0; i < lignes.length; i++) {
                const debut = lignes[i].premiere;
                let fin = Math.min(i + 1 < lignes.length ? lignes[i + 1].premiere : Infinity, finVie);
                if (aJour) fin = Math.min(fin, finDeVersion(publiees, lignes[i].version));
                if (fin > debut)
                    out.push({
                        debut,
                        fin,
                        p: 1,
                        k: { logiciel: l.logiciel, version: lignes[i].version, client: pc ? pc.client : "", licence: pc ? pc.licence : "", admin: pc ? pc.admin : "" },
                    });
            }
        }
        return out;
    },

    // ============================================================ le calcul ==
    // Une mesure sur des periodes : {points, total, parts: Map}.
    calculerMesure(nom, d, per, par, f, releves) {
        const def = MESURES[nom];
        const n = per.length;
        const de = per[0][0];
        const a = per[n - 1][1];
        const now = d.now;
        const par2 = par && def.dims.includes(par) ? par : "";
        const g = def.genre;
        const ajoutPart = (parts, c, i, v) => {
            if (!parts.has(c)) parts.set(c, new Array(n).fill(g === "max" ? null : 0));
            const p = parts.get(c);
            p[i] = g === "max" ? Math.max(p[i] ?? 0, v) : p[i] + v;
        };
        if (g === "flux" || g === "max") {
            const items = def.jours ? def.jours(d, de, a, this) : def.avecObjet ? def.items(this, d) : def.items(d);
            const points = new Array(n).fill(g === "max" ? null : 0);
            const parts = new Map();
            for (const it of items) {
                if (!passe(it.k, f)) continue;
                const i = indice(per, numeroDuJour(it.t));
                if (i < 0) continue;
                points[i] = g === "max" ? Math.max(points[i] ?? 0, it.v) : points[i] + it.v;
                if (par2) ajoutPart(parts, String(it.k[par2] ?? ""), i, it.v);
            }
            if (g === "max") {
                // un jour releve sans jeton pris : 0 (avant les releves : inconnu)
                const debutJour = numeroDuJour(d.debut);
                for (let i = 0; i < n; i++) if (points[i] === null && per[i][1] >= debutJour && per[i][0] <= numeroDuJour(now)) points[i] = 0;
            }
            const total = g === "max" ? points.reduce((m, v) => (v === null ? m : Math.max(m ?? 0, v)), null) : points.reduce((s, v) => s + v, 0);
            return { points, total, parts, totalPart: (p) => (g === "max" ? p.reduce((m, v) => (v === null ? m : Math.max(m ?? 0, v)), null) : p.reduce((s, v) => s + v, 0)) };
        }
        if (g === "ratio") {
            const valeurs = per.map(() => []);
            const tous = [];
            const vParts = new Map();
            for (const it of def.items(d)) {
                if (!passe(it.k, f)) continue;
                const i = indice(per, numeroDuJour(it.t));
                if (i < 0) continue;
                valeurs[i].push(it.v);
                tous.push(it.v);
                if (par2) {
                    const c = String(it.k[par2] ?? "");
                    if (!vParts.has(c)) vParts.set(c, { v: per.map(() => []), tous: [] });
                    vParts.get(c).v[i].push(it.v);
                    vParts.get(c).tous.push(it.v);
                }
            }
            const parts = new Map();
            const totaux = new Map();
            for (const [c, x] of vParts) {
                parts.set(c, x.v.map(def.calcul));
                totaux.set(c, def.calcul(x.tous));
            }
            return { points: valeurs.map(def.calcul), total: def.calcul(tous), parts, totalPart: (p, c) => totaux.get(c) };
        }
        if (g === "cumul") {
            // depuis le 1er janvier de l'annee de la fin de chaque periode
            const items = def.items(d).filter((it) => passe(it.k, f));
            items.sort((x, y) => x.t - y.t);
            const points = new Array(n).fill(0);
            const parts = new Map();
            for (let i = 0; i < n; i++) {
                const fin = Math.min((per[i][1] + 1) * JOUR - 1, now);
                const an = new Date(fin * 1000).getUTCFullYear();
                const debut = Date.UTC(an, 0, 1) / 1000;
                for (const it of items) {
                    if (it.t < debut) continue;
                    if (it.t > fin) break;
                    points[i] += it.v;
                    if (par2) ajoutPart(parts, String(it.k[par2] ?? ""), i, it.v);
                }
            }
            return { points, total: points[n - 1], parts, totalPart: (p) => p[n - 1] };
        }
        // niveau, age : a la fin de chaque periode (maintenant pour la periode en cours)
        const instants = per.map(([, b]) => Math.min((b + 1) * JOUR - 1, now));
        const ivs = def.intervalles(d, this, de).filter((iv) => passe(iv.k, f));
        let res;
        if (g === "age") {
            const points = instants.map((T) => {
                let m = 0;
                for (const iv of ivs) if (iv.debut <= T && iv.fin > T) m = Math.max(m, T - iv.debut);
                return m;
            });
            res = { points, parts: new Map() };
        } else res = evaluer(ivs, instants, par2);
        // le releve du jour suivant la fin de la periode (sans filtre, une division qu'il garde), s'il existe
        if (releves && def.releve && sansFiltre(f) && (!par2 || def.releve.includes(par2))) {
            for (let i = 0; i < n; i++) {
                if ((per[i][1] + 1) * JOUR - 1 >= now) continue;
                const r = releves.get(jourTexte(per[i][1] + 1));
                const m = r && r[nom];
                if (!m || typeof m.total !== "number") continue;
                res.points[i] = m.total;
                if (par2) {
                    for (const p of res.parts.values()) p[i] = 0;
                    const pp = m.parts && m.parts[par2];
                    if (pp && typeof pp === "object")
                        for (const [c, v] of Object.entries(pp)) {
                            if (!res.parts.has(c)) res.parts.set(c, new Array(n).fill(0));
                            res.parts.get(c)[i] = Number(v) || 0;
                        }
                }
            }
        }
        return { points: res.points, total: res.points[n - 1], parts: res.parts, totalPart: (p) => p[n - 1] };
    },

    // Les parts rendues : les 50 plus grandes (par total), puis "\u2026" (le reste ; un ratio : rien).
    partsRendues(calc, genre) {
        const liste = [...calc.parts].map(([cle, points]) => ({ cle, points, total: calc.totalPart(points, cle) }));
        liste.sort((x, y) => (y.total ?? -1) - (x.total ?? -1) || (x.cle < y.cle ? -1 : 1));
        if (liste.length <= MAX_PARTS) return liste;
        const gardees = liste.slice(0, MAX_PARTS);
        if (genre === "ratio" || genre === "age") return gardees;
        const reste = liste.slice(MAX_PARTS);
        const points = reste[0].points.map((_, i) => reste.reduce((s, x) => (x.points[i] === null ? s : (s ?? 0) + x.points[i]), null));
        gardees.push({ cle: "…", points, total: calc.totalPart(points, "…") });
        return gardees;
    },

    relevesEntre(de, a) {
        const m = new Map();
        for (const r of this.lignes("SELECT jour, donnees FROM stats_quotidien WHERE jour >= ? AND jour <= ?", jourTexte(de), jourTexte(a + 1)))
            try {
                m.set(r.jour, JSON.parse(r.donnees));
            } catch {
                // un releve illisible : reconstitue
            }
        return m;
    },

    // Les filtres de l'adresse : {client, admin, licence, logiciel} (null : aucun).
    filtresDe(p) {
        const f = { client: null, admin: null, licence: null, logiciel: null };
        if (p.get("client") !== null && p.get("client") !== "") f.client = normClient(p.get("client")).slice(0, 200);
        if (p.get("admin") !== null && p.get("admin") !== "") {
            if (!/^xpg-[0-9a-f]{16}$/.test(p.get("admin"))) throw mal("admin", "« admin » doit être l'identifiant d'une clé (xpg- et 16 hexadécimaux).");
            f.admin = p.get("admin");
        }
        if (p.get("licence") !== null && p.get("licence") !== "") f.licence = String(p.get("licence")).slice(0, 64);
        if (p.get("logiciel") !== null && p.get("logiciel") !== "") f.logiciel = String(p.get("logiciel")).slice(0, 40);
        return f;
    },

    // ============================================================ GET /admin/stats/serie ==
    async serieStats(ctx) {
        const p = ctx.url.searchParams;
        const noms = String(p.get("mesures") || "")
            .split(",")
            .map((s) => s.trim())
            .filter(Boolean);
        if (!noms.length || noms.length > MAX_MESURES) throw mal("mesures", "« mesures » : une à " + MAX_MESURES + " mesures, séparées par des virgules.");
        for (const n of noms) if (!Object.prototype.hasOwnProperty.call(MESURES, n)) throw new Erreur(400, "champ", "La mesure « " + n.slice(0, 60) + " » n'existe pas.");
        const { du, au } = periodeDe(p, ctx.now, 30);
        const pas = p.get("pas") || "jour";
        if (!PAS.includes(pas)) throw mal("pas", "« pas » : jour, semaine ou mois.");
        const comparer = p.get("comparer") || "avant";
        if (!COMPARER.includes(comparer)) throw mal("comparer", "« comparer » : avant, an ou rien.");
        const par = p.get("par") || "";
        if (par && !PAR.includes(par)) throw mal("par", "« par » : " + PAR.join(", ") + ".");
        const f = this.filtresDe(p);
        const per = decouper(du, au, pas);
        const longueur = au - du + 1;
        const perAvant = comparer === "rien" ? null : per.map(([x, y]) => (comparer === "avant" ? [x - longueur, y - longueur] : [anAvant(x), anAvant(y)]));
        this.ecrireCompteursStats(ctx.now, true);
        const d = donnees(this, ctx.now);
        const de = perAvant ? Math.min(du, perAvant[0][0]) : du;
        const releves = this.relevesEntre(de, au);
        const moi = await this.idAppelant(ctx);
        const mesures = {};
        for (const nom of noms) {
            const def = MESURES[nom];
            if (def.droit && ctx.appelant.role === "admin" && !ctx.appelant.droits.includes(def.droit)) {
                mesures[nom] = { refuse: "droit", droits: [def.droit] };
                continue;
            }
            const fm = def.soi && ctx.appelant.role === "admin" ? { ...f, admin: moi } : f;
            const manque = ["client", "admin", "licence", "logiciel"].find((x) => fm[x] !== null && !def.dims.includes(x));
            if (manque) {
                mesures[nom] = { refuse: "filtre", filtre: manque };
                continue;
            }
            const c = this.calculerMesure(nom, d, per, par, fm, releves);
            const m = { unite: def.unite, genre: def.genre === "age" ? "niveau" : def.genre, source: def.source, points: c.points, total: c.total };
            if (par && def.dims.includes(par)) m.parts = this.partsRendues(c, def.genre);
            if (perAvant) {
                const ca = this.calculerMesure(nom, d, perAvant, "", fm, releves);
                m.avant = { points: ca.points, total: ca.total };
                m.variation = typeof ca.total === "number" && ca.total !== 0 && typeof c.total === "number" ? Math.round(((c.total - ca.total) / ca.total) * 1000) / 10 : null;
            }
            mesures[nom] = m;
        }
        const rep = {
            version: this.versionService,
            heure: ctx.now,
            releves_depuis: jourDe(d.debut),
            du: jourTexte(du),
            au: jourTexte(au),
            pas,
            par,
            comparer,
            periodes: per.map(([x, y]) => ({ du: jourTexte(x), au: jourTexte(y) })),
        };
        if (perAvant)
            rep.avant = {
                du: jourTexte(perAvant[0][0]),
                au: jourTexte(perAvant[perAvant.length - 1][1]),
                periodes: perAvant.map(([x, y]) => ({ du: jourTexte(x), au: jourTexte(y) })),
            };
        rep.mesures = mesures;
        return reponseJson(200, rep);
    },

    // ============================================================ le releve du jour ==
    // Les niveaux maintenant (sans filtre), et leurs divisions : le releve d'un jour.
    niveauxMaintenant(now) {
        const d = donnees(this, now);
        const per = [[numeroDuJour(now), numeroDuJour(now)]];
        const f = { client: null, admin: null, licence: null, logiciel: null };
        const out = {};
        for (const nom of MESURES_RELEVE) {
            const def = MESURES[nom];
            const c = this.calculerMesure(nom, d, per, "", f, null);
            const m = { total: c.total };
            if (def.releve.length) {
                m.parts = {};
                const ivs = def.intervalles(d, this, per[0][0]);
                for (const par of def.releve) {
                    const r = evaluer(ivs, [now], par);
                    m.parts[par] = {};
                    for (const [cle, pts] of r.parts) if (pts[0]) m.parts[par][cle] = pts[0];
                }
            }
            out[nom] = m;
        }
        out.version_derniere = this.config("version_derniere") || "";
        return out;
    },

    // Le releve d'aujourd'hui (UTC), s'il n'est pas encore pris : {jour, pris, releve}.
    releverJour(now) {
        const jour = jourDe(now);
        const r = this.ligne("SELECT donnees FROM stats_quotidien WHERE jour = ?", jour);
        if (r) {
            let releve = null;
            try {
                releve = JSON.parse(r.donnees);
            } catch {
                releve = null;
            }
            return { jour, pris: false, releve };
        }
        this.ecrireCompteursStats(now, true);
        const releve = this.niveauxMaintenant(now);
        this.x("INSERT INTO stats_quotidien (jour, heure, donnees) VALUES (?, ?, ?) ON CONFLICT (jour) DO NOTHING", jour, now, JSON.stringify(releve));
        return { jour, pris: true, releve };
    },

    // POST /admin/stats/releve (admin+).
    async releveStats(ctx) {
        this.corpsJson(ctx);
        return reponseJson(200, this.releverJour(ctx.now));
    },

    // L'alarme de l'objet : le releve du jour (vers 00:05 UTC), puis la suivante.
    async programmerReleve() {
        try {
            if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(prochainReleve(Date.now()));
        } catch {
            // pas d'alarme : le premier appel du jour prend le releve
        }
    },

    // ============================================================ GET /admin/stats/mises-a-jour ==
    async misesAJourStats(ctx) {
        const p = ctx.url.searchParams;
        const { du, au } = periodeDe(p, ctx.now, 90);
        const pas = p.get("pas") || "semaine";
        if (!PAS.includes(pas)) throw mal("pas", "« pas » : jour, semaine ou mois.");
        const jours = p.get("jours") === null ? 42 : Number(p.get("jours"));
        if (!Number.isInteger(jours) || jours < 1 || jours > 120) throw mal("jours", "« jours » : de 1 à 120.");
        const groupe = p.get("logiciel") || "tous";
        if (!["tous", "studios", "admins"].includes(groupe)) throw mal("logiciel", "« logiciel » : studios, admins ou tous.");
        const groupes = groupe === "tous" ? ["studios", "admins"] : [groupe];
        this.ecrireCompteursStats(ctx.now, true);
        const d = donnees(this, ctx.now);
        const now = ctx.now;
        const publiees = d.publiees();
        const derniere = publiees.length ? publiees[publiees.length - 1] : null;
        const depuis = du * JOUR;
        const adminsIds = new Map(this.lignes("SELECT cle, id FROM admins").map((r) => [r.cle, r.id]));
        for (const k of await this.clesEditeur()) adminsIds.set(k.hex, k.id);
        // les cles vues dans la periode : leur version (celle de leur derniere connexion)
        const parVersion = new Map();
        const vus = { studios: 0, admins: 0 };
        const aJour = { studios: 0, admins: 0 };
        const enRetard = [];
        for (const [cle, l] of d.logiciels()) {
            const g = groupeDe(l.logiciel);
            if (!g || !groupes.includes(g) || l.vu < depuis) continue;
            vus[g]++;
            if (!parVersion.has(l.version)) parVersion.set(l.version, { studios: 0, admins: 0 });
            parVersion.get(l.version)[g]++;
            const n = versionEnNombres(l.version);
            const retard = n ? publiees.filter((v) => comparerVersions(v.n, n) > 0).length : publiees.length;
            if (!retard) aJour[g]++;
            else enRetard.push({ cle, l, retard });
        }
        const versions = [];
        const toutes = new Set([...parVersion.keys(), ...publiees.map((v) => v.version)]);
        for (const v of toutes) {
            const pub = publiees.find((x) => x.version === v);
            const c = parVersion.get(v) || { studios: 0, admins: 0 };
            versions.push({ version: v, publiee: pub ? pub.publiee : 0, studios: c.studios, admins: c.admins });
        }
        versions.sort((x, y) => {
            const a = versionEnNombres(x.version);
            const b = versionEnNombres(y.version);
            if (a && b) return -comparerVersions(a, b);
            return a ? -1 : b ? 1 : x.version < y.version ? -1 : 1;
        });
        // l'adoption et le delai de chaque version publiee dans les 400 derniers jours
        const adoption = [];
        const delais = [];
        const vv = d.versionsVues();
        for (let iv = 0; iv < publiees.length; iv++) {
            const V = publiees[iv];
            if (!V.publiee || V.publiee < now - 400 * JOUR || V.publiee > now) continue;
            const suivante = publiees.slice(iv + 1).reduce((m, x) => (x.publiee > V.publiee ? Math.min(m, x.publiee) : m), Infinity);
            const kmax = Math.min(jours - 1, Math.floor((Math.min(suivante, now) - V.publiee) / JOUR));
            for (const g of groupes) {
                let base = 0;
                const quand = [];
                const delai = [];
                for (const [cle, l] of d.logiciels()) {
                    if (groupeDe(l.logiciel) !== g) continue;
                    const lignes = vv.get(cle) || [{ version: l.version, premiere: l.vu }];
                    if (l.vu >= V.publiee - VU_RECENT) base++;
                    let t = Infinity;
                    let avant = false;
                    for (const r of lignes) {
                        const n = versionEnNombres(r.version);
                        if (n && comparerVersions(n, V.n) >= 0) t = Math.min(t, r.premiere);
                        else if (r.premiere < V.publiee) avant = true;
                    }
                    if (t !== Infinity) {
                        quand.push(t);
                        if (avant) delai.push(Math.max(0, t - V.publiee));
                    }
                }
                const courbe = [];
                for (let k = 0; k <= kmax; k++) {
                    const T = V.publiee + (k + 1) * JOUR - 1;
                    courbe.push(quand.filter((t) => t <= T).length);
                }
                adoption.push({ version: V.version, publiee: V.publiee, logiciel: g, base, jours: courbe });
                if (delai.length) delais.push({ version: V.version, logiciel: g, nombre: delai.length, median: mediane(delai), moyen: moyenne(delai) });
            }
        }
        // la part des PC a jour dans le temps
        const per = decouper(du, au, pas);
        const releves = this.relevesEntre(du, au);
        const f = { client: null, admin: null, licence: null, logiciel: null };
        const cAJour = this.calculerMesure("pc_a_jour", d, per, "logiciel", f, releves);
        const cVus = this.calculerMesure("pc_vus", d, per, "logiciel", f, releves);
        const somme = (c, g) => per.map((_, i) => [...c.parts].filter(([cle]) => groupeDe(cle) === g).reduce((s, [, pts]) => s + (pts[i] || 0), 0));
        const part = {};
        for (const g of groupes) part[g] = { a_jour: somme(cAJour, g), vus: somme(cVus, g) };
        // les PC pas a jour, les plus en retard d'abord
        enRetard.sort((x, y) => y.retard - x.retard || y.l.vu - x.l.vu);
        const pasAJour = enRetard.slice(0, MAX_PAS_A_JOUR).map(({ cle, l, retard }) => {
            const pc = d.clePc().get(cle);
            return {
                cle,
                logiciel: l.logiciel,
                version: l.version,
                retard,
                vu: l.vu,
                nom: pc ? pc.nom : "",
                client: pc ? pc.client : "",
                licence: pc ? pc.licence : "",
                admin: adminsIds.get(cle) || "",
            };
        });
        return reponseJson(200, {
            version: this.versionService,
            heure: now,
            releves_depuis: jourDe(d.debut),
            derniere: derniere ? { version: derniere.version, publiee: derniere.publiee } : { version: "", publiee: 0 },
            versions,
            vus: { ...vus, depuis: jourTexte(du) },
            a_jour: aJour,
            adoption,
            delais,
            periodes: per.map(([x, y]) => ({ du: jourTexte(x), au: jourTexte(y) })),
            part,
            pas_a_jour: pasAJour,
            pas_a_jour_total: enRetard.length,
        });
    },

    // ============================================================ GET /admin/stats/admins ==
    async adminsStats(ctx) {
        const p = ctx.url.searchParams;
        const { du, au } = periodeDe(p, ctx.now, 30);
        if (au - du + 1 > MAX_JOURS_ADMINS) throw mal("du", "La période des admins : " + MAX_JOURS_ADMINS + " jours au plus.");
        this.ecrireCompteursStats(ctx.now, true);
        const moi = await this.idAppelant(ctx);
        const lignes = [];
        for (const k of await this.clesEditeur()) lignes.push({ id: k.id, cle: k.hex, role: "admin+", nom: "", retire: 0 });
        for (const r of this.lignes("SELECT id, cle, nom, retire FROM admins ORDER BY depuis, cle")) lignes.push({ id: r.id, cle: r.cle, role: "admin", nom: r.nom, retire: r.retire });
        const parId = new Map();
        for (const r of this.lignes("SELECT jour, cle, n FROM stats_compteurs WHERE jour >= ? AND jour <= ? AND mesure = 'admin_appels'", jourTexte(du), jourTexte(au))) {
            const i = r.cle.indexOf("|");
            const id = r.cle.slice(0, i);
            const h = Number(r.cle.slice(i + 1));
            if (!(h >= 0 && h < 24)) continue;
            if (!parId.has(id)) parId.set(id, new Map());
            const j = parId.get(id);
            if (!j.has(r.jour)) j.set(r.jour, new Array(24).fill(0));
            j.get(r.jour)[h] += r.n;
        }
        const admins = [];
        for (const a of lignes) {
            if (ctx.appelant.role === "admin" && a.id !== moi) continue;
            const j = parId.get(a.id) || new Map();
            const heures = new Array(24).fill(0);
            const debuts = [];
            const fins = [];
            for (const h of j.values()) {
                h.forEach((n, i) => (heures[i] += n));
                debuts.push(h.findIndex((n) => n > 0));
                fins.push(23 - [...h].reverse().findIndex((n) => n > 0));
            }
            const log = this.logicielDe(a.cle);
            const lieu = this.ligne("SELECT ville, pays FROM lieux WHERE cle = ?", a.cle);
            admins.push({
                id: a.id,
                cle: a.cle,
                role: a.role,
                nom: a.nom,
                retire: a.retire,
                jours: [...j.keys()].sort(),
                heures,
                appels: heures.reduce((s, n) => s + n, 0),
                debut_habituel: debuts.length ? mediane(debuts) : -1,
                fin_habituelle: fins.length ? mediane(fins) : -1,
                vu: log.vu,
                logiciel: log.logiciel,
                logiciel_version: log.version,
                ville: lieu ? lieu.ville : "",
                pays: lieu ? lieu.pays : "",
            });
        }
        return reponseJson(200, { du: jourTexte(du), au: jourTexte(au), releves_depuis: jourDe(this.debutStats(ctx.now)), admins });
    },

    // ============================================================ le globe ==
    async globeStats(ctx) {
        const p = ctx.url.searchParams;
        const periode = p.get("periode") === null ? 30 : Number(p.get("periode"));
        if (!Number.isInteger(periode) || periode < 1 || periode > 400) throw mal("periode", "« periode » : un nombre de jours, de 1 à 400.");
        const now = ctx.now;
        const st = this.etatStats();
        const d = donnees(this, now);
        const voitDemandes = ctx.appelant.role !== "admin" || ctx.appelant.droits.includes("demandes");
        // les cles des admins (pas retires) et de l'editeur
        const admins = new Map(this.lignes("SELECT cle, id FROM admins WHERE retire = 0").map((r) => [r.cle, r.id]));
        for (const k of await this.clesEditeur()) admins.set(k.hex, k.id);
        const derniere = this.config("version_derniere") || "";
        const nDerniere = versionEnNombres(derniere);
        // les PC actives : cle -> {clients}
        const actives = new Map();
        for (const a of d.activations()) {
            if (a.etat !== "active" || a.fin <= now) continue;
            if (!actives.has(a.poste)) actives.set(a.poste, new Set());
            if (a.k.client) actives.get(a.poste).add(a.k.client);
        }
        // les Decouvertes en cours, les commandes de la periode, les demandes en attente : par cle (ou par poste)
        const conv = new Set(d.faits("decouverte_convertie").map((f) => f.poste));
        const decouvertes = new Map();
        for (const r of this.lignes("SELECT poste FROM decouvertes WHERE debut > ?", now - d.jours * JOUR)) if (!conv.has(r.poste) && !actives.has(r.poste)) decouvertes.set(r.poste, (decouvertes.get(r.poste) || 0) + 1);
        const commandes = new Map();
        for (const r of this.lignes("SELECT cle_pc FROM commandes WHERE paye > ?", now - periode * JOUR)) commandes.set(r.cle_pc, (commandes.get(r.cle_pc) || 0) + 1);
        const demandes = new Map();
        if (voitDemandes) for (const r of this.lignes("SELECT poste FROM demandes WHERE etat = 'attente'")) demandes.set(r.poste, (demandes.get(r.poste) || 0) + 1);
        const villes = new Map();
        const totaux = { villes: 0, pays: 0, clients: new Set(), pc: 0, actifs: 0, pas_a_jour: new Set(), admins: 0, decouvertes: 0, commandes: 0, demandes: 0 };
        const placees = new Set();
        for (const l of this.lignes("SELECT cle, poste, logiciel, ville, region, pays, lat, lon, colo, vu FROM lieux")) {
            placees.add(l.cle);
            const id = idVille(l);
            let v = villes.get(id);
            if (!v) {
                v = {
                    id,
                    ville: l.ville,
                    region: l.region,
                    pays: l.pays,
                    sLat: 0,
                    sLon: 0,
                    nCoord: 0,
                    colo: l.colo,
                    clients: new Set(),
                    pc: 0,
                    actifs: 0,
                    pas_a_jour: new Set(),
                    admins: 0,
                    decouvertes: 0,
                    commandes: 0,
                    demandes: 0,
                    postes: [],
                    ids_admins: [],
                };
                villes.set(id, v);
            }
            v.sLat += l.lat;
            v.sLon += l.lon;
            v.nCoord++;
            if (admins.has(l.cle)) {
                v.admins++;
                v.ids_admins.push(admins.get(l.cle));
                totaux.admins++;
                continue;
            }
            if (l.logiciel === LOGICIEL_ADMIN) continue; // un Admin qui n'est (plus) pas dans la liste
            const vu = Math.max(st.vus.get(l.cle) || 0, l.vu);
            if (now - vu <= ACTIF) {
                v.actifs++;
                totaux.actifs++;
            }
            const clients = actives.get(l.cle);
            if (clients) {
                v.pc++;
                totaux.pc++;
                if (v.postes.length < MAX_POSTES_VILLE) v.postes.push(l.cle);
                for (const c of clients) {
                    v.clients.add(c);
                    totaux.clients.add(c);
                }
                const log = d.logiciels().get(l.cle);
                const n = log ? versionEnNombres(log.version) : null;
                if (nDerniere && STUDIOS.includes(log ? log.logiciel : "") && (!n || comparerVersions(n, nDerniere) < 0))
                    for (const c of clients) {
                        v.pas_a_jour.add(c);
                        totaux.pas_a_jour.add(c);
                    }
            }
            const dc = decouvertes.get(l.cle) || 0;
            v.decouvertes += dc;
            totaux.decouvertes += dc;
            const cm = commandes.get(l.cle) || 0;
            v.commandes += cm;
            totaux.commandes += cm;
            const dm = demandes.get(l.poste) || 0;
            v.demandes += dm;
            totaux.demandes += dm;
        }
        const pays = new Set();
        const liste = [];
        for (const v of villes.values()) {
            if (!(v.pc || v.admins || v.actifs || v.decouvertes || v.commandes || v.demandes)) continue;
            pays.add(v.pays);
            liste.push({
                id: v.id,
                ville: v.ville,
                region: v.region,
                pays: v.pays,
                lat: Math.round((v.sLat / v.nCoord) * 100) / 100,
                lon: Math.round((v.sLon / v.nCoord) * 100) / 100,
                colo: v.colo,
                clients: v.clients.size,
                pc: v.pc,
                actifs: v.actifs,
                pas_a_jour: v.pas_a_jour.size,
                admins: v.admins,
                decouvertes: v.decouvertes,
                commandes: v.commandes,
                demandes: v.demandes,
                noms_clients: [...v.clients].sort().slice(0, MAX_NOMS_VILLE),
                postes: v.postes,
                ids_admins: v.ids_admins,
            });
        }
        liste.sort((x, y) => y.pc + y.admins - (x.pc + x.admins) || y.actifs - x.actifs || (x.id < y.id ? -1 : 1));
        let sansPc = 0;
        for (const cle of actives.keys()) if (!placees.has(cle)) sansPc++;
        let sansAdmins = 0;
        for (const cle of admins.keys()) if (!placees.has(cle)) sansAdmins++;
        return reponseJson(200, {
            version: this.versionService,
            heure: now,
            periode,
            version_derniere: derniere,
            villes: liste,
            totaux: {
                villes: liste.length,
                pays: pays.size,
                clients: totaux.clients.size,
                pc: totaux.pc,
                actifs: totaux.actifs,
                pas_a_jour: totaux.pas_a_jour.size,
                admins: totaux.admins,
                decouvertes: totaux.decouvertes,
                commandes: totaux.commandes,
                demandes: totaux.demandes,
            },
            sans_lieu: { pc: sansPc, admins: sansAdmins },
        });
    },

    // GET /admin/globe/direct?depuis=<unix> : les connexions depuis `depuis` (15 minutes au plus), en memoire.
    async globeDirect(ctx) {
        const d = ctx.url.searchParams.get("depuis");
        if (d !== null && !/^\d{1,12}$/.test(d)) throw mal("depuis", "« depuis » doit être une heure (secondes Unix).");
        const now = ctx.now;
        const depuis = Math.max(d === null ? 0 : Number(d), now - DIRECT_DUREE);
        const st = this.etatStats();
        this.elaguerStats(now, false);
        const connexions = st.direct.filter((x) => x.t > depuis).slice(-DIRECT_RENDUS);
        const admins = new Set(this.lignes("SELECT cle FROM admins").map((r) => r.cle));
        for (const k of await this.clesEditeur()) admins.add(k.hex);
        let actifs = 0;
        for (const [cle, t] of st.vus) if (now - t <= ACTIF && !admins.has(cle)) actifs++;
        return reponseJson(200, { heure: now, actifs, connexions });
    },
};

// Le prochain releve : demain (UTC) a 00:05.
export function prochainReleve(ms) {
    const j = Math.floor(ms / 86400000) + 1;
    return j * 86400000 + 5 * 60000;
}
