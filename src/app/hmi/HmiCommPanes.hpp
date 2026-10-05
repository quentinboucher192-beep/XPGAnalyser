// =============================================================================
//  app/hmi/HmiCommPanes.hpp - Configuration > Equipements (lot 14, lot 15)
// -----------------------------------------------------------------------------
//  L'AUTOMATE ET LES EQUIPEMENTS QUE LIT L'IHM, ET LE RESEAU DU PC.
//
//  Lot 14 : l'automate du projet (le simulateur de l'application, ou un automate
//  reel en Modbus TCP) - ses reglages a droite, sa table des adresses, son plan
//  d'adressage, Tester, le serveur de demonstration.
//
//  Lot 15 : les equipements du reseau (Modbus TCP/IP ou Ethernet TCP/IP), les
//  variables IHM qui s'y lient, le reseau du PC et le scanner IP. Huit onglets :
//
//    Reseau du PC         le schema : le PC, ses ports et leurs adresses, les
//                         equipements par reseau, ceux qu'aucun port ne joint ;
//                         un port choisi : son adresse se change a droite
//                         (verifications, Appliquer, Remettre l'adresse d'avant) ;
//    Scanner IP           tout un reseau (celui d'un port, ou une plage) : qui
//                         repond, sa MAC, son fabricant, ses ports, Modbus ;
//    Equipements          la liste (l'automate du projet en tete), la fiche de
//                         celui qu'on choisit, Tester, Ping ;
//    (1.11.4 : l'onglet Variables liees est retire ; une variable IHM liee se choisit
//                         et se regle dans le Plan d'adressage - adresse, mise a
//                         l'echelle, lecture seule - ou dans les Variables IHM)
//    Table des adresses   (l'automate du projet) les variables placees a la main ;
//    Plan d'adressage     tout ce que l'IHM peut lire : l'automate puis chaque
//                         equipement ; les refusees et pourquoi ; la qualite ;
//    Essai                le compte rendu de Tester ;
//    Etat des liaisons    chaque liaison en marche, rafraichie chaque seconde.
//
//  Lot 17 : un seul onglet Equipements (le vrai appareil, son jumeau simule,
//  ce que l'IHM utilise en simulation) ; supprimer un equipement (ses
//  variables IHM a cocher), le glisser sur un autre port ; les zones memoire
//  de chacun (a la main, un modele, ou detectees) ; l'onglet Carte memoire
//  (les variables case par case, les chevauchements, le scanner) ; les
//  esclaves virtuels (jumeaux) et le reseau simule (Reseau du PC : Reel |
//  Simule).
//
//  1.9 : l'ESCLAVE SIMULE LIE (le jumeau des lots 17-18) a sa ligne, sous son
//  vrai appareil, en retrait, au cadenas (il est lie : sa configuration suit
//  celle du vrai) ; la colonne L'IHM lit dit qui l'IHM lit (le vrai, l'esclave,
//  ou automatique) ; Montrer : tous, les vrais appareils, les esclaves simules,
//  ceux lus en simule. La fiche du vrai : Son esclave simule (la case Cloner en
//  esclave simule) et L'IHM lit (dans l'application, la bascule, le retour, le
//  poste) ; celle de l'esclave : une carte violette (Valeurs simulees, Carte
//  memoire, Outil Modbus, Detacher...), ce qu'il reprend du vrai (au cadenas), ce
//  qui est a lui, en ce moment. Une bascule (le vrai se tait, il revient) : le
//  message du volet et un avis en bas a droite. La case "Reperer les lectures
//  simulees" (station.simMarks) regle les reperes en marche.
//
//  La barre d'outils suit l'onglet. Chaque changement du projet est une
//  commande : Ctrl+Z.
// =============================================================================
#pragma once

#include "HmiAskDialog.hpp"
#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiIpScan.hpp"
#include "../../hmi/HmiNetInfo.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiZones.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ui { class StatusBar; }
namespace domain { class Project; }
namespace sim { class Runtime; }

namespace app {

class CommHost;
class EquipmentHost;
class HmiNetDiagram;
class HmiCheckList;
class HmiScanPage;
class HmiMemoryMap;
class HmiTwinValues;
class TwinValuesController;
struct NetDiagram;
namespace simmark { class Indicator; }

// 1.9 : LA CARTE VIOLETTE en tete de la fiche d'un esclave simule lie - le
// cadenas, "Esclave simule lie a <vrai>", ce qui suit le vrai et ce qui est a
// lui ; ses boutons : Valeurs simulees, Carte memoire, Outil Modbus, Detacher...
class HmiSlaveCard final : public ui::Widget {
public:
    enum Action : int { Values = 0, Map = 1, Tool = 2, Detach = 3 };
    explicit HmiSlaveCard(std::string id);
    void setEquipment(std::string realName);
    [[nodiscard]] const std::string& equipment() const noexcept { return equipment_; }
    [[nodiscard]] std::string title() const;
    [[nodiscard]] static std::string body();
    // Sa hauteur a cette largeur (le texte passe a la ligne, les boutons aussi).
    [[nodiscard]] float heightFor(float width) const;
    [[nodiscard]] ui::Button& button(int action) { return *buttons_[static_cast<std::size_t>(action)]; }
    const core::SignalPtr<int> clicked = core::Signal<int>::create();
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
private:
    std::string               equipment_;
    std::array<ui::Button*, 4> buttons_{};
    core::ConnectionScope     links_;
};

class HmiCommPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiCommPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    ~HmiCommPane() override;
    void refresh();

    // Les onglets (lot 15).
    // Lot 17 : la Carte memoire, apres le plan d'adressage ; lot 18 : les Valeurs simulees, apres elle.
    // 1.11.4 : sans Variables liees (une variable liee se choisit dans le Plan d'adressage).
    enum Tab : int { TNetwork = 0, TScanner, TEquipments, TTable, TPlan, TMap, TValues, TTest, TState };

    // ---- l'automate du projet (lot 14) ----------------------------------------
    // Un reglage : "mode" (simulateur, modbus), "hote", "port", "esclave",
    // "delai", "periode", "reessai", "ordre" (faible, fort), "mots", "bits",
    // "ecart", "ecritures", "mauvaise", "demo", "demo_port", "demo_reseau".
    bool setSetting(const std::string& key, const std::string& value, std::string* why = nullptr);
    // Une ligne de la table ; `address` vide : la prochaine adresse libre.
    bool addAddress(const std::string& variable, const std::string& address = {}, const std::string& type = {},
                    std::string* why = nullptr);
    bool removeAddress(const std::string& variable, std::string* why = nullptr);
    // "variable", "adresse", "type", "lecture_seule", "description".
    bool setAddressField(const std::string& variable, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Les variables de l'automate que l'IHM utilise sans adresse : une ligne chacune,
    // a une adresse libre. Rend combien.
    std::size_t propose();
    // Tester l'automate : le compte rendu dans l'onglet Essai.
    bool test();
    // Le plan d'adressage en Excel (exports/).
    bool exportPlan(std::string* where = nullptr);

    [[nodiscard]] std::string selectedAddress() const;
    void selectAddress(const std::string& variable);

    // ---- les equipements (lot 15) -----------------------------------------------
    // L'automate du projet dans la liste des equipements.
    static constexpr const char* kPlcKey = "\x01" "automate";
    // Ajouter un equipement (nom vide : "Equipement 1"...) et le choisir ; rend son nom (vide : refuse).
    std::string addEquipment(const std::string& name = {}, hmi::EquipmentType type = hmi::EquipmentType::ModbusTcp,
                             const std::string& host = {}, int port = 0, std::string* why = nullptr);
    bool removeEquipment(const std::string& name, std::string* why = nullptr);
    // "nom", "type", "hote", "port", "esclave", "delai", "periode", "reessai", "ordre",
    // "mots", "bits", "ecart", "ecritures", "mauvaise", "ping", "simule", "actif", "description".
    // 1.9 : "esclave_simule" (cloner en esclave simule ; "jumeau" aussi), "lecture_appli"
    // (vrai | esclave | auto, ou leurs libelles ; "en_simulation" aussi), "bascule_apres"
    // (1 a 3600 s), "bascule_retour" (oui : des qu'il repond ; non : l'esclave jusqu'au
    // redemarrage), "lecture_poste" (vrai | auto | admin, ou leurs libelles) ; les reglages
    // de l'esclave : "jumeau_..." ou "esclave_..." (nom, marche, depart, delai, gigue,
    // repond, exception, ping, visible, port_visible, hote).
    bool setEquipmentField(const std::string& name, const std::string& key, const std::string& value, std::string* why = nullptr);
    [[nodiscard]] std::string selectedEquipment() const { return equipment_; }
    void selectEquipment(const std::string& name);

    // ---- 1.9 : l'esclave simule lie, a l'ecran -------------------------------------
    // Les icones du tableau des equipements que la table ne connait pas (dessinees
    // par le volet) : la fiole, la ligne d'un esclave lie (le coude, le cadenas, la
    // fiole - son retrait), le point violet "lu par l'IHM".
    static constexpr int   kIconFlask = 1, kIconSlave = 2, kIconDot = 3;
    static constexpr float kSlaveIndent = 34.f;
    // La ligne de l'esclave simule de `equipment` (sa fiche a droite) ; un equipement
    // sans esclave lie : sa ligne a lui.
    void selectSlave(const std::string& equipment);
    [[nodiscard]] bool slaveSelected() const noexcept { return slaveRow_; }
    // Les lignes du tableau Equipements, dans l'ordre : l'equipement (kPlcKey :
    // l'automate du projet), et si c'est la ligne de son esclave simule.
    struct EquipRow {
        std::string equipment;
        bool        slave{false};
    };
    [[nodiscard]] const std::vector<EquipRow>& equipmentRows() const noexcept { return equipRows_; }
    // Le resume au-dessus du tableau ("7 vrais appareils (5 joignables) - ...").
    [[nodiscard]] const std::string& equipmentSummary() const noexcept { return equipSummary_; }
    [[nodiscard]] const std::string& equipmentFilter() const noexcept { return equipFilter_; }
    // DETACHER : un nouvel equipement seulement simule (le nom de l'esclave, sans
    // collision) reprend sa configuration, ses mouvements, ses forcages et sa memoire ;
    // le vrai perd son esclave et garde ses variables liees. Une commande (Ctrl+Z).
    // Rend le nom du nouvel equipement (vide : refuse). askDetachSlave : la question
    // d'abord (sans dialogue - les tests : faux).
    std::string detachSlave(const std::string& equipment, std::string* why = nullptr);
    bool askDetachSlave(const std::string& equipment);
    // Les reperes des lectures simulees (sur le poste et dans Simuler l'IHM) : la case
    // "Reperer les lectures simulees", une commande.
    bool setSimMarks(bool on, std::string* why = nullptr);
    [[nodiscard]] ui::Checkbox& simMarksBox() noexcept { return *simMarksBox_; }
    // A droite de la barre d'etat : "2 equipements lus en simule" (vide : aucun).
    [[nodiscard]] std::string simulatedReadsText() const;
    // La carte violette de la fiche de l'esclave (ses boutons), la note sous la fiche.
    [[nodiscard]] HmiSlaveCard& slaveCard() noexcept { return *slaveCard_; }
    [[nodiscard]] const std::string& sheetNote() const noexcept { return sheetNote_; }
    // L'avis d'une bascule (en bas a droite, quelques secondes) : son titre ("14:02:31 -
    // Variateur ATV320"), son texte ; vide : aucun.
    [[nodiscard]] std::pair<std::string, std::string> switchNotice() const { return {toast_.title, toast_.text}; }
    // Tester un equipement : connexion, identification, lecture de ses variables
    // (Modbus) ; ping et port (Ethernet). Le compte rendu dans Essai.
    bool testEquipment(const std::string& name);
    void testAll();

    // ---- les variables liees (lot 15) --------------------------------------------
    // Lier une variable IHM a un equipement (creee si elle n'existe pas ; type
    // vide : INT) ; adresse vide : la prochaine libre de l'equipement.
    bool bindVariable(const std::string& variable, const std::string& equipment, const std::string& address = {},
                      const std::string& type = {}, std::string* why = nullptr);
    bool unbindVariable(const std::string& variable, std::string* why = nullptr);
    // "equipement", "adresse", "type", "lecture_seule", "brut_min", "brut_max",
    // "echelle_min", "echelle_max", "type_brut", "description".
    bool setBoundField(const std::string& variable, const std::string& key, const std::string& value, std::string* why = nullptr);
    [[nodiscard]] std::string selectedBound() const { return bound_; }
    // 1.11.4 : la montre dans le Plan d'adressage (son onglet, sa ligne, son formulaire).
    void selectBound(const std::string& variable);

    // ---- lot 16 : le plan d'adressage, range par liaison ---------------------------
    //  Un groupe par liaison : l'automate du projet, puis chaque equipement Modbus
    //  (son adresse, son esclave, son etat) ; un groupe se replie. Les lignes d'un
    //  equipement sont des variables IHM : la pastille IHM, l'origine "Variable IHM
    //  > dossier", une infobulle qui le dit ; une structure se deplie en membres.
    //  "Plan de" : tous, l'automate seul, ou un equipement.
    struct PlanRow {
        enum class Kind : std::uint8_t { Group, Plc, Ihm, Member, Refused } kind{Kind::Plc};
        std::string group;          // kPlcKey, ou le nom de l'equipement
        std::string name;           // la variable (ou le chemin de la case)
        std::string root;           // Ihm, Member : la variable IHM
        std::string tip;            // l'infobulle
    };
    [[nodiscard]] const std::vector<PlanRow>& planRows() const noexcept { return planRows_; }
    [[nodiscard]] const PlanRow* selectedPlanRow() const;
    void setPlanFilter(const std::string& group);          // "" : tous ; kPlcKey ; un equipement
    void setPlanGroupOpen(const std::string& group, bool open);
    void setPlanVariableOpen(const std::string& variable, bool open);
    [[nodiscard]] ui::DropDown& planFilter() noexcept { return *planFilterBox_; }

    // ---- le reseau du PC (lot 15) ----------------------------------------------------
    void selectPort(const std::string& adapter);
    [[nodiscard]] std::string selectedPort() const { return port_; }
    // Le reglage prepare du port choisi : "mode" (fixe, dhcp), "ip", "masque", "passerelle", "dns".
    bool setPortField(const std::string& key, const std::string& value, std::string* why = nullptr);
    bool applyPort(std::string* why = nullptr);
    void cancelPort();
    bool restorePort(std::string* why = nullptr);
    // "Donner au port ... l'adresse ..." d'un equipement hors reseau (son rang).
    bool giveOutside(int index, std::string* why = nullptr);
    void refreshNetwork();
    // Ce que disent les verifications du reglage prepare (1 bon, 2 attention, 3 mauvais, 4 info, 0 en cours).
    [[nodiscard]] std::vector<std::pair<int, std::string>> portChecks() const;

    // ---- le scanner IP (lot 15) -------------------------------------------------------
    // "port" (le nom d'un port), "plage", "ports", "delai", "noms", "modbus".
    bool setScanField(const std::string& key, const std::string& value, std::string* why = nullptr);
    bool startScan(std::string* why = nullptr);
    void stopScan();
    [[nodiscard]] bool scanning() const;
    [[nodiscard]] std::vector<hmi::ipscan::Host> scanResults() const;
    // Ajouter comme equipement l'adresse trouvee (Modbus si le 502 repond).
    bool addScanned(const std::string& ip, std::string* why = nullptr);
    bool exportScan(std::string* where = nullptr);
    [[nodiscard]] std::string selectedScanned() const;

    // ---- lot 17 : supprimer, deplacer -------------------------------------------------
    // Supprimer un equipement : les variables de `drop` sont supprimees (et ce qui
    // les cite est signale), les autres restent, deliees ; son jumeau part avec lui
    // (dropTwin faux et il en a un : il reste, seulement simule). L'automate du
    // projet ne se supprime pas. Ctrl+Z rend tout.
    bool deleteEquipment(const std::string& name, const std::vector<std::string>& drop = {}, bool dropTwin = true, std::string* why = nullptr);
    // Le dialogue (les variables a cocher) ; sans dialogue (les tests) : rien.
    bool askDeleteEquipment(const std::string& name);
    // Glisse sur un port : changer son adresse (une libre du reseau du port,
    // `address` vide) ou garder la sienne (le port recoit une adresse de plus,
    // dans son reseau - droits d'administrateur).
    bool moveToPort(const std::string& equipment, const std::string& port, bool changeAddress, const std::string& address = {},
                    std::string* why = nullptr);
    bool askMoveToPort(const std::string& equipment, const std::string& port);
    // L'adresse que recevrait l'equipement dans le reseau du port (vide : aucune).
    [[nodiscard]] std::string freeAddressOn(const std::string& port, const std::string& equipment) const;

    // ---- lot 17 : les zones memoire -----------------------------------------------------
    // "4x" "0-99; 1000-1049" ; "aucune" : la table n'existe pas.
    bool setZones(const std::string& equipment, hmi::MemTable table, const std::string& text, std::string* why = nullptr);
    // Un modele : "variables", "automate", "comme:<nom>", "tout", "registres", "aucune", "non" (plus de zones).
    bool applyZonePreset(const std::string& equipment, const std::string& key, std::string* why = nullptr);
    // Detecter : lire (seulement lire) l'equipement (ou son jumeau) ; le rapport,
    // puis Appliquer. `ask` : le dialogue en direct.
    bool detectZones(const std::string& equipment, bool twin = false, bool ask = true, std::string* why = nullptr);
    [[nodiscard]] bool detecting() const;
    [[nodiscard]] std::optional<hmi::zones::DetectReport> detectReport() const;
    bool applyDetected(std::string* why = nullptr);
    [[nodiscard]] std::string detectText() const;           // ce que dit le dialogue

    // ---- lot 17 : la carte memoire --------------------------------------------------------
    // "Carte de" : un equipement (kPlcKey : l'automate du projet), son jumeau ou le vrai.
    void showMap(const std::string& equipment, bool twin = false);
    [[nodiscard]] const std::string& mapEquipment() const noexcept { return mapEquip_; }
    [[nodiscard]] bool mapTwin() const noexcept { return mapTwin_; }
    // "montrer" (tout, variables, problemes, activite), "valeurs" (decimal, signe, hexa),
    // "replier" (oui, non), "chercher", "par_ligne" (10, 16).
    bool setMapOption(const std::string& key, const std::string& value);
    bool startMapScan(std::string* why = nullptr);
    void stopMapScan();
    [[nodiscard]] bool mapScanning() const;
    void selectCell(hmi::MemTable table, std::uint32_t offset);
    [[nodiscard]] std::optional<std::pair<hmi::MemTable, std::uint32_t>> selectedCell() const;
    // Creer une variable IHM a la case choisie (le dialogue ; sans : Mesure_40053, du type vu).
    bool createVariableHere(std::string* why = nullptr);
    // Une adresse libre (dans les zones, sans chevauchement) pour la variable.
    bool proposeFreeAddress(const std::string& variable, std::string* why = nullptr);
    // Deplacer une variable IHM (sa racine) de `delta` cases.
    bool moveVariable(const std::string& variable, int delta, std::string* why = nullptr);
    bool exportMap(std::string* where = nullptr);
    [[nodiscard]] HmiMemoryMap& memoryMap() noexcept { return *map_; }
    [[nodiscard]] hmi::zones::MemoryMap currentMap() const;

    // ---- lot 17 : les jumeaux (esclaves virtuels) ------------------------------------------
    bool createTwin(const std::string& equipment, std::string* why = nullptr);
    // Un equipement seulement simule (pas encore d'appareil) ; rend son nom.
    std::string addVirtualSlave(const std::string& name = {}, std::string* why = nullptr);
    void runTwins(bool on);
    bool restartTwin(const std::string& equipment, std::string* why = nullptr);
    // Ecrire dans la memoire du jumeau (une valeur, un nombre ; "0x1F" ; TRUE).
    bool writeTwin(const std::string& equipment, hmi::MemTable table, std::uint32_t offset, const std::string& value, std::string* why = nullptr);
    // Le comportement d'une case : "genre" (constante, sinus... ; aucun : le retirer),
    // "type", "a", "b", "periode", "retard", "source".
    bool setBehavior(const std::string& equipment, const std::string& address, const std::string& key, const std::string& value,
                     std::string* why = nullptr);
    bool saveTwinMemory(const std::string& equipment, std::string* why = nullptr);
    bool exportTwinMemory(const std::string& equipment, std::string* where = nullptr);
    bool importTwinMemory(const std::string& equipment, const std::string& csv, std::string* why = nullptr);
    // Les ports du PC simule.
    std::string addSimPort(const std::string& ip = {}, int prefix = 24, std::string* why = nullptr);
    bool copyPcNetwork(std::string* why = nullptr);
    bool removeSimPort(const std::string& name, std::string* why = nullptr);
    // "nom", "ip", "masque".
    bool setSimPortField(const std::string& name, const std::string& key, const std::string& value, std::string* why = nullptr);
    void selectSimPort(const std::string& name);
    // Reseau du PC : le reel ou le simule.
    void setNetworkView(bool simulated);
    [[nodiscard]] bool networkSimulated() const noexcept { return simView_; }
    // Equipements, Montrer : "tous", "vrais" (les vrais appareils), "esclaves" (les
    // esclaves simules : les lignes des esclaves lies et les seulement simules),
    // "lus" (ceux que l'IHM lit en simule). Les cles d'avant : "jumeaux", "simules"
    // (les esclaves).
    void setEquipmentFilter(const std::string& filter);

    // ---- lot 18 : les valeurs simulees ----------------------------------------------------
    //  L'onglet "Valeurs simulees" : ce que les jumeaux font tout seuls (animer, la
    //  zone de mouvement, forcer), ligne par ligne ; le controleur fait les commandes.
    [[nodiscard]] HmiTwinValues&        values() noexcept { return *values_; }
    [[nodiscard]] TwinValuesController& valuesController() noexcept { return *valuesCtl_; }
    // Animer (ou non) la ligne d'une variable ou d'une case ; la zone ; forcer (vide : deforcer).
    bool animate(const std::string& equipment, const std::string& address, bool on, std::string* why = nullptr);
    bool setBand(const std::string& equipment, const std::string& address, double lo, double hi, std::string* why = nullptr);
    bool force(const std::string& equipment, const std::string& address, const std::string& raw, std::string* why = nullptr);
    bool unforceAll(std::string* why = nullptr);
    void showValues(const std::string& equipment = {}, const std::string& address = {});

    struct Hosts {
        std::function<const domain::Project*()>                        plc;        // le programme (nul : aucun)
        std::function<sim::Runtime*()>                                 runtime;    // le simulateur (les types)
        std::function<CommHost*()>                                     comm;       // la liaison, le serveur de demonstration
        std::function<bool(const hmi::ExportRequest&, std::string*)>   exportFile;
        std::function<EquipmentHost*()>                                equipments; // lot 15 : liaisons des equipements, pings, ports du PC
        // Lot 15 : l'outil Modbus (tab : "lecture", "ping"...) vers cette cible.
        std::function<void(const std::string& host, int port, int unit, const std::string& tab)> openTool;
        // Lot 15 : le dialogue "Lier une variable" (nom, type, adresse, equipement).
        std::function<void(const std::string& equipment)>             askBind;
        // Lot 16 : ouvrir une variable IHM (un chemin : Four1.Heures) dans Programmation
        // generale > Variables IHM - le double-clic d'une ligne du plan.
        std::function<void(const std::string& path)>                  openVariable;
        // Lot 17 : un dialogue qui demande avant d'agir (supprimer, deplacer, detecter).
        std::function<void(HmiAskDialog::Spec, std::function<void(bool, const HmiAskDialog::Answer&)>)> ask;
        // Lot 17 : "Creer une variable IHM ici" - le dialogue Lier, prerempli.
        std::function<void(const std::string& equipment, const std::string& address, const std::string& type)> askBindAt;
        // Lot 17 : importer un fichier CSV dans la memoire du jumeau (le dialogue du chemin).
        std::function<void(const std::string& equipment)>             askImportTwin;
    };
    void setHosts(Hosts h);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     addresses() noexcept { return *table_; }
    [[nodiscard]] ui::TableView&     plan() noexcept { return *plan_; }
    [[nodiscard]] ui::TableView&     report() noexcept { return *report_; }
    [[nodiscard]] ui::TableView&     state() noexcept { return *state_; }
    [[nodiscard]] ui::TableView&     equipmentsTable() noexcept { return *equipTable_; }
    [[nodiscard]] ui::TableView&     scanTable() noexcept;
    [[nodiscard]] HmiNetDiagram&     diagram() noexcept { return *diagram_; }
    [[nodiscard]] ui::TabControl&    tabs() noexcept { return *tabs_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::vector<std::string>& reportLines() const noexcept { return reportLines_; }
    [[nodiscard]] ui::DropDown&      mapTarget() noexcept { return *mapTargetBox_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    void onPaintOverlay(const ui::PaintContext&) override;   // 1.9 : l'avis d'une bascule
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    enum class Side : std::uint8_t { Plc, Equipment, Bound, Port, Scan, Map, SimPort, Values };
    // 1.9 : l'esclave simule lie - la fiche du vrai (Son esclave simule, L'IHM lit),
    // celle de l'esclave (Repris du vrai, L'esclave simule, En ce moment) ; ce que
    // l'IHM lit maintenant ; les bascules (le message, l'avis).
    void rebuildCloneProperties(std::vector<ui::PropertyGrid::Category>& cats, const hmi::Equipment& e);
    void rebuildReadProperties(std::vector<ui::PropertyGrid::Category>& cats, const hmi::Equipment& e);
    void rebuildSlaveProperties(std::vector<ui::PropertyGrid::Category>& cats, const hmi::Equipment& e);
    void rebuildOwnSlaveProperties(std::vector<ui::PropertyGrid::Category>& cats, const hmi::Equipment& e, const std::string& title);
    void rebuildNowProperties(std::vector<ui::PropertyGrid::Category>& cats, const hmi::Equipment& e);
    [[nodiscard]] std::string readNowText(const hmi::Equipment& e) const;
    void noteSwitches(double time);
    void runSlaveCard(int action);
    void refreshPlan();
    void refreshState();
    void refreshEquipments();
    void refreshDiagram();
    void refreshScan();
    void rebuildProperties();
    void rebuildPlcProperties(std::vector<ui::PropertyGrid::Category>& cats);
    void rebuildEquipmentProperties(std::vector<ui::PropertyGrid::Category>& cats);
    void rebuildBoundProperties(std::vector<ui::PropertyGrid::Category>& cats);
    void rebuildPortProperties(std::vector<ui::PropertyGrid::Category>& cats);
    void rebuildScanProperties(std::vector<ui::PropertyGrid::Category>& cats);
    // Lot 17.
    void rebuildMapProperties(std::vector<ui::PropertyGrid::Category>& cats);
    void rebuildSimPortProperties(std::vector<ui::PropertyGrid::Category>& cats);
    void rebuildZoneProperties(std::vector<ui::PropertyGrid::Category>& cats, const hmi::Equipment& e);
    void refreshMap(bool structure);                 // structure : la carte refaite (sinon : le direct)
    void refreshMapTargets();
    void refreshSimDiagram(NetDiagram& d);
    void tickLot17(double time);
    [[nodiscard]] hmi::MemZones mapShownZones(const hmi::Equipment* e) const;
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> plcMemory() const;     // %M, %MW de la configuration
    [[nodiscard]] const hmi::Equipment* equipmentOf(const std::string& name) const;
    [[nodiscard]] std::string twinStateText(const hmi::Equipment& e) const;
    void refreshChecks();
    void showReport(const std::vector<std::pair<std::string, int>>& lines, bool ok);
    [[nodiscard]] Side side() const;
    void say(std::string text, bool error = false);
    [[nodiscard]] hmi::comm::Plan currentPlan() const;
    [[nodiscard]] std::string demoState() const;
    bool change(const std::string& label, const std::function<void(hmi::Communication&)>& fn);
    bool changeProject(const std::string& label, const std::function<void(hmi::Project&)>& fn);
    [[nodiscard]] EquipmentHost* host() const;
    [[nodiscard]] std::optional<hmi::netinfo::Adapter> adapter(const std::string& name) const;
    void loadPortDraft();

    hmi::DocumentPtr       doc_;
    Apply                  apply_;
    Hosts                  hosts_;
    HmiToolStrip*          tools_{nullptr};
    ui::TabControl*        tabs_{nullptr};
    ui::TableView*         table_{nullptr};
    ui::TableView*         plan_{nullptr};
    ui::TableView*         report_{nullptr};
    ui::TableView*         state_{nullptr};
    ui::TableView*         equipTable_{nullptr};
    HmiScanPage*           scanPage_{nullptr};
    HmiNetDiagram*         diagram_{nullptr};
    ui::PropertyGrid*      grid_{nullptr};
    HmiCheckList*          checks_{nullptr};
    ui::Button*            applyButton_{nullptr};
    ui::Button*            cancelButton_{nullptr};
    ui::StatusBar*         status_{nullptr};
    std::shared_ptr<ui::ITableModel> tableModel_, planModel_, reportModel_, stateModel_, equipModel_, scanModel_;
    std::vector<std::string> order_;            // les variables de la table, dans l'ordre des lignes
    std::vector<std::string> planNames_;        // les variables du plan, dans l'ordre des lignes
    std::vector<PlanRow>     planRows_;         // lot 16 : les lignes du plan (groupes compris)
    std::set<std::string>    planClosed_;       // les groupes replies (majuscules)
    std::set<std::string>    planOpen_;         // les structures et tableaux deplies (majuscules)
    std::string              planFilter_;       // "" : tous ; kPlcKey ; un equipement
    ui::DropDown*            planFilterBox_{nullptr};
    void syncPlanTools();
    std::vector<EquipRow>    equipRows_;        // les lignes des equipements, dans l'ordre (kPlcKey en tete ; 1.9 : les esclaves)
    bool                     slaveRow_{false};  // 1.9 : la ligne choisie est celle de l'esclave simule de equipment_
    std::vector<std::string> scanOrder_;        // les adresses trouvees
    std::vector<std::string> reportLines_;
    std::vector<int>         reportTones_;
    double                   lastLive_{-10};
    double                   lastNet_{-10};
    std::string              shownDemoState_;
    std::string              shownSide_;
    bool                     refreshing_{false};
    std::string              message_;
    std::string              equipment_;        // l'equipement choisi (kPlcKey : l'automate)
    std::string              bound_;            // la variable liee choisie
    std::string              port_;             // le port choisi (Reseau du PC)
    bool                     portSide_{false};  // la droite montre le port (sinon l'equipement)
    struct PortDraft {
        std::string adapter;
        bool        dhcp{false};
        std::string ip, mask, gateway, dns;
        bool        edited{false};
    } draft_;
    std::string              diagramSignature_;
    double                   shownApplyAt_{0};  // le dernier changement d'adresse deja annonce
    struct Give {                               // "Donner au port ..." d'un equipement hors reseau
        std::string port, ip;
        int         prefix{24};
    };
    std::vector<Give>        gives_;
    // Le scanner.
    std::unique_ptr<hmi::ipscan::Scanner> scanner_;
    hmi::ipscan::Options     scanOptions_;
    std::string              scanPort_;         // le port dont le reseau est scanne
    std::string              scanRange_;        // la plage tapee (vide : le reseau du port)
    std::size_t              scanShown_{0};
    bool                     scanWasRunning_{false};
    // Lot 17 : la carte memoire.
    HmiMemoryMap*            map_{nullptr};
    ui::DropDown*            mapTargetBox_{nullptr};
    ui::DropDown*            mapShowBox_{nullptr};
    ui::DropDown*            mapRadixBox_{nullptr};
    ui::Checkbox*            mapFoldBox_{nullptr};
    ui::InputText*           mapSearchBox_{nullptr};
    std::vector<std::pair<std::string, bool>> mapTargets_;      // (equipement, jumeau), dans l'ordre de la liste
    std::string              mapEquip_;         // l'equipement de la carte (kPlcKey : l'automate)
    bool                     mapTwin_{false};
    std::string              mapSignature_;     // la carte montree (refaite quand elle change)
    std::unique_ptr<hmi::zones::Scanner> mapScanner_;
    std::string              mapScanKey_;       // ce que lit le scanner ("nom|jumeau")
    double                   lastMap_{-10};
    // Lot 17 : la detection des zones.
    std::unique_ptr<hmi::zones::Detector> detector_;
    std::string              detectEquip_;
    bool                     detectTwin_{false};
    bool                     detectAuto_{false};        // a la creation : appliquee d'office si elle trouve
    bool                     detectShown_{false};       // la fin deja annoncee
    // Lot 17 : le reseau simule, le filtre des equipements.
    bool                     simView_{false};
    std::string              simPort_;          // le port simule choisi
    std::string              equipFilter_{"tous"};
    ui::DropDown*            equipFilterBox_{nullptr};
    std::string              equipSummary_;
    // Lot 18 : les valeurs simulees.
    HmiTwinValues*           values_{nullptr};
    std::unique_ptr<TwinValuesController> valuesCtl_;
    double                   lastValues_{-10};
    double                   lastValuesSide_{-10};
    void refreshValuesBadge();
    // 1.9 : l'esclave simule lie.
    ui::Checkbox*            simMarksBox_{nullptr};      // Reperer les lectures simulees
    simmark::Indicator*      simReads_{nullptr};         // a droite de la barre d'etat
    HmiSlaveCard*            slaveCard_{nullptr};
    std::string              sheetNote_;                 // la note sous la fiche
    std::uint64_t            switchSeen_{0};             // la derniere bascule deja dite
    bool                     switchInit_{false};
    struct Toast {
        std::string title, text;
        double      since{-1}, until{-1};
    } toast_;
    bool                     darkTheme_{true};           // le theme du dernier dessin (le violet des lignes)
    core::ConnectionScope    links_;
};

} // namespace app
