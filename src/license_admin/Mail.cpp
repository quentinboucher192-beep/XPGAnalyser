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
    return c;
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
    Session(const Config& c, int timeoutMs) : c_(c), t_(timeoutMs) {}

    Attempt run(const std::string& password, const std::string& data, const std::string& to, const std::vector<Bytes>& extraRoots) {
        Attempt a;
        a.result = steps(password, data, to, extraRoots, a);
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

    // Le texte d'un serveur montre a l'editeur : jamais s'il contient le mot de passe (ou sa forme base64).
    static std::string shown(const Reply& r, const std::string& password) {
        const std::string t = r.text();
        if (password.empty()) return t;
        std::string b64 = crypto::base64(password);
        const bool leak = t.find(password) != std::string::npos || t.find(b64) != std::string::npos;
        wipe(b64);
        return leak ? std::to_string(r.code) : t;
    }

    SendResult authRefused(const Reply& r, const std::string& password) const {
        const std::string said = shown(r, password);
        std::string low = lower(said);
        // Microsoft (Outlook, Hotmail, Microsoft 365) : l'identification par mot de passe est retiree, OAuth seulement.
        if (low.find("basic authentication is disabled") != std::string::npos || low.find("5.7.139") != std::string::npos)
            return failure("Le serveur " + where() + " refuse toute identification par mot de passe pour ce compte (Microsoft exige "
                           "d\xC3\xA9sormais OAuth, que cet outil ne g\xC3\xA8re pas) : utilise une autre bo\xC3\xAEte, par exemple Gmail avec un "
                           "mot de passe d'application ; sa r\xC3\xA9ponse : " + cite(said) + ".");
        return failure("Le serveur refuse l'identifiant ou le mot de passe d'application : pour Gmail, il faut un mot de passe d'application "
                       "(compte Google \xE2\x80\xBA S\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Validation en deux \xC3\xA9tapes) ; sa r\xC3\xA9ponse : " + cite(said) + ".");
    }

    bool authenticate(const std::string& password, const Reply& ehlo, SendResult& out) {
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
            out = failure("Le serveur " + where() + " ne propose ni AUTH PLAIN ni AUTH LOGIN (il exige sans doute OAuth, que cet outil ne g\xC3\xA8re pas) : ce poste ne sait pas s'y identifier par mot de passe.");
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
                out = authRefused(r, password);
                return false;
            }
            if (ok) ok = command(crypto::base64(c_.user), r, "de l'identification", out);
            if (ok && r.code != 334) {
                out = authRefused(r, password);
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
            out = authRefused(r, password);
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
        if (r.code != 220) return failure("Le serveur " + h + " refuse la connexion ; sa r\xC3\xA9ponse : " + cite(shown(r, password)) + ".");
        const std::string hello = "EHLO " + ehloName();
        if (!command(hello, r, "EHLO", out)) return out;
        if (r.code != 250) return failure("Le serveur " + h + " refuse EHLO ; sa r\xC3\xA9ponse : " + cite(shown(r, password)) + ".");
        if (!implicitTls) {
            if (!r.capability("STARTTLS"))
                return failure("Le serveur " + h + " ne propose pas STARTTLS sur le port " + std::to_string(c_.port) +
                               " : rien n'est envoy\xC3\xA9 sans chiffrement (si ton fournisseur l'indique, choisis TLS sur le port 465).");
            if (!command("STARTTLS", r, "STARTTLS", out)) return out;
            if (r.code != 220)
                return failure("Le serveur " + h + " refuse STARTTLS (sa r\xC3\xA9ponse : " + cite(shown(r, password)) + ") : rien n'est envoy\xC3\xA9 sans chiffrement.");
            link_.discardInput();
            const int rc = link_.handshake(c_.host, t_, &why);
            if (rc != 0) return tlsFailure(rc, why, a);
            if (!command(hello, r, "EHLO", out)) return out;
            if (r.code != 250) return failure("Le serveur " + h + " refuse EHLO ; sa r\xC3\xA9ponse : " + cite(shown(r, password)) + ".");
        }
        if (!authenticate(password, r, out)) return out;
        if (!command("MAIL FROM:<" + c_.from + ">", r, "MAIL FROM", out)) return out;
        if (r.code != 250)
            return failure("Le serveur refuse l'exp\xC3\xA9" "diteur " + cite(c_.from) + " (sa r\xC3\xA9ponse : " + cite(shown(r, password)) +
                           ") : l'adresse d'exp\xC3\xA9" "dition doit \xC3\xAAtre celle de ton compte, ou un alias autoris\xC3\xA9.");
        if (!command("RCPT TO:<" + to + ">", r, "RCPT TO", out)) return out;
        if (r.code != 250 && r.code != 251)
            return failure("Le serveur refuse le destinataire " + cite(to) + " (sa r\xC3\xA9ponse : " + cite(shown(r, password)) + ") : v\xC3\xA9rifie son adresse.");
        if (!command("DATA", r, "DATA", out)) return out;
        if (r.code != 354) return failure("Le serveur refuse l'envoi du message ; sa r\xC3\xA9ponse : " + cite(shown(r, password)) + ".");
        io = link_.send(dotStuffed(data), t_);
        if (io == Io::Ok) io = link_.reply(r, t_);
        if (io != Io::Ok) return ioFailure(io, "de l'envoi du message");
        if (r.code != 250) return failure("Le serveur a refus\xC3\xA9 le message ; sa r\xC3\xA9ponse : " + cite(shown(r, password)) + ".");
        // QUIT : le message est deja accepte, une panne ici ne change rien.
        if (link_.send("QUIT\r\n", std::min(t_, 5000)) == Io::Ok) (void)link_.reply(r, std::min(t_, 5000));
        SendResult ok;
        ok.ok = true;
        ok.reached = true;
        ok.message = "Le courriel est parti vers " + to + ".";
        return ok;
    }

    const Config& c_;
    int           t_;
    Link          link_;
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
    return true;
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
    for (const std::string& path : {configPath(folder), passwordPath(folder)}) {
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

// ============================================================================ l'envoi
SendResult sendWith(const Config& in, const std::string& password, const Message& m, int timeoutMs) {
    SendResult r;
    const Config c = normalized(in);
    std::string why;
    if (!validate(c, &why)) {
        r.message = "Les r\xC3\xA9glages du courriel sont refus\xC3\xA9s : " + why + ".";
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
    const std::string to = trimmed(m.to);
    if (!plausibleAddress(to)) {
        r.message = "L'adresse du destinataire " + cite(oneLine(to)) + " n'est pas valable : rien n'est envoy\xC3\xA9.";
        return r;
    }
    if (m.body.size() > kMaxBody) {
        r.message = "Le texte du courriel est trop long (4 Mo au plus) : rien n'est envoy\xC3\xA9.";
        return r;
    }
    if (!crypto::init()) {
        r.message = "Le hasard du syst\xC3\xA8me est indisponible (libsodium) : rien n'est envoy\xC3\xA9.";
        return r;
    }
    const int t = timeoutMs > 0 ? std::max(500, timeoutMs) : kDefaultTimeoutMs;
    const std::string data = composeMessage(c, m);
    std::vector<Bytes> extra;
    Attempt a = Session(c, t).run(password, data, to, extra);
#if defined(_WIN32)
    // Une autorite que le magasin ROOT n'a pas encore : Windows verifie la chaine (et peut la telecharger) ; s'il la
    // reconnait pour ce nom, sa racine est ajoutee et mbedTLS verifie de nouveau, tout.
    if (!a.result.ok && a.untrustedOnly && !a.chain.empty()) {
        Bytes root;
        if (windowsRootFor(a.chain, c.host, t, root)) {
            extra.push_back(std::move(root));
            a = Session(c, t).run(password, data, to, extra);
        }
    }
#endif
    return a.result;
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
