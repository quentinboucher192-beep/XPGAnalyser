// =============================================================================
//  app/SimDebugPane.hpp - lot API 8 : l'onglet Simulation > Debogage
// -----------------------------------------------------------------------------
//  Ou le programme s'arrete, pourquoi, et ce qu'il fait - en francais, sur un
//  seul ecran :
//
//   * LA BARRE : Continuer (F5), Section suivante (F10), Cycle suivant (F11),
//     Pause, Arreter (Maj+F5), Executer jusqu'a la ligne (Ctrl+F10) ; + Point
//     d'arret, + Espion. Au-dessus du reste, L'ETAT EN CLAIR : « En pause au
//     point d'arret 2 : SFC_PurgeA, ligne 42, cycle 1 243 - la condition
//     Armoires[0].etat = 3 est vraie ».
//   * A GAUCHE : LES POINTS D'ARRET (le rond : actif / inactif d'un clic ;
//     section et ligne ; la condition, modifiable sur place (double-clic) ; les
//     passages ; la croix les enleve) et LES ESPIONS (des chemins, leur valeur
//     en direct, depuis quand elle a change ; ils se deplient comme dans
//     l'onglet Automate - project/MemberTree).
//   * AU CENTRE : LA PILE (MAST > Logigrammes_A > SFC_PurgeA > Gc_PurgeA
//     (DFB_GRAFCETENGINE) > Main), chaque niveau cliquable, et LE CODE de la
//     section ou l'on est (lecture seule) : la ligne d'arret surlignee, les
//     points d'arret en rouge dans la marge (un clic en pose ou en enleve un),
//     a droite de chaque ligne les valeurs de ses variables (au passage).
//   * A DROITE : POURQUOI ICI (en clair) et QUI A ECRIT ? (la variable choisie :
//     un espion, ou le nom sous le curseur dans le code) - la section, la
//     ligne, le cycle, et « Aller a ».
//   * EN BAS : OU PASSE LE TEMPS DU CYCLE (une barre par section, la plus lente
//     en tete, en ms et en % de la periode de MAST) et LA TRACE DU CYCLE (les
//     entrees de MAST dans l'ordre, les instructions de chaque section).
//
//  La simulation est a App (SimulationHost) : l'onglet la montre et la pilote ;
//  le fermer ne l'arrete pas et ne retire aucun point d'arret. Les calculs sans
//  ecran (les phrases, la pile, les temps) sont dans SimDebugText.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/MemberTree.hpp"
#include "../ui/Widget.hpp"
#include "SimDebugText.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TableView; class MultiLineText; class InputText; }
namespace sim { class Runtime; class Value; }

namespace app {

class ApiFrame;
class SimulationHost;

class SimDebugPane final : public ui::Widget {
public:
    enum Action : int {
        AContinue = 1, AStepSection, AStepCycle, APause, AStop, ARunToLine,
        AAddBreakpoint, AClearBreakpoints, AAddWatch, AHelp,
        ADisableAll, AModify,           // la maquette : « Tout desactiver », « Modifier le projet... »
    };

    struct Hosts {
        std::function<std::shared_ptr<const domain::Project>()> project;
        std::function<SimulationHost*()>                         host;
        // La simulation du projet ouvert, preparee (sans la lancer) ; faux : `why`.
        std::function<bool(std::string* why)>                     attach;
        // L'onglet d'une section, le curseur sur une ligne (1 = la premiere ;
        // "Bloc.Section" : le corps d'un DFB).
        std::function<void(const std::string& section, std::uint32_t line)> goToLine;
        std::function<void(const std::string& key)>               request;   // "ordre", "ordre:<entree>"
        std::function<void(const std::string& text)>              status;
        // Une question a un champ : le titre, le libelle du champ, le paragraphe,
        // la valeur de depart ; `done` recoit le texte (rien : Annuler).
        std::function<void(const std::string& title, const std::string& label, const std::string& text,
                           const std::string& initial, std::function<void(const std::string&)> done)> ask;
        // LE JOURNAL (le Centre de simulation) : un evenement du debogage - le
        // genre ("point-arret", "arret", "pas", "continuer", "pause", "arreter",
        // "condition", "espion"), la phrase, la section et la ligne (0 : aucune).
        std::function<void(const std::string& kind, const std::string& text, const std::string& section, int line)> journal;
        // L'aide (F1 de l'onglet).
        std::function<void()>                                     help;
        // Lot API 8 (2e partie) : le dialogue de la condition d'un point d'arret
        // (le clic droit dans la marge) - la section (nom de simulateur), la ligne
        // (1 = la premiere). L'ecran pose le point au besoin et ouvre le dialogue.
        std::function<void(const std::string& section, int line)> askCondition;
    };

    // ---- Lot API 8 (2e partie) : la condition par clic droit ----
    //  Ce que le dialogue de la condition montre (SimConditionDialog) : le point
    //  de la ligne (pose d'abord s'il n'y en avait pas : `placed`), son rang dans
    //  la liste, le titre, le code de la ligne, sa condition, les idees.
    struct ConditionAsk {
        std::uint32_t            id{0};
        std::size_t              number{0};     // 1 = le premier de la liste
        std::string              section;       // le nom de simulateur
        int                      line{0};       // 1 = la premiere
        bool                     placed{false};
        std::string              title, code, condition;
        std::vector<std::string> ideas;
        std::vector<std::string> prefixes;      // ou chercher un nom court (Unite., l'instance du bloc)
    };
    // Sans l'onglet (l'onglet d'une section) : le point de cette ligne, pose au
    // besoin, et ce que le dialogue montre ; `instance` : l'instance d'une section
    // de bloc (ses valeurs). Faux : section ou ligne introuvable (`why`).
    static bool prepareConditionFor(const domain::Project& p, SimulationHost& host, const std::string& section, int line,
                                    const std::string& instance, ConditionAsk& out, std::string* why = nullptr);
    // En direct : la condition lue dans la simulation MAINTENANT, sans rien
    // ecrire ni appeler (« Maintenant, c'est vrai (x = 3). », « Sans condition :
    // la simulation s'arretera a chaque cycle. », « ... n'existe pas »).
    [[nodiscard]] static std::string previewCondition(sim::Runtime* rt, const std::vector<std::string>& prefixes,
                                                      const std::string& condition);
    // ---- fin Lot API 8 (2e partie) ----

    explicit SimDebugPane(std::string id);
    ~SimDebugPane() override;
    void setHosts(Hosts h);
    void attach(ApiFrame& frame);
    // Le projet a change (une commande, un autre projet) : le code se relit.
    void refresh();
    // A chaque image, l'onglet a l'ecran ou non. Cache : rien. A l'ecran : l'etat,
    // les points d'arret, et (quatre fois par seconde en marche) les valeurs.
    void tick(double now);
    void runAction(int action);

    // ---- pour les scripts et les tests (sans la souris) --------------------------
    // Un point d'arret tape comme dans « + Point d'arret » : "SFC_PurgeA 42",
    // "SFC_PurgeA 42 si x > 3". Faux : `why`.
    bool addBreakpointText(const std::string& text, std::string* why = nullptr);
    // Poser ou enlever le point d'arret de cette ligne (1 = la premiere) : le clic
    // dans la marge. Rend vrai s'il est pose, faux s'il est enleve (ou refuse).
    bool toggleBreakpoint(const std::string& section, int line);
    // Par leur rang dans la liste (1 = le premier).
    bool setBreakpointCondition(std::size_t number, const std::string& condition);
    bool setBreakpointEnabled(std::size_t number, bool enabled);
    bool removeBreakpoint(std::size_t number);
    void clearBreakpoints();
    // « Tout desactiver » (la maquette) : ils restent dans la liste, inactifs.
    void disableAllBreakpoints();
    // « Modifier le projet... » : en pause, le code de la ligne d'arret dans l'onglet
    // de sa section (on y modifie comme d'habitude ; Continuer reprend au meme
    // cycle). Faux : pas en pause (la barre d'etat le dit).
    bool modifyProject();
    [[nodiscard]] std::size_t breakpointCount() const noexcept { return bps_.size(); }
    // Les espions : un chemin de la simulation ("Armoires[0].ana.PT1.mes",
    // "Unite.compteur"). Faux : introuvable (`why`), ou deja la.
    bool addWatch(const std::string& path, std::string* why = nullptr);
    bool removeWatch(const std::string& path);
    bool expandWatch(const std::string& path, bool open);
    [[nodiscard]] std::vector<std::string> watches() const;
    // "chemin = valeur (depuis)", une par ligne montree.
    [[nodiscard]] std::vector<std::string> watchLines() const;
    // Le code : une section par son nom de simulateur, une ligne en vue (0 :
    // aucune) ; un niveau de la pile (0 = le premier).
    bool showSection(const std::string& section, int line = 0);
    bool chooseLevel(std::size_t level);
    [[nodiscard]] const std::string& codeSection() const noexcept { return codeSection_; }
    [[nodiscard]] ui::MultiLineText& code() noexcept { return *code_; }
    // Executer jusqu'a la ligne (1 = la premiere) du code montre ; 0 : la ligne du curseur.
    bool runToLine(int line = 0);
    // La variable de « Qui a ecrit ? ».
    void chooseVariable(const std::string& path);
    [[nodiscard]] const std::string& chosenVariable() const noexcept { return writerPath_; }
    [[nodiscard]] std::string stateLine() const;
    [[nodiscard]] std::vector<std::string> whyLines() const;
    [[nodiscard]] std::vector<std::string> writerLines() const;
    [[nodiscard]] std::vector<std::string> stackLabels() const;
    // Ou est une zone cliquable (apres un dessin) : "pile:<n>" (0 = le premier
    // niveau), "ecrit:aller", "ecrit:<n>" (une ligne du code qui l'ecrit),
    // "etat:aller", "temps:<n>" (une barre), "points:ajouter", "points:tout",
    // "espions:ajouter", "espions:retirer". Vide : pas montree.
    [[nodiscard]] gfx::Rect partRect(std::string_view key) const;
    [[nodiscard]] ui::TableView& breakpointTable() noexcept { return *bpTable_; }
    [[nodiscard]] ui::TableView& watchTable() noexcept { return *watchTable_; }
    [[nodiscard]] ui::TableView& traceTable() noexcept { return *traceTable_; }

    // ---- Lot API 8 (2e partie) : pour les scripts et les tests ----
    // LE CLIC DROIT dans la marge du code (1 = la premiere ligne) : le point (pose
    // au besoin) et son dialogue (hosts_.askCondition ; sans lui : prepare seulement).
    bool editCondition(const std::string& section, int line);
    bool prepareCondition(const std::string& section, int line, ConditionAsk& out, std::string* why = nullptr);
    [[nodiscard]] std::string conditionPreview(const std::string& condition, const std::string& section = {}) const;
    // Les reponses du dialogue, par l'identifiant du point : Valider, Retirer le point.
    bool setConditionById(std::uint32_t id, const std::string& condition);
    bool removeBreakpointById(std::uint32_t id);
    // L'ESPION TAPE, sous la liste : le champ « Ajouter un espion : tape un nom »,
    // ses suggestions (les noms du projet qui commencent pareil) ; Entree ajoute ;
    // un nom inconnu : « X » n'existe pas dans le projet.
    [[nodiscard]] ui::InputText& watchField() noexcept { return *watchField_; }
    void typeWatch(const std::string& text);                  // le champ prend ce texte (focus, suggestions)
    bool submitWatchField(std::string* why = nullptr);        // Entree
    [[nodiscard]] std::vector<std::string> watchSuggestions(const std::string& typed) const;
    // « METTRE LA CONDITION nom = valeur » (Pourquoi ici, en pause sur un point
    // sans condition), a partir des valeurs de la ligne ; vide : pas de bouton.
    [[nodiscard]] std::string proposedCondition() const;
    bool applyProposedCondition();
    // ---- fin Lot API 8 (2e partie) ----

    // Un onglet Debogage a l'ecran prend F11 (Cycle suivant) : App et les
    // fenetres detachees le lui laissent avant le plein ecran.
    [[nodiscard]] static bool claimsKey(ui::Key key);
    // Le point d'arret provisoire d'« Executer jusqu'a la ligne » (d'un onglet
    // Debogage ouvert) : ni la liste ni les marges ne le montrent.
    [[nodiscard]] static bool isTransient(std::uint32_t id);

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    void            onPaintOverlay(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class BreakModel;
    class BreakTable;
    class WatchModel;
    class WatchTable;
    class TraceModel;
    class WatchField;                       // Lot API 8 (2e partie) : l'espion tape
    friend class WatchField;
    friend class BreakModel;
    friend class BreakTable;
    friend class WatchModel;
    friend class WatchTable;
    friend class TraceModel;

    struct Watch {
        std::string   path;                 // tel qu'ajoute
        std::string   type;
        std::string   text;                 // la derniere valeur lue
        bool          known{false};         // la simulation la connait
        bool          changed{false};       // a change depuis l'ajout
        std::uint64_t changedScan{0};
    };
    struct WatchRow {
        int                    watch{-1};   // l'espion (sa racine)
        int                    depth{0};
        project::members::Node node;
        bool                   expandable{false};
        bool                   open{false};
        std::string            value, since;
    };
    struct Hit {
        gfx::Rect   rect;
        std::string key, tip;
    };
    struct WriterSite {                     // une ligne du code qui l'ecrit (lecture du code)
        std::string   section, text;
        std::uint32_t line{0};
        std::string   unit;                 // 1.11.2 (D25) : l'unite de la section (vide : une section de tache)
    };

    [[nodiscard]] SimulationHost* host() const;
    [[nodiscard]] sim::Runtime*   runtime() const;
    [[nodiscard]] std::shared_ptr<const domain::Project> project() const;
    [[nodiscard]] bool shown() const;
    [[nodiscard]] bool isHit(std::uint32_t id) const;      // en pause sur ce point d'arret
    [[nodiscard]] const ui::Widget* focusedWidget();       // le widget qui a le focus (dans cette fenetre)
    [[nodiscard]] bool isInside(const ui::Widget* w) const;
    void status(const std::string& text) const;
    void journal(const std::string& kind, const std::string& text, const std::string& section = {}, int line = 0) const;
    bool ensureAttached();
    void transport(int action);

    [[nodiscard]] simdebug::StateFacts facts() const;
    void refreshBreakpoints();              // la liste, les marques du code
    void refreshWatches(bool values);       // les lignes (et leurs valeurs)
    void readWatchValues();
    void rebuildWatchRows();
    void refreshCodeMarks();                // les points, la ligne d'arret, les valeurs
    void refreshStack();
    void refreshTimes();
    void refreshWriter();
    void onBreakHit();
    void loadSection(domain::Index index, const std::string& key);
    [[nodiscard]] std::string valueText(const std::string& symbol) const;
    [[nodiscard]] bool readValue(const std::string& symbol, sim::Value& out) const;
    [[nodiscard]] std::vector<std::string> prefixes() const;   // unite, instance : ou chercher un nom du code
    bool locateWatch(const domain::Project& p, const std::string& path, project::members::Node& out) const;
    void toggleWatchRow(std::size_t row);
    void activate(const std::string& key);  // un clic sur une zone peinte
    [[nodiscard]] int hitAt(gfx::Point p) const;
    void askBreakpoint();
    void askWatch();
    void layoutParts();

    void paintBand(const ui::PaintContext& ctx);
    void paintTitle(const ui::PaintContext& ctx, const gfx::Rect& r, const std::string& title, const std::string& count,
                    const std::vector<std::pair<std::string, std::string>>& links);
    void paintStack(const ui::PaintContext& ctx);
    void paintSide(const ui::PaintContext& ctx);
    void paintTimes(const ui::PaintContext& ctx);

    Hosts                                   hosts_;
    ApiFrame*                               frame_{nullptr};
    int                                     continueLabel_{-1};    // "Simuler" (0) ou "Continuer" (1), a l'ecran
    ui::TableView*                          bpTable_{nullptr};
    std::shared_ptr<BreakModel>             bpModel_;
    ui::TableView*                          watchTable_{nullptr};
    std::shared_ptr<WatchModel>             watchModel_;
    ui::MultiLineText*                      code_{nullptr};
    ui::TableView*                          traceTable_{nullptr};
    std::shared_ptr<TraceModel>             traceModel_;
    ui::InputText*                          watchField_{nullptr};   // Lot API 8 (2e partie)

    // Les zones (refaites a chaque mise en page).
    gfx::Rect                               band_{}, bpTitle_{}, watchTitle_{}, stackRect_{}, side_{}, timesBox_{}, traceTitle_{};
    mutable std::vector<Hit>                hits_;
    int                                     hover_{-1};

    // Ce qui est montre.
    std::vector<SimBreakpoint>              bps_;
    // Lot API 8 : corrections des captures - "Section : ligne" abregee au milieu
    // pour la largeur de sa colonne (refait a chaque dessin ; vide : en entier).
    std::vector<std::string>                bpWhereShown_;
    std::string                             bpSignature_;
    std::vector<Watch>                      watches_;
    std::vector<WatchRow>                   watchRows_;
    std::set<std::string>                   expanded_;       // cles des membres deplies (minuscules)
    std::string                             codeSection_;    // le nom de simulateur de la section montree
    domain::Index                           codeIndex_{domain::kNoIndex};
    std::string                             codeInstance_;   // une section de bloc : l'instance dont on lit les valeurs
    std::size_t                             codeExecLine_{static_cast<std::size_t>(-1)};   // 0 = la premiere ; npos : aucune
    std::size_t                             codeCallLine_{static_cast<std::size_t>(-1)};   // un niveau appelant : la ligne de l'appel
    std::size_t                             notesFrom_{static_cast<std::size_t>(-1)};      // la premiere ligne des valeurs montrees
    std::vector<bool>                       commentAt_;      // un commentaire (* *) ouvert au debut de chaque ligne
    std::string                             nextSection_;    // en pause : ce que « Section suivante » executera
    std::vector<simdebug::StackLevel>       levels_;
    std::size_t                             level_{0};
    std::vector<SimSectionTime>             times_;
    std::vector<simdebug::TimeBar>          bars_;
    std::string                             timeSummary_;
    bool                                    plannedTrace_{false};   // la trace est l'ordre prevu (pas encore de cycle)
    std::string                             writerPath_;
    std::string                             writerValue_;
    bool                                    writerEngine_{false};
    SimLastWrite                            writerLast_;
    std::vector<WriterSite>                 writerSites_;
    std::string                             writerForPath_;   // la lecture du code est faite pour ce chemin
    simdebug::LastGesture                   gesture_{simdebug::LastGesture::None};
    std::uint32_t                           runToId_{0};      // le point d'arret provisoire de « jusqu'a la ligne »
    std::string                             runToText_;
    std::uint64_t                           hitSeen_{0};      // le passage deja montre (son cycle, sa ligne, son id)
    std::string                             hitKeySeen_;

    // Le rythme.
    double                                  now_{0.0}, nextLive_{0.0};
    int                                     stateSeen_{-1};
    std::uint64_t                           scanSeen_{~std::uint64_t{0}};
    std::uint64_t                           generationSeen_{~std::uint64_t{0}};
    std::uint64_t                           timesScan_{~std::uint64_t{0}};
    SimulationHost*                         hostSeen_{nullptr};
    bool                                    dirty_{true};     // tout se relit a la prochaine image a l'ecran
    bool                                    hitDirty_{false};
    bool                                    bpDirty_{true};
    core::ConnectionScope                   hostLinks_;
    core::ConnectionScope                   links_;
};

} // namespace app
