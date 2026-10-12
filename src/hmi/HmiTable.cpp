#include "HmiTable.hpp"

#include "HmiChoiceLists.hpp"   // les colonnes d'un tableau d'avant (rowsFromColumns, sourceColumns)
#include "HmiDisplay.hpp"       // l'unite et le format d'une variable (Unites et formats)
#include "HmiEnums.hpp"
#include "HmiMarkers.hpp"
#include "HmiSources.hpp"
#include "HmiTypeForms.hpp"
#include "HmiTypes.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>

namespace hmi::table {

namespace {

std::string trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return std::string(s);
}
bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
// Une lettre d'un nom : ASCII, chiffre, _ ou un octet d'UTF-8 (Cle, Cl\xC3\xA9).
bool identChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u >= 0x80;
}
bool isKeyWord(std::string_view w) { return same(w, "Cle") || w == "Cl\xC3\xA9" || w == "CL\xC3\x89"; }
std::string joinNames(const std::vector<TypeMember>& ms, std::size_t max = 6) {
    std::string out;
    for (std::size_t i = 0; i < ms.size() && i < max; ++i) out += (i ? ", " : "") + ms[i].name;
    if (ms.size() > max) out += "\xE2\x80\xA6";
    return out;
}

// ---- un chemin : sa racine ([*] ou un nom) et ses morceaux (.Membre, [indice]) -------------
struct Seg {
    bool        member{false};
    std::string name;     // .Membre
    std::string index;    // [indice] (le texte entre les crochets)
};
bool splitPath(std::string_view s, std::string& root, std::vector<Seg>& segs) {
    root.clear();
    segs.clear();
    std::size_t i = 0;
    if (s.substr(0, 3) == "[*]") {
        root = "[*]";
        i = 3;
    } else {
        std::size_t j = 0;
        while (j < s.size() && identChar(s[j])) ++j;
        if (j == 0 || std::isdigit(static_cast<unsigned char>(s[0])) != 0) return false;
        root = std::string(s.substr(0, j));
        i = j;
    }
    while (i < s.size()) {
        if (s[i] == '.') {
            std::size_t j = i + 1;
            while (j < s.size() && identChar(s[j])) ++j;
            if (j == i + 1) return false;
            segs.push_back({true, std::string(s.substr(i + 1, j - i - 1)), {}});
            i = j;
        } else if (s[i] == '[') {
            int depth = 0;
            std::size_t j = i;
            for (; j < s.size(); ++j) {
                if (s[j] == '[') ++depth;
                else if (s[j] == ']' && --depth == 0) break;
            }
            if (j >= s.size()) return false;
            segs.push_back({false, {}, trimmed(s.substr(i + 1, j - i - 1))});
            i = j + 1;
        } else {
            return false;
        }
    }
    return true;
}
bool isPathLike(std::string_view s) {
    std::string root;
    std::vector<Seg> segs;
    if (!splitPath(s, root, segs)) return false;
    return !same(root, "TRUE") && !same(root, "FALSE") && !same(root, "NOT");
}
std::string pathText(std::string_view root, const std::vector<Seg>& segs, std::size_t upto) {
    std::string out(root);
    for (std::size_t k = 0; k < upto && k < segs.size(); ++k) out += segs[k].member ? "." + segs[k].name : "[" + segs[k].index + "]";
    return out;
}
bool structType(const Project& p, std::string_view type) { return !types::membersOf(p, type).empty(); }
bool collectionForm(typeform::Form f) { return f != typeform::Form::Simple && f != typeform::Form::Tuple; }

// ---- le type d'un chemin, sans moteur --------------------------------------------------------
struct PathType {
    bool        ok{false};
    bool        unknownRoot{false};
    bool        rowKey{false};       // [*].Cle, [*].Membre, [*].Nom : la cle de la ligne (ne s'ecrit pas)
    bool        constant{false};     // une enumeration : rien ne s'ecrit
    std::string type;
    std::string error, fix;
    std::vector<std::string> lists;  // Debits (Debits[*])
};
PathType resolvePath(const Project& p, const Source& src, std::string_view text) {
    PathType r;
    std::string root;
    std::vector<Seg> segs;
    if (!splitPath(text, root, segs)) {
        r.error = "\xC2\xAB " + std::string(text) + " \xC2\xBB illisible";
        return r;
    }
    std::string type;
    std::size_t k = 0;
    if (root == "[*]") {
        switch (src.kind) {
            case SourceKind::None:
            case SourceKind::Error:
                r.error = "[*] d\xC3\xA9signe l'\xC3\xA9l\xC3\xA9ment de la ligne : il faut \xC2\xAB Lignes depuis \xC2\xBB. Sinon, nomme la liste de la "
                          "colonne : Debits[*].";
                return r;
            case SourceKind::Unknown:
                r.ok = true;            // un parametre, une formule : connus en marche
                return r;
            case SourceKind::Array:
            case SourceKind::List:
                type = src.element;
                break;
            case SourceKind::Map:
                if (!segs.empty() && segs[0].member && isKeyWord(segs[0].name)) {
                    type = src.key.empty() ? "STRING" : src.key;
                    r.rowKey = true;
                    k = 1;
                } else if (!segs.empty() && segs[0].member && same(segs[0].name, "Valeur")) {
                    type = src.element;
                    k = 1;
                } else {
                    r.error = "une ligne de MAP a deux cases : [*].Cle (la cl\xC3\xA9) et [*].Valeur (sa valeur).";
                    r.fix = "[*].Valeur";
                    return r;
                }
                break;
            case SourceKind::Struct:
                if (!segs.empty() && segs[0].member && same(segs[0].name, "Membre")) {
                    type = "STRING";
                    r.rowKey = true;
                    k = 1;
                } else if (!segs.empty() && segs[0].member && same(segs[0].name, "Valeur")) {
                    type = "ANY";
                    k = 1;
                } else {
                    r.error = "une ligne de structure a deux cases : [*].Membre (son nom) et [*].Valeur (sa valeur).";
                    r.fix = "[*].Valeur";
                    return r;
                }
                break;
            case SourceKind::Enum:
                r.constant = true;
                if (!segs.empty() && segs[0].member && same(segs[0].name, "Nom")) type = "STRING";
                else if (!segs.empty() && segs[0].member && same(segs[0].name, "Valeur")) type = "DINT";
                else if (!segs.empty() && segs[0].member && same(segs[0].name, "Texte")) type = "STRING";
                else {
                    r.error = "une ligne d'\xC3\xA9num\xC3\xA9ration a [*].Nom, [*].Valeur et [*].Texte.";
                    r.fix = "[*].Nom";
                    return r;
                }
                r.rowKey = same(segs[0].name, "Nom");
                k = 1;
                break;
        }
    } else {
        const Variable* v = p.variable(root);
        if (!v) {
            if (segs.empty() && findEnumeration(p, root)) {
                r.error = root + " est une \xC3\xA9num\xC3\xA9ration : pour une ligne par valeur, mets-la dans \xC2\xAB Lignes depuis \xC2\xBB.";
                return r;
            }
            r.unknownRoot = true;
            return r;
        }
        type = v->type;
        root = v->name;
    }
    for (; k < segs.size(); ++k) {
        const auto& sg = segs[k];
        const auto shape = typeform::decompose(type);
        const std::string here = pathText(root, segs, k);
        if (!sg.member) {
            if (!collectionForm(shape.form)) {
                r.error = here + " n'est pas un tableau ni une LIST : pas d'indice.";
                return r;
            }
            if (sg.index == "#") {
                r.error = "[#] est le num\xC3\xA9ro de la ligne : il s'\xC3\xA9" "crit seul.";
                return r;
            }
            if (sg.index == "*") {
                if (root == "[*]") {
                    r.error = "[*][*] : un seul [*] par chemin.";
                    return r;
                }
                if (shape.form == typeform::Form::Map) {
                    r.error = here + " est une MAP : elle se parcourt par \xC2\xAB Lignes depuis \xC2\xBB (une ligne par cl\xC3\xA9), ou par une cl\xC3\xA9 : "
                              + here + "[[*].Cle].";
                    return r;
                }
                r.lists.push_back(here);
            }
            type = shape.element;
        } else {
            if (collectionForm(shape.form)) {
                r.error = here + " est une collection : \xC3\xA9" "cris " + here + "[*]." + sg.name + " pour le membre de chaque \xC3\xA9l\xC3\xA9ment.";
                r.fix = here + "[*]." + sg.name;
                return r;
            }
            const auto members = types::membersOf(p, type);
            if (members.empty()) {
                r.error = "." + sg.name + " : " + (type.empty() ? std::string("ce type") : type) + " n'a pas de membres.";
                return r;
            }
            const auto it = std::find_if(members.begin(), members.end(), [&](const TypeMember& m) { return same(m.name, sg.name); });
            if (it == members.end()) {
                r.error = type + " n'a pas de membre \xC2\xAB " + sg.name + " \xC2\xBB (" + joinNames(members) + ").";
                return r;
            }
            type = it->type;
        }
    }
    const std::string whole = pathText(root, segs, segs.size());
    const auto shape = typeform::decompose(type);
    if (collectionForm(shape.form)) {
        const bool st = structType(p, shape.element);
        r.error = whole + " est une collection (" + type + ") : pour une valeur par ligne, \xC3\xA9" "cris " + whole + "[*]"
                + (st ? "." + types::membersOf(p, shape.element).front().name : std::string{}) + ".";
        r.fix = whole + "[*]" + (st ? "." + types::membersOf(p, shape.element).front().name : std::string{});
        return r;
    }
    if (shape.form == typeform::Form::Tuple) {
        r.error = whole + " est un tuple : choisis un de ses \xC3\xA9l\xC3\xA9ments (" + whole + ".Item1, " + whole + ".Item2\xE2\x80\xA6).";
        r.fix = whole + ".Item1";
        return r;
    }
    if (structType(p, type)) {
        const auto members = types::membersOf(p, type);
        std::string some;
        int n = 0;
        for (const auto& m : members) {
            if (structType(p, m.type)) continue;
            some += (n ? ", " : "") + whole + "." + m.name;
            if (++n == 3) break;
        }
        r.error = whole + " est un " + type + " : une structure n'a pas de case. Choisis un membre (" + some + "\xE2\x80\xA6).";
        for (const auto& m : members)
            if (!structType(p, m.type)) { r.fix = whole + "." + m.name; break; }
        return r;
    }
    r.ok = true;
    r.type = type;
    return r;
}

// Le bas d'une liste nommee (Debits[*] : la borne de Debits ; une LIST : 0).
long long lowOf(const Project* p, std::string_view listPath) {
    if (!p) return 0;
    std::string root;
    std::vector<Seg> segs;
    if (!splitPath(listPath, root, segs) || root == "[*]") return 0;
    const Variable* v = p->variable(root);
    if (!v) return 0;
    std::string type = v->type;
    for (const auto& sg : segs) {
        if (sg.member) {
            const auto ms = types::membersOf(*p, type);
            const auto it = std::find_if(ms.begin(), ms.end(), [&](const TypeMember& m) { return same(m.name, sg.name); });
            if (it == ms.end()) return 0;
            type = it->type;
        } else {
            type = typeform::decompose(type).element;
        }
    }
    types::Spec sp;
    if (types::parseSpec(type, sp) && sp.array()) return sp.low[0];
    return 0;
}

// Le format d'un trou ({X:0.0}) : apres le dernier ':' si ce qui suit n'est pas une expression.
void splitHole(std::string_view inner, std::string& expr, std::string& format) {
    expr = trimmed(inner);
    format.clear();
    const auto colon = inner.rfind(':');
    if (colon == std::string_view::npos || colon == 0) return;
    const std::string after = trimmed(inner.substr(colon + 1));
    if (after.empty() || after.find_first_of("[]()'\" =") != std::string::npos) return;
    if (inner[colon - 1] == ':' || (colon + 1 < inner.size() && inner[colon + 1] == '=')) return;   // := n'est pas un format
    expr = trimmed(inner.substr(0, colon));
    format = after;
}
// Les trous d'un texte : {expr[:format]}.
std::vector<std::string> holesOf(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while ((i = s.find('{', i)) != std::string_view::npos) {
        const auto close = s.find('}', i + 1);
        if (close == std::string_view::npos) break;
        out.emplace_back(s.substr(i + 1, close - i - 1));
        i = close + 1;
    }
    return out;
}
// Le seul trou d'un texte, s'il n'y en a qu'un et que c'est un chemin ; vide sinon.
std::string singleHolePath(std::string_view s) {
    const auto holes = holesOf(s);
    if (holes.size() != 1) return {};
    std::string expr, fmt;
    splitHole(holes[0], expr, fmt);
    return isPathLike(expr) ? expr : std::string{};
}

// La reecriture d'un contenu : la compilation (row nul : [*] -> __TABLE_ELT__...) ou le chemin d'une
// ligne (row : [*] -> Cuves[2]...). bad : un morceau qui ne s'ecrit pas ([#]).
std::string rewrite(std::string_view s, const Source& src, const Project* p, std::vector<std::string>* lists, const RowPaths* row,
                    bool* bad) {
    std::string out;
    std::size_t i = 0;
    bool quoted = false;
    const auto wordAt = [&](std::size_t at, std::string_view w) {
        if (at + w.size() > s.size()) return false;
        if (!same(s.substr(at, w.size()), w)) return false;
        return at + w.size() == s.size() || !identChar(s[at + w.size()]);
    };
    while (i < s.size()) {
        const char ch = s[i];
        if (ch == '\'') {
            quoted = !quoted;
            out += ch;
            ++i;
            continue;
        }
        if (quoted) {
            out += ch;
            ++i;
            continue;
        }
        if (s.compare(i, 3, "[#]") == 0) {
            if (row) {
                if (bad) *bad = true;
                return {};
            }
            out += kRow;
            i += 3;
            continue;
        }
        if (s.compare(i, 3, "[*]") == 0) {
            std::size_t b = out.size();
            const bool after = b > 0 && (identChar(out[b - 1]) || out[b - 1] == ']' || out[b - 1] == ')');
            if (after) {
                // Liste[*] : le chemin de la liste, avant les crochets.
                std::size_t a = b;
                int depth = 0;
                while (a > 0) {
                    const char c2 = out[a - 1];
                    if (c2 == ']') { ++depth; --a; continue; }
                    if (c2 == '[') {
                        if (depth == 0) break;
                        --depth;
                        --a;
                        continue;
                    }
                    if (depth > 0 || identChar(c2) || c2 == '.') { --a; continue; }
                    break;
                }
                const std::string listPath = out.substr(a, b - a);
                const long long low = lowOf(p, listPath);
                if (lists) lists->push_back(listPath);
                if (row) out += "[" + std::to_string(row->position + low) + "]";
                else out += "[" + std::string(kPos) + (low >= 0 ? " + " : " - ") + std::to_string(low >= 0 ? low : -low) + "]";
                i += 3;
                continue;
            }
            i += 3;
            const auto pseudo = [&](std::string_view w) {
                if (i < s.size() && s[i] == '.' && wordAt(i + 1, w)) {
                    i += 1 + w.size();
                    return true;
                }
                return false;
            };
            bool done = false;
            switch (src.kind) {
                case SourceKind::Map:
                    if (pseudo("Cle") || pseudo("Cl\xC3\xA9")) { out += row ? row->key : std::string(kKey); done = true; }
                    else if (pseudo("Valeur")) { out += row ? row->value : std::string(kVal); done = true; }
                    break;
                case SourceKind::Struct:
                    if (pseudo("Membre")) { out += row ? row->key : std::string(kKey); done = true; }
                    else if (pseudo("Valeur")) { out += row ? row->value : std::string(kVal); done = true; }
                    break;
                case SourceKind::Enum:
                    if (row) {
                        if (bad) *bad = true;
                        return {};
                    }
                    if (pseudo("Nom")) { out += kKey; done = true; }
                    else if (pseudo("Valeur")) { out += kVal; done = true; }
                    else if (pseudo("Texte")) { out += kTxt; done = true; }
                    break;
                default:
                    break;
            }
            if (!done) out += row ? row->element : std::string(kElt);
            continue;
        }
        out += ch;
        ++i;
    }
    return out;
}

std::string representative(std::string_view path, const Source& src, const Project* p) {
    // Un chemin d'exemple, pour Unites et formats : [*].Niveau -> Cuves[1].Niveau.
    RowPaths row;
    row.position = 0;
    if (src.kind == SourceKind::Array) row.element = src.name + "[" + std::to_string(src.low) + "]";
    else if (src.kind == SourceKind::List) row.element = src.name + "[0]";
    else if (src.kind == SourceKind::Map) row.element = row.value = src.name + "[]";
    else if (src.kind == SourceKind::Struct) row.element = row.value = src.name;
    row.key = "''";
    bool bad = false;
    return rewrite(path, src, p, nullptr, &row, &bad);
}

// Le type d'une formule, a peu pres : une comparaison, un BOOL ; une division, un REAL ; un texte, un STRING.
std::string formulaType(const Project& p, const Source& src, std::string_view f) {
    bool quoted = false;
    int depth = 0;
    bool cmp = false, div = false, text = false, real = false;
    std::string word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string u = [&] { std::string x = word; for (auto& c : x) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return x; }();
        if (depth == 0 && (u == "AND" || u == "OR" || u == "NOT" || u == "XOR")) cmp = true;
        word.clear();
    };
    for (std::size_t i = 0; i < f.size(); ++i) {
        const char c = f[i];
        if (c == '\'') { quoted = !quoted; if (quoted) text = true; continue; }
        if (quoted) continue;
        if (identChar(c)) { word += c; continue; }
        flush();
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        else if (depth == 0 && (c == '=' || c == '<' || c == '>')) cmp = true;
        else if (c == '/') div = true;
        else if (c == '.' && i > 0 && std::isdigit(static_cast<unsigned char>(f[i - 1])) != 0) real = true;
    }
    flush();
    if (cmp) return "BOOL";
    if (text && f.find("SEL") == std::string_view::npos && f.find('+') == std::string_view::npos) return "STRING";
    if (text) return "STRING";
    if (div || real) return "REAL";
    // Les chemins lus : un REAL l'emporte.
    std::string t;
    std::size_t i = 0;
    while (i < f.size()) {
        if (f.compare(i, 3, "[*]") == 0 || identChar(f[i])) {
            std::size_t j = i;
            int d = 0;
            while (j < f.size() && (identChar(f[j]) || f[j] == '.' || f[j] == '[' || f[j] == ']' || f[j] == '*' || f[j] == '#' || d > 0)) {
                if (f[j] == '[') ++d;
                else if (f[j] == ']') --d;
                ++j;
            }
            const auto r = resolvePath(p, src, f.substr(i, j - i));
            if (r.ok && (r.type == "REAL" || r.type == "LREAL")) return "REAL";
            if (r.ok && t.empty()) t = r.type;
            i = std::max(j, i + 1);
            continue;
        }
        ++i;
    }
    return t;
}

} // namespace

// ============================================================ le mode
std::string_view modeLabel(Mode m) noexcept {
    switch (m) {
        case Mode::Fixed: return "cases fixes";
        case Mode::Rows: return "lignes dynamiques";
        case Mode::File: return "fichier externe";
    }
    return "cases fixes";
}
std::string_view modeTitle(Mode m) noexcept {
    switch (m) {
        case Mode::Fixed: return "Cases fixes";
        case Mode::Rows: return "Lignes dynamiques";
        case Mode::File: return "Fichier externe";
    }
    return "Cases fixes";
}
std::string_view modeHint(Mode m) noexcept {
    switch (m) {
        case Mode::Fixed: return "un nombre fixe de lignes et de colonnes, chaque case r\xC3\xA9gl\xC3\xA9" "e dans Contenu";
        case Mode::Rows: return "des colonnes fixes, une ligne par \xC3\xA9l\xC3\xA9ment d'une source, qui la suit en marche";
        case Mode::File: return "les lignes d'un fichier du projet (CSV, Excel, JSON, SQLite)";
    }
    return {};
}
std::optional<Mode> modeFromLabel(std::string_view s) noexcept {
    for (const Mode m : kModes)
        if (same(s, modeLabel(m)) || same(s, modeTitle(m))) return m;
    return std::nullopt;
}
bool modeExplicit(const Object& o) {
    const Prop* p = o.find(kModeKey);
    return p && modeFromLabel(trimmed(p->value)).has_value();
}
Mode modeOf(const Object& o) {
    if (const Prop* p = o.find(kModeKey))
        if (const auto m = modeFromLabel(trimmed(p->value))) return *m;
    // Un tableau d'avant : l'ordre du moteur - Lignes depuis, puis le fichier, puis les cases ; des colonnes
    // dynamiques sans source (une liste par colonne) : des lignes dynamiques aussi.
    if (!trimmed(o.text("rowsFrom")).empty() || hasColumns(o)) return Mode::Rows;
    if (!trimmed(o.text("source")).empty()) return Mode::File;
    return Mode::Fixed;
}
bool modeEmpty(const Object& o, Mode m) {
    switch (m) {
        case Mode::Fixed: {
            for (const auto& row : parseCells(o.text("cells")))
                for (const auto& c : row)
                    if (!trimmed(c).empty()) return false;
            return true;
        }
        case Mode::Rows:
            return trimmed(o.text("rowsFrom")).empty() && trimmed(o.text(kColumnsKey)).empty()
                && trimmed(o.text(choices::kSourceColumnsKey)).empty();
        case Mode::File:
            return trimmed(o.text("source")).empty();
    }
    return true;
}
std::string modeSummary(const Object& o, Mode m) {
    switch (m) {
        case Mode::Fixed: {
            const auto rows = parseCells(o.text("cells"));
            std::size_t filled = 0, cols = splitSemicolons(o.text("columns")).size();
            for (const auto& row : rows) {
                cols = std::max(cols, row.size());
                for (const auto& c : row) filled += trimmed(c).empty() ? 0 : 1;
            }
            const auto n = std::max<std::size_t>(rows.size(), static_cast<std::size_t>(std::max(0.0, o.number("rows", 0))));
            if (!filled) return "aucune case remplie";
            return std::to_string(n) + " \xC3\x97 " + std::to_string(cols) + " cases, " + std::to_string(filled) + " remplie" + (filled > 1 ? "s" : "");
        }
        case Mode::Rows: {
            const std::string from = trimmed(markers::strip(o.text("rowsFrom")));
            const auto cols = columnsOf(o);
            if (from.empty() && cols.empty()) return "ni source, ni colonne";
            std::string out = from.empty() ? std::string("une liste par colonne") : "depuis " + from;
            if (!cols.empty()) out += ", " + std::to_string(cols.size()) + " colonne" + (cols.size() > 1 ? "s" : "");
            else out += ", les colonnes de la source";
            return out;
        }
        case Mode::File: {
            const std::string f = trimmed(o.text("source"));
            return f.empty() ? std::string("aucun fichier") : f;
        }
    }
    return {};
}
void clearMode(Object& o, Mode m) {
    const auto drop = [&](std::string_view key) {
        o.props.erase(std::remove_if(o.props.begin(), o.props.end(), [&](const Prop& p) { return p.key == key; }), o.props.end());
    };
    switch (m) {
        case Mode::Fixed:
            o.set("cells", "");
            drop(kAccessKey);
            break;
        case Mode::Rows:
            o.set("rowsFrom", "");
            if (Prop* p = o.find("rowsFrom")) p->expr.clear();
            for (const auto key : {kColumnsKey, std::string_view(choices::kSourceColumnsKey), kSortKey, kSortOrderKey, kPickKey, kEmptyKey}) drop(key);
            break;
        case Mode::File:
            o.set("source", "");
            break;
    }
}

// ============================================================ les colonnes
std::vector<Column> parseColumns(std::string_view text) {
    std::vector<Column> out;
    for (const auto& fields : parseCells(text)) {
        if (fields.empty() || (fields.size() == 1 && trimmed(fields[0]).empty())) continue;
        Column c;
        const auto at = [&](std::size_t i) { return i < fields.size() ? fields[i] : std::string{}; };
        c.title = at(0);
        c.content = at(1);
        c.format = at(2);
        c.unit = at(3);
        double w = 0;
        if (parseNumber(trimmed(at(4)), w) && w > 0) c.width = w;
        c.align = trimmed(at(5));
        const std::string acc = trimmed(at(6));
        c.write = same(acc, "W") || same(acc, "R/W") || same(acc, "RW");
        double lv = 0;
        if (parseNumber(trimmed(at(7)), lv) && lv > 0) c.level = static_cast<int>(lv);
        c.color = trimmed(at(8));
        c.colorExpr = at(9);
        out.push_back(std::move(c));
    }
    return out;
}
std::string formatColumns(const std::vector<Column>& cols) {
    std::vector<std::vector<std::string>> rows;
    for (const auto& c : cols)
        rows.push_back({c.title, c.content, c.format, c.unit, formatNumber(c.width), c.align, c.write ? "W" : "R",
                        c.level > 0 ? std::to_string(c.level) : std::string{}, c.color, c.colorExpr});
    return formatCells(rows);
}
bool hasColumns(const Object& o) { return !trimmed(o.text(kColumnsKey)).empty(); }
std::vector<Column> columnsOf(const Object& o) { return parseColumns(o.text(kColumnsKey)); }

// ============================================================ la source
Source analyzeSource(const Project& p, std::string_view rowsFrom) {
    Source s;
    const std::string raw = trimmed(rowsFrom);
    if (raw.empty()) return s;
    s.markers = raw.find('$') != std::string::npos;
    std::string t = trimmed(markers::strip(raw));
    if (!t.empty() && t.front() == '=') {
        s.formula = true;
        t = trimmed(std::string_view(t).substr(1));
    }
    if (t.empty()) return s;
    std::string root;
    std::vector<Seg> segs;
    if (!splitPath(t, root, segs) || root == "[*]") {
        s.kind = SourceKind::Unknown;     // une formule : connue en marche
        s.name = t;
        return s;
    }
    const Variable* v = p.variable(root);
    if (!v) {
        if (segs.empty())
            if (const HmiType* e = findEnumeration(p, root)) {
                s.kind = SourceKind::Enum;
                s.name = e->name;
                s.type = e->name;
                s.element = e->name;
                s.count = static_cast<long long>(e->values.size());
                return s;
            }
        s.kind = SourceKind::Unknown;     // un parametre de la vue : connu en marche
        s.name = t;
        return s;
    }
    std::string type = v->type;
    std::string path = v->name;
    for (std::size_t k = 0; k < segs.size(); ++k) {
        const auto& sg = segs[k];
        if (sg.member) {
            const auto ms = types::membersOf(p, type);
            const auto it = std::find_if(ms.begin(), ms.end(), [&](const TypeMember& m) { return same(m.name, sg.name); });
            if (it == ms.end()) {
                s.kind = SourceKind::Error;
                s.name = t;
                s.error = path + " n'a pas de membre \xC2\xAB " + sg.name + " \xC2\xBB" + (ms.empty() ? std::string{} : " (" + joinNames(ms) + ")") + ".";
                return s;
            }
            type = it->type;
            path += "." + it->name;
        } else {
            const auto shape = typeform::decompose(type);
            if (!collectionForm(shape.form)) {
                s.kind = SourceKind::Error;
                s.name = t;
                s.error = path + " n'est pas un tableau ni une LIST : pas d'indice.";
                return s;
            }
            type = shape.element;
            path += "[" + sg.index + "]";
        }
    }
    s.name = path;
    s.type = type;
    const auto shape = typeform::decompose(type);
    switch (shape.form) {
        case typeform::Form::Array: {
            types::Spec sp;
            if (types::parseSpec(type, sp) && sp.array()) {
                s.low = sp.low[0];
                s.count = sp.count(0);
            }
            s.kind = SourceKind::Array;
            s.element = shape.element;
            return s;
        }
        case typeform::Form::Array2D:
            s.kind = SourceKind::Error;
            s.error = path + " est un tableau \xC3\xA0 2 dimensions : ses colonnes viennent seules (une par second indice) ; "
                      "laisse les colonnes vides, ou prends une ligne : " + path + "[1].";
            return s;
        case typeform::Form::List:
        case typeform::Form::Vector:
        case typeform::Form::Set:
        case typeform::Form::Ring:
            s.kind = SourceKind::List;
            s.low = 0;
            s.element = shape.element;
            return s;
        case typeform::Form::Map:
            s.kind = SourceKind::Map;
            s.element = shape.element;
            s.key = shape.parameter.empty() ? std::string("STRING") : shape.parameter;
            return s;
        case typeform::Form::Tuple:
            s.kind = SourceKind::Error;
            s.error = path + " est un tuple (" + type + ") : une seule valeur, pas une collection de lignes.";
            return s;
        case typeform::Form::Simple:
            break;
    }
    if (structType(p, type)) {
        s.kind = SourceKind::Struct;
        s.element = type;
        s.count = static_cast<long long>(sources::leafMembers(p, type).size());
        return s;
    }
    s.kind = SourceKind::Error;
    if (!segs.empty() && !segs.back().member) {
        const std::string whole = pathText(v->name, segs, segs.size() - 1);
        s.error = t + " est un " + type + " : un seul \xC3\xA9l\xC3\xA9ment de " + whole + ", pas une collection. Pour une ligne par \xC3\xA9l\xC3\xA9ment, "
                  "\xC3\xA9" "cris " + whole + ".";
        s.fix = whole;
    } else {
        s.error = t + " est un " + type + " : une seule valeur, pas une collection (tableau, LIST, VECTOR, MAP, \xC3\xA9num\xC3\xA9ration, structure).";
    }
    return s;
}

std::string rowsWord(const Source& s) {
    switch (s.kind) {
        case SourceKind::Array:
        case SourceKind::List: return "une ligne par \xC3\xA9l\xC3\xA9ment";
        case SourceKind::Map: return "une ligne par cl\xC3\xA9 ([*].Cle, [*].Valeur)";
        case SourceKind::Struct: return "une ligne par membre ([*].Membre, [*].Valeur)";
        case SourceKind::Enum: return "une ligne par valeur ([*].Nom, [*].Valeur, [*].Texte)";
        case SourceKind::Unknown: return "les lignes de ce qu'elle d\xC3\xA9signe en marche";
        default: return {};
    }
}

// ============================================================ une colonne
Content contentKind(std::string_view text) {
    const std::string s = trimmed(text);
    if (s.empty()) return Content::Empty;
    if (s.front() == '=') return Content::Formula;
    if (cellIsTemplate(s)) return Content::Template;
    if (s == "[#]") return Content::RowNumber;
    if (isPathLike(markers::strip(s))) return Content::Path;
    return Content::Text;
}
std::string_view contentLabel(Content c) noexcept {
    switch (c) {
        case Content::Empty: return "vide";
        case Content::Text: return "texte fixe";
        case Content::Path: return "variable";
        case Content::RowNumber: return "n\xC2\xB0 de ligne";
        case Content::Template: return "texte \xC3\xA0 trous";
        case Content::Formula: return "formule";
    }
    return {};
}

namespace {
// Une colonne qui ecrit ce chemin : ce que la source permet.
void checkWritable(const Project& p, const Source& src, std::string_view path, const PathType& r, ColumnInfo& info) {
    if (!r.ok) { info.writeWhy = r.error; return; }
    if (r.rowKey) {
        info.writeWhy = src.kind == SourceKind::Map ? "la cl\xC3\xA9 d'une MAP fait la ligne : elle ne s'\xC3\xA9" "crit pas (sa valeur, oui : [*].Valeur)"
                                                    : "le nom d'un membre ne s'\xC3\xA9" "crit pas (sa valeur, oui : [*].Valeur)";
        return;
    }
    if (r.constant) { info.writeWhy = "une \xC3\xA9num\xC3\xA9ration est une constante : rien ne s'\xC3\xA9" "crit"; return; }
    if (src.kind == SourceKind::Unknown && path.substr(0, 3) == "[*]") {
        info.writable = true;   // connu en marche
    } else {
        std::string u = r.type;
        for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        // [*].Valeur d'une structure : le type de chaque membre, connu a la saisie (ANY ici).
        const bool perRow = u == "ANY" && src.kind == SourceKind::Struct;
        if (!perRow && (u == "TOD" || u == "TIME_OF_DAY" || u == "DATE" || u == "DT" || u == "DATE_AND_TIME" || u == "ANY")) {
            info.writeWhy = "un " + r.type + " ne se saisit pas dans une case";
            return;
        }
        info.writable = true;
    }
    RowPaths row;
    row.position = 0;
    std::string elt = src.name;
    if (src.kind == SourceKind::Map) row.element = row.value = src.name + "[cl\xC3\xA9]";
    else if (src.kind == SourceKind::Struct) row.element = row.value = src.name + ".<membre>";
    else row.element = src.name + "[i]";
    row.key = "cl\xC3\xA9";
    bool bad = false;
    std::string target = rewrite(path, src, &p, nullptr, &row, &bad);
    // Liste[*] : Liste[i].
    std::string human;
    for (std::size_t i = 0; i < target.size(); ++i) {
        if (target[i] == '[' && i + 1 < target.size() && std::isdigit(static_cast<unsigned char>(target[i + 1])) != 0
            && path.find("[*]") != std::string_view::npos && path.substr(0, 3) != "[*]") {
            const auto close = target.find(']', i);
            if (close != std::string::npos) {
                human += "[i]";
                i = close;
                continue;
            }
        }
        human += target[i];
    }
    info.target = human;
    info.path = std::string(path);
}
} // namespace

ColumnInfo analyzeColumn(const Project& p, const Source& src, const Column& c) {
    ColumnInfo info;
    info.kind = contentKind(c.content);
    const std::string s = trimmed(markers::strip(c.content));
    switch (info.kind) {
        case Content::Empty:
            info.writeWhy = "la case est vide : rien \xC3\xA0 \xC3\xA9" "crire";
            break;
        case Content::Text:
            info.type = "STRING";
            info.writeWhy = "un texte fixe n'a pas de variable derri\xC3\xA8re";
            break;
        case Content::RowNumber:
            info.type = "INT";
            info.writeWhy = "[#], le num\xC3\xA9ro de la ligne, se compte : il ne s'\xC3\xA9" "crit pas";
            break;
        case Content::Path: {
            const auto r = resolvePath(p, src, s);
            if (r.unknownRoot) {
                info.kind = Content::Text;
                info.type = "STRING";
                info.warning = "\xC2\xAB " + s + " \xC2\xBB : ni une variable du projet, ni un membre de la ligne - montr\xC3\xA9 tel quel, sur chaque ligne.";
                info.writeWhy = "un texte fixe n'a pas de variable derri\xC3\xA8re";
                break;
            }
            info.lists = r.lists;
            if (!r.ok) {
                info.error = r.error;
                info.fix = r.fix;
                info.writeWhy = r.error;
                break;
            }
            info.type = r.type;
            if (const auto* d = displayOf(p, representative(s, src, &p))) {
                info.unit = d->unit;
                info.format = d->format;
            }
            checkWritable(p, src, s, r, info);
            break;
        }
        case Content::Template: {
            info.type = "STRING";
            for (const auto& hole : holesOf(s)) {
                std::string expr, fmt;
                splitHole(hole, expr, fmt);
                if (isPathLike(expr)) {
                    const auto r = resolvePath(p, src, expr);
                    if (r.unknownRoot) {
                        if (info.error.empty()) info.error = "{" + expr + "} : ni une variable du projet, ni un membre de la ligne.";
                        continue;
                    }
                    for (const auto& l : r.lists) info.lists.push_back(l);
                    if (!r.ok && info.error.empty()) {
                        info.error = "{" + expr + "} : " + r.error;
                        if (!r.fix.empty()) {
                            std::string fixed = s;
                            const auto at = fixed.find(expr);
                            if (at != std::string::npos) fixed.replace(at, expr.size(), r.fix);
                            info.fix = fixed;
                        }
                    }
                } else {
                    std::vector<std::string> lists;
                    (void)compileContent(expr, src, &p, &lists);
                    for (auto& l : lists) info.lists.push_back(std::move(l));
                }
            }
            const std::string one = singleHolePath(s);
            if (!one.empty() && info.error.empty()) {
                const auto r = resolvePath(p, src, one);
                checkWritable(p, src, one, r, info);
            } else if (info.writeWhy.empty()) {
                info.writeWhy = holesOf(s).size() > 1 ? "plusieurs trous : on ne sait pas lequel \xC3\xA9" "crire"
                                                      : "le trou est une formule : elle se calcule, elle ne s'\xC3\xA9" "crit pas";
            }
            break;
        }
        case Content::Formula: {
            const std::string body = trimmed(std::string_view(s).substr(1));
            if (isPathLike(body)) {
                const auto r = resolvePath(p, src, body);
                if (r.unknownRoot) {
                    info.error = "\xC2\xAB " + body + " \xC2\xBB : ni une variable du projet, ni un membre de la ligne.";
                    break;
                }
                info.lists = r.lists;
                if (!r.ok) {
                    info.error = r.error;
                    if (!r.fix.empty()) info.fix = "=" + r.fix;
                    break;
                }
                info.type = r.type;
                checkWritable(p, src, body, r, info);
                break;
            }
            std::vector<std::string> lists;
            (void)compileContent(body, src, &p, &lists);
            info.lists = std::move(lists);
            info.type = formulaType(p, src, body);
            // Un chemin [*] sans source : dit comme pour une variable.
            if (body.find("[*]") != std::string::npos && (src.kind == SourceKind::None || src.kind == SourceKind::Error)) {
                bool element = false;
                for (std::size_t i = body.find("[*]"); i != std::string::npos; i = body.find("[*]", i + 3)) {
                    const bool after = i > 0 && (identChar(body[i - 1]) || body[i - 1] == ']' || body[i - 1] == ')');
                    element = element || !after;
                }
                if (element)
                    info.error = "[*] d\xC3\xA9signe l'\xC3\xA9l\xC3\xA9ment de la ligne : il faut \xC2\xAB Lignes depuis \xC2\xBB. Sinon, nomme la "
                                 "liste de la colonne : Debits[*].";
            }
            info.writeWhy = "une formule se calcule : elle ne s'\xC3\xA9" "crit pas";
            break;
        }
    }
    // Les listes nommees, une fois chacune.
    std::sort(info.lists.begin(), info.lists.end());
    info.lists.erase(std::unique(info.lists.begin(), info.lists.end()), info.lists.end());
    if (!info.writable && info.target.empty() && info.writeWhy.empty()) info.writeWhy = "cette case ne s'\xC3\xA9" "crit pas";
    return info;
}

std::vector<Suggestion> suggestions(const Project& p, const Source& src) {
    std::vector<Suggestion> out;
    const auto walk = [&](const std::string& prefix, const std::string& type, int depth, auto&& self) -> void {
        for (const auto& m : types::membersOf(p, type)) {
            const auto shape = typeform::decompose(m.type);
            if (structType(p, m.type) && depth < 2) self(prefix + m.name + ".", m.type, depth + 1, self);
            else if (!collectionForm(shape.form) && shape.form != typeform::Form::Tuple && !structType(p, m.type))
                out.push_back({prefix + m.name, m.type});
        }
    };
    switch (src.kind) {
        case SourceKind::Array:
        case SourceKind::List:
            if (structType(p, src.element)) walk("[*].", src.element, 0, walk);
            else if (typeform::decompose(src.element).form == typeform::Form::Tuple) {
                const auto shape = typeform::decompose(src.element);
                (void)shape;
                out.push_back({"[*].Item1", ""});
                out.push_back({"[*].Item2", ""});
            } else out.push_back({"[*]", src.element});
            break;
        case SourceKind::Map:
            out.push_back({"[*].Cle", src.key.empty() ? "STRING" : src.key});
            if (structType(p, src.element)) walk("[*].Valeur.", src.element, 1, walk);
            else out.push_back({"[*].Valeur", src.element});
            break;
        case SourceKind::Struct:
            out.push_back({"[*].Membre", "STRING"});
            out.push_back({"[*].Valeur", "(le type du membre)"});
            break;
        case SourceKind::Enum:
            out.push_back({"[*].Nom", "STRING"});
            out.push_back({"[*].Valeur", "DINT"});
            out.push_back({"[*].Texte", "STRING"});
            break;
        default:
            break;
    }
    out.push_back({"[#]", "INT \xC2\xB7 n\xC2\xB0 de ligne"});
    // Une liste par colonne : les tableaux et les listes de valeurs simples du projet.
    for (const auto& v : p.programs.variables) {
        if (same(v.name, src.name)) continue;
        const auto shape = typeform::decompose(v.type);
        if (shape.form == typeform::Form::Map && src.kind == SourceKind::Map && same(shape.parameter.empty() ? "STRING" : shape.parameter, src.key)
            && !structType(p, shape.element)) {
            out.push_back({v.name + "[[*].Cle]", shape.element + " \xC2\xB7 m\xC3\xAAme cl\xC3\xA9"});   // plusieurs MAP : par la cle de la ligne
            continue;
        }
        const bool list = shape.form == typeform::Form::Array || shape.form == typeform::Form::List || shape.form == typeform::Form::Vector;
        if (!list || structType(p, shape.element) || typeform::decompose(shape.element).form != typeform::Form::Simple) continue;
        std::string t = shape.element;
        if (shape.form == typeform::Form::Array) {
            types::Spec sp;
            if (types::parseSpec(v.type, sp) && sp.array()) t += " \xC2\xB7 " + std::to_string(sp.count(0));
        }
        out.push_back({v.name + "[*]", t});
    }
    return out;
}

std::string titleFor(std::string_view content) {
    const std::string s = trimmed(markers::strip(content));
    if (s == "[#]") return "N\xC2\xB0";
    if (s == "[*]") return "Valeur";
    std::string word;
    std::string last;
    bool inside = false;
    int depth = 0;
    for (char c : s) {
        if (c == '[') { ++depth; continue; }
        if (c == ']') { --depth; continue; }
        if (depth > 0) continue;
        if (identChar(c)) {
            if (!inside) word.clear();
            inside = true;
            word += c;
        } else {
            if (inside) last = word;
            inside = false;
        }
    }
    if (inside) last = word;
    if (isKeyWord(last)) return "Cl\xC3\xA9";
    std::replace(last.begin(), last.end(), '_', ' ');
    return last.empty() ? std::string("Colonne") : last;
}

std::vector<Column> columnsForSource(const Project& p, const Source& src) {
    std::vector<Column> out;
    for (const auto& sg : suggestions(p, src)) {
        if (sg.text.rfind("[*]", 0) != 0) continue;
        Column c;
        c.content = sg.text;
        c.title = titleFor(sg.text);
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Column> legacyColumns(const Project& p, const Object& o) {
    std::vector<Column> out;
    const std::string from = o.text("rowsFrom");
    const auto src = analyzeSource(p, from);
    if (src.kind == SourceKind::Error || src.kind == SourceKind::None) return out;
    const auto headers = choices::rowsFromColumns(p, from);
    for (const auto& h : headers) {
        Column c;
        c.title = h;
        switch (src.kind) {
            case SourceKind::Map: c.content = isKeyWord(h) ? "[*].Cle" : "[*].Valeur"; break;
            case SourceKind::Struct: c.content = same(h, "Membre") ? "[*].Membre" : "[*].Valeur"; break;
            case SourceKind::Enum: c.content = "[*]." + h; break;
            default: c.content = same(h, "Valeur") && !structType(p, src.element) ? std::string("[*]") : "[*]." + h; break;
        }
        out.push_back(std::move(c));
    }
    // Les colonnes choisies d'avant : lesquelles, dans quel ordre, leur titre et leur format.
    const auto spec = choices::parseSourceColumns(o.text(choices::kSourceColumnsKey));
    if (!spec.empty()) {
        std::vector<Column> chosen;
        for (const auto& sc : spec) {
            const auto it = std::find_if(out.begin(), out.end(), [&](const Column& c) { return same(trimmed(c.title), sc.member); });
            if (it == out.end()) continue;
            Column c = *it;
            if (!sc.title.empty()) c.title = sc.title;
            c.format = sc.format;
            chosen.push_back(std::move(c));
        }
        if (!chosen.empty()) out = std::move(chosen);
    }
    const auto widths = splitSemicolons(o.text("widths"));
    for (std::size_t i = 0; i < out.size() && i < widths.size(); ++i) {
        double w = 0;
        if (parseNumber(trimmed(widths[i]), w) && w > 0) out[i].width = w;
    }
    return out;
}

std::string compileContent(std::string_view text, const Source& src, const Project* p, std::vector<std::string>* lists) {
    std::string s = trimmed(markers::strip(text));
    if (!s.empty() && s.front() == '=') s = trimmed(std::string_view(s).substr(1));
    return rewrite(s, src, p, lists, nullptr, nullptr);
}

std::string writePathOf(std::string_view content, const Source& src, const RowPaths& row, const Project* p) {
    std::string s = trimmed(markers::strip(content));
    switch (contentKind(s)) {
        case Content::Formula: s = trimmed(std::string_view(s).substr(1)); break;
        case Content::Template: s = singleHolePath(s); break;
        case Content::Path: break;
        default: return {};
    }
    if (s.empty() || !isPathLike(s)) return {};
    bool bad = false;
    std::string out = rewrite(s, src, p, nullptr, &row, &bad);
    return bad ? std::string{} : out;
}

std::string keyLiteral(std::string_view key, bool text) {
    if (!text) return std::string(key);
    std::string out = "'";
    for (char c : key) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out + "'";
}

std::vector<bool> fixedAccess(const Object& o, std::size_t n) {
    std::vector<bool> out(n, false);
    const auto parts = splitSemicolons(o.text(kAccessKey));
    for (std::size_t i = 0; i < n && i < parts.size(); ++i) {
        const std::string a = trimmed(parts[i]);
        out[i] = same(a, "W") || same(a, "R/W") || same(a, "RW");
    }
    return out;
}
std::string formatAccess(const std::vector<bool>& access) {
    std::string out;
    for (std::size_t i = 0; i < access.size(); ++i) out += (i ? ";" : "") + std::string(access[i] ? "W" : "R");
    return out;
}
std::string fixedWritePath(std::string_view cell) {
    const std::string s = trimmed(markers::strip(cell));
    if (s.empty()) return {};
    if (s.front() == '=') {
        const std::string body = trimmed(std::string_view(s).substr(1));
        return isPathLike(body) && body.find("[*]") == std::string::npos && body.find("[#]") == std::string::npos ? body : std::string{};
    }
    if (!cellIsTemplate(s)) return {};
    const std::string one = singleHolePath(s);
    return one.find("[*]") == std::string::npos && one.find("[#]") == std::string::npos ? one : std::string{};
}

std::vector<Param> receivedParams(const Project& p, const Object& o, Trigger t) {
    std::vector<Param> out;
    if (!triggerIsTable(t)) return out;
    const std::string copy = "copie";
    if (t == Trigger::HeaderClick) {
        out.push_back({"Colonne", "INT", copy, "la colonne cliqu\xC3\xA9" "e (1, 2...)"});
        out.push_back({"Titre", "STRING", copy, "son titre"});
        return out;
    }
    out.push_back({"Ligne", "INT", copy, "la ligne montr\xC3\xA9" "e (1, 2... ; tri\xC3\xA9" "e, elle peut diff\xC3\xA9rer de l'indice)"});
    const Mode m = modeOf(o);
    std::string column;   // le type de la colonne (Case saisie)
    if (m == Mode::Rows) {
        const auto src = analyzeSource(p, o.text("rowsFrom"));
        switch (src.kind) {
            case SourceKind::Array:
            case SourceKind::List:
                out.push_back({"Index", "INT", copy, "l'indice de l'\xC3\xA9l\xC3\xA9ment dans " + src.name});
                break;
            case SourceKind::Map: out.push_back({"Cle", src.key.empty() ? "STRING" : src.key, copy, "la cl\xC3\xA9 de la ligne"}); break;
            case SourceKind::Struct: out.push_back({"Membre", "STRING", copy, "le nom du membre de la ligne"}); break;
            default: break;
        }
        if (src.kind == SourceKind::Array || src.kind == SourceKind::List || src.kind == SourceKind::Map || src.kind == SourceKind::Struct
            || src.kind == SourceKind::Unknown)
            out.push_back({"Element", "REF_TO " + (src.kind == SourceKind::Struct ? std::string("(le membre)") : src.element.empty() ? std::string("ANY") : src.element),
                           "r\xC3\xA9" "f\xC3\xA9rence",
                           src.kind == SourceKind::Map ? "la valeur de la ligne (" + src.name + "[Cle]) : l'\xC3\xA9" "crire change la MAP"
                                                       : "l'\xC3\xA9l\xC3\xA9ment de la ligne (" + src.name + "[Index]) : Element.Membre := ... l'\xC3\xA9" "crit"});
    }
    if (t != Trigger::RowPick) {
        out.push_back({"Colonne", "INT", copy, "la colonne de la case (1, 2...)"});
        out.push_back({"Valeur", "STRING", copy, "le texte de la case"});
    }
    out.push_back({"Valeurs", "ARRAY[1..n] OF STRING", copy, "les textes de la ligne : Valeurs[1], Valeurs[2]..."});
    if (t == Trigger::CellEdit) {
        out.push_back({"Ancienne", "(le type de la case)", copy, "la valeur avant la saisie"});
        out.push_back({"Nouvelle", "(le type de la case)", copy, "la valeur saisie"});
    }
    return out;
}

std::string withoutCaret(std::string_view text) {
    std::string out;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\'') quoted = !quoted;
        if (!quoted && c == '^' && i >= 7 && same(text.substr(i - 7, 7), "Element") && (i == 7 || !identChar(text[i - 8]))) continue;
        out += c;
    }
    return out;
}

bool sortDescending(const Object& o) {
    const std::string s = trimmed(o.text(kSortOrderKey));
    return !s.empty() && (s.front() == 'd' || s.front() == 'D');
}
int sortColumnOf(const Object& o, const std::vector<Column>& cols) {
    const std::string by = trimmed(o.text(kSortKey));
    if (by.empty()) return -1;
    for (std::size_t k = 0; k < cols.size(); ++k)
        if (same(trimmed(cols[k].title), by)) return static_cast<int>(k);
    for (std::size_t k = 0; k < cols.size(); ++k)
        if (trimmed(cols[k].content) == by) return static_cast<int>(k);
    return -1;
}

} // namespace hmi::table
