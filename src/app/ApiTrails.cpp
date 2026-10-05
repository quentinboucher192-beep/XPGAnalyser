// =============================================================================
//  app/ApiTrails.cpp - lot API 7 : le didacticiel de l'API (voir ApiTrails.hpp)
// -----------------------------------------------------------------------------
//  CE QUE LES ETAPES REGARDENT : l'arbre (les entrees du dossier API), la barre
//  du haut (Simuler, Nouveau, Vers Control Expert...), les onglets de l'API et
//  leurs barres (un bouton par son action), le projet (une variable, une
//  section qui l'ecrit, une ligne de table, la version d'un bloc, l'ordre de
//  MAST), la simulation (en marche, la valeur, le forcage), l'IHM en marche,
//  l'onglet Macros (le formulaire, l'apercu, Appliquer) et la pile d'annulation.
//
//  CE QUE MONTRE-MOI FAIT : ce que ferait l'utilisateur, par les memes
//  commandes (AddVariableCommand, AddSectionCommand, AddAnimationLinesCommand,
//  ReorderSectionCommand...) - un Ctrl+Z les reprend, Tout defaire aussi. Deux
//  exceptions, dites a l'ecran : forcer une variable (la simulation, pas le
//  projet ; Tout defaire la deforce) et mettre un bloc a jour (la macro le fait
//  hors de l'historique ; Montre-moi passe par une commande qui garde l'etat
//  d'avant, et Tout defaire rend l'etat du debut du parcours).
// =============================================================================
#include "ApiTrails.hpp"

#include "AnimationTablesPane.hpp"
#include "ApiPanes.hpp"
#include "App.hpp"
#include "MacrosPane.hpp"
#include "TaskPanes.hpp"
#include "TopBar.hpp"
#include "TypePanes.hpp"
#include "VariablesPane.hpp"
#include "hmi/HmiPanels.hpp"
#include "hmi/HmiSimulation.hpp"
#include "../project/ApiChecks.hpp"
#include "../project/ApiCommands.hpp"
#include "../project/EditCommands.hpp"
#include "../project/SharedLibrary.hpp"
#include "../project/TypeUsage.hpp"
#include "../sim/Runtime.hpp"
#include "TutorialsLot8.hpp"   // Lot API 8 : didacticiels et aide (les parcours du lot 8)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <optional>

namespace app {

using namespace ui;
using NK = ProjectTreeModel::NodeKind;
using Step = HmiTutorial::Step;
using Rect = gfx::Rect;

namespace {

constexpr std::size_t kNpos = static_cast<std::size_t>(-1);

// Les noms de l'exercice << Ma premiere variable suivie >>.
constexpr const char* kCounter = "Compteur_Essais";
constexpr const char* kSection = "Essais";
constexpr const char* kTable = "ESSAI";
constexpr const char* kHmiTable = "IHM_ESSAI";     // << Une variable IHM dans une table >>, sans table

const std::vector<ApiTrailDef> kDefs = {
    {"api-decouvrir", "D\xC3\xA9" "couvrir l'API", "visite", 14, 5, ui::Icon::Search},
    {"api-variable", "Ma premi\xC3\xA8re variable suivie", "interactif", 6, 3, ui::Icon::Variable},
    {"api-ihm-table", "Une variable IHM dans une table", "interactif", 5, 3, ui::Icon::Screen},
    {"api-bloc", "Mettre \xC3\xA0 jour un bloc", "interactif", 5, 3, ui::Icon::FunctionBlock},
    {"api-ordre", "Ranger l'ordre d'ex\xC3\xA9" "cution", "interactif", 4, 2, ui::Icon::Section},
    {"api-macro", "Lancer une macro", "interactif", 5, 3, ui::Icon::Play},
};

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool sameName(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Le texte d'une case, sans ses espaces de tete, correspond-il a ce nom ? (Le
// nom seul, ou suivi de ce qui n'est pas un identifiant : "Prog  . unite, 3
// sections", "Compteur_Essais  CREEE PAR LE PARCOURS".)
bool cellNames(std::string_view cell, std::string_view name) {
    while (!cell.empty() && (cell.front() == ' ' || cell.front() == '\t')) cell.remove_prefix(1);
    if (cell.size() < name.size() || !sameName(cell.substr(0, name.size()), name)) return false;
    return cell.size() == name.size() || !identChar(cell[name.size()]);
}

Rect unite(Rect a, Rect b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const float x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.right(), b.right()) - x, std::max(a.bottom(), b.bottom()) - y};
}

// La ligne a l'ecran d'un tableau (dans ce widget ou ses enfants montres) qui
// nomme `name` dans l'une de ses trois premieres colonnes.
bool findTableRow(ui::Widget* root, std::string_view name, Rect& out) {
    if (!root || !root->visible() || name.empty()) return false;
    std::vector<ui::Widget*> queue{root};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        ui::Widget* w = queue[i];
        if (auto* t = dynamic_cast<ui::TableView*>(w); t && t->model()) {
            const std::size_t cols = std::min<std::size_t>(t->model()->columnCount(), 3);
            for (std::size_t v = 0; v < t->visibleRowCount(); ++v) {
                Rect r{};
                if (!t->rowRect(v, r)) continue;              // hors de la zone visible : pas la peine de lire
                const auto row = t->viewRow(v);
                for (std::size_t c = 0; c < cols; ++c)
                    if (cellNames(t->model()->cellText(row, c), name)) {
                        out = r;
                        return true;
                    }
            }
        }
        for (const auto& c : w->children())
            if (c->visible()) queue.push_back(c.get());
    }
    return false;
}

// ---- le projet --------------------------------------------------------------
std::string text(const domain::Project& p, domain::SymbolId id) { return std::string(p.strings.text(id)); }

domain::Index globalVariable(const domain::Project& p, std::string_view name) {
    for (domain::Index i = 0; i < p.variables.size(); ++i)
        if (p.variables[i].scope == domain::VariableScope::Global && sameName(p.strings.text(p.variables[i].name), name)) return i;
    return domain::kNoIndex;
}

// Le corps affecte-t-il `var` (un "var :=" en debut d'instruction) ? Rend la ligne.
bool assigns(const std::string& body, std::string_view var, std::string* line) {
    const auto low = lower(body);
    const auto want = lower(var);
    for (std::size_t at = low.find(want); at != std::string::npos; at = low.find(want, at + 1)) {
        if (at > 0 && identChar(low[at - 1])) continue;
        std::size_t k = at + want.size();
        if (k < low.size() && identChar(low[k])) continue;
        while (k < low.size() && (low[k] == ' ' || low[k] == '\t')) ++k;
        if (low.compare(k, 2, ":=") != 0) continue;
        if (line) {
            const auto from = body.rfind('\n', at);
            const auto to = body.find('\n', at);
            *line = body.substr(from == std::string::npos ? 0 : from + 1, (to == std::string::npos ? body.size() : to) - (from == std::string::npos ? 0 : from + 1));
            while (!line->empty() && (line->back() == '\r' || line->back() == ' ')) line->pop_back();
            while (!line->empty() && (line->front() == ' ' || line->front() == '\t')) line->erase(0, 1);
        }
        return true;
    }
    return false;
}

bool runsEveryCycle(const domain::Project& p, const domain::Section& s) {
    if (s.isSubroutine) return false;
    if (s.owner < p.pous.size()) {
        const auto kind = p.pous[s.owner].kind;
        return kind != domain::PouKind::FunctionBlockType && kind != domain::PouKind::SubRoutine && kind != domain::PouKind::Function;
    }
    return true;
}

// La section (de tache ou d'unite) qui ecrit `var` ; kNoIndex : aucune.
domain::Index sectionWriting(const domain::Project& p, std::string_view var, std::string* line) {
    for (domain::Index i = 0; i < p.sections.size(); ++i)
        if (runsEveryCycle(p, p.sections[i]) && assigns(p.sections[i].body, var, line)) return i;
    return domain::kNoIndex;
}

domain::Index taskSectionNamed(const domain::Project& p, std::string_view name) {
    for (domain::Index i = 0; i < p.sections.size(); ++i)
        if (p.sections[i].task != 0 && !p.sections[i].isSubroutine && sameName(p.strings.text(p.sections[i].name), name)) return i;
    return domain::kNoIndex;
}

std::string mainTask(const domain::Project& p) { return p.tasks.empty() ? std::string("MAST") : text(p, p.tasks.front().name); }

std::size_t tableNamed(const domain::Project& p, std::string_view name) {
    if (name.empty()) return kNpos;
    for (std::size_t i = 0; i < p.animationTables.size(); ++i)
        if (sameName(p.strings.text(p.animationTables[i].name), name)) return i;
    return kNpos;
}

bool tableHas(const domain::Project& p, std::size_t table, std::string_view name, bool hmi) {
    if (table >= p.animationTables.size()) return false;
    for (const auto& e : p.animationTables[table].entries)
        if (e.hmi == hmi && sameName(p.strings.text(e.name), name)) return true;
    return false;
}

std::size_t tableWith(const domain::Project& p, std::string_view name, bool hmi) {
    for (std::size_t i = 0; i < p.animationTables.size(); ++i)
        if (tableHas(p, i, name, hmi)) return i;
    return kNpos;
}

const domain::Pou* blockNamed(const domain::Project& p, std::string_view name) {
    for (const auto& pou : p.pous)
        if (pou.kind == domain::PouKind::FunctionBlockType && sameName(p.strings.text(pou.name), name)) return &pou;
    return nullptr;
}

std::string blockVersion(const domain::Project& p, std::string_view name) {
    const auto* pou = blockNamed(p, name);
    return pou ? pou->version : std::string{};
}

// "PT1_A, PT1_B, PT3_A..." : les premieres instances.
std::string instanceNames(const std::vector<project::usage::Instance>& list, std::size_t shown) {
    std::string out;
    for (std::size_t i = 0; i < list.size() && i < shown; ++i) out += (out.empty() ? "" : ", ") + list[i].name;
    if (list.size() > shown) out += "\xE2\x80\xA6";
    return out;
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

// Une empreinte courte du projet (FNV-1a) : la cle de sa progression.
std::string projectTag(std::string_view where) {
    if (where.empty()) return "sans-projet";
    std::uint32_t h = 2166136261u;
    for (const char c : lower(where)) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    char b[12];
    std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(h));
    return b;
}

// ---- la mise a jour d'un bloc, en une commande --------------------------------
//  La macro MettreAJourBibliotheque le fait aussi, mais son LibUpdate n'est pas
//  une commande (la macro le dit : Ctrl+Z ne le reprend pas). Montre-moi passe
//  par celle-ci : l'etat d'avant est garde entier, Ctrl+Z (ou Tout defaire) le
//  rend ; Ctrl+Y remet l'etat d'apres. Entier parce que l'import remplace le
//  bloc ET ce qu'il emporte (ses sections, ses variables) : garder moins, ce
//  serait parier sur ce que l'import touche.
class TrailBlockUpdateCommand final : public core::ICommand {
public:
    TrailBlockUpdateCommand(std::shared_ptr<domain::Project> project, std::string root, std::string block, std::string from, std::string to)
        : project_(std::move(project)), root_(std::move(root)), block_(std::move(block)), from_(std::move(from)), to_(std::move(to)) {}

    core::Status execute() override {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "pas de projet");
        if (after_) {
            *project_ = *after_;
            return core::ok();
        }
        auto before = std::make_shared<domain::Project>(*project_);
        project::SharedLibrary library(root_);
        if (auto scanned = library.scan(); !scanned) return scanned;
        auto outcome = library.import(*project_, block_, project::SharedLibrary::OnConflict::Overwrite);
        if (!outcome) {
            *project_ = *before;
            return core::fail(outcome.error().code, outcome.error().context, outcome.error().source);
        }
        before_ = std::move(before);
        after_ = std::make_shared<domain::Project>(*project_);
        return core::ok();
    }
    core::Status undo() override {
        if (!project_ || !before_) return core::ok();
        *project_ = *before_;
        return core::ok();
    }
    [[nodiscard]] std::string label() const override {
        return "Mettre \xC3\xA0 jour " + block_ + " (" + from_ + " \xE2\x86\x92 " + to_ + ") depuis la biblioth\xC3\xA8que";
    }

private:
    std::shared_ptr<domain::Project> project_;
    std::string                      root_, block_, from_, to_;
    std::shared_ptr<domain::Project> before_, after_;
};

// ---- l'etat d'un parcours de l'API ----------------------------------------------
struct ApiRun {
    std::string key;
    // << Une variable IHM dans une table >>
    std::string hmiVar, hmiTable, buttonName, buttonViewName;
    hmi::Id     buttonView{hmi::kNoId};
    bool        signature{false};
    std::string hmiBefore;
    bool        hmiBeforeSet{false};
    // << Mettre a jour un bloc >>
    std::string block, versionBefore, libraryVersion, instanceList;
    std::size_t instances{0};
    std::shared_ptr<domain::Project> snapshot;   // le projet au debut : ce que la macro ne rend pas
    // << Ranger l'ordre d'execution >>
    std::string task, reader, variable, writer;
    std::size_t writerRank{0};
    bool        checked{false};
    std::weak_ptr<core::Signal<int>> watched;    // la barre de l'onglet Ordre qu'on ecoute
    std::uint64_t lateRevision{~std::uint64_t{0}};
    std::vector<project::api::LateRead> late;
    // << Lancer une macro >>
    std::string   macro;
    std::uint64_t serialBefore{0}, applied{0};
    int           macroStepBefore{0};            // l'etape de l'onglet Macros en arrivant sur Appliquer
    bool          nothingApplied{false};         // appliquee sans commande : rien a changer
    core::ConnectionScope links;
};

// L'accueil demande le didacticiel avant qu'un projet soit ouvert.
std::optional<std::string>& pendingTutorial() {
    static std::optional<std::string> pending;
    return pending;
}

// ---- l'onglet des cartes ----------------------------------------------------------
class ApiTrailsPage final : public ui::Widget {
public:
    using Provider = std::function<std::vector<TrailCard>()>;
    ApiTrailsPage(std::string id, Provider provider) : ui::Widget(std::move(id)), provider_(std::move(provider)) {
        auto panel = std::make_unique<ui::ScrollablePanel>(this->id() + ".defile");
        panel->setScrollPolicy(false, true);
        auto cards = std::make_unique<HmiTrailCards>(this->id() + ".cartes");
        HmiTrailCards::Style style;
        style.title = "Didacticiel : les parcours de l'API";
        style.countStarted = true;
        style.summaryLines = 3;
        style.noteTitle = "Comment \xC3\xA7" "a marche.";
        style.note = "Une visite montre : une bulle pointe l'endroit, Suivant avance. Un parcours interactif fait faire : l'\xC3\xA9tape "
                     "attend ton geste (\xC2\xAB lance la simulation \xC2\xBB) et le v\xC3\xA9rifie avant de passer ; Montre-moi le fait \xC3\xA0 ta place. "
                     "Tout ce qu'il cr\xC3\xA9" "e passe par l'historique : \xC3\xA0 la fin, Garder ou Tout d\xC3\xA9" "faire. Passer s'arr\xC3\xAAte l\xC3\xA0 ; "
                     "Reprendre repart de l'\xC3\xA9tape o\xC3\xB9 tu \xC3\xA9tais. On y vient par Aide \xE2\x80\xBA Didacticiel de l'API, la carte "
                     "\xC2\xAB D\xC3\xA9" "couvrir l'API \xC2\xBB du tableau de bord, F1 dans un onglet de l'API, et l'accueil.";
        cards->setStyle(std::move(style));
        cards_ = cards.get();
        panel->setContent(std::move(cards));
        panel_ = &static_cast<ui::ScrollablePanel&>(addChild(std::move(panel)));
    }
    [[nodiscard]] HmiTrailCards& cards() noexcept { return *cards_; }
    void refresh() {
        if (!provider_) return;
        auto next = provider_();
        if (next != cards_->cards()) cards_->setCards(std::move(next));
    }

protected:
    void onLayout() override { panel_->setBounds(bounds()); }
    void onPaint(const ui::PaintContext& ctx) override {
        ctx.r.fillRect(bounds(), ctx.theme.color.windowBg);
        // La progression bouge sans prevenir (un parcours lance d'ailleurs, un
        // autre projet ouvert) : relue deux fois par seconde, tant qu'on la voit.
        if (ctx.time - polled_ > 0.5 || ctx.time < polled_) {
            polled_ = ctx.time;
            refresh();
        }
    }

private:
    Provider               provider_;
    ui::ScrollablePanel*   panel_{nullptr};
    HmiTrailCards*         cards_{nullptr};
    double                 polled_{-10.0};
};

// Les actions de la barre de l'onglet.
enum PageAction : int { PVisit = 1, PResume, PHmiTrails, PHelp };

} // namespace

// =================================================================== le kit ====
struct MainAnalysisScreen::ApiTrails::Kit {
    using Screen = MainAnalysisScreen;

    // ---- ou sont les onglets ------------------------------------------------------
    //  Le centre peut montrer plusieurs groupes d'onglets cote a cote (la
    //  mosaique) et detacher un onglet dans sa propre fenetre : « l'onglet
    //  courant » ne dit plus ce qui se voit. Un widget se VOIT si lui et tous ses
    //  parents sont montres (les pages repliees d'un groupe ne le sont pas) ; il
    //  est DANS LA FENETRE DE LA BULLE si sa racine est celle de l'ecran - sinon
    //  ses rectangles ne sont pas ceux de la bulle, on n'y eclaire rien.
    static bool visibleNow(const ui::Widget* w) {
        if (!w) return false;
        for (const ui::Widget* at = w; at; at = at->parent())
            if (!at->visible()) return false;
        return true;
    }
    static bool onScreen(Screen& s, const ui::Widget* w) {
        if (!w) return false;
        const ui::Widget* top = w;
        for (const ui::Widget* at = w; at; at = at->parent()) {
            if (!at->visible()) return false;
            top = at;
        }
        return top == s.widgetRoot();
    }
    // Une entree du dossier API (sans IHM, la racine tient lieu de dossier API).
    static ui::NodeId apiNode(Screen& s, NK kind) {
        if (kind == NK::ApiFolder) {
            if (!s.treeModel_) return ui::kInvalidNode;
            return s.treeModel_->hasHmi() ? ProjectTreeModel::apiFolderNode() : s.treeModel_->root();
        }
        return ProjectTreeModel::pack(kind, 0);
    }
    // Une entree du dossier IHM, par son genre.
    static ui::NodeId hmiNode(Screen& s, NK kind) {
        if (!s.treeModel_ || !s.treeModel_->hasHmi()) return ui::kInvalidNode;
        const auto root = ProjectTreeModel::hmiFolderNode();
        for (std::size_t k = 0; k < s.treeModel_->childCount(root); ++k)
            if (ProjectTreeModel::kindOf(s.treeModel_->childAt(root, k)) == kind) return s.treeModel_->childAt(root, k);
        return root;
    }
    static bool nodeRow(Screen& s, ui::NodeId node, Rect& r) { return s.explorer_ && node != ui::kInvalidNode && s.explorer_->rowRect(node, r); }
    // Lot API 8 : l'arbre du projet - un outil sorti de l'arbre (Statistiques) :
    // son bouton dans la rangee sous le titre API.
    static std::function<bool(Rect&)> row(Screen& s, NK kind) {
        return [&s, kind](Rect& r) { return s.treeToolRect(apiNode(s, kind), r) || nodeRow(s, apiNode(s, kind), r); };
    }
    static std::function<void()> reveal(Screen& s, NK kind) {
        return [&s, kind] { if (!s.revealTreeTool(apiNode(s, kind))) (void)s.revealTreeNode(apiNode(s, kind)); };
    }
    static bool barPart(Screen& s, std::string_view part, Rect& r) {
        if (!s.topBar_) return false;
        r = s.topBar_->partRect(part);
        return !r.empty();
    }
    // La page de l'onglet de l'API de cette cle ; nulle : pas ouvert. Par sa cle
    // (apiTabs_ : le pointeur est compare aux pages du centre avant d'etre
    // suivi), sinon par l'identifiant de son cadre dans l'ecran
    // ("analysis.api.simulation") - ou qu'il soit range.
    static ui::Widget* pageOf(Screen& s, const std::string& key) {
        if (!s.centre_) return nullptr;
        if (ui::Widget* page = s.apiTab(key); page && s.centre_->indexOf(page) >= 0) return page;
        ui::Widget* root = s.widgetRoot();
        return root ? root->findById(key == "api" ? std::string("analysis.api") : "analysis.api." + key) : nullptr;
    }
    // Le cadre de cet onglet ; `shown` : et visible (au premier plan de son groupe).
    static ApiFrame* frameOf(Screen& s, const std::string& key, bool shown) {
        auto* frame = dynamic_cast<ApiFrame*>(pageOf(s, key));
        return frame && (!shown || visibleNow(frame)) ? frame : nullptr;
    }
    template <class Pane>
    static Pane* paneOf(Screen& s, const std::string& key, bool shown) {
        auto* f = frameOf(s, key, shown);
        return f ? dynamic_cast<Pane*>(&f->content()) : nullptr;
    }
    // Le volet, s'il est a l'ecran dans la fenetre de la bulle : ses rectangles
    // sont ceux qu'elle peut eclairer.
    template <class Pane>
    static Pane* paneOnScreen(Screen& s, const std::string& key) {
        auto* pane = paneOf<Pane>(s, key, true);
        return pane && onScreen(s, pane) ? pane : nullptr;
    }
    // Un bouton de la barre d'un onglet a l'ecran, par son action ou le debut de son infobulle.
    static bool tool(Screen& s, const std::string& key, int action, Rect& r) {
        auto* f = frameOf(s, key, true);
        if (!f || !onScreen(s, f)) return false;
        r = f->tools().rectOf(action);
        return !r.empty();
    }
    static bool toolByTip(Screen& s, const std::string& key, std::string_view tip, Rect& r) {
        auto* f = frameOf(s, key, true);
        if (!f || !onScreen(s, f)) return false;
        const int a = f->tools().actionByTip(tip);
        if (a < 0) return false;
        r = f->tools().rectOf(a);
        return !r.empty();
    }
    static bool shownWidget(Screen& s, const ui::Widget* w, Rect& r) {
        if (!onScreen(s, w)) return false;
        r = w->bounds();
        return !r.empty();
    }
    // Le premier widget de ce type montre dans `root` (en largeur d'abord).
    template <class T>
    static T* shownIn(ui::Widget* root) {
        if (!root || !root->visible()) return nullptr;
        std::vector<ui::Widget*> queue{root};
        for (std::size_t i = 0; i < queue.size(); ++i) {
            if (auto* t = dynamic_cast<T*>(queue[i])) return t;
            for (const auto& c : queue[i]->children())
                if (c->visible()) queue.push_back(c.get());
        }
        return nullptr;
    }
    // L'onglet Macros a l'ecran, et la macro qu'il montre (celle du formulaire
    // en cours, sinon celle de la liste).
    static MacrosPane* macrosShown(Screen& s) {
        auto* m = s.macrosPane();
        return m && visibleNow(m) ? m : nullptr;
    }
    static std::string macroOf(const MacrosPane& m) {
        return m.step() >= 1 && m.session() ? m.session()->name() : m.selectedMacro();
    }
    // L'editeur d'une section (son onglet, "analysis.doc.<rang>"), s'il se voit.
    static ui::Widget* sectionEditor(Screen& s, domain::Index section) {
        ui::Widget* root = s.widgetRoot();
        ui::Widget* page = root ? root->findById("analysis.doc." + std::to_string(section)) : nullptr;
        return visibleNow(page) ? page : nullptr;
    }
    static void say(Screen& s, const std::string& message) {
        if (s.status_) s.status_->setTransientMessage(message, 8.0);
    }
    static std::uint64_t topSerial(Screen& s) {
        const auto& st = s.app_.commands();
        return st.done().empty() ? 0 : st.done().back().info.serial;
    }
    // Le projet, tel que la progression le range : son dossier, sinon son nom.
    static std::string projectWhere(const Screen& s) {
        std::string where = s.app_.projectFolder();
        if (where.empty())
            if (auto p = s.app_.project()) where = p->header.projectName;
        return where;
    }
    // Le dernier parcours lance dans ce projet (Reprendre, dans l'onglet).
    static std::string lastTrailKey(const Screen& s) { return "didacticiel.api." + projectTag(projectWhere(s)) + ".dernier"; }

    // ---- la simulation ------------------------------------------------------------
    static sim::Runtime* runtime(Screen& s) { return s.app_.simulation().attached() ? s.app_.simulationRuntime() : nullptr; }
    static bool running(Screen& s) {
        return s.app_.simulation().attached() && s.app_.simulation().state() == SimulationHost::State::Running;
    }
    // Le programme est lu quand la simulation se prepare : ce qui vient apres
    // (une variable, une section) lui est inconnu.
    // ---- Lot API 8 : le moteur de simulation (plus de retour au cycle 0) ----
    //  Elle n'est plus lachee (le projet est modifie en place) : elle prend le
    //  programme a jour TOUT DE SUITE - en marche ou en pause, la modification
    //  en ligne (le cycle, les valeurs, les forcages restent) ; arretee, le
    //  nouveau code.
    //  `why` vide : sans le dire (rien ne tournait).
    static void restartOnProgram(Screen& s, const std::string& why) {
        auto& host = s.app_.simulation();
        const bool online = host.attached() && (host.state() == SimulationHost::State::Running || host.state() == SimulationHost::State::Paused);
        if (auto p = s.app_.project(); p && host.attached()) (void)host.attach(p);
        if (auto* sim = hmiSim(s)) sim->refreshNow();   // l'IHM en marche suit le runtime a jour
        s.refreshSimulationIndicator();
        if (why.empty()) return;
        if (host.stale()) say(s, "La simulation n'a pas encore pris le programme \xC3\xA0 jour (Simulation \xE2\x80\xBA Automate dit pourquoi) : " + why + ".");
        else if (online) say(s, "La simulation prend le programme \xC3\xA0 jour sans repartir du cycle 0 : " + why + ".");
        else say(s, "La simulation prend le programme \xC3\xA0 jour : " + why + ".");
    }
    // Preparee AVANT la variable (elle ne la connait pas) : mise a jour. `idle` :
    // aussi une simulation arretee ou en pause - peut-etre preparee avant la
    // section qui l'ecrit ; la mettre a jour ne coute rien (lot API 8 : en
    // pause, le cycle est garde).
    static bool refreshStale(Screen& s, const std::string& var, bool idle = false) {
        auto p = s.app_.project();
        auto* rt = runtime(s);
        if (!p || !rt || globalVariable(*p, var) == domain::kNoIndex) return false;
        if (!rt->known(var)) restartOnProgram(s, "elle avait \xC3\xA9t\xC3\xA9 pr\xC3\xA9par\xC3\xA9" "e avant " + var);
        else if (idle && !running(s)) restartOnProgram(s, {});
        else return false;
        return true;
    }
    static void startSimulation(Screen& s) {
        if (s.app_.simulation().attached() && s.app_.simulation().state() == SimulationHost::State::Halted) s.runSimulationTransport("sim.stop");
        if (!running(s)) s.runSimulationTransport("sim.run");
    }
    static HmiSimulationPane* hmiSim(Screen& s) { return dynamic_cast<HmiSimulationPane*>(s.hmiTab("simulation")); }
    // L'IHM en marche sur cette vue (et l'automate simule dessous).
    static void simulateHmi(Screen& s, hmi::Id view) {
        s.openHmiPane("simulation");
        auto* sim = hmiSim(s);
        if (!sim) return;
        startSimulation(s);
        if (!sim->runtime().running()) sim->restart();
        if (view != hmi::kNoId) sim->goToView(view);
        sim->refreshNow();
    }

    // ---- les parcours -------------------------------------------------------------
    static std::vector<Step> discover(Screen& s);
    static std::vector<Step> variable(Screen& s);
    static std::vector<Step> hmiTable(Screen& s, const std::shared_ptr<ApiRun>& run);
    static std::vector<Step> block(Screen& s, const std::shared_ptr<ApiRun>& run);
    static std::vector<Step> order(Screen& s, const std::shared_ptr<ApiRun>& run);
    static std::vector<Step> macro(Screen& s, const std::shared_ptr<ApiRun>& run);

    // Ce que chaque parcours choisit dans le projet ouvert.
    static void pickHmi(Screen& s, ApiRun& run);
    static void pickBlock(Screen& s, ApiRun& run);
    static void pickOrder(Screen& s, ApiRun& run);
    static void pickMacro(Screen& s, ApiRun& run);
    // Les lectures avant l'ecriture de la tache, relues quand le projet change.
    static const std::vector<project::api::LateRead>& lateNow(Screen& s, ApiRun& run);
    // Le resume d'une carte, avec les noms du projet.
    static std::string summary(Screen& s, const std::string& key);
};

// ============================================================ les choix ========
void MainAnalysisScreen::ApiTrails::Kit::pickHmi(Screen& s, ApiRun& run) {
    auto doc = s.app_.hmi();
    auto p = s.app_.project();
    if (!doc || !p) return;
    const auto& vars = doc->project.programs.variables;
    // Ce qui ecrit une variable IHM : une commande reliee (un bouton lumineux
    // qui la bascule) ou une action de clic qui l'ecrit.
    const auto writerOf = [&](std::string_view name, const hmi::View** view) -> const hmi::Object* {
        const hmi::Object* best = nullptr;
        for (const auto& v : doc->project.views) {
            if (v.role != "vue" && v.role != "popup") continue;
            for (const auto& o : v.objects) {
                const bool control = o.kind == hmi::Kind::IlluminatedButton || o.kind == hmi::Kind::PushButton || o.kind == hmi::Kind::Button
                                  || o.kind == hmi::Kind::Switch || o.kind == hmi::Kind::CheckBox;
                if (!control) continue;
                bool writes = sameName(o.text("variable"), name);
                for (const auto& a : o.actions)
                    if (a.trigger == hmi::Trigger::Click && sameName(a.target, name)
                        && (a.operation == hmi::Operation::Toggle || a.operation == hmi::Operation::Set || a.operation == hmi::Operation::Reset
                            || a.operation == hmi::Operation::Assign || a.operation == hmi::Operation::Increment
                            || a.operation == hmi::Operation::Decrement))
                        writes = true;
                if (!writes) continue;
                // Un bouton plutot qu'une case a cocher.
                if (!best || (best->kind == hmi::Kind::CheckBox && o.kind != hmi::Kind::CheckBox)) {
                    best = &o;
                    if (view) *view = &v;
                }
            }
        }
        return best;
    };
    std::string chosen;
    if (const auto* v = doc->project.variable("Vanne_Purge")) chosen = v->name;
    for (const auto& v : vars)
        if (chosen.empty() && writerOf(v.name, nullptr)) chosen = v.name;
    for (const auto& v : vars)
        if (chosen.empty() && sameName(v.type, "BOOL")) chosen = v.name;
    if (chosen.empty() && !vars.empty()) chosen = vars.front().name;
    run.hmiVar = chosen;
    const hmi::View* view = nullptr;
    if (const auto* o = chosen.empty() ? nullptr : writerOf(chosen, &view); o && view) {
        run.buttonName = o->name;
        run.buttonView = view->id;
        run.buttonViewName = view->name;
        const auto sig = lower(o->text("signature"));
        run.signature = !sig.empty() && sig != "aucune" && sig != "none" && sig != "non";
    }
    // La table : PURGE, une table de la purge, la premiere.
    std::size_t t = tableNamed(*p, "PURGE");
    for (std::size_t i = 0; t == kNpos && i < p->animationTables.size(); ++i)
        if (lower(p->strings.text(p->animationTables[i].name)).find("purge") != std::string::npos) t = i;
    if (t == kNpos && !p->animationTables.empty()) t = 0;
    run.hmiTable = t == kNpos ? std::string{} : text(*p, p->animationTables[t].name);
}

void MainAnalysisScreen::ApiTrails::Kit::pickBlock(Screen& s, ApiRun& run) {
    auto p = s.app_.project();
    if (!p) return;
    const auto library = s.apiLibrary();
    std::vector<project::VersionNotice> late;
    if (library)
        for (auto& n : library->outdated(*p))
            if (n.kind == project::LibraryItemKind::FunctionBlock && blockNamed(*p, n.name)) late.push_back(std::move(n));
    // CAPTEUR s'il est en retard ; sinon le premier bloc en retard ; sinon CAPTEUR, ou le premier bloc.
    const project::VersionNotice* notice = nullptr;
    for (const auto& n : late)
        if (!notice && sameName(n.name, "CAPTEUR")) notice = &n;
    if (!notice && !late.empty()) notice = &late.front();
    if (notice) {
        const auto* pou = blockNamed(*p, notice->name);
        run.block = pou ? text(*p, pou->name) : notice->name;
        run.versionBefore = pou ? pou->version : notice->projectVersion;
        run.libraryVersion = notice->libraryVersion;
    } else {
        const domain::Pou* pou = blockNamed(*p, "CAPTEUR");
        for (const auto& q : p->pous)
            if (!pou && q.kind == domain::PouKind::FunctionBlockType && q.userDefined) pou = &q;
        if (pou) {
            run.block = text(*p, pou->name);
            run.versionBefore = pou->version;
        }
    }
    if (!run.block.empty()) {
        const auto list = project::usage::instancesOf(*p, run.block, project::usage::Where(*p));
        run.instances = list.size();
        run.instanceList = instanceNames(list, 3);
    }
}

void MainAnalysisScreen::ApiTrails::Kit::pickOrder(Screen& s, ApiRun& run) {
    auto p = s.app_.project();
    if (!p) return;
    run.task = mainTask(*p);
    const auto& late = lateNow(s, run);
    if (late.empty()) return;
    run.reader = late.front().reader;
    run.variable = late.front().variable;
    run.writer = late.front().writer;
    run.writerRank = late.front().writerRank;
}

void MainAnalysisScreen::ApiTrails::Kit::pickMacro(Screen& s, ApiRun& run) {
    const auto library = s.apiLibrary();
    std::string first;
    if (library)
        for (const auto& item : library->items()) {
            if (item.kind != project::LibraryItemKind::Macro) continue;
            if (sameName(item.name, "ImporterClasseur")) {
                run.macro = item.name;
                return;
            }
            if (first.empty()) first = item.name;
        }
    run.macro = first.empty() ? std::string("ImporterClasseur") : first;
}

const std::vector<project::api::LateRead>& MainAnalysisScreen::ApiTrails::Kit::lateNow(Screen& s, ApiRun& run) {
    // La lecture du code de toute la tache coute : une fois par etat du projet.
    const auto revision = s.app_.commands().revision();
    if (revision != run.lateRevision) {
        run.lateRevision = revision;
        auto p = s.app_.project();
        run.late = p ? project::api::lateReads(*p, run.task.empty() ? mainTask(*p) : run.task) : std::vector<project::api::LateRead>{};
    }
    return run.late;
}

std::string MainAnalysisScreen::ApiTrails::Kit::summary(Screen& s, const std::string& key) {
    if (key == "api-decouvrir")
        return "L'arbre, la barre du haut, le tableau de bord, la configuration, les t\xC3\xA2" "ches, l'ordre, les variables, les tables "
               "d'animation, la simulation, les statistiques, les macros, Vers Control Expert.";
    if (key == "api-variable")
        return std::string("Cr\xC3\xA9" "er ") + kCounter + ", l'\xC3\xA9" "crire dans une section, l'ajouter \xC3\xA0 une table d'animation, lancer la "
               "simulation, la voir compter \xE2\x80\x94 puis la forcer.";
    if (key == "api-ihm-table") {
        ApiRun run;
        pickHmi(s, run);
        return "Ajouter " + (run.hmiVar.empty() ? std::string("une variable IHM") : run.hmiVar) + " \xC3\xA0 "
             + (run.hmiTable.empty() ? std::string("une table") : run.hmiTable)
             + ", lancer la simulation, cliquer le bouton de la vue, voir la table suivre.";
    }
    if (key == "api-bloc") {
        ApiRun run;
        pickBlock(s, run);
        if (run.block.empty()) return "Comparer un bloc DFB \xC3\xA0 la biblioth\xC3\xA8que, le mettre \xC3\xA0 jour, v\xC3\xA9rifier ses instances, et tout d\xC3\xA9" "faire si besoin.";
        const std::string versions = run.libraryVersion.empty() ? run.block + " \xC3\xA0 la biblioth\xC3\xA8que"
                                                                 : run.block + " " + run.versionBefore + " et " + run.libraryVersion;
        return "Comparer " + versions + ", mettre \xC3\xA0 jour, v\xC3\xA9rifier ses " + plural(run.instances, "instance", "instances")
             + ", et tout d\xC3\xA9" "faire si besoin.";
    }
    if (key == "api-ordre") return "Trouver une lecture avant l'\xC3\xA9" "criture, glisser la section au bon rang, v\xC3\xA9rifier l'ordre.";
    if (key == "api-macro") {
        ApiRun run;
        pickMacro(s, run);
        return run.macro + " : le formulaire, l'aper\xC3\xA7u, Appliquer, puis un seul Ctrl+Z.";
    }
    if (const auto* t = lot8::find(key)) return t->summary;   // Lot API 8 : didacticiels et aide
    return {};
}

// ============================================================ Decouvrir l'API ==
std::vector<Step> MainAnalysisScreen::ApiTrails::Kit::discover(Screen& s) {
    std::vector<Step> st;
    st.push_back({"Le dossier API",
                  "Bienvenue dans l'API. Tout part d'ici, dans l'arbre du projet : chaque entr\xC3\xA9" "e du dossier API ouvre son "
                  "onglet au centre, et un clic sur \xC2\xAB API \xC2\xBB ouvre le tableau de bord.\n"
                  "Suivant (ou Entr\xC3\xA9" "e) pour continuer, Passer (ou \xC3\x89" "chap) pour fermer la visite.",
                  row(s, NK::ApiFolder),
                  [&s] {
                      const auto node = apiNode(s, NK::ApiFolder);
                      (void)s.revealTreeNode(node);
                      if (s.explorer_ && node != ui::kInvalidNode) s.explorer_->expand(node);
                  }});
    st.push_back({"La barre du haut",
                  "Pour tout le projet : son nom et son \xC3\xA9tat (NEW, DEV, FINISH, LOCK), la version que tu modifies ; Annuler, "
                  "R\xC3\xA9tablir et l'Historique ; + Nouveau (une section, une variable, un bloc...) ; Aller \xC3\xA0... (Ctrl+K) ; la "
                  "simulation en un bloc ; Vers Control Expert ; Affichage et Aide.",
                  [&s](Rect& r) { return shownWidget(s, s.topBar_, r); }, {}});
    st.push_back({"Le tableau de bord",
                  "Un clic sur \xC2\xAB API \xC2\xBB l'ouvre : l'automate, la t\xC3\xA2" "che principale, les types, les variables - chaque carte "
                  "ouvre son onglet. Dessous, \xC2\xAB \xC3\x80 regarder \xC2\xBB dit ce qu'une relecture trouverait (un bloc en retard sur la "
                  "biblioth\xC3\xA8que, une variable lue avant d'\xC3\xAAtre \xC3\xA9" "crite), avec le bouton qui le r\xC3\xA8gle.",
                  [&s](Rect& r) {
                      auto* f = frameOf(s, "api", true);
                      auto* dash = f ? dynamic_cast<ApiDashboard*>(&f->content()) : nullptr;
                      if (dash && onScreen(s, dash)) {
                          // Les cartes, telles que le tableau les a dessinees : de
                          // l'Automate (la premiere) aux Variables (la derniere) - une
                          // rangee de quatre, ou deux rangees de deux.
                          const Rect all = unite(dash->partRect("configuration"), dash->partRect("variables"));
                          if (!all.empty()) {
                              r = all;
                              return true;
                          }
                          return shownWidget(s, dash, r);
                      }
                      // Pas (encore) a l'ecran : la ligne API de l'arbre, qui l'ouvre.
                      return nodeRow(s, apiNode(s, NK::ApiFolder), r);
                  },
                  [&s] { s.openApiPane("api"); }});
    st.push_back({"La configuration",
                  "Le processeur, les racks et leurs modules, les voies et leurs adresses, le r\xC3\xA9seau, le plan m\xC3\xA9moire (%M, "
                  "%MW, %KW). Importer le .XHW y pose le mat\xC3\xA9riel de Control Expert ; chaque changement est une commande : "
                  "Ctrl+Z le reprend.",
                  row(s, NK::ConfigurationFolder), reveal(s, NK::ConfigurationFolder)});
    st.push_back({"Les t\xC3\xA2" "ches",
                  "MAST, et FAST ou AUX si tu les ajoutes : cyclique ou p\xC3\xA9riodique, la p\xC3\xA9riode, le chien de garde. Chaque "
                  "t\xC3\xA2" "che d\xC3\xA9roule ses sections dans son ordre d'ex\xC3\xA9" "cution.",
                  row(s, NK::TaskFolder), reveal(s, NK::TaskFolder)});
    st.push_back({"L'ordre d'ex\xC3\xA9" "cution",
                  "Les sections et les unit\xC3\xA9s de MAST, dans l'ordre o\xC3\xB9 elles tournent : ce que chacune \xC3\xA9" "crit et lit, et les "
                  "lectures avant l'\xC3\xA9" "criture (une variable lue avant la section qui l'\xC3\xA9" "crit). Glisser une ligne la d\xC3\xA9place ; "
                  "le parcours \xC2\xAB Ranger l'ordre d'ex\xC3\xA9" "cution \xC2\xBB le fait faire.",
                  row(s, NK::ExecOrderFolder), reveal(s, NK::ExecOrderFolder)});
    st.push_back({"Les variables",
                  "Les variables globales, en dossiers (instances de blocs, temporisations, types d\xC3\xA9riv\xC3\xA9s, situ\xC3\xA9" "es...) : o\xC3\xB9 "
                  // Lot API 8 : didacticiels et aide - Renommer passe par le dialogue (F2), Ctrl+F cherche.
                  "chacune est \xC3\xA9" "crite et lue, ce que l'IHM en lit. Renommer (F2 : le dialogue qui montre tout ce qui change) "
                  "suit le code, les tables et l'IHM ; Ctrl+F cherche, tes filtres sont retenus ; un tableau d'Excel "
                  "se colle ici (Ctrl+V).",
                  row(s, NK::VariablesFolder), reveal(s, NK::VariablesFolder)});
    st.push_back({"Types d\xC3\xA9riv\xC3\xA9s et blocs DFB",
                  "Tes structures (DDT) et tes blocs (DFB), face \xC3\xA0 la biblioth\xC3\xA8que : \xC2\xAB 0.03 disponible \xC2\xBB, en orange, dit "
                  "qu'une version plus r\xC3\xA9" "cente attend. Un bloc montre ses broches et ses instances ; le parcours \xC2\xAB Mettre \xC3\xA0 "
                  "jour un bloc \xC2\xBB le fait faire.",
                  [&s](Rect& r) {
                      Rect a{}, b{};
                      const bool ok = nodeRow(s, apiNode(s, NK::TypesFolder), a);
                      const bool ok2 = nodeRow(s, apiNode(s, NK::DfbFolder), b);
                      r = unite(ok ? a : Rect{}, ok2 ? b : Rect{});
                      return !r.empty();
                  },
                  [&s] {
                      (void)s.revealTreeNode(apiNode(s, NK::TypesFolder));
                      (void)s.revealTreeNode(apiNode(s, NK::DfbFolder));
                  }});
    st.push_back({"Les unit\xC3\xA9s de programme",
                  "Une unit\xC3\xA9 regroupe des sections et porte ses propres variables : ses param\xC3\xA8tres (reli\xC3\xA9s aux variables du "
                  "projet) et ses locales. Elle tourne en bloc, \xC3\xA0 son rang dans la t\xC3\xA2" "che.",
                  row(s, NK::UnitsFolder), reveal(s, NK::UnitsFolder)});
    st.push_back({"Les tables d'animation",
                  "Des variables \xC3\xA0 surveiller ensemble, de l'automate ou de l'IHM : leur valeur en direct pendant la "
                  "simulation, une nouvelle valeur \xC3\xA0 \xC3\xA9" "crire, Forcer, la courbe. Une variable de l'arbre se glisse sur une table.",
                  row(s, NK::TablesFolder), reveal(s, NK::TablesFolder)});
    // ---- Lot API 8 : didacticiels et aide ---- la simulation a quitte l'API :
    // le dossier Simulation, au meme niveau qu'API, IHM et Versions.
    st.push_back({"Nouveau : le dossier Simulation",
                  "Au m\xC3\xAAme niveau qu'API, IHM et Versions : Vue d'ensemble (F9), Automate (l'ancien API \xE2\x80\xBA Simulation : "
                  "les variables du projet et leur valeur), IHM, \xC3\x89quipements, D\xC3\xA9" "bogage, For\xC3\xA7" "ages, Courbes, Journal. "
                  "Simuler (F5), Pause, Arr\xC3\xAAter (Maj+F5) et Un cycle sont aussi dans la barre du haut ; le parcours "
                  "\xC2\xAB Simuler et suivre la simulation \xC2\xBB le fait faire.",
                  [&s](Rect& r) { return nodeRow(s, ProjectTreeModel::simFolderNode(), r); },
                  [&s] {
                      const auto node = ProjectTreeModel::simFolderNode();
                      (void)s.revealTreeNode(node);
                      if (s.explorer_) s.explorer_->expand(node);
                  }});
    // ---- fin Lot API 8 : didacticiels et aide ----
    st.push_back({"Nouveau : les statistiques",
                  "Un onglet (Ctrl+5, ou le bouton Statistiques sous le titre API de l'arbre) : le projet en chiffres - "
                  "ses sections et leurs lignes, ses types, ses variables, ses blocs et leurs instances.",
                  row(s, NK::ApiStatistics), reveal(s, NK::ApiStatistics)});
    st.push_back({"Les macros",
                  "Des programmes qui font le travail r\xC3\xA9p\xC3\xA9titif : importer l'affaire depuis le classeur, g\xC3\xA9n\xC3\xA9rer, ranger, "
                  "mettre \xC3\xA0 jour. Lancer ouvre un formulaire et un aper\xC3\xA7u ; Appliquer est un seul pas de l'historique. Le "
                  "parcours \xC2\xAB Lancer une macro \xC2\xBB le fait faire.",
                  row(s, NK::MacroFolder), reveal(s, NK::MacroFolder)});
    st.push_back({"Vers Control Expert",
                  "Le livrable : src/MAST.XPG et src/CONFIG.XHW, pr\xC3\xAAts \xC3\xA0 r\xC3\xA9importer dans Control Expert.\n"
                  "C'est la fin de la visite. Les parcours interactifs (Aide \xE2\x80\xBA Didacticiel de l'API) te font faire : une "
                  "variable suivie, une variable IHM dans une table, un bloc mis \xC3\xA0 jour, l'ordre rang\xC3\xA9, une macro. "
                  "Ce qui est nouveau en 1.8.0 : Aide \xE2\x80\xBA Nouveaut\xC3\xA9s\xE2\x80\xA6 \xE2\x80\xBA Versions pr\xC3\xA9" "c\xC3\xA9" "dentes.",   // Lot API 8 : didacticiels et aide
                  [&s](Rect& r) { return barPart(s, "control-expert", r); }, {}});
    return st;
}

// ============================================================ Ma premiere variable suivie
std::vector<Step> MainAnalysisScreen::ApiTrails::Kit::variable(Screen& s) {
    std::vector<Step> st;
    const std::string var = kCounter;
    // 1. La variable.
    {
        Step x{"Cr\xC3\xA9" "e la variable " + var,
               "Une variable globale de type INT : + Nouveau \xE2\x80\xBA Variable, dans la barre du haut (ou Variable, dans la barre "
               "de API \xE2\x80\xBA Variables). Nom : " + var + ", Type : INT, puis Cr\xC3\xA9" "er. Tout ce que le parcours cr\xC3\xA9" "e se d\xC3\xA9" "fait \xC3\xA0 "
               "la fin, si tu veux.",
               [&s](Rect& r) {
                   if (tool(s, "variables", VariablesPane::AAdd, r)) return true;
                   return barPart(s, "nouveau", r);
               },
               {}};
        x.done = [&s, var](std::string& what) {
            auto p = s.app_.project();
            const auto v = p ? globalVariable(*p, var) : domain::kNoIndex;
            if (v == domain::kNoIndex) return false;
            what = text(*p, p->variables[v].name) + " existe : " + text(*p, p->variables[v].type.name) + ", globale";
            return true;
        };
        x.showMe = [&s, var] {
            auto doc = s.app_.document();
            if (!doc) return;
            project::AddVariableCommand::Spec spec;
            spec.name = var;
            spec.type = "INT";
            spec.initValue = "0";
            spec.comment = "Cr\xC3\xA9\xC3\xA9" "e par le didacticiel (Ma premi\xC3\xA8re variable suivie)";
            s.app_.apply(std::make_unique<project::AddVariableCommand>(doc, spec));
            s.openApiPane("variables");
            if (auto* pane = Kit::paneOf<VariablesPane>(s, "variables", false)) (void)pane->selectVariable(var);
            say(s, "Le parcours a cr\xC3\xA9\xC3\xA9 " + var + " (INT, globale) ; Ctrl+Z la reprend, Tout d\xC3\xA9" "faire aussi.");
        };
        x.waiting = "J'attends la variable " + var + "\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 2. La section qui la fait compter : + Nouveau > Section (un dialogue), puis
    //    son editeur - la bulle se met de cote. La bulle suit : + Nouveau, puis
    //    la ligne de la section dans l'arbre, puis Edit, puis le code.
    {
        auto revealed = std::make_shared<bool>(false);
        Step x{"\xC3\x89" "cris-la dans une section",
               "Une section de MAST qui la fait compter : + Nouveau \xE2\x80\xBA Section, dans la barre du haut (Nom : " + std::string(kSection)
                   + ", T\xC3\xA2" "che : MAST, Langage : ST), Cr\xC3\xA9" "er. Ouvre-la d'un clic dans l'arbre (API \xE2\x80\xBA T\xC3\xA2" "ches \xE2\x80\xBA MAST), "
                   "Edit, puis \xC3\xA9" "cris :\n" + var + " := " + var + " + 1;",
               [&s](Rect& r) {
                   auto p = s.app_.project();
                   const auto sec = p ? taskSectionNamed(*p, kSection) : domain::kNoIndex;
                   if (sec == domain::kNoIndex) return barPart(s, "nouveau", r);
                   // Son editeur a l'ecran : Edit tant qu'il est en lecture seule, puis le code.
                   if (auto* page = sectionEditor(s, sec)) {
                       if (auto* edit = shownIn<ui::ToggleButton>(page); edit && !edit->checked()) return shownWidget(s, edit, r);
                       if (auto* code = shownIn<ui::MultiLineText>(page)) return shownWidget(s, code, r);
                       return shownWidget(s, page, r);
                   }
                   return nodeRow(s, ProjectTreeModel::pack(NK::Section, sec), r);
               },
               [revealed] { *revealed = false; }};
        x.aside = true;
        x.done = [&s, var, revealed](std::string& what) {
            auto p = s.app_.project();
            if (!p) return false;
            std::string line;
            if (const auto sec = sectionWriting(*p, var, &line); sec != domain::kNoIndex) {
                what = text(*p, p->sections[sec].name) + " : " + line;
                return true;
            }
            // La section vient d'etre creee (l'arbre se refait a chaque creation,
            // replie) : sa ligne, une fois, pour l'ouvrir d'un clic.
            if (const auto sec = taskSectionNamed(*p, kSection); sec != domain::kNoIndex && !*revealed && !sectionEditor(s, sec)) {
                *revealed = true;
                (void)s.revealTreeNode(ProjectTreeModel::pack(NK::Section, sec));
            }
            return false;
        };
        x.showMe = [&s, var] {
            auto doc = s.app_.document();
            if (!doc) return;
            auto sec = taskSectionNamed(*doc, kSection);
            if (sec == domain::kNoIndex) {
                s.app_.apply(std::make_unique<project::AddSectionCommand>(doc, kSection, mainTask(*doc), domain::PouLanguage::ST));
                sec = taskSectionNamed(*doc, kSection);
            }
            if (sec == domain::kNoIndex) return;
            std::string body = doc->sections[sec].body;
            if (!assigns(body, var, nullptr)) {
                if (!body.empty() && body.back() != '\n') body += '\n';
                body += var + " := " + var + " + 1;\n";
                s.app_.apply(std::make_unique<project::SetSectionBodyCommand>(doc, sec, body));
            }
            s.openDocument(sec);
            say(s, "Le parcours a \xC3\xA9" "crit " + var + " := " + var + " + 1; dans la section " + std::string(kSection) + " (" + mainTask(*doc)
                       + ") ; Tout d\xC3\xA9" "faire la retire.");
        };
        x.waiting = "J'attends une section qui \xC3\xA9" "crit " + var + " := \xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 3. Une table d'animation.
    {
        Step x{"Ajoute-la \xC3\xA0 une table d'animation",
               "API \xE2\x80\xBA Tables d'animation : choisis une table (ou Table, dans la barre, pour en cr\xC3\xA9" "er une : " + std::string(kTable)
                   + "), puis Variable API et coche " + var + ". Ou glisse la variable depuis l'arbre (API \xE2\x80\xBA Variables) sur la table.",
               [&s](Rect& r) {
                   // L'onglet des tables a l'ecran : Variable API ; sans table, Table (en creer une).
                   auto p = s.app_.project();
                   if (p && p->animationTables.empty() && tool(s, "tables", AnimationTablesPane::ATable, r)) return true;
                   if (tool(s, "tables", AnimationTablesPane::AAddApi, r)) return true;
                   return nodeRow(s, apiNode(s, NK::TablesFolder), r);
               },
               reveal(s, NK::TablesFolder)};
        x.done = [&s, var](std::string& what) {
            auto p = s.app_.project();
            const auto t = p ? tableWith(*p, var, false) : kNpos;
            if (t == kNpos) return false;
            what = var + " est dans la table " + text(*p, p->animationTables[t].name);
            return true;
        };
        x.showMe = [&s, var] {
            auto doc = s.app_.document();
            if (!doc) return;
            if (tableWith(*doc, var, false) == kNpos)
                s.app_.apply(std::make_unique<project::AddAnimationTableCommand>(
                    doc, project::freeAnimationTableName(*doc, kTable), project::defaultAnimationTableOwner(*doc),
                    std::vector<project::AnimationLine>{{var, false}}));
            s.openApiPane("tables");
            const auto t = tableWith(*doc, var, false);
            if (auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", false); pane && t != kNpos) {
                (void)pane->selectTable(t);
                (void)pane->selectLine(var);
            }
            if (t != kNpos)
                say(s, "Le parcours a ajout\xC3\xA9 " + var + " \xC3\xA0 la table " + text(*doc, doc->animationTables[t].name)
                           + " ; Tout d\xC3\xA9" "faire la retire avec la section.");
        };
        x.waiting = "J'attends " + var + " dans une table d'animation\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 4. En marche : le bloc Simulation de la barre du haut.
    {
        Step x{"Lance la simulation",
               "Clique Simuler, dans la barre du haut (ou dans l'onglet Simulation \xE2\x80\xBA Automate) : la section " + std::string(kSection)
                   + " ajoute 1 \xC3\xA0 " + var + " \xC3\xA0 chaque cycle.",
               [&s](Rect& r) { return barPart(s, "simulation", r) || barPart(s, "simuler", r); },
               // Une simulation preparee plus tot ne connait pas la section : Simuler
               // la preparera sur le programme a jour.
               [&s, var] { (void)refreshStale(s, var, true); }};
        x.done = [&s, var](std::string& what) {
            auto* rt = runtime(s);
            if (!running(s) || !rt || !rt->known(var)) return false;
            what = "la simulation est en marche (cycle " + std::to_string(s.app_.simulation().scanCount()) + ")";
            return true;
        };
        x.showMe = [&s, var] {
            (void)refreshStale(s, var, true);
            startSimulation(s);
        };
        x.waiting = "J'attends : la simulation en marche.";
        st.push_back(std::move(x));
    }
    // 5. La voir compter : sa ligne a l'ecran, et sa valeur qui bouge d'une
    //    verification a l'autre (0,2 s : une dizaine de cycles).
    {
        auto last = std::make_shared<std::optional<std::int64_t>>();
        Step x{"Regarde-la compter",
               "Dans ta table d'animation (ou dans Simulation \xE2\x80\xBA Automate), la ligne " + var + " : sa valeur monte d'un cran \xC3\xA0 "
               "chaque cycle.",
               [&s, var](Rect& r) {
                   for (const char* key : {"tables", "simulation"})
                       if (auto* f = frameOf(s, key, true); f && onScreen(s, f) && findTableRow(f, var, r)) return true;
                   auto p = s.app_.project();
                   const auto t = p ? tableWith(*p, var, false) : kNpos;
                   if (t != kNpos && nodeRow(s, ProjectTreeModel::pack(NK::AnimationTable, static_cast<domain::Index>(t)), r)) return true;
                   return nodeRow(s, apiNode(s, NK::TablesFolder), r);
               },
               [last] { last->reset(); }};
        x.done = [&s, var, last](std::string& what) {
            auto* rt = runtime(s);
            sim::Value v;
            if (!running(s) || !rt || !rt->get(var, v)) {
                last->reset();
                return false;
            }
            const std::int64_t now = v.asInteger();
            const bool moved = last->has_value() && **last != now;
            *last = now;
            if (!moved) return false;
            // La ligne a l'ecran : une table qui la porte, ou l'onglet Simulation.
            bool shown = frameOf(s, "simulation", true) != nullptr;
            if (auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", true))
                if (auto p = s.app_.project()) shown = shown || tableHas(*p, pane->currentTable(), var, false);
            if (!shown) return false;
            what = var + " vaut " + v.display() + ", et \xC3\xA7" "a monte : un de plus \xC3\xA0 chaque cycle";
            return true;
        };
        x.showMe = [&s, var] {
            auto p = s.app_.project();
            if (!p) return;
            const auto t = tableWith(*p, var, false);
            if (t != kNpos) {
                s.openApiPane("tables");
                if (auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", false)) {
                    (void)pane->selectTable(t);
                    (void)pane->selectLine(var);
                }
            } else {
                s.openApiPane("simulation");
            }
            // Forcee (trop tot) : rendue au programme, qu'on la voie compter.
            if (auto* rt = runtime(s); rt && rt->isForced(var)) (void)rt->unforce(var);
            // En marche sans compter : preparee avant la section (elle connait la
            // variable, pas le code qui l'ecrit).
            if (auto* rt = runtime(s); rt && running(s) && s.app_.simulation().scanCount() > 5) {
                sim::Value v;
                if (rt->get(var, v) && v.asInteger() == 0) restartOnProgram(s, "elle avait \xC3\xA9t\xC3\xA9 pr\xC3\xA9par\xC3\xA9" "e avant la section " + std::string(kSection));
            }
            (void)refreshStale(s, var);
            startSimulation(s);
        };
        x.waiting = "J'attends la ligne " + var + " \xC3\xA0 l'\xC3\xA9" "cran (une table d'animation, ou Simulation \xE2\x80\xBA Automate), et sa valeur qui monte\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 6. La forcer.
    {
        Step x{"Force-la",
               "Choisis sa ligne, tape une nouvelle valeur (100), puis Forcer : la variable garde cette valeur, quoi que le "
               "programme \xC3\xA9" "crive. D\xC3\xA9" "forcer la rend au programme. Ensuite : garder ce que le parcours a cr\xC3\xA9\xC3\xA9, ou tout d\xC3\xA9" "faire.",
               [&s](Rect& r) {
                   if (tool(s, "tables", AnimationTablesPane::AForce, r)) return true;
                   if (toolByTip(s, "simulation", "Forcer", r)) return true;
                   return nodeRow(s, apiNode(s, NK::TablesFolder), r);
               },
               {}};
        x.done = [&s, var](std::string& what) {
            auto* rt = runtime(s);
            if (!rt || !rt->isForced(var)) return false;
            sim::Value v;
            (void)rt->get(var, v);
            what = var + " est forc\xC3\xA9" "e \xC3\xA0 " + v.display() + " : le programme ne la change plus";
            return true;
        };
        x.showMe = [&s, var] {
            if (!runtime(s) || !runtime(s)->known(var)) {
                (void)refreshStale(s, var);
                startSimulation(s);
            }
            if (auto* rt = runtime(s); rt && rt->force(var, sim::Value::integer(sim::Type::Int, 100)))
                say(s, "Le parcours a forc\xC3\xA9 " + var + " \xC3\xA0 100 (la simulation, pas le projet) ; Tout d\xC3\xA9" "faire la d\xC3\xA9" "force.");
            if (auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", true)) (void)pane->selectLine(var);
        };
        x.waiting = "J'attends le for\xC3\xA7" "age de " + var + " (Forcer)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ Une variable IHM dans une table
std::vector<Step> MainAnalysisScreen::ApiTrails::Kit::hmiTable(Screen& s, const std::shared_ptr<ApiRun>& run) {
    std::vector<Step> st;
    const std::string var = run->hmiVar;
    // Un projet sans table d'animation : la premiere etape en fait creer une.
    const bool noTable = run->hmiTable.empty();
    const std::string table = noTable ? std::string("ta table") : run->hmiTable;
    const std::string theTable = noTable ? std::string("ta table") : "la table " + run->hmiTable;
    const auto tableRow = [&s, run](Rect& r) {
        auto p = s.app_.project();
        const auto t = p ? tableNamed(*p, run->hmiTable) : kNpos;
        if (t != kNpos && nodeRow(s, ProjectTreeModel::pack(NK::AnimationTable, static_cast<domain::Index>(t)), r)) return true;
        return nodeRow(s, apiNode(s, NK::TablesFolder), r);
    };
    const auto revealTable = [&s, run] {
        auto p = s.app_.project();
        const auto t = p ? tableNamed(*p, run->hmiTable) : kNpos;
        if (t == kNpos || !s.revealTreeNode(ProjectTreeModel::pack(NK::AnimationTable, static_cast<domain::Index>(t))))
            (void)s.revealTreeNode(apiNode(s, NK::TablesFolder));
    };
    const auto openTable = [&s, run] {
        s.openApiPane("tables");
        if (auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", false); pane && !run->hmiTable.empty())
            (void)pane->selectTable(std::string_view(run->hmiTable));
    };
    // 1. La table.
    {
        Step x{noTable ? std::string("Ouvre une table d'animation") : "Ouvre la table " + table,
               (noTable ? std::string("API \xE2\x80\xBA Tables d'animation : ce projet n'en a pas encore. Table, dans la barre de l'onglet, en "
                                      "cr\xC3\xA9" "e une (Montre-moi cr\xC3\xA9" "e ") + kHmiTable + ")."
                        : "API \xE2\x80\xBA Tables d'animation \xE2\x80\xBA " + table + " : un clic dans l'arbre.")
                   + " Une table m\xC3\xAAle les variables de l'automate et celles de l'IHM ; une ligne de l'IHM ne part jamais vers "
                     "Control Expert.",
               [&s, tableRow, noTable](Rect& r) {
                   if (noTable && tool(s, "tables", AnimationTablesPane::ATable, r)) return true;
                   return tableRow(r);
               },
               revealTable};
        x.done = [&s, run](std::string& what) {
            auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", true);
            auto p = s.app_.project();
            if (!pane || !p || pane->currentTable() >= p->animationTables.size()) return false;
            const std::string name = pane->currentTableName();
            if (!run->hmiTable.empty() && !sameName(name, run->hmiTable)) return false;
            run->hmiTable = name;
            what = name + " est ouverte (" + plural(p->animationTables[pane->currentTable()].entries.size(), "ligne", "lignes") + ")";
            return true;
        };
        x.showMe = [&s, run, openTable] {
            auto doc = s.app_.document();
            if (!doc) return;
            if (tableNamed(*doc, run->hmiTable) == kNpos) {
                run->hmiTable = project::freeAnimationTableName(*doc, kHmiTable);
                s.app_.apply(std::make_unique<project::AddAnimationTableCommand>(doc, run->hmiTable, project::defaultAnimationTableOwner(*doc)));
            }
            openTable();
        };
        x.waiting = "J'attends " + theTable + ", ouverte\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 2. La variable IHM dans la table.
    {
        Step x{"Ajoute la variable IHM " + var,
               "Variable IHM, dans la barre de l'onglet : coche " + var + ", puis Ajouter. (Ou glisse-la depuis IHM \xE2\x80\xBA "
               "Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM.) Sa ligne dit IHM dans la colonne Source.",
               [&s, tableRow](Rect& r) {
                   if (tool(s, "tables", AnimationTablesPane::AAddHmi, r)) return true;
                   return tableRow(r);
               },
               {}};
        x.done = [&s, run](std::string& what) {
            auto p = s.app_.project();
            const auto t = p ? tableNamed(*p, run->hmiTable) : kNpos;
            if (t == kNpos || !tableHas(*p, t, run->hmiVar, true)) return false;
            what = run->hmiVar + " (IHM) est dans " + run->hmiTable;
            return true;
        };
        x.showMe = [&s, run, openTable] {
            auto doc = s.app_.document();
            const auto t = doc ? tableNamed(*doc, run->hmiTable) : kNpos;
            if (t == kNpos) return;
            openTable();
            std::vector<project::AnimationLine> lines{{run->hmiVar, true}};
            if (auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", false)) (void)pane->addLines(std::move(lines), t);
            else s.app_.apply(std::make_unique<project::AddAnimationLinesCommand>(doc, t, std::move(lines)));
        };
        x.waiting = "J'attends " + var + " (IHM) dans " + table + "\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 3. L'IHM en marche (son onglet est grand : la bulle se met de cote).
    {
        const std::string where = run->buttonViewName.empty() ? std::string{}
                                : " Puis la vue " + run->buttonViewName + " (Vue suivante, dans la barre, ou les boutons de l'IHM) : "
                                  "c'est l\xC3\xA0 qu'est le bouton " + run->buttonName + ".";
        Step x{"Lance l'IHM en simulation",
               "Simulation \xE2\x80\xBA IHM, Marche : l'IHM tourne sur l'automate simul\xC3\xA9, ses variables prennent vie." + where,   // Lot API 8 : Centre de simulation
               [&s](Rect& r) { return nodeRow(s, s.treeModel_ && s.treeModel_->hasHmi() ? ProjectTreeModel::pack(NK::HmiSimulation, 0) : ui::kInvalidNode, r); },
               [&s] { if (s.treeModel_ && s.treeModel_->hasHmi()) (void)s.revealTreeNode(ProjectTreeModel::pack(NK::HmiSimulation, 0)); }};
        x.aside = true;
        x.done = [&s](std::string& what) {
            auto* sim = hmiSim(s);
            if (!sim || !sim->runtime().running()) return false;
            what = "l'IHM tourne";
            if (const auto* v = s.app_.hmi() ? s.app_.hmi()->project.view(sim->currentView()) : nullptr) what += ", vue " + v->name;
            return true;
        };
        x.showMe = [&s, run] { simulateHmi(s, run->buttonView); };
        x.waiting = "J'attends l'IHM en marche (Simulation \xE2\x80\xBA IHM, Marche)\xE2\x80\xA6";   // Lot API 8 : Centre de simulation
        st.push_back(std::move(x));
    }
    // 4. Le bouton.
    {
        std::string how = run->buttonName.empty()
            ? "Change " + var + " dans l'IHM en marche : un bouton, un interrupteur, ou Forcer... dans la barre de la simulation."
            : "Dans la vue en marche, clique " + run->buttonName + " : il \xC3\xA9" "crit " + var + ".";
        if (run->signature)
            how += " Il demande une signature (ton mot de passe et un motif) : c'est la s\xC3\xA9" "curit\xC3\xA9 de ce geste.";
        how += " Montre-moi \xC3\xA9" "crit la valeur \xC3\xA0 ta place.";
        Step x{"Clique le bouton de la vue", how,
               [&s, run](Rect& r) {
                   auto* sim = hmiSim(s);
                   if (!sim || !onScreen(s, sim) || run->buttonName.empty()) return false;
                   return sim->canvas().objectRect(run->buttonName, r) && !r.empty();
               },
               [run] {
                   run->hmiBefore.clear();
                   run->hmiBeforeSet = false;
               }};
        x.aside = true;
        x.done = [&s, run](std::string& what) {
            auto* sim = hmiSim(s);
            if (!sim || !sim->runtime().running()) return false;
            const auto* v = sim->runtime().variable(run->hmiVar);
            if (!v) return false;
            const std::string now = v->display();
            if (!run->hmiBeforeSet) {             // la valeur de depart : la premiere vue en marche
                run->hmiBefore = now;
                run->hmiBeforeSet = true;
                return false;
            }
            if (now == run->hmiBefore) return false;
            what = run->hmiVar + " : " + run->hmiBefore + " \xE2\x86\x92 " + now;
            return true;
        };
        x.showMe = [&s, run] {
            auto* sim = hmiSim(s);
            if (!sim || !sim->runtime().running()) {
                simulateHmi(s, run->buttonView);
                sim = hmiSim(s);
            }
            if (!sim) return;
            const auto* v = sim->runtime().variable(run->hmiVar);
            if (!run->hmiBeforeSet) {
                run->hmiBefore = v ? v->display() : std::string{};
                run->hmiBeforeSet = true;
            }
            sim::Value next = sim::Value::boolean(true);
            if (v && v->type() == sim::Type::Bool) next = sim::Value::boolean(!v->isTruthy());
            else if (v && sim::isInteger(v->type())) next = sim::Value::integer(v->type(), v->asInteger() + 1);
            else if (v && v->type() == sim::Type::Real) next = sim::Value::real(v->asReal() + 1.0);
            (void)sim->runtime().environment().write(run->hmiVar, next);
            sim->refreshNow();
        };
        x.waiting = "J'attends un changement de " + var + " dans l'IHM en marche\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 5. La table suit.
    {
        Step x{"Regarde la table suivre",
               "Reviens \xC3\xA0 API \xE2\x80\xBA Tables d'animation \xE2\x80\xBA " + table + " : la ligne " + var + " montre la valeur que l'IHM vient "
               "d'\xC3\xA9" "crire, en direct. Ensuite : garder la ligne ajout\xC3\xA9" "e, ou tout d\xC3\xA9" "faire.",
               [&s, run, tableRow](Rect& r) {
                   if (auto* pane = paneOnScreen<AnimationTablesPane>(s, "tables")) {
                       if (findTableRow(&pane->lines(), run->hmiVar, r)) return true;
                       return shownWidget(s, &pane->lines(), r);
                   }
                   return tableRow(r);
               },
               {}};
        x.done = [&s, run](std::string& what) {
            auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", true);
            auto* sim = hmiSim(s);
            if (!pane || !sim || !sim->runtime().running() || !sameName(pane->currentTableName(), run->hmiTable)) return false;
            const auto& lines = pane->lines();
            if (!lines.model()) return false;
            std::string shown;
            for (std::size_t r = 0; r < lines.model()->rowCount() && shown.empty(); ++r)
                if (cellNames(lines.model()->cellText(static_cast<ui::RowIndex>(r), AnimationTablesPane::CName), run->hmiVar))
                    shown = pane->valueText(r);
            if (shown.empty() || shown == "?") return false;
            what = run->hmiVar + " vaut " + shown + " dans " + run->hmiTable + ", comme dans la vue";
            return true;
        };
        x.showMe = [&s, run, openTable] {
            openTable();
            if (auto* pane = Kit::paneOf<AnimationTablesPane>(s, "tables", false)) (void)pane->selectLine(run->hmiVar);
        };
        x.waiting = "J'attends " + theTable + " \xC3\xA0 l'\xC3\xA9" "cran, l'IHM en marche\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ Mettre a jour un bloc
std::vector<Step> MainAnalysisScreen::ApiTrails::Kit::block(Screen& s, const std::shared_ptr<ApiRun>& run) {
    std::vector<Step> st;
    const std::string name = run->block;
    const bool updatable = !run->libraryVersion.empty();
    const std::string from = run->versionBefore.empty() ? std::string("?") : run->versionBefore;
    const std::string to = run->libraryVersion;
    const auto upToDate = [&s, run] {
        auto p = s.app_.project();
        return p && !run->libraryVersion.empty() && blockVersion(*p, run->block) == run->libraryVersion;
    };
    const auto openBlock = [&s, name] {
        s.openApiPane("dfb");
        if (auto* pane = Kit::paneOf<DfbPane>(s, "dfb", false)) (void)pane->selectBlock(name);
    };
    // 1. L'onglet.
    {
        Step x{"Ouvre les blocs DFB",
               "API \xE2\x80\xBA Blocs DFB : tes blocs, leur version, leurs broches, leurs instances - et la colonne Biblioth\xC3\xA8que, qui "
               "dit si libs/ en a une plus r\xC3\xA9" "cente.",
               row(s, NK::DfbFolder), reveal(s, NK::DfbFolder)};
        x.done = [&s](std::string& what) {
            if (!frameOf(s, "dfb", true)) return false;
            what = "l'onglet Blocs DFB est ouvert";
            return true;
        };
        x.showMe = [&s] { s.openApiPane("dfb"); };
        x.waiting = "J'attends l'onglet Blocs DFB\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 2. Comparer.
    {
        const std::string body = updatable
            ? "Choisis " + name + " : le projet a la " + from + ", la biblioth\xC3\xA8que (libs/) la " + to + " - la colonne "
              "Biblioth\xC3\xA8que le dit en orange, ses propri\xC3\xA9t\xC3\xA9s aussi. La mise \xC3\xA0 jour touchera ses "
              + plural(run->instances, "instance", "instances") + " d'un coup."
            : "Choisis " + name + " : sa version (" + from + ") et la colonne Biblioth\xC3\xA8que. Ici, libs/ n'a pas de version "
              "plus r\xC3\xA9" "cente : ce parcours montre les gestes, sans rien \xC3\xA0 mettre \xC3\xA0 jour.";
        Step x{updatable ? "Compare " + name + " " + from + " et " + to : "Choisis " + name, body,
               [&s, name](Rect& r) {
                   if (auto* pane = paneOnScreen<DfbPane>(s, "dfb")) {
                       if (findTableRow(&pane->table(), name, r)) return true;
                       return shownWidget(s, &pane->table(), r);
                   }
                   return nodeRow(s, apiNode(s, NK::DfbFolder), r);
               },
               {}};
        x.done = [&s, run, updatable](std::string& what) {
            auto* pane = Kit::paneOf<DfbPane>(s, "dfb", true);
            if (!pane || !sameName(pane->currentBlock(), run->block)) return false;
            what = updatable ? run->block + " : " + run->versionBefore + " dans le projet, " + run->libraryVersion + " dans la biblioth\xC3\xA8que"
                             : run->block + " est choisi";
            return true;
        };
        x.showMe = openBlock;
        x.waiting = "J'attends " + name + ", choisi dans la liste\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 3. Mettre a jour.
    {
        Step x{"Mets-le \xC3\xA0 jour",
               updatable ? "Mettre \xC3\xA0 jour, dans la barre de l'onglet : la macro MettreAJourBibliotheque s'ouvre sur ce qui change - "
                           "Aper\xC3\xA7u, puis Appliquer. Elle le fait hors de l'historique : Tout d\xC3\xA9" "faire, \xC3\xA0 la fin, le rend quand "
                           "m\xC3\xAAme. Montre-moi le fait en une commande, que Ctrl+Z reprend."
                         : "Rien \xC3\xA0 mettre \xC3\xA0 jour : " + name + " est d\xC3\xA9j\xC3\xA0 \xC3\xA0 la version de la biblioth\xC3\xA8que (ou libs/ ne l'a pas).",
               [&s](Rect& r) {
                   // Mettre a jour mene a la macro (l'onglet Macros) : son Apercu, puis Appliquer.
                   if (auto* m = macrosShown(s); m && m->step() >= 1 && sameName(macroOf(*m), "MettreAJourBibliotheque")) {
                       const char* part = m->step() == 1 ? "suivant" : m->step() == 2 ? "appliquer" : nullptr;
                       if (part && shownWidget(s, m->findById(m->id() + ".lancer." + part), r)) return true;
                   }
                   if (tool(s, "dfb", DfbPane::AUpdate, r)) return true;
                   return frameOf(s, "dfb", false) == nullptr && nodeRow(s, apiNode(s, NK::DfbFolder), r);
               },
               {}};
        x.done = [&s, run, upToDate, updatable](std::string& what) {
            if (!updatable) {
                what = "rien \xC3\xA0 mettre \xC3\xA0 jour";
                return true;
            }
            if (!upToDate()) return false;
            what = run->block + " est en " + run->libraryVersion;
            return true;
        };
        x.showMe = [&s, run, openBlock, upToDate] {
            auto doc = s.app_.document();
            if (!doc || run->libraryVersion.empty() || upToDate()) return;
            s.app_.apply(std::make_unique<TrailBlockUpdateCommand>(doc, project::SharedLibrary::defaultRoot(), run->block,
                                                                   run->versionBefore, run->libraryVersion));
            openBlock();
            if (upToDate())
                say(s, "Le parcours a mis " + run->block + " \xC3\xA0 jour (" + run->versionBefore + " \xE2\x86\x92 " + run->libraryVersion
                           + "), en une commande : Ctrl+Z la reprend, Tout d\xC3\xA9" "faire aussi.");
        };
        x.waiting = "J'attends " + name + " en " + (updatable ? to : from) + " (Mettre \xC3\xA0 jour, puis Appliquer)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 4. Ses instances.
    {
        Step x{"V\xC3\xA9rifie ses " + plural(run->instances, "instance", "instances"),
               "Reviens \xC3\xA0 API \xE2\x80\xBA Blocs DFB, " + name + " : ses instances sont dessous (" + (run->instanceList.empty() ? std::string("aucune") : run->instanceList)
                   + "), chacune avec les sections qui l'appellent. "
                   + (updatable ? "Elles ont toutes pris la nouvelle version : le type est le m\xC3\xAAme pour toutes."
                                : "Une mise \xC3\xA0 jour les toucherait toutes d'un coup : le type est le m\xC3\xAAme pour toutes."),
               [&s](Rect& r) {
                   if (auto* pane = Kit::paneOf<DfbPane>(s, "dfb", true)) return shownWidget(s, &pane->instances(), r);
                   return nodeRow(s, apiNode(s, NK::DfbFolder), r);
               },
               {}};
        x.done = [&s, run, upToDate, updatable](std::string& what) {
            auto* pane = Kit::paneOf<DfbPane>(s, "dfb", true);
            if (!pane || !sameName(pane->currentBlock(), run->block) || (updatable && !upToDate())) return false;
            auto p = s.app_.project();
            const std::size_t n = p ? project::usage::instancesOf(*p, run->block, project::usage::Where(*p)).size() : run->instances;
            what = run->block + " " + (p ? blockVersion(*p, run->block) : std::string{}) + " : " + plural(n, "instance", "instances")
                 + (run->instanceList.empty() ? std::string{} : " (" + run->instanceList + ")");
            return true;
        };
        x.showMe = openBlock;
        x.waiting = "J'attends " + name + ", choisi dans API \xE2\x80\xBA Blocs DFB\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 5. Garder, ou tout defaire.
    {
        Step x{"Garde-la, ou d\xC3\xA9" "fais tout",
               updatable ? "La mise \xC3\xA0 jour est faite. Tout d\xC3\xA9" "faire remet " + name + " en " + from + ", ses instances avec ; Garder "
                           "la laisse en " + to + "."
                         : "Tu sais o\xC3\xB9 regarder : la colonne Biblioth\xC3\xA8que, Mettre \xC3\xA0 jour, les instances. Le jour o\xC3\xB9 libs/ "
                           "aura une version plus r\xC3\xA9" "cente, le tableau de bord le dira (\xC2\xAB \xC3\x80 regarder \xC2\xBB).",
               {}, {}};
        x.done = [&s, run](std::string& what) {
            auto p = s.app_.project();
            const std::string now = p ? blockVersion(*p, run->block) : std::string{};
            what = !run->libraryVersion.empty() && now == run->libraryVersion ? run->block + " est en " + now + ", ses instances avec"
                 : run->block + " est en " + (now.empty() ? std::string("?") : now);
            return true;
        };
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ Ranger l'ordre d'execution
std::vector<Step> MainAnalysisScreen::ApiTrails::Kit::order(Screen& s, const std::shared_ptr<ApiRun>& run) {
    std::vector<Step> st;
    const bool any = !run->reader.empty();
    const std::string task = run->task.empty() ? std::string("MAST") : run->task;
    // La barre de l'onglet Ordre : on ecoute ses clics sur Verifier l'ordre (une
    // fois par barre ; un onglet rouvert en a une nouvelle).
    const auto watch = [&s, run] {
        auto* f = frameOf(s, "ordre", false);
        if (!f) return;
        const auto& signal = f->tools().triggered;
        if (run->watched.lock() == signal) return;
        run->watched = signal;
        std::weak_ptr<ApiRun> weak = run;
        run->links += signal->connect([weak](int action) {
            if (auto r = weak.lock(); r && action == ExecutionOrderPane::ACheck) r->checked = true;
        });
    };
    // 1. L'onglet.
    {
        Step x{"Ouvre l'ordre d'ex\xC3\xA9" "cution",
               "API \xE2\x80\xBA Ordre d'ex\xC3\xA9" "cution : les entr\xC3\xA9" "es de " + task + " dans l'ordre o\xC3\xB9 elles tournent, ce que chacune \xC3\xA9" "crit et lit.",
               row(s, NK::ExecOrderFolder), reveal(s, NK::ExecOrderFolder)};
        x.done = [&s](std::string& what) {
            if (!frameOf(s, "ordre", true)) return false;
            what = "l'onglet Ordre d'ex\xC3\xA9" "cution est ouvert";
            return true;
        };
        x.showMe = [&s] { s.openApiPane("ordre"); };
        x.waiting = "J'attends l'onglet Ordre d'ex\xC3\xA9" "cution\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 2. Une lecture avant l'ecriture.
    {
        const std::string body = any
            ? "V\xC3\xA9rifier l'ordre, dans la barre : la premi\xC3\xA8re entr\xC3\xA9" "e qui lit une variable avant qu'elle soit \xC3\xA9" "crite est choisie "
              "(un ! devant son nom, la colonne de droite le dit). Ici : " + run->reader + " lit " + run->variable
              + ", \xC3\xA9" "crite plus loin par " + run->writer + " (rang " + std::to_string(run->writerRank) + ")."
            : "V\xC3\xA9rifier l'ordre, dans la barre : il dit les variables lues avant d'\xC3\xAAtre \xC3\xA9" "crites. Dans " + task
              + ", il n'y en a pas : l'ordre est d\xC3\xA9j\xC3\xA0 bon.";
        Step x{"Trouve une lecture avant l'\xC3\xA9" "criture", body,
               [&s](Rect& r) {
                   if (tool(s, "ordre", ExecutionOrderPane::ACheck, r)) return true;
                   return nodeRow(s, apiNode(s, NK::ExecOrderFolder), r);
               },
               {}};
        x.done = [&s, run](std::string& what) {
            auto* pane = Kit::paneOf<ExecutionOrderPane>(s, "ordre", true);
            if (!pane) return false;
            const auto& late = lateNow(s, *run);
            if (late.empty()) {
                run->reader.clear();
                what = "aucune lecture avant l'\xC3\xA9" "criture : l'ordre est d\xC3\xA9j\xC3\xA0 bon";
                return true;
            }
            const auto rows = pane->table().selectedModelRows();
            if (rows.empty()) return false;
            const std::size_t rank = static_cast<std::size_t>(rows.front()) + 1;
            for (const auto& l : late)
                if (l.readerRank == rank) {
                    run->reader = l.reader;
                    run->variable = l.variable;
                    run->writer = l.writer;
                    run->writerRank = l.writerRank;
                    what = l.reader + " (rang " + std::to_string(l.readerRank) + ") lit " + l.variable + " avant " + l.writer
                         + " (rang " + std::to_string(l.writerRank) + ")";
                    return true;
                }
            return false;
        };
        x.showMe = [&s] {
            s.openApiPane("ordre");
            if (auto* pane = Kit::paneOf<ExecutionOrderPane>(s, "ordre", false)) pane->runAction(ExecutionOrderPane::ACheck);
        };
        x.waiting = "J'attends une entr\xC3\xA9" "e choisie qui lit avant l'\xC3\xA9" "criture (V\xC3\xA9rifier l'ordre)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 3. La ranger.
    {
        const std::string body = any
            ? "Glisse la ligne " + run->reader + " juste apr\xC3\xA8s " + run->writer + " (ou Placer apr\xC3\xA8s, dans la barre) : elle lira "
              "la valeur de ce cycle-ci. Ctrl+Z la remet."
            : "Rien \xC3\xA0 ranger ici. Pour d\xC3\xA9placer une entr\xC3\xA9" "e : glisse sa ligne, ou Monter, Descendre, D\xC3\xA9placer vers\xE2\x80\xA6";
        Step x{"Glisse-la au bon rang", body,
               [&s, run](Rect& r) {
                   if (auto* pane = paneOnScreen<ExecutionOrderPane>(s, "ordre")) {
                       if (!run->reader.empty() && findTableRow(&pane->table(), run->reader, r)) return true;
                       return shownWidget(s, &pane->table(), r);
                   }
                   return nodeRow(s, apiNode(s, NK::ExecOrderFolder), r);
               },
               {}};
        x.done = [&s, run](std::string& what) {
            if (run->reader.empty()) {
                what = "rien \xC3\xA0 ranger";
                return true;
            }
            for (const auto& l : lateNow(s, *run))
                if (sameName(l.reader, run->reader) && sameName(l.variable, run->variable)) return false;
            auto p = s.app_.project();
            if (!p) return false;
            const auto entries = project::api::entriesOf(*p, run->task);
            std::size_t rank = 0;
            for (std::size_t i = 0; i < entries.size() && rank == 0; ++i)
                if (sameName(entries[i].name, run->reader)) rank = i + 1;
            if (rank == 0) return false;                   // plus dans la tache : pas un rangement
            what = run->reader + " est au rang " + std::to_string(rank) + " : " + run->variable + " est \xC3\xA9" "crite avant";
            return true;
        };
        x.showMe = [&s, run] {
            s.openApiPane("ordre");
            auto* pane = Kit::paneOf<ExecutionOrderPane>(s, "ordre", false);
            if (!pane || run->reader.empty()) return;
            const auto& entries = pane->entries();
            std::size_t from = kNpos, writer = kNpos;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (sameName(entries[i].name, run->reader)) from = i;
                if (sameName(entries[i].name, run->writer)) writer = i;
            }
            if (from == kNpos || writer == kNpos || entries.empty()) return;
            // Juste apres l'ecrivain (le rang compte une fois le lecteur retire), comme Placer apres.
            const std::size_t to = writer > from ? writer : writer + 1;
            (void)pane->moveEntry(from, std::min(to, entries.size() - 1));
        };
        x.waiting = any ? "J'attends " + run->reader + " apr\xC3\xA8s " + run->writer + "\xE2\x80\xA6" : std::string("\xE2\x80\xA6");
        st.push_back(std::move(x));
    }
    // 4. Verifier.
    {
        Step x{"V\xC3\xA9rifie l'ordre",
               "V\xC3\xA9rifier l'ordre, encore : " + (any ? run->reader + " n'est plus dans la liste. " : std::string{})
                   + "S'il en reste, chacune se range de la m\xC3\xAAme fa\xC3\xA7on - ou reste, si c'est voulu (une m\xC3\xA9moire du cycle d'avant). "
                     "Ensuite : garder le nouvel ordre, ou tout d\xC3\xA9" "faire.",
               [&s](Rect& r) {
                   if (tool(s, "ordre", ExecutionOrderPane::ACheck, r)) return true;
                   return nodeRow(s, apiNode(s, NK::ExecOrderFolder), r);
               },
               [run, watch] {
                   run->checked = false;
                   watch();
               }};
        x.done = [&s, run, watch](std::string& what) {
            watch();                                       // l'onglet ouvert depuis
            if (!run->checked) return false;
            const auto n = lateNow(s, *run).size();
            what = n == 0 ? "l'ordre est bon : aucune lecture avant l'\xC3\xA9" "criture"
                          : "l'ordre est v\xC3\xA9rifi\xC3\xA9 : " + plural(n, "lecture avant l'\xC3\xA9" "criture reste", "lectures avant l'\xC3\xA9" "criture restent");
            return true;
        };
        x.showMe = [&s, run] {
            s.openApiPane("ordre");
            if (auto* pane = Kit::paneOf<ExecutionOrderPane>(s, "ordre", false)) pane->runAction(ExecutionOrderPane::ACheck);
            run->checked = true;
        };
        x.waiting = "J'attends V\xC3\xA9rifier l'ordre\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ Lancer une macro ==
std::vector<Step> MainAnalysisScreen::ApiTrails::Kit::macro(Screen& s, const std::shared_ptr<ApiRun>& run) {
    std::vector<Step> st;
    const std::string name = run->macro;
    // L'onglet Macros a l'ecran, sur CETTE macro (sa fiche ou son formulaire).
    const auto shownPane = [&s, name]() -> MacrosPane* {
        auto* m = macrosShown(s);
        return m && sameName(macroOf(*m), name) ? m : nullptr;
    };
    const auto macroRow = [&s, name](Rect& r) {
        if (s.treeModel_) {
            const auto node = s.treeModel_->macroNode(name);
            if (node != ui::kInvalidNode && nodeRow(s, node, r)) return true;
        }
        return nodeRow(s, apiNode(s, NK::MacroFolder), r);
    };
    const auto button = [&s, shownPane](const char* part, Rect& r) {
        auto* m = shownPane();
        return m && shownWidget(s, m->findById(m->id() + ".lancer." + part), r);
    };
    // 1. La choisir.
    {
        Step x{"Ouvre l'onglet Macros, choisis " + name,
               "API \xE2\x80\xBA Macros (ou Affichage \xE2\x80\xBA Onglet Macros) : les macros rang\xC3\xA9" "es par sorte. Choisis " + name + " : sa fiche dit "
               "ce qu'elle lit, ce qu'elle produit, ce qu'elle va te demander.",
               [&s, macroRow](Rect& r) {
                   if (auto* m = macrosShown(s)) return shownWidget(s, &m->tree(), r);
                   return macroRow(r);
               },
               [&s, name] {
                   const auto node = s.treeModel_ ? s.treeModel_->macroNode(name) : ui::kInvalidNode;
                   if (node == ui::kInvalidNode || !s.revealTreeNode(node)) (void)s.revealTreeNode(apiNode(s, NK::MacroFolder));
               }};
        x.done = [shownPane, name](std::string& what) {
            if (!shownPane()) return false;
            what = name + " est choisie : sa fiche est \xC3\xA0 droite";
            return true;
        };
        x.showMe = [&s, name] { s.openMacros(name, false); };
        x.waiting = "J'attends " + name + ", choisie dans l'onglet Macros\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 2. Le formulaire.
    {
        Step x{"Lance-la : le formulaire",
               "Lancer (ou Entr\xC3\xA9" "e, ou un double-clic) : le formulaire remplace la fiche. 1, les questions : le classeur "
               "(glisse-le, ou Parcourir\xE2\x80\xA6), ce qu'il faut importer. Chaque r\xC3\xA9ponse est retenue pour la prochaine fois.",
               [&s, macroRow](Rect& r) {
                   if (auto* m = macrosShown(s); m && onScreen(s, m)) {
                       r = m->tools().rectOf(MacrosPane::ALaunch);
                       return !r.empty();
                   }
                   return macroRow(r);
               },
               {}};
        x.done = [shownPane, name](std::string& what) {
            auto* m = shownPane();
            if (!m || m->step() < 1) return false;
            what = "le formulaire de " + name + " est ouvert";
            return true;
        };
        x.showMe = [&s, name] { s.openMacros(name, true); };
        x.waiting = "J'attends le formulaire de " + name + " (Lancer)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 3. L'apercu.
    {
        Step x{"Regarde l'aper\xC3\xA7u",
               "Aper\xC3\xA7u : la macro tourne pour de vrai, puis d\xC3\xA9" "fait tout. Le compte rendu dit ce qu'elle va cr\xC3\xA9" "er ou changer "
               "(variables, sections, lignes) ; une r\xC3\xA9ponse chang\xC3\xA9" "e le refait. Sans classeur, il le dit : r\xC3\xA9ponds d'abord.",
               [button](Rect& r) { return button("suivant", r); }, {}};
        x.done = [shownPane, name](std::string& what) {
            auto* m = shownPane();
            if (!m || m->step() < 2) return false;
            what = "l'aper\xC3\xA7u de " + name + " est \xC3\xA0 l'\xC3\xA9" "cran";
            return true;
        };
        x.showMe = [&s, name] {
            auto* m = s.macrosPane();
            if (!m || m->step() < 1 || !sameName(macroOf(*m), name)) {
                s.openMacros(name, true);
                m = s.macrosPane();
            }
            if (m) m->goPreview();
        };
        x.waiting = "J'attends l'aper\xC3\xA7u (Aper\xC3\xA7u \xE2\x86\x92)\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 4. Appliquer : une commande de plus dans l'historique - ou aucune, si
    //    l'apercu n'avait rien a changer (le projet etait deja a jour).
    {
        Step x{"Applique",
               "Appliquer : ce que l'aper\xC3\xA7u a montr\xC3\xA9 est fait, en UNE commande de l'historique. La barre d'\xC3\xA9tat dit le compte.",
               [button](Rect& r) { return button("appliquer", r); },
               [&s, run] {
                   run->serialBefore = topSerial(s);
                   auto* m = s.macrosPane();
                   run->macroStepBefore = m ? m->step() : 0;
                   run->nothingApplied = false;
               }};
        x.done = [&s, run, shownPane, name](std::string& what) {
            auto* m = shownPane();
            if (!m || m->step() < 3) return false;
            const auto top = topSerial(s);
            if (top != run->serialBefore) {
                run->applied = top;
                run->nothingApplied = false;
                what = "\xC2\xAB " + s.app_.commands().undoLabel() + " \xC2\xBB : un seul pas dans l'historique";
                return true;
            }
            // Appliquee sous nos yeux sans commande : il n'y avait rien a changer.
            if (run->macroStepBefore >= 3) return false;
            run->applied = 0;
            run->nothingApplied = true;
            what = name + " est appliqu\xC3\xA9" "e : rien \xC3\xA0 changer, le projet \xC3\xA9tait d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour";
            return true;
        };
        x.showMe = [&s, name] {
            auto* m = s.macrosPane();
            if (!m || m->step() < 1 || !sameName(macroOf(*m), name)) {
                s.openMacros(name, true);
                m = s.macrosPane();
            }
            if (!m) return;
            if (m->step() < 2) m->goPreview();
            (void)m->applyNow();
        };
        x.waiting = "J'attends Appliquer\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    // 5. Un seul Ctrl+Z.
    {
        Step x{"Un seul Ctrl+Z",
               "Tout ce que la macro a fait est UN pas : Ctrl+Z le reprend d'un coup, Ctrl+Y le remet. Essaie.",
               [&s](Rect& r) { return barPart(s, "annuler", r); }, {}};
        x.done = [&s, run, name](std::string& what) {
            if (run->nothingApplied) {
                what = "rien \xC3\xA0 d\xC3\xA9" "faire : " + name + " n'avait rien chang\xC3\xA9 ; la prochaine fois, un seul Ctrl+Z";
                return true;
            }
            const auto& stack = s.app_.commands();
            bool undone = false;
            if (run->applied != 0) {
                for (const auto& e : stack.undone())
                    if (e.info.serial == run->applied) undone = true;
            } else {
                undone = stack.canRedo() && lower(stack.redoLabel()).find(lower(name)) != std::string::npos;
            }
            if (!undone) return false;
            what = name + " est d\xC3\xA9" "faite d'un coup (Ctrl+Y la remet)";
            return true;
        };
        x.showMe = [&s] { (void)s.app_.actions().trigger("edit.undo", s.app_.commands()); };
        x.waiting = "J'attends Ctrl+Z\xE2\x80\xA6";
        st.push_back(std::move(x));
    }
    return st;
}

// ============================================================ les parcours ======
// ---- Lot API 8 : didacticiels et aide ---- les parcours du lot 7, puis ceux
// du lot 8 (TutorialsLot8.hpp, marques NOUVEAU sur leur carte).
const std::vector<ApiTrailDef>& MainAnalysisScreen::ApiTrails::all() {
    static const std::vector<ApiTrailDef> defs = [] {
        std::vector<ApiTrailDef> v = kDefs;
        for (const auto& t : lot8::trails()) v.push_back({t.key, t.title, t.kind, t.steps, t.minutes, t.icon});
        return v;
    }();
    return defs;
}

const ApiTrailDef* MainAnalysisScreen::ApiTrails::find(std::string_view key) {
    for (const auto& d : all())
        if (key == d.key) return &d;
    return nullptr;
}
// ---- fin Lot API 8 : didacticiels et aide ----

std::string MainAnalysisScreen::ApiTrails::prefix(const MainAnalysisScreen& s, std::string_view key) {
    return "didacticiel." + std::string(key) + "." + projectTag(Kit::projectWhere(s));
}

std::string MainAnalysisScreen::ApiTrails::today() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
    return b;
}

MainAnalysisScreen::ApiTrails::Built MainAnalysisScreen::ApiTrails::build(MainAnalysisScreen& s, const std::string& key) {
    Built out;
    if (!find(key)) return out;
    auto p = s.app_.project();
    if (!p) {
        out.refusal = "ouvre d'abord un projet.";
        return out;
    }
    auto run = std::make_shared<ApiRun>();
    run->key = key;
    if (key == "api-decouvrir") out.steps = Kit::discover(s);
    else if (key == "api-variable") {
        out.steps = Kit::variable(s);
        const std::string var = kCounter;
        // La simulation garde le forcage (ce n'est pas le projet) ; et, le
        // programme defait, elle tournerait encore sur la section de l'exercice.
        out.beforeUndo = [&s, var] {
            if (auto* rt = Kit::runtime(s)) (void)rt->unforce(var);
        };
        out.afterUndo = [&s, var] {
            auto project = s.app_.project();
            auto* rt = Kit::runtime(s);
            if (!project || !rt || !rt->known(var) || globalVariable(*project, var) != domain::kNoIndex) return;
            // ---- Lot API 8 : le moteur de simulation (plus de retour au cycle 0) ----
            //  Le programme defait est pris tout de suite (modification en ligne :
            //  le cycle continue, la variable et la section du parcours s'en vont).
            auto& host = s.app_.simulation();
            const bool online = host.state() == SimulationHost::State::Running || host.state() == SimulationHost::State::Paused;
            (void)host.attach(project);
            if (auto* sim = Kit::hmiSim(s)) sim->refreshNow();
            s.refreshSimulationIndicator();
            if (host.stale())
                Kit::say(s, "Tout est d\xC3\xA9" "fait ; la simulation n'a pas encore pris le programme d'avant le parcours (Simulation \xE2\x80\xBA Automate dit pourquoi).");
            else if (online)
                Kit::say(s, "Tout est d\xC3\xA9" "fait ; la simulation continue sur le programme d'avant le parcours, sans repartir du cycle 0.");
            else
                Kit::say(s, "Tout est d\xC3\xA9" "fait ; la simulation est pr\xC3\xAAte sur le programme d'avant le parcours.");
        };
        // Forcee et rien d'autre a defaire (repris dans une autre session) :
        // Tout defaire la deforce quand meme.
        out.pending = [&s, var] {
            auto* rt = Kit::runtime(s);
            return rt && rt->isForced(var);
        };
    } else if (key == "api-ihm-table") {
        Kit::pickHmi(s, *run);
        if (!s.app_.hmi() || run->hmiVar.empty()) {
            out.refusal = "ce parcours a besoin d'une IHM qui a au moins une variable IHM (IHM \xE2\x80\xBA Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM).";
            return out;
        }
        out.steps = Kit::hmiTable(s, run);
    } else if (key == "api-bloc") {
        Kit::pickBlock(s, *run);
        if (run->block.empty()) {
            out.refusal = "ce projet n'a pas de bloc DFB \xC3\xA0 mettre \xC3\xA0 jour.";
            return out;
        }
        // Le projet tel qu'au debut : la macro met le bloc a jour HORS de
        // l'historique, et Tout defaire doit pouvoir le rendre quand meme.
        if (!run->libraryVersion.empty()) run->snapshot = std::make_shared<domain::Project>(*p);
        out.steps = Kit::block(s, run);
        out.afterUndo = [&s, run] {
            auto doc = s.app_.document();
            if (!doc || !run->snapshot || blockVersion(*doc, run->block) == run->versionBefore) return;
            *doc = *run->snapshot;
            s.onApiRequest("rafraichir");
            Kit::say(s, run->block + " est revenu en " + run->versionBefore + " : la macro l'avait mis \xC3\xA0 jour hors de l'historique, "
                                     "l'\xC3\xA9tat du d\xC3\xA9" "but du parcours est remis.");
        };
        // Mis a jour par la macro, la pile n'a rien de neuf : Tout defaire est
        // propose quand meme, il rend l'etat du debut.
        out.pending = [&s, run] {
            auto doc = s.app_.document();
            return doc && run->snapshot && blockVersion(*doc, run->block) != run->versionBefore;
        };
    } else if (key == "api-ordre") {
        Kit::pickOrder(s, *run);
        out.steps = Kit::order(s, run);
    } else if (key == "api-macro") {
        Kit::pickMacro(s, *run);
        bool known = false;
        if (const auto library = s.apiLibrary())
            for (const auto& item : library->items())
                known = known || (item.kind == project::LibraryItemKind::Macro && sameName(item.name, run->macro));
        if (!known) {
            out.refusal = "la biblioth\xC3\xA8que (libs/) n'a pas de macro \xC3\xA0 lancer (" + run->macro + ", par exemple).";
            return out;
        }
        out.steps = Kit::macro(s, run);
    }
    // ---- Lot API 8 : didacticiels et aide ----
    else if (lot8::find(key)) {
        out.steps = Lot8::steps(s, key, out.refusal);
        if (!out.refusal.empty()) return out;
    }
    // ---- fin Lot API 8 : didacticiels et aide ----
    // Les connexions du parcours (la barre de l'onglet Ordre) partent avec lui.
    out.closed = [run] { run->links.clear(); };
    // Reprendre, dans l'onglet : le dernier parcours lance.
    s.app_.settings().set(Kit::lastTrailKey(s), key);
    return out;
}

std::vector<TrailCard> MainAnalysisScreen::ApiTrails::cards(MainAnalysisScreen& s) {
    // Les resumes citent le projet (CAPTEUR, PURGE, ImporterClasseur) : relus
    // quand il change, pas a chaque rafraichissement.
    struct Cache {
        const void*                        project{nullptr};
        std::uint64_t                      revision{~std::uint64_t{0}};
        std::map<std::string, std::string> summaries;
    };
    static Cache cache;
    const auto p = s.app_.project();
    const auto revision = s.app_.commands().revision();
    if (cache.project != p.get() || cache.revision != revision) {
        cache.project = p.get();
        cache.revision = revision;
        cache.summaries.clear();
        for (const auto& d : all()) cache.summaries[d.key] = p ? Kit::summary(s, d.key) : std::string{};   // Lot API 8 : all()
    }
    std::vector<TrailCard> out;
    const auto& settings = s.app_.settings();
    const std::string running = s.runningTrail();
    for (const auto& d : all()) {   // Lot API 8 : didacticiels et aide (all() : + ceux du lot 8)
        TrailCard c;
        c.key = d.key;
        c.title = d.title;
        c.kind = d.kind;
        c.summary = cache.summaries[d.key];
        c.steps = d.steps;
        c.minutes = d.minutes;
        c.icon = d.icon;
        const std::string pre = prefix(s, d.key);
        c.reached = std::clamp(settings.getInt(pre + ".etape", 0), 0, d.steps);
        c.done = settings.getBool(pre + ".fait", false);
        c.doneOn = c.done ? settings.getString(pre + ".date") : std::string{};
        c.running = running == d.key;
        c.isNew = lot8::find(d.key) != nullptr;   // Lot API 8 : NOUVEAU
        out.push_back(std::move(c));
    }
    return out;
}

void MainAnalysisScreen::ApiTrails::refreshPage(MainAnalysisScreen& s) {
    if (auto* frame = Kit::frameOf(s, "didacticiel", false))
        if (auto* trails = dynamic_cast<ApiTrailsPage*>(&frame->content())) trails->refresh();
}

// ============================================================ l'onglet ==========
void MainAnalysisScreen::openApiTutorial(const std::string& trail) {
    if (!centre_) return;
    // ---- Lot API 8 : didacticiels et aide ---- Aide > Nouveautes du lot 8.
    if (trail == "nouveautes-lot8") {
        ApiTrails::Lot8::openNews(*this);
        return;
    }
    // ---- fin Lot API 8 : didacticiels et aide ----
    // Le centre doit etre a l'ecran, sinon l'onglet s'ouvre dans un panneau
    // cache (comme Affichage > Panneaux a afficher, et F9).
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
    ui::Widget* page = apiTab("didacticiel");
    const int at = page ? centre_->indexOf(page) : -1;
    if (at >= 0) {
        centre_->setCurrentIndex(static_cast<std::size_t>(at));
    } else {
        apiTabs_.erase("didacticiel");
        auto content = std::make_unique<ApiTrailsPage>("analysis.api.didacticiel.volet", [this] { return ApiTrails::cards(*this); });
        paneLinks_ += content->cards().startRequested->connect([this](const std::string& key, bool resume) { startTrail(key, resume); });
        auto frame = std::make_unique<ApiFrame>("analysis.api.didacticiel", std::move(content));
        auto& tools = frame->tools();
        tools.add(PVisit, HmiGlyph::Eye, "Commencer la visite : D\xC3\xA9" "couvrir l'API (14 \xC3\xA9tapes)", "D\xC3\xA9" "couvrir l'API");
        tools.add(PResume, HmiGlyph::Play, "Reprendre le dernier parcours, \xC3\xA0 l'\xC3\xA9tape o\xC3\xB9 tu \xC3\xA9tais", "Reprendre");
        tools.separator();
        tools.add(PHmiTrails, HmiGlyph::View, "Les parcours de l'IHM, dans son aide", "Didacticiel de l'IHM");
        tools.add(PHelp, HmiGlyph::Help, "L'aide (F1)", "Aide");
        const auto last = [this] { return app_.settings().getString(ApiTrails::Kit::lastTrailKey(*this)); };
        tools.setEnabledWhen(PResume, [this, last] {
            const auto key = last();
            const auto* d = ApiTrails::find(key);
            if (!d || runningTrail() == key) return false;
            const int reached = app_.settings().getInt(ApiTrails::prefix(*this, key) + ".etape", 0);
            return reached > 0 && reached < d->steps;
        });
        tools.setEnabledWhen(PHmiTrails, [this] { return app_.hmi() != nullptr; });
        paneLinks_ += tools.triggered->connect([this, last](int action) {
            switch (action) {
                case PVisit: startTrail("api-decouvrir", false); break;
                case PResume:
                    if (const auto key = last(); ApiTrails::find(key)) startTrail(key, true);
                    break;
                case PHmiTrails: openHmiHelp("didacticiel"); break;
                default: (void)app_.actions().trigger("help.open", app_.commands()); break;
            }
        });
        frame->setHint("Le didacticiel retient o\xC3\xB9 tu en es, projet par projet \xC2\xB7 le m\xC3\xAAme moteur que celui de l'IHM (ses parcours : "
                       "Didacticiel de l'IHM)");
        auto* raw = frame.get();
        const auto index = centre_->addTab(TabControl::Tab{"API \xC2\xB7 Didacticiel", Icon::Info, true, false}, std::move(frame));
        apiTabs_["didacticiel"] = raw;
        centre_->setCurrentIndex(index);
    }
    ApiTrails::refreshPage(*this);
    if (trail.empty()) return;
    if (const auto* d = ApiTrails::find(trail)) {
        const int reached = app_.settings().getInt(ApiTrails::prefix(*this, trail) + ".etape", 0);
        startTrail(trail, reached > 0 && reached < d->steps);
    }
}

void MainAnalysisScreen::requestApiTutorial(std::string trail) { pendingTutorial() = std::move(trail); }

void MainAnalysisScreen::takePendingApiTutorial() {
    auto& pending = pendingTutorial();
    if (!pending || !app_.project() || !centre_) return;
    const std::string trail = *pending;
    pending.reset();
    openApiTutorial(trail);
}

// ---- Lot API 8 : didacticiels et aide ----
//  L'ONGLET "API . Nouveautes du lot 8" (Aide > Nouveautes du lot 8) : les
//  cartes des parcours du lot 8 seules, la meme page que le didacticiel (ses
//  boutons Commencer / Reprendre / Refaire lancent le parcours), et ce qui n'a
//  pas de parcours dans l'encadre. Les parcours restent aussi dans l'onglet
//  "API . Didacticiel", marques NOUVEAU (le script "parcours api-..." les y trouve).
void MainAnalysisScreen::ApiTrails::Lot8::openNews(MainAnalysisScreen& s) {
    if (!s.centre_) return;
    if (s.panels_.size() > 2 && !s.panels_[2].box->isChecked()) s.panels_[2].box->setState(Checkbox::State::Checked);
    ui::Widget* page = s.apiTab("nouveautes");
    const int at = page ? s.centre_->indexOf(page) : -1;
    if (at >= 0) {
        s.centre_->setCurrentIndex(static_cast<std::size_t>(at));
        return;
    }
    s.apiTabs_.erase("nouveautes");
    auto content = std::make_unique<ApiTrailsPage>("analysis.api.nouveautes.volet", [&s] {
        std::vector<TrailCard> out;
        for (auto& c : ApiTrails::cards(s))
            if (c.isNew) out.push_back(std::move(c));
        return out;
    });
    HmiTrailCards::Style style = content->cards().style();
    style.title = "Nouveaut\xC3\xA9s de la 1.8.0";
    style.noteTitle = "Aussi dans la 1.8.0.";
    style.note = "Les exports demandent o\xC3\xB9 enregistrer (le dossier du projet est propos\xC3\xA9, le bouton \xE2\x80\xA6 en choisit un "
                 "autre). Les dialogues s'ouvrent dans la fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e, l\xC3\xA0 o\xC3\xB9 tu regardes. F5 "
                 "Simuler / Continuer, Maj+F5 Arr\xC3\xAAter, F9 Simulation \xE2\x80\xBA Vue d'ensemble, F10 Section suivante, F2 le "
                 "dialogue Renommer (Aide \xE2\x80\xBA Raccourcis clavier). Et aussi : l'explorateur de fichiers de l'appli (le bouton "
                 "\xE2\x80\xA6), l'arbre du projet (le filtre Ctrl+Maj+F, le rail, \xC3\x89pingl\xC3\xA9s et R\xC3\xA9" "cents), les "
                 "bandeaux haut et bas (la cloche des notifications, le journal des messages). Le d\xC3\xA9tail de chaque "
                 "nouveaut\xC3\xA9 : Aide (F1), "
                 "\xC2\xAB Nouveaut\xC3\xA9s : la simulation, le d\xC3\xA9" "bogage, les fichiers \xC2\xBB.";
    content->cards().setStyle(std::move(style));
    s.paneLinks_ += content->cards().startRequested->connect([&s](const std::string& key, bool resume) { s.startTrail(key, resume); });
    auto frame = std::make_unique<ApiFrame>("analysis.api.nouveautes", std::move(content));
    auto& tools = frame->tools();
    enum : int { NAll = 1, NKeys, NHelp };
    tools.add(NAll, HmiGlyph::View, "Tous les parcours de l'API (API \xC2\xB7 Didacticiel)", "Tous les parcours");
    tools.add(NKeys, HmiGlyph::Search, "Les raccourcis clavier, \xC3\xA0 jour", "Raccourcis clavier");
    tools.add(NHelp, HmiGlyph::Help, "Les nouveaut\xC3\xA9s de la 1.8.0, dans l'aide (F1)", "Aide");
    s.paneLinks_ += tools.triggered->connect([&s](int action) {
        if (action == NAll) s.openApiTutorial();
        else openHelpAt(s, action == NKeys ? lot8::kShortcutsAnchor : lot8::kNewsAnchor);
    });
    frame->setHint("Chaque nouveaut\xC3\xA9 de la 1.8.0 et son didacticiel \xC2\xB7 les m\xC3\xAAmes parcours que l'onglet Didacticiel (marqu\xC3\xA9s NOUVEAU)");
    auto* raw = frame.get();
    const auto index = s.centre_->addTab(TabControl::Tab{"API \xC2\xB7 Nouveaut\xC3\xA9s de la 1.8.0", Icon::Star, true, false}, std::move(frame));
    s.apiTabs_["nouveautes"] = raw;
    s.centre_->setCurrentIndex(index);
    ApiTrails::refreshPage(s);
    if (auto* f = dynamic_cast<ApiFrame*>(raw))
        if (auto* p = dynamic_cast<ApiTrailsPage*>(&f->content())) p->refresh();
}
// ---- fin Lot API 8 : didacticiels et aide ----

} // namespace app
