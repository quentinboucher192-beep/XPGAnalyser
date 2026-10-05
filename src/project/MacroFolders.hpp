// =============================================================================
//  project/MacroFolders.hpp - les dossiers des macros (lot macros 1)
// -----------------------------------------------------------------------------
//  COMME LES FONCTIONS IHM DANS L'IHM, LES MACROS SE RANGENT EN DOSSIERS. Un
//  dossier est un chemin ("Importer depuis le classeur/Etapes") SANS EFFET SUR
//  LES NOMS : RunMacro('ImporterVoies') marche ou que la macro soit rangee, et
//  le fichier reste libs/Macros/ImporterVoies.mac. C'est un rangement, comme
//  les filtres de Visual Studio.
//
//  LE RANGEMENT VIT DANS libs/Macros/dossiers.txt, a cote des macros : il se
//  partage avec elles. Deux sortes de lignes :
//
//      dossier ; Importer depuis le classeur          un dossier, meme vide
//      macro ; ImporterClasseur ; Importer depuis...  ou est rangee une macro
//
//  L'ordre des lignes est l'ordre d'affichage. UNE MACRO ABSENTE DU FICHIER va
//  dans le dossier de sa ligne "#! categorie" - une macro livree plus tard se
//  range donc toute seule - et, sans categorie, a la racine.
//
//  Le premier geste (ranger, renommer, creer un dossier) ECRIT TOUT le
//  rangement dans le fichier : ce qu'on voyait devient ce qui est ecrit, et la
//  suite ne depend plus des categories.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace project::macro {

// Le rangement tel qu'il se montre : les dossiers et les macros, dans l'ordre.
struct FolderLayout {
    struct Entry {
        std::string name;
        std::string folder;      // "" : a la racine
    };
    std::vector<std::string> folders;   // tous, parents avant enfants, dans l'ordre d'affichage
    std::vector<Entry>       macros;    // dans l'ordre d'affichage

    // Les sous-dossiers directs de `parent` ("" : la racine), dans l'ordre.
    [[nodiscard]] std::vector<std::string> subfolders(std::string_view parent) const;
    // Les macros rangees directement dans `folder`, dans l'ordre (indices dans `macros`).
    [[nodiscard]] std::vector<std::size_t> macrosIn(std::string_view folder) const;
    // Combien de macros dans `folder` (et ses sous-dossiers si `deep`).
    [[nodiscard]] std::size_t countIn(std::string_view folder, bool deep) const;
    [[nodiscard]] std::string folderOf(std::string_view macro) const;
    [[nodiscard]] bool hasFolder(std::string_view path) const;
};

// Le dernier segment ("A/B" -> "B") et le parent ("A/B" -> "A", "A" -> "").
[[nodiscard]] std::string folderLeaf(std::string_view path);
[[nodiscard]] std::string folderParent(std::string_view path);
// Le meme dossier (sans casse ni accents) ; `inside` : lui ou un de ses sous-dossiers.
[[nodiscard]] bool sameFolder(std::string_view a, std::string_view b);
[[nodiscard]] bool insideFolder(std::string_view folder, std::string_view parent);
// "  A /B/ " -> "A/B" ; vide si un segment est vide ("A//B").
[[nodiscard]] std::string cleanFolder(std::string_view path);

class MacroFolders {
public:
    MacroFolders() = default;
    explicit MacroFolders(std::string file) : file_(std::move(file)) {}

    [[nodiscard]] const std::string& file() const noexcept { return file_; }
    // Absent : aucun rangement ecrit, et ce n'est pas une erreur.
    bool load();
    [[nodiscard]] core::Status save() const;
    // Le texte du fichier (ce que save() ecrit).
    [[nodiscard]] std::string render() const;
    void parse(std::string_view text);

    // Les macros connues : leur nom et leur "#! categorie". A donner avant
    // arrange() et avant tout geste.
    void setMacros(std::vector<std::pair<std::string, std::string>> nameAndCategory);
    [[nodiscard]] FolderLayout arrange() const;

    // ---- les gestes (faux et `why` : refuse) ------------------------------------
    bool addFolder(const std::string& path, std::string* why = nullptr);
    // Ses macros et ses sous-dossiers suivent.
    bool renameFolder(const std::string& from, const std::string& to, std::string* why = nullptr);
    // Ses macros et ses sous-dossiers montent d'un cran.
    bool removeFolder(const std::string& path, std::string* why = nullptr);
    // Un dossier (et ce qu'il contient) dans un autre ("" : a la racine).
    bool moveFolder(const std::string& path, const std::string& parent, std::string* why = nullptr);
    // Rend combien de macros ont change de dossier.
    std::size_t moveMacros(const std::vector<std::string>& names, const std::string& folder);
    // Reordonner : `names` juste avant (ou apres) `anchor`, dans son dossier.
    bool placeNear(const std::vector<std::string>& names, const std::string& anchor, bool after);
    // Une macro creee : rangee dans `folder`, a la fin.
    void place(const std::string& name, const std::string& folder);
    void forget(const std::string& name);
    void renameMacro(const std::string& from, const std::string& to);

    // Pour les tests : ce qui est ecrit.
    [[nodiscard]] const std::vector<std::string>& explicitFolders() const noexcept { return folders_; }
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& placements() const noexcept { return placements_; }

private:
    // Ecrit dans les listes tout ce que l'arrangement montre.
    void materialize();
    [[nodiscard]] int placementOf(std::string_view name) const;

    std::string file_;
    std::vector<std::string> folders_;                              // dossier ; chemin
    std::vector<std::pair<std::string, std::string>> placements_;  // macro ; nom ; dossier
    std::vector<std::pair<std::string, std::string>> macros_;      // nom, categorie
};

} // namespace project::macro
