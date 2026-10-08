// =============================================================================
//  hmi/HmiRetain.hpp - 1.11.16 : LES VARIABLES IHM REMANENTES EN EXPLOITATION
// -----------------------------------------------------------------------------
//  Une variable IHM cochee « Remanente » (Variable::retain) garde sa valeur sur
//  le poste d'exploitation : a chaque changement (regroupe, au plus une
//  ecriture par seconde), elle est ecrite dans le stockage du poste ; au
//  lancement suivant, elle est rendue apres les valeurs initiales, avant les
//  scripts de Demarrage. La simulation de l'editeur n'ecrit jamais ici.
//
//  DEUX STOCKAGES SEPARES : la remanence de simulation (hmi::simdata) vit dans
//  <projet>/.xpg/simulation/remanence.txt ; celle d'exploitation, ici, dans
//  <projet>/ihm/historique/remanence_exploitation.txt - son propre format (son
//  en-tete, sa version), ses propres diagnostics. Les deux dossiers sont hors
//  des versions du projet.
//
//  Chaque valeur garde son identifiant stable (la variable), son chemin (la
//  case d'une structure, d'un tableau), son type, sa valeur et la date de sa
//  derniere sauvegarde. Le fichier est ecrit d'un bloc (core::writeFileAtomic,
//  une copie .bak) ; sa ligne "fin" compte ce qui precede : un fichier coupe ou
//  abime est refuse et sa copie .bak reprise.
//
//  Le retour suit les regles de la remanence de simulation (simdata) : par
//  identifiant, types compatibles convertis, sinon la valeur initiale ; une
//  variable qui n'est plus remanente : sa valeur gardee est ignoree.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiSimData.hpp"
#include "../core/Result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::retain {

inline constexpr std::string_view kHeader = "xpg-remanence-exploitation 1";
inline constexpr std::string_view kFileName = "remanence_exploitation.txt";

struct Entry {
    simdata::Cell cell;
    std::string   date;          // la derniere sauvegarde de cette valeur ("2026-10-08 21:10:05")
};
struct Store {
    std::string        project;  // le nom du projet (pour le dire)
    std::string        date;     // la derniere ecriture du fichier
    std::vector<Entry> entries;
    [[nodiscard]] const Entry* find(Id variable, std::string_view path) const;
};

[[nodiscard]] std::string serialize(const Store&);
[[nodiscard]] bool        parse(std::string_view text, Store& out, std::string* why = nullptr);

// Le fichier du projet : <dossier>/ihm/historique/remanence_exploitation.txt (vide : pas de dossier).
[[nodiscard]] std::filesystem::path fileOf(const std::string& projectFolder);
// Lire : le fichier, sinon sa copie .bak (`fromBackup` le dit) ; faux : ni l'un ni l'autre
// (`why` : absent, ou pourquoi il est refuse). Ecrire : d'un bloc, l'ancien en .bak.
bool                       load(const std::filesystem::path& file, Store& out, std::string* why = nullptr, bool* fromBackup = nullptr);
[[nodiscard]] core::Status save(const std::filesystem::path& file, const Store& store);

// Les cases des variables remanentes non liees, a leur valeur du moment.
[[nodiscard]] std::vector<simdata::Cell> captureRetained(const Project&, const simdata::Reader& read);
// Ce qu'il faut rendre au lancement : les cases des variables encore remanentes ;
// `ignored` : les variables gardees qui ne le sont plus (leur valeur est ignoree).
[[nodiscard]] std::vector<simdata::Cell> retainedCells(const Project&, const Store&, int* ignored = nullptr);
// Garder `cells` dans `store` : une valeur qui change prend la date `now` ; une
// valeur egale garde la sienne ; les cases des variables qui ne sont plus
// remanentes partent. Vrai : quelque chose a change.
bool merge(Store& store, const Project&, const std::vector<simdata::Cell>& cells, const std::string& now);

// L'etat d'une variable pour l'editeur (la grille de proprietes).
struct VariableState {
    bool        saved{false};    // une valeur gardee existe
    std::string value;           // "7", "4 cases"
    std::string date;            // la derniere sauvegarde
    std::string state;           // "a jour : rendue au prochain lancement du poste", "jamais sauvegardee"...
    bool        problem{false};  // un type incompatible, une variable liee
};
[[nodiscard]] VariableState stateOf(const Project&, const Variable&, const Store* store);

// Reinitialiser : retirer les valeurs gardees d'une variable (kNoId : toutes) ; rend combien.
std::size_t reset(Store& store, Id variable);

// Verifier l'integrite du fichier : lisible, complet, ses valeurs et sa copie .bak.
[[nodiscard]] std::string checkIntegrity(const std::filesystem::path& file, const Project&);

} // namespace hmi::retain
