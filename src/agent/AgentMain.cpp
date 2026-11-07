// =============================================================================
//  agent/AgentMain.cpp - 1.28.0 : XPG-Deployment-Agent, l'agent du PC de maintenance
// -----------------------------------------------------------------------------
//    XPG-Deployment-Agent --servir [--dossier D] [--port 47861] [--port-decouverte 47862]
//    XPG-Deployment-Agent --etat [--dossier D]        ce que fait l'agent (sans reseau)
//    XPG-Deployment-Agent --appairer [--dossier D]    un code a 6 chiffres (5 minutes)
//    XPG-Deployment-Agent --demarrer-avec-session oui|non
//                         1.29.0 : l'agent demarre avec la session de l'operateur
//                         (HKCU\...\Run << XPG Deployment Agent >> ; Linux : un .desktop)
//    XPG-Deployment-Agent --version
//  Une console, sans SDL ni editeur : tout le travail est dans deploy/Agent. Les
//  messages sont en francais (UTF-8 ; la console Windows est mise en UTF-8).
// =============================================================================
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <csignal>
#endif

#include "../core/SystemOpen.hpp"   // 1.32.0 : hardenLibrarySearch
#include "../deploy/Agent.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Journal.hpp"
#include "../deploy/Pairing.hpp"
#include "../deploy/Paths.hpp"
#include "../deploy/Protocol.hpp"
#include "../deploy/Station.hpp"

#include <mbedtls/build_info.h>
#include <sodium.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

#if defined(_WIN32)
BOOL WINAPI onConsole(DWORD) {
    g_stop = true;
    return TRUE;
}
#else
void onSignal(int) { g_stop = true; }
#endif

void say(const std::string& s) {
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void complain(const std::string& s) {
    std::fwrite(s.data(), 1, s.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

// Les arguments en UTF-8 (sous Windows : depuis la ligne de commande large, un dossier accentue passe).
std::vector<std::string> arguments(int argc, char** argv) {
    std::vector<std::string> out;
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; w && i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 1 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    if (w) LocalFree(w);
#else
    for (int i = 1; i < argc; ++i) out.emplace_back(argv[i]);
#endif
    return out;
}

std::string localTime(std::int64_t ms, const char* format) {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, format, &tm);
    return buf;
}

std::string fpText(const std::string& hexFp) {
    deploy::crypto::Bytes b;
    if (!deploy::crypto::unhex(hexFp, b, 32)) return "?";
    deploy::crypto::Digest d{};
    std::copy(b.begin(), b.end(), d.begin());
    return deploy::crypto::fingerprintText(d, 0);
}

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

void usage() {
    say("XPG-Deployment-Agent " + std::string(deploy::kAgentVersion)
        + " - l'agent de d\xC3\xA9ploiement du PC de maintenance\n"
          "\n"
          "  --servir            \xC3\xA9" "coute IHM Studio (TCP " + std::to_string(deploy::protocol::kAgentPort)
        + ", d\xC3\xA9" "couverte UDP " + std::to_string(deploy::protocol::kDiscoveryPort) + ") jusqu'\xC3\xA0 Ctrl+C\n"
          "  --etat              ce que fait l'agent (sans r\xC3\xA9seau)\n"
          "  --appairer          affiche un code \xC3\xA0 6 chiffres \xC3\xA0 taper dans IHM Studio (5 minutes, 3 essais)\n"
          "  --demarrer-avec-session oui|non\n"
          "                      l'agent d\xC3\xA9marre (ou plus) avec la session de l'op\xC3\xA9rateur\n"
          "  --version           la version\n"
          "\n"
          "  --dossier D         le dossier de l'agent (par d\xC3\xA9" "faut : " + deploy::defaultAgentDir() + ")\n"
          "  --port N            le port TCP (par d\xC3\xA9" "faut " + std::to_string(deploy::protocol::kAgentPort) + ")\n"
          "  --port-decouverte N le port UDP de la d\xC3\xA9" "couverte (par d\xC3\xA9" "faut " + std::to_string(deploy::protocol::kDiscoveryPort)
        + ")\n"
          "\n"
          "L'agent n'ex\xC3\xA9" "cute aucune commande re\xC3\xA7ue du r\xC3\xA9seau et n'utilise jamais Internet : il ne lance que le poste\n"
          "d'exploitation (IHM-Studio, pos\xC3\xA9 \xC3\xA0 c\xC3\xB4t\xC3\xA9 de lui) sur une version install\xC3\xA9" "e.");
}

bool parsePort(const std::string& text, int& out) {
    if (text.empty() || text.size() > 5) return false;
    int v = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (v < 1 || v > 65535) return false;
    out = v;
    return true;
}

int serve(const deploy::AgentConfig& cfg) {
    deploy::Agent agent(cfg);
    std::string why;
    if (!agent.start(&why)) {
        complain("XPG-Deployment-Agent : " + why);
        return 2;
    }
    say("XPG-Deployment-Agent " + cfg.version + " - " + agent.machineName() + " (" + agent.machineId() + ")");
    say("Empreinte : " + deploy::crypto::fingerprintText(agent.identity().fingerprint(), 0));
    say("Dossier : " + cfg.dir);
    say("Poste : " + agent.runtimeProgram() + " (le seul programme que l'agent lance)");
    say("\xC3\x89" "coute TCP " + std::to_string(agent.port()) + ", d\xC3\xA9" "couverte UDP " + std::to_string(agent.discoveryPort())
        + ". Ctrl+C pour arr\xC3\xAAter.");
#if defined(_WIN32)
    SetConsoleCtrlHandler(onConsole, TRUE);
#else
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#endif
    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    say("Arr\xC3\xAAt...");
    agent.stop();
    say("Arr\xC3\xAAt\xC3\xA9.");
    return 0;
}

int pair(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(P(dir), ec);
    deploy::pairing::CodeState cs;
    std::string why;
    if (!deploy::pairing::createCode(dir, cs, &why)) {
        complain("XPG-Deployment-Agent : impossible de cr\xC3\xA9" "er le code d'appairage : " + why);
        return 2;
    }
    say("Code d'appairage : " + cs.code.substr(0, 3) + " " + cs.code.substr(3));
    say("Valable jusqu'\xC3\xA0 " + localTime(cs.expiresAt, "%H:%M:%S") + " (5 minutes), une seule fois, 3 essais au plus.");
    say("Tape-le dans IHM Studio > D\xC3\xA9ploiement r\xC3\xA9seau > Appairer, sur le PC de d\xC3\xA9veloppement.");
    deploy::crypto::Bytes cert;
    if (deploy::crypto::readFile(deploy::joinPath(dir, "identite.cer"), cert, nullptr, 64 * 1024)) {
        const auto fp = deploy::crypto::sha256(cert.data(), cert.size());
        say("V\xC3\xA9rifie que le Studio affiche bien cette machine : " + deploy::machineIdOf(fp) + ", empreinte "
            + deploy::crypto::fingerprintText(fp, 4));
    } else {
        say("(l'identit\xC3\xA9 de cette machine sera cr\xC3\xA9\xC3\xA9" "e au premier lancement de --servir)");
    }
    deploy::Journal(dir).write("", "APPAIRER", "code cr\xC3\xA9\xC3\xA9, valable jusqu'\xC3\xA0 " + localTime(cs.expiresAt, "%H:%M:%S"));
    return 0;
}

// --demarrer-avec-session oui|non
int session(bool on) {
    const std::string exe = deploy::station::currentExecutable();
    std::string where, why, note;
    if (!deploy::station::setAgentAutostart(on, exe, &where, &why, &note)) {
        complain("XPG-Deployment-Agent : " + why);
        return 2;
    }
    if (!note.empty()) say(std::string(1, static_cast<char>(note[0] >= 'a' && note[0] <= 'z' ? note[0] - 'a' + 'A' : note[0])) + note.substr(1));
    else if (on) say("L'agent d\xC3\xA9marrera avec ta session (" + where + ") : " + exe + " --servir");
    else say("L'agent ne d\xC3\xA9marre plus avec ta session (" + where + " retir\xC3\xA9" "e).");
    return 0;
}

int state(const std::string& dir) {
    say("XPG-Deployment-Agent " + std::string(deploy::kAgentVersion) + " - dossier : " + dir);
    deploy::crypto::Bytes cert;
    if (deploy::crypto::readFile(deploy::joinPath(dir, "identite.cer"), cert, nullptr, 64 * 1024)) {
        const auto fp = deploy::crypto::sha256(cert.data(), cert.size());
        say("Identit\xC3\xA9 : " + deploy::machineIdOf(fp) + ", empreinte " + deploy::crypto::fingerprintText(fp, 0));
    } else {
        say("Identit\xC3\xA9 : pas encore cr\xC3\xA9\xC3\xA9" "e (premier lancement de --servir)");
    }
    core::json::Value st;
    std::string why;
    if (!deploy::readAgentState(dir, st, &why)) {
        say("\xC3\x89tat : l'agent n'a jamais tourn\xC3\xA9 avec ce dossier");
    } else {
        const std::int64_t age = deploy::protocol::nowMs() - st.integer("updatedAt");
        const std::string ports = "TCP " + std::to_string(st.integer("port")) + ", UDP " + std::to_string(st.integer("discoveryPort"));
        if (st.flag("running") && age < 15000) {
            say("\xC3\x89tat : en marche depuis le " + localTime(st.integer("startedAt"), "%d/%m/%Y %H:%M:%S") + " (pid "
                + std::to_string(st.integer("pid")) + "), " + ports + ", " + std::to_string(st.integer("connections"))
                + " connexion(s) ouverte(s)");
            say("Machine : " + st.str("machine") + " (" + st.str("id") + "), version " + st.str("agentVersion"));
        } else if (st.flag("running")) {
            say("\xC3\x89tat : ne r\xC3\xA9pond plus - dernier signe le " + localTime(st.integer("updatedAt"), "%d/%m/%Y %H:%M:%S")
                + " (l'agent s'est arr\xC3\xAAt\xC3\xA9 sans le dire)");
        } else {
            say("\xC3\x89tat : arr\xC3\xAAt\xC3\xA9 le " + localTime(st.integer("updatedAt"), "%d/%m/%Y %H:%M:%S"));
        }
    }
    deploy::pairing::StudioStore studios(dir);
    if (!studios.load(&why)) {
        say("PC appair\xC3\xA9s : " + why);
    } else {
        const auto all = studios.all();
        say("PC appair\xC3\xA9s : " + std::to_string(all.size()));
        for (const auto& s : all)
            say("  " + s.name + "  empreinte " + fpText(s.fingerprint) + "  \xC3\xA9" "diteur " + s.publisherKey.substr(0, 16) + "...  depuis le "
                + localTime(s.pairedAt, "%d/%m/%Y %H:%M"));
    }
    deploy::pairing::CodeState cs;
    switch (deploy::pairing::loadCode(dir, cs, &why)) {
        case deploy::pairing::CodeLoad::None: say("Code d'appairage : aucun"); break;
        case deploy::pairing::CodeLoad::Ok:
            say("Code d'appairage : en cours, valable jusqu'\xC3\xA0 " + localTime(cs.expiresAt, "%H:%M:%S") + " (" + std::to_string(cs.attempts)
                + " essai(s) sur 3)");
            break;
        case deploy::pairing::CodeLoad::Expired: say("Code d'appairage : expir\xC3\xA9"); break;
        case deploy::pairing::CodeLoad::Exhausted: say("Code d'appairage : \xC3\xA9puis\xC3\xA9 (3 essais manqu\xC3\xA9s)"); break;
        case deploy::pairing::CodeLoad::Error: say("Code d'appairage : illisible (" + why + ")"); break;
    }
    // 1.29.0 : le demarrage avec la session, le poste, les applications.
    const auto self = deploy::station::readAutostart(true);
    say(self.present ? "D\xC3\xA9marre avec la session : oui (" + self.where + ")" : std::string("D\xC3\xA9marre avec la session : non"));
    if (st.isObject() && st.at("station").isObject()) {
        const auto& p = st.at("station");
        say(p.flag("running") ? "Poste : " + p.str("application") + " " + p.str("version") + " (pid " + std::to_string(p.integer("pid")) + ")"
                                    + (p.flag("adopted") ? ", lanc\xC3\xA9 par la session" : "")
                              : std::string("Poste : aucun en marche"));
        if (!st.str("runtimeProgram").empty()) say("Programme du poste : " + st.str("runtimeProgram"));
    }
    const auto poste = deploy::station::readAutostart(false);
    say(poste.present ? "Le poste d\xC3\xA9marre avec la session : " + deploy::station::folderOfCommand(poste.command)
                      : std::string("Le poste ne d\xC3\xA9marre pas avec la session"));
    if (st.isObject()) {
        const auto& apps = st.at("apps").items();
        say("Applications install\xC3\xA9" "es : " + std::to_string(apps.size()));
        for (const auto& a : apps)
            say("  " + a.str("name") + "  active : " + (a.str("version").empty() ? std::string("aucune") : a.str("version")) + "  ("
                + std::to_string(a.integer("versions")) + " version(s))" + (a.flag("running") ? "  en marche" : ""));
    }
    say("Journal : " + deploy::Journal(dir).todayPath());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    core::hardenLibrarySearch();   // 1.32.0 : aucune DLL du dossier courant ni du PATH
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    const auto args = arguments(argc, argv);
    std::string command, sessionValue;
    deploy::AgentConfig cfg;
    cfg.dir = deploy::defaultAgentDir();
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--servir" || a == "--etat" || a == "--appairer" || a == "--version" || a == "--aide" || a == "-h" || a == "--help") {
            if (!command.empty() && command != a) {
                complain("XPG-Deployment-Agent : une seule commande \xC3\xA0 la fois (" + command + " et " + a + ")");
                return 1;
            }
            command = a;
        } else if (a == "--demarrer-avec-session" && hasValue) {
            if (!command.empty() && command != a) {
                complain("XPG-Deployment-Agent : une seule commande \xC3\xA0 la fois (" + command + " et " + a + ")");
                return 1;
            }
            command = a;
            sessionValue = args[++i];
            if (sessionValue != "oui" && sessionValue != "non") {
                complain("XPG-Deployment-Agent : --demarrer-avec-session attend oui ou non (" + sessionValue + ")");
                return 1;
            }
        } else if (a == "--dossier" && hasValue) {
            cfg.dir = args[++i];
        } else if (a == "--port" && hasValue) {
            if (!parsePort(args[++i], cfg.port)) {
                complain("XPG-Deployment-Agent : --port attend un nombre de 1 \xC3\xA0 65535 (" + args[i] + ")");
                return 1;
            }
        } else if (a == "--port-decouverte" && hasValue) {
            if (!parsePort(args[++i], cfg.discoveryPort)) {
                complain("XPG-Deployment-Agent : --port-decouverte attend un nombre de 1 \xC3\xA0 65535 (" + args[i] + ")");
                return 1;
            }
        } else {
            complain("XPG-Deployment-Agent : option inconnue ou incompl\xC3\xA8te : " + a + " (--aide pour la liste)");
            return 1;
        }
    }
    if (!deploy::crypto::init()) {
        complain("XPG-Deployment-Agent : le hasard du syst\xC3\xA8me est indisponible");
        return 2;
    }
    if (command == "--demarrer-avec-session") return session(sessionValue == "oui");
    if (command == "--version") {
        say("XPG-Deployment-Agent " + std::string(deploy::kAgentVersion) + " (protocoles 1 et 2, TLS 1.3 : mbedTLS " MBEDTLS_VERSION_STRING
            ", libsodium " + std::string(sodium_version_string()) + ")");
        return 0;
    }
#if defined(_WIN32)
    // 1.29.0 : lance par la session (la cle Run, l'installateur), l'agent a sa propre console : on la cache (il travaille
    // sans fenetre ; son journal dit tout). Lance d'un terminal (la console est partagee), elle reste.
    if (command == "--servir") {
        DWORD ids[4];
        if (GetConsoleProcessList(ids, 4) == 1)
            if (HWND w = GetConsoleWindow()) ShowWindow(w, SW_HIDE);
    }
#endif
    if (command == "--servir") return serve(cfg);
    if (command == "--appairer") return pair(cfg.dir);
    if (command == "--etat") return state(cfg.dir);
    usage();
    return command.empty() ? 1 : 0;
}
