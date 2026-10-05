// =============================================================================
//  help/CenterSources.hpp - 1.11 (chantier T2) : ce qui remplit l'index du centre
// -----------------------------------------------------------------------------
//  Le guide de l'IHM (hmi::guide::topics()) et la bibliotheque (libs/index.txt)
//  en SourceTopic, pour help::center::Index::build, et les pages de l'aide
//  generale (ui::HelpDocument : xpg_help depend deja de xpg_ui).
// =============================================================================
#pragma once

#include "CenterIndex.hpp"

#include "../project/LibraryCatalog.hpp"
#include "../ui/HelpDocument.hpp"

#include <vector>

namespace help::center {

// Les 216 sujets du guide, dans son ordre : la cle, le titre, le resume, le
// chapitre (group), "objet" pour un objet de la bibliotheque d'objets (@objet),
// le nombre d'intertitres.
[[nodiscard]] std::vector<SourceTopic> guideSources();

// Les macros (kind "macro") et les blocs (kind "dfb" / "ddt") ; group : la
// categorie (le dossier sous libs/). Les autres fichiers sont ignores.
[[nodiscard]] std::vector<SourceTopic> librarySources(const std::vector<project::CatalogEntry>& library);

// LES PAGES DE L'AIDE GENERALE (1.11, tranche 2) : les titres de niveau 2 de
// HelpDocument que le centre reprend, avec le partage de la maquette validee
// (NOTES.md, l'arbre) : group "start" (Demarrer : glisser, themes, dossiers,
// explorateur, bandeaux, filtres, exports) ou "plc" (L'automate : l'arbre,
// importer, reimporter, l'ordre, renommer, Compiler et Generer, les alarmes, les
// DDT dans les popups, les six pages de Simulation). Les pages de nouveautes, la
// table des raccourcis (la page Raccourcis du centre la remplace) et les formats
// de fichier ne sont pas reprises. Le resume : le premier paragraphe de la page.
[[nodiscard]] std::vector<SourceTopic> apiSources(const ui::HelpDocument& doc);
[[nodiscard]] std::vector<SourceTopic> apiSources();   // sur ui::buildHelp()

// Les ancres reprises, dans l'ordre de l'arbre ("start" puis "plc").
[[nodiscard]] const std::vector<std::pair<std::string_view, std::string_view>>& apiPageGroups();

// UNE PAGE, SEULE (1.11, recette R111-4). Le centre montrait toute l'aide generale,
// defilee jusqu'a l'ancre : sous chaque page venaient les suivantes, les nouveautes,
// l'ancienne table des raccourcis et les formats. La page n'a plus que son titre et
// ce qui le suit, jusqu'au titre suivant de son rang ou d'un rang plus haut (sans le
// separateur de la fin). « Importer un fichier » garde les formats de fichier, qui
// n'ont pas de page (leurs colonnes, leurs fichiers d'exemple, que sa carte
// enregistre) ; leur titre passe au rang de la page. Une ancre inconnue : un
// document vide.
[[nodiscard]] ui::HelpDocument apiPage(const ui::HelpDocument& doc, std::string_view anchor);

} // namespace help::center
