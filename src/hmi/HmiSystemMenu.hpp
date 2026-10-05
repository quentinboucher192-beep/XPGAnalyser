// =============================================================================
//  hmi/HmiSystemMenu.hpp - le menu natif "Parametres systeme" (lot 10)
// -----------------------------------------------------------------------------
//  Un menu que l'IHM porte elle-meme, sans rien dessiner : l'action "Parametres
//  systeme", l'objet du meme nom ou IHM_PARAMETRES_SYSTEME() l'ouvrent
//  par-dessus la vue. Trois onglets :
//
//    Reglages    ce qui se change en marche - luminosite, mise en veille, son,
//                volume, deconnexion automatique, clavier virtuel ; lot 13 :
//                taille du texte, couleurs, symboles, theme ; date et heure
//                de l'IHM - et trois gestes de maintenance ;
//    Diagnostic  ce que l'IHM sait d'elle, de l'automate, de l'utilisateur, des
//                alarmes et du poste, en lecture (chaque ligne est aussi une
//                variable SYS.) ;
//    Simulation  1.9 : LA PAGE SIMULATION - tous les esclaves simules de
//                l'application (une carte chacun : ce que lit l'IHM, le vrai,
//                l'esclave ou automatique), et les valeurs de l'esclave choisi
//                (animer, le mouvement, la zone, la periode, forcer, la valeur
//                en direct). Reservee a la permission Administrer (securite
//                eteinte : tout le monde) ; ses choix durent jusqu'au
//                redemarrage de l'IHM, ils ne vont pas dans le projet. Sur le
//                poste d'exploitation : Ctrl+Alt+S (Station::simPage).
//
//  Les reglages sont ceux du POSTE : Redemarrer l'IHM ne les change pas ; ils
//  ne vont pas dans le projet. Sous securite, les reglages proteges (veille,
//  deconnexion, clavier, heure, maintenance) demandent la permission
//  Administrer ; la luminosite, le son, le volume et "Eteindre l'ecran" sont a
//  tout le monde.
//
//  COMME HmiControls : le dessin et le clic lisent la meme geometrie
//  (systemMenuLayout) ; une partie cliquee est un petit texte ("plus:volume",
//  "onglet:diagnostic", "heure:appliquer") que Runtime::systemPart applique.
//  Sans ecran : hmi_test verifie tout.
// =============================================================================
#pragma once

#include "HmiControls.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// ---- les reglages du poste --------------------------------------------------------------
struct SystemSettings {
    int         brightness{100};           // % de 20 a 100
    int         screensaverMin{0};         // 0 : jamais
    bool        soundOn{true};
    int         volume{100};               // % de 0 a 100
    int         autoLogoutMin{-1};         // -1 : celle du projet (Configuration > Utilisateurs) ; 0 : jamais
    std::string keyboard{"automatique"};   // "automatique", "toujours", "jamais"
    double      clockOffset{0};            // secondes ajoutees a l'heure du poste
    // Lot 13 : l'affichage (HmiDisplay.hpp) - au lancement, ceux du projet
    // (Configuration) ; ici, l'operateur les change pour lui.
    int         textScale{100};            // % : 100, 125, 150, 175
    std::string colorMode{"normal"};       // "normal", "daltonien"
    bool        symbols{false};            // des symboles sur les voyants
    std::string theme{"nuit"};             // "nuit", "jour"
    bool operator==(const SystemSettings&) const = default;
};

enum class SettingKind : std::uint8_t { Percent, Choice, Toggle, Clock };
struct SettingSpec {
    std::string_view key;        // "luminosite", "veille", "son", "volume", "deconnexion", "clavier", "heure"
    std::string_view label;
    SettingKind      kind;
    bool             guarded;    // sous securite : la permission Administrer
    std::string_view help;
};
inline constexpr SettingSpec kSettings[] = {
    {"luminosite", "Luminosit\xC3\xA9", SettingKind::Percent, false,
     "L'\xC3\xA9" "clairage de l'\xC3\xA9" "cran, de 20 \xC3\xA0 100 %, par pas de 10."},
    {"veille", "Mise en veille", SettingKind::Choice, true,
     "Apr\xC3\xA8s ce temps sans toucher, l'\xC3\xA9" "cran s'\xC3\xA9teint ; un toucher le rallume, sans rien actionner."},
    {"son", "Son", SettingKind::Toggle, false, "Les sons de l'IHM : l'action Jouer un son, la fonction IHM_SON."},
    {"volume", "Volume", SettingKind::Percent, false, "Le volume des sons, de 0 \xC3\xA0 100 %, par pas de 10."},
    {"deconnexion", "D\xC3\xA9" "connexion automatique", SettingKind::Choice, true,
     "Apr\xC3\xA8s ce temps sans toucher, l'utilisateur est d\xC3\xA9" "connect\xC3\xA9. \xC2\xAB Selon le projet \xC2\xBB garde celle de Configuration > Utilisateurs."},
    {"clavier", "Clavier virtuel", SettingKind::Choice, true,
     "Automatique : celui que demande chaque champ ; toujours : un clavier pour chaque champ ; jamais : aucun."},
    // lot 13 : l'affichage, a tout le monde
    {"texte", "Taille du texte", SettingKind::Choice, false,
     "Tous les textes de l'IHM grandissent : 100, 125, 150 ou 175 %."},
    {"couleurs", "Couleurs", SettingKind::Choice, false,
     "Daltonien : le vert passe au bleu, le rouge au vermillon, l'orange \xC3\xA0 l'ambre - des couleurs qu'on distingue sans voir le vert et le rouge."},
    {"symboles", "Symboles sur les voyants", SettingKind::Toggle, false,
     "Un symbole dans chaque voyant (coche, croix, point d'exclamation, tiret) : l'\xC3\xA9tat se lit sans la couleur."},
    {"theme", "Th\xC3\xA8me", SettingKind::Choice, false,
     "Nuit : les couleurs de la conception ; jour : des couleurs claires, pour un \xC3\xA9" "cran en plein jour."},
    {"heure", "Date et heure", SettingKind::Clock, true,
     "L'heure de l'IHM : horloges, SYS.Time, programmateurs, journal. R\xC3\xA9gl\xC3\xA9" "e ici, elle s'\xC3\xA9" "carte de celle du poste."},
};
inline constexpr std::size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);
[[nodiscard]] const SettingSpec* settingSpec(std::string_view key) noexcept;

// Les gestes de maintenance : eteindre l'ecran (a tout le monde), et trois
// gestes proteges.
struct MaintenanceSpec {
    std::string_view key;        // "eteindre", "journal", "redemarrer", "defaut"
    std::string_view label;
    bool             guarded;
    std::string_view help;
};
inline constexpr MaintenanceSpec kMaintenance[] = {
    {"eteindre", "\xC3\x89teindre l'\xC3\xA9" "cran", false,
     "L'\xC3\xA9" "cran s'\xC3\xA9teint tout de suite (pour le nettoyer, la nuit) ; un toucher le rallume, sans rien actionner."},
    {"journal", "Vider le journal", true, "Le journal de l'IHM repart de z\xC3\xA9ro (l'historique des \xC3\xA9v\xC3\xA9nements reste)."},
    {"redemarrer", "Red\xC3\xA9marrer l'IHM", true,
     "Comme le bouton de la simulation : variables IHM \xC3\xA0 leur valeur initiale, vue de d\xC3\xA9marrage ; les r\xC3\xA9glages restent."},
    {"defaut", "R\xC3\xA9glages par d\xC3\xA9" "faut", true, "Les r\xC3\xA9glages reviennent \xC3\xA0 leur valeur de d\xC3\xA9part (l'heure reprend celle du poste)."},
};
[[nodiscard]] const MaintenanceSpec* maintenanceSpec(std::string_view key) noexcept;

// Les valeurs d'un reglage a choix, dans l'ordre (veille, deconnexion : des
// minutes ; clavier : un mot) ; et le texte d'une valeur.
[[nodiscard]] std::vector<int> settingMinutes(std::string_view key);
// Lot 13 : les tailles de texte proposees (en %).
[[nodiscard]] std::vector<int> textScales();
[[nodiscard]] std::string settingText(const SystemSettings&, std::string_view key, int projectAutoLogoutMin = 0);
// Changer un reglage : "plus", "moins" (pourcentages), "suivant", "precedent"
// (choix, en boucle), "bascule" (son). Faux : rien n'a change (deja au bout).
bool stepSetting(SystemSettings&, std::string_view key, std::string_view how);

// ---- le diagnostic ----------------------------------------------------------------------
struct DiagRow {
    std::string label;
    std::string value;
    std::string sys;          // la variable SYS. qui dit la meme chose ("" : aucune)
    int         tone{0};      // 0 normal, 1 a surveiller (orange), 2 en defaut (rouge), 3 bien (vert), 4 lu en simule (violet, 1.9)
};
struct DiagGroup {
    std::string          title;
    std::vector<DiagRow> rows;
};

// ---- 1.9 : les onglets -----------------------------------------------------------------
//  0 Reglages, 1 Diagnostic, 2 Simulation.
inline constexpr int kSystemTabCount = 3;
inline constexpr int kSimulationTab = 2;
// "Diagnostic" -> 1, "Simulation" -> 2, sinon 0 (sans casse, accents ou pas).
[[nodiscard]] int systemTabFrom(std::string_view text) noexcept;
[[nodiscard]] std::string_view systemTabLabel(int tab) noexcept;    // "Reglages", "Diagnostic", "Simulation"

// ---- 1.9 : la page Simulation (l'onglet 2) ---------------------------------------------
//  Ce que la geometrie doit savoir pour placer les cartes et les lignes (le
//  moteur la tire des esclaves : Runtime::simPageShape).
struct SimCardShape {
    bool linked{true};             // un esclave lie : le segment Le vrai | L'esclave | Auto
    bool why{false};               // une ligne d'explication (l'IHM le lit)
};
struct SimRowShape {
    std::string key;               // la ligne (son adresse : "%MW8504")
    bool        boolean{false};    // une bobine, un bit : clignote, 0 / 1
    std::string kind;              // son mouvement (behaviorKindKey) ; vide : aucun
    bool        forced{false};
};
struct SimPageShape {
    bool                      present{true};       // l'onglet existe (le poste : Station::simPage)
    bool                      locked{false};       // sans la permission Administrer : la carte du refus
    std::vector<SimCardShape> cards;               // un esclave chacune, dans l'ordre du projet
    std::size_t               chosen{0};           // la carte choisie
    // La premiere carte montree ; kAutoScroll : celle qui laisse voir la carte choisie
    // (tant qu'on n'a pas fait defiler la liste).
    static constexpr std::size_t kAutoScroll = static_cast<std::size_t>(-1);
    std::size_t               listScroll{kAutoScroll};
    std::vector<SimRowShape>  rows;                // les lignes de l'esclave choisi
    std::size_t               scroll{0};           // la premiere ligne montree
    bool                      chosenLinked{false}; // l'esclave choisi est lie : Revenir au vrai maintenant
};
struct SimPageLayout {
    bool locked{false};
    Box  lockCard, lockIcon, connect;              // sans la permission : la carte, Se connecter...
    // A gauche, les esclaves.
    Box  list, listHead;
    struct Card {
        std::size_t index{0};
        bool        linked{false};
        Box         box, name, sub, why;
        Box         seg[3];                        // Le vrai, L'esclave, Auto
    };
    std::vector<Card> cards;
    Box         listUp, listDown;                  // defiler la liste (vides : tout tient)
    std::size_t listFirst{0}, listMax{0};
    Box         allHead, allAnimate, allStop, allUnforce;
    // A droite, l'esclave choisi.
    Box  right, head, name, address;
    Box  running, runningSwitch, responds, respondsSwitch;   // la bascule et son libelle
    Box  excLabel, excPrev, excValue, excNext;
    bool twoLines{false};                          // un ecran etroit : chaque ligne sur deux etages
    Box  tableHead, table;
    Box  colAnimate, colName, colKind, colZone, colPeriod, colForce, colValue;   // les titres
    struct Row {
        std::size_t index{0};
        std::string key;
        Box         box, animate, name, value;
        Box         kindPrev, kindValue, kindNext;
        Box         zoneMinMinus, zoneMinValue, zoneMinPlus, zoneTo, zoneMaxMinus, zoneMaxValue, zoneMaxPlus;
        Box         periodMinus, periodValue, periodPlus;
        Box         force, forcedMinus, forcedValue, forcedPlus;
    };
    std::vector<Row> rows;
    Box         up, down;                          // defiler les lignes (vides : tout tient)
    std::size_t first{0}, visible{0}, maxScroll{0};
    Box         actions, animateSlave, stopSlave, unforceSlave, back;
    double      rowH{52}, fontSize{13};
};

// ---- la geometrie (le dessin et le clic) -------------------------------------------------
//  Dans une zone w x h (des pixels d'ecran) : le panneau centre, sa barre de
//  titre et sa croix, les onglets, le corps, une ligne d'etat. `rows` : les
//  lignes a montrer dans le corps (Diagnostic : celles des groupes, titres
//  compris), pour savoir s'il faut defiler ; `scroll` : la premiere montree.
//  1.9 : la page Simulation (onglet 2) grandit le panneau (jusqu'a 1200 x 800) ;
//  `sim` dit ses esclaves et ses lignes (nul : l'onglet existe, rien dedans).
struct SystemMenuLayout {
    Box panel, title, close, status, body;
    Box tabs[kSystemTabCount];
    int tabCount{kSystemTabCount};                 // 1.9 : 2 quand la page Simulation n'existe pas (le poste)
    double rowH{30}, fontSize{14};
    struct Setting {
        std::string key;
        Box         row, label, value;
        Box         minus, plus;       // pourcentage, choix (precedent / suivant)
        Box         toggle;            // son
    };
    std::vector<Setting> settings;     // onglet Reglages (sans l'heure)
    Box                  clockRow;     // la ligne de l'heure
    PickerLayout         clock;        // ses champs (dans le repere du panneau ecran, deja places)
    struct Button {
        std::string key;
        Box         box;
    };
    std::vector<Button> buttons;       // la maintenance
    // Onglet Diagnostic : une ou deux colonnes de lignes.
    std::vector<Box>    diagColumns;
    std::size_t         diagVisible{0};  // lignes par colonne
    double              diagRowH{20};
    Box                 up, down;        // defiler (vides : tout tient)
    SimPageLayout       sim;             // 1.9 : l'onglet Simulation
};
[[nodiscard]] SystemMenuLayout systemMenuLayout(double w, double h, int tab, std::size_t diagRows, std::size_t scroll,
                                                const SimPageShape* sim = nullptr);
// "fermer", "onglet:reglages", "onglet:diagnostic", "moins:luminosite", "plus:volume",
// "precedent:veille", "suivant:clavier", "bascule:son", "heure:plus:jour", "heure:moins:minute",
// "heure:maintenant", "heure:appliquer", "action:journal", "action:redemarrer", "action:defaut",
// "defiler:-1", "defiler:1", "" (dans le panneau, rien), "dehors" (hors du panneau : le fermer).
// 1.9 - la page Simulation : "onglet:simulation", "connecter" (sans la permission) ;
// "esclave:<i>" (une carte), "source:<i>:vrai|esclave|auto" ; "animer_tout", "arreter_tout",
// "deforcer_tout" ; "marche", "repond", "exception:precedent|suivant" ; une ligne (<cle> : son
// adresse) : "animer:<cle>", "mouvement:<cle>:precedent|suivant", "zone_min:<cle>:moins|plus",
// "zone_max:<cle>:moins|plus", "periode:<cle>:moins|plus", "forcer:<cle>", "forcee:<cle>:moins|plus" ;
// "animer_esclave", "arreter_esclave", "deforcer_esclave", "revenir" ; "defiler:-1|1" (les
// lignes), "defiler_liste:-1|1" (les cartes).
[[nodiscard]] std::string systemMenuHit(const SystemMenuLayout&, int tab, double x, double y);
// Le cadre d'une partie (les memes noms ; "valeur:volume" : la case de la
// valeur) : pour les scripts, les tests et l'exemple anime. Faux : pas a l'ecran.
// 1.9 : et "valeur:<cle>", "ligne:<cle>" (la page Simulation), "carte:<i>".
[[nodiscard]] bool systemMenuPartBox(const SystemMenuLayout&, int tab, std::string_view part, Box& out);
// Toutes les parties de la page Simulation a l'ecran (les tests verifient que
// chacune a son cadre et que le clic en son milieu la retrouve).
[[nodiscard]] std::vector<std::string> simPageParts(const SystemMenuLayout&);

// ---- 1.9 : ce que font les boutons - et + de la page Simulation --------------------------
// Un pas "rond" (1, 2, 5, 10, 20, 50...) d'environ un dixieme de `span` ; au moins 1
// pour un entier, 0,1 sinon.
[[nodiscard]] double simStep(double span, bool integer) noexcept;
// Les periodes proposees (0,5 a 600 s) : la suivante (+1) ou la precedente (-1).
[[nodiscard]] double simNextPeriod(double current, int direction) noexcept;
// Les mouvements de la page : aucun, sinus, rampe, aleatoire, compteur, constante
// (une bobine, un bit : aucun, clignote, constante) ; le suivant (+1), le precedent (-1).
[[nodiscard]] std::string simNextKind(std::string_view current, bool boolean, int direction);
// Ce qu'un mouvement montre sur la page : une zone (sinus, rampe, aleatoire), une
// periode (et compteur, clignote).
[[nodiscard]] bool simKindHasZone(std::string_view kind) noexcept;
[[nodiscard]] bool simKindHasPeriod(std::string_view kind) noexcept;
// Les exceptions proposees : 0 (aucune), 1, 2, 3, 4, 6, 11 ; la suivante (+1)...
[[nodiscard]] int simNextException(int current, int direction) noexcept;
[[nodiscard]] std::string simExceptionText(int code);       // "aucune", "02", "0B"

// Les lignes du diagnostic a l'ecran : un groupe ne passe pas d'une colonne a
// l'autre quand tout tient ; sinon les lignes se suivent, a partir de `scroll`
// (ramene a ce qui peut se montrer : `maxScroll`).
struct DiagLine {
    std::size_t group{0};
    int         row{-1};          // -1 : le titre du groupe
    std::size_t column{0};
    Box         box;              // dans le repere du menu
};
[[nodiscard]] std::vector<DiagLine> diagLines(const std::vector<DiagGroup>&, const SystemMenuLayout&, std::size_t scroll,
                                              std::size_t* maxScroll = nullptr);

} // namespace hmi
