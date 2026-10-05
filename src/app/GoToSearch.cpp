// =============================================================================
//  app/GoToSearch.cpp - lot recherche : l'index d'Aller a... et sa recherche
//  (voir GoToSearch.hpp).
// =============================================================================
#include "GoToSearch.hpp"

#include "../project/MemberTree.hpp"
#include "../ui/TextSearch.hpp"

#include <algorithm>
#include <cctype>

namespace app::gotosearch {

namespace {

using Result = GoToPanel::Result;
namespace mt = project::members;

const char* const kTitles[GCount] = {
    "VARIABLES API", "MEMBRES", "VARIABLES IHM", "TYPES ET BLOCS DFB", "UNIT\xC3\x89S DE PROGRAMME", "SECTIONS",
    "T\xC3\x82" "CHES", "TABLES D'ANIMATION", "VUES ET SYMBOLES", "OBJETS DES VUES", "ALARMES", "RECETTES",
    "UTILISATEURS", "RESSOURCES", "STYLES", "SCRIPTS ET FONCTIONS IHM", "VARIABLES SYST\xC3\x88ME", "\xC3\x89QUIPEMENTS",
    "MACROS", "VERSIONS", "AIDE", "CODE DES SECTIONS", "VOLETS", "ACTIONS"};

const char* const kChips[GCount] = {
    "Variables API", "Membres", "Variables IHM", "Types", "Unit\xC3\xA9s", "Sections", "T\xC3\xA2" "ches", "Tables",
    "Vues", "Objets", "Alarmes", "Recettes", "Utilisateurs", "Ressources", "Styles", "Scripts", "Syst\xC3\xA8me",
    "\xC3\x89quipements", "Macros", "Versions", "Aide", "Code", "Volets", "Actions"};

const char* const kDot = " \xC2\xB7 ";

std::string lowerText(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Une ligne de code montree : sans ses blancs de bord, 160 octets au plus.
std::string shownLine(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    std::string out;
    for (const char c : s) out += c == '\t' ? ' ' : c;
    if (out.size() > 160) {
        std::size_t cut = 160;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out = out.substr(0, cut) + "\xE2\x80\xA6";
    }
    return out;
}

// Les lignes d'un texte qui ont tous les mots (aucun exclu) : combien, et les
// premieres (dans l'ordre du texte). Le plus long mot sert de sonde.
void searchText(const Text& t, const ui::SearchQuery& query, bool collect, std::size_t cap, std::vector<Result>& out, std::size_t& total) {
    const std::string* probe = nullptr;
    for (const auto& term : query.terms())
        if (!probe || term.size() > probe->size()) probe = &term;
    if (!probe || probe->empty() || t.foldedLines.size() != t.lines.size()) return;
    std::size_t at = t.folded.find(*probe);
    while (at != std::string::npos) {
        const auto it = std::upper_bound(t.foldedLines.begin(), t.foldedLines.end(), at);
        const auto line = static_cast<std::size_t>(it - t.foldedLines.begin()) - 1;
        const std::size_t a = t.foldedLines[line];
        const std::size_t b = line + 1 < t.foldedLines.size() ? t.foldedLines[line + 1] - 1 : t.folded.size();
        const std::string_view text(t.folded.data() + a, b - a);
        bool ok = true;
        for (const auto& term : query.terms())
            if (text.find(term) == std::string_view::npos) { ok = false; break; }
        for (const auto& ex : query.excluded())
            if (ok && text.find(ex) != std::string_view::npos) ok = false;
        if (ok) {
            ++total;
            if (collect && out.size() < cap) {
                const std::size_t oa = t.lines[line];
                const std::size_t ob = line + 1 < t.lines.size() ? t.lines[line + 1] - 1 : t.body.size();
                Result r;
                r.group = t.group;
                r.groupTitle = kTitles[t.group];
                r.title = t.name + " : ligne " + std::to_string(line + 1);
                r.subtitle = shownLine(std::string_view(t.body).substr(oa, ob - oa));
                r.key = t.key + ":" + std::to_string(line + 1);
                r.icon = t.icon;
                r.score = 2500;
                out.push_back(std::move(r));
            }
        }
        at = b >= t.folded.size() ? std::string::npos : t.folded.find(*probe, b + 1);
    }
}

// Un paquet (ou une ligne) d'un tableau mene-t-il a `want` (un chemin en
// minuscules, sans blancs) ? L'indice de sa dimension dans sa plage, les
// indices deja fixes les siens.
bool packLeadsTo(const mt::Node& n, const std::string& want) {
    const auto base = lowerText(n.path);
    if (want.size() <= base.size() || want.compare(0, base.size(), base) != 0 || want[base.size()] != '[') return false;
    const auto close = want.find(']', base.size());
    if (close == std::string::npos) return false;
    std::vector<std::int64_t> indices;
    std::size_t from = base.size() + 1;
    while (from <= close) {
        const auto comma = std::min(want.find(',', from), close);
        std::size_t k = from;
        const bool negative = k < comma && want[k] == '-';
        if (negative) ++k;
        std::int64_t v = 0;
        const std::size_t digits = k;
        while (k < comma && want[k] >= '0' && want[k] <= '9' && v < 100000000000LL) v = v * 10 + (want[k++] - '0');
        if (k == digits || k != comma) return false;
        indices.push_back(negative ? -v : v);
        from = comma + 1;
    }
    if (n.dim >= indices.size() || n.fixed.size() > n.dim) return false;
    for (std::size_t d = 0; d < n.fixed.size(); ++d)
        if (indices[d] != n.fixed[d]) return false;
    return indices[n.dim] >= n.first && indices[n.dim] <= n.last;
}

// Descendre de la racine jusqu'au noeud `want` (en minuscules, sans blancs) ;
// faux : pas de tel membre.
bool walkTo(const domain::Project& p, mt::Node node, const std::string& want, mt::Node& out) {
    for (int level = 0; level < 128; ++level) {
        if (node.real && lowerText(node.path) == want) {
            out = std::move(node);
            return true;
        }
        const auto kids = mt::children(p, node);
        const mt::Node* next = nullptr;
        for (const auto& k : kids) {
            if (k.real) {
                const auto kp = lowerText(k.path);
                if (want == kp || (want.size() > kp.size() && want.compare(0, kp.size(), kp) == 0 && (want[kp.size()] == '.' || want[kp.size()] == '['))) {
                    next = &k;
                    break;
                }
            } else if (packLeadsTo(k, want)) {
                next = &k;
                break;
            }
        }
        if (!next) return false;
        node = *next;
    }
    return false;
}

// LES MEMBRES, par le chemin tape : "armoires[0].sorties.V" - ceux de
// armoires[0].sorties dont le nom contient "v".
void searchMembers(const Index& ix, const std::string& text, bool collect, std::vector<Result>& out, std::size_t& total) {
    if (!ix.plc) return;
    std::string path;
    for (const char c : text)
        if (c != ' ') path += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto rootEnd = path.find_first_of(".[");
    if (rootEnd == std::string::npos || rootEnd == 0) return;
    const auto rootName = path.substr(0, rootEnd);
    const auto g = std::lower_bound(ix.globals.begin(), ix.globals.end(), rootName,
                                    [](const auto& e, const std::string& k) { return e.first < k; });
    if (g == ix.globals.end() || g->first != rootName) return;
    const auto cut = path.find_last_of(".[");
    const std::string base = path.substr(0, cut);
    std::string last = path.substr(cut + 1);
    if (!last.empty() && last.back() == ']') last.pop_back();
    mt::Node node;
    if (!walkTo(*ix.plc, mt::root(g->second.first, g->second.second), base, node)) return;
    const std::string needle = ui::foldForSearch(last);
    for (const auto& k : mt::children(*ix.plc, node)) {
        std::string label = ui::foldForSearch(k.label);
        while (!label.empty() && (label.front() == '.' || label.front() == '[')) label.erase(label.begin());
        if (!needle.empty() && label.find(needle) == std::string::npos) continue;
        ++total;
        if (!collect || out.size() >= 200) continue;
        Result r;
        r.group = GMember;
        r.groupTitle = kTitles[GMember];
        r.title = k.real ? k.path : k.path + k.label;
        r.subtitle = (k.real ? k.type : mt::groupType(k)) + kDot + k.what + kDot + "membre de " + node.path;
        r.key = "member:" + r.title;
        r.icon = mt::isElementary(k.type) ? ui::Icon::Variable : ui::Icon::DerivedType;
        r.score = (label.rfind(needle, 0) == 0 ? 0 : 1000) + static_cast<int>(std::min<std::size_t>(r.title.size(), 999));
        out.push_back(std::move(r));
    }
}

} // namespace

const char* groupTitle(int group) { return group >= 0 && group < GCount ? kTitles[group] : ""; }
const char* groupChip(int group) { return group >= 0 && group < GCount ? kChips[group] : ""; }

// ======================================================================= Index ===
void Index::add(std::vector<Entry>& list, int group, std::string title, std::string subtitle, std::string key, ui::Icon icon,
                std::string extra, std::string hint) {
    Entry e;
    e.group = group;
    e.title = std::move(title);
    e.subtitle = std::move(subtitle);
    e.key = std::move(key);
    e.icon = icon;
    e.extra = std::move(extra);
    e.hint = std::move(hint);
    e.folded = ui::foldForSearch(e.title);
    e.titleEnd = e.folded.size();
    e.folded += '\n' + ui::foldForSearch(e.subtitle) + '\n' + ui::foldForSearch(e.extra);
    list.push_back(std::move(e));
}

void Index::addText(int group, std::string name, std::string key, const std::string& body, ui::Icon icon) {
    if (body.empty()) return;
    Text t;
    t.group = group;
    t.name = std::move(name);
    t.key = std::move(key);
    t.body = body;
    t.folded = ui::foldForSearch(t.body);
    t.lines.push_back(0);
    for (std::size_t i = 0; i < t.body.size(); ++i)
        if (t.body[i] == '\n') t.lines.push_back(i + 1);
    t.foldedLines.push_back(0);
    for (std::size_t i = 0; i < t.folded.size(); ++i)
        if (t.folded[i] == '\n') t.foldedLines.push_back(i + 1);
    t.icon = icon;
    texts.push_back(std::move(t));
}

void Index::addGlobal(const std::string& name, const std::string& type) { globals.push_back({lowerText(name), {name, type}}); }

void Index::finish() { std::sort(globals.begin(), globals.end()); }

void Index::clearProject() {
    plc.reset();
    entries.clear();
    texts.clear();
    globals.clear();
}

// ===================================================================== search ===
GoToPanel::Outcome search(const Index& ix, const std::string& text, int group) {
    GoToPanel::Outcome out;
    const auto category = [](int g, std::size_t total) { return GoToPanel::Category{g, kTitles[g], kChips[g], total}; };
    const ui::SearchQuery query(text);
    // Au moins un mot a chercher : "-mot" seul montrerait tout le projet.
    if (query.terms().empty()) {
        if (group >= 0 && group < GCount) out.categories.push_back(category(group, 0));
        return out;
    }
    const std::size_t cap = group >= 0 ? 200 : 8;
    // Par categorie : combien en tout (toutes, pour les pastilles), et les
    // meilleurs de celles montrees (un pointeur et un score : les resultats ne
    // sont fabriques que pour ceux qu'on montre).
    std::vector<std::size_t> totals(GCount, 0);
    std::vector<char> prefix(GCount, 0);          // un titre de la categorie commence par le texte
    std::vector<std::vector<std::pair<int, const Entry*>>> hits(GCount);
    std::vector<std::vector<Result>> extra(GCount);
    const std::string& first = query.terms().front();
    const auto consider = [&](const Entry& e) {
        for (const auto& ex : query.excluded())
            if (e.folded.find(ex) != std::string::npos) return;
        for (const auto& term : query.terms())
            if (e.folded.find(term) == std::string::npos) return;
        const auto gi = static_cast<std::size_t>(e.group);
        ++totals[gi];
        // Le rang : le titre commence par le premier mot (0), un mot du titre
        // commence par lui (1), le titre le contient (2), le reste seul (3).
        const auto at = e.folded.find(first);
        int r = 3;
        if (at < e.titleEnd) {
            const char before = at == 0 ? ' ' : e.folded[at - 1];
            const bool word = (before >= 'a' && before <= 'z') || (before >= '0' && before <= '9') || static_cast<unsigned char>(before) >= 0x80;
            r = at == 0 ? 0 : word ? 2 : 1;
        }
        if (r == 0) prefix[gi] = 1;
        if (group >= 0 && e.group != group) return;
        hits[gi].emplace_back(r * 1000 + static_cast<int>(std::min<std::size_t>(e.title.size(), 999)), &e);
    };
    for (const auto& e : ix.entries) consider(e);
    for (const auto& e : ix.library) consider(e);
    for (const auto& t : ix.texts)
        searchText(t, query, group < 0 || group == t.group, cap, extra[static_cast<std::size_t>(t.group)], totals[static_cast<std::size_t>(t.group)]);
    searchMembers(ix, text, group < 0 || group == GMember, extra[GMember], totals[GMember]);

    // Chaque categorie : ses meilleurs, dans l'ordre du score.
    std::vector<std::vector<Result>> shown(GCount);
    for (int g = 0; g < GCount; ++g) {
        const auto gi = static_cast<std::size_t>(g);
        auto& h = hits[gi];
        const auto keep = std::min(cap, h.size());
        std::partial_sort(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(keep), h.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t k = 0; k < keep; ++k) {
            const auto& e = *h[k].second;
            shown[gi].push_back(Result{e.group, kTitles[e.group], e.title, e.subtitle, e.hint, e.key, e.icon, h[k].first});
        }
        // Les lignes (code, scripts) et les membres, apres les noms.
        auto& x = extra[gi];
        std::stable_sort(x.begin(), x.end(), [](const Result& a, const Result& b) { return a.score < b.score; });
        for (auto& r : x) {
            if (shown[gi].size() >= cap) break;
            shown[gi].push_back(std::move(r));
        }
    }
    // Les categories ou un titre COMMENCE par le texte d'abord (un chemin tape :
    // ses membres), puis les autres ; chaque fois dans leur ordre. Le meme ordre
    // quelle que soit la categorie choisie : Tab les parcourt sans sauter.
    if (totals[GMember] > 0) prefix[GMember] = 1;
    std::vector<int> order;
    for (int pass = 0; pass < 2; ++pass)
        for (int g = 0; g < GCount; ++g) {
            const auto gi = static_cast<std::size_t>(g);
            if ((totals[gi] > 0 || g == group) && (prefix[gi] != 0) == (pass == 0)) order.push_back(g);
        }
    for (const int g : order) {
        out.categories.push_back(category(g, totals[static_cast<std::size_t>(g)]));
        for (auto& r : shown[static_cast<std::size_t>(g)]) out.results.push_back(std::move(r));
    }
    if (out.results.size() > 200) out.results.resize(200);
    return out;
}

} // namespace app::gotosearch
