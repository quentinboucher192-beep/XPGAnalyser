// =============================================================================
//  tests/admin_mail_test.cpp - 1.37.0 : LE COURRIEL DE L'EDITEUR
//  (license_admin/Mail), contre un VRAI serveur SMTP lance sur ce poste
//  (tests/outils/smtp_essai.py, Python 3 et sa bibliotheque standard)
// -----------------------------------------------------------------------------
//  Sans reseau d'abord : l'adresse plausible, la configuration (refus, aller-
//  retour, aucun secret dans mail.json, 0600), le mot de passe d'application
//  (protege par DPAPI sous Windows : pas en clair ; ailleurs 0600 ; relu tel
//  quel), oublier, les refus avant toute connexion.
//  Puis contre le serveur d'essai (127.0.0.1, ports libres) : un envoi par
//  STARTTLS (TLS 1.3, AUTH PLAIN) et un par TLS des la connexion (TLS 1.2,
//  AUTH LOGIN) - ce que le serveur a recu : les en-tetes (From et To avec un
//  nom accentue, le sujet accentue en mots encodes, Date, Message-ID, MIME), le
//  corps en base64 (lignes de 76) qui redonne le texte UTF-8 ; puis les refus,
//  chacun avec sa phrase : certificat d'aucune autorite (et l'autorite d'essai
//  absente), nom du serveur qui ne correspond pas, mot de passe faux,
//  destinataire refuse, pas de STARTTLS (rien en clair), TLS sur un port en
//  clair, serveur muet (delai), port ferme (reached faux). Le mot de passe
//  n'apparait dans aucun message.
//  Un seul compte, FICTIF (essai@exemple.invalid / mdp-essai-local), des
//  certificats d'essai (tests/fixtures/mail) ; aucun vrai serveur n'est joint.
//
//  1.38.0 : LE COMPTE MICROSOFT (OAuth 2.0, XOAUTH2). Sans reseau : un mail.json
//  de 1.37 se relit et se reecrit a l'identique (aucune nouvelle cle), les
//  nouvelles cles (authentification, id_application, locataire, autorite) en
//  aller-retour, les refus (identification, GUID, locataire, autorite https ou
//  ce poste seulement, identifiant qui n'est pas une adresse), les reglages tout
//  faits (Outlook.com, Microsoft 365), pas encore connecte (la phrase), oublier.
//  Puis contre un FAUX serveur OAuth (tests/outils/oauth_essai.py, 127.0.0.1) et
//  le serveur SMTP d'essai (XOAUTH2) : le code de l'appareil (les champs du
//  formulaire : client_id, scope, grant_type), en attente puis connecte, le jeton
//  de rafraichissement garde (DPAPI sous Windows : pas en clair ; ailleurs 0600),
//  microsoftWait et son arret, slow_down, refuse, expire, les flux clients
//  publics, l'application inconnue, sans SMTP.Send, un autre compte, la panne ;
//  un vrai envoi XOAUTH2 avec le jeton d'acces rafraichi, la rotation gardee,
//  l'ancien jeton refuse (invalid_grant : << reconnecte-toi >>, la connexion
//  oubliee), le consentement manquant, l'application changee, les refus du
//  serveur SMTP (le defi d'erreur 334 puis 535, SMTP AUTH coupe 5.7.139, sans
//  XOAUTH2), l'autorite injoignable. AUCUN jeton ni code (tous en essai_jeton_)
//  dans aucune phrase. ID d'application FICTIF : 00000000-0000-4000-8000-0000000000e5.
//
//    admin_mail_test SOURCE
//  Sans python3 (ou hors Linux/macOS) : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "../src/app/ai/AiHttp.hpp"   // 1.38.0 : httpAvailable (libcurl sur ce poste ?)
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Mail.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace json = core::json;
namespace mail = license_admin::mail;
namespace crypto = deploy::crypto;

namespace {

constexpr const char* kUser = "essai@exemple.invalid";
constexpr const char* kPassword = "mdp-essai-local";
constexpr const char* kClientId = "00000000-0000-4000-8000-0000000000e5";   // 1.38.0 : un ID d'application FICTIF
constexpr const char* kSecretMark = "essai_jeton_";                         // tous les jetons et codes d'essai

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}
bool waitFor(const std::function<bool()>& pred, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return pred();
}
#if !defined(_WIN32)
bool ownerOnly(const fs::path& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600;
}
#endif

// ---- lire un message recu : les en-tetes (deplies), le corps ----
struct Parsed {
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::string>                         headLines;   // les lignes physiques de l'en-tete
    std::string                                      body;
    bool                                             ok{false};
};
Parsed parseMail(const std::string& data) {
    Parsed p;
    const auto sep = data.find("\r\n\r\n");
    if (sep == std::string::npos) return p;
    const std::string head = data.substr(0, sep);
    p.body = data.substr(sep + 4);
    std::size_t at = 0;
    while (at <= head.size()) {
        auto eol = head.find("\r\n", at);
        if (eol == std::string::npos) eol = head.size();
        const std::string line = head.substr(at, eol - at);
        p.headLines.push_back(line);
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
            if (!p.headers.empty()) p.headers.back().second += line;   // deplier : la fin de ligne seule disparait
        } else {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string value = line.substr(colon + 1);
                if (!value.empty() && value[0] == ' ') value.erase(0, 1);
                p.headers.emplace_back(line.substr(0, colon), value);
            }
        }
        at = eol + 2;
    }
    p.ok = true;
    return p;
}
std::string header(const Parsed& p, const std::string& name) {
    for (const auto& h : p.headers)
        if (h.first == name) return h.second;
    return "\x01" "absent";
}
int headerCount(const Parsed& p, const std::string& name) {
    int n = 0;
    for (const auto& h : p.headers) n += h.first == name ? 1 : 0;
    return n;
}
// Les mots encodes =?UTF-8?B?...?= decodes (l'espace entre deux mots encodes ne compte pas, RFC 2047).
std::string decodeWords(const std::string& v) {
    std::string out, pending;
    bool lastWord = false;
    std::size_t i = 0;
    while (i < v.size()) {
        if (v.compare(i, 10, "=?UTF-8?B?") == 0) {
            const auto end = v.find("?=", i + 10);
            if (end == std::string::npos) return "\x01mot mal forme";
            crypto::Bytes raw;
            if (!crypto::unbase64(v.substr(i + 10, end - i - 10), raw)) return "\x01" "base64 mal forme";
            if (!lastWord) out += pending;
            pending.clear();
            out.append(raw.begin(), raw.end());
            i = end + 2;
            lastWord = true;
            continue;
        }
        if (v[i] == ' ' || v[i] == '\t') {
            pending += v[i++];
            continue;
        }
        out += pending;
        pending.clear();
        out += v[i++];
        lastWord = false;
    }
    return out + pending;
}
std::string crlf(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}
bool asciiOnly(const std::string& s) {
    for (char c : s)
        if (static_cast<unsigned char>(c) >= 0x80) return false;
    return true;
}

// ---- un serveur d'essai en Python (tests/outils) : python3 SCRIPT ARGS... SORTIE PORTS ----
//   smtp_essai.py FIXTURES SORTIE PORTS : le serveur SMTP (STARTTLS, TLS, refus ; 1.38.0 : XOAUTH2)
//   oauth_essai.py SORTIE PORTS         : 1.38.0, le faux serveur OAuth 2.0 (comme login.microsoftonline.com)
class TestServer {
public:
    TestServer(const std::string& source, const fs::path& dir, const std::string& script, const std::vector<std::string>& args) : dir_(dir) {
#if defined(_WIN32)
        (void)source;
        (void)script;
        (void)args;
        skipped_ = true;
        error_ = "les serveurs d'essai ne sont lances que sous Linux/macOS (python3)";
#else
        const std::string path = U(fs::path(source) / "tests" / "outils" / script);
        out_ = U(dir / "journal.json");
        const std::string ports = U(dir / "ports.json");
        const std::string log = U(dir / "serveur.log");
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!fs::exists(path, ec)) {
            error_ = "introuvable : " + path;
            return;
        }
        std::vector<std::string> argv = {"python3", path};
        argv.insert(argv.end(), args.begin(), args.end());
        argv.push_back(out_);
        argv.push_back(ports);
        std::vector<char*> cargv;
        for (auto& a : argv) cargv.push_back(a.data());
        cargv.push_back(nullptr);
        const pid_t pid = ::fork();
        if (pid < 0) {
            error_ = "fork impossible";
            return;
        }
        if (pid == 0) {
            const int fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd >= 0) {
                ::dup2(fd, 1);
                ::dup2(fd, 2);
                ::close(fd);
            }
            ::execvp("python3", cargv.data());
            ::_exit(127);
        }
        pid_ = pid;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < end) {
            json::Value v;
            if (fs::exists(ports, ec) && json::parse(readFile(ports), v, nullptr) && v.isObject()) {
                for (const auto& m : v.members()) ports_[m.first] = static_cast<int>(m.second.asInt());
                ready_ = true;
                return;
            }
            int status = 0;
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                pid_ = -1;
                const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                if (code == 127 || code == 3) {
                    skipped_ = true;
                    error_ = code == 127 ? "python3 est introuvable sur ce poste" : "le module ssl de Python manque sur ce poste";
                } else {
                    error_ = script + " s'est arrete (code " + std::to_string(code) + ") : " + readFile(log);
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        error_ = script + " n'a pas demarre en 20 s : " + readFile(log);
#endif
    }
    ~TestServer() { stop(); }
    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    // Arreter (SIGTERM). Vrai : il s'est arrete de lui-meme (code 0).
    bool stop() {
#if !defined(_WIN32)
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            int status = 0;
            for (int i = 0; i < 100; ++i) {
                if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                    pid_ = -1;
                    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
        }
#endif
        return false;
    }
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] bool skipped() const noexcept { return skipped_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] int port(const std::string& name) const {
        const auto it = ports_.find(name);
        return it == ports_.end() ? 0 : it->second;
    }
    // Le journal du serveur : SMTP "messages" et "evenements" ; OAuth "requetes".
    [[nodiscard]] json::Value journal() const {
        json::Value v;
        if (!json::parse(readFile(out_), v, nullptr) || !v.isObject()) return json::Value::object();
        return v;
    }
    [[nodiscard]] std::vector<json::Value> messages(const std::string& listener = "") const {
        std::vector<json::Value> out;
        const json::Value j = journal();
        for (const auto& m : j.at("messages").items())
            if (listener.empty() || m.str("ecoute") == listener) out.push_back(m);
        return out;
    }
    [[nodiscard]] bool event(const std::string& listener, const std::string& what) const {
        const json::Value j = journal();
        for (const auto& e : j.at("evenements").items())
            if (e.str("ecoute") == listener && e.str("evenement") == what) return true;
        return false;
    }
    // 1.38.0 : les requetes recues par le faux serveur OAuth ("devicecode", "token" ; vide : toutes).
    [[nodiscard]] std::vector<json::Value> requests(const std::string& point = "") const {
        std::vector<json::Value> out;
        const json::Value j = journal();
        for (const auto& r : j.at("requetes").items())
            if (point.empty() || r.str("point") == point) out.push_back(r);
        return out;
    }

private:
    fs::path                   dir_;
    std::string                out_;
    std::map<std::string, int> ports_;
    bool                       ready_{false};
    bool                       skipped_{false};
    std::string                error_;
#if !defined(_WIN32)
    pid_t pid_{-1};
#endif
};

#if !defined(_WIN32)
// Un port de 127.0.0.1 ou rien n'ecoute (pris puis rendu).
int closedPort() {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 1;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    socklen_t len = sizeof a;
    int port = 1;
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len) == 0)
        port = ntohs(a.sin_port);
    ::close(s);
    return port;
}
#endif

mail::Config baseConfig(const std::string& source) {
    mail::Config c;
    c.host = "localhost";
    c.port = 587;
    c.security = "starttls";
    c.from = kUser;
    c.fromName = "\xC3\x89" "diteur XPG";
    c.user = kUser;
    c.caFile = U(fs::path(source) / "tests" / "fixtures" / "mail" / "ac-essai.pem");
    return c;
}

// 1.38.0 : une boite Microsoft d'essai (l'autorite : le faux serveur OAuth ; vide : la vraie, jamais jointe ici).
mail::Config msConfig(const std::string& source, const std::string& authority, const std::string& tenant = "consumers") {
    mail::Config c = baseConfig(source);
    c.auth = "microsoft";
    c.clientId = kClientId;
    c.tenant = tenant;
    c.authority = authority;
    return c;
}
// La connexion gardee (mail-microsoft.bin), dechiffree : L'ESSAI SEULEMENT (le module ne rend jamais le jeton).
json::Value storedLink(const std::string& folder) {
    crypto::Bytes sealed, plain;
    json::Value v;
    if (!crypto::readFile(mail::microsoftPath(folder), sealed, nullptr) || !crypto::unprotect(sealed, plain, nullptr)) return v;
    if (!json::parse(std::string(plain.begin(), plain.end()), v, nullptr) || !v.isObject()) return json::Value();
    return v;
}
// Poser une connexion gardee (le format de mail-microsoft.bin) avec ce jeton de rafraichissement.
bool plantLink(const std::string& folder, const std::string& refresh, const std::string& authority, const std::string& tenant = "consumers") {
    json::Value v = json::Value::object();
    v.set("format", "xpg-courriel-microsoft/1");
    v.set("id_application", kClientId);
    v.set("locataire", tenant);
    if (!authority.empty()) v.set("autorite", authority);
    v.set("compte", kUser);
    v.set("depuis", 1);
    v.set("jeton", refresh);
    const std::string text = json::write(v, false);
    crypto::Bytes plain(text.begin(), text.end()), sealed;
    std::error_code ec;
    fs::create_directories(fs::path(folder), ec);
    return crypto::protect(plain, sealed, false, nullptr) && crypto::writePrivateFile(mail::microsoftPath(folder), sealed, nullptr);
}
std::vector<std::string> memberNames(const json::Value& v) {
    std::vector<std::string> out;
    for (const auto& m : v.members()) out.push_back(m.first);
    return out;
}

// ============================================================================ 1.38.0 : le compte Microsoft, sans reseau
void offlineMicrosoft(const std::string& source, const fs::path& tmp) {
    std::printf("-- 1.38.0 : le compte Microsoft (reglages, refus, pas encore connecte)\n");
    const std::string F = U(tmp / "editeur-ms");
    std::string why;

    // ---- un mail.json de 1.37 : relu tel quel (mot de passe d'application), reecrit sans aucune nouvelle cle
    writeFile(mail::configPath(F), "{\n  \"format\": \"xpg-courriel-editeur/1\",\n  \"hote\": \"smtp.gmail.com\",\n  \"port\": 587,\n"
                                   "  \"securite\": \"starttls\",\n  \"expediteur\": \"essai@exemple.invalid\",\n  \"nom\": \"Editeur XPG\",\n"
                                   "  \"identifiant\": \"essai@exemple.invalid\"\n}\n");
    mail::Config old;
    check(mail::loadConfig(F, old, &why), "un mail.json de 1.37 se relit : " + why);
    check(old.auth == "motdepasse" && old.clientId.empty() && old.tenant == "common" && old.authority.empty() && old.host == "smtp.gmail.com" &&
              old.port == 587 && old.security == "starttls" && old.from == kUser && old.user == kUser && old.fromName == "Editeur XPG",
          "1.37 : tout est relu, l'identification par mot de passe et les defauts de Microsoft");
    check(mail::saveConfig(F, old, &why), "1.37 : reecrit : " + why);
    {
        json::Value v;
        check(json::parse(readFile(mail::configPath(F)), v, nullptr) &&
                  memberNames(v) == std::vector<std::string>{"format", "hote", "port", "securite", "expediteur", "nom", "identifiant"},
              "1.37 reecrit : exactement les cles de 1.37 (aucune nouvelle)");
    }

    // ---- les reglages tout faits
    const mail::Config outlook = mail::microsoftPreset(true, baseConfig(source));
    check(outlook.host == "smtp-mail.outlook.com" && outlook.port == 587 && outlook.security == "starttls" && outlook.auth == "microsoft" &&
              outlook.tenant == "consumers" && outlook.from == kUser && outlook.user == kUser && outlook.fromName == baseConfig(source).fromName,
          "microsoftPreset(personnel) : Outlook.com, Hotmail (587, STARTTLS, consumers), le reste garde");
    const mail::Config m365 = mail::microsoftPreset(false);
    check(m365.host == "smtp.office365.com" && m365.port == 587 && m365.security == "starttls" && m365.auth == "microsoft" && m365.tenant == "organizations",
          "microsoftPreset(Microsoft 365) : smtp.office365.com, 587, STARTTLS, organizations");
    check(mail::microsoftHost("smtp-mail.outlook.com") && mail::microsoftHost(" SMTP.Office365.com. ") && mail::microsoftHost("smtp.live.com") &&
              mail::microsoftHost("hotmail.com") && !mail::microsoftHost("smtp.gmail.com") && !mail::microsoftHost("notoutlook.com") &&
              !mail::microsoftHost("outlook.com.exemple.invalid") && !mail::microsoftHost(""),
          "microsoftHost : les serveurs de Microsoft (et seulement eux)");

    // ---- les nouvelles cles : l'aller-retour (normalisees)
    mail::Config ms = outlook;
    ms.clientId = "  00000000-0000-4000-8000-0000000000E5 ";
    ms.tenant = " Consumers ";
    ms.authority = "HTTP://127.0.0.1:8080/";
    ms.auth = " Microsoft ";
    check(mail::validate(ms, &why), "Microsoft valide : " + why);
    check(mail::saveConfig(F, ms, &why), "Microsoft enregistre : " + why);
    {
        json::Value v;
        check(json::parse(readFile(mail::configPath(F)), v, nullptr) && v.str("authentification") == "microsoft" && v.str("id_application") == kClientId &&
                  v.str("locataire") == "consumers" && v.str("autorite") == "http://127.0.0.1:8080",
              "mail.json : authentification, id_application (minuscules), locataire, autorite (sans / final)");
    }
    mail::Config back;
    check(mail::loadConfig(F, back, &why) && back.auth == "microsoft" && back.clientId == kClientId && back.tenant == "consumers" &&
              back.authority == "http://127.0.0.1:8080" && back.host == "smtp-mail.outlook.com",
          "Microsoft : l'aller-retour garde tout");
    ms.tenant = "common";
    ms.authority.clear();
    check(mail::saveConfig(F, ms, &why), "Microsoft, locataire common, autorite vide : " + why);
    {
        json::Value v;
        check(json::parse(readFile(mail::configPath(F)), v, nullptr) && v.has("authentification") && v.has("id_application") && !v.has("locataire") &&
                  !v.has("autorite"),
              "les defauts ne sont pas ecrits (locataire common, autorite vide)");
    }
    mail::Config pw = baseConfig(source);
    pw.clientId = kClientId;   // garde en revenant au mot de passe
    check(mail::saveConfig(F, pw, &why) && mail::loadConfig(F, back, &why) && back.auth == "motdepasse" && back.clientId == kClientId,
          "motdepasse avec un ID d'application garde : " + why);
    {
        json::Value v;
        check(json::parse(readFile(mail::configPath(F)), v, nullptr) && !v.has("authentification") && v.str("id_application") == kClientId,
              "motdepasse : pas de cle authentification");
    }
    // Ce qui est accepte
    for (const char* tenant : {"consumers", "organizations", "common", "00000000-0000-4000-8000-0000000000aa", "exemple.onmicrosoft.com", "Exemple.FR"}) {
        mail::Config c = msConfig(source, "", tenant);
        check(mail::validate(c, &why), std::string("locataire accepte : ") + tenant + " " + why);
    }
    for (const char* authority : {"", "https://login.microsoftonline.com", "https://login.microsoftonline.us/", "http://localhost:1234", "http://127.0.0.1:1234",
                                  "HTTPS://LOGIN.MICROSOFTONLINE.COM"}) {
        mail::Config c = msConfig(source, authority);
        check(mail::validate(c, &why), std::string("autorite acceptee : ") + authority + " " + why);
    }

    // ---- les refus (rien n'est ecrit)
    struct Bad {
        std::function<void(mail::Config&)> change;
        const char*                        expect;
        const char*                        what;
    };
    const std::vector<Bad> bads = {
        {[](mail::Config& c) { c.auth = "oauth"; }, "l'identification", "identification oauth"},
        {[](mail::Config& c) { c.auth = "mot de passe"; }, "l'identification", "identification inconnue"},
        {[](mail::Config& c) { c.clientId = ""; }, "ID d'application Microsoft est vide", "ID d'application vide"},
        {[](mail::Config& c) { c.clientId = "pas-un-guid"; }, "n'est pas valable", "ID pas un GUID"},
        {[](mail::Config& c) { c.clientId = "{00000000-0000-4000-8000-0000000000e5}"; }, "n'est pas valable", "ID entre accolades"},
        {[](mail::Config& c) { c.clientId = "00000000-0000-4000-8000-0000000000eg"; }, "n'est pas valable", "ID non hexadecimal"},
        {[](mail::Config& c) { c.tenant = "mon annuaire"; }, "locataire", "locataire avec un blanc"},
        {[](mail::Config& c) { c.tenant = "a/b"; }, "locataire", "locataire avec /"},
        {[](mail::Config& c) { c.tenant = "exemple"; }, "locataire", "locataire sans point"},
        {[](mail::Config& c) { c.authority = "http://login.microsoftonline.com"; }, "autorit\xC3\xA9", "autorite http vers un autre poste"},
        {[](mail::Config& c) { c.authority = "http://192.168.1.10:8080"; }, "autorit\xC3\xA9", "autorite http du reseau local"},
        {[](mail::Config& c) { c.authority = "http://127.0.0.2:8080"; }, "autorit\xC3\xA9", "autorite http 127.0.0.2"},
        {[](mail::Config& c) { c.authority = "http://127.0.0.1.exemple.invalid"; }, "autorit\xC3\xA9", "autorite 127.0.0.1.exemple"},
        {[](mail::Config& c) { c.authority = "https://login.microsoftonline.com/common"; }, "autorit\xC3\xA9", "autorite avec un chemin"},
        {[](mail::Config& c) { c.authority = "https://login.microsoftonline.com?x=1"; }, "autorit\xC3\xA9", "autorite avec une requete"},
        {[](mail::Config& c) { c.authority = "ftp://exemple.invalid"; }, "autorit\xC3\xA9", "autorite ftp"},
        {[](mail::Config& c) { c.authority = "http://essai@127.0.0.1:80"; }, "autorit\xC3\xA9", "autorite avec un identifiant"},
        {[](mail::Config& c) { c.authority = "login.microsoftonline.com"; }, "autorit\xC3\xA9", "autorite sans https://"},
        {[](mail::Config& c) { c.user = "essai"; }, "l'adresse de ta bo\xC3\xAEte", "identifiant Microsoft qui n'est pas une adresse"},
    };
    const std::string G = U(tmp / "editeur-ms-refus");
    for (const auto& b : bads) {
        mail::Config c = msConfig(source, "");
        b.change(c);
        const bool saved = mail::saveConfig(G, c, &why);
        check(!saved && has(why, b.expect), std::string("Microsoft refuse (") + b.what + ") : " + why);
    }
    check(!mail::configured(G), "rien n'est ecrit pour un reglage Microsoft refuse");
    {
        mail::Config c = baseConfig(source);   // motdepasse : l'ID d'application facultatif, mais un GUID s'il est donne
        c.clientId = "pas-un-guid";
        check(!mail::validate(c, &why) && has(why, "n'est pas valable"), "motdepasse : un ID d'application faux est refuse aussi : " + why);
    }

    // ---- pas encore connecte
    const std::string H = U(tmp / "editeur-ms-seul");
    check(mail::saveConfig(H, msConfig(source, ""), &why), "reglages Microsoft sans connexion : " + why);
    check(!mail::microsoftConnected(H) && !mail::readyToSend(H), "pas encore connecte : ni connecte ni pret a envoyer");
    const mail::Message small{"client@exemple.invalid", "", "Essai", "Texte\n"};
    {
        const mail::SendResult r = mail::send(H, small, 2000);
        check(!r.ok && !r.reached && has(r.message, "Connecte-toi \xC3\xA0 Microsoft : Service en ligne \xE2\x80\xBA Le mail \xE2\x80\xBA Se connecter"),
              "envoi sans connexion : " + r.message);
        mail::MicrosoftLink link;
        check(!mail::microsoftLink(H, link, &why) && has(why, "Connecte-toi"), "microsoftLink sans connexion : " + why);
        const mail::SendResult r2 = mail::sendWith(msConfig(source, ""), kPassword, small, 2000);
        check(!r2.ok && !r2.reached && has(r2.message, "compte Microsoft"), "sendWith refuse un reglage Microsoft : " + r2.message);
        const mail::SendResult r3 = mail::sendWithToken(msConfig(source, ""), "", small, 2000);
        check(!r3.ok && !r3.reached && has(r3.message, "jeton d'acc\xC3\xA8s est vide"), "sendWithToken sans jeton : " + r3.message);
        const mail::SendResult r4 = mail::sendWithToken(msConfig(source, ""), "essai_jeton_acces_avec\nretour", small, 2000);
        check(!r4.ok && !r4.reached && has(r4.message, "illisible") && !has(r4.message, kSecretMark), "sendWithToken, un jeton sur deux lignes : " + r4.message);
    }
    {
        mail::Config noId = msConfig(source, "");
        noId.clientId.clear();
        mail::DeviceCode dc;
        check(!mail::microsoftStart(noId, dc, &why) && has(why, "ID d'application Microsoft est vide"), "microsoftStart sans ID d'application : " + why);
        mail::Config lan = msConfig(source, "http://192.168.1.10:8080");
        check(!mail::microsoftStart(lan, dc, &why) && has(why, "autorit\xC3\xA9"), "microsoftStart : une autorite http hors de ce poste, refusee avant tout : " + why);
        const mail::PollResult p = mail::microsoftPoll(H, msConfig(source, ""), mail::DeviceCode{}, &why);
        check(p.state == mail::PollState::Error && has(p.message, "Aucune connexion \xC3\xA0 Microsoft n'est en cours") && why == p.message,
              "microsoftPoll sans code : " + p.message);
        const mail::PollResult w = mail::microsoftWait(H, msConfig(source, ""), mail::DeviceCode{}, [] { return true; });
        check(w.state == mail::PollState::Cancelled && has(w.message, "arr\xC3\xAAt\xC3\xA9" "e"), "microsoftWait arrete tout de suite : " + w.message);
    }
    // ---- readyToSend selon l'identification
    {
        const std::string K = U(tmp / "editeur-ms-pret");
        check(mail::saveConfig(K, baseConfig(source), &why) && mail::savePassword(K, kPassword, &why) && mail::readyToSend(K),
              "readyToSend : mot de passe d'application enregistre");
        check(mail::saveConfig(K, msConfig(source, ""), &why) && !mail::readyToSend(K), "readyToSend : Microsoft sans connexion (le mot de passe ne compte pas)");
        check(plantLink(K, "essai_jeton_rafraichir_pose", "") && mail::microsoftConnected(K) && mail::readyToSend(K), "readyToSend : Microsoft connecte");
        mail::MicrosoftLink link;
        check(mail::microsoftLink(K, link, &why) && link.account == kUser && link.clientId == kClientId && link.tenant == "consumers" && link.since == 1,
              "microsoftLink : le compte, l'application, le locataire (jamais le jeton) : " + why);
        check(mail::microsoftForget(K, &why) && !mail::microsoftConnected(K) && mail::hasPassword(K) && mail::configured(K),
              "microsoftForget : la seule connexion Microsoft");
        check(mail::microsoftForget(K, &why), "microsoftForget une seconde fois : sans erreur");
        check(plantLink(K, "essai_jeton_rafraichir_pose", "") && mail::forget(K, &why) && !mail::microsoftConnected(K) &&
                  !fs::exists(fs::path(mail::microsoftPath(K))) && !mail::hasPassword(K) && !mail::configured(K),
              "forget retire aussi mail-microsoft.bin");
        writeFile(mail::microsoftPath(K), "pas une connexion");
        check(mail::saveConfig(K, msConfig(source, ""), &why), "reglages Microsoft (connexion abimee)");
        const mail::SendResult r = mail::send(K, small, 2000);
        check(!r.ok && !r.reached && has(r.message, "La connexion Microsoft enregistr\xC3\xA9" "e") && has(r.message, "reconnecte-toi"),
              "connexion abimee : " + r.message);
    }
}

// ============================================================================ sans reseau
void offline(const std::string& source, const fs::path& tmp) {
    std::printf("-- l'adresse, la configuration, le mot de passe\n");
    // ---- plausibleAddress
    for (const char* ok : {"client@exemple.fr", "a.b-c+d_e@sous.domaine.org", "x@y.co", "Prenom.Nom@Exemple.FR"})
        check(mail::plausibleAddress(ok), std::string("adresse acceptee : ") + ok);
    const std::string tooLong = std::string(64, 'a') + "@" + std::string(61, 'b') + "." + std::string(60, 'c') + "." + std::string(63, 'd') + ".fr";   // 254
    check(tooLong.size() == 254, "l'adresse de 254 caracteres");
    check(mail::plausibleAddress(tooLong), "254 caracteres : acceptee");
    check(!mail::plausibleAddress(tooLong + "r"), "255 caracteres : refusee");
    check(!mail::plausibleAddress(std::string(65, 'a') + "@b.fr"), "partie locale de 65 caracteres : refusee");
    check(!mail::plausibleAddress("a@" + std::string(64, 'b') + ".fr"), "etiquette de 64 caracteres : refusee");
    for (const char* bad : {"", "sans-arobase", "a@b", "a@@b.fr", "a@b@c.fr", "a b@c.fr", "a@b..fr", "<a@b.fr>", "a@b.fr;", "a\"b@c.fr",
                            "a@.b.fr", "a@b.fr.", "a\\b@c.fr", "@b.fr", "a@", "a,b@c.fr", "a@-b.fr", "a@b-.fr", ".a@b.fr", "a..b@c.fr",
                            "a\tb@c.fr", "a@b.fr\r\nBcc: x@y.fr", "j\xC3\xA9r\xC3\xB4me@exemple.fr", "a@b_c.fr"})
        check(!mail::plausibleAddress(bad), std::string("adresse refusee : ") + bad);
    check(!mail::plausibleAddress(std::string("a\0b@c.fr", 8)), "adresse refusee : un octet nul");
    check(mail::defaultPort("tls") == 465 && mail::defaultPort("starttls") == 587, "les ports habituels");

    // ---- la configuration : les refus (rien n'est ecrit)
    const fs::path folder = tmp / "editeur";
    const std::string F = U(folder);
    struct Bad {
        std::function<void(mail::Config&)> change;
        const char*                        expect;
        const char*                        what;
    };
    const std::vector<Bad> bads = {
        {[](mail::Config& c) { c.host = "  "; }, "serveur SMTP est vide", "hote vide"},
        {[](mail::Config& c) { c.host = "smtp.gmail.com:587"; }, "sans le port", "hote:port"},
        {[](mail::Config& c) { c.host = "smtp://smtp.gmail.com"; }, "n'est pas un nom de serveur", "smtp://"},
        {[](mail::Config& c) { c.host = "smtp gmail.com"; }, "n'est pas un nom de serveur", "espace dans l'hote"},
        {[](mail::Config& c) { c.port = 0; }, "de 1 \xC3\xA0 65535", "port 0"},
        {[](mail::Config& c) { c.port = 70000; }, "de 1 \xC3\xA0 65535", "port 70000"},
        {[](mail::Config& c) { c.security = "aucune"; }, "jamais en clair", "securite aucune"},
        {[](mail::Config& c) { c.security = "ssl"; }, "jamais en clair", "securite ssl"},
        {[](mail::Config& c) { c.security = ""; }, "jamais en clair", "securite vide"},
        {[](mail::Config& c) { c.from = "pas-une-adresse"; }, "l'exp\xC3\xA9" "diteur", "expediteur"},
        {[](mail::Config& c) { c.user = " "; }, "identifiant SMTP est vide", "identifiant vide"},
        {[](mail::Config& c) { c.user = "a\nb"; }, "n'est pas valable", "identifiant a deux lignes"},
        {[](mail::Config& c) { c.caFile = "/nulle/part/ac.pem"; }, "introuvable", "autorites introuvables"},
    };
    for (const auto& b : bads) {
        mail::Config c = baseConfig(source);
        b.change(c);
        std::string why;
        const bool saved = mail::saveConfig(F, c, &why);
        check(!saved && has(why, b.expect), std::string("configuration refusee (") + b.what + ") : " + why);
        check(!mail::validate(c, nullptr), std::string("validate refuse aussi (") + b.what + ")");
    }
    check(!mail::configured(F), "rien n'est ecrit pour une configuration refusee");

    // ---- l'aller-retour
    mail::Config c = baseConfig(source);
    c.host = "  SMTP.Exemple.Invalid. ";
    c.security = "STARTTLS";
    c.port = 587;
    std::string why;
    check(mail::saveConfig(F, c, &why), "configuration enregistree : " + why);
    check(mail::configured(F), "configured() apres l'enregistrement");
    const std::string jsonText = readFile(mail::configPath(F));
    check(has(jsonText, "smtp.exemple.invalid") && has(jsonText, "starttls") && !has(jsonText, kPassword) && !has(jsonText, "mdp"),
          "mail.json : l'hote normalise, aucun secret");
#if !defined(_WIN32)
    check(ownerOnly(mail::configPath(F)), "mail.json en 0600");
#endif
    mail::Config back;
    check(mail::loadConfig(F, back, &why), "configuration relue : " + why);
    check(back.host == "smtp.exemple.invalid" && back.port == 587 && back.security == "starttls" && back.from == kUser && back.user == kUser &&
              back.fromName == c.fromName && back.caFile == c.caFile,
          "l'aller-retour garde tout (hote en minuscules, sans point final)");
    // implicit TLS et sans nom
    mail::Config t = baseConfig(source);
    t.security = "tls";
    t.port = 465;
    t.fromName.clear();
    t.caFile.clear();
    check(mail::saveConfig(F, t, &why) && mail::loadConfig(F, back, &why) && back.security == "tls" && back.port == 465 && back.fromName.empty() &&
              back.caFile.empty(),
          "TLS sur 465, sans nom ni autorites : aller-retour");

    // ---- le mot de passe d'application
    check(!mail::hasPassword(F), "pas encore de mot de passe");
    {
        const mail::SendResult r = mail::send(F, {"client@exemple.invalid", "", "Sujet", "Texte"}, 2000);
        check(!r.ok && !r.reached && has(r.message, "n'est pas enregistr\xC3\xA9"), "envoi sans mot de passe enregistre : " + r.message);
    }
    check(!mail::savePassword(F, "", &why) && has(why, "vide"), "mot de passe vide refuse : " + why);
    check(!mail::savePassword(F, "mdp\nsuite", &why) && has(why, "retour \xC3\xA0 la ligne"), "mot de passe sur deux lignes refuse : " + why);
    check(mail::savePassword(F, kPassword, &why), "mot de passe enregistre : " + why);
    check(mail::hasPassword(F), "hasPassword()");
    {
        crypto::Bytes sealed, plain;
        check(crypto::readFile(mail::passwordPath(F), sealed, &why), "mail-mdp.bin lu");
        const std::string stored(sealed.begin(), sealed.end());
        if (crypto::protectedHere()) {
            check(!has(stored, kPassword), "Windows : le mot de passe n'est PAS en clair dans mail-mdp.bin (DPAPI)");
        } else {
#if !defined(_WIN32)
            check(ownerOnly(mail::passwordPath(F)), "mail-mdp.bin en 0600");
#endif
        }
        check(crypto::unprotect(sealed, plain, &why) && std::string(plain.begin(), plain.end()) == kPassword, "le mot de passe se relit tel quel");
    }
    check(!has(readFile(mail::configPath(F)), kPassword), "toujours aucun secret dans mail.json");

    // ---- les refus avant toute connexion
    {
        mail::Config bad = baseConfig(source);
        bad.security = "aucune";
        const mail::SendResult r = mail::sendWith(bad, kPassword, {"client@exemple.invalid", "", "Sujet", "Texte"}, 2000);
        check(!r.ok && !r.reached && has(r.message, "jamais en clair"), "sendWith : securite en clair refusee : " + r.message);
        const mail::SendResult r2 = mail::sendWith(baseConfig(source), kPassword, {"pas une adresse", "", "Sujet", "Texte"}, 2000);
        check(!r2.ok && !r2.reached && has(r2.message, "destinataire") && has(r2.message, "n'est pas valable"), "destinataire invalide : " + r2.message);
        const mail::SendResult r3 = mail::sendWith(baseConfig(source), "", {"client@exemple.invalid", "", "Sujet", "Texte"}, 2000);
        check(!r3.ok && !r3.reached && has(r3.message, "vide"), "mot de passe vide : " + r3.message);
    }

    // ---- un mail.json abime
    writeFile(mail::configPath(F), "{ pas du json");
    check(!mail::configured(F), "mail.json abime : pas configure");
    {
        const mail::SendResult r = mail::send(F, {"client@exemple.invalid", "", "Sujet", "Texte"}, 2000);
        check(!r.ok && !r.reached && has(r.message, "illisibles"), "mail.json abime : " + r.message);
    }

    // ---- oublier
    check(mail::forget(F, &why), "forget : " + why);
    check(!mail::configured(F) && !mail::hasPassword(F) && !fs::exists(mail::configPath(F)) && !fs::exists(mail::passwordPath(F)),
          "forget retire mail.json et mail-mdp.bin");
    check(mail::forget(F, &why), "forget une seconde fois : sans erreur");
    {
        const mail::SendResult r = mail::send(F, {"client@exemple.invalid", "", "Sujet", "Texte"}, 2000);
        check(!r.ok && !r.reached && has(r.message, "n'est pas r\xC3\xA9gl\xC3\xA9"), "envoi sans configuration : " + r.message);
    }
}

// ============================================================================ contre le serveur d'essai
// `allMessages` : chaque phrase (pour verifier a la fin qu'aucun secret n'y est) ; `delivered` : les envois reussis.
void online(const std::string& source, const fs::path& tmp, TestServer& srv, std::vector<std::string>& allMessages, int& delivered) {
    std::printf("-- contre le serveur SMTP d'essai (STARTTLS %d, TLS %d)\n", srv.port("starttls"), srv.port("tls"));
    const auto keep = [&](const mail::SendResult& r) {
        if (r.ok) ++delivered;
        allMessages.push_back(r.message);
        std::printf("    -> %s%s\n", r.ok ? "" : (r.reached ? "[refuse, joint] " : "[refuse, non joint] "), r.message.c_str());
        return r;
    };

    // ---- 1. STARTTLS, AUTH PLAIN, par send() (la configuration et le mot de passe du dossier)
    const std::string F = U(tmp / "editeur-envoi");
    mail::Config c = baseConfig(source);
    c.port = srv.port("starttls");
    std::string why;
    check(mail::saveConfig(F, c, &why) && mail::savePassword(F, kPassword, &why), "reglages enregistres : " + why);
    mail::Message m;
    m.to = "client@exemple.invalid";
    m.toName = "Client D\xC3\xA9mo";
    m.subject = "Ta licence XPG est pr\xC3\xAAte \xE2\x80\x94 num\xC3\xA9ro 42 (\xC3\xA9t\xC3\xA9 \xE2\x82\xAC)";
    m.body = "Bonjour,\n\nTa licence \xC2\xAB PROFESSIONAL \xC2\xBB est accept\xC3\xA9" "e.\n.une ligne qui commence par un point\n"
             "\xC3\x80 bient\xC3\xB4t, l'\xC3\xA9" "diteur\n" +
             std::string(200, 'x') + "\n";
    const mail::SendResult r1 = keep(mail::send(F, m, 10000));
    check(r1.ok && r1.reached, "STARTTLS : envoye (" + r1.message + ")");
    check(has(r1.message, "client@exemple.invalid"), "la phrase de reussite nomme le destinataire : " + r1.message);
    auto got = srv.messages("starttls");
    check(got.size() == 1, "STARTTLS : le serveur a recu un message (" + std::to_string(got.size()) + ")");
    if (!got.empty()) {
        const json::Value& g = got[0];
        check(g.flag("tls") && g.str("version_tls") == "TLSv1.3", "STARTTLS : chiffre en TLS 1.3 (" + g.str("version_tls") + ")");
        check(g.str("mecanisme") == "PLAIN" && g.str("utilisateur") == kUser, "STARTTLS : AUTH PLAIN propose et choisi (" + g.str("mecanisme") + ")");
        check(g.integer("ehlo") == 2 && g.integer("ehlo_apres_tls") == 1, "STARTTLS : EHLO avant et apres la poignee de main");
        check(g.str("de") == std::string("<") + kUser + ">", "MAIL FROM : " + g.str("de"));
        check(g.at("a").items().size() == 1 && g.at("a").items()[0].asString() == "<client@exemple.invalid>", "RCPT TO");
        const std::string data = g.str("donnees");
        check(asciiOnly(data), "le message est tout en ASCII (en-tetes encodes, corps en base64)");
        check(!has(data, kPassword), "le mot de passe n'est pas dans le message");
        const Parsed p = parseMail(data);
        check(p.ok, "le message a un en-tete et un corps");
        const std::string from = header(p, "From"), to = header(p, "To"), subject = header(p, "Subject");
        check(has(from, "=?UTF-8?B?") && decodeWords(from) == "\xC3\x89" "diteur XPG <essai@exemple.invalid>", "From : " + decodeWords(from));
        check(has(to, "=?UTF-8?B?") && decodeWords(to) == "Client D\xC3\xA9mo <client@exemple.invalid>", "To : " + decodeWords(to));
        check(subject.rfind("=?UTF-8?B?", 0) == 0, "Subject en mots encodes : " + subject);
        check(decodeWords(subject) == m.subject, "Subject redonne le sujet accentue : " + decodeWords(subject));
        int words = 0;
        for (std::size_t at = subject.find("=?UTF-8?B?"); at != std::string::npos; at = subject.find("=?UTF-8?B?", at + 1)) ++words;
        check(words >= 2, "un long sujet : plusieurs mots encodes, plies (" + std::to_string(words) + ")");
        bool shortLines = true;
        for (const auto& l : p.headLines) shortLines = shortLines && l.size() <= 78;
        check(shortLines, "aucune ligne d'en-tete de plus de 78 caracteres");
        check(std::regex_match(header(p, "Date"), std::regex(R"(^(Mon|Tue|Wed|Thu|Fri|Sat|Sun), \d{2} (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) \d{4} \d{2}:\d{2}:\d{2} \+0000$)")),
              "Date RFC 5322 : " + header(p, "Date"));
        check(std::regex_match(header(p, "Message-ID"), std::regex(R"(^<[0-9a-f]{32}\.xpg@exemple\.invalid>$)")), "Message-ID : " + header(p, "Message-ID"));
        check(header(p, "MIME-Version") == "1.0", "MIME-Version: 1.0");
        check(header(p, "Content-Type") == "text/plain; charset=UTF-8", "Content-Type : " + header(p, "Content-Type"));
        check(header(p, "Content-Transfer-Encoding") == "base64", "Content-Transfer-Encoding: base64");
        for (const char* once : {"From", "To", "Subject", "Date", "Message-ID"}) check(headerCount(p, once) == 1, std::string("un seul ") + once);
        bool b64Lines = true;
        std::size_t lines = 0;
        for (std::size_t at = 0; at < p.body.size();) {
            auto eol = p.body.find("\r\n", at);
            if (eol == std::string::npos) eol = p.body.size();
            const std::string l = p.body.substr(at, eol - at);
            if (!l.empty()) {
                ++lines;
                b64Lines = b64Lines && l.size() <= 76 && l.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") == std::string::npos;
            }
            at = eol + 2;
        }
        check(b64Lines && lines >= 3, "le corps : des lignes base64 de 76 au plus (" + std::to_string(lines) + ")");
        crypto::Bytes body;
        check(crypto::unbase64(p.body, body) && std::string(body.begin(), body.end()) == crlf(m.body), "le corps redonne le texte UTF-8 (fins de ligne CRLF)");
    }

    // ---- 2. TLS des la connexion, AUTH LOGIN seulement, TLS 1.2 ; sans nom ; sujet ASCII
    mail::Config t = baseConfig(source);
    t.security = "tls";
    t.port = srv.port("tls");
    t.fromName.clear();
    const mail::SendResult r2 = keep(mail::sendWith(t, kPassword, {"client@exemple.invalid", "", "Licence XPG", "Bonjour\n"}, 10000));
    check(r2.ok && r2.reached, "TLS (465) : envoye (" + r2.message + ")");
    got = srv.messages("tls");
    check(got.size() == 1, "TLS : le serveur a recu un message");
    if (!got.empty()) {
        const json::Value& g = got[0];
        check(g.flag("tls") && g.str("version_tls") == "TLSv1.2", "TLS : chiffre en TLS 1.2 (" + g.str("version_tls") + ")");
        check(g.str("mecanisme") == "LOGIN" && g.str("utilisateur") == kUser, "TLS : AUTH LOGIN quand PLAIN n'est pas propose (" + g.str("mecanisme") + ")");
        check(g.integer("ehlo") == 1, "TLS : un seul EHLO");
        const Parsed p = parseMail(g.str("donnees"));
        check(header(p, "From") == kUser, "From sans nom : l'adresse seule (" + header(p, "From") + ")");
        check(header(p, "To") == "client@exemple.invalid", "To sans nom : " + header(p, "To"));
        check(header(p, "Subject") == "Licence XPG", "un sujet ASCII reste tel quel : " + header(p, "Subject"));
        crypto::Bytes body;
        check(crypto::unbase64(p.body, body) && std::string(body.begin(), body.end()) == "Bonjour\r\n", "le corps (TLS)");
    }

    const mail::Message small{"client@exemple.invalid", "", "Essai", "Texte\n"};

    // ---- 3. mot de passe faux
    mail::Config s = baseConfig(source);
    s.port = srv.port("starttls");
    const std::string wrong = "mauvais-mdp-essai";
    const mail::SendResult r3 = keep(mail::sendWith(s, wrong, small, 10000));
    check(!r3.ok && r3.reached, "mot de passe faux : refuse, serveur joint");
    check(has(r3.message, "Le serveur refuse l'identifiant ou le mot de passe d'application : pour Gmail, il faut un mot de passe d'application "
                          "(compte Google \xE2\x80\xBA S\xC3\xA9" "curit\xC3\xA9 \xE2\x80\xBA Validation en deux \xC3\xA9tapes)"),
          "mot de passe faux : la phrase claire (" + r3.message + ")");
    check(has(r3.message, "535"), "mot de passe faux : la reponse du serveur est citee");
    check(!has(r3.message, wrong) && !has(r3.message, crypto::base64(wrong)), "mot de passe faux : il n'est pas dans la phrase");
    check(waitFor([&] { return srv.event("starttls", "auth_refusee"); }, 2000), "le serveur a vu l'identification refusee");

    // ---- 3b. comme Microsoft : le mot de passe refuse pour tout le compte (5.7.139), ou XOAUTH2 seul propose
    mail::Config ms = s;
    ms.user = "oauth@exemple.invalid";
    const mail::SendResult r3b = keep(mail::sendWith(ms, kPassword, small, 10000));
    check(!r3b.ok && r3b.reached && has(r3b.message, "OAuth") && has(r3b.message, "5.7.139"), "Microsoft, mot de passe retire : " + r3b.message);
    mail::Config xo = baseConfig(source);
    xo.port = srv.port("oauth");
    const mail::SendResult r3c = keep(mail::sendWith(xo, kPassword, small, 10000));
    check(!r3c.ok && r3c.reached && has(r3c.message, "ni AUTH PLAIN ni AUTH LOGIN") && has(r3c.message, "OAuth"), "XOAUTH2 seulement : " + r3c.message);
    check(srv.messages("oauth").empty(), "XOAUTH2 seulement : rien n'est arrive");

    // ---- 4. un certificat d'aucune autorite
    mail::Config u = baseConfig(source);
    u.security = "tls";
    u.port = srv.port("nonfiable");
    const mail::SendResult r4 = keep(mail::sendWith(u, kPassword, small, 10000));
    check(!r4.ok && r4.reached, "certificat non fiable : refuse, serveur joint");
    check(has(r4.message, "certificat") && has(r4.message, "n'est pas reconnu") && has(r4.message, "rien n'est envoy\xC3\xA9"),
          "certificat non fiable : la phrase (" + r4.message + ")");
    check(srv.messages("nonfiable").empty(), "certificat non fiable : rien n'est arrive au serveur");
    check(waitFor([&] { return srv.event("nonfiable", "tls_refuse"); }, 2000), "certificat non fiable : la poignee de main a ete coupee");

    // ---- 5. le bon serveur, mais sans l'autorite d'essai (les autorites du systeme seulement)
    mail::Config n = baseConfig(source);
    n.port = srv.port("starttls");
    n.caFile.clear();
    const mail::SendResult r5 = keep(mail::sendWith(n, kPassword, small, 10000));
    check(!r5.ok && r5.reached && has(r5.message, "n'est pas reconnu"), "autorite d'essai absente : refuse (" + r5.message + ")");

    // ---- 6. le nom du serveur ne correspond pas (le certificat nomme localhost, pas 127.0.0.1)
    mail::Config ip = baseConfig(source);
    ip.host = "127.0.0.1";
    ip.port = srv.port("starttls");
    const mail::SendResult r6 = keep(mail::sendWith(ip, kPassword, small, 10000));
    check(!r6.ok && r6.reached && has(r6.message, "le nom ne correspond pas"), "nom du serveur different : refuse (" + r6.message + ")");

    // ---- 7. destinataire refuse
    const mail::SendResult r7 = keep(mail::sendWith(s, kPassword, {"inconnu@refuse.invalid", "", "Essai", "Texte\n"}, 10000));
    check(!r7.ok && r7.reached && has(r7.message, "refuse le destinataire") && has(r7.message, "inconnu@refuse.invalid") && has(r7.message, "550"),
          "destinataire refuse : " + r7.message);

    // ---- 8. pas de STARTTLS : rien ne part en clair
    mail::Config p = baseConfig(source);
    p.port = srv.port("sans_starttls");
    const mail::SendResult r8 = keep(mail::sendWith(p, kPassword, small, 10000));
    check(!r8.ok && r8.reached && has(r8.message, "ne propose pas STARTTLS") && has(r8.message, "sans chiffrement"), "sans STARTTLS : " + r8.message);
    check(!srv.event("sans_starttls", "auth_sans_tls") && srv.messages("sans_starttls").empty(), "sans STARTTLS : ni identification ni message en clair");

    // ---- 9. TLS demande sur un port qui parle en clair
    mail::Config wrongTls = baseConfig(source);
    wrongTls.security = "tls";
    wrongTls.port = srv.port("starttls");
    const mail::SendResult r9 = keep(mail::sendWith(wrongTls, kPassword, small, 10000));
    check(!r9.ok && r9.reached && has(r9.message, "STARTTLS"), "TLS sur un port en clair : " + r9.message);

    // ---- 10. un serveur muet : le delai
    mail::Config mute = baseConfig(source);
    mute.port = srv.port("muet");
    const auto t0 = std::chrono::steady_clock::now();
    const mail::SendResult r10 = keep(mail::sendWith(mute, kPassword, small, 1500));
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(!r10.ok && !r10.reached && has(r10.message, "ne dit rien en 1,5 s"), "serveur muet : " + r10.message);
    check(spent >= 1400 && spent < 6000, "serveur muet : le delai est tenu (" + std::to_string(spent) + " ms)");
    mute.security = "tls";
    const mail::SendResult r10b = keep(mail::sendWith(mute, kPassword, small, 1500));
    check(!r10b.ok && !r10b.reached && has(r10b.message, "n'aboutit pas"), "serveur muet (TLS) : " + r10b.message);

#if !defined(_WIN32)
    // ---- 11. un port ferme
    mail::Config closed = baseConfig(source);
    closed.port = closedPort();
    const mail::SendResult r11 = keep(mail::sendWith(closed, kPassword, small, 3000));
    check(!r11.ok && !r11.reached && has(r11.message, "Impossible de joindre"), "port ferme : " + r11.message);
#endif
    // ---- 12. un serveur qui n'existe pas (.invalid : jamais resolu, RFC 6761)
    mail::Config nowhere = baseConfig(source);
    nowhere.host = "hote-inexistant.invalid";
    const mail::SendResult r12 = keep(mail::sendWith(nowhere, kPassword, small, 3000));
    check(!r12.ok && (has(r12.message, "est introuvable") || has(r12.message, "Impossible de joindre")), "serveur inexistant : " + r12.message);
}

// ============================================================================ 1.38.0 : le compte Microsoft, contre les serveurs d'essai
// La derniere reponse 200 du faux serveur OAuth a une requete .../token : ses jetons.
json::Value lastTokenReply(const TestServer& oauth) {
    const auto toks = oauth.requests("token");
    for (auto it = toks.rbegin(); it != toks.rend(); ++it)
        if (it->integer("statut") == 200) return it->at("reponse");
    return json::Value::object();
}

void microsoftOnline(const std::string& source, const fs::path& tmp, TestServer& srv, TestServer& oauth, std::vector<std::string>& said, int& delivered) {
    const std::string A = "http://127.0.0.1:" + std::to_string(oauth.port("oauth"));
    std::printf("-- 1.38.0 : le compte Microsoft contre le faux serveur OAuth (%s) et le serveur SMTP d'essai (XOAUTH2 %d, comme Microsoft %d)\n",
                A.c_str(), srv.port("oauth"), srv.port("microsoft"));
    const auto keep = [&](const std::string& m) {
        said.push_back(m);
        std::printf("    -> %s\n", m.c_str());
        return m;
    };
    const auto keepSend = [&](const mail::SendResult& r) {
        if (r.ok) ++delivered;
        keep(std::string(r.ok ? "" : (r.reached ? "[refuse, joint] " : "[refuse, non joint] ")) + r.message);
        return r;
    };
    const mail::Message small{"client@exemple.invalid", "", "Essai", "Texte\n"};
    const std::string F = U(tmp / "editeur-microsoft");
    mail::Config c = msConfig(source, A, "consumers");
    c.port = srv.port("oauth");
    std::string why;
    check(mail::saveConfig(F, c, &why), "reglages Microsoft enregistres : " + why);

    // ---- 1. commencer : le code de l'appareil
    mail::DeviceCode dc;
    check(mail::microsoftStart(c, dc, &why), "microsoftStart : " + why);
    check(dc.userCode.rfind("ESSAI", 0) == 0 && dc.verificationUri == "https://exemple.invalid/devicelogin" && has(dc.message, dc.userCode) &&
              dc.deviceCode.rfind("essai_jeton_appareil_", 0) == 0 && dc.interval == 1 && dc.expiresIn == 900,
          "le code de l'appareil : " + dc.userCode + ", " + dc.verificationUri + ", toutes les " + std::to_string(dc.interval) + " s");
    const std::string how = keep(mail::microsoftInstructions(dc));
    check(has(how, dc.verificationUri) && has(how, dc.userCode) && !has(how, dc.deviceCode) && has(how, "15 min"), "microsoftInstructions : " + how);
    auto reqs = oauth.requests("devicecode");
    check(reqs.size() == 1, "devicecode : une requete (" + std::to_string(reqs.size()) + ")");
    if (!reqs.empty()) {
        const json::Value& q = reqs.back();
        const json::Value& f = q.at("champs");
        check(q.str("locataire") == "consumers" && q.str("chemin") == "/consumers/oauth2/v2.0/devicecode", "devicecode : le chemin " + q.str("chemin"));
        check(f.str("client_id") == kClientId && f.members().size() == 2, "devicecode : client_id (et scope, rien d'autre)");
        const std::string scope = f.str("scope");
        check(has(scope, "https://outlook.office.com/SMTP.Send") && has(scope, "offline_access") && has(scope, "openid") && has(scope, "profile"),
              "devicecode : scope = " + scope);
        check(has(q.str("type"), "application/x-www-form-urlencoded") && has(q.str("langue"), "fr"), "devicecode : un formulaire, Accept-Language fr");
    }

    // ---- 2. sonder : en attente, puis connecte
    const mail::PollResult p1 = mail::microsoftPoll(F, c, dc, &why);
    keep(p1.message);
    check(p1.state == mail::PollState::Pending && p1.waitSeconds == 1 && !p1.slowDown, "premier sondage : en attente (" + p1.message + ")");
    check(!mail::microsoftConnected(F), "en attente : rien n'est garde");
    auto toks = oauth.requests("token");
    if (!toks.empty()) {
        const json::Value& f = toks.back().at("champs");
        check(f.str("grant_type") == "urn:ietf:params:oauth:grant-type:device_code" && f.str("device_code") == dc.deviceCode &&
                  f.str("client_id") == kClientId && toks.back().str("locataire") == "consumers",
              "token : grant_type device_code, le code de l'appareil, client_id");
    }
    const mail::PollResult p2 = mail::microsoftPoll(F, c, dc, &why);
    keep(p2.message);
    check(p2.state == mail::PollState::Done && p2.account == kUser && has(p2.message, kUser) && !has(p2.message, "Attention"),
          "second sondage : connecte, le compte (" + p2.message + ")");
    check(mail::microsoftConnected(F) && mail::readyToSend(F), "connecte et pret a envoyer");
    const std::string rt1 = lastTokenReply(oauth).str("refresh_token");
    check(rt1.rfind("essai_jeton_rafraichir_", 0) == 0, "le faux serveur a donne un jeton de rafraichissement : " + rt1);
    {
        crypto::Bytes sealed;
        check(crypto::readFile(mail::microsoftPath(F), sealed, &why), "mail-microsoft.bin lu");
        const std::string stored(sealed.begin(), sealed.end());
        if (crypto::protectedHere()) {
            check(!has(stored, rt1) && !has(stored, kSecretMark), "Windows : le jeton n'est PAS en clair dans mail-microsoft.bin (DPAPI)");
        } else {
#if !defined(_WIN32)
            check(ownerOnly(fs::path(mail::microsoftPath(F))), "mail-microsoft.bin en 0600");
#endif
        }
        const json::Value v = storedLink(F);
        check(v.str("jeton") == rt1 && v.str("id_application") == kClientId && v.str("locataire") == "consumers" && v.str("autorite") == A &&
                  v.str("compte") == kUser,
              "mail-microsoft.bin : le jeton de rafraichissement, l'application, le locataire, l'autorite, le compte");
    }
    {
        mail::MicrosoftLink link;
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        check(mail::microsoftLink(F, link, &why) && link.account == kUser && link.clientId == kClientId && link.tenant == "consumers" &&
                  link.authority == A && link.since > now - 600 && link.since <= now + 5,
              "microsoftLink : le compte et l'heure de la connexion : " + why);
    }
    check(!has(readFile(mail::configPath(F)), kSecretMark), "mail.json : aucun jeton");
    const mail::PollResult p3 = mail::microsoftPoll(F, c, dc, &why);   // un code deja servi
    keep(p3.message);
    check(p3.state == mail::PollState::Error && !has(p3.message, dc.deviceCode) && mail::microsoftConnected(F), "un code deja servi : refuse (" + p3.message + ")");

    // ---- 3. envoyer : le rafraichissement, XOAUTH2, la rotation gardee
    mail::Message m;
    m.to = "client@exemple.invalid";
    m.toName = "Client D\xC3\xA9mo";
    m.subject = "Ta licence XPG (par Microsoft)";
    m.body = "Bonjour,\nTa licence est accept\xC3\xA9" "e.\n";
    const std::size_t before = srv.messages("oauth").size();
    const mail::SendResult s1 = keepSend(mail::send(F, m, 10000));
    check(s1.ok && s1.reached && has(s1.message, "client@exemple.invalid"), "XOAUTH2 : envoye (" + s1.message + ")");
    toks = oauth.requests("token");
    std::string at2, rt2;
    if (!toks.empty()) {
        const json::Value& q = toks.back();
        const json::Value& f = q.at("champs");
        check(f.str("grant_type") == "refresh_token" && f.str("refresh_token") == rt1 && f.str("client_id") == kClientId && q.integer("statut") == 200,
              "rafraichissement : grant_type refresh_token, le jeton garde, client_id");
        check(has(f.str("scope"), "https://outlook.office.com/SMTP.Send") && has(f.str("scope"), "offline_access"), "rafraichissement : scope = " + f.str("scope"));
        at2 = q.at("reponse").str("access_token");
        rt2 = q.at("reponse").str("refresh_token");
    }
    const auto got = srv.messages("oauth");
    check(got.size() == before + 1, "XOAUTH2 : le serveur SMTP a recu le message");
    if (got.size() == before + 1) {
        const json::Value& g = got.back();
        check(g.str("mecanisme") == "XOAUTH2" && g.str("utilisateur") == kUser && g.str("forme") == "deux_temps", "XOAUTH2 en deux temps, user= l'adresse");
        check(!at2.empty() && g.str("jeton") == at2, "XOAUTH2 : le jeton d'acces est celui du rafraichissement (" + g.str("jeton") + ")");
        check(g.flag("tls") && g.integer("ehlo_apres_tls") == 1, "XOAUTH2 : apres STARTTLS");
        const Parsed p = parseMail(g.str("donnees"));
        check(decodeWords(header(p, "To")) == "Client D\xC3\xA9mo <client@exemple.invalid>" && header(p, "Subject") == m.subject, "XOAUTH2 : le message");
    }
    check(!rt2.empty() && rt2 != rt1 && storedLink(F).str("jeton") == rt2, "la rotation : le nouveau jeton de rafraichissement est garde");

    // ---- 4. un second envoi : le nouveau jeton sert (l'ancien ne vaut plus pour le faux serveur)
    const mail::SendResult s2 = keepSend(mail::send(F, small, 10000));
    check(s2.ok, "second envoi : envoye (" + s2.message + ")");
    toks = oauth.requests("token");
    check(!toks.empty() && toks.back().at("champs").str("refresh_token") == rt2, "second envoi : avec le jeton tourne");
    const std::string rt3 = lastTokenReply(oauth).str("refresh_token");
    check(!rt3.empty() && rt3 != rt2 && storedLink(F).str("jeton") == rt3, "second envoi : la rotation encore gardee");

    // ---- 5. l'ancien jeton (deja tourne) : invalid_grant -> reconnecte-toi ; la connexion est oubliee
    check(plantLink(F, rt1, A), "l'ancien jeton remis en place");
    const std::size_t sent = srv.messages().size();
    const mail::SendResult s3 = keepSend(mail::send(F, small, 10000));
    check(!s3.ok && !s3.reached && has(s3.message, "ne reconna\xC3\xAEt plus ta connexion") && has(s3.message, "reconnecte-toi") &&
              has(s3.message, "AADSTS70008"),
          "invalid_grant : la phrase (" + s3.message + ")");
    check(!mail::microsoftConnected(F) && srv.messages().size() == sent, "invalid_grant : la connexion morte est oubliee, rien n'est parti");
    {
        const mail::SendResult r = keepSend(mail::send(F, small, 10000));
        check(!r.ok && has(r.message, "Connecte-toi \xC3\xA0 Microsoft"), "puis : pas connecte (" + r.message + ")");
    }

    // ---- 6. le consentement manquant au rafraichissement (AADSTS65001) : la permission, la connexion gardee
    check(plantLink(F, "essai_jeton_rafraichir_consentement", A), "un jeton sans consentement");
    const mail::SendResult s4 = keepSend(mail::send(F, small, 10000));
    check(!s4.ok && !s4.reached && has(s4.message, "SMTP.Send") && has(s4.message, "Autorisations d'API") && has(s4.message, "consentement"),
          "consentement manquant : " + s4.message);
    check(mail::microsoftConnected(F), "consentement manquant : la connexion reste (a refaire apres le consentement)");

    // ---- 7. un rafraichissement sans nouveau jeton : l'ancien reste garde
    check(plantLink(F, "essai_jeton_rafraichir_fixe", A), "un jeton sans rotation");
    const mail::SendResult s5 = keepSend(mail::send(F, small, 10000));
    check(s5.ok && storedLink(F).str("jeton") == "essai_jeton_rafraichir_fixe", "sans rotation : envoye, le jeton garde reste (" + s5.message + ")");

    // ---- 8. l'application changee depuis la connexion : rien ne part, aucune requete
    {
        mail::Config other = c;
        other.clientId = "00000000-0000-4000-8000-0000000000e6";
        check(mail::saveConfig(F, other, &why), "un autre ID d'application");
        const std::size_t n = oauth.requests().size();
        const mail::SendResult r = keepSend(mail::send(F, small, 10000));
        check(!r.ok && !r.reached && has(r.message, "autre application") && has(r.message, "reconnecte-toi") && oauth.requests().size() == n,
              "application changee : " + r.message);
        check(mail::saveConfig(F, c, &why), "l'ID d'application remis");
    }

    // ---- 9. microsoftWait : en attente puis connecte (toutes les 1 s) ; et l'arret
    {
        const std::string W = U(tmp / "editeur-ms-attente");
        mail::DeviceCode dw;
        check(mail::microsoftStart(c, dw, &why), "microsoftWait : le code : " + why);
        const auto t0 = std::chrono::steady_clock::now();
        const mail::PollResult w = mail::microsoftWait(W, c, dw);
        const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        keep(w.message);
        check(w.state == mail::PollState::Done && w.account == kUser && mail::microsoftConnected(W),
              "microsoftWait : connecte (" + std::to_string(spent) + " ms) : " + w.message);
        check(spent >= 1500 && spent < 10000, "microsoftWait : deux sondages espaces de 1 s (" + std::to_string(spent) + " ms)");
        mail::DeviceCode ds;
        check(mail::microsoftStart(c, ds, &why), "microsoftWait arrete : le code : " + why);
        const std::size_t n = oauth.requests("token").size();
        const mail::PollResult st = mail::microsoftWait(U(tmp / "editeur-ms-arret"), c, ds, [] { return true; });
        keep(st.message);
        check(st.state == mail::PollState::Cancelled && oauth.requests("token").size() == n && !mail::microsoftConnected(U(tmp / "editeur-ms-arret")),
              "microsoftWait arrete : aucun sondage, rien n'est garde");
    }

    // ---- 10. slow_down : attendre 5 s de plus (un sondage seul : l'essai n'attend pas)
    {
        mail::Config cs = c;
        cs.tenant = "ralentir.essai.invalid";
        const std::string S = U(tmp / "editeur-ms-ralentir");
        mail::DeviceCode d;
        check(mail::microsoftStart(cs, d, &why), "slow_down : le code : " + why);
        const mail::PollResult a = mail::microsoftPoll(S, cs, d, &why);
        keep(a.message);
        check(a.state == mail::PollState::Pending && a.slowDown && a.waitSeconds == d.interval + 5 && has(a.message, "6 s"), "slow_down : " + a.message);
        const mail::PollResult b = mail::microsoftPoll(S, cs, d, &why);
        check(b.state == mail::PollState::Done && mail::microsoftConnected(S), "slow_down puis connecte : " + b.message);
    }

    // ---- 11. les fins de la connexion, une par locataire du faux serveur
    struct End {
        const char*     tenant;
        mail::PollState state;
        const char*     expect;
    };
    const std::vector<End> ends = {
        {"refus.essai.invalid", mail::PollState::Refused, "Tu as refus\xC3\xA9 la connexion"},
        {"expire.essai.invalid", mail::PollState::Expired, "a expir\xC3\xA9"},
        {"public.essai.invalid", mail::PollState::Error, "Autoriser les flux clients publics"},
        {"fuite.essai.invalid", mail::PollState::Error, "Microsoft refuse la demande (invalid_request)"},
        {"sans-permission.essai.invalid", mail::PollState::Error, "SMTP.Send"},
        {"autre-compte.essai.invalid", mail::PollState::Done, "Attention"},
    };
    for (const auto& e : ends) {
        mail::Config ce = c;
        ce.tenant = e.tenant;
        const std::string E = U(tmp / (std::string("editeur-ms-") + e.tenant));
        mail::DeviceCode d;
        check(mail::microsoftStart(ce, d, &why), std::string(e.tenant) + " : le code : " + why);
        why.clear();
        mail::PollResult r = mail::microsoftPoll(E, ce, d, &why);
        if (r.state == mail::PollState::Pending) r = mail::microsoftPoll(E, ce, d, &why);   // ces jetons-la : << en attente >> d'abord
        keep(r.message);
        check(r.state == e.state && has(r.message, e.expect) && !has(r.message, d.deviceCode), std::string(e.tenant) + " : " + r.message);
        if (e.state == mail::PollState::Done) {
            check(r.account == "autre@exemple.invalid" && has(r.message, kUser) && mail::microsoftConnected(E), "un autre compte : connecte, l'identifiant signale");
        } else {
            check(!mail::microsoftConnected(E) && why == r.message, std::string(e.tenant) + " : rien n'est garde, why = le message");
        }
    }
    {
        mail::Config ce = c;
        ce.tenant = "inconnue.essai.invalid";
        mail::DeviceCode d;
        const bool unknown = mail::microsoftStart(ce, d, &why);
        keep(why);
        check(!unknown && has(why, "ne conna\xC3\xAEt pas l'application") && has(why, kClientId), "application inconnue : " + why);
        ce.tenant = "panne.essai.invalid";
        const bool outage = mail::microsoftStart(ce, d, &why);
        keep(why);
        check(!outage && has(why, "panne (code 503)"), "Microsoft en panne : " + why);
    }

    // ---- 12. XOAUTH2 : les refus du serveur SMTP (sendWithToken : sans OAuth)
    {
        mail::Config x = c;
        x.port = srv.port("oauth");
        const mail::SendResult a = keepSend(mail::sendWithToken(x, "essai_jeton_acces_refuse", small, 10000));
        check(!a.ok && a.reached && has(a.message, "refuse le jeton Microsoft") && has(a.message, "535") && has(a.message, "SMTP.Send"),
              "jeton refuse : " + a.message);
        check(waitFor([&] { return srv.event("oauth", "defi_vide_recu"); }, 2000), "le defi d'erreur (334) a recu la ligne vide, puis 535");
        mail::Config sa = x;
        sa.user = "smtpauth@exemple.invalid";
        const mail::SendResult b = keepSend(mail::sendWithToken(sa, "essai_jeton_acces_direct", small, 10000));
        check(!b.ok && b.reached && has(b.message, "SMTP authentifi\xC3\xA9") && has(b.message, "5.7.139") && has(b.message, "smtpauth@exemple.invalid"),
              "SMTP AUTH coupe (Microsoft 365) : " + b.message);
        const mail::SendResult cc = keepSend(mail::sendWithToken(x, "essai_jeton_acces_fuite", small, 10000));
        check(!cc.ok && cc.reached && has(cc.message, "535") && !has(cc.message, "essai_jeton_acces_fuite") && !has(cc.message, "Authentication unsuccessful"),
              "un serveur qui recite le jeton : tu (" + cc.message + ")");
        mail::Config plain = x;
        plain.port = srv.port("starttls");
        const mail::SendResult d = keepSend(mail::sendWithToken(plain, "essai_jeton_acces_direct", small, 10000));
        check(!d.ok && d.reached && has(d.message, "ne propose pas l'identification par compte Microsoft (AUTH XOAUTH2)"), "sans XOAUTH2 : " + d.message);
        mail::Config like = x;
        like.port = srv.port("microsoft");
        const std::size_t n = srv.messages("microsoft").size();
        const mail::SendResult e = keepSend(mail::sendWithToken(like, "essai_jeton_acces_direct", small, 10000));
        const auto msGot = srv.messages("microsoft");
        check(e.ok && msGot.size() == n + 1 && msGot.back().str("mecanisme") == "XOAUTH2" && msGot.back().str("jeton") == "essai_jeton_acces_direct",
              "comme smtp-mail.outlook.com (AUTH LOGIN XOAUTH2) : XOAUTH2 choisi, envoye (" + e.message + ")");
        mail::Config pw = baseConfig(source);
        pw.port = srv.port("microsoft");
        const mail::SendResult f = keepSend(mail::sendWith(pw, kPassword, small, 10000));
        check(!f.ok && f.reached && has(f.message, "compte Microsoft") && has(f.message, "5.7.139") && srv.event("microsoft", "mdp_retire"),
              "un mot de passe chez Microsoft : la phrase dit de choisir le compte Microsoft (" + f.message + ")");
    }

#if !defined(_WIN32)
    // ---- 13. l'autorite injoignable
    {
        mail::Config cn = c;
        cn.authority = "http://127.0.0.1:" + std::to_string(closedPort());
        mail::DeviceCode d;
        const bool started = mail::microsoftStart(cn, d, &why);
        keep(why);
        check(!started && has(why, "ne r\xC3\xA9pond pas pour commencer la connexion") && has(why, "Internet"), "autorite injoignable (commencer) : " + why);
        const std::string N = U(tmp / "editeur-ms-injoignable");
        check(mail::saveConfig(N, cn, &why) && plantLink(N, "essai_jeton_rafraichir_pose", cn.authority), "une connexion vers une autorite injoignable");
        const mail::SendResult r = keepSend(mail::send(N, small, 3000));
        check(!r.ok && !r.reached && has(r.message, "ne r\xC3\xA9pond pas pour donner le jeton d'envoi") && mail::microsoftConnected(N),
              "autorite injoignable (envoyer) : la phrase, la connexion gardee (" + r.message + ")");
    }
#endif

    // ---- 14. oublier
    check(mail::forget(F, &why) && !mail::microsoftConnected(F) && !fs::exists(fs::path(mail::microsoftPath(F))), "forget : la connexion Microsoft aussi");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_mail_test SOURCE\n");
        return 2;
    }
    const std::string source = argv[1];
    if (!crypto::init()) {
        std::printf("ECHEC : libsodium ne demarre pas\n");
        return 1;
    }
    std::random_device rd;
    const fs::path tmp = fs::temp_directory_path() / ("xpg-courriel-essai-" + std::to_string(rd()) + std::to_string(rd()));
    std::error_code ec;
    fs::create_directories(tmp, ec);

    offline(source, tmp);
    offlineMicrosoft(source, tmp);   // 1.38.0

    int rc = 0;
    {
        TestServer srv(source, tmp / "serveur", "smtp_essai.py", {U(fs::path(source) / "tests" / "fixtures" / "mail")});
        if (srv.skipped()) {
            std::printf("SAUTE : %s\n", srv.error().c_str());
        } else if (!srv.ready()) {
            check(false, "le serveur SMTP d'essai : " + srv.error());
        } else {
            std::vector<std::string> said;   // toutes les phrases : aucun secret ne doit y etre
            int delivered = 0;               // les envois reussis : chacun doit etre arrive, et rien d'autre
            online(source, tmp, srv, said, delivered);
            // 1.38.0 : le compte Microsoft, contre le faux serveur OAuth (le transport HTTPS du systeme : libcurl ici)
            std::string httpWhy;
            if (!app::ai::httpAvailable(&httpWhy)) {
                std::printf("SAUTE : le compte Microsoft (OAuth) : %s\n", httpWhy.c_str());
            } else {
                TestServer oauth(source, tmp / "oauth", "oauth_essai.py", {});
                if (oauth.skipped()) {
                    std::printf("SAUTE : %s\n", oauth.error().c_str());
                } else if (!oauth.ready()) {
                    check(false, "le faux serveur OAuth : " + oauth.error());
                } else {
                    microsoftOnline(source, tmp, srv, oauth, said, delivered);
                    check(oauth.stop(), "le faux serveur OAuth s'arrete sur SIGTERM");
                }
            }
            // ---- a la fin
            check(static_cast<int>(srv.messages().size()) == delivered,
                  "chaque envoi reussi est arrive, et rien d'autre (" + std::to_string(srv.messages().size()) + " messages, " + std::to_string(delivered) + " envois)");
            bool secret = false;
            for (const auto& msg : said) secret = secret || has(msg, kPassword) || has(msg, crypto::base64(kPassword)) || has(msg, kSecretMark);
            check(!secret, "ni le mot de passe, ni un jeton, ni un code n'apparait dans aucune phrase (" + std::to_string(said.size()) + " phrases)");
            check(srv.stop(), "le serveur d'essai s'arrete sur SIGTERM");
        }
    }
    fs::remove_all(tmp, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    rc = g_failures ? 1 : 0;
    return rc;
}
