// =============================================================================
//  app/hmi/HmiObjectAlarmPanes.hpp - l'interface des alarmes des objets (1.9)
// -----------------------------------------------------------------------------
//  D'apres la maquette M (scenes A1 a A5) :
//    A1  l'onglet Alarmes d'un symbole : la liste, la fiche d'une alarme,
//        "Creer des alarmes" (suggestSymbolAlarms), l'apercu du groupe genere
//        sur une instance choisie ;
//    A2  la section "Alarmes de l'objet" de l'inspecteur d'un objet pose
//        (instance ou objet du synoptique) : case Active, champs, repere
//        SURCHARGE, infobulle avec la valeur du symbole, revenir, tout revenir ;
//        puis "Variables publiques de l'objet" (lecture seule) ;
//    A3  Configuration > Alarmes : les sous-categories generees (Symboles, puis
//        Objets du synoptique - decision 11 h 55 -, par vue, par objet) ;
//    A4  le selecteur de groupes du filtre des objets d'alarmes ;
//    A5  la cloche et le nombre d'alarmes par defaut de la bibliotheque.
//
//  Ici : ce que montrent ces ecrans (des lignes calculees depuis le projet) et
//  ce qu'ils changent - toujours par hmi::changeProject (Ctrl+Z). Les volets
//  (widgets) s'en servent ; les tests de l'editeur aussi, sans fenetre.
//  Les conditions et textes surcharges s'ecrivent dans les termes du symbole ;
//  la forme developpee (sur l'instance) est montree a cote, en lecture seule.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiObjectAlarms.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <memory>

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace app::objalarms {

using Apply = std::function<void(core::CommandPtr)>;

// Le libelle d'un champ surchargeable (overrideFields) : "Condition", "Priorit\xC3\xA9"...
[[nodiscard]] std::string fieldLabel(std::string_view field);
// La valeur d'un champ d'une definition, en texte ("2 - Haute", "5000", "oui").
[[nodiscard]] std::string fieldValue(const hmi::AlarmDef&, std::string_view field);
// Une valeur saisie verifiee pour un champ, ecrite dans `d` ; faux (et `why`) : refusee.
bool parseField(hmi::AlarmDef& d, std::string_view field, const std::string& value, std::string* why);

// ======================================================= A1 : le symbole ====
// Les changements d'une alarme d'un symbole (vue de role symbole `symbol`).
// `field` : nom, condition, message, priorite, categorie, groupe, delai,
// acquittement, consigne, description. Renommer : les surcharges suivent.
std::string addSymbolAlarm(const hmi::DocumentPtr&, const Apply&, hmi::Id symbol, std::string name = {}, std::string* why = nullptr);
bool setSymbolAlarmField(const hmi::DocumentPtr&, const Apply&, hmi::Id symbol, const std::string& alarm, const std::string& field,
                         const std::string& value, std::string* why = nullptr);
bool deleteSymbolAlarm(const hmi::DocumentPtr&, const Apply&, hmi::Id symbol, const std::string& alarm);
std::string duplicateSymbolAlarm(const hmi::DocumentPtr&, const Apply&, hmi::Id symbol, const std::string& alarm);
bool moveSymbolAlarm(const hmi::DocumentPtr&, const Apply&, hmi::Id symbol, const std::string& alarm, int delta);
// "Creer N alarmes" (le panneau Creer des alarmes) : une seule commande.
std::size_t createSymbolAlarms(const hmi::DocumentPtr&, const Apply&, hmi::Id symbol, const std::vector<hmi::AlarmDef>&);

// Le panneau "Creer des alarmes" : depuis un parametre (son type : ViewParam
// apres la fusion avec F ; ici donne par l'appelant), les alarmes proposees ;
// `taken` : celles dont le nom existe deja dans le symbole (grisees "deja : ...").
struct Proposal {
    hmi::AlarmDef def;
    bool          taken{false};
};
[[nodiscard]] std::vector<Proposal> proposals(const hmi::Project&, const hmi::View& symbol, std::string_view param,
                                              std::string_view type);

// L'apercu du groupe genere sur une instance (A1, a droite).
struct PreviewInstance {
    hmi::Id     view{hmi::kNoId}, object{hmi::kNoId};
    std::string path;              // "Vue_Pompes.Pompe_3"
    std::size_t alarms{0};
    std::size_t overriddenFields{0};
};
struct Preview {
    std::string                  instance;   // "Vue_Pompes.Pompe_3" ("" : aucune instance)
    std::string                  arguments;  // "Moteur := Pompes[3]; Nom := 'P3'"
    std::string                  group;      // le groupe interne
    std::string                  declared;   // les groupes declares, "Pompage"
    std::vector<hmi::ObjectAlarm> alarms;    // developpees, surcharges comprises
    std::vector<std::string>     publicVars; // "Vue_Pompes.Pompe_3.AlarmCount"...
    std::vector<PreviewInstance> instances;  // "Les N instances"
};
[[nodiscard]] Preview symbolPreview(const hmi::Project&, const hmi::View& symbol, std::string_view instance = {});
// Le resume de la barre d'etat de A1 : "Sym_Pompe \xC2\xB7 symbole \xC2\xB7 2 param\xC3\xA8tres (...) \xC2\xB7 3 alarmes \xC2\xB7 3 instances".
[[nodiscard]] std::string symbolStatus(const hmi::Project&, const hmi::View& symbol);

// ================================================ A2 : l'objet pose ====
struct FieldRow {
    std::string field;        // overrideFields
    std::string label;        // "Priorit\xC3\xA9"
    std::string value;        // la valeur sur cet objet (condition, textes : dans les termes du symbole)
    std::string developed;    // condition et textes : la forme developpee (lecture seule) ; sinon vide
    std::string base;         // la valeur du symbole (ou de la bibliotheque)
    bool        overridden{false};
    std::string tip;          // "Priorit\xC3\xA9 surcharg\xC3\xA9" "e sur Pompe_3 / Valeur du symbole Sym_Pompe : 3 - Moyenne"
};
struct AlarmRow {
    std::string localName;    // Defaut_Thermique
    std::string path;         // le chemin dans le symbole ("" : l'objet pose)
    std::string symbol;       // le symbole qui la declare ("" : la bibliotheque) - ses parametres pour la pastille fx
    std::string name;         // le nom genere, Vue_Pompes.Pompe_3.Defaut_Thermique
    bool        active{true};
    bool        applicable{true};
    std::string why;          // "sans objet : ..." (bibliotheque)
    int         priority{2};
    std::string category;
    std::size_t overridden{0};
    std::vector<FieldRow> fields;
};
struct PublicVarRow { std::string path, type, meaning; };
struct ObjectSection {
    bool                      carries{false};     // un objet du synoptique ou une instance
    std::string               group;              // Vue_Pompes.Pompe_3
    std::string               summary;            // "3 du symbole \xC2\xB7 2 actives \xC2\xB7 3 champs surcharg\xC3\xA9s"
    std::vector<AlarmRow>     alarms;
    std::vector<PublicVarRow> publicVars;
};
[[nodiscard]] ObjectSection objectSection(const hmi::Project&, const hmi::View&, const hmi::Object&);

// La surcharge d'un champ (dans les termes du symbole) ; la valeur du symbole
// donnee : la surcharge du champ disparait (revenir). Une commande chacune.
bool setOverride(const hmi::DocumentPtr&, const Apply&, hmi::Id view, hmi::Id object, const std::string& path,
                 const std::string& alarm, const std::string& field, const std::string& value, std::string* why = nullptr);
// Cocher / decocher une alarme sur l'objet (une surcharge "active").
bool setActive(const hmi::DocumentPtr&, const Apply&, hmi::Id view, hmi::Id object, const std::string& path, const std::string& alarm,
               bool active);
// Revenir a la valeur du symbole : un champ ; `field` vide : Tout revenir.
bool revert(const hmi::DocumentPtr&, const Apply&, hmi::Id view, hmi::Id object, const std::string& path, const std::string& alarm,
            const std::string& field = {});

// Les categories de l'inspecteur : "Alarmes de l'objet" (un resume, puis une
// sous-categorie par alarme : Etat - active, desactivee sur cet objet, N
// SURCHARGES -, Active, ses champs avec la forme developpee en lecture seule,
// et "Revenir a la valeur du symbole" : un champ surcharge ou Tout revenir ;
// l'aide d'un champ surcharge commence par SURCHARGE et donne la valeur du
// symbole), puis "Variables publiques de l'objet" (lecture seule). Des noms
// stables : la grille garde ce qui est deplie d'une saisie a l'autre. Vide :
// l'objet ne porte pas d'alarmes. Les commits : setOverride, setActive, revert.
// La condition (et sa forme developpee) porte la pastille fx (decision 7) ;
// `plc` : les globales de l'automate jugees (nul : seulement l'IHM).
[[nodiscard]] std::vector<ui::PropertyGrid::Category> inspectorCategories(const hmi::DocumentPtr&, const Apply&, hmi::Id view,
                                                                          hmi::Id object, const domain::Project* plc = nullptr);

// ============================== 1.9 (decision 7) : la pastille fx des conditions ====
// Une condition d'alarme est une expression (un booleen) : "" si elle peut
// marcher, sinon le premier probleme (pastille fx rouge, infobulle) - le controle
// de l'inspecteur (hmiExpressionError) avec les parametres de `where` connus : le
// symbole qui declare l'alarme, ou la vue de l'objet pour une forme developpee.
// Les textes a trous (message, consigne) ne sont pas des expressions.
[[nodiscard]] std::string conditionError(const hmi::Project&, const hmi::View& where, const std::string& condition,
                                         const domain::Project* plc = nullptr,
                                         const std::set<std::string, std::less<>>* plcUpperNames = nullptr,
                                         const hmi::exprcheck::PlcPaths* plcPaths = nullptr);
// L'infobulle d'une condition dans un tableau : "Expression : ... / Erreur : ...".
[[nodiscard]] std::string conditionTip(const std::string& condition, const std::string& error);
// La petite pastille fx devant une condition dans un tableau (CellStyle::customIcon,
// teinte par iconTone : Accent, Error si impossible) ; paintFx la dessine
// (TableView::setIconPainter).
inline constexpr int kFxIcon = 1901;
void paintFx(gfx::IRenderer&, int icon, const gfx::Rect& box, gfx::Color color);
// Le badge fx d'un objet pose (liste des objets) pour ses alarmes : 0 rien ; 1 une
// condition surchargee sur l'objet ; 2 une alarme generee de l'objet a une
// condition impossible (rouge).
[[nodiscard]] int conditionBadge(const hmi::Project&, const hmi::View&, const hmi::Object&, const domain::Project* plc = nullptr,
                                 const std::set<std::string, std::less<>>* plcUpperNames = nullptr,
                                 const hmi::exprcheck::PlcPaths* plcPaths = nullptr);

// =============================================== A3 : Configuration > Alarmes ====
enum class RowKind : std::uint8_t { Category, View, Object, Alarm };
struct TreeRow {
    RowKind     kind{RowKind::Alarm};
    int         depth{0};
    std::string label;        // "Symboles", "Vue_Pompes", "Pompe_3", "Surintensite"
    std::string detail;       // objet : "Sym_Pompe \xC2\xB7 Vue_Pompes.Pompe_3.*" ; alarme : le nom complet
    std::string count;        // "3 \xC2\xB7 2 actives"
    hmi::Id     view{hmi::kNoId}, object{hmi::kNoId};
    std::string path, alarm;  // une alarme : son chemin et son nom court
    bool        active{true}, overridden{false};
    int         priority{0};
    std::string category, groups, condition;   // "Pompage \xC2\xB7 Vue_Pompes.Pompe_3", condition developpee
};
// L'arbre des alarmes generees : "Symboles" puis "Objets du synoptique" (les
// alarmes ecrites par le client d'abord), par vue, par objet.
[[nodiscard]] std::vector<TreeRow> generatedTree(const hmi::Project&);
// "23 du projet \xC2\xB7 31 g\xC3\xA9n\xC3\xA9r\xC3\xA9" "es par les objets (24 actives) \xC2\xB7 3 surcharges"
[[nodiscard]] std::string generatedSummary(const hmi::Project&);
// Le double-clic : la vue et l'objet a ouvrir (faux : une ligne de categorie).
bool openTarget(const TreeRow&, hmi::Id& view, hmi::Id& object);

// ========================================== A4 : le filtre des objets d'alarmes ====
struct GroupChoice {
    std::string section;      // "Groupes du projet", "Groupes des objets", "Symboles"
    int         depth{0};
    std::string label;        // "Armoire A", "Vue_Pompes \xC2\xB7 tous ses objets", "Pompe_3", "Tous les objets du symbole Sym_Pompe"
    std::string value;        // ce que la case ecrit : "Armoire A", "Vue_Pompes.*", "Vue_Pompes.Pompe_3", "symbole:Sym_Pompe"
    std::string detail;       // "6 alarmes", "motif Vue_Pompes.*", "Sym_Pompe", "symbole:Sym_Pompe"
};
[[nodiscard]] std::vector<GroupChoice> groupChoices(const hmi::Project&, std::string_view search = {});
// Le filtre "a; b" avec `item` ajoute (on) ou retire.
[[nodiscard]] std::string toggleFilter(std::string_view filter, std::string_view item, bool on);
[[nodiscard]] bool filterHas(std::string_view filter, std::string_view item);
// Les alarmes possibles d'un filtre (projet + generees cochees) : la barre d'etat de A4.
[[nodiscard]] std::size_t filterReach(const hmi::Project&, std::string_view filter);

// ============================================ A5 : la bibliotheque ====
// Le nombre d'alarmes par defaut d'un genre (la cloche de sa tuile) ; 0 : aucune.
[[nodiscard]] int libraryAlarmCount(hmi::Kind);
// La section A5 d'un objet du synoptique : "par d\xC3\xA9" "faut \xC2\xB7 3 actives \xC2\xB7 2 sans objet".
[[nodiscard]] std::string librarySummary(const hmi::Object&);
// La pastille de la tuile (coin haut droit) : une cloche et le nombre (rien a 0) ;
// une tuile de symbole : le nombre de ses alarmes.
void paintBell(const ui::PaintContext&, int count, const gfx::Rect& tile);

} // namespace app::objalarms

namespace app {

// ============================================ A1 : le volet Alarmes d'un symbole ====
//  La barre (Ajouter, Creer des alarmes..., Dupliquer, Supprimer, Monter,
//  Descendre), le tableau (Alarme, Condition, Message, Priorite, Categorie,
//  Delai) et, a droite, la fiche "Alarme du symbole" puis "Dans les instances",
//  "Creer des alarmes" (depuis un parametre) et "Apercu du groupe genere" (sur
//  l'instance choisie). Le sous-onglet Alarmes du document d'un symbole.
class HmiSymbolAlarmsPane final : public ui::Widget {
public:
    using Apply = objalarms::Apply;
    HmiSymbolAlarmsPane(std::string id, hmi::DocumentPtr doc, hmi::Id symbol, Apply apply);

    void refresh();
    [[nodiscard]] std::string selectedAlarm() const;
    void selectAlarm(const std::string& name);
    [[nodiscard]] std::size_t rowCount() const noexcept { return names_.size(); }
    // Le panneau Creer des alarmes : le parametre et son type (ViewParam::type
    // apres la fusion avec F) ; createChecked() cree les propositions libres.
    void setProposalSource(std::string param, std::string type);
    std::size_t createProposals();
    // L'apercu : l'instance ("Vue.Objet" ; vide : la premiere).
    void setPreviewInstance(std::string instance);
    [[nodiscard]] const objalarms::Preview& preview() const noexcept { return preview_; }

    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& status() const noexcept { return statusText_; }
    // Le programme de l'automate (la pastille fx juge ses globales) ; vide : l'IHM seule.
    std::function<const domain::Project*()> plc;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    hmi::DocumentPtr  doc_;
    hmi::Id           symbol_;
    Apply             apply_;
    HmiToolStrip*     tools_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::TableView*    table_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<std::string> names_;
    std::string       param_, type_, instance_, statusText_;
    objalarms::Preview preview_;
    core::ConnectionScope links_;
};

// ============================================ A3 : Configuration > Alarmes, les generees ====
//  L'arbre des alarmes generees sous celles du projet : Symboles, puis Objets du
//  synoptique (etiquette GENEREES), par vue, par objet ; une ligne par alarme
//  (case cochee = active, cadenas, SURCHARGE), groupes (declare \xC2\xB7 interne),
//  condition developpee. Espace (ou setActiveAt) : cocher / decocher (une
//  surcharge) ; double-clic : `open` (l'objet dans sa vue).
class HmiGeneratedAlarmsTable final : public ui::Widget {
public:
    using Apply = objalarms::Apply;
    HmiGeneratedAlarmsTable(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();
    [[nodiscard]] const std::vector<objalarms::TreeRow>& rows() const noexcept { return rows_; }
    [[nodiscard]] int  rowOf(const std::string& generatedName) const;   // -1 : aucune
    bool               setActiveAt(int row, bool active);
    bool               openAt(int row);
    std::function<void(hmi::Id view, hmi::Id object)> open;              // l'hote : l'objet dans sa vue
    std::function<const domain::Project*()>           plc;               // la pastille fx (vide : l'IHM seule)
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] const std::string& summary() const noexcept { return summary_; }
    // 1.9 (chantier U) : LA FICHE de l'alarme generee choisie, a droite (A3) :
    // "Generee par Sym_Pompe, sur Pompe_3", Ouvrir (l'objet dans sa vue, le
    // symbole), puis ses champs comme dans la section Alarmes de l'objet (une
    // surcharge : le repere et le bouton de retour ; Tout revenir au bout de la
    // ligne Etat). Suit la ligne choisie ; `row` -1 : aucune (la table seule).
    void showSheet(int row);
    [[nodiscard]] ui::PropertyGrid& sheet() noexcept { return *sheet_; }
    std::function<void(hmi::Id symbol)> openSymbol;                      // l'hote : le symbole

protected:
    void onLayout() override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    hmi::DocumentPtr doc_;
    Apply            apply_;
    ui::TableView*   table_{nullptr};
    ui::PropertyGrid* sheet_{nullptr};   // 1.9 (chantier U)
    std::string      sheetName_;         // le nom complet de l'alarme montree ("" : aucune)
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<objalarms::TreeRow> rows_;
    std::string      summary_;
    core::ConnectionScope links_;
};

// Les sous-onglets du document d'un symbole : Dessin | Alarmes (n) | Instances (n)
// | Operateurs (n) (1.10, chantier S2 : HmiOperatorPanes.hpp).
// Instances montre le volet Alarmes sur son apercu (les instances et leurs alarmes).
class HmiSymbolTabs final : public ui::Widget {
public:
    // 1.11.10 : + Fonctions et Popups (les fonctions et les popups propres au symbole).
    enum Tab : int { Drawing = 0, Alarms = 1, Instances = 2, Operators = 3, Functions = 4, Popups = 5 };
    HmiSymbolTabs(std::string id, hmi::DocumentPtr doc, hmi::Id symbol);
    [[nodiscard]] int  current() const noexcept { return current_; }
    void               setCurrent(int tab);
    [[nodiscard]] std::string label(int tab) const;
    [[nodiscard]] bool tabRect(int tab, gfx::Rect& out) const;   // pour les scripts
    const core::SignalPtr<int> changed = core::Signal<int>::create();

protected:
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    hmi::DocumentPtr doc_;
    hmi::Id          symbol_;
    int              current_{Drawing};
};

} // namespace app
