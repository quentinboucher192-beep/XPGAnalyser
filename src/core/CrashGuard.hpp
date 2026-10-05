// =============================================================================
//  core/CrashGuard.hpp - 1.10.2 : le rapport de plantage et de blocage
// -----------------------------------------------------------------------------
//  Le client (02/10, 12 h 41) : « quand ca crash, ou plante sans raison, on a
//  acces a cette ressource [l'historique interne] ; ca ouvre quand meme un petit
//  modal avec crash occured ou quelque chose dans le genre ».
//
//  LE PLANTAGE. install() pose les gestionnaires :
//    * Windows : SetUnhandledExceptionFilter, std::set_terminate, SIGABRT, l'appel
//      d'une fonction virtuelle pure et le parametre invalide de la CRT ;
//    * Linux (les essais) : SIGSEGV, SIGABRT, SIGFPE, SIGILL et SIGBUS, sur une
//      pile a part (un debordement de pile se rapporte aussi).
//  Le gestionnaire ECRIT D'ABORD le rapport (crashs/plantage-<date>.txt : la
//  version, le systeme, l'heure, le code et l'adresse, la pile d'appels, les
//  portees ouvertes, les 2 000 dernieres entrees de l'historique, les documents
//  ouverts ; sous Windows un minidump a cote), PUIS montre la petite fenetre
//  native « XPGAnalyser a rencontre un probleme et doit se fermer ». Rien n'y
//  alloue : un tampon fixe et des appels du systeme.
//
//  LE BLOCAGE. La boucle principale appelle heartbeat() a chaque image ; le
//  chien de garde (un fil) regarde quatre fois par seconde. Sans battement
//  depuis 8 s : un rapport « blocage » (la pile du fil principal - Windows : le
//  fil est suspendu le temps de lire son contexte ; Linux : un signal lui fait
//  lire sa propre pile), puis une fenetre native « ne repond plus » avec
//  Attendre et Fermer. Si la boucle repart : takeRecovery() le dit a l'appli,
//  qui l'ecrit dans son journal.
//
//  AU LANCEMENT SUIVANT : takeNewReport() rend le rapport le plus recent qui n'a
//  pas encore ete montre (l'appli ouvre « Un plantage s'est produit le ... »).
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace core::crash {

struct Options {
    std::string folder;               // le dossier des rapports (UTF-8), cree s'il manque
    bool dialogs = true;              // les fenetres natives (pas dans les essais sans ecran)
};

// A appeler depuis le fil principal, le plus tot possible (main).
void install(const Options& options);
bool installed() noexcept;
std::string folder();

// Les documents ouverts, un nom par ligne (les noms seulement). L'appli les donne
// quand ils changent ; le gestionnaire les lit sans verrou.
void setOpenDocuments(std::string_view names) noexcept;

// La fenetre de blocage. Par defaut : MessageBoxW (Oui = attendre, Non = fermer)
// sous Windows, rien ailleurs. L'appli peut donner la sienne (SDL_ShowMessageBox,
// avec les boutons « Attendre » et « Fermer XPGAnalyser ») ; elle est appelee
// depuis le fil du chien de garde et rend vrai pour « Fermer ».
using HangDialog = bool (*)(const char* title, const char* messageUtf8);
void setHangDialog(HangDialog dialog) noexcept;

// La boite du plantage. Par defaut : MessageBoxW sous Windows (si `dialogs`), rien
// ailleurs. Un essai peut donner la sienne (crashtrail_test : une boite qui reste
// ouverte) ; elle est appelee depuis le gestionnaire, avec le chemin du rapport.
// Le chien de garde se tait des le debut d'un plantage, et tant qu'une boite de
// plantage ou de blocage est ouverte (1.10.2).
using CrashBox = void (*)(const char* reportUtf8);
void setCrashBox(CrashBox box) noexcept;

// ---- le chien de garde --------------------------------------------------------------
void heartbeat() noexcept;                                  // a chaque image
void startWatchdog(unsigned seconds = 8, bool dialog = true);
void stopWatchdog();
// Une attente longue et voulue (un dialogue natif modal) : le chien de garde ne compte
// pas pendant. Les appels s'emboitent.
void holdWatchdog(bool hold) noexcept;
// Vrai une fois apres une reprise : `seconds` dit combien de temps la boucle s'est arretee.
bool takeRecovery(double& seconds) noexcept;
// Le dernier rapport de blocage ecrit pendant cette session (vide sinon).
std::string lastHangReport();

// ---- les rapports -------------------------------------------------------------------
enum class ReportKind { Crash, Hang, Manual };
// Ecrire un rapport tout de suite, sans plantage (les essais, « Exporter » du journal
// interne). Rend le chemin (vide en cas d'echec).
std::string writeReportNow(ReportKind kind, const char* reason);

// Le rapport le plus recent pas encore montre, et le marquer comme vu.
struct PendingReport {
    std::string path;      // le chemin complet (UTF-8)
    std::string when;      // « 02/10/2026 a 13:05 »
    std::string kind;      // « plantage » ou « blocage »
    std::string text;      // le rapport entier
};
std::optional<PendingReport> takeNewReport();

// ---- les essais caches (--essai-plantage, --essai-blocage) ----------------------------
enum class Test { None, Crash, Hang };
// Arme un essai : heartbeat() le declenche apres `afterMs` (le temps de voir l'appli).
void armTest(Test test, unsigned afterMs = 1500) noexcept;

} // namespace core::crash
