// =============================================================================
//  license_admin/Mail.hpp - 1.37.0 : LE COURRIEL DE L'EDITEUR (SMTP), envoye
//  depuis SA PROPRE boite (Gmail, Outlook...) - l'outil de l'EDITEUR seulement,
//  jamais livre a un client
// -----------------------------------------------------------------------------
//  POURQUOI. Quand l'editeur accepte une demande de licence, le client recoit
//  un courriel tout de suite. Il part de la boite de l'editeur, par son
//  serveur SMTP, avec un << mot de passe d'application >> qu'il saisit une
//  fois (Gmail : compte Google > Securite > Validation en deux etapes > Mots
//  de passe des applications) - ou, 1.38.0, avec son COMPTE MICROSOFT (voir
//  plus bas : Outlook.com, Hotmail, Microsoft 365 n'acceptent plus de mot de
//  passe).
//
//  CE QUI EST GARDE (dans le dossier de la cle, comme service.json) :
//    mail.json      le serveur, le port, la securite, l'expediteur, l'identifiant
//                   (AUCUN secret) ; ecrit en prive (0600 hors Windows)
//    mail-mdp.bin   le mot de passe d'application, deploy::crypto::protect :
//                   DPAPI de l'utilisateur sous Windows (illisible pour un autre
//                   compte, un autre PC) ; ailleurs un fichier 0600 (la regle du
//                   projet, deploy/Crypto.hpp)
//    mail-microsoft.bin  1.38.0 : la connexion Microsoft (le jeton de
//                   rafraichissement, le compte, l'application et le locataire
//                   pour lesquels il a ete donne), protegee comme mail-mdp.bin
//
//  L'ENVOI (BLOQUANT : runSlow dans l'application). TCP (IPv4/IPv6), puis :
//    "tls"       TLS des la connexion (le port 465) ;
//    "starttls"  l'accueil 220, EHLO, STARTTLS EXIGE (sinon rien ne part), la
//                poignee de main sur la meme socket, EHLO de nouveau (le port 587).
//  JAMAIS EN CLAIR : aucune autre securite n'est acceptee ; l'identifiant et le
//  mot de passe (ou le jeton) ne partent qu'apres TLS.
//  TLS (mbedTLS, 1.2 ou 1.3) : le certificat du serveur est VERIFIE (autorite,
//  dates, nom du serveur - SNI et nom verifie). Les autorites : celles du
//  systeme - sous Windows le magasin << ROOT >> (et, si une autorite y manque,
//  la chaine verifiee par Windows lui-meme, qui sait la telecharger : voir
//  Mail.cpp) ; ailleurs SSL_CERT_FILE ou le paquet du systeme - plus
//  Config::caFile (les essais). Un certificat refuse arrete tout : il n'y a pas
//  de << continuer quand meme >>.
//  AUTH PLAIN si le serveur le propose, sinon LOGIN (auth "motdepasse") ; AUTH
//  XOAUTH2 (auth "microsoft"). Puis MAIL FROM, RCPT TO,
//  DATA : From et To (nom en mot encode RFC 2047 s'il n'est pas ASCII), Subject
//  (=?UTF-8?B?...?= s'il n'est pas ASCII), Date (RFC 5322), Message-ID, MIME
//  1.0, text/plain UTF-8 en base64 (lignes de 76) ; QUIT.
//  Chaque etape a son delai. Un echec rend UNE phrase claire, en francais
//  (tutoiement) : jamais le mot de passe, efface de la memoire apres usage.
//
//  1.38.0 : LE COMPTE MICROSOFT (OAuth 2.0). L'editeur inscrit UNE FOIS une
//  application dans le portail Microsoft Entra (client public : << Autoriser les
//  flux clients publics >> = Oui ; permission deleguee SMTP.Send d'Office 365
//  Exchange Online ; comptes de tout annuaire et comptes Microsoft personnels)
//  et colle son << ID d'application (client) >> (Config::clientId : un GUID, pas
//  un secret). Puis il se connecte par le FLUX A CODE (RFC 8628) :
//    microsoftStart  POST {autorite}/{locataire}/oauth2/v2.0/devicecode : une
//                    adresse et un code court a taper dans son navigateur ;
//    microsoftPoll   POST .../token (urn:ietf:params:oauth:grant-type:device_code),
//                    toutes les `interval` secondes, jusqu'a Done : le jeton de
//                    rafraichissement est garde (mail-microsoft.bin), avec le
//                    compte (preferred_username de l'id_token : les portees
//                    demandees sont SMTP.Send offline_access openid profile).
//  A CHAQUE ENVOI (send, auth "microsoft") : le jeton de rafraichissement donne
//  un jeton d'acces (et, souvent, un nouveau jeton de rafraichissement, garde a
//  la place de l'ancien), puis AUTH XOAUTH2 en deux temps (comme l'exemple de
//  Microsoft : << AUTH XOAUTH2 >>, 334, puis base64("user=" + identifiant +
//  "\x01auth=Bearer " + jeton + "\x01\x01")) ; un refus en 334 (le defi
//  d'erreur, RFC 7628) recoit une ligne vide, puis le 535 est explique.
//  L'identifiant SMTP (Config::user) est l'ADRESSE de la boite connectee.
//  HTTPS : le transport du systeme (app/ai/AiHttp : WinHTTP sous Windows,
//  libcurl chargee a la demande ailleurs), delais bornes, aucune redirection
//  suivie. JAMAIS un jeton, un code ni un mot de passe dans un message (les
//  textes de Microsoft et du serveur SMTP sont tus s'ils en contiennent un).
//
//  LIMITES : pas de SMTPUTF8 (une adresse non ASCII n'est pas plausible) ; la
//  recherche du nom du serveur (getaddrinfo) n'est pas bornee par le delai ;
//  le compte Microsoft : un jeton d'acces est demande a chaque envoi (aucun
//  n'est garde) ; les chaines lues du JSON de Microsoft (core::json) ne sont
//  pas effacees de la memoire (les tampons bruts, eux, le sont).
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace license_admin::mail {

inline constexpr const char* kConfigFile = "mail.json";
inline constexpr const char* kPasswordFile = "mail-mdp.bin";
inline constexpr const char* kMicrosoftFile = "mail-microsoft.bin";   // 1.38.0
inline constexpr int kDefaultTimeoutMs = 20000;

// 1.38.0 : l'identification (Config::auth)
inline constexpr const char* kAuthPassword = "motdepasse";   // un mot de passe d'application (AUTH PLAIN ou LOGIN)
inline constexpr const char* kAuthMicrosoft = "microsoft";   // un compte Microsoft (OAuth 2.0, AUTH XOAUTH2)
// 1.38.0 : Microsoft
inline constexpr const char* kMicrosoftAuthority = "https://login.microsoftonline.com";   // Config::authority vide
inline constexpr const char* kMicrosoftSmtpScope = "https://outlook.office.com/SMTP.Send";
inline constexpr const char* kOutlookHost = "smtp-mail.outlook.com";   // Outlook.com, Hotmail, Live (locataire consumers)
inline constexpr const char* kOffice365Host = "smtp.office365.com";    // Microsoft 365 (locataire organizations)

struct Config {
    std::string host;                 // "smtp.gmail.com"
    int         port{587};
    std::string security{"starttls"}; // "starttls" (port 587) ou "tls" (TLS des la connexion, port 465) ; rien d'autre (jamais en clair)
    std::string from;                 // l'adresse de l'expediteur (et l'en-tete From:)
    std::string fromName;             // facultatif : le nom montre ("Editeur XPG")
    std::string user;                 // l'identifiant SMTP (souvent = from ; "microsoft" : l'adresse de la boite connectee)
    std::string caFile;               // facultatif : un fichier PEM d'autorites en plus (les essais) ; vide : celles du systeme
    // 1.38.0 : l'identification (mail.json : ecrits seulement s'ils ne valent pas leur defaut - un mail.json de
    // 1.37 se relit et se reecrit a l'identique)
    std::string auth{"motdepasse"};   // "motdepasse" ou "microsoft"                       (mail.json : "authentification")
    std::string clientId;             // "microsoft" : l'ID d'application (client) du portail Entra, un GUID   ("id_application")
    std::string tenant{"common"};     // "common", "consumers" (Outlook.com, Hotmail), "organizations" (Microsoft 365),
                                      // ou le GUID ou le domaine d'un annuaire                          ("locataire")
    std::string authority;            // vide : https://login.microsoftonline.com ; https://NOM sans chemin ; http:// vers
                                      // 127.0.0.1 ou localhost seulement (LES ESSAIS)                     ("autorite")
};

// Les chemins des fichiers, dans le dossier de la cle.
[[nodiscard]] std::string configPath(const std::string& folder);
[[nodiscard]] std::string passwordPath(const std::string& folder);
[[nodiscard]] std::string microsoftPath(const std::string& folder);   // 1.38.0 : mail-microsoft.bin

// Une configuration acceptable (hote, port 1 a 65535, securite tls|starttls, expediteur plausible, identifiant ;
// 1.38.0 : l'identification motdepasse|microsoft, l'ID d'application (un GUID : exige pour "microsoft"), le
// locataire, l'autorite ; "microsoft" : l'identifiant est une adresse). Faux : `why` (une phrase en francais).
bool validate(const Config& c, std::string* why);
// Le port habituel d'une securite : 465 ("tls"), 587 ("starttls").
[[nodiscard]] int defaultPort(std::string_view security) noexcept;

// 1.38.0 : les reglages tout faits d'une boite Microsoft (pour l'interface) : `personal` - Outlook.com, Hotmail
// (smtp-mail.outlook.com, 587, STARTTLS, locataire consumers) ; sinon Microsoft 365 (smtp.office365.com, 587,
// STARTTLS, locataire organizations). auth = "microsoft" ; le reste de `base` est garde (l'expediteur, le nom,
// l'identifiant, l'ID d'application, les autorites, l'autorite).
[[nodiscard]] Config microsoftPreset(bool personal, Config base = {});
// 1.38.0 : un serveur SMTP de Microsoft (outlook.com, office365.com, hotmail.com, live.com et leurs sous-domaines) :
// il n'accepte plus de mot de passe.
[[nodiscard]] bool microsoftHost(std::string_view host) noexcept;

[[nodiscard]] bool configured(const std::string& folder);                       // mail.json present et lisible
bool loadConfig(const std::string& folder, Config& out, std::string* why);
bool saveConfig(const std::string& folder, const Config& c, std::string* why);   // valide ; ecrit <dossier>/mail.json (sans secret)
bool savePassword(const std::string& folder, const std::string& password, std::string* why);   // <dossier>/mail-mdp.bin, protege
[[nodiscard]] bool hasPassword(const std::string& folder);
bool forget(const std::string& folder, std::string* why);   // retire mail.json, mail-mdp.bin et (1.38.0) mail-microsoft.bin
// 1.38.0 : le dossier peut envoyer : mail.json lisible ET le secret de son identification (le mot de passe
// d'application, ou la connexion Microsoft).
[[nodiscard]] bool readyToSend(const std::string& folder);

// ============================================================================ 1.38.0 : le compte Microsoft
// Le flux a code (RFC 8628). BLOQUANTS (un fil de travail). Les phrases sont entieres (majuscule, point final),
// en francais, tutoiement ; aucune ne contient un jeton ni le code de l'appareil.
struct DeviceCode {
    std::string userCode;          // le code court a taper dans le navigateur ("ABCD1234") : a montrer
    std::string verificationUri;   // l'adresse ou le taper ("https://microsoft.com/devicelogin") : a montrer
    std::string message;           // la phrase de Microsoft (sa langue) : l'adresse et le code
    std::string deviceCode;        // SECRET : ni montre ni ecrit ; seulement rendu a microsoftPoll
    int         interval{5};       // secondes entre deux sondages (1 a 60)
    int         expiresIn{900};    // secondes de validite du code
};
// Commencer : un code pour cette application (c.clientId, c.tenant, c.authority ; le reste de `c` n'est pas lu).
// Faux : `why`.
bool microsoftStart(const Config& c, DeviceCode& out, std::string* why, int timeoutMs = kDefaultTimeoutMs);
// "Ouvre ADRESSE dans ton navigateur, tape le code CODE..." (notre phrase, a montrer avec ou sans dc.message).
[[nodiscard]] std::string microsoftInstructions(const DeviceCode& dc);

enum class PollState {
    Pending,     // pas encore : attendre waitSeconds, puis sonder de nouveau (authorization_pending, slow_down)
    Done,        // connecte : mail-microsoft.bin est ecrit
    Refused,     // l'editeur a refuse dans le navigateur (authorization_declined)
    Expired,     // le code a expire (expired_token) : recommencer par microsoftStart
    Error,       // tout le reste : `message` le dit
    Cancelled    // microsoftWait : `stop` a dit d'arreter
};
struct PollResult {
    PollState   state{PollState::Error};
    int         waitSeconds{0};   // Pending : l'attente avant le prochain sondage - A GARDER (dc.interval = waitSeconds) :
                                  // slow_down l'allonge de 5 s
    bool        slowDown{false};  // Pending : Microsoft a demande de ralentir
    std::string message;          // une phrase
    std::string account;          // Done : le compte connecte (preferred_username), s'il est connu
};
// UN sondage. Done : le jeton de rafraichissement est garde dans <dossier>/mail-microsoft.bin (protege), avec le
// compte, l'ID d'application, le locataire et l'autorite de `c`. `why` (facultatif) : le message, sauf Pending et Done.
PollResult microsoftPoll(const std::string& folder, const Config& c, const DeviceCode& dc, std::string* why,
                         int timeoutMs = kDefaultTimeoutMs);
// Sonder jusqu'au bout (attend dc.interval avant chaque sondage, suit slow_down, s'arrete a dc.expiresIn) : rend
// le premier resultat qui n'est pas Pending. `stop` (facultatif) est consulte toutes les 100 ms : vrai -> Cancelled.
PollResult microsoftWait(const std::string& folder, const Config& c, DeviceCode dc, const std::function<bool()>& stop = {},
                         int timeoutMs = kDefaultTimeoutMs);

// mail-microsoft.bin present (sans le relire).
[[nodiscard]] bool microsoftConnected(const std::string& folder);
// Ce que dit la connexion gardee (jamais le jeton). Faux : `why` (pas connecte, illisible...).
struct MicrosoftLink {
    std::string  account;     // le compte (preferred_username), peut etre vide
    std::string  clientId;    // l'application, le locataire et l'autorite de la connexion
    std::string  tenant;
    std::string  authority;
    std::int64_t since{0};    // l'heure de la connexion (secondes depuis 1970, UTC)
};
bool microsoftLink(const std::string& folder, MicrosoftLink& out, std::string* why);
// Oublier la seule connexion Microsoft (mail-microsoft.bin). Sans erreur s'il n'y en a pas.
bool microsoftForget(const std::string& folder, std::string* why);

struct Message {
    std::string to, toName, subject, body;   // UTF-8 ; le corps en texte simple, fins de ligne \n
};
struct SendResult {
    bool        ok{false};
    bool        reached{false};   // le serveur SMTP a repondu (quoi que ce soit)
    std::string message;          // UNE phrase claire en francais (tutoiement), jamais le mot de passe ni un jeton
};
// BLOQUANT. send : la configuration et le secret du dossier (le mot de passe ; 1.38.0, "microsoft" : un jeton
// d'acces obtenu par la connexion gardee, puis XOAUTH2) ; sendWith : la configuration et le mot de passe donnes
// ("motdepasse" seulement) ; sendWithToken (1.38.0) : AUTH XOAUTH2 avec ce jeton d'acces, quelle que soit c.auth.
[[nodiscard]] SendResult send(const std::string& folder, const Message& m, int timeoutMs = kDefaultTimeoutMs);
[[nodiscard]] SendResult sendWith(const Config& c, const std::string& password, const Message& m, int timeoutMs = kDefaultTimeoutMs);
[[nodiscard]] SendResult sendWithToken(const Config& c, const std::string& accessToken, const Message& m, int timeoutMs = kDefaultTimeoutMs);

// Une adresse plausible : un seul @, une partie locale (64 au plus), un domaine a points (lettres, chiffres, -),
// ni espace ni controle ni <>",;\ ni caractere non ASCII (sans SMTPUTF8, il ne partirait pas), 254 au plus.
[[nodiscard]] bool plausibleAddress(std::string_view s);

} // namespace license_admin::mail
