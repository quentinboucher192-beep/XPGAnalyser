// =============================================================================
//  app/LicenceService.hpp - 1.35.0 : LE CLIENT DU SERVICE DES LICENCES (le
//  Worker Cloudflare de l'editeur ; le contrat : docs/SERVICE-LICENCES.md, les
//  formats : licensing/Service.hpp) - remplace la passerelle de la 1.34
// -----------------------------------------------------------------------------
//  LES APPELS SIGNES : chaque appel /v1/... qui touche a ce PC porte les quatre
//  en-tetes X-XPG-* (service::signRequest), signes par la cle de ce PC
//  (licensing::deviceSignServiceMessage). L'heure signee = l'heure du systeme
//  corrigee de l'ecart appris de GET /v1/etat ; un refus 401 du a l'heure (l'ecart
//  a change) : /v1/etat relu, l'appel refait UNE fois.
//
//  A CHAQUE CONNEXION (un travail qui parle au service) : GET /v1/etat (l'ecart,
//  le service certifie, le registre publie : adopte s'il est plus recent), le
//  certificat (GET /v1/certificat, garde en memoire), puis l'heure signee
//  (GET /v1/heure?n=<nonce frais> -> licensing::noteServiceTime : le compteur du
//  bail est recale).
//
//  CE QUE FAIT LE CLIENT :
//    activer     un code (XPG-XXXX-XXXX-XXXX-XXXX) + la demande de ce PC (.xpgreq)
//                + le nom du PC -> la licence, l'activation, le certificat ->
//                licensing::installActivation. LE CODE EST UN SECRET : il ne va que
//                dans le corps de cet appel - jamais dans un message, l'historique,
//                le releve, un journal, une adresse.
//    renouveler  le bail de chaque activation (installActivation), celles que le
//                service ne renouvelle plus (`retirees`, et leur motif : liberee,
//                retiree, revoquee, desactivee, autre_poste... : dropActivation ;
//                expiree : gardee, le magasin la dit finie), la licence en ligne
//                dont le service ne dit plus rien (effacee), l'etat << poste >>
//                (les reponses aux demandes, et la revocation d'une licence liee a
//                ce PC, comme en 1.34). La liste de revocation de l'editeur ne
//                vient plus du service.
//    liberer     ce PC rend son poste : tout de suite dans les changements de PC
//                permis (2 par mois, ou autant que de postes), sinon a la fin de
//                son bail (le service le dit, la phrase aussi) ; son activation est
//                effacee.
//    LA STATION : renouveler, liberer, prendre et rendre le jeton portent
//                l'empreinte de l'installation de Windows de ce PC
//                (licensing::thisStationHash) : une cle de PC recopiee ailleurs
//                ne renouvelle rien, ne prend aucun jeton.
//    decouverte  l'empreinte de la machine (thisMachineHash) -> le debut connu du
//                service (installTrialRecord : le plus ancien des deux est garde).
//    flottante   le jeton (15 min) d'une licence flottante, et le rendre.
//    demandes    la demande manuelle (sans code), scellee pour la cle de l'editeur
//                que ce programme croit ; l'annuler ; GET /v1/poste : sa reponse.
//  Hors ligne, un refus, une limite (429 : quand reessayer) : une phrase en
//  francais, RIEN n'est invente, jamais de reussite pretendue.
//
//  LE RELEVE LOCAL : service-licences.json dans le dossier des licences (ecrit
//  d'un coup) : la demande manuelle (son numero, le poste, le nonce, la date, le
//  client...), le dernier etat << poste >> traite, les dernieres connexions (le
//  renouvellement, la Decouverte enregistree et son debut, l'heure), la licence
//  activee en ligne et le nom donne au PC. JAMAIS LE CODE.
//  DEUX STUDIOS SUR LE MEME DOSSIER (API Studio et IHM Studio) : le releve est
//  RELU juste avant chaque ecriture et fusionne (mergeRecords), comme en 1.34.
//
//  LES FONCTIONS BLOQUANTES font du reseau : un fil de travail seulement (le fil
//  de fond ci-dessous, un essai).
//
//  LE FIL DE FOND (l'application, pas le poste d'exploitation) : startRunner() -
//  au lancement : heure -> renouveler (une licence en ligne) -> Decouverte (pas
//  encore enregistree, puis une fois par jour) -> le code laisse par
//  l'installateur (active puis efface ; garde si le reseau manque) -> poste.
//  Ensuite : la licence flottante prend son jeton et le renouvelle toutes les
//  5 minutes (sans poste libre : un avis << libre dans N min >>, un nouvel essai) ;
//  le bail est renouvele chaque heure s'il finit dans 7 jours ; une demande qui
//  attend est verifiee toutes les minutes (1.36.0 ; les deux premieres heures, puis
//  toutes les 5 minutes : requestCheckDelay), un etat a reprendre chaque heure. UN travail a la fois ; poll() (le fil de
//  l'interface, chaque image : licence::tick) releve le resultat, pose les avis
//  (la cloche, la barre d'etat) et revalue la licence. shutdown() arrete la
//  requete en cours, attend le fil, puis rend le jeton flottant (au mieux, un
//  delai court, sur un fil).
// =============================================================================
#pragma once

#include "../licensing/Service.hpp"
#include "../licensing/Store.hpp"
#include "../licensing/Time.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::licence::service {

// ---- l'adresse, la disponibilite ----
// Par defaut : licensing::service::compiledAddress() (une construction d'essai : XPG_SERVICE). Les essais en posent
// une (nullopt : retour a l'adresse compilee).
void setAddressForTests(std::optional<licensing::service::Address> address);
[[nodiscard]] licensing::service::Address address();
// Le service sert-il dans ce programme ? Faux : `why` (pas de service compile, ou licences pas activees).
[[nodiscard]] bool available(std::string* why = nullptr);
// L'ecart (secondes) entre l'heure du service et celle de ce PC, appris de /v1/etat (0 : pas encore appris).
[[nodiscard]] licensing::Seconds clockOffset() noexcept;

// ---- le releve local ----
inline constexpr const char* kRecordFile = "service-licences.json";
inline constexpr const char* kLocalSent = "envoyee";
inline constexpr const char* kLocalCancelled = "annulee";

struct Record {
    // la demande manuelle (numero 0 : aucune)
    std::uint64_t      number{0};       // le numero rendu par le service
    std::string        device;          // l'identifiant du poste (service::deviceId)
    std::string        nonce;           // celui de la demande signee
    licensing::Seconds sentAt{0};
    std::string        customer, contact, message, pcName, licenseId;
    std::string        edition, email;  // 1.37.0 : l'edition choisie (la boutique), le courriel (facultatifs)
    std::string        option, optionText;   // 1.42.0 : l'option demandee (son identifiant) et ce qu'en dit l'ecran (<< 1 mois >>)
    std::string        local;           // kLocalSent, kLocalCancelled ; vide : aucune demande
    licensing::Seconds cancelledAt{0};
    std::string        outcome;         // la reponse de l'editeur a CETTE demande : "acceptee", "refusee", "annulee"
    // l'etat << poste >> (signe par l'editeur) traite
    std::uint64_t      lastNumber{0};
    std::string        lastState;       // licensing::gateway::stateKey
    std::string        lastNonce;
    licensing::Seconds lastCheck{0};
    std::string        lastMessage;     // la derniere phrase (une licence installee, un refus...)
    std::string        receivedLicense; // la licence installee par une reponse de l'editeur
    // les connexions au service
    licensing::Seconds lastSync{0};       // le service a repondu (la derniere fois)
    licensing::Seconds lastRenew{0};      // le dernier renouvellement fait
    licensing::Seconds lastTime{0};       // la derniere heure signee notee
    licensing::Seconds trialSync{0};      // la Decouverte declaree (la derniere fois)
    licensing::Seconds trialRecordedAt{0};// la premiere fois que le service l'a enregistree
    licensing::Seconds trialStart{0};     // le debut que le service connait pour cette machine
    // l'activation en ligne (par code) : la licence et le nom donne au PC - JAMAIS le code
    std::string        activatedLicense;
    std::string        activatedPcName;
    licensing::Seconds activatedAt{0};
};
[[nodiscard]] std::string recordPath();

// ---- 1.38.0 : LES PC D'UNE LICENCE EN LIGNE (Mes appareils) ----
//  A chaque renouvellement, le service donne, pour chaque licence renouvelee, ses PC (ce PC d'abord) : << 3 PC sur 20 >>.
//  Garde dans le dossier des licences (postes-en-ligne.json, commun aux deux Studios) : la fenetre Licence le relit,
//  sans reseau. Jamais la station ni le numero d'un autre PC (le service ne les donne pas).
struct OnlineDevice {
    std::string        name, key;          // le nom donne a l'activation (peut etre vide) ; la cle publique (64 hex)
    licensing::Seconds activatedAt{0}, seenAt{0}, until{0};
    std::string        state;              // "active", "liberee" (garde sa place jusqu'a `until`)
    bool               thisPc{false}, token{false};
};
struct OnlineDevices {
    std::string               licenseId, type;   // type : "perpetual", "temporary", "evaluation", "floating"
    int                       max{0}, used{0}, total{0};
    std::vector<OnlineDevice> list;
    licensing::Seconds        receivedAt{0};
};
[[nodiscard]] std::string devicesPath();
// Le champ << postes >> d'une reponse de /v1/renouveler (un objet : licence -> ses PC) ; faux : illisible.
bool parseDevices(std::string_view postesJson, licensing::Seconds receivedAt, std::vector<OnlineDevices>& out);
// Les PC retenus d'une licence (le dernier renouvellement) ; vide : rien de retenu.
[[nodiscard]] std::optional<OnlineDevices> onlineDevices(const std::string& licenseId);
// "4F9A12..CDEF01" : une cle publique courte, lisible.
[[nodiscard]] std::string shortKey(const std::string& keyHex);
// Absent : vrai, un releve vide. Faux : `why` (illisible).
bool loadRecord(Record& out, std::string* why);
bool saveRecord(const Record& r, std::string* why);
// La fusion (voir plus haut) : `disk` (relu), `mine` (ce que ce travail a fait).
[[nodiscard]] Record mergeRecords(const Record& disk, const Record& mine);
// Une demande manuelle attend : envoyee, pas annulee, sans reponse finale.
[[nodiscard]] bool pending(const Record& r) noexcept;
// La phrase de l'etat d'une demande manuelle.
[[nodiscard]] std::string stateText(const Record& r);
// "07/11/2026 10:42" (heure locale) ; vide : 0.
[[nodiscard]] std::string dateText(licensing::Seconds at);
// "07/11/2026" (heure locale) ; vide : 0.
[[nodiscard]] std::string dayText(licensing::Seconds at);

// ---- la demande manuelle (sans code) ----
struct RequestForm {
    std::string customer;    // obligatoire
    std::string contact;     // facultatif
    std::string pcName;      // facultatif
    std::string licenseId;   // facultatif : une licence existante de la societe
    std::string message;     // facultatif
    std::string edition;     // 1.37.0, facultatif : l'edition choisie dans la boutique (son identifiant, [A-Z0-9_])
    std::string email;       // 1.37.0, facultatif : le courriel (le mail de l'editeur a l'acceptation)
    // 1.42.0, facultatifs : une OPTION pour la licence de ce PC (licenseId : la licence installee) - son identifiant, sa duree
    // (jours ; 0 : jusqu'a la fin de la licence ; -1 : des dates), son debut (0 : des qu'elle est acceptee), son dernier jour
    // (des dates, compris), et ce qu'en dit l'ecran (<< Travail a plusieurs, 1 mois >>).
    std::string        option;
    int                optionDays{0};
    licensing::Seconds optionFrom{0}, optionLastDay{0};
    std::string        optionText;
};
inline constexpr std::size_t kMaxCustomer = 200, kMaxContact = 200, kMaxMessage = 2000, kMaxPcName = 80, kMaxLicenseId = 64, kMaxEdition = 40,
                             kMaxEmail = 200;

// ---- ce qu'a donne un travail ----
// 1.39.0 : Watch - le regard toutes les 2 minutes sur GET /v1/etat (rien d'ecrit, rien de dit : busyJob() ne le montre pas).
enum class Job : std::uint8_t { None, Sync, Time, Activate, Renew, Release, Trial, LeaseTake, LeaseReturn, Request, CancelRequest, Device, Watch };
struct Notice {
    std::string key;     // "licence:activation"...
    std::string title;
    std::string tone;    // "ok", "info", "warning", "error"
    bool        lost{false};   // 1.39.0 : la licence de ce PC perdue (retiree, revoquee, desactivee) : une fenetre aussi
    std::string licenseId;     // 1.39.0 : laquelle (lost)
};
struct Result {
    Job                 job{Job::None};
    bool                ok{false};                // fait
    bool                reached{false};           // le service a repondu (au moins une fois)
    bool                changed{false};           // une licence, une activation, un jeton, le releve...
    bool                licenseInstalled{false};
    bool                refused{false};           // un refus du service ou de l'editeur (pas une panne)
    bool                definitive{false};        // activer : refus definitif (le code ne servira pas)
    bool                pendingAfter{false};      // une demande manuelle attend encore
    bool                retryLater{false};        // un etat << poste >> a reprendre
    bool                noSeat{false};            // flottante : aucun poste libre
    int                 retryAfter{0};            // secondes : 429 (reessayer_dans), 409 flottante (libre_dans)
    std::string         licenseId;
    std::string         message;                  // une phrase en francais (le dernier geste)
    std::vector<Notice> notices;                  // a dire en avis (la cloche, la barre d'etat)
    // 1.36.0 : LA REPONSE DE L'EDITEUR a la demande de ce PC, apportee par CE travail (la fenetre qui la confirme) :
    // kAnswerAccepted (la licence installee), kAnswerUpdated (mise a jour), kAnswerRefused ; vide : rien de neuf.
    std::string         answer;
    std::string         answerNote;               // le mot de l'editeur (acceptee) ou la raison (refusee)
    // 1.37.0 : une licence EN LIGNE activee par le code recu de l'editeur (scelle pour ce PC). Le code n'est rendu
    // (answerCode, en memoire seulement) que si le mail de l'editeur n'est pas parti : la fenetre le montre.
    bool                answerOnline{false};
    std::string         answerCode;               // "XPG-XXXX-..." ; vide : rien a montrer
    std::string         answerMail;               // gateway::kMailSent, kMailFailed ; vide : pas de mail
    std::string         answerMailTo;
    licensing::Seconds  requestSentAt{0};         // une demande attend encore : envoyee le (0 : aucune)
    // 1.39.0 : ce que GET /v1/etat a dit en se connectant (-1 : pas lu, ou un service d'avant la 1.39) - le compteur des
    // changements chez les clients (une revocation, une licence desactivee, un PC retire) et le numero du registre.
    long long           serviceChanges{-1};
    long long           serviceRegistry{-1};
};
// 1.39.0 : LA LICENCE PERDUE EN DIRECT - tant qu'une licence est sur ce PC, le fil de fond regarde GET /v1/etat toutes
// les kWatchPeriod secondes (un appel public, rien d'ecrit) ; le compteur des changements ou le registre a bouge :
// la synchronisation complete tout de suite (le bail : retiree, revoquee, desactivee ; l'etat << poste >> ; le registre).
inline constexpr double kWatchPeriod = 120.0;
inline constexpr const char* kAnswerAccepted = "acceptee";
inline constexpr const char* kAnswerUpdated = "mise_a_jour";
inline constexpr const char* kAnswerRefused = "refusee";
// 1.36.0 : une demande qui attend, sa reponse regardee : toutes les minutes les deux premieres heures apres l'envoi,
// puis toutes les 5 minutes (age : secondes depuis l'envoi ; un age negatif, l'horloge reculee : comme au debut).
[[nodiscard]] double requestCheckDelay(licensing::Seconds age) noexcept;

// ---- BLOQUANTS : un fil de travail seulement ----
[[nodiscard]] Result readTime();                                           // etat + heure signee
[[nodiscard]] Result activate(const std::string& code, const std::string& pcName);
[[nodiscard]] Result renew();
[[nodiscard]] Result release(const std::string& licenseId = {});           // vide : la licence en ligne de ce PC
[[nodiscard]] Result trial();
[[nodiscard]] Result takeLease();
[[nodiscard]] Result returnLease();
[[nodiscard]] Result sendRequest(const RequestForm& form);
[[nodiscard]] Result cancelRequest();
[[nodiscard]] Result checkDevice();
// Le lancement : heure -> renouveler -> Decouverte -> code de l'installateur -> poste.
[[nodiscard]] Result sync();
// 1.39.0 : GET /v1/etat seulement (le compteur des changements, le registre) ; rien d'ecrit sur ce PC.
[[nodiscard]] Result watchState();

// ---- le fil de fond (le fil de l'interface appelle) ----
void startRunner();
bool startSync();
bool startTime();
bool startActivate(const std::string& code, const std::string& pcName);
bool startRenew();
bool startRelease(const std::string& licenseId = {});
bool startTrial();
bool startTakeLease();
bool startReturnLease();
bool startRequest(const RequestForm& form);
bool startCancelRequest();
bool startCheckDevice();
bool startWatch();   // 1.39.0
[[nodiscard]] bool busy() noexcept;
[[nodiscard]] Job  busyJob() noexcept;
// Chaque image : releve un travail fini (les avis, la revaluation) ; lance ce que le fil de fond a a faire.
void poll();
[[nodiscard]] Result lastResult();
[[nodiscard]] unsigned long long revision() noexcept;
// Le poste flottant rendu par l'utilisateur (Rendre le poste) : le fil de fond ne le reprend plus tout seul.
[[nodiscard]] bool leaseHeldBack() noexcept;
// Flottante sans poste libre : quand un poste se libere (secondes, horloge du systeme ; 0 : inconnu).
[[nodiscard]] licensing::Seconds seatFreeAt() noexcept;
// La phrase d'un travail en cours ("Activation en cours...").
[[nodiscard]] std::string jobText(Job j);
// Au lancement (l'application, pas le poste d'exploitation), AVANT la premiere evaluation du module : une licence
// flottante en ligne sur ce PC prend son poste tout de suite (un magasin a part, sur un fil ; `waitMs` au plus,
// puis la requete est arretee ; hors ligne, ou un jeton encore bon : rien). L'etat ne passe donc pas par
// << Flottante : aucun poste >> a chaque lancement (ni dans l'historique). Vrai : un jeton est installe.
bool startupLease(const licensing::StoreOptions& options, int waitMs = 4000);
// A la fermeture : la requete en cours arretee, le fil attendu, le jeton flottant rendu au service (au mieux, delai
// court ; le fichier du jeton reste jusqu'a sa fin : l'etat de ce PC ne change pas en partant).
void shutdown();

} // namespace app::licence::service
