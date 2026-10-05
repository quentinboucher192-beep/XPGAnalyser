#pragma once
//  1.10.2 (chantier AL) : LES COMMANDES (Ctrl+Z) des groupes d'alarmes de IHM >
//  Alarmes > Groupes et des liens des groupes d'alarmes des objets. Chacune rend
//  nullptr quand rien ne change (l'appelant n'empile rien). Voir HmiAlarmGroups.hpp.
#include "HmiAlarmGroups.hpp"
#include "HmiCommands.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// Un groupe neuf (nom libre d'apres `wanted`), reglages par defaut ; son nom dans `created`.
[[nodiscard]] core::CommandPtr addAlarmGroupCmd(const DocumentPtr&, std::string_view wanted, std::string* created = nullptr);
// Les reglages d'un groupe (le nom de `settings` est ignore). Un groupe seulement nomme
// par des alarmes est declare a cette occasion.
[[nodiscard]] core::CommandPtr setAlarmGroupCmd(const DocumentPtr&, std::string_view name, const AlarmGroupDef& settings);
// Renommer : les alarmes, les liens et les surcharges des objets suivent.
[[nodiscard]] core::CommandPtr renameAlarmGroupCmd(const DocumentPtr&, std::string_view from, std::string_view to,
                                                   std::string* why = nullptr);
// Supprimer le groupe declare (ses liens restent : Compiler les signale).
[[nodiscard]] core::CommandPtr deleteAlarmGroupCmd(const DocumentPtr&, std::string_view name);
// Lier des groupes d'objets a un groupe de l'IHM (vide : « (aucun) », les liens partent) :
// la case « Groupe de l'IHM » (un groupe) et « Lier... » (plusieurs d'un coup).
[[nodiscard]] core::CommandPtr linkObjectGroupsCmd(const DocumentPtr&, const std::vector<std::string>& objectGroups,
                                                   std::string_view group);

// 1.11 (R111) : « Lier... » dans une fenetre a cocher.
// Ce qu'on peut lier, dans l'ordre de la fenetre : les groupes d'objets qui portent des
// alarmes (Vue.Objet, Vue.Instance.Objet), les vues (Vue.*), les symboles (symbole:Sym),
// puis les groupes d'objets deja lies qui ne sont dans aucune de ces listes (un motif
// ecrit a la main, un objet renomme : on peut les decocher).
struct LinkableObjectGroup {
    std::string name;          // Vue_Pompes.Pompe_3, Vue_Pompes.*, symbole:Sym_Vanne
    int         kind{0};       // 0 un groupe d'objets, 1 une vue, 2 un symbole, 3 un autre lien
    std::size_t alarms{0};     // les alarmes des objets qu'il englobe
    std::string linkedTo;      // le groupe de l'IHM auquel il est lie ("" : aucun)
};
[[nodiscard]] std::vector<LinkableObjectGroup> linkableObjectGroups(const Project&);
// Les groupes d'objets lies a `group` deviennent exactement `checked` (les cases cochees) :
// les coches pas encore lies le sont (un groupe lie ailleurs change de groupe), les lies
// decoches ne le sont plus. UNE seule commande (Ctrl+Z) ; nullptr si rien ne change.
[[nodiscard]] core::CommandPtr setLinkedObjectGroupsCmd(const DocumentPtr&, std::string_view group,
                                                        const std::vector<std::string>& checked);

} // namespace hmi
