// =============================================================================
//  app/hmi/HmiMethodFollow.hpp - 1.50.1 : LA SIGNATURE D'UNE METHODE VIRTUELLE ET SES REDEFINITIONS
// -----------------------------------------------------------------------------
//  Changer la signature d'une methode VIRTUELLE d'un type parent (un parametre renomme, retype, ajoute, retire,
//  deplace - l'onglet Parametres ; son retour - ses proprietes) : les methodes qui la redefinissent dans les sous-types
//  (a toute profondeur) prennent la meme (hmi::followOverrides : leurs parametres renommes dans leur corps aussi,
//  SUPER.M(...) suit). Avant, UNE question qui les nomme (several::confirm : l'hote ouvre la fenetre ; sans hote, oui
//  tout de suite) ; puis UNE commande : un Ctrl+Z reprend la methode et toutes ses redefinitions.
// =============================================================================
#pragma once

#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app {

enum class Follow : std::uint8_t {
    None,      // rien a suivre (pas virtuelle, pas redefinie, la signature n'a pas change) : le geste se fait comme avant
    Done,      // fait (ou suspendu a la question : l'hote rendra la reponse)
    Refused,   // refuse : `why` (une redefinition ne peut pas suivre - un nom qu'elle emploie deja...)
};

// `trial` : une copie du projet ou le geste est deja fait sur la methode `method`. `label` : le nom de la commande ;
// `after` : appele une fois la commande posee (la grille se relit).
Follow followSignature(const hmi::DocumentPtr& doc, const std::function<void(core::CommandPtr)>& apply, hmi::Id method,
                       const hmi::Project& trial, const std::string& label, std::string* why, std::function<void()> after = {});

// Le texte de la question : les redefinitions touchees, nommees ("T_PompeDoublee.Demarrer"), la nouvelle signature.
[[nodiscard]] std::string followQuestion(const hmi::Project& trial, hmi::Id method, const std::vector<std::string>& touched);

} // namespace app
