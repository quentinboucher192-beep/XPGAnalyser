// =============================================================================
//  project/ProjectStore.hpp — a project is a folder, not a blob
// -----------------------------------------------------------------------------
//  LAYOUT
//
//    MyProject/
//      project.xpgproj          manifest: name, version, state, CPU, timestamps
//      project.lock             salt + hashes, present only in the LOCK state
//      config/hardware.txt      racks, modules, channels, data-memory sizing
//      vars/globals.txt         the global dictionary
//      ddt/<Name>.ddt           one file per derived type
//      dfb/<Name>/interface.txt   parameters, locals, attributes
//      dfb/<Name>/code/<Sec>.st   one file per body
//      units/<Name>/interface.txt
//      units/<Name>/code/<Sec>.st
//      sections/index.txt       task, order, activation condition per section
//      sections/<Name>.st       the task sections themselves
//      src/MAST.XPG             generated on export
//      src/CONFIG.XHW           generated on export
//
//  1.12.0 : DEUX APPLICATIONS. Dans XPGAnalyser IHM (core::hasApi() faux), un
//  projet n'est que son manifeste (edition = ihm), son verrou et ihm/ (ecrit par
//  hmi::save) : open() ne lit pas le programme (un modele vide, au nom du projet),
//  save() n'ecrit que le manifeste et le verrou - jamais config/, vars/... (un
//  dossier de la 1.11 garde son programme intact).
//
//  WHY A FOLDER
//
//  Code lives in .st files, one per section. That means an engineer can open a
//  section in any editor, a diff of two versions of a project is readable, and
//  a version-control system can merge two people's work on different sections
//  instead of conflicting over one large document. The declarations are
//  semicolon tables for the same reason.
//
//  A single-file container would be tidier to copy and worse at everything else.
//  Everything under src/ is generated: deleting it loses nothing.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"
#include "Security.hpp"

#include <memory>
#include <string>
#include <vector>

namespace project {

    struct Manifest {
        std::string name;
        std::string version{ "0.0.1" };
        State       state{ State::New };
        std::string created;
        std::string modified;
        std::string author;
        std::string cpuReference;
        std::string cpuFirmware;
        std::string comment;
        std::string formatVersion{ "1" };
        // Carried so a re-export reproduces the header of the file the project came
        // from, rather than claiming this tool authored it.
        std::string company{ "Schneider Automation" };
        std::string product;
        std::string dtdVersion{ "41" };
        // Ce que le fichier d'origine disait, et qu'un dossier doit rendre tel quel.
        // Sans ces trois-la, un projet range en dossier puis exporte ne produit plus
        // les memes octets qu'un export direct - et la comparaison avec le fichier
        // d'origine, qui est tout l'interet, devient impossible.
        bool        crlf{ true };
        std::string contentDateTime;
        std::string contentKind;
        // 1.12.0 : l'application du projet - "api" (XPGAnalyser API : le programme),
        // "ihm" (XPGAnalyser IHM : ihm/ seule) ; vide : un projet de la 1.11, qui a
        // les deux (chaque application n'y lit et n'y ecrit que sa moitie).
        std::string edition;
    };

    struct OpenResult {
        std::shared_ptr<domain::Project> project;
        Manifest                         manifest;
        LockRecord                       lock;
        bool                             needsPassword{ false };
    };

    class ProjectStore {
    public:
        // ---- creation ---------------------------------------------------------
        // An empty project with one MAST task and nothing else, ready to be filled.
        [[nodiscard]] static std::shared_ptr<domain::Project> createEmpty(std::string name,
            std::string cpuReference);

        // ---- folder round trip -------------------------------------------------
        [[nodiscard]] static core::Status save(const domain::Project&, const Manifest&,
            const std::string& folder,
            const LockRecord & = {});

        // Reads the manifest first. A locked project comes back with needsPassword
        // set and no model, so the caller asks before anything is parsed.
        [[nodiscard]] static core::Result<OpenResult> open(const std::string& folder);
        [[nodiscard]] static core::Result<OpenResult> open(const std::string& folder,
            std::string_view password,
            std::string_view masterKey = {});

        // ---- operations --------------------------------------------------------
        [[nodiscard]] static core::Status duplicate(const std::string& from, const std::string& to,
            std::string newName);

        // Writes src/MAST.XPG and, when a hardware configuration exists,
        // src/CONFIG.XHW. Returns the paths written.
        [[nodiscard]] static core::Result<std::vector<std::string>>
            exportSources(const domain::Project&, const std::string& folder);

        [[nodiscard]] static core::Result<Manifest> readManifest(const std::string& folder);
        [[nodiscard]] static bool                   isProjectFolder(const std::string& folder);
    };

} // namespace project