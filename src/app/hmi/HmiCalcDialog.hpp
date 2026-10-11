// =============================================================================
//  app/hmi/HmiCalcDialog.hpp - 1.12.7 : LA FENETRE D'UNE VALEUR CALCULEE AU DEMARRAGE
// -----------------------------------------------------------------------------
//  « ƒx Calcul... » d'une constante ou d'une variable d'un script (la grille des
//  declarations) : le code ST qui l'ecrit, ses locales (une par ligne, sans VAR ni
//  END_VAR), et en direct ce que le calcul donne - sa valeur au demarrage, ou
//  l'erreur qui arreterait le demarrage, et ses fautes (Compiler les dira aussi).
//
//    +-- Valeur calculee de Seuils ---------------------------------------------+
//    | Le code : il ecrit Seuils (MAP[STRING] OF REAL) une fois, au demarrage   |
//    | [ FOR i := 0 TO 2 DO Seuils[Noms[i]] := Base * (i + 1); END_FOR;       ] |
//    | Les locales, une par ligne : Nom : TYPE := valeur;   // documentation    |
//    | [ Noms : ARRAY[0..2] OF STRING := ['bas', 'moyen', 'haut'];            ] |
//    | Ce que donne le calcul (F5 : recalculer)                                  |
//    |   Valeur au demarrage : {'bas': 10, 'haut': 30, 'moyen': 20}             |
//    |                                             [Annuler] [Appliquer]        |
//    +---------------------------------------------------------------------------+
//
//  L'apercu se refait une demi-seconde apres la derniere frappe (F5 : tout de
//  suite). Appliquer rend le code et les locales (core::joinFields) ; Echap annule.
// =============================================================================
#pragma once

#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <functional>
#include <string>

namespace app {

class HmiCalcDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string id{"dialog.hmiCalc"};
        std::string title;          // « Valeur calculee de Seuils »
        std::string name;           // Seuils
        std::string type;           // MAP[STRING] OF REAL
        bool        constant{true};
        std::string calc;           // le code (un modele si la declaration n'en a pas)
        std::string locals;         // les locales, une par ligne
        // Ce que donnent ce code et ces locales : la valeur, l'erreur, les fautes (une par ligne).
        std::function<std::string(const std::string& calc, const std::string& locals)> preview;
    };
    explicit HmiCalcDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Pour les essais et les scripts : les champs, l'apercu.
    [[nodiscard]] std::string calcText() const;
    [[nodiscard]] std::string localsText() const;
    [[nodiscard]] const std::string& previewText() const noexcept { return live_; }
    void setTexts(const std::string& calc, const std::string& locals);
    void recompute();

protected:
    core::Status buildUi() override;

private:
    void finish(bool ok);

    Spec                   spec_;
    ui::MultiLineText*     code_{nullptr};
    ui::MultiLineText*     locals_{nullptr};
    ui::Widget*            body_{nullptr};
    std::string            live_;
    bool                   dirty_{false};
    double                 changedAt_{0.0};
    double                 now_{0.0};
    bool                   done_{false};
    core::ConnectionScope  links_;
};

} // namespace app
