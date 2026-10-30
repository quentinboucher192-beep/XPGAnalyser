// =============================================================================
//  app/hmi/HmiUnitPanes.hpp - 1.13.0 (lot J6b) : L'INTERFACE DES UNITES DE MESURE
// -----------------------------------------------------------------------------
//  Le moteur des unites (hmi/HmiUnits.hpp : les familles, les conversions, les
//  deux regles de Compiler) est fait ; voici ce que l'utilisateur en voit.
//
//  1) LA CASE UNITE (unitui) - la meme regle partout ou une unite se tape : la
//     colonne Unite des Variables IHM, des membres d'un type, des grilles de
//     declarations (parametres, locales, constantes), l'unite du retour d'une
//     fonction, les modals Nouvelle variable et Nouvelle fonction. Vide : sans
//     unite (comme avant). Sinon un symbole du catalogue (fournies + projet), sans
//     casse (degc -> degC), et seulement sur un type numerique : la raison du
//     refus se dit sous la case. L'aide a la saisie (Ctrl+Espace) propose les
//     unites, et sa derniere ligne ouvre le selecteur.
//
//  2) LE SELECTEUR D'UNITES (HmiUnitPicker, modal, facon selecteur de types) :
//
//    +-- Choisir une unite - Unite de Pression -----------------------------+
//    | [Rechercher (symbole, nom, famille)____]  Famille [Toutes        v]   |
//    | Unite | Nom        | Famille    | Conversion                          |
//    | bar   | bar        | Pression   | 1 bar = 100000 Pa                   |
//    | mbar  | millibar   | Pression   | 1 mbar = 100 Pa                     |
//    | bar : une pression ; reference de la famille : Pa                    |
//    | [Gerer les unites...]       [Sans unite] [Annuler] [Choisir]          |
//    +-----------------------------------------------------------------------+
//
//     La recherche plie la casse et les accents ; la liste Famille filtre (et
//     Recentes : les dernieres choisies). " Gerer les unites... " mene a la page
//     Configuration > Unites de mesure. Comme le selecteur de types, il est OUVERT
//     PAR UN HOTE (l'ecran) : une grille demande, l'hote ouvre, `done` recoit.
//
//  3) CONFIGURATION > UNITES DE MESURE (HmiMeasureUnitsPane) - la bibliotheque du
//     projet : a gauche les familles (reference, nombre d'unites, provenance), a
//     droite les unites de la famille choisie (nom, facteur, decalage, emplois) et
//     la fiche de l'unite choisie. Les fournies sont verrouillees ; celles du
//     projet se modifient. Une unite employee ne se retire pas (comme un type).
//     La case " Unites strictes " : une affectation de meme famille exige alors
//     EN(...). Chaque geste est une commande (Ctrl+Z).
//
//  4) 1.14.1 (lot N5) : L'ONGLET CONVERTIR (HmiUnitConvertPage), a cote de la
//     bibliotheque (onglet Bibliotheque) - un banc d'essai des conversions :
//
//    Rechercher [psi______]        Dimension [Toutes les dimensions v]
//    Valeur [1.5___]  De [bar - bar  v] [<=>] Vers [mbar - millibar v]  Decimales [6 v]
//        1.5 bar = 1500 mbar
//        Formule : 1 bar = 1000 mbar
//        Dans un script ou une fx : EN(Valeur, 'mbar')            [Copier]
//    HISTORIQUE DE LA SEANCE (un clic y revient)
//    | Valeur | De | Vers | Resultat |
//
//     Le resultat suit chaque frappe ; Vers ne propose que la dimension de De ;
//     la recherche (symbole, nom, dimension, sans casse ni accents) filtre De et
//     y prend la premiere unite trouvee. Le calcul est celui du moteur
//     (hmi::units::convertValue : Catalog::convert, comme EN) ; les refus (unite
//     inconnue, dimensions differentes, valeur illisible) se disent en rouge.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiUnits.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"   // 1.14.1 (lot N5) : les onglets Bibliotheque, Convertir
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// ------------------------------------------------------------ la case Unite ---
namespace unitui {
// La case Unite pour ce type : "" = bonne (`canonical` : le symbole fourni, degc -> degC ; vide :
// sans unite) ; sinon la raison du refus (une unite sur un BOOL, un symbole inconnu).
[[nodiscard]] std::string problem(const hmi::Project&, std::string_view type, std::string_view text, std::string* canonical = nullptr);
// Ce que la case montre quand le type ne prend pas d'unite (grisee) : "".
[[nodiscard]] bool usable(std::string_view type);
// L'unite comme l'operateur la lit : degC -> deg C, m3/h -> m3/h, Nm3/h -> Nm3/h ; le reste tel quel.
[[nodiscard]] std::string displayForm(std::string_view symbol);
// Une ligne pour une infobulle : "bar - Pression (bar)".
[[nodiscard]] std::string describe(const hmi::Project&, std::string_view symbol);
// L'aide a la saisie d'une case Unite : les unites dont le symbole, le nom ou la famille contient
// ce qui est tape (sans casse ni accents), puis " Choisir une unite... " (le selecteur, s'il a un
// hote). `chosen` : appele avec le symbole choisi dans le selecteur (la case l'ecrit d'elle-meme).
[[nodiscard]] ui::InputText::Assist fieldAssist(std::function<const hmi::Project*()> project, std::string field, std::string type,
                                                std::function<void(const std::string&)> chosen = {});
// Les emplois d'une unite dans le projet : "Variable Pression", "T_Ligne.Debit", "Moyenne.a"...
[[nodiscard]] std::vector<std::string> usesOf(const hmi::Project&, std::string_view symbol);
} // namespace unitui

// --------------------------------------------------------- le selecteur ---
class HmiUnitPicker final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string              id{"dialog.hmiUnitPicker"};
        std::string              field;        // " Unite de Pression " (le titre)
        std::string              current;      // l'unite actuelle (choisie d'office)
        std::string              type;         // le type qui la porte (REAL) ; vide : non dit
        hmi::DocumentPtr         doc;          // le projet : ses unites, relues a chaque changement
        std::vector<std::string> recents{};    // les dernieres choisies (les plus recentes d'abord)
        // 1.13.0 (seconde passe) : faux - ouvert depuis une fenetre modale (Nouvelle fonction, Nouvelle variable) : pas
        // de " Gerer les unites... " (la page s'ouvrirait dessous, inaccessible). L'hote le decide.
        bool                     canManage{true};
    };
    struct Answer {
        std::string unit;            // le symbole choisi ; vide : sans unite
        bool        manage{false};   // " Gerer les unites... " : la page Configuration > Unites de mesure
    };

    explicit HmiUnitPicker(Spec spec);
    ~HmiUnitPicker() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- ce que ferait la souris (scripts, essais) ----
    void setSearch(const std::string& text);
    bool setFamily(std::string_view labelOrName);    // "Toutes", "R\xC3\xA9" "centes", "Pression", "pression"
    bool select(std::string_view symbol);            // une ligne de la liste, par son symbole
    [[nodiscard]] std::vector<std::string> shownSymbols() const;
    [[nodiscard]] std::vector<std::string> familyChoices() const;
    [[nodiscard]] std::string selected() const;      // le symbole de la ligne choisie ; vide : aucune
    [[nodiscard]] std::string description() const;   // la ligne sous la liste
    [[nodiscard]] std::string notice() const;        // ce qui ne va pas (le type non numerique, l'unite actuelle inconnue)
    void choose();                                   // Choisir (rien si aucune ligne)
    void chooseNone();                               // Sans unite
    void manage();                                   // Gerer les unites... (rien sans canManage)
    [[nodiscard]] bool manageShown() const noexcept;  // le bouton Gerer les unites... est-il la ?
    void finish(bool ok);
    [[nodiscard]] ui::TableView* table() const noexcept { return table_; }

    [[nodiscard]] static std::string encode(const Answer&);
    static bool parse(const std::string& payload, Answer& out);

protected:
    core::Status buildUi() override;

private:
    class Body;
    void reload();
    void refill();
    Spec        spec_;
    Answer      answer_;
    bool        done_{false};
    std::string search_;
    std::string family_;                 // le nom de la famille filtree ; vide : toutes ; "*" : recentes
    std::vector<hmi::UnitDef> all_;      // le catalogue (fournies + projet)
    std::vector<std::size_t>  shown_;    // les lignes montrees (indices dans all_)
    std::string selected_;
    Body*          body_{nullptr};
    ui::InputText* searchField_{nullptr};
    ui::DropDown*  familyBox_{nullptr};
    ui::TableView* table_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    bool           syncing_{false};
    core::ConnectionScope links_;
};

// ---- l'hote : qui ouvre le selecteur (l'ecran ; les essais posent le leur) ----
namespace unitpicker {
inline const std::string kChoose = "Choisir une unit\xC3\xA9\xE2\x80\xA6";
using Done = std::function<void(const HmiUnitPicker::Answer&)>;
using Host = std::function<void(HmiUnitPicker::Spec, Done)>;
void setHost(Host host);
[[nodiscard]] bool available();
void ask(HmiUnitPicker::Spec spec, Done done);     // rien sans hote
// Les recentes (huit au plus, la plus recente d'abord) : gardees pour la seance.
void remember(const std::string& symbol);
[[nodiscard]] const std::vector<std::string>& recents();
} // namespace unitpicker

// ------------------------------------------------- 1.14.1 (lot N5) : l'onglet Convertir ---
class HmiUnitConvertPage final : public ui::Widget {
public:
    HmiUnitConvertPage(std::string id, hmi::DocumentPtr doc);
    void refresh();                                   // le catalogue relu (une unite du projet ajoutee, retiree)

    // ---- ce que ferait l'utilisateur (la souris, le clavier ; les essais) ----
    void setValueText(const std::string& text);       // la case Valeur (le resultat suit chaque frappe)
    void setSearch(const std::string& text);          // la recherche : filtre De, y prend la premiere trouvee
    bool setDimension(std::string_view labelOrName);  // "Toutes les dimensions", "Pression", "temperature"
    void setFrom(std::string_view symbol);            // une unite inconnue est gardee : le refus le dit
    void setTo(std::string_view symbol);              // ... une autre dimension aussi
    void setDecimals(int decimals);                   // 0..12
    void swap();                                      // le bouton inverser : De <-> Vers (la valeur garde son nombre)
    std::string copyExpression();                     // Copier : EN(Valeur, 'mbar') au presse-papiers (et le rend)
    void remember();                                  // la conversion du moment, en tete de l'historique
    bool recall(std::size_t row);                     // un clic sur une ligne de l'historique : la conversion revient

    // ---- ce qui est montre ----
    [[nodiscard]] std::vector<std::string> fromChoices() const;   // les symboles de la liste De (filtree)
    [[nodiscard]] std::vector<std::string> toChoices() const;     // ceux de Vers : la dimension de De
    [[nodiscard]] std::vector<std::string> dimensionChoices() const;
    [[nodiscard]] const std::string& from() const noexcept { return from_; }
    [[nodiscard]] const std::string& to() const noexcept { return to_; }
    [[nodiscard]] int decimals() const noexcept { return decimals_; }
    [[nodiscard]] const hmi::units::Conversion& conversion() const noexcept { return conv_; }
    [[nodiscard]] std::string resultText() const;     // "1.5 bar = 1500 mbar" ; vide : refus
    [[nodiscard]] std::string formulaLine() const;    // "Formule : 1 bar = 1000 mbar"
    [[nodiscard]] std::string expression() const;     // "EN(Valeur, 'mbar')"
    [[nodiscard]] const std::string& errorText() const noexcept { return error_; }
    struct Entry {
        std::string value, from, to, result;
        bool operator==(const Entry&) const = default;
    };
    [[nodiscard]] static const std::vector<Entry>& history();   // la seance (douze au plus, la plus recente d'abord)
    static void clearHistory();

    [[nodiscard]] ui::InputText&  valueField() noexcept { return *value_; }
    [[nodiscard]] ui::InputText&  searchField() noexcept { return *search_; }
    [[nodiscard]] ui::DropDown&   dimensionBox() noexcept { return *dimension_; }
    [[nodiscard]] ui::DropDown&   fromBox() noexcept { return *fromBox_; }
    [[nodiscard]] ui::DropDown&   toBox() noexcept { return *toBox_; }
    [[nodiscard]] ui::DropDown&   decimalsBox() noexcept { return *decimalsBox_; }
    [[nodiscard]] ui::Button&     swapButton() noexcept { return *swap_; }
    [[nodiscard]] ui::Button&     copyButton() noexcept { return *copy_; }
    [[nodiscard]] ui::TableView&  historyTable() noexcept { return *history_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    [[nodiscard]] hmi::units::Catalog catalog() const;
    void fillLists();                                 // De (dimension, recherche), Vers (la dimension de De)
    void recompute();                                 // le resultat, la formule, le refus
    void followFrom();                                // Vers suit la dimension de De
    void refillHistory();
    hmi::DocumentPtr doc_;
    std::string      valueText_{"1"}, searchText_, family_, from_{"bar"}, to_{"mbar"}, error_;
    int              decimals_{6};
    hmi::units::Conversion conv_;
    std::vector<std::string> fromSymbols_, toSymbols_;
    ui::InputText* value_{nullptr};
    ui::InputText* search_{nullptr};
    ui::DropDown*  dimension_{nullptr};
    ui::DropDown*  fromBox_{nullptr};
    ui::DropDown*  toBox_{nullptr};
    ui::DropDown*  decimalsBox_{nullptr};
    ui::Button*    swap_{nullptr};
    ui::Button*    copy_{nullptr};
    ui::TableView* history_{nullptr};
    gfx::Rect      labels_[5]{};                      // Rechercher, Dimension, Valeur, De, Vers (les etiquettes)
    gfx::Rect      decimalsLabel_{};
    gfx::Rect      resultArea_{}, historyTitle_{};
    bool           syncing_{false};
    core::ConnectionScope links_;
};

// ------------------------------------------- Configuration > Unites de mesure ---
class HmiMeasureUnitsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiMeasureUnitsPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // ---- les gestes : chacun une commande (Ctrl+Z) ; faux et `why` : refuse, rien ne change ----
    // Une famille du projet : son nom (minuscules, chiffres, _), sa reference (un symbole nouveau :
    // l'unite de reference est creee avec elle), son libelle.
    bool addFamily(const std::string& name, const std::string& reference, const std::string& label = {}, std::string* why = nullptr);
    bool setFamilyLabel(const std::string& name, const std::string& label, std::string* why = nullptr);
    // Renommer une famille du projet (ses unites la suivent) ; une unite du projet que rien n'emploie (la
    // reference de sa famille la suit). " + Famille " et " + Unite " posent un nom libre : on le change ici.
    bool renameFamily(const std::string& name, const std::string& newName, std::string* why = nullptr);
    bool renameUnit(const std::string& symbol, const std::string& newSymbol, std::string* why = nullptr);
    bool removeFamily(const std::string& name, std::string* why = nullptr);    // du projet, sans unite employee
    bool addUnit(const hmi::UnitDef& unit, std::string* why = nullptr);
    // Modifier une unite du projet (nom, facteur, decalage) ; une fournie est verrouillee.
    bool updateUnit(const std::string& symbol, const hmi::UnitDef& after, std::string* why = nullptr);
    bool removeUnit(const std::string& symbol, std::string* why = nullptr);    // du projet, pas employee
    bool setStrict(bool on);

    [[nodiscard]] std::string selectedFamily() const;
    void selectFamily(const std::string& name);
    [[nodiscard]] std::string selectedUnit() const;
    void selectUnit(const std::string& symbol);
    // Vrai : l'unite (ou la famille) vient de la bibliotheque fournie (verrouillee).
    [[nodiscard]] static bool builtinUnit(std::string_view symbol);
    [[nodiscard]] static bool builtinFamily(std::string_view name);

    // 1.14.1 (lot N5) : deux onglets - Bibliotheque (ce qui precede) et Convertir (le banc d'essai).
    enum Tab : std::size_t { TabLibrary = 0, TabConvert = 1 };
    void showTab(std::size_t tab);
    [[nodiscard]] std::size_t currentTab() const noexcept;
    [[nodiscard]] ui::TabControl&     tabs() noexcept { return *tabs_; }
    [[nodiscard]] HmiUnitConvertPage& convertPage() noexcept { return *convert_; }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     families() noexcept { return *families_; }
    [[nodiscard]] ui::TableView&     units() noexcept { return *units_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] ui::Checkbox&      strictBox() noexcept { return *strict_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    enum Action : int { AFamily = 1, AUnit, ARemove };

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void layoutLibrary(const gfx::Rect&);              // 1.14.1 (lot N5) : la page Bibliotheque (son contenu d'avant)
    void refreshUnits();
    void rebuildProperties();
    void say(std::string text, bool error = false);
    bool commit(const std::string& label, const std::function<bool(hmi::Project&, std::string*)>& change, std::string* why);
    hmi::DocumentPtr  doc_;
    Apply             apply_;
    HmiToolStrip*     tools_{nullptr};
    ui::Checkbox*     strict_{nullptr};
    ui::TableView*    families_{nullptr};
    ui::TableView*    units_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    ui::StatusBar*    status_{nullptr};
    HmiTitledPanel*   familiesPanel_{nullptr};
    HmiTitledPanel*   unitsPanel_{nullptr};
    ui::TabControl*     tabs_{nullptr};                // 1.14.1 (lot N5)
    HmiUnitConvertPage* convert_{nullptr};
    std::vector<std::string> familyNames_;   // dans l'ordre des lignes
    std::vector<std::string> unitSymbols_;
    std::string       family_;               // la famille choisie (son nom)
    bool              focusFamily_{false};   // la fiche dit la famille (sinon l'unite)
    bool              syncing_{false};
    std::string       message_;
    core::ConnectionScope links_;
};

} // namespace app
