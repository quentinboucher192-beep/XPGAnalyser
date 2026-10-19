// =============================================================================
//  app/hmi/HmiFunctionPicker.hpp - 1.12.16 : « CHOISIR UNE FONCTION… »
// -----------------------------------------------------------------------------
//  Le catalogue des fonctions, a portee de clavier, partout ou l'aide a la saisie
//  s.ouvre : la derniere ligne de chaque liste (ou Ctrl+I) ouvre ce
//  selecteur. A gauche les familles (celles de la vue, du symbole et du projet en
//  tete, puis les natives par groupe), au milieu les fonctions, a droite la fiche
//  (signature, ce qu'elle fait, ses arguments, ce qu'elle rend, un exemple).
//  « Rend un REAL » ne garde que ce qui convient au type attendu au curseur ;
//  « Dans une expression » ecarte ce qui ne s'ecrit qu'en instruction (une case
//  fx). Inserer rend l'appel, ses arguments a remplir reperes (le premier choisi
//  par l'editeur) ; Inserer l'exemple rend l'exemple de la fiche.
// =============================================================================
#pragma once

#include "../../menu/IMenu.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app {

class HmiFunctionPicker final : public menu::WidgetMenu {
public:
    struct Param {
        std::string name, type, role;
        bool        optional{false};
    };
    struct Entry {
        std::string        name;           // "LIST_AVG", "Pourcent"
        std::string        family;         // "Listes, vecteurs et tuples", "Fonctions de Vue_Four"
        std::string        group;          // "Le dialecte de l'IHM", "Ici"
        std::string        signature;      // "LIST_AVG(L : LIST...) : REAL"
        std::string        returns;        // "REAL", "comme IN"
        std::string        summary, example;
        std::vector<Param> params;
        bool               expression{true};   // s'ecrit dans une expression (une case fx)
    };
    struct Spec {
        std::string        expected;            // le type attendu au curseur ("REAL") ; vide : inconnu
        bool               expressionOnly{false};
        std::vector<Entry> extra;                // les fonctions de la vue, du symbole, du projet (en tete)
        // Le type rendu convient-il a `expected` ? Sans juge : seul le meme nom de type convient.
        std::function<bool(const std::string& returns)> fits{};
    };
    struct Answer {
        std::string name;                        // la fonction choisie
        std::string text;                        // ce qu'on insere : "LIST_AVG(L)" ou l'exemple
        std::vector<std::pair<int, int>> holes;  // les arguments a remplir : (debut, longueur) dans text
    };

    explicit HmiFunctionPicker(Spec spec);
    ~HmiFunctionPicker() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Les fonctions du catalogue des natives (sans les X_TO_Y), dans l'ordre de leurs familles.
    [[nodiscard]] static std::vector<Entry> natives();
    // L'appel d'une fonction, ses arguments obligatoires a remplir : "LIMIT(MN, IN, MX)".
    [[nodiscard]] static Answer callOf(const Entry& e);
    [[nodiscard]] static Answer parse(const std::string& payload);

    // Pour les sessions et les essais.
    void setSearch(const std::string& text);
    bool setFamily(std::string_view family);          // "" : toutes
    void setTyped(bool on);                           // « Rend un REAL »
    void setExpressionOnly(bool on);                  // « Dans une expression »
    bool select(std::string_view name);
    [[nodiscard]] std::vector<std::string> shownNames() const;
    [[nodiscard]] std::vector<std::string> families() const;   // les familles montrees, dans l'ordre
    [[nodiscard]] std::string selectedName() const;
    void insert(bool example);                        // Inserer (l'exemple : true)
    void finish(bool ok);

protected:
    core::Status buildUi() override;
    void         onEnter() override;

private:
    class Body;
    Spec   spec_;
    Body*  body_{nullptr};
    bool   done_{false};
    Answer answer_;
};

// ---- l'hote : qui ouvre le selecteur ----------------------------------------------------
//  Un editeur, une case ne connaissent pas la fenetre : ils DEMANDENT une fonction ; l'hote
//  (l'ecran principal) ouvre le selecteur et rend la reponse a `done` (jamais appele si l'on
//  annule). Les essais posent leur propre hote.
namespace functionpicker {
using Done = std::function<void(const HmiFunctionPicker::Answer&)>;
using Host = std::function<void(HmiFunctionPicker::Spec, Done)>;
inline const std::string kChoose = "Choisir une fonction\xE2\x80\xA6";
void setHost(Host host);
[[nodiscard]] bool available();
void ask(HmiFunctionPicker::Spec spec, Done done);   // rien sans hote
} // namespace functionpicker

} // namespace app
