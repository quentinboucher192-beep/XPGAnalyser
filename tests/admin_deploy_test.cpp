// =============================================================================
//  tests/admin_deploy_test.cpp - 1.45.0 : << DEPLOYER LE SERVICE >> DEPUIS L'ADMIN
//  (license_admin/ServiceDeploy, le dialogue DeployDialog.cpp, la console
//  service-deployer) et LA VERSION DU SERVICE face a l'Admin (le bandeau, la pastille)
// -----------------------------------------------------------------------------
//  Rien ne part chez Cloudflare : un FAUX KIT (un faux deployer.mjs, un script sh
//  que lance un faux node ; un faux npx pour la connexion) et un faux service
//  (/v1/etat sur 127.0.0.1) ; les jetons sont des jetons d'essai (essai_jeton_...)
//  et doivent rester masques partout. Sous Windows : les fausses commandes sont des
//  scripts sh, les essais des processus sont sautes.
//  argv[1] : la racine du depot ; argv[2] : la console XPG-License-Admin.
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/license_admin/ServiceDeploy.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/menu/MenuManager.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace admintest;
namespace dep = license_admin::deployer;
namespace online = license_admin::online;

namespace {

void writeText(const fs::path& p, const std::string& text, bool exec = false) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary) << text;
    if (exec) fs::permissions(p, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, ec);
}

bool anyHas(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& l : lines)
        if (has(l, part)) return true;
    return false;
}

// Le faux deployer.mjs : un script sh (le faux node le lance avec /bin/sh) ; ce qu'il fait : XPG_ESSAI_DEPLOYEUR.
const char* const kFakeDeployer =
    "# faux deployer.mjs (essais) : un script sh\n"
    "echo \"Mise en ligne du service des licences XPG (Cloudflare Workers)\"\n"
    "echo \"==============================================================\"\n"
    "case \"${XPG_ESSAI_DEPLOYEUR:-succes}\" in\n"
    "succes|succeslent)\n"
    "  echo \"Dossier du service : $PWD/service\"\n"
    "  echo \"Cl\xC3\xA9 publique de l'\xC3\xA9" "diteur (cle-publique.txt) : 0123456789abcdef\xE2\x80\xA6\"\n"
    "  echo \"\"\n"
    "  echo \"Installation de wrangler (l'outil de Cloudflare), une seule fois : quelques minutes...\"\n"
    "  echo \"Envoi \xC3\xA0 Cloudflare (la premi\xC3\xA8re fois, ton navigateur s'ouvre pour te connecter)...\"\n"
    "  [ \"$XPG_ESSAI_DEPLOYEUR\" = succeslent ] && sleep 1\n"
    "  echo \"CLOUDFLARE_API_TOKEN=essai_jeton_A1b2C3d4\"\n"
    "  echo \"Authorization: Bearer essai_jeton_E5f6G7h8\" >&2\n"
    "  echo \"Opening a link in your default browser: https://dash.cloudflare.com/oauth2/auth?response_type=code&client_id=essai&state=essai_jeton_etat&code_challenge=essai_jeton_defi\"\n"
    "  echo \"jeton : essai_jeton_Z9y8X7w6V5u4T3s2R1q0P9o8N7m6L5k4J3i2H1g0F9e8\"\n"
    "  printf 'Uploaded \\033[32mxpg-licences\\033[0m (1.23 sec)\\n'\n"
    "  echo \"Ton service est en ligne : https://xpg-licences.essai-fictif.workers.dev\"\n"
    "  [ -n \"$1\" ] && echo \"argument : $1\"\n"
    "  echo \"PWD=$PWD\"\n"
    "  exit 0 ;;\n"
    "auth)\n"
    "  echo \"Envoi \xC3\xA0 Cloudflare...\"\n"
    "  echo \"\xE2\x9C\x98 [ERROR] In a non-interactive environment, it's necessary to set a CLOUDFLARE_API_TOKEN environment variable for wrangler to work.\" >&2\n"
    "  echo \"\" >&2\n"
    "  echo \"ERREUR : la mise en ligne a \xC3\xA9" "chou\xC3\xA9 (code 1).\" >&2\n"
    "  exit 1 ;;\n"
    "lent)\n"
    "  echo \"Envoi \xC3\xA0 Cloudflare...\"\n"
    "  sleep 30\n"
    "  echo \"jamais vu\"\n"
    "  exit 0 ;;\n"
    "autre)\n"
    "  echo \"Installation de wrangler (l'outil de Cloudflare), une seule fois : quelques minutes...\"\n"
    "  echo \"npm ERR! network request failed\" >&2\n"
    "  echo \"ERREUR : l'installation de wrangler a \xC3\xA9" "chou\xC3\xA9 (es-tu connect\xC3\xA9 \xC3\xA0 Internet ?).\" >&2\n"
    "  exit 4 ;;\n"
    "sousdomaine)\n"
    "  echo \"\xE2\x9C\x98 [ERROR] You need to register a workers.dev subdomain before publishing to workers.dev\" >&2\n"
    "  exit 1 ;;\n"
    "esac\n";

// Un faux kit (deployer.mjs, service/wrangler.toml, service/src/licences.js, cle-publique.txt) et de faux node, npx.
struct FakeKit {
    fs::path root, kit, bin, emptyBin, noKey;
    explicit FakeKit(const fs::path& base) : root(base / "deploiement") {
        kit = root / "ServiceLicences";
        bin = root / "bin";
        emptyBin = root / "sans-node";
        noKey = root / "kit-sans-cle";
        for (const fs::path& k : {kit, noKey}) {
            writeText(k / "deployer.mjs", kFakeDeployer);
            writeText(k / "deployer.bat", "@echo off\r\nrem faux (essais)\r\n");
            writeText(k / "service" / "wrangler.toml", "name = \"xpg-licences\"\n");
            writeText(k / "service" / "src" / "licences.js", "// faux (essais)\nconst VERSION = \"1.44.0\";\n");
        }
        // une cle PUBLIQUE d'essai (64 hexadecimaux) : rien de secret
        writeText(kit / "cle-publique.txt", "# essai\n" + std::string(64, 'a') + "\n");
        writeText(bin / "node", "#!/bin/sh\nexec /bin/sh \"$@\"\n", true);
        writeText(bin / "npx", "#!/bin/sh\necho \"npx $*\"\necho \"Attempting to login via OAuth...\"\n"
                               "echo \"Opening a link in your default browser: https://dash.cloudflare.com/oauth2/auth?client_id=essai&state=essai_jeton_etat2&code_challenge=essai_jeton_defi2\"\n"
                               "echo \"Successfully logged in.\"\nexit 0\n",
                  true);
        std::error_code ec;
        fs::create_directories(emptyBin, ec);
    }
    [[nodiscard]] std::string path() const { return U(bin) + ":/usr/bin:/bin"; }
};

#if !defined(_WIN32)
// Un faux service : /v1/etat (sa version a la demande) ; tout le reste : 403.
class FakeService {
public:
    FakeService() {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && listen(fd_, 8) == 0) {
            socklen_t n = sizeof a;
            getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &n);
            port_ = ntohs(a.sin_port);
        }
        thread_ = std::thread([this] { loop(); });
    }
    ~FakeService() {
        stop_ = true;
        thread_.join();
        close(fd_);
    }
    [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
    void setVersion(const std::string& v) {
        std::lock_guard<std::mutex> lk(m_);
        version_ = v;
    }
    [[nodiscard]] int calls() const { return calls_.load(); }

private:
    void loop() {
        while (!stop_) {
            pollfd p{fd_, POLLIN, 0};
            if (poll(&p, 1, 50) <= 0) continue;
            const int c = accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            handle(c);
            close(c);
        }
    }
    void handle(int c) {
        std::string req;
        char buf[4096];
        for (int i = 0; i < 100 && req.find("\r\n\r\n") == std::string::npos; ++i) {
            pollfd p{c, POLLIN, 0};
            if (poll(&p, 1, 50) <= 0) continue;
            const ssize_t n = read(c, buf, sizeof buf);
            if (n <= 0) break;
            req.append(buf, static_cast<std::size_t>(n));
        }
        ++calls_;
        std::string body, status = "200 OK";
        if (req.rfind("GET /v1/etat", 0) == 0) {
            std::lock_guard<std::mutex> lk(m_);
            body = "{\"service\":\"xpg-licences\",\"version\":\"" + version_ + "\",\"heure\":" + std::to_string(g_now) + ",\"certifie\":true,\"registre\":0}";
        } else {
            status = "403 Forbidden";
            body = "{\"erreur\":\"cle\",\"message\":\"cl\xC3\xA9 inconnue (essai)\"}";
        }
        const std::string out = "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body;
        (void)!write(c, out.data(), out.size());
    }
    int               fd_{-1};
    int               port_{0};
    std::atomic<bool> stop_{false};
    std::atomic<int>  calls_{0};
    std::mutex        m_;
    std::string       version_{"1.43.0"};
    std::thread       thread_;
};
#endif

// Toute la pile (la coquille et ce qui est ouvert), comme la fenetre.
std::string paintAll(menu::MenuManager& menus, TextRenderer& r, float w = 1400.f, float h = 900.f) {
    static const ui::Theme theme = ui::Theme::byName("Light");
    r.size = {w, h};
    menus.applyPending();
    const menu::FrameContext fc{0.016, 1.0, &theme, r.surfaceSize(), 1.f};
    menus.Update(fc);
    r.beginFrame(theme.color.windowBg);
    menus.Render(r, fc);
    r.endFrame();
    return r.all();
}

// Dessiner le dialogue jusqu'a ce qu'il montre `part` (le travail tourne sur un fil) ; vrai : vu.
bool waitShown(menu::IMenu& m, TextRenderer& tr, const std::string& part, int ms = 15000) {
    for (int waited = 0; waited < ms; waited += 25) {
        if (has(paintMenu(m, tr, 1100.f, 800.f), part)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

std::string runConsole(const std::string& exe, const std::string& env, const std::string& args) {
    const std::string cmd = env + " '" + exe + "' " + args + " 2>&1; echo \"code=$?\"";
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[4096];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        pclose(p);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    const std::string console = argc > 2 ? argv[2] : std::string();
    installMetrics();

    // ============================================================================ la version du service
    std::printf("-- la version du service : face a ce qu'il faut a l'Admin et a son kit, jamais face a l'Admin lui-meme\n");
    {
        check(std::string(dep::kServiceRequired) == "1.44.0", "1.45.0 n'a rien change au service : il en faut la 1.44.0");
        auto v = dep::checkVersion("1.43.0", "1.44.0");
        check(v.known && v.outdated && v.belowRequired && v.target == "1.44.0", "service 1.43.0, kit 1.44.0 : a mettre a jour, la 1.44.0");
        check(dep::outdatedMessage(v) == "Ton service en ligne est en 1.43.0 : d\xC3\xA9ploie la 1.44.0 pour les sessions par Internet, les admins et leurs droits, "
                                         "les fichiers et les mises \xC3\xA0 jour pouss\xC3\xA9" "es.",
              "la phrase du bandeau : " + dep::outdatedMessage(v));
        v = dep::checkVersion("1.44.0", "1.44.0");
        check(v.known && !v.outdated && dep::outdatedMessage(v).empty(), "service 1.44.0, kit 1.44.0 : a jour (l'Admin peut etre en 1.45.0)");
        v = dep::checkVersion("1.44.0", "1.45.0");
        check(v.outdated && !v.belowRequired && v.kitNewer && v.target == "1.45.0", "le kit de l'Admin porte un service plus recent : a mettre a jour");
        check(dep::outdatedMessage(v) == "Ton service en ligne est en 1.44.0, la 1.45.0 est pr\xC3\xAAte dans l'Admin : d\xC3\xA9ploie-la.", dep::outdatedMessage(v));
        v = dep::checkVersion("1.45.0", "1.44.0");
        check(v.known && !v.outdated, "un service plus recent que le kit : rien a dire");
        v = dep::checkVersion("1.43.0", "");
        check(v.outdated && v.target == "1.44.0", "sans kit lisible : face a ce qu'il faut a l'Admin");
        v = dep::checkVersion("1.43.0", "1.43.0");
        check(v.outdated && has(dep::outdatedMessage(v), "r\xC3\xA9installe l'Admin"), "un kit trop ancien : reinstaller l'Admin (" + dep::outdatedMessage(v) + ")");
        v = dep::checkVersion("", "1.44.0");
        check(!v.known && !v.outdated, "version inconnue (pas encore lue) : rien a dire");
        v = dep::checkVersion("pas une version", "1.44.0");
        check(!v.known && !v.outdated, "version illisible : rien a dire");
    }

    // ============================================================================ la sortie : nettoyee, sans secret
    std::printf("-- la sortie : sans couleurs, sans secret, lisible\n");
    {
        check(!has(dep::scrub("CLOUDFLARE_API_TOKEN=essai_jeton_123abc"), "essai_jeton_") && has(dep::scrub("CLOUDFLARE_API_TOKEN=essai_jeton_123abc"), "[masqu\xC3\xA9]"),
              "NOM_TOKEN=... masque : " + dep::scrub("CLOUDFLARE_API_TOKEN=essai_jeton_123abc"));
        check(!has(dep::scrub("Authorization: Bearer essai_jeton_x"), "essai_jeton_"), "Authorization: Bearer ... masque");
        check(!has(dep::scrub("https://dash.cloudflare.com/oauth2/auth?client_id=x&state=essai_jeton_a&code_challenge=essai_jeton_b&scope=y"), "essai_jeton_"),
              "le code et l'etat d'une adresse OAuth masques");
        check(has(dep::scrub("https://dash.cloudflare.com/oauth2/auth?client_id=x&state=s"), "client_id=x"), "le reste de l'adresse reste");
        check(dep::scrub("Authentication error [code: 10000]") == "Authentication error [code: 10000]", "<< [code: 10000] >> reste lisible");
        const std::string pub = std::string(64, 'b');
        check(has(dep::scrub("--var PUBLISHER_KEYS:" + pub), pub), "une cle PUBLIQUE (64 hexadecimaux) reste lisible");
        check(has(dep::scrub("api_key = Ab12Cd34Ef56Gh78Ij90Kl12Mn34Op56Qr78St90"), "[masqu\xC3\xA9]"), "api_key = ... masque");
        check(has(dep::scrub("le jeton Ab12Cd34Ef56Gh78Ij90Kl12Mn34Op56Qr78St90Uv"), "[masqu\xC3\xA9]"), "une longue suite opaque masquee");
        check(dep::scrub("\x1B[32mUploaded\x1B[0m xpg") == "Uploaded xpg", "les couleurs retirees");
        check(dep::scrub("a\xFF" "b") == "a?b", "un octet qui n'est pas de l'UTF-8 : ?");
        check(dep::scrub("Envoi \xC3\xA0 Cloudflare") == "Envoi \xC3\xA0 Cloudflare", "l'UTF-8 reste");
        const std::string longLine = dep::scrub(std::string(900, 'x') + " fin");
        check(longLine.size() <= 404 && has(longLine, "\xE2\x80\xA6"), "une ligne de 900 caracteres : coupee a 400");
        dep::Report r;
        r.exitCode = 1;
        dep::classify(r, {"ERREUR : Node.js 20.1.0 est trop ancien : installe la version LTS (22 ou plus r\xC3\xA9" "cente) depuis https://nodejs.org"}, false, false);
        check(r.outcome == dep::Outcome::NodeTooOld && r.link == "https://nodejs.org", "Node.js trop ancien : nodejs.org");
        r = {};
        r.exitCode = 1;
        dep::classify(r, {"X [ERROR] In a non-interactive environment, it is mandatory to specify an account ID, either by assigning its value to CLOUDFLARE_ACCOUNT_ID"},
                      false, false);
        check(r.outcome == dep::Outcome::Account, "plusieurs comptes Cloudflare : dit");
        r = {};
        r.exitCode = 1;
        dep::classify(r, {"Pas encore connect\xC3\xA9 \xC3\xA0 Cloudflare ? Lance \xC2\xAB npx wrangler login \xC2\xBB dans le dossier du service"}, false, false);
        check(r.outcome == dep::Outcome::Failed, "la phrase d'aide de deployer.mjs ne fait pas croire a une connexion absente");
    }

#if defined(_WIN32)
    std::printf("SAUTE : les faux deployeurs sont des scripts sh (Linux, macOS)\n");
    return finish();
#else
    const fs::path base = fs::temp_directory_path() / ("xpg-admin-deploiement-" + std::to_string(static_cast<long long>(::getpid())));
    {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
    FakeKit fk(base);

    // ============================================================================ le kit, Node.js
    std::printf("-- le kit et Node.js (comme deployer.bat : where node)\n");
    {
        dep::Kit k;
        std::string why;
        check(dep::findKit(U(fk.kit), k, &why) && k.folder == U(fk.kit) && k.publicKey && k.version == "1.44.0" && has(k.service, "service"),
              "le kit donne : deployer.mjs, le service 1.44.0, cle-publique.txt (" + why + ")");
        check(!dep::findKit(U(fk.bin), k, &why) && has(why, "deployer.mjs"), "un dossier qui n'est pas le kit : refuse (" + why + ")");
        check(dep::serviceVersionIn(U(fk.kit / "service")) == "1.44.0", "la version du service du kit (src/licences.js)");
        dep::NodeTools t;
        check(dep::findNode(fk.path(), U(fk.kit), t) && t.node == U(fk.bin / "node") && t.npx == U(fk.bin / "npx"), "node et npx trouves dans le PATH");
        check(!dep::findNode(U(fk.emptyBin), U(fk.kit), t), "sans node dans le PATH : introuvable");
        const auto cands = dep::kitCandidates({});
        check(!cands.empty() && has(cands.back(), "service-licences"), "sans dossier donne : service-licences a cote de l'Admin");
    }

    // ============================================================================ deployer : le succes
    std::printf("-- deployer : le succes (la sortie en direct, nettoyee ; l'adresse)\n");
    dep::Options o;
    o.kit = U(fk.kit);
    o.path = fk.path();
    {
        setenv("XPG_ESSAI_DEPLOYEUR", "succes", 1);
        std::vector<std::string> live;
        const dep::Report r = dep::deploy(o, [&](const std::string& l) { live.push_back(l); });
        check(r.ok() && r.exitCode == 0, "en ligne : " + r.text());
        check(r.url == "https://xpg-licences.essai-fictif.workers.dev", "l'adresse lue dans la sortie : " + r.url);
        check(anyHas(live, "Envoi \xC3\xA0 Cloudflare") && anyHas(live, "Installation de wrangler") && anyHas(live, "Kit du service : ") && anyHas(live, "Node.js : "),
              "chaque ligne arrive (le kit, Node.js, puis deployer.mjs)");
        check(anyHas(live, "PWD=" + U(fk.kit)), "il tourne dans le dossier du kit");
        check(anyHas(live, "Uploaded xpg-licences (1.23 sec)"), "sans les couleurs");
        bool secret = false, esc = false;
        for (const auto& l : live) {
            secret = secret || has(l, "essai_jeton_");
            esc = esc || has(l, "\x1B");
        }
        check(!secret && anyHas(live, "[masqu\xC3\xA9]"), "aucun jeton d'essai dans la sortie (masques)");
        check(!esc, "aucune sequence de couleur");
        check(!has(r.text(), "essai_jeton_"), "ni dans le compte rendu");
        check(!dep::running(), "fini : plus rien ne tourne");
    }
    {
        std::vector<std::string> live;
        o.dryRun = true;
        const dep::Report r = dep::deploy(o, [&](const std::string& l) { live.push_back(l); });
        o.dryRun = false;
        check(r.ok() && anyHas(live, "argument : --essai") && has(r.reason, "Essai r\xC3\xA9ussi"), "--essai : passe a deployer.mjs (" + r.reason + ")");
    }

    // ============================================================================ deployer : les echecs
    std::printf("-- deployer : les echecs dits en francais, avec l'etape suivante\n");
    {
        setenv("XPG_ESSAI_DEPLOYEUR", "auth", 1);
        dep::Report r = dep::deploy(o);
        check(r.outcome == dep::Outcome::NotLoggedIn && r.exitCode == 1 && has(r.next, "Se connecter \xC3\xA0 Cloudflare"), "pas connecte : " + r.text());
        setenv("XPG_ESSAI_DEPLOYEUR", "autre", 1);
        r = dep::deploy(o);
        check(r.outcome == dep::Outcome::Failed && r.exitCode == 4 && has(r.reason, "l'installation de wrangler a \xC3\xA9" "chou\xC3\xA9") && anyHas(r.tail, "npm ERR!"),
              "un autre echec : la raison (ERREUR : ...) et les dernieres lignes : " + r.text());
        setenv("XPG_ESSAI_DEPLOYEUR", "sousdomaine", 1);
        r = dep::deploy(o);
        check(r.outcome == dep::Outcome::Subdomain && r.link == "https://dash.cloudflare.com", "pas de sous-domaine workers.dev : " + r.reason);
        // Node.js absent : rien ne se lance
        dep::Options none = o;
        none.path = U(fk.emptyBin);
        std::vector<std::string> live;
        r = dep::deploy(none, [&](const std::string& l) { live.push_back(l); });
        check(r.outcome == dep::Outcome::NodeMissing && r.link == "https://nodejs.org" && has(r.reason, "Node.js n'est pas install\xC3\xA9") && r.exitCode == -1,
              "Node.js absent : nodejs.org (" + r.text() + ")");
        check(!anyHas(live, "Mise en ligne du service des licences"), "Node.js absent : deployer.mjs n'est pas lance");
        // pas de kit, pas de cle publique
        dep::Options noKit = o;
        noKit.kit = U(fk.emptyBin);
        r = dep::deploy(noKit);
        check(r.outcome == dep::Outcome::KitMissing && has(r.reason, "deployer.mjs"), "pas de kit : " + r.reason);
        dep::Options noKey = o;
        noKey.kit = U(fk.noKey);
        r = dep::deploy(noKey);
        check(r.outcome == dep::Outcome::KeyMissing && has(r.reason, "cle-publique.txt"), "pas de cle-publique.txt : " + r.reason);
    }

    // ============================================================================ annuler, un seul a la fois
    std::printf("-- annuler ; un seul deploiement a la fois\n");
    {
        setenv("XPG_ESSAI_DEPLOYEUR", "lent", 1);
        std::atomic<bool> cancel{false};
        std::atomic<bool> sent{false};
        dep::Report r;
        std::vector<std::string> live;
        std::mutex m;
        const auto t0 = std::chrono::steady_clock::now();
        std::thread worker([&] {
            r = dep::deploy(o, [&](const std::string& l) {
                std::lock_guard<std::mutex> lk(m);
                live.push_back(l);
                if (has(l, "Envoi")) sent = true;
            }, &cancel);
        });
        for (int i = 0; i < 400 && !sent; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        check(sent.load() && dep::running(), "il tourne (l'envoi a commence)");
        const dep::Report busy = dep::deploy(o);
        check(busy.outcome == dep::Outcome::Busy && has(busy.reason, "tourne d\xC3\xA9j\xC3\xA0"), "un second deploiement en meme temps : refuse");
        const dep::Report busyLogin = dep::login(o);
        check(busyLogin.outcome == dep::Outcome::Busy, "la connexion aussi attend");
        cancel = true;
        worker.join();
        const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(r.outcome == dep::Outcome::Cancelled && spent < 12.0, "Annuler : arrete en " + std::to_string(spent) + " s (" + r.reason + ")");
        check(!anyHas(live, "jamais vu"), "le faux deployeur (et son sleep) n'a pas fini");
        check(!dep::running(), "plus rien ne tourne");
        // stopAll (la fenetre se ferme) : comme Annuler
        sent = false;
        std::thread w2([&] {
            r = dep::deploy(o, [&](const std::string& l) {
                if (has(l, "Envoi")) sent = true;
            });
        });
        for (int i = 0; i < 400 && !sent; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        dep::stopAll();
        w2.join();
        check(r.outcome == dep::Outcome::Cancelled, "stopAll : arrete");
        setenv("XPG_ESSAI_DEPLOYEUR", "succes", 1);
        check(dep::deploy(o).ok(), "apres stopAll, un nouveau deploiement part");
    }

    // ============================================================================ se connecter a Cloudflare
    std::printf("-- se connecter a Cloudflare (npx wrangler login)\n");
    {
        std::vector<std::string> live;
        const dep::Report r = dep::login(o, [&](const std::string& l) { live.push_back(l); });
        check(r.ok() && has(r.reason, "connect\xC3\xA9 \xC3\xA0 ton compte Cloudflare"), "connecte : " + r.text());
        check(anyHas(live, "npx --yes wrangler login") && anyHas(live, "Successfully logged in."), "npx --yes wrangler login, dans le dossier du service");
        check(!anyHas(live, "essai_jeton_"), "l'etat et le defi de l'adresse OAuth : masques");
    }

    // ============================================================================ la console
    std::printf("-- la console : service-deployer\n");
    if (console.empty() || !fs::exists(console)) {
        std::printf("SAUTE : la console (argv[2]) manque\n");
    } else {
        const std::string aide = runConsole(console, "", "--aide");
        check(has(aide, "service-deployer [--dossier-kit D]") && has(aide, "kit install\xC3\xA9 avec l'Admin"), "l'aide dit service-deployer");
        const std::string env = "PATH='" + fk.path() + "'";
        std::string out = runConsole(console, env + " XPG_ESSAI_DEPLOYEUR=succes", "service-deployer --dossier-kit '" + U(fk.kit) + "'");
        check(has(out, "Ton service est en ligne : https://xpg-licences.essai-fictif.workers.dev") && has(out, "Le service est en ligne") && has(out, "code=0"),
              "en ligne : la sortie, puis le compte rendu ; code 0");
        check(!has(out, "essai_jeton_"), "la console ne montre aucun jeton");
        out = runConsole(console, env + " XPG_ESSAI_DEPLOYEUR=autre", "service-deployer --dossier-kit '" + U(fk.kit) + "'");
        check(has(out, "l'installation de wrangler a \xC3\xA9" "chou\xC3\xA9") && has(out, "Les derni\xC3\xA8res lignes") && has(out, "code=4"), "un echec : le code du deployeur (4)");
        out = runConsole(console, "PATH='" + U(fk.emptyBin) + "'", "service-deployer --dossier-kit '" + U(fk.kit) + "'");
        check(has(out, "Node.js n'est pas install\xC3\xA9") && has(out, "https://nodejs.org") && has(out, "code=1"), "Node.js absent : nodejs.org, code 1");
        out = runConsole(console, env, "service-deployer --dossier-kit '" + U(fk.emptyBin) + "'");
        check(has(out, "introuvable") && has(out, "code=1"), "pas de kit : dit");
    }

    // ============================================================================ l'Admin : le bandeau, la pastille, le dialogue
    std::printf("-- l'Admin : le service en ligne en 1.43.0 (le bandeau, la pastille, le tableau de bord)\n");
    std::vector<std::string> opened;   // les adresses ouvertes (nodejs.org)
    std::mutex openedMutex;
    dep::setTestHooks({U(fk.kit), fk.path(), [&](const std::string& u) {
                           std::lock_guard<std::mutex> lk(openedMutex);
                           opened.push_back(u);
                           return std::string();
                       }});
    FakeService svc;
    Fixture fx(source, "deploiement-gui");
    check(fx.ready(), "le poste d'essai : cle d'essai et registre");
    check(fx.ws->serviceConfigure(svc.url()).ok, "le faux service regle : " + svc.url());
    {
        online::ServiceState st;
        st.reached = true;
        st.version = "1.43.0";
        st.adminOk = true;
        st.certOk = true;
        st.certExpires = g_now + 200 * 86400;
        fx.ws->adoptServiceState(st);
    }
    const auto ver = gui::serviceVersion(*fx.ws);
    check(ver.outdated && ver.online == "1.43.0" && ver.target == "1.44.0" && ver.kit == "1.44.0", "le Workspace : 1.43.0 en ligne, la 1.44.0 dans le kit");
    TextRenderer tr;
    HostRig rig(fx);
    rig.slowOnThread = true;
    auto sp = gui::makeServicePage(rig.host());
    auto db = gui::makeDashboardPage(rig.host());
    rig.refreshers.push_back([&] {
        sp->refresh();
        db->refresh();
    });
    {
        sp->refresh();
        const std::string t = paint(*sp, tr, 1100.f, 3200.f);
        check(has(t, "Ton service en ligne est \xC3\xA0 mettre \xC3\xA0 jour") && has(t, "Ton service en ligne est en 1.43.0 : d\xC3\xA9ploie la 1.44.0 pour les sessions par Internet"),
              "Service en ligne : le bandeau");
        check(has(t, "\xC3\xA0 mettre \xC3\xA0 jour : la 1.44.0 est pr\xC3\xAAte"), "l'etat du service dit sa version et qu'elle est a mettre a jour");
        for (const char* id : {"service.deployer", "service.deployer.bandeau", "service.deployer.etapes"}) {
            auto* b = find<ui::Button>(*sp, id);
            check(b && b->visible() && b->enabled(), std::string("le bouton ") + id + " (Deployer le service)");
        }
        check(has(t, "Mettre le service en ligne (une fois)") && has(t, "npx wrangler login") && has(t, "deployer.bat"), "les etapes de la mise en ligne restent");
        db->refresh();
        const std::string d = paint(*db, tr, 1300.f, 900.f);
        auto* b = find<ui::Button>(*db, "tableau.deployer");
        check(has(d, "Ton service en ligne est en 1.43.0 : d\xC3\xA9ploie la 1.44.0") && b && b->visible(), "le tableau de bord : le bandeau et Deployer le service");
    }
    {
        menu::MenuManager menus{menu::MenuFactory{}};
        auto owned = std::make_unique<gui::Shell>(U(fx.admin), [] { return g_now; });
        gui::Shell* shell = owned.get();
        menus.PushMenu(std::move(owned));
        menus.applyPending();
        online::ServiceState st = fx.ws->service().state;
        shell->workspace().adoptServiceState(st);
        shell->reloadAll();   // comme la lecture du lancement : les pages relues
        TextRenderer r;
        const std::string t = paintAll(menus, r, 1400.f, 1300.f);   // la carte du service : en bas du menu, s'il y a la place
        check(has(shell->syncStatusText(), "\xC2\xB7 \xC3\xA0 mettre \xC3\xA0 jour") && has(t, "\xC3\xA0 mettre \xC3\xA0 jour"), "la pastille du haut : " + shell->syncStatusText());
        check(has(t, "En 1.43.0 \xC2\xB7 \xC3\xA0 mettre \xC3\xA0 jour"), "la carte du service, en bas du menu");
        check(has(t, "Ton service en ligne est en 1.43.0"), "le tableau de bord de la coquille");
    }
    std::printf("-- au lancement : la version lue sans bruit, un avis\n");
    {
        gui::Shell shell(U(fx.admin), [] { return g_now; });
        check(!shell.workspace().service().hasState, "une coquille neuve : l'etat pas encore lu");
        check(shell.checkServiceVersion(), "la lecture part (sans bruit)");
        check(!shell.busy(), "elle ne bloque rien (pas << occupe >>)");
        check(shell.finishSlow(), "elle finit");
        check(shell.workspace().service().hasState && shell.workspace().service().state.version == "1.43.0", "l'etat lu : 1.43.0");
        const auto* last = shell.lastMessage();
        check(last && has(last->result.message, "Ton service en ligne est en 1.43.0 : d\xC3\xA9ploie la 1.44.0") && has(last->result.message, "D\xC3\xA9ployer le service"),
              "un avis : " + std::string(last ? last->result.message : ""));
        check(!shell.checkServiceVersion(), "une fois seulement (l'etat est lu)");
    }

    std::printf("-- Deployer le service : la sortie en direct, puis << Service en ligne : version 1.44.0 >>\n");
    {
        setenv("XPG_ESSAI_DEPLOYEUR", "succeslent", 1);
        check(click(*sp, "service.deployer") && !rig.dialogs.empty(), "le dialogue s'ouvre et la mise en ligne part");
        menu::IMenu& dlg = *rig.dialogs.back();
        check(waitShown(dlg, tr, "Envoi \xC3\xA0 Cloudflare"), "le journal montre la sortie en direct");
        std::string t = paintMenu(dlg, tr, 1100.f, 800.f);
        auto* cancel = find<ui::Button>(*dlg.widgetRoot(), "deploiement.annuler");
        check(cancel && cancel->visible() && cancel->enabled(), "pendant : Annuler");
        check(has(t, "D\xC3\xA9ployer le service en ligne") && has(t, "1. Node.js") && has(t, "3. Envoi \xC3\xA0 Cloudflare"), "le titre et les etapes");
        (void)paint(*sp, tr, 1100.f, 3200.f);   // la page voit le travail lent
        check(rig.bg.running() && find<ui::Button>(*sp, "service.deployer") && !find<ui::Button>(*sp, "service.deployer")->enabled(), "un seul a la fois : le bouton est gris");
        check(!click(*sp, "service.deployer"), "un second clic ne fait rien");
        svc.setVersion("1.44.0");   // le faux service, une fois deploye
        check(rig.finishSlow(), "la mise en ligne finit");
        check(rig.finishSlow(), "puis l'etat est relu (/v1/etat)");
        t = paintMenu(dlg, tr, 1100.f, 800.f);
        check(has(t, "Service en ligne : version 1.44.0"), "Service en ligne : version 1.44.0");
        check(has(t, "Adresse : https://xpg-licences.essai-fictif.workers.dev"), "l'adresse donnee par la mise en ligne");
        check(!has(t, "essai_jeton_"), "aucun jeton a l'ecran");
        check(fx.ws->service().state.version == "1.44.0" && !gui::serviceVersion(*fx.ws).outdated, "l'Admin le sait : a jour");
        bool reported = false;
        for (const auto& r : rig.reports) reported = reported || (r.ok && has(r.message, "Mise en ligne du service : Le service est en ligne"));
        check(reported, "le resultat au journal de la seance");
        for (const auto& r : rig.reports) check(!has(r.text(), "essai_jeton_"), "aucun jeton dans un resultat");
        const std::string page = paint(*sp, tr, 1100.f, 3200.f);
        check(!has(page, "Ton service en ligne est \xC3\xA0 mettre \xC3\xA0 jour") && has(page, "version 1.44.0"), "la page : plus de bandeau, la 1.44.0");
        auto* close = find<ui::Button>(*dlg.widgetRoot(), "deploiement.fermer");
        auto* copy = find<ui::Button>(*dlg.widgetRoot(), "deploiement.copier");
        g_copied.clear();
        check(copy && copy->visible() && click(*dlg.widgetRoot(), "deploiement.copier") && has(g_copied, "Envoi \xC3\xA0 Cloudflare") && !has(g_copied, "essai_jeton_"),
              "Copier le journal : sans aucun jeton");
        check(close && close->visible() && click(*dlg.widgetRoot(), "deploiement.fermer"), "Fermer");
    }

    std::printf("-- pas connecte a Cloudflare : Se connecter a Cloudflare..., puis Deployer de nouveau\n");
    {
        svc.setVersion("1.43.0");
        setenv("XPG_ESSAI_DEPLOYEUR", "auth", 1);
        check(click(*sp, "service.deployer") && rig.finishSlow(), "la mise en ligne echoue");
        menu::IMenu& dlg = *rig.dialogs.back();
        std::string t = paintMenu(dlg, tr, 1100.f, 800.f);
        check(has(t, "Ce PC n'est pas connect\xC3\xA9 \xC3\xA0 ton compte Cloudflare") && has(t, "connecte-toi et clique \xC2\xAB Allow \xC2\xBB"), "la raison et l'etape suivante");
        auto* login = find<ui::Button>(*dlg.widgetRoot(), "deploiement.connecter");
        check(login && login->visible(), "Se connecter a Cloudflare...");
        check(click(*dlg.widgetRoot(), "deploiement.connecter") && rig.finishSlow(), "npx wrangler login");
        t = paintMenu(dlg, tr, 1100.f, 800.f);
        check(has(t, "Connect\xC3\xA9 \xC3\xA0 Cloudflare") && has(t, "npx --yes wrangler login"), "connecte");
        auto* again = find<ui::Button>(*dlg.widgetRoot(), "deploiement.deployer");
        check(again && again->visible(), "Deployer le service, de nouveau");
        setenv("XPG_ESSAI_DEPLOYEUR", "succes", 1);
        svc.setVersion("1.44.0");
        check(click(*dlg.widgetRoot(), "deploiement.deployer") && rig.finishSlow() && rig.finishSlow(), "la mise en ligne, puis la verification");
        t = paintMenu(dlg, tr, 1100.f, 800.f);
        check(has(t, "Service en ligne : version 1.44.0"), "en ligne apres la connexion");
        (void)click(*dlg.widgetRoot(), "deploiement.fermer");
    }

    std::printf("-- Node.js absent : nodejs.org ; Annuler\n");
    {
        dep::setTestHooks({U(fk.kit), U(fk.emptyBin), [&](const std::string& u) {
                               std::lock_guard<std::mutex> lk(openedMutex);
                               opened.push_back(u);
                               return std::string();
                           }});
        check(click(*sp, "service.deployer") && rig.finishSlow(), "sans Node.js");
        menu::IMenu& dlg = *rig.dialogs.back();
        const std::string t = paintMenu(dlg, tr, 1100.f, 800.f);
        check(has(t, "Node.js n'est pas install\xC3\xA9") && has(t, "nodejs.org"), "la raison : Node.js");
        auto* link = find<ui::Button>(*dlg.widgetRoot(), "deploiement.lien");
        check(link && link->visible() && link->text() == "Ouvrir nodejs.org", "Ouvrir nodejs.org");
        check(click(*dlg.widgetRoot(), "deploiement.lien") && !opened.empty() && opened.back() == "https://nodejs.org", "nodejs.org s'ouvre dans le navigateur");
        auto* retry = find<ui::Button>(*dlg.widgetRoot(), "deploiement.reessayer");
        check(retry && retry->visible(), "Reessayer");
        (void)click(*dlg.widgetRoot(), "deploiement.fermer");

        dep::setTestHooks({U(fk.kit), fk.path(), {}});
        setenv("XPG_ESSAI_DEPLOYEUR", "lent", 1);
        check(click(*sp, "service.deployer"), "une mise en ligne lente");
        menu::IMenu& slow = *rig.dialogs.back();
        check(waitShown(slow, tr, "Envoi \xC3\xA0 Cloudflare"), "elle a commence");
        const auto t0 = std::chrono::steady_clock::now();
        check(click(*slow.widgetRoot(), "deploiement.annuler") && rig.finishSlow(), "Annuler");
        const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const std::string a = paintMenu(slow, tr, 1100.f, 800.f);
        check(has(a, "Mise en ligne arr\xC3\xAAt\xC3\xA9" "e \xC3\xA0 ta demande") && spent < 12.0, "arretee en " + std::to_string(spent) + " s");
        auto* retry2 = find<ui::Button>(*slow.widgetRoot(), "deploiement.reessayer");
        check(retry2 && retry2->visible() && !dep::running(), "Reessayer ; plus rien ne tourne");
        (void)click(*slow.widgetRoot(), "deploiement.fermer");
    }
    dep::setTestHooks({});
    {
        std::error_code ec;
        fs::remove_all(base, ec);
    }
    return finish();
#endif
}
