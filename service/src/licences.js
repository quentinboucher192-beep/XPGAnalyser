// =============================================================================
//  service/src/licences.js - 1.35.0 : le Durable Object << principal >> du
//  service des licences (SQLite) : toutes les routes du contrat
//  (docs/SERVICE-LICENCES.md)
//  1.38.0 : LES PC DE LA LICENCE (<< Mes appareils >> dans le Studio) - le
//  renouvellement rend aussi `postes` : pour chaque licence renouvelee pour CE
//  PC (et seulement elles), ses PC qui tiennent une place (nom, cle, dates,
//  etat, ce_pc) et le nombre de places prises (les jetons en cours pour une
//  flottante). Jamais la station d'un autre PC, jamais un PC revoque.
//  1.39.0 : LA LICENCE PERDUE EN DIRECT - GET /v1/etat dit aussi `revocations`
//  (le numero de la liste) et `changements` (un compteur qui monte a chaque
//  revocation, licence deposee ou desactivee, PC retire, etat de poste depose) ;
//  les Studios le regardent toutes les 2 minutes et se resynchronisent s'il bouge.
//  1.44.0 : PLUSIEURS ADMINS (docs/ADMINS.md) - les routes /admin acceptent la
//  cle de l'editeur (<< admin+ >>) ou la cle d'un admin de la liste signee par
//  l'editeur, non retire (<< admin >>, avec ses droits) ; un document depose par
//  un admin est signe par SA cle, et il faut le droit de ce qu'il change. La
//  liste, les demandes d'admin, le trousseau, le journal : admins.js ; les
//  fichiers, les messages a un PC, les versions : fichiers.js ; la sauvegarde
//  et la restauration du service entier : sauvegarde.js. Le logiciel de chaque
//  cle (X-XPG-Logiciel de ses appels signes) est garde (logiciels).
//  1.47.0 : LES STATISTIQUES ET LE GLOBE (docs/STATISTIQUES.md, stats.js) - les
//  faits (une licence deposee, un PC active...), le releve de chaque jour (une
//  alarme de l'objet), les compteurs du jour, les versions vues, le lieu de la
//  derniere connexion de chaque cle (jamais son adresse IP) ; les routes
//  /admin/stats/* et /admin/globe*.
// -----------------------------------------------------------------------------
//  LES TABLES
//    config        la cle du service (creee ici au premier appel ; la partie
//                  secrete ne sort jamais), le certificat, le registre, la
//                  liste de revocation (privee), les compteurs (numeros, et le
//                  nombre de lignes des grandes tables : jamais de COUNT(*) sur
//                  elles a chaque appel)
//    licences      les licences en ligne : le bloc signe par l'editeur (la
//                  derniere emission), le type, les postes, le bail, l'EMPREINTE
//                  du code (jamais le code), active
//    activations   une ligne par (licence, PC) : numero, station (l'empreinte de
//                  l'installation de Windows du PC), bail (jusqu_a), etat, et
//                  place_jusqu (un PC libere au-dela des transferts permis garde
//                  sa place jusqu'a la fin de son bail)
//    liberations   les liberations faites par les PC eux-memes qui ont rendu leur
//                  place tout de suite (les transferts : 30 jours glissants)
//    jetons        les jetons des licences flottantes (15 min), par (licence, PC,
//                  station)
//    decouvertes   la premiere fois qu'une machine a ete vue (gardee a vie)
//    demandes      les demandes manuelles (scellees pour l'editeur) ; 1.44.0 :
//                  `autres` = les octets des scelles pour d'autres cles
//    demandes_scelles  1.44.0 : une demande scellee aussi pour d'autres cles
//                  (un admin qui a le droit << demandes >>, une autre cle de
//                  l'editeur) : (numero, cle, scelle)
//    postes        l'etat d'un PC (bloc << poste >> signe par l'editeur)
//    admins, admins_demandes, trousseau, journal          1.44.0 : admins.js
//    fichiers, morceaux, fichiers_jetons, messages,
//    versions, logiciels                                  1.44.0 : fichiers.js
//    archive       l'archive privee de l'editeur (chiffree par lui) et sa version
//    nonces        les nonces vus (20 min : un appel rejoue est refuse)
//    debits        les compteurs des limites (par adresse, par PC)
//    stats_jour    les appels du jour : UNE ligne par jour (JSON), ecrite une fois
//                  par minute au plus (les compteurs sont tenus en memoire)
//
//  LES ECRITURES (le plan gratuit compte les lignes ecrites, 100 000 par jour) :
//  un appel signe ecrit son nonce, et une ligne de limite par PC pour les routes
//  limitees, en plus de son travail ; les routes publiques n'ecrivent rien (leur
//  limite par adresse est celle de Cloudflare, devant le service : index.js).
//  Pres de 127.0.0.1, la reponse dit combien de lignes l'appel a ecrites
//  (X-XPG-Lignes-Ecrites : les essais les comptent).
//  LES MENAGES : toutes les 5 minutes (nonces, jetons, limites), et une fois par
//  jour (statistiques, demandes closes apres 7 jours, activations finies,
//  liberations de plus de 30 jours).
//
//  LES APPELS SONT TRAITES UN PAR UN (une file) : un numero, une place, une
//  version de l'archive ne sont jamais donnes deux fois.
//  RIEN N'EST ECRIT DANS UN JOURNAL : ni un corps, ni un code, ni une cle.
// =============================================================================
import { DurableObject } from "cloudflare:workers";

import { DROITS_ADMINS, DROITS_LICENCE, DROITS_REGISTRE, GARDE_DEMANDES_ADMINS, RoutesAdmins, droitsDuRegistre } from "./admins.js";
import { RoutesBoutique, TABLES_BOUTIQUE_SQL } from "./boutique.js";
import { RoutesStats, TABLES_STATS_SQL, prochainReleve } from "./stats.js";
import { RoutesDerogations } from "./derogations.js";
import { BlocIllisible, ecrireBlocLu, signerBloc, trouverBloc, verifierBloc } from "./blocs.js";
import { GARDE_MESSAGE, RoutesFichiers } from "./fichiers.js";
import { CONFIG_RESTAURATION, RoutesSauvegarde } from "./sauvegarde.js";
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
    estBase64,
    estHex,
    hex,
    iso,
    jourDe,
    mal,
    posteDe,
    reponseErreur,
    reponseJson,
    reponseTexte,
    sha256Hex,
    unhex,
    verifierEd25519,
} from "./outils.js";

const VERSION = "1.48.1"; // 1.48.1 : un article rembourse seul, les demandes de licence dans Commandes et demandes
// 3 (1.44.0) : les admins, le journal, les fichiers, les messages, les versions, les logiciels ; les demandes
// scellees pour plusieurs cles. 4 (1.46.0) : la boutique et les commandes (boutique.js) - des tables en plus ; une
// sauvegarde du schema 3 se restaure encore (ses tables de la boutique restent vides). 5 (1.47.0) : les statistiques et
// le globe (stats.js) - des tables en plus, deux index sur decouvertes ; une sauvegarde du schema 3 ou 4 se restaure
// encore (ses tables de statistiques restent vides).
const SCHEMA = 5;
const JOUR = 86400;
const HEURE = 3600;
const FENETRE_APPEL = 600; // +-600 s
const DUREE_NONCE = 1200; // 20 min
const DUREE_JETON = 900; // 15 min
const BAIL_DEFAUT = 30;

const MAX_LICENCES = 10000;
const MAX_ACTIVATIONS = 100000;
const MAX_DEMANDES_ATTENTE = 1000;
const MAX_SCELLE = 64 * 1024;
// 1.44.0 : une demande scellee aussi pour d'autres cles (les admins qui ont le droit << demandes >>, 32 au plus, et
// une autre cle de l'editeur).
const MAX_SCELLES = 33;
const MAX_LOGICIELS = 200000; // les cles dont le logiciel est garde, au plus
const GARDE_LOGICIEL = 365 * JOUR; // une cle qui n'appelle plus depuis un an est oubliee
const MAX_OCTETS_DEMANDES = 20 * 1024 * 1024; // toutes les demandes gardees (scellees), au plus
const GARDE_DEMANDES = 7 * JOUR; // une demande close (annulee, remplacee, traitee) est effacee apres 7 jours
const MAX_DECOUVERTES = 1000000;
const MAX_POSTES = 100000;
const MAX_FICHIER = 1.5 * 1024 * 1024;
const MAX_FICHIERS = 20000;
const MAX_ARCHIVE = 512 * 1024 * 1024;
// Les PC d'une licence rendus au renouvellement (postes.liste), au plus : la reponse reste bornee (~100 Kio par
// licence), meme pour une grande flottante. Le nombre exact est dans `total` (et `utilises`).
const MAX_LISTE_POSTES = 500;

// Les transferts : un PC qui se libere lui-meme rend sa place tout de suite au plus max(2, postes) fois par
// licence sur 30 jours glissants ; au-dela, sa place reste prise jusqu'a la fin de son bail (son activation,
// gardee hors ligne, vaut jusque-la).
const FENETRE_TRANSFERTS = 30 * JOUR;
const transfertsPermis = (lic) => Math.max(2, lic.postesMax);
// Les lignes d'activation d'une licence (toutes, quel que soit leur etat), au plus : un client ne remplit jamais la
// table des autres.
const maxLignes = (lic) => (lic.type === "floating" ? Math.max(50, 20 * lic.postesMax) : 10 * lic.postesMax + 10);
// Une activation finie est effacee apres la fin de son bail : 7 jours (liberee, retiree), 90 jours (active : un PC
// parti sans se liberer ; revenu plus tard, il s'active de nouveau avec son code).
const GARDE_FINIE = 7 * JOUR;
const GARDE_ACTIVE = 90 * JOUR;
// Les compteurs des appels du jour : ecrits une fois par minute au plus.
const ECRIRE_STATS = 60;

const DROITS = { activation: "activation", flottante: "flottante", heure: "heure", decouverte: "decouverte" };

// Les limites (le plan gratuit, et les abus) : [nom, nombre, duree en secondes]. Les routes publiques (etat, heure,
// certificat, registre) sont limitees par adresse DEVANT le service (index.js, le limiteur de Cloudflare).
const PAR_IP = {
    activer: ["activer", 10, HEURE],
    decouverte: ["decouverte", 10, HEURE],
    demandes: ["demandes", 10, HEURE],
    admins: ["admins_demande", 10, HEURE],
    commande: ["boutique_commande", 10, HEURE], // 1.46.0
};
const PAR_PC = {
    activer: ["activer", 5, HEURE],
    renouveler: ["renouveler", 30, HEURE, "memoire"],
    jeton: ["jeton", 60, HEURE, "memoire"],
    demandes: ["demandes", 5, JOUR],
    admins: ["admins_demande", 5, JOUR],
    commande: ["boutique_commande", 10, JOUR], // 1.46.0 : une commande refusee par Stripe, une annulee : on recommence
    commandes: ["boutique_commandes", 400, HEURE, "memoire"], // 1.46.0 : le Studio regarde sa commande toutes les 10 s
};

// signe : "pc" (n'importe quelle cle : celle du PC), "admin" (une cle de PUBLISHER_KEYS - l'admin+ - ou celle d'un
// admin de la liste, non retire), "plus" (une cle de PUBLISHER_KEYS seulement) ; absent : public.
// droit (1.44.0) : un admin doit avoir l'un de ces droits (l'admin+ les a tous) ; les documents deposes demandent en
// plus le droit de ce qu'ils changent (deposerRegistre, deposerLicence).
// restauration : la route repond aussi pendant une restauration (les autres : 503).
const DROITS_POSTE = ["demandes", "licence_creer", "licence_prolonger", "licence_option", "licence_revoquer", "poste_retirer"];
const ROUTES = {
    "GET /v1/etat": { f: "etat" },
    "GET /v1/heure": { f: "heure" },
    "GET /v1/certificat": { f: "lireCertificat" },
    "GET /v1/registre": { f: "lireRegistre" },
    "GET /v1/admins": { f: "lireAdminsPublic" },
    "GET /v1/version": { f: "lireVersionPublique" },
    "GET /v1/fichiers/manifeste": { f: "lireManifesteFichier" },
    // publique, ou signee par un admin (l'acces du fichier), ou avec un jeton
    "GET /v1/fichiers/morceau": { f: "lireMorceauFichier" },
    "GET /v1/fichiers/lien": { f: "lienFichier" },
    "POST /v1/activer": { f: "activer", signe: "pc", ip: PAR_IP.activer, pc: PAR_PC.activer },
    "POST /v1/renouveler": { f: "renouveler", signe: "pc", pc: PAR_PC.renouveler },
    "POST /v1/liberer": { f: "liberer", signe: "pc" },
    "POST /v1/decouverte": { f: "decouverte", signe: "pc", ip: PAR_IP.decouverte },
    "POST /v1/flottante/jeton": { f: "prendreJeton", signe: "pc", pc: PAR_PC.jeton },
    "POST /v1/flottante/rendre": { f: "rendreJeton", signe: "pc" },
    "POST /v1/demandes": { f: "deposerDemande", signe: "pc", ip: PAR_IP.demandes, pc: PAR_PC.demandes },
    "POST /v1/demandes/annuler": { f: "annulerDemande", signe: "pc" },
    "GET /v1/poste": { f: "lirePoste", signe: "pc" },
    "GET /v1/messages": { f: "lireMessages", signe: "pc" },
    "POST /v1/admins/demande": { f: "deposerDemandeAdmin", signe: "pc", ip: PAR_IP.admins, pc: PAR_PC.admins },
    "GET /v1/admins/demande": { f: "etatDemandeAdmin", signe: "pc" },
    "GET /admin/cle-service": { f: "cleService", signe: "admin" },
    "PUT /admin/certificat": { f: "deposerCertificat", signe: "plus" },
    "PUT /admin/registre": { f: "deposerRegistre", signe: "admin", droit: DROITS_REGISTRE },
    "PUT /admin/revocations": { f: "deposerRevocations", signe: "admin", droit: ["licence_revoquer"] },
    "PUT /admin/licences": { f: "deposerLicence", signe: "admin", droit: DROITS_LICENCE },
    "GET /admin/licences": { f: "listerLicences", signe: "admin" },
    "POST /admin/activations/retirer": { f: "retirerActivation", signe: "admin", droit: ["poste_retirer"] },
    "GET /admin/demandes": { f: "listerDemandes", signe: "admin", droit: ["demandes"] },
    "POST /admin/demandes/traiter": { f: "traiterDemande", signe: "admin", droit: ["demandes"] },
    "PUT /admin/postes": { f: "deposerPoste", signe: "admin", droit: DROITS_POSTE },
    "GET /admin/archive": { f: "listerArchive", signe: "admin" },
    "GET /admin/archive/fichier": { f: "lireFichier", signe: "admin" },
    "POST /admin/archive": { f: "ecrireArchive", signe: "admin" },
    "GET /admin/stats": { f: "stats", signe: "admin" },
    "GET /admin/etat": { f: "etatAdmin", signe: "admin" },
    // 1.44.0 (docs/ADMINS.md, § 6)
    "PUT /admin/admins": { f: "deposerAdmins", signe: "plus" },
    "GET /admin/admins": { f: "listerAdmins", signe: "admin" },
    "GET /admin/admins/demandes": { f: "listerDemandesAdmins", signe: "plus" },
    "POST /admin/admins/demande/refuser": { f: "refuserDemandeAdmin", signe: "plus" },
    "PUT /admin/trousseau": { f: "deposerTrousseau", signe: "plus" },
    "GET /admin/trousseau": { f: "lireTrousseau", signe: "admin" },
    "POST /admin/journal": { f: "ecrireJournal", signe: "admin" },
    "GET /admin/journal": { f: "lireJournal", signe: "plus" },
    "POST /admin/fichiers": { f: "deposerManifeste", signe: "plus" },
    "PUT /admin/fichiers/morceau": { f: "deposerMorceau", signe: "plus" },
    "POST /admin/fichiers/jeton": { f: "tirerJetonFichier", signe: "plus" },
    "PUT /admin/messages": { f: "deposerMessage", signe: "plus" },
    "PUT /admin/versions": { f: "deposerVersion", signe: "plus" },
    "GET /admin/versions": { f: "listerVersions", signe: "plus" },
    "GET /admin/sauvegarde": { f: "sauvegarde", signe: "plus", restauration: true },
    "POST /admin/restauration/debut": { f: "debutRestauration", signe: "plus", restauration: true },
    "POST /admin/restauration/lot": { f: "lotRestauration", signe: "plus", restauration: true },
    "POST /admin/restauration/fin": { f: "finRestauration", signe: "plus", restauration: true },
    // 1.46.0 : la boutique et les commandes (boutique.js, docs/BOUTIQUE.md)
    "GET /v1/boutique": { f: "lireBoutiquePublique" },
    "GET /v1/boutique/merci": { f: "pageMerci" },
    "GET /v1/boutique/annule": { f: "pageAnnule" },
    "POST /v1/boutique/stripe": { f: "webhookStripe" },
    "POST /v1/boutique/commande": { f: "passerCommande", signe: "pc", ip: PAR_IP.commande, pc: PAR_PC.commande },
    "GET /v1/boutique/commandes": { f: "commandesDuPc", signe: "pc", pc: PAR_PC.commandes },
    "POST /v1/boutique/annuler": { f: "annulerCommande", signe: "pc" },
    "PUT /admin/boutique": { f: "deposerBoutique", signe: "plus" },
    "POST /admin/boutique/stripe": { f: "catalogueStripe", signe: "plus" }, // 1.46.1 : les produits et les prix chez Stripe
    "GET /admin/boutique": { f: "lireBoutiqueAdmin", signe: "admin" },
    "GET /admin/commandes": { f: "listerCommandes", signe: "admin", droit: ["demandes"] },
    "POST /admin/commandes/prendre": { f: "prendreCommande", signe: "admin", droit: ["demandes"] },
    "POST /admin/commandes/livree": { f: "commandeLivree", signe: "admin", droit: ["demandes"] },
    "POST /admin/commandes/a-revoir": { f: "commandeARevoir", signe: "admin", droit: ["demandes"] },
    "POST /admin/commandes/ligne": { f: "commandeLigne", signe: "admin", droit: ["demandes"] }, // 1.48.0 : le panier
    "POST /admin/commandes/rembourser": { f: "rembourserCommande", signe: "plus" },
    // 1.47.0 : les statistiques et le globe (stats.js, docs/STATISTIQUES.md)
    "GET /admin/stats/serie": { f: "serieStats", signe: "admin" },
    "GET /admin/stats/mises-a-jour": { f: "misesAJourStats", signe: "admin" },
    "GET /admin/stats/admins": { f: "adminsStats", signe: "admin" },
    "POST /admin/stats/releve": { f: "releveStats", signe: "plus", derogation: true },
    "POST /admin/stats/reevaluer": { f: "reevaluerStats", signe: "admin" }, // 1.48.1 : la coherence (stats.js)
    "GET /admin/globe": { f: "globeStats", signe: "admin" },
    "GET /admin/globe/direct": { f: "globeDirect", signe: "admin" },
    // 1.47.0 : la derogation << Gerer le service >> (derogations.js, docs/ADMINS.md § 11)
    "PUT /admin/derogation": { f: "deposerDerogation", signe: "plus" },
    "POST /admin/derogation/revoquer": { f: "revoquerDerogation", signe: "plus" },
    "GET /admin/derogation": { f: "lireDerogation", signe: "admin" },
    "POST /admin/derogation/utiliser": { f: "utiliserDerogation", signe: "admin" },
};
const CHEMINS = new Set(Object.keys(ROUTES).map((r) => r.slice(r.indexOf(" ") + 1)));

// La station d'un appel (l'empreinte de l'installation de Windows du PC, stationHash : 32 hexadecimaux).
function stationDe(corps) {
    if (!estHex(corps.station, 32))
        throw mal("station", "Le champ « station » doit être l'empreinte de l'installation de Windows de ce PC (32 hexadécimaux minuscules) : mets le programme à jour.");
    return corps.station;
}

const autrePoste = () =>
    new Erreur(
        403,
        "activation",
        "Cette activation est pour un autre poste : la clé de ce PC sert sur une autre installation de Windows (une clé recopiée ?). Rien n'est fait.",
    );

// "JJ/MM/AAAA" (UTC).
function dateFr(t) {
    const d = iso(t);
    return d.slice(8, 10) + "/" + d.slice(5, 7) + "/" + d.slice(0, 4);
}

// Toutes les places d'une licence fixe sont prises ; `revient` : quand une place gardee par un PC libere revient.
function complet(lic, revient) {
    let m = "Cette licence est déjà activée sur " + lic.postesMax + " PC (le maximum).";
    if (revient)
        m +=
            " Une place revient le " +
            dateFr(revient) +
            " : celle d'un PC libéré au-delà des " +
            transfertsPermis(lic) +
            " changements de PC permis en 30 jours, qui la garde jusqu'à la fin de son bail.";
    m += " Libère un PC, ou demande à l'éditeur.";
    return new Erreur(409, "complet", m, revient ? { libre_le: iso(revient) } : null);
}

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
        this.menageJour = null; // le jour du dernier grand menage (lu de config une fois)
        this.ecrites = 0; // les lignes ecrites par l'appel en cours
        this.debitsMem = new Map(); // les limites tenues en memoire (renouveler, jeton) : aucune ecriture
        this.statsMem = null; // les appels du jour, en memoire : {jour, routes, sale, ecrit}
        this.schema = SCHEMA; // (sauvegarde.js)
        this.versionService = VERSION;
        this.enRestauration = false; // 1.44.0 : une restauration en cours (sauvegarde.js) : tout le reste repond 503
        this.dernierLocal = false; // 1.47.0 : le dernier appel visait 127.0.0.1 (l'alarme prend alors l'heure des essais)
        ctx.blockConcurrencyWhile(async () => {
            this.creerTables();
            this.enRestauration = !!this.config(CONFIG_RESTAURATION);
            await this.chargerCle();
            await this.programmerReleve(); // 1.47.0 : le releve du jour (stats.js)
        });
    }

    // Les caches en memoire (apres une restauration : tout est relu des tables).
    oublierCaches() {
        this.certCache = null;
        this.revCache = null;
        this.statsMem = null;
        this.menageJour = null;
        this.dernierMenage = 0;
        this.debitsMem.clear();
        this.refusesMem = null;
        this.stMem = null; // 1.47.0 : les statistiques en memoire (stats.js)
        this.statsDebut = null;
    }

    // 1.47.0 : l'alarme de l'objet (vers 00:05 UTC) - le releve du jour (stats.js), dans la file des appels, puis la
    // suivante.
    async alarm() {
        const suite = this.file.then(async () => {
            if (!this.enRestauration)
                try {
                    this.releverJour(this.maintenant(this.dernierLocal));
                } catch {
                    // le premier appel du jour le prendra
                }
            await this.ctx.storage.setAlarm(prochainReleve(Date.now()));
        });
        this.file = suite.catch(() => {});
        return suite;
    }

    // ================================================================ la base ==
    // Toute requete passe ici : les lignes rendues, et les lignes ecrites comptees (X-XPG-Lignes-Ecrites).
    x(q, ...a) {
        const c = this.sql.exec(q, ...a);
        const r = c.toArray();
        this.ecrites += c.rowsWritten || 0;
        return r;
    }

    creerTables() {
        this.x("CREATE TABLE IF NOT EXISTS config (cle TEXT PRIMARY KEY, valeur TEXT NOT NULL) WITHOUT ROWID");
        const avant = Number(this.config("schema") || "0");
        if (avant === 1) {
            // 1 -> 2 : la place gardee d'un PC libere, les jetons par station (ils durent 15 min : refaits), une ligne
            // de statistiques par jour.
            this.x("ALTER TABLE activations ADD COLUMN place_jusqu INTEGER NOT NULL DEFAULT 0");
            this.x("DROP TABLE IF EXISTS jetons");
            this.x("DROP TABLE IF EXISTS stats");
        }
        // 2 -> 3 (1.44.0) : les octets des scelles d'une demande pour d'autres cles ; les tables neuves sont creees
        // ci-dessous. Rien n'est efface.
        if (avant >= 1 && avant < 3) this.x("ALTER TABLE demandes ADD COLUMN autres INTEGER NOT NULL DEFAULT 0");
        this.x(`
            CREATE TABLE IF NOT EXISTS licences (
                id TEXT PRIMARY KEY, bloc TEXT NOT NULL, emission INTEGER NOT NULL, type TEXT NOT NULL,
                postes_max INTEGER NOT NULL, expire INTEGER NOT NULL, bail_jours INTEGER NOT NULL,
                code_hash TEXT, active INTEGER NOT NULL, maj INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS activations (
                licence TEXT NOT NULL, poste TEXT NOT NULL, nom TEXT NOT NULL, station TEXT NOT NULL, type TEXT NOT NULL,
                numero INTEGER NOT NULL, activee INTEGER NOT NULL, vue INTEGER NOT NULL, emis INTEGER NOT NULL,
                jusqu_a INTEGER NOT NULL, etat TEXT NOT NULL, place_jusqu INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (licence, poste));
            CREATE INDEX IF NOT EXISTS activations_poste ON activations (poste);
            CREATE TABLE IF NOT EXISTS liberations (licence TEXT NOT NULL, t INTEGER NOT NULL, poste TEXT NOT NULL, PRIMARY KEY (licence, t, poste)) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS jetons (
                licence TEXT NOT NULL, poste TEXT NOT NULL, station TEXT NOT NULL, emis INTEGER NOT NULL, jusqu_a INTEGER NOT NULL,
                PRIMARY KEY (licence, poste, station)) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS decouvertes (machine TEXT PRIMARY KEY, debut INTEGER NOT NULL, poste TEXT NOT NULL, vue INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS demandes (
                numero INTEGER PRIMARY KEY, poste TEXT NOT NULL, cle TEXT NOT NULL, scelle TEXT NOT NULL,
                etat TEXT NOT NULL, cree INTEGER NOT NULL, maj INTEGER NOT NULL, autres INTEGER NOT NULL DEFAULT 0);
            CREATE INDEX IF NOT EXISTS demandes_poste ON demandes (poste, etat);
            CREATE INDEX IF NOT EXISTS demandes_etat ON demandes (etat);
            CREATE TABLE IF NOT EXISTS demandes_scelles (numero INTEGER NOT NULL, cle TEXT NOT NULL, scelle TEXT NOT NULL, PRIMARY KEY (numero, cle)) WITHOUT ROWID;
            CREATE INDEX IF NOT EXISTS demandes_scelles_cle ON demandes_scelles (cle, numero);
            CREATE TABLE IF NOT EXISTS postes (poste TEXT PRIMARY KEY, bloc TEXT NOT NULL, numero INTEGER NOT NULL, maj INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS archive (
                chemin TEXT PRIMARY KEY, contenu TEXT NOT NULL, sha256 TEXT NOT NULL, taille INTEGER NOT NULL, version INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS nonces (nonce TEXT PRIMARY KEY, vu INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS debits (cle TEXT NOT NULL, fin INTEGER NOT NULL, n INTEGER NOT NULL, PRIMARY KEY (cle, fin)) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS stats_jour (jour TEXT PRIMARY KEY, routes TEXT NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS admins (
                cle TEXT PRIMARY KEY, id TEXT NOT NULL, nom TEXT NOT NULL, pc TEXT NOT NULL, poste TEXT NOT NULL, depuis INTEGER NOT NULL,
                profil TEXT NOT NULL, droits TEXT NOT NULL, retire INTEGER NOT NULL, gardes TEXT NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS admins_demandes (
                cle TEXT PRIMARY KEY, nom TEXT NOT NULL, courriel TEXT NOT NULL, pc TEXT NOT NULL, poste TEXT NOT NULL, logiciel TEXT NOT NULL,
                etat TEXT NOT NULL, recue INTEGER NOT NULL, maj INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS trousseau (cle TEXT PRIMARY KEY, scelle TEXT NOT NULL, maj INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS journal (
                numero INTEGER PRIMARY KEY, texte TEXT NOT NULL, empreinte TEXT NOT NULL, cle TEXT NOT NULL, nonce TEXT NOT NULL, recu INTEGER NOT NULL);
            CREATE UNIQUE INDEX IF NOT EXISTS journal_nonce ON journal (cle, nonce);
            CREATE INDEX IF NOT EXISTS journal_cle ON journal (cle, numero);
            CREATE TABLE IF NOT EXISTS fichiers (
                id TEXT PRIMARY KEY, manifeste TEXT NOT NULL, nom TEXT NOT NULL, taille INTEGER NOT NULL, sha256 TEXT NOT NULL,
                sorte TEXT NOT NULL, version TEXT NOT NULL, acces TEXT NOT NULL, morceau INTEGER NOT NULL, morceaux INTEGER NOT NULL,
                recus INTEGER NOT NULL, complet INTEGER NOT NULL, cree INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS morceaux (fichier TEXT NOT NULL, n INTEGER NOT NULL, octets BLOB NOT NULL, PRIMARY KEY (fichier, n));
            CREATE TABLE IF NOT EXISTS fichiers_jetons (
                hash TEXT PRIMARY KEY, fichier TEXT NOT NULL, expire INTEGER NOT NULL, une_fois INTEGER NOT NULL, cree INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS messages (numero INTEGER PRIMARY KEY, poste TEXT NOT NULL, bloc TEXT NOT NULL, recu INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS messages_poste ON messages (poste, numero);
            CREATE TABLE IF NOT EXISTS versions (
                version TEXT PRIMARY KEY, x INTEGER NOT NULL, y INTEGER NOT NULL, z INTEGER NOT NULL, bloc TEXT NOT NULL,
                publiee INTEGER NOT NULL, fichiers TEXT NOT NULL, recue INTEGER NOT NULL) WITHOUT ROWID;
            CREATE TABLE IF NOT EXISTS logiciels (cle TEXT PRIMARY KEY, logiciel TEXT NOT NULL, version TEXT NOT NULL, vu INTEGER NOT NULL) WITHOUT ROWID;
        `);
        // 1.46.0 (schema 4) : la boutique et les commandes - des tables neuves, rien d'autre ne change.
        this.x(TABLES_BOUTIQUE_SQL);
        // 1.47.0 (schema 5) : les statistiques et le globe - des tables neuves, deux index sur decouvertes. Les versions
        // deja vues (logiciels) sont recopiees dans versions_vues, datees de leur derniere connexion (une approximation :
        // le debut des releves, stats_debut, le dit).
        this.x(TABLES_STATS_SQL);
        if (avant >= 1 && avant < 5)
            this.x("INSERT INTO versions_vues (cle, version, logiciel, premiere) SELECT cle, version, logiciel, vu FROM logiciels WHERE true ON CONFLICT DO NOTHING");
        if (avant < SCHEMA) {
            // Les compteurs des grandes tables : comptes UNE fois (une base d'avant), puis tenus a chaque ecriture.
            if (avant) {
                const n = (q) => String(this.nombre(q));
                this.ecrireConfig("n_licences", n("SELECT COUNT(*) AS n FROM licences"));
                this.ecrireConfig("n_activations", n("SELECT COUNT(*) AS n FROM activations"));
                this.ecrireConfig("n_decouvertes", n("SELECT COUNT(*) AS n FROM decouvertes"));
                this.ecrireConfig("n_postes", n("SELECT COUNT(*) AS n FROM postes"));
                this.ecrireConfig("n_attente", n("SELECT COUNT(*) AS n FROM demandes WHERE etat = 'attente'"));
                this.ecrireConfig("octets_demandes", n("SELECT COALESCE(SUM(LENGTH(scelle) + autres), 0) AS n FROM demandes"));
            }
            this.ecrireConfig("schema", String(SCHEMA));
        }
    }

    lignes(q, ...a) {
        return this.x(q, ...a);
    }
    ligne(q, ...a) {
        const r = this.x(q, ...a);
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
        if (valeur === null) this.x("DELETE FROM config WHERE cle = ?", cle);
        else this.x("INSERT INTO config (cle, valeur) VALUES (?, ?) ON CONFLICT (cle) DO UPDATE SET valeur = excluded.valeur", cle, valeur);
    }
    // Un compteur croissant (les numeros des activations, des demandes, la version de l'archive).
    suivant(nom) {
        const n = Number(this.config(nom) || "0") + 1;
        this.ecrireConfig(nom, String(n));
        return n;
    }
    // Le nombre de lignes d'une grande table (n_activations...), tenu a chaque ecriture : jamais un COUNT(*).
    compte(nom) {
        return Number(this.config(nom) || "0");
    }
    ajouter(nom, d) {
        if (d) this.ecrireConfig(nom, String(Math.max(0, this.compte(nom) + d)));
    }
    // 1.39.0 : un changement que les Studios doivent voir tout de suite (GET /v1/etat : << changements >>).
    signalerChangement() {
        return this.suivant("changements");
    }
    // 1.40.0 : ce que l'outil de l'editeur, ouvert, doit voir tout de suite (GET /admin/etat, PRIVE) : un PC active,
    // libere par son client, retire au renouvellement (sa place reprise) - << activite >> ; une demande deposee ou
    // annulee par un client - << demandes >>. Jamais au renouvellement d'un bail (trop frequent, rien a montrer).
    signalerActivite() {
        return this.suivant("activite");
    }
    signalerDemande() {
        return this.suivant("demandes_changements");
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

    // 1.44.0 : un document depose (licence, registre, revocations, poste) - par l'admin+ : signe par une cle de
    // l'editeur (comme avant) ; par un admin : signe par SA cle (il depose ce qu'il a signe).
    async blocDepose(ctx, texte, genre, quoi) {
        if (ctx.appelant.role !== "admin") return this.blocEditeur(texte, genre, quoi);
        if (typeof texte !== "string" || !texte) throw mal(quoi);
        let bloc;
        try {
            bloc = trouverBloc(texte, genre);
        } catch (e) {
            throw new Erreur(400, "document", "Le document « " + genre + " » est illisible : " + e.message + ".");
        }
        if (!(await verifierBloc(bloc, ctx.appelant.octets, await cleIdDe(ctx.appelant.octets))))
            throw new Erreur(400, "signature", "Le document « " + genre + " » n'est pas signé par la clé qui l'envoie : un admin dépose ce qu'il a signé.");
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
        let reponse;
        let compte = false;
        try {
            const methode = request.method.toUpperCase();
            const route = ROUTES[methode + " " + url.pathname];
            if (!route) {
                if (CHEMINS.has(url.pathname)) throw new Erreur(405, "methode", "Cette méthode n'est pas permise sur " + url.pathname + ".");
                throw new Erreur(404, "route", "Cette adresse n'existe pas sur le service des licences.");
            }
            // 1.44.0 : une restauration en cours (sauvegarde.js) - rien d'autre ne repond (les tables sont a moitie la).
            if (this.enRestauration && !route.restauration)
                throw new Erreur(503, "restauration", "Le service est en cours de restauration : réessaie dans quelques minutes.", { reessayer_dans: 60 });
            this.dernierLocal = local;
            if (!this.enRestauration) {
                this.debutStats(now); // 1.47.0 : le debut des releves (une fois)
                this.menage(now);
                this.compter(now, methode + " " + url.pathname);
            }
            // Les lignes ecrites par CET appel (sans les menages, ni la statistique : une fois par minute au plus).
            this.ecrites = 0;
            compte = true;
            const corps = new Uint8Array(await request.arrayBuffer());
            const ip = request.headers.get("X-XPG-IP") || "inconnue";
            // LIMITES_ESSAI = "sans" : les limites levees, SEULEMENT sur ce poste (les essais qui enchainent les appels).
            const limites = !(local && this.env.LIMITES_ESSAI === "sans");
            const ctx = { request, url, methode, corps, ip, now, local, appelant: null };
            // L'ordre : la signature (rien n'est ecrit), puis les limites, puis le nonce. Un appel refuse n'ecrit rien
            // (sauf la ligne du journal d'une cle d'admin retire, 1.44.0).
            if (route.signe) ctx.appelant = await this.verifierAppel(ctx, route.signe, route.derogation);
            if (route.droit) this.exigerDroit(ctx, route.droit);
            if (route.ip && limites) this.debit("ip", route.ip, ip, now);
            if (route.pc && limites) this.debit("pc", route.pc, ctx.appelant.cle, now);
            if (ctx.appelant) {
                this.x("INSERT INTO nonces (nonce, vu) VALUES (?, ?)", ctx.appelant.nonce, now);
                this.noterLogiciel(ctx);
                // 1.47.0 : le lieu, << actif maintenant >>, les heures des admins (rien pendant une restauration)
                if (!this.enRestauration) await this.noterAppelStats(ctx);
                // 1.47.0 : une route de l'admin+ ouverte par une derogation : le journal le dit (derogations.js)
                if (ctx.appelant.derogation) await this.noterUsageDerogation(ctx, ctx.appelant.derogation, "route " + methode + " " + url.pathname);
            }
            reponse = await this[route.f](ctx);
        } catch (e) {
            if (e instanceof Erreur) reponse = reponseErreur(e);
            else {
                // Rien du corps ni de l'appel dans le journal : seulement le genre de l'erreur.
                console.error("erreur interne :", e && e.name ? e.name : "?");
                reponse = reponseErreur(new Erreur(500, "interne", "Le service a rencontré une erreur interne. Réessaie plus tard."));
            }
        }
        // Pres de 127.0.0.1 seulement : les lignes ecrites par l'appel (les essais mesurent le plan gratuit).
        if (local && compte) reponse.headers.set("X-XPG-Lignes-Ecrites", String(this.ecrites));
        try {
            this.ecrireStats(now, false);
            if (!this.enRestauration) this.ecrireCompteursStats(now, false); // 1.47.0 : les compteurs du jour (stats.js)
        } catch {
            // la statistique attendra l'appel suivant
        }
        return reponse;
    }

    // Les menages : toutes les 5 minutes au plus (nonces, jetons, limites), et une fois par jour (le reste).
    menage(now) {
        if (now - this.dernierMenage >= 300) {
            this.dernierMenage = now;
            this.x("DELETE FROM nonces WHERE vu < ?", now - DUREE_NONCE - FENETRE_APPEL);
            this.x("DELETE FROM jetons WHERE jusqu_a < ?", now - HEURE);
            this.x("DELETE FROM debits WHERE fin < ?", now);
            for (const [cle, d] of this.debitsMem) if (d.fin < now) this.debitsMem.delete(cle);
            this.menageBoutique(now); // 1.46.0 : une page de paiement finie sans nouvelle de Stripe : expiree
        }
        const jour = jourDe(now);
        if (this.menageJour === null) this.menageJour = this.config("menage_jour") || "";
        if (this.menageJour !== jour) {
            this.grandMenage(now);
            this.menageJour = jour;
            this.ecrireConfig("menage_jour", jour);
            // 1.47.0 : le releve du jour, si l'alarme ne l'a pas encore pris (stats.js)
            try {
                this.releverJour(now);
            } catch {
                // l'alarme, ou POST /admin/stats/releve
            }
        }
    }

    // Une fois par jour : les statistiques de plus de 60 jours, les demandes closes de plus de 7 jours, les activations
    // finies (7 jours apres la fin du bail ; 90 jours pour un PC parti sans se liberer), les liberations de plus de
    // 30 jours. Chaque requete parcourt sa table une fois : une fois par jour.
    grandMenage(now) {
        this.x("DELETE FROM stats_jour WHERE jour < ?", jourDe(now - 60 * JOUR));
        const d = this.ligne(
            "SELECT COUNT(*) AS n, COALESCE(SUM(LENGTH(scelle) + autres), 0) AS o FROM demandes WHERE etat != 'attente' AND maj < ?",
            now - GARDE_DEMANDES,
        );
        if (d && d.n) {
            this.x("DELETE FROM demandes_scelles WHERE numero IN (SELECT numero FROM demandes WHERE etat != 'attente' AND maj < ?)", now - GARDE_DEMANDES);
            this.x("DELETE FROM demandes WHERE etat != 'attente' AND maj < ?", now - GARDE_DEMANDES);
            this.ajouter("octets_demandes", -Number(d.o));
        }
        // 1.44.0 : les demandes d'admin closes (7 jours), les messages (30 jours), les fichiers qu'aucune des 3
        // dernieres versions ne porte (30 jours), les liens finis, les logiciels des cles qui n'appellent plus (un an).
        this.x("DELETE FROM admins_demandes WHERE etat != 'attente' AND maj < ?", now - GARDE_DEMANDES_ADMINS);
        this.x("DELETE FROM messages WHERE recu < ?", now - GARDE_MESSAGE);
        this.purgerFichiers(now);
        const vieux = this.nombre("SELECT COUNT(*) AS n FROM logiciels WHERE vu < ?", now - GARDE_LOGICIEL);
        if (vieux) {
            this.x("DELETE FROM logiciels WHERE vu < ?", now - GARDE_LOGICIEL);
            this.ajouter("n_logiciels", -vieux);
        }
        const finies = "(etat != 'active' AND jusqu_a < ?) OR (etat = 'active' AND jusqu_a < ?)";
        const n = this.nombre("SELECT COUNT(*) AS n FROM activations WHERE " + finies, now - GARDE_FINIE, now - GARDE_ACTIVE);
        if (n) {
            this.x("DELETE FROM activations WHERE " + finies, now - GARDE_FINIE, now - GARDE_ACTIVE);
            this.ajouter("n_activations", -n);
        }
        this.x("DELETE FROM liberations WHERE t < ?", now - FENETRE_TRANSFERTS);
        this.grandMenageBoutique(now); // 1.46.0
        this.grandMenageStats(now); // 1.47.0 : les faits, releves, compteurs, versions vues (2 ans), les lieux (un an)
    }

    // Les appels du jour : comptes en memoire, ecrits (une ligne) une fois par minute au plus. Un objet arrete entre
    // deux ecritures perd ce qu'il n'a pas ecrit : c'est une statistique, pas une facture.
    compter(now, route) {
        const jour = jourDe(now);
        if (!this.statsMem || this.statsMem.jour !== jour) {
            if (this.statsMem) this.ecrireStats(now, true);
            const r = this.ligne("SELECT routes FROM stats_jour WHERE jour = ?", jour);
            let routes = {};
            try {
                routes = r ? JSON.parse(r.routes) : {};
            } catch {
                routes = {};
            }
            this.statsMem = { jour, routes, sale: false, ecrit: 0 };
        }
        this.statsMem.routes[route] = (this.statsMem.routes[route] || 0) + 1;
        this.statsMem.sale = true;
    }

    ecrireStats(now, maintenant) {
        const m = this.statsMem;
        if (!m || !m.sale || (!maintenant && now - m.ecrit < ECRIRE_STATS)) return;
        this.x(
            "INSERT INTO stats_jour (jour, routes) VALUES (?, ?) ON CONFLICT (jour) DO UPDATE SET routes = excluded.routes",
            m.jour,
            JSON.stringify(m.routes),
        );
        m.sale = false;
        m.ecrit = now;
    }

    // Une limite : [nom, nombre, duree, "memoire"?]. En memoire : aucune ecriture (renouveler, jeton : les appels
    // frequents) ; l'objet n'est arrete qu'apres un moment sans appel, la limite tient donc tant qu'on la presse.
    debit(portee, [nom, limite, duree, memoire], qui, now) {
        const fin = (Math.floor(now / duree) + 1) * duree;
        const cle = portee + ":" + nom + ":" + qui;
        const trop = () => {
            const attente = Math.max(1, fin - now);
            return new Erreur(429, "trop", "Trop d'appels « " + nom + " » : réessaie dans " + dureeTexte(attente) + ".", { reessayer_dans: attente });
        };
        if (memoire) {
            const m = this.debitsMem.get(cle);
            const n = m && m.fin === fin ? m.n : 0;
            if (n >= limite) throw trop();
            this.debitsMem.set(cle, { fin, n: n + 1 });
            return;
        }
        const r = this.ligne("SELECT n FROM debits WHERE cle = ? AND fin = ?", cle, fin);
        if (r && r.n >= limite) throw trop();
        this.x("INSERT INTO debits (cle, fin, n) VALUES (?, ?, 1) ON CONFLICT (cle, fin) DO UPDATE SET n = n + 1", cle, fin);
    }

    // L'appel signe : X-XPG-Cle, X-XPG-Date, X-XPG-Nonce, X-XPG-Signature (Service.hpp, verifyRequest). Rien n'est
    // ecrit ici (le nonce l'est par traiter(), une fois les limites passees), sauf la ligne du journal d'une cle
    // d'admin retire (1.44.0).
    // `signe` : "pc" (toute cle), "admin" (la cle de l'editeur - role << admin+ >>, tous les droits - ou celle d'un
    // admin de la liste, non retire - role << admin >>, ses droits), "plus" (la cle de l'editeur seulement).
    // Rend {cle, octets, nonce, poste, role ("pc", "admin+", "admin"), droits, admin (sa ligne de la liste)}.
    // 1.47.0 : `derogation` - une route << plus >> qui s'ouvre aussi a l'admin qui a une derogation << Gerer le service >>
    // en cours (derogations.js) ; son usage est ecrit au journal.
    async verifierAppel(ctx, signe, derogation) {
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
        let role = "pc";
        let admin = null;
        if (signe === "admin" || signe === "plus") {
            if ((await this.clesEditeur()).some((k) => k.hex === cle)) role = "admin+";
            else {
                admin = this.admin(cle);
                if (!admin)
                    throw new Erreur(
                        403,
                        "editeur",
                        signe === "plus"
                            ? "Cette clé n'est pas une clé de l'éditeur de ce service (PUBLISHER_KEYS)."
                            : "Cette clé n'est ni une clé de l'éditeur de ce service (PUBLISHER_KEYS), ni celle d'un admin de la liste.",
                    );
                role = "admin";
            }
        }
        const t = Number(date);
        if (Math.abs(t - ctx.now) > FENETRE_APPEL)
            throw new Erreur(401, "heure", "L'heure de l'appel est trop loin de celle du service : lis l'heure du service (GET /v1/etat) et signe avec elle.", { heure: ctx.now });
        const chemin = ctx.url.pathname + ctx.url.search;
        const message = "XPG-SERVICE-1\n" + ctx.methode + "\n" + chemin + "\n" + date + "\n" + nonce + "\n" + (await sha256Hex(ctx.corps));
        const octets = unhex(cle);
        if (!(await verifierEd25519(octets, enc.encode(message), sig))) throw new Erreur(401, "signature", "La signature de l'appel est fausse.");
        if (this.ligne("SELECT 1 AS x FROM nonces WHERE nonce = ?", nonce)) throw new Erreur(401, "rejeu", "Cet appel a déjà été reçu (nonce déjà vu) : il est refusé.");
        const appelant = { cle, octets, nonce, poste: await posteDe(octets), role, droits: role === "admin+" ? DROITS_ADMINS.slice() : [], admin: null };
        if (admin) {
            // Un admin retire : refuse, et le journal le dit (la signature est la sienne : verifiee ci-dessus).
            if (admin.retire) {
                await this.signatureRefusee(ctx, admin);
                throw new Erreur(403, "retire", "Cette clé est celle d'un admin retiré : elle ne peut plus rien faire sur ce service.");
            }
            appelant.droits = JSON.parse(admin.droits);
            appelant.admin = admin;
            if (signe === "plus") {
                const d = derogation ? this.derogationDe(cle, ctx.now) : null;
                if (!d) {
                    // (le code reste << droit >>, comme avant la 1.47.0 ; la phrase dit la derogation)
                    if (derogation) throw new Erreur(403, "droit", this.refusDerogation(cle, ctx.now).message);
                    throw new Erreur(403, "droit", "Cette action est réservée à l'admin+ (la clé de l'éditeur).");
                }
                appelant.derogation = d; // l'usage est ecrit au journal une fois l'appel accepte (traiter)
            }
        }
        return appelant;
    }

    // Un admin doit avoir l'un de ces droits (`tous` : chacun) ; l'admin+ les a tous. Leve 403 << droit >>.
    exigerDroit(ctx, droits, tous) {
        const a = ctx.appelant;
        if (!a || a.role !== "admin") return;
        const ok = tous ? droits.every((d) => a.droits.includes(d)) : droits.some((d) => a.droits.includes(d));
        if (!ok)
            throw new Erreur(
                403,
                "droit",
                "Tes droits d'admin ne permettent pas cette action (il faut " +
                    (droits.length > 1 ? (tous ? "les droits " : "l'un des droits ") : "le droit ") +
                    droits.map((d) => "« " + d + " »").join(", ") +
                    ").",
                { droits },
            );
    }

    // 1.44.0 : le logiciel d'un appel signe (X-XPG-Logiciel : "IHM-Studio/1.44.0"), garde par cle. Une ecriture
    // seulement s'il change, ou une fois par heure (l'heure vue) ; un en-tete illisible est ignore.
    noterLogiciel(ctx) {
        const v = ctx.request.headers.get("X-XPG-Logiciel");
        if (!v || !ctx.appelant) return;
        const m = /^([A-Za-z-]{1,30})\/(\d{1,6}\.\d{1,6}\.\d{1,6})$/.exec(v);
        if (!m) return;
        const cle = ctx.appelant.cle;
        const r = this.ligne("SELECT logiciel, version, vu FROM logiciels WHERE cle = ?", cle);
        if (!r) {
            if (this.compte("n_logiciels") >= MAX_LOGICIELS) return;
            this.x("INSERT INTO logiciels (cle, logiciel, version, vu) VALUES (?, ?, ?, ?)", cle, m[1], m[2], ctx.now);
            this.ajouter("n_logiciels", 1);
            this.noterVersionVue(cle, m[1], m[2], ctx.now); // 1.47.0 : la premiere fois avec cette version (stats.js)
        } else if (r.logiciel !== m[1] || r.version !== m[2] || r.vu < ctx.now - HEURE) {
            this.x("UPDATE logiciels SET logiciel = ?, version = ?, vu = ? WHERE cle = ?", m[1], m[2], ctx.now, cle);
            if (r.version !== m[2] || r.logiciel !== m[1]) this.noterVersionVue(cle, m[1], m[2], ctx.now);
        }
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

    // Les PC qui tiennent une place d'une licence : les activations actives dans leur bail, et les PC liberes au-dela
    // des transferts permis, jusqu'a la fin de leur bail (place_jusqu) ; jamais un PC revoque. Une flottante n'a pas de
    // place a garder : ses PC actifs seulement. [{r, fin}] : `fin` = la fin de la place (le bail ; place_jusqu pour un
    // PC libere). places() les compte, postesDe() les montre : le meme ensemble, jamais deux regles.
    occupants(lic, now) {
        const out = [];
        for (const r of this.lignes(
            "SELECT poste, nom, station, activee, vue, jusqu_a, etat, place_jusqu FROM activations WHERE licence = ? AND (etat = 'active' OR (etat = 'liberee' AND place_jusqu > ?))",
            lic.id,
            now,
        )) {
            if (r.etat !== "active" && lic.type === "floating") continue;
            const fin = r.etat === "active" ? r.jusqu_a : r.place_jusqu;
            if (fin <= now || this.revoquee(lic, r.poste)) continue;
            out.push({ r, fin });
        }
        return out;
    }

    // Les places prises d'une licence fixe : ses occupants. `sauf` {cle, station} : le PC qui demande reprend sa
    // propre place (la meme cle ET la meme installation de Windows).
    // {n, revient} : `revient` = la premiere fin d'une place gardee par un PC libere (0 : aucune).
    places(lic, now, sauf) {
        let n = 0;
        let revient = 0;
        for (const { r, fin } of this.occupants(lic, now)) {
            if (sauf && r.poste === sauf.cle && (!r.station || !sauf.station || r.station === sauf.station)) continue;
            n++;
            if (r.etat === "liberee" && (!revient || fin < revient)) revient = fin;
        }
        return { n, revient };
    }

    // 1.38.0 : les PC d'une licence, vus par l'un d'eux (`cle`) au renouvellement - << Mes appareils >> dans le Studio.
    // La liste : ses occupants (ceux que places() compte), CE PC d'abord (il y est toujours : il vient d'etre renouvele,
    // et la borne ne le coupe jamais), puis les autres, les plus recemment actives d'abord ; MAX_LISTE_POSTES au plus
    // (`total` : combien sans la borne). D'un autre PC, rien que son nom, sa cle et ses dates : ni sa station, ni son
    // numero. `utilises` : ce qui limite la licence, tel que le service l'applique -
    //   fixe      ses places prises (places().n : un PC libere qui garde sa place compte, jusqu'a la fin de son bail) ;
    //   flottante ses jetons en cours (prendreJeton compte les jetons, pas les PC actives : une flottante en active
    //             autant qu'elle veut) ; `jeton` dit alors, pour chaque PC, s'il en tient un.
    // Rien n'est ecrit ici (des lectures : le renouvellement garde ses 3 lignes).
    postesDe(lic, cle, now) {
        const occ = this.occupants(lic, now);
        occ.sort((a, b) => (b.r.poste === cle) - (a.r.poste === cle) || b.r.activee - a.r.activee || (a.r.poste < b.r.poste ? -1 : a.r.poste > b.r.poste ? 1 : 0));
        let utilises = occ.length;
        let tenus = null;
        if (lic.type === "floating") {
            const jetons = this.lignes("SELECT poste FROM jetons WHERE licence = ? AND jusqu_a > ?", lic.id, now);
            utilises = jetons.length;
            tenus = new Set(jetons.map((j) => j.poste));
        }
        const liste = occ.slice(0, MAX_LISTE_POSTES).map(({ r, fin }) => {
            const p = { nom: r.nom, cle: r.poste, activee: iso(r.activee), vue: iso(r.vue), jusqu_a: iso(fin), etat: r.etat, ce_pc: r.poste === cle };
            if (tenus) p.jeton = tenus.has(r.poste);
            return p;
        });
        return { max: lic.postesMax, type: lic.type, utilises, total: occ.length, liste };
    }

    // Signer l'activation de ce PC (un numero neuf, le bail de la licence a partir de maintenant). La fin de la licence
    // (et sa grace) est appliquee par le PC lui-meme, avec ses dates signees par l'editeur ; le service, lui, ne
    // renouvelle plus une licence finie (renouveler : motif expiree).
    async signerActivation(lic, poste, station, now) {
        const numero = this.suivant("numero_activation");
        const jusqu = now + lic.bail * JOUR;
        const type = lic.type === "floating" ? "flottante" : "fixe";
        this.x(
            "UPDATE activations SET numero = ?, emis = ?, jusqu_a = ?, vue = ?, type = ?, station = ? WHERE licence = ? AND poste = ?",
            numero,
            now,
            jusqu,
            now,
            type,
            station,
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
            // 1.39.0 : ce qui change chez les clients - la liste de revocation (son numero) et un compteur de
            // changements (une revocation, une licence deposee ou desactivee, un PC retire, l'etat d'un poste) : un
            // Studio qui le voit bouger se resynchronise tout de suite (la licence perdue en direct).
            revocations: Number(this.config("revocations_numero") || "0"),
            changements: Number(this.config("changements") || "0"),
            // 1.44.0 : le numero de la liste des admins (GET /v1/admins), et la derniere version publiee des logiciels
            // (GET /v1/version ; "" : aucune). `version`, lui, reste celle du service.
            admins: Number(this.config("admins_numero") || "0"),
            version_publiee: this.config("version_derniere") || "",
            // 1.46.0 : la boutique en ligne est ouverte (des articles s'achetent : GET /v1/boutique).
            boutique: this.boutiqueOuverte(),
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
        // L'activation est liee a l'installation de Windows de ce PC (station) : une demande sans elle est refusee.
        if (!demande.station)
            throw new Erreur(
                400,
                "demande",
                "La demande d'activation est refusée : elle ne porte pas l'empreinte de l'installation de Windows de ce PC (« station »). Mets le programme à jour, puis réessaie.",
            );
        const cert = await this.exigerCertificat(ctx.now, DROITS.activation);
        const norm = normaliserCode(corps.code);
        // Toujours le meme travail, que le code soit lisible ou non.
        const hash = await sha256Hex("xpg-code/1|" + (norm || "?"));
        const lic = norm ? this.licenceParCode(hash) : null;
        if (!lic || !lic.active) throw new Erreur(403, "code", "Ce code d'activation n'est pas valable (inconnu ou désactivé). Vérifie-le, ou demande-le à l'éditeur.");
        if (this.revoquee(lic, cle)) throw new Erreur(410, "revoquee", "Cette licence est révoquée : elle ne peut plus être activée.");
        if (lic.expire && ctx.now >= lic.expire) throw new Erreur(410, "expiree", "Cette licence a expiré le " + dateFr(lic.expire) + ".");
        const avant = this.ligne("SELECT * FROM activations WHERE licence = ? AND poste = ?", lic.id, cle);
        const memePoste = !!avant && (!avant.station || avant.station === demande.station);
        // La cle de ce PC, active pour cette licence sur une AUTRE installation de Windows, dans son bail : une cle
        // recopiee. Elle ne prend pas la place de l'autre.
        if (avant && !memePoste && avant.etat === "active" && avant.jusqu_a > ctx.now) throw autrePoste();
        const tientSaPlace = memePoste && avant.etat === "active" && avant.jusqu_a > ctx.now;
        if (lic.type !== "floating" && !tientSaPlace) {
            const p = this.places(lic, ctx.now, { cle, station: demande.station });
            if (p.n >= lic.postesMax) throw complet(lic, p.revient);
        }
        if (!avant) {
            // Les lignes de CETTE licence (son maximum ne gene aucune autre licence), puis tout le service.
            const max = maxLignes(lic);
            if (this.nombre("SELECT COUNT(*) AS n FROM activations WHERE licence = ?", lic.id) >= max)
                throw new Erreur(
                    409,
                    "limite",
                    "Cette licence a déjà " +
                        max +
                        " activations enregistrées (son maximum) : demande à l'éditeur de retirer les PC qui ne servent plus. Une activation finie s'efface d'elle-même une semaine après la fin de son bail.",
                );
            if (this.compte("n_activations") >= MAX_ACTIVATIONS) throw new Erreur(507, "limite", "Le service a atteint son nombre maximal d'activations.");
        }
        const nom = nomPropre(corps.nom !== undefined ? corps.nom : demande.nom);
        const activee = tientSaPlace ? avant.activee : ctx.now;
        this.x(
            `INSERT INTO activations (licence, poste, nom, station, type, numero, activee, vue, emis, jusqu_a, etat, place_jusqu)
             VALUES (?, ?, ?, ?, ?, 0, ?, ?, ?, ?, 'active', 0)
             ON CONFLICT (licence, poste) DO UPDATE SET nom = excluded.nom, station = excluded.station, activee = excluded.activee,
                 vue = excluded.vue, etat = 'active', place_jusqu = 0`,
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
        if (!avant) this.ajouter("n_activations", 1);
        if (!avant || avant.etat !== "active") {
            this.signalerActivite();
            // 1.47.0 : le fait (stats.js) - un PC active (neuf, ou repris apres une liberation) ; sa Decouverte convertie
            this.faitPc(ctx.now, "pc_active", lic, cle);
            this.faitConversion(ctx.now, lic, cle);
        }
        const a = await this.signerActivation(lic, cle, demande.station, ctx.now);
        const message =
            lic.type === "floating"
                ? "La licence flottante " + lic.id + " est activée sur ce PC (bail jusqu'au " + dateFr(a.jusqu) + ")."
                : "La licence " + lic.id + " est activée sur ce PC jusqu'au " + dateFr(a.jusqu) + " ; elle se renouvelle à chaque connexion.";
        return reponseJson(200, { licence: lic.bloc, activation: a.bloc, certificat: cert.bloc, message });
    }

    // Le renouvellement : le bail de chaque activation de ce PC (cette cle, cette installation de Windows). Les autres
    // sont dans `retirees`, avec leur motif (`motifs`) : liberee, retiree, revoquee, desactivee, expiree, autre_poste
    // (la cle de ce PC sert ailleurs : jamais renouvelee ici), inconnue. La liste de revocation de l'editeur ne sort
    // jamais : une revocation se lit ici (revoquee), ou dans l'etat << poste >> scelle pour un PC lie par l'editeur.
    // 1.38.0 : `postes` {"<id>": {max, type, utilises, total, liste}}, pour chaque licence renouvelee ici, et elles
    // seules (jamais une licence ou ce PC n'est pas actif, ni une retiree) : ses PC et ses places prises (postesDe).
    // Un Studio d'avant ne lit pas ce champ.
    async renouveler(ctx) {
        const corps = this.corpsJson(ctx);
        const station = stationDe(corps);
        const { cle, poste } = ctx.appelant;
        const cert = await this.exigerCertificat(ctx.now, DROITS.activation);
        const activations = [];
        const retirees = [];
        const motifs = {};
        const postes = {};
        const retirer = (id, motif) => {
            retirees.push(id);
            motifs[id] = motif;
        };
        for (const r of this.lignes("SELECT * FROM activations WHERE poste = ? ORDER BY licence", cle)) {
            // Une activation faite par une autre installation de Windows : rien n'en est dit de plus, rien n'est change.
            if (r.station && r.station !== station) {
                retirer(r.licence, "autre_poste");
                continue;
            }
            const lic = this.licence(r.licence);
            if (!lic) {
                retirer(r.licence, "inconnue");
                continue;
            }
            if (r.etat !== "active") {
                retirer(r.licence, r.etat === "liberee" ? "liberee" : "retiree");
                continue;
            }
            if (this.revoquee(lic, cle)) {
                this.x("DELETE FROM jetons WHERE licence = ? AND poste = ?", r.licence, cle);
                retirer(r.licence, "revoquee");
                continue;
            }
            if (!lic.active) {
                retirer(r.licence, "desactivee");
                continue;
            }
            // Une licence finie n'est plus renouvelee (une emission plus recente, deposee par l'editeur, la prolonge).
            if (lic.expire && ctx.now >= lic.expire) {
                retirer(r.licence, "expiree");
                continue;
            }
            // Un bail echu dont la place a ete reprise par un autre PC : ce PC est retire.
            if (r.jusqu_a <= ctx.now && lic.type !== "floating" && this.places(lic, ctx.now, { cle, station }).n >= lic.postesMax) {
                this.x("UPDATE activations SET etat = 'retiree', vue = ? WHERE licence = ? AND poste = ?", ctx.now, r.licence, cle);
                this.signalerActivite();
                this.faitPc(ctx.now, "pc_retire", lic, cle, ""); // 1.47.0 : sa place reprise par un autre PC
                retirer(r.licence, "retiree");
                continue;
            }
            const a = await this.signerActivation(lic, cle, station, ctx.now);
            activations.push({ licence: lic.bloc, activation: a.bloc });
            // apres signerActivation : le bail neuf de ce PC est celui que la liste montre
            postes[lic.id] = this.postesDe(lic, cle, ctx.now);
        }
        const etatPoste = this.ligne("SELECT bloc FROM postes WHERE poste = ?", poste);
        return reponseJson(200, { activations, retirees, motifs, certificat: cert.bloc, poste: etatPoste ? etatPoste.bloc : null, postes });
    }

    // Liberer ce PC. Sa place revient tout de suite dans les transferts permis (max(2, postes) en 30 jours glissants,
    // par licence) ; au-dela, elle reste prise jusqu'a la fin de son bail (son activation, gardee hors ligne, vaut
    // jusque-la). Une licence flottante n'a pas de place a garder (le jeton est rendu).
    async liberer(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence");
        const station = corps.station === undefined ? "" : stationDe(corps);
        const { cle } = ctx.appelant;
        const r = this.ligne("SELECT * FROM activations WHERE licence = ? AND poste = ?", corps.licence, cle);
        if (r && station && r.station && r.station !== station) throw autrePoste();
        let garde = 0;
        let faits = 0;
        let permis = 0;
        if (r && r.etat === "active") {
            const lic = this.licence(r.licence);
            if (lic && lic.type !== "floating" && lic.active && r.jusqu_a > ctx.now && !this.revoquee(lic, cle)) {
                permis = transfertsPermis(lic);
                faits = this.nombre("SELECT COUNT(*) AS n FROM liberations WHERE licence = ? AND t > ?", lic.id, ctx.now - FENETRE_TRANSFERTS);
                if (faits < permis) {
                    this.x("INSERT INTO liberations (licence, t, poste) VALUES (?, ?, ?) ON CONFLICT DO NOTHING", lic.id, ctx.now, cle);
                    faits++;
                } else {
                    garde = r.jusqu_a;
                }
            }
            this.x("UPDATE activations SET etat = 'liberee', vue = ?, place_jusqu = ? WHERE licence = ? AND poste = ?", ctx.now, garde, corps.licence, cle);
            this.signalerActivite();
            this.faitPc(ctx.now, "pc_libere", this.licence(corps.licence), cle); // 1.47.0 (stats.js)
        } else if (r && r.etat === "liberee" && r.place_jusqu > ctx.now) {
            garde = r.place_jusqu;
        }
        this.x("DELETE FROM jetons WHERE licence = ? AND poste = ?", corps.licence, cle);
        const rep = garde ? { place: "gardee", jusqu_a: iso(garde) } : { place: "rendue" };
        if (permis) {
            rep.transferts = faits;
            rep.transferts_permis = permis;
        }
        rep.message = garde
            ? "Ce PC est libéré, mais la licence " +
              corps.licence +
              " a déjà changé de PC " +
              (permis || "plusieurs") +
              " fois en 30 jours (le maximum) : sa place reste prise jusqu'à la fin de son bail, le " +
              dateFr(garde) +
              "."
            : "Ce PC est libéré : sa place est rendue, un autre PC peut la prendre.";
        return reponseJson(200, rep);
    }

    async decouverte(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.machine, 32)) throw mal("machine", "Le champ « machine » doit être fait de 32 hexadécimaux minuscules.");
        await this.exigerCertificat(ctx.now, DROITS.decouverte);
        let r = this.ligne("SELECT debut FROM decouvertes WHERE machine = ?", corps.machine);
        if (!r) {
            if (this.compte("n_decouvertes") >= MAX_DECOUVERTES) throw new Erreur(507, "limite", "Le service a atteint son nombre maximal de Découvertes.");
            this.x("INSERT INTO decouvertes (machine, debut, poste, vue) VALUES (?, ?, ?, ?)", corps.machine, ctx.now, ctx.appelant.cle, ctx.now);
            this.ajouter("n_decouvertes", 1);
            r = { debut: ctx.now };
        } else {
            // vue : une fois par jour au plus (une ecriture de moins pour les declarations repetees)
            this.x("UPDATE decouvertes SET vue = ? WHERE machine = ? AND vue < ?", ctx.now, corps.machine, ctx.now - JOUR);
        }
        let jours = Number.parseInt(String(this.env.DECOUVERTE_JOURS || "30"), 10);
        if (!Number.isFinite(jours) || jours < 1 || jours > 366) jours = 30;
        const bloc = await this.signer("decouverte", { version: 1, poste: ctx.appelant.cle, machine: corps.machine, debut: iso(r.debut), jours });
        return reponseTexte(200, bloc);
    }

    // Le jeton d'une licence flottante : pour (licence, cle, station) - la station doit etre celle de l'activation.
    async prendreJeton(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence");
        const station = stationDe(corps);
        const { cle } = ctx.appelant;
        const lic = this.licence(corps.licence);
        const act = lic ? this.ligne("SELECT * FROM activations WHERE licence = ? AND poste = ?", lic.id, cle) : null;
        if (!lic || !act || act.etat !== "active" || lic.type !== "floating" || !lic.active)
            throw new Erreur(403, "activation", "Ce PC n'a pas d'activation flottante pour cette licence.");
        if (act.station && act.station !== station) throw autrePoste();
        if (this.revoquee(lic, cle)) throw new Erreur(410, "revoquee", "Cette licence est révoquée.");
        if (lic.expire && ctx.now >= lic.expire) throw new Erreur(410, "expiree", "Cette licence a expiré le " + dateFr(lic.expire) + ".");
        if (act.jusqu_a <= ctx.now) throw new Erreur(403, "activation", "Le bail de l'activation de ce PC est échu : renouvelle-la d'abord.");
        await this.exigerCertificat(ctx.now, DROITS.flottante);
        const autres = this.lignes(
            "SELECT jusqu_a FROM jetons WHERE licence = ? AND NOT (poste = ? AND station = ?) AND jusqu_a > ?",
            lic.id,
            cle,
            station,
            ctx.now,
        );
        if (autres.length >= lic.postesMax) {
            const libre = Math.max(1, Math.min(...autres.map((x) => x.jusqu_a)) - ctx.now);
            throw new Erreur(409, "complet", "Les " + lic.postesMax + " jetons de cette licence sont pris : un se libère dans " + dureeTexte(libre) + ".", {
                libre_dans: libre,
            });
        }
        const jusqu = ctx.now + DUREE_JETON;
        this.x(
            `INSERT INTO jetons (licence, poste, station, emis, jusqu_a) VALUES (?, ?, ?, ?, ?)
             ON CONFLICT (licence, poste, station) DO UPDATE SET emis = excluded.emis, jusqu_a = excluded.jusqu_a`,
            lic.id,
            cle,
            station,
            ctx.now,
            jusqu,
        );
        this.noterJetons(ctx.now, lic.id, autres.length + 1); // 1.47.0 : le pic des jetons (en memoire : stats.js)
        // vu : une fois par heure au plus (le jeton est repris toutes les 5 minutes)
        this.x("UPDATE activations SET vue = ? WHERE licence = ? AND poste = ? AND vue < ?", ctx.now, lic.id, cle, ctx.now - HEURE);
        const jeton = await this.signer("jeton", { version: 1, licence: lic.id, poste: cle, emis: iso(ctx.now), jusqu_a: iso(jusqu) });
        return reponseJson(200, { jeton });
    }

    async rendreJeton(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estIdLicence(corps.licence)) throw mal("licence");
        const station = stationDe(corps);
        const act = this.ligne("SELECT station FROM activations WHERE licence = ? AND poste = ?", corps.licence, ctx.appelant.cle);
        if (act && act.station && act.station !== station) throw autrePoste();
        this.x("DELETE FROM jetons WHERE licence = ? AND poste = ? AND station = ?", corps.licence, ctx.appelant.cle, station);
        return reponseJson(200, {});
    }

    // 1.44.0 : une cle (son identifiant, xpg-…) pour qui une demande peut etre scellee : une cle de l'editeur, ou un
    // admin de la liste (non retire) qui a le droit << demandes >>.
    async clePourDemandes(id) {
        if ((await this.clesEditeur()).some((k) => k.id === id)) return true;
        const a = this.ligne("SELECT droits FROM admins WHERE id = ? AND retire = 0", id);
        return !!a && JSON.parse(a.droits).includes("demandes");
    }

    // `cle`/`scelle` : pour une cle de l'editeur (comme avant). 1.44.0 : `scelles` [{cle, scelle}], la meme demande
    // scellee pour d'autres cles (les admins qui ont le droit << demandes >>, une autre cle de l'editeur) ; un Studio
    // d'avant n'envoie que cle/scelle.
    async deposerDemande(ctx) {
        const corps = this.corpsJson(ctx);
        if (typeof corps.cle !== "string" || !/^xpg-[0-9a-f]{16}$/.test(corps.cle)) throw mal("cle");
        if (!(await this.clesEditeur()).some((k) => k.id === corps.cle))
            throw new Erreur(400, "cle", "Cette demande est scellée pour une clé que l'éditeur n'emploie pas sur ce service.");
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
                    throw new Erreur(400, "cle", "Cette demande est scellée pour une clé qui n'est ni celle de l'éditeur, ni celle d'un admin qui traite les demandes.");
                autres.push(s);
            }
        }
        const octetsAutres = autres.reduce((n, s) => n + s.scelle.length, 0);
        const poste = ctx.appelant.poste;
        // Les demandes en attente et leur taille : des compteurs (config), jamais un COUNT(*) de la table.
        const miennes = this.nombre("SELECT COUNT(*) AS n FROM demandes WHERE poste = ? AND etat = 'attente'", poste);
        if (this.compte("n_attente") - miennes >= MAX_DEMANDES_ATTENTE) throw new Erreur(507, "limite", "Trop de demandes attendent l'éditeur : réessaie plus tard.");
        if (this.compte("octets_demandes") + corps.scelle.length + octetsAutres > MAX_OCTETS_DEMANDES)
            throw new Erreur(
                507,
                "limite",
                "Le service garde déjà trop de demandes (leur taille maximale est atteinte) : réessaie dans quelques jours, ou écris à l'éditeur.",
            );
        if (miennes) this.x("UPDATE demandes SET etat = 'remplacee', maj = ? WHERE poste = ? AND etat = 'attente'", ctx.now, poste);
        const numero = this.suivant("numero_demande");
        this.x(
            "INSERT INTO demandes (numero, poste, cle, scelle, etat, cree, maj, autres) VALUES (?, ?, ?, ?, 'attente', ?, ?, ?)",
            numero,
            poste,
            corps.cle,
            corps.scelle,
            ctx.now,
            ctx.now,
            octetsAutres,
        );
        for (const s of autres) this.x("INSERT INTO demandes_scelles (numero, cle, scelle) VALUES (?, ?, ?)", numero, s.cle, s.scelle);
        // 1.47.0 : le fait (stats.js) - le client du PC (d'apres ses licences), s'il en a un
        const cl = this.clientDuPc(ctx.appelant.cle);
        this.fait(ctx.now, "demande_recue", { licence: cl.licence, offre: cl.offre, client: cl.client, poste, valeur: numero });
        this.ajouter("n_attente", 1 - miennes);
        this.ajouter("octets_demandes", corps.scelle.length + octetsAutres);
        this.signalerDemande();
        return reponseJson(201, { numero });
    }

    async annulerDemande(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entierPositif(corps.numero) || corps.numero < 1) throw mal("numero");
        const r = this.ligne("SELECT etat FROM demandes WHERE numero = ? AND poste = ?", corps.numero, ctx.appelant.poste);
        if (!r) throw new Erreur(404, "demande", "Cette demande n'existe pas, ou n'est pas celle de ce PC.");
        if (r.etat === "attente") {
            this.x("UPDATE demandes SET etat = 'annulee', maj = ? WHERE numero = ?", ctx.now, corps.numero);
            this.ajouter("n_attente", -1);
            this.signalerDemande();
        } else if (r.etat !== "annulee")
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
        const bloc = await this.blocDepose(ctx, corps.registre, "registre", "registre");
        let r;
        try {
            r = lireRegistre(bloc.json);
        } catch (e) {
            throw new Erreur(400, "document", "Le registre est illisible : " + e.message + ".");
        }
        // 1.44.0 : un admin a le droit de chaque partie qui change (editions : << offres >>, options, annonces ; une
        // autre partie : << offres >>) ; rien ne change : l'un des trois.
        if (ctx.appelant.role === "admin") {
            let ancien = null;
            const enPlace = this.config("registre");
            if (enPlace)
                try {
                    ancien = JSON.parse(trouverBloc(enPlace, "registre").json);
                } catch {
                    ancien = null;
                }
            const requis = droitsDuRegistre(ancien, JSON.parse(bloc.json));
            if (requis.length) this.exigerDroit(ctx, requis, true);
            else this.exigerDroit(ctx, DROITS_REGISTRE);
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
        const bloc = await this.blocDepose(ctx, corps.revocations, "revocations", "revocations");
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
        const ancienne = this.revocations(); // 1.47.0 : les entrees neuves font chacune un fait (stats.js)
        this.ctx.storage.transactionSync(() => {
            this.ecrireConfig("revocations", texte);
            this.ecrireConfig("revocations_liste", JSON.stringify(liste));
            this.ecrireConfig("revocations_numero", String(liste.numero));
        });
        this.revCache = null;
        this.faitsRevocations(ctx.now, ancienne, liste, await this.idAppelant(ctx));
        // Les jetons des PC revoques sont rendus tout de suite (leurs activations ne sont plus renouvelees).
        for (const j of this.lignes("SELECT DISTINCT licence, poste FROM jetons")) {
            const lic = this.licence(j.licence);
            if (lic && this.revoquee(lic, j.poste)) this.x("DELETE FROM jetons WHERE licence = ? AND poste = ?", j.licence, j.poste);
        }
        this.signalerChangement();
        return reponseJson(200, { revocations: liste.numero });
    }

    async deposerLicence(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocDepose(ctx, corps.licence, "licence", "licence");
        let doc;
        try {
            doc = lireLicence(bloc.json);
        } catch (e) {
            throw new Erreur(400, "document", "La licence est illisible : " + e.message + ".");
        }
        // 1.44.0 : un admin - une licence nouvelle demande << licence_creer >> ; une licence deja la, l'un des trois.
        this.exigerDroit(ctx, this.licence(doc.id) ? DROITS_LICENCE : ["licence_creer"]);
        if (corps.code_hash !== undefined && corps.code_hash !== null && !estHex(corps.code_hash, 64))
            throw mal("code_hash", "Le champ « code_hash » doit être l'empreinte du code (64 hexadécimaux minuscules), ou null.");
        if (corps.bail_jours !== undefined && !(Number.isInteger(corps.bail_jours) && corps.bail_jours >= 1 && corps.bail_jours <= 3660))
            throw mal("bail_jours", "Le champ « bail_jours » doit être un nombre de jours entre 1 et 3660.");
        if (corps.active !== undefined && typeof corps.active !== "boolean") throw mal("active");
        const avant = this.licence(doc.id);
        if (avant && doc.emission < avant.emission)
            throw new Erreur(409, "ancien", "Cette émission de la licence est plus ancienne que celle du service (" + iso(avant.emission) + ").");
        if (!avant && this.compte("n_licences") >= MAX_LICENCES)
            throw new Erreur(507, "limite", "Le service a atteint son nombre maximal de licences.");
        const codeHash = corps.code_hash === undefined ? (avant ? avant.codeHash : null) : corps.code_hash;
        if (codeHash && this.ligne("SELECT id FROM licences WHERE code_hash = ? AND id != ?", codeHash, doc.id))
            throw new Erreur(409, "code", "Ce code est déjà celui d'une autre licence : crée un code neuf.");
        const bail = corps.bail_jours !== undefined ? corps.bail_jours : avant ? avant.bail : BAIL_DEFAUT;
        const active = corps.active !== undefined ? corps.active : avant ? avant.active : true;
        this.x(
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
        if (!avant) this.ajouter("n_licences", 1);
        // 1.47.0 : les faits (stats.js) - nouvelle, prolongee, une option ajoutee ; par la cle qui la depose
        this.faitsLicence(ctx.now, avant, ecrireBlocLu(bloc), doc, await this.idAppelant(ctx));
        // Desactivee par l'editeur : les places gardees par des PC liberes reviennent (sa decision).
        if (!active) this.x("UPDATE activations SET place_jusqu = 0 WHERE licence = ? AND etat = 'liberee' AND place_jusqu > 0", doc.id);
        this.signalerChangement();
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
            // un PC libere au-dela des transferts permis : sa place reste prise jusque-la (null : aucune place gardee)
            place_jusqu_a: r.etat === "liberee" && r.place_jusqu > now ? iso(r.place_jusqu) : null,
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
        // Retire par l'editeur : la place revient tout de suite (sa decision), meme celle d'un PC libere qui la gardait.
        this.x("UPDATE activations SET etat = 'retiree', place_jusqu = 0 WHERE licence = ? AND poste = ?", corps.licence, corps.poste);
        this.x("DELETE FROM jetons WHERE licence = ? AND poste = ?", corps.licence, corps.poste);
        // 1.47.0 : le fait (stats.js) - retire par cet admin
        if (r.etat !== "retiree") this.faitPc(ctx.now, "pc_retire", this.licence(corps.licence), corps.poste, await this.idAppelant(ctx));
        this.signalerChangement();
        return reponseJson(200, {});
    }

    async listerDemandes(ctx) {
        const d = ctx.url.searchParams.get("depuis");
        if (d !== null && !/^\d{1,15}$/.test(d)) throw new Erreur(400, "champ", "« depuis » doit être un numéro de demande.");
        const depuis = d === null ? 0 : Number(d);
        // 1.44.0 : a chaque cle SON scelle. L'admin+ : celui de l'editeur (comme avant). Un admin : le sien
        // (`cle` dit alors son identifiant) ; sans scelle pour lui, la demande est listee sans contenu (scelle null,
        // pour_un_autre : scellee pour un autre admin).
        let miens = null;
        if (ctx.appelant.role === "admin") {
            miens = new Map();
            const id = await cleIdDe(ctx.appelant.octets);
            for (const s of this.lignes("SELECT numero, scelle FROM demandes_scelles WHERE cle = ? AND numero > ?", id, depuis)) miens.set(s.numero, { id, scelle: s.scelle });
        }
        const demandes = this.lignes("SELECT * FROM demandes WHERE numero > ? ORDER BY numero LIMIT 1000", depuis).map((r) => {
            const o = { numero: r.numero, poste: r.poste, cle: r.cle, scelle: r.scelle, etat: r.etat, cree: iso(r.cree), maj: iso(r.maj) };
            if (miens) {
                const m = miens.get(r.numero);
                if (m) {
                    o.cle = m.id;
                    o.scelle = m.scelle;
                } else {
                    o.scelle = null;
                    o.pour_un_autre = true;
                }
            }
            return o;
        });
        return reponseJson(200, { demandes });
    }

    // 1.47.0 : `issue` (facultatif) : "acceptee" ou "refusee" - pour les statistiques (stats.js : le fait demande_traitee,
    // son delai, qui l'a traitee).
    async traiterDemande(ctx) {
        const corps = this.corpsJson(ctx);
        if (!entierPositif(corps.numero) || corps.numero < 1) throw mal("numero");
        if (corps.issue !== undefined && corps.issue !== "acceptee" && corps.issue !== "refusee")
            throw mal("issue", "Le champ « issue » vaut « acceptee » ou « refusee » (ou il est absent).");
        const r = this.ligne("SELECT etat, cree, poste FROM demandes WHERE numero = ?", corps.numero);
        if (!r) throw new Erreur(404, "demande", "Cette demande n'existe pas.");
        if (r.etat === "attente") {
            this.x("UPDATE demandes SET etat = 'traitee', maj = ? WHERE numero = ?", ctx.now, corps.numero);
            this.ajouter("n_attente", -1);
            const f = this.ligne("SELECT licence, offre, client FROM stats_faits WHERE genre = 'demande_recue' AND valeur = ? AND poste = ? LIMIT 1", corps.numero, r.poste);
            this.fait(ctx.now, "demande_traitee", {
                licence: f ? f.licence : "",
                offre: f ? f.offre : "",
                client: f ? f.client : "",
                admin: await this.idAppelant(ctx),
                poste: r.poste,
                valeur: Math.max(0, ctx.now - r.cree),
                detail: corps.issue ? { issue: corps.issue, numero: corps.numero } : { numero: corps.numero },
            });
        } else if (r.etat !== "traitee")
            throw new Erreur(409, "etat", r.etat === "annulee" ? "Le PC a annulé cette demande." : "Le PC a remplacé cette demande par une plus récente.", { etat: r.etat });
        return reponseJson(200, {});
    }

    async deposerPoste(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.poste, 32)) throw mal("poste", "Le champ « poste » doit être l'identifiant du poste (32 hexadécimaux minuscules).");
        const bloc = await this.blocDepose(ctx, corps.etat, "poste", "etat");
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
        if (!avant && this.compte("n_postes") >= MAX_POSTES) throw new Erreur(507, "limite", "Le service a atteint son nombre maximal de postes.");
        this.x(
            "INSERT INTO postes (poste, bloc, numero, maj) VALUES (?, ?, ?, ?) ON CONFLICT (poste) DO UPDATE SET bloc = excluded.bloc, numero = excluded.numero, maj = excluded.maj",
            corps.poste,
            texte,
            p.numero,
            ctx.now,
        );
        if (!avant) this.ajouter("n_postes", 1);
        this.signalerChangement();
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
                if (c.texte === null) this.x("DELETE FROM archive WHERE chemin = ?", c.chemin);
                else
                    this.x(
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
    // 1.40.0 : L'ETAT VU PAR L'EDITEUR (signe par lui) - des compteurs qui montent a chaque changement : l'outil
    // ouvert le regarde toutes les 15 secondes et ne relit les licences (ou les demandes) que s'ils ont bouge.
    async etatAdmin(ctx) {
        return reponseJson(200, {
            version: VERSION,
            heure: ctx.now,
            activite: this.compte("activite"),
            demandes: this.compte("demandes_changements"),
            attente: this.compte("n_attente"),
            changements: this.compte("changements"),
            registre: Number(this.config("registre_numero") || "0"),
            // 1.44.0 : la liste des admins (son numero), le journal (son dernier numero), les demandes d'admin en
            // attente, et qui appelle : son role (<< admin+ >>, << admin >>) et ses droits.
            admins: Number(this.config("admins_numero") || "0"),
            journal: this.dernierNumeroJournal(),
            admins_demandes: this.nombre("SELECT COUNT(*) AS n FROM admins_demandes WHERE etat = 'attente'"),
            version_publiee: this.config("version_derniere") || "",
            role: ctx.appelant.role,
            droits: ctx.appelant.droits,
            // 1.46.0 : les commandes - un compteur qui monte a chaque changement d'une commande, et celles a livrer (payees).
            commandes: this.compte("commandes_changements"),
            a_livrer: this.nombre("SELECT COUNT(*) AS n FROM commandes WHERE etat = 'payee'"),
            // 1.47.0 : la derogation << Gerer le service >> (la sienne ; l'admin+ : celle en place), sans son document
            derogation: await this.derogationPourEtat(ctx),
        });
    }

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
        activations.lignes = this.compte("n_activations"); // le compteur tenu a chaque ecriture
        // Les appels du jour : les compteurs en memoire (cet appel compris, traiter() l'a compte), ecrits au passage.
        this.ecrireStats(ctx.now, true);
        const routes = {};
        let total = 0;
        for (const route of Object.keys(this.statsMem.routes).sort()) {
            routes[route] = this.statsMem.routes[route];
            total += routes[route];
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
            demandes: { attente: 0, annulee: 0, remplacee: 0, traitee: 0, ...parEtat("demandes"), octets: this.compte("octets_demandes") },
            decouvertes: this.compte("n_decouvertes"),
            postes: this.compte("n_postes"),
            archive: { version: Number(this.config("archive_version") || "0"), fichiers: archive.n, octets: archive.o },
            appels_jour: { jour, total, routes },
        });
    }
}

// 1.44.0 : les routes des admins, des fichiers (et des messages, des versions), de la sauvegarde - des methodes de
// l'objet, ecrites a part (admins.js, fichiers.js, sauvegarde.js).
// 1.47.0 : les statistiques et le globe (stats.js).
Object.assign(Licences.prototype, RoutesAdmins, RoutesFichiers, RoutesSauvegarde, RoutesBoutique, RoutesStats, RoutesDerogations);
