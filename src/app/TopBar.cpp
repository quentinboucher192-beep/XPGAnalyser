// =============================================================================
//  app/TopBar.cpp - lot API 2 : la barre du haut, repensee
// =============================================================================
#include "TopBar.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : le bandeau de chaque application
#include "../core/CallTrail.hpp"   // 1.10.2 (CR) : le journal interne
#include "NoveltyCenter.hpp"           // 1.10 (R2, menu Aide) : les reperes, ce qui n'a pas ete vu
#include "../help/Novelties.hpp"

#include "../ui/NoveltyMarks.hpp"   // 1.10 (integration I2) : l'orange des reperes (la pastille sur Aide)
#include "../ui/Theme.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <chrono>      // Lot API 8 : bandeau haut (le clic long sur Annuler)
#include <cmath>

namespace app {

namespace {

// Lot API 8 : bandeau haut - 44 px, trois zones (la maquette validee) ; le theme
// a fort contraste la porte a 48, et les parties se centrent dans ce que la
// barre recoit (la mise en place : TopBarLot8.cpp).
constexpr float kHeight = TopBar::kBarHeight;
const gfx::FontId kLabel{14};
const gfx::FontId kHelpBadge{11};   // 1.10 (R2) : la pastille des nouveautes sur Aide

// 1.10 (R2, menu Aide) : les nouveautes pas encore vues (la fenetre Nouveautes
// les montre ; "Me montrer" ou "Tout vu" les marque vues).
int unseenNovelties() {
    const auto& s = help::news::session();
    int n = 0;
    for (const auto* it : help::news::pending(s, help::news::sessionVersion()))
        if (it && !help::news::wasSeen(s, it->id)) ++n;
    return n;
}
const gfx::FontId kSmall{12};
const gfx::FontId kTiny{11};

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// "Enregistrer sous\xE2\x80\xA6" et "Enregistrer sous..." : la meme entree.
std::string plainLabel(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 3, "\xE2\x80\xA6") == 0) { i += 2; continue; }
        if (s[i] == '.') continue;
        out += s[i];
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return lowerAscii(out);
}

std::string thousands(std::uint64_t n) {
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";   // espace fine insecable
        out += d[i];
    }
    return out;
}

void chevron(gfx::IRenderer& r, float x, float y, gfx::Color c) {
    r.line({x - 4.f, y - 2.f}, {x, y + 2.f}, c, 1.5f);
    r.line({x, y + 2.f}, {x + 4.f, y - 2.f}, c, 1.5f);
}

void plusGlyph(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, h = b.w * 0.36f;
    r.line({cx - h, cy}, {cx + h, cy}, c, 1.8f);
    r.line({cx, cy - h}, {cx, cy + h}, c, 1.8f);
}

void eyeGlyph(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, w = b.w * 0.46f, h = b.h * 0.26f;
    // deux arcs grossiers et la pupille
    for (int s = -1; s <= 1; s += 2) {
        gfx::Point prev{cx - w, cy};
        for (int k = 1; k <= 8; ++k) {
            const float t = static_cast<float>(k) / 8.f;
            const float x = cx - w + 2.f * w * t;
            const float y = cy + static_cast<float>(s) * h * std::sin(t * 3.14159f);
            r.line(prev, {x, y}, c, 1.4f);
            prev = {x, y};
        }
    }
    r.fillRoundedRect({cx - 2.5f, cy - 2.5f, 5.f, 5.f}, c, 2.5f);
}

void helpGlyph(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    r.strokeRect({b.x + 1.f, b.y + 1.f, b.w - 2.f, b.h - 2.f}, c, 1.3f);
    r.drawText({b.x + b.w * 0.5f - 3.5f, b.y + 0.5f}, "?", gfx::FontId{13}, c);
}

// 1.11 (R111, recette T1-1) : un texte trop long pour sa place finit par « … »
// (le bandeau serre coupait « Armoire_Gaz » en « Armoire_ », sans rien dire).
// La coupe ne tombe jamais au milieu d'un caractere : un « é » coupe en deux
// laisse un octet orphelin, que la police dessine en carre.
const std::string kEllipsis = "\xE2\x80\xA6";

std::string elided(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float maxW) {
    if (r.fitCharacters(s, f, maxW) >= s.size()) return s;
    const float room = maxW - r.measure(kEllipsis, f).width;
    std::size_t keep = room > 0.f ? std::min(r.fitCharacters(s, f, room), s.size()) : 0;
    while (keep > 0 && keep < s.size() && (static_cast<unsigned char>(s[keep]) & 0xC0) == 0x80) --keep;
    std::string out = s.substr(0, keep);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + kEllipsis;
}

// La place du nom dans la puce projet : apres l'icone (5 + 32 + 10 px), avant
// le chevron (20 px). paintProject et l'infobulle la calculent pareil.
float projectNameRoom(const gfx::Rect& r) { return r.w - 5.f - 32.f - 10.f - 20.f; }

} // namespace

// ------------------------------------------------------------ construction ----
TopBar::TopBar(std::string id) : ui::Widget(std::move(id)) {
    const auto part = [this](std::string key, std::string label, std::string tip, std::string action, ui::Icon icon,
                             Menu menu = Menu::None, bool iconOnly = false, bool primary = false) {
        Part p;
        p.key = std::move(key);
        p.label = std::move(label);
        p.tip = std::move(tip);
        p.action = std::move(action);
        p.icon = icon;
        p.menu = menu;
        p.iconOnly = iconOnly;
        p.primary = primary;
        parts_.push_back(std::move(p));
    };
    part("projet", "Projet", core::hasApi() ? "Le projet : ouvrir, enregistrer, importer le .XHW, son \xC3\xA9tat, Vers Control Expert, le menu principal"
                                            : "Le projet IHM : ouvrir, enregistrer, son \xC3\xA9tat, ses dossiers, le menu principal",
         {}, ui::Icon::Project, Menu::Project);
    part("version", "Version", "La version que tu modifies et d'o\xC3\xB9 elle part ; Terminer (FINISH), Livrer et verrouiller (LOCK), Version interm\xC3\xA9" "diaire",
         {}, ui::Icon::None, Menu::Version);
    part("annuler", "Annuler", "Annuler (Ctrl+Z)", "edit.undo", ui::Icon::Undo, Menu::None, true);
    part("retablir", "R\xC3\xA9tablir", "R\xC3\xA9tablir (Ctrl+Y)", "edit.redo", ui::Icon::Redo, Menu::None, true);
    part("historique", "Historique", "L'historique du projet : chaque action, son endroit, son heure ; un double clic y revient (Ctrl+H)",
         "edit.history", ui::Icon::History);
    part("nouveau", "Nouveau", "Cr\xC3\xA9" "er : une section, une unit\xC3\xA9, un bloc DFB, un type d\xC3\xA9riv\xC3\xA9, une variable, un rack, un module, une macro",
         {}, ui::Icon::None, Menu::New);
    part("simuler", "Simuler", "Lance le programme ici m\xC3\xAAme, cycle apr\xC3\xA8s cycle (F5) ; les valeurs se lisent au survol (F9 : Simulation \xE2\x80\xBA Vue d'ensemble)",
         "sim.run", ui::Icon::Play);
    part("arreter", "Arr\xC3\xAAter", "Arr\xC3\xAAte et remet chaque variable et chaque temporisation \xC3\xA0 z\xC3\xA9ro (Maj+F5)", "sim.stop", ui::Icon::Stop,
         Menu::None, true);
    part("cycle", "Un cycle", "Ex\xC3\xA9" "cute exactement un cycle", "sim.step", ui::Icon::StepOnce, Menu::None, true);
    // 1.10 : l'IHM a cote de l'API, son etat et son bouton.
    part("ihm", "IHM", "L'IHM simul\xC3\xA9" "e : un clic la d\xC3\xA9marre ou l'arr\xC3\xAAte, sans toucher \xC3\xA0 l'API (le programme). "
         "Ses commandes compl\xC3\xA8tes : l'onglet Simulation \xC2\xB7 IHM", "hmi.toggle", ui::Icon::Screen, Menu::None, true);
    // 1.10 (maquette, scene 2) : ses deux boutons, et le menu des deux (l'API et l'IHM).
    part("ihm-demarrer", "D\xC3\xA9marrer l'IHM", "D\xC3\xA9marrer l'IHM (F8) : l'API ne d\xC3\xA9marre pas avec elle", "hmi.start", ui::Icon::Play,
         Menu::None, true);
    part("ihm-arreter", "Arr\xC3\xAAter l'IHM", "Arr\xC3\xAAter l'IHM (Maj+F8) : l'API continue", "hmi.stop", ui::Icon::Stop, Menu::None, true);
    part("les-deux", "Les deux", "L'API et l'IHM ensemble : D\xC3\xA9marrer les deux, Tout arr\xC3\xAAter", {}, ui::Icon::None, Menu::Both);
    part("control-expert", "Vers Control Expert", "\xC3\x89" "crit src/MAST.XPG et src/CONFIG.XHW, pr\xC3\xAAts \xC3\xA0 r\xC3\xA9importer dans Control Expert",
         "project.exportSources", ui::Icon::Export, Menu::None, false, true);
    part("affichage", "Affichage", "Affichage : la disposition (onglets, c\xC3\xB4te \xC3\xA0 c\xC3\xB4te, mosa\xC3\xAFque, fen\xC3\xAAtres), le tableau de bord de l'API, l'onglet Macros, les panneaux, le th\xC3\xA8me",
         {}, ui::Icon::None, Menu::View);
    part("aide", "Aide", "L'aide (F1), les guides, le didacticiel, les nouveaut\xC3\xA9s, \xC3\xA0 propos", {}, ui::Icon::None, Menu::Help);
    // ---- Lot API 8 : bandeau haut ----
    //  Enregistrer dans la puce projet ; le chevron d'Annuler (les 10 dernieres
    //  actions) ; la cloche ; les taches de fond ; Deposer un fichier. Simuler,
    //  + Nouveau, Affichage et Aide deviennent des icones (leur libelle reste :
    //  actionForLabel les retrouve).
    part("enregistrer", "Enregistrer", "Enregistrer le projet (Ctrl+S)", "project.save", ui::Icon::Save);
    part("annuler-liste", "Revenir avant", "Les 10 derni\xC3\xA8res actions : un clic y revient (ou un clic long sur Annuler)",
         {}, ui::Icon::None, Menu::UndoList, true);
    part("cloche", "Notifications", "Les notifications : ce qui m\xC3\xA9rite ton attention, avec le bouton qui r\xC3\xA8gle chaque ligne",
         {}, ui::Icon::None, Menu::Notices, true);
    part("taches", "T\xC3\xA2" "ches de fond", "Les t\xC3\xA2" "ches de fond : une g\xC3\xA9n\xC3\xA9ration, un import, un export qui tourne",
         {}, ui::Icon::History, Menu::Tasks, true);
    part("deposer", "D\xC3\xA9poser un fichier", "D\xC3\xA9poser un fichier dans le projet : choisis-le (ou glisse-le sur la fen\xC3\xAAtre)",
         "files.drop", ui::Icon::Open, Menu::None, true);
    for (auto& p : parts_)
        if (p.key == "simuler" || p.key == "nouveau" || p.key == "affichage" || p.key == "aide") p.iconOnly = true;
    // ---- fin Lot API 8 : bandeau haut ----
    // 1.12.0 : CHAQUE APPLICATION, SON BANDEAU. XPGAnalyser API : ni l'IHM ni « Les deux » ;
    // XPGAnalyser IHM : ni la simulation de l'automate (Simuler, Arreter, Un cycle), ni
    // « Les deux », ni Vers Control Expert, ni + Nouveau (des sections, des blocs, des racks).
    std::erase_if(parts_, [](const Part& p) {
        if (!core::hasIhm() && (p.key == "ihm" || p.key == "ihm-demarrer" || p.key == "ihm-arreter" || p.key == "les-deux")) return true;
        if (!core::hasApi() && (p.key == "simuler" || p.key == "arreter" || p.key == "cycle" || p.key == "les-deux"
                                || p.key == "control-expert" || p.key == "nouveau"))
            return true;
        return false;
    });

    const auto e = [](std::vector<Entry>& v, std::string label, std::string shortcut, ui::Icon icon, std::string action) {
        Entry x;
        x.label = std::move(label);
        x.shortcut = std::move(shortcut);
        x.icon = icon;
        x.action = std::move(action);
        v.push_back(std::move(x));
    };
    const auto sep = [](std::vector<Entry>& v) { Entry x; x.separator = true; v.push_back(std::move(x)); };

    e(project_, "Ouvrir\xE2\x80\xA6", "Ctrl+O", ui::Icon::Open, "file.open");
    e(project_, "Enregistrer", "Ctrl+S", ui::Icon::Save, "project.save");
    e(project_, "Enregistrer sous\xE2\x80\xA6", "", ui::Icon::Document, "project.saveAs");
    sep(project_);
    if (core::hasApi()) {      // 1.12.0 : l'automate (XPGAnalyser API)
    e(project_, "Importer la configuration mat\xC3\xA9rielle (.XHW)\xE2\x80\xA6", "Ctrl+Maj+O", ui::Icon::Rack, "file.importHardware");   // 1.11 (T2) : la table help::keys
    // Lot 7 : un nouveau MAST dans le projet ouvert (le recapitulatif d'abord).
    e(project_, "Importer un .XPG (nouveau MAST)\xE2\x80\xA6", "", ui::Icon::Program, "file.importMast");
    e(project_, "R\xC3\xA9importer et r\xC3\xA9" "analyser", "", ui::Icon::Refresh, "analyze.run");   // Lot API 8 : F5 est Simuler (la maquette ; il n'a jamais relance l'analyse)
    sep(project_);
    }
    e(project_, "Ic\xC3\xB4ne du projet\xE2\x80\xA6", "", ui::Icon::Image, "project.icon");
    e(project_, "\xC3\x89tat du projet\xE2\x80\xA6", "", ui::Icon::Settings, "project.state");
    e(project_, "D\xC3\xA9verrouiller\xE2\x80\xA6", "", ui::Icon::Lock, "project.unlock");
    if (core::hasApi()) {
    e(project_, "Vers Control Expert\xE2\x80\xA6", "", ui::Icon::Export, "project.exportSources");
    // 1.8.0 : le programme lisible (Excel, PDF, texte), dans l'ordre d'execution.
    e(project_, "Exporter le programme lisible\xE2\x80\xA6", "Ctrl+Maj+E", ui::Icon::Document, "program.export");
    }
    sep(project_);
    e(project_, "Fermer le projet", "", ui::Icon::Close, "file.close");    // Lot API 8 : bandeau haut
    // 1.8.0 : les dossiers de l'application (XPGAnalyser.ini) : voir, ouvrir, changer.
    e(project_, "Dossiers de l'application\xE2\x80\xA6", "", ui::Icon::Folder, "app.folders");
    e(project_, "Revenir au menu principal", "Ctrl+Maj+H", ui::Icon::Project, "app.home");

    if (core::hasApi()) {      // 1.12.2 : les creations de l'automate dans XPGAnalyser API seulement
    e(create_, "Section", "", ui::Icon::Section, "create.section");
    e(create_, "Unit\xC3\xA9 de programme", "", ui::Icon::Program, "create.unit");
    e(create_, "Bloc DFB", "", ui::Icon::FunctionBlock, "create.dfb");
    e(create_, "Type d\xC3\xA9riv\xC3\xA9 (DDT)", "", ui::Icon::DerivedType, "create.ddt");
    e(create_, "Variable", "", ui::Icon::Variable, "create.variable");
    sep(create_);
    e(create_, "Rack", "", ui::Icon::Rack, "create.rack");
    e(create_, "Module", "", ui::Icon::Module, "create.module");
    sep(create_);
    e(create_, "Macro\xE2\x80\xA6", "", ui::Icon::Code, "macros.new");
    } else {
    // 1.12.2 : XPGAnalyser IHM - Projet > Nouveau listait les creations de l'automate (Section,
    // DFB, Rack...), qui ne menaient nulle part ici.
    e(create_, "Vue\xE2\x80\xA6", "", ui::Icon::Screen, "hmi.new.view");
    e(create_, "Popup\xE2\x80\xA6", "", ui::Icon::Layers, "hmi.new.popup");
    e(create_, "Symbole\xE2\x80\xA6", "", ui::Icon::Layers, "hmi.new.symbol");
    sep(create_);
    e(create_, "Script\xE2\x80\xA6", "", ui::Icon::Code, "hmi.new.script");
    e(create_, "Fonction IHM\xE2\x80\xA6", "", ui::Icon::Code, "hmi.new.function");
    e(create_, "Variable IHM\xE2\x80\xA6", "", ui::Icon::Variable, "hmi.new.variable");
    }

    // Lot API 7 : l'ecran de simulation (F9), l'explorateur de variables
    // (Ctrl+1) et les statistiques (Ctrl+5) ont quitte ce menu : ce sont des
    // onglets de l'API (l'arbre : API > Simulation, Variables, Statistiques).
    // Leurs actions et leurs raccourcis restent ; legacyButtons() retrouve
    // toujours "Simulate", "Variables", "Statistics".
    if (core::hasApi()) {      // 1.12.0 : l'automate seulement
    e(view_, "Tableau de bord de l'API", "", ui::Icon::Cpu, "api.dashboard");
    e(view_, "Onglet Macros", "", ui::Icon::Code, "macros.open");
    sep(view_);
    }
    // Lot 7 : L'AFFICHAGE MULTI-FENETRE - le centre en un groupe d'onglets, en
    // groupes cote a cote, en mosaique automatique ; un onglet dans une fenetre.
    e(view_, "Disposition : onglets", "", ui::Icon::Document, "layout.tabs");
    e(view_, "Disposition : c\xC3\xB4te \xC3\xA0 c\xC3\xB4te", "", ui::Icon::Layers, "layout.groups");
    e(view_, "Disposition : mosa\xC3\xAFque automatique", "", ui::Icon::Chart, "layout.mosaic");
    e(view_, "D\xC3\xA9tacher l'onglet dans une fen\xC3\xAAtre", "", ui::Icon::Screen, "layout.detach");
    sep(view_);
    e(view_, "Panneaux \xC3\xA0 afficher\xE2\x80\xA6", "", ui::Icon::Settings, "view.layout");
    sep(view_);
    e(view_, "Th\xC3\xA8me\xE2\x80\xA6", "", ui::Icon::Layers, "view.theme");
    // ---- Lot API 8 : l'explorateur de fichiers - le bouton ... ouvre celui de l'appli ou celui du systeme ----
    e(view_, "Explorateur de fichiers : l'appli ou le syst\xC3\xA8me", "", ui::Icon::Folder, "view.fileExplorer");

    // 1.10 (R2, menu Aide, rapides 1 a 5) : quatre blocs titres. Une seule entree
    // Nouveautes (la fenetre de P : la version, ce qui a ete manque) - plus de
    // "Nouveautes du lot 8", un nom interne : ses parcours sont les cartes
    // nouvelles du Didacticiel de l'API (l'action help.lot8 reste, pour la fenetre
    // des nouveautes). Les reperes : une case (son etat a droite). A propos.
    const auto head = [](std::vector<Entry>& v, const char* text) {
        Entry x;
        x.heading = true;
        x.label = text;
        v.push_back(std::move(x));
    };
    head(help_, "Chercher et lire");
    e(help_, "Aide", "F1", ui::Icon::Info, "help.open");
    e(help_, "Raccourcis clavier", "", ui::Icon::Document, "help.shortcuts");
    sep(help_);
    head(help_, "Les guides");
    // 1.12.0 : les guides de l'application.
    if (core::hasApi()) e(help_, "L'API et son programme", "", ui::Icon::Cpu, "help.open");
    if (core::hasIhm()) e(help_, "L'IHM", "", ui::Icon::Screen, "help.hmi");
    if (core::hasApi()) e(help_, "Les macros", "", ui::Icon::Code, "help.macros");
    if (core::hasApi()) e(help_, "Blocs DFB / DDT", "", ui::Icon::FunctionBlock, "help.blocs");
    if (core::hasIhm()) e(help_, "Les expressions", "", ui::Icon::Code, "help.expressions");   // 1.11 (chantier T3, D5)
    sep(help_);
    head(help_, "Apprendre");
    if (core::hasApi()) e(help_, "Didacticiel de l'API", "", ui::Icon::Play, "help.apiTutorial");     // lot API 7
    if (!core::hasApi()) e(help_, "Didacticiel de l'IHM", "", ui::Icon::Play, "help.hmiTutorial");    // 1.12.0 : XPGAnalyser IHM
    sep(help_);
    head(help_, "Nouveaut\xC3\xA9s et support");
    e(help_, "Nouveaut\xC3\xA9s\xE2\x80\xA6", "", ui::Icon::StarFilled, "help.news");
    e(help_, "Rep\xC3\xA8res des nouveaut\xC3\xA9s", "", ui::Icon::Ok, "help.newsMarks");
    e(help_, "Journal interne (historique des appels)\xE2\x80\xA6", "", ui::Icon::Document, "help.callTrail");   // 1.10.2 (CR)
    e(help_, "\xC3\x80 propos d'XPGAnalyser\xE2\x80\xA6", "", ui::Icon::Info, "help.about");
}

TopBar::~TopBar() = default;

void TopBar::setPopup(ui::PopupMenu* popup) {
    // Rappele a chaque retour sur l'ecran (un dialogue qui se ferme) : les
    // abonnements d'avant partent, sinon un choix de menu partait deux fois.
    links_.clear();
    popup_ = popup;
    if (!popup_) return;
    links_ += popup_->itemChosen->connect([this](int i) {
        if (opened_ == Menu::None) return;
        opened_ = Menu::None;
        invalidate();
        if (i >= 0 && static_cast<std::size_t>(i) < popupActions_.size()) {
            const std::string action = popupActions_[static_cast<std::size_t>(i)];
            trigger(action);
        }
    });
    links_ += popup_->dismissed->connect([this] { opened_ = Menu::None; invalidate(); });
}

void TopBar::setGoTo(ui::WidgetPtr w) {
    if (!w) return;
    goTo_ = &addChild(std::move(w));
    invalidateLayout();
}

void TopBar::setConfiguration(ui::WidgetPtr w) {
    if (!w) return;
    config_ = &addChild(std::move(w));
    // 1.12.0 : Release / Debug (la sortie vers Control Expert) est celle de l'automate.
    if (!core::hasApi()) config_->setVisibility(ui::Visibility::Collapsed);
    invalidateLayout();
}

void TopBar::setProject(std::string name, std::string state, bool modified) {
    if (name == name_ && state == state_ && modified == modified_) return;
    name_ = name.empty() ? std::string("Projet") : std::move(name);
    state_ = std::move(state);
    modified_ = modified;
    invalidateLayout();
    invalidate();
}

void TopBar::setVersion(VersionChip chip) {
    if (chip == version_) return;
    const bool relayout = chip.title != version_.title || chip.subtitle != version_.subtitle;
    version_ = std::move(chip);
    for (auto& p : parts_)
        if (p.key == "version") p.tip = version_.tip.empty() ? p.tip : version_.tip;
    if (relayout) invalidateLayout();
    invalidate();
}

void TopBar::setLogo(std::vector<std::uint8_t> rgba32) {
    if (rgba32 == logo_) return;
    logo_ = std::move(rgba32);
    logoDirty_ = true;
    invalidate();
}

void TopBar::setHistory(bool canUndo, std::string undoTip, bool canRedo, std::string redoTip) {
    canUndo_ = canUndo;
    canRedo_ = canRedo;
    for (auto& p : parts_) {
        if (p.key == "annuler") p.tip = std::move(undoTip);
        if (p.key == "retablir") p.tip = std::move(redoTip);
    }
    invalidate();
}

void TopBar::setHmi(Hmi state) {
    if (state == hmi_) return;
    hmi_ = state;
    invalidateLayout();
    invalidate();
}

void TopBar::setSimulation(Sim state, std::uint64_t scans) {
    if (state == sim_ && scans == scans_) return;
    const bool relabel = (state == Sim::Running) != (sim_ == Sim::Running);
    sim_ = state;
    scans_ = scans;
    for (auto& p : parts_)
        if (p.key == "simuler") {
            p.label = state == Sim::Running ? "Pause" : "Simuler";
            p.action = state == Sim::Running ? "sim.pause" : "sim.run";
            p.icon = state == Sim::Running ? ui::Icon::Pause : ui::Icon::Play;
        }
    if (relabel) invalidateLayout();
    invalidate();
}

ui::SizeHint TopBar::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {1200.f, kHeight};
    h.minimum = {400.f, kHeight};
    h.stretchX = 1.f;
    return h;
}

// ------------------------------------------------------------------ menus ----
const std::vector<TopBar::Entry>& TopBar::menuEntries(Menu m) const {
    static const std::vector<Entry> none;
    switch (m) {
        case Menu::Project: return project_;
        case Menu::New:     return create_;
        case Menu::View:    return view_;
        case Menu::Help:    return help_;
        case Menu::Version: return version_.menu;
        case Menu::Both: {   // 1.10 : les deux simulations ensemble
            static const std::vector<Entry> both = [] {
                std::vector<Entry> v;
                Entry h;
                h.label = "L'API et l'IHM ensemble";
                h.heading = true;
                v.push_back(h);
                Entry start;
                start.label = "D\xC3\xA9marrer les deux";
                start.icon = ui::Icon::Play;
                start.action = "both.start";
                v.push_back(start);
                Entry stop;
                stop.label = "Tout arr\xC3\xAAter";
                stop.icon = ui::Icon::Stop;
                stop.action = "all.stop";
                v.push_back(stop);
                return v;
            }();
            return both;
        }
        default:            return none;
    }
}

void TopBar::openMenu(Menu m) {
    if (!popup_ || m == Menu::None) return;
    // ---- Lot API 8 : bandeau haut (la liste d'Annuler, la cloche, les taches) ----
    if (m == Menu::UndoList || m == Menu::Notices || m == Menu::Tasks) { openLot8Menu(m); return; }
    // ---- fin Lot API 8 : bandeau haut ----
    const auto& entries = menuEntries(m);
    std::vector<ui::PopupMenu::Item> items;
    popupActions_.clear();
    // ---- Lot API 8 : bandeau haut ----
    //  Le menu Projet commence par les projets recents et + Nouveau (deux
    //  sous-menus) : la puce projet donne tout ce qui touche au projet.
    if (m == Menu::Project) {
        ui::PopupMenu::Item recent;
        recent.label = "Projets r\xC3\xA9" "cents";
        recent.icon = ui::Icon::History;
        for (const auto& path : recents_) {
            if (recent.children.size() >= 8) break;
            ui::PopupMenu::Item it;
            const auto slash = path.find_last_of("/\\");
            it.label = slash == std::string::npos || slash + 1 >= path.size() ? path : path.substr(slash + 1);
            it.shortcut = path;
            it.icon = ui::Icon::Folder;
            it.id = static_cast<int>(popupActions_.size());
            popupActions_.push_back("project.openRecent:" + path);
            recent.children.push_back(std::move(it));
        }
        recent.enabled = !recent.children.empty();
        if (!recent.enabled) recent.disabledReason = "aucun projet r\xC3\xA9" "cent";
        items.push_back(std::move(recent));
        ui::PopupMenu::Item create;
        create.label = "Nouveau";
        create.icon = ui::Icon::Document;
        for (const auto& en : create_) {
            ui::PopupMenu::Item it;
            if (en.separator) { it.separator = true; create.children.push_back(std::move(it)); continue; }
            it.label = en.label;
            it.icon = en.icon;
            it.id = static_cast<int>(popupActions_.size());
            popupActions_.push_back(en.action);
            create.children.push_back(std::move(it));
        }
        items.push_back(std::move(create));
        ui::PopupMenu::Item rule;
        rule.separator = true;
        items.push_back(std::move(rule));
    }
    // ---- fin Lot API 8 : bandeau haut ----
    for (const auto& en : entries) {
        ui::PopupMenu::Item it;
        if (en.separator) {
            it.separator = true;
            items.push_back(std::move(it));
            continue;
        }
        it.label = en.label;
        it.shortcut = en.shortcut;
        it.icon = en.icon;
        // 1.10 (R2, menu Aide) : la case des reperes (coche et etat a droite), et ce
        // qui reste a voir a droite de Nouveautes.
        if (m == Menu::Help && en.action == "help.newsMarks") {
            const bool shown = !noveltyCenter().marksHidden();
            it.icon = shown ? ui::Icon::Ok : ui::Icon::None;
            it.shortcut = shown ? "affich\xC3\xA9s" : "masqu\xC3\xA9s";
        }
        if (m == Menu::Help && en.action == "help.news")
            if (const int unseen = unseenNovelties(); unseen > 0)
                it.shortcut = std::to_string(unseen) + " \xC3\xA0 voir";
        if (en.heading) {
            it.heading = true;
            it.enabled = false;
            items.push_back(std::move(it));
            continue;
        }
        it.enabled = en.enabled && (!enabled_ || en.action.rfind("version.", 0) == 0 || enabled_(en.action));
        if (!it.enabled) it.disabledReason = !en.disabledReason.empty() ? en.disabledReason
                                           : en.action == "project.unlock" ? "le projet n'est pas LOCK" : "pas maintenant";
        it.id = static_cast<int>(popupActions_.size());
        popupActions_.push_back(en.action);
        items.push_back(std::move(it));
    }
    popup_->setItems(std::move(items));
    gfx::Rect anchor{};
    for (const auto& p : parts_)
        if (p.menu == m) anchor = p.rect;
    opened_ = m;
    popup_->openAt({anchor.x, anchor.y + anchor.h + 4.f}, surface_);
    invalidate();
}

// -------------------------------------------------------------- les actions ----
const std::vector<std::pair<std::string, std::string>>& TopBar::legacyButtons() {
    static const std::vector<std::pair<std::string, std::string>> k{
        {"Menu", "app.home"}, {"Open", "file.open"}, {"Save", "project.save"}, {"Save as", "project.saveAs"},
        {"Aide", "help.open"}, {"State", "project.state"}, {"D\xC3\xA9verrouiller", "project.unlock"},
        {"To Control Expert", "project.exportSources"}, {"Section", "create.section"}, {"Unit", "create.unit"},
        {"DFB", "create.dfb"}, {"DDT", "create.ddt"}, {"Variable", "create.variable"}, {"Rack", "create.rack"},
        {"Module", "create.module"}, {"Macros", "macros.open"}, {"Simulate", "sim.open"}, {"Run", "sim.run"},
        {"Pause", "sim.pause"}, {"Stop", "sim.stop"}, {"Step", "sim.step"}, {"Annuler", "edit.undo"},
        {"R\xC3\xA9tablir", "edit.redo"}, {"Historique", "edit.history"}, {"Hardware", "file.importHardware"},
        {"Refresh", "analyze.run"}, {"Variables", "view.variables"}, {"Statistics", "view.statistics"},
        {"Layout", "view.layout"},
    };
    return k;
}

bool TopBar::reaches(std::string_view action) const {
    for (const auto& p : parts_) if (p.action == action) return true;
    // Pause : la meme partie que Simuler, quand la simulation tourne.
    if (action == "sim.pause") return true;
    for (const auto m : {Menu::Project, Menu::New, Menu::View, Menu::Help})
        for (const auto& en : menuEntries(m))
            if (!en.separator && en.action == action) return true;
    return false;
}

std::string TopBar::actionForLabel(std::string_view label) const {
    const auto want = plainLabel(label);
    for (const auto& p : parts_)
        if (!p.action.empty() && plainLabel(p.label) == want) return p.action;
    for (const auto m : {Menu::Project, Menu::New, Menu::View, Menu::Help})
        for (const auto& en : menuEntries(m))
            if (!en.separator && plainLabel(en.label) == want) return en.action;
    for (const auto& [old, action] : legacyButtons())
        if (plainLabel(old) == want) return action;
    return {};
}

void TopBar::trigger(std::string_view action) {
    // ---- Lot API 8 : bandeau haut (la cloche : Tout marquer comme lu, a la barre seule) ----
    if (action == "bandeau.notices.read") { markNoticesRead(); return; }
    // ---- fin Lot API 8 : bandeau haut ----
    if (action.empty() || !sink_) return;
    const std::string keep(action);     // l'action peut rouvrir un menu et vider popupActions_
    XPG_TRACE(Menu, "barre : %s", keep.c_str());   // 1.10.2 (CR) : le journal interne
    sink_(keep);
}

bool TopBar::partEnabled(const Part& p) const {
    if (p.key == "annuler") return canUndo_;
    if (p.key == "retablir") return canRedo_;
    if (p.key == "arreter") return sim_ == Sim::Running || sim_ == Sim::Paused || sim_ == Sim::Halted;
    if (p.key == "cycle") return sim_ != Sim::Halted;
    if (p.key == "version") return !version_.title.empty();
    if (p.key == "simuler") return sim_ != Sim::Halted;
    if (p.key == "ihm-demarrer") return hmi_ != Hmi::Running;   // 1.10
    if (p.key == "ihm-arreter") return hmi_ == Hmi::Running;
    return true;
}

// ------------------------------------------------------------------- mise en place ----
void TopBar::layoutParts() const {
    // ---- Lot API 8 : bandeau haut ----
    //  Trois zones sur 44 px : TopBarLot8.cpp (layoutLot8). L'ancienne mise en
    //  place (une rangee, tout de la meme importance) est partie avec le lot 7.
    layoutLot8();
    // ---- fin Lot API 8 : bandeau haut ----
}

void TopBar::onLayout() {
    layoutParts();
    if (goTo_) goTo_->setBounds(goToRect_);
    if (config_) config_->setBounds(configRect_);
}

gfx::Rect TopBar::partRect(std::string_view part) const {
    layoutParts();
    if (part == "aller") return goToRect_;
    if (part == "configuration") return configRect_;
    if (part == "simulation") return simRect_;
    // ---- Lot API 8 : bandeau haut ----
    //  "etat" : l'etat de la simulation (sa couleur, le cycle, la mini-courbe) ;
    //  "palette" : Aller a / Faire... ; "historique" n'est plus dans le bandeau :
    //  le chevron d'Annuler, ou sa liste le donne.
    if (part == "etat") return stateRect_;
    if (part == "palette") return goToRect_;
    if (part == "historique") {
        for (const auto& p : parts_)
            if (p.key == "annuler-liste") return p.rect;
    }
    // ---- fin Lot API 8 : bandeau haut ----
    for (const auto& p : parts_)
        if (p.key == part) return p.rect;
    return {};
}

int TopBar::partAt(gfx::Point pt) const {
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        const auto& r = parts_[i].rect;
        if (r.w > 0.f && pt.x >= r.x && pt.x < r.x + r.w && pt.y >= r.y && pt.y < r.y + r.h) return static_cast<int>(i);
    }
    return -1;
}

// ------------------------------------------------------------------- dessin ----
void TopBar::paintProject(const ui::PaintContext& ctx, const Part& p, bool hot) const {
    const auto& c = ctx.theme.color;
    const auto r = p.rect;
    if (hot || opened_ == Menu::Project) ctx.r.fillRoundedRect(r, c.text.withAlpha(opened_ == Menu::Project ? 30 : 22), 6.f);
    // Lot API 6 : l'icone du projet, en 32 x 32 ; sinon le logo "X".
    const gfx::Rect logo{r.x + 5.f, r.y + std::floor((r.h - 32.f) * 0.5f), 32.f, 32.f};
    if (!logo_.empty()) {
        paintLogo(ctx, logo);
    } else {
        const gfx::Rect box{logo.x + 2.f, logo.y + 2.f, 28.f, 28.f};
        ctx.r.fillRoundedRect(box, c.accent, 7.f);
        const float lx = box.x + (box.w - ctx.r.measure("X", gfx::FontId{15}).width) * 0.5f;
        ctx.r.drawText({lx, box.y + (box.h - ctx.r.lineHeight(gfx::FontId{15})) * 0.5f}, "X", gfx::FontId{15}, c.selectionText);
        ctx.r.drawText({lx + 0.6f, box.y + (box.h - ctx.r.lineHeight(gfx::FontId{15})) * 0.5f}, "X", gfx::FontId{15}, c.selectionText);
    }
    const float tx = logo.right() + 10.f;
    // 1.11 (R111, T1-1) : coupe, le nom finit par « … » ; l'infobulle le donne entier.
    const auto name = elided(ctx.r, name_, gfx::FontId{15}, projectNameRoom(r));
    ctx.r.drawText({tx, r.y + 1.f}, name, gfx::FontId{15}, c.text);
    ctx.r.drawText({tx + 0.6f, r.y + 1.f}, name, gfx::FontId{15}, c.text);
    // la ligne d'etat : la pastille (NEW / DEV / FINISH / LOCK), le point orange
    const std::string st = state_.empty() ? std::string("export") : state_;
    const float pw = ctx.r.measure(st, kTiny).width + 10.f;
    const gfx::Rect pill{tx, r.y + 19.f, pw, 14.f};
    const auto tone = st == "LOCK" ? c.error : st == "DEV" ? c.accent : st == "export" || st == "NEW" ? c.textMuted : c.ok;
    ctx.r.fillRoundedRect(pill, tone.withAlpha(ctx.theme.isDark() ? 60 : 40), 3.f);
    ctx.r.drawText({pill.x + 5.f, pill.y + (pill.h - ctx.r.lineHeight(kTiny)) * 0.5f}, st, kTiny, ctx.theme.onSurface(tone));
    if (modified_) {
        const float dx = pill.right() + 8.f;
        ctx.r.fillRoundedRect({dx, pill.y + 4.f, 7.f, 7.f}, c.warning, 3.5f);
        // 1.11 (R111, T1-1) : le mot seulement s'il tient avant le chevron ; en mode
        // serre, il passait sous Enregistrer (« modifie »). Le point orange reste,
        // et Enregistrer le dit aussi.
        const std::string word = "modifi\xC3\xA9";
        if (ctx.r.measure(word, kSmall).width <= r.right() - 20.f - (dx + 11.f))
            ctx.r.drawText({dx + 11.f, pill.y + (pill.h - ctx.r.lineHeight(kSmall)) * 0.5f}, word, kSmall, c.textMuted);
    }
    chevron(ctx.r, r.right() - 11.f, r.y + r.h * 0.5f, c.textMuted);
}

void TopBar::paintLogo(const ui::PaintContext& ctx, const gfx::Rect& dst) const {
    if (logoOwner_ != &ctx.r) {     // un autre renderer (une capture hors ecran) : on refait la texture
        logoOwner_ = &ctx.r;
        logoTex_ = {};
        logoDirty_ = true;
    }
    if (logoDirty_) {
        if (logoTex_.v != 0) ctx.r.releaseImage(logoTex_);
        logoTex_ = logo_.size() == 32u * 32u * 4u ? ctx.r.createImage(logo_.data(), 32, 32) : gfx::TextureId{};
        logoDirty_ = false;
    }
    if (logoTex_.v != 0) {
        ctx.r.drawImage(logoTex_, dst);
        return;
    }
    // Un renderer sans images (les tests) : pixel par pixel.
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const auto* px = &logo_[static_cast<std::size_t>((y * 32 + x) * 4)];
            if (px[3] == 0) continue;
            ctx.r.fillRect({dst.x + static_cast<float>(x) * dst.w / 32.f, dst.y + static_cast<float>(y) * dst.h / 32.f, dst.w / 32.f, dst.h / 32.f},
                           gfx::Color{px[0], px[1], px[2], px[3]});
        }
}

void TopBar::paintVersion(const ui::PaintContext& ctx, const Part& p, bool hot) const {
    const auto& c = ctx.theme.color;
    const auto r = p.rect;
    if (r.w <= 0.f) return;
    const bool open = opened_ == Menu::Version;
    ctx.r.fillRoundedRect(r, c.border, 7.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, hot || open ? c.text.withAlpha(open ? 30 : 22) : c.panelBg, 6.f);
    const auto tone = version_.tone == "lock" ? c.error : version_.tone == "finish" ? c.ok : version_.tone == "new" ? c.textMuted : c.accent;
    const gfx::Rect disc{r.x + 7.f, r.y + (r.h - 24.f) * 0.5f, 24.f, 24.f};
    ctx.r.fillRoundedRect(disc, tone.withAlpha(ctx.theme.isDark() ? 60 : 40), 12.f);
    const auto icon = version_.tone == "lock" ? ui::Icon::Lock : version_.tone == "finish" ? ui::Icon::Ok : version_.tone == "new" ? ui::Icon::Star : ui::Icon::Document;
    ui::drawIcon(ctx.r, icon, {disc.x + 5.f, disc.y + 5.f, 14.f, 14.f}, ctx.theme.onSurface(tone));
    const float tx = disc.right() + 8.f, maxW = r.right() - tx - 20.f;
    // 1.11 (R111, T1-1) : la puce version, serree elle aussi (124 px), finit par « … ».
    const auto fit = [&](const std::string& s, gfx::FontId f) { return elided(ctx.r, s, f, maxW); };
    const auto title = fit(version_.title, gfx::FontId{14});
    ctx.r.drawText({tx, r.y + 1.f}, title, gfx::FontId{14}, c.text);
    ctx.r.drawText({tx + 0.6f, r.y + 1.f}, title, gfx::FontId{14}, c.text);
    ctx.r.drawText({tx, r.y + 18.f}, fit(version_.subtitle, kSmall), kSmall, c.textMuted);
    chevron(ctx.r, r.right() - 11.f, r.y + r.h * 0.5f, c.textMuted);
}

void TopBar::paintSim(const ui::PaintContext& ctx) const {
    if (simRect_.w <= 0.f) return;       // 1.12.0 : XPGAnalyser IHM n'a pas la simulation de l'automate
    const auto& c = ctx.theme.color;
    ctx.r.fillRoundedRect(simRect_, c.border, 7.f);
    ctx.r.fillRoundedRect({simRect_.x + 1.f, simRect_.y + 1.f, simRect_.w - 2.f, simRect_.h - 2.f}, c.panelBg, 6.f);
    // ---- Lot API 8 : bandeau haut ----
    //  L'etat EN COULEUR : vert en marche, bleu en pause, orange arretee, rouge
    //  en defaut, gris sans simulation ; dessous le cycle ; a droite la
    //  mini-courbe du temps de cycle (la periode en pointilles).
    const auto st = stateRect_;
    const auto tone = sim_ == Sim::Running ? c.ok : sim_ == Sim::Paused ? c.info : sim_ == Sim::Stopped ? c.warning
                    : sim_ == Sim::Halted ? c.error : c.textMuted;
    ctx.r.fillRoundedRect({st.x + 1.f, st.y + 1.f, st.w - 2.f, st.h - 2.f}, tone.withAlpha(ctx.theme.isDark() ? 38 : 26), 6.f);
    const gfx::Rect dot{st.x + 9.f, st.y + st.h * 0.5f - 4.f, 8.f, 8.f};
    if (sim_ == Sim::Running) ctx.r.fillRoundedRect({dot.x - 3.f, dot.y - 3.f, 14.f, 14.f}, tone.withAlpha(50), 7.f);
    ctx.r.fillRoundedRect(dot, tone, 4.f);
    // 1.10 : "API" devant l'etat - l'IHM a sa pastille a cote.
    const std::string line1 = std::string(hmi_ == Hmi::None ? "" : "API ")
                            + (sim_ == Sim::Running ? "en marche" : sim_ == Sim::Paused ? "en pause"
                               : sim_ == Sim::Halted ? "en d\xC3\xA9" "faut" : "arr\xC3\xAAt\xC3\xA9" "e");
    const std::string line2 = sim_ == Sim::Off ? std::string("F5 la lance") : "cycle " + thousands(scans_);
    const float tx = dot.right() + 7.f;
    ctx.r.drawText({tx, st.y + 2.f}, line1, kSmall, c.text);
    ctx.r.drawText({tx + 0.6f, st.y + 2.f}, line1, kSmall, c.text);
    ctx.r.drawText({tx, st.y + 18.f}, line2, kTiny, c.textMuted);
    if (st.w > 120.f) paintSparkline(ctx, {st.right() - 64.f, st.y + 9.f, 56.f, st.h - 18.f}, tone);
    // les separations entre les boutons du bloc
    for (const auto* key : {"arreter", "cycle"}) {
        const auto r = partRect(key);
        if (r.w > 0.f) ctx.r.fillRect({r.x - 1.f, simRect_.y + 6.f, 1.f, simRect_.h - 12.f}, c.border);
    }
    // ---- fin Lot API 8 : bandeau haut ----
}

// 1.10 : la pastille de l'IHM - verte en marche, orange arretee ; dessous, ce
// qu'un clic fait.
void TopBar::paintHmi(const ui::PaintContext& ctx, const Part& p, bool hot) const {
    const auto& c = ctx.theme.color;
    const auto r = p.rect;
    const auto tone = hmi_ == Hmi::Running ? c.ok : c.warning;
    ctx.r.fillRoundedRect(r, hot ? c.accent.withAlpha(120) : c.border, 7.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, 6.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, tone.withAlpha(ctx.theme.isDark() ? 38 : 26), 6.f);
    const gfx::Rect dot{r.x + 9.f, r.y + r.h * 0.5f - 4.f, 8.f, 8.f};
    if (hmi_ == Hmi::Running) ctx.r.fillRoundedRect({dot.x - 3.f, dot.y - 3.f, 14.f, 14.f}, tone.withAlpha(50), 7.f);
    ctx.r.fillRoundedRect(dot, tone, 4.f);
    const float tx = dot.right() + 7.f;
    const std::string line1 = hmi_ == Hmi::Running ? "IHM en marche" : "IHM arr\xC3\xAAt\xC3\xA9" "e";
    ctx.r.drawText({tx, r.y + 2.f}, line1, kSmall, c.text);
    ctx.r.drawText({tx + 0.6f, r.y + 2.f}, line1, kSmall, c.text);
    ctx.r.drawText({tx, r.y + 18.f}, hmi_ == Hmi::Running ? "Maj+F8 l'arr\xC3\xAAte" : "F8 la d\xC3\xA9marre", kTiny, c.textMuted);
}

void TopBar::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    surface_ = ctx.r.surfaceSize();
    if (!hovered()) hover_ = -1;
    layoutParts();
    ctx.r.fillRect(b, c.headerBg);
    ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
    ctx.r.pushClip(b);
    // Lot API 8 : bandeau haut - les groupes encadres (le projet et son
    // Enregistrer, Annuler / Retablir, la sortie) au lieu des separations.
    paintLot8Groups(ctx);
    paintSim(ctx);
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        const auto& p = parts_[i];
        if (p.rect.w <= 0.f) continue;
        const bool enabled = partEnabled(p);
        const bool hot = enabled && static_cast<int>(i) == hover_;
        if (p.key == "projet") { paintProject(ctx, p, hot); continue; }
        if (p.key == "version") { paintVersion(ctx, p, hot); continue; }
        // ---- Lot API 8 : bandeau haut ----
        if (p.key == "enregistrer") { paintSave(ctx, p, hot); continue; }
        if (p.key == "cloche") { paintBell(ctx, p, hot); continue; }
        if (p.key == "ihm") { paintHmi(ctx, p, hot); continue; }   // 1.10
        if (p.key == "taches") { paintTasks(ctx, p, hot); continue; }
        if (p.key == "annuler-liste") {
            if (hot || opened_ == Menu::UndoList) ctx.r.fillRoundedRect(p.rect, c.text.withAlpha(opened_ == Menu::UndoList ? 30 : 22), 5.f);
            chevron(ctx.r, p.rect.x + p.rect.w * 0.5f, p.rect.y + p.rect.h * 0.5f, undoList_.empty() ? c.textDisabled : c.textMuted);
            continue;
        }
        // ---- fin Lot API 8 : bandeau haut ----
        const bool open = p.menu != Menu::None && opened_ == p.menu;
        if (p.primary) {
            ctx.r.fillRoundedRect(p.rect, c.accent.withAlpha(hot ? 90 : 60), 6.f);
            ctx.r.strokeRect(p.rect, c.accent, 1.f);
        } else if (hot || open) {
            ctx.r.fillRoundedRect(p.rect, c.text.withAlpha(open ? 30 : 22), 6.f);
        }
        if (static_cast<int>(i) == pressed_ && enabled) ctx.r.fillRoundedRect(p.rect, c.accent.withAlpha(70), 6.f);
        const gfx::Color col = !enabled ? c.textDisabled : p.primary ? ctx.theme.onSurface(c.accent) : c.text;
        float x = p.rect.x + 9.f;
        const gfx::Rect ib{x, p.rect.y + (p.rect.h - 17.f) * 0.5f, 17.f, 17.f};
        const auto iconCol = !enabled ? c.textDisabled
                           : (p.key == "simuler" && sim_ != Sim::Running) || p.key == "ihm-demarrer" ? c.ok
                           : p.primary ? ctx.theme.onSurface(c.accent) : c.textMuted;
        bool drew = true;
        if (p.menu == Menu::New) plusGlyph(ctx.r, ib, col);
        else if (p.menu == Menu::View) eyeGlyph(ctx.r, ib, col);
        else if (p.menu == Menu::Help) {
            helpGlyph(ctx.r, ib, col);
            // 1.10 (R2) : la pastille orange (la couleur des reperes) tant qu'une
            // nouveaute n'a pas ete vue.
            if (const int unseen = unseenNovelties(); unseen > 0) {
                const std::string t = unseen > 9 ? std::string("9+") : std::to_string(unseen);
                const float w = std::max(14.f, ctx.r.measure(t, kHelpBadge).width + 7.f);
                const gfx::Rect badge{ib.right() - 5.f, p.rect.y + 1.f, w, 14.f};
                ctx.r.fillRoundedRect(badge, ui::novelty::orange(ctx.theme), 7.f);   // I2 (note de P) : l'orange des reperes
                ctx.r.drawText({badge.x + (badge.w - ctx.r.measure(t, kHelpBadge).width) * 0.5f,
                                badge.y + (badge.h - ctx.r.lineHeight(kHelpBadge)) * 0.5f},
                               t, kHelpBadge, c.selectionText);
            }
        }
        else if (p.icon != ui::Icon::None) ui::drawIcon(ctx.r, p.icon, ib, iconCol);
        else drew = false;
        if (p.iconOnly && p.menu == Menu::None) {
            // l'icone seule, centree
            continue;
        }
        if (drew) x += 24.f;
        const bool compactLabel = p.rect.w <= 36.f;
        if (!p.iconOnly && !compactLabel)
            ctx.r.drawText({x, p.rect.y + (p.rect.h - ctx.r.lineHeight(kLabel)) * 0.5f}, p.label, kLabel, col);
        if (p.menu != Menu::None) chevron(ctx.r, p.rect.right() - 11.f, p.rect.y + p.rect.h * 0.5f, c.textMuted);
    }
    ctx.r.popClip();
}

// ------------------------------------------------------------------- souris ----
// Lot 7 : l'infobulle a un endroit de la barre, calculee avec ce que la barre
// sait MAINTENANT (le survol la pose, l'hote la relit tant qu'elle est ouverte).
std::string TopBar::tipAt(gfx::Point p) const {
    const int h = partAt(p);
    if (h >= 0) {
        const auto& part = parts_[static_cast<std::size_t>(h)];
        // Lot API 6 : sur l'icone du projet, ce qu'un clic y fait.
        if (part.key == "projet" && !name_.empty() && p.x < part.rect.x + 40.f)
            return "L'ic\xC3\xB4ne du projet : un clic ouvre son \xC3\xA9" "diteur (une galerie, et un mini paint en 32 x 32)";
        // 1.11 (R111, recette T1-1) : le nom coupe (bandeau serre) : l'infobulle le donne entier.
        if (part.key == "projet" && !name_.empty() && ui::measureWidth(name_, gfx::FontId{15}) > projectNameRoom(part.rect))
            return name_ + "\n" + part.tip;
        // ---- Lot API 8 : bandeau haut (ce que chaque partie dit maintenant) ----
        if (part.key == "enregistrer")
            return modified_ ? std::string("Des modifications attendent : Enregistrer (Ctrl+S)") : std::string("Tout est enregistr\xC3\xA9 (Ctrl+S)");
        if (part.key == "cloche") {
            const int n = unreadNotices();
            return (n > 0 ? std::to_string(n) + " notification" + (n > 1 ? "s" : "") + " non lue" + (n > 1 ? "s" : "")
                          : std::string("Aucune notification non lue"))
                   + "\nLes expressions impossibles, les mises \xC3\xA0 jour de biblioth\xC3\xA8que, les exports finis, la simulation";
        }
        if (part.key == "aide") {   // 1.10 (R2)
            const int unseen = unseenNovelties();
            return part.tip + (unseen > 0 ? "\n" + std::to_string(unseen) + " nouveaut\xC3\xA9(s) pas encore vue(s) : Aide \xE2\x80\xBA Nouveaut\xC3\xA9s\xE2\x80\xA6"
                                          : std::string{});
        }
        if (part.key == "taches")
            return tasks_.empty() ? std::string("Aucune t\xC3\xA2" "che de fond en cours")
                                  : tasks_.front().label + (tasks_.size() > 1 ? " (+" + std::to_string(tasks_.size() - 1) + ")" : std::string{})
                                        + "\nUn clic : le d\xC3\xA9tail, et Annuler";
        // ---- fin Lot API 8 : bandeau haut ----
        return part.tip;
    }
    // L'etat de la simulation : l'etat et le cycle du moment ; pourquoi elle
    // s'est arretee.
    const auto st = partRect("etat");
    const bool over = p.x >= st.x && p.x < st.x + st.w && p.y >= st.y && p.y < st.y + st.h;
    if (!over) return {};
    const std::string state = sim_ == Sim::Running ? "en marche" : sim_ == Sim::Paused ? "en pause"
                      : sim_ == Sim::Halted ? "arr\xC3\xAAt\xC3\xA9" "e (d\xC3\xA9" "faut)" : "arr\xC3\xAAt\xC3\xA9" "e";
    std::string tip = "La simulation : " + state + " \xC2\xB7 cycle " + thousands(scans_);
    tip += "\n" + (simNote_.empty() ? std::string("L'\xC3\xA9tat de la simulation et le num\xC3\xA9ro du cycle") : simNote_);
    tip += "\nUn clic : Simulation \xE2\x80\xBA Vue d'ensemble";   // Lot API 8 : Centre de simulation
    return tip;
}

std::string TopBar::liveTooltip(gfx::Point mouse) const { return tipAt(mouse); }

ui::EventResult TopBar::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = partAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        // Lot 7 : le texte du moment ; l'hote le relit tant que l'infobulle est
        // ouverte (liveTooltip). Le poser ici garde tooltip() juste (et fait de
        // la barre la cible de l'infobulle).
        setTooltip(tipAt(m->pos));
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        pressed_ = partAt(m->pos);
        // ---- Lot API 8 : Centre de simulation ----
        // L'etat de la simulation (en marche, cycle...) se clique : la Vue d'ensemble.
        if (pressed_ < 0 && partRect("etat").contains(m->pos)) pressed_ = -2;
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : bandeau haut (un clic long sur Annuler : la liste) ----
        pressedAt_ = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        // ---- fin Lot API 8 : bandeau haut ----
        invalidate();
        return pressed_ >= 0 || pressed_ == -2 ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseUp>(&ev)) {
        const int was = pressed_;
        pressed_ = -1;
        invalidate();
        // ---- Lot API 8 : Centre de simulation (l'etat -> la Vue d'ensemble) ----
        if (was == -2) {
            if (partRect("etat").contains(m->pos)) trigger("sim.center");
            return ui::EventResult::Consumed;
        }
        // ---- fin Lot API 8 ----
        if (was >= 0 && partAt(m->pos) == was) {
            const auto& p = parts_[static_cast<std::size_t>(was)];
            if (!partEnabled(p)) return ui::EventResult::Consumed;
            // Lot API 6 : l'icone du projet (le carre de gauche) ouvre son editeur.
            if (p.key == "projet" && m->pos.x < p.rect.x + 40.f && !name_.empty()) {
                trigger("project.icon");
                return ui::EventResult::Consumed;
            }
            // ---- Lot API 8 : bandeau haut (un clic long sur Annuler : la liste) ----
            const double held = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - pressedAt_;
            if (p.key == "annuler" && held > 0.5 && !undoList_.empty()) {
                openMenu(Menu::UndoList);
                return ui::EventResult::Consumed;
            }
            // ---- fin Lot API 8 : bandeau haut ----
            if (p.menu != Menu::None) openMenu(p.menu);
            else trigger(p.action);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
