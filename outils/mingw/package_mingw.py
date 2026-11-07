#!/usr/bin/env python3
# =============================================================================
#  outils/mingw/package_mingw.py - 1.11.3 : l'installateur Windows fabrique
#  sous Linux, a partir de l'exe MinGW (cross_mingw.sh).
# -----------------------------------------------------------------------------
#  Le meme resultat que outils\package.bat /exe:... /sdl:... /chaine:mingw sous
#  Windows, etape par etape (outils\lib\package.ps1 et build_installer.ps1) :
#    1. dist/staging : l'exe, SDL3.dll, pdfium.dll (1.12.30 : le moteur PDF de la
#       visionneuse, charge a la demande), resources\ (les deux catalogues), libs\,
#       maintenance\ (les six scripts, leur lib\ et leur config.ini), licences\,
#       LISEZ-MOI.txt (la version mise) ;
#    2. manifeste.json : chaque fichier, sa taille, son SHA-256, son role, et les
#       DLL que l'exe importe (lues dans sa table d'importation) - l'arret si
#       l'une d'elles n'est ni du systeme ni livree ;
#    3. ISCC (Inno Setup 6.3 ou plus) sous Wine, avec les valeurs de config.ini
#       passees par /D : API-IHM-Studio-Setup-<version>.exe a la racine du projet ;
#    4. dist/SHA256SUMS.txt.
#  Le runtime C++ est lie dans l'exe (-static) : pas de DLL du runtime a livrer.
#  1.30.0 : licences\registre.xpgreg, le registre des licences signe, s'il est dans
#  ressources-licences/ (sinon rien, et la sortie le dit) ; le controle du paquet
#  (--controler) verifie qu'il est signe par une cle de PublisherKey.hpp.
#  1.32.0 : de meme licences\revocations.xpgrev (la liste de revocation livree) ;
#  licences\ ne contient QUE le registre, la liste et les textes de licence des
#  bibliotheques ; [Licences] obligatoires = oui (outils/config.ini, ou
#  --licences-obligatoires oui) refuse un paquet sans vraie cle d'editeur.
#
#    python3 outils/mingw/package_mingw.py --iscc /chemin/ISCC.exe
#        [--exe build-mingw/XpgAnalyzer.exe] [--sdl third_party/SDL3/lib/x64/SDL3.dll]
#        [--pdfium third_party/pdfium/win-x64/pdfium.dll]
#        [--sans-installateur]
#  Wine : WINEPREFIX et WINEDEBUG sont pris dans l'environnement.
# =============================================================================
import argparse
import datetime
import fnmatch
import hashlib
import json
import zipfile
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def ini(section, key, default=""):
    """Une valeur de outils/config.ini (le commentaire de fin de ligne retire)."""
    current = None
    with open(os.path.join(ROOT, "outils", "config.ini"), encoding="utf-8-sig") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith(";"):
                continue
            m = re.match(r"^\[(.+)\]$", line)
            if m:
                current = m.group(1)
                continue
            if current == section and "=" in line:
                k, v = line.split("=", 1)
                if k.strip() == key:
                    v = re.sub(r"\s+;.*$", "", v).strip()
                    return v if v else default
    return default


def write_text(path, text, bom=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = text.replace("\r\n", "\n").replace("\n", "\r\n")
    with open(path, "w", encoding="utf-8-sig" if bom else "utf-8", newline="") as f:
        f.write(data)


def copy_tree(src, dst, exclude=()):
    if not os.path.isdir(src):
        return 0
    n = 0
    for base, _dirs, files in os.walk(src):
        for name in files:
            full = os.path.join(base, name)
            rel = os.path.relpath(full, src)
            if any(fnmatch.fnmatch(rel, e) or fnmatch.fnmatch(name, e) for e in exclude):
                continue
            target = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(full, target)
            n += 1
    return n


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def imported_dlls(exe):
    out = subprocess.run(["x86_64-w64-mingw32-objdump", "-p", exe], capture_output=True, text=True, check=True).stdout
    return sorted(set(re.findall(r"DLL Name:\s*(\S+)", out)), key=str.lower)


# 1.15.0 : winhttp (l'Assistant IA : HTTPS par Windows) ; crypt32 (DPAPI, la cle API protegee) y etait deja.
AGENT = "XPG-Deployment-Agent.exe"     # 1.28.0 : l'agent de deploiement reseau (src/agent/)
SYSTEM = {"kernel32", "user32", "gdi32", "shell32", "ole32", "oleaut32", "advapi32", "comdlg32", "ws2_32", "iphlpapi", "winhttp",
          "winmm", "imm32", "setupapi", "version", "dwmapi", "uxtheme", "shlwapi", "bcrypt", "winspool", "uuid", "msvcrt",
          "ucrtbase", "crypt32", "dbghelp", "hid", "cfgmgr32", "dinput8", "xinput1_4", "powrprof", "comctl32"}


# 1.30.0 (prepare en 1.29.0) : LE CONTROLE DU PAQUET - rien de l'outil Admin des licences ne part chez
# un client (docs/LICENCES.md). Un seul ecart arrete la construction. On cherche, dans chaque fichier du
# dossier a livrer (les octets, en ASCII et en UTF-16 : les chaines larges des exe) :
#   - le nom de l'outil Admin (XPG-License-Admin, et les noms de fichier qui le rappellent) ;
#   - sa marque (src/license_admin/Admin.hpp : kAdminMarker), qui est dans l'exe et dans le fichier
#     de la cle de l'editeur ;
#   - la marque d'une cle d'ESSAI (kTestKeyMarker) : un programme qui l'embarque n'est pas livrable ;
#   - une cle privee : un bloc PEM << PRIVATE KEY >> suivi de son contenu (l'en-tete seul est une
#     chaine de mbedTLS, dans les exe : ce n'est pas une cle), un fichier de cle (*.cle, cle_editeur.bin) ;
#   - un jeton GitHub (ghp_..., github_pat_...).
# 1.30.0 : le registre livre (licences\registre.xpgreg, s'il y est) doit etre signe par une cle que les
# programmes croient (src/licensing/PublisherKey.hpp) - jamais par la cle d'essai. 1.32.0 : de meme la liste de
# revocation livree (licences\revocations.xpgrev).
# 1.32.0 : LE DOSSIER licences\ N'ADMET QUE le registre, la liste de revocation et les textes de licence des
# bibliotheques (LICENCES_PERMIS) ; et nulle part une licence (*.xpglic), une demande (*.xpgreq), la configuration
# de la synchro (github*.json, jusqu'a la 1.34.0), l'archive de l'Admin (un dossier archive), l'etat ou l'historique
# d'un PC. 1.35.0 : ni les fichiers du service des licences de l'outil Admin (service.json, service-releve.json,
# archive-vu.json, postes-publies.json, cle-synchro.cle), ni son dossier demandes-en-ligne.
# [Licences] obligatoires = oui (outils/config.ini) : un paquet sans vraie cle d'editeur (PublisherKey.hpp vide :
# les licences << non activees >>, tout ouvert) est refuse.
# Le MEME controle est dans outils/lib/package.ps1 (Test-XpgLivraison) : memes chaines, memes motifs
# (ecrits sans \b, pour que .NET et Python les lisent pareil), memes noms. Changer l'un, c'est changer l'autre.
ADMIN_CHAINES = (
    (b"XPG-License-Admin", "le nom de l'outil Admin des licences"),
    (b"XPG License Manager Admin", "le nom de l'outil Admin des licences"),
    (b"XPG-License-Manager-Admin", "le nom de l'application Admin des licences (1.33.0)"),
    (b"XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER", "la marque de l'outil Admin des licences"),
    (b"XPG-CLE-ESSAI-LICENCES", "la cle d'ESSAI de l'editeur (cree la vraie cle et regenere src/licensing/PublisherKey.hpp)"),
    (b"XPG-SAUVEGARDE-CLE", "une sauvegarde de la cle de l'editeur (1.32.0)"),
    (b"XPG-CHIFFRE 1 xpgs-", "un fichier chiffre de la synchro des licences (1.31.0)"),
    (b"XPG-INDEX 1 xpgs-", "l'index de la synchro des licences (1.31.0)"),
    (b"XPG-LICENSING 1 licence ", "une licence signee (le contenu d'un .xpglic : elle va a un client, jamais dans le paquet) (1.32.0)"),
    (b"XPG-LICENSING 1 demande ", "une demande d'activation (le contenu d'un .xpgreq) (1.32.0)"),
)
ADMIN_MOTIFS = (
    (re.compile(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----\r?\n[A-Za-z0-9+/=\r\n]{40,}"), "une cle privee (bloc PEM)"),
    (re.compile(rb"(?<![A-Za-z0-9_])gh[pousr]_[A-Za-z0-9]{36}(?![A-Za-z0-9_])|(?<![A-Za-z0-9_])github_pat_[A-Za-z0-9_]{50,}"), "un jeton GitHub"),
)
ADMIN_NOMS = ("*.cle", "cle_editeur.bin", "cle-editeur-licences*", "*license*admin*", "*licence*admin*", "*license_admin*",
              "*.xpgcle", "*.xpgsync", "activations.jsonl",
              "*.xpglic", "*.xpgreq", "github*.json", "etat.bin", "historique.jsonl",   # 1.32.0
              "service.json", "service-releve.json", "archive-vu.json", "postes-publies.json", "cle-synchro.cle")   # 1.35.0
# 1.32.0 : l'archive de l'outil Admin (signer-*, archiver), a aucun niveau ; 1.35.0 : ses demandes en ligne (le releve)
ADMIN_DOSSIERS = ("archive", "demandes-en-ligne")
# 1.32.0 : ce que licences\ admet (chemins relatifs a licences\, sans casse) - le reste est un ecart.
LICENCES_PERMIS = ("registre.xpgreg", "revocations.xpgrev", "*-LICENSE*", "*.LICENSE",
                   "pdfium/*.txt", "pdfium/*.md", "pdfium/*.ijg")


REGISTRE_LIVRE = os.path.join("licences", "registre.xpgreg")   # 1.30.0 : le registre des editions livre
REVOCATIONS_LIVREES = os.path.join("licences", "revocations.xpgrev")   # 1.32.0 : la liste de revocation livree


def cles_compilees(nom):
    """Les identifiants des cles d'un fichier ecrit par exporter-cle (src/licensing/<nom>)."""
    chemin = os.path.join(ROOT, "src", "licensing", nom)
    if not os.path.isfile(chemin):
        return set()
    with open(chemin, encoding="ascii", errors="replace") as f:
        return set(re.findall(r'\{"(xpg-[0-9a-f]{16})",', f.read()))


def controle_signe(dossier, rel, genre, textes):
    """Un document livre (le registre, la liste de revocation) : signe par une cle des programmes (PublisherKey.hpp),
    jamais par la cle d'essai. `textes` : (sujet, << pas un ... signe >>, << signe >> accorde)."""
    sujet, pas_signe, signe = textes
    chemin = os.path.join(dossier, rel)
    if not os.path.isfile(chemin):
        return []
    with open(chemin, "rb") as f:
        data = f.read()
    m = re.match(rb"XPG-LICENSING 1 " + genre.encode("ascii") + rb" ([0-9]+)\n", data)
    cles = re.findall(rb"\nSIGNATURE ed25519 (xpg-[0-9a-f]{16}) [0-9a-f]{128}\n", data)
    if not m or len(cles) != 1:
        return [(rel, f"{sujet} n'est pas {pas_signe} (un seul bloc XPG-LICENSING {genre})")]
    cle = cles[0].decode("ascii")
    if cle in cles_compilees("TestPublisherKey.hpp"):
        return [(rel, f"{sujet} est {signe} par la cle d'ESSAI ({cle}) : signe-le avec la vraie cle")]
    if cle not in cles_compilees("PublisherKey.hpp"):
        return [(rel, f"{sujet} est {signe} par la cle {cle}, que src/licensing/PublisherKey.hpp ne contient pas : "
                      "les programmes ne pourraient pas le verifier")]
    return []


def controle_registre(dossier):
    """Le registre et (1.32.0) la liste de revocation livres : signes par une cle des programmes."""
    return (controle_signe(dossier, REGISTRE_LIVRE, "registre", ("le registre livre", "un registre signe", "signe"))
            + controle_signe(dossier, REVOCATIONS_LIVREES, "revocations",
                             ("la liste de revocation livree", "une liste de revocation signee", "signee")))


def controle_dossier_licences(dossier):
    """1.32.0 : licences\\ n'admet que le registre, la liste de revocation et les textes de licence des bibliotheques."""
    racine = os.path.join(dossier, "licences")
    ecarts = []
    for base, _dirs, files in os.walk(racine):
        for name in sorted(files):
            rel = os.path.relpath(os.path.join(base, name), racine).replace(os.sep, "/")
            if not any(fnmatch.fnmatchcase(rel.lower(), p.lower()) for p in LICENCES_PERMIS):
                ecarts.append((os.path.join("licences", rel.replace("/", os.sep)),
                               "licences\\ n'admet que registre.xpgreg, revocations.xpgrev et les textes de licence des bibliotheques"))
    return ecarts


def controle_cle_obligatoire(obligatoires):
    """1.32.0 : [Licences] obligatoires = oui - PublisherKey.hpp doit contenir une vraie cle de l'editeur."""
    if obligatoires.strip().lower() != "oui":
        return []
    if cles_compilees("PublisherKey.hpp"):
        return []
    return [(os.path.join("src", "licensing", "PublisherKey.hpp"),
             "aucune cle de l'editeur alors que [Licences] obligatoires = oui (outils/config.ini) : les programmes livres "
             "auraient les licences non activees (tout ouvert) ; exporter-cle la vraie cle, ou obligatoires = non")]


def controle_livraison(dossier, obligatoires="non"):
    """Les ecarts trouves dans le dossier a livrer : [(chemin relatif, ce qui est trouve)] ; vide : rien."""
    ecarts = controle_cle_obligatoire(obligatoires) + controle_registre(dossier) + controle_dossier_licences(dossier)
    motifs = [(c, quoi) for c, quoi in ADMIN_CHAINES] + [(c.decode("ascii").encode("utf-16-le"), quoi) for c, quoi in ADMIN_CHAINES]
    for base, dirs, files in os.walk(dossier):
        for d in sorted(dirs):
            if d.lower() in ADMIN_DOSSIERS:
                ecarts.append((os.path.relpath(os.path.join(base, d), dossier),
                               "un dossier de l'outil Admin (l'archive des licences signees, ses demandes en ligne)"))
        for name in sorted(files):
            full = os.path.join(base, name)
            rel = os.path.relpath(full, dossier)
            compact = name.lower().replace(" ", "-")
            for n in ADMIN_NOMS:
                if fnmatch.fnmatch(compact, n) or fnmatch.fnmatch(compact.replace("-", "_"), n.replace("-", "_")):
                    ecarts.append((rel, "un fichier de l'outil Admin ou une cle (nom " + name + ")"))
                    break
            with open(full, "rb") as f:
                data = f.read()
            for c, quoi in motifs:
                if c in data:
                    ecarts.append((rel, quoi))
                    break
            for m, quoi in ADMIN_MOTIFS:
                if m.search(data):
                    ecarts.append((rel, quoi))
    return ecarts


def licences_obligatoires(valeur=""):
    """[Licences] obligatoires (outils/config.ini) ; `valeur` (--licences-obligatoires) la remplace."""
    return (valeur or ini("Licences", "obligatoires", "non")).strip().lower()


def controler_ou_arreter(dossier, obligatoires=None):
    ecarts = controle_livraison(dossier, licences_obligatoires() if obligatoires is None else obligatoires)
    if ecarts:
        lignes = "\n".join(f"  {rel} : {quoi}" for rel, quoi in ecarts)
        sys.exit(f"CONTROLE DU PAQUET : construction arretee, {len(ecarts)} ecart(s) dans {dossier}\n{lignes}\n"
                 "L'outil Admin des licences, sa cle et une cle d'essai ne se livrent jamais (docs/LICENCES.md).")
    print(f"controle du paquet : {dossier} - rien de l'outil Admin, aucune cle privee, aucun jeton")


def winpath(path):
    return subprocess.run(["winepath", "-w", path], capture_output=True, text=True, check=True).stdout.strip()


# 1.12.0 : UN ZIP PORTABLE PAR APPLICATION - le dossier a livrer sans l'exe de l'autre,
# son manifeste et sa maintenance\config.ini a lui (exe = le sien) :
# <application>-<version>-portable.zip (1.12.20 : API-Studio-..., IHM-Studio-...), un dossier
# <application>-<version> dedans.
def portables(st, sortie, version, editions, obligatoires="non"):
    os.makedirs(sortie, exist_ok=True)
    for application, exe, autre in editions:
        nom = f"{application}-{version}"
        travail = os.path.join(sortie, nom)
        if os.path.isdir(travail):
            shutil.rmtree(travail)
        shutil.copytree(st, travail)
        os.remove(os.path.join(travail, autre))
        if exe != editions[-1][1] and os.path.exists(os.path.join(travail, AGENT)):
            os.remove(os.path.join(travail, AGENT))     # 1.28.0 : l'agent, dans le portable d'IHM Studio seulement
        cfg = os.path.join(travail, "maintenance", "config.ini")
        with open(cfg, encoding="utf-8-sig") as f:
            texte = f.read()
        texte = re.sub(r"(?m)^exe = .*$", f"exe = {exe}", texte)
        texte = re.sub(r"(?m)^exe_ihm = .*\n", "", texte)
        write_text(cfg, texte)
        with open(os.path.join(travail, "manifeste.json"), encoding="utf-8") as f:
            m = json.load(f)
        m["exe"] = exe
        m.pop("exe_ihm", None)
        m["fichiers"] = []
        for base, _dirs, files in os.walk(travail):
            for name in files:
                if name == "manifeste.json":
                    continue
                full = os.path.join(base, name)
                rel = os.path.relpath(full, travail).replace("/", "\\")
                role = "programme"
                if rel.startswith("resources\\") or rel.startswith("libs\\"):
                    role = "donnee-livree"
                elif rel.startswith("maintenance\\"):
                    role = "maintenance"
                elif rel.startswith("licences\\") or rel.lower().endswith(".txt"):
                    role = "document"
                m["fichiers"].append({"chemin": rel, "taille": os.path.getsize(full), "sha256": sha256(full), "role": role})
        m["fichiers"].sort(key=lambda x: x["chemin"].lower())
        with open(os.path.join(travail, "manifeste.json"), "w", encoding="utf-8", newline="") as f:
            f.write(json.dumps(m, ensure_ascii=False, indent=4).replace("\n", "\r\n"))
        controler_ou_arreter(travail, obligatoires)   # 1.30.0 : rien de l'outil Admin des licences
        archive = os.path.join(sortie, nom + "-portable.zip")
        if os.path.exists(archive):
            os.remove(archive)
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for base, _dirs, files in os.walk(travail):
                for name in sorted(files):
                    full = os.path.join(base, name)
                    z.write(full, os.path.relpath(full, sortie))
        print(f"portable : {archive} ({os.path.getsize(archive) // 1024} Kio, {len(m['fichiers']) + 1} fichiers)")


def main():
    ap = argparse.ArgumentParser()
    # 1.12.0 : deux applications - API Studio (--exe) et IHM Studio (--exe-ihm).
    ap.add_argument("--exe", default=os.path.join(ROOT, "build-mingw", "API-Studio.exe"))
    ap.add_argument("--exe-ihm", default=os.path.join(ROOT, "build-mingw", "IHM-Studio.exe"))
    # 1.28.0 : l'agent de deploiement (installe avec IHM Studio et le poste ; dans le portable d'IHM Studio seulement).
    ap.add_argument("--agent", default=os.path.join(ROOT, "build-mingw", AGENT))
    ap.add_argument("--portables", default="", help="un dossier : un zip portable par application")
    ap.add_argument("--sdl", default=os.path.join(ROOT, "third_party", "SDL3", "lib", "x64", "SDL3.dll"))
    # 1.12.30 : PDFium (outils/pdfium/obtenir_pdfium.py le pose la).
    ap.add_argument("--pdfium", default=os.path.join(ROOT, "third_party", "pdfium", "win-x64", "pdfium.dll"))
    ap.add_argument("--iscc", default="")
    ap.add_argument("--sans-installateur", action="store_true")
    # 1.30.0 : le controle du paquet seul, sur un dossier (les essais : tests/licensing_test.cpp).
    ap.add_argument("--controler", default="", help="un dossier : le controle du paquet seulement")
    # 1.32.0 : remplace [Licences] obligatoires de outils/config.ini (les essais).
    ap.add_argument("--licences-obligatoires", default="", choices=("", "oui", "non"))
    a = ap.parse_args()
    obligatoires = licences_obligatoires(a.licences_obligatoires)
    if a.controler:
        controler_ou_arreter(a.controler, obligatoires)
        return

    produit = ini("Produit", "nom")
    cle = ini("Produit", "cle", "XPGAnalyser")             # 1.12.20 : le nom d'origine (registre, dossiers)
    fichier = ini("Produit", "fichier", "API-IHM-Studio")  # 1.12.20 : le debut du nom de l'installateur
    version = ini("Produit", "version")
    exe_nom = ini("Produit", "exe", "API-Studio.exe")
    exe_ihm_nom = ini("Produit", "exe_ihm", "IHM-Studio.exe")
    chaine = "mingw"
    dist = os.path.join(ROOT, ini("Installateur", "sortie", "dist"))
    st = os.path.join(dist, "staging")

    # ---- 1. le dossier a livrer ----
    if os.path.isdir(st):
        shutil.rmtree(st)
    os.makedirs(st)
    shutil.copy2(a.exe, os.path.join(st, exe_nom))
    shutil.copy2(a.exe_ihm, os.path.join(st, exe_ihm_nom))
    if not os.path.isfile(a.agent):
        sys.exit(f"{a.agent} manque : l'agent de deploiement (outils/mingw/cross_mingw.sh le construit)")
    shutil.copy2(a.agent, os.path.join(st, AGENT))
    shutil.copy2(a.sdl, os.path.join(st, "SDL3.dll"))
    # 1.12.30 : le moteur PDF de la visionneuse de documents. L'exe ne l'importe pas (il le charge a la demande) :
    # le manifeste ne le demande pas, il est livre quand meme, a cote des exe, avec ses licences.
    if not os.path.isfile(a.pdfium):
        sys.exit(f"{a.pdfium} manque : python3 outils/pdfium/obtenir_pdfium.py (third_party/pdfium/ORIGINE.txt)")
    shutil.copy2(a.pdfium, os.path.join(st, "pdfium.dll"))
    n_res = 0
    for f in ("schneider_library.txt", "plc_catalog.txt"):
        os.makedirs(os.path.join(st, "resources"), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, "resources", f), os.path.join(st, "resources", f))
        n_res += 1
    n_res += copy_tree(os.path.join(ROOT, "resources", "fonts"), os.path.join(st, "resources", "fonts"))
    # 1.26.0 : les polices fournies des IHM (TrueType libres, et leurs licences) - pas celles de l interface.
    n_res += copy_tree(os.path.join(ROOT, "resources", "polices"), os.path.join(st, "resources", "polices"))
    n_libs = copy_tree(os.path.join(ROOT, "libs"), os.path.join(st, "libs"), exclude=("*.bak", "*~", "~$*"))
    m = os.path.join(st, "maintenance")
    os.makedirs(os.path.join(m, "lib"))
    for b in ("repair", "verify_installation", "restore_backup", "resume_installation", "reset_user_settings", "collect_diagnostics"):
        shutil.copy2(os.path.join(ROOT, "outils", b + ".bat"), m)
        shutil.copy2(os.path.join(ROOT, "outils", "lib", b + ".ps1"), os.path.join(m, "lib"))
    for f in ("XpgCommun.psm1", "XpgInstallation.psm1", "installer_etape.ps1", "empreintes-livrees.txt"):
        s = os.path.join(ROOT, "outils", "lib", f)
        if os.path.exists(s):
            shutil.copy2(s, os.path.join(m, "lib"))
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    write_text(os.path.join(m, "config.ini"),
               f"; maintenance\\config.ini - ecrit par package_mingw.py ({stamp}) depuis outils\\config.ini.\n"
               f"; Ce que les scripts de maintenance doivent savoir d'{produit}. Ne pas modifier.\n"
               "[Produit]\n"
               f"nom = {produit}\n"
               f"cle = {cle}\n"
               f"editeur = {ini('Produit', 'editeur')}\n"
               f"version = {version}\n"
               f"app_id = {ini('Produit', 'app_id')}\n"
               f"exe = {exe_nom}\n"
               f"exe_ihm = {exe_ihm_nom}\n"
               f"chaine = {chaine}\n"
               "\n"
               "[Maintenance]\n"
               f"sauvegardes_a_garder = {ini('Maintenance', 'sauvegardes_a_garder', '3')}\n"
               f"anciens_dossiers = {ini('Installateur', 'anciens_dossiers')}")
    lic = os.path.join(st, "licences")
    os.makedirs(lic)
    # 1.30.0 : le registre des editions et fonctionnalites, signe par l'editeur avec sa vraie cle (docs/LICENCES.md) :
    # installe dans {app}\licences\registre.xpgreg, les programmes l'adoptent s'il est plus recent que celui du PC.
    registre = os.path.join(ROOT, "ressources-licences", "registre.xpgreg")
    if os.path.isfile(registre):
        shutil.copy2(registre, os.path.join(st, REGISTRE_LIVRE))
        print("registre des licences : ressources-licences/registre.xpgreg -> licences\\registre.xpgreg")
    else:
        print("registre des licences : ressources-licences/registre.xpgreg absent - aucun registre livre "
              "(il sera signe par l'editeur avec sa vraie cle)")
    # 1.32.0 : la liste de revocation signee par l'editeur, s'il y en a une : adoptee si elle est plus recente.
    revocations = os.path.join(ROOT, "ressources-licences", "revocations.xpgrev")
    if os.path.isfile(revocations):
        shutil.copy2(revocations, os.path.join(st, REVOCATIONS_LIVREES))
        print("liste de revocation : ressources-licences/revocations.xpgrev -> licences\\revocations.xpgrev")
    shutil.copy2(os.path.join(ROOT, "third_party", "SDL3", "LICENSE.txt"), os.path.join(lic, "SDL3-LICENSE.txt"))
    # 1.28.0 : mbedTLS (Apache-2.0) et libsodium (ISC), dans les exe (le deploiement reseau).
    shutil.copy2(os.path.join(ROOT, "third_party", "mbedtls", "LICENSE"), os.path.join(lic, "mbedtls-LICENSE.txt"))
    shutil.copy2(os.path.join(ROOT, "third_party", "libsodium", "LICENSE"), os.path.join(lic, "libsodium-LICENSE.txt"))
    # 1.12.30 : les licences de PDFium et de sa construction (BSD-3-Clause, Apache-2.0, MIT...).
    lic_pdfium = os.path.join(os.path.dirname(a.pdfium), "LICENSES")
    if os.path.isdir(lic_pdfium):
        os.makedirs(os.path.join(lic, "pdfium"))
        for f in sorted(os.listdir(lic_pdfium)):
            if os.path.isfile(os.path.join(lic_pdfium, f)):
                shutil.copy2(os.path.join(lic_pdfium, f), os.path.join(lic, "pdfium", f))
    else:
        sys.exit(f"{lic_pdfium} manque : les licences de PDFium (python3 outils/pdfium/obtenir_pdfium.py)")
    for f in sorted(os.listdir(os.path.join(ROOT, "third_party"))):
        if f.endswith(".LICENSE"):
            shutil.copy2(os.path.join(ROOT, "third_party", f), lic)
    with open(os.path.join(ROOT, "third_party", "stb_image.h"), encoding="utf-8", errors="replace") as f:
        stb = f.read()
    i = stb.find("This software is available under 2 licenses")
    if i >= 0:
        write_text(os.path.join(lic, "stb.LICENSE"), stb[i:].replace("*/", "").strip() + "\n", bom=False)
    with open(os.path.join(ROOT, "installateur", "LISEZ-MOI.txt"), encoding="utf-8-sig") as f:
        write_text(os.path.join(st, "LISEZ-MOI.txt"), f.read().replace("{VERSION}", version))
    print(f"staging : {exe_nom}, {exe_ihm_nom}, SDL3.dll, pdfium.dll, 0 DLL du runtime (statique), {n_res} ressource(s), {n_libs} fichier(s) de bibliotheque, 6 scripts")

    controler_ou_arreter(st, obligatoires)   # 1.30.0 : rien de l'outil Admin des licences

    # ---- 2. le manifeste ----
    dlls = sorted(set(imported_dlls(os.path.join(st, exe_nom))) | set(imported_dlls(os.path.join(st, exe_ihm_nom)))
                  | set(imported_dlls(os.path.join(st, AGENT))), key=str.lower)
    a_livrer = [d for d in dlls if re.sub(r"\.dll$", "", d.lower()) not in SYSTEM and not d.lower().startswith("api-ms-win-")]
    for d in a_livrer:
        if not os.path.exists(os.path.join(st, d)):
            sys.exit(f"L'exe demande {d}, qui n'est pas dans le dossier a livrer.")
    fichiers = []
    for base, _dirs, files in os.walk(st):
        for name in files:
            full = os.path.join(base, name)
            rel = os.path.relpath(full, st).replace("/", "\\")
            role = "programme"
            if rel.startswith("resources\\") or rel.startswith("libs\\"):
                role = "donnee-livree"
            elif rel.startswith("maintenance\\"):
                role = "maintenance"
            elif rel.startswith("licences\\") or rel.lower().endswith(".txt"):
                role = "document"
            fichiers.append({"chemin": rel, "taille": os.path.getsize(full), "sha256": sha256(full), "role": role})
    fichiers.sort(key=lambda x: x["chemin"].lower())
    manifeste = {"produit": produit, "version": version, "chaine": chaine,
                 "date": datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
                 "exe": exe_nom, "exe_ihm": exe_ihm_nom, "dependances": a_livrer, "runtime_local": [], "fichiers": fichiers}
    with open(os.path.join(st, "manifeste.json"), "w", encoding="utf-8", newline="") as f:
        f.write(json.dumps(manifeste, ensure_ascii=False, indent=4).replace("\n", "\r\n"))
    print(f"manifeste : {len(fichiers)} fichiers ; DLL a livrer : {', '.join(a_livrer) or 'aucune'} ; importees : {', '.join(dlls)}")
    if a.portables:
        portables(st, a.portables, version, ((os.path.splitext(exe_nom)[0], exe_nom, exe_ihm_nom), (os.path.splitext(exe_ihm_nom)[0], exe_ihm_nom, exe_nom)),
                  obligatoires)
    if a.sans_installateur:
        return

    # ---- 3. Inno Setup sous Wine ----
    if not a.iscc or not os.path.exists(a.iscc):
        sys.exit("ISCC.exe introuvable (--iscc).")
    setup_nom = f"{fichier}-Setup-{version}.exe"
    defs = {
        "Nom": produit,
        "Cle": cle,
        "Fichier": fichier,
        "Editeur": ini("Produit", "editeur"),
        "Version": version,
        "AppGuid": ini("Produit", "app_id").strip("{}"),
        "Exe": exe_nom,
        "ExeIhm": exe_ihm_nom,
        "ExeAgent": AGENT,
        "Description": ini("Produit", "description"),
        "UrlSupport": ini("Produit", "url_support"),
        "Staging": winpath(st),
        "Sortie": winpath(ROOT),
        "DonneesDefaut": ini("Installateur", "dossier_donnees_par_defaut", "{Documents}\\XPGAnalyser"),
        "AnciensDossiers": ini("Installateur", "anciens_dossiers"),
        "Chaine": chaine,
        "Racine": winpath(ROOT),
    }
    args = ["wine", a.iscc, "/Q"] + [f"/D{k}={v}" for k, v in defs.items()] + [winpath(os.path.join(ROOT, "installateur", "XPGAnalyser.iss"))]
    r = subprocess.run(args)
    setup = os.path.join(ROOT, setup_nom)
    if r.returncode != 0 or not os.path.exists(setup):
        sys.exit(f"Inno Setup a echoue (code {r.returncode}).")

    # ---- 4. l'empreinte ----
    h = sha256(setup)
    os.makedirs(dist, exist_ok=True)
    with open(os.path.join(dist, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="") as f:
        f.write(f"{h}  {setup_nom}\r\n")
    print(f"fait : {setup} ({os.path.getsize(setup) // 1024} Kio) ; SHA-256 {h}")


if __name__ == "__main__":
    main()
