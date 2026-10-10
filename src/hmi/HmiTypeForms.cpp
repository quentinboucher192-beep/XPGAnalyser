// hmi/HmiTypeForms.cpp - 1.12.2 : la forme d'un type (Simple, Tableau, Liste, Vecteur,
// Dictionnaire, Tuple), pour tous les createurs de variables.
#include "HmiTypeForms.hpp"

#include "HmiEnums.hpp"
#include "HmiTypeRegistry.hpp"
#include "HmiTypes.hpp"
#include "../sim/Interpreter.hpp"

#include <cctype>

namespace hmi::typeform {

namespace {

namespace tr = typereg;

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool sameText(std::string_view a, std::string_view b) { return upperOf(a) == upperOf(b); }

// Coupe aux virgules de premier niveau (hors crochets, parentheses et textes).
std::vector<std::string> splitTop(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    char quote = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) {
            cur += c;
            if (c == '$' && i + 1 < s.size()) cur += s[++i];          // $' : une apostrophe dans le texte
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') quote = c;
        else if (c == '[' || c == '(') ++depth;
        else if ((c == ']' || c == ')') && depth > 0) --depth;
        if (c == ',' && depth == 0) {
            out.push_back(trimmed(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trimmed(cur).empty() || !out.empty()) out.push_back(trimmed(cur));
    return out;
}

// Le crochet (ou la parenthese) qui ferme celui de `open` ; npos : aucun.
std::size_t closing(std::string_view s, std::size_t open) {
    const char o = s[open], c = o == '[' ? ']' : ')';
    int depth = 0;
    char quote = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        const char x = s[i];
        if (quote) {
            if (x == quote) quote = 0;
            continue;
        }
        if (x == '\'' || x == '"') quote = x;
        else if (x == o) ++depth;
        else if (x == c && --depth == 0) return i;
    }
    return std::string_view::npos;
}

// Un environnement pour lire une valeur sans rien executer : les types IHM du projet
// (leurs membres, les valeurs des enumerations), aucune variable.
class TypingEnv final : public sim::Environment {
public:
    explicit TypingEnv(const Project& p) : p_(p) {}
    bool read(std::string_view, sim::Value&) override { return false; }
    bool write(std::string_view, const sim::Value&) override { return false; }
    bool exists(std::string_view) override { return false; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override { return false; }
    void report(sim::Diagnostic) override {}
    bool structMembers(std::string_view typeName, std::vector<std::pair<std::string, std::string>>& out) override {
        if (const auto* e = findEnumeration(p_, typeName)) {
            for (const auto& v : e->values) out.emplace_back(v.name, "#" + std::to_string(v.value));
            return !out.empty();
        }
        for (const auto& m : types::membersOf(p_, typeName)) out.emplace_back(m.name, m.type);
        return !out.empty();
    }
private:
    const Project& p_;
};

// "line 1: expected ']' at the end of the list" -> en francais, sans le numero de ligne.
std::string frenchOf(std::string m) {
    if (m.rfind("line 1: ", 0) == 0) m.erase(0, 8);
    if (m.rfind("ligne 1 : ", 0) == 0) m.erase(0, 10);
    struct Pair { const char* en; const char* fr; };
    static const Pair kPairs[] = {
        {"expected ']' at the end of the list", "il manque ']' \xC3\xA0 la fin de la liste"},
        {"expected ')' at the end of the tuple (a, b)", "il manque ')' \xC3\xA0 la fin du tuple"},
        {"a list mixes values and 'key := value' entries", "la liste m\xC3\xAAle des valeurs et des entr\xC3\xA9" "es 'cl\xC3\xA9 := valeur'"},
        {"expected 'key := value' (a MAP literal: ['a' := 1, 'b' := 2])", "une MAP s'\xC3\xA9" "crit ['a' := 1, 'b' := 2]"},
        {"expected a value", "une valeur est attendue"},
        {"expected the end of the condition", "du texte en trop apr\xC3\xA8s la valeur"},
    };
    for (const auto& [en, fr] : kPairs)
        if (const auto at = m.find(en); at != std::string::npos) m.replace(at, std::string_view(en).size(), fr);
    return m;
}

} // namespace

std::string_view label(Form f) noexcept {
    switch (f) {
        case Form::Simple: return "Simple";
        case Form::Array: return "Tableau";
        case Form::Array2D: return "Tableau 2D";
        case Form::List: return "Liste";
        case Form::Vector: return "Vecteur";
        case Form::Map: return "Dictionnaire (MAP)";
        case Form::Tuple: return "Tuple";
    }
    return "Simple";
}

std::string_view key(Form f) noexcept {
    switch (f) {
        case Form::Simple: return "simple";
        case Form::Array: return "tableau";
        case Form::Array2D: return "tableau2d";
        case Form::List: return "liste";
        case Form::Vector: return "vecteur";
        case Form::Map: return "map";
        case Form::Tuple: return "tuple";
    }
    return "simple";
}

std::optional<Form> fromText(std::string_view text) noexcept {
    const std::string t = trimmed(text);
    for (const auto f : kForms)
        if (sameText(t, label(f)) || sameText(t, key(f))) return f;
    if (sameText(t, "Dictionnaire") || sameText(t, "MAP")) return Form::Map;
    if (sameText(t, "LIST")) return Form::List;
    if (sameText(t, "VECTOR")) return Form::Vector;
    if (sameText(t, "ARRAY")) return Form::Array;
    return std::nullopt;
}

std::string_view parameterLabel(Form f) noexcept {
    switch (f) {
        case Form::Array:
        case Form::Array2D: return "Bornes";
        case Form::Map: return "Cl\xC3\xA9";
        case Form::Tuple: return "Puis";
        default: return {};
    }
}

std::string_view parameterHint(Form f) noexcept {
    switch (f) {
        case Form::Array: return "0..9 : 10 cases ; 1..4 ; -5..5";
        case Form::Array2D: return "0..3, 0..9 : 4 lignes de 10 colonnes";
        case Form::Map: return "le type des cl\xC3\xA9s : STRING, un entier (DINT) ou une \xC3\xA9num\xC3\xA9ration";
        case Form::Tuple: return "les types qui suivent le premier : STRING, BOOL";
        default: return {};
    }
}

std::string defaultParameter(Form f) {
    switch (f) {
        case Form::Array: return "0..9";
        case Form::Array2D: return "0..3, 0..9";
        case Form::Map: return "STRING";
        case Form::Tuple: return "STRING";
        default: return {};
    }
}

std::string_view summary(Form f) noexcept {
    switch (f) {
        case Form::Simple: return "Une seule valeur.";
        case Form::Array: return "Un tableau de taille fixe : T[i], T.Length ; li\xC3\xA9, ses cases suivent son adresse.";
        case Form::Array2D: return "Un tableau \xC3\xA0 deux dimensions : M[i, j], M.Rows, M.Columns.";
        case Form::List:
            return "Une liste de taille variable : L[0], L.Count, LIST_ADD, LIST_REMOVE, FOR EACH x IN L - dans la m\xC3\xA9moire de l'IHM.";
        case Form::Vector:
            return "Un vecteur de taille variable : V[0], VECTOR_PUSH, VECTOR_POP, VECTOR_RESIZE - dans la m\xC3\xA9moire de l'IHM.";
        case Form::Map:
            return "Un dictionnaire cl\xC3\xA9 \xE2\x86\x92 valeur : M['cl\xC3\xA9'], MAP_HAS, MAP_GET, FOR EACH k, v IN M - dans la m\xC3\xA9moire de l'IHM.";
        case Form::Tuple: return "Des valeurs de types fixes : t.Item1, t.Item2... - dans la m\xC3\xA9moire de l'IHM.";
    }
    return {};
}

std::string_view valueHint(Form f) noexcept {
    switch (f) {
        case Form::Simple: return "0, TRUE, 2.5, 'texte', T#5s";
        case Form::Array:
        case Form::Array2D: return "0 (toutes les cases), 1, 2, 3 ou [1, 2, 3]";
        case Form::List:
        case Form::Vector: return "[1, 2, 3] (vide : vide)";
        case Form::Map: return "['a' := 1, 'b' := 2] (vide : vide)";
        case Form::Tuple: return "(1, 'texte')";
    }
    return {};
}

bool dynamic(Form f) noexcept { return f == Form::List || f == Form::Vector || f == Form::Map; }
bool memoryOnly(Form f) noexcept { return dynamic(f) || f == Form::Tuple; }

bool allowed(Form f, unsigned use) noexcept {
    switch (f) {
        case Form::Simple: return true;
        case Form::Array:
        case Form::Array2D: return (use & (tr::UseVariable | tr::UseDeclaration | tr::UseParameter | tr::UseReturn)) != 0;
        default: return (use & (tr::UseVariable | tr::UseDeclaration | tr::UseReturn)) != 0;
    }
}

std::vector<Form> forms(unsigned use) {
    std::vector<Form> out;
    for (const auto f : kForms)
        if (allowed(f, use)) out.push_back(f);
    return out;
}

std::vector<std::string> labels(unsigned use) {
    std::vector<std::string> out;
    for (const auto f : forms(use)) out.emplace_back(label(f));
    return out;
}

bool referenceAllowed(unsigned use) noexcept { return (use & (tr::UseDeclaration | tr::UseReturn)) != 0; }

std::string compose(const Shape& s) {
    std::string e = trimmed(s.element);
    if (e.empty()) e = "INT";
    if (s.reference) e = "REF_TO " + e;
    std::string p = trimmed(s.parameter);
    // Des bornes ecrites [0..9] : sans leurs crochets.
    while (!p.empty() && (p.front() == '[' || p.front() == '(')) p.erase(p.begin());
    while (!p.empty() && (p.back() == ']' || p.back() == ')')) p.pop_back();
    p = trimmed(p);
    if (p.empty() && s.form != Form::Tuple) p = defaultParameter(s.form);   // un tuple d'un seul type reste seul
    switch (s.form) {
        case Form::Simple: return e;
        case Form::Array:
        case Form::Array2D: return "ARRAY[" + p + "] OF " + e;
        case Form::List: return "LIST OF " + e;
        case Form::Vector: return "VECTOR OF " + e;
        case Form::Map: return "MAP[" + p + "] OF " + e;
        case Form::Tuple: return "TUPLE(" + e + (p.empty() ? std::string{} : ", " + p) + ")";
    }
    return e;
}

Shape decompose(std::string_view type) {
    Shape s;
    std::string t = trimmed(type);
    s.element = t;
    const std::string u = upperOf(t);
    const auto word = [&](std::string_view w) {
        return u.rfind(w, 0) == 0 && (u.size() == w.size() || !(std::isalnum(static_cast<unsigned char>(u[w.size()])) || u[w.size()] == '_'));
    };
    const auto afterOf = [&](std::size_t from) -> std::optional<std::string> {   // " OF reste" apres `from`
        const std::string rest = trimmed(std::string_view(t).substr(from));
        if (upperOf(rest).rfind("OF", 0) != 0 || rest.size() < 3 || !std::isspace(static_cast<unsigned char>(rest[2]))) return std::nullopt;
        return trimmed(std::string_view(rest).substr(2));
    };
    if (word("ARRAY")) {
        const auto open = t.find('[');
        const auto close = open == std::string::npos ? open : closing(t, open);
        if (close != std::string::npos)
            if (auto e = afterOf(close + 1)) {
                s.parameter = trimmed(std::string_view(t).substr(open + 1, close - open - 1));
                s.form = splitTop(s.parameter).size() > 1 ? Form::Array2D : Form::Array;
                s.element = *e;
            }
    } else if (word("LIST") || word("VECTOR")) {
        const bool vector = word("VECTOR");
        if (auto e = afterOf(vector ? 6 : 4)) {
            s.form = vector ? Form::Vector : Form::List;
            s.element = *e;
        }
    } else if (word("MAP")) {
        const auto open = t.find('[');
        const auto close = open == std::string::npos ? open : closing(t, open);
        if (close != std::string::npos)
            if (auto e = afterOf(close + 1)) {
                s.form = Form::Map;
                s.parameter = trimmed(std::string_view(t).substr(open + 1, close - open - 1));
                s.element = *e;
            }
    } else if (word("TUPLE")) {
        const auto open = t.find('(');
        const auto close = open == std::string::npos ? open : closing(t, open);
        if (close != std::string::npos && trimmed(std::string_view(t).substr(close + 1)).empty()) {
            const auto parts = splitTop(std::string_view(t).substr(open + 1, close - open - 1));
            if (!parts.empty()) {
                s.form = Form::Tuple;
                s.element = parts.front();
                s.parameter.clear();
                for (std::size_t i = 1; i < parts.size(); ++i) s.parameter += (i > 1 ? ", " : "") + parts[i];
            }
        }
    }
    if (upperOf(s.element).rfind("REF_TO ", 0) == 0) {
        s.reference = true;
        s.element = trimmed(std::string_view(s.element).substr(7));
    }
    return s;
}

bool valueFits(const Project& p, std::string_view type, std::string_view value, std::string* why) {
    if (trimmed(value).empty()) return true;
    TypingEnv env(p);
    std::string w;
    if (sim::makeRichValue(type, value, env, &w)) return true;
    if (why) *why = frenchOf(w);
    return false;
}

bool literalItems(std::string_view text, std::vector<std::string>& out) {
    out.clear();
    const std::string t = trimmed(text);
    if (t.size() < 2) return false;
    const char o = t.front();
    if ((o != '[' && o != '(') || closing(t, 0) != t.size() - 1) return false;
    out = splitTop(std::string_view(t).substr(1, t.size() - 2));
    if (out.size() == 1 && out.front().empty()) out.clear();
    return true;
}

std::string literalOf(const std::vector<std::string>& items, bool tuple) {
    std::string s = tuple ? "(" : "[";
    bool first = true;
    for (const auto& i : items) {
        const std::string x = trimmed(i);
        if (x.empty()) continue;
        s += (first ? "" : ", ") + x;
        first = false;
    }
    return s + (tuple ? ")" : "]");
}

} // namespace hmi::typeform
