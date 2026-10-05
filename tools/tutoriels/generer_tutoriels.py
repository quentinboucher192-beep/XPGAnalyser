#!/usr/bin/env python3
"""generer_tutoriels.py : les tutoriels ecrits (tools/tutoriels/*.tuto) embarques dans l'appli.

Ecrit src/help/TutorialTexts.cpp : le texte de chaque fichier, tel quel, en chaines C++ ASCII
(les octets UTF-8 en \\x, comme hmi/HmiGuideText.cpp). L'appli n'a ainsi aucun fichier a trouver
a l'execution (Windows, installation), et c'est le meme lecteur (help::Tutorial::parse) qui lit le
texte embarque et un fichier. Conception : v111/agents/CONCEPTION-T1.md, section 3.

    python3 tools/tutoriels/generer_tutoriels.py --cpp src/help/TutorialTexts.cpp
    python3 tools/tutoriels/generer_tutoriels.py --verifier src/help/TutorialTexts.cpp   # 1 si perime
"""
import argparse
import pathlib
import sys

ICI = pathlib.Path(__file__).resolve().parent
HEX = set("0123456789abcdefABCDEF")


def chaine(ligne: str) -> str:
    """Une ligne en litteral C++ ASCII. Apres un \\x, un chiffre hexadecimal ferme la chaine."""
    out, apres_hex = [], False
    for b in ligne.encode("utf-8"):
        c = chr(b)
        if b >= 0x80:
            out.append("\\x%02X" % b)
            apres_hex = True
            continue
        if apres_hex and c in HEX:
            out.append('" "')
        apres_hex = False
        if c == "\\":
            out.append("\\\\")
        elif c == '"':
            out.append('\\"')
        elif c == "\t":
            out.append("\\t")
        elif b < 0x20 or b == 0x7F:
            out.append("\\x%02X" % b)
            apres_hex = True
        else:
            out.append(c)
    return '"' + "".join(out) + '\\n"'


def generer(dossier: pathlib.Path) -> str:
    fichiers = sorted(dossier.glob("*.tuto"))
    l = [
        "// =============================================================================",
        "//  help/TutorialTexts.cpp - les tutoriels ecrits, embarques dans l'appli",
        "// -----------------------------------------------------------------------------",
        "//  GENERE par tools/tutoriels/generer_tutoriels.py depuis tools/tutoriels/*.tuto :",
        "//  NE PAS MODIFIER A LA MAIN. Changer le fichier .tuto, puis :",
        "//      python3 tools/tutoriels/generer_tutoriels.py --cpp src/help/TutorialTexts.cpp",
        "// =============================================================================",
        '#include "TutorialLaunch.hpp"',
        "",
        "namespace help {",
        "",
        "const std::vector<EmbeddedTutorial>& embeddedTutorials() {",
        "    static const std::vector<EmbeddedTutorial> all = {",
    ]
    for f in fichiers:
        texte = f.read_text(encoding="utf-8").replace("\r\n", "\n")
        lignes = texte.split("\n")
        if lignes and lignes[-1] == "":
            lignes.pop()
        l.append('        {%s",' % chaine(f.name)[:-3])
        for i, x in enumerate(lignes):
            fin = "}," if i == len(lignes) - 1 else ""
            l.append("         " + chaine(x) + fin)
        if not lignes:
            l.append('         ""},')
    l += ["    };", "    return all;", "}", "", "} // namespace help", ""]
    return "\n".join(l)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--dossier", default=str(ICI), help="le dossier des .tuto (defaut : celui du script)")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--cpp", help="ecrire ce fichier")
    g.add_argument("--verifier", help="rendre 1 si ce fichier n'est pas a jour")
    a = p.parse_args()
    texte = generer(pathlib.Path(a.dossier))
    if a.cpp:
        pathlib.Path(a.cpp).write_text(texte, encoding="ascii", newline="\n")
        print("%s : %d tutoriel(s)" % (a.cpp, texte.count("\n        {")))
        return 0
    actuel = pathlib.Path(a.verifier).read_text(encoding="ascii") if pathlib.Path(a.verifier).exists() else ""
    if actuel != texte:
        print("%s n'est pas a jour : python3 tools/tutoriels/generer_tutoriels.py --cpp %s" % (a.verifier, a.verifier))
        return 1
    print("%s : a jour" % a.verifier)
    return 0


if __name__ == "__main__":
    sys.exit(main())
