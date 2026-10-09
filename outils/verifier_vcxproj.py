#!/usr/bin/env python3
# =============================================================================
#  outils/verifier_vcxproj.py - 1.11.17 (refonte des scripts, lot 0) : le projet
#  Visual Studio suit-il la construction CMake ?
# -----------------------------------------------------------------------------
#  CMake ramasse les sources par dossier (xpg_sources, CMakeLists.txt) ;
#  XpgAnalyzer.vcxproj les nomme une a une. Depuis la 1.11.3, vingt et un .cpp
#  ajoutes cote CMake n'y etaient pas : outils\build.bat (MSBuild) ne liait plus.
#  Ce script compare les deux listes ; ctest le lance (essai "vcxproj"), pour que
#  l'ecart ne revienne pas sans bruit.
#
#      python3 outils/verifier_vcxproj.py             le dit (sortie 1 s'il manque un fichier)
#      python3 outils/verifier_vcxproj.py --corriger  ajoute les manquants, a leur place
#                                                     alphabetique, dans .vcxproj et .filters
#
#  Les dossiers suivent ceux de CMakeLists.txt (bibliotheques xpg_core, xpg_xls,
#  xpg_import, xpg_ui, xpg_help, xpg_hmi, l'application) : a tenir ensemble.
#  third_party/miniz.c est nomme a part (CMake l'ajoute s'il est la).
# =============================================================================
import pathlib
import re
import sys

RACINE = pathlib.Path(__file__).resolve().parent.parent
DOSSIERS = ["core", "xls", "domain", "import", "export", "project", "sim", "ui", "menu", "help", "hmi", "app"]


def attendus():
    cpp = set()
    for d in DOSSIERS:
        for f in (RACINE / "src" / d).rglob("*.cpp"):
            r = f.relative_to(RACINE).as_posix()
            if not r.endswith("compile_check.cpp"):     # une sonde du dialecte, hors bibliotheque
                cpp.add(r)
    cpp.add("src/main.cpp")
    for f in (RACINE / "src" / "platform").glob("Sdl*.cpp"):
        cpp.add(f.relative_to(RACINE).as_posix())
    cpp.add("src/platform/FontAtlas.cpp")
    hpp = {f.relative_to(RACINE).as_posix() for ext in ("*.hpp", "*.h") for f in (RACINE / "src").rglob(ext)}
    return cpp, hpp


def win(p):
    return p.replace("/", "\\")


def lire(chemin):
    brut = chemin.read_bytes()
    return brut.startswith(b"\xef\xbb\xbf"), brut.decode("utf-8-sig").split("\r\n")


def ecrire(chemin, bom, lignes):
    chemin.write_bytes((b"\xef\xbb\xbf" if bom else b"") + "\r\n".join(lignes).encode("utf-8"))


def presents(lignes, balise, motif):
    pat = re.compile(motif % balise)
    vus = {}
    for i, l in enumerate(lignes):
        m = pat.match(l)
        if m:
            vus[m.group(2).replace("\\", "/")] = i
    return vus


def place(vus, f):
    """L'index ou inserer f : apres le dernier de son dossier qui le precede."""
    d = f.rsplit("/", 1)[0]
    meme = sorted((k.lower(), i) for k, i in vus.items() if k.rsplit("/", 1)[0] == d)
    avant = [i for k, i in meme if k < f.lower()]
    if avant:
        return max(avant), True
    if meme:
        return min(i for _, i in meme), False
    return max(vus.values()), True


def corriger_vcxproj(manquants_cpp, manquants_hpp):
    chemin = RACINE / "XpgAnalyzer.vcxproj"
    bom, lignes = lire(chemin)
    for balise, fichiers in (("ClCompile", manquants_cpp), ("ClInclude", manquants_hpp)):
        for f in sorted(fichiers, key=str.lower):
            vus = presents(lignes, balise, r'^(\s*)<%s Include="([^"]+)" />$')
            i, apres = place(vus, f)
            retrait = re.match(r"^(\s*)", lignes[i]).group(1)
            lignes.insert(i + 1 if apres else i, '%s<%s Include="%s" />' % (retrait, balise, win(f)))
    ecrire(chemin, bom, lignes)


def corriger_filters(manquants_cpp, manquants_hpp):
    chemin = RACINE / "XpgAnalyzer.vcxproj.filters"
    bom, lignes = lire(chemin)
    filtres = set(re.findall(r'<Filter Include="([^"]+)">', "\n".join(lignes)))
    for balise, fichiers in (("ClCompile", manquants_cpp), ("ClInclude", manquants_hpp)):
        for f in sorted(fichiers, key=str.lower):
            vus = presents(lignes, balise, r'^(\s*)<%s Include="([^"]+)">$')
            d = f.rsplit("/", 1)[0]
            filtre = win(d[len("src/"):]) if d.startswith("src/") else ""
            if filtre not in filtres:
                sys.exit("verifier_vcxproj : pas de filtre %r pour %s (a creer dans le .filters)" % (filtre, f))
            i, apres = place(vus, f)
            bloc = ['    <%s Include="%s">' % (balise, win(f)), "      <Filter>%s</Filter>" % filtre, "    </%s>" % balise]
            j = i + 3 if apres else i
            lignes[j:j] = bloc
    ecrire(chemin, bom, lignes)


def main():
    cpp, hpp = attendus()
    _, lignes = lire(RACINE / "XpgAnalyzer.vcxproj")
    a_cpp = set(presents(lignes, "ClCompile", r'^(\s*)<%s Include="([^"]+)" />$')) - {"third_party/miniz.c"}
    a_hpp = set(presents(lignes, "ClInclude", r'^(\s*)<%s Include="([^"]+)" />$'))
    manquants_cpp, manquants_hpp = cpp - a_cpp, hpp - a_hpp
    en_trop = sorted((a_cpp - cpp) | (a_hpp - hpp))
    if "--corriger" in sys.argv[1:] and (manquants_cpp or manquants_hpp):
        corriger_vcxproj(manquants_cpp, manquants_hpp)
        corriger_filters(manquants_cpp, manquants_hpp)
        print("XpgAnalyzer.vcxproj et .filters : %d .cpp et %d en-tetes ajoutes" % (len(manquants_cpp), len(manquants_hpp)))
        return 0
    for f in sorted(manquants_cpp | manquants_hpp):
        print("absent du vcxproj : %s" % f)
    for f in en_trop:
        print("dans le vcxproj mais plus dans src : %s" % f)
    if manquants_cpp or manquants_hpp or en_trop:
        print("XpgAnalyzer.vcxproj n'est pas a jour : python3 outils/verifier_vcxproj.py --corriger")
        return 1
    print("XpgAnalyzer.vcxproj : %d .cpp et %d en-tetes, comme CMake" % (len(cpp), len(hpp)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
