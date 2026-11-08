// =============================================================================
//  license_admin/Mail.hpp - 1.37.0 : LE COURRIEL DE L'EDITEUR (SMTP), envoye
//  depuis SA PROPRE boite (Gmail, Outlook...) - l'outil de l'EDITEUR seulement,
//  jamais livre a un client
// -----------------------------------------------------------------------------
//  POURQUOI. Quand l'editeur accepte une demande de licence, le client recoit
//  un courriel tout de suite. Il part de la boite de l'editeur, par son
//  serveur SMTP, avec un << mot de passe d'application >> qu'il saisit une
//  fois (Gmail : compte Google > Securite > Validation en deux etapes > Mots
//  de passe des applications).
//
//  CE QUI EST GARDE (dans le dossier de la cle, comme service.json) :
//    mail.json      le serveur, le port, la securite, l'expediteur, l'identifiant
//                   (AUCUN secret) ; ecrit en prive (0600 hors Windows)
//    mail-mdp.bin   le mot de passe d'application, deploy::crypto::protect :
//                   DPAPI de l'utilisateur sous Windows (illisible pour un autre
//                   compte, un autre PC) ; ailleurs un fichier 0600 (la regle du
//                   projet, deploy/Crypto.hpp)
//
//  L'ENVOI (BLOQUANT : runSlow dans l'application). TCP (IPv4/IPv6), puis :
//    "tls"       TLS des la connexion (le port 465) ;
//    "starttls"  l'accueil 220, EHLO, STARTTLS EXIGE (sinon rien ne part), la
//                poignee de main sur la meme socket, EHLO de nouveau (le port 587).
//  JAMAIS EN CLAIR : aucune autre securite n'est acceptee ; l'identifiant et le
//  mot de passe ne partent qu'apres TLS.
//  TLS (mbedTLS, 1.2 ou 1.3) : le certificat du serveur est VERIFIE (autorite,
//  dates, nom du serveur - SNI et nom verifie). Les autorites : celles du
//  systeme - sous Windows le magasin << ROOT >> (et, si une autorite y manque,
//  la chaine verifiee par Windows lui-meme, qui sait la telecharger : voir
//  Mail.cpp) ; ailleurs SSL_CERT_FILE ou le paquet du systeme - plus
//  Config::caFile (les essais). Un certificat refuse arrete tout : il n'y a pas
//  de << continuer quand meme >>.
//  AUTH PLAIN si le serveur le propose, sinon LOGIN. Puis MAIL FROM, RCPT TO,
//  DATA : From et To (nom en mot encode RFC 2047 s'il n'est pas ASCII), Subject
//  (=?UTF-8?B?...?= s'il n'est pas ASCII), Date (RFC 5322), Message-ID, MIME
//  1.0, text/plain UTF-8 en base64 (lignes de 76) ; QUIT.
//  Chaque etape a son delai. Un echec rend UNE phrase claire, en francais
//  (tutoiement) : jamais le mot de passe, efface de la memoire apres usage.
// =============================================================================
#pragma once

#include <string>
#include <string_view>

namespace license_admin::mail {

inline constexpr const char* kConfigFile = "mail.json";
inline constexpr const char* kPasswordFile = "mail-mdp.bin";
inline constexpr int kDefaultTimeoutMs = 20000;

struct Config {
    std::string host;                 // "smtp.gmail.com"
    int         port{587};
    std::string security{"starttls"}; // "starttls" (port 587) ou "tls" (TLS des la connexion, port 465) ; rien d'autre (jamais en clair)
    std::string from;                 // l'adresse de l'expediteur (et l'en-tete From:)
    std::string fromName;             // facultatif : le nom montre ("Editeur XPG")
    std::string user;                 // l'identifiant SMTP (souvent = from)
    std::string caFile;               // facultatif : un fichier PEM d'autorites en plus (les essais) ; vide : celles du systeme
};

// Les chemins des deux fichiers, dans le dossier de la cle.
[[nodiscard]] std::string configPath(const std::string& folder);
[[nodiscard]] std::string passwordPath(const std::string& folder);

// Une configuration acceptable (hote, port 1 a 65535, securite tls|starttls, expediteur plausible, identifiant).
// Faux : `why` (une phrase en francais).
bool validate(const Config& c, std::string* why);
// Le port habituel d'une securite : 465 ("tls"), 587 ("starttls").
[[nodiscard]] int defaultPort(std::string_view security) noexcept;

[[nodiscard]] bool configured(const std::string& folder);                       // mail.json present et lisible
bool loadConfig(const std::string& folder, Config& out, std::string* why);
bool saveConfig(const std::string& folder, const Config& c, std::string* why);   // valide ; ecrit <dossier>/mail.json (sans secret)
bool savePassword(const std::string& folder, const std::string& password, std::string* why);   // <dossier>/mail-mdp.bin, protege
[[nodiscard]] bool hasPassword(const std::string& folder);
bool forget(const std::string& folder, std::string* why);                        // retire mail.json et mail-mdp.bin

struct Message {
    std::string to, toName, subject, body;   // UTF-8 ; le corps en texte simple, fins de ligne \n
};
struct SendResult {
    bool        ok{false};
    bool        reached{false};   // le serveur a repondu (quoi que ce soit)
    std::string message;          // UNE phrase claire en francais (tutoiement), jamais le mot de passe
};
// BLOQUANT. send : la configuration et le mot de passe du dossier ; sendWith : ceux donnes.
[[nodiscard]] SendResult send(const std::string& folder, const Message& m, int timeoutMs = kDefaultTimeoutMs);
[[nodiscard]] SendResult sendWith(const Config& c, const std::string& password, const Message& m, int timeoutMs = kDefaultTimeoutMs);

// Une adresse plausible : un seul @, une partie locale (64 au plus), un domaine a points (lettres, chiffres, -),
// ni espace ni controle ni <>",;\ ni caractere non ASCII (sans SMTPUTF8, il ne partirait pas), 254 au plus.
[[nodiscard]] bool plausibleAddress(std::string_view s);

} // namespace license_admin::mail
