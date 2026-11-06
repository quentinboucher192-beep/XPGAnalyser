// =============================================================================
//  tests/deploy_ui_test.cpp - 1.28.0 : le volet IHM > Deploiement reseau, branche
//                             sur un vrai agent (deploy::Agent) sur la boucle locale
// -----------------------------------------------------------------------------
//  Ce que fait le developpeur, geste par geste, par le volet et son fil de travail
//  (HmiDeployPane + HmiDeployService) : les cles de ce PC creees, Ajouter par
//  adresse (127.0.0.1), un code faux refuse, le bon code (deploy::createCode, comme
//  --appairer) -> appairee ; Rechercher la retrouve ; Etat, Journal ; Creer un
//  package (le projet FINISH) puis le relire ; un projet DEV refuse ; Oublier.
//  1.29.0 (Linux) : Deployer (la progression vue a l'ecran), le poste lance par
//  l'agent (un petit programme d'essai qui fait comme le poste : il attend la
//  demande d'arret, ecrit son etat et quitte), Applications, Historique, un second
//  Deployer (le poste prevenu, son etat garde), Revenir a la precedente.
//  De vraies sockets, de vraies poignees de main : rien n'est simule.
// =============================================================================
#include "../src/app/hmi/HmiDeployPane.hpp"
#include "../src/app/hmi/HmiDeployService.hpp"
#include "../src/deploy/Agent.hpp"
#include "../src/deploy/DevKey.hpp"
#include "../src/deploy/Package.hpp"
#include "../src/deploy/Pairing.hpp"

#include <chrono>
#include <cstdio>
#include <sstream>
#include <thread>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

namespace {

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
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Un geste qui part, puis son attente en regardant la progression (comme l'ecran, image apres image).
struct Watched {
    bool sawText{false};
    double maxFraction{-1};
    bool finished{false};
};
Watched watch(app::HmiDeployPane& pane, double seconds) {
    Watched w;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
    while (std::chrono::steady_clock::now() < end) {
        pane.poll();
        if (!pane.busy()) {
            w.finished = true;
            return w;
        }
        if (!pane.progressText().empty()) w.sawText = true;
        w.maxFraction = std::max(w.maxFraction, pane.progressFraction());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return w;
}

void makeProject(const fs::path& root, const std::string& state) {
    writeFile(root / "project.xpgproj", "# API Studio project. Safe to read; edit with care.\nformatVersion = 1\nname = Essai_Poste\n"
                                        "version = 0.0.1\nstate = " + state + "\ncreated = 2026-11-06 10:00\nmodified = 2026-11-06 10:00\n"
                                        "author = essai\ncomment = \nedition = ihm\n");
    writeFile(root / "ihm" / "ihm.txt", "# IHM Studio\nformat 33\nvue id=1 fichier=vues/0001-Accueil.vue\nfin\n");
    writeFile(root / "ihm" / "vues" / "0001-Accueil.vue", "vue id=1 nom=Accueil\nfin\n");
    writeFile(root / "donnees" / "consignes.csv", "nom;valeur\nP1;3\n");
}

} // namespace

int main() {
    std::mt19937_64 rng(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path base = fs::temp_directory_path() / ("xpg-deploy-ui-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path agentDir = base / "agent", studioDir = base / "studio", project = base / "Essai_Poste", dev = base / "Essai_DEV";
    makeProject(project, "FINISH");
    makeProject(dev, "DEV");

    deploy::AgentConfig cfg;
    cfg.dir = U(agentDir);
    cfg.bindAddress = "127.0.0.1";
    cfg.port = 0;
    cfg.discoveryPort = 0;
    cfg.machineName = "PC-ESSAI";
#ifndef _WIN32
    // 1.29.0 : le poste d'essai (un script : il note comment il est lance, attend la demande d'arret, garde son etat).
    const fs::path fakeStation = base / "poste-essai.sh";
    writeFile(fakeStation, "#!/bin/sh\n"
                           "echo \"$@\" >> \"" + U(base / "poste-lancements.txt") + "\"\n"
                           "if [ -f \"$XPG_REPRISE_FICHIER\" ]; then echo reprise >> \"" + U(base / "poste-lancements.txt") + "\"; rm -f \"$XPG_REPRISE_FICHIER\"; fi\n"
                           "while [ ! -f \"$XPG_ARRET_FICHIER\" ]; do sleep 0.1; done\n"
                           "echo \"etat du poste d'essai\" > \"$XPG_REPRISE_FICHIER\"\n"
                           "exit 0\n");
    fs::permissions(fakeStation, fs::perms::owner_all, fs::perm_options::replace, ec);
    cfg.runtimeProgram = U(fakeStation);
    cfg.startCheckMs = 1500;
    cfg.stopGraceMs = 5000;
#endif
    deploy::Agent agent(cfg);
    std::string why;
    check(agent.start(&why), "l'agent demarre [" + why + "]");
    if (!agent.running()) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }

    app::HmiDeployPane pane("essai.deploy", app::makeDeployBackend(U(studioDir), [] { return std::string("C:\\Program Files\\API IHM Studio\\IHM-Studio.exe"); },
                                                                  agent.discoveryPort()));
    app::HmiDeployPane::Hosts hosts;
    std::string folder = U(project);
    hosts.projectFolder = [&folder] { return folder; };
    hosts.finished = [&folder](std::string* w) {
        deploy::ProjectInfo info;
        if (!deploy::readProjectInfo(folder, info, w)) return false;
        if (!info.finished()) {
            if (w) *w = "le projet est " + info.state;
            return false;
        }
        return true;
    };
    pane.setHosts(hosts);

    // A l'ouverture : les cles de ce PC (creees), aucune machine.
    check(pane.waitIdle(30), "l'ouverture finit");
    check(!pane.lastError() && pane.machines().empty(), "aucune machine au depart [" + pane.message() + "]");
    check(fs::exists(studioDir / "studio.cer") && fs::exists(studioDir / deploy::DevKey::kFileName), "le certificat et la cle d'editeur de ce PC crees");

    // Ajouter par adresse.
    check(!pane.addAddress("  "), "Ajouter : une adresse vide est refusee tout de suite");
    check(pane.addAddress("127.0.0.1", agent.port()) && pane.waitIdle(30), "Ajouter 127.0.0.1");
    check(!pane.lastError() && pane.machines().size() == 1 && !pane.machines()[0].paired && pane.machines()[0].name == "PC-ESSAI"
              && pane.machines()[0].id == agent.machineId(),
          "Ajouter : PC-ESSAI, decouverte, pas appairee [" + pane.message() + "]");
    const std::string id = agent.machineId();

    // Appairer : un code faux, puis le bon.
    check(!pane.pairWith(id, "12a"), "Appairer : un code qui n'a pas 6 chiffres est refuse tout de suite");
    deploy::pairing::CodeState code;
    check(deploy::pairing::createCode(U(agentDir), code, &why), "--appairer : un code [" + why + "]");
    const std::string wrong = code.code == "000000" ? "111111" : "000000";
    check(pane.pairWith(id, wrong) && pane.waitIdle(30), "Appairer avec un code faux");
    check(pane.lastError() && !pane.machines().empty() && !pane.machines()[0].paired, "un code faux : refuse [" + pane.message() + "]");
    check(pane.pairWith(id, code.code) && pane.waitIdle(30), "Appairer avec le bon code");
    check(!pane.lastError() && pane.machines().size() == 1 && pane.machines()[0].paired && has(pane.message(), "Appair"),
          "le bon code : appairee [" + pane.message() + "]");
    check(fs::exists(studioDir / "machines-appairees.json"), "la machine est gardee (machines-appairees.json)");

    // Rechercher : en direct a son adresse, elle repond (toujours une seule ligne, appairee).
    check(pane.discover() && pane.waitIdle(30), "Rechercher");
    check(!pane.lastError() && pane.machines().size() == 1 && pane.machines()[0].paired && !pane.machines()[0].version.empty(),
          "Rechercher : la machine appairee a repondu, sa version lue [" + pane.message() + "]");

    // Etat et Journal.
    check(pane.select(id) && pane.askStatus() && pane.waitIdle(30), "Etat");
    check(!pane.lastError() && has(pane.message(), "PC-ESSAI"), "Etat : lu [" + pane.message() + "]");
    check(pane.askLogs() && pane.waitIdle(30), "Journal");
    check(!pane.lastError() && !pane.logLines().empty(), "Journal : des lignes (" + std::to_string(pane.logLines().size()) + ") [" + pane.message() + "]");
    bool pairLine = false;
    for (const auto& l : pane.logLines()) pairLine = pairLine || has(l, "PAIR");
    check(pairLine, "Journal : l'appairage y est");

    // Creer un package : le projet FINISH, puis le relire avec la cle de ce PC.
    check(pane.createPackage() && pane.waitIdle(60), "Creer un package");
    check(!pane.lastError() && !pane.lastPackage().empty() && fs::exists(fs::path(std::u8string(pane.lastPackage().begin(), pane.lastPackage().end()))),
          "le package est ecrit [" + pane.message() + "]");
    check(has(pane.lastPackage(), "exports") && has(pane.lastPackage(), ".xpgdeploy"), "dans exports/, en .xpgdeploy : " + pane.lastPackage());
    {
        auto key = deploy::DevKey::loadOrCreate(U(studioDir), &why);
        deploy::PackageVerifyOptions v;
        if (key) v.approvedKeys.push_back(key->publicKey());
        const auto report = deploy::verifyPackage(pane.lastPackage(), v);
        check(report.ok() && report.manifest.application == "Essai_Poste", "le package se relit : signature et empreintes [" + report.reason + "]");
        bool launcher = false;
        for (const auto& f : report.manifest.files) launcher = launcher || has(f.path, "lancer-poste");
        check(launcher, "le lanceur est dans le package");
    }
    folder = U(dev);
    check(!pane.createPackage() && has(pane.message(), "FINISH"), "un projet DEV : refuse tout de suite [" + pane.message() + "]");
    folder = U(project);

#ifndef _WIN32
    // 1.29.0 : Deployer (sans demarrer avec la session : l'essai ne touche pas a la session de l'utilisateur).
    {
        app::HmiDeployPane::DeployOptions o;
        o.autostart = false;
        o.stopDelay = 1;
        check(pane.select(id) && pane.deploy(o), "Deployer : parti");
        const auto w = watch(pane, 120);
        check(w.finished, "Deployer : fini en moins de 2 minutes");
        check(w.sawText, "Deployer : la progression s'est vue a l'ecran");
        check(!pane.lastError() && has(pane.message(), "Essai_Poste") && has(pane.message(), "le poste tourne"),
              "Deployer : installe, le poste tourne [" + pane.message() + "]");
        check(pane.progressText().empty(), "la progression s'efface a la fin");
        check(pane.appRows().size() == 1 && pane.appRows()[0][0] == "Essai_Poste" && has(pane.appRows()[0][3], "en marche"),
              "Applications (apres Deployer) : Essai_Poste, le poste en marche");
        const std::string launches = readAll(base / "poste-lancements.txt");
        check(has(launches, "--ihm") && has(launches, "Applications"), "le poste lance avec --ihm sur la version installee : " + launches);
        check(fs::exists(agentDir / "Applications" / "Essai_Poste" / "active.json"), "active.json ecrit");
    }
    check(pane.askHistory() && pane.waitIdle(30), "Historique");
    {
        bool install = false;
        for (const auto& r : pane.historyRows()) install = install || (r.size() == 5 && has(r[4], "Installation") && r[2] == "Essai_Poste");
        check(!pane.lastError() && install, "Historique : l'installation y est [" + pane.message() + "]");
    }
    // Le projet change (sans nouvelle version) : un second Deployer, le poste en marche prevenu, son etat garde.
    writeFile(project / "donnees" / "consignes.csv", "nom;valeur\nP1;4\n");
    {
        app::HmiDeployPane::DeployOptions o;
        o.autostart = false;
        o.stopDelay = 1;
        check(pane.deploy(o), "second Deployer : parti");
        const auto w = watch(pane, 120);
        check(w.finished && !pane.lastError() && has(pane.message(), "l'ancien poste s'est ferm"),
              "second Deployer : l'ancien poste prevenu s'est ferme [" + pane.message() + "]");
        check(pane.appRows().size() == 1 && !has(pane.appRows()[0][2], "aucune"), "Applications : la version d'avant est gardee : "
              + (pane.appRows().empty() ? std::string("-") : pane.appRows()[0][2]));
        check(has(readAll(base / "poste-lancements.txt"), "reprise"), "la nouvelle version a repris l'etat garde par l'ancienne");
    }
    // Revenir a la precedente.
    check(pane.rollback() && pane.waitIdle(90), "Revenir a la precedente");
    check(!pane.lastError() && has(pane.message(), "Revenu"), "Revenir : fait [" + pane.message() + "]");
    check(pane.askApps() && pane.waitIdle(30) && !pane.lastError(), "Applications");
    // Un projet DEV : refuse tout de suite.
    folder = U(dev);
    check(!pane.deploy() && has(pane.message(), "FINISH"), "Deployer un projet DEV : refuse tout de suite [" + pane.message() + "]");
    folder = U(project);
    // Le poste d'essai s'arrete (la demande d'arret, comme l'agent la pose).
    writeFile(agentDir / "arret.json", "{\"raison\": \"fin de l'essai\", \"delai\": 0}");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif

    // Oublier : elle l'oublie aussi.
    check(pane.select(id) && pane.forget() && pane.waitIdle(30), "Oublier");
    check(!pane.lastError() && has(pane.message(), "aussi"), "Oublier : et elle t'a oublie aussi [" + pane.message() + "]");
    bool stillPaired = false;
    for (const auto& m : pane.machines()) stillPaired = stillPaired || m.paired;
    check(!stillPaired, "plus de machine appairee ici");

    agent.stop();
    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
