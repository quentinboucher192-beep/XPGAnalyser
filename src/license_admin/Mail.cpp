// =============================================================================
//  license_admin/Mail.cpp - 1.37.0 : le courriel de l'editeur par SMTP (voir
//  Mail.hpp). JAMAIS LIVRE A UN CLIENT.
// -----------------------------------------------------------------------------
//  FILS. mbedTLS est configure sans MBEDTLS_THREADING_C : comme deploy/Tls.cpp,
//  TOUT appel a mbedTLS (analyse des certificats compris) passe sous
//  deploy::tls::libraryLock() ; la socket est non bloquante (deploy::net) et
//  les attentes se font HORS du verrou. PSA : deploy::tls::ready() ; le
//  hasard : deploy::tls::randomCallback.
//
//  WINDOWS. Les autorites sont celles du magasin << ROOT >>. Windows n'y met
//  pas toutes celles qu'il reconnait : il en telecharge a la demande (mise a
//  jour automatique des racines). Si mbedTLS refuse le certificat pour la
//  SEULE raison << autorite inconnue >>, la chaine recue est confiee a
//  Windows (CertGetCertificateChain, puis la politique SSL avec le nom du
//  serveur) ; s'il la reconnait, sa racine est ajoutee et la connexion
//  recommence : c'est encore mbedTLS qui verifie tout (nom, dates, chaine).
//  Toute autre raison (nom, dates) : refuse, sans recours.
//
//  1.38.0 : LE COMPTE MICROSOFT (OAuth 2.0, voir Mail.hpp). Le HTTPS est celui
//  de app/ai/AiHttp (WinHTTP, libcurl) : un corps de formulaire, une reponse
//  JSON, des delais bornes. Les tampons qui portent un jeton (le corps envoye,
//  la reponse brute, la ligne XOAUTH2) sont effaces apres usage. La rotation du
//  jeton de rafraichissement (relire, rafraichir, reecrire) se fait sous un
//  verrou : deux envois en meme temps ne gardent pas un jeton perime.
// =============================================================================
#include "Mail.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef CERT_CHAIN_PARA_HAS_EXTRA_FIELDS
#    define CERT_CHAIN_PARA_HAS_EXTRA_FIELDS
#  endif
#  include <windows.h>
#  include <wincrypt.h>
#endif

#include "../app/ai/AiHttp.hpp"   // 1.38.0 : le HTTPS du systeme (OAuth 2.0 de Microsoft)
#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Net.hpp"
#include "../deploy/Paths.hpp"
#include "../deploy/Tls.hpp"

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace license_admin::mail {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace net = deploy::net;
namespace tls = deploy::tls;
using core::json::Value;
using Bytes = crypto::Bytes;
using Clock = std::chrono::steady_clock;

namespace {

constexpr const char* kConfigFormat = "xpg-courriel-editeur/1";
constexpr std::size_t kMaxReply = 64u * 1024u;          // une reponse SMTP (toutes ses lignes)
constexpr std::size_t kMaxCaFile = 4u * 1024u * 1024u;  // un fichier d'autorites
constexpr std::size_t kMaxBody = 4u * 1024u * 1024u;    // le texte d'un courriel
constexpr std::size_t kMaxName = 100;                   // un nom montre (octets)
// 1.38.0 : Microsoft
constexpr const char* kMicrosoftFormat = "xpg-courriel-microsoft/1";
constexpr std::size_t kMaxHttpReply = 256u * 1024u;     // une reponse de Microsoft
constexpr std::size_t kMaxToken = 16u * 1024u;          // un jeton, un code d'appareil
constexpr const char* kDeviceGrant = "urn:ietf:params:oauth:grant-type:device_code";
// La connexion demande aussi openid profile : l'id_token dit le compte (preferred_username). Le rafraichissement
// ne demande que l'envoi (et un nouveau jeton de rafraichissement).
constexpr const char* kSignInScope = "https://outlook.office.com/SMTP.Send offline_access openid profile";
constexpr const char* kRefreshScope = "https://outlook.office.com/SMTP.Send offline_access";
// Le chemin de l'interface pour se connecter (les phrases le citent).
constexpr const char* kSignInPath = "Service en ligne \xE2\x80\xBA Le mail \xE2\x80\xBA Se connecter";

// ---------------------------------------------------------------- petits outils
fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
bool isFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(P(path), ec);
}
bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}
std::string cite(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return std::string(s.substr(a, b - a));
}
std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool isControl(char ch) {
    const auto c = static_cast<unsigned char>(ch);
    return c < 0x20 || c == 0x7F;
}
bool hasControl(std::string_view s) { return std::any_of(s.begin(), s.end(), isControl); }
bool isAscii(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}
// Sur une seule ligne : chaque caractere de controle devient une espace (un en-tete ne se coupe jamais par la).
std::string oneLine(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (isControl(c)) c = ' ';
    return trimmed(out);
}
// Couper a `max` octets sans couper un caractere UTF-8.
std::string clipUtf8(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
    return s;
}
void wipe(std::string& s) {
    if (!s.empty()) crypto::wipe(s.data(), s.size());
    s.clear();
}
void wipe(Bytes& b) {
    if (!b.empty()) crypto::wipe(b.data(), b.size());
    b.clear();
}
bool writePrivate(const std::string& path, const Bytes& bytes, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    return crypto::writePrivateFile(path, bytes, why);
}
// "20 s", "1,5 s"
std::string secondsText(int ms) {
    if (ms % 1000 == 0) return std::to_string(ms / 1000) + " s";
    return std::to_string(ms / 1000) + "," + std::to_string((ms % 1000) / 100) + " s";
}

// Un nom d'hote : des etiquettes (lettres, chiffres, -, _) separees par des points, ou une adresse IPv6 (au moins
// deux :). Un seul : est un << hote:port >> (le port se regle a part).
bool validHost(std::string_view h) {
    if (h.empty() || h.size() > 253) return false;
    if (std::count(h.begin(), h.end(), ':') >= 2)
        return std::all_of(h.begin(), h.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.'; });
    std::size_t label = 0;
    for (char c : h) {
        if (c == '.') {
            if (label == 0) return false;
            label = 0;
            continue;
        }
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
        if (++label > 63) return false;
    }
    return label > 0;
}

Config normalized(const Config& in) {
    Config c;
    c.host = lower(trimmed(in.host));
    while (!c.host.empty() && c.host.back() == '.') c.host.pop_back();
    c.port = in.port;
    c.security = lower(trimmed(in.security));
    c.from = trimmed(in.from);
    c.fromName = clipUtf8(oneLine(in.fromName), kMaxName);
    c.user = trimmed(in.user);
    c.caFile = trimmed(in.caFile);
    // 1.38.0 : l'identification ; un vide vaut le defaut (un mail.json de 1.37 n'a aucune de ces cles)
    c.auth = lower(trimmed(in.auth));
    if (c.auth.empty()) c.auth = kAuthPassword;
    c.clientId = lower(trimmed(in.clientId));
    c.tenant = lower(trimmed(in.tenant));
    if (c.tenant.empty()) c.tenant = "common";
    c.authority = lower(trimmed(in.authority));
    while (!c.authority.empty() && c.authority.back() == '/') c.authority.pop_back();
    return c;
}

// ---------------------------------------------------------------- 1.38.0 : les reglages de Microsoft
// Un GUID : 8-4-4-4-12 chiffres hexadecimaux (sans accolades).
bool isGuid(std::string_view s) {
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? s[i] != '-' : !std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}
// Un nom de domaine : au moins deux etiquettes (lettres, chiffres, -), aucune vide ni bordee d'un -.
bool isDomain(std::string_view d) {
    if (d.empty() || d.size() > 253) return false;
    std::size_t labels = 0, start = 0;
    for (std::size_t i = 0; i <= d.size(); ++i) {
        if (i < d.size() && d[i] != '.') {
            if (!(std::isalnum(static_cast<unsigned char>(d[i])) || d[i] == '-')) return false;
            continue;
        }
        const std::string_view label = d.substr(start, i - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        ++labels;
        start = i + 1;
    }
    return labels >= 2;
}
bool validTenant(std::string_view t) {
    return t == "common" || t == "consumers" || t == "organizations" || isGuid(t) || isDomain(t);
}
// L'autorite (normalisee) : vide, ou https://NOM[:PORT] sans chemin, ni requete, ni identifiant ; http:// vers
// 127.0.0.1 ou localhost seulement (les essais : le faux serveur de tests/outils/oauth_essai.py).
bool validAuthority(const std::string& a) {
    if (a.empty()) return true;
    if (hasControl(a) || !isAscii(a) || a.find_first_of(" ?#") != std::string::npos) return false;
    app::ai::Url u;
    if (!app::ai::parseUrl(a, u, nullptr)) return false;
    if (u.hasPath || u.path != "/") return false;
    if (!u.https && u.host != "127.0.0.1" && u.host != "localhost") return false;
    return true;
}
std::string authorityOf(const Config& c) { return c.authority.empty() ? std::string(kMicrosoftAuthority) : c.authority; }
// {autorite}/{locataire}/oauth2/v2.0/{point} ("devicecode", "token")
std::string endpoint(const Config& c, const char* point) { return authorityOf(c) + "/" + c.tenant + "/oauth2/v2.0/" + point; }
// Le nom de l'hote de l'autorite (les phrases) : "login.microsoftonline.com".
std::string authorityHost(const Config& c) {
    app::ai::Url u;
    return app::ai::parseUrl(authorityOf(c), u, nullptr) ? u.host : std::string("login.microsoftonline.com");
}

// L'ID d'application (exige si `requireId`, sinon un GUID s'il est donne), le locataire, l'autorite (normalises).
bool checkMicrosoft(const Config& c, bool requireId, std::string* why) {
    if (c.clientId.empty()) {
        if (requireId)
            return fail(why, "l'ID d'application Microsoft est vide : colle l'\xC2\xAB ID d'application (client) \xC2\xBB de ton application "
                             "(portail Microsoft Entra \xE2\x80\xBA Inscriptions d'applications \xE2\x80\xBA ton application \xE2\x80\xBA Vue d'ensemble)");
    } else if (!isGuid(c.clientId)) {
        return fail(why, "l'ID d'application Microsoft " + cite(clipUtf8(oneLine(c.clientId), 60)) +
                             " n'est pas valable (un GUID : 32 chiffres hexad\xC3\xA9" "cimaux en 5 groupes, 8-4-4-4-12, sans accolades)");
    }
    if (!validTenant(c.tenant))
        return fail(why, "le locataire Microsoft " + cite(clipUtf8(oneLine(c.tenant), 80)) +
                             " est refus\xC3\xA9 : \xC2\xAB consumers \xC2\xBB (Outlook.com, Hotmail), \xC2\xAB organizations \xC2\xBB (Microsoft 365), \xC2\xAB common \xC2\xBB (les deux), "
                             "ou le GUID ou le domaine de ton annuaire");
    if (!validAuthority(c.authority))
        return fail(why, "l'autorit\xC3\xA9 Microsoft " + cite(clipUtf8(oneLine(c.authority), 120)) +
                             " est refus\xC3\xA9" "e : laisse-la vide (https://login.microsoftonline.com), ou https://NOM sans chemin ; "
                             "http:// seulement vers 127.0.0.1 ou localhost (les essais)");
    return true;
}

// ---------------------------------------------------------------- l'en-tete du message
// Des mots encodes RFC 2047 (=?UTF-8?B?...?=), 39 octets au plus chacun (une ligne pliee reste sous 78
// caracteres), sans couper un caractere UTF-8 ; plies par CRLF + espace.
std::string encodedWords(std::string_view utf8) {
    std::string out;
    std::size_t i = 0;
    while (i < utf8.size()) {
        std::size_t n = std::min<std::size_t>(39, utf8.size() - i);
        while (n > 0 && i + n < utf8.size() && (static_cast<unsigned char>(utf8[i + n]) & 0xC0) == 0x80) --n;
        if (n == 0) n = std::min<std::size_t>(39, utf8.size() - i);   // pas de l'UTF-8 : coupe quand meme
        if (!out.empty()) out += "\r\n ";
        out += "=?UTF-8?B?" + crypto::base64(utf8.data() + i, n) + "?=";
        i += n;
    }
    return out;
}
// Un texte d'en-tete (le sujet) : tel quel s'il est ASCII et court, sinon des mots encodes.
std::string headerText(std::string_view s) {
    if (isAscii(s) && s.find("=?") == std::string_view::npos && s.size() <= 900) return std::string(s);
    return encodedWords(s);
}
// "Nom" <adresse>, ou =?UTF-8?B?...?= <adresse> pour un nom non ASCII, ou l'adresse seule.
std::string mailbox(std::string_view name, std::string_view address) {
    const std::string n = clipUtf8(oneLine(name), kMaxName);
    if (n.empty()) return std::string(address);
    if (!isAscii(n) || n.find("=?") != std::string::npos) return encodedWords(n) + " <" + std::string(address) + ">";
    std::string q = "\"";
    for (char c : n) {
        if (c == '"' || c == '\\') q += '\\';
        q += c;
    }
    return q + "\" <" + std::string(address) + ">";
}
// La date RFC 5322, en temps universel : "Sun, 08 Nov 2026 14:03:09 +0000".
std::string mailDate(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s, %02d %s %04d %02d:%02d:%02d +0000", kDays[(tm.tm_wday % 7 + 7) % 7], tm.tm_mday,
                  kMonths[(tm.tm_mon % 12 + 12) % 12], tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}
std::string domainOf(std::string_view address) {
    const auto at = address.rfind('@');
    return at == std::string_view::npos ? std::string("localhost") : std::string(address.substr(at + 1));
}
// Le texte en forme canonique (CRLF), en base64, lignes de 76.
std::string bodyBase64(std::string_view body) {
    std::string crlf;
    crlf.reserve(body.size() + body.size() / 32 + 2);
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '\r') {
            if (i + 1 < body.size() && body[i + 1] == '\n') ++i;
            crlf += "\r\n";
        } else if (c == '\n') {
            crlf += "\r\n";
        } else {
            crlf += c;
        }
    }
    const std::string b64 = crypto::base64(crlf.data(), crlf.size());
    std::string out;
    out.reserve(b64.size() + b64.size() / 76 * 2 + 2);
    for (std::size_t i = 0; i < b64.size(); i += 76) {
        out.append(b64, i, 76);
        out += "\r\n";
    }
    return out;
}
std::string composeMessage(const Config& c, const Message& m) {
    std::uint8_t id[16];
    crypto::randomBytes(id, sizeof id);
    std::string s;
    s += "From: " + mailbox(c.fromName, c.from) + "\r\n";
    s += "To: " + mailbox(m.toName, trimmed(m.to)) + "\r\n";
    s += "Subject: " + headerText(oneLine(m.subject)) + "\r\n";
    s += "Date: " + mailDate(std::time(nullptr)) + "\r\n";
    s += "Message-ID: <" + crypto::hex(id, sizeof id) + ".xpg@" + domainOf(c.from) + ">\r\n";
    s += "MIME-Version: 1.0\r\n";
    s += "Content-Type: text/plain; charset=UTF-8\r\n";
    s += "Content-Transfer-Encoding: base64\r\n";
    s += "\r\n";
    s += bodyBase64(m.body);
    return s;
}
// Le message pour DATA : une ligne qui commence par un point en recoit un second (aucune ne le fait ici : le
// base64 et les en-tetes n'en ont pas - la regle est appliquee quand meme), puis la ligne finale ".".
std::string dotStuffed(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    bool lineStart = true;
    for (char c : text) {
        if (lineStart && c == '.') out += '.';
        out += c;
        lineStart = (c == '\n');
    }
    if (!out.empty() && out.back() != '\n') out += "\r\n";
    out += ".\r\n";
    return out;
}
// Le nom de ce poste pour EHLO : lettres, chiffres, - et . seulement (sinon "xpg-license-admin").
std::string ehloName() {
    std::string n;
    for (char c : lower(net::hostName()))
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.') n += c;
    while (!n.empty() && (n.front() == '.' || n.front() == '-')) n.erase(n.begin());
    while (!n.empty() && (n.back() == '.' || n.back() == '-')) n.pop_back();
    return n.empty() || n == "pc" ? std::string("xpg-license-admin") : n;
}

// ---------------------------------------------------------------- les autorites
// Hors Windows : le paquet d'autorites du systeme (PEM, termine par un zero pour mbedTLS), lu HORS du verrou ;
// SSL_CERT_FILE d'abord, puis les emplacements habituels. Vide : aucun.
Bytes systemBundle() {
#if defined(_WIN32)
    return {};
#else
    std::vector<std::string> candidates;
    if (const char* f = std::getenv("SSL_CERT_FILE"); f && *f) candidates.emplace_back(f);
    candidates.emplace_back("/etc/ssl/certs/ca-certificates.crt");   // Debian, Ubuntu, Arch
    candidates.emplace_back("/etc/pki/tls/certs/ca-bundle.crt");     // Fedora, RHEL
    candidates.emplace_back("/etc/ssl/cert.pem");                    // macOS, Alpine
    for (const auto& path : candidates) {
        Bytes pem;
        if (isFile(path) && crypto::readFile(path, pem, nullptr, 16u * 1024u * 1024u) && !pem.empty()) {
            pem.push_back(0);
            return pem;
        }
    }
    return {};
#endif
}

int countCerts(const mbedtls_x509_crt& chain) {
    int n = 0;
    for (const mbedtls_x509_crt* p = &chain; p && p->raw.p; p = p->next) ++n;
    return n;
}

// Les autorites du systeme dans `roots`, SOUS le verrou de mbedTLS (`bundle` : systemBundle()). Rend le nombre de
// certificats ajoutes ; une autorite que mbedTLS refuse (un algorithme inconnu...) est passee.
int loadSystemRoots(mbedtls_x509_crt& roots, const Bytes& bundle) {
    const int before = countCerts(roots);
#if defined(_WIN32)
    (void)bundle;
    HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
    if (store) {
        PCCERT_CONTEXT ctx = nullptr;
        while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
            if ((ctx->dwCertEncodingType & X509_ASN_ENCODING) == 0) continue;
            (void)mbedtls_x509_crt_parse_der(&roots, ctx->pbCertEncoded, ctx->cbCertEncoded);
        }
        CertCloseStore(store, 0);
    }
#else
    if (!bundle.empty()) (void)mbedtls_x509_crt_parse(&roots, bundle.data(), bundle.size());   // > 0 : certains passes
#endif
    return countCerts(roots) - before;
}

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
// La chaine recue (le certificat du serveur d'abord) verifiee par Windows pour un serveur TLS de ce nom (la
// politique SSL : autorite, dates, nom) ; vrai : `root` = la racine que Windows reconnait (il a pu la telecharger).
bool windowsRootFor(const std::vector<Bytes>& chain, const std::string& host, int timeoutMs, Bytes& root) {
    if (chain.empty()) return false;
    HCERTSTORE mem = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!mem) return false;
    PCCERT_CONTEXT leaf = nullptr;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        PCCERT_CONTEXT added = nullptr;
        if (!CertAddEncodedCertificateToStore(mem, X509_ASN_ENCODING, chain[i].data(), static_cast<DWORD>(chain[i].size()),
                                              CERT_STORE_ADD_ALWAYS, i == 0 ? &added : nullptr))
            continue;
        if (i == 0) leaf = added;
    }
    bool ok = false;
    if (leaf) {
        char serverAuth[] = szOID_PKIX_KP_SERVER_AUTH;
        LPSTR usages[] = {serverAuth};
        CERT_CHAIN_PARA para;
        std::memset(&para, 0, sizeof para);
        para.cbSize = sizeof para;
        para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        para.RequestedUsage.Usage.cUsageIdentifier = 1;
        para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
        para.dwUrlRetrievalTimeout = static_cast<DWORD>(std::max(1000, timeoutMs));
        PCCERT_CHAIN_CONTEXT chainCtx = nullptr;
        if (CertGetCertificateChain(nullptr, leaf, nullptr, mem, &para, 0, nullptr, &chainCtx) && chainCtx) {
            std::wstring name = widen(host);
            SSL_EXTRA_CERT_CHAIN_POLICY_PARA extra;
            std::memset(&extra, 0, sizeof extra);
            extra.cbStruct = sizeof extra;
            extra.dwAuthType = AUTHTYPE_SERVER;
            extra.fdwChecks = 0;
            extra.pwszServerName = name.data();
            CERT_CHAIN_POLICY_PARA policy;
            std::memset(&policy, 0, sizeof policy);
            policy.cbSize = sizeof policy;
            policy.pvExtraPolicyPara = &extra;
            CERT_CHAIN_POLICY_STATUS status;
            std::memset(&status, 0, sizeof status);
            status.cbSize = sizeof status;
            if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chainCtx, &policy, &status) && status.dwError == 0 &&
                chainCtx->cChain > 0 && chainCtx->rgpChain[0]->cElement > 0) {
                const CERT_SIMPLE_CHAIN* simple = chainCtx->rgpChain[0];
                const CERT_CONTEXT* top = simple->rgpElement[simple->cElement - 1]->pCertContext;
                if (top && top->pbCertEncoded && top->cbCertEncoded > 0) {
                    root.assign(top->pbCertEncoded, top->pbCertEncoded + top->cbCertEncoded);
                    ok = true;
                }
            }
            CertFreeCertificateChain(chainCtx);
        }
        CertFreeCertificateContext(leaf);
    }
    CertCloseStore(mem, 0);
    return ok;
}
#endif

int remainingMs(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<int>(std::min<long long>(left, 1000000000LL));
}

// ---------------------------------------------------------------- la liaison (TCP, puis TLS)
enum class Io { Ok, Timeout, Closed, Error };

struct Reply {
    int                      code{0};
    std::vector<std::string> lines;
    // "535 5.7.8 Username and Password not accepted..." (sans controle, 200 caracteres au plus)
    [[nodiscard]] std::string text() const {
        std::string t = std::to_string(code);
        for (const auto& l : lines) {
            const std::string c = oneLine(l);
            if (!c.empty()) t += " " + c;
        }
        return clipUtf8(t, 200);
    }
    // Le mot-cle d'une ligne de capacite (EHLO) : "STARTTLS", "AUTH" (et la suite dans `rest`).
    [[nodiscard]] bool capability(std::string_view keyword, std::string* rest = nullptr) const {
        for (const auto& l : lines) {
            std::string up = l;
            for (auto& c : up)
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (up.compare(0, keyword.size(), keyword) != 0) continue;
            if (up.size() > keyword.size() && up[keyword.size()] != ' ' && up[keyword.size()] != '=') continue;
            if (rest) *rest = up.size() > keyword.size() ? up.substr(keyword.size() + 1) : std::string();
            return true;
        }
        return false;
    }
};

class Link {
public:
    static constexpr int kTimeout = -0x7FFF0001;

    Link() {
        std::lock_guard<std::mutex> lock(tls::libraryLock());
        mbedtls_ssl_config_init(&conf_);
        mbedtls_ssl_init(&ssl_);
        mbedtls_x509_crt_init(&roots_);
    }
    ~Link() {
        close();
        std::lock_guard<std::mutex> lock(tls::libraryLock());
        mbedtls_ssl_free(&ssl_);
        mbedtls_ssl_config_free(&conf_);
        mbedtls_x509_crt_free(&roots_);
    }
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    bool connect(const std::string& host, int port, int timeoutMs, std::string* why) { return sock_.connect(host, port, timeoutMs, why); }

    [[nodiscard]] bool secure() const noexcept { return tlsUp_; }
    [[nodiscard]] bool reached() const noexcept { return received_ > 0; }
    [[nodiscard]] std::uint32_t verifyFlags() const noexcept { return flags_; }
    [[nodiscard]] const std::vector<Bytes>& peerChain() const noexcept { return chain_; }
    // Les premiers octets recus (avant TLS) : "220 " si le serveur parle en clair.
    [[nodiscard]] const std::string& head() const noexcept { return head_; }
    // Ce qui serait arrive en clair avant la poignee de main ne compte pas (l'injection apres STARTTLS).
    void discardInput() noexcept { in_.clear(); }

    // Les autorites : celles du systeme, celles du fichier `caFile`, et `extra` (DER). Faux : `why`.
    bool loadRoots(const std::string& caFile, const std::vector<Bytes>& extra, std::string* why) {
        Bytes pem;
        if (!caFile.empty()) {
            std::string w;
            if (!crypto::readFile(caFile, pem, &w, kMaxCaFile) || pem.empty())
                return fail(why, "le fichier d'autorit\xC3\xA9s " + cite(caFile) + " est illisible");
            pem.push_back(0);
        }
        const Bytes bundle = systemBundle();
        std::lock_guard<std::mutex> lock(tls::libraryLock());
        int count = loadSystemRoots(roots_, bundle);
        if (!pem.empty()) {
            const int before = countCerts(roots_);
            const int rc = mbedtls_x509_crt_parse(&roots_, pem.data(), pem.size());
            const int added = countCerts(roots_) - before;
            if (rc < 0 || added == 0)
                return fail(why, "le fichier d'autorit\xC3\xA9s " + cite(caFile) + " ne contient aucun certificat lisible");
            count += added;
        }
        for (const auto& der : extra)
            if (mbedtls_x509_crt_parse_der(&roots_, der.data(), der.size()) == 0) ++count;
        if (count == 0) return fail(why, "aucune autorit\xC3\xA9 de certification n'est disponible sur ce poste");
        return true;
    }

    // La poignee de main TLS (client, certificat verifie, nom `host`). 0 : reussie ; sinon le code de mbedTLS, ou
    // kTimeout ; `setupWhy` : une panne avant la poignee de main (rend -1).
    int handshake(const std::string& host, int timeoutMs, std::string* setupWhy) {
        if (!tls::ready(setupWhy)) return -1;
        {
            std::lock_guard<std::mutex> lock(tls::libraryLock());
            int rc = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
            if (rc == 0) {
                mbedtls_ssl_conf_min_tls_version(&conf_, MBEDTLS_SSL_VERSION_TLS1_2);
                mbedtls_ssl_conf_max_tls_version(&conf_, MBEDTLS_SSL_VERSION_TLS1_3);
                mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
                mbedtls_ssl_conf_ca_chain(&conf_, &roots_, nullptr);
                mbedtls_ssl_conf_rng(&conf_, tls::randomCallback, nullptr);
                rc = mbedtls_ssl_setup(&ssl_, &conf_);
            }
            if (rc == 0) rc = mbedtls_ssl_set_hostname(&ssl_, host.c_str());   // SNI et le nom verifie
            if (rc != 0) {
                if (setupWhy) *setupWhy = "TLS : " + tls::errorText(rc);
                return -1;
            }
            mbedtls_ssl_set_verify(&ssl_, onVerify, this);
            mbedtls_ssl_set_bio(&ssl_, this, bioSend, bioRecv, nullptr);
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
        const int rc = drive([this] { return mbedtls_ssl_handshake(&ssl_); }, deadline);
        if (rc != 0) {
            // Les raisons du refus d'un certificat (celles du rappel, et le jugement final) ; jamais pour une autre panne.
            if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || rc == MBEDTLS_ERR_SSL_BAD_CERTIFICATE) {
                std::lock_guard<std::mutex> lock(tls::libraryLock());
                const std::uint32_t r = mbedtls_ssl_get_verify_result(&ssl_);
                if (r != 0xFFFFFFFFu) flags_ |= r;
            }
            return rc;
        }
        tlsUp_ = true;
        return 0;
    }

    // Ecrire tout, en `timeoutMs` au plus.
    Io send(std::string_view data, int timeoutMs) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
        std::size_t done = 0;
        while (done < data.size()) {
            if (tlsUp_) {
                const auto* p = reinterpret_cast<const unsigned char*>(data.data()) + done;
                const std::size_t n = data.size() - done;
                const int rc = drive([&] { return mbedtls_ssl_write(&ssl_, p, n); }, deadline);
                if (rc > 0) {
                    done += static_cast<std::size_t>(rc);
                    continue;
                }
                return rc == kTimeout ? Io::Timeout : Io::Closed;
            }
            const long n = sock_.sendSome(data.data() + done, data.size() - done);
            if (n > 0) {
                done += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0) return Io::Closed;
            const int left = remainingMs(deadline);
            if (left <= 0) return Io::Timeout;
            if (sock_.wait(true, left) < 0) return Io::Closed;
        }
        return Io::Ok;
    }

    // Une reponse SMTP entiere ("250-..." puis "250 ..."), en `timeoutMs` au plus.
    Io reply(Reply& r, int timeoutMs) {
        r = Reply{};
        const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(1, timeoutMs));
        std::size_t total = 0;
        for (;;) {
            const auto eol = in_.find('\n');
            if (eol == std::string::npos) {
                if (in_.size() > kMaxReply) return Io::Error;
                const Io io = fill(deadline);
                if (io != Io::Ok) return io;
                continue;
            }
            std::string line = in_.substr(0, eol);
            in_.erase(0, eol + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            total += line.size();
            if (line.size() < 3 || total > kMaxReply || !std::isdigit(static_cast<unsigned char>(line[0])) ||
                !std::isdigit(static_cast<unsigned char>(line[1])) || !std::isdigit(static_cast<unsigned char>(line[2])))
                return Io::Error;
            if (line.size() > 3 && line[3] != ' ' && line[3] != '-') return Io::Error;
            r.code = std::atoi(line.substr(0, 3).c_str());
            r.lines.push_back(line.size() > 4 ? line.substr(4) : std::string());
            if (line.size() <= 3 || line[3] == ' ') return Io::Ok;
            if (r.lines.size() > 500) return Io::Error;
        }
    }

    void close() noexcept {
        if (tlsUp_ && sock_.valid()) {
            std::lock_guard<std::mutex> lock(tls::libraryLock());
            (void)mbedtls_ssl_close_notify(&ssl_);   // une fois, sans attendre
        }
        tlsUp_ = false;
        sock_.close();
    }

private:
    static int bioSend(void* ctx, const unsigned char* buf, std::size_t len) {
        const long n = static_cast<Link*>(ctx)->sock_.sendSome(buf, len);
        if (n > 0) return static_cast<int>(n);
        if (n == 0) return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    static int bioRecv(void* ctx, unsigned char* buf, std::size_t len) {
        auto* self = static_cast<Link*>(ctx);
        const long n = self->sock_.receiveSome(buf, len);
        if (n > 0) {
            self->noteReceived(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
            return static_cast<int>(n);
        }
        if (n == 0) return 0;   // fin : l'autre bout a ferme
        if (n == -2) return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    // Appele pour chaque certificat de la chaine (sous le verrou, pendant la poignee de main) : les raisons sont
    // gardees (le message), et la chaine recue (DER, le certificat du serveur d'abord) pour Windows. Ne change rien
    // au jugement de mbedTLS.
    static int onVerify(void* ctx, mbedtls_x509_crt* crt, int depth, std::uint32_t* flags) {
        auto* self = static_cast<Link*>(ctx);
        self->flags_ |= *flags;
        if (depth == 0) {
            self->chain_.clear();
            for (const mbedtls_x509_crt* c = crt; c && c->raw.p && self->chain_.size() < 10; c = c->next)
                self->chain_.emplace_back(c->raw.p, c->raw.p + c->raw.len);
        }
        return 0;
    }
    void noteReceived(const char* data, std::size_t n) {
        if (received_ < 4) head_.append(data, std::min<std::size_t>(n, 4 - head_.size()));
        received_ += n;
    }

    // Un pas de mbedTLS sous le verrou, puis l'attente (hors du verrou) tant qu'il le demande (comme deploy/Tls).
    int drive(const std::function<int()>& step, Clock::time_point deadline) {
        for (;;) {
            int rc;
            {
                std::lock_guard<std::mutex> lock(tls::libraryLock());
                rc = step();
            }
            if (rc >= 0) return rc;
            if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;   // sans usage ici
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) return rc;
            const int left = remainingMs(deadline);
            if (left <= 0) return kTimeout;
            const int w = sock_.wait(rc == MBEDTLS_ERR_SSL_WANT_WRITE, left);
            if (w < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
        }
    }

    // Lire ce qui arrive (au moins un octet) dans in_.
    Io fill(Clock::time_point deadline) {
        char buf[4096];
        if (tlsUp_) {
            const int rc = drive([&] { return mbedtls_ssl_read(&ssl_, reinterpret_cast<unsigned char*>(buf), sizeof buf); }, deadline);
            if (rc > 0) {
                in_.append(buf, static_cast<std::size_t>(rc));
                return Io::Ok;
            }
            return rc == kTimeout ? Io::Timeout : Io::Closed;
        }
        for (;;) {
            const long n = sock_.receiveSome(buf, sizeof buf);
            if (n > 0) {
                noteReceived(buf, static_cast<std::size_t>(n));
                in_.append(buf, static_cast<std::size_t>(n));
                return Io::Ok;
            }
            if (n == 0 || n == -1) return Io::Closed;
            const int left = remainingMs(deadline);
            if (left <= 0) return Io::Timeout;
            if (sock_.wait(false, left) < 0) return Io::Closed;
        }
    }

    net::TcpSocket      sock_;
    mbedtls_ssl_config  conf_;
    mbedtls_ssl_context ssl_;
    mbedtls_x509_crt    roots_;
    bool                tlsUp_{false};
    std::string         in_;
    std::size_t         received_{0};
    std::string         head_;
    std::uint32_t       flags_{0};
    std::vector<Bytes>  chain_;
};

// ---------------------------------------------------------------- une tentative d'envoi
struct Attempt {
    SendResult         result;
    bool               untrustedOnly{false};   // refuse pour la seule raison << autorite inconnue >>
    std::vector<Bytes> chain;                  // la chaine recue alors (DER)
};

class Session {
public:
    // 1.38.0 : `oauth` - AUTH XOAUTH2 avec un jeton d'acces (le secret) ; `account` : le compte Microsoft connecte
    // (les phrases d'un refus le citent), vide s'il n'est pas connu.
    Session(const Config& c, int timeoutMs, bool oauth = false, std::string account = {})
        : c_(c), t_(timeoutMs), oauth_(oauth), account_(std::move(account)) {
        secrets_.reserve(4);   // sans reallocation : aucune copie d'un secret laissee en memoire
    }
    ~Session() {
        for (auto& s : secrets_) wipe(s);
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // `secret` : le mot de passe d'application, ou (oauth) le jeton d'acces.
    Attempt run(const std::string& secret, const std::string& data, const std::string& to, const std::vector<Bytes>& extraRoots) {
        for (auto& s : secrets_) wipe(s);
        secrets_.clear();
        if (!secret.empty()) {
            secrets_.push_back(secret);
            secrets_.push_back(crypto::base64(secret));
        }
        Attempt a;
        a.result = steps(secret, data, to, extraRoots, a);
        a.result.reached = link_.reached();
        link_.close();
        return a;
    }

private:
    SendResult failure(std::string text) const {
        SendResult r;
        r.ok = false;
        r.reached = link_.reached();
        r.message = std::move(text);
        return r;
    }
    std::string where() const { return c_.host; }

    // Une panne de lecture ou d'ecriture a l'etape `step`.
    SendResult ioFailure(Io io, const char* step) const {
        switch (io) {
            case Io::Timeout:
                if (!link_.reached())
                    return failure("Le serveur " + where() + " accepte la connexion sur le port " + std::to_string(c_.port) + " mais ne dit rien en " +
                                   secondsText(t_) + " : v\xC3\xA9rifie la s\xC3\xA9" "curit\xC3\xA9 choisie (STARTTLS sur le port 587, TLS sur le port 465).");
                return failure("Le serveur " + where() + " ne r\xC3\xA9pond plus (rien en " + secondsText(t_) + ", \xC3\xA0 l'\xC3\xA9tape " + step +
                               ") : r\xC3\xA9" "essaie plus tard, ou v\xC3\xA9rifie le port et la s\xC3\xA9" "curit\xC3\xA9 choisis.");
            case Io::Closed:
                return failure("Le serveur " + where() + " a ferm\xC3\xA9 la connexion (\xC3\xA0 l'\xC3\xA9tape " + std::string(step) +
                               ") : r\xC3\xA9" "essaie plus tard, ou v\xC3\xA9rifie le port et la s\xC3\xA9" "curit\xC3\xA9 choisis.");
            default:
                return failure("Le serveur " + where() + " a r\xC3\xA9pondu autre chose que du SMTP (\xC3\xA0 l'\xC3\xA9tape " + std::string(step) +
                               ") : v\xC3\xA9rifie le nom du serveur, le port et la s\xC3\xA9" "curit\xC3\xA9 choisis.");
        }
    }

    // Une commande et sa reponse. Faux : `out` dit la panne.
    bool command(const std::string& line, Reply& r, const char* step, SendResult& out) {
        std::string wire;   // efface apres l'envoi : une ligne d'identification porte le mot de passe (en base64)
        wire.reserve(line.size() + 2);
        wire += line;
        wire += "\r\n";
        Io io = link_.send(wire, t_);
        wipe(wire);
        if (io == Io::Ok) io = link_.reply(r, t_);
        if (io != Io::Ok) {
            out = ioFailure(io, step);
            return false;
        }
        return true;
    }

    SendResult tlsFailure(int rc, const std::string& setupWhy, Attempt& a) const {
        const std::string h = where();
        const std::string refused = " : la connexion est refus\xC3\xA9" "e et rien n'est envoy\xC3\xA9.";
        if (rc == -1) return failure("La connexion chiffr\xC3\xA9" "e avec " + h + " ne peut pas \xC3\xAAtre pr\xC3\xA9par\xC3\xA9" "e (" + setupWhy + ").");
        if (rc == Link::kTimeout)
            return failure("La n\xC3\xA9gociation TLS avec " + h + " n'aboutit pas en " + secondsText(t_) +
                           " : v\xC3\xA9rifie le port et la s\xC3\xA9" "curit\xC3\xA9 choisis (TLS sur le port 465, STARTTLS sur le port 587).");
        const std::uint32_t f = link_.verifyFlags();
        if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || rc == MBEDTLS_ERR_SSL_BAD_CERTIFICATE || (f != 0 && rc != MBEDTLS_ERR_SSL_CONN_EOF)) {
            if (f & MBEDTLS_X509_BADCERT_CN_MISMATCH)
                return failure("Le certificat pr\xC3\xA9sent\xC3\xA9 par " + h + " est celui d'un autre serveur (le nom ne correspond pas)" + refused);
            if (f & MBEDTLS_X509_BADCERT_EXPIRED) return failure("Le certificat du serveur " + h + " a expir\xC3\xA9" + refused);
            if (f & MBEDTLS_X509_BADCERT_FUTURE)
                return failure("Le certificat du serveur " + h + " n'est pas encore valable (v\xC3\xA9rifie la date et l'heure de ce PC)" + refused);
            if (f & MBEDTLS_X509_BADCERT_REVOKED) return failure("Le certificat du serveur " + h + " est r\xC3\xA9voqu\xC3\xA9" + refused);
            if (f & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
                a.untrustedOnly = (f & ~static_cast<std::uint32_t>(MBEDTLS_X509_BADCERT_NOT_TRUSTED)) == 0;
                a.chain = link_.peerChain();
                return failure("Le certificat du serveur " + h + " n'est pas reconnu par ce poste (aucune autorit\xC3\xA9 de confiance ne l'a sign\xC3\xA9)" + refused);
            }
            return failure("Le certificat du serveur " + h + " n'est pas fiable (" + tls::errorText(rc) + ")" + refused);
        }
        if (c_.security == "tls" && link_.head().rfind("220", 0) == 0)
            return failure("Le serveur " + h + " parle d'abord en clair sur le port " + std::to_string(c_.port) +
                           " : choisis la s\xC3\xA9" "curit\xC3\xA9 STARTTLS (port 587) plut\xC3\xB4t que TLS.");
        switch (rc) {
            case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE:
                return failure("Le serveur " + h + " refuse la connexion chiffr\xC3\xA9" "e (TLS) : v\xC3\xA9rifie le port et la s\xC3\xA9" "curit\xC3\xA9 choisis.");
            case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
            case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
                return failure("Ce poste et le serveur " + h + " n'ont aucun chiffrement en commun (il faut TLS 1.2 ou 1.3) : rien n'est envoy\xC3\xA9.");
            case MBEDTLS_ERR_SSL_CONN_EOF:
            case MBEDTLS_ERR_NET_CONN_RESET:
            case MBEDTLS_ERR_NET_RECV_FAILED:
            case MBEDTLS_ERR_NET_SEND_FAILED:
                return failure("Le serveur " + h + " a ferm\xC3\xA9 la connexion pendant la n\xC3\xA9gociation TLS : v\xC3\xA9rifie le port et la s\xC3\xA9" "curit\xC3\xA9 choisis.");
            default:
                return failure("La connexion chiffr\xC3\xA9" "e avec " + h + " a \xC3\xA9" "chou\xC3\xA9 (" + tls::errorText(rc) + ") : rien n'est envoy\xC3\xA9.");
        }
    }

    // Le texte d'un serveur montre a l'editeur : jamais s'il contient un secret de cet envoi (le mot de passe, le
    // jeton, leur forme base64, la ligne XOAUTH2 - ou leurs 16 premiers caracteres : le texte montre est coupe a
    // 200) - le code seul alors. Cherche dans le texte entier, avant la coupe.
    std::string shown(const Reply& r) const {
        std::string all;
        for (const auto& l : r.lines) {
            all += l;
            all += '\n';
        }
        bool leak = false;
        for (const auto& s : secrets_) {
            if (s.empty()) continue;
            if (all.find(s) != std::string::npos || (s.size() > 16 && all.find(std::string_view(s).substr(0, 16)) != std::string::npos)) {
                leak = true;
                break;
            }
        }
        wipe(all);
        return leak ? std::to_string(r.code) : r.text();
    }

    SendResult authRefused(const Reply& r) const {
        const std::string said = shown(r);
        std::string low = lower(said);
        // Microsoft (Outlook, Hotmail, Microsoft 365) : l'identification par mot de passe est retiree, OAuth seulement
        // (1.38.0 : aussi tout 535 d'un serveur de Microsoft).
        if (low.find("basic authentication is disabled") != std::string::npos || low.find("5.7.139") != std::string::npos ||
            (r.code == 535 && microsoftHost(c_.host)))
            return failure("Le serveur " + where() + " refuse toute identification par mot de passe pour ce compte (Microsoft exige "
                           "d\xC3\xA9sormais OAuth) : dans les r\xC3\xA9glages du courriel, choisis l'identification par compte Microsoft, puis "
                           "connecte-toi (" + std::string(kSignInPath) + ") ; sa r\xC3\xA9ponse : " + cite(said) + ".");
        return failure("Le serveur refuse l'identifiant ou le mot de passe d'application : pour Gmail, il faut un mot de passe d'application "
                       "(compte Google \xE2\x80\xBA S\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Validation en deux \xC3\xA9tapes) ; sa r\xC3\xA9ponse : " + cite(said) + ".");
    }

    // 1.38.0 : le refus du jeton Microsoft (AUTH XOAUTH2).
    SendResult xoauthRefused(const Reply& r) const {
        const std::string said = shown(r);
        const std::string low = lower(said);
        // Microsoft 365 : SMTP AUTH coupe pour la boite ou pour toute l'organisation (5.7.139, SmtpClientAuthentication).
        if (low.find("5.7.139") != std::string::npos || low.find("smtpclientauthentication") != std::string::npos ||
            low.find("smtp_auth_disabled") != std::string::npos)
            return failure("Microsoft refuse l'envoi authentifi\xC3\xA9 (SMTP AUTH) pour la bo\xC3\xAEte " + c_.user +
                           " : sur Microsoft 365, l'administrateur doit autoriser \xC2\xAB SMTP authentifi\xC3\xA9 \xC2\xBB pour cette bo\xC3\xAEte (centre "
                           "d'administration Microsoft 365 \xE2\x80\xBA Utilisateurs actifs \xE2\x80\xBA la bo\xC3\xAEte \xE2\x80\xBA Courrier \xE2\x80\xBA G\xC3\xA9rer les applications de "
                           "messagerie) et ne pas le d\xC3\xA9sactiver pour toute l'organisation ; sa r\xC3\xA9ponse : " + cite(said) + ".");
        std::string hint;
        if (!account_.empty() && lower(account_) != lower(c_.user))
            hint = "la connexion Microsoft est celle de " + account_ + ", mais l'identifiant SMTP r\xC3\xA9gl\xC3\xA9 est " + c_.user +
                   " : mets l'adresse du compte connect\xC3\xA9, ou reconnecte-toi avec la bonne bo\xC3\xAEte (" + std::string(kSignInPath) + ")";
        else
            hint = "v\xC3\xA9rifie que l'identifiant SMTP est l'adresse de la bo\xC3\xAEte connect\xC3\xA9" "e et que l'application a la permission "
                   "SMTP.Send (portail Microsoft Entra \xE2\x80\xBA ton application \xE2\x80\xBA Autorisations d'API) ; sinon reconnecte-toi (" +
                   std::string(kSignInPath) + ")";
        return failure("Le serveur " + where() + " refuse le jeton Microsoft : " + hint + " ; sa r\xC3\xA9ponse : " + cite(said) + ".");
    }

    // 1.38.0 : AUTH XOAUTH2 en deux temps (l'exemple de Microsoft) : << AUTH XOAUTH2 >>, 334, la ligne
    // base64("user=" + identifiant + ^A + "auth=Bearer " + jeton + ^A ^A) ; 235 : accepte. Un refus en 334 (le
    // defi d'erreur, RFC 7628 : un JSON en base64) recoit une ligne vide, puis le serveur dit 535.
    bool xoauth2(const std::string& token, const Reply& ehlo, SendResult& out) {
        if (!link_.secure()) {
            out = failure("La connexion avec " + where() + " n'est pas chiffr\xC3\xA9" "e : le jeton Microsoft n'est pas envoy\xC3\xA9.");
            return false;
        }
        std::string mechs;
        if (!ehlo.capability("AUTH", &mechs)) {
            out = failure("Le serveur " + where() + " ne propose aucune identification sur ce port : v\xC3\xA9rifie le serveur et le port (587 ou 465).");
            return false;
        }
        mechs = " " + mechs + " ";
        if (mechs.find(" XOAUTH2 ") == std::string::npos) {
            out = failure("Le serveur " + where() + " ne propose pas l'identification par compte Microsoft (AUTH XOAUTH2) : pour Outlook.com "
                          "ou Hotmail, choisis " + std::string(kOutlookHost) + " ; pour Microsoft 365, " + std::string(kOffice365Host) +
                          " (port 587, STARTTLS).");
            return false;
        }
        Reply r;
        if (!command("AUTH XOAUTH2", r, "de l'identification", out)) return false;
        if (r.code != 334) {
            out = xoauthRefused(r);
            return false;
        }
        std::string sasl;
        sasl.reserve(c_.user.size() + token.size() + 24);
        sasl += "user=";
        sasl += c_.user;
        sasl += '\x01';
        sasl += "auth=Bearer ";
        sasl += token;
        sasl += '\x01';
        sasl += '\x01';
        std::string line = crypto::base64(sasl);
        wipe(sasl);
        secrets_.push_back(line);   // une copie, pour taire un serveur qui la renverrait (effacee a la fin)
        const bool ok = command(line, r, "de l'identification", out);
        wipe(line);
        if (!ok) return false;
        if (r.code == 235) return true;
        if (r.code == 334 && !command("", r, "de l'identification", out)) return false;   // le defi d'erreur : une ligne vide
        out = xoauthRefused(r);
        return false;
    }

    bool authenticate(const std::string& password, const Reply& ehlo, SendResult& out) {
        if (oauth_) return xoauth2(password, ehlo, out);
        // Jamais avant TLS (la suite ne le permet pas ; la regle est verifiee quand meme).
        if (!link_.secure()) {
            out = failure("La connexion avec " + where() + " n'est pas chiffr\xC3\xA9" "e : l'identifiant et le mot de passe ne sont pas envoy\xC3\xA9s.");
            return false;
        }
        std::string mechs;
        if (!ehlo.capability("AUTH", &mechs)) {
            out = failure("Le serveur " + where() + " ne propose aucune identification sur ce port : v\xC3\xA9rifie le serveur et le port (587 ou 465).");
            return false;
        }
        mechs = " " + mechs + " ";
        const bool plain = mechs.find(" PLAIN ") != std::string::npos;
        const bool login = mechs.find(" LOGIN ") != std::string::npos;
        if (!plain && !login) {
            out = failure("Le serveur " + where() + " ne propose ni AUTH PLAIN ni AUTH LOGIN (il exige sans doute OAuth) : ce poste ne peut pas s'y "
                          "identifier par mot de passe ; pour Outlook, Hotmail ou Microsoft 365, choisis l'identification par compte Microsoft "
                          "dans les r\xC3\xA9glages du courriel.");
            return false;
        }
        Reply r;
        bool ok = false;
        if (plain) {
            std::string raw;
            raw.reserve(c_.user.size() + password.size() + 2);
            raw += '\0';
            raw += c_.user;
            raw += '\0';
            raw += password;
            std::string b64 = crypto::base64(raw);
            wipe(raw);
            std::string line;
            line.reserve(11 + b64.size());
            line += "AUTH PLAIN ";
            line += b64;
            wipe(b64);
            ok = command(line, r, "de l'identification", out);
            wipe(line);
        } else {
            ok = command("AUTH LOGIN", r, "de l'identification", out);
            if (ok && r.code != 334) {
                out = authRefused(r);
                return false;
            }
            if (ok) ok = command(crypto::base64(c_.user), r, "de l'identification", out);
            if (ok && r.code != 334) {
                out = authRefused(r);
                return false;
            }
            if (ok) {
                std::string line = crypto::base64(password);
                ok = command(line, r, "de l'identification", out);
                wipe(line);
            }
        }
        if (!ok) return false;
        if (r.code != 235) {
            out = authRefused(r);
            return false;
        }
        return true;
    }

    SendResult steps(const std::string& password, const std::string& data, const std::string& to, const std::vector<Bytes>& extraRoots, Attempt& a) {
        const std::string h = where();
        const bool implicitTls = c_.security == "tls";
        std::string why;
        if (!tls::ready(&why)) return failure("La connexion chiffr\xC3\xA9" "e ne peut pas \xC3\xAAtre pr\xC3\xA9par\xC3\xA9" "e (" + why + ") : rien n'est envoy\xC3\xA9.");
        if (!link_.loadRoots(c_.caFile, extraRoots, &why)) return failure("Le courriel n'est pas parti : " + why + ".");
        if (!link_.connect(c_.host, c_.port, t_, &why)) {
            if (why.rfind("adresse inconnue", 0) == 0)
                return failure("Le serveur SMTP " + cite(h) + " est introuvable : v\xC3\xA9rifie son nom et ta connexion \xC3\xA0 Internet.");
            // "hote:port : la raison" : la raison seule
            const std::string prefix = c_.host + ":" + std::to_string(c_.port) + " : ";
            if (why.rfind(prefix, 0) == 0) why = why.substr(prefix.size());
            return failure("Impossible de joindre " + h + " sur le port " + std::to_string(c_.port) + " : " + why +
                           " ; v\xC3\xA9rifie le serveur, le port et ta connexion \xC3\xA0 Internet.");
        }
        if (implicitTls) {
            const int rc = link_.handshake(c_.host, t_, &why);
            if (rc != 0) return tlsFailure(rc, why, a);
        }
        SendResult out;
        Reply r;
        Io io = link_.reply(r, t_);
        if (io != Io::Ok) return ioFailure(io, "de l'accueil");
        if (r.code != 220) return failure("Le serveur " + h + " refuse la connexion ; sa r\xC3\xA9ponse : " + cite(shown(r)) + ".");
        const std::string hello = "EHLO " + ehloName();
        if (!command(hello, r, "EHLO", out)) return out;
        if (r.code != 250) return failure("Le serveur " + h + " refuse EHLO ; sa r\xC3\xA9ponse : " + cite(shown(r)) + ".");
        if (!implicitTls) {
            if (!r.capability("STARTTLS"))
                return failure("Le serveur " + h + " ne propose pas STARTTLS sur le port " + std::to_string(c_.port) +
                               " : rien n'est envoy\xC3\xA9 sans chiffrement (si ton fournisseur l'indique, choisis TLS sur le port 465).");
            if (!command("STARTTLS", r, "STARTTLS", out)) return out;
            if (r.code != 220)
                return failure("Le serveur " + h + " refuse STARTTLS (sa r\xC3\xA9ponse : " + cite(shown(r)) + ") : rien n'est envoy\xC3\xA9 sans chiffrement.");
            link_.discardInput();
            const int rc = link_.handshake(c_.host, t_, &why);
            if (rc != 0) return tlsFailure(rc, why, a);
            if (!command(hello, r, "EHLO", out)) return out;
            if (r.code != 250) return failure("Le serveur " + h + " refuse EHLO ; sa r\xC3\xA9ponse : " + cite(shown(r)) + ".");
        }
        if (!authenticate(password, r, out)) return out;
        if (!command("MAIL FROM:<" + c_.from + ">", r, "MAIL FROM", out)) return out;
        if (r.code != 250)
            return failure("Le serveur refuse l'exp\xC3\xA9" "diteur " + cite(c_.from) + " (sa r\xC3\xA9ponse : " + cite(shown(r)) +
                           ") : l'adresse d'exp\xC3\xA9" "dition doit \xC3\xAAtre celle de ton compte, ou un alias autoris\xC3\xA9.");
        if (!command("RCPT TO:<" + to + ">", r, "RCPT TO", out)) return out;
        if (r.code != 250 && r.code != 251)
            return failure("Le serveur refuse le destinataire " + cite(to) + " (sa r\xC3\xA9ponse : " + cite(shown(r)) + ") : v\xC3\xA9rifie son adresse.");
        if (!command("DATA", r, "DATA", out)) return out;
        if (r.code != 354) return failure("Le serveur refuse l'envoi du message ; sa r\xC3\xA9ponse : " + cite(shown(r)) + ".");
        io = link_.send(dotStuffed(data), t_);
        if (io == Io::Ok) io = link_.reply(r, t_);
        if (io != Io::Ok) return ioFailure(io, "de l'envoi du message");
        if (r.code != 250) return failure("Le serveur a refus\xC3\xA9 le message ; sa r\xC3\xA9ponse : " + cite(shown(r)) + ".");
        // QUIT : le message est deja accepte, une panne ici ne change rien.
        if (link_.send("QUIT\r\n", std::min(t_, 5000)) == Io::Ok) (void)link_.reply(r, std::min(t_, 5000));
        SendResult ok;
        ok.ok = true;
        ok.reached = true;
        ok.message = "Le courriel est parti vers " + to + ".";
        return ok;
    }

    const Config&            c_;
    int                      t_;
    bool                     oauth_{false};
    std::string              account_;
    std::vector<std::string> secrets_;   // les formes du secret a taire (effacees a la fin)
    Link                     link_;
};

bool readPassword(const std::string& folder, std::string& out, std::string* why) {
    const std::string path = passwordPath(folder);
    if (!isFile(path)) return fail(why, "Le mot de passe d'application n'est pas enregistr\xC3\xA9 sur ce poste : saisis-le dans les r\xC3\xA9glages du courriel.");
    Bytes sealed, plain;
    std::string w;
    if (!crypto::readFile(path, sealed, &w, 64u * 1024u) || sealed.empty())
        return fail(why, "Le mot de passe d'application enregistr\xC3\xA9 est illisible : saisis-le de nouveau dans les r\xC3\xA9glages du courriel.");
    if (!crypto::unprotect(sealed, plain, &w)) {
        wipe(sealed);
        return fail(why, "Le mot de passe d'application enregistr\xC3\xA9 ne peut pas \xC3\xAAtre relu par ce compte sur ce PC (" + w +
                             ") : saisis-le de nouveau dans les r\xC3\xA9glages du courriel.");
    }
    out.assign(plain.begin(), plain.end());
    wipe(sealed);
    wipe(plain);
    return true;
}


// ============================================================================ 1.38.0 : OAuth 2.0 (Microsoft)
std::mutex& microsoftLock() {
    static std::mutex m;   // relire, rafraichir et reecrire mail-microsoft.bin d'un seul tenant
    return m;
}

// Un jeton, un code d'appareil : 1 a 16 Kio de caracteres ASCII visibles (ni espace ni controle).
bool tokenText(std::string_view s) {
    if (s.empty() || s.size() > kMaxToken) return false;
    return std::all_of(s.begin(), s.end(), [](char ch) {
        const auto c = static_cast<unsigned char>(ch);
        return c > 0x20 && c < 0x7F;
    });
}

// Un champ de formulaire application/x-www-form-urlencoded ajoute a `out` (sans copie intermediaire : la
// valeur peut etre un jeton). Rien que les caracteres non reserves restent tels quels.
void appendField(std::string& out, const char* key, std::string_view value) {
    static const char* const kHex = "0123456789ABCDEF";
    if (!out.empty()) out += '&';
    out += key;
    out += '=';
    for (char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
}

struct OAuthReply {
    bool        answered{false};   // une reponse HTTP entiere est arrivee (quel que soit son code)
    int         status{0};
    Value       json;              // l'objet rendu (nul si ce n'est pas un objet JSON)
    std::string failure;           // !answered : la panne du transport (le texte de AiHttp, sans secret)
};

// POST d'un formulaire, une reponse JSON. `body` est consomme et efface (il porte un code ou un jeton) ; la
// reponse brute aussi, apres lecture.
OAuthReply postForm(const std::string& url, std::string& body, int timeoutMs, bool french) {
    OAuthReply out;
    app::ai::HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}, {"User-Agent", "XPG-License-Admin"}};
    if (french) req.headers.emplace_back("Accept-Language", "fr-FR, fr;q=0.9");   // la phrase de Microsoft (DeviceCode::message)
    req.body.swap(body);
    const int seconds = std::clamp((std::max(1, timeoutMs) + 999) / 1000, 1, 120);
    req.connectTimeoutSeconds = seconds;
    req.idleTimeoutSeconds = seconds;
    std::string raw;
    raw.reserve(32u * 1024u);   // une reponse ordinaire tient sans reallocation (pas de copie du jeton laissee en memoire)
    bool tooBig = false;
    app::ai::HttpSink sink;
    sink.onData = [&](std::string_view chunk) {
        if (tooBig || raw.size() + chunk.size() > kMaxHttpReply) {
            tooBig = true;
            return;
        }
        raw.append(chunk);
    };
    const std::unique_ptr<app::ai::HttpClient> client = app::ai::makeHttpClient();
    const app::ai::HttpResponse resp = client->perform(req, sink);
    wipe(req.body);
    if (!resp.ok()) {
        app::ai::Url u;
        (void)app::ai::parseUrl(url, u, nullptr);
        out.failure = resp.error.empty() ? app::ai::failureText(resp.failure, u.host) : resp.error;
        wipe(raw);
        return out;
    }
    if (tooBig) {
        out.failure = "la r\xC3\xA9ponse est trop grande";
        wipe(raw);
        return out;
    }
    out.answered = true;
    out.status = resp.status;
    Value v;
    if (core::json::parse(raw, v, nullptr) && v.isObject()) out.json = std::move(v);
    wipe(raw);
    return out;
}

// "Microsoft (login.microsoftonline.com) ne repond pas QUOI : la panne ; FIN."
std::string unreachable(const Config& c, const std::string& failure, const char* what, const char* end) {
    std::string f = trimmed(failure);
    while (!f.empty() && f.back() == '.') f.pop_back();
    if (!f.empty() && f[0] >= 'A' && f[0] <= 'Z') f[0] = static_cast<char>(f[0] - 'A' + 'a');
    if (f.compare(0, 2, "\xC3\x89") == 0) f.replace(0, 2, "\xC3\xA9");
    return "Microsoft (" + authorityHost(c) + ") ne r\xC3\xA9pond pas " + what + " : " + (f.empty() ? std::string("pas de r\xC3\xA9ponse") : f) + " ; " + end + ".";
}

// Un entier d'une reponse (un nombre, ou un texte de chiffres : l'ancienne autorite de Microsoft les ecrivait ainsi).
long long intOf(const Value& j, std::string_view key, long long fallback) {
    const Value& v = j.at(key);
    if (v.isNumber()) return v.asInt(fallback);
    const std::string& s = v.asString();
    if (!s.empty() && s.size() <= 9 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) return std::atoll(s.c_str());
    return fallback;
}

struct OAuthError {
    std::string code;      // "authorization_pending", "invalid_grant"... (minuscules) ; vide : pas un refus OAuth lisible
    long long   aadsts{0}; // le code de Microsoft (AADSTS70016...), 0 sans
    std::string said;      // le debut de error_description : une ligne, 200 octets au plus ; vide s'il porte un secret
};
OAuthError oauthError(const OAuthReply& r, std::initializer_list<const std::string*> secrets) {
    OAuthError e;
    e.code = lower(clipUtf8(oneLine(r.json.str("error")), 64));
    const std::string d = r.json.str("error_description");
    if (const auto at = d.find("AADSTS"); at != std::string::npos) {
        for (std::size_t i = at + 6; i < d.size() && i < at + 15 && std::isdigit(static_cast<unsigned char>(d[i])); ++i) e.aadsts = e.aadsts * 10 + (d[i] - '0');
    }
    if (e.aadsts == 0) {
        const Value& codes = r.json.at("error_codes");
        if (codes.isArray() && !codes.items().empty()) e.aadsts = codes.items()[0].asInt(0);
    }
    std::string first = d;
    for (const char* cut : {"\r", "\n", " Trace ID", " Correlation ID", " Timestamp:"})
        if (const auto at = first.find(cut); at != std::string::npos) first.resize(at);
    e.said = clipUtf8(oneLine(first), 200);
    for (const std::string* s : secrets) {
        if (!s || s->empty()) continue;
        std::string b64 = crypto::base64(*s);
        const bool leak = e.said.find(*s) != std::string::npos || e.said.find(b64) != std::string::npos ||
                          (s->size() >= 16 && e.said.find(s->substr(0, 16)) != std::string::npos);
        wipe(b64);
        if (leak) {
            e.said.clear();
            break;
        }
    }
    return e;
}

enum class Stage { Start, Poll, Refresh };

bool permissionError(const OAuthError& e) {
    switch (e.aadsts) {
        case 65001: case 65005: case 70011: case 90008: case 90094: case 90095: case 500011: case 650052: case 650057: return true;
        default: break;
    }
    return e.code == "invalid_scope" || e.code == "consent_required";
}
bool policyError(const OAuthError& e) { return e.aadsts == 53000 || e.aadsts == 53001 || e.aadsts == 53002 || e.aadsts == 53003 || e.aadsts == 530032; }
bool tenantError(const OAuthError& e) {
    return e.aadsts == 90002 || e.aadsts == 900023 || e.aadsts == 50059 || e.aadsts == 50020 || e.aadsts == 50194 || e.aadsts == 90072;
}
std::string permissionText() {
    return "l'application n'a pas la permission d'envoyer des mails : dans le portail Microsoft Entra \xE2\x80\xBA ton application \xE2\x80\xBA "
           "Autorisations d'API, ajoute Office 365 Exchange Online \xE2\x80\xBA SMTP.Send (d\xC3\xA9l\xC3\xA9gu\xC3\xA9" "e) ; pour Microsoft 365, un administrateur doit "
           "aussi donner son consentement ; puis reconnecte-toi (" + std::string(kSignInPath) + ")";
}

// La phrase d'un refus de Microsoft (jamais un jeton ni un code : `e.said` est deja tu s'il en portait un).
std::string oauthRefusal(const Config& c, const OAuthReply& r, const OAuthError& e, Stage stage) {
    const std::string tail = e.said.empty() ? std::string() : " ; sa r\xC3\xA9ponse : " + cite(e.said);
    const char* head = stage == Stage::Start ? "La connexion \xC3\xA0 Microsoft ne peut pas commencer : "
                       : stage == Stage::Poll ? "La connexion \xC3\xA0 Microsoft a \xC3\xA9" "chou\xC3\xA9 : "
                                              : "Le courriel n'est pas parti : ";
    const auto says = [&](const std::string& what) { return head + what + tail + "."; };
    if (r.status == 429) return says("Microsoft limite les demandes pour l'instant, r\xC3\xA9" "essaie dans quelques minutes");
    if (r.status >= 500) return says("Microsoft a une panne (code " + std::to_string(r.status) + "), r\xC3\xA9" "essaie dans quelques minutes");
    if (!r.json.isObject() || e.code.empty())
        return says("Microsoft (" + authorityHost(c) + ") a r\xC3\xA9pondu " + std::to_string(r.status) +
                    " sans explication lisible ; v\xC3\xA9rifie l'autorit\xC3\xA9 (laisse-la vide pour https://login.microsoftonline.com)");
    if (e.aadsts == 7000218 || (e.code == "invalid_client" && e.aadsts != 700016))
        return says("l'application n'accepte pas la connexion par code : dans le portail Microsoft Entra \xE2\x80\xBA ton application \xE2\x80\xBA "
                    "Authentification, mets \xC2\xAB Autoriser les flux clients publics \xC2\xBB \xC3\xA0 Oui");
    if (e.aadsts == 700016)
        return says("Microsoft ne conna\xC3\xAEt pas l'application " + c.clientId + " : v\xC3\xA9rifie l'ID d'application (client) copi\xC3\xA9 du portail "
                    "Microsoft Entra (ton application \xE2\x80\xBA Vue d'ensemble) et le locataire (\xC2\xAB consumers \xC2\xBB pour Outlook.com et Hotmail, "
                    "\xC2\xAB organizations \xC2\xBB pour Microsoft 365)");
    if (e.code == "unauthorized_client" && (c.tenant == "consumers" || lower(e.said).find("consumers") != std::string::npos))
        return says("l'application n'accepte pas les comptes Microsoft personnels (Outlook.com, Hotmail) : dans le portail Microsoft "
                    "Entra \xE2\x80\xBA ton application \xE2\x80\xBA Authentification, choisis les comptes de tout annuaire et les comptes Microsoft personnels");
    if (tenantError(e))
        return says("Microsoft refuse le locataire " + cite(c.tenant) + " pour ce compte : choisis \xC2\xAB consumers \xC2\xBB (Outlook.com, Hotmail), "
                    "\xC2\xAB organizations \xC2\xBB (Microsoft 365) ou \xC2\xAB common \xC2\xBB, et v\xC3\xA9rifie les types de comptes accept\xC3\xA9s par l'application "
                    "(portail Microsoft Entra \xE2\x80\xBA ton application \xE2\x80\xBA Authentification)");
    if (permissionError(e)) return says(permissionText());
    if (policyError(e))
        return says("une r\xC3\xA8gle d'acc\xC3\xA8s de ton organisation bloque la connexion \xC3\xA0 Microsoft : demande \xC3\xA0 ton administrateur Microsoft 365");
    if (e.code == "unauthorized_client")
        return says("Microsoft n'autorise pas l'application " + c.clientId + " ici : v\xC3\xA9rifie l'ID d'application (client) et le locataire");
    if (stage == Stage::Poll && (e.code == "invalid_grant" || e.code == "bad_verification_code"))
        return says("Microsoft ne reconna\xC3\xAEt pas ce code de connexion, recommence (" + std::string(kSignInPath) + ")");
    return says("Microsoft refuse la demande (" + e.code + ")");
}

// Le base64url (JWT) en octets.
bool unbase64url(std::string_view s, Bytes& out) {
    std::string b(s);
    for (auto& ch : b) {
        if (ch == '-') ch = '+';
        else if (ch == '_') ch = '/';
    }
    while (b.size() % 4 != 0) b += '=';
    return crypto::unbase64(b, out);
}
// Le compte d'un id_token (preferred_username, sinon email, sinon upn). Il n'est que MONTRE : sa signature
// n'est pas verifiee (il arrive par TLS de l'autorite elle-meme, dans la reponse a notre demande).
std::string accountOf(const std::string& idToken) {
    const auto a = idToken.find('.');
    if (a == std::string::npos) return {};
    const auto b = idToken.find('.', a + 1);
    if (b == std::string::npos || b - a - 1 > kMaxToken) return {};
    Bytes payload;
    if (!unbase64url(std::string_view(idToken).substr(a + 1, b - a - 1), payload)) return {};
    Value v;
    if (!core::json::parse(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()), v, nullptr) || !v.isObject()) return {};
    for (const char* k : {"preferred_username", "email", "upn"}) {
        const std::string s = clipUtf8(oneLine(v.str(k)), 254);
        if (!s.empty()) return s;
    }
    return {};
}

struct Tokens {
    std::string access, refresh, account;
    Tokens() = default;
    Tokens(const Tokens&) = delete;
    Tokens& operator=(const Tokens&) = delete;
    ~Tokens() {
        wipe(access);
        wipe(refresh);
    }
};
// Les jetons d'une reponse 200 de .../token. `needRefresh` : la connexion (sans jeton de rafraichissement, rien a
// garder). Faux : `why` (un fragment de phrase).
bool readTokens(const Value& j, Tokens& t, bool needRefresh, std::string* why) {
    t.access = j.str("access_token");
    if (!tokenText(t.access)) return fail(why, "Microsoft a r\xC3\xA9pondu sans jeton d'acc\xC3\xA8s lisible");
    const std::string type = lower(trimmed(j.str("token_type")));
    if (!type.empty() && type != "bearer")
        return fail(why, "Microsoft a donn\xC3\xA9 un jeton d'un autre type que Bearer (" + cite(clipUtf8(oneLine(type), 40)) + ")");
    t.refresh = j.str("refresh_token");
    if (!t.refresh.empty() && !tokenText(t.refresh)) return fail(why, "Microsoft a donn\xC3\xA9 un jeton de rafra\xC3\xAE" "chissement illisible");
    if (needRefresh && t.refresh.empty())
        return fail(why, "Microsoft n'a pas donn\xC3\xA9 de jeton durable (la permission offline_access manque) : ajoute-la dans le portail "
                         "Microsoft Entra \xE2\x80\xBA ton application \xE2\x80\xBA Autorisations d'API, puis recommence");
    // Les portees accordees, si Microsoft les dit : SMTP.Send doit y etre.
    const std::string scope = lower(j.str("scope"));
    if (!scope.empty() && scope.find("smtp.send") == std::string::npos) return fail(why, permissionText());
    t.account = accountOf(j.str("id_token"));
    return true;
}

// ---- mail-microsoft.bin : un JSON protege (crypto::protect), ecrit en prive
struct Stored {
    std::string  account, clientId, tenant, authority, refresh;
    std::int64_t since{0};
    Stored() = default;
    Stored(const Stored&) = delete;
    Stored& operator=(const Stored&) = delete;
    ~Stored() { wipe(refresh); }
};
std::string notConnectedText() {
    return "Aucun compte Microsoft n'est connect\xC3\xA9 sur ce poste. Connecte-toi \xC3\xA0 Microsoft : " + std::string(kSignInPath) + ".";
}
bool writeStored(const std::string& folder, const Stored& s, std::string* why) {
    Value v = Value::object();
    v.set("format", std::string(kMicrosoftFormat));
    v.set("id_application", s.clientId);
    v.set("locataire", s.tenant);
    if (!s.authority.empty()) v.set("autorite", s.authority);
    if (!s.account.empty()) v.set("compte", s.account);
    v.set("depuis", s.since);
    v.set("jeton", s.refresh);
    std::string text = core::json::write(v, false);
    Bytes plain(text.begin(), text.end()), sealed;
    wipe(text);
    std::string w;
    bool ok = crypto::protect(plain, sealed, /*machine=*/false, &w);
    if (!ok) w = "la connexion Microsoft ne peut pas \xC3\xAAtre prot\xC3\xA9g\xC3\xA9" "e sur ce poste (" + w + ")";
    if (ok) ok = writePrivate(microsoftPath(folder), sealed, &w);
    wipe(plain);
    wipe(sealed);
    if (!ok && why) *why = w;
    return ok;
}
bool readStored(const std::string& folder, Stored& s, std::string* why) {
    const std::string path = microsoftPath(folder);
    if (!isFile(path)) return fail(why, notConnectedText());
    const std::string again = " : reconnecte-toi (" + std::string(kSignInPath) + ").";
    Bytes sealed, plain;
    std::string w;
    if (!crypto::readFile(path, sealed, &w, kMaxHttpReply) || sealed.empty())
        return fail(why, "La connexion Microsoft enregistr\xC3\xA9" "e est illisible" + again);
    if (!crypto::unprotect(sealed, plain, &w)) {
        wipe(sealed);
        return fail(why, "La connexion Microsoft enregistr\xC3\xA9" "e ne peut pas \xC3\xAAtre relue par ce compte sur ce PC (" + w + ")" + again);
    }
    wipe(sealed);
    Value v;
    const bool parsed = core::json::parse(std::string_view(reinterpret_cast<const char*>(plain.data()), plain.size()), v, nullptr) &&
                        v.isObject() && v.str("format") == kMicrosoftFormat;
    wipe(plain);
    if (!parsed) return fail(why, "La connexion Microsoft enregistr\xC3\xA9" "e est illisible" + again);
    s.clientId = v.str("id_application");
    s.tenant = v.str("locataire");
    s.authority = v.str("autorite");
    s.account = clipUtf8(oneLine(v.str("compte")), 254);
    s.since = v.integer("depuis", 0);
    s.refresh = v.str("jeton");
    if (!tokenText(s.refresh)) return fail(why, "La connexion Microsoft enregistr\xC3\xA9" "e est illisible" + again);
    return true;
}

// Le jeton de rafraichissement -> un jeton d'acces (et peut-etre un nouveau jeton de rafraichissement).
enum class Refresh { Ok, Dead, Failed };   // Dead : Microsoft ne reconnait plus la connexion (invalid_grant)
Refresh refreshTokens(const Config& c, const std::string& refreshToken, Tokens& t, std::string* why, int timeoutMs) {
    std::string body;
    body.reserve(refreshToken.size() * 3 + 256);
    appendField(body, "grant_type", "refresh_token");
    appendField(body, "client_id", c.clientId);
    appendField(body, "refresh_token", refreshToken);
    appendField(body, "scope", kRefreshScope);
    const OAuthReply r = postForm(endpoint(c, "token"), body, timeoutMs, false);
    if (!r.answered) {
        (void)fail(why, unreachable(c, r.failure, "pour donner le jeton d'envoi", "rien n'est envoy\xC3\xA9, v\xC3\xA9rifie ta connexion \xC3\xA0 Internet"));
        return Refresh::Failed;
    }
    std::string w;
    if (r.status == 200 && r.json.isObject() && !r.json.has("error")) {
        if (readTokens(r.json, t, false, &w)) return Refresh::Ok;
        (void)fail(why, "Le courriel n'est pas parti : " + w + ".");
        return Refresh::Failed;
    }
    const OAuthError e = oauthError(r, {&refreshToken});
    if (e.code == "invalid_grant" && !permissionError(e) && !policyError(e)) {
        (void)fail(why, "Microsoft ne reconna\xC3\xAEt plus ta connexion (elle a expir\xC3\xA9 ou a \xC3\xA9t\xC3\xA9 retir\xC3\xA9" "e) : reconnecte-toi (" + std::string(kSignInPath) +
                            ") ; rien n'est envoy\xC3\xA9" + (e.said.empty() ? std::string() : " ; sa r\xC3\xA9ponse : " + cite(e.said)) + ".");
        return Refresh::Dead;
    }
    (void)fail(why, oauthRefusal(c, r, e, Stage::Refresh));
    return Refresh::Failed;
}

// ---- l'envoi, commun aux deux identifications
// Les refus avant toute connexion (`c` normalisee). Faux : `r.message`.
bool precheck(const Config& c, const Message& m, SendResult& r) {
    std::string why;
    if (!validate(c, &why)) {
        r.message = "Les r\xC3\xA9glages du courriel sont refus\xC3\xA9s : " + why + ".";
        return false;
    }
    const std::string to = trimmed(m.to);
    if (!plausibleAddress(to)) {
        r.message = "L'adresse du destinataire " + cite(oneLine(to)) + " n'est pas valable : rien n'est envoy\xC3\xA9.";
        return false;
    }
    if (m.body.size() > kMaxBody) {
        r.message = "Le texte du courriel est trop long (4 Mo au plus) : rien n'est envoy\xC3\xA9.";
        return false;
    }
    if (!crypto::init()) {
        r.message = "Le hasard du syst\xC3\xA8me est indisponible (libsodium) : rien n'est envoy\xC3\xA9.";
        return false;
    }
    return true;
}
// Une session SMTP (et, sous Windows, une seconde si l'autorite manquait au magasin ROOT). `secret` : le mot de
// passe, ou (oauth) le jeton d'acces.
SendResult deliver(const Config& c, const std::string& secret, const Message& m, int timeoutMs, bool oauth, const std::string& account) {
    const int t = timeoutMs > 0 ? std::max(500, timeoutMs) : kDefaultTimeoutMs;
    const std::string to = trimmed(m.to);
    const std::string data = composeMessage(c, m);
    std::vector<Bytes> extra;
    Attempt a = Session(c, t, oauth, account).run(secret, data, to, extra);
#if defined(_WIN32)
    // Une autorite que le magasin ROOT n'a pas encore : Windows verifie la chaine (et peut la telecharger) ; s'il la
    // reconnait pour ce nom, sa racine est ajoutee et mbedTLS verifie de nouveau, tout.
    if (!a.result.ok && a.untrustedOnly && !a.chain.empty()) {
        Bytes root;
        if (windowsRootFor(a.chain, c.host, t, root)) {
            extra.push_back(std::move(root));
            a = Session(c, t, oauth, account).run(secret, data, to, extra);
        }
    }
#endif
    return a.result;
}
// auth "microsoft" : la connexion gardee -> un jeton d'acces (la rotation gardee) -> XOAUTH2.
SendResult sendMicrosoft(const std::string& folder, const Config& c, const Message& m, int timeoutMs) {
    SendResult r;
    if (!precheck(c, m, r)) return r;
    Tokens t;
    std::string account, note;
    {
        std::lock_guard<std::mutex> lock(microsoftLock());
        Stored st;
        std::string why;
        if (!readStored(folder, st, &why)) {
            r.message = why;
            return r;
        }
        if (st.clientId != c.clientId || st.tenant != c.tenant || st.authority != c.authority) {
            r.message = "La connexion Microsoft enregistr\xC3\xA9" "e a \xC3\xA9t\xC3\xA9 faite pour une autre application ou un autre locataire que ceux des "
                        "r\xC3\xA9glages : reconnecte-toi (" + std::string(kSignInPath) + ") ; rien n'est envoy\xC3\xA9.";
            return r;
        }
        const Refresh rr = refreshTokens(c, st.refresh, t, &why, timeoutMs);
        if (rr != Refresh::Ok) {
            if (rr == Refresh::Dead) {   // une connexion morte : l'interface montre de nouveau << Se connecter >>
                std::error_code ec;
                fs::remove(P(microsoftPath(folder)), ec);
            }
            r.message = why;
            return r;
        }
        account = st.account;
        if (!t.refresh.empty() && t.refresh != st.refresh) {   // la rotation : le nouveau jeton remplace l'ancien
            wipe(st.refresh);
            st.refresh = t.refresh;
            if (!writeStored(folder, st, &why))
                note = " (Le nouveau jeton de Microsoft n'a pas pu \xC3\xAAtre gard\xC3\xA9 : " + why + " ; si le prochain envoi \xC3\xA9" "choue, reconnecte-toi.)";
        }
    }
    r = deliver(c, t.access, m, timeoutMs, /*oauth=*/true, account);
    r.message += note;
    return r;
}

} // namespace

// ============================================================================ l'adresse
bool plausibleAddress(std::string_view s) {
    if (s.size() < 3 || s.size() > 254) return false;
    const auto at = s.find('@');
    if (at == std::string_view::npos || s.find('@', at + 1) != std::string_view::npos) return false;
    const std::string_view local = s.substr(0, at), domain = s.substr(at + 1);
    if (local.empty() || local.size() > 64 || domain.empty()) return false;
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c >= 0x7F) return false;   // espaces, controles, non ASCII
        if (std::strchr("<>\",;\\()[]:", ch) != nullptr) return false;
    }
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string_view::npos) return false;
    // le domaine : au moins deux etiquettes (lettres, chiffres, -), aucune vide, aucune bordee d'un -
    std::size_t labels = 0, start = 0;
    for (std::size_t i = 0; i <= domain.size(); ++i) {
        if (i < domain.size() && domain[i] != '.') {
            const char c = domain[i];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
            continue;
        }
        const std::string_view label = domain.substr(start, i - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        ++labels;
        start = i + 1;
    }
    return labels >= 2;
}

// ============================================================================ la configuration
std::string configPath(const std::string& folder) { return deploy::joinPath(folder, kConfigFile); }
std::string passwordPath(const std::string& folder) { return deploy::joinPath(folder, kPasswordFile); }
std::string microsoftPath(const std::string& folder) { return deploy::joinPath(folder, kMicrosoftFile); }

int defaultPort(std::string_view security) noexcept { return security == "tls" ? 465 : 587; }

bool validate(const Config& in, std::string* why) {
    const Config c = normalized(in);
    if (c.host.empty()) return fail(why, "le serveur SMTP est vide (par exemple smtp.gmail.com)");
    if (c.host.find('/') != std::string::npos || c.host.find('@') != std::string::npos)
        return fail(why, "le serveur " + cite(c.host) + " n'est pas un nom de serveur (par exemple smtp.gmail.com, sans \xC2\xAB smtp:// \xC2\xBB ni chemin)");
    if (std::count(c.host.begin(), c.host.end(), ':') == 1)
        return fail(why, "le serveur " + cite(c.host) + " s'\xC3\xA9" "crit sans le port (le port se r\xC3\xA8gle \xC3\xA0 part : 587 ou 465)");
    if (!validHost(c.host))
        return fail(why, "le serveur " + cite(c.host) + " n'est pas un nom de serveur (par exemple smtp.gmail.com, sans \xC2\xAB smtp:// \xC2\xBB ni chemin)");
    if (c.port < 1 || c.port > 65535)
        return fail(why, "le port " + std::to_string(c.port) + " n'existe pas (de 1 \xC3\xA0 65535 : 587 pour STARTTLS, 465 pour TLS)");
    if (c.security != "starttls" && c.security != "tls")
        return fail(why, "la s\xC3\xA9" "curit\xC3\xA9 " + cite(c.security) + " est refus\xC3\xA9" "e : \xC2\xAB starttls \xC2\xBB (port 587) ou \xC2\xAB tls \xC2\xBB (port 465), jamais en clair");
    if (!plausibleAddress(c.from)) return fail(why, "l'adresse de l'exp\xC3\xA9" "diteur " + cite(c.from) + " n'est pas valable");
    if (c.user.empty()) return fail(why, "l'identifiant SMTP est vide (le plus souvent, ton adresse)");
    if (hasControl(c.user) || c.user.size() > 254) return fail(why, "l'identifiant SMTP " + cite(oneLine(c.user)) + " n'est pas valable");
    if (!c.caFile.empty() && !isFile(c.caFile)) return fail(why, "le fichier d'autorit\xC3\xA9s " + cite(c.caFile) + " est introuvable");
    // 1.38.0 : l'identification
    if (c.auth != kAuthPassword && c.auth != kAuthMicrosoft)
        return fail(why, "l'identification " + cite(clipUtf8(oneLine(c.auth), 40)) +
                             " est refus\xC3\xA9" "e : \xC2\xAB motdepasse \xC2\xBB (un mot de passe d'application) ou \xC2\xAB microsoft \xC2\xBB (un compte Microsoft, OAuth 2.0)");
    if (!checkMicrosoft(c, c.auth == kAuthMicrosoft, why)) return false;
    if (c.auth == kAuthMicrosoft && !plausibleAddress(c.user))
        return fail(why, "avec un compte Microsoft, l'identifiant SMTP est l'adresse de ta bo\xC3\xAEte (par exemple prenom.nom@outlook.fr) : " +
                             cite(clipUtf8(oneLine(c.user), 254)) + " n'en est pas une");
    return true;
}

Config microsoftPreset(bool personal, Config base) {
    base.host = personal ? kOutlookHost : kOffice365Host;
    base.port = 587;
    base.security = "starttls";
    base.auth = kAuthMicrosoft;
    base.tenant = personal ? "consumers" : "organizations";
    return base;
}

bool microsoftHost(std::string_view host) noexcept {
    while (!host.empty() && (host.back() == '.' || host.back() == ' ')) host.remove_suffix(1);
    while (!host.empty() && host.front() == ' ') host.remove_prefix(1);
    const auto sameText = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char x = a[i];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
            if (x != b[i]) return false;
        }
        return true;
    };
    for (std::string_view d : {std::string_view("outlook.com"), std::string_view("office365.com"), std::string_view("hotmail.com"),
                               std::string_view("live.com"), std::string_view("office.com")}) {
        if (sameText(host, d)) return true;
        if (host.size() > d.size() + 1 && host[host.size() - d.size() - 1] == '.' && sameText(host.substr(host.size() - d.size()), d)) return true;
    }
    return false;
}

bool loadConfig(const std::string& folder, Config& out, std::string* why) {
    const std::string path = configPath(folder);
    if (!isFile(path))
        return fail(why, "le courriel n'est pas r\xC3\xA9gl\xC3\xA9 sur ce poste (le serveur SMTP, ton adresse et ton mot de passe d'application)");
    Bytes raw;
    if (!crypto::readFile(path, raw, why, 1024u * 1024u)) return false;
    Value v;
    std::string err;
    if (!core::json::parse(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), v, &err) || !v.isObject() ||
        v.str("format") != kConfigFormat)
        return fail(why, path + " est illisible (" + (err.empty() ? std::string("pas le bon format") : err) + ")");
    Config c;
    c.host = v.str("hote");
    c.port = static_cast<int>(v.integer("port", 0));
    c.security = v.str("securite");
    c.from = v.str("expediteur");
    c.fromName = v.str("nom");
    c.user = v.str("identifiant");
    c.caFile = v.str("autorites");
    // 1.38.0 : absentes d'un mail.json de 1.37 -> les defauts (mot de passe d'application)
    c.auth = v.str("authentification", kAuthPassword);
    c.clientId = v.str("id_application");
    c.tenant = v.str("locataire", "common");
    c.authority = v.str("autorite");
    c = normalized(c);
    std::string w;
    if (!validate(c, &w)) return fail(why, path + " : " + w);
    out = std::move(c);
    return true;
}

bool configured(const std::string& folder) {
    Config c;
    return isFile(configPath(folder)) && loadConfig(folder, c, nullptr);
}

bool saveConfig(const std::string& folder, const Config& in, std::string* why) {
    const Config c = normalized(in);
    if (!validate(c, why)) return false;
    Value v = Value::object();
    v.set("format", std::string(kConfigFormat));
    v.set("hote", c.host);
    v.set("port", c.port);
    v.set("securite", c.security);
    v.set("expediteur", c.from);
    if (!c.fromName.empty()) v.set("nom", c.fromName);
    v.set("identifiant", c.user);
    if (!c.caFile.empty()) v.set("autorites", c.caFile);
    // 1.38.0 : seulement hors des defauts (un mail.json de 1.37 se reecrit a l'identique)
    if (c.auth != kAuthPassword) v.set("authentification", c.auth);
    if (!c.clientId.empty()) v.set("id_application", c.clientId);
    if (c.tenant != "common") v.set("locataire", c.tenant);
    if (!c.authority.empty()) v.set("autorite", c.authority);
    const std::string text = core::json::write(v, true) + "\n";
    return writePrivate(configPath(folder), Bytes(text.begin(), text.end()), why);
}

bool savePassword(const std::string& folder, const std::string& password, std::string* why) {
    if (password.empty()) return fail(why, "le mot de passe d'application est vide");
    if (password.size() > 512) return fail(why, "le mot de passe d'application est trop long (512 caract\xC3\xA8res au plus)");
    if (hasControl(password)) return fail(why, "le mot de passe d'application contient un retour \xC3\xA0 la ligne ou un caract\xC3\xA8re invisible : recopie-le");
    Bytes plain(password.begin(), password.end()), sealed;
    std::string w;
    bool ok = crypto::protect(plain, sealed, /*machine=*/false, &w);
    if (!ok) w = "le mot de passe d'application ne peut pas \xC3\xAAtre prot\xC3\xA9g\xC3\xA9 sur ce poste (" + w + ")";
    if (ok) ok = writePrivate(passwordPath(folder), sealed, &w);
    wipe(plain);
    wipe(sealed);
    if (!ok && why) *why = w;
    return ok;
}

bool hasPassword(const std::string& folder) {
    std::error_code ec;
    const fs::path p = P(passwordPath(folder));
    return fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 0 && !ec;
}

bool forget(const std::string& folder, std::string* why) {
    bool ok = true;
    std::string w;
    std::lock_guard<std::mutex> lock(microsoftLock());
    for (const std::string& path : {configPath(folder), passwordPath(folder), microsoftPath(folder)}) {
        std::error_code ec;
        fs::remove(P(path), ec);
        if (ec || isFile(path)) {
            ok = false;
            if (w.empty()) w = "impossible d'effacer " + path + (ec ? " (" + ec.message() + ")" : std::string());
        }
    }
    if (!ok && why) *why = w;
    return ok;
}

bool readyToSend(const std::string& folder) {
    Config c;
    if (!isFile(configPath(folder)) || !loadConfig(folder, c, nullptr)) return false;
    return c.auth == kAuthMicrosoft ? microsoftConnected(folder) : hasPassword(folder);
}

// ============================================================================ 1.38.0 : le compte Microsoft
bool microsoftStart(const Config& in, DeviceCode& out, std::string* why, int timeoutMs) {
    const Config c = normalized(in);
    std::string w;
    if (!checkMicrosoft(c, /*requireId=*/true, &w)) return fail(why, "Les r\xC3\xA9glages du compte Microsoft sont refus\xC3\xA9s : " + w + ".");
    std::string body;
    appendField(body, "client_id", c.clientId);
    appendField(body, "scope", kSignInScope);
    const OAuthReply r = postForm(endpoint(c, "devicecode"), body, timeoutMs, /*french=*/true);
    if (!r.answered) return fail(why, unreachable(c, r.failure, "pour commencer la connexion", "v\xC3\xA9rifie ta connexion \xC3\xA0 Internet"));
    if (r.status != 200 || !r.json.isObject() || r.json.has("error")) return fail(why, oauthRefusal(c, r, oauthError(r, {}), Stage::Start));
    DeviceCode dc;
    dc.userCode = trimmed(r.json.str("user_code"));
    dc.verificationUri = trimmed(r.json.str("verification_uri"));
    if (dc.verificationUri.empty()) dc.verificationUri = trimmed(r.json.str("verification_url"));   // l'ancienne autorite (v1)
    dc.message = clipUtf8(oneLine(r.json.str("message")), 600);
    dc.deviceCode = r.json.str("device_code");
    dc.interval = static_cast<int>(std::clamp<long long>(intOf(r.json, "interval", 5), 1, 60));
    dc.expiresIn = static_cast<int>(std::clamp<long long>(intOf(r.json, "expires_in", 900), 60, 3600));
    // Ce qui sera montre : un code court (lettres, chiffres, -) et une adresse https (http:// vers ce poste : les essais).
    const bool codeOk = !dc.userCode.empty() && dc.userCode.size() <= 64 &&
                        std::all_of(dc.userCode.begin(), dc.userCode.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '-'; });
    app::ai::Url u;
    const bool uriOk = !dc.verificationUri.empty() && dc.verificationUri.size() <= 300 && isAscii(dc.verificationUri) &&
                       !hasControl(dc.verificationUri) && dc.verificationUri.find(' ') == std::string::npos &&
                       app::ai::parseUrl(dc.verificationUri, u, nullptr);
    if (!codeOk || !uriOk || !tokenText(dc.deviceCode)) {
        wipe(dc.deviceCode);
        return fail(why, "Microsoft a r\xC3\xA9pondu sans code de connexion lisible : r\xC3\xA9" "essaie dans quelques minutes.");
    }
    out = std::move(dc);
    return true;
}

std::string microsoftInstructions(const DeviceCode& dc) {
    const int minutes = std::max(1, (dc.expiresIn + 59) / 60);
    return "Ouvre " + dc.verificationUri + " dans ton navigateur, tape le code " + dc.userCode +
           ", puis connecte-toi avec la bo\xC3\xAEte qui enverra les mails (le code est valable " + std::to_string(minutes) + " min).";
}

PollResult microsoftPoll(const std::string& folder, const Config& in, const DeviceCode& dc, std::string* why, int timeoutMs) {
    PollResult res;
    const auto end = [&](PollState state, std::string text) {
        res.state = state;
        res.message = std::move(text);
        if (why && state != PollState::Pending && state != PollState::Done) *why = res.message;
        return res;
    };
    const Config c = normalized(in);
    std::string w;
    if (!checkMicrosoft(c, /*requireId=*/true, &w)) return end(PollState::Error, "Les r\xC3\xA9glages du compte Microsoft sont refus\xC3\xA9s : " + w + ".");
    if (!tokenText(dc.deviceCode))
        return end(PollState::Error, "Aucune connexion \xC3\xA0 Microsoft n'est en cours : recommence (" + std::string(kSignInPath) + ").");
    (void)crypto::init();
    std::string body;
    body.reserve(dc.deviceCode.size() * 3 + 192);
    appendField(body, "grant_type", kDeviceGrant);
    appendField(body, "client_id", c.clientId);
    appendField(body, "device_code", dc.deviceCode);
    const OAuthReply r = postForm(endpoint(c, "token"), body, timeoutMs, false);
    if (!r.answered)
        return end(PollState::Error, unreachable(c, r.failure, "pendant la connexion", "v\xC3\xA9rifie ta connexion \xC3\xA0 Internet, puis recommence"));
    const int interval = std::clamp(dc.interval, 1, 60);
    if (r.status == 200 && r.json.isObject() && !r.json.has("error")) {
        Tokens t;
        if (!readTokens(r.json, t, /*needRefresh=*/true, &w)) return end(PollState::Error, "La connexion \xC3\xA0 Microsoft a \xC3\xA9" "chou\xC3\xA9 : " + w + ".");
        Stored st;
        st.account = t.account;
        st.clientId = c.clientId;
        st.tenant = c.tenant;
        st.authority = c.authority;
        st.refresh = t.refresh;
        st.since = static_cast<std::int64_t>(std::time(nullptr));
        {
            std::lock_guard<std::mutex> lock(microsoftLock());
            if (!writeStored(folder, st, &w))
                return end(PollState::Error, "La connexion \xC3\xA0 Microsoft a r\xC3\xA9ussi, mais elle ne peut pas \xC3\xAAtre gard\xC3\xA9" "e sur ce poste (" + w + ") : recommence.");
        }
        res.account = st.account;
        std::string text = st.account.empty() ? std::string("La connexion \xC3\xA0 Microsoft est faite : les mails partiront de ta bo\xC3\xAEte.")
                                              : "La connexion \xC3\xA0 Microsoft est faite (compte " + st.account + ") : les mails partiront de cette bo\xC3\xAEte.";
        if (!st.account.empty() && !c.user.empty() && lower(st.account) != lower(c.user))
            text += " Attention : l'identifiant SMTP r\xC3\xA9gl\xC3\xA9 est " + c.user + " ; mets plut\xC3\xB4t " + st.account + ", l'adresse du compte connect\xC3\xA9.";
        return end(PollState::Done, std::move(text));
    }
    const OAuthError e = oauthError(r, {&dc.deviceCode});
    if (e.code == "authorization_pending") {
        res.waitSeconds = interval;
        return end(PollState::Pending, "Microsoft attend que tu tapes le code dans ton navigateur.");
    }
    if (e.code == "slow_down") {
        res.waitSeconds = std::min(interval + 5, 120);
        res.slowDown = true;
        return end(PollState::Pending, "Microsoft demande d'espacer les v\xC3\xA9rifications : la prochaine dans " + std::to_string(res.waitSeconds) + " s.");
    }
    if (e.code == "authorization_declined" || e.code == "access_denied" || e.aadsts == 65004)
        return end(PollState::Refused, "Tu as refus\xC3\xA9 la connexion dans le navigateur : rien n'est enregistr\xC3\xA9 (recommence quand tu veux : " +
                                           std::string(kSignInPath) + ").");
    if (e.code == "expired_token" || e.code == "code_expired")
        return end(PollState::Expired, "Le code de connexion a expir\xC3\xA9 : recommence (" + std::string(kSignInPath) + ").");
    return end(PollState::Error, oauthRefusal(c, r, e, Stage::Poll));
}

PollResult microsoftWait(const std::string& folder, const Config& c, DeviceCode dc, const std::function<bool()>& stop, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::seconds(std::max(1, dc.expiresIn));
    int wait = std::clamp(dc.interval, 1, 60);
    for (;;) {
        const auto next = Clock::now() + std::chrono::seconds(wait);
        while (Clock::now() < next) {
            if (stop && stop()) {
                PollResult r;
                r.state = PollState::Cancelled;
                r.message = "La connexion \xC3\xA0 Microsoft est arr\xC3\xAAt\xC3\xA9" "e : rien n'est enregistr\xC3\xA9.";
                return r;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (Clock::now() >= deadline) {
            PollResult r;
            r.state = PollState::Expired;
            r.message = "Le code de connexion a expir\xC3\xA9 : recommence (" + std::string(kSignInPath) + ").";
            return r;
        }
        PollResult r = microsoftPoll(folder, c, dc, nullptr, timeoutMs);
        if (r.state != PollState::Pending) return r;
        wait = std::clamp(r.waitSeconds, 1, 120);
        dc.interval = wait;
    }
}

bool microsoftConnected(const std::string& folder) {
    std::error_code ec;
    const fs::path p = P(microsoftPath(folder));
    return fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 0 && !ec;
}

bool microsoftLink(const std::string& folder, MicrosoftLink& out, std::string* why) {
    std::lock_guard<std::mutex> lock(microsoftLock());
    Stored st;
    if (!readStored(folder, st, why)) return false;
    out.account = st.account;
    out.clientId = st.clientId;
    out.tenant = st.tenant;
    out.authority = st.authority;
    out.since = st.since;
    return true;
}

bool microsoftForget(const std::string& folder, std::string* why) {
    std::lock_guard<std::mutex> lock(microsoftLock());
    const std::string path = microsoftPath(folder);
    std::error_code ec;
    fs::remove(P(path), ec);
    if (ec || isFile(path)) return fail(why, "impossible d'effacer " + path + (ec ? " (" + ec.message() + ")" : std::string()));
    return true;
}

// ============================================================================ l'envoi
SendResult sendWith(const Config& in, const std::string& password, const Message& m, int timeoutMs) {
    SendResult r;
    const Config c = normalized(in);
    if (!precheck(c, m, r)) return r;
    if (c.auth == kAuthMicrosoft) {
        r.message = "Ces r\xC3\xA9glages s'identifient par un compte Microsoft, pas par un mot de passe : le courriel part avec la connexion "
                    "Microsoft enregistr\xC3\xA9" "e (" + std::string(kSignInPath) + ").";
        return r;
    }
    if (password.empty()) {
        r.message = "Le mot de passe d'application est vide : saisis-le dans les r\xC3\xA9glages du courriel.";
        return r;
    }
    if (hasControl(password)) {
        r.message = "Le mot de passe d'application contient un retour \xC3\xA0 la ligne ou un caract\xC3\xA8re invisible : saisis-le de nouveau.";
        return r;
    }
    return deliver(c, password, m, timeoutMs, /*oauth=*/false, {});
}

SendResult sendWithToken(const Config& in, const std::string& accessToken, const Message& m, int timeoutMs) {
    SendResult r;
    const Config c = normalized(in);
    if (!precheck(c, m, r)) return r;
    if (!tokenText(accessToken)) {
        r.message = "Le jeton d'acc\xC3\xA8s est vide ou illisible : rien n'est envoy\xC3\xA9.";
        return r;
    }
    return deliver(c, accessToken, m, timeoutMs, /*oauth=*/true, {});
}

SendResult send(const std::string& folder, const Message& m, int timeoutMs) {
    SendResult r;
    Config c;
    std::string why;
    if (!isFile(configPath(folder))) {
        r.message = "Le courriel n'est pas r\xC3\xA9gl\xC3\xA9 sur ce poste : r\xC3\xA8gle ton serveur SMTP, ton adresse et ton mot de passe d'application.";
        return r;
    }
    if (!loadConfig(folder, c, &why)) {
        r.message = "Les r\xC3\xA9glages du courriel sont illisibles (" + why + ") : r\xC3\xA8gle-les de nouveau.";
        return r;
    }
    if (c.auth == kAuthMicrosoft) return sendMicrosoft(folder, c, m, timeoutMs);   // 1.38.0
    std::string password;
    if (!readPassword(folder, password, &why)) {
        r.message = why;
        return r;
    }
    r = sendWith(c, password, m, timeoutMs);
    wipe(password);
    return r;
}

} // namespace license_admin::mail
