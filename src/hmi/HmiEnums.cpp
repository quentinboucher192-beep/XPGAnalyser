// hmi/HmiEnums.cpp - les enumerations IHM (1.10, decision 15, chantier E).
#include "HmiEnums.hpp"

#include "HmiOperators.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>

namespace hmi {

namespace {

std::string upperAscii(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}
std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}
bool sameName(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Une chaine ST entre apostrophes : $ -> $$, ' -> $', fin de ligne -> $N.
std::string stQuoted(std::string_view s) {
    std::string out = "'";
    for (const char ch : s) {
        if (ch == '$') out += "$$";
        else if (ch == '\'') out += "$'";
        else if (ch == '\n') out += "$N";
        else if (ch != '\r') out += ch;
    }
    return out + "'";
}

bool parseInt64(std::string_view s, std::int64_t& out) {
    s = trimmed(s);
    if (s.empty()) return false;
    const char* b = s.data();
    const char* e = s.data() + s.size();
    if (*b == '+') ++b;
    const auto r = std::from_chars(b, e, out);
    return r.ec == std::errc{} && r.ptr == e;
}

// Les mots reserves du ST (et du dialecte IHM) : pas des noms de valeur.
bool reservedWord(std::string_view name) {
    static const std::set<std::string> kWords = {
        "IF", "THEN", "ELSE", "ELSIF", "END_IF", "CASE", "OF", "END_CASE", "FOR", "TO", "BY", "DO", "END_FOR",
        "WHILE", "END_WHILE", "REPEAT", "UNTIL", "END_REPEAT", "RETURN", "EXIT", "CONTINUE", "TRUE", "FALSE",
        "AND", "OR", "XOR", "NOT", "MOD", "VAR", "VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT", "VAR_TEMP", "END_VAR",
        "FUNCTION", "END_FUNCTION", "EACH", "IN", "ARRAY", "MAP", "POINTER", "REF_TO", "REFERENCE", "NULL",
        "BOOL", "INT", "UINT", "DINT", "UDINT", "WORD", "DWORD", "REAL", "LREAL", "STRING", "TIME", "BYTE",
        "SINT", "USINT", "LINT", "ULINT", "LWORD", "DATE", "TOD", "DT", "TYPE", "END_TYPE", "STRUCT", "END_STRUCT"};
    return kWords.count(upperAscii(name)) != 0;
}

// Parcourt un texte ST : chaque litteral <ident>#<ident> hors des chaines et des
// commentaires est passe a `on(type, value, newType, newValue)` ; vrai : remplace.
std::string rewriteLiterals(std::string_view code,
                            const std::function<bool(std::string_view, std::string_view, std::string&, std::string&)>& on) {
    std::string out;
    out.reserve(code.size());
    std::size_t i = 0;
    const std::size_t n = code.size();
    while (i < n) {
        const char c = code[i];
        // Les commentaires (* ... *) et // ... ; les chaines ' ' et " " (echappement $).
        if (c == '(' && i + 1 < n && code[i + 1] == '*') {
            const auto end = code.find("*)", i + 2);
            const std::size_t stop = end == std::string_view::npos ? n : end + 2;
            out.append(code.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < n && code[i + 1] == '/') {
            const auto end = code.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? n : end;
            out.append(code.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < n && code[j] != c) j += code[j] == '$' && j + 1 < n ? 2 : 1;
            const std::size_t stop = std::min(n, j + 1);
            out.append(code.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (identStart(c) && (i == 0 || !identChar(code[i - 1]))) {
            std::size_t j = i;
            while (j < n && identChar(code[j])) ++j;
            const std::string_view type = code.substr(i, j - i);
            if (j + 1 < n && code[j] == '#' && identStart(code[j + 1])) {
                std::size_t k = j + 1;
                while (k < n && identChar(code[k])) ++k;
                const std::string_view value = code.substr(j + 1, k - j - 1);
                std::string newType(type), newValue(value);
                if (on(type, value, newType, newValue)) {
                    out += newType + "#" + newValue;
                } else {
                    out.append(code.substr(i, k - i));
                }
                i = k;
                continue;
            }
            out.append(type);
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// Tous les textes ST du projet, a reecrire (les memes que renameFunctionEverywhere,
// plus les scripts des operateurs).
std::size_t rewriteEverywhere(Project& p, const std::function<std::string(std::string_view)>& rewrite) {
    std::size_t changed = 0;
    const auto code = [&](std::string& text) {
        if (text.find('#') == std::string::npos) return;
        auto next = rewrite(text);
        if (next != text) { text = std::move(next); ++changed; }
    };
    const auto actions = [&](std::vector<Action>& list) {
        for (auto& a : list) {
            code(a.watch);
            code(a.guard);
            if (a.operation != Operation::Log) code(a.value);
        }
    };
    for (auto& sc : p.programs.scripts) if (sc.lang == ScriptLang::ST) { code(sc.body); code(sc.watch); }
    for (auto& f : p.programs.functions) code(f.body);
    for (auto& ty : p.programs.types) for (auto& o : ty.operators) code(o.body);
    for (auto& v : p.views) {
        for (auto& sc : v.scripts) if (sc.lang == ScriptLang::ST) code(sc.body);
        for (auto& o : v.operators) code(o.body);
        actions(v.actions);
        for (auto& o : v.objects) {
            actions(o.actions);
            for (auto& pr : o.props) code(pr.expr);
        }
    }
    for (auto& a : p.alarms) code(a.condition);
    return changed;
}

} // namespace

// ------------------------------------------------------------- les genres ---
bool isEnumeration(const HmiType& t) noexcept { return t.kind == HmiTypeKind::Enumeration; }

std::string_view typeKindKey(HmiTypeKind k) noexcept {
    return k == HmiTypeKind::Enumeration ? "enumeration" : "structure";
}
HmiTypeKind typeKindFromKey(std::string_view key) noexcept {
    return sameName(key, "enumeration") ? HmiTypeKind::Enumeration : HmiTypeKind::Structure;
}
std::string_view typeKindLabel(HmiTypeKind k) noexcept {
    return k == HmiTypeKind::Enumeration ? "\xC3\xA9num\xC3\xA9ration" : "structure";
}

// ---------------------------------------------------------------- trouver ---
const HmiType* findEnumeration(const Project& p, std::string_view name) {
    for (const auto& t : p.programs.types)
        if (isEnumeration(t) && sameName(t.name, trimmed(name))) return &t;
    return nullptr;
}
HmiType* findEnumeration(Project& p, std::string_view name) {
    for (auto& t : p.programs.types)
        if (isEnumeration(t) && sameName(t.name, trimmed(name))) return &t;
    return nullptr;
}
std::vector<const HmiType*> enumerations(const Project& p) {
    std::vector<const HmiType*> out;
    for (const auto& t : p.programs.types) if (isEnumeration(t)) out.push_back(&t);
    return out;
}
const HmiEnumValue* enumValueByName(const HmiType& t, std::string_view name) {
    name = trimmed(name);
    for (const auto& v : t.values) if (sameName(v.name, name)) return &v;
    return nullptr;
}
const HmiEnumValue* enumValueByNumber(const HmiType& t, std::int64_t value) {
    for (const auto& v : t.values) if (v.value == value) return &v;
    return nullptr;
}
int enumValueIndex(const HmiType& t, std::string_view name) {
    name = trimmed(name);
    for (std::size_t i = 0; i < t.values.size(); ++i)
        if (sameName(t.values[i].name, name)) return static_cast<int>(i);
    return -1;
}
std::string enumText(const HmiEnumValue& v) { return v.text.empty() ? v.name : v.text; }
std::string enumLiteral(const HmiType& t, const HmiEnumValue& v) { return t.name + "#" + v.name; }

bool parseEnumLiteral(const Project& p, std::string_view text, const HmiType** type, const HmiEnumValue** value) {
    text = trimmed(text);
    const auto hash = text.find('#');
    if (hash == std::string_view::npos || hash == 0) return false;
    const HmiType* t = findEnumeration(p, text.substr(0, hash));
    if (!t) return false;
    const HmiEnumValue* v = enumValueByName(*t, text.substr(hash + 1));
    if (!v) return false;
    if (type) *type = t;
    if (value) *value = v;
    return true;
}

std::string enumDisplay(const HmiType& t, std::int64_t value) {
    const HmiEnumValue* v = enumValueByNumber(t, value);
    return (v ? v->name : std::string("?")) + " (" + std::to_string(value) + ")";
}

bool enumNumberOf(const HmiType& t, std::string_view text, std::int64_t& out) {
    text = trimmed(text);
    if (text.empty()) return false;
    if (const auto hash = text.find('#'); hash != std::string_view::npos) {
        if (!sameName(trimmed(text.substr(0, hash)), t.name)) return false;
        text = trimmed(text.substr(hash + 1));
    }
    if (const HmiEnumValue* v = enumValueByName(t, text)) { out = v->value; return true; }
    for (const auto& v : t.values)
        if (!v.text.empty() && sameName(v.text, text)) { out = v.value; return true; }
    // "Auto (1)" : ce que montre une variable.
    if (const auto open = text.rfind(" ("); open != std::string_view::npos && text.back() == ')') {
        if (const HmiEnumValue* v = enumValueByName(t, text.substr(0, open))) { out = v->value; return true; }
    }
    std::int64_t n = 0;
    if (parseInt64(text, n) && enumValueByNumber(t, n)) { out = n; return true; }
    return false;
}

// ----------------------------------------------------------- les controles ---
bool validEnumName(std::string_view name) {
    if (name.empty() || !identStart(name.front())) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (!identChar(name[i])) return false;
        if (name[i] == '_' && i + 1 < name.size() && name[i + 1] == '_') return false;
    }
    if (name.back() == '_') return false;
    return !reservedWord(name);
}

std::vector<EnumIssue> enumIssues(const HmiType& t) {
    std::vector<EnumIssue> out;
    if (t.values.empty()) {
        out.push_back({-1, {}, "L'\xC3\xA9num\xC3\xA9ration " + t.name + " n'a aucune valeur."});
        return out;
    }
    constexpr std::int64_t kMin = std::numeric_limits<std::int32_t>::min();
    constexpr std::int64_t kMax = std::numeric_limits<std::int32_t>::max();
    for (std::size_t i = 0; i < t.values.size(); ++i) {
        const auto& v = t.values[i];
        const int idx = static_cast<int>(i);
        if (v.name.empty()) {
            out.push_back({idx, "nom", "Valeur n\xC2\xB0 " + std::to_string(i + 1) + " : le nom est vide."});
        } else if (reservedWord(v.name) && validEnumName(std::string(v.name) + "x")) {
            out.push_back({idx, "nom", "\xC2\xAB " + v.name + " \xC2\xBB est un mot r\xC3\xA9serv\xC3\xA9 du ST."});
        } else if (!validEnumName(v.name)) {
            out.push_back({idx, "nom", "\xC2\xAB " + v.name + " \xC2\xBB n'est pas un nom ST (une lettre ou _, puis lettres, chiffres, _)."});
        } else {
            for (std::size_t j = 0; j < i; ++j)
                if (sameName(t.values[j].name, v.name)) {
                    out.push_back({idx, "nom", "Nom en double : \xC2\xAB " + v.name + " \xC2\xBB (d\xC3\xA9j\xC3\xA0 la valeur n\xC2\xB0 "
                                                   + std::to_string(j + 1) + ")."});
                    break;
                }
        }
        if (v.value < kMin || v.value > kMax) {
            out.push_back({idx, "valeur", std::to_string(v.value) + " sort des DINT (" + std::to_string(kMin) + " \xC3\xA0 "
                                              + std::to_string(kMax) + ")."});
        } else {
            for (std::size_t j = 0; j < i; ++j)
                if (t.values[j].value == v.value) {
                    out.push_back({idx, "valeur", "Valeur en double : " + std::to_string(v.value) + " (d\xC3\xA9j\xC3\xA0 \xC2\xAB "
                                                      + t.values[j].name + " \xC2\xBB)."});
                    break;
                }
        }
    }
    return out;
}

// ------------------------------------------------------------- proposer -----
std::int64_t nextEnumNumber(const HmiType& t) {
    if (t.values.empty()) return 0;
    std::int64_t best = t.values.front().value;
    for (const auto& v : t.values) best = std::max(best, v.value);
    return best < std::numeric_limits<std::int32_t>::max() ? best + 1 : best;
}
std::string nextEnumName(const HmiType& t, std::string_view base) {
    for (int k = 1;; ++k) {
        std::string name = std::string(base) + std::to_string(k);
        if (!enumValueByName(t, name)) return name;
    }
}
HmiEnumValue nextEnumValue(const HmiType& t) {
    HmiEnumValue v;
    v.name = nextEnumName(t);
    v.value = nextEnumNumber(t);
    return v;
}

// ---------------------------------------------------- toString, fromString ---
std::string enumToStringScript(const HmiType& t) {
    HmiOperator o;
    o.op = "TO";
    o.left = t.name;
    o.result = "STRING";
    std::string s = "(* " + operatorSignature(o) + " *)\n";
    s += "(* le toString de " + t.name + " : a est la valeur ; le r\xC3\xA9sultat, son texte affich\xC3\xA9 *)\n";
    if (t.values.empty()) return s + "TO_STRING := '';\n";
    s += "CASE a OF\n";
    for (const auto& v : t.values) s += "    " + enumLiteral(t, v) + ": TO_STRING := " + stQuoted(enumText(v)) + ";\n";
    s += "ELSE\n    TO_STRING := '?';\nEND_CASE;\n";
    return s;
}

std::string enumFromStringScript(const HmiType& t) {
    HmiOperator o;
    o.op = "TO";
    o.left = "STRING";
    o.result = t.name;
    const std::string fn = "TO_" + t.name;
    std::string s = "(* " + operatorSignature(o) + " *)\n";
    s += "(* le fromString de " + t.name + " : a est le texte ; le nom ou le texte affich\xC3\xA9 (tel quel, en MAJUSCULES\n"
         "   ou en minuscules) ; sinon la premi\xC3\xA8re valeur *)\n";
    if (t.values.empty()) return s;
    bool first = true;
    for (const auto& v : t.values) {
        std::vector<std::string> forms;
        for (const std::string& f : {v.name, upperAscii(v.name), lowerAscii(v.name), enumText(v), upperAscii(enumText(v)),
                                     lowerAscii(enumText(v))})
            if (std::find(forms.begin(), forms.end(), f) == forms.end()) forms.push_back(f);
        std::string cond;
        for (const auto& f : forms) cond += (cond.empty() ? "" : " OR ") + std::string("a = ") + stQuoted(f);
        s += std::string(first ? "IF " : "ELSIF ") + cond + " THEN\n    " + fn + " := " + enumLiteral(t, v) + ";\n";
        first = false;
    }
    s += "ELSE\n    " + fn + " := " + enumLiteral(t, t.values.front()) + ";\nEND_IF;\n";
    return s;
}

HmiOperator enumToStringOperator(const HmiType& t) {
    HmiOperator o;
    o.op = "TO";
    o.left = t.name;
    o.result = "STRING";
    o.body = enumToStringScript(t);
    o.description = "toString : le texte affich\xC3\xA9 de la valeur.";
    return o;
}
HmiOperator enumFromStringOperator(const HmiType& t) {
    HmiOperator o;
    o.op = "TO";
    o.left = "STRING";
    o.result = t.name;
    o.body = enumFromStringScript(t);
    o.description = "fromString : la valeur d'apr\xC3\xA8s son nom ou son texte affich\xC3\xA9.";
    return o;
}
bool isEnumToString(const HmiType& t, const HmiOperator& o) noexcept {
    return o.op == "TO" && o.right.empty() && sameName(o.left, t.name) && sameName(o.result, "STRING");
}
bool isEnumFromString(const HmiType& t, const HmiOperator& o) noexcept {
    return o.op == "TO" && o.right.empty() && sameName(o.left, "STRING") && sameName(o.result, t.name);
}
std::string_view enumConversionRole(const HmiType& t, const HmiOperator& o) noexcept {
    if (!isEnumeration(t)) return {};
    if (isEnumToString(t, o)) return "toString";
    if (isEnumFromString(t, o)) return "fromString";
    return {};
}

HmiType makeEnumeration(Project& p, std::string_view name) {
    HmiType t;
    t.kind = HmiTypeKind::Enumeration;
    std::string base = std::string(trimmed(name));
    if (base.empty()) base = "T_MODE";
    const auto taken = [&](const std::string& n) {
        return std::any_of(p.programs.types.begin(), p.programs.types.end(), [&](const HmiType& x) { return sameName(x.name, n); });
    };
    t.name = base;
    for (int k = 2; taken(t.name); ++k) t.name = base + std::to_string(k);
    t.id = p.allocate();
    t.values.push_back({"Arret", 0, "Arr\xC3\xAAt", ""});
    t.values.push_back({"Marche", 1, "En marche", ""});
    t.operators.push_back(enumToStringOperator(t));
    t.operators.push_back(enumFromStringOperator(t));
    renumberOperators(p, t.operators);
    return t;
}

std::size_t regenerateEnumConversions(Project& p, HmiType& t) {
    if (!isEnumeration(t)) return 0;
    bool hasTo = false, hasFrom = false;
    for (auto& o : t.operators) {
        if (isEnumToString(t, o)) { o.body = enumToStringScript(t); hasTo = true; }
        else if (isEnumFromString(t, o)) { o.body = enumFromStringScript(t); hasFrom = true; }
    }
    std::vector<HmiOperator> added;
    if (!hasTo) added.push_back(enumToStringOperator(t));
    if (!hasFrom) added.push_back(enumFromStringOperator(t));
    renumberOperators(p, added);
    for (auto& o : added) t.operators.push_back(std::move(o));
    return 2;
}

// ------------------------------------------------------ renommer, copier -----
std::string renameEnumValueInText(std::string_view code, std::string_view type, std::string_view from, std::string_view to) {
    return rewriteLiterals(code, [&](std::string_view ty, std::string_view value, std::string&, std::string& newValue) {
        if (!sameName(ty, type) || !sameName(value, from)) return false;
        newValue = std::string(to);
        return true;
    });
}
std::string renameEnumTypeInText(std::string_view code, std::string_view from, std::string_view to) {
    return rewriteLiterals(code, [&](std::string_view ty, std::string_view, std::string& newType, std::string&) {
        if (!sameName(ty, from)) return false;
        newType = std::string(to);
        return true;
    });
}
std::size_t renameEnumValue(Project& p, std::string_view type, std::string_view from, std::string_view to) {
    if (sameName(from, to) && from == to) return 0;
    return rewriteEverywhere(p, [&](std::string_view text) { return renameEnumValueInText(text, type, from, to); });
}
std::size_t renameEnumType(Project& p, std::string_view from, std::string_view to) {
    if (from == to) return 0;
    return rewriteEverywhere(p, [&](std::string_view text) { return renameEnumTypeInText(text, from, to); });
}
void copyEnumerationLiterals(HmiType& copy, std::string_view from) {
    for (auto& o : copy.operators) o.body = renameEnumTypeInText(o.body, from, copy.name);
}

} // namespace hmi
