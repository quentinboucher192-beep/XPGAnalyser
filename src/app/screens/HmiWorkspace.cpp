// =============================================================================
//  app/screens/HmiWorkspace.cpp - le dossier IHM de l'espace de travail
// -----------------------------------------------------------------------------
//  L'arbre du projet porte un dossier IHM (ViewModels.hpp) ; chacune de ses
//  entrees ouvre ici un onglet, a cote des sections et des grafcets :
//
//    Configuration          HmiConfigPane  identite, resolution, statistiques,
//                                          variables du programme
//    Vues                   HmiViewsPane   creer, dupliquer, ordonner, supprimer
//    <une vue>              HmiEditor      l'editeur de la vue
//    <une vue> > Scripts    HmiScriptsPane OnOpen, OnCycle, OnClose de la vue
//    Generer / Compiler     HmiReportPane  le rapport, double-clic = la source
//                                          (la vue, l'objet, l'action, la ligne)
//    Ressources             HmiResourcesPane   images, sons, videos, polices
//    Fichiers externes      HmiFilesPane   Excel, CSV, TXT, JSON, XML, SQLite, bases
//    Simulation             HmiSimulationPane  l'IHM qui tourne : scripts, actions,
//                                          navigation animee, popups, journal
//    Programmation generale HmiScriptsPane scripts generaux, variables IHM
//      > Fonctions          HmiFunctionsPane les fonctions IHM du projet (lot 7)
//    Configuration > Alarmes, Recettes, Utilisateurs, Historiques (lot 4)
//                           HmiAlarmsPane, HmiRecipesPane, HmiUsersPane, HmiHistoryPane
//    Exporter / Importer    HmiExchangePane  archive .zip, reconstruction, controle
//    Aide (F1)              HmiHelpPane    le guide de l'IHM, et le didacticiel (lot 7)
//    (leurs dialogues : HmiSupervision.cpp)
//
//  TOUT PASSE PAR LA PILE DE L'APPLICATION (App::apply, sans republier le
//  projet : l'IHM ne change rien au programme). Le bouton Annuler de la barre,
//  Ctrl+Z dans l'editeur de vues et Ctrl+Z ailleurs defont la meme chose.
//
//  UN ONGLET EST RETROUVE PAR SA PAGE. Fermer un onglet a gauche decale les
//  indices de ceux de droite ; un pointeur de page ne bouge pas. La page est
//  verifiee avant usage (TabControl::indexOf) : un onglet ferme n'est jamais
//  touche.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../BackgroundTasks.hpp"      // Lot API 8 : bandeau haut (la cloche)
#include "../../hmi/HmiBuildState.hpp"  // 1.11 (chantier T3, C4) : les icones compilable / generable
#include "../../hmi/HmiDecl.hpp"   // 1.11.18 (refonte, lot 3) : les declarations du modele
#include "../hmi/HmiDesignPanes.hpp"
#include "../hmi/HmiAssetPanes.hpp"
#include "../hmi/HmiAssist.hpp"
#include "../hmi/HmiEditor.hpp"
#include "../hmi/HmiValuePicker.hpp"              // 1.11.3 : le selecteur de valeur
#include "../../project/EditCommands.hpp"             // 1.11.3 : creer une variable de l'automate
#include "../hmi/HmiDuplicateDialog.hpp"            // 1.10.2 (chantier D) : "Dupliquer..."
#include "../hmi/HmiFunctionPanes.hpp"
#include "../hmi/HmiHelpPane.hpp"
#include "../hmi/HmiTutorial.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiGuide.hpp"
#include "../../help/F1Table.hpp"   // 1.11 (T2, tranche 16) : F1 ouvre le centre, au sujet de l'endroit
#include "../../hmi/HmiPublicVars.hpp"
#include "../hmi/HmiImages.hpp"
#include "../hmi/HmiPanes.hpp"
#include "../hmi/HmiAskDialog.hpp"                 // 1.11.18 (lot 5) : migrer les declarations (les codes a cocher)
#include "../../hmi/HmiMigrate.hpp"                // 1.11.18 (lot 5) : le plan de la migration
#include "../hmi/HmiPublicVarsPane.hpp"
#include "../hmi/HmiQualityPanes.hpp"                // lot 13 : les essais
#include "../hmi/HmiDisplayPanes.hpp"               // lot 13 : les langues
#include "../hmi/HmiCommPanes.hpp"                  // lot 14 : la communication
#include "../hmi/HmiModbusToolPane.hpp"              // lot 15 : l'outil Modbus
#include "../hmi/HmiCyclicPage.hpp"                  // 1.9 : la lecture cyclique (importer un jeu)
#include "../../hmi/HmiComm.hpp"                     // 1.9 : le plan de la liaison (les variables de l'API a lire)
#include "../VersionsPane.hpp"
#include "../../hmi/HmiVersionState.hpp"   // lot API 6 : la version en cours                       // lot 21 : les versions
#include "../hmi/HmiVariablePanes.hpp"               // lot 16 : variables et types IHM
#include "../hmi/HmiEnumPanes.hpp"                   // 1.10 (chantier U) : choisir une valeur depuis l'arbre
#include "../ExportTarget.hpp"                       // 1.10 (chantier O) : exporter les valeurs du graphique
#include "../../hmi/HmiTypes.hpp"
#include "../hmi/HmiStationPanes.hpp"               // lot 14 : le poste d'exploitation
#include "../hmi/HmiNotifyPanes.hpp"                // lot 14 : les notifications
#include "../hmi/HmiReportPanes.hpp"                // lot 14 : les rapports
#include "../hmi/HmiBuild.hpp"                      // 1.11.13 : la generation incrementale
#include "../hmi/HmiBuildPanes.hpp"                 // 1.11.13 : ses sorties
#include "../hmi/HmiWebPanes.hpp"                   // lot 14 : l'acces web
#include "../hmi/HmiScriptPanes.hpp"
#include "../hmi/HmiSimulation.hpp"
#include "../hmi/HmiParamPanes.hpp"   // 1.9 : les parametres des popups (un clic dans les infos)
#include "../hmi/HmiSupervisionPanes.hpp"
#include "../hmi/HmiObjectAlarmPanes.hpp"           // 1.9 : les alarmes generees (double-clic)
#include "../hmi/HmiOperatorPanes.hpp"              // 1.10 (integration I2) : Compiler -> le script d'un operateur
#include "../hmi/HmiObjectAlarmTree.hpp"            // 1.10 (chantier O) : le noeud Alarmes d'un objet dans l'arbre
#include "../hmi/HmiTreeData.hpp"
#include "../../sim/Runtime.hpp"
#include "../../hmi/HmiAssets.hpp"
#include "../../hmi/HmiCheck.hpp"
// 1.10 (decision 12) : l'etat de la fenetre avant le plein ecran de l'IHM.
#if defined(XPG_WITH_SDL) && __has_include(<SDL3/SDL.h>)
#  include <SDL3/SDL.h>
#  define XPG_HMI_FULL_SDL 1
#else
#  define XPG_HMI_FULL_SDL 0
#endif
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiStore.hpp"
#include "../../hmi/HmiHistory.hpp"
#include "../../hmi/HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : les types, un seul catalogue
#include "../ApiPanes.hpp"     // 1.11.19 (lot 6) : Ouvrir la definition d'un DDT
#include "../TypePanes.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <set>
#include <system_error>

namespace app {

using ui::Icon;
using ui::TabControl;
using NK = ProjectTreeModel::NodeKind;

namespace {

// Publie apres une commande IHM, recu a l'image suivante (EventBus::drain) :
// fermer l'onglet d'une vue qui n'existe plus ne peut pas se faire PENDANT la
// commande, qui vient peut-etre d'un clic dans cet onglet-la.
struct HmiTabsCheck {};

// L'arbre et l'ecran parlent en 64 bits (NodeId) ; le modele IHM en hmi::Id.
hmi::Id asId(std::uint64_t v) { return static_cast<hmi::Id>(v); }
// 1.10.4 : les copies du prochain "Dupliquer..." (3 ; 0 : "Remplacer..." de Compiler).
int& nextDuplicateCopies() {
    static int copies = 3;
    return copies;
}

// Le poids de ihm/ sur le disque : le "poids projet" de la configuration.
std::uint64_t folderBytes(const std::filesystem::path& folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::uint64_t total = 0;
    if (!fs::exists(folder, ec)) return 0;
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file(ec)) total += it->file_size(ec);
    }
    return total;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Les noms que le programme declare, pour "variable inexistante". Sans
// distinction de casse : Control Expert n'en fait pas.
hmi::NameExists plcNames(const std::shared_ptr<const domain::Project>& plc) {
    if (!plc) return {};
    // Les GLOBALES seulement : une vue ne lit pas la variable locale d'un bloc.
    auto names = std::make_shared<std::set<std::string, std::less<>>>();
    for (const auto& v : plc->variables)
        if (v.scope == domain::VariableScope::Global) names->insert(upper(plc->strings.text(v.name)));
    return [names](std::string_view root) { return names->count(upper(root)) > 0; };
}

// Un chemin colle depuis l'explorateur ("Copier en tant que chemin" l'entoure
// de guillemets) ou tape avec des espaces autour.
std::string cleanPath(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    s.erase(0, i);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// "Vue_Armoire_A/Logo.image, Vue_Accueil/Fond.image (+3)" : qui cite, en une ligne.
std::string citedBy(const std::vector<hmi::Citation>& uses) {
    std::string out;
    for (std::size_t i = 0; i < uses.size() && i < 3; ++i) out += (i ? ", " : "") + uses[i].where;
    if (uses.size() > 3) out += " (+" + std::to_string(uses.size() - 3) + ")";
    return out;
}

// Ce que l'aide a la saisie d'un script demande : le programme de l'automate
// et, pendant la simulation IHM, la valeur d'un nom (IHM d'abord, puis
// l'automate, comme le script la lit).
template <class Pane>
void wireAssist(Pane& pane, App& app, std::function<HmiSimulationPane*()> simulation) {
    pane.setAssist([&app] { return app.project(); },
                   [simulation](std::string_view name, std::string& out) {
                       auto* sim = simulation ? simulation() : nullptr;
                       if (!sim || !sim->runtime().running()) return false;
                       ::sim::Value v;
                       if (!sim->runtime().environment().read(name, v)) return false;
                       out = v.display();
                       return true;
                   });
}

struct PaneInfo { const char* title; Icon icon; std::vector<std::string> lines; };

// Les entrees dont le contenu arrive dans un lot suivant. Elles existent deja
// dans l'arbre, dans l'ordre de la specification, et disent ce qu'elles feront.
PaneInfo laterPane(const std::string&) {
    return {"IHM", Icon::Screen, {"Cette entr\xC3\xA9" "e n'a pas encore de volet."}};
}

// Le libelle d'un declencheur de script general (le dialogue), et l'inverse.
const std::vector<std::string>& scriptTriggerLabels() {
    static const std::vector<std::string> l = {"D\xC3\xA9marrage", "Cyclique", "Sur changement", "Appel\xC3\xA9"};
    return l;
}
std::string scriptTriggerKey(const std::string& label) {
    for (std::size_t i = 0; i < std::size(hmi::kGeneralEvents); ++i)
        if (scriptTriggerLabels()[i] == label) return std::string(hmi::kGeneralEvents[i]);
    return "Appel";
}

// Qui appelle ce script general : les actions "Script general" et les
// IHM_APPELER des autres scripts. Pour la confirmation de suppression.
std::vector<std::string> callersOf(const hmi::Project& p, const std::string& name) {
    std::vector<std::string> out;
    for (const auto& v : p.views) {
        for (const auto& a : v.actions)
            if (a.operation == hmi::Operation::CallScript && a.target == name) out.push_back(v.name + " (action de vue)");
        for (const auto& o : v.objects)
            for (const auto& a : o.actions)
                if (a.operation == hmi::Operation::CallScript && a.target == name) out.push_back(v.name + "/" + o.name);
        for (const auto& sc : v.scripts)
            for (const auto& c : hmi::scriptCalls(sc.body))
                if (c == name) out.push_back(v.name + "." + sc.event);
    }
    for (const auto& sc : p.programs.scripts)
        if (sc.name != name)
            for (const auto& c : hmi::scriptCalls(sc.body))
                if (c == name) out.push_back(sc.name);
    return out;
}

std::string joinFew(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size() && i < 3; ++i) out += (i ? ", " : "") + items[i];
    if (items.size() > 3) out += " (+" + std::to_string(items.size() - 3) + ")";
    return out;
}

// "action 2 (Clic)" -> 1 : le rang d'une action dans un constat.
int actionIndexOf(const std::string& where) {
    if (where.rfind("action ", 0) != 0) return -1;
    int n = 0;
    std::size_t i = 7;
    while (i < where.size() && std::isdigit(static_cast<unsigned char>(where[i]))) n = n * 10 + (where[i++] - '0');
    return n > 0 ? n - 1 : -1;
}

// ---- Lot API 8 : les expressions impossibles (l'arbre, la cloche) ----
bool impossibleExpression(const hmi::Issue& i) {
    if (i.severity != hmi::Issue::Severity::Error) return false;
    return i.category == "Expression" || i.message.find("n'existe pas") != std::string::npos;
}

} // namespace

bool MainAnalysisScreen::isHmiNode(ui::NodeId n) {
    const auto k = ProjectTreeModel::kindOf(n);
    return (k >= NK::HmiFolder && k <= NK::HmiScripts) || (k >= NK::HmiAlarms && k <= NK::HmiHistory)
        || (k >= NK::HmiObject && k <= NK::HmiUsedVariable)       // lot 5 : ce qui se deballe
        || k == NK::HmiFunctionsFolder || k == NK::HmiFunction      // lot 7 : les fonctions IHM
        || k == NK::HmiViewFolder || k == NK::HmiTemplateFolder     // lot 8 : les sous-dossiers des vues
        || (k >= NK::HmiSysFolder && k <= NK::HmiInstViewInfo)      // lot 9 : variables systeme et d'instances
        || (k >= NK::HmiInstParam && k <= NK::HmiInstAlarmVar)      // 1.11.1 (decision 108) : ce qu'un objet publie
        || k == NK::HmiSymbolsFolder                                 // lot 10 : les symboles
        || k == NK::HmiStyles || k == NK::HmiFind                    // lot 12 : les styles, rechercher
        || k == NK::HmiTests || k == NK::HmiLanguages || k == NK::HmiUnits    // lot 13 : essais, langues, unites
        || k == NK::HmiComm || k == NK::HmiStation                    // lot 14 : la communication, le poste,
        || k == NK::HmiNotify || k == NK::HmiReports || k == NK::HmiWeb    // les notifications, les rapports, le web
        || k == NK::HmiModbusTool                                            // lot 15 : l'outil Modbus
        || k == NK::HmiTypesFolder || k == NK::HmiTypeNode || k == NK::HmiVarFolder    // lot 16 : types, dossiers
        || k == NK::HmiTypeValues || k == NK::HmiTypeValue || k == NK::HmiTypeOperators || k == NK::HmiTypeOperator   // 1.10
        // 1.10.3 (Q1103) : sous un objet, les noeuds venus apres HmiUsedVariable (Alarmes,
        // Operateurs 1.10 ; familles, parametres, reperes 1.10.2) : sans eux, un clic n'y
        // faisait rien (openHmiNode les attendait pourtant).
        || k == NK::HmiObjectAlarms || k == NK::HmiObjectAlarm || k == NK::HmiObjectOperators || k == NK::HmiObjectOperator
        || (k >= NK::HmiObjectFunctions && k <= NK::HmiSymbolFunction)    // 1.11.10 : les fonctions et popups d'un symbole
        || k == NK::HmiObjectFamily || k == NK::HmiObjectParam || k == NK::HmiObjectMarker
        || k == NK::VersionsFolder || k == NK::VersionItem;                           // lot 21 : les versions
}

ui::Widget* MainAnalysisScreen::hmiTab(const std::string& key) const {
    const auto it = hmiTabs_.find(key);
    if (it == hmiTabs_.end() || !centre_ || centre_->indexOf(it->second) < 0) return nullptr;
    return it->second;
}

void MainAnalysisScreen::forgetHmiTab(std::size_t tabIndex) {
    if (!centre_) return;
    const auto* page = centre_->page(tabIndex);
    for (auto it = hmiTabs_.begin(); it != hmiTabs_.end();) {
        if (it->second == page) it = hmiTabs_.erase(it);
        else ++it;
    }
}

// 1.11 (chantier T3, C4) : le cache de hmi::build. Jamais a chaque image :
// l'arbre le lit ; il est refait ici (au chargement, quand l'arbre se refait,
// a Compiler), et un script n'est recalcule que si son texte a change.
void MainAnalysisScreen::refreshBuildState(bool full) {
    const auto plc = app_.project();
    const auto doc = app_.hmi();
    if (!buildState_) buildState_ = std::make_shared<hmi::build::Cache>();
    if (full || plc.get() != buildStateOf_) {
        buildState_->rebuild(plc.get(), doc ? &doc->project : nullptr);
        buildStateOf_ = plc.get();
        hmiPublishPlcNames(plc.get());   // 1.11.3 : les globales de l'automate pour le moteur IHM
    } else {
        // Les scripts supprimes (ou ceux d'une IHM fermee) quittent le cache.
        static const hmi::Project none{};
        buildState_->syncScripts(doc ? doc->project : none);
    }
    if (treeModel_) treeModel_->setBuildState(buildState_);
    if (explorer_) explorer_->invalidate();
}

void MainAnalysisScreen::bindHmi() {
    auto doc = app_.hmi();
    if (treeModel_) treeModel_->setHmi(doc);
    refreshBuildState();        // 1.11 (chantier T3, C4) : les icones compilable / generable
    // 1.10 (chantier O) : deplier un objet montre ses alarmes (le noeud Alarmes
    // de l'arbre ; le modele ne lie pas l'IHM : on lui donne de quoi les trouver).
    if (treeModel_)
        treeModel_->setObjectAlarms([weak = std::weak_ptr<const hmi::Document>(doc)](std::uint64_t viewId, std::uint64_t objectId)
                                        -> std::shared_ptr<const alarmtree::Group> {
            const auto d = weak.lock();
            const auto* view = d ? d->project.view(static_cast<hmi::Id>(viewId)) : nullptr;
            const auto* object = view ? view->object(static_cast<hmi::Id>(objectId)) : nullptr;
            if (!object) return nullptr;
            auto node = alarmtree::nodeOf(d->project, *view, *object);
            return node ? std::make_shared<const alarmtree::Group>(std::move(*node)) : nullptr;
        });
    refreshVersions();      // lot 21 : les versions, a la racine de l'arbre
    // L'aide a la saisie des volets IHM (champs variables, expressions, textes
    // a trous) : le programme de l'automate, et la simulation IHM en marche.
    assist::installProgram([app = &app_] { return app->project(); },
                           [this](std::string_view name, std::string& out) {
                               auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
                               if (!sim || !sim->runtime().running()) return false;
                               ::sim::Value v;
                               if (!sim->runtime().environment().read(name, v)) return false;
                               out = v.display();
                               return true;
                           });
    // 1.9 : un clic dans les infos d'une popup (qui l'ouvre) mene a l'objet d'une autre vue.
    hmiparams::setOpenObjectHost([this](const std::string& viewName, const std::string& object) {
        const auto hdoc = app_.hmi();
        const auto* v = hdoc ? hdoc->project.viewByName(viewName) : nullptr;
        if (!v) return;
        std::uint64_t obj = 0;
        for (const auto& o : v->objects)
            if (o.name == object) obj = o.id;
        openHmiView(v->id, -1, obj);
    });
    // 1.11.19 (refonte, lot 6) : le selecteur de types, demande par une grille ou un volet.
    typepicker::setHost([this](HmiTypePicker::Spec spec, typepicker::Done done) { askHmiType(std::move(spec), std::move(done)); });
    // Les chemins relatifs des fichiers externes partent du dossier du projet.
    setHmiProjectFolder(app_.projectFolder());

    if (doc != boundHmi_) {
        // Une autre IHM : ses onglets sont partis avec le projet precedent
        // (bindProject les a fermes), leurs connexions avec eux.
        hmiTabs_.clear();
        hmiLinks_.clear();
        boundHmi_ = doc;
        // 1.11.13 : une autre IHM - ses etats de build sont a refaire (l'ancienne n'en dit plus rien).
        hmiBuildMarksGen_ = ~0ull;
        if (treeModel_) treeModel_->setHmiBuildMarks(nullptr);
        if (hmiBuild_) hmiBuild_->analyseNow();
        if (!doc) return;
        hmiLinks_ += doc->changed->connect([this](hmi::Id v) { onHmiChanged(v); });
        hmiLinks_ += app_.events().subscribe<HmiTabsCheck>([this](const HmiTabsCheck&) {
            auto current = app_.hmi();
            if (!current || !centre_) return;
            // Les onglets des vues : le titre suit le nom, et celui d'une vue
            // qui n'existe plus se ferme (Ctrl+Z la rendra, on la rouvrira).
            std::vector<ui::Widget*> gone;
            for (const auto& [key, page] : hmiTabs_) {
                const int at = centre_->indexOf(page);
                if (at < 0) continue;
                hmi::Id view = hmi::kNoId;
                std::string prefix;
                if (auto* editor = dynamic_cast<HmiEditor*>(page)) view = editor->viewId();
                else if (auto* scripts = dynamic_cast<HmiScriptsPane*>(page); scripts && !scripts->general()) {
                    view = scripts->viewId();
                    prefix = "Scripts \xC2\xB7 ";
                }
                if (view == hmi::kNoId) continue;
                if (const auto* v = current->project.view(view))
                    centre_->setTabTitle(static_cast<std::size_t>(at), prefix + v->name);
                else
                    gone.push_back(page);
            }
            for (auto* page : gone)
                if (const int at = centre_->indexOf(page); at >= 0) closeDocument(static_cast<std::size_t>(at));
        });
        if (!app_.hmiWarnings().empty()) {
            const auto& w = app_.hmiWarnings();
            status_->setTransientMessage("IHM : " + w.front()
                                         + (w.size() > 1 ? "  (+" + std::to_string(w.size() - 1) + " autres)" : ""),
                                         10.0);
        }
        return;
    }

    // La meme IHM, un programme qui a change : les onglets restent ouverts et
    // relisent les variables du programme.
    for (const auto& [key, page] : hmiTabs_) {
        if (!centre_ || centre_->indexOf(page) < 0) continue;
        if (auto* editor = dynamic_cast<HmiEditor*>(page)) editor->setPlcProject(app_.project());
        if (auto* config = dynamic_cast<HmiConfigPane*>(page)) config->setPlc(app_.project(), app_.report().get());
        if (auto* report = dynamic_cast<HmiReportPane*>(page)) {
            report->setNameExists(plcNames(app_.project()));
            report->setPlcPaths(hmiPlcPaths(app_.project().get()));   // ---- Lot API 8 : les expressions impossibles ----
        }
    }
}

void MainAnalysisScreen::onHmiChanged(std::uint64_t viewId) {
    // La structure (vues ajoutees, retirees, reordonnees) : l'arbre refait ses
    // lignes. Le contenu d'une vue ne change que des libelles, relus au dessin.
    if (viewId == hmi::kNoId && treeModel_) treeModel_->hmiChanged();
    // 1.11 (chantier T3, C4) : un script modifie a ses icones a jour tout de suite
    // (seuls les scripts dont le texte ou le langage a change sont recalcules).
    refreshBuildState();
    // 1.11.13 : la generation incrementale - l'analyse repart (300 ms apres la derniere
    // modification) : l'element touche passe a « Modifie » dans l'arbre.
    if (hmiBuild_) hmiBuild_->invalidate();
    // 1.11.15 : qui a modifie - la simulation elle-meme (une recette, un jumeau...) ou le developpeur.
    if (hmiApplyingLive_ > 0) hmiLiveSeen_ = true;
    else hmiEditSeen_ = true;
    app_.events().publish(HmiTabsCheck{});
}

void MainAnalysisScreen::openHmiNode(ui::NodeId n) {
    if (!treeModel_ || !app_.hmi()) return;
    switch (ProjectTreeModel::kindOf(n)) {
        case NK::HmiConfig:        openHmiPane("config"); break;
        case NK::HmiExternalFiles: openHmiPane("fichiers"); break;
        case NK::HmiResources:     openHmiPane("ressources"); break;
        case NK::HmiExchange:      openHmiPane("echange"); break;
        case NK::HmiViews:         openHmiViewsFolder(-1); break;
        // Lot 8 : Modeles, Vues, Popups ; et les trois sortes de modeles.
        case NK::HmiViewFolder:    openHmiViewsFolder(static_cast<int>(ProjectTreeModel::indexOf(n))); break;
        case NK::HmiTemplateFolder: openHmiViewsFolder(3 + static_cast<int>(ProjectTreeModel::indexOf(n))); break;
        case NK::HmiSymbolsFolder: openHmiViewsFolder(6); break;   // lot 10
        case NK::HmiStyles:        openHmiPane("styles"); break;   // lot 12
        case NK::HmiFind:          openHmiPane("rechercher"); break;
        case NK::HmiTests:         openHmiPane("essais"); break;       // lot 13
        case NK::HmiLanguages:     openHmiPane("langues"); break;
        case NK::HmiUnits:         openHmiPane("unites"); break;
        case NK::HmiComm:          openHmiPane("communication"); break;   // lot 14
        case NK::HmiStation:       openHmiPane("poste"); break;
        case NK::HmiNotify:        openHmiPane("notifications"); break;
        case NK::HmiReports:       openHmiPane("rapports"); break;
        case NK::HmiWeb:           openHmiPane("web"); break;
        case NK::HmiView:          openHmiView(treeModel_->hmiViewOf(n)); break;
        case NK::HmiViewPart:
            // Les scripts d'une vue ont leur onglet ; les autres parties sont
            // dans l'editeur de la vue.
            if (ProjectTreeModel::subOf(n) == static_cast<domain::Index>(ProjectTreeModel::HmiPart::Scripts))
                openHmiScripts(treeModel_->hmiViewOf(n));
            else
                openHmiView(treeModel_->hmiViewOf(n), static_cast<int>(ProjectTreeModel::subOf(n)));
            break;
        case NK::HmiSimulation:    openHmiPane("simulation"); break;
        case NK::HmiModbusTool:    openHmiPane("outil"); break;          // lot 15
        case NK::HmiGenerate:      openHmiPane("generer"); break;
        case NK::HmiCompile:       openHmiPane("compiler"); break;
        case NK::HmiScripts:       openHmiPane("scripts"); break;
        case NK::HmiAlarms:        openHmiPane("alarmes"); break;
        case NK::HmiRecipes:       openHmiPane("recettes"); break;
        case NK::HmiUsers:         openHmiPane("utilisateurs"); break;
        case NK::HmiHistory:       openHmiPane("historiques"); break;
        // Lot 21 : les versions (a la racine, a cote de API et IHM).
        case NK::VersionsFolder:   openVersions(); break;
        case NK::VersionItem:      openVersions(treeModel_->versionOf(n)); break;

        // ---- lot 5 : ce qui se deballe ouvre la ou ca se modifie ---------------
        case NK::HmiObject:
        case NK::HmiGroupEntry:
            openHmiView(treeModel_->hmiViewOf(n), -1, treeModel_->hmiObjectOf(n));
            break;
        case NK::HmiObjectItem:
        case NK::HmiAnimation:
        case NK::HmiObjectFamily:          // 1.10.2 (chantier A) : une famille de l'objet,
        case NK::HmiObjectParam:           //   un parametre d'une instance,
        case NK::HmiObjectMarker: {        //   un repere (l'objet choisi)
            const auto viewId = treeModel_->hmiViewOf(n);
            const auto objectId = treeModel_->hmiObjectOf(n);
            openHmiView(viewId, -1, objectId);
            auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
            const auto* view = app_.hmi()->project.view(asId(viewId));
            const auto* object = view ? view->object(asId(objectId)) : nullptr;
            if (!editor || !object) break;
            // Une action : l'onglet Actions, sur elle. Une propriete : sa case,
            // dans l'inspecteur (l'expression d'une animation : "... (expression)").
            // 1.10.2 : une famille va a sa premiere ligne (Actions : l'onglet
            // Actions ; Liens fx, Animations : la 1re propriete) ; Parametres et
            // un parametre : la case Arguments de l'instance ; Elements : l'objet.
            // 1.10.3 (Q1103) : les lignes et leur clic sont ceux de l'explorateur
            // d'objets de la vue - un seul code (hmitree::familyLines, HmiEditor::showLine) ;
            // un repere ouvre la premiere case qui l'utilise.
            const auto kind = ProjectTreeModel::kindOf(n);
            const auto& project = app_.hmi()->project;
            hmitree::Line target;
            if (kind == NK::HmiObjectFamily)
                target = hmitree::familyTarget(project, *object, static_cast<hmitree::Family>(treeModel_->hmiRankOf(n)));
            else if (kind == NK::HmiObjectParam)
                target = hmitree::familyTarget(project, *object, hmitree::Family::Params);
            else if (kind == NK::HmiObjectMarker) {
                const auto lines = hmitree::familyLines(project, *object, hmitree::Family::Markers);
                const auto k = static_cast<std::size_t>(treeModel_->hmiRankOf(n));
                if (k < lines.size()) target = lines[k];
            } else if (kind == NK::HmiObjectItem) {
                const auto items = hmitree::overridesOf(*object);
                const auto k = static_cast<std::size_t>(treeModel_->hmiRankOf(n));
                if (k < items.size()) target = items[k];
            } else {
                const auto all = hmitree::animationsOf(*view);
                const auto k = static_cast<std::size_t>(treeModel_->hmiRankOf(n));
                if (k < all.size()) target.property = all[k].second->key;
            }
            editor->showLine(object->id, target);
            break;
        }
        // ---- 1.10 (chantier O) : le noeud Operateurs d'un objet : sa vue, l'objet choisi ----
        case NK::HmiObjectOperators:
        case NK::HmiObjectOperator:
        case NK::HmiObjectFunctions:       // 1.11.10 : l'instance choisie - son inspecteur dit
        case NK::HmiObjectFunction:        //   ses fonctions (redefinir) et ses popups
        case NK::HmiObjectPopups:
            openHmiView(treeModel_->hmiViewOf(n), -1, treeModel_->hmiObjectOf(n));
            break;
        // ---- 1.11.10 : une fonction d'un symbole : son editeur, le sous-onglet Fonctions, elle ----
        case NK::HmiSymbolFunction: {
            const auto viewId = treeModel_->hmiViewOf(n);
            openHmiView(viewId);
            auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
            const auto* sym = app_.hmi()->project.view(asId(viewId));
            const int k = treeModel_->hmiRankOf(n);
            if (editor && editor->symbolTabs() && editor->symbolFunctions() && sym && k >= 0
                && static_cast<std::size_t>(k) < sym->functions.size()) {
                editor->symbolTabs()->setCurrent(HmiSymbolTabs::Functions);
                editor->symbolFunctions()->selectFunction(sym->functions[static_cast<std::size_t>(k)].id);
            }
            break;
        }
        // ---- 1.10 (chantier O) : le noeud Alarmes d'un objet, une alarme ----
        // L'objet choisi dans sa vue ; une alarme : la section Alarmes de l'objet
        // s'ouvre sur elle (comme un clic dans l'explorateur d'objets).
        case NK::HmiObjectAlarms:
        case NK::HmiObjectAlarm: {
            const auto viewId = treeModel_->hmiViewOf(n);
            const auto objectId = treeModel_->hmiObjectOf(n);
            openHmiView(viewId, -1, objectId);
            auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
            std::string name, path;
            if (editor && treeModel_->hmiObjectAlarmOf(n, name, path)) {
                editor->inspector().setCurrentIndex(0);
                editor->objects().alarmActivated->emit(asId(objectId), name, path);
            }
            break;
        }
        case NK::HmiViewScript: {
            const auto viewId = treeModel_->hmiViewOf(n);
            const auto* view = app_.hmi()->project.view(asId(viewId));
            const auto k = static_cast<std::size_t>(treeModel_->hmiRankOf(n));
            openHmiScripts(viewId, view && k < view->scripts.size() ? view->scripts[k].id : 0);
            break;
        }
        case NK::HmiLayer:
            openHmiView(treeModel_->hmiViewOf(n), static_cast<int>(ProjectTreeModel::HmiPart::Layers));
            break;
        case NK::HmiAlarmGroup:
        case NK::HmiAlarm: {
            openHmiPane("alarmes");
            auto* pane = dynamic_cast<HmiAlarmsPane*>(hmiTab("alarmes"));
            if (!pane) break;
            if (ProjectTreeModel::kindOf(n) == NK::HmiAlarm) {
                pane->setSearch({});
                pane->selectAlarm(asId(treeModel_->hmiIdOf(n)));
            } else {
                // Un groupe : la liste filtree sur lui (la recherche porte aussi sur le groupe).
                const auto groups = hmitree::alarmGroups(app_.hmi()->project);
                const auto k = static_cast<std::size_t>(treeModel_->hmiRankOf(n));
                pane->setSearch(k < groups.size() ? groups[k] : std::string{});
            }
            break;
        }
        case NK::HmiRecipe:
        case NK::HmiRecord: {
            openHmiPane("recettes");
            if (auto* pane = dynamic_cast<HmiRecipesPane*>(hmiTab("recettes"))) {
                pane->selectRecipe(asId(treeModel_->hmiIdOf(n)));
                if (const auto rec = treeModel_->hmiRecordOf(n)) pane->selectRecord(asId(rec));
            }
            break;
        }
        case NK::HmiUserGroup:
        case NK::HmiUser:
        case NK::HmiRoles:
        case NK::HmiRole: {
            openHmiPane("utilisateurs");
            auto* pane = dynamic_cast<HmiUsersPane*>(hmiTab("utilisateurs"));
            if (!pane) break;
            const auto kind = ProjectTreeModel::kindOf(n);
            if (kind == NK::HmiUser) { pane->showTab(HmiUsersPane::Users); pane->selectUser(asId(treeModel_->hmiIdOf(n))); }
            else if (kind == NK::HmiUserGroup) { pane->showTab(HmiUsersPane::Groups); pane->selectGroup(asId(treeModel_->hmiIdOf(n))); }
            else {
                pane->showTab(HmiUsersPane::Roles);
                const auto& roles = app_.hmi()->project.security.roles;
                const auto k = static_cast<std::size_t>(treeModel_->hmiRankOf(n));
                if (kind == NK::HmiRole && k < roles.size()) pane->selectRole(roles[k].name);
            }
            break;
        }
        case NK::HmiScriptsFolder:
            openHmiPane("scripts");
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) pane->showTab(HmiScriptsPane::TabScripts);
            break;
        case NK::HmiVariablesFolder:
            // Lot 16 : l'onglet Variables IHM (dossiers, structures, tableaux, liaison).
            openHmiPane("scripts");
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) pane->showTab(HmiScriptsPane::TabVariables);
            break;
        case NK::HmiGeneralScript:
            openHmiScripts(0, treeModel_->hmiIdOf(n));
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) pane->showTab(HmiScriptsPane::TabScripts);
            break;
        case NK::HmiVariable:
            openHmiPane("scripts");
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
                pane->selectVariable(asId(treeModel_->hmiIdOf(n)));
                pane->showTab(HmiScriptsPane::TabVariables);
                if (auto* vars = pane->variablesPane()) vars->selectVariable(asId(treeModel_->hmiIdOf(n)));
            }
            break;
        case NK::HmiVarFolder:
            openHmiPane("scripts");
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
                pane->showTab(HmiScriptsPane::TabVariables);
                if (auto* vars = pane->variablesPane()) vars->selectFolder(treeModel_->hmiFolderOf(n));
            }
            break;
        case NK::HmiTypesFolder:
        case NK::HmiTypeNode:
        case NK::HmiTypeValues: case NK::HmiTypeValue:            // 1.10 (chantier O) : le type choisi
        case NK::HmiTypeOperators: case NK::HmiTypeOperator:
            openHmiPane("scripts");
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
                pane->showTab(HmiScriptsPane::TabTypes);
                if (ProjectTreeModel::kindOf(n) != NK::HmiTypesFolder)
                    if (auto* types = pane->typesPane()) {
                        types->selectType(asId(treeModel_->hmiIdOf(n)));
                        // 1.10 (chantier U) : un clic sur << Arret = 0 >> choisit la valeur dans la grille.
                        if (ProjectTreeModel::kindOf(n) == NK::HmiTypeValue && types->enumValues())
                            types->enumValues()->selectValue(static_cast<int>(ProjectTreeModel::subOf(n)));
                    }
            }
            break;
        case NK::HmiUsedFolder:
            // R1111-5 (decision 104, reponse 2) : « Variables employees » s'est
            // fondue dans l'arbre de la Configuration : son filtre « Employees ».
            openHmiPane("config");
            if (auto* config = dynamic_cast<HmiConfigPane*>(hmiTab("config")))
                config->apiVars().setFilter(HmiApiVarsView::Filter::Used);
            break;
        case NK::HmiFunctionsFolder:
            openHmiPane("fonctions");
            break;
        case NK::HmiFunction:
            openHmiFunctions(treeModel_->hmiIdOf(n));
            break;
        case NK::HmiUsedVariable: {
            // Une variable IHM : la ou elle se declare ; une variable de
            // l'automate : la table des variables du programme (et leurs emplois).
            const auto path = treeModel_->hmiUsedPathOf(n);
            std::size_t end = 0;
            while (end < path.size() && path[end] != '.' && path[end] != '[') ++end;
            if (const auto* v = app_.hmi()->project.variable(path.substr(0, end))) {
                openHmiPane("scripts");
                if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) pane->selectVariable(v->id);
            } else {
                openHmiPane("config");
                // R1111-5 : le filtre « Employees », la variable choisie dans l'arbre.
                if (auto* config = dynamic_cast<HmiConfigPane*>(hmiTab("config"))) {
                    config->ensureBuilt();          // un onglet tout juste ouvert : l'arbre rempli avant d'y choisir
                    auto& tree = config->apiVars();
                    tree.setFilter(HmiApiVarsView::Filter::Used);
                    (void)tree.selectName(path.rfind("API.", 0) == 0 ? path : "API." + path);
                }
            }
            break;
        }
        // ---- lot 9 : les variables systeme et d'instances, dans leur volet ------------
        case NK::HmiSysFolder: case NK::HmiSysDomain: case NK::HmiSysVar:
        case NK::HmiInstFolder: case NK::HmiInstView: case NK::HmiInstViewVar: case NK::HmiInstObject: case NK::HmiInstVar:
        case NK::HmiInstViewInfo:
        case NK::HmiInstParam: case NK::HmiInstAlarmGroup: case NK::HmiInstGroupVar:          // 1.11.1 (decision 108)
        case NK::HmiInstAlarms: case NK::HmiInstAlarm: case NK::HmiInstAlarmVar: {
            openHmiPane("variables-publiques");
            auto* pane = dynamic_cast<HmiPublicVarsPane*>(hmiTab("variables-publiques"));
            if (!pane) break;
            const auto kind = ProjectTreeModel::kindOf(n);
            const auto& project = app_.hmi()->project;
            const auto view = [&]() -> const hmi::View* {
                for (const auto& v : project.views)
                    if ((v.id & ((1ull << 28) - 1)) == ProjectTreeModel::indexOf(n)) return &v;
                return nullptr;
            };
            const auto objectOf = [&](const hmi::View* v, std::uint64_t id, std::uint64_t mask) -> const hmi::Object* {
                if (v) for (const auto& o : v->objects) if ((o.id & mask) == id) return &o;
                return nullptr;
            };
            if (kind == NK::HmiSysFolder || kind == NK::HmiSysDomain || kind == NK::HmiSysVar) {
                pane->showTab(HmiPublicVarsPane::System);
                const auto i = ProjectTreeModel::indexOf(n);
                if (kind == NK::HmiSysDomain && i < hmi::pub::kSysDomainCount) pane->setSearch(std::string(hmi::pub::kSysDomains[i]));
                else if (kind == NK::HmiSysVar && i < hmi::pub::kSysVarCount) {
                    pane->setSearch(std::string(hmi::pub::kSysDomains[hmi::pub::kSysVars[i].domain]));
                    (void)pane->selectPath("SYS." + std::string(hmi::pub::kSysVars[i].name));
                } else pane->setSearch({});
                break;
            }
            pane->showTab(HmiPublicVarsPane::Instances);
            // 1.11.1 (decision 108) : un parametre, le groupe d'alarmes, une alarme et ses
            // variables - le volet filtre sur ce qui est dessous et choisit la variable.
            if (kind == NK::HmiInstParam || kind == NK::HmiInstAlarmGroup || kind == NK::HmiInstGroupVar
                || kind == NK::HmiInstAlarms || kind == NK::HmiInstAlarm || kind == NK::HmiInstAlarmVar) {
                pane->setSearch(treeModel_ ? treeModel_->hmiInstPrefixOf(n) : std::string{});
                if (const auto path = treeModel_ ? treeModel_->hmiInstPathOf(n) : std::string{}; !path.empty()) (void)pane->selectPath(path);
                break;
            }
            const auto* v = kind == NK::HmiInstFolder ? nullptr : view();
            if (!v) { pane->setSearch({}); break; }
            if (kind == NK::HmiInstView || kind == NK::HmiInstViewInfo) { pane->setSearch(v->name + "."); break; }
            if (kind == NK::HmiInstViewVar) {
                const auto k = ProjectTreeModel::subOf(n);
                pane->setSearch(v->name + ".");
                if (k < std::size(hmi::pub::kViewInfo)) (void)pane->selectPath(v->name + "." + std::string(hmi::pub::kViewInfo[k].name));
                break;
            }
            const auto sub = ProjectTreeModel::subOf(n);
            const auto* o = kind == NK::HmiInstObject ? objectOf(v, sub, (1ull << 28) - 1) : objectOf(v, sub >> 8, (1ull << 20) - 1);
            if (!o) { pane->setSearch(v->name + "."); break; }
            pane->setSearch(v->name + "." + o->name + ".");
            if (kind == NK::HmiInstVar) {
                const auto members = hmi::pub::objectMembers(*o);
                if ((sub & 0xFF) < members.size()) (void)pane->selectPath(hmi::pub::instancePath(*v, *o, members[sub & 0xFF].name));
            }
            break;
        }
        default: break;
    }
    // Lot 7 : la premiere fois qu'on ouvre l'IHM, la visite guidee (jamais
    // dans une session rejouee : elle couvrirait l'ecran que le script clique).
    if (hmiTutorial_ && !hmiTutorial_->active() && !app_.scripted()
        && !app_.settings().getBool("hmi.tutorial.seen", false))
        startHmiTutorial();
}

void MainAnalysisScreen::openHmiModbusTool(const std::string& host, int port, int unit, const std::string& tab) {
    openHmiPane("outil");
    if (auto* tool = dynamic_cast<HmiModbusToolPane*>(hmiTab("outil"))) {
        if (!host.empty()) tool->setTarget(host, port, unit);
        tool->showTab(tab);
    }
}

void MainAnalysisScreen::askHmiImportTwin(const std::string& equipment) {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier CSV", "", "le chemin d'un fichier table;adresse;valeur (Exporter (CSV) en \xC3\xA9" "crit un)", false, {}});
    // Le bouton ... : l'explorateur, dans exports/ (ou Exporter (CSV) l'ecrit).
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiImportTwin", "Importer une m\xC3\xA9moire dans l'esclave simul\xC3\xA9",
            "Chaque ligne \xC2\xAB table;adresse;valeur \xC2\xBB (4x;40001;1234, 0x;00017;1) \xC3\xA9" "crit sa case dans la m\xC3\xA9moire de l'esclave simul\xC3\xA9 de \xC2\xAB " + equipment + " \xC2\xBB.",
            std::move(fields), "Importer"), 0, ui::openFile("Fichiers CSV|*.csv;*.txt", ui::pathIn(app_.projectFolder(), "exports"))),
        [this, equipment](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiCommPane*>(hmiTab("communication"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || v[0].empty()) return;
            const std::u8string u8(v[0].begin(), v[0].end());
            std::ifstream in(std::filesystem::path(u8), std::ios::binary);
            if (!in) {
                status_->setTransientMessage("Importer : impossible d'ouvrir " + v[0], 8.0);
                return;
            }
            std::stringstream text;
            text << in.rdbuf();
            std::string why;
            if (!pane->importTwinMemory(equipment, text.str(), &why)) status_->setTransientMessage("Importer : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiBind(const std::string& equipment, const std::string& address, const std::string& type) {
    auto doc = app_.hmi();
    if (!doc) return;
    std::vector<std::string> equipments;
    for (const auto& e : doc->project.equipments)
        if (e.modbus()) equipments.push_back(e.name);
    if (equipments.empty()) {
        status_->setTransientMessage("Lier une variable : ajoutez d'abord un \xC3\xA9quipement Modbus TCP/IP.", 8.0);
        return;
    }
    std::string chosen = equipment;
    if (std::find(equipments.begin(), equipments.end(), chosen) == equipments.end()) chosen = equipments.front();
    const auto types = hmi::typereg::baseRegistry().names(hmi::typereg::UseVariable);   // 1.11.19 : le registre des types
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Variable", hmi::uniqueVariableName(doc->project, "Mesure"), "un nom (Tension_L1) ; une variable IHM qui existe : elle est li\xC3\xA9" "e", false, {}});
    fields.push_back({"Type", type.empty() ? std::string("INT") : type, "", false, types});
    fields.push_back({"\xC3\x89quipement", chosen, "", false, equipments});
    fields.push_back({"Adresse", address, "vide : la prochaine libre ; 40101, 30001, %MW100, %MF20...", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiBind", "Lier une variable \xC3\xA0 un \xC3\xA9quipement",
            "La variable IHM se lit (et s'\xC3\xA9" "crit) dans l'\xC3\xA9quipement, \xC3\xA0 son adresse : Modicon (40101, \xC3\xA0 partir de 1) ou "
            "Schneider (%MW100, \xC3\xA0 partir de 0). La mise \xC3\xA0 l'\xC3\xA9" "chelle se r\xC3\xA8gle ensuite \xC3\xA0 droite. Ctrl+Z reprend.",
            std::move(fields), "Lier"),
        [this](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiCommPane*>(hmiTab("communication"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 3) return;
            std::string why;
            if (!pane->bindVariable(v[0], v[2], v.size() > 3 ? v[3] : std::string{}, v[1], &why))
                status_->setTransientMessage("Lier : " + why, 8.0);
        });
}

void MainAnalysisScreen::openHmiPane(const std::string& key) {
    // 1.11.14 : les sorties du build vivent dans le panneau du bas.
    if (key == "sorties") {
        showBottomPanel(true, HmiBuildOutputPane::kSorties);
        return;
    }
    auto doc = app_.hmi();
    if (!doc || !centre_) return;
    setHmiProjectFolder(app_.projectFolder());     // "Enregistrer sous" a pu le changer
    if (auto* page = hmiTab(key)) {
        (void)showPage(page);      // lot 7 : en onglet, ou sa fenetre detachee devant
        // Generer et Compiler repartent a chaque ouverture : un rapport d'il y
        // a dix minutes ne dit rien de la vue telle qu'elle est. L'etat des
        // fichiers externes non plus : un fichier a pu changer sur le disque.
        if (auto* report = dynamic_cast<HmiReportPane*>(page)) report->run();
        if (auto* files = dynamic_cast<HmiFilesPane*>(page)) files->refresh();
        if (auto* history = dynamic_cast<HmiHistoryPane*>(page)) history->refresh();
        if (auto* exchange = dynamic_cast<HmiExchangePane*>(page)) {
            exchange->setNameExists(plcNames(app_.project()));
            exchange->setProjectFolder(app_.projectFolder());
        }
        return;
    }

    auto apply = [this](core::CommandPtr c) { if (c) app_.apply(std::move(c), /*refreshViews=*/false); };
    ui::WidgetPtr page;
    std::string title;
    Icon icon = Icon::Screen;

    if (key == "config") {
        const std::string folder = app_.projectFolder();
        auto pane = std::make_unique<HmiConfigPane>(
            "hmi.config", doc, apply, app_.project(), app_.report().get(),
            [folder]() -> std::uint64_t {
                return folder.empty() ? 0u : folderBytes(std::filesystem::path(folder) / "ihm");
            });
        // 1.11.1 (API-V) : un emploi d'une variable de l'automate choisi dans l'arbre - y aller.
        hmiLinks_ += pane->useActivated->connect([this](const hmi::Issue& i) { openHmiIssue(i); });
        page = std::move(pane);
        title = "IHM \xC2\xB7 Configuration";
        icon = Icon::Settings;
    } else if (key == "vues") {
        auto pane = std::make_unique<HmiViewsPane>("hmi.views", doc, apply);
        hmiLinks_ += pane->openView->connect([this](hmi::Id v) { openHmiView(v); });
        pane->setHosts([this](const std::string& role) { askNewHmiView(role); }, [this](hmi::Id v) { askDeleteHmiView(v); });
        pane->setDuplicateReplaceHost([this](hmi::Id v) { askHmiDuplicateReplace(v); });   // lot 12
        pane->setFromTypeHost([this] { askHmiViewFromType(); });
        // Lot 20 : les modeles et les paquets de vues.
        HmiViewsPane::PackageHosts ph;
        ph.saveTemplate = [this](hmi::Id v) { askHmiSaveTemplate(v); };
        ph.exportViews = [this](hmi::Id v) { askHmiExportViews(v); };
        ph.importViews = [this] { askHmiImportViews(); };
        ph.exportSymbols = [this](hmi::Id v) { askHmiExportSymbols(v); };      // 1.11.2 (decision 162)
        ph.importSymbols = [this] { askHmiImportSymbols(); };
        pane->setPackageHosts(std::move(ph));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Vues";
        icon = Icon::Folder;
    } else if (key == "generer" || key == "compiler") {
        const bool generate = key == "generer";
        auto pane = std::make_unique<HmiReportPane>(
            generate ? "hmi.generate" : "hmi.compile", doc,
            generate ? HmiReportPane::Mode::Generate : HmiReportPane::Mode::Compile, plcNames(app_.project()));
        hmiLinks_ += pane->issueActivated->connect([this](const hmi::Issue& i) { openHmiIssue(i); });
        hmiLinks_ += pane->migrateRequested->connect([this] { askHmiMigrateDeclarations(); });   // 1.11.18 (lot 5)
        // 1.10.4 : "Remplacer..." (l'avertissement d'un repere) - la vue s'ouvre, l'objet est
        // choisi, et Dupliquer s'ouvre avec 0 copie : on remplit le repere dans l'original.
        hmiLinks_ += pane->replaceMarkers->connect([this](hmi::Id v, hmi::Id o) {
            auto h = app_.hmi();
            const auto* view = h ? h->project.view(v) : nullptr;
            if (!view || !view->object(o)) return;
            openHmiView(v);
            auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(v)));
            if (!editor) return;
            editor->canvas().setSelection({o});
            nextDuplicateCopies() = 0;
            askHmiDuplicate(v);
        });
        pane->setPlcPaths(hmiPlcPaths(app_.project().get()));   // ---- Lot API 8 : les expressions impossibles ----
        // Lot 14 : le plan d'adressage Modbus (Configuration > Communication).
        pane->setCommPlan([this] {
            const auto h = app_.hmi();
            return CommHost::planFor(h ? h->project.comm : hmi::Communication{}, app_.project().get(), app_.simulationRuntime());
        });
        pane->setPlcScalar([this](std::string_view path) {
            auto* rt = app_.simulationRuntime();
            ::sim::Value v;
            return !rt || (rt->get(path, v) && v.type() != ::sim::Type::Unknown);
        });
        // ---- Lot API 8 : l'arbre du projet (la pastille rouge : les expressions impossibles du dernier rapport) ----
        //  Une expression impossible : une erreur de categorie Expression, ou un nom qui
        //  n'existe pas (le texte a trous, la cible d'une action, la condition d'une
        //  alarme : ce sont aussi des expressions) - le meme compte partout (l'arbre,
        //  la cloche, le pied de l'arbre).
        hmiLinks_ += pane->ran->connect([this, report = pane.get()] {
            std::size_t n = 0, inViews = 0;
            for (const auto& i : report->issues())
                if (impossibleExpression(i)) {
                    ++n;
                    if (i.view != hmi::kNoId) ++inViews;
                }
            setTreeExprErrors(n, inViews);
        });
        // ---- fin Lot API 8 : l'arbre du projet ----
        // ---- Lot API 8 : bandeau haut (la cloche : les expressions impossibles du dernier Compiler) ----
        hmiLinks_ += pane->ran->connect([report = pane.get()] {
            std::size_t n = 0;
            for (const auto& i : report->issues())
                if (impossibleExpression(i)) ++n;
            if (n == 0) { bgtasks::withdraw("compiler:expressions"); return; }
            bgtasks::post({"compiler:expressions", "Projet",
                           std::to_string(n) + (n > 1 ? " expressions impossibles (Compiler)" : " expression impossible (Compiler)"),
                           "Des propri\xC3\xA9t\xC3\xA9s de l'IHM qui ne peuvent pas marcher", "Voir la liste", "cmd:compiler", "error"});
        });
        // ---- fin Lot API 8 : bandeau haut ----
        pane->run();
        page = std::move(pane);
        title = generate ? "IHM \xC2\xB7 G\xC3\xA9n\xC3\xA9rer" : "IHM \xC2\xB7 Compiler";
        icon = generate ? Icon::Analyze : Icon::Code;
    } else if (key == "simulation") {
        HmiSimulationHost host;
        host.runtime = [this] { return app_.simulationRuntime(); };
        host.transport = [this](std::string_view id) { runSimulationTransport(id); };
        host.state = [this] {
            const auto& sim = app_.simulation();
            return sim.attached() ? sim.statusLine() : std::string("simulateur non lanc\xC3\xA9 (Marche)");
        };
        host.force = [this](std::string variable) { askHmiForce(std::move(variable)); };
        host.unforceAll = [this] {
            if (auto* rt = app_.simulationRuntime()) rt->unforceAll();
        };
        host.login = [this](std::string login) { askHmiLogin(std::move(login)); };
        // 1.11.13 : demarrer passe par le build de l'IHM (un projet a jour demarre aussitot).
        host.buildGate = [this](const std::string& source) { startHmiBuild(source); };
        // 1.11.14 : la Console (chaque ligne du moteur) et les Sorties (demarree, arretee).
        host.console = [this](const hmi::JournalEntry& e) {
            if (bottomPanel_) bottomPanel_->console().addRuntime(e);
        };
        host.lifecycle = [this](bool started, int session) {
            if (bottomPanel_) bottomPanel_->simulationEvent(started, session);
            hmiSimulationLifecycle(started);               // 1.11.15 : l'arret sur modification
        };
        // 1.11.15 : la remanence de simulation (l'option, l'instantane dans .xpg/simulation,
        // hors des versions du projet) et la suite d'un arret (la question, les builds).
        host.keepData = [this] { return app_.settings().getBool("simulation.keepData", false); };
        host.setKeepData = [this](bool on) { app_.settings().set("simulation.keepData", on); };
        host.dataFile = [this] {
            const std::string folder = app_.projectFolder();
            return folder.empty() ? std::string{} : (std::filesystem::path(folder) / ".xpg" / "simulation" / "remanence.txt").string();
        };
        host.outputs = [this](int severity, const std::string& text) {
            if (!bottomPanel_) return;
            bottomPanel_->say(severity >= 3 ? hmi::pipeline::Severity::Error : severity == 2 ? hmi::pipeline::Severity::Warning
                              : severity == 1 ? hmi::pipeline::Severity::Success : hmi::pipeline::Severity::Information,
                              "Simulation", text);
        };
        host.confirmRestart = [this](std::function<void(bool)> answer) { askHmiRestart(std::move(answer)); };
        host.rebuild = [this](const std::string& mode) { rebuildHmiFor(mode); };
        // 1.10 (decision 12) : le plein ecran de l'IHM - la fenetre en plein ecran, puis
        // rendue comme avant (deja en plein ecran par F11 : elle le reste).
        host.fullScreenWindow = [this, before = std::make_shared<bool>(false)](bool on) {
#if XPG_HMI_FULL_SDL
            // L'onglet detache dans une fenetre a lui (il garde sa place dans hmiTabs_) :
            // c'est cette fenetre qui passe en plein ecran, pas la principale.
            const auto tab = hmiTabs_.find("simulation");
            if (const std::uint32_t id = tab != hmiTabs_.end() ? app_.detachedWindows().windowIdOf(tab->second) : 0; id != 0) {
                if (SDL_Window* w = SDL_GetWindowFromID(id)) {
                    if (on) *before = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
                    (void)SDL_SetWindowFullscreen(w, on || *before);
                }
                return;
            }
            if (on) {
                SDL_Window* w = SDL_GetKeyboardFocus();
                *before = w && (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
            }
#endif
            app_.setStationWindow(on || *before, false);
        };
        host.recipe = [this](const hmi::RecipeRequest& rq) { askHmiRecipeRecord(rq); };
        host.users = [this](const hmi::UserRequest& rq) { askHmiUserRecord(rq); };
        host.resource = [this](const hmi::ResourceRequest& rq) { askHmiRuntimeResource(rq); };
        // Lot 11 : un export va dans exports/ du dossier du projet.
        host.exportFile = [this](const hmi::ExportRequest& rq, std::string* where) { return writeHmiExport(rq, where); };
        // ---- Lot API 8 : les exports qui demandent ou ----
        // Lance par un geste de l'operateur (le bouton d'export, l'action Exporter,
        // IHM_EXPORTER) : le dialogue du lot 7, puis l'ecriture a l'endroit choisi.
        host.askExport = [this](const hmi::ExportRequest& rq, std::function<void(bool, const std::string&)> done) {
            return askHmiRuntimeExport(rq, std::move(done));
        };
        // ---- fin Lot API 8 ----
        // Lot 13 : chaque ligne d'audit va tout de suite dans ihm/historique/audit.csv
        // (un projet pas encore enregistre : a son premier enregistrement).
        host.audit = [this](const hmi::AuditEntry& e) {
            const std::string folder = app_.projectFolder();
            if (!folder.empty() && hmi::exists(folder)) (void)hmi::appendAuditFile(e, folder);
        };
        host.plc = [this] {
            hmi::PlcStatus st;
            const auto& sim = app_.simulation();
            st.attached = sim.attached();
            st.running = sim.state() == SimulationHost::State::Running;
            st.paused = sim.state() == SimulationHost::State::Paused;
            st.halted = sim.state() == SimulationHost::State::Halted;
            st.scans = sim.scanCount();
            st.cycleMs = static_cast<int>(sim.scanIntervalMs());
            st.error = sim.haltMessage();
            if (const auto* rt = sim.runtime()) st.forced = static_cast<int>(rt->forcedNames().size());
            if (const auto plcProject = app_.project()) st.project = plcProject->header.projectName;
            return st;
        };
        // Lot 14 : relie a un automate reel (Configuration > Communication : Modbus
        // TCP), l'IHM lit la liaison ; ses evenements vont au journal.
        host.link = [this]() -> ::sim::Environment* { return app_.comm().link(); };
        host.commEvents = [this] {
            auto events = app_.comm().takeEvents();
            for (auto& e : app_.equipments().takeEvents()) events.push_back(std::move(e));   // lot 15
            return events;
        };
        // Lot 15 : les variables IHM liees a un equipement, l'etat des equipements.
        host.equipmentLink = [this](const std::string& name) { return app_.equipments().link(name); };
        host.equipmentStatus = [this] { return app_.equipments().statuses(); };
        // 1.9 : les esclaves simules (la page Simulation de Parametres systeme).
        host.simSlaves = [this](bool withValues) {
            const auto d = app_.hmi();
            return d ? app_.equipments().simSlaves(d->project, withValues) : std::vector<hmi::SimSlave>{};
        };
        host.simSlaveCommand = [this](const hmi::SimSlaveCommand& c, std::string* why) {
            const auto d = app_.hmi();
            return d && app_.equipments().simSlaveCommand(d->project, c, why);
        };
        host.commDemo = [this] { return std::make_pair(app_.comm().running(), app_.comm().demoPort()); };
        // Lot 14 : les notifications des alarmes, et les rapports a envoyer.
        host.alarmNotice = [this](const hmi::AlarmNotice& n) { app_.notify().notice(n); };
        host.notifyStats = [this] {
            auto st = app_.notify().stats();
            st.webClients = app_.web().clientCount();
            return st;
        };
        host.notifyEvents = [this] {
            auto events = app_.notify().takeEvents();
            for (auto& e : app_.web().takeEvents()) events.push_back(std::move(e));
            return events;
        };
        host.reportWritten = [this](const hmi::ReportOutput& r) { app_.notify().report(r); };
        // Lot 18 : l'onglet Jumeaux (les esclaves simules, les commandes du projet).
        host.equipments = [this] { return &app_.equipments(); };
        host.apply = [this](core::CommandPtr c) { applyFromSimulation(std::move(c)); };   // 1.11.15 : n'arrete pas la simulation
        page = std::make_unique<HmiSimulationPane>("hmi.simulation", doc, std::move(host));
        title = "Simulation \xC2\xB7 IHM";   // Lot API 8 : Centre de simulation (etait "IHM . Simulation" ; les scripts gardent l'ancien titre)
        icon = Icon::Play;
    } else if (key == "scripts") {
        auto pane = std::make_unique<HmiScriptsPane>("hmi.scripts", doc, apply);
        HmiScriptsPane::Hosts hosts;
        hosts.newScript = [this](hmi::ScriptLang lang) { askHmiNewScript(lang); };
        hosts.rename = [this](hmi::Id sc) { askHmiRenameScript(sc); };
        hosts.remove = [this](hmi::Id sc) { askHmiDeleteScript(sc); };
        hosts.newVariable = [this] { askHmiVariable(0); };
        hosts.editVariable = [this](hmi::Id v) { askHmiVariable(v); };
        hosts.removeVariable = [this](hmi::Id v) { askHmiDeleteVariable(v); };
        hosts.compile = [this] { openHmiPane("compiler"); };
        hosts.openIssue = [this](const hmi::Issue& i) { openHmiIssue(i); };   // 1.10 (chantier N) : un resultat de Compiler d'ailleurs
        hosts.exportItems = [this](hmi::Id sc) { askHmiExportPrograms(2, sc); };   // 1.11.2 (decision 174)
        hosts.importAny = [this] { askHmiImport(); };
        wireHmiBuildHosts(hosts);   // 1.11.13 : Generer, Regenerer, Compiler, Generer et compiler
        pane->setHosts(std::move(hosts));
        // Lot 16 : les variables IHM (dossiers, structures, tableaux, liaison) et les types IHM.
        if (auto* vars = pane->variablesPane()) {
            HmiVariablesPane::Hosts vh;
            vh.removeVariable = [this](hmi::Id v) { askHmiDeleteVariable(v); };
            vh.showEquipment = [this](const std::string& name) {
                openHmiPane("communication");
                if (auto* comm = dynamic_cast<HmiCommPane*>(hmiTab("communication"))) {
                    comm->selectBound(name);   // 1.11.4 : le Plan d'adressage, sur sa ligne
                }
            };
            vh.arrayType = [this](const std::string& current, std::function<void(const std::string&)> done) { askHmiArrayType(current, std::move(done)); };
            vh.newType = [this] {
                if (auto* p2 = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
                    p2->showTab(HmiScriptsPane::TabTypes);
                    if (auto* types = p2->typesPane()) types->tools().triggered->emit(1);
                }
            };
            vh.link = [this](const std::string& equipment) { return app_.equipments().link(equipment); };
            // ---- 1.11.16 : la remanence d'exploitation - le stockage du poste ----
            vh.retainFile = [this] {
                const std::string folder = app_.projectFolder();
                return folder.empty() ? std::string{} : hmi::retain::fileOf(folder).string();
            };
            vh.stationRunning = [this] { return app_.stationActive(); };
            // La valeur actuelle : la simulation de l'editeur (le poste ne tourne pas sous l'editeur).
            vh.currentValue = [this](const std::string& name) -> std::string {
                auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
                if (!sim || !sim->runtime().running()) return {};
                const auto* v = sim->runtime().variable(name);
                if (!v) return "(une structure : voir ses membres)";
                return v->type() == ::sim::Type::String ? "'" + v->asString() + "'" : v->display();
            };
            vh.confirm = [this](const std::string& head, const std::string& text, const std::string& button, std::function<void()> yes) {
                app_.menus().ShowDialog(std::make_unique<MessageDialog>(head, text, MessageDialog::Icon::Question, button),
                                        [yes = std::move(yes)](const menu::DialogResult& r) {
                                            if (r.accepted() && yes) yes();
                                        });
            };
            vh.askPath = [this](const std::string& head, const std::string& text, const std::string& initial, ui::PathBrowse browse,
                                std::function<void(const std::string&)> done) { askApiText(head, text, initial, std::move(done), std::move(browse)); };
            vh.report = [this](const std::string& head, const std::string& text, bool warning) {
                app_.menus().ShowDialog(std::make_unique<MessageDialog>(head, text, warning ? MessageDialog::Icon::Warning : MessageDialog::Icon::Info),
                                        [](const menu::DialogResult&) {});
            };
            // ---- 1.10 (chantier O) : la fenetre graphique temporaire des variables IHM ----
            // Les valeurs : la simulation de l'IHM (son environnement lit aussi
            // l'automate). La fenetre : une fenetre a elle (DetachedWindows) ; la
            // fermer la detruit - rien n'est enregistre dans le projet.
            vh.trendSource.running = [this] {
                auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
                return sim && sim->runtime().running();
            };
            vh.trendSource.read = [this](const std::string& path, double& value, bool& isBool) {
                auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
                if (!sim || !sim->runtime().running()) return false;
                ::sim::Value v;
                if (!sim->runtime().environment().read(path, v)) return false;
                isBool = v.type() == ::sim::Type::Bool;
                if (!isBool && !::sim::isNumeric(v.type())) return false;
                value = isBool ? (v.isTruthy() ? 1.0 : 0.0) : v.asReal();
                return true;
            };
            vh.showTrend = [this](std::unique_ptr<HmiQuickTrend> trend) -> HmiQuickTrend* {
                HmiQuickTrend* raw = trend.get();
                hmiLinks_ += raw->exportRequested->connect([this, raw](int kind) {
                    if (kind != static_cast<int>(HmiQuickTrend::Export::Csv)) {
                        // L'image : la fenetre du graphique redessinee et relue
                        // (DetachedWindows::capturePng), par le dialogue des exports.
                        auto write = [this, raw] {
                            if (!app_.detachedWindows().windowIdOf(raw)) {
                                if (status_) status_->setTransientMessage("La fen\xC3\xAAtre du graphique est ferm\xC3\xA9" "e.", 5.0, ui::StatusBar::Severity::Warning);
                                return;
                            }
                            std::error_code ec;
                            const auto tmp = std::filesystem::temp_directory_path(ec) / "xpg_graphique.png";
                            std::string error;
                            if (ec || !app_.detachedWindows().capturePng(raw, tmp.string(), &error)) {
                                if (status_) status_->setTransientMessage("Image impossible : " + (error.empty() ? ec.message() : error), 6.0, ui::StatusBar::Severity::Warning);
                                return;
                            }
                            std::ifstream in(tmp, std::ios::binary);
                            auto bytes = std::make_shared<hmi::Bytes>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                            in.close();
                            std::filesystem::remove(tmp, ec);
                            hmi::ExportRequest rq;
                            rq.fileName = "graphique.png";
                            rq.format = "PNG";
                            rq.source = "graphique";
                            rq.data = std::move(bytes);
                            std::string where;
                            const bool ok = writeHmiExport(rq, &where);
                            if (status_) status_->setTransientMessage(ok ? "Image export\xC3\xA9" "e : " + where : "Export impossible : " + where, 6.0,
                                                                      ok ? ui::StatusBar::Severity::Info : ui::StatusBar::Severity::Warning);
                        };
                        if (!askExportTarget("l'image du graphique (PNG)", "png", write)) write();
                        return;
                    }
                    const std::string text = raw->csv();
                    auto write = [this, text] {
                        hmi::ExportRequest rq;
                        rq.fileName = "graphique.csv";
                        rq.format = "CSV";
                        rq.source = "graphique";
                        rq.data = std::make_shared<const hmi::Bytes>(text.begin(), text.end());
                        std::string where;
                        const bool ok = writeHmiExport(rq, &where);
                        if (status_) status_->setTransientMessage(ok ? "Valeurs export\xC3\xA9" "es : " + where : "Export impossible : " + where, 6.0,
                                                                  ok ? ui::StatusBar::Severity::Info : ui::StatusBar::Severity::Warning);
                    };
                    if (!askExportTarget("les valeurs du graphique (CSV)", "csv", write)) write();
                });
                // Le titre de la fenetre suit les courbes ("Graphique : A, B, C").
                hmiLinks_ += raw->changed->connect([this, raw] { app_.detachedWindows().setTitle(raw, raw->title()); });
                // Rien ne tourne : son bouton "Demarrer la simulation de l'IHM" ouvre
                // l'onglet Simulation - IHM, qui la demarre (1.9). Integration 1.10 : avec
                // le chantier L, le geste "Demarrer l'IHM" de L (l'onglet ne relance pas
                // une IHM que l'utilisateur a arretee ; l'API ne demarre pas).
                hmiLinks_ += raw->startRequested->connect([this] {
                    openHmiPane("simulation");
                    if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); sim && !sim->runtime().running())
                        sim->command("hmi.start");
                });
                ui::WidgetPtr trendPage = std::move(trend);
                const std::string trendTitle = raw->title();
                if (!app_.detachedWindows().detach(trendTitle, trendPage, [](ui::WidgetPtr) {}, 900, 460)) {
                    if (status_) status_->setTransientMessage("Impossible d'ouvrir la fen\xC3\xAAtre du graphique.", 5.0, ui::StatusBar::Severity::Warning);
                    return nullptr;
                }
                return raw;
            };
            // ---- fin 1.10 ----
            vars->setHosts(std::move(vh));
        }
        if (auto* types = pane->typesPane()) {
            HmiTypesPane::Hosts th;
            th.arrayType = [this](const std::string& current, std::function<void(const std::string&)> done) { askHmiArrayType(current, std::move(done)); };
            th.exportItems = [this](hmi::Id t) { askHmiExportPrograms(0, t); };   // 1.11.2 (decision 174)
            th.importAny = [this] { askHmiImport(); };
            types->setHosts(std::move(th));
            // 1.11.17 (refonte, lot 1) : Compiler les operateurs d'un type - le build du type.
            if (auto* ops = types->operators())
                ops->build = [this](hmi::pipeline::Mode mode, const std::string& k) { runHmiBuildFor(mode, k); };
        }
        wireAssist(*pane, app_, [this] { return dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); });
        page = std::move(pane);
        title = "IHM \xC2\xB7 Programmation g\xC3\xA9n\xC3\xA9rale";
        icon = Icon::Code;
    } else if (key == "fonctions") {
        auto pane = std::make_unique<HmiFunctionsPane>("hmi.functions", doc, apply);
        HmiFunctionsPane::Hosts hosts;
        hosts.newFunction = [this] { askHmiNewFunction(); };
        hosts.remove = [this](hmi::Id f) { askHmiDeleteFunction(f); };
        hosts.tryIt = [this](hmi::Id f) { askHmiTryFunction(f); };
        hosts.compile = [this] { openHmiPane("compiler"); };
        hosts.plc = [this]() -> ::sim::Environment* {
            return app_.simulation().attached() ? app_.simulationRuntime() : nullptr;
        };
        hosts.exportItems = [this](hmi::Id f) { askHmiExportPrograms(1, f); };   // 1.11.2 (decision 174)
        hosts.importAny = [this] { askHmiImport(); };
        wireHmiBuildHosts(hosts);   // 1.11.13
        pane->setHosts(std::move(hosts));
        wireAssist(*pane, app_, [this] { return dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); });
        page = std::move(pane);
        title = "IHM \xC2\xB7 Fonctions";
        icon = Icon::FunctionBlock;
    } else if (key == "styles") {
        // Lot 12 : les styles nommes.
        page = std::make_unique<HmiStylesPane>("hmi.styles", doc, apply);
        title = "IHM \xC2\xB7 Styles";
        icon = Icon::Settings;
    } else if (key == "essais") {
        // Lot 13 : les essais de reception - rejoues dans la simulation, ou tous d'un coup.
        auto pane = std::make_unique<HmiScenariosPane>("hmi.tests", doc, apply);
        HmiScenariosPane::Hosts hosts;
        hosts.play = [this](hmi::Id sc) {
            openHmiPane("simulation");
            if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) (void)sim->playScenario(sc);
        };
        hosts.stop = [this] {
            if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) sim->stopScenario();
        };
        hosts.plc = [this]() -> ::sim::Environment* { return app_.simulation().attached() ? app_.simulationRuntime() : nullptr; };
        hosts.exportFile = [this](const hmi::ExportRequest& rq, std::string* where) { return writeHmiExport(rq, where); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Essais";
        icon = Icon::Ok;
    } else if (key == "langues") {
        // Lot 13 : Configuration > Langues - les traductions, l'export et l'import Excel.
        auto pane = std::make_unique<HmiLanguagesPane>("hmi.languages", doc, apply);
        HmiLanguagesPane::Hosts hosts;
        hosts.exportFile = [this](const hmi::ExportRequest& rq, std::string* where) { return writeHmiExport(rq, where); };
        hosts.askImport = [this] { askHmiImportTranslations(); };
        hosts.askAdd = [this] { askHmiAddLanguage(); };
        hosts.tryIt = [this](const std::string& code) {
            openHmiPane("simulation");
            if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"))) {
                sim->refreshNow();
                std::string why;
                if (!sim->runtime().setLanguage(code, sim->runtime().now(), "Configuration > Langues", &why))
                    status_->setTransientMessage("Langue " + code + " : " + why, 6.0);
                sim->refreshNow();
            }
        };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Langues";
        icon = Icon::Globe;
    } else if (key == "unites") {
        // Lot 13 : Configuration > Unites et formats - ceux des variables, repris partout.
        auto pane = std::make_unique<HmiUnitsPane>("hmi.units", doc, apply);
        HmiUnitsPane::Hosts hosts;
        hosts.askAdd = [this] { askHmiAddUnit(); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Unit\xC3\xA9s et formats";
        icon = Icon::Variable;
    } else if (key == "communication") {
        // Lot 14 : Configuration > Communication - l'automate reel (Modbus TCP), le
        // plan d'adressage, Tester, le serveur de demonstration.
        auto pane = std::make_unique<HmiCommPane>("hmi.comm", doc, apply);
        HmiCommPane::Hosts hosts;
        hosts.plc = [this]() -> const domain::Project* { return app_.project().get(); };
        // Le simulateur dit le type des variables : prepare (sans le lancer) s'il ne l'est pas.
        hosts.runtime = [this]() -> ::sim::Runtime* {
            if (!app_.simulation().attached() && app_.project()) (void)app_.simulation().attach(app_.project());
            return app_.simulationRuntime();
        };
        hosts.comm = [this] { return &app_.comm(); };
        hosts.exportFile = [this](const hmi::ExportRequest& rq, std::string* where) { return writeHmiExport(rq, where); };
        // Lot 15 : les equipements du reseau, le reseau du PC ; l'outil Modbus.
        hosts.equipments = [this] { return &app_.equipments(); };
        hosts.openTool = [this](const std::string& host, int port, int unit, const std::string& tab) { openHmiModbusTool(host, port, unit, tab); };
        hosts.askBind = [this](const std::string& equipment) { askHmiBind(equipment); };
        // Lot 17 : les dialogues qui demandent avant d'agir (supprimer, deplacer, detecter les zones).
        hosts.ask = [this](HmiAskDialog::Spec spec, std::function<void(bool, const HmiAskDialog::Answer&)> done) {
            app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [done](const menu::DialogResult& r) {
                if (done) done(r.accepted(), HmiAskDialog::parse(r.payload));
            });
        };
        hosts.askBindAt = [this](const std::string& equipment, const std::string& address, const std::string& type) { askHmiBind(equipment, address, type); };
        hosts.askImportTwin = [this](const std::string& equipment) { askHmiImportTwin(equipment); };
        // Lot 16 : le double-clic d'une variable IHM du plan d'adressage l'ouvre la ou elle se declare.
        hosts.openVariable = [this](const std::string& path) {
            openHmiPane("scripts");
            if (auto* scripts = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
                scripts->showTab(HmiScriptsPane::TabVariables);
                if (auto* vars = scripts->variablesPane()) vars->selectPath(path);
            }
        };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 \xC3\x89quipements";
        icon = Icon::Network;
    } else if (key == "outil") {
        // Lot 15 : l'outil Modbus - lire, ecrire, trames, espion, ping.
        auto pane = std::make_unique<HmiModbusToolPane>("hmi.tool");
        HmiModbusToolPane::Hosts hosts;
        hosts.project = [this]() -> const hmi::Project* {
            const auto h = app_.hmi();
            return h ? &h->project : nullptr;
        };
        hosts.equipments = [this] { return &app_.equipments(); };
        hosts.exportFile = [this](const hmi::ExportRequest& rq, std::string* where) { return writeHmiExport(rq, where); };
        // 1.9 : la lecture cyclique a plusieurs requetes - les jeux de lecture (une commande
        // sur le projet IHM), l'enregistrement continu, la carte memoire, le presse-papiers.
        hosts.document = [this]() -> hmi::DocumentPtr { return app_.hmi(); };
        hosts.apply = apply;
        hosts.recordFolder = [this]() -> std::string {
            namespace fs = std::filesystem;
            std::error_code ec;
            const std::string projectFolder = app_.projectFolder();
            const fs::path folder = projectFolder.empty() ? fs::temp_directory_path(ec) / "xpg-exports" / "modbus" : fs::path(projectFolder) / "exports" / "modbus";
            const auto u8 = folder.u8string();
            return std::string(u8.begin(), u8.end());
        };
        hosts.openMemoryMap = [this](const std::string& equipment) {
            openHmiPane("communication");
            if (auto* comm = dynamic_cast<HmiCommPane*>(hmiTab("communication"))) {
                comm->tabs().setCurrentIndex(HmiCommPane::TMap);
                comm->showMap(equipment);
            }
        };
        hosts.clipboardText = [] { return ui::clipboardText(); };
        // Importer un jeu de lecture : le fichier CSV (le bouton ... : l'explorateur, dans exports/).
        hosts.importSetFile = [this] {
            std::vector<FormDialog::Field> fields;
            fields.push_back({"Fichier CSV", "", "un jeu \xC3\xA9" "crit par Jeu \xE2\x96\xBE \xE2\x80\xBA Exporter le jeu", false, {}});
            app_.menus().ShowDialog(
                FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiImportModbusSet", "Importer un jeu de lecture",
                    "Ses requ\xC3\xAAtes remplacent la liste de la lecture cyclique (Ctrl+Z dans l'outil la rend). "
                    "Enregistrer le jeu le garde dans le projet.",
                    std::move(fields), "Importer"), 0, ui::openFile("Fichiers CSV|*.csv;*.txt", ui::pathIn(app_.projectFolder(), "exports"))),
                [this](const menu::DialogResult& r) {
                    auto* tool = dynamic_cast<HmiModbusToolPane*>(hmiTab("outil"));
                    if (!r.accepted() || !tool) return;
                    const auto v = FormDialog::split(r.payload);
                    if (v.empty() || v[0].empty()) return;
                    const std::u8string u8(v[0].begin(), v[0].end());
                    std::ifstream in(std::filesystem::path(u8), std::ios::binary);
                    if (!in) {
                        status_->setTransientMessage("Importer un jeu : impossible d'ouvrir " + v[0], 8.0);
                        return;
                    }
                    std::stringstream text;
                    text << in.rdbuf();
                    std::string why;
                    if (!tool->cyclic().importSet(text.str(), &why)) status_->setTransientMessage("Importer un jeu : " + why, 8.0);
                });
        };
        // Les variables localisees du programme (et la table des adresses) : le plan de la liaison.
        hosts.plcVariables = [this]() {
            std::vector<HmiModbusToolPane::Hosts::PlcVariable> out;
            const auto h = app_.hmi();
            if (!h) return out;
            const auto plan = hmi::comm::buildPlan(app_.project().get(), h->project.comm);
            for (const auto& pt : plan.points()) out.push_back({pt.name, pt.typeName, pt.address, pt.description});
            return out;
        };
        pane->setHosts(std::move(hosts));
        // La premiere cible : le premier equipement Modbus du projet, sinon l'automate.
        if (const auto h = app_.hmi()) {
            bool chosen = false;
            for (const auto& e : h->project.equipments)
                if (!chosen && e.modbus() && e.enabled) chosen = pane->chooseEquipment(e.name);
            if (!chosen && h->project.comm.modbus()) pane->setTarget(h->project.comm.host, h->project.comm.port, h->project.comm.unit);
        }
        page = std::move(pane);
        title = "IHM \xC2\xB7 Outil Modbus";
        icon = Icon::Settings;
    } else if (key == "notifications") {
        // Lot 14 : Configuration > Notifications - courriels et SMS des alarmes.
        auto pane = std::make_unique<HmiNotifyPane>("hmi.notify", doc, apply);
        HmiNotifyPane::Hosts hosts;
        hosts.notify = [this] { return &app_.notify(); };
        hosts.askPassword = [this] { askHmiSmtpPassword(); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Notifications";
        icon = Icon::Mail;
    } else if (key == "rapports") {
        // Lot 14 : Configuration > Rapports - les rapports periodiques.
        auto pane = std::make_unique<HmiReportsPane>("hmi.reports", doc, apply);
        HmiReportsPane::Hosts hosts;
        hosts.write = [this](hmi::Id report, bool current, std::string* where) {
            // La simulation en marche : ses compteurs de production ; sinon l'historique seul.
            if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); sim && sim->runtime().running())
                return sim->runtime().writeReport(report, sim->runtime().now(), current, where);
            auto current_doc = app_.hmi();
            if (!current_doc) return false;
            hmi::ReportOutput out;
            const bool ok = HmiReportsPane::writeOffline(current_doc->project, current_doc->history, report, current,
                                                         [this](const hmi::ExportRequest& rq, std::string* w) { return writeHmiExport(rq, w); }, where, &out);
            if (ok) app_.notify().report(out);
            return ok;
        };
        hosts.next = [this](hmi::Id report) {
            if (auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); sim && sim->runtime().running())
                return sim->runtime().nextReportText(report);
            return std::string{};
        };
        hosts.projectFolder = [this] { return app_.projectFolder(); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Rapports";
        icon = Icon::Document;
    } else if (key == "web") {
        // Lot 14 : Configuration > Acces web - l'IHM dans un navigateur.
        auto pane = std::make_unique<HmiWebPane>("hmi.web", doc, apply);
        HmiWebPane::Hosts hosts;
        hosts.web = [this] { return &app_.web(); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Acc\xC3\xA8s web";
        icon = Icon::Globe;
    } else if (key == "poste") {
        // Lot 14 : Configuration > Poste d'exploitation - l'IHM seule, en plein
        // ecran ; les ecrans secondaires ; le lanceur ; le demarrage avec la session.
        auto pane = std::make_unique<HmiStationPane>("hmi.station", doc, apply);
        HmiStationPane::Hosts hosts;
        hosts.projectFolder = [this] { return app_.projectFolder(); };
        hosts.displays = [this] { return app_.displayCount(); };
        hosts.tryStation = [this] { app_.enterStation(); };
        hosts.finished = [this](std::string* why) { return app_.projectFinished(why); };   // lot 15
        hosts.askPassword = [this] { askHmiStationPassword(); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Poste d'exploitation";
        icon = Icon::Station;
    } else if (key == "rechercher") {
        // Lot 12 : rechercher / remplacer, avec l'apercu.
        auto pane = std::make_unique<HmiFindPane>("hmi.find", doc, apply);
        hmiLinks_ += pane->openHit->connect([this](hmi::Id v, hmi::Id o) { openHmiView(v, -1, o); });
        page = std::move(pane);
        title = "IHM \xC2\xB7 Rechercher / remplacer";
        icon = Icon::Search;
    } else if (key == "variables-publiques") {
        // Lot 9 : les variables systeme et d'instances ; en marche, leurs valeurs.
        auto pane = std::make_unique<HmiPublicVarsPane>("hmi.public", doc);
        HmiPublicVarsPane::Hosts hosts;
        hosts.live = [this](const std::string& path, std::string& out) {
            auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
            if (!sim || !sim->runtime().running()) return false;
            ::sim::Value v;
            if (!sim->runtime().environment().read(path, v)) return false;
            out = hmi::formatValue(v);
            return true;
        };
        hosts.help = [this](const std::string& topic) { openHmiHelp(topic); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Variables syst\xC3\xA8me et d'instances";
        icon = Icon::Variable;
    } else if (key == "aide") {
        auto pane = std::make_unique<HmiHelpPane>("hmi.help");
        HmiHelpPane::Hosts hosts;
        hosts.replayTutorial = [this] { startHmiTutorial(); };
        // Lot 21 : les parcours du didacticiel (leurs cartes sur la page du didacticiel).
        hosts.trails = [this] { return tutorialTrails(); };
        hosts.startTrail = [this](const std::string& trail, bool resume) { startTrail(trail, resume); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Aide";
        icon = Icon::Info;
    } else if (key == "ressources") {
        auto pane = std::make_unique<HmiResourcesPane>("hmi.resources", doc, apply);
        HmiResourcesPane::Hosts hosts;
        hosts.importFile = [this] { askHmiImportResource(); };
        hosts.replace = [this](hmi::Id r) { askHmiReplaceResource(r); };
        hosts.rename = [this](hmi::Id r) { askHmiRenameResource(r); };
        hosts.remove = [this](hmi::Id r) { askHmiDeleteResource(r); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Ressources";
        icon = Icon::Image;
    } else if (key == "fichiers") {
        auto pane = std::make_unique<HmiFilesPane>("hmi.files", doc, apply);
        HmiFilesPane::Hosts hosts;
        hosts.linkFile = [this] { askHmiLinkFile(false); };
        hosts.linkDatabase = [this] { askHmiLinkFile(true); };
        hosts.remove = [this](hmi::Id f) { askHmiRemoveFile(f); };
        hosts.choosePart = [this](hmi::Id f) { askHmiChoosePart(f); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Fichiers externes";
        icon = Icon::Document;
    } else if (key == "alarmes") {
        auto pane = std::make_unique<HmiAlarmsPane>("hmi.alarms", doc, apply);
        HmiAlarmsPane::Hosts hosts;
        hosts.remove = [this](hmi::Id a) { askHmiDeleteAlarm(a); };
        // 1.11 (R111) : « Lier... » d'un groupe d'alarmes, une fenetre a cocher.
        hosts.ask = [this](HmiAskDialog::Spec spec, std::function<void(bool, const HmiAskDialog::Answer&)> done) {
            app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [done](const menu::DialogResult& r) {
                if (done) done(r.accepted(), HmiAskDialog::parse(r.payload));
            });
        };
        pane->setHosts(std::move(hosts));
        // 1.9 : un double-clic sur une alarme generee ouvre l'objet dans sa vue (maquette A3).
        pane->generated().open = [this](hmi::Id v, hmi::Id o) { openHmiView(v, -1, o); };
        pane->generated().openSymbol = [this](hmi::Id s) { openHmiView(s); };   // 1.9 (chantier U) : la fiche A3
        // 1.9 (decision 7) : la pastille fx des conditions generees juge aussi les globales de l'automate.
        pane->generated().plc = [this] { return app_.project().get(); };
        pane->generated().refresh();
        page = std::move(pane);
        title = "IHM \xC2\xB7 Alarmes";
        icon = Icon::Warning;
    } else if (key == "recettes") {
        auto pane = std::make_unique<HmiRecipesPane>("hmi.recipes", doc, apply);
        HmiRecipesPane::Hosts hosts;
        hosts.importCsv = [this](hmi::Id r) { askHmiRecipeCsv(r, true); };
        hosts.exportCsv = [this](hmi::Id r) { askHmiRecipeCsv(r, false); };
        hosts.compare = [this](hmi::Id r) { askHmiCompareRecords(r); };
        hosts.remove = [this](hmi::Id r) { askHmiDeleteRecipe(r); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Recettes";
        icon = Icon::AnimationTable;
    } else if (key == "utilisateurs") {
        auto pane = std::make_unique<HmiUsersPane>("hmi.users", doc, apply);
        HmiUsersPane::Hosts hosts;
        hosts.password = [this](hmi::Id u) { askHmiPassword(u); };
        hosts.removeUser = [this](hmi::Id u) { askHmiDeleteUser(u); };
        hosts.removeGroup = [this](hmi::Id g) { askHmiDeleteGroup(g); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Utilisateurs";
        icon = Icon::User;
    } else if (key == "historiques") {
        auto pane = std::make_unique<HmiHistoryPane>("hmi.history", doc, apply);
        HmiHistoryPane::Hosts hosts;
        hosts.exportCsv = [this](int tab) { askHmiHistoryExport(tab); };
        hosts.clear = [this] { askHmiClearHistory(); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Historiques";
        icon = Icon::Document;
    } else if (key == "echange") {
        auto pane = std::make_unique<HmiExchangePane>("hmi.exchange", doc, apply, plcNames(app_.project()), app_.projectFolder());
        HmiExchangePane::Hosts hosts;
        hosts.exportArchive = [this] { askHmiArchive(false); };
        hosts.importArchive = [this] { askHmiArchive(true); };
        hosts.openIssue = [this](const hmi::Issue& i) { openHmiIssue(i); };
        // Lot 13 : le dossier de l'IHM va dans exports/, comme les exports.
        hosts.writeFile = [this](const hmi::ExportRequest& rq, std::string* where) { return writeHmiExport(rq, where); };
        pane->setHosts(std::move(hosts));
        page = std::move(pane);
        title = "IHM \xC2\xB7 Exporter / Importer";
        icon = Icon::Export;
    } else if (key == "versions") {
        // Lot 21 : les versions du projet.
        auto pane = std::make_unique<VersionsPane>("versions", app_.projectFolder());
        VersionsPane::Hosts hosts;
        hosts.create = [this] { askCreateVersion(); };
        hosts.restore = [this](int n) { askRestoreVersion(n); };
        hosts.remove = [this](int n) { askDeleteVersion(n); };
        hosts.exportZip = [this](int n) { exportVersion(n); };
        hosts.extract = [this](int n) { askExtractVersion(n); };
        hosts.compare = [this](int a, int b, const std::string& element) { openVersionCompare(a, b, element); };
        hosts.unsaved = [this] { return app_.pendingChanges(); };
        hosts.changed = [this] { refreshVersions(); };
        // Lot API 6 : la premiere ligne dit la version que l'on modifie ("V4 en cours . DEV").
        hosts.workingTitle = [this](const hmi::ver::Store& store) {
            const auto st = app_.manifest().state;
            const auto s = hmi::ver::standing(st, &store, 0, 0);
            const std::string name(project::toString(st));
            // Un FINISH ou un LOCK d'avant les versions se dit deja par son nom :
            // "FINISH . derniere version : V3", pas "FINISH . FINISH".
            if (s.title != name) return s.title + " \xC2\xB7 " + name;
            return s.subtitle.empty() ? s.title : s.title + " \xC2\xB7 " + s.subtitle;
        };
        pane->setHosts(std::move(hosts));
        pane->refresh();
        page = std::move(pane);
        title = "Versions";
        icon = Icon::History;
    } else {
        auto info = laterPane(key);
        page = std::make_unique<HmiInfoPane>("hmi." + key, info.title, std::move(info.lines));
        title = info.title;
        icon = info.icon;
    }

    auto* raw = page.get();
    const auto tab = centre_->addTab(TabControl::Tab{title, icon, /*closable=*/true, false}, std::move(page));
    hmiTabs_[key] = raw;
    centre_->setCurrentIndex(tab);
}

void MainAnalysisScreen::openHmiView(std::uint64_t viewId, int part, std::uint64_t objectId) {
    auto doc = app_.hmi();
    if (!doc || !centre_) return;
    const auto* view = doc->project.view(asId(viewId));
    if (!view) return;

    const std::string key = "vue:" + std::to_string(viewId);
    auto* editor = dynamic_cast<HmiEditor*>(hmiTab(key));
    if (!editor) {
        auto apply = [this](core::CommandPtr c) { if (c) app_.apply(std::move(c), /*refreshViews=*/false); };
        auto made = std::make_unique<HmiEditor>("hmi.view." + std::to_string(viewId), doc, asId(viewId), apply,
                                                app_.project());
        made->setHistory(
            [this](bool redo) { (void)app_.actions().trigger(redo ? "edit.redo" : "edit.undo", app_.commands()); },
            [this](bool redo) { return redo ? app_.commands().canRedo() : app_.commands().canUndo(); });
        // LES FAVORIS DE LA BIBLIOTHEQUE sont ceux de l'utilisateur, pas ceux
        // d'un onglet : relus des preferences a l'ouverture, ecrits a chaque
        // changement, et repris aussitot par les autres editeurs ouverts.
        if (const auto saved = app_.settings().getList("hmi.palette.favorites"); !saved.empty()) {
            std::set<hmi::Kind> favs;
            for (const auto& k : saved)
                if (const auto kind = hmi::kindFromKey(k)) favs.insert(*kind);
            made->palette().setFavorites(std::move(favs));    // "-" seul : aucun favori
        }
        HmiEditor* raw = made.get();
        // ---- Lot API 8 : les expressions impossibles ----
        // L'infobulle d'une propriete pilotee dit sa valeur pendant la simulation de l'IHM.
        made->properties().setExprValueProvider([this](const ui::PropertyGrid::Property& p) -> std::string {
            auto* sim = dynamic_cast<HmiSimulationPane*>(hmiTab("simulation"));
            if (!sim || !sim->runtime().running() || p.expression.empty()) return {};
            const auto e = hmi::Expression::compile(p.expression);
            if (!e.valid()) return {};
            auto v = e.evaluate(sim->runtime().environment());
            return v ? hmi::formatValue(*v) : "illisible (" + v.error().message() + ")";
        });
        // ---- fin Lot API 8 ----
        // Lot 10 : "Creer un symbole" passe par un dialogue ; double-clic sur une
        // instance ouvre son symbole.
        made->setSymbolAsker([this, viewId] { askHmiSymbol(viewId); });
        // 1.11.3 : le carre de legende d'une case ouvre la liste des carres, puis le selecteur.
        made->setValueAsker([this, viewId](const valuekind::Request& r) { askHmiValue(viewId, r); });
        // 1.11.9 : les petites fenetres des actions (l'operation en arbre, le script, la formule de Maths).
        made->actions().setDialogHost(
            [this](menu::MenuPtr dialog, std::function<void(const menu::DialogResult&)> onClose) {
                app_.menus().ShowDialog(std::move(dialog), std::move(onClose));
            },
            [this] { return app_.project(); });
        made->setStyleAsker([this, viewId] { askHmiStyle(viewId); });     // lot 12
        made->setDuplicateAsker([this, viewId] { askHmiDuplicate(viewId); });   // 1.10.2 (chantier D)
        made->setTemplateAsker([this, viewId] { askHmiSaveTemplate(viewId); });   // lot 20
        hmiLinks_ += made->openView->connect([this](hmi::Id v) { openHmiView(v); });
        hmiLinks_ += made->palette().favoritesChanged->connect([this, raw] {
            std::vector<std::string> keys;
            for (const auto k : raw->palette().favorites()) keys.emplace_back(hmi::kindKey(k));
            if (keys.empty()) keys.emplace_back("-");
            app_.settings().setList("hmi.palette.favorites", keys);
            for (const auto& entry : hmiTabs_)
                if (auto* other = dynamic_cast<HmiEditor*>(entry.second); other && other != raw)
                    other->palette().setFavorites(raw->palette().favorites());
        });
        editor = made.get();
        const auto tab = centre_->addTab(TabControl::Tab{view->name, Icon::Screen, true, false}, std::move(made));
        hmiTabs_[key] = editor;
        centre_->setCurrentIndex(tab);
        editor->canvas().zoomToFit();
    } else {
        centre_->setCurrentIndex(static_cast<std::size_t>(centre_->indexOf(editor)));
    }

    // Aller a la source : l'objet d'un constat de Generer ou de Compiler.
    if (objectId != 0 && view->object(asId(objectId))) {
        editor->canvas().setSelection({asId(objectId)});
        editor->objects().revealSelection();
    }
    // Une partie de la vue, choisie dans l'arbre : ce qu'elle designe est deja
    // a l'ecran (explorateur d'objets, calques, groupes) ; le dire suffit.
    // 1.11.12 : les Fonctions et les Popups d'un symbole ouvrent leur sous-onglet ;
    // la phrase vient de hmiPartHint (la table de la 1.11.11 n'avait que 5 phrases
    // pour 7 parties : un clic sur Fonctions ou Popups plantait).
    using Part = ProjectTreeModel::HmiPart;
    if (part >= 0 && part < static_cast<int>(Part::Count)) {
        const auto which = static_cast<Part>(part);
        if (auto* tabs = editor->symbolTabs(); tabs && (which == Part::Functions || which == Part::Popups))
            tabs->setCurrent(which == Part::Functions ? HmiSymbolTabs::Functions : HmiSymbolTabs::Popups);
        if (const char* hint = ProjectTreeModel::hmiPartHint(which)) status_->setTransientMessage(hint, 8.0);
    }
}

// Forcer une variable depuis le volet Simulation : la valeur est une
// expression ST (TRUE, 7.5, T#2s, 'Azote', Armoires[0].seuil_poids_saisi + 1),
// evaluee par le meme moteur que les vues - donc typee comme l'automate l'attend.
void MainAnalysisScreen::askHmiForce(std::string variable) {
    // 1.10 : la memoire de l'automate simule, preparee sans cycle - l'API reste
    // arretee (avant : "sim.pause", l'API se disait en pause sans qu'on l'ait lancee).
    if (!app_.simulation().attached()) runSimulationTransport("sim.prepare");
    auto* rt = app_.simulationRuntime();
    if (!rt) return;
    std::string current;
    if (!variable.empty()) {
        sim::Value v;
        if (rt->get(variable, v)) current = hmi::formatValue(v);
    }
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Variable", variable, "Armoires[0].ana.PT1.mes", false, {}});
    fields.push_back({"Valeur", current, "TRUE, 42, 7.5, T#2s, 'texte' ou une expression", false, {}});
    auto dialog = std::make_unique<FormDialog>("dialog.hmiForce", "Forcer une variable",
            "Tant qu'elle est forc\xC3\xA9" "e, le programme n'\xC3\xA9" "crit plus dans la variable : elle relit "
            "toujours cette valeur, comme dans la table de for\xC3\xA7" "age de l'automate. \"Tout rel\xC3\xA2" "cher\" "
            "la rend au programme.",
            std::move(fields), "Forcer");
    if (auto doc = app_.hmi()) {
        dialog->setFieldAssist(0, assist::fieldAssist(assist::sourcesFor(doc)));
        dialog->setFieldAssist(1, assist::fieldAssist(assist::sourcesFor(doc)));
    }
    app_.menus().ShowDialog(std::move(dialog),
        [this](const menu::DialogResult& r) {
            auto* runtime = app_.simulationRuntime();
            if (!r.accepted() || !runtime) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 2 || v[0].empty()) return;
            auto value = hmi::Expression::compile(v[1]).evaluate(*runtime);
            if (!value) {
                status_->setTransientMessage("Valeur illisible : " + value.error().message(), 8.0);
                return;
            }
            if (!runtime->force(v[0], *value))
                status_->setTransientMessage(v[0] + " : variable inconnue du simulateur", 8.0);
            else
                status_->setTransientMessage(v[0] + " forc\xC3\xA9" "e \xC3\xA0 " + hmi::formatValue(*value), 6.0);
        });
}

void MainAnalysisScreen::openHmiViewsFolder(int folder) {
    openHmiPane("vues");
    if (auto* pane = dynamic_cast<HmiViewsPane*>(hmiTab("vues"))) {
        pane->setFolder(folder);
        // Le titre de l'onglet dit le dossier montre.
        if (centre_)
            for (std::size_t i = 0; i < centre_->tabCount(); ++i)
                if (centre_->page(i) == pane) centre_->setTabTitle(i, "IHM \xC2\xB7 " + pane->folderTitle());
    }
}

// Lot 20 : askNewHmiView (la galerie des modeles) est dans TemplatesWorkspace.cpp.

// Lot 10 : "Creer un symbole". Le dialogue propose un nom libre et les
// parametres lus dans la selection (Armoire := Armoires[0]) ; l'editeur fait la
// commande. L'editeur est RETROUVE a la reponse : l'onglet a pu etre ferme.
// ---- 1.11.3 : le selecteur de valeur et la creation d'une variable ----
void MainAnalysisScreen::askHmiValue(std::uint64_t viewId, const valuekind::Request& req) {
    auto doc = app_.hmi();
    if (!doc) return;
    if (!req.create.empty()) {
        askHmiCreateVariable(viewId, req, req.text, req.fx, req.create);
        return;
    }
    HmiValuePicker::Spec spec;
    spec.field = req.field;
    spec.expected = req.expected;
    spec.text = req.text;
    spec.fx = req.fx;
    spec.source = req.source;
    spec.doc = doc;
    spec.view = asId(viewId);
    spec.plc = app_.project();
    app_.menus().ShowDialog(std::make_unique<HmiValuePicker>(std::move(spec)), [this, viewId, req](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto a = HmiValuePicker::parse(r.payload);
        if (!a.create.empty()) {
            askHmiCreateVariable(viewId, req, a.text, a.fx, a.create);
            return;
        }
        commitHmiValue(viewId, req, a.text, a.fx);
    });
}

void MainAnalysisScreen::commitHmiValue(std::uint64_t viewId, const valuekind::Request& req, const std::string& text, bool fx) {
    auto* ed = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
    if (!ed || !ed->commitValue(req.category, req.property, text, fx)) {
        status_->setTransientMessage("La case \xC2\xAB " + req.field + " \xC2\xBB n'est plus montr\xC3\xA9" "e : la valeur n'a pas \xC3\xA9t\xC3\xA9 \xC3\xA9" "crite.", 8.0);
        return;
    }
    status_->setTransientMessage(req.field + " \xE2\x86\x90 " + (text.empty() ? std::string("(d\xC3\xA9" "faut)") : (fx ? "=" : "") + text)
                                     + " \xC2\xB7 Ctrl+Z annule",
                                 6.0);
}

void MainAnalysisScreen::askHmiCreateVariable(std::uint64_t viewId, const valuekind::Request& req, const std::string& text, bool fx,
                                              const std::string& name) {
    auto doc = app_.hmi();
    if (!doc) return;
    const auto plc = app_.document();
    // Le type propose : celui que la case attend, s'il en est un vrai.
    std::string wanted = req.expected;
    {
        std::string u;
        for (const char c : wanted) u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (u.empty() || u == "ANY" || u == "TEXTE" || u == "COULEUR") wanted = "STRING";
        else if (u == "NOMBRE") wanted = "REAL";
    }
    const auto hmiTypes = [doc, wanted] {
        std::vector<std::string> out{wanted};
        for (const auto& t : hmi::typereg::baseRegistry().names(hmi::typereg::UseVariable)) if (t != wanted) out.push_back(t);
        for (const auto& t : doc->project.programs.types) if (t.name != wanted) out.push_back(t.name);
        return out;
    };
    const auto plcTypes = [plc, wanted] {
        std::vector<std::string> out{wanted};
        if (plc)
            for (const auto& c : project::availableTypeChoices(*plc)) if (c.name != wanted) out.push_back(c.name);
        return out;
    };
    const std::string zoneHmi = "IHM \xE2\x80\x94 une variable de l'IHM";
    const std::string zoneApi = "API \xE2\x80\x94 une variable de l'automate";
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", name, "Debit_Max", false, {}});
    fields.push_back({"Zone", zoneHmi, {}, false, plc ? std::vector<std::string>{zoneHmi, zoneApi} : std::vector<std::string>{zoneHmi}});
    fields.push_back({"Type", wanted, {}, false, hmiTypes()});
    fields.push_back({"Valeur initiale (IHM)", {}, "vide : 0, FALSE ou ''", false, {}});
    fields.push_back({"Adresse (API, facultative)", {}, "%MW100 ; vide : non situ\xC3\xA9" "e", false, {}});
    fields.push_back({"Commentaire", "Cr\xC3\xA9\xC3\xA9" "e depuis le s\xC3\xA9lecteur de " + req.field, {}, false, {}});
    auto dialog = std::make_unique<FormDialog>(
        "dialog.hmiCreateVariable", "Cr\xC3\xA9" "er la variable \xC2\xAB " + name + " \xC2\xBB",
        name + " n'existe ni dans l'automate, ni dans l'IHM. Le champ " + req.field + " attend " + valuekind::expectedLabel(req.expected)
            + ".\nIHM : elle s'ajoute aux variables IHM du projet (Programmation g\xC3\xA9n\xC3\xA9rale > Variables IHM). "
              "API : elle s'ajoute aux variables globales du programme ; elle devra exister dans Control Expert. "
              "Un seul Ctrl+Z retire la variable et la saisie.",
        std::move(fields), "Cr\xC3\xA9" "er et valider");
    dialog->setRules([zoneApi, hmiTypes, plcTypes](const std::vector<std::string>& v, std::vector<FormDialog::FieldState>& st) {
        if (v.size() < 6 || st.size() < 6) return;
        const bool api = v[1] == zoneApi;
        const auto choices = api ? plcTypes() : hmiTypes();
        st[2].choices = choices;
        if (std::find(choices.begin(), choices.end(), v[2]) == choices.end()) st[2].value = choices.front();
        st[3].enabled = !api;
        st[3].hint = api ? "seulement pour une variable IHM" : std::string{};
        st[4].enabled = api;
        st[4].hint = api ? std::string{} : "seulement pour une variable de l'automate";
    });
    app_.menus().ShowDialog(std::move(dialog), [this, viewId, req, text, fx, name, zoneApi](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto v = FormDialog::split(r.payload);
        if (v.size() < 6) return;
        auto hdoc = app_.hmi();
        if (!hdoc) return;
        const std::string newName = v[0];
        std::string why;
        if (!hmi::isIdentifier(newName)) why = "\xC2\xAB " + newName + " \xC2\xBB n'est pas un nom de variable (des lettres, des chiffres et _).";
        else if (hdoc->project.variable(newName)) why = "Une variable IHM s'appelle d\xC3\xA9j\xC3\xA0 " + newName + ".";
        if (!why.empty()) {
            status_->setTransientMessage(why, 8.0);
            return;
        }
        // Le nom change ? Il change aussi dans la valeur.
        std::string value = text;
        if (newName != name && !value.empty()) {
            std::string out;
            std::size_t at = 0, hit = 0;
            while ((hit = value.find(name, at)) != std::string::npos) {
                out += value.substr(at, hit - at) + newName;
                at = hit + name.size();
            }
            value = out + value.substr(at);
        }
        if (value.empty()) value = newName;
        // UN SEUL CTRL+Z : la variable et la saisie.
        core::CommandGroupScope group("Cr\xC3\xA9" "er " + newName + " et l'utiliser");
        if (v[1] == zoneApi) {
            project::AddVariableCommand::Spec spec;
            spec.name = newName;
            spec.type = v[2];
            spec.address = v[4];
            spec.comment = v[5];
            app_.apply(std::make_unique<project::AddVariableCommand>(app_.document(), spec), true);
            if (!app_.project() || std::none_of(app_.project()->variables.begin(), app_.project()->variables.end(), [&](const domain::Variable& x) {
                    return app_.project()->strings.text(x.name) == newName;
                })) {
                status_->setTransientMessage("La variable de l'automate n'a pas \xC3\xA9t\xC3\xA9 cr\xC3\xA9\xC3\xA9" "e (nom pris, type ou adresse refus\xC3\xA9s).", 8.0);
                return;
            }
        } else {
            std::string initial = v[3];
            const std::string type = hmi::types::normalized(v[2]);
            if (initial.empty()) initial = type == "BOOL" ? "FALSE" : type == "STRING" ? "''" : hmi::types::isComposite(type) ? "" : "0";
            if (!hmi::types::validType(hdoc->project, type, &why)) {
                status_->setTransientMessage("Type refus\xC3\xA9 : " + why, 8.0);
                return;
            }
            auto cmd = hmi::changeProject(hdoc, "Nouvelle variable " + newName, [&](hmi::Project& p) {
                hmi::Variable var;
                var.id = p.allocate();
                var.name = newName;
                var.type = type;
                var.initial = initial;
                var.description = v[5];
                p.programs.variables.push_back(std::move(var));
            });
            if (cmd) app_.apply(std::move(cmd), false);
        }
        commitHmiValue(viewId, req, value, fx || value == newName);
        group.close();
    });
}

void MainAnalysisScreen::askHmiSymbol(std::uint64_t viewId) {
    auto doc = app_.hmi();
    auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
    const auto* view = doc ? doc->project.view(asId(viewId)) : nullptr;
    if (!doc || !editor || !view) return;
    const auto sel = editor->canvas().selection();
    if (sel.empty()) {
        status_->setTransientMessage("Choisis d'abord, dans la vue, les objets qui feront le symbole.", 6.0);
        return;
    }
    std::vector<FormDialog::Field> fields;
    // Lot 21 : pendant le parcours "Creer un symbole de projet", le nom qu'il attend.
    const std::string suggested = !trailSymbolName_.empty() && !doc->project.viewByName(trailSymbolName_)
                                      ? trailSymbolName_ : hmi::uniqueViewName(doc->project, "Symbole");
    fields.push_back({"Nom du symbole", suggested, "Carte_Armoire, Faceplate_Pompe...", false, {}});
    fields.push_back({"Param\xC3\xA8tres", hmi::suggestSymbolParams(*view, sel), "Armoire := Armoires[0]; Nom := 'A'", false, {}});
    auto dialog = std::make_unique<FormDialog>("dialog.hmiSymbol", "Cr\xC3\xA9" "er un symbole",
        "Les " + std::to_string(sel.size()) + " objet(s) choisi(s) deviennent un symbole r\xC3\xA9utilisable (IHM > Symboles, "
        "et la biblioth\xC3\xA8que de l'\xC3\xA9" "diteur) ; une instance les remplace, \xC3\xA0 leur place.\n"
        "Param\xC3\xA8tres : nom := valeur par d\xC3\xA9" "faut. Un chemin (Armoires[0]) donn\xC3\xA9 en valeur est remplac\xC3\xA9 "
        "par le nom du param\xC3\xA8tre dans les expressions du symbole ; chaque instance relie ensuite le sien "
        "(Armoire := Armoires[1]). Modifier le symbole modifie toutes ses instances. Ctrl+Z d\xC3\xA9" "fait tout.",
        std::move(fields), "Cr\xC3\xA9" "er");
    dialog->setFieldAssist(1, assist::fieldAssist(assist::sourcesFor(doc)));
    // Lot 21 : un parcours en cours suit le dialogue (l'etape "Nomme-le" met sa bulle de cote).
    dialog->setUpdatesBelow(hmiTutorial_ && hmiTutorial_->active());
    app_.menus().ShowDialog(std::move(dialog), [this, viewId](const menu::DialogResult& r) {
        auto* ed = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
        if (!r.accepted() || !ed) return;
        const auto v = FormDialog::split(r.payload);
        if (v.empty()) return;
        std::string why;
        if (!ed->makeSymbol(v[0], v.size() > 1 ? v[1] : std::string{}, &why))
            status_->setTransientMessage("Symbole non cr\xC3\xA9\xC3\xA9 : " + why, 8.0);
    });
}

// Lot 12 : "Dupliquer en remplacant" - la copie d'une vue ou un texte devient
// un autre partout (Armoires[0] -> Armoires[1]) : le nom de la copie, le texte,
// son remplacant ; une seule commande.
void MainAnalysisScreen::askHmiDuplicateReplace(std::uint64_t viewId) {
    auto doc = app_.hmi();
    const auto* view = doc ? doc->project.view(asId(viewId)) : nullptr;
    if (!view) return;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom de la copie", hmi::uniqueViewName(doc->project, view->name + "_2"), "Vue_Armoire_B", false, {}});
    fields.push_back({"Rechercher", "", "Armoires[0]", false, {}});
    fields.push_back({"Remplacer par", "", "Armoires[1]", false, {}});
    fields.push_back({"Respecter la casse", "Non", "", false, {"Non", "Oui"}});
    fields.push_back({"Mot entier", "Non", "", false, {"Non", "Oui"}});
    auto dialog = std::make_unique<FormDialog>("dialog.hmiDuplicateReplace", "Dupliquer " + view->name + " en rempla\xC3\xA7" "ant",
        "Une copie de la vue (objets, calques, scripts, actions, param\xC3\xA8tres) o\xC3\xB9 le texte cherch\xC3\xA9 est remplac\xC3\xA9 "
        "partout : les expressions, les textes \xC3\xA0 trous, les actions, les scripts. Ctrl+Z retire la copie.",
        std::move(fields), "Dupliquer");
    const std::string sourceName = view->name;
    app_.menus().ShowDialog(std::move(dialog), [this, viewId, sourceName](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto v = FormDialog::split(r.payload);
        if (v.empty() || v[0].empty()) return;
        hmi::design::FindOptions o;
        o.matchCase = v.size() > 3 && v[3] == "Oui";
        o.wholeWord = v.size() > 4 && v[4] == "Oui";
        const std::string find = v.size() > 1 ? v[1] : std::string{}, repl = v.size() > 2 ? v[2] : std::string{};
        hmi::Id made = hmi::kNoId;
        std::size_t changed = 0;
        auto cmd = hmi::changeProject(current, "Dupliquer " + sourceName + " en rempla\xC3\xA7" "ant", [&](hmi::Project& p) {
            made = hmi::design::duplicateViewReplacing(p, asId(viewId), v[0], find, repl, o, &changed);
        });
        if (!cmd) return;
        app_.apply(std::move(cmd), false);
        if (made != hmi::kNoId) openHmiView(made);
        const auto* copy = current->project.view(made);
        status_->setTransientMessage((copy ? copy->name : v[0]) + " : copie de " + sourceName
                                         + (find.empty() ? std::string{} : ", \xC2\xAB " + find + " \xC2\xBB \xE2\x86\x92 \xC2\xAB " + repl + " \xC2\xBB dans "
                                                                               + std::to_string(changed) + " champ(s)")
                                         + " (Ctrl+Z la retire)",
                                     8.0, ui::StatusBar::Severity::Success);
    });
}

// Lot 12 : "Depuis un type" - une instance d'un DDT ou d'un DFB (son type, ses
// membres), popup ou vue, le nom : la vue generee s'ouvre dans l'editeur.
void MainAnalysisScreen::askHmiViewFromType() {
    auto doc = app_.hmi();
    if (!doc) return;
    auto plc = app_.project();
    std::vector<hmi::design::VarInfo> structures;
    for (auto& v : assist::designVariables(doc->project, plc.get()))
        if (hmi::design::shapeOf(v) == hmi::design::VarShape::Structure) structures.push_back(std::move(v));
    if (structures.empty()) {
        status_->setTransientMessage("Aucune instance de DDT ou de DFB dans le programme de l'automate : rien \xC3\xA0 g\xC3\xA9n\xC3\xA9rer.",
                                     8.0, ui::StatusBar::Severity::Warning);
        return;
    }
    std::vector<std::string> choices;
    for (const auto& v : structures) choices.push_back(v.name + "  (" + v.type + ")");
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Instance", choices.front(), "celle que montre l'\xC3\xA9" "diteur", false, choices});
    fields.push_back({"Faire", "Une popup", "", false, {"Une popup", "Une vue"}});
    fields.push_back({"Nom", "", "vide : Popup_<Type>", false, {}});
    auto dialog = std::make_unique<FormDialog>("dialog.hmiViewFromType", "G\xC3\xA9n\xC3\xA9rer depuis un type",
        "Une ligne par membre du type (son commentaire, sinon son nom) et l'objet de sa valeur : un voyant pour un BOOL, un "
        "afficheur pour un nombre, un texte pour une cha\xC3\xAEne. Tout se lit \xC3\xA0 travers le param\xC3\xA8tre Equipement : "
        "la m\xC3\xAAme popup sert \xC3\xA0 toutes les instances (Ouvrir une popup, Equipement := Pompe_2). Ctrl+Z la retire.",
        std::move(fields), "G\xC3\xA9n\xC3\xA9rer");
    app_.menus().ShowDialog(std::move(dialog), [this, structures](const menu::DialogResult& r) {
        auto current = app_.hmi();
        if (!r.accepted() || !current) return;
        const auto v = FormDialog::split(r.payload);
        if (v.empty()) return;
        const hmi::design::VarInfo* chosen = nullptr;
        for (const auto& s : structures)
            if (v[0].rfind(s.name + "  (", 0) == 0) chosen = &s;
        if (!chosen) return;
        hmi::design::TypeViewOptions o;
        o.typeName = chosen->type;
        o.sample = chosen->name;
        o.popup = !(v.size() > 1 && v[1] == "Une vue");
        o.viewName = v.size() > 2 ? v[2] : std::string{};
        hmi::Id made = hmi::kNoId;
        const auto members = chosen->members;
        auto cmd = hmi::changeProject(current, "G\xC3\xA9n\xC3\xA9rer depuis " + chosen->type,
                                      [&](hmi::Project& p) { made = hmi::design::generateTypeView(p, o, members); });
        if (!cmd || made == hmi::kNoId) {
            status_->setTransientMessage(chosen->type + " : aucun membre \xC3\xA0 montrer (des structures dans la structure ?)", 8.0,
                                         ui::StatusBar::Severity::Warning);
            return;
        }
        app_.apply(std::move(cmd), false);
        openHmiView(made);
        const auto* made_view = current->project.view(made);
        // Les membres montres, et ceux laisses de cote (structures, tableaux dans
        // la structure : a poser a part, depuis la bibliotheque).
        std::size_t shown = 0;
        for (const auto& m : members) {
            const auto shape = hmi::design::shapeOf(m);
            shown += shape != hmi::design::VarShape::Other && shape != hmi::design::VarShape::Structure ? 1 : 0;
        }
        const std::size_t left = members.size() - shown;
        status_->setTransientMessage((made_view ? made_view->name : std::string("La vue")) + " g\xC3\xA9n\xC3\xA9r\xC3\xA9" "e depuis "
                                         + chosen->type + " : " + std::to_string(shown) + " membre(s) montr\xC3\xA9(s)"
                                         + (left ? ", " + std::to_string(left) + " laiss\xC3\xA9(s) de c\xC3\xB4t\xC3\xA9 (structures ou tableaux imbriqu\xC3\xA9s)"
                                                 : std::string{})
                                         + ", param\xC3\xA8tre Equipement := " + chosen->name + " (Ctrl+Z la retire)",
                                     8.0, ui::StatusBar::Severity::Success);
    });
}

// Lot 12 : "Creer un style" - son nom ; l'editeur fait la commande.
void MainAnalysisScreen::askHmiStyle(std::uint64_t viewId) {
    auto doc = app_.hmi();
    auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
    if (!doc || !editor) return;
    if (editor->canvas().selection().empty()) {
        status_->setTransientMessage("Choisis d'abord, dans la vue, l'objet dont le style est fait.", 6.0);
        return;
    }
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom du style", hmi::uniqueStyleName(doc->project, "Style"), "Titre, Bouton principal, Alarme...", false, {}});
    auto dialog = std::make_unique<FormDialog>("dialog.hmiStyle", "Cr\xC3\xA9" "er un style nomm\xC3\xA9",
        "L'apparence du premier objet choisi (couleurs, contour, police, taille, rayon) devient un style nomm\xC3\xA9 (IHM > Styles). "
        "Les objets choisis le citent (propri\xC3\xA9t\xC3\xA9 Style nomm\xC3\xA9) et suivront ses changements. Ctrl+Z le retire.",
        std::move(fields), "Cr\xC3\xA9" "er");
    app_.menus().ShowDialog(std::move(dialog), [this, viewId](const menu::DialogResult& r) {
        auto* ed = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
        if (!r.accepted() || !ed) return;
        const auto v = FormDialog::split(r.payload);
        if (v.empty() || v[0].empty()) return;
        std::string why;
        if (!ed->makeStyle(v[0], &why)) status_->setTransientMessage("Style non cr\xC3\xA9\xC3\xA9 : " + why, 8.0);
    });
}

void MainAnalysisScreen::askDeleteHmiView(std::uint64_t viewId) {
    auto doc = app_.hmi();
    const auto* view = doc ? doc->project.view(asId(viewId)) : nullptr;
    if (!view) return;
    // Un seul paragraphe, et le compte dit en francais : "vide", "son objet",
    // "ses 12 objets".
    const std::size_t n = view->objects.size();
    std::string message = "La vue \xC2\xAB " + view->name + " \xC2\xBB";
    if (n == 0)      message += " (vide) est retir\xC3\xA9" "e";
    else if (n == 1) message += " et son objet sont retir\xC3\xA9s";
    else             message += " et ses " + std::to_string(n) + " objets sont retir\xC3\xA9s";
    message += " du projet IHM. Ctrl+Z la rend ; enregistr\xC3\xA9" "e, son fichier part dans ihm/corbeille.";
    // Lot 10 : un symbole pose ailleurs.
    if (hmi::isSymbolView(*view))
        if (const auto uses = hmi::instancesOf(doc->project, view->name).size(); uses > 0)
            message += " C'est un symbole : ses " + std::to_string(uses) + " instance(s) montreront \xC2\xAB Symbole introuvable \xC2\xBB "
                       "(G\xC3\xA9n\xC3\xA9rer le signale).";
    if (doc->project.config.startView == viewId)
        message += " C'est la vue de d\xC3\xA9marrage : la premi\xC3\xA8re restante la remplace.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer la vue ?", message, MessageDialog::Icon::Question, "Supprimer"),
        [this, viewId](const menu::DialogResult& r) {
            auto current = app_.hmi();
            if (!r.accepted() || !current) return;
            if (auto cmd = hmiDeleteViewCommand(current, asId(viewId))) app_.apply(std::move(cmd), false);
        });
}

// ---- Ressources et fichiers externes : les dialogues des deux volets ----------
// Le volet est RETROUVE a la reponse, pas capture : l'onglet a pu etre ferme
// pendant que le dialogue etait ouvert. Les actions visent un identifiant :
// la selection a pu changer entre-temps.
void MainAnalysisScreen::askHmiImportResource() {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier", "", "C:\\Images\\logo.png", false, {}});
    fields.push_back({"Nom dans le projet", "", "vide : le nom du fichier", false, {}});
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiImportResource", "Importer une ressource",
            "Images : PNG, JPG, JPEG, BMP, SVG, ICO. Sons : WAV, MP3. Vid\xC3\xA9os : MP4, WEBM. Polices : TTF, OTF.\n"
            "Le contenu est copi\xC3\xA9 dans le projet (ihm/ressources) : le fichier d'origine peut ensuite bouger "
            "ou dispara\xC3\xAEtre. Glisser un fichier sur le volet l'importe aussi. Ctrl+Z retire l'import.",
            std::move(fields), "Importer"), 0,
            ui::openFile("Ressources IHM|*.png;*.jpg;*.jpeg;*.bmp;*.svg;*.ico;*.gif;*.wav;*.mp3;*.mp4;*.webm;*.ttf;*.otf"
                                                "|Images|*.png;*.jpg;*.jpeg;*.bmp;*.svg;*.ico;*.gif|Sons|*.wav;*.mp3"
                                                "|Vid\xC3\xA9os|*.mp4;*.webm|Polices|*.ttf;*.otf", app_.projectFolder(), "Importer une ressource")),
        [this](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || cleanPath(v[0]).empty()) return;
            std::string why;
            if (!pane->importFile(cleanPath(v[0]), v.size() > 1 ? v[1] : std::string{}, &why))
                status_->setTransientMessage("Import impossible : " + why, 8.0);
        });
}

// Lot 13 : Configuration > Langues.
void MainAnalysisScreen::askHmiAddLanguage() {
    auto doc = app_.hmi();
    if (!doc) return;
    std::vector<std::string> choices;
    for (const auto& k : hmi::knownLanguages())
        if (!doc->project.languages.find(k.code)) choices.push_back(k.name + " (" + k.code + ")");
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Langue", choices.empty() ? std::string{} : choices.front(), "English (en), ou un code : pt-br", false, choices});
    fields.push_back({"Nom affich\xC3\xA9", "", "vide : le nom dans sa langue (English, Deutsch...)", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiAddLanguage", "Ajouter une langue",
            "Les textes que lit l'op\xC3\xA9rateur se traduisent dans cette langue (ici, ou dans Excel). "
            "Sans traduction, un texte reste dans la langue du projet. Ctrl+Z retire la langue.",
            std::move(fields), "Ajouter"),
        [this](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiLanguagesPane*>(hmiTab("langues"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty()) return;
            std::string why;
            if (!pane->addLanguage(v[0], v.size() > 1 ? v[1] : std::string{}, &why))
                status_->setTransientMessage("Langue non ajout\xC3\xA9" "e : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiAddUnit() {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Variable", "", "Pression_Reseau, ou Armoires[].ana.PT1.mes (chaque case)", false, {}});
    fields.push_back({"Unit\xC3\xA9", "", "bar, \xC2\xB0" "C, %", false, {}});
    fields.push_back({"Format", "0.0", "0, 0.0, 0.00 ; vide : celui de chaque objet", false, {"", "0", "0.0", "0.00", "0.000"}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiAddUnit", "Unit\xC3\xA9 et format d'une variable",
            "Repris par chaque objet qui montre la variable (afficheur, champ de saisie, jauge, bargraphe, tableau de "
            "variables...) et par les trous des textes : {Pression} prend le format, {Pression:u} le format et l'unit\xC3\xA9. "
            "Un objet garde les siens s'il d\xC3\xA9" "coche \xC2\xAB Format de la variable \xC2\xBB. Ctrl+Z retire la ligne.",
            std::move(fields), "Ajouter"),
        [this](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiUnitsPane*>(hmiTab("unites"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty()) return;
            std::string why;
            if (!pane->addDisplay(v[0], v.size() > 1 ? v[1] : std::string{}, v.size() > 2 ? v[2] : std::string{}, &why))
                status_->setTransientMessage("Non ajout\xC3\xA9" "e : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiImportTranslations() {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Classeur", "", "exports\\traductions_IHM.xlsx", false, {}});
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiImportTranslations", "Importer les traductions",
            "Le classeur export\xC3\xA9 par Configuration > Langues, rempli : la colonne Texte (fr) rep\xC3\xA8re chaque texte, "
            "une colonne par langue (English (en)). Une langue nouvelle s'ajoute ; une case vid\xC3\xA9" "e retire la traduction. "
            "Ctrl+Z annule l'import.",
            std::move(fields), "Importer"), 0,
            ui::openFile("Classeurs Excel|*.xlsx;*.xlsm", ui::pathIn(app_.projectFolder(), "exports"), "Importer les traductions")),
        [this](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiLanguagesPane*>(hmiTab("langues"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || cleanPath(v[0]).empty()) return;
            std::string path = cleanPath(v[0]);
            // Un chemin relatif : depuis le dossier du projet (exports\...).
            const std::string folder = app_.projectFolder();
            if (!folder.empty() && !std::filesystem::path(path).is_absolute()) path = (std::filesystem::path(folder) / path).string();
            std::string why;
            if (!pane->importFile(path, &why)) status_->setTransientMessage("Import impossible : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiReplaceResource(std::uint64_t resourceId) {
    auto doc = app_.hmi();
    const auto* res = doc ? doc->project.resource(asId(resourceId)) : nullptr;
    if (!res) return;
    const auto uses = hmi::citations(doc->project, res->name);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier", res->origin, "un fichier du m\xC3\xAAme genre", false, {}});
    // Le bouton ... part du fichier d'origine (le champ), sinon du dossier du projet.
    app_.menus().ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.hmiReplaceResource", "Remplacer " + res->name,
            "Le nom reste \xC2\xAB " + res->name + " \xC2\xBB : "
            + (uses.empty() ? std::string("aucun objet ne la cite encore.")
                            : std::to_string(uses.size()) + " r\xC3\xA9" "f\xC3\xA9rence(s) montreront le nouveau contenu ("
                                  + citedBy(uses) + ").")
            + " Une image se remplace par une image, un son par un son. Ctrl+Z rend l'ancien contenu.",
            std::move(fields), "Remplacer"), 0,
            ui::openFile("Ressources IHM|*.png;*.jpg;*.jpeg;*.bmp;*.svg;*.ico;*.gif;*.wav;*.mp3;*.mp4;*.webm;*.ttf;*.otf"
                                                "|Images|*.png;*.jpg;*.jpeg;*.bmp;*.svg;*.ico;*.gif|Sons|*.wav;*.mp3"
                                                "|Vid\xC3\xA9os|*.mp4;*.webm|Polices|*.ttf;*.otf", app_.projectFolder(), "Remplacer " + res->name)),
        [this, resourceId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty() || cleanPath(v[0]).empty()) return;
            std::string why;
            if (!pane->replaceResource(asId(resourceId), cleanPath(v[0]), &why))
                status_->setTransientMessage("Remplacement impossible : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiRenameResource(std::uint64_t resourceId) {
    auto doc = app_.hmi();
    const auto* res = doc ? doc->project.resource(asId(resourceId)) : nullptr;
    if (!res) return;
    const auto uses = hmi::citations(doc->project, res->name);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", res->name, "avec son extension : vanne_ouverte.png", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiRenameResource", "Renommer " + res->name,
            uses.empty() ? std::string("Aucun objet ne cite cette ressource.")
                         : "Les " + std::to_string(uses.size()) + " r\xC3\xA9" "f\xC3\xA9rence(s) suivent, expressions comprises : "
                               + citedBy(uses) + ".",
            std::move(fields), "Renommer"),
        [this, resourceId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty()) return;
            std::string why;
            if (!pane->renameResource(asId(resourceId), v[0], &why))
                status_->setTransientMessage("Renommage refus\xC3\xA9 : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiDeleteResource(std::uint64_t resourceId) {
    auto doc = app_.hmi();
    const auto* res = doc ? doc->project.resource(asId(resourceId)) : nullptr;
    auto* pane = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"));
    if (!res || !pane) return;
    const auto uses = hmi::citations(doc->project, res->name);
    // Inutilisee : rien a perdre, Ctrl+Z la rend. Citee : on le dit d'abord.
    if (uses.empty()) { (void)pane->deleteResource(asId(resourceId)); return; }
    const std::string message = "\xC2\xAB " + res->name + " \xC2\xBB est cit\xC3\xA9" "e " + std::to_string(uses.size())
        + " fois : " + citedBy(uses) + ". Ces objets la montreront absente et G\xC3\xA9n\xC3\xA9rer le signalera. "
          "Ctrl+Z la rend.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer la ressource ?", message, MessageDialog::Icon::Question, "Supprimer"),
        [this, resourceId](const menu::DialogResult& r) {
            auto* current = dynamic_cast<HmiResourcesPane*>(hmiTab("ressources"));
            if (r.accepted() && current) (void)current->deleteResource(asId(resourceId));
        });
}

void MainAnalysisScreen::askHmiLinkFile(bool database) {
    std::vector<FormDialog::Field> fields;
    std::string title, text;
    if (database) {
        fields.push_back({"Nom", "", "le nom que citent les objets (vide : Base)", false, {}});
        fields.push_back({"Cha\xC3\xAEne de connexion", "", "Driver={SQL Server};Server=...;Database=...;Uid=...;Pwd=...", false, {}});
        fields.push_back({"Table", "", "facultatif", false, {}});
        title = "Lier une base externe";
        text = "La cha\xC3\xAEne de connexion est gard\xC3\xA9" "e telle quelle (le mot de passe est masqu\xC3\xA9 \xC3\xA0 "
               "l'affichage). Sans pilote install\xC3\xA9, son \xC3\xA9tat reste \xC2\xAB non v\xC3\xA9rifiable \xC2\xBB.";
    } else {
        fields.push_back({"Chemin", "", "C:\\Donnees\\consignes.csv", false, {}});
        fields.push_back({"Type", "Selon l'extension", "", false,
                          {"Selon l'extension", "Excel", "CSV", "TXT", "JSON", "XML", "SQLite",
                           "Document"}});   // ---- Lot API 8 : glisser de fichiers, 2e partie : tout autre fichier ----
        fields.push_back({"Nom", "", "vide : le nom du fichier sans extension", false, {}});
        fields.push_back({"Onglet / table", "", "Excel : l'onglet ; SQLite : la table ; vide : le premier", false, {}});
        title = "Lier un fichier externe";
        text = "Le fichier reste o\xC3\xB9 il est : on garde son chemin (relatif s'il est dans le dossier du projet), "
               "sa taille et sa date. Un Tableau le montre en choisissant ce fichier comme source. "
               "Glisser un fichier sur le volet le lie aussi.";
    }
    auto dialog = std::make_unique<FormDialog>(database ? "dialog.hmiLinkDatabase" : "dialog.hmiLinkFile", title, text,
                                               std::move(fields), "Lier");
    // Un fichier (pas une base) : le chemin se choisit aussi dans l'explorateur.
    if (!database)
        dialog->setFieldBrowse(0, ui::openFile("Fichiers externes|*.xlsx;*.xlsm;*.xls;*.csv;*.txt;*.log;*.json;*.xml;*.db;*.sqlite;*.sqlite3"
                                               "|Excel|*.xlsx;*.xlsm;*.xls|CSV|*.csv|Texte|*.txt;*.log|JSON|*.json|XML|*.xml"
                                               "|SQLite|*.db;*.sqlite;*.sqlite3",
                                               app_.projectFolder(), "Lier un fichier externe"));
    app_.menus().ShowDialog(
        std::move(dialog),
        [this, database](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiFilesPane*>(hmiTab("fichiers"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            std::string why;
            bool ok = false;
            if (database) {
                if (v.size() < 2) return;
                ok = pane->link(hmi::ExternalKind::Database, v[1], v[0], v.size() > 2 ? v[2] : std::string{}, &why);
            } else {
                if (v.empty()) return;
                const std::string path = cleanPath(v[0]);
                auto kind = v.size() > 1 ? hmi::externalKindFromKey(v[1]) : std::nullopt;
                if (!kind) kind = hmi::externalKindFromPath(path);
                if (!kind) {
                    status_->setTransientMessage("Type inconnu : choisis-le dans la liste (" + path + ")", 8.0);
                    return;
                }
                ok = pane->link(*kind, path, v.size() > 2 ? v[2] : std::string{}, v.size() > 3 ? v[3] : std::string{}, &why);
            }
            if (!ok) status_->setTransientMessage("Lien impossible : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiRemoveFile(std::uint64_t fileId) {
    auto doc = app_.hmi();
    const auto* f = doc ? doc->project.externalFile(asId(fileId)) : nullptr;
    auto* pane = dynamic_cast<HmiFilesPane*>(hmiTab("fichiers"));
    if (!f || !pane) return;
    const auto uses = hmi::externalCitations(doc->project, f->name);
    if (uses.empty()) { (void)pane->removeFile(asId(fileId)); return; }
    const std::string message = "\xC2\xAB " + f->name + " \xC2\xBB est la source de " + std::to_string(uses.size())
        + " objet(s) : " + citedBy(uses) + ". Ils n'afficheront plus rien et G\xC3\xA9n\xC3\xA9rer le signalera. "
          "Le fichier lui-m\xC3\xAAme n'est pas touch\xC3\xA9. Ctrl+Z rend le lien.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Retirer le lien ?", message, MessageDialog::Icon::Question, "Retirer"),
        [this, fileId](const menu::DialogResult& r) {
            auto* current = dynamic_cast<HmiFilesPane*>(hmiTab("fichiers"));
            if (r.accepted() && current) (void)current->removeFile(asId(fileId));
        });
}

void MainAnalysisScreen::askHmiChoosePart(std::uint64_t fileId) {
    auto doc = app_.hmi();
    const auto* f = doc ? doc->project.externalFile(asId(fileId)) : nullptr;
    if (!f) return;
    const auto data = hmiExternalData(*f, 1);
    if (data->parts.empty()) {
        status_->setTransientMessage(f->name + " : aucun onglet ni table lisible"
                                     + (data->error.empty() ? std::string{} : " (" + data->error + ")"), 8.0);
        return;
    }
    const bool excel = f->kind == hmi::ExternalKind::Excel;
    std::vector<FormDialog::Field> fields;
    fields.push_back({excel ? "Onglet" : "Table", data->partUsed.empty() ? data->parts.front() : data->partUsed, "", false,
                      data->parts});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiChoosePart", f->name + (excel ? " : l'onglet" : " : la table"),
            excel ? "Le Tableau li\xC3\xA9 \xC3\xA0 ce classeur montre les lignes de cet onglet ; la premi\xC3\xA8re ligne "
                    "remplie donne les titres des colonnes."
                  : "Le Tableau li\xC3\xA9 \xC3\xA0 cette base montre les lignes de cette table, colonnes dans l'ordre "
                    "du CREATE TABLE.",
            std::move(fields), "Choisir"),
        [this, fileId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiFilesPane*>(hmiTab("fichiers"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (!v.empty()) (void)pane->setPart(asId(fileId), v[0]);
        });
}

// ---- Programmation : scripts de vue, scripts generaux, variables IHM ---------
void MainAnalysisScreen::openHmiScripts(std::uint64_t viewId, std::uint64_t scriptId, int line) {
    auto doc = app_.hmi();
    if (!doc || !centre_) return;
    HmiScriptsPane* pane = nullptr;
    if (viewId == hmi::kNoId) {
        openHmiPane("scripts");
        pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"));
    } else {
        const auto* view = doc->project.view(asId(viewId));
        if (!view) return;
        const std::string key = "scripts:" + std::to_string(viewId);
        pane = dynamic_cast<HmiScriptsPane*>(hmiTab(key));
        if (!pane) {
            auto apply = [this](core::CommandPtr c) { if (c) app_.apply(std::move(c), /*refreshViews=*/false); };
            auto made = std::make_unique<HmiScriptsPane>("hmi.viewscripts." + std::to_string(viewId), doc, apply, asId(viewId));
            HmiScriptsPane::Hosts hosts;
            hosts.compile = [this] { openHmiPane("compiler"); };
            hosts.openIssue = [this](const hmi::Issue& i) { openHmiIssue(i); };   // 1.10 (chantier N)
            wireHmiBuildHosts(hosts);   // 1.11.13
            made->setHosts(std::move(hosts));
            wireAssist(*made, app_, [this] { return dynamic_cast<HmiSimulationPane*>(hmiTab("simulation")); });
            pane = made.get();
            const auto tab = centre_->addTab(TabControl::Tab{"Scripts \xC2\xB7 " + view->name, Icon::Code, true, false},
                                             std::move(made));
            hmiTabs_[key] = pane;
            centre_->setCurrentIndex(tab);
        } else {
            centre_->setCurrentIndex(static_cast<std::size_t>(centre_->indexOf(pane)));
        }
    }
    if (pane && scriptId != hmi::kNoId) pane->goTo(asId(scriptId), line);
}

// Double-clic sur un constat de Generer ou de Compiler : la source, au plus
// pres - la ligne d'un script, l'action dans l'onglet Actions, la variable IHM,
// sinon l'objet dans sa vue.
void MainAnalysisScreen::openHmiIssue(const hmi::Issue& issue) {
    auto doc = app_.hmi();
    if (!doc) return;
    if (openHmiSupervisionIssue(issue)) return;
    // 1.10 (decision 14 ; integration I2) : un constat d'un operateur (Compiler) - le
    // type (Types IHM, sa section Operateurs) ou le symbole (son sous-onglet
    // Operateurs), l'operateur choisi, son script a la ligne.
    if (issue.category == "Op\xC3\xA9rateur" && issue.item != hmi::kNoId) {
        hmi::OperatorOwner owner;
        if (!hmi::operatorById(doc->project, issue.item, &owner)) return;
        if (owner.kind == hmi::OperatorOwner::Kind::Type) {
            openHmiPane("scripts");
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
                pane->showTab(HmiScriptsPane::TabTypes);
                if (auto* types = pane->typesPane()) {
                    types->selectType(owner.id);
                    if (auto* ops = types->operators()) ops->goTo(issue.item, issue.line);
                }
            }
        } else if (owner.kind == hmi::OperatorOwner::Kind::Symbol) {
            openHmiView(owner.id);
            if (auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(owner.id)))) {
                if (auto* tabs = editor->symbolTabs()) tabs->setCurrent(HmiSymbolTabs::Operators);
                if (auto* ops = editor->symbolOperators()) ops->goTo(issue.item, issue.line);
            }
        }
        return;
    }
    if (issue.script != hmi::kNoId && doc->project.script(issue.script)) {
        const auto viewOf = doc->project.viewOfScript(issue.script);
        openHmiScripts(viewOf, issue.script, issue.line);
        // 1.10 (chantier N) : la colonne de la faute, ses caracteres selectionnes.
        if (issue.column > 0)
            if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab(viewOf == hmi::kNoId ? std::string("scripts") : "scripts:" + std::to_string(viewOf))))
                pane->goTo(issue.script, issue.line, issue.column, issue.length);
        return;
    }
    if (issue.category == "Fonction") {
        openHmiFunctions(issue.item, issue.line);
        if (issue.column > 0)       // 1.10 (chantier N) : la colonne de la faute
            if (auto* pane = dynamic_cast<HmiFunctionsPane*>(hmiTab("fonctions"))) pane->goTo(issue.item, issue.line, issue.column, issue.length);
        return;
    }
    if (issue.category == "Variable IHM") {
        openHmiPane("scripts");
        if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts")))
            if (const auto* v = doc->project.variable(issue.property)) pane->selectVariable(v->id);
        return;
    }
    // ---- Lot API 8 : finitions (un modele de vue garde dans le projet) ----
    //  Pas une vue du projet : rien a ouvrir ; ou le corriger se dit.
    if (issue.category == "Mod\xC3\xA8le de vue" && issue.view == hmi::kNoId) {
        if (status_)
            status_->setTransientMessage("Le mod\xC3\xA8le " + issue.property + " n'est pas une vue du projet : Nouvelle vue \xE2\x80\xBA "
                                         "Mod\xC3\xA8les du projet en cr\xC3\xA9" "e une, corrige-la, puis enregistre-la comme mod\xC3\xA8le "
                                         "(Ce projet, m\xC3\xAAme nom : il est remplac\xC3\xA9)",
                                         8.0);
        return;
    }
    // ---- fin Lot API 8 : finitions ----
    if (issue.view == hmi::kNoId) return;
    if (issue.category == "Action") {
        openHmiView(issue.view);
        if (auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(issue.view))))
            editor->showAction(issue.object, actionIndexOf(issue.property));
        return;
    }
    openHmiView(issue.view, -1, issue.object);
}

void MainAnalysisScreen::askHmiNewScript(hmi::ScriptLang lang) {
    auto doc = app_.hmi();
    if (!doc) return;
    const bool st = lang == hmi::ScriptLang::ST;
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", hmi::uniqueScriptName(doc->project, st ? "Script" : "Code"), "lettres, chiffres, _", false, {}});
    fields.push_back({"D\xC3\xA9" "clencheur", "Appel\xC3\xA9", "", false, scriptTriggerLabels()});
    fields.push_back({"P\xC3\xA9riode (ms)", "1000", "Cyclique : toutes les N ms (10 au moins)", false, {}});
    fields.push_back({"Expression surveill\xC3\xA9" "e", "", "Sur changement : Armoires[0].etat", false, {}});
    fields.push_back({"Description", "", "ce que fait le script", false, {}});
    const std::string langName(hmi::scriptLangKey(lang));
    auto dialog = std::make_unique<FormDialog>(
        "dialog.hmiNewScript", "Nouveau script " + langName,
        st ? std::string("Un script ST s'ex\xC3\xA9" "cute dans la simulation de l'IHM : au d\xC3\xA9marrage, cycliquement, "
                         "quand une expression change, ou quand on l'appelle (action \xC2\xAB Script g\xC3\xA9n\xC3\xA9ral \xC2\xBB, "
                         "IHM_APPELER('nom')). Il lit et \xC3\xA9" "crit les variables du programme et les variables IHM. "
                         "Ctrl+Z le retire.")
           : "Un script " + langName + " est \xC3\xA9" "dit\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9 (accolades, parenth\xC3\xA8ses, "
                 "cha\xC3\xAEnes, commentaires) mais le simulateur ne l'ex\xC3\xA9" "cute pas : il accompagne le projet pour "
                 "la cible. Ctrl+Z le retire.",
        std::move(fields), "Cr\xC3\xA9" "er");
    dialog->setFieldAssist(3, assist::fieldAssist(assist::sourcesFor(doc)));
    dialog->setRules([](const std::vector<std::string>& v, std::vector<FormDialog::FieldState>& state) {
        if (v.size() < 4 || state.size() < 4) return;
        const bool cyclic = v[1] == "Cyclique", change = v[1] == "Sur changement";
        state[2].enabled = cyclic;
        state[2].hint = cyclic ? std::string{} : std::string("d\xC3\xA9" "clencheur Cyclique seulement");
        state[3].enabled = change;
        state[3].hint = change ? std::string{} : std::string("d\xC3\xA9" "clencheur Sur changement seulement");
    });
    app_.menus().ShowDialog(std::move(dialog), [this, lang](const menu::DialogResult& r) {
        auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"));
        if (!r.accepted() || !pane) return;
        const auto v = FormDialog::split(r.payload);
        if (v.empty()) return;
        hmi::Script spec;
        spec.lang = lang;
        spec.name = v[0];
        spec.event = scriptTriggerKey(v.size() > 1 ? v[1] : std::string{});
        double ms = 1000;
        if (v.size() > 2 && !v[2].empty()) (void)hmi::parseNumber(v[2], ms);
        spec.periodMs = static_cast<int>(ms);
        if (v.size() > 3) spec.watch = v[3];
        if (v.size() > 4) spec.description = v[4];
        std::string why;
        if (pane->addScript(std::move(spec), &why) == hmi::kNoId)
            status_->setTransientMessage("Script refus\xC3\xA9 : " + why, 8.0);
    });
}

void MainAnalysisScreen::askHmiRenameScript(std::uint64_t scriptId) {
    auto doc = app_.hmi();
    const auto* sc = doc ? doc->project.script(asId(scriptId)) : nullptr;
    if (!sc) return;
    const auto callers = callersOf(doc->project, sc->name);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", sc->name, "lettres, chiffres, _", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiRenameScript", "Renommer " + sc->name,
            callers.empty()
                ? std::string("Rien ne l'appelle encore.")
                : "Appel\xC3\xA9 par : " + joinFew(callers) + ". Les actions \xC2\xAB Script g\xC3\xA9n\xC3\xA9ral \xC2\xBB suivent "
                  "le nouveau nom ; un IHM_APPELER('" + sc->name + "') \xC3\xA9" "crit dans un script est \xC3\xA0 reprendre "
                  "(Compiler le signale).",
            std::move(fields), "Renommer"),
        [this, scriptId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty()) return;
            std::string why;
            if (!pane->renameScript(asId(scriptId), v[0], &why))
                status_->setTransientMessage("Renommage refus\xC3\xA9 : " + why, 8.0);
        });
}

void MainAnalysisScreen::askHmiDeleteScript(std::uint64_t scriptId) {
    auto doc = app_.hmi();
    const auto* sc = doc ? doc->project.script(asId(scriptId)) : nullptr;
    if (!sc) return;
    const auto callers = callersOf(doc->project, sc->name);
    std::string message = "Le script \xC2\xAB " + sc->name + " \xC2\xBB (" + std::string(hmi::scriptLangKey(sc->lang)) + ") est retir\xC3\xA9 du projet IHM.";
    if (!callers.empty())
        message += " Il est appel\xC3\xA9 par " + joinFew(callers) + " : ces appels \xC3\xA9" "choueront et Compiler les signalera.";
    message += " Ctrl+Z le rend ; enregistr\xC3\xA9, son fichier part dans ihm/corbeille.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer le script ?", message, MessageDialog::Icon::Question, "Supprimer"),
        [this, scriptId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"));
            if (r.accepted() && pane) (void)pane->deleteScript(asId(scriptId));
        });
}

void MainAnalysisScreen::askHmiVariable(std::uint64_t variableId) {
    auto doc = app_.hmi();
    if (!doc) return;
    const hmi::Variable* var = nullptr;
    for (const auto& v : doc->project.programs.variables) if (v.id == asId(variableId)) var = &v;
    if (variableId != 0 && !var) return;
    // Lot 16 : les types IHM aussi, un tableau (ses bornes), un dossier.
    std::vector<std::string> types = hmi::typereg::Registry::build(doc->project)->names(hmi::typereg::UseVariable);   // 1.11.19
    hmi::types::Spec spec;
    const bool parsed = var && hmi::types::parseSpec(var->type, spec);
    // Un type que l'analyse ne decompose pas reste tel quel (jamais remplace par INT).
    std::string element = parsed ? spec.element : var ? var->type : std::string("INT");
    std::string bounds;
    if (parsed && spec.array()) {
        bounds = std::to_string(spec.low[0]) + ".." + std::to_string(spec.high[0]);
        if (spec.dims == 2) bounds += ", " + std::to_string(spec.low[1]) + ".." + std::to_string(spec.high[1]);
    }
    std::vector<std::string> folders{"(racine)"};
    for (const auto& f : hmi::types::allFolders(doc->project)) folders.push_back(f);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", var ? var->name : hmi::uniqueVariableName(doc->project, "Variable"), "lettres, chiffres, _", false, {}});
    fields.push_back({"Type", element, "", false, types});
    // L'ordre des lots precedents reste (Nom, Type, Valeur initiale, Description) : les
    // habitudes et les scripts d'avant remplissent les memes champs ; le lot 16 ajoute a la fin.
    fields.push_back({"Valeur initiale", var ? var->initial : std::string("0"), "0, TRUE, 2.5, 'texte', T#5s ; un tableau : 0 (toutes) ou 1, 2, 3", false, {}});
    fields.push_back({"Description", var ? var->description : std::string{}, "\xC3\xA0 quoi elle sert", false, {}});
    fields.push_back({"Tableau (facultatif)", bounds, "vide : une valeur ; 0..9 : 10 cases ; 0..3, 0..9 : deux dimensions", false, {}});
    fields.push_back({"Dossier", var && !var->folder.empty() ? var->folder : std::string("(racine)"), "", false, folders});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>(var ? "dialog.hmiEditVariable" : "dialog.hmiNewVariable",
            var ? "Modifier " + var->name : std::string("Nouvelle variable IHM"),
            "Une variable IHM vit dans l'IHM, pas dans l'automate : la vue courante, un compteur de clics, "
            "un choix d'op\xC3\xA9rateur. Les expressions, les actions et les scripts la lisent et l'\xC3\xA9" "crivent "
            "par son nom ; elle passe avant une variable du programme du m\xC3\xAAme nom. Ctrl+Z reprend.",
            std::move(fields), var ? "Enregistrer" : "Cr\xC3\xA9" "er"),
        [this, variableId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 2) return;
            // Lot 16 : le type et ses bornes font le type de la variable.
            std::string range = v.size() > 4 ? v[4] : std::string{};
            while (!range.empty() && (range.front() == ' ' || range.front() == '[')) range.erase(range.begin());
            while (!range.empty() && (range.back() == ' ' || range.back() == ']')) range.pop_back();
            const std::string type = range.empty() ? v[1] : "ARRAY[" + range + "] OF " + v[1];
            const std::string initial = v.size() > 2 ? v[2] : std::string{}, description = v.size() > 3 ? v[3] : std::string{};
            const std::string folder = v.size() > 5 && v[5] != "(racine)" ? v[5] : std::string{};
            std::string why;
            const hmi::Id made = variableId == 0 ? pane->addVariable(v[0], type, initial, description, &why) : asId(variableId);
            const bool ok = variableId == 0 ? made != hmi::kNoId : pane->updateVariable(asId(variableId), v[0], type, initial, description, &why);
            if (!ok) {
                status_->setTransientMessage("Variable refus\xC3\xA9" "e : " + why, 8.0);
                return;
            }
            if (auto* vars = pane->variablesPane()) (void)vars->setFolder(made, folder);
        });
}

// Lot 16 : Tableau... - les bornes (une ou deux dimensions) et le type des cases.
void MainAnalysisScreen::askHmiArrayType(const std::string& current, std::function<void(const std::string&)> done) {
    auto doc = app_.hmi();
    if (!doc) return;
    hmi::types::Spec spec;
    const bool parsed = hmi::types::parseSpec(current, spec);
    std::string bounds = "0..9";
    if (parsed && spec.array()) {
        bounds = std::to_string(spec.low[0]) + ".." + std::to_string(spec.high[0]);
        if (spec.dims == 2) bounds += ", " + std::to_string(spec.low[1]) + ".." + std::to_string(spec.high[1]);
    }
    std::vector<std::string> types = hmi::typereg::Registry::build(doc->project)->names(hmi::typereg::UseVariable);   // 1.11.19
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Bornes", bounds, "0..9 : 10 cases ; 1..4 ; -5..5 ; 0..3, 0..9 : deux dimensions (4 lignes, 10 colonnes)", false, {}});
    fields.push_back({"Type des cases", parsed ? spec.element : std::string("INT"), "", false, types});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiArray", "Tableau",
            "Un tableau a une ou deux dimensions, bornes libres : ARRAY[0..9] OF REAL, ARRAY[0..3, 0..9] OF INT, ARRAY[1..4] OF T_Four. "
            "Ses cases s'\xC3\xA9" "crivent Consignes[i] ou Matrice[i, j] ; ses propri\xC3\xA9t\xC3\xA9s Consignes.Length, .Low, .High, .Rows, "
            ".Columns, .Bytes, .Words se lisent partout (scripts, expressions, textes \xC3\xA0 trous).",
            std::move(fields), "Appliquer"),
        [done = std::move(done)](const menu::DialogResult& r) {
            if (!r.accepted() || !done) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 2) return;
            std::string range = v[0];
            while (!range.empty() && (range.front() == ' ' || range.front() == '[')) range.erase(range.begin());
            while (!range.empty() && (range.back() == ' ' || range.back() == ']')) range.pop_back();
            done(range.empty() ? v[1] : "ARRAY[" + range + "] OF " + v[1]);
        });
}

void MainAnalysisScreen::askHmiDeleteVariable(std::uint64_t variableId) {
    auto doc = app_.hmi();
    if (!doc) return;
    std::string name;
    for (const auto& v : doc->project.programs.variables) if (v.id == asId(variableId)) name = v.name;
    if (name.empty()) return;
    // Qui la lit ou l'ecrit : les scripts (leurs noms) et les actions (leur cible).
    std::vector<std::string> users;
    const auto same = [&](const std::string& n) { return upper(n) == upper(name); };
    for (const auto& sc : doc->project.programs.scripts)
        for (const auto& u : hmi::scriptNames(sc.body)) if (same(u.name)) { users.push_back(sc.name); break; }
    for (const auto& v : doc->project.views) {
        for (const auto& sc : v.scripts)
            for (const auto& u : hmi::scriptNames(sc.body)) if (same(u.name)) { users.push_back(v.name + "." + sc.event); break; }
        for (const auto& o : v.objects)
            for (const auto& a : o.actions) if (same(a.target)) { users.push_back(v.name + "/" + o.name); break; }
    }
    std::string message = "La variable IHM \xC2\xAB " + name + " \xC2\xBB est retir\xC3\xA9" "e.";
    if (!users.empty()) message += " Elle sert \xC3\xA0 " + joinFew(users) + " : Compiler et G\xC3\xA9n\xC3\xA9rer le signaleront.";
    message += " Ctrl+Z la rend.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer la variable ?", message, MessageDialog::Icon::Question, "Supprimer"),
        [this, variableId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"));
            if (r.accepted() && pane) (void)pane->deleteVariable(asId(variableId));
        });
}

// ---- lot 7 : les fonctions IHM ---------------------------------------------------
void MainAnalysisScreen::openHmiFunctions(std::uint64_t functionId, int line) {
    openHmiPane("fonctions");
    auto* pane = dynamic_cast<HmiFunctionsPane*>(hmiTab("fonctions"));
    if (pane && functionId != hmi::kNoId) pane->goTo(asId(functionId), line);
}

void MainAnalysisScreen::askHmiNewFunction() {
    auto doc = app_.hmi();
    if (!doc) return;
    std::vector<std::string> types{"Aucun"};          // 1.11.18 (lot 5) : une procedure (avant : "(aucun)")
    // 1.11.19 (lot 6) : le registre des types - la base, puis les structures et les enumerations
    // du projet (comme le retour dans les proprietes de la fonction ; avant : la base seule).
    for (const auto& t : hmi::typereg::Registry::build(doc->project)->names(hmi::typereg::UseDeclaration)) types.push_back(t);
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom", hmi::uniqueFunctionName(doc->project, "Fonction"), "lettres, chiffres, _", false, {}});
    fields.push_back({"Type de retour", "REAL", "", false, types});
    fields.push_back({"Description", "", "ce qu'elle calcule ou fait", false, {}});
    app_.menus().ShowDialog(
        std::make_unique<FormDialog>("dialog.hmiNewFunction", "Nouvelle fonction IHM",
            "Une fonction IHM s'appelle par son nom, comme une fonction de l'automate : Moyenne(a, b) dans un script, "
            "une action ou - si elle rend une valeur - une expression de vue. Ses param\xC3\xA8tres, ses locales et ses constantes "
            "se d\xC3\xA9" "clarent dans ses onglets ; Aucun en retour : une proc\xC3\xA9" "dure, appel\xC3\xA9" "e seule sur sa ligne. "
            "Le corps part d'un mod\xC3\xA8le \xC3\xA0 compl\xC3\xA9ter. Ctrl+Z la retire.",
            std::move(fields), "Cr\xC3\xA9" "er"),
        [this](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiFunctionsPane*>(hmiTab("fonctions"));
            if (!r.accepted() || !pane) return;
            const auto v = FormDialog::split(r.payload);
            if (v.empty()) return;
            std::string why;
            if (pane->addFunction(v[0], v.size() > 1 ? v[1] : std::string{}, v.size() > 2 ? v[2] : std::string{}, &why) == hmi::kNoId)
                status_->setTransientMessage("Fonction refus\xC3\xA9" "e : " + why, 8.0);
        });
}

// ---- 1.11.18 (refonte des scripts, lot 5) : MIGRER LES DECLARATIONS DU PROJET ----
//  Le plan d'abord (hmi::migrate::plan) : chaque code a migrer, coche, avec ce qu'il devient ;
//  ceux qui restent tels quels et pourquoi. Puis, comme l'import du MAST : le projet
//  enregistre s'il le faut, la version "Avant migration des declarations", la migration
//  (une commande : Ctrl+Z la reprend entiere).
void MainAnalysisScreen::askHmiMigrateDeclarations() {
    auto doc = app_.hmi();
    if (!doc) return;
    const auto plan = hmi::migrate::plan(doc->project);
    if (plan.migrated() == 0) {
        std::string why = plan.items.empty() ? std::string("aucun code ne d\xC3\xA9" "clare ses variables dans son texte.")
                                             : "rien ne peut l'\xC3\xAAtre (" + plan.items.front().place.label + " : " + plan.items.front().why + ").";
        status_->setTransientMessage("Migrer les d\xC3\xA9" "clarations : " + why, 8.0, ui::StatusBar::Severity::Warning);
        return;
    }
    HmiAskDialog::Spec spec;
    spec.id = "dialog.hmiMigrate";
    spec.title = "Migrer les d\xC3\xA9" "clarations du projet";
    spec.text = hmi::migrate::summary(plan, /*before=*/true) + ".\n\n"
                "Chaque code coch\xC3\xA9 perd ses blocs VAR \xE2\x80\xA6 END_VAR : ses constantes, variables et param\xC3\xA8tres "
                "passent dans ses onglets, leurs commentaires deviennent leur documentation. Le moteur lit les m\xC3\xAAmes "
                "d\xC3\xA9" "clarations : l'ex\xC3\xA9" "cution ne change pas.\n"
                "Avant : la version \xC2\xAB Avant migration des d\xC3\xA9" "clarations \xC2\xBB (le projet est d'abord enregistr\xC3\xA9). "
                "Ensuite, Ctrl+Z reprend la migration enti\xC3\xA8re.";
    spec.listTitle = "Les codes \xC3\xA0 migrer (d\xC3\xA9" "cochez ceux \xC3\xA0 laisser)";
    std::vector<hmi::migrate::Place> places;
    std::string skipped;
    std::size_t nSkipped = 0;
    for (const auto& it : plan.items) {
        if (!it.migrated) {
            skipped += (skipped.empty() ? "" : " ; ") + it.place.label + " (" + it.why + ")";
            ++nSkipped;
            continue;
        }
        std::string names, detail;
        for (const auto& d : it.decls) names += (names.empty() ? "" : ", ") + d.name;
        if (it.decls.empty()) {                    // une redefinition : ses parametres sont ceux de sa fonction
            for (const auto& n : it.notes)
                if (!n.attention) detail += (detail.empty() ? "" : " \xC2\xB7 ") + n.text;
            if (detail.empty()) detail = "un bloc vide, retir\xC3\xA9";
        } else {
            detail = std::to_string(it.decls.size()) + (it.decls.size() > 1 ? " d\xC3\xA9" "clarations : " : " d\xC3\xA9" "claration : ") + names;
        }
        if (it.comments) detail += " \xC2\xB7 " + std::to_string(it.comments) + " commentaire(s) en documentation";
        for (const auto& n : it.notes)
            if (n.attention) detail += " \xC2\xB7 attention : " + n.text;
        spec.items.push_back({it.place.label, detail, true});
        places.push_back(it.place);
    }
    if (nSkipped) spec.note = "Laiss\xC3\xA9" + std::string(nSkipped > 1 ? "s" : "") + " tel" + (nSkipped > 1 ? "s" : "") + " quel"
                              + (nSkipped > 1 ? "s" : "") + " : " + skipped;
    spec.confirm = "Migrer";
    spec.confirmLabel = [](const std::vector<bool>& items, const std::vector<bool>&, int) {
        const auto n = static_cast<std::size_t>(std::count(items.begin(), items.end(), true));
        return n == 0 ? std::string("Rien \xC3\xA0 migrer") : "Migrer " + std::to_string(n) + (n > 1 ? " codes" : " code");
    };
    spec.width = 780.f;
    app_.menus().ShowDialog(std::make_unique<HmiAskDialog>(std::move(spec)), [this, places](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto answer = HmiAskDialog::parse(r.payload);
        std::vector<hmi::migrate::Place> chosen;
        for (std::size_t i = 0; i < places.size(); ++i)
            if (i >= answer.items.size() || answer.items[i]) chosen.push_back(places[i]);
        if (chosen.empty()) return;
        const auto run = [this, chosen](const std::string& versionSaid) {
            auto d = app_.hmi();
            if (!d) return;
            const auto same = [](const hmi::migrate::Place& a, const hmi::migrate::Place& b) {
                return a.kind == b.kind && a.view == b.view && a.object == b.object && a.type == b.type && a.id == b.id && a.function == b.function;
            };
            const auto again = hmi::migrate::plan(d->project, [&](const hmi::migrate::Place& at) {
                return std::any_of(chosen.begin(), chosen.end(), [&](const hmi::migrate::Place& c) { return same(c, at); });
            });
            auto cmd = hmi::changeProject(d, "Migrer les d\xC3\xA9" "clarations (" + hmi::migrate::summary(again) + ")",
                                          [&](hmi::Project& q) { (void)hmi::migrate::apply(q, again); });
            if (cmd) app_.apply(std::move(cmd), /*refreshViews=*/false);
            if (auto* report = dynamic_cast<HmiReportPane*>(hmiTab("compiler"))) report->run();
            status_->setTransientMessage("Migr\xC3\xA9 : " + hmi::migrate::summary(again)
                                             + (versionSaid.empty() ? std::string{} : " \xC2\xB7 version " + versionSaid)
                                             + " \xC2\xB7 Ctrl+Z reprend la migration",
                                         10.0, ui::StatusBar::Severity::Success);
        };
        // La version d'abord, le projet enregistre s'il le faut (comme l'import du MAST).
        const std::string folder = app_.projectFolder();
        if (folder.empty()) {
            run({});
            return;
        }
        std::string failed, said;
        if (app_.commands().isModified())
            if (auto st = app_.saveProject(); !st) failed = st.error().context.empty() ? st.error().message() : st.error().context;
        if (failed.empty()) {
            if (auto store = hmi::ver::open(folder); !store) {
                failed = store.error().context.empty() ? store.error().message() : store.error().context;
            } else if (auto made = hmi::ver::create(*store, "Avant migration des d\xC3\xA9" "clarations", hmi::ver::State::Draft,
                                                    "Cr\xC3\xA9\xC3\xA9" "e avant la migration des blocs VAR (" + std::to_string(chosen.size())
                                                        + (chosen.size() > 1 ? " codes)" : " code)"),
                                                    hmi::ver::defaultAuthor());
                       !made) {
                failed = made.error().context.empty() ? made.error().message() : made.error().context;
            } else {
                said = made->label();
            }
            versionWatch_.checkedAt = -100.0;          // la barre du haut relit les versions
        }
        if (!failed.empty()) {
            auto ask = std::make_unique<MessageDialog>(
                "Version non cr\xC3\xA9\xC3\xA9" "e",
                "La version \xC2\xAB Avant migration des d\xC3\xA9" "clarations \xC2\xBB n'a pas pu \xC3\xAAtre cr\xC3\xA9\xC3\xA9" "e : " + failed
                    + "\n\nMigrer quand m\xC3\xAAme ? Ctrl+Z annulera la migration tant que le projet reste ouvert.",
                MessageDialog::Icon::Question, "Migrer quand m\xC3\xAAme");
            app_.menus().ShowDialog(std::move(ask), [run](const menu::DialogResult& rr) {
                if (rr.accepted()) run({});
            });
            return;
        }
        run(said);
    });
}

// ---- 1.11.19 (refonte des scripts, lot 6) : LE SELECTEUR DE TYPES ----
//  Le projet, les DDT du programme, les recents (le reglage hmi.types.recents) ; a la reponse :
//  le type choisi va a qui l'a demande (`done`), ou l'on mene a la definition du type.
void MainAnalysisScreen::askHmiType(HmiTypePicker::Spec spec, typepicker::Done done) {
    if (!spec.doc) spec.doc = app_.hmi();
    if (!spec.doc) return;
    if (!spec.plc.names) spec.plc = hmiparams::plcTypesOf(app_.project().get());
    spec.recents = app_.settings().getList("hmi.types.recents");
    app_.menus().ShowDialog(std::make_unique<HmiTypePicker>(std::move(spec)), [this, done](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const auto a = HmiTypePicker::parse(r.payload);
        if (!a.open.empty()) {
            openHmiTypeDefinition(a.open);
            return;
        }
        if (a.type.empty()) return;
        auto recents = app_.settings().getList("hmi.types.recents");
        HmiTypePicker::remember(recents, a.element);
        app_.settings().setList("hmi.types.recents", recents);
        if (done) done(a);
    });
}

void MainAnalysisScreen::openHmiTypeDefinition(const std::string& key) {
    if (key.rfind("ihm:", 0) == 0) {                     // un type IHM : l'onglet Types IHM, sur lui
        const auto id = static_cast<hmi::Id>(std::strtoull(key.c_str() + 4, nullptr, 10));
        openHmiPane("scripts");
        if (auto* pane = dynamic_cast<HmiScriptsPane*>(hmiTab("scripts"))) {
            pane->showTab(HmiScriptsPane::TabTypes);
            if (auto* types = pane->typesPane()) types->selectType(id);
        }
        return;
    }
    if (key.rfind("api:", 0) == 0) {                     // un DDT : les types derives de l'API
        const std::string wanted = key.substr(4);
        std::string name = wanted;
        if (const auto plc = hmiparams::plcTypesOf(app_.project().get()); plc.names)
            for (const auto& n : plc.names())
                if (hmi::typereg::comparable(n) == wanted) name = n;
        openApiPane("types");
        if (auto* frame = dynamic_cast<ApiFrame*>(apiTab("types")))
            if (auto* pane = dynamic_cast<DerivedTypesPane*>(&frame->content())) (void)pane->selectType(name);
    }
}

void MainAnalysisScreen::askHmiDeleteFunction(std::uint64_t functionId) {
    auto doc = app_.hmi();
    const auto* f = doc ? doc->project.function(asId(functionId)) : nullptr;
    if (!f) return;
    const auto callers = hmi::functionCallers(doc->project, f->name);
    std::string message = "La fonction \xC2\xAB " + hmi::functionSignature(*f) + " \xC2\xBB est retir\xC3\xA9" "e du projet IHM.";
    if (!callers.empty())
        message += " Elle est appel\xC3\xA9" "e par " + joinFew(callers) + " : ces appels \xC3\xA9" "choueront et Compiler les signalera.";
    message += " Ctrl+Z la rend ; enregistr\xC3\xA9, son fichier part dans ihm/corbeille.";
    app_.menus().ShowDialog(
        std::make_unique<MessageDialog>("Supprimer la fonction ?", message, MessageDialog::Icon::Question, "Supprimer"),
        [this, functionId](const menu::DialogResult& r) {
            auto* pane = dynamic_cast<HmiFunctionsPane*>(hmiTab("fonctions"));
            if (r.accepted() && pane) (void)pane->deleteFunction(asId(functionId));
        });
}

void MainAnalysisScreen::askHmiTryFunction(std::uint64_t functionId) {
    auto doc = app_.hmi();
    const auto* f = doc ? doc->project.function(asId(functionId)) : nullptr;
    auto* pane = dynamic_cast<HmiFunctionsPane*>(hmiTab("fonctions"));
    if (!f || !pane) return;
    const auto parts = hmi::splitDeclarations(hmi::decl::codeOf(*f), true);   // 1.11.18 (lot 3) : ses parametres du modele aussi
    const auto inputs = parts.inputs();
    // Sans parametre : l'essai part tout de suite.
    if (inputs.empty()) {
        (void)pane->tryFunction(f->id, {});
        return;
    }
    std::vector<FormDialog::Field> fields;
    for (const auto* in : inputs)
        fields.push_back({in->name + " : " + in->type, "",
                          "vide : " + (in->initial.empty() ? std::string("la valeur par d\xC3\xA9" "faut du type") : in->initial),
                          false, {}});
    auto dialog = std::make_unique<FormDialog>("dialog.hmiTryFunction", "Essayer " + hmi::functionSignature(*f),
        "Les arguments en ST : 2.5, TRUE, 'texte', T#5s, ou une expression (variables IHM \xC3\xA0 leur valeur initiale ; "
        "celles de l'automate lues dans la simulation si elle tourne). L'essai ne modifie ni le projet ni la simulation.",
        std::move(fields), "Essayer");
    for (std::size_t i = 0; i < inputs.size(); ++i) dialog->setFieldAssist(i, assist::fieldAssist(assist::sourcesFor(doc)));
    app_.menus().ShowDialog(std::move(dialog), [this, functionId](const menu::DialogResult& r) {
        auto* p = dynamic_cast<HmiFunctionsPane*>(hmiTab("fonctions"));
        if (!r.accepted() || !p) return;
        (void)p->tryFunction(asId(functionId), FormDialog::split(r.payload));
    });
}

// ---- lot 7 : F1 et le didacticiel -----------------------------------------------
namespace {

// Le volet d'un onglet IHM (sa cle), dit comme hmi::guide::topicForPlace l'attend.
std::string placeOfTab(const std::string& key) {
    if (key.rfind("vue:", 0) == 0) return "vue";
    if (key.rfind("scripts:", 0) == 0) return "scripts-vue";
    return key;
}

// Le volet d'un noeud de l'arbre IHM.
std::string placeOfNode(NK kind) {
    switch (kind) {
        case NK::HmiFolder:        return "ihm";
        case NK::HmiConfig:        return "config";
        case NK::HmiExternalFiles: return "fichiers";
        case NK::HmiResources:     return "ressources";
        case NK::HmiExchange:      return "echange";
        case NK::HmiViews: case NK::HmiViewFolder: case NK::HmiTemplateFolder:
            return "vues";
        case NK::HmiSymbolsFolder: return "symboles";   // lot 10
        case NK::HmiStyles:        return "styles";     // lot 12
        case NK::HmiFind:          return "rechercher";
        case NK::HmiTests:         return "essais";     // lot 13
        case NK::HmiLanguages:     return "langues";
        case NK::HmiUnits:         return "unites";
        case NK::HmiComm:          return "communication";   // lot 14
        case NK::HmiStation:       return "poste";
        case NK::HmiNotify:        return "notifications";
        case NK::HmiReports:       return "rapports";
        case NK::HmiWeb:           return "web";
        case NK::HmiView: case NK::HmiViewPart: case NK::HmiObject: case NK::HmiObjectItem:
        case NK::HmiAnimation: case NK::HmiLayer: case NK::HmiGroupEntry:
            return "vue";
        case NK::HmiViewScript:    return "scripts-vue";
        case NK::HmiSimulation:    return "simulation";
        case NK::HmiModbusTool:    return "outil";      // lot 15
        case NK::HmiGenerate:      return "generer";
        case NK::HmiCompile:       return "compiler";
        case NK::HmiScripts: case NK::HmiScriptsFolder: case NK::HmiGeneralScript:
            return "scripts";
        case NK::HmiVariablesFolder: case NK::HmiVariable: case NK::HmiUsedFolder: case NK::HmiUsedVariable:
        case NK::HmiVarFolder:
            return "variables";
        case NK::HmiTypesFolder: case NK::HmiTypeNode:
        case NK::HmiTypeValues: case NK::HmiTypeValue: case NK::HmiTypeOperators: case NK::HmiTypeOperator:   // 1.10
            return "types-ihm";
        case NK::HmiSysFolder: case NK::HmiSysDomain: case NK::HmiSysVar:
            return "variables-systeme";
        case NK::HmiInstFolder: case NK::HmiInstView: case NK::HmiInstViewVar: case NK::HmiInstObject: case NK::HmiInstVar:
        case NK::HmiInstViewInfo:
        case NK::HmiInstParam: case NK::HmiInstAlarmGroup: case NK::HmiInstGroupVar:          // 1.11.1 (decision 108)
        case NK::HmiInstAlarms: case NK::HmiInstAlarm: case NK::HmiInstAlarmVar:
            return "variables-instances";
        case NK::HmiFunctionsFolder: case NK::HmiFunction:
            return "fonctions";
        case NK::HmiAlarms: case NK::HmiAlarmGroup: case NK::HmiAlarm:
            return "alarmes";
        case NK::HmiRecipes: case NK::HmiRecipe: case NK::HmiRecord:
            return "recettes";
        case NK::HmiUsers: case NK::HmiUserGroup: case NK::HmiUser: case NK::HmiRoles: case NK::HmiRole:
            return "utilisateurs";
        case NK::HmiHistory:       return "historiques";
        default:                   return {};
    }
}

} // namespace

// 1.11 (T2, tranche 16, decision du chef) : l'aide de l'IHM, c'est le centre d'aide,
// au sujet demande (F1, Aller a..., le ? des volets). L'onglet "IHM . Aide" de la 1.10
// ne s'ouvre plus que pour le didacticiel de l'IHM (ses parcours, PHmiTrails).
void MainAnalysisScreen::openHmiHelp(const std::string& topic) {
    if (topic != "didacticiel") {
        app_.setHelpTopic(topic.empty() ? std::string("ihm") : topic);   // lu par HelpCenterScreen::onEnter (forF1)
        app_.menus().PushMenu("help.hmi");
        return;
    }
    openHmiPane("aide");
    if (auto* pane = dynamic_cast<HmiHelpPane*>(hmiTab("aide"))) pane->show(topic);
}

bool MainAnalysisScreen::openHmiHelpNow() {
    auto doc = app_.hmi();
    if (!doc || !centre_) return false;
    std::string topic;
    // 1. Un onglet de l'IHM au centre : le nom sous le curseur, sinon le volet.
    const auto* page = centre_->page(centre_->currentIndex());
    std::string key;
    for (const auto& [k, w] : hmiTabs_)
        if (w == page) key = k;
    // Lot macros 1 : l'onglet Macros a l'aide des macros (l'onglet Macros de
    // l'aide, a la page de la macro choisie) - pas celle de l'IHM.
    if (key == "macros") return false;
    if (!key.empty()) {
        // 1.11 (T2, tranche 16) : F1 sur l'onglet d'avant ouvre le centre, a son sujet.
        if (key == "aide") {
            const auto* pane = dynamic_cast<HmiHelpPane*>(hmiTab("aide"));
            app_.setHelpTopic(pane && !pane->current().empty() ? pane->current() : std::string("ihm"));
            app_.menus().PushMenu("help.hmi");
            return true;
        }
        // 1.11.2 (T2, decision 208) : IHM > Programmation generale, onglets Variables IHM et Types IHM :
        // leur sujet (variables-ihm, types-ihm, par la table de F1), et non le mot sous le curseur de
        // l'editeur des scripts, cache derriere eux (F1 y menait au sujet des scripts, ou a celui de ce mot).
        static_assert(HmiScriptsPane::TabVariables == 1 && HmiScriptsPane::TabTypes == 2,
                      "help::f1::placeOfProgrammingTab suit l'ordre des onglets de HmiScriptsPane");
        if (auto* general = dynamic_cast<HmiScriptsPane*>(hmiTab(key)); general && general->tabs()) {
            const auto tab = general->currentTab();
            if (tab == HmiScriptsPane::TabVariables || tab == HmiScriptsPane::TabTypes) {
                const auto own = help::f1::keyForPlace(help::f1::placeOfProgrammingTab(tab));
                openHmiHelp(own.empty() ? std::string("ihm") : own);
                return true;
            }
        }
        std::string word;
        if (auto* scripts = dynamic_cast<HmiScriptsPane*>(hmiTab(key))) word = scripts->editor().symbolAtCaret();
        if (auto* functions = dynamic_cast<HmiFunctionsPane*>(hmiTab(key))) word = functions->editor().symbolAtCaret();
        if (!word.empty()) {
            topic = hmi::guide::topicForWord(word);
            // Lot 9 : SYS.X, Vue.Objet.Propriete.
            if (const auto dot = word.find('.'); topic.empty() && dot != std::string::npos) {
                if (hmi::pub::isSysRoot(word.substr(0, dot))) topic = "variables-systeme";
                else if (hmi::pub::viewNamed(doc->project, word.substr(0, dot))) topic = "variables-instances";
            }
            if (topic.empty() && doc->project.functionByName(word)) topic = "fonctions";
            if (topic.empty() && doc->project.variable(word)) topic = "variables-ihm";
        }
        // Lot 8 : dans l'editeur, la tuile survolee de la bibliotheque, sinon
        // l'objet choisi : la page de cet objet, avec son exemple anime.
        if (topic.empty())
            if (auto* editor = dynamic_cast<HmiEditor*>(hmiTab(key))) {
                std::optional<hmi::Kind> kind;
                if (editor->palette().hovered()) kind = editor->palette().hoveredKind();
                if (!kind)
                    if (const auto* v = doc->project.view(editor->viewId()); v && !editor->canvas().selection().empty())
                        if (const auto* o = v->object(editor->canvas().selection().front())) kind = o->kind;
                if (kind)
                    if (const auto* t = hmi::guide::topicForKind(hmi::kindKey(*kind))) topic = t->key;
            }
        // Lot 9 : le volet des variables publiques, par son onglet.
        if (topic.empty())
            if (auto* pub = dynamic_cast<HmiPublicVarsPane*>(hmiTab(key)))
                topic = pub->currentTab() == HmiPublicVarsPane::System ? "variables-systeme" : "variables-instances";
        if (topic.empty()) {
            std::string place = placeOfTab(key);
            // Lot 15 : Configuration > Equipements - le sujet de l'onglet ouvert.
            if (auto* comm = dynamic_cast<HmiCommPane*>(hmiTab(key))) {
                switch (static_cast<int>(comm->tabs().currentIndex())) {
                    case HmiCommPane::TNetwork:    place = "reseau-pc"; break;
                    case HmiCommPane::TScanner:    place = "scanner-ip"; break;
                    case HmiCommPane::TEquipments: place = "equipements"; break;
                    case HmiCommPane::TState:      place = "equipements"; break;
                    default:                       break;      // l'automate du projet (lot 14)
                }
            }
            // L'editeur de vues : l'onglet de l'inspecteur dit ce qu'on regle.
            if (auto* editor = dynamic_cast<HmiEditor*>(hmiTab(key))) {
                const auto tab = editor->inspector().currentIndex();
                if (tab == 1) place = "actions";
                else if (tab == 2) place = "contenu";
            }
            topic = help::f1::keyForPlace(place);   // 1.11 (T2, tranche 16) : la table de F1, puis le guide
        }
        openHmiHelp(topic.empty() ? std::string("ihm") : topic);
        return true;
    }
    // 2. Au centre, rien ou un onglet de l'API (le lot API 2 a retire les
    // onglets fixes) : le noeud IHM choisi dans l'arbre.
    const bool apiOrNothing = centre_->tabCount() == 0 || isApiTab(centre_->page(centre_->currentIndex()));
    if (apiOrNothing && explorer_ && isHmiNode(explorer_->currentNode())) {
        topic = help::f1::keyForPlace(placeOfNode(ProjectTreeModel::kindOf(explorer_->currentNode())));
        openHmiHelp(topic.empty() ? std::string("ihm") : topic);
        return true;
    }
    return false;
}

// Lot 21 : la visite de l'IHM est devenue le premier parcours du didacticiel
// ("Decouvrir l'IHM", TutorialWorkspace.cpp), a jour des lots 10 a 21.
void MainAnalysisScreen::startHmiTutorial() { startTrail("decouvrir", false); }

// 1.10.2 (chantier D) : "DUPLIQUER..." - Ctrl+D, la barre d'outils. La fenetre
// rend un plan (hmi::dup) ; il se joue en UNE commande : Ctrl+Z retire tout.
void MainAnalysisScreen::askHmiDuplicate(std::uint64_t viewId) {
    // 1.10.4 : le nombre de copies de depart (0 : "Remplacer..." de Compiler), une fois.
    const int copies = std::exchange(nextDuplicateCopies(), 3);
    auto doc = app_.hmi();
    auto* editor = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
    const auto* view = doc ? doc->project.view(asId(viewId)) : nullptr;
    if (!doc || !editor || !view) return;
    const auto sel = editor->canvas().selection();
    if (sel.empty()) {
        status_->setTransientMessage("Choisis d'abord, dans la vue, les objets \xC3\xA0 dupliquer.", 6.0);
        return;
    }
    HmiDuplicateDialog::Spec spec;
    spec.doc = doc;
    spec.view = asId(viewId);
    spec.selection = sel;
    spec.copies = copies;                 // 1.10.4 : 0 - "Remplacer..." (l'original seulement)
    const auto sources = assist::sourcesFor(doc);
    spec.assist = assist::fieldAssist(sources);
    spec.exists = [doc, sources](std::string_view path) {
        const auto plc = sources.plc ? sources.plc() : nullptr;
        return assist::describe(doc->project, plc.get(), path).found;
    };
    spec.apply = [this, viewId](const hmi::dup::Plan& plan) {
        auto* ed = dynamic_cast<HmiEditor*>(hmiTab("vue:" + std::to_string(viewId)));
        if (!ed) return;
        auto res = std::make_shared<hmi::dup::Result>();
        const int n = plan.copies();
        const std::string label = n == 0 ? std::string("Remplacer les rep\xC3\xA8res")      // 1.10.4 : 0 copie
                                         : "Dupliquer (" + std::to_string(n) + (n > 1 ? " copies)" : " copie)");
        if (ed->canvas().edit(label, [plan, res](hmi::Project& p, hmi::View& v) { *res = hmi::dup::apply(p, v, plan); })) {
            // 1.11 (R111) : les copies sont retenues - apres Ctrl+Y, la selection y revient.
            if (!res->created.empty()) ed->canvas().selectCopies(res->created);
            status_->setTransientMessage(hmi::dup::summary(*res) + " (Ctrl+Z retire tout).", 8.0);
        }
    };
    app_.menus().ShowDialog(std::make_unique<HmiDuplicateDialog>(std::move(spec)), [](const menu::DialogResult&) {});
}

} // namespace app
