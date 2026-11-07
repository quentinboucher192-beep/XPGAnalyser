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
//  - 1.35.0 : la licence active toujours visible - la pastille et l'indicateur
//    (la meme phrase : << Licence : PERSONAL . jusqu'au ... >>, << Sans licence >>),
//    l'encart de l'accueil, la partie << licence >> du bandeau haut (son texte, la
//    forme courte puis le cadenas seul quand c'est serre, un clic : help.licence).
// =============================================================================
#include "../src/app/Licence.hpp"
#include "../src/app/LicenceService.hpp"   // 1.36.0 : la demande en ligne
#include "../src/app/StartPage.hpp"             // 1.35.0 : l'encart de la licence sur l'accueil (le menu principal)
#include "../src/app/TopBar.hpp"                // 1.35.0 : la licence active dans le bandeau haut
#include "../src/ui/Theme.hpp"
#include "../src/app/hmi/HmiImportDialog.hpp"   // 1.32.0 : la validation d'un import de vues
#include "../src/app/hmi/HmiSimulation.hpp"     // 1.32.0 : la simulation de l'IHM (hmi_runtime)
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPackage.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Licensing.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <thread>
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

// 1.35.0 : un renderer qui retient les textes (le bandeau haut, l'accueil dessines a blanc).
struct Recorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color) override { texts.emplace_back(t); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {static_cast<float>(t.size()) * f.v * 0.5f, static_cast<float>(f.v), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
};

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
        o.compiledKeys = false;   // 1.35.1 : la vraie cle de l'editeur est compilee ; << sans cle >> : un programme qui n'en a pas
        app::licence::start(std::move(o));
        const auto st = lic::getLicenseState();
        bool allOpen = true;
        for (const auto& g : app::licence::gates()) allOpen = allOpen && app::licence::allows(g.feature);
        check(app::licence::active() && st.status == lic::LicenseStatus::NotConfigured, "sans cle : Non activees (" + lic::statusLabel(st.status) + ")");
        check(allOpen && app::licence::lockedText("ai_generation").empty(), "sans cle : chaque point d'entree est ouvert");
        check(has(app::licence::badge().text, "non activ"), "la pastille : " + app::licence::badge().text);
        check(app::licence::projectsAllowed(), "1.35.1 : sans cle, les projets s'ouvrent (licences pas actives)");
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
        check(app::licence::indicator().text == "Sans licence" && app::licence::indicator().tone == 2, "l'indicateur du bandeau : la meme phrase");
        {   // 1.35.1 : sans licence, le logiciel ne s'utilise pas : aucun projet ne s'ouvre ni ne se cree
            std::string refus;
            check(!app::licence::projectsAllowed(&refus) && has(refus, "pas de licence"), "sans licence : aucun projet (" + refus + ")");
        }
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
        check(app::licence::projectsAllowed(), "1.35.1 : avec la licence, les projets s'ouvrent");
        check(app::licence::allows("ai_assistant") && app::licence::allows("hmi_scripts") && app::licence::allows("api_library")
                  && !app::licence::allows("ai_generation") && !app::licence::allows("professional_tools") && !app::licence::allows("hmi_export"),
              "PERSONAL : l'Assistant, les scripts, les bibliotheques ouverts ; la generation IA, les outils pro, l'export des vues fermes");
        const std::string locked = app::licence::lockedText("ai_generation");
        check(!locked.empty() && has(locked, "PERSONAL"), "la phrase d'une entree fermee : " + locked);
        check(app::licence::lockedText("ai_assistant").empty(), "une entree ouverte : pas de phrase");
        const auto b = app::licence::badge();
        check(has(b.text, "Licence : PERSONAL \xC2\xB7 jusqu'au ") && b.tone == 1 && b.shortText == "PERSONAL", "la pastille : " + b.text);
        {   // 1.35.0 : la meme phrase dans le bandeau haut ; l'encart de l'accueil apres startNotices
            const auto ind = app::licence::indicator();
            check(ind.text == b.text && ind.tone == 1 && has(ind.tip, "fen\xC3\xAAtre Licence"), "l'indicateur : " + ind.text);
            check(app::licence::startCard().title.empty(), "l'encart de l'accueil : rien avant startNotices (le poste d'exploitation)");
            app::licence::showNotices();   // 1.35.1 : comme le poste d'exploitation refuse, revenu a l'accueil
            app::licence::showNotices();   // une deuxieme fois : sans effet
            const auto card = app::licence::startCard();
            check(has(card.title, "Licence : PERSONAL") && has(card.text, "XPG-UI-0001") && card.tone == 1 && card.tab == app::licence::kTabAuto,
                  "l'encart de l'accueil : " + card.title + " / " + card.text);
            app::TopBar bar("barre.licence");
            std::vector<std::string> sent;
            bar.setActionSink([&sent](core::ActionId a) { sent.emplace_back(std::string(a)); });
            bar.setBounds(gfx::Rect{0.f, 0.f, 1600.f, 44.f});
            bar.layout();
            check(bar.partRect("licence").w <= 0.f, "le bandeau : sans licence donnee, pas de partie licence");
            bar.setLicence({ind.text, ind.shortText, ind.tip, ind.tone});
            bar.layout();
            const auto lr = bar.partRect("licence"), aide = bar.partRect("aide"), cloche = bar.partRect("cloche");
            check(lr.w > 120.f && lr.right() <= aide.x && lr.x > cloche.right() - 0.5f, "le bandeau (1600 px) : la licence a droite, avant Affichage et Aide");
            check(bar.reaches("help.licence") && bar.liveTooltip({lr.x + 10.f, lr.y + 10.f}) == ind.tip, "son infobulle : la phrase de l'etat");
            bar.trigger("help.licence");
            check(!sent.empty() && sent.back() == "help.licence", "un clic : la fenetre Licence (help.licence)");
            bar.setBounds(gfx::Rect{0.f, 0.f, 1100.f, 44.f});
            bar.layout();
            const float compact = bar.partRect("licence").w;
            check(compact > 0.f && compact < lr.w && bar.partRect("aide").right() <= 1100.f, "1100 px : la forme courte, tout tient");
            bar.setBounds(gfx::Rect{0.f, 0.f, 640.f, 44.f});
            bar.layout();
            check(bar.partRect("licence").w > 0.f && bar.partRect("licence").w <= 36.f,
                  "tres serre : le cadenas seul, jamais disparu (" + std::to_string(bar.partRect("licence").w) + " px)");
            // dessines a blanc : le bandeau (la phrase entiere a 1600 px) et l'encart de l'accueil
            bar.setBounds(gfx::Rect{0.f, 0.f, 1600.f, 44.f});
            bar.layout();
            Recorder rec;
            bar.render(ui::PaintContext{rec, ui::Theme::dark(), bar.bounds(), 0.0, nullptr});
            // (la phrase entiere si elle tient a la mesure du renderer, sinon la forme courte)
            check(rec.saw(ind.text) || rec.saw(ind.shortText), "le bandeau dessine la licence : " + ind.text);
            app::start::StartRail rail("startup.rail");
            rail.setBounds(gfx::Rect{0.f, 0.f, 380.f, 900.f});
            rail.setLicenceCard(card.title, card.text, card.tone, card.button);
            rail.layout();
            Recorder r2;
            rail.render(ui::PaintContext{r2, ui::Theme::light(), rail.bounds(), 0.0, nullptr});
            check(r2.saw("Licence : PERSONAL") && rail.licenceButton().visible() && rail.licenceButton().text() == card.button && rail.licenceTone() == 1,
                  "l'accueil dessine l'encart de la licence, son bouton (" + rail.licenceButton().text() + ")");
            rail.setLicenceCard("D\xC3\xA9" "couverte : il te reste 23 jours (jusqu'au 30/11/2026)", "Ensuite : les fonctions de l'\xC3\xA9" "dition sans licence.", 4,
                                "Activer avec un code\xE2\x80\xA6");
            rail.layout();
            Recorder r3;
            rail.render(ui::PaintContext{r3, ui::Theme::dark(), rail.bounds(), 0.0, nullptr});
            check(r3.saw("D\xC3\xA9" "couverte") && rail.licenceButton().text() == "Activer avec un code\xE2\x80\xA6" && rail.licenceTone() == 4,
                  "l'encart de la Decouverte : Activer avec un code...");
            rail.setLicenceCard({}, {}, 0, {});
            rail.layout();
            check(!rail.licenceButton().visible(), "pas d'encart : le bouton cache");
        }
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

        // ---- 1.36.0 : la demande en ligne, le chemin principal (un PC sans licence ; aucun appel au reseau ici) ----
        std::printf("-- 1.36.0 : la demande en ligne (l'encart, la demande qui attend, la reponse)\n");
        {
            namespace ls = app::licence::service;
            lic::StoreOptions o2;
            o2.folder = U(base / "pc-demande");
            o2.trustedKeys = {trustedOf(*key)};
            o2.bundledRegistry = U(base / "livre" / "registre.xpgreg");
            app::licence::start(o2);   // les avis restent (startNotices plus haut)
            check(lic::getLicenseState().status == lic::LicenseStatus::Absent, "un autre PC, sans licence");
            ls::setAddressForTests(licensing::service::Address{});   // pas de service
            auto card = app::licence::startCard();
            check(!app::licence::onlineRequests() && has(card.button, "Activer avec un code") && card.tab == app::licence::kTabActivation,
                  "sans service : le code (ou le fichier), comme avant (" + card.button + ")");
            licensing::service::Address a;
            a.url = "http://127.0.0.1:1";   // un service suppose (personne n'y ecoute ; rien ne l'appelle ici)
            ls::setAddressForTests(a);
            card = app::licence::startCard();
            check(app::licence::onlineRequests() && card.button == "Demander ma licence\xE2\x80\xA6" && card.tab == app::licence::kTabRequest &&
                      has(card.title, "Sans licence") && has(card.text, "Demande ta licence") && card.tone == 3,
                  "avec un service : << Demander ma licence... >>, l'onglet de la demande (" + card.text + ")");
            // une demande qui attend (le releve du service, tel que l'ecrit le Studio)
            writeFile(base / "pc-demande" / ls::kRecordFile,
                      "{\"numero\": 12, \"poste\": \"0123456789abcdef0123456789abcdef\", \"demande\": \"nonce-essai\", \"envoyee_le\": \"2026-11-07T10:42:00Z\", "
                      "\"client\": \"Client d'essai\", \"etat_local\": \"envoyee\"}");
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));   // le releve est relu au plus une fois par seconde
            const auto pr = app::licence::pendingRequest();
            card = app::licence::startCard();
            check(pr.pending && pr.number == 12 && has(pr.state, "attente"), "la demande qui attend : relue (" + pr.state + ")");
            check(has(card.title, "Demande envoy") && has(card.text, "n\xC2\xB0 12") && has(card.text, "s'installera") && card.button == "Voir ma demande" &&
                      card.tab == app::licence::kTabRequest && card.tone == 2,
                  "l'encart : la demande en attente, << Voir ma demande >> (" + card.title + " / " + card.text + ")");
            std::string refus;
            check(!app::licence::projectsAllowed(&refus), "en attendant : aucun projet");
            // la reponse de l'editeur : une fenetre (la file)
            app::licence::queueAnswer({ls::kAnswerAccepted, "XPG-UI-0002", "Bonne route"});
            app::licence::queueAnswer({ls::kAnswerRefused, "", "Raison d'essai"});
            const auto first = app::licence::takeAnswer();
            const auto second = app::licence::takeAnswer();
            check(first && first->kind == ls::kAnswerAccepted && first->note == "Bonne route" && second && second->kind == ls::kAnswerRefused &&
                      !app::licence::takeAnswer(),
                  "les reponses de l'editeur : une fenetre chacune, dans l'ordre");
            fs::remove(base / "pc-demande" / ls::kRecordFile, ec);
            ls::setAddressForTests(licensing::service::Address{});
        }
    }

    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
