// =============================================================================
//  hmi/HmiPublicVars.hpp - les variables systeme (SYS.) et les variables
//                          d'instances (Vue.Objet.Propriete)             lot 9
// -----------------------------------------------------------------------------
//  LES VARIABLES SYSTEME : ce que l'IHM sait d'elle-meme en marche - qui est
//  connecte, la date et l'heure, les vues et les popups, les alarmes, les
//  recettes, les historiques, l'automate, le projet, le poste. En LECTURE
//  SEULE (sauf, au lot 13, SYS.Language et l'affichage : SYS.TextScale,
//  SYS.ColorMode, SYS.StatusSymbols, SYS.Theme - les ecrire les change), partout ou
//  une variable se lit : une expression, un texte a trous ({SYS.UserName}), un
//  script, une condition d'alarme ou d'action.
//
//  LES VARIABLES D'INSTANCES : chaque objet pose sur une vue publie ses
//  proprietes (Vue_Commandes.Inter_Pompe.Visible, .Fill, .Text, .X, .Width...)
//  et ce que le moteur sait de lui (.Pressed, .Focused, .Enabled, .Shown...) ;
//  chaque vue, les siennes (Vue_Commandes.Open, Popup_Armoire.X...). Lues
//  partout ; ECRITES par un script ou par l'action Affecter : la propriete est
//  alors figee a cette valeur (son expression ne compte plus) jusqu'au
//  redemarrage de l'IHM. Un objet emprunte a un ecran modele (ou a un en-tete,
//  un pied de page) se lit aussi par la vue qui l'emprunte.
//
//  Les noms ne tiennent pas compte de la casse (sys.username = SYS.UserName).
//
//  EN-TETE SEUL : l'arbre du projet (ViewModels.cpp) le lit sans la
//  bibliotheque IHM ; le moteur, la verification et l'aide a la saisie aussi.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Edition.hpp"   // 1.12.0 : les domaines de XPGAnalyser IHM
#include "HmiKeys.hpp"            // 1.11.23 : SYS.Key.<touche>
#include "HmiObjectAlarms.hpp"   // 1.11.1 (decision 108) : les alarmes d'un objet (Vue.Objet.Alarmes.<alarme>)
#include "HmiTemplates.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::pub {

enum class Access : std::uint8_t { Read, ReadWrite };
[[nodiscard]] constexpr std::string_view accessLabel(Access a) noexcept { return a == Access::Read ? "R" : "R/W"; }

// ---- sans casse -------------------------------------------------------------------
[[nodiscard]] inline bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// ================================================================ systeme ===
struct SysVar {
    std::string_view name;      // "UserName" : on ecrit SYS.UserName
    int              domain;    // le rang dans kSysDomains
    std::string_view type;      // BOOL, INT, DINT, UDINT, REAL, TIME, STRING
    std::string_view text;      // ce qu'elle vaut
};

inline constexpr std::string_view kSysDomains[] = {
    "Utilisateur et s\xC3\xA9" "curit\xC3\xA9", "Date et heure", "Vues et popups", "Alarmes", "Recettes",
    "Historiques et journal", "Automate", "IHM en marche", "Projet", "Poste et application", "Ressources",
    "Param\xC3\xA8tres syst\xC3\xA8me", "Communication",
    "Esclaves simul\xC3\xA9s",      // 1.9 : 13 - les compteurs, puis une structure SYS.Slave.<nom> par esclave
    "Souris et clavier",            // 1.11.23 : 14 - la souris, les touches, les raccourcis ; SYS.Key.<touche>
};
inline constexpr std::size_t kSysDomainCount = sizeof(kSysDomains) / sizeof(kSysDomains[0]);
// 1.12.0 : XPGAnalyser IHM n'a pas d'automate - ni le domaine Automate (SYS.Plc...), ni
// Communication (la liaison vers l'automate du projet) : ils ne se montrent pas (arbre,
// volet, selecteurs, aide a la saisie). Le moteur les garde (des projets de la 1.11).
[[nodiscard]] inline bool sysDomainShown(std::size_t domain) noexcept { return core::hasApi() || (domain != 6 && domain != 12); }

inline constexpr SysVar kSysVars[] = {
    // ---- 0 : l'utilisateur et la securite
    {"UserName",            0, "STRING", "L'identifiant de l'utilisateur connect\xC3\xA9 ('' : personne)."},
    {"UserFullName",        0, "STRING", "Son nom complet (Paul Martin)."},
    {"UserGroup",           0, "STRING", "Son groupe (Op\xC3\xA9rateur, Maintenance...)."},
    {"UserLevel",           0, "INT",    "Son niveau : 0 personne, 1 op\xC3\xA9rateur, 2 maintenance, 3 superviseur, 4 administrateur ; 99 sans s\xC3\xA9" "curit\xC3\xA9."},
    {"UserRoles",           0, "STRING", "Ses r\xC3\xB4les, s\xC3\xA9par\xC3\xA9s par ; (Conduite;Maintenance)."},
    {"UserPermissions",     0, "STRING", "Ses permissions, s\xC3\xA9par\xC3\xA9" "es par ; (Naviguer;Piloter;Acquitter)."},
    {"UserLoggedIn",        0, "BOOL",   "Vrai quand quelqu'un est connect\xC3\xA9."},
    {"UserLoginTime",       0, "STRING", "L'heure de sa connexion (2026-09-23 14:05:12)."},
    {"UserSessionTime",     0, "TIME",   "Depuis quand il est connect\xC3\xA9."},
    {"AutoLogoutRemaining", 0, "TIME",   "Le temps avant la d\xC3\xA9" "connexion automatique (T#0s : pas de minuterie)."},
    {"SecurityEnabled",     0, "BOOL",   "La s\xC3\xA9" "curit\xC3\xA9 est active (Configuration > Utilisateurs)."},
    {"StartUser",           0, "STRING", "L'utilisateur connect\xC3\xA9 au lancement ('' : personne)."},
    {"UserCount",           0, "INT",    "Le nombre de comptes, actifs ou non."},
    {"CanNavigate",         0, "BOOL",   "L'utilisateur a la permission Naviguer (changer de vue)."},
    {"CanControl",          0, "BOOL",   "... la permission Piloter (commandes, affectations)."},
    {"CanAcknowledge",      0, "BOOL",   "... la permission Acquitter (les alarmes)."},
    {"CanUseRecipes",       0, "BOOL",   "... la permission Recettes."},
    {"CanRunScripts",       0, "BOOL",   "... la permission Scripts."},
    {"CanAdminister",       0, "BOOL",   "... la permission Administrer (tout)."},
    {"LoginMenuOpen",       0, "BOOL",   "Le menu de connexion est ouvert."},
    {"LoginMenuTab",        0, "STRING", "Son onglet : Connexion, Mon compte, Comptes, Acc\xC3\xA8s ou Journal ('' : ferm\xC3\xA9)."},
    {"LoginMenuTabs",       0, "STRING", "Les onglets que l'utilisateur connect\xC3\xA9 y voit, s\xC3\xA9par\xC3\xA9s par ; (Connexion;Mon compte)."},
    // lot 13 : la securite renforcee
    {"AutoLogoutWarning",   0, "BOOL",   "L'avertissement de la d\xC3\xA9" "connexion automatique est affich\xC3\xA9 (les derni\xC3\xA8res secondes)."},
    {"PasswordDaysLeft",    0, "INT",    "Les jours avant que le mot de passe de l'utilisateur connect\xC3\xA9 expire (-1 : il n'expire pas)."},
    {"PasswordRenewal",     0, "BOOL",   "Un mot de passe expir\xC3\xA9 (ou \xC3\xA0 changer \xC3\xA0 la premi\xC3\xA8re connexion) attend son remplacement."},
    {"LockedAccountCount",  0, "INT",    "Les comptes verrouill\xC3\xA9s (trop d'\xC3\xA9" "checs de suite)."},
    {"SignaturePending",    0, "BOOL",   "Une commande attend sa signature \xC3\xA9lectronique."},
    {"LastSignature",       0, "STRING", "La derni\xC3\xA8re signature : qui, le motif, quand ('' : aucune)."},
    {"BadgeLogin",          0, "BOOL",   "La connexion par badge est permise (Configuration > Utilisateurs)."},
    // ---- 1 : la date et l'heure (celles du poste)
    {"DateTime",            1, "STRING", "La date et l'heure : 2026-09-23 14:05:12."},
    {"Date",                1, "STRING", "La date : 2026-09-23."},
    {"Time",                1, "STRING", "L'heure : 14:05:12."},
    {"Year",                1, "INT",    "L'ann\xC3\xA9" "e (2026)."},
    {"Month",               1, "INT",    "Le mois, de 1 \xC3\xA0 12."},
    {"Day",                 1, "INT",    "Le jour du mois, de 1 \xC3\xA0 31."},
    {"Hour",                1, "INT",    "L'heure, de 0 \xC3\xA0 23."},
    {"Minute",              1, "INT",    "La minute, de 0 \xC3\xA0 59."},
    {"Second",              1, "INT",    "La seconde, de 0 \xC3\xA0 59."},
    {"Millisecond",         1, "INT",    "La milliseconde, de 0 \xC3\xA0 999."},
    {"WeekDay",             1, "INT",    "Le jour de la semaine : 1 lundi ... 7 dimanche."},
    {"WeekDayName",         1, "STRING", "Son nom : lundi, mardi..."},
    {"MonthName",           1, "STRING", "Le nom du mois : janvier, f\xC3\xA9vrier..."},
    {"WeekNumber",          1, "INT",    "La semaine de l'ann\xC3\xA9" "e (ISO 8601)."},
    {"DayOfYear",           1, "INT",    "Le jour de l'ann\xC3\xA9" "e, de 1 \xC3\xA0 366."},
    {"UnixTime",            1, "UDINT",  "Les secondes depuis le 1er janvier 1970 (UTC)."},
    {"TimeZone",            1, "STRING", "Le d\xC3\xA9" "calage du poste : UTC+02:00."},
    {"DaylightSaving",      1, "BOOL",   "Vrai \xC3\xA0 l'heure d'\xC3\xA9t\xC3\xA9."},
    // ---- 2 : les vues et les popups
    {"CurrentView",         2, "STRING", "La vue affich\xC3\xA9" "e, sous les popups."},
    {"PreviousView",        2, "STRING", "La vue d'avant la derni\xC3\xA8re navigation ('' : aucune)."},
    {"StartView",           2, "STRING", "La vue de d\xC3\xA9marrage (Configuration)."},
    {"TopView",             2, "STRING", "Celle qui re\xC3\xA7oit les clics : la popup du dessus, sinon la vue."},
    {"PopupCount",          2, "INT",    "Le nombre de popups ouvertes."},
    {"TopPopup",            2, "STRING", "La popup du dessus ('' : aucune)."},
    {"ModalOpen",           2, "BOOL",   "Une popup modale est ouverte."},
    {"ViewCount",           2, "INT",    "Le nombre de vues du projet."},
    {"NavigationCount",     2, "DINT",   "Les navigations depuis le lancement."},
    {"TransitionRunning",   2, "BOOL",   "Une transition anim\xC3\xA9" "e est en cours."},
    {"FocusedObject",       2, "STRING", "Le champ qui a le clavier (Vue.Objet ; '' : aucun)."},
    {"PressedObject",       2, "STRING", "L'objet enfonc\xC3\xA9 par la souris (Vue.Objet ; '' : aucun)."},
    {"ScreenWidth",         2, "INT",    "La largeur de l'\xC3\xA9" "cran (Configuration), en pixels."},
    {"ScreenHeight",        2, "INT",    "Sa hauteur."},
    {"Orientation",         2, "STRING", "Paysage ou Portrait."},
    // lot 12 : l'historique de navigation, l'accueil, le fil d'Ariane, le zoom
    {"CanGoBack",           2, "BOOL",   "Il y a une vue pr\xC3\xA9" "c\xC3\xA9" "dente (Vue pr\xC3\xA9" "c\xC3\xA9" "dente, IHM_PRECEDENTE)."},
    {"CanGoForward",        2, "BOOL",   "Il y a une vue suivante (apr\xC3\xA8s un retour en arri\xC3\xA8re)."},
    {"HistoryDepth",        2, "INT",    "Le nombre de vues derri\xC3\xA8re la vue courante (au plus 50)."},
    {"HomeView",            2, "STRING", "La vue d'accueil : celle du groupe de l'utilisateur connect\xC3\xA9, sinon la vue de d\xC3\xA9marrage."},
    {"NavigationPath",      2, "STRING", "Le chemin de la vue courante par ses vues parentes : Accueil > Production > Ligne 1."},
    {"ViewZoom",            2, "REAL",   "Le zoom de la vue courante en marche, en % (100 : ajust\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9" "cran)."},
    // ---- 3 : les alarmes
    {"AlarmCount",          3, "INT",    "Les alarmes de la liste : actives ou pas encore acquitt\xC3\xA9" "es."},
    {"AlarmActiveCount",    3, "INT",    "Celles dont la condition est vraie."},
    {"AlarmUnackCount",     3, "INT",    "Celles \xC3\xA0 acquitter."},
    {"AlarmHighestPriority",3, "INT",    "La priorit\xC3\xA9 la plus forte parmi celles \xC3\xA0 acquitter (1 = la plus forte ; 0 : aucune)."},
    {"AlarmLastName",       3, "STRING", "La derni\xC3\xA8re alarme apparue ('' : aucune)."},
    {"AlarmLastMessage",    3, "STRING", "Son message."},
    {"AlarmLastTime",       3, "STRING", "L'heure de son apparition."},
    {"AlarmDefinedCount",   3, "INT",    "Les alarmes d\xC3\xA9" "finies dans le projet."},
    {"AlarmHistoryCount",   3, "INT",    "Les alarmes termin\xC3\xA9" "es depuis le lancement."},
    {"AlarmShelvedCount",   3, "INT",    "Les alarmes mises de c\xC3\xB4t\xC3\xA9 (elles n'apparaissent plus jusqu'\xC3\xA0 la fin du d\xC3\xA9lai)."},
    {"AlarmSilenced",       3, "BOOL",   "Le son des alarmes est coup\xC3\xA9 (Faire taire) jusqu'\xC3\xA0 la prochaine apparition."},
    {"AlarmSelected",       3, "STRING", "L'alarme choisie d'un clic (bandeau, liste, r\xC3\xA9sum\xC3\xA9) ; '' : aucune."},
    {"AlarmZoneSelected",   3, "STRING", "La zone choisie d'un clic dans un r\xC3\xA9sum\xC3\xA9 par zone ; '' : aucune."},
    // ---- 4 : les recettes
    {"RecipeCount",         4, "INT",    "Les recettes du projet."},
    {"RecipeRecordCount",   4, "INT",    "Leurs jeux, en tout."},
    {"RecipeLastApplied",   4, "STRING", "Le dernier jeu appliqu\xC3\xA9 (Recette/Jeu ; '' : aucun)."},
    {"RecipeLastTime",      4, "STRING", "L'heure o\xC3\xB9 il l'a \xC3\xA9t\xC3\xA9."},
    {"RecipeApplyCount",    4, "INT",    "Les jeux appliqu\xC3\xA9s depuis le lancement."},
    // ---- 5 : les historiques et le journal
    {"HistoryAlarms",       5, "BOOL",   "L'historique des alarmes est tenu (Configuration > Historiques)."},
    {"HistoryEvents",       5, "BOOL",   "L'historique des \xC3\xA9v\xC3\xA9nements est tenu."},
    {"HistorySystem",       5, "BOOL",   "Le journal syst\xC3\xA8me est gard\xC3\xA9."},
    {"ArchivedVariableCount",5,"INT",    "Les variables archiv\xC3\xA9" "es (les mesures)."},
    {"EventCount",          5, "INT",    "Les \xC3\xA9v\xC3\xA9nements depuis le lancement (connexions, acquittements...)."},
    {"JournalCount",        5, "INT",    "Les lignes du journal de la simulation."},
    {"LastJournal",         5, "STRING", "La derni\xC3\xA8re ligne du journal."},
    {"ErrorCount",          5, "INT",    "Les erreurs du journal."},
    {"LastError",           5, "STRING", "La derni\xC3\xA8re erreur du journal ('' : aucune)."},
    {"LastExport",          5, "STRING", "Le dernier fichier export\xC3\xA9 (bouton d'export, action Exporter) ; '' : aucun."},
    {"ExportCount",         5, "INT",    "Les exports depuis le lancement."},
    {"AuditEnabled",        5, "BOOL",   "Le journal d'audit est tenu (Configuration > Historiques)."},
    {"AuditCount",          5, "DINT",   "Les lignes du journal d'audit."},
    // ---- 6 : l'automate
    {"PlcConnected",        6, "BOOL",   "L'IHM est reli\xC3\xA9" "e au simulateur de l'automate."},
    {"PlcRunning",          6, "BOOL",   "L'automate tourne (Marche)."},
    {"PlcPaused",           6, "BOOL",   "L'automate est en pause."},
    {"PlcStatus",           6, "STRING", "En marche, En pause, Arr\xC3\xAAt\xC3\xA9, En d\xC3\xA9" "faut ou Absent."},
    {"PlcScanCount",        6, "UDINT",  "Les cycles de l'automate depuis sa mise en marche."},
    {"PlcCycleTime",        6, "INT",    "La p\xC3\xA9riode de ses cycles, en ms."},
    {"PlcForcedCount",      6, "INT",    "Les variables forc\xC3\xA9" "es."},
    {"PlcError",            6, "STRING", "Le dernier d\xC3\xA9" "faut du simulateur ('' : aucun)."},
    {"PlcProject",          6, "STRING", "Le projet de l'automate."},
    // ---- 7 : l'IHM en marche
    {"Running",             7, "BOOL",   "L'IHM est en marche."},
    {"StartTime",           7, "STRING", "L'heure de son lancement."},
    {"Uptime",              7, "TIME",   "Depuis quand elle tourne."},
    {"UptimeSeconds",       7, "DINT",   "La m\xC3\xAAme dur\xC3\xA9" "e, en secondes."},
    {"CycleTime",           7, "INT",    "La p\xC3\xA9riode du cycle IHM, en ms (Configuration)."},
    {"CycleCount",          7, "DINT",   "Les cycles IHM depuis le lancement."},
    {"IdleTime",            7, "TIME",   "Depuis le dernier clic ou la derni\xC3\xA8re touche de l'op\xC3\xA9rateur."},
    {"ScriptCount",         7, "INT",    "Les scripts g\xC3\xA9n\xC3\xA9raux."},
    {"ScriptErrorCount",    7, "INT",    "Les scripts dont la derni\xC3\xA8re ex\xC3\xA9" "cution a \xC3\xA9" "chou\xC3\xA9."},
    {"SoundCount",          7, "INT",    "Les sons jou\xC3\xA9s depuis le lancement."},
    {"LastSound",           7, "STRING", "Le dernier son jou\xC3\xA9 ('' : aucun)."},
    {"OverrideCount",       7, "INT",    "Les propri\xC3\xA9t\xC3\xA9s d'objets \xC3\xA9" "crites en marche (variables d'instances)."},
    // lot 13 : les performances
    {"CycleLoad",           7, "REAL",   "Le travail du dernier cycle IHM, en ms (scripts, alarmes, \xC3\xA9" "chantillons)."},
    {"CycleLoadMax",        7, "REAL",   "Le plus long depuis le lancement (ou la remise \xC3\xA0 z\xC3\xA9ro du relev\xC3\xA9), en ms."},
    {"CycleOverruns",       7, "DINT",   "Les cycles plus longs que leur p\xC3\xA9riode (l'IHM prend du retard)."},
    {"PaintTime",           7, "REAL",   "Le dernier dessin de la vue, en ms (mesur\xC3\xA9 par l'\xC3\xA9" "cran)."},
    {"ExpressionCount",     7, "INT",    "Les expressions \xC3\xA9valu\xC3\xA9" "es au dernier rafra\xC3\xAE" "chissement de la vue."},
    // ---- 8 : le projet
    {"ProjectName",         8, "STRING", "Le nom du projet IHM."},
    {"ProjectDescription",  8, "STRING", "Sa description."},
    {"ProjectVersion",      8, "STRING", "Sa version."},
    {"ProjectAuthor",       8, "STRING", "Son auteur."},
    {"ProjectCreated",      8, "STRING", "Sa date de cr\xC3\xA9" "ation."},
    {"ProjectModified",     8, "STRING", "Sa derni\xC3\xA8re modification."},
    {"VariableCount",       8, "INT",    "Les variables IHM."},
    {"FunctionCount",       8, "INT",    "Les fonctions IHM."},
    {"ObjectCount",         8, "INT",    "Les objets de toutes les vues."},
    // ---- 9 : le poste et l'application
    {"ComputerName",        9, "STRING", "Le nom du poste."},
    {"OsName",              9, "STRING", "Son syst\xC3\xA8me : Windows, Linux ou macOS."},
    {"OsUser",              9, "STRING", "Le compte du syst\xC3\xA8me qui fait tourner l'application."},
    {"ProcessorCount",      9, "INT",    "Les processeurs du poste."},
    {"AppName",             9, "STRING", "L'application : XpgAnalyzer."},
    {"AppVersion",          9, "STRING", "Sa version."},
    {"Language",            9, "STRING", "La langue de l'IHM en marche (fr, en...) ; l'\xC3\xA9" "crire la change : SYS.Language := 'en'."},
    // lot 13 : les langues (Configuration > Langues)
    {"LanguageName",        9, "STRING", "Son nom (Fran\xC3\xA7" "ais, English...)."},
    {"LanguageCount",       9, "INT",    "Les langues du projet."},
    {"LanguageList",        9, "STRING", "Leurs codes, s\xC3\xA9par\xC3\xA9s par ; (fr;en;de)."},
    {"LanguageChanges",     9, "INT",    "Les changements de langue depuis le lancement."},
    // ---- 10 : les ressources
    {"ResourceCount",       10, "INT",   "Les ressources du projet (images, sons, vid\xC3\xA9os, polices)."},
    {"ImageCount",          10, "INT",   "Les images."},
    {"SoundResourceCount",  10, "INT",   "Les sons."},
    {"VideoCount",          10, "INT",   "Les vid\xC3\xA9os."},
    {"FontCount",           10, "INT",   "Les polices."},
    {"ExternalFileCount",   10, "INT",   "Les fichiers externes li\xC3\xA9s."},
    // ---- 11 : les parametres systeme (lot 10 : le menu natif et les reglages du poste)
    {"SystemMenuOpen",      11, "BOOL",   "Le menu Param\xC3\xA8tres syst\xC3\xA8me est ouvert."},
    {"SystemMenuTab",       11, "STRING", "Son onglet : R\xC3\xA9glages, Diagnostic ou Simulation ('' : ferm\xC3\xA9)."},
    {"Brightness",          11, "INT",    "La luminosit\xC3\xA9 de l'\xC3\xA9" "cran, de 20 \xC3\xA0 100 %."},
    {"ScreenSaverMinutes",  11, "INT",    "La mise en veille, en minutes sans toucher (0 : jamais)."},
    {"ScreenSaverActive",   11, "BOOL",   "L'\xC3\xA9" "cran est en veille."},
    {"SoundEnabled",        11, "BOOL",   "Les sons de l'IHM sont activ\xC3\xA9s."},
    {"Volume",              11, "INT",    "Le volume des sons, de 0 \xC3\xA0 100 %."},
    {"AutoLogoutMinutes",   11, "INT",    "La d\xC3\xA9" "connexion automatique en vigueur, en minutes (0 : jamais) : celle du poste, sinon celle du projet."},
    {"AutoLogoutFromProject",11,"BOOL",   "Le poste suit la d\xC3\xA9" "connexion automatique du projet (Configuration > Utilisateurs)."},
    {"KeyboardMode",        11, "STRING", "Le clavier virtuel : automatique, toujours ou jamais."},
    {"ClockOffset",         11, "DINT",   "L'\xC3\xA9" "cart de l'heure de l'IHM avec celle du poste, en secondes (0 : celle du poste)."},
    {"DeviceDateTime",      11, "STRING", "La date et l'heure du poste, sans l'\xC3\xA9" "cart : 2026-09-23 14:05:12."},
    // lot 13 : l'affichage (en lecture et en ecriture)
    {"TextScale",           11, "INT",    "La taille des textes, en % (100, 125, 150, 175) ; l'\xC3\xA9" "crire la change."},
    {"ColorMode",           11, "STRING", "Les couleurs : normal ou daltonien (bleu et vermillon au lieu de vert et rouge) ; s'\xC3\xA9" "crit."},
    {"StatusSymbols",       11, "BOOL",   "Des symboles sur les voyants (l'\xC3\xA9tat se lit sans la couleur) ; s'\xC3\xA9" "crit."},
    {"Theme",               11, "STRING", "Le th\xC3\xA8me : nuit (les couleurs de la conception) ou jour (claires) ; s'\xC3\xA9" "crit."},
    // ---- 12 : la communication (lot 14)
    {"CommMode",            12, "STRING", "La liaison avec l'automate : simulateur, ou modbus (un automate r\xC3\xA9" "el, Configuration > Communication)."},
    {"CommConnected",       12, "BOOL",   "L'automate r\xC3\xA9" "el r\xC3\xA9pond (Modbus TCP) ; sur le simulateur : vrai s'il est reli\xC3\xA9."},
    {"CommState",           12, "STRING", "Connect\xC3\xA9" "e, D\xC3\xA9" "connect\xC3\xA9" "e, Connexion... ; Simulateur sur le simulateur."},
    {"CommAddress",         12, "STRING", "L'adresse de l'automate : 192.168.1.10:502 (esclave 255)."},
    {"CommSince",           12, "STRING", "L'heure du dernier changement d'\xC3\xA9tat (connexion, coupure)."},
    {"CommDevice",          12, "STRING", "L'identification de l'automate (fonction 43) : fabricant, produit, version."},
    {"CommResponseTime",    12, "REAL",   "Le temps de r\xC3\xA9ponse de la derni\xC3\xA8re requ\xC3\xAAte, en ms."},
    {"CommResponseTimeAvg", 12, "REAL",   "Le temps de r\xC3\xA9ponse moyen, en ms."},
    {"CommResponseTimeMax", 12, "REAL",   "Le plus long temps de r\xC3\xA9ponse, en ms."},
    {"CommCycleTime",       12, "REAL",   "La dur\xC3\xA9" "e du dernier cycle de lecture (toutes les requ\xC3\xAAtes), en ms."},
    {"CommRequests",        12, "UDINT",  "Les requ\xC3\xAAtes envoy\xC3\xA9" "es (depuis la remise \xC3\xA0 z\xC3\xA9ro des compteurs)."},
    {"CommErrors",          12, "UDINT",  "Les requ\xC3\xAAtes en erreur : d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9, exception, coupure."},
    {"CommTimeouts",        12, "UDINT",  "Les d\xC3\xA9lais d\xC3\xA9pass\xC3\xA9s."},
    {"CommReconnects",      12, "UDINT",  "Les reconnexions apr\xC3\xA8s une coupure."},
    {"CommLastError",       12, "STRING", "La derni\xC3\xA8re erreur de la liaison ('' : aucune)."},
    {"CommGoodCount",       12, "INT",    "Les variables lues de bonne qualit\xC3\xA9."},
    {"CommStaleCount",      12, "INT",    "Les variables de qualit\xC3\xA9 ancienne (la derni\xC3\xA8re valeur, pas relue)."},
    {"CommBadCount",        12, "INT",    "Les variables de mauvaise qualit\xC3\xA9 (jamais lues, refus\xC3\xA9" "es, sans adresse)."},
    {"CommReadOnly",        12, "BOOL",   "La liaison est en lecture seule : rien n'est \xC3\xA9" "crit dans l'automate."},
    {"CommDemoServer",      12, "BOOL",   "Le serveur de d\xC3\xA9monstration tourne : le simulateur est expos\xC3\xA9 en Modbus TCP."},
    // lot 15 : les equipements du reseau (Configuration > Equipements)
    {"EquipCount",          12, "INT",    "Les \xC3\xA9quipements du r\xC3\xA9seau actifs (Configuration > \xC3\x89quipements)."},
    {"EquipOnline",         12, "INT",    "Les \xC3\xA9quipements qui r\xC3\xA9pondent : liaison Modbus connect\xC3\xA9" "e, ou ping re\xC3\xA7u."},
    {"EquipOffline",        12, "INT",    "Les \xC3\xA9quipements qui ne r\xC3\xA9pondent pas (simul\xC3\xA9s exclus)."},
    {"EquipOfflineNames",   12, "STRING", "Leurs noms, s\xC3\xA9par\xC3\xA9s par des virgules ('' : tous r\xC3\xA9pondent)."},
    {"EquipSimulated",      12, "INT",    "Les \xC3\xA9quipements simul\xC3\xA9s (leur m\xC3\xA9moire dans l'application)."},
    // lot 14 : le poste d'exploitation
    {"StationMode",         7, "BOOL",   "L'IHM tourne en poste d'exploitation (xpg_analyzer --ihm : plein \xC3\xA9" "cran, sans l'\xC3\xA9" "diteur)."},
    {"StationScreens",      7, "INT",    "Les \xC3\xA9" "crans du poste : 1, plus les \xC3\xA9" "crans secondaires qui montrent une vue."},
    // lot 14 : les notifications, les rapports, l'acces web
    {"NotifyEnabled",       3, "BOOL",   "Les notifications des alarmes sont actives (Configuration > Notifications)."},
    {"NotifySent",          3, "UDINT",  "Les courriels et SMS envoy\xC3\xA9s depuis le lancement."},
    {"NotifyFailed",        3, "UDINT",  "Les envois en \xC3\xA9" "chec (relais ou passerelle injoignable, refus)."},
    {"NotifyLast",          3, "STRING", "Le dernier envoi : l'heure, le canal, le destinataire, l'alarme ('' : aucun)."},
    {"ReportLast",          5, "STRING", "Le dernier rapport p\xC3\xA9riodique \xC3\xA9" "crit : son fichier ('' : aucun)."},
    {"WebClients",          9, "INT",    "Les navigateurs connect\xC3\xA9s \xC3\xA0 l'acc\xC3\xA8s web (Configuration > Acc\xC3\xA8s web)."},
    // ---- 13 : les esclaves simules (1.9 : la page Simulation de Parametres systeme)
    {"SimSlaveCount",       13, "INT",    "Les esclaves simul\xC3\xA9s (li\xC3\xA9s et seulement simul\xC3\xA9s)."},
    {"SimSlavesRunning",    13, "INT",    "Ceux qui sont en marche."},
    {"SimReads",            13, "BOOL",   "L'IHM lit au moins un \xC3\xA9quipement sur un esclave simul\xC3\xA9."},
    {"SimReadNames",        13, "STRING", "Lesquels, s\xC3\xA9par\xC3\xA9s par des virgules."},
    {"SimFallbacks",        13, "INT",    "Les bascules automatiques en cours (le vrai ne r\xC3\xA9pond pas)."},
    {"SimAnimated",         13, "INT",    "Les valeurs anim\xC3\xA9" "es."},
    {"SimForced",           13, "INT",    "Les cases forc\xC3\xA9" "es."},
    // ---- 14 : la souris et le clavier (1.11.23) - sur la vue qui tourne (la simulation de
    //      l'editeur, le poste d'exploitation) ; les raccourcis des vues (Action::key)
    {"MouseX",              14, "REAL",   "La position de la souris sur la vue, en pixels de la vue (depuis la gauche)."},
    {"MouseY",              14, "REAL",   "... depuis le haut."},
    {"MouseView",           14, "STRING", "La vue (ou la popup) sous la souris ('' : hors de l'IHM)."},
    {"MouseObject",         14, "STRING", "L'objet sous la souris (Vue.Objet ; '' : aucun)."},
    {"MouseInside",         14, "BOOL",   "La souris est sur l'IHM."},
    {"MouseLeft",           14, "BOOL",   "Le bouton gauche est enfonc\xC3\xA9."},
    {"MouseRight",          14, "BOOL",   "Le bouton droit est enfonc\xC3\xA9."},
    {"MouseMiddle",         14, "BOOL",   "Le bouton du milieu (la molette) est enfonc\xC3\xA9."},
    {"MouseButtons",        14, "INT",    "Les boutons enfonc\xC3\xA9s : 1 gauche + 2 droit + 4 milieu."},
    {"MouseWheel",          14, "DINT",   "Les crans de molette depuis le lancement (vers le haut : +1, vers le bas : -1)."},
    {"KeyLast",             14, "STRING", "La derni\xC3\xA8re touche enfonc\xC3\xA9" "e, avec Ctrl, Maj, Alt (Ctrl+F5 ; '' : aucune)."},
    {"KeysDown",            14, "STRING", "Les touches tenues, s\xC3\xA9par\xC3\xA9" "es par ; (F5;Haut ; '' : aucune)."},
    {"KeyDownCount",        14, "INT",    "Le nombre de touches tenues."},
    {"KeyAnyDown",          14, "BOOL",   "Une touche au moins est tenue."},
    {"KeyCtrl",             14, "BOOL",   "Ctrl est tenue."},
    {"KeyShift",            14, "BOOL",   "Maj est tenue."},
    {"KeyAlt",              14, "BOOL",   "Alt est tenue."},
    {"KeyHoldTime",         14, "TIME",   "Depuis quand la derni\xC3\xA8re touche enfonc\xC3\xA9" "e est tenue (T#0s : rel\xC3\xA2" "ch\xC3\xA9" "e)."},
    {"KeyPresses",          14, "DINT",   "Les touches enfonc\xC3\xA9" "es depuis le lancement."},
    {"ShortcutLast",        14, "STRING", "Le dernier raccourci parti : Vue \xC2\xB7 touche ('' : aucun)."},
    {"ShortcutCount",       14, "DINT",   "Les raccourcis partis depuis le lancement."},
};
inline constexpr std::size_t kSysVarCount = sizeof(kSysVars) / sizeof(kSysVars[0]);

// "SYS" (sans casse).
[[nodiscard]] inline bool isSysRoot(std::string_view root) noexcept { return same(root, "SYS"); }
// Lot 13 : SYS.Language et les quatre reglages de l'affichage s'ecrivent ; les autres se lisent.
[[nodiscard]] inline Access sysAccess(const SysVar& v) noexcept {
    return same(v.name, "Language") || same(v.name, "TextScale") || same(v.name, "ColorMode") || same(v.name, "StatusSymbols")
                || same(v.name, "Theme")
             ? Access::ReadWrite
             : Access::Read;
}
// La variable systeme de ce nom (sans "SYS.", sans casse) ; nul sinon.
[[nodiscard]] inline const SysVar* sysVar(std::string_view name) noexcept {
    for (const auto& v : kSysVars) if (same(v.name, name)) return &v;
    return nullptr;
}
// Les variables d'un domaine, dans l'ordre du catalogue.
[[nodiscard]] inline std::vector<const SysVar*> sysVarsOf(int domain) {
    std::vector<const SysVar*> out;
    for (const auto& v : kSysVars) if (v.domain == domain) out.push_back(&v);
    return out;
}

// ============================================================== instances ===
//  Ce que le moteur sait d'un objet (en lecture), et d'une vue.
struct InfoVar {
    std::string_view name;
    std::string_view type;
    Access           access;
    std::string_view text;
};
inline constexpr InfoVar kObjectInfo[] = {
    {"Name",    "STRING", Access::Read, "Son nom."},
    {"Type",    "STRING", Access::Read, "Son genre (Bouton, Curseur...)."},
    {"Id",      "DINT",   Access::Read, "Son identifiant dans le projet."},
    {"Layer",   "STRING", Access::Read, "Son calque."},
    {"Shown",   "BOOL",   Access::Read, "Il est \xC3\xA0 l'\xC3\xA9" "cran : sa vue est ouverte et il est visible."},
    {"Pressed", "BOOL",   Access::Read, "La souris l'enfonce en ce moment."},
    {"Focused", "BOOL",   Access::Read, "Il a le clavier (un champ de saisie)."},
    {"Enabled", "BOOL",   Access::Read, "L'utilisateur connect\xC3\xA9 peut agir dessus (niveau d'acc\xC3\xA8s, autorisation, permission Piloter)."},
};
// Lot 16 : ce que le moteur sait d'un GIF anime, en plus.
inline constexpr InfoVar kGifInfo[] = {
    {"Playing",    "BOOL", Access::Read, "Le GIF joue (ni en pause, ni arr\xC3\xAAt\xC3\xA9, ni fini)."},
    {"Frame",      "INT",  Access::Read, "L'image montr\xC3\xA9" "e, \xC3\xA0 partir de 0."},
    {"FrameCount", "INT",  Access::Read, "Les images du GIF."},
    {"Loops",      "DINT", Access::Read, "Les tours faits depuis le d\xC3\xA9part."},
};
// 1.9 : le groupe d'alarmes interne d'un objet qui porte des alarmes (un objet du
// synoptique, une instance de symbole : ses objets compris) - HmiObjectAlarms.hpp.
inline constexpr InfoVar kAlarmInfo[] = {
    {"AlarmActive",      "BOOL",   Access::Read, "Au moins une de ses alarmes a sa condition vraie."},
    {"AlarmUnacked",     "BOOL",   Access::Read, "Au moins une de ses alarmes est \xC3\xA0 acquitter."},
    {"AlarmCount",       "INT",    Access::Read, "Ses alarmes en cours : actives ou pas encore acquitt\xC3\xA9" "es (une instance : avec celles de ses objets)."},
    {"AlarmActiveCount", "INT",    Access::Read, "Celles dont la condition est vraie (0 : aucune)."},
    {"AlarmUnackCount",  "INT",    Access::Read, "Celles \xC3\xA0 acquitter (0 : aucune)."},
    {"AlarmHighest",     "INT",    Access::Read, "La priorit\xC3\xA9 la plus forte en cours (1 = critique ; 0 : aucune)."},
    {"AlarmGroup",       "STRING", Access::Read, "Le nom de son groupe d'alarmes interne (Vue.Objet) : un filtre pour les objets d'alarmes."},
    // 1.11.1 (decision 108) : le groupe de l'IHM auquel ce groupe interne est lie.
    {"AlarmLinkedGroup", "STRING", Access::Read, "Le groupe d'alarmes de l'IHM (IHM > Alarmes > Groupes) auquel son groupe interne est li\xC3\xA9 ('' : aucun lien)."},
};
// L'objet a-t-il un groupe d'alarmes interne (un objet du synoptique, une instance) ?
[[nodiscard]] inline bool hasAlarmGroup(const Object& o) noexcept { return kindIsSynoptic(o.kind) || o.kind == Kind::SymbolInstance; }

// 1.11.1 (decision 108) : CHAQUE ALARME D'UN OBJET, par son nom dans l'objet :
//  Vue.Objet.Alarmes.<alarme>.<membre> (<alarme> : Defaut, Niveau_Bas... ; dans une
//  instance, celles de ses objets aussi : Vue.Pompe_3.Alarmes.Moteur.Defaut.Active).
//  "Alarms" s'ecrit aussi. Acked et Shelved s'ecrivent : c'est ce que l'operateur
//  peut faire d'une alarme (acquitter ; mettre de cote, faire revenir), avec la
//  permission Acquitter quand la securite est active. Le reste se lit.
inline constexpr std::string_view kAlarmsRoot = "Alarmes";
inline constexpr std::string_view kAlarmsRootAlias = "Alarms";
inline constexpr InfoVar kAlarmMembers[] = {
    {"Active",   "BOOL",   Access::Read,      "Sa condition est vraie : l'alarme est apparue (apr\xC3\xA8s son d\xC3\xA9lai) et n'a pas encore disparu."},
    {"Unacked",  "BOOL",   Access::Read,      "Elle attend son acquittement (active ou disparue)."},
    {"Acked",    "BOOL",   Access::ReadWrite, "Rien n'est \xC3\xA0 acquitter (FALSE : elle attend son acquittement). \xC3\x89" "crire TRUE l'acquitte (permission Acquitter) ; FALSE est refus\xC3\xA9."},
    {"Shelved",  "BOOL",   Access::ReadWrite, "Elle est mise de c\xC3\xB4t\xC3\xA9 (masqu\xC3\xA9" "e : elle n'appara\xC3\xAEt plus jusqu'\xC3\xA0 la fin du d\xC3\xA9lai). \xC3\x89" "crire TRUE la met de c\xC3\xB4t\xC3\xA9 (sans limite de temps : elle quitte la liste), FALSE la fait revenir (permission Acquitter)."},
    {"Enabled",  "BOOL",   Access::Read,      "Coch\xC3\xA9" "e : elle vit en marche (une surcharge de l'objet peut la d\xC3\xA9" "cocher)."},
    {"State",    "STRING", Access::Read,      "Active, Acquitt\xC3\xA9" "e, Disparue \xC3\xA0 acquitter, Active sans acquittement ('' : pas dans la liste des alarmes)."},
    {"Priority", "INT",    Access::Read,      "Sa priorit\xC3\xA9 (1 = critique)."},
    {"Message",  "STRING", Access::Read,      "Son message."},
    {"Group",    "STRING", Access::Read,      "Son groupe d'alarmes : celui de sa d\xC3\xA9" "finition, ou le groupe de l'IHM li\xC3\xA9."},
    {"Name",     "STRING", Access::Read,      "Son nom dans la liste des alarmes : Vue.Objet.Alarme."},
};
[[nodiscard]] inline bool isAlarmsRoot(std::string_view s) noexcept { return same(s, kAlarmsRoot) || same(s, kAlarmsRootAlias); }
[[nodiscard]] inline const InfoVar* alarmMember(std::string_view name) noexcept {
    for (const auto& m : kAlarmMembers) if (same(m.name, name)) return &m;
    return nullptr;
}
// Une alarme d'un objet, telle que l'arbre la montre : son nom dans l'objet
// ("Defaut", "Moteur.Defaut") et son nom complet dans la liste ("Vue.Pompe.Defaut").
struct ObjectAlarmName {
    std::string local, full;
    bool        enabled{true};                 // cochee
    int         priority{3};                   // dans l'editeur : sa definition (surcharge comprise)
    std::string message, group;
};
// Les alarmes de l'objet pose `o` de la vue `v` (une instance : celles de ses objets aussi).
[[nodiscard]] inline std::vector<ObjectAlarmName> objectAlarmNames(const Project& p, const View& v, const Object& o) {
    std::vector<ObjectAlarmName> out;
    if (!hasAlarmGroup(o) || !viewGeneratesAlarms(v)) return out;
    const std::string group = objectGroupOf(v, o) + ".";
    for (const auto& a : objectAlarmsOf(p, v, o)) {
        ObjectAlarmName n;
        n.full = a.def.name;
        n.local = n.full.size() > group.size() && same(std::string_view(n.full).substr(0, group.size()), group) ? n.full.substr(group.size())
                                                                                                                 : a.localName;
        n.enabled = a.active;
        n.priority = a.def.priority;
        n.message = a.def.message;
        n.group = a.def.group;
        out.push_back(std::move(n));
    }
    return out;
}
inline constexpr InfoVar kViewInfo[] = {
    {"Name",        "STRING", Access::Read,      "Son nom."},
    {"Description", "STRING", Access::Read,      "Sa description."},
    {"Role",        "STRING", Access::Read,      "Vue, \xC3\x89" "cran mod\xC3\xA8le, Mod\xC3\xA8le d'en-t\xC3\xAAte, Mod\xC3\xA8le de pied de page ou Popup."},
    {"Width",       "INT",    Access::Read,      "Sa largeur, en pixels."},
    {"Height",      "INT",    Access::Read,      "Sa hauteur."},
    {"Background",  "STRING", Access::ReadWrite, "Sa couleur de fond (#20242B) ; l'\xC3\xA9" "crire la change en marche."},
    {"Open",        "BOOL",   Access::Read,      "Elle est affich\xC3\xA9" "e : la vue courante ou une popup ouverte."},
    {"IsCurrent",   "BOOL",   Access::Read,      "C'est la vue courante (sous les popups)."},
    {"IsPopup",     "BOOL",   Access::Read,      "Elle est ouverte en popup."},
    {"X",           "REAL",   Access::ReadWrite, "Ouverte en popup : sa place dans la vue du dessous ; l'\xC3\xA9" "crire la d\xC3\xA9place."},
    {"Y",           "REAL",   Access::ReadWrite, "... et sa place verticale."},
    {"Title",       "STRING", Access::Read,      "Le titre de la popup."},
    {"ObjectCount", "INT",    Access::Read,      "Ses objets."},
    {"OpenCount",   "DINT",   Access::Read,      "Les fois o\xC3\xB9 elle a \xC3\xA9t\xC3\xA9 ouverte depuis le lancement."},
};
[[nodiscard]] inline const InfoVar* objectInfo(std::string_view name) noexcept {
    for (const auto& v : kObjectInfo) if (same(v.name, name)) return &v;
    for (const auto& v : kGifInfo) if (same(v.name, name)) return &v;      // lot 16 (un GIF anime)
    for (const auto& v : kAlarmInfo) if (same(v.name, name)) return &v;    // 1.9 (un objet qui porte des alarmes)
    return nullptr;
}
[[nodiscard]] inline const InfoVar* viewInfo(std::string_view name) noexcept {
    for (const auto& v : kViewInfo) if (same(v.name, name)) return &v;
    return nullptr;
}

// ---- 1.9 : les esclaves simules - une structure SYS.Slave.<nom> par esclave ----------
//  <nom> : hmi::slaveKey(le nom de l'equipement) - "Variateur ATV320" donne
//  SYS.Slave.Variateur_ATV320. Les esclaves : les equipements Modbus TCP qui
//  ont un esclave simule (lie a un vrai appareil, ou seulement simules). Seize
//  membres, en lecture.
inline constexpr InfoVar kSlaveMembers[] = {
    {"Name",          "STRING", Access::Read, "Son nom affich\xC3\xA9."},
    {"Kind",          "STRING", Access::Read, "\xC2\xAB li\xC3\xA9 \xC2\xBB (clone d'un vrai appareil) ou \xC2\xAB seulement simul\xC3\xA9 \xC2\xBB."},
    {"Equipment",     "STRING", Access::Read, "L'\xC3\xA9quipement dont il est l'esclave."},
    {"Running",       "BOOL",   Access::Read, "Il est en marche."},
    {"Responds",      "BOOL",   Access::Read, "Faux : panne simul\xC3\xA9" "e."},
    {"Exception",     "INT",    Access::Read, "L'exception forc\xC3\xA9" "e (0 : aucune)."},
    {"Read",          "BOOL",   Access::Read, "L'IHM le lit \xC3\xA0 la place du vrai appareil (ou il n'y a que lui)."},
    {"Fallback",      "BOOL",   Access::Read, "Lu parce que le vrai ne r\xC3\xA9pond pas (bascule automatique)."},
    {"Mode",          "STRING", Access::Read, "\xC2\xAB vrai \xC2\xBB, \xC2\xAB esclave \xC2\xBB ou \xC2\xAB auto \xC2\xBB."},
    {"RealOnline",    "BOOL",   Access::Read, "Le vrai appareil r\xC3\xA9pond."},
    // Un port local depasse souvent 32767 : un DINT (un INT le tronquerait).
    {"Port",          "DINT",   Access::Read, "Son port sur 127.0.0.1 (0 : arr\xC3\xAAt\xC3\xA9)."},
    {"Requests",      "UDINT",  Access::Read, "Les requ\xC3\xAAtes servies."},
    {"RefusedWrites", "UDINT",  Access::Read, "Les \xC3\xA9" "critures refus\xC3\xA9" "es (cases forc\xC3\xA9" "es)."},
    {"Clients",       "INT",    Access::Read, "Les clients connect\xC3\xA9s (l'IHM, l'outil Modbus...)."},
    {"Animated",      "INT",    Access::Read, "Ses valeurs anim\xC3\xA9" "es."},
    {"Forced",        "INT",    Access::Read, "Ses cases forc\xC3\xA9" "es."},
};
inline constexpr std::size_t kSlaveMemberCount = sizeof(kSlaveMembers) / sizeof(kSlaveMembers[0]);
inline constexpr std::string_view kSlaveRoot = "Slave";    // SYS.Slave.<nom>.<membre>
inline constexpr int kSlaveDomain = 13;                    // "Esclaves simules" dans kSysDomains
// 1.11.23 : la souris et le clavier - SYS.Key.<touche> (BOOL : la touche est tenue ; les
// jetons de hmi/HmiKeys.hpp : A..Z, Digit0..Digit9, F1..F12, Enter, Escape, Space...).
inline constexpr std::string_view kKeyRoot = "Key";
inline constexpr int kInputDomain = 14;                    // "Souris et clavier" dans kSysDomains
[[nodiscard]] inline const InfoVar* slaveMember(std::string_view name) noexcept {
    for (const auto& m : kSlaveMembers) if (same(m.name, name)) return &m;
    return nullptr;
}
// Les equipements qui ont un esclave simule, dans l'ordre du projet.
[[nodiscard]] inline std::vector<const Equipment*> slaveEquipments(const Project& p) {
    std::vector<const Equipment*> out;
    for (const auto& e : p.equipments)
        if (e.modbus() && e.hasTwin()) out.push_back(&e);
    return out;
}
// L'equipement de SYS.Slave.<key> (sans casse) ; nul : pas d'esclave de ce nom.
[[nodiscard]] inline const Equipment* slaveEquipment(const Project& p, std::string_view key) {
    for (const auto* e : slaveEquipments(p))
        if (same(slaveKey(e->name), key)) return e;
    return nullptr;
}
// "Centrale_PM5560, Variateur_ATV320" : les noms possibles (un message d'erreur).
[[nodiscard]] inline std::string slaveKeysText(const Project& p) {
    std::string out;
    for (const auto* e : slaveEquipments(p)) out += (out.empty() ? "" : ", ") + slaveKey(e->name);
    return out;
}

// ---- les proprietes : leur nom public, leur type, leur acces -----------------------
//  Le nom public : la cle, avec sa premiere lettre en majuscule (colorOn ->
//  ColorOn) ; quelques cles courtes ont un nom plus parlant (w -> Width). Les
//  deux s'ecrivent, et quelques synonymes : Visibility pour Visible.
struct PropName { std::string_view key, name; };
inline constexpr PropName kPropNames[] = {
    {"x", "X"}, {"y", "Y"}, {"w", "Width"}, {"h", "Height"}, {"rot", "Rotation"},
};
struct PropAlias { std::string_view alias, key; };
inline constexpr PropAlias kPropAliases[] = {
    {"Visibility", "visible"}, {"Left", "x"}, {"Top", "y"}, {"W", "w"}, {"H", "h"}, {"Rot", "rot"}, {"Angle", "rot"},
    {"Color", "fill"}, {"FillColor", "fill"}, {"Caption", "text"},
};
// Les proprietes qui ne s'ecrivent pas en marche : la securite de l'objet (son
// niveau d'acces, son profil, son autorisation), la variable qu'il represente
// ou qu'il ecrit, et les contenus structures (cases, etats d'image).
inline constexpr std::string_view kReadOnlyKeys[] = {"access", "profile", "auth", "variable", "output", "cells", "states"};
struct PropType { std::string_view key, type; };
inline constexpr PropType kPropTypes[] = {
    {"x", "REAL"}, {"y", "REAL"}, {"w", "REAL"}, {"h", "REAL"}, {"rot", "REAL"}, {"pivotX", "REAL"}, {"pivotY", "REAL"},
    {"opacity", "REAL"}, {"strokeWidth", "REAL"}, {"fontSize", "REAL"}, {"radius", "REAL"}, {"min", "REAL"}, {"max", "REAL"},
    {"step", "REAL"}, {"ymin", "REAL"}, {"ymax", "REAL"}, {"duration", "REAL"}, {"window", "REAL"}, {"deadband", "REAL"},
    {"speed", "REAL"},
    {"visible", "BOOL"}, {"blink", "BOOL"}, {"flipH", "BOOL"}, {"flipV", "BOOL"}, {"wrap", "BOOL"}, {"showValue", "BOOL"},
    {"continuous", "BOOL"}, {"seconds", "BOOL"}, {"showText", "BOOL"}, {"showDate", "BOOL"}, {"leadingZeros", "BOOL"},
    {"legend", "BOOL"}, {"clip", "BOOL"}, {"inverted", "BOOL"}, {"userList", "BOOL"}, {"showUser", "BOOL"},
    {"confirm", "BOOL"}, {"icon", "BOOL"}, {"requireDigit", "BOOL"}, {"validateOnExit", "BOOL"}, {"loop", "BOOL"},
    {"autoplay", "BOOL"}, {"muted", "BOOL"}, {"bold", "BOOL"}, {"italic", "BOOL"},
    {"ticks", "INT"}, {"digits", "INT"}, {"decimals", "INT"}, {"maxVisible", "INT"}, {"holdMs", "INT"}, {"refresh", "INT"},
    {"access", "INT"}, {"rows", "INT"}, {"maxLength", "INT"}, {"minLength", "INT"}, {"margin", "INT"}, {"period", "INT"},
    // lot 12 : la navigation et la structure
    {"page", "INT"}, {"tabPage", "INT"}, {"maxItems", "INT"}, {"collapsed", "BOOL"}, {"pushBelow", "BOOL"},
    {"backForward", "BOOL"}, {"showScrollbar", "BOOL"}, {"showNames", "BOOL"}, {"showCounts", "BOOL"}, {"home", "BOOL"},
    {"scrollX", "REAL"}, {"scrollY", "REAL"}, {"contentWidth", "REAL"}, {"contentHeight", "REAL"}, {"headerHeight", "REAL"},
    {"tabHeight", "REAL"}, {"wheelStep", "REAL"}, {"gap", "REAL"},
};

// Les objets dont la valeur montre une variable, sans propriete "value" a
// eux : le champ de saisie (lot 8), les commandes et les afficheurs (lot 9).
// Leur .Value se lit (la variable, ou le retour d'etat) et s'ecrit (figee).
[[nodiscard]] inline bool linkedValue(Kind k) noexcept {
    switch (k) {
        case Kind::InputField: case Kind::PushButton: case Kind::Switch: case Kind::IlluminatedButton: case Kind::Selector:
        case Kind::Slider: case Kind::Knob: case Kind::ComboBox: case Kind::CheckBox: case Kind::RadioGroup:
        case Kind::List:            // 1.12.2 : la ligne choisie (sa variable, ou son retour d'etat)
        case Kind::NumericDisplay: case Kind::MultiStateIndicator: case Kind::MultiStateText: case Kind::Bargraph:
        case Kind::Thermometer: case Kind::Dial: case Kind::SevenSegment: case Kind::TrendArrow:
            return true;
        default:
            return kindIsSynoptic(k);      // lot 10 : l'etat ou le niveau d'un symbole de synoptique
    }
}
inline constexpr std::string_view kLinkedValueText = "Ce que montre l'objet : sa variable (ou son retour d'\xC3\xA9tat) ; l'\xC3\xA9" "crire la fige.";

[[nodiscard]] inline std::string publicName(std::string_view key) {
    for (const auto& p : kPropNames) if (p.key == key) return std::string(p.name);
    std::string s(key);
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
[[nodiscard]] inline bool keyWritable(std::string_view key) noexcept {
    for (const auto k : kReadOnlyKeys) if (k == key) return false;
    return true;
}
// Le type d'une propriete : connu par sa cle, sinon lu dans sa valeur (TRUE /
// FALSE : BOOL ; un nombre : REAL ; le reste : STRING).
[[nodiscard]] inline std::string keyType(std::string_view key, std::string_view value = {}) {
    for (const auto& p : kPropTypes) if (p.key == key) return std::string(p.type);
    if (same(value, "TRUE") || same(value, "FALSE")) return "BOOL";
    if (!value.empty()) {
        std::string tmp(value);
        char* end = nullptr;
        (void)std::strtod(tmp.c_str(), &end);
        if (end && end != tmp.c_str() && *end == '\0') return "REAL";
    }
    return "STRING";
}
// La cle d'une propriete de l'objet, par son nom public, sa cle ou un synonyme
// (sans casse) ; "" : l'objet n'a pas cette propriete.
[[nodiscard]] inline std::string keyOf(const Object& o, std::string_view name) {
    const auto has = [&](std::string_view key) {
        for (const auto& p : o.props) if (p.key == key) return true;
        return false;
    };
    for (const auto& p : o.props) if (same(p.key, name)) return p.key;
    for (const auto& p : kPropNames) if (same(p.name, name) && has(p.key)) return std::string(p.key);
    for (const auto& a : kPropAliases) if (same(a.alias, name) && has(a.key)) return std::string(a.key);
    if (same(name, "Value") && linkedValue(o.kind)) return "value";
    return {};
}

// Une variable publique, telle que l'arbre et le volet la montrent.
struct Member {
    std::string name;      // "Visible", "Width", "Pressed"
    std::string key;       // la propriete ("visible", "w") ; vide : une information du moteur
    std::string type;
    Access      access{Access::Read};
    std::string text;      // ce qu'elle est
    std::string value;     // dans l'editeur : la valeur statique, ou "= expression"
};
// Les variables d'un objet : ses informations, puis ses proprietes (dans leur
// ordre). `v` : sa vue (le nom de son calque).
[[nodiscard]] inline std::vector<Member> objectMembers(const Object& o, const View* v = nullptr) {
    std::vector<Member> out;
    std::vector<InfoVar> infos(std::begin(kObjectInfo), std::end(kObjectInfo));
    if (o.kind == Kind::AnimatedGif) infos.insert(infos.end(), std::begin(kGifInfo), std::end(kGifInfo));   // lot 16
    for (const auto& i : infos) {
        Member m;
        m.name = std::string(i.name);
        m.type = std::string(i.type);
        m.access = i.access;
        m.text = std::string(i.text);
        if (same(i.name, "Name")) m.value = o.name;
        else if (same(i.name, "Type")) m.value = std::string(kindLabel(o.kind));
        else if (same(i.name, "Id")) m.value = std::to_string(o.id);
        else if (same(i.name, "Layer") && v)
            for (const auto& l : v->layers) if (l.id == o.layer) m.value = l.name;
        out.push_back(std::move(m));
    }
    bool hasValue = false;
    for (const auto& p : o.props) {
        Member m;
        m.name = publicName(p.key);
        m.key = p.key;
        m.type = keyType(p.key, p.value);
        m.access = keyWritable(p.key) ? Access::ReadWrite : Access::Read;
        m.value = p.expr.empty() ? p.value : "= " + p.expr;
        hasValue = hasValue || p.key == "value";
        out.push_back(std::move(m));
    }
    if (!hasValue && linkedValue(o.kind)) {
        Member m;
        m.name = "Value";
        m.key = "value";
        m.type = "ANY";
        m.access = Access::ReadWrite;
        m.text = std::string(kLinkedValueText);
        for (const auto& p : o.props)
            if (p.key == "variable" && !p.value.empty()) m.value = "= " + p.value;
        out.push_back(std::move(m));
    }
    return out;
}
// Les variables d'une vue (ses informations).
[[nodiscard]] inline std::vector<Member> viewMembers(const View& v) {
    std::vector<Member> out;
    for (const auto& i : kViewInfo) {
        Member m;
        m.name = std::string(i.name);
        m.type = std::string(i.type);
        m.access = i.access;
        m.text = std::string(i.text);
        if (same(i.name, "Name")) m.value = v.name;
        else if (same(i.name, "Description")) m.value = v.description;
        else if (same(i.name, "Role")) m.value = std::string(viewRoleLabel(v.role));
        else if (same(i.name, "Width")) m.value = std::to_string(v.width);
        else if (same(i.name, "Height")) m.value = std::to_string(v.height);
        else if (same(i.name, "Background")) m.value = v.background;
        else if (same(i.name, "ObjectCount")) m.value = std::to_string(v.objects.size());
        else if (same(i.name, "Title")) m.value = v.popup.title;
        out.push_back(std::move(m));
    }
    return out;
}

// ================================================================ un chemin ===
//  "SYS.UserName", "Vue_1.Obj.Visible", "Vue_1.Open" : ce qu'il designe. Les
//  objets d'une vue sont les siens et ceux qu'elle emprunte (`borrowed` : la
//  vue telle qu'elle tourne, composee ; nul : ses objets seulement).
struct Resolved {
    enum class What : std::uint8_t {
        None,           // pas un chemin public : sa racine n'est ni SYS ni une vue
        Sys, ViewMember, ObjectMember,
        Incomplete,     // "SYS", "Vue_1", "Vue_1.Obj" : il manque la fin
        Unknown,        // une racine publique, une suite inconnue (error)
    } what{What::None};
    const SysVar*  sys{nullptr};
    const View*    view{nullptr};
    const Object*  object{nullptr};
    const InfoVar* info{nullptr};     // une information (de la vue ou de l'objet)
    std::string    key;               // ObjectMember : la propriete ("" : une information)
    std::string    type;
    Access         access{Access::Read};
    std::string    error;
    // 1.9 : SYS.Slave.<nom>.<membre> - what vaut Sys, `sys` reste NUL : l'esclave
    // (son nom dans SYS.Slave, l'equipement) et le membre.
    std::string    slave;             // "Variateur_ATV320"
    std::string    slaveEquipment;    // "Variateur ATV320"
    const InfoVar* slaveMember{nullptr};
    // 1.10.2 (chantier A) : Vue.Pompe_3.Armoire - un parametre d'une instance de
    // symbole (what vaut ObjectMember, `key` reste vide) : son nom et ce qu'il
    // relie (l'argument de l'instance, sinon la valeur par defaut du symbole).
    std::string    param;             // "Armoire"
    std::string    argument;          // "Armoires[1]"
    // 1.11.1 (decision 108) : Vue.Objet.Alarmes.<alarme>.<membre> - une alarme de
    // l'objet (what vaut ObjectMember, `key` reste vide) : son nom complet dans la
    // liste des alarmes ("Vue.Objet.Defaut"), son nom dans l'objet et le membre.
    std::string    alarm;             // "Vue_1.Pompe_1.Defaut"
    std::string    alarmLocal;        // "Defaut"
    const InfoVar* alarmMember{nullptr};
    // 1.11.23 : SYS.Key.<touche> - what vaut Sys, `sys` reste NUL : le jeton de la touche.
    std::string    keyName;           // "F5", "Enter", "Digit1"
};

// 1.10.2 (chantier A) : LES PARAMETRES D'UNE INSTANCE DE SYMBOLE, lus partout
// comme une variable d'instance : Vue.Pompe_3.Armoire vaut ce qu'elle relie
// (Armoires[1]) ; en lecture seule (on ecrit la variable reliee).
struct InstanceParam {
    std::string name, argument, type, description;
    bool        given{false};         // l'instance donne l'argument (sinon : la valeur par defaut)
};
[[nodiscard]] inline std::vector<InstanceParam> instanceParams(const Project& p, const Object& o) {
    std::vector<InstanceParam> out;
    if (o.kind != Kind::SymbolInstance) return out;
    std::string symbol, given;
    for (const auto& x : o.props) {
        if (x.key == "symbol") symbol = x.value;
        else if (x.key == "params") given = x.value;
    }
    const View* sym = nullptr;
    for (const auto& v : p.views) if (same(v.name, symbol)) sym = &v;
    if (!sym) return out;
    // "Armoire := Armoires[1]; Nom := 'B'" : les ; hors des chaines.
    std::vector<std::pair<std::string, std::string>> args;
    {
        const auto trim = [](std::string t) {
            while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
            std::size_t b = 0;
            while (b < t.size() && std::isspace(static_cast<unsigned char>(t[b]))) ++b;
            return t.substr(b);
        };
        bool quoted = false;
        std::string cur;
        for (std::size_t i = 0; i <= given.size(); ++i) {
            const char c = i < given.size() ? given[i] : ';';
            if (c == '\'') quoted = !quoted;
            if (c == ';' && !quoted) {
                if (const auto at = cur.find(":="); at != std::string::npos) args.emplace_back(trim(cur.substr(0, at)), trim(cur.substr(at + 2)));
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    for (const auto& prm : sym->params) {
        InstanceParam ip;
        ip.name = prm.name;
        ip.type = prm.type.empty() ? std::string("ANY") : prm.type;
        ip.description = prm.description;
        ip.argument = prm.defaultValue;
        for (const auto& [n, a] : args)
            if (same(n, prm.name) && !a.empty()) { ip.argument = a; ip.given = true; }
        out.push_back(std::move(ip));
    }
    return out;
}

[[nodiscard]] inline const View* viewNamed(const Project& p, std::string_view name) noexcept {
    for (const auto& v : p.views) if (same(v.name, name)) return &v;
    return nullptr;
}

// 1.10.2 (chantier A) : "veux-tu dire ... ?" - le nom le plus proche d'un nom
// inconnu (sans casse) : une ou deux lettres de travers, ou le debut d'un nom.
// Vide : rien d'assez proche.
[[nodiscard]] inline std::size_t nameDistance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            const bool eq = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1]));
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (eq ? 0u : 1u)});
            diag = up;
        }
    }
    return row[b.size()];
}
[[nodiscard]] inline std::string nearestName(const std::vector<std::string_view>& names, std::string_view name) {
    std::string best;
    std::size_t bestD = std::max<std::size_t>(1, std::min<std::size_t>(3, name.size() / 3)) + 1;
    for (const auto n : names) {
        if (n.empty() || same(n, name)) continue;
        std::size_t d = nameDistance(n, name);
        // Le debut d'un nom (V_10 -> V_101) compte comme une lettre de travers.
        if (n.size() > name.size() && name.size() >= 2 && same(n.substr(0, name.size()), name)) d = std::min<std::size_t>(d, 1);
        if (d < bestD) { bestD = d; best = std::string(n); }
    }
    return best;
}
[[nodiscard]] inline std::string didYouMean(const std::vector<std::string_view>& names, std::string_view name) {
    const std::string n = nearestName(names, name);
    return n.empty() ? std::string{} : " : veux-tu dire " + n + " ?";
}
// Coupe "a.b.c" en segments ; faux si un segment est vide ou porte un index.
[[nodiscard]] inline bool splitPath(std::string_view path, std::vector<std::string_view>& parts) {
    parts.clear();
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '.') {
            if (i == start) return false;
            parts.push_back(path.substr(start, i - start));
            start = i + 1;
        } else if (path[i] == '[' || path[i] == ']' || std::isspace(static_cast<unsigned char>(path[i]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline Resolved resolve(const Project& p, std::string_view path, const View* borrowed = nullptr) {
    Resolved r;
    std::vector<std::string_view> parts;
    if (!splitPath(path, parts) || parts.empty()) {
        // Une racine suivie d'un index (Vue_1[0]...) : pas un chemin public.
        return r;
    }
    if (isSysRoot(parts[0])) {
        if (parts.size() == 1) { r.what = Resolved::What::Incomplete; return r; }
        // 1.9 : SYS.Slave.<nom>.<membre> - les esclaves du projet, seize membres.
        if (same(parts[1], kSlaveRoot) && !sysVar(parts[1])) {
            if (parts.size() == 2) { r.what = Resolved::What::Incomplete; return r; }
            const Equipment* e = slaveEquipment(p, parts[2]);
            if (!e) {
                const std::string known = slaveKeysText(p);
                r.what = Resolved::What::Unknown;
                r.error = "esclave simul\xC3\xA9 inconnu : SYS.Slave." + std::string(parts[2])
                        + (known.empty() ? std::string(" (le projet n'a pas d'esclave simul\xC3\xA9)") : " (les esclaves : " + known + ")");
                return r;
            }
            r.slave = slaveKey(e->name);
            r.slaveEquipment = e->name;
            if (parts.size() == 3) { r.what = Resolved::What::Incomplete; return r; }
            r.slaveMember = parts.size() == 4 ? slaveMember(parts[3]) : nullptr;
            if (!r.slaveMember) {
                std::string names;
                for (const auto& m : kSlaveMembers) names += (names.empty() ? "" : ", ") + std::string(m.name);
                r.what = Resolved::What::Unknown;
                r.error = parts.size() == 4 ? "SYS.Slave." + r.slave + " n'a pas de membre " + std::string(parts[3]) + " (" + names + ")"
                                            : std::string(path) + " : une structure d'esclave s'\xC3\xA9" "crit SYS.Slave.<nom>.<membre>";
                return r;
            }
            r.what = Resolved::What::Sys;
            r.type = std::string(r.slaveMember->type);
            r.access = Access::Read;
            return r;
        }
        // 1.11.23 : SYS.Key.<touche> - la touche est tenue (BOOL, en lecture).
        if (same(parts[1], kKeyRoot) && !sysVar(parts[1])) {
            if (parts.size() == 2) { r.what = Resolved::What::Incomplete; return r; }
            const auto token = parts.size() == 3 ? keys::tokenOf(parts[2]) : std::string_view{};
            // un identifiant seulement : SYS.Key.1 se lirait comme un bit (Digit1)
            const bool ident = parts.size() == 3 && !parts[2].empty() && !std::isdigit(static_cast<unsigned char>(parts[2].front()));
            if (token.empty() || !ident) {
                r.what = Resolved::What::Unknown;
                r.error = parts.size() == 3 ? "touche inconnue : SYS.Key." + std::string(parts[2])
                                                  + " (A \xC3\xA0 Z, Digit0 \xC3\xA0 Digit9, F1 \xC3\xA0 F12, Enter, Escape, Space, Tab, "
                                                    "Backspace, Delete, Insert, Home, End, PageUp, PageDown, Up, Down, Left, Right)"
                                            : std::string(path) + " : une touche s'\xC3\xA9" "crit SYS.Key.<touche>";
                return r;
            }
            r.what = Resolved::What::Sys;
            r.keyName = std::string(token);
            r.type = "BOOL";
            r.access = Access::Read;
            return r;
        }
        r.sys = parts.size() == 2 ? sysVar(parts[1]) : nullptr;
        if (!r.sys) {
            r.what = Resolved::What::Unknown;
            r.error = "variable syst\xC3\xA8me inconnue : SYS." + std::string(parts[1]);
            return r;
        }
        r.what = Resolved::What::Sys;
        r.type = std::string(r.sys->type);
        r.access = sysAccess(*r.sys);
        return r;
    }
    const View* v = viewNamed(p, parts[0]);
    if (!v) return r;
    r.view = v;
    if (parts.size() == 1) { r.what = Resolved::What::Incomplete; return r; }
    const View& objects = borrowed ? *borrowed : *v;
    const Object* o = nullptr;
    for (const auto& x : objects.objects) if (same(x.name, parts[1])) { o = &x; break; }
    if (parts.size() == 2) {
        if (const auto* i = viewInfo(parts[1])) {
            r.what = Resolved::What::ViewMember;
            r.info = i;
            r.type = std::string(i->type);
            r.access = i->access;
            return r;
        }
        if (o) { r.what = Resolved::What::Incomplete; r.object = o; return r; }
        std::vector<std::string_view> names;      // 1.10.2 : ses objets et ses variables
        for (const auto& x : objects.objects) names.push_back(x.name);
        for (const auto& i : kViewInfo) names.push_back(i.name);
        r.what = Resolved::What::Unknown;
        r.error = "ni objet ni variable de la vue " + v->name + " : " + std::string(parts[1]) + didYouMean(names, parts[1]);
        return r;
    }
    if (!o) {
        std::vector<std::string_view> names;      // 1.10.2 : ses objets
        for (const auto& x : objects.objects) names.push_back(x.name);
        r.what = Resolved::What::Unknown;
        r.error = "objet inconnu dans la vue " + v->name + " : " + std::string(parts[1]) + didYouMean(names, parts[1]);
        return r;
    }
    r.object = o;
    // 1.11.1 (decision 108) : Vue.Objet.Alarmes.<alarme>.<membre>.
    if (isAlarmsRoot(parts[2]) && !hasAlarmGroup(*o)) {
        r.what = Resolved::What::Unknown;
        r.error = o->name + " (" + std::string(kindLabel(o->kind)) + ") ne porte pas d'alarmes : "
                + "seuls les objets du synoptique et les instances de symboles en ont";
        return r;
    }
    if (isAlarmsRoot(parts[2]) && keyOf(*o, parts[2]).empty()) {
        if (parts.size() < 5) { r.what = Resolved::What::Incomplete; return r; }
        std::string local;
        for (std::size_t k = 3; k + 1 < parts.size(); ++k) local += (local.empty() ? "" : ".") + std::string(parts[k]);
        const View& home = *v;      // les alarmes sont generees depuis la vue nommee
        const auto alarms = objectAlarmNames(p, home, *o);
        const ObjectAlarmName* hit = nullptr;
        for (const auto& a : alarms) if (same(a.local, local)) { hit = &a; break; }
        if (!hit) {
            std::vector<std::string_view> names;
            for (const auto& a : alarms) names.push_back(a.local);
            r.what = Resolved::What::Unknown;
            r.error = alarms.empty() ? o->name + " n'a pas d'alarme (aucune n'est r\xC3\xA9gl\xC3\xA9" "e sur lui)"
                                     : o->name + " n'a pas d'alarme " + local + didYouMean(names, local);
            return r;
        }
        r.alarm = hit->full;
        r.alarmLocal = hit->local;
        r.alarmMember = alarmMember(parts.back());
        if (!r.alarmMember) {
            std::string names;
            for (const auto& m : kAlarmMembers) names += (names.empty() ? "" : ", ") + std::string(m.name);
            r.what = Resolved::What::Unknown;
            r.error = "l'alarme " + hit->full + " n'a pas de membre " + std::string(parts.back()) + " (" + names + ")";
            return r;
        }
        r.what = Resolved::What::ObjectMember;      // `info` reste nul : ce n'est pas l'objet qui renseigne
        r.type = std::string(r.alarmMember->type);
        r.access = r.alarmMember->access;
        return r;
    }
    if (parts.size() > 3) {
        r.what = Resolved::What::Unknown;
        r.error = std::string(path) + " : une variable d'instance s'\xC3\xA9" "crit Vue.Objet.Propri\xC3\xA9t\xC3\xA9";
        return r;
    }
    const std::string key = keyOf(*o, parts[2]);
    if (!key.empty()) {
        r.what = Resolved::What::ObjectMember;
        r.key = key;
        const Prop* prop = nullptr;
        for (const auto& x : o->props) if (x.key == key) prop = &x;
        r.type = prop ? keyType(key, prop->value) : std::string("ANY");
        r.access = keyWritable(key) ? Access::ReadWrite : Access::Read;
        return r;
    }
    if (const auto* i = objectInfo(parts[2])) {
        r.what = Resolved::What::ObjectMember;
        r.info = i;
        r.type = std::string(i->type);
        r.access = i->access;
        return r;
    }
    const auto params = instanceParams(p, *o);        // 1.10.2 : Vue.Pompe_3.Armoire
    for (const auto& ip : params)
        if (same(ip.name, parts[2])) {
            r.what = Resolved::What::ObjectMember;
            r.param = ip.name;
            r.argument = ip.argument;
            r.type = ip.type;
            r.access = Access::Read;
            return r;
        }
    std::vector<std::string> owned;
    for (const auto& ip : params) owned.push_back(ip.name);               // 1.10.2 : ses proprietes et ses informations
    for (const auto& x : o->props) owned.push_back(publicName(x.key));
    std::vector<std::string_view> names(owned.begin(), owned.end());
    for (const auto& i : kObjectInfo) names.push_back(i.name);
    r.what = Resolved::What::Unknown;
    r.error = o->name + " (" + std::string(kindLabel(o->kind)) + ") n'a pas de propri\xC3\xA9t\xC3\xA9 " + std::string(parts[2]) + didYouMean(names, parts[2]);
    return r;
}

// Le chemin montre d'une variable d'instance : Vue.Objet.Membre.
[[nodiscard]] inline std::string instancePath(const View& v, const Object& o, std::string_view member) {
    return v.name + "." + o.name + "." + std::string(member);
}

} // namespace hmi::pub
