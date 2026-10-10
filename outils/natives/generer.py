# -*- coding: utf-8 -*-
# =============================================================================
#  outils/natives/generer.py - 1.12.0 : ECRIT src/hmi/HmiNativesData.cpp
# -----------------------------------------------------------------------------
#  Depuis catalogue.py (la seule source des fiches des natives). Les textes sont
#  ecrits en octets UTF-8 echappes (\xC3\xA9), comme partout dans le code : le
#  fichier reste en ASCII.
#
#    python3 outils/natives/generer.py        (depuis la racine du depot)
# =============================================================================
import os
import sys

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
import catalogue as cat  # noqa: E402

RACINE = os.path.dirname(os.path.dirname(ICI))
SORTIE = os.path.join(RACINE, "src", "hmi", "HmiNativesData.cpp")
HEX = set("0123456789abcdefABCDEF")


def cstr(s):
    """Un litteral C++ : ASCII, octets UTF-8 en \\xHH, coupe apres un \\x suivi d'un chiffre hexadecimal."""
    if s is None:
        return '""'
    out = ['"']
    data = s.encode("utf-8")
    prev_hex = False
    for b in data:
        ch = chr(b)
        if b >= 0x80:
            out.append("\\x%02X" % b)
            prev_hex = True
            continue
        if prev_hex and ch in HEX:
            out.append('" "')
        prev_hex = False
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def mode(code, name):
    if code is None or code == "":
        return "None"
    if "pas de " in code or "pas d'" in code or "pas dans la biblioth" in code:
        return "None"
    if name.split("(")[0] in code:
        return "Same"
    return "Equivalent"


def main():
    lines = []
    w = lines.append
    w("// =============================================================================")
    w("//  hmi/HmiNativesData.cpp - 1.12.0 : LE CATALOGUE DES NATIVES (GENERE)")
    w("// -----------------------------------------------------------------------------")
    w("//  Ecrit par outils/natives/generer.py depuis outils/natives/catalogue.py : ne pas")
    w("//  le modifier a la main, changer le catalogue et relancer le generateur.")
    w("// =============================================================================")
    w('#include "HmiNatives.hpp"')
    w("")
    w("namespace hmi::natives {")
    w("")
    # ---- categories
    w("const std::vector<Category>& categories() {")
    w("    static const std::vector<Category> k = {")
    for cid, title, short, group in cat.CATS:
        w("        {%s, %s, %s, %s}," % (cstr(cid), cstr(title), cstr(short), cstr(group)))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    # ---- fonctions
    w("const std::vector<Function>& functions() {")
    w("    static const std::vector<Function> k = {")
    for f in cat.F:
        name = f["name"]
        params = ", ".join("{%s, %s, %s, %s}" % (cstr(p[0]), cstr(p[1]), cstr(p[2]), "true" if p[3] else "false") for p in f.get("params", []))
        e = f.get("essai")
        ex = "{}"
        if e:
            ex = "{%s, %s, %s, %s}" % (cstr(e[0]), cstr(e[1]), cstr(e[2]), cstr(e[3] if len(e) > 3 else ""))
        ihm = name.startswith("IHM_")
        ro = bool(f.get("ro", False))
        dialect = f["cat"] in ("map", "ref", "liste")   # 1.12.2 : les listes, comme les MAP
        expression = not dialect and (not ihm or ro)
        notes = ", ".join(cstr(n) for n in f.get("notes", []))
        w("        {%s, %s, {%s}, %s, %s," % (cstr(name), cstr(f["cat"]), params, cstr(f["ret"]), cstr(f["short"])))
        w("         %s," % cstr(f["st"]))
        w("         %s," % cstr(f["c"]))
        w("         %s," % cstr(f["cpp"]))
        w("         Avail::%s, Avail::%s, %s, %s, %s, {%s}}," % (mode(f["c"], name), mode(f["cpp"], name), "true" if expression else "false",
                                                             "true" if ro else "false", ex, notes))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    # ---- operateurs
    w("const std::vector<Operator>& operators() {")
    w("    static const std::vector<Operator> k = {")
    for sym, nom, phr, st, c, cpp, e in cat.OPS:
        ex = "{%s, %s, %s, %s}" % (cstr(e[0]), cstr(e[1]), cstr(e[2]), '""') if e else "{}"
        w("        {%s, %s, %s, %s, %s, %s, %s}," % (cstr(sym), cstr(nom), cstr(phr), cstr(st), cstr(c), cstr(cpp), ex))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    # ---- instructions
    w("const std::vector<Instruction>& instructions() {")
    w("    static const std::vector<Instruction> k = {")
    for kw, nom, forme, st, c, cpp in cat.INSTR:
        w("        {%s, %s, %s, %s, %s, %s}," % (cstr(kw), cstr(nom), cstr(forme), cstr(st), cstr(c), cstr(cpp)))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    # ---- enumerations natives
    w("const std::vector<NativeEnum>& enums() {")
    w("    static const std::vector<NativeEnum> k = {")
    for e in cat.ENUMS:
        nom, phr, fonction, rang, valeurs = e[:5]
        proprietes = e[5] if len(e) > 5 else []      # 1.12.1 : les proprietes des objets qui la prennent
        vals = ", ".join("{%s, %d, %s, %s}" % (cstr(v[0]), v[1], cstr(v[2]), cstr(v[3])) for v in valeurs)
        uses = ", ".join("{%s, %s}" % (cstr(g), cstr(k)) for g, k in proprietes)
        w("        {%s, %s, %s, %d, {%s}, {%s}}," % (cstr(nom), cstr(phr), cstr(fonction), rang, vals, uses))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    # ---- les generiques (1.12.1)
    w("const std::vector<Generic>& generics() {")
    w("    static const std::vector<Generic> k = {")
    for nom, membres, phr in cat.GENERIQUES:
        w("        {%s, %s, %s}," % (cstr(nom), cstr(membres), cstr(phr)))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    # ---- ce que les types ont en plus du registre
    w("const std::vector<TypeExtra>& typeExtras() {")
    w("    static const std::vector<TypeExtra> k = {")
    for t, lits in cat.LITERALS.items():
        info = cat.TYPE_EXTRA.get(t, {})
        w("        {%s, %s, %s, %s, %s, {%s}, {%s}}," % (
            cstr(t), cstr(info.get("c", "")), cstr(info.get("cpp", "")), cstr(info.get("def", "")), cstr(info.get("modbus", "")),
            ", ".join(cstr(l) for l in lits), ", ".join(cstr(n) for n in info.get("notes", []))))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    # ---- les types construits
    w("const std::vector<Constructed>& constructed() {")
    w("    static const std::vector<Constructed> k = {")
    for a, b, c, d, e, f in cat.CONSTRUCTED:
        w("        {%s, %s, %s, %s, %s, %s}," % (cstr(a), cstr(b), cstr(c), cstr(f), cstr(d), cstr(e)))
    w("    };")
    w("    return k;")
    w("}")
    w("")
    w("} // namespace hmi::natives")
    text = "\n".join(lines) + "\n"
    with open(SORTIE, "w", encoding="ascii") as out:
        out.write(text)
    print("ecrit : %s (%d fonctions, %d operateurs, %d instructions, %d enumerations)" % (
        os.path.relpath(SORTIE, RACINE), len(cat.F), len(cat.OPS), len(cat.INSTR), len(cat.ENUMS)))


if __name__ == "__main__":
    main()
