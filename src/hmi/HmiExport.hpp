// =============================================================================
//  hmi/HmiExport.hpp - exporter un tableau de donnees : CSV, Excel, PDF (lot 11)
// -----------------------------------------------------------------------------
//  LE BOUTON D'EXPORT (et l'action Exporter des donnees) met dans un fichier ce
//  que montre l'IHM : les alarmes en cours, l'historique des alarmes, les
//  evenements, le journal, les mesures archivees, les jeux d'une recette, ou
//  le contenu d'un objet de la vue (une courbe, un tableau, un graphique, les
//  compteurs de production, les statistiques d'alarmes).
//
//  TROIS FORMATS, ECRITS ICI, SANS BIBLIOTHEQUE :
//    CSV    separateur ';', UTF-8 avec BOM : il s'ouvre dans l'Excel francais
//           tel quel ;
//    Excel  un classeur .xlsx (un zip de XML) : le titre, l'en-tete en gras sur
//           fond gris, les nombres en nombres, les colonnes a leur largeur,
//           l'en-tete fige et filtrable ;
//    PDF    un document A4 paysage : titre, sous-titre (la date, la source),
//           le tableau (l'en-tete repete a chaque page), "Page 2 / 3".
//
//  Le moteur construit le tableau (Runtime::exportTable) ; l'ecran ecrit le
//  fichier (dans le dossier exports/ du projet).
// =============================================================================
#pragma once

#include "HmiMedia.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {

struct ExportTable {
    std::string                           title;      // "Alarmes en cours"
    std::string                           subtitle;   // "2026-09-24 14:05:12 - IHM Station"
    std::vector<std::string>              headers;
    std::vector<std::vector<std::string>> rows;
    // Lot 13 : des lignes sous le tableau (un rapport d'essai : sa description,
    // le visa de qui l'a joue).
    std::vector<std::string>              notes;
};

enum class ExportFormat : std::uint8_t { Csv, Excel, Pdf };
inline constexpr std::string_view kExportFormats[] = {"CSV", "Excel", "PDF"};
// "CSV", "Excel", "PDF" (sans casse) ; ou un nom de fichier : son extension
// (.csv, .xlsx, .xls, .pdf). nullopt : ni l'un ni l'autre.
[[nodiscard]] std::optional<ExportFormat> exportFormatFrom(std::string_view labelOrFile);
[[nodiscard]] std::string_view exportFormatLabel(ExportFormat) noexcept;   // "CSV", "Excel", "PDF"
[[nodiscard]] std::string_view exportExtension(ExportFormat) noexcept;     // "csv", "xlsx", "pdf"
// Un nom de fichier sur : les caracteres interdits (\ / : * ? " < > |)
// remplaces par '-', l'extension du format (remplace une autre extension
// connue). Vide : "export".
[[nodiscard]] std::string exportFileName(std::string_view wanted, ExportFormat);

// Les sources d'un export (en plus de "recette:Nom" et "objet:Nom").
inline constexpr std::string_view kExportSources[] = {"alarmes", "historique", "\xC3\xA9v\xC3\xA9nements", "syst\xC3\xA8me", "mesures", "audit"};   // lot 13 : audit

[[nodiscard]] std::string exportCsv(const ExportTable&);
[[nodiscard]] Bytes       exportXlsx(const ExportTable&);
// Lot 14 : un classeur de plusieurs feuilles, une par tableau (un rapport).
[[nodiscard]] Bytes       exportXlsxSheets(const std::vector<ExportTable>&);
[[nodiscard]] Bytes       exportPdf(const ExportTable&);
[[nodiscard]] Bytes       exportBytes(const ExportTable&, ExportFormat);

// Pour les tests : UTF-8 -> Windows-1252 (ce que la police standard du PDF
// sait ecrire) ; un caractere qu'elle n'a pas devient '?'.
[[nodiscard]] std::string toWinAnsi(std::string_view utf8);

// ---- lot 13 : les memes outils, pour le dossier de l'IHM (HmiDossier) -------------
// Un zip sans compression (chemin, contenu), date fixe : le meme contenu donne
// le meme fichier. La largeur d'un texte Windows-1252 en Helvetica (points) ;
// une chaine PDF litterale ; un nombre PDF ("12.5", "0").
[[nodiscard]] Bytes       zipStored(const std::vector<std::pair<std::string, std::string>>& items);
[[nodiscard]] double      pdfTextWidth(std::string_view winAnsi, double size, bool bold);
[[nodiscard]] std::string pdfLiteral(std::string_view winAnsi);
[[nodiscard]] std::string pdfNumber(double v);

} // namespace hmi
