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
//    admin_mail_test SOURCE
//  Sans python3 (ou hors Linux/macOS) : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Mail.hpp"

#include <chrono>
#include <cstdio>
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

// ---- le serveur SMTP d'essai (tests/outils/smtp_essai.py) ----
class SmtpServer {
public:
    SmtpServer(const std::string& source, const fs::path& dir) : dir_(dir) {
#if defined(_WIN32)
        (void)source;
        skipped_ = true;
        error_ = "le serveur SMTP d'essai n'est lance que sous Linux/macOS (python3)";
#else
        const std::string script = U(fs::path(source) / "tests" / "outils" / "smtp_essai.py");
        const std::string fixtures = U(fs::path(source) / "tests" / "fixtures" / "mail");
        out_ = U(dir / "recus.json");
        const std::string ports = U(dir / "ports.json");
        const std::string log = U(dir / "serveur.log");
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!fs::exists(script, ec)) {
            error_ = "introuvable : " + script;
            return;
        }
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
            ::execlp("python3", "python3", script.c_str(), fixtures.c_str(), out_.c_str(), ports.c_str(), static_cast<char*>(nullptr));
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
                    error_ = "le serveur d'essai s'est arrete (code " + std::to_string(code) + ") : " + readFile(log);
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        error_ = "le serveur d'essai n'a pas demarre en 20 s : " + readFile(log);
#endif
    }
    ~SmtpServer() { stop(); }
    SmtpServer(const SmtpServer&) = delete;
    SmtpServer& operator=(const SmtpServer&) = delete;

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
    // Le journal du serveur : "messages" et "evenements".
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
void online(const std::string& source, const fs::path& tmp, SmtpServer& srv) {
    std::printf("-- contre le serveur SMTP d'essai (STARTTLS %d, TLS %d)\n", srv.port("starttls"), srv.port("tls"));
    std::vector<std::string> allMessages;   // pour verifier a la fin que le mot de passe n'y est jamais
    const auto keep = [&](const mail::SendResult& r) {
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

    // ---- a la fin
    check(srv.messages().size() == 2, "deux messages en tout sont arrives (" + std::to_string(srv.messages().size()) + ")");
    bool secret = false;
    for (const auto& msg : allMessages) secret = secret || has(msg, kPassword) || has(msg, crypto::base64(kPassword));
    check(!secret, "le mot de passe n'apparait dans aucune phrase");
    check(srv.stop(), "le serveur d'essai s'arrete sur SIGTERM");
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

    int rc = 0;
    {
        SmtpServer srv(source, tmp / "serveur");
        if (srv.skipped()) {
            std::printf("SAUTE : %s\n", srv.error().c_str());
        } else if (!srv.ready()) {
            check(false, "le serveur SMTP d'essai : " + srv.error());
        } else {
            online(source, tmp, srv);
        }
    }
    fs::remove_all(tmp, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    rc = g_failures ? 1 : 0;
    return rc;
}
