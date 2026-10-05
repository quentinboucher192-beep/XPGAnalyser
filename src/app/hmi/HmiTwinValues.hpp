// =============================================================================
//  app/hmi/HmiTwinValues.hpp - les valeurs simulees des jumeaux (lot 18)
// -----------------------------------------------------------------------------
//  CE QUE LES ESCLAVES SIMULES FONT TOUT SEULS, LIGNE PAR LIGNE. Une ligne par
//  variable IHM liee a un equipement qui a un jumeau (ou par registre anime ou
//  force sans variable), rangee sous son jumeau :
//
//    [x] Animer     la valeur bouge toute seule (decochee : ses reglages restent) ;
//    Mouvement      sinus, rampe, aleatoire, clignote, etapes... (un menu) ;
//    La barre       deux poignees placent la zone de mouvement (tirer une
//                   poignee, ou la bande entiere) ; le point est la valeur ; en
//                   valeur de la variable (sa mise a l'echelle), le brut suit ;
//    Forcer         un registre tenu a une valeur brute ; une bobine, un bit,
//                   un BOOL : libre, 0 ou 1. Une ecriture sur une case forcee
//                   est REFUSEE (exception 04, comptee) - l'IHM, l'outil Modbus,
//                   "Ecrire une valeur" ;
//    La courbe      les 60 dernieres secondes de la ligne ; en bas, les courbes
//                   en pistes (la bande de la ligne choisie en clair, le force
//                   en orange).
//
//  Deux endroits : Configuration > Equipements > Valeurs simulees (tout, avec
//  la fiche a droite) et IHM > Simulation, onglet Jumeaux (le meme, serre).
//  Le controleur (TwinValuesController) fait les lignes et les commandes :
//  chaque changement est une commande du projet (Ctrl+Z) ; une poignee tiree
//  n'en fait qu'une, au lacher.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiTwin.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ui {
class DropDown;
class InputText;
class PopupMenu;
} // namespace ui

namespace app {

class EquipmentHost;

class HmiTwinValues final : public ui::Widget {
public:
    struct Line {
        // 1.11.5 : Node - un noeud d'une variable structuree (Four1, Four1.Zones, Four1.Zones[2]) ;
        // les lignes s'y rangent a toute profondeur, et il se replie.
        enum class Kind : std::uint8_t { Group, Row, Add, Node };
        Kind        kind{Kind::Row};
        int         depth{0};           // 1.11.5 : la profondeur dans l'arbre (0 : sous l'esclave)
        std::string node;               // 1.11.5 : Node - sa cle ("equipement|Four1.Zones") ; Row : celle de son parent
        std::string label;              // 1.11.5 : ce qui se montre (le dernier morceau : Temp, [2]) ; vide : title
        std::size_t count{0};           // 1.11.5 : Node - les valeurs qu'il contient
        std::string equipment;          // son equipement
        std::string key;                // Row : l'adresse (la cle de la ligne dans l'equipement)
        std::string title, sub;         // Row : la variable (ou "registre 40011"), dessous ; Group : le nom, l'etat
        std::string address, type;
        bool        boolean{false};
        bool        animated{false};    // la case Animer
        bool        hasBehavior{false};
        std::string kindLabel;          // "sinus" ; "\xE2\x80\x94" : aucun
        int         barMode{0};         // 0 le point seul, 1 une bande [lo, hi], 2 une valeur (constante), 3 clignote (a 1 de 0 a hi s)
        bool        bandFixed{false};   // la bande se lit mais ne se tire pas (etapes)
        double      barLo{0}, barHi{100};
        double      lo{0}, hi{0};
        std::string unit;               // "s" (clignote) ; sinon vide
        std::string period;             // "30 s"
        bool        forced{false};
        std::string forcedText;         // "5100"
        double      forcedEng{0};       // la valeur forcee, pour la variable (la marque sur la barre)
        int         boolForce{-1};      // -1 libre, 0, 1
        bool        warn{false};
        std::string warnText;           // "ecrite par le script Regulation"
        std::string note;               // ce que la barre dit a la place (recopie, compteur...)
        bool        running{true};      // le jumeau est en marche
        std::string rawHint;            // "brut 80" : l'infobulle d'une poignee
        std::function<std::string(double)> rawOf;   // la valeur brute d'une valeur de la variable (l'infobulle)
        std::string tooltip;
    };
    enum class Part : std::uint8_t { None, Check, Kind, Low, High, Band, Bar, ForceBox, ForceValue, Free, Zero, One, Row, Add, Group, Node, Edit };

    HmiTwinValues(std::string id = {}, bool compact = false);
    ~HmiTwinValues() override;

    void setLines(std::vector<Line> lines);
    [[nodiscard]] const std::vector<Line>& lines() const noexcept { return lines_; }
    // Le direct : le texte de la valeur, dessous ; le point de la barre (valeur de la variable).
    void setLive(const std::string& equipment, const std::string& key, std::string value, std::string sub, std::optional<double> v, bool forced);
    // Un point de courbe (t : hmi::twin::now()).
    void sample(const std::string& equipment, const std::string& key, double t, double v, bool forced);
    void setNow(double t);
    void select(const std::string& equipment, const std::string& key);
    [[nodiscard]] std::pair<std::string, std::string> selected() const { return {selEquip_, selKey_}; }
    void setStatus(std::string text) { status_ = std::move(text); invalidate(); }
    [[nodiscard]] bool compact() const noexcept { return compact_; }

    // Les courbes (en bas) : ouvertes, la fenetre (30, 60, 300 s), figees, la bande de la ligne choisie.
    void setCurvesOpen(bool open);
    void setWindow(double seconds);
    void setFrozen(bool frozen);
    [[nodiscard]] bool curvesOpen() const noexcept { return curvesOpen_; }
    [[nodiscard]] double window() const noexcept { return window_; }
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }
    [[nodiscard]] std::size_t samples(const std::string& equipment, const std::string& key) const;

    // Les filtres (en haut, pas en mode serre).
    [[nodiscard]] ui::DropDown* twinFilter() noexcept { return twinBox_; }
    [[nodiscard]] ui::DropDown* showFilter() noexcept { return showBox_; }
    [[nodiscard]] ui::InputText* searchBox() noexcept { return searchBox_; }

    // Pour les scripts et les tests : ou est dessinee une partie d'une ligne (faux : pas montree).
    [[nodiscard]] bool partRect(const std::string& equipment, const std::string& key, Part, gfx::Rect& out);
    // Ou tombe une valeur (de la variable) sur la barre de la ligne.
    [[nodiscard]] bool valueX(const std::string& equipment, const std::string& key, double v, float& x);
    [[nodiscard]] const Line* line(const std::string& equipment, const std::string& key) const;
    // 1.11.5 : LES BORNES AU CLAVIER. Le crayon au bout de la barre (ou un double-clic sur la
    // zone) ouvre un champ : « 20 ; 80 » (une zone), « 50 » (une constante), « 1,5 » (a 1
    // pendant, en s) ; Entree l'applique (bandChanged, comme une poignee tiree), Echap annule.
    bool openZoneEditor(const std::string& equipment, const std::string& key);
    [[nodiscard]] bool zoneEditorOpen() const noexcept;
    [[nodiscard]] ui::InputText* zoneEditor() noexcept;
    // Le texte du champ, lu : vrai si la zone (lo, hi) se lit pour ce mode de barre.
    static bool parseZone(std::string_view text, int barMode, double& lo, double& hi);
    // 1.11.5 : un noeud de l'arbre (sa cle : Line::node) - ouvert ou replie.
    void setNodeOpen(const std::string& node, bool open);
    [[nodiscard]] bool nodeOpen(const std::string& node) const { return folded_.count(node) == 0; }
    // Les lignes montrees (pas repliees), pour les tests.
    [[nodiscard]] std::size_t shownLines() const;
    // 1.11.6 : LE CLIC DROIT sur une ligne - Tout deplier, Deplier, Replier, Tout replier,
    // Forcer, Deforcer (la ligne, ou toutes les valeurs sous un esclave ou un noeud).
    enum MenuItem : int { MExpandAll = 1, MExpand, MCollapse, MCollapseAll, MForce, MUnforce };
    bool openContextMenu(std::size_t line, gfx::Point at);
    [[nodiscard]] ui::PopupMenu* contextMenu() noexcept { return ctxMenu_; }
    bool contextAction(std::size_t line, int item);        // ce que fait l'entree (les tests, les scripts)
    void setAllOpen(bool open);
    // Les lignes de valeurs sous une ligne (un esclave, un noeud) ; une valeur : elle-meme.
    [[nodiscard]] std::vector<std::size_t> rowsUnder(std::size_t line) const;
    // Ou est dessinee une ligne (un esclave, un noeud...) - pour les scripts ; faux : pas montree.
    [[nodiscard]] bool lineRectOf(std::size_t line, gfx::Rect& out) const { return lineRect(line, out); }
    // 1.11.6 : « Sur la vue actuelle » (l'onglet de la simulation) : la case de la barre.
    [[nodiscard]] ui::Checkbox* viewBox() noexcept { return viewBox_; }

    // Les signaux (equipement, cle...). Une poignee tiree : au lacher.
    const core::SignalPtr<const std::string&, const std::string&, bool>           animateToggled = core::Signal<const std::string&, const std::string&, bool>::create();
    const core::SignalPtr<const std::string&, const std::string&, const std::string&> kindChosen = core::Signal<const std::string&, const std::string&, const std::string&>::create();
    const core::SignalPtr<const std::string&, const std::string&, double, double> bandChanged = core::Signal<const std::string&, const std::string&, double, double>::create();
    const core::SignalPtr<const std::string&, const std::string&, bool>           forceToggled = core::Signal<const std::string&, const std::string&, bool>::create();
    const core::SignalPtr<const std::string&, const std::string&, int>            boolForced = core::Signal<const std::string&, const std::string&, int>::create();
    const core::SignalPtr<const std::string&, const std::string&>                 rowChosen = core::Signal<const std::string&, const std::string&>::create();
    const core::SignalPtr<const std::string&, const std::string&>                 rowActivated = core::Signal<const std::string&, const std::string&>::create();
    const core::SignalPtr<const std::string&>                                      addRequested = core::Signal<const std::string&>::create();
    // 1.11.6 : forcer (vrai) ou deforcer plusieurs valeurs d'un esclave d'un coup (le clic droit).
    const core::SignalPtr<const std::string&, const std::vector<std::string>&, bool> forceMany =
        core::Signal<const std::string&, const std::vector<std::string>&, bool>::create();

    [[nodiscard]] gfx::Rect eventBounds() const override;

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Cols {
        float check{0}, name{0}, nameW{0}, addr{0}, type{0}, kind{0}, kindW{0}, bar{0}, barW{0}, period{0}, force{0}, forceW{0}, value{0}, valueW{0}, spark{0}, sparkW{0};
    };
    struct Sample {
        double t{0};
        float  v{0};
        bool   forced{false};
    };
    struct Live {
        std::string text, sub;
        std::optional<double> v;
        bool forced{false};
    };
    [[nodiscard]] Cols cols() const;
    [[nodiscard]] gfx::Rect listArea() const;
    [[nodiscard]] gfx::Rect curvesArea() const;
    [[nodiscard]] float lineHeight(const Line&) const;
    [[nodiscard]] float contentHeight() const;
    [[nodiscard]] std::vector<std::size_t> curveLines() const;
    [[nodiscard]] bool lineRect(std::size_t i, gfx::Rect& out) const;
    [[nodiscard]] gfx::Rect partOf(std::size_t i, const gfx::Rect& row, Part) const;
    [[nodiscard]] float xOf(const Line&, const gfx::Rect& bar, double v) const;
    [[nodiscard]] double valueAtX(const Line&, const gfx::Rect& bar, float x) const;
    [[nodiscard]] std::pair<std::size_t, Part> hit(gfx::Point p) const;
    [[nodiscard]] std::string lineKey(const Line& l) const { return l.equipment + "|" + l.key; }
    [[nodiscard]] bool folded(const std::string& equipment) const { return folded_.count(equipment) != 0; }
    // 1.11.5 : les lignes cachees par un esclave ou un noeud replie (refait quand les lignes ou
    // les replis changent ; lineHeight le lit).
    void computeHidden();
    void unfoldAncestors(std::size_t row);
    // La ligne choisie (deux variables sur la meme case : celle qu'on a cliquee).
    [[nodiscard]] std::size_t selIndex() const;
    void paintRow(const ui::PaintContext&, std::size_t i, const gfx::Rect& r, const Cols& c);
    void paintBar(const ui::PaintContext&, const Line&, const Live*, const gfx::Rect& bar, bool dragging);
    void paintCurves(const ui::PaintContext&, const gfx::Rect& area);
    void paintSpark(const ui::PaintContext&, const Line&, const gfx::Rect& r);
    void openKindMenu(std::size_t i, gfx::Point at);

    bool                                     compact_{false};
    std::vector<Line>                        lines_;
    std::map<std::string, Live>              live_;
    std::map<std::string, std::deque<Sample>> hist_;
    std::set<std::string>                    folded_;
    std::vector<char>                        hidden_;   // 1.11.5 : une par ligne
    std::string                              selEquip_, selKey_, selTitle_;
    std::string                              status_;
    double                                   now_{0};
    double                                   frozenAt_{0};
    bool                                     curvesOpen_{true};
    double                                   window_{60};
    bool                                     frozen_{false};
    bool                                     bandOn_{true};
    float                                    scroll_{0};
    ui::PaintedScrollBar                     sbar_;   // 1.11.4 : la barre se tire
    ui::PopupMenu*                           ctxMenu_{nullptr};   // 1.11.6 : le clic droit
    std::size_t                              ctxLine_{std::string::npos};
    ui::Checkbox*                            viewBox_{nullptr};   // 1.11.6 : sur la vue actuelle
    ui::DropDown*                            twinBox_{nullptr};
    ui::DropDown*                            showBox_{nullptr};
    ui::InputText*                           searchBox_{nullptr};
    ui::PopupMenu*                           menu_{nullptr};
    // 1.11.5 : le champ des bornes (cache tant qu'il n'est pas ouvert), et sa ligne.
    ui::InputText*                           zoneEdit_{nullptr};
    std::string                              zoneEquip_, zoneKey_;
    int                                      zoneMode_{0};
    void closeZoneEditor(bool apply);
    std::string                              menuEquip_, menuKey_;
    // Tirer une poignee, la bande.
    struct Drag {
        bool        on{false};
        std::size_t line{0};
        Part        part{Part::None};
        float       startX{0};
        double      lo{0}, hi{0};         // au depart
        double      curLo{0}, curHi{0};   // maintenant
        std::string equipment, key;
    } drag_;
    std::optional<std::pair<std::size_t, Part>> hover_;
    core::ConnectionScope                    links_;
};

// ---------------------------------------------------------------- le controleur ---
//  Les lignes (le projet), le direct (les jumeaux de l'hote), et ce que font les
//  clics : chaque changement du projet passe par `apply` (une commande, Ctrl+Z).
class TwinValuesController {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    TwinValuesController(hmi::DocumentPtr doc, Apply apply);
    void setHost(std::function<EquipmentHost*()> host) { host_ = std::move(host); }
    void attach(HmiTwinValues& widget);
    // Ce qu'il dit (la barre d'etat) ; error : refuse.
    std::function<void(const std::string&, bool)> say;
    // Apres un changement du projet, ou un autre choix de ligne (la fiche se refait).
    std::function<void()> changed;

    void refresh();                     // les lignes (le projet a change)
    void tick();                        // le direct (dix fois par seconde)

    // "tous" (vide) ou un equipement ; montrer : 0 tout, 1 les lignes animees ou forcees.
    void setTwinFilter(const std::string& equipment);
    void setShow(int mode);
    void setSearch(const std::string& text);
    // 1.11.6 : sur la vue actuelle - seulement les valeurs dont la variable est lue par la vue
    // montree (`covers`) ; nul : rien a filtrer.
    void setViewFilter(std::function<bool(std::string_view)> covers);
    void setOnlyView(bool on);
    [[nodiscard]] bool onlyView() const noexcept { return onlyView_; }
    // 1.11.6 : forcer (a leur valeur du moment) ou deforcer ces valeurs d'un esclave - une commande.
    bool forceRows(const std::string& equipment, const std::vector<std::string>& addresses, bool on, std::string* why = nullptr);
    // 1.11.7 : le mouvement d'une ligne pose (ou retire : vide) d'un coup - une commande ; ses
    // bornes en valeur BRUTE (le registre). Le forcage commun de l'onglet Variables IHM.
    bool setBehavior(const std::string& equipment, const std::string& address, const std::optional<hmi::Behavior>& b,
                     std::string* why = nullptr);

    // Les actions (la ligne, la fiche, les scripts, les tests).
    bool setAnimated(const std::string& equipment, const std::string& address, bool on, std::string* why = nullptr);
    bool setKind(const std::string& equipment, const std::string& address, const std::string& kind, std::string* why = nullptr);
    // La bande [lo, hi] en valeur de la variable (sa mise a l'echelle) ; clignote : la duree a 1 (hi).
    bool setBand(const std::string& equipment, const std::string& address, double lo, double hi, std::string* why = nullptr);
    // "periode", "retard", "type", "source", "min", "max" (valeur de la variable), "a", "b" (brut).
    bool setField(const std::string& equipment, const std::string& address, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Forcer a une valeur brute ("5100", "1", "TRUE") ; vide ou "libre" : deforcer.
    bool setForced(const std::string& equipment, const std::string& address, const std::string& raw, std::string* why = nullptr);
    bool unforceAll(std::string* why = nullptr);
    bool animateAll(bool on, std::string* why = nullptr);
    // Un registre de plus (anime) : la premiere case libre des zones de l'equipement ; rend son adresse.
    std::string addRegister(const std::string& equipment, const std::string& address = {}, std::string* why = nullptr);
    // La barre d'une ligne : de ... a ... (le reglage de l'ecran, pas du projet).
    bool setBarRange(const std::string& equipment, const std::string& address, double lo, double hi);

    void select(const std::string& equipment, const std::string& address);
    [[nodiscard]] std::string selectedEquipment() const { return selEquip_; }
    [[nodiscard]] std::string selectedAddress() const { return selKey_; }
    [[nodiscard]] std::optional<hmi::twin::ValueRow> row(const std::string& equipment, const std::string& address) const;
    // La fiche de la ligne choisie (Configuration > Equipements).
    void properties(std::vector<ui::PropertyGrid::Category>& cats);

    struct Counts {
        std::size_t twins{0}, running{0}, rows{0}, animated{0}, forced{0}, refused{0};
    };
    [[nodiscard]] Counts counts() const;
    [[nodiscard]] HmiTwinValues* widget() const noexcept { return widget_; }

private:
    struct RowRef {
        std::string          equipment;
        hmi::twin::ValueRow  row;
        bool                 low{true};
    };
    [[nodiscard]] EquipmentHost* host() const { return host_ ? host_() : nullptr; }
    [[nodiscard]] std::string simulatedReadsTail() const;   // 1.9 : " - 2 equipements lus en simule"
    [[nodiscard]] const hmi::Equipment* equipment(const std::string& name) const;
    [[nodiscard]] const RowRef* ref(const std::string& equipment, const std::string& address) const;
    bool fail(std::string m, std::string* why);
    bool changeBehaviors(const std::string& equipment, const std::string& label, const std::function<bool(std::vector<hmi::Behavior>&, std::string&)>& fn,
                         std::string* why, const std::string& mergeKey = {});
    bool changeForcings(const std::string& equipment, const std::string& label, const std::function<bool(std::vector<hmi::Forcing>&, std::string&)>& fn,
                        std::string* why);
    void buildLines();

    hmi::DocumentPtr                    doc_;
    Apply                               apply_;
    std::function<EquipmentHost*()>     host_;
    HmiTwinValues*                      widget_{nullptr};
    std::vector<RowRef>                 refs_;
    std::string                         filter_;
    int                                 show_{0};
    std::string                         search_;
    std::function<bool(std::string_view)> covers_;                 // 1.11.6 : sur la vue actuelle
    bool                                onlyView_{false};
    std::string                         selEquip_, selKey_;
    std::map<std::string, std::pair<double, double>> bars_;   // "equipement|adresse" -> la barre reglee
    std::map<std::string, std::pair<double, double>> autoBars_;
    double                              lastSample_{-1};
    std::uint64_t                       shownRevision_{~0ull};
    core::ConnectionScope               links_;
};

} // namespace app
