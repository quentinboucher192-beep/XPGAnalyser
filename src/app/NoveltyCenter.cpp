#include "NoveltyCenter.hpp"

#include "App.hpp"
#include "screens/Screens.hpp"
#include "screens/HelpCenterScreen.hpp"
#include "../core/Version.hpp"
#include "../help/CenterIndex.hpp"
#include "../help/Novelties.hpp"
#include "../hmi/HmiGuide.hpp"
#include "hmi/HmiPanels.hpp"
#include "../menu/IMenu.hpp"
#include "../ui/NoveltyBoard.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../ui/widgets/HelpArticleView.hpp"
#include "../ui/widgets/Containers.hpp"
#include "TopBar.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app {

namespace news = help::news;

namespace {

// 1.10 (H) : "arbre2:" - le chemin de l'arbre se termine par un double-clic.
bool gGoDouble = false;
// 1.11 (T2, tranche 14) : vrai quand la visite passe a la carte suivante (Suivante,
// Me montrer tout) ; faux pour la carte cliquee. Une carte "tuto:" ne lance son
// tutoriel que cliquee : en cours de visite, elle ouvre la page du sujet (qui porte
// la carte du tutoriel), et la visite continue.
bool gWalking = false;

// 1.11 (T2, tranche 14) : une page du centre d'aide. Le centre deja au-dessus (la
// carte d'avant de la visite y menait) la montre ; sinon, il s'ouvre dessus. Les
// cartes "aide:" qui se suivent n'empilent plus un centre par carte.
void openHelpPage(App& app, const std::string& key) {
    if (auto* center = dynamic_cast<HelpCenterScreen*>(app.menus().top())) {
        center->open(key);
        return;
    }
    app.setHelpTopic(key);
    app.menus().PushMenu("help.hmi");
}
// 1.10 (H) : "touche:F7" - la touche a envoyer a l'ecran du projet (a l'image suivante :
// le retour a cet ecran se fait d'abord).
std::string gGoKey;

// La vignette d'une carte : un petit dessin en texte (la maquette, scene 8).
std::string pictoOf(const std::string& id) {
    static const std::pair<const char*, const char*> kPictos[] = {
        {"1.10.expressions", "fx   = Pompes[3].Debit"},
        {"1.10.saisie-typee", "BOOL \xE2\x96\xBE  Marche, Defaut\xE2\x80\xA6"},
        {"1.10.simulation-api-ihm", "\xE2\x97\x8F API   \xE2\x97\x8F IHM"},
        {"1.10.barre-simulation-ihm", "\xE2\x88\x92   150 %   +"},
        {"1.10.plein-ecran", "F11   \xE2\x86\x94   \xC3\x89" "chap"},
        {"1.10.compiler-scripts", "Debitt  \xE2\x86\x92  ligne 2, col. 13"},
        {"1.10.visualisation-graphique", "\xE2\x86\x97 courbes en direct"},
        {"1.10.types-excel", "A  \xE2\x86\x92  B  \xE2\x86\x92  C   Ctrl+V"},
        {"1.10.objet-alarmes", "\xE2\x96\xBE Alarmes (3)"},
        {"1.10.couleurs", "#2A7FBF   RVB   TSV"},
        {"1.10.grafcet", "X1 \xE2\x86\x92 X2 \xE2\x86\x92 X3"},
        {"1.10.fonctions-scripts", "FUNCTION   REF_TO   MAP"},
        {"1.10.operateurs", "TO_REAL(v)    v += 1.5"},
        {"1.10.aide-notations", "ST  |  C  |  C++"},
        {"1.10.f8", "F8   \xE2\x96\xB6 IHM   Maj+F8"},
        {"1.10.grafcet-code", "X3  \xE2\x86\x94  section ST"},
        {"1.10.enum", "T_MODE#Auto   CASE"},
        {"1.10.menu-aide", "?  \xE2\x96\xBE   Nouveaut\xC3\xA9s\xE2\x80\xA6"},
        {"1.10.nouveautes", "NOUVEAU"},
        // 1.11 (T2, tranche 14) : les cartes de la 1.11.
        {"1.11.centre-aide", "?   Ctrl+F   \xE2\x98\x85   Alt+\xE2\x86\x90"},
        {"1.11.raccourcis", "Ctrl  Maj  O    A4"},
        {"1.11.signaler", "1  2  3   \xE2\x86\x92   .zip"},
        {"1.11.tutoriels", "\xE2\x96\xB6  1/7   0:02 / 1:04"},
        {"1.11.expressions", "SEL(G, a, b)   Essaie ici"},
        {"1.11.icones-compilable", "Scripts   \xE2\x97\x8F compile   \xE2\x97\x8F g\xC3\xA9n\xC3\xA8re"},
        {"1.11.lier-alarmes", "[x] A   [x] B   [ ] Sym"},
        // 1.11.2 (T2, decisions 201 et 216) : les cartes de la 1.11.2.
        {"1.11.2.paquets", ".xpgsymboles  .xpgtypes  \xE2\x86\x92 Importer\xE2\x80\xA6"},
        {"1.11.2.icones-saisie", "\xE2\x97\x8F automate  \xE2\x97\x8F IHM  \xE2\x97\x8F objet"},
    };
    for (const auto& [k, v] : kPictos)
        if (id == k) return v;
    return {};
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool startsWithNoCase(const std::string& text, const std::string& part) {
    return lower(text).rfind(lower(part), 0) == 0;
}

// La fenetre : la planche des cartes, au centre.
class BoardHost final : public ui::Widget {
public:
    BoardHost() : ui::Widget("nouveautes.fenetre") {}
    ui::novelty::Board* board{nullptr};

protected:
    void onLayout() override {
        const auto r = bounds();
        // 1.10 : quatre colonnes sur un grand ecran (la maquette), trois sinon.
        const float w = std::min(1320.f, std::max(300.f, r.w - 48.f));
        const float h = std::min(880.f, std::max(260.f, r.h - 48.f));
        if (board) board->setBounds({std::floor(r.x + (r.w - w) / 2.f), std::floor(r.y + (r.h - h) / 2.f), w, h});
    }
};

class NoveltyDialog final : public menu::WidgetMenu {
public:
    NoveltyDialog(std::vector<ui::novelty::Board::Card> cards, std::string current, std::string from, std::string caption)
        : menu::WidgetMenu("dialog.nouveautes"), cards_(std::move(cards)), current_(std::move(current)),
          from_(std::move(from)), caption_(std::move(caption)) {}

    [[nodiscard]] menu::MenuTraits traits() const override {
        menu::MenuTraits t;
        t.kind = menu::MenuKind::Dialog;
        t.rendersBelow = true;
        t.updatesBelow = false;
        t.blocksInput = true;
        t.dimsBelow = true;
        return t;
    }
    [[nodiscard]] std::string title() const override { return "Nouveaut\xC3\xA9s"; }
    [[nodiscard]] ui::novelty::Board* board() const noexcept { return board_; }

protected:
    core::Status buildUi() override {
        auto host = std::make_unique<BoardHost>();
        auto board = std::make_unique<ui::novelty::Board>("nouveautes.cartes");
        board->setCards(std::move(cards_), current_, from_, caption_);
        board->setMarksHidden(news::session().marksHidden);
        board_ = &static_cast<ui::novelty::Board&>(host->addChild(std::move(board)));
        host->board = board_;
        links_ += board_->showMeRequested->connect([this](const std::string& id) { finish("show:" + id); });
        links_ += board_->showAllRequested->connect([this] { finish("all"); });
        links_ += board_->laterRequested->connect([this] { finish("later"); });
        links_ += board_->allSeenRequested->connect([this] { finish("allseen"); });
        links_ += board_->previousRequested->connect([this] { finish("lot8"); });   // 1.10 (H) : les versions precedentes
        links_ += board_->hideMarksRequested->connect([this](bool hide) {
            noveltyCenter().setMarksHidden(hide);
            board_->setMarksHidden(hide);
        });
        setRoot(std::move(host));
        return core::ok();
    }

private:
    void finish(std::string what) {
        if (done_) return;
        done_ = true;
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Ok, std::move(what)});
    }

    std::vector<ui::novelty::Board::Card> cards_;
    std::string           current_, from_, caption_;
    ui::novelty::Board*   board_{nullptr};
    bool                  done_{false};
    core::ConnectionScope links_;
};

std::string snapshot(const news::State& s) {
    std::string out;
    news::save(s, [&](const std::string& k, const std::string& v) { out += k + "=" + v + "\n"; });
    return out;
}

} // namespace

NoveltyCenter& noveltyCenter() {
    static NoveltyCenter c;
    return c;
}

void NoveltyCenter::start(App& app, bool existingProfile) {
    app_ = &app;
    scripted_ = app.scripted();
    auto& version = news::sessionVersion();
    version = XPG_ANALYZER_VERSION;
    // Avant que la version soit portee a la 1.10 (l'integrateur) : le registre
    // a deja ses nouveautes - la plus recente des deux.
    if (const auto vs = news::versions(); !vs.empty() && news::compareVersions(vs.front(), version) > 0) version = vs.front();
    auto& s = news::session();
    s = {};
    news::load(s, [&](const std::string& k) { return app.settings().getString(k); });
    pending_ = news::onLaunch(s, version, existingProfile);
    if (scripted_) {
        // Une session de captures : ni fenetre, ni orange dans l'aide (sauf demande).
        pending_ = false;
        news::markHelpRead(s, version);
        s.helpOnlyNew = false;
    }
    // Les parties d'un widget du registre ("<barre>#<bouton>") : un bouton d'une
    // barre d'outils de l'IHM, par son infobulle ou son texte.
    // 1.10 (H) : l'onglet Aide general encadre ses sections des nouveautes (nouveautes-1-9...).
    ui::novelty::setSinceLabeler([](std::string_view since) {
        return news::helpIsNew(news::session(), since) ? news::label(since) : std::string{};
    });
    ui::novelty::setPartFinder([](ui::Widget& w, std::string_view part, gfx::Rect& out) {
        if (auto* strip = dynamic_cast<HmiToolStrip*>(&w)) {
            const int a = strip->actionByTip(part);
            if (a < 0) return false;
            out = strip->rectOf(a);
            return out.w > 0.f && out.h > 0.f;
        }
        // 1.10 (H) : un bouton d'une barre d'outils, par son action
        // ("hmi.simulation.bar#hmi.fullscreen") ; une partie de la barre du haut
        // ("analysis.topbar#aide", TopBar::partRect).
        if (auto* bar = dynamic_cast<ui::ToolBar*>(&w)) {
            for (const auto& c : bar->children())
                if (auto* b = dynamic_cast<ui::Button*>(c.get()); b && b->visible() && b->action() == part) {
                    out = b->bounds();
                    return out.w > 0.f && out.h > 0.f;
                }
            return false;
        }
        if (auto* top = dynamic_cast<TopBar*>(&w)) {
            out = top->partRect(part);
            return out.w > 0.f && out.h > 0.f;
        }
        // 1.10 : une partie d'une page d'aide - "help.hmi.pane.article#notation:"
        // (le selecteur ST | C | C++ du premier exemple a l'ecran).
        if (auto* page = dynamic_cast<ui::HelpArticleView*>(&w)) {
            out = page->hotspotsRect(part);
            // Le selecteur ST | C | C++ est petit : le cadre s'etend a sa droite (la
            // bande vide du cadre de code), la pastille NOUVEAU s'y pose sans cacher C++.
            if (part.rfind("notation:", 0) == 0 && out.w > 0.f) out.w += 86.f;
            return out.w > 0.f && out.h > 0.f;
        }
        return false;
    });
    links_ += spot_.nextRequested->connect([this] { next(); });
    links_ += spot_.closeRequested->connect([this] { endTour(); });
    links_ += spot_.boardRequested->connect([this] { endTour(); openBoard(); });
    links_ += spot_.helpRequested->connect([this] {
        const auto* item = news::find(currentId_);
        if (!item || item->topic.empty() || !app_) return;
        endTour();
        app_->setHelpTopic(item->topic);
        app_->menus().PushMenu("help.hmi");
    });
    saveIfChanged(true);
}

void NoveltyCenter::relaunch(const std::string& current, const std::string& previous) {
    auto& s = news::session();
    s = {};
    if (!previous.empty()) s.lastVersion = previous;
    news::sessionVersion() = current;
    pending_ = news::onLaunch(s, current, !previous.empty());
    scriptMarks_ = true;
    saveIfChanged(true);
}

ui::Widget* NoveltyCenter::topRoot() const {
    if (!app_) return nullptr;
    auto* top = app_->menus().top();
    return top ? top->widgetRoot() : nullptr;
}

// 1.10 (H, demande de R2) : "Versions precedentes" de la fenetre - les nouveautes
// de l'API du lot 8 et leurs parcours (l'action help.lot8 de la barre du haut :
// ApiWorkspace ouvre l'onglet). Sans projet ouvert, pas de barre : on le dit.
namespace {
// Les images qui restent pour trouver la barre du haut apres un retour a l'ecran
// du projet (l'aide generale ou celle de l'IHM etait par-dessus) ; 0 : rien a faire.
int gPreviousPending = 0;

TopBar* findTopBar(ui::Widget* root) {
    TopBar* bar = nullptr;
    std::function<void(ui::Widget&)> walk = [&](ui::Widget& w) {
        if (!bar) bar = dynamic_cast<TopBar*>(&w);
        for (const auto& c : w.children())
            if (!bar) walk(*c);
    };
    if (root) walk(*root);
    return bar;
}

void openPreviousVersions(App& app, ui::Widget* root) {
    if (auto* bar = findTopBar(root)) {
        bar->trigger("help.lot8");
        return;
    }
    if (app.project()) {
        // Une page d'aide par-dessus l'ecran du projet : on y revient, la barre du
        // haut ouvre l'onglet a l'image suivante (tick).
        app.menus().PopTo("analysis");
        gPreviousPending = 60;
        return;
    }
    app.events().publish(StatusNotice{"Ouvre un projet : les nouveaut\xC3\xA9s de la 1.8.0 et leurs parcours s'ouvrent dans son onglet API.", 8.0});
}
} // namespace

void NoveltyCenter::openBoard(bool automatic) {
    if (!app_) return;
    // Ouverte alors qu'elle etait due (premier lancement, ou rejoue par un script) :
    // "premiere ouverture de cette version".
    automatic = automatic || pending_;
    pending_ = false;
    const auto& s = news::session();
    const auto current = news::sessionVersion();
    auto items = news::pending(s, current);
    // Revue du menu Aide alors que tout est vieux (pas de version manquee) :
    // les nouveautes de la version lancee.
    if (items.empty()) items = news::ofVersion(current);
    std::vector<ui::novelty::Board::Card> cards;
    for (const auto* it : items) {
        ui::novelty::Board::Card c;
        c.id = it->id;
        c.version = it->version;
        c.title = it->title;
        c.text = it->text;
        c.image = it->image;
        c.picto = pictoOf(it->id);
        c.seen = news::wasSeen(s, it->id);
        c.canShow = true;
        cards.push_back(std::move(c));
    }
    const std::string from = s.baseline.empty() || s.baseline == "0" ? std::string{} : news::shortVersion(s.baseline);
    const std::string caption = automatic ? "premi\xC3\xA8re ouverture de cette version" : "ce qui est nouveau dans cette version";
    app_->menus().ShowDialog(std::make_unique<NoveltyDialog>(std::move(cards), current, from, caption),
                             [this, ids = [&] {
                                 std::vector<std::string> v;
                                 for (const auto* it : items) v.push_back(it->id);
                                 return v;
                             }()](const menu::DialogResult& r) {
                                 const auto& what = r.payload;
                                 if (what == "allseen") {
                                     news::markAllSeen(news::session(), news::sessionVersion());
                                 } else if (what == "all") {
                                     if (!ids.empty()) showMe(ids.front(), ids);
                                 } else if (what.rfind("show:", 0) == 0) {
                                     showMe(what.substr(5), ids);
                                 } else if (what == "lot8") {
                                     openPreviousVersions(*app_, topRoot());
                                 }
                                 saveIfChanged();
                             });
}

void NoveltyCenter::showMe(const std::string& id, std::vector<std::string> tour) {
    const auto* item = news::find(id);
    if (!item) return;
    if (tour.empty()) tour.push_back(id);
    gWalking = false;
    tour_ = std::move(tour);
    const auto at = std::find(tour_.begin(), tour_.end(), id);
    tourIndex_ = at == tour_.end() ? 0 : static_cast<std::size_t>(at - tour_.begin());
    if (at == tour_.end()) tour_.insert(tour_.begin(), id);
    present();
}

namespace {
// "Me montrer" d'une partie d'une page d'aide hors de la vue : la page y defile
// une fois par nouveaute montree (gPresented compte les presentations).
std::size_t gPresented = 0, gRevealed = 0;
} // namespace

void NoveltyCenter::present() {
    ++gPresented;
    if (tourIndex_ >= tour_.size()) { endTour(); return; }
    const auto* item = news::find(tour_[tourIndex_]);
    if (!item) { endTour(); return; }
    currentId_ = item->id;
    news::markSeen(news::session(), item->id);
    spot_.show(item->title, item->text, tourIndex_, tour_.size(), news::label(item->version));
    // "Voir l'aide" : seulement si le sujet est dans le guide (les fragments des
    // chantiers y entrent a la fusion). Recette 1.11 (T2, tranche 15) : pas pour
    // une carte qui montre deja sa page dans le centre (aide:, tuto:) - la bulle
    // y proposait "Voir l'aide" sur l'aide deja ouverte.
    const bool shownInCenter = item->go.rfind("aide:", 0) == 0 || item->go.rfind("tuto:", 0) == 0;
    spot_.setHelpOffered(!shownInCenter && !item->topic.empty() && hmi::guide::topic(item->topic) != nullptr);
    spot_.setTarget(std::nullopt);
    targetFound_ = false;
    go(item->go);
    saveIfChanged();
}

void NoveltyCenter::next() {
    ++tourIndex_;
    if (tourIndex_ >= tour_.size()) { endTour(); return; }
    gWalking = true;
    present();
}

void NoveltyCenter::endTour() {
    spot_.close();
    currentId_.clear();
    goPending_.clear();
}

void NoveltyCenter::go(const std::string& where) {
    lastGo_ = where;
    goPending_.clear();
    goTries_ = 0;
    if (!app_) return;
    // 1.10 (H) : une page d'aide par-dessus l'ecran du projet (la nouveaute d'avant
    // ouvrait l'aide) : on revient a l'ecran du projet, sauf pour aller dans l'aide.
    // 1.11 (T2, tranche 14) : ni pour "tuto:" - en visite, il montre la page dans le centre
    // deja au-dessus (le retour a l'ecran du projet ne se fait qu'a l'image suivante :
    // le centre serait retire apres coup) ; clique, le lanceur pose son propre ecran.
    if (app_->project() && where.rfind("aide:", 0) != 0 && where.rfind("tuto:", 0) != 0
        && !dynamic_cast<MainAnalysisScreen*>(app_->menus().top()))
        app_->menus().PopTo("analysis");
    if (where.empty() || where == "projet:") return;
    if (where.rfind("action:", 0) == 0) {
        (void)app_->actions().trigger(where.substr(7), app_->commands());
    } else if (where.rfind("aide:", 0) == 0) {
        openHelpPage(*app_, where.substr(5));
    } else if (where.rfind("arbre:", 0) == 0) {
        goPending_ = where.substr(6);       // l'arbre du projet : a l'image suivante (stepGo)
        gGoDouble = false;
    } else if (where.rfind("touche:", 0) == 0) {
        // 1.10 (H) : une touche de l'ecran du projet (F7 : Compiler l'IHM), a l'image suivante.
        gGoKey = where.substr(7);
    } else if (where.rfind("arbre2:", 0) == 0) {
        goPending_ = where.substr(7);       // 1.10 : un double-clic (une instance de grafcet s'ouvre ainsi)
        gGoDouble = true;
    } else if (where.rfind("tuto:", 0) == 0) {
        // 1.11 (T2, tranche 14) : "tuto:objet-vanne" - le tutoriel du sujet, dans son bac a sable (le
        // lanceur de T1, comme la carte du centre). Il prend la main : la visite s'arrete la. Sans
        // lanceur (ou s'il refuse) : la page du sujet dans le centre, qui porte sa carte du tutoriel.
        help::center::TutorialRequest r;
        r.topic = where.substr(5);
        r.returnTopic = r.topic;
        if (!gWalking && help::center::launchTutorial(r)) {
            endTour();
        } else {
            openHelpPage(*app_, r.topic);
        }
    }
}

// Un chemin de l'arbre du projet ("IHM/Configuration/Equipements") : chaque
// morceau est cherche sous le precedent (trois niveaux au plus), ses dossiers
// se deplient, puis un clic sur la ligne l'ouvre (comme la commande de script
// arbre). Faux : pas (encore) d'arbre - on reessaie quelques images.
bool NoveltyCenter::stepGo() {
    auto* root = topRoot();
    auto* tree = root ? dynamic_cast<ui::TreeView*>(root->findById("analysis.explorer")) : nullptr;
    if (!tree || !tree->model()) return false;
    const auto* model = tree->model().get();
    std::vector<std::string> parts;
    for (std::size_t from = 0;;) {
        const auto slash = goPending_.find('/', from);
        parts.push_back(goPending_.substr(from, slash == std::string::npos ? std::string::npos : slash - from));
        if (slash == std::string::npos) break;
        from = slash + 1;
    }
    ui::NodeId node = model->root();
    for (const auto& part : parts) {
        std::vector<std::pair<ui::NodeId, std::vector<ui::NodeId>>> level{{node, {}}};
        bool hit = false;
        std::vector<ui::NodeId> path;
        for (int depth = 0; depth < 3 && !hit; ++depth) {
            std::vector<std::pair<ui::NodeId, std::vector<ui::NodeId>>> nextLevel;
            for (const auto& [n, chain] : level) {
                for (std::size_t c = 0; c < model->childCount(n) && !hit; ++c) {
                    const auto child = model->childAt(n, c);
                    auto childChain = chain;
                    childChain.push_back(n);
                    if (startsWithNoCase(model->text(child), part)) { node = child; path = childChain; hit = true; break; }
                    nextLevel.emplace_back(child, std::move(childChain));
                }
                if (hit) break;
            }
            level = std::move(nextLevel);
        }
        if (!hit) { goPending_.clear(); return true; }      // introuvable : la bulle reste au centre
        for (const auto n : path) tree->expand(n);
    }
    // "IHM/Vues/" : le premier element au bout des dossiers (une vue, pas un dossier).
    if (!goPending_.empty() && goPending_.back() == '/')
        for (int guard = 0; guard < 6 && model->childCount(node) > 0; ++guard) {
            tree->expand(node);
            node = model->childAt(node, 0);
        }
    tree->ensureVisible(node);
    gfx::Rect r;
    if (!tree->rowRect(node, r)) return false;              // pas encore mis en page : l'image suivante
    goPending_.clear();
    const gfx::Point p{r.right() - 30.f, r.y + r.h * 0.5f};
    app_->menus().HandleEvent(ui::MouseMove{p, {}, {}});
    app_->menus().HandleEvent(ui::MouseDown{p, ui::MouseButton::Left, 1, {}});
    app_->menus().HandleEvent(ui::MouseUp{p, ui::MouseButton::Left, {}});
    if (gGoDouble) {
        app_->menus().HandleEvent(ui::MouseDown{p, ui::MouseButton::Left, 2, {}});
        app_->menus().HandleEvent(ui::MouseUp{p, ui::MouseButton::Left, {}});
        gGoDouble = false;
    }
    return true;
}

void NoveltyCenter::setMarksHidden(bool hidden) {
    news::session().marksHidden = hidden;
    if (!hidden) scriptMarks_ = true;
    saveIfChanged();
}

bool NoveltyCenter::marksHidden() const { return news::session().marksHidden; }

void NoveltyCenter::tick() {
    if (!app_) return;
    ++frames_;
    // La fenetre du premier lancement : l'accueil (ou le projet) a l'ecran, sans question.
    if (pending_ && !app_->scripted() && frames_ > 60 && !app_->menus().questionWaiting()) {
        auto* top = app_->menus().top();
        if (dynamic_cast<StartupScreen*>(top) || dynamic_cast<MainAnalysisScreen*>(top)) openBoard(true);
    }
    if (!goPending_.empty()) {
        if (!stepGo() && ++goTries_ > 90) goPending_.clear();
    }
    if (!gGoKey.empty() && dynamic_cast<MainAnalysisScreen*>(app_->menus().top())) {
        if (gGoKey == "F7") app_->menus().HandleEvent(ui::KeyDown{ui::Key::F7, {}, false});
        gGoKey.clear();
    }
    if (gPreviousPending > 0) {
        // 1.10 (H) : Versions precedentes, apres le retour a l'ecran du projet.
        if (auto* bar = findTopBar(topRoot())) {
            bar->trigger("help.lot8");
            gPreviousPending = 0;
        } else if (--gPreviousPending == 0) {
            app_->events().publish(StatusNotice{"Les nouveaut\xC3\xA9s de la 1.8.0 : Aide \xE2\x80\xBA Didacticiel de l'API (leurs parcours sont marqu\xC3\xA9s nouveau).", 8.0});
        }
    }
    saveIfChanged();
}

void NoveltyCenter::saveIfChanged(bool force) {
    if (!app_) return;
    const auto now = snapshot(news::session());
    if (!force && now == saved_) return;
    saved_ = now;
    news::save(news::session(), [&](const std::string& k, const std::string& v) { app_->settings().set(k, std::string(v)); });
    (void)app_->settings().save();
}

void NoveltyCenter::paint(gfx::IRenderer& r, const ui::Theme& t, gfx::Size surface) {
    auto* root = topRoot();
    // Les reperes : les nouveautes dont l'element est a l'ecran (pas en mode
    // script, sauf demande ; pas pendant la bulle : elle encadre deja le sien).
    const auto& s = news::session();
    std::vector<ui::novelty::Marks::Target> targets;
    if ((!app_ || !app_->scripted() || scriptMarks_) && !spot_.active())
        for (const auto& it : news::all())
            if (news::markShown(s, it)) targets.push_back({it.widget, it.id});
    marks_.setTargets(std::move(targets));
    marks_.collect(root);
    marks_.paint(r, t);
    if (spot_.active()) {
        // L'element de la nouveaute montree, s'il est a l'ecran (cherche a chaque
        // image : l'endroit vient de s'ouvrir, un volet bouge).
        std::optional<gfx::Rect> target;
        targetFound_ = false;
        if (const auto* item = news::find(currentId_); item && !item->widget.empty() && root)
            if (gfx::Rect rc; ui::novelty::targetRect(*root, item->widget, rc)) { target = rc; targetFound_ = true; }
        // Une partie d'une page d'aide ("help.hmi.pane.article#notation:") hors
        // de la vue : la page y defile, une fois (la bulle la trouve ensuite).
        if (const auto* item = news::find(currentId_); !target && item && root && gRevealed != gPresented) {
            const auto hash = item->widget.find('#');
            if (hash != std::string::npos)
                if (auto* page = dynamic_cast<ui::HelpArticleView*>(ui::novelty::findVisible(*root, item->widget.substr(0, hash))))
                    if (page->revealHotspots(std::string_view(item->widget).substr(hash + 1))) gRevealed = gPresented;
        }
        spot_.setTarget(target);
        spot_.setNote(target || goPending_.size() ? std::string{}
                      : (app_ && !app_->project() && news::find(currentId_) && news::find(currentId_)->go.rfind("arbre:", 0) == 0)
                          ? "Ouvre un projet : \xC2\xAB Me montrer \xC2\xBB t'y emm\xC3\xA8nera."
                          : std::string{});
        spot_.paint(r, t, surface);
    }
}

ui::EventResult NoveltyCenter::handle(const ui::InputEvent& ev) {
    if (spot_.active() && spot_.handle(ev) == ui::EventResult::Consumed) return ui::EventResult::Consumed;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        // Un clic sur un element marque : il est utilise, son repere s'en va ; le clic passe.
        // Toutes les nouveautes de cet element (deux peuvent partager l'inspecteur) :
        // sinon le repere de la suivante reviendrait a l'image d'apres.
        for (const auto& m : marks_.marks())
            if (m.rect.contains(d->pos)) {
                for (const auto& it : news::all())
                    if (it.widget == m.widget) news::markUsed(news::session(), it.id);
                saveIfChanged();
                break;
            }
    }
    return ui::EventResult::Ignored;
}

std::string NoveltyCenter::describe() const {
    const auto& s = news::session();
    std::string out = "version " + news::sessionVersion() + " ; derniere vue " + (s.baseline.empty() ? "?" : s.baseline);
    out += " ; a montrer " + std::to_string(news::pending(s, news::sessionVersion()).size());
    out += " ; vues " + std::to_string(s.seen.size()) + " ; utilisees " + std::to_string(s.used.size());
    out += std::string(" ; reperes ") + (s.marksHidden ? "masques" : "montres") + " (" + std::to_string(marks_.marks().size()) + " a l'ecran)";
    if (spot_.active()) out += " ; bulle " + currentId_ + " " + std::to_string(tourIndex_ + 1) + "/" + std::to_string(tour_.size())
                               + (targetFound_ ? " (element encadre)" : " (element pas a l'ecran)");
    return out;
}

} // namespace app
