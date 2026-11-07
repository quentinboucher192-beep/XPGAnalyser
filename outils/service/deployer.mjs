// =============================================================================
//  outils/service/deployer.mjs - 1.35.0 : mettre en ligne le service des
//  licences sur le compte Cloudflare de l'editeur (deployer.bat sous Windows,
//  deployer.sh sous Linux et macOS l'appellent ; voir LISEZ-MOI-SERVICE.txt)
// -----------------------------------------------------------------------------
//  node deployer.mjs [--cle <hexa>] [--service <dossier>] [--essai]
//    1. trouve le dossier du service (wrangler.toml) : a cote de ce fichier,
//       dans service/ a cote, ou ../../service (le depot) ;
//    2. lit la cle PUBLIQUE de l'editeur : --cle, sinon cle-publique.txt (a cote
//       de ce fichier, ou dans le dossier du service), sinon la demande ;
//    3. installe wrangler s'il manque (npm ci, la version du package-lock.json) ;
//    4. lance `wrangler deploy --var PUBLISHER_KEYS:<cle>` (la premiere fois,
//       wrangler ouvre le navigateur pour te connecter a Cloudflare) ;
//    5. ecrit l'adresse du service (https://xpg-licences.<compte>.workers.dev)
//       et la garde dans adresse-du-service.txt.
//  --essai : tout, sauf l'envoi a Cloudflare (wrangler deploy --dry-run).
//  AUCUN SECRET : la cle donnee au service est la partie PUBLIQUE ; la cle
//  privee de l'editeur ne quitte jamais son outil.
// =============================================================================
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import readline from "node:readline/promises";
import { fileURLToPath } from "node:url";

const ICI = path.dirname(fileURLToPath(import.meta.url));
const WINDOWS = process.platform === "win32";

function arreter(message) {
    console.error("");
    console.error("ERREUR : " + message);
    process.exit(1);
}

function argument(nom) {
    const i = process.argv.indexOf(nom);
    return i > 0 && i + 1 < process.argv.length ? process.argv[i + 1] : null;
}

// Les cles publiques (64 hexadecimaux) d'un texte : en minuscules, sans doublon, separees par des virgules.
function clesDe(texte) {
    const vues = [];
    for (const ligne of String(texte).split(/\r?\n/)) {
        if (/^\s*#/.test(ligne)) continue; // un commentaire
        for (const m of ligne.matchAll(/(?<![0-9a-fA-F])[0-9a-fA-F]{64}(?![0-9a-fA-F])/g)) {
            const k = m[0].toLowerCase();
            if (!vues.includes(k)) vues.push(k);
        }
    }
    return vues.join(",");
}

// ---- 1. le dossier du service ----
function trouverService() {
    const donne = argument("--service");
    const candidats = donne ? [donne] : [ICI, path.join(ICI, "service"), path.join(ICI, "..", "..", "service")];
    for (const c of candidats) if (fs.existsSync(path.join(c, "wrangler.toml"))) return path.resolve(c);
    arreter(
        "je ne trouve pas le dossier du service (le fichier wrangler.toml).\n" +
            "Mets deployer.bat, deployer.sh et deployer.mjs dans le dossier « service », ou à côté de lui.",
    );
}

// ---- 2. la cle publique de l'editeur ----
async function trouverCle(service) {
    const donnee = argument("--cle");
    if (donnee) {
        const k = clesDe(donnee);
        if (!k) arreter("la clé donnée (--cle) n'est pas une clé publique (64 caractères hexadécimaux).");
        return { cle: k, origine: "--cle" };
    }
    for (const f of [path.join(ICI, "cle-publique.txt"), path.join(service, "cle-publique.txt")]) {
        if (!fs.existsSync(f)) continue;
        const k = clesDe(fs.readFileSync(f, "utf8"));
        if (!k) arreter(f + " ne contient pas de clé publique (64 caractères hexadécimaux).");
        return { cle: k, origine: f };
    }
    console.log("Je ne trouve pas cle-publique.txt (l'outil de l'éditeur l'écrit dans son kit : Service en ligne › Kit).");
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
    const saisie = await rl.question("Colle ici la clé PUBLIQUE de l'éditeur (64 caractères hexadécimaux), puis Entrée : ");
    rl.close();
    const k = clesDe(saisie);
    if (!k) arreter("ce n'est pas une clé publique (64 caractères hexadécimaux).");
    return { cle: k, origine: "saisie" };
}

// ---- 3. wrangler ----
function installerWrangler(service) {
    const cli = path.join(service, "node_modules", "wrangler", "bin", "wrangler.js");
    if (fs.existsSync(cli)) return cli;
    console.log("");
    console.log("Installation de wrangler (l'outil de Cloudflare), une seule fois : quelques minutes...");
    const avecVerrou = fs.existsSync(path.join(service, "package-lock.json"));
    const r = spawnSync(WINDOWS ? "npm.cmd" : "npm", [avecVerrou ? "ci" : "install", "--no-audit", "--no-fund"], {
        cwd: service,
        stdio: "inherit",
        shell: WINDOWS,
    });
    if (r.status !== 0 || !fs.existsSync(cli)) arreter("l'installation de wrangler a échoué (es-tu connecté à Internet ?).");
    return cli;
}

// ---- 4. et 5. la mise en ligne ----
async function principal() {
    console.log("Mise en ligne du service des licences XPG (Cloudflare Workers)");
    console.log("==============================================================");
    const majeure = Number(process.versions.node.split(".")[0]);
    if (majeure < 22) arreter("Node.js " + process.versions.node + " est trop ancien : installe la version LTS (22 ou plus récente) depuis https://nodejs.org");
    const essai = process.argv.includes("--essai");
    const service = trouverService();
    const { cle, origine } = await trouverCle(service);
    console.log("Dossier du service : " + service);
    console.log("Clé publique de l'éditeur (" + (origine === "saisie" ? "saisie" : path.basename(origine)) + ") : " + cle.split(",").map((k) => k.slice(0, 16) + "…").join(", "));
    const cli = installerWrangler(service);
    const sortie = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "xpg-deploiement-")), "sortie.jsonl");
    const args = [cli, "deploy", "--var", "PUBLISHER_KEYS:" + cle];
    if (essai) args.push("--dry-run", "--outdir", path.join(path.dirname(sortie), "paquet"));
    console.log("");
    console.log(essai ? "Essai (rien n'est envoyé à Cloudflare)..." : "Envoi à Cloudflare (la première fois, ton navigateur s'ouvre pour te connecter)...");
    console.log("");
    const r = spawnSync(process.execPath, args, {
        cwd: service,
        stdio: "inherit",
        env: { ...process.env, WRANGLER_OUTPUT_FILE_PATH: sortie, WRANGLER_SEND_METRICS: "false" },
    });
    if (r.status !== 0) {
        arreter(
            "la mise en ligne a échoué (code " + r.status + ").\n" +
                "  - Pas encore connecté à Cloudflare ? Lance « npx wrangler login » dans le dossier du service, puis relance ce fichier.\n" +
                "  - Un compte neuf : Cloudflare demande de choisir ton sous-domaine workers.dev (le tableau de bord, Workers et Pages).",
        );
    }
    if (essai) {
        console.log("");
        console.log("Essai réussi : le service est prêt à partir (relance sans --essai pour le mettre en ligne).");
        return;
    }
    let adresses = [];
    try {
        for (const ligne of fs.readFileSync(sortie, "utf8").split("\n")) {
            if (!ligne.trim()) continue;
            const o = JSON.parse(ligne);
            if (o.type === "deploy" && Array.isArray(o.targets)) adresses = adresses.concat(o.targets);
        }
    } catch {
        adresses = [];
    }
    const adresse = adresses.find((u) => /^https:\/\/[^/]+\.workers\.dev\/?$/.test(u)) || adresses[0];
    console.log("");
    console.log("==============================================================");
    if (adresse) {
        const propre = adresse.replace(/\/+$/, "");
        try {
            fs.writeFileSync(path.join(ICI, "adresse-du-service.txt"), propre + (WINDOWS ? "\r\n" : "\n"));
        } catch {
            // le dossier est en lecture seule : l'adresse est affichee ci-dessous
        }
        console.log("Ton service est en ligne : " + propre);
        console.log("(gardée aussi dans adresse-du-service.txt)");
    } else {
        console.log("Ton service est en ligne. Son adresse est écrite plus haut par wrangler (https://xpg-licences.<ton compte>.workers.dev).");
    }
    console.log("");
    console.log("Et maintenant, dans l'outil de l'éditeur : Service en ligne › colle cette adresse › Vérifier ›");
    console.log("Certifier la clé du service.");
}

principal().catch((e) => arreter(e && e.message ? e.message : String(e)));
