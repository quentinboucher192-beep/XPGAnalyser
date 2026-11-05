// =============================================================================
//  app/hmi/HmiSoundLibraryPane.hpp - 1.24.0 : IHM > Ressources > Bibliotheque de sons
// -----------------------------------------------------------------------------
//  Le client (1.14.2, repris le 05/11) : << une bibliotheque de sons : ecouter,
//  importer, Mes sons >>. Le moteur : hmi/HmiSoundLibrary.hpp.
//
//    [> Ecouter] [Importer dans le projet] | [+ Ajouter a Mes sons] [Retirer]   [Chercher un son...]
//    +-----------------------------------------------+---------------------------------+
//    | v Mes sons (2)                                | Alarme haute priorite           |
//    |     Mon bip          0,08 s   -                | Alarmes . 1,52 s . WAV          |
//    | v Alarmes (7)                                 | Deux salves de trois bips...    |
//    |   (cadenas) Alarme critique  2,0 s  (v) alarme-critique.wav | Dans le projet : pas encore |
//    |   ...                                         | [ forme d'onde, Lire ]          |
//    +-----------------------------------------------+---------------------------------+
//
//  Des dossiers par categorie (Mes sons en tete, puis les natifs : Alarmes,
//  Notifications, Interface, Procede), qui se replient ; un cadenas pour un son
//  natif, une personne pour un des tiens ; la colonne << Dans le projet >> dit la
//  ressource qui a deja ce son. A droite, la fiche : sa description, sa duree, et
//  l'apercu des fichiers (sa forme d'onde, Lire).
//
//  Ecouter (Espace, double-clic), Importer dans le projet (Entree : une ressource,
//  Ctrl+Z la retire ; le meme contenu deja la est reutilise), Ajouter a Mes sons
//  (un fichier WAV ou MP3 du disque, ou lache sur la liste), Retirer de Mes sons
//  (Suppr ; un natif se garde), Copier le nom. Le clic droit propose les memes
//  gestes, plus Ouvrir dans l'explorateur (un de Mes sons) et Aller a la ressource.
//  1.24.1 : plusieurs sons choisis (Ctrl+clic, Maj+clic) se retirent d'un coup
//  (<< Retirer les 3 >>, Suppr, le clic droit) : les natifs se gardent, le compte
//  rendu le dit ; un seul Ctrl+Z les rend (un seul retire aussi). Ecouter,
//  Importer, Copier, Ouvrir dans l'explorateur : un a la fois.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiSoundLibrary.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

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
class HmiSoundDetail;

class HmiSoundLibraryPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum Action : int { ActPlay = 1, ActImport, ActAdd, ActRemove, ActCopy, ActReveal, ActGoTo, ActAttach };

    struct Hosts {
        std::function<void(hmi::Id resource)>                   openResource;   // Aller a la ressource
        std::function<std::string(const std::string& path)>      reveal;         // l'Explorateur ; "" : fait
        std::function<void()>                                    askFile;        // + : choisir un fichier
        // 1.25.0 (la palette de l'editeur de vues) : la vue ouverte et ses objets choisis - << Jouer au clic >>.
        std::function<std::pair<hmi::Id, std::vector<hmi::Id>>()> chosenObjects;
    };

    // 1.25.0 : LE SOUS-ONGLET SONS de la BIBLIOTHEQUE de l'editeur de vues (le client : << ajouter un sous-onglet sons >>).
    // Le meme volet, en etroit : la liste seule (Son, Duree, un \xE2\x9C\x93 s'il est dans le projet), ses dossiers et leur
    // nombre ; pas de champ de recherche (celui de la bibliotheque le pilote, setSearch) ni de fiche (Ecouter joue quand
    // meme) ; en bas, le son choisi ou le mode d'emploi. En plus : << Jouer au clic de l'objet choisi >> (ActAttach).
    void setCompact(bool on);
    [[nodiscard]] bool compact() const noexcept { return compact_; }
    // Le son choisi, joue au clic des objets choisis de la vue : importe s'il ne l'est pas, puis une action << Jouer un
    // son >> au clic de chacun - UNE commande (Ctrl+Z). Rend le nombre d'objets ; 0 et `why` sinon.
    std::size_t attachToChosen(std::string* why = nullptr);

    HmiSoundLibraryPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    ~HmiSoundLibraryPane() override;
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    void refresh();

    // ---- les gestes (les essais, les scripts, le clic droit) ----
    bool select(const std::string& soundId);                     // "alarme-haute", "mes/Mon bip.wav"
    [[nodiscard]] std::string selectedId() const;
    [[nodiscard]] const hmi::soundlib::Sound* selectedSound() const;
    bool togglePlay(std::string* why = nullptr);                 // Ecouter / Arreter
    hmi::Id importSound(const std::string& soundId, std::string* why = nullptr);
    hmi::Id importSelected(std::string* why = nullptr) { return importSound(selectedId(), why); }
    bool addFile(const std::string& path, std::string* why = nullptr);
    // 1.24.1 : les sons choisis (Ctrl+clic, Maj+clic) ; plusieurs : Retirer les retire tous de Mes sons d'un coup (un
    // natif se garde, le compte rendu le dit) ; un seul Ctrl+Z les rend (un seul aussi).
    bool removeSelected(std::string* why = nullptr);
    [[nodiscard]] std::vector<std::string> selectedIds() const;
    bool selectIds(const std::vector<std::string>& ids);
    void setCategoryOpen(const std::string& category, bool open);
    [[nodiscard]] bool categoryOpen(const std::string& category) const;
    void setSearch(const std::string& text);

    // Ce que montre la liste (la recherche appliquee) : les dossiers et leur nombre, les sons dans l'ordre.
    [[nodiscard]] std::vector<std::pair<std::string, std::size_t>> shownCategories() const;
    [[nodiscard]] std::vector<std::string> shownIds() const;

    [[nodiscard]] std::vector<ui::PopupMenu::Item> contextItems() const;
    void runAction(int action);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     table() noexcept { return *table_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] HmiSoundDetail&    detail() noexcept { return *detail_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // un fichier lache : Mes sons

private:
    struct Line {
        bool        header{false};
        std::string category;
        std::size_t count{0};
        int         sound{-1};          // rang dans sounds_
    };
    void syncMine();
    void rebuild();
    void showDetail();
    void say(std::string text, bool error = false);
    bool fail(std::string text, std::string* why);
    [[nodiscard]] const hmi::soundlib::Sound* byId(const std::string& id) const;
    bool removeOne(const hmi::soundlib::Sound& s, std::string* why);   // 1.24.1 : une commande (Ctrl+Z le rend)

    hmi::DocumentPtr                  doc_;
    Apply                             apply_;
    Hosts                             hosts_;
    HmiToolStrip*                     tools_{nullptr};
    ui::SearchField*                  search_{nullptr};
    ui::TableView*                    table_{nullptr};
    HmiSoundDetail*                   detail_{nullptr};
    ui::StatusBar*                    status_{nullptr};
    std::shared_ptr<ui::ITableModel>  model_;
    std::vector<hmi::soundlib::Sound> sounds_;            // Mes sons, puis les natifs
    std::uint64_t                     mineRevision_{0};
    std::vector<Line>                 lines_;
    std::set<std::string>             closed_;            // les dossiers replies
    std::string                       query_;
    bool                              refreshing_{false};
    bool                              compact_{false};      // 1.25.0 : dans la palette de l'editeur de vues
    std::string                       message_;
    core::ConnectionScope             links_;
};

} // namespace app
