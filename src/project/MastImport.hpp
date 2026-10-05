// =============================================================================
//  project/MastImport.hpp - lot 7 : importer un .XPG dans le projet ouvert
// -----------------------------------------------------------------------------
//  UN NOUVEAU MAST REMPLACE LE PROGRAMME, PAS LE PROJET. Le .XPG apporte tout le
//  cote API (taches, sections, unites, types DDT, blocs DFB, variables) ; le
//  projet garde ce que le .XPG ne connait pas : l'IHM, les versions, les
//  reglages (plan memoire, icone), la configuration materielle (sauf si un
//  .XHW vient avec), et - option cochee - ses tables d'animation.
//
//  LES LIENS SONT DES NOMS. L'IHM lit l'automate par le NOM d'une variable
//  ("Armoires[0].ana.PT1.mes", dans une vue, un script, une alarme...) ou par
//  son ADRESSE (une variable IHM liee a %MW200) ; les tables d'animation par
//  le nom. Rien ne retient un indice : une variable qui garde son nom (et son
//  adresse) garde ses liens sans rien renumeroter. Le plan dit donc, avant
//  d'importer, ce qui se retrouve et ce qui se perd :
//
//    GARDE      meme nom, meme type, meme adresse (une instance de DFB / DDT :
//               note si la definition de son type change) ;
//    CHANGE     meme nom, un autre type ou une autre adresse ;
//    SUPPRIME   seulement dans le projet ;
//    NOUVEAU    seulement dans le .XPG ;
//    LIENS IHM PERDUS  ce que l'IHM lit et qui ne se retrouve plus (la
//               variable, ou le champ du chemin, n'existe plus).
//
//  PUR : ni ecran ni IHM ici. Les references de l'IHM sont trouvees par
//  l'appelant (l'IHM vit au-dessus de cette bibliotheque) et passees en
//  HmiRef ; le plan les resout contre les deux projets.
//
//  L'IMPORT EST UNE COMMANDE : elle garde le projet d'avant, Ctrl+Z le rend
//  tel quel, Ctrl+Y le refait.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace project::mast {

// Les cinq onglets du recapitulatif, dans leur ordre.
enum class Status : std::uint8_t { Kept = 0, Changed, Removed, Added, LinkLost };
constexpr std::size_t kStatusCount = 5;

// Le premier niveau de l'arbre d'un onglet, dans son ordre.
enum class Group : std::uint8_t { Variables = 0, Instances, Types, Program, Tables, Hmi, Hardware };
constexpr std::size_t kGroupCount = 7;

[[nodiscard]] const char* statusLabel(Status s) noexcept;   // "Garde", "Change"... (accentues)
[[nodiscard]] const char* groupLabel(Group g) noexcept;     // "Variables globales", "Instances de DFB/DDT"...

// Une ligne du recapitulatif.
struct Item {
    Status      status{Status::Kept};
    Group       group{Group::Variables};
    std::string sub;       // le deuxieme niveau : un genre, un type, une table, une vue ; vide : sous le groupe
    std::string name;      // la variable, le type, la section, l'objet de l'IHM...
    std::string detail;    // ce qui change, ou pourquoi
};

// Une reference de l'IHM a l'automate, trouvee par l'appelant.
struct HmiRef {
    std::string where;     // "Variables IHM", "Vue Vue_Accueil", "Scripts generaux", "Alarmes"...
    std::string what;      // "Pression_IHM", "Bouton_Marche", le nom du script, de l'alarme...
    std::string path;      // le chemin cite : "Armoires[0].ana.PT1.mes" ; vide : par l'adresse seule
    std::string address;   // une variable IHM liee : l'adresse qu'elle lit ("%MW200")
};

struct Options {
    // Coche (recommande) : une variable identique (meme nom, type et adresse)
    // garde sa declaration du projet (commentaire, valeur initiale,
    // commentaires des elements) et les tables d'animation du projet restent
    // (sans les lignes des variables disparues, avec les lignes nouvelles du
    // .XPG). Decoche : tout le cote API vient du .XPG, tables comprises.
    bool keepIdentical{true};
};

struct Plan {
    std::string       source;          // "MAST.XPG", ou "MAST.XPG + CONFIG.XHW"
    Options           options;
    std::vector<Item> items;
    std::array<std::size_t, kStatusCount> counts{};
    // Pour le bandeau : le projet d'avant et le .XPG, en chiffres.
    std::size_t variablesBefore{0}, variablesAfter{0};     // les globales
    std::size_t sectionsBefore{0}, sectionsAfter{0};       // sections et sous-routines des taches
    std::size_t unitsAfter{0}, ddtAfter{0}, dfbAfter{0}, tasksAfter{0};
    bool        hardwareReplaced{false};                   // un .XHW vient avec
    [[nodiscard]] std::size_t count(Status s) const noexcept { return counts[static_cast<std::size_t>(s)]; }
    // Les lignes d'un onglet, dans l'ordre du plan.
    [[nodiscard]] std::vector<const Item*> itemsOf(Status s) const;
};

// Compare le projet ouvert (`current`) et le .XPG lu (`imported`).
[[nodiscard]] Plan makePlan(const domain::Project& current, const domain::Project& imported,
                            const std::vector<HmiRef>& hmi, const Options& options = {},
                            std::string source = {});

// Le projet apres l'import : le cote API de `imported`, ce que `current` garde.
[[nodiscard]] domain::Project merge(const domain::Project& current, const domain::Project& imported,
                                    const Options& options = {});

// Lit un .XPG (et, apres lui, son .XHW) dans un projet neuf, analyse compris,
// SANS le bus de l'application : ImportFinished y remplacerait tout le projet.
// Le premier fichier doit etre un programme (.XPG), les suivants des .XHW.
[[nodiscard]] core::Result<std::shared_ptr<domain::Project>> read(const std::vector<std::string>& paths);

// Le genre d'un fichier d'apres son contenu (le premier Ko), puis son extension :
// 1 un programme (.XPG), 2 une configuration (.XHW), 0 autre chose.
[[nodiscard]] int sniffFile(const std::string& path);

// Un chemin ("Armoires[0].ana.PT1.mes") existe-t-il dans ce projet ? Sa racine
// est une variable globale, chaque champ et chaque indice existent dans son
// type. `why` : ce qui manque. Un type que ni le projet ni la bibliotheque ne
// connaissent est suivi sans rien dire (rien a en dire).
[[nodiscard]] bool pathExists(const domain::Project& p, std::string_view path, std::string* why = nullptr);

// ---------------------------------------------------------------- la commande --
class ImportMastCommand final : public core::ICommand {
public:
    ImportMastCommand(std::shared_ptr<domain::Project> project, std::shared_ptr<const domain::Project> imported,
                      Options options, std::string source);
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override;
private:
    std::shared_ptr<domain::Project>        project_;
    std::shared_ptr<const domain::Project>  imported_;
    Options                                 options_;
    std::string                             source_;
    std::optional<domain::Project>          before_;    // le projet d'avant (Ctrl+Z)
};

} // namespace project::mast
