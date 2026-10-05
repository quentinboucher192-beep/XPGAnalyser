// =============================================================================
//  help/HelpSession.hpp - ce que l'aide retient, et ce qu'elle rend au projet
// -----------------------------------------------------------------------------
//  Cinq choses qui n'ont l'air d'avoir aucun rapport, et qui en ont un seul :
//  toutes repondent a " ou suis-je, d'ou viens-je, et comment est-ce que je
//  ressors d'ici avec quelque chose ".
//
//    L'HISTORIQUE (Precedent / Suivant). Lire une aide, c'est sauter : d'un
//    bloc a un DDT, du DDT a un parametre, du parametre a un code de defaut.
//    Au troisieme saut on ne sait plus revenir, et on referme la fenetre. Un
//    navigateur resout ca depuis trente ans ; il n'y a aucune raison de le
//    resoudre autrement.
//
//    LES RECENTS ET LES FAVORIS. Un integrateur consulte quatre pages, encore
//    et encore, pendant trois semaines. Les retrouver dans un arbre de soixante
//    entrees a chaque fois est un impot. Les favoris SURVIVENT A LA FERMETURE -
//    un favori qu'il faut reposer chaque matin n'est pas un favori.
//
//    INSERER L'EXEMPLE (ndeg13). La fin d'une consultation d'aide, c'est
//    presque toujours " bon, je recopie ca dans ma section ". Le faire a la
//    main veut dire alt-tab, selection, collage, indentation. Le bouton le fait
//    en une commande annulable, et il ajoute la ligne de commentaire qui dit
//    d'ou vient le code - six mois plus tard, c'est cette ligne qui compte.
//
//    OUVRIR LE FICHIER SOURCE (ndeg17). L'aide vit dans le .ddt. Quand on veut
//    la corriger vraiment - ou juste voir ce que le bloc fait - il faut ouvrir
//    le fichier, et a la bonne ligne.
//
//    LA VISITE GUIDEE (ndeg20). Cinq etapes, une seule fois, la premiere fois.
//    C'est de la donnee, pas de l'interface : ce fichier dit quoi montrer et
//    dans quel ordre, l'ecran dit ou pointer.
//
//  TOUT EST PUR ET SANS ECRAN, sauf `openInEditor`, qui est la seule ligne de
//  ce module a dependre du systeme et qui est isolee pour cette raison.
// =============================================================================
#pragma once

#include "HelpIndex.hpp"

#include "../project/LibraryCatalog.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace help {

// ---------------------------------------------------------- une destination --
// `Target` sert de place : c'est deja ce que F1 rend, ce que la recherche
// designe et ce qu'un renvoi ouvre. Une seule notion de " ou je suis " pour
// l'historique, les recents et les favoris, sinon les trois divergent.

// " ST_EQ_Pump#Fbk " : une ligne, lisible, qui tient dans un fichier de
// reglages et qu'un humain peut corriger a la main.
[[nodiscard]] std::string formatTarget(const Target&);
[[nodiscard]] Target      parseTarget(std::string_view);

// Ce qui s'affiche dans la liste : " ST_EQ_Pump . Fbk ", " XPG-2101 ", " EBOOL ".
[[nodiscard]] std::string labelOf(const Target&);

[[nodiscard]] bool sameTarget(const Target&, const Target&) noexcept;

// ------------------------------------------------------------ la navigation --
class Navigation {
public:
    static constexpr std::size_t kMaxRecents   = 20;
    static constexpr std::size_t kMaxHistory   = 100;

    // Aller quelque part. Ecrase la pile " suivant ", comme un navigateur : on
    // a bifurque, ce qu'il y avait devant n'existe plus. Aller la ou on est
    // deja ne fait rien - sinon un rafraichissement d'ecran empilerait vingt
    // fois la meme page et Precedent ne remonterait plus nulle part.
    void go(const Target&);

    [[nodiscard]] bool canBack() const noexcept { return index_ > 0; }
    [[nodiscard]] bool canForward() const noexcept {
        return !history_.empty() && index_ + 1 < history_.size();
    }
    // Rendent la destination atteinte. Sur une pile vide, rendent une cible
    // None : l'appelant n'a pas a tester deux fois.
    Target back();
    Target forward();

    [[nodiscard]] const Target& current() const noexcept;
    [[nodiscard]] const std::vector<Target>& history() const noexcept { return history_; }
    [[nodiscard]] std::size_t position() const noexcept { return index_; }
    void clear();

    // --- les recents : par `go`, sans doublon, le plus recent en tete --------
    [[nodiscard]] const std::vector<Target>& recents() const noexcept { return recents_; }

    // --- les favoris --------------------------------------------------------
    [[nodiscard]] bool isFavourite(const Target&) const;
    bool toggleFavourite(const Target&);      // rend l'etat APRES bascule
    void removeFavourite(const Target&);
    [[nodiscard]] const std::vector<Target>& favourites() const noexcept {
        return favourites_;
    }

    // --- la persistance -----------------------------------------------------
    // Deux listes de chaines, ce que app::Settings sait deja ecrire. Le module
    // d'aide ne connait pas Settings : il rendrait le sens de la dependance
    // absurde pour gagner deux lignes.
    [[nodiscard]] std::vector<std::string> saveRecents() const;
    [[nodiscard]] std::vector<std::string> saveFavourites() const;
    void restore(const std::vector<std::string>& recents,
                 const std::vector<std::string>& favourites);

    // Les destinations qui ne designent plus rien - un bloc renomme, un favori
    // pose sur un fichier supprime - une fois la bibliotheque rechargee. On ne
    // les efface pas en silence : on les rend, et l'ecran les montre grises.
    [[nodiscard]] std::vector<Target> stale(
        const std::vector<project::CatalogEntry>& library) const;

private:
    std::vector<Target> history_;
    std::size_t         index_{0};
    std::vector<Target> recents_;
    std::vector<Target> favourites_;
    Target              none_{};
};

// --------------------------------------------- ndeg13 : reprendre l'exemple ----
// Ce qu'il y a a mettre dans le presse-papier : l'exemple, tel quel.
[[nodiscard]] std::string copyText(const project::CatalogEntry&);

// Le nouveau corps d'une section, avec l'exemple insere a `atLine` (le curseur)
// ou a la fin quand `atLine` depasse. L'exemple est precede d'une ligne de
// commentaire qui nomme le bloc et sa version, et il est indente comme la ligne
// devant laquelle il arrive - du code colle a la colonne 1 dans un IF imbrique
// se voit immediatement, et se recorrige a la main a chaque fois.
//
// Rend le corps inchange quand l'entree n'a pas d'exemple : l'appelant teste
// `hasExample` pour griser le bouton, et cette fonction ne fabrique rien.
[[nodiscard]] std::string bodyWithExample(std::string_view body,
                                          const project::CatalogEntry&,
                                          std::size_t atLine = static_cast<std::size_t>(-1));

[[nodiscard]] bool hasExample(const project::CatalogEntry&) noexcept;

// ------------------------------------------ ndeg17 : ouvrir le fichier source --
struct SourceLocation {
    std::string path;
    std::size_t line{1};     // 1 = la premiere ligne
    bool        found{false};
};

// Le fichier de l'entree, et la ligne du sujet quand il y en a un : la ligne de
// declaration du parametre, ou la ligne d'aide qui le documente. Sans sujet, la
// ligne 1.
[[nodiscard]] SourceLocation locate(const project::CatalogEntry&,
                                    std::string_view subject = {});

// La seule chose de ce fichier qui touche au systeme. Rend un message d'erreur
// vide en cas de succes. Ne garantit pas que l'editeur s'ouvre a la ligne - peu
// le permettent depuis une ligne de commande generique - mais ouvre toujours le
// bon fichier.
[[nodiscard]] std::string openInEditor(const SourceLocation&);

// ------------------------------------- la boite aux lettres de F1 ------------
//
//  UNE SEULE PLACE, ET C'EST VOLONTAIRE.
//
//  L'ecran qui appuie sur F1 et l'ecran d'aide ne se voient pas : le second est
//  fabrique par une fabrique, et `PushMenu("help")` ne transporte pas
//  d'argument. Il faut donc poser la destination quelque part entre les deux.
//
//  `take` VIDE la boite. Une destination consommee ne doit pas rouvrir la meme
//  page au prochain passage par l'aide - un sommaire qui s'ouvre toujours sur
//  la derniere chose cherchee est un sommaire qu'on cesse d'ouvrir.
void   setPendingTarget(const Target&);
[[nodiscard]] Target takePendingTarget();
[[nodiscard]] bool   hasPendingTarget() noexcept;

// Lot macros 1 : " Lancer " depuis la page d'une macro. L'aide se ferme, et
// l'ecran d'analyse, revenu au premier plan, ouvre le formulaire de la macro.
// Meme boite a une place, meme regle : `take` la vide.
void setPendingMacroLaunch(std::string name);
[[nodiscard]] std::string takePendingMacroLaunch();

// --------------------------------------------- ndeg20 : la visite guidee -------
struct TourStep {
    std::string_view anchor;   // ce que l'ecran doit mettre en evidence
    std::string_view title;
    std::string_view text;
};
[[nodiscard]] const std::vector<TourStep>& tour();

// La cle de reglage qui dit que la visite a ete faite. Ici parce que le nom de
// la cle doit etre le meme partout, et qu'une chaine recopiee a trois endroits
// finit par differer a un.
inline constexpr std::string_view kTourSeenKey = "help.tour.seen";

} // namespace help
