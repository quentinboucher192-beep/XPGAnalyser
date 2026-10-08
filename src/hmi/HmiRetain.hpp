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

// ---- Le verrou : un seul poste ecrit le stockage (plusieurs instances) ----
//  remanence_exploitation.lock, a cote du fichier : le PID et l'heure du poste qui
//  ecrit. Le poste le rafraichit en marche (au moins toutes les 60 s) et le rend a
//  l'arret ; le verrou d'un autre PID que personne n'a rafraichi depuis 3 minutes
//  est repris (un poste arrete brutalement). Un autre poste en marche : celui-ci lit
//  les valeurs gardees mais n'ecrit pas (il le dit, et reessaie toutes les 30 s).
//  lockedBy : qui le tient (vide : personne d'autre, ou un verrou perime).
struct Lock {
    bool        held{false};
    std::string owner;       // un autre poste : "PID 4120, depuis 2026-10-08 21:10:05"
    std::string error;       // ou le verrou ne s'ecrit pas (dossier refuse, disque plein) : pourquoi
};
[[nodiscard]] Lock        acquireLock(const std::filesystem::path& file);
void                      refreshLock(const std::filesystem::path& file);
void                      releaseLock(const std::filesystem::path& file);
[[nodiscard]] std::string lockedBy(const std::filesystem::path& file);

// ---- Exporter, importer les valeurs gardees ----
//  Le format du poste (ce fichier), ou un CSV pour Excel (';', UTF-8 avec BOM) :
//  Variable;Chemin;Type;Valeur;Date;Type declare;Id - un nombre a virgule ecrit
//  avec une virgule (Excel en francais), un texte qui commencerait par = + - @
//  precede d'une apostrophe (Excel n'en fait pas une formule). A la lecture : la
//  virgule ou le point, TRUE/FALSE, VRAI/FAUX, oui/non, 1/0 ; le separateur de la
//  ligne des titres (';', ',' ou tabulation) ou d'une ligne "sep=;" ; les colonnes
//  se trouvent par leur titre, dans n'importe quel ordre.
[[nodiscard]] std::string toCsv(const Store&);
[[nodiscard]] bool        fromCsv(std::string_view text, Store& out, std::string* why = nullptr);
// L'un ou l'autre : l'en-tete du poste, sinon un CSV.
[[nodiscard]] bool        parseAny(std::string_view text, Store& out, std::string* why = nullptr);

// Importer : les valeurs de `incoming` remplacent celles des memes variables dans
// `store` ; les autres valeurs gardees restent. Une valeur va a sa variable par son
// identifiant si le nom concorde, sinon par son nom (un autre projet, un CSV sans
// Id). Ecartees : les variables inconnues, non remanentes ou liees. Un type devenu
// incompatible est compte : le poste reprendra la valeur initiale (et le dira).
struct ImportReport {
    std::size_t values{0}, variables{0}, unknown{0}, notRetained{0}, incompatible{0};
    [[nodiscard]] std::string summary() const;
};
ImportReport importInto(Store& store, const Project&, const Store& incoming, const std::string& now);

} // namespace hmi::retain
