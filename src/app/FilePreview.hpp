// =============================================================================
//  app/FilePreview.hpp - lot API 8 : l'explorateur de fichiers, 2e partie
// -----------------------------------------------------------------------------
//  L'APERCU de l'element choisi dans l'explorateur de l'appli (la colonne de
//  droite de ui::FileExplorerDialog, FileExplorerContext::preview) et les
//  VIGNETTES des images (FileExplorerContext::thumbnail). Lu vite, pour le
//  seul element choisi :
//    - .XPG : l'en-tete (projet, automate, date de l'export) et les nombres de
//      sections, de variables et de DFB (un comptage de balises, sans analyse) ;
//      "deja importe dans ce projet", "plus recent que le projet ouvert" ;
//    - .XHW : l'automate, les racks et les modules ;
//    - classeur (xls::Workbook) : les onglets, le debut du premier (4 x 4),
//      "avec macros" ; CSV / texte / log / json / xml : les 8 premieres lignes ;
//    - image : la vignette (hmi::decodeImage, ramenee a `maxSide`) et ses
//      dimensions ; PDF et autres : taille, date, "Ouvrir avec le programme du
//      systeme" ; dossier : "12 elements : 3 classeurs, 8 CSV, 1 dossier".
//  Jamais d'exception. Sans App : le test (fileexplorer_test) le lie seul.
// =============================================================================
#pragma once

#include "../ui/widgets/FileExplorer.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace app::preview {

    // Ce que l'appli sait du projet ouvert (FileExplorerHost le remplit).
    struct ProjectFacts {
        std::string sourceXpg;    // le .XPG d'origine du projet ("" : aucun)
        std::string exportedAt;   // son fileHeader/dateTime ("date_and_time#2026-9-3-14:18:52")
    };

    // L'en-tete d'un .XPG et ses nombres.
    struct XpgSummary {
        std::string project, version, plc, product, exportedAt;
        std::size_t sections{0}, variables{0}, dfbs{0};
        bool        counted{false};   // faux : fichier trop gros, les nombres ne sont pas lus
    };
    [[nodiscard]] bool readXpg(const std::string& path, XpgSummary& out, std::string* why = nullptr);

    // "date_and_time#2026-9-3-14:18:52" -> "03/09/2026 14:18" ("" : illisible).
    [[nodiscard]] std::string dateLabel(std::string_view dateAndTime);
    // Pour comparer deux dates Control Expert (0 : illisible).
    [[nodiscard]] long long dateKey(std::string_view dateAndTime);

    [[nodiscard]] ui::FilePreviewInfo describe(const std::string& path, const ProjectFacts& project);
    // La vignette d'une image (nullptr : pas une image, illisible, trop grande).
    [[nodiscard]] std::shared_ptr<const ui::PreviewImage> thumbnail(const std::string& path, int maxSide);
    // Ouvrir avec le programme que le systeme associe au fichier ; "" : ouvert.
    [[nodiscard]] std::string openWithSystem(const std::string& path);

} // namespace app::preview
