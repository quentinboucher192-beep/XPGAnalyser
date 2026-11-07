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
//      demandes, masque), le registre et la liste de revocation de l'archive ;
//    - RELEVER LES DEMANDES : les issues ouvertes par les Studios, ouvertes avec
//      la cle de l'editeur, verifiees (la signature du PC), rangees dans le
//      dossier de la cle (demandes-en-ligne/ : le .xpgreq tel quel, et un
//      fichier .json a cote : le client, le message, l'etat) ; une demande
//      nouvelle recoit l'etat << recue >> (le PC le voit) ; une annulation signee
//      par le PC donne << annulee >> et ferme l'issue ;
//    - publier une licence signee a TOUS ses PC (chacun son etat, la licence
//      scellee pour lui), refuser une demande (la raison scellee), dire une
//      revocation, un PC retire ;
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
    [[nodiscard]] std::string text() const;    // message puis details
};

// ---- une demande en ligne relevee (demandes-en-ligne/issue-<n>.json, et <poste>-<nonce8>.xpgreq) ----
enum class RequestState : std::uint8_t {
    Pending,      // << en_attente >> : vue, l'etat << recue >> pas encore publie
    Received,     // << recue >> : le PC voit << Recue par l'editeur >> ; elle attend la decision
    Accepted,     // << acceptee >> : la licence est publiee pour ce PC, l'issue fermee
    Refused,      // << refusee >>
    Cancelled,    // << annulee >> : le PC l'a annulee (commentaire signe)
    Replaced,     // << remplacee >> : une demande plus recente du meme PC
                  //   (une issue fermee sans annulation signee reste en attente : un probleme est note)
    Unreadable,   // << illisible >> : scellee pour une autre cle, signature fausse...
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
    std::vector<std::string> problems;
    [[nodiscard]] bool waiting() const noexcept { return state == RequestState::Pending || state == RequestState::Received; }
};
// Les demandes relevees (lues sur ce poste, sans reseau), les plus recentes d'abord.
[[nodiscard]] std::vector<Entry> listRequests(const std::string& folder);
[[nodiscard]] std::string requestsFolder(const std::string& folder);
// La demande en attente d'un poste : `device` = son identifiant (32 hexadecimaux, ou un debut d'au moins 6), ou
// "#<issue>". Faux : aucune, ou plusieurs.
bool findWaiting(const std::string& folder, const std::string& device, Entry& out, std::string* why);
// Le chemin est-il une demande relevee (dans demandes-en-ligne/) ?
[[nodiscard]] bool isOnlineRequestPath(const std::string& folder, const std::string& path);

struct FetchState {
    Seconds fetchedAt{0};
    int     total{0}, pending{0};
};
[[nodiscard]] FetchState readFetchState(const std::string& folder);

// ============================================================================ le reseau (BLOQUANT)
struct Options {
    Seconds     now{0};
    std::string tool;    // "XPG-License-Admin 1.2.0" (rien d'autre : l'User-Agent)
};

// Le depot : public, le jeton de l'editeur y ecrit (permissions), les issues se lisent ; le jeton des demandes
// (s'il est enregistre) est accepte. Ne change rien.
[[nodiscard]] Result verify(const std::string& folder, const Options& o);
// passerelle.xpgpas (numero + 1, signe), puis le registre et la liste de revocation de l'archive s'ils manquent
// au depot ou y sont plus anciens.
[[nodiscard]] Result publishConfig(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// Le registre et la liste de revocation de l'archive, s'ils manquent au depot ou y sont plus anciens.
[[nodiscard]] Result publishArchive(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// Relever les issues (toutes, page par page), ranger chaque demande, publier << recue >> (une fois) et
// << annulee >> (une annulation signee par le PC).
[[nodiscard]] Result fetchRequests(const std::string& folder, const licensing::SigningKey& key, const Options& o);
// Publier une licence signee (.xpglic : ses blocs, tels quels) a CHACUN de ses PC : << acceptee >> pour celui
// dont une demande attend (elle y repond : commentaire sans rien du client, issue fermee), << mise_a_jour >>
// pour les autres ; la licence scellee pour chaque PC.
[[nodiscard]] Result publishLicence(const std::string& folder, const licensing::SigningKey& key, const std::string& licenceText, const Options& o);
// Refuser la demande en attente de `device` (findWaiting) : << refusee >>, la raison scellee pour le PC ;
// commentaire << Refusee. >> (sans la raison) et issue fermee.
[[nodiscard]] Result refuse(const std::string& folder, const licensing::SigningKey& key, const std::string& device, const std::string& reason,
                            const Options& o);
// Apres une revocation : la liste de l'archive, puis << revoquee >> pour chaque PC donne (cles, 64 hexadecimaux).
[[nodiscard]] Result publishRevocation(const std::string& folder, const licensing::SigningKey& key, const std::string& licenseId,
                                       const std::vector<std::string>& deviceKeys, const std::string& reason, const Options& o);
// Apres le retrait d'un PC : << mise_a_jour >> (la nouvelle emission) pour les autres, << retiree >> pour lui.
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
