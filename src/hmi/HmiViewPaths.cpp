#include "HmiViewPaths.hpp"

#include "HmiCharts.hpp"      // chartItems
#include "HmiExpr.hpp"        // Scope
#include "HmiLive.hpp"        // autoValueSource
#include "HmiRuntime.hpp"     // parseArguments (1.11.7)
#include "HmiWidgets.hpp"     // parseCells, parseImageStates

#include <algorithm>
#include <cctype>
#include <set>

namespace hmi::viewpaths {

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }
bool identStart(char c) { return std::isalpha(uc(c)) != 0 || c == '_'; }
bool identChar(char c) { return std::isalnum(uc(c)) != 0 || c == '_'; }

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}

// Les mots du ST qui ne sont pas des variables (les scripts de la vue).
bool keyword(std::string_view w) {
    static const std::set<std::string, std::less<>> k = {
        "IF", "THEN", "ELSE", "ELSIF", "END_IF", "FOR", "TO", "BY", "DO", "END_FOR", "WHILE", "END_WHILE", "REPEAT",
        "UNTIL", "END_REPEAT", "CASE", "OF", "END_CASE", "RETURN", "EXIT", "VAR", "END_VAR", "AND", "OR", "XOR", "NOT",
        "MOD", "TRUE", "FALSE",
        "CONTINUE", "TRY", "CATCH", "END_TRY", "ENTRE", "ET"};   // 1.12.1 : le dialecte de l'IHM
    return k.count(upper(w)) > 0;
}

// Un index : "[3]", "[1,2]" (des nombres) ; sinon "[*]" (calcule en marche).
std::string indexOf(std::string_view inside) {
    std::string out;
    for (char c : inside) {
        if (std::isspace(uc(c))) continue;
        if (!std::isdigit(uc(c)) && c != '-' && c != ',') return "[*]";
        out += c;
    }
    return out.empty() ? std::string("[*]") : "[" + out + "]";
}

void add(std::vector<std::string>& out, std::vector<std::string> more) {
    for (auto& m : more) out.push_back(std::move(m));
}

} // namespace

std::vector<std::string> inCode(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {                       // une chaine : sautee ($' : une apostrophe)
            const char q = c;
            ++i;
            while (i < s.size() && s[i] != q) i += s[i] == '$' ? 2 : 1;
            ++i;
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {   // un commentaire (* ... *)
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? s.size() : end + 2;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {    // un commentaire // ...
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? s.size() : end;
            continue;
        }
        if (c == '%' && i + 1 < s.size() && std::isalpha(uc(s[i + 1]))) {   // une adresse : %MW100, %M5, %MW10.3
            const std::size_t start = i++;
            while (i < s.size() && (identChar(s[i]) || (s[i] == '.' && i + 1 < s.size() && std::isdigit(uc(s[i + 1]))))) ++i;
            out.emplace_back(s.substr(start, i - start));
            continue;
        }
        if (std::isdigit(uc(c))) {                          // un nombre, 16#FF, 1.5E3, T#5s
            while (i < s.size() && (identChar(s[i]) || s[i] == '#' || s[i] == '.')) ++i;
            continue;
        }
        if (!identStart(c) || (i > 0 && (identChar(s[i - 1]) || s[i - 1] == '#' || s[i - 1] == '.'))) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < s.size() && identChar(s[i])) ++i;
        // Un litteral d'enumeration (T_MODE#Auto) : pas une variable.
        if (i < s.size() && s[i] == '#') {
            while (i < s.size() && (identChar(s[i]) || s[i] == '#')) ++i;
            continue;
        }
        std::string path(s.substr(start, i - start));
        const bool word = keyword(path);
        for (;;) {
            if (i + 1 < s.size() && s[i] == '.' && identStart(s[i + 1])) {
                std::size_t j = i + 1;
                while (j < s.size() && identChar(s[j])) ++j;
                path += s.substr(i, j - i);
                i = j;
                continue;
            }
            if (i < s.size() && s[i] == '[') {
                int depth = 0;
                std::size_t j = i;
                for (; j < s.size(); ++j) {
                    if (s[j] == '[') ++depth;
                    else if (s[j] == ']' && --depth == 0) break;
                }
                path += indexOf(s.substr(i + 1, (j < s.size() ? j : s.size()) - i - 1));
                // Les noms de l'index (V[i]) se lisent aussi : on y repasse.
                add(out, inCode(s.substr(i + 1, (j < s.size() ? j : s.size()) - i - 1)));
                i = j < s.size() ? j + 1 : s.size();
                continue;
            }
            break;
        }
        std::size_t k = i;
        while (k < s.size() && std::isspace(uc(s[k]))) ++k;
        const bool call = k < s.size() && s[k] == '(';
        if (!word && !call) out.push_back(std::move(path));
    }
    return out;
}

std::vector<std::string> inTemplate(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto a = text.find('{', i);
        if (a == std::string_view::npos) break;
        if (a + 1 < text.size() && text[a + 1] == '{') { i = a + 2; continue; }   // {{ : une accolade ecrite
        const auto b = text.find('}', a + 1);
        if (b == std::string_view::npos) break;
        std::string_view hole = text.substr(a + 1, b - a - 1);
        // Le format suit le dernier ':' hors d'une chaine ({P:0.0}, {Marche:marche|arret}).
        bool quoted = false;
        std::size_t colon = std::string_view::npos;
        for (std::size_t k = 0; k < hole.size(); ++k) {
            if (hole[k] == '\'') quoted = !quoted;
            else if (hole[k] == ':' && !quoted && (k + 1 >= hole.size() || hole[k + 1] != '=')) colon = k;
        }
        if (colon != std::string_view::npos) hole = hole.substr(0, colon);
        add(out, inCode(hole));
        i = b + 1;
    }
    return out;
}

std::vector<std::string> ofView(const View& v, const Scope* scope) {
    std::vector<std::string> raw;
    const auto actions = [&](const std::vector<Action>& list) {
        for (const auto& a : list) {
            add(raw, inCode(a.watch));
            add(raw, inCode(a.guard));
            if (operationWritesVariable(a.operation) || a.operation == Operation::RequestResource) add(raw, inCode(a.target));
            if (a.operation == Operation::Log) add(raw, inTemplate(a.value));
            else if (operationHasValue(a.operation) || operationTakesArguments(a.operation)) add(raw, inCode(a.value));
            for (const auto& [n, val] : parseArguments(a.params)) add(raw, inCode(val));   // 1.11.7 : Maths, le clavier
        }
    };
    for (const auto& o : v.objects) {
        for (const auto& p : o.props) {
            if (!p.expr.empty()) add(raw, inCode(p.expr));
            else if (p.value.find('{') != std::string::npos && (p.key == "text" || p.key == "title" || p.key == "label"))
                add(raw, inTemplate(p.value));
            else if ((p.key == "variable" || p.key == "feedback" || p.key == "xVariable") && !p.value.empty())
                add(raw, inCode(p.value));
        }
        if (const std::string source = autoValueSource(o); !source.empty()) add(raw, inCode(source));
        if (o.kind == Kind::Table)
            if (const auto* cells = o.find("cells"); cells && !cells->value.empty())
                for (const auto& row : parseCells(cells->value))
                    for (const auto& cell : row) {
                        if (cellIsExpression(cell)) add(raw, inCode(std::string_view(cell).substr(1)));
                        else if (cellIsTemplate(cell)) add(raw, inTemplate(cell));
                    }
        if (o.find("variables"))
            for (const auto& item : chartItems(o, "variables")) add(raw, inCode(item.expression));
        if (o.find("references"))
            for (const auto& item : chartItems(o, "references")) add(raw, inCode(item.expression));
        if (o.kind == Kind::AnimatedImage)
            if (const auto* st = o.find("states"); st && !st->value.empty())
                for (const auto& s : parseImageStates(st->value)) add(raw, inCode(s.condition));
        actions(o.actions);
    }
    actions(v.actions);
    for (const auto& sc : v.scripts) add(raw, inCode(sc.body));
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (auto& p : raw) {
        std::string r = scope ? scope->resolve(p) : p;
        if (seen.insert(upper(r)).second) out.push_back(std::move(r));
    }
    return out;
}

std::vector<std::string> segments(std::string_view path) {
    std::vector<std::string> out;
    std::size_t i = 0;
    std::string cur;
    while (i < path.size()) {
        const char c = path[i];
        if (c == '.') {
            if (!cur.empty()) out.push_back(upper(cur));
            cur.clear();
            ++i;
            continue;
        }
        if (c == '[') {
            if (!cur.empty()) out.push_back(upper(cur));
            cur.clear();
            const auto end = path.find(']', i);
            const auto stop = end == std::string_view::npos ? path.size() : end;
            out.push_back(indexOf(path.substr(i + 1, stop - i - 1)));
            i = stop + 1;
            continue;
        }
        if (!std::isspace(uc(c))) cur += c;
        ++i;
    }
    if (!cur.empty()) out.push_back(upper(cur));
    // API.Armoires[0] : la variable de l'automate, sans son prefixe.
    if (out.size() > 1 && out.front() == "API") out.erase(out.begin());
    return out;
}

void Filter::set(const std::vector<std::string>& paths) {
    byRoot_.clear();
    count_ = 0;
    for (const auto& p : paths) {
        auto s = segments(p);
        if (s.empty()) continue;
        auto& list = byRoot_[s.front()];
        if (std::find(list.begin(), list.end(), s) != list.end()) continue;
        list.push_back(std::move(s));
        ++count_;
    }
}

bool Filter::covers(std::string_view path) const {
    const auto s = segments(path);
    if (s.empty()) return false;
    const auto it = byRoot_.find(s.front());
    if (it == byRoot_.end()) return false;
    for (const auto& ref : it->second) {
        const std::size_t n = std::min(ref.size(), s.size());
        bool same = true;
        for (std::size_t k = 1; k < n && same; ++k) {
            const auto& a = ref[k];
            const auto& b = s[k];
            same = a == b || (a == "[*]" && !b.empty() && b.front() == '[') || (b == "[*]" && !a.empty() && a.front() == '[');
        }
        if (same) return true;
    }
    return false;
}

} // namespace hmi::viewpaths
