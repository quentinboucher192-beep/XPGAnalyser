// =============================================================================
//  app/LicenceGateway.hpp - 1.34.0 : LA DEMANDE EN LIGNE (le cote des Studios
//  de la passerelle GitHub des licences ; les formats : licensing/Gateway.hpp)
// -----------------------------------------------------------------------------
//  CE QUE FAIT LE CLIENT, TOUT SEUL :
//    envoyer   sa demande d'activation a l'editeur : une issue du depot de la
//              passerelle (le jeton des demandes, lu dans la configuration
//              signee), son corps scelle pour la cle de l'editeur qui a signe
//              cette configuration. UNE SEULE DEMANDE EN COURS PAR PC.
//    verifier  la configuration (signee, pour ce depot), le registre et la liste
//              de revocation (importes s'ils sont plus recents), puis l'etat de
//              CE PC (postes/<poste>.xpgpos, signe par l'editeur, pour ce poste,
//              plus recent que le dernier vu) : acceptee -> la licence, scellee
//              pour ce PC, est installee (aucun fichier a importer) ; refusee ->
//              la raison ; annulee, revoquee, retiree -> notees. La liste de
//              revocation scellee dans l'etat (revoquee, le plus souvent) est
//              importee AVANT la licence (signee : le magasin la verifie ; une
//              licence revoquee devient Revoquee sur ce PC).
//    annuler   un commentaire signe par la cle de ce PC, puis l'issue fermee ;
//              une nouvelle demande est alors permise.
//
//  1.34.0 (relecture de securite) - CE QUE LE PC CROIT D'UN ETAT :
//    - l'identifiant de la licence vient de la licence elle-meme (installee ou
//      recue, scellee) ou du releve, JAMAIS du champ en clair de l'etat (vide :
//      le depot est public) ;
//    - un etat << recue >>, << annulee >> ou << refusee >> ne vaut pour la
//      demande en cours que s'il porte SON nonce exact (et qu'elle attend) ;
//      sinon (un etat ancien republie, rejoue) : note dans l'historique, son
//      numero traite, rien d'autre ne change (ni l'etat affiche, ni la reponse) ;
//    - un etat << acceptee >> ou << mise_a_jour >> porteur d'une licence est
//      toujours traite (la licence est verifiee par le magasin) ; si elle n'est
//      PAS installee (le sceau ne s'ouvre pas, le dossier refuse l'ecriture,
//      l'horloge en arriere, une licence d'un autre PC...), l'etat n'est PAS
//      marque traite (ni son numero, ni la reponse) : la raison va dans la
//      derniere phrase et la verification de l'heure le reprend.
//
//  LE RELEVE LOCAL : demande-en-ligne.json dans le dossier des licences (ecrit
//  d'un coup) - l'issue, le poste, le nonce de la demande, la date, le client,
//  le contact, le message, le nom du PC, la licence demandee, l'etat local
//  (envoyee, annulee), le dernier etat vu de l'editeur (son numero), la
//  derniere verification, la derniere phrase. JAMAIS LE JETON.
//  DEUX STUDIOS SUR LE MEME DOSSIER (API Studio et IHM Studio, ouverts ensemble) :
//  check, send et cancel lisent le releve, font du reseau, puis l'ecrivent ; pour
//  ne rien perdre de ce que l'autre a ecrit entre-temps, le releve est RELU juste
//  avant l'ecriture et fusionne (mergeRecords) : la demande la plus recente
//  (envoyee le, puis le numero de l'issue) ; pour la meme demande, une
//  annulation ou une reponse de l'un ou de l'autre reste ; ce que la passerelle a
//  dit vient de celui qui a traite l'etat le plus recent ; le plus grand numero
//  de configuration, la verification la plus recente. Pas de fichier verrou (un
//  verrou abandonne par un programme tue bloquerait l'autre) : la fenetre qui
//  reste (entre la relecture et l'ecriture) est de l'ordre de la milliseconde.
//  send relit aussi le releve juste avant de creer l'issue (une demande partie
//  depuis l'autre Studio est vue : pas de seconde issue).
//
//  LES FONCTIONS BLOQUANTES (check, send, cancel) font du reseau : un fil de
//  travail seulement (le fil de fond ci-dessous, un essai). Hors ligne, une
//  erreur HTTP, la limite de GitHub : une phrase claire, RIEN ne change, jamais
//  de reussite inventee. Le jeton des demandes ne va que dans l'en-tete
//  Authorization : jamais dans une adresse, un message, l'historique, un journal.
//
//  LE FIL DE FOND (l'application) : startCheck / startSend / startCancel lancent
//  UN travail a la fois sur un fil ; poll() (le fil de l'interface, chaque
//  image : licence::tick) releve le resultat, pose les avis (la cloche, la
//  barre d'etat) et revalue la licence ; une verification par heure tant
//  qu'une demande attend ou qu'un etat est a reprendre. shutdown() arrete la
//  requete en cours et attend le fil (HttpClient::cancel : la fermeture ne
//  reste pas pendue). Sans shutdown() (une sortie brutale), le destructeur du
//  fil fait de meme : ce qu'il verrouille est construit AVANT lui (detruit
//  apres).
// =============================================================================
#pragma once

#include "../licensing/Gateway.hpp"
#include "../licensing/Time.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace app::licence::gateway {

// ---- l'adresse, la disponibilite ----
// Par defaut : licensing::gateway::compiledAddress() (et, dans une construction d'essai, XPG_PASSERELLE).
// Les essais en posent une (nullopt : retour a l'adresse compilee).
void setAddressForTests(std::optional<licensing::gateway::Address> address);
[[nodiscard]] licensing::gateway::Address address();
// La passerelle sert-elle dans ce programme ? Faux : `why` (une phrase : pas de passerelle compilee, ou
// licences pas activees) - la demande et la licence passent alors par fichier, comme avant.
[[nodiscard]] bool available(std::string* why = nullptr);

// ---- le releve local ----
inline constexpr const char* kRecordFile = "demande-en-ligne.json";
inline constexpr const char* kLocalSent = "envoyee";
inline constexpr const char* kLocalCancelled = "annulee";

struct Record {
    // la demande envoyee (issue 0 : aucune)
    int                issue{0};
    std::string        device;          // l'identifiant du poste (Gateway.hpp)
    std::string        nonce;           // celui de la demande signee
    licensing::Seconds sentAt{0};
    std::string        customer, contact, message, pcName, licenseId;
    std::string        local;           // kLocalSent, kLocalCancelled ; vide : aucune demande
    licensing::Seconds cancelledAt{0};
    std::string        outcome;         // la reponse de l'editeur a CETTE demande : "acceptee", "refusee", "annulee"
    // ce que la passerelle a dit de ce PC (garde d'une demande a l'autre)
    std::uint64_t      lastNumber{0};   // le numero du dernier etat traite (un etat pas plus recent est ignore)
    std::string        lastState;       // sa cle (licensing::gateway::stateKey) ; vide : aucun
    std::string        lastNonce;       // la demande a laquelle il repondait
    licensing::Seconds lastCheck{0};
    std::string        lastMessage;     // la derniere phrase (une licence installee, un refus et sa raison...)
    std::string        receivedLicense; // la licence installee par la passerelle (son identifiant)
    std::uint64_t      configNumber{0}; // la configuration la plus recente vue (une plus ancienne est refusee)
};
[[nodiscard]] std::string recordPath();
// Absent : vrai, un releve vide. Faux : `why` (illisible).
bool loadRecord(Record& out, std::string* why);
bool saveRecord(const Record& r, std::string* why);   // tel quel (check, send, cancel : relu et fusionne avant)
// La fusion de deux releves (voir plus haut) : `disk` (relu), `mine` (ce que ce travail a fait).
[[nodiscard]] Record mergeRecords(const Record& disk, const Record& mine);
// Une demande attend : envoyee, pas annulee, sans reponse finale (acceptee, refusee, annulee) de l'editeur.
[[nodiscard]] bool pending(const Record& r) noexcept;
// La phrase de l'etat d'une demande ("Envoy\xC3\xA9" "e \xE2\x80\x94 en attente de l'\xC3\xA9" "diteur"...).
[[nodiscard]] std::string stateText(const Record& r);
// "07/11/2026 10:42" (heure locale) ; vide : 0.
[[nodiscard]] std::string dateText(licensing::Seconds at);

// ---- le formulaire ----
struct SendForm {
    std::string customer;    // obligatoire : la societe, le client
    std::string contact;     // facultatif : un courriel, un telephone
    std::string pcName;      // facultatif : le nom donne a ce PC
    std::string licenseId;   // facultatif : une licence existante de la societe (ajouter ce PC)
    std::string message;     // facultatif : un mot pour l'editeur
};
inline constexpr std::size_t kMaxCustomer = 200, kMaxContact = 200, kMaxMessage = 2000, kMaxPcName = 80, kMaxLicenseId = 64;

// ---- ce qu'a donne un travail ----
enum class Job : std::uint8_t { None, Check, Send, Cancel };
struct Result {
    Job         job{Job::None};
    bool        ok{false};                // fait (la verification a abouti, la demande est partie, annulee)
    bool        changed{false};           // quelque chose a change (une licence, un registre, un etat, le releve)
    bool        licenseInstalled{false};
    bool        refused{false};           // l'editeur a refuse la demande
    bool        pendingAfter{false};      // une demande attend encore
    bool        retryLater{false};        // un etat n'a pas pu etre applique (sa licence) : la verification de l'heure le reprend
    std::string licenseId;
    std::string message;                  // une phrase, en francais (le dernier geste)
    std::string notice;                   // a dire en avis ("" : rien) : licence installee, refus, revocation...
    std::string noticeTone;               // "ok", "warning", "error"
};

// ---- BLOQUANTS : un fil de travail seulement ----
[[nodiscard]] Result check();
[[nodiscard]] Result send(const SendForm& form);
[[nodiscard]] Result cancel();

// ---- le fil de fond (le fil de l'interface appelle) ----
// L'application : une verification tout de suite (si la passerelle sert), puis une par heure tant qu'une
// demande attend (ou qu'un etat est a reprendre : Result::retryLater).
void startRunner();
bool startCheck();                    // faux : un travail est deja en cours (ou la passerelle ne sert pas)
bool startSend(const SendForm& form);
bool startCancel();
[[nodiscard]] bool busy() noexcept;   // un travail tourne (ou son resultat n'est pas encore releve)
[[nodiscard]] Job  busyJob() noexcept;
// Chaque image : releve un travail fini (les avis, la revaluation), lance la verification de l'heure.
void poll();
// Le dernier travail fini, et un compteur qui change a chaque fin (la fenetre se rafraichit).
[[nodiscard]] Result lastResult();
[[nodiscard]] unsigned long long revision() noexcept;
// A la fermeture : la requete en cours est arretee, le fil attendu.
void shutdown();

} // namespace app::licence::gateway
