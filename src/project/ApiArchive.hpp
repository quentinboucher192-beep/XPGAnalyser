// =============================================================================
//  project/ApiArchive.hpp - 1.12.30 : le projet de l'automate en un fichier .xapi
// -----------------------------------------------------------------------------
//  L'ARCHIVE D'API STUDIO, sur la base commune (project/ProjectArchive.hpp) :
//  tout le dossier du projet - le manifeste, le programme (sections ST, ladders
//  et GRAFCET gardes en XML, unites, DFB, DDT, variables), la configuration
//  materielle, les taches, les tables d'animation, l'icone, les icones du code,
//  le plan memoire, les sources generees (src/MAST.XPG), les donnees, les
//  versions - et ce qui vit a cote : la disposition des grafcets
//  (<projet>.xpglayout) ; au choix, les macros de la bibliotheque (libs/Macros).
//
//  LE CONTROLE : le manifeste annonce les comptes du projet (sections, unites,
//  blocs DFB, types derives, variables globales, taches, modules, tables
//  d'animation, versions) ; apres le deballage, le projet est relu et chaque
//  compte retrouve - ou dit.
// =============================================================================
#pragma once

#include "ProjectArchive.hpp"

#include <string>
#include <vector>

namespace domain { class Project; }

namespace project::archive {

// Les comptes d'un projet de l'automate (`versions` : combien de versions).
[[nodiscard]] std::vector<Count> apiCounts(const domain::Project&, long long versions);

struct ApiOptions {
    CollectOptions collect;
    bool           macros{true};        // joindre les macros de la bibliotheque
    std::string    libraryRoot;         // ou est libs/ ("" : pas de macros)
    std::string    tool;                // "API Studio 1.12.30"
};

struct ApiReport {
    Manifest                 manifest;
    std::vector<Skipped>     skipped;
    std::vector<Count>       counts;      // relus apres le deballage
    std::vector<std::string> problems;    // vide : tout est coherent
    std::vector<std::string> notes;
    std::uint64_t            archiveBytes{0};
    Unpacked                 unpacked;
    [[nodiscard]] bool coherent() const noexcept { return problems.empty(); }
};

// L'archive d'un dossier de projet (enregistre), en memoire.
[[nodiscard]] core::Result<Bytes> apiArchive(const std::string& folder, const ApiOptions&, ApiReport* report = nullptr);
[[nodiscard]] core::Status        exportApiArchive(const std::string& folder, const std::string& path, const ApiOptions&,
                                                   ApiReport* report = nullptr);
// Deballer dans un dossier neuf, puis relire le projet et refaire les comptes.
[[nodiscard]] core::Status unpackApiArchive(const Contents&, const std::string& folder, const UnpackOptions&,
                                            ApiReport* report = nullptr);

} // namespace project::archive
