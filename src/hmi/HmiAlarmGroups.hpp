#pragma once
//  1.10.2 (chantier AL) : LES GROUPES D'ALARMES de IHM > Alarmes > Groupes
//  (Project::alarmGroups, avec leurs reglages) et LES LIENS des groupes d'alarmes
//  des objets vers eux (Project::alarmGroupLinks).
//
//  Les groupes que nomment les alarmes (AlarmDef::group) sans etre declares sont
//  des groupes aussi, avec les reglages par defaut d'AlarmGroupDef, qui ne changent
//  rien : un projet d'avant la 1.10.2 a les memes groupes, et les memes alarmes.
//
//  CE QUI GAGNE pour une alarme d'objet : la surcharge de l'objet, puis le lien (le
//  groupe general : son nom, sa priorite par defaut, son acquittement automatique),
//  puis le symbole (ou la bibliotheque).
#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// Les groupes : ceux declares, dans leur ordre, puis ceux que nomment les alarmes de
// IHM > Alarmes sans etre declares (id kNoId, reglages par defaut).
[[nodiscard]] std::vector<AlarmGroupDef> alarmGroupsOf(const Project&);
// Le groupe declare de ce nom (nullptr : pas declare).
[[nodiscard]] const AlarmGroupDef* alarmGroupByName(const Project&, std::string_view name) noexcept;
[[nodiscard]] AlarmGroupDef*       alarmGroupById(Project&, Id) noexcept;
// Un groupe existe : declare, ou nomme par une alarme de IHM > Alarmes.
[[nodiscard]] bool                 alarmGroupExists(const Project&, std::string_view name) noexcept;
// Les reglages d'un groupe : le groupe declare, ou les reglages par defaut (nom = name).
[[nodiscard]] AlarmGroupDef        alarmGroupSettings(const Project&, std::string_view name);
// La zone d'un groupe (le resume par zone) : sa zone, ou son nom.
[[nodiscard]] std::string          alarmZoneOf(const Project&, std::string_view group);
[[nodiscard]] std::string_view     alarmAckModeLabel(AlarmAckMode) noexcept;   // "Un par un", "Par groupe", "Automatique au retour"
[[nodiscard]] std::string          uniqueAlarmGroupName(const Project&, std::string_view wanted);

// ---- 1.20.2 : la case << Incidents >> d'un groupe ---------------------------------------
// Ce qu'elle montre : 1 cochee (toutes ses alarmes ouvrent un dossier d'incident), 0 decochee
// (aucune), -1 melangee (<< - >>). Ce qui compte : la case de chaque alarme de IHM > Alarmes
// du groupe (AlarmDef::incidents) et celle du groupe lui-meme (AlarmGroupDef::incidents, que
// suivent ses alarmes d'objets) quand des groupes d'objets lui sont lies ou qu'il n'a aucune
// alarme du projet.
[[nodiscard]] int                  alarmGroupIncidents(const Project&, std::string_view group);
// Cocher (on) ou decocher la case de toutes les alarmes du groupe et la sienne (le groupe
// seulement nomme par des alarmes est declare a cette occasion, si on decoche). Le nombre de
// cases changees (0 : rien a faire).
std::size_t                        setAlarmGroupIncidents(Project&, std::string_view group, bool on);

// ---- les liens -------------------------------------------------------------------------
// Le lien qui vaut pour une alarme d'objet de ce groupe interne ("Vue.Objet",
// "Vue.Instance.Objet") : le plus precis - le groupe lui-meme, puis le groupe qui
// l'englobe le plus pres (l'instance), puis un motif ("Vue.*"), puis "symbole:Sym"
// (`symbols` : les symboles des instances qui la contiennent, "Sym_A;Sym_B").
[[nodiscard]] const AlarmGroupLink* alarmGroupLinkOf(const Project&, std::string_view objectGroup,
                                                     std::string_view symbols = {}) noexcept;
// Le lien pose sur ce groupe d'objets exactement (la case « Groupe de l'IHM »).
[[nodiscard]] const AlarmGroupLink* alarmGroupLinkFor(const Project&, std::string_view objectGroup) noexcept;
// Les groupes d'objets lies a ce groupe general, dans l'ordre des liens (la colonne
// « Groupes d'objets lies »).
[[nodiscard]] std::vector<std::string> objectGroupsLinkedTo(const Project&, std::string_view group);
// Les liens dont le groupe general n'existe pas (supprime, renomme a la main) : Compiler.
[[nodiscard]] std::vector<const AlarmGroupLink*> danglingAlarmGroupLinks(const Project&);
// Pose (group non vide) ou enleve (group vide) le lien d'un groupe d'objets. Vrai si
// quelque chose a change.
bool setAlarmGroupLink(Project&, std::string_view objectGroup, std::string_view group);
// Renommer un groupe general : le groupe declare, les alarmes de IHM > Alarmes, les
// liens et la surcharge « groupe » des objets suivent. Le nombre d'endroits changes.
std::size_t renameAlarmGroup(Project&, std::string_view from, std::string_view to);

} // namespace hmi
