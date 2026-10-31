// =============================================================================
//  app/ai/AiProjectAnswers.hpp - 1.19.0 : l'IA integree repond sur le PROJET
// -----------------------------------------------------------------------------
//  Trois familles de demandes, sans reseau, sur les donnees reelles du projet :
//
//    les relations   << Quels objets utilisent cette variable ? >>, << Si je
//                    supprime ce type... >>, << Quels equipements sont utilises
//                    dans cette vue ? >>, << Quelles alarmes concernent la pompe
//                    2 ? >>, << Montre-moi les scripts qui utilisent Niveau_Cuve_1 >>,
//                    << Quels objets sont orphelins ? >> - lues dans le graphe des
//                    relations (hmi::rel). JAMAIS une dependance inventee : la
//                    reponse cite ce que le graphe a trouve, ou dit qu'il n'a rien
//                    trouve et ce qu'il ne suit pas (un nom calcule) ;
//    verifier        << Verifie tout mon projet et propose les corrections >> : la
//                    verification globale (hmi::audit::run) en erreurs bloquantes,
//                    avertissements et informations ; les corrections d'adresse
//                    deviennent une PROPOSITION (jamais appliquee sans validation) ;
//    les adresses    << Optimise les adresses des pompes >> : le plan avant / apres
//                    (hmi::audit::optimize) et sa proposition - une adresse
//                    existante ne change jamais en silence.
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "../../hmi/HmiAudit.hpp"
#include "../../hmi/HmiRelations.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::answers {

enum class Question : std::uint8_t {
    Users,           // qui l'utilise (tout)
    ObjectsUsing,    // les objets qui l'utilisent
    ScriptsUsing,    // les scripts et fonctions qui l'utilisent
    AlarmsOf,        // les alarmes qui le concernent
    Impact,          // si je le supprime
    ViewEquipments,  // les equipements d'une vue
    Orphans,         // les elements orphelins
};

struct Answer {
    std::string              text;            // markdown simple, au tutoiement
    std::vector<std::string> suggestions;
    std::optional<Proposal>  proposal;        // des corrections a valider (verifier, optimiser) ; nullopt : rien a proposer
    bool                     understood{true};
};

// Le sujet d'une question, lu dans la demande : un nom du projet ecrit tel quel (sans casse), ou "la pompe 2", "pompe2",
// "cuve 1" -> Pompe_2, Cuve_1 quand il existe. `prefer` : les genres a preferer (une variable d'abord...). Vide : rien.
[[nodiscard]] std::string subjectOf(const hmi::rel::Graph&, std::string_view request, const std::vector<hmi::rel::Kind>& prefer = {});

// Une question de relations. `subject` : le nom (une vue pour ViewEquipments ; vide pour Orphans) ; `openView` : la vue
// ouverte (<< cette vue >>), si le sujet manque.
[[nodiscard]] Answer relations(const hmi::Project&, Question, std::string_view subject, std::string_view openView = {});

// La verification globale et ses corrections proposees.
[[nodiscard]] Answer verify(const hmi::Project&, const hmi::NameExists& plcHasName = {});

// 1.19.0 : LES CONVENTIONS DU PROJET, lues dans le projet (jamais devinees) : le prefixe des types, des vues, des
// symboles, des popups, des alarmes ; les series (Pompe_1, Pompe_2...) ; la taille des vues ; les dossiers ; les plages
// d'adresses par equipement ; les priorites des alarmes. La reponse dit comment les garder dans la memoire de l'IA
// (une connaissance << Contexte de projet >>, propre a ce projet) - rien n'y est ecrit sans l'utilisateur.
[[nodiscard]] Answer conventions(const hmi::Project&);

// Le plan d'optimisation des adresses (`filter` : des noms, * permis ; vide : toutes) et sa proposition.
[[nodiscard]] Answer optimizeAddresses(const hmi::Project&, const std::vector<std::string>& filter);

} // namespace app::ai::answers
