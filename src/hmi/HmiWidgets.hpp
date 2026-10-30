// =============================================================================
//  hmi/HmiWidgets.hpp - le contenu des objets du lot 6
// -----------------------------------------------------------------------------
//  CE QUE GARDENT, EN TEXTE, LES OBJETS A CONTENU. Une propriete de plus par
//  objet, lisible dans ihm/vues/*.vue et modifiable a la main :
//
//    Image animee    states  une ligne par etat :
//                              Alarme => alarme_1.png, alarme_2.png @ 250
//                              Marche => pompe_on.png
//                            (la condition, les images qui defilent, la periode
//                            en ms ; sans periode : celle de l'objet). Le premier
//                            etat vrai l'emporte ; aucun : l'image par defaut.
//    Tableau         cells   une ligne par ligne du tableau, les cases separees
//                            par des tabulations. Une case : du texte, un texte a
//                            trous ({Niveau:0.0} bar) ou =expression.
//                    widths  la largeur relative des colonnes (a;b;...), facultative.
//    Courbe          names   le nom de chaque plume (a;b;...), pour la legende.
//
//  LE GESTIONNAIRE DE RECETTES se dessine et se clique avec la meme geometrie
//  (recipeManagerLayout) : la barre de boutons en haut, puis le tableau des jeux.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ---------------------------------------------------------------- image animee
struct ImageState {
    std::string              condition;
    std::vector<std::string> images;
    int                      periodMs{0};     // 0 : la periode de l'objet
    bool operator==(const ImageState&) const = default;
};
[[nodiscard]] std::vector<ImageState> parseImageStates(std::string_view text);
[[nodiscard]] std::string             formatImageStates(const std::vector<ImageState>&);
// L'image a montrer : l'etat retenu (-1 : aucun, l'image par defaut) et, a
// l'instant `seconds`, l'image de son defilement.
[[nodiscard]] std::string imageFrame(const ImageState& s, int objectPeriodMs, double seconds);

// ------------------------------------------------------------------- tableau
[[nodiscard]] std::vector<std::vector<std::string>> parseCells(std::string_view text);
[[nodiscard]] std::string                           formatCells(const std::vector<std::vector<std::string>>&);
// Une case pilotee : "=expression" ou un texte a trous.
[[nodiscard]] bool cellIsExpression(std::string_view cell) noexcept;
[[nodiscard]] bool cellIsTemplate(std::string_view cell) noexcept;
// Les largeurs relatives des `n` colonnes (widths, ou egales), en fraction.
[[nodiscard]] std::vector<double> columnFractions(const Object&, std::size_t n);

// ------------------------------------------------------------ liste a ; b ----
[[nodiscard]] std::vector<std::string> splitSemicolons(std::string_view s);   // "a; b" -> {a, b} (vides gardes)
[[nodiscard]] std::string              joinSemicolons(const std::vector<std::string>&);

// ------------------------------------------------------ gestionnaire de recettes
struct RecipeManagerLayout {
    struct Button { std::string label; Box box; };
    std::vector<Button> buttons;          // dans le repere de l'objet
    Box                 title;            // le nom de la recette, a droite de la barre
    Box                 table;            // en-tete + lignes
    double              headerH{24}, rowH{22};
    std::size_t         visibleRows{0};
};
// Les boutons de l'objet (Ajouter;Modifier;Supprimer;Appliquer;Lire), a leur place.
[[nodiscard]] RecipeManagerLayout recipeManagerLayout(const Object&, double w, double h);
inline constexpr std::string_view kRecipeButtons[] = {"Ajouter", "Modifier", "Supprimer", "Appliquer", "Lire"};
// Ce qu'un clic en (x, y) (repere de l'objet) touche : "bouton:Ajouter",
// "ligne:2" (0 = le premier jeu), ou "".
[[nodiscard]] std::string recipeManagerHit(const Object&, double w, double h, double x, double y, std::size_t records);

// 1.12.5 (lot A) : LE TEXTE D'UNE BARRE DE PROGRESSION - le pourcentage de sa plage (500 sur
// 0..2000 : "25 %") ; `percentSign` faux : le nombre seul (la barre verticale, etroite).
[[nodiscard]] std::string progressLabel(double value, double min, double max, bool percentSign);

// ------------------------------------------------- 1.14.1 : LES FINS DE COURSE
//  Une vanne, une vanne 3 voies, un registre se commandent aussi PAR LEURS FINS DE COURSE (plusieurs
//  booleens de l'automate, comme sur le terrain) au lieu d'une seule valeur : "Commande de l'etat" =
//  fins de course (stateMode ; la vanne 3 voies : positionMode = fins de course). Leur etat se decide ICI,
//  partout le meme : le dessin, Pourquoi ?, les essais.
//   - deux voies (vanne, registre) : FC ouvert seule, ouvert ; FC ferme seule, ferme ; aucune, en mouvement
//     (vers l'Ordre s'il est donne) ; les deux, ou l'Ordre donne et sa fin de course pas atteinte apres le
//     Temps de manoeuvre : discordance. Une seule fin de course reliee : l'autre position s'en deduit.
//   - la vanne 3 voies : une fin de course par voie (1-2, 1-3, 2-3) et, facultative, fermee ; la seule vraie
//     donne la voie ; aucune, en mouvement ; plusieurs, discordance. L'Ordre de position (0 a 3) de meme.
//  LE CHRONO (TravelClock) suit, a l'heure de la marche, depuis quand la fin de course attendue manque ; il
//  pose limitOverdue (TRUE) sur la copie montree quand le Temps de manoeuvre est passe.
namespace limits {

inline constexpr std::string_view kModeKey = "stateMode";             // la vanne, les registres
inline constexpr std::string_view kValueMode = "une valeur";          // par defaut (et un objet d'avant)
inline constexpr std::string_view kLimitsMode = "fins de course";
inline constexpr std::string_view kOverdueKey = "limitOverdue";       // pose en marche (TravelClock), jamais enregistre
inline constexpr std::string_view kWayKeys[4] = {"limitClosed", "limit12", "limit13", "limit23"};   // la vanne 3 voies

// La vanne, la vanne 3 voies et les trois registres ont la variante.
[[nodiscard]] bool kindHasLimits(Kind) noexcept;
// L'objet est-il commande par ses fins de course ? (la vanne 3 voies : positionMode ; les autres : stateMode)
[[nodiscard]] bool limitMode(const Object&) noexcept;
[[nodiscard]] bool limitModeText(Kind, std::string_view modeText) noexcept;

enum class State : std::uint8_t { None, Open, Closed, Moving, Discord };
// "ouverte", "fermee", "en mouvement", "discordance", "inconnu" (`feminine` : une vanne).
[[nodiscard]] std::string stateLabel(State, bool feminine);

// Un booleen de l'objet : relie (une formule ou une valeur) et sa valeur.
struct Input {
    bool given{false};
    bool on{false};
};

struct Reading {
    State       state{State::None};
    int         toward{0};        // en mouvement : +1 vers l'ouverture, -1 vers la fermeture, 0 on ne sait pas
    bool        both{false};      // discordance : les deux fins de course a la fois
    bool        overdue{false};   // discordance : l'Ordre donne, sa fin de course pas atteinte a temps
    bool        between{false};   // reglante ou modulant : entre les deux, a sa place (pas en mouvement)
    int         order{-1};        // l'Ordre : 1 ouvrir, 0 fermer, -1 aucun
    bool        reached{false};   // l'Ordre donne : sa fin de course est atteinte
    std::string why;              // ce qui decide, en francais (Pourquoi ?)
};
// Deux voies. `overdue` : le chrono ; `regulating` : une vanne reglante, un registre modulant (entre deux
// fins de course, il est a sa place sans Ordre).
[[nodiscard]] Reading decide(Input open, Input closed, Input order, bool overdue, bool regulating = false);

struct Reading3 {
    State            state{State::None};
    int              way{-1};       // 0 fermee, 1 (1-2), 2 (1-3), 3 (2-3) ; en mouvement : la voie visee (-1 : aucune)
    std::vector<int> on;            // les voies dont la fin de course est vraie (0 : fermee)
    bool             overdue{false};
    int              order{-1};     // l'Ordre de position (0 a 3), -1 aucun
    bool             reached{false};
    std::string      why;
};
// `ports` : [0] fermee, [1] 1-2, [2] 1-3, [3] 2-3 (kWayKeys) ; boisseau en L : 1-2 ignoree.
[[nodiscard]] Reading3 decide3(const Input (&ports)[4], bool lBore, int order, bool overdue);

// La voie d'un texte : 0 a 3, 12, 13, 23 (21, 31, 32), "1-3"... ; -1 : illisible.
[[nodiscard]] int wayOf(std::string_view text);
// Le nom d'une voie : "fermee", "1-2", "1-3", "2-3".
[[nodiscard]] std::string_view wayLabel(int way) noexcept;

// L'objet TEL QU'ON LE VOIT (en marche : ses formules calculees) - la propriete `key` en booleen.
[[nodiscard]] Input inputOf(const Object& shown, std::string_view key);
[[nodiscard]] Reading  read(const Object& shown);     // la vanne, les registres
[[nodiscard]] Reading3 read3(const Object& shown);    // la vanne 3 voies
// Le temps de manoeuvre en secondes (vide, illisible ou nul : 0, pas de chrono) ; travelTimeOk : vide ou lisible.
[[nodiscard]] double travelSeconds(std::string_view text);
[[nodiscard]] bool   travelTimeOk(std::string_view text);

class TravelClock {
public:
    // Chaque objet commande par ses fins de course, avec un Ordre et un Temps de manoeuvre : depuis quand sa
    // fin de course attendue manque (l'Ordre change, ou elle se perd) ; au-dela du temps, limitOverdue = TRUE
    // sur la copie montree. `now` : l'heure de la marche (secondes).
    void observe(View& shown, double now);
    [[nodiscard]] bool overdue(Id object) const noexcept;
    // Pose limitOverdue sur une autre copie de la meme vue (Pourquoi ?), sans faire avancer le chrono.
    void mark(View& shown) const;
    void reset() noexcept { tracks_.clear(); }
    // Le coeur : un pas du chrono d'un objet. `order` : -1 aucun ; `travel` : secondes (0 : jamais en retard).
    struct Track {
        int    order{-2};
        bool   reached{false};
        double since{0};
        bool   overdue{false};
    };
    static bool step(Track& t, int order, bool reached, double now, double travel) noexcept;
private:
    std::map<Id, Track> tracks_;
};

} // namespace limits

} // namespace hmi
