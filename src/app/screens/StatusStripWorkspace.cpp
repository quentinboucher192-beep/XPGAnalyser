// =============================================================================
//  screens/StatusStripWorkspace.cpp - Lot API 8 : le bandeau bas de l'ecran
//  d'analyse (la barre d'etat).
// -----------------------------------------------------------------------------
//  A gauche, le dernier message (dessine par ui::StatusBar, avec son icone) :
//  un clic l'ouvre en JOURNAL (les 50 derniers, l'heure, "Aller a" l'onglet
//  ou il a ete ecrit, Copier, Vider). Puis, de gauche a droite, les pastilles
//  cliquables (app/StatusStrip) : l'heure du message et son chevron, la
//  selection de l'onglet ouvert, les compteurs (le dernier Compiler de l'IHM,
//  "A regarder" de l'API), la simulation, la cible, le zoom de l'onglet, le
//  theme, l'enregistrement, les raccourcis du moment. Quand la place manque,
//  les moins utiles s'effacent (les raccourcis d'abord).
// =============================================================================
#include "Screens.hpp"
#include "../../core/Edition.hpp"   // 1.12.0 : la bande d'etat de chaque application
#include "../hmi/HmiSimulation.hpp"   // 1.12.0 : l'etat de la simulation de l'IHM (IHM Studio)

#include "../App.hpp"
#include "../ApiPanes.hpp"
#include "../GrafcetView.hpp"
#include "../RackView.hpp"
#include "../SimulationHost.hpp"
#include "../StatusStrip.hpp"
#include "../TaskPanes.hpp"
#include "../VariablesPane.hpp"
#include "../hmi/HmiBuildPanes.hpp"   // 1.12.31 : le panneau du bas (son zoom)
#include "../hmi/HmiCanvas.hpp"
#include "../hmi/HmiLanguageCompare.hpp"   // 1.12.31 : le tableau des langues (son zoom)
#include "../hmi/HmiPanes.hpp"
#include "../hmi/HmiVariablePanes.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCheck.hpp"
#include "../../project/TypeUsage.hpp"
#include "../../ui/Layout.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/TabArea.hpp"
#include "../../ui/Zoom.hpp"          // 1.12.30 : le zoom de chaque page
#include "../ZoomMemory.hpp"             // 1.12.31 : le zoom garde d'une seance a l'autre

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <typeinfo>
#include <vector>

namespace app {

using namespace ui;

namespace {

// Les numeros des entrees du menu du bandeau (un seul PopupMenu, trois usages).
constexpr int kJournalCopy  = 1;
constexpr int kJournalClear = 2;
constexpr int kJournalFirst = 100;   // 100 + rang d'un message
constexpr int kZoomIn       = 290;   // 1.12.30 : Zoom avant, Zoom arriere, 100 %
constexpr int kZoomOut      = 291;
constexpr int kZoomReset    = 292;
constexpr int kZoomFirst    = 300;   // 300 + pourcentage / 5 (75 -> 315 ...)
constexpr int kThemeAll     = 499;
constexpr int kThemeFirst   = 500;   // 500 + rang d'un theme recent

// Couper un texte a `max` octets sans couper un caractere UTF-8 en deux.
std::string clip(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t keep = max;
    while (keep > 0 && (static_cast<unsigned char>(s[keep]) & 0xC0) == 0x80) --keep;
    return s.substr(0, keep) + "\xE2\x80\xA6";
}

Icon iconOf(StatusBar::Severity s) {
    switch (s) {
        case StatusBar::Severity::Info:    return Icon::Info;
        case StatusBar::Severity::Success: return Icon::Ok;
        case StatusBar::Severity::Warning: return Icon::Warning;
        case StatusBar::Severity::Error:   return Icon::Error;
        case StatusBar::Severity::None:    break;
    }
    return Icon::None;
}

// Le widget qui a le clavier, sous `w` (nul : aucun).
const Widget* focusedUnder(const Widget& w) {
    if (!w.visible()) return nullptr;
    if (w.focused()) return &w;
    for (const auto& c : w.children())
        if (const auto* f = focusedUnder(*c)) return f;
    return nullptr;
}

// 1.12.30 : CE QUE ZOOME UNE PAGE. Le contrat est celui de Widget (zoomSupport).
// 1.12.31 : LA PAGE ENTIERE. Son widget du haut se zoome (Scaled) : ses barres
// d'outils et de filtres, ses sous-onglets, ses tableaux, ses arbres, ses panneaux,
// ses lignes d'etat grandissent ensemble, et ses marges ; un widget Native (un code,
// un grafcet, une vue, le rack, l'IHM en marche) y est un ILOT : il garde son zoom
// a lui - c'est le document de la page (ui/Widget.hpp).
// La puce vise le dernier vise (un clic, Ctrl + molette) s'il est dans la page,
// sinon le plus grand document s'il couvre au moins le tiers de la page, sinon la page.
struct PageZoomTarget {
    Widget* native{nullptr};                 // un document : son zoom
    std::vector<Widget*> scaled;             // sinon : la page (son widget du haut)
    [[nodiscard]] bool any() const noexcept { return native || !scaled.empty(); }
};

// Le cadre d'un widget a l'ecran (une page zoomee agrandit celui de ses widgets).
gfx::Rect onScreen(const Widget* w) {
    if (!w) return {};
    const auto f = w->zoomFrame();
    return w->parent() ? w->parent()->toSurface(f) : f;
}
float areaOf(const Widget* w) {
    const auto r = onScreen(w);
    return r.w * r.h;
}

// La page se zoome toute : son widget du haut (sans zoom) devient Scaled - a 100 %,
// rien ne change (ui/Widget.hpp). Un widget du haut Native (un document seul) garde le sien.
void ensurePageZoomable(Widget& page) {
    if (page.zoomSupport() == ZoomSupport::None) page.setZoomSupport(ZoomSupport::Scaled);
}

PageZoomTarget pageZoomTarget(Widget& page, const Widget* active) {
    PageZoomTarget t;
    t.scaled = ui::scaledUnder(page);
    const bool activeHere = active && Widget::inTree(page, active);
    if (activeHere && active->zoomSupport() == ZoomSupport::Native && active->visible()) {
        t.native = const_cast<Widget*>(active);
        return t;
    }
    if (activeHere && active->zoomSupport() == ZoomSupport::Scaled) return t;
    const auto natives = ui::nativesUnder(page);
    if (!natives.empty() && (t.scaled.empty() || areaOf(natives.front()) * 3.f >= areaOf(&page))) t.native = natives.front();
    return t;
}

// Le nom de ce que zoome la puce (l'en-tete de son menu, son infobulle).
std::string zoomTargetName(const PageZoomTarget& t) {
    if (!t.native) return "la page";
    if (dynamic_cast<MultiLineText*>(t.native)) return "le texte";
    if (dynamic_cast<GrafcetView*>(t.native)) return "le grafcet";
    if (dynamic_cast<HmiCanvas*>(t.native)) return "la vue";
    if (dynamic_cast<RackView*>(t.native)) return "le rack";
    if (dynamic_cast<HmiLanguageBoard*>(t.native)) return "la vue dans chaque langue";   // 1.12.31
    return "la vue en marche";
}

// 1.12.31 : les codes d'une page (leur zoom se garde aussi : la taille du texte a lire ;
// celui d'une vue, d'un grafcet, du rack est un cadrage, refait a chaque ouverture).
void codesUnder(Widget& w, std::vector<MultiLineText*>& out) {
    if (auto* m = dynamic_cast<MultiLineText*>(&w); m && m->zoomSupport() == ZoomSupport::Native) { out.push_back(m); return; }
    for (const auto& c : w.children()) codesUnder(*c, out);
}

// Le premier widget de ce type sous `w`, montre (un volet dans un autre).
template <class T>
T* visibleUnder(Widget& w) {
    if (!w.visible()) return nullptr;
    if (auto* t = dynamic_cast<T*>(&w)) return t;
    for (const auto& c : w.children())
        if (auto* f = visibleUnder<T>(*c)) return f;
    return nullptr;
}

} // namespace

struct MainAnalysisScreen::StatusStripState {
    MessageJournal journal;
    StatusStrip*   strip{nullptr};
    StatusChip*    when{nullptr};       // l'heure du dernier message et le chevron
    StatusChip*    selection{nullptr};
    StatusChip*    errors{nullptr};
    StatusChip*    warnings{nullptr};
    StatusChip*    sim{nullptr};
    StatusChip*    target{nullptr};
    StatusChip*    zoom{nullptr};
    StatusChip*    theme{nullptr};
    StatusChip*    save{nullptr};
    StatusChip*    keys{nullptr};
    PopupMenu*     menu{nullptr};
    int            menuKind{0};         // 0 : ferme ; 1 : le journal ; 2 : le zoom ; 3 : les themes
    std::vector<std::string> journalTargets;    // la cible de chaque ligne du menu du journal
    std::vector<std::string> recentThemes;      // les cles, la plus recente en tete (6 au plus)
    std::string    lastTheme;
    std::map<const Widget*, std::function<std::string()>> selections;
    // Les compteurs : le dernier Compiler de l'IHM, les points de A regarder.
    std::size_t    hmiErrors{0}, hmiWarnings{0}, apiPoints{0};
    std::string    apiDetail;
    double         lastCount{-1.0e9};
    // La selection lue par l'ecran : sa cle ("v:<chemin>", "s:<section>",
    // "h:<vue>"), la revision du projet, le texte.
    std::string    selKey, selText;
    std::uint64_t  selRevision{0};
    bool           quiet{false};        // session rejouee : pas d'infobulle, pas de surbrillance
    // 1.12.30 : le zoom de chaque page (ses widgets Scaled, ensemble), et le dernier
    // widget zoomable vise (un clic, Ctrl + molette) : la puce le suit.
    std::map<const Widget*, float> pageZoom;
    const Widget*  activeZoomable{nullptr};
    double         lastZoomSync{-1.0e9};
    // 1.12.31 : les pages deja vues (une page vue pour la premiere fois reprend son zoom
    // retenu), et le zoom retenu de chaque code (pour n'ecrire que ce qui change).
    std::set<const Widget*> seenPages;
    std::map<const Widget*, float> codeZoom;
};

// Le texte de la selection d'une cle (voir tickStatusStrip) ; "" : rien.
std::string MainAnalysisScreen::describeStatusSelection(const std::string& key) const {
    const auto project = app_.project();
    if (key.size() < 3 || !project) return {};
    const std::string what = key.substr(2);
    const std::string dotSep = " \xC2\xB7 ";
    if (key[0] == 'v') {
        // Une variable (ou un membre) : son type, son adresse, qui la lit.
        const auto rootEnd = what.find_first_of(".[");
        const std::string root = what.substr(0, rootEnd);
        for (const auto& v : project->variables) {
            if (project->strings.text(v.name) != root) continue;
            std::string t = what;
            if (rootEnd == std::string::npos) {
                const auto type = project->strings.text(v.type.name);
                if (!type.empty()) t += dotSep + std::string(type);
            }
            if (v.address.valid() && !v.address.raw.empty()) t += dotSep + v.address.raw;
            std::string lowered = root;
            for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const auto reads = hmiReadsOfPlc();
            if (auto it = reads.find(lowered); it != reads.end() && it->second.uses > 0)
                t += dotSep + "lue par l'IHM : " + std::to_string(it->second.uses) + (it->second.uses > 1 ? " emplois" : " emploi")
                     + (it->second.via.empty() ? std::string{} : " (via " + it->second.via + ")");
            else
                t += dotSep + "pas lue par l'IHM";
            // Les sections qui la nomment : le compte de la colonne Utilisee de API > Variables
            // (referenceCount n'est rempli par personne).
            const project::usage::Where where(*project);
            const auto n = where.sectionsOf(root).size();
            if (n == 0) t += dotSep + "nomm\xC3\xA9" "e dans aucune section";
            else t += dotSep + "nomm\xC3\xA9" "e dans " + std::to_string(n) + (n > 1 ? " sections : " : " section : ") + where.sectionNames(root, 3);
            return t;
        }
        return what;
    }
    if (key[0] == 's') {
        // Une section : son langage, ses lignes, sa tache et son rang.
        const auto index = static_cast<std::size_t>(std::strtoull(what.c_str(), nullptr, 10));
        if (index >= project->sections.size()) return {};
        const auto& s = project->sections[index];
        std::string t = "Section " + std::string(project->strings.text(s.name)) + dotSep + std::string(domain::toString(s.language))
                        + dotSep + groupedCount(s.lineCount) + (s.lineCount > 1 ? " lignes" : " ligne");
        const auto task = project->strings.text(s.task);
        if (!task.empty()) t += dotSep + std::string(task) + (s.isSubroutine ? ", sous-routine" : ", rang " + std::to_string(s.order));
        return t;
    }
    if (key[0] == 'i') {
        // Une variable de l'IHM (ou un membre) : son type, sa liaison, sa description.
        const auto doc = app_.hmi();
        const auto colon = what.find(':');
        const auto id = static_cast<hmi::Id>(std::strtoull(what.c_str(), nullptr, 10));
        const auto* v = doc && id != hmi::kNoId ? doc->project.variableById(id) : nullptr;
        if (!v) return {};
        const std::string path = colon == std::string::npos ? std::string{} : what.substr(colon + 1);
        std::string t = path.empty() ? v->name : path;
        if (path.empty() || path == v->name) t += dotSep + v->type;
        if (!v->equipment.empty()) t += dotSep + v->equipment + (v->address.empty() ? std::string{} : " " + v->address);
        else if (!v->address.empty()) t += dotSep + v->address;
        else t += dotSep + "interne \xC3\xA0 l'IHM";
        if (v->readOnly) t += dotSep + "lecture seule";
        if (!v->description.empty()) t += dotSep + v->description;
        return t;
    }
    if (key[0] == 'h') {
        // Une vue de l'IHM : sa taille, ses objets.
        const auto doc = app_.hmi();
        const auto id = static_cast<hmi::Id>(std::strtoull(what.c_str(), nullptr, 10));
        const auto* view = doc ? doc->project.view(id) : nullptr;
        if (!view) return {};
        return "Vue " + view->name + dotSep + std::to_string(view->width) + " \xC3\x97 " + std::to_string(view->height) + dotSep
               + std::to_string(view->objects.size()) + (view->objects.size() > 1 ? " objets" : " objet");
    }
    return {};
}

// ------------------------------------------------------------------ build ---
void MainAnalysisScreen::buildStatusStrip(OverlayHost& host) {
    if (!status_) return;
    strip_ = std::make_shared<StatusStripState>();
    auto& st = *strip_;
    // UNE SESSION REJOUEE gare la souris dans la barre d'etat avant ses captures
    // ("survol 960 1070", "survol 1200 1060") : les pastilles y restent muettes
    // (ni infobulle ni surbrillance), comme l'ancienne barre.
    st.quiet = app_.scripted();
    const auto tip = [&st](StatusChip* c, std::string text) { if (!st.quiet) c->setTooltip(std::move(text)); };

    auto menu = std::make_unique<PopupMenu>("analysis.statusMenu");
    st.menu = menu.get();
    host.addOverlay(std::move(menu));
    paneLinks_ += st.menu->itemChosen->connect([this](int id) { runStatusMenu(id); });
    paneLinks_ += st.menu->dismissed->connect([this] { if (strip_) strip_->menuKind = 0; });

    auto strip = std::make_unique<StatusStrip>("analysis.statusStrip");
    auto* s = strip.get();
    st.strip = s;
    const auto chip = [s](const char* id, int priority) -> StatusChip& {
        return s->add(std::make_unique<StatusChip>(id), priority);
    };
    st.when = &chip("analysis.status.journal", 90);
    st.when->setCaret(true);
    tip(st.when, "Le journal des messages : les 50 derniers, avec leur heure (un clic sur le message aussi)");
    st.when->setOnClick([this] { openMessageJournal(!messageJournalOpen()); });
    st.selection = &chip("analysis.status.selection", 30);
    st.selection->setShown(false);
    st.selection->setElastic(true);
    st.errors = &chip("analysis.status.errors", 80);
    st.errors->setIcon(Icon::Error);
    st.errors->setTone(Tone::Error);
    st.errors->setShown(false);
    st.errors->setOnClick([this] { openStatusCounts(); });
    st.warnings = &chip("analysis.status.warnings", 78);
    st.warnings->setIcon(Icon::Warning);
    st.warnings->setTone(Tone::Warning);
    st.warnings->setShown(false);
    st.warnings->setOnClick([this] {
        if (strip_ && strip_->apiPoints > 0) openApiPane("api"); else openHmiPane("compiler");
    });
    st.sim = &chip("analysis.status.simulation", 70);
    tip(st.sim, "La simulation : un clic ouvre Simulation \xE2\x80\xBA Vue d'ensemble");
    st.sim->setOnClick([this] { openSimCenter("ensemble"); });
    st.target = &chip("analysis.status.target", 40);
    st.target->setIcon(core::hasApi() ? Icon::Cpu : Icon::Screen);
    // 1.12.0 : IHM Studio - l'ecran de l'IHM (un clic : IHM > Configuration).
    tip(st.target, core::hasApi() ? "La cible : l'automate et la t\xC3\xA2" "che MAST (un clic : Configuration)"
                                  : "L'\xC3\xA9" "cran de l'IHM : sa taille (un clic : IHM \xE2\x80\xBA Configuration)");
    st.target->setOnClick([this] { if (core::hasApi()) openApiPane("configuration"); else openHmiPane("config"); });
    st.zoom = &chip("analysis.status.zoom", 50);
    st.zoom->setOnClick([this] { openStatusMenu(1); });
    // 1.12.30 : - et + de part et d'autre du pourcentage ; Ctrl + molette sur un tableau,
    // un arbre, une liste : la page entiere.
    st.zoom->setStepper([this](int step) { (void)stepTabZoom(step); });
    Widget::setZoomRequestHandler([this](Widget& w, int step) { zoomRequested(w, step); });
    st.theme = &chip("analysis.status.theme", 45);
    st.theme->setIcon(Icon::Layers);
    tip(st.theme, "Le th\xC3\xA8me : les 6 derniers utilis\xC3\xA9s, et tous les th\xC3\xA8mes");
    st.theme->setOnClick([this] { openStatusMenu(2); });
    st.save = &chip("analysis.status.save", 85);
    st.save->setOnClick([this] {
        if (app_.project() && app_.commands().isModified()) (void)app_.actions().trigger("project.save", app_.commands());
    });
    st.keys = &chip("analysis.status.keys", 10);
    st.keys->setMuted(true);
    if (st.quiet)
        for (auto* c : {st.when, st.selection, st.errors, st.warnings, st.sim, st.target, st.zoom, st.theme, st.save, st.keys})
            c->setQuiet(true);
    status_->addIndicator(std::move(strip), StatusBar::Slot::Right);

    // Les themes recents : retenus d'une seance a l'autre.
    {
        std::string list = app_.settings().getString("view.recentThemes");
        std::size_t at = 0;
        while (at <= list.size() && st.recentThemes.size() < 6) {
            const auto comma = list.find(',', at);
            auto key = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            if (!key.empty()) st.recentThemes.push_back(std::move(key));
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }

    // LE POINT DE PASSAGE UNIQUE : tout ce que l'appli ecrit dans la barre.
    status_->setMessageHook([this](const std::string& text, StatusBar::Severity severity, bool transient) {
        if (!strip_) return;
        std::string where;
        if (centre_ && centre_->tabCount() > 0)
            if (const auto* t = centre_->tab(centre_->currentIndex())) where = t->title;
        // L'horloge de l'appli (celle d'une session rejouee avance de 1/30 s par image).
        strip_->journal.push(text, severity, transient, std::move(where), frameClock_,
                             wallClock(core::CommandStack::wallMs()));
    });
    status_->setMessageClick([this] { openMessageJournal(!messageJournalOpen()); });
}

// ------------------------------------------------------------------- tick ---
void MainAnalysisScreen::tickStatusStrip(double now) {
    if (!strip_ || !status_) return;
    auto& st = *strip_;
    const auto project = app_.project();
    const auto tip = [&st](StatusChip* c, std::string text) { if (!st.quiet) c->setTooltip(std::move(text)); };

    // Le dernier message : son heure et le chevron du journal.
    if (!st.journal.entries().empty()) st.when->setText(st.journal.entries().front().time.substr(0, 5));
    else st.when->setText("Journal");

    // La selection de l'onglet ouvert : ce que son volet a declare
    // (setStatusSelection), sinon ce que l'ecran sait en lire (API > Variables,
    // une section, IHM > Vues). Recalculee quand la selection ou le projet change.
    {
        std::string sel;
        Widget* page = centre_ && centre_->tabCount() > 0 ? centre_->page(centre_->currentIndex()) : nullptr;
        if (page) {
            if (auto it = st.selections.find(page); it != st.selections.end() && it->second) sel = it->second();
            else {
                std::string key;
                const auto revision = app_.commands().revision();
                if (auto* vars = dynamic_cast<VariablesPane*>(page->findById("analysis.api.variables.volet")))
                    key = "v:" + vars->selectedPath();
                else if (auto* views = dynamic_cast<HmiViewsPane*>(page->findById("hmi.views")))
                    key = "h:" + std::to_string(static_cast<std::uint64_t>(views->selectedView()));
                else if (auto* hv = visibleUnder<HmiVariablesPane>(*page))
                    key = "i:" + std::to_string(static_cast<std::uint64_t>(hv->selectedVariable())) + ":" + hv->selectedPath();
                else
                    for (const auto& d : documents_)
                        if (d.tab < centre_->tabCount() && centre_->page(d.tab) == page) { key = "s:" + std::to_string(d.section); break; }
                if (key != st.selKey || revision != st.selRevision) {
                    st.selKey = key;
                    st.selRevision = revision;
                    st.selText = describeStatusSelection(key);
                }
                sel = st.selText;
            }
        }
        st.selection->setText(clip(sel, 120));
        st.selection->setShown(!sel.empty());
        tip(st.selection, sel);
    }

    // Les compteurs et les raccourcis (cinq fois par seconde : ils parcourent les onglets).
    const bool slow = now - st.lastCount >= 0.2 || now < st.lastCount;
    if (slow) {
        st.lastCount = now;
        if (auto* w = root().findById("hmi.compile")) {
            if (const auto* report = dynamic_cast<const HmiReportPane*>(w)) {
                std::size_t e = 0, wn = 0;
                for (const auto& i : report->issues()) {
                    if (i.severity == hmi::Issue::Severity::Error) ++e;
                    else if (i.severity == hmi::Issue::Severity::Warning) ++wn;
                }
                st.hmiErrors = e;
                st.hmiWarnings = wn;
            }
        }
        if (!project) { st.hmiErrors = st.hmiWarnings = 0; }
        std::size_t points = 0;
        std::string detail;
        if (const auto* dash = dynamic_cast<const ApiDashboard*>(root().findById("analysis.api.tableau")); dash && project) {
            const auto& s = dash->summary();
            if (!s.outdated.empty()) { ++points; detail += "\n\xC2\xB7 " + std::to_string(s.outdated.size()) + " \xC3\xA9l\xC3\xA9ment(s) plus r\xC3\xA9" "cent(s) en biblioth\xC3\xA8que"; }
            if (!s.unused.empty()) { ++points; detail += "\n\xC2\xB7 " + std::to_string(s.unused.size()) + " variable(s) qui ne servent pas"; }
            if (!s.late.empty()) { ++points; detail += "\n\xC2\xB7 " + std::to_string(s.late.size()) + " lecture(s) avant l'\xC3\xA9" "criture"; }
        }
        st.apiPoints = points;
        st.apiDetail = detail;
    }
    const std::size_t errors = st.hmiErrors;
    const std::size_t warnings = st.hmiWarnings + st.apiPoints;
    st.errors->setText(std::to_string(errors) + (errors > 1 ? " erreurs" : " erreur"));
    st.errors->setShown(errors > 0);
    tip(st.errors, "Le dernier Compiler de l'IHM : " + std::to_string(st.hmiErrors) + " erreur(s). Un clic ouvre IHM \xE2\x80\xBA Compiler.");
    st.warnings->setText(std::to_string(warnings) + (warnings > 1 ? " avertissements" : " avertissement"));
    st.warnings->setShown(warnings > 0);
    // 1.12.0 : chaque application, ses avertissements.
    tip(st.warnings, !core::hasApi() ? "IHM (dernier Compiler) : " + std::to_string(st.hmiWarnings) + " avertissement(s)"
                     : !core::hasIhm() ? "API (\xC3\x80 regarder) : " + std::to_string(st.apiPoints) + " point(s)" + st.apiDetail
                                       : "IHM (dernier Compiler) : " + std::to_string(st.hmiWarnings) + " avertissement(s)\nAPI (\xC3\x80 regarder) : "
                                             + std::to_string(st.apiPoints) + " point(s)" + st.apiDetail);

    // La simulation.
    if (!core::hasApi()) {
        // 1.12.0 : IHM Studio - la simulation de l'IHM (pas d'automate).
        const auto tab = hmiTabs_.find("simulation");
        const auto* hsim = dynamic_cast<const HmiSimulationPane*>(tab != hmiTabs_.end() ? tab->second : nullptr);
        const bool running = hsim && hsim->hmiRunning();
        const bool paused = hsim && hsim->hmiDebugPaused();   // 1.13.0 : un script arrete (le debogueur)
        st.sim->setShown(project != nullptr);
        if (project) {
            st.sim->setText(paused ? "IHM en pause (d\xC3\xA9" "bogage)" : running ? "IHM en marche" : "IHM arr\xC3\xAAt\xC3\xA9" "e");
            st.sim->setDot(true, running && !paused ? Tone::Ok : Tone::Warning);
        }
    } else {
        using State = SimulationHost::State;
        const auto& sim = app_.simulation();
        std::string text;
        Tone tone = Tone::Muted;
        if (!project) {
            st.sim->setShown(false);
        } else if (!sim.attached()) {
            text = "arr\xC3\xAAt\xC3\xA9" "e";
            tone = Tone::Warning;
        } else {
            const auto state = sim.state();
            text = state == State::Running ? "en marche" : state == State::Paused ? "en pause"
                 : state == State::Halted ? "halte" : "arr\xC3\xAAt\xC3\xA9" "e";
            tone = state == State::Running ? Tone::Ok : state == State::Paused ? Tone::Info
                 : state == State::Halted ? Tone::Error : Tone::Warning;
            text += " \xC2\xB7 cycle " + groupedCount(sim.scanCount());
            if (state == State::Running || state == State::Paused) {
                const double ms = static_cast<double>(sim.lastScanMicros()) / 1000.0;
                char buf[32];
                if (ms < 10.0) std::snprintf(buf, sizeof buf, "%.1f ms", ms);
                else std::snprintf(buf, sizeof buf, "%.0f ms", ms);
                std::string m = buf;
                if (const auto dot = m.find('.'); dot != std::string::npos) m[dot] = ',';
                text += " \xC2\xB7 " + m;
            }
        }
        if (project) {
            st.sim->setShown(true);
            st.sim->setText(text);
            st.sim->setDot(true, tone);
        }
    }

    // La cible : l'automate, la periode de MAST. 1.12.0 : IHM Studio - l'ecran de l'IHM.
    if (!core::hasApi()) {
        const auto doc = app_.hmi();
        st.target->setShown(doc != nullptr);
        if (doc) st.target->setText("\xC3\x89" "cran " + std::to_string(doc->project.config.width) + " \xC3\x97 " + std::to_string(doc->project.config.height));
    } else if (project) {
        std::string t = project->hardware.cpuReference.empty() ? std::string("Automate") : project->hardware.cpuReference;
        for (const auto& task : project->tasks)
            if (project->strings.text(task.name) == "MAST") {
                t += " \xC2\xB7 MAST";
                if (task.period > 0) t += " " + std::to_string(task.period) + " ms";
                else t += " cyclique";
                break;
            }
        st.target->setText(t);
        st.target->setShown(true);
    } else {
        st.target->setShown(false);
    }

    // Le zoom de la page ouverte (1.12.30 : toutes les pages ; - et + de part et d'autre).
    {
        // Les widgets Scaled nes depuis (un volet refait, un onglet de la page montre)
        // prennent le zoom de la page, cinq fois par seconde.
        if (now - st.lastZoomSync >= 0.2 || now < st.lastZoomSync) {
            st.lastZoomSync = now;
            syncPageZoom();
        }
        const int pct = tabZoomPercent();
        st.zoom->setText(pct > 0 ? std::to_string(pct) + " %" : "100 %");
        st.zoom->setMuted(pct == 0);
        std::string what = "la page";
        if (Widget* page = centre_ && centre_->tabCount() > 0 ? centre_->page(centre_->currentIndex()) : nullptr)
            what = zoomTargetName(pageZoomTarget(*page, st.activeZoomable));
        // 1.12.31 : celui de la page et celui d'un code se gardent d'une seance a l'autre.
        const bool kept = what == "la page" || what == "le texte";
        tip(st.zoom, pct > 0 ? "Le zoom de " + what + " : \xE2\x88\x92 et + d'un cran, un clic sur le nombre pour 50 \xC3\xA0 200 %."
                                   "\nCtrl + molette, Ctrl + plus, Ctrl + moins, Ctrl + 0 (100 %) font de m\xC3\xAAme."
                                   + std::string(kept ? "\nIl est gard\xC3\xA9 d'une s\xC3\xA9" "ance \xC3\xA0 l'autre ; 100 % l'oublie." : "")
                             : "Cette page n'a pas de zoom");
        st.zoom->setShown(centre_ && centre_->tabCount() > 0);
    }

    // Le theme : retenir les derniers utilises.
    {
        const auto& name = app_.theme().name;
        if (name != st.lastTheme) {
            st.lastTheme = name;
            auto& r = st.recentThemes;
            r.erase(std::remove(r.begin(), r.end(), name), r.end());
            r.insert(r.begin(), name);
            if (r.size() > 6) r.resize(6);
            std::string list;
            for (const auto& k : r) list += (list.empty() ? "" : ",") + k;
            app_.settings().set("view.recentThemes", list);
        }
        st.theme->setText("Th\xC3\xA8me " + Theme::labelOf(name));
    }

    // L'enregistrement.
    if (project) {
        const bool modified = app_.commands().isModified();
        if (modified) {
            st.save->setText("Modifi\xC3\xA9 :");
            st.save->setKey("Ctrl+S");
            st.save->setTone(Tone::Warning);
            st.save->setIcon(Icon::Save);
            tip(st.save, "Des modifications ne sont pas enregistr\xC3\xA9" "es : un clic (ou Ctrl+S) enregistre");
        } else {
            const auto saved = app_.savedAtMs();
            std::string t = "Enregistr\xC3\xA9";
            if (saved > 0) {
                const auto mins = (core::CommandStack::wallMs() - saved) / 60000;
                t += mins < 1 ? std::string(" \xC3\xA0 l'instant") : " il y a " + std::to_string(mins) + " min";
            }
            st.save->setText(t);
            st.save->setKey({});
            st.save->setTone(Tone::None);
            st.save->setIcon(Icon::Ok);
            tip(st.save, "Tout est enregistr\xC3\xA9");
        }
        st.save->setShown(true);
    } else {
        st.save->setShown(false);
    }

    // Les raccourcis du moment, d'apres ce qui a le clavier.
    if (slow) {
        const Widget* f = focusedUnder(root());
        std::string k;
        if (f && dynamic_cast<const TreeView*>(f)) k = "F2 renommer \xC2\xB7 Suppr supprimer \xC2\xB7 Ctrl+F chercher";
        else if (f && dynamic_cast<const TableView*>(f)) k = "Ctrl+F chercher \xC2\xB7 Ctrl+C copier \xC2\xB7 F2 renommer";
        else if (const auto* code = dynamic_cast<const MultiLineText*>(f); code && code->commandKeys()) {
            // 1.12.2 : les raccourcis de Visual Studio (ou ceux de la 1.12.1, profil classique).
            k = ui::keymap::current() == ui::keymap::Profile::VisualStudio
                    ? "Ctrl+K, Ctrl+C commenter \xC2\xB7 Ctrl+D dupliquer \xC2\xB7 F12 d\xC3\xA9" "finition \xC2\xB7 F8 faute \xC2\xB7 Ctrl+T aller \xC3\xA0"
                    : "Ctrl+F chercher \xC2\xB7 Ctrl+D dupliquer \xC2\xB7 Ctrl+G ligne \xC2\xB7 Ctrl+K aller \xC3\xA0";
        }
        else if (f && dynamic_cast<const MultiLineText*>(f)) k = "Ctrl+Z annuler \xC2\xB7 Ctrl+molette zoom";
        else if (f && dynamic_cast<const InputText*>(f)) k = "Entr\xC3\xA9" "e valider \xC2\xB7 \xC3\x89" "chap annuler";
        else k = "Ctrl+K aller \xC3\xA0 \xC2\xB7 F9 simulation \xC2\xB7 Ctrl+S enregistrer";
        st.keys->setText(k);
    }
}

// ---------------------------------------------------------------- journal ---
void MainAnalysisScreen::openMessageJournal(bool open) {
    if (!strip_ || !strip_->menu) return;
    auto& st = *strip_;
    if (!open) {
        if (st.menuKind == 1 && st.menu->isOpen()) st.menu->close();
        st.menuKind = 0;
        return;
    }
    std::string current;
    if (centre_ && centre_->tabCount() > 0)
        if (const auto* t = centre_->tab(centre_->currentIndex())) current = t->title;
    const auto& entries = st.journal.entries();
    std::vector<PopupMenu::Item> items;
    PopupMenu::Item head;
    head.heading = true;
    head.label = "Journal des messages";
    head.shortcut = std::to_string(entries.size()) + " sur " + std::to_string(MessageJournal::kMax);
    items.push_back(head);
    // Copier et Vider en tete (la maquette) : toujours a portee, meme long.
    PopupMenu::Item copy;
    copy.label = "Copier le journal";
    copy.icon = Icon::Document;
    copy.id = kJournalCopy;
    copy.enabled = !entries.empty();
    copy.disabledReason = "rien \xC3\xA0 copier";
    items.push_back(copy);
    PopupMenu::Item clear = copy;
    clear.label = "Vider le journal";
    clear.icon = Icon::Close;
    clear.id = kJournalClear;
    clear.disabledReason = "d\xC3\xA9j\xC3\xA0 vide";
    items.push_back(clear);
    PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    // Le menu ne defile pas : autant de lignes que la hauteur au-dessus du bandeau en tient.
    const auto surface = root().bounds();
    const auto bar = status_->bounds();
    const float rowH = std::max(16.f, app_.theme().metric.rowHeight);
    const auto fit = static_cast<std::size_t>(std::max(4.f, (bar.y - 40.f) / rowH - 6.f));
    const std::size_t shown = entries.size() <= fit ? entries.size() : fit - 1;
    st.journalTargets.clear();
    int row = 0;
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& e = entries[i];
        PopupMenu::Item it;
        it.label = clip(e.text, 110);
        it.icon = iconOf(e.severity);
        const bool go = !e.target.empty() && e.target != current;
        it.shortcut = e.time + (go ? " \xC2\xB7 Aller \xC3\xA0" : "");
        it.id = kJournalFirst + row++;
        st.journalTargets.push_back(go ? e.target : std::string{});
        items.push_back(std::move(it));
    }
    PopupMenu::Item foot;
    foot.heading = true;
    if (entries.empty()) foot.label = "Aucun message pour l'instant.";
    else if (shown < entries.size())
        foot.label = "\xE2\x80\xA6 et " + std::to_string(entries.size() - shown) + " plus anciens : Copier les donne tous.";
    else foot.label = "Les 50 derniers messages, les plus r\xC3\xA9" "cents en haut : plus rien ne se perd.";
    items.push_back(foot);

    st.menu->setItems(std::move(items));
    st.menu->openAt({bar.x + 6.f, bar.y}, {surface.w, surface.h});
    st.menuKind = 1;
}

bool MainAnalysisScreen::messageJournalOpen() const {
    return strip_ && strip_->menu && strip_->menuKind == 1 && strip_->menu->isOpen();
}

std::vector<std::string> MainAnalysisScreen::messageJournalLines() const {
    std::vector<std::string> out;
    if (!strip_) return out;
    for (const auto& e : strip_->journal.entries()) {
        std::string l = e.time;
        const auto word = severityWord(e.severity);
        if (!word.empty()) { l += " ["; l += word; l += "]"; }
        l += " " + e.text;
        if (!e.target.empty()) l += "  (" + e.target + ")";
        out.push_back(std::move(l));
    }
    return out;
}

std::string MainAnalysisScreen::statusStripText() const {
    std::string out;
    if (!status_) return out;
    out = "message \"" + status_->message() + "\"";
    if (!strip_ || !strip_->strip) return out;
    for (const auto* c : strip_->strip->visibleChips()) out += " | " + c->text();
    return out;
}

void MainAnalysisScreen::setStatusSelection(const Widget* page, std::function<std::string()> describe) {
    if (!strip_ || !page) return;
    if (describe) strip_->selections[page] = std::move(describe);
    else strip_->selections.erase(page);
}

bool MainAnalysisScreen::clickStatusChip(const std::string& which) {
    if (!strip_) return false;
    auto& st = *strip_;
    StatusChip* c = which == "journal" ? st.when : which == "selection" ? st.selection
                  : which == "erreurs" ? st.errors : which == "avertissements" ? st.warnings
                  : which == "simulation" ? st.sim : which == "cible" ? st.target
                  : which == "zoom" ? st.zoom : (which == "theme" || which == "th\xC3\xA8me") ? st.theme
                  : which == "enregistrement" ? st.save : nullptr;
    if (!c || !c->shown()) return false;
    // Le zoom sans zoom dans l'onglet : le menu s'ouvre quand meme, ses entrees grisees disent pourquoi.
    if (c == st.zoom) { openStatusMenu(1); return true; }
    c->click();
    return true;
}

void MainAnalysisScreen::openStatusCounts() {
    if (!strip_) return;
    if (strip_->hmiErrors > 0 || strip_->hmiWarnings > 0 || strip_->apiPoints == 0) openHmiPane("compiler");
    else openApiPane("api");
}

// ------------------------------------------------------------ zoom, theme ---
// 1.12.30 : la page d'un widget (un onglet du centre, ou detache) ; nul : hors des pages
// (l'arbre du projet, le panneau du bas, un dialogue).
Widget* MainAnalysisScreen::pageOfWidget(const Widget* w) const {
    for (const Widget* a = w; a; a = a->parent()) {
        if (centre_ && centre_->indexOf(a) >= 0) return const_cast<Widget*>(a);
        if (a->parent() && a->parent()->id() == "detached.root") return const_cast<Widget*>(a);
    }
    return nullptr;
}

float MainAnalysisScreen::pageZoomOf(const Widget* page) const {
    if (!strip_ || !page) return 1.f;
    const auto it = strip_->pageZoom.find(page);
    return it == strip_->pageZoom.end() ? 1.f : it->second;
}

// ---- 1.12.31 : le zoom garde d'une seance a l'autre (app/ZoomMemory.hpp) ----
// Les bornes : celles du widget (un code va jusqu'a 300 %), dans celles d'une page au moins.
float MainAnalysisScreen::rememberedZoom(const Widget& w) const {
    return zoommemory::read(app_.zoomSettings(), w.id(), std::min(zoommemory::kMin, w.minZoom()), std::max(zoommemory::kMax, w.maxZoom()));
}

void MainAnalysisScreen::rememberZoom(const Widget& w, float zoom) {
    if (zoommemory::write(app_.zoomSettings(), w.id(), zoom, std::min(zoommemory::kMin, w.minZoom()), std::max(zoommemory::kMax, w.maxZoom())))
        app_.zoomSettingsChanged();
}

void MainAnalysisScreen::applyPageZoom(Widget& page, float zoom) {
    // Le widget du haut porte le zoom de toute la page ; ses widgets Scaled restent a 100 %
    // dedans (un zoom de plus les agrandirait deux fois). Une page qui est un document : rien.
    ensurePageZoomable(page);
    if (page.zoomSupport() == ZoomSupport::Scaled && std::fabs(page.zoom() - zoom) > 1e-4f) page.setZoom(zoom);
}

void MainAnalysisScreen::setPageZoom(Widget& page, float zoom) {
    if (!strip_) return;
    const float z = std::clamp(zoom, zoommemory::kMin, zoommemory::kMax);
    if (std::fabs(z - 1.f) < 1e-4f) strip_->pageZoom.erase(&page);
    else strip_->pageZoom[&page] = z;
    applyPageZoom(page, z);
    rememberZoom(page, z);
}

void MainAnalysisScreen::sayPanelZoom(const Widget& w) {
    if (!status_) return;
    const bool tree = treeColumn_ && &w == treeColumn_;
    const bool bottom = bottomPanel_ && &w == static_cast<const Widget*>(bottomPanel_);
    const std::string what = tree ? "l'arbre du projet" : bottom ? "le panneau du bas" : "ce panneau";
    status_->setTransientMessage("Zoom de " + what + " : " + std::to_string(ui::zoomPercent(w.zoom())) + " %"
                                     + (std::fabs(w.zoom() - 1.f) < 1e-4f ? std::string{} : std::string(" (Ctrl+0 : 100 %)")),
                                 3.0);
}

void MainAnalysisScreen::syncPageZoom() {
    if (!strip_ || !centre_) return;
    // Les pages fermees s'oublient (une page detachee dans sa fenetre garde son zoom) ;
    // une page ouverte reprend le sien. 1.12.31 : une page vue pour la premiere fois
    // reprend celui qui est retenu (son identifiant), et ses codes le leur.
    auto& detached = app_.detachedWindows();
    const auto isOpen = [this, &detached](const Widget* p) { return centre_->indexOf(p) >= 0 || detached.isDetached(p); };
    for (auto it = strip_->pageZoom.begin(); it != strip_->pageZoom.end();) {
        if (!isOpen(it->first)) it = strip_->pageZoom.erase(it);
        else ++it;
    }
    for (auto it = strip_->seenPages.begin(); it != strip_->seenPages.end();) {
        if (!isOpen(*it)) it = strip_->seenPages.erase(it);
        else ++it;
    }
    std::set<const Widget*> codesNow;
    const auto apply = [this, &codesNow](Widget* page) {
        if (!page) return;
        const bool first = strip_->seenPages.insert(page).second;
        if (first) {
            const float z = rememberedZoom(*page);
            if (std::fabs(z - 1.f) > 1e-4f) strip_->pageZoom[page] = z;
        }
        applyPageZoom(*page, pageZoomOf(page));
        // Les codes de la page : le zoom retenu a leur premiere vue, puis chaque changement retenu.
        std::vector<MultiLineText*> codes;
        codesUnder(*page, codes);
        for (auto* code : codes) {
            codesNow.insert(code);
            const auto it = strip_->codeZoom.find(code);
            if (it == strip_->codeZoom.end()) {
                const float z = rememberedZoom(*code);
                if (std::fabs(z - 1.f) > 1e-4f) code->setZoom(z);
                strip_->codeZoom[code] = code->zoom();
            } else if (std::fabs(it->second - code->zoom()) > 1e-4f) {
                it->second = code->zoom();
                rememberZoom(*code, code->zoom());
            }
        }
    };
    for (std::size_t i = 0; i < centre_->tabCount(); ++i) apply(centre_->page(i));
    for (const auto* page : detached.pages()) apply(const_cast<Widget*>(page));
    for (auto it = strip_->codeZoom.begin(); it != strip_->codeZoom.end();) {
        if (!codesNow.count(it->first)) it = strip_->codeZoom.erase(it);
        else ++it;
    }
}

void MainAnalysisScreen::noteZoomGesture(gfx::Point at) {
    if (!strip_) return;
    if (auto* z = ui::zoomableAt(root(), at)) strip_->activeZoomable = z;
}

void MainAnalysisScreen::zoomRequested(Widget& w, int step) {
    // Ctrl + molette sur un widget Scaled : la page entiere d'un cran ; hors des pages
    // (l'arbre du projet, le panneau du bas), lui seul.
    if (strip_) strip_->activeZoomable = &w;
    if (Widget* page = pageOfWidget(&w)) {
        const float now = pageZoomOf(page);
        setPageZoom(*page, ui::zoomStep(now, step, zoommemory::kMin, zoommemory::kMax));
        return;
    }
    w.setZoom(ui::zoomStep(w.zoom(), step, w.minZoom(), w.maxZoom()));
    // 1.12.31 : l'arbre du projet, le panneau du bas - gardes d'une seance a l'autre.
    if (&w == treeColumn_ || &w == static_cast<const Widget*>(bottomPanel_)) rememberZoom(w, w.zoom());
    sayPanelZoom(w);
}

int MainAnalysisScreen::tabZoomPercent() const {
    if (!centre_ || centre_->tabCount() == 0) return 0;
    auto* page = centre_->page(centre_->currentIndex());
    if (!page) return 0;
    const auto t = pageZoomTarget(*page, strip_ ? strip_->activeZoomable : nullptr);
    if (t.native) return ui::zoomPercent(t.native->zoom());
    if (!t.scaled.empty()) return ui::zoomPercent(pageZoomOf(page));
    return 0;
}

bool MainAnalysisScreen::setTabZoomPercent(int percent) {
    if (!centre_ || centre_->tabCount() == 0 || percent < 5 || percent > 1600) return false;
    auto* page = centre_->page(centre_->currentIndex());
    if (!page) return false;
    ensurePageZoomable(*page);
    const auto t = pageZoomTarget(*page, strip_ ? strip_->activeZoomable : nullptr);
    const float z = static_cast<float>(percent) / 100.f;
    if (t.native) { t.native->setZoom(z); return true; }
    if (t.scaled.empty()) return false;
    setPageZoom(*page, z);
    return true;
}

void MainAnalysisScreen::reloadRememberedZoom() {
    if (treeColumn_) treeColumn_->setZoom(rememberedZoom(*treeColumn_));
    if (bottomPanel_) bottomPanel_->setZoom(rememberedZoom(*bottomPanel_));
    if (!strip_) return;
    strip_->seenPages.clear();
    strip_->pageZoom.clear();
    strip_->codeZoom.clear();
    syncPageZoom();
}

bool MainAnalysisScreen::setOpenPageZoomPercent(int percent) {
    if (!centre_ || centre_->tabCount() == 0 || percent < 50 || percent > 200) return false;
    auto* page = centre_->page(centre_->currentIndex());
    if (!page || page->zoomSupport() == ZoomSupport::Native) return false;
    if (strip_) strip_->activeZoomable = page;      // la puce suit la page
    setPageZoom(*page, static_cast<float>(percent) / 100.f);
    return true;
}

bool MainAnalysisScreen::stepTabZoom(int step) {
    if (!centre_ || centre_->tabCount() == 0) return false;
    auto* page = centre_->page(centre_->currentIndex());
    if (!page) return false;
    ensurePageZoomable(*page);
    const auto t = pageZoomTarget(*page, strip_ ? strip_->activeZoomable : nullptr);
    if (t.native) {
        auto* n = t.native;
        n->setZoom(step == 0 ? 1.f : ui::zoomStep(n->zoom(), step, n->minZoom(), n->maxZoom()));
        return true;
    }
    if (t.scaled.empty()) return false;
    setPageZoom(*page, step == 0 ? 1.f : ui::zoomStep(pageZoomOf(page), step, zoommemory::kMin, zoommemory::kMax));
    return true;
}

namespace {
// Les plus grands morceaux d'une page qui ne se zooment pas : un widget montre qui ne
// contient aucun widget zoomable (s'il en contient un, ses enfants sont regardes).
bool holdsZoomable(const Widget& w) {
    if (w.zoomable()) return true;
    for (const auto& c : w.children())
        if (c->visible() && holdsZoomable(*c)) return true;
    return false;
}
void plainBlocks(const Widget& w, std::vector<const Widget*>& out) {
    if (!w.visible() || w.zoomable()) return;
    if (!holdsZoomable(w)) { out.push_back(&w); return; }
    for (const auto& c : w.children()) plainBlocks(*c, out);
}
float coveredArea(const Widget& w) {
    if (!w.visible()) return 0.f;
    if (w.zoomable()) return areaOf(&w);
    float a = 0.f;
    for (const auto& c : w.children()) a += coveredArea(*c);
    return a;
}
} // namespace

std::vector<std::string> MainAnalysisScreen::zoomReport() const {
    std::vector<std::string> out;
    char buf[200];
    // 1.12.31 : l'arbre du projet et le panneau du bas, chacun son zoom.
    if (treeColumn_) {
        std::snprintf(buf, sizeof buf, "arbre du projet : %d %%", ui::zoomPercent(treeColumn_->zoom()));
        out.push_back(buf);
    }
    if (bottomPanel_) {
        std::snprintf(buf, sizeof buf, "panneau du bas : %d %%%s", ui::zoomPercent(bottomPanel_->zoom()),
                      bottomPanel_->visible() ? "" : " (cach\xC3\xA9)");
        out.push_back(buf);
    }
    if (!centre_ || centre_->tabCount() == 0) { out.push_back("aucune page ouverte"); return out; }
    Widget* page = centre_->page(centre_->currentIndex());
    const auto* tab = centre_->tab(centre_->currentIndex());
    if (!page) return out;
    const auto t = pageZoomTarget(*page, strip_ ? strip_->activeZoomable : nullptr);
    const int pct = tabZoomPercent();
    std::string head = (tab ? tab->title : std::string("?")) + " : ";
    if (t.native) head += "document " + std::string(typeid(*t.native).name()) + " \"" + t.native->id() + "\" " + std::to_string(pct) + " %";
    else if (!t.scaled.empty()) head += "la page, " + std::to_string(t.scaled.size()) + " widget(s) zoom\xC3\xA9(s) " + std::to_string(pct) + " %";
    else head += "PAS DE ZOOM";
    out.push_back(head);
    std::snprintf(buf, sizeof buf, "page \"%s\" : %s, zoom %d %%", page->id().c_str(),
                  page->zoomSupport() == ZoomSupport::Scaled ? "zoom\xC3\xA9" "e enti\xC3\xA8re"
                  : page->zoomSupport() == ZoomSupport::Native ? "un document" : "sans zoom",
                  ui::zoomPercent(pageZoomOf(page)));
    out.push_back(buf);
    // Les documents de la page : chacun son zoom (un ilot quand la page est zoomee).
    for (const auto* n : ui::nativesUnder(*page)) {
        std::snprintf(buf, sizeof buf, "document a part : %s \"%s\" %d %%%s", typeid(*n).name(), n->id().c_str(),
                      ui::zoomPercent(n->zoom()), n->zoomIsland() ? " (ilot)" : "");
        out.push_back(buf);
    }
    const float pageArea = std::max(1.f, areaOf(page));
    std::snprintf(buf, sizeof buf, "couverture : %.0f %% de la page", std::min(100.f, 100.f * coveredArea(*page) / pageArea));
    out.push_back(buf);
    // 1.12.31 : une page zoomee entiere (son widget du haut Scaled) couvre tous ses morceaux.
    std::vector<const Widget*> blocks;
    if (page->zoomSupport() != ZoomSupport::Scaled)
        for (const auto& c : page->children()) plainBlocks(*c, blocks);
    if (!page->zoomable() && !holdsZoomable(*page)) blocks.assign(1, page);
    std::stable_sort(blocks.begin(), blocks.end(), [](const Widget* a, const Widget* b) { return areaOf(a) > areaOf(b); });
    for (std::size_t i = 0; i < blocks.size() && i < 6; ++i) {
        const float share = 100.f * areaOf(blocks[i]) / pageArea;
        if (share < 1.f) break;
        std::snprintf(buf, sizeof buf, "sans zoom : %s \"%s\" (%.0f %%)", typeid(*blocks[i]).name(), blocks[i]->id().c_str(), share);
        out.push_back(buf);
    }
    return out;
}

bool MainAnalysisScreen::zoomFromKeys(int step) {
    // Le clavier dans un widget zoomable hors des pages (l'arbre du projet, le panneau du
    // bas) : lui ; sinon la page ouverte.
    if (Widget* f = Widget::focusedIn(root())) {
        Widget* scaled = nullptr;
        for (Widget* a = f; a; a = a->parent())
            if (a->zoomSupport() == ZoomSupport::Scaled) scaled = a;
        if (scaled && !pageOfWidget(scaled)) {
            scaled->setZoom(step == 0 ? 1.f : ui::zoomStep(scaled->zoom(), step, scaled->minZoom(), scaled->maxZoom()));
            // 1.12.31 : l'arbre du projet, le panneau du bas - gardes d'une seance a l'autre.
            if (scaled == treeColumn_ || scaled == static_cast<Widget*>(bottomPanel_)) rememberZoom(*scaled, scaled->zoom());
            sayPanelZoom(*scaled);
            return true;
        }
    }
    if (stepTabZoom(step)) return true;
    if (status_) status_->setTransientMessage("Cette page n'a pas de zoom", 3.0);
    return true;
}

bool MainAnalysisScreen::setStatusTheme(const std::string& name) {
    const auto key = Theme::keyOf(name);
    if (key.empty()) return false;
    app_.setTheme(key);
    return true;
}

void MainAnalysisScreen::openStatusMenu(int which) {
    if (!strip_ || !strip_->menu) return;
    auto& st = *strip_;
    std::vector<PopupMenu::Item> items;
    StatusChip* anchor = nullptr;
    if (which == 1) {
        // 1.12.30 : 50 a 200 %, puis les crans et 100 %.
        anchor = st.zoom;
        const int now = tabZoomPercent();
        std::string what = "la page";
        if (Widget* page = centre_ && centre_->tabCount() > 0 ? centre_->page(centre_->currentIndex()) : nullptr)
            what = zoomTargetName(pageZoomTarget(*page, st.activeZoomable));
        PopupMenu::Item head;
        head.heading = true;
        head.label = "Zoom de " + what;
        head.shortcut = now > 0 ? std::to_string(now) + " %" : std::string{};
        items.push_back(head);
        for (const int p : {50, 75, 100, 125, 150, 175, 200}) {
            PopupMenu::Item it;
            it.label = std::to_string(p) + " %";
            it.radio = true;
            it.checked = p == now;
            it.id = kZoomFirst + p / 5;
            it.enabled = now > 0;
            it.disabledReason = "cette page n'a pas de zoom";
            items.push_back(std::move(it));
        }
        PopupMenu::Item sep;
        sep.separator = true;
        items.push_back(sep);
        const auto step = [&](const char* label, const char* key, int id) {
            PopupMenu::Item it;
            it.label = label;
            it.shortcut = key;
            it.id = id;
            it.enabled = now > 0;
            it.disabledReason = "cette page n'a pas de zoom";
            items.push_back(std::move(it));
        };
        step("Zoom avant", "Ctrl++", kZoomIn);
        step("Zoom arri\xC3\xA8re", "Ctrl+\xE2\x88\x92", kZoomOut);
        step("Revenir \xC3\xA0 100 %", "Ctrl+0", kZoomReset);
        st.menuKind = 2;
    } else {
        anchor = st.theme;
        PopupMenu::Item head;
        head.heading = true;
        head.label = "Th\xC3\xA8mes r\xC3\xA9" "cents";
        items.push_back(head);
        int i = 0;
        for (const auto& k : st.recentThemes) {
            PopupMenu::Item it;
            it.label = Theme::labelOf(k);
            it.icon = k == app_.theme().name ? Icon::Ok : Icon::Layers;
            it.id = kThemeFirst + i++;
            items.push_back(std::move(it));
        }
        PopupMenu::Item sep;
        sep.separator = true;
        items.push_back(sep);
        PopupMenu::Item all;
        all.label = "Tous les th\xC3\xA8mes\xE2\x80\xA6";
        all.icon = Icon::Layers;
        all.id = kThemeAll;
        items.push_back(all);
        st.menuKind = 3;
    }
    st.menu->setItems(std::move(items));
    const auto surface = root().bounds();
    const auto r = anchor ? anchor->bounds() : status_->bounds();
    st.menu->openAt({r.x, status_->bounds().y}, {surface.w, surface.h});
}

void MainAnalysisScreen::runStatusMenu(int id) {
    if (!strip_) return;
    auto& st = *strip_;
    st.menuKind = 0;
    if (id == kJournalCopy) {
        const auto n = st.journal.entries().size();
        setClipboardText(st.journal.toText());
        status_->setTransientMessage("Journal des messages copi\xC3\xA9 (" + std::to_string(n) + " ligne" + (n > 1 ? "s" : "") + ")", 4.0,
                                     StatusBar::Severity::Success);
        return;
    }
    if (id == kJournalClear) { st.journal.clear(); return; }
    if (id >= kJournalFirst && id < kJournalFirst + static_cast<int>(MessageJournal::kMax)) {
        const auto row = static_cast<std::size_t>(id - kJournalFirst);
        if (row < st.journalTargets.size() && !st.journalTargets[row].empty() && centre_)
            for (std::size_t i = 0; i < centre_->tabCount(); ++i)
                if (const auto* t = centre_->tab(i); t && t->title == st.journalTargets[row]) { centre_->setCurrentIndex(i); break; }
        return;
    }
    if (id == kZoomIn || id == kZoomOut || id == kZoomReset) { (void)stepTabZoom(id == kZoomIn ? 1 : id == kZoomOut ? -1 : 0); return; }
    if (id >= kZoomFirst && id < kThemeAll) { (void)setTabZoomPercent((id - kZoomFirst) * 5); return; }
    if (id == kThemeAll) { app_.showThemeGallery(); return; }
    if (id >= kThemeFirst) {
        const auto i = static_cast<std::size_t>(id - kThemeFirst);
        if (i < st.recentThemes.size()) app_.setTheme(st.recentThemes[i]);
    }
}

} // namespace app
