// =============================================================================
//  hmi/HmiReportLayout.hpp - 1.25.0 : la mise en page des rapports
// -----------------------------------------------------------------------------
//  LE CLIENT (05/11, maquette validee) : << configurer la mise en page du fichier
//  genere, avec apercu et sauvegarde des mises en page, et quand on exporte on
//  puisse choisir la mise en page >>.
//
//  UNE MISE EN PAGE (hmi::ReportLayout, HmiModel.hpp) dit :
//    la page        le format (A4, A3, A5, Lettre), l'orientation, les marges ;
//    l'en-tete      le logo (une image des ressources, a gauche ou a droite), le
//                   titre et le sous-titre << a trous >> ({Titre} {Rapport} {Site}
//                   {Debut} {Fin}...), la couleur d'accent (#RRGGBB ou @Nom) ;
//    le pied        a gauche, au centre, a droite ({Page} {Pages} {Date}...), les
//                   cases de visa (Etabli par ; Verifie par) en fin de rapport ;
//    le contenu     les parties cochees, dans leur ordre : la synthese, le
//                   graphique des alarmes par priorite, les alarmes de la periode,
//                   les plus frequentes, les mesures, les courbes des mesures, la
//                   production, les evenements ;
//    les tableaux   leur taille, les lignes alternees, l'en-tete repete ;
//    le texte       la police (Helvetica, Times, Courier ; 1.25.1 : ou une TrueType des ressources, integree au PDF),
//                   titre et corps ; le titre et les intertitres dans la couleur d'accent, ou non (Standard).
//
//  TROIS SONT FOURNIES, verrouillees : Standard (celle d'avant la 1.25.0, la
//  mise en page par defaut), Compacte (A4 paysage, petits caracteres, avec les
//  courbes), Synthese seule (la synthese et le graphique). Celles du projet se
//  font en dupliquant ; Project::reportLayoutDefault dit celle d'un nouveau
//  rapport (et d'un rapport sans la sienne).
//
//  UNE PARTIE S'IMPRIME si la mise en page la coche ET si le rapport la recueille
//  (ses cases Alarmes, Production, Evenements ; ses mesures).
//
//  LA PAGE EST CALCULEE UNE FOIS (paginate : des ordres de dessin en points PDF,
//  page par page) : le PDF les ecrit (toPdf), l'apercu de l'editeur les dessine
//  (app/hmi/HmiReportLayoutPane) - ce qu'on voit est ce qui s'ecrit.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"
#include "HmiModel.hpp"
#include "HmiReports.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::report {

// ---- les parties ----
struct PartInfo {
    std::string_view key, label, hint;
    bool             table;             // un tableau (une feuille Excel)
};
[[nodiscard]] const std::vector<PartInfo>& partInfos();          // dans l'ordre par defaut
[[nodiscard]] const PartInfo* partInfo(std::string_view key) noexcept;
// Les 8 parties de la mise en page, dans son ordre (les cles inconnues partent, les manquantes viennent en fin, decochees).
[[nodiscard]] std::vector<ReportLayoutPart> partsOf(const ReportLayout&);

// ---- les formats de page (en mm) ----
struct PaperInfo {
    std::string_view key, label;
    double           width, height;
};
[[nodiscard]] const std::vector<PaperInfo>& papers();
[[nodiscard]] const PaperInfo* paperInfo(std::string_view key) noexcept;   // sans casse ; nul : inconnu
// La page en points (1/72 pouce), l'orientation appliquee.
[[nodiscard]] std::pair<double, double> pageSize(const ReportLayout&);
// "A4 portrait", "A3 paysage".
[[nodiscard]] std::string pageText(const ReportLayout&);

// ---- les fournies, celles du projet ----
inline constexpr std::string_view kStandard = "Standard";
[[nodiscard]] const std::vector<ReportLayout>& builtInLayouts();
[[nodiscard]] const ReportLayout* builtInLayout(std::string_view name) noexcept;   // sans casse
[[nodiscard]] bool isBuiltIn(std::string_view name) noexcept;
// Celle du projet, sinon une fournie (sans casse) ; nul : inconnue.
[[nodiscard]] const ReportLayout* findLayout(const Project&, std::string_view name) noexcept;
// La mise en page par defaut (celle d'un nouveau rapport) : Project::reportLayoutDefault si elle existe, sinon Standard.
[[nodiscard]] std::string defaultLayoutName(const Project&);
// La mise en page d'un rapport : la sienne si elle existe, sinon celle par defaut.
[[nodiscard]] const ReportLayout& layoutOf(const Project&, const Report&);
// Les rapports qui l'emploient (la leur, ou celle par defaut quand ils n'en ont pas).
[[nodiscard]] std::vector<std::string> usersOf(const Project&, std::string_view name);
// Ce qui ne va pas : un nom vide, celui d'une fournie ou pris ; une marge, une taille hors bornes ; une couleur
// illisible ; un logo qui n'est pas une image des ressources ; aucune partie cochee.
[[nodiscard]] std::vector<std::string> problems(const Project&, const ReportLayout&);
[[nodiscard]] bool nameFree(const Project&, std::string_view name, Id except = kNoId);
[[nodiscard]] std::string uniqueName(const Project&, std::string wanted);
// DUPLIQUER A PARTIR DE : une copie pour le projet (un identifiant, un nom libre).
[[nodiscard]] ReportLayout duplicate(Project&, const ReportLayout&, const std::string& name = {});
// Renommer : les rapports qui la citent, et la mise en page par defaut, suivent. Rend le nombre de rapports changes.
std::size_t renameReferences(Project&, std::string_view from, std::string_view to);
// Avant de la retirer : les rapports qui la citent reviennent a celle par defaut ; elle cesse d'etre celle par defaut.
std::size_t forget(Project&, std::string_view name);
// La couleur d'accent (une reference @Nom resolue) en 0xRRGGBB ; illisible : le bleu de Standard.
[[nodiscard]] std::uint32_t accentOf(const Project&, const ReportLayout&);

// ---- les trous ----
// {Titre} {SousTitre} {Rapport} {Nom} {Periode} {Site} {Projet} {Debut} {Fin} {Date} {Heure} {Ecrit} {Page} {Pages} :
// remplaces ; un trou inconnu reste tel quel.
[[nodiscard]] std::string expand(std::string_view pattern, const std::vector<std::pair<std::string, std::string>>& fields);
[[nodiscard]] const std::vector<std::string_view>& fieldNames();

// ---- la page calculee ----
struct DrawOp {
    enum class Kind : std::uint8_t { Text, Fill, Stroke, Line, Polyline, Image };
    Kind          kind{Kind::Text};
    double        x{0}, y{0}, w{0}, h{0};   // points PDF : l'origine en bas a gauche ; un texte : (x, ligne de base)
    double        size{9};                  // un texte : sa taille ; un trait : son epaisseur
    bool          bold{false};
    std::uint32_t color{0};                 // 0xRRGGBB
    std::string   text;                     // en WinAnsi (ce que le PDF ecrit)
    std::vector<std::pair<double, double>> points;   // un trait brise
    int           image{-1};                // le rang dans PageDoc::images
};
struct PageDoc {
    double                            width{595}, height{842};
    std::string                       font{"Helvetica"};
    std::vector<std::vector<DrawOp>>  pages;
    std::vector<Rgba>                 images;   // le logo (decode, sans transparence : sur du blanc)
    // 1.25.1 : une police TrueType des ressources - son nom, son fichier, la chasse de chaque octet WinAnsi (en millemes
    // de cadratin ; vide : une des trois polices de tout PDF), ses mesures (le PDF les ecrit).
    std::string                       fontName;
    BlobPtr                           fontData;
    std::vector<double>               widths;
    double                            ascent{800}, descent{-200}, capHeight{700};
    int                               bbox[4]{0, -200, 1000, 900};
};
// La page : l'en-tete, les parties, les cases de visa, le pied de chaque page.
[[nodiscard]] PageDoc paginate(const Project&, const Content&, const ReportLayout&);
[[nodiscard]] Bytes   toPdf(const PageDoc&);
// Le texte WinAnsi d'un ordre de dessin, en UTF-8 (l'apercu, les essais).
[[nodiscard]] std::string fromWinAnsi(std::string_view winAnsi);
// La largeur d'un texte WinAnsi dans la police de la mise en page (Times : celle d'Helvetica, plus large, jamais trop courte).
[[nodiscard]] double textWidth(std::string_view font, std::string_view winAnsi, double size, bool bold);
// 1.25.1 : dans la police de la page calculee (une TrueType des ressources : ses chasses).
[[nodiscard]] double textWidth(const PageDoc&, std::string_view winAnsi, double size, bool bold);
// Les polices proposees : Helvetica, Times, Courier, puis les polices TrueType des ressources du projet.
[[nodiscard]] bool isStandardFont(std::string_view name) noexcept;
[[nodiscard]] std::vector<std::string> fontChoices(const Project&);

// ---- le fichier ----
[[nodiscard]] Bytes renderPdf(const Project&, const Content&, const ReportLayout&);
// Excel : la synthese (si cochee), puis une feuille par tableau coche, dans l'ordre de la mise en page.
[[nodiscard]] Bytes renderXlsx(const Content&, const ReportLayout&);
[[nodiscard]] Bytes render(const Project&, const Content&, ExportFormat, const ReportLayout&);

// ---- l'apercu ----
// DES DONNEES D'EXEMPLE : une journee inventee (des alarmes, trois mesures et leurs courbes, un compteur de
// production, des evenements) - l'apercu d'une mise en page sans historique. `r` : le rapport (ses cases) ; nul : un
// rapport journalier qui recueille tout.
[[nodiscard]] Content sample(const Project&, const Report* r = nullptr);

} // namespace hmi::report
