// =============================================================================
//  help/CenterIndex.cpp - 1.11 (chantier T2) : l'index unique du centre d'aide
// -----------------------------------------------------------------------------
//  L'arbre de la maquette 1.11 validee (NOTES.md, section 4) : Demarrer (le
//  chapitre 1 du guide, puis les pages generales de l'API), L'IHM (les
//  chapitres 2 a 8 du guide, un sous-chapitre chacun), L'automate (API) (le
//  chapitre 9 du guide, puis les pages de l'API), Macros, Blocs DFB / DDT (par
//  categorie), Expressions (11 types), Raccourcis, Notes de version (une page
//  par version), Signaler un probleme.
// =============================================================================
#include "CenterIndex.hpp"
#include "HelpIndex.hpp"
#include "HelpSession.hpp"   // 1.11.2 (T2) : help::parseTarget, pour keyOfLink sur "lib:" et "par:"

#include "ReleaseNotes.hpp"
#include "Shortcuts.hpp"

#include <algorithm>
#include <cstdio>

namespace help::center {

namespace {

// Les chapitres du guide qui ne vont pas dans L'IHM.
constexpr std::string_view kGuideStart = "D\xC3\xA9marrer";
constexpr std::string_view kGuidePlc   = "L'automate (API)";

InfoProvider& providerSlot() { static InfoProvider p; return p; }
Launcher&     launcherSlot() { static Launcher l; return l; }

TutorialInfo estimate(int steps, int durationMs) {
    TutorialInfo i;
    i.steps = steps;
    i.durationMs = durationMs;
    i.estimated = true;
    return i;
}

} // namespace

// ---- les chapitres ----------------------------------------------------------------
const std::vector<Chapter>& chapters() {
    static const std::vector<Chapter> k = {Chapter::Start, Chapter::Hmi, Chapter::Plc, Chapter::Macros, Chapter::Blocks,
                                           Chapter::Expressions, Chapter::Shortcuts, Chapter::Notes, Chapter::Report};
    return k;
}

std::string_view chapterLabel(Chapter c) {
    switch (c) {
        case Chapter::Start:       return "D\xC3\xA9marrer";
        case Chapter::Hmi:         return "L'IHM";
        case Chapter::Plc:         return "L'automate (API)";
        case Chapter::Macros:      return "Macros";
        case Chapter::Blocks:      return "Blocs DFB / DDT";
        case Chapter::Expressions: return "Expressions";
        case Chapter::Shortcuts:   return "Raccourcis";
        case Chapter::Notes:       return "Notes de version";
        case Chapter::Report:      return "Signaler un probl\xC3\xA8me";
    }
    return {};
}

const std::vector<std::pair<std::string_view, std::string_view>>& expressionTypes() {
    static const std::vector<std::pair<std::string_view, std::string_view>> k = {
        {"bool", "BOOL"},
        {"entier", "Entier"},
        {"reel", "R\xC3\xA9" "el"},
        {"texte", "Texte"},
        {"texte-a-trous", "Texte \xC3\xA0 trous {\xE2\x80\xA6}"},
        {"couleur", "Couleur"},
        {"duree", "Dur\xC3\xA9" "e / heure"},
        {"vue", "Vue"},
        {"enumeration", "\xC3\x89num\xC3\xA9ration"},
        {"membre", "Membre et \xC3\xA9l\xC3\xA9ment"},
        {"liste", "Liste de choix"},
    };
    return k;
}

// ---- les tutoriels ----------------------------------------------------------------
void setTutorialInfoProvider(InfoProvider p) { providerSlot() = std::move(p); }
void setTutorialLauncher(Launcher l) { launcherSlot() = std::move(l); }
bool hasTutorialLauncher() { return static_cast<bool>(launcherSlot()); }

TutorialInfo estimateTutorial(const Topic& t) {
    // La table "Etapes deduites" de la maquette (NOTES.md, section 2) ; ~8 s par etape
    // a la vitesse 1x, les trois tutoriels complets de la maquette pour etalon.
    const int sections = std::min(t.headings, 5);
    switch (t.source) {
        case Source::Guide:
            if (t.kind == "objet") return estimate(7, 64000);           // la vanne : 1 min 04
            return estimate(2 + sections, (2 + sections) * 8000);       // l'endroit, un par ###, A toi
        case Source::ApiPage:
            return estimate(std::max(3, 2 + sections), std::max(3, 2 + sections) * 8000);
        case Source::Macro:
            return estimate(6, 48000);
        case Source::Block:
            if (t.kind == "ddt") return estimate(4, 32000);
            return estimate(5, 45000);
        case Source::Expression:
            return estimate(5, 51000);                                  // l'expression : 51 s
        case Source::ShortcutsPage:
        case Source::NotesPage:
        case Source::ReportPage:
            return estimate(3, 24000);
    }
    return estimate(3, 24000);
}

TutorialInfo tutorialInfo(const Topic& t) {
    if (const auto& p = providerSlot()) {
        if (auto info = p(t.key); info && info->steps > 0) return *info;
    }
    return estimateTutorial(t);
}

std::string formatDuration(int ms) {
    const int s = (std::max(ms, 0) + 500) / 1000;
    char buf[32];
    if (s < 60) std::snprintf(buf, sizeof buf, "%d s", s);
    else std::snprintf(buf, sizeof buf, "%d min %02d", s / 60, s % 60);
    return buf;
}

bool launchTutorial(const TutorialRequest& r) {
    const auto& l = launcherSlot();
    return l ? l(r) : false;
}

// ---- l'index ----------------------------------------------------------------------
void Index::add(Topic t) {
    if (find(t.key)) { duplicates_.push_back(t.key); return; }
    topics_.push_back(std::move(t));
}

Index Index::build(const Inputs& in) {
    Index ix;
    const auto fromGuide = [&ix](const SourceTopic& s, Chapter c, std::string sub) {
        ix.add(Topic{s.key, c, std::move(sub), s.title, s.summary, Source::Guide, s.key, s.kind, s.headings});
    };
    const auto fromApi = [&ix](const SourceTopic& s, Chapter c) {
        ix.add(Topic{"api-" + s.key, c, {}, s.title, s.summary, Source::ApiPage, s.key, s.kind, s.headings});
    };

    // Demarrer : le chapitre 1 du guide, puis les pages generales de l'API.
    for (const auto& s : in.guide)
        if (s.group == kGuideStart) fromGuide(s, Chapter::Start, {});
    for (const auto& s : in.api)
        if (s.group == "start") fromApi(s, Chapter::Start);

    // L'IHM : les chapitres 2 a 8 du guide, chacun un sous-chapitre.
    for (const auto& s : in.guide)
        if (s.group != kGuideStart && s.group != kGuidePlc) fromGuide(s, Chapter::Hmi, s.group);

    // L'automate (API) : le chapitre 9 du guide, puis les pages de l'API.
    for (const auto& s : in.guide)
        if (s.group == kGuidePlc) fromGuide(s, Chapter::Plc, {});
    for (const auto& s : in.api)
        if (s.group != "start") fromApi(s, Chapter::Plc);

    // Macros. Tranche 11 : la bibliotheque les range dans une seule famille, "Macros",
    // le nom du chapitre : sans ce sous-niveau, l'arbre et le fil d'Ariane ne disent
    // plus "Macros > Macros > CreerEquipement". Une autre famille garde son niveau.
    for (const auto& s : in.library)
        if (s.kind == "macro") {
            const std::string group = s.group == chapterLabel(Chapter::Macros) ? std::string{} : s.group;
            ix.add(Topic{"macro-" + s.key, Chapter::Macros, group, s.title, s.summary, Source::Macro, s.key, s.kind, 0});
        }

    // Blocs DFB / DDT, par categorie (l'ordre des categories : leur premiere apparition).
    std::vector<std::string> categories;
    for (const auto& s : in.library)
        if (s.kind != "macro" && std::find(categories.begin(), categories.end(), s.group) == categories.end())
            categories.push_back(s.group);
    for (const auto& cat : categories)
        for (const auto& s : in.library)
            if (s.kind != "macro" && s.group == cat)
                ix.add(Topic{"bloc-" + s.key, Chapter::Blocks, cat, s.title, s.summary, Source::Block, s.key, s.kind, 0});

    // Expressions : les 11 types de la page des expressions. L'appli les lit
    // dans hmi::exprguide::all() (T3) et les passe dans in.expressions (cle,
    // titre, resume) ; sans eux, la meme liste ecrite ici (cles et titres de T3).
    if (!in.expressions.empty()) {
        for (const auto& s : in.expressions)
            ix.add(Topic{"expr-" + s.key, Chapter::Expressions, {}, s.title, s.summary, Source::Expression, s.key, {}, 0});
    } else {
        for (const auto& [key, title] : expressionTypes())
            ix.add(Topic{"expr-" + std::string(key), Chapter::Expressions, {}, std::string(title), {},
                         Source::Expression, std::string(key), {}, 0});
    }

    // Raccourcis.
    ix.add(Topic{"page-raccourcis", Chapter::Shortcuts, {}, "Les raccourcis clavier",
                 "Toutes les touches, group\xC3\xA9" "es par contexte ; la fiche A4 \xC3\xA0 imprimer.",
                 Source::ShortcutsPage, {}, {}, 0});

    // Notes de version : une page par version, la plus recente d'abord.
    for (const auto& r : notes::releases())
        ix.add(Topic{"notes-" + std::string(r.version), Chapter::Notes, {}, "XPGAnalyser " + std::string(r.version),
                     notes::summary(r.version), Source::NotesPage, std::string(r.version), {}, 0});

    // Signaler un probleme.
    ix.add(Topic{"page-signaler", Chapter::Report, {}, "Signaler un probl\xC3\xA8me",
                 "Ce qui s'est pass\xC3\xA9, comment le refaire, ce que tu attendais : un zip pr\xC3\xAAt \xC3\xA0 envoyer.",
                 Source::ReportPage, {}, {}, 0});
    return ix;
}

const Topic* Index::find(std::string_view key) const {
    for (const auto& t : topics_)
        if (t.key == key) return &t;
    return nullptr;
}

std::vector<const Topic*> Index::ofChapter(Chapter c) const {
    std::vector<const Topic*> out;
    for (const auto& t : topics_)
        if (t.chapter == c) out.push_back(&t);
    return out;
}

std::size_t Index::count(Chapter c) const {
    return static_cast<std::size_t>(std::count_if(topics_.begin(), topics_.end(),
                                                  [c](const Topic& t) { return t.chapter == c; }));
}

const Topic* Index::next(std::string_view key) const {
    for (std::size_t i = 0; i + 1 < topics_.size(); ++i)
        if (topics_[i].key == key) return &topics_[i + 1];
    return nullptr;
}

const Topic* Index::prev(std::string_view key) const {
    for (std::size_t i = 1; i < topics_.size(); ++i)
        if (topics_[i].key == key) return &topics_[i - 1];
    return nullptr;
}

// ---- la recherche unique ------------------------------------------------------------
std::string_view groupLabel(GroupKind g) {
    switch (g) {
        case GroupKind::Hmi:         return "Sujets de l'IHM";
        case GroupKind::Plc:         return "L'automate (API)";
        case GroupKind::Macros:      return "Macros";
        case GroupKind::Blocks:      return "Blocs DFB / DDT";
        case GroupKind::Expressions: return "Expressions";
        case GroupKind::Shortcuts:   return "Raccourcis";
        case GroupKind::Notes:       return "Notes de version";
    }
    return {};
}

std::size_t groupLimit(GroupKind g) {
    return (g == GroupKind::Shortcuts || g == GroupKind::Notes) ? 3 : 4;
}

namespace {

GroupKind groupOf(const Topic& t) {
    switch (t.source) {
        case Source::Guide:         return t.chapter == Chapter::Plc ? GroupKind::Plc : GroupKind::Hmi;
        case Source::ApiPage:       return GroupKind::Plc;
        case Source::Macro:         return GroupKind::Macros;
        case Source::Block:         return GroupKind::Blocks;
        case Source::Expression:    return GroupKind::Expressions;
        case Source::ShortcutsPage: return GroupKind::Shortcuts;
        case Source::NotesPage:     return GroupKind::Notes;
        case Source::ReportPage:    return GroupKind::Hmi;
    }
    return GroupKind::Hmi;
}

// Le terme dans `text` (replie) : le score et les plages a surligner (dans le texte d'origine).
int matchText(std::string_view text, const std::string& term, std::vector<std::pair<std::size_t, std::size_t>>* marks) {
    std::vector<std::size_t> origin;
    const auto folded = keys::fold(text, &origin);
    const auto at = folded.find(term);
    if (at == std::string::npos) return 0;
    if (marks) marks->emplace_back(origin[at], origin[at + term.size()]);
    if (at == 0) return 100;                                         // le titre commence par le terme
    const char before = folded[at - 1];
    return (before == ' ' || before == '-' || before == '_' || before == '(' || before == '.') ? 70 : 50;
}

} // namespace

// ---- F1 et la navigation (1.11, tranche 2) ----
help::Target topicTarget(std::string_view key) {
    return help::Target{help::TargetKind::Topic, std::string(key), {}};
}

std::string Index::keyOf(const help::Target& t) const {
    switch (t.kind) {
        case help::TargetKind::Topic:
            return find(t.entry) != nullptr ? t.entry : std::string{};
        case help::TargetKind::LibraryEntry:
        case help::TargetKind::Parameter:
            for (const char* pre : {"bloc-", "macro-"})
                if (std::string k = pre + t.entry; find(k) != nullptr) return k;
            return {};
        default:
            return {};
    }
}

std::string Index::keyOfLink(std::string_view target) const {
    for (const std::string_view pre : {std::string_view("sujet:"), std::string_view("topic:")})
        if (target.substr(0, pre.size()) == pre) {
            target.remove_prefix(pre.size());
            break;
        }
    // 1.11.2 (T2, decision 141) : les liens des pages de Macros et de Blocs DFB / DDT
    // (buildHelpArticle : "lib:<nom>" pour Voir aussi et les renvois, "par:<nom>#<parametre>")
    // menent a la page du centre de ce nom (bloc- ou macro-), comme les favoris de la 1.10.
    // Dans la 1.11.0 et la 1.11.1, aucune branche de HelpCenterScreen::onLink ne les prenait.
    if (target.substr(0, 4) == "lib:" || target.substr(0, 4) == "par:") return keyOf(help::parseTarget(target));
    return find(target) != nullptr ? std::string(target) : std::string{};
}

std::string Index::forF1(const F1Place& p) const {
    const auto trim = [](std::string_view s) {
        while (!s.empty() && static_cast<unsigned char>(s.front()) <= ' ') s.remove_prefix(1);
        while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ') s.remove_suffix(1);
        return std::string(s);
    };
    if (!p.guideTopic.empty() && find(p.guideTopic) != nullptr) return p.guideTopic;
    if (!p.libraryEntry.empty())
        if (std::string k = keyOf(help::Target{help::TargetKind::LibraryEntry, p.libraryEntry, {}}); !k.empty()) return k;
    if (!p.apiAnchor.empty()) {
        if (std::string k = "api-" + p.apiAnchor; find(k) != nullptr) return k;
        if (find(p.apiAnchor) != nullptr) return p.apiAnchor;
    }
    const std::string w = trim(p.word);
    if (w.empty()) return {};
    for (const char* pre : {"", "bloc-", "macro-", "expr-", "api-"})
        if (std::string k = pre + w; find(k) != nullptr) return k;
    const std::string fw = keys::fold(w);
    for (const auto& t : topics_)
        if (keys::fold(t.title) == fw) return t.key;
    for (const auto& g : search(w))
        for (const auto& h : g.hits)
            if (h.topic != nullptr) return h.topic->key;
    return {};
}

namespace {
// Recette 1.11 (T2, tranche 15) : vrai le temps d'une searchUnlimited().
bool gNoLimit = false;
} // namespace

// La meme recherche, sans la limite par groupe : le centre montre tout un groupe
// quand on clique sur " ... les N autres " (declaree la ou elle sert, dans
// HelpCenterScreen.cpp, pour ne pas toucher a CenterIndex.hpp avant la 1.11).
std::vector<SearchGroup> searchUnlimited(const Index& ix, std::string_view term) {
    gNoLimit = true;
    auto groups = ix.search(term);
    gNoLimit = false;
    return groups;
}

std::vector<SearchGroup> Index::search(std::string_view term) const {
    std::string t = keys::fold(term);
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (t.empty()) return {};

    std::vector<SearchGroup> groups;
    const auto groupFor = [&groups](GroupKind k) -> SearchGroup& {
        for (auto& g : groups)
            if (g.kind == k) return g;
        groups.push_back(SearchGroup{k, {}, 0});
        return groups.back();
    };

    for (const auto& topic : topics_) {
        SearchHit h;
        h.topic = &topic;
        h.title = topic.title;
        h.score = matchText(topic.title, t, &h.marks);
        if (h.score == 0 && topic.source != Source::Guide && topic.source != Source::ApiPage)
            h.score = matchText(topic.ref, t, nullptr) ? 45 : 0;        // le nom du bloc, la version
        if (h.score == 0) h.score = matchText(topic.summary, t, nullptr) ? 20 : 0;
        if (h.score == 0) continue;
        h.detail = topic.sub.empty() ? std::string(chapterLabel(topic.chapter)) : topic.sub;
        groupFor(groupOf(topic)).hits.push_back(std::move(h));
    }
    for (const auto* s : keys::search(t)) {
        SearchHit h;
        h.shortcut = s;
        h.title = std::string(s->keys);
        h.detail = std::string(s->text) + " \xC2\xB7 " + std::string(keys::contextLabel(s->context));
        h.score = matchText(s->keys, t, &h.marks);
        if (h.score == 0) h.score = 20;
        groupFor(GroupKind::Shortcuts).hits.push_back(std::move(h));
    }
    for (const auto* n : notes::search(t)) {
        SearchHit h;
        h.note = n;
        h.title = std::string(n->text);
        h.detail = std::string(n->version) + " \xC2\xB7 " + std::string(n->domain);
        h.score = matchText(n->text, t, &h.marks);
        if (h.score == 0) h.score = 10;
        groupFor(GroupKind::Notes).hits.push_back(std::move(h));
    }

    for (auto& g : groups) {
        std::stable_sort(g.hits.begin(), g.hits.end(),
                         [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
        g.total = g.hits.size();
        if (!gNoLimit && g.hits.size() > groupLimit(g.kind)) g.hits.resize(groupLimit(g.kind));
    }
    std::sort(groups.begin(), groups.end(),
              [](const SearchGroup& a, const SearchGroup& b) { return a.kind < b.kind; });
    return groups;
}

} // namespace help::center
