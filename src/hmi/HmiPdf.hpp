// =============================================================================
//  hmi/HmiPdf.hpp - 1.12.30 : LE MOTEUR PDF (PDFium, charge a la demande)
// -----------------------------------------------------------------------------
//  La visionneuse de documents lit les PDF avec PDFium (le moteur PDF de
//  Chromium, licence BSD ; les binaires officiels de pdfium-binaries, pris dans
//  la roue pypdfium2 par outils/pdfium/obtenir_pdfium.py). PDFium N'EST PAS LIE
//  A L'EDITION DES LIENS : pdfium.dll (libpdfium.so sous Linux) se charge a la
//  premiere page demandee, comme Media Foundation pour la video. Sans lui (le
//  fichier absent, une version qui n'a pas les fonctions attendues), les deux
//  applications demarrent quand meme ; la visionneuse le dit dans son cadre, et
//  les images et le texte s'y lisent toujours.
//
//  OU IL EST CHERCHE, dans l'ordre : les chemins donnes par setSearchPaths (les
//  essais), la variable d'environnement XPG_PDFIUM (un fichier ou un dossier),
//  le dossier de l'executable (pdfium.dll livre a cote de IHM-Studio.exe), et
//  sous Linux le chemin de recherche du systeme (LD_LIBRARY_PATH).
//
//  UN SEUL FIL APPELLE PDFium (il n'est pas sur entre fils) : le fil des
//  documents (HmiDocument.cpp). Les fonctions "un fil" ci-dessous ne
//  s'appellent que de lui. library() et missingAdvice() s'appellent de partout.
//  1.21.0 : l'Assistant IA lit aussi le texte des PDF joints (AiAttachments, sur
//  son fil d'analyse) : chaque fonction "un fil" prend desormais un verrou
//  (callLock, recursif) - deux fils s'en servent tour a tour, jamais ensemble.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::pdf {

// ---- la bibliotheque ---------------------------------------------------------------
struct Library {
    bool        ok{false};
    bool        found{false};  // un fichier trouve : charge, ou abime, ou trop ancien
    std::string file;          // le fichier charge (ou le dernier essaye)
    std::string why;           // pourquoi il ne l'est pas (en francais) ; vide : charge
};
// Les chemins ou chercher (des fichiers ou des dossiers), avant tout autre ; un essai
// les donne avant la premiere page. Un chargement manque se refait avec les nouveaux
// chemins ; un chargement reussi reste (la bibliotheque n'est jamais dechargee).
void setSearchPaths(std::vector<std::string> paths);
// Charge PDFium au premier appel (sur n'importe quel fil) ; rend ce qu'il en est.
[[nodiscard]] Library library();
// Ce que l'utilisateur lit quand PDFium manque : "Lecteur PDF absent : pdfium.dll...".
[[nodiscard]] std::string missingAdvice();
// Le nom du fichier attendu sur cette plateforme (pdfium.dll, libpdfium.so).
[[nodiscard]] std::string_view libraryFileName() noexcept;

// ---- un fil : le fil des documents -------------------------------------------------
using DocHandle = void*;
// Initialise PDFium sur ce fil (une fois ; vrai si la bibliotheque est la).
bool initOnThisThread();
// Ouvre un PDF en memoire : `data` doit vivre jusqu'a close(). Nul : `error` dit pourquoi
// ("PDF illisible ou abime", "PDF protege par un mot de passe"...).
[[nodiscard]] DocHandle open(const std::uint8_t* data, std::size_t size, std::string* error);
void close(DocHandle);
[[nodiscard]] int pageCount(DocHandle);
// La taille d'une page, en points (1/72 de pouce), sans la charger.
bool pageSize(DocHandle, int index, double& width, double& height);
// La page `index` en RGBA, `pxW` x `pxH` pixels, tournee de `quarter` quarts de tour (sens horaire) ;
// fond blanc, annotations comprises.
bool render(DocHandle, int index, int pxW, int pxH, int quarter, Rgba& out, std::string* error = nullptr);
// Chercher `query` (UTF-16, sans casse) dans la page : chaque occurrence, ses rectangles en points, origine en
// haut a gauche de la page non tournee (x0, y0, x1, y1).
struct Found {
    std::vector<std::array<double, 4>> rects;
};
[[nodiscard]] std::vector<Found> search(DocHandle, int index, const std::u16string& query);

// ---- 1.21.0 : le texte (l'Assistant IA lit les PDF joints) -------------------------
// Le verrou des appels a PDFium (recursif) : chaque fonction "un fil" le prend ; plusieurs appels d'affilee sur un
// meme document peuvent le prendre autour.
[[nodiscard]] std::unique_lock<std::recursive_mutex> callLock();
// Le texte d'une page, en UTF-8, dans l'ordre de lecture de PDFium ; vide : une page sans texte (scannee) ou une
// bibliotheque sans FPDFText_CountChars / FPDFText_GetText.
[[nodiscard]] std::string pageText(DocHandle, int index);
// Vrai si la bibliotheque chargee sait rendre le texte d'une page.
[[nodiscard]] bool canExtractText();
// Tout le texte d'un PDF en memoire, une entree par page (au plus `maxPages` ; `pageTotal` : le nombre de pages du
// document). De n'importe quel fil (le verrou est pris). Faux et `error` : PDFium absent ou trop ancien, PDF
// illisible, abime ou protege.
bool extractText(const std::uint8_t* data, std::size_t size, std::vector<std::string>& pages, std::string* error = nullptr,
                 int maxPages = 2000, int* pageTotal = nullptr);

} // namespace hmi::pdf
