// =============================================================================
//  service/src/documents.js - lire les documents signes par l'EDITEUR ou par un
//  PC (licence, registre, revocations, certificat de service, etat d'un poste,
//  demande d'activation) : seulement les champs dont le service a besoin, et
//  leurs bornes (le lecteur strict reste celui des Studios, Documents.cpp)
// =============================================================================
import { estHex, lireIso } from "./outils.js";

export class DocumentIllisible extends Error {}

function objet(json, quoi) {
    let v;
    try {
        v = JSON.parse(json);
    } catch {
        throw new DocumentIllisible(quoi + " n'est pas un JSON lisible");
    }
    if (!v || typeof v !== "object" || Array.isArray(v)) throw new DocumentIllisible(quoi + " n'est pas un objet JSON");
    return v;
}

function entier(v, min, max) {
    return typeof v === "number" && Number.isInteger(v) && v >= min && v <= max;
}

function format(v, attendu, quoi) {
    if (v.format !== attendu) throw new DocumentIllisible(quoi + " n'est pas du bon genre (format)");
    if (!entier(v.version, 1, 1)) throw new DocumentIllisible(quoi + " n'est pas de la version 1");
}

function date(v, champ, quoi, nullable) {
    if (nullable && v[champ] === null) return 0;
    const t = lireIso(v[champ]);
    if (t === null) throw new DocumentIllisible(quoi + " : la date « " + champ + " » est illisible");
    return t;
}

// [A-Za-z0-9._-]{1,64} (isLicenseId)
export function estIdLicence(s) {
    return typeof s === "string" && /^[A-Za-z0-9._-]{1,64}$/.test(s);
}

export const TYPES_LICENCE = ["perpetual", "temporary", "evaluation", "floating"];

// La licence en ligne (xpg-license) : {id, type, postesMax, emission, expire (0 : jamais)}.
export function lireLicence(json) {
    const quoi = "la licence";
    const v = objet(json, quoi);
    format(v, "xpg-license", quoi);
    if (!estIdLicence(v.license_id)) throw new DocumentIllisible(quoi + " : l'identifiant est mal écrit");
    if (!TYPES_LICENCE.includes(v.type)) throw new DocumentIllisible(quoi + " : le type est inconnu");
    if (!entier(v.max_devices, 1, 100000)) throw new DocumentIllisible(quoi + " : le nombre de PC est illisible");
    // Une licence EN LIGNE (Documents.hpp, 1.35.0) : l'adresse du service, et aucun PC lie (ses PC s'activent ici).
    if (typeof v.service !== "string" || !v.service)
        throw new DocumentIllisible(quoi + " n'est pas une licence en ligne (le champ « service » est vide) : le service ne tient que les licences en ligne");
    if (!Array.isArray(v.devices) || v.devices.length) throw new DocumentIllisible(quoi + " lie des PC : une licence en ligne n'en lie aucun");
    const emission = date(v, "issued_at", quoi, false);
    const expire = date(v, "expires_at", quoi, true);
    if (expire && expire <= emission) throw new DocumentIllisible(quoi + " : elle finit avant d'avoir commencé");
    return { id: v.license_id, type: v.type, postesMax: v.max_devices, emission, expire };
}

// Le registre (xpg-registry) : {numero}.
export function lireRegistre(json) {
    const quoi = "le registre";
    const v = objet(json, quoi);
    format(v, "xpg-registry", quoi);
    if (!entier(v.number, 1, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : le numéro est illisible");
    date(v, "issued_at", quoi, false);
    return { numero: v.number };
}

// La liste de revocation (xpg-revocations) : {numero, revoquees: [{licence, poste ("" : toute la licence), avant (0 : toutes les emissions)}]}.
export function lireRevocations(json) {
    const quoi = "la liste de révocation";
    const v = objet(json, quoi);
    format(v, "xpg-revocations", quoi);
    if (!entier(v.number, 1, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : le numéro est illisible");
    date(v, "issued_at", quoi, false);
    if (!Array.isArray(v.revoked) || v.revoked.length > 200000) throw new DocumentIllisible(quoi + " : la liste est illisible");
    const revoquees = v.revoked.map((r) => {
        if (!r || typeof r !== "object" || !estIdLicence(r.license_id)) throw new DocumentIllisible(quoi + " : une entrée est illisible");
        if (r.device !== undefined && !estHex(r.device, 64)) throw new DocumentIllisible(quoi + " : une clé de PC est illisible");
        const avant = r.issued_before === undefined ? 0 : lireIso(r.issued_before);
        if (avant === null) throw new DocumentIllisible(quoi + " : une date issued_before est illisible");
        return { licence: r.license_id, poste: r.device || "", avant };
    });
    return { numero: v.number, revoquees };
}

// La revocation qui touche cette emission d'une licence sur ce PC (RevocationList::find), ou null.
export function trouverRevocation(liste, licence, poste, emission) {
    if (!liste) return null;
    for (const r of liste.revoquees)
        if (r.licence === licence && (r.poste === "" || r.poste === poste) && (r.avant === 0 || emission < r.avant)) return r;
    return null;
}

// Le certificat de service (genre "service") : {cle, url, emis, expire, droits}.
export function lireCertificat(json) {
    const quoi = "le certificat du service";
    const v = objet(json, quoi);
    if (!entier(v.version, 1, 1)) throw new DocumentIllisible(quoi + " n'est pas de la version 1");
    if (!estHex(v.cle, 64)) throw new DocumentIllisible(quoi + " : la clé est illisible");
    if (typeof v.url !== "string" || !v.url || v.url.length > 220) throw new DocumentIllisible(quoi + " : l'adresse est illisible");
    const emis = date(v, "emis", quoi, false);
    const expire = date(v, "expire", quoi, false);
    if (expire <= emis) throw new DocumentIllisible(quoi + " : il expire avant d'être émis");
    if (!Array.isArray(v.droits) || v.droits.length > 16 || !v.droits.every((d) => typeof d === "string" && d.length > 0 && d.length <= 32))
        throw new DocumentIllisible(quoi + " : les droits sont illisibles");
    return { cle: v.cle, url: v.url, emis, expire, droits: v.droits };
}

// L'etat d'un PC (genre "poste", 1.34) : {poste (32 hexa), numero}.
export function lirePoste(json) {
    const quoi = "l'état du PC";
    const v = objet(json, quoi);
    if (!entier(v.version, 1, 1)) throw new DocumentIllisible(quoi + " n'est pas de la version 1");
    if (!estHex(v.poste, 32)) throw new DocumentIllisible(quoi + " : l'identifiant du poste est illisible");
    if (!entier(v.numero, 0, Number.MAX_SAFE_INTEGER)) throw new DocumentIllisible(quoi + " : le numéro est illisible");
    date(v, "emis", quoi, false);
    return { poste: v.poste, numero: v.numero };
}

// La demande d'activation (xpg-activation-request, signee par le PC) : {cle, nom, station}.
export function lireDemande(json) {
    const quoi = "la demande d'activation";
    const v = objet(json, quoi);
    format(v, "xpg-activation-request", quoi);
    if (!estHex(v.device_key, 64)) throw new DocumentIllisible(quoi + " : la clé du PC est illisible");
    if (typeof v.device_name !== "string" || v.device_name.length > 100) throw new DocumentIllisible(quoi + " : le nom du PC est illisible");
    if (v.station !== undefined && !estHex(v.station, 32)) throw new DocumentIllisible(quoi + " : l'empreinte du poste est illisible");
    date(v, "created_at", quoi, false);
    return { cle: v.device_key, nom: v.device_name, station: v.station || "" };
}

// Le code d'activation lu avec souplesse (normalizeCode) : les 16 caracteres normalises, ou null.
const CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
export function normaliserCode(texte) {
    if (typeof texte !== "string" || texte.length > 64) return null;
    let s = "";
    for (let c of texte) {
        if (c === " " || c === "-" || c === "\t") continue;
        if (c >= "a" && c <= "z") c = c.toUpperCase();
        s += c;
    }
    if (s.length === 19 && s.startsWith("XPG")) s = s.slice(3);
    if (s.length !== 16) return null;
    let out = "";
    for (let c of s) {
        if (c === "O") c = "0";
        else if (c === "I" || c === "L") c = "1";
        if (!CROCKFORD.includes(c)) return null;
        out += c;
    }
    return out;
}
