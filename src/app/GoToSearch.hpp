// =============================================================================
//  app/GoToSearch.hpp - lot recherche : l'index d'Aller a... et sa recherche
// -----------------------------------------------------------------------------
//  L'ecran (screens/GoToWorkspace.cpp) remplit un Index avec tout ce qui a un
//  nom - une entree par chose : son titre, ce qui la decrit, du texte cherche
//  mais pas montre, la cle qui dit ou aller - et avec les TEXTES a chercher
//  ligne a ligne (le code des sections, les scripts). search() y cherche, par
//  categorie : les totaux de chacune, les meilleurs resultats de celles qu'on
//  montre (8 par categorie quand toutes le sont, 200 pour une seule).
//
//  Tout est plie une fois, a la construction (minuscules, sans accents) : une
//  frappe ne plie que ce qu'elle a tape. La recherche est ui::SearchQuery
//  (mots ET, "phrase", -exclu). Le classement : un titre qui COMMENCE par le
//  premier mot, puis un mot du titre qui commence par lui, puis le titre le
//  contient, puis le reste (le commentaire, le texte) ; les plus courts
//  d'abord. Les categories ou un titre commence par le premier mot passent
//  devant les autres, chacune a son rang de depart.
//
//  LES MEMBRES, par le chemin tape ("armoires[0].sorties.V") : ceux du noeud
//  deja ecrit (project/MemberTree) dont le nom contient la fin tapee.
// =============================================================================
#pragma once

#include "GoToPanel.hpp"

#include "../domain/ProjectModel.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app::gotosearch {

// Les categories, dans leur ordre de depart.
enum Group : int {
    GApiVar = 0, GMember, GHmiVar, GType, GUnit, GSection, GTask, GTable, GView, GObject, GAlarm, GRecipe, GUser,
    GResource, GStyle, GScript, GSysVar, GEquipment, GMacro, GVersion, GHelp, GCode, GPane, GAction, GCount
};
[[nodiscard]] const char* groupTitle(int group);     // "VARIABLES API"
[[nodiscard]] const char* groupChip(int group);      // "Variables API"

struct Entry {
    int         group{0};
    std::string title, subtitle, extra, key, hint;   // `extra` : cherche, pas montre
    ui::Icon    icon{ui::Icon::None};
    std::string folded;                              // titre, sous-titre, extra : plies, separes par un saut de ligne
    std::size_t titleEnd{0};                         // la fin du titre dans `folded`
};

// Un texte cherche ligne a ligne : le code d'une section, un script, une fonction.
struct Text {
    int         group{0};
    std::string name;                                // ce que dit le resultat : "Gestion_reports"
    std::string key;                                 // la cle, sans la ligne : ":<ligne>" s'y ajoute
    std::string body, folded;                        // l'original, et plie
    std::vector<std::size_t> lines, foldedLines;     // le debut de chaque ligne
    ui::Icon    icon{ui::Icon::Code};
};

struct Index {
    std::shared_ptr<const domain::Project> plc;      // les membres se deplient dedans
    std::vector<Entry> entries;                      // ce qui suit le projet
    std::vector<Entry> library;                      // ce qui ne le suit pas (macros, aide)
    std::vector<Text>  texts;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> globals;   // nom en minuscules -> (nom, type), trie

    static void add(std::vector<Entry>& list, int group, std::string title, std::string subtitle, std::string key, ui::Icon icon,
                    std::string extra = {}, std::string hint = {});
    void addText(int group, std::string name, std::string key, const std::string& body, ui::Icon icon);
    void addGlobal(const std::string& name, const std::string& type);
    void finish();                                   // apres les addGlobal : les racines triees
    void clearProject();                             // tout sauf `library`
};

// `group` : la categorie montree (-1 : toutes).
[[nodiscard]] GoToPanel::Outcome search(const Index& ix, const std::string& text, int group);

} // namespace app::gotosearch
