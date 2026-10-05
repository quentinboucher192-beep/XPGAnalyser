// 1.10.2 (chantier D) : Dupliquer avec des reperes - le moteur (voir HmiDuplicate.hpp).
#include "HmiDuplicate.hpp"

#include "HmiEdit.hpp"
#include "HmiExpr.hpp"      // 1.11 (REP) : scanRoots (l'ancien $Vanne$ qui n'est pas une variable)
#include "HmiMarkers.hpp"   // 1.11 (REP) : hmi::markers, le lecteur de reperes
#include "HmiSymbols.hpp"   // 1.11.2 (SYM) : isSymbolView - les parametres d'un symbole sont des chemins
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace hmi::dup {

namespace {

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool geometric(const std::string& key) { return key == "x" || key == "y" || key == "w" || key == "h" || key == "rot"; }

// Le radical d'un nom (comme Dupliquer tel quel) : "V_101" -> "V", "Rect3" -> "Rect".
std::string stemOf(const std::string& name) {
    std::size_t end = name.size();
    while (end > 0 && digit(name[end - 1])) --end;
    if (end < name.size() && end > 0 && name[end - 1] == '_') return name.substr(0, end - 1);
    return end == 0 ? name : name.substr(0, end);
}

bool intersects(const Box& a, const Box& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace

// ================================================================ reperes ===
// 1.11 (REP) : le lecteur est hmi::markers (HmiMarkers), le meme que l'analyse.
bool validMarkerName(std::string_view n) noexcept { return markers::validContent(n); }

std::vector<MarkerUse> markersIn(std::string_view t, bool expression) {
    std::vector<MarkerUse> out;
    for (const auto& s : markers::find(t, expression ? markers::Mode::Expression : markers::Mode::Text))
        out.push_back({std::string(s.content(t)), s.at, s.length});
    return out;
}

bool hasMarker(std::string_view text, bool expression) {
    return text.find('$') != std::string_view::npos && !markersIn(text, expression).empty();
}

std::string markerKey(std::string_view name) { return upper(name); }

std::string replaceMarkers(std::string_view text, const std::map<std::string, std::string>& values, std::size_t* count,
                           std::map<std::string, std::size_t>* perMarker, bool expression) {
    std::size_t n = 0;
    std::string out;
    std::size_t from = 0;
    for (const auto& m : markersIn(text, expression)) {
        const auto it = values.find(markerKey(m.name));
        if (it == values.end() || it->second.empty() || it->second == m.name) continue;
        out.append(text.substr(from, m.at - from));
        // 1.11 : la copie garde ses `$` (elle se duplique a son tour) ; Dupliquer n'en retire jamais.
        // R111 (REP-2) : une valeur qui ne peut pas etre un repere (10, A, $V[2]$ tape avec ses $)
        // s'ecrit telle quelle - `$10$` ne se calculerait pas.
        const bool stays = markers::validContent(it->second)
                        && !markers::find("$" + it->second + "$", expression ? markers::Mode::Expression : markers::Mode::Text).empty();
        out.append(stays ? "$" + it->second + "$" : it->second);
        from = m.at + m.length;
        ++n;
        if (perMarker) ++(*perMarker)[m.name];
    }
    out.append(text.substr(from));
    if (count) *count = n;
    return out;
}

// ================================================================ indices ===
std::vector<IndexUse> indicesIn(std::string_view t, bool expression) {
    std::vector<IndexUse> out;
    // 1.11 : un indice dans un repere ($V[0]$, $V[1].Ouv$) varie avec son repere, pas seul.
    const auto spans = markers::find(t, expression ? markers::Mode::Expression : markers::Mode::Text);
    std::size_t sp = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '[') continue;
        while (sp < spans.size() && spans[sp].end() <= i) ++sp;
        if (sp < spans.size() && spans[sp].at < i) continue;
        std::size_t s = i;
        while (s > 0 && (identChar(t[s - 1]) || t[s - 1] == '.')) --s;
        const std::string_view path = t.substr(s, i - s);
        // `.B[2]` (apres un autre indice), `$Tab$[0]`, `12[3]`, `A.[0]` : non.
        if (path.empty() || !identStart(path[0]) || path.back() == '.') continue;
        if (s > 0 && t[s - 1] == '$') continue;
        std::size_t j = i + 1;
        while (j < t.size() && t[j] == ' ') ++j;
        const std::size_t a = j;
        if (j < t.size() && t[j] == '-') ++j;
        const std::size_t d = j;
        while (j < t.size() && digit(t[j])) ++j;
        if (j == d || j - d > 15) continue;
        const std::size_t e = j;
        while (j < t.size() && t[j] == ' ') ++j;
        if (j >= t.size() || t[j] != ']') continue;   // `Tab[k]`, `M[1, 2]`, `ARRAY[0..7]` : non
        out.push_back({std::string(path), std::stoll(std::string(t.substr(a, e - a))), a, e - a});
    }
    return out;
}

std::string indexKey(std::string_view path, long long value) { return upper(path) + "[" + std::to_string(value) + "]"; }

std::string followIndices(std::string_view text, const std::map<std::string, long long>& values, std::size_t* count) {
    std::size_t n = 0;
    std::string out;
    std::size_t from = 0;
    if (!values.empty())
        for (const auto& u : indicesIn(text)) {
            const auto it = values.find(indexKey(u.path, u.value));
            if (it == values.end()) continue;
            out.append(text.substr(from, u.at - from));
            out.append(std::to_string(it->second));
            from = u.at + u.length;
            ++n;
        }
    out.append(text.substr(from));
    if (count) *count = n;
    return out;
}

std::string shiftIndices(std::string_view piece, long long by) {
    std::map<std::string, long long> to;
    for (const auto& u : indicesIn(piece)) to[indexKey(u.path, u.value)] = u.value + by;
    return followIndices(piece, to);
}

bool hasIndices(std::string_view piece) { return !indicesIn(piece).empty(); }

BoundsFn projectBounds(const Project& p) {
    std::map<std::string, Bounds> known;
    for (const auto& v : p.programs.variables) {
        types::Spec s;
        if (types::parseSpec(v.type, s) && s.dims == 1) known[upper(v.name)] = {s.low[0], s.high[0]};
    }
    return [known](std::string_view path) -> std::optional<Bounds> {
        const auto it = known.find(upper(path));
        if (it == known.end()) return std::nullopt;
        return it->second;
    };
}

std::vector<std::pair<std::string, Bounds>> projectArrays(const Project& p) {
    std::vector<std::pair<std::string, Bounds>> out;
    for (const auto& v : p.programs.variables) {
        types::Spec s;
        if (types::parseSpec(v.type, s) && s.dims == 1) out.push_back({v.name, {s.low[0], s.high[0]}});
    }
    return out;
}

// ---- 1.11.2 (SYM, decision 240) : les parametres d'un symbole sont des chemins -------
namespace {
// Le type auquel mene `path` parmi les parametres `params` d'un symbole : "Value" -> son
// type ("ARRAY[0..9] OF UINT") ; "Value[3]" -> "UINT" (3 dans les bornes) ; "Armoire.ana"
// -> le type du membre, si `members` le donne. nullopt : pas un parametre, un indice
// hors des bornes, un membre inconnu, ou un parametre sans type (ANY) suivi de quelque
// chose (rien de plus n'est su). Le type rendu peut etre vide : un parametre ANY, seul.
std::optional<std::string> paramPathType(const std::vector<ViewParam>& params, std::string_view path, const TypeMembers& members) {
    while (!path.empty() && path.front() == ' ') path.remove_prefix(1);
    while (!path.empty() && path.back() == ' ') path.remove_suffix(1);
    if (path.empty() || !identStart(path.front())) return std::nullopt;
    std::size_t i = 0;
    while (i < path.size() && identChar(path[i])) ++i;
    const std::string root = upper(path.substr(0, i));
    const ViewParam* prm = nullptr;
    for (const auto& p : params)
        if (upper(p.name) == root) { prm = &p; break; }
    if (!prm) return std::nullopt;
    std::string type = prm->type;
    while (i < path.size()) {
        if (path[i] == '[') {
            // Un indice litteral, dans les bornes d'un tableau a une dimension.
            types::Spec s;
            if (type.empty() || !types::parseSpec(type, s) || s.dims != 1) return std::nullopt;
            std::size_t j = i + 1;
            while (j < path.size() && path[j] == ' ') ++j;
            const std::size_t a = j;
            if (j < path.size() && path[j] == '-') ++j;
            const std::size_t d = j;
            while (j < path.size() && digit(path[j])) ++j;
            if (j == d || j - d > 15) return std::nullopt;
            const long long value = std::stoll(std::string(path.substr(a, j - a)));
            while (j < path.size() && path[j] == ' ') ++j;
            if (j >= path.size() || path[j] != ']') return std::nullopt;
            if (value < s.low[0] || value > s.high[0]) return std::nullopt;
            type = s.element;
            i = j + 1;
            continue;
        }
        if (path[i] == '.') {
            // Un membre : seulement si le type est une structure que `members` connait.
            std::size_t j = i + 1;
            if (j >= path.size() || !identStart(path[j])) return std::nullopt;
            std::size_t k = j;
            while (k < path.size() && identChar(path[k])) ++k;
            const std::string member = upper(path.substr(j, k - j));
            types::Spec s;
            if (type.empty() || !members || (types::parseSpec(type, s) && s.array())) return std::nullopt;
            std::string found;
            bool has = false;
            for (const auto& [name, mtype] : members(type))
                if (upper(name) == member) { found = mtype; has = true; break; }
            if (!has) return std::nullopt;
            type = found;
            i = k;
            continue;
        }
        return std::nullopt;
    }
    return type;
}
} // namespace

PathExists symbolParamExists(const View& symbol, PathExists inner, TypeMembers members) {
    if (!isSymbolView(symbol) || symbol.params.empty()) return inner;
    // Une copie : le dialogue est modal, mais rien ne depend de la vue ensuite.
    auto params = std::make_shared<const std::vector<ViewParam>>(symbol.params);
    return [params, inner = std::move(inner), members = std::move(members)](std::string_view path) {
        if (paramPathType(*params, path, members)) return true;
        return inner ? inner(path) : false;
    };
}

BoundsFn symbolParamBounds(const View& symbol, BoundsFn inner, TypeMembers members) {
    if (!isSymbolView(symbol) || symbol.params.empty()) return inner;
    auto params = std::make_shared<const std::vector<ViewParam>>(symbol.params);
    return [params, inner = std::move(inner), members = std::move(members)](std::string_view path) -> std::optional<Bounds> {
        if (const auto type = paramPathType(*params, path, members)) {
            types::Spec s;
            if (!type->empty() && types::parseSpec(*type, s) && s.dims == 1) return Bounds{s.low[0], s.high[0]};
            // Un parametre qui n'est pas un tableau a une dimension : des bornes inconnues (comme
            // le nom nu d'un tableau de l'automate) - pas celles d'une variable du meme nom.
            return std::nullopt;
        }
        return inner ? inner(path) : std::nullopt;
    };
}

std::vector<std::pair<std::string, Bounds>> symbolParamArrays(const View& symbol) {
    std::vector<std::pair<std::string, Bounds>> out;
    if (!isSymbolView(symbol)) return out;
    for (const auto& p : symbol.params) {
        types::Spec s;
        if (types::parseSpec(p.type, s) && s.dims == 1) out.push_back({p.name, {s.low[0], s.high[0]}});
    }
    return out;
}

std::vector<std::string> symbolParamNames(const View& symbol) {
    std::vector<std::string> out;
    if (!isSymbolView(symbol)) return out;
    for (const auto& p : symbol.params) out.push_back(p.name);
    return out;
}

std::optional<ElementRef> elementRef(std::string_view v) {
    while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
    while (!v.empty() && v.back() == ' ') v.remove_suffix(1);
    const auto open = v.find('[');
    if (open == std::string_view::npos || open == 0) return std::nullopt;
    const std::string_view path = v.substr(0, open);
    if (!identStart(path.front()) || path.back() == '.') return std::nullopt;
    for (char c : path)
        if (!identChar(c) && c != '.') return std::nullopt;
    const auto close = v.find(']', open);
    if (close == std::string_view::npos) return std::nullopt;
    std::string_view idx = v.substr(open + 1, close - open - 1);
    while (!idx.empty() && idx.front() == ' ') idx.remove_prefix(1);
    while (!idx.empty() && idx.back() == ' ') idx.remove_suffix(1);
    const std::size_t d = !idx.empty() && idx.front() == '-' ? 1 : 0;
    if (idx.size() <= d || idx.size() - d > 15) return std::nullopt;
    for (std::size_t k = d; k < idx.size(); ++k)
        if (!digit(idx[k])) return std::nullopt;
    const std::string_view rest = v.substr(close + 1);
    // La suite : des membres (.Ouv, .sorties.V2) ; un second indice ou autre chose : non.
    for (std::size_t k = 0; k < rest.size(); ++k)
        if (!identChar(rest[k]) && rest[k] != '.') return std::nullopt;
    if (!rest.empty() && (rest.front() != '.' || rest.size() < 2)) return std::nullopt;
    ElementRef e;
    e.array = std::string(path);
    e.index = std::string(idx);
    e.value = std::stoll(e.index);
    e.rest = std::string(rest);
    e.leadingZero = idx.size() - d > 1 && idx[d] == '0';
    return e;
}

bool followFits(const IndexInfo& info, int copies, long long step) {
    if (!info.bounds || copies <= 0) return info.bounds.has_value();
    const long long last = info.value + static_cast<long long>(copies) * step;
    const long long lo = std::min(info.value + step, last), hi = std::max(info.value + step, last);
    return lo >= info.bounds->first && hi <= info.bounds->second;
}

// ================================================================ champs ===
namespace {
template <class O, class F>
void fields(O& o, F&& fn) {
    for (auto& prop : o.props) {
        // 1.10.2 (integration, a la fusion avec l'arbre de A) : la VALEUR d'une propriete
        // geometrique (x, y, w, h, rot) est faite par la pose ; son EXPRESSION (une animation :
        // rot = $Vanne$.OUV * 2) se lit et se remplace comme les autres (avant : sautee, la copie
        // gardait $Vanne$ sans que Compiler le dise).
        if (!geometric(prop.key)) fn(prop.value, prop.key, false);
        fn(prop.expr, prop.key + " (expression)", true);
    }
    for (std::size_t i = 0; i < o.actions.size(); ++i) {
        auto& a = o.actions[i];
        const std::string an = "action " + std::to_string(i + 1) + " ";
        const bool writes = operationWritesVariable(a.operation);
        fn(a.target, an + "(cible)", writes);
        // 1.11.1 (REP) : le script d'une action (Executer un script) est du ST : il se lit comme
        // une expression (ses chaines '$N', '5$$' et ses commentaires ne portent pas de repere).
        fn(a.value, an + "(valeur)",
           a.operation == Operation::Set || operationTakesArguments(a.operation) || a.operation == Operation::RunScript);
        fn(a.guard, an + "(condition)", true);
        fn(a.watch, an + "(surveill\xC3\xA9" "e)", true);
    }
    for (auto& al : o.alarmOverrides) {
        const std::string an = "alarme " + al.alarm + " ";
        if (al.condition) fn(*al.condition, an + "(condition)", true);
        if (al.message) fn(*al.message, an + "(message)", false);
        if (al.category) fn(*al.category, an + "(cat\xC3\xA9gorie)", false);
        if (al.group) fn(*al.group, an + "(groupe)", false);
        if (al.instruction) fn(*al.instruction, an + "(consigne)", false);
        if (al.description) fn(*al.description, an + "(description)", false);
    }
}

// Le repere est une variable : suivi de `.`, apres `:=`, ou dans une expression.
bool usedAsVariable(std::string_view text, const MarkerUse& m, bool expression) {
    if (expression) return true;
    const std::size_t end = m.at + m.length;
    if (end < text.size() && text[end] == '.') return true;
    std::size_t b = m.at;
    while (b > 0 && text[b - 1] == ' ') --b;
    return b >= 2 && text.substr(b - 2, 2) == ":=";
}

void collect(const Object& o, std::vector<MarkerInfo>& markers, std::vector<IndexInfo>* indices, const BoundsFn& bounds) {
    fields(o, [&](const std::string& field, const std::string& where, bool expression) {
        if (field.empty()) return;
        Spot spot{o.id, o.name, where, field, expression};
        std::set<std::string> seenHere;
        for (const auto& m : markersIn(field, expression)) {
            const std::string key = markerKey(m.name);
            auto it = std::find_if(markers.begin(), markers.end(), [&](const MarkerInfo& x) { return markerKey(x.name) == key; });
            if (it == markers.end()) {
                markers.push_back({m.name, 0, false, {}, {}});
                it = markers.end() - 1;
            }
            ++it->uses;
            if (usedAsVariable(field, m, expression)) it->variable = true;
            // Le membre employe : `$Vanne$.OUV`, `$Vanne$.sorties.V2`.
            if (std::size_t e = m.at + m.length; e < field.size() && field[e] == '.') {
                std::size_t j = e + 1;
                while (j < field.size() && (identChar(field[j]) || (field[j] == '.' && j + 1 < field.size() && identStart(field[j + 1])))) ++j;
                const std::string member = field.substr(e + 1, j - e - 1);
                if (!member.empty() &&
                    std::none_of(it->members.begin(), it->members.end(), [&](const std::string& x) { return upper(x) == upper(member); }))
                    it->members.push_back(member);
            }
            if (seenHere.insert(key).second) it->spots.push_back(spot);
        }
        if (!indices) return;
        std::set<std::string> idxHere;
        for (const auto& u : indicesIn(field, expression)) {
            const std::string key = indexKey(u.path, u.value);
            auto it = std::find_if(indices->begin(), indices->end(), [&](const IndexInfo& x) { return indexKey(x.path, x.value) == key; });
            if (it == indices->end()) {
                IndexInfo info;
                info.path = u.path;
                info.value = u.value;
                if (bounds) info.bounds = bounds(u.path);
                indices->push_back(std::move(info));
                it = indices->end() - 1;
            }
            ++it->uses;
            if (idxHere.insert(key).second) it->spots.push_back(spot);
        }
    });
}
} // namespace

void forEachField(const Object& o, const std::function<void(const std::string&, const std::string&, bool)>& fn) { fields(o, fn); }
void forEachField(Object& o, const std::function<void(std::string&, const std::string&, bool)>& fn) { fields(o, fn); }

const MarkerInfo* Scan::marker(std::string_view name) const {
    const std::string key = markerKey(name);
    for (const auto& m : markers)
        if (markerKey(m.name) == key) return &m;
    return nullptr;
}

std::size_t Scan::markerUses() const {
    std::size_t n = 0;
    for (const auto& m : markers) n += m.uses;
    return n;
}

Scan scan(const View& v, const std::vector<Id>& selection, const BoundsFn& bounds) {
    Scan s;
    s.units = edit::units(v, selection);
    std::set<Id> inTree;
    for (Id u : s.units) {
        inTree.insert(u);
        for (Id d : v.descendantsOf(u)) inTree.insert(d);
    }
    // Dans l'ordre de dessin : l'ordre ou on les lit dans l'arbre.
    for (const auto& o : v.objects) {
        if (!inTree.count(o.id)) continue;
        s.objects.push_back(o.id);
        collect(o, s.markers, &s.indices, bounds);
    }
    s.box = edit::selectionBounds(v, s.units);
    return s;
}

std::vector<MarkerInfo> markersOf(const View& v, Id object) {
    std::vector<MarkerInfo> out;
    std::set<Id> tree{object};
    for (Id d : v.descendantsOf(object)) tree.insert(d);
    for (const auto& o : v.objects)
        if (tree.count(o.id)) collect(o, out, nullptr, {});
    return out;
}

std::vector<MarkerInfo> ownMarkers(const Object& o) {
    std::vector<MarkerInfo> out;
    collect(o, out, nullptr, {});
    return out;
}

bool isTemplatesFolder(std::string_view folder) noexcept {
    if (folder.size() < kTemplatesFolder.size() || folder.substr(0, kTemplatesFolder.size()) != kTemplatesFolder) return false;
    return folder.size() == kTemplatesFolder.size() || folder[kTemplatesFolder.size()] == '/';
}

std::string markerHint(std::string_view text, std::string_view unknown, bool expression) {
    if (unknown.empty() || text.find('$') == std::string_view::npos) return {};
    const std::string want = upper(unknown);
    for (const auto& m : markersIn(text, expression)) {
        const auto roots = scanRoots(m.name);
        if (!roots.empty() && upper(roots.front()) == want) return " \xE2\x80\x94 " + std::string(kMarkerHint);
    }
    return {};
}

bool isUnreplacedIssueMessage(std::string_view m) noexcept { return m.find(kMarkerHint) != std::string_view::npos; }

std::string fieldLabel(std::string_view where) {
    if (where.rfind("action ", 0) == 0 || where.rfind("alarme ", 0) == 0) {
        const auto paren = where.find(" (");
        return std::string(where.substr(0, paren));
    }
    const std::string_view key = where.substr(0, where.find(' '));
    static const std::pair<const char*, const char*> k[] = {
        {"visible", "Visibilit\xC3\xA9"}, {"fill", "Couleur dynamique"}, {"blink", "Clignotement"}, {"rot", "Rotation"},
        {"x", "D\xC3\xA9placement X"}, {"y", "D\xC3\xA9placement Y"}, {"text", "Texte"}, {"value", "Valeur"},
        {"opacity", "Transparence"}, {"w", "Largeur"}, {"h", "Hauteur"}, {"image", "Image"}, {"textColor", "Couleur du texte"},
        {"stroke", "Contour"}, {"colorOn", "Couleur allum\xC3\xA9"}, {"colorOff", "Couleur \xC3\xA9teint"}, {"label", "Libell\xC3\xA9"},
        {"opening", "Ouverture"}, {"moving", "En mouvement"}, {"fault", "D\xC3\xA9" "faut"}, {"title", "Titre"},
        {"tooltip", "Infobulle"}, {"unit", "Unit\xC3\xA9"}};
    for (const auto& [key2, label] : k)
        if (key == key2) return label;
    return std::string(key);
}

// ========================================================== remplissages ===
std::vector<std::string> pastedList(std::string_view clip) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= clip.size()) {
        std::size_t end = clip.find('\n', at);
        if (end == std::string_view::npos) end = clip.size();
        std::string_view line = clip.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (const auto tab = line.find('\t'); tab != std::string_view::npos) line = line.substr(0, tab);
        while (!line.empty() && (line.front() == ' ')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ')) line.remove_suffix(1);
        out.emplace_back(line);
        if (end == clip.size()) break;
        at = end + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

namespace {
std::string numbered(std::string_view pattern, long long n, std::size_t width) {
    std::string num = std::to_string(n < 0 ? -n : n);
    if (num.size() < width) num.insert(0, width - num.size(), '0');
    if (n < 0) num.insert(0, 1, '-');
    std::string out;
    bool any = false;
    for (std::size_t i = 0; i < pattern.size();) {
        if (pattern.compare(i, 3, "{n}") == 0 || pattern.compare(i, 3, "{N}") == 0) {
            out += num;
            i += 3;
            any = true;
        } else {
            out += pattern[i++];
        }
    }
    if (!any) out += num;
    return out;
}
} // namespace

std::vector<std::string> seriesN(std::string_view pattern, std::string_view from, std::size_t count, long long step) {
    std::vector<std::string> out;
    std::string f(from);
    while (!f.empty() && f.front() == ' ') f.erase(f.begin());
    if (f.empty() || !std::all_of(f.begin() + (f[0] == '-' ? 1 : 0), f.end(), digit) || f.size() > 15) return out;
    const long long start = std::stoll(f);
    const std::size_t width = f.size() - (f[0] == '-' ? 1 : 0);
    if (step == 0) step = 1;
    for (std::size_t i = 0; i < count; ++i) out.push_back(numbered(pattern, start + static_cast<long long>(i) * step, width));
    return out;
}

std::vector<std::string> series(std::string_view pattern, std::string_view from, long long to, long long step) {
    std::string f(from);
    if (f.empty() || step == 0) return {};
    long long start = 0;
    try {
        start = std::stoll(f);
    } catch (...) {
        return {};
    }
    if ((step > 0 && to < start) || (step < 0 && to > start)) return {};
    const auto count = static_cast<std::size_t>((to - start) / step + 1);
    if (count > 10000) return {};
    return seriesN(pattern, from, count, step);
}

std::vector<std::string> arrayElements(std::string_view array, long long from, long long to, std::string_view suffix) {
    std::vector<std::string> out;
    if (to < from || to - from > 10000) return out;
    for (long long i = from; i <= to; ++i) out.push_back(std::string(array) + "[" + std::to_string(i) + "]" + std::string(suffix));
    return out;
}

std::vector<std::string> typeInstances(const Project& p, std::string_view type,
                                       const std::vector<std::pair<std::string, std::string>>& plcVariables) {
    std::vector<std::string> out;
    const std::string want = upper(type);
    if (want.empty()) return out;
    for (const auto& v : p.programs.variables)
        if (upper(v.type) == want) out.push_back(v.name);
    for (const auto& [name, t] : plcVariables)
        if (upper(t) == want) out.push_back(name);
    return out;
}

std::vector<std::string> symbolInstances(const Project& p, std::string_view symbol, bool qualified) {
    std::vector<std::string> out;
    const std::string want = upper(symbol);
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            if (o.kind == Kind::SymbolInstance && upper(o.text("symbol")) == want)
                out.push_back(qualified ? v.name + "." + o.name : o.name);
    return out;
}

namespace {
// Damerau-Levenshtein (transpositions voisines), sans la casse.
std::size_t distance(std::string_view a0, std::string_view b0) {
    const std::string a = upper(a0), b = upper(b0);
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::vector<std::size_t>> d(n + 1, std::vector<std::size_t>(m + 1));
    for (std::size_t i = 0; i <= n; ++i) d[i][0] = i;
    for (std::size_t j = 0; j <= m; ++j) d[0][j] = j;
    for (std::size_t i = 1; i <= n; ++i)
        for (std::size_t j = 1; j <= m; ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[n][m];
}
bool sameLetters(std::string a, std::string b) {
    a = upper(a);
    b = upper(b);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}
} // namespace

std::vector<std::string> didYouMean(std::string_view value, const std::vector<std::string>& candidates, std::size_t max) {
    struct Hit { std::size_t d; bool perm; std::size_t rank; std::string s; };
    std::vector<Hit> hits;
    if (value.empty()) return {};
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (upper(c) == upper(value)) return {};   // il existe : rien a proposer
        const std::size_t d = distance(value, c);
        if (d <= 2) hits.push_back({d, sameLetters(std::string(value), c), i, c});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.perm != b.perm) return a.perm;
        if (a.d != b.d) return a.d < b.d;
        return a.rank < b.rank;
    });
    std::vector<std::string> out;
    for (const auto& h : hits) {
        if (out.size() >= max) break;
        out.push_back(h.s);
    }
    return out;
}

// ================================================================== pose ===
double stepOf(const Box& sel, const Layout& l) { return (l.axis == Axis::Y ? sel.h : sel.w) + l.spacing; }

std::vector<Offset> offsets(const Box& sel, int copies, const Layout& l) {
    std::vector<Offset> out;
    const int cols = std::max(1, l.columns);
    for (int k = 1; k <= copies; ++k) {
        switch (l.axis) {
        case Axis::X: out.push_back({k * (sel.w + l.spacing), 0}); break;
        case Axis::Y: out.push_back({0, k * (sel.h + l.spacing)}); break;
        case Axis::Grid: out.push_back({(k % cols) * (sel.w + l.spacing), (k / cols) * (sel.h + l.spacing)}); break;
        }
    }
    return out;
}

std::vector<int> overflowing(const View& v, const Box& sel, int copies, const Layout& l) {
    std::vector<int> out;
    const auto off = offsets(sel, copies, l);
    for (int k = 0; k < copies; ++k) {
        const double x = sel.x + off[static_cast<std::size_t>(k)].dx, y = sel.y + off[static_cast<std::size_t>(k)].dy;
        if (x < 0 || y < 0 || x + sel.w > v.width + 1e-6 || y + sel.h > v.height + 1e-6) out.push_back(k + 1);
    }
    return out;
}

std::optional<double> fittingSpacing(const View& v, const Box& sel, int copies, const Layout& l) {
    if (copies <= 0) return l.spacing;
    // Le plus grand s entier >= 0 tel que la derniere case tienne.
    auto fits = [&](double s) {
        Layout t = l;
        t.spacing = s;
        return overflowing(v, sel, copies, t).empty();
    };
    if (!fits(0)) return std::nullopt;
    double lo = 0, hi = std::max<double>(v.width, v.height);
    if (fits(hi)) return hi;
    while (hi - lo > 1) {
        const double mid = std::floor((lo + hi) / 2);
        if (fits(mid)) lo = mid;
        else hi = mid;
    }
    return lo;
}

std::vector<int> overlapping(const View& v, const std::vector<Id>& selection, int copies, const Layout& l) {
    std::vector<int> out;
    const auto unitIds = edit::units(v, selection);
    std::set<Id> mine;
    for (Id u : unitIds) {
        mine.insert(u);
        for (Id d : v.descendantsOf(u)) mine.insert(d);
    }
    const Box sel = edit::selectionBounds(v, unitIds);
    const auto off = offsets(sel, copies, l);
    for (int k = 0; k < copies; ++k) {
        const Box b{sel.x + off[static_cast<std::size_t>(k)].dx, sel.y + off[static_cast<std::size_t>(k)].dy, sel.w, sel.h};
        for (const auto& o : v.objects) {
            if (mine.count(o.id) || o.parent != kNoId) continue;   // les objets de premier niveau
            if (intersects(b, edit::rotatedBounds(o))) {
                out.push_back(k + 1);
                break;
            }
        }
    }
    return out;
}

// ================================================================= plan ===
bool followByDefault(const Scan& s) {
    return std::none_of(s.markers.begin(), s.markers.end(), [](const MarkerInfo& m) { return hasIndices(m.name); });
}

Plan defaultPlan(const View& v, const Scan& s, int copies) {
    Plan p;
    p.selection = s.units;
    copies = std::max(0, copies);
    p.rows.resize(static_cast<std::size_t>(copies) + 1);
    // Les noms : la serie de l'original (V_101 -> V_102, V_103...), libres dans la vue.
    if (!s.units.empty())
        if (const Object* o = v.object(s.units.front())) {
            p.rows[0].name = o->name;
            std::set<std::string> taken;
            for (const auto& x : v.objects) taken.insert(upper(x.name));
            std::size_t end = o->name.size();
            while (end > 0 && digit(o->name[end - 1])) --end;
            const std::string stem = o->name.substr(0, end), digits = o->name.substr(end);
            long long n = digits.empty() ? 1 : std::stoll(digits.size() > 15 ? digits.substr(digits.size() - 15) : digits);
            for (int k = 1; k <= copies; ++k) {
                std::string name;
                do {
                    ++n;
                    std::string num = std::to_string(n);
                    if (num.size() < digits.size()) num.insert(0, digits.size() - num.size(), '0');
                    name = (digits.empty() ? o->name + "_" : stem) + num;
                } while (taken.count(upper(name)));
                taken.insert(upper(name));
                p.rows[static_cast<std::size_t>(k)].name = name;
            }
        }
    // 1.11 (REP-4) : les indices hors repere se suivent si aucun repere ne porte
    // d'indice (comme en 1.10.2) ; sinon seul le marque varie. "Suivre V[0]" le change.
    if (followByDefault(s))
        for (const auto& idx : s.indices) {
            if (!followFits(idx, copies)) continue;   // garder
            for (int k = 1; k <= copies; ++k) p.rows[static_cast<std::size_t>(k)].indices[indexKey(idx.path, idx.value)] = idx.value + k;
        }
    // 1.11 (REP) : un repere a indices litteraux se preremplit : le morceau, ses
    // indices decales du numero de la copie ($V[1].Ouv$ : V[2].Ouv, V[3].Ouv...).
    for (const auto& m : s.markers) {
        if (!hasIndices(m.name)) continue;
        const std::string key = markerKey(m.name);
        for (int k = 0; k <= copies; ++k) p.rows[static_cast<std::size_t>(k)].markers[key] = shiftIndices(m.name, k);
    }
    return p;
}

std::optional<Prefill> prefill(const Scan& s, const Plan& plan) {
    if (plan.rows.empty()) return std::nullopt;
    // Le repere de type variable, vide sur toutes les lignes : un seul.
    const MarkerInfo* only = nullptr;
    for (const auto& m : s.markers) {
        if (!m.variable) continue;
        const std::string key = markerKey(m.name);
        const bool empty = std::all_of(plan.rows.begin(), plan.rows.end(), [&](const Row& r) {
            const auto it = r.markers.find(key);
            return it == r.markers.end() || it->second.empty();
        });
        if (!empty) continue;
        if (only) return std::nullopt;       // deux reperes : on ne devine pas lequel
        only = &m;
    }
    if (!only) return std::nullopt;
    // L'indice que l'objet suit ailleurs : un tableau aux bornes connues, suivi
    // (1.11, REP-4 : par defaut des qu'aucun repere ne porte d'indice, le cas du client).
    for (const auto& idx : s.indices) {
        // Suivi par les copies ; sans copie ("Remplacer..." de Compiler), l'indice de l'original.
        const bool followed = plan.rows.size() < 2 || plan.rows[1].indices.count(indexKey(idx.path, idx.value)) > 0;
        if (!idx.bounds || !followed) continue;
        if (idx.path.find('.') != std::string::npos) continue;          // V[0], pas Four1.Vannes[0]
        return Prefill{only->name, idx.path, idx.value};
    }
    return std::nullopt;
}

std::vector<std::string> arrayColumn(std::string_view array, long long from, std::size_t rows) {
    if (rows == 0) return {};
    return arrayElements(array, from, from + static_cast<long long>(rows) - 1);
}

bool Validation::blocked() const noexcept {
    return std::any_of(cells.begin(), cells.end(), [](const CellCheck& c) { return c.blocks; });
}

std::size_t Validation::count(CellState s) const noexcept {
    return static_cast<std::size_t>(std::count_if(cells.begin(), cells.end(), [s](const CellCheck& c) { return c.state == s; }));
}

Validation validate(const View& v, const Scan& s, const Plan& plan, const ValidateOptions& opt) {
    Validation out;
    const auto keep = [&](const std::string& key) {
        return std::any_of(opt.keepColumns.begin(), opt.keepColumns.end(), [&](const std::string& k) { return markerKey(k) == key; });
    };
    std::set<std::string> names;
    for (const auto& o : v.objects) names.insert(upper(o.name));
    // 1.11.1 (REP-5, le `$` de fin oublie) : une expression dont un `$` reste seul a des reperes
    // faux ($V[1].Ouv+$V[2].Ouv$ y fait le repere "V[1].Ouv+", qui a pris le `$` du suivant) : la
    // case de l'original, en rouge, dit la faute et le texte juste ; Dupliquer attend qu'on corrige.
    // (Dans un texte, un `$` seul est un vrai `$` : "10 $".)
    for (const auto& m : s.markers)
        for (const auto& spot : m.spots) {
            if (!spot.expression || markers::lone(spot.text).empty()) continue;
            const std::string advice = markers::pairingAdvice(spot.text);
            out.cells.push_back({0, "$" + m.name + "$", CellState::Unknown,
                                 spot.objectName + " \xC2\xB7 " + fieldLabel(spot.where) + " : " +
                                     (advice.empty() ? std::string("un $ reste seul (un rep\xC3\xA8re s'\xC3\xA9" "crit entre deux $)") : advice),
                                 {}, true});
            break;
        }
    for (std::size_t r = 0; r < plan.rows.size(); ++r) {
        const Row& row = plan.rows[r];
        const int rowNo = static_cast<int>(r);
        if (r == 0 && !plan.replaceOriginal) continue;   // l'original garde ses reperes
        // Le nom (une copie : libre dans la vue et parmi les autres lignes).
        if (r > 0 && !row.name.empty()) {
            const bool taken = names.count(upper(row.name)) != 0 ||
                               std::any_of(plan.rows.begin() + 1, plan.rows.begin() + static_cast<std::ptrdiff_t>(r),
                                           [&](const Row& x) { return upper(x.name) == upper(row.name); });
            if (!edit::validName(row.name))
                out.cells.push_back({rowNo, "Nom", CellState::BadName, row.name + " n'est pas un nom valide", {}, true});
            else if (taken)
                out.cells.push_back({rowNo, "Nom", CellState::BadName, row.name + " existe d\xC3\xA9j\xC3\xA0 dans la vue", {}, true});
        }
        for (const auto& m : s.markers) {
            const std::string key = markerKey(m.name), column = "$" + m.name + "$";
            const auto it = row.markers.find(key);
            const std::string value = it == row.markers.end() ? std::string{} : it->second;
            if (value.empty()) {
                // 1.11.1 (REP-11) : l'original, vide, garde son repere quand il y a des copies - le repere
                // entoure deja la vraie valeur ($Exploitation$) ; sans copie ("Remplacer..." de Compiler),
                // c'est lui qu'on remplit : sa case vide bloque, comme en 1.10.4. L'ancien $Vanne$ qui n'est
                // pas une variable (un nom a remplir, pas une valeur) bloque toujours : l'original se remplit.
                if (r == 0 && plan.copies() > 0) {
                    bool real = !m.variable || !opt.exists || opt.exists(m.name);
                    for (const auto& mem : m.members)
                        if (real && m.variable && opt.exists) real = opt.exists(m.name + "." + mem);
                    if (real) continue;
                }
                const bool k = keep(key);
                out.cells.push_back({rowNo, column, CellState::Empty,
                                     k ? std::string("la copie garde le rep\xC3\xA8re") : std::string("case vide : \xC3\xA0 remplir (ou r\xC3\xA9gler la colonne sur \xC2\xAB garder le rep\xC3\xA8re \xC2\xBB)"),
                                     {}, !k});
                continue;
            }
            if (!m.variable) continue;
            // 1.10.4 : un element de tableau (V[3], V[3].Ouv) - l'indice s'ecrit sans
            // zero devant (V[02] : "veux-tu dire V[2] ?") et tient dans les bornes.
            const auto e = elementRef(value);
            if (e && e->leadingZero) {
                out.cells.push_back({rowNo, column, CellState::Unknown,
                                     value + " : un indice s'\xC3\xA9" "crit sans z\xC3\xA9ro devant", {e->canonical()}, true});
                continue;
            }
            // 1.11 (REP) : chaque indice litteral de la valeur ($V[0].Pos > 10$ : V[64].Pos > 10).
            bool outside = false;
            if (opt.bounds)
                for (const auto& u : indicesIn(value)) {
                    const auto b = opt.bounds(u.path);
                    if (!b || (u.value >= b->first && u.value <= b->second)) continue;
                    out.cells.push_back({rowNo, column, CellState::OutOfBounds,
                                         value + " : hors des bornes de " + u.path + " (" + std::to_string(b->first) + ".." +
                                             std::to_string(b->second) + ")",
                                         {}, true});
                    outside = true;
                    break;
                }
            if (outside || !opt.exists) continue;
            // 1.11 : l'existence se verifie pour un chemin (V104, V[3].Ouv), pas pour un
            // morceau d'expression (V[1].Pos > 10 : Compiler le verifie dans la copie).
            if (!e) {
                bool path = !value.empty() && identStart(value.front());
                for (char c : value) path = path && (identChar(c) || c == '.');
                if (!path) continue;
            }
            // Une variable : la valeur existe, et chaque membre employe.
            std::string missing;
            if (!opt.exists(value)) missing = value;
            else
                for (const auto& mem : m.members)
                    if (!opt.exists(value + "." + mem)) {
                        missing = value + "." + mem;
                        break;
                    }
            if (missing.empty()) continue;
            CellCheck c{rowNo, column, CellState::Unknown, missing + " n'existe pas", {}, true};
            if (missing == value) c.suggestions = didYouMean(value, opt.candidates);
            out.cells.push_back(std::move(c));
        }
        if (r == 0) continue;
        for (const auto& idx : s.indices) {
            const auto it = row.indices.find(indexKey(idx.path, idx.value));
            if (it == row.indices.end() || !idx.bounds) continue;
            if (it->second < idx.bounds->first || it->second > idx.bounds->second)
                out.cells.push_back({rowNo, idx.label(), CellState::OutOfBounds,
                                     idx.path + " n'a que les indices " + std::to_string(idx.bounds->first) + " \xC3\xA0 " +
                                         std::to_string(idx.bounds->second),
                                     {}, true});
        }
    }
    return out;
}

Result apply(Project& p, View& v, const Plan& plan) {
    Result r;
    const auto unitIds = edit::units(v, plan.selection);
    if (unitIds.empty() || plan.rows.empty()) return r;
    const Box sel = edit::selectionBounds(v, unitIds);
    const auto off = offsets(sel, plan.copies(), plan.layout);
    std::set<Id> tree;
    for (Id u : unitIds) {
        tree.insert(u);
        for (Id d : v.descendantsOf(u)) tree.insert(d);
    }
    // Les copies, ligne par ligne, chacune dans l'ordre de dessin d'origine.
    std::vector<Object> made;
    for (int k = 1; k <= plan.copies(); ++k) {
        const Row& row = plan.rows[static_cast<std::size_t>(k)];
        std::map<Id, Id> renamed;
        for (const auto& o : v.objects)
            if (tree.count(o.id)) renamed[o.id] = p.allocate();
        for (const auto& o : v.objects) {
            if (!tree.count(o.id)) continue;
            Object c = o;
            c.id = renamed[o.id];
            c.parent = renamed.count(o.parent) ? renamed[o.parent] : o.parent;
            c.locked = false;
            fields(c, [&](std::string& field, const std::string&, bool expression) {
                if (field.empty()) return;
                std::size_t followed = 0, replacedCount = 0;
                // Les indices d'abord (sur le texte d'origine), puis les reperes :
                // un indice dans la valeur d'un repere n'est pas suivi.
                std::string t = followIndices(field, row.indices, &followed);
                t = replaceMarkers(t, row.markers, &replacedCount, &r.perMarker, expression);
                r.indicesFollowed += followed;
                r.markersReplaced += replacedCount;
                field = std::move(t);
            });
            c.setNumber("x", c.number("x") + off[static_cast<std::size_t>(k - 1)].dx);
            c.setNumber("y", c.number("y") + off[static_cast<std::size_t>(k - 1)].dy);
            const bool root = std::find(unitIds.begin(), unitIds.end(), o.id) != unitIds.end();
            std::string wanted = (root && o.id == unitIds.front() && !row.name.empty()) ? row.name : std::string{};
            if (!wanted.empty() && edit::validName(wanted) && !v.objectByName(wanted) &&
                std::none_of(made.begin(), made.end(), [&](const Object& m) { return upper(m.name) == upper(wanted); }))
                c.name = wanted;
            else {
                // Libre parmi la vue ET les copies deja faites.
                View probe;
                probe.objects = v.objects;
                probe.objects.insert(probe.objects.end(), made.begin(), made.end());
                c.name = uniqueObjectName(probe, stemOf(o.name));
            }
            if (root) r.created.push_back(c.id);
            made.push_back(std::move(c));
        }
    }
    // L'original (ligne 0), si on le remplace aussi.
    if (plan.replaceOriginal) {
        const Row& row0 = plan.rows.front();
        for (auto& o : v.objects) {
            if (!tree.count(o.id)) continue;
            fields(o, [&](std::string& field, const std::string&, bool expression) {
                if (field.empty()) return;
                std::size_t n = 0;
                std::string t = replaceMarkers(field, row0.markers, &n, &r.perMarker, expression);
                if (n == 0) return;
                r.markersReplaced += n;
                r.originalChanged = true;
                field = std::move(t);
            });
            if (o.id == unitIds.front() && !row0.name.empty() && upper(row0.name) != upper(o.name) && edit::validName(row0.name) &&
                !v.objectByName(row0.name))
                o.name = row0.name, r.originalChanged = true;
        }
    }
    r.objects = made.size();
    for (auto& c : made) v.objects.push_back(std::move(c));
    edit::refreshGroupBounds(v);
    return r;
}

std::string summary(const Result& r) {
    const std::size_t n = r.created.size();
    std::string s = std::to_string(n) + (n > 1 ? " objets cr\xC3\xA9\xC3\xA9s" : " objet cr\xC3\xA9\xC3\xA9");
    s += ", " + std::to_string(r.markersReplaced) + (r.markersReplaced > 1 ? " rep\xC3\xA8res remplac\xC3\xA9s" : " rep\xC3\xA8re remplac\xC3\xA9");
    if (!r.perMarker.empty()) {
        // Le plus employe d'abord.
        std::vector<std::pair<std::string, std::size_t>> per(r.perMarker.begin(), r.perMarker.end());
        std::stable_sort(per.begin(), per.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        s += " (";
        for (std::size_t i = 0; i < per.size(); ++i) s += (i ? ", $" : "$") + per[i].first + "$ " + std::to_string(per[i].second);
        s += ")";
    }
    // 1.11 (REP) : un objet a repere ne suit pas ses indices hors repere : rien a dire.
    if (r.indicesFollowed > 0)
        s += ", " + std::to_string(r.indicesFollowed) + (r.indicesFollowed > 1 ? " indices suivis" : " indice suivi");
    return s;
}

} // namespace hmi::dup
