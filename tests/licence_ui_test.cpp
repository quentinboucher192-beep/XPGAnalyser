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
// =============================================================================
#include "../src/app/Licence.hpp"
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
    }

    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
