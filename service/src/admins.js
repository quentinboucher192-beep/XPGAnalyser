// =============================================================================
//  service/src/admins.js - 1.44.0 : PLUSIEURS ADMINS (docs/ADMINS.md, § 2 a 6)
//  - la liste des admins (genre << admins >>, signee par la cle de l'editeur),
//    publique (GET /v1/admins) et lue en tables (admins) pour l'authentification
//    des routes /admin (licences.js, verifierAppel) ;
//  - les demandes d'admin (POST /v1/admins/demande, signee par la cle du futur
//    admin) : une par cle, 20 en attente au plus ;
//  - le trousseau : la cle de synchro scellee pour chaque admin (le service ne
//    voit qu'un bloc scelle) ;
//  - le journal des admins : une chaine (chaque ligne porte l'empreinte de la
//    precedente) ; chaque entree est signee par la cle qui l'envoie ; le service
//    ecrit lui-meme (signees par SA cle) les lignes signature_refusee, autre_pc,
//    demande_admin, sauvegarde, restauration.
// -----------------------------------------------------------------------------
//  Les routes sont des methodes du Durable Object (licences.js les ajoute a sa
//  classe : `this` est l'objet, ses tables et ses aides).
//  LES TABLES
//    admins           la liste en place, une ligne par cle (droits, gardes : JSON)
//    admins_demandes  les demandes d'admin (attente, acceptee, refusee) ; une
//                     demande close est effacee apres 7 jours
//    trousseau        le scelle de la cle de synchro, par cle d'admin
//    journal          les lignes du journal, telles qu'ecrites (texte), leur
//                     empreinte, la cle et le nonce de l'entree (un renvoi de la
//                     meme entree rend la meme ligne)
// =============================================================================
import { ecrireBlocLu } from "./blocs.js";
import { DocumentIllisible } from "./documents.js";
import {
    ED25519,
    Erreur,
    cleIdDe,
    enc,
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
    verifierEd25519,
} from "./outils.js";

const JOUR = 86400;

// Les 13 droits d'un admin (§ 2). L'admin+ (la cle de l'editeur) les a tous.
export const DROITS_ADMINS = [
    "licence_creer",
    "licence_prolonger",
    "licence_option",
    "licence_revoquer",
    "poste_retirer",
    "offres",
    "options",
    "annonces",
    "demandes",
    "codes",
    "client_supprimer",
    "clients_exporter",
    "courriels",
];
export const DROITS_LICENCE = ["licence_creer", "licence_prolonger", "licence_option"];

// Les actions du journal qu'un admin (ou l'admin+) envoie (§ 6) ; celles du service, il est seul a les ecrire.
export const ACTIONS_JOURNAL = [
    "licence_creer",
    "licence_prolonger",
    "licence_option",
    "licence_revoquer",
    "poste_retirer",
    "offres",
    "options",
    "annonces",
    "demande_accepter",
    "demande_refuser",
    "code_revoir",
    "client_supprimer",
    "client_restaurer", // 1.44.0 : un client restaure (l'historique et le journal de l'Admin)
    "clients_exporter",
    "courriel",
    "admin_ajouter",
    "admin_retirer",
    "admin_droits",
    "admin_profil",
    "version_publier",
    "fichier_envoyer",
    "criticite",
    "connexion",
    // 1.46.0 : la boutique (docs/BOUTIQUE.md)
    "boutique", // le catalogue, la vente en ligne, la livraison automatique
    "stripe_brancher", // Brancher Stripe (la cle secrete et le webhook passes au Worker)
    "commande_livrer", // une commande payee livree (sa licence)
    "commande_revoir", // une commande que l'Admin n'a pas pu livrer (a revoir)
    "commande_rembourser", // un remboursement demande a Stripe
];
export const ACTIONS_SERVICE = ["signature_refusee", "autre_pc", "demande_admin", "sauvegarde", "restauration"];

// Ce que signe la cle qui ecrit une entree du journal : ce prefixe, puis le texte de l'entree.
export const PREFIXE_JOURNAL = "XPG-JOURNAL-1\n";

const MAX_ADMINS = 32;
const MAX_GARDES = 512;
const MAX_DEMANDES_ADMINS = 20; // en attente, au plus
export const GARDE_DEMANDES_ADMINS = 7 * JOUR; // une demande d'admin close est effacee apres 7 jours
const MAX_TROUSSEAU = 4096; // un scelle (base64)
export const MAX_JOURNAL = 200000; // les lignes du journal, au plus
const MAX_ENTREE = 16 * 1024; // une entree (octets)
const LIGNES_JOURNAL = 500; // une lecture, au plus

// Les parties du registre et le droit qui les change (§ 2 ; le registre ecrit << announcements >> pour les
// annonces). Une autre partie qui change : << offres >>. Ce qui change a chaque registre n'est pas une partie.
const PARTIES_REGISTRE = { editions: "offres", options: "options", announcements: "annonces", annonces: "annonces" };
const HORS_PARTIES = new Set(["format", "version", "number", "issued_at"]);
export const DROITS_REGISTRE = ["offres", "options", "annonces"];

// Un texte d'une ligne, sans caractere de controle, entre min et max caracteres.
function texteLigne(v, min, max) {
    return typeof v === "string" && !/[\u0000-\u001f\u007f]/.test(v) && Array.from(v).length >= min && Array.from(v).length <= max;
}

// L'empreinte d'une cle, lue au telephone (§ 4) : les 24 premiers hexadecimaux, en majuscules, du SHA-256 de la
// cle publique, en 6 groupes de 4 (<< B7E2 48A9 1D3C 90F6 5A2E C471 >>).
export async function empreinteCle(octets) {
    return (await sha256Hex(octets)).slice(0, 24).toUpperCase().match(/.{4}/g).join(" ");
}

// La liste des admins (genre << admins >>, § 3) : {numero, emise, admins: [{id, cle, nom, pc, poste, depuis, profil,
// droits, retire, gardes}]}. Leve DocumentIllisible.
export async function lireListeAdmins(json) {
    const quoi = "la liste des admins";
    let v;
    try {
        v = JSON.parse(json);
    } catch {
        throw new DocumentIllisible(quoi + " n'est pas un JSON lisible");
    }
    if (!v || typeof v !== "object" || Array.isArray(v)) throw new DocumentIllisible(quoi + " n'est pas un objet JSON");
    if (v.format !== 1) throw new DocumentIllisible(quoi + " n'est pas du format 1");
    if (!entier(v.numero, 1, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : le numéro est illisible");
    if (!entier(v.emise, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : la date d'émission est illisible");
    if (!Array.isArray(v.admins) || v.admins.length > MAX_ADMINS) throw new DocumentIllisible(quoi + " : la liste est illisible (" + MAX_ADMINS + " admins au plus)");
    const vues = new Set();
    const admins = [];
    for (const [i, a] of v.admins.entries()) {
        const ou = quoi + " : l'admin n° " + (i + 1);
        if (!a || typeof a !== "object" || Array.isArray(a)) throw new DocumentIllisible(ou + " n'est pas un objet");
        if (!estHex(a.cle, 64)) throw new DocumentIllisible(ou + " : sa clé est illisible (64 hexadécimaux minuscules)");
        if (vues.has(a.cle)) throw new DocumentIllisible(ou + " : sa clé est déjà dans la liste");
        vues.add(a.cle);
        if (a.id !== (await cleIdDe(unhex(a.cle)))) throw new DocumentIllisible(ou + " : son identifiant ne correspond pas à sa clé");
        if (!texteLigne(a.nom, 1, 100)) throw new DocumentIllisible(ou + " : son nom est illisible (1 à 100 caractères)");
        const pc = a.pc === undefined ? "" : a.pc;
        if (!texteLigne(pc, 0, 100)) throw new DocumentIllisible(ou + " : le nom de son PC est illisible");
        const poste = a.poste === undefined ? "" : a.poste;
        if (poste !== "" && !estHex(poste, 32)) throw new DocumentIllisible(ou + " : l'identifiant de sa machine est illisible (32 hexadécimaux)");
        if (!entier(a.depuis, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(ou + " : « depuis » est illisible");
        const profil = a.profil === undefined ? "" : a.profil;
        if (!texteLigne(profil, 0, 60)) throw new DocumentIllisible(ou + " : son profil est illisible");
        if (
            !Array.isArray(a.droits) ||
            !a.droits.every((d) => DROITS_ADMINS.includes(d)) ||
            new Set(a.droits).size !== a.droits.length
        )
            throw new DocumentIllisible(ou + " : ses droits sont illisibles (un droit inconnu, ou deux fois)");
        if (!entier(a.retire, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(ou + " : « retire » est illisible");
        if (a.retire > 0 && a.droits.length) throw new DocumentIllisible(ou + " est retiré : ses droits doivent être vidés");
        const gardes = a.gardes === undefined ? [] : a.gardes;
        if (!Array.isArray(gardes) || gardes.length > MAX_GARDES || !gardes.every((g) => estHex(g, 64)))
            throw new DocumentIllisible(ou + " : ses empreintes gardées sont illisibles (" + MAX_GARDES + " au plus, 64 hexadécimaux)");
        admins.push({ id: a.id, cle: a.cle, nom: a.nom, pc, poste, depuis: a.depuis, profil, droits: a.droits, retire: a.retire, gardes });
    }
    return { numero: v.numero, emise: v.emise, admins };
}

// Les droits qu'il faut pour deposer `nouveau` (le JSON du registre) a la place de `ancien` (null : aucun) : le
// droit de chaque partie qui change (§ 2). [] : rien n'a change (l'un des trois droits du registre suffit alors).
export function droitsDuRegistre(ancien, nouveau) {
    const a = ancien && typeof ancien === "object" ? ancien : {};
    const requis = new Set();
    for (const k of new Set([...Object.keys(a), ...Object.keys(nouveau)])) {
        if (HORS_PARTIES.has(k)) continue;
        if (JSON.stringify(a[k]) === JSON.stringify(nouveau[k])) continue;
        requis.add(PARTIES_REGISTRE[k] || "offres");
    }
    return [...requis];
}

// La demande d'admin (le corps de POST /v1/admins/demande) : {nom, courriel, pc, poste, logiciel}.
function lireDemandeAdmin(corps) {
    const nom = typeof corps.nom === "string" ? corps.nom.trim() : null;
    if (!texteLigne(nom, 1, 60)) throw mal("nom", "Le champ « nom » doit être ton nom (1 à 60 caractères).");
    const courriel = typeof corps.courriel === "string" ? corps.courriel.trim() : null;
    if (!courriel || courriel.length > 120 || !/^[^\s@<>"]+@[^\s@<>"]+\.[^\s@<>"]+$/.test(courriel))
        throw mal("courriel", "Le champ « courriel » doit être une adresse de courriel (120 caractères au plus).");
    const pc = corps.pc === undefined ? "" : corps.pc;
    if (!texteLigne(pc, 0, 60)) throw mal("pc", "Le champ « pc » doit être le nom de ce PC (60 caractères au plus).");
    if (!estHex(corps.poste, 32)) throw mal("poste", "Le champ « poste » doit être l'identifiant de cette machine (32 hexadécimaux minuscules).");
    const logiciel = corps.logiciel === undefined ? "" : corps.logiciel;
    if (!texteLigne(logiciel, 0, 40)) throw mal("logiciel", "Le champ « logiciel » doit faire 40 caractères au plus.");
    return { nom, courriel, pc, poste: corps.poste, logiciel };
}

function depuisDe(ctx) {
    const d = ctx.url.searchParams.get("depuis");
    if (d !== null && !/^\d{1,15}$/.test(d)) throw new Erreur(400, "champ", "« depuis » doit être un numéro.");
    return d === null ? 0 : Number(d);
}

// Les methodes ajoutees au Durable Object (licences.js).
export const RoutesAdmins = {
    // ============================================================ la liste ==
    // L'admin d'une cle (la ligne de la table), ou null.
    admin(cle) {
        return this.ligne("SELECT * FROM admins WHERE cle = ?", cle);
    },

    // GET /v1/admins (publique) : le bloc de la liste en place.
    async lireAdminsPublic() {
        const bloc = this.config("admins");
        if (!bloc) throw new Erreur(404, "admins", "Le service n'a pas encore de liste des admins.");
        return reponseTexte(200, bloc);
    },

    // PUT /admin/admins (admin+) {"admins":"<bloc admins>"} : la liste signee par la cle de l'editeur. Le numero
    // monte (le meme numero : le meme texte, rien a faire). Un admin retire, ou absent de la liste, perd son scelle du
    // trousseau ; la demande d'admin d'une cle de la liste (non retiree) est close (acceptee).
    async deposerAdmins(ctx) {
        const corps = this.corpsJson(ctx);
        const bloc = await this.blocEditeur(corps.admins, "admins", "admins");
        let liste;
        try {
            liste = await lireListeAdmins(bloc.json);
        } catch (e) {
            if (!(e instanceof DocumentIllisible)) throw e;
            throw new Erreur(400, "document", "La liste des admins est illisible : " + e.message + ".");
        }
        const texte = ecrireBlocLu(bloc);
        const actuel = Number(this.config("admins_numero") || "0");
        if (liste.numero < actuel || (liste.numero === actuel && texte !== this.config("admins")))
            throw new Erreur(409, "ancien", "Cette liste des admins (n° " + liste.numero + ") n'est pas plus récente que celle du service (n° " + actuel + ").", {
                numero: actuel,
            });
        if (liste.numero === actuel) return reponseJson(200, { admins: actuel });
        let acceptees = 0;
        this.ctx.storage.transactionSync(() => {
            this.ecrireConfig("admins", texte);
            this.ecrireConfig("admins_numero", String(liste.numero));
            this.x("DELETE FROM admins");
            for (const a of liste.admins)
                this.x(
                    "INSERT INTO admins (cle, id, nom, pc, poste, depuis, profil, droits, retire, gardes) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    a.cle,
                    a.id,
                    a.nom,
                    a.pc,
                    a.poste,
                    a.depuis,
                    a.profil,
                    JSON.stringify(a.droits),
                    a.retire,
                    JSON.stringify(a.gardes),
                );
            this.x("DELETE FROM trousseau WHERE cle NOT IN (SELECT cle FROM admins WHERE retire = 0)");
            acceptees = this.nombre("SELECT COUNT(*) AS n FROM admins_demandes WHERE etat = 'attente' AND cle IN (SELECT cle FROM admins WHERE retire = 0)");
            if (acceptees)
                this.x(
                    "UPDATE admins_demandes SET etat = 'acceptee', maj = ? WHERE etat = 'attente' AND cle IN (SELECT cle FROM admins WHERE retire = 0)",
                    ctx.now,
                );
        });
        return reponseJson(200, { admins: liste.numero, demandes_acceptees: acceptees });
    },

    // La derniere ligne du journal ecrite par une cle (son heure, 0 : aucune).
    derniereAction(cle) {
        const r = this.ligne("SELECT recu FROM journal WHERE cle = ? ORDER BY numero DESC LIMIT 1", cle);
        return r ? r.recu : 0;
    },
    // Le logiciel d'une cle (X-XPG-Logiciel de ses appels signes).
    logicielDe(cle) {
        const r = this.ligne("SELECT logiciel, version, vu FROM logiciels WHERE cle = ?", cle);
        return r ? { logiciel: r.logiciel, version: r.version, vu: r.vu } : { logiciel: "", version: "", vu: 0 };
    },

    // GET /admin/admins (admin+ ou admin) : la liste lue (sans les empreintes gardees), avec, pour chaque cle, sa
    // derniere action au journal et son logiciel ; et les cles de l'editeur (l'admin+).
    async listerAdmins() {
        const admins = this.lignes("SELECT * FROM admins ORDER BY depuis, cle").map((r) => ({
            id: r.id,
            cle: r.cle,
            nom: r.nom,
            pc: r.pc,
            poste: r.poste,
            depuis: r.depuis,
            profil: r.profil,
            droits: JSON.parse(r.droits),
            retire: r.retire,
            derniere: this.derniereAction(r.cle),
            ...this.logicielDe(r.cle),
        }));
        const editeurs = [];
        for (const k of await this.clesEditeur()) editeurs.push({ id: k.id, cle: k.hex, derniere: this.derniereAction(k.hex), ...this.logicielDe(k.hex) });
        return reponseJson(200, { numero: Number(this.config("admins_numero") || "0"), admins, editeurs });
    },

    // ============================================================ les demandes d'admin ==
    // POST /v1/admins/demande (signee par la cle du futur admin) {nom, courriel, pc, poste, logiciel} : une demande
    // par cle (la deposer de nouveau la remplace), 20 en attente au plus ; une ligne au journal (demande_admin).
    async deposerDemandeAdmin(ctx) {
        const corps = this.corpsJson(ctx);
        const d = lireDemandeAdmin(corps);
        const { cle, octets } = ctx.appelant;
        if ((await this.clesEditeur()).some((k) => k.hex === cle)) throw new Erreur(409, "deja", "Cette clé est celle de l'éditeur : elle fait déjà tout.");
        const a = this.admin(cle);
        if (a && !a.retire) throw new Erreur(409, "deja", "Cette clé est déjà celle d'un admin de la liste.");
        const attente = this.nombre("SELECT COUNT(*) AS n FROM admins_demandes WHERE etat = 'attente' AND cle != ?", cle);
        if (attente >= MAX_DEMANDES_ADMINS) throw new Erreur(507, "limite", "Trop de demandes d'admin attendent l'admin+ : réessaie plus tard.");
        this.x(
            `INSERT INTO admins_demandes (cle, nom, courriel, pc, poste, logiciel, etat, recue, maj) VALUES (?, ?, ?, ?, ?, ?, 'attente', ?, ?)
             ON CONFLICT (cle) DO UPDATE SET nom = excluded.nom, courriel = excluded.courriel, pc = excluded.pc, poste = excluded.poste,
                 logiciel = excluded.logiciel, etat = 'attente', recue = excluded.recue, maj = excluded.maj`,
            cle,
            d.nom,
            d.courriel,
            d.pc,
            d.poste,
            d.logiciel,
            ctx.now,
            ctx.now,
        );
        const empreinte = await empreinteCle(octets);
        await this.journalService(ctx.now, "demande_admin", await cleIdDe(octets), "demande à devenir admin : " + d.nom + (d.pc ? " (" + d.pc + ")" : ""), {
            cle,
            empreinte,
            nom: d.nom,
            pc: d.pc,
            poste: d.poste,
            logiciel: d.logiciel,
        });
        return reponseJson(201, { etat: "attente", empreinte });
    },

    // GET /v1/admins/demande?cle=<la cle qui signe> : ou en est la demande de cette cle (attente, acceptee, refusee ;
    // retire : un admin retire de la liste, sans nouvelle demande). Une cle de la liste (non retiree) : acceptee.
    async etatDemandeAdmin(ctx) {
        const q = ctx.url.searchParams.get("cle");
        if (q !== null && q !== ctx.appelant.cle) throw mal("cle", "« cle » doit être la clé qui signe l'appel.");
        const a = this.admin(ctx.appelant.cle);
        const numero = Number(this.config("admins_numero") || "0");
        if (a && !a.retire) return reponseJson(200, { etat: "acceptee", admins: numero });
        const r = this.ligne("SELECT etat, recue, maj FROM admins_demandes WHERE cle = ?", ctx.appelant.cle);
        if (a && (!r || r.etat !== "attente")) return reponseJson(200, { etat: "retire", admins: numero });
        if (!r) throw new Erreur(404, "demande", "Cette clé n'a pas de demande d'admin.");
        return reponseJson(200, { etat: r.etat, recue: r.recue, maj: r.maj });
    },

    // GET /admin/admins/demandes (admin+) : les demandes en attente (et l'empreinte de chaque cle, a lire au telephone).
    async listerDemandesAdmins() {
        const demandes = [];
        for (const r of this.lignes("SELECT * FROM admins_demandes WHERE etat = 'attente' ORDER BY recue, cle"))
            demandes.push({
                cle: r.cle,
                id: await cleIdDe(unhex(r.cle)),
                empreinte: await empreinteCle(unhex(r.cle)),
                nom: r.nom,
                courriel: r.courriel,
                pc: r.pc,
                poste: r.poste,
                logiciel: r.logiciel,
                recue: r.recue,
            });
        return reponseJson(200, { demandes });
    },

    // POST /admin/admins/demande/refuser (admin+) {"cle":"<64 hexa>"}.
    async refuserDemandeAdmin(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.cle, 64)) throw mal("cle", "Le champ « cle » doit être la clé du demandeur (64 hexadécimaux minuscules).");
        const r = this.ligne("SELECT etat FROM admins_demandes WHERE cle = ?", corps.cle);
        if (!r) throw new Erreur(404, "demande", "Cette clé n'a pas de demande d'admin.");
        if (r.etat === "attente") this.x("UPDATE admins_demandes SET etat = 'refusee', maj = ? WHERE cle = ?", ctx.now, corps.cle);
        else if (r.etat !== "refusee") throw new Erreur(409, "etat", "Cette demande est déjà acceptée.", { etat: r.etat });
        return reponseJson(200, {});
    },

    // ============================================================ le trousseau ==
    // PUT /admin/trousseau (admin+) {"cle":"<cle de l'admin>","scelle":"<base64>"} : la cle de synchro, scellee pour
    // cet admin (le service ne l'ouvre pas).
    async deposerTrousseau(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.cle, 64)) throw mal("cle", "Le champ « cle » doit être la clé de l'admin (64 hexadécimaux minuscules).");
        if (!estBase64(corps.scelle, MAX_TROUSSEAU)) throw mal("scelle", "Le champ « scelle » doit être du base64 (4 Kio au plus).");
        const a = this.admin(corps.cle);
        if (!a || a.retire) throw new Erreur(409, "admin", "Cette clé n'est pas celle d'un admin de la liste en place (non retiré) : publie d'abord la liste.");
        this.x(
            "INSERT INTO trousseau (cle, scelle, maj) VALUES (?, ?, ?) ON CONFLICT (cle) DO UPDATE SET scelle = excluded.scelle, maj = excluded.maj",
            corps.cle,
            corps.scelle,
            ctx.now,
        );
        return reponseJson(200, {});
    },

    // GET /admin/trousseau (admin+ ou admin) : le scelle de LA cle qui appelle.
    async lireTrousseau(ctx) {
        const r = this.ligne("SELECT scelle FROM trousseau WHERE cle = ?", ctx.appelant.cle);
        if (!r) throw new Erreur(404, "trousseau", "Le trousseau n'a rien pour cette clé : demande à l'admin+ de te le donner.");
        return reponseJson(200, { scelle: r.scelle });
    },

    // ============================================================ le journal ==
    // Ajouter une ligne a la chaine : {"numero","precedent","recu","cle","entree","signature"}, gardee telle quelle
    // (son empreinte : le SHA-256 de ce texte). 507 quand le journal est plein.
    async ajouterLigne(cle, entree, signature, nonce, now) {
        const dernier = this.ligne("SELECT numero, empreinte FROM journal ORDER BY numero DESC LIMIT 1");
        const numero = dernier ? dernier.numero + 1 : 1;
        if (numero > MAX_JOURNAL) throw new Erreur(507, "limite", "Le journal des admins est plein (" + MAX_JOURNAL + " lignes).");
        const texte = JSON.stringify({ numero, precedent: dernier ? dernier.empreinte : "", recu: now, cle, entree, signature });
        this.x(
            "INSERT INTO journal (numero, texte, empreinte, cle, nonce, recu) VALUES (?, ?, ?, ?, ?, ?)",
            numero,
            texte,
            await sha256Hex(texte),
            cle,
            nonce,
            now,
        );
        return numero;
    },

    // Une ligne ecrite par le service (signee par SA cle) : {"quand","action","objet","resume","detail","nonce"}.
    // Le journal plein : rien (l'appel qui l'a fait ecrire n'echoue pas pour autant). Rend son numero (0 : rien).
    async journalService(now, action, objet, resume, detail) {
        const nonce = hasardHex(16);
        const entree = JSON.stringify({ quand: now, action, objet, resume, detail, nonce });
        const signature = hex(new Uint8Array(await crypto.subtle.sign(ED25519, this.clePrivee, enc.encode(PREFIXE_JOURNAL + entree))));
        try {
            return await this.ajouterLigne(this.clePub, entree, signature, nonce, now);
        } catch (e) {
            if (e instanceof Erreur && e.statut === 507) return 0;
            throw e;
        }
    },

    // Une cle d'admin retire a signe un appel : une ligne << signature_refusee >> (une par cle et par heure au plus :
    // une cle retiree qui insiste ne remplit pas le journal).
    async signatureRefusee(ctx, a) {
        const heure = Math.floor(ctx.now / 3600);
        if (!this.refusesMem) this.refusesMem = new Map();
        if (this.refusesMem.get(a.cle) === heure) return;
        this.refusesMem.set(a.cle, heure);
        await this.journalService(ctx.now, "signature_refusee", a.id, "a refusé un appel signé par " + a.nom + " (admin retiré)", {
            cle: a.cle,
            nom: a.nom,
            route: ctx.methode + " " + ctx.url.pathname,
            retire: a.retire,
        });
    },

    // POST /admin/journal (admin+ ou admin) {"entree":"<JSON>","signature":"<128 hexa>"} : la signature (de la cle
    // qui appelle) porte sur "XPG-JOURNAL-1\n" + entree. La meme entree (le meme nonce, la meme cle) envoyee de
    // nouveau : 200 et la meme ligne. Une entree d'un admin dont le << poste >> n'est pas celui de la liste : le service
    // ecrit aussi une ligne << autre_pc >>.
    async ecrireJournal(ctx) {
        const corps = this.corpsJson(ctx);
        if (typeof corps.entree !== "string" || !corps.entree || enc.encode(corps.entree).length > MAX_ENTREE)
            throw mal("entree", "Le champ « entree » doit être le texte JSON de l'entrée (16 Kio au plus).");
        if (!estHex(corps.signature, 128)) throw mal("signature", "Le champ « signature » doit faire 128 hexadécimaux minuscules.");
        let e;
        try {
            e = JSON.parse(corps.entree);
        } catch {
            throw new Erreur(400, "entree", "L'entrée du journal n'est pas un JSON lisible.");
        }
        if (!e || typeof e !== "object" || Array.isArray(e)) throw new Erreur(400, "entree", "L'entrée du journal doit être un objet JSON.");
        if (typeof e.quand !== "number" || !Number.isFinite(e.quand) || e.quand < 0) throw new Erreur(400, "entree", "L'entrée du journal : « quand » doit être une heure (secondes Unix).");
        if (typeof e.action !== "string" || !ACTIONS_JOURNAL.includes(e.action))
            throw new Erreur(400, "entree", "L'entrée du journal : l'action « " + String(e.action).slice(0, 40) + " » est inconnue.");
        if (!estHex(e.nonce, 32)) throw new Erreur(400, "entree", "L'entrée du journal : « nonce » doit faire 32 hexadécimaux minuscules.");
        const { cle, octets } = ctx.appelant;
        if (!(await verifierEd25519(octets, enc.encode(PREFIXE_JOURNAL + corps.entree), unhex(corps.signature))))
            throw new Erreur(400, "signature", "L'entrée du journal n'est pas signée par la clé qui l'envoie.");
        const deja = this.ligne("SELECT numero FROM journal WHERE cle = ? AND nonce = ?", cle, e.nonce);
        if (deja) return reponseJson(200, { numero: deja.numero });
        const numero = await this.ajouterLigne(cle, corps.entree, corps.signature, e.nonce, ctx.now);
        const a = ctx.appelant.admin;
        if (a && a.poste && typeof e.poste === "string" && e.poste !== a.poste)
            await this.journalService(ctx.now, "autre_pc", a.id, "une entrée de " + a.nom + " (ligne " + numero + ") vient d'un autre PC que le sien", {
                ligne: numero,
                cle: a.cle,
                poste_liste: a.poste,
                poste_entree: e.poste.slice(0, 64),
                pc: typeof e.pc === "string" ? e.pc.slice(0, 100) : "",
            });
        return reponseJson(201, { numero });
    },

    // GET /admin/journal?depuis=N (admin+) : les lignes apres N (500 au plus), telles qu'ecrites ; `dernier` : le
    // numero de la derniere ligne du journal.
    async lireJournal(ctx) {
        const depuis = depuisDe(ctx);
        const lignes = this.lignes("SELECT texte FROM journal WHERE numero > ? ORDER BY numero LIMIT ?", depuis, LIGNES_JOURNAL).map((r) => r.texte);
        return reponseJson(200, { lignes, dernier: this.dernierNumeroJournal() });
    },

    dernierNumeroJournal() {
        return this.nombre("SELECT COALESCE(MAX(numero), 0) AS n FROM journal");
    },
};
