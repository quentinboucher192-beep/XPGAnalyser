#!/usr/bin/env python3
# =============================================================================
#  outils/compter_lignes.py - 1.11.7 : le nombre de lignes de code de l'application
# -----------------------------------------------------------------------------
#  Compte les lignes des fichiers .h, .hpp, .c et .cpp du depot (src, tests,
#  tools ; pas third_party ni les dossiers de construction) et ecrit
#  src/core/CodeStats.hpp, que l'ecran d'accueil (le menu principal) et
#  « A propos » affichent. A relancer avant chaque livraison :
#
#      python3 outils/compter_lignes.py
# =============================================================================
import datetime
import os
import sys

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOSSIERS = ["src", "tests", "tools"]
EXTENSIONS = [".h", ".hpp", ".c", ".cpp"]
SORTIE = os.path.join(RACINE, "src", "core", "CodeStats.hpp")


def compter():
    stats = {e: [0, 0] for e in EXTENSIONS}   # fichiers, lignes
    for d in DOSSIERS:
        for base, dirs, files in os.walk(os.path.join(RACINE, d)):
            dirs[:] = [x for x in dirs if x not in ("third_party", "build", "build-linux", "build-mingw")]
            for f in files:
                ext = os.path.splitext(f)[1].lower()
                if ext not in stats:
                    continue
                chemin = os.path.join(base, f)
                if os.path.abspath(chemin) == os.path.abspath(SORTIE):
                    continue                          # pas lui-meme : le compte ne bouge pas a chaque passage
                with open(chemin, "rb") as fh:
                    data = fh.read()
                lignes = data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)
                stats[ext][0] += 1
                stats[ext][1] += lignes
    return stats


def main():
    stats = compter()
    total_f = sum(v[0] for v in stats.values())
    total_l = sum(v[1] for v in stats.values())
    jour = datetime.date.today().strftime("%d/%m/%Y")
    lignes = [
        "// =============================================================================",
        "//  core/CodeStats.hpp - 1.11.7 : le nombre de lignes de code de l'application",
        "// -----------------------------------------------------------------------------",
        "//  ECRIT PAR outils/compter_lignes.py (ne pas modifier a la main) : les fichiers",
        "//  .h, .hpp, .c et .cpp de src, tests et tools. L'ecran d'accueil et A propos",
        "//  l'affichent.",
        "// =============================================================================",
        "#pragma once",
        "",
        "namespace xpg::codestats {",
        "",
        "struct ByExtension {",
        "    const char* extension;",
        "    int         files;",
        "    long long   lines;",
        "};",
        "",
        "inline constexpr ByExtension kByExtension[] = {",
    ]
    for e in EXTENSIONS:
        lignes.append('    {"%s", %d, %d},' % (e, stats[e][0], stats[e][1]))
    lignes += [
        "};",
        "inline constexpr int        kTotalFiles = %d;" % total_f,
        "inline constexpr long long  kTotalLines = %d;" % total_l,
        'inline constexpr const char* kCountedOn = "%s";' % jour,
        "",
        "} // namespace xpg::codestats",
        "",
    ]
    with open(SORTIE, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lignes))
    for e in EXTENSIONS:
        print("%-5s %5d fichiers %9d lignes" % (e, stats[e][0], stats[e][1]))
    print("total %5d fichiers %9d lignes -> %s" % (total_f, total_l, os.path.relpath(SORTIE, RACINE)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
