// =============================================================================
//  service/src/blocs.js - les blocs signes (le meme format que
//  src/licensing/SignedFile.hpp, octet pour octet)
// -----------------------------------------------------------------------------
//      XPG-LICENSING 1 <genre> <taille>\n
//      <exactement <taille> octets : le JSON>
//      SIGNATURE ed25519 <xpg-16 hexa> <128 hexa>\n
//  Le message signe : "XPG-LICENSING-1\n<genre>\n" suivi des octets du JSON.
//  Un document recu n'est jamais reecrit : ses octets sont gardes tels quels.
// =============================================================================
import { enc, dec, hex, unhex, concat, verifierEd25519 } from "./outils.js";

const MAGIC = "XPG-LICENSING 1 ";
const SIGNATURE = "SIGNATURE ed25519 ";
const CONTEXTE = "XPG-LICENSING-1\n";

// Les genres connus et la taille maximale de leur JSON (docKindMaxBytes ; les genres << passerelle >> et
// << annulation >> de la 1.34 ont disparu en 1.35.0).
const GENRES = {
    licence: 256 * 1024,
    registre: 2 * 1024 * 1024,
    revocations: 8 * 1024 * 1024,
    demande: 16 * 1024,
    poste: 512 * 1024,
    service: 4 * 1024,
    activation: 4 * 1024,
    jeton: 2 * 1024,
    heure: 1024,
    decouverte: 2 * 1024,
};
const FICHIER_MAX = 16 * 1024 * 1024;
const BLOCS_MAX = 8;

const MAGIC_B = enc.encode(MAGIC);
const SIGNATURE_B = enc.encode(SIGNATURE);

function commencePar(b, pos, motif) {
    if (b.length - pos < motif.length) return false;
    for (let i = 0; i < motif.length; i++) if (b[pos + i] !== motif[i]) return false;
    return true;
}

function ascii(b) {
    let s = "";
    for (const c of b) s += String.fromCharCode(c);
    return s;
}

export class BlocIllisible extends Error {}

// Lire les blocs d'un texte : [{genre, octets (le JSON), json (texte), cleId, signature}]. Rien n'est verifie ici
// que la forme (comme readBlocks). Leve BlocIllisible.
export function lireBlocs(texte) {
    if (typeof texte !== "string") throw new BlocIllisible("ce n'est pas un texte");
    const b = enc.encode(texte);
    if (b.length > FICHIER_MAX) throw new BlocIllisible("le fichier est trop gros");
    let pos = 0;
    if (b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf) pos = 3;
    const out = [];
    for (;;) {
        while (pos < b.length && (b[pos] === 10 || b[pos] === 13 || b[pos] === 32 || b[pos] === 9)) pos++;
        if (pos >= b.length) break;
        if (out.length >= BLOCS_MAX) throw new BlocIllisible("le fichier contient trop de documents");
        if (!commencePar(b, pos, MAGIC_B)) throw new BlocIllisible(out.length ? "du texte inattendu suit un document" : "ce n'est pas un fichier signé XPG (en-tête absent)");
        let eol = b.indexOf(10, pos);
        if (eol < 0 || eol - pos > 128) throw new BlocIllisible("l'en-tête est incomplet");
        let ligne = ascii(b.subarray(pos, eol));
        pos = eol + 1;
        if (ligne.endsWith("\r")) ligne = ligne.slice(0, -1);
        ligne = ligne.slice(MAGIC.length);
        const sp = ligne.indexOf(" ");
        if (sp < 0) throw new BlocIllisible("l'en-tête est illisible");
        const genre = ligne.slice(0, sp);
        const taille = ligne.slice(sp + 1);
        if (!Object.prototype.hasOwnProperty.call(GENRES, genre)) throw new BlocIllisible("genre de document inconnu");
        if (!/^\d{1,9}$/.test(taille)) throw new BlocIllisible("la taille est illisible");
        const n = Number(taille);
        if (n > GENRES[genre]) throw new BlocIllisible("le document est trop gros");
        if (b.length - pos < n) throw new BlocIllisible("le document est tronqué");
        const octets = b.slice(pos, pos + n);
        pos += n;
        if (!commencePar(b, pos, SIGNATURE_B)) throw new BlocIllisible("la signature manque (document modifié ou tronqué)");
        eol = b.indexOf(10, pos);
        if (eol < 0 || eol - pos > 256) throw new BlocIllisible("la signature est incomplète");
        ligne = ascii(b.subarray(pos, eol));
        pos = eol + 1;
        if (ligne.endsWith("\r")) ligne = ligne.slice(0, -1);
        ligne = ligne.slice(SIGNATURE.length);
        const sp2 = ligne.indexOf(" ");
        const cleId = sp2 < 0 ? "" : ligne.slice(0, sp2);
        if (!/^xpg-[0-9a-f]{16}$/.test(cleId)) throw new BlocIllisible("la signature est illisible");
        const signature = unhex(ligne.slice(sp2 + 1), 64);
        if (!signature) throw new BlocIllisible("la signature est illisible");
        let json;
        try {
            json = dec.decode(octets);
        } catch {
            throw new BlocIllisible("le document n'est pas de l'UTF-8");
        }
        out.push({ genre, octets, json, cleId, signature });
    }
    if (!out.length) throw new BlocIllisible("le fichier est vide");
    return out;
}

// Le premier bloc du genre `genre` d'un texte ; leve BlocIllisible.
export function trouverBloc(texte, genre) {
    const b = lireBlocs(texte).find((x) => x.genre === genre);
    if (!b) throw new BlocIllisible("il n'y a pas de bloc « " + genre + " »");
    return b;
}

export function messageSigne(genre, octets) {
    return concat(enc.encode(CONTEXTE + genre + "\n"), octets);
}

// Le texte d'un bloc lu (normalise : fins de ligne \n ; le JSON et la signature tels quels).
export function ecrireBlocLu(bloc) {
    return MAGIC + bloc.genre + " " + bloc.octets.length + "\n" + bloc.json + SIGNATURE + bloc.cleId + " " + hex(bloc.signature) + "\n";
}

// Signer un document : `signer(message)` rend les 64 octets de la signature.
export async function signerBloc(genre, json, cleId, signer) {
    const octets = enc.encode(json);
    const signature = await signer(messageSigne(genre, octets));
    return MAGIC + genre + " " + octets.length + "\n" + json + SIGNATURE + cleId + " " + hex(signature) + "\n";
}

// La signature d'un bloc est-elle celle de cette cle (identifiant et signature) ?
export async function verifierBloc(bloc, cle, cleId) {
    if (bloc.cleId !== cleId) return false;
    return verifierEd25519(cle, messageSigne(bloc.genre, bloc.octets), bloc.signature);
}
