// =============================================================================
//  app/CodeIconPicker.hpp - 1.8.0 : le mini-menu des icones au choix
// -----------------------------------------------------------------------------
//  Clic droit > Definir l'icone... sur une section, une unite de programme, un
//  bloc DFB, un type DDT, un script ou une fonction de l'IHM (plusieurs choisis :
//  une fois pour tous). Les dix-huit icones du catalogue (core/CodeIcons.hpp) sur
//  deux colonnes, chacune avec son nom et sa description ; l'actuelle cadree ;
//  celle que le nom suggere marquee "suggeree" (l'application ne choisit
//  jamais). Un clic choisit et ferme ; les fleches et Entree aussi ; "Aucune
//  icone" rend celle de toujours ; Echap ferme sans rien changer.
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../menu/IMenu.hpp"

#include <optional>
#include <string>

namespace app {

class CodeIconPicker final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string title;          // "Ic\xC3\xB4ne de la section SFC_ManuB"
        std::string current;        // la cle actuelle ("" : aucune ; plusieurs differentes : "")
        std::string suggestion;     // la cle suggeree d'apres le nom ("" : aucune)
    };
    explicit CodeIconPicker(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // La reponse (DialogResult::payload d'un Ok) : la cle choisie, "" pour aucune.
    [[nodiscard]] static std::string parse(const std::string& payload);

    void choose(int index);          // -1 : aucune icone (pour les scripts et les tests)
    void cancel();

protected:
    core::Status buildUi() override;

private:
    friend class PickerBody;
    Spec spec_;
    int  hot_{-1};
    bool done_{false};
    core::ConnectionScope links_;
};

} // namespace app
