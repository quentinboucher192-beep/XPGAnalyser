// =============================================================================
//  app/Licence.hpp - 1.30.0 : LES LICENCES DANS L'APPLICATION
// -----------------------------------------------------------------------------
//  Le module commun (src/licensing, docs/LICENCES.md) dit quelles fonctionnalites
//  sont ouvertes ; ici, ce que l'application en fait :
//
//    start()   au lancement : le dossier des licences de ce PC, le registre livre
//              avec le programme (<programme>/licences/registre.xpgreg) et, 1.32.0,
//              la liste de revocation livree (<programme>/licences/revocations.xpgrev) ;
//    tick()    une fois par heure, l'etat est revalue (une licence prend fin
//              pendant que le logiciel tourne : rien ne se ferme, seules les
//              ENTREES des fonctionnalites se verrouillent) ;
//    gate()    UN SEUL CONTROLE, aux points d'entree des fonctionnalites (la table
//              gates() : une action, un volet, un geste -> l'identifiant de sa
//              fonctionnalite). Ouverte : vrai. Fermee : la fenetre << Pas dans ta
//              licence >> (la phrase du module, et << Licence... >>), faux.
//
//    require() 1.32.0 (H3) : LE MEME CONTROLE AUX POINTS DE PASSAGE, la ou la
//              fonctionnalite se fait quel que soit le geste qui l'a demandee (la
//              simulation de l'IHM qui demarre, la validation d'un import de vues,
//              les commandes de script). Sans App : ouverte -> vrai ; fermee ->
//              faux et la phrase du module ; c'est a l'appelant de la dire.
//
//  JAMAIS DE PERTE DE TRAVAIL : Ouvrir, Enregistrer, Enregistrer sous, Exporter
//  le projet, Fermer, Quitter ne sont dans aucune entree de gates() - un essai le
//  verifie. Le poste d'exploitation (--ihm) n'est jamais arrete par la licence du
//  PC : un atelier ne s'arrete pas pour une licence.
//
//  SANS CLE D'EDITEUR dans le programme (le module : << Non activees >>), et
//  avant start() (les essais, les outils) : tout est ouvert.
// =============================================================================
#pragma once

#include "../licensing/Store.hpp"

#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app {

class App;

namespace licence {

// Ou une fonctionnalite se demande.
enum class Where : unsigned char { Action, HmiPane, ApiPane, Gesture };

struct Gate {
    Where       where;
    const char* key;        // l'action ("ia.assistant"), le volet ("scripts"), le geste ("sim.run")
    const char* feature;    // l'identifiant de la fonctionnalite ("ai_assistant")
    const char* label;      // ce que voit l'utilisateur (UTF-8) : "Assistant IA"
};
// Tous les points d'entree verrouillables (l'ecran Licence les montre, les essais les verifient).
[[nodiscard]] const std::vector<Gate>& gates();
// La fonctionnalite d'un point d'entree ; vide : aucun verrou.
[[nodiscard]] std::string_view featureOf(Where where, std::string_view key);

// Au lancement (App) : configure le module. XPG_LICENCES_DOSSIER (environnement) remplace le dossier des
// licences de ce PC - 1.32.0 : SEULEMENT dans une construction d'essai (licensing::testFolderOverride, option
// CMake XPG_LICENCES_CLE_ESSAI : les captures) ; un programme livre l'ignore. 1.35.0 : `takeFloatingSeat` (pas le
// poste d'exploitation) : une licence flottante prend son poste avant la premiere evaluation (service::startupLease).
void start(bool takeFloatingSeat = false);
// Les essais : configurer avec ces options (un dossier, des cles de confiance).
void start(licensing::StoreOptions options);
// Chaque image : une revaluation par heure.
void tick();
// Revaluer tout de suite (apres un import).
void refresh();
// L'application a configure le module (start).
[[nodiscard]] bool active() noexcept;
// Ouverte (ou rien de configure, ou pas de cle d'editeur : tout est ouvert).
[[nodiscard]] bool allows(std::string_view feature);
// Le controle d'un point d'entree : ouverte -> vrai ; fermee -> la fenetre << Pas dans ta licence >>, faux.
bool gate(App& app, std::string_view feature);
bool gate(App& app, Where where, std::string_view key);

// 1.32.0 (H3) : le controle aux points de passage (voir l'en-tete). EN LIGNE DANS CET EN-TETE : les fichiers qui
// l'appellent (app/hmi/HmiSimulation.cpp, HmiImportDialog.cpp) se compilent aussi dans des essais sans Licence.cpp
// ni les licences (hmi_editor_test) ; start() branche le vrai controle. Avant start() : ouverte, comme allows().
namespace detail {
using RequireFn = bool (*)(std::string_view feature, std::string* why);
inline std::atomic<RequireFn> g_require{nullptr};
} // namespace detail
[[nodiscard]] inline bool require(std::string_view feature, std::string* why = nullptr) {
    const detail::RequireFn f = detail::g_require.load(std::memory_order_acquire);
    return f == nullptr || f(feature, why);
}
// La phrase d'une entree verrouillee (le module), ou vide si elle est ouverte.
[[nodiscard]] std::string lockedText(std::string_view feature);
// 1.35.1 : LE LOGICIEL NE S'UTILISE PAS SANS LICENCE (la demande de l'editeur). Ouvrir, creer, importer un projet :
// seulement avec une licence valable (Valide, Grace) ou la Decouverte en cours ; le poste d'exploitation aussi (--ihm :
// il ne demarre pas). Tout ouvert : les licences pas actives (aucune cle de l'editeur compilee). Un projet deja ouvert
// reste ouvert et s'enregistre (un poste qui tourne continue) : rien n'est perdu. `why` : la raison, pour une phrase.
[[nodiscard]] bool projectsAllowed(std::string* why = nullptr);
// 1.35.1 : la phrase de l'etat telle que l'ecran la montre. Le module dit << l'edition FREE s'applique >> et << ton projet
// reste ouvert >> ; dans les Studios, sans licence valable, aucun projet ne s'ouvre : la phrase le dit.
[[nodiscard]] std::string stateText(const licensing::LicenseState& st);
// Le libelle d'une fonctionnalite (le registre ; sinon son identifiant).
[[nodiscard]] std::string featureLabel(std::string_view feature);

// 1.35.0 : LA LICENCE ACTIVE, TOUJOURS VISIBLE - la pastille de la barre d'etat (badge) et l'indicateur du bandeau
// haut (indicator, la meme phrase ; vide quand les licences ne sont pas actives) :
//   "Licence : PROFESSIONAL \xC2\xB7 jusqu'au 07/11/2027", "D\xC3\xA9" "couverte \xC2\xB7 23 j",
//   "Bail : 5 j \xE2\x80\x94 connecte-toi", "Flottante \xC2\xB7 poste pris", "Sans licence"...
// Un clic : la fenetre Licence.
struct Badge {
    std::string text;
    std::string shortText;   // la forme courte (un bandeau serre) : "PROFESSIONAL", "D\xC3\xA9" "couverte \xC2\xB7 23 j"
    std::string tip;
    int         tone{0};     // 0 neutre, 1 bon, 2 attention, 3 erreur, 4 information (la Decouverte)
};
[[nodiscard]] Badge badge();
[[nodiscard]] Badge indicator();

// 1.35.0 : l'encart de la licence sur l'accueil (le menu principal) - la licence active, la Decouverte et ses jours,
// le bail qui finit, sans licence -> << Activer avec un code... >> ; 1.35.1 : licences pas actives (aucune cle de
// l'editeur compilee) -> le dit aussi. Un titre vide : pas d'encart (le poste d'exploitation).
struct Card {
    std::string title, text;
    int         tone{0};          // comme Badge::tone
    std::string button;           // "Licence\xE2\x80\xA6", "Activer avec un code\xE2\x80\xA6"
    int         tab{0};           // l'onglet de la fenetre Licence qu'ouvre le bouton
};
[[nodiscard]] Card startCard();

// Les onglets de la fenetre Licence (1.35.0 : Activation en tete ; -1 : le plus utile - Ma licence pour une licence
// par fichier valable, sinon Activation).
inline constexpr int kTabAuto = -1, kTabActivation = 0, kTabMine = 1, kTabDevices = 2, kTabFeatures = 3, kTabEditions = 4, kTabHistory = 5,
                     kTabSettings = 6, kTabRequest = 7, kTabProfile = 8;   // 1.37.0 : Mon profil

// ---- 1.34.0 / 1.35.0 : LES AVIS DE LA LICENCE ET LE SERVICE DES LICENCES (app/LicenceService) ----
//  App, apres start(), SAUF le poste d'exploitation (--ihm : personne devant l'ecran) : startNotices(true).
//    - la Decouverte : a CHAQUE lancement, tant qu'elle court, un avis de la cloche (bouton Licence...) et la
//      phrase launchNotice() sur l'accueil ; revalue a chaque refresh (les jours qui restent) ;
//    - 1.35.0 : le bail d'une licence en ligne qui finit dans 7 jours (ou fini) : un avis, tenu a jour ;
//    - `online` : le service des licences (si ce programme en a un) - le fil de fond (heure, renouvellement,
//      Decouverte, code de l'installateur, jeton flottant, demande manuelle) ; ce qu'il apporte devient un avis
//      de la cloche et un message de la barre d'etat (takeMessages).
//  Jamais une fenetre qui bloque le demarrage.
void startNotices(bool online);
// 1.35.1 : le poste d'exploitation (--ihm) : le service seul (le bail, l'heure, le jeton flottant), sans avis.
void startService();
// 1.35.1 : les avis (et l'encart de l'accueil) sans relancer le service : le poste d'exploitation refuse faute de
// licence revient a l'accueil, quelqu'un est devant l'ecran - l'encart dit << Sans licence >>. Sans effet s'ils sont deja la.
void showNotices();
// "D\xC3\xA9" "couverte : il te reste 12 jours (jusqu'au 19/11/2026)." ; vide : pas en Decouverte.
[[nodiscard]] std::string discoveryText(const licensing::LicenseState& st);
// "Bail : il reste 5 jours \xE2\x80\x94 connecte ce PC \xC3\xA0 Internet avant le 12/11/2026." ; vide : rien a dire.
[[nodiscard]] std::string leaseText(const licensing::LicenseState& st);
// La phrase de la licence pour l'accueil (la Decouverte, le bail ; vide : rien a dire, ou startNotices pas appele).
[[nodiscard]] std::string launchNotice();
// Un avis de la cloche de la licence (bouton << Licence... >> : la fenetre Licence) ; tone : "ok", "info",
// "warning", "error".
void postNotice(const std::string& key, const std::string& title, const std::string& detail, const std::string& tone);
// Les messages pour la barre d'etat (App les publie : StatusNotice), sures entre fils.
void queueMessage(std::string text);
[[nodiscard]] std::vector<std::string> takeMessages();
// A la fermeture (App) : le fil du service s'arrete (la requete en cours est coupee, le jeton flottant rendu).
void shutdown();

// "07/11/2027" (une date de licence, UTC) ; vide : 0.
[[nodiscard]] std::string dayOf(licensing::Seconds at);

// ---- 1.36.0 : LA DEMANDE EN LIGNE, LE CHEMIN PRINCIPAL ----
// Ce programme a-t-il un service des licences (la demande en ligne) ? Faux : la demande et la licence par fichier.
[[nodiscard]] bool onlineRequests();
// La demande de ce PC qui attend l'editeur (le releve du service, relu quand il change, au plus une fois par seconde).
struct PendingRequest {
    bool               pending{false};
    std::uint64_t      number{0};
    licensing::Seconds sentAt{0};
    std::string        state;     // service::stateText : "Envoy\xC3\xA9" "e \xE2\x80\x94 en attente de l'\xC3\xA9" "diteur"...
};
[[nodiscard]] PendingRequest pendingRequest();
// LA REPONSE DE L'EDITEUR a la demande de ce PC (le fil du service la pose, l'application la prend chaque image et
// la montre : showAnswer). Seulement avec les avis (quelqu'un devant l'ecran ; pas le poste d'exploitation qui tourne).
struct Answer {
    std::string kind;        // service::kAnswerAccepted, kAnswerUpdated, kAnswerRefused
    std::string licenseId;
    std::string note;        // le mot de l'editeur, ou la raison du refus
    // 1.37.0 : une licence EN LIGNE activee avec le code recu ; le code (en memoire seulement) quand le mail de l'editeur
    // n'est pas parti (mail : gateway::kMailFailed, vers mailTo) : la fenetre le montre et demande de verifier l'adresse.
    bool        online{false};
    std::string code;
    std::string mail, mailTo;
};
void queueAnswer(Answer a);
[[nodiscard]] std::optional<Answer> takeAnswer();
// Ce que dit la fenetre : acceptee - l'edition, le client, jusqu'a quand (et les jours), les PC, ce qu'elle ouvre (par
// logiciel), le mot de l'editeur ; refusee - la raison, et une nouvelle demande. `st` : l'etat apres l'installation.
struct AnswerView {
    std::string title, text;
    std::string confirm;     // "Voir ma licence", "Nouvelle demande\xE2\x80\xA6"
    int         tab{kTabMine};
    bool        accepted{true};
};
[[nodiscard]] AnswerView answerView(const Answer& a, const licensing::LicenseState& st);

} // namespace licence

// La fenetre Licence (le License Manager du client) ; `tab` : licence::kTab* (1.35.0 : 0 Activation, 1 Ma licence,
// 2 Mes appareils, 3 Fonctionnalites, 4 Editions, 5 Historique, 6 Parametres, 7 Demande a l'editeur ; -1 : le plus utile).
void showLicenceWindow(App& app, int tab = licence::kTabAuto);
namespace licence {
// 1.35.1 : le controle de projectsAllowed, avec la fenetre << Licence necessaire >> quand c'est ferme (son bouton : la
// fenetre Licence, onglet Activation). Vrai : on peut continuer.
bool gateProjects(App& app);
// 1.36.0 : la fenetre de la reponse de l'editeur (answerView) ; son bouton : la fenetre Licence (Ma licence, ou une
// nouvelle demande).
void showAnswer(App& app, const Answer& a);
} // namespace licence

} // namespace app
