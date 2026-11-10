// =============================================================================
//  service/src/derogations.js - 1.47.0 : LA DEROGATION << Gerer le service >>
//  (docs/ADMINS.md, § 11)
// -----------------------------------------------------------------------------
//  Gerer le service (le mettre en ligne ou a jour, changer son adresse, brancher
//  Stripe, le releve du jour) est a l'admin+ seul. Il peut le DONNER a UN admin
//  pour un temps COURT : une derogation signee par la cle de l'editeur,
//  de 10 minutes a 72 heures (jamais << permanente >>), revocable avant la fin.
//
//  LE DOCUMENT (signe par une cle de l'editeur ; la signature porte sur
//  "XPG-DEROGATION-1\n" + le texte, comme le journal) :
//      {"format":1,"id":"<32 hexa>","droit":"service","admin":"<64 hexa : sa cle>",
//       "nom":"Lea Essai","debut":T,"fin":T,"emise":T}
//  Une seule derogation a la fois (une nouvelle remplace l'ancienne). Elle est
//  gardee dans la table config (une ligne << derogation >>) : elle part avec la
//  sauvegarde et revient avec la restauration, sans table neuve (schema 5).
//
//  LE SERVICE EN EST L'ARBITRE, A SON HEURE :
//    PUT  /admin/derogation           (admin+)  {"derogation":"<texte>","signature":"<128 hexa>"}
//    POST /admin/derogation/revoquer  (admin+)  {"id"}
//    GET  /admin/derogation           (admin+ : la derogation en cours ; un admin : la sienne seulement)
//    POST /admin/derogation/utiliser  (admin)   {"action","detail"} : l'Admin le demande AVANT chaque geste qui
//         gere le service (deployer, changer l'adresse, brancher Stripe...) ; 200 tant qu'elle court, 403
//         << derogation >> sinon (finie, revoquee, pas la sienne) ; une ligne << derogation_utilisee >> au journal
//         (le service l'ecrit, signee par SA cle : l'admin+ est prevenu).
//    Une route reservee a l'admin+ marquee << derogation >> (licences.js : POST /admin/stats/releve) s'ouvre aussi a
//    l'admin qui a une derogation en cours (et le journal le dit).
//  La fin : une ligne << derogation_expiree >> (le service, une fois), au premier appel qui la voit finie (GET
//  /admin/etat, que chaque Admin ouvert relit toutes les 15 s). Accorder et revoquer : les lignes de l'Admin de
//  l'admin+ (derogation_accorder, derogation_revoquer, signees par la cle de l'editeur).
//  Mettre le Worker en ligne se fait chez Cloudflare (wrangler), avec le compte Cloudflare de l'admin lui-meme
//  (membre du compte de l'editeur) : jamais un jeton partage ; le service ne le voit pas, d'ou << utiliser >>.
// =============================================================================
import { Erreur, enc, estHex, mal, reponseJson, unhex, verifierEd25519 } from "./outils.js";

export const CONFIG_DEROGATION = "derogation";
export const PREFIXE_DEROGATION = "XPG-DEROGATION-1\n";
export const DUREE_MIN_DEROGATION = 600; // 10 minutes
export const DUREE_MAX_DEROGATION = 72 * 3600; // 72 heures, jamais plus
const ECART_DEBUT = 600; // le debut a +-10 min de l'heure du service
const MAX_DOCUMENT = 2048;
const MAX_LIGNES_USAGE = 200; // les lignes << derogation_utilisee >> d'une derogation, au plus
// Ce qu'un Admin dit faire avec sa derogation (la ligne du journal le nomme).
export const GESTES_DEROGATION = ["deployer", "connexion_cloudflare", "adresse", "stripe", "secours", "releve", "verifier"];

function texteCourt(v, max) {
    return typeof v === "string" && !/[\u0000-\u001f\u007f]/.test(v) && Array.from(v).length <= max;
}

// La vue publique d'une derogation gardee (ce que rendent les routes et GET /admin/etat).
function vue(d, now) {
    const etat = d.revoquee ? "revoquee" : now >= d.fin ? "expiree" : now < d.debut ? "a_venir" : "active";
    return {
        id: d.id,
        admin: d.admin,
        nom: d.nom,
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

    // La derogation EN COURS de cette cle d'admin (a l'heure du service), ou null.
    derogationDe(cle, now) {
        const d = this.derogationGardee();
        if (!d || d.admin !== cle || d.revoquee || now < d.debut || now >= d.fin) return null;
        return d;
    },

    // Finie sans revocation et pas encore dite : une ligne << derogation_expiree >> (une fois).
    async noterFinDerogation(now) {
        const d = this.derogationGardee();
        if (!d || d.revoquee || d.fin_notee || now < d.fin) return;
        d.fin_notee = true;
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        await this.journalService(now, "derogation_expiree", d.id, "la dérogation « Gérer le service » de " + d.nom + " a pris fin", {
            derogation: d.id,
            admin: d.admin,
            nom: d.nom,
            fin: d.fin,
            usages: d.usages || 0,
        });
    },

    // Le droit << Gerer le service >> utilise (une route ou un geste de l'Admin) : la ligne du journal (bornee).
    async noterUsageDerogation(ctx, d, geste, detail) {
        d.usages = (d.usages || 0) + 1;
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        if (d.usages > MAX_LIGNES_USAGE) return;
        await this.journalService(ctx.now, "derogation_utilisee", d.id, d.nom + " a utilisé sa dérogation « Gérer le service » : " + geste, {
            derogation: d.id,
            admin: d.admin,
            nom: d.nom,
            geste,
            detail: detail || "",
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
        if (!doc || typeof doc !== "object" || Array.isArray(doc) || doc.format !== 1) throw refus("La dérogation est illisible (format 1 attendu).");
        if (doc.droit !== "service") throw refus("Une dérogation ne donne que « Gérer le service » (droit « service »).");
        if (!estHex(doc.id, 32)) throw refus("L'identifiant de la dérogation doit faire 32 hexadécimaux minuscules.");
        if (!estHex(doc.admin, 64)) throw refus("La dérogation doit nommer la clé de l'admin (64 hexadécimaux).");
        if (!texteCourt(doc.nom, 60)) throw refus("Le nom de l'admin (60 caractères au plus).");
        for (const k of ["debut", "fin", "emise"])
            if (typeof doc[k] !== "number" || !Number.isInteger(doc[k]) || doc[k] < 0) throw refus("« " + k + " » doit être une heure (secondes Unix).");
        if (Math.abs(doc.debut - ctx.now) > ECART_DEBUT)
            throw refus("Le début de la dérogation doit être maintenant (à 10 minutes près, à l'heure du service) : ni antidatée, ni pour plus tard.");
        const duree = doc.fin - doc.debut;
        if (duree < DUREE_MIN_DEROGATION) throw refus("Une dérogation dure au moins 10 minutes.");
        if (duree > DUREE_MAX_DEROGATION) throw refus("Une dérogation dure 72 heures au plus : jamais « permanente ».");
        const a = this.admin(doc.admin);
        if (!a) throw new Erreur(404, "admin", "Cette clé n'est pas celle d'un admin de la liste en place.");
        if (a.retire) throw new Erreur(409, "retire", "Cet admin est retiré : il ne peut rien recevoir.");
        const avant = this.derogationGardee();
        const remplacee = avant && !avant.revoquee && ctx.now < avant.fin && avant.id !== doc.id ? avant.id : "";
        const d = {
            id: doc.id,
            admin: doc.admin,
            nom: doc.nom,
            debut: doc.debut,
            fin: doc.fin,
            emise: doc.emise,
            par: cle,
            document: corps.derogation,
            signature: corps.signature,
            recue: ctx.now,
            revoquee: 0,
            usages: 0,
            fin_notee: false,
        };
        this.ecrireConfig(CONFIG_DEROGATION, JSON.stringify(d));
        return reponseJson(201, { derogation: vue(d, ctx.now), remplacee, heure: ctx.now });
    },

    // POST /admin/derogation/revoquer (admin+) {"id"} : finie tout de suite.
    async revoquerDerogation(ctx) {
        const corps = this.corpsJson(ctx);
        if (!estHex(corps.id, 32)) throw mal("id", "Le champ « id » doit être l'identifiant de la dérogation (32 hexadécimaux).");
        const d = this.derogationGardee();
        if (!d || d.id !== corps.id) throw new Erreur(404, "derogation", "Cette dérogation n'est pas celle en place.");
        if (d.revoquee) return reponseJson(200, { derogation: vue(d, ctx.now), heure: ctx.now });
        if (ctx.now >= d.fin) throw new Erreur(409, "expiree", "Cette dérogation a déjà pris fin.");
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
            if (d.revoquee) phrase = "Ta dérogation « Gérer le service » a été révoquée par l'admin+ : l'admin+ seul gère le service.";
            else if (now >= d.fin) phrase = "Ta dérogation « Gérer le service » a pris fin : l'admin+ seul gère le service (demande-lui une nouvelle dérogation).";
            else if (now < d.debut) phrase = "Ta dérogation « Gérer le service » n'a pas encore commencé.";
        }
        return new Erreur(403, "derogation", phrase, d && d.admin === cle ? { fin: d.fin, revoquee: d.revoquee || 0 } : null);
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
