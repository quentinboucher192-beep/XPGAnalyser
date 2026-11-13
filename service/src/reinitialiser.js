// =============================================================================
//  service/src/reinitialiser.js - 1.49.0 : REPARTIR DE ZERO (docs/ADMINS.md,
//  par. 13) - l'admin+ seul, jamais d'elle-meme
// -----------------------------------------------------------------------------
//  POST /admin/reinitialiser (admin+, la cle de l'editeur) :
//    {"confirmation":"REPARTIR","sauvegarde":"<64 hexa : le SHA-256 du fichier
//     .xpgsauve qui vient d'etre ecrit>","admins":"<bloc admins>",
//     "revocations":"<bloc revocations>"[,"registre":"<bloc registre>"]}
//  Refuse sans le mot << REPARTIR >>, sans une sauvegarde complete exportee par
//  cette cle il y a moins d'une heure (GET /admin/sauvegarde : la ligne de config
//  << sauvegarde_derniere >>), sans une NOUVELLE liste des admins VIDE et une
//  NOUVELLE liste de revocation VIDE, signees par la cle de l'editeur, aux
//  numeros plus grands (l'anti-retour tient : un Studio, un Admin gardent le plus
//  grand numero vu). Le registre en place signe par un admin (qui n'existe plus) :
//  il faut le meme, re-signe par l'editeur, numero + 1 (sinon 409 << registre >>).
//
//  EFFACE (toutes les tables de la sauvegarde, sauf celles gardees ci-dessous) :
//  les licences, les activations, les liberations, les jetons flottants, les PC
//  en Decouverte, les demandes et leurs scelles, les etats de poste, l'archive en
//  ligne (chiffree : elle repart du poste de l'admin+), les admins, leurs
//  inscriptions (1.51.0), le trousseau, le journal (puis UNE ligne << reinitialisation >>,
//  numero 1, precedent "" : la chaine repart proprement), les messages aux PC,
//  les jetons des fichiers, les fichiers joints (sorte << autre >>, hors versions),
//  les logiciels vus, les commandes, leurs lignes, leurs scelles, les evenements
//  de Stripe deja traites, les statistiques (faits, releves, compteurs, versions
//  vues, lieux, les appels du jour) ; dans la config : la derogation, les
//  invitations d'admin, les exclusions des statistiques.
//  GARDE : la config (la cle du service, le certificat, le registre, le schema,
//  la derniere version publiee, les numeros qui montent : activations, demandes,
//  commandes, messages - un Studio qui lit ses messages << depuis N >> ne perd
//  rien de neuf), la boutique (le catalogue, les produits et les prix de Stripe),
//  les versions et leurs fichiers (et leurs morceaux), les nonces et les limites.
//  Les compteurs que les Studios et les Admins regardent (changements, activite,
//  demandes, commandes, la version de l'archive) MONTENT : tous se relisent.
// =============================================================================
import { lireListeAdmins } from "./admins.js";
import { BlocIllisible, ecrireBlocLu, trouverBloc } from "./blocs.js";
import { DocumentIllisible, lireRegistre, lireRevocations } from "./documents.js";
import { TABLES_SAUVEGARDE } from "./sauvegarde.js";
import { Erreur, estHex, mal, reponseJson } from "./outils.js";

export const MOT_REPARTIR = "REPARTIR";
// La sauvegarde complete exportee juste avant (GET /admin/sauvegarde) : {heure, cle}.
export const CONFIG_SAUVEGARDE = "sauvegarde_derniere";
const DELAI_SAUVEGARDE = 3600; // une heure au plus entre la sauvegarde et la remise a zero

// Les tables de la sauvegarde GARDEES ; toutes les autres sont effacees (une table neuve d'une version suivante : effacee
// par defaut - les essais nomment chacune, pour qu'aucune ne passe sans qu'on decide).
export const TABLES_GARDEES = ["config", "boutique", "versions", "fichiers"];
export const TABLES_EFFACEES = TABLES_SAUVEGARDE.map(([nom]) => nom).filter((nom) => !TABLES_GARDEES.includes(nom));

// Les lignes de config effacees (les listes signees sont remplacees, pas effacees).
const CONFIG_EFFACEES = ["derogation", "admins_invitations", "stats_exclus", CONFIG_SAUVEGARDE];
// Les compteurs des grandes tables (licences.js, boutique.js) : remis a zero avec leurs tables.
const COMPTEURS_ZERO = ["n_licences", "n_activations", "n_decouvertes", "n_postes", "n_attente", "octets_demandes", "n_commandes", "octets_commandes", "n_logiciels"];
// Ce que les Studios et les Admins regardent pour se relire : monte.
const COMPTEURS_PLUS_UN = ["changements", "activite", "demandes_changements", "commandes_changements", "archive_version"];

// Un bloc signe par l'editeur et son JSON lu par `lire` ; une Erreur 400 sinon.
async function documentEditeur(objet, texte, genre, quoi, lire) {
    const bloc = await objet.blocEditeur(texte, genre, quoi);
    try {
        return { bloc, doc: lire(bloc.json), texte: ecrireBlocLu(bloc) };
    } catch (e) {
        if (!(e instanceof DocumentIllisible)) throw e;
        throw new Erreur(400, "document", "Le document « " + genre + " » est illisible : " + e.message + ".");
    }
}

// Les methodes ajoutees au Durable Object (licences.js).
export const RoutesReinitialiser = {
    // GET /admin/sauvegarde (le sommaire) l'appelle : une sauvegarde complete vient d'etre exportee par cette cle.
    noterSauvegarde(ctx) {
        this.ecrireConfig(CONFIG_SAUVEGARDE, JSON.stringify({ heure: ctx.now, cle: ctx.appelant.cle }));
    },

    // POST /admin/reinitialiser (admin+).
    async reinitialiser(ctx) {
        const corps = this.corpsJson(ctx);
        if (corps.confirmation !== MOT_REPARTIR)
            throw new Erreur(400, "confirmation", "Pour repartir de zéro, tape « " + MOT_REPARTIR + " » : rien n'est effacé sans ce mot.");
        if (!estHex(corps.sauvegarde, 64))
            throw mal("sauvegarde", "Le champ « sauvegarde » doit être le SHA-256 (64 hexadécimaux minuscules) de la sauvegarde complète faite juste avant.");
        let s = null;
        try {
            s = JSON.parse(this.config(CONFIG_SAUVEGARDE) || "null");
        } catch {
            s = null;
        }
        if (!s || s.cle !== ctx.appelant.cle || !(ctx.now - s.heure <= DELAI_SAUVEGARDE && s.heure <= ctx.now + 600))
            throw new Erreur(
                409,
                "sauvegarde",
                "Fais d'abord une sauvegarde complète du service (Paramètres de sécurité › Sauvegarder maintenant) : aucune n'a été exportée par cette clé dans l'heure. Rien n'est effacé.",
            );
        // les deux listes neuves, VIDES, plus recentes que celles en place
        const adm = await documentEditeur(this, corps.admins, "admins", "admins", () => null);
        let liste;
        try {
            liste = await lireListeAdmins(adm.bloc.json);
        } catch (e) {
            if (!(e instanceof DocumentIllisible)) throw e;
            throw new Erreur(400, "document", "La liste des admins est illisible : " + e.message + ".");
        }
        const adminsAvant = Number(this.config("admins_numero") || "0");
        if (liste.admins.length) throw new Erreur(400, "admins", "La liste des admins de la remise à zéro doit être vide (aucun admin).");
        if (liste.numero <= adminsAvant)
            throw new Erreur(409, "ancien", "La liste des admins (n° " + liste.numero + ") doit être plus récente que celle du service (n° " + adminsAvant + ").", { numero: adminsAvant });
        const rev = await documentEditeur(this, corps.revocations, "revocations", "revocations", lireRevocations);
        const revAvant = Number(this.config("revocations_numero") || "0");
        if (rev.doc.revoquees.length) throw new Erreur(400, "revocations", "La liste de révocation de la remise à zéro doit être vide.");
        if (rev.doc.numero <= revAvant)
            throw new Erreur(409, "ancien", "La liste de révocation (n° " + rev.doc.numero + ") doit être plus récente que celle du service (n° " + revAvant + ").", { numero: revAvant });
        // le registre (garde) : signe par un admin qui n'existera plus -> re-signe par l'editeur
        const regAvant = Number(this.config("registre_numero") || "0");
        let reg = null;
        if (corps.registre !== undefined) {
            reg = await documentEditeur(this, corps.registre, "registre", "registre", lireRegistre);
            if (reg.doc.numero <= regAvant)
                throw new Erreur(409, "ancien", "Le registre (n° " + reg.doc.numero + ") doit être plus récent que celui du service (n° " + regAvant + ").", { numero: regAvant });
        } else if (this.config("registre")) {
            let parEditeur = false;
            try {
                parEditeur = await this.signeParEditeur(trouverBloc(this.config("registre"), "registre"));
            } catch (e) {
                if (!(e instanceof BlocIllisible)) throw e;
            }
            if (!parEditeur)
                throw new Erreur(409, "registre", "Le registre en place est signé par un admin : re-signe-le avec la clé de l'éditeur (numéro + 1) et joins-le (« registre »).", {
                    numero: regAvant,
                });
        }

        // ---- la remise a zero : une seule transaction ----
        const effacees = {};
        let fichiersJoints = 0;
        this.ctx.storage.transactionSync(() => {
            for (const t of TABLES_EFFACEES) effacees[t] = this.nombre("SELECT COUNT(*) AS n FROM " + t);
            // les fichiers joints aux messages (sorte << autre >>) qui ne sont pas d'une version : partis avec les messages
            const versions = new Set();
            for (const v of this.lignes("SELECT fichiers FROM versions"))
                try {
                    for (const f of JSON.parse(v.fichiers)) versions.add(f.id);
                } catch {
                    // une version illisible : ses fichiers restent (rien n'est efface au hasard)
                }
            for (const f of this.lignes("SELECT id FROM fichiers WHERE sorte = 'autre'"))
                if (!versions.has(f.id)) {
                    this.x("DELETE FROM morceaux WHERE fichier = ?", f.id);
                    this.x("DELETE FROM fichiers WHERE id = ?", f.id);
                    fichiersJoints++;
                }
            for (const t of TABLES_EFFACEES) this.x("DELETE FROM " + t);
            for (const k of CONFIG_EFFACEES) this.ecrireConfig(k, null);
            for (const k of COMPTEURS_ZERO) this.ecrireConfig(k, "0");
            for (const k of COMPTEURS_PLUS_UN) this.suivant(k);
            this.ecrireConfig("octets_morceaux", String(this.nombre("SELECT COALESCE(SUM(LENGTH(octets)), 0) AS o FROM morceaux")));
            this.ecrireConfig("admins", adm.texte);
            this.ecrireConfig("admins_numero", String(liste.numero));
            this.ecrireConfig("revocations", rev.texte);
            this.ecrireConfig("revocations_liste", JSON.stringify(rev.doc));
            this.ecrireConfig("revocations_numero", String(rev.doc.numero));
            if (reg) {
                this.ecrireConfig("registre", reg.texte);
                this.ecrireConfig("registre_numero", String(reg.doc.numero));
            }
            this.ecrireConfig("stats_debut", String(ctx.now)); // les statistiques repartent d'ici
        });
        this.oublierCaches();
        // la premiere ligne du journal neuf (numero 1, precedent "") : ecrite et signee par le service
        const gardees = {};
        for (const t of TABLES_GARDEES) gardees[t] = this.nombre("SELECT COUNT(*) AS n FROM " + t);
        const lignes = Object.values(effacees).reduce((n, x) => n + x, 0);
        const journal = await this.journalService(ctx.now, "reinitialisation", "", "réinitialisation par l'admin+ : le service repart de zéro", {
            sauvegarde: corps.sauvegarde,
            lignes_effacees: lignes,
            effacees,
            fichiers_joints: fichiersJoints,
            admins: liste.numero,
            revocations: rev.doc.numero,
            registre: reg ? reg.doc.numero : regAvant,
        });
        return reponseJson(200, {
            reinitialise: true,
            journal,
            effacees,
            fichiers_joints: fichiersJoints,
            gardees,
            admins: liste.numero,
            revocations: rev.doc.numero,
            registre: reg ? reg.doc.numero : regAvant,
        });
    },
};
