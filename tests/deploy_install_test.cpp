// =============================================================================
//  tests/deploy_install_test.cpp - 1.29.0 : l'envoi, l'installation, le poste
// -----------------------------------------------------------------------------
//  De vraies sockets sur 127.0.0.1, un vrai agent dans ses fils (deploy/Agent),
//  le client du Studio (deploy/Client), de vrais paquets signes, et un faux
//  << poste >> (deploy_fake_station, a cote de cet essai) que l'agent lance a
//  la place d'IHM-Studio : il attend la demande d'arret, ou sort tout de suite,
//  ou l'ignore, selon donnees/comportement.txt du paquet.
//    - l'extraction sure (extractPackage) : le contenu, un dossier deja la, un
//      chemin dangereux, une cle non approuvee ;
//    - un PC non appaire, un Studio 1.28 (version 1 du protocole) ;
//    - l'envoi complet ; annule au milieu, l'agent redemarre (il retrouve son
//      poste), puis une coupure reseau : seul le reste part ; un bloc abime, un
//      bloc hors d'ordre ; un paquet d'une cle non appairee, un paquet altere ;
//    - l'installation (active.json, le lanceur reecrit, 3 versions gardees),
//      la meme version deux fois ; STOP propre (la demande recue par le poste,
//      son etat repris par le suivant), STOP qui depasse le delai, STOP force ;
//      START et son controle ; un poste qui sort tout de suite -> retour
//      automatique ; ROLLBACK ; LIST_APPS, GET_HISTORY, STATUS ; le disque
//      << plein >> (AgentConfig::simulatedFreeBytes : une place libre SIMULEE,
//      option d'essai de l'agent) ; demarrer avec la session (un HOME
//      temporaire sous Linux) ; un poste lance par la session, retrouve.
//
//  deploy_install_test [--poste CHEMIN]       (DEPLOY_VERBOSE=1 : chaque controle)
//  deploy_install_test --distant ADRESSE PORT CODE [DOSSIER] : l'essai croise (un
//  agent ailleurs, l'agent Windows sous Wine) - appairage, deux deploiements, un
//  poste qui ne tient pas, le retour en arriere, la liste, l'historique ; avec
//  DOSSIER (garde), le poste reste en marche, puis
//  deploy_install_test --distant-suite PORT DOSSIER [lancer] (apres un
//  redemarrage de l'agent) : le poste retrouve, arrete, la cle Run retiree ;
//  << lancer >> : lancer la version active et la laisser tourner.
// =============================================================================
#include "../src/deploy/Agent.hpp"
#include "../src/deploy/Client.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/deploy/DevKey.hpp"
#include "../src/deploy/Install.hpp"
#include "../src/deploy/Package.hpp"
#include "../src/deploy/Pairing.hpp"
#include "../src/deploy/Protocol.hpp"
#include "../src/deploy/Station.hpp"

#include "../third_party/miniz.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace deploy;
using core::json::Value;

int g_checks = 0, g_failures = 0;
const bool g_verbose = std::getenv("DEPLOY_VERBOSE") != nullptr;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_verbose) {
        std::printf("  ok     %s\n", what.c_str());
    }
    std::fflush(stdout);
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool present(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// ---- les paquets ----
const char* kLauncher = "@echo off\r\nrem Poste d'exploitation - Essai_Deploiement (IHM Studio)\r\nchcp 65001 >nul\r\n"
                        "start \"\" \"C:\\Program Files\\API IHM Studio\\IHM-Studio.exe\" --ihm \"%~dp0.\"\r\n";

// Un projet FINISH : l'IHM, donnees/comportement.txt (le faux poste), donnees/gros.bin (`big` octets au hasard).
void makeProject(const fs::path& root, std::size_t big) {
    writeFile(root / "project.xpgproj", "# API Studio project.\nformatVersion = 1\nname = Essai_Deploiement\nversion = 0.0.1\nstate = FINISH\n"
                                        "created = 2026-11-06 10:00\nmodified = 2026-11-06 10:00\nauthor = essai\ncomment = \nedition = ihm\n");
    writeFile(root / "ihm" / "ihm.txt", "# IHM Studio\nformat 33\nvue id=1 fichier=vues/0001-Accueil.vue\nfin\n");
    writeFile(root / "ihm" / "vues" / "0001-Accueil.vue", "vue id=1 nom=Accueil\nfin\n");
    std::string data(big, '\0');
    crypto::randomBytes(data.data(), data.size());
    writeFile(root / "donnees" / "gros.bin", data);
}

// Le paquet de la version `label`, dont le poste fera `mode`.
bool buildVersion(const fs::path& project, const fs::path& out, const std::string& label, const std::string& mode, const DevKey& key,
                  std::string* why = nullptr) {
    writeFile(project / "donnees" / "comportement.txt", mode + "\n");
    writeFile(project / "donnees" / "version.txt", label + "\n");
    PackageBuildOptions o;
    o.projectFolder = U(project);
    o.outputPath = U(out);
    o.launcherName = "lancer-poste.cmd";
    o.launcherText = kLauncher;
    o.appVersion = label;
    const auto r = buildPackage(o, key);
    if (!r.ok && why) *why = r.error;
    return r.ok;
}

using Entries = std::vector<std::pair<std::string, std::string>>;

Entries readZip(const fs::path& p) {
    Entries out;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, U(p).c_str(), 0)) return out;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        char name[1024];
        mz_zip_reader_get_filename(&zip, i, name, sizeof name);
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        out.emplace_back(name, data ? std::string(static_cast<const char*>(data), size) : std::string());
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return out;
}

bool writeZip(const fs::path& p, const Entries& entries) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, U(p).c_str(), 0)) return false;
    bool ok = true;
    for (const auto& [name, data] : entries) ok = ok && mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return ok;
}

// Le paquet `src` avec un fichier de plus (`path`), le manifeste resigne par `key` : un chemin dangereux signe.
bool withExtraFile(const fs::path& src, const fs::path& out, const std::string& path, const std::string& content, const DevKey& key) {
    PackageVerifyOptions vo;
    vo.inspect = true;
    const auto rep = verifyPackage(U(src), vo);
    if (!rep.manifestRead) return false;
    PackageManifest m = rep.manifest;
    m.files.push_back({path, content.size(), crypto::sha256Hex(content)});
    std::sort(m.files.begin(), m.files.end(), [](const PackageFile& a, const PackageFile& b) { return a.path < b.path; });
    m.totalSize += content.size();
    Entries e;
    for (auto& [n, d] : readZip(src))
        if (n != "manifest.json" && n != "manifest.sig") e.emplace_back(n, d);
    e.emplace_back(path, content);
    const std::string json = writeManifest(m);
    e.emplace_back("manifest.json", json);
    e.emplace_back("manifest.sig", crypto::hex(key.sign(json)));
    return writeZip(out, e);
}

AgentConfig agentConfig(const std::string& dir, const std::string& program) {
    AgentConfig c;
    c.dir = dir;
    c.bindAddress = "127.0.0.1";
    c.port = 0;
    c.discoveryPort = -1;
    c.machineName = "PC-MAINTENANCE";
    c.stateIntervalMs = 300;
    c.runtimeProgram = program;
    c.startCheckMs = 1500;
    c.stopGraceMs = 1500;
    c.keepVersions = 3;
    return c;
}

// Appairer `me` a l'agent (le code pose comme --appairer).
bool pairWith(const std::string& agentDir, int port, const TlsIdentity& me, const std::string& keyHex, PairedMachine& m, std::string* why) {
    pairing::CodeState cs;
    if (!pairing::createCode(agentDir, cs, why)) return false;
    Client c(me, "PC-DEV", keyHex);
    Outcome o = c.connect("127.0.0.1", port, 5000);
    if (o) o = c.pair(cs.code, m);
    if (!o && why) *why = o.message;
    return o.ok;
}

std::string stepsText(const DeployResult& r) {
    std::string s;
    for (const auto& st : r.steps)
        s += "\n         - " + deployStepName(st.step) + (st.skipped ? " (rien a faire)" : st.ok ? " ok" : " ECHEC") + " : " + st.message;
    return s;
}

void show(const char* what, const DeployResult& r) {
    if (!g_verbose) return;
    std::printf("         [%s] %s%s\n", what, r.outcome.message.c_str(), stepsText(r).c_str());
}

const InstalledApp* findApp(const AppList& l, const std::string& name) {
    for (const auto& a : l.apps)
        if (a.name == name) return &a;
    return nullptr;
}

std::vector<std::string> ids(const InstalledApp* a) {
    std::vector<std::string> out;
    if (a)
        for (const auto& v : a->versions) out.push_back(v.id);
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ",") + x;
    return s;
}

int countHistory(const std::vector<HistoryEntry>& h, const std::string& op, const std::string& result) {
    int n = 0;
    for (const auto& e : h) n += e.op == op && e.result == result ? 1 : 0;
    return n;
}

// Tous les blocs d'un paquet, a la main (prepare, puis sendChunk).
Outcome sendAll(Client& c, const fs::path& pkg, std::string& transfer) {
    PackageVerifyOptions vo;
    vo.inspect = true;
    const auto rep = verifyPackage(U(pkg), vo);
    std::string sha;
    std::uint64_t size = 0;
    crypto::sha256File(U(pkg), sha, &size, nullptr);
    Client::PrepareReply pr;
    Outcome o = c.prepare(rep.manifest.application, rep.manifest.appVersion, size, sha, rep.manifest.totalSize, pr);
    if (!o) return o;
    transfer = pr.transfer;
    const std::string bytes = readFile(pkg);
    for (std::uint64_t off = pr.offset; off < size;) {
        const auto n = std::min<std::uint64_t>(protocol::kChunkSize, size - off);
        std::uint64_t got = 0;
        o = c.sendChunk(pr.transfer, off, std::string_view(bytes).substr(off, n), got);
        if (!o) return o;
        off = got;
    }
    return o;
}

// ==================================================================== l'essai ===
void run(const fs::path& base, const std::string& program) {
    std::string why;
    // Le demarrage avec la session : dans un HOME temporaire.
    const fs::path home = base / "home";
    fs::create_directories(home);
#if !defined(_WIN32)
    setenv("HOME", U(home).c_str(), 1);
    setenv("XDG_CONFIG_HOME", U(home / ".config").c_str(), 1);
#endif
    auto key = DevKey::loadOrCreate(U(base / "cle"), &why, "PC-DEV");
    auto other = DevKey::loadOrCreate(U(base / "autre-cle"), &why, "PC-INCONNU");
    check(key && other, "deux cl\xC3\xA9s d'\xC3\xA9" "diteur [" + why + "]");
    if (!key || !other) return;
    TlsIdentity me, stranger;
    check(TlsIdentity::loadOrCreate(U(base / "studio"), "studio", false, "XPG-Studio PC-DEV", me, &why), "identit\xC3\xA9 du Studio");
    check(TlsIdentity::loadOrCreate(U(base / "inconnu"), "studio", false, "XPG-Studio PC-INCONNU", stranger, &why), "identit\xC3\xA9 d'un autre PC");

    // ---- les paquets : 3,5 Mio au hasard (4 blocs) ----
    const fs::path project = base / "projet" / "Essai_Deploiement";
    makeProject(project, 3 * 1024 * 1024 + 512 * 1024);
    const fs::path pk = base / "paquets";
    fs::create_directories(pk);
    const auto pkg = [&](const std::string& label) { return pk / ("Essai_Deploiement-" + label + ".xpgdeploy"); };
    bool built = buildVersion(project, pkg("V1"), "V1", "attendre", *key, &why) && buildVersion(project, pkg("V2"), "V2", "attendre", *key, &why)
                 && buildVersion(project, pkg("V3"), "V3", "attendre", *key, &why) && buildVersion(project, pkg("V4"), "V4", "sortir", *key, &why)
                 && buildVersion(project, pkg("V5"), "V5", "ignorer", *key, &why) && buildVersion(project, pkg("V6"), "V6", "attendre", *key, &why)
                 && buildVersion(project, pkg("X"), "X", "attendre", *other, &why);
    check(built, "les paquets V1 \xC3\xA0 V6 sont construits [" + why + "]");
    if (!built) return;
    const auto pkgSize = fs::file_size(pkg("V2"));
    check(pkgSize > 3 * protocol::kChunkSize, "un paquet de plus de 3 blocs (" + std::to_string(pkgSize) + " octets)");

    // =================================================== l'extraction sure ===
    {
        PackageExtractOptions xo;
        xo.approvedKeys = {key->publicKey()};
        const fs::path dest = base / "extrait" / "V1";
        auto x = extractPackage(U(pkg("V1")), U(dest), xo);
        check(x.ok && x.files == 7 && present(dest / "project.xpgproj") && present(dest / "donnees" / "gros.bin") && present(dest / "lancer-poste.cmd"),
              "extractPackage : le contenu de app/ dans le dossier (" + std::to_string(x.files) + " fichiers) [" + x.error + "]");
        check(readFile(dest / "donnees" / "gros.bin") == readFile(project / "donnees" / "gros.bin"), "extractPackage : les octets sont les m\xC3\xAAmes");
        check(!present(fs::path(U(dest) + ".extraction")), "extractPackage : pas de dossier temporaire laiss\xC3\xA9");
        x = extractPackage(U(pkg("V1")), U(dest), xo);
        check(!x.ok && has(x.error, "existe d\xC3\xA9j\xC3\xA0"), "extractPackage : un dossier d\xC3\xA9j\xC3\xA0 l\xC3\xA0 n'est pas \xC3\xA9" "cras\xC3\xA9 [" + x.error + "]");
        for (const std::string bad : {"app/../evasion.txt", "app/donnees/../../../evasion.txt", "app/C:/evasion.txt", "app/NUL.txt"}) {
            const fs::path piege = pk / "piege.xpgdeploy";
            const bool made = withExtraFile(pkg("V1"), piege, bad, "dehors", *key);
            const fs::path d2 = base / "extrait" / "piege";
            x = extractPackage(U(piege), U(d2), xo);
            check(made && !x.ok && x.report.failedStep == 3 && !present(d2) && !present(base / "evasion.txt") && !present(base / "extrait" / "evasion.txt"),
                  "extractPackage : chemin dangereux " + bad + " refus\xC3\xA9, rien d'\xC3\xA9" "crit [" + x.error + "]");
        }
        x = extractPackage(U(pkg("X")), U(base / "extrait" / "X"), xo);
        check(!x.ok && has(x.error, "approuv") && !present(base / "extrait" / "X"), "extractPackage : une cl\xC3\xA9 non approuv\xC3\xA9" "e est refus\xC3\xA9" "e [" + x.error + "]");
    }

    // ============================================================ l'agent ===
    const std::string agentDir = U(base / "agent");
    auto agent = std::make_unique<Agent>(agentConfig(agentDir, program));
    check(agent->start(&why), "l'agent d\xC3\xA9marre [" + why + "]");
    PairedMachine m;
    check(pairWith(agentDir, agent->port(), me, key->publicKeyHex(), m, &why), "le Studio est appair\xC3\xA9 [" + why + "]");
    Client c(me, "PC-DEV", key->publicKeyHex());

    // ---- un PC non appaire ; un Studio 1.28 (version 1) ----
    {
        Client k(stranger, "PC-INCONNU", other->publicKeyHex());
        AgentInfo info;
        Outcome o = k.connect("127.0.0.1", agent->port(), 5000);
        if (o) o = k.identify(info);
        check(o.ok && info.proto == 2, "IDENTIFY : la version 2 du protocole est choisie [" + o.message + "]");
        AppList l;
        o = k.listApps(l);
        check(!o.ok && o.error == "non-appaire", "un PC non appair\xC3\xA9 : LIST_APPS refus\xC3\xA9 [" + o.message + "]");
        Client::PrepareReply pr;
        o = k.prepare("Essai_Deploiement", "V1", 1000, std::string(64, 'a'), 10, pr);
        check(!o.ok && o.error == "non-appaire", "un PC non appair\xC3\xA9 : PREPARE refus\xC3\xA9");
        PairedMachine fake = m;
        const DeployResult r = k.deploy(fake, U(pkg("V1")));
        check(!r.outcome.ok && r.outcome.error == "non-appaire" && r.version.empty(), "un PC non appair\xC3\xA9 : deploy() refus\xC3\xA9 [" + r.outcome.message + "]");

        Client old(me, "PC-DEV", key->publicKeyHex());
        old.setOfferedVersions({1});
        o = old.connect(m, 5000);
        if (o) o = old.identify(info);
        check(o.ok && info.proto == 1, "un Studio 1.28 (version 1) : accept\xC3\xA9");
        o = old.prepare("Essai_Deploiement", "V1", 1000, std::string(64, 'a'), 10, pr);
        check(!o.ok && o.error == "proto" && has(o.message, "1.29.0") && has(o.message, "mets \xC3\xA0 jour IHM Studio"),
              "version 1 : PREPARE refus\xC3\xA9 avec une phrase claire [" + o.message + "]");
        const DeployResult r1 = old.deploy(m, U(pkg("V1")));
        check(!r1.outcome.ok && r1.outcome.error == "incompatible" && has(r1.outcome.message, "ne sait pas encore installer"),
              "un agent qui ne parle que la version 1 : deploy() le dit [" + r1.outcome.message + "]");
    }

    // ---- V1 : l'envoi complet, l'installation, le lancement ----
    std::vector<std::pair<DeployStep, std::uint64_t>> seen;
    DeployOptions opt;
    opt.stopDelaySeconds = 2;
    DeployResult r = c.deploy(m, U(pkg("V1")), opt, [&](DeployStep s, std::uint64_t b, std::uint64_t) {
        seen.emplace_back(s, b);
        return true;
    });
    show("V1", r);
    check(r.outcome.ok && r.version == "V1" && r.application == "Essai_Deploiement" && r.started && r.pid > 0 && r.previousVersion.empty()
              && r.bytesSent == fs::file_size(pkg("V1")) && r.packageBytes == r.bytesSent && r.resumedFrom == 0 && !r.alreadyInstalled,
          "V1 d\xC3\xA9ploy\xC3\xA9" "e : envoy\xC3\xA9" "e enti\xC3\xA8re, install\xC3\xA9" "e, lanc\xC3\xA9" "e [" + r.outcome.message + "]" + stepsText(r));
    check(r.steps.size() == 8 && r.steps[3].step == DeployStep::Send && r.steps[6].step == DeployStep::Stop && r.steps[6].skipped,
          "V1 : les 8 \xC3\xA9tapes (aucun poste \xC3\xA0 arr\xC3\xAAter)");
    check(std::count_if(seen.begin(), seen.end(), [](const auto& p) { return p.first == DeployStep::Send; }) >= 4
              && seen.back().first == DeployStep::Done,
          "V1 : la progression suit l'envoi bloc par bloc, puis Termin\xC3\xA9");
    const fs::path apps = fs::path(agentDir) / "Applications" / "Essai_Deploiement";
    {
        Value act;
        check(install::readJson(U(apps / "active.json"), act, &why) && act.str("active") == "V1" && act.str("format") == "xpg-application-active",
              "active.json : V1 active");
        const std::string launcher = readFile(apps / "versions" / "V1" / "lancer-poste.cmd");
        check(has(launcher, program) && has(launcher, "--ihm \"%~dp0.\"") && !has(launcher, "Program Files"),
              "le lanceur est r\xC3\xA9\xC3\xA9" "crit pour le programme de ce PC");
        const std::string trace = readFile(apps / "versions" / "V1" / "poste-trace.txt");
        check(has(trace, "pid=" + std::to_string(r.pid)) && has(trace, "arret=" + U(fs::path(agentDir) / "arret.json"))
                  && has(trace, "reprise=" + U(fs::path(agentDir) / "reprise-poste.txt")),
              "le poste est lanc\xC3\xA9 avec --ihm, XPG_ARRET_FICHIER et XPG_REPRISE_FICHIER [" + trace + "]");
    }
    AppList list;
    Outcome o = c.listApps(m, list);
    const InstalledApp* a = findApp(list, "Essai_Deploiement");
    check(o.ok && a && a->version == "V1" && a->running && a->pid == r.pid && list.station.running && list.station.version == "V1" && !list.station.adopted
              && list.program == program,
          "LIST_APPS : V1 active, le poste tourne (pid " + std::to_string(r.pid) + ") [" + o.message + "]");

    // ---- V2 : annule au milieu ; l'agent redemarre ; une coupure ; seul le reste part ----
    r = c.deploy(m, U(pkg("V2")), opt, [&](DeployStep s, std::uint64_t b, std::uint64_t) { return !(s == DeployStep::Send && b >= protocol::kChunkSize); });
    show("V2 annule", r);
    check(!r.outcome.ok && r.cancelled && r.outcome.error == "annule" && r.bytesSent == protocol::kChunkSize && has(r.outcome.message, "reprendra"),
          "V2 annul\xC3\xA9 apr\xC3\xA8s un bloc : rien d'install\xC3\xA9, l'envoi reprendra [" + r.outcome.message + "]");
    const int pidV1 = list.station.pid;
    agent->stop();
    agent.reset();
    check(station::processCreated(pidV1) != 0, "l'agent arr\xC3\xAAt\xC3\xA9 : le poste V1 tourne toujours");
    agent = std::make_unique<Agent>(agentConfig(agentDir, program));
    check(agent->start(&why), "l'agent red\xC3\xA9marre [" + why + "]");
    m.port = agent->port();
    o = c.listApps(m, list);
    check(o.ok && list.station.running && list.station.pid == pidV1 && list.station.version == "V1" && !list.station.adopted,
          "l'agent red\xC3\xA9marr\xC3\xA9 retrouve le poste qu'il avait lanc\xC3\xA9 (pid " + std::to_string(list.station.pid) + ")");
    bool cutDone = false;
    r = c.deploy(m, U(pkg("V2")), opt, [&](DeployStep s, std::uint64_t b, std::uint64_t) {
        if (s == DeployStep::Send && b >= 2 * protocol::kChunkSize && !cutDone) {
            cutDone = true;
            c.stream().close();   // la coupure : la connexion tombe au milieu de l'envoi
        }
        return true;
    });
    show("V2", r);
    check(r.outcome.ok && r.version == "V2" && r.previousVersion == "V1" && r.resumedFrom == protocol::kChunkSize && r.reconnects == 1
              && r.bytesSent + r.resumedFrom == pkgSize,
          "V2 : reprise apr\xC3\xA8s l'annulation et le red\xC3\xA9marrage (1 Mio d\xC3\xA9j\xC3\xA0 l\xC3\xA0), puis apr\xC3\xA8s une coupure : seul le reste part ("
              + std::to_string(r.bytesSent) + " octets) [" + r.outcome.message + "]" + stepsText(r));
    check(r.stationWasRunning && !r.stationForced && r.started && r.pid != pidV1, "V2 : le poste V1 a quitt\xC3\xA9 proprement, V2 est lanc\xC3\xA9" "e");
    {
        const std::string req = readFile(apps / "versions" / "V1" / "arret-recu.txt");
        Value v;
        check(core::json::parse(req, v) && v.isObject() && v.integer("delai") == 2 && has(v.str("raison"), "Mise \xC3\xA0 jour vers Essai_Deploiement V2")
                  && v.members().size() == 2,
              "le poste a re\xC3\xA7u la demande d'arr\xC3\xAAt {\"raison\", \"delai\"} [" + req + "]");
        check(has(readFile(apps / "versions" / "V2" / "poste-trace.txt"), "repris=etat de V1"),
              "V2 reprend l'\xC3\xA9tat \xC3\xA9" "crit par V1 \xC3\xA0 l'arr\xC3\xAAt (XPG_REPRISE_FICHIER n'est pas effac\xC3\xA9 entre les deux)");
        check(!present(fs::path(agentDir) / "arret.json"), "la demande d'arr\xC3\xAAt est effac\xC3\xA9" "e quand le poste est sorti");
        check(!present(fs::path(agentDir) / "incoming" / (crypto::sha256Hex(readFile(pkg("V2"))) + ".part")), "le paquet re\xC3\xA7u est effac\xC3\xA9 apr\xC3\xA8s l'installation");
    }

    // ---- la meme version deux fois ----
    r = c.deploy(m, U(pkg("V2")), opt);
    show("V2 bis", r);
    check(r.outcome.ok && r.alreadyInstalled && r.bytesSent == 0 && r.steps.size() >= 4 && r.steps[3].skipped && has(r.outcome.message, "rien \xC3\xA0 refaire"),
          "la m\xC3\xAAme version deux fois : rien n'est envoy\xC3\xA9 ni refait [" + r.outcome.message + "]");

    // ---- un bloc abime, un bloc hors d'ordre, un paquet d'une cle non appairee, un paquet altere ----
    {
        Client k(me, "PC-DEV", key->publicKeyHex());
        o = k.connect(m, 5000);
        AgentInfo info;
        if (o) o = k.identify(info);
        const std::string bytes = readFile(pkg("V3"));
        const std::string sha = crypto::sha256Hex(bytes);
        Client::PrepareReply pr;
        PackageVerifyOptions vo;
        vo.inspect = true;
        const auto rep = verifyPackage(U(pkg("V3")), vo);
        o = k.prepare("Essai_Deploiement", "V3", bytes.size(), sha, rep.manifest.totalSize, pr);
        check(o.ok && pr.offset == 0 && !pr.installed && pr.chunkSize == protocol::kChunkSize, "PREPARE V3 : depuis 0, blocs de 1 Mio [" + o.message + "]");
        std::uint64_t got = 0;
        o = k.sendChunk(pr.transfer, 0, std::string_view(bytes).substr(0, protocol::kChunkSize), got, std::string(64, '0'));
        check(!o.ok && o.error == "bloc-abime" && has(o.message, "renvoie"), "un bloc ab\xC3\xAEm\xC3\xA9 (SHA-256 faux) est refus\xC3\xA9 [" + o.message + "]");
        o = k.prepare("Essai_Deploiement", "V3", bytes.size(), sha, rep.manifest.totalSize, pr);
        check(o.ok && pr.offset == 0, "le bloc ab\xC3\xAEm\xC3\xA9 n'a pas \xC3\xA9t\xC3\xA9 \xC3\xA9" "crit (reprise \xC3\xA0 0)");
        o = k.sendChunk(pr.transfer, 2 * protocol::kChunkSize, std::string_view(bytes).substr(0, 100), got);
        check(!o.ok && o.error == "transfert" && has(o.message, "hors d'ordre"), "un bloc hors d'ordre est refus\xC3\xA9 [" + o.message + "]");
        Value v;
        o = k.verifyPackage(pr.transfer, v);
        check(!o.ok && o.error == "transfert" && has(o.message, "pas complet"), "VERIFY_PACKAGE d'un paquet incomplet : refus\xC3\xA9 [" + o.message + "]");
        // Un paquet altere apres signature (le Studio, lui, le refuserait avant d'envoyer).
        auto e = readZip(pkg("V3"));
        for (auto& [n, d] : e)
            if (n == "app/donnees/version.txt") d = "V3 modifiee\n";
        const fs::path altered = pk / "altere.xpgdeploy";
        writeZip(altered, e);
        std::string transfer;
        o = sendAll(k, altered, transfer);
        Value vr;
        if (o) o = k.verifyPackage(transfer, vr);
        check(!o.ok && o.error == "paquet-refuse" && has(o.message, "version.txt"), "un paquet alt\xC3\xA9r\xC3\xA9 est refus\xC3\xA9 par l'agent [" + o.message + "]");
        Value iv;
        o = k.install(transfer, std::nullopt, iv);
        check(!o.ok && o.error == "transfert", "un paquet refus\xC3\xA9 ne s'installe pas [" + o.message + "]");
        const DeployResult ra = c.deploy(m, U(altered), opt);
        check(!ra.outcome.ok && ra.outcome.error == "paquet-local" && ra.steps.size() == 1, "deploy() refuse un paquet alt\xC3\xA9r\xC3\xA9 avant de l'envoyer [" + ra.outcome.message + "]");
    }
    r = c.deploy(m, U(pkg("X")), opt);
    show("X", r);
    check(!r.outcome.ok && r.outcome.error == "paquet-refuse" && has(r.outcome.message, "approuv") && r.version.empty(),
          "un paquet sign\xC3\xA9 par une cl\xC3\xA9 non appair\xC3\xA9" "e est refus\xC3\xA9 (VERIFY_PACKAGE) [" + r.outcome.message + "]");

    // ---- V3 ; V4 qui ne tient pas : retour automatique a V3 ----
    r = c.deploy(m, U(pkg("V3")), opt);
    show("V3", r);
    check(r.outcome.ok && r.version == "V3" && r.previousVersion == "V2" && r.stationWasRunning && r.started, "V3 d\xC3\xA9ploy\xC3\xA9" "e [" + r.outcome.message + "]");
    r = c.deploy(m, U(pkg("V4")), opt);
    show("V4", r);
    check(!r.outcome.ok && r.outcome.error == "demarrage" && r.rolledBack && r.rolledBackTo == "V3" && r.started && r.pid > 0
              && has(r.outcome.message, "retour automatique \xC3\xA0 V3") && has(r.outcome.message, "code 3"),
          "V4 sort tout de suite : retour automatique \xC3\xA0 V3, relanc\xC3\xA9" "e [" + r.outcome.message + "]");
    check(has(readFile(apps / "versions" / "V3" / "poste-trace.txt"), "repris=etat de V3"),
          "le retour automatique remet \xC3\xA0 V3 l'\xC3\xA9tat qu'elle avait \xC3\xA9" "crit (V4 l'avait pris en tombant)");
    check(r.removedVersions == std::vector<std::string>{"V1"}, "V4 install\xC3\xA9" "e : V1 effac\xC3\xA9" "e (3 versions gard\xC3\xA9" "es) [" + join(r.removedVersions) + "]");
    o = c.listApps(m, list);
    a = findApp(list, "Essai_Deploiement");
    check(o.ok && a && a->version == "V3" && a->previous == "V4" && a->running && a->runningVersion == "V3" && join(ids(a)) == "V2,V3,V4"
              && !present(apps / "versions" / "V1") && !present(apps / "versions" / "V1.json"),
          "apr\xC3\xA8s le retour : V3 active et en marche ; V2, V3, V4 install\xC3\xA9" "es [" + (a ? join(ids(a)) : std::string()) + "]");

    // ---- STOP propre, START et son controle ----
    {
        Client k(me, "PC-DEV", key->publicKeyHex());
        o = k.connect(m, 5000);
        Value args = Value::object(), out;
        args.set("delai", 1);
        args.set("raison", "Essai : arr\xC3\xAAt propre");
        if (o) o = k.stopStation(args, out);
        check(o.ok && out.flag("wasRunning") && out.flag("stopped") && !out.flag("forced") && out.str("version") == "V3",
              "STOP : le poste V3 quitte proprement [" + o.message + "]");
        check(has(readFile(apps / "versions" / "V3" / "arret-recu.txt"), "Essai : arr\xC3\xAAt propre"), "STOP : la raison donn\xC3\xA9" "e arrive au poste");
        o = k.stopStation(args, out);
        check(o.ok && !out.flag("wasRunning"), "STOP sans poste en marche : rien \xC3\xA0 faire");
        Value sa = Value::object();
        sa.set("check", 1);
        o = k.startStation(sa, out);
        check(o.ok && out.flag("running") && out.str("version") == "V3" && out.integer("pid") > 0 && out.integer("checkMs") == 1000,
              "START : V3 lanc\xC3\xA9" "e, tient 1 s [" + o.message + "]");
        o = k.startStation(sa, out);
        check(o.ok && out.flag("alreadyRunning"), "START d'une version d\xC3\xA9j\xC3\xA0 en marche : rien \xC3\xA0 refaire");
    }

    // ---- V5 ignore la demande d'arret : STOP depasse le delai, puis STOP force ----
    r = c.deploy(m, U(pkg("V5")), opt);
    show("V5", r);
    check(r.outcome.ok && r.version == "V5" && r.started && r.removedVersions == std::vector<std::string>{"V2"},
          "V5 d\xC3\xA9ploy\xC3\xA9" "e (V2 effac\xC3\xA9" "e) [" + r.outcome.message + "]");
    {
        Client k(me, "PC-DEV", key->publicKeyHex());
        o = k.connect(m, 5000);
        Value args = Value::object(), out;
        args.set("delai", 1);
        const auto t0 = std::chrono::steady_clock::now();
        if (o) o = k.stopStation(args, out);
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        check(!o.ok && o.error == "arret-delai" && has(o.message, "forcer") && has(o.message, "tourne toujours") && !out.flag("stopped") && waited >= 2400,
              "STOP qui d\xC3\xA9passe le d\xC3\xA9lai (1 s + 1,5 s) : \xC3\xA9" "chec dit, le poste n'est pas tu\xC3\xA9 [" + o.message + "]");
        AppList l2;
        check(k.listApps(l2).ok && l2.station.running && l2.station.version == "V5", "le poste V5 tourne toujours");
        args.set("force", true);
        o = k.stopStation(args, out);
        check(o.ok && out.flag("forced") && out.flag("stopped"), "STOP force=true : le poste est termin\xC3\xA9 [" + o.message + "]");
        check(k.listApps(l2).ok && !l2.station.running, "plus aucun poste en marche");
        std::vector<std::string> lines;
        o = k.logs(200, lines);
        check(o.ok && std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return has(l, "termin\xC3\xA9 de force"); })
                  && std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return has(l, "ROLLBACK automatique"); }),
              "le journal dit l'arr\xC3\xAAt forc\xC3\xA9 et le retour automatique");
    }

    // ---- ROLLBACK ----
    RollbackResult rb;
    o = c.rollback(m, rb);
    check(o.ok && rb.from == "V5" && rb.to == "V4" && !rb.wasRunning && !rb.restarted && has(o.message, "V4"),
          "ROLLBACK (aucun poste en marche) : V5 -> V4, rien \xC3\xA0 relancer [" + o.message + "]");
    RollbackOptions ro;
    ro.version = "V3";
    o = c.rollback(m, rb, ro);
    check(o.ok && rb.from == "V4" && rb.to == "V3" && !rb.restarted, "ROLLBACK vers une version donn\xC3\xA9" "e (V3) [" + o.message + "]");
    {
        Client k(me, "PC-DEV", key->publicKeyHex());
        o = k.connect(m, 5000);
        Value sa = Value::object(), out;
        if (o) o = k.startStation(sa, out);
        check(o.ok && out.str("version") == "V3", "START : V3 en marche");
    }
    ro.version = "V5";   // V5 ignore l'arret : le retour doit forcer
    ro.stopDelaySeconds = 1;
    o = c.rollback(m, rb, ro);
    check(o.ok && rb.wasRunning && rb.restarted && rb.running && rb.to == "V5" && rb.pid > 0, "ROLLBACK avec un poste en marche : arr\xC3\xAAt\xC3\xA9, puis relanc\xC3\xA9 [" + o.message + "]");
    ro.version.clear();
    ro.force = false;
    o = c.rollback(m, rb, ro);
    check(!o.ok && o.error == "arret-delai" && has(o.message, "retour en arri\xC3\xA8re annul\xC3\xA9"), "ROLLBACK : le poste ne quitte pas -> annul\xC3\xA9, dit [" + o.message + "]");
    ro.force = true;
    o = c.rollback(m, rb, ro);
    check(!o.ok && o.error == "demarrage" && rb.from == "V5" && rb.to == "V4" && rb.restarted && !rb.running && has(o.message, "s'est arr\xC3\xAAt\xC3\xA9"),
          "ROLLBACK forc\xC3\xA9 vers V4, qui ne tient pas : dit [" + o.message + "]");
    ro = RollbackOptions{};
    ro.application = "Inconnue";
    o = c.rollback(m, rb, ro);
    check(!o.ok && o.error == "application-inconnue", "ROLLBACK d'une application inconnue : refus\xC3\xA9 [" + o.message + "]");

    // ---- l'historique, STATUS ----
    std::vector<HistoryEntry> h;
    o = c.history(m, 500, h);
    check(o.ok && countHistory(h, "INSTALL", "installee") == 5 && countHistory(h, "INSTALL", "deja-installee") == 1 && countHistory(h, "START", "retour") == 1
              && countHistory(h, "ROLLBACK", "automatique") == 1 && countHistory(h, "STOP", "delai") >= 1 && countHistory(h, "STOP", "force") >= 1
              && countHistory(h, "VERIFY_PACKAGE", "refuse") == 2 && !h.empty() && h.front().from == "PC-DEV" && h.front().at > 0,
          "GET_HISTORY : les installations, les arr\xC3\xAAts, le retour automatique, les refus (" + std::to_string(h.size()) + " entr\xC3\xA9" "es)");
    {
        Client k(me, "PC-DEV", key->publicKeyHex());
        AgentStatus st;
        o = k.connect(m, 5000);
        if (o) o = k.status(st);
        check(o.ok && st.apps.size() == 1 && st.apps[0].name == "Essai_Deploiement" && st.apps[0].version == "V4" && st.apps[0].versionCount == 3,
              "STATUS : les applications install\xC3\xA9" "es [" + o.message + "]");
    }

    // ---- demarrer avec la session ----
#if !defined(_WIN32)
    {
        opt.autostart = true;
        r = c.deploy(m, U(pkg("V6")), opt);
        show("V6", r);
        const fs::path desk = home / ".config" / "autostart" / "xpg-poste.desktop";
        const std::string text = readFile(desk);
        check(r.outcome.ok && has(text, "Name=XPG Poste d'exploitation") && has(text, U(apps / "versions" / "V6")) && has(text, program),
              "autostart : le .desktop du poste lance V6 [" + r.outcome.message + "]");
        const auto e = station::readAutostart(false);
        check(e.present && station::folderOfCommand(e.command) == U(apps / "versions" / "V6"), "autostart : relu, le dossier de V6");
        ro = RollbackOptions{};
        ro.force = true;
        ro.stopDelaySeconds = 0;
        o = c.rollback(m, rb, ro);
        check(o.ok && rb.to == "V5" && station::folderOfCommand(station::readAutostart(false).command) == U(apps / "versions" / "V5"),
              "autostart : r\xC3\xA9\xC3\xA9" "crit \xC3\xA0 la bascule (ROLLBACK vers V5) [" + o.message + "]");
        opt.autostart = false;
        opt.force = true;
        r = c.deploy(m, U(pkg("V6")), opt);
        check(r.outcome.ok && !present(desk), "autostart retir\xC3\xA9 (d\xC3\xA9ployer avec autostart = faux) [" + r.outcome.message + "]");
        std::string where, note;
        check(station::setAgentAutostart(true, "/opt/xpg/XPG-Deployment-Agent", &where, &why, &note)
                  && has(readFile(home / ".config" / "autostart" / "xpg-deployment-agent.desktop"), "Exec=\"/opt/xpg/XPG-Deployment-Agent\" --servir")
                  && station::readAutostart(true).present,
              "l'agent d\xC3\xA9marre avec la session (xpg-deployment-agent.desktop) [" + why + "]");
        check(station::setAgentAutostart(false, {}, &where, &why) && !station::readAutostart(true).present, "l'agent ne d\xC3\xA9marre plus avec la session");
    }
#endif

    // ---- un poste lance par la session (pas par l'agent) : retrouve ----
    {
        Client k(me, "PC-DEV", key->publicKeyHex());
        o = k.connect(m, 5000);
        Value args = Value::object(), out;
        args.set("delai", 0);
        args.set("force", true);
        if (o) o = k.stopStation(args, out);
        check(o.ok, "STOP (force) avant l'essai de la session");
        agent->stop();
        agent.reset();
        std::error_code ec;
        fs::remove(fs::path(agentDir) / "poste.json", ec);
        station::Process p;
        // Une version qui attend la demande d'arret (V6 apres l'essai de la session, sinon V3).
        const std::string label = present(apps / "versions" / "V6") ? "V6" : "V3";
        const std::string folder = U(apps / "versions" / label);
        check(p.launch(program, {"--ihm", folder}, {{"XPG_ARRET_FICHIER", U(fs::path(agentDir) / "arret.json")}}, folder, {}, &why),
              "un poste lanc\xC3\xA9 comme par la session [" + why + "]");
        agent = std::make_unique<Agent>(agentConfig(agentDir, program));
        check(agent->start(&why), "l'agent red\xC3\xA9marre [" + why + "]");
        m.port = agent->port();
        o = c.listApps(m, list);
        check(o.ok && list.station.running && list.station.adopted && list.station.pid == p.pid() && list.station.version == label,
              "l'agent retrouve le poste lanc\xC3\xA9 par la session (pid " + std::to_string(list.station.pid) + ")");
        o = k.connect(m, 5000);
        args.set("delai", 1);
        args.set("force", false);
        if (o) o = k.stopStation(args, out);
        check(o.ok && out.flag("adopted") && !out.flag("forced"), "STOP du poste retrouv\xC3\xA9 : il quitte (arret.json du dossier de l'agent) [" + o.message + "]");
    }
    agent->stop();

    // ---- le disque << plein >> (place libre SIMULEE par l'option d'essai de l'agent) ----
    {
        const std::string dir2 = U(base / "agent-plein");
        AgentConfig cfg = agentConfig(dir2, program);
        cfg.simulatedFreeBytes = 10 * 1024 * 1024;
        Agent full(cfg);
        check(full.start(&why), "un agent dont le disque est presque plein (simul\xC3\xA9 : 10 Mio libres)");
        PairedMachine m2;
        check(pairWith(dir2, full.port(), me, key->publicKeyHex(), m2, &why), "appair\xC3\xA9 [" + why + "]");
        r = c.deploy(m2, U(pkg("V1")), opt);
        check(!r.outcome.ok && r.outcome.error == "place" && has(r.outcome.message, "pas assez de place") && r.bytesSent == 0,
              "le disque plein : PREPARE refuse, rien n'est envoy\xC3\xA9 [" + r.outcome.message + "]");
        full.stop();
    }
}

// ---- l'essai croise : un agent ailleurs (l'agent Windows sous Wine) ----
// `keep` : un dossier ou garder la cle, l'identite et la machine (pour --distant-suite) ; le poste
// reste alors en marche a la fin.
int remote(const std::string& host, int port, const std::string& code, const std::string& keep) {
    std::error_code ec;
    unsigned char rnd[6];
    crypto::randomBytes(rnd, sizeof rnd);
    const fs::path base = keep.empty() ? fs::temp_directory_path(ec) / ("xpg-deployinstall-distant-" + crypto::hex(rnd, sizeof rnd)) : fs::path(keep);
    fs::create_directories(base, ec);
    std::string why;
    auto key = DevKey::loadOrCreate(U(base / "cle"), &why, "PC-LINUX");
    TlsIdentity me;
    if (!key || !TlsIdentity::loadOrCreate(U(base / "studio"), "studio", false, "XPG-Studio PC-LINUX", me, &why)) {
        std::printf("cle ou identite : %s\n", why.c_str());
        return 1;
    }
    const fs::path project = base / "projet" / "Essai_Deploiement";
    makeProject(project, 2 * 1024 * 1024 + 300 * 1024);
    const auto pkg = [&](const std::string& l) { return base / ("Essai_Deploiement-" + l + ".xpgdeploy"); };
    if (!buildVersion(project, pkg("V1"), "V1", "attendre", *key, &why) || !buildVersion(project, pkg("V2"), "V2", "attendre", *key, &why)
        || !buildVersion(project, pkg("V3"), "V3", "sortir", *key, &why)) {
        std::printf("paquets : %s\n", why.c_str());
        return 1;
    }
    Client c(me, "PC-LINUX", key->publicKeyHex());
    PairedMachine m;
    Outcome o = c.connect(host, port, 10000);
    if (o) o = c.pair(code, m);
    std::printf("appairage : %s %s\n", o.ok ? "ok" : "ECHEC", o.message.c_str());
    if (!o) return 1;
    MachineStore store(U(base));
    (void)store.upsert(m, &why);
    int failures = 0;
    const auto deployOne = [&](const std::string& label, bool expectOk, bool autostart) {
        DeployOptions opt;
        opt.stopDelaySeconds = 5;
        opt.startCheckSeconds = 3;
        opt.autostart = autostart;
        const DeployResult r = c.deploy(m, U(pkg(label)), opt);
        std::printf("deploiement %s : %s [%s] %s\n", label.c_str(), r.outcome.ok ? "ok" : "ECHEC", r.outcome.error.c_str(), r.outcome.message.c_str());
        for (const auto& s : r.steps)
            std::printf("   - %s%s : %s (%lld ms)\n", deployStepName(s.step).c_str(), s.skipped ? " (rien a faire)" : s.ok ? " ok" : " ECHEC",
                        s.message.c_str(), static_cast<long long>(s.ms));
        std::printf("   envoye %llu octets sur %llu, pid %d, retour automatique : %s %s\n", static_cast<unsigned long long>(r.bytesSent),
                    static_cast<unsigned long long>(r.packageBytes), r.pid, r.rolledBack ? "oui ->" : "non", r.rolledBackTo.c_str());
        if (r.outcome.ok != expectOk) ++failures;
        return r;
    };
    const auto listing = [&] {
        AppList l;
        o = c.listApps(m, l);
        std::printf("LIST_APPS : %s %s - programme du poste %s\n", o.ok ? "ok" : "ECHEC", o.message.c_str(), l.program.c_str());
        for (const auto& a : l.apps) {
            std::printf("   %s active %s (avant %s) %s pid %d, versions :", a.name.c_str(), a.version.c_str(), a.previous.c_str(),
                        a.running ? "EN MARCHE" : "arrete", a.pid);
            for (const auto& v : a.versions) std::printf(" %s%s", v.id.c_str(), v.active ? "*" : "");
            std::printf("\n");
        }
        if (!o) ++failures;
        return l;
    };
    // Les ids rendus (un agent qui a deja un V1 d'un autre contenu installe celui-ci en V1-2).
    const auto r1 = deployOne("V1", true, false);
    listing();
    const auto r2 = deployOne("V2", true, true);   // le poste demarre avec la session : la cle Run suit la version active
    const auto r3 = deployOne("V3", false, true);
    if (!r3.rolledBack || r3.rolledBackTo != r2.version) ++failures;
    listing();
    RollbackResult rb;
    o = c.rollback(m, rb);
    std::printf("ROLLBACK : %s %s\n", o.ok ? "ok" : "ECHEC", o.message.c_str());
    if (!o || rb.to != r1.version || !rb.running) ++failures;
    listing();
    std::vector<HistoryEntry> h;
    o = c.history(m, 50, h);
    std::printf("GET_HISTORY : %s, %zu entrees\n", o.ok ? "ok" : "ECHEC", h.size());
    for (const auto& e : h)
        std::printf("   %s %s %s %s : %s - %s\n", e.op.c_str(), e.application.c_str(), e.version.c_str(), e.result.c_str(), e.from.c_str(), e.message.c_str());
    if (!keep.empty()) {
        std::printf("essai croise (le poste reste en marche) : %d echec(s)\n", failures);
        return failures == 0 ? 0 : 1;
    }
    // Pour finir : le poste arrete (proprement).
    Client k(me, "PC-LINUX", key->publicKeyHex());
    o = k.connect(m, 10000);
    Value args = Value::object(), out;
    args.set("delai", 2);
    if (o) o = k.stopStation(args, out);
    std::printf("STOP : %s %s (en marche avant : %s, force : %s)\n", o.ok ? "ok" : "ECHEC", o.message.c_str(), out.flag("wasRunning") ? "oui" : "non",
                out.flag("forced") ? "oui" : "non");
    if (!o) ++failures;
    fs::remove_all(base, ec);
    std::printf("essai croise : %d echec(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

// ---- la suite, apres un redemarrage de l'agent : le poste retrouve, arrete, la cle Run retiree ----
int remoteFollowUp(int port, const std::string& keep, bool startOnly) {
    std::string why;
    auto key = DevKey::loadOrCreate(U(fs::path(keep) / "cle"), &why, "PC-LINUX");
    TlsIdentity me;
    MachineStore store(keep);
    if (!key || !TlsIdentity::loadOrCreate(U(fs::path(keep) / "studio"), "studio", false, "XPG-Studio PC-LINUX", me, &why) || !store.load(&why)
        || store.machines().empty()) {
        std::printf("dossier %s illisible : %s\n", keep.c_str(), why.c_str());
        return 1;
    }
    PairedMachine m = store.machines().front();
    m.port = port;
    Client c(me, "PC-LINUX", key->publicKeyHex());
    int failures = 0;
    if (startOnly) {   // lancer la version active et la laisser tourner (avant d'arreter l'agent)
        Outcome o = c.connect(m, 10000);
        Value sa = Value::object(), out;
        sa.set("check", 3);
        if (o) o = c.startStation(sa, out);
        std::printf("START : %s %s - version %s pid %lld\n", o.ok ? "ok" : "ECHEC", o.message.c_str(), out.str("version").c_str(),
                    static_cast<long long>(out.integer("pid")));
        return o.ok ? 0 : 1;
    }
    AppList l;
    Outcome o = c.listApps(m, l);
    std::printf("LIST_APPS apres le redemarrage de l'agent : %s %s - poste %s pid %d version %s%s\n", o.ok ? "ok" : "ECHEC", o.message.c_str(),
                l.station.running ? "EN MARCHE" : "arrete", l.station.pid, l.station.version.c_str(), l.station.adopted ? " (lance par la session, retrouve)" : "");
    if (!o || !l.station.running) ++failures;
    Client k(me, "PC-LINUX", key->publicKeyHex());
    o = k.connect(m, 10000);
    Value args = Value::object(), out;
    args.set("delai", 2);
    if (o) o = k.stopStation(args, out);
    std::printf("STOP : %s %s (en marche avant : %s, pid %lld, force : %s)\n", o.ok ? "ok" : "ECHEC", o.message.c_str(), out.flag("wasRunning") ? "oui" : "non",
                static_cast<long long>(out.integer("pid")), out.flag("forced") ? "oui" : "non");
    if (!o || !out.flag("wasRunning") || out.flag("forced")) ++failures;
    k.close();
    const fs::path pkg = fs::path(keep) / "Essai_Deploiement-V2.xpgdeploy";
    DeployOptions opt;
    opt.autostart = false;
    opt.startCheckSeconds = 3;
    const DeployResult r = c.deploy(m, U(pkg), opt);
    std::printf("V2 de nouveau, sans demarrage avec la session : %s %s (deja installee : %s, envoye %llu octets)\n", r.outcome.ok ? "ok" : "ECHEC",
                r.outcome.message.c_str(), r.alreadyInstalled ? "oui" : "non", static_cast<unsigned long long>(r.bytesSent));
    if (!r.outcome.ok || !r.alreadyInstalled) ++failures;
    o = k.connect(m, 10000);
    if (o) o = k.stopStation(args, out);
    std::printf("STOP : %s %s\n", o.ok ? "ok" : "ECHEC", o.message.c_str());
    if (!o) ++failures;
    std::printf("suite de l'essai croise : %d echec(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (!crypto::init()) {
        std::printf("libsodium indisponible\n");
        return 1;
    }
    if ((argc == 5 || argc == 6) && std::strcmp(argv[1], "--distant") == 0)
        return remote(argv[2], std::atoi(argv[3]), argv[4], argc == 6 ? argv[5] : "");
    if ((argc == 4 || argc == 5) && std::strcmp(argv[1], "--distant-suite") == 0)
        return remoteFollowUp(std::atoi(argv[2]), argv[3], argc == 5 && std::strcmp(argv[4], "lancer") == 0);
    // Le faux poste : a cote de cet essai, ou --poste CHEMIN.
    std::string program;
    if (argc == 3 && std::strcmp(argv[1], "--poste") == 0) {
        program = argv[2];
    } else {
        std::error_code ec;
        fs::path self = fs::absolute(argv[0], ec);
        program = U(self.parent_path() / "deploy_fake_station");
#if defined(_WIN32)
        program += ".exe";
#endif
    }
    if (!present(program)) {
        std::printf("le faux poste est introuvable : %s (cible deploy_fake_station)\n", program.c_str());
        return 1;
    }
    program = U(fs::canonical(program));
    std::error_code ec;
    unsigned char rnd[6];
    crypto::randomBytes(rnd, sizeof rnd);
    const fs::path base = fs::temp_directory_path(ec) / ("xpg-deployinstall-" + crypto::hex(rnd, sizeof rnd));
    fs::create_directories(base, ec);
    run(base, program);
    fs::remove_all(base, ec);
    std::printf("deploy_install_test : %d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
