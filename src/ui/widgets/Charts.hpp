// =============================================================================
//  ui/widgets/Charts.hpp - 1.47.0 : LES GRAPHIQUES DE L'ADMIN (les courbes, les
//  barres, la carte jour x heure, les sparklines des tuiles, la distribution d'un
//  delai, les petits graphiques, la frise des licences...)
// -----------------------------------------------------------------------------
//  La maquette validee : docs/maquettes/1.47.0 (planches 01 a 10 ; graphes.py
//  pour 00-07, stats.js et stats.css pour 08-10 ; les couleurs en tete
//  d'extra.css). Ce fichier ne sait rien du service ni des pages : il dessine
//  des donnees simples (des structures), et rappelle la page quand on clique.
//
//  LA METHODE (la meme que les planches, a garder) :
//    - un seul axe des valeurs par graphique, jamais deux (deux mesures
//      differentes : deux graphiques) ;
//    - des traits fins : courbes de 2 px (la comparaison : 1,5 px, gris), barres
//      de 24 px au plus, le bout de la donnee arrondi (4 px), la base carree sur
//      la ligne de base ; 2 px de surface entre deux morceaux empiles ;
//    - une legende des 2 series ; peu d'etiquettes directes (4 au plus) ;
//    - une grille discrete (traits de 1 px tres clairs, jamais de pointilles),
//      des graduations rondes (1, 2, 2,5, 5 x 10^k) ;
//    - les nombres a la francaise : << 12 480 EUR >> (espace fine insecable),
//      << 3 nov. >>, << 12 % >>, le signe moins typographique ;
//    - une couleur suit son ENTITE, jamais son rang : Camille, Lea, Hugo, Nina
//      toujours dans cet ordre et ces couleurs ; un filtre ne repeint pas ceux qui
//      restent (ColorKeys) ;
//    - le texte est toujours en couleur de texte (l'encre), jamais de la couleur
//      de la serie ; les couleurs d'etat (vert, ambre, rouge) seulement pour une
//      alerte.
//  L'INTERACTION : sur une courbe, une regle verticale et une bulle qui donne
//  toutes les series a cet endroit ; sur une barre, une case, une ligne : la
//  bulle de ce qu'on survole. La cible est plus grande que la marque (toute la
//  colonne, toute la ligne, la case et son espace). Un clic appelle la page
//  (setOnPick : la page Statistiques filtre sa liste). Au clavier : le graphique
//  prend le focus (Tab, ou un clic), les fleches vont d'un point a l'autre,
//  Debut / Fin, Entree ou Espace << cliquent >>, Echap efface.
//  CLAIR ET SOMBRE : les pages de l'Admin sont claires (le theme XPG) - les
//  couleurs y sont celles de la maquette a l'octet pres ; sur un theme sombre,
//  les gris (grille, axes, texte) viennent du theme et les teintes trop
//  sombres sont eclaircies (lisibles sur le fond), toujours les memes pour une
//  meme cle.
//
//  LES PIECES
//    colors::*        la palette (extra.css) : admins, offres, criticite, la
//                     comparaison, sequentielle, ordinale (versions), divergente,
//                     etats ; et Ink : les gris du theme courant.
//    ColorKeys        cle -> couleur, stable (le domaine entier, dans son ordre).
//    fr::*            les formats francais (nombres, euros, %, ecarts, durees,
//                     dates : un jour = Day, des jours depuis le 01/01/1970).
//    niceStep, niceTicks, Linear, Band, nearestIndex : les echelles.
//    TimeChart        le graphique sur un axe de categories (jours, semaines,
//                     mois, versions, tranches) : courbes, aires, cumuls, barres,
//                     barres empilees, groupees, divergentes (entrees / sorties),
//                     plages (debut -> fin d'une journee) ; la comparaison en gris,
//                     des bandes de fond, des lignes de reference (un plafond),
//                     des reperes verticaux (une publication, aujourd'hui), des
//                     notes, la periode en cours en pale.
//    makeDistribution la distribution d'un delai (des tranches, et la mediane, la
//                     moyenne, le max reperes au-dessus) -> un TimeChartData.
//    BarList          des barres horizontales : un classement, une barre et sa
//                     piste, la part de chacun (100 %), des barres groupees.
//    HeatMap          la carte jour x heure (la rampe sequentielle, les cases
//                     signalees, la bande des heures habituelles).
//    DayStrip         un carre par jour (la serie en cours soulignee).
//    Dumbbell         avant -> maintenant, une ligne par entite.
//    Timeline         la frise des licences et de leurs echeances.
//    Sparkline        la petite courbe d'une tuile (et paintSparkline, pour la
//                     dessiner dans une tuile, une cellule).
//    SmallMultiples   des petits graphiques cote a cote, un titre chacun ;
//                     sharedScale : la meme echelle pour tous (sinon chacun la
//                     sienne, comme la planche 05).
//    ChartCard        la carte d'un graphique (planche 08) : titre, valeur, ecart,
//                     ce a quoi on compare, des valeurs a cote, le graphique.
//    paint*           les morceaux a dessiner ailleurs : la legende, la bulle,
//                     l'ecart << +12 % >>, une barre dans un tableau, une barre
//                     a 100 % (la criticite), une colonne au bout arrondi.
//
//  EXEMPLE (la page Statistiques) :
//      auto chart = std::make_unique<ui::charts::TimeChart>("stats.montant");
//      ui::charts::TimeChartData d;
//      d.kind = ui::charts::Kind::Bars;
//      d.x = ui::charts::weeklyAxis(ui::charts::fr::day(2026, 8, 17), 13);
//      d.series.push_back({"montant", "2026", montants});
//      ui::charts::Series cmp{"avant", "l'an dernier", montantsAvant};
//      cmp.role = ui::charts::Role::Compare;
//      d.series.push_back(cmp);
//      d.yFormat = ui::charts::fr::integer;  d.valueFormat = ui::charts::fr::euros;
//      d.partialFrom = 12;  d.clickHint = "Clic : ces commandes";
//      chart->setData(std::move(d));
//      chart->setOnPick([this](const ui::charts::Pick& p) { filterOrders(p.index); });
// =============================================================================
#pragma once

#include "../Theme.hpp"
#include "../Widget.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui::charts {

using gfx::Color;

// =============================================================================
//  1. LES COULEURS (extra.css, en tete) - sur une surface blanche
// =============================================================================
namespace colors {
// Les admins, dans l'ordre fixe (legendes, piles, listes) : Camille (l'admin+), Lea, Hugo, Nina. Au-dela : la suite
// (des teintes tirees de la meme famille, stables par rang dans le domaine).
inline constexpr Color kAdmins[4] = {Color::rgb(0x6D4AFF), Color::rgb(0x0B7FA3), Color::rgb(0xEA580C), Color::rgb(0xDB2777)};
// Les offres (identite) et la Decouverte (l'essai, gris).
inline constexpr Color kPersonal = Color::rgb(0x1BAF7A), kProfessional = Color::rgb(0x6250D6), kEnterprise = Color::rgb(0xEDA100),
                       kOptions = Color::rgb(0xE87BA4), kDiscovery = Color::rgb(0xA3ADBF);
// La criticite : Info, Moyenne, Haute, Critique.
inline constexpr Color kCriticality[4] = {Color::rgb(0x94A3B8), Color::rgb(0x2563EB), Color::rgb(0xD97706), Color::rgb(0xDC2626)};
// UNE mesure seule ; DEUX mesures sur le meme axe : la premiere puis la seconde. La comparaison : gris.
inline constexpr Color kOne = Color::rgb(0x2A78D6), kTwo = Color::rgb(0xEB6834), kCompare = Color::rgb(0xA3ADBF);
// Les autres d'une serie mise en avant (<< elle >> en couleur, << les autres >> en gris clair : planche 04).
inline constexpr Color kOthers = Color::rgb(0xD5DCE6);
// Sequentielle (intensite) : [0] = zero, puis 6 paliers.
inline constexpr Color kSequential[7] = {Color::rgb(0xF1F4F8), Color::rgb(0xCDE2FB), Color::rgb(0x9EC5F4), Color::rgb(0x6DA7EC),
                                         Color::rgb(0x3987E5), Color::rgb(0x256ABF), Color::rgb(0x184F95)};
// Ordinale (les versions, de la plus ancienne a la plus recente).
inline constexpr Color kOrdinal[4] = {Color::rgb(0x86B6EF), Color::rgb(0x5598E7), Color::rgb(0x256ABF), Color::rgb(0x104281)};
// Divergente (entrees au-dessus de 0, sorties au-dessous) : le plus fort d'abord.
inline constexpr Color kUp[2] = {Color::rgb(0x256ABF), Color::rgb(0x86B6EF)};
inline constexpr Color kDown[2] = {Color::rgb(0xE34948), Color::rgb(0xF4A3A2)};
// Les etats : SEULEMENT pour une alerte (toujours avec une icone et un libelle).
inline constexpr Color kGood = Color::rgb(0x16A34A), kAttention = Color::rgb(0xD97706), kBad = Color::rgb(0xDC2626);

// L'admin de rang `order` dans l'ordre fixe (0 : Camille).
[[nodiscard]] Color admin(int order) noexcept;
// Une offre par son code (PERSONAL, PROFESSIONAL, ENTERPRISE, OPTIONS, DECOUVERTE ; casse et accents ignores) ;
// inconnue : kCompare.
[[nodiscard]] Color offer(std::string_view code) noexcept;
// 0 Info .. 3 Critique.
[[nodiscard]] Color criticality(int level) noexcept;
// Le palier sequentiel (0 : rien) d'une valeur sur un maximum : 0 -> 0 ; sinon 1..6.
[[nodiscard]] int sequentialStep(double value, double max) noexcept;
[[nodiscard]] Color sequential(double value, double max) noexcept;
// La version `index` (0 la plus ancienne) parmi `count` : les plus recentes prennent les teintes les plus foncees ;
// au-dela de 4, les plus anciennes sont grises (<< 1.43 et avant >>).
[[nodiscard]] Color ordinal(int index, int count) noexcept;

// Les gris et l'encre du theme courant : exactement ceux de la maquette sur un theme clair, ceux du theme sur un
// sombre.
struct Ink {
    Color ink, ink2, muted, faint;   // le texte : fort, second, discret, les axes
    Color grid, base, hover, rule;   // la grille, la ligne de base, la colonne survolee, la regle des courbes
    Color surface, line, line2;      // la carte, son trait, le trait d'une bulle
    Color accent, accentInk;         // l'accent du logiciel (le focus, << Clic : >>)
    Color track{};                     // la piste d'une barre (sa part sur 100 %)
    bool  dark{false};
};
[[nodiscard]] Ink ink(const Theme& theme) noexcept;
// Une teinte de serie lisible sur ce theme : telle quelle sur un clair ; eclaircie si elle est trop sombre sur un
// sombre (toujours la meme pour une meme teinte).
[[nodiscard]] Color onTheme(Color c, const Ink& k) noexcept;
// Le melange de deux couleurs (t : 0 -> a, 1 -> b).
[[nodiscard]] Color mix(Color a, Color b, float t) noexcept;
} // namespace colors

// =============================================================================
//  2. LA COULEUR D'UNE CLE, STABLE
// -----------------------------------------------------------------------------
//  La couleur d'une cle depend de sa place dans le DOMAINE ENTIER (l'ordre fixe :
//  les admins, les offres, les versions), jamais de son rang parmi ce qui est
//  montre : filtrer Hugo ne donne pas sa couleur a Nina. Une cle hors du domaine
//  prend une couleur par son empreinte (toujours la meme). standard() connait :
//    "admin:0".."admin:3" (et "admin:camille", "admin:lea", "admin:hugo",
//    "admin:nina"), "offre:PERSONAL", "offre:PROFESSIONAL", "offre:ENTERPRISE",
//    "offre:OPTIONS", "offre:DECOUVERTE", "crit:0".."crit:3", "cmp" (gris),
//    "autres" (gris clair), "un", "deux" (les deux mesures), "up:0", "up:1",
//    "down:0", "down:1" (la divergente).
// =============================================================================
class ColorKeys {
public:
    ColorKeys() = default;
    ColorKeys(std::vector<std::string> domain, std::vector<Color> palette);
    // Le domaine (les cles dans l'ordre fixe) et la palette qu'il parcourt (vide : celle des admins puis une suite).
    void setDomain(std::vector<std::string> domain, std::vector<Color> palette = {});
    // Une couleur imposee pour une cle (passe avant le domaine).
    void pin(std::string key, Color c);
    [[nodiscard]] Color colorOf(std::string_view key) const;
    [[nodiscard]] int   indexOf(std::string_view key) const;   // -1 : hors du domaine
    [[nodiscard]] bool  knows(std::string_view key) const;     // epinglee ou dans le domaine
    // La table commune (voir plus haut).
    [[nodiscard]] static const ColorKeys& standard();
    // La couleur d'une cle hors de tout domaine : la palette etendue, par l'empreinte de la cle.
    [[nodiscard]] static Color byHash(std::string_view key);

private:
    std::vector<std::string>                 domain_;
    std::vector<Color>                       palette_;
    std::vector<std::pair<std::string, Color>> pinned_;
};

// =============================================================================
//  3. LES FORMATS FRANCAIS
// =============================================================================
namespace fr {
// L'espace fine insecable (les milliers, avant % ) et l'insecable (avant EUR), le moins typographique, l'ellipse.
inline constexpr const char* kThin = "\xE2\x80\xAF";
inline constexpr const char* kNbsp = "\xC2\xA0";
inline constexpr const char* kMinus = "\xE2\x88\x92";
inline constexpr const char* kEuro = "\xE2\x82\xAC";
inline constexpr const char* kArrow = "\xE2\x86\x92";   // la fleche (8 h 40 -> 17 h 50)
inline constexpr const char* kDot = "\xC2\xB7";         // le point median (19 - le 30/10)

// Un entier : "12 480", "-3" (moins typographique) ; arrondi a l'unite.
[[nodiscard]] std::string integer(double v);
// Un decimal : "2,5", "0,25" ; les zeros de fin retires ; "3" pour 3,0.
[[nodiscard]] std::string decimal(double v, int digits = 1);
// "12 480 EUR" (arrondi a l'euro) ; centimes : "12,50 EUR".
[[nodiscard]] std::string euros(double v);
[[nodiscard]] std::string eurosCents(double v);
// Compact (les axes) : "850", "1,5 k", "85 k", "1,2 M", "3 Md".
[[nodiscard]] std::string compact(double v);
// "850 EUR", "1,5 kEUR", "85 kEUR", "1,2 MEUR".
[[nodiscard]] std::string eurosCompact(double v);
// "12 %" (digits : chiffres apres la virgule, sans zeros de fin).
[[nodiscard]] std::string percent(double v, int digits = 0);
// L'ecart signe : "+12 %", "-3 %" (moins typographique), "=" pour zero.
[[nodiscard]] std::string signedPercent(double v);
[[nodiscard]] std::string signedInteger(double v);   // "+2", "-1", "="
[[nodiscard]] std::string signedPoints(double v);    // "+4 pts", "-1 pt"
// L'ecart de cur a prev en % ("+13 %") ; prev nul : "" (pas d'ecart a dire).
[[nodiscard]] std::string deltaPercent(double cur, double prev);
// Une duree en heures : "35 min", "4 h 05", "1 h", "2 j 6 h", "3 j" (au-dela de 48 h : en jours et heures).
[[nodiscard]] std::string hours(double h);
// Une heure du jour : 8.6667 -> "8 h 40", 21.05 -> "21 h 03".
[[nodiscard]] std::string clock(double h);

// ---- les dates : un jour = des jours depuis le 01/01/1970 (calendrier civil, sans fuseau) ----
using Day = std::int64_t;
[[nodiscard]] Day  day(int year, int month, int dayOfMonth) noexcept;
void               civil(Day d, int& year, int& month, int& dayOfMonth) noexcept;
[[nodiscard]] int  weekday(Day d) noexcept;           // 0 lundi .. 6 dimanche
[[nodiscard]] Day  today() noexcept;                  // le jour du systeme (UTC)
[[nodiscard]] std::string ddmm(Day d);                // "03/11"
[[nodiscard]] std::string ddmmyyyy(Day d);            // "03/11/2026"
[[nodiscard]] std::string dayShort(Day d);            // "3 nov."
[[nodiscard]] std::string dayLong(Day d);             // "mar. 3 nov."
[[nodiscard]] std::string weekdayShort(int wd);       // 0 -> "lun."
[[nodiscard]] std::string monthShort(int month);      // 1 -> "janv."
[[nodiscard]] std::string monthLong(int month);       // 1 -> "janvier"
[[nodiscard]] std::string monthYear(Day d);           // "mars 2024", "janv. 2025"
[[nodiscard]] std::string weekRange(Day monday);      // "Semaine du 26/10 au 01/11"
} // namespace fr

// =============================================================================
//  4. LES ECHELLES
// =============================================================================
// Un pas rond au-dessus de `raw` : 1, 2, 2,5, 5 x 10^k (raw <= 0 : 1).
[[nodiscard]] double niceStep(double raw) noexcept;
// Des graduations rondes qui couvrent [lo, hi], environ `target` intervalles (le pas qui deborde le moins parmi
// target, target + 1, target + 2). lo == hi : [lo, lo + 1 pas]. `integral` : des nombres entiers (des commandes, des
// PC) - jamais un pas de 2,5 ni de 0,5.
[[nodiscard]] std::vector<double> niceTicks(double lo, double hi, int target = 3, bool integral = false);
// Des graduations de duree (en heures) jusqu'a hi : 0, 4, 8 h ; 0, 12, 24, 36, 48 h ; au-dela : en jours.
[[nodiscard]] std::vector<double> hourTicks(double hi, int target = 3);

// Une echelle lineaire : le domaine [d0, d1] -> la portee [r0, r1] (r1 < r0 pour un axe vertical vers le haut).
struct Linear {
    double d0{0}, d1{1};
    float  r0{0}, r1{1};
    [[nodiscard]] float  map(double v) const noexcept;
    [[nodiscard]] double invert(float x) const noexcept;
};
// Des bandes egales (des colonnes) : n bandes sur [r0, r1].
struct Band {
    int   n{0};
    float r0{0}, r1{0};
    [[nodiscard]] float slot() const noexcept { return n > 0 ? (r1 - r0) / static_cast<float>(n) : 0.f; }
    [[nodiscard]] float center(int i) const noexcept { return r0 + slot() * (static_cast<float>(i) + 0.5f); }
    [[nodiscard]] int   indexAt(float x) const noexcept;   // -1 : hors des bandes
};
// L'indice du point le plus proche de x pour n points repartis sur [r0, r1] (bords compris) ; -1 si n == 0.
[[nodiscard]] int nearestIndex(float x, float r0, float r1, int n) noexcept;
// La mediane, la moyenne (NaN sur une liste vide).
[[nodiscard]] double median(std::vector<double> values);
[[nodiscard]] double mean(const std::vector<double>& values);

// =============================================================================
//  5. LES DONNEES
// =============================================================================
// Pas de valeur (un trou dans une courbe, une case vide) : NaN.
inline constexpr double kNone = std::numeric_limits<double>::quiet_NaN();
[[nodiscard]] inline bool isNone(double v) noexcept { return std::isnan(v); }
using Format = std::function<std::string(double)>;

// Un point de l'axe des categories.
struct Category {
    std::string label{};      // l'axe ("12/10", "J+7", "< 1/2 h")
    std::string tip{};        // la bulle ("lun. 12/10", "Semaine du 26/10 au 01/11") ; vide : label
    bool        shade{false};   // une bande de fond tres claire (les fins de semaine)
};
// Les axes du temps tout faits : un point par jour, par semaine (lundi), par mois.
[[nodiscard]] std::vector<Category> dailyAxis(fr::Day first, int count, bool shadeWeekends = false);
[[nodiscard]] std::vector<Category> weeklyAxis(fr::Day firstMonday, int count);
[[nodiscard]] std::vector<Category> monthlyAxis(int year, int month, int count);
[[nodiscard]] std::vector<Category> labelAxis(const std::vector<std::string>& labels);

enum class Role : std::uint8_t {
    Main,      // une serie de la periode
    Compare,   // la periode d'avant, l'an dernier : gris, 1,5 px, dessous ; toujours dans la legende
};
enum class Mark : std::uint8_t {
    Auto,      // celle du graphique (Kind)
    Line,      // une courbe par-dessus des barres (la comparaison est toujours une courbe)
};

struct Series {
    std::string         key{};      // la cle de couleur ("admin:1", "offre:PERSONAL") ; vide : l'ordre des series
    std::string         name{};     // la legende, la bulle
    std::vector<double> values{};   // une par categorie (NaN : rien)
    Role   role{Role::Main};
    Mark   mark{Mark::Auto};
    Color  color{0, 0, 0, 0};     // alpha 0 : la couleur de la cle
    bool   area{false};           // l'aire sous la courbe (10 %)
    bool   step{false};           // en escalier (un nombre qui tient jusqu'au changement : les PC actifs)
    bool   dashed{false};         // en tirets (pas par defaut : la maquette dessine la comparaison pleine)
    bool   endDot{false};         // le point de la derniere valeur
    bool   below{false};          // Diverging : sous la ligne de 0 (les sorties)
    bool   noTip{false};          // pas dans la bulle
    bool   noLegend{false};       // pas dans la legende
    bool   zeroInTip{false};      // une barre a 0 dite dans la bulle
    float  width{0.f};            // l'epaisseur (0 : 2 px, la comparaison 1,5 px)
    std::string        endLabel{};  // une etiquette directe apres le dernier point ("1.46.0 - 94 %")
    std::string        legendValue{};   // un chiffre en gras apres le nom dans la legende (<< en ligne 52 >>)
    Format             format{};    // la valeur dans la bulle (vide : celui du graphique)
    std::vector<Color> pointColors{};   // une couleur par point (alpha 0 : celle de la serie) - un jour signale
};

// Une bande de fond (de l'indice `from` a `to`, bornes comprises, en bandes) : << pas encore admin >>.
struct Shade {
    double      from{0}, to{0};
    std::string label{};            // en haut a gauche, discret
    Color       color{0, 0, 0, 0};    // alpha 0 : tres clair
};
// Une bande horizontale de valeurs (les heures habituelles : de 8 h 40 a 17 h 50), tres claire, derriere tout.
struct YBand {
    double      from{0}, to{0};
    std::string label{};                // en haut a droite de la bande, discret
    Color       color{0, 0, 0, 0};    // alpha 0 : un bleu tres clair
};
// Une ligne de reference horizontale (un plafond).
struct RefLine {
    double      value{0};
    std::string label{};
    bool        labelLeft{false};
    Color       color{0, 0, 0, 0};    // alpha 0 : l'encre discrete
};
// Un repere vertical (une publication, aujourd'hui) a l'indice `at` (fractionnaire : entre deux points). Avec
// `fromValue` : le trait part de cette valeur (au-dessus d'une barre : la mediane d'une distribution) ; sinon toute
// la hauteur. Les etiquettes se rangent seules en haut (une deuxieme rangee si elles se chevauchent).
struct Marker {
    double      at{0};
    std::string label{};
    double      fromValue{kNone};
    Color       color{0, 0, 0, 0};    // alpha 0 : gris clair (pleine hauteur), l'encre (depuis une valeur)
    bool        strong{false};        // l'etiquette en encre forte (sinon discrete)
};
// Une etiquette directe pres d'un point (<< 19 - le 30/10 >>).
struct Note {
    int         index{0};
    double      value{0};
    std::string text{};
    enum class Anchor : std::uint8_t { Middle, Start, End } anchor{Anchor::Middle};
    float       dy{-8.f};             // au-dessus du point
};

enum class Kind : std::uint8_t {
    Line,        // des courbes (et des aires, des cumuls, des escaliers)
    Bars,        // une serie de colonnes (plusieurs : groupees, voir Grouped)
    Stacked,     // des colonnes empilees (dans l'ordre des series, de la base vers le haut)
    Grouped,     // des colonnes cote a cote
    Diverging,   // empilees au-dessus de 0, et au-dessous (Series::below)
    Range,       // une plage par point : series[0] le debut, series[1] la fin (le debut et la fin des journees)
};
enum class LegendMode : std::uint8_t { Auto /* 2 series et plus */, Top, None };

struct TimeChartData {
    Kind                  kind{Kind::Line};
    std::vector<Category> x{};
    std::vector<Series>   series{};
    // Les libelles de l'axe : (indice, ancrage -1 debut, 0 milieu, 1 fin) ; vide : choisis seuls (4 ou 5).
    std::vector<std::pair<int, int>> xTicks{};
    Format                yFormat{};           // les graduations (vide : fr::integer)
    Format                valueFormat{};       // la bulle (vide : yFormat)
    double                yMin{kNone}, yMax{kNone};
    std::vector<double>   yTicks{};            // imposees (vide : niceTicks)
    int                   yTickCount{3};
    bool                  yAxis{true};       // les libelles des valeurs a gauche
    bool                  xAxis{true};       // les libelles de l'axe en bas
    bool                  lineInBands{false};    // Line : les points au milieu des bandes (comme des colonnes)
    std::vector<Shade>    shades{};
    std::vector<YBand>    yBands{};
    std::vector<RefLine>  refs{};
    std::vector<Marker>   markers{};
    std::vector<Note>     notes{};
    int                   partialFrom{-1};   // a partir de cet indice : la periode en cours (pale, << en cours >>)
    float                 barMax{24.f};      // la largeur d'une colonne au plus
    float                 barFraction{0.62f};    // la part de la bande
    LegendMode            legend{LegendMode::Auto};
    std::string           clickHint{};         // la derniere ligne de la bulle ("Clic : ces commandes") ; vide : rien
    std::function<std::string(int)> tipExtra;    // une ligne en plus dans la bulle (vide : rien)
    bool                  tipForward{false}; // la bulle dans l'ordre des series (sinon : de la plus haute a la base)
    int                   exampleTip{-1};    // une bulle montree sans survol (l'exemple d'une planche, une capture)
    std::string           emptyText{"Aucune donn\xC3\xA9" "e sur la p\xC3\xA9riode"};   // aucune valeur du tout
};

// Ce qu'un clic (ou Entree) donne a la page.
struct Pick {
    int         index{-1};    // la categorie (TimeChart), la ligne (BarList, Dumbbell, Timeline), le jour (DayStrip),
                              // la colonne (HeatMap)
    int         series{-1};   // la serie touchee (-1 : aucune en particulier) ; HeatMap : la rangee ; BarList : le
                              // morceau
    std::string key{};          // sa cle
    double      value{kNone};
};
using PickHandler = std::function<void(const Pick&)>;

// La bulle : un titre, des lignes (valeur, pastille, nom), une ligne en plus, << Clic : ... >>.
struct TipRow {
    std::string value{}, name{};
    Color       color{0, 0, 0, 0};    // alpha 0 : pas de pastille
    bool        line{false};          // la pastille d'une courbe (un trait), sinon un carre
};
struct Tip {
    std::string         title{};
    std::vector<TipRow> rows{};
    std::string         extra{};
    std::string         click{};
    [[nodiscard]] bool empty() const noexcept { return title.empty() && rows.empty() && extra.empty(); }
    // Le texte a plat (les essais, l'infobulle d'accessibilite) : une ligne par morceau.
    [[nodiscard]] std::string text() const;
};

// La legende : une pastille (un carre, un trait, un cadre, un point) et son libelle.
enum class Swatch : std::uint8_t { Box, Line, Ring, Dot };
struct LegendItem {
    std::string label{};
    Color       color{0, 0, 0, 0};
    Swatch      swatch{Swatch::Box};
    std::string value{};               // en gras apres le libelle (<< 2026 >>, << 52 >>)
};

// =============================================================================
//  6. LA BASE DES GRAPHIQUES (le survol, le clic, le clavier, la bulle)
// =============================================================================
class ChartBase : public Widget {
public:
    explicit ChartBase(std::string id);
    void setOnPick(PickHandler handler) { onPick_ = std::move(handler); }
    // L'element actif (survole, ou choisi au clavier) : -1 aucun. `sub` : la rangee (HeatMap), la serie.
    [[nodiscard]] int activeIndex() const noexcept { return active_; }
    [[nodiscard]] int activeSub() const noexcept { return activeSub_; }
    // Montrer la bulle d'un element sans survol (le clavier, un exemple, les essais) ; -1 : la cacher.
    void showTip(int index, int sub = -1);
    // Le nombre d'elements (pour le clavier) et la bulle de l'un d'eux.
    [[nodiscard]] virtual int  itemCount() const = 0;
    [[nodiscard]] virtual int  subCount() const { return 0; }
    [[nodiscard]] virtual Tip  tipFor(int index, int sub) const = 0;
    // Le clic : ce que donne l'element sous ce point (repere de la surface) ; index -1 : rien.
    [[nodiscard]] virtual Pick pickAt(gfx::Point p) const = 0;
    // L'element sous ce point (index, sub) : la cible est plus grande que la marque.
    [[nodiscard]] virtual std::pair<int, int> itemAt(gfx::Point p) const = 0;
    // Le geste << cliquer >> (souris ou Entree) : appelle la page.
    void pick(const Pick& p);
    [[nodiscard]] bool requestsOverlayPass() const override;
    [[nodiscard]] SizeHint sizeHint() const override;

protected:
    // La bulle au-dessus de tout (elle peut deborder de la carte, comme la maquette) : son ancre (repere de la
    // surface) - la bulle se pose a droite de `anchor` (a gauche si elle ne tient pas).
    [[nodiscard]] virtual gfx::Rect tipAnchor(int index, int sub) const = 0;
    void        onPaintOverlay(const PaintContext& ctx) override;
    EventResult onEvent(const InputEvent& ev) override;
    void        onFocusChanged(bool focused) override;
    // Le pick d'un element choisi au clavier.
    [[nodiscard]] virtual Pick pickFor(int index, int sub) const;
    void paintFocus(const PaintContext& ctx, const gfx::Rect& r) const;
    [[nodiscard]] bool tipShown() const noexcept;
    [[nodiscard]] bool keyboardShown() const noexcept { return keyboard_ && focused(); }

    int  active_{-1}, activeSub_{-1};
    bool keyboard_{false};    // la derniere main etait au clavier (l'anneau du focus)
    int  example_{-1}, exampleSub_{-1};

private:
    PickHandler onPick_;
};

// =============================================================================
//  7. TimeChart - le graphique sur un axe de categories
// =============================================================================
class TimeChart : public ChartBase {
public:
    explicit TimeChart(std::string id = {});
    void setData(TimeChartData data);
    [[nodiscard]] const TimeChartData& data() const noexcept { return data_; }
    // Les couleurs des cles (vide : ColorKeys::standard()).
    void setColorKeys(ColorKeys keys);
    [[nodiscard]] Color seriesColor(std::size_t i) const;
    // SmallMultiples : un domaine commun a plusieurs graphiques (NaN : le sien seul).
    void setSharedDomain(double lo, double hi);
    // Le domaine des donnees (sans les graduations ni les bornes imposees).
    [[nodiscard]] std::pair<double, double> extent() const;
    [[nodiscard]] std::vector<LegendItem> legendItems() const;

    // LA GEOMETRIE pour les bornes du moment (les essais, la page qui pose une etiquette).
    struct Frame {
        gfx::Rect           plot{};          // la zone tracee
        double              lo{0}, hi{1};  // le domaine des valeurs
        std::vector<double> ticks{};
        bool                band{true};    // des bandes (colonnes) ou des points aux bords (courbes)
        int                 n{0};
        float               legendHeight{0};
        [[nodiscard]] float xAt(double index) const noexcept;
        [[nodiscard]] float yAt(double value) const noexcept;
        [[nodiscard]] float slot() const noexcept;
        [[nodiscard]] int   indexAt(float x) const noexcept;   // la bande, ou le point le plus proche ; -1 hors
    };
    [[nodiscard]] Frame frame() const;
    [[nodiscard]] float barWidth(const Frame& f) const;

    [[nodiscard]] int  itemCount() const override { return static_cast<int>(data_.x.size()); }
    [[nodiscard]] Tip  tipFor(int index, int sub) const override;
    [[nodiscard]] Pick pickAt(gfx::Point p) const override;
    [[nodiscard]] std::pair<int, int> itemAt(gfx::Point p) const override;

protected:
    void onPaint(const PaintContext& ctx) override;
    [[nodiscard]] gfx::Rect tipAnchor(int index, int sub) const override;
    [[nodiscard]] Pick pickFor(int index, int sub) const override;

private:
    struct MarkerLayout {
        struct Item {
            float x{0};
            int   row{0};
            bool  shown{false};
        };
        std::vector<Item> items;
        int               rows{0};
    };
    [[nodiscard]] MarkerLayout layoutMarkers(const Frame& f) const;
    [[nodiscard]] int  markerRows(const Frame& f) const;
    [[nodiscard]] bool isBar(const Series& s) const noexcept;
    [[nodiscard]] int  seriesAt(int index, gfx::Point p, const Frame& f) const;
    TimeChartData data_;
    ColorKeys     keys_;
    bool          ownKeys_{false};
    double        sharedLo_{kNone}, sharedHi_{kNone};
};

// La distribution d'un delai (planche 04) : `samples` en heures, les tranches (`edges` : leurs bornes, une de plus que
// les libelles), et au-dessus la mediane, la moyenne et le max (<< mediane 1 h 10 >>). Le reste comme un TimeChart.
[[nodiscard]] TimeChartData makeDistribution(const std::vector<double>& samples, const std::vector<double>& edges,
                                             const std::vector<std::string>& labels, Color color, std::string seriesName,
                                             Format valueFormat = {});
// La position (fractionnaire, en bandes) d'une valeur dans des tranches (la mediane tombe dans << 1-2 h >>).
[[nodiscard]] double binPosition(double value, const std::vector<double>& edges);

// =============================================================================
//  8. BarList - des barres horizontales
// =============================================================================
enum class BarMode : std::uint8_t {
    Ranking,   // un classement : une barre par ligne, la valeur a droite
    Track,     // une barre et sa piste (la part d'un tout : << 44 % >>, une option vendue)
    Share,     // la part de chacun : une barre a 100 % par ligne, un morceau par cle (dans l'ordre fixe)
    Grouped,   // plusieurs barres fines par ligne (la mediane et la moyenne par version)
};
struct BarRow {
    std::string         key{};        // la couleur de la ligne (Ranking : une entite) ; vide : celle de la liste
    std::string         label{};
    std::string         sub{};        // une ligne sous le libelle (<< en cours >>)
    double              value{0};   // Ranking, Track ; Share : le total (0 : la somme des morceaux)
    std::vector<double> parts{};      // Share : un morceau par cle ; Grouped : une valeur par groupe
    std::string         valueText{};  // vide : le format de la valeur
    std::string         extra{};      // une deuxieme colonne, discrete (<< 5 lic. >>, << 1re sur 4 >>)
    std::string         note{};       // a la place de la barre (<< pas ce droit >>, << jamais (90 j) >>)
    bool                warn{false};    // la note est une alerte (l'icone, la couleur d'attention)
    bool                off{false};     // eteinte (libelle pale, valeur << - >>)
    bool                strong{false};  // la ligne du total (en gras, un peu plus haute)
    Color               color{0, 0, 0, 0};
    std::string         tip{};        // la bulle (vide : composee)
};
struct BarListData {
    BarMode                  mode{BarMode::Ranking};
    std::vector<BarRow>      rows{};
    std::vector<std::string> partKeys{}, partNames{};   // Share, Grouped : les morceaux dans l'ordre fixe
    std::vector<Color>       partColors{};            // facultatif (sinon : leurs cles)
    Color                    color{0, 0, 0, 0};     // la teinte d'une serie unique (alpha 0 : #2a78d6)
    double                   maxValue{0};           // 0 : la plus grande valeur
    float                    labelWidth{0};         // 0 : le plus long libelle (45 % au plus)
    float                    valueWidth{0};         // 0 : la plus longue valeur
    float                    rowHeight{0};          // 0 : 26 px (Grouped : 2 barres de 8 et 2 px)
    float                    barThickness{0};       // 0 : 10 px (Share : 10, la ligne forte 12)
    Format                   format{};                // vide : fr::integer
    bool                     axis{false};           // Grouped : l'axe des valeurs en bas
    std::vector<double>      ticks{};                 // l'axe (vide : choisi seul)
    bool                     valueLabels{true};     // Grouped : la valeur au bout de chaque barre
    bool                     labelsAbove{false};    // Ranking, Track : le libelle et la valeur au-dessus de la barre (planche 08,
                                                    // << Options vendues >>) ; aussi seul quand la colonne serait trop etroite
    LegendMode               legend{LegendMode::Auto};   // Share, Grouped : la legende des morceaux
    std::string              clickHint{};
    std::string              emptyText{"Aucune donn\xC3\xA9" "e sur la p\xC3\xA9riode"};   // aucune ligne
};
class BarList : public ChartBase {
public:
    explicit BarList(std::string id = {});
    void setData(BarListData data);
    [[nodiscard]] const BarListData& data() const noexcept { return data_; }
    void setColorKeys(ColorKeys keys);
    [[nodiscard]] Color partColor(std::size_t part) const;
    [[nodiscard]] Color rowColor(std::size_t row) const;
    // La geometrie : le haut de la ligne i, la colonne des barres.
    struct Frame {
        float x0{0}, x1{0};     // la colonne des barres
        float top{0};           // le haut de la premiere ligne
        bool  stacked{false};   // le libelle au-dessus de la barre
        float legendHeight{0};
        std::vector<float> rowTop{}, rowHeight{};
        double maxValue{1};
        [[nodiscard]] float xAt(double v) const noexcept;
    };
    [[nodiscard]] Frame frame() const;
    [[nodiscard]] float preferredHeight() const;

    [[nodiscard]] int  itemCount() const override { return static_cast<int>(data_.rows.size()); }
    [[nodiscard]] Tip  tipFor(int index, int sub) const override;
    [[nodiscard]] Pick pickAt(gfx::Point p) const override;
    [[nodiscard]] std::pair<int, int> itemAt(gfx::Point p) const override;
    [[nodiscard]] SizeHint sizeHint() const override;

protected:
    void onPaint(const PaintContext& ctx) override;
    [[nodiscard]] gfx::Rect tipAnchor(int index, int sub) const override;
    [[nodiscard]] Pick pickFor(int index, int sub) const override;

private:
    [[nodiscard]] double rowTotal(const BarRow& r) const;
    BarListData data_;
    ColorKeys   keys_;
    bool        ownKeys_{false};
};

// =============================================================================
//  9. HeatMap - la carte jour x heure
// =============================================================================
struct HeatMapData {
    std::vector<std::string>         rowLabels{};   // "lun." .. "dim."
    std::vector<std::string>         colLabels{};   // "0 h" .. "23 h" (un libelle tous les labelEvery)
    std::vector<std::vector<double>> cells{};       // [rangee][colonne]
    std::vector<std::pair<int, int>> flagged{};     // (rangee, colonne) : signalees (un cadre rouge : une alerte)
    double      bandFrom{kNone}, bandTo{kNone};   // les heures habituelles (en colonnes, fractionnaires)
    std::string bandLabel{};                        // << ses heures habituelles : 8 h 40 -> 17 h 50 >>
    int         labelEvery{3};
    std::string scaleLabel{"par heure :"};        // la legende de la rampe
    std::string flagLabel{};                        // la legende du cadre (vide : pas de legende)
    std::function<std::string(int row, int col)> cellTitle;   // la bulle (vide : << lun. 9 h >>)
    Format      valueFormat{};                      // vide : fr::integer
    std::string valueUnit{};                        // << action >> (au pluriel seul : << 3 actions >>)
    std::string clickHint{};
};
class HeatMap : public ChartBase {
public:
    explicit HeatMap(std::string id = {});
    void setData(HeatMapData data);
    [[nodiscard]] const HeatMapData& data() const noexcept { return data_; }
    struct Frame {
        float x0{0}, y0{0}, cw{0}, ch{0}, gap{2};
        int   rows{0}, cols{0};
        double max{0};
        [[nodiscard]] gfx::Rect cell(int r, int c) const noexcept;
    };
    [[nodiscard]] Frame frame() const;
    [[nodiscard]] int  itemCount() const override;   // les colonnes
    [[nodiscard]] int  subCount() const override;    // les rangees
    [[nodiscard]] Tip  tipFor(int col, int row) const override;
    [[nodiscard]] Pick pickAt(gfx::Point p) const override;
    [[nodiscard]] std::pair<int, int> itemAt(gfx::Point p) const override;

protected:
    void onPaint(const PaintContext& ctx) override;
    [[nodiscard]] gfx::Rect tipAnchor(int index, int sub) const override;
    [[nodiscard]] Pick pickFor(int index, int sub) const override;

private:
    HeatMapData data_;
};

// =============================================================================
//  10. DayStrip - un carre par jour, la serie en cours soulignee
// =============================================================================
struct DayStripData {
    fr::Day             first{0};
    std::vector<double> values{};          // une par jour (0 : rien fait)
    int                 streakFrom{-1}, streakTo{-1};   // la serie en cours (soulignee)
    Color               streakColor{0, 0, 0, 0};        // la couleur de l'entite (alpha 0 : #2a78d6)
    std::vector<int>    flagged{};         // des jours signales (un cadre rouge)
    std::vector<int>    labelAt{};         // les jours dont on ecrit la date (vide : le premier, chaque semaine, le dernier)
    bool                lastIsToday{true};   // le dernier libelle : << auj. >>
    std::string         valueUnit{"action"};
    std::string         clickHint{};
};
class DayStrip : public ChartBase {
public:
    explicit DayStrip(std::string id = {});
    void setData(DayStripData data);
    [[nodiscard]] const DayStripData& data() const noexcept { return data_; }
    [[nodiscard]] gfx::Rect cellRect(int i) const;
    [[nodiscard]] int  itemCount() const override { return static_cast<int>(data_.values.size()); }
    [[nodiscard]] Tip  tipFor(int index, int sub) const override;
    [[nodiscard]] Pick pickAt(gfx::Point p) const override;
    [[nodiscard]] std::pair<int, int> itemAt(gfx::Point p) const override;

protected:
    void onPaint(const PaintContext& ctx) override;
    [[nodiscard]] gfx::Rect tipAnchor(int index, int sub) const override;

private:
    DayStripData data_;
};

// =============================================================================
//  11. Dumbbell - avant -> maintenant, une ligne par entite (planche 05)
// =============================================================================
struct DumbbellRow {
    std::string key{}, label{};
    double      before{0}, after{0};
    Color       color{0, 0, 0, 0};    // alpha 0 : la couleur de la cle
    std::string text{};                 // vide : << 117  +22 % >>
};
struct DumbbellData {
    std::vector<DumbbellRow> rows{};
    std::vector<double>      ticks{};   // vide : choisies seules
    Format                   format{};  // vide : fr::integer
    std::string              beforeName{"la p\xC3\xA9riode d'avant"}, afterName{"cette p\xC3\xA9riode"};
    std::string              clickHint{};
};
class Dumbbell : public ChartBase {
public:
    explicit Dumbbell(std::string id = {});
    void setData(DumbbellData data);
    [[nodiscard]] const DumbbellData& data() const noexcept { return data_; }
    struct Frame {
        float x0{0}, x1{0}, top{0}, rowH{22};
        double lo{0}, hi{1};
        std::vector<double> ticks{};
        [[nodiscard]] float xAt(double v) const noexcept;
    };
    [[nodiscard]] Frame frame() const;
    [[nodiscard]] int  itemCount() const override { return static_cast<int>(data_.rows.size()); }
    [[nodiscard]] Tip  tipFor(int index, int sub) const override;
    [[nodiscard]] Pick pickAt(gfx::Point p) const override;
    [[nodiscard]] std::pair<int, int> itemAt(gfx::Point p) const override;
    void setColorKeys(ColorKeys keys);
    // SmallMultiples : un domaine commun (NaN : le sien seul) ; le domaine des donnees.
    void setSharedDomain(double lo, double hi);
    [[nodiscard]] std::pair<double, double> extent() const;

protected:
    void onPaint(const PaintContext& ctx) override;
    [[nodiscard]] gfx::Rect tipAnchor(int index, int sub) const override;

private:
    [[nodiscard]] Color rowColor(std::size_t i) const;
    DumbbellData data_;
    ColorKeys    keys_;
    bool         ownKeys_{false};
    double       sharedLo_{kNone}, sharedHi_{kNone};
};

// =============================================================================
//  12. Timeline - la frise des licences (planche 10)
// =============================================================================
struct TimelineSegment {
    fr::Day     from{0}, to{0};
    std::string key{};                  // la couleur (une offre : "offre:PROFESSIONAL")
    std::string tip{};                  // ce que dit la bulle de ce morceau (vide : les dates)
};
struct TimelineRow {
    std::string                  label{}, sub{};    // "XPG-2024-00031", "5 PC"
    std::vector<TimelineSegment> segments{};      // signee, prolongee... (2 px entre deux morceaux)
    std::string                  lead{};          // avant le premier morceau (<< nouvelle >>, en accent)
    std::string                  end{};           // apres le dernier (<< 11/03/2027 >>)
    bool                         warn{false};   // la fin est une alerte (<< 05/12/2026 - dans 25 j >>)
    bool                         faded{false};  // expiree : pale
    bool                         mono{true};    // le libelle en chasse fixe (un identifiant)
};
struct TimelineData {
    fr::Day                  from{0}, to{0};    // l'axe
    fr::Day                  today{0};          // la ligne << aujourd'hui >> (0 : pas de ligne)
    std::string              todayLabel{"aujourd'hui"};
    std::vector<TimelineRow> rows{};
    std::vector<LegendItem>  legend{};            // en haut (les offres, << aujourd'hui >>) ; vide : aucune
    std::string              clickHint{};
};
class Timeline : public ChartBase {
public:
    explicit Timeline(std::string id = {});
    void setData(TimelineData data);
    [[nodiscard]] const TimelineData& data() const noexcept { return data_; }
    void setColorKeys(ColorKeys keys);
    struct Frame {
        float  x0{0}, x1{0}, top{0}, rowH{30}, labelW{0}, legendHeight{0};
        double from{0}, to{1};
        [[nodiscard]] float xAt(double day) const noexcept;
    };
    [[nodiscard]] Frame frame() const;
    [[nodiscard]] int  itemCount() const override { return static_cast<int>(data_.rows.size()); }
    [[nodiscard]] Tip  tipFor(int index, int sub) const override;
    [[nodiscard]] Pick pickAt(gfx::Point p) const override;
    [[nodiscard]] std::pair<int, int> itemAt(gfx::Point p) const override;

protected:
    void onPaint(const PaintContext& ctx) override;
    [[nodiscard]] gfx::Rect tipAnchor(int index, int sub) const override;

private:
    TimelineData data_;
    ColorKeys    keys_;
    bool         ownKeys_{false};
};

// =============================================================================
//  13. Sparkline - la petite courbe d'une tuile
// =============================================================================
struct SparkStyle {
    Color  color{colors::kOne};
    bool   area{true};
    bool   endDot{true};
    double yMax{kNone};               // une echelle commune (les lignes du classement) ; NaN : la plus grande valeur
    float  width{2.f};
};
// Dessiner une sparkline dans `r` (une tuile, une cellule). `compare` (facultatif) : la periode d'avant, en gris.
void paintSparkline(gfx::IRenderer& r, const Theme& theme, const gfx::Rect& box, const std::vector<double>& values,
                    const std::vector<double>* compare = nullptr, const SparkStyle& style = {});
class Sparkline : public ChartBase {
public:
    explicit Sparkline(std::string id = {});
    void setValues(std::vector<double> values, std::vector<double> compare = {}, SparkStyle style = {});
    void setLabels(std::vector<std::string> labels, Format format = {});   // la bulle de chaque point
    [[nodiscard]] int  itemCount() const override { return static_cast<int>(values_.size()); }
    [[nodiscard]] Tip  tipFor(int index, int sub) const override;
    [[nodiscard]] Pick pickAt(gfx::Point p) const override;
    [[nodiscard]] std::pair<int, int> itemAt(gfx::Point p) const override;
    [[nodiscard]] SizeHint sizeHint() const override;

protected:
    void onPaint(const PaintContext& ctx) override;
    [[nodiscard]] gfx::Rect tipAnchor(int index, int sub) const override;

private:
    std::vector<double>      values_, compare_;
    std::vector<std::string> labels_;
    Format                   format_;
    SparkStyle               style_;
};

// =============================================================================
//  14. SmallMultiples - des petits graphiques cote a cote
// =============================================================================
class SmallMultiples : public Widget {
public:
    explicit SmallMultiples(std::string id = {}, int columns = 2);
    // Un graphique, son titre (gras) et ce qu'il dit a droite (<< 263 - -1 % >>) ; rend le graphique.
    Widget& add(std::string title, std::string aside, WidgetPtr chart);
    template <class T>
    T& addChart(std::string title, std::string aside, std::unique_ptr<T> chart) {
        return static_cast<T&>(add(std::move(title), std::move(aside), std::move(chart)));
    }
    void setColumns(int columns);
    // La meme echelle des valeurs pour tous les TimeChart (le plus grand domaine) ; faux : chacun la sienne.
    void setSharedScale(bool shared);
    [[nodiscard]] int cellCount() const noexcept { return static_cast<int>(cells_.size()); }
    [[nodiscard]] gfx::Rect cellRect(int i) const;

protected:
    void onLayout() override;
    void onPaint(const PaintContext& ctx) override;

private:
    struct Cell {
        std::string title{}, aside{};
        Widget*     chart{nullptr};
    };
    std::vector<Cell> cells_;
    int               columns_{2};
    bool              shared_{false};
};

// =============================================================================
//  15. ChartCard - la carte d'un graphique (planche 08)
// =============================================================================
struct Delta {
    std::string text{};       // "+13 %" ; vide : rien
    int         sense{0};   // +1 c'est bien (vert), -1 c'est mal (rouge), 0 neutre (gris)
    int         direction{0};   // +1 monte, -1 descend, 0 egal : la fleche
};
// L'ecart de cur a prev en % ; upIsGood : monter est-il bien (le montant oui, le delai non, les remboursements non).
[[nodiscard]] Delta deltaOf(double cur, double prev, bool upIsGood = true);
struct SideRow {
    std::string label{}, value{}, faint{};   // << PERSONAL >>, << 9 880 EUR >>, << 38 % >>
    Color       color{0, 0, 0, 0};     // la pastille (alpha 0 : aucune)
    bool        line{false};           // la pastille d'une courbe
    bool        total{false};          // la ligne du total (sans pastille, en encre discrete)
};
class ChartCard : public Widget {
public:
    explicit ChartCard(std::string id = {}, std::string title = {}, std::string small = {});
    void setTitle(std::string title, std::string small = {});
    // La valeur en grand, son ecart, ce a quoi on compare (<< contre 23 000 EUR l'an dernier >>).
    void setValue(std::string value, Delta delta = {}, std::string compare = {});
    // Des valeurs a cote du graphique (la legende avec ses chiffres) ; vide : aucune.
    void setSide(std::vector<SideRow> rows, float width = 0.f);
    // Le graphique de la carte (la carte le possede) ; rend une reference.
    Widget& setChart(WidgetPtr chart);
    template <class T>
    T& setChartAs(std::unique_ptr<T> chart) { return static_cast<T&>(setChart(std::move(chart))); }
    [[nodiscard]] Widget* chart() const noexcept { return chart_; }
    [[nodiscard]] gfx::Rect chartRect() const;

protected:
    void onLayout() override;
    void onPaint(const PaintContext& ctx) override;

private:
    std::string          title_, small_, value_, compare_;
    Delta                delta_;
    std::vector<SideRow> side_;
    float                sideW_{0};
    Widget*              chart_{nullptr};
};

// =============================================================================
//  16. LES MORCEAUX A DESSINER AILLEURS
// =============================================================================
// La hauteur d'une legende (des lignes qui passent a la ligne) ; `paintLegend` la dessine depuis (x, y).
[[nodiscard]] float legendHeight(const std::vector<LegendItem>& items, float width);
float paintLegend(gfx::IRenderer& r, const Theme& theme, float x, float y, float width, const std::vector<LegendItem>& items);
// La bulle : sa taille, puis la dessiner a droite de `anchor` (a gauche si elle ne tient pas dans `limit`).
[[nodiscard]] gfx::Size tipSize(const Tip& tip);
[[nodiscard]] gfx::Rect placeTip(const Tip& tip, const gfx::Rect& anchor, const gfx::Rect& limit);
void paintTip(gfx::IRenderer& r, const Theme& theme, const Tip& tip, const gfx::Rect& box);
// L'ecart (la fleche et << +13 % >>) depuis (x, y) ; rend sa largeur.
float paintDelta(gfx::IRenderer& r, const Theme& theme, float x, float y, const Delta& d, gfx::FontId font = gfx::FontId{12});
[[nodiscard]] float deltaWidth(const Delta& d, gfx::FontId font = gfx::FontId{12});
// Une barre dans un tableau : la piste et la part (0..1).
void paintInlineBar(gfx::IRenderer& r, const Theme& theme, const gfx::Rect& box, double fraction, Color color);
// Une barre a 100 % en morceaux (la criticite, l'etat des licences) : 2 px de surface entre deux morceaux.
void paintSegments(gfx::IRenderer& r, const Theme& theme, const gfx::Rect& box, const std::vector<double>& values,
                   const std::vector<Color>& colors);
// Une colonne : le bout de la donnee arrondi, la base carree. `up` : la valeur au-dessus de la base (sinon dessous) ;
// `horizontal` : une barre couchee (le bout a droite).
void paintColumn(gfx::IRenderer& r, const gfx::Rect& box, Color c, float radius = 4.f, bool up = true, bool horizontal = false);
// Le dessin fin : une ligne brisee lissee (anticrenelee), une aire sous une courbe, un disque.
void strokePath(gfx::IRenderer& r, const std::vector<gfx::Point>& pts, Color c, float width, bool dashed = false);
void fillUnder(gfx::IRenderer& r, const std::vector<gfx::Point>& pts, float baseY, Color c);
void fillDisc(gfx::IRenderer& r, gfx::Point centre, float radius, Color c);
// Un point de fin : le disque de la serie dans son anneau de la couleur de la surface.
void paintDot(gfx::IRenderer& r, gfx::Point centre, float radius, Color c, Color ring);

// Le texte des graphiques (interne, partage par Charts.cpp et ChartsMore.cpp).
namespace detail {
[[nodiscard]] gfx::FontId fontAxis();
[[nodiscard]] float       tw(std::string_view s, gfx::FontId f);
[[nodiscard]] float       lh(gfx::FontId f);
// Le texte centre verticalement sur `cy` ; align : -1 x est le debut, 0 le milieu, 1 la fin.
void                      text(gfx::IRenderer& r, float x, float cy, std::string_view s, gfx::FontId f, Color c, int align = -1);
[[nodiscard]] std::string fit(const gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w);
} // namespace detail

} // namespace ui::charts
