// =============================================================================
//  help/HelpIndex.hpp — chercher, relier, situer
// -----------------------------------------------------------------------------
//  Quatre choses qui ont l'air separees et qui sont la meme : savoir ce que
//  l'aide contient.
//
//    CHERCHER        dans tout - resume, usage, parametres, codes de defaut,
//                    questions de macro - et CLASSER : un nom exact d'abord,
//                    un parametre ensuite, une phrase apres. Une recherche qui
//                    rend trente lignes dans le desordre ne repond pas.
//
//    RELIER DANS LES DEUX SENS. `#! see` est ecrit a la main dans un fichier ;
//    la page de ST_IO_Dig devrait lister les dix DFB_IO_DIG* qui la citent,
//    sans qu'on ait a l'ecrire dix fois. Ca se calcule.
//
//    LE GLOSSAIRE. TC, TM, TA, DDT, EBOOL, %MW, front montant. Un integrateur
//    qui arrive sur le projet n'a pas ce vocabulaire, et il ne le demandera pas.
//
//    SITUER (F1). Un nom sous le curseur, un noeud d'arbre, une ligne de
//    diagnostic : tous designent une page. C'est une fonction pure, donc elle
//    se verifie sans ecran.
// =============================================================================
#pragma once

#include "../project/LibraryCatalog.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace help {

// ------------------------------------------------------------- la recherche --
enum class HitKind : std::uint8_t {
    Name,        // le nom de l'element : ST_EQ_Pump
    Param,       // un parametre : AlarmDelayMs
    Fault,       // un code de defaut du bloc
    Question,    // une question de macro
    Summary,     // le resume
    Usage,       // le corps de l'aide
    Example,
    Diagnostic,  // un code XPG-nnnn
    Glossary,
};

struct Hit {
    HitKind     kind{HitKind::Name};
    std::string entry;      // l'element : "ST_EQ_Pump", ou le code "XPG-2101"
    std::string subject;    // le parametre, le code de defaut, la question
    std::string excerpt;    // le passage, coupe autour du terme
    int         score{0};   // plus grand = plus pertinent
};

// Le terme est cherche sans se soucier de la casse ni des accents. `limit` a 0
// veut dire tout.
[[nodiscard]] std::vector<Hit> search(const std::vector<project::CatalogEntry>& library,
                                      std::string_view term, std::size_t limit = 60);

// ----------------------------------------------------------- les renvois -----
// Les elements qui citent celui-ci dans leur `#! see`, ou qui l'utilisent comme
// type d'un parametre. Le premier est declaratif, le second est structurel, et
// les deux repondent a « qui depend de moi ».
struct Backlink {
    std::string name;
    std::string why;     // "cite dans Voir aussi" / "parametre Chan"
};
[[nodiscard]] std::vector<Backlink> backlinks(const std::vector<project::CatalogEntry>& library,
                                              std::string_view name);

// LES NOMS D'UN `#! see`, UN PAR UN.
//
// Un `#! see` porte une LIGNE, pas un nom : « see = ST_IO_Dig, DFB_IO_DIG04 ».
// Comparer la ligne entiere a un nom ne rend donc jamais vrai des qu'un fichier
// en cite deux - ce que fait la moitie de la bibliotheque - et le renvoi
// declaratif disparait sans bruit. La decoupe est ici, en un seul endroit,
// parce que trois appelants qui la refont chacun la referont differemment.
[[nodiscard]] std::vector<std::string> seeNames(const project::LibraryHelp&);

// ----------------------------------------------------------- le glossaire ----
struct Term {
    std::string_view word;
    std::string_view short_;   // une phrase, pour le survol
    std::string_view long_;    // le paragraphe, pour la page
};
[[nodiscard]] const std::vector<Term>& glossary();
[[nodiscard]] const Term* glossaryTerm(std::string_view word);

// Les termes du glossaire presents dans ce texte, avec leur position. C'est ce
// qui permet de les souligner dans un article sans les marquer a la main.
struct Mention { std::size_t at{0}; std::size_t length{0}; const Term* term{nullptr}; };
[[nodiscard]] std::vector<Mention> mentions(std::string_view text);

// ------------------------------------------------------ situer : F1 ----------
// Topic (1.11, chantier T2) : un sujet du centre d'aide unique ; entry = sa cle
// dans help::center::Index ("objet-vanne", "bloc-ST_EQ_Pump", "page-raccourcis"),
// ecrite "sujet:<cle>" dans les reglages (recents, favoris).
enum class TargetKind : std::uint8_t { None, LibraryEntry, Parameter, Diagnostic, Glossary, Topic };

struct Target {
    TargetKind  kind{TargetKind::None};
    std::string entry;      // le bloc, le code, le mot
    std::string subject;    // le parametre, quand il y en a un
};

// Depuis un mot : ce que F1 doit ouvrir. L'ordre des essais est le sujet - un
// nom d'element avant un parametre, un parametre avant un mot du glossaire -
// parce que `Val` est les trois a la fois.
[[nodiscard]] Target targetForWord(const std::vector<project::CatalogEntry>& library,
                                   std::string_view word);

// Le mot sous le curseur dans une ligne de ST. `%MW1000` et `Pompes[0].Fbk`
// comptent chacun pour un mot : couper sur les espaces donnerait `Pompes[0].Fbk`
// entier, couper sur tout donnerait `Pompes`.
[[nodiscard]] std::string wordAt(std::string_view line, std::size_t column);

// Depuis un message de diagnostic.
[[nodiscard]] Target targetForDiagnostic(std::string_view message);

// -------------------------------------------- la version du projet -----------
// Ce que le bandeau d'un article doit dire quand le projet et la bibliotheque
// ne sont pas d'accord. Chaine vide : ils le sont, pas de bandeau.
[[nodiscard]] std::string versionNotice(std::string_view entryName,
                                        std::string_view libraryVersion,
                                        std::string_view projectVersion);

} // namespace help
