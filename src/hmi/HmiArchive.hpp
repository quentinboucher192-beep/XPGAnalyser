// =============================================================================
//  hmi/HmiArchive.hpp — Exporter / Importer : le projet IHM en une archive
// -----------------------------------------------------------------------------
//  UNE ARCHIVE .zip QUI CONTIENT LE DOSSIER ihm/ TEL QU'IL S'ENREGISTRE :
//  configuration, vues, ressources, scripts, alarmes, utilisateurs, recettes,
//  securite, historiques - et un MANIFESTE (manifeste.txt) : ce que l'archive
//  doit contenir (combien de vues, d'objets, d'alarmes...) et l'empreinte
//  SHA-256 de chaque fichier. Elle s'ouvre avec n'importe quel outil zip.
//
//  IMPORTER RECONSTRUIT TOUT LE PROJET IHM depuis l'archive, puis fait le
//  CONTROLE DE COHERENCE : chaque fichier est la et intact (taille, empreinte),
//  chaque compte du manifeste est retrouve dans le projet relu. Ce qui ne
//  colle pas est dit, fichier par fichier ; l'ecran enchaine sur Generer.
//
//  Les entrees sont ecrites sans compression (methode "stored") : l'archive
//  reste lisible partout, et un lecteur zip suffit a la relire. A l'import,
//  les entrees compressees (deflate) sont acceptees aussi.
// =============================================================================
#pragma once

#include "HmiHistory.hpp"
#include "HmiModel.hpp"
#include "HmiStore.hpp"
#include "../core/Result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hmi {

struct ArchiveEntry {
    std::string   path;          // "ihm/vues/0003-Vue_Armoire_A.vue"
    std::uint64_t bytes{0};
    std::string   sha256;        // hexa
    bool          ok{true};      // a l'import : present et intact
};

// Une ligne du controle : "Vues : 4 attendu(s), 4 relu(s)".
struct ArchiveCount {
    std::string section;
    long long   expected{0};
    long long   found{0};
    [[nodiscard]] bool ok() const noexcept { return expected == found; }
};

struct ArchiveReport {
    std::vector<ArchiveEntry> entries;
    std::vector<ArchiveCount> counts;
    std::vector<std::string>  problems;     // vide : tout est coherent
    std::string               created;      // date de l'archive (manifeste)
    std::string               projectName;
    std::uint64_t             archiveBytes{0};
    [[nodiscard]] bool coherent() const noexcept { return problems.empty(); }
};

// Les comptes d'un projet (et de son historique), section par section : ce
// que le manifeste annonce et ce que le controle retrouve.
[[nodiscard]] std::vector<ArchiveCount> projectCounts(const Project&, const History*);

[[nodiscard]] core::Status exportArchive(const Project&, const History* history, const std::string& zipPath,
                                         ArchiveReport* report = nullptr);
// `history` recoit l'historique de l'archive (s'il y en a un).
[[nodiscard]] core::Result<Project> importArchive(const std::string& zipPath, History* history = nullptr,
                                                  ArchiveReport* report = nullptr, LoadReport* load = nullptr);

} // namespace hmi
