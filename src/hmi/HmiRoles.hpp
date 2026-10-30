// =============================================================================
//  hmi/HmiRoles.hpp - 1.12.6 (lots B et C de l'audit des objets) : LE ROLE DE
//                     CHAQUE PROPRIETE, UNE SEULE TABLE
// -----------------------------------------------------------------------------
//  Avant, trois tables disaient le type attendu d'une propriete : l'inspecteur
//  (expectedOf), la verification des formules (kPropTypes et le suffixe
//  « Color ») et les verifications au cas par cas de Generer. Elles ne
//  s'accordaient pas : `value`, `fault`, `state`, les seuils, la consigne, les
//  zones n'etaient types nulle part ; une couleur `colorOn` n'etait pas vue
//  comme une couleur.
//
//  ICI, CHAQUE CLE A UN ROLE, eventuellement propre au genre de l'objet (la
//  valeur d'une jauge est un Nombre, celle d'un voyant un Booleen, celle d'un
//  multi-etats un Etat). Le role dit :
//   - ce que la propriete attend (le carre de legende de l'inspecteur, Generer) ;
//   - comment la lire (le lecteur du role, HmiRoles.cpp : un BOOL vaut 1 dans
//     une jauge, une duree se lit en secondes, une enumeration montre son texte) ;
//   - comment ses reperes `$...$` se lisent (une formule en mode Expression, un
//     texte, un nom ou une liste en mode Texte) - le moteur, le dessin,
//     Generer et Dupliquer lisent pareil ;
//   - le type de Vue.Objet.Cle (REAL, INT, BOOL, STRING).
//
//  EN LIGNE, ET C'EST VOULU : HmiPublicVars.hpp (l'arbre du projet) s'en sert
//  sans lier la bibliotheque de l'IHM. Le lecteur, le decoupeur et la
//  conversion a l'ecriture sont dans HmiRoles.cpp.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace hmi::roles {

enum class Role : std::uint8_t {
    Number,     // un nombre : la valeur d'une jauge, min, max, un seuil, une position, une taille
    Boolean,    // vrai ou faux : visible, clignotant, une case de l'inspecteur
    State,      // un etat parmi plusieurs : la valeur d'un multi-etats, d'un selecteur, l'onglet montre
    Text,       // un texte montre (a trous) : le texte, un libelle, un titre, l'unite
    Duration,   // une duree : en secondes (duree d'une courbe, fenetre) ou en millisecondes (periode)
    Date,       // une date et heure (le selecteur de date)
    Color,      // une couleur (#RRGGBB)
    Name,       // un nom cherche dans le projet : vue, ressource, groupe, recette, alarme, symbole, style
    Formula,    // une formule lue par le moteur (une variable, un retour d'etat, une condition), sans =
    List,       // une liste a;b;c dont chaque element a son role (`item`)
    Mini,       // un petit langage : les etats, les zones, le programme, les points, les cases
    Choice,     // un mot propose par l'inspecteur (alignement, mode, orientation...)
    Format,     // un format d'affichage (0.0, 0.00, x4)
};

// L'unite d'une duree.
enum class Unit : std::uint8_t { None, Seconds, Milliseconds };

struct KeyRole {
    std::string_view key;
    Role             role{Role::Text};
    Role             item{Role::Text};   // List : le role de chaque element ; Formula : celui de sa valeur
    Unit             unit{Unit::None};   // Duration
    std::string_view type{};             // le type de Vue.Objet.Cle : REAL, INT, BOOL ; vide : lu dans la valeur
};

// ---- la table ------------------------------------------------------------------------
//  Une ligne par cle de l'inspecteur (et `variable`). Une cle propre a un genre se
//  regle dans roleOf (la valeur, la variable, le retour d'etat, la source...).
inline constexpr KeyRole kKeyRoles[] = {
    // ---- communes : la geometrie, l'apparence, la securite
    {"x", Role::Number, Role::Text, Unit::None, "REAL"}, {"y", Role::Number, Role::Text, Unit::None, "REAL"},
    {"w", Role::Number, Role::Text, Unit::None, "REAL"}, {"h", Role::Number, Role::Text, Unit::None, "REAL"},
    {"rot", Role::Number, Role::Text, Unit::None, "REAL"}, {"pivotX", Role::Number, Role::Text, Unit::None, "REAL"},
    {"pivotY", Role::Number, Role::Text, Unit::None, "REAL"}, {"opacity", Role::Number, Role::Text, Unit::None, "REAL"},
    {"flipH", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"flipV", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"visible", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"blink", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"blinkColor", Role::Color},
    {"access", Role::Number, Role::Text, Unit::None, "INT"}, {"profile", Role::Choice}, {"auth", Role::Formula, Role::Boolean},
    {"refresh", Role::Duration, Role::Text, Unit::Milliseconds, "INT"},
    {"variable", Role::Formula, Role::Text},
    {"namedStyle", Role::Name},
    {"signature", Role::Choice}, {"signatureReasons", Role::List, Role::Text},
    {"signatureLevel", Role::Number, Role::Text, Unit::None, "INT"},
    {"tabPage", Role::Number, Role::Text, Unit::None, "INT"},
    {"a11ySymbols", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // ---- le texte et son style
    {"text", Role::Text}, {"font", Role::Choice}, {"fontSize", Role::Number, Role::Text, Unit::None, "REAL"},
    {"textColor", Role::Color}, {"align", Role::Choice}, {"wrap", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"bold", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"italic", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // ---- les formes
    {"fill", Role::Color}, {"stroke", Role::Color}, {"strokeWidth", Role::Number, Role::Text, Unit::None, "REAL"},
    {"radius", Role::Number, Role::Text, Unit::None, "REAL"}, {"points", Role::Mini}, {"shape", Role::Choice},
    // 1.12.12 (lot H) : « Points depuis » d'une ligne, d'un polygone (sa source) et son echelle (vide : celle des donnees)
    {"pointsFrom", Role::Mini}, {"pointsXMin", Role::Number, Role::Text, Unit::None, "REAL"},
    {"pointsXMax", Role::Number, Role::Text, Unit::None, "REAL"}, {"pointsYMin", Role::Number, Role::Text, Unit::None, "REAL"},
    {"pointsYMax", Role::Number, Role::Text, Unit::None, "REAL"},
    // ---- images, video
    {"image", Role::Name}, {"poster", Role::Name}, {"video", Role::Name}, {"stretch", Role::Choice},
    {"recolor", Role::Color}, {"recolorFrom", Role::Color}, {"recolorMode", Role::Choice},
    {"autoplay", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"muted", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // ---- les valeurs et leurs bornes
    {"value", Role::Number},   // reglee par genre (valueRole)
    {"colorOn", Role::Color}, {"colorOff", Role::Color}, {"colorFault", Role::Color},
    {"min", Role::Number, Role::Text, Unit::None, "REAL"}, {"max", Role::Number, Role::Text, Unit::None, "REAL"},
    {"step", Role::Number, Role::Text, Unit::None, "REAL"},
    {"ymin", Role::Number, Role::Text, Unit::None, "REAL"}, {"ymax", Role::Number, Role::Text, Unit::None, "REAL"},
    {"xmin", Role::Number, Role::Text, Unit::None, "REAL"}, {"xmax", Role::Number, Role::Text, Unit::None, "REAL"},
    {"unit", Role::Text}, {"background", Role::Color}, {"orientation", Role::Choice},
    {"format", Role::Format}, {"label", Role::Text},
    {"lowAlarm", Role::Number, Role::Text, Unit::None, "REAL"}, {"low", Role::Number, Role::Text, Unit::None, "REAL"},
    {"high", Role::Number, Role::Text, Unit::None, "REAL"}, {"highAlarm", Role::Number, Role::Text, Unit::None, "REAL"},
    {"colorWarning", Role::Color}, {"colorAlarm", Role::Color},
    {"setpoint", Role::Number, Role::Text, Unit::None, "REAL"}, {"setpointColor", Role::Color},
    {"zones", Role::Mini}, {"stateList", Role::Mini}, {"unknownText", Role::Text},
    {"showValue", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"showText", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"ticks", Role::Number, Role::Text, Unit::None, "INT"}, {"needleColor", Role::Color},
    {"digits", Role::Number, Role::Text, Unit::None, "INT"}, {"decimals", Role::Number, Role::Text, Unit::None, "INT"},
    {"leadingZeros", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"window", Role::Duration, Role::Text, Unit::Seconds, "REAL"},
    {"deadband", Role::Number, Role::Text, Unit::None, "REAL"},
    {"colorUp", Role::Color}, {"colorDown", Role::Color}, {"colorSteady", Role::Color},
    // ---- le tableau, la liste, les courbes
    {"columns", Role::List, Role::Text}, {"rows", Role::Number, Role::Text, Unit::None, "INT"},
    {"widths", Role::List, Role::Number}, {"cells", Role::Mini},
    {"source", Role::Name}, {"rowsFrom", Role::Formula, Role::List},
    {"group", Role::Name}, {"groups", Role::List, Role::Name},
    {"variables", Role::List, Role::Formula}, {"references", Role::List, Role::Formula},
    {"names", Role::List, Role::Text}, {"colors", Role::List, Role::Color},
    {"units", Role::List, Role::Text}, {"formats", Role::List, Role::Format},
    {"mode", Role::Choice}, {"duration", Role::Duration, Role::Text, Unit::Seconds, "REAL"},
    {"scale", Role::Choice}, {"interpolation", Role::Choice}, {"legend", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"penVisible", Role::List, Role::Formula}, {"pilotable", Role::Boolean, Role::Text, Unit::None, "BOOL"},   // 1.12.16 : les courbes
    {"items", Role::List, Role::Text}, {"values", Role::List, Role::Formula}, {"itemsFrom", Role::Formula, Role::List},
    {"positions", Role::List, Role::Text},
    {"state", Role::Formula, Role::Boolean}, {"lamp", Role::Formula, Role::Boolean},
    {"accent", Role::Color}, {"clip", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // ---- recettes, images animees, saisie, utilisateurs
    {"recipe", Role::Name}, {"buttons", Role::List, Role::Name},
    {"states", Role::Mini}, {"period", Role::Duration, Role::Text, Unit::Milliseconds, "INT"},
    {"keyboard", Role::Choice}, {"maxLength", Role::Number, Role::Text, Unit::None, "INT"},
    {"minLength", Role::Number, Role::Text, Unit::None, "INT"}, {"placeholder", Role::Text},
    {"validateOnExit", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"userList", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"buttonText", Role::Text}, {"afterLogin", Role::Name},
    {"showUser", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"confirm", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"display", Role::Mini}, {"empty", Role::Text}, {"icon", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"requireDigit", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // ---- les commandes
    {"confirmMode", Role::Choice}, {"holdMs", Role::Duration, Role::Text, Unit::Milliseconds, "INT"},
    {"pressedColor", Role::Color}, {"inverted", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"textOn", Role::Text}, {"textOff", Role::Text}, {"knobColor", Role::Color},
    {"operation", Role::Choice}, {"style", Role::Choice},
    {"continuous", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"maxVisible", Role::Number, Role::Text, Unit::None, "INT"},
    {"fields", Role::Choice}, {"seconds", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"schedule", Role::Mini}, {"resolution", Role::Choice}, {"output", Role::Formula, Role::Boolean}, {"nowColor", Role::Color},
    // ---- les afficheurs
    {"clockStyle", Role::Choice}, {"showDate", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"condition", Role::Formula, Role::Boolean}, {"durationFormat", Role::Choice},
    {"speed", Role::Number, Role::Text, Unit::None, "REAL"}, {"direction", Role::Choice},
    {"ecLevel", Role::Choice}, {"margin", Role::Number, Role::Text, Unit::None, "INT"}, {"darkColor", Role::Color},
    {"tab", Role::Choice},
    // ---- le synoptique
    {"fault", Role::Formula, Role::Boolean}, {"moving", Role::Formula, Role::Boolean},
    {"opening", Role::Number, Role::Text, Unit::None, "REAL"},
    {"openingMin", Role::Number, Role::Text, Unit::None, "REAL"}, {"openingMax", Role::Number, Role::Text, Unit::None, "REAL"},   // 1.12.10
    {"labelPosition", Role::Choice}, {"animate", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"valveType", Role::Choice}, {"pipeShape", Role::Choice}, {"thickness", Role::Number, Role::Text, Unit::None, "REAL"},
    {"fluidColor", Role::Color}, {"fillColor", Role::Color}, {"gasColor", Role::Color},
    {"function", Role::Text}, {"loop", Role::Text, Role::Text, Unit::None, "STRING"}, {"mounting", Role::Choice},
    {"colorMoving", Role::Color}, {"valve3Function", Role::Choice}, {"valve3Bore", Role::Choice},
    {"positionMode", Role::Choice}, {"positionA", Role::Formula, Role::Boolean}, {"positionB", Role::Formula, Role::Boolean},
    {"showPorts", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // 1.12.24 : les registres (actionneur, fins de course, lames) et le clapet coupe-feu (declenche, son declencheur)
    {"damperActuator", Role::Choice}, {"limitOpen", Role::Formula, Role::Boolean}, {"limitClosed", Role::Formula, Role::Boolean},
    {"blades", Role::Number, Role::Text, Unit::None, "INT"}, {"bladeMode", Role::Choice},
    {"tripped", Role::Formula, Role::Boolean}, {"fireRelease", Role::Choice},
    // 1.14.1 : la commande par les fins de course (la vanne, la vanne 3 voies, les registres) - le choix, l'Ordre
    // (d'ouverture : un booleen ; de position : une voie), le temps de manoeuvre (secondes), les voyants, les fins de
    // course des voies d'une vanne 3 voies.
    {"stateMode", Role::Choice}, {"openOrder", Role::Formula, Role::Boolean}, {"positionOrder", Role::Formula, Role::State},
    {"travelTime", Role::Duration, Role::Text, Unit::Seconds, "REAL"}, {"showLimits", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"limit12", Role::Formula, Role::Boolean}, {"limit13", Role::Formula, Role::Boolean}, {"limit23", Role::Formula, Role::Boolean},
    // 1.12.30 : le variateur de vitesse - pret, arriere, local (des booleens), la frequence, sa consigne, le
    // courant, la vitesse (des nombres) ; une variable tapee seule y devient sa formule, comme pour l'ouverture.
    {"ready", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"reverse", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"localMode", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"frequency", Role::Number, Role::Text, Unit::None, "REAL"}, {"frequencySetpoint", Role::Number, Role::Text, Unit::None, "REAL"},
    {"current", Role::Number, Role::Text, Unit::None, "REAL"}, {"speedPercent", Role::Number, Role::Text, Unit::None, "REAL"},
    {"showValues", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"frequencyFormat", Role::Format}, {"currentFormat", Role::Format}, {"speedFormat", Role::Format},
    {"frequencyUnit", Role::Text}, {"currentUnit", Role::Text}, {"speedUnit", Role::Text},
    {"colorReady", Role::Color},
    // 1.14.0 : le Coupe-son (son canal) et le Melangeur de volume (les canaux montres, ce qu'il montre, la couleur du M)
    {"soundChannel", Role::Choice}, {"channels", Role::List, Role::Text},
    {"showPercent", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"showMute", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"showMeter", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"muteColor", Role::Color},
    // 1.12.24 : le Reglage PID - la boucle (des formules lues par le moteur), le bloc (ses variables, ecrites apres
    // confirmation), les echelles, l'apprentissage, le reglage, le clic, ses couleurs
    {"pidLoop", Role::Formula, Role::Text}, {"pidSp", Role::Formula, Role::Number}, {"pidPv", Role::Formula, Role::Number},
    {"pidOut", Role::Formula, Role::Number}, {"pidAuto", Role::Formula, Role::Boolean},
    {"pidKp", Role::Formula, Role::Number}, {"pidTi", Role::Formula, Role::Number}, {"pidTd", Role::Formula, Role::Number},
    {"pidForm", Role::Choice}, {"pidTimeUnit", Role::Choice}, {"pidKpMode", Role::Choice},
    {"pidKpScale", Role::Number, Role::Text, Unit::None, "REAL"}, {"pidWrite", Role::Choice},
    {"pidPvMin", Role::Number, Role::Text, Unit::None, "REAL"}, {"pidPvMax", Role::Number, Role::Text, Unit::None, "REAL"},
    {"pidOutMin", Role::Number, Role::Text, Unit::None, "REAL"}, {"pidOutMax", Role::Number, Role::Text, Unit::None, "REAL"},
    {"pidAction", Role::Choice}, {"pidPeriod", Role::Duration, Role::Text, Unit::Milliseconds, "INT"},
    {"pidLearn", Role::Choice}, {"pidMemory", Role::Number, Role::Text, Unit::None, "REAL"},
    {"pidForget", Role::Number, Role::Text, Unit::None, "REAL"}, {"pidTolerance", Role::Number, Role::Text, Unit::None, "REAL"},
    {"pidGoal", Role::Choice}, {"pidDerivative", Role::Choice}, {"pidClick", Role::Choice},
    {"colorPv", Role::Color}, {"colorSp", Role::Color}, {"colorOut", Role::Color},
    // 1.12.11 (lot G) : la liaison Equipement (le chemin d'une structure), le mode Etat (valeur -> aspect), le mode montre
    {"equipment", Role::Formula, Role::Text}, {"stateAspects", Role::Mini}, {"operatingMode", Role::Text},
    {"cylinderMode", Role::Choice},
    {"symbol", Role::Name}, {"params", Role::Mini},
    // 1.12.7 (lot E) : « Repeter depuis » - une instance par element d'une source
    {"repeatFrom", Role::Name}, {"repeatAxis", Role::Choice}, {"repeatGap", Role::Number, Role::Text, Unit::None, "REAL"},
    {"repeatPerRow", Role::Number, Role::Text, Unit::None, "INT"},
    // 1.12.7 (lot E) : « Etats depuis » - les etats d'un multi-etats, d'un chronogramme lus dans une source
    {"statesFrom", Role::Name},
    // 1.12.7 (lot D) : les colonnes choisies d'une source de tableau (Nom; Debit = Debit | 0.0)
    {"sourceColumns", Role::Mini},
    // 1.12.13 : le Tableau, un mode a la fois - le mode, les colonnes dynamiques (leurs contenus gardent leurs reperes,
    // Dupliquer les suit), le tri, la ligne choisie (une variable), le texte sans ligne, l'acces des cases fixes.
    {"tableMode", Role::Choice}, {"dynColumns", Role::Mini}, {"sortBy", Role::Choice}, {"sortOrder", Role::Choice},
    {"pickVariable", Role::Formula, Role::Text}, {"emptyText", Role::Text}, {"colAccess", Role::Mini},
    // ---- les graphiques
    {"xVariable", Role::Formula, Role::Number}, {"xLabel", Role::Text}, {"yLabel", Role::Text},
    {"reference", Role::Mini}, {"referenceColor", Role::Color},
    {"tolerance", Role::Number, Role::Text, Unit::None, "REAL"},
    {"maxPoints", Role::Number, Role::Text, Unit::None, "INT"}, {"reset", Role::Formula, Role::Boolean},
    {"hole", Role::Number, Role::Text, Unit::None, "REAL"}, {"labelMode", Role::Choice},
    {"rings", Role::Number, Role::Text, Unit::None, "INT"}, {"bins", Role::Number, Role::Text, Unit::None, "INT"},
    {"samplePeriod", Role::Duration, Role::Text, Unit::Milliseconds, "INT"},
    {"maxSamples", Role::Number, Role::Text, Unit::None, "INT"}, {"showStats", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // ---- les alarmes
    {"show", Role::Choice}, {"ackButton", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"showCount", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"blinkUnacked", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"count", Role::Choice}, {"perRow", Role::Number, Role::Text, Unit::None, "INT"}, {"colorOk", Role::Color},
    {"alarm", Role::Name}, {"showMessage", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"range", Role::Choice}, {"sort", Role::Choice}, {"top", Role::Number, Role::Text, Unit::None, "INT"},
    // ---- la production
    {"good", Role::Formula, Role::Number}, {"bad", Role::Formula, Role::Number}, {"running", Role::Formula, Role::Boolean},
    {"idealRate", Role::Number, Role::Text, Unit::None, "REAL"}, {"target", Role::Number, Role::Text, Unit::None, "REAL"},
    {"shifts", Role::List, Role::Text}, {"rateWindow", Role::Duration, Role::Text, Unit::Seconds, "REAL"},
    {"showReset", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"writable", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"varFormat", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"compare", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"exportSource", Role::Choice}, {"fileFormat", Role::Choice}, {"fileName", Role::Text},
    {"askWhere", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // ---- la navigation et la structure
    {"views", Role::List, Role::Name}, {"labels", Role::List, Role::Text}, {"tabStyle", Role::Choice},
    {"backForward", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"transition", Role::Choice},
    {"gap", Role::Number, Role::Text, Unit::None, "REAL"},
    {"activeTextColor", Role::Color}, {"buttonColor", Role::Color}, {"activeColor", Role::Color},
    {"trail", Role::Choice}, {"separator", Role::Text}, {"maxItems", Role::Number, Role::Text, Unit::None, "INT"},
    {"home", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"linkColor", Role::Color},
    {"tabs", Role::List, Role::Text}, {"page", Role::State, Role::Text, Unit::None, "INT"}, {"tabPosition", Role::Choice},
    {"tabHeight", Role::Number, Role::Text, Unit::None, "REAL"}, {"tabColor", Role::Color},
    {"title", Role::Text}, {"titleStyle", Role::Choice}, {"titleAlign", Role::Choice},
    {"titleColor", Role::Color}, {"titleFill", Role::Color},
    {"scrollDirection", Role::Choice}, {"contentWidth", Role::Number, Role::Text, Unit::None, "REAL"},
    {"contentHeight", Role::Number, Role::Text, Unit::None, "REAL"}, {"scrollX", Role::Number, Role::Text, Unit::None, "REAL"},
    {"scrollY", Role::Number, Role::Text, Unit::None, "REAL"}, {"wheelStep", Role::Number, Role::Text, Unit::None, "REAL"},
    {"showScrollbar", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"barColor", Role::Color},
    {"collapsed", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"pushBelow", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"headerHeight", Role::Number, Role::Text, Unit::None, "REAL"}, {"headerColor", Role::Color},
    {"mapZones", Role::Mini}, {"showNames", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"showCounts", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"selectedColor", Role::Color},
    {"languages", Role::List, Role::Name}, {"languageStyle", Role::Choice}, {"languageLabel", Role::Choice},
    {"themeLabels", Role::List, Role::Text},
    {"showAddress", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"showTime", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"compact", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"colorGood", Role::Color}, {"colorWarn", Role::Color}, {"colorBad", Role::Color}, {"colorSim", Role::Color},
    {"showBad", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"showButtons", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"labelColor", Role::Color},
    {"play", Role::Choice}, {"start", Role::Choice}, {"end", Role::Choice},
    // 1.12.12 (lot H) : l'horloge (format de la date, 12 h / 24 h, fuseau), le texte defilant depuis une
    // source, la video (sa Lecture, la variable de sa position)
    {"dateFormat", Role::Choice}, {"hourFormat", Role::Choice}, {"timeZone", Role::Choice},
    {"messagesFrom", Role::Name},
    {"playing", Role::Formula, Role::Boolean}, {"positionVariable", Role::Formula, Role::Number},
    // 1.12.30 : la visionneuse de documents (sa page : roleOf, un nombre ; "page" est l'onglet d'un conteneur)
    {"document", Role::Name}, {"startZoom", Role::Choice},
    {"showToolbar", Role::Boolean, Role::Text, Unit::None, "BOOL"}, {"showThumbnails", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"allowNavigation", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    // 1.14.0 (M4) : l'Enregistreur (son nom est un texte a trous) et le Lecteur d'enregistrements
    {"recName", Role::Text}, {"description", Role::Text}, {"askName", Role::Boolean, Role::Text, Unit::None, "BOOL"},
    {"maxMinutes", Role::Number, Role::Text, Unit::None, "REAL"}, {"password", Role::Text},
    {"prefix", Role::Text}, {"fixed", Role::Name},
};

// La ligne d'une cle ; nul : une cle que la table ne connait pas.
[[nodiscard]] inline const KeyRole* keyRole(std::string_view key) noexcept {
    for (const auto& r : kKeyRoles)
        if (r.key == key) return &r;
    return nullptr;
}

// LE ROLE DE LA VALEUR D'UN GENRE : ce que montre (ou ecrit) l'objet - sa propriete
// `value`, sa variable, son retour d'etat.
[[nodiscard]] inline Role valueRole(Kind k) noexcept {
    switch (k) {
        case Kind::Indicator: case Kind::PushButton: case Kind::Switch: case Kind::IlluminatedButton: case Kind::CheckBox:
        case Kind::CollapsiblePanel:
            return Role::Boolean;
        case Kind::ProgressBar: case Kind::Gauge: case Kind::NumericDisplay: case Kind::Bargraph: case Kind::Thermometer:
        case Kind::Dial: case Kind::SevenSegment: case Kind::TrendArrow: case Kind::Slider: case Kind::Knob:
        case Kind::Histogram: case Kind::IsaInstrument:
            return Role::Number;
        case Kind::MultiStateIndicator: case Kind::MultiStateText: case Kind::Selector: case Kind::ComboBox:
        case Kind::RadioGroup: case Kind::List: case Kind::ThreeWayValve: case Kind::TabContainer:
            return Role::State;
        case Kind::DateTimePicker: return Role::Date;
        case Kind::HourMeter: return Role::Duration;
        case Kind::WeeklySchedule: return Role::Text;
        default:
            if (kindHasLevel(k)) return Role::Number;
            if (kindIsSynoptic(k)) return Role::Boolean;
            return Role::Text;
    }
}

// 1.12.10 (lot F) : LES BORNES ET LES SEUILS qu'un objet peut prendre a sa variable (HmiRanges.hpp) - Min et
// Max des objets a echelle, les seuils des afficheurs et des contenants, l'axe Y d'une courbe, la plage de
// l'ouverture d'une vanne, la consigne d'un bargraphe ; Min, Max et les seuils d'un graphique en barres, d'un
// histogramme (plusieurs variables, ou une tolerance : rien a heriter). Ils se tapent aussi en nom de variable
// sans = (Consigne_Max) : une formule qui rend un nombre (avant : seules les bornes du champ de saisie).
[[nodiscard]] inline bool rangeNameKey(Kind k, std::string_view key) noexcept {
    const bool bounds = key == "min" || key == "max";
    const bool thresholds = key == "lowAlarm" || key == "low" || key == "high" || key == "highAlarm";
    switch (k) {
        case Kind::ProgressBar: case Kind::Dial: case Kind::Slider: case Kind::Knob: case Kind::InputField: case Kind::Cylinder:
            return bounds;
        case Kind::Gauge: case Kind::Thermometer: return bounds || thresholds;   // 1.12.10 : leurs seuils (ceux de la variable)
        case Kind::Bargraph: return bounds || key == "setpoint";
        case Kind::BarChart: case Kind::Histogram: return bounds || key == "low" || key == "high";
        case Kind::NumericDisplay: case Kind::IsaInstrument: return thresholds;
        case Kind::Tank: case Kind::GasBottle: case Kind::Silo: case Kind::Hopper: return bounds || thresholds;
        case Kind::Trend: return key == "ymin" || key == "ymax";
        case Kind::Valve: case Kind::Damper: case Kind::MultiBladeDamper: return key == "openingMin" || key == "openingMax";   // 1.12.24 : les registres
        default: return false;
    }
}

// 1.12.19 (la regle des cases) : L'INVITE D'UNE CASE VIDE qui prend une valeur sans qu'on la tape, partout
// la meme - « 12 · valeur par défaut » ; `origin` dit d'ou elle vient (« 10 · de Pression_Ligne »). Avant :
// « (défaut : 12) », « 100 (par défaut) », « 10 (de Pression_Ligne) », « 0 (mise à l'échelle) », « (rang 2) ».
[[nodiscard]] inline std::string defaultInvite(std::string_view value, std::string_view origin = {}) {
    if (value.empty()) return {};
    return std::string(value) + " \xC2\xB7 " + (origin.empty() ? std::string("valeur par d\xC3\xA9" "faut") : std::string(origin));
}

// LE ROLE D'UNE PROPRIETE D'UN OBJET DE CE GENRE : la table, et ce qui depend du genre.
[[nodiscard]] inline KeyRole roleOf(Kind k, std::string_view key) noexcept {
    const KeyRole* row = keyRole(key);
    KeyRole r = row ? *row : KeyRole{key, Role::Text};
    r.key = key;
    if (key == "value") {
        r.role = valueRole(k);
        if (r.role == Role::Duration || r.role == Role::Date) r.role = Role::Number;
        r.type = r.role == Role::Boolean ? "BOOL" : r.role == Role::Number ? "REAL" : "";
        if (k == Kind::InputField) { r.role = Role::Text; r.type = {}; }
        // 1.12.12 (lot H) : l'heure d'une horloge - son texte exact (une DT, une TOD, une DATE, un REAL, une STRING).
        if (k == Kind::Clock) { r.role = Role::Date; r.type = {}; }
    } else if (key == "variable") {
        // ce que la variable porte : la valeur montree ou ecrite (le texte d'un programme horaire...) ;
        // un objet qui ne montre pas de valeur (un texte, un rectangle) : quelconque.
        r.item = k == Kind::InputField ? Role::Text : valueRole(k);
    } else if (key == "state") {
        r.item = valueRole(k);
    } else if (key == "source") {
        if (k == Kind::History) r.role = Role::Choice;
    } else if (key == "count") {
        if (k == Kind::AnimatedGif) { r.role = Role::Number; r.type = "INT"; }
    } else if (key == "page") {
        if (k == Kind::DocumentViewer) { r.role = Role::Number; r.type = "INT"; }   // 1.12.30 : la page d'un document
    } else if (rangeNameKey(k, key)) {
        // une borne, un seuil se tapent aussi en formule sans = (Consigne_Max) ; 1.12.10 : plus seulement
        // celles du champ de saisie
        r.role = Role::Formula;
        r.item = Role::Number;
    }
    return r;
}

// Le role de la VALEUR d'une formule (une variable, un retour d'etat, une condition),
// d'un element de liste de formules, ou de la propriete elle-meme.
[[nodiscard]] inline Role valueRoleOf(Kind k, std::string_view key) noexcept {
    const KeyRole r = roleOf(k, key);
    if (r.role == Role::Formula) return r.item;
    if (r.role == Role::List && r.item == Role::Formula) {
        // les mesures des graphiques et des courbes sont des nombres ; un chronogramme, des etats
        if (k == Kind::StateChart) return Role::State;
        if (k == Kind::VariableTable || k == Kind::List || k == Kind::ComboBox || k == Kind::RadioGroup || k == Kind::Selector)
            return Role::Text;
        return Role::Number;
    }
    return r.role;
}

// Ce qui se lit comme une FORMULE (repere en mode Expression) : une formule, une liste
// de formules, la valeur d'une propriete pilotee.
[[nodiscard]] inline bool readsAsFormula(Kind k, std::string_view key) noexcept {
    const KeyRole r = roleOf(k, key);
    return r.role == Role::Formula || (r.role == Role::List && r.item == Role::Formula);
}

} // namespace hmi::roles

// =============================================================================
//  LE LECTEUR, LE DECOUPEUR ET L'ECRITURE (HmiRoles.cpp, bibliotheque de l'IHM)
// =============================================================================
#include "../sim/Value.hpp"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace hmi::roles {

// ---- LE LECTEUR (lot B, § 2.1 de l'audit) -----------------------------------------------
//  L'objet recoit la VALEUR, pas son texte : un adaptateur par role la convertit, avec
//  les memes regles partout (le dessin, le clic, le moteur). Une valeur qui ne convient
//  pas se voit (hachures et ###) au lieu de dessiner une valeur par defaut.

// Ce que l'adaptateur sait du type de la valeur, en plus d'elle.
struct Shape {
    std::string enumText;       // une enumeration du projet : le texte affiche de la valeur (Automatique) ; vide : pas une enumeration
    std::string enumName;       // ... et son nom ST (Auto) : un etat le compare (« Auto = ... »)
    std::string aggregate;      // une structure, un tableau, une liste : son affichage ; vide : un scalaire
    std::string aggregateWhy;   // pourquoi l'agregat ne se lit pas comme un nombre ("Pompe est une structure : ...")
};

struct Read {
    std::string text;           // ce que porte la copie evaluee (ce que lit le dessin)
    bool        ok{true};       // faux : la valeur ne convient pas au role (### a l'ecran)
    std::string why;            // pourquoi, en francais
    std::string name;           // role Etat : le nom de la valeur (le nom ST d'une enumeration, la STRING lue)
};

// La valeur `v` lue dans le role `r` (un BOOL : 1 dans une jauge, Vrai dans un texte ; un TIME : des
// secondes dans une jauge, « 1,5 s » dans un texte ; une enumeration : son numero, ou son texte).
[[nodiscard]] Read adapt(const KeyRole& r, const sim::Value& v, const Shape& shape = {});
// Le role Texte d'une valeur seule : un BOOL Vrai / Faux, un TIME « 1,5 s », une DATE 09/10/2026,
// un REAL a 6 chiffres, le reste comme formatValue.
[[nodiscard]] std::string displayText(const sim::Value& v);
// UN TEXTE TAPE, lu dans un role (la valeur fixe d'une propriete) :
//  - un booleen : TRUE, VRAI, 1, OUI, ON (et un nombre non nul) ; FALSE, FAUX, 0, NON, OFF ; le reste : `fallback` ;
//  - un nombre : la virgule acceptee, TRUE / FALSE valent 1 / 0.
[[nodiscard]] bool readBool(std::string_view text, bool fallback) noexcept;
[[nodiscard]] bool readNumber(std::string_view text, double& out) noexcept;
// Un mot vrai pour une STRING lue en booleen : TRUE, VRAI, 1, OUI, ON (sans casse, apostrophes et blancs du bord retires).
[[nodiscard]] bool trueWord(std::string_view text) noexcept;

// ---- LE DECOUPEUR (lot C, § 2.4 et 2.8) ---------------------------------------------------
//  Une liste a;b;c se coupe aux `sep` de premier niveau seulement : pas dans une chaine '...'
//  ou "...", ni entre crochets, parentheses ou accolades, ni dans un repere $...$ - la cle
//  M['a;b'], le repere $A;B$, la chaine 'x;y' restent entiers. Chaque element sans les blancs
//  du bord ; les vides gardes (un rang reste un rang). Le moteur, le dessin, Generer,
//  Dupliquer et Contenu coupent pareil.
[[nodiscard]] std::vector<std::string> splitList(std::string_view text, char sep = ';');
// Les memes morceaux, tels qu'ils sont ecrits (blancs compris) : pour reecrire une liste en place
// (un renommage, Dupliquer) sans toucher a sa mise en forme. Un texte vide : un morceau vide.
[[nodiscard]] std::vector<std::string> splitListRaw(std::string_view text, char sep = ';');
// Les elements d'une liste lus dans le role de leurs elements : un texte ou un nom sans les $
// de ses reperes ($$ : un $) ; une formule, un format telle quelle (son repere est transparent).
[[nodiscard]] std::vector<std::string> listItems(const KeyRole& r, std::string_view text);
// Une couleur ecrite : #RRGGBB, #RRGGBBAA, 16#RRGGBB, transparent, aucun (vide : aucune).
[[nodiscard]] bool colorOk(std::string_view text) noexcept;
// Un format qui ne dit pas ce qu'on croit : « 0,0 » (a la francaise) donne un entier sur deux chiffres ;
// vide : le format est sur. Sinon, la phrase pour Generer.
[[nodiscard]] std::string formatTrap(std::string_view format);
// Un NOM cherche dans le projet (vue, ressource, groupe, recette, alarme, symbole, style,
// fichier) : sans les $ de ses reperes, sans les blancs du bord.
[[nodiscard]] std::string nameOf(std::string_view text);
// Un texte montre tel quel (sans trou) : sans les $ de ses reperes ($$ : un $).
[[nodiscard]] std::string plainText(std::string_view text);
// 1.12.6 (lot C, § 2.8) : L'OBJET TEL QUE LE MOTEUR ET L'ECRAN LE LISENT - chaque propriete fixe sans les
// $ de ses reperes, selon son role : un nom (une vue, une ressource, un groupe, un symbole...) ; une
// liste de textes, de noms ou de couleurs, element par element (coupee d'abord : $A;B$ reste entier) ;
// un petit langage (les etats, le programme, les zones d'un plan). Une formule, une liste de formules,
// les cases d'un tableau, les etats d'une image animee et les arguments d'une instance gardent leurs
// reperes (ils y sont transparents). Le texte enregistre, lui, les garde tous (Dupliquer les y trouve).
void plainProps(Object& o);

// ---- L'ECRITURE (lot B, § 2.2) ---------------------------------------------------------------
//  Ce qu'un objet ecrit (une commande, un champ de saisie, un onglet, une ligne de tableau)
//  est converti vers le type de la variable : arrondi et ramene dans les bornes du type et de
//  l'objet ; un texte numerique lu, un autre refuse ; une enumeration par son nom ; une duree
//  T#5s, 5 s, 00:05, 1 h 30 ; une date en millisecondes. Un refus dit la vraie raison.
struct Target {
    sim::Type   type{sim::Type::Unknown};   // le type du moteur de la variable (celui de sa valeur)
    std::string typeName;                   // son type ecrit : INT, T_MODE, Pompe_T, ARRAY[0..9] OF REAL ; vide : inconnu
    std::string path;                       // son chemin (pour les messages)
    std::vector<std::pair<std::string, std::int64_t>> enumValues;   // une enumeration : ses valeurs (nom, numero)
    std::string aggregate;                  // un agregat : "une structure", "un tableau", "une liste"... ; vide : un scalaire
    std::string members;                    // une structure : quelques membres a proposer (Marche, Vitesse...)
    double      min{NAN}, max{NAN};         // les bornes de l'objet (curseur, champ de saisie) ; NAN : aucune
    Unit        unit{Unit::Seconds};        // un nombre nu ecrit dans un TIME
    std::string format;                     // un nombre ecrit dans une STRING : le format de la variable (vide : au plus court)
};
struct Written {
    bool        ok{false};
    sim::Value  value;                      // ce qui s'ecrit
    std::string note;                       // ramene dans les bornes, ou refuse : la phrase pour la Console
};
[[nodiscard]] Written toVariable(const sim::Value& in, const Target& target);
// Un texte tape (champ de saisie, clavier) vers le type de la variable.
[[nodiscard]] Written textToVariable(std::string_view text, const Target& target);
// Une duree tapee : T#5s, TIME#1m30s, 5 s, 1,5 s, 250 ms, 00:05 (mm:ss), 01:30:00, 1 h 30, 90 min ;
// un nombre nu dans l'unite `bare`. Faux : illisible.
[[nodiscard]] bool parseDuration(std::string_view text, std::int64_t& ms, Unit bare = Unit::Seconds);
// Les bornes d'un type entier du moteur (INT : -32768..32767) ; faux : pas un entier borne.
[[nodiscard]] bool integerBounds(std::string_view typeName, sim::Type t, long double& low, long double& high);

} // namespace hmi::roles
