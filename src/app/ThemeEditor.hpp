// =============================================================================
//  app/ThemeEditor.hpp - l'editeur d'un theme (lot API 8)
// -----------------------------------------------------------------------------
//  Galerie des themes > Modifier (ou Nouveau) : un volet a droite, comme la
//  galerie, SANS assombrir l'ecran - chaque couleur changee se voit tout de
//  suite sur toute l'application (l'apercu en direct). Le volet, lui, garde
//  les couleurs du theme d'avant : on peut rendre le texte illisible sans
//  perdre l'editeur.
//
//    a gauche   les 53 couleurs, par groupe (Fonds, Textes, Accent et
//               selection, Etats, Bordures, Code, Familles et portees) :
//               chacune sa pastille, sa valeur #RRGGBB, et pour celles qui
//               portent du texte, leur contraste ("12,4:1", ou "3,9:1 - il
//               faut 4,5" en couleur d'alerte) ;
//    a droite   la couleur choisie : sa valeur a taper, trois reglettes
//               (teinte, saturation, luminosite), les couples ou elle entre
//               avec leur contraste, et le theme en miniature.
//
//  Deriver de l'accent : l'accent choisi, et le reste calcule (ui::
//  deriveFromAccent) - contrastes tenus. Corriger les contrastes : les couleurs
//  fautives eclaircies ou assombries, teinte gardee (ui::fixContrasts).
//  Ctrl+Z : le changement d'avant. Enregistrer (Ctrl+S) ecrit le fichier du
//  theme ; Annuler (Echap) rend le theme d'avant, comme si rien n'avait eu lieu.
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

class ThemeEditorDialog final : public menu::WidgetMenu {
public:
    struct Hosts {
        std::function<void(const ui::Theme&)>               preview;   // l'apercu en direct, sans rien retenir
        std::function<bool(const ui::Theme&, std::string&)> save;      // ecrire le fichier et l'appliquer ; faux : l'erreur
        std::function<void()>                               cancel;    // rendre le theme d'avant
    };

    ThemeEditorDialog(ui::Theme theme, Hosts hosts);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    void Update(const menu::FrameContext& f) override;
    void Render(gfx::IRenderer& r, const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // ---- les gestes, sans souris (scripts, tests) ----
    bool select(const std::string& key);                       // "texte.secondaire" (ou le libelle, "Texte secondaire")
    bool setColor(const std::string& key, const std::string& hex);
    bool setHsl(double hue, double saturation, double lightness);   // la couleur choisie ; h en degres, s et l en %
    bool derive(const std::string& accentHex = {}, int dark = -1);   // -1 : sombre si le theme l'est
    int  fixContrasts();                                       // le nombre de couleurs changees
    bool undo();
    bool save();                                               // Enregistrer
    void cancel();                                             // Annuler (Echap)
    bool exportTo(const std::string& path);                    // Exporter... : le theme tel qu'il est, dans un .xpgtheme
    void setName(const std::string& name);                     // le nom (verifie a l'enregistrement)
    void setAuthor(const std::string& author);
    bool setFamily(const std::string& family);                 // "Sombres"... (Contraste eleve : 7:1 partout)

    [[nodiscard]] const ui::Theme&   theme() const noexcept { return theme_; }
    [[nodiscard]] const std::string& selected() const noexcept { return selected_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

protected:
    core::Status buildUi() override;

private:
    class Body;
    friend class Body;
    void changed(bool snapshotFirst = true);    // apres une modification : l'apercu, la valeur, la liste
    void snapshot();
    void syncHex();
    void say(std::string text, bool error = false);
    void finish(bool keep);

    ui::Theme                theme_;
    ui::Theme                chrome_;           // le volet se dessine avec le theme d'avant
    Hosts                    hosts_;
    std::string              selected_;
    std::vector<ui::Theme>   undo_;
    std::string              message_;
    bool                     messageError_{false};
    double                   messageUntil_{0}, now_{0};
    bool                     done_{false};
    Body*                    body_{nullptr};
    ui::InputText*           hex_{nullptr};
    ui::InputText*           name_{nullptr};
    ui::InputText*           author_{nullptr};
    ui::Button*              family_{nullptr};
    ui::Button*              export_{nullptr};
    std::shared_ptr<char>    alive_{std::make_shared<char>('e')};   // l'explorateur repond plus tard
    ui::Button*              save_{nullptr};
    ui::Button*              cancel_{nullptr};
    ui::Button*              derive_{nullptr};
    ui::Button*              fix_{nullptr};
    ui::Button*              undoButton_{nullptr};
    bool                     syncing_{false};
    core::ConnectionScope    links_;
};

} // namespace app
