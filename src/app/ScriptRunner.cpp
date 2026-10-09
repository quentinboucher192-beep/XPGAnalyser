#include "ScriptRunner.hpp"
#include "tutorial/TutorialApp.hpp"
#include "tutorial/TutorialStageApp.hpp"
#include "tutorial/TutorialOverlay.hpp"   // 1.11.1 (T1, R111-18) : clic-souris lecteur:<partie>
#include "../help/TutorialLaunch.hpp"
#include "../help/TutorialPlayer.hpp"
#include "../help/TutorialVerify.hpp"   // 1.11 (T1, tranche 16) : l'A toi deja vrai avant ses gestes
#include "hmi/HmiTutorialDeduce.hpp"    // 1.11 (T1, tranche 18) : tutoriel-compte, la raison du deducteur (whyNoTutorial)
#include "HistoryPanel.hpp"
#include "NoveltyCenter.hpp"              // 1.10 (chantier P) : les commandes nouveautes-*
#include "../ui/NoveltyBoard.hpp"

#include "App.hpp"
#include "Capture.hpp"
#include "hmi/HmiCommPanes.hpp"         // lot 15 : les equipements, le reseau du PC
#include "hmi/HmiEditor.hpp"
#include "hmi/HmiValuePicker.hpp"        // 1.11.3 : le selecteur de valeur (selecteur-...)
#include "hmi/HmiTypePicker.hpp"   // 1.11.19 (refonte, lot 6) : les commandes choix-type-...
#include "hmi/HmiActionDialogs.hpp"      // 1.11.9 : les fenetres des actions (fenetre-action)
#include "hmi/HmiDuplicateDialog.hpp"  // 1.10.2 (chantier D) : "Dupliquer..."
#include "hmi/HmiMemoryMap.hpp"         // lot 17 : la carte memoire
#include "hmi/HmiTwinValues.hpp"        // lot 18 : les valeurs simulees
#include "hmi/HmiModbusToolPane.hpp"     // lot 15 : l'outil Modbus
#include "hmi/HmiCyclicPage.hpp"         // 1.9 : la lecture cyclique a plusieurs requetes
#include "hmi/HmiNetDiagram.hpp"
#include "hmi/HmiHelpPane.hpp"
#include "hmi/HmiTutorial.hpp"               // lot 21 : les parcours du didacticiel
#include "hmi/HmiTemplateGallery.hpp"      // lot 20 : la galerie des modeles
#include "hmi/HmiQualityPanes.hpp"
#include "hmi/HmiSimulation.hpp"
#include "hmi/HmiScriptPanes.hpp"         // 1.11.16 : ihm-curseur
#include "hmi/HmiFunctionPanes.hpp"       // 1.11.17 : volet-message (le volet des fonctions)
#include "hmi/HmiOperatorPanes.hpp"       // 1.11.17 : volet-message (le volet des operateurs)
#include "hmi/HmiSimVarTree.hpp"         // 1.11.5 : les variables en arbre
#include "hmi/HmiPublicVarsPane.hpp"     // 1.9 : les structures des esclaves simules (vars-dossier, vars-choisir)
#include "screens/StationScreen.hpp"      // lot 14 : le poste d'exploitation
#include "screens/HelpChrome.hpp"         // lot macros 1 : les onglets de l'aide
#include "screens/LibraryHelpScreen.hpp"  // lot macros 1 : les pages Macros et Blocs de l'aide
#include "screens/HelpCenterScreen.hpp"   // 1.11 (T2) : le centre d'aide unique (aide-sujet)
#include "MacroFormView.hpp"                // lot macros 1 : le formulaire d'une macro
#include "MacrosPane.hpp"                   // lot macros 1 : l'onglet Macros
#include "TopBar.hpp"                       // lot API 2 : la barre du haut
#include "BackgroundTasks.hpp"              // Lot API 8 : bandeau haut (taches de fond, avis de la cloche)
#include "TypePanes.hpp"                    // lot API 7 : deballer (types, DFB, unites)
#include "VariablesPane.hpp"                // lot API 7 : deballer (variables)
#include "hmi/HmiTrails.hpp"                // lot API 7 : les cartes des parcours de l'API
#include "ApiPanes.hpp"                     // lot API 2 : le tableau de bord de l'API
#include "AnimationTablesPane.hpp"          // lot API 3 : les tables d'animation
#include "ApiListKit.hpp"                   // lot API 5 : les pastilles des filtres
#include "RackView.hpp"                     // lot API 4 : les racks (un module par script)
#include "VariablePicker.hpp"               // lot API 3 : le choix des variables
#include "MacroEditorView.hpp"               // lot API 6 : le mode Modifier des macros
#include "PixelIconEditor.hpp"               // lot API 6 : l'icone du projet
#include "ThemeGallery.hpp"                  // lot API 6 : la galerie des themes
#include "VersionsPane.hpp"                  // lot API 6 : la frise qui defile
#include "SimulationPane.hpp"                // lot API 7 : l'onglet API > Simulation
#include "StatisticsPane.hpp"                // lot API 7 : l'onglet API > Statistiques
#include "screens/Screens.hpp"               // lot API 7 : MainAnalysisScreen::apiPaneContent
#include "RenameDialog.hpp"                  // lot 7 : renommer, renommer-onglet, renommer-confirmer...
#include "hmi/HmiVariablePanes.hpp"          // lot API 8 : renommer une variable IHM (F2, le nom tape)
#include "hmi/HmiPanes.hpp"                  // ---- Lot API 8 : les expressions impossibles (le rapport) ----
#include "../hmi/HmiModbus.hpp"                // 1.9 (chantier C) : un vrai appareil de laboratoire
#include "../hmi/HmiTwin.hpp"
#include "SectionComparePane.hpp"            // 1.8.0 : comparer (le rapport au journal du script)
#include "ImportJobDialog.hpp"          // 1.8.0 : attendre-import
#include "hmi/HmiPanels.hpp"                 // ---- Lot API 8 : les expressions impossibles (la grille) ----
#include "hmi/HmiTreeData.hpp"               // 1.10.3 (Q1103) : objet-famille (les familles d'un objet)
#include "hmi/HmiOperatorPanes.hpp"          // 1.10.1 (U2) : la fenetre Ajouter un operateur
#include "SimDebugPane.hpp"                  // Lot API 8 : Simulation > Debogage (debogage-..., espion-..., point-arret-marge)
#include "SimConditionDialog.hpp"            // Lot API 8 : Simulation > Debogage (2e partie) : point-arret-condition-marge
#include "SimCenter.hpp"                     // ---- Lot API 8 : Centre de simulation (centre, etat-bandeau, journal-..., courbe-..., forcages-...) ----
#include "SimStatus.hpp"                     // ---- Lot API 8 : Centre de simulation ----
#include "ImportMastDialog.hpp"              // lot 7 : le recapitulatif d'un nouveau MAST
#include "DropFilesDialog.hpp"               // ---- Lot API 8 : glisser n'importe quel fichier (depot-...) ----
#include "../ui/ThemeFile.hpp"               // ---- Lot API 8 : themes (theme-...) ----
#include "ThemeEditor.hpp"                   // ---- Lot API 8 : themes (l'editeur) ----
#include "FileExplorerHost.hpp"              // ---- Lot API 8 : l'explorateur de fichiers (explorateur-...) ----
#include "../ui/widgets/FileExplorer.hpp"    // ---- Lot API 8 : l'explorateur de fichiers ----
#include "../hmi/HmiVersions.hpp"
#include "../hmi/HmiCrypto.hpp"
#include "../hmi/HmiGuide.hpp"
#include "../hmi/HmiHistory.hpp"
#include "../hmi/HmiModel.hpp"
#include "../menu/IMenu.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/ColorPalette.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : propriete-retirer, propriete-menu
#include "../ui/widgets/TableFilters.hpp"    // lot recherche : la fenetre du filtre d'une colonne
#include "../ui/widgets/SearchField.hpp"     // ---- Lot API 8 : les filtres retenus (chercher, filtres-retenus) ----
#include "../ui/widgets/HelpView.hpp"       // Lot API 8 : didacticiels et aide (aide-section)
#include "TutorialsLot8.hpp"                // Lot API 8 : didacticiels et aide (aide-nouveautes, aide-raccourcis, aide-page)
#include "../ui/widgets/PathBrowse.hpp"         // parcourir : le bouton ... d'un champ de chemin
#include "hmi/HmiObjectAlarmPanes.hpp"           // 1.9 : les sous-onglets d'un symbole (sous-onglet Alarmes)

#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>

namespace app {

namespace {
// 1.11 (T1, tranche 9) : le bilan du verificateur en lot (tutoriel-lot, tutoriel-bilan) :
// une ligne par tutoriel et variante joues (tutoriel ... tutoriel-quitter). Une session
// n'a qu'un ScriptRunner : l'etat est celui du fichier (ScriptRunner.hpp reste tel quel).
struct TutoRow {
    std::string subject, variant;
    std::size_t steps = 0, ok = 0;
    int failures = 0;
    std::string firstFault;
    // Tranche 16 : les "A toi" deja vrais avant leurs gestes (avertissements, pas des fautes) :
    // "etape N : <conditions>", separes par " ; ".
    std::size_t alreadyTrue = 0;
    std::string alreadyTrueText;
};
std::vector<TutoRow> g_tutoRows;
bool g_tutoRowOpen = false;
int g_tutoStepMark = 0;                                   // failures_ au debut de l'etape (tutoriel-aller)
double g_tutoClockNoted = -1.0;                           // 1.11.2 : l'horloge notee par tutoriel-etat / tutoriel-avance
std::vector<std::pair<std::string, std::string>> g_tutoAbsent;   // tutoriel-absent <n> : le sujet, la faute


using ui::Key;
using ui::KeyMods;
using ui::MouseButton;

// Lot API 7 : attendre-cycles N - le compteur de cycles a atteindre pendant
// l'attente en cours (une session rejouee a la fois : un seul ScriptRunner).
std::uint64_t g_cyclesTarget = 0;

// ${NOM} : la variable d'environnement NOM. Un script se rejoue ainsi sur une
// autre machine sans etre reecrit (set XPG=D:\\exports\\MAST.XPG).
std::string expand(const std::string& line) {
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '$' && i + 1 < line.size() && line[i + 1] == '{') {
            const auto end = line.find('}', i + 2);
            if (end != std::string::npos) {
                const std::string name = line.substr(i + 2, end - i - 2);
                if (const char* v = std::getenv(name.c_str())) out += v;
                i = end;
                continue;
            }
        }
        out += line[i];
    }
    return out;
}

// Une ligne en mots ; "entre guillemets" fait un seul mot, \" et \\ s'y echappent.
std::vector<std::string> words(const std::string& raw) {
    const std::string line = expand(raw);
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size()) break;
        std::string w;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') {
                // \" et \\ seulement : un autre \ reste tel quel (C:\Images,
                // le \n d'un texte multiligne ou d'un script).
                if (line[i] == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) ++i;
                w += line[i++];
            }
            ++i;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) w += line[i++];
        }
        out.push_back(std::move(w));
    }
    return out;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Lot API 2 : l'arbre de l'API est en francais. Les sessions d'avant disent
// encore "Program units" ou "Animation tables" : le libelle d'avant designe le
// dossier d'aujourd'hui.
std::string treeAlias(const std::string& part) {
    static const std::pair<const char*, const char*> k[] = {
        {"Derived data types", "Types d\xC3\xA9riv\xC3\xA9s"}, {"DFB types", "Blocs DFB"},
        {"Program units", "Unit\xC3\xA9s de programme"}, {"Tasks", "T\xC3\xA2" "ches"},
        {"Animation tables", "Tables d'animation"}, {"Ordre d'execution", "Ordre d'ex\xC3\xA9" "cution"},
        {"Racks (", "Racks et modules ("}, {"Inputs (", "Entr\xC3\xA9" "es ("}, {"Outputs (", "Sorties ("}};
    for (const auto& [old, now] : k)
        if (part.rfind(old, 0) == 0) return std::string(now) + part.substr(std::string_view(old).size());
    return part;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return lower(text).rfind(lower(prefix), 0) == 0;
}

gfx::Point centre(const gfx::Rect& r) { return {r.x + r.w * 0.5f, r.y + r.h * 0.5f}; }

// "3" : un rang ; "hmi.find.find" : un identifiant de widget (lot 12).
bool isNumber(std::string_view s) {
    if (s.empty()) return false;
    for (const char c : s) if (c < '0' || c > '9') return false;
    return true;
}

template <class F>
void walk(ui::Widget& w, F&& f) {
    f(w);
    for (const auto& c : w.children()) walk(*c, f);
}

bool shown(const ui::Widget& w) {
    for (const ui::Widget* p = &w; p; p = p->parent())
        if (!p->visible()) return false;
    return true;
}

// 1.10 (chantier Q) : la palette de couleurs ouverte - celle d'une boite de
// dialogue au premier plan (editeur de theme, d'icones) d'abord, sinon celle
// de l'onglet (une case Couleur d'une grille).
ui::ColorPalette* openColorPalette(menu::IMenu* top, ui::Widget* page) {
    ui::ColorPalette* found = nullptr;
    auto look = [&](ui::Widget& x) {
        if (auto* p = dynamic_cast<ui::ColorPalette*>(&x); !found && p && p->isOpen()) found = p;
    };
    if (top)
        if (ui::Widget* root = top->widgetRoot()) walk(*root, look);
    if (!found && page) walk(*page, look);
    return found;
}

// Lot API 7 : le volet d'un onglet de l'API ("simulation", "statistiques"), par
// l'ecran d'analyse : ou qu'il soit - derriere un autre onglet, dans un autre
// groupe d'onglets, dans une fenetre detachee - et pas seulement dans l'onglet
// courant ni dans l'arbre de widgets de l'ecran. `open` : pas encore ouvert, il
// s'ouvre (comme F9, Ctrl+5). Nul : pas l'ecran d'analyse au-dessus (un
// dialogue ouvert), ou pas de projet.
template <class Pane>
Pane* apiPaneOf(App& app, const std::string& key, bool open) {
    auto* screen = dynamic_cast<MainAnalysisScreen*>(app.menus().top());
    return screen ? dynamic_cast<Pane*>(screen->apiPaneContent(key, open)) : nullptr;
}

// 1.11 (T1) : les noms des touches sont dans UiDriver (le lecteur des tutoriels les lit aussi).
bool parseKey(const std::string& combo, Key& key, KeyMods& mods) { return UiDriver::parseKey(combo, key, mods); }

} // namespace

// ---------------------------------------------------------------------------
core::Result<std::unique_ptr<ScriptRunner>> ScriptRunner::fromFile(App& app, const std::string& path,
                                                                   std::string capturesDir) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return core::Err<core::Error>(core::Error{core::ErrorCode::FileNotFound, "script introuvable", path});
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    std::filesystem::path dir = capturesDir.empty() ? std::filesystem::path(path).parent_path()
                                                    : std::filesystem::path(capturesDir);
    return std::unique_ptr<ScriptRunner>(new ScriptRunner(app, std::move(lines), std::move(dir)));
}

ScriptRunner::ScriptRunner(App& app, std::vector<std::string> lines, std::filesystem::path capturesDir)
    : app_(app), lines_(std::move(lines)), capturesDir_(std::move(capturesDir)), driver_(app) {}

void ScriptRunner::fail(const std::string& why) {
    ++failures_;
    if (g_tutoRowOpen && !g_tutoRows.empty()) {   // tranche 9 : la faute va au tutoriel en cours
        auto& row = g_tutoRows.back();
        ++row.failures;
        if (row.firstFault.empty()) row.firstFault = why;
    }
    std::fprintf(stderr, "[script] ligne %zu : %s\n", pc_ + 1, why.c_str());
}

bool ScriptRunner::afterRender(gfx::IRenderer& renderer) {
    ++sinceInput_;
    // 1.11 (T1) : encadrer et dire, par-dessus l'image qui vient d'etre dessinee
    // (avant sa presentation : une capture les prend).
    if (spot_) {
        const gfx::Rect r{spot_->x - 4.f, spot_->y - 4.f, spot_->w + 8.f, spot_->h + 8.f};
        renderer.strokeRect(r, gfx::Color::rgb(0xF39C12), 3.f);
    }
    if (!say_.empty()) {
        const auto size = renderer.surfaceSize();
        const gfx::FontId font{16};   // la police du texte courant (ui::Fonts::ui) ; {} est la police de secours, sans accents
        const auto m = renderer.measure(say_, font);
        const float w = std::min(m.width + 32.f, size.w - 32.f), h = renderer.lineHeight(font) + 20.f;
        const gfx::Rect bubble{(size.w - w) * 0.5f, size.h - h - 48.f, w, h};
        renderer.fillRoundedRect(bubble, gfx::Color::rgb(0x1F2430), 8.f);
        renderer.strokeRect(bubble, gfx::Color::rgb(0xF39C12), 2.f);
        renderer.drawText({bubble.x + 16.f, bubble.y + 10.f}, say_, font, gfx::Color::rgb(0xFFFFFF));
    }
    if (wait_ > 0) { --wait_; return true; }
    while (pc_ < lines_.size()) {
        const auto w = words(lines_[pc_]);
        if (w.empty() || w.front().front() == '#') { ++pc_; continue; }
        const Step step = run(w, renderer);
        if (step == Step::Retry) {
            if (++retries_ > 3000) { fail("toujours pas pret apres 3000 images : " + lines_[pc_]); retries_ = 0; ++pc_; }
            return true;
        }
        retries_ = 0;
        ++pc_;
        if (step == Step::Quit) return false;
        if (step == Step::Yield) return true;
    }
    return true;
}

// ------------------------------------------------------------------ entrees -
void ScriptRunner::send(const ui::InputEvent& e) {
    sinceInput_ = 0;
    driver_.send(e);   // 1.11 (T1) : la bulle des nouveautes, puis MenuManager::HandleEvent
}

void ScriptRunner::moveTo(gfx::Point p, KeyMods m) {
    sinceInput_ = 0;
    driver_.moveTo(p, m);
}

void ScriptRunner::click(gfx::Point p, MouseButton b, int clicks, KeyMods m) {
    sinceInput_ = 0;
    driver_.click(p, b, clicks, m);
}

// 1.11.1 (T1, R111-18) : comme une souris. L'evenement suit la voie du client
// (App::deliverClientEvent : la bulle des nouveautes, le calque du tutoriel, puis
// les ecrans), et non UiDriver::send, que la scene du tutoriel garde pour ses
// propres gestes : si UiDriver::send passait par le calque, il mangerait ces gestes.
void ScriptRunner::clientClick(gfx::Point p, MouseButton b, int clicks, KeyMods m) {
    sinceInput_ = 0;
    app_.deliverClientEvent(ui::MouseMove{p, {p.x - clientMouse_.x, p.y - clientMouse_.y}, m});
    clientMouse_ = p;
    app_.deliverClientEvent(ui::MouseDown{p, b, clicks, m});
    app_.deliverClientEvent(ui::MouseUp{p, b, m});
}

void ScriptRunner::drag(gfx::Point a, gfx::Point b, KeyMods m, bool release) {
    sinceInput_ = 0;
    driver_.drag(a, b, m, release);
}

void ScriptRunner::typeInto(gfx::Point cell, const std::string& text) {
    sinceInput_ = 0;
    driver_.typeInto(cell, text);
}

// ------------------------------------------------------------------ cibles --
ui::Widget* ScriptRunner::top() const { return driver_.top(); }
ui::Widget* ScriptRunner::currentPage() const { return driver_.currentPage(); }
HmiEditor* ScriptRunner::currentEditor() const { return driver_.currentEditor(); }

// ---------------------------------------------------------------- commandes -
ScriptRunner::Step ScriptRunner::run(const std::vector<std::string>& w, gfx::IRenderer& renderer) {
    const std::string& cmd = w.front();
    auto arg = [&](std::size_t i) -> std::string { return i < w.size() ? w[i] : std::string{}; };
    auto num = [&](std::size_t i) { return static_cast<float>(std::atof(arg(i).c_str())); };
    auto has = [&](std::string_view word) { return std::find(w.begin() + 1, w.end(), word) != w.end(); };
    auto mods = [&] {
        KeyMods m;
        m.ctrl = has("ctrl");
        m.shift = has("maj") || has("shift");
        m.alt = has("alt");
        return m;
    };
    std::printf("[script] %zu: %s\n", pc_ + 1, lines_[pc_].c_str());
    std::fflush(stdout);

    // ---- Lot API 8 : Centre de simulation ----
    //  Les titres d'onglets d'avant (onglet, fermer-onglet, onglet-diviser,
    //  onglet-deplacer, onglet-detacher, capture-fenetre, fenetre-...) : "API .
    //  Simulation" est devenu "Simulation . Automate", "IHM . Simulation"
    //  "Simulation . IHM" (la ligne est rejouee avec le nouveau titre).
    {
        static const std::pair<std::string_view, std::string_view> kMovedTabs[] = {
            {"API \xC2\xB7 Simulation", "Simulation \xC2\xB7 Automate"}, {"IHM \xC2\xB7 Simulation", "Simulation \xC2\xB7 IHM"}};
        std::vector<std::string> again;
        for (std::size_t k = 1; k < w.size(); ++k)
            for (const auto& [old, now] : kMovedTabs)
                if (w[k].rfind(old, 0) == 0) {
                    if (again.empty()) again = w;
                    again[k] = std::string(now) + w[k].substr(old.size());
                    break;
                }
        if (!again.empty()) return run(again, renderer);
    }
    // ---- fin Lot API 8 : Centre de simulation ----
    // ---- Lot API 8 : le bandeau bas ----
    //  messages-journal [ouvrir|fermer]   le journal des messages de la barre d'etat (sans mot : l'ouvre) ; ses lignes au journal du script
    //  barre-bas                          ce que montre le bandeau bas : le message, puis chaque pastille (au journal du script)
    //  barre-compteurs                    les pastilles au journal du script, puis le clic sur les compteurs (IHM > Compiler, ou A regarder)
    //  barre-zoom 125                     le zoom de l'onglet ouvert (75, 100, 125, 150 ; sans nombre : ouvre le menu du zoom)
    //  barre-theme "Nord"                 le theme, par sa cle ou son libelle (sans nom : ouvre le menu des themes recents)
    //  barre-clic journal|selection|erreurs|avertissements|simulation|cible|zoom|theme|enregistrement   le clic d'une pastille
    if (cmd == "messages-journal" || cmd == "barre-bas" || cmd == "barre-compteurs" || cmd == "barre-zoom" || cmd == "barre-theme"
        || cmd == "barre-clic") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) { fail(cmd + " : pas d'\xC3\xA9" "cran d'analyse"); return Step::Next; }
        if (cmd == "messages-journal") {
            const bool close = lower(arg(1)) == "fermer";
            screen->openMessageJournal(!close);
            if (!close)
                for (const auto& l : screen->messageJournalLines()) std::printf("[script] journal : %s\n", l.c_str());
            return Step::Yield;
        }
        if (cmd == "barre-bas" || cmd == "barre-compteurs") {
            std::printf("[script] barre-bas : %s\n", screen->statusStripText().c_str());
            if (cmd == "barre-compteurs") { screen->openStatusCounts(); return Step::Yield; }
            return Step::Next;
        }
        if (cmd == "barre-zoom") {
            if (arg(1).empty()) { (void)screen->clickStatusChip("zoom"); return Step::Yield; }
            if (!screen->setTabZoomPercent(std::atoi(arg(1).c_str())))
                fail("barre-zoom " + arg(1) + " : l'onglet ouvert n'a pas de zoom (ou pourcentage hors de 25 a 400)");
            return Step::Yield;
        }
        if (cmd == "barre-theme") {
            if (arg(1).empty()) { (void)screen->clickStatusChip("theme"); return Step::Yield; }
            if (!screen->setStatusTheme(arg(1))) fail("barre-theme : th\xC3\xA8me inconnu : " + arg(1));
            return Step::Yield;
        }
        if (!screen->clickStatusChip(lower(arg(1)))) fail("barre-clic : pastille inconnue ou cach\xC3\xA9" "e : " + arg(1));
        return Step::Yield;
    }
    // ---- fin Lot API 8 : le bandeau bas ----

    // ---- 1.8.0 : l'export lisible, les icones, le comparateur, l'import suivi ----
    //  lisible-exporter "dossier" [xlsx,pdf,txt] [tout | unite:<nom> | sections:<nom>,<nom>]
    //                                         ecrit sans la boite ; les fichiers au journal du script
    //  lisible-boite [tout | unite:<nom> | sections:<nom>,<nom>]   la boite Exporter le programme lisible
    //  icone <cle> <icone|aucune>             "section:MAST/Init" grafcet ; plusieurs cles : a;b
    //  icone-menu <cle>                       le petit menu des icones (pour les captures)
    //  comparer <section> <section> [...]     l'onglet du comparateur ; le rapport au journal
    //  import-suivi "fichier.XPG|.XHW"        l'import en tache de fond, avec sa fenetre
    //  attendre-import                        jusqu'a la fin de l'import suivi
    if (cmd == "lisible-exporter" || cmd == "lisible-boite" || cmd == "icone" || cmd == "icone-menu" || cmd == "comparer"
        || cmd == "import-suivi" || cmd == "attendre-import") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) {
            // La fenetre de l'import au-dessus : il tourne ; autre chose (le recapitulatif) : fini.
            if (cmd == "attendre-import") return dynamic_cast<ImportJobDialog*>(app_.menus().top()) ? Step::Retry : Step::Yield;
            fail(cmd + " : pas d'\xC3\xA9" "cran d'analyse");
            return Step::Next;
        }
        const auto p = app_.project();
        // Des noms de sections (sans la casse) : leurs indices.
        const auto sectionsOf = [&](const std::vector<std::string>& names, std::string& bad) {
            std::vector<domain::Index> out;
            for (const auto& n : names) {
                bool found = false;
                for (domain::Index s = 0; p && s < p->sections.size(); ++s)
                    if (lower(std::string(p->strings.text(p->sections[s].name))) == lower(n)) {
                        out.push_back(s);
                        found = true;
                        break;
                    }
                if (!found) bad = n;
            }
            return out;
        };
        const auto splitList = [](const std::string& text, char sep) {
            std::vector<std::string> out;
            std::string cur;
            for (const char c : text + std::string(1, sep)) {
                if (c == sep) {
                    if (!cur.empty()) out.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            return out;
        };
        if (cmd == "attendre-import") return screen->importRunning() ? Step::Retry : Step::Yield;
        if (cmd == "import-suivi") {
            const std::string path = arg(1);
            const bool xhw = lower(path).size() >= 4 && lower(path).substr(lower(path).size() - 4) == ".xhw";
            screen->startImportJob(!xhw, {path});
            return Step::Yield;
        }
        if (cmd == "icone" || cmd == "icone-menu") {
            const auto keys = splitList(arg(1), ';');
            if (keys.empty()) { fail(cmd + " : quelle cl\xC3\xA9 ?"); return Step::Next; }
            if (cmd == "icone-menu") {
                std::string list;
                for (const auto& k : keys) list += k + "\n";
                (void)screen->lisibleRequest("icone:" + list);
                return Step::Yield;
            }
            std::string why;
            const std::string icon = lower(arg(2)) == "aucune" ? std::string{} : arg(2);
            if (!screen->setCodeIcon(keys, icon, &why)) fail("icone : " + why);
            return Step::Yield;
        }
        // La portee : tout, unite:<nom>, sections:<a>,<b>.
        int scope = 0;
        std::string unit;
        std::vector<domain::Index> secs;
        const std::string portee = cmd == "lisible-exporter" ? arg(3) : arg(1);
        if (lower(portee).rfind("unite:", 0) == 0) {
            scope = 1;
            unit = portee.substr(6);
        } else if (lower(portee).rfind("sections:", 0) == 0) {
            scope = 2;
            std::string bad;
            secs = sectionsOf(splitList(portee.substr(9), ','), bad);
            if (!bad.empty()) { fail(cmd + " : section inconnue : " + bad); return Step::Next; }
        }
        if (cmd == "lisible-boite") {
            std::string key = "exporter:tout";
            if (scope == 1) key = "exporter:unite:" + unit;
            if (scope == 2) {
                key = "exporter:sections:";
                for (std::size_t i = 0; i < secs.size(); ++i) key += (i ? "," : "") + std::to_string(secs[i]);
            }
            (void)screen->lisibleRequest(key);
            return Step::Yield;
        }
        if (cmd == "lisible-exporter") {
            std::vector<std::string> written;
            std::string why;
            if (!screen->exportProgramNow(scope, unit, secs, arg(1), arg(2).empty() ? std::string("xlsx,pdf,txt") : arg(2), &written, &why))
                fail("lisible-exporter : " + why);
            for (const auto& file : written) std::printf("[script] lisible : %s\n", file.c_str());
            return Step::Yield;
        }
        // comparer
        std::vector<std::string> names;
        for (std::size_t i = 1; !arg(i).empty(); ++i) names.push_back(arg(i));
        std::string bad;
        secs = sectionsOf(names, bad);
        if (!bad.empty()) { fail("comparer : section inconnue : " + bad); return Step::Next; }
        std::string why;
        if (!screen->openSectionCompare(secs, &why)) { fail("comparer : " + why); return Step::Next; }
        if (auto* pane = screen->sectionComparePane()) {
            const auto& r = pane->result();
            std::printf("[script] comparer : %s : %d %% semblables, %zu identiques, %zu modifi\xC3\xA9" "es, %zu ajout\xC3\xA9" "es, %zu retir\xC3\xA9" "es\n",
                        pane->title().c_str(), r.similarity, static_cast<std::size_t>(r.same), static_cast<std::size_t>(r.changed),
                        static_cast<std::size_t>(r.added), static_cast<std::size_t>(r.removed));
        }
        return Step::Yield;
    }
    // ---- fin 1.8.0 ----

    if (cmd == "attendre") {
        wait_ = std::max(0, std::atoi(arg(1).c_str()) - 1);
        return Step::Yield;
    }
    if (cmd == "attendre-projet") return app_.project() ? Step::Yield : Step::Retry;
    // Lot 14 : la liaison Modbus TCP vit en temps reel. "attendre-reel 3" :
    // trois secondes de l'horloge du poste ; "attendre-liaison connectee" (ou
    // "coupee") : jusqu'a cet etat, 20 s au plus. Les images continuent.
    // Lot 15 : attendre la fin des pings des equipements, d'un changement
    // d'adresse, d'un scan, d'une requete de l'outil Modbus (30 s au plus ; les
    // images continuent).
    if (cmd == "attendre-equipements" || cmd == "attendre-adresse" || cmd == "attendre-scan" || cmd == "attendre-outil") {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (realUntil_ < 0) {
            realUntil_ = now + (cmd == "attendre-scan" ? 90.0 : 30.0);
            waitStarted_ = now;
        }
        bool done = false;
        if (now - waitStarted_ >= 0.3) {
            auto* pane = dynamic_cast<HmiCommPane*>(currentPage());
            if (cmd == "attendre-equipements") done = !app_.equipments().testing();
            else if (cmd == "attendre-adresse") done = !app_.equipments().applyState().busy;
            else if (cmd == "attendre-scan") done = !pane || !pane->scanning();
            else if (auto* tool = dynamic_cast<HmiModbusToolPane*>(currentPage())) done = !tool->busy();
            else done = true;
        }
        if (!done && now >= realUntil_) {
            fail(cmd + " : pas fini apr\xC3\xA8s le d\xC3\xA9lai");
            done = true;
        }
        if (done) {
            realUntil_ = -1;
            return Step::Yield;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        retries_ = 0;
        return Step::Retry;
    }
    if (cmd == "attendre-reel" || cmd == "attendre-liaison") {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (realUntil_ < 0) realUntil_ = now + (cmd == "attendre-reel" ? std::atof(arg(1).c_str()) : 20.0);
        bool done = now >= realUntil_;
        if (cmd == "attendre-liaison" && !done) {
            const auto* link = app_.comm().link();
            const bool connected = link && link->connected();
            done = arg(1) == "connectee" ? connected : !connected;
        } else if (cmd == "attendre-liaison" && done) {
            fail("la liaison n'est pas " + arg(1) + " apr\xC3\xA8s 20 s");
        }
        if (done) {
            realUntil_ = -1;
            return Step::Yield;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        retries_ = 0;
        return Step::Retry;
    }
    // Lot API 7 : attendre-cycles N - jusqu'a ce que la simulation ait fait N
    // cycles de plus. Les images continuent, a l'horloge fixe de la session (pas
    // d'attente du poste) ; la simulation doit tourner (Simuler). 120 s de
    // l'horloge du poste au plus.
    if (cmd == "attendre-cycles") {
        using SimState = SimulationHost::State;
        const auto& sim = app_.simulation();
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (realUntil_ < 0) {
            realUntil_ = now + 120.0;
            g_cyclesTarget = sim.scanCount() + static_cast<std::uint64_t>(std::max(1, std::atoi(arg(1).c_str())));
        }
        bool done = sim.scanCount() >= g_cyclesTarget;
        if (!done && (!sim.attached() || sim.state() != SimState::Running)) {
            std::string why = "arr\xC3\xAAt\xC3\xA9" "e";
            if (!sim.attached()) why = "pas pr\xC3\xAAte";
            else if (sim.state() == SimState::Paused) why = "en pause";
            else if (sim.state() == SimState::Halted) why = "halte : " + sim.haltMessage();
            fail("attendre-cycles : la simulation ne tourne pas (" + why + ", cycle " + std::to_string(sim.scanCount()) + ")");
            done = true;
        } else if (!done && now >= realUntil_) {
            fail("attendre-cycles : cycle " + std::to_string(sim.scanCount()) + " apr\xC3\xA8s 120 s (attendu : "
                 + std::to_string(g_cyclesTarget) + ")");
            done = true;
        }
        if (done) {
            realUntil_ = -1;
            return Step::Yield;
        }
        retries_ = 0;
        return Step::Retry;
    }
    if (cmd == "quitter") return Step::Quit;
    if (cmd == "etat") {                      // pour mettre un script au point
        std::string path;
        for (const auto& id : app_.menus().path()) path += (path.empty() ? "" : " > ") + id;
        std::string tab;
        if (auto* root = top())
            if (auto* centre = dynamic_cast<ui::TabArea*>(root->findById("analysis.centre")); centre && centre->tabCount())
                tab = centre->tab(centre->currentIndex())->title;
        std::printf("[script] ecrans : %s | projet : %s | dossier : %s | onglet : %s\n", path.c_str(),
                    app_.project() ? "oui" : "non", app_.projectFolder().c_str(), tab.c_str());
        return Step::Next;
    }
    if (cmd == "theme") { app_.setTheme(arg(1)); return Step::Yield; }
    // ---- Lot API 8 : themes ----
    //   theme "Nom"                      un theme integre (43) ou un des tiens, par sa cle ou son libelle
    //   theme-galerie ["Nom"]            ouvre Affichage > Theme... (la galerie) ; ouverte : choisit la carte "Nom" (lot API 6)
    //   theme-famille "Clairs"           la galerie ne montre qu'une famille ("Tous", "A toi", "Sombres"...)
    //   theme-chercher "nord"            le champ de recherche de la galerie
    //   theme-nouveau "Nom" ["base"]     un theme a toi, copie de "base" (defaut : le theme applique), applique
    //   theme-couleur "cle" "#RRGGBB"    change une couleur du theme applique (un des tiens), l'enregistre
    //   theme-corriger ["Nom"]           Corriger les contrastes (un des tiens ; defaut : le theme applique)
    //   theme-renommer "Ancien" "Nouveau"   un des tiens (son fichier suit)
    //   theme-supprimer "Nom"            un des tiens (son fichier est retire)
    //   theme-exporter "Nom" "chemin"    ecrit le .xpgtheme d'un theme (integre ou non)
    //   theme-importer "chemin"          lit un .xpgtheme, le range dans tes themes et l'applique
    //   theme-editer "Nom"               Modifier -> l'editeur s'ouvre (un integre est d'abord duplique ; la galerie s'ouvre au besoin)
    //   L'EDITEUR OUVERT (Modifier, Nouveau) :
    //   theme-couleur "cle" "#RRGGBB"    change une couleur, apercu en direct (rien n'est ecrit avant theme-enregistrer)
    //   theme-selection "cle"            la couleur choisie (celle des reglettes)
    //   theme-hsl H S L                  la couleur choisie en teinte (degres), saturation et luminosite (%)
    //   theme-deriver ["#RRGGBB"] ["sombre"|"clair"]   Deriver de l'accent (defaut : l'accent du theme)
    //   theme-corriger                   Corriger les contrastes (la luminosite des couleurs fautives)
    //   theme-annuler-dernier            Ctrl+Z
    //   theme-nom "Nom" | theme-auteur "Qui" | theme-famille "Industriels"
    //   theme-enregistrer                Enregistrer (dans tes themes, applique) ; theme-annuler : le theme d'avant revient
    //   theme-verifier                   echec si un contraste du theme en cours d'edition ne tient pas
    //   theme-exporter "chemin"          Exporter... de l'editeur : le theme tel qu'il est (nom, famille, auteur, couleurs)
    //   theme-verifier ["Nom"] [N]       echec si un contraste ne tient pas ; N : au moins N themes en tout
    //   Les messages de la galerie (ou de la commande) s'ecrivent en "[script] theme : ...".
    // 1.10.2 (chantier D) : LA FENETRE "DUPLIQUER..." (touche ctrl+d l'ouvre sur la selection).
    //   dupliquer-copies N                       le nombre de copies
    //   dupliquer-case L "Colonne" "valeur"      une case (ligne 1 : l'original ; colonne Nom, $Vanne$...)
    //   dupliquer-serie "Colonne" "V1{n}" "01"   une serie, depuis l'original
    //   dupliquer-coller "Colonne" "a|b|c"       une colonne d'Excel (| : a la ligne) ; le nombre de copies suit
    //   dupliquer-garder "Colonne" oui|non        une case vide garde le repere
    //   dupliquer-suivre N oui|non               l'indice N (1 : le premier) suivi ou garde
    //   dupliquer-original oui|non               "Remplacer aussi dans l'original"
    //   dupliquer-pose x|y|grille [colonnes] [espacement]
    //   dupliquer-ajuster                        "Ajuster l'espacement"
    //   dupliquer-onglet 1|2                     les copies / la disposition
    //   dupliquer-ligne L                        l'avant / apres de la ligne L
    //   dupliquer-verifier [bloque]              echec si "Dupliquer" est gris (bloque : s'il ne l'est pas)
    //   dupliquer-valider                        "Dupliquer" (une commande)
    //   dupliquer-tableau COL TABLEAU            1.10.4 : Remplir > Tableau... (V[0], V[1]...)
    //   dupliquer-vider COL                      1.10.4 : Vider (la colonne preremplie aussi)
    if (cmd.rfind("dupliquer-", 0) == 0) {
        auto* d = dynamic_cast<HmiDuplicateDialog*>(app_.menus().top());
        if (!d) { fail(cmd + " : la fen\xC3\xAAtre \xC2\xAB Dupliquer\xE2\x80\xA6 \xC2\xBB n'est pas ouverte (touche ctrl+d)"); return Step::Yield; }
        const auto yes = [](const std::string& word) { const auto l = lower(word); return l.empty() || l == "oui" || l == "1" || l == "vrai"; };
        if (cmd == "dupliquer-copies") d->setCopies(std::atoi(arg(1).c_str()));
        else if (cmd == "dupliquer-case") { if (!d->setCell(std::atoi(arg(1).c_str()) - 1, arg(2), arg(3))) fail(cmd + " : pas de case " + arg(1) + " / " + arg(2)); }
        else if (cmd == "dupliquer-serie") { if (!d->fillSeries(arg(1), arg(2), arg(3), true)) fail(cmd + " : colonne inconnue " + arg(1)); }
        else if (cmd == "dupliquer-coller") {
            std::string list = arg(2);
            std::replace(list.begin(), list.end(), '|', '\n');
            if (!d->fillList(arg(1), list)) fail(cmd + " : colonne inconnue " + arg(1));
        }
        else if (cmd == "dupliquer-tableau") { if (!d->fillArray(arg(1), arg(2))) fail(cmd + " : " + arg(1) + " / " + arg(2)); }   // 1.10.4
        else if (cmd == "dupliquer-vider") { if (!d->clearColumn(arg(1))) fail(cmd + " : colonne inconnue " + arg(1)); }         // 1.10.4
        else if (cmd == "dupliquer-garder") d->setKeep(arg(1), yes(arg(2)));
        else if (cmd == "dupliquer-suivre") d->setFollow(static_cast<std::size_t>(std::max(1, std::atoi(arg(1).c_str())) - 1), yes(arg(2)));
        else if (cmd == "dupliquer-original") d->setReplaceOriginal(yes(arg(1)));
        else if (cmd == "dupliquer-pose") {
            auto l = d->layout();
            const auto a = lower(arg(1));
            l.axis = a == "y" ? hmi::dup::Axis::Y : a == "grille" ? hmi::dup::Axis::Grid : hmi::dup::Axis::X;
            if (!arg(2).empty()) l.columns = std::atoi(arg(2).c_str());
            if (!arg(3).empty()) l.spacing = std::atof(arg(3).c_str());
            d->setLayout(l);
        }
        else if (cmd == "dupliquer-ajuster") { if (!d->fitSpacing()) fail(cmd + " : aucun espacement ne fait tout tenir"); }
        else if (cmd == "dupliquer-onglet") d->showTab(arg(1) == "2" ? 1 : 0);
        else if (cmd == "dupliquer-ligne") d->chooseRow(std::atoi(arg(1).c_str()) - 1);
        else if (cmd == "dupliquer-verifier") {
            const bool wantBlocked = lower(arg(1)) == "bloque";
            if (d->canConfirm() == wantBlocked) fail(cmd + " : " + (wantBlocked ? std::string("Dupliquer est permis") : "Dupliquer est gris : " + d->blockingText()));
            std::printf("[script] dupliquer : %s ; %s\n", d->confirmLabel().c_str(), d->blockingText().c_str());
        }
        else if (cmd == "dupliquer-valider") { if (!d->confirm()) fail(cmd + " : Dupliquer est gris : " + d->blockingText()); }
        else fail("commande inconnue : " + cmd);
        return Step::Yield;
    }
    if (cmd.rfind("theme-", 0) == 0) {
        auto* gallery = dynamic_cast<ThemeGalleryDialog*>(app_.menus().top());
        const auto say = [&](const std::string& text) { std::printf("[script] theme : %s\n", text.c_str()); };
        const auto current = [&] { return app_.theme().name; };
        // L'editeur (Modifier, Nouveau) : tant qu'il est ouvert, les gestes vont a lui.
        if (auto* editor = dynamic_cast<ThemeEditorDialog*>(app_.menus().top())) {
            if (cmd == "theme-couleur") {
                if (!editor->setColor(arg(1), arg(2))) fail("theme-couleur : " + editor->message());
                return Step::Yield;
            }
            if (cmd == "theme-selection") {
                if (!editor->select(arg(1))) fail("theme-selection : couleur inconnue " + arg(1));
                return Step::Yield;
            }
            if (cmd == "theme-hsl") {
                if (!editor->setHsl(std::atof(arg(1).c_str()), std::atof(arg(2).c_str()), std::atof(arg(3).c_str())))
                    fail("theme-hsl : aucune couleur choisie");
                return Step::Yield;
            }
            if (cmd == "theme-deriver") {
                const int dark = arg(2).empty() ? -1 : (ui::Theme::fold(arg(2)) == "sombre" ? 1 : 0);
                if (!editor->derive(arg(1), dark)) fail("theme-deriver : " + editor->message());
                else say(editor->message());
                return Step::Yield;
            }
            if (cmd == "theme-corriger") {
                say(std::to_string(editor->fixContrasts()) + " couleur(s) corrigee(s)");
                return Step::Yield;
            }
            if (cmd == "theme-annuler-dernier") {
                if (!editor->undo()) fail("theme-annuler-dernier : rien a annuler");
                return Step::Yield;
            }
            if (cmd == "theme-nom") { editor->setName(arg(1)); return Step::Yield; }
            if (cmd == "theme-auteur") { editor->setAuthor(arg(1)); return Step::Yield; }
            if (cmd == "theme-famille") {
                if (!editor->setFamily(arg(1))) fail("theme-famille : famille inconnue " + arg(1));
                return Step::Yield;
            }
            if (cmd == "theme-enregistrer") {
                if (!editor->save()) fail("theme-enregistrer : " + editor->message());
                return Step::Yield;
            }
            if (cmd == "theme-annuler") { editor->cancel(); return Step::Yield; }
            if (cmd == "theme-exporter") {          // "chemin", ou "Nom" "chemin" : le theme tel qu'il est dans l'editeur
                if (!editor->exportTo(arg(2).empty() ? arg(1) : arg(2))) fail("theme-exporter : " + editor->message());
                return Step::Next;
            }
            if (cmd == "theme-verifier") {
                const auto fails = ui::contrastFailures(editor->theme());
                for (const auto& f : fails) say(editor->theme().name + " : " + f.label + " " + ui::contrastText(f.ratio) + " (il faut " + ui::contrastText(f.need) + ")");
                if (!fails.empty()) fail("theme-verifier : " + std::to_string(fails.size()) + " contraste(s) insuffisant(s) dans l'editeur");
                return Step::Next;
            }
            fail(cmd + " : l'editeur de theme est ouvert (theme-enregistrer ou theme-annuler d'abord)");
            return Step::Next;
        }
        if (cmd == "theme-enregistrer" || cmd == "theme-annuler" || cmd == "theme-deriver" || cmd == "theme-selection" || cmd == "theme-hsl"
            || cmd == "theme-nom" || cmd == "theme-auteur" || cmd == "theme-annuler-dernier") {
            fail(cmd + " : l'editeur de theme n'est pas ouvert (theme-editer \"Nom\" d'abord)");
            return Step::Next;
        }
        if (cmd == "theme-galerie") {
            // Lot API 6 : theme-galerie "Nom" choisit une carte de la galerie ouverte.
            if (!gallery) { app_.showThemeGallery(); return Step::Yield; }
            if (!arg(1).empty() && !gallery->choose(arg(1))) fail("theme-galerie : th\xC3\xA8me inconnu " + arg(1));
            return Step::Yield;
        }
        if (cmd == "theme-editer" && !gallery) {
            // Sans galerie : elle s'ouvre, et la commande reprend a l'image ou elle est la.
            bool open = false;
            for (const auto& id : app_.menus().path()) open = open || id == "dialog.theme";
            if (!open) app_.showThemeGallery();
            return Step::Retry;
        }
        if (cmd == "theme-famille" || cmd == "theme-chercher" || cmd == "theme-editer") {
            if (!gallery) { fail(cmd + " : la galerie n'est pas ouverte (theme-galerie d'abord)"); return Step::Next; }
            if (cmd == "theme-famille" && !gallery->setFamily(arg(1))) fail("theme-famille : famille inconnue " + arg(1));
            if (cmd == "theme-chercher") gallery->setSearch(arg(1));
            if (cmd == "theme-editer" && !gallery->edit(arg(1))) fail("theme-editer : " + gallery->message());
            if (!gallery->message().empty()) say(gallery->message());
            return Step::Yield;
        }
        if (cmd == "theme-nouveau") {
            const std::string from = ui::Theme::keyOf(arg(2).empty() ? current() : arg(2));
            if (from.empty() || !ui::themeNameProblem(arg(1)).empty()) { fail("theme-nouveau : nom ou base invalide (" + arg(1) + ", " + arg(2) + ")"); return Step::Next; }
            if (gallery) {
                if (!gallery->createFrom(arg(1), from, false)) fail("theme-nouveau : " + gallery->message());
                return Step::Yield;
            }
            ui::Theme t = ui::Theme::byName(from);
            if (!t.user) t.base = from;
            t.user = true;
            t.name = ui::freeThemeName(arg(1));
            std::string error;
            if (!ui::UserThemes::save(t, &error)) { fail("theme-nouveau : " + error); return Step::Next; }
            app_.setTheme(t.name);
            say("\"" + t.name + "\" cree a partir de \"" + from + "\"");
            return Step::Yield;
        }
        if (cmd == "theme-couleur") {
            const auto& mine = ui::Theme::userThemes();
            const auto it = std::find_if(mine.begin(), mine.end(), [&](const ui::Theme& u) { return u.name == current(); });
            if (it == mine.end()) { fail("theme-couleur : \"" + current() + "\" est integre (theme-nouveau d'abord)"); return Step::Next; }
            ui::Theme t = *it;
            gfx::Color* slot = ui::themeColor(t, arg(1));
            const ui::ThemeColorKey* info = ui::themeColorKey(arg(1));
            if (!slot || !info || !ui::parseThemeColor(arg(2), *slot, info->alpha)) { fail("theme-couleur : cle ou couleur invalide (" + arg(1) + " = " + arg(2) + ")"); return Step::Next; }
            std::string error;
            if (!ui::UserThemes::save(t, &error)) { fail("theme-couleur : " + error); return Step::Next; }
            app_.setTheme(t.name);
            return Step::Yield;
        }
        if (cmd == "theme-corriger") {
            if (gallery) {
                if (!gallery->fixContrastsOf(arg(1))) fail("theme-corriger : " + gallery->message());
                else say(gallery->message());
                return Step::Yield;
            }
            const std::string key = ui::Theme::keyOf(arg(1).empty() ? current() : arg(1));
            const auto& mine = ui::Theme::userThemes();
            const auto it = std::find_if(mine.begin(), mine.end(), [&](const ui::Theme& u) { return u.name == key; });
            if (it == mine.end()) { fail("theme-corriger : \"" + key + "\" n'est pas un de tes themes"); return Step::Next; }
            ui::Theme t = *it;
            const auto changed = ui::fixContrasts(t);
            std::string error;
            if (!ui::UserThemes::save(t, &error)) { fail("theme-corriger : " + error); return Step::Next; }
            if (current() == key) app_.setTheme(key);
            say(std::to_string(changed.size()) + " couleur(s) corrigee(s) dans \"" + key + "\"");
            return Step::Yield;
        }
        if (cmd == "theme-renommer" || cmd == "theme-supprimer") {
            if (gallery) {
                const bool ok = cmd == "theme-renommer" ? gallery->renameTheme(arg(1), arg(2)) : gallery->deleteTheme(arg(1));
                if (!ok) fail(cmd + " : " + gallery->message());
                return Step::Yield;
            }
            const std::string key = ui::Theme::keyOf(arg(1));
            const bool shown = current() == key;
            std::string error;
            const bool ok = cmd == "theme-renommer" ? ui::UserThemes::rename(key, arg(2), &error) : ui::UserThemes::remove(key, &error);
            if (!ok) { fail(cmd + " : " + (error.empty() ? "\"" + arg(1) + "\" n'est pas un de tes themes" : error)); return Step::Next; }
            if (shown) app_.setTheme(cmd == "theme-renommer" ? arg(2) : std::string("Light"));
            return Step::Yield;
        }
        if (cmd == "theme-exporter") {
            if (gallery) {
                if (!gallery->exportTo(ui::Theme::keyOf(arg(1)), arg(2))) fail("theme-exporter : " + gallery->message());
                return Step::Yield;
            }
            std::string error;
            if (!ui::UserThemes::exportTheme(ui::Theme::keyOf(arg(1)), arg(2), &error)) fail("theme-exporter : " + error);
            return Step::Next;
        }
        if (cmd == "theme-importer") {
            if (gallery) {
                if (!gallery->importFrom(arg(1))) fail("theme-importer : " + gallery->message());
                else say(gallery->message());
                return Step::Yield;
            }
            const auto r = ui::UserThemes::importFile(arg(1));
            if (!r.ok) { fail("theme-importer : ligne " + std::to_string(r.line) + " : " + r.error); return Step::Next; }
            app_.setTheme(r.name);
            say("\"" + r.name + "\" importe" + (r.renamedFrom.empty() ? std::string{} : " (\"" + r.renamedFrom + "\" existait)"));
            return Step::Yield;
        }
        if (cmd == "theme-verifier") {
            const std::string key = ui::Theme::keyOf(arg(1).empty() ? current() : arg(1));
            if (key.empty()) { fail("theme-verifier : theme inconnu " + arg(1)); return Step::Next; }
            const auto fails = ui::contrastFailures(ui::Theme::byName(key));
            for (const auto& f : fails) say(key + " : " + f.label + " " + ui::contrastText(f.ratio) + " (il faut " + ui::contrastText(f.need) + ")");
            if (!fails.empty()) fail("theme-verifier : " + std::to_string(fails.size()) + " contraste(s) insuffisant(s) dans \"" + key + "\"");
            const std::size_t total = ui::Theme::all().size();
            say(std::to_string(total) + " themes (" + std::to_string(ui::Theme::userThemes().size()) + " a toi)");
            if (!arg(2).empty() && total < static_cast<std::size_t>(std::max(0, std::atoi(arg(2).c_str()))))
                fail("theme-verifier : " + std::to_string(total) + " themes, il en faut " + arg(2));
            return Step::Next;
        }
        fail("commande inconnue : " + cmd);
        return Step::Next;
    }
    // ---- fin Lot API 8 : themes ----
    if (cmd == "importer") { app_.openPath(arg(1)); return Step::Yield; }
    if (cmd == "ouvrir") { app_.openProjectFolder(arg(1)); return Step::Yield; }
    if (cmd == "action") {
        const std::string id = arg(1);
        if (auto r = app_.actions().trigger(id, app_.commands()); !r) fail("action " + id + " : " + r.error().message());
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "capture") {
        if (sinceInput_ < 3) return Step::Retry;           // que l'ecran ait fini de se mettre en place
        const auto path = (capturesDir_ / arg(1)).string();
        if (auto r = captureToPng(renderer, path); !r) fail("capture " + path + " : " + r.error().message());
        else std::printf("[script] capture %s\n", path.c_str());
        return Step::Next;
    }
    // 1.11.15 : ihm-carte "<libelle>" - un bouton de la carte de l'IHM arretee (Demarrer
    // l'IHM, Generer et redemarrer, Annuler le redemarrage...), dessine sur la vue.
    if (cmd == "ihm-carte") {
        auto* root = top();
        gfx::Rect at{};
        if (root)
            walk(*root, [&](ui::Widget& x) {
                auto* canvas = dynamic_cast<HmiLiveCanvas*>(&x);
                if (!canvas || !shown(*canvas) || at.w > 0.f || canvas->stoppedNote().empty()) return;
                const auto& buttons = canvas->noteButtons();
                if (buttons.empty()) {      // la carte ordinaire : Demarrer l'IHM, Demarrer les deux
                    if (arg(1) == "D\xC3\xA9marrer l'IHM") at = canvas->stoppedButtonRect(false);
                    else if (arg(1) == "D\xC3\xA9marrer les deux") at = canvas->stoppedButtonRect(true);
                    return;
                }
                for (std::size_t i = 0; i < buttons.size(); ++i)
                    if (buttons[i].label == arg(1)) at = canvas->noteButtonRect(i);
            });
        if (at.w <= 0.f) {
            if (retries_ < 30) return Step::Retry;     // la carte se dessine a l'image suivante
            fail("ihm-carte : pas de bouton \"" + arg(1) + "\" sur la carte");
            return Step::Next;
        }
        click({at.x + at.w / 2.f, at.y + at.h / 2.f}, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "clic") {
        const gfx::Point p{num(1), num(2)};
        const auto b = has("droit") ? MouseButton::Right : MouseButton::Left;
        click(p, b, 1, mods());
        if (has("double")) click(p, b, 2, mods());
        return Step::Yield;
    }
    // 1.11.1 (T1, R111-18) : clic-souris X Y [droit] [double] | clic-souris lecteur:<partie>
    //  Un clic comme une vraie souris : par App::deliverClientEvent, la voie d'App::pumpEvents
    //  (la bulle des nouveautes, puis le calque du tutoriel, puis les ecrans). "clic X Y" va
    //  droit aux ecrans (UiDriver::send) : la barre du lecteur d'un tutoriel ne le voit pas.
    //  lecteur:<partie> vise une partie de cette barre (tutorialBarPart : lecture, precedente,
    //  suivante, recommencer, atoi, quitter, pause-etape, vitesse-1 a 3, etape-<n>...).
    if (cmd == "clic-souris") {
        gfx::Point p{num(1), num(2)};
        if (arg(1).rfind("lecteur:", 0) == 0) {
            auto* ov = tutorials::overlay();
            gfx::Rect r{};
            if (!ov) { fail("clic-souris " + arg(1) + " : aucun tutoriel a l'ecran"); return Step::Next; }
            if (!tutorialBarPart(ov->barLayout(), std::string_view(arg(1)).substr(8), r)) {
                // La barre n'est posee qu'a la premiere image du calque (setScreen) : on l'attend un peu.
                if (retries_ < 60) return Step::Retry;
                fail("clic-souris : partie du lecteur inconnue, ou pas a l'ecran : " + arg(1));
                return Step::Next;
            }
            p = centre(r);
        }
        const auto b = has("droit") ? MouseButton::Right : MouseButton::Left;
        clientClick(p, b, 1, mods());
        if (has("double")) clientClick(p, b, 2, mods());
        std::printf("[script] clic-souris %s : %d, %d\n", arg(1).c_str(), static_cast<int>(p.x), static_cast<int>(p.y));
        return Step::Yield;
    }
    if (cmd == "glisser") { drag({num(1), num(2)}, {num(3), num(4)}, mods()); return Step::Yield; }
    if (cmd == "survol") { moveTo({num(1), num(2)}, {}); return Step::Yield; }
    if (cmd == "molette") {
        const gfx::Point p{num(1), num(2)};
        moveTo(p, mods());
        send(ui::MouseWheel{p, 0.f, num(3), mods()});
        return Step::Yield;
    }
    if (cmd == "touche") {
        Key k{};
        KeyMods m;
        if (!parseKey(arg(1), k, m)) { fail("touche inconnue : " + arg(1)); return Step::Next; }
        send(ui::KeyDown{k, m, false});
        send(ui::KeyUp{k, m});
        return Step::Yield;
    }
    // ---- 1.11 (T1) : les gestes des tutoriels (encadrer, dire), dessines sur les images ----
    if (cmd == "encadrer") {
        if (arg(1).empty()) { spot_.reset(); return Step::Yield; }
        std::string why;
        if (const auto r = driver_.locate(arg(1), &why)) {
            spot_ = *r;
            std::printf("[script] encadrer %s : %.0f %.0f %.0f x %.0f\n", arg(1).c_str(), r->x, r->y, r->w, r->h);
            return Step::Yield;
        }
        if (retries_ < 3) return Step::Retry;     // le temps d'une mise en page (une case revelee)
        fail("encadrer : " + why);
        return Step::Next;
    }
    // ---- 1.11 (T1, tranche 3) : un tutoriel dans une session ----
    //  tutoriel <cle> [variante] [etape] [atoi|pause]   le lance (help::startTutorial)
    //  tutoriel-aller <etape> [ms]     remet l'etat exact (seek : copie neuve, rejoue sans animation)
    //  tutoriel-atoi                   "A toi" sur l'etape courante
    //  tutoriel-pause / tutoriel-lecture / tutoriel-quitter
    //  tutoriel-attendre               attend que la scene ait fini sa file (bac a sable, gestes)
    //  tutoriel-verifier               (--verifier-tutoriels) attend la scene ; chaque cible introuvable
    //                                  depuis la derniere fois est un echec, et l'A toi de l'etape doit
    //                                  etre juste (ses gestes viennent d'etre joues : "Montre-moi")
    //  tutoriel-avant <etape>          (tranche 16, l'idee de T3) attend la scene, puis lit l'A toi de
    //                                  <etape> sur l'etat COURANT, qui doit etre celui d'avant ses gestes :
    //                                  juste apres "tutoriel ... 1 pause" + tutoriel-attendre (etape 1), ou
    //                                  a la fin de l'etape d'avant (tutoriel-aller <etape-1> 9999999). C'est
    //                                  l'etat ou "A toi" met l'utilisateur (startATry : seek(etape, 0) rejoue
    //                                  les memes gestes) : pas de remise a neuf de plus. S'il est deja juste,
    //                                  un AVERTISSEMENT (pas une faute) : le sujet, l'etape, les conditions
    //                                  et leur ligne ; il va dans la colonne "A toi deja vrai" du bilan.
    if (cmd == "tutoriel") {
        (void)HelpCenterScreen::index();   // integration I111 : un sujet sans tutoriel ecrit a son deduit
        help::TutorialStart o;
        o.variant = arg(2) == "-" ? std::string() : arg(2);
        if (!arg(3).empty()) o.step = static_cast<std::size_t>(std::max(1, std::atoi(arg(3).c_str())) - 1);
        o.aTry = has("atoi");
        o.paused = has("pause");
        const auto r = help::startTutorial(arg(1), o.variant, o);
        std::printf("[script] tutoriel %s : %s\n", arg(1).c_str(), help::startResultName(r));
        // Tranche 9 : une ligne du bilan par tutoriel et variante (fermee par tutoriel-quitter).
        {
            TutoRow row;
            row.subject = arg(1);
            row.variant = arg(2).empty() ? std::string("-") : arg(2);
            if (auto* pl = tutorials::player(); pl && r == help::StartResult::Started) {
                row.steps = pl->compiled().steps.size();
                if (!pl->compiled().variant.empty()) row.variant = pl->compiled().variant;   // la variante jouee
            }
            else if (const auto* t = help::findTutorial(arg(1)) ? help::findTutorial(arg(1)) : help::tutorialForTopic(arg(1)))
                row.steps = t->compile(o.variant).steps.size();
            g_tutoRows.push_back(std::move(row));
            g_tutoRowOpen = true;
            g_tutoStepMark = failures_;
        }
        if (r != help::StartResult::Started) fail("tutoriel " + arg(1) + " : " + help::startResultName(r));
        sinceInput_ = 0;
        return Step::Yield;
    }
    // Tranche 9 : LE VERIFICATEUR EN LOT (decision du chef : un tutoriel pour chaque sujet).
    //  tutoriel-lot inscrits | sujets | dossier "<chemin>" | <id>[:<variante>]  [captures]
    //   inscrits : les tutoriels ecrits (embarques, tools/tutoriels ou XPG_TUTORIELS) ;
    //   sujets   : chaque sujet inscrit par le centre (help::setTopics), son tutoriel ecrit
    //              sinon deduit (help::tutorialForTopic) : un sujet sans tutoriel est un echec ;
    //   dossier  : les .tuto du dossier, inscrits a l'execution (un id deja connu est remplace).
    //  Deplie ICI, a l'execution (les sujets et le deducteur sont branches par l'appli), la
    //  suite de chaque tutoriel et variante : tutoriel, tutoriel-attendre, puis pour chaque
    //  etape tutoriel-aller n / tutoriel-verifier [/ capture <id>-<variante>-<n>.png], et
    //  tutoriel-quitter. Un seul lancement de l'appli pour tout le lot.
    //  tutoriel-bilan [fichier]  le tableau : sujet, variante, etapes justes / total, code
    //  (les fautes de la ligne, 0 = juste), premiere faute ; sur la sortie, et dans le fichier
    //  (Markdown ; relatif : dans le dossier des captures).
    if (cmd == "tutoriel-lot") {
        (void)HelpCenterScreen::index();   // integration I111 : le centre inscrit ses sujets (help::setTopics)
        struct Item { std::string key; const help::Tutorial* t = nullptr; std::string fault; std::string variant; };
        std::vector<Item> items;
        const std::string mode = arg(1);
        if (mode == "sujets") {
            for (const auto& topic : help::topics()) {
                Item it;
                it.key = topic.key;
                it.t = help::tutorialForTopic(topic.key, &it.fault);
                items.push_back(std::move(it));
            }
            if (items.empty()) fail("tutoriel-lot sujets : aucun sujet inscrit (help::setTopics)");
        } else if (mode == "dossier") {
            std::error_code ec;
            std::vector<std::filesystem::path> files;
            for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(arg(2)), ec))
                if (e.path().extension() == ".tuto") files.push_back(e.path());
            std::sort(files.begin(), files.end());
            if (ec || files.empty()) fail("tutoriel-lot dossier : aucun .tuto dans " + arg(2));
            for (const auto& f : files) {
                std::ifstream in(f, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                std::vector<help::TutorialProblem> problems;
                const std::string id = help::registerTutorialText(ss.str(), &problems);
                Item it;
                it.key = id.empty() ? f.filename().string() : id;
                if (!id.empty()) it.t = help::findTutorial(id);
                else if (!problems.empty()) it.fault = "ligne " + std::to_string(problems.front().line) + " : " + problems.front().message;
                else it.fault = "pas d'id (= <id> | <titre>)";
                items.push_back(std::move(it));
            }
        } else {
            const auto colon = mode.find(':');
            const std::string onlyId = mode == "inscrits" ? std::string() : mode.substr(0, colon);
            const std::string onlyVariant = colon == std::string::npos ? std::string() : mode.substr(colon + 1);
            for (const auto* t : help::tutorials())
                if (onlyId.empty() || t->id == onlyId) {
                    Item it;
                    it.key = t->id;
                    it.t = t;
                    it.variant = onlyVariant;
                    items.push_back(std::move(it));
                }
            if (!onlyId.empty() && items.empty()) {
                Item it;
                it.key = onlyId;
                it.t = help::tutorialForTopic(onlyId, &it.fault);   // un sujet, ou un deduit
                it.variant = onlyVariant;
                items.push_back(std::move(it));
            }
        }
        std::vector<std::string> out;
        for (const auto& it : items) {
            if (!it.t) {
                g_tutoAbsent.emplace_back(it.key, it.fault.empty() ? std::string("pas de tutoriel") : it.fault);
                out.push_back("tutoriel-absent " + std::to_string(g_tutoAbsent.size() - 1));
                continue;
            }
            std::vector<std::string> variants = it.t->variants;
            if (variants.empty()) variants.push_back("-");
            for (const auto& v : variants) {
                if (!it.variant.empty() && v != it.variant) continue;
                const auto steps = it.t->compile(v == "-" ? std::string() : v).steps.size();
                out.push_back("tutoriel \"" + it.key + "\" \"" + v + "\" 1 pause");
                out.push_back("tutoriel-attendre");
                const auto compiled = it.t->compile(v == "-" ? std::string() : v);
                for (std::size_t n = 1; n <= steps; ++n) {
                    // Tranche 16 : l'A toi de l'etape, lu avant ses gestes (l'etat de la fin de l'etape d'avant).
                    if (n <= compiled.steps.size() && compiled.steps[n - 1].aTry)
                        out.push_back("tutoriel-avant " + std::to_string(n));
                    out.push_back("tutoriel-aller " + std::to_string(n) + " 9999999");
                    out.push_back("tutoriel-verifier");
                    if (has("captures")) {
                        out.push_back("attendre 2");
                        out.push_back("capture \"" + it.t->id + "-" + (v == "-" ? std::string("x") : v) + "-" + std::to_string(n) + ".png\"");
                    }
                }
                out.push_back("tutoriel-quitter");
                out.push_back("attendre 5");
            }
        }
        std::printf("[script] tutoriel-lot %s : %zu tutoriels, %zu lignes\n", mode.c_str(), items.size(), out.size());
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(pc_ + 1), out.begin(), out.end());
        return Step::Next;
    }
    // Tranche 9 : tutoriel-texte <sujet> [fichier] ecrit le texte deduit d'un sujet (T3), tel quel,
    // sur la sortie et dans le fichier : l'auteur part de la pour en faire un .tuto (section 5).
    if (cmd == "tutoriel-texte") {
        (void)HelpCenterScreen::index();   // integration I111 : le centre inscrit ses sujets (help::setTopics)
        const std::string text = help::deducedTutorialText(arg(1));
        if (text.empty()) fail("tutoriel-texte " + arg(1) + " : " + (help::findTopic(arg(1)) ? "rien de d\xC3\xA9" "duit" : "sujet inconnu"));
        std::printf("[tutoriel-texte %s]\n%s\n", arg(1).c_str(), text.c_str());
        if (!arg(2).empty() && !text.empty()) {
            std::filesystem::path file(arg(2));
            if (file.is_relative() && !capturesDir_.empty()) file = capturesDir_ / file;
            std::ofstream(file, std::ios::binary) << text;
        }
        return Step::Next;
    }
    // Tranche 18 : tutoriel-compte [fichier] : le compteur "Tutoriels prets" du centre, puis
    // CHAQUE sujet qui n'est pas pret, avec sa raison : celle du deducteur (whyNoTutorial, T3),
    // sinon la faute de lecture ou de compilation (la variante par defaut, comme le centre).
    // Sur la sortie, et en Markdown dans le fichier (relatif : dans le dossier des captures).
    // Le registre (countTutorials : toutes les variantes) est redit ; s'il compte autrement que
    // le centre, chaque sujet en plus est nomme. Le compte de la livraison se lit ainsi.
    if (cmd == "tutoriel-compte") {
        namespace hc = help::center;
        const auto& ix = HelpCenterScreen::index();   // le centre inscrit ses sujets (help::setTopics)
        const auto shown = hc::tutorialCount(ix);
        const auto reg = help::countTutorials();
        auto cell = [](std::string t) { std::replace(t.begin(), t.end(), '|', '/'); return t; };
        std::string md = "# " + hc::tutorialCountText(shown) + "\n\n";
        md += "Le registre (toutes les variantes) : " + std::to_string(reg.ready) + " / " + std::to_string(reg.total) + " ("
            + std::to_string(reg.written) + " \xC3\xA9" "crits, " + std::to_string(reg.deduced) + " d\xC3\xA9" "duits).\n\n";
        // Tranche 19 (decision 12 du 03/10) : les notes de version n'attendent pas de tutoriel.
        if (!reg.excluded.empty()) {
            std::string keys;
            for (const auto& k : reg.excluded) keys += (keys.empty() ? "" : ", ") + k;
            md += "Hors du compte du registre (pas de tutoriel, les notes de version) : " + std::to_string(reg.excluded.size())
                + " (" + keys + ").\n\n";
            std::printf("[tutoriel-compte] hors du compte du registre : %zu (%s)\n", reg.excluded.size(), keys.c_str());
        }
        md += "| sujet | genre | titre | raison |\n|---|---|---|---|\n";
        std::printf("[tutoriel-compte] %s ; le registre : %d / %d (%d \xC3\xA9" "crits, %d d\xC3\xA9" "duits)\n",
                    hc::tutorialCountText(shown).c_str(), reg.ready, reg.total, reg.written, reg.deduced);
        std::vector<std::string> shownKeys;
        for (const auto& t : ix.topics()) {
            const auto info = hc::tutorialInfo(t);
            if (info.steps > 0 && !info.estimated) continue;
            shownKeys.push_back(t.key);
            const help::TopicInfo* topic = help::findTopic(t.key);
            std::string why = topic ? help::whyNoTutorial(*topic) : std::string("pas inscrit dans le registre (help::setTopics)");
            if (why.empty()) {
                std::string fault;
                if (const help::Tutorial* tuto = help::tutorialForTopic(t.key, &fault)) {
                    const auto c = tuto->compile();
                    if (!c.problems.empty()) why = "ligne " + std::to_string(c.problems.front().line) + " : " + c.problems.front().message;
                    else if (c.steps.empty()) why = "aucune \xC3\xA9tape";
                    else why = "pr\xC3\xAAt pour le registre, estim\xC3\xA9 par le centre";
                } else why = fault.empty() ? std::string("pas de tutoriel") : fault;
            }
            const char* kind = topic ? help::topicKindName(topic->kind) : "-";
            std::printf("[tutoriel-compte] pas pr\xC3\xAAt : %s | %s | %s | %s\n", t.key.c_str(), kind, t.title.c_str(), why.c_str());
            md += "| " + cell(t.key) + " | " + kind + " | " + cell(t.title) + " | " + cell(why) + " |\n";
        }
        auto inShown = [&](const std::string& entry) {
            const std::string key = entry.substr(0, entry.find(" : "));
            return std::find(shownKeys.begin(), shownKeys.end(), key) != shownKeys.end();
        };
        std::string extra;
        for (const auto& b : reg.broken)
            if (!inShown(b)) extra += "- illisible pour le registre (une autre variante) : " + b + "\n";
        for (const auto& m : reg.missing)
            if (!inShown(m)) extra += "- sans tutoriel pour le registre : " + m + "\n";
        if (!extra.empty()) {
            md += "\nLe registre compte autrement que le centre :\n\n" + extra;
            std::printf("[tutoriel-compte] le registre compte autrement que le centre :\n%s", extra.c_str());
        }
        md += "\n" + std::to_string(shownKeys.size()) + " sujets pas pr\xC3\xAAts sur " + std::to_string(shown.total) + ".\n";
        std::printf("[tutoriel-compte] %zu sujets pas pr\xC3\xAAts sur %zu\n", shownKeys.size(), shown.total);
        if (!arg(1).empty()) {
            std::filesystem::path file(arg(1));
            if (file.is_relative() && !capturesDir_.empty()) file = capturesDir_ / file;
            std::ofstream(file, std::ios::binary) << md;
        }
        return Step::Next;
    }
    if (cmd == "tutoriel-absent") {
        const auto n = static_cast<std::size_t>(std::atoi(arg(1).c_str()));
        if (n < g_tutoAbsent.size()) {
            TutoRow row;
            row.subject = g_tutoAbsent[n].first;
            row.variant = "-";
            g_tutoRows.push_back(std::move(row));
            g_tutoRowOpen = true;
            fail("tutoriel " + g_tutoAbsent[n].first + " : " + g_tutoAbsent[n].second);
            g_tutoRowOpen = false;
        }
        return Step::Next;
    }
    if (cmd == "tutoriel-bilan") {
        g_tutoRowOpen = false;
        auto cell = [](std::string t) { std::replace(t.begin(), t.end(), '|', '/'); return t; };
        // Tranche 16 : la colonne "A toi deja vrai" (avertissements de tutoriel-avant ; vide : aucun).
        std::string md = "| sujet | variante | \xC3\xA9tapes justes / total | code | premi\xC3\xA8re faute | \xC3\x80 toi d\xC3\xA9j\xC3\xA0 vrai |\n|---|---|---|---|---|---|\n";
        std::size_t good = 0, alreadyRows = 0, alreadyCases = 0;
        for (const auto& r : g_tutoRows) {
            if (r.failures == 0 && r.steps > 0 && r.ok == r.steps) ++good;
            if (r.alreadyTrue > 0) { ++alreadyRows; alreadyCases += r.alreadyTrue; }
            md += "| " + cell(r.subject) + " | " + cell(r.variant) + " | " + std::to_string(r.ok) + " / " + std::to_string(r.steps)
                + " | " + std::to_string(r.failures) + " | " + cell(r.firstFault) + " | " + cell(r.alreadyTrueText) + " |\n";
        }
        md += "\n" + std::to_string(g_tutoRows.size()) + " tutoriels et variantes : " + std::to_string(good) + " justes, "
            + std::to_string(g_tutoRows.size() - good) + " en \xC3\xA9" "chec.\n";
        md += "\xC3\x80 toi d\xC3\xA9j\xC3\xA0 vrai avant ses gestes : " + std::to_string(alreadyCases) + " cas, dans " +
              std::to_string(alreadyRows) + " tutoriels et variantes (des avertissements, pas des fautes).\n";
        std::printf("[bilan]\n%s", md.c_str());
        std::fflush(stdout);
        if (!arg(1).empty()) {
            std::filesystem::path file(arg(1));
            if (file.is_relative() && !capturesDir_.empty()) file = capturesDir_ / file;
            std::ofstream(file, std::ios::binary) << md;
            std::printf("[script] tutoriel-bilan : %s\n", file.string().c_str());
        }
        return Step::Next;
    }
    // Tranche 16 : l'A toi de l'etape <n>, lu sur l'etat d'avant ses gestes (voir plus haut). Un
    // avertissement seulement : sans tutoriel, sans scene ou hors de sa place, il le dit et ne compte
    // aucune faute (le code et les etapes justes d'un texte n'en dependent pas).
    if (cmd == "tutoriel-avant") {
        auto* player = tutorials::player();
        auto* st = tutorials::stage();
        if (!player || !st) { std::printf("[script] tutoriel-avant : ignor\xC3\xA9 (aucun tutoriel en cours)\n"); return Step::Next; }
        if (st->busy() && retries_ < 900) return Step::Retry;
        const auto& c = player->compiled();
        const auto n = static_cast<std::size_t>(std::max(1, std::atoi(arg(1).c_str())));
        // L'etat doit etre celui d'avant les gestes de <n> : le debut de l'etape 1, ou la fin de l'etape d'avant.
        const bool before = n <= c.steps.size() &&
                            (n == 1 ? (player->step() == 0 && player->stepTimeMs() <= 0.0)
                                    : (player->step() == n - 2 && player->stepTimeMs() >= static_cast<double>(c.steps[n - 2].durationMs)));
        const std::string where = c.id + " [" + c.variant + "] \xC3\xA9tape " + std::to_string(n);
        if (!before) {
            std::printf("[script] tutoriel-avant : %s : ignor\xC3\xA9 (le lecteur est \xC3\xA0 l'\xC3\xA9tape %zu, %ld ms : pas avant ses gestes)\n",
                        where.c_str(), player->step() + 1, static_cast<long>(player->stepTimeMs()));
            return Step::Next;
        }
        const auto& aTry = c.steps[n - 1].aTry;
        const std::string why = aTry ? help::aTryAlreadyTrue(*aTry, [&](std::string_view p) { return st->read(p); }) : std::string();
        if (!why.empty()) {
            std::printf("[script] AVERTISSEMENT : %s : \xC3\x80 toi d\xC3\xA9j\xC3\xA0 vrai avant ses gestes : %s\n", where.c_str(), why.c_str());
            if (g_tutoRowOpen && !g_tutoRows.empty()) {
                auto& row = g_tutoRows.back();
                ++row.alreadyTrue;
                if (!row.alreadyTrueText.empty()) row.alreadyTrueText += " ; ";
                row.alreadyTrueText += "\xC3\xA9tape " + std::to_string(n) + " : " + why;
            }
        } else {
            std::printf("[script] tutoriel-avant : %s : %s\n", where.c_str(), aTry ? "\xC3\x80 toi faux avant ses gestes" : "pas d'\xC3\x80 toi");
        }
        return Step::Next;
    }
    if (cmd == "tutoriel-infobulles") {
        // 1.11.2 (T1, R1112-6) : tutoriel-infobulles <muettes|rendues> : echoue si les infobulles de l'appli ne
        //  sont pas dans cet etat (muettes pendant la demonstration d'un tutoriel ; rendues a l'« A toi », a la fin).
        const bool muted = menu::WidgetMenu::tooltipsMuted();
        if (arg(1) != (muted ? "muettes" : "rendues"))
            fail("tutoriel-infobulles : attendu " + arg(1) + ", elles sont " + (muted ? "muettes" : "rendues"));
        std::printf("[script] tutoriel-infobulles : %s\n", muted ? "muettes" : "rendues");
        return Step::Next;
    }
    if (cmd.rfind("tutoriel-", 0) == 0) {
        auto* player = tutorials::player();
        if (!player) { fail(cmd + " : aucun tutoriel en cours"); return Step::Next; }
        if (cmd == "tutoriel-attendre") {
            auto* st = tutorials::stage();
            if (st && st->busy() && retries_ < 900) return Step::Retry;
            if (st)
                for (const auto& m : st->missing()) std::printf("[script] tutoriel : introuvable : %s\n", m.c_str());
            return Step::Next;
        }
        if (cmd == "tutoriel-verifier") {
            auto* st = tutorials::stage();
            if (st && st->busy() && retries_ < 900) return Step::Retry;
            if (!st) { fail("tutoriel-verifier : pas de sc\xC3\xA8ne"); return Step::Next; }
            static const void* lastStage = nullptr;
            static std::size_t seen = 0;
            // Tranche 6 : une cible introuvable n'est dite qu'une fois par tutoriel et variante
            // (tutoriel-aller rejoue les etapes d'avant : leurs absences revenaient a chaque etape).
            static std::string saidFor;
            static std::vector<std::string> said;
            const auto& c = player->compiled();
            if (lastStage != st || saidFor != c.id + "|" + c.variant) { lastStage = st; seen = 0; saidFor = c.id + "|" + c.variant; said.clear(); }
            if (seen > st->missing().size()) seen = 0;
            const std::string where = c.id + " [" + c.variant + "] \xC3\xA9tape " + std::to_string(player->step() + 1);
            for (; seen < st->missing().size(); ++seen) {
                const auto& m = st->missing()[seen];
                if (std::find(said.begin(), said.end(), m) != said.end()) continue;
                said.push_back(m);
                fail(where + " : introuvable : " + m);
            }
            const auto& step = c.steps[player->step()];
            if (step.aTry) {
                const auto out = help::evaluateATry(*step.aTry, [&](std::string_view p) { return st->read(p); });
                if (out.result != help::CheckOutcome::Result::Ok) {
                    // Tranche 15 : la faute nomme la premiere condition fausse et la valeur lue.
                    std::string why;
                    for (const auto& ck : step.aTry->checks) {
                        const auto v = st->read(ck.path);
                        if (v && help::evaluateCheck(ck, *v)) continue;
                        // Sur une ligne du bilan : sans retour a la ligne, 80 octets au plus (help::shortReadValue).
                        why = " : " + ck.path + " " + ck.op + " " + ck.value + " est faux, lu \xC2\xAB " +
                              help::shortReadValue(v) + " \xC2\xBB (ligne " + std::to_string(ck.line) + ")";
                        break;
                    }
                    if (out.result == help::CheckOutcome::Result::Almost && !out.message.empty())
                        why += " ; presque : " + out.message;
                    fail(where + " : \xC3\x80 toi n'est pas juste apr\xC3\xA8s ses gestes" + why);
                }
            }
            std::printf("[script] tutoriel-verifier : %s\n", where.c_str());
            if (g_tutoRowOpen && !g_tutoRows.empty() && failures_ == g_tutoStepMark) ++g_tutoRows.back().ok;
            g_tutoStepMark = failures_;
            return Step::Next;
        }
        if (cmd == "tutoriel-aller") {
            g_tutoStepMark = failures_;
            player->seek(static_cast<std::size_t>(std::max(1, std::atoi(arg(1).c_str())) - 1), std::atof(arg(2).c_str()));
        } else if (cmd == "tutoriel-atoi") {
            if (!player->startATry()) fail("tutoriel-atoi : pas d'A toi a cette etape");
        } else if (cmd == "tutoriel-etat") {
            // 1.11.1 (T1, R111-18) : tutoriel-etat <lecture|pause|fin-etape|atoi|fini> [<ms>]
            //  echoue si le lecteur n'est pas dans cet etat ; avec <ms> : si son horloge
            //  (timeMs) n'est pas a <ms> pres celle notee par le dernier tutoriel-etat.
            double& noted = g_tutoClockNoted;
            const help::PlayerState st = player->state();
            const char* name = st == help::PlayerState::Playing ? "lecture"
                             : st == help::PlayerState::Paused  ? "pause"
                             : st == help::PlayerState::StepEnd ? "fin-etape"
                             : st == help::PlayerState::ATry    ? "atoi" : "fini";
            if (arg(1) != name) fail("tutoriel-etat : attendu " + arg(1) + ", le lecteur est en " + name);
            if (!arg(2).empty() && noted >= 0.0 && std::fabs(player->timeMs() - noted) > std::atof(arg(2).c_str()))
                fail("tutoriel-etat : l'horloge a bouge de " + std::to_string(static_cast<long>(player->timeMs() - noted)) + " ms");
            std::printf("[script] tutoriel-etat : %s, horloge %ld ms\n", name, static_cast<long>(player->timeMs()));
            noted = player->timeMs();
        } else if (cmd == "tutoriel-avance") {
            // 1.11.2 (T1, decision 155 : « la barre de progression ne bouge meme pas ») : tutoriel-avance <ms>
            //  echoue si l'horloge du lecteur n'a pas avance d'au moins <ms> depuis le dernier tutoriel-etat
            //  ou tutoriel-avance (le client : le lecteur reste a 0:00 sur la page de l'aide).
            const double now = player->timeMs(), want = std::atof(arg(1).c_str());
            if (g_tutoClockNoted < 0.0) fail("tutoriel-avance : aucune horloge notee avant (tutoriel-etat)");
            else if (now - g_tutoClockNoted < want)
                fail("tutoriel-avance : l'horloge n'a avance que de " + std::to_string(static_cast<long>(now - g_tutoClockNoted))
                     + " ms (au moins " + arg(1) + " attendues)");
            std::printf("[script] tutoriel-avance : horloge %ld ms (+%ld)\n", static_cast<long>(now),
                        static_cast<long>(g_tutoClockNoted < 0.0 ? 0.0 : now - g_tutoClockNoted));
            g_tutoClockNoted = now;
        } else if (cmd == "tutoriel-lit") {
            // 1.11.2 (T1, decision 155) : tutoriel-lit <chemin> <valeur> : la scene lit <chemin> (une condition
            //  des « A toi » : centre.ouvert, onglet.courant...) ; echoue si ce n'est pas <valeur>.
            auto* st = tutorials::stage();
            const auto v = st ? st->read(arg(1)) : std::nullopt;
            if (!v || *v != arg(2))
                fail("tutoriel-lit : " + arg(1) + " vaut \xC2\xAB " + help::shortReadValue(v) + " \xC2\xBB, attendu \xC2\xAB " + arg(2) + " \xC2\xBB");
            std::printf("[script] tutoriel-lit : %s = %s\n", arg(1).c_str(), help::shortReadValue(v).c_str());
        } else if (cmd == "tutoriel-cadre") {
            // 1.11.2 (T1, R1112-8 : « Essayer » sur objet-vanne, la tuile Vanne n'etait pas a l'ecran) : tutoriel-cadre <cible>
            //  echoue si l'encadre de la scene n'est pas sur <cible>, retrouvee a l'ecran (UiDriver::locate l'y amene).
            auto* st = tutorials::stage();
            const std::string on = st ? st->spotTarget() : std::string();
            const auto r = st ? st->spot() : std::nullopt;
            if (on != arg(1) || !r)
                fail("tutoriel-cadre : l'encadre est sur \xC2\xAB " + on + " \xC2\xBB" + (r ? "" : " (pas a l'ecran)") + ", attendu \xC2\xAB " + arg(1) + " \xC2\xBB");
            else
                std::printf("[script] tutoriel-cadre : %s : %.0f %.0f %.0f x %.0f\n", on.c_str(), r->x, r->y, r->w, r->h);
        } else if (cmd == "tutoriel-etape") {
            // 1.11.2 (T1, R1112-4) : tutoriel-etape <n> : echoue si le lecteur n'est pas a l'etape <n> (1 : la premiere).
            if (player->step() + 1 != static_cast<std::size_t>(std::max(0, std::atoi(arg(1).c_str()))))
                fail("tutoriel-etape : attendu " + arg(1) + ", le lecteur est a l'\xC3\xA9tape " + std::to_string(player->step() + 1));
        } else if (cmd == "tutoriel-mot") {
            // 1.11.2 (T1, R1112-4) : tutoriel-mot <morceau> : le mot du lecteur (« Essayer » commence a l'etape 2 ;
            //  ce tutoriel n'a pas d'« A toi ») le contient ; « - » : aucun mot.
            const std::string said = player->notice();
            if (arg(1) == "-" ? !said.empty() : said.find(arg(1)) == std::string::npos)
                fail("tutoriel-mot : \xC2\xAB " + said + " \xC2\xBB, attendu \xC2\xAB " + arg(1) + " \xC2\xBB");
            std::printf("[script] tutoriel-mot : %s\n", said.c_str());
        } else if (cmd == "tutoriel-pause") {
            player->pause();
        } else if (cmd == "tutoriel-lecture") {
            player->play();
        } else if (cmd == "tutoriel-quitter") {
            tutorials::stop();
            g_tutoRowOpen = false;
        } else {
            fail("commande inconnue : " + cmd);
        }
        std::printf("[script] %s : etape %zu / %zu\n", cmd.c_str(), player->step() + 1, player->compiled().steps.size());
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin 1.11 (T1, tranche 3) ----
    if (cmd == "dire") {
        say_ = arg(1);
        std::printf("[script] dire : %s\n", say_.c_str());
        return Step::Yield;
    }
    // ---- fin 1.11 (T1) ----
    if (cmd == "texte") { send(ui::TextInput{arg(1)}); return Step::Yield; }
    // Lot 20 : le presse-papiers du systeme, comme si on venait de copier dans
    // Excel : presse-papiers "Nom\tType\nPression\tREAL" (\t, \n, \\) ;
    // presse-papiers-fichier "chemin" (un tableau copie, en texte) ;
    // presse-papiers-contient "texte" : echoue s'il n'y est pas (ce que Ctrl+C a copie).
    if (cmd == "presse-papiers" || cmd == "presse-papiers-fichier" || cmd == "presse-papiers-contient") {
        std::string text;
        if (cmd == "presse-papiers-fichier") {
            std::ifstream in(arg(1), std::ios::binary);
            if (!in) { fail("fichier illisible : " + arg(1)); return Step::Next; }
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        } else {
            const std::string& raw = arg(1);
            for (std::size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size()) {
                    const char n = raw[i + 1];
                    if (n == 't') { text += '\t'; ++i; continue; }
                    if (n == 'n') { text += "\r\n"; ++i; continue; }
                    if (n == '\\') { text += '\\'; ++i; continue; }
                }
                text += raw[i];
            }
        }
        if (cmd == "presse-papiers-contient") {
            const std::string have = ui::clipboardText();
            if (have.find(text) == std::string::npos) fail("le presse-papiers ne contient pas \"" + arg(1) + "\" (" + std::to_string(have.size()) + " octets)");
            return Step::Next;
        }
        ui::setClipboardText(text);
        return Step::Next;
    }
    if (cmd == "deposer") {
        // Un fichier glisse depuis l'explorateur et lache en (x, y), ou la ou
        // est la souris : ce que l'application recoit de SDL_EVENT_DROP_FILE.
        // Lot 7 : par App::dropFile, comme SDL (un .XPG, un .XHW sur l'espace de
        // travail : la question) ; "a.XPG;b.XHW" : deux fichiers du meme depot.
        const gfx::Point p = w.size() > 3 ? gfx::Point{num(2), num(3)} : driver_.mouse();
        moveTo(p, {});
        sinceInput_ = 0;
        const std::string& files = arg(1);
        std::size_t from = 0;
        while (from <= files.size()) {
            auto semi = files.find(';', from);
            if (semi == std::string::npos) semi = files.size();
            std::string one = files.substr(from, semi - from);
            while (!one.empty() && one.back() == ' ') one.pop_back();
            while (!one.empty() && one.front() == ' ') one.erase(0, 1);
            if (!one.empty() || files.empty()) app_.dropFile(ui::FileDropped{one, p});
            from = semi + 1;
        }
        return Step::Yield;
    }
    // ---- lot 7 : un .XPG dans le projet ouvert (ImportMastDialog) -------------
    //  importer-mast "chemin.xpg" ["config.xhw"]  le recapitulatif, comme Projet >
    //                                  Importer un .XPG (et le .XHW qui vient avec)
    //  import-onglet "Supprime"        un onglet du recapitulatif (Garde, Change,
    //                                  Supprime, Nouveau, Liens) - sans casse ni accents
    //  import-garder oui|non           la case "Garder les variables identiques..."
    //  import-confirmer                "Importer et remplacer" ; import-annuler : Annuler
    if (cmd == "importer-mast") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) { fail("importer-mast : l'espace de travail n'est pas au-dessus"); return Step::Next; }
        screen->importMastIntoProject(arg(1), arg(2));
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "import-onglet" || cmd == "import-garder" || cmd == "import-confirmer" || cmd == "import-annuler") {
        auto* dialog = dynamic_cast<ImportMastDialog*>(app_.menus().top());
        if (!dialog) {
            // Le recapitulatif s'ouvre a l'image suivante (ShowDialog) : un peu de patience.
            if (retries_ < 30) return Step::Retry;
            fail(cmd + " : pas de r\xC3\xA9" "capitulatif d'import ouvert");
            return Step::Next;
        }
        if (cmd == "import-onglet") {
            if (!dialog->selectTab(arg(1))) fail("onglet introuvable : " + arg(1));
        } else if (cmd == "import-garder") {
            dialog->setKeepIdentical(lower(arg(1)) != "non");
        } else if (cmd == "import-confirmer") {
            dialog->confirm();
        } else {
            dialog->cancel();
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- Lot API 8 : glisser n'importe quel fichier ----
    //  deposer "a.xlsm;logo.png" x y   (plus haut) : autre chose qu'un .XPG / .XHW ouvre "Que faire de ces fichiers ?"
    //  depot-lignes                    ce que montre ce dialogue, une ligne par case ([x], [ ], [-] grisee, (o) le .XPG)
    //  depot-cocher "debut" oui|non    une case par le debut de son libelle, sans casse ni accents :
    //                                  "Lancer ImporterCartes", "logo.png : Ressources", un choix du .XPG ("Ne pas")
    //  depot-choisir "debut" "element" la liste d'une ligne : la recette, la table d'animation ; un fichier
    //                                  deja la ("logo.png") : Remplacer, Garder les deux, Ignorer
    //  depot-faire / depot-annuler     les boutons Faire, Annuler
    //  depot-bilan                     ce que le dernier Faire a fait, une ligne par chose
    //  depot-macro-suivante            la macro cochee suivante tout de suite (sans attendre Fermer)
    if (cmd == "depot-lignes" || cmd == "depot-cocher" || cmd == "depot-choisir" || cmd == "depot-faire" || cmd == "depot-annuler") {
        auto* dialog = dynamic_cast<DropFilesDialog*>(app_.menus().top());
        if (!dialog) {
            // Le dialogue s'ouvre apres le depot, a l'image suivante : un peu de patience.
            if (retries_ < 30) return Step::Retry;
            fail(cmd + " : pas de dialogue de d\xC3\xA9p\xC3\xB4t ouvert");
            return Step::Next;
        }
        std::string why;
        if (cmd == "depot-lignes") {
            for (const auto& l : dialog->lines()) std::printf("[script] depot : %s\n", l.c_str());
        } else if (cmd == "depot-cocher") {
            if (!dialog->check(arg(1), lower(arg(2)) != "non", &why)) fail("depot-cocher \"" + arg(1) + "\" : " + why);
        } else if (cmd == "depot-choisir") {
            if (!dialog->choose(arg(1), arg(2), &why)) fail("depot-choisir \"" + arg(1) + "\" : " + why);
        } else if (cmd == "depot-faire") {
            if (dialog->count() == 0) fail("depot-faire : rien n'est coch\xC3\xA9");
            else dialog->confirm();
        } else {
            dialog->cancel();
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "depot-bilan" || cmd == "depot-macro-suivante") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) { fail(cmd + " : l'espace de travail n'est pas au-dessus"); return Step::Next; }
        if (cmd == "depot-bilan") {
            for (const auto& l : screen->dropReport()) std::printf("[script] depot-bilan : %s\n", l.c_str());
            if (screen->dropReport().empty()) std::printf("[script] depot-bilan : (rien)\n");
        } else if (!screen->nextDroppedMacro()) {
            fail("depot-macro-suivante : aucune macro du d\xC3\xA9p\xC3\xB4t n'attend");
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : glisser n'importe quel fichier ----
    // ---- Lot API 8 : glisser de fichiers, 2e partie ----
    //  deposer "notice.pdf" x y        (plus haut) le dialogue "Que faire de ces fichiers ?" ouvert : le fichier s'y ajoute
    //  depot-ajouter "a.pdf;b.zip"     ajouter des fichiers au dialogue ouvert, sans souris ; dit combien de lignes de plus
    //  bouton "Ouvrir avec*"           (commande generique) IHM > Fichiers externes, un document choisi : l'ouvrir avec le
    //                                  programme du systeme
    if (cmd == "depot-ajouter") {
        auto* dialog = dynamic_cast<DropFilesDialog*>(app_.menus().top());
        if (!dialog) {
            if (retries_ < 30) return Step::Retry;
            fail("depot-ajouter : pas de dialogue de d\xC3\xA9p\xC3\xB4t ouvert");
            return Step::Next;
        }
        std::vector<std::string> paths;
        const std::string& all = arg(1);
        for (std::size_t from = 0; from <= all.size();) {
            auto semi = all.find(';', from);
            if (semi == std::string::npos) semi = all.size();
            std::string one = all.substr(from, semi - from);
            while (!one.empty() && one.back() == ' ') one.pop_back();
            while (!one.empty() && one.front() == ' ') one.erase(0, 1);
            if (!one.empty()) paths.push_back(std::move(one));
            from = semi + 1;
        }
        std::string why;
        const std::size_t added = dialog->addFiles(paths, &why);
        std::printf("[script] depot-ajouter : %zu ligne(s) de plus%s\n", added, added ? "" : (" (" + why + ")").c_str());
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

    auto* root = top();
    if (!root) { fail("aucun ecran"); return Step::Next; }

    // Lot macros 1 : la page Macros ou Blocs de l'aide - un lien de l'article
    // ("run:ImporterClasseur", "lib:DFB_EQ_PUMP", "cat:Creer"), ou une page.
    // Lot API 8 : sans page Macros / Blocs ouverte, "aide-page <ancre>" est celle de
    // l'aide generale (plus bas, bloc "didacticiels et aide").
    LibraryHelpPage* libraryPage = nullptr;
    if (cmd == "aide-page") walk(*root, [&](ui::Widget& x) { if (!libraryPage) libraryPage = dynamic_cast<LibraryHelpPage*>(&x); });
    if (cmd == "aide-lien" || (cmd == "aide-page" && libraryPage)) {
        LibraryHelpPage* page = nullptr;
        walk(*root, [&](ui::Widget& x) { if (!page) page = dynamic_cast<LibraryHelpPage*>(&x); });
        if (!page) { fail("aucune page d'aide des macros ou des blocs"); return Step::Next; }
        if (cmd == "aide-lien") page->linkForTest(arg(1));
        else page->goTo(help::Target{help::TargetKind::LibraryEntry, arg(1), {}});
        sinceInput_ = 0;
        return Step::Yield;
    }

    if (cmd == "bouton") {
        ui::Button* found = nullptr;
        // Lot API 6 : "Terminer la V*" - le debut du libelle (le numero de la
        // version depend de ce que les sessions d'avant ont fait).
        const bool prefix = !arg(1).empty() && arg(1).back() == '*';
        const std::string want = prefix ? arg(1).substr(0, arg(1).size() - 1) : arg(1);
        walk(*root, [&](ui::Widget& x) {
            auto* b = dynamic_cast<ui::Button*>(&x);
            if (b && shown(*b) && b->enabled() && (prefix ? b->text().rfind(want, 0) == 0 : b->text() == want)) found = b;
        });
        if (!found) {
            // Lot API 5 : les dialogues passes en francais - les sessions d'avant
            // disent encore "Create", "Cancel", "Import", "Open", "Close", "Add".
            static const std::pair<const char*, const char*> kFrench[] = {
                {"Create", "Cr\xC3\xA9" "er"}, {"Cancel", "Annuler"}, {"Import", "Importer"},
                {"Open", "Ouvrir"}, {"Close", "Fermer"}, {"Add", "Ajouter"}};
            for (const auto& [en, fr] : kFrench) {
                if (arg(1) != en) continue;
                walk(*root, [&](ui::Widget& x) {
                    auto* b = dynamic_cast<ui::Button*>(&x);
                    if (b && shown(*b) && b->enabled() && b->text() == fr) found = b;
                });
            }
        }
        if (!found && (arg(1) == "D\xC3\xA9marrage auto\xE2\x80\xA6" || arg(1) == "D\xC3\xA9marrage auto...")) {
            // Lot API 7 : l'accueil refait - "Demarrage auto..." est devenu
            // "Choisir..." / "Changer..." sous le poste d'exploitation : par son id.
            walk(*root, [&](ui::Widget& x) {
                auto* b = dynamic_cast<ui::Button*>(&x);
                if (b && shown(*b) && b->enabled() && b->id() == "startup.autostart") found = b;
            });
        }
        if (!found) {
            // Lot 19 : un bouton a bascule (Edit d'une section de l'automate).
            ui::ToggleButton* toggle = nullptr;
            walk(*root, [&](ui::Widget& x) {
                auto* t = dynamic_cast<ui::ToggleButton*>(&x);
                if (t && shown(*t) && t->enabled() && t->text() == arg(1)) toggle = t;
            });
            if (toggle) {
                click(centre(toggle->bounds()), MouseButton::Left, 1, {});
                return Step::Yield;
            }
        }
        if (!found) {
            // Lot macros 1 : un onglet de l'aide (Aide, Macros, Blocs DFB / DDT,
            // IHM) ou son bouton Fermer.
            gfx::Rect r{};
            walk(*root, [&](ui::Widget& x) {
                auto* strip = dynamic_cast<HelpTabStrip*>(&x);
                if (!strip || !shown(*strip) || r.w > 0.f) return;
                if (arg(1) == "Fermer") r = strip->closeRect();
                else if (const int t = HelpTabStrip::tabOfLabel(arg(1)); t >= 0) r = strip->tabRect(t);
            });
            if (r.w > 0.f) {
                click(centre(r), MouseButton::Left, 1, {});
                return Step::Yield;
            }
        }
        if (!found) {
            // Lot API 7 : les boutons de l'ancien ecran de simulation (F9), en
            // anglais, deviennent ceux de l'onglet API > Simulation. Run, Pause,
            // Stop, Step : la barre du haut (plus bas) ; Workspace : on y est.
            static const std::pair<const char*, int> kOldSimulation[] = {
                {"Force", SimulationPane::AForce}, {"Release", SimulationPane::ARelease}, {"Watch", SimulationPane::AWatch},
                {"Export forcing", SimulationPane::AExport}, {"Import forcing", SimulationPane::AImport}};
            if (arg(1) == "Workspace") {
                (void)app_.actions().trigger("sim.back", app_.commands());
                return Step::Yield;
            }
            for (const auto& [old, action] : kOldSimulation) {
                if (arg(1) != old) continue;
                // L'onglet ouvert par l'action sim.open d'avant (F9), ou qu'il soit.
                // Lot API 8 : F9 ouvre la Vue d'ensemble - l'onglet Automate s'ouvre ici.
                auto* pane = apiPaneOf<SimulationPane>(app_, "simulation", /*open=*/true);
                if (!pane) break;
                pane->runAction(action);
                sinceInput_ = 0;
                return Step::Yield;
            }
        }
        if (!found) {
            // Lot API 2 : un bouton de la barre du haut, par son libelle d'aujourd'hui
            // ("Historique", "Simuler") ou par celui de l'ANCIENNE barre ("Macros",
            // "Save", "Refresh") - les sessions d'avant le lot 2 se rejouent telles quelles.
            TopBar* bar = nullptr;
            walk(*root, [&](ui::Widget& x) { if (!bar) bar = dynamic_cast<TopBar*>(&x); });
            if (bar) {
                const auto action = bar->actionForLabel(arg(1));
                if (!action.empty()) {
                    bar->trigger(action);
                    sinceInput_ = 0;
                    return Step::Yield;
                }
            }
        }
        if (!found) { fail("bouton introuvable : " + arg(1)); return Step::Next; }
        click(centre(found->bounds()), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot API 2 : la barre du haut, a la vraie souris.
    //   barre "historique" [survol]  une partie (projet, annuler, retablir, historique,
    //                         nouveau, aller, simuler, arreter, cycle, control-expert,
    //                         configuration, affichage, aide) - un menu s'ouvre ;
    //                         survol : la souris dessus, pour son info-bulle
    //   barre-menu "Projet" "Enregistrer sous..."   ouvre le menu et clique l'entree
    if (cmd == "barre" || cmd == "barre-menu") {
        TopBar* bar = nullptr;
        walk(*root, [&](ui::Widget& x) { if (!bar) bar = dynamic_cast<TopBar*>(&x); });
        if (!bar) { fail("barre du haut introuvable"); return Step::Next; }
        std::string part = arg(1);
        for (auto& ch : part) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        // Lot API 6 : l'icone du projet, a gauche du nom (un clic ouvre son editeur).
        if (part == "logo" || part == "icone") {
            const auto r = bar->partRect("projet");
            if (r.w <= 0.f) { fail("partie introuvable : projet"); return Step::Next; }
            const gfx::Point at{r.x + 21.f, r.y + r.h * 0.5f};
            if (has("survol")) { moveTo(at, {}); return Step::Yield; }
            click(at, MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (part == "projet" || part == "version" || part == "nouveau" || part == "affichage" || part == "aide" || part == "les-deux"
            || cmd == "barre") {
            if (cmd == "barre-menu" && w.size() > 2) {
                const auto m = part == "projet" ? TopBar::Menu::Project : part == "nouveau" ? TopBar::Menu::New
                             : part == "version" ? TopBar::Menu::Version
                             : part == "les-deux" ? TopBar::Menu::Both   // 1.10 : l'API et l'IHM ensemble
                             : part == "affichage" ? TopBar::Menu::View : TopBar::Menu::Help;
                // Lot API 7 : trois entrees ont quitte Affichage - des onglets de
                // l'API. Une session d'avant qui les choisit arrive sur l'onglet.
                if (m == TopBar::Menu::View) {
                    static const std::pair<const char*, const char*> kMoved[] = {
                        {"\xC3\x89" "cran de simulation", "sim.open"}, {"Ecran de simulation", "sim.open"},
                        {"Explorateur de variables", "view.variables"}, {"Statistiques", "view.statistics"}};
                    for (const auto& [old, action] : kMoved) {
                        if (!startsWith(arg(2), old)) continue;
                        walk(*root, [&](ui::Widget& x) {
                            if (auto* pm = dynamic_cast<ui::PopupMenu*>(&x); pm && pm->isOpen()) pm->close();
                        });
                        if (auto r = app_.actions().trigger(action, app_.commands()); !r)
                            fail(std::string("action ") + action + " : " + r.error().message());
                        sinceInput_ = 0;
                        return Step::Yield;
                    }
                }
                if (bar->openedMenu() != m) {
                    const auto r = bar->partRect(part);
                    if (r.w <= 0.f) { fail("partie introuvable : " + arg(1)); return Step::Next; }
                    click(centre(r), MouseButton::Left, 1, {});
                    return Step::Retry;       // le menu s'ouvre ; la ligne est rejouee pour cliquer l'entree
                }
                ui::PopupMenu* menu = nullptr;
                walk(*root, [&](ui::Widget& x) {
                    if (auto* pm = dynamic_cast<ui::PopupMenu*>(&x); pm && pm->isOpen()) menu = pm;
                });
                if (!menu) { fail("le menu ne s'est pas ouvert"); return Step::Next; }
                const auto want = arg(2);
                for (std::size_t i = 0; i < menu->items().size(); ++i) {
                    const auto& label = menu->items()[i].label;
                    if (!menu->items()[i].separator && !menu->items()[i].heading && (label == want || startsWith(label, want))) {
                        click(centre(menu->itemRect(i)), MouseButton::Left, 1, {});
                        return Step::Yield;
                    }
                }
                fail("pas dans le menu : " + want);
                return Step::Next;
            }
            // ---- Lot API 8 : bandeau haut ----
            //  Historique a quitte le bandeau (la liste d'Annuler le donne) : son
            //  action, telle quelle - les sessions d'avant se rejouent.
            if (const auto hidden = bar->hiddenPartAction(part); !hidden.empty() && !has("survol")) {
                bar->trigger(hidden);
                sinceInput_ = 0;
                return Step::Yield;
            }
            // ---- fin Lot API 8 : bandeau haut ----
            const auto r = bar->partRect(part);
            if (r.w <= 0.f) { fail("partie de la barre introuvable : " + arg(1)); return Step::Next; }
            if (has("survol")) { moveTo(centre(r), {}); return Step::Yield; }    // son info-bulle
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        fail("barre-menu : projet, version, nouveau, affichage, aide ou les-deux");
        return Step::Next;
    }
    // ---- Lot API 8 : bandeau haut ----
    //  bandeau-cloche [ouvrir|fermer|lu]    la cloche : ouvre son panneau (defaut), le ferme, ou "Tout marquer comme lu"
    //  bandeau-projet-menu                  le menu de la puce projet (recents, Nouveau, Ouvrir, Enregistrer sous, Fermer...)
    //  bandeau-version-menu                 le menu de la puce version (Creer un essai..., les versions, comparer)
    //  bandeau-annuler-liste [n]            la liste des 10 dernieres actions ; n : y revenir (annule les n dernieres)
    //  bandeau-taches                       le detail des taches de fond (la jauge)
    //  bandeau-enregistrer                  le bouton Enregistrer de la puce projet (vraie souris)
    //  bandeau-tache "Generation IHM" 62    (captures) une tache de fond a 62 % ; bandeau-tache fin : la retire
    //  bandeau-avis "titre" ["detail"] ["bouton"] ["action"]  (captures) un avis dans la cloche (visible dans la seconde)
    //  palette ">compiler" [entree|n]       Aller a / Faire... (Ctrl+K) en mode commandes : tape le texte, attend
    //                                       les resultats ; entree : le premier, n : le n-ieme (comme aller-a)
    if (cmd == "bandeau-cloche" || cmd == "bandeau-projet-menu" || cmd == "bandeau-version-menu" || cmd == "bandeau-annuler-liste"
        || cmd == "bandeau-taches" || cmd == "bandeau-enregistrer" || cmd == "bandeau-tache" || cmd == "bandeau-avis") {
        if (cmd == "bandeau-tache") {
            static int demoTask = 0;
            if (arg(1) == "fin") {
                if (demoTask) bgtasks::end(demoTask);
                demoTask = 0;
                return Step::Yield;
            }
            if (demoTask) bgtasks::end(demoTask);
            demoTask = bgtasks::begin(arg(1).empty() ? std::string("T\xC3\xA2" "che") : arg(1));
            bgtasks::progress(demoTask, arg(2).empty() ? -1.f : static_cast<float>(std::atof(arg(2).c_str()) / 100.0));
            return Step::Yield;
        }
        if (cmd == "bandeau-avis") {
            if (arg(1).empty()) { fail("bandeau-avis : quel titre ?"); return Step::Next; }
            bgtasks::Notice n;
            n.key = "script:" + arg(1);
            n.group = "Projet";
            n.title = arg(1);
            n.detail = arg(2);
            n.button = arg(3);
            n.action = arg(4);
            bgtasks::post(std::move(n));
            return Step::Yield;
        }
        TopBar* bar = nullptr;
        walk(*root, [&](ui::Widget& x) { if (!bar) bar = dynamic_cast<TopBar*>(&x); });
        if (!bar) { fail("bandeau haut introuvable (" + cmd + ")"); return Step::Next; }
        const auto closeMenus = [&] {
            walk(*root, [&](ui::Widget& x) {
                if (auto* pm = dynamic_cast<ui::PopupMenu*>(&x); pm && pm->isOpen()) pm->close();
            });
        };
        // Un clic sur la partie (sur son chevron pour la puce projet : son logo
        // ouvre l'editeur d'icone) ; deja ouvert : rien.
        const auto openPart = [&](const char* key, TopBar::Menu m) {
            if (bar->openedMenu() == m) return Step::Yield;
            const auto r = bar->partRect(key);
            if (r.w <= 0.f) { fail(std::string("partie du bandeau introuvable : ") + key); return Step::Next; }
            const gfx::Point at = m == TopBar::Menu::Project ? gfx::Point{r.x + r.w - 12.f, r.y + r.h * 0.5f} : centre(r);
            click(at, MouseButton::Left, 1, {});
            return Step::Yield;
        };
        if (cmd == "bandeau-cloche") {
            if (arg(1) == "fermer") { closeMenus(); return Step::Yield; }
            if (arg(1) == "lu") { bar->markNoticesRead(); closeMenus(); return Step::Yield; }
            return openPart("cloche", TopBar::Menu::Notices);
        }
        if (cmd == "bandeau-projet-menu") return openPart("projet", TopBar::Menu::Project);
        if (cmd == "bandeau-version-menu") return openPart("version", TopBar::Menu::Version);
        if (cmd == "bandeau-taches") return openPart("taches", TopBar::Menu::Tasks);
        if (cmd == "bandeau-enregistrer") {
            const auto r = bar->partRect("enregistrer");
            if (r.w <= 0.f) { fail("partie du bandeau introuvable : enregistrer"); return Step::Next; }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        // bandeau-annuler-liste [n]
        if (arg(1).empty()) return openPart("annuler-liste", TopBar::Menu::UndoList);
        const int n = std::atoi(arg(1).c_str());
        if (n < 1 || static_cast<std::size_t>(n) > bar->undoList().size()) {
            fail("bandeau-annuler-liste : de 1 \xC3\xA0 " + std::to_string(bar->undoList().size()));
            return Step::Next;
        }
        closeMenus();
        bar->trigger("edit.undoTo:" + std::to_string(n));
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "palette" && !arg(1).empty() && arg(1)[0] == '>') {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        auto* go = screen ? screen->goToPanel(true) : nullptr;
        if (!go) { fail("palette : l'\xC3\xA9" "cran d'analyse n'est pas au-dessus"); return Step::Next; }
        if (retries_ == 0) {
            if (!go->text().empty()) go->setText("");
            send(ui::TextInput{arg(1)});
            return Step::Retry;
        }
        if (go->pending() && retries_ < 240) return Step::Retry;
        go->flush();
        std::printf("[script] palette \"%s\" : %zu commande(s)\n", arg(1).c_str(), go->results().size());
        if (has("entree") || has("entr\xC3\xA9" "e") || isNumber(arg(2))) {
            if (go->results().empty()) { fail("palette : aucune commande pour \xC2\xAB " + arg(1) + " \xC2\xBB"); return Step::Next; }
            const int index = isNumber(arg(2)) ? std::max(1, std::atoi(arg(2).c_str())) - 1 : go->current();
            if (index >= static_cast<int>(go->results().size())) { fail("palette : pas de commande " + arg(2)); return Step::Next; }
            go->activate(index);
        }
        return Step::Yield;
    }
    // ---- fin Lot API 8 : bandeau haut ----
    // Lot API 2 : le tableau de bord de l'API - une carte ou un bouton, par sa cle
    // (configuration, ordre, types, variables, variables-inutilisees,
    // bibliotheque, import-xhw, macros).
    if (cmd == "tableau-de-bord") {
        ApiDashboard* dash = nullptr;
        walk(*root, [&](ui::Widget& x) { if (!dash) if (auto* d = dynamic_cast<ApiDashboard*>(&x); d && shown(*d)) dash = d; });
        if (!dash) { fail("tableau de bord de l'API introuvable"); return Step::Next; }
        const auto r = dash->partRect(arg(1));
        if (r.w <= 0.f) {
            if (retries_ < 3) return Step::Retry;     // le temps d'un dessin
            fail("pas de carte ni de bouton : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot API 2 : la legende de l'onglet Macros - une pastille (0 a 6 : classeur,
    // CSV, enchaine, bibliotheque, sections, variables, fichier), "tout", ou replier.
    if (cmd == "macros-pastille" || cmd == "macros-legende") {
        MacrosPane* pane = nullptr;
        walk(*root, [&](ui::Widget& x) { if (!pane) if (auto* p = dynamic_cast<MacrosPane*>(&x); p && shown(*p)) pane = p; });
        if (!pane) { fail("onglet Macros introuvable"); return Step::Next; }
        if (cmd == "macros-legende") { pane->setLegendOpen(arg(1) != "replier"); return Step::Yield; }
        if (arg(1) == "tout") { pane->setPipFilter(-1); return Step::Yield; }
        const auto r = pane->legendPipRect(std::atoi(arg(1).c_str()));
        if (r.w <= 0.f) { if (retries_ < 3) return Step::Retry; fail("pastille introuvable : " + arg(1)); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "liste") {
        // Ouvrir la N-ieme liste deroulante visible (un champ a choix d'un
        // dialogue), comme d'un clic. Lot 12 : ou celle de cet identifiant
        // (liste "hmi.find.scope"), dans un volet de l'ecran principal.
        std::vector<ui::DropDown*> lists;
        walk(*root, [&](ui::Widget& x) {
            if (auto* d = dynamic_cast<ui::DropDown*>(&x); d && shown(*d)) lists.push_back(d);
        });
        ui::DropDown* target = nullptr;
        if (isNumber(arg(1))) {
            const auto n = static_cast<std::size_t>(std::max(1, std::atoi(arg(1).c_str())));
            if (n <= lists.size()) target = lists[n - 1];
        } else {
            for (auto* d : lists) if (d->id() == arg(1)) target = d;
        }
        if (!target) { fail("liste " + arg(1) + " introuvable"); return Step::Next; }
        click(centre(target->bounds()), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "choisir") {
        // Dans la liste deroulee ouverte (une propriete a choix, un champ de
        // dialogue) : l'element de ce libelle, au clavier comme on le ferait.
        ui::DropDown* open = nullptr;
        walk(*root, [&](ui::Widget& x) {
            if (auto* d = dynamic_cast<ui::DropDown*>(&x); d && d->isOpen()) open = d;
        });
        if (!open) {
            // Lot 18 : un menu ouvert (le mouvement d'une valeur simulee) - l'entree de ce libelle, cliquee.
            ui::PopupMenu* menu = nullptr;
            walk(*root, [&](ui::Widget& x) {
                if (auto* m = dynamic_cast<ui::PopupMenu*>(&x); m && m->isOpen()) menu = m;
            });
            if (menu) {
                // Lot 7 : un sous-menu ouvert ("Ajouter a une table d'animation >",
                // "Ouvrir le code >") - ses entrees d'abord. Choisir l'entree qui
                // porte le sous-menu l'ouvre ; la ligne suivante y choisit.
                if (const int s = menu->openedSubmenu(); s >= 0) {
                    const auto& kids = menu->items()[static_cast<std::size_t>(s)].children;
                    for (std::size_t j = 0; j < kids.size(); ++j)
                        if (!kids[j].separator && kids[j].label == arg(1)) {
                            click(centre(menu->subItemRect(j)), MouseButton::Left, 1, {});
                            return Step::Yield;
                        }
                }
                for (std::size_t i = 0; i < menu->items().size(); ++i)
                    if (!menu->items()[i].separator && menu->items()[i].label == arg(1)) {
                        click(centre(menu->itemRect(i)), MouseButton::Left, 1, {});
                        return Step::Yield;
                    }
                fail("pas dans le menu : " + arg(1));
                return Step::Next;
            }
        }
        if (!open) { fail("aucune liste ouverte (" + arg(1) + ")"); return Step::Next; }
        int index = -1;
        for (std::size_t i = 0; i < open->items().size(); ++i)
            if (open->items()[i].label == arg(1)) index = static_cast<int>(i);
        // Sinon, le seul element qui commence ainsi ("Tous :" quel que soit le compte).
        if (index < 0) {
            int found = 0;
            for (std::size_t i = 0; i < open->items().size(); ++i)
                if (open->items()[i].label.rfind(arg(1), 0) == 0) {
                    index = static_cast<int>(i);
                    ++found;
                }
            if (found != 1) index = -1;
        }
        if (index < 0) { fail("pas dans la liste : " + arg(1)); return Step::Next; }
        send(ui::KeyDown{Key::Home, {}, false});
        for (int i = 0; i < index; ++i) send(ui::KeyDown{Key::Down, {}, false});
        send(ui::KeyDown{Key::Return, {}, false});
        send(ui::KeyUp{Key::Return, {}});
        return Step::Yield;
    }
    if (cmd == "case") {
        // Lot API 8 : Affichage > Panneaux est en francais ; les anciens noms marchent encore.
        static const std::pair<const char*, const char*> kOldBoxes[] = {
            {"Project explorer", "Arbre du projet"}, {"Open documents", "Documents ouverts"},
            {"PLC configuration", "Configuration de l'automate"}, {"Bottom row", "Rang\xC3\xA9" "e du bas"},
            {"Analysis summary", "R\xC3\xA9sum\xC3\xA9 de l'analyse"}, {"Project status", "\xC3\x89tat du projet"},
            {"Alternating row colours", "Lignes altern\xC3\xA9" "es"}};
        std::string want = arg(1);
        for (const auto& [old, now] : kOldBoxes) if (want == old) want = now;
        ui::Checkbox* found = nullptr;
        walk(*root, [&](ui::Widget& x) {
            auto* b = dynamic_cast<ui::Checkbox*>(&x);
            if (!found && b && shown(*b) && (b->label() == arg(1) || b->label() == want)) found = b;
        });
        if (!found) { fail("case introuvable : " + arg(1)); return Step::Next; }
        const auto r = found->bounds();
        click({r.x + 9.f, r.y + r.h * 0.5f}, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "champ") {
        std::vector<ui::InputText*> fields;
        walk(*root, [&](ui::Widget& x) {
            if (auto* t = dynamic_cast<ui::InputText*>(&x); t && shown(*t)) fields.push_back(t);
        });
        // Le N-ieme champ visible ; lot 12 : ou celui de cet identifiant
        // (champ "hmi.find.find" "Armoires[0]"), dans un volet de l'ecran principal.
        ui::InputText* target = nullptr;
        if (isNumber(arg(1))) {
            const auto n = static_cast<std::size_t>(std::max(1, std::atoi(arg(1).c_str())));
            if (n <= fields.size()) target = fields[n - 1];
        } else {
            for (auto* t : fields) if (t->id() == arg(1)) target = t;
        }
        if (!target) { fail("champ " + arg(1) + " introuvable"); return Step::Next; }
        typeInto(centre(target->bounds()), arg(2));
        return Step::Yield;
    }
    if (cmd == "code") {
        // Le code dynamique du moment d'un utilisateur, tape dans le N-ieme
        // champ (le dialogue de connexion) : ce que l'utilisateur lirait sur
        // son application d'authentification.
        auto doc = app_.hmi();
        const auto* u = doc ? doc->project.userByLogin(arg(2)) : nullptr;
        if (!u || u->secret.empty()) { fail("utilisateur sans code dynamique : " + arg(2)); return Step::Next; }
        std::vector<ui::InputText*> fields;
        walk(*root, [&](ui::Widget& x) {
            if (auto* t = dynamic_cast<ui::InputText*>(&x); t && shown(*t)) fields.push_back(t);
        });
        const auto n = static_cast<std::size_t>(std::max(1, std::atoi(arg(1).c_str())));
        if (n > fields.size()) { fail("champ " + arg(1) + " introuvable"); return Step::Next; }
        const auto& sec = doc->project.security;
        typeInto(centre(fields[n - 1]->bounds()), hmi::dynamicCode(u->secret, hmi::wallEpoch(), sec.dynamicPeriodS, sec.dynamicDigits));
        return Step::Yield;
    }
    if (cmd == "onglet") {
        auto* centreTabs = dynamic_cast<ui::TabArea*>(root->findById("analysis.centre"));
        if (!centreTabs) { fail("pas d'onglets ici"); return Step::Next; }
        for (std::size_t i = 0; i < centreTabs->tabCount(); ++i) {
            gfx::Rect r;
            if (startsWith(centreTabs->tab(i)->title, arg(1)) && centreTabs->headerRect(i, r)) {
                // Lot 13 : la barre defile - un en-tete hors de vue est choisi directement.
                // Lot 7 : la bande de SON groupe (le centre en a peut-etre plusieurs).
                const auto* band = centreTabs->stripOf(i);
                const auto b = band ? band->bounds() : centreTabs->bounds();
                if (r.x < b.x || r.x + 24.f > b.right()) { centreTabs->setCurrentIndex(i); return Step::Yield; }
                click({r.x + 24.f, r.y + r.h * 0.5f}, MouseButton::Left, 1, {});
                return Step::Yield;
            }
        }
        // Lot API 2 : les onglets fixes d'avant sont des pages gardees, fermees
        // au depart. "Layout" (les panneaux) s'ouvre par Affichage > Panneaux a
        // afficher - et devient l'onglet courant, ce que demandait la ligne.
        if (arg(1) == "Layout" || arg(1) == "Panneaux") {
            TopBar* bar = nullptr;
            walk(*root, [&](ui::Widget& x) { if (!bar) bar = dynamic_cast<TopBar*>(&x); });
            if (bar) {
                bar->trigger("view.layout");
                return Step::Yield;
            }
        }
        fail("onglet introuvable : " + arg(1));
        return Step::Next;
    }
    if (cmd == "fermer-onglet") {
        auto* centreTabs = dynamic_cast<ui::TabArea*>(root->findById("analysis.centre"));
        if (!centreTabs) { fail("pas d'onglets ici"); return Step::Next; }
        for (std::size_t i = 0; i < centreTabs->tabCount(); ++i) {
            gfx::Rect r;
            if (startsWith(centreTabs->tab(i)->title, arg(1)) && centreTabs->headerRect(i, r)) {
                click({r.right() - 10.f, r.y + r.h * 0.5f}, MouseButton::Left, 1, {});   // la croix
                return Step::Yield;
            }
        }
        fail("onglet introuvable : " + arg(1));
        return Step::Next;
    }
    // Lot 7 : le menu d'un onglet du centre.
    //   onglet-menu "Titre"     un VRAI clic droit sur son en-tete : le menu s'ouvre
    //                           (une capture le montre ; "choisir" y prend une entree)
    //   onglets-fermer "Titre" celui-ci|tout|autres|milieu
    //                           en une ligne : le menu, puis son entree (Fermer, Fermer
    //                           tout, Fermer tout sauf celui-ci) ; milieu : un clic du
    //                           milieu sur l'en-tete. Du pas enregistre : la question
    //                           reste ouverte, "bouton" y repond.
    if (cmd == "onglet-menu" || cmd == "onglets-fermer") {
        auto* centreTabs = dynamic_cast<ui::TabArea*>(root->findById("analysis.centre"));
        auto* tabMenu = dynamic_cast<ui::PopupMenu*>(root->findById("analysis.tabMenu"));
        if (!centreTabs || !tabMenu) { fail("pas d'onglets ici"); return Step::Next; }
        const std::string scope = cmd == "onglets-fermer" ? lower(arg(2)) : std::string{};
        std::string entry;
        if (cmd == "onglets-fermer") {
            if (scope.empty() || scope == "celui-ci") entry = "Fermer";
            else if (scope == "tout") entry = "Fermer tout";
            else if (scope == "autres") entry = "Fermer tout sauf celui-ci";
            else if (scope != "milieu") { fail("onglets-fermer : celui-ci, tout, autres ou milieu"); return Step::Next; }
        }
        // La ligne rejouee, le menu ouvert : son entree, cliquee comme a la souris.
        if (!entry.empty() && tabMenu->isOpen()) {
            for (std::size_t i = 0; i < tabMenu->items().size(); ++i) {
                const auto& item = tabMenu->items()[i];
                if (item.separator || item.heading || item.label != entry) continue;
                if (item.enabled) {
                    click(centre(tabMenu->itemRect(i)), MouseButton::Left, 1, {});
                    return Step::Yield;
                }
                send(ui::KeyDown{Key::Escape, {}, false});      // grisee : le menu se referme
                fail(entry + " : " + (item.disabledReason.empty() ? std::string("gris\xC3\xA9" "e") : item.disabledReason));
                return Step::Next;
            }
            send(ui::KeyDown{Key::Escape, {}, false});
            fail("pas dans le menu de l'onglet : " + entry);
            return Step::Next;
        }
        if (!entry.empty() && retries_ > 0) { fail("le menu de l'onglet ne s'est pas ouvert"); return Step::Next; }
        // Un menu deja ouvert mangerait le clic (il se fermerait, sans plus).
        bool menuOpen = false;
        walk(*root, [&](ui::Widget& x) {
            if (auto* pm = dynamic_cast<ui::PopupMenu*>(&x); pm && pm->isOpen()) menuOpen = true;
        });
        if (menuOpen) send(ui::KeyDown{Key::Escape, {}, false});
        for (std::size_t i = 0; i < centreTabs->tabCount(); ++i) {
            gfx::Rect r;
            if (!startsWith(centreTabs->tab(i)->title, arg(1)) || !centreTabs->headerRect(i, r)) continue;
            // A la souris quand l'en-tete se voit ; hors de la bande (elle defile,
            // ou il passe sous le bouton de la liste), le menu s'ouvre sans elle.
            const gfx::Point at{r.x + 24.f, r.y + r.h * 0.5f};
            const auto* band = centreTabs->stripOf(i);      // lot 7 : la bande de son groupe
            const bool inView = band && band->bounds().contains(at) && !band->listButtonRect().contains(at)
                             && !band->arrowRect(false).contains(at) && !band->arrowRect(true).contains(at);
            if (scope == "milieu") {
                if (!inView) { fail("onglet hors de la bande : " + arg(1)); return Step::Next; }
                click(at, MouseButton::Middle, 1, {});
                return Step::Yield;
            }
            if (inView) click(at, MouseButton::Right, 1, {});
            else centreTabs->requestContextMenu(i);
            return entry.empty() ? Step::Yield : Step::Retry;    // rejouee : cliquer l'entree
        }
        fail("onglet introuvable : " + arg(1));
        return Step::Next;
    }
    // Lot API 7 : DEBALLER A TOUTE PROFONDEUR, sans la souris.
    //   types-deplier "m:config_gaz.purge[2]"     (types-replier) - Types derives
    //   dfb-deplier "b:DFB_GRAFCETENGINE"         (dfb-replier)   - Blocs DFB
    //   unites-deplier "g:Logigrammes_A/entrees"  (unites-replier) - Unites de programme
    //   variables-deplier "armoires[0].sorties"   (variables-replier) - Variables
    if (cmd == "types-deplier" || cmd == "types-replier" || cmd == "dfb-deplier" || cmd == "dfb-replier"
        || cmd == "unites-deplier" || cmd == "unites-replier" || cmd == "variables-deplier" || cmd == "variables-replier") {
        const bool open = cmd.find("-deplier") != std::string::npos;
        bool found = false, ok = false;
        const auto visit = [&](ui::Widget& x) {
            if (found) return;
            if (cmd.rfind("types-", 0) == 0) { if (auto* p = dynamic_cast<DerivedTypesPane*>(&x)) { found = true; ok = p->setExpanded(arg(1), open); } }
            else if (cmd.rfind("dfb-", 0) == 0) { if (auto* p = dynamic_cast<DfbPane*>(&x)) { found = true; ok = p->setExpanded(arg(1), open); } }
            else if (cmd.rfind("unites-", 0) == 0) { if (auto* p = dynamic_cast<UnitsPane*>(&x)) { found = true; ok = p->setExpanded(arg(1), open); } }
            else if (auto* p = dynamic_cast<VariablesPane*>(&x)) { found = true; ok = p->setExpanded(arg(1), open); }
        };
        if (auto* page = currentPage()) walk(*page, visit);
        if (!found) walk(*root, visit);
        if (!found) { fail("onglet introuvable (" + cmd + ")"); return Step::Next; }
        if (!ok) fail(cmd + " : rien \xC3\xA0 d\xC3\xA9plier : " + arg(1));
        return Step::Yield;
    }
    // Lot API 7 : l'accueil, sans la souris (l'ecran du dessus doit etre l'accueil).
    //   accueil-projet "Armoire_Gaz" | accueil-chercher "gaz" | accueil-filtre "FINISH" | accueil-tri "Par nom"
    if (cmd == "accueil-projet" || cmd == "accueil-chercher" || cmd == "accueil-filtre" || cmd == "accueil-tri") {
        auto* home = dynamic_cast<StartupScreen*>(app_.menus().top());
        if (!home) { fail(cmd + " : l'accueil n'est pas l'\xC3\xA9" "cran du dessus"); return Step::Next; }
        bool ok = true;
        if (cmd == "accueil-projet") ok = home->selectProject(arg(1));
        else if (cmd == "accueil-chercher") home->setSearch(arg(1));
        else if (cmd == "accueil-filtre") ok = home->chooseFilter(arg(1));
        else ok = home->chooseSort(arg(1));
        if (!ok) fail(cmd + " " + arg(1) + " : introuvable");
        return Step::Yield;
    }
    // Lot 7 : L'AFFICHAGE MULTI-FENETRE.
    //   disposition onglets|groupes|mosaique      le mode du centre (groupes : l'onglet
    //                                             ouvert part a droite s'il est seul)
    //   onglet-diviser "Titre" droite|bas         Diviser a droite / en bas
    //   onglet-deplacer "Titre" "Titre d'un onglet du groupe cible"
    //   onglet-agrandir "Titre"                   la mosaique : sa tuile en grand (et retour)
    //   onglet-detacher "Titre"                   dans une fenetre a lui (fenetre-rattacher le ramene)
    if (cmd == "disposition" || cmd == "onglet-diviser" || cmd == "onglet-deplacer" || cmd == "onglet-agrandir"
        || cmd == "onglet-detacher") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        auto* area = screen ? screen->centreTabs() : nullptr;
        if (!screen || !area) { fail(cmd + " : l'\xC3\xA9" "cran d'analyse n'est pas au-dessus"); return Step::Next; }
        if (cmd == "disposition") {
            if (!screen->setLayoutMode(lower(arg(1)))) fail("disposition : onglets, groupes ou mosaique");
            return Step::Yield;
        }
        const auto find = [&](const std::string& title) -> std::size_t {
            for (std::size_t i = 0; i < area->tabCount(); ++i)
                if (startsWith(area->tab(i)->title, title)) return i;
            return ui::TabArea::npos;
        };
        const auto i = find(arg(1));
        if (i == ui::TabArea::npos) { fail("onglet introuvable : " + arg(1)); return Step::Next; }
        bool ok = false;
        if (cmd == "onglet-diviser") ok = screen->splitTab(i, lower(arg(2)) == "bas");
        else if (cmd == "onglet-agrandir") ok = area->toggleMaximized(i);
        else if (cmd == "onglet-detacher") ok = screen->detachTab(i);
        else {
            const auto j = find(arg(2));
            if (j == ui::TabArea::npos) { fail("onglet cible introuvable : " + arg(2)); return Step::Next; }
            ok = area->moveTabToGroup(i, area->groupOf(j));
        }
        if (!ok) fail(cmd + " \"" + arg(1) + "\" : rien n'a chang\xC3\xA9");
        return Step::Yield;
    }
    // Lot 21 : arbre-glisser "IHM/Vues/Vues/Vue_A" "IHM/Vues/Vues/Ligne 1" [avant|apres] [tenir]
    //  un noeud visible tire sur un autre (ranger dans un dossier, reordonner), a
    //  la vraie souris ; "avant" / "apres" : dans le tiers haut / bas de la cible.
    if (cmd == "arbre-glisser") {
        auto* tree = dynamic_cast<ui::TreeView*>(root->findById("analysis.explorer"));
        if (!tree || !tree->model()) { fail("explorateur introuvable"); return Step::Next; }
        const auto nodeOf = [&](const std::string& path) -> ui::NodeId {
            std::vector<std::string> parts;
            for (std::size_t from = 0;;) {
                const auto slash = path.find('/', from);
                parts.push_back(treeAlias(path.substr(from, slash == std::string::npos ? std::string::npos : slash - from)));
                if (slash == std::string::npos) break;
                from = slash + 1;
            }
            const auto nodes = tree->visibleNodes();
            std::size_t part = 0;
            for (const auto n : nodes) {
                if (!startsWith(tree->model()->text(n), parts[part])) continue;
                if (++part == parts.size()) return n;
            }
            return ui::kInvalidNode;
        };
        const auto a = nodeOf(arg(1)), b = nodeOf(arg(2));
        gfx::Rect ra{}, rb{};
        if (a == ui::kInvalidNode || b == ui::kInvalidNode || !tree->rowRect(a, ra) || !tree->rowRect(b, rb)) {
            if (retries_ < 3) return Step::Retry;
            fail("noeuds introuvables (visibles) : " + arg(1) + " -> " + arg(2));
            return Step::Next;
        }
        float y = rb.y + rb.h * 0.5f;
        if (has("avant")) y = rb.y + 2.f;
        else if (has("apres")) y = rb.bottom() - 2.f;
        drag({ra.right() - 40.f, ra.y + ra.h * 0.5f}, {rb.right() - 40.f, y}, {}, !has("tenir"));
        return Step::Yield;
    }
    // Lot 7 : arbre-menu "API/Variables/Elementaires/Moteur1" - un VRAI clic droit
    //  sur ce noeud de l'arbre (le chemin se lit comme pour "arbre" : visibles
    //  d'abord, puis dans les dossiers replies, qui se deplient) : son menu, adapte
    //  a ce qu'est le noeud, s'ouvre - une capture le montre ; "choisir" y prend
    //  une entree, puis une entree de son sous-menu. Un menu deja ouvert se ferme
    //  d'abord (il mangerait le clic).
    if (cmd == "arbre-menu") {
        if (arg(1).empty()) { fail("arbre-menu \"<chemin dans l'arbre>\""); return Step::Next; }
        walk(*root, [&](ui::Widget& x) {
            if (auto* pm = dynamic_cast<ui::PopupMenu*>(&x); pm && pm->isOpen()) pm->close();
        });
        return run({"arbre", arg(1), "droit"}, renderer);
    }
    // ---- Lot API 8 : l'arbre du projet ----
    //  arbre-filtre "purge"   le filtre au-dessus de l'arbre, comme au clavier ("" : l'efface)
    //  arbre-lignes           ce que montre l'arbre, une ligne par noeud visible (retrait = profondeur)
    //  arbre-versions-toutes [non]   toutes les versions dans l'arbre (non : en bref, les 5 dernieres)
    //  arbre-epingler "IHM/Vues/Vue_A" [non]   l'epingle en haut de l'arbre (non : la retire), comme le clic droit
    //  1.11.12 : arbre-parcourir "IHM" [max]   chaque noeud du sous-arbre (lui compris,
    //  profondeur d'abord, 4000 au plus), quatre par image : son menu du clic droit, sa
    //  carte, puis son clic - ce que ferait la souris. Une fenetre qu'un clic ouvre se
    //  ferme (Annuler). Ecrit le nombre de noeuds visites. La 1.11.11 plantait sur un
    //  clic sur Fonctions ou Popups d'un symbole : ce parcours l'aurait trouve.
    // ---- 1.11.13 : la generation incrementale de l'IHM ----
    //  ihm-build <mode> [portee] [sans-fenetre] : generer, regenerer, compiler,
    //    generer-compiler, regenerer-compiler, demarrer, nettoyer ; la portee : une
    //    cle ("script:12") ou un chemin ("IHM/Vues") ; la fenetre de progression
    //    s'ouvre tout de suite (sauf sans-fenetre).
    //  ihm-build-attendre [secondes] : jusqu'a la fin du build (et du demarrage qui suit).
    //  ihm-build-etat [texte] : l'etat dans le journal ; avec un texte, il doit y etre.
    //  ihm-sorties [diagnostics|console] : le panneau du bas, sur Sorties (ou Diagnostics, ou la Console).
    //  ihm-console-etat [texte] : les chiffres de la Console ; avec un texte, une ligne doit le contenir (1.11.14).
    //  ihm-script-modifier <script> <ligne> : une ligne ajoutee au script (une commande).
    //  ihm-remanence on|off : l'option « Conserver les donnees de simulation entre les demarrages » (1.11.15).
    //  ihm-sim-etat [texte] : l'etat de la simulation IHM (en marche, arretee, la carte, la remanence) ;
    //    avec un texte, il doit y etre (1.11.15).
    //  ihm-variable <nom> [valeur] : la valeur d'une variable IHM dans la simulation ; avec une
    //    valeur, elle doit l'avoir (1.11.15).
    // ---- 1.11.16 : le poste d'exploitation devant (commande poste) ----
    //  ihm-variable <nom> [valeur] : lue sur le poste ; ihm-variable-ecrire <nom> <valeur> :
    //  une saisie de l'operateur (sur le poste, sinon la simulation de l'editeur) ;
    //  ihm-remanence-poste [texte] : ce que le poste dit de ses variables remanentes ;
    //  ihm-remanence-poste-ecrire : les ecrire tout de suite (comme a l'arret).
    //  ihm-curseur [ligne] [script] : l'editeur de scripts IHM (l'onglet courant) - le script
    //  choisi et la ligne du curseur (1, 2...) ; avec une ligne (et un nom), les exiger : apres
    //  un double-clic sur un diagnostic, la source est ouverte a sa ligne (§ 19).
    // 1.11.17 (refonte des scripts, lot 1) : volet-message ["texte"] - le message du volet de
    // code montre (scripts, fonctions, operateurs : « Compiler le script Horloge : 1 faute »)
    // contient le texte ; sans texte, il est seulement ecrit.
    if (cmd == "volet-message") {
        auto* page = currentPage();
        std::string said;
        bool found = false;
        if (page)
            walk(*page, [&](ui::Widget& x) {
                if (found || !shown(x)) return;
                if (auto* sp = dynamic_cast<HmiScriptsPane*>(&x)) { said = sp->lastMessage(); found = true; }
                else if (auto* fp = dynamic_cast<HmiFunctionsPane*>(&x)) { said = fp->lastMessage(); found = true; }
                else if (auto* op = dynamic_cast<HmiOperatorsPane*>(&x)) { said = op->lastMessage(); found = true; }
            });
        std::printf("[script] volet : %s\n", found ? said.c_str() : "(aucun volet de code)");
        if (!found) fail("volet-message : aucun volet de code montr\xC3\xA9");
        else if (!arg(1).empty() && said.find(arg(1)) == std::string::npos) fail("volet-message : \"" + arg(1) + "\" absent de : " + said);
        return Step::Next;
    }
    if (cmd == "ihm-curseur") {
        HmiScriptsPane* scripts = nullptr;
        if (auto* page = currentPage())
            walk(*page, [&](ui::Widget& x) {
                if (auto* p = dynamic_cast<HmiScriptsPane*>(&x); p && !scripts && shown(*p)) scripts = p;
            });
        if (!scripts) { fail(cmd + " : l'onglet courant n'est pas l'\xC3\xA9" "diteur des scripts IHM"); return Step::Next; }
        // Le script doit se VOIR : la Programmation generale montre son onglet Scripts generaux.
        if (scripts->tabs() && scripts->currentTab() != HmiScriptsPane::TabScripts) {
            fail(cmd + " : l'onglet Scripts g\xC3\xA9n\xC3\xA9raux n'est pas montr\xC3\xA9 (onglet " + std::to_string(scripts->currentTab()) + ")");
            return Step::Next;
        }
        const std::size_t line = scripts->editor().caretLine() + 1;
        std::string name;
        if (const auto r = scripts->scriptTable().selectedModelRows(); !r.empty() && scripts->scriptTable().model())
            name = scripts->scriptTable().model()->cellText(r.front(), 0);
        std::printf("[script] editeur IHM : %s, ligne %zu\n", name.empty() ? "(aucun script)" : name.c_str(), line);
        if (!arg(1).empty() && std::to_string(line) != arg(1)) fail(cmd + " : ligne " + std::to_string(line) + ", pas " + arg(1));
        if (!arg(2).empty() && name.find(arg(2)) == std::string::npos) fail(cmd + " : script \"" + name + "\", pas " + arg(2));
        return Step::Next;
    }
    if (cmd == "ihm-variable" || cmd == "ihm-variable-ecrire" || cmd == "ihm-remanence-poste" || cmd == "ihm-remanence-poste-ecrire") {
        HmiSimulationPane* live = nullptr;
        if (auto* station = dynamic_cast<StationScreen*>(app_.menus().top())) live = station->pane();
        if (!live && cmd == "ihm-variable-ecrire") live = app_.liveHmiPane();   // la simulation de l'editeur (son onglet)
        if (cmd == "ihm-remanence-poste" || cmd == "ihm-remanence-poste-ecrire") {
            if (!live) { fail(cmd + " : pas de poste d'exploitation devant"); return Step::Next; }
            if (cmd == "ihm-remanence-poste-ecrire") {
                if (!live->saveRetained(true)) fail(cmd + " : " + live->retainState());
                return Step::Yield;
            }
            std::printf("[script] poste, remanence : %s\n", live->retainState().c_str());
            if (!arg(1).empty() && live->retainState().find(arg(1)) == std::string::npos) fail(cmd + " : \"" + arg(1) + "\" absent de : " + live->retainState());
            return Step::Next;
        }
        if (cmd == "ihm-variable-ecrire") {
            if (!live) { fail(cmd + " : ni poste ni simulation IHM"); return Step::Next; }
            auto& rt = live->runtime();
            const auto* cur = rt.variable(arg(1));
            if (!cur) { fail(cmd + " : variable IHM inconnue (ou une structure) : " + arg(1)); return Step::Next; }
            const std::string& t = arg(2);
            ::sim::Value v;
            switch (cur->type()) {
                case ::sim::Type::Bool: v = ::sim::Value::boolean(t == "TRUE" || t == "true" || t == "1" || t == "VRAI" || t == "vrai"); break;
                case ::sim::Type::Real: v = ::sim::Value::real(std::strtod(t.c_str(), nullptr)); break;
                case ::sim::Type::String: v = ::sim::Value::text(t); break;
                case ::sim::Type::Time: v = ::sim::Value::time(std::strtoll(t.c_str(), nullptr, 10)); break;
                default: v = ::sim::Value::integer(cur->type(), std::strtoll(t.c_str(), nullptr, 10)); break;
            }
            // Une ecriture (forcer puis liberer : la valeur reste, les scripts reprennent la main).
            std::string why;
            if (!rt.forceVariable(arg(1), v, &why)) fail(cmd + " : " + why);
            (void)rt.unforceVariable(arg(1));
            return Step::Yield;
        }
        if (live) {
            const auto* v = live->runtime().variable(arg(1));
            const std::string text = !v ? std::string{} : v->type() == ::sim::Type::String ? v->asString() : v->display();
            std::printf("[script] poste, variable IHM %s = %s\n", arg(1).c_str(), text.empty() ? "(inconnue)" : text.c_str());
            if (!arg(2).empty() && text != arg(2)) fail("ihm-variable : " + arg(1) + " vaut " + (text.empty() ? std::string("(inconnue)") : text) + ", pas " + arg(2));
            return Step::Next;
        }
        // l'editeur : la suite (la simulation IHM de l'editeur)
    }
    if (cmd == "ihm-build" || cmd == "ihm-build-attendre" || cmd == "ihm-build-etat" || cmd == "ihm-sorties" || cmd == "ihm-script-modifier"
        || cmd == "ihm-console-etat" || cmd == "ihm-remanence" || cmd == "ihm-sim-etat" || cmd == "ihm-variable") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (screen) buildScreen_ = screen;
        // la fenetre de progression est devant : l'ecran est dessous (le meme, il ne part pas)
        else if ((cmd == "ihm-build-attendre" || cmd == "ihm-build-etat" || cmd == "ihm-console-etat" || cmd == "ihm-sim-etat" || cmd == "ihm-variable"
                  || cmd == "ihm-remanence")
                 && app_.menus().depth() >= 2)
            screen = buildScreen_;
        // ihm-sorties, la fenetre du build devant (il a dure plus de 0,3 s) : son
        // bouton Voir les sorties, puis l'onglet voulu a l'image suivante.
        if (!screen && cmd == "ihm-sorties" && buildScreen_ && app_.menus().depth() >= 2) {
            ui::Button* outputs = nullptr;
            if (auto* root = top())
                walk(*root, [&](ui::Widget& x) {
                    auto* b = dynamic_cast<ui::Button*>(&x);
                    if (b && b->id() == "hmiBuildProgress.sorties" && shown(*b) && b->enabled()) outputs = b;
                });
            if (outputs) click(centre(outputs->bounds()), MouseButton::Left, 1, {});
            if (outputs || retries_ < 600) return Step::Retry;
        }
        if (!screen) {
            if (cmd == "ihm-build-attendre") { retries_ = 0; return Step::Retry; }
            fail(cmd + " : pas d'\xC3\xA9" "cran d'analyse");
            return Step::Next;
        }
        if (cmd == "ihm-build") {
            std::string why;
            if (!screen->scriptHmiBuild(arg(1), arg(2) == "sans-fenetre" ? std::string{} : arg(2), arg(2) != "sans-fenetre" && arg(3) != "sans-fenetre", &why))
                fail("ihm-build " + arg(1) + " : " + why);
            return Step::Yield;
        }
        if (cmd == "ihm-build-attendre") {
            if (screen->hmiBuildBusy()) {
                const double limit = arg(1).empty() ? 120.0 : std::max(1.0, static_cast<double>(num(1)));
                if (static_cast<double>(retries_) / 60.0 < limit) return Step::Retry;
                fail("ihm-build-attendre : le build ne finit pas");
            }
            std::printf("[script] build IHM : %s\n", screen->hmiBuildSummary().c_str());
            return Step::Next;
        }
        if (cmd == "ihm-build-etat") {
            const auto text = screen->hmiBuildSummary();
            std::printf("[script] build IHM : %s\n", text.c_str());
            if (!arg(1).empty() && text.find(arg(1)) == std::string::npos) fail("ihm-build-etat : \"" + arg(1) + "\" absent de : " + text);
            return Step::Next;
        }
        if (cmd == "ihm-sorties") {
            screen->showHmiBuildOutputs(arg(1) == "diagnostics" ? 1 : arg(1) == "console" ? 2 : 0);
            return Step::Yield;
        }
        if (cmd == "ihm-console-etat") {
            std::printf("[script] console IHM : %s\n", screen->hmiConsoleSummary().c_str());
            if (!arg(1).empty() && !screen->hmiConsoleHas(arg(1))) fail("ihm-console-etat : \"" + arg(1) + "\" absent de la Console");
            return Step::Next;
        }
        if (cmd == "ihm-remanence") {
            if (arg(1) != "on" && arg(1) != "off") { fail("ihm-remanence : on ou off"); return Step::Next; }
            screen->scriptHmiKeepData(arg(1) == "on");
            return Step::Yield;
        }
        if (cmd == "ihm-sim-etat") {
            const auto text = screen->hmiSimulationSummary();
            std::printf("[script] simulation IHM : %s\n", text.c_str());
            if (!arg(1).empty() && text.find(arg(1)) == std::string::npos) fail("ihm-sim-etat : \"" + arg(1) + "\" absent de : " + text);
            return Step::Next;
        }
        if (cmd == "ihm-variable") {
            const auto text = screen->hmiVariableText(arg(1));
            std::printf("[script] variable IHM %s = %s\n", arg(1).c_str(), text.empty() ? "(inconnue)" : text.c_str());
            if (!arg(2).empty() && text != arg(2)) fail("ihm-variable : " + arg(1) + " vaut " + (text.empty() ? std::string("(inconnue)") : text) + ", pas " + arg(2));
            return Step::Next;
        }
        std::string why;
        if (!screen->scriptHmiEditScript(arg(1), arg(2), &why)) fail("ihm-script-modifier : " + why);
        return Step::Yield;
    }
    if (cmd == "arbre-parcourir") {
        // Une fenetre ouverte par le noeud precedent : Annuler, puis on reprend.
        if (crawling_ && !dynamic_cast<MainAnalysisScreen*>(app_.menus().top())) {
            if (++crawlStuck_ > 60) {
                fail("arbre-parcourir : une fenetre ne se ferme pas, apres le noeud " + std::to_string(crawlAt_));
                crawl_.clear();
                crawling_ = false;
                crawlStuck_ = 0;
                return Step::Next;
            }
            app_.menus().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
            retries_ = 0;
            return Step::Retry;
        }
        crawlStuck_ = 0;
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) { fail(cmd + " : pas d'\xC3\xA9" "cran d'analyse"); return Step::Next; }
        if (!crawling_) {
            const auto from = screen->treeNodeOfPath(arg(1));
            if (from == ui::kInvalidNode) { fail("arbre-parcourir : introuvable : " + arg(1)); return Step::Next; }
            std::size_t cap = 4000;
            if (!arg(2).empty()) cap = static_cast<std::size_t>(std::max(1, std::atoi(arg(2).c_str())));
            const auto nodes = screen->treeSubtree(from, cap);
            crawl_.assign(nodes.begin(), nodes.end());
            crawlAt_ = 0;
            crawling_ = true;
            std::printf("[script] arbre-parcourir \"%s\" : %zu noeud(s)\n", arg(1).c_str(), crawl_.size());
        }
        for (int k = 0; k < 4 && crawlAt_ < crawl_.size(); ++k) {
            const auto n = static_cast<ui::NodeId>(crawl_[crawlAt_++]);
            const auto text = screen->visitTreeNode(n);
            if (crawlAt_ % 100 == 0 || crawlAt_ == crawl_.size())
                std::printf("[script] arbre-parcourir : %zu / %zu (%s)\n", crawlAt_, crawl_.size(), text.c_str());
            if (!dynamic_cast<MainAnalysisScreen*>(app_.menus().top())) break;   // une fenetre : l'image suivante
        }
        if (crawlAt_ < crawl_.size() || !dynamic_cast<MainAnalysisScreen*>(app_.menus().top())) {
            retries_ = 0;   // un long parcours n'est pas une attente
            return Step::Retry;
        }
        std::printf("[script] arbre-parcourir : fini, %zu noeud(s) visit\xC3\xA9(s), sans plantage\n", crawl_.size());
        crawl_.clear();
        crawling_ = false;
        return Step::Yield;
    }
    if (cmd == "arbre-epingler") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) { fail(cmd + " : pas d'\xC3\xA9" "cran d'analyse"); return Step::Next; }
        if (!screen->pinTreePath(arg(1), lower(arg(2)) != "non"))
            fail("arbre-epingler \"" + arg(1) + "\" : introuvable, ou rien n'a chang\xC3\xA9");
        return Step::Yield;
    }
    if (cmd == "arbre-filtre" || cmd == "arbre-lignes" || cmd == "arbre-versions-toutes") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) { fail(cmd + " : pas d'\xC3\xA9" "cran d'analyse"); return Step::Next; }
        if (cmd == "arbre-filtre") {
            screen->setTreeFilter(arg(1));
            return Step::Yield;
        }
        if (cmd == "arbre-versions-toutes") {
            screen->showAllTreeVersions(lower(arg(1)) != "non");
            return Step::Yield;
        }
        if (!screen->treeFilterText().empty())
            std::printf("[script] arbre : filtre \"%s\" : %zu r\xC3\xA9sultat(s)\n", screen->treeFilterText().c_str(),
                        screen->treeFilterCount());
        for (const auto& l : screen->treeLines()) std::printf("[script] arbre : %s\n", l.c_str());
        return Step::Next;
    }
    //  arbre "IHM/Compiler"   (alias) les outils sortis de l'arbre - API : Statistiques ; IHM : Exporter
    //                         (Echanges), Rechercher, Outil Modbus, Generer, Compiler - le bouton de la
    //                         rangee sous le titre du domaine, comme l'ancien noeud (outil-arbre "IHM/Compiler" aussi)
    if ((cmd == "arbre" || cmd == "outil-arbre") && w.size() > 1 && !has("deplier") && !has("replier") && !has("droit")) {
        const auto slash = arg(1).find('/');
        const auto first = lower(arg(1).substr(0, slash == std::string::npos ? 0 : slash));
        const std::string rest = slash == std::string::npos ? std::string{} : arg(1).substr(slash + 1);
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (screen && (first == "api" || first == "ihm") && rest.size() >= 3 && rest.find('/') == std::string::npos
            && screen->openTreeTool(first == "ihm", rest))
            return Step::Yield;
        if (cmd == "outil-arbre") { fail("outil-arbre : pas un outil de l'arbre : " + arg(1)); return Step::Next; }
    }
    //  2e partie :
    //  arbre-portee tout|epingles|api|ihm|simulation|versions   le rail : l'arbre ne montre que ce domaine
    //  arbre-densite serre|large|normal   la hauteur des lignes (22 / 26 px / celle du theme), retenue
    //  arbre-replier          tout replier (et effacer le filtre)
    //  arbre-suivre [non]     suivre l'onglet actif (non : l'arreter), retenu
    //  arbre-sante [simulation|erreurs|bibliotheque|versions]   ecrit la sante du projet (le pied) ; un mot : clique ce morceau
    //  arbre-carte            ecrit la carte du noeud choisi (celle du survol)
    //  arbre-action epingler|detacher|plus   une action au survol, sur le noeud choisi
    if (cmd == "arbre-portee" || cmd == "arbre-densite" || cmd == "arbre-replier" || cmd == "arbre-suivre"
        || cmd == "arbre-sante" || cmd == "arbre-carte" || cmd == "arbre-action") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen) { fail(cmd + " : pas d'\xC3\xA9" "cran d'analyse"); return Step::Next; }
        if (cmd == "arbre-portee") {
            if (!screen->setTreeScope(lower(arg(1)))) fail("arbre-portee : tout, epingles, api, ihm, simulation ou versions (\"" + arg(1) + "\")");
            return Step::Yield;
        }
        if (cmd == "arbre-densite") {
            if (!screen->setTreeDensity(lower(arg(1)))) fail("arbre-densite : serre, large ou normal (\"" + arg(1) + "\")");
            return Step::Yield;
        }
        if (cmd == "arbre-replier") { screen->collapseTree(); return Step::Yield; }
        if (cmd == "arbre-suivre") { screen->setTreeFollow(lower(arg(1)) != "non"); return Step::Yield; }
        if (cmd == "arbre-sante") {
            screen->refreshTreeState();
            std::printf("[script] arbre : sant\xC3\xA9 : %s\n", screen->treeHealthText().c_str());
            if (!arg(1).empty()) { screen->openTreeHealthPart(lower(arg(1))); return Step::Yield; }
            return Step::Next;
        }
        if (cmd == "arbre-carte") {
            std::printf("[script] arbre : carte : %s\n", screen->treeCurrentCard().c_str());
            return Step::Next;
        }
        const auto a = lower(arg(1));
        const std::size_t action = a == "epingler" ? 0u : a == "detacher" ? 1u : a == "plus" ? 2u : 9u;
        if (action > 2 || !screen->treeActionOnCurrent(action)) fail("arbre-action : epingler, detacher ou plus, sur un noeud choisi (\"" + arg(1) + "\")");
        return Step::Yield;
    }
    // ---- fin Lot API 8 : l'arbre du projet ----
    // ---- Lot API 8 : Centre de simulation ----
    //  Les anciens chemins : API > Simulation est devenu Simulation > Automate,
    //  IHM > Simulation Simulation > IHM ("API/Simulation" designerait sinon le
    //  dossier Simulation de la racine, la Vue d'ensemble).
    if (cmd == "arbre" && w.size() > 1) {
        const auto slash = arg(1).find('/');
        if (slash != std::string::npos) {
            const auto first = lower(arg(1).substr(0, slash));
            const auto next = arg(1).find('/', slash + 1);
            const auto second = lower(arg(1).substr(slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1));
            if ((first == "api" || first == "ihm") && second == "simulation") {
                auto again = w;
                again[1] = std::string(first == "api" ? "Simulation/Automate" : "Simulation/IHM")
                         + (next == std::string::npos ? std::string{} : arg(1).substr(next));
                return run(again, renderer);
            }
        }
    }
    // ---- fin Lot API 8 : Centre de simulation ----
    if (cmd == "arbre") {
        auto* tree = dynamic_cast<ui::TreeView*>(root->findById("analysis.explorer"));
        if (!tree || !tree->model()) { fail("explorateur introuvable"); return Step::Next; }
        // "IHM/Vues/Vue_Production" : chaque morceau est cherche APRES le
        // precedent, dans l'ordre des lignes - assez pour designer un enfant
        // quand deux dossiers portent le meme nom.
        std::vector<std::string> parts;
        for (std::size_t from = 0;;) {
            const auto slash = arg(1).find('/', from);
            parts.push_back(treeAlias(arg(1).substr(from, slash == std::string::npos ? std::string::npos : slash - from)));
            if (slash == std::string::npos) break;
            from = slash + 1;
        }
        // ---- Lot API 8 : l'arbre du projet (un chemin ne passe pas par Epingles / Recents, en tete) ----
        auto nodes = tree->visibleNodes();
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](ui::NodeId n) {
                        using TK = ProjectTreeModel::NodeKind;
                        const auto k = ProjectTreeModel::kindOf(n);
                        return k == TK::PinsFolder || k == TK::PinItem || k == TK::RecentFolder || k == TK::RecentItem;
                    }), nodes.end());
        // ---- fin Lot API 8 : l'arbre du projet ----
        std::size_t at = 0, part = 0, lastMatch = 0;
        for (; at < nodes.size(); ++at) {
            if (!startsWith(tree->model()->text(nodes[at]), parts[part])) continue;
            lastMatch = at;
            if (++part == parts.size()) break;
        }
        // Lot 8 : un morceau qui n'est pas visible est cherche SOUS le precedent,
        // dans des dossiers replies (Vues > Vues > Vue_Production s'ecrit encore
        // "IHM/Vues/Vue_Production") ; ses dossiers se deplient.
        if (at >= nodes.size() && part > 0 && part < parts.size()) {
            ui::NodeId node = nodes[lastMatch];
            const auto* model = tree->model().get();
            bool ok = true;
            for (std::size_t k = part; k < parts.size() && ok; ++k) {
                // En largeur d'abord, trois niveaux au plus : le plus proche gagne.
                std::vector<std::pair<ui::NodeId, std::vector<ui::NodeId>>> level{{node, {}}};
                std::vector<ui::NodeId> path;
                bool hit = false;
                for (int depth = 0; depth < 3 && !hit; ++depth) {
                    std::vector<std::pair<ui::NodeId, std::vector<ui::NodeId>>> next;
                    for (const auto& [n, chain] : level) {
                        for (std::size_t c = 0; c < model->childCount(n) && !hit; ++c) {
                            const auto child = model->childAt(n, c);
                            auto childChain = chain;
                            childChain.push_back(n);
                            if (startsWith(model->text(child), parts[k])) { node = child; path = childChain; hit = true; break; }
                            next.emplace_back(child, std::move(childChain));
                        }
                        if (hit) break;
                    }
                    level = std::move(next);
                }
                if (!hit) { ok = false; break; }
                for (const auto n : path) tree->expand(n);
            }
            if (ok) {
                if (has("deplier") || has("replier")) {
                    if (has("deplier")) tree->expand(node);
                    else tree->collapse(node);
                    return Step::Yield;
                }
                tree->ensureVisible(node);
                gfx::Rect r;
                if (!tree->rowRect(node, r)) return Step::Retry;
                const gfx::Point p{r.right() - 30.f, r.y + r.h * 0.5f};
                click(p, has("droit") ? MouseButton::Right : MouseButton::Left, 1, {});   // lot API 3 : "droit", le menu du clic droit
                if (has("double")) click(p, MouseButton::Left, 2, {});
                return Step::Yield;
            }
        }
        if (at < nodes.size()) {
            const auto node = nodes[at];
            if (has("deplier") || has("replier")) {   // le chevron : sans ouvrir
                if (has("deplier")) tree->expand(node);
                else tree->collapse(node);
                return Step::Yield;
            }
            tree->ensureVisible(node);
            gfx::Rect r;
            if (!tree->rowRect(node, r)) return Step::Retry;     // le temps d'un dessin
            const gfx::Point p{r.right() - 30.f, r.y + r.h * 0.5f};
            click(p, has("droit") ? MouseButton::Right : MouseButton::Left, 1, {});
            if (has("double")) click(p, MouseButton::Left, 2, {});
            return Step::Yield;
        }
        fail("ligne introuvable dans l'arbre : " + arg(1));
        return Step::Next;
    }

    // ---- lot macros 1 : l'onglet Macros, son formulaire, l'explorateur -------------
    //
    //   explorateur "C:/Affaires/affaire.xlsm"   la prochaine reponse de l'explorateur
    //                                            (le bouton ... ne l'ouvre pas)
    //   presse-papiers-fichiers "a.csv" ["b.csv"]  des fichiers copies dans l'Explorateur
    //   macros-arbre "Dossier/Macro" [double|droit]  une ligne de la liste, comme un clic
    //   macros-glisser "Dossier/Macro" "Autre dossier"  la glisser dessus (le rangement)
    //   macro-lancer "Macro"                     le formulaire (comme Entree)
    //   macro-champ "cle" "valeur"               une reponse, comme la souris
    //   macro-parcourir "cle"                    le bouton ... d'un champ de fichier
    //   macro-coller "cle"                       Ctrl+V dans un champ de fichier
    //   macro-deposer "chemin" "cle"             un fichier lache sur le champ
    //   macro-voir "cle"                         fait defiler le formulaire jusqu'au champ
    //   macro-etape apercu|retour|appliquer|fermer
    //   attendre-macro                           l'apercu a fini de tourner
    if (cmd == "explorateur") {
        ui::queueFilePick(arg(1));
        return Step::Next;
    }
    // ---- Lot API 8 : l'explorateur de fichiers de l'appli ----
    //  explorateur-montrer               la prochaine demande (un bouton ..., Ouvrir) ouvre l'explorateur
    //                                    de l'appli, meme dans une session (sans lui : celui du systeme,
    //                                    et `explorateur "chemin"` repond toujours sans rien ouvrir)
    //  explorateur-aller "D:/Affaires"   ouvre ce dossier ("" : Ce PC ; un fichier : son dossier, lui choisi)
    //  explorateur-selectionner "MAST.XPG"  un element de la liste (sans casse ; un debut qui suffit)
    //  explorateur-filtre "Classeurs"    un type de la liste par le debut de son nom
    //  explorateur-vue details|vignettes la vue
    //  explorateur-chercher "texte"      le champ Chercher (filtre le dossier en direct)
    //  explorateur-nom "rapport.csv"     le champ Nom du fichier
    //  explorateur-valider               le bouton principal (Ouvrir, Enregistrer, Remplacer, Choisir)
    //  explorateur-annuler               Annuler (la demande recoit "")
    //  explorateur-lignes                ce que montre la liste, une ligne par element (et l'etat)
    if (cmd == "explorateur-montrer") {
        explorer::showNextInScript();
        return Step::Next;
    }
    //  explorateur-systeme oui|non       le reglage : celui du systeme (oui) ou celui de l'appli (non, le defaut)
    if (cmd == "explorateur-systeme") {
        explorer::setSystemPreferred(app_.settings(), lower(arg(1)) != "non");
        return Step::Next;
    }
    if (cmd == "explorateur-aller" || cmd == "explorateur-selectionner" || cmd == "explorateur-filtre" || cmd == "explorateur-vue"
        || cmd == "explorateur-chercher" || cmd == "explorateur-nom" || cmd == "explorateur-valider" || cmd == "explorateur-annuler"
        || cmd == "explorateur-lignes") {
        auto* dialog = explorer::current(app_);
        if (!dialog) {
            // L'explorateur s'ouvre a l'image suivante (la demande passe par la pile).
            if (retries_ < 30) return Step::Retry;
            fail(cmd + " : pas d'explorateur de fichiers ouvert (explorateur-montrer avant la demande)");
            return Step::Next;
        }
        std::string why;
        if (cmd == "explorateur-aller") {
            if (!dialog->navigate(arg(1), &why)) fail("explorateur-aller \"" + arg(1) + "\" : " + why);
        } else if (cmd == "explorateur-selectionner") {
            if (!dialog->select(arg(1), &why)) fail("explorateur-selectionner \"" + arg(1) + "\" : " + why);
        } else if (cmd == "explorateur-filtre") {
            if (!dialog->setFilter(arg(1), &why)) fail("explorateur-filtre \"" + arg(1) + "\" : " + why);
        } else if (cmd == "explorateur-vue") {
            const auto v = lower(arg(1));
            if (v.rfind("vig", 0) == 0) dialog->setThumbnails(true);
            else if (v.rfind("det", 0) == 0 || v.rfind("d\xC3\xA9t", 0) == 0) dialog->setThumbnails(false);
            else fail("explorateur-vue : details ou vignettes");
        } else if (cmd == "explorateur-chercher") {
            dialog->setSearch(arg(1));
        } else if (cmd == "explorateur-nom") {
            dialog->setFileName(arg(1));
        } else if (cmd == "explorateur-valider") {
            if (!dialog->accept(&why)) fail("explorateur-valider : " + why);
        } else if (cmd == "explorateur-annuler") {
            dialog->cancel();
        } else if (cmd == "explorateur-lignes") {
            std::printf("[script] explorateur : %s | %s\n", dialog->folder().c_str(), dialog->statusText().c_str());
            for (const auto& l : dialog->lines()) std::printf("[script] explorateur : %s\n", l.c_str());
            if (!dialog->message().empty()) std::printf("[script] explorateur : message : %s\n", dialog->message().c_str());
            std::printf("[script] explorateur : bouton : %s\n", dialog->primaryLabel().c_str());
        } else {
            fail("commande inconnue : " + cmd);
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : l'explorateur de fichiers ----
    // ---- Lot API 8 : l'explorateur, 2e partie (l'apercu) ----
    //  explorateur-apercu                ecrit ce que montre l'apercu de l'element choisi (calcule tout de suite)
    //  explorateur-apercu replier|montrer  replie ou montre la colonne Apercu, puis l'ecrit
    if (cmd == "explorateur-apercu") {
        auto* dialog = explorer::current(app_);
        if (!dialog) {
            if (retries_ < 30) return Step::Retry;
            fail(cmd + " : pas d'explorateur de fichiers ouvert (explorateur-montrer avant la demande)");
            return Step::Next;
        }
        const auto how = lower(arg(1));
        if (how.rfind("rep", 0) == 0) dialog->setPreviewShown(false);
        else if (how.rfind("mon", 0) == 0) dialog->setPreviewShown(true);
        else if (!how.empty()) fail("explorateur-apercu : replier ou montrer (ou rien)");
        for (const auto& l : dialog->previewLines()) std::printf("[script] explorateur : apercu : %s\n", l.c_str());
        sinceInput_ = 0;
        return Step::Yield;
    }
    //  explorateur-menu "Copier le chemin"  une entree du menu du clic droit de l'element choisi (Epingler,
    //                                    Desepingler, Copier le chemin ; "Ouvrir..." est seulement ecrit : rien ne se lance)
    //  explorateur-lien-systeme          le lien du bas "Utiliser l'explorateur du systeme" (explorateur.systeme = oui)
    //  explorateur-sous-dossiers oui|non la case "et ses sous-dossiers" de Chercher
    if (cmd == "explorateur-menu" || cmd == "explorateur-lien-systeme" || cmd == "explorateur-sous-dossiers") {
        auto* dialog = explorer::current(app_);
        if (!dialog) {
            if (retries_ < 30) return Step::Retry;
            fail(cmd + " : pas d'explorateur de fichiers ouvert (explorateur-montrer avant la demande)");
            return Step::Next;
        }
        std::string why;
        if (cmd == "explorateur-lien-systeme") {
            if (!dialog->chooseSystemExplorer(&why)) fail(cmd + " : " + why);
        } else if (cmd == "explorateur-sous-dossiers") {
            dialog->setSearchSubfolders(lower(arg(1)) != "non");
        } else if (lower(arg(1)).rfind("ouvrir", 0) == 0) {
            std::printf("[script] explorateur : menu : %s (non lance dans une session)\n", arg(1).c_str());
        } else if (!dialog->rowMenuChoose(arg(1), &why)) {
            fail("explorateur-menu \"" + arg(1) + "\" : " + why);
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : l'explorateur, 2e partie ----
    // Le bouton ... d'un champ de chemin (ui::BrowseButton) de l'ecran du dessus :
    // un formulaire (Nouveau projet, Exporter en CSV...), Ouvrir un projet, les
    // forcages de la simulation. La reponse de l'explorateur se range AVANT
    // (explorateur "chemin") : sinon, le vrai s'ouvre.
    //   parcourir "Fichier CSV"     par le libelle du champ (ou son debut)
    //   parcourir 2                 le 2e bouton ... visible ; sans argument : le premier
    if (cmd == "parcourir") {
        std::vector<ui::BrowseButton*> buttons;
        walk(*root, [&](ui::Widget& x) {
            if (auto* b = dynamic_cast<ui::BrowseButton*>(&x); b && shown(*b)) buttons.push_back(b);
        });
        ui::BrowseButton* target = nullptr;
        if (arg(1).empty()) {
            if (!buttons.empty()) target = buttons.front();
        } else if (isNumber(arg(1))) {
            const auto n = static_cast<std::size_t>(std::max(1, std::atoi(arg(1).c_str())));
            if (n <= buttons.size()) target = buttons[n - 1];
        } else {
            for (auto* b : buttons)
                if (!target && lower(b->fieldLabel()) == lower(arg(1))) target = b;
            for (auto* b : buttons)
                if (!target && (startsWith(b->fieldLabel(), arg(1)) || startsWith(b->text(), arg(1)))) target = b;
        }
        if (!target) { fail("bouton \xE2\x80\xA6 introuvable : " + arg(1)); return Step::Next; }
        if (target->bounds().w <= 0.f) return Step::Retry;     // le temps d'un dessin
        if (!target->enabled()) { fail("bouton \xE2\x80\xA6 inactif : " + arg(1)); return Step::Next; }
        click(centre(target->bounds()), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "presse-papiers-fichiers") {
        ui::setScriptedClipboardFiles(std::vector<std::string>(w.begin() + 1, w.end()));
        return Step::Next;
    }
    // Utiliser / Modifier, dans l'onglet Macros.
    if (cmd == "macros-mode") {
        MacrosPane* pane = nullptr;
        if (auto* page = currentPage()) walk(*page, [&](ui::Widget& x) { if (!pane) pane = dynamic_cast<MacrosPane*>(&x); });
        if (!pane) { fail("l'onglet Macros n'est pas l'onglet courant"); return Step::Next; }
        auto& seg = pane->modeSwitch();
        const int want = arg(1) == "modifier" ? 1 : 0;
        const auto r = seg.optionRect(static_cast<std::size_t>(want));
        if (!shown(seg) || r.w <= 0.f) { fail("Utiliser / Modifier n'est pas affiche"); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd.rfind("macro", 0) == 0 || cmd == "attendre-macro") {
        auto* pane = dynamic_cast<MacrosPane*>(currentPage());
        if (!pane) { fail("l'onglet Macros n'est pas l'onglet courant"); return Step::Next; }
        auto* form = pane->form();
        if (cmd == "attendre-macro") return pane->busy() ? Step::Retry : Step::Next;
        if (cmd == "macros-arbre") {
            const auto node = pane->nodeOf(arg(1));
            if (node == ui::kInvalidNode) { fail("macro ou dossier introuvable : " + arg(1)); return Step::Next; }
            // Les dossiers qui la contiennent se deplient.
            for (std::size_t at = arg(1).find('/'); at != std::string::npos; at = arg(1).find('/', at + 1))
                if (const auto up = pane->nodeOf(arg(1).substr(0, at)); up != ui::kInvalidNode) pane->tree().expand(up);
            if (const auto f = pane->layout().folderOf(arg(1)); !f.empty())
                for (std::size_t at = 0; at != std::string::npos;) {
                    at = f.find('/', at + 1);
                    if (const auto up = pane->nodeOf(f.substr(0, at)); up != ui::kInvalidNode) pane->tree().expand(up);
                }
            pane->tree().ensureVisible(node);
            gfx::Rect r;
            if (!pane->tree().rowRect(node, r)) return Step::Retry;     // le temps d'un dessin
            const gfx::Point p{r.x + std::min(r.w * 0.5f, 150.f), r.y + r.h * 0.5f};
            click(p, has("droit") ? MouseButton::Right : MouseButton::Left, 1, mods());
            if (has("double")) click(p, MouseButton::Left, 2, {});
            return Step::Yield;
        }
        if (cmd == "macros-glisser") {
            // Une macro (ou un dossier) lachee sur un dossier, ou entre deux lignes.
            const auto from = pane->nodeOf(arg(1)), to = pane->nodeOf(arg(2));
            if (from == ui::kInvalidNode || to == ui::kInvalidNode) {
                fail("macros-glisser : introuvable : " + (from == ui::kInvalidNode ? arg(1) : arg(2)));
                return Step::Next;
            }
            gfx::Rect a, b;
            pane->tree().ensureVisible(from);
            if (!pane->tree().rowRect(from, a) || !pane->tree().rowRect(to, b)) return Step::Retry;
            drag({a.x + std::min(a.w * 0.5f, 150.f), a.y + a.h * 0.5f}, {b.x + std::min(b.w * 0.5f, 150.f), b.y + b.h * 0.5f}, mods());
            return Step::Yield;
        }
        if (cmd == "macro-lancer") {
            pane->launch(arg(1));
            sinceInput_ = 0;
            return Step::Yield;
        }
        if (cmd == "macro-etape") {
            const auto e = arg(1);
            if (e == "apercu") pane->goPreview();
            else if (e == "retour") pane->goBack();
            else if (e == "appliquer") { if (!pane->applyNow()) fail("Appliquer refuse : " + pane->lastMessage()); }
            else if (e == "fermer") pane->closeRun();
            else { fail("etape inconnue : " + e); return Step::Next; }
            sinceInput_ = 0;
            return Step::Yield;
        }
        if (!form) { fail("pas de formulaire ouvert"); return Step::Next; }
        if (cmd == "macro-champ") {
            if (!pane->answer(arg(1), arg(2))) fail("champ introuvable : " + arg(1));
            sinceInput_ = 0;
            return Step::Yield;
        }
        if (cmd == "macro-voir") {
            form->ensureVisible(arg(1));
            sinceInput_ = 0;
            return Step::Yield;
        }
        if (cmd == "macro-parcourir") {
            form->ensureVisible(arg(1));
            if (!form->browse(arg(1))) fail("pas de bouton ... pour " + arg(1));
            sinceInput_ = 0;
            return Step::Yield;
        }
        if (cmd == "macro-coller" || cmd == "macro-deposer") {
            const auto key = cmd == "macro-coller" ? arg(1) : arg(2);
            form->ensureVisible(key);
            auto* control = form->controlOf(key);
            if (!control) { fail("champ introuvable : " + key); return Step::Next; }
            const auto r = control->bounds();
            if (r.w <= 0.f) return Step::Retry;
            const gfx::Point p{r.x + std::min(r.w * 0.5f, 120.f), r.y + r.h * 0.5f};
            if (cmd == "macro-deposer") {
                moveTo(p, {});
                send(ui::FileDropped{arg(1), p});
            } else {
                click(p, MouseButton::Left, 1, {});
                KeyMods ctrl;
                ctrl.ctrl = true;
                send(ui::KeyDown{Key::V, ctrl, false});
                send(ui::KeyUp{Key::V, ctrl});
            }
            sinceInput_ = 0;
            return Step::Yield;
        }
        fail("commande inconnue : " + cmd);
        return Step::Next;
    }

    // Lot recherche : LES FILTRES DES COLONNES d'un tableau de l'onglet courant
    // (le premier montre qui a une colonne de ce titre - sans casse ni accents,
    // par son debut) :
    //   filtre-colonne "Type" "=" "BOOL"            contient, commence, =, different,
    //                                               vide, non-vide, entre ("1..5", ou "1" "5"),
    //                                               valeurs "BOOL;INT", sauf "BOOL;INT"
    //   filtre-colonne "Commentaire" ouvrir         la fenetre du filtre (pour une capture)
    //   filtre-effacer ["Type"]                     tous les filtres (ou ceux de cette colonne)
    if (cmd == "filtre-colonne" || cmd == "filtre-effacer") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        if (cmd == "filtre-effacer") {
            bool any = false;
            walk(*page, [&](ui::Widget& x) {
                auto* t = dynamic_cast<ui::TableView*>(&x);
                if (!t || !shown(*t)) return;
                if (auto* pop = t->columnFilterPopup(); pop && pop->isOpen()) pop->close();
                if (t->columnFilters().empty()) return;
                any = true;
                if (arg(1).empty()) t->clearColumnFilters();
                else if (const int c = t->columnByTitle(arg(1)); c >= 0) t->removeColumnFilter(static_cast<std::size_t>(c));
            });
            if (!any) std::printf("[script] filtre-effacer : aucun filtre de colonne\n");
            return Step::Yield;
        }
        ui::TableView* table = nullptr;
        int col = -1;
        walk(*page, [&](ui::Widget& x) {
            auto* t = dynamic_cast<ui::TableView*>(&x);
            if (table || !t || !shown(*t) || !t->model()) return;
            if (const int c = t->columnByTitle(arg(1)); c >= 0) {
                table = t;
                col = c;
            }
        });
        if (!table) { fail("filtre-colonne : pas de colonne \xC2\xAB " + arg(1) + " \xC2\xBB dans l'onglet"); return Step::Next; }
        const std::string cond = ui::foldForSearch(arg(2));
        if (cond == "ouvrir") {
            if (!table->openColumnFilter(static_cast<std::size_t>(col))) fail("filtre-colonne : la fen\xC3\xAAtre ne s'ouvre pas");
            return Step::Yield;
        }
        using Op = ui::ColumnFilter::Op;
        ui::ColumnFilter f;
        f.column = static_cast<std::size_t>(col);
        f.value = arg(3);
        const auto list = [&](ui::ColumnFilter::List kind) {
            f.list = kind;
            f.value.clear();
            std::string part;
            for (const char ch : arg(3) + ";") {
                if (ch != ';') { part += ch; continue; }
                f.values.push_back(part == "(vide)" ? std::string{} : part);
                part.clear();
            }
        };
        if (cond == "contient") f.op = Op::Contains;
        else if (cond == "commence" || cond == "commence par" || cond == "commence-par") f.op = Op::StartsWith;
        else if (cond == "=" || cond == "egal" || cond == "egale") f.op = Op::Equals;
        else if (cond == "!=" || cond == "<>" || cond == "different" || cond == "different de" || cond == "different-de") f.op = Op::NotEquals;
        else if (cond == "vide") f.op = Op::Empty;
        else if (cond == "non vide" || cond == "non-vide") f.op = Op::NotEmpty;
        else if (cond == "entre") {
            f.op = Op::Between;
            if (const auto dots = arg(3).find(".."); dots != std::string::npos) {
                f.value = arg(3).substr(0, dots);
                f.value2 = arg(3).substr(dots + 2);
            } else {
                f.value2 = arg(4);
            }
        } else if (cond == "valeurs" || cond == "parmi") list(ui::ColumnFilter::List::Only);
        else if (cond == "sauf") list(ui::ColumnFilter::List::Except);
        else { fail("filtre-colonne : condition inconnue \xC2\xAB " + arg(2) + " \xC2\xBB (contient, commence, =, different, vide, non-vide, entre, valeurs, sauf, ouvrir)"); return Step::Next; }
        if (f.op == Op::Empty || f.op == Op::NotEmpty) f.value.clear();
        if (!f.active()) { fail("filtre-colonne : il manque la valeur"); return Step::Next; }
        table->setColumnFilter(std::move(f));
        std::printf("[script] filtre-colonne : %zu ligne(s) montr\xC3\xA9" "e(s)\n", table->visibleRowCount());
        return Step::Yield;
    }
    // Lot recherche : ALLER A... (Ctrl+K), tape comme au clavier.
    //   aller-a "pompe"              ouvre le panneau, tape, attend les resultats (le
    //                                delai de frappe compris)
    //   aller-a "pompe" entree       ... puis Entree : le premier resultat ; "3" : le troisieme
    //   aller-a-categorie "Code"     la categorie seule (par le debut de sa pastille) ; "Tout"
    if (cmd == "aller-a" || cmd == "aller-a-categorie") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        auto* go = screen ? screen->goToPanel(true) : nullptr;
        if (!go) { fail(cmd + " : l'\xC3\xA9" "cran d'analyse n'est pas au-dessus"); return Step::Next; }
        if (cmd == "aller-a-categorie") {
            const auto want = ui::foldForSearch(arg(1));
            if (want.empty() || want == "tout") { go->setCategory(-1); return Step::Yield; }
            for (const auto& c : go->categories())
                if (ui::foldForSearch(c.chip).rfind(want, 0) == 0) {
                    go->setCategory(c.group);
                    return Step::Yield;
                }
            fail("aller-a-categorie : pas de cat\xC3\xA9gorie \xC2\xAB " + arg(1) + " \xC2\xBB");
            return Step::Next;
        }
        if (retries_ == 0) {
            // Le texte d'avant efface, puis tape : la recherche attend la fin de la frappe.
            if (!go->text().empty()) go->setText("");
            send(ui::TextInput{arg(1)});
            return Step::Retry;
        }
        if (go->pending() && retries_ < 240) return Step::Retry;
        go->flush();
        std::printf("[script] aller-a \"%s\" : %zu r\xC3\xA9sultat(s), %zu cat\xC3\xA9gorie(s)\n", arg(1).c_str(), go->results().size(),
                    go->categories().size());
        const bool enter = has("entree") || has("entr\xC3\xA9" "e");
        if (enter || isNumber(arg(2))) {
            if (go->results().empty()) { fail("aller-a : rien ne contient \xC2\xAB " + arg(1) + " \xC2\xBB"); return Step::Next; }
            const int index = isNumber(arg(2)) ? std::max(1, std::atoi(arg(2).c_str())) - 1 : go->current();
            if (index >= static_cast<int>(go->results().size())) { fail("aller-a : pas de r\xC3\xA9sultat " + arg(2)); return Step::Next; }
            go->activate(index);
        }
        return Step::Yield;
    }
    // Lot API 5 : puce "Pas utilis" - la pastille d'un filtre de l'onglet courant
    // (Types derives, Variables), cliquee a la souris.
    if (cmd == "puce") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect r{};
        walk(*page, [&](ui::Widget& x) {
            auto* bar = dynamic_cast<ApiFilterBar*>(&x);
            if (r.w > 0.f || !bar || !shown(*bar)) return;
            // Le libelle : on demande a la barre (sans la casse, par le debut).
            const auto before = bar->current();
            if (bar->chooseChip(arg(1))) {
                r = bar->chipRect(bar->current());
                // Remise comme avant : c'est le clic qui choisit (et se voit).
                if (bar->current() != before) bar->setCurrent(before);
            }
        });
        if (r.w <= 0.f) { fail("pastille introuvable : " + arg(1)); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // ---- Lot API 8 : les filtres retenus, les champs de recherche ----
    //   chercher "gaz"                     tape la recherche de l'onglet courant (la premiere montree : Recettes,
    //                                      Utilisateurs, Styles, Sous-routines, Taches, Statistiques...) ; "" l'efface
    //   filtres-retenus ["Type = BOOL"]    ecrit au journal ce qui est retenu (et montre) pour les tableaux et les
    //                                      recherches de l'onglet courant ; un texte : echoue si rien ne le contient
    if (cmd == "chercher") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        ApiFilterBar* bar = nullptr;
        ui::SearchField* field = nullptr;
        walk(*page, [&](ui::Widget& x) {
            if (bar || field || !shown(x)) return;
            if (auto* b = dynamic_cast<ApiFilterBar*>(&x)) bar = b;
            else if (auto* f = dynamic_cast<ui::SearchField*>(&x)) field = f;
        });
        if (!bar && !field) {
            if (retries_ < 30) return Step::Retry;      // l'onglet se met en place
            fail("chercher : pas de champ de recherche dans l'onglet");
            return Step::Next;
        }
        if ((bar ? bar->search() : field->text()) != arg(1)) {
            if (retries_ >= 60) { fail("chercher : le texte ne se pose pas"); return Step::Next; }
            if (bar) bar->setSearch(arg(1));
            else field->setText(arg(1));
            return Step::Retry;      // le compte : a l'image suivante (le volet a filtre)
        }
        const std::string count = bar ? bar->countText() : field->countText();
        std::printf("[script] chercher \xC2\xAB %s \xC2\xBB : %s\n", arg(1).c_str(), count.empty() ? "(pas de compte)" : count.c_str());
        return Step::Yield;
    }
    if (cmd == "filtres-retenus") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        std::vector<std::string> lines;
        walk(*page, [&](ui::Widget& x) {
            if (!shown(x)) return;
            std::string now;
            if (auto* t = dynamic_cast<ui::TableView*>(&x)) {
                if (!t->columnFiltersEnabled() || !t->remembersColumnFilters()) return;
                std::vector<ui::FilterMemory::SavedFilter> shownFilters;
                for (const auto& f : t->columnFilters())
                    if (f.column < t->columns().size()) shownFilters.push_back({t->columns()[f.column].title, f});
                now = ui::FilterMemory::describe(ui::FilterMemory::encodeColumns(shownFilters));
            } else if (auto* b = dynamic_cast<ApiFilterBar*>(&x)) {
                // La premiere pastille (Toutes, Tous) : rien a dire, comme ce qui est retenu.
                now = ui::FilterMemory::describe(ui::FilterMemory::encodeSearch(b->search(), b->current() > 0 ? b->currentChip() : std::string{}));
            } else if (auto* in = dynamic_cast<ui::InputText*>(&x)) {
                now = ui::FilterMemory::describe(ui::FilterMemory::encodeSearch(in->text(), {}));
            } else {
                return;
            }
            const std::string stored = ui::FilterMemory::read(x.id());
            // Un champ quelconque (pas une recherche retenue) : rien a dire.
            if (stored.empty() && dynamic_cast<ui::InputText*>(&x)) return;
            lines.push_back(x.id() + " : retenu " + (stored.empty() ? std::string("(rien)") : ui::FilterMemory::describe(stored))
                            + " ; montr\xC3\xA9 " + (now.empty() ? std::string("(rien)") : now));
        });
        const auto want = ui::foldForSearch(arg(1));
        const bool found = want.empty() || std::any_of(lines.begin(), lines.end(), [&](const std::string& l) {
                               return ui::foldForSearch(l).find(want) != std::string::npos;
                           });
        if (!found && retries_ < 10) return Step::Retry;      // l'onglet vient de s'ouvrir : le temps d'une mise en page
        if (lines.empty()) std::printf("[script] filtres-retenus : ni tableau filtrable ni recherche retenue dans l'onglet\n");
        for (const auto& l : lines) std::printf("[script] filtres-retenus : %s\n", l.c_str());
        if (!found) fail("filtres-retenus : rien de retenu ne contient \xC2\xAB " + arg(1) + " \xC2\xBB");
        return Step::Yield;
    }
    // ---- fin Lot API 8 : les filtres retenus ----
    // ---- Lot API 8 : finitions (Ctrl+F dans les volets) ----
    //   touche ctrl+f                      le curseur dans le champ de recherche montre de l'onglet courant
    //                                      (fenetre-touche "Titre" ctrl+f : dans une fenetre detachee) ; Echap l'efface
    //   champ-actif ["id" | aucun]         ecrit au journal le champ qui a le curseur (ecran du dessus, puis les
    //                                      fenetres detachees) ; un id (ou sa fin) : echoue si ce n'est pas lui
    if (cmd == "champ-actif") {
        ui::InputText* active = nullptr;
        const auto look = [&](ui::Widget& from) {
            walk(from, [&](ui::Widget& x) {
                if (auto* t = dynamic_cast<ui::InputText*>(&x); !active && t && t->focused() && shown(*t)) active = t;
            });
        };
        if (auto* r = top()) look(*r);
        for (const auto& title : app_.detachedWindows().titles())
            if (auto* page = active ? nullptr : app_.detachedWindows().pageByTitle(title)) look(*page);
        const std::string id = active ? active->id() : std::string("aucun");
        const std::string& want = arg(1);
        const bool ok = want.empty() || id == want
                        || (want.size() < id.size() && id.compare(id.size() - want.size(), want.size(), want) == 0);
        if (!ok && retries_ < 10) return Step::Retry;
        const std::string shownText = active ? " \xC2\xAB " + active->text() + " \xC2\xBB" : std::string();
        std::printf("[script] champ-actif : %s%s\n", id.c_str(), shownText.c_str());
        std::fflush(stdout);
        if (!ok) fail("champ-actif : " + id + " au lieu de " + want);
        return Step::Next;
    }
    // ---- fin Lot API 8 : finitions ----
    // ---- l'onglet IHM courant ----------------------------------------------
    // 1.10.1 (chantier U2) : la fenetre "Ajouter un operateur" - un clic sur une
    // pastille du genre ("genre +"), de l'operande ou de la cible ("operande REAL"),
    // sur "Creer et ouvrir le script" ("creer"), sur "l'ouvrir" ("ouvrir") ; "annuler".
    if (cmd == "operateur-fenetre") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        HmiOperatorDialog* dialog = nullptr;
        walk(*page, [&](ui::Widget& x) {
            auto* d = dynamic_cast<HmiOperatorDialog*>(&x);
            if (!dialog && d && d->isOpen()) dialog = d;
        });
        if (!dialog) { fail("la fen\xC3\xAAtre Ajouter un op\xC3\xA9rateur n'est pas ouverte"); return Step::Next; }
        gfx::Rect r{};
        const std::string what = arg(1);
        bool ok = false;
        if (what == "genre") ok = dialog->kindRect(arg(2), r);
        else if (what == "operande") ok = dialog->choiceRect(arg(2), r);
        else if (what == "creer") { r = dialog->createRect(); ok = r.w > 0.f; }
        else if (what == "annuler") { r = dialog->cancelRect(); ok = r.w > 0.f; }
        else if (what == "ouvrir") { r = dialog->openLinkRect(); ok = r.w > 0.f; }
        if (!ok) { fail("operateur-fenetre : introuvable : " + what + " " + arg(2)); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // 1.11.14 : le panneau du bas (Sorties, Console, Diagnostics), hors des onglets du centre :
    // outil et ligne y cherchent aussi, apres l'onglet courant.
    const auto bottomPanel = [&]() -> ui::Widget* {
        ui::Widget* found = nullptr;
        if (auto* root = top())
            walk(*root, [&](ui::Widget& x) {
                if (!found && x.id() == "hmi.sorties" && shown(x)) found = &x;
            });
        return found;
    };
    if (cmd == "outil") {
        auto* page = currentPage();
        auto* panel = bottomPanel();
        if (!page && !panel) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect r{};
        bool ok = false;
        const auto look = [&](ui::Widget& x) {
            auto* strip = dynamic_cast<HmiToolStrip*>(&x);
            if (ok || !strip || !shown(*strip)) return;
            const int a = strip->actionByTip(arg(1));
            if (a >= 0) { r = strip->rectOf(a); ok = r.w > 0.f; }
        };
        if (page) walk(*page, look);
        if (!ok && panel) walk(*panel, look);
        if (!ok) { fail("outil introuvable : " + arg(1)); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "ligne") {
        // Une ligne de tableau de l'onglet courant (1.11.14 : puis du panneau du bas), par un texte qu'elle contient.
        auto* page = currentPage();
        auto* panel = bottomPanel();
        if (!page && !panel) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect r{};
        bool ok = false;
        bool offscreen = false;
        ui::TableView* foundTable = nullptr;
        std::size_t foundRow = 0;
        // Lot 21 : "exact" - la premiere colonne egale au texte (T_Four, pas la ligne
        // de T_Vanne dont une case dit "T_Four.Vannes").
        const bool exact = has("exact");
        // Lot 21 : avec Ctrl ou Maj (ajouter a la selection), une ligne hors de la
        // zone visible vient a la molette : la choisir l'aurait fait perdre aux autres.
        const bool adding = has("ctrl") || has("maj") || has("shift");
        bool wheeled = false;
        const auto scan = [&](ui::Widget& x) {
            auto* table = dynamic_cast<ui::TableView*>(&x);
            if (ok || wheeled || !table || !shown(*table) || !table->model()) return;
            for (std::size_t i = 0; i < table->visibleRowCount() && !ok && !wheeled; ++i)
                for (std::size_t c = 0; c < table->model()->columnCount(); ++c) {
                    const std::string cell = table->model()->cellText(table->viewRow(i), c);
                    if (exact ? (c != 0 || cell != arg(1)) : cell.find(arg(1)) == std::string::npos) continue;
                    ok = table->rowRect(i, r);
                    foundTable = table;
                    foundRow = i;
                    // Lot 13 : une ligne hors de la zone visible - amenee en vue
                    // (comme la molette), puis cliquee au pas suivant.
                    if (!ok && !offscreen) {
                        offscreen = true;
                        if (!adding) table->selectModelRows({table->viewRow(i)}, false);
                        else {
                            // Au-dessus ou au-dessous des lignes montrees ?
                            bool below = true;
                            for (std::size_t k = 0; k < table->visibleRowCount(); ++k) {
                                gfx::Rect probe{};
                                if (table->rowRect(k, probe)) {
                                    below = i > k;
                                    break;
                                }
                            }
                            const auto b = table->bounds();
                            send(ui::MouseWheel{{b.x + b.w * 0.5f, b.y + b.h * 0.5f}, 0.f, below ? -1.f : 1.f, {}});
                            wheeled = true;
                        }
                    }
                    break;
                }
        };
        if (page) walk(*page, scan);
        if (!ok && !offscreen && !wheeled && panel) walk(*panel, scan);
        if (!ok && offscreen && retries_ < (wheeled ? 40 : 3)) return Step::Retry;
        if (!ok) { fail("ligne introuvable : " + arg(1)); return Step::Next; }
        const gfx::Point p{r.x + 40.f, r.y + r.h * 0.5f};
        // Lot 12 : "survol" - la souris sur la ligne, sans cliquer (une vignette).
        // Lot 20 : survol "Type" - sur la case de cette colonne (l'infobulle d'une case refusee).
        if (has("survol")) {
            gfx::Point at = p;
            for (std::size_t k = 2; k + 1 < w.size(); ++k)
                if (w[k] == "survol" && foundTable) {
                    for (std::size_t c = 0; c < foundTable->model()->columnCount(); ++c)
                        if (startsWith(foundTable->model()->headerText(c), w[k + 1])) {
                            gfx::Rect cr{};
                            if (foundTable->cellRect(foundRow, c, cr)) at = {cr.x + cr.w * 0.5f, cr.y + cr.h * 0.5f};
                            break;
                        }
                }
            moveTo(at, {});
            return Step::Yield;
        }
        // Lot 20 : "droit" - le menu du clic droit (Copier, Coller...).
        if (has("droit")) { click(p, MouseButton::Right, 1, mods()); return Step::Yield; }
        click(p, MouseButton::Left, 1, mods());
        if (has("double")) click(p, MouseButton::Left, 2, mods());
        return Step::Yield;
    }
    // Lot 16 : glisser-ligne "source" "cible" - une ligne de table tiree sur une
    // autre (ranger une variable IHM dans un dossier), a la vraie souris ; cible ""
    // : lachee sous les lignes (la racine).
    if (cmd == "glisser-ligne") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect from{}, to{};
        bool okFrom = false, okTo = arg(2).empty();
        ui::TableView* table = nullptr;
        walk(*page, [&](ui::Widget& x) {
            auto* tv = dynamic_cast<ui::TableView*>(&x);
            if (table || !tv || !shown(*tv) || !tv->model()) return;
            bool a = false, b = arg(2).empty();
            gfx::Rect ra{}, rb{};
            for (std::size_t i = 0; i < tv->visibleRowCount(); ++i) {
                const std::string first = tv->model()->cellText(tv->viewRow(i), 0);
                if (!a && first == arg(1)) a = tv->rowRect(i, ra);
                if (!b && first == arg(2)) b = tv->rowRect(i, rb);
            }
            // Lot 21 : a defaut du texte exact, le debut ("Vue_Accueil" pour
            // "Vue_Accueil   * demarrage").
            for (std::size_t i = 0; i < tv->visibleRowCount() && (!a || !b); ++i) {
                const std::string first = tv->model()->cellText(tv->viewRow(i), 0);
                if (!a && !arg(1).empty() && first.rfind(arg(1), 0) == 0) a = tv->rowRect(i, ra);
                if (!b && !arg(2).empty() && first.rfind(arg(2), 0) == 0) b = tv->rowRect(i, rb);
            }
            if (a && b) {
                table = tv;
                from = ra;
                to = rb;
                okFrom = true;
                okTo = true;
            }
        });
        if (!okFrom || !okTo) { fail("lignes introuvables : " + arg(1) + " -> " + arg(2)); return Step::Next; }
        if (arg(2).empty()) {
            const auto b = table->bounds();
            to = {b.x, b.bottom() - 30.f, b.w, 20.f};
        }
        // Lot 21 : "avant" / "apres" - lachee dans le quart haut / bas de la cible
        // (reordonner) ; "tenir" - la souris reste appuyee (une capture du geste ;
        // "lacher" la relache).
        float y = to.y + to.h * 0.5f;
        if (has("avant")) y = to.y + 3.f;
        else if (has("apres")) y = to.bottom() - 3.f;
        drag({from.x + 60.f, from.y + from.h * 0.5f}, {to.x + 80.f, y}, {}, !has("tenir"));
        return Step::Yield;
    }
    // Lot API 3 : le choix des variables d'une table - cocher "Vanne_Purge",
    // filtre-choix "BOOL" (le debut du libelle d'un filtre).
    if (cmd == "cocher" || cmd == "filtre-choix") {
        auto* picker = dynamic_cast<VariablePicker*>(app_.menus().top());
        if (!picker) { fail("pas de choix de variables ouvert"); return Step::Next; }
        const bool ok = cmd == "cocher" ? picker->toggle(arg(1)) : picker->chooseFilter(arg(1));
        if (!ok) fail((cmd == "cocher" ? "variable introuvable (ou deja dans la table) : " : "filtre introuvable : ") + arg(1));
        return Step::Yield;
    }
    // Lot API 3 : glisser-vers-table "Variables IHM/Purge_Auto" [tenir] - un noeud
    // VISIBLE de l'arbre tire jusqu'aux lignes de l'onglet des tables, a la vraie
    // souris : quelques pixels dans l'arbre d'abord (le glisser part), puis dehors.
    if (cmd == "glisser-vers-table") {
        auto* tree = dynamic_cast<ui::TreeView*>(root->findById("analysis.explorer"));
        auto* page = currentPage();
        AnimationTablesPane* pane = nullptr;
        if (page) walk(*page, [&](ui::Widget& x) { if (!pane) pane = dynamic_cast<AnimationTablesPane*>(&x); });
        if (!tree || !tree->model() || !pane) { fail("arbre ou onglet des tables introuvable"); return Step::Next; }
        std::vector<std::string> parts;
        for (std::size_t from = 0;;) {
            const auto slash = arg(1).find('/', from);
            parts.push_back(treeAlias(arg(1).substr(from, slash == std::string::npos ? std::string::npos : slash - from)));
            if (slash == std::string::npos) break;
            from = slash + 1;
        }
        ui::NodeId node = ui::kInvalidNode;
        std::size_t part = 0;
        for (const auto n : tree->visibleNodes()) {
            if (!startsWith(tree->model()->text(n), parts[part])) continue;
            if (++part == parts.size()) { node = n; break; }
        }
        gfx::Rect r{};
        if (node == ui::kInvalidNode || !tree->rowRect(node, r)) {
            if (node != ui::kInvalidNode) tree->ensureVisible(node);     // hors de la vue : amene, puis tire
            if (retries_ < 3) return Step::Retry;
            fail("noeud introuvable (visible) : " + arg(1));
            return Step::Next;
        }
        const auto target = pane->lines().visible() ? pane->lines().bounds() : pane->bounds();
        const gfx::Point a{r.x + 60.f, r.y + r.h * 0.5f};
        const gfx::Point b{target.x + target.w * 0.45f, target.y + std::min(target.h * 0.5f, 160.f)};
        moveTo(a, {});
        send(ui::MouseDown{a, MouseButton::Left, 1, {}});
        const gfx::Point start{a.x + 12.f, a.y};
        moveTo(start, {});
        for (int k = 1; k <= 8; ++k) {
            const float t = static_cast<float>(k) / 8.f;
            moveTo({start.x + (b.x - start.x) * t, start.y + (b.y - start.y) * t}, {});
        }
        if (!has("tenir")) send(ui::MouseUp{b, MouseButton::Left, {}});
        return Step::Yield;
    }
    // Lot API 4 : module 0 5 - le module du rack 0, emplacement 5, choisi dans les
    // racks dessines de l'onglet courant (un clic sur sa carte).
    if (cmd == "module") {
        auto* page = currentPage();
        RackView* racks = nullptr;
        if (page) walk(*page, [&](ui::Widget& x) { if (!racks && shown(x)) racks = dynamic_cast<RackView*>(&x); });
        gfx::Rect r{};
        if (!racks || !racks->slotRect(static_cast<std::uint16_t>(std::atoi(arg(1).c_str())), static_cast<std::int16_t>(std::atoi(arg(2).c_str())), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("module introuvable : rack " + arg(1) + ", emplacement " + arg(2));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "lacher") {
        send(ui::MouseUp{driver_.mouse(), MouseButton::Left, mods()});
        return Step::Yield;
    }
    // Lot 16 : une case d'une table, editee sur place (double-clic) : cellule
    // "texte de la ligne" "Colonne" ["valeur"] - un champ recoit la valeur (et
    // Entree), une liste deroulee la choisit ; sans valeur, la case reste ouverte.
    // deplier "texte de la ligne" : la fleche d'une table en arbre.
    if (cmd == "cellule" || cmd == "deplier") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        ui::TableView* table = nullptr;
        std::size_t viewRow = 0, column = 0;
        bool found = false, colFound = cmd == "deplier";
        walk(*page, [&](ui::Widget& x) {
            auto* tv = dynamic_cast<ui::TableView*>(&x);
            if (found || !tv || !shown(*tv) || !tv->model()) return;
            // La colonne d'abord (cellule) : une ligne dont cette case s'edite passe
            // avant une autre qui porte le meme texte (un dossier et sa variable).
            std::size_t col = 0;
            bool hasCol = cmd == "deplier";
            if (cmd == "cellule")
                for (std::size_t c = 0; c < tv->model()->columnCount(); ++c)
                    if (startsWith(tv->model()->headerText(c), arg(2))) {
                        col = c;
                        hasCol = true;
                        break;
                    }
            if (!hasCol) return;
            for (int pass = 0; pass < 2 && !found; ++pass)
                for (std::size_t i = 0; i < tv->visibleRowCount() && !found; ++i) {
                    const auto row = tv->viewRow(i);
                    if (pass == 0 && cmd == "cellule" && !tv->model()->editable(row, col)) continue;
                    gfx::Rect er{};
                    if (pass == 0 && cmd == "deplier" && !tv->expanderRect(i, er)) continue;
                    for (std::size_t c = 0; c < tv->model()->columnCount(); ++c)
                        if (tv->model()->cellText(row, c) == arg(1) || (c == 0 && tv->model()->cellText(row, c).find(arg(1)) != std::string::npos)) {
                            table = tv;
                            viewRow = i;
                            column = col;
                            colFound = true;
                            found = true;
                            break;
                        }
                }
        });
        if (!found || !table) { fail("ligne introuvable : " + arg(1)); return Step::Next; }
        if (cmd == "deplier") {
            gfx::Rect r{};
            if (!table->expanderRect(viewRow, r)) {
                table->selectModelRows({table->viewRow(viewRow)}, false);
                if (retries_ < 3) return Step::Retry;
                fail("rien a deplier : " + arg(1));
                return Step::Next;
            }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (!colFound) { fail("colonne introuvable : " + arg(2)); return Step::Next; }
        table->selectModelRows({table->viewRow(viewRow)}, true);
        if (!table->beginCellEdit(table->viewRow(viewRow), column)) { fail("case non modifiable : " + arg(1) + " / " + arg(2)); return Step::Next; }
        if (w.size() < 4) return Step::Yield;
        if (auto* list = table->activeCellList()) {
            int index = -1;
            for (std::size_t i = 0; i < list->items().size(); ++i)
                if (list->items()[i].label == arg(3) || list->items()[i].value == arg(3)) index = static_cast<int>(i);
            if (index < 0) { fail("pas dans la liste : " + arg(3)); return Step::Next; }
            send(ui::KeyDown{Key::Home, {}, false});
            for (int i = 0; i < index; ++i) send(ui::KeyDown{Key::Down, {}, false});
            send(ui::KeyDown{Key::Return, {}, false});
            send(ui::KeyUp{Key::Return, {}});
            return Step::Yield;
        }
        if (auto* field = table->activeCellField()) {
            field->setText(arg(3));
            send(ui::KeyDown{Key::End, {}, false});
            send(ui::KeyDown{Key::Return, {}, false});
            send(ui::KeyUp{Key::Return, {}});
        }
        return Step::Yield;
    }
    // 1.9 (chantier C) : l'esclave simule lie, dans Configuration > Equipements (le
    // volet ouvert).
    //   equipement "<nom>"                     sa ligne (sa fiche a droite)
    //   equipement-esclave "<nom>"             la ligne de son esclave simule lie
    //   equipement-champ "<nom>" <cle> <val>   un champ de sa fiche (les cles 1.9 aussi)
    //   equipement-lecture "<nom>" vrai|esclave|auto   ce que l'IHM lit (l'application)
    //   equipement-montrer tous|vrais|esclaves|lus
    //   equipement-detacher "<nom>"            Detacher son esclave (sans question)
    //   equipement-reperes oui|non             la case Reperer les lectures simulees
    if (cmd == "equipement" || cmd == "equipement-esclave" || cmd == "equipement-champ" || cmd == "equipement-lecture"
        || cmd == "equipement-montrer" || cmd == "equipement-detacher" || cmd == "equipement-reperes") {
        auto* pane = dynamic_cast<HmiCommPane*>(currentPage());
        if (!pane) { fail(cmd + " : ouvre d'abord Configuration \xE2\x80\xBA \xC3\x89quipements"); return Step::Next; }
        std::string why;
        bool ok = true;
        if (cmd == "equipement") pane->selectEquipment(arg(1));
        else if (cmd == "equipement-esclave") {
            pane->selectSlave(arg(1));
            ok = pane->slaveSelected();
            why = arg(1) + " n'a pas d'esclave simul\xC3\xA9 li\xC3\xA9";
        } else if (cmd == "equipement-champ") ok = pane->setEquipmentField(arg(1), arg(2), arg(3), &why);
        else if (cmd == "equipement-lecture") ok = pane->setEquipmentField(arg(1), "lecture_appli", arg(2), &why);
        else if (cmd == "equipement-montrer") pane->setEquipmentFilter(arg(1));
        else if (cmd == "equipement-detacher") ok = !pane->detachSlave(arg(1), &why).empty();
        else ok = pane->setSimMarks(arg(1) != "non" && arg(1) != "0", &why);
        if (!ok) fail(cmd + " : " + why);
        return Step::Yield;
    }
    // 1.9 (chantier C) : UN VRAI APPAREIL DE LABORATOIRE, pour voir la bascule :
    // un serveur Modbus sur 127.0.0.1 (un port libre), que l'equipement vise (son
    // adresse et son port : deux commandes, Ctrl+Z) ; il se tait, il revient.
    //   labo-appareil "<equipement>"          le serveur, et l'equipement pointe dessus
    //   labo-valeur "<equipement>" <mot> <v>   un registre de maintien (offset 0..65535)
    //   labo-couper "<equipement>"            il ne repond plus (le serveur s'arrete)
    //   labo-relancer "<equipement>"          il repond de nouveau (le meme port)
    if (cmd == "labo-appareil" || cmd == "labo-valeur" || cmd == "labo-couper" || cmd == "labo-relancer") {
        struct Lab {
            std::shared_ptr<hmi::twin::TwinBank> bank{std::make_shared<hmi::twin::TwinBank>()};
            std::unique_ptr<hmi::modbus::Server> server{std::make_unique<hmi::modbus::Server>()};
            int port{0};
        };
        static std::map<std::string, Lab> labs;       // les appareils de laboratoire de la session
        auto& lab = labs[arg(1)];
        std::string why;
        if (cmd == "labo-appareil") {
            auto* pane = dynamic_cast<HmiCommPane*>(currentPage());
            if (!pane) { fail("labo-appareil : ouvre d'abord Configuration \xE2\x80\xBA \xC3\x89quipements"); return Step::Next; }
            if (!lab.server->start("127.0.0.1", lab.port, lab.bank, &why)) { fail("labo-appareil : " + why); return Step::Next; }
            lab.port = lab.server->port();
            if (!pane->setEquipmentField(arg(1), "hote", "127.0.0.1", &why) || !pane->setEquipmentField(arg(1), "port", std::to_string(lab.port), &why))
                fail("labo-appareil : " + why);
        } else if (cmd == "labo-valeur") {
            lab.bank->setWord(hmi::MemTable::Holding, static_cast<std::uint32_t>(std::strtoul(arg(2).c_str(), nullptr, 10)),
                              static_cast<std::uint16_t>(std::strtol(arg(3).c_str(), nullptr, 10)));
        } else if (cmd == "labo-couper") {
            lab.server->stop();
        } else if (!lab.server->start("127.0.0.1", lab.port, lab.bank, &why)) {
            fail("labo-relancer : " + why);
        }
        return Step::Yield;
    }
    // 1.9 (chantier C) : attendre-bascule "<equipement>" esclave|vrai - jusqu'a ce que
    // l'IHM lise son esclave simule (ou de nouveau le vrai), 120 s au plus ; les images
    // continuent. Sous un script, l'horloge de la bascule avance d'1/30 s par image :
    // "Basculer apres 10 s" dure plus longtemps qu'attendre-reel 10 sur une machine
    // chargee - on attend donc l'etat, pas une duree.
    if (cmd == "attendre-bascule") {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (realUntil_ < 0) realUntil_ = now + 120.0;
        const bool toSlave = lower(arg(2)) != "vrai";
        bool done = app_.equipments().viaTwin(arg(1)) == toSlave;
        if (!done && now >= realUntil_) {
            fail("attendre-bascule : " + arg(1) + (toSlave ? " n'est pas lu sur son esclave simul\xC3\xA9" : " n'est pas revenu au vrai appareil")
                 + " apr\xC3\xA8s 120 s");
            done = true;
        }
        if (done) {
            realUntil_ = -1;
            return Step::Yield;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        retries_ = 0;
        return Step::Retry;
    }
    // 1.9 (chantier C) : le bouton Reperes de Simuler l'IHM (pour la seance) :
    // simulation-reperes oui|non.
    // 1.10 (chantier L) : la barre de Simulation . IHM - une de ses commandes
    // (hmi.start, hmi.stop, hmi.restart, plc.toggle, both.start, all.stop, hmi.back,
    // hmi.zoomIn, hmi.zoomOut, hmi.zoomFit, hmi.zoom100) : simulation-barre <commande> ;
    // le zoom de la vue en cours : simulation-zoom <pourcent>|ajuster ; le deplacement
    // d'une vue qui depasse : simulation-deplacer <dx> <dy> (en pixels de l'ecran).
    if (cmd == "simulation-barre" || cmd == "simulation-zoom" || cmd == "simulation-deplacer") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail(cmd + " : ouvre d'abord Simulation \xC2\xB7 IHM"); return Step::Next; }
        if (cmd == "simulation-barre") pane->command(arg(1));
        else if (cmd == "simulation-deplacer")
            pane->canvas().panDisplay(static_cast<float>(std::atof(arg(1).c_str())), static_cast<float>(std::atof(arg(2).c_str())));
        else if (arg(1) == "ajuster") pane->command("hmi.zoomFit");
        else pane->canvas().setDisplayZoom(static_cast<float>(std::atof(arg(1).c_str())) / 100.f);
        pane->refreshNow();
        return Step::Yield;
    }
    if (cmd == "simulation-reperes") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("simulation-reperes : ouvre d'abord Simuler l'IHM"); return Step::Next; }
        pane->setSimMarksShown(arg(1) != "non" && arg(1) != "0");
        return Step::Yield;
    }
    if (cmd == "propriete" || cmd == "propriete-clic") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect r{};
        bool ok = false, exists = false;
        walk(*page, [&](ui::Widget& x) {
            auto* grid = dynamic_cast<ui::PropertyGrid*>(&x);
            if (ok || !grid || !shown(*grid)) return;
            ok = grid->valueRect(arg(1), r);
            if (!ok && grid->revealValue(arg(1))) exists = true;   // hors de vue : defiler
        });
        if (!ok && exists && retries_ < 3) return Step::Retry;     // le temps d'une mise en page
        if (!ok) { fail("propriete introuvable : " + arg(1)); return Step::Next; }
        if (cmd == "propriete-clic") click(centre(r), MouseButton::Left, 1, {});
        else {
            typeInto(centre(r), arg(2));
            send(ui::KeyDown{Key::Return, {}, false});
            send(ui::KeyUp{Key::Return, {}});
        }
        return Step::Yield;
    }

    // 1.11.9 : "propriete-ouvrir X" - le bouton « … » au bout de la ligne X (la fenetre de
    // l'action : l'operation en arbre, le script, la formule de Maths).
    if (cmd == "propriete-ouvrir") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect r{};
        bool ok = false;
        walk(*page, [&](ui::Widget& x) {
            auto* grid = dynamic_cast<ui::PropertyGrid*>(&x);
            if (!ok && grid && shown(*grid)) ok = grid->openRect(arg(1), r);
        });
        if (!ok) {
            if (retries_ < 3) return Step::Retry;
            fail("propriete-ouvrir : pas de bouton \xE2\x80\xA6 sur la ligne " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // 1.11.9 : les fenetres des actions (celle du dessus) -
    //   fenetre-action chercher "clavier" | choisir "Maths" | valider | annuler   (l'operation en arbre)
    //   fenetre-action cible "Sortie" | ref "Mesure" "M" "12,5" | formule "(Mesure - Consigne) * 2" | tester   (Maths)
    //   fenetre-action valeur-test 0 "12,5"   la valeur de test de la reference n. 0 (Maths)
    //   fenetre-action code "Compteur := 0;\nIHM_JOURNAL('ok');" | noms "Mot"   (le script ; \n : une ligne)
    if (cmd == "fenetre-action") {
        auto* top = app_.menus().top();
        auto* od = dynamic_cast<HmiOperationDialog*>(top);
        auto* md = dynamic_cast<HmiMathsDialog*>(top);
        auto* sd = dynamic_cast<HmiActionScriptDialog*>(top);
        if (!od && !md && !sd) { fail("fenetre-action : aucune fen\xC3\xAAtre d'action ouverte"); return Step::Next; }
        const std::string what = arg(1);
        bool ok = true;
        if (what == "valider" || what == "annuler") {
            if (od) od->finish(what == "valider");
            else if (md) { if (what == "valider") md->validate(); else md->finish(false); }
            else sd->finish(what == "valider");
        } else if (od && what == "chercher") {
            od->setSearch(arg(2));
        } else if (od && what == "choisir") {
            const auto o = hmi::operationFromLabel(arg(2));
            ok = o && od->choose(*o);
        } else if (md && what == "cible") {
            md->setTarget(arg(2));
        } else if (md && what == "ref") {
            const auto i = md->addReference(arg(2), arg(3), arg(4));
            ok = i < md->referenceCount();
        } else if (md && what == "formule") {
            md->setFormula(arg(2));
        } else if (md && what == "valeur-test") {
            const auto i = static_cast<std::size_t>(std::max(0.f, num(2)));
            ok = i < md->referenceCount();
            if (ok) md->setTestValue(i, arg(3));
        } else if (md && what == "tester") {
            (void)md->test();
        } else if (sd && what == "code") {
            std::string code = arg(2);
            for (std::size_t at = code.find("\\n"); at != std::string::npos; at = code.find("\\n", at)) code.replace(at, 2, "\n");
            sd->setCode(code);
        } else if (sd && what == "noms") {
            sd->setSearch(arg(2));
        } else {
            ok = false;
        }
        if (!ok) fail("fenetre-action : " + what + " " + arg(2) + " ?");
        return Step::Yield;
    }

    // 1.10 (chantier K) : les champs a expression. "propriete-retirer X" : le
    // bouton X "Retirer l'expression" au bout de la ligne X ; "propriete-menu X" :
    // le clic droit sur sa case (puis "choisir Retirer l'expression", "choisir
    // Modifier l'expression...", "choisir Copier").
    // 1.11.3 : "propriete-carre X" - un clic sur le carre de legende de la case X (la
    // liste des carres s'ouvre ; puis "choisir Variable IHM", "choisir Ouvrir le selecteur...").
    if (cmd == "propriete-retirer" || cmd == "propriete-menu" || cmd == "propriete-carre") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect r{};
        bool ok = false, exists = false;
        walk(*page, [&](ui::Widget& x) {
            auto* grid = dynamic_cast<ui::PropertyGrid*>(&x);
            if (ok || !grid || !shown(*grid)) return;
            ok = cmd == "propriete-retirer" ? ui::exprfield::clearRect(*grid, arg(1), r)
               : cmd == "propriete-carre"   ? grid->legendRect(arg(1), r)
                                            : grid->valueRect(arg(1), r);
            if (!ok && grid->revealValue(arg(1))) exists = true;   // hors de vue : defiler
        });
        if (!ok && exists && retries_ < 3) return Step::Retry;
        if (!ok) {
            fail(cmd == "propriete-retirer" ? "pas d'expression a retirer : " + arg(1) : "propriete introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), cmd == "propriete-menu" ? MouseButton::Right : MouseButton::Left, 1, {});
        return Step::Yield;
    }

    // 1.11.3 : LE SELECTEUR DE VALEUR ouvert (HmiValuePicker) :
    //   selecteur-chercher "texte"        la recherche de l'arbre
    //   selecteur-tout                     Tout montrer (le filtre du type attendu, decoche)
    //   selecteur-source A|I|S|V|C|Tout    la source
    //   selecteur-choisir CHEMIN           un noeud de l'arbre : il va dans Resultat
    //   selecteur-resultat "texte" [fx|constante]
    //   selecteur-valider [force]
    if (cmd.rfind("selecteur-", 0) == 0) {
        auto* picker = dynamic_cast<HmiValuePicker*>(app_.menus().top());
        if (!picker) { fail("aucun s\xC3\xA9lecteur de valeur ouvert"); return Step::Next; }
        using LS = ui::PropertyGrid::LegendStyle;
        if (cmd == "selecteur-chercher") picker->setSearch(arg(1));
        else if (cmd == "selecteur-tout") picker->setTypeFilter(false);
        else if (cmd == "selecteur-source") {
            const std::string s = arg(1);
            picker->setSource(s == "A" ? LS::Api : s == "I" ? LS::Hmi : s == "S" ? LS::System : s == "V" ? LS::Local : s == "C" ? LS::Constant : LS::Empty);
        } else if (cmd == "selecteur-choisir") {
            if (!picker->pick(arg(1))) fail("pas dans l'arbre : " + arg(1));
        } else if (cmd == "selecteur-resultat") picker->setResult(arg(1), arg(2) != "constante");
        else if (cmd == "selecteur-valider") picker->validate(arg(1) == "force");
        else { fail("commande inconnue : " + cmd); }
        return Step::Yield;
    }

    // 1.11.19 (refonte, lot 6) : le selecteur de types ouvert ("Choisir un type..." d'une liste).
    //   choix-type-chercher "four" | choix-type-categorie "Structures IHM" | choix-type-choisir "T_Four"
    //   choix-type-tableau "1..4" (vide : decoche) | choix-type-reference oui|non | choix-type-map oui|non
    //   choix-type-etat "ARRAY[1..4] OF T_Four" (le type qu'on choisirait ; echoue s'il differe)
    //   choix-type-valider | choix-type-definition | choix-type-annuler
    if (cmd.rfind("choix-type-", 0) == 0) {
        auto* picker = dynamic_cast<HmiTypePicker*>(app_.menus().top());
        if (!picker) { fail("aucun s\xC3\xA9lecteur de types ouvert"); return Step::Next; }
        if (cmd == "choix-type-chercher") picker->setSearch(arg(1));
        else if (cmd == "choix-type-categorie") { if (!picker->setCategory(arg(1))) fail("cat\xC3\xA9gorie inconnue : " + arg(1)); }
        else if (cmd == "choix-type-choisir") { if (!picker->select(arg(1))) fail("pas dans la liste : " + arg(1)); }
        else if (cmd == "choix-type-tableau") picker->setArray(!arg(1).empty(), arg(1));
        else if (cmd == "choix-type-reference") picker->setReference(arg(1) != "non");
        else if (cmd == "choix-type-map") picker->setMap(arg(1) != "non");
        else if (cmd == "choix-type-etat") {
            std::printf("[script] choix de type : %s%s\n", picker->result().c_str(),
                        picker->resultProblem().empty() ? "" : (" (" + picker->resultProblem() + ")").c_str());
            if (picker->result() != arg(1)) fail("choix de type : " + picker->result() + " au lieu de " + arg(1));
        } else if (cmd == "choix-type-valider") picker->choose();
        else if (cmd == "choix-type-definition") picker->openDefinition();
        else if (cmd == "choix-type-annuler") picker->finish(false);
        else fail("commande inconnue : " + cmd);
        return Step::Yield;
    }

    if (cmd == "couleur") {
        // Une pastille de la palette ouverte (une case Couleur) : couleur "#2F6FD6",
        // ou "transparent".
        ui::ColorPalette* palette = openColorPalette(app_.menus().top(), currentPage());
        if (!palette) { fail("aucune palette ouverte"); return Step::Next; }
        gfx::Rect r{};
        if (arg(1) == "transparent") {
            r = palette->transparentRect();
        } else if (!palette->swatchRect(arg(1), r)) {
            fail("pastille introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // 1.10 (chantier Q) : le choix personnalise et la pipette de la palette ouverte.
    //   couleur-carre S V          un clic dans le carre : saturation et valeur, en %
    //   couleur-teinte D           un clic dans la barre de teinte (degres, 0 a 359)
    //   couleur-alpha P            un clic dans la barre de transparence (opacite en %)
    //   couleur-champ C "texte"    le champ C (code R G B A T S V ; A : l'opacite en %) : un clic, puis la frappe
    //   couleur-avant | couleur-ok | couleur-annuler | couleur-pipette   un clic sur ce bouton
    //   couleur-viser X Y          pendant la pipette : la souris en (X, Y), la loupe suit
    //   couleur-prendre X Y        pendant la pipette : le clic qui prend la couleur
    //   couleur-echap              Echap (la pipette, sinon la palette)
    if (cmd.rfind("couleur-", 0) == 0) {
        ui::ColorPalette* palette = openColorPalette(app_.menus().top(), currentPage());
        if (!palette) { fail("aucune palette ouverte"); return Step::Next; }
        using F = ui::ColorPalette::Field;
        if (cmd == "couleur-carre") click(palette->squarePoint(num(1) / 100.0, num(2) / 100.0), MouseButton::Left, 1, {});
        else if (cmd == "couleur-teinte") click(palette->huePoint(num(1)), MouseButton::Left, 1, {});
        else if (cmd == "couleur-alpha") click(palette->alphaPoint(num(1) / 100.0), MouseButton::Left, 1, {});
        else if (cmd == "couleur-avant") click(centre(palette->beforeRect()), MouseButton::Left, 1, {});
        else if (cmd == "couleur-ok") click(centre(palette->okRect()), MouseButton::Left, 1, {});
        else if (cmd == "couleur-annuler") click(centre(palette->cancelRect()), MouseButton::Left, 1, {});
        else if (cmd == "couleur-pipette") click(centre(palette->pipetteRect()), MouseButton::Left, 1, {});
        else if (cmd == "couleur-viser") moveTo({num(1), num(2)}, {});
        else if (cmd == "couleur-prendre") click({num(1), num(2)}, MouseButton::Left, 1, {});
        else if (cmd == "couleur-echap") {
            send(ui::KeyDown{Key::Escape, {}, false});
            send(ui::KeyUp{Key::Escape, {}});
        } else if (cmd == "couleur-champ") {
            const std::string c = arg(1);
            const F f = c == "R" ? F::R : c == "G" ? F::G : c == "B" ? F::B : c == "A" ? F::A
                      : c == "T" ? F::H : c == "S" ? F::S : c == "V" ? F::V : F::Hex;
            click(centre(palette->fieldRect(f)), MouseButton::Left, 1, {});
            if (!arg(2).empty()) send(ui::TextInput{arg(2)});
        } else {
            fail("commande inconnue : " + cmd);
            return Step::Next;
        }
        return Step::Yield;
    }

    // Lot 15 : le schema du reseau du PC - un port, un equipement, un bouton
    // de l'encadre Hors reseau (reseau-bouton 1 donner | modifier).
    if (cmd == "reseau-port" || cmd == "reseau-equipement" || cmd == "reseau-bouton") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        HmiNetDiagram* diagram = nullptr;
        walk(*page, [&](ui::Widget& x) {
            if (auto* d = dynamic_cast<HmiNetDiagram*>(&x); !diagram && d && shown(*d)) diagram = d;
        });
        if (!diagram) { fail("pas de sch\xC3\xA9ma du r\xC3\xA9seau ici"); return Step::Next; }
        gfx::Rect r{};
        bool ok = false;
        if (cmd == "reseau-port") ok = diagram->portRect(arg(1), r);
        else if (cmd == "reseau-equipement") ok = diagram->equipmentRect(arg(1), r);
        else ok = diagram->buttonRect(std::atoi(arg(1).c_str()) - 1, arg(2) != "modifier", r);
        if (!ok && retries_ < 5) return Step::Retry;          // le temps d'un dessin
        if (!ok) { fail(cmd + " introuvable : " + arg(1)); return Step::Next; }
        const gfx::Point p = cmd == "reseau-bouton" ? centre(r) : gfx::Point{r.x + r.w * 0.5f, r.y + 14.f};
        if (has("survol")) { moveTo(p, {}); return Step::Yield; }
        click(p, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot 17 : glisser un equipement du schema sur un port (reseau-glisser "equipement"
    // "port" [tenir] : sans lacher - le port vise s'allume ; relacher : lacher la ou
    // est la souris) ; le bouton Reel | Simule (reseau-vue simule).
    if (cmd == "reseau-glisser" || cmd == "reseau-vue") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        HmiNetDiagram* diagram = nullptr;
        walk(*page, [&](ui::Widget& x) {
            if (auto* d = dynamic_cast<HmiNetDiagram*>(&x); !diagram && d && shown(*d)) diagram = d;
        });
        if (!diagram) { fail("pas de sch\xC3\xA9ma du r\xC3\xA9seau ici"); return Step::Next; }
        if (cmd == "reseau-vue") {
            gfx::Rect r{};
            if (!diagram->toggleRect(arg(1).rfind("sim", 0) == 0, r)) {
                if (retries_ < 5) return Step::Retry;
                fail("bouton R\xC3\xA9" "el | Simul\xC3\xA9 introuvable");
                return Step::Next;
            }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        gfx::Rect from{}, to{};
        if (!diagram->equipmentRect(arg(1), from) || !diagram->portRect(arg(2), to)) {
            if (retries_ < 5) return Step::Retry;
            fail("reseau-glisser : " + arg(1) + " ou " + arg(2) + " introuvable");
            return Step::Next;
        }
        drag({from.x + from.w * 0.5f, from.y + 20.f}, {to.x + to.w * 0.5f, to.y + to.h * 0.5f}, {}, !has("tenir"));
        return Step::Yield;
    }
    if (cmd == "relacher") {
        send(ui::MouseUp{driver_.mouse(), MouseButton::Left, {}});
        return Step::Yield;
    }
    // Lot 18 : une ligne des valeurs simulees (Configuration > Equipements, ou
    // l'onglet Jumeaux de la simulation) - valeur "Centrale PM5560" 43003 animer
    // | mouvement | forcer | valeur-brute | libre | 0 | 1 | ligne | ajouter [survol|double].
    if (cmd == "valeur" || cmd == "valeur-tirer") {
        auto* page = currentPage();
        HmiTwinValues* tv = nullptr;
        if (page)
            walk(*page, [&](ui::Widget& x) {
                if (auto* twv = dynamic_cast<HmiTwinValues*>(&x); !tv && twv && shown(*twv)) tv = twv;
            });
        if (!tv) {
            if (retries_ < 4) return Step::Retry;
            fail("pas de valeurs simul\xC3\xA9" "es ici");
            return Step::Next;
        }
        using P = HmiTwinValues::Part;
        if (cmd == "valeur-tirer") {
            // valeur-tirer "Centrale PM5560" 43003 haut|bas|bande <valeur> [tenir]
            const std::string which = arg(3);
            const P part = which == "bas" ? P::Low : which == "bande" ? P::Band : P::High;
            gfx::Rect r{};
            float x = 0;
            std::string v = arg(4);
            for (auto& ch : v)
                if (ch == ',') ch = '.';
            if (!tv->partRect(arg(1), arg(2), part, r) || !tv->valueX(arg(1), arg(2), std::atof(v.c_str()), x)) {
                if (retries_ < 4) return Step::Retry;
                fail("valeur-tirer : " + arg(1) + " " + arg(2) + " " + which + " introuvable");
                return Step::Next;
            }
            const gfx::Point from = centre(r);
            float to = x;
            if (part == P::Band) {
                // La bande : son milieu va a la valeur.
                const auto* l = tv->line(arg(1), arg(2));
                float xa = 0, xb = 0;
                if (l && tv->valueX(arg(1), arg(2), l->lo, xa) && tv->valueX(arg(1), arg(2), l->hi, xb)) to = from.x + (x - (xa + xb) * 0.5f);
            }
            drag(from, {to, from.y}, {}, !has("tenir"));
            return Step::Yield;
        }
        const std::string what = arg(3);
        P part = P::Row;
        if (what == "animer") part = P::Check;
        else if (what == "mouvement") part = P::Kind;
        else if (what == "forcer") part = P::ForceBox;
        else if (what == "valeur-brute") part = P::ForceValue;
        else if (what == "libre") part = P::Free;
        else if (what == "0") part = P::Zero;
        else if (what == "1") part = P::One;
        else if (what == "ajouter") part = P::Add;
        else if (what == "barre") part = P::Bar;
        else if (what == "bornes") part = P::Edit;          // 1.11.5 : le crayon des bornes
        else if (what == "noeud") part = P::Node;
        gfx::Rect r{};
        if (!tv->partRect(arg(1), arg(2), part, r)) {
            if (retries_ < 4) return Step::Retry;
            fail("valeur : " + arg(1) + " " + arg(2) + " (" + what + ") introuvable");
            return Step::Next;
        }
        const gfx::Point p = part == P::Row ? gfx::Point{r.x + 60.f, r.y + r.h * 0.5f} : centre(r);
        if (has("survol")) {
            moveTo(p, {});
            return Step::Yield;
        }
        click(p, MouseButton::Left, 1, {});
        if (has("double")) click(p, MouseButton::Left, 2, {});
        return Step::Yield;
    }
    // 1.11.5 : l'arbre des valeurs simulees - valeurs-chercher "texte" (la recherche),
    // valeurs-noeud "equipement|Four1.Zones" ouvrir|fermer.
    if (cmd == "valeurs-chercher" || cmd == "valeurs-noeud" || cmd == "valeurs-vue" || cmd == "valeurs-menu") {
        auto* page = currentPage();
        HmiTwinValues* tv = nullptr;
        if (page)
            walk(*page, [&](ui::Widget& x) {
                if (auto* twv = dynamic_cast<HmiTwinValues*>(&x); !tv && twv && shown(*twv)) tv = twv;
            });
        if (!tv) {
            if (retries_ < 4) return Step::Retry;
            fail("pas de valeurs simul\xC3\xA9" "es ici");
            return Step::Next;
        }
        if (cmd == "valeurs-noeud") {
            tv->setNodeOpen(arg(1), arg(2) != "fermer");
            return Step::Next;
        }
        // 1.11.6 : valeurs-vue on|off (sur la vue actuelle) ; valeurs-menu "titre" (le clic droit sur la ligne :
        // un esclave, un noeud Four1, une variable).
        if (cmd == "valeurs-vue") {
            if (auto* box = tv->viewBox()) box->setState(arg(1) == "off" || arg(1) == "non" ? ui::Checkbox::State::Unchecked : ui::Checkbox::State::Checked);
            else fail("valeurs-vue : pas de case ici (l'onglet Esclaves simul\xC3\xA9s de la simulation)");
            return Step::Yield;
        }
        if (cmd == "valeurs-menu") {
            const auto& ls = tv->lines();
            for (std::size_t i = 0; i < ls.size(); ++i) {
                const bool group = ls[i].kind == HmiTwinValues::Line::Kind::Group;
                if ((group ? ls[i].equipment : ls[i].title) != arg(1)) continue;
                gfx::Rect r{};
                if (!tv->lineRectOf(i, r)) break;
                click({r.x + 80.f, r.y + r.h * 0.5f}, MouseButton::Right, 1, {});
                return Step::Yield;
            }
            if (retries_ < 4) return Step::Retry;
            fail("valeurs-menu : ligne " + arg(1) + " introuvable");
            return Step::Next;
        }
        auto* box = tv->searchBox();
        if (!box || !shown(*box)) { fail("valeurs-chercher : pas de recherche ici"); return Step::Next; }
        typeInto(centre(box->bounds()), arg(1));
        return Step::Yield;
    }
    // 1.11.5 : les onglets Variables IHM et Variables API de la simulation (des arbres) -
    //   simvar ihm|api chercher "texte"
    //   simvar ihm|api noeud "Four1.Vannes" ouvrir|fermer
    //   simvar ihm|api forcer "chemin" ["valeur"]   (sans valeur : la case, a la valeur du moment)
    //   simvar ihm|api liberer "chemin"
    //   simvar ihm|api editer "chemin"              (le champ de la valeur, comme un double-clic)
    // 1.11.6 : l'onglet Expressions en arbre - expressions-chercher "texte" ; expressions-deplier on|off.
    if (cmd == "expressions-chercher" || cmd == "expressions-deplier") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        if (cmd == "expressions-deplier") {
            pane->expandExpressions(arg(1) != "off" && arg(1) != "non");
            return Step::Yield;
        }
        auto* box = pane->expressionsSearch();
        if (!box || !shown(*box)) {
            if (retries_ < 4) return Step::Retry;
            fail("expressions-chercher : l'onglet Expressions n'est pas montr\xC3\xA9");
            return Step::Next;
        }
        typeInto(centre(box->bounds()), arg(1));
        return Step::Yield;
    }
    if (cmd == "simvar") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        HmiSimVarTree& tree = arg(1) == "api" ? pane->apiVariables() : pane->ihmVariables();
        const std::string what = arg(2);
        if (what == "chercher") {
            auto* box = tree.searchBox();
            if (!box || !shown(*box)) {
                if (retries_ < 4) return Step::Retry;
                fail("simvar : la recherche n'est pas montr\xC3\xA9" "e (l'onglet est-il ouvert ?)");
                return Step::Next;
            }
            typeInto(centre(box->bounds()), arg(3));
            return Step::Yield;
        }
        if (what == "noeud") { tree.setOpen(arg(3), arg(4) != "fermer"); return Step::Next; }
        if (what == "liberer") {
            if (!tree.unforcePath(arg(3))) fail("simvar : " + arg(3) + " ne se lib\xC3\xA8re pas");
            return Step::Next;
        }
        if (what == "forcer") {
            if (w.size() > 4) {
                if (!tree.forcePath(arg(3), arg(4))) fail("simvar : " + arg(3) + " ne se force pas a " + arg(4));
                return Step::Next;
            }
            gfx::Rect r{};
            if (!tree.rowRect(arg(3), r)) {
                (void)tree.reveal(arg(3));
                if (retries_ < 4) return Step::Retry;
                fail("simvar : la ligne " + arg(3) + " n'est pas montr\xC3\xA9" "e");
                return Step::Next;
            }
            click(centre(tree.forceBox(r)), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (what == "editer") {
            if (!tree.openValueEditor(arg(3))) fail("simvar : pas de champ pour " + arg(3));
            return Step::Next;
        }
        // 1.11.6 : simvar ihm|api vue on|off ; menu "chemin" (le clic droit) ; mouvement "chemin" "sinus"|"aucun" ;
        // bornes "chemin" (le champ des bornes) ; cellule-mouvement "chemin" (le menu des types).
        if (what == "vue") {
            tree.setOnlyView(arg(3) != "off" && arg(3) != "non");
            return Step::Yield;
        }
        if (what == "menu" || what == "cellule-mouvement") {
            gfx::Rect r{};
            if (!tree.rowRect(arg(3), r)) {
                (void)tree.reveal(arg(3));
                if (retries_ < 4) return Step::Retry;
                fail("simvar : la ligne " + arg(3) + " n'est pas montr\xC3\xA9" "e");
                return Step::Next;
            }
            const gfx::Point at{r.x + (what == "menu" ? 60.f : r.w * 0.72f), r.y + r.h * 0.5f};
            if (what == "menu") {
                click(at, MouseButton::Right, 1, {});
                return Step::Yield;
            }
            if (!tree.openMotionMenu(arg(3), at)) fail("simvar : pas de menu de mouvement pour " + arg(3));
            return Step::Yield;
        }
        if (what == "mouvement") {
            if (!tree.setMotionKind(arg(3), arg(4))) fail("simvar : le mouvement " + arg(4) + " ne se pose pas sur " + arg(3));
            return Step::Yield;
        }
        if (what == "bornes") {
            if (!tree.openMotionEditor(arg(3))) fail("simvar : pas de bornes pour " + arg(3));
            return Step::Yield;
        }
        fail("simvar : " + what + " ? (chercher, noeud, forcer, liberer, editer, vue, menu, mouvement, bornes, cellule-mouvement)");
        return Step::Next;
    }
    // Lot 17 : une case de la carte memoire - carte-case 4x 52 [double|survol].
    if (cmd == "carte-case") {
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        HmiMemoryMap* map = nullptr;
        walk(*page, [&](ui::Widget& x) {
            if (auto* m = dynamic_cast<HmiMemoryMap*>(&x); !map && m && shown(*m)) map = m;
        });
        if (!map) { fail("pas de carte m\xC3\xA9moire ici"); return Step::Next; }
        const auto t = hmi::zones::tableFrom(arg(1));
        if (!t) { fail("table inconnue : " + arg(1)); return Step::Next; }
        const auto o = static_cast<std::uint32_t>(std::max(0, std::atoi(arg(2).c_str())));
        gfx::Rect r{};
        if (!map->cellRect(*t, o, r)) {
            if (retries_ < 4) {
                map->select(*t, o, true);          // la montrer (deplier, defiler), puis cliquer
                return Step::Retry;
            }
            fail("case introuvable : " + arg(1) + " " + arg(2));
            return Step::Next;
        }
        const gfx::Point p = centre(r);
        if (has("survol")) { moveTo(p, {}); return Step::Yield; }
        click(p, MouseButton::Left, 1, {});
        if (has("double")) click(p, MouseButton::Left, 2, {});
        return Step::Yield;
    }
    // Lot 17 : glisser une barre de la carte sur une autre case (carte-glisser 4x 52 4x 60).
    if (cmd == "carte-glisser") {
        auto* page = currentPage();
        HmiMemoryMap* map = nullptr;
        if (page)
            walk(*page, [&](ui::Widget& x) {
                if (auto* m = dynamic_cast<HmiMemoryMap*>(&x); !map && m && shown(*m)) map = m;
            });
        const auto t1 = hmi::zones::tableFrom(arg(1)), t2 = hmi::zones::tableFrom(arg(3));
        gfx::Rect a{}, b{};
        if (!map || !t1 || !t2 || !map->cellRect(*t1, static_cast<std::uint32_t>(std::atoi(arg(2).c_str())), a)
            || !map->cellRect(*t2, static_cast<std::uint32_t>(std::atoi(arg(4).c_str())), b)) {
            if (retries_ < 3) return Step::Retry;
            fail("carte-glisser : cases introuvables");
            return Step::Next;
        }
        drag(centre(a), centre(b), {});
        return Step::Yield;
    }
    // Lot 17 : un choix exclusif (un rond) d'un dialogue, par son libelle (le debut suffit).
    if (cmd == "radio") {
        ui::RadioButton* found = nullptr;
        walk(*root, [&](ui::Widget& x) {
            auto* b = dynamic_cast<ui::RadioButton*>(&x);
            if (!found && b && shown(*b) && startsWith(b->label(), arg(1))) found = b;
        });
        if (!found) { fail("choix introuvable : " + arg(1)); return Step::Next; }
        const auto r = found->bounds();
        click({r.x + 9.f, r.y + r.h * 0.5f}, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // ---- 1.9 : la lecture cyclique a plusieurs requetes (IHM > Outil Modbus) ----
    //  outil-requete "<nom>" <cible> <fonction> <adresse> <nombre> [format] [periode]
    //        une requete ; cible : automate, adresse, le nom d'un equipement, ou
    //        "<Nom> . esclave simule" (son libelle) ; format : une cle (decimal, signe,
    //        hexa, binaire, ascii, u32, s32, flottant) ; periode : ms (0 : la commune)
    //  outil-requete-regler <Rn|n> <cle> "<valeur>"   un reglage de la grille (nom, active,
    //        cible, hote, port, esclave, delai, fonction, adresse, nombre, format, ordre, periode, tracer)
    //  outil-requetes-variables <nom> [<nom>...]       depuis des variables (regroupees)
    //  outil-requetes-zones "<equipement>"            une requete par zone memoire
    //  outil-requetes-coller "<texte>"                coller (\t entre les cases, | entre les lignes)
    //  outil-requete-choisir <Rn> ; outil-requete-retirer <Rn> ; outil-requete-dupliquer <Rn>
    //  outil-cyclique-demarrer ; outil-cyclique-arreter ; outil-cyclique-figer [oui|non]
    //  outil-cyclique-reprendre <Rn> ; outil-cyclique-attendre <lectures> (toutes requetes)
    //  outil-cyclique-option <cle> "<valeur>"  (periode_commune, enchainement, pause, echecs, fenetre, enregistrer, pistes)
    //  outil-cyclique-vue valeurs|journal|pistes|echelle|choisie|toutes
    //  outil-cyclique-menu requete|jeu|zones ; outil-cyclique-menu-choisir "<debut du libelle>"
    //  outil-cyclique-variables [<nom>...]  le dialogue Lire des variables (et ces cases cochees)
    //  outil-cyclique-export [un|chaque] [brut] ; outil-cyclique-fermer (le menu, le dialogue)
    //  outil-jeu-enregistrer "<nom>" ; outil-jeu-ouvrir "<nom>" ; outil-jeu-nouveau
    if (cmd.rfind("outil-requete", 0) == 0 || cmd.rfind("outil-cyclique", 0) == 0 || cmd.rfind("outil-jeu", 0) == 0) {
        auto* tool = dynamic_cast<HmiModbusToolPane*>(currentPage());
        if (!tool) { fail(cmd + " : l'onglet n'est pas l'outil Modbus"); return Step::Next; }
        HmiCyclicPage& cy = tool->cyclic();
        tool->showTab("cyclique");
        const auto rid = [&](const std::string& t) { return std::atoi(t.c_str() + (!t.empty() && (t[0] == 'R' || t[0] == 'r') ? 1 : 0)); };
        std::string why;
        bool ok = true;
        if (cmd == "outil-requete") {
            const int id = cy.addNew(&why);
            ok = id != 0 && cy.setField(id, "nom", arg(1), &why) && cy.setField(id, "cible", arg(2), &why)
                 && cy.setField(id, "fonction", arg(3).empty() ? "3" : arg(3), &why) && cy.setField(id, "adresse", arg(4).empty() ? "0" : arg(4), &why)
                 && cy.setField(id, "nombre", arg(5).empty() ? "10" : arg(5), &why) && (arg(6).empty() || cy.setField(id, "format", arg(6), &why))
                 && (arg(7).empty() || cy.setField(id, "periode", arg(7), &why));
        } else if (cmd == "outil-requete-regler") {
            ok = cy.setField(rid(arg(1)), arg(2), arg(3), &why);
        } else if (cmd == "outil-requetes-variables") {
            ok = !cy.addFromVariables(std::vector<std::string>(w.begin() + 1, w.end()), true, 10, &why).empty();
        } else if (cmd == "outil-requetes-zones") {
            ok = !cy.addFromZones(arg(1), &why).empty();
        } else if (cmd == "outil-requetes-coller") {
            std::string text = arg(1);
            for (auto& c : text) if (c == '|') c = '\n';
            std::string tabs;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 't') { tabs += '\t'; ++i; }
                else tabs += text[i];
            }
            const auto rep = cy.pasteRequests(tabs);
            ok = rep.added > 0;
            if (!ok) why = rep.notes.empty() ? std::string("rien de coll\xC3\xA9") : rep.notes.front();
        } else if (cmd == "outil-requete-choisir") {
            ok = cy.select(rid(arg(1)));
        } else if (cmd == "outil-requete-retirer") {
            ok = cy.remove(rid(arg(1)));
        } else if (cmd == "outil-requete-dupliquer") {
            ok = cy.duplicate(rid(arg(1)));
        } else if (cmd == "outil-cyclique-demarrer") {
            ok = tool->startCyclic(&why);
        } else if (cmd == "outil-cyclique-arreter") {
            tool->stopCyclic();
        } else if (cmd == "outil-cyclique-figer") {
            cy.setFrozen(arg(1) != "non");
        } else if (cmd == "outil-cyclique-reprendre") {
            ok = cy.resume(rid(arg(1)));
        } else if (cmd == "outil-cyclique-attendre") {
            std::uint64_t reads = 0;
            for (const auto& s : cy.poller().states()) reads += s.reads + s.errors;
            if (reads < static_cast<std::uint64_t>(std::max(1, std::atoi(arg(1).c_str())))) {
                if (retries_ < 2000) return Step::Retry;
                why = "seulement " + std::to_string(reads) + " lectures";
                ok = false;
            }
            cy.tick(0, true);
        } else if (cmd == "outil-cyclique-option") {
            ok = cy.setOption(arg(1), arg(2), &why);
        } else if (cmd == "outil-cyclique-vue") {
            const std::string v = arg(1);
            if (v == "valeurs") cy.showJournal(false);
            else if (v == "journal") cy.showJournal(true);
            else if (v == "pistes") cy.setLanes(true);
            else if (v == "echelle") cy.setLanes(false);
            else if (v == "choisie") cy.setOnlySelected(true);
            else if (v == "toutes") cy.setOnlySelected(false);
            else { ok = false; why = "vue inconnue : " + v; }
        } else if (cmd == "outil-cyclique-menu") {
            const auto r = tool->tools().rectOf(tool->tools().actionByTip(arg(1) == "jeu" ? "Les jeux de lecture" : "Ajouter des requ\xC3\xAAtes"));
            const gfx::Point at{r.x, r.y + r.h + 2};
            if (arg(1) == "jeu") cy.openSetsMenu(at);
            else if (arg(1) == "zones") cy.openZonesMenu(at);
            else cy.openAddMenu(at);
        } else if (cmd == "outil-cyclique-menu-choisir") {
            auto* menu = dynamic_cast<HmiCyclicMenu*>(cy.overlay());
            ok = false;
            if (menu)
                for (const auto& it : menu->items())
                    if (!ok && !it.separator && !it.heading && startsWith(it.label, arg(1))) ok = menu->choose(it.id);
            if (!ok) why = "pas d'entr\xC3\xA9" "e \xC2\xAB " + arg(1) + " \xC2\xBB dans le menu";
        } else if (cmd == "outil-cyclique-variables") {
            cy.openVariablesDialog();
            if (auto* dlg = dynamic_cast<HmiCyclicVarsDialog*>(cy.overlay()))
                for (std::size_t i = 1; i < w.size(); ++i)
                    if (!dlg->check(w[i])) { ok = false; why = "pas de variable " + w[i]; }
        } else if (cmd == "outil-cyclique-export") {
            cy.openExportDialog();
            if (auto* dlg = dynamic_cast<HmiCyclicExportDialog*>(cy.overlay())) {
                dlg->setOneFile(arg(1) != "chaque");
                dlg->setRaw(has("brut"));
            }
        } else if (cmd == "outil-cyclique-fermer") {
            if (auto* o = cy.overlay()) o->close();
        } else if (cmd == "outil-jeu-enregistrer") {
            ok = cy.saveSet(arg(1), &why);
        } else if (cmd == "outil-jeu-ouvrir") {
            ok = cy.openSet(arg(1), &why);
        } else if (cmd == "outil-jeu-nouveau") {
            cy.newSet();
        } else {
            ok = false;
            why = "commande inconnue";
        }
        if (!ok) fail(cmd + " : " + why);
        return Step::Yield;
    }
    // Lot 15 : la trame tapee dans l'outil Modbus.
    if (cmd == "trame") {
        auto* tool = dynamic_cast<HmiModbusToolPane*>(currentPage());
        if (!tool) { fail("l'onglet n'est pas l'outil Modbus"); return Step::Next; }
        std::string why;
        if (!tool->sendFrame(arg(1), &why)) fail("trame : " + why);
        return Step::Yield;
    }
    // ---- lot 19 : l'historique ------------------------------------------------
    //  historique [ouvrir|fermer]                  le tiroir (Ctrl+H)
    //  historique-ligne "debut du libelle" [double|droit]   une ligne du tiroir
    //  historique-filtre tout|api|ihm|onglet ; historique-chercher "texte"
    //  annuler [n] ; retablir [n] ; fermer-fenetre (le garde-fou a la fermeture)
    if (cmd == "historique" || cmd == "historique-ligne" || cmd == "historique-filtre" || cmd == "historique-chercher") {
        auto* topRoot = top();
        auto* panel = topRoot ? dynamic_cast<HistoryPanel*>(topRoot->findById("analysis.history")) : nullptr;
        if (!panel) { fail("pas de tiroir Historique ici"); return Step::Next; }
        if (cmd == "historique") {
            const std::string how = arg(1);
            if (how == "ouvrir" && panel->isOpen()) return Step::Yield;
            if (how == "fermer" && !panel->isOpen()) return Step::Yield;
            (void)app_.actions().trigger("edit.history", app_.commands());
            return Step::Yield;
        }
        if (cmd == "historique-filtre") {
            const std::string f = lower(arg(1));
            panel->setFilter(f == "api" ? HistoryPanel::Filter::Api : f == "ihm" ? HistoryPanel::Filter::Hmi
                           : f == "onglet" ? HistoryPanel::Filter::Here : HistoryPanel::Filter::All);
            return Step::Yield;
        }
        if (cmd == "historique-chercher") { panel->setSearch(arg(1)); return Step::Yield; }
        const int row = panel->rowByLabel(arg(1));
        if (row < 0) { fail("ligne de l'historique introuvable : " + arg(1)); return Step::Next; }
        panel->reveal(row);
        const auto r = panel->rowRect(row);
        if (r.empty()) { fail("ligne de l'historique hors de vue (dessiner d'abord) : " + arg(1)); return Step::Yield; }
        const gfx::Point at{r.x + r.w * 0.4f, r.y + r.h * 0.5f};
        if (arg(2) == "droit") click(at, MouseButton::Right, 1, {});
        else if (arg(2) == "double") { click(at, MouseButton::Left, 1, {}); click(at, MouseButton::Left, 2, {}); }
        else click(at, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // ---- 1.10 (chantier P) : les nouveautes se voient ----
    //  nouveautes-relancer 1.10.0 1.8.0   rejouer le premier lancement de 1.10.0 pour qui vient de 1.8.0
    //                                     ("" : un profil neuf) ; l'etat repart de zero, les reperes montres
    //  nouveautes-fenetre                 la fenetre des nouveautes (comme Aide > Nouveautes de cette version)
    //  nouveautes-deplier 1.9             deplier / replier la ligne d'une version manquee (vraie souris)
    //  nouveautes-carte 1.10.couleurs     Me montrer sur cette carte (vraie souris)
    //  nouveautes-bouton "Tout vu"        un bouton du pied : Plus tard, Tout vu, Me montrer tout, Reperes,
    //                                     Fermer (vraie souris)
    //  nouveautes-bulle Suivante          un bouton de la bulle : Suivante, Terminer, Aide, Fermer (vraie souris)
    //  nouveautes-reperes oui|non         montrer / masquer les reperes (masques par defaut en mode script)
    //  nouveautes-repere 1.10.xxx         cliquer l'element marque de cette nouveaute (vraie souris)
    //  nouveautes-etat ["texte"]          ecrire l'etat dans le journal ; avec un texte : verifier qu'il y est
    if (cmd.rfind("nouveautes-", 0) == 0) {
        auto& nc = noveltyCenter();
        if (cmd == "nouveautes-relancer") {
            nc.relaunch(arg(1).empty() ? std::string("1.10.0") : arg(1), arg(2));
            return Step::Next;
        }
        if (cmd == "nouveautes-fenetre") { nc.openBoard(); return Step::Yield; }
        // nouveautes-aide reperes-nouveautes : l'aide de l'IHM sur ce sujet ;
        // nouveautes-aide-outil "Tout marquer comme lu" : un bouton de sa barre (vraie souris).
        if (cmd == "nouveautes-aide") {
            app_.setHelpTopic(arg(1));
            app_.menus().PushMenu("help.hmi");
            return Step::Yield;
        }
        if (cmd == "nouveautes-aide-outil") {
            HmiHelpPane* help = nullptr;
            if (auto* root0 = top()) walk(*root0, [&](ui::Widget& x) {
                if (auto* h = dynamic_cast<HmiHelpPane*>(&x); !help && h && shown(*h)) help = h;
            });
            gfx::Rect r{};
            if (help) walk(*help, [&](ui::Widget& x) {
                auto* strip = dynamic_cast<HmiToolStrip*>(&x);
                if (r.w > 0.f || !strip || !shown(*strip)) return;
                if (const int a = strip->actionByTip(arg(1)); a >= 0) r = strip->rectOf(a);
            });
            if (r.w <= 0.f) {
                if (retries_ < 30) return Step::Retry;
                fail("aide de l'IHM : pas de bouton " + arg(1));
                return Step::Next;
            }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (cmd == "nouveautes-aide-notation") {
            // 1.10 (chantier P) : nouveautes-aide-notation ST|C|C++ - un clic sur cette notation
            // du selecteur du premier exemple a plusieurs notations du sujet montre
            // (la page y defile d'abord).
            HmiHelpPane* help = nullptr;
            if (auto* root0 = top()) walk(*root0, [&](ui::Widget& x) {
                if (auto* h = dynamic_cast<HmiHelpPane*>(&x); !help && h && shown(*h)) help = h;
            });
            gfx::Rect hit{};
            if (help) {
                auto& page = help->article();
                (void)page.revealHotspots("notation:");
                const auto r = page.contentRect();
                const float top0 = r.y - page.scrollOffset();
                for (const auto& h : page.layoutForTest().hotspots) {
                    if (h.target != "notation:" + arg(1)) continue;
                    const gfx::Rect rc{r.x + h.rect.x, top0 + h.rect.y, h.rect.w, h.rect.h};
                    if (r.contains(centre(rc))) { hit = rc; break; }
                }
            }
            if (hit.w <= 0.f) {
                if (retries_ < 30) return Step::Retry;
                fail("aide de l'IHM : pas de notation " + arg(1) + " a l'ecran");
                return Step::Next;
            }
            click(centre(hit), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (cmd == "nouveautes-reperes") {
            nc.setMarksHidden(arg(1) == "non");
            nc.setScriptMarks(arg(1) != "non");
            return Step::Next;
        }
        if (cmd == "nouveautes-etat") {
            const auto d = nc.describe();
            std::fprintf(stderr, "[script] ligne %zu : nouveautes : %s\n", pc_ + 1, d.c_str());
            if (!arg(1).empty() && d.find(arg(1)) == std::string::npos) fail("l'\xC3\xA9tat des nouveaut\xC3\xA9s ne dit pas \"" + arg(1) + "\" : " + d);
            return Step::Next;
        }
        if (cmd == "nouveautes-bulle") {
            auto& sp = nc.spotlight();
            using P = ui::novelty::Spotlight::Part;
            const auto& a = arg(1);
            const P part = a == "Terminer" ? P::End : a == "Aide" ? P::Help : a == "Fermer" ? P::Close : P::Next;
            const auto r = sp.partRect(part);
            if (!sp.active() || r.w <= 0.f) {
                if (retries_ < 30) return Step::Retry;
                fail("bulle des nouveaut\xC3\xA9s : pas de bouton " + a);
                return Step::Next;
            }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (cmd == "nouveautes-repere") {
            for (const auto& m : nc.marks().marks())
                if (m.key == arg(1)) { click(centre(m.rect), MouseButton::Left, 1, {}); return Step::Yield; }
            if (retries_ < 30) return Step::Retry;
            fail("pas de rep\xC3\xA8re \xC3\xA0 l'\xC3\xA9" "cran pour " + arg(1));
            return Step::Next;
        }
        // La fenetre ouverte : sa planche de cartes.
        ui::novelty::Board* board = nullptr;
        if (auto* topRoot = top()) walk(*topRoot, [&](ui::Widget& x) { if (!board) board = dynamic_cast<ui::novelty::Board*>(&x); });
        if (!board || board->bounds().w <= 0.f) {
            if (retries_ < 30) return Step::Retry;
            fail("la fen\xC3\xAAtre des nouveaut\xC3\xA9s n'est pas ouverte (" + cmd + ")");
            return Step::Next;
        }
        gfx::Rect r{};
        if (cmd == "nouveautes-deplier") {
            r = board->groupRect(arg(1));
            // 1.10 (H) : la ligne de la version manquee sous le bord (19 cartes de la 1.10
            // avant elle) : la fenetre defile d'abord, comme le ferait l'utilisateur.
            if (r.w > 0.f && r.bottom() > board->bounds().bottom() - 70.f) { board->scrollBy(r.bottom() - board->bounds().bottom() + 140.f); return Step::Retry; }
        } else if (cmd == "nouveautes-carte") {
            for (std::size_t i = 0; i < board->cards().size(); ++i)
                if (board->cards()[i].id == arg(1)) {
                    r = board->showMeRect(i);
                    // Plus bas que la fenetre : la faire defiler d'abord.
                    if (r.w > 0.f && r.bottom() > board->bounds().bottom() - 70.f) { board->scrollBy(r.bottom() - board->bounds().bottom() + 140.f); return Step::Retry; }
                    // 1.10 (H) : plus haut (la fenetre a defile vers une version manquee) : remonter.
                    if (r.w > 0.f && r.y < board->bounds().y + 64.f) { board->scrollBy(r.y - board->bounds().y - 120.f); return Step::Retry; }
                }
        } else if (cmd == "nouveautes-bouton") {
            using B = ui::novelty::Board::Button;
            const auto& a = arg(1);
            r = board->buttonRect(a == "Tout vu" ? B::AllSeen : a == "Plus tard" ? B::Later : a == "Reperes" ? B::HideMarks
                                  : a == "Fermer" ? B::Close : a == "Versions precedentes" ? B::Previous : B::ShowAll);
            // 1.10 (H) : la ligne des versions precedentes est au bout de la liste : y defiler d'abord.
            if (a == "Versions precedentes" && r.w > 0.f && r.bottom() > board->bounds().bottom() - 70.f) {
                board->scrollBy(r.bottom() - board->bounds().bottom() + 140.f);
                return Step::Retry;
            }
        } else {
            fail("commande inconnue : " + cmd);
            return Step::Next;
        }
        if (r.w <= 0.f) {
            if (retries_ < 30) return Step::Retry;
            fail("fen\xC3\xAAtre des nouveaut\xC3\xA9s : rien \xC3\xA0 cliquer pour " + cmd + " " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // ---- fin 1.10 (chantier P) ----
    // ---- Lot API 8 : didacticiels et aide ----
    //  aide-nouveautes                  Aide > Nouveautes du lot 8 : l'onglet de leurs parcours (verifie ses 8 cartes)
    //  aide-nouveautes-carte api-theme  cliquer le bouton d'une carte de cet onglet (vraie souris)
    //  aide-raccourcis                  Aide > Raccourcis clavier : l'aide generale, a la page des raccourcis
    //  aide-page sim-debogage           l'aide generale a cette page (une ancre de ui::buildHelp)
    //  aide-section "Raccourcis clavier"  verifier la section que montre l'aide generale ouverte
    //  (les parcours du lot 8 se lancent comme les autres : parcours api-simuler, parcours-clic "Suivant"...)
    if (cmd == "aide-nouveautes" || cmd == "aide-nouveautes-carte" || cmd == "aide-raccourcis" || cmd == "aide-page"
        || cmd == "aide-section") {
        auto* topRoot = top();
        TopBar* bar = nullptr;
        if (topRoot) walk(*topRoot, [&](ui::Widget& x) { if (!bar) bar = dynamic_cast<TopBar*>(&x); });
        if (cmd == "aide-raccourcis" || cmd == "aide-page") {
            if (cmd == "aide-raccourcis") {
                if (!bar) { fail("barre du haut introuvable (" + cmd + ")"); return Step::Next; }
                bar->trigger(lot8::kShortcutsAction);
            } else {
                if (arg(1).empty()) { fail("aide-page : quelle page ?"); return Step::Next; }
                lot8::setPendingHelpAnchor(arg(1));
                app_.menus().PushMenu("help");
            }
            return Step::Yield;
        }
        if (cmd == "aide-section") {
            ui::HelpView* view = nullptr;
            if (topRoot) walk(*topRoot, [&](ui::Widget& x) { if (!view) view = dynamic_cast<ui::HelpView*>(&x); });
            if (!view) {
                if (retries_ < 3) return Step::Retry;
                fail("l'aide g\xC3\xA9n\xC3\xA9rale n'est pas ouverte (" + cmd + ")");
                return Step::Next;
            }
            if (view->currentSection() != arg(1)) fail("l'aide montre \"" + view->currentSection() + "\", pas \"" + arg(1) + "\"");
            return Step::Next;
        }
        HmiTrailCards* cards = nullptr;
        if (topRoot) walk(*topRoot, [&](ui::Widget& x) {
            if (auto* c = dynamic_cast<HmiTrailCards*>(&x); !cards && c && x.id() == "analysis.api.nouveautes.volet.cartes") cards = c;
        });
        if (!cards || cards->cards().empty()) {
            if (!bar || retries_ >= 3) { fail("onglet des nouveaut\xC3\xA9s introuvable (" + cmd + ")"); return Step::Next; }
            if (!cards) bar->trigger(lot8::kNewsAction);
            return Step::Retry;
        }
        if (cmd == "aide-nouveautes") {
            if (cards->cards().size() != lot8::trails().size())
                fail("l'onglet des nouveaut\xC3\xA9s montre " + std::to_string(cards->cards().size()) + " cartes, pas "
                     + std::to_string(lot8::trails().size()));
            return Step::Next;
        }
        gfx::Rect r{};
        if (!cards->buttonRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("carte des nouveaut\xC3\xA9s introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // ---- fin Lot API 8 : didacticiels et aide ----
    // ---- lot 21 : le didacticiel en parcours -----------------------------------
    //  parcours "symbole" [debut]       lancer un parcours (par sa carte : l'aide de l'IHM ouverte ;
    //                                   "debut" : depuis la premiere etape, meme s'il est en cours)
    //  parcours-carte "symbole"         cliquer le bouton de sa carte (vraie souris)
    //  parcours-clic "Montre-moi"       un bouton de la bulle : Suivant, Terminer, Precedent, Passer,
    //                                   Montre-moi, Garder, Tout defaire (vraie souris)
    //  parcours-etape 4                 attendre l'etape 4 du parcours ouvert
    //  parcours-fin                     attendre qu'il soit ferme ; parcours-fini : verifier qu'il l'est
    if (cmd.rfind("parcours", 0) == 0) {
        auto* topRoot = top();
        auto* tuto = topRoot ? dynamic_cast<HmiTutorial*>(topRoot->findById("analysis.hmiTutorial")) : nullptr;
        if (cmd == "parcours" || cmd == "parcours-carte") {
            // Lot API 7 : les parcours de l'API ("api-...") ont leur onglet, API > Didacticiel.
            if (arg(1).rfind("api-", 0) == 0) {
                HmiTrailCards* cards = nullptr;
                if (topRoot) walk(*topRoot, [&](ui::Widget& x) {
                    if (auto* c = dynamic_cast<HmiTrailCards*>(&x); !cards && c && x.id() == "analysis.api.didacticiel.volet.cartes") cards = c;
                });
                if (!cards) {
                    TopBar* bar = nullptr;
                    if (topRoot) walk(*topRoot, [&](ui::Widget& x) { if (!bar) bar = dynamic_cast<TopBar*>(&x); });
                    if (!bar || retries_ >= 3) { fail("onglet API \xC2\xB7 Didacticiel introuvable (" + cmd + ")"); return Step::Next; }
                    bar->trigger("help.apiTutorial");
                    return Step::Retry;
                }
                if (cmd == "parcours") {
                    if (has("debut")) { cards->startRequested->emit(arg(1), false); return Step::Yield; }
                    if (!cards->press(arg(1))) fail("parcours inconnu : " + arg(1));
                    return Step::Yield;
                }
                gfx::Rect r{};
                if (!cards->buttonRect(arg(1), r)) {
                    if (retries_ < 3) return Step::Retry;
                    fail("carte de parcours introuvable : " + arg(1));
                    return Step::Next;
                }
                click(centre(r), MouseButton::Left, 1, {});
                return Step::Yield;
            }
            HmiHelpPane* help = nullptr;
            if (topRoot) walk(*topRoot, [&](ui::Widget& x) {
                if (auto* h = dynamic_cast<HmiHelpPane*>(&x); !help && h) help = h;
            });
            if (!help) { fail("l'aide de l'IHM n'est pas ouverte (F1 dans l'IHM) : " + cmd); return Step::Next; }
            if (help->current() != "didacticiel") {
                help->show("didacticiel");
                return Step::Retry;
            }
            if (cmd == "parcours") {
                // "debut" : depuis la premiere etape, ou qu'on en soit (une ancienne
                // session qui rejoue la visite) ; sinon la carte decide : Reprendre si
                // le parcours est en cours, Commencer / Refaire.
                if (has("debut")) {
                    help->trails().startRequested->emit(arg(1), false);
                    return Step::Yield;
                }
                if (!help->trails().press(arg(1))) fail("parcours inconnu : " + arg(1));
                return Step::Yield;
            }
            gfx::Rect r{};
            if (!help->trails().buttonRect(arg(1), r)) {
                if (retries_ < 3) return Step::Retry;
                fail("carte de parcours introuvable : " + arg(1));
                return Step::Next;
            }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (!tuto) { fail("pas de didacticiel ici (" + cmd + ")"); return Step::Next; }
        if (cmd == "parcours-etape") {
            const auto want = static_cast<std::size_t>(std::max(1.f, num(1)));
            if (tuto->active() && tuto->step() + 1 == want) return Step::Next;
            if (retries_ < 600) return Step::Retry;
            fail("le parcours n'est pas a l'etape " + arg(1) + " (etape " + std::to_string(tuto->step() + 1) + ", "
                 + (tuto->active() ? "ouvert" : "ferme") + ")");
            return Step::Next;
        }
        if (cmd == "parcours-fin") return tuto->active() ? Step::Retry : Step::Next;
        if (cmd == "parcours-fini") {
            if (tuto->active()) fail("le parcours est encore ouvert (etape " + std::to_string(tuto->step() + 1) + ")");
            return Step::Next;
        }
        if (cmd == "parcours-clic") {
            if (!tuto->active()) { fail("aucun parcours ouvert (" + cmd + ")"); return Step::Next; }
            const std::string what = lower(arg(1));
            using Part = HmiTutorial::Part;
            Part part = Part::Next;
            if (what.rfind("pr", 0) == 0) part = Part::Previous;
            else if (what.rfind("passer", 0) == 0) part = Part::Skip;
            else if (what.rfind("montre", 0) == 0) part = Part::ShowMe;
            else if (what.rfind("garder", 0) == 0) part = Part::Keep;
            else if (what.rfind("tout", 0) == 0) part = Part::UndoAll;
            const gfx::Rect r = tuto->partRect(part);
            if (r.empty()) {
                if (retries_ < 30) return Step::Retry;     // la bulle se dessine d'abord
                fail("bouton absent de la bulle : " + arg(1));
                return Step::Next;
            }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        fail("commande de parcours inconnue : " + cmd);
        return Step::Next;
    }
    // Lot 20 : la galerie de Nouvelle vue - une source (0, 1, 2 ou son libelle :
    // "Mes modeles"), une vignette par son nom (double : creer tout de suite).
    if (cmd == "galerie-source" || cmd == "galerie-modele") {
        auto* gallery = dynamic_cast<HmiTemplateGallery*>(app_.menus().top());
        if (!gallery) { fail("la galerie n'est pas ouverte"); return Step::Next; }
        gfx::Rect r{};
        if (cmd == "galerie-source") {
            int source = -1;
            if (isNumber(arg(1))) source = std::atoi(arg(1).c_str());
            else if (arg(1).rfind("Mes", 0) == 0) source = 1;
            else if (arg(1).find("projet") != std::string::npos) source = 2;
            else source = 0;
            r = gallery->sourceRect(source);
        } else {
            r = gallery->cardRect(arg(1));
        }
        if (r.w <= 0.f) { fail("pas dans la galerie : " + arg(1)); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, {});
        if (has("double")) click(centre(r), MouseButton::Left, 2, {});
        return Step::Yield;
    }
    if (cmd == "onglets-liste") {
        // Lot 19 : la liste de tous les sous-onglets (le bouton a droite de la
        // bande, quand ils ne tiennent pas) ; "choisir" y prend un onglet.
        auto* page = currentPage();
        ui::TabControl* strip = nullptr;
        if (page)
            walk(*page, [&](ui::Widget& x) {
                if (auto* t = dynamic_cast<ui::TabControl*>(&x); !strip && t && shown(*t) && t->tabCount() > 1) strip = t;
            });
        if (!strip) { fail("aucune bande de sous-onglets ici"); return Step::Next; }
        const auto b = strip->listButtonRect();
        if (b.w > 0.f) click({b.x + b.w * 0.5f, b.y + b.h * 0.5f}, MouseButton::Left, 1, {});
        else strip->openTabList();
        return Step::Yield;
    }
    // Lot API 7 : les onglets detaches, chacun dans sa fenetre (DetachedWindows).
    //   fenetres-detachees [N]                  la liste dans le journal ; N : le nombre attendu
    //   fenetre-rattacher "Titre" | *           la remettre en onglet (* ou rien : toutes)
    //   capture-fenetre "Titre" fichier.png     l'image de SA fenetre (dossier des captures)
    //   fenetre-clic "Titre" x y [droit] [double] [ctrl] [maj]
    //                                           un clic dans sa fenetre, en pixels de sa
    //                                           surface (la bande du haut comprise)
    //  "Titre" : le titre de l'onglet detache, ou son debut.
    if (cmd == "fenetres-detachees") {
        const auto titles = app_.detachedWindows().titles();
        std::printf("[script] fen\xC3\xAAtres d\xC3\xA9tach\xC3\xA9" "es : %zu\n", titles.size());
        for (const auto& t : titles) std::printf("[script]   %s\n", t.c_str());
        std::fflush(stdout);
        if (isNumber(arg(1)) && static_cast<std::size_t>(std::atoi(arg(1).c_str())) != titles.size())
            fail("fenetres-detachees : " + std::to_string(titles.size()) + " au lieu de " + arg(1));
        return Step::Next;
    }
    if (cmd == "fenetre-rattacher") {
        auto& windows = app_.detachedWindows();
        if (arg(1).empty() || arg(1) == "*") {
            windows.giveBackAll();
        } else if (const auto* page = windows.findByTitle(arg(1))) {
            (void)windows.giveBack(page);
        } else {
            fail("fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e introuvable : " + arg(1));
            return Step::Next;
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "capture-fenetre") {
        if (arg(2).empty()) { fail("capture-fenetre \"Titre\" fichier.png"); return Step::Next; }
        if (sinceInput_ < 3) return Step::Retry;           // que la fenetre ait fini de se mettre en place
        auto& windows = app_.detachedWindows();
        const auto* page = windows.findByTitle(arg(1));
        if (!page) { fail("fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e introuvable : " + arg(1)); return Step::Next; }
        const auto path = (capturesDir_ / arg(2)).string();
        std::string why;
        if (!windows.capturePng(page, path, &why)) fail("capture-fenetre " + path + " : " + why);
        else std::printf("[script] capture %s\n", path.c_str());
        return Step::Next;
    }
    if (cmd == "fenetre-clic") {
        auto& windows = app_.detachedWindows();
        const auto* page = windows.findByTitle(arg(1));
        if (!page) { fail("fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e introuvable : " + arg(1)); return Step::Next; }
        const gfx::Point p{num(2), num(3)};
        const auto b = has("droit") ? MouseButton::Right : MouseButton::Left;
        const KeyMods m = mods();
        // Comme "clic" : la souris y va, appuie, relache (deux fois pour "double").
        // Un clic qui ramene l'onglet ferme la fenetre : la suite ne va nulle part.
        (void)windows.sendEvent(page, ui::MouseMove{p, {}, m});
        for (int n = 1; n <= (has("double") ? 2 : 1); ++n) {
            (void)windows.sendEvent(page, ui::MouseDown{p, b, n, m});
            (void)windows.sendEvent(page, ui::MouseUp{p, b, m});
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- Lot API 8 : les dialogues dans la fenetre detachee -----------------
    //  Un geste fait DANS la fenetre d'un onglet detache (par elle, comme
    //  fenetre-clic) : un dialogue qu'il demande s'ouvre dans cette fenetre.
    //  Les commandes des dialogues (bouton, champ, touche, texte, renommer-...,
    //  import-...) le pilotent ou qu'il soit : elles vont au haut de la pile.
    //   fenetre-outil "Titre" "Libelle"         un outil de la barre de sa page (libelle ou
    //                                           infobulle, comme "outil"), clique dans sa fenetre
    //   fenetre-ligne "Titre" "texte" [exact] [droit] [double]
    //                                           une ligne d'un tableau de sa page, cliquee dans
    //                                           sa fenetre (comme "ligne")
    //   fenetre-touche "Titre" touche           une touche dans sa fenetre (au dialogue qui y
    //                                           est ouvert, sinon a sa page) : f2, escape...
    //   dialogue-fenetre "Titre" | principale | aucun
    //                                           ou attend la question (le dialogue du dessus) :
    //                                           le journal le dit ; echec si ce n'est pas la
    if (cmd == "fenetre-outil" || cmd == "fenetre-ligne" || cmd == "fenetre-touche") {
        auto& windows = app_.detachedWindows();
        ui::Widget* page = windows.pageByTitle(arg(1));
        if (!page) { fail("fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e introuvable : " + arg(1)); return Step::Next; }
        // Par la fenetre, comme un vrai geste : le dialogue demande sait d'ou il vient.
        const auto sendClick = [&](gfx::Point p, MouseButton b, int clicks, KeyMods m) {
            (void)windows.sendEvent(page, ui::MouseMove{p, {}, m});
            (void)windows.sendEvent(page, ui::MouseDown{p, b, clicks, m});
            (void)windows.sendEvent(page, ui::MouseUp{p, b, m});
        };
        if (cmd == "fenetre-touche") {
            Key k{};
            KeyMods m;
            if (!parseKey(arg(2), k, m)) { fail("touche inconnue : " + arg(2)); return Step::Next; }
            (void)windows.sendEvent(page, ui::KeyDown{k, m, false});
            (void)windows.sendEvent(page, ui::KeyUp{k, m});
            sinceInput_ = 0;
            return Step::Yield;
        }
        if (cmd == "fenetre-outil") {
            gfx::Rect r{};
            walk(*page, [&](ui::Widget& x) {
                auto* strip = dynamic_cast<HmiToolStrip*>(&x);
                if (r.w > 0.f || !strip || !shown(*strip)) return;
                if (const int a = strip->actionByTip(arg(2)); a >= 0) r = strip->rectOf(a);
            });
            if (r.w <= 0.f) { fail("outil introuvable dans la fen\xC3\xAAtre " + arg(1) + " : " + arg(2)); return Step::Next; }
            sendClick(centre(r), MouseButton::Left, 1, {});
            sinceInput_ = 0;
            return Step::Yield;
        }
        // fenetre-ligne : la premiere ligne dont une case contient le texte ("exact" :
        // la premiere colonne egale au texte) ; hors de la zone visible, amenee en vue
        // puis cliquee au pas suivant.
        const bool exact = has("exact");
        gfx::Rect r{};
        bool ok = false, offscreen = false;
        walk(*page, [&](ui::Widget& x) {
            auto* table = dynamic_cast<ui::TableView*>(&x);
            if (ok || offscreen || !table || !shown(*table) || !table->model()) return;
            for (std::size_t i = 0; i < table->visibleRowCount() && !ok && !offscreen; ++i)
                for (std::size_t c = 0; c < table->model()->columnCount(); ++c) {
                    const std::string cell = table->model()->cellText(table->viewRow(i), c);
                    if (exact ? (c != 0 || cell != arg(2)) : cell.find(arg(2)) == std::string::npos) continue;
                    ok = table->rowRect(i, r);
                    if (!ok) {
                        offscreen = true;
                        table->selectModelRows({table->viewRow(i)}, false);
                    }
                    break;
                }
        });
        if (!ok && offscreen && retries_ < 3) return Step::Retry;
        if (!ok) { fail("ligne introuvable dans la fen\xC3\xAAtre " + arg(1) + " : " + arg(2)); return Step::Next; }
        const gfx::Point p{r.x + 40.f, r.y + r.h * 0.5f};
        sendClick(p, has("droit") ? MouseButton::Right : MouseButton::Left, 1, mods());
        if (has("double")) sendClick(p, MouseButton::Left, 2, mods());
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "dialogue-fenetre") {
        auto& menus = app_.menus();
        auto& windows = app_.detachedWindows();
        const bool waiting = menus.questionWaiting();
        const auto where = menus.questionWindow();
        const std::string place = !waiting ? std::string("aucun")
                                : where == menu::MenuManager::kMainWindow ? std::string("principale")
                                : windows.titleOfWindow(where);
        const std::string want = lower(arg(1));
        bool ok = false;
        if (want == "aucun") ok = !waiting;
        else if (want == "principale") ok = waiting && where == menu::MenuManager::kMainWindow;
        else if (const auto* page = windows.findByTitle(arg(1))) ok = waiting && windows.windowIdOf(page) == where;
        // Le dialogue s'ouvre (ou se ferme) a l'image suivante : un peu de patience.
        if (!ok && retries_ < 30) return Step::Retry;
        std::printf("[script] dialogue : %s%s%s\n", place.c_str(), waiting && menus.top() ? " - " : "",
                    waiting && menus.top() ? menus.top()->id().c_str() : "");
        std::fflush(stdout);
        if (!ok) fail("dialogue-fenetre : attendu " + arg(1) + ", la question est : " + place);
        return Step::Next;
    }
    // ---- fin lot API 8 ----
    // Lot 7 : RENOMMER EN VOYANT TOUT CE QUI SUIT (le dialogue RenameDialog).
    //   renommer "variable" "Vitesse" "Regime"   le dialogue s'ouvre (l'ecran d'analyse au-dessus),
    //                                            le nouveau nom tape ; genres : variable ("Unite.var"
    //                                            pour une variable d'unite), ddt, dfb, unite, section,
    //                                            table, ihm-variable, ihm-vue. Dialogue deja ouvert :
    //                                            seulement taper le nom (le 3e mot).
    //   renommer-onglet "API"                    Tout, API, IHM, Tables
    //   renommer-ligne "texte"                   une ligne de l'arbre (le detail en bas)
    //   renommer-liees oui|non                   la case "Aussi renommer ... liee"
    //   renommer-confirmer | renommer-annuler
    if (cmd == "renommer" || cmd.rfind("renommer-", 0) == 0) {
        auto* dialog = dynamic_cast<RenameDialog*>(app_.menus().top());
        if (cmd == "renommer") {
            if (dialog) {
                dialog->typeName(arg(3));
                return Step::Yield;
            }
            auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
            if (!screen) { fail("renommer : l'\xC3\xA9" "cran d'analyse n'est pas au-dessus"); return Step::Next; }
            std::string why;
            if (!screen->askRenameTo(arg(1), arg(2), arg(3), &why)) { fail("renommer : " + why); return Step::Next; }
            return Step::Retry;   // le dialogue s'ouvre a l'image suivante ; alors le nom est la
        }
        if (!dialog) { fail(cmd + " : le dialogue Renommer n'est pas ouvert"); return Step::Next; }
        if (cmd == "renommer-onglet") {
            if (!dialog->showTab(arg(1))) fail("renommer-onglet : onglet inconnu " + arg(1) + " (Tout, API, IHM, Tables)");
            return Step::Yield;
        }
        if (cmd == "renommer-ligne") {
            if (!dialog->selectRow(arg(1))) fail("renommer-ligne : aucune ligne ne contient " + arg(1));
            return Step::Yield;
        }
        if (cmd == "renommer-liees") {
            if (!dialog->setWithLinks(lower(arg(1)) != "non")) fail("renommer-liees : pas de variable li\xC3\xA9" "e \xC3\xA0 renommer");
            return Step::Yield;
        }
        if (cmd == "renommer-confirmer") {
            std::string why;
            if (!dialog->confirm(&why)) fail("renommer-confirmer : " + why);
            return Step::Yield;
        }
        if (cmd == "renommer-annuler") {
            dialog->cancel();
            return Step::Yield;
        }
        fail("commande inconnue : " + cmd);
        return Step::Next;
    }
    // ---- Lot API 8 : renommer - un champ de DDT, F2 dans les tables ----
    //   renommer "ddt-champ" "config_gaz.Nom_gaz" "Nom_du_gaz"   le genre ddt-champ ("Type.champ") par la
    //                                            commande renommer ci-dessus (le dialogue, le nom tape)
    //   types-champ-renommer "config_gaz.Nom_gaz" ["Nom_du_gaz"]   API > Types derives (ouvert s'il ne l'est
    //                                            pas) : la ligne du champ choisie, puis ce que fait F2 (sans nom)
    //                                            ou le nom tape dans la case Nom de la grille (avec) - le dialogue
    //   ihm-variable-renommer "Pompe_Marche" ["Pompe_En_Marche"]   IHM > Variables IHM (l'onglet courant) :
    //                                            la variable choisie, puis ce que font F2 et le double-clic sur le
    //                                            nom (sans nom), ou le nom tape dans la case Nom (avec) - le dialogue
    if (cmd == "types-champ-renommer") {
        const auto dot = arg(1).find('.');
        auto* pane = apiPaneOf<DerivedTypesPane>(app_, "types", true);
        if (!pane || dot == std::string::npos) { fail(cmd + " : \"Type.champ\" dans API > Types d\xC3\xA9riv\xC3\xA9s"); return Step::Next; }
        if (!pane->selectField(arg(1).substr(0, dot), arg(1).substr(dot + 1))) { fail(cmd + " : champ introuvable : " + arg(1)); return Step::Next; }
        if (arg(2).empty()) {
            if (!pane->renameSelected()) fail(cmd + " : rien \xC3\xA0 renommer");
        } else if (!requestRename("ddt-champ", arg(1), arg(2))) {
            fail(cmd + " : l'\xC3\xA9" "cran d'analyse n'est pas au-dessus");
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "ihm-variable-renommer") {
        HmiVariablesPane* pane = nullptr;
        if (auto* page = currentPage())
            walk(*page, [&](ui::Widget& x) {
                if (auto* p = dynamic_cast<HmiVariablesPane*>(&x); p && !pane && shown(*p)) pane = p;
            });
        if (!pane) { fail(cmd + " : ouvre d'abord IHM > Variables IHM"); return Step::Next; }
        const auto* v = app_.hmi() ? app_.hmi()->project.variable(arg(1)) : nullptr;
        if (!v) { fail(cmd + " : variable IHM introuvable : " + arg(1)); return Step::Next; }
        pane->selectVariable(v->id);
        const bool opened = arg(2).empty() ? pane->renameSelectedInDialog() : pane->renameInDialog(v->id, arg(2));
        if (!opened) fail(cmd + " : le dialogue ne s'ouvre pas (l'\xC3\xA9" "cran d'analyse n'est pas au-dessus)");
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : renommer ----
    // ---- Lot API 8 : renommer partout (IHM) ----
    //   ihm-objet-renommer "Voyant_1" "Voyant_Pompe"   la vue IHM ouverte (l'onglet courant) : l'objet renomme
    //                                            comme par F2 / double-clic dans la liste des objets ou la case
    //                                            Nom des proprietes - ce qui le cite (Vue.Objet...) suit
    if (cmd == "ihm-objet-renommer") {
        auto* ed = currentEditor();
        const auto hdoc = app_.hmi();
        const auto* vw = ed && hdoc ? hdoc->project.view(ed->viewId()) : nullptr;
        const auto* ob = vw ? vw->objectByName(arg(1)) : nullptr;
        if (!ed || !ob) { fail(cmd + " : objet introuvable dans la vue ouverte : " + arg(1)); return Step::Next; }
        const hmi::Id oid = ob->id;
        std::string why;
        if (!ed->renameObject(oid, arg(2), &why)) fail(cmd + " : " + why);
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : renommer partout (IHM) ----
    // ---- Lot API 8 : les expressions impossibles ----
    //   ihm-expression-choisir [erreur]          la vue IHM ouverte : le premier objet dont une propriete est une
    //                                            expression (avec "erreur" : une expression qui ne peut pas marcher)
    //                                            est choisi, la grille defile jusqu'a elle et la souris se pose sur
    //                                            sa valeur (la pastille fx, l'infobulle : expression, erreur)
    //   ihm-rapport-verifier "texte" [N]         l'onglet IHM > Compiler ou Generer ouvert : au moins N (1) erreurs
    //                                            dont le message contient ce texte, sinon la session echoue
    //   (Compiler : arbre "IHM/Compiler" ; aller a la source : ligne "<objet>" double)
    if (cmd == "ihm-expression-choisir") {
        auto* ed = currentEditor();
        const auto hdoc = app_.hmi();
        const auto* vw = ed && hdoc ? hdoc->project.view(ed->viewId()) : nullptr;
        if (!vw) { fail(cmd + " : ouvre d'abord une vue IHM"); return Step::Next; }
        const bool broken = arg(1) == "erreur";
        hmi::Id oid = hmi::kNoId;
        std::string key;
        for (const auto& o : vw->objects) {
            for (const auto& pr : o.props) {
                if (pr.expr.empty()) continue;
                if (broken && hmiExpressionError(*vw, pr.key, pr.expr, app_.project().get(), &hdoc->project).empty()) continue;
                oid = o.id;
                key = pr.key;
                break;
            }
            if (oid != hmi::kNoId) break;
        }
        if (oid == hmi::kNoId) {
            fail(cmd + " : aucun objet de la vue n'a d'expression" + std::string(broken ? " en erreur" : ""));
            return Step::Next;
        }
        if (ed->canvas().selection() != std::vector<hmi::Id>{oid}) ed->canvas().setSelection({oid});
        std::string label, help;
        if (!hmiPropertyInfo(key, label, help)) label = key;
        const std::string name = label + "  \xC6\x92";
        gfx::Rect r{};
        if (!ed->properties().valueRect(name, r)) {
            (void)ed->properties().revealValue(name);
            if (retries_ < 3) return Step::Retry;
            fail(cmd + " : la propriete " + label + " n'est pas dans la grille");
            return Step::Next;
        }
        moveTo({r.x + 24.f, r.y + r.h * 0.5f}, {});
        sinceInput_ = 0;
        // ---- Lot API 8 : corrections des captures ---- l'infobulle promise
        // n'apparaissait jamais : "attendre N" compte des IMAGES (1/30 s chacune
        // en script) et l'infobulle attend 0,9 s immobile (27 images) ;
        // "attendre 4" puis "attendre 3" ne font que 0,23 s. La commande attend
        // elle-meme 32 images, la souris posee : l'infobulle est ouverte ensuite.
        wait_ = 32;
        return Step::Yield;
    }
    if (cmd == "ihm-rapport-verifier") {
        HmiReportPane* pane = nullptr;
        if (auto* page = currentPage())
            walk(*page, [&](ui::Widget& x) {
                if (auto* p = dynamic_cast<HmiReportPane*>(&x); p && !pane) pane = p;
            });
        if (!pane) { fail(cmd + " : ouvre d'abord IHM > Compiler ou IHM > G\xC3\xA9n\xC3\xA9rer"); return Step::Next; }
        const long want = arg(2).empty() ? 1L : std::max(1L, std::atol(arg(2).c_str()));
        long n = 0;
        for (const auto& i : pane->issues())
            if (i.severity == hmi::Issue::Severity::Error && i.message.find(arg(1)) != std::string::npos) ++n;
        if (n < want) fail(cmd + " : " + std::to_string(n) + " erreur(s) contenant \"" + arg(1) + "\", " + std::to_string(want) + " attendue(s)");
        return Step::Next;
    }
    // 1.10.4 (K1) : ihm-rapport-avertissement TEXTE [N] - au moins N avertissements qui le
    // contiennent ; ihm-rapport-remplacer TEXTE - la ligne qui le contient, puis "Remplacer...".
    if (cmd == "ihm-rapport-avertissement" || cmd == "ihm-rapport-remplacer") {
        HmiReportPane* pane = nullptr;
        if (auto* page = currentPage())
            walk(*page, [&](ui::Widget& x) {
                if (auto* p = dynamic_cast<HmiReportPane*>(&x); p && !pane) pane = p;
            });
        if (!pane) { fail(cmd + " : ouvre d'abord IHM > Compiler"); return Step::Next; }
        if (cmd == "ihm-rapport-avertissement") {
            const long want = arg(2).empty() ? 1L : std::max(1L, std::atol(arg(2).c_str()));
            long n = 0;
            for (const auto& i : pane->issues())
                if (i.severity == hmi::Issue::Severity::Warning && i.message.find(arg(1)) != std::string::npos) ++n;
            if (n < want) fail(cmd + " : " + std::to_string(n) + " avertissement(s) contenant \"" + arg(1) + "\"");
            return Step::Next;
        }
        for (std::size_t r = 0; r < pane->issues().size(); ++r)
            if (pane->issues()[r].message.find(arg(1)) != std::string::npos) {
                pane->table().selectModelRows({static_cast<ui::RowIndex>(r)});
                break;
            }
        if (!pane->replaceSelected()) fail(cmd + " : aucune ligne \"" + arg(1) + "\" a remplacer");
        return Step::Yield;
    }
    // ---- fin Lot API 8 : les expressions impossibles ----
    if (cmd == "annuler" || cmd == "retablir") {
        const int n = arg(1).empty() ? 1 : std::max(1, std::atoi(arg(1).c_str()));
        for (int i = 0; i < n; ++i) {
            if (cmd == "annuler") app_.undo();
            else app_.redo();
        }
        return Step::Yield;
    }
    if (cmd == "fermer-fenetre") {
        app_.requestClose();
        return Step::Yield;
    }
    if (cmd == "sous-onglet") {
        // Un onglet a l'interieur de l'onglet courant (l'inspecteur Proprietes /
        // Actions, le volet Expressions / Journal / Variables IHM...).
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        gfx::Rect r{};
        bool ok = false;
        ui::TabControl* owner = nullptr;
        std::size_t index = 0;
        // 1.11.18 : le titre exact d'abord ("Variables" : l'onglet du code, pas "Variables IHM"),
        // puis le debut du titre.
        for (int pass = 0; pass < 2 && !ok; ++pass)
            walk(*page, [&](ui::Widget& x) {
                auto* tabs = dynamic_cast<ui::TabControl*>(&x);
                if (ok || !tabs || !shown(*tabs)) return;
                for (std::size_t i = 0; i < tabs->tabCount() && !ok; ++i)
                    if ((pass == 0 ? tabs->tab(i)->title == arg(1) : startsWith(tabs->tab(i)->title, arg(1))) && tabs->headerRect(i, r)) {
                        ok = true;
                        owner = tabs;
                        index = i;
                    }
            });
        if (!ok) {
            // 1.9 : les sous-onglets du document d'un symbole (Dessin | Alarmes | Instances).
            HmiSymbolTabs* symTabs = nullptr;
            walk(*page, [&](ui::Widget& x) {
                auto* t = dynamic_cast<HmiSymbolTabs*>(&x);
                if (symTabs || !t || !shown(*t)) return;
                for (int i = HmiSymbolTabs::Drawing; i <= HmiSymbolTabs::Popups && !symTabs; ++i)   // 1.11.10 : jusqu'a Popups
                    if (startsWith(t->label(i), arg(1)) && t->tabRect(i, r)) symTabs = t;
            });
            if (symTabs) {
                click({r.x + r.w * 0.5f, r.y + r.h * 0.5f}, MouseButton::Left, 1, {});
                return Step::Yield;
            }
        }
        if (!ok) { fail("sous-onglet introuvable : " + arg(1)); return Step::Next; }
        // Lot 13 : la barre defile - un en-tete hors de vue est choisi directement.
        if (const auto b = owner->bounds(); r.x < b.x || r.x + 24.f > b.right()) { owner->setCurrentIndex(index); return Step::Yield; }
        click({r.x + 24.f, r.y + r.h * 0.5f}, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "editeur") {
        // L'editeur de code de l'onglet courant : le texte remplace tout (ou
        // s'ajoute a la fin : "ajout"). \n : une nouvelle ligne, \t : 4 espaces.
        auto* page = currentPage();
        if (!page) { fail("aucun onglet"); return Step::Next; }
        ui::MultiLineText* ed = nullptr;
        walk(*page, [&](ui::Widget& x) {
            if (auto* t = dynamic_cast<ui::MultiLineText*>(&x); !ed && t && shown(*t) && !t->readOnly()) ed = t;
        });
        if (!ed) { fail("aucun editeur de code modifiable ici"); return Step::Next; }
        std::string text;
        for (std::size_t i = 0; i < arg(1).size(); ++i) {
            const char c = arg(1)[i];
            if (c == '\\' && i + 1 < arg(1).size() && (arg(1)[i + 1] == 'n' || arg(1)[i + 1] == 't')) {
                text += arg(1)[++i] == 'n' ? std::string("\n") : std::string(4, ' ');
                continue;
            }
            text += c;
        }
        const auto b = ed->bounds();
        click({b.x + b.w * 0.6f, b.y + 12.f}, MouseButton::Left, 1, {});
        if (has("ajout")) {
            send(ui::KeyDown{Key::End, KeyMods{true, false, false, false}, false});
            send(ui::KeyDown{Key::End, {}, false});
        } else {
            send(ui::KeyDown{Key::A, KeyMods{true, false, false, false}, false});
            send(ui::KeyUp{Key::A, KeyMods{true, false, false, false}});
        }
        send(ui::TextInput{text});
        send(ui::KeyDown{Key::Escape, {}, false});     // la liste de completion, si elle s'est ouverte
        send(ui::KeyUp{Key::Escape, {}});
        return Step::Yield;
    }
    if (cmd == "sim-partie") {
        // Lot 6 : une partie d'un gestionnaire de recettes de la vue simulee :
        // sim-partie "Gestion_Recettes" "bouton:Ajouter" | "ligne:1" (0 = le premier jeu).
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (!pane->canvas().partRect(arg(1), arg(2), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("partie introuvable dans la vue simulee : " + arg(1) + " " + arg(2));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot 10 : une partie du menu natif Parametres systeme de la vue simulee -
    // sim-menu "plus:luminosite" | "onglet:diagnostic" | "heure:plus:minute" |
    // "action:journal" | "fermer" | "dehors" ; sim-menu "molette" 3 : la molette
    // (3 crans vers le bas, -3 vers le haut) sur le diagnostic.
    if (cmd == "sim-menu") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (arg(1) == "molette") {
            r = pane->canvas().systemPanelRect();
            if (r.w <= 0.f) { fail("le menu Parametres systeme n'est pas ouvert"); return Step::Next; }
            moveTo(centre(r), {});
            send(ui::MouseWheel{centre(r), 0.f, -static_cast<float>(num(2)), {}});
            return Step::Yield;
        }
        if (!pane->canvas().systemPartRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("partie du menu Parametres systeme introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // ---- 1.9 : la page Simulation de Parametres systeme (la vue simulee ou le poste) ----
    //   page-simulation ["Variateur ATV320"]   l'ouvre (comme le bouton Page Simulation) et
    //                                          choisit l'esclave de cet equipement
    //   page-simulation-partie "animer:Vitesse_Affichee" | "source:1:esclave" | "forcer:Consigne_Variateur"
    //                                          | "forcee:Consigne_Variateur:plus" | "zone_max:%MW8504:moins"
    //                                          | "revenir" | "connecter" | "defiler:1"...   un clic sur cette
    //                                          partie (systemMenuHit) ; une ligne se nomme par sa variable
    //                                          ou par son adresse
    //   (Ctrl+Alt+S sur le poste : touche ctrl+alt+s)
    if (cmd == "page-simulation") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est ni la simulation IHM ni le poste (" + cmd + ")"); return Step::Next; }
        auto& rt = pane->runtime();
        if (!rt.systemShown() || rt.systemTab() != hmi::kSimulationTab) pane->openSystemMenu(hmi::kSimulationTab, "script (page-simulation)");
        if (!arg(1).empty()) {
            const auto slaves = rt.simSlaves(false);
            std::size_t found = slaves.size();
            for (std::size_t i = 0; i < slaves.size(); ++i)
                if (lower(slaves[i].equipment) == lower(arg(1)) || lower(slaves[i].key) == lower(arg(1))) found = i;
            if (found == slaves.size()) { fail("page-simulation : pas d'esclave simul\xC3\xA9 pour " + arg(1)); return Step::Next; }
            rt.systemPart("esclave:" + std::to_string(found), rt.now());
            pane->refreshNow();
        }
        return Step::Yield;
    }
    if (cmd == "page-simulation-partie") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est ni la simulation IHM ni le poste (" + cmd + ")"); return Step::Next; }
        std::string part = arg(1);
        gfx::Rect r{};
        if (!pane->canvas().systemPartRect(part, r)) {
            // Une ligne nommee par sa variable : sa cle (son adresse) dans la page.
            const auto colon = part.find(':');
            if (colon != std::string::npos) {
                const auto end = part.find(':', colon + 1);
                const std::string name = part.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1);
                const auto slaves = pane->runtime().simSlaves(true);
                if (!slaves.empty()) {
                    const auto& cur = slaves[pane->runtime().simChosenIndex(slaves)];
                    for (const auto& v : cur.values)
                        if (lower(v.variable) == lower(name)) {
                            part = part.substr(0, colon + 1) + v.key + (end == std::string::npos ? std::string{} : part.substr(end));
                            break;
                        }
                }
            }
        }
        if (!pane->canvas().systemPartRect(part, r)) {
            // Une ligne hors de l'ecran : la page defile jusqu'a elle, puis on reessaie.
            const auto colon = part.find(':');
            if (colon != std::string::npos && retries_ < 3) {
                const auto end = part.find(':', colon + 1);
                const std::string key = part.substr(colon + 1, end == std::string::npos ? std::string::npos : end - colon - 1);
                auto& rt = pane->runtime();
                const auto slaves = rt.simSlaves(true);
                if (!slaves.empty()) {
                    const auto& cur = slaves[rt.simChosenIndex(slaves)];
                    for (std::size_t i = 0; i < cur.values.size(); ++i)
                        if (cur.values[i].key == key) {
                            rt.systemPart("defiler:=" + std::to_string(i), rt.now());
                            pane->refreshNow();
                            break;
                        }
                }
            }
            if (retries_ < 3) return Step::Retry;
            fail("partie de la page Simulation introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // 1.9 : le volet Variables systeme et d'instances (l'onglet courant) -
    //   vars-dossier "Esclaves simul\xC3\xA9s" [replier]   deplie (ou replie) un dossier ; une structure
    //                                                 d'esclave : "Esclaves simul\xC3\xA9s/SYS.Slave.Variateur_ATV320"
    //   vars-choisir "SYS.Slave.Variateur_ATV320.Read"  choisit la ligne (ses dossiers se deplient)
    // 1.11.8 : le volet Variables IHM (l'onglet courant) -
    //   varihm deplier "V[0]" | replier "V[0]"    deplie (replie) une variable ou un membre compose
    //   varihm choisir "V[0].NOM"                 choisit la ligne (ses parents se deplient)
    //   varihm menu "V[0].NOM"                    le vrai clic droit sur sa ligne (le menu du volet)
    // 1.11.16 : la remanence d'exploitation -
    //   varihm remanente "Compteur" oui|non       la case Remanente (comme dans la table)
    //   varihm remanence-reinit ["Compteur"]      oublier ses valeurs gardees (rien : toutes)
    //   varihm remanence-exporter "f.csv"         .csv pour Excel, sinon le format du poste
    //   varihm remanence-importer "f.csv"         l'un ou l'autre ; le compte rendu est ecrit
    //   varihm remanence-etat ["texte"]           l'integrite du stockage ; avec un texte : l'exiger
    //   varihm fiche "[Categorie/]Ligne" ["texte"] une ligne de la fiche de la ligne choisie ; avec un texte : l'exiger
    if (cmd == "varihm") {
        HmiVariablesPane* pane = nullptr;
        if (auto* page = currentPage())
            walk(*page, [&](ui::Widget& x) {
                if (!pane) pane = dynamic_cast<HmiVariablesPane*>(&x);
            });
        if (!pane) { fail("l'onglet courant n'est pas IHM \xC2\xB7 Variables IHM (" + cmd + ")"); return Step::Next; }
        const std::string what = arg(1), path = arg(2);
        // ---- 1.11.16 : la remanence d'exploitation ----
        if (what == "remanente") {
            const auto* v = app_.hmi() ? app_.hmi()->project.variable(path) : nullptr;
            std::string why;
            if (!v) fail("varihm remanente : variable IHM introuvable : " + path);
            else if (!pane->setRetain(v->id, arg(3) == "oui" || arg(3) == "on", &why)) fail("varihm remanente : " + why);
            return Step::Yield;
        }
        if (what == "remanence-reinit") {
            const auto* v = path.empty() || !app_.hmi() ? nullptr : app_.hmi()->project.variable(path);
            if (!path.empty() && !v) { fail("varihm remanence-reinit : variable IHM introuvable : " + path); return Step::Next; }
            std::string why;
            std::size_t n = 0;
            if (!pane->resetRetained(v ? v->id : hmi::kNoId, &n, &why)) fail("varihm remanence-reinit : " + why);
            else std::printf("[script] remanence : %zu valeur(s) oubliee(s)\n", n);
            return Step::Yield;
        }
        if (what == "remanence-exporter" || what == "remanence-importer") {
            std::string target = path;
            if (!target.empty() && std::filesystem::path(target).is_relative()) target = (capturesDir_ / target).string();
            std::string why;
            const bool ok = what == "remanence-exporter" ? pane->exportRetained(target, &why) : pane->importRetained(target, &why);
            std::printf("[script] %s %s : %s\n", what.c_str(), target.c_str(), why.c_str());
            if (!ok) fail("varihm " + what + " : " + why);
            return Step::Yield;
        }
        if (what == "remanence-etat") {
            const std::string text = pane->retainIntegrity();
            std::printf("[script] remanence d'exploitation : %s\n", text.c_str());
            if (!path.empty() && text.find(path) == std::string::npos) fail("varihm remanence-etat : \"" + path + "\" absent de : " + text);
            return Step::Next;
        }
        if (what == "fiche") {
            // "Categorie/Ligne" (une ligne de meme nom dans deux categories : Initiale), ou "Ligne".
            std::string category, name = path;
            if (const auto slash = path.find('/'); slash != std::string::npos) {
                category = path.substr(0, slash);
                name = path.substr(slash + 1);
            }
            std::string found;
            bool seen = false;
            for (const auto& c : pane->properties().categories())
                if (category.empty() || c.name == category)
                    for (const auto& pr : c.properties)
                        if (!seen && pr.name == name) {
                            found = pr.value;
                            seen = true;
                        }
            std::printf("[script] fiche %s = %s\n", path.c_str(), seen ? found.c_str() : "(absente)");
            if (!seen) fail("varihm fiche : pas de ligne \"" + path + "\"");
            else if (!arg(3).empty() && found.find(arg(3)) == std::string::npos) fail("varihm fiche : " + path + " = " + found + ", sans \"" + arg(3) + "\"");
            return Step::Next;
        }
        if (what == "deplier" || what == "replier") {
            pane->setExpanded(path, what == "deplier");
            return Step::Yield;
        }
        if (what == "choisir" || what == "menu") {
            pane->selectPath(path);
            const int r = pane->rowOf(path);
            if (r < 0) { fail("varihm : ligne introuvable : " + path); return Step::Next; }
            if (what == "choisir") return Step::Yield;
            auto& t = pane->table();
            gfx::Rect rr{};
            bool ok = false;
            for (std::size_t i = 0; i < t.visibleRowCount() && !ok; ++i)
                if (t.viewRow(i) == static_cast<ui::RowIndex>(r)) ok = t.rowRect(i, rr);
            if (!ok) {
                if (retries_ < 3) return Step::Retry;
                fail("varihm menu : ligne hors de la vue : " + path);
                return Step::Next;
            }
            click({rr.x + 120.f, rr.y + rr.h * 0.5f}, MouseButton::Right, 1, {});
            return Step::Yield;
        }
        fail("varihm : " + what + " ? (deplier, replier, choisir, menu, remanente, remanence-reinit, remanence-exporter, remanence-importer, "
             "remanence-etat, fiche)");
        return Step::Next;
    }
    if (cmd == "vars-dossier" || cmd == "vars-choisir") {
        auto* pane = dynamic_cast<HmiPublicVarsPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas IHM \xC2\xB7 Variables syst\xC3\xA8me et d'instances (" + cmd + ")"); return Step::Next; }
        if (cmd == "vars-choisir") {
            if (!pane->selectPath(arg(1))) fail("vars-choisir : ligne introuvable : " + arg(1));
            return Step::Yield;
        }
        pane->setFolderOpen(pane->currentTab(), arg(1), !has("replier"));
        return Step::Yield;
    }
    // Lot 12 : une partie du menu natif de connexion de la vue simulee -
    // sim-connexion "suivant" | "champ:secret" | "bouton:connexion" |
    // "onglet:comptes" | "ligne:1" (le rang a l'ecran) | "bouton:groupe:suivant" |
    // "role:1,2" | "deconnexion:plus" | "fermer" | "dehors" ; sim-connexion
    // "molette" 3 : la molette sur les listes (Comptes, Journal).
    // Lot 15 : un utilisateur connecte d'office sur la marche (le poste, la
    // simulation) - sim-utilisateur "admin" "motdepasse".
    if (cmd == "sim-utilisateur") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        std::string why;
        if (!pane->runtime().login(arg(1), arg(2), pane->runtime().now(), &why)) fail("connexion de " + arg(1) + " : " + why);
        pane->refreshNow();
        return Step::Yield;
    }
    if (cmd == "sim-connexion") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (arg(1) == "molette") {
            r = pane->canvas().loginPanelRect();
            if (r.w <= 0.f) { fail("le menu de connexion n'est pas ouvert"); return Step::Next; }
            moveTo(centre(r), {});
            send(ui::MouseWheel{centre(r), 0.f, -static_cast<float>(num(2)), {}});
            return Step::Yield;
        }
        if (!pane->canvas().loginPartRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("partie du menu de connexion introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot 13 : une partie du panneau de signature de la vue simulee -
    // sim-signature "champ:motdepasse" | "motif:suivant" | "visa:suivant" |
    // "champ:visa" | "bouton:signer" | "bouton:annuler" | "fermer".
    if (cmd == "sim-signature") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (!pane->canvas().signaturePartRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("partie du panneau de signature introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot 13 : "Rester connecte" dans le bandeau de l'avertissement de deconnexion.
    if (cmd == "sim-rester") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        const auto r = pane->canvas().stayButtonRect();
        if (r.w <= 0.f) {
            if (retries_ < 3) return Step::Retry;
            fail("pas d'avertissement de d\xC3\xA9" "connexion \xC3\xA0 l'\xC3\xA9" "cran");
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot 13 : un pas de plus a l'essai choisi (IHM > Essais), apres le pas choisi -
    // essai-pas "Cliquer" "Btn_Purge" ["valeur"] ["attendu"] ; un Commentaire : son texte.
    if (cmd == "essai-pas") {
        auto* pane = dynamic_cast<HmiScenariosPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas IHM > Essais (" + cmd + ")"); return Step::Next; }
        const hmi::Id sc = pane->selectedScenario();
        if (sc == hmi::kNoId) { fail("aucun essai choisi (" + cmd + ")"); return Step::Next; }
        hmi::TestStep st;
        st.action = arg(1);
        st.target = arg(2);
        st.value = arg(3);
        st.expected = arg(4);
        if (hmi::stepKind(st.action) == hmi::StepKind::Comment) {
            st.note = st.target;
            st.target.clear();
        }
        if (hmi::stepKind(st.action) == hmi::StepKind::Unknown) { fail("action d'essai inconnue : " + st.action); return Step::Next; }
        const int at = pane->addStep(sc, st, pane->selectedStep());
        if (at < 0) fail("pas refus\xC3\xA9 : " + st.action);
        else pane->selectStep(at);
        return Step::Yield;
    }
    // Lot 13 : attendre que l'essai qui se joue dans la simulation soit fini.
    if (cmd == "essai-attendre") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        return pane->scenarioRunning() ? Step::Retry : Step::Yield;
    }
    // Lot 13 : un badge passe sur le lecteur (il tape le numero puis Entree) -
    // sim-badge "04A7C3E2".
    if (cmd == "sim-badge") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        pane->canvas().textTyped->emit(arg(1));
        pane->canvas().keyTyped->emit(static_cast<int>(hmi::EditKey::Enter));
        return Step::Yield;
    }
    // Lot 10 : l'ecran de l'IHM simulee seul (la vue a sa taille a l'ecran, sans le
    // reste de la fenetre) - capture-sim NOM.png.
    if (cmd == "capture-sim") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        if (sinceInput_ < 3) return Step::Retry;
        auto s = pane->canvas().screenRect();
        if (s.w <= 1.f || s.h <= 1.f) { fail("aucune vue simulee a l'ecran"); return Step::Next; }
        // Lot 12 : le clavier virtuel ouvert est de la capture (il deborde de l'ecran).
        if (const auto k = pane->canvas().keyboardRect(); k.w > 1.f && k.h > 1.f) {
            const float x0 = std::min(s.x, k.x), y0 = std::min(s.y, k.y);
            const float x1 = std::max(s.x + s.w, k.x + k.w), y1 = std::max(s.y + s.h, k.y + k.h);
            s = {x0, y0, x1 - x0, y1 - y0};
        }
        const auto path = (capturesDir_ / arg(1)).string();
        if (auto res = captureRegionToPng(renderer, path, static_cast<int>(s.x) - 6, static_cast<int>(s.y) - 6, static_cast<int>(s.w) + 12,
                                          static_cast<int>(s.h) + 12); !res)
            fail("capture " + path + " : " + res.error().message());
        else std::printf("[script] capture %s\n", path.c_str());
        return Step::Next;
    }
    // Lot 10 : un toucher sur l'ecran simule (le rallumer quand il est en veille).
    if (cmd == "sim-toucher") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        const auto sr = pane->canvas().screenRect();
        click({sr.x + sr.w * 0.5f, sr.y + sr.h * 0.5f}, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot 9 : tirer une commande de la vue simulee d'une partie a une autre -
    // sim-glisser "Curseur_Vitesse" "fraction:0.2" "fraction:0.8" [tenir] ; avec
    // "tenir", le bouton reste enfonce (la capture montre le geste en cours) et
    // sim-lacher le relache la ou est la souris.
    if (cmd == "sim-glisser") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect a{}, b{};
        if (!pane->canvas().partRect(arg(1), arg(2), a) || !pane->canvas().partRect(arg(1), arg(3), b)) {
            if (retries_ < 3) return Step::Retry;
            fail("partie introuvable dans la vue simulee : " + arg(1) + " " + arg(2) + " / " + arg(3));
            return Step::Next;
        }
        const auto from = centre(a), to = centre(b);
        if (!has("tenir")) { drag(from, to, {}); return Step::Yield; }
        moveTo(from, {});
        send(ui::MouseDown{from, MouseButton::Left, 1, {}});
        constexpr int kSteps = 8;
        for (int k = 1; k <= kSteps; ++k) {
            const float t = static_cast<float>(k) / kSteps;
            moveTo({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, {});
        }
        return Step::Yield;
    }
    if (cmd == "sim-lacher") {
        send(ui::MouseUp{driver_.mouse(), MouseButton::Left, {}});
        return Step::Yield;
    }
    // Lot 8 : une popup de la vue simulee - sa barre de titre (la cliquer, la
    // tirer de dx, dy pixels), sa croix ; une touche du clavier virtuel.
    if (cmd == "sim-popup") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        HmiLiveCanvas::PopupRect pr;
        if (!pane->canvas().popupRect(arg(1), pr)) {
            if (retries_ < 3) return Step::Retry;
            fail("popup introuvable dans la vue simulee : " + arg(1));
            return Step::Next;
        }
        if (arg(2) == "croix") {
            if (pr.close.w <= 0.f) { fail("la popup " + arg(1) + " n'a pas de croix"); return Step::Next; }
            click(centre(pr.close), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (arg(2) == "dehors") {
            // Un clic dans la vue du dessous, hors de la popup (en haut a gauche du volet).
            const gfx::Point p{pr.window.x > 60.f ? pr.window.x - 30.f : pr.window.right() + 30.f, pr.window.y + 10.f};
            click(p, MouseButton::Left, 1, {});
            return Step::Yield;
        }
        const gfx::Point from{pr.title.x + std::min(60.f, pr.title.w / 2.f), pr.title.y + pr.title.h / 2.f};
        if (arg(2) == "glisser") {
            drag(from, {from.x + num(3), from.y + num(4)}, {});
            return Step::Yield;
        }
        click(from, MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // Lot 8 : l'aide de l'IHM - un sujet, l'exemple anime fige a un instant de
    // son tour, et la capture de l'exemple seul (pour le guide Word).
    // 1.11 (chantier T2, tranche 8) : "aide-cible <cible>" suit un lien du centre
    // d'aide comme un clic (le filtre des notes, une pastille, Me montrer...).
    if (cmd == "aide-cible") {
        auto* centre = dynamic_cast<HelpCenterScreen*>(app_.menus().top());
        if (!centre) { fail("le centre d'aide n'est pas a l'ecran (aide-cible)"); return Step::Next; }
        centre->follow(arg(1));
        return Step::Yield;
    }
    if (cmd == "aide-sujet" || cmd == "exemple-figer" || cmd == "capture-exemple") {
        HmiHelpPane* help = nullptr;
        if (auto* root0 = top()) walk(*root0, [&](ui::Widget& x) {
            if (auto* h = dynamic_cast<HmiHelpPane*>(&x); !help && h && shown(*h)) help = h;
        });
        // 1.11 (chantier T2) : le centre d'aide unique - "aide-sujet <cle>" y
        // ouvre un sujet du centre (une cle du guide, page-raccourcis,
        // notes-1.10.1, page-signaler, bloc-..., api-...).
        if (!help && cmd == "aide-sujet") {
            if (auto* centre = dynamic_cast<HelpCenterScreen*>(app_.menus().top())) {
                if (!HelpCenterScreen::index().find(arg(1))) { fail("sujet inconnu : " + arg(1)); return Step::Next; }
                centre->open(arg(1));
                return Step::Yield;
            }
        }
        if (!help) { fail("aucune aide de l'IHM a l'ecran (" + cmd + ")"); return Step::Next; }
        if (cmd == "aide-sujet") {
            if (!hmi::guide::topic(arg(1))) { fail("sujet inconnu : " + arg(1)); return Step::Next; }
            help->show(arg(1));
            return Step::Yield;
        }
        if (cmd == "exemple-figer") {
            help->example().freezeAt(num(1));
            return Step::Yield;
        }
        if (sinceInput_ < 3) return Step::Retry;
        // La vue de l'exemple et une marge (pas les bandes vides de chaque cote).
        const auto f = help->example().exampleRect();
        const gfx::Rect r{f.x - 10.f, f.y - 10.f, f.w + 20.f, f.h + 20.f};
        if (f.w <= 1.f || f.h <= 1.f) { fail("pas d'exemple a l'ecran : " + arg(1)); return Step::Next; }
        const auto path = (capturesDir_ / arg(1)).string();
        if (auto res = captureRegionToPng(renderer, path, static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w),
                                          static_cast<int>(r.h)); !res)
            fail("capture " + path + " : " + res.error().message());
        else std::printf("[script] capture %s\n", path.c_str());
        return Step::Next;
    }
    // Lot 16 : le tutoriel d'un objet (l'onglet Tutoriel de sa page d'aide) :
    //   tutoriel "objet-gif-anime"      ouvrir le sujet sur son tutoriel
    //   tutoriel-figer 5.0              a cet instant du tour, a l'arret (captures)
    //   tutoriel-chapitre 3             aller au chapitre 3 (et jouer)
    //   tutoriel-vitesse 2              x0,5, x1, x2
    //   tutoriel-lecture | tutoriel-pause | tutoriel-arret | tutoriel-a-toi
    //   tutoriel-commande "a-toi"       cliquer une commande de la barre (vraie souris)
    //   tutoriel-clic "Btn_Jouer"       "A toi" : cliquer un objet de la scene (vraie souris)
    if (cmd.rfind("tutoriel", 0) == 0) {
        HmiHelpPane* help = nullptr;
        if (auto* root0 = top()) walk(*root0, [&](ui::Widget& x) {
            if (auto* h = dynamic_cast<HmiHelpPane*>(&x); !help && h && shown(*h)) help = h;
        });
        if (!help) { fail("aucune aide de l'IHM a l'ecran (" + cmd + ")"); return Step::Next; }
        auto& tuto = help->tutorial();
        if (cmd == "tutoriel") {
            if (!hmi::guide::topic(arg(1))) { fail("sujet inconnu : " + arg(1)); return Step::Next; }
            help->show(arg(1));
            if (!help->showTutorial(true)) fail("ce sujet n'a pas de tutoriel : " + arg(1));
            return Step::Yield;
        }
        if (!tuto.hasTutorial()) { fail("aucun tutoriel ouvert (" + cmd + ")"); return Step::Next; }
        if (cmd == "tutoriel-figer") { tuto.seek(num(1)); tuto.pause(); return Step::Yield; }
        if (cmd == "tutoriel-chapitre") { tuto.goToChapter(static_cast<std::size_t>(std::max(1.f, num(1))) - 1); return Step::Yield; }
        if (cmd == "tutoriel-vitesse") { tuto.setSpeed(num(1)); return Step::Yield; }
        if (cmd == "tutoriel-lecture") { tuto.play(); return Step::Yield; }
        if (cmd == "tutoriel-pause") { tuto.pause(); return Step::Yield; }
        if (cmd == "tutoriel-arret") { tuto.stop(); return Step::Yield; }
        if (cmd == "tutoriel-a-toi") { tuto.yourTurn(); return Step::Yield; }
        if (cmd == "tutoriel-commande") {
            gfx::Rect r{};
            if (!tuto.controlRect(arg(1), r)) { fail("commande du tutoriel introuvable : " + arg(1)); return Step::Next; }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        if (cmd == "tutoriel-clic") {
            gfx::Rect r{};
            if (!tuto.stage().canvas().objectRect(arg(1), r)) {
                if (retries_ < 3) return Step::Retry;
                fail("objet introuvable dans la scene du tutoriel : " + arg(1));
                return Step::Next;
            }
            click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        fail("commande de tutoriel inconnue : " + cmd);
        return Step::Next;
    }
    // Lot 12 : la molette sur un objet de la vue simulee (un panneau defilant, une
    // vue qu'on peut zoomer) ; un balayage de l'ecran (gauche : la vue suivante).
    if (cmd == "sim-molette") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (!pane->canvas().objectRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("objet introuvable dans la vue simulee : " + arg(1));
            return Step::Next;
        }
        const auto p = centre(r);
        moveTo(p, {});
        send(ui::MouseWheel{p, 0.f, num(2), {}});
        return Step::Yield;
    }
    // 1.9 (chantier C) : la souris sur un objet de la vue simulee (son infobulle :
    // une lecture simulee) - sim-survol "<objet>".
    if (cmd == "sim-survol") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (!pane->canvas().objectRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("objet introuvable dans la vue simulee : " + arg(1));
            return Step::Next;
        }
        moveTo(centre(r), {});
        return Step::Yield;
    }
    if (cmd == "sim-balayer") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        const auto sc = pane->canvas().screenRect();
        const float y = sc.y + sc.h * (w.size() > 2 ? num(2) : 0.5f);
        const bool left = arg(1) == "gauche";
        const gfx::Point a{sc.x + sc.w * (left ? 0.8f : 0.2f), y}, b{sc.x + sc.w * (left ? 0.2f : 0.8f), y};
        drag(a, b, {});
        return Step::Yield;
    }
    if (cmd == "sim-vue") {
        // La simulation va a cette vue (comme une navigation, sans transition).
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        auto doc = app_.hmi();
        if (!pane || !doc) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        const auto* v = doc->project.viewByName(arg(1));
        if (!v) { fail("vue inconnue : " + arg(1)); return Step::Next; }
        pane->goToView(v->id);
        return Step::Yield;
    }
    // Lot 14 : le poste d'exploitation. "poste" y entre (comme Essayer le poste) ;
    // "attendre-poste" attend qu'il soit la ; "capture-ecran 2 NOM.png" capture
    // un ecran secondaire (sa fenetre) ; "poste-sortir" demande la sortie.
    if (cmd == "poste") {
        app_.enterStation();
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "attendre-poste") return dynamic_cast<StationScreen*>(app_.menus().top()) ? Step::Yield : Step::Retry;
    if (cmd == "poste-sortir") {
        auto* station = dynamic_cast<StationScreen*>(app_.menus().top());
        if (!station) { fail("pas de poste d'exploitation (" + cmd + ")"); return Step::Next; }
        station->askExit();
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "capture-ecran") {
        auto* station = dynamic_cast<StationScreen*>(app_.menus().top());
        if (!station) { fail("pas de poste d'exploitation (" + cmd + ")"); return Step::Next; }
        if (sinceInput_ < 3) return Step::Retry;
        const auto path = (capturesDir_ / arg(2)).string();
        if (screenCapture_ != path) {
            station->requestCapture(std::atoi(arg(1).c_str()), path);
            screenCapture_ = path;
            return Step::Retry;
        }
        const auto result = station->captureResult(path);
        if (!result) return Step::Retry;
        screenCapture_.clear();
        if (!result->empty()) fail("capture " + path + " : " + *result);
        else std::printf("[script] capture %s\n", path.c_str());
        return Step::Next;
    }
    if (cmd == "sim-forcer") {
        // Lot 14 : une valeur forcee dans l'automate simule (comme le dialogue
        // Forcer) : sim-forcer "Armoires[0].ana.PT1.mes" "7.25" ; "" : liberer.
        if (!app_.simulation().attached() && app_.project()) (void)app_.simulation().attach(app_.project());
        auto* rt = app_.simulationRuntime();
        if (!rt) { fail("pas d'automate simul\xC3\xA9 (sim-forcer)"); return Step::Next; }
        if (arg(2).empty()) {
            (void)rt->unforce(arg(1));
            return Step::Next;
        }
        ::sim::Value cur;
        if (!rt->get(arg(1), cur)) { fail("variable inconnue du simulateur : " + arg(1)); return Step::Next; }
        ::sim::Value v;
        const std::string& t = arg(2);
        switch (cur.type()) {
            case ::sim::Type::Bool: v = ::sim::Value::boolean(t == "TRUE" || t == "true" || t == "1" || t == "VRAI" || t == "vrai"); break;
            case ::sim::Type::Real: v = ::sim::Value::real(std::strtod(t.c_str(), nullptr)); break;
            case ::sim::Type::String: v = ::sim::Value::text(t); break;
            case ::sim::Type::Time: v = ::sim::Value::time(std::strtoll(t.c_str(), nullptr, 10)); break;
            default: v = ::sim::Value::integer(cur.type(), std::strtoll(t.c_str(), nullptr, 10)); break;
        }
        if (!rt->force(arg(1), v)) fail("for\xC3\xA7" "age refus\xC3\xA9 : " + arg(1));
        return Step::Next;
    }
    // ---- Lot API 8 : le moteur de simulation, 2e partie ----
    //   programme-dirait "chemin" ["attendu"]   ce que le programme ecrit sous le forcage (la colonne "Le programme dirait" des Forcages) ; un attendu : l'exiger
    //   automate-bandeau [vert|bleu|orange|rouge|gris] ["texte"]   la couleur et la phrase du bandeau de l'onglet Automate (ouvert) ; les exiger
    if (cmd == "programme-dirait") {
        std::string says;
        if (!app_.simulation().unforcedValue(arg(1), says)) { fail("programme-dirait : pas de simulation, ou variable inconnue : " + arg(1)); return Step::Next; }
        std::printf("[script]   le programme dirait %s = %s\n", arg(1).c_str(), says.c_str());
        std::fflush(stdout);
        if (w.size() > 2 && says != arg(2)) fail("programme-dirait " + arg(1) + " : " + says + ", attendu : " + arg(2));
        return Step::Next;
    }
    if (cmd == "automate-bandeau") {
        auto* pane = apiPaneOf<SimulationPane>(app_, "simulation", /*open=*/true);
        if (!pane) { fail("automate-bandeau : l'onglet Automate ne s'ouvre pas"); return Step::Next; }
        const auto said = pane->stateLine();
        const auto color = pane->bandColor();
        std::printf("[script]   bandeau Automate (%s) : %s\n", color.c_str(), said.c_str());
        std::fflush(stdout);
        if (w.size() > 1 && !arg(1).empty() && lower(arg(1)) != color) fail("automate-bandeau : " + color + ", attendu : " + arg(1));
        if (w.size() > 2 && said.find(arg(2)) == std::string::npos) fail("automate-bandeau : '" + said + "', attendu : " + arg(2));
        return Step::Next;
    }
    // ---- fin Lot API 8 : le moteur de simulation, 2e partie ----
    if (cmd == "sim-touche") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (!pane->canvas().keyRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("touche du clavier virtuel introuvable : " + arg(1));
            return Step::Next;
        }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "sim-clic" || cmd == "sim-appui" || cmd == "sim-relache") {
        // Un objet de la vue simulee (celle du dessus : la popup s'il y en a une).
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est pas la simulation IHM (" + cmd + ")"); return Step::Next; }
        gfx::Rect r{};
        if (!pane->canvas().objectRect(arg(1), r)) {
            if (retries_ < 3) return Step::Retry;
            fail("objet introuvable dans la vue simulee : " + arg(1));
            return Step::Next;
        }
        const auto p = centre(r);
        if (cmd == "sim-appui") { moveTo(p, {}); send(ui::MouseDown{p, MouseButton::Left, 1, {}}); }
        else if (cmd == "sim-relache") { moveTo(p, {}); send(ui::MouseUp{p, MouseButton::Left, {}}); }
        else {
            click(p, MouseButton::Left, 1, {});
            if (has("double")) click(p, MouseButton::Left, 2, {});
        }
        return Step::Yield;
    }

    // ---- Lot API 8 : Simulation > Debogage et les points d'arret de l'editeur ----
    //   debogage-ouvrir                          ouvrir (ou montrer) l'onglet Simulation > Debogage
    //   point-arret-marge "Section" 42 [maj]     le clic dans la marge de l'onglet de cette section, ligne 42 (1 = la
    //                                            premiere) : pose / enleve le point d'arret (maj : l'active / le
    //                                            desactive) ; la section s'ouvre et la ligne vient en vue au besoin
    //   debogage-point "SFC_PurgeA 42 si x > 3"  + Point d'arret, tape comme dans son champ (la condition : facultative)
    //   debogage-condition 1 "x > 3"             la condition du point d'arret n. 1 de la liste ("" : sans condition)
    //   debogage-point-actif 1 oui|non           l'activer / le desactiver ; debogage-point-retirer 1 ; debogage-points-effacer
    //   debogage-points-desactiver               "Tout desactiver" (ils restent dans la liste)
    //   debogage-modifier                        "Modifier le projet..." : en pause, le code de la ligne d'arret dans son onglet
    //   espion-ajouter "Armoires[0].etat"        un espion (deja la : rien) ; espion-retirer "chemin" ;
    //                                            espion-deplier "chemin" [fermer]  (comme dans l'onglet Automate)
    //   debogage-continuer | debogage-section | debogage-cycle | debogage-pause | debogage-arreter
    //                                            Continuer (F5), Section suivante (F10), Cycle suivant (F11), Pause, Arreter
    //   debogage-jusqua 42                       Executer jusqu'a la ligne 42 du code montre (0 : la ligne du curseur)
    //   debogage-voir "Section" [42]             montrer ce code au centre de l'onglet (la ligne en vue)
    //   debogage-pile 1                          cliquer le niveau 1 de la pile (1 = le premier, MAST)
    //   debogage-variable "chemin"               la variable de "Qui a ecrit ?"
    //   debogage-etat "texte"                    echec si la phrase d'etat (le bandeau) ne contient pas le texte
    //   debogage-journal "texte"                 echec si aucun evenement du debogage (le journal) ne le contient
    {
        static const char* const kDebugCommands[] = {
            "debogage-ouvrir", "point-arret-marge", "debogage-point", "debogage-condition", "debogage-point-actif",
            "debogage-point-retirer", "debogage-points-effacer", "debogage-points-desactiver", "debogage-modifier",
            "espion-ajouter", "espion-retirer", "espion-deplier",
            "debogage-continuer", "debogage-section", "debogage-cycle", "debogage-pause", "debogage-arreter",
            "debogage-jusqua", "debogage-voir", "debogage-pile", "debogage-variable", "debogage-etat", "debogage-journal"};
        if (std::any_of(std::begin(kDebugCommands), std::end(kDebugCommands), [&](const char* c) { return cmd == c; })) {
            auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
            if (!screen || !app_.project()) { fail(cmd + " : pas de projet ouvert, ou un dialogue devant"); return Step::Next; }
            if (cmd == "point-arret-marge") {
                const int line = std::atoi(arg(2).c_str());
                if (line < 1) { fail("point-arret-marge : une ligne (1 = la premi\xC3\xA8re), pas " + arg(2)); return Step::Next; }
                gfx::Point where{};
                if (!screen->sectionMarginPoint(arg(1), line, where)) {
                    if (retries_ < 30) return Step::Retry;       // l'onglet s'ouvre, la ligne vient en vue : apres un dessin
                    fail("point-arret-marge : section introuvable, ou pas de ligne " + arg(2) + " dans " + arg(1));
                    return Step::Next;
                }
                click(where, MouseButton::Left, 1, mods());
                sinceInput_ = 0;
                return Step::Yield;
            }
            if (cmd == "debogage-journal") {
                const auto events = screen->debugEvents();
                if (std::none_of(events.begin(), events.end(), [&](const std::string& e) { return e.find(arg(1)) != std::string::npos; }))
                    fail("debogage-journal : aucun \xC3\xA9v\xC3\xA9nement du d\xC3\xA9" "bogage ne contient : " + arg(1));
                return Step::Next;
            }
            // Les autres passent par l'onglet : ouvert au besoin (il vient devant).
            auto* pane = dynamic_cast<SimDebugPane*>(screen->apiPaneContent("debogage", false));
            if (!pane || cmd == "debogage-ouvrir") {
                screen->openSimDebug();
                pane = dynamic_cast<SimDebugPane*>(screen->apiPaneContent("debogage", false));
            }
            if (!pane) { fail("l'onglet Simulation \xE2\x80\xBA D\xC3\xA9" "bogage ne s'ouvre pas (" + cmd + ")"); return Step::Next; }
            const auto rank = static_cast<std::size_t>(std::max(0, std::atoi(arg(1).c_str())));
            std::string why;
            bool ok = true;
            if (cmd == "debogage-point") ok = pane->addBreakpointText(arg(1), &why);
            else if (cmd == "debogage-condition") ok = pane->setBreakpointCondition(rank, arg(2));
            else if (cmd == "debogage-point-actif") ok = pane->setBreakpointEnabled(rank, lower(arg(2)).rfind('n', 0) != 0);
            else if (cmd == "debogage-point-retirer") ok = pane->removeBreakpoint(rank);
            else if (cmd == "debogage-points-effacer") pane->clearBreakpoints();
            else if (cmd == "debogage-points-desactiver") pane->disableAllBreakpoints();
            else if (cmd == "debogage-modifier") ok = pane->modifyProject();
            else if (cmd == "espion-ajouter") {
                const auto want = lower(arg(1));
                const auto list = pane->watches();
                if (std::none_of(list.begin(), list.end(), [&](const std::string& s) { return lower(s) == want; })) ok = pane->addWatch(arg(1), &why);
            }
            else if (cmd == "espion-retirer") ok = pane->removeWatch(arg(1));
            else if (cmd == "espion-deplier") ok = pane->expandWatch(arg(1), !has("fermer"));
            else if (cmd == "debogage-continuer") pane->runAction(SimDebugPane::AContinue);
            else if (cmd == "debogage-section") pane->runAction(SimDebugPane::AStepSection);
            else if (cmd == "debogage-cycle") pane->runAction(SimDebugPane::AStepCycle);
            else if (cmd == "debogage-pause") pane->runAction(SimDebugPane::APause);
            else if (cmd == "debogage-arreter") pane->runAction(SimDebugPane::AStop);
            else if (cmd == "debogage-jusqua") ok = pane->runToLine(std::atoi(arg(1).c_str()));
            else if (cmd == "debogage-voir") ok = pane->showSection(arg(1), std::atoi(arg(2).c_str()));
            else if (cmd == "debogage-pile") ok = rank >= 1 && pane->chooseLevel(rank - 1);
            else if (cmd == "debogage-variable") pane->chooseVariable(arg(1));
            else if (cmd == "debogage-etat") {
                const auto said = pane->stateLine();
                if (said.find(arg(1)) == std::string::npos) fail("debogage-etat : '" + said + "', attendu : " + arg(1));
                return Step::Next;
            }
            if (!ok) fail(cmd + " refus\xC3\xA9" + (why.empty() ? std::string() : " : " + why) + (w.size() > 1 ? " (" + arg(1) + ")" : std::string()));
            sinceInput_ = 0;
            return Step::Yield;
        }
    }
    // ---- fin Lot API 8 : Simulation > Debogage ----

    // ---- Lot API 8 : Simulation > Debogage (2e partie) : la condition par clic droit, l'espion tape ----
    //   point-arret-condition-marge "Section" 42 ["x > 3"]
    //                                            le clic droit dans la marge de l'onglet de cette section, ligne 42 :
    //                                            le point (pose s'il n'y en a pas) et le dialogue de sa condition ; la
    //                                            condition : tapee dans son champ. Puis touche entree (Valider) ou
    //                                            touche echap (Annuler)
    //   espion-taper "Armoires[0].et"            le champ sous les espions prend ce debut (ses suggestions s'ouvrent) ;
    //                                            puis touche entree : la suggestion choisie devient un espion (un nom
    //                                            inconnu : "<< X >> n'existe pas dans le projet.", dans la barre d'etat)
    //   debogage-mettre-condition                Pourquoi ici, en pause sur un point sans condition : le bouton
    //                                            "Mettre la condition nom = valeur" (les valeurs de la ligne)
    if (cmd == "point-arret-condition-marge" || cmd == "espion-taper" || cmd == "debogage-mettre-condition") {
        auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
        if (!screen || !app_.project()) { fail(cmd + " : pas de projet ouvert, ou un dialogue devant"); return Step::Next; }
        if (cmd == "point-arret-condition-marge") {
            const int line = std::atoi(arg(2).c_str());
            if (line < 1) { fail("point-arret-condition-marge : une ligne (1 = la premi\xC3\xA8re), pas " + arg(2)); return Step::Next; }
            gfx::Point where{};
            if (!screen->sectionMarginPoint(arg(1), line, where)) {
                if (retries_ < 30) return Step::Retry;       // l'onglet s'ouvre, la ligne vient en vue : apres un dessin
                fail("point-arret-condition-marge : section introuvable, ou pas de ligne " + arg(2) + " dans " + arg(1));
                return Step::Next;
            }
            if (w.size() > 3) SimConditionDialog::setNextText(arg(3));
            click(where, MouseButton::Right, 1, KeyMods{});
            sinceInput_ = 0;
            return Step::Yield;
        }
        auto* pane = dynamic_cast<SimDebugPane*>(screen->apiPaneContent("debogage", false));
        if (!pane) {
            screen->openSimDebug();
            pane = dynamic_cast<SimDebugPane*>(screen->apiPaneContent("debogage", false));
        }
        if (!pane) { fail("l'onglet Simulation \xE2\x80\xBA D\xC3\xA9" "bogage ne s'ouvre pas (" + cmd + ")"); return Step::Next; }
        if (cmd == "espion-taper") {
            pane->typeWatch(arg(1));
        } else if (!pane->applyProposedCondition()) {
            fail("debogage-mettre-condition : pas en pause sur un point d'arr\xC3\xAAt sans condition (ou pas de valeur sur sa ligne)");
            return Step::Next;
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : Simulation > Debogage (2e partie) ----

    // ---- Lot API 8 : les exports qui demandent ou ----
    //   sim-exporter "alarmes" ["alarmes_{SYS.Date}.csv"] [sans-question]
    //                        l'IHM en marche (Simulation de l'IHM, ou le poste) exporte comme le clic d'un
    //                        bouton d'export : la question s'ouvre (le dialogue du lot 7 dans l'editeur, le
    //                        sien au poste) ; sans-question : exports/ tout de suite, comme un timer
    //   sim-dernier-export "texte"   echec si le dernier export (SYS.LastExport) ne contient pas le texte
    //   La question (l'IHM en marche, Generer maintenant, Derniere periode) : bouton "Exporter" |
    //   bouton "Annuler" | touche entree | touche echap ; ailleurs : explorateur "D:/x/y.csv" puis
    //   parcourir "Dossier ou fichier", ou champ 1 "D:/x/" (un dossier : le nom habituel dedans).
    //   L'option d'une action Exporter ou d'un bouton d'export : propriete-clic sur la case
    //   "Demander ou enregistrer" (le libelle accentue, comme a l'ecran) - une bascule.
    if (cmd == "sim-exporter" || cmd == "sim-dernier-export") {
        auto* pane = dynamic_cast<HmiSimulationPane*>(currentPage());
        if (!pane) { fail("l'onglet courant n'est ni la simulation IHM ni le poste (" + cmd + ")"); return Step::Next; }
        auto& rt = pane->runtime();
        if (cmd == "sim-dernier-export") {
            if (rt.lastExport().find(arg(1)) == std::string::npos) fail("dernier export : '" + rt.lastExport() + "', attendu : " + arg(1));
            return Step::Next;
        }
        const bool ask = !has("sans-question");
        const std::string file = w.size() > 2 && arg(2) != "sans-question" ? arg(2) : std::string("export_{SYS.Date}");
        std::string why;
        if (!rt.exportAsOperator(arg(1).empty() ? std::string("alarmes") : arg(1), file, {}, rt.now(), ask, &why))
            fail("sim-exporter : " + why);
        pane->refreshNow();
        sinceInput_ = 0;
        return Step::Yield;
    }
    // ---- fin Lot API 8 : les exports qui demandent ou ----

    // ---- Lot API 8 : Centre de simulation ----
    //   centre ensemble|automate|ihm|equipements|debogage|forcages|courbes|journal   un onglet du Centre (comme l'arbre)
    //   centre-clic "bandeau:bouton"      un VRAI clic sur une zone de la Vue d'ensemble ("carte:automate:0", "chaine:ihm", "attention:<id>", "frise:journal"...)
    //   centre-aller "ligne:BUILDING:58"  suivre une cle "aller a" (variable:X, vue:Vue_A, alarmes, sim.run, sim.pause, sim.step, relancer, forcages...)
    //   etat-bandeau [vert|orange|rouge|bleu|gris]   le bandeau, les cartes et l'attention dans le journal du script ; une couleur : l'exiger
    //   courbe-ajouter "chemin"           une courbe de plus (8 au plus ; "ihm:" devant : une variable de l'IHM)
    //   courbe-retirer "chemin"           la retirer ; courbes-vider : toutes
    //   courbes-figer oui|non             figer l'affichage (la mesure continue)
    //   courbes-curseur <secondes>        le curseur N secondes avant la fin ; ses valeurs dans le journal du script
    //   forcages-relacher "nom"           relacher la ligne de ce nom (automate, IHM ou jumeau) ; forcages-tout-relacher
    //   journal-filtre <source> [gravite] tout|automate|ihm|equipements|debogage|simulation ; tout|surveiller|erreurs
    //   journal-chercher "texte"          ne garder que les lignes qui le contiennent ("" : toutes)
    //   journal-ajouter <source> <gravite> "texte" ["aller a"]   une ligne (essais) ; gravite info|ok|surveiller|erreur
    //   journal-aller <rang>              choisir la ligne (1 : la plus recente) et y aller
    //   journal-lignes [N]                les N premieres lignes montrees, dans le journal du script
    {
        static const char* const kSimCenterCommands[] = {"centre", "centre-clic", "centre-aller", "etat-bandeau", "courbe-ajouter",
                                                         "courbe-retirer", "courbes-vider", "courbes-figer", "courbes-curseur",
                                                         "forcages-relacher", "forcages-tout-relacher", "journal-filtre",
                                                         "journal-chercher", "journal-ajouter", "journal-aller", "journal-lignes"};
        if (std::any_of(std::begin(kSimCenterCommands), std::end(kSimCenterCommands), [&](const char* c) { return cmd == c; })) {
            auto* screen = dynamic_cast<MainAnalysisScreen*>(app_.menus().top());
            if (!screen) { fail(cmd + " : l'\xC3\xA9" "cran d'analyse n'est pas au-dessus (un dialogue ouvert ?)"); return Step::Next; }
            sinceInput_ = 0;
            if (cmd == "centre") {
                if (!screen->openSimCenter(lower(arg(1))))
                    fail("centre : " + arg(1) + " ne s'ouvre pas (ensemble, automate, ihm, equipements, debogage, forcages, courbes, journal)");
                return Step::Yield;
            }
            if (cmd == "centre-aller") {
                screen->simCenterGo(arg(1));
                return Step::Yield;
            }
            if (cmd == "centre-clic") {
                auto* pane = dynamic_cast<SimOverviewPane*>(screen->simCenterPane("ensemble", true));
                if (!pane) { fail("centre-clic : la Vue d'ensemble ne s'ouvre pas"); return Step::Next; }
                const auto r = pane->partRect(arg(1));
                if (r.w <= 0.f) {
                    if (retries_ < 3) return Step::Retry;          // le temps d'un dessin
                    std::string keys;
                    for (const auto& k : pane->partKeys()) keys += " " + k;
                    fail("centre-clic : pas de zone " + arg(1) + " (il y a :" + keys + ")");
                    return Step::Next;
                }
                // Hors de la vue (plus bas) : ce que ferait le clic.
                if (!pane->bounds().contains(centre(r))) {
                    if (!pane->activate(arg(1))) fail("centre-clic : " + arg(1) + " ne fait rien");
                    return Step::Yield;
                }
                click(centre(r), MouseButton::Left, 1, {});
                return Step::Yield;
            }
            if (cmd == "etat-bandeau") {
                const auto rep = screen->simCenterReport();
                const auto tone = simstatus::toneName(rep.banner.tone);
                std::printf("[script] bandeau (%s) %s : %s\n", tone.c_str(), rep.banner.word.c_str(), rep.banner.title.c_str());
                if (!rep.banner.meaning.empty()) std::printf("[script]   ce que \xC3\xA7" "a veut dire : %s\n", rep.banner.meaning.c_str());
                if (!rep.banner.fix.empty()) std::printf("[script]   bouton : %s (%s)\n", rep.banner.fix.label.c_str(), rep.banner.fix.key.c_str());
                if (!rep.banner.second.empty()) std::printf("[script]   bouton : %s (%s)\n", rep.banner.second.label.c_str(), rep.banner.second.key.c_str());
                for (const auto& c : rep.cards) {
                    std::string figures;
                    for (const auto& f : c.figures)
                        if (!f.label.empty()) figures += " | " + f.label + " " + f.value + (f.detail.empty() ? std::string{} : " " + f.detail);
                    std::printf("[script]   carte %s (%s) %s : %s%s\n", c.title.c_str(), simstatus::toneName(c.tone).c_str(), c.state.c_str(),
                                c.sentence.c_str(), figures.c_str());
                }
                for (const auto& a : rep.attention)
                    std::printf("[script]   attention (%s) %s%s%s\n", simstatus::toneName(a.tone).c_str(), a.text.c_str(),
                                a.action.empty() ? "" : (" [" + a.action.label + "]").c_str(),
                                a.second.empty() ? "" : (" [" + a.second.label + "]").c_str());
                std::fflush(stdout);
                if (!arg(1).empty() && lower(arg(1)) != tone) fail("etat-bandeau : le bandeau est " + tone + ", pas " + arg(1));
                return Step::Yield;
            }
            if (cmd == "courbe-ajouter") {
                std::string why;
                if (!screen->addSimTrend(arg(1), &why)) fail("courbe-ajouter : " + why);
                (void)screen->openSimCenter("courbes");
                return Step::Yield;
            }
            if (cmd == "courbe-retirer") {
                if (!screen->simCenterModel().removeTrend(arg(1))) fail("courbe-retirer : pas de courbe " + arg(1));
                return Step::Yield;
            }
            if (cmd == "courbes-vider") {
                screen->simCenterModel().clearTrends();
                return Step::Yield;
            }
            if (cmd == "courbes-figer") {
                auto& model = screen->simCenterModel();
                model.setFrozen(lower(arg(1)) != "non", model.now);
                return Step::Yield;
            }
            if (cmd == "courbes-curseur") {
                auto* pane = dynamic_cast<SimTrendsPane*>(screen->simCenterPane("courbes", true));
                if (!pane) { fail("courbes-curseur : l'onglet Courbes ne s'ouvre pas"); return Step::Next; }
                pane->setCursor(static_cast<double>(num(1)));
                for (const auto& line : pane->cursorReadout()) std::printf("[script]   curseur : %s\n", line.c_str());
                std::fflush(stdout);
                return Step::Yield;
            }
            if (cmd == "forcages-relacher" || cmd == "forcages-tout-relacher") {
                auto* pane = dynamic_cast<SimForcingPane*>(screen->simCenterPane("forcages", true));
                if (!pane) { fail(cmd + " : l'onglet For\xC3\xA7" "ages ne s'ouvre pas"); return Step::Next; }
                pane->refresh();
                if (cmd == "forcages-tout-relacher") pane->releaseAll();
                else if (!pane->select(arg(1))) fail("forcages-relacher : pas de for\xC3\xA7" "age " + arg(1));
                else pane->releaseSelected();
                return Step::Yield;
            }
            if (cmd == "journal-ajouter") {
                const auto src = lower(arg(1)), sev = lower(arg(2));
                const SimSource source = src.rfind("auto", 0) == 0 ? SimSource::Automate : src == "ihm" ? SimSource::Ihm
                                       : src.rfind("equip", 0) == 0 ? SimSource::Equipements : src.rfind("debog", 0) == 0 ? SimSource::Debogage
                                                                                                                        : SimSource::Simulation;
                const SimSeverity severity = sev == "ok" ? SimSeverity::Ok : sev.rfind("surv", 0) == 0 ? SimSeverity::Warning
                                           : sev.rfind("err", 0) == 0 ? SimSeverity::Error : SimSeverity::Info;
                (void)app_.simJournal().add(source, severity, arg(3), arg(4));
                if (auto* pane = dynamic_cast<SimJournalPane*>(screen->simCenterPane("journal", false))) pane->refresh();
                return Step::Yield;
            }
            auto* pane = dynamic_cast<SimJournalPane*>(screen->simCenterPane("journal", true));
            if (!pane) { fail(cmd + " : l'onglet Journal ne s'ouvre pas"); return Step::Next; }
            pane->refresh();
            if (cmd == "journal-filtre") {
                if (!pane->setSource(lower(arg(1)))) fail("journal-filtre : source tout, automate, ihm, equipements, debogage ou simulation, pas " + arg(1));
                else if (w.size() > 2 && !pane->setSeverity(lower(arg(2)))) fail("journal-filtre : gravit\xC3\xA9 tout, surveiller ou erreurs, pas " + arg(2));
            } else if (cmd == "journal-chercher") {
                pane->setSearch(arg(1));
            } else if (cmd == "journal-aller") {
                const auto row = static_cast<std::size_t>(std::max(1.f, num(1))) - 1;
                if (!pane->selectRow(row) || !pane->goSelected()) fail("journal-aller : pas de ligne " + arg(1) + " (ou rien o\xC3\xB9 aller)");
            } else {                                                    // journal-lignes
                const auto max = static_cast<std::size_t>(std::max(1.f, w.size() > 1 ? num(1) : 10.f));
                for (const auto& line : pane->lines(max)) std::printf("[script]   journal : %s\n", line.c_str());
                std::fflush(stdout);
            }
            return Step::Yield;
        }
    }
    // ---- fin Lot API 8 : Centre de simulation ----

    // ---- Lot API 8 : le moteur de simulation ----
    //   point-arret "Section" 42 ["condition"]   un point d'arret du moteur ("Section", "Unite.Section", "BLOC.Section") ;
    //                                            sa ligne effective et sa note dans le journal du script
    //   points-arret-effacer                      enlever tous les points d'arret
    //   simulation-section-suivante               la prochaine entree de MAST seulement, puis pause (pas a pas par section)
    //   qui-a-ecrit "Armoires[0].ana.PT1.mes"     la derniere ecriture de la case (section, ligne, cycle), dans le journal du script
    //   etat-simulation [marche|pause|arretee|defaut]   l'etat, le cycle, le dernier point d'arret (valeurs, pile), la
    //                                            derniere modification en ligne ; un etat : l'exiger
    //   simulation-temps                          ou passe le temps : chaque section au dernier cycle complet (instructions, us)
    {
        static const char* const kSimEngineCommands[] = {"point-arret", "points-arret-effacer", "simulation-section-suivante",
                                                         "qui-a-ecrit", "etat-simulation", "simulation-temps"};
        if (std::any_of(std::begin(kSimEngineCommands), std::end(kSimEngineCommands), [&](const char* c) { return cmd == c; })) {
            auto& sim = app_.simulation();
            if (!sim.attached() && app_.project()) (void)sim.attach(app_.project());
            sinceInput_ = 0;
            if (cmd == "point-arret") {
                const int line = std::atoi(arg(2).c_str());
                if (arg(1).empty() || line < 1) {
                    fail("point-arret : \"Section\" ligne [\"condition\"] (1 = la premi\xC3\xA8re ligne), pas " + arg(2));
                    return Step::Next;
                }
                const auto id = sim.addBreakpoint(arg(1), line, arg(3));
                for (const auto& b : sim.breakpoints())
                    if (b.id == id)
                        std::printf("[script] point d'arr\xC3\xAAt %u : %s, ligne %d%s%s%s%s\n", b.id, b.section.c_str(), b.line,
                                    b.condition.empty() ? "" : " si ", b.condition.c_str(), b.note.empty() ? "" : " - ", b.note.c_str());
            } else if (cmd == "points-arret-effacer") {
                sim.clearBreakpoints();
                std::printf("[script] points d'arr\xC3\xAAt effac\xC3\xA9s\n");
            } else if (cmd == "simulation-section-suivante") {
                if (!sim.attached()) { fail("simulation-section-suivante : rien \xC3\xA0 simuler"); return Step::Next; }
                const auto entries = sim.taskEntries();
                const auto index = sim.nextEntryIndex();
                const std::string ran = index < entries.size() ? entries[index] : std::string{};
                sim.stepSection();
                const auto next = sim.nextSection();
                std::printf("[script] section ex\xC3\xA9" "cut\xC3\xA9" "e : %s ; cycle %llu%s%s\n", ran.c_str(),
                            static_cast<unsigned long long>(sim.scanCount()), next.empty() ? " (fini)" : " ; suivante : ", next.c_str());
            } else if (cmd == "qui-a-ecrit") {
                app::SimLastWrite where;
                if (!sim.lastWrite(arg(1), where))
                    std::printf("[script] qui a \xC3\xA9" "crit %s : personne (inconnue, ou jamais \xC3\xA9" "crite)\n", arg(1).c_str());
                else if (where.section.empty())
                    std::printf("[script] qui a \xC3\xA9" "crit %s : hors du programme (IHM, script, \xC3\xA9" "cran), cycle %llu\n", arg(1).c_str(),
                                static_cast<unsigned long long>(where.scan));
                else
                    std::printf("[script] qui a \xC3\xA9" "crit %s : %s, ligne %d, cycle %llu\n", arg(1).c_str(), where.section.c_str(), where.line,
                                static_cast<unsigned long long>(where.scan));
            } else if (cmd == "etat-simulation") {
                using St = app::SimulationHost::State;
                const auto st = sim.state();
                const char* word = st == St::Running ? "marche" : st == St::Paused ? "pause" : st == St::Halted ? "defaut" : "arretee";
                std::printf("[script] simulation : %s, cycle %llu%s\n", word, static_cast<unsigned long long>(sim.scanCount()),
                            sim.stale() ? " (le programme a chang\xC3\xA9 : pas encore pris)" : "");
                if (const auto& hit = sim.lastBreakHit()) {
                    std::string stack;
                    for (const auto& s : hit->stack) stack += (stack.empty() ? std::string{} : std::string(" > ")) + s;
                    std::printf("[script]   point d'arr\xC3\xAAt %u : %s, ligne %d, cycle %llu ; pile %s\n", hit->id, hit->section.c_str(), hit->line,
                                static_cast<unsigned long long>(hit->scan), stack.c_str());
                    for (const auto& [name, value] : hit->values) std::printf("[script]     %s = %s\n", name.c_str(), value.c_str());
                }
                if (const auto& change = sim.lastOnlineChange()) std::printf("[script]   %s\n", change->summary.c_str());
                if (const auto next = sim.nextSection(); !next.empty())
                    std::printf("[script]   cycle commenc\xC3\xA9 : section suivante %s\n", next.c_str());
                std::fflush(stdout);
                if (!arg(1).empty() && lower(arg(1)) != word) fail("etat-simulation : la simulation est " + std::string(word) + ", pas " + arg(1));
            } else {                                                    // simulation-temps
                for (const auto& t : sim.sectionTimes()) {
                    if (!t.active) {                                    // 1.10.2 : sa condition d'activation etait fausse
                        std::printf("[script]   temps %s%s%s : inactive (%s faux)\n", t.entry.c_str(), t.entry == t.section ? "" : " > ",
                                    t.entry == t.section ? "" : t.section.c_str(), t.condition.c_str());
                        continue;
                    }
                    std::printf("[script]   temps %s%s%s : %llu instructions, %lld \xC2\xB5s\n", t.entry.c_str(), t.entry == t.section ? "" : " > ",
                                t.entry == t.section ? "" : t.section.c_str(), static_cast<unsigned long long>(t.statements),
                                static_cast<long long>(t.micros));
                }
            }
            std::fflush(stdout);
            return Step::Yield;
        }
    }
    // ---- fin Lot API 8 : le moteur de simulation ----

    // ---- lot API 7 : les onglets API > Simulation et API > Statistiques ----------
    //   simulation-deplier "Armoires[0].ana"          deplier une variable, a toute profondeur
    //   simulation-forcer "Armoires[0].ana.PT1.mes" "7.25"   la choisir, la forcer ("" : la relacher)
    //   simulation-suivre "Armoires[0].ana.PT1.mes"   la choisir, la tracer sur la courbe
    //   simulation-vitesse 1|10|0                     x1, x10, au plus vite
    //   simulation-politique continuer|arreter        une fonction inconnue : rendre 0, ou s'arreter
    //   statistiques-vue entree|section|langage       la repartition des statistiques
    //   (attendre-cycles N : plus haut, il ne depend d'aucun onglet)
    // Le volet est pris par l'ecran d'analyse (apiPaneOf) : l'onglet peut etre
    // derriere un autre, dans un autre groupe, detache. Pas encore ouvert, il
    // s'ouvre, comme par F9 ou Ctrl+5 (action sim.open, arbre "API/Simulation").
    if (cmd.rfind("simulation-", 0) == 0) {
        // Le nom d'abord : une faute de frappe n'ouvre pas l'onglet.
        static const char* const kSimulationCommands[] = {"simulation-deplier", "simulation-forcer", "simulation-suivre",
                                                          "simulation-vitesse", "simulation-politique"};
        if (std::none_of(std::begin(kSimulationCommands), std::end(kSimulationCommands), [&](const char* c) { return cmd == c; })) {
            fail("commande inconnue : " + cmd);
            return Step::Next;
        }
        auto* pane = apiPaneOf<SimulationPane>(app_, "simulation", /*open=*/true);
        if (!pane) { fail("l'onglet API \xE2\x80\xBA Simulation ne s'ouvre pas : pas de projet, ou un dialogue ouvert (" + cmd + ")"); return Step::Next; }
        // Le volet retrouve une variable cachee : il ouvre son dossier et ses
        // parents ("Armoires", "Armoires[0]"...) avant elle.
        if (cmd == "simulation-deplier") {
            if (!pane->setExpanded(arg(1), true)) fail("simulation-deplier : rien \xC3\xA0 d\xC3\xA9plier : " + arg(1));
        } else if (cmd == "simulation-forcer" || cmd == "simulation-suivre") {
            // Comme sim-forcer : la simulation se prepare au besoin (sans se lancer).
            if (!app_.simulation().attached() && app_.project()) (void)app_.simulation().attach(app_.project());
            if (!pane->selectVariable(arg(1))) { fail(cmd + " : variable introuvable : " + arg(1)); return Step::Next; }
            if (cmd == "simulation-suivre") {
                // Suivre bascule : deja suivie, elle le reste (un script rejoue deux fois).
                const auto want = lower(arg(1));
                const auto watching = pane->watched();
                if (std::none_of(watching.begin(), watching.end(), [&](const std::string& s) { return lower(s) == want; }))
                    pane->runAction(SimulationPane::AWatch);
            } else if (arg(2).empty()) {
                // "" : la relacher, comme sim-forcer (le programme la reprend).
                pane->runAction(SimulationPane::ARelease);
            } else if (!pane->forceSelected(arg(2))) {
                fail("simulation-forcer : for\xC3\xA7" "age refus\xC3\xA9 : " + arg(1) + " = " + arg(2));
            }
        } else if (cmd == "simulation-vitesse") {
            std::string v = lower(arg(1));
            if (!v.empty() && v.front() == 'x') v.erase(0, 1);          // x10 : 10
            if (v != "1" && v != "10" && v != "0") { fail("simulation-vitesse : 1, 10 ou 0 (au plus vite), pas " + arg(1)); return Step::Next; }
            pane->setSpeed(std::atoi(v.c_str()));
        } else {                                                        // simulation-politique
            const auto v = lower(arg(1));
            if (v.rfind("cont", 0) == 0) pane->setUnknownPolicy(true);
            else if (v.rfind("arr", 0) == 0) pane->setUnknownPolicy(false);
            else { fail("simulation-politique : continuer ou arreter, pas " + arg(1)); return Step::Next; }
        }
        sinceInput_ = 0;
        return Step::Yield;
    }
    if (cmd == "statistiques-vue") {
        auto* pane = apiPaneOf<StatisticsPane>(app_, "statistiques", /*open=*/true);
        if (!pane) { fail("l'onglet API \xE2\x80\xBA Statistiques ne s'ouvre pas : pas de projet, ou un dialogue ouvert"); return Step::Next; }
        if (!pane->chooseBreakdown(arg(1)))       // "entree", "section", "langage" (le debut suffit)
            fail("statistiques-vue : entree, section ou langage, pas " + arg(1));
        sinceInput_ = 0;
        return Step::Yield;
    }

    // ---- lot API 6 ---------------------------------------------------------------
    // L'editeur des macros (le mode Modifier), dans l'onglet courant.
    if (cmd.rfind("editeur-", 0) == 0) {
        MacroEditorView* ed = nullptr;
        if (auto* page = currentPage())
            walk(*page, [&](ui::Widget& x) {
                if (auto* e = dynamic_cast<MacroEditorView*>(&x); !ed && e && shown(*e)) ed = e;
            });
        if (!ed) { fail("pas d'\xC3\xA9" "diteur de macro dans l'onglet courant"); return Step::Next; }
        if (cmd == "editeur-carte") {
            ed->selectQuestion(arg(1));
            if (ed->selectedQuestion() != arg(1)) fail("question introuvable : " + arg(1));
            return Step::Yield;
        }
        if (cmd == "editeur-deplacer") {
            const std::string group = arg(2) == "-" ? std::string{} : arg(2);
            const auto index = w.size() > 3 ? static_cast<std::size_t>(std::max(0.f, num(3))) : static_cast<std::size_t>(-1);
            if (!ed->moveQuestion(arg(1), group, index)) fail("carte non d\xC3\xA9plac\xC3\xA9" "e : " + arg(1));
            return Step::Yield;
        }
        if (cmd == "editeur-ajouter") {
            if (!ed->addQuestion(arg(1), arg(2), arg(3), w.size() > 4 ? arg(4) : std::string{})) fail("question refus\xC3\xA9" "e : " + arg(1));
            return Step::Yield;
        }
        if (cmd == "editeur-onglet") { ed->setSideTab(arg(1) == "apercu" ? 1 : 0); return Step::Yield; }
        if (cmd == "editeur-essayer") { ed->tryRun(); return Step::Yield; }
        if (cmd == "editeur-verifier") { ed->check(); return Step::Yield; }
        if (cmd == "editeur-ligne") { ed->goToLine(static_cast<std::size_t>(std::max(1.f, num(1)))); return Step::Yield; }
        if (cmd == "editeur-annuler") { if (!ed->undoStep()) fail("rien \xC3\xA0 annuler dans l'\xC3\xA9" "diteur"); return Step::Yield; }
        if (cmd == "editeur-enregistrer") { if (!ed->save(arg(1), w.size() > 2 ? arg(2) : std::string{})) fail("non enregistr\xC3\xA9" "e"); return Step::Yield; }
        if (cmd == "editeur-texte") {
            // Taper a la fin d'une ligne (1...) : comme au clavier, puis le releve.
            ed->goToLine(static_cast<std::size_t>(std::max(1.f, num(1))));
            send(ui::KeyDown{Key::End, {}, false});
            send(ui::KeyDown{Key::Return, {}, false});
            send(ui::TextInput{arg(2)});
            return Step::Yield;
        }
        if (cmd == "editeur-contient" || cmd == "editeur-sans") {
            const bool present = ed->source().find(arg(1)) != std::string::npos;
            if (present != (cmd == "editeur-contient")) fail(std::string(cmd == "editeur-contient" ? "le code ne contient pas : " : "le code contient encore : ") + arg(1));
            return Step::Next;
        }
        if (cmd == "editeur-remarque") {
            bool found = false;
            for (const auto& r : ed->remarks()) found = found || r.text.find(arg(1)) != std::string::npos;
            if (!found) fail("pas de remarque : " + arg(1));
            return Step::Next;
        }
        if (cmd == "editeur-cartes") {
            // L'ordre attendu des cartes, separees par des virgules.
            std::string got;
            for (const auto& k : ed->questionKeys()) got += (got.empty() ? "" : ",") + k;
            if (got.rfind(arg(1), 0) != 0) fail("cartes : " + got + " (attendu : " + arg(1) + "...)");
            return Step::Next;
        }
        fail("commande inconnue : " + cmd);
        return Step::Next;
    }
    // L'editeur de l'icone du projet (le dialogue du dessus).
    if (cmd.rfind("icone-", 0) == 0) {
        auto* ed = dynamic_cast<PixelIconEditor*>(app_.menus().top());
        if (!ed) { fail("l'\xC3\xA9" "diteur d'ic\xC3\xB4ne n'est pas ouvert"); return Step::Next; }
        auto& st = ed->state();
        if (cmd == "icone-dessin") { ed->choosePreset(arg(1)); root->invalidate(); return Step::Yield; }
        if (cmd == "icone-onglet") {
            st.tab = arg(1) == "galerie" ? PixelIconEditor::Tab::Gallery : PixelIconEditor::Tab::Draw;
            root->invalidate();
            return Step::Yield;
        }
        if (cmd == "icone-outil") {
            static const std::pair<const char*, PixelIconEditor::Tool> kTools[] = {
                {"crayon", PixelIconEditor::Tool::Pencil}, {"gomme", PixelIconEditor::Tool::Eraser}, {"pot", PixelIconEditor::Tool::Bucket},
                {"pipette", PixelIconEditor::Tool::Picker}, {"ligne", PixelIconEditor::Tool::Line}, {"rectangle", PixelIconEditor::Tool::Rect}};
            if (arg(1) == "symetrie") st.mirror = !st.mirror;
            else if (arg(1) == "grille") st.grid = !st.grid;
            else {
                bool ok = false;
                for (const auto& [n, t] : kTools)
                    if (arg(1) == n) { ed->setTool(t); ok = true; }
                if (!ok) fail("outil inconnu : " + arg(1));
            }
            root->invalidate();
            return Step::Yield;
        }
        if (cmd == "icone-couleur") { ed->setColor(static_cast<int>(num(1))); root->invalidate(); return Step::Yield; }
        if (cmd == "icone-trait") {
            ed->strokeAt(static_cast<int>(num(1)), static_cast<int>(num(2)), true);
            if (w.size() > 4) ed->strokeAt(static_cast<int>(num(3)), static_cast<int>(num(4)), false);
            ed->strokeEnd();
            root->invalidate();
            return Step::Yield;
        }
        if (cmd == "icone-annuler") { ed->undoStep(); root->invalidate(); return Step::Yield; }
        fail("commande inconnue : " + cmd);
        return Step::Next;
    }
    // La galerie des themes (le dialogue du dessus) : un theme, par sa cle ou son libelle.
    if (cmd == "theme-galerie") {
        auto* g = dynamic_cast<ThemeGalleryDialog*>(app_.menus().top());
        if (!g) { fail("la galerie des th\xC3\xA8mes n'est pas ouverte"); return Step::Next; }
        g->choose(arg(1));
        return Step::Yield;
    }
    // Des versions d'essai, d'un coup (le volet Versions en montre beaucoup).
    if (cmd == "versions-fabriquer") {
        const auto folder = app_.projectFolder();
        if (folder.empty()) { fail("pas de dossier de projet"); return Step::Next; }
        auto store = hmi::ver::open(folder);
        if (!store) { fail("versions/ illisible"); return Step::Next; }
        const int n = static_cast<int>(num(1));
        static const hmi::ver::State kStates[] = {hmi::ver::State::Draft, hmi::ver::State::Auto, hmi::ver::State::Validated, hmi::ver::State::Auto,
                                                  hmi::ver::State::Draft, hmi::ver::State::Delivered};
        for (int i = 0; i < n; ++i) {
            const auto state = kStates[static_cast<std::size_t>(i) % (sizeof kStates / sizeof kStates[0])];
            char date[32];
            std::snprintf(date, sizeof date, "2026-09-%02d %02d:%02d", 1 + (i / 24) % 27, i % 24, (i * 7) % 60);
            auto made = hmi::ver::create(*store, state == hmi::ver::State::Auto ? std::string{} : "Essai " + std::to_string(i + 1), state, "", "Quentin", date);
            if (!made) { fail("version " + std::to_string(i + 1) + " : " + made.error().context); return Step::Next; }
        }
        app_.events().publish(ProjectSaved{});
        return Step::Yield;
    }
    if (cmd.rfind("frise-", 0) == 0) {
        VersionsPane* pane = nullptr;
        if (auto* page = currentPage())
            walk(*page, [&](ui::Widget& x) { if (!pane) pane = dynamic_cast<VersionsPane*>(&x); });
        if (!pane) { fail("pas de volet Versions dans l'onglet courant"); return Step::Next; }
        if (cmd == "frise-defiler") { pane->scrollTimeline(num(1)); return Step::Yield; }
        if (cmd == "frise-molette") {
            const auto s = pane->timelineStrip();
            send(ui::MouseWheel{{s.x + s.w * 0.5f, s.y + 40.f}, 0.f, num(1), {}});
            return Step::Yield;
        }
        if (cmd == "frise-verifier") {
            // Les points montres ne se chevauchent pas ; la version choisie se voit ;
            // au-dela de ce qui tient, la frise defile.
            const auto& vs = pane->store().versions;
            float last = -1e9f;
            int shown = 0;
            for (const auto& v : vs) {
                if (!pane->dotVisible(v.number)) continue;
                const auto r = pane->dotRect(v.number);
                const float x = r.x + r.w * 0.5f;
                if (shown > 0 && x - last < 90.f) { fail("V" + std::to_string(v.number) + " chevauche la pr\xC3\xA9" "c\xC3\xA9" "dente (" + std::to_string(static_cast<int>(x - last)) + " px)"); return Step::Next; }
                last = x;
                ++shown;
            }
            if (vs.size() > 12 && !pane->timelineScrolls()) fail("la frise devrait d\xC3\xA9" "filer");
            const int sel = pane->selectedVersion();
            if (sel > 0 && !pane->dotVisible(sel)) fail("la version choisie (V" + std::to_string(sel) + ") est hors de la frise");
            if (w.size() > 1 && shown < static_cast<int>(num(1))) fail("seulement " + std::to_string(shown) + " points montr\xC3\xA9s");
            return Step::Next;
        }
        if (cmd == "frise-choisir") { pane->selectVersion(static_cast<int>(num(1))); return Step::Yield; }
        fail("commande inconnue : " + cmd);
        return Step::Next;
    }

    auto* editor = currentEditor();
    if (!editor) { fail("l'onglet courant n'est pas une vue IHM (" + cmd + ")"); return Step::Next; }
    const auto doc = app_.hmi();
    const auto* view = doc ? doc->project.view(editor->viewId()) : nullptr;
    if (!view) { fail("vue introuvable"); return Step::Next; }

    // Lot 12 : palette-variable "Armoires[0]" [glisser X Y] - l'onglet Variables de
    // la bibliotheque : un clic sur la variable (puis vue-clic la pose), ou la
    // variable glissee jusqu'au point (X, Y) de la vue et lachee.
    if (cmd == "palette-variable") {
        auto& pal = editor->palette();
        if (pal.mode() != HmiPalette::Mode::Variables) {
            gfx::Rect tab;
            if (!pal.modeRect(HmiPalette::Mode::Variables, tab)) { fail("bibliotheque invisible"); return Step::Next; }
            click(centre(tab), MouseButton::Left, 1, {});
            return Step::Retry;
        }
        gfx::Rect r;
        if (!pal.variableRect(arg(1), r)) {
            pal.revealVariable(arg(1));
            if (retries_ < 3) return Step::Retry;
            fail("variable absente de la bibliotheque : " + arg(1));
            return Step::Next;
        }
        if (has("glisser")) {
            std::size_t at = 0;
            for (std::size_t i = 0; i < w.size(); ++i) if (w[i] == "glisser") at = i;
            const double x = at + 1 < w.size() ? std::atof(w[at + 1].c_str()) : 0, y = at + 2 < w.size() ? std::atof(w[at + 2].c_str()) : 0;
            drag(centre(r), editor->canvas().viewport().toScreen(x, y), {});
            return Step::Yield;
        }
        if (has("survol")) moveTo(centre(r), {});
        else click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "palette") {
        // Lot 12 : la bibliotheque revient a ses objets.
        if (editor->palette().mode() != HmiPalette::Mode::Objects) {
            gfx::Rect tab;
            if (editor->palette().modeRect(HmiPalette::Mode::Objects, tab)) {
                click(centre(tab), MouseButton::Left, 1, {});
                return Step::Retry;
            }
        }
        // Lot 10 : palette "Carte_Armoire" symbole - la tuile d'un symbole du projet.
        if (has("symbole")) {
            gfx::Rect r;
            if (!editor->palette().symbolTileRect(arg(1), r)) {
                editor->palette().revealSymbol(arg(1));
                if (retries_ < 3) return Step::Retry;
                fail("symbole absent de la bibliotheque : " + arg(1));
                return Step::Next;
            }
            if (has("survol")) moveTo(centre(r), {});
            else click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        for (auto k : hmi::kPlaceableKinds) {
            if (lower(hmi::kindLabel(k)) != lower(arg(1))) continue;
            gfx::Rect r;
            if (!editor->palette().tileRect(k, r)) {
                editor->palette().revealKind(k);                 // comme la molette le ferait
                if (retries_ < 3) return Step::Retry;
                const auto pb = editor->palette().bounds();
                fail("tuile cachee : " + arg(1) + " (bibliotheque " + std::to_string(static_cast<int>(pb.w)) + " x "
                     + std::to_string(static_cast<int>(pb.h)) + ")");
                return Step::Next;
            }
            // "etoile" : l'etoile de la tuile (ajouter / retirer des favoris) ;
            // "survol" (lot 8) : la souris sur la tuile, sans cliquer (F1 ensuite).
            if (has("etoile")) click({r.x + r.w - 10.f, r.y + 10.f}, MouseButton::Left, 1, {});
            else if (has("survol")) moveTo(centre(r), {});
            else click(centre(r), MouseButton::Left, 1, {});
            return Step::Yield;
        }
        fail("genre inconnu : " + arg(1));
        return Step::Next;
    }
    if (cmd == "vue-clic") {
        const auto p = editor->canvas().viewport().toScreen(num(1), num(2));
        click(p, MouseButton::Left, 1, mods());
        if (has("double")) click(p, MouseButton::Left, 2, mods());
        return Step::Yield;
    }
    if (cmd == "vue-glisser") {
        const auto vp = editor->canvas().viewport();
        // Lot 12 : "tenir" - la souris reste appuyee (vue-lacher la relache) : une
        // capture montre les reperes et les ecarts egaux du glisser en cours.
        drag(vp.toScreen(num(1), num(2)), vp.toScreen(num(3), num(4)), mods(), !has("tenir"));
        return Step::Yield;
    }
    if (cmd == "vue-zoom") {
        // Lot 12 : vue-zoom 100 [X Y] - le zoom de l'editeur (en %) ; le point (X, Y)
        // de la vue en haut a gauche de la zone de dessin.
        auto& canvas = editor->canvas();
        const float pct = num(1);
        if (pct <= 0) { fail("vue-zoom : un pourcentage"); return Step::Next; }
        canvas.setZoom(pct / 100.f);
        if (w.size() > 3) canvas.scrollTo(num(2), num(3));
        return Step::Yield;
    }
    if (cmd == "vue-lacher") {
        send(ui::MouseUp{driver_.mouse(), MouseButton::Left, mods()});
        return Step::Yield;
    }
    // 1.10 (chantier O) : objet-alarmes NOM [n] - dans l'explorateur, l'objet
    // deplie sur son noeud Alarmes, deplie lui aussi ; avec n, un clic sur la
    // ligne n de ce noeud (1 : la premiere alarme ; l'inspecteur s'ouvre sur elle).
    if (cmd == "objet-alarmes") {
        hmi::Id id = hmi::kNoId;
        for (const auto& o : view->objects) if (o.name == arg(1)) id = o.id;
        if (id == hmi::kNoId || !editor->objects().alarmNode(id)) { fail("objet sans alarmes : " + arg(1)); return Step::Next; }
        if (!editor->objects().alarmsOpen(id)) {
            editor->objects().setAlarmsOpen(id, true);
            editor->objects().reveal(id);
            return Step::Retry;
        }
        if (arg(2).empty()) return Step::Yield;
        gfx::Rect r;
        if (!editor->objects().alarmRowRect(id, static_cast<int>(num(2)), r)) { fail("ligne d'alarme introuvable : " + arg(2)); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    // 1.10.3 (Q1103) : objet-famille NOM [FAMILLE [n]] - dans l'explorateur d'objets,
    // l'objet deplie par familles (comme l'arbre) ; avec FAMILLE ("Actions", "Liens fx",
    // "Reperes"...), cette famille depliee ; avec n, un clic sur sa ligne n (1 : la
    // premiere ; 0 : le titre de la famille).
    if (cmd == "objet-famille") {
        hmi::Id id = hmi::kNoId;
        for (const auto& o : view->objects) if (o.name == arg(1)) id = o.id;
        if (id == hmi::kNoId) { fail("objet introuvable : " + arg(1)); return Step::Next; }
        auto& list = editor->objects();
        if (arg(2).empty()) { list.setObjectOpen(id, true); list.reveal(id); return Step::Yield; }
        static const char* ascii[] = {"actions", "liens", "parametres", "animations", "reperes", "elements", "securite"};
        int fam = -1;
        for (int f = 0; f < static_cast<int>(hmitree::Family::Count) && fam < 0; ++f)
            if (startsWith(hmitree::familyLabel(static_cast<hmitree::Family>(f)), arg(2))
                || (f < static_cast<int>(std::size(ascii)) && startsWith(ascii[f], arg(2))))
                fam = f;
        if (fam < 0) { fail("famille inconnue : " + arg(2)); return Step::Next; }
        gfx::Rect r;
        if (!list.familyRowRect(id, fam, -1, r) || (!arg(3).empty() && !list.familyRowRect(id, fam, static_cast<int>(num(3)) - 1, r))) {
            list.setFamilyOpen(id, fam, true);
            list.reveal(id);
            if (retries_ < 3) return Step::Retry;
            fail("famille ou ligne introuvable : " + arg(1) + " / " + arg(2) + " " + arg(3));
            return Step::Next;
        }
        list.setFamilyOpen(id, fam, true);
        if (arg(3).empty()) return Step::Yield;
        (void)list.familyRowRect(id, fam, static_cast<int>(num(3)) - 1, r);
        click(centre(r), MouseButton::Left, 1, {});
        return Step::Yield;
    }
    if (cmd == "objet" || cmd == "calque") {
        const int part = has("oeil") ? 1 : has("verrou") ? 2 : 0;
        gfx::Rect r;
        bool ok = false;
        if (cmd == "objet") {
            hmi::Id id = hmi::kNoId;
            for (const auto& o : view->objects) if (o.name == arg(1)) id = o.id;
            ok = id != hmi::kNoId && editor->objects().rowRect(id, r, part);
            if (!ok && id != hmi::kNoId && retries_ < 3) {
                // Hors de la liste visible : l'explorateur defile jusqu'a lui
                // (comme la molette), puis le clic se fait normalement.
                editor->objects().reveal(id);
                return Step::Retry;
            }
        } else {
            for (const auto& l : view->layers)
                if (l.name == arg(1)) ok = editor->layers().rowRect(l.id, r, part);
        }
        if (!ok) { fail(cmd + " introuvable ou cache : " + arg(1)); return Step::Next; }
        click(centre(r), MouseButton::Left, 1, mods());
        if (has("double")) click(centre(r), MouseButton::Left, 2, mods());
        return Step::Yield;
    }

    fail("commande inconnue : " + cmd);
    return Step::Next;
}

} // namespace app
