// =============================================================================
//  project/MacroSpecWriter.hpp - ecrire les lignes "#!" d'une macro (lot API 6)
// -----------------------------------------------------------------------------
//  MacroSpec LIT l'en-tete ; ce fichier l'ECRIT. L'editeur des macros montre les
//  questions en cartes : deplacer une carte, changer un libelle, ajouter une
//  question reecrit ici les lignes qui les disent - et RIEN D'AUTRE. Une ligne
//  dont la valeur ne change pas reste a l'octet pres (son retrait, ses espaces,
//  sa fin de ligne) : relue puis reecrite sans changement, une macro est
//  identique (macrospec_write_test, sur les 31 de libs/).
//
//  Les lignes de l'en-tete et le code sont le meme texte : une ligne "#!" tapee
//  a la main dans le code cree sa carte au prochain parseMacroSpec.
// =============================================================================
#pragma once

#include "MacroSpec.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace project::macro {

// Une ligne "#!" de l'en-tete : son numero (0...), son mot ("champ"), sa cle
// ("tache", vide pour "#! version"), sa valeur.
struct HeaderLine {
    std::size_t line{0};
    std::string word, key, value;
};
[[nodiscard]] std::vector<HeaderLine> headerLines(std::string_view source);

// Les lignes d'une question (0...) : ses "#! champ", "#! libelle", "#! param",
// "#! exemple", "#! option", les "#! groupe" et "#! avance" qui la citent, et
// l'appel Ask du code.
[[nodiscard]] std::vector<std::size_t> linesOf(std::string_view source, const std::string& key);

// Les groupes et les reglages avances tels qu'on les veut : les lignes "#!
// groupe" et "#! avance" sont reecrites (ajoutees, retirees au besoin). A
// l'identique si rien ne change.
[[nodiscard]] std::string writeGroups(std::string_view source, const std::vector<GroupSpec>& groups,
                                      const std::vector<std::string>& advanced);

// Deplacer une question : dans `group` ("" : sans groupe), a la position `index`
// de ce groupe (au-dela : a la fin).
[[nodiscard]] std::string moveField(std::string_view source, const std::string& key, const std::string& group, std::size_t index);
[[nodiscard]] std::string setAdvanced(std::string_view source, const std::string& key, bool advanced);

// Une ligne "#! <mot> <cle> = <valeur>" : remplacee, ajoutee apres la derniere
// ligne de la meme question (ou des "#! champ"), retiree si `value` est vide.
[[nodiscard]] std::string setKeyLine(std::string_view source, const std::string& word, const std::string& key, const std::string& value);
// L'aide : la PREMIERE ligne "#! param" de la cle (les suivantes restent).
[[nodiscard]] std::string setHelp(std::string_view source, const std::string& key, const std::string& help);

// Une question nouvelle : "#! champ" et "#! libelle" apres les autres, et
// "cle := Ask('cle', 'Libelle', 'defaut');" apres le dernier Ask du code.
[[nodiscard]] std::string addField(std::string_view source, const std::string& key, const std::string& kind,
                                   const std::string& label, const std::string& preset);

// Enregistrer : "#! version = v" et "#! changes v = texte" (la plus recente en tete).
[[nodiscard]] std::string bumpVersion(std::string_view source, const std::string& version, const std::string& changes);
// "1.10" -> "1.11", "1.9" -> "1.10", "2" -> "2.1", "" -> "1.0".
[[nodiscard]] std::string nextVersion(std::string_view version);

// Un libelle en litteral ST (ASCII : accents retires, apostrophe en $').
[[nodiscard]] std::string stLiteral(std::string_view text);

} // namespace project::macro
