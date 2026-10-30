// =============================================================================
//  app/FileFinderPane.hpp - 1.14.0 (lot M5) : la fenetre "Chercher des fichiers"
// -----------------------------------------------------------------------------
//  LA MAQUETTE VALIDEE (scene 7 de maquette-1140) :
//
//    [Nom contient...] Ou : [Tout le PC (C:, D:)] [Chercher] [Arreter]  Recherche... 62 311 dossiers lus . 108 fichiers . D:\...
//    EXTENSIONS            | Afficher : [.ddt 7] [.dfb 5] [.xihm 9] ...
//    PROJETS               | Nom v | Type | Dossier | Taille | Modifie le | Version
//    [x] .xpgproj  Projet 8|  ...
//    ...                   |
//    [Tout] [Rien] [Celles d'IHM Studio]
//    OPTIONS               |
//    [x] Lecteurs reseau   | [Ouvrir] [Ouvrir le dossier] [Copier le chemin] [Importer dans le projet] [Ajouter aux recents]
//
//  Un onglet de document dans l'ecran du projet (on continue a travailler pendant
//  la recherche), un ecran depuis l'accueil. Le meme volet : FinderWorkspace.cpp
//  le pose et fait ses gestes (Hosts). Il montre la recherche partagee
//  (finder::shared()) : ses resultats restent tant que l'appli est ouverte.
//
//  LES CASES DES EXTENSIONS ET LES PUCES SONT LE MEME FILTRE : on cherche tout ce
//  qui est au catalogue (le parcours est le meme), on montre ce qui est coche ;
//  le nombre trouve est a droite de chaque extension et dans sa puce. Le choix
//  (les extensions, ou chercher, les options) est garde dans les reglages.
// =============================================================================
#pragma once

#include "FileFinder.hpp"

#include "../core/Signal.hpp"
#include "../ui/Widget.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ui {
class Button;
class Checkbox;
class DropDown;
class InputText;
class BrowseButton;
class ScrollablePanel;
class TableView;
class PopupMenu;
}

namespace app {

class FinderModel;
class FinderSide;
class FinderChips;

class FileFinderPane final : public ui::Widget {
public:
    enum Column : std::size_t { ColName = 0, ColType, ColDir, ColSize, ColDate, ColVersion, ColCount };
    enum class Act : std::uint8_t { Open, Reveal, CopyPath, Import, Recent };
    enum class Option : std::uint8_t { Network, Removable, SkipSystem, Zips };

    // Ce que l'ecran fait pour le volet. Absent : le geste est grise (les essais).
    struct Hosts {
        std::function<finder::Context()>                 context;     // ce qui est ouvert (le projet, l'endroit)
        std::function<void(const finder::Hit&)>          open;        // Ouvrir : ici, ou l'autre application
        std::function<void(const finder::Hit&)>          import;      // Importer, selon le genre
        std::function<void(const finder::Hit&)>          addRecent;   // Ajouter aux recents
        std::function<std::string(const std::string&)>   reveal;      // l'Explorateur ; vide : fait (absent : finder::revealInExplorer)
        std::function<void(const std::string&)>          saveChoices; // les reglages (finder::serialize)
        std::function<void()>                            help;        // F1
    };

    explicit FileFinderPane(std::string id, finder::Search& search = finder::shared());
    ~FileFinderPane() override;
    void setHosts(Hosts h);

    // ---- le choix (les reglages) ----------------------------------------------
    void setChoices(const finder::Choices& c);
    [[nodiscard]] finder::Choices choices() const;

    // ---- la recherche ----------------------------------------------------------
    void start();                              // Chercher
    void stop();                               // Arreter
    // L'ecran l'appelle a chaque image : les fichiers trouves depuis, la progression.
    // Le tableau se refait au plus quatre fois par seconde (`force` : tout de suite).
    void poll(bool force = false);
    [[nodiscard]] bool searching() const noexcept { return progress_.running; }
    [[nodiscard]] const finder::Progress& progress() const noexcept { return progress_; }
    [[nodiscard]] std::string progressLine() const { return finder::progressText(progress_); }
    void setWhere(finder::Where w, std::string folder = {});
    [[nodiscard]] finder::Where where() const;
    void setOption(Option o, bool on);
    [[nodiscard]] bool option(Option o) const;
    // Les essais : ces dossiers au lieu de ceux de "Ou chercher" (et ceux-la refuses).
    void setTestRoots(std::vector<std::string> roots, std::vector<std::string> refused = {}) {
        testRoots_ = std::move(roots);
        testRefused_ = std::move(refused);
    }

    // ---- le filtre -------------------------------------------------------------
    void setNameFilter(std::string text);
    [[nodiscard]] const std::string& nameFilter() const noexcept { return nameFilter_; }
    void setExtension(std::string_view key, bool on);
    [[nodiscard]] bool extensionOn(std::string_view key) const;
    void checkAll();
    void checkNone();
    void checkMine();                          // Celles d'API Studio / d'IHM Studio
    void toggleChip(std::string_view key);     // une puce : montrer / cacher son extension
    [[nodiscard]] std::size_t countOf(std::string_view key) const;   // trouves (sans le filtre)
    [[nodiscard]] std::vector<std::string> chipKeys() const;          // les extensions trouvees, dans l'ordre des puces

    // ---- les resultats ----------------------------------------------------------
    [[nodiscard]] const std::vector<finder::Hit>& hits() const noexcept { return hits_; }
    // Ce que montre le tableau, dans son ordre (le filtre, puis le tri).
    [[nodiscard]] std::vector<const finder::Hit*> rows() const;
    void sortBy(Column c, bool ascending);
    bool select(const std::string& path);
    [[nodiscard]] const finder::Hit* selected() const;
    [[nodiscard]] finder::Gesture gesture(Act a) const;   // pour le fichier choisi
    bool run(Act a);                                      // faux : grise (message() dit pourquoi)
    // Le double-clic, Entree : Ouvrir ; sinon Importer ; sinon rien (le message dit pourquoi).
    bool activate();
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    // Ce que l'ecran dit d'un geste (au pied de la page, a la place du chemin choisi).
    void tell(std::string text) { say(std::move(text)); }

    // ---- les widgets (les essais, les scripts) -----------------------------------
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ui::InputText& nameField() noexcept { return *name_; }
    [[nodiscard]] ui::DropDown&  whereBox() noexcept { return *where_; }
    [[nodiscard]] ui::Button&    searchButton() noexcept { return *go_; }
    [[nodiscard]] ui::Button&    stopButton() noexcept { return *stop_; }
    [[nodiscard]] ui::Button&    actionButton(Act a) noexcept;
    [[nodiscard]] ui::Button&    mineButton() noexcept;
    [[nodiscard]] ui::Checkbox*  extensionBox(std::string_view key) noexcept;
    // Le menu du clic droit sur la ligne choisie : ses entrees (le titre en tete).
    [[nodiscard]] std::vector<std::string> contextLabels() const;
    bool chooseContext(Act a);                            // une entree du menu

    ui::EventResult onEvent(const ui::InputEvent&) override;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    void onPaintOverlay(const ui::PaintContext&) override;

private:
    friend class FinderModel;
    friend class FinderSide;
    friend class FinderChips;
    [[nodiscard]] bool passes(const finder::Hit& h) const;
    void refilter();                       // tout le filtre, de nouveau
    void refreshTable(bool keepScroll);    // le modele a change
    void refreshButtons();
    void say(std::string text);
    void saveChoices();
    [[nodiscard]] finder::Context context() const;

    finder::Search&               search_;
    Hosts                         hosts_;
    std::vector<finder::Hit>      hits_;
    std::vector<std::size_t>      rows_;           // les fichiers montres (rangs dans hits_), dans l'ordre d'arrivee
    std::vector<std::size_t>      counts_;         // par extension du catalogue (son rang)
    std::vector<bool>             on_;             // par extension du catalogue : cochee
    std::string                   nameFilter_;
    std::string                   selectedPath_;
    std::string                   message_;
    std::string                   folder_;
    finder::Progress              progress_;
    std::uint64_t                 generation_{0};
    std::uint64_t                 mine_{0};        // la recherche lancee depuis ce volet (l'arreter s'il se ferme)
    std::vector<std::string>      testRoots_;
    std::vector<std::string>      testRefused_;
    double                        lastRefresh_{-1.0};
    bool                          pendingRefresh_{false};
    bool                          filling_{false};
    bool                          options_[4]{true, true, true, false};
    std::shared_ptr<FinderModel>  model_;
    ui::InputText*                name_{nullptr};
    ui::DropDown*                 where_{nullptr};
    ui::InputText*                folderField_{nullptr};
    ui::BrowseButton*             browse_{nullptr};
    ui::Button*                   go_{nullptr};
    ui::Button*                   stop_{nullptr};
    ui::ScrollablePanel*          sidePanel_{nullptr};
    FinderSide*                   side_{nullptr};
    FinderChips*                  chips_{nullptr};
    ui::TableView*                table_{nullptr};
    ui::Button*                   actions_[5]{};
    gfx::Rect                     barRect_{}, footRect_{}, sideRect_{};
    core::ConnectionScope         links_;
};

} // namespace app
