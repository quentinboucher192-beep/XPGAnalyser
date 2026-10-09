#!/usr/bin/env python3
# =============================================================================
#  outils/corpus_declarations.py - 1.11.18 (refonte des scripts, lot 2) : LE CORPUS
#  DES DECLARATIONS
# -----------------------------------------------------------------------------
#  Les codes ST a blocs VAR... END_VAR ecrits dans les essais, le guide et les
#  sessions (des chaines C++, bout a bout comme le compilateur les colle), verses
#  dans tests/fixtures/decl/corpus.txt. L'essai hmidecl y verifie que
#  hmi::decl::extract lit les memes declarations que hmi::splitDeclarations, et que
#  chaque bloc recompose (compose) se relit a l'identique.
#
#      python3 outils/corpus_declarations.py            (re)ecrit le corpus
#      python3 outils/corpus_declarations.py --verifier dit s'il est a jour (code 1 sinon)
#
#  Format : pour chaque code, une ligne "@@ <n> <source>:<ligne> <octets>", puis ses
#  octets tels quels, puis une fin de ligne.
# =============================================================================
import pathlib
import re
import sys

RACINE = pathlib.Path(__file__).resolve().parent.parent
SOURCES = [
    "tests/hmi_test.cpp",
    "tests/hmi_editor_test.cpp",
    "tests/hmi_build_test.cpp",
    "tests/hmi_log_test.cpp",
    "src/hmi/HmiGuideText.cpp",
    "src/app/hmi/HmiAssist.cpp",
    "src/help/HelpTryIt.cpp",
    "tools/sessions/preparer-projet-11110.cpp",
    "tools/sessions/preparer-projet-11111.cpp",
]
SORTIE = RACINE / "tests/fixtures/decl/corpus.txt"

ECHAPPEMENTS = {"n": b"\n", "t": b"\t", "r": b"\r", "0": b"\0", "\\": b"\\", '"': b'"', "'": b"'", "?": b"?",
                "a": b"\a", "b": b"\b", "f": b"\f", "v": b"\v"}


def decoder(corps: str) -> bytes:
    """Le contenu d'une chaine C++ ordinaire (sans ses guillemets), echappements lus."""
    out = bytearray()
    i = 0
    while i < len(corps):
        c = corps[i]
        if c != "\\":
            out += c.encode("utf-8")
            i += 1
            continue
        e = corps[i + 1]
        if e == "x":                                   # \x : tous les chiffres hexadecimaux qui suivent
            j = i + 2
            while j < len(corps) and corps[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(corps[i + 2:j], 16) & 0xFF)
            i = j
        elif e in "01234567" and not (e == "0" and (i + 2 >= len(corps) or corps[i + 2] not in "01234567")):
            j = i + 1
            while j < len(corps) and j < i + 4 and corps[j] in "01234567":
                j += 1
            out.append(int(corps[i + 1:j], 8) & 0xFF)
            i = j
        else:
            out += ECHAPPEMENTS.get(e, e.encode("utf-8"))
            i += 2
    return bytes(out)


def chaines(texte: str):
    """Les chaines du source, les voisines (seulement des blancs ou des commentaires
    entre elles) collees : (ligne, octets)."""
    i, n, ligne = 0, len(texte), 1
    courante, debut, fin = None, 0, 0           # la chaine en cours de collage
    while i < n:
        c = texte[i]
        if c == "\n":
            ligne += 1
            i += 1
            continue
        if texte.startswith("//", i):
            i = texte.find("\n", i)
            i = n if i < 0 else i
            continue
        if texte.startswith("/*", i):
            j = texte.find("*/", i + 2)
            j = n if j < 0 else j + 2
            ligne += texte.count("\n", i, j)
            i = j
            continue
        if c.isspace():
            i += 1
            continue
        m = re.compile(r'(u8|u|U|L)?R"([^(\s]*)\(').match(texte, i)
        if m and (i == 0 or not (texte[i - 1].isalnum() or texte[i - 1] == "_")):
            delim = ")" + m.group(2) + '"'
            j = texte.find(delim, m.end())
            j = n if j < 0 else j
            morceau = texte[m.end():j].encode("utf-8")
            if courante is None:
                courante, debut = b"", ligne
            courante += morceau
            ligne += texte.count("\n", i, j)
            i = j + len(delim)
            continue
        m = re.compile(r'(u8|u|U|L)?"').match(texte, i)
        if m and (i == 0 or not (texte[i - 1].isalnum() or texte[i - 1] == "_") or m.group(1)):
            j = m.end()
            while j < n and texte[j] != '"':
                j += 2 if texte[j] == "\\" else 1
            if courante is None:
                courante, debut = b"", ligne
            courante += decoder(texte[m.end():j])
            i = j + 1
            continue
        if c == "'":                                   # un caractere : '"' ne commence pas une chaine
            j = i + 1
            while j < n and texte[j] != "'":
                j += 2 if texte[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
        if courante is not None:
            yield debut, courante
            courante = None
    if courante is not None:
        yield debut, courante


def corpus() -> bytes:
    out = bytearray()
    vus = set()
    numero = 0
    for nom in SOURCES:
        chemin = RACINE / nom
        if not chemin.exists():
            continue
        for ligne, octets in chaines(chemin.read_text(encoding="utf-8")):
            if b"END_VAR" not in octets.upper() or octets in vus:
                continue
            vus.add(octets)
            numero += 1
            out += f"@@ {numero} {nom}:{ligne} {len(octets)}\n".encode("utf-8") + octets + b"\n"
    return bytes(out)


def main() -> int:
    contenu = corpus()
    if "--verifier" in sys.argv:
        ok = SORTIE.exists() and SORTIE.read_bytes() == contenu
        print("corpus a jour" if ok else f"corpus a refaire : python3 outils/corpus_declarations.py ({SORTIE})")
        return 0 if ok else 1
    SORTIE.parent.mkdir(parents=True, exist_ok=True)
    SORTIE.write_bytes(contenu)
    print(f"{SORTIE.relative_to(RACINE)} : {contenu.count(b'@@ ')} codes, {len(contenu)} octets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
