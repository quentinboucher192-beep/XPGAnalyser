#!/usr/bin/env python3
# =============================================================================
#  outils/mingw/package_mingw.py - 1.11.3 : l'installateur Windows fabrique
#  sous Linux, a partir de l'exe MinGW (cross_mingw.sh).
# -----------------------------------------------------------------------------
#  Le meme resultat que outils\package.bat /exe:... /sdl:... /chaine:mingw sous
#  Windows, etape par etape (outils\lib\package.ps1 et build_installer.ps1) :
#    1. dist/staging : l'exe, SDL3.dll, resources\ (les deux catalogues), libs\,
#       maintenance\ (les six scripts, leur lib\ et leur config.ini), licences\,
#       LISEZ-MOI.txt (la version mise) ;
#    2. manifeste.json : chaque fichier, sa taille, son SHA-256, son role, et les
#       DLL que l'exe importe (lues dans sa table d'importation) - l'arret si
#       l'une d'elles n'est ni du systeme ni livree ;
#    3. ISCC (Inno Setup 6.3 ou plus) sous Wine, avec les valeurs de config.ini
#       passees par /D : XPGAnalyser-Setup-<version>.exe a la racine du projet ;
#    4. dist/SHA256SUMS.txt.
#  Le runtime C++ est lie dans l'exe (-static) : pas de DLL du runtime a livrer.
#
#    python3 outils/mingw/package_mingw.py --iscc /chemin/ISCC.exe
#        [--exe build-mingw/XpgAnalyzer.exe] [--sdl third_party/SDL3/lib/x64/SDL3.dll]
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


SYSTEM = {"kernel32", "user32", "gdi32", "shell32", "ole32", "oleaut32", "advapi32", "comdlg32", "ws2_32", "iphlpapi",
          "winmm", "imm32", "setupapi", "version", "dwmapi", "uxtheme", "shlwapi", "bcrypt", "winspool", "uuid", "msvcrt",
          "ucrtbase", "crypt32", "dbghelp", "hid", "cfgmgr32", "dinput8", "xinput1_4", "powrprof", "comctl32"}


def winpath(path):
    return subprocess.run(["winepath", "-w", path], capture_output=True, text=True, check=True).stdout.strip()


# 1.12.0 : UN ZIP PORTABLE PAR APPLICATION - le dossier a livrer sans l'exe de l'autre,
# son manifeste et sa maintenance\config.ini a lui (exe = le sien) :
# <produit>-<edition>-<version>-portable.zip, un dossier <produit>-<edition>-<version> dedans.
def portables(st, sortie, produit, version, editions):
    os.makedirs(sortie, exist_ok=True)
    for edition, exe, autre in editions:
        nom = f"{produit}-{edition}-{version}"
        travail = os.path.join(sortie, nom)
        if os.path.isdir(travail):
            shutil.rmtree(travail)
        shutil.copytree(st, travail)
        os.remove(os.path.join(travail, autre))
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
    # 1.12.0 : deux applications - XPGAnalyser API (--exe) et XPGAnalyser IHM (--exe-ihm).
    ap.add_argument("--exe", default=os.path.join(ROOT, "build-mingw", "XpgAnalyzer-API.exe"))
    ap.add_argument("--exe-ihm", default=os.path.join(ROOT, "build-mingw", "XpgAnalyzer-IHM.exe"))
    ap.add_argument("--portables", default="", help="un dossier : un zip portable par application")
    ap.add_argument("--sdl", default=os.path.join(ROOT, "third_party", "SDL3", "lib", "x64", "SDL3.dll"))
    ap.add_argument("--iscc", default="")
    ap.add_argument("--sans-installateur", action="store_true")
    a = ap.parse_args()

    produit = ini("Produit", "nom")
    version = ini("Produit", "version")
    exe_nom = ini("Produit", "exe", "XpgAnalyzer-API.exe")
    exe_ihm_nom = ini("Produit", "exe_ihm", "XpgAnalyzer-IHM.exe")
    chaine = "mingw"
    dist = os.path.join(ROOT, ini("Installateur", "sortie", "dist"))
    st = os.path.join(dist, "staging")

    # ---- 1. le dossier a livrer ----
    if os.path.isdir(st):
        shutil.rmtree(st)
    os.makedirs(st)
    shutil.copy2(a.exe, os.path.join(st, exe_nom))
    shutil.copy2(a.exe_ihm, os.path.join(st, exe_ihm_nom))
    shutil.copy2(a.sdl, os.path.join(st, "SDL3.dll"))
    n_res = 0
    for f in ("schneider_library.txt", "plc_catalog.txt"):
        os.makedirs(os.path.join(st, "resources"), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, "resources", f), os.path.join(st, "resources", f))
        n_res += 1
    n_res += copy_tree(os.path.join(ROOT, "resources", "fonts"), os.path.join(st, "resources", "fonts"))
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
               "; Ce que les scripts de maintenance doivent savoir de XPGAnalyser. Ne pas modifier.\n"
               "[Produit]\n"
               f"nom = {produit}\n"
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
    shutil.copy2(os.path.join(ROOT, "third_party", "SDL3", "LICENSE.txt"), os.path.join(lic, "SDL3-LICENSE.txt"))
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
    print(f"staging : {exe_nom}, {exe_ihm_nom}, SDL3.dll, 0 DLL du runtime (statique), {n_res} ressource(s), {n_libs} fichier(s) de bibliotheque, 6 scripts")

    # ---- 2. le manifeste ----
    dlls = sorted(set(imported_dlls(os.path.join(st, exe_nom))) | set(imported_dlls(os.path.join(st, exe_ihm_nom))), key=str.lower)
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
        portables(st, a.portables, produit, version, (("API", exe_nom, exe_ihm_nom), ("IHM", exe_ihm_nom, exe_nom)))
    if a.sans_installateur:
        return

    # ---- 3. Inno Setup sous Wine ----
    if not a.iscc or not os.path.exists(a.iscc):
        sys.exit("ISCC.exe introuvable (--iscc).")
    setup_nom = f"{produit}-Setup-{version}.exe"
    defs = {
        "Nom": produit,
        "Editeur": ini("Produit", "editeur"),
        "Version": version,
        "AppGuid": ini("Produit", "app_id").strip("{}"),
        "Exe": exe_nom,
        "ExeIhm": exe_ihm_nom,
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
