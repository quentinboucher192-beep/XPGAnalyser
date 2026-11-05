// =============================================================================
//  hmi/HmiSemanticColors.cpp - 1.23.1 : les couleurs semantiques (voir HmiSemanticColors.hpp)
// =============================================================================
#include "HmiSemanticColors.hpp"

#include "core/SemanticColors.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace hmi::semantic {

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}
// Le nom sans le @ ni les blancs du bord.
std::string bare(std::string_view name) {
    std::string n = trim(name);
    if (!n.empty() && n.front() == '@') n = trim(std::string_view(n).substr(1));
    return n;
}
bool sameName(std::string_view a, std::string_view b) { return upper(bare(a)) == upper(bare(b)); }

bool hexOk(std::string_view text) {
    const std::string t = trim(text);
    std::string_view h = t;
    if (!h.empty() && h.front() == '#') h.remove_prefix(1);
    else if (h.size() > 3 && h.substr(0, 3) == "16#") h.remove_prefix(3);
    else return false;
    if (h.size() != 6 && h.size() != 8) return false;
    return std::all_of(h.begin(), h.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

SemanticColor make(std::string name, std::string value, std::string description) {
    SemanticColor c;
    c.name = std::move(name);
    c.value = std::move(value);
    c.description = std::move(description);
    c.builtin = true;
    return c;
}

// Les fournies : l'ISA-101 (l'IHM haute performance). Le gris dit le normal ; la couleur, l'anomalie.
std::vector<SemanticColor> makeBuiltins() {
    return {
        make("Fond", "#E0E0E0", "Le fond des vues : un gris neutre (ISA-101)."),
        make("Texte", "#1C1C1C", "Le texte : sombre sur le fond gris."),
        make("\xC3\x89quipement", "#4D4D4D", "Le contour et le corps d'un \xC3\xA9quipement."),
        make("Marche", "#4D4D4D",
             "Un \xC3\xA9quipement en marche : plein, gris fonc\xC3\xA9 (ISA-101 : l'\xC3\xA9tat se lit sans couleur, pas de vert)."),
        make("Arr\xC3\xAAt", "#F5F5F5", "Un \xC3\xA9quipement \xC3\xA0 l'arr\xC3\xAAt : vide, presque blanc."),
        make("D\xC3\xA9" "faut", "#D32F2F", "Un \xC3\xA9quipement en d\xC3\xA9" "faut : rouge."),
        make("Alarme critique", "#D32F2F", "Une alarme de priorit\xC3\xA9 1 : rouge."),
        make("Alarme", "#F57C00", "Une alarme de priorit\xC3\xA9 2 : orange."),
        make("Avertissement", "#F2C200", "Une alarme de priorit\xC3\xA9 3, un avertissement : jaune."),
        make("Information", "#3D7CC9", "Une information, une alarme de priorit\xC3\xA9 4 : bleu."),
        make("Accent", "#1F5F99", "Ce qui agit : la s\xC3\xA9lection, le bouton par d\xC3\xA9" "faut."),
        make("D\xC3\xA9sactiv\xC3\xA9", "#A6A6A6", "Ce qui ne r\xC3\xA9pond pas : un bouton interdit, une valeur absente."),
    };
}

// ---- les citations ------------------------------------------------------------------------------------------------------
//  Une valeur cite une couleur par son texte entier (<< @Alarme >>), ou apres un = dans des zones (<< 0-60 = @Marche >>) ;
//  une formule (et le texte des parametres d'un symbole, Couleur := '@Alarme') par un texte constant entre apostrophes ou
//  guillemets.
struct Ref {
    std::size_t at{0};
    std::size_t len{0};
    std::string name;
};
std::vector<Ref> refsIn(std::string_view text, bool formula) {
    std::vector<Ref> out;
    if (text.find('@') == std::string_view::npos) return out;
    if (!formula) {
        std::string name;
        if (core::semantic::isReference(text, &name)) {
            out.push_back({0, text.size(), name});
            return out;
        }
        // Les zones d'un bargraphe, d'un cadran : << 0-60 = @Marche; 60-100 = @Alarme >> - un @ juste apres un =, le nom
        // jusqu'au ; (ou la fin de la ligne).
        for (std::size_t at = text.find('@'); at != std::string_view::npos; at = text.find('@', at + 1)) {
            std::size_t before = at;
            while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) --before;
            if (before == 0 || text[before - 1] != '=') continue;
            std::size_t end = at + 1;
            while (end < text.size() && text[end] != ';' && text[end] != '\n' && text[end] != '\r') ++end;
            while (end > at + 1 && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
            if (end > at + 1) out.push_back({at, end - at, std::string(text.substr(at + 1, end - at - 1))});
        }
        return out;
    }
    std::size_t i = 0;
    while (i < text.size()) {
        const char q = text[i];
        if (q != '\'' && q != '"') {
            ++i;
            continue;
        }
        const std::size_t start = i + 1;
        std::size_t end = start;
        while (end < text.size() && text[end] != q) {
            if (text[end] == '$' && end + 1 < text.size()) ++end;   // l'echappement ST ($', $$)
            ++end;
        }
        if (end >= text.size()) break;
        const std::string_view lit = text.substr(start, end - start);
        std::string name;
        if (core::semantic::isReference(lit, &name)) out.push_back({start, lit.size(), name});
        i = end + 1;
    }
    return out;
}

// Chaque texte du projet qui peut citer une couleur : f(texte, formule ?, ou).
struct Loc {
    Id view{kNoId};
    Id object{kNoId};
    std::string key;
    std::function<std::string()> label;
};
template <class P, class F>
void visitTexts(P& p, F&& f) {
    for (auto& v : p.views) {
        f(v.background, false, Loc{v.id, kNoId, "background", [&v] { return v.name + " : fond"; }});
        for (auto& o : v.objects)
            for (auto& pr : o.props) {
                if (pr.value.find('@') == std::string::npos && pr.expr.find('@') == std::string::npos) continue;
                const Loc at{v.id, o.id, pr.key, [&v, &o, &pr] { return v.name + " : " + o.name + " (" + pr.key + ")"; }};
                if (!pr.value.empty()) f(pr.value, pr.key == "params", at);
                if (!pr.expr.empty()) f(pr.expr, true, at);
            }
    }
    for (auto& s : p.styles) {
        const Loc at{kNoId, kNoId, {}, [&s] { return "style " + s.name; }};
        for (auto& pr : s.props) f(pr.value, false, at);
        for (auto& t : s.targets)
            for (auto& pr : t.props) f(pr.value, false, at);
    }
    for (auto& c : p.semanticColors) f(c.value, false, Loc{kNoId, kNoId, {}, [&c] { return "couleur @" + c.name; }});
}

// Remplacer les citations de `name` dans un texte (de la fin vers le debut) ; rend leur nombre.
std::size_t replaceRefs(std::string& text, bool formula, std::string_view name, const std::string& by) {
    auto refs = refsIn(text, formula);
    std::size_t n = 0;
    for (auto it = refs.rbegin(); it != refs.rend(); ++it) {
        if (!sameName(it->name, name)) continue;
        text.replace(it->at, it->len, by);
        ++n;
    }
    return n;
}

std::string freeName(const Project& p, std::string wanted) {
    wanted = bare(wanted);
    if (wanted.empty()) wanted = "Couleur";
    if (nameFree(p, wanted)) return wanted;
    for (int k = 2; k < 10000; ++k) {
        std::string candidate = wanted + " " + std::to_string(k);
        if (nameFree(p, candidate)) return candidate;
    }
    return wanted + " " + std::to_string(p.nextId);
}

} // namespace

// ================================================================== les fournies ===
const std::vector<SemanticColor>& builtins() {
    static const std::vector<SemanticColor> all = makeBuiltins();
    return all;
}

const SemanticColor* builtin(std::string_view name) noexcept {
    for (const auto& c : builtins())
        if (sameName(c.name, name)) return &c;
    return nullptr;
}

const SemanticColor* find(const Project& p, std::string_view name) noexcept {
    if (bare(name).empty()) return nullptr;
    for (const auto& c : p.semanticColors)
        if (sameName(c.name, name)) return &c;
    return builtin(name);
}

std::vector<const SemanticColor*> all(const Project& p) {
    std::vector<const SemanticColor*> out;
    for (const auto& c : p.semanticColors) out.push_back(&c);
    for (const auto& c : builtins()) {
        const bool hidden = std::any_of(p.semanticColors.begin(), p.semanticColors.end(),
                                        [&](const SemanticColor& m) { return sameName(m.name, c.name); });
        if (!hidden) out.push_back(&c);
    }
    return out;
}

void publish(const Project& p) {
    std::vector<std::pair<std::string, std::string>> table;
    for (const auto* c : all(p)) table.emplace_back(c->name, c->value);
    core::semantic::publish(std::move(table));
}

// ================================================================== les citations ===
std::string reference(std::string_view name) { return "@" + bare(name); }

bool isReference(std::string_view text, std::string* name) { return core::semantic::isReference(text, name); }

std::string resolve(const Project& p, std::string_view text) {
    std::string out(text);
    std::string name;
    for (int hop = 0; hop < 8 && isReference(out, &name); ++hop) {
        const SemanticColor* c = find(p, name);
        if (!c) break;
        out = c->value;
    }
    return out;
}

std::size_t users(const Project& p, std::string_view name) {
    std::size_t n = 0;
    visitTexts(p, [&](const std::string& text, bool formula, const Loc&) {
        for (const auto& r : refsIn(text, formula))
            if (sameName(r.name, name)) ++n;
    });
    return n;
}

std::size_t renameReferences(Project& p, std::string_view from, std::string_view to) {
    if (sameName(from, to) && bare(from) == bare(to)) return 0;
    const std::string by = reference(to);
    std::size_t n = 0;
    visitTexts(p, [&](std::string& text, bool formula, const Loc&) { n += replaceRefs(text, formula, from, by); });
    return n;
}

std::size_t forget(Project& p, std::string_view name) {
    const SemanticColor* c = find(p, name);
    if (!c || c->builtin) return 0;
    std::string hex = resolve(p, reference(name));
    if (isReference(hex)) hex = "#000000";   // une boucle, une couleur inconnue : du noir, plutot qu'une citation morte
    const Id self = c->id;
    std::size_t n = 0;
    for (auto& v : p.views) {
        n += replaceRefs(v.background, false, name, hex);
        for (auto& o : v.objects)
            for (auto& pr : o.props) {
                n += replaceRefs(pr.value, pr.key == "params", name, hex);
                n += replaceRefs(pr.expr, true, name, hex);
            }
    }
    for (auto& s : p.styles) {
        for (auto& pr : s.props) n += replaceRefs(pr.value, false, name, hex);
        for (auto& t : s.targets)
            for (auto& pr : t.props) n += replaceRefs(pr.value, false, name, hex);
    }
    for (auto& other : p.semanticColors)
        if (other.id != self) n += replaceRefs(other.value, false, name, hex);
    return n;
}

// ================================================================== en faire une ===
bool nameFree(const Project& p, std::string_view name, Id except) {
    const std::string n = bare(name);
    if (n.empty() || builtin(n)) return false;
    return std::none_of(p.semanticColors.begin(), p.semanticColors.end(),
                        [&](const SemanticColor& c) { return c.id != except && sameName(c.name, n); });
}

std::string uniqueName(const Project& p, std::string wanted) { return freeName(p, std::move(wanted)); }

SemanticColor duplicate(Project& p, const SemanticColor& from, const std::string& name) {
    SemanticColor c = from;
    c.id = p.allocate();
    c.builtin = false;
    c.folder = from.builtin ? std::string{} : from.folder;
    c.name = freeName(p, name.empty() ? from.name + " (copie)" : name);
    if (from.builtin) c.description = "Copie de \xC2\xAB @" + from.name + " \xC2\xBB (couleur fournie). " + from.description;
    return c;
}

// ======================================================================= verifier ===
std::vector<std::string> problems(const Project& p, const SemanticColor& c) {
    std::vector<std::string> out;
    const std::string n = trim(c.name);
    if (n.empty()) out.push_back("le nom est vide");
    else if (n.find_first_of("@'\",;:=") != std::string::npos)
        out.push_back("le nom ne peut pas contenir @ ' \" , ; : =");
    if (!c.builtin && !n.empty() && builtin(n)) out.push_back("@" + n + " est le nom d'une couleur fournie : choisis-en un autre");
    if (!c.builtin && !n.empty()) {
        const auto same = std::count_if(p.semanticColors.begin(), p.semanticColors.end(),
                                        [&](const SemanticColor& m) { return sameName(m.name, n); });
        if (same > 1) out.push_back("deux couleurs s'appellent @" + n);
    }
    std::string target;
    if (isReference(c.value, &target)) {
        if (sameName(target, n)) out.push_back("@" + n + " se cite elle-m\xC3\xAAme");
        else if (!find(p, target)) out.push_back("cite @" + bare(target) + " : couleur inconnue");
        else if (isReference(resolve(p, c.value))) out.push_back("@" + n + " : les citations tournent en rond");
    } else if (!hexOk(c.value)) {
        out.push_back("couleur illisible : \xC2\xAB " + c.value + " \xC2\xBB (attendu #RRGGBB ou #RRGGBBAA)");
    }
    return out;
}

std::vector<Citation> unknownReferences(const Project& p) {
    std::vector<Citation> out;
    visitTexts(p, [&](const std::string& text, bool formula, const Loc& at) {
        for (const auto& r : refsIn(text, formula))
            if (!find(p, r.name)) out.push_back({at.label(), bare(r.name), at.view, at.object, at.key});
    });
    return out;
}

} // namespace hmi::semantic
