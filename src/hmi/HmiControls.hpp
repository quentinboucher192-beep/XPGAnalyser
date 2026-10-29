// =============================================================================
//  hmi/HmiControls.hpp - les commandes et les afficheurs du lot 9
// -----------------------------------------------------------------------------
//  LE DESSIN ET LE CLIC LISENT LA MEME GEOMETRIE (comme HmiForms au lot 8) :
//  une position du selecteur, une option des boutons radio, une ligne de la
//  liste deroulante, une fleche de la date, une case du programmateur se
//  dessinent la ou elles se cliquent. Tout est dans le repere de l'objet (0,0
//  en haut a gauche, w x h), en pixels de vue.
//
//  Et leurs petits langages, lus ici une fois pour toutes :
//    - les choix       "Manu;Arret;Auto" (+ "0;1;2" : les valeurs ecrites) ;
//    - les etats       "0 = Arret | #4A5261; 1 = Marche | #2ECC71 | clignote" ;
//    - les zones       "0-60 = #2ECC71; 60-80 = #F2C94C; 80-100 = #E5534B" ;
//    - les plages      "Lu-Ve=07:00-12:00,13:30-17:00; Sa=08:00-12:00" ;
//    - les dates       "2026-09-23 14:30" (ou 23/09/2026 14:30).
//  Sans ecran : hmi_test les verifie.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../sim/Value.hpp"   // 1.12.5 : le type du total d'un compteur horaire

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ---- les listes "a;b;c" : elements sans les blancs autour, vides ignores ----------
[[nodiscard]] std::vector<std::string> listItems(std::string_view text);

// ---- les choix (selecteur, liste deroulante, boutons radio) -----------------------
//  Le libelle de chaque choix ("positions" du selecteur, "items" sinon) et la
//  valeur ecrite : celle de "values" au meme rang (une expression : 2, 'Azote',
//  TRUE) ; sans valeur, le rang (0, 1, 2...) - ou le libelle si la variable est
//  une STRING (le moteur en decide).
struct Choice {
    std::string label;
    std::string value;
    bool        index{false};    // vrai : pas de valeur donnee, c'est le rang
};
[[nodiscard]] std::vector<Choice> choicesOf(const Object&);
// 1.12.2 : LES ELEMENTS VENUS D'AILLEURS - la propriete itemsFrom (Liste, Liste deroulante, Groupe
//  d'options, Selecteur) remplace "items" : une enumeration du projet (ses textes ; la valeur ecrite :
//  son nombre), une variable LIST, VECTOR ou un tableau (ses elements, ecrits tels quels), une MAP
//  (ses cles), ou une expression qui rend un texte "a;b;c". Le moteur qui tourne les donne
//  (setChoiceResolver) ; sans moteur (l'editeur), une seule ligne montre la source : << Recettes.
//  Plusieurs moteurs peuvent tourner (la simulation, le poste, les essais) : le dernier pose, encore
//  la, repond - sur le fil qui l'a pose (ailleurs : la ligne de la source) ; chacun retire le sien
//  en s'arretant (et en mourant).
// 1.12.4 : avec l'objet (sa vue : une popup qui recoit la liste en parametre, « <- L »).
using ChoiceResolver = std::function<bool(const Object& owner, std::string_view source, std::vector<Choice>& out)>;
void registerChoiceResolver(const void* owner, ChoiceResolver resolver);
void unregisterChoiceResolver(const void* owner) noexcept;
[[nodiscard]] std::string itemsSourceOf(const Object&);       // itemsFrom, sans blancs ni $ ; vide : "items"
[[nodiscard]] bool hasChoiceResolver() noexcept;
// Le choix que montre une valeur lue (nombre compare en nombre, texte avec ou
// sans apostrophes, TRUE = 1) ; -1 : aucun.
[[nodiscard]] int choiceIndexOf(const std::vector<Choice>&, std::string_view shown);

// ---- les etats (voyant et texte multi-etats) --------------------------------------
//  "valeur = texte | couleur | clignote", separes par ';'. La valeur : un nombre,
//  un intervalle "a..b", TRUE / FALSE, un texte ('Auto' ou Auto), ou "*" (tout
//  le reste). Le premier etat qui correspond l'emporte.
struct StateEntry {
    std::string match;
    std::string text;
    std::string color;
    bool        blink{false};
};
[[nodiscard]] std::vector<StateEntry> parseStateList(std::string_view text, std::string* error = nullptr);
[[nodiscard]] int stateIndexOf(const std::vector<StateEntry>&, std::string_view shown);
// 1.12.6 (lot B) : PAR SON NUMERO OU PAR SON NOM - une enumeration (Mode vaut 1, Auto) trouve
// « 1 = ... » comme « Auto = ... » ; `name` vide : par la valeur seule.
[[nodiscard]] int stateIndexOf(const std::vector<StateEntry>&, std::string_view shown, std::string_view name);
// 1.12.3 : L'ECRITURE QUI MANQUAIT - "0 = Arret | #4A5261; 1 = Marche | #2ECC71 | clignote". Un
// texte, une valeur ou une couleur ne peut pas contenir ';' ni '|' (la liste ne se relirait pas) :
// stateFieldFits le dit avant (faux, `why` en francais).
[[nodiscard]] std::string formatStateList(const std::vector<StateEntry>&);
[[nodiscard]] bool stateFieldFits(std::string_view text, std::string* why = nullptr);
// 1.12.3 : LES ETATS SELON DES CONDITIONS - la Valeur qu'ils ecrivent : "(C1) ? 1 : (C2) ? 2 : 0"
// (la premiere vraie gagne ; aucune : 0, l'etat « sinon »). parseConditionChain relit ce qu'elle a
// ecrit ; faux : la Valeur n'a pas cette forme (une variable, une autre expression).
[[nodiscard]] std::string conditionChain(const std::vector<std::string>& conditions);
bool parseConditionChain(std::string_view expr, std::vector<std::string>& conditions);

// ---- les zones colorees (bargraphe, cadran) : "de-a = couleur" ------------------------
struct Zone {
    double      from{0}, to{0};
    std::string color;
};
// 1.12.7 (lot D) : une borne peut se donner en % de l'echelle (« 60 % », « 60% ») : elle vaut
// lo + (hi - lo) x 60 / 100 quand l'echelle est donnee (lo < hi) - changer min ou max ne casse plus
// les zones ; sans echelle, 60. Les $ des reperes sont transparents.
[[nodiscard]] std::vector<Zone> parseZones(std::string_view text, std::string* error = nullptr, double lo = 0, double hi = 0);
[[nodiscard]] const Zone* zoneOf(const std::vector<Zone>&, double value);

// ---- les valeurs bornees (curseur, potentiometre) ----------------------------------
[[nodiscard]] double snapToStep(double value, double min, double max, double step);
[[nodiscard]] double fractionOf(double value, double min, double max);     // 0..1
// Le curseur : la piste (et son sens), la zone de la valeur.
struct SliderLayout {
    bool vertical{false};
    Box  track;          // la piste, epaisse de 6 pixels environ
    Box  value;          // la valeur ecrite (vide : pas de place)
    double knob{9};      // le rayon de la poignee
};
[[nodiscard]] SliderLayout sliderLayout(const Object&, double w, double h);
[[nodiscard]] double sliderFractionAt(const Object&, double w, double h, double x, double y);
// Le potentiometre : 270 degres, du bas a gauche (min) au bas a droite (max),
// en passant par le haut. Angles a l'ecran : 0 a droite, sens horaire.
inline constexpr double kKnobStartDeg = 135.0, kKnobSweepDeg = 270.0;
[[nodiscard]] double knobFractionAt(double w, double h, double x, double y);

// ---- le selecteur a N positions ----------------------------------------------------
struct SelectorLayout {
    bool                rotary{true};
    double              cx{0}, cy{0};    // rotatif : le centre du bouton
    double              radius{0};
    std::vector<double> angles;          // rotatif : l'angle de chaque position (degres ecran)
    std::vector<Box>    labels;          // la zone de chaque position (libelle, ou segment)
};
[[nodiscard]] SelectorLayout selectorLayout(const Object&, double w, double h, std::size_t count);
// "position:2", "suivant" (le bouton rotatif lui-meme), "" (rien).
[[nodiscard]] std::string selectorHit(const Object&, double w, double h, double x, double y, std::size_t count);

// ---- les boutons radio --------------------------------------------------------------
[[nodiscard]] std::vector<Box> radioBoxes(const Object&, double w, double h, std::size_t count);
[[nodiscard]] std::string radioHit(const Object&, double w, double h, double x, double y, std::size_t count);

// ---- la liste deroulante --------------------------------------------------------------
//  Ouverte, la liste se deroule SOUS l'objet (hors de sa boite) : `first` est la
//  premiere ligne montree quand il y a plus de lignes que "maxVisible" ; deux
//  bandes fleches font defiler.
struct ComboLayout {
    Box  list;                   // tout le panneau deroule
    Box  up, down;               // les bandes de defilement (vides : pas besoin)
    std::vector<Box> rows;       // les lignes montrees
    std::size_t first{0};        // le rang de la premiere
    double rowH{28};
};
[[nodiscard]] ComboLayout comboLayout(const Object&, double w, double h, std::size_t count, std::size_t first);
// 1.12.2 : LA LISTE (Kind::List) - les lignes dans l'objet (sa hauteur), les bandes de defilement
// en haut et en bas quand tout ne tient pas. listHit : "choix:3", "defiler:-1" / "defiler:1", "".
[[nodiscard]] ComboLayout listLayout(const Object&, double w, double h, std::size_t count, std::size_t first);
[[nodiscard]] std::string listHit(const Object&, double w, double h, double x, double y, std::size_t count, std::size_t first);
// 1.12.2 : LE TABLEAU DYNAMIQUE (rowsFrom) - l'en-tete, les lignes qui tiennent (la taille du texte
// donne leur hauteur), la barre de defilement a droite quand toutes ne tiennent pas (la poignee : la
// part montree). tableHit : "defiler:-N" / "defiler:N" (une page, au-dessus ou au-dessous de la
// poignee), "" ailleurs. Le premier rang montre se borne a count - fit.
struct TableLayout {
    double      headerH{26};
    double      rowH{22};
    std::size_t fit{0};          // les lignes qui tiennent
    std::size_t first{0};        // la premiere montree
    Box         bar{};           // la barre (w = 0 : tout tient)
    Box         thumb{};
};
[[nodiscard]] TableLayout tableLayout(const Object&, double w, double h, std::size_t count, std::size_t first);
[[nodiscard]] std::string tableHit(const Object&, double w, double h, double x, double y, std::size_t count, std::size_t first);
// 1.12.16 : une courbe pilotable - son bouton Pause / Continuer (en haut a droite) : "pause" ; "" : ailleurs.
// 1.13.1 : toute sa barre - "figer", "curseurs", "toutvoir" ; `cursors` : les deux curseurs sont poses, le cadre de
// leurs mesures porte les deux boutons d'export de la zone, "zone:csv" et "zone:excel" (trendLayout).
[[nodiscard]] std::string trendHit(const Object&, double w, double h, double x, double y, bool cursors = false);
// ... et ou il est, dans le repere de l'objet (x, y, largeur, hauteur).
[[nodiscard]] std::array<double, 4> trendPauseBox(double w, double h);

// ---- courbes : les points d'une plume d'un objet Courbe (1.13.1 : ici, avec la courbe en marche) ---------
struct TrendSeries {
    std::string expression;
    std::deque<std::pair<double, double>> points;   // (instant IHM en s, valeur)
    bool        visible{true};                      // 1.12.16 : sa condition (penVisible) ; fausse : cachee, mesuree quand meme
};

// ---- 1.13.1 : LA COURBE EN MARCHE - figer, curseurs, zoom, export de la zone ------------------------------
//  ("dans les courbes il faut ajouter un mode freeze qui bloque l'acquisition de nouvelles valeurs, + pouvoir
//  ajouter des lignes verticales pour faire des mesures de temps + pouvoir zoomer/dezoomer etirer/detendre de
//  maniere hyper zoomee, pouvoir exporter que la zone dans une balise de lignes verticales")
//  Sur une courbe pilotable (sa case Pilotable), en marche - la simulation et le poste d'exploitation :
//   - FIGER arrete l'ACQUISITION : aucune mesure n'entre dans les plumes, aucune ne tombe (la fenetre reste entiere
//     aussi longtemps qu'on veut) ; Reprendre la relance, le temps du gel reste un trou (gaps). La PAUSE de la
//     1.12.16 fige l'affichage seul : les mesures continuent, Continuer les montre ;
//   - LA FENETRE : `span` secondes (0 : la duree de l'objet), finie au bord direct (l'instant present, celui de la
//     pause ou du gel) - ou posee (`pinned`) a `end`, apres un glisser ou un zoom hors du direct. La molette zoome
//     le temps autour de la souris (en direct, le bord droit reste le present), Maj+molette les valeurs (lo, hi),
//     glisser deplace, Tout voir revient a la duree et a l'echelle de l'objet ;
//   - DEUX CURSEURS (A, B) : deux instants poses (le bouton Curseurs, ou un clic) et tires a la souris : leurs
//     heures, l'ecart, la valeur de chaque plume a chacun et la difference ;
//   - L'EXPORT DE LA ZONE : les mesures entre A et B, l'heure et toutes les plumes (CSV, Excel).
//  Le moteur garde un TrendState par courbe ; le dessin et le clic lisent ces fonctions-ci (sans ecran : hmi_test).
struct TrendState {
    bool   frozen{false};
    double frozenAt{0};                              // l'instant du gel
    std::vector<std::pair<double, double>> gaps;     // les gels passes (debut, fin) : des trous sur la courbe
    double span{0};                                  // la largeur de la fenetre (s) ; 0 : la duree de l'objet
    bool   pinned{false};                            // la fenetre posee a `end` ; faux : elle finit au bord direct
    double end{0};
    bool   valueZoom{false};                         // l'axe des valeurs zoome (lo, hi) ; faux : celui de l'objet
    double lo{0}, hi{0};
    std::optional<double> cursorA, cursorB;          // les deux curseurs (des instants IHM)
    // Le geste en cours (la souris tenue sur la zone de trace) : 1 le curseur A, 2 le curseur B, 3 la fenetre.
    int    grab{0};
    double pressX{0}, pressY{0}, lastX{0}, lastY{0};
    bool   moved{false};
    [[nodiscard]] bool zoomed() const noexcept { return span > 0 || pinned || valueZoom; }
    [[nodiscard]] bool cursors() const noexcept { return cursorA.has_value() && cursorB.has_value(); }
};
inline constexpr double      kTrendMinSpan   = 1e-4;     // au plus pres, 0,1 ms sur toute la largeur
inline constexpr std::size_t kTrendMaxPoints = 200000;   // par plume au plus (une longue pause, une fenetre posee)

// La geometrie d'une courbe (le dessin et le clic) : la zone de trace (la legende - ou la barre - en haut, les
// graduations a gauche et en bas) ; pilotable, sa barre en haut a droite, de droite a gauche Pause (l'icone de la
// 1.12.16), Figer / Reprendre, Curseurs, Tout voir - un bouton qui ne tient pas n'y est pas ; avec les curseurs, le
// cadre de leurs mesures en haut a gauche de la zone de trace (sa hauteur : le dessin) et ses boutons CSV, Excel.
struct TrendLayout {
    struct Button {
        std::string part;     // "pause", "figer", "curseurs", "toutvoir"
        Box         box;
    };
    Box                 plot;
    std::vector<Button> buttons;
    double              legendRight{0};    // la legende s'arrete avant la barre
    Box                 readout;           // x, y, largeur (h : 0)
    Box                 exportCsv, exportExcel;   // w = 0 : le cadre est trop etroit
};
[[nodiscard]] TrendLayout trendLayout(const Object&, double w, double h, bool cursors = false);

// La largeur de la fenetre : le zoom, sinon la duree. La fenetre : (debut, fin) ; `edge`, le bord direct.
[[nodiscard]] double trendSpan(const TrendState*, double duration);
[[nodiscard]] std::pair<double, double> trendWindow(const TrendState*, double duration, double edge);
// La molette sur le temps : `factor` < 1 rapproche (sans limite genante : kTrendMinSpan), > 1 eloigne (jusqu'a la
// duree) ; l'instant `at` reste sous la souris. `moving` : le bord direct avance (ni pause ni gel) - une fenetre qui
// le suit garde alors son bord droit au present.
void trendZoomTime(TrendState&, double factor, double at, double duration, double edge, bool moving);
// Maj+molette : l'axe des valeurs autour de `at` ; lo, hi : l'echelle montree jusque-la. Faux : trop pres.
bool trendZoomValues(TrendState&, double factor, double at, double lo, double hi);
// Glisser : la fenetre avance de `dt` secondes (negatif : vers le passe), jamais apres le bord direct ni loin avant
// `oldest` (la plus vieille mesure) ; revenue au bord, elle le suit de nouveau. `dv` : l'axe des valeurs, s'il est zoome.
void trendPan(TrendState&, double dt, double dv, double duration, double edge, double oldest);
void trendShowAll(TrendState&);   // la duree et l'echelle de l'objet (les curseurs restent)
// Les curseurs : le bouton (sans curseur, A et B au tiers et aux deux tiers de la fenetre ; sinon ils partent) ;
// un clic a l'instant t (A, puis B, puis le plus proche s'y pose) ; celui qui est a moins de `reach` pixels de x
// dans la zone de trace (1 : A, 2 : B, 0 : aucun).
void trendToggleCursors(TrendState&, double start, double end);
void trendPlaceCursor(TrendState&, double t);
[[nodiscard]] int trendCursorNear(const TrendState&, double x, const Box& plot, double start, double end, double reach = 6);
// La valeur d'une plume a l'instant t : lineaire entre deux mesures, ou tenue (`step`, l'escalier) ; nullopt : pas de
// mesure la - avant la premiere, dans un trou (un gel), plus de deux periodes apres la derniere.
[[nodiscard]] std::optional<double> trendValueAt(const std::deque<std::pair<double, double>>& points, double t, bool step,
                                                 const std::vector<std::pair<double, double>>& gaps = {});
// L'echelle montree : le zoom des valeurs, sinon Axe Y min / max (Echelle fixe) ou les valeurs de la fenetre (auto).
[[nodiscard]] std::pair<double, double> trendScale(const Object&, const std::vector<TrendSeries>* series, double start, double end,
                                                   const TrendState*);
// Les mesures des curseurs : par plume, son nom (Noms des plumes, sinon l'expression), sa valeur en A et en B.
struct TrendReading {
    std::string           name;
    std::optional<double> a, b;
    bool                  visible{true};
};
[[nodiscard]] std::vector<TrendReading> trendReadings(const Object&, const std::vector<TrendSeries>& series, const TrendState&);
// "2.35 s", "12.5 ms", 250 micro-secondes (le signe garde) ; une valeur lue : six chiffres significatifs au plus.
[[nodiscard]] std::string trendDuration(double seconds);
[[nodiscard]] std::string trendNumber(double v);
// La graduation `k` (1..3 sur 4) d'une fenetre zoomee : son ecart au bord droit, dans l'unite de la fenetre (-2.5 ms).
[[nodiscard]] std::string trendTick(double offset, double span);
// 1.12.13 : LA CASE TOUCHEE d'un tableau (lignes dynamiques, cases fixes : la meme mise en page) - "entete:C" (l'en-tete
// de la colonne C, 0...), "case:R:C" (la ligne R du modele, la premiere montree comprise, et sa colonne), "case:-1:-1"
// ailleurs dans l'objet (sous les lignes) ; "" sur la barre de defilement (tableHit) ou dehors. `widths` : les largeurs
// relatives des colonnes (vide : une seule).
[[nodiscard]] std::string tableCellHit(const Object&, double w, double h, double x, double y, std::size_t count, std::size_t first,
                                       const std::vector<double>& widths);
// 1.12.13 : la place d'une case ("case:R:C", R une ligne montree) ou d'un en-tete ("entete:C") - les scripts de session
// et les essais cliquent dessus. Faux : hors du tableau, ou la ligne n'est pas montree (defilee).
[[nodiscard]] bool tableCellBox(const Object&, double w, double h, std::string_view part, std::size_t count, std::size_t first,
                                const std::vector<double>& widths, Box& out);
// "ouvrir" (la boite), "choix:3", "defiler:-1" / "defiler:1", "" (rien).
[[nodiscard]] std::string comboHit(const Object&, double w, double h, double x, double y, std::size_t count, bool open,
                                   std::size_t first);

// ---- la date et l'heure ---------------------------------------------------------------
struct DateTime {
    int year{2026}, month{1}, day{1}, hour{0}, minute{0}, second{0};
    bool operator==(const DateTime&) const = default;
};
[[nodiscard]] int daysInMonth(int year, int month);
[[nodiscard]] DateTime dateTimeFromEpoch(double seconds);        // heure locale
[[nodiscard]] int      weekdayFromEpoch(double seconds);         // 0 lundi ... 6 dimanche (heure locale)
[[nodiscard]] double   epochFromDateTime(const DateTime&);       // heure locale
// Un champ ("jour", "mois", "annee", "heure", "minute", "seconde") avance ou
// recule, en boucle dans ses bornes ; le jour se ramene au dernier du mois.
[[nodiscard]] DateTime stepDateTime(DateTime, std::string_view field, int delta);
// fields : "date et heure", "date" ou "heure".
[[nodiscard]] std::string isoDateTime(const DateTime&, std::string_view fields, bool seconds);     // 2026-09-23 14:30
[[nodiscard]] std::string frenchDateTime(const DateTime&, std::string_view fields, bool seconds);  // 23/09/2026 14:30
// Lit 2026-09-23 14:30[:05], 23/09/2026 14:30, une date seule ou une heure seule
// (les autres champs gardent ceux de `inOut`).
[[nodiscard]] bool parseDateTime(std::string_view text, DateTime& inOut);
struct PickerLayout {
    struct Field {
        std::string name;     // "jour", "mois", "annee", "heure", "minute", "seconde"
        Box box, up, down;
    };
    std::vector<Field> fields;
    Box now, ok;              // Maintenant, Valider
};
[[nodiscard]] PickerLayout pickerLayout(const Object&, double w, double h);
// Lot 10 : la meme geometrie sans objet (l'heure du menu Parametres systeme).
[[nodiscard]] PickerLayout pickerLayout(std::string_view fields, bool seconds, double w, double h);
// "plus:jour", "moins:minute", "maintenant", "valider", "" (rien).
[[nodiscard]] std::string pickerHit(const Object&, double w, double h, double x, double y);

// ---- le programmateur horaire ----------------------------------------------------------
//  Sept jours (lundi d'abord), chacun en cases de 60, 30 ou 15 minutes.
struct WeekSchedule {
    int slotMinutes{30};
    std::array<std::vector<bool>, 7> days;
    [[nodiscard]] int slots() const noexcept { return 24 * 60 / slotMinutes; }
};
inline constexpr std::string_view kDayNames[7] = {"Lu", "Ma", "Me", "Je", "Ve", "Sa", "Di"};
[[nodiscard]] int resolutionMinutes(std::string_view resolution);     // "1 h" 60, "30 min" 30, "15 min" 15
[[nodiscard]] WeekSchedule emptySchedule(int slotMinutes);
// "Lu-Ve=07:00-12:00,13:30-17:00; Sa=08:00-12:00" (les heures arrondies aux cases).
[[nodiscard]] bool parseSchedule(std::string_view text, int slotMinutes, WeekSchedule& out, std::string* error = nullptr);
// La forme courte : les jours qui se suivent et se ressemblent groupes (Lu-Ve=...).
[[nodiscard]] std::string formatSchedule(const WeekSchedule&);
// 1.12.7 (lot D) : CE QUE LA RESOLUTION FAIT DES PLAGES (avant : arrondies en silence) - une ligne par
// plage qui ne tombe pas juste : `range` tel qu'ecrit (07:00-07:20), `becomes` ce que le programmateur
// en garde (07:00-07:30), `gone` : plus courte qu'une case, elle disparait. Les plages illisibles : rien.
struct ScheduleRounding {
    std::string range, becomes;
    bool        gone{false};
};
[[nodiscard]] std::vector<ScheduleRounding> scheduleRoundings(std::string_view text, int slotMinutes);
[[nodiscard]] bool scheduleOn(const WeekSchedule&, int weekday, int minuteOfDay);    // weekday : 0 lundi
struct ScheduleLayout {
    double labelW{34}, headerH{22};
    double cellW{0}, cellH{0};
    Box    grid;
    int    slots{48};
};
[[nodiscard]] ScheduleLayout scheduleLayout(const Object&, double w, double h, int slotMinutes);
// "case:2,17" (mercredi, 17e case), "jour:2", "heure:17", "" (rien).
[[nodiscard]] std::string scheduleHit(const Object&, double w, double h, double x, double y, int slotMinutes);

// ---- l'afficheur 7 segments ------------------------------------------------------------
//  Les segments d'un caractere : bits 0..6 = a b c d e f g (a en haut, puis dans
//  le sens horaire, g au milieu).
[[nodiscard]] std::uint8_t segmentsOf(char c);
struct SegmentDigit {
    char c{' '};
    bool dot{false};
};
// Le nombre sur `digits` chiffres dont `decimals` apres la virgule ; trop grand :
// des tirets ; `valid` faux (valeur illisible) : des tirets aussi.
[[nodiscard]] std::vector<SegmentDigit> sevenSegmentDigits(double value, int digits, int decimals, bool leadingZeros, bool valid);

// ---- le compteur horaire ----------------------------------------------------------------
// "h:mm:ss" (125:03:45), "heures" (125.06 h), "jours" (5 j 05 h 03 min).
[[nodiscard]] std::string formatRunTime(double seconds, std::string_view format);

// 1.12.5 (lot A) : LE COMPTEUR HORAIRE. Ce que son total tient dans le type de sa variable,
// en secondes (un INT 9,1 h, un UINT 18,2 h, un TIME 49,7 jours ; 0 : sans limite utile, un
// REAL en heures) ; et cette duree en clair ("9,1 h", "49,7 jours", "68 ans").
[[nodiscard]] double      hourMeterLimit(sim::Type);
[[nodiscard]] std::string hourMeterSpan(double seconds);

} // namespace hmi
