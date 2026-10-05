// =============================================================================
//  hmi/HmiCheck.hpp - Generer et Compiler : ce qui ne tournera pas, avant de
//                     le lancer
// -----------------------------------------------------------------------------
//  GENERER verifie la COHERENCE du projet IHM : vues invalides, noms en double,
//  calques et groupes casses, references circulaires, variables inexistantes
//  dans le programme, ressources manquantes, du mauvais genre ou inutilisees,
//  fichiers externes absents ou modifies depuis leur lien.
//
//  COMPILER traduit tout ce qui s'execute : chaque expression de propriete,
//  chaque texte a trous, chaque script ST. Les scripts C et C++ sont relus
//  (accolades, parentheses) mais ne s'executent pas en simulation : c'est dit
//  en information, pas cache.
//
//  Chaque constat dit OU (vue, objet, propriete) : l'ecran s'en sert pour y
//  aller d'un double-clic.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "HmiExprCheck.hpp"   // ---- Lot API 8 : les expressions impossibles (les chemins de l'automate) ----

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct Issue {
    enum class Severity : std::uint8_t { Info, Warning, Error } severity{Severity::Error};
    std::string category;     // "Vue", "Objet", "Expression", "Variable", "Script", "Action"...
    Id          view{kNoId};
    Id          object{kNoId};
    std::string property;     // la cle, ou le nom du script
    std::string message;
    Id          script{kNoId};   // un script (de vue ou general) : y aller
    int         line{0};         // ... a cette ligne (1 = la premiere)
    Id          item{kNoId};     // lot 4 : l'alarme, la recette, l'utilisateur en cause
    // 1.10 : la colonne de la faute dans le script (1 = le premier octet de la
    // ligne ; 0 : la ligne entiere) et sa longueur - le clic y mene.
    int         column{0};
    int         length{0};
};

struct IssueCounts {
    std::size_t errors{0}, warnings{0}, infos{0};
};

// Le programme connait-il ce nom (racine d'un chemin) ? Fourni par l'appelant,
// qui a le projet automate : le module IHM ne le lit pas lui-meme.
using NameExists = std::function<bool(std::string_view root)>;

// `projectFolder` : ou chercher les fichiers externes relatifs (vide : ils ne
// sont verifies que s'ils sont designes par un chemin absolu).
[[nodiscard]] std::vector<Issue> generate(const Project&, const NameExists& plcHasName,
                                          const std::string& projectFolder = {});
// Lot 13 : ce que l'ecran fournit en plus - la mesure des textes avec ses
// polices (les textes qui debordent) ; sans elle, une estimation.
namespace comm { class Plan; }
struct GenerateOptions {
    std::string projectFolder;
    std::function<double(std::string_view text, std::string_view font, double sizePx)> measure;
    // Lot 14 : le plan d'adressage Modbus (Configuration > Communication) - les
    // lignes de la table illisibles, les variables que l'IHM utilise sans
    // adresse. Nul : ces controles ne se font pas.
    const comm::Plan* plan{nullptr};
    // ... et ce qui est une valeur simple de l'automate (le simulateur le sait) :
    // une structure entiere, une instance de bloc ne se lisent pas par Modbus -
    // l'IHM en lit les membres. Vide : tout chemin compte.
    std::function<bool(std::string_view path)> plcScalar;
    // ---- Lot API 8 : les expressions impossibles ---- les membres et les indices
    // constants des chemins de l'automate (vides : pas verifies).
    exprcheck::PlcPaths plcPaths{};
};
// (Un autre nom que generate : generate(p, plc, {}) reste sans ambiguite.)
[[nodiscard]] std::vector<Issue> generateWith(const Project&, const NameExists& plcHasName, const GenerateOptions&);
[[nodiscard]] std::vector<Issue> compile(const Project&);
// ---- Lot API 8 : les expressions impossibles ----
// Compiler avec les noms de l'automate : compile() et, en plus, chaque
// expression qui se lit mais ne peut pas marcher - un nom inconnu (avec le nom
// connu le plus proche), un membre ou un indice constant hors du type, une
// fonction inconnue ou le mauvais nombre d'arguments, un texte compare a un
// nombre, un booleen ou une couleur attendus et autre chose donne, une division
// par zero, une ecriture vers une constante ou une variable en lecture seule.
// Chacune est une ERREUR a son endroit. generateWith() les dit aussi (avec les
// expressions illisibles) : elles bloquent comme ses autres erreurs.
[[nodiscard]] std::vector<Issue> compileWith(const Project&, const NameExists& plcHasName, const exprcheck::PlcPaths& plcPaths = {});
// Ces seules erreurs (et les expressions illisibles), pour un controle rapide.
[[nodiscard]] std::vector<Issue> expressionIssues(const Project&, const NameExists& plcHasName, const exprcheck::PlcPaths& plcPaths = {});
[[nodiscard]] IssueCounts        count(const std::vector<Issue>&);
[[nodiscard]] std::string_view   toString(Issue::Severity) noexcept;

} // namespace hmi
