// =============================================================================
//  app/ThemeGallery.hpp - la galerie des themes (lot API 6, refaite au lot API 8)
// -----------------------------------------------------------------------------
//  Affichage > Theme... : un volet a droite, par-dessus l'ecran SANS l'assombrir
//  - on choisit un theme en regardant l'ecran le prendre. Chaque carte est
//  l'ecran en miniature dans ses propres couleurs, avec le contraste du texte
//  et du texte secondaire sur les panneaux. Un clic applique, tout de suite.
//
//  Garder (Entree) : le theme reste, retenu pour le prochain lancement.
//  Revenir (Echap) : le theme d'avant revient.
//
//  LOT API 8 : QUARANTE-TROIS THEMES, ET LES TIENS.
//    - Les cartes sont rangees par famille (A toi, Sombres, Clairs, Colores,
//      Contraste eleve, Industriels) ; des pastilles filtrent une famille, un
//      champ cherche (nom, famille, description) ; la liste defile.
//    - Sous la liste, ce qu'on fait du theme choisi : Nouveau (a partir de
//      lui : l'editeur s'ouvre), Modifier (un theme integre se duplique
//      d'abord - on ne demande que le nom), Dupliquer, Renommer, Supprimer
//      (les tiens seulement), Exporter... (un fichier .xpgtheme, le bouton ...
//      du systeme), Importer... (ou un .xpgtheme LACHE sur la galerie).
//    - Un theme qui ne tient pas un contraste est accepte, mais la galerie dit
//      lesquels et propose "Corriger les contrastes".
//  L'editeur : app/ThemeEditor.hpp.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../menu/IMenu.hpp"
#include "../ui/Theme.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class Button; class InputText; }

namespace app {

class ThemeGalleryDialog final : public menu::WidgetMenu {
public:
    struct Hosts {
        std::function<std::string()>            current;   // la cle du theme actif ("Dark", ou le nom d'un des tiens)
        std::function<void(const std::string&)> apply;     // l'applique a toute l'application (et le retient)
        // ---- Lot API 8 : themes ----
        std::function<void(const ui::Theme&)>   preview;   // un theme pas encore enregistre (l'editeur), sans le retenir
        std::function<std::string()>            folder;    // ou s'ouvre l'explorateur (Exporter, Importer)
    };

    explicit ThemeGalleryDialog(Hosts hosts);
    ~ThemeGalleryDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Pour les scripts et les tests : choisir une carte par sa cle ou son libelle.
    bool choose(const std::string& name);

    // ---- Lot API 8 : themes (les gestes, sans souris) ----
    // La famille montree ("Clairs", "A toi" / "Mes themes" : les tiens, "" : toutes).
    bool setFamily(const std::string& family);
    void setSearch(const std::string& text);
    [[nodiscard]] std::vector<std::string> visibleKeys() const;     // les cartes montrees, dans l'ordre
    // Nouveau a partir de `source` (vide : le theme choisi) : cree `name`, et
    // l'editeur s'ouvre si `edit`. Faux : le message dit pourquoi.
    bool createFrom(const std::string& name, const std::string& source, bool edit);
    bool renameTheme(const std::string& from, const std::string& to);
    bool deleteTheme(const std::string& name);                        // sans question (scripts)
    bool exportTo(const std::string& name, const std::string& path);
    bool importFrom(const std::string& path);
    bool fixContrastsOf(const std::string& name);                     // un des tiens ; "" : le choisi
    // Modifier : un des tiens s'ouvre dans l'editeur ; un integre est d'abord
    // duplique sous `copyName` (vide : "<libelle> (perso)").
    bool edit(const std::string& name, const std::string& copyName = {});
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] bool               messageIsError() const noexcept { return messageError_; }

    // Le contraste d'une couleur de texte sur un fond (WCAG), "13,6:1".
    [[nodiscard]] static double      contrast(gfx::Color text, gfx::Color background);
    [[nodiscard]] static std::string contrastText(double ratio);

protected:
    core::Status buildUi() override;

private:
    void finish(bool keep);
    void relabel();
    // ---- Lot API 8 : themes ----
    void say(std::string text, bool error = false);
    void reload();                                   // la liste a change (un theme ajoute, renomme...)
    void askName(const std::string& title, const std::string& explanation, const std::string& proposed,
                 const std::string& confirm, std::function<void(const std::string&)> done);
    void onNew();
    void onEdit();
    void onDuplicate();
    void onRename();
    void onDelete();
    void onExport();
    void onImport();
    // `before` : le theme a remettre si l'editeur est annule (vide : `name` tel
    // qu'enregistre) ; `fresh` : `name` vient d'etre cree pour l'editeur, et
    // Annuler le retire.
    void openEditor(const std::string& name, const std::string& before = {}, bool fresh = false);
    [[nodiscard]] std::string chosenKey() const;
    [[nodiscard]] std::string startFolder() const;

    Hosts                 hosts_;
    std::string           original_;
    std::string           shown_;
    ui::Button*           back_{nullptr};
    ui::Button*           keep_{nullptr};
    bool                  done_{false};
    core::ConnectionScope links_;
    // ---- Lot API 8 : themes ----
    class Body;
    Body*                    body_{nullptr};
    ui::InputText*           search_{nullptr};
    std::vector<ui::Button*> actions_;               // Nouveau, Modifier, Dupliquer, Renommer, Supprimer, Exporter, Importer
    ui::Button*              fix_{nullptr};          // Corriger les contrastes
    std::string              message_;
    bool                     messageError_{false};
    double                   messageUntil_{0};
    double                   now_{0};
    std::size_t              userCount_{0};
    std::shared_ptr<char>    alive_;                 // l'explorateur repond plus tard : la galerie est-elle encore la ?
};

} // namespace app
