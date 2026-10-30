// =============================================================================
//  hmi/HmiArchive.hpp — Exporter / Importer : le projet IHM en une archive
// -----------------------------------------------------------------------------
//  1.12.30 : L'ARCHIVE .xihm, LE PROJET ENTIER. Tout le dossier du projet tel
//  qu'il est enregistre - la fiche (project.xpgproj), ihm/ (configuration, vues,
//  ressources de tous les genres, scripts, fonctions, alarmes, recettes,
//  utilisateurs, equipements et communication, simulation, modeles, styles,
//  historique des objets ihm/historique-edition/), la remanence de la simulation
//  (.xpg/simulation/), les donnees, les versions, l'historique de marche - et
//  les fichiers externes lies hors du dossier (_externes/). La base commune est
//  project/ProjectArchive.hpp (le zip, le manifeste, le deballage) ; ici, ce qui
//  est propre a l'IHM : ses comptes, ses fichiers externes, le controle.
//
//  UN MANIFESTE (manifeste.txt) : ce que l'archive doit contenir (combien de
//  vues, d'objets, d'alarmes...) et l'empreinte SHA-256 de chaque fichier. Elle
//  s'ouvre avec n'importe quel outil zip.
//
//  OUVRIR (deballer) RECONSTRUIT LE DOSSIER DU PROJET, puis fait le CONTROLE DE
//  COHERENCE : chaque fichier est la et intact (taille, empreinte), chaque
//  compte du manifeste est retrouve dans le projet relu. Un fichier externe lie
//  ailleurs que dans le projet est pose dans externes/ et le lien le suit - sauf
//  s'il est deja la, identique, a son chemin d'origine.
//
//  1.12.31 : LE CACHE DE CONSTRUCTION SUIT (.xpg/build/build-cache.txt) ; ses
//  artefacts sont refaits au deballage, d'apres lui (hmi::pipeline::restoreArtifacts) :
//  le projet rouvert reprend son build ou il en etait, sans rien regenerer.
//
//  IMPORTER (le volet Exporter / Importer) reconstruit l'IHM du projet ouvert
//  depuis une archive (.xihm, ou .zip de la 1.x) : une seule commande, Ctrl+Z
//  rend l'IHM d'avant. exportArchive ecrit l'IHM en memoire (sans le reste du
//  dossier) : les essais s'en servent.
// =============================================================================
#pragma once

#include "HmiHistory.hpp"
#include "HmiModel.hpp"
#include "HmiStore.hpp"
#include "../core/Result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace project::archive { struct Contents; }   // project/ProjectArchive.hpp

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
    // 1.12.30 : l'archive du projet entier.
    int                       format{0};    // 1 : le .zip de l'IHM (1.x) ; 2 : .xihm
    std::string               tool;         // l'application qui l'a ecrite, et sa version
    std::vector<std::string>  skipped;      // ce qu'elle n'a pas pris, et pourquoi
    std::vector<std::string>  notes;        // ce qui merite d'etre dit (un fichier externe relie...)
    std::string               folder;       // le dossier deballe
    // 1.12.31 : le cache de construction deballe (buildCache faux : l'archive n'en avait pas - une
    // archive d'avant, le build refera tout) ; ses artefacts refaits, et les elements qui ont change
    // depuis le build (un fichier externe relie a sa copie...) : ceux-la seront regeneres.
    bool                      buildCache{false};
    std::size_t               artifacts{0}, stale{0};
    [[nodiscard]] bool coherent() const noexcept { return problems.empty(); }
};

// Les comptes d'un projet (et de son historique), section par section : ce
// que le manifeste annonce et ce que le controle retrouve.
[[nodiscard]] std::vector<ArchiveCount> projectCounts(const Project&, const History*);

// ---- l'IHM en memoire (le volet Exporter / Importer, les essais) ------------
[[nodiscard]] core::Status exportArchive(const Project&, const History* history, const std::string& zipPath,
                                         ArchiveReport* report = nullptr);
// `history` recoit l'historique de l'archive (s'il y en a un). Une archive .xihm
// (le projet entier) se lit aussi : seule son IHM est reprise.
[[nodiscard]] core::Result<Project> importArchive(const std::string& zipPath, History* history = nullptr,
                                                  ArchiveReport* report = nullptr, LoadReport* load = nullptr);

// ---- 1.12.30 : le projet entier, .xihm ---------------------------------------
// 1.14.0 (relecture M4b) : LES ENREGISTREMENTS QU'UNE ARCHIVE JOINDRAIT (la case de l'export) - ceux du dossier natif
// et ceux du dossier de Configuration > Enregistrements (dans le projet ou ailleurs) : combien, leur poids, ou.
struct RecordingsShare {
    std::size_t   files{0};
    std::uint64_t bytes{0};
    std::string   folder;          // le dossier de la configuration ('' : le natif)
    bool          outside{false};  // hors du dossier du projet : l'archive les met dans le dossier natif
};
[[nodiscard]] RecordingsShare recordingsShare(const RecordingSettings&, const std::string& projectFolder);
struct ProjectArchiveOptions {
    bool        versions{true};       // versions/
    bool        history{true};        // ihm/historique/ : alarmes, mesures, audit, memoire des reglages PID
    bool        recordings{false};    // 1.14.0 (M4) : les enregistrements (la case "avec les enregistrements") - le dossier
                                      // natif et celui de Configuration > Enregistrements, dans le projet ou hors de lui
    std::string tool;                 // "IHM Studio 1.12.30"
};
// L'archive du dossier d'un projet (enregistre), en memoire ; puis ecrite.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> projectArchive(const std::string& projectFolder, const ProjectArchiveOptions&,
                                                                   ArchiveReport* report = nullptr);
[[nodiscard]] core::Status exportProjectArchive(const std::string& projectFolder, const std::string& path,
                                                const ProjectArchiveOptions&, ArchiveReport* report = nullptr);
// "" : l'archive s'ouvre dans IHM Studio ; sinon pourquoi pas (une version plus
// recente, l'archive d'un projet API, un projet IHM d'un format plus recent).
[[nodiscard]] std::string refusal(const project::archive::Contents&);
// Deballer dans un dossier neuf, relier les fichiers externes, puis le controle.
// `skipDamaged` : deballer ce qui est intact d'une archive abimee.
[[nodiscard]] core::Status unpackProjectArchive(const project::archive::Contents&, const std::string& folder,
                                                ArchiveReport* report = nullptr, bool skipDamaged = false);

} // namespace hmi
