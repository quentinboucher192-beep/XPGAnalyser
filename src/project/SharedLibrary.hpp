// =============================================================================
//  project/SharedLibrary.hpp - DDTs and DFBs shared between projects
// -----------------------------------------------------------------------------
//  A folder, with categories, exactly as asked:
//
//    libs/
//      index.txt                 every entry, its category and its version
//      Sensors/
//        CAPTEUR.dfb             interface, attributes and body
//        ST_Cabinet.ddt          fields
//      Sequencing/
//        DFB_GRAFCETENGINE.dfb
//
//  VERSIONS ARE NOTIFIED, NEVER APPLIED. A project keeps the copy it imported
//  until somebody decides otherwise, because a machine in service must not
//  change because a colleague edited a block. `outdated()` lists what has moved
//  on; importing then offers to overwrite or to bring the new one in beside the
//  old one under a different name.
//
//  The format is the project format's own: a .dfb here is the same text as
//  dfb/<Name>/interface.txt plus its bodies, so nothing new has to be learned
//  and a library entry can be read in a text editor.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"

#include <memory>
#include <string>
#include <vector>

namespace project {

    // A macro is a third kind, and it is not a fourth variation of the first two: a
    // DDT and a DFB are DEFINITIONS that get copied into the project, while a macro
    // is a SCRIPT that stays in the library and is run against the project. Sharing
    // the index, the categories and the version comparison is worth it; sharing
    // import() is not, and macroSource() exists instead.
    enum class LibraryItemKind : std::uint8_t { DerivedType, FunctionBlock, Macro };

    struct LibraryItem {
        LibraryItemKind kind{ LibraryItemKind::DerivedType };
        std::string     name;
        std::string     category;     // a folder under libs/
        std::string     version;
        std::string     author;
        std::string     comment;
        std::string     path;         // where it lives, filled by scan()

        [[nodiscard]] std::string fileName() const;
    };

    // What a project holds against what the library now offers.
    struct VersionNotice {
        std::string name;
        std::string projectVersion;
        std::string libraryVersion;
        LibraryItemKind kind{ LibraryItemKind::DerivedType };
    };

    class SharedLibrary {
    public:
        explicit SharedLibrary(std::string root);

        [[nodiscard]] const std::string& root() const noexcept { return root_; }

        // ---- reading --------------------------------------------------------
        [[nodiscard]] core::Status scan();
        [[nodiscard]] const std::vector<LibraryItem>& items() const noexcept { return items_; }
        [[nodiscard]] std::vector<std::string> categories() const;
        [[nodiscard]] const LibraryItem* find(std::string_view name) const;

        // ---- publishing -------------------------------------------------------
        // "Define as generic": copies the type or block out of `project` into the
        // library under `category`. Refuses to overwrite a different version unless
        // `replace` is set, so publishing twice by accident cannot lose work.
        [[nodiscard]] core::Status publishDerivedType(const domain::Project&, std::string_view name,
            std::string category, bool replace = false);
        // The text of a macro, ready to run. Empty when the item is not a macro or
        // cannot be read.
        [[nodiscard]] std::string macroSource(std::string_view name) const;

        // Writes a macro into the library, creating or replacing it.
        //
        // Lot macros 1 : sans version donnee, c'est celle de la ligne "#! version"
        // de la macro (sinon celle de l'index, sinon 1.00). Enregistrer depuis
        // l'editeur remettait l'index a 1.00 alors que le fichier disait 2.01.
        [[nodiscard]] core::Status publishMacro(std::string_view name, std::string_view source,
            std::string category = {},
            std::string version = {},
            std::string comment = {});

        // ---- lot macros 1 : creer, supprimer, renommer, dupliquer -----------------
        //
        //  LA CORBEILLE. Supprimer une macro ne l'efface pas : son fichier va dans
        //  libs/Macros/_corbeille, avec une ligne dans _corbeille/corbeille.txt
        //  (son dossier, sa version, la date), et Restaurer la remet a sa place.
        //  Une macro livree (EnsureSection...) qu'on a supprimee ou renommee n'est
        //  pas recreee par ensureDefaultMacros : c'etait une decision.
        //
        //  RENOMMER CORRIGE LES APPELANTS : RunMacro('Ancien') devient
        //  RunMacro('Nouveau') dans toutes les macros de libs/, et le resultat dit
        //  lesquelles. Une chaine d'import cassee par un renommage ne se verrait
        //  qu'a l'execution.
        [[nodiscard]] std::string macrosFolder() const;          // <root>/Macros
        [[nodiscard]] std::string macroFoldersFile() const;      // <root>/Macros/dossiers.txt
        struct TrashedMacro {
            std::string name, file, date, folder, version, comment;
        };
        // La plus recente d'abord.
        [[nodiscard]] std::vector<TrashedMacro> trashedMacros() const;
        [[nodiscard]] core::Status createMacro(std::string_view name, std::string_view source,
                                               const std::string& folder = {});
        [[nodiscard]] core::Status deleteMacro(std::string_view name);
        [[nodiscard]] core::Status restoreMacro(std::string_view name);
        [[nodiscard]] core::Status purgeMacro(std::string_view name);
        // Rend les macros dont un RunMacro a ete corrige.
        [[nodiscard]] core::Result<std::vector<std::string>> renameMacro(std::string_view from,
                                                                        std::string_view to);
        [[nodiscard]] core::Status duplicateMacro(std::string_view from, std::string_view to);
        // Le dossier ou une macro est rangee (dossiers.txt, sinon sa categorie).
        [[nodiscard]] std::string macroFolderOf(std::string_view name) const;
        // "(* Ancien" -> "(* Nouveau" : le premier mot de l'en-tete, s'il etait l'ancien nom.
        [[nodiscard]] static std::string renameInHeader(std::string_view source, std::string_view from,
                                                        std::string_view to);
        // RunMacro('from') -> RunMacro('to'), casse du nom ignoree. Rend combien.
        [[nodiscard]] static std::size_t replaceRunMacro(std::string& source, std::string_view from,
                                                         std::string_view to);

        // Writes the sub-macros this program ships with, and ONLY the ones that are
        // missing.
        //
        // Never overwrites. A file you corrected is a file you corrected, and a new
        // release quietly putting its own version back would be the single most
        // destructive thing this class could do. An existing file whose version is
        // older is reported by outdated() like anything else, and left alone.
        [[nodiscard]] core::Status ensureDefaultMacros();

        // What ensureDefaultMacros would write. Exposed so a test can check that
        // every one of them parses.
        struct DefaultMacro {
            std::string name, category, version, comment, source;
        };
        [[nodiscard]] static const std::vector<DefaultMacro>& defaultMacros();

        [[nodiscard]] core::Status publishFunctionBlock(const domain::Project&, std::string_view name,
            std::string category, bool replace = false);

        // ---- importing ---------------------------------------------------------
        enum class OnConflict : std::uint8_t {
            Refuse,      // stop and say the name is taken
            Overwrite,   // replace the project's copy
            Duplicate,   // bring it in as <Name>_v<version>
        };
        struct ImportOutcome {
            std::string importedAs;
            bool        replacedExisting{ false };
        };
        [[nodiscard]] core::Result<ImportOutcome> import(domain::Project&, std::string_view name,
            OnConflict = OnConflict::Refuse) const;

        // ---- version notices ----------------------------------------------------
        // Everything the project holds that the library has moved past. This is the
        // list the menu shows; nothing is changed by asking.
        [[nodiscard]] std::vector<VersionNotice> outdated(const domain::Project&) const;

        [[nodiscard]] static std::string defaultRoot();
        // 1.8.0 : la version installee range la bibliotheque ou dit XPGAnalyser.ini
        // (app/Dossiers.hpp) ; vide (le defaut) : libs\ du dossier de travail, comme avant.
        static void setDefaultRoot(std::string root);

    private:
        std::string              root_;
        std::vector<LibraryItem> items_;
    };

} // namespace project