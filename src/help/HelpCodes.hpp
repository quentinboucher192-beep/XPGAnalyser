// =============================================================================
//  help/HelpCodes.hpp — un code par diagnostic, et une page derriere chaque code
// -----------------------------------------------------------------------------
//  POURQUOI.
//
//  Un message de diagnostic doit tenir sur une ligne : il s'affiche dans un
//  tableau, a cote de vingt autres. Mais « la voie 15 n'est jamais traitee » ne
//  se comprend qu'avec trois paragraphes de contexte, et ces paragraphes n'ont
//  pas leur place dans la ligne.
//
//  Chaque diagnostic porte donc un CODE - `XPG-2101` - et le code a sa page.
//  La ligne reste courte, le double-clic ouvre l'explication. C'est aussi ce
//  qui rend un message cherchable : « XPG-2101 » est un terme exact, « la
//  section n'est pas en ST » ne l'est pas.
//
//  LE PLAN DE NUMEROTATION, par domaine, pour qu'un code dise d'ou il vient
//  avant meme d'etre cherche :
//
//      XPG-0xxx   ouvrir un fichier, un projet, un export
//      XPG-1xxx   le modele : types, variables, sections
//      XPG-2xxx   le simulateur
//      XPG-3xxx   les macros
//      XPG-4xxx   la bibliotheque partagee
//      XPG-5xxx   le classeur .xlsm
//      XPG-6xxx   l'ordre d'execution
//
//  UN CODE NE SE REUTILISE JAMAIS. Un diagnostic supprime laisse son numero
//  vide : quelqu'un a une capture d'ecran, un courriel, un compte rendu de mise
//  en service ou ce numero apparait, et le faire designer autre chose est pire
//  que de le laisser mort.
//
//  CE FICHIER NE DEPEND QUE DE ui/HelpArticleView. Il transforme un code en
//  article ; c'est le meme widget qui affiche l'aide d'un bloc, donc il n'y a
//  pas deux mises en page a tenir.
// =============================================================================
#pragma once

#include "../ui/widgets/HelpArticleView.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace help {

enum class Severity : std::uint8_t { Info, Warning, Error };

struct Code {
    std::string_view id;         // "XPG-2101"
    Severity         severity{Severity::Warning};
    std::string_view title;      // une ligne, ce qui s'affiche a cote du code
    std::string_view cause;      // pourquoi ca arrive
    std::string_view effect;     // ce que ca change au programme, concretement
    std::vector<std::string_view> fixes;   // quoi faire, dans l'ordre a essayer
    std::vector<std::string_view> see;     // autres codes, blocs, macros
};

// Toutes les pages, dans l'ordre des numeros. C'est aussi la table des
// matieres de l'onglet "Diagnostics" de l'aide.
[[nodiscard]] const std::vector<Code>& allCodes();

// La page d'un code, ou nullptr. La comparaison est exacte et insensible a la
// casse : « xpg-2101 » tape a la main doit marcher.
[[nodiscard]] const Code* find(std::string_view id);

// LE CODE D'UN MESSAGE DEJA EMIS.
//
// Les diagnostics existants sont du texte libre, ecrit avant qu'il y ait des
// codes. Plutot que de reecrire cinquante `push_back` - et de casser tout ce
// qui les compare - on reconnait le message ici. Rend une chaine vide quand le
// message n'a pas encore de page : c'est une information, pas une erreur, et
// l'interface affiche alors le message seul comme avant.
[[nodiscard]] std::string_view codeFor(std::string_view message);

// Le message, prefixe de son code quand il en a un : « XPG-2101  la section... ».
// C'est ce que le tableau des diagnostics affiche.
[[nodiscard]] std::string withCode(std::string_view message);

// La page, mise en page pour HelpArticleView.
[[nodiscard]] ui::HelpArticle article(const Code&);

// La table des matieres : une ligne par code, pour la liste de gauche.
[[nodiscard]] ui::HelpArticle indexArticle();

} // namespace help
