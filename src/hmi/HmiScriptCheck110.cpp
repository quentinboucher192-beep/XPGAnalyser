#include "HmiScriptCheck110.hpp"
#include "HmiNatives.hpp"   // 1.12.0 : les enumerations natives

#include "HmiEnums.hpp"
#include "HmiOverload.hpp"   // 1.11.20 : deux fonctions internes de meme forme
#include "HmiModel.hpp"
#include "HmiScript.hpp"
#include "HmiTypes.hpp"
#include "../sim/Interpreter.hpp"

#include <algorithm>
#include <cctype>

namespace hmi::lang110 {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Un mot du code, avec sa place (les chaines et les commentaires sont sautes).
struct Word {
    std::string text;          // tel qu'ecrit
    std::string up;            // en majuscules
    int         line{1}, column{1};
    std::size_t at{0};         // sa place dans le code
    char        next{0};       // le premier caractere non blanc qui suit
};

std::vector<Word> wordsOf(std::string_view s) {
    std::vector<Word> out;
    int line = 1;
    std::size_t lineStart = 0;
    std::size_t i = 0;
    const auto skipTo = [&](std::size_t to) {
        for (; i < to && i < s.size(); ++i)
            if (s[i] == '\n') { ++line; lineStart = i + 1; }
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\n') { ++line; lineStart = ++i; continue; }
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            skipTo(end == std::string_view::npos ? s.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            skipTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            skipTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        if (identStart(c) && !(i > 0 && s[i - 1] == '.')) {
            Word w;
            w.at = i;
            w.line = line;
            w.column = static_cast<int>(i - lineStart) + 1;
            while (i < s.size() && identChar(s[i])) ++i;
            w.text = std::string(s.substr(w.at, i - w.at));
            w.up = upper(w.text);
            std::size_t k = i;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            w.next = k < s.size() ? s[k] : '\0';
            out.push_back(std::move(w));
            continue;
        }
        if (identStart(c)) {            // un membre (.Nom) : saute
            while (i < s.size() && identChar(s[i])) ++i;
            continue;
        }
        ++i;
    }
    return out;
}

// Le nombre d'arguments d'un appel dont la parenthese ouvrante est a `open`.
int argumentCount(std::string_view s, std::size_t open) {
    int depth = 0, count = 0;
    bool any = false;
    for (std::size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            if (end == std::string_view::npos) return -1;
            i = end;
            any = true;
            continue;
        }
        if (c == '(' || c == '[') { ++depth; if (depth > 1) any = true; continue; }
        if (c == ')' || c == ']') {
            --depth;
            if (depth == 0) return any ? count + 1 : 0;
            continue;
        }
        if (depth == 1 && c == ',') { ++count; continue; }
        if (depth >= 1 && !std::isspace(static_cast<unsigned char>(c))) any = true;
    }
    return -1;
}

// "Nom(a : T; VAR_IN_OUT b : U) : R" (sim::functionSignature) -> parametres et retour.
void fromSignature(const std::string& sig, InnerFunction& f) {
    const auto open = sig.find('(');
    const auto close = sig.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) return;
    const std::string list = sig.substr(open + 1, close - open - 1);
    std::size_t from = 0;
    while (from < list.size()) {
        auto semi = list.find("; ", from);
        if (semi == std::string::npos) semi = list.size();
        std::string one = list.substr(from, semi - from);
        from = semi + 2;
        Param p;
        if (one.rfind("VAR_IN_OUT ", 0) == 0) { p.mode = Param::Mode::InOut; one.erase(0, 11); }
        else if (one.rfind("VAR_OUTPUT ", 0) == 0) { p.mode = Param::Mode::Output; one.erase(0, 11); }
        const auto colon = one.find(" : ");
        if (colon == std::string::npos) continue;
        p.name = one.substr(0, colon);
        p.type = one.substr(colon + 3);
        f.params.push_back(std::move(p));
    }
    if (close + 4 < sig.size() && sig.compare(close + 1, 3, " : ") == 0) f.returnType = sig.substr(close + 4);
}

// ---- 1.10 (decision 15) : les enumerations ----------------------------------
// Le code, chaines et commentaires blanchis (les places et les lignes restent).
std::string cleanCode(std::string_view s) {
    std::string out(s);
    std::size_t i = 0;
    const auto blankTo = [&](std::size_t to) {
        for (; i < to && i < out.size(); ++i)
            if (out[i] != '\n') out[i] = ' ';
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            blankTo(end == std::string_view::npos ? s.size() : end + 1);
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            blankTo(end == std::string_view::npos ? s.size() : end + 2);
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto end = s.find('\n', i);
            blankTo(end == std::string_view::npos ? s.size() : end);
            continue;
        }
        ++i;
    }
    return out;
}

struct Tok {
    enum class Kind : std::uint8_t { Ident, Typed, Number, Punct } kind{Kind::Punct};
    std::string text, up;       // Typed : "T_MODE#Auto"
    std::size_t at{0};
    int         line{1}, column{1};
};

bool elementaryPrefix(const std::string& up) {
    return up == "T" || up == "TIME" || sim::typeFromName(up) != sim::Type::Unknown || up == "LREAL" || up == "SINT"
        || up == "USINT" || up == "LINT" || up == "ULINT" || up == "DT" || up == "D" || up == "TOD" || up == "DATE";
}

std::vector<Tok> tokensOf(const std::string& c) {
    std::vector<Tok> out;
    int line = 1;
    std::size_t lineStart = 0;
    std::size_t i = 0;
    while (i < c.size()) {
        const char ch = c[i];
        if (ch == '\n') { ++line; lineStart = ++i; continue; }
        if (std::isspace(static_cast<unsigned char>(ch))) { ++i; continue; }
        Tok t;
        t.at = i;
        t.line = line;
        t.column = static_cast<int>(i - lineStart) + 1;
        if (identStart(ch)) {
            while (i < c.size() && identChar(c[i])) ++i;
            t.text = c.substr(t.at, i - t.at);
            t.up = upper(t.text);
            t.kind = Tok::Kind::Ident;
            if (i < c.size() && c[i] == '#') {
                if (i + 1 < c.size() && identStart(c[i + 1]) && !elementaryPrefix(t.up)) {
                    std::size_t k = i + 1;
                    while (k < c.size() && identChar(c[k])) ++k;
                    t.text = c.substr(t.at, k - t.at);
                    t.up = upper(t.text);
                    t.kind = Tok::Kind::Typed;
                    i = k;
                } else {             // T#5s, INT#3, 16#FF : un litteral
                    ++i;
                    while (i < c.size() && (identChar(c[i]) || c[i] == '.')) ++i;
                    t.text = c.substr(t.at, i - t.at);
                    t.up = upper(t.text);
                    t.kind = Tok::Kind::Number;
                }
            }
            out.push_back(std::move(t));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            while (i < c.size() && (identChar(c[i]) || c[i] == '#' || (c[i] == '.' && i + 1 < c.size() && c[i + 1] != '.'))) ++i;
            t.text = c.substr(t.at, i - t.at);
            t.up = upper(t.text);
            t.kind = Tok::Kind::Number;
            out.push_back(std::move(t));
            continue;
        }
        static constexpr std::string_view kTwo[] = {":=", "<>", "<=", ">=", "..", "=>", "+=", "-=", "*=", "/="};
        bool two = false;
        for (auto op : kTwo)
            if (c.compare(i, 2, op) == 0) {
                t.text = std::string(op);
                i += 2;
                two = true;
                break;
            }
        if (!two) t.text = std::string(1, c[i++]);
        t.up = t.text;
        t.kind = Tok::Kind::Punct;
        out.push_back(std::move(t));
    }
    return out;
}

bool plainInteger(const Tok& t) {
    if (t.kind != Tok::Kind::Number || t.text.empty()) return false;
    for (const char ch : t.text)
        if (!std::isdigit(static_cast<unsigned char>(ch)) && ch != '_') return false;
    return true;
}

// La distance d'edition (pour "veux-tu dire ... ?").
std::size_t distance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t prev = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cur = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, prev + (a[i - 1] == b[j - 1] ? 0 : 1)});
            prev = cur;
        }
    }
    return row[b.size()];
}

} // namespace

std::string InnerFunction::signature() const {
    std::string s = name + "(";
    for (std::size_t k = 0; k < params.size(); ++k) {
        s += k ? "; " : "";
        if (params[k].mode == Param::Mode::InOut) s += "VAR_IN_OUT ";
        else if (params[k].mode == Param::Mode::Output) s += "VAR_OUTPUT ";
        s += params[k].name + " : " + params[k].type;
    }
    s += ")";
    if (!returnType.empty()) s += " : " + returnType;
    return s;
}

const InnerFunction* Analysis::function(std::string_view name) const noexcept {
    const std::string u = upper(name);
    for (const auto& f : functions)
        if (upper(f.name) == u) return &f;
    return nullptr;
}

bool knownHmiType(const Project* project, std::string_view type) {
    return project != nullptr && (!types::membersOf(*project, type).empty() || findEnumeration(*project, type) != nullptr);
}

EnumValuesFn enumValuesOf(const Project* project) {
    return [project](std::string_view type) {
        std::vector<std::pair<std::string, std::int64_t>> out;
        if (const auto* e = project ? findEnumeration(*project, type) : nullptr)
            for (const auto& v : e->values) out.emplace_back(v.name, v.value);
        // 1.12.0 : les enumerations natives (NIVEAU_LOG, TRANSITION, POSITION_POPUP...).
        if (const auto* n = out.empty() ? natives::nativeEnum(type) : nullptr)
            for (const auto& v : n->values) out.emplace_back(std::string(v.name), v.number);
        return out;
    };
}

namespace {
// Les controles des enumerations (decision 15) : litteral inconnu, CASE incomplet,
// entier affecte, entier passe, deux enumerations comparees.
void enumChecks(std::string_view code, Analysis& out, const std::function<std::string(std::string_view)>& typeOf,
                const EnumValuesFn& enumValues, const Project* project) {
    if (!enumValues) return;
    const std::string clean = cleanCode(code);
    const auto toks = tokensOf(clean);
    const auto values = [&](const std::string& type) { return type.empty() ? std::vector<std::pair<std::string, std::int64_t>>{} : enumValues(type); };
    // Les types declares : les locales du script, les parametres et locales des fonctions internes.
    const auto permissive = [](std::string_view) { return true; };
    const auto scriptParts = splitDeclarations(code, false, permissive);
    const auto declared = [&](const std::string& name, int line) -> std::string {
        for (const auto& f : out.functions) {
            if (line < f.firstLine || line > f.lastLine) continue;
            for (const auto& p : f.params) if (upper(p.name) == upper(name)) return p.type;
            for (const auto& l : f.locals) if (upper(l.name) == upper(name)) return l.type;
            if (upper(f.name) == upper(name)) return f.returnType;
        }
        if (const auto* l = scriptParts.local(name)) return l->type;
        return typeOf ? typeOf(name) : std::string{};
    };
    const auto enumOf = [&](std::size_t k) -> std::string {
        if (k >= toks.size()) return {};
        const auto& t = toks[k];
        if (t.kind == Tok::Kind::Typed) {
            const std::string type = t.text.substr(0, t.text.find('#'));
            return values(type).empty() ? std::string{} : type;
        }
        if (t.kind != Tok::Kind::Ident) return {};
        if (k + 1 < toks.size() && (toks[k + 1].text == "(" || toks[k + 1].text == "[" || toks[k + 1].text == ".")) return {};
        if (k > 0 && toks[k - 1].text == ".") return {};
        const std::string type = declared(t.text, t.line);
        return values(type).empty() ? std::string{} : type;
    };
    // (1) un litteral inconnu : veux-tu dire ... ?
    for (const auto& t : toks) {
        if (t.kind != Tok::Kind::Typed) continue;
        const auto hash = t.text.find('#');
        const std::string type = t.text.substr(0, hash), name = t.text.substr(hash + 1);
        const auto vals = values(type);
        if (vals.empty()) continue;
        bool found = false;
        for (const auto& v : vals) found = found || upper(v.first) == upper(name);
        if (found) continue;
        std::string best;
        std::size_t bestD = 1000;
        for (const auto& v : vals) {
            const std::size_t d = upper(name).rfind(upper(v.first), 0) == 0 || upper(v.first).rfind(upper(name), 0) == 0
                                      ? 0 : distance(upper(name), upper(v.first));
            if (d < bestD) { bestD = d; best = v.first; }
        }
        std::string msg = t.text + " n'existe pas";
        if (!best.empty() && bestD <= std::max<std::size_t>(2, name.size() / 2)) msg += " : veux-tu dire " + type + "#" + best + " ?";
        else {
            msg += " (valeurs de " + type + " : ";
            for (std::size_t i = 0; i < vals.size(); ++i) msg += (i ? ", " : "") + vals[i].first;
            msg += ")";
        }
        out.findings.push_back({t.line, t.column, static_cast<int>(t.text.size()), msg, true});
    }
    // (2) CASE sans ELSE qui oublie des valeurs.
    for (std::size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].kind != Tok::Kind::Ident || toks[k].up != "CASE") continue;
        std::size_t of = k + 1;
        while (of < toks.size() && toks[of].up != "OF") ++of;
        if (of >= toks.size()) continue;
        std::string type = (of == k + 2) ? enumOf(k + 1) : std::string{};
        int caseDepth = 0, ifDepth = 0, paren = 0;
        bool hasElse = false;
        std::size_t end = toks.size();
        std::vector<std::string> covered;
        std::vector<std::size_t> bareLabels;
        int labelColumn = 0;
        for (std::size_t j = of + 1; j < toks.size(); ++j) {
            const auto& t = toks[j];
            if (t.text == "(" || t.text == "[") { ++paren; continue; }
            if (t.text == ")" || t.text == "]") { --paren; continue; }
            if (t.kind == Tok::Kind::Ident) {
                if (t.up == "CASE") { ++caseDepth; continue; }
                if (t.up == "END_CASE") {
                    if (caseDepth == 0) { end = j; break; }
                    --caseDepth;
                    continue;
                }
                if (t.up == "IF") { ++ifDepth; continue; }
                if (t.up == "END_IF") { --ifDepth; continue; }
                if (t.up == "ELSE" && caseDepth == 0 && ifDepth == 0) { hasElse = true; continue; }
            }
            if (caseDepth != 0 || paren != 0 || j + 1 >= toks.size()) continue;
            const bool label = toks[j + 1].text == ":" || (toks[j + 1].text == "," && (toks[j - 1].text == "OF" || toks[j - 1].text == ";"
                                                                                     || toks[j - 1].text == "," || toks[j - 1].text == ":"));
            if (!label) continue;
            if (t.kind == Tok::Kind::Typed) {
                const auto hash = t.text.find('#');
                if (type.empty() && !values(t.text.substr(0, hash)).empty()) type = t.text.substr(0, hash);
                covered.push_back(upper(t.text.substr(hash + 1)));
                if (!labelColumn) labelColumn = t.column;
            } else if (t.kind == Tok::Kind::Ident) {
                covered.push_back(t.up);
                if (!labelColumn) labelColumn = t.column;
                bareLabels.push_back(j);
            }
        }
        // Les noms seuls d'un CASE sur une enumeration : des valeurs (N ne les prend pas pour des variables).
        if (!type.empty())
            for (const auto j : bareLabels) out.names.push_back({toks[j].text, type, toks[j].line, toks[j].line});
        if (type.empty() || hasElse || end >= toks.size()) continue;
        std::vector<std::string> missing;
        for (const auto& v : values(type))
            if (std::find(covered.begin(), covered.end(), upper(v.first)) == covered.end()) missing.push_back(v.first);
        if (missing.empty()) continue;
        Finding f{toks[k].line, toks[k].column, 4, "CASE sans ELSE : valeurs non trait\xC3\xA9" "es : ", false};
        std::string fix;
        const std::string indent(static_cast<std::size_t>(std::max(1, labelColumn) - 1), ' ');
        for (std::size_t i = 0; i < missing.size(); ++i) {
            f.message += (i ? ", " : "") + missing[i];
            fix += indent + type + "#" + missing[i] + ": ;\n";
        }
        f.fixLabel = "Ajouter les valeurs manquantes";
        f.fixLine = toks[end].line;
        f.fixText = fix;
        out.findings.push_back(std::move(f));
    }
    // Le nom seul d'une valeur, la ou le type attendu est une enumeration : m := Auto, m = Auto.
    const auto isValue = [&](const std::string& type, const Tok& t) {
        if (t.kind != Tok::Kind::Ident) return false;
        for (const auto& v : values(type)) if (upper(v.first) == t.up) return true;
        return false;
    };
    for (std::size_t k = 0; k + 2 < toks.size(); ++k) {
        const auto& op = toks[k + 1].text;
        if (op != ":=" && op != "=" && op != "<>" && op != "<" && op != ">" && op != "<=" && op != ">=") continue;
        const std::string type = enumOf(k);
        if (!type.empty() && isValue(type, toks[k + 2]) && declared(toks[k + 2].text, toks[k + 2].line).empty())
            out.names.push_back({toks[k + 2].text, type, toks[k + 2].line, toks[k + 2].line});
        if (op != ":=") {
            const std::string right = enumOf(k + 2);
            if (!right.empty() && isValue(right, toks[k]) && declared(toks[k].text, toks[k].line).empty())
                out.names.push_back({toks[k].text, right, toks[k].line, toks[k].line});
        }
    }
    // RETURN Auto dans une fonction qui rend une enumeration.
    for (std::size_t k = 0; k + 1 < toks.size(); ++k) {
        if (toks[k].kind != Tok::Kind::Ident || toks[k].up != "RETURN" || toks[k + 1].kind != Tok::Kind::Ident) continue;
        for (const auto& f : out.functions)
            if (toks[k].line >= f.firstLine && toks[k].line <= f.lastLine && isValue(f.returnType, toks[k + 1])
                && declared(toks[k + 1].text, toks[k + 1].line).empty())
                out.names.push_back({toks[k + 1].text, f.returnType, toks[k + 1].line, toks[k + 1].line});
    }
    // (3) un entier affecte a une enumeration ; (5) deux enumerations comparees.
    for (std::size_t k = 0; k + 2 < toks.size(); ++k) {
        if (toks[k + 1].text == ":=" && plainInteger(toks[k + 2])
            && (k + 3 >= toks.size() || toks[k + 3].text == ";" || toks[k + 3].line != toks[k + 2].line)
            && (k == 0 || toks[k - 1].text != ".")) {
            const std::string type = enumOf(k);
            if (!type.empty())
                out.findings.push_back({toks[k + 2].line, toks[k + 2].column, static_cast<int>(toks[k + 2].text.size()),
                                        "un entier ne s'affecte pas \xC3\xA0 " + toks[k].text + " (" + type + ") : \xC3\xA9" "cris TO_"
                                            + type + "(" + toks[k + 2].text + ") (contr\xC3\xB4l\xC3\xA9) ou " + type + "#...", true});
        }
    }
    for (std::size_t k = 1; k + 1 < toks.size(); ++k) {
        const auto& op = toks[k].text;
        if (toks[k].kind != Tok::Kind::Punct || (op != "=" && op != "<>" && op != "<" && op != ">" && op != "<=" && op != ">="))
            continue;
        const std::string a = enumOf(k - 1), b = enumOf(k + 1);
        if (!a.empty() && !b.empty() && upper(a) != upper(b))
            out.findings.push_back({toks[k].line, toks[k].column, static_cast<int>(op.size()),
                                    "comparaison de deux \xC3\xA9" "num\xC3\xA9" "rations diff\xC3\xA9rentes : " + a + " et " + b, true});
    }
    // (4) un entier passe a une fonction interne qui attend une enumeration.
    std::vector<InnerFunction> projectFunctions;      // les fonctions IHM du projet appelees (leur signature)
    const auto projectFunction = [&](const std::string& name) -> const InnerFunction* {
        for (const auto& pf : projectFunctions) if (upper(pf.name) == upper(name)) return &pf;
        const HmiFunction* hf = project ? project->functionByName(name) : nullptr;
        if (!hf) return nullptr;
        InnerFunction pf;
        pf.name = hf->name;
        if (auto parsed = sim::parseFunction(functionText(*hf), hf->name)) fromSignature(sim::functionSignature(**parsed), pf);
        projectFunctions.push_back(std::move(pf));
        return &projectFunctions.back();
    };
    projectFunctions.reserve(64);
    for (std::size_t k = 0; k + 1 < toks.size(); ++k) {
        if (toks[k].kind != Tok::Kind::Ident || toks[k + 1].text != "(") continue;
        const auto* f = out.function(toks[k].text);
        if (f && toks[k].line == f->firstLine) continue;
        if (!f && projectFunctions.size() < 64) f = projectFunction(toks[k].text);
        if (!f) continue;
        std::size_t arg = 0, j = k + 2;
        int depth = 0;
        std::size_t argStart = j;
        for (; j < toks.size(); ++j) {
            const auto& t = toks[j];
            if (t.text == "(" || t.text == "[") { ++depth; continue; }
            if ((t.text == ")" || t.text == "]") && depth > 0) { --depth; continue; }
            if ((t.text == "," || t.text == ")") && depth == 0) {
                // Le nom seul d'une valeur pour un parametre enumeration : Suivant(Auto).
                if (j == argStart + 1 && toks[argStart].kind == Tok::Kind::Ident && arg < f->params.size()
                    && isValue(f->params[arg].type, toks[argStart]) && declared(toks[argStart].text, toks[argStart].line).empty())
                    out.names.push_back({toks[argStart].text, f->params[arg].type, toks[argStart].line, toks[argStart].line});
                if (j == argStart + 1 && plainInteger(toks[argStart]) && arg < f->params.size()
                    && !values(f->params[arg].type).empty()) {
                    const auto& p = f->params[arg];
                    const auto vals = values(p.type);
                    out.findings.push_back({toks[argStart].line, toks[argStart].column, static_cast<int>(toks[argStart].text.size()),
                                            f->name + " attend un " + p.type + " : " + toks[argStart].text + " est un INT \xC2\xB7 \xC3\xA9"
                                                "cris " + p.type + "#" + vals.front().first + " ou TO_" + p.type + "(" + toks[argStart].text + ")",
                                            true});
                }
                ++arg;
                argStart = j + 1;
                if (t.text == ")") break;
            }
        }
    }
}
} // namespace

Analysis analyze(std::string_view code, const std::function<bool(std::string_view)>& known,
                 const std::function<std::string(std::string_view)>& typeOf, const EnumValuesFn& enumValues,
                 const Project* project) {
    (void)known;
    Analysis out;
    // La syntaxe d'abord : le simulateur (dialecte IHM) lit tout le script.
    const auto words = wordsOf(code);
    // Les fonctions internes : FUNCTION Nom ... END_FUNCTION, chacune lue par le
    // simulateur pour sa signature exacte.
    for (std::size_t k = 0; k < words.size(); ++k) {
        if (words[k].up != "FUNCTION" || k + 1 >= words.size()) continue;
        std::size_t end = k + 1;
        while (end < words.size() && words[end].up != "END_FUNCTION") ++end;
        const std::size_t stop = end < words.size() ? words[end].at + words[end].text.size() : code.size();
        const std::string text(code.substr(words[k].at, stop - words[k].at));
        InnerFunction f;
        f.name = words[k + 1].text;
        f.firstLine = words[k].line;
        f.lastLine = end < words.size() ? words[end].line : words.back().line;
        // Les lignes avant la fonction : le simulateur compte ses lignes depuis 1.
        const std::string padded = std::string(static_cast<std::size_t>(f.firstLine - 1), '\n') + text;
        if (auto parsed = sim::parseFunction(padded, f.name)) fromSignature(sim::functionSignature(**parsed), f);
        // Ses locales (VAR, VAR_TEMP) et ses blocs VAR_INPUT / VAR_IN_OUT : leurs types.
        {
            const auto parts = splitDeclarations(text, true, [](std::string_view) { return true; });
            for (const auto& l : parts.locals) {
                bool param = false;
                for (const auto& pa : f.params) param = param || upper(pa.name) == upper(l.name);
                if (!param) f.locals.push_back(Param{Param::Mode::Value, l.name, l.type});
            }
        }
        // 1.11.20 : deux fonctions internes du meme nom sont des surcharges si leurs parametres
        // different (nombre, modes, types que le moteur distingue) ; de meme forme : refusees.
        const auto shapeOf = [](const InnerFunction& g) {
            overload::Signature sg;
            sg.name = g.name;
            for (const auto& pa : g.params)
                sg.params.push_back({pa.name, pa.type,
                                     pa.mode == Param::Mode::InOut ? overload::Mode::InOut
                                     : pa.mode == Param::Mode::Output ? overload::Mode::Out : overload::Mode::In,
                                     pa.mode != Param::Mode::InOut});
            return sg;
        };
        for (const auto& other : out.functions)
            if (upper(other.name) == upper(f.name) && overload::sameShape(shapeOf(other), shapeOf(f))) {
                out.findings.push_back({words[k + 1].line, words[k + 1].column, static_cast<int>(f.name.size()),
                                        "fonction interne d\xC3\xA9" "clar\xC3\xA9" "e deux fois avec les m\xC3\xAAmes param\xC3\xA8tres : "
                                            + shapeOf(f).shape() + " (d\xC3\xA9j\xC3\xA0 ligne " + std::to_string(other.firstLine) + ")",
                                        true});
                break;
            }
        out.functions.push_back(std::move(f));
        k = end;
    }
    // Les noms de FOR EACH k, v IN m : connus jusqu'au END_FOR qui ferme la boucle.
    for (std::size_t k = 0; k + 2 < words.size(); ++k) {
        if (words[k].up != "FOR" || words[k + 1].up != "EACH") continue;
        std::vector<std::size_t> names{k + 2};
        if (words[k + 2].next == ',' && k + 3 < words.size()) names.push_back(k + 3);
        int depth = 0;
        int last = words.back().line;
        for (std::size_t j = k + 1; j < words.size(); ++j) {
            if (words[j].up == "FOR") ++depth;
            if (words[j].up == "END_FOR") {
                if (depth == 0) { last = words[j].line; break; }
                --depth;
            }
        }
        for (const auto n : names) out.names.push_back({words[n].text, {}, words[k].line, last});
    }
    // Les appels aux fonctions internes : le nombre d'arguments (1.11.20 : celui de l'une de ses
    // surcharges ; le controle des appels, quand le code se lit, juge aussi les types).
    for (std::size_t wi = 0; wi < words.size(); ++wi) {
        const auto& w = words[wi];
        if (w.next != '(') continue;
        const auto* f = out.function(w.text);
        if (!f) continue;
        if (wi > 0 && words[wi - 1].up == "FUNCTION") continue;   // un en-tete (de chaque surcharge)
        const auto open = code.find('(', w.at + w.text.size());
        const int n = argumentCount(code, open);
        if (n < 0) continue;
        std::vector<const InnerFunction*> all;
        for (const auto& g : out.functions)
            if (upper(g.name) == upper(w.text)) all.push_back(&g);
        bool fits = false;
        for (const auto* g : all) {
            int inOut = 0;
            for (const auto& p : g->params) inOut += p.mode == Param::Mode::InOut ? 1 : 0;
            fits = fits || (n <= static_cast<int>(g->params.size()) && n >= inOut);
        }
        if (fits) continue;
        Finding bad;
        if (all.size() > 1) {
            bad = {w.line, w.column, static_cast<int>(w.text.size()),
                   f->name + " : aucune de ses " + std::to_string(all.size()) + " surcharges ne prend " + std::to_string(n) + " argument"
                       + (n > 1 ? "s" : ""),
                   true};
        } else {
            const int want = static_cast<int>(f->params.size());
            bad = {w.line, w.column, static_cast<int>(w.text.size()),
                   f->name + (n > want ? " : trop d'arguments (" : " : il manque des arguments (") + std::to_string(n) + " pour "
                       + std::to_string(want) + ")",
                   true};
        }
        bad.arity = true;
        out.findings.push_back(std::move(bad));
    }
    enumChecks(code, out, typeOf, enumValues, project);       // 1.10 (decision 15)
    return out;
}

const std::vector<Builtin>& builtins() {
    static const std::vector<Builtin> b = {
        {"MAP_HAS", "MAP_HAS(m, cl\xC3\xA9) : BOOL", "Vrai si la cl\xC3\xA9 existe dans la MAP."},
        {"MAP_GET", "MAP_GET(m, cl\xC3\xA9, d\xC3\xA9" "faut) : T", "La valeur de la cl\xC3\xA9, ou d\xC3\xA9" "faut si elle manque."},
        {"MAP_REMOVE", "MAP_REMOVE(m, cl\xC3\xA9) : BOOL", "Retire la cl\xC3\xA9 (vrai si elle existait)."},
        {"MAP_SIZE", "MAP_SIZE(m) : DINT", "Le nombre de cl\xC3\xA9s."},
        {"MAP_CLEAR", "MAP_CLEAR(m)", "Vide la MAP."},
        {"MAP_KEYS", "MAP_KEYS(m) : ARRAY OF K", "Les cl\xC3\xA9s, tri\xC3\xA9" "es, dans un tableau."},
        {"MAP_BEGIN", "MAP_BEGIN(m) : MAP_ITERATOR", "Un it\xC3\xA9rateur sur la premi\xC3\xA8re cl\xC3\xA9 (it.Key, it.Value)."},
        {"MAP_NEXT", "MAP_NEXT(it) : BOOL", "Avance l'it\xC3\xA9rateur \xC3\xA0 la cl\xC3\xA9 suivante."},
        {"MAP_END", "MAP_END(it) : BOOL", "Vrai quand l'it\xC3\xA9rateur a d\xC3\xA9pass\xC3\xA9 la derni\xC3\xA8re cl\xC3\xA9."},
        {"REF", "REF(variable) : REF_TO T", "Une r\xC3\xA9" "f\xC3\xA9rence vers la variable (r^ est la variable)."},
        {"ADR", "ADR(variable) : POINTER TO T", "Un pointeur vers la variable (p^ est la variable)."},
        {"LOWER_BOUND", "LOWER_BOUND(t, dimension) : DINT", "La borne basse d'une dimension (1 = la premi\xC3\xA8re)."},
        {"UPPER_BOUND", "UPPER_BOUND(t, dimension) : DINT", "La borne haute d'une dimension (1 = la premi\xC3\xA8re)."},
        {"SIZEOF", "SIZEOF(t) : DINT", "Le nombre de cases d'un tableau (toutes dimensions)."},
        {"TO_UPPER", "TO_UPPER(texte) : STRING", "Le texte en MAJUSCULES."},
        {"TO_LOWER", "TO_LOWER(texte) : STRING", "Le texte en minuscules."},
        {"ASSERT", "ASSERT(condition, 'message')", "Faux : une erreur (le message), le script s'arr\xC3\xAAte - dans un TRY, son CATCH."},   // 1.12.1
    };
    return b;
}

const std::vector<Builtin>& snippets() {
    static const std::vector<Builtin> s = {
        {"FUNCTION", "FUNCTION Nom(x : REAL) : REAL\n    Nom := x;\nEND_FUNCTION", "Une fonction interne du script."},
        {"FOR EACH", "FOR EACH cle, valeur IN m DO\n    \nEND_FOR", "Parcourir une MAP (cl\xC3\xA9s croissantes) ou un tableau."},
        {"RETURN", "RETURN valeur;", "Rendre la valeur de la fonction et en sortir."},
        {"ARRAY", "ARRAY[0..9] OF REAL", "Un tableau (plusieurs dimensions : ARRAY[0..3, 0..9] OF REAL)."},
        {"MAP", "MAP[STRING] OF REAL", "Un dictionnaire : des valeurs sous des cl\xC3\xA9s (STRING ou DINT)."},
        {"REF_TO", "REF_TO REAL", "Une r\xC3\xA9" "f\xC3\xA9rence : r := REF(x); r^ := 1.0;"},
        {"POINTER TO", "POINTER TO REAL", "Un pointeur : p := ADR(x); IF p <> NULL THEN p^ := 1.0; END_IF"},
        {"VAR_IN_OUT", "VAR_IN_OUT x : REAL", "Un param\xC3\xA8tre par r\xC3\xA9" "f\xC3\xA9rence : la fonction \xC3\xA9" "crit dans la variable donn\xC3\xA9" "e."},
        {"MAP_ITERATOR", "MAP_ITERATOR", "Un it\xC3\xA9rateur de MAP (MAP_BEGIN, MAP_NEXT, MAP_END, it.Key, it.Value)."},
        {"NULL", "NULL", "Le pointeur qui ne vise rien."},
        // 1.12.1
        {"TRY", "TRY\n    \nCATCH Erreur\n    \nEND_TRY", "Essayer : une erreur du bloc va au CATCH (son message dans Erreur) au lieu d'arr\xC3\xAAter le script."},
        {"CONTINUE", "CONTINUE;", "Le tour suivant de la boucle."},
    };
    return s;
}

bool isBuiltin(std::string_view name) noexcept {
    const std::string u = upper(name);
    for (const auto& b : builtins())
        if (u == b.name) return true;
    return false;
}

} // namespace hmi::lang110
