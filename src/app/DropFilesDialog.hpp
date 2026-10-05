// =============================================================================
//  app/DropFilesDialog.hpp - lot API 8 : "Que faire de ces fichiers ?"
// -----------------------------------------------------------------------------
//  LE DIALOGUE D'UN DEPOT. Des fichiers laches sur la fenetre (projet ouvert)
//  qui ne sont pas tous des .XPG / .XHW : un seul dialogue, une section par
//  genre, dans l'ordre ou Faire les fait.
//
//    PROGRAMME ET CONFIGURATION (.XPG, .XHW) : les choix du lot 7, un seul
//    (les deux, le MAST, la configuration, un projet a part) - ou rien.
//
//    UN CLASSEUR (une section par classeur) : tout ce qu'on peut en faire,
//    une case chacune, rangees en trois temps - importer maintenant, ajouter
//    au projet, les macros. Chaque ligne dit ce qu'elle fera et ce qu'elle a
//    reconnu ; grisee, pourquoi ; une liste quand il faut choisir ou (la
//    recette, la table d'animation).
//
//    LES AUTRES FICHIERS : une ligne par fichier (sa vignette pour une image,
//    son genre, son poids) et deux cases, Ressources et Fichiers externes ;
//    deja la : Remplacer / Garder les deux / Ignorer.
//
//  Le plan (dropfiles::Plan) est PARTAGE avec l'ecran : les cases l'ecrivent.
//  Faire ferme le dialogue (DialogResult Ok) et l'ecran fait ce qui est coche ;
//  Annuler (ou Echap) ne touche a rien. Le bouton dit combien de choses il
//  fera ; la note du bas, dans quel ordre.
// =============================================================================
#pragma once

#include "DropFilesPlan.hpp"
#include "../menu/IMenu.hpp"
#include "../ui/widgets/Controls.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ui { class ScrollablePanel; }

namespace app {

class DropFilesBody;
class DropFilesContent;

class DropFilesDialog final : public menu::WidgetMenu {
public:
    explicit DropFilesDialog(std::shared_ptr<dropfiles::Plan> plan);
    ~DropFilesDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- pour les scripts et les tests ------------------------------------------
    //  Une case par le debut de son libelle, sans casse ni accents : "Lancer
    //  ImporterCartes", "Importer les traductions" ; un choix du .XPG ("Les
    //  deux", "Ne pas") ; une case d'un fichier : "logo.png : Ressources",
    //  "logo.png : Fichiers externes". Faux (et why) : introuvable, ou grisee
    //  (sa raison), ou un choix du .XPG qu'on ne decoche pas.
    bool check(const std::string& label, bool on, std::string* why = nullptr);
    //  La liste d'une ligne : la recette, la table d'animation (par le debut du
    //  libelle de la ligne) ; pour un fichier ("logo.png") : Remplacer, Garder
    //  les deux, Ignorer. L'element par le debut de son libelle.
    bool choose(const std::string& label, const std::string& item, std::string* why = nullptr);
    //  Ce que montre le dialogue, une ligne par case : "[x] ...", "[ ] ...",
    //  "[-] ... (grisee : ...)", "(o) ..." pour le choix du .XPG.
    [[nodiscard]] std::vector<std::string> lines() const;
    [[nodiscard]] std::size_t count() const;          // ce que Faire fera
    [[nodiscard]] const dropfiles::Plan& plan() const noexcept { return *plan_; }
    void confirm();                                   // Faire
    void cancel();                                    // Annuler, Echap

    // ---- Lot API 8 : glisser de fichiers, 2e partie ----
    //  Des fichiers laches sur le dialogue deja ouvert s'y ajoutent : lus avec
    //  le contexte du depot (setContext, donne par l'ecran), mis a la suite de
    //  leur section, leurs cases comme au premier depot ; un fichier deja dans
    //  la liste est laisse ; un .XPG / .XHW de plus aussi (il se depose seul).
    //  Rend le nombre de lignes ajoutees (0 et why : rien de nouveau).
    void setContext(dropfiles::Context ctx) { context_ = std::move(ctx); }
    std::size_t addFiles(const std::vector<std::string>& paths, std::string* why = nullptr);
    // ---- fin Lot API 8 : glisser de fichiers, 2e partie ----

protected:
    core::Status buildUi() override;

private:
    friend class DropFilesBody;
    friend class DropFilesContent;
    void sync();                                      // le bouton, la note, apres chaque case
    void finish(bool ok);
    [[nodiscard]] std::string orderNote() const;

    std::shared_ptr<dropfiles::Plan> plan_;
    DropFilesBody*                   body_{nullptr};
    DropFilesContent*                content_{nullptr};
    ui::ScrollablePanel*             scroll_{nullptr};
    ui::Button*                      ok_{nullptr};
    ui::Button*                      cancel_{nullptr};
    bool                             done_{false};
    core::ConnectionScope            links_;
    dropfiles::Context               context_;       // lot API 8 : pour addFiles
};

} // namespace app
