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
#include "LicenceShop.hpp"   // 1.46.0 : la boutique (le catalogue, les commandes)
#include "Notices.hpp"   // 1.38.0 : les notifications generales (la licence, les offres, les annonces)

#include <atomic>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

namespace ui {
class Widget;   // 1.44.0 : captureSpotCenter
}
namespace menu {
class MenuManager;   // 1.45.0 : la fenetre Licence sur une pile de menus (les essais : sans App)
}

namespace licensing {
struct RegistryView;
}

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
// 1.39.0 : la fenetre << Ta licence ne permet pas cette action >> (ce que l'action demande, pourquoi, la capture de
// la fonctionnalite, les offres qui la contiennent ; Voir les offres... : la boutique). `actionLabel` vide : le nom de la
// fonctionnalite. Ecrite dans LicenceWindow.cpp.
void showLocked(App& app, std::string_view feature, std::string actionLabel = {});
// 1.39.0 : la fenetre << Offre modifiee : avant / apres >> de la notification `key` (son bouton Voir le detail) ; faux :
// pas de notification, ou pas une offre modifiee. Ecrite dans LicenceWindow.cpp.
bool showOfferChange(App& app, const std::string& key);

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
// 1.39.0 : UNE ENTREE VERROUILLEE VUE DANS UN MENU OU UNE LISTE - son libelle barre, un cadenas ; le clic va toujours
// au controle (gate : la fenetre). EN LIGNE, comme require() : les menus et les listes (la barre du haut, l'arbre du
// projet, Ctrl+K) se compilent aussi dans des essais sans Licence.cpp. Avant start() : rien de verrouille.
namespace detail {
using LockedFn = bool (*)(Where where, std::string_view key);
inline std::atomic<LockedFn> g_locked{nullptr};
} // namespace detail
[[nodiscard]] inline bool locked(Where where, std::string_view key) {
    const detail::LockedFn f = detail::g_locked.load(std::memory_order_acquire);
    return f != nullptr && !key.empty() && f(where, key);
}
// Une action d'un menu (son identifiant : "project.exportSources", "sim.run"...) : une action ou un geste verrouille.
[[nodiscard]] inline bool lockedAction(std::string_view action) { return locked(Where::Action, action) || locked(Where::Gesture, action); }
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
// 1.46.0 : PLUS D'ONGLETS - un accueil (kTabHome : le resume de la licence, les rubriques en quatre groupes) et, sur chaque
// rubrique, un fil d'Ariane (<< Licence > Demander ma licence >>). Les numeros restent ceux des rubriques (les notifications,
// les scripts, Regarder) ; kTabAuto : l'accueil, sauf la ou une rubrique est plus utile (sans licence : la demande, ou
// l'activation ; un bail fini, un poste flottant a prendre : l'activation). kTabProfile : MON PROFIL N'EST PLUS UNE
// RUBRIQUE - sa petite fenetre a part (showProfileWindow ; l'accueil du Studio, Aide > Mon profil..., la demande).
inline constexpr int kTabHome = -2;
inline constexpr int kTabAuto = -1, kTabActivation = 0, kTabMine = 1, kTabDevices = 2, kTabFeatures = 3, kTabEditions = 4, kTabHistory = 5,
                     kTabSettings = 6, kTabRequest = 7, kTabProfile = 8,   // 1.37.0 : Mon profil
                     kTabOptions = 9, kTabPurchases = 10,                     // 1.42.0 : Options, Mes achats
                     kTabCart = 11;                                           // 1.48.0 : le Panier (la boutique)

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
// 1.44.0 : les avis sont-ils montres (startNotices : quelqu'un devant l'ecran ; faux : le poste d'exploitation) ?
[[nodiscard]] bool noticesShown() noexcept;

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
    std::string option;      // 1.43.0 : une demande d'option acceptee (son nom) ; vide : une licence
    std::uint64_t order{0};  // 1.46.0 : une commande livree (son numero) ; 0 : une demande
};
// 1.45.0 : LA MEME REPONSE UNE FOIS - une reponse deja dans la file (meme genre, meme licence, meme option, meme mot) n'y
// entre pas une seconde fois ; et UNE FENETRE A LA FOIS : tant qu'une fenetre de reponse est ouverte (setAnswerOpen),
// takeAnswer ne rend rien - la suivante vient a sa fermeture, jamais deux fenetres empilees a fermer l'une apres l'autre.
void queueAnswer(Answer a);
[[nodiscard]] std::optional<Answer> takeAnswer();
void setAnswerOpen(bool open);
[[nodiscard]] bool answerOpen() noexcept;
// 1.39.0 : LA LICENCE PERDUE EN DIRECT - retiree de ce PC, revoquee ou desactivee par l'editeur (le service le dit au
// regard des 2 minutes ou au lancement ; une liste de revocation adoptee) : une fenetre tout de suite (showLoss) et une
// notification. Une fois par licence et par lancement ; seulement avec les avis (pas le poste d'exploitation).
struct Loss {
    std::string licenseId, text;
};
void queueLoss(std::string licenseId, std::string text);
[[nodiscard]] std::optional<Loss> takeLoss();
// Ce que dit la fenetre : acceptee - l'edition, le client, jusqu'a quand (et les jours), les PC, ce qu'elle ouvre (par
// logiciel), le mot de l'editeur ; refusee - la raison, et une nouvelle demande. `st` : l'etat apres l'installation.
struct AnswerView {
    std::string title, text;
    std::string confirm;     // "Voir ma licence", "Nouvelle demande\xE2\x80\xA6"
    int         tab{kTabMine};
    bool        accepted{true};
};
[[nodiscard]] AnswerView answerView(const Answer& a, const licensing::LicenseState& st);

// ---- 1.37.0 : MON PROFIL (tout facultatif) : il remplit la demande de licence ; le courriel recoit le mail de
// l'editeur a l'acceptation. Dans le dossier des reglages (commun aux deux Studios), en clair (rien de secret).
struct Profile {
    std::string email, name, company;
    // 1.38.0 : l'adresse a laquelle un mail de l'editeur est PARTI (son serveur l'a accepte) ; vide : aucune.
    std::string verified;
    [[nodiscard]] bool empty() const noexcept { return email.empty() && name.empty() && company.empty(); }
    [[nodiscard]] bool emailVerified() const noexcept { return !email.empty() && verified == email; }
};
// 1.38.0 : un courriel a-t-il la forme d'une adresse (la coche de Mon profil) ?
[[nodiscard]] bool plausibleEmail(const std::string& email);
// 1.38.0 : le mail de l'editeur est parti vers `address` : si c'est le courriel du profil, il est << verifie >>.
void noteEmailReached(const std::string& address);
inline constexpr std::size_t kMaxProfileField = 200;
[[nodiscard]] std::string profilePath();
[[nodiscard]] Profile loadProfile();   // absent ou illisible : vide
// Faux : `why` (un courriel qui n'a pas la forme d'une adresse, un champ trop long, l'ecriture).
bool saveProfile(const Profile& p, std::string* why);

// ---- 1.37.0 : L'APERCU D'UNE FONCTIONNALITE (onglet Editions) : une capture et deux lignes, livrees avec le
// logiciel (resources/licences/fonctions/<id>.png et descriptions.json). `folder` : celui d'a cote de l'exe (main) ;
// sinon resources/licences/fonctions du dossier de travail.
void setFeatureImagesFolder(std::string folder);
// 1.44.0 : PLUSIEURS CAPTURES PAR FONCTIONNALITE (<id>.png, <id>-2.png... <id>-8.png), chacune sa legende et ses bulles
// (descriptions.json, "captures" : {"<id>": [{"legende": "...", "bulles": [{"x":..,"y":..,"l":..,"h":..,"texte":"..."}]}]},
// dans l'ordre des captures) - une zone de la capture (en pixels de la capture) qui dit ce qu'on y voit au survol.
struct CaptureSpot {
    float       x{0.f}, y{0.f}, w{0.f}, h{0.f};
    std::string tip;
};
struct CaptureShot {
    std::string              imagePath;
    std::string              caption;   // vide : pas de legende
    std::vector<CaptureSpot> spots;
    // 1.45.0 : LE REPERE DES BULLES - la taille de l'image pour laquelle leurs x, y, l, h sont ecrits ("repere": {"l": 960,
    // "h": 600}, par capture ou pour tout le fichier) ; 0 : les pixels de l'image elle-meme. Une capture refaite en plus grand
    // (2x : 1920 x 1200, 3200 x 2000) garde ses bulles a leur place sans les recompter.
    float                    refW{0.f}, refH{0.f};
};
struct FeaturePreview {
    std::string imagePath;     // vide : pas de capture (la premiere)
    std::string description;   // vide : pas de texte
    std::vector<CaptureShot> shots;   // 1.44.0 : toutes les captures (la premiere : imagePath)
};
[[nodiscard]] FeaturePreview featurePreview(const std::string& featureId);

// ---- 1.38.0 : LES NOTIFICATIONS DE LA LICENCE (le carnet general, Notices.hpp ; jamais un mail) ----
//  Chaque Studio retient ce qu'il a deja vu (<ses reglages>/vu-licence.json) : les offres du registre (leurs
//  fonctionnalites, leur prix), les annonces de l'editeur, la licence installee. A chaque changement (un registre
//  adopte a la connexion, une licence installee ou renouvelee, revoquee, bientot finie), il compare et poste :
//    - une offre dont les fonctionnalites changent : l'avant et l'apres (ce qui est ajoute, ce qui est retire) ;
//    - une offre nouvelle, une offre retiree ;
//    - une annonce de l'editeur (<< Prevenir mes clients >> : une reduction...), une seule fois ;
//    - la licence : installee, renouvelee, une autre offre, revoquee, finie, bientot finie (14 jours avant).
//  Un client hors connexion les recoit a sa prochaine connexion : elles viennent du registre signe et de la licence.
//  Le premier lancement retient sans rien dire (sauf les annonces en cours).
namespace watch {
struct Offer {
    std::string              id, label, price, promoPrice;
    std::vector<std::string> features;   // les identifiants, tries
    std::int64_t             promoUntil{0};
    bool                     retired{false};
    int                      devices{-1};    // 1.39.0 : les PC au plus (-1 : pas retenu - un vu-licence.json de la 1.38)
    int                      days{-1};       // 1.39.0 : la duree en jours (0 : sans fin ; -1 : pas retenue)
    bool operator==(const Offer&) const = default;
};
struct Seen {
    bool                     known{false};   // un etat retenu existait (faux : le premier lancement)
    std::uint64_t            registry{0};
    std::vector<Offer>       offers;
    std::vector<std::pair<std::string, std::string>> names;   // les fonctionnalites : identifiant, nom
    std::vector<std::pair<std::string, std::string>> products;   // 1.39.0 : identifiant, logiciel (API Studio, IHM Studio...)
    std::vector<std::string> announcements;   // les annonces deja montrees (leurs identifiants)
    std::string              licenseId, edition, editionLabel;
    std::int64_t             expiresAt{0};
    int                      status{-1};      // licensing::LicenseStatus (-1 : inconnu)
    std::int64_t             remindedEnd{0};  // la fin pour laquelle << bientot finie >> a ete dite
    // Pas retenu : les annonces du registre courant (pour compare).
    std::vector<std::tuple<std::string, std::string, std::string, std::string, std::int64_t>> current;   // id, titre, texte, offre, fin
};
// L'etat courant (le registre peut manquer).
[[nodiscard]] Seen snapshot(const licensing::RegistryView* reg, const licensing::LicenseState& st);
// Les notifications de `before` a `now` ; `now.announcements` et `now.remindedEnd` sont mis a jour (a retenir).
[[nodiscard]] std::vector<notices::Item> compare(const Seen& before, Seen& now, std::int64_t at);
// 1.39.0 : l'offre modifiee, telle que sa notification la garde (Item::data, << offre >>) - pour sa fenetre.
struct OfferChange {
    struct Row {
        std::string id, label, product;
        bool        before{false}, after{false};
    };
    struct Value {
        std::string label, before, after;
    };
    std::string        id, label;
    bool               mine{false};
    std::uint64_t      registry{0};
    std::vector<Row>   rows;     // toutes les fonctionnalites de l'avant et de l'apres : les changements d'abord
    std::vector<Value> values;   // le prix, les PC, la duree qui changent
};
[[nodiscard]] std::optional<OfferChange> offerChangeOf(const std::string& data);
[[nodiscard]] std::string seenPath();
[[nodiscard]] bool loadSeen(const std::string& file, Seen& out);
bool saveSeen(const std::string& file, const Seen& s);
// Les essais : un autre fichier (vide : seenPath()).
void setSeenFile(std::string file);
// Regarde (au plus toutes les 3 secondes ; `force` : tout de suite) ; rend le nombre de notifications postees.
std::size_t check(bool force = false);
// Le carnet general : <les reglages de l'application>/notifications.json.
[[nodiscard]] std::string noticesPath();
} // namespace watch

} // namespace licence

// La fenetre Licence (le License Manager du client) ; `tab` : licence::kTab* (1.35.0 : 0 Activation, 1 Ma licence,
// 2 Mes appareils, 3 Fonctionnalites, 4 Editions, 5 Historique, 6 Parametres, 7 Demande a l'editeur ; -1 : le plus utile).
// 1.46.0 : -2 (kTabHome) : son accueil ; 8 (kTabProfile) : la fenetre Mon profil (showProfileWindow), pas la fenetre Licence.
void showLicenceWindow(App& app, int tab = licence::kTabAuto);
// 1.45.0 : UNE SEULE FENETRE LICENCE. Deja ouverte (sous la fenetre de la reponse de l'editeur : << Voir ma licence >>, une
// notification...) : elle passe a l'onglet demande et se relit ; aucune autre ne s'empile par-dessus (avant : deux fenetres
// identiques, deux clics sur la croix pour en sortir). La meme sur une pile de menus (les essais : sans App).
void showLicenceWindow(menu::MenuManager& menus, int tab = licence::kTabAuto);
// 1.45.0 : une fenetre Licence est-elle ouverte (et pas en train de se fermer) ?
[[nodiscard]] bool licenceWindowOpen() noexcept;
// 1.46.0 : la rubrique que montre la fenetre Licence ouverte (licence::kTabHome : son accueil) ; kTabAuto : aucune fenetre.
[[nodiscard]] int licenceWindowSection() noexcept;
// 1.46.0 : MON PROFIL, A PART (plus une rubrique de la fenetre Licence) : sa petite fenetre - le courriel, le nom, la
// societe, Enregistrer. Ouverte par l'accueil du Studio, Aide > Mon profil..., << Modifier mon profil... >> de la demande,
// et showLicenceWindow(kTabProfile) (les appels d'avant). Une seule a la fois ; par-dessus la fenetre Licence s'il y en a une.
void showProfileWindow(App& app);
void showProfileWindow(menu::MenuManager& menus);
[[nodiscard]] bool profileWindowOpen() noexcept;
namespace licence {
// 1.42.0 : l'option a choisir a l'ouverture de l'onglet Options (une action fermee qui la propose) ; prise une fois.
void setWantedOption(std::string optionId);
[[nodiscard]] std::string takeWantedOption();
// 1.44.0 : l'offre a choisir a l'ouverture de Demander ma licence (l'essai : licence-offre ID) ; prise une fois.
void setWantedOffer(std::string edition);
[[nodiscard]] std::string takeWantedOffer();
// 1.44.0 : le centre (a l'ecran) de la bulle n d'une capture de la fenetre Licence (false : pas une capture, pas de bulle n).
[[nodiscard]] bool captureSpotCenter(const ui::Widget& w, std::size_t n, float& x, float& y);
} // namespace licence
// 1.38.0 : la fenetre Notifications (le carnet general, Notices.hpp) ; le bouton d'une notification de licence ouvre la
// fenetre Licence sur son onglet.
void showNoticesWindow(App& app);
// 1.44.0 : ouverte sur le filtre << Avec fichiers >> (withFiles).
void showNoticesWindow(App& app, bool withFiles);
namespace licence {
// 1.35.1 : le controle de projectsAllowed, avec la fenetre << Licence necessaire >> quand c'est ferme (son bouton : la
// fenetre Licence, onglet Activation). Vrai : on peut continuer.
bool gateProjects(App& app);
// 1.36.0 : la fenetre de la reponse de l'editeur (answerView) ; son bouton : la fenetre Licence (Ma licence, ou une
// nouvelle demande).
void showAnswer(App& app, const Answer& a);
void showAnswer(menu::MenuManager& menus, const Answer& a);   // 1.45.0 : la meme, sur une pile de menus (les essais)
// 1.45.0 : la capture `shot` (0 : la premiere) d'une fonctionnalite en grand (ses fleches, sa legende, ses bulles) ; faux :
// pas de capture. L'essai : licence-capture ID [N].
bool showFeatureCapture(App& app, const std::string& feature, std::size_t shot = 0);
// 1.39.0 : la fenetre de la licence perdue (Licence... : Ma licence).
void showLoss(App& app, const Loss& l);
// 1.44.0 : le bouton d'une notification (son action) : "licence:<onglet>", "detail:<cle>", "fichiers:<cle>" (la fenetre du
// message et de ses fichiers, planche 05), "maj:<version>" (la fenetre de la mise a jour, planche 06). Faux : rien ouvert.
// Ecrite dans UpdateWindows.cpp.
bool openNoticeAction(App& app, const std::string& action);
// 1.44.0 : la fenetre de la mise a jour disponible (la pastille, le toast) ; faux : aucune.
bool showUpdateWindow(App& app);

// ---- 1.46.0 : LA BOUTIQUE (docs/BOUTIQUE.md, par. 7 ; le modele : LicenceShop.hpp ; les fenetres : LicenceShopWindows.cpp) ----
// ACHETER : un recapitulatif - l'offre (ou l'option et sa duree), ce qu'elle donne (les PC, la duree, les fonctionnalites),
// le montant TTC, la societe, le nom et le courriel du profil (<< Modifier mon profil... >>), la phrase de la carte
// (shop::cardSentence), le paiement d'essai s'il l'est ; << Payer ... sur Stripe... >> passe la commande (le fil du
// service) et ouvre la fenetre << Commande n. N >>, qui suit son etat. Ce que montre la fenetre (sans fenetre : les essais) :
struct PurchaseView {
    bool                     ok{false};       // l'article est en vente en ligne (faux : `missing` dit pourquoi)
    std::string              title;           // "Acheter PROFESSIONAL", "Acheter une option"
    std::string              article, label;  // l'identifiant (le catalogue), le libelle de la facture
    std::string              what;            // l'offre ou l'option (son nom au registre)
    std::string              amount;          // "490,00 EUR TTC" (le signe de l'euro)
    std::vector<std::string> gives;           // ce qu'il donne (une ligne chacune)
    std::string              company, name, email;   // le profil (la facture)
    std::string              missing;         // vide : rien ne manque ; sinon la phrase (la societe, le courriel, la licence)
    std::string              testMode;        // vide : un paiement reel
    std::string              card;            // la phrase de la carte
    std::string              confirm;         // "Payer 490,00 EUR TTC sur Stripe..."
    std::string              licenseId;       // une option : la licence de ce PC
    std::string              optionText;      // une option : << Assistant IA avance, 1 an >>
};
[[nodiscard]] PurchaseView purchaseView(const shop::Catalogue& cat, const std::string& article, const Profile& p, const licensing::LicenseState& st);
// 1.48.0 : le recapitulatif du PANIER (ses lignes a payer, le total) - showPurchase avec un article vide.
[[nodiscard]] PurchaseView cartView(const shop::Catalogue& cat, const shop::Cart& cart, const Profile& p);
// 1.48.0 : le panier part - `payment` : ses lignes a payer (UNE page de paiement), sinon ses lignes sans paiement (une
// demande ; `all` : tout le panier, sans paiement) ; la fenetre << Commande >> suit. Faux : `why`.
bool startCartOrder(menu::MenuManager& menus, bool payment, bool all, std::string* why, bool showWindow = true);
// La fenetre << Commande n. N >> : son etat, ce qui se passe, ses boutons.
struct OrderView {
    std::string                                      title;   // "Commande n. 12"
    std::string                                      state;   // shop::stateLabel
    std::string                                      text;    // shop::stateText
    int                                              tone{0};
    std::vector<std::pair<std::string, std::string>> facts;   // Article, Montant, Passee le, Paiement, Licence...
    bool                                             pay{false}, cancel{false}, receipt{false}, licence{false};
};
[[nodiscard]] OrderView orderView(const shop::Order& o, licensing::Seconds now);
// Le recapitulatif d'un achat (une seule fenetre a la fois) ; `optionText` : une option (<< Assistant IA avance, 1 an >>).
void showPurchase(menu::MenuManager& menus, const std::string& article, const std::string& optionText = {});
// La fenetre << Commande n. N >> (0 : la commande en train d'etre passee - elle prend son numero a la reponse du service).
void showOrder(menu::MenuManager& menus, std::uint64_t number);
[[nodiscard]] bool purchaseWindowOpen() noexcept;
[[nodiscard]] bool orderWindowOpen() noexcept;
[[nodiscard]] std::uint64_t orderWindowNumber() noexcept;
// La phrase de l'accueil de la fenetre Licence pour la commande qui attend (vide : aucune) ; `tone` : 1 bien, 2 attention.
[[nodiscard]] std::string orderHint(const std::vector<shop::Order>& orders, int* tone = nullptr);
// Les essais (et les scripts) : la fenetre << Acheter >> ouverte - son bouton << Payer >> (comme un clic) ; faux : aucune.
bool confirmPurchaseForTests();
} // namespace licence

} // namespace app
