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
#include "../src/app/BackgroundTasks.hpp"
#include "../src/app/Licence.hpp"
#include "../src/app/Notices.hpp"           // 1.38.0 : les notifications generales
#include "../src/core/Json.hpp"             // 1.39.0 : le tableau d'une notification
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
#include <cstdlib>
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
    check(!app::licence::locked(Where::HmiPane, "scripts") && !app::licence::lockedAction("ia.assistant"),
          "1.39.0 : avant start(), rien n'est barre dans les menus ni les listes");
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
        check(app::licence::locked(Where::HmiPane, "scripts") && app::licence::lockedAction("ia.assistant") && app::licence::locked(Where::HmiPane, "fonctions") &&
                  !app::licence::lockedAction("hmi.start") && !app::licence::lockedAction("sim.stop") && !app::licence::lockedAction("project.save") &&
                  !app::licence::locked(Where::HmiPane, "vues") && !app::licence::locked(Where::HmiPane, ""),
              "1.39.0 : barres dans les menus et les listes - les scripts, l'Assistant IA ; pas le poste, ni Arreter, ni Enregistrer");
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
            const auto answerOf = [](const char* kind, const char* id, const char* note) {
                app::licence::Answer a;
                a.kind = kind;
                a.licenseId = id;
                a.note = note;
                return a;
            };
            app::licence::queueAnswer(answerOf(ls::kAnswerAccepted, "XPG-UI-0002", "Bonne route"));
            app::licence::queueAnswer(answerOf(ls::kAnswerRefused, "", "Raison d'essai"));
            const auto first = app::licence::takeAnswer();
            const auto second = app::licence::takeAnswer();
            check(first && first->kind == ls::kAnswerAccepted && first->note == "Bonne route" && second && second->kind == ls::kAnswerRefused &&
                      !app::licence::takeAnswer(),
                  "les reponses de l'editeur : une fenetre chacune, dans l'ordre");
            fs::remove(base / "pc-demande" / ls::kRecordFile, ec);
            ls::setAddressForTests(licensing::service::Address{});
        }
    }

#ifndef _WIN32
    std::printf("-- 1.37.0 : Mon profil, l'apercu d'une fonctionnalite\n");
    {
        const fs::path cfg = base / "reglages";
        fs::create_directories(cfg, ec);
        setenv("XDG_CONFIG_HOME", cfg.string().c_str(), 1);
        check(app::licence::loadProfile().empty(), "sans profil : vide (tout est facultatif)");
        app::licence::Profile p;
        p.email = " camille.essai@exemple.fr ";
        p.name = "Camille Essai";
        p.company = "Client d'essai Atelier";
        std::string why;
        check(app::licence::saveProfile(p, &why), "le profil enregistre (" + why + ")");
        auto back = app::licence::loadProfile();
        check(back.email == "camille.essai@exemple.fr" && back.name == p.name && back.company == p.company, "relu tel quel (le courriel sans les espaces)");
        check(fs::path(app::licence::profilePath()).parent_path().string().rfind(cfg.string(), 0) == 0, "dans le dossier des reglages : " + app::licence::profilePath());
        app::licence::Profile bad = p;
        bad.email = "pas une adresse";
        check(!app::licence::saveProfile(bad, &why) && has(why, "forme d'une adresse"), "un courriel sans forme d'adresse : refuse (" + why + ")");
        check(app::licence::loadProfile().email == "camille.essai@exemple.fr", "le profil d'avant reste");
        app::licence::Profile empty;
        check(app::licence::saveProfile(empty, &why) && app::licence::loadProfile().empty(), "tout vide : permis (facultatif)");
        // L'apercu : la capture et la description livrees avec le logiciel.
        const fs::path dir = base / "fonctions";
        fs::create_directories(dir, ec);
        {
            std::ofstream(dir / "api_editor.png", std::ios::binary) << "PNG d'essai";
            std::ofstream(dir / "descriptions.json", std::ios::binary)
                << "{\"format\": \"xpg-fonctions-apercu/1\", \"fonctions\": {\"api_editor\": \"L'\xC3\xA9" "diteur du programme, en deux lignes.\"}}";
        }
        app::licence::setFeatureImagesFolder(dir.string());
        const auto pv = app::licence::featurePreview("api_editor");
        check(!pv.imagePath.empty() && pv.imagePath.find("api_editor.png") != std::string::npos && pv.description == "L'\xC3\xA9" "diteur du programme, en deux lignes.",
              "l'apercu : la capture et sa description");
        const auto none = app::licence::featurePreview("ai_generation");
        check(none.imagePath.empty() && none.description.empty(), "une fonctionnalite sans capture : rien (l'onglet le dit)");
        const auto evil = app::licence::featurePreview("../fonctions/api_editor");
        check(evil.imagePath.empty() && evil.description.empty(), "un identifiant qui serait un chemin : rien");
        app::licence::setFeatureImagesFolder({});
        unsetenv("XDG_CONFIG_HOME");
    }
#endif

    // ---- 1.38.0 : le registre - une reduction, les annonces ----
    std::printf("-- 1.38.0 : le registre (reduction, annonces)\n");
    {
        lic::Registry r;
        std::string why;
        r.number = 12;
        r.issuedAt = 1790000000;
        r.unlicensedEdition = "FREE";
        r.features.push_back({"api_editor", "API Studio", "\xC3\x89" "diteur", {}, false});
        r.features.push_back({"ai_generation", "IA", "G\xC3\xA9n\xC3\xA9ration", {}, false});
        {
            lic::EditionDef free;
            free.id = "FREE";
            free.label = "Libre";
            free.type = lic::LicenseType::Perpetual;
            free.features.push_back({"api_editor", {}});
            r.editions.push_back(free);
            lic::EditionDef pro;
            pro.id = "PROFESSIONAL";
            pro.label = "Pro";
            pro.maxDevices = 5;
            pro.durationDays = 365;
            pro.price = "490 \xE2\x82\xAC HT / an";
            pro.features.push_back({"ai_generation", {}});
            pro.features.push_back({"api_editor", {}});
            r.editions.push_back(pro);
        }
        auto& e = r.editions[1];
        e.promoPrice = "390 \xE2\x82\xAC HT / an";
        e.promoUntil = 1795000000;
        const lic::Announcement ann{"R12-" + e.id, "R\xC3\xA9" "duction", "Au lieu de 490.", e.id, 1790000000, 1795000000};
        r.announcements.push_back(ann);
        const std::string out = lic::writeRegistry(r);
        lic::Registry back;
        check(lic::parseRegistry(out, back, &why) && back.editions[1].promoPrice == e.promoPrice && back.editions[1].promoUntil == e.promoUntil &&
                  back.announcements.size() == 1 && back.announcements[0] == ann,
              "ecrit puis relu : le prix reduit, sa fin, l'annonce (" + why + ")");
        check(lic::writeRegistry(back) == out, "le meme texte (l'ordre fixe)");
        const auto refused = [&](std::string t, const char* what) {
            lic::Registry x;
            std::string w;
            check(!lic::parseRegistry(t, x, &w), std::string(what) + " : refuse (" + w + ")");
        };
        {
            std::string t = out;
            const auto p = t.find("\"promo_until\"");
            t.erase(p, t.find('\n', p) - p + 1);
            const auto q = t.rfind("\"promo_price\"", p);
            const auto lineEnd = t.find('\n', q);
            if (t[lineEnd - 1] == ',') t.erase(lineEnd - 1, 1);   // le prix reduit, sans sa fin
            refused(t, "un prix reduit sans fin");
        }
        {
            std::string t = out;
            const auto p = t.find("\"R12-");
            t.replace(p + 1, 4, "R 12");
            refused(t, "un identifiant d'annonce avec une espace");
        }
        {
            std::string t = out;
            const auto p = t.find("\"edition\": \"" + e.id + "\"");
            t.replace(p, 13 + e.id.size(), "\"edition\": \"NULLE_PART\"");
            refused(t, "une annonce d'une offre absente");
        }
    }

    // ---- 1.38.0 : les notifications des offres, des annonces, de la licence ----
    std::printf("-- 1.38.0 : les notifications (avant / apres)\n");
    {
        namespace w = app::licence::watch;
        const std::int64_t now = 1790000000;
        w::Seen before;
        before.known = true;
        before.registry = 5;
        before.names = {{"a", "Analyse"}, {"b", "Biblioth\xC3\xA8que"}, {"c", "Captures"}, {"d", "D\xC3\xA9ploiement"}};
        before.offers.push_back({"PRO", "Pro", "490 \xE2\x82\xAC", "", {"a", "b", "c"}, 0, false});
        before.offers.push_back({"OLD", "Ancienne", "", "", {"a"}, 0, false});
        before.licenseId = "XPG-2026-00042";
        before.edition = "PRO";
        before.editionLabel = "Pro";
        before.expiresAt = now + 100 * 86400;
        before.status = static_cast<int>(lic::LicenseStatus::Valid);
        w::Seen after = before;
        after.registry = 6;
        after.offers[0].features = {"a", "b", "d"};
        after.offers[1].retired = true;
        after.offers.push_back({"TEAM", "\xC3\x89quipe", "990 \xE2\x82\xAC", "", {"a", "b", "c", "d"}, 0, false});
        after.current.emplace_back("A1", "R\xC3\xA9" "duction Pro", "Jusqu'au 30.", "PRO", now + 86400);
        after.current.emplace_back("A0", "Finie", "", "", now - 10);
        const auto items = w::compare(before, after, now);
        const auto find = [&](const std::string& word) -> const app::notices::Item* {
            for (const auto& n : items)
                if (has(n.title, word)) return &n;
            return nullptr;
        };
        const auto* changed = find("modifi");
        // 1.39.0 : un message court et precis, un tableau (les changements), la fenetre de l'avant / l'apres
        check(changed && has(changed->title, "Pro") && has(changed->title, "ta licence") &&
                  has(changed->detail, "1 fonctionnalit\xC3\xA9 retir\xC3\xA9" "e : Captures") && has(changed->detail, "1 ajout\xC3\xA9" "e : D\xC3\xA9ploiement") &&
                  !has(changed->detail, "Avant :") && changed->tone == "warning" && changed->button == "Voir le d\xC3\xA9tail" &&
                  changed->action == app::notices::toDetail("offre:PRO:6"),
              "1.39.0 : une offre modifiee - le message court (" + (changed ? changed->detail : std::string()) + ")");
        {
            core::json::Value d;
            const bool ok = changed && core::json::parse(changed->data, d, nullptr) && d.isObject();
            const auto& t = ok ? d.at("tableau") : d;
            const auto& rows = t.at("lignes");
            check(ok && t.at("colonnes").items().size() == 4 && rows.items().size() == 2 && rows.items()[0].str("ton") == "error" &&
                      rows.items()[0].at("cellules").items()[0].asString() == "Captures" && rows.items()[1].str("ton") == "ok" &&
                      rows.items()[1].at("cellules").items()[0].asString() == "D\xC3\xA9ploiement",
                  "1.39.0 : son tableau - Captures retiree (rouge), Deploiement ajoutee (vert)");
            const auto ch = changed ? w::offerChangeOf(changed->data) : std::nullopt;
            check(ch && ch->id == "PRO" && ch->mine && ch->registry == 6 && ch->rows.size() == 4 && ch->rows[0].id == "c" && ch->rows[0].before &&
                      !ch->rows[0].after && ch->rows[1].id == "d" && !ch->rows[1].before && ch->rows[1].after && ch->rows[2].before && ch->rows[2].after &&
                      ch->values.empty(),
                  "1.39.0 : sa fenetre - toute l'offre, les changements d'abord");
            check(!w::offerChangeOf("") && !w::offerChangeOf("{}") && !w::offerChangeOf("pas du json"), "1.39.0 : des donnees vides ou fausses : pas de fenetre");
        }
        {   // 1.39.0 : le prix, les PC, la duree qui changent (sans fonctionnalite changee) : dits aussi
            w::Seen b2 = before, a2 = before;
            b2.offers[0].devices = 5;
            b2.offers[0].days = 365;
            a2.registry = 6;
            a2.offers[0].price = "590 \xE2\x82\xAC";
            a2.offers[0].devices = 10;
            a2.offers[0].days = 0;
            const auto it = w::compare(b2, a2, now);
            const auto ch = it.empty() ? std::nullopt : w::offerChangeOf(it[0].data);
            check(it.size() == 1 && has(it[0].detail, "Prix : 490") && has(it[0].detail, "PC au plus : 5") && has(it[0].detail, "sans fin") && ch &&
                      ch->values.size() == 3 && ch->values[1].before == "5" && ch->values[1].after == "10",
                  "1.39.0 : le prix, les PC, la duree (" + (it.empty() ? std::string() : it[0].detail) + ")");
            w::Seen b3 = before, a3 = before;   // une valeur pas retenue (un fichier de la 1.38) : rien n'est invente
            a3.registry = 6;
            a3.offers[0].devices = 10;
            check(w::compare(b3, a3, now).empty(), "1.39.0 : les PC pas retenus avant : rien a dire");
        }
        check(find("Nouvelle offre") != nullptr && find("retir\xC3\xA9" "e : Ancienne") != nullptr, "une offre nouvelle, une offre retiree");
        const auto* ann = find("R\xC3\xA9" "duction Pro");
        check(ann && ann->category == "Annonce" && ann->action == app::notices::toLicence(app::licence::kTabRequest) && find("Finie") == nullptr,
              "l'annonce en cours (son bouton : les offres), pas celle qui est finie");
        check(after.announcements.size() == 2, "les deux annonces sont retenues comme vues");
        check(items.size() == 4, "rien d'autre (" + std::to_string(items.size()) + ")");
        w::Seen again = after;
        check(w::compare(after, again, now).empty(), "rien de neuf : aucune notification (une annonce une seule fois)");
        // la licence
        w::Seen lic2 = after;
        lic2.expiresAt = now + 465 * 86400;
        auto l1 = w::compare(after, lic2, now);
        check(l1.size() == 1 && has(l1[0].title, "renouvel") && l1[0].tone == "ok", "la licence renouvelee");
        w::Seen lic3 = lic2;
        lic3.status = static_cast<int>(lic::LicenseStatus::Revoked);
        auto l2 = w::compare(lic2, lic3, now);
        check(l2.size() == 1 && has(l2[0].title, "r\xC3\xA9voqu") && l2[0].tone == "error", "la licence revoquee");
        w::Seen soon = after;
        soon.expiresAt = now + 5 * 86400;
        w::Seen soonBefore = soon;
        auto l3 = w::compare(soonBefore, soon, now);
        check(l3.size() == 1 && has(l3[0].title, "finit le") && soon.remindedEnd == soon.expiresAt, "bientot finie : dite une fois");
        w::Seen soon2 = soon;
        check(w::compare(soon, soon2, now + 3600).empty(), "... pas deux");
        w::Seen first;   // le premier lancement : rien des offres ni de la licence
        w::Seen now1 = after;
        auto f = w::compare(first, now1, now);
        check(f.size() == 1 && f[0].category == "Annonce", "le premier lancement : seulement l'annonce en cours");
        // garde sur le disque
        const std::string file = U(base / "vu-licence.json");
        check(w::saveSeen(file, after), "l'etat vu s'ecrit");
        w::Seen read;
        check(w::loadSeen(file, read) && read.registry == 6 && read.offers.size() == after.offers.size() && read.offers[0] == after.offers[0] &&
                  read.products == after.products &&
                  read.announcements == after.announcements && read.licenseId == after.licenseId && read.expiresAt == after.expiresAt,
              "et se relit tel quel");
    }

    // ---- 1.38.0 : le carnet general (un second journal, le meme mecanisme) ----
    std::printf("-- 1.38.0 : le carnet des notifications generales\n");
    {
        app::ai::notifs::resetForTests();
        app::notices::resetForTests();
        const auto id1 = app::notices::post({0, 0, "Licence", "Licence install\xC3\xA9" "e", "jusqu'au 31/12/2027", "Voir ma licence", "licence:1", "ok", "k1"});
        (void)app::notices::post({0, 0, "Offres", "Offre modifi\xC3\xA9" "e", "Avant / apr\xC3\xA8s", "", "", "info", ""});
        check(app::notices::size() == 2 && app::notices::unread() == 2 && app::ai::notifs::size() == 0, "deux notifications generales ; l'Assistant n'en voit aucune");
        (void)app::notices::post({0, 0, "Licence", "Licence install\xC3\xA9" "e", "encore", "", "", "ok", "k1"});
        check(app::notices::size() == 2 && app::notices::find(id1) && app::notices::find(id1)->count == 2, "la meme cle remplace (x2)");
        check(app::notices::markRead(id1) && app::notices::unread() == 1 && app::notices::clearRead() == 1 && app::notices::size() == 1, "lue, effacee");
        const std::string file = U(base / "notifications.json");
        app::notices::open(file);
        app::notices::Item withData{0, 0, "Annonce", "R\xC3\xA9" "duction", "jusqu'au 30", "", "", "info", "annonce:A1"};
        withData.data = "{\"tableau\":{\"colonnes\":[\"A\",\"B\"],\"lignes\":[{\"cellules\":[\"x\",\"y\"],\"ton\":\"ok\"}]}}";
        (void)app::notices::post(withData);
        check(app::notices::waitIdle(5000) && fs::exists(fs::path(file)), "ecrit dans son fichier");
        app::notices::close();
        app::notices::resetForTests();
        app::notices::open(file);
        check(app::notices::waitIdle(5000) && app::notices::size() == 2, "relu a la reouverture (" + std::to_string(app::notices::size()) + ")");
        {
            bool kept = false;
            for (const auto& n : app::notices::list())
                if (n.key == "annonce:A1" && has(n.data, "\"colonnes\"") && has(n.data, "\"ok\"")) kept = true;
            check(kept, "1.39.0 : ses donnees (le tableau) relues aussi");
        }
        app::notices::close();
        app::notices::resetForTests();
        const auto t = app::notices::parseAction("licence:7");
        check(t.kind == "licence" && t.tab == 7 && app::notices::parseAction("licence:-1").tab == -1 && app::notices::parseAction("conv:x").kind.empty(),
              "le bouton : licence:<onglet>");
        const auto dt = app::notices::parseAction(app::notices::toDetail("offre:PRO:6"));
        check(dt.kind == "detail" && dt.key == "offre:PRO:6" && app::notices::parseAction("detail:").kind.empty(), "1.39.0 : le bouton : detail:<cle>");
    }

    // ---- 1.44.0 : le bug << les notifications reapparaissent apres Tout marquer comme lu, apres fermeture / ouverture
    //      d'un projet >> : le MEME evenement poste de nouveau (au lancement, a la reouverture) reste lu ; et les avis de la
    //      cloche lus le restent pour ce compte (une barre neuve, un autre lancement), plus seulement pour la barre.
    std::printf("-- 1.44.0 : une notification lue le reste (le meme evenement poste de nouveau ; la cloche)\n");
    {
        app::notices::resetForTests();
        const std::string file = U(base / "notifications-144.json");
        app::notices::open(file);
        const app::notices::Item inst{0, 0, "Licence", "Licence install\xC3\xA9" "e : ENTERPRISE", "Licence XPG-2026-00009, sans fin.", "Voir ma licence",
                                      "licence:1", "ok", "licence:installee:XPG-2026-00009:0"};
        const app::notices::Item promo{0, 0, "Offres", "R\xC3\xA9" "duction sur ENTERPRISE : 10 \xE2\x82\xAC", "jusqu'au 30/11", "", "", "info", "promo:ENTERPRISE:10"};
        const auto id1 = app::notices::post(inst);
        const auto id2 = app::notices::post(promo);
        check(app::notices::markAllRead() == 2 && app::notices::unread() == 0, "Tout marquer comme lu : 0 non lue");
        // la reouverture d'un projet (ou un nouveau lancement) poste les memes evenements de nouveau
        (void)app::notices::post(inst);
        (void)app::notices::post(promo);
        check(app::notices::unread() == 0 && app::notices::size() == 2, "les memes evenements postes de nouveau : toujours lus, pas en double (" +
                                                                              std::to_string(app::notices::unread()) + " non lues)");
        check(app::notices::find(id1) && app::notices::find(id1)->count == 1 && app::notices::find(id2)->read, "ni x2, ni non lue");
        // un evenement nouveau sous la meme cle : non lu (et x2)
        app::notices::Item changed = promo;
        changed.title = "R\xC3\xA9" "duction sur ENTERPRISE : 20 \xE2\x82\xAC";
        (void)app::notices::post(changed);
        check(app::notices::unread() == 1 && app::notices::find(id2)->count == 2, "un titre nouveau sous la meme cle : un evenement nouveau, non lu");
        check(app::notices::markRead(id2) && app::notices::waitIdle(5000), "lue, ecrite");
        // l'application relancee : l'etat lu relu du fichier, et les memes evenements postes encore : lus
        app::notices::close();
        app::notices::resetForTests();
        app::notices::open(file);
        check(app::notices::waitIdle(5000) && app::notices::size() == 2 && app::notices::unread() == 0, "relancee : relues, toujours lues");
        (void)app::notices::post(inst);
        check(app::notices::unread() == 0, "relancee, le meme evenement poste encore : toujours lu");
        app::notices::close();
        app::notices::resetForTests();

        // la cloche de la barre du haut : lu pour ce compte (pas pour une barre, ni un lancement)
        app::bgtasks::resetReadForTests();
        const std::string lu = U(base / "cloche-lue.txt");
        app::bgtasks::setReadFile(lu);
        std::vector<std::string> hooked;
        app::bgtasks::setReadHook([&hooked](const std::string& k) { hooked.push_back(k); });
        const std::string title = "Ta licence XPG-2026-00009 est install\xC3\xA9" "e (re\xC3\xA7ue de l'\xC3\xA9" "diteur)";
        check(!app::bgtasks::isRead("licence:installee", title), "un avis de la cloche : non lu");
        app::bgtasks::markRead("licence:installee", title);
        app::bgtasks::markRead("notification:7", "Licence r\xC3\xA9voqu\xC3\xA9" "e");
        check(app::bgtasks::isRead("licence:installee", title) && hooked.size() == 2 && hooked[1] == "notification:7",
              "lu ; une notification generale est lue aussi dans son carnet (le crochet)");
        check(!app::bgtasks::isRead("licence:installee", "Une autre licence est install\xC3\xA9" "e"), "un autre titre sous la meme cle : non lu");
        app::bgtasks::resetReadForTests();   // l'application relancee
        app::bgtasks::setReadFile(lu);
        check(app::bgtasks::isRead("licence:installee", title), "relancee : l'avis de la cloche est toujours lu (cloche-lue.txt)");
        app::bgtasks::resetReadForTests();
    }

    // ---- 1.38.0 : les PC d'une licence en ligne (Mes appareils) ----
    std::printf("-- 1.38.0 : les PC d'une licence en ligne\n");
    {
        namespace ls = app::licence::service;
        const std::string k1(64, 'a'), k2(64, 'b');
        const std::string postes = "{\"XPG-2026-00042\": {\"max\": 20, \"type\": \"temporary\", \"utilises\": 2, \"total\": 3, \"liste\": ["
                                   "{\"nom\": \"PC-ATELIER-1\", \"cle\": \"" + k1 + "\", \"activee\": \"2026-11-02T08:14:03Z\", \"vue\": \"2026-11-08T13:05:41Z\", "
                                   "\"jusqu_a\": \"2026-12-08T13:05:41Z\", \"etat\": \"active\", \"ce_pc\": true},"
                                   "{\"nom\": \"\", \"cle\": \"" + k2 + "\", \"activee\": \"2026-10-20T09:00:00Z\", \"vue\": \"2026-11-01T12:00:00Z\", "
                                   "\"jusqu_a\": \"2026-11-20T09:00:00Z\", \"etat\": \"liberee\", \"ce_pc\": false}]}}";
        std::vector<ls::OnlineDevices> all;
        check(ls::parseDevices(postes, 1790000000, all) && all.size() == 1 && all[0].licenseId == "XPG-2026-00042" && all[0].max == 20 && all[0].used == 2 &&
                  all[0].total == 3 && all[0].list.size() == 2 && all[0].list[0].thisPc && all[0].list[0].name == "PC-ATELIER-1" &&
                  all[0].list[1].state == "liberee" && all[0].list[1].until > 0,
              "le champ postes se lit : ce PC d'abord, << 2 sur 20 >>, une place gardee");
        check(ls::shortKey(k1) == "AAAAAA\xE2\x80\xA6" "AAAAAA", "la cle courte");
        check(!ls::parseDevices("[1,2]", 0, all), "pas un objet : refuse");
    }

    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
