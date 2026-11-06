// =============================================================================
//  tests/deploy_ui_test.cpp - 1.28.0 : le volet IHM > Deploiement reseau, branche
//                             sur un vrai agent (deploy::Agent) sur la boucle locale
// -----------------------------------------------------------------------------
//  Ce que fait le developpeur, geste par geste, par le volet et son fil de travail
//  (HmiDeployPane + HmiDeployService) : les cles de ce PC creees, Ajouter par
//  adresse (127.0.0.1), un code faux refuse, le bon code (deploy::createCode, comme
//  --appairer) -> appairee ; Rechercher la retrouve ; Etat, Journal ; Creer un
//  package (le projet FINISH) puis le relire ; un projet DEV refuse ; Oublier.
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
