// =============================================================================
//  ui/widgets/FilterBar.hpp - 1.47.0 (planche 12) : << FILTRER ET CHERCHER >>,
//  LA MEME BARRE AU-DESSUS DE CHAQUE LONGUE LISTE
// -----------------------------------------------------------------------------
//    [Q Chercher...  Ctrl+F] (Etat : A livrer, A revoir x) (Article v) (Periode : 30 j v)
//    + Filtre                               2 sur 214  Effacer les filtres  [* Mes filtres v]
//
//  - CHERCHER (Ctrl+F, ou que soit le clavier sur la page) : dans toutes les
//    colonnes montrees (et ce que la page ajoute : Source::extra), les mots ET,
//    "une phrase", -mot exclu, sans casse ni accents (ui::SearchQuery) ; les
//    trouvailles surlignees dans la table ;
//  - UNE PUCE PAR COLONNE UTILE (Field::chip) : son menu - une recherche, une
//    case par valeur avec son nombre (celui des lignes que gardent les AUTRES
//    filtres), << Tout cocher . Aucune . sur les 128 >> ; une colonne de dates
//    (Kind::Period) : aujourd'hui, 7 j, 30 j, 90 j, 12 mois, tout, et ses
//    nombres. Active, la puce est violette et dit ce qu'elle garde ; sa croix
//    l'efface ;
//  - << + Filtre >> : une puce sur n'importe quelle autre colonne ;
//  - << 12 sur 128 >>, << Effacer les filtres >> (tout revient) ;
//  - << Mes filtres >> : des filtres nommes, par liste (<< Commandes a
//    livrer >>), chacun avec ce qu'il montrerait maintenant ; << Enregistrer ce
//    filtre... >>, << Gerer... >> (les retirer). Ils sont gardes dans les
//    reglages du poste (ui::FilterMemory : l'application branche ou) ; la barre
//    se souvient aussi de ce qui est choisi (par liste, sur ce PC) ;
//  - l'entonnoir des titres de la table ouvre le meme menu que la puce ; il est
//    violet quand la colonne est filtree. Le clic droit << Filtrer sur > >>
//    met la meme puce (filterOn).
//
//  L'EMPLOI (une page de l'Admin, une table dont le modele a TOUTES les lignes) :
//
//      bar_ = &adopt(*this, std::make_unique<ui::FilterBar>("commandes", std::vector<ui::FilterBar::Field>{
//                 {.title = "N\xC2\xB0"}, {.title = "Date", .kind = ui::FilterBar::Kind::Period, .chip = true, .chipLabel = "P\xC3\xA9riode"},
//                 {.title = "Article", .chip = true, .noun = "un article"}, ...}));
//      bar_->attach(*table_, [this](ui::RowIndex r, std::size_t field) { return model_->rows[r].created; });
//      bar_->setMemoryKey("commandes");          // ce qui est choisi, et << Mes filtres >>, retenus pour cette liste
//      ... onLayout : bar_->setBounds({x, y, w, bar_->heightFor(w)});
//
//  Les champs (Field) suivent les colonnes du modele, dans le meme ordre (le
//  champ i est la colonne i) ; une colonne cachee peut rester un champ. Une
//  liste peinte par sa page (le journal) : setSource (ses lignes), accepts(i)
//  en faisant ses lignes, `changed` pour les refaire, refreshCounts.
//  Les identifiants (les scripts) : <id>.recherche (le champ), <id>.puce.<titre>
//  (une puce : << commandes.puce.etat >>), <id>.plus-filtre, <id>.effacer,
//  <id>.mes-filtres, <id>.menu (le menu ouvert).
// =============================================================================
#pragma once

#include "DataViews.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui {

class InputText;
class FilterBarPopup;

class FilterBar final : public Widget {
public:
    enum class Kind : std::uint8_t { Values, Period, Text };
    struct Field {
        std::string              title;              // le titre de la colonne (<< Etat >>)
        Kind                     kind{Kind::Values}; // Values : des cases ; Period : des periodes ; Text : chercher seulement
        bool                     chip{false};        // une puce d'office
        std::string              chipLabel{};        // le nom de la puce (<< Periode >>, << Livree par >>) ; vide : le titre
        std::string              noun{};             // << une offre >> : << Chercher une offre... >> dans son menu
        bool                     searchable{true};   // la recherche la lit
        std::vector<std::string> order{};            // l'ordre des valeurs dans le menu (Info, Moyenne...) ; le reste ensuite
    };
    // Ce que la barre retient d'une colonne.
    struct Pick {
        std::size_t              field{0};
        std::vector<std::string> values{};    // Values : les valeurs gardees (vide : la puce est la, sans filtre)
        int                      days{0};     // Period : les N derniers jours (1 : aujourd'hui) ; 0 : aucun
        std::int64_t             day{0};      // Period : ce jour-la (son debut, heure locale) ; 0 : aucun
        bool                     viaMenu{false};   // posee par le clic droit (la puce le dit : << clic droit >>)
        [[nodiscard]] bool active() const noexcept { return !values.empty() || days > 0 || day > 0; }
    };
    struct State {
        std::string       search;
        std::vector<Pick> picks;              // les puces ajoutees (+ Filtre, le clic droit), dans l'ordre
        [[nodiscard]] bool active() const noexcept;   // une recherche, ou une puce qui retient quelque chose
    };
    struct Saved {
        std::string name;
        State       state;
    };
    // Les lignes d'une liste peinte par sa page (setSource) ; attach les prend dans la table.
    struct Source {
        std::function<std::size_t()>                                rows;
        std::function<std::string(std::size_t row, std::size_t field)> text;
        std::function<std::int64_t(std::size_t row, std::size_t field)> time{};   // Period : en secondes (0 : pas de date)
        std::function<std::string(std::size_t row)>                 extra{};      // ce que la recherche lit en plus
    };
    using TimeOf = std::function<std::int64_t(RowIndex row, std::size_t field)>;

    // `searchId` : l'id du champ de recherche (vide : <id>.recherche) - une page dont l'id est deja << licences >>.
    FilterBar(std::string id, std::vector<Field> fields, std::string searchId = {});
    ~FilterBar() override;

    // ---- brancher ----
    // La table et son modele (toutes les lignes) : la barre filtre la table (FilterChain), surligne la recherche, allume
    // ses entonnoirs ; `time` : la date d'une ligne pour un champ Period ; `extra` : ce que la recherche lit en plus.
    void attach(TableView& table, TimeOf time = {}, std::function<std::string(RowIndex)> extra = {});
    void setSource(Source source);       // une liste peinte : accepts(i), `changed`, refreshCounts
    // UNE TABLE QUE LA PAGE REMPLIT ELLE-MEME (setSource : toutes ses lignes ; elle ne met dans le modele que celles
    // que accepts garde, a chaque `changed`) : la barre en allume les entonnoirs et y surligne la recherche.
    void linkTable(TableView& table);
    void refreshCounts();                // les lignes ont change (une liste peinte) : << 12 sur 128 >> refait
    // Ou retenir ce qui est choisi et << Mes filtres >> (ui::FilterMemory) : << commandes >> -> lu tout de suite.
    void setMemoryKey(std::string key);
    // L'heure pour les periodes (les essais : une horloge d'essai) ; defaut : l'horloge du systeme.
    void setClock(std::function<std::int64_t()> now) { now_ = std::move(now); }
    // Le nom de ce qu'on compte (<< commandes >> : << 2 commandes sur 214 avec ces filtres >>, la ligne de la table vide).
    void setNoun(std::string plural) { noun_ = std::move(plural); }

    // ---- ce qui est choisi ----
    [[nodiscard]] const State& state() const noexcept { return state_; }
    void setState(State s);              // tout d'un coup (Mes filtres, la memoire)
    [[nodiscard]] bool accepts(std::size_t row) const;                    // la recherche et toutes les puces
    [[nodiscard]] bool acceptsExcept(std::size_t row, int field) const;   // ... sauf ce champ (-1 : aucun)
    [[nodiscard]] std::size_t shownCount() const;
    [[nodiscard]] std::size_t totalCount() const;
    [[nodiscard]] std::string countText() const;                          // << 2 sur 214 >>
    [[nodiscard]] bool fieldActive(std::size_t field) const;
    [[nodiscard]] const std::vector<Field>& fields() const noexcept { return fields_; }
    [[nodiscard]] int fieldByTitle(std::string_view title) const;         // sans casse ni accents ; -1 : aucun
    // Le libelle d'une puce (<< Etat : A livrer, A revoir >>, << Article >>, << Periode : 30 j >>).
    [[nodiscard]] std::string chipText(std::size_t field) const;
    // Les puces montrees, dans l'ordre (d'office, puis ajoutees).
    [[nodiscard]] std::vector<std::size_t> chipFields() const;

    // ---- les gestes (la souris, le clic droit, les essais) ----
    void setSearch(std::string text);
    void focusSearch();                                    // Ctrl+F
    void filterOn(std::size_t field, const std::string& value);   // le clic droit << Filtrer sur > >> : la puce = cette valeur
    void filterOnDay(std::size_t field, std::int64_t when);       // << Ce jour : 10/11 >>
    void setValues(std::size_t field, std::vector<std::string> values);
    void toggleValue(std::size_t field, const std::string& value);
    void setPeriod(std::size_t field, int days);           // 0 : tout
    void addChip(std::size_t field);                       // << + Filtre >> : la puce, sans filtre
    void clearField(std::size_t field);                    // la croix d'une puce
    void clearAll();                                       // << Effacer les filtres >>
    // Les nombres d'un menu : (valeur, combien) d'un champ Values, sur les lignes que gardent les autres filtres ;
    // (jours, combien) d'un champ Period (1, 7, 30, 90, 365, 0 : tout).
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> valueCounts(std::size_t field) const;
    [[nodiscard]] std::vector<std::pair<int, std::size_t>> periodCounts(std::size_t field) const;
    [[nodiscard]] static std::string periodLabel(int days);   // << 30 j >>, << aujourd'hui >>, << 12 mois >>, << tout >>

    // ---- Mes filtres ----
    [[nodiscard]] const std::vector<Saved>& saved() const noexcept { return saved_; }
    bool saveAs(const std::string& name);                  // ce qui est choisi, sous ce nom (le meme nom : remplace)
    bool applySaved(const std::string& name);
    bool removeSaved(const std::string& name);
    [[nodiscard]] std::size_t countFor(const State& s) const;   // ce qu'il montrerait maintenant
    [[nodiscard]] std::string currentSavedName() const;         // le filtre de Mes filtres qui est choisi ; vide : aucun
    [[nodiscard]] std::string proposedName() const;             // un nom propose pour Enregistrer (<< Etat : A livrer >>)

    // ---- les menus (un a la fois ; les essais) ----
    enum class Menu : std::uint8_t { None, Chip, Add, Saved };
    void openChipMenu(std::size_t field, gfx::Rect anchor = {});
    void openAddMenu();
    void openSavedMenu();
    void closeMenu();
    [[nodiscard]] Menu openMenu() const noexcept { return menu_; }
    [[nodiscard]] std::size_t menuField() const noexcept { return menuField_; }
    [[nodiscard]] FilterBarPopup* popup() const noexcept { return popup_; }

    // ---- la mise en page ----
    [[nodiscard]] float heightFor(float width) const;       // une rangee, deux si les puces ne tiennent pas
    [[nodiscard]] SizeHint sizeHint() const override;
    [[nodiscard]] InputText& searchField() noexcept { return *search_; }
    [[nodiscard]] Widget* chipWidget(std::size_t field) const;   // la puce d'un champ (montree ou non) ; nul : aucune

    // Les textes retenus (FilterMemory) : ce qui est choisi, Mes filtres - par le titre des colonnes.
    [[nodiscard]] std::string encodeState(const State& s) const;
    [[nodiscard]] State decodeState(std::string_view text) const;

    const core::SignalPtr<> changed = core::Signal<>::create();   // ce qui est choisi a change (la liste est refaite)

protected:
    void        onLayout() override;
    void        onPaint(const PaintContext&) override;
    void        onPaintOverlay(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    friend class FilterBarPopup;
    class Chip;
    struct Placed {
        Widget*   widget{nullptr};
        gfx::Rect rect{};
    };
    [[nodiscard]] Pick* pickOf(std::size_t field);
    [[nodiscard]] const Pick* pickOf(std::size_t field) const;
    [[nodiscard]] bool fieldAccepts(const Pick& p, std::size_t row) const;
    [[nodiscard]] bool matchesSearch(std::size_t row, const SearchQuery& q) const;
    [[nodiscard]] bool acceptsWith(const State& s, std::size_t row, int skipField) const;
    [[nodiscard]] std::int64_t now() const;
    [[nodiscard]] std::size_t rowCount() const;
    [[nodiscard]] std::string cell(std::size_t row, std::size_t field) const;
    void edited(bool remember = true);                     // la table, les puces, la memoire, `changed`
    void syncChips();
    void layoutRow(float width, std::vector<Placed>* out, float* height) const;
    void refreshMenu();                                    // le menu ouvert, ses nombres et ses cases
    void popupRow(std::size_t index);
    void popupFooter(std::size_t index);
    void popupDelete(std::size_t index);
    void popupEnter(const std::string& text);
    void loadMemory();
    void saveMemory() const;
    [[nodiscard]] gfx::Rect anchorOf(std::size_t field) const;

    std::vector<Field>                 fields_;
    State                              state_;
    std::vector<Saved>                 saved_;
    Source                             source_;
    TableView*                         table_{nullptr};      // attach : la barre filtre cette table
    TableView*                         linked_{nullptr};     // linkTable : la page la remplit
    void hookTable(TableView& table);
    std::shared_ptr<bool>              alive_{std::make_shared<bool>(true)};
    std::function<std::int64_t()>      now_;
    std::string                        memoryKey_, noun_;
    bool                               loading_{false};
    mutable std::size_t                shownCache_{0};
    mutable bool                       countDirty_{true};
    SearchQuery                        query_;
    // les widgets
    InputText*                         search_{nullptr};
    std::vector<Chip*>                 chips_;          // une par champ (montree ou non)
    Button*                            add_{nullptr};
    Button*                            clear_{nullptr};
    Button*                            savedBtn_{nullptr};
    FilterBarPopup*                    popup_{nullptr};
    Menu                               menu_{Menu::None};
    std::size_t                        menuField_{0};
    bool                               naming_{false}, managing_{false};
    std::vector<std::string>           menuValues_;     // ce que chaque ligne du menu ouvert vaut
    gfx::Rect                          countRect_{};
    core::ConnectionScope              links_;
};

// LE MENU DE LA BARRE (une puce, << + Filtre >>, << Mes filtres >>) : par-dessus tout, sous ce qui l'a ouvert - une
// recherche en haut (facultative), des lignes (une case, une etoile, un point ; un libelle ; un nombre a droite ; une
// coche ; une croix pour << Gerer >>), un pied de liens (<< Tout cocher . Aucune >>) et une note (<< sur les 128 >>).
// Un clic sur une case ne le ferme pas ; Echap, ou un clic a cote, le ferme.
class FilterBarPopup final : public Widget {
public:
    struct Row {
        std::string label;
        std::string count{};
        int         mark{0};          // 0 rien, 1 une case, 2 une etoile, 3 un point (un choix parmi d'autres)
        bool        on{false};        // cochee, etoile pleine, point pris
        bool        tick{false};      // une coche a droite (le filtre choisi)
        bool        removable{false}; // << Gerer >> : une croix a droite
        Icon        icon{Icon::None};
    };
    struct Spec {
        std::string              title{};        // en tete (<< MES FILTRES . COMMANDES >>) ; vide : aucun
        std::string              searchHint{};   // non vide : un champ en haut
        bool                     nameField{false};   // le champ sert a taper un nom (Entree : valider)
        std::string              text{};          // ... ce qui y est deja
        std::vector<Row>         rows{};
        std::vector<std::string> links{};         // le pied, a gauche (<< Tout cocher >>, << Aucune >>)
        std::string              note{};          // le pied, a droite (<< sur les 128 >>)
        bool                     keepOpen{false}; // une ligne choisie ne ferme pas (des cases)
        float                    width{240.f};
        gfx::Rect                anchor{};
    };
    explicit FilterBarPopup(std::string id);
    void open(Spec spec);
    void setRows(std::vector<Row> rows, std::string note);   // les nombres, les cases (le menu ouvert)
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] const Spec& spec() const noexcept { return spec_; }
    [[nodiscard]] const std::string& fieldText() const noexcept { return text_; }
    void setFieldText(std::string t);
    [[nodiscard]] std::vector<std::size_t> shownRows() const;   // les lignes que la recherche garde (indices de spec().rows)
    [[nodiscard]] gfx::Rect popupRect() const;
    [[nodiscard]] gfx::Rect rowRect(std::size_t shownIndex) const;
    [[nodiscard]] gfx::Rect linkRect(std::size_t index) const;
    [[nodiscard]] bool      requestsOverlayPass() const override { return open_; }
    [[nodiscard]] gfx::Rect eventBounds() const override;

    std::function<void(std::size_t row)>          onRow;
    std::function<void(std::size_t link)>         onLink;
    std::function<void(std::size_t row)>          onRemove;
    std::function<void(const std::string& text)>  onEnter;
    std::function<void()>                         onClose;

protected:
    void        onPaintOverlay(const PaintContext&) override;
    EventResult onEvent(const InputEvent&) override;

private:
    [[nodiscard]] float rowsTop() const;
    [[nodiscard]] int   visibleRows() const;
    Spec        spec_;
    bool        open_{false};
    std::string text_;
    int         scroll_{0};
    int         hover_{-1};
    gfx::Size   surface_{};
};

} // namespace ui
