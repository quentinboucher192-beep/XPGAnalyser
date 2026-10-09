// =============================================================================
//  help/CenterView.cpp - 1.11 (chantier T2, tranche 2) : ce que l'ecran du
//  centre d'aide dessine (voir l'en-tete)
// =============================================================================
#include "CenterView.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : les chapitres de chaque application

namespace help::center {

namespace {

constexpr Chapter kChapters[] = {Chapter::Start,  Chapter::Hmi,         Chapter::Plc,
                                 Chapter::Macros, Chapter::Blocks,      Chapter::Expressions,
                                 Chapter::Shortcuts, Chapter::Notes,    Chapter::Report};

std::string pillOf(const TutorialInfo& info) {
    if (info.steps <= 0) return {};
    return "\xE2\x96\xB6 " + formatDuration(info.durationMs);
}

} // namespace

std::string foldKey(Chapter c, std::string_view sub) {
    std::string k = (sub.empty() ? "ch:" : "sub:") + std::to_string(static_cast<int>(c));
    if (!sub.empty()) { k += ':'; k += sub; }
    return k;
}

std::vector<TreeRow> treeRows(const Index& ix, const TreeState& state) {
    const Topic* cur = state.current.empty() ? nullptr : ix.find(state.current);
    const auto isOpen = [&](const std::string& k, bool forced) { return forced || state.open.count(k) != 0; };

    std::vector<TreeRow> rows;
    for (const Chapter c : kChapters) {
        const auto topics = ix.ofChapter(c);
        // 1.12.0 : un chapitre vide est celui de l'autre application (XPGAnalyser API : ni l'IHM,
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
        // premier sujet (l'index les garde groupes).
        std::string lastSub;
        bool subOpen = true;
        for (const Topic* t : topics) {
            if (!t->sub.empty() && t->sub != lastSub) {
                lastSub = t->sub;
                TreeRow s;
                s.kind = RowKind::Sub;
                s.depth = 1;
                s.label = t->sub;
                s.key = foldKey(c, t->sub);
                s.chapter = c;
                for (const Topic* u : topics) s.count += u->sub == t->sub ? 1u : 0u;
                s.current = cur != nullptr && cur->chapter == c && cur->sub == t->sub;
                s.open = isOpen(s.key, s.current);
                subOpen = s.open;
                rows.push_back(s);
            } else if (t->sub.empty()) {
                lastSub.clear();
                subOpen = true;
            }
            if (!subOpen) continue;
            const TutorialInfo info = tutorialInfo(*t);
            TreeRow r;
            r.kind = RowKind::Topic;
            r.depth = t->sub.empty() ? 1 : 2;
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

std::vector<std::string> breadcrumb(const Index& ix, std::string_view key) {
    const Topic* t = ix.find(key);
    if (t == nullptr) return {};
    std::vector<std::string> out{std::string(chapterLabel(t->chapter))};
    if (!t->sub.empty()) out.push_back(t->sub);
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
