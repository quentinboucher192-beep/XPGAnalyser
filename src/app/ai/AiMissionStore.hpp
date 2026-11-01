// =============================================================================
//  app/ai/AiMissionStore.hpp - 1.22.0 : les missions sur le disque, et leur reprise
// -----------------------------------------------------------------------------
//  IA/missions/<id>.json, un fichier par mission (AiMission.hpp : toJson), ecrit
//  D'UN BLOC (core::writeFileAtomic : un .tmp relu puis renomme) avec une copie
//  de la version precedente (<id>.json.bak) : un arret brutal laisse toujours
//  une version complete. Un fichier illisible est repris de sa copie (et c'est
//  dit) ; sinon il est laisse tel quel et signale - jamais efface.
//
//  L'EMPREINTE DU PROJET (printOf) : le projet IHM mis en fichiers
//  (hmi::store::serializeProject, sans disque), une empreinte par fichier et
//  une pour l'ensemble. A la reprise, la comparaison dit si le projet est le
//  meme (cas A), modifie (cas B : quels fichiers - << la vue Vue_Accueil >>,
//  << les variables >>) ou introuvable (cas C : un autre projet ouvert, aucun).
//
//  LA REPRISE (recover) : un appel a l'IA en cours a la fermeture est marque
//  INTERROMPU (sa proposition n'existe pas : rien n'est rejoue), les etapes en
//  cours redeviennent pretes, l'etat du projet est compare. Jamais une
//  application rejouee : une proposition appliquee l'est par le projet reel
//  (AiMission.hpp : verifyAgainst), pas par ce qu'en dit le fichier.
// =============================================================================
#pragma once

#include "AiMission.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi { struct Project; }

namespace app::ai::mission {

// ---- le disque ----------------------------------------------------------------------------------------------------
[[nodiscard]] std::string missionsDir();                      // IA/missions (AiPaths.hpp : root)
[[nodiscard]] std::string missionFile(std::string_view id);   // IA/missions/<id>.json
// Ecrire (atomique, avec .bak). Faux et `why` : impossible ; une mission lue d'un format plus recent (readOnly) n'est
// jamais reecrite (faux, et dit).
bool save(const Mission&, std::string* why = nullptr);
struct Loaded {
    std::vector<Mission>     missions;     // les plus recentes d'abord (modified)
    std::vector<std::string> problems;     // "<fichier> : illisible, repris de sa copie", "... : illisible (laisse tel quel)"
    std::vector<std::string> readOnly;     // les identifiants lus d'un format plus recent (jamais reecrits)
};
[[nodiscard]] Loaded loadAll();
[[nodiscard]] std::optional<Mission> load(std::string_view id, std::string* why = nullptr);
// Supprimer le fichier (et sa copie) - seulement a la demande de l'utilisateur (archiver garde tout).
bool remove(std::string_view id, std::string* why = nullptr);

// ---- l'empreinte du projet ------------------------------------------------------------------------------------------
struct ProjectPrint {
    std::string                                       all;     // l'empreinte de l'ensemble
    std::vector<std::pair<std::string, std::string>>  files;   // fichier -> empreinte, tries
};
[[nodiscard]] ProjectPrint printOf(const hmi::Project&);
// Ce qui a change entre deux empreintes, en clair : "la vue Vue_Accueil (modifi\xC3\xA9" "e)", "les variables (modifi\xC3\xA9" "es)",
// "le script Calcul (ajout\xC3\xA9)" ; vide : rien.
[[nodiscard]] std::vector<std::string> changesBetween(const std::vector<std::pair<std::string, std::string>>& before,
                                                      const std::vector<std::pair<std::string, std::string>>& after);
// Prendre l'empreinte dans la mission (ProjectLink : print, files, printed).
void stamp(Mission&, const ProjectPrint&, std::string now);

// ---- la reprise ---------------------------------------------------------------------------------------------------------
struct Recovery {
    Coherence                coherence{Coherence::Unknown};
    std::vector<std::string> changes;        // cas B : ce qui a change
    bool                     interrupted{false};   // un appel a l'IA etait en cours a la fermeture
    std::string              text;           // en clair, pour l'ecran de reprise
    std::string              nextAction;     // la prochaine action possible
};
// `openProjectId` : l'identite du projet ouvert (memory::identifyProject ; vide : aucun projet) ; `project` : le projet
// ouvert (nul : aucun). Met a jour la mission (coherence, etapes en cours -> pretes, l'appel interrompu, l'historique),
// SANS RIEN APPLIQUER. Vrai si la mission a change (a enregistrer).
bool recover(Mission&, std::string_view openProjectId, const hmi::Project* project, std::string now, Recovery* out = nullptr);
// Le meme constat sans rien modifier (l'ecran de reprise, avant de cliquer).
[[nodiscard]] Recovery inspect(const Mission&, std::string_view openProjectId, const hmi::Project* project);

} // namespace app::ai::mission
