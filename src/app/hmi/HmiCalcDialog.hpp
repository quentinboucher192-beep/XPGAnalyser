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
//
//  1.12.16 (T4) : « VALEUR INITIALE DE ... » - avec Spec::project, la meme fenetre a
//  deux modes, comme la maquette 1.12.16 :
//    Brut     un editeur fait pour le type (app::initvalue) : une ligne par entree
//             d'une MAP, par element d'une liste, par case d'un tableau, par membre
//             d'une structure ; les valeurs d'une enumeration a choisir ; une duree en
//             heures, minutes, secondes, millisecondes ; la case vide montre la
//             valeur du type, en gris. Dessous, le litteral enregistre et ses
//             problemes (une faute grise Appliquer). « Passer en code » le reprend.
//    Code fx  une fonction a l'en-tete fixe, FUNCTION Init_<Nom> : <type> - le code
//             RENVOIE la valeur (RETURN) ; « Partir d'un exemple » (faits du type) ;
//             ses Locales ; l'apercu en direct ; « calculee au demarrage » ou « a
//             chaque ouverture » pour une constante de vue ou de popup.
//  A droite, LE TYPE, EXPLIQUE : son arbre, ce qu'il est, le lire, en C++, les natives
//  qui vont avec. Appliquer rend (core::joinFields) : le code, les locales, "code" ou
//  "brut", la valeur ecrite, le moment du calcul (outcomeOf).
// =============================================================================
#pragma once

#include "HmiInitModel.hpp"
#include "HmiUnfold.hpp"
#include "../../menu/IMenu.hpp"
#include "../../sim/Rich.hpp"
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
        // Le deballage d'une locale (sous la grille) : sa valeur initiale, lue dans son type (vide : celle
        // de son type) ; nulle : `why` dit pourquoi. Sans lui : pas de deballage.
        std::function<sim::ObjRef(const std::string& type, const std::string& value, std::string* why)> valueOf;
        // ---- 1.12.16 (T4) : la valeur initiale, en brut ou en code (voir l'en-tete) ----
        const hmi::Project*          project{nullptr};   // non nul : la fenetre a deux modes
        std::string                  value;              // la valeur ecrite (le Brut)
        bool                         code{false};        // s'ouvre en Code (la declaration est calculee)
        std::string                  codeRefused;        // non vide : pas de Code ici, et pourquoi
        std::string                  owner;              // Reglages, Vue_Four : « Le lire » d'ailleurs
        std::vector<initvalue::Name> readable;           // ce que le code peut lire (les exemples s'en servent)
        int                          reopen{-1};         // -1 : pas de choix ; 0 : au demarrage ; 1 : a chaque ouverture
        bool                         reopenForced{false};   // une popup qui lit ses parametres : a chaque ouverture
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
    class RawModel;
    // 1.12.16 (T4) : ce que rend Appliquer dans la fenetre a deux modes.
    struct Outcome {
        bool        code{false};
        std::string calc, locals, value;
        int         reopen{-1};
    };
    [[nodiscard]] static Outcome outcomeOf(const std::string& payload);

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
    [[nodiscard]] const ui::TableView* localsUnfold() const noexcept { return unfoldShown_ ? unfold_ : nullptr; }   // nul : cache
    void selectLocal(std::size_t row);
    bool addLocal();
    bool removeLocal(std::size_t row);
    bool moveLocal(std::size_t row, int delta);
    bool setLocal(std::size_t row, std::size_t column, const std::string& text);   // 0 nom, 1 type, 2 valeur, 3 doc
    bool moveVarBlocks(std::string* why = nullptr);  // « Deplacer dans Locales »
    void revert();                                   // « Revenir au calcul d'origine »
    bool goToProblem(std::size_t index);             // la ligne d'un probleme, dans le code

    // ---- 1.12.16 (T4) : la valeur initiale, en brut ou en code ----
    [[nodiscard]] bool unified() const noexcept { return spec_.project != nullptr; }
    [[nodiscard]] bool codeShown() const noexcept { return codeShown_; }      // le mode Code (sinon le Brut)
    bool showCode(bool on);                                                   // faux : refuse (codeRefused)
    [[nodiscard]] const initvalue::Raw& raw() const noexcept { return raw_; }
    [[nodiscard]] std::string rawLiteral() const;                             // le litteral enregistre
    [[nodiscard]] const std::vector<initvalue::Problem>& rawProblems() const noexcept { return rawProblems_; }
    // Les gestes du Brut (comme la souris) : une case (Map : la valeur), une cle, + Ligne, Retirer...
    bool setRawCell(std::size_t row, std::size_t cell, const std::string& text);
    bool setRawKey(std::size_t row, const std::string& text);
    bool rawAdd();
    bool rawRemove(std::size_t row);
    bool rawMove(std::size_t row, int delta);
    bool rawSortByKey();
    bool rawFillZero();
    bool rawSequence(double start, double step);
    bool rawChoose(int option);
    bool rawLeaveFreeText();
    bool toCode();                                                            // « Passer en code »
    [[nodiscard]] const std::vector<initvalue::Template>& templates() const noexcept { return templates_; }
    bool useTemplate(std::size_t index);                                      // « Partir d'un exemple »
    [[nodiscard]] const initvalue::Explanation& explanation() const noexcept { return explanation_; }
    [[nodiscard]] int reopen() const noexcept { return reopen_; }
    bool setReopen(bool atOpen);                                              // faux : pas de choix, ou d'office
    [[nodiscard]] const ui::TableView* rawTable() const noexcept { return rawTable_; }
    [[nodiscard]] bool canApply() const;                                      // le Brut sans faute (le Code : toujours)
    void apply() { finish(true); }

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
    void refreshUnfold();
    [[nodiscard]] std::size_t selectedLocal() const;
    core::Status buildUnified();
    void setLocalsRows(const std::string& text);              // les locales d'un texte, la grille avertie
    [[nodiscard]] std::shared_ptr<ui::ITableModel> localsModel() const;
    void rawChanged();                     // 1.12.16 : le litteral, ses problemes, la table, les boutons
    void refreshUnified();                 // 1.12.16 : les modes, le moment, les gestes du Brut, Appliquer

    Spec                         spec_;
    std::shared_ptr<LocalsModel> model_;
    ui::MultiLineText*           code_{nullptr};
    ui::TableView*               table_{nullptr};
    ui::TableView*               unfold_{nullptr};
    std::shared_ptr<HmiUnfoldModel> unfoldModel_;
    bool                         unfoldShown_{false};
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
    // 1.12.16 (T4) : la valeur initiale, en brut ou en code.
    initvalue::Raw                    raw_;
    std::string                       literal_;
    std::vector<initvalue::Problem>   rawProblems_;
    std::vector<initvalue::Template>  templates_;
    initvalue::Explanation            explanation_;
    std::shared_ptr<RawModel>         rawModel_;
    ui::TableView*                    rawTable_{nullptr};
    std::vector<ui::Button*>          rawBar_;
    std::vector<ui::Button*>          templateButtons_;
    ui::Button*                       modeRaw_{nullptr};
    ui::Button*                       modeCode_{nullptr};
    ui::Button*                       reopenButton_{nullptr};
    ui::Button*                       toCodeButton_{nullptr};
    bool                              codeShown_{false};
    bool                              syncing_{false};
    int                               reopen_{-1};
    core::ConnectionScope        links_;
};

} // namespace app
