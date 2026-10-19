// =============================================================================
//  app/hmi/HmiFunctionPicker.hpp - 1.12.16 : " CHOISIR UNE FONCTION... "
// -----------------------------------------------------------------------------
//  Le catalogue des fonctions, a portee de clavier, partout ou l'aide a la saisie
//  s'ouvre : la derniere ligne de chaque liste (ou Ctrl+I) ouvre ce selecteur.
//  A gauche les dossiers, au milieu les fonctions, a droite la fiche (signature,
//  ce qu'elle fait, ses arguments, ce qu'elle rend, un exemple).
//
//  1.12.17 : LES DOSSIERS. En haut les natives, rangees comme dans l'arbre (Nombres,
//  Textes... Conteneurs > Listes > Ajouter et retirer) ; en bas, en violet, MES
//  FONCTIONS : celles qu'on peut appeler d'ici, rangees comme le projet
//  (Programmation generale > Fonctions IHM ; Vues > Vue_Cuves > Fonctions ; Popups ;
//  Symboles). Le dossier d'ou l'on vient est marque " ici " et s'ouvre deplie ; une
//  privee ne se montre que chez elle. Une fonction de symbole se deplie sur les
//  instances : en choisir une insere Vue_Cuves.Vanne_1.Ouvrir().
//  LA LISTE " REND " remplace la puce " Rend un REAL " : Tout, Ce qui convient ici,
//  Sans retour, BOOL, Entiers... chaque choix dit combien il en reste (la recherche
//  les recompte). " Dans une expression " ecarte ce qui ne s'ecrit qu'en instruction ;
//  dans une case fx, elle est cochee d'office et Sans retour est grise.
//  Inserer rend l'appel, ses arguments a remplir reperes (le premier choisi par
//  l'editeur) ; Inserer l'exemple rend l'exemple de la fiche ; une de mes fonctions :
//  Ouvrir sa definition (Ctrl+Entree).
// =============================================================================
#pragma once

#include "../../menu/IMenu.hpp"

#include <cstdint>
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
        std::string              name;        // "LIST_AVG", "Pourcent"
        std::string              family;      // son dossier : "Calculer", "Vue_Cuves", "Fonctions IHM"
        std::string              group;       // "Natives" ; "Mes fonctions" : celles du projet (en bas, en violet)
        std::vector<std::string> path;        // 1.12.17 : ses dossiers sous le groupe ("Conteneurs", "Listes", "Calculer")
        std::string              signature;   // "LIST_AVG(L : LIST...) : REAL"
        std::string              returns;     // "REAL", "comme IN" ; "" : sans retour
        std::string              summary, example;
        std::vector<Param>       params;
        bool                     expression{true};   // s'ecrit dans une expression (une case fx)
        // ---- 1.12.17 : une de mes fonctions ----
        std::string              prefix;      // devant son nom, d'ici : "Vue_Cuves.", "Vanne_1." ; vide : son nom seul
        std::string              where;       // "Vues > Vue_Cuves > sous-onglet Fonctions"
        std::string              access;      // "publique", "privee, visible ici", "virtuelle"
        std::string              code;        // ses premieres lignes
        std::vector<std::string> instances;   // une fonction de symbole appelee d'ailleurs : "Vue_Cuves.Vanne_1"...
        bool                     byInstance{false};   // d'ici, elle s'appelle par une instance
        bool                     here{false};         // rangee la ou l'on ecrit
        std::uint64_t            view{0}, function{0}; // Ouvrir sa definition (view 0 : une fonction du projet)
    };
    // 1.12.17 : la liste " Rend ".
    enum class Returns : std::uint8_t { All, Fits, Void, Bool, Integer, Real, Text, Time, Word, Container, Reference, Generic, Other };
    struct Spec {
        std::string        expected;            // le type attendu au curseur ("REAL") ; vide : inconnu
        bool               expressionOnly{false};   // une case fx : " Dans une expression ", sans la decocher
        std::vector<Entry> extra;                // mes fonctions (group "Mes fonctions", path : leurs dossiers)
        // Le type rendu convient-il a `expected` ? Sans juge : seul le meme nom de type convient.
        std::function<bool(const std::string& returns)> fits{};
        std::string        from;                 // 1.12.17 : d'ou l'on ecrit ("Vue_Four") ; vide : Programmation generale
    };
    struct Answer {
        std::string name;                        // la fonction choisie
        std::string text;                        // ce qu'on insere : "LIST_AVG(L)" ou l'exemple
        std::vector<std::pair<int, int>> holes;  // les arguments a remplir : (debut, longueur) dans text
        bool          open{false};               // 1.12.17 : Ouvrir sa definition (rien a inserer)
        std::uint64_t view{0}, function{0};
    };

    explicit HmiFunctionPicker(Spec spec);
    ~HmiFunctionPicker() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // Les fonctions du catalogue des natives (sans les X_TO_Y), rangees comme l'arbre.
    [[nodiscard]] static std::vector<Entry> natives();
    // L'appel d'une fonction, ses arguments obligatoires a remplir : "LIMIT(MN, IN, MX)" ;
    // `instance` (une fonction de symbole) : par cette instance, "Vue_Cuves.Vanne_1.Ouvrir()".
    [[nodiscard]] static Answer callOf(const Entry& e, int instance = -1);
    [[nodiscard]] static Answer parse(const std::string& payload);
    // 1.12.17 : la famille de ce qu'elle rend, et son nom dans la liste " Rend ".
    [[nodiscard]] static Returns classify(std::string_view returns);
    [[nodiscard]] static std::string returnsLabel(Returns r, std::string_view expected = {});

    // Pour les sessions et les essais.
    void setSearch(const std::string& text);
    bool setFolder(std::string_view path);            // "Mes fonctions > Vues > Vue_Cuves > Fonctions" (le separateur : \xE2\x80\xBA) ; "" : toutes
    void setTyped(bool on);                           // Ce qui convient ici (sinon : Tout)
    bool setReturns(Returns r);                       // faux : grise (Sans retour dans une case fx)
    [[nodiscard]] Returns returnsFilter() const;
    [[nodiscard]] std::vector<std::pair<Returns, std::size_t>> returnCounts() const;   // les choix, combien chacun
    void setExpressionOnly(bool on);                  // " Dans une expression " (sans effet dans une case fx)
    bool select(std::string_view name, int instance = -1);
    bool expand(std::string_view path, bool open);
    void showReturnsMenu(bool open);                  // la liste Rend, ouverte (les captures)
    [[nodiscard]] std::vector<std::string> shownNames() const;
    [[nodiscard]] std::vector<std::string> folders() const;   // les dossiers montres, leurs chemins, dans l'ordre
    [[nodiscard]] std::string hereFolder() const;             // le dossier marque " ici " ; vide : aucun
    [[nodiscard]] std::string selectedName() const;
    [[nodiscard]] std::string selectedCall() const;           // ce qu'Inserer poserait
    void insert(bool example);                        // Inserer (l'exemple : true)
    void openDefinition();                            // une de mes fonctions : sa definition
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
//  annule, ni pour Ouvrir sa definition : l'hote l'ouvre). Les essais posent leur propre hote.
namespace functionpicker {
using Done = std::function<void(const HmiFunctionPicker::Answer&)>;
using Host = std::function<void(HmiFunctionPicker::Spec, Done)>;
inline const std::string kChoose = "Choisir une fonction\xE2\x80\xA6";
void setHost(Host host);
[[nodiscard]] bool available();
void ask(HmiFunctionPicker::Spec spec, Done done);   // rien sans hote
} // namespace functionpicker

} // namespace app
