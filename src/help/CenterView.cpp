// =============================================================================
//  help/CenterView.cpp - 1.11 (chantier T2, tranche 2) : ce que l'ecran du
//  centre d'aide dessine (voir l'en-tete)
// =============================================================================
#include "CenterView.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : les chapitres de chaque application

#include <algorithm>

namespace help::center {

namespace {

constexpr Chapter kChapters[] = {Chapter::Start,  Chapter::Hmi,         Chapter::Plc,
                                 Chapter::Macros, Chapter::Blocks,      Chapter::Expressions,
                                 Chapter::Shortcuts, Chapter::Notes,    Chapter::Report};

std::string pillOf(const TutorialInfo& info) {
    if (info.steps <= 0) return {};
    return "\xE2\x96\xB6 " + formatDuration(info.durationMs);
}

// 1.12.27 : UN SOUS-CHAPITRE A LES SIENS. Le guide ecrit "La bibliotheque d'objets > Formes" (le
// chapitre du guide, puis sa famille) : deux niveaux de l'arbre, chacun repliable.
constexpr std::string_view kSubSep = " \xE2\x80\xBA ";

// Les niveaux d'un sous-chapitre, chacun par son chemin entier : "A > B" -> {"A", "A > B"}.
std::vector<std::string> levelsOf(const std::string& sub) {
    std::vector<std::string> out;
    if (sub.empty()) return out;
    for (auto at = sub.find(kSubSep); at != std::string::npos; at = sub.find(kSubSep, at + kSubSep.size()))
        out.push_back(sub.substr(0, at));
    out.push_back(sub);
    return out;
}

// Ce que la ligne d'un niveau ecrit : son dernier morceau.
std::string lastPart(const std::string& level) {
    const auto at = level.rfind(kSubSep);
    return at == std::string::npos ? level : level.substr(at + kSubSep.size());
}

// Le sous-chapitre `sub` est-il dans le niveau `level` (lui-meme, ou plus fin) ?
bool within(const std::string& sub, const std::string& level) {
    if (sub.size() < level.size() || sub.compare(0, level.size(), level) != 0) return false;
    return sub.size() == level.size() || sub.compare(level.size(), kSubSep.size(), kSubSep) == 0;
}

} // namespace

std::string foldKey(Chapter c, std::string_view sub) {
    std::string k = (sub.empty() ? "ch:" : "sub:") + std::to_string(static_cast<int>(c));
    if (!sub.empty()) { k += ':'; k += sub; }
    return k;
}

std::vector<TreeRow> treeRows(const Index& ix, const TreeState& state) {
    const Topic* cur = state.current.empty() ? nullptr : ix.find(state.current);
    // 1.12.28 : deplie d'office (la page ouverte est dedans), sauf si on l'a replie soi-meme.
    const auto isOpen = [&](const std::string& k, bool forced) {
        return (forced && state.closed.count(k) == 0) || state.open.count(k) != 0;
    };

    std::vector<TreeRow> rows;
    for (const Chapter c : kChapters) {
        const auto topics = ix.ofChapter(c);
        // 1.12.0 : un chapitre vide est celui de l'autre application (API Studio : ni l'IHM,
        // ni les expressions ; IHM : ni l'automate, ni les macros, ni les blocs) - il ne se montre pas.
        if (topics.empty() && c != Chapter::Start && core::edition() != core::Edition::Both) continue;
        TreeRow ch;
        ch.kind = RowKind::Chapter;
        ch.depth = 0;
        ch.label = std::string(chapterLabel(c));
        ch.key = foldKey(c);
        ch.chapter = c;
        ch.count = topics.size();
        ch.current = cur != nullptr && cur->chapter == c;
        ch.open = isOpen(ch.key, ch.current);
        rows.push_back(ch);
        if (!ch.open) continue;

        // Les sujets dans l'ordre de l'arbre ; un sous-chapitre s'ouvre a son
        // premier sujet (l'index les garde groupes). 1.12.27 : il peut en avoir
        // lui-meme (levelsOf) ; un niveau replie cache tout ce qu'il contient.
        std::vector<std::string> levels;   // les niveaux ouverts par le sujet d'avant
        std::vector<bool> levelOpen;       // deplie, pour chacun
        for (const Topic* t : topics) {
            const auto path = levelsOf(t->sub);
            std::size_t same = 0;
            while (same < path.size() && same < levels.size() && path[same] == levels[same]) ++same;
            levels.resize(same);
            levelOpen.resize(same);
            bool shown = std::find(levelOpen.begin(), levelOpen.end(), false) == levelOpen.end();
            for (std::size_t lv = same; lv < path.size(); ++lv) {
                TreeRow s;
                s.kind = RowKind::Sub;
                s.depth = static_cast<int>(lv) + 1;
                s.label = lastPart(path[lv]);
                s.key = foldKey(c, path[lv]);
                s.chapter = c;
                for (const Topic* u : topics) s.count += within(u->sub, path[lv]) ? 1u : 0u;
                s.current = cur != nullptr && cur->chapter == c && within(cur->sub, path[lv]);
                s.open = isOpen(s.key, s.current);
                if (shown) rows.push_back(s);
                levels.push_back(path[lv]);
                levelOpen.push_back(s.open);
                shown = shown && s.open;
            }
            if (!shown) continue;
            const TutorialInfo info = tutorialInfo(*t);
            TreeRow r;
            r.kind = RowKind::Topic;
            r.depth = static_cast<int>(path.size()) + 1;
            r.label = t->title;
            r.key = t->key;
            r.chapter = c;
            if (hasTutorial(*t)) {   // decision 12 : pas de pastille sur les notes de version
                r.pill = pillOf(info);
                r.estimated = info.estimated;
            }
            r.current = cur == t;
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

std::vector<std::string> foldsOf(const Index& ix, std::string_view key) {
    const Topic* t = ix.find(key);
    if (t == nullptr) return {};
    std::vector<std::string> out{foldKey(t->chapter)};
    for (const auto& level : levelsOf(t->sub)) out.push_back(foldKey(t->chapter, level));
    return out;
}

std::vector<std::string> breadcrumb(const Index& ix, std::string_view key) {
    const Topic* t = ix.find(key);
    if (t == nullptr) return {};
    std::vector<std::string> out{std::string(chapterLabel(t->chapter))};
    for (const auto& level : levelsOf(t->sub)) out.push_back(lastPart(level));   // 1.12.27 : chaque niveau
    out.push_back(t->title);
    return out;
}

std::string tutorialCardText(const TutorialInfo& info) {
    if (info.steps <= 0) return "Le tutoriel de ce sujet arrive";
    std::string s = "\xE2\x96\xB6 Regarder le tutoriel \xC2\xB7 " + formatDuration(info.durationMs) + " \xC2\xB7 "
                  + std::to_string(info.steps) + (info.steps > 1 ? " \xC3\xA9tapes" : " \xC3\xA9tape");
    if (info.estimated) s += " (dur\xC3\xA9" "e estim\xC3\xA9" "e)";
    return s;
}

bool hasTutorial(const Topic& t) { return t.source != Source::NotesPage; }

TutorialCount tutorialCount(const Index& ix) {
    TutorialCount c;
    for (const auto& t : ix.topics()) {
        if (!hasTutorial(t)) continue;   // decision 12 : les notes de version sortent du compteur
        ++c.total;
        const TutorialInfo info = tutorialInfo(t);
        if (info.steps > 0 && !info.estimated) ++c.provided;
    }
    return c;
}

std::string tutorialCountText(const TutorialCount& c) {
    // Tranche 7 : "0 / 262" sous "Tous les sujets ont leur tutoriel" se contredisait
    // tant que les tutoriels de T1 et T3 ne sont pas branches (durees estimees).
    if (c.provided >= c.total)
        return "Tous les sujets ont leur tutoriel : " + std::to_string(c.provided) + " / " + std::to_string(c.total);
    return "Tutoriels pr\xC3\xAAts : " + std::to_string(c.provided) + " / " + std::to_string(c.total)
         + " (les autres : dur\xC3\xA9" "e estim\xC3\xA9" "e)";
}

PageFooter pageFooter(const Index& ix, std::string_view key) {
    PageFooter f;
    if (const Topic* p = ix.prev(key)) { f.prevKey = p->key; f.prevTitle = p->title; }
    if (const Topic* n = ix.next(key)) { f.nextKey = n->key; f.nextTitle = n->title; }
    return f;
}

} // namespace help::center
