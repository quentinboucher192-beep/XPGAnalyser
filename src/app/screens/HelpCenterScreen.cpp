// =============================================================================
//  app/screens/HelpCenterScreen.cpp - 1.11 (chantier T2, tranche 4) : le
//  centre d'aide unique. Voir l'en-tete.
// =============================================================================
#include "HelpCenterScreen.hpp"
#include "HmiExprScreen.hpp"   // integration I111, lien 4 : la page des expressions (T3)

#include "../../core/CallTrail.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../help/CenterPages.hpp"
#include "../../help/CenterSources.hpp"
#include "../../help/CenterTutorials.hpp"   // integration I111 : les sujets pour le registre de T1
#include "../../help/Novelties.hpp"
#include "../../help/ProblemReport.hpp"
#include "../../help/ReleaseNotes.hpp"
#include "../../help/TutorialLaunch.hpp"
#include "../../hmi/HmiExprGuide.hpp"
#include "../../hmi/HmiGuide.hpp"
#include "../../hmi/HmiTutorialDeduce.hpp"
#include "../../project/ImportSchema.hpp"
#include "../../project/LibraryCatalog.hpp"
#include "../../project/SharedLibrary.hpp"
#include "../../ui/HelpDocument.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/HelpArticleView.hpp"
#include "../../ui/widgets/HelpView.hpp"
#include "../App.hpp"
#include "../Dossiers.hpp"
#include "../LibraryHelpModels.hpp"
#include "../NoveltyCenter.hpp"
#include "../Settings.hpp"
#include "../TutorialsLot8.hpp"
#include "../hmi/HmiExprPage.hpp"
#include "../hmi/HmiHelpPane.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>

namespace help::center {
// Recette 1.11 (T2, tranche 15) : definie dans CenterIndex.cpp (la recherche sans
// la limite par groupe).
std::vector<SearchGroup> searchUnlimited(const Index& ix, std::string_view term);
} // namespace help::center

namespace app {
using namespace ui;
namespace hc = help::center;

namespace {

// Recette 1.11 (T2, tranche 15) : un groupe de la recherche n'en montrait que les
// premiers (" Sujets de l'IHM (29) " et 4 lignes), sans le dire. Une ligne
// " ... les N autres " le dit ; un clic montre tout le groupe, jusqu'a ce que le
// mot cherche change (un seul centre a la fois).
struct SearchAll {
    std::string   term;
    std::set<int> kinds;
};
SearchAll& searchAll() {
    static SearchAll s;
    return s;
}
constexpr const char* kAllPrefix = "#tout:";

struct CenterData {
    std::vector<project::CatalogEntry> library;
    hc::Index                          ix;
};

// Recette 1.11 (T3-9, I111) : l'entree de la bibliotheque d'une macro, d'un bloc DFB ou d'un
// type DDT du centre : celle dont il montre l'article (meme nom ; une macro est une macro).
const project::CatalogEntry* libraryEntry(const std::vector<project::CatalogEntry>& library, const hc::Topic& t) {
    if (t.source != hc::Source::Macro && t.source != hc::Source::Block) return nullptr;
    const bool macro = t.source == hc::Source::Macro;
    for (const auto& e : library)
        if (e.name == t.ref && (e.kind == project::CatalogKind::Macro) == macro) return &e;
    return nullptr;
}

// Recette 1.11 (T3-9, I111) : ce que l'article du centre (buildHelpArticle) montre d'une macro,
// d'un bloc ou d'un type DDT, pour le deducteur : ses parametres dans l'ordre du fichier, sans
// les locaux ni les internes (nom ; type, et le sens d'une broche : ses pastilles), et son exemple.
void fillLibraryFacts(help::TopicInfo& i, const project::CatalogEntry& e) {
    for (const auto& d : e.declarations) {
        if (d.isLocal() || d.name.empty() || e.isInternal(d.name)) continue;
        std::string scope;
        for (const char c : d.scope) scope += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        std::string type = d.type;
        const char* sens = scope == "input"  ? "entr\xC3\xA9" "e"
                         : scope == "output" ? "sortie"
                         : scope == "inout"  ? "entr\xC3\xA9" "e/sortie" : "";
        if (*sens != '\0') type += (type.empty() ? "" : ", ") + std::string(sens);
        i.fields.emplace_back(d.name, std::move(type));
    }
    i.example = e.help.example;
}

// Integration I111, lien 2 : les sujets du centre pour le registre des tutoriels de T1
// (help::setTopics). L'adaptateur de T2 (topicFacts) donne le genre de chaque sujet ; un sujet
// du guide et un type d'expression prennent en plus ce que lit le deducteur de T3
// (hmi::tutotopics : le nom francais de la tuile, ses proprietes, les intertitres et leur
// paragraphe, l'exemple et l'erreur courante du type). La cle et le titre restent ceux du centre.
// Recette 1.11 (T3-9) : une macro, un bloc DFB et un type DDT prennent leurs parametres
// (fields) et leur exemple dans la bibliotheque, comme leur article (fillLibraryFacts).
std::vector<help::TopicInfo> tutorialTopics(const hc::Index& ix, const std::vector<project::CatalogEntry>& library) {
    const auto facts = hc::topicFacts(ix);
    const auto& topics = ix.topics();
    std::vector<help::TopicInfo> out;
    out.reserve(facts.size());
    for (std::size_t n = 0; n < facts.size() && n < topics.size(); ++n) {
        const auto& f = facts[n];
        const auto& t = topics[n];
        help::TopicInfo i;
        if (t.source == hc::Source::Guide) {
            if (const auto* g = hmi::guide::topic(t.ref)) i = hmi::tutotopics::fromGuide(*g);
        } else if (t.source == hc::Source::Expression) {
            if (const auto* e = hmi::exprguide::find(t.ref)) i = hmi::tutotopics::fromExprType(*e);
        }
        if (i.key.empty()) {   // macro, bloc, type DDT, page de l'aide generale, page speciale
            i.kind = static_cast<help::TopicKind>(f.kind);
            i.name = f.name;              // T2, tranche 10 : un objet, "Vanne" (le nom de sa tuile)
            i.objectType = f.objectType;  // ... et son genre interne a part, "Valve" (TopicInfo de T1)
            i.variants = f.variants;
            i.places = f.places;
            i.headings = f.headings;
            if (const auto* e = libraryEntry(library, t)) fillLibraryFacts(i, *e);   // T3-9
        }
        i.key = f.key;
        i.title = f.title;
        if (i.summary.empty()) i.summary = t.summary;
        out.push_back(std::move(i));
    }
    return out;
}

const CenterData& centerData() {
    static const CenterData d = [] {
        CenterData out;
        out.library = project::scanLibrary(project::SharedLibrary::defaultRoot());
        hc::Inputs in;
        in.guide   = hc::guideSources();
        in.api     = hc::apiSources();
        in.library = hc::librarySources(out.library);
        // Integration I111, lien 4 : les 11 types d'expression de T3 (hmi::exprguide), plus la
        // liste de secours du centre ; leur sujet reste "expr-<cle>".
        for (const auto& e : hmi::exprguide::all())
            in.expressions.push_back(hc::SourceTopic{std::string(e.key), std::string(e.title), std::string(e.summary), {}, {}, 0});
        out.ix = hc::Index::build(in);
        help::setTopics(tutorialTopics(out.ix, out.library));   // lien 2 : le compteur et les deduits en dependent
        return out;
    }();
    return d;
}

HelpBlock block(HelpBlockKind k, std::string text) {
    HelpBlock b;
    b.kind = k;
    b.text = std::move(text);
    return b;
}

std::filesystem::path reportDataDir() {
    return std::filesystem::path(Settings::defaultPath()).parent_path();
}
help::report::Stamp nowStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min};
}
std::string joinCaps(const std::vector<std::vector<std::string>>& caps) {
    std::string out;
    for (const auto& v : caps) {
        if (!out.empty()) out += "  ou  ";
        for (std::size_t i = 0; i < v.size(); ++i) out += (i ? "+" : "") + ("[" + v[i] + "]");
    }
    return out;
}

// Recette 1.11 (T2-10) : le sujet ouvert d'ailleurs (F1, un lien, la recherche,
// <- ->) est amene en vue dans l'arbre, a l'image suivante (Update), une fois
// l'arbre dispose. ListView n'a pas d'acces a son defilement (DataViews.hpp, que
// tout inclut, n'est pas touche) : la molette, en haut puis jusqu'a la ligne.
// Un clic dans l'arbre ne le fait pas : l'arbre reste ou on l'a mis.
std::string gRevealKey;

} // namespace

// Tranche 8 : le modele de la liste de gauche est garde tant qu'on reste dans
// l'arbre (ListView::setModel remet le defilement a zero) ; la liste le relit a
// chaque dessin.
// Tranche 11 : la duree du tutoriel ("\xE2\x96\xB6 22 s") est la pastille de la
// ligne (CellStyle::badge), calee a droite : un titre long est coupe avant elle,
// au lieu de la perdre derriere ses points de suspension.
class HelpCenterScreen::TreeModel final : public IListModel {
public:
    TreeModel(std::vector<std::string> rows, std::vector<std::string> pills)
        : rows_(std::move(rows)), pills_(std::move(pills)) {}
    void set(std::vector<std::string> rows, std::vector<std::string> pills) {
        rows_ = std::move(rows);
        pills_ = std::move(pills);
    }
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::string text(RowIndex r) const override { return r < rows_.size() ? rows_[r] : std::string{}; }
    [[nodiscard]] CellStyle style(RowIndex r) const override {
        CellStyle st;
        if (r < pills_.size()) st.badge = pills_[r];
        return st;
    }
private:
    std::vector<std::string> rows_;
    std::vector<std::string> pills_;
};

// Les faits de l'appli (tranche 8) : la version, le systeme, les comptes du projet
// ouvert (ni noms, ni valeurs, ni code) et le journal - les 200 dernieres entrees
// de la trace de l'appli (core::trail, celle du rapport de plantage) ; l'apercu et
// le zip n'en gardent que les lignes sans valeurs (help::report::filterLog).
help::report::Facts HelpCenterScreen::reportFacts() const {
    help::report::Facts f;
    f.version = help::news::sessionVersion();
    f.system = help::report::systemName();
    auto& c = f.project;
    if (const auto p = app_.project()) {
        c.open = true;
        c.sections  = static_cast<int>(p->sections.size());
        c.ddts      = static_cast<int>(p->derivedTypes.size());
        c.variables = static_cast<int>(p->variables.size());
        for (const auto& pou : p->pous)
            if (pou.kind == domain::PouKind::FunctionBlockType) ++c.dfbs;
    }
    if (const auto h = app_.hmi()) {
        c.open = true;
        c.views  = static_cast<int>(h->project.views.size());
        c.alarms = static_cast<int>(h->project.alarms.size());
        c.scripts = static_cast<int>(h->project.programs.scripts.size());
        for (const auto& v : h->project.views) {
            c.objects += static_cast<int>(v.objects.size());
            c.scripts += static_cast<int>(v.scripts.size());
        }
    }
    char line[core::trail::kLineMax];
    for (const auto& e : core::trail::recent(200)) {
        const std::size_t n = core::trail::formatEntry(e, line, sizeof line);
        f.log.emplace_back(line, std::min(n, sizeof line));
    }
    return f;
}

std::string HelpCenterScreen::keysTerm() const { return keysSearch_ ? keysSearch_->text() : std::string{}; }

// Les favoris (a chaque bascule) et les recents (en quittant le centre), dans les
// reglages : "sujet:<cle>", comme la bibliotheque garde les siens.
void HelpCenterScreen::saveNavigation(bool all) {
    auto& s = app_.settings();
    s.setList("aide.centre.favoris", nav_.saveFavourites());
    if (all) s.setList("aide.centre.recents", nav_.saveRecents());
    (void)s.save();
}

void HelpCenterScreen::onExit() { saveNavigation(true); }

void HelpCenterScreen::Update(const menu::FrameContext& fc) {
    menu::WidgetMenu::Update(fc);
    // T2-10 : la ligne du sujet ouvert en vue, trois lignes sous le haut de l'arbre.
    if (!gRevealKey.empty() && tree_ && fc.theme && tree_->bounds().h > 0.f && listMode_ == 0) {
        std::size_t at = rows_.size();
        for (std::size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].key == gRevealKey) { at = i; break; }
        if (at < rows_.size()) {
            const float rowH = std::max(1.f, fc.theme->metric.rowHeight);
            const auto b = tree_->bounds();
            const gfx::Point mid{b.x + b.w / 2.f, b.y + b.h / 2.f};
            const bool hidden = (static_cast<float>(at) + 1.f) * rowH > b.h - rowH;
            (void)tree_->dispatch(ui::InputEvent{ui::MouseWheel{mid, 0.f, 1.0e6f, {}}});   // en haut
            if (hidden) {
                const float rows = static_cast<float>(at) - 3.f;
                if (rows > 0.f) (void)tree_->dispatch(ui::InputEvent{ui::MouseWheel{mid, 0.f, -rows / 3.f, {}}});
            }
        }
        gRevealKey.clear();
    }
    // Tranche 9 : les ~35 px sous la carte. HelpView se pose 8 px au-dessus du bloc
    // du titre vise, l'espace qui precede le titre compris (0,8 ligne : voir
    // HelpView::relayout) ; sous l'en-tete du centre, c'est une bande vide. Une fois
    // la page disposee (le saut attend le premier dessin), le TEXTE du titre se pose
    // a 2 px du bord. HelpView n'est pas touche : Screens.hpp inclut son en-tete, et
    // le changer referait une quarantaine de fichiers ; les autres ecrans ne changent pas.
    if (!apiSettle_.empty() && api_ && api_->visible() && api_->document() && !api_->layout().empty()
        && fc.theme) {
        const auto block = api_->document()->blockOf(apiSettle_);
        const auto& lay = api_->layout();
        if (block < lay.size()) {
            const float lead = block == 0 ? 0.f : fc.theme->metric.rowHeight * 0.8f;
            api_->setScroll(lay[block].y + lead - 2.f);
            api_->invalidate();
        }
        apiSettle_.clear();
    }
    // L'en-tete d'une page de l'aide generale (le fil d'Ariane, la carte), mesure
    // au dessin precedent. Tranche 10 : il s'arrete sous la carte. La marge du bas
    // d'un article (kPadBottom, 24 px, dans HelpArticleView.cpp) est coupee : la vue
    // ne defile pas pour ce seul blanc (HelpArticleView::maxScroll), donc pas de
    // barre. Reste l'espace qui suit le bloc de la carte (14 px) : la bande de
    // ~40 px de T2_10 en fait 16. HelpArticleView.hpp n'est pas touche : 17 objets
    // de la construction en dependent (App.cpp, ScriptRunner.cpp...).
    if (!head_ || !pageDock_ || !head_->visible() || head_->contentHeight() < 1.f) return;
    constexpr float kArticlePadBottom = 24.f;
    const float want = std::clamp(std::ceil(head_->contentHeight() - kArticlePadBottom) + 2.f, 40.f, 200.f);
    if (std::abs(want - headExtent_) > 1.f) {
        headExtent_ = want;
        pageDock_->setExtent(*head_, want);
    }
}

HelpCenterScreen::HelpCenterScreen(App& app, std::string menuId, hc::Chapter chapter)
    : menu::WidgetMenu(std::move(menuId)), app_(app), chapter_(chapter) {}

const hc::Index& HelpCenterScreen::index() { return centerData().ix; }

core::Status HelpCenterScreen::buildUi() {
    doc_ = std::make_shared<const HelpDocument>(buildHelp());
    const std::string id = this->id();

    auto root = std::make_unique<DockLayout>(id + ".root");

    // ---- la barre : < >, la recherche (Ctrl+F), l'etoile, le compteur ----
    auto bar = std::make_unique<BoxLayout>(Orientation::Horizontal, id + ".bar");
    bar->setSpacing(6.f);
    back_    = &static_cast<Button&>(bar->addChild(std::make_unique<Button>("\xE2\x80\xB9", id + ".back")));
    forward_ = &static_cast<Button&>(bar->addChild(std::make_unique<Button>("\xE2\x80\xBA", id + ".forward")));
    auto champ = std::make_unique<InputText>(id + ".search");
    champ->setPlaceholder("Rechercher dans toute l'aide (Ctrl+F) : un mot, une touche, une version");
    search_ = &static_cast<InputText&>(bar->addChild(std::move(champ)));
    star_   = &static_cast<Button&>(bar->addChild(std::make_unique<Button>("\xE2\x98\x86", id + ".star")));
    // Tranche 8 : les favoris et les recents, dans la liste de gauche.
    recentsBtn_ = &static_cast<Button&>(bar->addChild(std::make_unique<Button>("R\xC3\xA9" "cents", id + ".recents")));
    // Tranche 9 : ST | C | C++, pour tous les exemples de code (aide.notation).
    {
        static constexpr const char* kNotations[] = {"ST", "C", "C++"};
        static constexpr const char* kIds[]       = {"st", "c", "cpp"};
        for (std::size_t i = 0; i < notationBtn_.size(); ++i)
            notationBtn_[i] = &static_cast<Button&>(bar->addChild(
                std::make_unique<Button>(kNotations[i], id + ".notation." + kIds[i])));
    }
    count_  = &static_cast<Button&>(bar->addChild(std::make_unique<Button>(
                  hc::tutorialCountText(hc::tutorialCount(index())), id + ".count")));
    count_->setStyle(Button::Style::Flat);
    root->dock(std::move(bar), DockLayout::Side::Top, 36.f);

    status_ = &static_cast<StatusBar&>(root->dock(std::make_unique<StatusBar>(id + ".status"),
                                                  DockLayout::Side::Bottom, 24.f));
    tree_ = &static_cast<ListView&>(root->dock(std::make_unique<ListView>(id + ".tree"),
                                               DockLayout::Side::Left, 280.f));

    // ---- la page : l'en-tete, puis le texte de la source ----
    auto page = std::make_unique<DockLayout>(id + ".page");
    pageDock_ = page.get();
    head_ = &static_cast<HelpArticleView&>(page->dock(std::make_unique<HelpArticleView>(id + ".head"),
                                                      DockLayout::Side::Top, 132.f));
    // ---- Raccourcis : la recherche de la page, l'apercu A4, Imprimer (tranche 8) ----
    {
        auto bande = std::make_unique<BoxLayout>(Orientation::Horizontal, id + ".keys");
        bande->setSpacing(8.f);
        bande->setPadding({8.f, 20.f, 4.f, 20.f});
        auto champ2 = std::make_unique<InputText>(id + ".keys.search");
        champ2->setPlaceholder("Chercher un raccourci : une touche (F8, Ctrl+K) ou un mot");
        keysSearch_ = &static_cast<InputText&>(bande->addChild(std::move(champ2)));
        auto& a4 = static_cast<Button&>(bande->addChild(std::make_unique<Button>("Aper\xC3\xA7u A4", id + ".keys.a4")));
        auto& imprimer = static_cast<Button&>(bande->addChild(std::make_unique<Button>("Imprimer\xE2\x80\xA6",
                                                                                         id + ".keys.print")));
        links_ += keysSearch_->textChanged->connect([this](const std::string&) {
            if (current_ == "page-raccourcis") showTopic(current_, false);
        });
        links_ += a4.clicked->connect([this, &a4] {
            keysSheet_ = !keysSheet_;
            a4.setText(keysSheet_ ? "Les tables" : "Aper\xC3\xA7u A4");
            if (current_ == "page-raccourcis") showTopic(current_, false);
        });
        links_ += imprimer.clicked->connect([this] { onLink("raccourcis:imprimer"); });
        keysPanel_ = &page->dock(std::move(bande), DockLayout::Side::Top, 46.f);
        keysPanel_->setVisibility(Visibility::Collapsed);
    }
    // ---- Signaler : le formulaire, au-dessus de l'apercu (tranche 7) ----
    {
        auto form = std::make_unique<BoxLayout>(Orientation::Vertical, id + ".report");
        form->setSpacing(6.f);
        form->setPadding({8.f, 20.f, 6.f, 20.f});   // t, r, b, l : la marge de l'article
        auto& titre = static_cast<Button&>(form->addChild(std::make_unique<Button>(
            "Signaler un probl\xC3\xA8me  \xC2\xB7  rien n'est envoy\xC3\xA9 par le r\xC3\xA9seau", id + ".report.title")));
        titre.setStyle(Button::Style::Flat);
        const auto field = [&](const char* suffix, std::string hint, std::string help::report::Form::*member) {
            // Recette 1.11 (T2-7) : l'etiquette au-dessus du champ ; le texte d'aide du champ
            // disparaissait une fois le champ rempli. Un clic sur l'etiquette y met le curseur.
            auto& etiquette = static_cast<Button&>(form->addChild(std::make_unique<Button>(hint, id + ".report." + suffix + ".label")));
            etiquette.setStyle(Button::Style::Flat);
            auto f = std::make_unique<InputText>(id + ".report." + suffix);
            f->setPlaceholder(std::move(hint));
            auto& ref = static_cast<InputText&>(form->addChild(std::move(f)));
            links_ += etiquette.clicked->connect([this, &ref] { focus().focus(&ref); });
            links_ += ref.textChanged->connect([this, member](const std::string& s) {
                reportForm_.*member = s;
                if (!current_.empty()) showTopic(current_, false);   // l'apercu suit
            });
        };
        // Tranche 11 : l'ordre de la SPEC (D7), de l'apercu et du zip : ce qui s'est
        // passe, comment le refaire, ce que tu attendais.
        field("quoi", "Ce qui s'est pass\xC3\xA9 (obligatoire pour le zip)", &help::report::Form::what);
        field("refaire", "Comment le refaire : les \xC3\xA9tapes, dans l'ordre", &help::report::Form::howTo);
        field("attendu", "Ce que tu attendais", &help::report::Form::expected);
        auto cases = std::make_unique<BoxLayout>(Orientation::Horizontal, id + ".report.pieces");
        cases->setSpacing(18.f);
        const auto box = [&](const char* suffix, std::string label, bool help::report::Form::*member) {
            auto c = std::make_unique<Checkbox>(std::move(label), id + ".report." + suffix);
            c->setState(Checkbox::State::Checked);
            auto& ref = static_cast<Checkbox&>(cases->addChild(std::move(c)));
            links_ += ref.stateChanged->connect([this, member](Checkbox::State s) {
                reportForm_.*member = s == Checkbox::State::Checked;
                if (!current_.empty()) showTopic(current_, false);
            });
        };
        box("version", "La version", &help::report::Form::withVersion);
        box("systeme", "Le syst\xC3\xA8me", &help::report::Form::withSystem);
        box("projet", "Le r\xC3\xA9sum\xC3\xA9 du projet (sans ses donn\xC3\xA9" "es)", &help::report::Form::withProject);
        box("journal", "Le journal", &help::report::Form::withLog);
        form->addChild(std::move(cases));
        auto actions = std::make_unique<BoxLayout>(Orientation::Horizontal, id + ".report.actions");
        actions->setSpacing(8.f);
        const auto bouton = [&](const char* suffix, std::string label, std::string target) {
            auto& b = static_cast<Button&>(actions->addChild(std::make_unique<Button>(std::move(label),
                                                                                       id + ".report." + suffix)));
            links_ += b.clicked->connect([this, target] { onLink(target); });
        };
        bouton("capture", "Joindre une capture", "signaler:capture");
        bouton("zip", "Pr\xC3\xA9parer le zip", "signaler:zip");
        bouton("dossier", "Ouvrir le dossier", "signaler:dossier");
        bouton("chemin", "Copier le chemin", "signaler:chemin");
        bouton("copier", "Copier le texte", "signaler:copier");
        form->addChild(std::move(actions));
        reportPanel_ = &page->dock(std::move(form), DockLayout::Side::Top, 318.f);   // T2-7 : + les trois etiquettes
        reportPanel_->setVisibility(Visibility::Collapsed);
    }
    page_ = &static_cast<HelpArticleView&>(page->dock(std::make_unique<HelpArticleView>(id + ".pane.article"),
                                                      DockLayout::Side::Center, 0.f));
    auto vue = std::make_unique<HelpView>(id + ".view");
    vue->setDocument(doc_);
    api_ = &static_cast<HelpView&>(page->dock(std::move(vue), DockLayout::Side::Center, 0.f));
    api_->setVisibility(Visibility::Collapsed);
    root->dock(std::move(page), DockLayout::Side::Center, 0.f);

    setRoot(std::move(root));

    links_ += tree_->selectionChanged->connect([this](RowIndex r) { onRow(r); });
    links_ += search_->textChanged->connect([this](const std::string&) { rebuildList(); });
    links_ += back_->clicked->connect([this] {
        if (nav_.canBack()) { gRevealKey = index().keyOf(nav_.back()); showTopic(gRevealKey, false); }
    });
    links_ += forward_->clicked->connect([this] {
        if (nav_.canForward()) { gRevealKey = index().keyOf(nav_.forward()); showTopic(gRevealKey, false); }
    });
    links_ += star_->clicked->connect([this] {
        if (current_.empty()) return;
        const bool on = nav_.toggleFavourite(hc::topicTarget(current_));
        status_->setMessage(on ? "Ajout\xC3\xA9 aux favoris" : "Retir\xC3\xA9 des favoris");
        saveNavigation(false);
        refreshBar();
        if (recentsShown_) rebuildList();
    });
    links_ += recentsBtn_->clicked->connect([this] {
        recentsShown_ = !recentsShown_;
        recentsBtn_->setText(recentsShown_ ? "Sommaire" : "R\xC3\xA9" "cents");
        if (recentsShown_ && search_ && !search_->text().empty()) search_->setText("");
        rebuildList();
    });
    for (auto* b : notationBtn_) {
        const std::string n = b->text();
        links_ += b->clicked->connect([this, n] { setNotation(n); });
    }
    links_ += head_->linkActivated->connect([this](const std::string& t) { onLink(t); });
    links_ += page_->linkActivated->connect([this](const std::string& t) { onLink(t); });

    // Tranche 8 : les favoris et les recents d'une session a l'autre (les reglages).
    nav_.restore(app_.settings().getList("aide.centre.recents"), app_.settings().getList("aide.centre.favoris"));

    const auto first = index().ofChapter(chapter_);
    showTopic(first.empty() ? std::string{} : first.front()->key, true);
    return core::ok();
}

void HelpCenterScreen::onEnter() {
    // F1 : la boite aux lettres d'avant (le lieu de l'IHM, la cible de la
    // bibliotheque, l'ancre de l'onglet de l'API) -> le sujet de l'endroit.
    hc::F1Place place;
    std::string libKey;
    if (help::hasPendingTarget()) libKey = index().keyOf(help::takePendingTarget());
    place.guideTopic = app_.takeHelpTopic("");
    place.apiAnchor  = lot8::takePendingHelpAnchor();
    std::string key = index().forF1(place);
    if (key.empty()) key = libKey;
    if (!key.empty()) open(key);
}

void HelpCenterScreen::open(const std::string& key) {
    gRevealKey = key;   // T2-10
    if (!index().find(key)) {
        const auto first = index().ofChapter(chapter_);
        if (first.empty()) return;
        showTopic(first.front()->key, true);
        return;
    }
    if (search_ && !search_->text().empty()) search_->setText("");
    showTopic(key, true);
}

// Tranche 9 : la notation des exemples (ST, C, C++). Elle vaut pour tous les
// exemples, se garde (aide.notation, comme l'aide IHM de la 1.10) ; la page
// ouverte est redessinee dans la nouvelle notation, a la meme place.
void HelpCenterScreen::setNotation(const std::string& n) {
    if (n != "ST" && n != "C" && n != "C++") return;
    auto& s = help::news::session();
    const bool change = (s.helpNotation.empty() ? std::string("ST") : s.helpNotation) != n;
    s.helpNotation = n;
    if (change && !current_.empty()) { keepPlace_ = true; showTopic(current_, false); keepPlace_ = false; }
    refreshBar();
    if (status_ && change) status_->setMessage("Les exemples sont maintenant en " + n);
}

void HelpCenterScreen::refreshBar() {
    {
        const auto& n = help::news::session().helpNotation;
        const std::string active = (n == "C" || n == "C++") ? n : std::string("ST");
        for (auto* b : notationBtn_)
            if (b) b->setStyle(b->text() == active ? Button::Style::Primary : Button::Style::Flat);
    }
    if (back_) back_->setEnabled(nav_.canBack());
    if (forward_) forward_->setEnabled(nav_.canForward());
    if (star_ && !current_.empty())
        star_->setText(nav_.isFavourite(hc::topicTarget(current_)) ? "\xE2\x98\x85" : "\xE2\x98\x86");
}

void HelpCenterScreen::rebuildList() {
    rows_.clear();
    std::vector<std::string> lines;
    std::vector<std::string> pills;   // la pastille de chaque ligne (vide : aucune)
    const std::string term = search_ ? search_->text() : std::string{};
    const bool searching = term.find_first_not_of(' ') != std::string::npos;
    const int mode = searching ? 1 : (recentsShown_ ? 2 : 0);
    if (mode == 2) {
        // Tranche 8 : les favoris, puis les recents (le plus recent en tete).
        const auto groupe = [&](std::string label, const std::vector<help::Target>& liste) {
            std::vector<std::string> keys;
            for (const auto& t : liste) {
                std::string k = index().keyOf(t);
                if (index().find(k)) keys.push_back(std::move(k));
            }
            lines.push_back(std::move(label) + "  (" + std::to_string(keys.size()) + ")");
            rows_.push_back({});
            for (auto& k : keys) {
                lines.push_back("   " + index().find(k)->title);
                rows_.push_back({std::move(k), {}});
            }
        };
        groupe("\xE2\x98\x85 Favoris", nav_.favourites());
        groupe("R\xC3\xA9" "cents", nav_.recents());
    } else if (mode == 0) {
        for (const auto& r : hc::treeRows(index(), state_)) {
            std::string line(static_cast<std::size_t>(r.depth) * 3, ' ');
            if (r.kind != hc::RowKind::Topic) line += r.open ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ";
            line += r.label;
            if (r.kind != hc::RowKind::Topic) line += "  (" + std::to_string(r.count) + ")";
            pills.resize(lines.size());
            pills.push_back(r.pill);   // tranche 11 : a droite, en pastille (TreeModel)
            // Le sujet ouvert : une puce et une espace a la place des trois
            // dernieres espaces du retrait (un sujet a toujours un retrait).
            // Tranche 10 : "• " fait a peu pres leur largeur ; le "●" d'avant,
            // plus large, decalait le texte de 8 px (T2_08, T2_10).
            const std::size_t indent = static_cast<std::size_t>(r.depth) * 3;
            if (r.current && r.kind == hc::RowKind::Topic && indent >= 3 && line.size() > indent)
                line.replace(indent - 3, 3, "\xE2\x80\xA2 ");
            lines.push_back(std::move(line));
            rows_.push_back(r.kind == hc::RowKind::Topic ? Row{r.key, {}} : Row{{}, r.key});
        }
    } else {
        // Recette 1.11 : le compte de la barre d'etat est celui des groupes (tout ce
        // qui est trouve), et un groupe coupe le dit par " ... les N autres ".
        auto& all = searchAll();
        if (all.term != term) { all.term = term; all.kinds.clear(); }
        std::size_t n = 0;
        for (const auto& g : hc::searchUnlimited(index(), term)) {
            lines.push_back(std::string(hc::groupLabel(g.kind)) + "  (" + std::to_string(g.total) + ")");
            rows_.push_back({});
            const bool whole = all.kinds.count(static_cast<int>(g.kind)) > 0;
            const std::size_t shown = whole ? g.hits.size() : std::min(g.hits.size(), hc::groupLimit(g.kind));
            for (std::size_t i = 0; i < shown; ++i) {
                const auto& h = g.hits[i];
                std::string key = h.topic ? h.topic->key : std::string{};
                if (key.empty() && h.shortcut) key = "page-raccourcis";
                lines.push_back("   " + h.title + (h.detail.empty() ? "" : "  \xC2\xB7  " + h.detail));
                rows_.push_back({key, {}});
            }
            if (shown < g.hits.size()) {
                const std::size_t rest = g.hits.size() - shown;
                lines.push_back("   \xE2\x80\xA6 " + (rest > 1 ? "les " + std::to_string(rest) + " autres" : std::string("1 autre"))
                                + " (clic : tout le groupe)");
                rows_.push_back({{}, kAllPrefix + std::to_string(static_cast<int>(g.kind))});
            }
            n += g.total;
        }
        if (status_)
            status_->setMessage(n ? std::to_string(n) + (n > 1 ? " r\xC3\xA9sultats" : " r\xC3\xA9sultat") + " pour \xC2\xAB " + term + " \xC2\xBB"
                                  : "Rien pour \xC2\xAB " + term + " \xC2\xBB dans l'aide.");
    }
    if (!tree_) return;
    // L'arbre garde son defilement (le meme modele, relu) ; la recherche et les
    // recents repartent du haut.
    if (mode == 0 && listMode_ == 0 && treeModel_) {
        treeModel_->set(std::move(lines), std::move(pills));
        tree_->invalidate();
    } else {
        treeModel_ = std::make_shared<TreeModel>(std::move(lines), std::move(pills));
        tree_->setModel(treeModel_);
    }
    listMode_ = mode;
}

void HelpCenterScreen::onRow(std::size_t r) {
    if (r >= rows_.size()) return;
    const Row row = rows_[r];
    if (row.fold.rfind(kAllPrefix, 0) == 0) {          // recette 1.11 : " ... les N autres "
        searchAll().kinds.insert(std::atoi(row.fold.c_str() + std::char_traits<char>::length(kAllPrefix)));
        rebuildList();
        return;
    }
    if (!row.fold.empty()) {
        if (!state_.open.erase(row.fold)) state_.open.insert(row.fold);
        rebuildList();
        return;
    }
    if (!row.key.empty() && row.key != current_) showTopic(row.key, true);
}

void HelpCenterScreen::showTopic(const std::string& key, bool record) {
    const auto& ix = index();
    const hc::Topic* t = ix.find(key);
    if (!t || !page_ || !head_) return;
    current_ = key;
    state_.current = key;
    if (record) nav_.go(hc::topicTarget(key));

    // ---- l'en-tete : le fil d'Ariane, le titre, la carte du tutoriel ----
    // Tranche 7 : il ouvre l'article lui-meme (plus de bande vide sous la carte) ;
    // la vue a part (head_) ne sert qu'aux pages de l'aide generale (HelpView).
    HelpArticle head;
    std::string crumbs;
    for (const auto& c : hc::breadcrumb(ix, key)) crumbs += (crumbs.empty() ? "" : "  \xE2\x80\xBA  ") + c;
    head.blocks.push_back(block(HelpBlockKind::Subtitle, crumbs));
    // Tranche 8 : une page de l'aide generale a deja son titre (HelpView, en
    // intertitre) : l'en-tete n'a que le fil d'Ariane et la carte.
    if (t->source != hc::Source::ApiPage) head.blocks.push_back(block(HelpBlockKind::Title, t->title));
    // La carte : "> Regarder le tutoriel - duree - N etapes" ; pas sur Signaler, ni
    // sur les notes de version, qui n'ont pas de tutoriel (decision 12 : hasTutorial).
    if (t->source != hc::Source::ReportPage && hc::hasTutorial(*t)) {
        const auto info = hc::tutorialInfo(*t);
        auto card = block(HelpBlockKind::Links, {});
        card.links.push_back(HelpLink{hc::tutorialCardText(info), "tuto:" + key, "Le tutoriel de ce sujet"});
        card.links.push_back(HelpLink{"Essayer", "essayer:" + key, "\xC3\x80 toi : le tutoriel te laisse faire"});
        // Tranche 13 : la page Importer un fichier enregistre les fichiers d'exemple
        // des formats (Formats de fichier, plus bas), en CSV UTF-8 avec le BOM.
        if (t->source == hc::Source::ApiPage && t->ref == "importer")
            card.links.push_back(HelpLink{"Enregistrer les fichiers d'exemple", "exemples-csv:",
                                          "Un CSV par format, en UTF-8 avec BOM : Excel lit les accents"});
        // Tranche 16 (decision du chef) : F1 ne mene plus au didacticiel de l'API ; il
        // reste une entree du centre, sur chaque page de L'automate (API).
        if (t->chapter == hc::Chapter::Plc)
            card.links.push_back(HelpLink{"Didacticiel de l'API \xE2\x80\xBA", "didacticiel-api:",
                                          "La visite et les parcours de l'API, sur ton projet"});
        head.blocks.push_back(std::move(card));
    }

    // ---- le texte de la source ----
    const bool api = t->source == hc::Source::ApiPage;
    api_->setVisibility(api ? Visibility::Visible : Visibility::Collapsed);
    page_->setVisibility(api ? Visibility::Collapsed : Visibility::Visible);
    head_->setVisibility(api ? Visibility::Visible : Visibility::Collapsed);
    // Signaler : le formulaire tient le haut (son titre) ; l'article n'a que l'apercu.
    const bool report = t->source == hc::Source::ReportPage;
    if (reportPanel_) reportPanel_->setVisibility(report ? Visibility::Visible : Visibility::Collapsed);
    if (keysPanel_)
        keysPanel_->setVisibility(t->source == hc::Source::ShortcutsPage ? Visibility::Visible : Visibility::Collapsed);
    HelpArticle a;
    if (api) head_->setArticle(std::move(head));
    else if (!report) a.blocks = std::move(head.blocks);
    switch (t->source) {
    case hc::Source::Guide:
        if (const auto* g = hmi::guide::topic(t->ref)) {
            // Le cadre du guide (son fil "Aide IHM / chapitre" et le titre) doublerait
            // l'en-tete du centre : on le retire ; son repere "nouveau" reste sur le resume.
            HelpArticle body = HmiHelpPane::compose(*g);
            for (auto& b : body.blocks)
                if (b.kind != HelpBlockKind::Hero) a.blocks.push_back(std::move(b));
        }
        break;
    case hc::Source::ApiPage:
        // Recette R111-4 : la page seule (hc::apiPage), plus toute l'aide generale defilee
        // jusqu'a l'ancre (dessous venaient les pages suivantes, les nouveautes, l'ancienne
        // table des raccourcis).
        api_->setDocument(std::make_shared<const HelpDocument>(hc::apiPage(*doc_, t->ref)));
        if (api_->goToAnchor(t->ref)) apiSettle_ = t->ref;   // tranche 9 : reposee par Update
        else if (status_)
            status_->setMessage("Ce renvoi ne m\xC3\xA8ne nulle part : " + t->ref);
        break;
    case hc::Source::Macro:
    case hc::Source::Block:
        for (const auto& e : centerData().library)
            if (e.name == t->ref) {
                // Son en-tete (le cadre, le titre) double celui du centre : retire.
                HelpArticle body = buildHelpArticle(e);
                bool titled = false;
                for (auto& b : body.blocks) {
                    if (b.kind == HelpBlockKind::Hero) continue;
                    if (b.kind == HelpBlockKind::Title && !titled) { titled = true; continue; }
                    a.blocks.push_back(std::move(b));
                }
                break;
            }
        break;
    case hc::Source::Expression:
        a.blocks.push_back(block(HelpBlockKind::Lead, t->summary));
        // Integration I111, lien 4 : l'article du type, celui de la page des expressions de T3
        // (app::exprpage::article). Ses puces (essayer:, inserer:) visent le champ d'essai : elles
        // ouvrent la page complete sur ce type et y jouent la puce ("expr:<cle>|<cible>", onLink) ;
        // un autre type reste un sujet du centre ("sujet:expr-<cle>").
        // 1.11 (chantier T3, tranche 11, decision du chef) : le centre n'a pas le champ d'essai.
        // L'article est donc pris sans lui (trialField = false) : il ne parle plus du champ et
        // montre, sous le resume, « Essayer dans la page des expressions » ("expr:<cle>").
        if (const auto* e = hmi::exprguide::find(t->ref)) {
            const std::string typeKey(e->key);
            HelpArticle body = exprpage::article(*e, exprpage::ArticleOptions{false});
            for (auto& b : body.blocks) {
                // Le titre et le resume de l'article : l'en-tete du centre et le chapeau ci-dessus.
                if (b.kind == HelpBlockKind::Title || b.kind == HelpBlockKind::Lead) continue;
                for (auto& l : b.links) {
                    const auto act = exprpage::parseTarget(l.target);
                    if (!act) continue;
                    l.target = act->kind == exprpage::Action::Kind::Open ? "sujet:expr-" + act->typeKey
                                                                          : "expr:" + typeKey + "|" + l.target;
                }
                a.blocks.push_back(std::move(b));
            }
        } else {
            a.blocks.push_back(block(HelpBlockKind::Note, "Ce type d'expression n'a pas de page."));
        }
        break;
    case hc::Source::ShortcutsPage: {
        // Tranche 8 : les pastilles des contextes, la recherche de la page (la bande
        // au-dessus), l'apercu de la fiche A4.
        const auto& ctx = help::keys::contexts();
        std::optional<help::keys::Context> only;
        if (keysOnly_ >= 0 && static_cast<std::size_t>(keysOnly_) < ctx.size())
            only = ctx[static_cast<std::size_t>(keysOnly_)];
        auto pastilles = block(HelpBlockKind::Links, {});
        pastilles.links.push_back(HelpLink{std::string(only ? "" : "\xE2\x97\x8F ") + "Tous", "contexte:-1", {}});
        for (std::size_t i = 0; i < ctx.size(); ++i)
            pastilles.links.push_back(HelpLink{(static_cast<int>(i) == keysOnly_ ? "\xE2\x97\x8F " : "")
                                                   + std::string(help::keys::contextLabel(ctx[i])),
                                               "contexte:" + std::to_string(i), {}});
        a.blocks.push_back(std::move(pastilles));
        if (keysSheet_) {
            a.blocks.push_back(block(HelpBlockKind::Lead,
                "Aper\xC3\xA7u de la fiche A4 : toutes les touches, par contexte. \xC2\xAB Imprimer\xE2\x80\xA6 \xC2\xBB "
                "l'\xC3\xA9" "crit en page HTML au format A4 et l'ouvre dans ton navigateur, qui l'imprime ou "
                "l'enregistre en PDF."));
            // Tranche 9 : en tableaux (les colonnes alignees), comme la fiche HTML :
            // toutes les touches, sans le filtre ni la recherche de la page.
            a.blocks.push_back(block(HelpBlockKind::Subtitle, "XPGAnalyser " + help::news::sessionVersion()
                                                                  + " \xE2\x80\x94 les raccourcis clavier"));
            for (const auto& g : hc::keysPage({}, std::nullopt).groups) {
                a.blocks.push_back(block(HelpBlockKind::Heading, g.label));
                std::string table;
                for (const auto& r : g.rows)
                    table += joinCaps(r.caps) + "\t" + r.text + "\t" + r.since + "\n";
                auto tableau = block(HelpBlockKind::Table, table);
                tableau.label = "touches";
                a.blocks.push_back(std::move(tableau));
            }
            break;
        }
        const auto kp = hc::keysPage(keysTerm(), only);
        a.blocks.push_back(block(HelpBlockKind::Lead, kp.status));
        for (const auto& g : kp.groups) {
            a.blocks.push_back(block(HelpBlockKind::Heading, g.label));
            std::string table;
            for (const auto& r : g.rows)
                table += joinCaps(r.caps) + "\t" + r.text + "\t" + r.since + "\n";
            // "touches" : la vue dessine les [touches] de la premiere colonne en touches de clavier.
            auto tableau = block(HelpBlockKind::Table, table);
            tableau.label = "touches";
            a.blocks.push_back(std::move(tableau));
        }
        break;
    }
    case hc::Source::NotesPage: {
        // Tranche 8 : les pastilles des versions, puis le filtre par domaine (pour
        // cette version : une autre version repart de tous les domaines).
        if (notesVersion_ != t->ref) { notesVersion_ = t->ref; notesDomain_.clear(); }
        const auto np = hc::notesPage(t->ref, notesDomain_);
        auto versions = block(HelpBlockKind::Links, {});
        for (const auto& v : np.versions)
            versions.links.push_back(HelpLink{(v == np.version ? "\xE2\x97\x8F " : "") + v, "sujet:" + hc::notesKey(v), {}});
        a.blocks.push_back(std::move(versions));
        a.blocks.push_back(block(HelpBlockKind::Lead, np.version + " \xC2\xB7 " + np.date + " \xC2\xB7 " + np.state));
        if (!np.summary.empty()) a.blocks.push_back(block(HelpBlockKind::Paragraph, np.summary));
        auto domaines = block(HelpBlockKind::Links, {});
        domaines.links.push_back(HelpLink{std::string(notesDomain_.empty() ? "\xE2\x97\x8F " : "") + "Tous les domaines",
                                          "domaine:", {}});
        for (const auto& d : np.domains)
            domaines.links.push_back(HelpLink{(d == notesDomain_ ? "\xE2\x97\x8F " : "") + d, "domaine:" + d, {}});
        a.blocks.push_back(std::move(domaines));
        for (std::size_t si = 0; si < np.sections.size(); ++si) {
            const auto& s = np.sections[si];
            a.blocks.push_back(block(HelpBlockKind::Heading, s.domain));
            for (std::size_t ri = 0; ri < s.rows.size(); ++ri) {
                const auto& r = s.rows[ri];
                // Tranche 11 : le texte d'une carte commence le plus souvent par une
                // majuscule ("Vide, un champ dit..."), qui suivait mal un deux-points :
                // "Titre. Texte" ; un texte en minuscule garde "Titre : texte".
                const bool minuscule = !r.text.empty() && r.text[0] >= 'a' && r.text[0] <= 'z';
                auto b = block(HelpBlockKind::Bullet,
                               (r.title.empty() ? "" : r.title + (minuscule ? " : " : ". ")) + r.text);
                b.label = r.letter;
                a.blocks.push_back(std::move(b));
                const bool montrer = !r.show.topic.empty() || r.news;
                if (montrer || !r.tutorialTopic.empty()) {
                    auto l = block(HelpBlockKind::Links, {});
                    // "Me montrer" : la ligne est retrouvee par sa place dans la page (onLink).
                    if (montrer)
                        l.links.push_back(HelpLink{"Me montrer", "montrer:" + t->ref + "|" + std::to_string(si) + "|"
                                                                     + std::to_string(ri), {}});
                    if (!r.tutorialTopic.empty())
                        l.links.push_back(HelpLink{"\xE2\x96\xB6 Tutoriel", "tuto:" + r.tutorialTopic, {}});
                    a.blocks.push_back(std::move(l));
                }
            }
        }
        break;
    }
    case hc::Source::ReportPage: {
        // Les trois questions, les quatre cases et les boutons sont dans le panneau
        // au-dessus (tranche 7) ; ici, l'apercu : ce que "Copier le texte" copie et ce
        // que le zip contient. Un texte simple, en police fixe, sans la coloration du ST.
        a.blocks.push_back(block(HelpBlockKind::Lead,
            "D\xC3\xA9" "cris ce qui s'est pass\xC3\xA9, comment le refaire et ce que tu attendais ; "
            "\xC2\xAB Pr\xC3\xA9parer le zip \xC2\xBB l'\xC3\xA9" "crit dans le dossier signalements de tes donn\xC3\xA9" "es."));
        if (!lastZip_.empty())
            // Tranche 11 : le nom du zip seul ; le chemin entier, sans espace, debordait
            // de la colonne (T2_06b). Il est dans la barre d'etat et dans "Copier le chemin".
            a.blocks.push_back(block(HelpBlockKind::Paragraph,
                "Dernier zip : " + dossiers::utf8De(lastZip_.filename())
                    + ", dans le dossier signalements de tes donn\xC3\xA9" "es (\xC2\xAB Ouvrir le dossier \xC2\xBB, "
                      "\xC2\xAB Copier le chemin \xC2\xBB)."));
        a.blocks.push_back(block(HelpBlockKind::Heading, "Aper\xC3\xA7u"));
        auto apercu = block(HelpBlockKind::Code, help::report::text(reportForm_, reportFacts()));
        apercu.label = "texte";
        a.blocks.push_back(std::move(apercu));
        break;
    }
    }
    if (!api) {
        // Le bas de page : precedent / suivant dans l'ordre de l'arbre.
        const auto f = hc::pageFooter(ix, key);
        auto l = block(HelpBlockKind::Links, {});
        if (!f.prevKey.empty()) l.links.push_back(HelpLink{"\xE2\x80\xB9 " + f.prevTitle, "sujet:" + f.prevKey, {}});
        if (!f.nextKey.empty()) l.links.push_back(HelpLink{f.nextTitle + " \xE2\x80\xBA", "sujet:" + f.nextKey, {}});
        if (!l.links.empty()) a.blocks.push_back(std::move(l));
        // Tranche 9 : une autre notation des exemples : le meme article, le defilement reste.
        if (keepPlace_) page_->replaceArticle(std::move(a));
        else { page_->setArticle(std::move(a)); page_->scrollToTop(); }
    }
    if (status_) status_->setMessage(crumbs);
    rebuildList();
    refreshBar();
}

ui::EventResult HelpCenterScreen::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Alt+Gauche / Alt+Droite : l'historique du centre, comme un navigateur.
        // Dans le centre, ces touches sont toujours a lui : la touche globale
        // Retour reste en dehors (au debut de l'historique, rien ne se passe).
        if (k->mods.alt && !k->mods.ctrl && (k->key == ui::Key::Left || k->key == ui::Key::Right)) {
            const bool back = k->key == ui::Key::Left;
            if (back ? nav_.canBack() : nav_.canForward()) {
                gRevealKey = index().keyOf(back ? nav_.back() : nav_.forward());   // T2-10
                showTopic(gRevealKey, false);
            }
            else if (status_)
                status_->setMessage(back ? "D\xC3\xA9" "but de l'historique de l'aide."
                                         : "Fin de l'historique de l'aide.");
            return ui::EventResult::Consumed;
        }
        // Ctrl+F : la recherche unique.
        if (k->mods.ctrl && !k->mods.alt && !k->mods.shift && k->key == ui::Key::F && search_) {
            focus().focus(search_);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void HelpCenterScreen::onLink(const std::string& target) {
    const auto after = [&](std::string_view p) { return target.substr(p.size()); };
    if (target.rfind("sujet:", 0) == 0) { open(after("sujet:")); return; }
    // 1.11.1 (recette R1111-14, deja dans la 1.11.0) : les puces « Voir aussi » d'un sujet du
    // guide (HmiHelpPane::compose) visent "topic:<cle>", qu'aucune branche ne prenait : le
    // clic ne menait nulle part. La cle du guide est celle du sujet dans le centre (keyOfLink).
    // 1.11.2 (decision 141) : de meme les liens d'une page de Macros ou de Blocs DFB / DDT
    // (buildHelpArticle : "lib:<nom>", "par:<nom>#<parametre>"), vers la page bloc- ou macro-.
    if (target.rfind("topic:", 0) == 0 || target.rfind("lib:", 0) == 0 || target.rfind("par:", 0) == 0) {
        if (const std::string k = index().keyOfLink(target); !k.empty()) open(k);
        else if (status_) status_->setMessage("Ce renvoi ne m\xC3\xA8ne nulle part : " + target.substr(target.find(':') + 1));
        return;
    }
    // Le selecteur d'un exemple du guide (ST | C | C++) : comme la barre.
    if (target.rfind("notation:", 0) == 0) { setNotation(after("notation:")); return; }
    // Integration I111, lien 4 : la page des expressions de T3 (HmiExprScreen), sur un type ;
    // "expr:<cle>|<cible>" y joue en plus une puce de l'article (essayer:, inserer:).
    if (target.rfind("expr:", 0) == 0) {
        const std::string rest = after("expr:");
        const auto bar = rest.find('|');
        app_.menus().PushMenu(std::make_unique<HmiExprScreen>(app_, rest.substr(0, bar),
                                                              bar == std::string::npos ? std::string() : rest.substr(bar + 1)));
        return;
    }
    if (target.rfind("tuto:", 0) == 0 || target.rfind("essayer:", 0) == 0) {
        hc::TutorialRequest r;
        r.aTry        = target.rfind("essayer:", 0) == 0;
        r.topic       = after(r.aTry ? "essayer:" : "tuto:");
        r.returnTopic = current_;
        if (!hc::launchTutorial(r) && status_)
            status_->setMessage("Le tutoriel de ce sujet arrive avec la 1.11 (dur\xC3\xA9" "e estim\xC3\xA9" "e).");
        return;
    }
    if (target.rfind("montrer:", 0) == 0) {
        // "Me montrer" d'une ligne des notes : a l'etape du tutoriel, en pause (le
        // lanceur de T1) ; sinon a l'endroit (une carte de la fenetre Nouveautes :
        // son "go" et le widget encadre) ; sinon la page du sujet.
        const std::string rest = after("montrer:");
        const auto p1 = rest.find('|'), p2 = rest.rfind('|');
        if (p1 == std::string::npos || p2 == p1) return;
        // La meme page que celle dessinee : la version, et son domaine retenu.
        const std::string version = rest.substr(0, p1);
        const auto np = hc::notesPage(version, version == notesVersion_ ? notesDomain_ : std::string{});
        const auto si = static_cast<std::size_t>(std::strtoul(rest.c_str() + p1 + 1, nullptr, 10));
        const auto ri = static_cast<std::size_t>(std::strtoul(rest.c_str() + p2 + 1, nullptr, 10));
        if (si >= np.sections.size() || ri >= np.sections[si].rows.size()) return;
        const auto& row = np.sections[si].rows[ri];
        if (row.note && row.show.kind == hc::ShowKind::TutorialStep && hc::launchTutorial(hc::showRequest(*row.note)))
            return;
        if (row.news && row.note && !row.note->id.empty()) {
            noveltyCenter().showMe(std::string(row.note->id));
            return;
        }
        if (!row.show.topic.empty()) open(row.show.topic);
        return;
    }
    if (target.rfind("contexte:", 0) == 0) {
        keysOnly_ = std::atoi(target.c_str() + std::string_view("contexte:").size());
        if (current_ == "page-raccourcis") showTopic(current_, false);
        return;
    }
    if (target.rfind("domaine:", 0) == 0) {
        notesDomain_ = after("domaine:");
        if (!current_.empty()) showTopic(current_, false);
        return;
    }
    if (target == "didacticiel-api:") {
        // Tranche 16 : l'onglet "API . Didacticiel" de l'ecran du projet (ses cartes) ;
        // l'ecran l'ouvre en revenant au premier plan (takePendingApiTutorial).
        if (!app_.project()) {
            if (status_) status_->setMessage("Le didacticiel de l'API se joue sur un projet : ouvre d'abord un projet.");
            return;
        }
        MainAnalysisScreen::requestApiTutorial();
        app_.menus().PopTo("analysis");
        return;
    }
    if (target == "exemples-csv:") {
        // Tranche 13 (decision du chef) : les fichiers d'exemple des formats d'import,
        // en UTF-8 avec le BOM, pour qu'Excel lise les accents de leur ligne d'aide.
        const auto dir = reportDataDir() / "exemples-import";
        std::string why;
        const auto files = project::writeExampleCsvs(dossiers::utf8De(dir), &why);
        if (!status_) return;
        if (files.empty()) {
            status_->setMessage("Les fichiers d'exemple n'ont pas pu s'\xC3\xA9" "crire : " + why);
            return;
        }
        const std::string err = dossiers::ouvrirDansLeSysteme(dossiers::utf8De(dir));
        status_->setMessage(std::to_string(files.size()) + " fichiers d'exemple (CSV, UTF-8 avec BOM : Excel lit les accents) dans "
                            + dossiers::utf8De(dir) + (err.empty() ? std::string() : " (" + err + ")"));
        return;
    }
    if (target == "raccourcis:imprimer") {
        // L'appli n'a pas d'impression : la fiche A4 en HTML (@page A4), ouverte
        // dans le navigateur du systeme, qui l'imprime ou l'enregistre en PDF.
        const auto dir = reportDataDir() / "fiches";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto file = dir / ("raccourcis-" + help::news::sessionVersion() + ".html");
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << hc::keysSheetHtml(help::news::sessionVersion());
            if (!out) {
                if (status_) status_->setMessage("La fiche n'a pas pu s'\xC3\xA9" "crire : " + dossiers::utf8De(file));
                return;
            }
        }
        const std::string err = dossiers::ouvrirDansLeSysteme(dossiers::utf8De(file));
        if (status_)
            status_->setMessage(err.empty() ? "La fiche A4 s'ouvre dans ton navigateur (Imprimer, ou Enregistrer en PDF) : "
                                                  + dossiers::utf8De(file)
                                            : "La fiche est \xC3\xA9" "crite : " + dossiers::utf8De(file) + " (" + err + ")");
        return;
    }
    if (target == "signaler:capture") {
        // La fenetre telle qu'elle est, au prochain dessin (comme F12) ; le zip la
        // prend sous le nom capture.png.
        const auto dir = reportDataDir() / "signalements";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        captureFile_ = dir / "capture-en-cours.png";
        std::filesystem::remove(captureFile_, ec);
        app_.requestCapture(captureFile_.string());
        reportForm_.screenshotPng = "capture.png";
        if (!current_.empty()) showTopic(current_, false);
        if (status_) status_->setMessage("Capture jointe : capture.png (la fen\xC3\xAAtre, au prochain dessin).");
        return;
    }
    if (target == "signaler:copier") {
        setClipboardText(help::report::text(reportForm_, reportFacts()));
        if (status_) status_->setMessage("Le texte du signalement est copi\xC3\xA9.");
        return;
    }
    if (target == "signaler:zip") {
        std::string why;
        std::string png;
        if (!reportForm_.screenshotPng.empty() && !captureFile_.empty()) {
            std::ifstream in(captureFile_, std::ios::binary);
            png.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const auto zip = help::report::writeZip(reportDataDir(), reportForm_, reportFacts(), png, nowStamp(), &why);
        if (zip.empty()) {
            if (status_) status_->setMessage(why.empty() ? std::string("Le zip n'a pas pu s'\xC3\xA9" "crire.") : why);
            return;
        }
        lastZip_ = zip;
        if (!current_.empty()) showTopic(current_, false);   // "Dernier zip" ; puis le message
        if (status_) status_->setMessage("Le zip est pr\xC3\xAAt : " + dossiers::utf8De(zip));
        return;
    }
    if (target == "signaler:dossier" || target == "signaler:chemin") {
        const auto dir = lastZip_.empty() ? reportDataDir() / "signalements" : lastZip_.parent_path();
        if (target == "signaler:chemin") {
            const std::string path = dossiers::utf8De(lastZip_.empty() ? dir : lastZip_);
            setClipboardText(path);
            if (status_) status_->setMessage("Le chemin est copi\xC3\xA9 : " + path);
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string err = dossiers::ouvrirDansLeSysteme(dossiers::utf8De(dir));
        if (status_) status_->setMessage(err.empty() ? "Le dossier s'ouvre : " + dossiers::utf8De(dir) : err);
        return;
    }
    // Un renvoi d'une page du guide ou de la bibliotheque : une cle du centre.
    if (index().find(target)) open(target);
}

} // namespace app
