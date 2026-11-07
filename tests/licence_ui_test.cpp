// =============================================================================
//  tests/licence_ui_test.cpp - 1.30.0 : les licences DANS L'APPLICATION
//  (app/Licence : la table des points d'entree, le controle, la pastille)
// -----------------------------------------------------------------------------
//  - la table : chaque point d'entree a une fonctionnalite du registre (celui de
//    la maquette), un libelle ; JAMAIS Ouvrir, Enregistrer, Exporter le projet,
//    Fermer, Quitter (la regle << jamais de perte de travail >>) ;
//  - avant start() : tout est ouvert (les essais, les outils) ;
//  - sans cle d'editeur : << Licences non activees >>, tout est ouvert ;
//  - une vraie chaine, avec une cle d'ESSAI creee dans un dossier temporaire :
//    le registre signe (sans licence : FREE), la demande d'activation de ce PC,
//    la licence PERSONAL signee (l'outil Admin, dans ce processus), importee -
//    ce qu'elle ouvre et ce qu'elle ferme, la phrase d'une entree fermee, la
//    pastille ; puis une licence abimee refusee sans rien changer.
//  - 1.32.0 (H3) : les points de passage - require() refuse chaque point d'entree
//    dont la fonctionnalite est fermee ; la simulation de l'IHM ne demarre par
//    aucun chemin (l'onglet qui s'ouvre, Demarrer, Les deux, une vue choisie,
//    Redemarrer), le poste d'exploitation si ; la validation d'un import de vues
//    (advanced_import) est refusee ; avant start(), tout reste ouvert.
// =============================================================================
#include "../src/app/Licence.hpp"
#include "../src/app/hmi/HmiImportDialog.hpp"   // 1.32.0 : la validation d'un import de vues
#include "../src/app/hmi/HmiSimulation.hpp"     // 1.32.0 : la simulation de l'IHM (hmi_runtime)
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPackage.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Licensing.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace adm = license_admin;
using app::licence::Where;

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
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path repo = argc > 1 ? fs::path(argv[1]) : fs::path(".");
    std::mt19937_64 rng(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path base = fs::temp_directory_path() / ("xpg-licence-ui-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(base, ec);

    // ---- la table des points d'entree ----
    std::printf("-- la table des points d'entree\n");
    {
        const std::string registry = readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json");
        check(!registry.empty(), "le registre d'exemple se lit (" + U(repo) + ")");
        const std::set<std::string> forbidden = {"file.open", "project.save", "project.saveAs", "project.export", "program.export", "file.close",
                                                 "app.quit", "file.quit", "project.import", "file.import", "project.duplicate", "project.rename",
                                                 "edit.undo", "edit.redo", "help.licence", "help.open"};
        std::set<std::string> seen;
        bool allKnown = true, allLabelled = true, noneForbidden = true, unique = true;
        for (const auto& g : app::licence::gates()) {
            const std::string key = std::string(g.key), feature = g.feature;
            allKnown = allKnown && has(registry, "\"" + feature + "\"");
            allLabelled = allLabelled && g.label && *g.label;
            noneForbidden = noneForbidden && !forbidden.count(key) && !has(key, "save") && !has(key, "enregistr") && !has(key, "quit");
            unique = seen.insert(std::to_string(static_cast<int>(g.where)) + ":" + key).second && unique;
        }
        check(app::licence::gates().size() >= 15, "au moins 15 points d'entree (" + std::to_string(app::licence::gates().size()) + ")");
        check(allKnown, "chaque fonctionnalite est dans le registre");
        check(allLabelled, "chaque point d'entree a un libelle");
        check(noneForbidden, "ni Ouvrir, ni Enregistrer, ni Exporter le projet, ni Fermer, ni Quitter");
        check(unique, "chaque point d'entree une seule fois");
        check(app::licence::featureOf(Where::HmiPane, "scripts") == "hmi_scripts" && app::licence::featureOf(Where::HmiPane, "vues").empty()
                  && app::licence::featureOf(Where::Action, "ia.assistant") == "ai_assistant" && app::licence::featureOf(Where::Gesture, "sim.stop").empty(),
              "featureOf : les volets, les actions, les gestes (arreter n'est jamais verrouille)");
    }

    // ---- avant start() ----
    std::printf("-- avant start()\n");
    check(!app::licence::active() && app::licence::allows("ai_generation") && app::licence::lockedText("ai_generation").empty()
              && app::licence::badge().text.empty(),
          "avant start() : rien de configure, tout est ouvert, pas de pastille");
    {
        std::string why;
        check(app::licence::require("hmi_runtime", &why) && app::licence::require("advanced_import") && why.empty(),
              "avant start() : require (les points de passage) laisse passer aussi (les essais, les outils)");
    }

    // ---- sans cle d'editeur ----
    std::printf("-- sans cle d'editeur\n");
    {
        lic::StoreOptions o;
        o.folder = U(base / "sans-cle");
        app::licence::start(std::move(o));
        const auto st = lic::getLicenseState();
        bool allOpen = true;
        for (const auto& g : app::licence::gates()) allOpen = allOpen && app::licence::allows(g.feature);
        check(app::licence::active() && st.status == lic::LicenseStatus::NotConfigured, "sans cle : Non activees (" + lic::statusLabel(st.status) + ")");
        check(allOpen && app::licence::lockedText("ai_generation").empty(), "sans cle : chaque point d'entree est ouvert");
        check(has(app::licence::badge().text, "non activ"), "la pastille : " + app::licence::badge().text);
    }

    // ---- une vraie chaine, avec une cle d'essai ----
    std::printf("-- une licence PERSONAL, signee et importee\n");
    {
        std::string why;
        auto key = adm::createPublisherKey(U(base / "admin"), "essai licence_ui_test", true, &why);
        check(key != nullptr, "la cle d'essai de l'editeur (" + why + ")");
        if (!key) {
            std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
            return 1;
        }
        const auto now = lic::systemNow();
        std::string reg;
        check(adm::signRegistry(*key, readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json"), now, reg, &why), "le registre signe (" + why + ")");
        writeFile(base / "livre" / "registre.xpgreg", reg);
        lic::StoreOptions o;
        o.folder = U(base / "pc");
        o.trustedKeys = {trustedOf(*key)};
        o.bundledRegistry = U(base / "livre" / "registre.xpgreg");
        app::licence::start(o);
        auto st = lic::getLicenseState();
        check(st.status == lic::LicenseStatus::Absent && st.edition == "FREE", "sans licence : Absente, l'edition FREE du registre livre (" + st.edition + ")");
        check(app::licence::allows("api_editor") && app::licence::allows("hmi_runtime") && !app::licence::allows("ai_assistant")
                  && !app::licence::allows("hmi_scripts"),
              "FREE : l'editeur et le poste ouverts ; l'Assistant IA et les scripts fermes");
        check(has(app::licence::badge().text, "Sans licence") && app::licence::badge().tone == 2, "la pastille : " + app::licence::badge().text);
        check(app::licence::featureLabel("ai_generation") != "ai_generation", "le libelle d'une fonctionnalite vient du registre : "
              + app::licence::featureLabel("ai_generation"));

        // La demande de ce PC, puis la licence PERSONAL (l'outil Admin).
        const fs::path reqPath = base / "demande.xpgreq";
        check(lic::writeActivationRequest(U(reqPath), "Poste d'essai", "XPG-UI-0001", "IHM Studio (essai)", &why), "la demande d'activation (" + why + ")");
        const std::string req = readFile(reqPath);
        check(!req.empty() && !lic::thisDeviceFingerprint().empty(), "la demande est ecrite, ce PC a son empreinte");
        adm::LicenseInputs in;
        in.params = "{\"license_id\": \"XPG-UI-0001\", \"customer\": \"Client d'essai\", \"edition\": \"PERSONAL\", \"max_devices\": 2, \"duration_days\": 365}";
        in.requests = {req};
        in.registry = reg;
        std::string licFile, summary;
        check(adm::signLicense(*key, in, now, licFile, &why, &summary), "la licence PERSONAL signee (" + why + ")");
        writeFile(base / "XPG-UI-0001.xpglic", licFile);

        // Un fichier abime d'abord : refuse, rien ne change.
        std::string broken = licFile;
        if (const auto at = broken.find("PERSONAL"); at != std::string::npos) broken[at] = 'Q';
        writeFile(base / "abime.xpglic", broken);
        const auto bad = lic::importLicenseFile(U(base / "abime.xpglic"));
        app::licence::refresh();
        check(!bad.ok && lic::getLicenseState().status == lic::LicenseStatus::Absent, "un fichier abime : refuse, rien ne change (" + bad.why + ")");

        const auto report = lic::importLicenseFile(U(base / "XPG-UI-0001.xpglic"));
        app::licence::refresh();
        st = lic::getLicenseState();
        check(report.ok && report.licenseInstalled && st.status == lic::LicenseStatus::Valid && st.edition == "PERSONAL",
              "la licence importee : Valide, PERSONAL (" + st.message + ")");
        check(app::licence::allows("ai_assistant") && app::licence::allows("hmi_scripts") && app::licence::allows("api_library")
                  && !app::licence::allows("ai_generation") && !app::licence::allows("professional_tools") && !app::licence::allows("hmi_export"),
              "PERSONAL : l'Assistant, les scripts, les bibliotheques ouverts ; la generation IA, les outils pro, l'export des vues fermes");
        const std::string locked = app::licence::lockedText("ai_generation");
        check(!locked.empty() && has(locked, "PERSONAL"), "la phrase d'une entree fermee : " + locked);
        check(app::licence::lockedText("ai_assistant").empty(), "une entree ouverte : pas de phrase");
        const auto b = app::licence::badge();
        check(has(b.text, "Licence PERSONAL") && has(b.text, "valide jusqu'au") && b.tone == 1, "la pastille : " + b.text);
        const auto devices = lic::getLicensedDevices();
        check(devices.size() == 1 && devices[0].thisPc && devices[0].name == "Poste d'essai", "Mes appareils : ce PC, sous le nom donne");
        const auto history = lic::getHistory(20);
        bool importLine = false, refusedLine = false;
        for (const auto& h : history) {
            importLine = importLine || (h.kind == "import" && h.ok);
            refusedLine = refusedLine || (h.kind == "import" && !h.ok);
        }
        check(importLine && refusedLine, "l'Historique : l'import accepte et l'import refuse");
        bool gatesConsistent = true;
        for (const auto& g : app::licence::gates()) gatesConsistent = gatesConsistent && app::licence::allows(g.feature) == lic::isFeatureEnabled(g.feature);
        check(gatesConsistent, "chaque point d'entree suit le module (isFeatureEnabled)");

        // ---- 1.32.0 (H3) : les points de passage ----
        std::printf("-- 1.32.0 : les points de passage (la simulation de l'IHM, l'import de vues, require)\n");
        bool requireConsistent = true;
        for (const auto& g : app::licence::gates()) {
            std::string w;
            const bool open = app::licence::require(g.feature, &w);
            requireConsistent = requireConsistent && open == app::licence::allows(g.feature) && (open ? w.empty() : has(w, g.feature));
        }
        check(requireConsistent, "PERSONAL : require suit allows pour chaque point d'entree (fermee : la phrase qui la nomme)");
        auto doc = std::make_shared<hmi::Document>();
        hmi::View accueil = hmi::makeView(doc->project, "Accueil");
        const hmi::Id accueilId = accueil.id;
        doc->project.views = {accueil};
        doc->project.config.startView = accueilId;
        {
            app::HmiSimulationPane pane("sim.licence.ouverte", doc, app::HmiSimulationHost{});
            pane.setBounds({0, 0, 800, 600});
            pane.layout();
            pane.refreshAt(0.0);
            check(pane.hmiRunning(), "PERSONAL (hmi_runtime ouverte) : la simulation de l'IHM demarre");
            pane.command("hmi.stop");
        }
        // La validation d'un import de vues : advanced_import n'est pas dans PERSONAL. Le meme refus quel que soit le
        // chemin qui a ouvert la fenetre (Importer des vues, un paquet lache, la ligne de commande, un script).
        {
            app::HmiImportDialog::Spec spec;
            spec.fileName = "Armoires.xpgvues";
            spec.manifest.kind = "vues";
            spec.plan.items.push_back(hmi::pkg::Item{});
            app::HmiImportDialog dialog(std::move(spec));
            dialog.confirm();
            const auto state = dialog.state();
            check(state && has(state->error, "advanced_import"), "un paquet de vues : la validation de l'import est refusee (" + (state ? state->error : std::string()) + ")");
        }

        // Une licence CUSTOM (api_editor seul) : aucune fonctionnalite des points d'entree.
        const fs::path req2Path = base / "demande-2.xpgreq";
        check(lic::writeActivationRequest(U(req2Path), "Poste d'essai", "XPG-UI-0002", "IHM Studio (essai)", &why), "la demande pour XPG-UI-0002 (" + why + ")");
        adm::LicenseInputs in2;
        in2.params = "{\"license_id\": \"XPG-UI-0002\", \"customer\": \"Client d'essai\", \"edition\": \"CUSTOM\", \"features\": [\"api_editor\"], "
                     "\"duration_days\": 365}";
        in2.requests = {readFile(req2Path)};
        in2.registry = reg;
        std::string custom;
        check(adm::signLicense(*key, in2, now, custom, &why, &summary), "la licence CUSTOM signee (" + why + ")");
        writeFile(base / "XPG-UI-0002.xpglic", custom);
        const auto report2 = lic::importLicenseFile(U(base / "XPG-UI-0002.xpglic"));
        app::licence::refresh();
        check(report2.ok && lic::getLicenseState().edition == "CUSTOM" && !app::licence::allows("hmi_runtime"),
              "CUSTOM importee : hmi_runtime fermee (" + report2.why + ")");
        bool allRefused = true;
        for (const auto& g : app::licence::gates()) {
            std::string w;
            allRefused = allRefused && !app::licence::require(g.feature, &w) && has(w, g.feature);
        }
        check(allRefused, "CUSTOM : require refuse CHAQUE point d'entree (" + std::to_string(app::licence::gates().size()) + "), avec la phrase");
        check(app::licence::require("api_editor"), "une fonctionnalite de la licence : require laisse passer");
        {
            app::HmiSimulationPane pane("sim.licence.fermee", doc, app::HmiSimulationHost{});
            pane.setBounds({0, 0, 800, 600});
            pane.layout();
            pane.refreshAt(0.0);
            const bool tab = !pane.hmiRunning();
            pane.startHmi("bouton D\xC3\xA9marrer l'IHM");
            const bool start = !pane.hmiRunning();
            pane.command("hmi.start");
            pane.command("hmi.toggle");
            pane.command("both.start");
            const bool bar = !pane.hmiRunning();
            pane.goToView(accueilId);
            const bool view = !pane.hmiRunning();
            pane.restart();
            pane.restartClean("essai");
            pane.buildAndRestart("essai");
            const bool restart = !pane.hmiRunning();
            pane.refreshAt(0.5);
            check(tab && start && bar && view && restart,
                  std::string("hmi_runtime fermee : l'IHM ne demarre par aucun chemin (onglet ") + (tab ? "ok" : "DEMARRE") + ", Demarrer " + (start ? "ok" : "DEMARRE")
                      + ", la barre " + (bar ? "ok" : "DEMARRE") + ", une vue " + (view ? "ok" : "DEMARRE") + ", Redemarrer " + (restart ? "ok" : "DEMARRE") + ")");
            check(has(pane.canvasNote(), "hmi_runtime"), "la vue arretee dit pourquoi : " + pane.canvasNote());
        }
        {
            app::HmiSimulationPane station("poste.licence", doc, app::HmiSimulationHost{});
            station.setStationMode(true);
            station.setBounds({0, 0, 800, 600});
            station.layout();
            station.refreshAt(0.0);
            check(station.hmiRunning(), "le poste d'exploitation : jamais arrete par la licence (hmi_runtime fermee, il demarre)");
            station.command("hmi.stop");
        }
    }

    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
