// =============================================================================
//  help/HelpExport.hpp — sortir l'aide de l'ecran : une page, ou le dossier
// -----------------------------------------------------------------------------
//  CE QUE CA RESOUT, ET CE N'EST PAS « IMPRIMER ».
//
//  Une mise en service se prepare dans un train et se fait devant une armoire.
//  Ni l'un ni l'autre n'a XpgAnalyzer ouvert. L'aide de la bibliotheque -
//  quarante blocs, leurs parametres, leurs codes de defaut, les macros et leurs
//  questions - est exactement ce qu'on voudrait avoir sous la main a ce
//  moment-la, et c'est precisement le moment ou elle est inaccessible.
//
//  LE DOSSIER D'AFFAIRE EST UN SEUL FICHIER. Pas un dossier de fichiers, pas
//  une archive : UN .html que l'on envoie en piece jointe, que l'on ouvre d'un
//  double-clic sur n'importe quelle machine, et que l'on imprime tel quel. Tout
//  y est en dur - le style, la table des matieres, les renvois - parce qu'un
//  document qui va chercher quelque chose sur le reseau est un document qui ne
//  s'ouvre pas la ou on en a besoin.
//
//  POURQUOI HTML ET PAS PDF. Un generateur de PDF, c'est une dependance, une
//  gestion de polices et une pagination a ecrire. Le HTML se met en page tout
//  seul, se cherche au Ctrl+F, se lit sur un telephone, et la feuille de style
//  `@media print` lui donne ses sauts de page - donc « Imprimer » depuis le
//  navigateur rend un PDF correct, sans qu'on ait eu a en ecrire un.
//
//  DEUX SORTIES, UNE SEULE MISE EN FORME.
//
//    renderArticleHtml   la page qu'on a sous les yeux, telle qu'elle est
//                        affichee : c'est le ui::HelpArticle qui est rendu,
//                        donc l'impression ne peut pas diverger de l'ecran.
//
//    renderDossierHtml   toute la bibliotheque, composee pour etre lue a plat :
//                        couverture, sommaire, un chapitre par categorie, les
//                        pages de diagnostic, le glossaire. Ce n'est PAS la
//                        concatenation des pages de l'ecran, et c'est voulu -
//                        sur papier on veut le tableau des parametres et la
//                        liste de qui appelle quoi, que l'ecran obtient en
//                        cliquant.
//
//  L'ECHAPPEMENT EST LA SEULE CHOSE QUI PEUT SILENCIEUSEMENT TOUT CASSER : une
//  aide qui contient « < 5 mA » couperait le document en deux. Tout passe par
//  escapeHtml, y compris les titres et les attributs, et un test le verifie sur
//  la vraie bibliotheque.
// =============================================================================
#pragma once

#include "../project/LibraryCatalog.hpp"
#include "../ui/widgets/HelpArticleView.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace help {

// Ce que l'on met sur la couverture. Tout est facultatif : un dossier sans
// nom d'affaire reste un dossier utilisable.
struct DossierInfo {
    std::string affaire;      // "SNCF - Poste de Vitry"
    std::string auteur;
    std::string projet;       // le nom du .XPG ouvert
    std::string date;         // laisse vide : rempli avec la date du jour
    std::string libsRoot;     // d'ou vient la bibliotheque exportee
};

struct DossierOptions {
    bool includeDiagnostics{true};   // les pages XPG-nnnn
    bool includeGlossary{true};
    bool includeSource{false};       // les declarations telles qu'au fichier
    bool includeBacklinks{true};     // « appele par », calcule
    bool onlyDocumented{false};      // sauter les blocs sans aide du tout
};

// --- une page -----------------------------------------------------------------
// n°12 : imprimer ce qu'on lit. Le titre sert de <title> et d'en-tete de page.
[[nodiscard]] std::string renderArticleHtml(const ui::HelpArticle&, std::string_view title);

// --- le dossier ---------------------------------------------------------------
// n°11 : toute la bibliotheque en un fichier.
[[nodiscard]] std::string renderDossierHtml(const std::vector<project::CatalogEntry>& library,
                                            const DossierInfo&    info = {},
                                            const DossierOptions& options = {});

// Ecrit le fichier. Rend un message vide quand tout s'est bien passe ; sinon la
// raison, deja redigee pour le bandeau d'etat.
[[nodiscard]] std::string writeHtmlFile(const std::string& path, const std::string& html);

// Le nom de fichier propose : « Aide-SNCF_Poste_de_Vitry-2026-09-20.html ».
[[nodiscard]] std::string suggestedFileName(const DossierInfo&);

// --- expose pour etre verifiable ----------------------------------------------
[[nodiscard]] std::string escapeHtml(std::string_view);

// L'ancre d'un element dans le dossier : « e-ST_EQ_Pump ». Stable, parce que
// c'est ce qui rend les renvois cliquables et les liens partageables.
[[nodiscard]] std::string anchorFor(std::string_view name);

} // namespace help
