// =============================================================================
//  app/ai/AiViewTemplates.hpp - 1.22.8 : LES MODELES DE VUES (un PV d'approbation, des captures d'ecran)
// -----------------------------------------------------------------------------
//  << on peut donner a l'IA des modeles de vues et elle doit les reproduire avec
//  les donnees de l'application >>. Un MODELE DE VUES donne la FORME (les vues,
//  leur disposition, les couleurs, les symboles, l'en-tete et le pied, la
//  navigation) ; les DONNEES viennent du projet (les variables IHM, l'automate
//  lu, les reports, les types) - jamais une variable inventee.
//
//  SES DEUX FORMES :
//    - un PV D'APPROBATION DE VUES (un PDF) : une page de garde (le numero du
//      document, son titre, ses revisions), puis pour chaque vue une page
//      << VUE : <NOM> >> (la capture de l'ecran, le cartouche APPROBATION OUI /
//      NON, DATE, VISA, SIGNATURE) et sa page << COMMENTAIRES : >> (des demandes
//      de changement). Reconnu par son texte ;
//    - une ou des CAPTURES (PNG, JPEG) : choisies par l'utilisateur (le geste
//      << Modele... >> de la piece), ou designees par une demande qui le dit.
//
//  CE QUI EST LU, SUR LE FIL D'ANALYSE (AiAttachments : analyzeNow ; jamais sur
//  celui de l'interface) : la liste des vues (le nom, la page, l'approbation si
//  elle se lit, les commentaires), le document ; l'IMAGE de chaque vue (la page
//  rendue par PDFium et RECADREE sur la capture : findScreen), sa taille et son
//  rapport ; LA CHARTE GRAPHIQUE calculee sur les images (paletteOf : les
//  couleurs dominantes et leur part ; commonBands : l'en-tete et le bandeau du
//  bas repetes d'une vue a l'autre). Les images recadrees vont dans le cache
//  (cache/images/modeles-vues/<id>/ : elles se refont) ; le reste dans l'analyse
//  de la piece (son extra << modeleVues >>), gardee avec la conversation.
//
//  CE QUE L'IA RECOIT : a chaque demande, le RESUME (summaryText : le document,
//  les vues, leur etat et leurs commentaires, la charte - court, a la place du
//  PDF) ; les IMAGES seulement quand elles servent - l'outil local
//  voir_vue_modele rend l'image recadree d'une vue (LocalTool::runRich ; sans
//  appel d'outil : les images jointes une fois, au tour de la demande qui
//  reproduit). Les ROLES des couleurs (defaut, conduite, valeur...) ne se
//  devinent pas par le calcul seul : ils sont dits << probables >>, l'IA les
//  confirme en regardant l'image. Le modele distant n'est jamais reentraine : le
//  modele de vues est une donnee locale, relue et envoyee.
// =============================================================================
#pragma once

#include "AiProvider.hpp"
#include "AiStore.hpp"
#include "../../hmi/HmiMedia.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::ai {
struct Proposal;   // AiOperations.hpp
}

namespace app::ai::viewtpl {

// ------------------------------------------------------------- les images (pures) ---
struct Rect {
    int x{0}, y{0}, w{0}, h{0};
    [[nodiscard]] bool empty() const noexcept { return w <= 0 || h <= 0; }
    bool operator==(const Rect&) const = default;
};
// LE CADRE DE L'ECRAN dans une page rendue : le plus grand rectangle non blanc (des blocs remplis a moitie au moins), elargi
// jusqu'a son cadre (la ligne pleine la plus proche, meme au-dela d'un fond blanc ; pas une ligne du tableau de la page,
// separee du contenu par du blanc), sinon jusqu'a une bande blanche. Vide : rien de tel (une page blanche, un rectangle de moins
// de 4 % de la page). Une capture sans marge (son pourtour n'est pas blanc) : l'image entiere.
[[nodiscard]] Rect findScreen(const hmi::Rgba& page);
[[nodiscard]] hmi::Rgba crop(const hmi::Rgba& img, const Rect& r);
// Reduite (la moyenne des pixels) pour que son grand cote fasse au plus `maxSide` ; deja assez petite : telle quelle.
[[nodiscard]] hmi::Rgba fit(const hmi::Rgba& img, int maxSide);

// Une couleur de la charte : sa valeur (0xRRGGBB), sa part de la surface, son role PROBABLE (vide : aucun).
struct Swatch {
    std::uint32_t rgb{0};
    double        share{0};
    std::string   role;
};
struct Palette {
    std::vector<Swatch> colors;            // par part decroissante (les plus petites, sous 0,3 %, ne sont pas gardees)
    std::uint32_t       background{0};     // le fond : la couleur la plus frequente au centre de l'image
    double              backgroundShare{0};
    [[nodiscard]] bool  empty() const noexcept { return colors.empty(); }
};
// LES COULEURS DOMINANTES : les couleurs exactes comptees, regroupees quand elles sont proches (l'anticrenelage), la plus
// frequente de chaque groupe le represente ; au plus `maxColors`. Plusieurs images : leurs surfaces s'additionnent.
[[nodiscard]] Palette paletteOf(const hmi::Rgba& img, std::size_t maxColors = 20);
[[nodiscard]] Palette paletteOf(const std::vector<const hmi::Rgba*>& imgs, std::size_t maxColors = 20);

// LES BANDES COMMUNES : l'en-tete (depuis le haut) et le bandeau du bas, quand leurs lignes de pixels se retrouvent d'une
// vue a l'autre a la meme place (les vues ramenees a une meme largeur ; une ligne << pareille >> : 70 % de ses pixels retrouves a
// un pixel pres).
struct Bands {
    double        header{0}, footer{0};           // en part de la hauteur (0 : aucune)
    int           headerViews{0}, footerViews{0}; // les vues qui la partagent (la reference comprise)
    int           views{0};
    std::uint32_t headerColor{0}, footerColor{0}; // la couleur la plus frequente de chaque bande
    int           headerAccent{-1}, footerAccent{-1};   // la suivante, si elle couvre 15 % de la bande (0xRRGGBB ; -1 : aucune)
};
[[nodiscard]] Bands commonBands(const std::vector<const hmi::Rgba*>& views);
// L'ENCRE d'une page de commentaires : la part de pixels sombres dans son cadre (de 6 a 94 % de la largeur, de 12 a 82 % de la
// hauteur), sans les lignes du tableau (une ligne ou une colonne sombre a moitie). Au-dessus de 0,1 % : annotee (a la main ?).
[[nodiscard]] double inkRatio(const hmi::Rgba& page);

[[nodiscard]] std::string hex(std::uint32_t rgb);              // "#E0E0E0"
[[nodiscard]] std::string colorWord(std::uint32_t rgb);        // "rouge", "gris clair", "cyan"...
[[nodiscard]] std::string ratioText(int w, int h);             // "4:3", "16:9", "16:10", "5:4" ; sinon "1,39:1"
[[nodiscard]] std::string percentText(double share);           // 0.58 -> "58", 0.004 -> "0,4"
// La taille probable de l'ecran d'origine (des pixels a 96 points par pouce) : une resolution courante du meme rapport et
// d'une largeur proche (a 15 %), sinon la mesure arrondie a la dizaine.
[[nodiscard]] std::pair<int, int> likelySize(double w96, double h96);

// -------------------------------------------------------------- le document (pur) ---
struct Approval {
    std::string state;   // "oui", "non" ; vide : pas renseignee (ou illisible)
    std::string date, visa;
};
struct View {
    std::string name;                 // << VUE : <NOM> >> ; une image : son nom de fichier sans extension
    int         page{0};              // 1 : la premiere page (une image : 0)
    int         commentsPage{0};      // sa page COMMENTAIRES (0 : aucune)
    Approval    approval;
    std::string comments;             // le texte de sa page COMMENTAIRES (vide : aucun)
    bool        handwritten{false};   // des traits dans le cadre des commentaires, sans texte (annotee a la main ?)
    Rect        screen;               // la capture dans la page rendue (pixels du rendu)
    int         width{0}, height{0};  // l'image gardee (pixels ; le grand cote borne a kMaxSide)
    int         screenW{0}, screenH{0};   // la taille probable de l'ecran d'origine (likelySize)
    std::string image;                // l'image recadree, relative a IA/ (cache/images/modeles-vues/<id>/vue-2.png)
    Palette     palette;
};
struct Document {
    std::string number, title;               // << N DE DOCUMENT : ... >>, le titre de la page de garde
    std::string revision, revisionDate, revisionText;   // la derniere revision remplie (Ind, Date, Designations)
};
struct Template {
    std::string              kind;           // "pv" (un PV d'approbation) | "captures" (des images)
    Document                 doc;
    std::vector<View>        views;
    Palette                  palette;        // toutes les vues ensemble
    Bands                    bands;
    double                   confidence{0};
    std::vector<std::string> notes;          // ce qui n'a pas pu etre lu (une page sans capture, PDFium absent...)
    [[nodiscard]] const View* view(std::string_view nameOrNumber) const noexcept;
};
// Le texte des pages dit-il un modele de vues ? << APPROBATION DE VUES >>, des pages << VUE : <nom> >>, APPROBATION, VISA,
// COMMENTAIRES. Reconnu : 0,6 de confiance au moins.
struct TextRecognition {
    bool                     recognized{false};
    double                   confidence{0};
    std::vector<std::string> reasons;
};
[[nodiscard]] TextRecognition recognizeText(const std::vector<std::string>& pages);
// Le document et ses vues, tires du texte seul (sans les images) : le nom, la page, l'approbation, les commentaires.
[[nodiscard]] Template parseText(const std::vector<std::string>& pages);

// -------------------------------------------------------- l'analyse (fil d'analyse) ---
inline constexpr int kMaxSide = 1280;   // le grand cote d'une image envoyee, au plus
// Le dossier du cache des images d'une piece (absolu ; vide : pas de racine).
[[nodiscard]] std::string cacheFolder(std::string_view attachmentId);
// UN PDF : son texte deja extrait (une entree par page) reconnu, puis ses pages de vues rendues, recadrees, la charte ; les
// images ecrites dans `cacheDir` (vide : rien n'est ecrit). Faux : pas un modele de vues (`why` : pourquoi, s'il l'etait).
bool analyzePdf(std::string_view bytes, const std::vector<std::string>& pagesText, const std::string& cacheDir, Template& out,
                std::string* why = nullptr);
// UNE IMAGE (PNG, JPEG) choisie comme modele : une vue (le nom : celui du fichier), sa charte. Faux : illisible.
bool analyzeImage(std::string_view bytes, std::string_view ext, const std::string& fileName, const std::string& cacheDir, Template& out,
                  std::string* why = nullptr);
// L'image d'une vue, refaite (le cache vide) depuis la piece : la page rendue et recadree, ou l'image ; ecrite a `outPath`.
bool renderView(const std::string& sourcePath, std::string_view ext, const View& v, const std::string& outPath, std::string* why = nullptr);

// ------------------------------------------------------------------------- le JSON ---
//  L'analyse d'extraction porte "modeleVues" (le modele lu) ; la piece porte, dans son extra, "modeleVues" : le choix
//  ({ "actif": true|false, "par": "utilisateur"|"demande" }).
[[nodiscard]] json::Value toJson(const Template& t);
[[nodiscard]] Template    fromJson(const json::Value& v);
// L'analyse d'extraction recoit le modele (extra "modeleVues", le resume, les details).
void applyTemplate(AttachmentAnalysis& an, const Template& t);
// Le modele lu par la derniere analyse de la piece - s'il n'est pas ecarte par l'utilisateur ; nullopt : aucun.
[[nodiscard]] std::optional<Template> templateOf(const Attachment& a);
// Une piece qui PEUT etre un modele de vues : un PDF, une image (pas retiree).
[[nodiscard]] bool candidate(const Attachment& a) noexcept;
[[nodiscard]] bool isImageExt(std::string_view ext) noexcept;
// Le choix de l'utilisateur (ou d'une demande) : vrai, faux ; nullopt : rien de choisi (un PDF : sa reconnaissance).
[[nodiscard]] std::optional<bool> choiceOf(const Attachment& a);
void setChoice(Attachment& a, bool on, std::string_view by = "utilisateur");
// A analyser comme modele de vues (une image choisie ; un PDF : toujours, sauf ecarte).
[[nodiscard]] bool wanted(const Attachment& a);

// --------------------------------------------------------------------- pour l'IA ---
struct Attached {
    std::string id, name, path;   // la piece, sa copie
    Template    tpl;
};
// LA CHARTE en quelques lignes : << fond #E0E0E0 (58 %) ; rouge #FF0000 (6 %) : defauts (probable) ; ... ; en-tete 70 px
// commun a 12 vues sur 13 ; bandeau du bas 40 px >>.
[[nodiscard]] std::string charteText(const Template& t);
// LE RESUME qui part a la place de la piece (dans le tour qui l'a portee) : ce qu'elle est, comment la reproduire (court),
// le document, les vues (taille, approbation, commentaires), la charte. `tool` : l'outil voir_vue_modele est offert.
[[nodiscard]] std::string summaryText(const Attached& f, bool tool);
// Ce que dit le contexte de la demande (court) : les modeles joints, l'outil. Vide : aucun modele.
[[nodiscard]] std::string templatesPrompt(const std::vector<Attached>& files, bool tool);

inline constexpr std::string_view kViewTool = "voir_vue_modele";
[[nodiscard]] std::string viewToolJson();
// La reponse a un appel (sur le fil du fournisseur) : la vue (son nom ou son numero ; << commentaires >> : sa page de
// commentaires), son texte et son image (refaite si le cache a ete vide).
[[nodiscard]] LocalToolReply runViewTool(const std::vector<Attached>& files, std::string_view inputJson);
// Sans appel d'outil : les images des vues (au plus `max`), jointes une fois au tour de la demande qui reproduit.
[[nodiscard]] std::vector<ProviderAttachment> imagesFor(const Attached& f, std::size_t max);
// La demande parle-t-elle de reproduire un modele de vues ? (<< reproduis >>, << modele de vue >>, << a l'identique >>,
// << meme charte >>, << PV d'approbation >>...)
[[nodiscard]] bool wantsReproduction(std::string_view request);
// L'IMAGE d'une vue comme une piece jointe (creer_ressource : << PV.pdf > Vue_A >>, ou << PV.pdf > 2 >>) : son chemin ;
// vide : pas cette piece, pas cette vue.
[[nodiscard]] std::string imagePath(const Attached& f, std::string_view reference);
// Les noms << piece > vue >> d'une piece et leurs chemins (les essais, l'environnement fige des verifications de fond).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> imageNames(const Attached& f);

// A RELIER : les objets crees par la proposition qui montrent ou ecrivent une donnee (un afficheur, un voyant, un symbole du
// synoptique, une commande) sans variable ni formule - le logiciel les dit sous la reponse (rien n'est invente).
[[nodiscard]] std::vector<std::string> toLink(const Proposal& p);
[[nodiscard]] std::string toLinkNote(const std::vector<std::string>& names);

} // namespace app::ai::viewtpl
