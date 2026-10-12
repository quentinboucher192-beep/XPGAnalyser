// =============================================================================
//  app/hmi/HmiCalcDialog.hpp - 1.12.7 : LA FENETRE D'UNE VALEUR CALCULEE AU DEMARRAGE
// -----------------------------------------------------------------------------
//  « ƒx Calcul... » d'une constante ou d'une variable d'un script (la grille des
//  declarations), comme la maquette 1.12.7 : deux onglets, le Code (l'editeur ST,
//  Ctrl+Espace : l'aide a la saisie) et les Locales (une grille : nom, type, valeur
//  initiale, documentation - jamais de VAR ... END_VAR dans le code) ; a droite, ce
//  que le calcul donne, en direct : l'Apercu (sa valeur au demarrage, ou l'erreur qui
//  arreterait le demarrage), ce qu'il Lit, qui le lit (Lu par) et ses Problemes (un
//  clic : la ligne dans le code).
//
//    +-- Valeur calculee de Seuils --------------------------------------------------+
//    | Le code ecrit Seuils sous son nom ; il tourne une fois au demarrage...        |
//    | [</> Code] [= Locales 1]                        | Apercu                      |
//    | [ FOR i := 0 TO 2 DO                          ] |  {'bas': 10, 'haut': 30...} |
//    | [     Seuils[Noms[i]] := Base * (i + 1);      ] | Lit : Base                  |
//    | [ END_FOR;                                    ] | Lu par : calcul de Total    |
//    | [▶ Calculer (F5)] [Revenir au calcul d'origine] | Problemes : aucun           |
//    |                                                       [Annuler] [Appliquer]   |
//    +--------------------------------------------------------------------------------+
//
//  L'apercu se refait une demi-seconde apres la derniere frappe (F5 : tout de suite).
//  « Deplacer dans Locales » (un bloc VAR colle dans le code) range ses declarations
//  dans la grille. Appliquer rend le code et les locales (core::joinFields) ; Echap
//  annule.
// =============================================================================
#pragma once

#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

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
        std::string locals;         // les locales, une par ligne (Nom : TYPE := valeur;   // doc)
        int         line{0};        // > 0 : le code s'ouvre a cette ligne (une faute de la Console, de Compiler)
        // Ce que donnent ce code et ces locales, un fait par ligne : « Valeur au demarrage : ... »,
        // « Erreur : ... », « Faute ligne N : ... », « Attention ligne N : ... », « Lit : a, b »,
        // « Lu par : x, y » ; une autre ligne : une note (« Pas de code : ... »).
        std::function<std::string(const std::string& calc, const std::string& locals)> preview;
        // L'aide a la saisie du code (Ctrl+Espace) ; `locals` rend les locales du moment.
        std::function<void(ui::MultiLineText& code, std::function<std::string()> locals)> assist;
    };
    // Une locale de la grille.
    struct Local {
        std::string name, type, value, doc;
    };
    // Ce que l'apercu dit, range par boite.
    struct Facts {
        std::string              value, error, note;
        std::vector<std::string> reads, readBy;
        struct Problem {
            bool        error{false};
            int         line{0};       // 0 : pas de ligne
            std::string text;
        };
        std::vector<Problem> problems;
    };
    class LocalsModel;

    explicit HmiCalcDialog(Spec spec);
    ~HmiCalcDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Pour les essais et les scripts : les champs, l'apercu, les onglets, les gestes.
    [[nodiscard]] std::string calcText() const;
    [[nodiscard]] std::string localsText() const;
    [[nodiscard]] const std::string& previewText() const noexcept { return live_; }
    [[nodiscard]] const Facts& facts() const noexcept { return facts_; }
    void setTexts(const std::string& calc, const std::string& locals);
    void recompute();
    void showLocals(bool on);                       // l'onglet Locales (vrai) ou le Code
    [[nodiscard]] bool localsShown() const noexcept { return localsShown_; }
    [[nodiscard]] const std::vector<Local>& locals() const;
    [[nodiscard]] const ui::TableView* localsTable() const noexcept { return table_; }
    bool addLocal();
    bool removeLocal(std::size_t row);
    bool moveLocal(std::size_t row, int delta);
    bool setLocal(std::size_t row, std::size_t column, const std::string& text);   // 0 nom, 1 type, 2 valeur, 3 doc
    bool moveVarBlocks(std::string* why = nullptr);  // « Deplacer dans Locales »
    void revert();                                   // « Revenir au calcul d'origine »
    bool goToProblem(std::size_t index);             // la ligne d'un probleme, dans le code

    // Les locales d'un texte (une par ligne), et le texte de locales.
    [[nodiscard]] static std::vector<Local> parseLocals(const std::string& text);
    [[nodiscard]] static std::string composeLocals(const std::vector<Local>& rows);
    // Les faits d'un texte d'apercu.
    [[nodiscard]] static Facts factsOf(const std::string& preview);

protected:
    core::Status buildUi() override;

private:
    void finish(bool ok);
    void changed();
    void refreshButtons();
    [[nodiscard]] std::size_t selectedLocal() const;

    Spec                         spec_;
    std::shared_ptr<LocalsModel> model_;
    ui::MultiLineText*           code_{nullptr};
    ui::TableView*               table_{nullptr};
    ui::Widget*                  body_{nullptr};
    ui::Button*                  tabCode_{nullptr};
    ui::Button*                  tabLocals_{nullptr};
    ui::Button*                  moveButton_{nullptr};
    std::string                  live_;
    Facts                        facts_;
    bool                         localsShown_{false};
    bool                         dirty_{false};
    double                       changedAt_{0.0};
    double                       now_{0.0};
    bool                         done_{false};
    core::ConnectionScope        links_;
};

} // namespace app
