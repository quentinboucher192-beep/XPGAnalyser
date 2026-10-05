// =============================================================================
//  app/StartPage.hpp - l'accueil, repense (lot API 7)
// -----------------------------------------------------------------------------
//  TROIS COLONNES, comme la maquette de l'accueil :
//
//    a gauche   la marque (le logo, le nom, ce que fait l'application), les
//               deux grandes actions (Nouveau projet, Ouvrir...), la zone ou
//               glisser un dossier ou un export - et son chemin a coller -, les
//               liens (didacticiel, aide, reglages), les neuf themes en
//               pastilles et le poste d'exploitation lance au demarrage du PC ;
//    au centre  les projets recents en cartes : chercher, filtrer, trier ;
//    a droite   le projet choisi : ce qu'on en fait, ce qu'il contient, ses
//               dernieres versions.
//
//  CES WIDGETS NE LISENT PAS LE DISQUE et ne connaissent pas App. L'ecran
//  (screens/StartScreen.cpp) leur donne les projets deja lus, branche leurs
//  signaux une fois pour toutes, et fait ce qu'ils demandent. Ils se dessinent
//  avec le theme du moment : changer de theme d'une pastille repeint tout.
//
//  Les boutons sont de vrais ui::Button (StartButton en change seulement le
//  dessin) : le clavier (Tab, Entree, Espace) et les sessions rejouees
//  (bouton "Nouveau projet") marchent comme partout ailleurs.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../project/Security.hpp"       // project::State
#include "../ui/Icons.hpp"
#include "../ui/Theme.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/Containers.hpp"  // ui::StatusBar
#include "../ui/widgets/Controls.hpp"    // ui::Button, ui::InputText, ui::DropDown
#include "../ui/widgets/PathBrowse.hpp"  // le bouton ... du champ du chemin

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::start {

// ------------------------------------------------------------ les donnees ----
// Une version, telle que la liste du panneau de droite la montre.
struct VersionLine {
    std::string number;              // "V47"
    std::string label;               // "automatique", "Essai 39"
    std::string date;                // "28/09 18:12"
    ui::Tone    tone{ui::Tone::Muted};
};

// Un projet recent : un dossier de projet, un export (.XPG, .XHW...) ou un
// chemin qui n'existe plus. Lu par l'ecran ; ce qui coute (compter les
// variables, les vues...) ne l'est que pour le projet choisi (factsLoaded).
struct ProjectEntry {
    std::string    path;
    std::string    name;
    bool           isFolder{false};
    bool           missing{false};      // le chemin n'existe plus (deplace, lecteur absent)
    bool           open{false};         // le projet charge dans l'application en ce moment
    bool           modified{false};     // ... avec des modifications non enregistrees
    bool           autostart{false};    // le poste lance au demarrage du PC
    project::State state{project::State::New};
    std::string    badge;               // "DEV", "FINISH", "LOCK", "NEW", "export", "introuvable"
    std::string    versionTitle;        // "V48 en cours"
    std::string    versionSubtitle;     // "depuis V47 . auto"
    std::string    cpu;                 // "BMX P34 2020"
    std::string    when;                // "aujourd'hui a 18:20"
    std::string    stamp;               // "2026-09-28 18:20:33" : le tri par date
    std::size_t    recentRank{0};       // la place dans la liste des recents d'App
    // L'icone du projet (config/icone.txt), 32 x 32 agrandie quatre fois sans
    // lisser : kIconPixels x kIconPixels, RGBA. Nulle : pas d'icone.
    std::shared_ptr<const std::vector<std::uint8_t>> icon;
    std::vector<VersionLine> versions;  // les plus recentes d'abord, 4 au plus
    bool           factsLoaded{false};
    std::vector<std::pair<std::string, std::string>> facts;   // "Automate" -> "BMX P34 2020 . Modicon M340"
};

inline constexpr int kIconPixels = 128;

enum class Filter : std::uint8_t { All = 0, Projects, Exports, Finish, Lock };
inline constexpr std::size_t kFilterCount = 5;
enum class Sort : std::uint8_t { Recent = 0, Name, State };

[[nodiscard]] const char* filterLabel(Filter f) noexcept;   // "Tous", "Projets"...
[[nodiscard]] bool        passes(const ProjectEntry& e, Filter f) noexcept;
// La recherche : chaque mot doit se trouver dans le nom, l'automate ou le
// chemin - sans souci de la casse ni des accents.
[[nodiscard]] bool        matches(const ProjectEntry& e, const std::string& query);
[[nodiscard]] std::string foldText(std::string_view s);    // "Etat" et "etat" : pareil

// ------------------------------------------------------------- un bouton ----
// Un ui::Button dessine a la maniere de l'accueil : une grande action, une
// ligne de lien, un bouton plein, encadre ou rouge.
class StartButton : public ui::Button {
public:
    enum class Look : std::uint8_t {
        CardPrimary,   // la grande action principale (Nouveau projet)
        Card,          // une grande action (Ouvrir...)
        Link,          // une ligne de lien (Aide, Reglages)
        Primary,       // plein, couleur d'accent (Ouvrir)
        Secondary,     // encadre (Dupliquer...)
        Danger,        // encadre rouge (Supprimer...)
        Ghost,         // petit et discret (Changer...)
    };
    enum class Glyph : std::uint8_t { None, Plus, Open, Tutorial, Help, Settings, Station, Copy, Rename, Trash, Remove };

    StartButton(std::string text, std::string id, Look look, Glyph glyph = Glyph::None);

    void setSubtitle(std::string s);   // la seconde ligne d'une grande action ; apres le libelle d'un lien
    void setHint(std::string s);       // a droite : "Ctrl+N", "F1" ; ">" dessine un chevron
    void setGlyph(Glyph g);
    void setLook(Look l);              // "Supprimer..." rouge, "Retirer de la liste" encadre
    [[nodiscard]] Look  look() const noexcept { return look_; }
    // La largeur qui montre tout le libelle (les dispositions a la main).
    [[nodiscard]] float naturalWidth() const;
    [[nodiscard]] ui::SizeHint sizeHint() const override;

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    std::string subtitle_, hint_;
    Look        look_;
    Glyph       glyph_;
    bool        down_{false};
};

// Un champ de saisie avec son pictogramme a gauche, qui sait prendre le focus
// (Ctrl+F) : Widget::grabFocus est protege.
class StartField final : public ui::InputText {
public:
    StartField(std::string id, ui::Icon icon);
    void takeFocus() { grabFocus(); }
    void dropFocus() { releaseFocus(); }
protected:
    void onPaint(const ui::PaintContext&) override;
private:
    ui::Icon icon_;
};

// --------------------------------------------------------- les pastilles ----
// Les neuf themes, chacun dessine avec SES couleurs (le fond, l'accent) ; le
// theme en cours est entoure. Un clic demande le theme par sa cle.
class ThemeSwatches final : public ui::Widget {
public:
    explicit ThemeSwatches(std::string id);
    [[nodiscard]] std::size_t        count() const noexcept { return swatches_.size(); }
    [[nodiscard]] const std::string& keyAt(std::size_t i) const;
    [[nodiscard]] gfx::Rect          swatchRect(std::size_t i) const;   // pour les scripts
    const core::SignalPtr<const std::string&> chosen = core::Signal<const std::string&>::create();
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    struct Swatch { std::string key, label, description; gfx::Color window, panel, accent, text; };
    [[nodiscard]] int at(gfx::Point p) const;
    std::vector<Swatch> swatches_;
    int                 hover_{-1}, pressed_{-1};
};

// ------------------------------------------------------------ les filtres ----
// Tous, Projets, Exports, FINISH, LOCK - chacun avec son nombre.
class FilterChips final : public ui::Widget {
public:
    explicit FilterChips(std::string id);
    void setCount(Filter f, std::size_t n);
    void setCurrent(Filter f);
    [[nodiscard]] Filter    current() const noexcept { return current_; }
    [[nodiscard]] float     naturalWidth() const;
    [[nodiscard]] gfx::Rect chipRect(Filter f) const;
    const core::SignalPtr<int> chosen = core::Signal<int>::create();   // un Filter
protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
private:
    [[nodiscard]] std::string text(std::size_t i) const;
    [[nodiscard]] int         at(gfx::Point p) const;
    std::size_t counts_[kFilterCount]{};
    Filter      current_{Filter::All};
    int         hover_{-1}, pressed_{-1};
};

// --------------------------------------------------------- les cartes ----
// Les projets en cartes : deux colonnes, une quand la place manque ; la
// molette fait defiler. Un clic choisit, un double clic (ou Entree, geree par
// l'ecran) ouvre. La derniere carte, en pointilles, cree un projet.
class ProjectGrid final : public ui::Widget {
public:
    explicit ProjectGrid(std::string id);

    // Les cartes, dans l'ordre ; `selected` : un indice dans `items` (-1 : aucune).
    void setItems(std::vector<ProjectEntry> items, int selected);
    // Ce qui s'affiche au-dessus de la carte "Nouveau projet" quand il n'y a rien.
    void setEmptyMessage(std::string title, std::string text);
    void setSelected(int index, bool revealIt = true);
    [[nodiscard]] int selected() const noexcept { return selected_; }
    [[nodiscard]] int count() const noexcept { return static_cast<int>(items_.size()); }
    [[nodiscard]] int columns() const noexcept { return columns_; }
    // Les fleches : +-1 dans une ligne, +-columns() d'une ligne a l'autre.
    // Vrai si la selection a bouge (le signal part alors).
    bool moveSelection(int delta);
    bool pageSelection(int pages);        // Page precedente / suivante
    bool selectEdge(bool last);           // Debut / Fin
    // Ou est une carte, a l'ecran ; index == count() : la carte "Nouveau projet".
    [[nodiscard]] gfx::Rect cardRect(int index) const;
    // Un double clic n'ouvre que si son premier clic est venu ICI : celui d'un
    // dialogue qui se ferme au-dessus n'ouvre pas la carte qui est dessous.
    void resetClickChain() noexcept { lastDown_ = -1; pressed_ = -1; }
    void takeFocus() { grabFocus(); }

    const core::SignalPtr<int> selectionChanged = core::Signal<int>::create();
    const core::SignalPtr<int> activated        = core::Signal<int>::create();
    const core::SignalPtr<>    newRequested     = core::Signal<>::create();

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void  measure();                       // colonnes, largeur des cartes, hauteur du tout
    void  clampScroll();
    void  reveal(int index);
    [[nodiscard]] int       cardAt(gfx::Point p) const;
    [[nodiscard]] float     top() const;   // le haut de la premiere carte (sans defilement)
    [[nodiscard]] gfx::Rect trackRect() const;
    [[nodiscard]] gfx::Rect thumbRect() const;
    void  paintCard(const ui::PaintContext&, const ProjectEntry&, const gfx::Rect&, bool hot, bool sel) const;
    void  paintNewCard(const ui::PaintContext&, const gfx::Rect&, bool hot) const;

    std::vector<ProjectEntry> items_;
    std::string               emptyTitle_, emptyText_;
    int                       selected_{-1}, hover_{-1}, pressed_{-1}, lastDown_{-1};
    int                       columns_{2};
    float                     cardW_{300.f}, contentH_{0.f}, scroll_{0.f};
    bool                      dragging_{false};
    float                     dragGrab_{0.f};
};

// ------------------------------------------------------ le projet choisi ----
class ProjectDetail final : public ui::Widget {
public:
    explicit ProjectDetail(std::string id);
    void setEntry(const ProjectEntry* e);            // nul : rien de choisi
    [[nodiscard]] const ProjectEntry* entry() const noexcept { return entry_ ? &*entry_ : nullptr; }

    [[nodiscard]] StartButton& openButton() noexcept { return *open_; }
    [[nodiscard]] StartButton& stationButton() noexcept { return *station_; }
    [[nodiscard]] StartButton& duplicateButton() noexcept { return *duplicate_; }
    [[nodiscard]] StartButton& renameButton() noexcept { return *rename_; }
    [[nodiscard]] StartButton& removeButton() noexcept { return *remove_; }
    // La phrase sous les boutons (pourquoi le poste est grise...) ; vide : aucune.
    [[nodiscard]] std::string note() const;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    std::optional<ProjectEntry> entry_;
    StartButton*                open_{nullptr};
    StartButton*                station_{nullptr};
    StartButton*                duplicate_{nullptr};
    StartButton*                rename_{nullptr};
    StartButton*                remove_{nullptr};
    std::vector<std::string>    noteLines_;
    float                       headerBottom_{0.f}, noteY_{0.f}, actionsBottom_{0.f};
};

// ------------------------------------------------------ la colonne gauche ----
class StartRail final : public ui::Widget {
public:
    explicit StartRail(std::string id);

    [[nodiscard]] StartButton&   newCard() noexcept { return *new_; }
    [[nodiscard]] StartButton&   openCard() noexcept { return *open_; }
    [[nodiscard]] StartField&    pathField() noexcept { return *path_; }
    // Le bouton ... au bout du champ : l'explorateur (un export, ou le
    // project.xpgproj d'un dossier de projet) ; l'ecran l'ouvre ensuite.
    [[nodiscard]] ui::BrowseButton& browseButton() noexcept { return *browse_; }
    [[nodiscard]] StartButton&   tutorialLink() noexcept { return *tutorial_; }
    [[nodiscard]] StartButton&   helpLink() noexcept { return *help_; }
    [[nodiscard]] StartButton&   settingsLink() noexcept { return *settings_; }
    // 1.8.0 : les dossiers de l'application (XPGAnalyser.ini).
    [[nodiscard]] StartButton&   foldersLink() noexcept { return *folders_; }
    [[nodiscard]] ThemeSwatches& swatches() noexcept { return *swatches_; }
    [[nodiscard]] StartButton&   autostartButton() noexcept { return *autostart_; }

    // Le poste d'exploitation lance au demarrage du PC : le nom du projet ;
    // vide : aucun.
    void setAutostart(std::string projectName);
    [[nodiscard]] const std::string& autostart() const noexcept { return autostartName_; }
    // La pastille sous le nom : "lot API 7", et la date du jour.
    void setRelease(std::string tag, std::string date);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    // Tout ce qui est au-dessus du poste au demarrage ; rend le bas des
    // pastilles. `level` 0 a 3 : de plus en plus serre, pour les fenetres
    // basses (moins d'espace, puis des textes plus courts, puis sans eux).
    float placeBody(int level);

    StartButton*   new_{nullptr};
    StartButton*   open_{nullptr};
    StartField*    path_{nullptr};
    ui::BrowseButton* browse_{nullptr};
    StartButton*   tutorial_{nullptr};
    StartButton*   help_{nullptr};
    StartButton*   settings_{nullptr};
    StartButton*   folders_{nullptr};     // 1.8.0
    ThemeSwatches* swatches_{nullptr};
    StartButton*   autostart_{nullptr};
    std::string    autostartName_, tag_, date_;
    // Ce que onLayout calcule et que onPaint dessine.
    gfx::Rect                logo_{}, drop_{}, themeLabel_{}, station_{};
    std::vector<std::string> title_, tagline_, dropText_;
    float                    titleY_{0.f}, taglineY_{0.f}, chipY_{0.f}, textX_{0.f}, dropTextY_{0.f};
    // Le poste au demarrage du PC : une ligne pour le texte et le bouton a
    // droite ; dans une colonne etroite, le texte sur toute la largeur (trois
    // lignes) et le bouton dessous.
    bool                     stationStacked_{false};
    float                    stationTextY_{0.f};
};

// ------------------------------------------------------------ la page ----
class StartPage final : public ui::Widget {
public:
    explicit StartPage(std::string id);

    // Les projets, deja lus ; la selection reste sur `keepPath` s'il y est
    // encore, sinon a la meme place.
    void setEntries(std::vector<ProjectEntry> entries, const std::string& keepPath);
    [[nodiscard]] const std::vector<ProjectEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] const ProjectEntry*               selected() const noexcept;
    // Les projets montres, dans l'ordre des cartes.
    [[nodiscard]] std::vector<const ProjectEntry*>  visibleEntries() const;
    // Choisir un projet par son chemin (tel quel) ; faux s'il n'est pas montre.
    bool select(const std::string& path);
    bool selectVisible(int index);

    void setFilter(Filter f);
    [[nodiscard]] Filter filter() const noexcept { return filter_; }
    void setSort(Sort s);
    [[nodiscard]] Sort sort() const noexcept { return sort_; }
    void setQuery(const std::string& text);            // ecrit aussi dans le champ
    [[nodiscard]] const std::string& query() const noexcept { return query_; }

    // Ce qui coute a lire (les nombres du panneau de droite), fait par l'ecran
    // au premier choix d'un projet.
    void setDetailsLoader(std::function<void(ProjectEntry&)> loader) { loader_ = std::move(loader); }

    [[nodiscard]] StartRail&     rail() noexcept { return *rail_; }
    [[nodiscard]] StartField&    search() noexcept { return *search_; }
    [[nodiscard]] FilterChips&   chips() noexcept { return *chips_; }
    [[nodiscard]] ui::DropDown&  sortBox() noexcept { return *sortBox_; }
    [[nodiscard]] ProjectGrid&   grid() noexcept { return *grid_; }
    [[nodiscard]] ProjectDetail& detail() noexcept { return *detail_; }
    [[nodiscard]] ui::StatusBar& status() noexcept { return *status_; }
    // A droite de la barre d'etat : les raccourcis, puis le nom du programme.
    void setStatusRight(std::string hints, std::string program);

    const core::SignalPtr<> selectionChanged = core::Signal<>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void refilter(int fallback);   // `fallback` : la place a reprendre si le choix disparait
    void showSelection();

    std::vector<ProjectEntry>              entries_;
    std::vector<std::size_t>               visible_;      // des indices dans entries_
    std::size_t                            selected_{static_cast<std::size_t>(-1)};
    Filter                                 filter_{Filter::All};
    Sort                                   sort_{Sort::Recent};
    std::string                            query_;
    std::string                            counts_;       // "5 projets . 1 export"
    std::function<void(ProjectEntry&)>     loader_;
    bool                                   syncing_{false};

    StartRail*     rail_{nullptr};
    StartField*    search_{nullptr};
    FilterChips*   chips_{nullptr};
    ui::DropDown*  sortBox_{nullptr};
    ProjectGrid*   grid_{nullptr};
    ProjectDetail* detail_{nullptr};
    ui::StatusBar* status_{nullptr};
    ui::Widget*    hints_{nullptr};
    ui::Widget*    program_{nullptr};
    gfx::Rect      center_{}, header_{};
    core::ConnectionScope links_;
};

} // namespace app::start
