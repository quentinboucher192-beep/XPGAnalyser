// =============================================================================
//  app/hmi/HmiImageLibraryPane.hpp - 1.25.1 : IHM > Ressources > Bibliotheque d'images
// -----------------------------------------------------------------------------
//  Le client (05/11), en montrant le volet des sons : << fais un menu comme ca
//  pour bibliotheques d'images aussi >>. Le jumeau de HmiSoundLibraryPane ; le
//  moteur : hmi/HmiAssets.hpp (imagelib).
//
//    [Importer dans le projet] [Ouvrir dans l'editeur de vues] | [+ Ajouter a Mes images...] [Retirer]  [Chercher une image...]
//    +------------------------------------------------------------+-----------------------------+
//    | v Mes images (1)                                           |  +-----------------------+  |
//    |     (personne) [vignette] Logo     120 x 80  PNG  -        |  |  grand apercu, damier |  |
//    | v Fleches et navigation (24)                               |  +-----------------------+  |
//    |   (cadenas) [vignette] Fleche haut  64 x 64  SVG  (v) fleche-haut.svg | [Clair][Sombre][Damier] |
//    |   ...                                                      |  Fleche haut                |
//    +------------------------------------------------------------+  Fleches . 64 x 64 . SVG ... |
//
//  Des dossiers par categorie (Mes images en tete, puis celles de la bibliotheque :
//  Fleches et navigation, Interface...), qui se replient, avec leur nombre ; une
//  petite vignette devant le nom ; un cadenas pour une image native, une personne
//  pour une des tiennes ; la colonne << Dans le projet >> dit la ressource qui a deja
//  ce contenu (en n'importe quelle couleur, pour un dessin monochrome). A droite, la
//  fiche : le grand apercu (sur un damier, ou un fond clair, ou sombre), le nom, la
//  categorie, la taille, le format, ses mots-cles, la ressource, le poids.
//
//  Importer dans le projet (Entree : une ressource, Ctrl+Z la retire ; le meme
//  contenu deja la est reutilise ; un dessin monochrome prend la couleur Auto, celle
//  qui se lit sur le fond de la vue de demarrage - une autre couleur : le sous-onglet
//  Images de l'editeur de vues), Ouvrir dans l'editeur de vues (le sous-onglet Images
//  de sa BIBLIOTHEQUE, sur cette image ; aussi le double-clic), Ajouter a Mes images
//  (un fichier PNG, JPG, SVG... du disque, ou lache sur le volet), Retirer de Mes
//  images (Suppr ; plusieurs d'un coup, un seul Ctrl+Z les rend ; une native se
//  garde), Copier le nom. Le clic droit propose les memes gestes, plus Ouvrir dans
//  l'explorateur (une de Mes images) et Aller a la ressource.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiAssets.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ui {
class StatusBar;
class SearchField;
}

namespace app {

class HmiToolStrip;
class HmiImageDetail;

class HmiImageLibraryPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum Action : int { ActImport = 1, ActEditor, ActAdd, ActRemove, ActCopy, ActReveal, ActGoTo };
    // Le fond du grand apercu : un damier (la transparence se voit), clair, sombre.
    enum class Backdrop : std::uint8_t { Checker, Light, Dark };

    struct Hosts {
        std::function<void(hmi::Id resource)>                                       openResource;   // Aller a la ressource
        std::function<std::string(const std::string& path)>                         reveal;         // l'Explorateur ; "" : fait
        std::function<void()>                                                        askFile;        // + : choisir un fichier
        // Le sous-onglet Images de la BIBLIOTHEQUE de l'editeur de vues, ce dossier deplie, cette image montree.
        std::function<void(const std::string& folder, const std::string& imageId)>   openInEditor;
    };

    HmiImageLibraryPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    ~HmiImageLibraryPane() override;
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    void refresh();

    // ---- les gestes (les essais, les scripts, le clic droit) ----
    // L'arbre (Ressources > Bibliotheque d'images, un dossier, une image) : le dossier deplie, l'image choisie ; un
    // dossier seul : sa premiere image ; rien : la liste telle quelle. Faux : l'image n'est pas (ou plus) la.
    bool reveal(const std::string& folder, const std::string& imageId);
    bool select(const std::string& imageId);                      // "fleche-haut", "mes/Logo.png"
    [[nodiscard]] std::string selectedId() const;
    [[nodiscard]] const hmi::imagelib::Image* selectedImage() const;
    hmi::Id importImage(const std::string& imageId, std::string* why = nullptr);
    hmi::Id importSelected(std::string* why = nullptr) { return importImage(selectedId(), why); }
    bool addFile(const std::string& path, std::string* why = nullptr);
    // Les images choisies (Ctrl+clic, Maj+clic) ; plusieurs : Retirer les retire toutes de Mes images d'un coup (une
    // native se garde, le compte rendu le dit) ; un seul Ctrl+Z les rend (une seule aussi).
    bool removeSelected(std::string* why = nullptr);
    [[nodiscard]] std::vector<std::string> selectedIds() const;
    bool selectIds(const std::vector<std::string>& ids);
    void setCategoryOpen(const std::string& category, bool open);
    [[nodiscard]] bool categoryOpen(const std::string& category) const;
    void setSearch(const std::string& text);
    // La ressource du projet qui a deja ce contenu (un dessin monochrome : en n'importe quelle couleur) ; nul : aucune.
    [[nodiscard]] const hmi::Resource* importedAs(const hmi::imagelib::Image& image) const;
    // La couleur des traits d'un dessin monochrome a l'import : Auto, celle qui se lit sur le fond de la vue de
    // demarrage (claire sur un fond sombre) ; une image en couleurs : la sienne (kInk, rien ne change).
    [[nodiscard]] std::uint32_t importInk(const hmi::imagelib::Image& image) const;

    // Ce que montre la liste (la recherche appliquee) : les dossiers et leur nombre, les images dans l'ordre.
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> shownCategories() const;
    [[nodiscard]] std::vector<std::string> shownIds() const;

    // Le fond du grand apercu (le meme pour tous les volets ouverts) ; ses trois boutons (apres un premier dessin).
    void setBackdrop(Backdrop b);
    [[nodiscard]] Backdrop backdrop() const noexcept;
    [[nodiscard]] bool backdropRect(Backdrop b, gfx::Rect& out) const;

    [[nodiscard]] std::vector<ui::PopupMenu::Item> contextItems() const;
    void runAction(int action);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     table() noexcept { return *table_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::string& statusText() const noexcept { return statusText_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // un fichier lache : Mes images

private:
    struct Line {
        bool        header{false};
        std::string category;
        std::size_t count{0};
        int         image{-1};          // rang dans images_
    };
    void syncMine();
    void rebuild();
    void showDetail();
    void say(std::string text, bool error = false);
    bool fail(std::string text, std::string* why);
    [[nodiscard]] const hmi::imagelib::Image* byId(const std::string& id) const;
    bool removeOne(const hmi::imagelib::Image& img, std::string* why);   // une commande (Ctrl+Z la rend)
    [[nodiscard]] const hmi::Resource& thumbOf(std::size_t index, std::uint32_t ink);
    void paintThumb(gfx::IRenderer& r, int icon, const gfx::Rect& box, gfx::Color color);

    hmi::DocumentPtr                   doc_;
    Apply                              apply_;
    Hosts                              hosts_;
    HmiToolStrip*                      tools_{nullptr};
    ui::SearchField*                   search_{nullptr};
    ui::TableView*                     table_{nullptr};
    HmiImageDetail*                    detail_{nullptr};
    ui::StatusBar*                     status_{nullptr};
    std::shared_ptr<ui::ITableModel>   model_;
    std::vector<hmi::imagelib::Image>  images_;            // Mes images, puis les natives
    std::uint64_t                      mineRevision_{0};
    bool                               loaded_{false};
    std::vector<Line>                  lines_;
    std::set<std::string>              closed_;            // les dossiers replies
    std::string                        query_;
    bool                               refreshing_{false};
    std::uint32_t                      listInk_{0xE6EAF0};  // les vignettes monochromes : lisibles sur le fond de la liste
    std::vector<std::pair<std::uint32_t, hmi::Resource>> thumbs_;   // par image : la couleur et le contenu de sa vignette
    std::string                        message_;
    std::string                        statusText_;
    core::ConnectionScope              links_;
};

} // namespace app
