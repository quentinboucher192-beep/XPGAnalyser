// =============================================================================
//  hmi/HmiModel.hpp - le projet IHM : configuration, vues, calques, objets
// -----------------------------------------------------------------------------
//  LE MODELE NE SAIT RIEN DE L'ECRAN. Il tient ce que l'editeur modifie et ce
//  que la simulation lit, comme domain::Project pour le programme : l'editeur
//  (app/hmi) et le rendu en passent par lui, jamais l'inverse.
//
//  TROIS CHOIX, ET LEUR RAISON.
//
//  1. DES IDENTIFIANTS, PAS DES INDEX. Une navigation vise une vue, une
//     alarme vise un objet, l'annulation remet un objet a sa place : il faut
//     un nom qui ne bouge pas quand on supprime au milieu. `Id` est attribue
//     par le projet, jamais reutilise, et enregistre avec lui.
//
//  2. UN SAC DE PROPRIETES, PAS UN CHAMP PAR PROPRIETE. Chaque propriete a une
//     valeur (du texte : "120", "#3080FF", "TRUE", "Niveau : {Niv} %") et une
//     EXPRESSION facultative, evaluee a chaque cycle en simulation. C'est ce
//     que demande la specification ("toutes les proprietes acceptent des
//     expressions"), et c'est ce qui rend le reste generique : enregistrer,
//     copier le style, le panneau des proprietes, les parametres d'un
//     composant ne connaissent qu'une forme.
//
//  3. DES COORDONNEES ABSOLUES, MEME DANS UN GROUPE. Un groupe est un objet
//     dont les enfants designent le groupe par `parent` ; son cadre est celui
//     de ses enfants. Tourner ou retourner un groupe tourne ou retourne chaque
//     enfant autour du centre du groupe : pas de pile de transformations a
//     composer, et un enfant sorti du groupe reste exactement ou il etait.
//
//  L'ORDRE DE DESSIN : les calques du bas vers le haut, puis, dans un calque,
//  l'ordre du vecteur `objects`. Premier plan / arriere-plan agissent dans le
//  calque de l'objet.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

using Id = std::uint32_t;
inline constexpr Id kNoId = 0;

// ---------------------------------------------------------------- objets ----
enum class Kind : std::uint8_t {
    Text, Image, Button, Rectangle, Ellipse, Line, Polygon, Indicator, ProgressBar,
    Gauge, Table, History, Trend, List, Video, Container, Group,
    // Lot 6, A LA FIN (le fichier enregistre la cle, mais l'ordre de l'enum
    // indexe les tables) : le gestionnaire de recettes et l'image animee.
    RecipeManager, AnimatedImage,
    // Lot 8, A LA FIN toujours : le champ de saisie, et les cinq objets des
    // utilisateurs (connexion, deconnexion, utilisateur connecte, changer le
    // mot de passe, gestion des utilisateurs).
    InputField, LoginPanel, LogoutButton, UserInfo, PasswordChange, UserManager,
    // Lot 9, A LA FIN : les commandes (impulsion, interrupteur, bouton lumineux,
    // selecteur, curseur, potentiometre, liste deroulante, case a cocher,
    // boutons radio, date et heure, programmateur horaire) et les afficheurs
    // (numerique, voyant et texte multi-etats, bargraphe, thermometre, cadran,
    // horloge, compteur horaire, 7 segments, fleche de tendance, texte
    // defilant, code QR).
    PushButton, Switch, IlluminatedButton, Selector, Slider, Knob, ComboBox, CheckBox, RadioGroup,
    DateTimePicker, WeeklySchedule,
    NumericDisplay, MultiStateIndicator, MultiStateText, Bargraph, Thermometer, Dial, Clock, HourMeter,
    SevenSegment, TrendArrow, Marquee, QrCode,
    // Lot 10, A LA FIN : l'objet Parametres systeme (un clic ouvre le menu natif) ;
    // les 24 symboles de synoptique (process, electrique, stockage).
    SystemButton,
    Valve, Pump, Motor, Pipe, Tank, GasBottle, Fan, Compressor, HeatExchanger, Filter, Boiler,
    Conveyor, Cylinder, IsaInstrument, CircuitBreaker, Disconnector, Contactor, Lamp, Transformer,
    Silo, Hopper, Mixer, CheckValve, FlowArrow,
    // Lot 10 encore : l'instance d'un symbole reutilisable (HmiSymbols.hpp). Pas
    // dans la palette des genres : elle se pose depuis la section Symboles du
    // projet de la bibliotheque, ou nait de "Creer un symbole".
    SymbolInstance,
    // Lot 11, A LA FIN : les graphiques (barres, courbe XY, chronogramme d'etats,
    // camembert, radar, histogramme) ; les objets des alarmes (bandeau, compteur,
    // resume par zone, consigne, statistiques) ; la production (compteurs et TRS,
    // tableau de variables, editeur de recette, bouton d'export).
    BarChart, XYChart, StateChart, PieChart, RadarChart, Histogram,
    AlarmBanner, AlarmCounter, AlarmSummary, AlarmInstruction, AlarmStats,
    ProductionCounter, VariableTable, RecipeEditor, ExportButton,
    // Lot 12, A LA FIN : la navigation et la structure - barre de navigation, fil
    // d'Ariane, conteneur a onglets, cadre avec titre, panneau defilant, panneau
    // repliable, plan a zones.
    NavBar, Breadcrumb, TabContainer, Frame, ScrollPanel, CollapsiblePanel, ZoneMap,
    // Lot 12 : l'objet Menu de connexion (il ouvre le menu natif de connexion).
    LoginMenuButton,
    // Lot 13, A LA FIN : le selecteur de langue (un bouton par langue du projet),
    // le selecteur de theme (jour / nuit).
    LanguageSelector, ThemeSelector,
    // Lot 14, A LA FIN : l'etat de la communication (un voyant et une ligne), le
    // diagnostic de l'automate (la liaison Modbus en detail, deux boutons).
    CommStatus, PlcDiagnostic,
    // Lot 16, A LA FIN : le GIF anime (une ressource GIF jouee une fois, N fois ou
    // en boucle, a sa vitesse ; au lancement de la vue, sur action, sur condition).
    AnimatedGif,
    // 1.10.4, A LA FIN : la vanne 3 voies (melangeuse ou repartitrice, en T ou en
    // L ; une voie active parmi 1-2, 1-3, 2-3 ou fermee). Un symbole de
    // synoptique comme la vanne (kindIsSynoptic), pose a cote d'elle.
    ThreeWayValve,
};

// Tout ce qu'on peut poser sur une vue, dans l'ordre de la palette. Le groupe
// n'y est pas : il nait d'une selection.
inline constexpr Kind kPlaceableKinds[] = {
    Kind::Text,  Kind::Image,     Kind::Button,      Kind::Rectangle, Kind::Ellipse,
    Kind::Line,  Kind::Polygon,   Kind::Indicator,   Kind::ProgressBar, Kind::Gauge,
    Kind::Table, Kind::History,   Kind::Trend,       Kind::List,      Kind::Video,
    Kind::Container, Kind::RecipeManager, Kind::AnimatedImage,
    Kind::InputField, Kind::LoginPanel, Kind::LogoutButton, Kind::UserInfo, Kind::PasswordChange, Kind::UserManager,
    // lot 9
    Kind::PushButton, Kind::Switch, Kind::IlluminatedButton, Kind::Selector, Kind::Slider, Kind::Knob,
    Kind::ComboBox, Kind::CheckBox, Kind::RadioGroup, Kind::DateTimePicker, Kind::WeeklySchedule,
    Kind::NumericDisplay, Kind::MultiStateIndicator, Kind::MultiStateText, Kind::Bargraph, Kind::Thermometer,
    Kind::Dial, Kind::Clock, Kind::HourMeter, Kind::SevenSegment, Kind::TrendArrow, Kind::Marquee, Kind::QrCode,
    // lot 10 : l'objet Parametres systeme, les symboles de synoptique
    Kind::SystemButton,
    Kind::Valve, Kind::ThreeWayValve, Kind::Pump, Kind::Motor, Kind::Pipe, Kind::Tank, Kind::GasBottle, Kind::Fan,
    Kind::Compressor, Kind::HeatExchanger, Kind::Filter, Kind::Boiler, Kind::Conveyor, Kind::Cylinder, Kind::IsaInstrument,
    Kind::CircuitBreaker, Kind::Disconnector, Kind::Contactor, Kind::Lamp, Kind::Transformer, Kind::Silo, Kind::Hopper,
    Kind::Mixer, Kind::CheckValve, Kind::FlowArrow,
    // lot 11 : graphiques, alarmes, production
    Kind::BarChart, Kind::XYChart, Kind::StateChart, Kind::PieChart, Kind::RadarChart, Kind::Histogram,
    Kind::AlarmBanner, Kind::AlarmCounter, Kind::AlarmSummary, Kind::AlarmInstruction, Kind::AlarmStats,
    Kind::ProductionCounter, Kind::VariableTable, Kind::RecipeEditor, Kind::ExportButton,
    // lot 12 : navigation et structure
    Kind::NavBar, Kind::Breadcrumb, Kind::TabContainer, Kind::Frame, Kind::ScrollPanel, Kind::CollapsiblePanel, Kind::ZoneMap,
    Kind::LoginMenuButton,
    // lot 13 : la langue, le theme
    Kind::LanguageSelector, Kind::ThemeSelector,
    // lot 14 : la communication
    Kind::CommStatus, Kind::PlcDiagnostic,
    // lot 16 : le GIF anime
    Kind::AnimatedGif,
};
// Lot 8 : les objets qui repondent au clavier en marche (un champ a le focus).
[[nodiscard]] bool kindTakesText(Kind) noexcept;   // champ de saisie, connexion, changer le mot de passe
// Lot 9 : les commandes, qui ECRIVENT leur variable (propriete "variable" : la
// variable ecrite ; la permission Piloter sous securite) ; et les objets qui
// MONTRENT une valeur (propriete "value" ; sans expression, la variable).
[[nodiscard]] bool kindWritesVariable(Kind) noexcept;
[[nodiscard]] bool kindShowsValue(Kind) noexcept;
// Lot 10 : les symboles de synoptique (vanne, pompe... fleche de flux) - ils
// montrent aussi une valeur (leur etat, ou leur niveau) ; et ceux dont la
// valeur est un niveau entre min et max (cuve, bouteille, silo, tremie, verin).
//  En ligne : l'arbre du projet (app/ViewModels.cpp, par HmiPublicVars.hpp) s'en
//  sert sans lier la bibliotheque de l'IHM.
[[nodiscard]] inline bool kindIsSynoptic(Kind k) noexcept {
    return (static_cast<int>(k) >= static_cast<int>(Kind::Valve) && static_cast<int>(k) <= static_cast<int>(Kind::FlowArrow))
        || k == Kind::ThreeWayValve;   // 1.10.4
}
[[nodiscard]] inline bool kindHasLevel(Kind k) noexcept {
    return k == Kind::Tank || k == Kind::GasBottle || k == Kind::Silo || k == Kind::Hopper || k == Kind::Cylinder;
}
// Lot 11 : les graphiques (barres... histogramme) et les objets des alarmes
// (bandeau... statistiques). En ligne, pour la meme raison.
[[nodiscard]] inline bool kindIsChart(Kind k) noexcept {
    return static_cast<int>(k) >= static_cast<int>(Kind::BarChart) && static_cast<int>(k) <= static_cast<int>(Kind::Histogram);
}
[[nodiscard]] inline bool kindIsAlarmView(Kind k) noexcept {
    return static_cast<int>(k) >= static_cast<int>(Kind::AlarmBanner) && static_cast<int>(k) <= static_cast<int>(Kind::AlarmStats);
}
// Lot 12 : les objets de la navigation et de la structure (barre de navigation...
// plan a zones) ; et ceux qui TIENNENT d'autres objets - le conteneur, le
// conteneur a onglets, le cadre, les panneaux : un double-clic y entre, ce
// qu'on y pose devient leur enfant et les suit.
[[nodiscard]] inline bool kindIsNavigation(Kind k) noexcept {
    return static_cast<int>(k) >= static_cast<int>(Kind::NavBar) && static_cast<int>(k) <= static_cast<int>(Kind::ZoneMap);
}
[[nodiscard]] inline bool kindHoldsChildren(Kind k) noexcept {
    return k == Kind::Container || k == Kind::TabContainer || k == Kind::Frame || k == Kind::ScrollPanel
        || k == Kind::CollapsiblePanel;
}
// Lot 13 : les objets dont le geste peut demander une SIGNATURE ELECTRONIQUE
// (proprietes "signature" : aucune, simple, double ; "signatureReasons" : les
// motifs proposes ; "signatureLevel" : le niveau du second signataire). Le
// bouton (ses actions au clic), les bascules, les choix, le curseur, la date,
// le programmateur, le champ de saisie - pas l'impulsion (l'appui ecrit deja).
[[nodiscard]] inline bool kindSignable(Kind k) noexcept {
    return k == Kind::Button || k == Kind::Switch || k == Kind::CheckBox || k == Kind::IlluminatedButton || k == Kind::Selector
        || k == Kind::RadioGroup || k == Kind::ComboBox || k == Kind::Slider || k == Kind::Knob || k == Kind::DateTimePicker
        || k == Kind::WeeklySchedule || k == Kind::InputField;
}

// Le nom de chaque genre : sa cle (enregistree) et son libelle (affiche). Dans
// l'en-tete (lot 9) : l'arbre du projet (app/ViewModels.cpp) s'en sert sans
// lier la bibliotheque de l'IHM. L'ordre est celui de l'enum : on l'indexe.
struct KindInfo { Kind kind; std::string_view key; std::string_view label; };

inline constexpr KindInfo kKindInfos[] = {
    {Kind::Text,        "Text",        "Texte"},
    {Kind::Image,       "Image",       "Image"},
    {Kind::Button,      "Button",      "Bouton"},
    {Kind::Rectangle,   "Rectangle",   "Rectangle"},
    {Kind::Ellipse,     "Ellipse",     "Ellipse"},
    {Kind::Line,        "Line",        "Ligne"},
    {Kind::Polygon,     "Polygon",     "Polygone"},
    {Kind::Indicator,   "Indicator",   "Voyant"},
    {Kind::ProgressBar, "ProgressBar", "Barre de progression"},
    {Kind::Gauge,       "Gauge",       "Jauge"},
    {Kind::Table,       "Table",       "Tableau"},
    {Kind::History,     "History",     "Historique"},
    {Kind::Trend,       "Trend",       "Courbe"},
    {Kind::List,        "List",        "Liste"},
    {Kind::Video,       "Video",       "Vid\xC3\xA9o"},
    {Kind::Container,   "Container",   "Conteneur"},
    {Kind::Group,       "Group",       "Groupe"},
    {Kind::RecipeManager, "RecipeManager", "Gestion de recettes"},
    {Kind::AnimatedImage, "AnimatedImage", "Image anim\xC3\xA9" "e"},
    // lot 8
    {Kind::InputField,     "InputField",     "Champ de saisie"},
    {Kind::LoginPanel,     "LoginPanel",     "Connexion"},
    {Kind::LogoutButton,   "LogoutButton",   "D\xC3\xA9" "connexion"},
    {Kind::UserInfo,       "UserInfo",       "Utilisateur connect\xC3\xA9"},
    {Kind::PasswordChange, "PasswordChange", "Changer le mot de passe"},
    {Kind::UserManager,    "UserManager",    "Gestion des utilisateurs"},
    // lot 9 : commandes
    {Kind::PushButton,        "PushButton",        "Bouton \xC3\xA0 impulsion"},
    {Kind::Switch,            "Switch",            "Interrupteur"},
    {Kind::IlluminatedButton, "IlluminatedButton", "Bouton lumineux"},
    {Kind::Selector,          "Selector",          "S\xC3\xA9lecteur"},
    {Kind::Slider,            "Slider",            "Curseur"},
    {Kind::Knob,              "Knob",              "Potentiom\xC3\xA8tre"},
    {Kind::ComboBox,          "ComboBox",          "Liste d\xC3\xA9roulante"},
    {Kind::CheckBox,          "CheckBox",          "Case \xC3\xA0 cocher"},
    {Kind::RadioGroup,        "RadioGroup",        "Boutons radio"},
    {Kind::DateTimePicker,    "DateTimePicker",    "Date et heure"},
    {Kind::WeeklySchedule,    "WeeklySchedule",    "Programmateur horaire"},
    // lot 9 : afficheurs
    {Kind::NumericDisplay,      "NumericDisplay",      "Afficheur num\xC3\xA9rique"},
    {Kind::MultiStateIndicator, "MultiStateIndicator", "Voyant multi-\xC3\xA9tats"},
    {Kind::MultiStateText,      "MultiStateText",      "Texte multi-\xC3\xA9tats"},
    {Kind::Bargraph,            "Bargraph",            "Bargraphe"},
    {Kind::Thermometer,         "Thermometer",         "Thermom\xC3\xA8tre"},
    {Kind::Dial,                "Dial",                "Cadran \xC3\xA0 zones"},
    {Kind::Clock,               "Clock",               "Horloge"},
    {Kind::HourMeter,           "HourMeter",           "Compteur horaire"},
    {Kind::SevenSegment,        "SevenSegment",        "Afficheur 7 segments"},
    {Kind::TrendArrow,          "TrendArrow",          "Fl\xC3\xA8" "che de tendance"},
    {Kind::Marquee,             "Marquee",             "Texte d\xC3\xA9" "filant"},
    {Kind::QrCode,              "QrCode",              "Code QR"},
    // lot 10
    {Kind::SystemButton,        "SystemButton",        "Param\xC3\xA8tres syst\xC3\xA8me"},
    {Kind::Valve,               "Valve",               "Vanne"},
    {Kind::Pump,                "Pump",                "Pompe"},
    {Kind::Motor,               "Motor",               "Moteur"},
    {Kind::Pipe,                "Pipe",                "Tuyauterie"},
    {Kind::Tank,                "Tank",                "Cuve"},
    {Kind::GasBottle,           "GasBottle",           "Bouteille de gaz"},
    {Kind::Fan,                 "Fan",                 "Ventilateur"},
    {Kind::Compressor,          "Compressor",          "Compresseur"},
    {Kind::HeatExchanger,       "HeatExchanger",       "\xC3\x89" "changeur"},
    {Kind::Filter,              "Filter",              "Filtre"},
    {Kind::Boiler,              "Boiler",              "Chaudi\xC3\xA8re"},
    {Kind::Conveyor,            "Conveyor",            "Convoyeur"},
    {Kind::Cylinder,            "Cylinder",            "V\xC3\xA9rin"},
    {Kind::IsaInstrument,       "IsaInstrument",       "Instrument ISA"},
    {Kind::CircuitBreaker,      "CircuitBreaker",      "Disjoncteur"},
    {Kind::Disconnector,        "Disconnector",        "Sectionneur"},
    {Kind::Contactor,           "Contactor",           "Contacteur"},
    {Kind::Lamp,                "Lamp",                "Lampe"},
    {Kind::Transformer,         "Transformer",         "Transformateur"},
    {Kind::Silo,                "Silo",                "Silo"},
    {Kind::Hopper,              "Hopper",              "Tr\xC3\xA9mie"},
    {Kind::Mixer,               "Mixer",               "M\xC3\xA9langeur"},
    {Kind::CheckValve,          "CheckValve",          "Clapet anti-retour"},
    {Kind::FlowArrow,           "FlowArrow",           "Fl\xC3\xA8" "che de flux"},
    {Kind::SymbolInstance,      "SymbolInstance",      "Instance de symbole"},
    // lot 11
    {Kind::BarChart,            "BarChart",            "Graphique en barres"},
    {Kind::XYChart,             "XYChart",             "Courbe XY"},
    {Kind::StateChart,          "StateChart",          "Chronogramme d'\xC3\xA9tats"},
    {Kind::PieChart,            "PieChart",            "Camembert"},
    {Kind::RadarChart,          "RadarChart",          "Radar"},
    {Kind::Histogram,           "Histogram",           "Histogramme"},
    {Kind::AlarmBanner,         "AlarmBanner",         "Bandeau d'alarme"},
    {Kind::AlarmCounter,        "AlarmCounter",        "Compteur d'alarmes"},
    {Kind::AlarmSummary,        "AlarmSummary",        "R\xC3\xA9sum\xC3\xA9 par zone"},
    {Kind::AlarmInstruction,    "AlarmInstruction",    "Consigne d'alarme"},
    {Kind::AlarmStats,          "AlarmStats",          "Statistiques d'alarmes"},
    {Kind::ProductionCounter,   "ProductionCounter",   "Compteurs de production"},
    {Kind::VariableTable,       "VariableTable",       "Tableau de variables"},
    {Kind::RecipeEditor,        "RecipeEditor",        "\xC3\x89" "diteur de recette"},
    {Kind::ExportButton,        "ExportButton",        "Bouton d'export"},
    // lot 12
    {Kind::NavBar,              "NavBar",              "Barre de navigation"},
    {Kind::Breadcrumb,          "Breadcrumb",          "Fil d'Ariane"},
    {Kind::TabContainer,        "TabContainer",        "Conteneur \xC3\xA0 onglets"},
    {Kind::Frame,               "Frame",               "Cadre avec titre"},
    {Kind::ScrollPanel,         "ScrollPanel",         "Panneau d\xC3\xA9" "filant"},
    {Kind::CollapsiblePanel,    "CollapsiblePanel",    "Panneau repliable"},
    {Kind::ZoneMap,             "ZoneMap",             "Plan \xC3\xA0 zones"},
    {Kind::LoginMenuButton,     "LoginMenuButton",     "Menu de connexion"},
    // lot 13
    {Kind::LanguageSelector,    "LanguageSelector",    "S\xC3\xA9lecteur de langue"},
    {Kind::ThemeSelector,       "ThemeSelector",       "S\xC3\xA9lecteur de th\xC3\xA8me"},
    // lot 14
    {Kind::CommStatus,          "CommStatus",          "\xC3\x89tat de la communication"},
    {Kind::PlcDiagnostic,       "PlcDiagnostic",       "Diagnostic automate"},
    {Kind::AnimatedGif,         "AnimatedGif",         "GIF anim\xC3\xA9"},                        // lot 16
    {Kind::ThreeWayValve,       "ThreeWayValve",       "Vanne 3 voies"},                           // 1.10.4
};

static_assert(sizeof(kKindInfos) / sizeof(kKindInfos[0]) == static_cast<std::size_t>(Kind::ThreeWayValve) + 1, "une ligne par genre d'objet, dans l'ordre");
[[nodiscard]] inline std::string_view kindKey(Kind k) noexcept { return kKindInfos[static_cast<std::size_t>(k)].key; }      // "Rectangle" : enregistre
[[nodiscard]] inline std::string_view kindLabel(Kind k) noexcept { return kKindInfos[static_cast<std::size_t>(k)].label; }  // affiche
[[nodiscard]] std::optional<Kind>     kindFromKey(std::string_view) noexcept;

struct Prop {
    std::string key;     // "x", "fill", "text", "visible"...
    std::string value;   // la valeur statique, en texte
    std::string expr;    // l'expression ; vide = statique
    bool operator==(const Prop&) const = default;
};

struct Box {
    double x{0}, y{0}, w{0}, h{0};
    [[nodiscard]] double right() const noexcept { return x + w; }
    [[nodiscard]] double bottom() const noexcept { return y + h; }
    [[nodiscard]] double cx() const noexcept { return x + w / 2; }
    [[nodiscard]] double cy() const noexcept { return y + h / 2; }
    [[nodiscard]] bool   contains(double px, double py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    [[nodiscard]] bool   intersects(const Box& o) const noexcept {
        return !(o.x > right() || o.right() < x || o.y > bottom() || o.bottom() < y);
    }
    [[nodiscard]] Box    united(const Box& o) const noexcept;
    bool operator==(const Box&) const = default;
};

// --------------------------------------------------------------- actions ----
//  UNE ACTION = UN DECLENCHEUR + UNE OPERATION. "Au clic, naviguer vers
//  Vue_Donnees en glissant" ; "sur front montant de Armoires[0].bouteille_vide,
//  ouvrir la popup Alarme" ; "toutes les 5 s, journaliser {Niveau:0.0}".
//  Un objet en porte une liste ; la vue aussi (ouverture, fermeture, timer,
//  fronts : ce qui n'a pas besoin d'un objet). Elles s'executent dans l'ordre
//  de la liste.
enum class Trigger : std::uint8_t {
    Click, DoubleClick, RisingEdge, FallingEdge, LongPress, ValueChange, ViewOpen, ViewClose, Timer,
};
enum class Operation : std::uint8_t {
    Toggle, Set, Reset, Increment, Decrement, Assign, Navigate, Popup, ClosePopup, RunScript, CallScript, Log,
    AckAlarm, LoadRecipe, ChangeUser,
    // lot 6 : les ressources
    RequestResource, BindTable, PlaySound, ShowSystem,
    // lot 8 : les popups intelligentes, et la deconnexion
    ChangePopup, CenterPopup, PreviousPopup, CloseAllPopups, Logout,
    // lot 11 : mettre une alarme de cote (et l'y reprendre), faire taire le son
    // des alarmes, exporter des donnees (CSV, Excel, PDF)
    ShelveAlarm, UnshelveAlarm, SilenceAlarms, Export,
    // lot 12 : l'historique de navigation (vue precedente, vue suivante) et la vue
    // d'accueil (celle du groupe de l'utilisateur connecte, sinon la vue de demarrage)
    NavigateBack, NavigateForward, NavigateHome,
    // lot 12 : le menu natif de connexion (la valeur : l'onglet - Connexion,
    // Mon compte, Comptes, Acces, Journal)
    ShowLogin,
    // lot 13 : changer de langue (la cible : un code - en, de -, "suivante", ou
    // une expression qui donne le code) ; changer de theme (jour, nuit ; vide :
    // l'autre)
    SetLanguage, SetTheme,
    // lot 16 : un objet GIF anime de la vue (la cible : son nom) - le jouer, le
    // mettre en pause, l'arreter, le rejouer N fois (la valeur : N, 0 sans fin).
    GifPlay, GifPause, GifStop, GifReplay,
    // 1.9 : "Appliquer copie sur reference" (categorie popup) - la cible : le nom
    // d'un parametre de la popup en mode Les deux ; vide ou "*" : tous ceux en
    // mode Les deux. Ecrit la copie dans la variable de l'appelant.
    ApplyCopy,
    // 1.11.6 : Maths - la cible recoit une formule (value) calculee sur des
    // references nommees (params : "Mesure := Armoires[0].ana.PT1.mes; ...").
    // Clavier virtuel - un champ de saisie et son clavier ; ce qu'on tape va dans
    // la cible (params : titre, clavier, min, max, unite, masque). HmiActionKinds.hpp.
    Maths, Keyboard,
};
inline constexpr Trigger kTriggers[] = {
    Trigger::Click, Trigger::DoubleClick, Trigger::RisingEdge, Trigger::FallingEdge, Trigger::LongPress,
    Trigger::ValueChange, Trigger::ViewOpen, Trigger::ViewClose, Trigger::Timer,
};
inline constexpr Operation kOperations[] = {
    Operation::Toggle, Operation::Set, Operation::Reset, Operation::Increment, Operation::Decrement,
    Operation::Assign, Operation::Navigate, Operation::Popup, Operation::ClosePopup, Operation::RunScript,
    Operation::CallScript, Operation::Log, Operation::AckAlarm, Operation::LoadRecipe, Operation::ChangeUser,
    Operation::RequestResource, Operation::BindTable, Operation::PlaySound, Operation::ShowSystem,
    Operation::ChangePopup, Operation::CenterPopup, Operation::PreviousPopup, Operation::CloseAllPopups,
    Operation::Logout,
    Operation::ShelveAlarm, Operation::UnshelveAlarm, Operation::SilenceAlarms, Operation::Export,
    Operation::NavigateBack, Operation::NavigateForward, Operation::NavigateHome,
    Operation::ShowLogin,
    Operation::SetLanguage, Operation::SetTheme,
    Operation::GifPlay, Operation::GifPause, Operation::GifStop, Operation::GifReplay,   // lot 16
    Operation::ApplyCopy,                                                                // 1.9
    Operation::Maths, Operation::Keyboard,                                               // 1.11.6
};
// Lot 16 : les operations qui visent un GIF anime de la vue.
[[nodiscard]] constexpr bool operationTargetsGif(Operation o) noexcept {
    return o == Operation::GifPlay || o == Operation::GifPause || o == Operation::GifStop || o == Operation::GifReplay;
}
[[nodiscard]] std::string_view         triggerLabel(Trigger) noexcept;      // "Clic", "Front montant"... (enregistre)
[[nodiscard]] std::optional<Trigger>   triggerFromLabel(std::string_view) noexcept;
[[nodiscard]] std::string_view         operationLabel(Operation) noexcept;  // "Basculer", "Naviguer"... (enregistre)
[[nodiscard]] std::optional<Operation> operationFromLabel(std::string_view) noexcept;
// Ce que le declencheur surveille (une expression) ou attend (une duree).
[[nodiscard]] bool triggerWatches(Trigger) noexcept;    // front montant / descendant, changement de valeur
[[nodiscard]] bool triggerWaits(Trigger) noexcept;      // appui long, timer
// Ce que l'operation vise : une variable, une vue, un script ; et si elle a une valeur.
[[nodiscard]] bool operationWritesVariable(Operation) noexcept;
// 1.11.7 : la variable visee, telle qu'elle s'ecrit : un "=" tape devant (la case Variable
// a sa pastille fx, comme la Condition) est retire ("=Vanne.CMD_OUV" -> "Vanne.CMD_OUV").
[[nodiscard]] std::string targetVariable(std::string_view target);
[[nodiscard]] bool operationOpensView(Operation) noexcept;       // naviguer, popup, changer de popup
[[nodiscard]] bool operationHasValue(Operation) noexcept;        // increment, affectation, script, journal
// Lot 8 : ce qui passe des parametres a la vue ouverte (Naviguer, Ouvrir une
// popup, Changer de popup) : `value` porte "Moteur := Pompes[3]; Titre := 'P3'".
[[nodiscard]] bool operationTakesArguments(Operation) noexcept;
// Lot 8 : ou se pose une popup. "centre" (defaut), "objet" (sous l'objet
// clique), "haut-gauche", "haut-droite", "bas-gauche", "bas-droite",
// "derniere" (la ou elle etait), ou "x,y" (dans la vue du dessous).
inline constexpr std::string_view kPopupPlacements[] = {
    "centre", "objet", "haut-gauche", "haut-droite", "bas-gauche", "bas-droite", "derniere",
};
[[nodiscard]] std::string_view popupPlacementLabel(std::string_view placement) noexcept;   // "Centr\xC3\xA9e"...
[[nodiscard]] std::string      popupPlacementFromLabel(std::string_view label);          // l'inverse ; "x,y" tel quel

// Le passage d'une vue a l'autre (navigation, popup).
enum class TransitionKind : std::uint8_t { Instant, Fade, Slide, Zoom, Rotate, Custom };
inline constexpr TransitionKind kTransitionKinds[] = {
    TransitionKind::Instant, TransitionKind::Fade, TransitionKind::Slide, TransitionKind::Zoom,
    TransitionKind::Rotate, TransitionKind::Custom,
};
[[nodiscard]] std::string_view              transitionLabel(TransitionKind) noexcept;   // "Instantanee", "Fondu"... (accentues)
[[nodiscard]] std::optional<TransitionKind> transitionFromLabel(std::string_view) noexcept;

struct Transition {
    TransitionKind kind{TransitionKind::Instant};
    int            durationMs{400};
    // Glissement : gauche, droite, haut, bas (d'ou vient la nouvelle vue) ;
    // zoom : avant, arriere ; rotation : horaire, antihoraire.
    std::string    direction{"gauche"};
    // lineaire, acceleree, deceleree, douce, rebond, ou bezier(x1, y1, x2, y2).
    std::string    easing{"douce"};
    // Personnalisee : d'ou part la nouvelle vue (elle arrive a l'identite).
    // Decalages en fraction de la vue, echelle, angle en degres, opacite 0..1.
    double         fromOpacity{0}, fromOffsetX{0}, fromOffsetY{0}, fromScale{1}, fromAngle{0};
    bool operator==(const Transition&) const = default;
};

struct Action {
    Trigger     trigger{Trigger::Click};
    std::string watch;          // front montant / descendant, changement : l'expression surveillee
    int         delayMs{0};     // appui long : la duree ; timer : la periode
    std::string guard;          // condition facultative : l'action ne part que si elle est vraie
    Operation   operation{Operation::Set};
    std::string target;         // la variable, la vue, le script (selon l'operation) ; lot 6 :
                                // demander une ressource : la variable STRING qui recoit son nom ;
                                // lier un tableau : l'objet Tableau ; jouer un son : la ressource
    std::string value;          // increment : le pas ; affectation : l'expression ; script : le code ST ;
                                // journal : le message (textes a trous) ; lot 6 : demander une
                                // ressource : les extensions (png;jpg) ; lier un tableau : le
                                // fichier externe (vide : sa source de l'editeur)
    Transition  transition;     // naviguer, popup, fermer la popup
    // Lot 8 : ou se pose la popup (Ouvrir une popup) ; vide : son reglage.
    std::string placement;
    // ---- Lot API 8 : les exports qui demandent ou ----
    // Exporter des donnees : "Demander ou enregistrer" (coche par defaut ; absent
    // d'un projet d'avant : coche). La question ne se pose que pendant un geste
    // de l'operateur (Runtime::exportData) ; decoche : exports/ sans question.
    bool        askWhere{true};
    // 1.11.6 : les parametres de l'operation, "Nom := valeur; ..." - Maths : les
    // references de la formule ; Clavier virtuel : ses reglages (HmiActionKinds.hpp).
    std::string params;
    bool operator==(const Action&) const = default;
};
// "Clic -> Naviguer vers Vue_Donnees (Glissement, 400 ms)" : ce que montre la liste.
[[nodiscard]] std::string describeAction(const Action&);

// 1.9 : LA SURCHARGE D'UNE ALARME SUR UN OBJET POSE. Les alarmes d'un objet
// viennent de la bibliotheque (les alarmes par defaut d'un objet du synoptique)
// ou de son symbole (une instance) ; l'objet pose garde, pour chacune, les champs
// qu'on a changes chez lui - les autres suivent le symbole (ou la bibliotheque).
// `alarm` : le nom de l'alarme (Defaut, Niveau_Bas, celui du symbole) ; `path` :
// pour un objet pris dans une instance, le chemin de cet objet dans le symbole
// ("Moteur", "Groupe_1.Vanne" pour une instance imbriquee) ; vide : l'objet
// lui-meme. Un champ vide (std::nullopt) n'est pas surcharge.
struct AlarmOverride {
    std::string                alarm;
    std::string                path;
    std::optional<bool>        active;
    std::optional<std::string> condition;
    std::optional<std::string> message;
    std::optional<int>         priority;
    std::optional<std::string> category;
    std::optional<std::string> group;
    std::optional<int>         delayMs;
    std::optional<bool>        ackRequired;
    std::optional<std::string> instruction;
    std::optional<std::string> description;
    [[nodiscard]] bool empty() const noexcept {
        return !active && !condition && !message && !priority && !category && !group && !delayMs && !ackRequired
            && !instruction && !description;
    }
    bool operator==(const AlarmOverride&) const = default;
};

struct Object {
    Id          id{kNoId};
    Kind        kind{Kind::Rectangle};
    std::string name;
    Id          layer{kNoId};
    Id          parent{kNoId};    // le groupe (ou le conteneur) qui le tient ; kNoId = la vue
    bool        locked{false};    // verrouille : ni selection a la souris, ni deplacement
    bool        hidden{false};    // cache DANS L'EDITEUR ; la visibilite en marche est "visible"
    std::vector<Prop> props;
    std::vector<Action> actions;
    std::vector<AlarmOverride> alarmOverrides;   // 1.9 : ses alarmes surchargees (voir plus haut)

    [[nodiscard]] const Prop* find(std::string_view key) const noexcept;
    [[nodiscard]] Prop*       find(std::string_view key) noexcept;
    [[nodiscard]] std::string text(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] double      number(std::string_view key, double fallback = 0.0) const;
    [[nodiscard]] bool        flag(std::string_view key, bool fallback = false) const;
    [[nodiscard]] std::string expr(std::string_view key) const;
    void set(std::string_view key, std::string value);
    void setNumber(std::string_view key, double v);
    void setFlag(std::string_view key, bool v);
    void setExpr(std::string_view key, std::string expression);

    [[nodiscard]] Box    box() const;
    void                 setBox(const Box&);
    [[nodiscard]] double rotation() const { return number("rot"); }
    [[nodiscard]] bool   flipH() const { return flag("flipH"); }
    [[nodiscard]] bool   flipV() const { return flag("flipV"); }

    bool operator==(const Object&) const = default;
};

// ---------------------------------------------------------------- vues ------
// Lot 8 : UN PARAMETRE DE VUE. Une popup (ou une vue) peut declarer des noms
// que l'appelant relie : "Moteur", relie a Pompes[3] a l'ouverture, se lit
// Moteur.Marche dans ses expressions, ses textes, ses actions et ses scripts.
// `defaultValue` : ce qu'il vaut quand l'appelant ne le donne pas (et dans
// l'editeur) - un chemin (Pompes[0]) ou une valeur ('Pompe 1', 3).
// 1.9 : comment une popup recoit un parametre a son ouverture - REFERENCE : le
// nom designe la variable de l'appelant (on la lit et l'ecrit en direct) ; COPIE :
// une capture faite a l'ouverture, que la popup lit et modifie sans jamais
// toucher l'original ; LES DEUX : la capture, puis "Appliquer copie sur
// reference" ecrit la copie dans la variable de l'appelant.
enum class ParamMode : std::uint8_t { Reference, Copy, Both };
struct ViewParam {
    std::string name;
    std::string defaultValue;
    std::string description;
    std::string type;   // 1.9 : BOOL, INT... un type IHM, un DDT de l'API ; vide : ANY
    ParamMode mode{ParamMode::Reference};
    bool operator==(const ViewParam&) const = default;
};
// Lot 8 : comment une vue se montre quand on l'ouvre en popup.
struct PopupSettings {
    bool        titleBar{true};       // une barre de titre (on la tire pour deplacer)
    std::string title;                // vide : le nom de la vue
    bool        modal{true};          // le dessous s'assombrit et ne repond plus
    bool        movable{true};
    bool        closeButton{true};    // la croix de la barre de titre
    bool        closeOutside{false};  // un clic hors de la popup la ferme
    std::string placement{"centre"};  // voir kPopupPlacements
    bool operator==(const PopupSettings&) const = default;
};
inline constexpr double kPopupTitleHeight = 30.0;   // en pixels de vue

struct Layer {
    Id          id{kNoId};
    std::string name;
    bool        visible{true};
    bool        locked{false};
    bool operator==(const Layer&) const = default;
};

struct Guide {
    bool   vertical{true};
    double position{0};
    bool operator==(const Guide&) const = default;
};

struct GridSettings {
    bool visible{true};
    int  step{10};
    bool snapGrid{true};
    bool snapObjects{true};
    bool snapGuides{true};
    bool operator==(const GridSettings&) const = default;
};

enum class ScriptLang : std::uint8_t { ST, C, Cpp };
[[nodiscard]] std::string_view              scriptLangKey(ScriptLang) noexcept;
[[nodiscard]] std::optional<ScriptLang>     scriptLangFromKey(std::string_view) noexcept;

//  DEUX SORTES DE SCRIPTS, UNE FORME.
//    - de vue : `event` = OnOpen, OnCycle ou OnClose ;
//    - generaux (Programmation generale) : `event` = Demarrage (au lancement
//      de l'IHM), Cyclique (toutes les `periodMs`), Changement (quand
//      l'expression `watch` change de valeur) ou Appel (seulement appele : par
//      une action, ou par IHM_APPELER('nom') depuis un autre script).
//  ST s'execute en simulation ; C et C++ sont edites et verifies, pas executes.
struct Script {
    Id          id{kNoId};
    std::string name;
    ScriptLang  lang{ScriptLang::ST};
    std::string event;
    std::string body;
    int         periodMs{1000};
    std::string watch;
    std::string description;
    std::string folder{};           // lot 21 : son dossier dans la liste des scripts generaux ("" : la racine)
    bool operator==(const Script&) const = default;
};
inline constexpr std::string_view kViewEvents[] = {"OnOpen", "OnCycle", "OnClose"};
inline constexpr std::string_view kGeneralEvents[] = {"Demarrage", "Cyclique", "Changement", "Appel"};
[[nodiscard]] std::string_view eventLabel(std::string_view event) noexcept;   // "Demarrage", "Sur changement"... (accentues)

struct AlarmDef;   // 1.9 : plus bas (les alarmes) ; un symbole en declare

// ---- 1.10 : les operateurs d'un symbole ou d'un type IHM (decision 14) -------------
//  UN OPERATEUR EST DEFINI PAR UN SCRIPT. Un symbole (vue de role "symbole") et un
//  type IHM (une structure) portent une liste d'operateurs :
//    - des CONVERSIONS (`op` = "TO") : TO_REAL(v), TO_STRING(Pompe_1), TO_T_RESUME(v),
//      vers un type de base, un type IHM ou un DDT ; elles partent du porteur
//      (TO_REAL de T_VECTEUR) ou y arrivent (TO_T_VECTEUR d'un REAL) ;
//    - des OPERATEURS entre le porteur et lui-meme ou un type externe : + - * /
//      (un resultat), += -= *= /= (ils modifient la gauche, sans resultat),
//      = <> < > <= >= (un BOOL). L'ordre compte : T_VECTEUR * REAL et
//      REAL * T_VECTEUR sont deux operateurs.
//  Le corps est du ST du dialecte IHM, comme une fonction IHM : les operandes
//  s'appellent A (la gauche, ou le seul d'une conversion) et B (la droite) ; il rend
//  son resultat en l'affectant au nom de sa fonction (TO_STRING, ADD, SUB, MUL, DIV,
//  EQ, NE, LT, GT, LE, GE) ou par RETURN. Le reste : HmiOperators.hpp.
struct HmiOperator {
    Id          id{kNoId};
    std::string op;               // "TO" : une conversion ; "+", "-", "*", "/", "+=", "-=", "*=", "/=", "=", "<>", "<", ">", "<=", ">="
    std::string left;             // le type de l'operande de gauche (d'une conversion : sa source)
    std::string right{};          // le type de l'operande de droite ; vide : une conversion
    std::string result{};         // le type rendu (d'une conversion : sa cible) ; vide : += -= *= /=
    std::string body{};           // le script
    std::string description{};
    bool operator==(const HmiOperator&) const = default;
};

struct View {
    Id          id{kNoId};
    std::string name;
    std::string description;
    int         width{1920};
    int         height{1080};
    std::string background{"#20242B"};
    std::vector<Layer>  layers;        // du bas vers le haut
    Id                  activeLayer{kNoId};
    std::vector<Object> objects;       // ordre de dessin a l'interieur d'un calque
    std::vector<Script> scripts;
    std::vector<Action> actions;       // de la vue : ouverture, fermeture, timer, fronts
    std::vector<Guide>  guides;
    GridSettings        grid;

    // ---- lot 6 : ecrans modeles, en-tetes et pieds de page ----------------------
    //  `role` : "vue" (ordinaire), "modele" (un ecran modele : une vue en herite,
    //  il se dessine dessous et ses scripts passent avant les siens), "entete" ou
    //  "pied" (un modele d'en-tete / de pied de page : une bande de la hauteur de
    //  la vue-modele, posee en haut / en bas des vues qui cochent la case) ;
    //  lot 8 : "popup" (une vue faite pour s'ouvrir par-dessus : rangee dans le
    //  dossier Popups, reglee par `popup`).
    std::string role{"vue"};
    Id          templateView{kNoId};   // l'ecran modele dont elle herite
    bool        showHeader{false};     // la case "En-tete"
    Id          header{kNoId};         // son modele ; kNoId : le premier en-tete du projet
    bool        showFooter{false};     // la case "Pied de page"
    Id          footer{kNoId};
    // ---- lot 8 : les popups ("popup" : un nouveau role) et les parametres -----------
    PopupSettings          popup;
    std::vector<ViewParam> params;
    // ---- lot 12 : la navigation ---------------------------------------------------------
    //  `upView` : la vue parente (Accueil pour Ligne_1) - le fil d'Ariane en
    //  hierarchie remonte cette chaine ; kNoId : une racine. `zoomable` : en
    //  marche, la molette zoome et le fond se tire (une grande vue, un plan).
    Id                     upView{kNoId};
    bool                   zoomable{false};
    // ---- lot 21 : son dossier dans sa liste (Vues, Popups, Symboles...) ; "" : la racine
    std::string            folder{};
    // ---- 1.9 : les alarmes d'un symbole (role "symbole") : leurs conditions et leurs
    //  textes citent les parametres ; chaque instance posee les recoit, developpees
    //  (HmiObjectAlarms.hpp). Vide pour les autres vues.
    std::vector<AlarmDef>  alarms;
    // ---- 1.10 : les operateurs d'un symbole (voir HmiOperator). Vide pour les autres vues.
    std::vector<HmiOperator> operators{};
    [[nodiscard]] const ViewParam* param(std::string_view name) const noexcept;   // sans casse

    [[nodiscard]] Object*       object(Id) noexcept;
    [[nodiscard]] const Object* object(Id) const noexcept;
    [[nodiscard]] Object*       objectByName(std::string_view) noexcept;
    [[nodiscard]] const Object* objectByName(std::string_view) const noexcept;   // lot 10
    [[nodiscard]] Layer*        layer(Id) noexcept;
    [[nodiscard]] const Layer*  layer(Id) const noexcept;
    [[nodiscard]] int           layerRank(Id) const noexcept;    // -1 : inconnu
    [[nodiscard]] int           indexOf(Id) const noexcept;      // dans `objects`, -1 sinon
    [[nodiscard]] std::vector<Id> childrenOf(Id parent) const;   // dans l'ordre de dessin
    [[nodiscard]] std::vector<Id> descendantsOf(Id parent) const;
    // Du dessous vers le dessus : calques dans l'ordre, puis ordre du vecteur.
    [[nodiscard]] std::vector<const Object*> paintOrder() const;
    // Le calque et le verrou qui comptent pour la souris : l'objet OU son
    // calque OU un groupe parent.
    [[nodiscard]] bool effectivelyLocked(const Object&) const noexcept;
    [[nodiscard]] bool effectivelyHidden(const Object&) const noexcept;

    bool operator==(const View&) const = default;
};

// Lot 8 : comment une vue se montre ouverte en popup. Une vue de role Popup :
// ses reglages (barre de titre, place, modale...). Une autre vue (n'importe
// laquelle peut s'ouvrir par-dessus) : comme avant le lot 8 - centree, modale,
// sans barre de titre ; on la ferme par ses boutons.
[[nodiscard]] inline PopupSettings popupSettingsOf(const View& v) {
    if (v.role == "popup") return v.popup;
    PopupSettings plain;
    plain.titleBar = false;
    plain.movable = false;
    plain.closeButton = false;
    return plain;
}

// ------------------------------------------------------------ ressources ----
//  UNE RESSOURCE EST UN FICHIER QUE LE PROJET POSSEDE : une image, un son, une
//  video, une police ; ou (lot API 8) un document garde tel quel (PDF, DOCX,
//  ZIP... : format = son extension). Importee, elle est copiee dans le projet (enregistree
//  dans ihm/ressources/) : le projet ne depend plus de l'endroit d'ou elle
//  vient. Les objets la citent PAR SON NOM ("logo_site.png") ; la renommer
//  met a jour les objets qui la citent.
//
//  Le contenu est garde en memoire, partage et jamais modifie : une commande
//  annulable copie un pointeur, pas le fichier. Remplacer une ressource, c'est
//  lui donner un autre contenu.
struct Resource {
    Id            id{kNoId};
    std::string   name;              // ce que les objets citent
    std::string   format;            // "PNG", "JPEG", "SVG", "WAV", "MP4", "TTF"...
    std::string   origin;            // le fichier importe (pour memoire)
    std::string   added;             // la date d'import
    std::uint64_t bytes{0};
    int           width{0}, height{0};
    double        seconds{0};
    std::string   detail;            // codec, frequence, famille de la police...
    BlobPtr       data;              // nul : le fichier manque sur le disque
    std::string   folder{};          // lot 21 : son dossier dans la liste des ressources
    [[nodiscard]] MediaKind kind() const noexcept { return kindOfFormat(format); }
    bool operator==(const Resource&) const = default;
};

// ------------------------------------------------------- fichiers externes ---
//  UN FICHIER EXTERNE RESTE OU IL EST : un classeur de consignes, un journal
//  CSV, une base SQLite qu'un autre outil ecrit. Le projet en garde le chemin et
//  ce qu'il a vu en le liant (taille, date) : l'etat dit s'il est toujours la,
//  et s'il a change depuis. Un Tableau peut en montrer le contenu.
// ---- Lot API 8 : glisser de fichiers, 2e partie ---- Document (en fin : rien ne
// se renumerote) : tout autre fichier (PDF, DOCX, ZIP, image...), cite sans etre
// lu ; le programme du systeme l'ouvre. Sur le disque : type="Document".
enum class ExternalKind : std::uint8_t { Excel, Csv, Text, Json, Xml, Sqlite, Database, Document };
[[nodiscard]] std::string_view            externalKindKey(ExternalKind) noexcept;   // "CSV", "Excel"...
[[nodiscard]] std::optional<ExternalKind> externalKindFromKey(std::string_view) noexcept;
// L'extension decide ; une autre extension (ou aucune) : Document. Un chemin vide : rien.
[[nodiscard]] std::optional<ExternalKind> externalKindFromPath(std::string_view) noexcept;

struct ExternalFile {
    Id            id{kNoId};
    std::string   name;
    ExternalKind  kind{ExternalKind::Csv};
    std::string   path;              // un fichier : chemin absolu, ou relatif au dossier du projet ;
                                     // une base externe : sa chaine de connexion
    std::string   part;              // Excel : l'onglet ; SQLite : la table ; vide : le premier
    std::uint64_t bytes{0};          // vus en liant (ou en reliant)
    std::string   modified;          // "2026-09-22 01:10:05"
    std::string   linked;
    std::string   description;
    bool operator==(const ExternalFile&) const = default;
};

// Tout ce que le projet IHM possede hors des vues. Une commande de projet le
// garde en entier quand il change : quelques Ko, les contenus etant partages.
struct Assets {
    std::vector<Resource>     resources;
    std::vector<ExternalFile> files;
    bool operator==(const Assets&) const = default;
};

// -------------------------------------------------- programmation generale ---
// Lot 16 : l'adresse d'un membre d'une structure liee, corrigee a la main.
struct MemberAddress {
    std::string path;              // relatif a la variable : "Heures", "Vannes[2].Position"
    std::string address;           // "43020", "%MW3019"
    bool operator==(const MemberAddress&) const = default;
};

//  LES VARIABLES IHM : ce que l'IHM garde pour elle (un mode d'affichage, un
//  compteur de clics, la derniere vue...). Les scripts et les expressions les
//  lisent et les ecrivent comme une variable de l'automate ; un nom de
//  l'automate reste celui de l'automate.
struct Variable {
    Id          id{kNoId};
    std::string name;
    std::string type{"INT"};       // BOOL, INT, UINT, WORD, DINT, UDINT, DWORD, REAL, LREAL, STRING, TIME
    std::string initial{"0"};
    std::string description;
    // Lot 15 : LIEE A UN EQUIPEMENT (Configuration > Equipements) - sa valeur est
    // lue, et ecrite, a cette adresse de l'equipement ; `equipment` vide : elle
    // reste dans l'IHM. La mise a l'echelle : brut [rawMin, rawMax] -> [engMin,
    // engMax] (0..27648 -> 0..10 bar) ; rawMin == rawMax : aucune.
    // (Des initialiseurs, meme vides : Variable{id, nom, type, initiale,
    // description} reste complet pour -Wmissing-field-initializers.)
    std::string equipment{};
    std::string address{};         // %MW100, %MF20, %M5, %MW10.3, 40101, 4x0101, 30001, 00017, 10005
    bool        readOnly{false};
    double      rawMin{0}, rawMax{0}, engMin{0}, engMax{0};
    std::string rawType{};         // mise a l'echelle : le type du registre (INT, UINT, DINT...) ; vide : INT
    // Lot 16 : LE RANGEMENT ET LES TYPES COMPOSES.
    //  - `folder` : le dossier ou elle est rangee ("Ligne/Convoyeur"), comme un
    //    filtre de Visual Studio - sans effet sur son nom ; vide : a la racine ;
    //  - `type` peut etre un type IHM (une structure, "T_Four") ou un tableau a
    //    une ou deux dimensions ("ARRAY[0..9] OF REAL", "ARRAY[0..3, 0..9] OF INT",
    //    "ARRAY[1..4] OF T_Four") ; hmi::types (HmiTypes.hpp) les deplie ;
    //  - liee a un equipement, une structure ou un tableau part de `address` et
    //    ses membres suivent (REAL et DINT 2 mots, INT 1, STRING 16) ; les BOOL
    //    a la suite se rangent 16 par mot (`packBools`), sinon un mot chacun ;
    //    `places` corrige l'adresse d'un membre ("Heures" -> "43020").
    std::string folder{};
    bool        packBools{true};
    std::vector<MemberAddress> places{};
    // 1.11.8 (« chaque membre peut choisir s'il est interne ou attribue a l'equipement de la
    // structure ») : LES MEMBRES INTERNES d'une variable liee - ils restent dans l'IHM (une
    // variable IHM locale), l'equipement ne les lit ni ne les ecrit. Un chemin relatif a la
    // variable, comme `places` : "[0].NOM" (une case), "[*].NOM" (le membre NOM de toutes les
    // cases), "Vannes[2]" ou "Defaut" (le membre, et tout ce qui est dessous).
    std::vector<std::string> internal{};
    // 1.11.8 (« Recalculer la place memoire ») : vrai - un mot que n'occupent que des membres
    // internes est rendu et les membres suivants se resserrent ; faux : leur place reste reservee.
    bool        compact{false};
    [[nodiscard]] bool bound() const noexcept { return !equipment.empty(); }
    [[nodiscard]] bool scaled() const noexcept { return rawMax != rawMin; }
    bool operator==(const Variable&) const = default;
};
inline constexpr std::string_view kVariableTypes[] = {"BOOL", "INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL", "LREAL", "STRING", "TIME"};

// ---- lot 16 : les types IHM (structures) ---------------------------------------
//  UN TYPE IHM EST UNE STRUCTURE, comme un DDT de l'automate : des membres, chacun
//  d'un type elementaire, d'un autre type IHM ou un tableau ("ARRAY[1..3] OF
//  T_Vanne"). Un type ne se contient pas lui-meme (Generer le refuse). Une
//  variable IHM de ce type a tous ses membres : Four1.Temperature,
//  Four1.Vannes[2].Position.
struct TypeMember {
    std::string name;
    std::string type{"INT"};
    std::string initial{};         // vide : 0, FALSE, '' (ou celles du type)
    std::string description{};
    bool operator==(const TypeMember&) const = default;
};
// 1.10 (decision 15) : une valeur d'enumeration IHM
//  UNE ENUMERATION IHM (T_MODE) est le troisieme genre de type IHM : des valeurs
//  ordonnees (Arret = 0, Auto = 1, Manu = 2, Defaut = 9), chacune un nom ST, un
//  nombre (DINT), un texte affiche et une description ; ni membres ni adresse.
//  Son toString et son fromString sont deux operateurs de conversion (S2) crees
//  avec elle et preremplis : TO_STRING(T_MODE) : STRING et TO_T_MODE(STRING).
//  Les fonctions d'aide : HmiEnums.hpp.
struct HmiEnumValue {
    std::string  name;              // l'identifiant ST : Auto
    std::int64_t value{0};          // sa valeur (DINT)
    std::string  text{};            // le texte affiche ; vide : le nom
    std::string  description{};
    bool operator==(const HmiEnumValue&) const = default;
};
enum class HmiTypeKind : std::uint8_t { Structure, Enumeration };
struct HmiType {
    Id          id{kNoId};
    std::string name;
    std::string description{};
    std::vector<TypeMember> members{};
    std::string folder{};          // lot 21 : son dossier dans la liste des types
    std::vector<HmiOperator> operators{};   // 1.10 : ses operateurs (voir HmiOperator)
    HmiTypeKind kind{HmiTypeKind::Structure};   // 1.10 : une enumeration a des values, pas de members
    std::vector<HmiEnumValue> values{};         // 1.10 : ses valeurs, dans l'ordre de declaration
    bool operator==(const HmiType&) const = default;
};

// ---- lot 7 : les fonctions IHM ---------------------------------------------------
//  UNE FONCTION IHM : un nom, un type de retour (vide : sans retour, une
//  procedure), un corps ST qui declare ses parametres (VAR_INPUT) et ses
//  variables (VAR, VAR_TEMP), comme une FUNCTION de l'automate. Elle s'appelle
//  depuis un script, une expression de vue, une action : Moyenne(a, b) ou
//  Moyenne(a := 1.5, b := 2.0) ; elle donne sa valeur en l'affectant a son nom.
struct HmiFunction {
    Id          id{kNoId};
    std::string name;
    std::string returnType;       // BOOL, INT, REAL, STRING... ; "" : sans retour
    std::string body;
    std::string description;
    bool operator==(const HmiFunction&) const = default;
};

struct Programs {
    std::vector<Script>   scripts;     // les scripts generaux
    std::vector<Variable> variables;
    std::vector<HmiFunction> functions;   // lot 7
    // Lot 16 : les types IHM, et les dossiers des variables ("Ligne",
    // "Ligne/Convoyeur") - un dossier vide existe tant qu'il est dans la liste.
    std::vector<HmiType>     types{};
    std::vector<std::string> folders{};
    bool operator==(const Programs&) const = default;
};

// -------------------------------------------------------------- projet ------
struct Config {
    std::string name{"IHM"};
    std::string description;
    std::string version{"1.0.0"};
    std::string author;
    std::string created;
    std::string modified;
    int         width{1920};
    int         height{1080};
    std::string orientation{"Paysage"};   // Paysage, Portrait
    Id          startView{kNoId};
    int         cycleMs{100};             // le cycle IHM : OnCycle, fronts, timers
    // Lot 12 : en marche, un glisser horizontal rapide sur le fond de la vue
    // (ecran tactile) passe a la vue suivante ou precedente.
    bool        swipeNavigation{false};
    // Lot 13 : Generer plus exigeant (HmiQuality.hpp) - la case Qualite, et les
    // seuils (0 : pas de controle) : la plus petite cible tactile (px), le
    // contraste minimal d'un texte sur son fond (WCAG : 4,5 ; 3 pour un grand
    // texte), le nombre d'objets d'une vue chargee.
    bool        quality{true};
    int         touchMin{32};
    double      contrastMin{3.0};
    int         heavyObjects{400};
    // Lot 13 : l'affichage au lancement de l'IHM (HmiDisplay.hpp) - la taille des
    // textes (en %), les couleurs ("normal", ou "daltonien" : bleu et orange au
    // lieu de vert et rouge), des symboles sur les voyants (pas seulement la
    // couleur), le theme ("nuit" : les couleurs de la conception ; "jour" : claires).
    // En marche, le menu Parametres systeme et SYS.TextScale, SYS.ColorMode,
    // SYS.StatusSymbols, SYS.Theme les changent.
    int         textScale{100};
    std::string colorMode{"normal"};
    bool        statusSymbols{false};
    std::string theme{"nuit"};
    bool operator==(const Config&) const = default;
};

// ------------------------------------------------------------- alarmes -----
//  UNE ALARME = UNE CONDITION SURVEILLEE. Vraie : l'alarme apparait (horodatee,
//  son message rempli a cet instant) ; l'operateur l'acquitte ; fausse, elle
//  disparait. Elle quitte la liste des alarmes en cours quand elle est a la fois
//  disparue et acquittee (ou sans acquittement demande) : elle passe alors a
//  l'historique, avec ses trois heures.
//
//  PRIORITE 1 a 4 (1 = critique) ; CATEGORIE : Defaut, Alarme, Avertissement,
//  Information ; GROUPE : libre (une zone, une armoire) - l'acquittement d'un
//  groupe acquitte ses alarmes.
struct AlarmDef {
    Id          id{kNoId};
    std::string name;             // identifiant : Bouteille_Vide_A
    std::string condition;        // expression booleenne
    std::string message;          // texte a trous : "Bouteille A vide ({Armoires[0].ana.PT1.mes:0.0} bar)"
    int         priority{3};
    std::string category{"Alarme"};
    std::string group;
    bool        ackRequired{true};
    int         delayMs{0};       // la condition doit tenir ce temps avant l'apparition
    std::string description;
    // Lot 11 : la CONSIGNE - ce que fait l'operateur quand elle apparait ("Changer
    // la bouteille A ; verifier le detendeur."). L'objet Consigne d'alarme la montre.
    std::string instruction;
    bool operator==(const AlarmDef&) const = default;
};
inline constexpr int kAlarmPriorities = 4;
// Lot 11 : ce qui vaut pour toutes les alarmes - le son de chaque priorite (une
// ressource ; vide : aucun), joue a l'apparition et repete toutes les `repeatS`
// secondes tant qu'une alarme de cette priorite attend son acquittement (0 :
// une fois) ; la plus longue mise de cote permise.
struct AlarmSettings {
    std::array<std::string, kAlarmPriorities> sounds{};
    int  repeatS{0};
    int  maxShelveMin{480};
    bool operator==(const AlarmSettings&) const = default;
};
// 1.10.2 : UN GROUPE D'ALARMES (IHM > Alarmes > Groupes) et ses reglages. Les
// groupes deja nommes par les alarmes (AlarmDef::group) en deviennent, avec les
// reglages par defaut ci-dessous, qui ne changent rien (hmi::ensureAlarmGroups,
// HmiAlarmGroups.hpp, ou vivent aussi la recherche, les liens et ce qui gagne).
//   priority : la priorite par defaut (1 a 4) ; 0 : celle de l'alarme ;
//   colors   : active, acquittee, disparue ("#RRGGBB") ; vides : celles de la priorite ;
//   ack      : un par un (comme avant), par groupe (acquitter une alarme acquitte le
//              groupe), automatique au retour (rien a acquitter) ;
//   sound    : une ressource ; vide : le son de la priorite (AlarmSettings::sounds) ;
//   zone     : le resume par zone ; vide : le nom du groupe ;
//   level    : le niveau d'acces pour acquitter (1 a 4) ; 0 : quiconque peut acquitter ;
//   archive  : l'historique garde ses alarmes (oui par defaut).
enum class AlarmAckMode : std::uint8_t { Single, Group, Auto };
struct AlarmGroupDef {
    Id           id{kNoId};
    std::string  name;
    std::string  description;
    int          priority{0};
    std::string  colorActive, colorAcked, colorCleared;
    AlarmAckMode ack{AlarmAckMode::Single};
    std::string  sound;
    std::string  zone;
    int          level{0};
    bool         archive{true};
    bool operator==(const AlarmGroupDef&) const = default;
};
// 1.10.2 : UN LIEN d'un groupe d'alarmes des objets (le groupe interne "Vue.Objet",
// "Vue.Instance.Objet", ou un motif "Vue.*", "symbole:Sym_Pompe") vers un groupe de
// IHM > Alarmes : ses alarmes deviennent des alarmes de ce groupe. Ce qui gagne : la
// surcharge de l'objet, puis le lien (le groupe general), puis le symbole.
struct AlarmGroupLink {
    std::string objectGroup;
    std::string group;
    bool operator==(const AlarmGroupLink&) const = default;
};
[[nodiscard]] std::string_view alarmPriorityLabel(int priority) noexcept;   // "Critique", "Haute", "Moyenne", "Basse"
[[nodiscard]] const std::vector<std::string>& alarmCategories();           // Defaut, Alarme, Avertissement, Information

// ------------------------------------------------------------- recettes ----
//  UNE RECETTE = DES ELEMENTS (une variable chacun, une unite, des bornes) ET
//  DES JEUX DE VALEURS. Appliquer un jeu ecrit ses valeurs dans les variables ;
//  lire un jeu fait l'inverse ; comparer montre ce qui differe.
struct RecipeField {
    std::string name;             // "Pression de consigne"
    std::string variable;         // Armoires[0].seuil_poids_saisi, ou une variable IHM
    std::string unit;
    std::string min, max;         // vides : sans borne
    bool operator==(const RecipeField&) const = default;
};
struct RecipeRecord {
    Id                       id{kNoId};
    std::string              name;       // "Azote"
    std::vector<std::string> values;     // une par element, dans l'ordre des elements
    std::string              description;
    std::string              modified;   // horodatage de la derniere modification
    bool operator==(const RecipeRecord&) const = default;
};
struct Recipe {
    Id                        id{kNoId};
    std::string               name;
    std::string               description;
    std::vector<RecipeField>  fields;
    std::vector<RecipeRecord> records;
    [[nodiscard]] const RecipeRecord* record(std::string_view name) const noexcept;
    [[nodiscard]] RecipeRecord*       record(Id) noexcept;
    [[nodiscard]] const RecipeRecord* record(Id) const noexcept;
    bool operator==(const Recipe&) const = default;
};

// -------------------------------------------------------- utilisateurs -----
//  DES PERMISSIONS, REUNIES EN ROLES, DONNEES A DES GROUPES, AUXQUELS LES
//  UTILISATEURS APPARTIENNENT. Un groupe a aussi un NIVEAU (1 a 4) : un objet
//  dont le niveau d'acces depasse celui de l'utilisateur ne repond pas.
//
//  TROIS PROTECTIONS pour se connecter :
//    classique  : un mot de passe, garde sous forme d'empreinte salee (SHA-256
//                 iteree) - jamais en clair ;
//    dynamique  : un code a 6 chiffres qui change toutes les 30 s, calcule
//                 depuis un secret partage (TOTP, HMAC-SHA-256) ;
//    expression : pas de mot de passe ; la connexion est permise tant qu'une
//                 expression est vraie (une cle, un badge : Cle_Maintenance).
struct Role {
    std::string              name;
    std::vector<std::string> permissions;
    std::string              description;
    bool operator==(const Role&) const = default;
};
struct UserGroup {
    Id                       id{kNoId};
    std::string              name;
    int                      level{1};
    std::vector<std::string> roles;
    std::string              description;
    // Lot 12 : la vue ouverte quand un utilisateur du groupe se connecte (et au
    // lancement, pour l'utilisateur de depart) ; kNoId : la vue de demarrage.
    Id                       startView{kNoId};
    bool operator==(const UserGroup&) const = default;
};
struct User {
    Id          id{kNoId};
    std::string login;
    std::string fullName;
    Id          group{kNoId};
    std::string protection{"classique"};   // classique, dynamique, expression
    std::string passwordHash;              // hexa ; vide : pas de mot de passe
    std::string salt;                      // hexa
    std::string secret;                    // hexa : le secret du code dynamique
    std::string expression;                // l'autorisation par expression
    bool        enabled{true};
    std::string description;
    // Lot 13 : la politique des mots de passe - le jour ou le mot de passe a ete
    // donne ("2026-09-25" ; vide : inconnu), les empreintes des precedents
    // ("sel:empreinte", le plus recent d'abord), un changement exige a la
    // prochaine connexion ; le badge ("sel:empreinte" de son numero, jamais en
    // clair ; vide : aucun).
    std::string              passwordSet;
    std::vector<std::string> previous;
    bool                     mustChange{false};
    std::string              badge;
    bool operator==(const User&) const = default;
};
struct Security {
    bool                   enabled{false};     // eteinte : tout est permis
    std::vector<Role>      roles;
    std::vector<UserGroup> groups;
    std::vector<User>      users;
    std::string            startUser;          // connecte au lancement ; vide : personne (niveau 0)
    int                    dynamicPeriodS{30};
    int                    dynamicDigits{6};
    int                    autoLogoutMin{0};   // 0 : jamais
    // Lot 12 : le menu natif de connexion - le niveau a partir duquel chaque
    // onglet se voit (Comptes, Acces, Journal) ; la permission Administrer les
    // montre tous. Y changer quelque chose demande toujours Administrer.
    int                    menuLevelAccounts{4};
    int                    menuLevelAccess{4};
    int                    menuLevelJournal{3};
    // Lot 13 : LA POLITIQUE DES MOTS DE PASSE - chaque regle a 0 (ou faux) ne
    // joue pas. La longueur minimale s'ajoute a celle de chaque endroit (4 dans
    // l'editeur, 6 en marche) : la plus grande des deux compte.
    int                    pwMinLength{0};
    bool                   pwDigit{false};       // au moins un chiffre
    bool                   pwLetter{false};      // au moins une lettre
    bool                   pwMixedCase{false};   // des majuscules et des minuscules
    bool                   pwSpecial{false};     // au moins un caractere qui n'est ni lettre ni chiffre
    int                    pwMaxAgeDays{0};      // perime au bout de N jours (0 : jamais)
    int                    pwHistory{0};         // les N derniers ne reviennent pas
    bool                   pwChangeFirst{false}; // donne par un administrateur : a changer a la premiere connexion
    // Le verrouillage : N echecs de suite verrouillent le compte (0 : jamais) pour
    // M minutes (0 : jusqu'a ce qu'un administrateur le deverrouille).
    int                    lockAttempts{0};
    int                    lockMinutes{15};
    // Avant la deconnexion automatique : un bandeau compte les N dernieres
    // secondes (0 : sans avertissement) ; un toucher le fait partir.
    int                    logoutWarnS{30};
    // Un lecteur de badge (il tape le numero puis Entree, comme un clavier)
    // connecte le compte dont c'est le badge.
    bool                   badgeLogin{false};
    [[nodiscard]] static Security defaults();  // 4 roles, les 4 groupes de la specification
    bool operator==(const Security&) const = default;
};
[[nodiscard]] const std::vector<std::string>& permissionNames();   // Naviguer, Piloter, Acquitter, Recettes, Scripts, Administrer
[[nodiscard]] const std::vector<std::string>& protectionNames();   // classique, dynamique, expression

// ------------------------------------------------------------ historiques ---
//  CE QUI EST GARDE DE LA MARCHE (ihm/historique/) : les alarmes terminees,
//  les evenements, le journal systeme, et les mesures des variables archivees
//  (les courbes en mode historique les relisent).
struct HistorySettings {
    bool                     alarms{true};
    bool                     events{true};
    bool                     system{true};
    int                      maxEntries{2000};     // par liste ; les plus anciennes tombent
    int                      retentionDays{30};
    int                      samplePeriodMs{1000};
    std::vector<std::string> archived;             // les variables (expressions) mesurees
    // Lot 13 : le journal d'audit (ihm/historique/audit.csv) - qui a change quoi,
    // quand, ou, avant et apres, pourquoi ; chaque ligne chainee a la precedente
    // par son empreinte. Il ne suit pas la conservation : il a sa propre limite.
    bool                     audit{false};
    bool operator==(const HistorySettings&) const = default;
};

// ------------------------------------------------------------ lot 12 : styles ---
//  UN STYLE NOMME : des proprietes d'apparence (fill, stroke, textColor,
//  fontSize, radius...) sous un nom (Titre, Bouton principal, Alarme). Un objet
//  le cite par sa propriete "namedStyle" ; appliquer le style y copie ses
//  valeurs. Changer le style change les objets qui le citent - sauf une valeur
//  que l'objet a changee lui-meme (elle differe de l'ancienne valeur du style).
struct Style {
    Id                id{kNoId};
    std::string       name;
    std::vector<Prop> props;
    std::string       description;
    std::string       folder{};    // lot 21 : son dossier dans la liste des styles
    bool operator==(const Style&) const = default;
};

// ------------------------------------------------------------ lot 13 : essais ---
//  UN ESSAI DE RECEPTION : des pas rejoues sur l'IHM en marche - ouvrir une
//  vue, cliquer, saisir, ecrire une valeur, attendre, verifier, se connecter,
//  signer... - chacun avec son verdict ; le rapport dit ce qui passe et ce qui
//  casse (HmiScenarios.hpp). `action` : un mot de kStepActions ; `target`,
//  `value`, `expected` : ce que l'action demande (voir stepFields).
struct TestStep {
    std::string action{"cliquer"};
    std::string target;
    std::string value;
    std::string expected;
    std::string note;
    bool operator==(const TestStep&) const = default;
};
struct TestScenario {
    Id                    id{kNoId};
    std::string           name;
    std::string           description;
    std::vector<TestStep> steps;
    bool operator==(const TestScenario&) const = default;
};

// ------------------------------------------------------------ lot 13 : langues ---
//  LES LANGUES DU PROJET. La premiere est celle dans laquelle il est ecrit (ses
//  textes tels quels) ; chacune des autres a la traduction des textes qu'on lui
//  a donnee - un texte sans traduction reste dans la langue du projet. Une
//  traduction est rangee sous son texte d'origine : le meme libelle ("Marche")
//  se traduit une fois pour tout le projet (HmiLanguages.hpp).
struct Language {
    std::string code;          // "fr", "en", "de" (ISO 639-1)
    std::string name;          // "Fran\xC3\xA7" "ais", "English"
    bool operator==(const Language&) const = default;
};
struct Languages {
    std::vector<Language> list{{"fr", "Fran\xC3\xA7" "ais"}};
    // texte d'origine -> (code -> texte traduit)
    std::map<std::string, std::map<std::string, std::string>> texts;
    std::string startLanguage;  // la langue au lancement de l'IHM ; vide : la premiere
    [[nodiscard]] const Language* find(std::string_view code) const noexcept;   // sans casse
    [[nodiscard]] const std::string& source() const noexcept;                    // le code de la premiere
    bool operator==(const Languages&) const = default;
};

// ------------------------------------------------------ lot 13 : unites et formats ---
//  L'UNITE ET LE FORMAT D'UNE VARIABLE, repris partout ou elle s'affiche :
//  l'afficheur numerique, le champ de saisie, la jauge, le bargraphe, le
//  cadran, le thermometre, les contenants, le curseur, le potentiometre, les
//  lignes d'un tableau de variables, les trous des textes ({Pression} prend le
//  format ; {Pression:u} le format et l'unite). Un objet garde les siens s'il
//  decoche "Format de la variable". `path` : une variable IHM ou de l'automate ;
//  "Armoires[].ana.PT1.mes" vaut pour chaque case (un chemin exact l'emporte).
struct VariableDisplay {
    std::string path;          // "Pression_Reseau", "Armoires[].ana.PT1.mes"
    std::string unit;          // "bar", "\xC2\xB0" "C", "%" ; vide : celle de l'objet
    std::string format;        // "0.0", "0", "0.00" ; vide : celui de l'objet
    bool operator==(const VariableDisplay&) const = default;
};

// ------------------------------------------------------ lot 14 : communication ---
//  L'AUTOMATE REEL. Mode "simulateur" : l'IHM lit et ecrit l'automate simule de
//  l'application (comme avant) ; "modbus" : un automate (ou tout equipement
//  Modbus TCP) a cette adresse IP. Une variable s'y trouve par son adresse :
//  celle du programme (Vitesse AT %MW100 : INT), ou celle donnee dans la table
//  des adresses (une variable non localisee que le programme recopie dans des
//  %MW, ou une variable d'un equipement sans programme). HmiComm.hpp.
struct CommAddress {
    std::string variable;      // Vitesse_Pompe, Armoires[0].ana.PT1.mes, Temperature_Four
    std::string address;       // %MW100, %MF200, %MD20, %M12, %MW40.3, %IW4, %I7
    std::string type;          // INT, REAL, BOOL... ; vide : celui du programme
    bool        readOnly{false};
    std::string description;
    bool operator==(const CommAddress&) const = default;
};
struct Communication {
    std::string mode{"simulateur"};     // "simulateur", "modbus"
    std::string host{"192.168.1.10"};
    int         port{502};
    int         unit{255};              // l'esclave (Unit Id) : 255 pour l'UC d'un M340 / M580
    int         timeoutMs{1000};        // le delai d'une reponse
    int         periodMs{500};          // la scrutation : une lecture de tout ce qui s'affiche
    int         retryS{5};              // connexion perdue : un nouvel essai toutes les N s
    std::string wordOrder{"faible"};    // 32 bits : "faible" (poids faible d'abord, Schneider), "fort"
    int         maxWords{120};          // une requete : au plus N mots (125 au plus)
    int         maxBits{1968};          // ... au plus N bits (2000 au plus)
    int         gap{8};                 // deux zones a moins de N mots l'une de l'autre : une seule requete
    bool        writes{true};           // faux : lecture seule (rien n'est ecrit dans l'automate)
    int         badAfterS{30};          // liaison perdue : "ancienne" N s, puis "mauvaise"
    std::vector<CommAddress> addresses;
    // Le serveur de demonstration : le simulateur expose en Modbus TCP, pour
    // essayer la liaison (et d'autres clients : une supervision) sans automate.
    bool        demoServer{false};
    int         demoPort{5020};
    bool        demoAllInterfaces{false};   // faux : ce poste seulement (127.0.0.1)
    [[nodiscard]] bool modbus() const noexcept { return mode == "modbus"; }
    bool operator==(const Communication&) const = default;
};

// ------------------------------------------------------ lot 15 : les equipements ---
//  UN EQUIPEMENT DU RESEAU, en plus de l'automate du projet (Communication) :
//  une centrale de mesure, un variateur, un analyseur - Modbus TCP/IP -, ou un
//  appareil IP sans Modbus - switch, camera, imprimante - dont l'IHM sait s'il
//  repond (Ethernet TCP/IP : le ping, et un port TCP s'il est donne). Les
//  variables IHM s'y lient (Variable::equipment, address). Simule, sa memoire
//  vit dans l'application : un serveur Modbus sur ce PC, que l'outil Modbus
//  lit et ecrit, pour essayer l'IHM sans l'equipement.
enum class EquipmentType : std::uint8_t {
    ModbusTcp,       // Modbus TCP/IP
    EthernetTcp,     // Ethernet TCP/IP : joignable ou non
    // a venir : ModbusRtu (serie RS-485, par un adaptateur ou une passerelle)
};
[[nodiscard]] std::string_view equipmentTypeKey(EquipmentType) noexcept;     // "modbus_tcp", "ethernet_tcp"
[[nodiscard]] std::string_view equipmentTypeLabel(EquipmentType) noexcept;   // "Modbus TCP/IP", "Ethernet TCP/IP"
// Par sa cle ou son libelle, sans casse ; vide : inconnu.
[[nodiscard]] std::optional<EquipmentType> equipmentTypeFrom(std::string_view) noexcept;
[[nodiscard]] const std::vector<std::string>& equipmentTypeLabels();

// ------------------------------------------------ lot 17 : les zones memoire ---
//  CE QU'UN EQUIPEMENT A DANS SA MEMOIRE, table par table : les bobines (0x,
//  %M), les entrees TOR (1x, %I), les registres d'entree (3x, %IW), les
//  registres de maintien (4x, %MW). Chaque table a une ou plusieurs plages
//  (une centrale de mesure a des trous : 2700-2799, 3000-3199) ; une table sans
//  plage n'existe pas. Pas declarees (declared faux) : rien n'est controle.
//  Detectees (lecture seule) ou saisies ; le jumeau simule les sert telles
//  quelles (au-dela : "adresse illegale", exception 02).
enum class MemTable : std::uint8_t { Coils, DiscreteInputs, InputRegisters, Holding };
inline constexpr MemTable kMemTables[] = {MemTable::Coils, MemTable::DiscreteInputs, MemTable::InputRegisters, MemTable::Holding};
struct MemRange {
    std::uint32_t first{0}, last{0};          // les numeros (a partir de 0, comme %MW), bornes comprises
    [[nodiscard]] std::uint32_t size() const noexcept { return last >= first ? last - first + 1 : 0; }
    bool operator==(const MemRange&) const = default;
};
struct MemZones {
    bool                                   declared{false};
    std::array<std::vector<MemRange>, 4>   tables;         // dans l'ordre de MemTable
    std::string                            origin;         // "detectees le 26/09 a 09:52", "saisies", "modele : M340"
    [[nodiscard]] const std::vector<MemRange>& of(MemTable t) const noexcept { return tables[static_cast<std::size_t>(t)]; }
    [[nodiscard]] std::vector<MemRange>&       of(MemTable t) noexcept { return tables[static_cast<std::size_t>(t)]; }
    bool operator==(const MemZones&) const = default;
};

// ----------------------------------------------- lot 17 : le jumeau simule ---
//  UN ESCLAVE MODBUS VIRTUEL : un serveur Modbus TCP dans l'application, avec
//  les zones de l'equipement, une memoire et des comportements. En simulation
//  (l'application, pas le poste d'exploitation), l'IHM parle au jumeau au lieu
//  du vrai appareil ; un equipement "seulement simule" (simulated) n'a que son
//  jumeau, partout. L'outil Modbus, la carte memoire, Detecter les zones le
//  voient comme un vrai.
enum class TwinStart : std::uint8_t { Zeros, Initial, Saved };
[[nodiscard]] std::string_view twinStartKey(TwinStart) noexcept;          // "zeros", "initiales", "gardee"
[[nodiscard]] std::string_view twinStartLabel(TwinStart) noexcept;        // "des zeros", "valeurs initiales des variables", "la memoire gardee"
enum class BehaviorKind : std::uint8_t { Constant, Sine, Ramp, Counter, Blink, Random, Copy, FollowPlc, Steps };
inline constexpr BehaviorKind kBehaviorKinds[] = {BehaviorKind::Constant, BehaviorKind::Sine, BehaviorKind::Ramp, BehaviorKind::Counter,
                                                  BehaviorKind::Blink, BehaviorKind::Random, BehaviorKind::Copy, BehaviorKind::FollowPlc,
                                                  BehaviorKind::Steps};
[[nodiscard]] std::string_view behaviorKindKey(BehaviorKind) noexcept;    // "sinus", "rampe"...
[[nodiscard]] std::string_view behaviorKindLabel(BehaviorKind) noexcept;  // "sinus", "suit l'automate"...
[[nodiscard]] std::optional<BehaviorKind> behaviorKindFrom(std::string_view) noexcept;
// Ce que l'esclave fait tout seul sur une plage : une valeur de `type` a
// `address` ("40001", "%MW0", "%MF20", "%M5", "40005.1").
struct Behavior {
    std::string  address;
    std::string  type{"INT"};                // INT, UINT, WORD, DINT, UDINT, DWORD, REAL, BOOL
    BehaviorKind kind{BehaviorKind::Constant};
    double       a{0};                       // la valeur (constante), le minimum, le depart (compteur)
    double       b{100};                     // le maximum ; le pas (compteur)
    double       period{10};                 // s : la periode (sinus, rampe), l'intervalle (compteur, clignote, aleatoire, etapes)
    double       delay{0};                   // s : le retard (recopie)
    std::string  source;                     // recopie : l'adresse ; suit l'automate : la variable ; etapes : "1; 5; 3"
    // Lot 18 : anime ou non (decoche : il ne fait rien, ses reglages restent) ;
    // clignote : "delay" > 0, a 1 pendant delay s toutes les period s (sinon
    // une moitie de periode sur deux, comme au lot 17).
    bool         enabled{true};
    double       noise{0};                   // lot 18 : un bruit, +/- cette valeur brute, ajoute au mouvement
    bool operator==(const Behavior&) const = default;
};
// Lot 18 : une case forcee du jumeau - tenue a une valeur brute (celle du
// registre, dans son type : INT, REAL... ; une bobine, un bit : 0 ou 1). Une
// ecriture qui la changerait est refusee (exception 04).
struct Forcing {
    std::string address;                     // "43021", "00001", "%MW10.3", "%MF20"
    std::string type{"INT"};                 // INT, UINT, WORD, DINT, UDINT, DWORD, REAL, BOOL
    double      value{0};
    std::string since;                       // "2026-09-26 14:32:10"
    bool operator==(const Forcing&) const = default;
};
// La memoire gardee d'un jumeau : les valeurs non nulles, par plages.
struct SavedMemory {
    MemTable                   table{MemTable::Holding};
    std::uint32_t              first{0};
    std::vector<std::uint16_t> values;       // les mots (ou les bits, 0/1)
    bool operator==(const SavedMemory&) const = default;
};
// Un port du PC simule (le reseau simule) : son nom et son adresse.
struct SimPort {
    std::string name;                        // "Port simule 1"
    std::string ip;                          // "192.168.56.1"
    int         prefix{24};
    std::string copyOf;                      // le vrai port copie ("Ethernet 5") ; vide : cree a la main
    bool operator==(const SimPort&) const = default;
};

// 1.9 : CE QUE L'IHM LIT quand un vrai appareil a un ESCLAVE SIMULE LIE (son
// clone : un esclave Modbus dans l'application, dont la configuration suit
// celle du vrai - esclave, zones, variables, ordre des mots ; d'ou le cadenas).
// Dans l'application : le vrai, l'esclave, ou automatique (le vrai ; l'esclave
// quand le vrai ne repond plus depuis "basculer apres" secondes ; le vrai a
// nouveau des qu'il repond, si "revenir au vrai"). Sur le poste d'exploitation :
// toujours le vrai, automatique, ou au choix d'un administrateur (la page
// Simulation de Parametres systeme, Ctrl+Alt+S, permission Administrer).
enum class ReadSource : std::uint8_t { Real, Slave, Auto };
enum class StationRead : std::uint8_t { Real, Auto, Admin };
[[nodiscard]] std::string_view readSourceKey(ReadSource) noexcept;       // "vrai", "esclave", "auto"
[[nodiscard]] std::string_view readSourceLabel(ReadSource) noexcept;     // "le vrai appareil", "l'esclave simule", "le vrai ; l'esclave s'il ne repond pas"
[[nodiscard]] std::optional<ReadSource> readSourceFrom(std::string_view) noexcept;    // la cle, le libelle, "jumeau", "simule"...
[[nodiscard]] std::string_view stationReadKey(StationRead) noexcept;     // "vrai", "auto", "admin"
[[nodiscard]] std::string_view stationReadLabel(StationRead) noexcept;   // "toujours le vrai appareil", "automatique", "au choix d'un administrateur"
[[nodiscard]] std::optional<StationRead> stationReadFrom(std::string_view) noexcept;
// "Variateur_ATV320" : le nom d'un equipement dans SYS.Slave.<nom> (ce qui n'est
// ni lettre ni chiffre devient _, les accents perdent leur accent).
[[nodiscard]] std::string slaveKey(std::string_view equipmentName);

struct Equipment {
    Id            id{kNoId};
    std::string   name;
    EquipmentType type{EquipmentType::ModbusTcp};
    std::string   host{"192.168.1.30"};
    int           port{502};            // Ethernet TCP/IP : le port surveille (0 : le ping seul)
    int           unit{1};              // l'esclave (Unit Id) : 1 le plus souvent, 255 pour une UC Schneider
    int           timeoutMs{1000};
    int           periodMs{500};
    int           retryS{5};
    std::string   wordOrder{"faible"};  // 32 bits : "faible" (poids faible d'abord), "fort"
    int           maxWords{120};
    int           maxBits{1968};
    int           gap{8};
    bool          writes{true};         // faux : lecture seule
    int           badAfterS{30};
    int           pingS{10};            // la surveillance par ping : toutes les N s (0 : jamais)
    bool          simulated{false};     // simule : rien sur le reseau, sa memoire dans l'application
                                        // (lot 17 : "seulement simule" - son jumeau, partout)
    bool          enabled{true};
    std::string   description;
    // Lot 17 : ses zones memoire, et son jumeau simule.
    MemZones      zones;
    bool          twin{false};          // il a un jumeau (un esclave Modbus virtuel)
    std::string   twinName;             // "Balance B (virtuelle)" ; vide : "<nom> (virtuel)"
    std::string   twinHost;             // l'adresse simulee ; vide : la meme que host
    int           twinPort{502};
    bool          twinRunning{true};    // en marche des que la simulation demarre
    bool          useTwin{true};        // en simulation, l'IHM parle au jumeau (faux : au vrai appareil)
    int           twinDelayMs{5};       // le temps de reponse du jumeau
    int           twinJitterMs{2};      // plus ou moins
    bool          twinResponds{true};   // faux : panne simulee (plus de reponse)
    int           twinException{0};     // une exception forcee a chaque requete (0 : aucune)
    bool          twinPing{true};       // il repond au ping (simule)
    bool          twinExpose{false};    // visible sur le vrai reseau (un autre maitre peut l'interroger)
    int           twinExposePort{1502};
    TwinStart     twinStart{TwinStart::Initial};
    std::vector<Behavior>    behaviors;
    std::vector<SavedMemory> twinMemory;     // la memoire gardee (twinStart == Saved)
    std::vector<Forcing>     forcings;       // lot 18 : ses cases forcees
    // 1.9 : l'esclave simule lie (le jumeau des lots 17-18) - ce que l'IHM lit.
    bool          twinAuto{false};      // dans l'application : automatique (alors useTwin ne compte plus)
    int           fallbackAfterS{10};   // automatique : l'esclave apres N s sans reponse du vrai (1 a 3600)
    bool          fallbackReturn{true}; // ... et le vrai a nouveau des qu'il repond (faux : l'esclave jusqu'au redemarrage)
    StationRead   stationRead{StationRead::Real};   // sur le poste d'exploitation
    [[nodiscard]] bool modbus() const noexcept { return type == EquipmentType::ModbusTcp; }
    // Lot 17 : il a un esclave virtuel (un jumeau, ou il n'est que simule).
    [[nodiscard]] bool hasTwin() const noexcept { return twin || simulated; }
    // 1.9 : un vrai appareil et son esclave simule lie (le clone, au cadenas).
    [[nodiscard]] bool linkedSlave() const noexcept { return twin && !simulated; }
    // "Variateur ATV320 · esclave simule" (twinName s'il est donne).
    [[nodiscard]] std::string twinLabel() const { return twinName.empty() ? name + " \xC2\xB7 esclave simul\xC3\xA9" : twinName; }
    [[nodiscard]] std::string twinAddress() const { return twinHost.empty() ? host : twinHost; }
    // Dans l'application : ce que dit la fiche (automatique passe avant useTwin).
    [[nodiscard]] ReadSource appRead() const noexcept { return twinAuto ? ReadSource::Auto : useTwin ? ReadSource::Slave : ReadSource::Real; }
    bool operator==(const Equipment&) const = default;
};

// ------------------------------------------------ lot 14 : poste d'exploitation ---
//  L'IHM SEULE, SUR LE POSTE DE L'ATELIER : xpg_analyzer --ihm <dossier du
//  projet> ouvre la vue de demarrage en plein ecran, sans l'editeur ; en
//  kiosque, fermer la fenetre (Alt+F4) ne fait rien, et la sortie (Ctrl+Alt+Q,
//  ou cinq touchers du coin haut droit) demande le mot de passe de sortie. Les
//  ecrans en plus (2, 3...) montrent chacun une vue, en direct.
struct StationScreen {
    int         display{2};            // l'ecran (1 : le principal)
    std::string view;                  // la vue qu'il montre
    bool operator==(const StationScreen&) const = default;
};
struct Station {
    bool        fullScreen{true};      // plein ecran, sans barre de titre
    bool        kiosk{true};           // la sortie demande le mot de passe ; fermer la fenetre ne fait rien
    std::string exitSalt, exitHash;    // le mot de passe de sortie (empreinte salee) ; vide : sortie libre
    bool        cornerExit{true};      // cinq touchers du coin haut droit (ecran tactile), en plus de Ctrl+Alt+Q
    bool        hideCursor{false};     // un ecran tactile : pas de curseur
    bool        runSimulator{true};    // sur le simulateur : l'automate simule tourne des le lancement
    std::vector<StationScreen> screens;
    // 1.9 : la page Simulation de Parametres systeme (les esclaves simules, ce que
    // lit l'IHM) - Ctrl+Alt+S ; toujours reservee a la permission Administrer.
    // Faux : elle n'existe pas sur le poste.
    bool        simPage{true};
    // 1.9 : les reperes des lectures simulees (un cadre violet en tirets et une
    // pastille sur les objets, le bandeau LECTURES SIMULEES) - sur le poste et
    // dans Simuler l'IHM. Faux : un poste de formation, tout simule, sans reperes.
    bool        simMarks{true};
    bool operator==(const Station&) const = default;
};

// ------------------------------------------------ lot 14 : notifications ---
//  LES ALARMES PREVIENNENT QUELQU'UN. Un courriel par le relais SMTP de
//  l'usine (sans TLS ; AUTH LOGIN si un utilisateur est donne), un SMS par une
//  passerelle HTTP (une URL ou {numero} et {message} sont remplaces). Chaque
//  destinataire recoit les alarmes de ses priorites et de ses groupes ; celui
//  d'astreinte, seulement pendant ses heures. Anti-rafale : un delai (acquittee
//  ou disparue avant, rien ne part), une seule fois par alarme et par
//  destinataire en N secondes, au plus N messages par heure. HmiNotify.hpp.
struct NotifyRecipient {
    std::string name;                   // "Astreinte electricite"
    std::string email;                  // vide : pas de courriel
    std::string phone;                  // "+33612345678" ; vide : pas de SMS
    int         maxPriority{2};         // les alarmes de priorite 1 (critique) a N
    std::string groups;                 // "Armoire A; Armoire B" ; vide : tous les groupes
    bool        duty{false};            // d'astreinte : seulement les jours et heures dits
    std::string dutyDays{"1234567"};    // 1 lundi ... 7 dimanche
    std::string dutyFrom{"18:00"};      // de... (passe minuit si "a" est avant)
    std::string dutyTo{"08:00"};        // ...a
    bool        onClear{false};         // prevenir aussi quand l'alarme disparait
    bool        enabled{true};
    bool operator==(const NotifyRecipient&) const = default;
};
struct Notifications {
    bool        enabled{false};
    std::string smtpHost;               // le relais de l'usine ; vide : pas de courriel
    int         smtpPort{25};
    std::string smtpFrom{"ihm@usine.local"};
    std::string smtpUser;               // vide : sans authentification
    std::string smtpPassword;           // masque (notify::maskSecret), jamais en clair
    int         timeoutS{10};           // une connexion, une reponse
    std::string smsUrl;                 // http://192.168.1.50/sms?to={numero}&text={message} ; vide : pas de SMS
    std::string smsBody;                // vide : GET ; sinon POST de ce corps ({numero}, {message})
    std::string subject{"[{projet}] {priorite} : {alarme}"};
    std::string body;                   // vide : le texte par defaut (notify::defaultBody)
    std::string sms{"{projet} {priorite} {etat} : {message} ({heure})"};
    int         delayS{0};              // attendre N s : acquittee ou disparue avant, rien ne part
    int         repeatS{600};           // la meme alarme au meme destinataire : une fois en N s
    int         maxPerHour{30};         // au plus N messages par heure (tous destinataires)
    // La boite d'essai : un serveur SMTP (et HTTP pour les SMS) sur ce poste qui
    // garde ce qu'il recoit - pour essayer sans relais ni passerelle.
    bool        testBox{false};
    int         testPort{2525};         // SMTP ; HTTP : le suivant
    std::vector<NotifyRecipient> recipients;
    bool operator==(const Notifications&) const = default;
};

// ------------------------------------------------ lot 14 : rapports ---
//  UN RAPPORT PERIODIQUE : chaque jour, chaque semaine ou chaque mois, a
//  l'heure dite, un PDF ou un classeur Excel dans exports/rapports/ - les
//  alarmes de la periode, les mesures archivees (minimum, moyenne, maximum),
//  la production ; envoye aux destinataires dits (les notifications).
//  HmiReports.hpp.
struct Report {
    Id          id{kNoId};
    std::string name;                   // "Rapport_journalier"
    std::string title;                  // vide : le nom
    std::string period{"jour"};         // jour, semaine, mois
    std::string time{"06:00"};          // l'heure de depart (la fin de la periode)
    int         weekday{1};             // semaine : le jour (1 lundi ... 7 dimanche)
    int         monthDay{1};            // mois : le jour (1 a 28)
    std::string format{"PDF"};          // PDF, Excel
    bool        alarms{true};           // les alarmes de la periode
    std::string measures;               // "Pression; Debit" : minimum, moyenne, maximum ; vide : toutes les archivees
    bool        production{true};       // les compteurs de production
    bool        events{false};          // les evenements (connexions, recettes, ecritures...)
    std::string recipients;             // des destinataires des notifications (leurs noms) : en piece jointe
    bool        enabled{true};
    bool operator==(const Report&) const = default;
};

// ------------------------------------------------ lot 14 : acces web ---
//  L'IHM DANS UN NAVIGATEUR (une tablette, un autre PC) : le serveur web
//  integre montre la vue en marche, rafraichie ; les clics y passent si on le
//  permet. Les comptes sont ceux de l'IHM ; lecture seule par defaut.
//  HmiWeb.hpp.
struct WebAccess {
    bool enabled{false};
    int  port{8080};
    bool allInterfaces{false};          // faux : ce poste seulement (127.0.0.1)
    bool login{true};                   // se connecter avec un utilisateur de l'IHM
    bool control{false};                // faux : lecture seule ; vrai : les clics passent (permission Piloter)
    int  refreshMs{500};                // l'image de la vue
    int  quality{80};                   // JPEG
    int  maxClients{8};
    int  sessionMin{30};                // une session sans activite se ferme
    bool operator==(const WebAccess&) const = default;
};

// ---------------------------------------------------- lot 20 : modeles de vues ----
//  UN MODELE GARDE DANS LE PROJET (il voyage avec lui) : un paquet de vues
//  (HmiPackage.hpp : la vue, ses symboles, ses images, ses styles, ses
//  variables), tel qu'il s'enregistre - un zip dans ihm/modeles/ - et ce que la
//  galerie en montre sans l'ouvrir. Les modeles de "Ma bibliotheque" vivent a
//  cote des reglages de l'application, pas dans le projet.
struct ViewTemplateFile {
    Id          id{kNoId};
    std::string name;
    std::string category;
    std::string description;
    std::string created;
    BlobPtr     data;            // le paquet (.xpgmodele)
    bool operator==(const ViewTemplateFile&) const = default;
};

struct Stats {
    std::size_t views{0}, objects{0}, scripts{0}, alarms{0}, resources{0}, layers{0}, groups{0}, actions{0};
    std::size_t recipes{0}, users{0};
    std::size_t expressions{0};      // proprietes pilotees par une expression
    std::size_t bytesInMemory{0};    // estimation : chaines + structures
};

// ------------------------------------- 1.9 : les jeux de lecture de l'outil Modbus ---
//  UNE LISTE DE REQUETES DE LA LECTURE CYCLIQUE (IHM > Outil Modbus), GARDEE
//  SOUS UN NOM DANS LE PROJET : elle suit ses versions et part avec l'affaire.
//  Une requete vise l'automate du projet, un equipement (par son nom : elle suit
//  ses reglages), son esclave simule, ou une adresse tapee ; elle lit une plage
//  (fonction 1 a 4) dans un format, ou les variables qu'on y a mises (chacune
//  son type : le format "variables"). HmiModbusCyclic.hpp lit, l'outil montre.
struct ModbusReadValue {
    std::string name;                       // la variable (DINT_1058, Consigne_Variateur)
    std::string type{"INT"};                // INT, UINT, WORD, DINT, UDINT, DWORD, TIME, REAL, BOOL, STRING
    int         offset{0};                  // depuis l'adresse de la requete : mots (fonctions 3, 4) ou bits (1, 2)
    int         words{1};                   // les mots occupes (un texte : N)
    int         bit{-1};                    // le bit d'un mot (%MW10.3) ; -1 : aucun
    bool operator==(const ModbusReadValue&) const = default;
};
struct ModbusRead {
    std::string name;
    bool        active{true};               // decoche : coupee, sans etre retiree
    std::string target{"automate"};         // "automate", "equipement", "esclave" (son esclave simule), "adresse"
    std::string equipment;                  // equipement, esclave : son nom
    std::string host{"127.0.0.1"};          // adresse : l'adresse tapee
    int         port{502};
    int         unit{1};
    int         timeoutMs{1000};
    int         function{3};                // 1 a 4
    int         address{0};                 // a partir de 0 (40001 : 0)
    int         count{10};
    std::string format{"decimal"};          // mbtool::formatKey ; "variables" : d'apres les variables
    bool        lowFirst{true};             // 32 bits : poids faible d'abord (Schneider)
    int         periodMs{0};                // 0 : la periode commune
    std::vector<ModbusReadValue> values;    // format "variables" : une par variable
    std::vector<int> traced;                // les valeurs tracees (leur rang)
    bool operator==(const ModbusRead&) const = default;
};
struct ModbusReadSet {
    std::string name;
    std::vector<ModbusRead> reads;
    int         periodMs{500};              // la periode commune
    int         maxFailures{10};            // une requete en pause apres N echecs de suite (0 : jamais)
    int         pauseMs{0};                 // entre deux requetes d'une meme connexion
    bool        perTarget{true};            // une connexion par equipement (faux : toutes l'une apres l'autre)
    int         windowS{60};                // la fenetre du graphique
    bool        lanes{true};                // le graphique en pistes (faux : une seule echelle)
    bool operator==(const ModbusReadSet&) const = default;
};

struct Project {
    Config            config;
    std::vector<View> views;
    Assets            assets;
    Programs          programs;
    std::vector<AlarmDef> alarms;
    AlarmSettings         alarmSettings;     // lot 11 : sons, mise de cote
    std::vector<AlarmGroupDef>  alarmGroups;       // 1.10.2 : IHM > Alarmes > Groupes, avec leurs reglages
    std::vector<AlarmGroupLink> alarmGroupLinks;   // 1.10.2 : groupe d'alarmes des objets -> groupe de l'IHM
    std::vector<Recipe>   recipes;
    Security          security{Security::defaults()};
    HistorySettings   history;
    std::vector<Style> styles;               // lot 12 : les styles nommes
    std::vector<TestScenario> scenarios;     // lot 13 : les essais de reception
    Languages         languages;             // lot 13 : les langues et les traductions
    std::vector<VariableDisplay> displays;   // lot 13 : les unites et les formats des variables
    Communication     comm;                  // lot 14 : l'automate reel (Modbus TCP)
    Station           station;               // lot 14 : le poste d'exploitation
    Notifications     notify;                // lot 14 : courriels et SMS des alarmes
    std::vector<Report> reports;             // lot 14 : les rapports periodiques
    WebAccess         web;                   // lot 14 : l'acces par navigateur
    std::vector<Equipment> equipments;       // lot 15 : les equipements du reseau
    std::vector<SimPort>   simPorts;         // lot 17 : les ports du PC simule (le reseau simule)
    std::vector<ViewTemplateFile> viewTemplates;   // lot 20 : les modeles de vues du projet
    std::vector<ModbusReadSet> modbusSets;   // 1.9 : les jeux de lecture de l'outil Modbus
    std::string       modbusSetLast;         // 1.9 : le dernier jeu ouvert (il revient a l'ouverture de l'outil)
    // Lot 21 : les dossiers vides des listes (cle de la liste : "vues", "scripts"...,
    // hmi::fold) - un dossier qui a des elements existe par eux.
    std::map<std::string, std::vector<std::string>> listFolders{};
    Id                nextId{1};

    [[nodiscard]] Id           allocate() noexcept { return nextId++; }
    [[nodiscard]] View*        view(Id) noexcept;
    [[nodiscard]] const View*  view(Id) const noexcept;
    [[nodiscard]] View*        viewByName(std::string_view) noexcept;
    [[nodiscard]] const View*  viewByName(std::string_view) const noexcept;
    [[nodiscard]] Resource*           resource(Id) noexcept;
    [[nodiscard]] const Resource*     resource(Id) const noexcept;
    [[nodiscard]] const Resource*     resourceByName(std::string_view) const noexcept;
    [[nodiscard]] ExternalFile*       externalFile(Id) noexcept;
    [[nodiscard]] const ExternalFile* externalFile(Id) const noexcept;
    [[nodiscard]] const ExternalFile* externalByName(std::string_view) const noexcept;
    [[nodiscard]] Script*             script(Id) noexcept;               // general ou de vue
    [[nodiscard]] const Script*       script(Id) const noexcept;
    [[nodiscard]] const Script*       generalScript(std::string_view name) const noexcept;
    [[nodiscard]] const Variable*     variable(std::string_view name) const noexcept;   // sans casse
    [[nodiscard]] Variable*           variableById(Id) noexcept;         // lot 16
    [[nodiscard]] const Variable*     variableById(Id) const noexcept;
    [[nodiscard]] HmiType*            hmiType(Id) noexcept;              // lot 16 : les types IHM
    [[nodiscard]] const HmiType*      hmiType(Id) const noexcept;
    [[nodiscard]] const HmiType*      hmiTypeByName(std::string_view) const noexcept;   // sans casse
    [[nodiscard]] HmiFunction*        function(Id) noexcept;             // lot 7
    [[nodiscard]] const HmiFunction*  function(Id) const noexcept;
    [[nodiscard]] const HmiFunction*  functionByName(std::string_view) const noexcept;   // sans casse
    // La vue qui porte ce script (kNoId : un script general).
    [[nodiscard]] Id                  viewOfScript(Id) const noexcept;
    [[nodiscard]] AlarmDef*           alarm(Id) noexcept;
    [[nodiscard]] const AlarmDef*     alarm(Id) const noexcept;
    [[nodiscard]] const AlarmDef*     alarmByName(std::string_view) const noexcept;
    [[nodiscard]] Recipe*             recipe(Id) noexcept;
    [[nodiscard]] const Recipe*       recipe(Id) const noexcept;
    [[nodiscard]] const Recipe*       recipeByName(std::string_view) const noexcept;
    [[nodiscard]] User*               user(Id) noexcept;
    [[nodiscard]] const User*         user(Id) const noexcept;
    [[nodiscard]] const User*         userByLogin(std::string_view) const noexcept;    // sans casse
    [[nodiscard]] UserGroup*          group(Id) noexcept;
    [[nodiscard]] const UserGroup*    group(Id) const noexcept;
    [[nodiscard]] const UserGroup*    groupByName(std::string_view) const noexcept;
    [[nodiscard]] const Role*         role(std::string_view) const noexcept;
    [[nodiscard]] Style*              style(Id) noexcept;                // lot 12
    [[nodiscard]] const Style*        style(Id) const noexcept;
    [[nodiscard]] const Style*        styleByName(std::string_view) const noexcept;   // sans casse
    [[nodiscard]] TestScenario*       scenario(Id) noexcept;             // lot 13
    [[nodiscard]] const TestScenario* scenario(Id) const noexcept;
    [[nodiscard]] const TestScenario* scenarioByName(std::string_view) const noexcept;   // sans casse
    [[nodiscard]] Report*             report(Id) noexcept;               // lot 14
    [[nodiscard]] const Report*       report(Id) const noexcept;
    [[nodiscard]] const Report*       reportByName(std::string_view) const noexcept;   // sans casse
    [[nodiscard]] const NotifyRecipient* recipientByName(std::string_view) const noexcept;   // sans casse
    [[nodiscard]] Equipment*          equipment(Id) noexcept;            // lot 15
    [[nodiscard]] const Equipment*    equipment(Id) const noexcept;
    [[nodiscard]] const Equipment*    equipmentByName(std::string_view) const noexcept;   // sans casse
    [[nodiscard]] Stats        statistics() const;

    bool operator==(const Project&) const = default;
};

// ------------------------------------------------------------- fabriques ----
// Un objet avec des valeurs par defaut raisonnables pour son genre : taille,
// couleurs, texte. Pose en (x, y), dans le calque donne.
[[nodiscard]] Object makeObject(Kind, Id id, std::string name, double x, double y, Id layer);
// Une vue vide a la taille du projet, avec un premier calque actif.
[[nodiscard]] View   makeView(Project&, std::string name);
// "Rectangle_1", "Rectangle_2"... : le premier nom libre de la vue.
[[nodiscard]] std::string uniqueObjectName(const View&, std::string_view base);
[[nodiscard]] std::string uniqueViewName(const Project&, std::string_view base);
[[nodiscard]] std::string uniqueLayerName(const View&, std::string_view base);
// "logo.png", "logo_2.png"... : un nom de ressource (ou de fichier externe) libre.
[[nodiscard]] std::string uniqueResourceName(const Project&, std::string_view wanted);
[[nodiscard]] std::string uniqueExternalName(const Project&, std::string_view wanted);
[[nodiscard]] std::string uniqueScriptName(const Project&, std::string_view wanted);
[[nodiscard]] std::string uniqueVariableName(const Project&, std::string_view wanted);
// Lot 7 : libre parmi les fonctions ET les variables IHM (elles partagent les noms d'un script).
[[nodiscard]] std::string uniqueFunctionName(const Project&, std::string_view wanted);
[[nodiscard]] std::string uniqueAlarmName(const Project&, std::string_view wanted);
[[nodiscard]] std::string uniqueRecipeName(const Project&, std::string_view wanted);
[[nodiscard]] std::string uniqueLogin(const Project&, std::string_view wanted);
[[nodiscard]] std::string uniqueStyleName(const Project&, std::string_view wanted);   // lot 12
[[nodiscard]] std::string uniqueEquipmentName(const Project&, std::string_view wanted);   // lot 15
// Ce que l'utilisateur peut : ses roles, par son groupe. "Administrer" vaut tout.
[[nodiscard]] bool        userHas(const Project&, const User&, std::string_view permission);
[[nodiscard]] int         userLevel(const Project&, const User&);
// Un nom de script ou de variable : lettres, chiffres, _ ; pas un chiffre en tete.
[[nodiscard]] bool        isIdentifier(std::string_view) noexcept;

// Nombres et couleurs en texte : un seul format, celui qu'on enregistre.
[[nodiscard]] std::string formatNumber(double v);
[[nodiscard]] bool        parseNumber(std::string_view s, double& out) noexcept;
[[nodiscard]] bool        parseBool(std::string_view s, bool fallback) noexcept;

} // namespace hmi
