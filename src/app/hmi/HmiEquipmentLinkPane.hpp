// =============================================================================
//  app/hmi/HmiEquipmentLinkPane.hpp - 1.12.11 (lot G) : la section « Liaison
//                                     Equipement » de l'inspecteur
// -----------------------------------------------------------------------------
//  Un symbole du synoptique relie a une structure (sa propriete Equipement :
//  P1, Pompes[2], le parametre d'un symbole) montre, sous sa section, le tableau
//  de correspondance de son type : une ligne par role (Valeur, Defaut, Mode,
//  Libelle...), le membre lu - par son nom, ou choisi -, et « a la main » quand
//  l'objet regle lui-meme la propriete (elle gagne). Un choix vaut pour toutes
//  les structures de ce type (Project::equipmentLinks, hmi::eqlink) : une
//  commande du projet, que Ctrl+Z defait.
// =============================================================================
#pragma once

#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace app::eqlinkpane {

using Apply = std::function<void(core::CommandPtr)>;

// Ce que montre la liste d'un role : revenir aux noms proposes ; aucun membre.
inline constexpr const char* kByName = "(selon le nom)";
inline constexpr const char* kNoMember = "(aucun)";

// La section de l'objet `object` de la vue `view` ; vide : il n'a pas de liaison Equipement.
[[nodiscard]] std::vector<ui::PropertyGrid::Category> categories(const hmi::DocumentPtr&, const Apply&, hmi::Id view, hmi::Id object);
// Choisir le membre d'un role pour un type (kByName : les noms proposes ; kNoMember : aucun). Vrai : le
// projet a change (une commande appliquee).
bool choose(const hmi::DocumentPtr&, const Apply&, const std::string& type, const std::string& role, const std::string& choice);

} // namespace app::eqlinkpane
