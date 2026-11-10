// =============================================================================
//  service/test/aides.js - les aides des essais du Worker : lancer le VRAI
//  service (outils/service/lancer-local.sh : workerd par wrangler dev), des
//  cles Ed25519 (node:crypto), des blocs signes et des appels signes ecrits
//  ICI, sans le code du Worker (le format de SignedFile.hpp et Service.hpp)
// =============================================================================
import { spawn } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ICI = path.dirname(fileURLToPath(import.meta.url));
export const LANCEUR = path.resolve(ICI, "../../outils/service/lancer-local.sh");

export const sha256Hex = (data) => crypto.createHash("sha256").update(data).digest("hex");
export const hasard = (n) => crypto.randomBytes(n).toString("hex");

// ---- les cles ----
// Une cle de PC (ou de l'editeur) ; `station` : l'empreinte de << son >> installation de Windows (stationHash).
export function nouvelleCle() {
    const { publicKey, privateKey } = crypto.generateKeyPairSync("ed25519");
    const pub = Buffer.from(publicKey.export({ format: "jwk" }).x, "base64url");
    return {
        pub,
        hex: pub.toString("hex"),
        station: crypto.randomBytes(16).toString("hex"),
        id: "xpg-" + sha256Hex(pub).slice(0, 16),
        poste: sha256Hex(Buffer.concat([Buffer.from("xpg-poste-passerelle/1"), pub])).slice(0, 32),
        signer: (m) => crypto.sign(null, Buffer.from(m), privateKey),
    };
}

export function verifierSignature(pubHex, message, sig) {
    const key = crypto.createPublicKey({ key: { kty: "OKP", crv: "Ed25519", x: Buffer.from(pubHex, "hex").toString("base64url") }, format: "jwk" });
    return crypto.verify(null, Buffer.from(message), key, sig);
}

// ---- les blocs signes ----
export function bloc(genre, json, cle) {
    const doc = Buffer.from(json, "utf8");
    const sig = cle.signer(Buffer.concat([Buffer.from("XPG-LICENSING-1\n" + genre + "\n"), doc]));
    return "XPG-LICENSING 1 " + genre + " " + doc.length + "\n" + json + "SIGNATURE ed25519 " + cle.id + " " + sig.toString("hex") + "\n";
}

// Lire le premier bloc d'un texte : {genre, json, cleId, signature, reste}.
export function lireBloc(texte) {
    const b = Buffer.from(texte, "utf8");
    const eol = b.indexOf(10);
    const m = /^XPG-LICENSING 1 ([a-z]+) (\d+)$/.exec(b.subarray(0, eol).toString());
    if (!m) throw new Error("en-tete illisible : " + texte.slice(0, 60));
    const n = Number(m[2]);
    const doc = b.subarray(eol + 1, eol + 1 + n);
    const rest = b.subarray(eol + 1 + n);
    const eol2 = rest.indexOf(10);
    const s = /^SIGNATURE ed25519 (xpg-[0-9a-f]{16}) ([0-9a-f]{128})$/.exec(rest.subarray(0, eol2).toString());
    if (!s) throw new Error("signature illisible");
    return { genre: m[1], json: doc.toString("utf8"), octets: doc, cleId: s[1], signature: Buffer.from(s[2], "hex"), reste: rest.subarray(eol2 + 1).toString() };
}

// Le bloc est-il signe par cette cle (identifiant et signature) ?
export function blocSignePar(texte, pubHex) {
    const b = lireBloc(texte);
    const id = "xpg-" + sha256Hex(Buffer.from(pubHex, "hex")).slice(0, 16);
    return b.cleId === id && verifierSignature(pubHex, Buffer.concat([Buffer.from("XPG-LICENSING-1\n" + b.genre + "\n"), b.octets]), b.signature);
}

export const iso = (t) => new Date(t * 1000).toISOString().replace(/\.\d{3}Z$/, "Z");
export const DATE_ISO = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/;

// ---- les documents de l'editeur et du PC (le JSON des Studios : Documents.cpp, deux espaces) ----
export function licenceJson({ id, type = "temporary", max = 2, emission, expire, devices = [], service = "http://127.0.0.1:8787" }) {
    return (
        JSON.stringify(
            {
                format: "xpg-license",
                version: 1,
                license_id: id,
                customer: "Client d'essai",
                edition: "PROFESSIONAL",
                type,
                service,
                max_devices: max,
                features: [{ id: "ai_generation" }],
                issued_at: iso(emission),
                expires_at: expire ? iso(expire) : null,
                offline_grace_days: 0,
                devices,
                manifest: 1,
                nonce: hasard(16),
            },
            null,
            2,
        ) + "\n"
    );
}

// `station` : celle de la cle par defaut ; null : aucune (une demande d'avant la 1.35.0).
export function demandeJson(cle, { nom = "PC d'essai", station = cle.station } = {}) {
    const o = {
        format: "xpg-activation-request",
        version: 1,
        device_key: cle.hex,
        device_name: nom,
        license_id: "",
        software: "IHM Studio 1.35.0",
        created_at: iso(Math.floor(Date.now() / 1000)),
        nonce: hasard(16),
    };
    if (station) o.station = station;
    return JSON.stringify(o, null, 2) + "\n";
}

// Le code : XPG-XXXX-XXXX-XXXX-XXXX (Crockford) et son empreinte.
const CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
export function nouveauCode() {
    let n = "";
    for (const b of crypto.randomBytes(16)) n += CROCKFORD[b & 31];
    return { code: "XPG-" + n.match(/.{4}/g).join("-"), norm: n, hash: sha256Hex("xpg-code/1|" + n) };
}

// ---- le service local ----
// Lance lancer-local.sh (un port libre, un dossier d'etat) ; {base, arreter(), etat}.
// 1.46.0 : `stripe` {cle, webhook, base} - un FAUX Stripe (tests/outils/faux_stripe.mjs) : des valeurs essai_jeton_...
export async function lancerService({ cles = [], decalage = 0, jours, etat, sansLimites = false, stripe } = {}) {
    const dossier = etat || fs.mkdtempSync(path.join(os.tmpdir(), "xpg-service-essai-"));
    const env = { ...process.env, XPG_DECALAGE_HORLOGE_ESSAI: String(decalage) };
    if (jours) env.XPG_DECOUVERTE_JOURS = String(jours);
    if (sansLimites) env.XPG_LIMITES_ESSAI = "sans";
    for (const k of ["XPG_STRIPE_SECRET_KEY", "XPG_STRIPE_WEBHOOK_SECRET", "XPG_STRIPE_API_BASE"]) delete env[k];
    if (stripe && stripe.cle) env.XPG_STRIPE_SECRET_KEY = stripe.cle;
    if (stripe && stripe.webhook) env.XPG_STRIPE_WEBHOOK_SECRET = stripe.webhook;
    if (stripe && stripe.base) env.XPG_STRIPE_API_BASE = stripe.base;
    const p = spawn("bash", [LANCEUR, "0", dossier, cles.join(",")], { env, detached: true, stdio: ["ignore", "pipe", "pipe"] });
    let sortie = "";
    let erreurs = "";
    p.stderr.on("data", (d) => (erreurs += d));
    const port = await new Promise((resolve, reject) => {
        const minuteur = setTimeout(() => reject(new Error("le service n'a pas demarre en 60 s : " + erreurs)), 60000);
        p.stdout.on("data", (d) => {
            sortie += d;
            const m = /service pret sur le port (\d+)/.exec(sortie);
            if (m) {
                clearTimeout(minuteur);
                resolve(Number(m[1]));
            }
        });
        p.on("exit", (code) => {
            clearTimeout(minuteur);
            reject(new Error("le lanceur s'est arrete (code " + code + ") : " + erreurs));
        });
    });
    const base = "http://127.0.0.1:" + port;
    const etatService = await (await fetch(base + "/v1/etat")).json();
    return {
        base,
        etat: dossier,
        // le decalage entre l'heure du service et celle de ce programme
        ecart: etatService.heure - Math.floor(Date.now() / 1000),
        async arreter() {
            await new Promise((resolve) => {
                if (p.exitCode !== null) return resolve();
                p.once("exit", resolve);
                try {
                    process.kill(-p.pid, "SIGTERM");
                } catch {
                    resolve();
                }
                setTimeout(() => {
                    try {
                        process.kill(-p.pid, "SIGKILL");
                    } catch {
                        // deja parti
                    }
                }, 5000).unref();
            });
        },
    };
}

// Toutes les reponses recues (pour verifier qu'aucun secret n'y passe).
export const reponses = [];

// Un appel ; signe si `cle` est donnee. `ip` : CF-Connecting-IP (une neuve a chaque appel par defaut : les limites
// par adresse ne genent pas les essais qui ne les visent pas). 1.44.0 : `corps` peut etre des octets (un Buffer : un
// morceau de fichier, envoye tel quel), `entetes` des en-tetes de plus (X-XPG-Logiciel, X-XPG-Jeton), `binaire` :
// la reponse est aussi rendue en octets (`octets`).
export async function appel(svc, methode, chemin, { corps, cle, date, nonce, ip, signeChemin, signeCorps, signature, entetes: plus, binaire } = {}) {
    const octets = Buffer.isBuffer(corps) ? corps : null;
    const texte = octets ? "" : corps === undefined ? "" : typeof corps === "string" ? corps : JSON.stringify(corps);
    const entetes = { "CF-Connecting-IP": ip || "198.51." + crypto.randomInt(0, 256) + "." + crypto.randomInt(1, 255), ...(plus || {}) };
    if (texte) entetes["Content-Type"] = "application/json";
    if (octets) entetes["Content-Type"] = "application/octet-stream";
    if (cle) {
        const d = date !== undefined ? String(date) : String(Math.floor(Date.now() / 1000) + svc.ecart);
        const n = nonce || hasard(16);
        const message =
            "XPG-SERVICE-1\n" + methode + "\n" + (signeChemin || chemin) + "\n" + d + "\n" + n + "\n" + sha256Hex(signeCorps !== undefined ? signeCorps : octets || texte);
        entetes["X-XPG-Cle"] = cle.hex;
        entetes["X-XPG-Date"] = d;
        entetes["X-XPG-Nonce"] = n;
        entetes["X-XPG-Signature"] = signature || cle.signer(message).toString("hex");
    }
    const r = await fetch(svc.base + chemin, { method: methode, headers: entetes, body: octets || texte || undefined });
    const recu = Buffer.from(await r.arrayBuffer());
    const brut = binaire ? "" : recu.toString("utf8");
    reponses.push(brut);
    let json = null;
    try {
        json = JSON.parse(binaire ? recu.toString("utf8") : brut);
    } catch {
        json = null;
    }
    return { statut: r.status, json, texte: brut, type: r.headers.get("content-type") || "", entetes: r.headers, octets: binaire ? recu : null };
}

// Le certificat de service, signe par l'editeur.
export function certificat(editeur, cleService, { emis, expire, droits = ["activation", "flottante", "heure", "decouverte"] } = {}) {
    const now = Math.floor(Date.now() / 1000);
    const json = JSON.stringify({
        version: 1,
        cle: cleService,
        url: "http://127.0.0.1:8787",
        emis: iso(emis || now - 60),
        expire: iso(expire || now + 2 * 365 * 86400),
        droits,
    });
    return bloc("service", json, editeur);
}

// La meme cle de PC recopiee sur une autre installation de Windows (une autre station).
export const copieAilleurs = (cle) => ({ ...cle, station: crypto.randomBytes(16).toString("hex") });

// Les lignes ecrites par un appel (le service les dit pres de 127.0.0.1 : X-XPG-Lignes-Ecrites) ; -1 : non dit.
export const lignesEcrites = (r) => {
    const v = r.entetes.get("x-xpg-lignes-ecrites");
    return v === null ? -1 : Number(v);
};
