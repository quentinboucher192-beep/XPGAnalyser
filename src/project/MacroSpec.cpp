// =============================================================================
//  project/MacroSpec.cpp - les lignes "#!" d'une macro, et ses appels
// =============================================================================
#include "MacroSpec.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace project::macro {

namespace {

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool sameKey(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::vector<std::string> splitCommas(std::string_view s) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= s.size()) {
        const auto at = s.find(',', from);
        auto part = trim(s.substr(from, (at == std::string_view::npos ? s.size() : at) - from));
        if (!part.empty()) out.push_back(std::move(part));
        if (at == std::string_view::npos) break;
        from = at + 1;
    }
    return out;
}

std::vector<std::string> splitWords(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

void appendLine(std::string& field, const std::string& value, const char* sep = "\n") {
    if (value.empty()) return;
    if (!field.empty()) field += sep;
    field += value;
}

// "#! param Fbk = le retour" -> key "param", sub "Fbk", value "le retour".
bool parseLine(std::string_view text, std::string& key, std::string& sub, std::string& value) {
    auto t = trim(text);
    if (t.size() < 2 || t.compare(0, 2, "#!") != 0) return false;
    t = trim(std::string_view(t).substr(2));
    const auto eq = t.find('=');
    if (eq == std::string::npos) return false;
    const auto spec = trim(std::string_view(t).substr(0, eq));
    value = trim(std::string_view(t).substr(eq + 1));
    if (spec.empty()) return false;
    const auto space = spec.find_first_of(" \t");
    if (space == std::string::npos) {
        key = spec;
        sub.clear();
    } else {
        key = spec.substr(0, space);
        sub = trim(std::string_view(spec).substr(space + 1));
    }
    return true;
}

// ---- le code : les appels a arguments litteraux --------------------------------
struct Arg {
    bool        literal{false};
    std::string text;       // le litteral decode, ou le texte brut de l'expression
};
struct Call {
    std::string      name;   // tel qu'ecrit
    std::vector<Arg> args;
    std::size_t      at{0};
};

// Un litteral ST a partir de s[i] == '\'' ; rend la position apres le guillemet.
std::size_t readLiteral(std::string_view s, std::size_t i, std::string& out) {
    out.clear();
    std::size_t j = i + 1;
    while (j < s.size()) {
        const char c = s[j];
        if (c == '$' && j + 1 < s.size()) {
            const char n = s[j + 1];
            switch (n) {
                case '\'': out += '\''; break;
                case '$':  out += '$'; break;
                case 'N': case 'n': case 'L': case 'l': out += '\n'; break;
                case 'T': case 't': out += '\t'; break;
                case 'R': case 'r': out += '\r'; break;
                default: out += n; break;
            }
            j += 2;
            continue;
        }
        if (c == '\'') return j + 1;
        out += c;
        ++j;
    }
    return j;
}

// Lot API 2 : les noms de TOUTES les fonctions appelees (en minuscules), pour
// les pastilles « ce qu'elle touche ». Memes regles que scanCalls : ni les
// commentaires, ni les chaines, ni les membres (x.Foo()).
std::vector<std::string> calledNames(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? s.size() : end + 2;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? s.size() : end + 1;
            continue;
        }
        if (c == '\'') {
            std::string ignored;
            i = readLiteral(s, i, ignored);
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            const bool member = i > 0 && s[i - 1] == '.';
            std::size_t k = j;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            if (!member && k < s.size() && s[k] == '(') {
                auto name = lower(s.substr(i, j - i));
                if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
            }
            i = j;
            continue;
        }
        ++i;
    }
    return out;
}

std::vector<Call> scanCalls(std::string_view s) {
    static const char* const kWanted[] = {"ask", "askchoice", "asknumber", "askyesno", "confirm",
                                          "runmacro", "opentable", "importtable", "opensheet", "hassheet"};
    std::vector<Call> calls;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? s.size() : end + 2;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? s.size() : end + 1;
            continue;
        }
        if (c == '\'') {
            std::string ignored;
            i = readLiteral(s, i, ignored);
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            const std::string word(s.substr(i, j - i));
            // Un nom precede d'un point est un membre, pas un appel.
            const bool member = i > 0 && s[i - 1] == '.';
            std::size_t k = j;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            const auto lw = lower(word);
            const bool wanted = std::any_of(std::begin(kWanted), std::end(kWanted), [&](const char* w) { return lw == w; });
            if (!member && wanted && k < s.size() && s[k] == '(') {
                Call call;
                call.name = word;
                call.at = i;
                // Les arguments, au premier niveau de parentheses.
                std::size_t p = k + 1;
                int depth = 1;
                Arg cur;
                bool onlyLiteral = true, sawLiteral = false;
                std::string raw;
                while (p < s.size() && depth > 0) {
                    const char d = s[p];
                    if (d == '\'') {
                        std::string lit;
                        const auto next = readLiteral(s, p, lit);
                        raw.append(s.substr(p, next - p));
                        if (sawLiteral) onlyLiteral = false;
                        cur.text = lit;
                        sawLiteral = true;
                        p = next;
                        continue;
                    }
                    if (d == '(') ++depth;
                    if (d == ')') {
                        --depth;
                        if (depth == 0) break;
                    }
                    if (d == ',' && depth == 1) {
                        const auto t = trim(raw);
                        cur.literal = sawLiteral && onlyLiteral && !t.empty() && t.front() == '\'' && t.back() == '\'';
                        if (!cur.literal) cur.text = t;
                        call.args.push_back(cur);
                        cur = Arg{};
                        raw.clear();
                        onlyLiteral = true;
                        sawLiteral = false;
                        ++p;
                        continue;
                    }
                    if (!std::isspace(static_cast<unsigned char>(d))) {
                        if (sawLiteral) onlyLiteral = false;
                    }
                    raw += d;
                    ++p;
                }
                const auto t = trim(raw);
                if (!t.empty()) {
                    cur.literal = sawLiteral && onlyLiteral && t.front() == '\'' && t.back() == '\'';
                    if (!cur.literal) cur.text = t;
                    call.args.push_back(cur);
                }
                calls.push_back(std::move(call));
                i = k + 1;
                continue;
            }
            i = j;
            continue;
        }
        ++i;
    }
    return calls;
}

// Le bloc d'en-tete : le premier "(* ... *)" du fichier.
std::string_view headerOf(std::string_view s) {
    const auto open = s.find("(*");
    if (open == std::string_view::npos) return {};
    // Seulement s'il est en tete (des blancs peuvent le preceder).
    for (std::size_t i = 0; i < open; ++i)
        if (!std::isspace(static_cast<unsigned char>(s[i]))) return {};
    const auto close = s.find("*)", open + 2);
    if (close == std::string_view::npos) return {};
    return s.substr(open + 2, close - open - 2);
}

bool parseRange(std::string_view token, double& lo, double& hi) {
    const auto dots = token.find("..");
    if (dots == std::string_view::npos) return false;
    const std::string a(token.substr(0, dots)), b(token.substr(dots + 2));
    if (a.empty() || b.empty()) return false;
    char* e1 = nullptr;
    char* e2 = nullptr;
    lo = std::strtod(a.c_str(), &e1);
    hi = std::strtod(b.c_str(), &e2);
    return e1 && *e1 == '\0' && e2 && *e2 == '\0';
}

std::vector<FieldOption> parseOptions(std::string_view text) {
    std::vector<FieldOption> out;
    for (const auto& part : splitCommas(text)) {
        const auto space = part.find_first_of(" \t");
        FieldOption o;
        if (space == std::string::npos) {
            o.value = part;
            o.label = part;
        } else {
            o.value = part.substr(0, space);
            o.label = trim(std::string_view(part).substr(space + 1));
        }
        out.push_back(std::move(o));
    }
    return out;
}

double numberOf(const Arg& a, bool& ok) {
    ok = false;
    if (a.text.empty()) return 0.0;
    char* end = nullptr;
    const double v = std::strtod(a.text.c_str(), &end);
    ok = end && *end == '\0';
    return v;
}

} // namespace

// ============================================================ les genres ====
std::string_view kindKey(FieldKind k) noexcept {
    switch (k) {
        case FieldKind::Text: return "texte";
        case FieldKind::Name: return "nom";
        case FieldKind::File: return "fichier";
        case FieldKind::OutputFile: return "fichier-sortie";
        case FieldKind::Task: return "tache";
        case FieldKind::Section: return "section";
        case FieldKind::Unit: return "unite";
        case FieldKind::Subroutine: return "sous-routine";
        case FieldKind::Dfb: return "dfb";
        case FieldKind::Ddt: return "ddt";
        case FieldKind::Type: return "type";
        case FieldKind::Variable: return "variable";
        case FieldKind::LibraryItem: return "bibliotheque";
        case FieldKind::Macro: return "macro";
        case FieldKind::Sheet: return "onglet";
        case FieldKind::SheetChoice: return "choix-classeur";
        case FieldKind::YesNo: return "oui-non";
        case FieldKind::Choice: return "choix";
        case FieldKind::Number: return "nombre";
        case FieldKind::List: return "liste";
        case FieldKind::Checks: return "cases";
    }
    return "texte";
}

std::optional<FieldKind> kindFromKey(std::string_view word) noexcept {
    const auto k = lower(foldAccents(word));
    for (int i = 0; i <= static_cast<int>(FieldKind::Checks); ++i) {
        const auto kind = static_cast<FieldKind>(i);
        if (k == kindKey(kind)) return kind;
    }
    // Les formes qu'on tape sans y penser.
    if (k == "unite-de-programme" || k == "unite_programme") return FieldKind::Unit;
    if (k == "sr" || k == "sous_routine" || k == "sousroutine") return FieldKind::Subroutine;
    if (k == "oui/non" || k == "ouinon" || k == "booleen") return FieldKind::YesNo;
    if (k == "fichier_sortie" || k == "sortie") return FieldKind::OutputFile;
    if (k == "choix_classeur") return FieldKind::SheetChoice;
    if (k == "classeur") return FieldKind::File;
    if (k == "bloc") return FieldKind::Dfb;
    return std::nullopt;
}

bool isPicker(FieldKind k) noexcept {
    switch (k) {
        case FieldKind::Task:
        case FieldKind::Section:
        case FieldKind::Unit:
        case FieldKind::Subroutine:
        case FieldKind::Dfb:
        case FieldKind::Ddt:
        case FieldKind::Type:
        case FieldKind::Variable:
        case FieldKind::LibraryItem:
        case FieldKind::Macro:
        case FieldKind::Sheet:
            return true;
        default:
            return false;
    }
}

std::string FieldSpec::optionLabel(std::string_view value) const {
    for (const auto& o : options)
        if (o.value == value) return o.label;
    return {};
}

// ============================================================== le spec ====
std::optional<std::string> MacroSpec::matchPattern(std::string_view pattern, std::string_view key) {
    const auto star = pattern.find('*');
    if (star == std::string_view::npos) return std::nullopt;
    const auto head = pattern.substr(0, star), tail = pattern.substr(star + 1);
    if (key.size() <= head.size() + tail.size()) return std::nullopt;
    if (!sameKey(key.substr(0, head.size()), head)) return std::nullopt;
    if (!sameKey(key.substr(key.size() - tail.size()), tail)) return std::nullopt;
    return std::string(key.substr(head.size(), key.size() - head.size() - tail.size()));
}

const FieldSpec* MacroSpec::field(std::string_view key) const {
    for (const auto& f : fields)
        if (!f.isPattern() && sameKey(f.key, key)) return &f;
    for (const auto& f : fields)
        if (f.isPattern() && matchPattern(f.key, key)) return &f;
    return nullptr;
}

std::string MacroSpec::groupOf(std::string_view key) const {
    for (const auto& g : groups)
        for (const auto& k : g.keys)
            if (sameKey(k, key) || matchPattern(k, key)) return g.name;
    if (const auto* f = field(key)) return f->group;
    return {};
}

bool MacroSpec::isAdvanced(std::string_view key) const {
    for (const auto& k : advanced)
        if (sameKey(k, key) || matchPattern(k, key)) return true;
    return false;
}

MacroSpec parseMacroSpec(std::string_view source, std::string_view name) {
    MacroSpec spec;
    spec.name = std::string(name);

    // ---- 1. les lignes "#!" -----------------------------------------------------
    std::vector<FieldSpec> declared;
    std::map<std::string, std::string> labels, examples, helps;
    std::vector<std::pair<std::string, FieldOption>> optionLines;
    const auto header = headerOf(source);
    // Le nom : le premier mot du bloc ("(* ImporterClasseur").
    if (spec.name.empty()) {
        const auto words = splitWords(header.substr(0, header.find('\n')));
        if (!words.empty()) spec.name = words.front();
    }
    std::size_t lineNo = 0;
    std::size_t from = 0;
    while (from <= header.size()) {
        const auto nl = header.find('\n', from);
        const auto line = header.substr(from, (nl == std::string_view::npos ? header.size() : nl) - from);
        from = nl == std::string_view::npos ? header.size() + 1 : nl + 1;
        ++lineNo;
        std::string key, sub, value;
        if (!parseLine(line, key, sub, value)) continue;
        const auto k = lower(foldAccents(key));
        if (k == "summary" && sub.empty()) appendLine(spec.summary, value, " ");
        else if (k == "version" && sub.empty()) spec.version = value;
        else if ((k == "categorie" || k == "category" || k == "dossier") && sub.empty()) spec.category = value;
        else if (k == "param" && !sub.empty()) appendLine(helps[lower(sub)], value);
        else if ((k == "libelle" || k == "label") && !sub.empty()) labels[lower(sub)] = value;
        else if (k == "exemple" && !sub.empty()) examples[lower(sub)] = value;
        else if (k == "option" && !sub.empty()) {
            const auto space = sub.find_first_of(" \t");
            if (space == std::string::npos) {
                spec.problems.push_back("#! option " + sub + " : il manque la valeur (#! option cle valeur = libelle)");
                continue;
            }
            FieldOption o;
            o.value = trim(std::string_view(sub).substr(space + 1));
            o.label = value;
            optionLines.emplace_back(lower(sub.substr(0, space)), std::move(o));
        } else if (k == "groupe" && !sub.empty()) {
            GroupSpec* g = nullptr;
            for (auto& x : spec.groups)
                if (sameKey(x.name, sub)) g = &x;
            if (!g) {
                spec.groups.push_back({sub, {}});
                g = &spec.groups.back();
            }
            for (auto& part : splitCommas(value)) g->keys.push_back(std::move(part));
        } else if (k == "avance" && sub.empty()) {
            for (auto& part : splitCommas(value)) spec.advanced.push_back(std::move(part));
        } else if (k == "lit" && sub.empty()) {
            spec.readsDeclared = true;
            for (auto& part : splitCommas(value)) spec.reads.push_back(std::move(part));
        } else if ((k == "lit-facultatif" || k == "lit_facultatif") && sub.empty()) {
            spec.readsDeclared = true;
            for (auto& part : splitCommas(value)) spec.readsOptional.push_back(std::move(part));
        } else if (k == "produit" && sub.empty()) {
            spec.produces.push_back(value);
        } else if (k == "appliquer" && sub.empty()) {
            appendLine(spec.applyText, value, " ");
        } else if (k == "tableau" && !sub.empty()) {
            spec.tables.push_back({sub, value});
        } else if (k == "champ" && !sub.empty()) {
            FieldSpec f;
            f.key = sub;
            f.declared = true;
            std::string left = value, right;
            if (const auto colon = value.find(':'); colon != std::string::npos) {
                left = trim(std::string_view(value).substr(0, colon));
                right = trim(std::string_view(value).substr(colon + 1));
            }
            const auto words = splitWords(left);
            if (words.empty()) {
                spec.problems.push_back("#! champ " + sub + " : le genre manque (texte, nom, fichier, tache, section, oui-non, nombre...)");
                continue;
            }
            const auto kind = kindFromKey(words.front());
            if (!kind) {
                spec.problems.push_back("#! champ " + sub + " : genre inconnu '" + words.front() + "'");
                continue;
            }
            f.kind = *kind;
            if (f.kind == FieldKind::SheetChoice) {
                const auto rest = trim(std::string_view(left).substr(words.front().size()));
                const auto parts = splitCommas(rest);
                if (parts.size() < 2) {
                    spec.problems.push_back("#! champ " + sub + " : choix-classeur Onglet, Colonne[, ColonneDuLibelle]");
                } else {
                    f.sheet = parts[0];
                    f.valueColumn = parts[1];
                    if (parts.size() > 2) f.labelColumn = parts[2];
                }
            } else {
                for (std::size_t w = 1; w < words.size(); ++w) {
                    const auto t = lower(foldAccents(words[w]));
                    double lo = 0.0, hi = 0.0;
                    if (t == "facultatif" || t == "facultative") f.optional = true;
                    else if (t == "nouvelle" || t == "nouveau" || t == "nouvelles" || t == "nouveaux") f.allowNew = true;
                    else if (t == "existant" || t == "existante") f.mustExist = true;
                    else if (t == "vide-si-tout") f.emptyMeansAll = true;
                    else if (t == "membres-de" && w + 1 < words.size()) f.membersOf = words[++w];
                    else if (!t.empty() && t.front() == '.') f.extensions.push_back(lower(words[w]));
                    else if (parseRange(t, lo, hi)) {
                        f.hasRange = true;
                        f.minimum = std::min(lo, hi);
                        f.maximum = std::max(lo, hi);
                    } else if (f.kind == FieldKind::Checks && f.source.empty()) {
                        f.source = t;
                    } else if (const auto picked = kindFromKey(t);
                               f.kind == FieldKind::List && f.source.empty() && picked && isPicker(*picked)) {
                        // "liste sous-routine" : chaque ligne se complete parmi les sous-routines.
                        f.source = std::string(kindKey(*picked));
                    } else {
                        spec.problems.push_back("#! champ " + sub + " : mot inconnu '" + words[w] + "'");
                    }
                }
            }
            if (!right.empty()) f.options = parseOptions(right);
            if (f.kind == FieldKind::Checks && f.source.empty()) f.source = f.options.empty() ? "onglets" : "options";
            // Deux lignes pour la meme cle : la derniere gagne, et on le dit.
            const auto dup = std::find_if(declared.begin(), declared.end(), [&](const FieldSpec& x) { return sameKey(x.key, f.key); });
            if (dup != declared.end()) {
                spec.problems.push_back("#! champ " + sub + " : declare deux fois, la seconde ligne gagne");
                *dup = std::move(f);
            } else {
                declared.push_back(std::move(f));
            }
        }
        (void)lineNo;
    }

    // ---- 2. le code : questions, macros lancees, tableaux, onglets ----------------
    std::vector<FieldSpec> asked;
    std::vector<std::string> sheetsOpened, sheetsTested;
    // Lot API 2 : ce que la macro touche (les pastilles de l'onglet Macros), lu
    // dans les fonctions qu'elle appelle. SectionRank ne fait que lire un rang ;
    // PlaceSection, elle, deplace la section dans l'ordre.
    for (const auto& n : calledNames(source)) {
        if (n == "openworkbook" || n == "opensheet") spec.touches.workbook = true;
        else if (n == "opentable" || n == "importtable") spec.touches.csv = true;
        else if (n == "runmacro") spec.touches.chains = true;
        else if (n == "libimport" || n == "libupdate") spec.touches.library = true;
        else if (n == "addsection" || n == "appendtosection" || n == "clearsection" || n == "replaceinsection"
                 || n == "addprogramunit" || n == "addsubroutine") spec.touches.sections = true;
        else if (n == "addvariable") spec.touches.variables = true;
        else if (n == "placesection") spec.touches.order = true;
        else if (n == "filewrite" || n == "fileappend") spec.touches.fileOut = true;
    }
    for (const auto& call : scanCalls(source)) {
        const auto n = lower(call.name);
        const auto lit = [&](std::size_t i) -> const Arg* {
            return i < call.args.size() && call.args[i].literal ? &call.args[i] : nullptr;
        };
        if (n == "runmacro") {
            if (const auto* a = lit(0))
                if (std::none_of(spec.launches.begin(), spec.launches.end(), [&](const std::string& x) { return sameKey(x, a->text); }))
                    spec.launches.push_back(a->text);
            continue;
        }
        if (n == "opentable" || n == "importtable") {
            if (const auto* a = lit(0))
                if (std::none_of(spec.tables.begin(), spec.tables.end(), [&](const MacroSpec::Table& x) { return sameKey(x.name, a->text); }))
                    spec.tables.push_back({a->text, {}});
            continue;
        }
        if (n == "opensheet" || n == "hassheet") {
            if (const auto* a = lit(0)) {
                auto& list = n == "opensheet" ? sheetsOpened : sheetsTested;
                if (std::find(list.begin(), list.end(), a->text) == list.end()) list.push_back(a->text);
            }
            continue;
        }
        if (n == "confirm") continue;
        const auto* keyArg = lit(0);
        if (!keyArg || keyArg->text.empty()) continue;      // une cle calculee : un motif la couvrira
        if (std::any_of(asked.begin(), asked.end(), [&](const FieldSpec& x) { return sameKey(x.key, keyArg->text); })) continue;
        FieldSpec f;
        f.key = keyArg->text;
        f.askKind = call.name;
        if (const auto* p = lit(1)) f.askPrompt = p->text;
        if (n == "ask") {
            f.kind = FieldKind::Text;
            if (const auto* p = lit(2)) f.askPreset = p->text;
        } else if (n == "askchoice") {
            f.kind = FieldKind::Choice;
            if (const auto* c = lit(2)) {
                const auto choices = splitCommas(c->text);
                const bool yesNo = choices.size() == 2
                    && ((choices[0] == "O" && choices[1] == "N") || (choices[0] == "N" && choices[1] == "O"));
                if (yesNo) f.kind = FieldKind::YesNo;
                for (const auto& ch : choices) f.options.push_back({ch, ch});
            }
            if (const auto* p = lit(3)) f.askPreset = p->text;
        } else if (n == "asknumber") {
            f.kind = FieldKind::Number;
            bool okLo = false, okHi = false;
            if (call.args.size() > 3) {
                const double lo = numberOf(call.args[2], okLo);
                const double hi = numberOf(call.args[3], okHi);
                if (okLo && okHi) {
                    f.hasRange = true;
                    f.minimum = lo;
                    f.maximum = hi;
                }
            }
            if (const auto* p = lit(4)) f.askPreset = p->text;
        } else if (n == "askyesno") {
            f.kind = FieldKind::YesNo;
            f.askPreset = "O";
        }
        asked.push_back(std::move(f));
    }

    // Les onglets lus : deduits du code quand "#! lit" manque. Un onglet que la
    // macro teste avec HasSheet est facultatif ; les autres sont requis.
    if (!spec.readsDeclared) {
        for (const auto& s : sheetsOpened)
            if (std::find(sheetsTested.begin(), sheetsTested.end(), s) == sheetsTested.end()) spec.reads.push_back(s);
        for (const auto& s : sheetsTested) spec.readsOptional.push_back(s);
    }

    // ---- 3. les champs : l'ordre du code, puis les declares sans Ask -------------
    for (auto& a : asked) {
        const auto exact = std::find_if(declared.begin(), declared.end(), [&](const FieldSpec& d) { return !d.isPattern() && sameKey(d.key, a.key); });
        if (exact != declared.end()) {
            FieldSpec f = *exact;
            f.askPrompt = a.askPrompt;
            f.askPreset = a.askPreset;
            f.askKind = a.askKind;
            if (!f.hasRange && a.hasRange) {
                f.hasRange = true;
                f.minimum = a.minimum;
                f.maximum = a.maximum;
            }
            if (f.options.empty() && (f.kind == FieldKind::Choice || f.kind == FieldKind::YesNo)) f.options = a.options;
            spec.fields.push_back(std::move(f));
            continue;
        }
        const bool covered = std::any_of(declared.begin(), declared.end(),
                                         [&](const FieldSpec& d) { return d.isPattern() && MacroSpec::matchPattern(d.key, a.key); });
        if (!covered) spec.fields.push_back(std::move(a));
    }
    for (auto& d : declared)
        if (std::none_of(spec.fields.begin(), spec.fields.end(), [&](const FieldSpec& f) { return sameKey(f.key, d.key); }))
            spec.fields.push_back(d);

    // ---- 4. les habillages ----------------------------------------------------------
    const auto known = [&](const std::string& key) {
        return std::any_of(spec.fields.begin(), spec.fields.end(), [&](const FieldSpec& f) {
            return sameKey(f.key, key) || (f.isPattern() && MacroSpec::matchPattern(f.key, key)) || (MacroSpec::matchPattern(key, f.key).has_value());
        });
    };
    for (auto& f : spec.fields) {
        const auto k = lower(f.key);
        if (const auto it = labels.find(k); it != labels.end()) f.label = it->second;
        if (const auto it = examples.find(k); it != examples.end()) f.example = it->second;
        if (const auto it = helps.find(k); it != helps.end()) f.help = it->second;
        for (const auto& [key, option] : optionLines) {
            if (key != k) continue;
            bool replaced = false;
            for (auto& o : f.options)
                if (o.value == option.value) {
                    o.label = option.label;
                    replaced = true;
                }
            if (!replaced) f.options.push_back(option);
        }
        f.group = spec.groupOf(f.key);
        f.advanced = spec.isAdvanced(f.key);
        if (f.label.empty()) f.label = f.askPrompt;
    }
    for (const auto& [key, text] : labels)
        if (!known(key)) spec.problems.push_back("#! libelle " + key + " : aucune question de cette cle");
    for (const auto& [key, text] : examples)
        if (!known(key)) spec.problems.push_back("#! exemple " + key + " : aucune question de cette cle");
    for (const auto& [key, option] : optionLines)
        if (!known(key)) spec.problems.push_back("#! option " + key + " : aucune question de cette cle");
    std::vector<std::string> seen;
    for (const auto& g : spec.groups)
        for (const auto& k : g.keys) {
            if (!known(k)) spec.problems.push_back("#! groupe " + g.name + " : aucune question '" + k + "'");
            if (std::find(seen.begin(), seen.end(), lower(k)) != seen.end())
                spec.problems.push_back("#! groupe " + g.name + " : '" + k + "' est deja dans un autre groupe");
            seen.push_back(lower(k));
        }
    for (const auto& k : spec.advanced)
        if (!known(k)) spec.problems.push_back("#! avance : aucune question '" + k + "'");
    for (const auto& d : declared)
        if (!d.isPattern()
            && std::none_of(asked.begin(), asked.end(), [&](const FieldSpec& a) { return sameKey(a.key, d.key); })
            && spec.launches.empty())
            spec.problems.push_back("#! champ " + d.key + " : la macro ne pose aucune question de cette cle");
    // Lot API 2 : les champs disent aussi ce que la macro lit et ecrit.
    if (!spec.tables.empty()) spec.touches.csv = true;
    for (const auto& f : spec.fields) {
        if (f.kind == FieldKind::OutputFile) spec.touches.fileOut = true;
        if (f.kind != FieldKind::File) continue;
        for (const auto& e : f.extensions) {
            const auto x = lower(e);
            if (x.rfind(".xls", 0) == 0) spec.touches.workbook = true;
            if (x == ".csv") spec.touches.csv = true;
        }
    }
    return spec;
}

// ================================================================== textes ====
std::string exampleFor(const FieldSpec& f, std::string_view answer) {
    if (f.example.empty()) return {};
    std::string out;
    std::size_t i = 0;
    while (i < f.example.size()) {
        if (f.example.compare(i, 2, "{}") == 0) {
            out.append(answer);
            i += 2;
            continue;
        }
        out += f.example[i++];
    }
    return out;
}

std::string applyTextFor(const MacroSpec& spec, const std::map<std::string, std::string>& answers, std::string_view fallback) {
    const std::string& text = spec.applyText;
    if (text.empty()) return std::string(fallback);
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            const auto close = text.find('}', i + 1);
            if (close != std::string::npos) {
                const auto key = text.substr(i + 1, close - i - 1);
                std::string value;
                bool found = false;
                for (const auto& [k, v] : answers)
                    if (sameKey(k, key)) {
                        value = v;
                        found = true;
                    }
                if (found) {
                    out += value;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

std::string labelFor(const FieldSpec& f, std::string_view key, std::string_view runtimePrompt) {
    std::string label = f.label;
    if (label.empty() || (!f.declared && !runtimePrompt.empty())) label = std::string(runtimePrompt);
    if (label.empty()) label = std::string(key);
    if (f.isPattern()) {
        if (const auto part = MacroSpec::matchPattern(f.key, key)) {
            std::string out;
            std::size_t i = 0;
            while (i < label.size()) {
                if (label.compare(i, 2, "{}") == 0) {
                    out += *part;
                    i += 2;
                    continue;
                }
                out += label[i++];
            }
            label = std::move(out);
        }
    }
    return label;
}

std::string describe(const FieldSpec& f) {
    const auto join = [](const std::vector<std::string>& v, const char* sep) {
        std::string out;
        for (const auto& s : v) out += (out.empty() ? "" : sep) + s;
        return out;
    };
    const auto number = [](double v) {
        char buf[32];
        if (std::fabs(v - std::round(v)) < 1e-9) std::snprintf(buf, sizeof buf, "%.0f", v);
        else std::snprintf(buf, sizeof buf, "%g", v);
        return std::string(buf);
    };
    std::string d;
    switch (f.kind) {
        case FieldKind::Text: d = "texte"; break;
        case FieldKind::Name: d = f.example.empty() ? "nom" : "nom, avec exemple"; break;
        case FieldKind::File: d = "fichier" + (f.extensions.empty() ? std::string{} : " " + join(f.extensions, " ")); break;
        case FieldKind::OutputFile: d = "fichier \xC3\xA0 \xC3\xA9" "crire" + (f.extensions.empty() ? std::string{} : " " + join(f.extensions, " ")); break;
        case FieldKind::Task: d = "t\xC3\xA2" "che du projet"; break;
        case FieldKind::Section: d = f.allowNew ? "section, existante ou nouvelle" : "section du projet"; break;
        case FieldKind::Unit: d = f.allowNew ? "unit\xC3\xA9, existante ou nouvelle" : "unit\xC3\xA9 de programme"; break;
        case FieldKind::Subroutine: d = f.allowNew ? "sous-routine, existante ou nouvelle" : "sous-routine du projet"; break;
        case FieldKind::Dfb: d = "bloc DFB (projet et biblioth\xC3\xA8que)"; break;
        case FieldKind::Ddt: d = "type d\xC3\xA9riv\xC3\xA9 (projet et biblioth\xC3\xA8que)"; break;
        case FieldKind::Type: d = "type"; break;
        case FieldKind::Variable: d = "variable du projet"; break;
        case FieldKind::LibraryItem: d = "\xC3\xA9l\xC3\xA9ment de la biblioth\xC3\xA8que"; break;
        case FieldKind::Macro: d = "macro"; break;
        case FieldKind::Sheet: d = "onglet du classeur"; break;
        case FieldKind::SheetChoice: d = "lu dans l'onglet " + f.sheet; break;
        case FieldKind::YesNo: d = "oui / non"; break;
        case FieldKind::Choice: {
            std::vector<std::string> labels;
            for (const auto& o : f.options) labels.push_back(o.label.empty() ? o.value : o.label);
            d = labels.empty() || labels.size() > 4 ? "choix" : join(labels, " \xC2\xB7 ");
            break;
        }
        case FieldKind::Number:
            d = f.hasRange ? number(f.minimum) + " \xC3\xA0 " + number(f.maximum) : "nombre";
            if (!f.options.empty()) d += ", \xC3\xA0 libell\xC3\xA9s";
            break;
        case FieldKind::List: d = "liste, une ligne par \xC3\xA9l\xC3\xA9ment"; break;
        case FieldKind::Checks: d = f.source == "retard" ? "cases : ce qui est en retard" : "cases \xC3\xA0 cocher"; break;
    }
    if (f.optional && f.kind != FieldKind::YesNo) d += ", facultatif";
    return d;
}

// ============================================================ verifier ====
bool isIdentifier(std::string_view s) noexcept {
    if (s.empty()) return false;
    const auto c0 = static_cast<unsigned char>(s.front());
    if (!(std::isalpha(c0) || c0 == '_')) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (!(std::isalnum(c) || c == '_')) return false;
        if (c == '_' && i + 1 < s.size() && s[i + 1] == '_') return false;
    }
    return true;
}

Check checkName(std::string_view text, bool optional) {
    const auto t = trim(text);
    if (t.empty()) return optional ? Check{} : Check{Verdict::Error, "\xC3\xA0 remplir"};
    for (char c : t)
        if (static_cast<unsigned char>(c) >= 0x80) return {Verdict::Error, "pas d'accent dans un nom : Control Expert le refuserait"};
    if (std::isdigit(static_cast<unsigned char>(t.front()))) return {Verdict::Error, "un nom ne commence pas par un chiffre"};
    for (char c : t)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            return {Verdict::Error, std::string("caract\xC3\xA8re refus\xC3\xA9 dans un nom : '") + c + "'"};
    if (t.find("__") != std::string::npos) return {Verdict::Error, "deux _ de suite : Control Expert le refuserait"};
    if (t.size() > 32) return {Verdict::Warning, "plus de 32 caract\xC3\xA8res : trop long pour Control Expert"};
    return {};
}

Check checkNumber(std::string_view text, double minimum, double maximum) {
    auto t = trim(text);
    std::replace(t.begin(), t.end(), ',', '.');
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    if (t.empty()) return {Verdict::Error, "un nombre"};
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0') return {Verdict::Error, "ce n'est pas un nombre"};
    if (maximum > minimum && (v < minimum || v > maximum)) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "entre %g et %g", minimum, maximum);
        return {Verdict::Error, buf};
    }
    return {};
}

Check checkMacroName(std::string_view name) {
    const auto t = trim(name);
    if (t.empty()) return {Verdict::Error, "le nom est vide"};
    if (t.front() == '_') return {Verdict::Error, "un nom de macro ne commence pas par _ (les dossiers _corbeille et _modeles)"};
    if (!isIdentifier(t)) return {Verdict::Error, "lettres, chiffres et _ seulement, sans accent ni espace, et pas de chiffre en t\xC3\xAAte"};
    if (t.size() > 64) return {Verdict::Error, "64 caract\xC3\xA8res au plus"};
    return {};
}

std::string foldAccents(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(c);
            continue;
        }
        // Deux octets : U+0080..U+07FF.
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            ++i;
            if (cp >= 0xC0 && cp <= 0xFF) {
                static const char* const kLatin1 =
                    "AAAAAAACEEEEIIII"   // C0-CF
                    "DNOOOOOxOUUUUYTs"   // D0-DF
                    "aaaaaaaceeeeiiii"   // E0-EF
                    "dnooooo/ouuuuyty";  // F0-FF
                const char a = kLatin1[cp - 0xC0];
                if (cp == 0xC6) out += "AE";
                else if (cp == 0xE6) out += "ae";
                else if (cp == 0xDF) out += "ss";
                else out += a;
            } else if (cp == 0x152) {
                out += "OE";
            } else if (cp == 0x153) {
                out += "oe";
            } else {
                out += '?';
            }
            continue;
        }
        // Trois octets et plus : rendu tel quel (guillemets, tirets...).
        std::size_t n = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        out.append(s.substr(i, n));
        i += n - 1;
    }
    return out;
}

} // namespace project::macro
