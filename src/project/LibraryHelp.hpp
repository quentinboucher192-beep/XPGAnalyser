// =============================================================================
//  project/LibraryHelp.hpp — l'aide d'un DDT ou d'un DFB, dans son propre fichier
// -----------------------------------------------------------------------------
//  L'AIDE VIT DANS LE .ddt, PAS A COTE.
//
//  Une documentation rangee ailleurs - un fichier d'aide, une table, un wiki -
//  se separe de ce qu'elle decrit des la premiere modification faite dans
//  l'urgence. Celle-ci est dans le fichier du bloc : on ne peut pas copier le
//  bloc sans elle, ni le modifier sans l'avoir sous les yeux.
//
//  ELLE EST ECRITE DANS DES COMMENTAIRES, et ce n'est pas un detail.
//
//  Le format d'un .ddt est deja fixe et deja lu par trois choses : le parseur
//  C++, la macro INIT du classeur Excel, et l'oeil de celui qui l'ouvre dans un
//  editeur. Une ligne d'aide doit donc etre invisible pour les deux premiers.
//
//  Un lecteur de .ddt teste les lignes dans cet ordre :
//
//      "name " en tete        -> le nom de la famille
//      "#" en tete, ou vide   -> commentaire, ignore
//      "<<<" en tete          -> le corps ST commence, on arrete les declarations
//      contient un ";"        -> un parametre
//
//  Le test du "#" passe AVANT celui du point-virgule. Une aide prefixee par "#"
//  est donc ignoree par construction, y compris quand son texte contient un
//  point-virgule - ce qu'une prose francaise fait sans prevenir, et ce qui
//  ferait lire "Attention ; voir la notice" comme un parametre nomme
//  "Attention".
//
//  Le marqueur est "#!" : un commentaire pour tout le monde, une donnee pour
//  nous. Un "#" seul reste un commentaire ordinaire, et le reste tel quel.
//
//  CE FICHIER NE DEPEND DE RIEN. Ni du modele de projet, ni de l'interface : il
//  transforme du texte en aide et de l'aide en texte. C'est ce qui le rend
//  verifiable sans ecran et sans projet charge.
// =============================================================================
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace project {

// Une entree d'aide attachee a un parametre, un code de defaut, ou l'item
// lui-meme. La cle porte le sujet ("Fbk", "3"), vide pour l'item.
struct HelpEntry {
    std::string key;
    std::string text;
};

struct LibraryHelp {
    // ---- les champs connus ----------------------------------------------
    std::string summary;      // une phrase : ce que c'est
    std::string usage;        // comment s'en servir, plusieurs lignes permises
    std::string example;      // un appel ST, recopie tel quel
    std::string since;        // la version ou c'est apparu
    std::string author;
    std::vector<std::string> see;       // les items voisins

    // L'aide longue d'un parametre. Le cinquieme champ de la ligne de
    // declaration reste le resume court ; ceci est ce qui ne tient pas dedans.
    std::vector<HelpEntry> params;

    // Ce que veut dire un code de defaut. Les blocs d'equipement en publient,
    // et un code sans explication oblige a relire le corps du bloc.
    std::vector<HelpEntry> faults;

    // LES CLES INCONNUES SONT CONSERVEES, ET C'EST VOLONTAIRE. Un fichier ecrit
    // par une version plus recente porte des cles que celle-ci ne comprend pas ;
    // les jeter a la reecriture ferait perdre du travail a quelqu'un d'autre,
    // silencieusement, et c'est le genre de perte qu'on ne remarque que
    // longtemps apres.
    std::vector<HelpEntry> unknown;

    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] const std::string* param(std::string_view name) const;
    [[nodiscard]] const std::string* fault(std::string_view code) const;
    void setParam(std::string_view name, std::string text);
    void setFault(std::string_view code, std::string text);
};

// Ce qu'on a lu d'un fichier de bibliotheque : son aide, et de quoi la
// reecrire sans toucher au reste.
struct HelpParse {
    LibraryHelp help;
    std::string itemName;        // ce que dit "name ="
    std::size_t firstHelpLine{0};   // ou commence le bloc d'aide
    std::size_t helpLineCount{0};   // combien de lignes il occupe
    bool        hadHelp{false};
    std::vector<std::string> warnings;
};

// Lit l'aide d'un contenu de fichier .ddt ou .dfb.
[[nodiscard]] HelpParse readHelp(std::string_view fileContents);

// Reecrit le fichier avec cette aide, en ne touchant QU'AU bloc d'aide.
//
// Les commentaires ordinaires, les declarations, les lignes vides, le corps ST
// et les fins de ligne reviennent a l'octet pres. Un fichier sans aide en
// recoit une, posee juste apres "name =" - c'est la que le lecteur la cherche,
// et c'est la qu'elle documente ce qu'elle suit.
[[nodiscard]] std::string writeHelp(std::string_view fileContents, const LibraryHelp&);

// Le bloc d'aide seul, tel qu'il s'ecrit dans un fichier. Expose pour pouvoir
// le montrer dans l'editeur avant de l'appliquer.
[[nodiscard]] std::string renderHelpBlock(const LibraryHelp&);

} // namespace project
