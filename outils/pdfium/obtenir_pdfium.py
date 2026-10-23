#!/usr/bin/env python3
# =============================================================================
#  outils/pdfium/obtenir_pdfium.py - 1.12.30 : LE MOTEUR PDF DE LA VISIONNEUSE
# -----------------------------------------------------------------------------
#  La visionneuse de documents lit les PDF avec PDFium (le moteur PDF de
#  Chromium, licence BSD-3-Clause / Apache-2.0), charge a la demande par
#  l'application (src/hmi/HmiPdf.cpp) : pdfium.dll a cote de IHM-Studio.exe,
#  libpdfium.so pour les essais sous Linux. Les binaires sont ceux de
#  pdfium-binaries (bblanchon), tels que la roue pypdfium2 5.14.0 de PyPI les
#  livre (PDFium 156.0.8076.0). Ce script les prend dans les deux roues (Windows
#  x64, Linux x64), verifie leurs empreintes, et pose :
#
#    third_party/pdfium/win-x64/pdfium.dll        + LICENSES/ (les licences de la construction)
#    third_party/pdfium/linux-x64/libpdfium.so    + LICENSES/
#
#  Ces deux dossiers ne vont pas dans le depot (.gitignore) : third_party/pdfium/ORIGINE.txt dit d'ou ils
#  viennent. Lance-le une fois apres avoir clone le depot (il faut l'acces a PyPI) :
#
#    python3 outils/pdfium/obtenir_pdfium.py                 # telecharge les roues (pip download)
#    python3 outils/pdfium/obtenir_pdfium.py --depuis DOSSIER   # des roues deja telechargees
#    python3 outils/pdfium/obtenir_pdfium.py --verifier      # les fichiers poses sont-ils les bons ?
#
#  Les roues sont des donnees : rien n'en est execute, seuls trois genres de fichiers en sont lus (la
#  bibliotheque, sa version, ses licences), ecrits sous des noms que ce script choisit.
# =============================================================================
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

VERSION_PYPDFIUM = "5.14.0"
VERSION_PDFIUM = "156.0.8076.0"

# Les deux plateformes : la roue, son empreinte, la bibliotheque et son empreinte.
PLATEFORMES = [
    {
        "dossier": "win-x64",
        "pip": "win_amd64",
        "roue": "pypdfium2-5.14.0-py3-none-win_amd64.whl",
        "roue_sha256": "149fd5c6397b8df8bf7911a93506eff0be874f877afe7ac936cf5d37d21a6a06",
        "biblio": "pdfium.dll",
        "biblio_sha256": "69f1e860f6742fbaafcc4db80e785eec9a4ee0da36ea6d1dc0867ea0f18baea6",
        "licences": "pypdfium2-5.14.0.dist-info/licenses/data/windows_x64/BUILD_LICENSES/",
    },
    {
        "dossier": "linux-x64",
        "pip": "manylinux_2_17_x86_64",
        "roue": "pypdfium2-5.14.0-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
        "roue_sha256": "11f281613fa22313d9c7ab89947665e84eccf8ebe40e1198a84a88352305648d",
        "biblio": "libpdfium.so",
        "biblio_sha256": "fd4de9f91f183476d04b1bc3fd17dacd95aba349c55d2c376512d95e082b441e",
        "licences": "pypdfium2-5.14.0.dist-info/licenses/data/linux_x64/BUILD_LICENSES/",
    },
]

RACINE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DESTINATION = os.path.join(RACINE, "third_party", "pdfium")


def empreinte(chemin):
    h = hashlib.sha256()
    with open(chemin, "rb") as f:
        for bloc in iter(lambda: f.read(1 << 20), b""):
            h.update(bloc)
    return h.hexdigest()


def empreinte_octets(octets):
    return hashlib.sha256(octets).hexdigest()


def telecharger(plateforme, dossier):
    """pip download de la roue d'une plateforme, dans `dossier` (vide)."""
    commande = [
        sys.executable, "-m", "pip", "download", "pypdfium2==" + VERSION_PYPDFIUM,
        "--no-deps", "--only-binary=:all:", "--platform", plateforme["pip"],
        "--python-version", "3.11", "--implementation", "py", "-d", dossier,
    ]
    print("  pip download pypdfium2==%s (%s)" % (VERSION_PYPDFIUM, plateforme["pip"]))
    resultat = subprocess.run(commande, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if resultat.returncode != 0:
        sys.stdout.write(resultat.stdout)
        raise SystemExit("echec du telechargement (PyPI injoignable ?) : %s" % plateforme["roue"])
    chemin = os.path.join(dossier, plateforme["roue"])
    if not os.path.isfile(chemin):
        raise SystemExit("pip n'a pas rendu la roue attendue %s (dans %s : %s)" % (plateforme["roue"], dossier, ", ".join(os.listdir(dossier))))
    return chemin


def poser(plateforme, roue):
    """Verifie la roue, en sort la bibliotheque, sa version et ses licences."""
    vu = empreinte(roue)
    if vu != plateforme["roue_sha256"]:
        raise SystemExit("%s : empreinte inattendue %s (attendue %s)" % (os.path.basename(roue), vu, plateforme["roue_sha256"]))
    cible = os.path.join(DESTINATION, plateforme["dossier"])
    licences = os.path.join(cible, "LICENSES")
    os.makedirs(licences, exist_ok=True)
    with zipfile.ZipFile(roue) as z:
        octets = z.read("pypdfium2_raw/" + plateforme["biblio"])
        if empreinte_octets(octets) != plateforme["biblio_sha256"]:
            raise SystemExit("%s dans la roue : empreinte inattendue" % plateforme["biblio"])
        version = json.loads(z.read("pypdfium2_raw/version.json").decode("utf-8"))
        lue = "%d.%d.%d.%d" % (version["major"], version["minor"], version["build"], version["patch"])
        if lue != VERSION_PDFIUM:
            raise SystemExit("PDFium %s dans la roue (attendu %s)" % (lue, VERSION_PDFIUM))
        temporaire = os.path.join(cible, plateforme["biblio"] + ".part")
        with open(temporaire, "wb") as f:
            f.write(octets)
        os.replace(temporaire, os.path.join(cible, plateforme["biblio"]))
        with open(os.path.join(cible, "version.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(version, f, indent=2)
            f.write("\n")
        n = 0
        for nom in z.namelist():
            if not nom.startswith(plateforme["licences"]) or nom.endswith("/"):
                continue
            base = os.path.basename(nom)
            # Un nom simple seulement (pas de chemin) : rien n'est ecrit hors de LICENSES/.
            if not base or base != nom[len(plateforme["licences"]):] or base.startswith("."):
                continue
            with open(os.path.join(licences, base), "wb") as f:
                f.write(z.read(nom))
            n += 1
    print("  %s/%s : PDFium %s, %d licences" % (plateforme["dossier"], plateforme["biblio"], lue, n))


def verifier():
    bon = True
    for p in PLATEFORMES:
        chemin = os.path.join(DESTINATION, p["dossier"], p["biblio"])
        if not os.path.isfile(chemin):
            print("absent  : %s" % chemin)
            bon = False
        elif empreinte(chemin) != p["biblio_sha256"]:
            print("DIFFERENT : %s (empreinte %s)" % (chemin, empreinte(chemin)))
            bon = False
        else:
            print("bon     : %s" % chemin)
    return bon


def main():
    parser = argparse.ArgumentParser(description="Pose PDFium (pdfium.dll, libpdfium.so) dans third_party/pdfium/.")
    parser.add_argument("--depuis", help="un dossier ou sont deja les deux roues pypdfium2 %s" % VERSION_PYPDFIUM)
    parser.add_argument("--verifier", action="store_true", help="verifier les fichiers deja poses, sans rien telecharger")
    args = parser.parse_args()
    if args.verifier:
        return 0 if verifier() else 1
    print("PDFium %s (pypdfium2 %s, pdfium-binaries) -> %s" % (VERSION_PDFIUM, VERSION_PYPDFIUM, DESTINATION))
    for p in PLATEFORMES:
        if args.depuis:
            roue = os.path.join(args.depuis, p["roue"])
            if not os.path.isfile(roue):
                raise SystemExit("roue absente : %s" % roue)
            poser(p, roue)
        else:
            dossier = tempfile.mkdtemp(prefix="pdfium-")
            try:
                poser(p, telecharger(p, dossier))
            finally:
                shutil.rmtree(dossier, ignore_errors=True)
    return 0 if verifier() else 1


if __name__ == "__main__":
    sys.exit(main())
