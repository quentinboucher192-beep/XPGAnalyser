// =============================================================================
//  app/hmi/HmiPartage.hpp - 1.12.27 : COPIER DANS UNE FENETRE, COLLER DANS L'AUTRE
// -----------------------------------------------------------------------------
//  Le presse-papiers de l'editeur vit dans la fenetre (HmiCanvas). Celui du
//  systeme ne porte qu'un texte : la liste des objets copies (pour Excel). Pour
//  qu'une AUTRE fenetre d'IHM Studio colle les objets eux-memes, la copie les
//  publie aussi dans le dossier des fenetres (app/Fenetres.hpp) : un paquet de
//  vues (hmi/HmiPackage.hpp) - une vue de passage avec les objets copies, et ce
//  dont ils ont besoin : les symboles de leurs instances, les popups que leurs
//  actions ouvrent, leurs images, leurs styles, les variables IHM qu'ils lisent
//  ou ecrivent, leurs types et fonctions IHM.
//
//  Le paquet porte l'empreinte du texte mis dans le presse-papiers du systeme :
//  il ne sert que tant que ce texte y est. Une copie dans Excel, et Ctrl+V colle
//  le texte, comme avant (des noms de variables).
//
//  Coller importe ce qui MANQUE au projet (ajoute) et garde ce qu'il a deja
//  (meme nom : le sien) - comme une vue creee depuis un modele. Tout se fait en
//  une modification : Ctrl+Z annule le collage entier.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiPackage.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::partage {

// L'empreinte d'un texte du presse-papiers ("3f2a..."), sans les \r ni les fins
// de ligne en trop : le systeme peut les changer d'une fenetre a l'autre.
[[nodiscard]] std::string empreinte(std::string_view texteSysteme);

// Le fichier du paquet (dans le dossier des fenetres).
[[nodiscard]] std::string chemin();

// Publier la copie : les objets `objects` (avec leurs enfants) de la vue `from`
// du projet `p` ; `texteSysteme` : ce que la copie a mis dans le presse-papiers
// du systeme. Faux : rien d'ecrit (le dossier refuse) - la copie reste locale.
bool publier(const hmi::Project& p, hmi::Id from, const std::vector<hmi::Object>& objects, const std::string& texteSysteme);

// La copie publiee dont le texte est `texteSysteme` (le presse-papiers du systeme,
// maintenant). nullopt : aucune, ou une autre copie depuis.
struct Copie {
    hmi::pkg::Package paquet;
    std::string       projet;     // le projet d'origine
    std::string       vue;        // la vue d'origine
    long              pid{0};     // la fenetre d'origine
    std::string       cle;        // l'empreinte
};
[[nodiscard]] std::optional<Copie> lire(const std::string& texteSysteme);

// Les objets de la copie (ceux du haut : pas le contenu d'un groupe) et leur
// etendue (x, y, largeur, hauteur) dans leur vue d'origine.
[[nodiscard]] std::size_t objetsDe(const Copie&);
struct Etendue {
    double x{0}, y{0}, w{0}, h{0};
};
[[nodiscard]] std::optional<Etendue> etendue(const Copie&);

// Coller la copie dans la vue `into` du projet (a faire dans UNE modification :
// changeProject) : ce qui manque est ajoute, ce qui existe est garde ; les objets
// se posent decales de (dx, dy), dans le groupe `insideGroup` s'il y en a un.
// Un nom d'objet libre dans la vue est garde (les scripts le citent), sinon il
// recoit le suivant libre.
struct Resultat {
    std::vector<hmi::Id>     poses;      // les objets du haut, poses
    std::size_t              objets{0};  // tous les objets poses (le contenu des groupes compris)
    std::vector<std::string> ajoutes;    // "symbole Pompe", "style Alarme", "variable Niveau"...
    std::vector<std::string> gardes;     // ce que le projet avait deja : le sien est garde
};
Resultat coller(hmi::Project& p, hmi::Id into, const Copie&, double dx, double dy, hmi::Id insideGroup = hmi::kNoId);

// Le message de la barre d'etat apres un collage depuis une copie publiee.
[[nodiscard]] std::string message(const Copie&, const Resultat&);

} // namespace app::partage
