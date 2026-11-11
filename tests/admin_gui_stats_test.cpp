// =============================================================================
//  tests/admin_gui_stats_test.cpp - 1.47.0 : LA PAGE << Statistiques >> (planches
//  08 et 09), LA FICHE D'UN CLIENT ET SES COURBES (planche 10), LA RANGEE DE
//  COURBES DU TABLEAU DE BORD - dessinees A BLANC (sans SDL)
// -----------------------------------------------------------------------------
//  LES REPONSES SONT DES FAUX : statskit::sampleFetchers (StatsSamples.cpp) ecrit
//  en JSON, comme le service 1.47.0, des chiffres fictifs et deterministes (Camille,
//  Lea, Hugo, Nina Essai ; Atelier Exemple...), lus par les VRAIS lecteurs
//  (stats::parseSeries, parseUpdates, parseAdmins) et donnes a la page par le
//  crochet des essais (statskit::setFetchersForTests). Des reponses ecrites a la
//  main pour les cas limites : des points vides, une mesure refusee (un droit
//  manque), un refus du service. Puis, s'il se lance sur ce poste, LE VRAI
//  SERVICE LOCAL (tests/service_local.hpp : wrangler, 127.0.0.1) pour le chemin
//  du chargement (sans crochet : Workspace::statsSeries).
//  Ce qui est controle : chaque carte de la planche 08 (titre, graphique rempli,
//  la bulle), les onglets, la periode (7 j, 30 j, 90 j, 12 mois, tout, deux dates),
//  la comparaison (la periode d'avant, l'an dernier, rien), le pas (jour, semaine,
//  mois), la note des chiffres reconstitues, un service 1.46.0 (UN avertissement,
//  << Deployer le service >>), des donnees vides, un clic sur un point (la liste
//  ouverte et filtree : avec la vraie coquille, la barre de filtres de Commandes),
//  Relever maintenant, Exporter, les Mises a jour (tuiles, adoption, versions,
//  PC pas a jour), les Admins (leurs heures a l'heure du PC), la fiche d'un client
//  (ses courbes, ses onglets), le tableau de bord (la rangee, la place du globe),
//  la navigation (Statistiques sous Pilotage, la sous-page << client >>).
// =============================================================================
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/core/Json.hpp"
#include "../src/license_admin/Stats.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/license_admin/gui/StatsKit.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/Charts.hpp"
#include "../src/ui/widgets/DatePicker.hpp"
#include "../src/ui/widgets/FilterBar.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace admintest;
namespace stats = license_admin::stats;
namespace sk = license_admin::gui::statskit;
namespace ch = ui::charts;

namespace {

std::string draw(ui::Widget& w, float width = 1141.f, float height = 813.f) {
    TextRenderer r;
    r.size = {width, height};
    std::string t = paint(w, r, width, height);
    if (std::getenv("XPG_ESSAI_MONTRER")) std::printf("----\n%s\n", t.c_str());
    return t;
}
std::size_t count(const std::string& s, const std::string& part) {
    std::size_t n = 0;
    for (std::size_t at = s.find(part); at != std::string::npos; at = s.find(part, at + part.size())) ++n;
    return n;
}
bool visibleW(ui::Widget& root, std::string_view id) {
    ui::Widget* w = root.findById(id);
    return w && w->visible();
}
ch::TimeChart* tchart(ui::Widget& root, const std::string& id) { return find<ch::TimeChart>(root, id); }
bool hasValues(const ch::TimeChart* c) {
    if (!c) return false;
    for (const auto& s : c->data().series)
        for (const double v : s.values)
            if (!ch::isNone(v)) return true;
    return false;
}

// Les fausses reponses, sauf ce que `edit` change dans le JSON de la serie (un refus, des points vides).
sk::Fetchers fakes(std::function<void(core::json::Value&)> edit = {}) {
    sk::Fetchers f = sk::sampleFetchers([] { return g_now; });
    if (edit)
        f.series = [edit](const stats::SeriesQuery& q, stats::SeriesReply& out) {
            core::json::Value v;
            std::string why;
            if (!core::json::parse(sk::sampleSeriesJson(q, g_now), v, &why)) return gui::OpResult::fail(why);
            edit(v);
            if (!stats::parseSeries(v, out, &why)) return gui::OpResult::fail(why);
            return gui::OpResult::done({});
        };
    return f;
}

// La coquille entiere (pour un clic qui ouvre une autre page).
struct ShellRig {
    menu::MenuManager menus{menu::MenuFactory{}};
    gui::Shell*       shell{nullptr};
    TextRenderer      r;
    explicit ShellRig(const fs::path& folder) {
        auto owned = std::make_unique<gui::Shell>(U(folder), [] { return g_now; });
        shell = owned.get();
        menus.PushMenu(std::move(owned));
        menus.applyPending();
    }
    std::string paint(float w = 1373.f, float h = 917.f) {
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
    ui::Widget& root() { return *shell->widgetRoot(); }
};

const std::vector<std::string> kOverviewTitles = {
    "Montant encaiss\xC3\xA9", "Cumul\xC3\xA9 sur l'ann\xC3\xA9" "e", "Commandes", "Panier moyen", "Ventes par offre", "Options vendues", "Licences vendues",
    "Remboursements", "Licences actives", "Mouvements des licences", "\xC3\x89" "ch\xC3\xA9" "ances", "Demandes", "D\xC3\xA9lai de r\xC3\xA9ponse", "PC actifs",
    "Mouvements des PC", "Flottantes", "D\xC3\xA9" "couvertes"};
const std::vector<std::string> kOverviewCharts = {"montant", "cumul", "commandes", "panier", "offres", "vendues", "remboursements", "actives", "mouvements",
                                                  "echeances", "demandes", "delai", "pc", "mouvements-pc", "flottantes", "decouvertes"};

// LE TEXTE COUPE (<< le texte sort du cadre >>) : un renderer qui mesure comme la police de l'Admin (Inter : ~0,58 de la
// taille par caractere, plus large que le harnais), et les lignes dessinees qui finissent par une ellipse - hors des
// libelles qui en ont une par nature (<< Exporter... >>, << Deux dates... >>, << Lecture des chiffres... >>).
class WideRenderer final : public gfx::IRenderer {
public:
    static constexpr float kFactor = 0.52f;   // Inter mesure : 0,39 a 0,52 de la taille par caractere (les capitales), le demi-gras ~0,47
    void beginFrame(gfx::Color) override {
        lines.clear();
        rects.clear();
    }
    void endFrame() override {}
    void pushClip(const gfx::Rect& r) override { clips_.push_back(clips_.empty() ? r : clips_.back().intersect(r)); }
    void popClip() override {
        if (!clips_.empty()) clips_.pop_back();
    }
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void drawText(gfx::Point p, std::string_view s, gfx::FontId f, gfx::Color) override {
        if (!clips_.empty() && clips_.back().empty()) return;
        lines.emplace_back(s);
        const gfx::TextMetrics m = measure(s, f);
        rects.push_back({p.x, p.y, m.width, m.height});
    }
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return {static_cast<float>(TextRenderer::chars(s)) * h * kFactor, h, h * 0.8f, h * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return f.v ? static_cast<float>(f.v) : 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float w) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return std::min(static_cast<std::size_t>(std::max(0.f, w / (h * kFactor))), s.size());
    }
    std::vector<std::string> lines;
    std::vector<gfx::Rect>   rects;   // la place de chaque ligne dessinee (les chevauchements)
    gfx::Size                size{1280.f, 800.f};

private:
    std::vector<gfx::Rect> clips_;
};
void installWideMetrics() {
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return static_cast<float>(TextRenderer::chars(t)) * (f.v ? static_cast<float>(f.v) : 16.f) * WideRenderer::kFactor; };
    s.lineHeight = [](gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 16.f; };
    s.clipboardGet = [] { return std::string(); };
    s.clipboardSet = [](std::string_view) {};
    s.surfaceSize = [] { return gfx::Size{1280.f, 800.f}; };
    ui::installPlatformServices(std::move(s));
}
// Les lignes coupees d'un dessin (vide : aucune).
std::string cutLines(ui::Widget& w, float width, float height) {
    static const ui::Theme theme = ui::Theme::byName("Light");
    WideRenderer r;
    r.size = {width, height};
    for (int pass = 0; pass < 2; ++pass) {   // deux passes : les cartes choisissent leurs textes a la mise en page
        w.setBounds({0.f, 0.f, width, height});
        w.layout();
        r.beginFrame(theme.color.windowBg);
        ui::PaintContext ctx{r, theme, w.bounds(), 0.0, nullptr};
        w.render(ctx);
        r.endFrame();
    }
    static const char* const kAllowed[] = {"Exporter\xE2\x80\xA6", "Deux dates\xE2\x80\xA6", "Lecture des chiffres\xE2\x80\xA6", "Lecture des versions\xE2\x80\xA6",
                                           "Nouvelle licence\xE2\x80\xA6", "Prolonger\xE2\x80\xA6", "Chercher\xE2\x80\xA6", "Relev\xC3\xA9 en cours\xE2\x80\xA6"};
    std::string out;
    for (const auto& l : r.lines) {
        if (l.find("\xE2\x80\xA6") == std::string::npos) continue;
        bool ok = false;
        for (const char* a : kAllowed) ok = ok || l == a;
        if (!ok) out += "[" + l + "] ";
    }
    return out;
}

// Les textes qui se chevauchent dans le cadre du widget `id` (vide : aucun) - dessines comme cutLines.
std::string overlapLines(ui::Widget& w, const std::string& id, float width, float height) {
    static const ui::Theme theme = ui::Theme::byName("Light");
    WideRenderer r;
    r.size = {width, height};
    for (int pass = 0; pass < 2; ++pass) {
        w.setBounds({0.f, 0.f, width, height});
        w.layout();
        r.beginFrame(theme.color.windowBg);
        ui::PaintContext ctx{r, theme, w.bounds(), 0.0, nullptr};
        w.render(ctx);
        r.endFrame();
    }
    const ui::Widget* part = w.findById(id);
    if (!part) return "[" + id + " introuvable]";
    const gfx::Rect area = part->bounds();
    std::vector<std::size_t> in;
    for (std::size_t i = 0; i < r.rects.size(); ++i) {
        const gfx::Rect& t = r.rects[i];
        if (t.w > 0.f && t.x >= area.x - 1.f && t.y >= area.y - 1.f && t.right() <= area.right() + 1.f && t.bottom() <= area.bottom() + 1.f) in.push_back(i);
    }
    std::string out;
    for (std::size_t a = 0; a < in.size(); ++a)
        for (std::size_t b = a + 1; b < in.size(); ++b) {
            const gfx::Rect &p = r.rects[in[a]], &q = r.rects[in[b]];
            const float ox = std::min(p.right(), q.right()) - std::max(p.x, q.x), oy = std::min(p.bottom(), q.bottom()) - std::max(p.y, q.y);
            if (ox > 1.f && oy > 2.f) out += "[" + r.lines[in[a]] + " / " + r.lines[in[b]] + "] ";
        }
    return out;
}

// LES CAPTURES (XPG_STATS_DOSSIER=<dossier>) : un poste Admin d'essai plus fourni, garde dans ce dossier - des licences
// FICTIVES signees a des dates passees (l'horloge des essais reculee), pour que la fiche d'<< Atelier Exemple >> ressemble a
// la planche 10 : la PROFESSIONAL du 12/03/2024 prolongee deux fois, la PERSONAL du 05/12/2024 (finit dans un mois), une
// PERSONAL expiree, une PROFESSIONAL neuve ; d'autres clients d'essai. Une cle d'ESSAI. Rien de reel.
void captureFolder(const std::string& source, const std::string& dir) {
    Fixture cf(source, "stats-captures");
    if (!cf.ready()) {
        std::printf("  (le dossier des captures : poste pas pret)\n");
        return;
    }
    const lic::Seconds keep = g_now;
    const auto at = [](const char* day) { return stats::dayStart(day) + 9 * 3600 + 20 * 60; };
    int pc = 0;
    std::map<std::string, int> next;
    const auto pcs = [&](const std::string& prefix, int n) {
        std::vector<std::string> out;
        for (int i = 0; i < n; ++i) {
            ++pc;
            out.push_back(cf.request("pc" + std::to_string(pc), prefix + std::to_string(++next[prefix])));
        }
        return out;
    };
    const auto extend = [&](const std::string& id, const char* until) {
        const gui::LicenceEntry* e = cf.ws->licence(id);
        if (!e) return;
        gui::LicenceDraft d;
        d.licenseId = id;
        d.customer = e->licence.customer;
        d.edition = e->licence.edition;
        d.extend = true;
        d.expiresAt = stats::dayStart(until) + 12 * 3600;
        std::error_code ec;
        fs::create_directories(cf.root / "sortie", ec);
        const gui::OpResult r = cf.ws->signLicence(d, U(cf.root / "sortie" / (id + "-p.xpglic")));
        if (!r.ok) std::printf("  (prolonger %s : %s)\n", id.c_str(), r.text().c_str());
    };
    const auto sign = [&](const char* day, const std::string& id, const std::string& customer, const std::string& edition, std::vector<std::string> reqs) {
        g_now = at(day);
        const gui::OpResult r = cf.licence(id, customer, edition, reqs);
        if (!r.ok) std::printf("  (%s : %s)\n", id.c_str(), r.text().c_str());
    };
    sign("2024-01-22", "XPG-2024-00012", "Soci\xC3\xA9t\xC3\xA9 d'essai", "ENTERPRISE", pcs("ESSAI-POSTE-", 4));
    sign("2024-03-12", "XPG-2024-00031", "Atelier Exemple", "PROFESSIONAL", pcs("ATELIER-PC", 5));
    sign("2024-06-19", "XPG-2024-00102", "Atelier Exemple", "PERSONAL", {cf.request("pc-ae-old", "PORTABLE-JULIE")});
    sign("2024-09-03", "XPG-2024-00140", "Usine Exemple Nord", "PROFESSIONAL", pcs("NORD-LIGNE-0", 3));
    sign("2024-12-05", "XPG-2025-00188", "Atelier Exemple", "PERSONAL", pcs("ATELIER-PC", 2));
    g_now = at("2025-03-11");
    extend("XPG-2024-00031", "2026-03-11");
    sign("2025-05-20", "XPG-2025-00233", "Bureau d'\xC3\xA9tudes Exemple", "PERSONAL", pcs("BE-CALCUL-0", 2));
    sign("2025-10-02", "XPG-2025-00301", "Laboratoire Exemple", "PROFESSIONAL", pcs("LABO-MESURE-", 2));
    g_now = at("2025-12-05");
    extend("XPG-2025-00188", "2026-12-05");
    g_now = at("2026-03-11");
    extend("XPG-2024-00031", "2027-03-11");
    sign("2026-06-15", "XPG-2026-00377", "Garage d'essai", "PERSONAL", pcs("GARAGE-ATELIER-", 1));
    sign("2026-11-09", "XPG-2026-00412", "Atelier Exemple", "PROFESSIONAL", pcs("ATELIER-PC", 3));
    g_now = keep;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    fs::copy(cf.admin, dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    std::printf("  le dossier des captures : %s%s\n", dir.c_str(), ec ? (" (copie : " + ec.message() + ")").c_str() : "");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_stats_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();
    if (const char* dir = std::getenv("XPG_STATS_DOSSIER"); dir && *dir) {
        captureFolder(source, dir);
        return 0;
    }
    Fixture fx(source, "stats-gui");
    check(fx.ready(), "poste d'essai pret (cle + registre)");
    gui::Workspace& ws = *fx.ws;
    // des licences pour << Atelier Exemple >> (la fiche du client)
    check(fx.licence("XPG-2026-00031", "Atelier Exemple", "PROFESSIONAL", {fx.request("pc-a", "ATELIER-PC1"), fx.request("pc-b", "ATELIER-PC2")}).ok,
          "une licence d'essai pour Atelier Exemple (2 PC)");
    check(fx.licence("XPG-2026-00032", "Soci\xC3\xA9t\xC3\xA9 d'essai", "PROFESSIONAL", {fx.request("pc-c", "ESSAI-PC1")}).ok, "une licence pour Soci\xC3\xA9t\xC3\xA9 d'essai");
    ws.reload();
    HostRig rig(fx);
    sk::setFetchersForTests(fakes());   // DES FAUX : les reponses d'essai, lues par les vrais lecteurs

    // ------------------------------------------------------------------------------------------------ la navigation
    std::printf("-- la navigation : Statistiques sous Pilotage, la fiche d'un client\n");
    {
        const auto& e = gui::Shell::entries();
        check(e.size() > 2 && std::string(e[0].id) == "tableau" && std::string(e[1].id) == "statistiques" && std::string(e[1].label) == "Statistiques",
              "Statistiques : la deuxieme entree du menu, sous le Tableau de bord (Pilotage)");
        check(gui::Shell::entry("client") && gui::Shell::parentOf("client") == "clients", "la fiche d'un client : une sous-page de Clients");
        check(!gui::Shell::plusOnly("statistiques"), "Statistiques : pour l'admin+ et les admins");
    }

    // ------------------------------------------------------------------------------------------------ la vue generale
    std::printf("-- Statistiques, la vue generale (planche 08) : chaque carte\n");
    auto page = gui::makeStatsPage(rig.host());
    {
        page->refresh();
        const std::string t = draw(*page);
        check(has(t, "Statistiques"), "le titre");
        for (const char* tab : {"Vue g\xC3\xA9n\xC3\xA9rale", "Ventes", "Licences", "PC", "Mises \xC3\xA0 jour", "Demandes", "Admins", "Globe"})
            check(has(t, tab), std::string("l'onglet ") + tab);
        for (const auto& title : kOverviewTitles) check(has(t, title), "la carte " + title);
        check(has(t, "VENTES") && has(t, "LICENCES \xC2\xB7 DEMANDES") && has(t, "PC"), "les bandes des sections (Ventes, Licences \xC2\xB7 demandes, PC)");
        check(has(t, "compar\xC3\xA9 \xC3\xA0") && has(t, "la p\xC3\xA9riode d'avant") && has(t, "l'an dernier") && has(t, "semaine") && has(t, "Deux dates"),
              "la barre : periode, comparaison, pas");
        check(has(t, "13/08 \xE2\x86\x92 10/11/2026") || has(t, "09/08 \xE2\x86\x92 06/11/2026"), "la periode montree en tete (90 jours)");
        check(has(draw(*page, 1688.f, 976.f), "filtre la liste correspondante"), "a 1920 px : << Un clic sur un point filtre la liste correspondante >>");
        for (const auto& id : kOverviewCharts) check(hasValues(tchart(*page, "stats.vue." + id)), "le graphique " + id + " a des valeurs");
        // la liste des options (comme la planche : le libelle, le montant et les commandes, la barre dessous)
        check(has(t, "Exports avanc\xC3\xA9s") && has(t, "Travail \xC3\xA0 plusieurs") && has(t, "Imports avanc\xC3\xA9s"), "Options vendues : les trois options, entieres");
        check(has(draw(*page, 1688.f, 976.f), " cmd"), "Options vendues a 1920 px : le nombre de commandes de chacune");
        check(has(t, "Exports avanc\xC3\xA9s") && t.find("Exports avanc\xC3\xA9s") < t.find("Imports avanc\xC3\xA9s"), "la plus vendue d'abord");
        auto* m = tchart(*page, "stats.vue.montant");
        check(m && m->data().x.size() >= 13 && m->data().x.size() <= 15, "Montant encaisse : une colonne par semaine (" + std::to_string(m ? m->data().x.size() : 0) + ")");
        check(m && m->data().series.size() == 2 && m->data().series[1].role == ch::Role::Compare && has(m->data().series[1].name, "l'an dernier"),
              "Montant encaisse : compare a l'an dernier (en gris)");
        check(m && m->data().partialFrom == static_cast<int>(m->data().x.size()) - 1, "la semaine en cours : pale");
        if (m) {
            const ch::Tip tip = m->tipFor(static_cast<int>(m->data().x.size()) - 3, -1);
            check(has(tip.title, "Semaine du") && has(tip.text(), "commande") && has(tip.text(), "panier") && tip.click == "Clic : ces commandes",
                  "la bulle : la semaine, le montant, les commandes et le panier, << Clic : ces commandes >> (" + tip.text() + ")");
        }
        check(has(t, "contre ") && has(t, "\xE2\x82\xAC"), "les valeurs en euros et << contre ... >>");
        auto* off = tchart(*page, "stats.vue.offres");
        check(off && off->data().kind == ch::Kind::Stacked && off->data().series.size() == 4 && off->data().series[0].name == "PERSONAL" &&
                  off->data().series[3].name == "Options",
              "Ventes par offre : empilees, dans l'ordre fixe des offres");
        check(has(t, "gratuite"), "Ventes par offre : la Decouverte gratuite");
        auto* mv = tchart(*page, "stats.vue.mouvements");
        check(mv && mv->data().kind == ch::Kind::Diverging && mv->data().series.size() == 4 && mv->data().series[2].below, "Mouvements des licences : entrees et sorties");
        auto* ech = tchart(*page, "stats.vue.echeances");
        check(ech && ech->data().x.size() >= 13 && ech->data().series.size() == 2 && ech->data().series[0].name == "dans les 30 jours",
              "Echeances : les 90 jours a venir, les 30 premiers en avant");
        auto* fl = tchart(*page, "stats.vue.flottantes");
        check(fl && fl->data().x.size() == 90 && !fl->data().refs.empty(), "Flottantes : le pic de chaque jour, le plafond");
        check(has(t, "jetons"), "Flottantes : << 20 / 20 jetons >>");
        check(has(t, "converties"), "Decouvertes : le taux converti");
        check(has(t, "reconstitu\xC3\xA9s"), "la note des chiffres reconstitues (releves_depuis apres le debut de la periode)");
    }

    std::printf("-- la periode, la comparaison, le pas\n");
    {
        auto* m = tchart(*page, "stats.vue.montant");
        check(click(*page, "stats.periode.30j"), "30 j");
        (void)draw(*page);
        check(m && m->data().x.size() == 30, "30 j : une colonne par jour (" + std::to_string(m ? m->data().x.size() : 0) + ")");
        check(click(*page, "stats.pas.semaine"), "par semaine");
        (void)draw(*page);
        check(m && m->data().x.size() >= 5 && m->data().x.size() <= 6, "30 j par semaine : 5 ou 6 colonnes");
        check(click(*page, "stats.periode.7j"), "7 j");
        (void)draw(*page);
        check(m && m->data().x.size() == 7, "7 j : 7 jours");
        check(click(*page, "stats.periode.12mois"), "12 mois");
        std::string t = draw(*page);
        check(m && m->data().x.size() >= 12 && m->data().x.size() <= 13, "12 mois : par mois (" + std::to_string(m ? m->data().x.size() : 0) + ")");
        check(has(t, "par mois"), "<< par mois >> sur la carte");
        check(click(*page, "stats.pas.jour"), "12 mois par jour");
        (void)draw(*page);
        check(m && m->data().x.size() >= 365, "12 mois par jour : 365 points (moins de 400 : le service les donne)");
        check(click(*page, "stats.periode.tout"), "Tout");
        t = draw(*page);
        check(has(t, "06/11 \xE2\x86\x92 06/11/2026") || has(t, "06/11/2026"), "Tout : depuis la premiere licence de l'archive");
        check(click(*page, "stats.periode.dates"), "Deux dates...");
        (void)draw(*page);
        auto* du = find<ui::DatePicker>(*page, "stats.du");
        auto* au = find<ui::DatePicker>(*page, "stats.au");
        check(du && au && visibleW(*page, "stats.du"), "Deux dates : les deux champs");
        if (du && au) {
            du->pick(stats::dayStart("2026-07-01"));
            au->pick(stats::dayStart("2026-10-31"));
        }
        t = draw(*page);
        check(du && au && has(du->fieldText(), "01/07/2026") && has(au->fieldText(), "31/10/2026") && has(t, "01/07/2026") && has(t, "31/10/2026"),
              "Deux dates : du 01/07 au 31/10/2026 (dans les deux champs)");
        check(m && m->data().x.size() == 4, "Deux dates, par mois (le pas de << Tout >> garde) : 4 mois (" + std::to_string(m ? m->data().x.size() : 0) + ")");
        check(click(*page, "stats.pas.semaine"), "par semaine");
        (void)draw(*page);
        check(m && m->data().x.size() == 18, "Deux dates par semaine : 18 semaines (" + std::to_string(m ? m->data().x.size() : 0) + ")");
        check(click(*page, "stats.periode.90j"), "90 j");
        check(click(*page, "stats.comparer.avant"), "comparer a la periode d'avant");
        (void)draw(*page);
        check(m && m->data().series.size() == 2 && has(m->data().series[1].name, "la p\xC3\xA9riode d'avant"), "la comparaison : la periode d'avant");
        check(click(*page, "stats.comparer.rien"), "comparer a rien");
        t = draw(*page);
        check(m && m->data().series.size() == 1, "sans comparaison : une serie");
        check(click(*page, "stats.comparer.an"), "l'an dernier");
        (void)draw(*page);
        check(m && m->data().series.size() == 2, "l'an dernier : de nouveau deux series");
    }

    std::printf("-- un clic sur un point : la liste filtree\n");
    {
        auto* m = tchart(*page, "stats.vue.montant");
        rig.navigations.clear();
        if (m) m->pick(m->pickAt({0, 0}).index >= 0 ? m->pickAt({0, 0}) : ch::Pick{3, 0, "un", 0});
        check(!rig.navigations.empty() && rig.navigations.back() == "commandes", "Montant encaisse : Commandes");
        const auto& f = sk::lastOpened();
        check(f.periodField == "Date" && !f.from.empty() && !f.to.empty() && f.from <= f.to, "la periode de la colonne Date : la semaine du point (" + f.from + " - " + f.to + ")");
        auto* pc = tchart(*page, "stats.vue.mouvements-pc");
        if (pc) pc->pick({2, 0, "up:0", 1});
        check(rig.navigations.back() == "activations" && sk::lastOpened().periodField == "Activ\xC3\xA9" "e le", "Mouvements des PC : Activations, la colonne << Activee le >>");
        auto* d = tchart(*page, "stats.vue.demandes");
        if (d) d->pick({1, 0, "un", 1});
        check(rig.navigations.back() == "demandes", "Demandes : Demandes en ligne");
        auto* a = tchart(*page, "stats.vue.actives");
        if (a) a->pick({1, 0, "un", 1});
        check(rig.navigations.back() == "licences" && !sk::lastOpened().values.empty(), "Licences actives : Licences, l'etat Active");
    }

    std::printf("-- les onglets : Ventes, Licences, PC, Demandes, Admins\n");
    {
        check(click(*page, "stats.onglet.ventes"), "Ventes");
        std::string t = draw(*page);
        check(has(t, "Les meilleurs clients") && has(t, "Livr\xC3\xA9" "es par") && has(t, "3 admins"), "Ventes : les meilleurs clients, livrees par (3 admins)");
        auto* top = find<ch::BarList>(*page, "stats.ventes.clients");
        check(top && top->data().rows.size() == 8, "les 8 premiers clients");
        auto* liv = tchart(*page, "stats.ventes.livrees");
        check(liv && liv->data().series.size() >= 3 && liv->data().series[0].name == "Camille Essai" && liv->data().series[1].name == "L\xC3\xA9" "a Essai",
              "Livrees par : Camille puis Lea (l'ordre et les couleurs des admins)");
        check(click(*page, "stats.onglet.licences"), "Licences");
        t = draw(*page);
        check(has(t, "Licences actives par offre") && has(t, "Options ajout\xC3\xA9" "es") && has(t, "Sign\xC3\xA9" "es par"), "Licences : par offre, options, signees par");
        check(click(*page, "stats.onglet.pc"), "PC");
        t = draw(*page);
        check(has(t, "PC actifs") && has(t, "Mouvements des PC") && has(t, "Flottantes") && has(t, "D\xC3\xA9" "couvertes"), "PC : ses quatre cartes");
        check(click(*page, "stats.onglet.demandes"), "Demandes");
        t = draw(*page);
        check(has(t, "Trait\xC3\xA9" "es par") && has(t, "Accept\xC3\xA9" "es, refus\xC3\xA9" "es") && has(t, "Attente la plus longue"), "Demandes : traitees par, issue, l'attente la plus longue");
        check(click(*page, "stats.onglet.admins"), "Admins");
        t = draw(*page);
        check(has(t, "Camille Essai") && has(t, "L\xC3\xA9" "a Essai") && has(t, "Hugo Essai") && has(t, "Nina Essai"), "Admins : les quatre admins d'essai");
        check(has(t, "SES HEURES (HEURE DU PC)") && has(t, " h \xE2\x86\x92 "), "Admins : leurs heures habituelles, a l'heure du PC");
        check(click(*page, "stats.onglet.globe"), "Globe");
        (void)draw(*page);
        check(!visibleW(*page, "stats.periode") && !visibleW(*page, "stats.grille.vue"), "Globe : sa propre vue (pas la barre commune)");
        check(click(*page, "stats.onglet.vue"), "retour a la vue generale");
    }

    std::printf("-- Mises a jour (planche 09)\n");
    {
        check(click(*page, "stats.onglet.mises-a-jour"), "l'onglet");
        const std::string t = draw(*page);
        for (const char* s : {"PC \xC3\xA0 jour", "La version actuelle", "D\xC3\xA9lai m\xC3\xA9" "dian", "PC pas \xC3\xA0 jour", "Admins \xC3\xA0 jour",
                              "L'adoption de chaque version", "Les PC \xC3\xA0 jour", "D\xC3\xA9lai avant la mise \xC3\xA0 jour", "Les versions publi\xC3\xA9" "es"})
            check(has(t, s), std::string("Mises a jour : ") + s);
        check(has(t, "59\xE2\x80\xAF%") || has(t, "59 %"), "PC a jour : 59 % (170 sur 289)");
        check(has(t, "170 sur 289"), "170 sur 289");
        check(has(t, "1.47.0") && has(t, "actuelle") && has(t, "correctif"), "les versions : l'actuelle, un correctif");
        check(has(t, "ATELIER-PC3") && has(t, "1.43.2"), "PC pas a jour : ATELIER-PC3 en 1.43.2, le plus en retard");
        check(has(t, "Hugo Essai : Admin 1.45.2"), "Admins a jour : Hugo Essai en 1.45.2");
        check(has(t, "Align\xC3\xA9" "es sur leur jour de publication"), "la note de l'adoption");
        check(visibleW(*page, "stats.logiciel") && !visibleW(*page, "stats.comparer"), "la barre : << les PC >> (tous, Studios, Admins) a la place de la comparaison");
        auto* ad = tchart(*page, "stats.maj.adoption");
        check(ad && ad->data().series.size() == 4 && ad->data().x.size() >= 43 && ad->data().x[0].label == "J0", "l'adoption : 4 versions, alignees sur J0");
        auto* aj = tchart(*page, "stats.maj.ajour");
        check(aj && aj->data().series.size() == 2 && !aj->data().markers.empty(), "les PC a jour : Studios et Admins, les publications reperees");
        check(click(*page, "stats.logiciel.studios"), "les Studios");
        (void)draw(*page);
        check(aj && aj->data().series.size() == 1 && aj->data().series[0].name == "Studios", "les Studios seuls");
        check(click(*page, "stats.maj.retard.voir"), "Voir les 119 dans Activations");
        check(rig.navigations.back() == "activations", "Activations");
        check(click(*page, "stats.onglet.vue"), "retour");
    }

    std::printf("-- Relever maintenant, Exporter\n");
    {
        check(visibleW(*page, "stats.relever"), "Relever maintenant : l'admin+");
        const std::size_t before = rig.reports.size();
        check(click(*page, "stats.relever") && rig.reports.size() > before && rig.last()->ok && has(rig.last()->message, "Relev\xC3\xA9 du"),
              "le releve du jour (" + (rig.last() ? rig.last()->message : std::string()) + ")");
        const std::string out = U(fx.root / "stats.csv");
        rig.picks.push_back(out);
        check(click(*page, "stats.exporter") && rig.last() && rig.last()->ok, "Exporter : " + (rig.last() ? rig.last()->message : std::string()));
        const std::string csv = readFile(out);
        check(has(csv, "Groupe;Du;Au;Mesure") && has(csv, "Montant encaiss\xC3\xA9") && count(csv, "\r\n") > 100, "le fichier CSV : une ligne par periode et par mesure");
    }

    // 1.48.1 : LA COHERENCE - un changement dans l'Admin (changes::bump) : les chiffres relus au retour de la page, rien de
    // vieux ; Actualiser (tous les onglets) ; Reevaluer (DES FAUX : une reponse ecrite ici, lue par le vrai lecteur) : la
    // fenetre de ses corrections par groupe, ou << Rien a corriger >>, puis les chiffres relus.
    std::printf("-- 1.48.1 : un changement, Actualiser, Reevaluer\n");
    {
        int calls = 0;
        sk::Fetchers f = fakes();
        auto inner = f.series;
        f.series = [&calls, inner](const stats::SeriesQuery& q, stats::SeriesReply& out) {
            ++calls;
            return inner(q, out);
        };
        std::string answer = "{\"jour\":\"2026-11-10\",\"exclus\":{\"clients\":1,\"licences\":2,\"pc\":3},\"nombre\":3,\"corrections\":["
                             "{\"groupe\":\"releves\",\"texte\":\"Licences actives, relev\xC3\xA9 du 10/11 : 17 \xE2\x86\x92 15\",\"avant\":17,\"apres\":15},"
                             "{\"groupe\":\"globe\",\"texte\":\"Globe : Nantes retir\xC3\xA9" "e (plus rien \xC3\xA0 y montrer)\"},"
                             "{\"groupe\":\"releves\",\"texte\":\"PC actifs, relev\xC3\xA9 du 10/11 : 40 \xE2\x86\x92 37\",\"avant\":40,\"apres\":37}]}";
        int asked = 0;
        f.reevaluate = [&answer, &asked](stats::ReevaluateReply& out) {
            ++asked;
            core::json::Value v;
            std::string why;
            if (!core::json::parse(answer, v, &why) || !stats::parseReevaluate(v, out, &why)) return gui::OpResult::fail(why);
            return gui::OpResult::done("Statistiques r\xC3\xA9\xC3\xA9valu\xC3\xA9" "es : " + stats::reevaluateSummary(out));
        };
        sk::setFetchersForTests(f);
        page->refresh();
        (void)draw(*page);
        const int c0 = calls;
        page->refresh();
        (void)draw(*page);
        check(calls == c0, "sans changement : les chiffres gardes (aucune relecture)");
        license_admin::changes::bump();   // une suppression, une publication... ailleurs dans l'Admin
        page->refresh();
        (void)draw(*page);
        check(calls > c0, "apres un changement : les chiffres relus (" + std::to_string(calls - c0) + " lectures)");
        const int c1 = calls;
        const auto g0 = license_admin::changes::current();
        check(visibleW(*page, "stats.actualiser") && click(*page, "stats.actualiser"), "Actualiser : visible, cliquable");
        (void)draw(*page);
        check(calls > c1 && license_admin::changes::current() > g0, "Actualiser : les chiffres relus, les autres vues averties");
        check(click(*page, "stats.onglet.ventes") && click(*page, "stats.actualiser"), "Actualiser sur un autre onglet (Ventes)");
        check(click(*page, "stats.onglet.vue"), "retour a la vue generale");
        // Reevaluer
        auto* rb = find<ui::Button>(*page, "stats.reevaluer");
        check(rb && rb->enabled() && visibleW(*page, "stats.reevaluer"), "Reevaluer : visible, cliquable (l'admin+)");
        const std::size_t dialogs = rig.dialogs.size();
        const int c2 = calls;
        check(click(*page, "stats.reevaluer") && asked == 1, "Reevaluer : demande au service");
        (void)draw(*page);
        check(rig.dialogs.size() == dialogs + 1 && rig.dialogs.back(), "la fenetre des corrections s'ouvre");
        if (rig.dialogs.size() == dialogs + 1 && rig.dialogs.back()) {
            TextRenderer r;
            const std::string d = paintMenu(*rig.dialogs.back(), r, 1100.f, 760.f);
            if (std::getenv("XPG_ESSAI_MONTRER")) std::printf("----\n%s\n", d.c_str());
            check(has(d, "R\xC3\xA9\xC3\xA9valuer les statistiques") && has(d, "3 corrections") && has(d, "Les relev\xC3\xA9s des jours pass\xC3\xA9s (2)") &&
                      has(d, "Le globe (1)") && has(d, "Licences actives, relev\xC3\xA9 du 10/11 : 17 \xE2\x86\x92 15") && has(d, "Globe : Nantes retir\xC3\xA9" "e") &&
                      has(d, "Fermer"),
                  "la fenetre : le titre, le nombre, les groupes et leurs corrections, Fermer");
        }
        check(calls > c2 && rb->enabled() && rb->text() == "R\xC3\xA9\xC3\xA9valuer", "apres Reevaluer : les chiffres relus, le bouton revenu");
        const auto lines = sk::reevaluateLines([&] {
            stats::ReevaluateReply x;
            core::json::Value v;
            (void)core::json::parse(answer, v, nullptr);
            (void)stats::parseReevaluate(v, x, nullptr);
            return x;
        }());
        check(lines.size() == 5 && lines[0] == "# Le globe (1)" && lines[2] == "# Les relev\xC3\xA9s des jours pass\xC3\xA9s (2)", "les lignes : les groupes dans leur ordre");
        answer = "{\"jour\":\"2026-11-10\",\"exclus\":{},\"nombre\":0,\"corrections\":[]}";
        check(click(*page, "stats.reevaluer") && asked == 2 && rig.dialogs.size() == dialogs + 2, "Reevaluer encore");
        {
            TextRenderer r;
            const std::string d = paintMenu(*rig.dialogs.back(), r, 1100.f, 760.f);
            check(has(d, "Rien \xC3\xA0 corriger : les statistiques \xC3\xA9taient coh\xC3\xA9rentes."), "rien a corriger : dit");
        }
        sk::setFetchersForTests(fakes());
        page->refresh();
    }

    std::printf("-- des donnees vides, une mesure refusee, un refus du service\n");
    {
        sk::setFetchersForTests(fakes([](core::json::Value& v) {
            for (auto& [name, m] : v["mesures"].members()) {
                if (!m.isObject()) continue;
                if (name == "ventes_montant") {
                    m = core::json::Value::object();
                    m.set("refuse", "droit");
                    core::json::Value r = core::json::Value::array();
                    r.push("demandes");
                    m.set("droits", r);
                    continue;
                }
                if (auto* p = m.find("points"))
                    for (auto& x : p->items()) x = core::json::Value();
                m.erase("parts");
            }
        }));
        check(click(*page, "stats.reessayer") || true, "relire");
        page->refresh();
        check(click(*page, "stats.periode.30j"), "30 j (une autre periode : relue)");
        const std::string t = draw(*page);
        check(has(t, "Il te manque le droit \xC2\xAB demandes \xC2\xBB"), "une mesure refusee : le droit qui manque");
        check(has(t, "Aucune donn\xC3\xA9" "e sur la p\xC3\xA9riode"), "des points vides : << Aucune donnee sur la periode >>");
        sk::Fetchers broken = fakes();
        broken.series = [](const stats::SeriesQuery&, stats::SeriesReply&) { return gui::OpResult::fail("Refus\xC3\xA9 : le service ne r\xC3\xA9pond pas (essai)"); };
        sk::setFetchersForTests(broken);
        check(click(*page, "stats.periode.90j"), "90 j");
        const std::string t2 = draw(*page);
        check(has(t2, "Les chiffres ne se lisent pas") && visibleW(*page, "stats.reessayer"), "un refus du service : dit une fois, Reessayer");
        sk::setFetchersForTests(fakes());
        check(click(*page, "stats.reessayer"), "Reessayer");
        const std::string t3 = draw(*page);
        check(!has(t3, "ne se lisent pas") && hasValues(tchart(*page, "stats.vue.montant")), "les chiffres reviennent");
    }

    std::printf("-- un service 1.46.0 : deploie la 1.47.0\n");
    {
        license_admin::online::ServiceState st;
        st.reached = true;
        st.version = "1.46.0";
        ws.adoptServiceState(st);
        page->refresh();
        const std::string t = draw(*page);
        const std::string phrase = "Ton service en ligne est en 1.46.0 : d\xC3\xA9ploie la 1.47.0 pour les statistiques et le globe.";
        check(has(t, phrase) && count(t, "pour les statistiques et le globe") == 1, "un seul avertissement, la phrase du contrat");
        check(visibleW(*page, "stats.deployer"), "<< Deployer le service >>");
        check(!has(t, "Lecture des chiffres"), "pas de << lecture >> qui ne viendra jamais");
        check(click(*page, "stats.onglet.mises-a-jour"), "Mises a jour");
        const std::string u = draw(*page);
        check(count(u, "pour les statistiques et le globe") == 1 && visibleW(*page, "stats.maj.deployer"), "Mises a jour : le meme avertissement, une fois, Deployer");
        check(click(*page, "stats.onglet.vue"), "retour");
        st.version = "1.47.0";
        ws.adoptServiceState(st);
        page->refresh();
        const std::string t2 = draw(*page);
        check(!has(t2, "pour les statistiques et le globe") && !visibleW(*page, "stats.deployer") && hasValues(tchart(*page, "stats.vue.montant")),
              "le service a jour : les chiffres reviennent");
    }

    std::printf("-- la fiche d'un client (planche 10)\n");
    {
        gui::requestPageFocus("client", "client:Atelier Exemple");
        auto cp = gui::makeClientCardPage(rig.host());
        const std::string t = draw(*cp);
        check(cp->title() == "Atelier Exemple", "le titre : le client");
        for (const char* s : {"Ses licences et leurs \xC3\xA9" "ch\xC3\xA9" "ances", "Ses PC actifs", "Ses mises \xC3\xA0 jour", "Ses achats", "Le montant cumul\xC3\xA9",
                              "Ses demandes", "Qui l'a servi", "Identit\xC3\xA9", "Ses PC", "Vue d'ensemble", "Commandes", "Journal"})
            check(has(t, s), std::string("la fiche : ") + s);
        check(has(t, "XPG-2026-00031") && !has(t, "XPG-2026-00032"), "la frise : sa licence, pas celle d'un autre client");
        check(has(t, "ATELIER-PC1") && has(t, "ATELIER-PC2"), "ses PC");
        check(has(t, "client depuis le"), "<< client depuis le ... >>");
        auto* pcs = tchart(*cp, "client.pc-actifs");
        check(hasValues(pcs) && pcs->data().series.size() == 2 && pcs->data().series[1].name == "places achet\xC3\xA9" "es (ses licences)",
              "ses PC actifs et les places achetees (deux series, un axe)");
        check(hasValues(tchart(*cp, "client.achats")) && hasValues(tchart(*cp, "client.cumul")), "ses achats ; le montant cumule : un second graphique");
        check(hasValues(tchart(*cp, "client.maj")), "ses mises a jour : la version de ses PC");
        rig.navigations.clear();
        check(click(*cp, "client.onglet.licences") && !rig.navigations.empty() && rig.navigations.back() == "licences" && sk::lastOpened().values.size() == 1 &&
                  sk::lastOpened().values[0].second == "Atelier Exemple",
              "l'onglet Licences : la liste filtree sur ce client");
        check(click(*cp, "client.onglet.commandes") && rig.navigations.back() == "commandes", "l'onglet Commandes");
        check(click(*cp, "client.onglet.pc") && rig.navigations.back() == "activations", "l'onglet PC : Activations");
        check(click(*cp, "client.periode.12mois"), "12 mois");
        (void)draw(*cp);
        check(hasValues(tchart(*cp, "client.pc-actifs")), "12 mois : ses PC actifs");
        auto* dem = tchart(*cp, "client.demandes");
        check(dem && !dem->data().series.empty() && dem->data().series[0].name == "Camille", "12 mois : ses demandes, par admin (Camille d'abord)");
        auto* bar = find<ui::FilterBar>(*cp, "client.pc.barre");
        check(bar != nullptr, "ses PC : la barre de filtres des listes");
        if (bar) {
            bar->setSearch("PC2");
            (void)draw(*cp);
            const std::string t2 = draw(*cp);
            check(has(t2, "ATELIER-PC2") && !has(t2, "ATELIER-PC1"), "chercher << PC2 >> : un PC");
            bar->clearAll();
        }
    }

    std::printf("-- le tableau de bord : la rangee de courbes, la place du globe\n");
    {
        auto dp = gui::makeDashboardPage(rig.host());
        (void)draw(*dp, 1141.f, 813.f);
        const std::string t = draw(*dp, 1141.f, 813.f);
        check(visibleW(*dp, "tableau.courbes") && visibleW(*dp, "tableau.globe"), "la rangee et la petite carte du globe");
        check(hasValues(tchart(*dp, "tableau.courbes.montant")) && hasValues(tchart(*dp, "tableau.courbes.pc")), "les courbes ont des valeurs (30 jours)");
        check(has(t, "Montant encaiss\xC3\xA9") && has(t, "30 j"), "<< Montant encaisse >> sur 30 jours");
        (void)draw(*dp, 1100.f, 640.f);
        check(!visibleW(*dp, "tableau.courbes"), "une fenetre basse : pas de rangee (les tuiles et l'activite d'abord)");
    }

    std::printf("-- aucun texte coupe : 1373 x 917 et 1100 x 700 (les pages : 1141 x 813, 868 x 596)\n");
    {
        installWideMetrics();
        auto wp = gui::makeStatsPage(rig.host());
        wp->refresh();
        gui::requestPageFocus("client", "client:Atelier Exemple");
        auto wc = gui::makeClientCardPage(rig.host());
        const struct {
            const char* window;
            float       w, h;
        } sizes[] = {{"1373x917", 1141.f, 813.f}, {"1100x700", 868.f, 596.f}};
        for (const auto& sz : sizes) {
            for (const char* tab : {"vue", "ventes", "licences", "pc", "mises-a-jour", "demandes", "admins"}) {
                check(click(*wp, std::string("stats.onglet.") + tab), std::string("onglet ") + tab);
                const std::string cut = cutLines(*wp, sz.w, sz.h);
                check(cut.empty(), std::string("Statistiques, ") + tab + " a " + sz.window + " : aucun texte coupe " + cut);
            }
            check(click(*wp, "stats.onglet.mises-a-jour"), "Mises a jour");
            for (const char* part : {"stats.maj.versions", "stats.maj.retard", "stats.maj.tuiles"}) {
                const std::string over = overlapLines(*wp, part, sz.w, sz.h);
                check(over.empty(), std::string("Mises a jour, ") + part + " a " + sz.window + " : aucun texte sur un autre " + over);
            }
            auto* segs = find<sk::SegGroup>(*wp, "stats.maj.retard.filtre");
            check(segs && segs->preferredWidth() <= segs->bounds().w + 0.5f, std::string("Mises a jour a ") + sz.window + " : les filtres des PC en retard en entier (des mots plus courts si etroit)");
            check(click(*wp, "stats.onglet.vue"), "retour");
            const std::string cut = cutLines(*wc, sz.w, sz.h);
            check(cut.empty(), std::string("la fiche d'un client a ") + sz.window + " : aucun texte coupe " + cut);
        }
        auto* g = find<ui::Widget>(*wp, "stats.grille.vue");
        (void)cutLines(*wp, 868.f, 596.f);
        check(g && g->bounds().w > 0, "la grille a 1100 x 700");
        installMetrics();
    }

    std::printf("-- la vraie coquille : un clic sur un point filtre la liste de Commandes\n");
    {
        ShellRig shell(fx.admin);
        shell.shell->navigate("statistiques");
        (void)shell.paint();
        for (int i = 0; i < 20 && shell.shell->finishSlow(); ++i) (void)shell.paint();
        (void)shell.paint();
        check(shell.shell->currentPageId() == "statistiques", "la page Statistiques ouverte");
        const std::string s = shell.paint();
        check(has(s, "Pilotage") && has(s, "nouveau"), "le fil (Pilotage) et << nouveau >> dans le menu");
        auto* m = find<ch::TimeChart>(shell.root(), "stats.vue.montant");
        check(hasValues(m), "le montant encaisse lu dans la coquille");
        if (m) m->pick({static_cast<int>(m->data().x.size()) - 2, 0, "un", 1});
        (void)shell.paint();
        check(shell.shell->currentPageId() == "commandes", "le clic : la page Commandes");
        auto* bar = find<ui::FilterBar>(shell.root(), "commandes");
        bool period = false;
        if (bar)
            for (const auto& p : bar->state().picks)
                if (bar->fields()[p.field].title == "Date" && (p.days > 0 || p.day > 0)) period = true;
        check(bar && period, "sa barre de filtres : la periode de la colonne Date posee");
        check(shell.shell->lastMessage() && has(shell.shell->lastMessage()->result.message, "Commandes"), "l'avis dit ce qui est filtre");
        gui::requestPageFocus("client", "client:Atelier Exemple");
        shell.shell->navigate("client");
        const std::string c = shell.paint();
        check(shell.shell->currentPageId() == "client" && has(c, "Clients") && has(c, "Atelier Exemple"), "Clients > Atelier Exemple (le fil)");
        while (shell.shell->finishSlow()) (void)shell.paint();
    }

    // ------------------------------------------------------------------------------------------------ le vrai service local
    std::printf("-- le service local : le chemin du chargement (sans crochet)\n");
    sk::setFetchersForTests({});
    {
        xpg_test::LocalService::Options so;
        so.sourceDir = source;
        xpg_test::LocalService svc({ws.signingKey() ? ws.signingKey()->publicKeyHex() : std::string()}, so);
        if (svc.skipped()) {
            std::printf("SAUTE : %s\n", svc.error().c_str());
            return finish();
        }
        check(svc.ready(), "le service demarre (" + svc.error() + ")");
        if (!svc.ready()) return finish();
        std::printf("  le service local : %s\n", svc.url().c_str());
        check(ws.serviceConfigure(svc.url()).ok, "l'adresse du service");
        license_admin::online::ServiceState st;
        const gui::OpResult v = ws.serviceVerify(&st);
        check(v.ok && st.version == "1.49.0", "le service local est en 1.49.0 (" + v.text() + ")");
        ws.adoptServiceState(st);
        ws.reload();
        HostRig lrig(fx);
        auto lp = gui::makeStatsPage(lrig.host());
        lp->refresh();
        (void)draw(*lp);
        sk::finishJobsForTests();   // le releve tourne sur son fil (StatsJob) : l'attendre
        const std::string t = draw(*lp);
        check(hasValues(tchart(*lp, "stats.vue.commandes")) || (tchart(*lp, "stats.vue.commandes") && !tchart(*lp, "stats.vue.commandes")->data().series.empty()),
              "les courbes lues sur le vrai service (des series, meme a zero)");
        auto* m = tchart(*lp, "stats.vue.montant");
        check(m && m->data().x.size() >= 13, "le montant : une colonne par semaine (le service local)");
        std::printf("  le montant (le service local) : %zu semaines\n", m ? m->data().x.size() : std::size_t{0});
        check(!has(t, "ne se lisent pas") && !has(t, "Lecture des chiffres"), "aucune carte en panne ni en attente");
        check(click(*lp, "stats.onglet.mises-a-jour"), "Mises a jour (le service local)");
        (void)draw(*lp);
        sk::finishJobsForTests();
        const std::string u = draw(*lp);
        check(has(u, "L'adoption de chaque version") && !has(u, "ne se lisent pas"), "Mises a jour lues sur le vrai service");
    }
    return finish();
}
