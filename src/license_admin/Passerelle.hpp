// =============================================================================
//  license_admin/Passerelle.hpp - 1.34.0 : LA PASSERELLE GITHUB DES LICENCES,
//  COTE EDITEUR (l'outil de l'EDITEUR seulement ; jamais livre a un client)
// -----------------------------------------------------------------------------
//  Le protocole est decrit dans licensing/Gateway.hpp. Ici, ce que fait l'outil
//  de l'editeur avec le depot PUBLIC de la passerelle :
//    - le regler (passerelle.json, dans le dossier de la cle) et garder le
//      JETON DES DEMANDES (celui des clients : Issues seulement) protege comme
//      le jeton GitHub (DPAPI : jeton-demandes.cle) ;
//    - verifier le depot (public, et le jeton de l'editeur peut y ecrire) ;
//    - publier la configuration signee (passerelle.xpgpas : le jeton des
//      demandes, masque) et le registre de l'archive (1.34.0, relecture : la
//      liste de revocation n'est PLUS publiee en clair - ses raisons sont du
//      texte libre, elle lie des cles de PC a des licences ; elle part scellee
//      dans l'etat des PC concernes, et l'ancienne copie publique est effacee) ;
//    - RELEVER LES DEMANDES : les issues ouvertes par les Studios, ouvertes avec
//      la cle de l'editeur, verifiees (la signature du PC), rangees dans le
//      dossier de la cle (demandes-en-ligne/ : le .xpgreq tel quel, et un
//      fichier .json a cote : le client, le message, l'etat) ; une demande
//      nouvelle recoit l'etat << recue >> (le PC le voit) ; une annulation signee
//      par le PC donne << annulee >> et ferme l'issue ;
//    - publier une licence signee a TOUS ses PC (chacun son etat, la licence
//      scellee pour lui), refuser une demande (la raison scellee), dire une
//      revocation (la liste scellee pour chaque PC revoque), un PC retire ;
//    - preparer les fichiers PUBLICS a compiler dans les Studios (kit).
//
//  LE JETON DE L'EDITEUR est celui de la synchro (GitHubSync : jeton-github.cle) ;
//  il doit couvrir aussi le depot de la passerelle (Contents et Issues : Read and
//  write). Il ne part que vers api.github.com (ou ce poste, pour les essais).
//  AUCUN JETON n'est ecrit dans un message, un journal, un fichier en clair.
//  RIEN D'UN CLIENT n'est ecrit en clair dans le depot : les etats portent leur
//  contenu scelle pour le PC ; les commentaires des issues ne disent que
//  << Acceptee >> ou << Refusee >>.
//
//  RESEAU : tout ce qui suit << le reseau >> est BLOQUANT (l'application le lance
//  par runSlow, la console attend). Hors ligne, un jeton refuse, la limite de
//  GitHub : un refus en francais, jamais une reussite inventee.
//
//  LES ISSUES SONT ECRITES PAR N'IMPORTE QUI (1.34.0, relecture) : le jeton des
//  demandes est public en pratique ; on peut ouvrir, modifier, fermer, commenter
//  une issue, et relire tout ce qui y est passe (les demandes scellees, les
//  annulations signees). Le releve s'en garde :
//    - une demande (poste, nonce) deja vue, ou dont la date SIGNEE n'est pas plus
//      recente que la derniere vue de ce PC, est REJOUEE : ignoree (un probleme
//      est note), jamais rien de publie pour elle ; la plus recente d'un PC se
//      choisit par sa date signee, jamais par le numero de l'issue ;
//    - << recue >> et << annulee >> ne s'ecrivent jamais automatiquement sur un
//      etat publie plus recent que la demande, ou qui porte une licence (acceptee,
//      mise a jour, revoquee, retiree) pour une autre demande : un probleme est
//      note, le fichier du depot ne change pas ;
//    - le corps d'une issue est garde a sa premiere lecture (son empreinte) : un
//      corps modifie ensuite est ignore (un probleme est note), l'etat ne change pas ;
//    - le releve est incremental (since : les issues changees depuis le dernier,
//      moins une marge), une liste coupee est dite, au plus 20 << recue >> (et
//      << annulee >>) publiees par releve (les autres au suivant, c'est dit),
//      l'etat publie d'un PC n'est relu que si son issue a change ;
//    - une demande d'AJOUT a une licence existante n'est pas une preuve
//      (n'importe qui peut taper un identifiant de licence et un nom de client) :
//      joinWarning le dit, l'application demande une confirmation avant de signer.
//  Les numeros (etats, configuration) : max(numero du depot + 1, l'heure Unix) -
//  ils montent encore apres un depot nettoye ou recree.
// =============================================================================
#pragma once

#include "../licensing/Gateway.hpp"
#include "../licensing/KeyFile.hpp"
#include "../licensing/Time.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin::passerelle {

using licensing::Seconds;

inline constexpr const char* kConfigFile = "passerelle.json";
inline constexpr const char* kRequestTokenFile = "jeton-demandes.cle";
inline constexpr const char* kRequestsDir = "demandes-en-ligne";
inline constexpr const char* kFetchFile = "releve.json";          // dans kRequestsDir : le dernier releve
inline constexpr const char* kLicencesDir = "licences";           // dans kRequestsDir : les licences signees pour elles
inline constexpr int kMaxAutoPublishPerFetch = 20;                // << recue >> / << annulee >> publiees au plus par releve
inline constexpr int kMaxListPages = 50;                          // pages de 100 issues lues au plus par releve

// ---- la configuration (passerelle.json, dans le dossier de la cle) ----
struct Config {
    std::string repo;                                       // "proprietaire/nom" : le depot PUBLIC
    std::string branch{"main"};
    std::string api{licensing::gateway::kDefaultApi};       // les essais : http://127.0.0.1:PORT/api
    std::string raw{licensing::gateway::kDefaultRaw};       // les essais : http://127.0.0.1:PORT/raw
};
[[nodiscard]] std::string configPath(const std::string& folder);
[[nodiscard]] bool configured(const std::string& folder);
bool readConfig(const std::string& folder, Config& out, std::string* why);
bool writeConfig(const std::string& folder, const Config& config, std::string* why);
[[nodiscard]] licensing::gateway::Address address(const Config& c);
// "https://github.com/<depot>/issues/<n>" (le lien d'une demande ; 0 : le depot).
[[nodiscard]] std::string issueLink(const std::string& repo, int issue);

// ---- le jeton des demandes (jeton-demandes.cle : DPAPI, ce compte) ----
//  Celui que les Studios recoivent (masque) dans la configuration publiee : un jeton a grain fin limite aux
//  issues de ce depot. Jamais le jeton de l'editeur (refuse).
[[nodiscard]] bool hasRequestToken(const std::string& folder);
bool saveRequestToken(const std::string& folder, const std::string& token, std::string* why);
bool loadRequestToken(const std::string& folder, std::string& token, std::string* why);
bool eraseRequestToken(const std::string& folder, std::string* why);

// ---- le resultat d'une operation (en francais ; jamais un jeton, jamais rien d'un client en clair ici
//      sauf ce que l'editeur voit deja dans son outil) ----
struct Result {
    bool                     ok{false};
    std::string              message;      // la phrase
    std::vector<std::string> details;      // une ligne par fait
    std::vector<std::string> published;    // ce qui a ete ecrit dans le depot ("postes/1a2b... : acceptee")
    int                      newRequests{0};   // fetchRequests : les demandes vues pour la premiere fois
    int                      pending{0};       // fetchRequests : celles qui attendent la decision de l'editeur
    int                      ignored{0};       // fetchRequests : les demandes rejouees, ignorees (nouvelles a ce releve)
    int                      deferred{0};      // fetchRequests : le travail remis au releve suivant (le plafond atteint)
    bool                     truncated{false}; // fetchRequests : la liste des issues coupee (plus de kMaxListPages pages)
    [[nodiscard]] std::string text() const;    // message puis details
};

// ---- une demande en ligne relevee (demandes-en-ligne/issue-<n>.json, et <poste>-<nonce8>.xpgreq) ----
enum class RequestState : std::uint8_t {
    Pending,      // << en_attente >> : vue, l'etat << recue >> pas encore publie
    Received,     // << recue >> : le PC voit << Recue par l'editeur >> ; elle attend la decision
    Accepted,     // << acceptee >> : la licence est publiee pour ce PC, l'issue fermee
    Refused,      // << refusee >>
    Cancelled,    // << annulee >> : le PC l'a annulee (commentaire signe)
    Replaced,     // << remplacee >> : une demande plus recente du meme PC (sa date signee)
                  //   (une issue fermee sans annulation signee reste en attente : un probleme est note)
    Unreadable,   // << illisible >> : scellee pour une autre cle, signature fausse...
    Ignored,      // << ignoree >> : rejouee (deja vue, ou pas plus recente que la derniere de ce PC) ; rien n'est publie
};
[[nodiscard]] const char* requestStateKey(RequestState s) noexcept;
[[nodiscard]] bool parseRequestStateKey(std::string_view key, RequestState& out) noexcept;
[[nodiscard]] std::string requestStateLabel(RequestState s);   // "En attente", "Re\xC3\xA7ue"...

struct Entry {
    int                      issue{0};
    bool                     issueOpen{true};
    RequestState             state{RequestState::Pending};
    std::string              device;        // l'identifiant du poste (gateway::deviceId)
    std::string              deviceKey;     // la cle du PC (64 hexadecimaux) ; vide : illisible
    std::string              nonce;         // la demande
    std::string              customer, contact, message;   // donnes par le client (scelles dans l'issue)
    std::string              deviceName, fingerprint, software, licenseId;   // la demande (licenseId : demandee)
    Seconds                  requestedAt{0};   // la date de la demande (le PC)
    Seconds                  openedAt{0};      // l'issue (GitHub)
    Seconds                  seenAt{0};        // le premier releve qui l'a vue
    Seconds                  answeredAt{0};
    std::string              answer;        // acceptee : la licence ; refusee : la raison (seulement sur ce poste)
    std::string              requestPath;   // le .xpgreq ("" : illisible)
    std::string              sidecarPath;   // le .json
    bool                     receivedPublished{false};
    bool                     receivedHeld{false};  // << recue >> retenue : l'etat publie de ce PC est plus recent (probleme note)
    std::string              bodyHash;      // SHA-256 du corps de l'issue a sa premiere lecture (un corps change ensuite : ignore)
    Seconds                  issueUpdatedAt{0};    // le dernier releve qui l'a vue : updated_at de l'issue
    int                      issueComments{0};     // ... et son nombre de commentaires
    Seconds                  checkedUpdatedAt{0};  // la derniere verification (commentaires, etat publie) : updated_at d'alors
    int                      checkedComments{-1};  // ... et le nombre de commentaires d'alors (-1 : jamais)
    std::vector<std::string> problems;
    [[nodiscard]] bool waiting() const noexcept { return state == RequestState::Pending || state == RequestState::Received; }
    // Une demande d'ajout a une licence existante (licenseId) : n'importe qui peut l'ecrire - a verifier.
    [[nodiscard]] bool joinRequest() const noexcept { return !licenseId.empty() && state != RequestState::Unreadable; }
};
// L'avertissement d'une demande d'ajout a une licence (vide : ce n'en est pas une) : << A verifier : ajout a la licence
// X demande par quelqu'un qui dit etre << Y >> - confirme avec ton client avant d'accepter. >>
[[nodiscard]] std::string joinWarning(const Entry& e);
[[nodiscard]] std::string joinWarning(const std::string& licenseId, const std::string& customer);
// Les demandes relevees (lues sur ce poste, sans reseau), les plus recentes d'abord.
[[nodiscard]] std::vector<Entry> listRequests(const std::string& folder);
[[nodiscard]] std::string requestsFolder(const std::string& folder);
// La demande en attente d'un poste : `device` = son identifiant (32 hexadecimaux, ou un debut d'au moins 6), ou
// "#<issue>". Faux : aucune, ou plusieurs postes. (Plusieurs demandes d'un poste : la plus recente par sa date signee.)
bool findWaiting(const std::string& folder, const std::string& device, Entry& out, std::string* why);
// Le chemin est-il une demande relevee (dans demandes-en-ligne/) ?
[[nodiscard]] bool isOnlineRequestPath(const std::string& folder, const std::string& path);

struct FetchState {
    Seconds     fetchedAt{0};
    int         total{0}, pending{0};
    std::string repo;               // le depot releve
    Seconds     lastUpdated{0};     // le plus grand updated_at vu sur GitHub (son horloge) : le releve suivant part de la
                                    // (moins une marge) ; 0 : le prochain releve lit tout
    bool        truncated{false};   // le dernier releve a ete coupe
    int         deferred{0};        // le dernier releve a remis du travail au suivant
};
[[nodiscard]] FetchState readFetchState(const std::string& folder);

// ============================================================================ le reseau (BLOQUANT)
struct Options {
    Seconds     now{0};
    std::string tool;    // "XPG-License-Admin 1.2.0" (rien d'autre : l'User-Agent)
    // publishLicence : joindre la liste de revocation de l'archive (scellee) a chaque PC, meme si elle ne nomme pas
    // cette licence (apres une revocation retiree : le PC doit recevoir la liste qui ne la revoque plus).
    bool        revocations{false};
};

// Le depot : public, le jeton de l'editeur y ecrit (permissions), les issues se lisent ; le jeton des demandes
// (s'il est enregistre) est accepte. Ne change rien.
[[nodiscard]] Result verify(const std::string& folder, const Options& o);
// passerelle.xpgpas (numero : max(celui du depot + 1, l'heure), signe), puis le registre de l'archive s'il manque
// au depot ou y est plus ancien ; une ancienne revocations.xpgrev publique est effacee.
[[nodiscard]] Result publishConfig(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// Le registre de l'archive, s'il manque au depot ou y est plus ancien ; une ancienne revocations.xpgrev publique
// est effacee (la liste part scellee dans l'etat des PC concernes).
[[nodiscard]] Result publishArchive(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// Relever les issues (les changees depuis le dernier releve ; le premier : toutes), ranger chaque demande nouvelle
// (une rejouee : ignoree), publier << recue >> (une fois) et << annulee >> (une annulation signee par le PC) - au plus
// kMaxAutoPublishPerFetch par releve, jamais sur un etat publie plus recent ou qui porte une licence.
[[nodiscard]] Result fetchRequests(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// Publier une licence signee (.xpglic : ses blocs, tels quels) a CHACUN de ses PC : << acceptee >> pour celui
// dont une demande attend (elle y repond : commentaire sans rien du client, issue fermee), << mise_a_jour >>
// pour les autres ; la licence scellee pour chaque PC (avec la liste de revocation de l'archive si elle nomme
// cette licence, ou si o.revocations). L'identifiant de la licence n'est jamais ecrit en clair.
[[nodiscard]] Result publishLicence(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const Options& o);
// Refuser la demande en attente de `device` (findWaiting) : << refusee >>, la raison scellee pour le PC ;
// commentaire << Refusee. >> (sans la raison) et issue fermee.
[[nodiscard]] Result refuse(const std::string& folder, const licensing::SigningKey& key, const std::string& device, const std::string& reason,
                            const Options& o);
// Apres une revocation : << revoquee >> pour chaque PC donne (cles, 64 hexadecimaux), la raison et la liste de
// revocation de l'archive (signee) scellees pour lui. Aucun PC : rien a publier (la liste partira avec la prochaine
// emission de la licence, qu'elle nomme).
[[nodiscard]] Result publishRevocation(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId,
                                       const std::vector<std::string>& deviceKeys, const std::string& reason, const Options& o);
// Apres le retrait d'un PC : << mise_a_jour >> (la nouvelle emission) pour les autres, << retiree >> pour lui
// (avec la liste de revocation si elle nomme la licence).
[[nodiscard]] Result publishRemoval(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText,
                                    const std::string& removedDeviceKey, const Options& o);

// ============================================================================ les fichiers des Studios
//  Ce que l'integrateur compile dans les Studios livres (rien de secret) :
//    PublisherKey.hpp       la cle PUBLIQUE de l'editeur (une cle d'ESSAI est refusee) -> src/licensing/
//    registre.xpgreg        le registre signe de l'archive (signe par CETTE cle)       -> ressources-licences/
//    GatewayAddress.hpp     l'adresse de la passerelle (vide : pas de passerelle)       -> src/licensing/
//    LISEZ-MOI.txt          quoi en faire
[[nodiscard]] std::string gatewayAddressHeader(const std::string& repo, const std::string& branch, Seconds now);
[[nodiscard]] Result studioKit(const std::string& folder, const licensing::SigningKey* key, const std::string& outDir, Seconds now);

} // namespace license_admin::passerelle
