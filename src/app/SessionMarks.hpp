// =============================================================================
//  app/SessionMarks.hpp - 1.43.0 : ce que les editeurs de vues savent de la session
// -----------------------------------------------------------------------------
//  Le service des sessions (app/SessionService) y met, a chaque changement, les
//  objets que les AUTRES membres tiennent (ceux qu'ils ont choisis) ; le canevas
//  d'une vue les entoure de leur couleur, avec leur nom, et refuse de les choisir.
//  Dans l'autre sens, le canevas y dit ce que CE PC choisit et la vue ou il est :
//  le service le lit a chaque image et l'envoie. Hors session : rien (active()).
//  Le fil de l'interface seulement.
// =============================================================================
#pragma once

#include "../hmi/HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace app::sessionmarks {

struct Holder {
    int           member{0};
    std::string   name;
    std::uint32_t color{0};   // 0xRRGGBB
    bool operator==(const Holder&) const = default;
};
using Held = std::map<std::pair<hmi::Id, hmi::Id>, Holder>;   // (vue, objet) -> qui le tient

void setActive(bool on);
[[nodiscard]] bool active();
void setHeld(Held held);
// Qui d'autre tient cet objet (nullptr : personne, ou hors session).
[[nodiscard]] const Holder* heldByOther(hmi::Id view, hmi::Id object);
[[nodiscard]] std::vector<std::pair<hmi::Id, const Holder*>> heldIn(hmi::Id view);
[[nodiscard]] std::uint64_t revision();

// Ce que ce PC choisit, et la vue ou il est (la derniere peinte, ou celle d'un choix).
struct Mine {
    hmi::Id view{hmi::kNoId};
    std::vector<hmi::Id> selection;
    std::uint64_t serial{0};
};
void mine(hmi::Id view, const std::vector<hmi::Id>& selection);
void seen(hmi::Id view);
[[nodiscard]] const Mine& current();

// Un choix refuse (l'objet est pris) : le service le dit dans la barre d'etat.
void setRefusedNotice(std::function<void(const std::string&)> fn);
void refused(const std::string& text);

} // namespace app::sessionmarks
