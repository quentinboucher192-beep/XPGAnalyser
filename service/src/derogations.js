// =============================================================================
//  service/src/derogations.js - 1.47.0 : LA DEROGATION << Gerer le service >> ;
//  1.52.0 : LA DEROGATION << tout >>, ACTIVEE PAR UN CODE (docs/ADMINS.md, § 11)
// -----------------------------------------------------------------------------
//  Gerer le service (le mettre en ligne ou a jour, changer son adresse et ses
//  adresses de secours, le releve du jour) est a l'admin+ seul (brancher Stripe,
//  certifier, la sauvegarde, la restauration : toujours lui seul). Il peut le DONNER a UN admin
//  pour un temps COURT : une derogation signee par la cle de l'editeur,
//  de 10 minutes a 72 heures (jamais << permanente >>), revocable avant la fin.
//
//  1.52.0 : LE FORMAT 2 (l'Admin+ 1.52) - le droit << tout >> : l'admin voit tout comme l'admin+ (les routes << derogation >>
//  ci-dessous, et les LECTURES du journal des admins et des versions : GET /admin/journal, GET /admin/versions) ; ce qui
//  repose sur la signature de la cle de l'editeur ou lui est reserve reste a l'admin+ (la liste des admins, les
//  invitations, le trousseau, les fichiers, les messages, deposer une version, la boutique, Stripe, rembourser, la
//  sauvegarde, la restauration, repartir de zero, accorder et revoquer une derogation).
//  ELLE COMMENCE QUAND L'ADMIN TAPE LE CODE que l'admin+ voit sur son PC (8 chiffres). Le code n'est JAMAIS dans le
//  document signe : l'Admin+ le tire de sa cle (une signature Ed25519, deterministe, de "XPG-DEROGATION-CODE-1\n" + id,
//  son SHA-256, les 8 premiers octets modulo 10^8) et n'envoie que son empreinte
//  sha256hex("XPG-DEROGATION-CODE-1\n" + id + "\n" + 8 chiffres), a cote du document. Le service la garde et ne la rend
//  JAMAIS (ni vue(), ni /admin/etat, ni GET /admin/derogation, ni la sauvegarde) : 10^8 essais se font en quelques
//  secondes hors ligne a qui la lirait ; ici, 5 essais au plus, puis la derogation est annulee.
//
//  LE DOCUMENT (signe par une cle de l'editeur ; la signature porte sur
//  "XPG-DEROGATION-1\n" + le texte, comme le journal) :
//      format 1 (avant la 1.52.0 ; toujours recu) : {"format":1,"id":"<32 hexa>","droit":"service","admin":"<64 hexa>",
//               "nom":"Lea Essai","debut":T,"fin":T,"emise":T} - elle commence tout de suite ;
//      format 2 (1.52.0) : {"format":2,"id","droit":"tout","admin","nom","duree":S,"valable":T,"emise":T} - duree de
//               600 a 259200 s (10 min a 72 h) ; valable : l'heure limite pour taper le code (10 min a 24 h apres emise ;
//               l'Admin+ met emise + 1 h) ; emise a 10 min pres de l'heure du service.
//  Une seule derogation a la fois (une nouvelle remplace l'ancienne). Elle est
//  gardee dans la table config (une ligne << derogation >>) : elle part avec la
//  sauvegarde (SANS l'empreinte du code) et revient avec la restauration, sans table neuve (schema 5).
//
//  LES ETATS (vue(), GET /admin/etat) : en_attente (le code pas encore tape : duree, valable, essais_restants), active,
//  expiree, revoquee, annulee (raison : code_faux - 5 codes faux ; code_perime - pas tape avant << valable >>), a_venir
//  (format 1). Un Admin 1.51 ne connait pas en_attente ni annulee : << debut >> et << fin >> y valent l'heure limite (ou
//  celle de l'annulation) - jamais en cours pour lui.
//
//  LE SERVICE EN EST L'ARBITRE, A SON HEURE :
//    PUT  /admin/derogation           (admin+)  {"derogation":"<texte>","signature":"<128 hexa>"[,"code":"<64 hexa>"]}
//    POST /admin/derogation/activer   (admin)   {"id","code":"<8 chiffres>"} (1.52.0) : l'admin de la derogation tape
//         le code ; faux : 403 << code >> (<< Code faux : il te reste N essais. >>), le 5e l'annule ; juste : elle court
//         de maintenant a maintenant + duree. Une ligne au journal a chaque fois (derogation_code_refuse,
//         derogation_annulee, derogation_activee ; le service les signe ; jamais le code).
//    POST /admin/derogation/revoquer  (admin+)  {"id"} (en attente ou en cours)
//    GET  /admin/derogation           (admin+ : la derogation en cours ; un admin : la sienne seulement)
//    POST /admin/derogation/utiliser  (admin)   {"action","detail"} : l'Admin le demande AVANT chaque geste qui
//         gere le service (deployer, changer l'adresse, les adresses de secours...) ; 200 tant qu'elle court, 403
//         << derogation >> sinon (finie, revoquee, pas la sienne) ; une ligne << derogation_utilisee >> au journal
//         (le service l'ecrit, signee par SA cle : l'admin+ est prevenu).
//    Une route reservee a l'admin+ marquee << derogation >> (licences.js : POST /admin/stats/releve ; 1.52.0 : les
//    lectures GET /admin/journal et GET /admin/versions, marquees << tout >> : une derogation de format 2) s'ouvre aussi
//    a l'admin qui a une derogation en cours (et le journal le dit : une ligne par geste, 200 au plus ; une LECTURE, une
//    ligne la premiere fois seulement par route et par derogation - le regard des 15 s n'ecrit pas 200 lignes).
//  La fin : une ligne << derogation_expiree >> (le service, une fois), au premier appel qui la voit finie (GET
//  /admin/etat, que chaque Admin ouvert relit toutes les 15 s) ; 1.52.0 : le code pas tape a temps, une ligne
//  << derogation_annulee >> (une fois). Accorder et revoquer : les lignes de l'Admin de
//  l'admin+ (derogation_accorder, derogation_revoquer, signees par la cle de l'editeur).
//  Mettre le Worker en ligne se fait chez Cloudflare (wrangler), avec le compte Cloudflare de l'admin lui-meme
//  (membre du compte de l'editeur) : jamais un jeton partage ; le service ne le voit pas, d'ou << utiliser >>.
// =============================================================================
import { Erreur, egalConstant, enc, estHex, mal, reponseJson, sha256Hex, unhex, verifierEd25519 } from "./outils.js";

export const CONFIG_DEROGATION = "derogation";
export const PREFIXE_DEROGATION = "XPG-DEROGATION-1\n";
export const PREFIXE_CODE_DEROGATION = "XPG-DEROGATION-CODE-1\n"; // 1.52.0 : l'empreinte du code
export const DUREE_MIN_DEROGATION = 600; // 10 minutes
export const DUREE_MAX_DEROGATION = 72 * 3600; // 72 heures, jamais plus
const ECART_DEBUT = 600; // le debut (format 1), l'emission (format 2) a +-10 min de l'heure du service
const VALABLE_MIN = 600; // 1.52.0 : le code a taper dans les 10 min a 24 h qui suivent l'emission
const VALABLE_MAX = 24 * 3600;
export const ESSAIS_CODE_DEROGATION = 5; // 1.52.0 : 5 codes faux, la derogation est annulee
const MAX_DOCUMENT = 2048;
const MAX_LIGNES_USAGE = 200; // les lignes << derogation_utilisee >> d'une derogation, au plus
// Ce qu'un Admin dit faire avec sa derogation (la ligne du journal le nomme).
export const GESTES_DEROGATION = ["deployer", "connexion_cloudflare", "adresse", "secours", "releve", "verifier"];

function texteCourt(v, max) {
    return typeof v === "string" && !/[\u0000-\u001f\u007f]/.test(v) && Array.from(v).length <= max;
}

// << 30 min >>, << 4 h >>, << 2 h 30 >> (une duree en clair, pour le journal).
function dureeTexte(s) {
    if (s < 3600) return Math.round(s / 60) + " min";
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return m ? h + " h " + String(m).padStart(2, "0") : h + " h";
}

function entierPositif(v) {
    return typeof v === "number" && Number.isInteger(v) && v >= 0;
}

// L'empreinte d'un code (8 chiffres) pour cette derogation : ce que l'Admin+ envoie, ce que le service compare.
export async function empreinteCode(id, code) {
    return sha256Hex(PREFIXE_CODE_DEROGATION + id + "\n" + code);
}

// 1.52.0 : l'etat d'une derogation gardee, a l'heure du service.
function etatDe(d, now) {
    if (d.revoquee) return "revoquee";
    if (d.annulee) return "annulee";
    if (d.format === 2 && !d.debut) return now >= d.valable ? "annulee" : "en_attente";
    if (now >= d.fin) return "expiree";
    if (now < d.debut) return "a_venir";
    return "active";
}

function raisonDe(d, now) {
    if (d.raison) return d.raison;
    return d.format === 2 && !d.debut && !d.revoquee && now >= d.valable ? "code_perime" : "";
}

// La vue publique d'une derogation gardee (ce que rendent les routes et GET /admin/etat). JAMAIS l'empreinte du code.
function vue(d, now) {
    const etat = etatDe(d, now);
    const v = {
        id: d.id,
        admin: d.admin,
        nom: d.nom,
        format: d.format === 2 ? 2 : 1,
        droit: d.droit || "service",
        debut: d.debut,
        fin: d.fin,
        revoquee: d.revoquee || 0,
        etat,
        reste: etat === "active" ? d.fin - now : 0,
        usages: d.usages || 0,
        par: d.par,
        document: d.document,
        signature: d.signature,
    };
    if (d.format === 2) {
        v.duree = d.duree;
        v.valable = d.valable;
        v.emise = d.emise;
        v.essais_restants = Math.max(0, ESSAIS_CODE_DEROGATION - (d.essais || 0));
        v.annulee = d.annulee || (etat === "annulee" ? d.valable : 0);
        v.raison = raisonDe(d, now);
        if (!d.debut) {
            // pas encore commencee : un Admin 1.51 lit debut = fin (jamais << en cours >>)
            const t = v.annulee || d.valable;
            v.debut = t;
            v.fin = t;
        }
    }
    return v;
}

// Le droit << Gerer le service >> (format 1) ou << tout >> (format 2), en francais.
function libelle(d) {
    return d && d.format === 2 ? "dérogation" : "dérogation « Gérer le service »";
}

// Les methodes ajoutees au Durable Object (licences.js).
export const RoutesDerogations = {
    // La derogation gardee (objet), ou null.
    derogationGardee() {
        const t = this.config(CONFIG_DEROGATION);
        if (!t) return null;
        try {
            const d = JSON.parse(t);
            return d && typeof d === "object" && estHex(d.admin, 64) ? d : null;
        } catch {
            return null;
        }
    },

    // La derogation EN COURS de cette cle d'admin (a l'heure du service), ou null. 1.52.0 : activee (le code tape).
    derogationDe(cle, now) {
        const d = this.derogationGardee();
        if (!d || d.admin !== cle || etatDe(d, now) !== "active") return null;
        return d;
    },

    // Finie sans revocation et pas encore dite : une ligne << derogation_expiree >> (une fois). 1.52.0 : le code pas
    // tape avant << valable >> : annulee, une ligne << derogation_annulee >> (une fois).
    async noterFinDerogation(now) {
        const d = this.derogationGardee();
        if (!d || d.revoquee) return;
        if (d.format === 2 && !d.debut) {
            if (d.annulee || now < d.valable) return; // annulee par 5 codes faux : dite a ce moment-la
            d.annulee = d.valable;
            d.raison = "code_perime";
            this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
            await this.journalService(now, "derogation_annulee", d.id, "la dérogation de " + d.nom + " est annulée : le code n'a pas été tapé à temps", {
                derogation: d.id,
                admin: d.admin,
                nom: d.nom,
                raison: "code_perime",
                valable: d.valable,
            });
            return;
        }
        if (d.annulee || d.fin_notee || now < d.fin) return;
        d.fin_notee = true;
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        await this.journalService(now, "derogation_expiree", d.id, "la " + libelle(d) + " de " + d.nom + " a pris fin", {
            derogation: d.id,
            admin: d.admin,
            nom: d.nom,
            fin: d.fin,
            usages: d.usages || 0,
        });
    },

    // Le droit utilise (une route ou un geste de l'Admin) : la ligne du journal (bornee). 1.52.0 : `lecture` - une
    // route qui ne fait que lire (GET) : une ligne la premiere fois par route et par derogation, puis rien (ni ligne,
    // ni ecriture : le regard des 15 s la relit sans fin).
    async noterUsageDerogation(ctx, d, geste, detail, lecture) {
        if (lecture) {
            const vues = Array.isArray(d.lectures) ? d.lectures : [];
            if (vues.includes(geste)) return;
            d.lectures = [...vues, geste].slice(0, 20);
        }
        d.usages = (d.usages || 0) + 1;
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        if (d.usages > MAX_LIGNES_USAGE) return;
        await this.journalService(ctx.now, "derogation_utilisee", d.id, d.nom + " a utilisé sa " + libelle(d) + " : " + geste, {
            derogation: d.id,
            admin: d.admin,
            nom: d.nom,
            geste,
            detail: detail || (lecture ? "lecture (une ligne la première fois seulement)" : ""),
            fin: d.fin,
            reste: Math.max(0, d.fin - ctx.now),
        });
    },

    // PUT /admin/derogation (admin+) : la derogation signee par la cle de l'editeur qui appelle.
    async deposerDerogation(ctx) {
        const corps = this.corpsJson(ctx);
        if (typeof corps.derogation !== "string" || !corps.derogation || enc.encode(corps.derogation).length > MAX_DOCUMENT)
            throw mal("derogation", "Le champ « derogation » doit être le texte JSON de la dérogation (2 Kio au plus).");
        if (!estHex(corps.signature, 128)) throw mal("signature", "Le champ « signature » doit faire 128 hexadécimaux minuscules.");
        const { cle, octets } = ctx.appelant;
        if (!(await verifierEd25519(octets, enc.encode(PREFIXE_DEROGATION + corps.derogation), unhex(corps.signature))))
            throw new Erreur(400, "signature", "La dérogation n'est pas signée par la clé de l'éditeur qui l'envoie.");
        let doc;
        try {
            doc = JSON.parse(corps.derogation);
        } catch {
            throw new Erreur(400, "derogation", "La dérogation n'est pas un JSON lisible.");
        }
        const refus = (phrase) => new Erreur(400, "derogation", phrase);
        if (!doc || typeof doc !== "object" || Array.isArray(doc) || (doc.format !== 1 && doc.format !== 2))
            throw refus("La dérogation est illisible (format 1 ou 2 attendu).");
        const f2 = doc.format === 2;
        if (f2 ? doc.droit !== "tout" : doc.droit !== "service")
            throw refus(f2 ? "Une dérogation de format 2 donne « tout » (droit « tout »)." : "Une dérogation de format 1 ne donne que « Gérer le service » (droit « service »).");
        if (!estHex(doc.id, 32)) throw refus("L'identifiant de la dérogation doit faire 32 hexadécimaux minuscules.");
        if (!estHex(doc.admin, 64)) throw refus("La dérogation doit nommer la clé de l'admin (64 hexadécimaux).");
        if (!texteCourt(doc.nom, 60)) throw refus("Le nom de l'admin (60 caractères au plus).");
        let debut = 0;
        let fin = 0;
        if (f2) {
            // le code n'est JAMAIS dans le document signe (ni lui, ni son empreinte) : seulement a cote, son empreinte
            if ("code" in doc || "empreinte" in doc) throw refus("Le code de la dérogation ne va jamais dans le document signé.");
            if (!estHex(corps.code, 64)) throw mal("code", "Le champ « code » doit être l'empreinte du code (64 hexadécimaux minuscules).");
            for (const k of ["duree", "valable", "emise"]) if (!entierPositif(doc[k])) throw refus("« " + k + " » doit être un nombre entier de secondes.");
            if (Math.abs(doc.emise - ctx.now) > ECART_DEBUT)
                throw refus("La dérogation doit être émise maintenant (à 10 minutes près, à l'heure du service) : ni antidatée, ni pour plus tard.");
            if (doc.duree < DUREE_MIN_DEROGATION) throw refus("Une dérogation dure au moins 10 minutes.");
            if (doc.duree > DUREE_MAX_DEROGATION) throw refus("Une dérogation dure 72 heures au plus : jamais « permanente ».");
            const pourTaper = doc.valable - doc.emise;
            if (pourTaper < VALABLE_MIN || pourTaper > VALABLE_MAX) throw refus("Le code se tape dans les 10 minutes à 24 heures qui suivent l'envoi (« valable »).");
            if (doc.valable <= ctx.now) throw refus("L'heure limite pour taper le code est déjà passée.");
        } else {
            for (const k of ["debut", "fin", "emise"]) if (!entierPositif(doc[k])) throw refus("« " + k + " » doit être une heure (secondes Unix).");
            if (Math.abs(doc.debut - ctx.now) > ECART_DEBUT)
                throw refus("Le début de la dérogation doit être maintenant (à 10 minutes près, à l'heure du service) : ni antidatée, ni pour plus tard.");
            const duree = doc.fin - doc.debut;
            if (duree < DUREE_MIN_DEROGATION) throw refus("Une dérogation dure au moins 10 minutes.");
            if (duree > DUREE_MAX_DEROGATION) throw refus("Une dérogation dure 72 heures au plus : jamais « permanente ».");
            debut = doc.debut;
            fin = doc.fin;
        }
        const a = this.admin(doc.admin);
        if (!a) throw new Erreur(404, "admin", "Cette clé n'est pas celle d'un admin de la liste en place.");
        if (a.retire) throw new Erreur(409, "retire", "Cet admin est retiré : il ne peut rien recevoir.");
        const avant = this.derogationGardee();
        const enCours = avant && ["active", "a_venir", "en_attente"].includes(etatDe(avant, ctx.now));
        const remplacee = enCours && avant.id !== doc.id ? avant.id : "";
        const d = {
            format: f2 ? 2 : 1,
            id: doc.id,
            droit: doc.droit,
            admin: doc.admin,
            nom: doc.nom,
            debut,
            fin,
            emise: doc.emise,
            par: cle,
            document: corps.derogation,
            signature: corps.signature,
            recue: ctx.now,
            revoquee: 0,
            usages: 0,
            fin_notee: false,
        };
        if (f2) {
            d.duree = doc.duree;
            d.valable = doc.valable;
            d.code = corps.code; // l'empreinte du code : jamais rendue (vue(), l'etat, la sauvegarde)
            d.essais = 0;
            d.annulee = 0;
            d.raison = "";
            d.lectures = [];
        }
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        return reponseJson(201, { derogation: vue(d, ctx.now), remplacee, heure: ctx.now });
    },

    // POST /admin/derogation/activer (admin) {"id","code"} - 1.52.0 : l'admin tape le code que l'admin+ voit sur son PC.
    async activerDerogation(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.id, 32)) throw mal("id", "Le champ « id » doit être l'identifiant de la dérogation (32 hexadécimaux).");
        if (typeof corps.code !== "string" || !/^[0-9]{8}$/.test(corps.code)) throw mal("code", "Le code fait 8 chiffres (celui que l'admin+ voit sur son PC).");
        if (ctx.appelant.role !== "admin") throw new Erreur(403, "derogation", "Seul l'admin qui reçoit la dérogation tape son code (l'admin+ n'en a pas besoin).");
        await this.noterFinDerogation(ctx.now);
        const d = this.derogationGardee();
        if (!d || d.id !== corps.id || d.admin !== ctx.appelant.cle)
            throw new Erreur(404, "derogation", "Cette dérogation n'est pas (ou plus) la tienne : l'admin+ en a peut-être envoyé une autre.");
        if (d.format !== 2) throw new Erreur(409, "derogation", "Cette dérogation commence sans code.");
        const etat = etatDe(d, ctx.now);
        const juste = typeof d.code === "string" && egalConstant(await empreinteCode(d.id, corps.code), d.code);
        if (etat === "revoquee") throw new Erreur(409, "revoquee", "L'admin+ a révoqué cette dérogation : demande-lui une nouvelle dérogation.");
        if (etat === "annulee") {
            if (raisonDe(d, ctx.now) === "code_faux")
                throw new Erreur(409, "annulee", "Cette dérogation est annulée (5 codes faux) : demande à l'admin+ une nouvelle dérogation.", { raison: "code_faux" });
            throw new Erreur(409, "perime", "Le code n'est plus valable (il se tape dans l'heure qui suit l'envoi) : demande à l'admin+ une nouvelle dérogation.", {
                raison: raisonDe(d, ctx.now) || "code_perime",
            });
        }
        if (etat !== "en_attente") {
            // deja activee : le meme code rend la meme reponse (un appel repete) ; jamais un essai de plus
            if (juste && etat === "active") return reponseJson(200, { derogation: vue(d, ctx.now), heure: ctx.now });
            throw new Erreur(409, etat === "active" ? "deja" : "expiree", etat === "active" ? "Cette dérogation est déjà active." : "Cette dérogation a déjà pris fin.");
        }
        if (typeof d.code !== "string" || !estHex(d.code, 64))
            throw new Erreur(409, "annulee", "Cette dérogation est revenue d'une sauvegarde sans son code : demande à l'admin+ une nouvelle dérogation.");
        if (!juste) {
            d.essais = (d.essais || 0) + 1;
            const reste = Math.max(0, ESSAIS_CODE_DEROGATION - d.essais);
            if (reste === 0) {
                d.annulee = ctx.now;
                d.raison = "code_faux";
            }
            this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
            await this.journalService(ctx.now, "derogation_code_refuse", d.id, d.nom + " a tapé un code faux pour sa dérogation (essai " + d.essais + " sur " + ESSAIS_CODE_DEROGATION + ")", {
                derogation: d.id,
                admin: d.admin,
                nom: d.nom,
                essai: d.essais,
                essais_restants: reste,
            });
            if (reste === 0) {
                await this.journalService(ctx.now, "derogation_annulee", d.id, "la dérogation de " + d.nom + " est annulée : 5 codes faux", {
                    derogation: d.id,
                    admin: d.admin,
                    nom: d.nom,
                    raison: "code_faux",
                });
                throw new Erreur(403, "code", "Code faux : la dérogation est annulée (5 essais). Demande à l'admin+ une nouvelle dérogation.", {
                    essais_restants: 0,
                    raison: "code_faux",
                });
            }
            throw new Erreur(403, "code", "Code faux : il te reste " + reste + (reste > 1 ? " essais." : " essai."), { essais_restants: reste });
        }
        d.debut = ctx.now;
        d.fin = ctx.now + d.duree;
        d.activee = ctx.now;
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        await this.journalService(ctx.now, "derogation_activee", d.id, d.nom + " a tapé le code : sa dérogation court " + dureeTexte(d.duree), {
            derogation: d.id,
            admin: d.admin,
            nom: d.nom,
            debut: d.debut,
            fin: d.fin,
            duree: d.duree,
        });
        return reponseJson(200, { derogation: vue(d, ctx.now), heure: ctx.now });
    },

    // POST /admin/derogation/revoquer (admin+) {"id"} : finie tout de suite (1.52.0 : en attente du code aussi).
    async revoquerDerogation(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.id, 32)) throw mal("id", "Le champ « id » doit être l'identifiant de la dérogation (32 hexadécimaux).");
        const d = this.derogationGardee();
        if (!d || d.id !== corps.id) throw new Erreur(404, "derogation", "Cette dérogation n'est pas celle en place.");
        if (d.revoquee) return reponseJson(200, { derogation: vue(d, ctx.now), heure: ctx.now });
        const etat = etatDe(d, ctx.now);
        if (etat === "annulee") throw new Erreur(409, "annulee", "Cette dérogation est déjà annulée.");
        if (etat === "expiree") throw new Erreur(409, "expiree", "Cette dérogation a déjà pris fin.");
        d.revoquee = ctx.now;
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        return reponseJson(200, { derogation: vue(d, ctx.now), heure: ctx.now });
    },

    // GET /admin/derogation : l'admin+ voit celle en place ; un admin, la sienne seulement.
    async lireDerogation(ctx) {
        await this.noterFinDerogation(ctx.now);
        const d = this.derogationGardee();
        const visible = d && (ctx.appelant.role === "admin+" || d.admin === ctx.appelant.cle);
        return reponseJson(200, { derogation: visible ? vue(d, ctx.now) : null, heure: ctx.now });
    },

    // POST /admin/derogation/utiliser (admin) {"action","detail"} : la derogation de CET admin court-elle ?
    async utiliserDerogation(ctx) {
        const corps = this.corpsJson(ctx);
        if (typeof corps.action !== "string" || !GESTES_DEROGATION.includes(corps.action))
            throw mal("action", "Le champ « action » doit être l'un de : " + GESTES_DEROGATION.join(", ") + ".");
        if (corps.detail !== undefined && !texteCourt(corps.detail, 200)) throw mal("detail", "Le champ « detail » : une ligne de 200 caractères au plus.");
        await this.noterFinDerogation(ctx.now);
        if (ctx.appelant.role === "admin+") return reponseJson(200, { role: "admin+", heure: ctx.now });
        const d = this.derogationDe(ctx.appelant.cle, ctx.now);
        if (!d) throw this.refusDerogation(ctx.appelant.cle, ctx.now);
        await this.noterUsageDerogation(ctx, d, corps.action, corps.detail);
        return reponseJson(200, { derogation: vue(d, ctx.now), heure: ctx.now });
    },

    // Le refus (403 << derogation >>) d'un admin sans derogation en cours : pourquoi, en francais.
    refusDerogation(cle, now) {
        const d = this.derogationGardee();
        let phrase = "L'admin+ seul gère le service : tu n'as pas de dérogation « Gérer le service » en cours.";
        if (d && d.admin === cle) {
            const etat = etatDe(d, now);
            const nom = "Ta " + libelle(d);
            if (etat === "revoquee") phrase = nom + " a été révoquée par l'admin+ : l'admin+ seul gère le service.";
            else if (etat === "expiree") phrase = nom + " a pris fin : l'admin+ seul gère le service (demande-lui une nouvelle dérogation).";
            else if (etat === "a_venir") phrase = nom + " n'a pas encore commencé.";
            else if (etat === "en_attente") phrase = nom + " attend son code : tape le code que l'admin+ voit sur son PC (Mon compte).";
            else if (etat === "annulee") phrase = nom + " est annulée (" + (raisonDe(d, now) === "code_faux" ? "5 codes faux" : "le code n'a pas été tapé à temps") + ") : demande à l'admin+ une nouvelle dérogation.";
        }
        return new Erreur(403, "derogation", phrase, d && d.admin === cle ? { fin: d.fin, revoquee: d.revoquee || 0, etat: etatDe(d, now) } : null);
    },

    // Ce que GET /admin/etat dit de la derogation a qui appelle (l'admin+ : celle en place) ; null : rien.
    async derogationPourEtat(ctx) {
        await this.noterFinDerogation(ctx.now);
        const d = this.derogationGardee();
        if (!d || !(ctx.appelant.role === "admin+" || d.admin === ctx.appelant.cle)) return null;
        const v = vue(d, ctx.now);
        delete v.document;
        delete v.signature;
        return v;
    },
};

// 1.52.0 : la ligne << derogation >> de la table config, telle que la sauvegarde la rend : SANS l'empreinte du code.
export function derogationPourSauvegarde(valeur) {
    try {
        const d = JSON.parse(valeur);
        if (!d || typeof d !== "object") return valeur;
        delete d.code;
        return JSON.stringify(d);
    } catch {
        return valeur;
    }
}
