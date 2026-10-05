// =============================================================================
//  project/LibraryCatalog.hpp — ce que contient libs/, vu depuis l'aide
// -----------------------------------------------------------------------------
//  LibraryHelp sait lire l'aide d'UN fichier. Il manquait la marche d'avant :
//  savoir quels fichiers existent, et ce qu'ils declarent.
//
//  POURQUOI RELIRE LES DECLARATIONS PLUTOT QUE DE REUTILISER SharedLibrary ?
//
//  SharedLibrary scanne deja libs/, mais il rend un LibraryItem : un nom, une
//  categorie, une version. Il ne garde pas la liste des parametres, parce que
//  son travail est de publier et d'importer des blocs, pas de les decrire.
//  L'editeur d'aide, lui, a besoin de la liste complete : on documente un
//  parametre a la fois, et un parametre qu'on ne voit pas est un parametre
//  qu'on oublie de documenter. C'est la difference entre "quels blocs ai-je"
//  et "de quoi est fait ce bloc".
//
//  LES MEMES REGLES DE LECTURE, DANS LE MEME ORDRE. Un fichier de bibliotheque
//  se lit ainsi, et l'ordre est porteur :
//
//      "name " / "version " en tete   -> en-tete
//      "#" en tete, ou ligne vide     -> commentaire
//      "<<<" en tete                  -> le corps ST commence, on s'arrete
//      contient un ";"                -> une declaration
//
//  Le "#" passe avant le ";" : c'est ce qui permet a une aide redigee en
//  francais de contenir un point-virgule sans devenir un parametre fantome.
//  Reimplementer ces regles ailleurs avec un ordre different, c'est se
//  preparer deux catalogues qui ne comptent pas la meme chose.
//
//  CE FICHIER NE DEPEND QUE DE LibraryHelp ET DE LA BIBLIOTHEQUE STANDARD.
//  Pas d'interface, pas de modele de projet : il se verifie sans ecran.
// =============================================================================
#pragma once

#include "LibraryHelp.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace project {

enum class CatalogKind : std::uint8_t {
    DerivedType,     // .ddt
    FunctionBlock,   // .dfb
    Macro,           // .mac
    Other,
};

[[nodiscard]] std::string_view kindLabel(CatalogKind) noexcept;

// Une ligne de declaration, telle qu'elle est ecrite.
struct Declaration {
    std::string name;
    std::string type;
    std::string scope;     // Member / Input / Output / InOut / Local
    std::string initial;
    std::string comment;   // le resume court, cinquieme champ

    // Un parametre interne au bloc. Il est declare, il compte dans le fichier,
    // mais le documenter n'apporte rien a qui appelle le bloc : c'est une
    // variable de boucle. L'editeur le montre en retrait plutot que de le
    // compter comme un trou dans la documentation.
    [[nodiscard]] bool isLocal() const noexcept;
};

// Un code de defaut annonce par la table en commentaire d'un DDT :
//      # 1  le relais thermique a declenche
struct FaultCode {
    std::string code;
    std::string text;    // ce que dit la table, tel quel
};

struct CatalogEntry {
    CatalogKind kind{CatalogKind::Other};
    std::string name;        // ce que dit "name =", sinon le nom de fichier
    std::string category;    // le dossier sous libs/ : "Equipment"
    std::string version;
    std::string path;        // vide quand l'entree vient d'un texte en memoire
    std::string fileName;

    std::vector<Declaration> declarations;
    std::vector<FaultCode>   faultTable;   // la table en commentaire
    LibraryHelp              help;
    bool                     hasHelp{false};
    std::vector<std::string> warnings;

    // Combien de parametres publics portent une aide longue. C'est le chiffre
    // que l'onglet affiche : il dit s'il reste du travail, et ou.
    [[nodiscard]] std::size_t documentedCount() const;
    [[nodiscard]] std::size_t documentableCount() const;
    [[nodiscard]] const Declaration* declaration(std::string_view name) const;

    // Les aides qui documentent un parametre ABSENT du fichier. Elles
    // apparaissent quand un parametre est renomme ou supprime : l'aide reste,
    // ne s'affiche plus nulle part, et personne ne sait qu'elle est la. Les
    // lister est la seule facon de les retrouver ; on ne les efface pas, parce
    // qu'un renommage se rattrape et qu'un effacement non.
    [[nodiscard]] std::vector<std::string> orphanHelpParams() const;

    // ---- l'aide COMMUNE -----------------------------------------------------
    //
    // Douze champs - Name, Enable, ManMode, Fault... - sont les memes dans les
    // dix DDT d'equipement. Les ecrire dix fois, c'est garantir qu'un jour ils
    // diront dix choses differentes. Un fichier COMMUN.hlp pose dans le dossier
    // d'une categorie porte des lignes « #! param Nom = ... » ; tout parametre
    // de la categorie qui n'a pas d'aide a lui la prend de la. L'aide propre du
    // fichier gagne toujours : une famille qui dit autre chose sur RunMs le dit.
    //
    // Elle est gardee A PART de `help` : l'editeur enregistre `help` dans le
    // fichier, et y recopier l'aide commune la figerait dans chaque fichier au
    // premier enregistrement - exactement ce qu'on voulait eviter.
    std::vector<HelpEntry> inherited;

    // L'aide d'un parametre : la sienne, sinon celle du fichier commun. Nul si
    // aucune des deux. `fromCommon` dit laquelle, pour que l'aide l'affiche.
    [[nodiscard]] const std::string* paramHelp(std::string_view name,
                                               bool* fromCommon = nullptr) const;

    // ---- les champs INTERNES ------------------------------------------------
    //
    // « #! internal = PrevAck, FbkElapsed » : des champs publics par necessite
    // (un DDT n'a pas de membre prive) mais qu'il ne faut jamais ecrire - la
    // memoire d'un front, un chrono. L'aide les range a part, et la couverture
    // ne les compte pas : documenter un chrono interne n'apprend rien a qui
    // cable l'equipement.
    [[nodiscard]] bool isInternal(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> internals() const;

    // ---- l'historique des versions -----------------------------------------
    //
    // « #! changes 1.02 = ce qui a change » : ce qu'il faut savoir en passant
    // d'une version a l'autre. La cle est la version, le texte peut tenir sur
    // plusieurs lignes. Rendu du plus recent au plus ancien.
    [[nodiscard]] std::vector<HelpEntry> changes() const;

    // Ce qu'un essai pose et ce qu'il attend : « #! given = X := v » pose une
    // variable avant de lancer l'exemple, « #! expect = n : X = v » dit ce
    // qu'elle vaut apres n cycles. Une ligne par assertion.
    [[nodiscard]] std::vector<std::string> givens() const;
    [[nodiscard]] std::vector<std::string> expectations() const;
};

// Lit un contenu de fichier. Ne touche pas au disque : c'est ce qui rend la
// chose verifiable avec des chaines litterales.
//
// Un .mac ne se lit PAS comme un .ddt, et ce n'est pas un detail de forme.
// Une macro est du code ST : le "#" n'y est pas un commentaire, c'est le
// separateur des litteraux bases (16#FF). Une ligne "#! summary = ..." posee
// telle quelle dans un .mac ne serait pas ignoree, elle serait une erreur de
// syntaxe, et la macro cesserait de se lancer. L'aide d'une macro vit donc
// DANS SON BLOC D'EN-TETE "(* ... *)", que l'interpreteur saute deja.
[[nodiscard]] CatalogEntry parseLibraryFile(std::string_view contents,
                                            std::string_view fileName = {});

// Les "parametres" d'une macro sont les questions qu'elle pose. Elles sont
// extraites de ses appels Ask / AskChoice / AskNumber : c'est la seule
// interface qu'une macro expose a qui la lance, et c'est donc exactement ce
// qu'il faut documenter. Rien a tenir a jour a cote : ajouter un Ask ajoute
// une ligne a documenter.
[[nodiscard]] std::vector<Declaration> macroQuestions(std::string_view contents);

// Reecrit l'aide d'une macro dans son bloc d'en-tete. Le corps de la macro, sa
// prose d'origine et ses fins de ligne reviennent a l'identique.
//
// Echoue si un texte d'aide contient "*)" : ces deux caracteres fermeraient le
// commentaire, et la suite de l'aide deviendrait du code. Mieux vaut refuser
// en le disant que produire une macro qui ne compile plus.
[[nodiscard]] std::string writeMacroHelp(std::string_view contents, const LibraryHelp&,
                                         std::string* error = nullptr);

// Parcourt un dossier de bibliotheque : chaque sous-dossier est une categorie,
// chaque .ddt / .dfb / .mac une entree. Les entrees sortent triees par
// categorie puis par nom, pour que l'arbre ne change pas d'ordre d'un
// lancement a l'autre - un arbre qui se reordonne tout seul est un arbre dans
// lequel on ne retrouve rien.
[[nodiscard]] std::vector<CatalogEntry> scanLibrary(const std::string& root);

// L'aide commune d'un dossier de categorie : les lignes « #! param » de son
// fichier COMMUN.hlp. Vide s'il n'y en a pas.
[[nodiscard]] std::vector<HelpEntry> commonHelpOf(const std::string& folder);

// Remplit `inherited` : chaque parametre sans aide propre qui en a une dans
// `common`. Appele par scanLibrary, reloadEntry et saveHelp.
void applyCommonHelp(CatalogEntry&, const std::vector<HelpEntry>& common);

// Relit une entree depuis son fichier. Utilise par le bouton Recharger.
[[nodiscard]] bool reloadEntry(CatalogEntry&);

// Ecrit l'aide dans le fichier de l'entree, et met l'entree a jour.
//
// Le fichier est relu APRES ecriture et compare a ce qu'on croyait ecrire. Une
// ecriture qui reussit a moitie - disque plein, droits, fichier verrouille par
// Control Expert - rend un fichier plausible et faux ; le signaler tout de
// suite vaut mieux que le decouvrir a la prochaine ouverture.
struct SaveOutcome {
    bool        ok{false};
    std::string message;
};
[[nodiscard]] SaveOutcome saveHelp(CatalogEntry&, const LibraryHelp&);

} // namespace project
