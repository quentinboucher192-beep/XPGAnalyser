// =============================================================================
//  app/hmi/HmiSimulation.hpp - l'entree Simulation du dossier IHM
// -----------------------------------------------------------------------------
//  L'IHM EN MARCHE, BRANCHEE SUR LE SIMULATEUR EXISTANT (SimulationHost) :
//  la vue de demarrage d'abord, puis la navigation que font les actions et les
//  scripts. Chaque image, chaque propriete pilotee est reevaluee
//  (hmi::LiveView) contre les variables IHM puis l'automate simule, et le
//  tableau de droite dit, expression par expression, ce qu'elle vaut.
//
//  LE MOTEUR IHM (hmi::Runtime) tourne ici : scripts generaux et de vue,
//  actions (clic, double clic, appui long, fronts, timers...), navigation et
//  popups, avec leurs transitions (fondu, glissement, zoom, rotation,
//  personnalisee), journal. Les clics sur la vue vont aux objets de la vue du
//  dessus - la popup, s'il y en a une ; pendant une transition, ils attendent.
//
//  Les commandes de marche sont CELLES DE L'ESPACE DE TRAVAIL (Marche, Pause,
//  Arret, Un cycle) : une seule simulation, vue de deux endroits. "Forcer"
//  pose une valeur dans la table de forcage de l'automate simule.
// =============================================================================
#pragma once

#include "HmiPainter.hpp"
#include "../../hmi/HmiViewPaths.hpp"   // 1.11.6 : sur la vue actuelle
#include "../../hmi/HmiVarMotion.hpp"   // 1.11.6 : le forcage par type et bornes
#include "../../hmi/HmiTwin.hpp"        // 1.11.7 : le forcage commun (les cases des esclaves)
#include "HmiPanels.hpp"
#include "HmiLot13Painter.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiForms.hpp"
#include "../../hmi/HmiLive.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiScenarios.hpp"          // lot 13 : les essais
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <string>

namespace sim { class Runtime; class Environment; }

namespace app {

class EquipmentHost;
class HmiTwinValues;
class HmiSimVarTree;   // 1.11.5 : les variables IHM et API en arbre
class TwinValuesController;

struct HmiSimulationHost {
    std::function<sim::Runtime*()>          runtime;     // nullptr : pas de simulation preparee
    std::function<void(std::string_view)>  transport;   // "sim.run", "sim.pause", "sim.stop", "sim.step"
    std::function<std::string()>           state;       // "En marche - cycle 1204"
    std::function<void(std::string)>       force;       // le dialogue "Forcer", pre-rempli
    std::function<void()>                  unforceAll;
    // "Changer d'utilisateur" (bouton, ou action) : le dialogue de connexion,
    // pre-rempli avec ce login ; il appelle ensuite Runtime::login.
    std::function<void(std::string)>       login;
    // Lot 6 : le gestionnaire de recettes change le projet (ajouter, modifier,
    // supprimer, lire un jeu) ; "Demander une ressource" ouvre un selecteur.
    std::function<void(const hmi::RecipeRequest&)>   recipe;
    std::function<void(const hmi::ResourceRequest&)> resource;
    // Lot 8 : la gestion des utilisateurs (ajouter, modifier, supprimer, activer,
    // donner un mot de passe) et le mot de passe change par l'utilisateur.
    std::function<void(const hmi::UserRequest&)>     users;
    // Lot 9 : l'etat du simulateur de l'automate, pour les variables SYS.Plc*.
    std::function<hmi::PlcStatus()>                  plc;
    // Lot 11 : ecrire un fichier exporte (bouton d'export, action Exporter) ; vrai :
    // ecrit, `where` son chemin (sinon pourquoi).
    std::function<bool(const hmi::ExportRequest&, std::string* where)> exportFile;
    // Lot 13 : une ligne du journal d'audit, a peine ecrite (ajoutee tout de suite
    // a ihm/historique/audit.csv).
    std::function<void(const hmi::AuditEntry&)>      audit;
    // Lot 14 : l'automate reel - la liaison Modbus TCP (nulle : l'IHM lit le
    // simulateur, par runtime) ; les evenements de la communication (au
    // journal) ; le serveur de demonstration (tourne-t-il, sur quel port).
    std::function<sim::Environment*()>               link;
    std::function<std::vector<std::string>()>        commEvents;
    std::function<std::pair<bool, int>()>            commDemo;
    // Lot 14 : le poste d'exploitation (SYS.StationMode, SYS.StationScreens) -
    // tourne-t-on en poste, sur combien d'ecrans.
    std::function<std::pair<bool, int>()>            station;
    // Lot 14 : les notifications (chaque evenement d'alarme, leurs chiffres, les
    // lignes pour le journal) et les rapports periodiques ecrits (a envoyer).
    std::function<void(const hmi::AlarmNotice&)>     alarmNotice;
    std::function<hmi::NotifyStats()>                notifyStats;
    std::function<std::vector<std::string>()>        notifyEvents;
    std::function<void(const hmi::ReportOutput&)>    reportWritten;
    // Lot 15 : les equipements du reseau - la liaison d'un equipement (les
    // variables IHM liees), l'etat de chacun (SYS.Equip*, IHM_EQUIPEMENT_OK).
    std::function<hmi::comm::Link*(const std::string&)>   equipmentLink;
    std::function<std::vector<hmi::EquipmentStatus>()>    equipmentStatus;
    // 1.9 : les esclaves simules - la page Simulation de Parametres systeme,
    // SYS.Slave.* (withValues : leurs lignes) ; une commande de la page.
    std::function<std::vector<hmi::SimSlave>(bool withValues)>         simSlaves;
    std::function<bool(const hmi::SimSlaveCommand&, std::string* why)> simSlaveCommand;
    // Lot 18 : l'onglet Jumeaux - les esclaves simules (leurs memoires) et les
    // changements du projet (animer, la zone, forcer : des commandes, Ctrl+Z).
    std::function<EquipmentHost*()>                       equipments;
    std::function<void(core::CommandPtr)>                 apply;
    // ---- Lot API 8 : les exports qui demandent ou ----
    // Un export parti d'un geste de l'operateur (le bouton d'export, l'action
    // Exporter au clic, IHM_EXPORTER dans le script d'un clic), l'option
    // "Demander ou enregistrer" cochee : l'hote pose la question (l'editeur : le
    // dialogue du lot 7 ; le poste : le sien, au doigt), ecrit a la reponse, puis
    // `done(ecrit, ou)` - Annuler : done(false, ""). Faux (ou vide) : pas de
    // question possible ici, exportFile l'ecrit dans exports/ tout de suite.
    std::function<bool(const hmi::ExportRequest&, std::function<void(bool, const std::string&)> done)> askExport;
    // 1.10 (decision 12) : le plein ecran de l'IHM - la fenetre de l'appli en plein
    // ecran (vrai), puis rendue comme avant (faux). Nul : la vue couvre la fenetre seule.
    std::function<void(bool)>                             fullScreenWindow;
    // ---- 1.11.13 : LA GENERATION INCREMENTALE ----
    //  Demarrer l'IHM passe par le build (analyse des modifications, API, IHM,
    //  compilation, validation ; un projet a jour ne refait rien) : l'hote le lance
    //  et rappelle buildDone(ok, pourquoi) a la fin - la simulation ne demarre que
    //  sur un build valide. Nul : elle demarre tout de suite (le poste
    //  d'exploitation, les essais, les exemples de l'aide).
    std::function<void(const std::string& source)>        buildGate;
    // ---- 1.11.14 : LA CONSOLE ET LES SORTIES ----
    //  Chaque ligne du journal du moteur (IHM_LOG, une erreur d'execution, une action,
    //  la navigation) : l'hote la passe a la Console du panneau du bas. Le demarrage et
    //  l'arret de l'IHM (la session : le numero du demarrage) : une ligne des Sorties.
    std::function<void(const hmi::JournalEntry&)>         console;
    std::function<void(bool started, int session)>        lifecycle;
};

// Une vue a dessiner : evaluee, avec l'etat de sa transition.
struct HmiLiveLayer {
    hmi::View            view;
    std::vector<hmi::Id> errors;
    hmi::Frame           frame;
    bool                 popup{false};    // centree, par-dessus la vue, sur un voile
    float                veil{0};         // l'opacite du voile sous la popup (0..1)
    // Les objets que l'utilisateur connecte ne peut pas actionner (niveau
    // d'acces, autorisation) : un cadenas dessus.
    std::vector<hmi::Id> locked;
    // Lot 8 : une popup a SA place (coin haut-gauche de sa fenetre, en pixels de
    // la vue du dessous), avec sa barre de titre et son comportement. slot < 0 :
    // centree (une popup qui se ferme, sans place connue).
    int         slot{-1};
    double      px{0}, py{0};
    bool        titleBar{false}, closeButton{false}, modal{true}, movable{false}, closeOutside{false};
    std::string title;
    // Lot 14 : relie a un automate reel, les objets dont une valeur n'est pas
    // bonne - 1 ancienne (un cadre orange, une horloge), 2 mauvaise (voile,
    // cadre rouge, une croix).
    std::vector<std::pair<hmi::Id, std::uint8_t>> quality;
    // 1.11.1 (decision 134, R1111-12) : pour chaque objet marque, la raison de sa
    // valeur la pire (le chemin, pourquoi, le remede) - l'infobulle de la marque.
    std::vector<std::pair<hmi::Id, std::string>> qualityTips;
    // 1.9 : LES LECTURES SIMULEES - les objets dont une valeur est lue sur
    // l'esclave simule de son equipement (Runtime::slaveReadOf) : un cadre violet
    // en tirets et une pastille a fiole ; au survol, l'infobulle (tip).
    struct SimRead {
        hmi::Id     object{hmi::kNoId};
        std::string equipment, path, tip;
    };
    std::vector<SimRead> simulated;
};

// Le dessin des vues evaluees, ajustees a la place disponible.
class HmiLiveCanvas final : public ui::Widget {
public:
    explicit HmiLiveCanvas(std::string id);
    void show(hmi::View evaluated, std::vector<hmi::Id> errors);
    // La vue courante d'abord, puis ce qui se dessine par-dessus (la vue qui
    // arrive pendant une transition, les popups). `interactive` : les clics
    // vont a la derniere couche.
    void showLayers(std::vector<HmiLiveLayer> layers, bool interactive);
    // Les ressources et fichiers externes du projet (images, polices, tableaux).
    void setAssets(const hmi::Assets* assets) noexcept { assets_ = assets; }
    // Le moteur et l'historique : les courbes et les historiques dessines vivants.
    void setLive(const hmi::Runtime* runtime, const hmi::History* history) noexcept { runtime_ = runtime; history_ = history; }
    // Le projet (lot 6) : les recettes des gestionnaires de recettes.
    void setProject(const hmi::Project* project) noexcept { project_ = project; }
    // Lot 14 : le poste d'exploitation - fond noir, la vue a toute la place.
    void setStation(bool on) noexcept { station_ = on; invalidate(); }
    [[nodiscard]] HmiViewport viewport() const noexcept { return vp_; }
    // Les couches montrees (lot 14 : les essais y lisent la qualite des objets).
    [[nodiscard]] const std::vector<HmiLiveLayer>& layers() const noexcept { return layers_; }
    // L'objet sous ce point (couche du dessus), kNoId sinon ; et son cadre a
    // l'ecran (pour les scripts qui cliquent dessus).
    [[nodiscard]] hmi::Id objectAt(gfx::Point p) const;
    // 1.10.2 : la couche du dessus qui vit (pas une popup qui se ferme, place -2).
    [[nodiscard]] std::size_t liveTopLayer() const;
    [[nodiscard]] bool    objectRect(std::string_view name, gfx::Rect& out) const;
    // Lot 6 : une partie d'un gestionnaire de recettes a l'ecran ("bouton:Ajouter",
    // "ligne:0"), pour les scripts qui cliquent dessus.
    [[nodiscard]] bool    partRect(std::string_view name, std::string_view part, gfx::Rect& out) const;
    const core::SignalPtr<hmi::Id> clicked = core::Signal<hmi::Id>::create();
    const core::SignalPtr<hmi::Id> pressed = core::Signal<hmi::Id>::create();
    const core::SignalPtr<hmi::Id, bool> released = core::Signal<hmi::Id, bool>::create();
    const core::SignalPtr<hmi::Id> doubleClicked = core::Signal<hmi::Id>::create();
    // Lot 6 : un clic sur une partie d'un objet a parties (le gestionnaire de
    // recettes : "bouton:Ajouter", "ligne:2").
    const core::SignalPtr<hmi::Id, std::string> partClicked = core::Signal<hmi::Id, std::string>::create();
    // Lot 10 : le menu natif Parametres systeme - la partie touchee
    // (hmi::systemMenuHit : "plus:luminosite", "onglet:diagnostic", "fermer",
    // "dehors" ; defiler : "defiler:=N", la premiere ligne voulue) ; et un
    // toucher sur l'ecran en veille (il le rallume, rien d'autre).
    const core::SignalPtr<std::string> systemPartClicked = core::Signal<std::string>::create();
    const core::SignalPtr<> wakeRequested = core::Signal<>::create();
    // Le cadre du menu Parametres systeme au dernier dessin (vide : ferme) ; une
    // de ses parties a l'ecran (pour les scripts et les tests) ; l'ecran de
    // l'IHM (le cadre de la vue courante), ou le menu, la veille et la
    // luminosite se dessinent.
    [[nodiscard]] gfx::Rect systemPanelRect() const noexcept { return systemRect_; }
    [[nodiscard]] bool      systemPartRect(std::string_view part, gfx::Rect& out) const;
    [[nodiscard]] gfx::Rect screenRect() const noexcept { return screen_; }
    // 1.9 : LE BANDEAU "LECTURES SIMULEES" en haut de l'ecran de l'IHM, par-dessus
    // la vue (vide : aucun) ; l'infobulle d'une lecture simulee sous ce point (vide :
    // aucune) ; le cadre de chaque objet repere au dernier dessin.
    void setSimRibbon(std::string text, std::string right = {});   // right : a droite (le poste : "poste d'exploitation - 1 equipement")
    [[nodiscard]] const std::string& simRibbon() const noexcept { return simRibbon_; }
    [[nodiscard]] std::string simTipAt(gfx::Point p) const;
    [[nodiscard]] const std::vector<std::pair<gfx::Rect, std::string>>& simMarkRects() const noexcept { return simRects_; }
    // Lot 12 : le menu natif de connexion - la partie touchee (hmi::loginMenuHit :
    // "onglet:comptes", "champ:secret", "bouton:connexion", "ligne:2", "role:1,2",
    // "fermer", "dehors" ; "rien" : le fond du menu ; defiler : "defiler:=N") ;
    // son cadre au dernier dessin, une de ses parties a l'ecran ("ligne:N" : le
    // rang a l'ecran).
    const core::SignalPtr<std::string> loginPartClicked = core::Signal<std::string>::create();
    [[nodiscard]] gfx::Rect loginPanelRect() const noexcept { return loginRect_; }
    [[nodiscard]] bool      loginPartRect(std::string_view part, gfx::Rect& out) const;
    // Lot 13 : le panneau de signature (hmi::signatureHit : "champ:motdepasse",
    // "motif:suivant", "bouton:signer", "fermer", "dehors"...) ; le bandeau de
    // l'avertissement de deconnexion et son bouton "Rester connecte".
    const core::SignalPtr<std::string> signaturePartClicked = core::Signal<std::string>::create();
    const core::SignalPtr<> stayRequested = core::Signal<>::create();
    [[nodiscard]] gfx::Rect signaturePanelRect() const noexcept { return signatureRect_; }
    [[nodiscard]] bool      signaturePartRect(std::string_view part, gfx::Rect& out) const;
    // 1.11.7 : le champ de saisie de l'action Clavier virtuel - une partie cliquee
    // ("bouton:valider", "bouton:annuler", "fermer", "champ", "dehors") ; ou elle est.
    const core::SignalPtr<std::string> promptPartClicked = core::Signal<std::string>::create();
    [[nodiscard]] gfx::Rect promptPanelRect() const noexcept { return promptRect_; }
    [[nodiscard]] bool      promptPartRect(std::string_view part, gfx::Rect& out) const;
    [[nodiscard]] gfx::Rect logoutWarningRect() const noexcept { return warning_.bar; }
    [[nodiscard]] gfx::Rect stayButtonRect() const noexcept { return warning_.button; }

    // ---- lot 8 : les popups a leur place, la saisie, le clavier virtuel ----------
    // La croix, un clic dehors ; `instant` : sans fondu (une popup non modale fermee
    // d'un clic dans la vue dessous : ce clic compte aussi pour la vue).
    const core::SignalPtr<int, bool> popupCloseRequested = core::Signal<int, bool>::create();
    const core::SignalPtr<int, double, double> popupMoved = core::Signal<int, double, double>::create();   // tiree
    const core::SignalPtr<int> popupRaised = core::Signal<int>::create();                      // cliquee dessous
    const core::SignalPtr<std::string> textTyped = core::Signal<std::string>::create();        // un champ a le focus
    const core::SignalPtr<int> keyTyped = core::Signal<int>::create();                         // hmi::EditKey
    const core::SignalPtr<> clickedAway = core::Signal<>::create();                            // hors du champ en saisie
    // Ou sont, a l'ecran, la fenetre d'une popup, sa barre de titre et sa croix
    // (pour les scripts et les tests) ; le clavier virtuel et ses touches.
    struct PopupRect { gfx::Rect window, title, close, content; int slot{-1}; bool modal{true}, movable{false}, closeOutside{false}; std::size_t layer{0}; };
    [[nodiscard]] const std::vector<PopupRect>& popupRects() const noexcept { return popupRects_; }
    [[nodiscard]] bool popupRect(std::string_view viewName, PopupRect& out) const;
    [[nodiscard]] gfx::Rect keyboardRect() const noexcept { return keyboardRect_; }
    [[nodiscard]] bool keyRect(std::string_view label, gfx::Rect& out) const;
    // Le point a l'ecran d'un point de la vue du dessous (la vue courante).
    [[nodiscard]] gfx::Point baseToScreen(double x, double y) const;
    // ---- lot 9 : le curseur et le potentiometre suivent la souris --------------------
    //  (objet, fraction 0..1 de la course, relache) : a l'appui, en glissant, au relachement.
    const core::SignalPtr<hmi::Id, double, bool> valueDragged = core::Signal<hmi::Id, double, bool>::create();
    // ---- lot 12 : glisser pour changer de vue, zoomer une grande vue -----------------
    //  Un glisser horizontal ample sur le fond : +1 la vue suivante (vers la gauche),
    //  -1 la precedente. La molette sur une vue qu'on peut zoomer : son zoom (en %).
    const core::SignalPtr<int> swiped = core::Signal<int>::create();
    const core::SignalPtr<double> viewZoomed = core::Signal<double>::create();
    [[nodiscard]] float userZoom() const noexcept { return userZoom_; }
    void resetUserZoom() { userZoom_ = 1.f; userPan_ = {}; invalidate(); }
    // ---- 1.10 : LE ZOOM DE LA VUE EN COURS (la barre de Simulation . IHM) ---------
    //  0 : la vue ajustee a la place (le defaut) ; sinon son echelle, 1 = 100 %
    //  (de 10 a 800 %). Ctrl+molette zoome autour du curseur ; une vue plus grande
    //  que la place se deplace (le fond tire, la molette, Maj+molette ; le bouton du
    //  milieu tire toujours). Les clics passent par le meme cadre que le dessin :
    //  ils touchent le bon objet a tout zoom.
    static constexpr float kMinDisplayZoom = 0.1f, kMaxDisplayZoom = 8.f;
    void setDisplayZoom(float scale);
    void zoomDisplayAround(float factor, gfx::Point anchor);
    void panDisplay(float dx, float dy);
    [[nodiscard]] float      displayZoom() const noexcept { return displayZoom_; }
    [[nodiscard]] float      shownZoom() const noexcept { return shownZoom_; }    // l'echelle au dernier dessin
    [[nodiscard]] gfx::Point displayPan() const noexcept { return displayPan_; }
    [[nodiscard]] bool       viewOverflows() const noexcept { return overflowX_ || overflowY_; }
    const core::SignalPtr<> displayZoomChanged = core::Signal<>::create();
    // 1.10 : L'IHM ARRETEE - la derniere image reste, sous un voile, avec ce texte
    // au milieu (vide : l'IHM tourne).
    void setStoppedNote(std::string text) { stoppedNote_ = std::move(text); invalidate(); }
    [[nodiscard]] const std::string& stoppedNote() const noexcept { return stoppedNote_; }
    // Les deux boutons de la carte ("Demarrer l'IHM", "Demarrer les deux") :
    // un clic envoie "hmi.start" ou "both.start" ; leur cadre au dernier dessin.
    const core::SignalPtr<std::string> stoppedAction = core::Signal<std::string>::create();
    [[nodiscard]] gfx::Rect stoppedButtonRect(bool both) const noexcept { return both ? stoppedBoth_ : stoppedStart_; }
    // Lot 13 : le dernier dessin, en ms, et les objets dessines (les performances).
    [[nodiscard]] double      lastPaintMs() const noexcept { return lastPaintMs_; }
    [[nodiscard]] std::size_t lastPaintObjects() const noexcept { return lastPaintObjects_; }
    [[nodiscard]] std::size_t paintCount() const noexcept { return paintCount_; }
    // Lot 13 : le doigt d'un essai - l'objet que le pas vient de toucher, marque
    // d'un cercle qui s'efface (`strength` 1 a 0 ; 0 : rien).
    void setTouch(hmi::Id object, float strength) { touch_ = object; touchStrength_ = strength; }
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    [[nodiscard]] hmi::Id objectAtLayer(std::size_t layer, gfx::Point p) const;
    void pressObject(std::size_t layer, gfx::Point p, int clickCount);
    void paintKeyboard(const ui::PaintContext&);
    // Lot 9 : l'objet dont on tire la valeur (curseur, potentiometre), et sa couche.
    hmi::Id                   valueDrag_{hmi::kNoId};
    std::size_t               valueDragLayer_{0};
    [[nodiscard]] double      dragFraction(std::size_t layer, const hmi::Object&, gfx::Point) const;
    // Lot 9 : une liste deroulante ouverte (couche du dessus) sous ce point :
    // la partie touchee ("choix:2", "defiler:1") ; "" : ailleurs.
    [[nodiscard]] bool        comboListHit(gfx::Point p, hmi::Id& object, std::string& part) const;
    // Lot 10 : defiler le diagnostic de `delta` lignes, borne : "defiler:=N".
    // 1.9 : la page Simulation - ses lignes ("defiler:=N"), ses cartes ("defiler_liste:=N").
    [[nodiscard]] std::string systemScrollPart(long delta) const;
    [[nodiscard]] std::string simListScrollPart(long delta) const;
    // 1.9 : les esclaves simules que montre la page Simulation - relus quatre fois
    // par seconde, et a chaque geste de la page (Runtime::simRevision).
    void refreshSimSlaves();
    std::vector<hmi::SimSlave> simSlaves_;
    double                    simSlavesAt_{-1};
    std::uint64_t             simSlavesRev_{0};
    // Lot 12 : la meme chose pour les listes du menu de connexion ; une touche du
    // clavier virtuel (le texte ou la commande qu'elle envoie).
    [[nodiscard]] std::string loginScrollPart(long delta) const;
    void pressKeyboard(gfx::Point p);
    std::vector<PopupRect>    popupRects_;
    int                       dragSlot_{-1};
    gfx::Point                dragGrab_{};
    float                     baseZoom_{1.f};
    gfx::Point                baseOrigin_{};
    gfx::Rect                 keyboardRect_{};
    std::vector<hmi::KeyCap>  keys_;
    bool                      shift_{false};
    std::size_t               downLayer_{0};
    std::vector<HmiLiveLayer> layers_;
    std::vector<HmiViewport>  vps_;          // celui de chaque couche, au dernier dessin
    bool                      interactive_{true};
    HmiViewport               vp_;
    const hmi::Assets*        assets_{nullptr};
    const hmi::Runtime*       runtime_{nullptr};
    const hmi::History*       history_{nullptr};
    const hmi::Project*       project_{nullptr};
    bool                      station_{false};         // lot 14 : fond noir, sans marge
    hmi::Id                   down_{hmi::kNoId};
    gfx::Rect                 systemRect_{};
    gfx::Rect                 screen_{};
    gfx::Rect                 systemScreen_{};     // 1.9 : la ou le menu Parametres systeme est dessine (la page Simulation : tout le canevas)
    std::optional<hmi::SystemMenuLayout> systemLayout_;   // le menu, au dernier dessin
    int                       systemTabShown_{0};
    gfx::Rect                 loginRect_{};
    std::optional<hmi::LoginMenuLayout> loginLayout_;     // lot 12 : le menu de connexion, au dernier dessin
    hmi::LoginTab             loginTabShown_{hmi::LoginTab::Connexion};
    gfx::Rect                 signatureRect_{};           // lot 13 : le panneau de signature, au dernier dessin
    std::optional<hmi::SignatureLayout> signLayout_;
    gfx::Rect                 promptRect_{};              // 1.11.7 : le champ du clavier virtuel, au dernier dessin
    std::optional<hmi::PromptLayout> promptLayout_;
    LogoutWarningRects        warning_{};                 // lot 13 : le bandeau de l'avertissement
    // Lot 12 : la barre d'un panneau defilant qu'on tire ; le glisser sur le fond ;
    // le zoom et la place d'une vue qu'on peut zoomer.
    hmi::Id                   scrollDrag_{hmi::kNoId};
    std::size_t               scrollDragLayer_{0};
    bool                      scrollDragVertical_{true};
    bool                      swipeArmed_{false};
    gfx::Point                swipeFrom_{};
    std::size_t               swipeLayer_{0};
    bool                      panning_{false};
    gfx::Point                panGrab_{}, panStart_{};
    float                     userZoom_{1.f};
    gfx::Point                userPan_{};
    hmi::Id                   zoomView_{hmi::kNoId};
    double                    lastPaintMs_{-1};          // lot 13 : -1 tant que rien n'est dessine
    std::size_t               lastPaintObjects_{0};
    std::size_t               paintCount_{0};
    hmi::Id                   touch_{hmi::kNoId};         // lot 13 : le doigt d'un essai
    float                     touchStrength_{0.f};
    // 1.9 : les lectures simulees - le bandeau, les cadres reperes (l'infobulle), la souris.
    std::string               simRibbon_, simRibbonRight_;
    std::vector<std::pair<gfx::Rect, std::string>> simRects_;
    // 1.11.1 (decision 134, R1111-12) : les marques de qualite et leur infobulle.
    struct QualityTip { gfx::Rect rect; std::string title, body; };
    std::vector<QualityTip>   qualityRects_;
    gfx::Point                mouse_{-1.f, -1.f};
    // 1.10 : le zoom de la vue en cours, son deplacement ; la taille de la vue du
    // dernier dessin (le zoom autour du curseur) ; l'IHM arretee.
    float                     displayZoom_{0.f}, shownZoom_{1.f};
    gfx::Point                displayPan_{};
    bool                      overflowX_{false}, overflowY_{false};
    float                     shownViewW_{0.f}, shownViewH_{0.f};
    bool                      displayPanning_{false};
    gfx::Point                displayGrab_{}, displayPanStart_{};
    std::string               stoppedNote_;
    gfx::Rect                 stoppedStart_{}, stoppedBoth_{};
};

// Lot 8 : les couches de l'IHM en marche - la vue courante (les deux vues d'une
// transition), puis les popups a leur place, avec leur barre de titre et leur
// titre a trous. La simulation s'en sert, les exemples animes de l'aide aussi :
// une seule facon de montrer une IHM qui tourne.
class HmiLayerBuilder {
public:
    // `topValues` : les expressions de la vue du dessus (le tableau Expressions).
    // Lot 14 : `onlyView` - cette vue seule, sans popup ni transition (un ecran
    // secondaire du poste d'exploitation).
    [[nodiscard]] std::vector<HmiLiveLayer> build(hmi::Runtime& runtime, const hmi::Project& project, double now,
                                                  std::vector<hmi::LiveValue>* topValues, hmi::Id onlyView = hmi::kNoId);
    void reset() {
        live_.clear();
        originals_.clear();
        paths_.clear();
    }
    // Les objets que l'utilisateur connecte ne peut pas actionner (vue du dessus).
    [[nodiscard]] const std::vector<hmi::Id>& locked() const noexcept { return locked_; }
    // 1.9 : reperer les lectures simulees (en plus de project.station.simMarks : le
    // bouton Reperes de la simulation, pour la seance).
    bool simMarks{true};
private:
    std::map<hmi::Id, hmi::LiveView> live_;                      // une par vue affichee
    std::map<hmi::Id, std::pair<double, double>> lastPlaces_;    // la derniere place de chaque popup
    std::map<std::string, hmi::TextTemplate> titles_;            // les titres a trous ("Armoire {Nom}")
    std::vector<hmi::Id> locked_;
    // Lot 13 : la vue d'origine et les reglages de chaque vue liee preparee
    // (HmiDisplay), et les traductions et unites du dernier dessin (changees :
    // tout se relie).
    std::map<hmi::Id, std::pair<hmi::View, hmi::DisplayOptions>> originals_;
    hmi::Languages                                               languages_;
    std::vector<hmi::VariableDisplay>                            displays_;
    // Lot 14 : les chemins de l'automate que lit chaque expression (la qualite).
    std::map<std::string, std::vector<std::string>>              paths_;
};

class HmiSimulationPane final : public ui::Widget {
public:
    HmiSimulationPane(std::string id, hmi::DocumentPtr doc, HmiSimulationHost host);
    ~HmiSimulationPane() override;
    void goToView(hmi::Id view);
    [[nodiscard]] hmi::Id currentView() const noexcept { return runtime_.currentView(); }
    [[nodiscard]] const std::vector<hmi::LiveValue>& values() const noexcept { return values_; }
    [[nodiscard]] hmi::Runtime& runtime() noexcept { return runtime_; }
    [[nodiscard]] HmiLiveCanvas& canvas() noexcept { return *canvas_; }
    [[nodiscard]] ui::TabControl& tabs() noexcept { return *tabs_; }
    // Les onglets du bas : Expressions, Journal, Variables IHM, Alarmes, Recettes ;
    // lot 13 : Performances.
    // Lot 18 : Jumeaux (ce que font les esclaves simules : animer, la zone, forcer).
    // 1.11.5 : Variables API apres Variables IHM (les deux en arbre, avec la recherche et le forcage).
    enum Tab : std::size_t { TabValues = 0, TabJournal = 1, TabVariables = 2, TabApiVariables = 3, TabAlarms = 4, TabRecipes = 5, TabPerf = 6, TabTwins = 7 };
    [[nodiscard]] HmiTwinValues& twins() noexcept { return *twins_; }
    // 1.11.5 : les onglets Variables IHM et Variables API (pour les scripts et les tests).
    [[nodiscard]] HmiSimVarTree& ihmVariables() noexcept { return *ihmVars_; }
    [[nodiscard]] HmiSimVarTree& apiVariables() noexcept { return *apiVars_; }
    // 1.11.6 : l'onglet Expressions en arbre (objet, instance, propriete) et sa recherche.
    [[nodiscard]] ui::TableView& expressions() noexcept { return *table_; }
    [[nodiscard]] ui::InputText* expressionsSearch() noexcept { return exprSearch_; }
    void expandExpressions(bool open);
    // 1.11.6 : les mouvements appliques maintenant (la mise a jour le fait ; les tests aussi).
    void applyMotions();
    [[nodiscard]] TwinValuesController& twinsController() noexcept { return *twinsCtl_; }
    // Lot 13 : le releve des performances (remis a zero, exporte en CSV).
    void resetPerf();
    bool exportPerf(std::string* where = nullptr);
    // Lot 13 : rejouer un essai de reception (IHM > Essais) - l'IHM redemarre,
    // chaque pas se joue a `pace` secondes du precedent ; le rapport va dans le
    // Document au fil des pas. Faux : essai introuvable.
    bool playScenario(hmi::Id scenario, double pace = 0.6);
    void stopScenario();
    [[nodiscard]] bool scenarioRunning() const noexcept { return scenario_.has_value() || pendingScenario_.has_value(); }
    // Les objets verrouilles de la vue du dessus (dernier rafraichissement).
    [[nodiscard]] const std::vector<hmi::Id>& lockedObjects() const noexcept { return layers_.locked(); }
    // Appliquer un jeu de recette depuis l'onglet Recettes (comme l'action).
    bool applyRecipe(const std::string& recipe, const std::string& record, std::string* why = nullptr);
    // Une evaluation, sans attendre le dessin (tests, captures), a l'heure `now`.
    // 1.10.1 : le volet avance de ce qui s'est ecoule depuis l'heure d'avant (celle
    // des images : le dessin, l'onglet cache) ; une heure d'une autre base de temps
    // ne le fige plus (voir followClock).
    void refreshNow();
    void refreshAt(double now);
    // Relancer l'IHM : variables a leur valeur initiale, scripts de demarrage,
    // vue de demarrage.
    void restart();
    // 1.10.1 : VIDER LE JOURNAL de l'onglet Journal (Vider dans sa barre, Vider le
    // journal au clic droit). Ce sont les lignes de la seance (Runtime::journal),
    // que le tableau relit a chaque rafraichissement : vider le seul tableau ne
    // servait a rien. Les lignes suivantes s'affichent comme avant. L'historique
    // (alarmes, evenements, systeme : le document) n'est pas touche. Rend le
    // nombre de lignes videes ; l'avis le dit ("Journal vide (154 lignes)").
    std::size_t clearJournal();
    [[nodiscard]] ui::TableView& journalTable() noexcept { return *journal_; }
    [[nodiscard]] const ui::StatusBar& statusBar() const noexcept { return *status_; }
    // 1.9 : le menu natif Parametres systeme sur cet onglet (0 Reglages, 1
    // Diagnostic, 2 la page Simulation) - les boutons de la barre, Ctrl+Alt+S
    // sur le poste ; `source` : ce qui l'ouvre (le journal le dit).
    void openSystemMenu(int tab, const std::string& source);
    // Lot 14 : LE POSTE D'EXPLOITATION - la vue seule, sur tout le volet (ni
    // barre, ni tableaux, ni etat) ; un fond noir autour.
    void setStationMode(bool on);
    [[nodiscard]] bool stationMode() const noexcept { return station_; }
    // 1.9 : LES REPERES DES LECTURES SIMULEES pour cette seance (le bouton Reperes de
    // la barre ; le projet les regle : station.simMarks). Le texte de la barre d'etat
    // ("Variateur ATV320 et Balance B lus en simule") et celui du bandeau.
    void setSimMarksShown(bool on);
    [[nodiscard]] bool simMarksShown() const noexcept { return layers_.simMarks; }
    [[nodiscard]] std::string simReadsText() const;
    [[nodiscard]] std::string simRibbonText() const;
    // ---- 1.10 : L'IHM ET L'API INDEPENDANTES ---------------------------------------
    //  L'IHM a son etat (arretee / en marche) et ses commandes (Demarrer, Arreter,
    //  Redemarrer l'IHM) ; l'API (le programme simule) les siennes, par l'hote
    //  ("sim.run", "sim.stop"...). Demarrer l'IHM ne lance pas le programme : sans
    //  API en marche, l'IHM lit et ecrit la memoire de l'automate simule, preparee
    //  sans cycle ("sim.prepare"). Rien ne demarre ni n'arrete l'autre en cachette.
    //  autoStart : l'IHM demarre d'elle-meme au premier rafraichissement (l'onglet
    //  qu'on ouvre, le poste, les essais, les exemples de l'aide : seule, l'API ne
    //  demarre pas) ; arretee par l'utilisateur (Arreter l'IHM), elle le reste
    //  (autoStart s'eteint) jusqu'a Demarrer l'IHM.
    void setAutoStart(bool on) noexcept { autoStart_ = on; }
    [[nodiscard]] bool autoStart() const noexcept { return autoStart_; }
    [[nodiscard]] bool hmiRunning() const noexcept { return started_; }
    void startHmi(const std::string& source = {});
    void stopHmi(const std::string& source = {});
    // 1.11.13 : la fin du build d'un demarrage (l'hote, buildGate) - valide : l'IHM
    // demarre ; sinon elle reste arretee et dit pourquoi (les sorties, la source).
    void buildDone(bool ok, const std::string& why = {});
    [[nodiscard]] bool waitingBuild() const noexcept { return waitingBuild_; }
    // L'etat de l'API en clair ("en marche \xC2\xB7 cycle 1204") et son ton
    // ("running", "paused", "stopped", "halted", "off").
    [[nodiscard]] std::string plcStateText() const;
    [[nodiscard]] std::string plcTone() const;
    // La barre de l'onglet : ses boutons suivent les deux etats, la vue, le zoom,
    // l'utilisateur (a chaque rafraichissement ; ici pour les essais).
    void refreshBar();
    [[nodiscard]] ui::ToolBar& bar() noexcept { return *bar_; }
    // Les parties de la barre, pour les scripts et les essais : "ihm", "api",
    // "vue", "zoom" (la valeur), "utilisateur" ; "" sinon.
    [[nodiscard]] std::string barText(std::string_view part) const;
    [[nodiscard]] gfx::Rect   barPartRect(std::string_view part) const;
    // Une commande de la barre (la meme que son bouton) : "hmi.start", "hmi.stop",
    // "hmi.restart", "plc.start", "plc.stop", "both.start", "all.stop",
    // "hmi.zoomIn", "hmi.zoomOut", "hmi.zoomFit", "hmi.zoom100", "hmi.back"...
    void command(core::ActionId id) { run(id); }
    // ---- 1.10 (decision 12) : LE SIMULATEUR IHM EN PLEIN ECRAN ---------------------
    //  Le bouton Plein ecran de la barre, F11 ("hmi.fullscreen") : la fenetre passe en
    //  plein ecran (l'hote), la vue de l'IHM la couvre toute (la passe du dessus : ni
    //  l'arbre, ni les onglets, ni les barres), ajustee ; une petite barre flottante en
    //  haut (la vue, l'etat de l'IHM, le zoom, Quitter) s'efface apres 3 s sans souris
    //  et revient quand la souris approche du haut. Echap ou F11 : la sortie, la fenetre
    //  et le zoom comme avant. Tout reste cliquable (la vue recoit les gestes).
    void setFullScreen(bool on);
    [[nodiscard]] bool fullScreen() const noexcept { return fullScreen_; }
    [[nodiscard]] bool fullScreenBarShown() const;
    // Les parties de la barre flottante : "barre", "zoomOut", "zoom", "zoomIn", "fit", "exit".
    [[nodiscard]] gfx::Rect fullScreenBarPart(std::string_view part) const;
    // F11 : un onglet Simulation . IHM a l'ecran (ou en plein ecran) la prend - dans
    // la fenetre principale (windowRoot nul : les volets hors des fenetres detachees),
    // ou dans la fenetre detachee de racine `windowRoot` (DetachedWindows).
    [[nodiscard]] static bool claimsKey(ui::Key key, const ui::Widget* windowRoot = nullptr);
    [[nodiscard]] bool requestsOverlayPass() const override { return fullScreen_; }
    [[nodiscard]] gfx::Rect eventBounds() const override { return fullScreen_ ? fullRect_ : bounds(); }
protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    void onPaintOverlay(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    void run(core::ActionId id);
    bool                  fullScreen_{false};     // 1.10 : le plein ecran
    float                 zoomBeforeFull_{0.f};   // ... et le zoom d'avant (rendu a la sortie)
    gfx::Rect             fullRect_{};            // toute la fenetre
    ui::Widget*           fsBar_{nullptr};        // la barre flottante (devant tout)
    void ensureStarted(double now);
    void updateTables();
    // 1.11.6 : sur la vue actuelle - les variables que lit la vue montree (et ses popups
    // ouvertes), refaites quand elles changent, pour Esclaves simules, Variables IHM et API.
    void updateViewFilter();
    // 1.11.6 : LE FORCAGE PAR TYPE ET BORNES - chaque mouvement tient sa variable (IHM :
    // forceVariable ; API : sim::Runtime::force) a sa valeur du moment, a chaque mise a jour.
    struct MotionOn {
        hmi::motion::Motion motion;
        hmi::motion::State  state;
    };
    std::map<std::string, MotionOn> ihmMotions_, apiMotions_;
    // 1.11.7 : LE FORCAGE COMMUN. Une variable IHM liee a un esclave simule (Four1.Temperature
    // lue sur la Balance B, en 43001) est la meme case que la ligne de l'esclave : la forcer,
    // l'animer, la liberer - depuis Variables IHM ou Esclaves simules - c'est forcer, animer,
    // liberer la case de l'esclave (Equipment::forcings, behaviors). Les deux onglets disent le
    // meme etat ; le moteur ignore les ecritures des scripts sur elle (Hooks::boundForced).
    struct TwinPlace {
        std::string           equipment;
        hmi::twin::ValueRow   row;
    };
    std::map<std::string, TwinPlace> twinPlaces_;           // cle : le chemin en majuscules
    std::string                      twinPlacesSig_;
    void updateTwinPlaces();
    [[nodiscard]] const TwinPlace* twinPlace(std::string_view path) const;
    [[nodiscard]] bool             twinForced(const TwinPlace&) const;
    [[nodiscard]] const hmi::Behavior* twinBehavior(const TwinPlace&) const;
    // 1.10.1 (C1) : L'HORLOGE DU VOLET. Deux sources la font avancer : le dessin
    // (onPaint, ctx.time) et les appels (refreshAt : l'onglet cache, les tests).
    // Chacune a son decalage, nul tant qu'elle est sur la meme base que le volet :
    // now_ = max(now_, heure + decalage), comme avant. Une heure qui retarde de
    // plus d'une seconde ou avance de plus d'une heure sur le volet vient d'une
    // autre base : son decalage la ramene a l'heure du volet, qui continue sans se
    // figer ni sauter. En 1.10.0, l'onglet cache avancait a l'horloge du journal
    // de la simulation (steady_clock), le dessin a celle des images : now_ restait a
    // la plus grande, l'IHM revenue a l'ecran ne faisait plus un cycle (la vanne a
    // 12 %, 55 % apres un changement d'onglet). refreshPane : sans toucher a l'heure.
    enum class ClockSource : std::uint8_t { Paint = 0, Call = 1 };
    void followClock(double now, ClockSource source);
    void refreshPane();
    static constexpr double kClockLeap = 3600.0;   // au-dela : une autre horloge
    bool                  clockSeen_{false};
    double                clockOffset_[2]{0.0, 0.0};
    hmi::DocumentPtr      doc_;
    HmiSimulationHost     host_;
    hmi::Runtime          runtime_;
    bool                  started_{false};
    bool                  autoStart_{true};  // 1.10
    // 1.11.13 : le demarrage attend son build (buildGate) ; gateOpen_ : le build est
    // valide, le prochain demarrage passe ; blockedNote_ : pourquoi il a ete refuse.
    bool                  waitingBuild_{false};
    bool                  gateOpen_{false};
    std::string           gateSource_, blockedNote_;
    bool                  askBuild(const std::string& source);
    bool                  station_{false};   // lot 14
    // 1.10 : la barre de l'onglet - les boutons qui suivent les etats, les pastilles
    // (IHM, API, zoom), le choix de la vue ; la vue d'avant (Precedente).
    ui::Button*           hmiStartBtn_{nullptr};
    ui::Button*           hmiStopBtn_{nullptr};
    ui::Button*           plcBtn_{nullptr};
    ui::Button*           loginBtn_{nullptr};
    ui::Widget*           hmiChip_{nullptr};
    ui::Widget*           plcChip_{nullptr};
    ui::Widget*           zoomChip_{nullptr};
    ui::DropDown*         viewList_{nullptr};
    std::vector<hmi::Id>  viewIds_;           // l'identifiant de chaque ligne de la liste
    hmi::Id               shownView_{hmi::kNoId}, previousView_{hmi::kNoId};
    std::string           barState_;          // ce que la barre montre (pour ne la refaire qu'au besoin)
    bool                  listing_{false};    // la liste se remplit : son signal ne navigue pas
    double                now_{0};
    HmiLayerBuilder       layers_;               // les vues evaluees, a dessiner
    bool                  bound_{false};
    std::vector<hmi::LiveValue> values_;
    ui::ToolBar*          bar_{nullptr};
    ui::Splitter*         split_{nullptr};
    HmiLiveCanvas*        canvas_{nullptr};
    ui::TabControl*       tabs_{nullptr};
    ui::TableView*        table_{nullptr};
    ui::TableView*        journal_{nullptr};
    HmiSimVarTree*        ihmVars_{nullptr};      // 1.11.5
    HmiSimVarTree*        apiVars_{nullptr};      // 1.11.5
    std::string           ihmVarsSig_;            // les variables IHM telles que l'arbre les a
    const void*           apiRuntime_{nullptr};   // la simulation de l'automate telle que l'arbre l'a
    std::size_t           apiSlots_{0};
    ui::InputText*        exprSearch_{nullptr};                               // 1.11.6 : la recherche des expressions
    std::string           viewFilterSig_;                                   // 1.11.6 : la vue et ses popups
    std::shared_ptr<const hmi::viewpaths::Filter> viewFilter_;
    ui::TableView*        alarms_{nullptr};
    ui::TableView*        recipes_{nullptr};
    ui::TableView*        perf_{nullptr};                      // lot 13 : les performances
    double                perfShownAt_{-1};
    std::size_t           paintSeen_{0};                       // le dernier dessin deja compte
    std::optional<hmi::ScenarioRun> scenario_;                 // lot 13 : l'essai qui se joue
    std::optional<std::pair<hmi::Id, double>> pendingScenario_;  // ... et celui qui attend le prochain rafraichissement
    std::size_t           scenarioShown_{static_cast<std::size_t>(-1)};
    ui::StatusBar*        status_{nullptr};
    ui::Widget*           simReads_{nullptr};                  // 1.9 : a droite de la barre d'etat (simmark::Indicator)
    ui::ToggleButton*     marksButton_{nullptr};               // 1.9 : Reperes (la seance)
    std::shared_ptr<ui::ITableModel> model_, journalModel_, alarmsModel_, recipesModel_, perfModel_;
    std::string           alarmsShown_, recipesShown_;         // ce que montrent les deux tableaux (pour ne les refaire qu'au besoin)
    std::vector<std::pair<std::string, std::string>> recipeRows_;   // (recette, jeu) de chaque ligne
    std::size_t           journalShown_{static_cast<std::size_t>(-1)};
    // Lot 18 : l'onglet Jumeaux.
    HmiTwinValues*        twins_{nullptr};
    std::unique_ptr<TwinValuesController> twinsCtl_;
    double                twinsTick_{-10};
    bool                  twinsDirty_{true};
    // Lot 18 : les marques des courbes (une zone tiree, un forcage) - ce que les jumeaux faisaient.
    void noteTwinChanges();
    std::map<std::string, std::pair<std::vector<hmi::Behavior>, std::vector<hmi::Forcing>>> twinSeen_;
    bool                  twinSeenInit_{false};
    // Lot API 8 : un jeton de vie - la reponse a la question "ou enregistrer"
    // arrive plus tard ; le volet parti entretemps, elle ne va nulle part.
    std::shared_ptr<char> exportAlive_{std::make_shared<char>('\0')};
    core::ConnectionScope links_;
};

} // namespace app
