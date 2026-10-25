// =============================================================================
//  app/hmi/HmiLibraryPanes.hpp - 1.13.0 (lot J8) : IHM > Espaces et bibliotheques
// -----------------------------------------------------------------------------
//  LA PAGE DES ESPACES DE NOMS ET DES BIBLIOTHEQUES RELIEES (Programmation
//  generale > Bibliotheques dans l'arbre). Deux listes :
//    - les BIBLIOTHEQUES reliees : nom, version, etat (reliee, plus recente dispo,
//      plus ancienne, incompatible, introuvable) avec sa pastille, chemin, contenu
//      (types, fonctions, scripts) - chacune verrouillee (lecture seule) ;
//    - les ESPACES du projet : leur nom, ce qu'ils rangent (types, fonctions,
//      scripts), un espace vide garde.
//  La fiche de droite dit la bibliotheque choisie (sa version, son plancher, son
//  chemin, ce qu'on a trouve sur le disque, son interface) ou l'espace choisi.
//  Les gestes - Relier..., Publier..., Mettre a jour..., Localiser..., Delier,
//  Nouvel espace..., Renommer l'espace... - sont chacun UNE commande (Ctrl+Z) ; les
//  fenetres (le fichier a choisir, le diff de la mise a jour) viennent de l'ecran
//  (Hosts), les gestes eux-memes sont ici (pour les boutons, les scripts, les essais).
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiLibrary.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiLibrariesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiLibrariesPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // ---- les gestes (chacun une commande ; faux et `why` : refuse, rien ne change) ----
    bool link(const std::string& file, const std::string& alias = {}, std::string* why = nullptr);
    bool unlink(const std::string& name, std::string* why = nullptr);
    bool relocate(const std::string& name, const std::string& file, std::string* why = nullptr);
    // Mettre a jour : relire le fichier du lien (ou `file`), figer la version et l'interface lues.
    bool update(const std::string& name, const std::string& file = {}, std::string* why = nullptr);
    // Publier : ecrire le .xpgbib (rien ne change dans le projet).
    bool publish(const hmi::biblio::PublishSpec& spec, const std::string& file, std::string* why = nullptr);
    bool addNamespace(const std::string& ns, std::string* why = nullptr);
    bool renameNamespace(const std::string& from, const std::string& to, std::string* why = nullptr);
    bool removeNamespace(const std::string& ns, std::string* why = nullptr);
    // Relire les bibliotheques sur le disque (un fichier change a cote) : pas une modification du projet.
    void reload();

    [[nodiscard]] std::string selectedLibrary() const;
    void selectLibrary(const std::string& name);
    [[nodiscard]] std::string selectedNamespace() const;
    void selectNamespace(const std::string& ns);

    [[nodiscard]] ui::TableView&    libraries() noexcept { return *libraries_; }
    [[nodiscard]] ui::TableView&    spaces() noexcept { return *spaces_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    // Les fenetres de l'ecran (le fichier a choisir, le diff a confirmer) ; sans elles, rien ne s'ouvre.
    struct Hosts {
        std::function<void()>                   askLink;        // Relier une bibliotheque...
        std::function<void()>                   askPublish;     // Publier une bibliotheque...
        std::function<void(const std::string&)> askUpdate;      // Mettre a jour la bibliotheque (le diff)
        std::function<void(const std::string&)> askLocate;      // Localiser... (le fichier deplace)
        std::function<void()>                   askNewNamespace;
        std::function<void(const std::string&)> askRenameNamespace;
        std::function<void(const std::string&)> openDefinition; // un nom complet : sa definition en lecture seule
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // Le dossier du projet (les chemins relatifs des liens en partent).
    [[nodiscard]] static std::string projectFolder();
    // Les actions de la barre (pour les scripts et les essais).
    enum Action : int { ALink = 1, APublish, AUpdate, ALocate, AUnlink, AReload, ANewSpace, ARenameSpace, ARemoveSpace };

protected:
    void onLayout() override;

private:
    void rebuildProperties();
    void say(std::string text, bool error = false);
    bool commit(const std::string& label, const std::function<bool(hmi::Project&, std::string*)>& change, std::string* why);
    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::TableView*    libraries_{nullptr};
    ui::TableView*    spaces_{nullptr};
    HmiTitledPanel*   librariesPanel_{nullptr};
    HmiTitledPanel*   spacesPanel_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::vector<std::string> libraryNames_;   // dans l'ordre des lignes
    std::vector<std::string> spaceNames_;
    bool              focusSpaces_{false};    // la fiche dit l'espace choisi (sinon la bibliotheque)
    std::string       message_;
    core::ConnectionScope links_;
};

} // namespace app
