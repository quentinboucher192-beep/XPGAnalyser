// =============================================================================
//  hmi/HmiWhy.cpp - 1.12.27 : « Pourquoi ? » en simulation (voir HmiWhy.hpp)
// =============================================================================
#include "HmiWhy.hpp"

#include "HmiControls.hpp"
#include "HmiWidgets.hpp"   // 1.14.1 : les fins de course

#include <algorithm>
#include <cctype>

namespace hmi::why {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Un nom commence par une lettre, _ ou un octet d'un caractere accentue.
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool identChar(char c) { return identStart(c) || std::isdigit(static_cast<unsigned char>(c)); }

bool keyword(const std::string& w) {
    static const char* const k[] = {"AND", "OR", "XOR", "NOT", "MOD", "DIV", "TRUE", "FALSE", "NULL", "IN", "ENTRE", "ET",
                                    "AND_THEN", "OR_ELSE", "SHL", "SHR", "IF", "THEN", "ELSE", "ELSIF", "END_IF"};
    for (const char* x : k) if (w == x) return true;
    return false;
}

// La parenthese qui ferme celle de `open` ; npos : aucune.
std::size_t closing(std::string_view s, std::size_t open) {
    int depth = 0;
    char quote = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) { if (c == quote) quote = 0; continue; }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if ((c == ')' || c == ']' || c == '}') && --depth == 0) return i;
    }
    return std::string_view::npos;
}

// "((a AND b))" -> "a AND b".
std::string stripParens(std::string_view s) {
    std::string t = trim(s);
    while (t.size() >= 2 && t.front() == '(' && closing(t, 0) == t.size() - 1) t = trim(std::string_view(t).substr(1, t.size() - 2));
    return t;
}

// Pour chaque caractere hors des guillemets et au niveau 0 des parentheses : f(i) ; f rend le nombre
// de caracteres a sauter apres i (0 : continuer).
template <class F>
void topLevel(std::string_view s, F&& f) {
    int depth = 0;
    char quote = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) { if (c == quote) quote = 0; continue; }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; continue; }
        if (depth == 0) i += f(i);
    }
}

// Les operandes de tete d'un AND / OR / XOR (&&, ||) ; vide : la formule n'en a pas.
std::vector<std::string> splitLogic(std::string_view s) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    topLevel(s, [&](std::size_t i) -> std::size_t {
        const char c = s[i];
        std::size_t len = 0;
        if (i + 1 < s.size() && ((c == '&' && s[i + 1] == '&') || (c == '|' && s[i + 1] == '|'))) len = 2;
        else if (identStart(c) && (i == 0 || !identChar(s[i - 1]))) {
            std::size_t j = i;
            while (j < s.size() && identChar(s[j])) ++j;
            const auto w = upper(s.substr(i, j - i));
            if (w == "AND" || w == "OR" || w == "XOR" || w == "AND_THEN" || w == "OR_ELSE") len = j - i;
            else return j - i - 1;
        }
        if (len == 0) return 0;
        parts.push_back(trim(s.substr(start, i - start)));
        start = i + len;
        return len - 1;
    });
    if (parts.empty()) return {};
    parts.push_back(trim(s.substr(start)));
    std::erase_if(parts, [](const std::string& p) { return p.empty(); });
    return parts;
}

// La condition d'un « c ? a : b » de tete (pas « ?? ») ; faux : aucun.
bool splitTernary(std::string_view s, std::string& condition) {
    bool found = false;
    topLevel(s, [&](std::size_t i) -> std::size_t {
        if (found || s[i] != '?') return 0;
        if (i + 1 < s.size() && s[i + 1] == '?') return 1;   // ?? : la valeur par defaut
        if (i > 0 && s[i - 1] == '?') return 0;
        condition = trim(s.substr(0, i));
        found = !condition.empty();
        return 0;
    });
    return found;
}

// Les deux cotes d'une comparaison de tete ; faux : aucune.
bool splitCompare(std::string_view s, std::string& left, std::string& right) {
    bool found = false;
    topLevel(s, [&](std::size_t i) -> std::size_t {
        if (found) return 0;
        const char c = s[i];
        const char n = i + 1 < s.size() ? s[i + 1] : '\0';
        const char p = i > 0 ? s[i - 1] : '\0';
        std::size_t len = 0;
        if ((c == '<' && (n == '>' || n == '=')) || (c == '>' && n == '=') || (c == '=' && n == '=') || (c == '!' && n == '=')) len = 2;
        else if (c == '=' && p != ':' && p != '<' && p != '>' && p != '!' && p != '=' && n != '>') len = 1;
        else if ((c == '<' || c == '>') && p != '<' && p != '>' && n != c && p != '-' && n != '-') len = 1;
        if (len == 0) return 0;
        left = trim(s.substr(0, i));
        right = trim(s.substr(i + len));
        found = !left.empty() && !right.empty();
        return len - 1;
    });
    return found;
}

void addUnique(std::vector<std::string>& out, std::string t) {
    if (t.empty()) return;
    if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(std::move(t));
}

Term evaluateTerm(const std::string& text, sim::Environment& env, const Scope* scope) {
    Term t;
    t.text = text;
    const auto e = Expression::compile(text);
    if (!e.valid()) {
        t.value = e.error().empty() ? std::string("illisible") : e.error();
        t.error = true;
        return t;
    }
    if (auto v = e.evaluate(env, scope)) t.value = formatValue(*v);
    else {
        t.value = v.error().message();
        t.error = true;
    }
    return t;
}

// Ce que l'etat de l'objet doit a la valeur (voyant, etats, zones, visibilite).
std::string stateOf(const Object& o, const Item& it, const std::vector<Term>& conditions) {
    if (it.error) return {};
    if (it.key == "visible") return valueOn(it.value) ? std::string("visible") : std::string("cach\xC3\xA9 (la formule vaut FALSE)");
    if (it.key != "value") return {};
    switch (o.kind) {
        case Kind::Indicator:
            return valueOn(it.value) ? "allum\xC3\xA9 : " + o.text("colorOn", "#2ECC71") : "\xC3\xA9teint : " + o.text("colorOff", "#4A5261");
        case Kind::MultiStateIndicator:
        case Kind::MultiStateText: {
            const auto list = parseStateList(o.text("stateList"));
            if (list.empty()) return {};
            std::string s;
            if (!conditions.empty()) {
                std::size_t k = 0;
                while (k < conditions.size() && !(valueOn(conditions[k].value) && !conditions[k].error)) ++k;
                s = k < conditions.size() ? "condition " + std::to_string(k + 1) + " vraie (" + conditions[k].text + ") ; "
                                          : std::string("aucune condition vraie : l'\xC3\xA9tat \xC2\xAB sinon \xC2\xBB ; ");
            }
            const int i = stateIndexOf(list, it.value);
            if (i < 0) return s + "aucun \xC3\xA9tat pour " + it.value + " (" + std::to_string(list.size()) + " \xC3\xA9tats)";
            const auto& e = list[static_cast<std::size_t>(i)];
            return s + "\xC3\xA9tat " + std::to_string(i + 1) + " sur " + std::to_string(list.size()) + " : " + e.match + " = " + e.text
                 + (e.color.empty() ? std::string{} : " | " + e.color) + (e.blink ? std::string(" | clignote") : std::string{});
        }
        case Kind::Bargraph:
        case Kind::Dial:
        case Kind::Thermometer:
        case Kind::Gauge:
        case Kind::ProgressBar: {
            double v = 0;
            if (!parseNumber(it.value, v) || o.text("zones").empty()) return {};
            const auto zones = parseZones(o.text("zones"), nullptr, o.number("min", 0), o.number("max", 100));
            if (zones.empty()) return {};
            if (const auto* z = zoneOf(zones, v)) return "zone " + formatNumber(z->from) + ".." + formatNumber(z->to) + " : " + z->color;
            return "hors des zones (" + std::to_string(zones.size()) + ")";
        }
        default:
            return {};
    }
}

} // namespace

std::vector<std::string> namesOf(std::string_view s) {
    std::vector<std::string> out;
    char quote = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (quote) { if (c == quote) quote = 0; ++i; continue; }
        if (c == '\'' || c == '"') { quote = c; ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(c))) {     // 12, 1.5e3, 16#FF
            while (i < s.size() && (identChar(s[i]) || s[i] == '.' || s[i] == '#')) ++i;
            continue;
        }
        if (!identStart(c) || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '.'))) { ++i; continue; }
        std::size_t j = i;
        while (j < s.size() && identChar(s[j])) ++j;
        if (j < s.size() && s[j] == '#') {                      // T#2s, INT#5, Mode#Auto : un litteral
            ++j;
            while (j < s.size() && (identChar(s[j]) || s[j] == '.' || s[j] == ':' || s[j] == '-')) ++j;
            i = j;
            continue;
        }
        const auto after = [&](std::size_t k) {
            while (k < s.size() && s[k] == ' ') ++k;
            return k < s.size() ? s[k] : '\0';
        };
        if (after(j) == '(' || keyword(upper(s.substr(i, j - i)))) { i = j; continue; }   // une fonction, un mot cle
        std::vector<std::string> inner;
        for (;;) {
            if (j + 1 < s.size() && s[j] == '.' && identStart(s[j + 1])) {
                ++j;
                while (j < s.size() && identChar(s[j])) ++j;
                continue;
            }
            if (j < s.size() && s[j] == '[') {
                const auto close = closing(s, j);
                if (close == std::string_view::npos) break;
                inner.emplace_back(s.substr(j + 1, close - j - 1));
                j = close + 1;
                continue;
            }
            break;
        }
        std::string path(s.substr(i, j - i));
        if (after(j) == '(') {                                    // Liste.LENGTH() : la liste
            const auto dot = path.rfind('.');
            path = dot == std::string::npos ? std::string{} : path.substr(0, dot);
        }
        addUnique(out, std::move(path));
        for (const auto& in : inner)
            for (auto& n : namesOf(in)) addUnique(out, std::move(n));
        i = j;
    }
    return out;
}

std::vector<std::string> holesOf(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{') continue;
        if (i + 1 < text.size() && text[i + 1] == '{') { ++i; continue; }
        const auto close = text.find('}', i + 1);
        if (close == std::string_view::npos) break;
        std::string inside = trim(text.substr(i + 1, close - i - 1));
        // Le format apres le dernier ':' (pas celui d'un := ) : 0.0, 000, +0.00, %, hex...
        if (const auto colon = inside.rfind(':'); colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=') {
            const std::string fmt = inside.substr(colon + 1);
            const bool looksFormat = fmt.size() <= 12 && std::all_of(fmt.begin(), fmt.end(), [](char ch) {
                return std::isdigit(static_cast<unsigned char>(ch)) || std::isalpha(static_cast<unsigned char>(ch)) || ch == '.' || ch == '#'
                    || ch == '+' || ch == '%' || ch == ',';
            });
            if (looksFormat) inside = trim(std::string_view(inside).substr(0, colon));
        }
        addUnique(out, std::move(inside));
        i = close;
    }
    return out;
}

std::vector<std::string> termsOf(std::string_view formula) {
    std::string f = trim(formula);
    if (!f.empty() && f.front() == '=') f = trim(std::string_view(f).substr(1));
    const std::string body = stripParens(f);
    std::vector<std::string> out;
    const auto add = [&](std::string t) {
        t = stripParens(t);
        if (t.empty() || t == body || out.size() >= 12) return;
        addUnique(out, std::move(t));
    };
    std::vector<std::string> conditions;
    if (parseConditionChain(body, conditions) && !conditions.empty()) {
        for (auto& c : conditions) add(c);
    } else if (auto parts = splitLogic(body); parts.size() > 1) {
        for (auto& p : parts) add(p);
    } else {
        std::string condition, left, right;
        std::string inner = body;
        const std::string head = upper(std::string_view(inner).substr(0, std::min<std::size_t>(4, inner.size())));
        if (head == "NOT " || head == "NOT(") inner = stripParens(std::string_view(inner).substr(3));
        else if (!inner.empty() && inner.front() == '!' && (inner.size() < 2 || inner[1] != '=')) inner = stripParens(std::string_view(inner).substr(1));
        if (splitTernary(inner, condition)) add(condition);
        else if (splitCompare(inner, left, right)) {
            add(left);
            add(right);
        } else if (inner != body) {
            add(inner);
        }
    }
    for (auto& n : namesOf(body)) add(n);
    return out;
}

Report explain(const Object& shown, const std::vector<LiveValue>& values, sim::Environment& env, const Scope* scope) {
    Report r;
    r.object = shown.id;
    r.name = shown.name;
    r.kind = std::string(kindLabel(shown.kind));
    for (const auto& lv : values) {
        if (lv.object != shown.id) continue;
        Item it;
        it.key = lv.key;
        it.formula = lv.expression;
        it.text = lv.text;
        it.value = lv.value;
        it.error = lv.error;
        std::vector<std::string> conditionTexts;
        if (!lv.text) (void)parseConditionChain(stripParens(lv.expression), conditionTexts);
        std::vector<Term> conditions;
        const auto texts = lv.text ? holesOf(lv.expression) : termsOf(lv.expression);
        for (const auto& t : texts) {
            auto term = evaluateTerm(t, env, scope);
            const auto inside = namesOf(t);
            term.name = inside.size() == 1 && inside.front() == t;   // un nom seul : il se force
            if (std::find(conditionTexts.begin(), conditionTexts.end(), t) != conditionTexts.end()) conditions.push_back(term);
            it.terms.push_back(std::move(term));
        }
        it.state = stateOf(shown, it, conditions);
        r.items.push_back(std::move(it));
    }
    // 1.14.1 : COMMANDE PAR LES FINS DE COURSE - l'etat retenu et ce qui le decide (quelle fin de course, l'Ordre,
    // le temps de manoeuvre), en tete : la couleur de l'objet vient de la.
    if (limits::kindHasLimits(shown.kind) && limits::limitMode(shown)) {
        Item it;
        it.key = shown.kind == Kind::ThreeWayValve ? "positionMode" : std::string(limits::kModeKey);
        it.formula = std::string(limits::kLimitsMode);
        it.text = true;
        const bool feminine = shown.kind == Kind::Valve || shown.kind == Kind::ThreeWayValve;
        if (shown.kind == Kind::ThreeWayValve) {
            const auto r3 = limits::read3(shown);
            it.value = r3.state == limits::State::Open ? "voie " + std::string(limits::wayLabel(r3.way)) : limits::stateLabel(r3.state, true);
            it.state = r3.why;
        } else {
            const auto r2 = limits::read(shown);
            it.value = limits::stateLabel(r2.state, feminine);
            it.state = r2.why;
        }
        r.items.insert(r.items.begin(), std::move(it));
    }
    return r;
}

std::string toText(const Report& r) {
    std::string out = r.name + " (" + r.kind + ")\n";
    if (r.items.empty()) return out + "  rien ne se calcule : ses propri\xC3\xA9t\xC3\xA9s sont fixes\n";
    for (const auto& it : r.items) {
        out += it.key + " = " + it.value + (it.error ? " (erreur)" : "") + "    " + (it.text ? "" : "=") + it.formula + "\n";
        if (!it.state.empty()) out += "  -> " + it.state + "\n";
        for (const auto& t : it.terms) out += "    " + t.text + " = " + t.value + (t.error ? " (erreur)" : "") + "\n";
    }
    return out;
}

} // namespace hmi::why
