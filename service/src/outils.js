// =============================================================================
//  service/src/outils.js - les aides du service des licences : hexadecimal,
//  SHA-256, Ed25519 (WebCrypto), dates ISO, reponses JSON, erreurs
// =============================================================================

export const enc = new TextEncoder();
// fatal : un texte qui n'est pas de l'UTF-8 est refuse ; ignoreBOM : un BOM reste dans le texte (rien n'est
// retire en silence d'un document signe).
export const dec = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });

// Les noms des algorithmes WebCrypto (workerd : "Ed25519" standard ; "NODE-ED25519" existe aussi).
export const ED25519 = { name: "Ed25519" };

export function hex(bytes) {
    let s = "";
    for (const b of bytes) s += b.toString(16).padStart(2, "0");
    return s;
}

// Des hexadecimaux (minuscules ou majuscules) -> octets ; null : illisible, ou pas `n` octets.
export function unhex(s, n) {
    if (typeof s !== "string" || s.length % 2 !== 0 || !/^[0-9a-fA-F]*$/.test(s)) return null;
    if (n !== undefined && s.length !== 2 * n) return null;
    const out = new Uint8Array(s.length / 2);
    for (let i = 0; i < out.length; i++) out[i] = parseInt(s.substr(2 * i, 2), 16);
    return out;
}

// Des hexadecimaux minuscules, exactement `n` caracteres.
export function estHex(s, n) {
    return typeof s === "string" && s.length === n && /^[0-9a-f]+$/.test(s);
}

export function concat(...parts) {
    let n = 0;
    for (const p of parts) n += p.length;
    const out = new Uint8Array(n);
    let i = 0;
    for (const p of parts) {
        out.set(p, i);
        i += p.length;
    }
    return out;
}

export async function sha256(data) {
    const bytes = typeof data === "string" ? enc.encode(data) : data;
    return new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
}

export async function sha256Hex(data) {
    return hex(await sha256(data));
}

// Comparer deux textes de meme longueur (des empreintes) sans s'arreter au premier caractere different : le temps
// ne dit rien de la valeur.
export function egalConstant(a, b) {
    if (typeof a !== "string" || typeof b !== "string" || a.length !== b.length) return false;
    let d = 0;
    for (let i = 0; i < a.length; i++) d |= a.charCodeAt(i) ^ b.charCodeAt(i);
    return d === 0;
}

// La verification Ed25519 d'une signature ; faux pour toute cle ou signature illisible.
export async function verifierEd25519(cle, message, signature) {
    try {
        if (!(cle instanceof Uint8Array) || cle.length !== 32 || !(signature instanceof Uint8Array) || signature.length !== 64) return false;
        const k = await crypto.subtle.importKey("raw", cle, ED25519, false, ["verify"]);
        return await crypto.subtle.verify(ED25519, k, signature, message);
    } catch {
        return false;
    }
}

// L'identifiant d'une cle publique : "xpg-" + les 16 premiers hexadecimaux de son SHA-256 (SignedFile.hpp).
export async function cleIdDe(cle) {
    return "xpg-" + (await sha256Hex(cle)).slice(0, 16);
}

// L'identifiant d'un poste : les 32 premiers hexadecimaux de SHA-256("xpg-poste-passerelle/1" || cle publique).
export async function posteDe(cle) {
    return (await sha256Hex(concat(enc.encode("xpg-poste-passerelle/1"), cle))).slice(0, 32);
}

// ---- les dates : "AAAA-MM-JJTHH:MM:SSZ", a la seconde, toujours en UTC (Time.hpp) ----
export function iso(t) {
    return new Date(t * 1000).toISOString().replace(/\.\d{3}Z$/, "Z");
}

// null : pas une date (le C++ refuse tout autre texte : ni fraction, ni decalage).
export function lireIso(s) {
    if (typeof s !== "string") return null;
    const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})Z$/.exec(s);
    if (!m) return null;
    const y = +m[1];
    if (y < 1970 || y > 9999) return null;
    const t = Date.UTC(y, +m[2] - 1, +m[3], +m[4], +m[5], +m[6]) / 1000;
    if (!Number.isFinite(t) || iso(t) !== s) return null; // 2026-02-30, 25:00... refuses
    return t;
}

export function jourDe(t) {
    return iso(t).slice(0, 10);
}

// ---- les erreurs et les reponses ----
// Une erreur rendue au client : {"erreur":"<code>","message":"<phrase>"} (et des champs en plus, ex. reessayer_dans).
export class Erreur extends Error {
    constructor(statut, code, message, extra) {
        super(message);
        this.statut = statut;
        this.code = code;
        this.extra = extra || null;
    }
}

const ENTETES_COMMUNS = {
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
};

export function reponseJson(statut, objet, entetes) {
    return new Response(JSON.stringify(objet), {
        status: statut,
        headers: { "Content-Type": "application/json; charset=utf-8", ...ENTETES_COMMUNS, ...(entetes || {}) },
    });
}

export function reponseTexte(statut, texte) {
    return new Response(texte, { status: statut, headers: { "Content-Type": "text/plain; charset=utf-8", ...ENTETES_COMMUNS } });
}

export function reponseErreur(e) {
    const corps = { erreur: e.code, message: e.message, ...(e.extra || {}) };
    const entetes = e.statut === 429 && e.extra && e.extra.reessayer_dans ? { "Retry-After": String(e.extra.reessayer_dans) } : undefined;
    return reponseJson(e.statut, corps, entetes);
}

// La taille maximale d'un corps (le contrat : 2 Mio).
export const CORPS_MAX = 2 * 1024 * 1024;
