// =============================================================================
//  project/MacroMemory.hpp - ce qu'on retient d'un lancement a l'autre
//  (lot macros 1)
// -----------------------------------------------------------------------------
//  RELANCER UNE MACRO, C'ETAIT TOUT RETAPER : le classeur, la tache, le
//  prefixe, les quinze reponses. On retient donc :
//
//    - les REPONSES du dernier lancement, par projet et par macro : le
//      formulaire les reprend et le dit ("comme la derniere fois") ;
//    - les PROFILS : un jeu de reponses garde sous un nom ("Affaire
//      2024_06_264"), par macro, pour tous les projets ;
//    - les FICHIERS RECENTS (les huit derniers), proposes sous le champ ;
//    - les FAVORITES et les DERNIERS LANCEMENTS, pour l'onglet Macros.
//
//  UN FICHIER A PART, A COTE DES REGLAGES (macros-memoire.txt) : ce ne sont
//  pas des reglages de l'application, et settings.txt tient une valeur par
//  ligne, sans retour a la ligne possible. Une ligne par fait, des tabulations
//  entre les champs ; \t, \n et \\ sont echappes.
// =============================================================================
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace project::macro {

class MacroMemory {
public:
    using Answers = std::map<std::string, std::string>;

    MacroMemory() = default;
    explicit MacroMemory(std::string path) : path_(std::move(path)) {}

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    bool load();                  // absent : vide, et vrai
    bool save() const;
    [[nodiscard]] std::string render() const;
    void parse(std::string_view text);

    // ---- les reponses du dernier lancement ---------------------------------------
    [[nodiscard]] Answers answers(std::string_view project, std::string_view macro) const;
    void remember(std::string_view project, std::string_view macro, const Answers& answers);

    // ---- les profils ---------------------------------------------------------------
    [[nodiscard]] std::vector<std::string> profiles(std::string_view macro) const;
    [[nodiscard]] Answers profile(std::string_view macro, std::string_view name) const;
    void saveProfile(std::string_view macro, std::string_view name, const Answers& answers);
    void removeProfile(std::string_view macro, std::string_view name);

    // ---- les fichiers recents (le plus recent d'abord, huit au plus) --------------
    [[nodiscard]] std::vector<std::string> recentFiles() const { return files_; }
    void touchFile(std::string_view path);

    // ---- favorites et lancements -------------------------------------------------
    [[nodiscard]] bool favourite(std::string_view macro) const;
    void setFavourite(std::string_view macro, bool on);
    [[nodiscard]] std::vector<std::string> favourites() const { return favourites_; }
    struct Run {
        std::string macro, when, summary;
    };
    // Le plus recent d'abord, vingt au plus.
    [[nodiscard]] const std::vector<Run>& runs() const noexcept { return runs_; }
    void noteRun(std::string_view macro, std::string_view when, std::string_view summary);
    [[nodiscard]] const Run* lastRun(std::string_view macro) const;
    // Une macro renommee ou supprimee : ce qu'on en retenait la suit.
    void renameMacro(std::string_view from, std::string_view to);

private:
    struct Stored {
        std::string owner, macro, key, value;   // owner : le projet (reponse) ou le nom du profil
    };
    std::string          path_;
    std::vector<Stored>  answers_;
    std::vector<Stored>  profiles_;
    std::vector<std::string> files_;
    std::vector<std::string> favourites_;
    std::vector<Run>     runs_;
};

} // namespace project::macro
