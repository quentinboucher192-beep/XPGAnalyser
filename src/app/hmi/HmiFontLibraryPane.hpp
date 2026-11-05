// =============================================================================
//  app/hmi/HmiFontLibraryPane.hpp - 1.26.0 : IHM > Ressources > Bibliotheque de polices
// -----------------------------------------------------------------------------
//  Le client (05/11) : << ajouter des TTF dans les ressources de base + onglet
//  Polices dans bibliotheques editeur de vue, pouvoir changer de police partout,
//  avoir apercu des TTF, pouvoir editer des TTF en live, en creer, en dupliquer a
//  partir de >>. Le moteur : hmi/HmiFontLibrary.hpp.
//
//    [Importer dans le projet] [+ Ajouter a Mes polices...] | [Nouvelle police] [Dupliquer a partir de...] [Modifier] |
//    [Supprimer]                                                                   [Rechercher (nom, style, Arial...)]
//    +-- Texte [Pompe P1 en marche...]  Taille [15]  [Normal|Gras] --+--- APERCU . CARLITO ------ [Normal|Gras] ---+
//    | MES POLICES . 2                                              | Aa Bb Ee 0123   (en grand)                   |
//    |   Ma police Station    Pompe P1 en marche...   dans le projet | 10 / 14 / 20 / 30 : le texte                  |
//    | SANS EMPATTEMENT . 3                                          | DANS UNE VUE (fond sombre : un bouton, une val.)|
//    |   Liberation Sans      Pompe P1 en marche...                  | GLYPHES . 2 612 (double-clic : l'editeur)     |
//    |   (cadenas) les memes chasses qu'Arial . Normal, Gras, Italique| Fichier, Sorte, Licence (lire), Dans le projet|
//    | ...                                                            | [Importer dans le projet] [Dupliquer pour...] |
//    +----------------------------------------------------------------+-----------------------------------------------+
//
//  UNE LIGNE PAR FAMILLE (ses styles dedans), son nom ECRIT DANS SA POLICE, puis le
//  texte d'apercu (a taper, sa taille, Normal ou Gras) dans sa police ; << dans le
//  projet >> : un de ses styles est deja une ressource. Mes polices en tete, puis
//  les trois dossiers des fournies. A droite, l'apercu du style choisi.
//
//  Importer (Entree, une ressource ; Ctrl+Z la retire), Ajouter a Mes polices (un
//  TTF ou OTF du disque, ou lache sur le volet), Supprimer (Suppr : Mes polices
//  seulement, plusieurs d'un coup, un seul Ctrl+Z les rend). NOUVELLE POLICE,
//  DUPLIQUER A PARTIR DE..., MODIFIER et le double-clic sur un glyphe ouvrent
//  l'EDITEUR DE POLICES (Hosts) ; sans hote, ces boutons sont grises.
//
//  EN ETROIT (setCompact) : LE SOUS-ONGLET POLICES de la bibliotheque de l'editeur
//  de vues - << DU PROJET >> (ou elles servent), puis la bibliotheque par dossiers
//  (qui se replient), chaque police ecrite dans elle-meme. Appuyer sur une ligne :
//  `picked` (l'editeur arme le lacher : une police glissee sur un objet lui est
//  donnee) ; double-clic ou Entree : `activated` (la selection de la vue la prend).
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiFontLibrary.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ui {
class StatusBar;
class SearchField;
}

namespace app {

class HmiToolStrip;
class HmiFontList;
class HmiFontPreview;

// Une police choisie dans le sous-onglet : une du projet (sa ressource) ou une de la bibliotheque (importee en la donnant).
struct HmiFontPick {
    std::string                       resource;   // une police du projet ; vide : `font`
    std::optional<hmi::fontlib::Font> font;       // une police de la bibliotheque
    std::string                       label;      // "Carlito", "Carlito Gras"
    [[nodiscard]] bool valid() const noexcept { return !resource.empty() || font.has_value(); }
};

class HmiFontLibraryPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum Action : int { ActImport = 1, ActAdd, ActNew, ActDuplicate, ActEdit, ActRemove, ActCopy, ActReveal, ActGoTo, ActGive, ActLicense };

    struct Hosts {
        std::function<void(hmi::Id resource)>               openResource;   // Aller a la ressource
        std::function<std::string(const std::string& path)> reveal;         // l'Explorateur ; "" : fait
        std::function<std::string(const std::string& path)> openFile;       // la licence (le programme du systeme) ; "" : fait
        std::function<void()>                               askFile;        // + : choisir un fichier
        // L'EDITEUR DE POLICES (branche par l'ecran) : une neuve, une copie a partir d'une police, modifier une des tiennes
        // (une fournie : la dupliquer). Absents : les boutons sont grises.
        std::function<void()>                               newFont;
        std::function<void(const hmi::fontlib::Font&)>      duplicateFont;
        std::function<void(const hmi::fontlib::Font&)>      editFont;
        // Le double-clic sur un glyphe de l'apercu : l'editeur sur ce glyphe ; absent : editFont (une fournie : duplicateFont).
        std::function<void(const hmi::fontlib::Font&, char32_t)> editGlyph;
        // Le sous-onglet de l'editeur de vues : la police donnee aux objets choisis (double-clic, Entree, Donner a la
        // selection) ; rend le nombre d'objets (0 et `why` : rien).
        std::function<std::size_t(const HmiFontPick&, std::string* why)> give;
    };

    HmiFontLibraryPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    ~HmiFontLibraryPane() override;
    void setHosts(Hosts h);
    // Un hote a la fois (l'editeur de vues pose `give`, l'ecran l'editeur de polices).
    [[nodiscard]] Hosts& hosts() noexcept { return hosts_; }
    void refresh();

    // ---- en etroit : le sous-onglet Polices de l'editeur de vues ----
    void setCompact(bool on);
    [[nodiscard]] bool compact() const noexcept { return compact_; }
    // Appuyer sur une ligne (le glisser commence), un double-clic ou Entree.
    const core::SignalPtr<const HmiFontPick&> picked = core::Signal<const HmiFontPick&>::create();
    const core::SignalPtr<const HmiFontPick&> activated = core::Signal<const HmiFontPick&>::create();
    // La police de la ligne choisie (etroit : une du projet ou de la bibliotheque).
    [[nodiscard]] HmiFontPick currentPick() const;
    bool selectProjectFont(const std::string& resource);       // etroit : une ligne << Du projet >>

    // ---- les gestes (les essais, les scripts, le clic droit) ----
    // Une famille, par l'id d'un de ses styles ("carlito", "mes/Ma police.ttf") ; le style montre suit.
    bool select(const std::string& fontId);
    [[nodiscard]] std::string selectedId() const;                       // le style montre de la famille choisie
    [[nodiscard]] const hmi::fontlib::Family* selectedFamily() const;
    [[nodiscard]] const hmi::fontlib::Font* selectedFont() const;       // le style montre (Normal / Gras)
    [[nodiscard]] std::vector<std::string> selectedIds() const;         // une par famille choisie (son style Normal)
    bool selectIds(const std::vector<std::string>& ids);
    void setBold(bool bold);                                            // Normal / Gras (la liste et l'apercu)
    [[nodiscard]] bool bold() const noexcept { return bold_; }
    void setSampleText(const std::string& text);
    [[nodiscard]] const std::string& sampleText() const noexcept { return sample_; }
    void setSampleSize(int px);
    [[nodiscard]] int sampleSize() const noexcept { return size_; }
    void setSearch(const std::string& text);
    void setCategoryOpen(const std::string& category, bool open);
    [[nodiscard]] bool categoryOpen(const std::string& category) const;

    hmi::Id importSelected(std::string* why = nullptr);
    hmi::Id importFont(const std::string& fontId, std::string* why = nullptr);
    bool addFile(const std::string& path, std::string* why = nullptr);
    // Mes polices seulement ; plusieurs familles d'un coup ; un seul Ctrl+Z les rend.
    bool removeSelected(std::string* why = nullptr);
    bool editGlyph(char32_t code, std::string* why = nullptr);         // le double-clic sur un glyphe de l'apercu

    // Ce que montre la liste (la recherche appliquee) : les dossiers et leur nombre de familles, les familles (le nom).
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> shownCategories() const;
    [[nodiscard]] std::vector<std::string> shownFamilies() const;
    // Etroit : les polices << Du projet >> montrees (le nom de leur ressource).
    [[nodiscard]] std::vector<std::string> shownProjectFonts() const;
    // Le rectangle d'une famille (de son nom de famille) ou d'une police du projet dans la liste ; faux : pas montree.
    [[nodiscard]] bool familyRect(const std::string& label, gfx::Rect& out) const;
    [[nodiscard]] bool projectFontRect(const std::string& resource, gfx::Rect& out) const;
    // Les glyphes de l'apercu (les essais : leur rectangle, pour un double-clic).
    [[nodiscard]] bool glyphRect(char32_t code, gfx::Rect& out) const;

    [[nodiscard]] std::vector<ui::PopupMenu::Item> contextItems() const;
    void runAction(int action);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] HmiFontList&       list() noexcept { return *list_; }
    [[nodiscard]] HmiFontPreview&    preview() noexcept { return *preview_; }
    [[nodiscard]] ui::InputText&     sampleField() noexcept { return *sampleField_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] ui::PopupMenu*     contextMenu() const noexcept { return menu_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // un fichier lache : Mes polices

private:
    friend class HmiFontList;
    friend class HmiFontPreview;
    void syncFonts();
    void rebuild();
    void rowChosen();          // la liste : une ligne choisie (l'apercu suit)
    void rowActivated();       // double-clic, Entree : importer (etroit : donner a la selection)
    void say(std::string text, bool error = false);
    bool fail(std::string text, std::string* why);
    void openMenu(gfx::Point at);
    [[nodiscard]] const hmi::fontlib::Family* familyById(const std::string& id) const;
    bool removeFamily(const hmi::fontlib::Family& f, std::string* why);   // une commande (Ctrl+Z la rend)

    hmi::DocumentPtr                     doc_;
    Apply                                apply_;
    Hosts                                hosts_;
    HmiToolStrip*                        tools_{nullptr};
    ui::SearchField*                     search_{nullptr};
    ui::InputText*                       sampleField_{nullptr};
    ui::DropDown*                        sizeBox_{nullptr};
    ui::ToggleButton*                    normalBtn_{nullptr};
    ui::ToggleButton*                    boldBtn_{nullptr};
    HmiFontList*                         list_{nullptr};
    HmiFontPreview*                      preview_{nullptr};
    ui::StatusBar*                       status_{nullptr};
    ui::PopupMenu*                       menu_{nullptr};
    std::vector<hmi::fontlib::Family>    families_;            // Mes polices, puis les fournies
    std::uint64_t                        revision_{0};
    std::string                          query_;
    std::string                          sample_;
    int                                  size_{15};
    bool                                 bold_{false};
    bool                                 compact_{false};
    std::string                          message_;
    core::ConnectionScope                links_;
};

} // namespace app
