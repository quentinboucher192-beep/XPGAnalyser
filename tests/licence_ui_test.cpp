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
#include "../src/licensing/Time.hpp"         // 1.46.0 : la fin d'une reduction (isoTime)
#include "../src/app/Capture.hpp"            // 1.45.0 : encodePng (les captures d'essai)
#include "../src/app/LicenceViews.hpp"       // 1.45.0 : les licences de ce PC, la capture et ses bulles
#include "../src/app/hmi/HmiImages.hpp"      // 1.45.0 : les copies reduites (smooth)
#include "../src/menu/MenuManager.hpp"       // 1.45.0 : la fenetre Licence sur une pile de menus
#include "../src/ui/DialogFrame.hpp"
#include "../src/ui/widgets/Containers.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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

// 1.45.0 : un renderer qui fait des images (un numero, sa taille) et retient les textes et les images dessinees.
struct ImageRecorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    std::map<std::uint32_t, std::pair<int, int>> sizes;
    struct Drawn {
        std::uint32_t id;
        gfx::Rect     dst;
    };
    std::vector<Drawn> drawn;
    std::uint32_t      next{1};
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
    gfx::TextureId createImage(const std::uint8_t*, int w, int h) override {
        sizes[next] = {w, h};
        return gfx::TextureId{next++};
    }
    void drawImage(gfx::TextureId id, const gfx::Rect& dst, float, bool, bool, gfx::Color) override { drawn.push_back({id.v, dst}); }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
    void clear() {
        texts.clear();
        drawn.clear();
    }
};

// 1.45.0 : une capture d'essai (un degrade : rien de reel), en PNG.
bool writeCapture(const fs::path& p, int w, int h) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto* q = rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
            q[0] = static_cast<std::uint8_t>(x * 255 / std::max(1, w - 1));
            q[1] = static_cast<std::uint8_t>(y * 255 / std::max(1, h - 1));
            q[2] = 90;
            q[3] = 255;
        }
    std::vector<std::uint8_t> png;
    if (!app::encodePng(rgba, w, h, png)) return false;
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

bool near(float a, float b, float eps = 1.5f) { return std::abs(a - b) <= eps; }

// 1.46.0 : un renderer qui mesure comme l'interface (ui::measureWidth) : la mise en page (sans renderer) et le dessin disent
// la meme largeur - ce qui tient a la mise en page tient au dessin (les cartes des offres).
struct UiRecorder final : gfx::IRenderer {
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
        return {ui::measureWidth(t, f), ui::lineHeight(f), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return ui::lineHeight(f); }
    gfx::Size surfaceSize() const override { return {1600.f, 1000.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
    // Les textes dessines, a la suite (une ligne coupee en deux se relit entiere).
    [[nodiscard]] std::string joined() const {
        std::string all;
        for (const auto& t : texts) all += t + " ";
        return all;
    }
};

// 1.46.0 : une variable d'environnement (vide : retiree).
void setEnvVar(const char* name, const std::string& value) {
#ifdef _WIN32
    (void)_putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
#endif
}

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

// =============================================================================
//  1.46.0 : << ces scrollbars ne bougent pas avec la souris >> (Demander ma licence) - la barre de la partie gauche (le
//  formulaire) etait seulement dessinee : elle suivait la molette mais ne se prenait pas, et un clic dans sa gouttiere ne
//  faisait rien. Maintenant (ui::ScrollArea) : le pouce se tire, en proportion, la souris tenue (hors de la fenetre),
//  la gouttiere avance d'une page, la molette fait toujours defiler la partie gauche (pas la liste de l'offre).
// =============================================================================
// Les gouttieres dessinees (ui::paintScrollBar : un rectangle arrondi de rayon 4, la couleur des barres au quart).
struct TrackRecorder final : gfx::IRenderer {
    gfx::Color             track{};
    std::vector<gfx::Rect> tracks;
    void beginFrame(gfx::Color) override { tracks.clear(); }
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect& r, gfx::Color c, float radius) override {
        if (radius == 4.f && c.r == track.r && c.g == track.g && c.b == track.b && c.a == track.a) tracks.push_back(r);
    }
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view, gfx::FontId, gfx::Color) override {}
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {static_cast<float>(t.size()) * f.v * 0.5f, static_cast<float>(f.v), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    gfx::Size surfaceSize() const override { return {1302.f, 640.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
};

// La fenetre Licence ouverte sur Demander ma licence (un service suppose), dans une fenetre basse (1302 x 640 : la
// partie gauche y defile) ; `mm` : sa pile de menus.
void leftBarOfRequest(menu::MenuManager& mm) {
    std::printf("-- 1.46.0 : la barre de la partie gauche de Demander ma licence se tire\n");
    const ui::Theme theme = ui::Theme::dark();
    const menu::FrameContext fc{1.0 / 30.0, 1.0, &theme, {1302.f, 640.f}, 1.f};
    TrackRecorder rec;
    rec.track = theme.color.scrollbar.withAlpha(static_cast<std::uint8_t>(theme.color.scrollbar.a / 4));
    const auto frame = [&] {
        mm.applyPending();
        mm.Update(fc);
        mm.Render(rec, fc);
    };
    const auto send = [&](const ui::InputEvent& e) { (void)mm.HandleEvent(e); };
    frame();
    frame();
    ui::Widget* root = mm.top() ? mm.top()->widgetRoot() : nullptr;
    ui::Widget* panel = root ? root->findById("licence.demande") : nullptr;
    ui::Widget* field = root ? root->findById("licence.demande.client") : nullptr;   // dans la partie gauche : il suit le defilement
    auto* list = root ? dynamic_cast<ui::TableView*>(root->findById("licence.demande.offre.liste")) : nullptr;
    check(panel && field && list && panel->visible() && field->visible() && list->visible(), "Demander ma licence : le formulaire a gauche, l'offre a droite");
    if (!panel || !field || !list) return;
    // la barre de la partie gauche : entre le formulaire et la liste de l'offre
    gfx::Rect bar{};
    for (const auto& t : rec.tracks)
        if (t.h > t.w && t.x >= field->bounds().right() - 1.f && t.right() <= list->bounds().x + 1.f) bar = t;
    check(bar.h > 40.f, "1302 x 640 : la partie gauche est plus haute que la fenetre - sa barre, entre le formulaire et l'offre");
    if (bar.h <= 40.f) return;
    const float ph = panel->bounds().h;
    const auto at = [&] { return field->bounds().y; };
    const float y0 = at();
    // tire le pouce (en haut : rien n'a defile) de 15 px, puis de 30 px
    const gfx::Point th{bar.x + bar.w * 0.5f, bar.y + 4.f};
    send(ui::MouseMove{th, {}, {}});
    send(ui::MouseDown{th, ui::MouseButton::Left, 1, {}});
    check(panel->holdsMouse(), "le pouce se prend (la partie gauche tient la souris)");
    send(ui::MouseMove{{th.x, th.y + 15.f}, {}, {}});
    frame();
    const float d1 = y0 - at();
    send(ui::MouseMove{{th.x, th.y + 30.f}, {}, {}});
    frame();
    const float d2 = y0 - at();
    send(ui::MouseMove{{th.x + 700.f, th.y + 3000.f}, {}, {}});   // hors de la fenetre : la souris tenue suit
    frame();
    const float dmax = y0 - at();
    send(ui::MouseUp{{th.x + 700.f, th.y + 3000.f}, ui::MouseButton::Left, {}});
    frame();
    const float thumb = std::min(bar.h, std::max(24.f, bar.h * ph / (dmax + ph)));
    const float want = 15.f * dmax / std::max(1.f, bar.h - thumb);
    check(d1 > 1.f && std::abs(d2 - 2.f * d1) <= 1.5f && std::abs(d1 - want) <= 1.5f && dmax > d2 && !panel->holdsMouse(),
          "tire de 15 px : le formulaire monte de " + std::to_string(static_cast<int>(d1)) + " (attendu " + std::to_string(static_cast<int>(want)) +
              "), de 30 px : " + std::to_string(static_cast<int>(d2)) + " ; hors de la fenetre : jusqu'au bout (" + std::to_string(static_cast<int>(dmax)) +
              ") ; relache, la souris rendue");
    // remonter (le pouce, tire vers le haut), puis un clic dans la gouttiere sous le pouce : une page
    {
        const gfx::Point low{bar.x + bar.w * 0.5f, bar.bottom() - 6.f};
        send(ui::MouseMove{low, {}, {}});
        send(ui::MouseDown{low, ui::MouseButton::Left, 1, {}});
        send(ui::MouseMove{{low.x, low.y - 4000.f}, {}, {}});
        send(ui::MouseUp{{low.x, low.y - 4000.f}, ui::MouseButton::Left, {}});
        frame();
        check(std::abs(at() - y0) <= 0.5f, "tire vers le haut : le formulaire revient en haut");
        send(ui::MouseDown{low, ui::MouseButton::Left, 1, {}});
        send(ui::MouseUp{low, ui::MouseButton::Left, {}});
        frame();
        const float page = std::min(dmax, ph * 0.9f);
        check(std::abs((y0 - at()) - page) <= 1.f, "un clic dans la gouttiere : une page (" + std::to_string(static_cast<int>(y0 - at())) + " sur " +
                                                       std::to_string(static_cast<int>(page)) + ")");
    }
    // la molette : sur le formulaire, il defile ; sur la liste de l'offre, c'est elle qui defile
    {
        const float before = at(), listBefore = list->scrollOffset();
        const gfx::Point left{panel->bounds().x + 30.f, panel->bounds().y + panel->bounds().h * 0.5f};
        send(ui::MouseWheel{left, 0.f, 1.f, {}});
        frame();
        const float afterWheel = at();
        const gfx::Rect lb = list->bounds();
        send(ui::MouseWheel{{lb.x + 30.f, lb.y + lb.h * 0.6f}, 0.f, -1.f, {}});
        frame();
        check(afterWheel > before + 1.f && std::abs(at() - afterWheel) <= 0.5f && list->scrollOffset() > listBefore,
              "la molette : sur le formulaire, il defile ; sur la liste de l'offre, la liste (pas le formulaire)");
        // rendre la fenetre comme elle etait (en haut, la liste aussi)
        send(ui::MouseWheel{left, 0.f, 200.f, {}});
        list->setScrollOffset(0.f);
        frame();
        check(std::abs(at() - y0) <= 0.5f, "la molette vers le haut : le formulaire revient en haut");
    }
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

        // ---- 1.45.0 : la fenetre Licence - les licences de ce PC, une seule fenetre, les captures en grand ----
        std::printf("-- 1.45.0 : les licences de ce PC (Regarder, Liberer), une seule fenetre Licence, les captures et leurs bulles\n");
        {
            namespace ls = app::licence::service;
            namespace L = app::licence;
            using K = L::PcLicence::Kind;
            app::licence::start(o);   // le PC de la licence installee par fichier (la derniere importee plus haut)
            const auto st45 = lic::getLicenseState();
            const lic::Seconds t0 = lic::systemNow();
            // -- le modele, sur le magasin d'essai
            auto rows = L::pcLicences();
            check(!st45.licenseId.empty() && rows.size() == 1 && rows[0].kind == K::File && rows[0].id == st45.licenseId && rows[0].edition == st45.licensedEdition &&
                      rows[0].customer == "Client d'essai" && has(rows[0].state, "Valide") && has(rows[0].state, "fichier") && has(rows[0].until, "jusqu'au") &&
                      rows[0].tab == L::kTabMine && rows[0].tone == 1,
                  "les licences de ce PC : la licence li\xC3\xA9" "e par fichier (" + st45.licenseId + " : " + (rows.empty() ? std::string("aucune") : rows[0].state) + ")");
            const auto filePlan = rows.empty() ? L::ReleasePlan{} : L::releasePlan(rows[0], st45, t0);
            check(filePlan.action == L::ReleasePlan::Action::None && has(filePlan.text, st45.licenseId) && has(filePlan.text, "retirer ce PC"),
                  "Lib\xC3\xA9rer une licence par fichier : rien d'ici, la phrase dit quoi faire");
            // une licence en ligne, son poste flottant, une option en cours, une option finie, une activation d'avant
            lic::LicenseState on = st45;
            on.online = true;
            on.floating = true;
            on.activationUntil = t0 + 20 * lic::kDay;
            on.activationDaysLeft = 20;
            on.leaseUntil = t0 + 600;
            lic::LicenseOption opt;
            opt.id = "multi_user";
            opt.label = "Travail \xC3\xA0 plusieurs";
            opt.from = t0 - lic::kDay;
            opt.until = t0 + 30 * lic::kDay;
            lic::LicenseOption over = opt;
            over.id = "advanced_export";
            over.until = t0 - 3600;
            on.options = {opt, over};
            ls::Record rec;
            rec.activatedLicense = "XPG-UI-0099";
            rows = L::pcLicences(on, rec, false, t0);
            check(rows.size() == 4 && rows[0].kind == K::Online && has(rows[0].state, "bail jusqu'au") && has(rows[0].state, "(20 j)") &&
                      rows[1].kind == K::Floating && has(rows[1].state, "Poste pris") && rows[1].tab == L::kTabActivation &&
                      rows[2].kind == K::Option && has(rows[2].title, "Travail") && rows[2].tab == L::kTabOptions && has(rows[2].state, "active") &&
                      rows[3].kind == K::Renew && rows[3].id == "XPG-UI-0099",
                  "en ligne : la licence et son bail, le poste flottant, l'option en cours (pas la finie), l'activation d'avant \xC3\xA0 rendre");
            check(L::pcLicences(on, ls::Record{}, true, t0).size() == 2, "le poste flottant rendu (Rendre le poste) : plus dans la liste");
            on.status = lic::LicenseStatus::LeaseExpired;
            on.activationUntil = 0;
            rows = L::pcLicences(on, ls::Record{}, false, t0);
            check(!rows.empty() && rows[0].kind == K::Renew && rows[0].tone == 3 && has(rows[0].state, "bail expir") && rows[0].tab == L::kTabActivation,
                  "le bail fini : la licence en ligne \xC3\xA0 renouveler (sa place est prise)");
            lic::LicenseState none;
            none.status = lic::LicenseStatus::NotConfigured;
            check(L::pcLicences(none, ls::Record{}, false, t0).empty(), "licences pas actives : aucune ligne");
            {   // Liberer : ce qui se passe (la Decouverte, ou plus de licence apres)
                L::PcLicence onl;
                onl.kind = K::Online;
                onl.id = "XPG-UI-0001";
                onl.edition = "PERSONAL";
                lic::LicenseState ev = st45;
                ev.evaluationStartedAt = t0 - 5 * lic::kDay;
                ev.evaluationEndsAt = t0 + 10 * lic::kDay;
                const auto p1 = L::releasePlan(onl, ev, t0);
                ev.evaluationEndsAt = t0 - lic::kDay;
                ev.evaluationOver = true;
                const auto p2 = L::releasePlan(onl, ev, t0);
                L::PcLicence seat = onl;
                seat.kind = K::Floating;
                L::PcLicence o2 = onl;
                o2.kind = K::Option;
                o2.title = "Option Travail";
                check(p1.action == L::ReleasePlan::Action::Release && has(p1.text, "repasse en D\xC3\xA9" "couverte") && has(p1.text, "(10 jours)") &&
                          has(p1.text, "XPG-UI-0001") && p1.confirm == "Lib\xC3\xA9rer ce poste" && p1.cancel == "Garder la licence ici",
                      "Lib\xC3\xA9rer une licence en ligne : la question, et ce PC repasse en D\xC3\xA9" "couverte (" + p1.text.substr(0, 60) + ")");
                check(has(p2.text, "plus de licence") && has(p2.text, "aucun projet"), "la D\xC3\xA9" "couverte finie : ce PC n'aura plus de licence");
                check(L::releasePlan(seat, st45, t0).action == L::ReleasePlan::Action::ReturnSeat &&
                          has(L::releasePlan(seat, st45, t0).text, "comme sans licence") &&
                          L::releasePlan(o2, st45, t0).action == L::ReleasePlan::Action::None,
                      "le poste flottant : rendu (ce PC comme sans licence) ; une option : elle suit sa licence");
            }
            {   // la vue : une ligne, Regarder, Liberer ; vide : la phrase
                L::PcLicencesView view("pc-essai");
                std::vector<L::PcLicence> got;
                int looks = 0;
                view.onLook = [&](const L::PcLicence& l) {
                    ++looks;
                    got.push_back(l);
                };
                view.onRelease = [&](const L::PcLicence& l) { got.push_back(l); };
                view.setRows(L::pcLicences(), "vide");
                view.setBounds({0, 0, 900, view.wantedHeight()});
                view.layout();
                auto* look = view.lookButton(0);
                auto* release = view.releaseButton(0);
                check(look && release && look->visible() && release->visible() && !view.lookButton(1)->visible() && look->bounds().w > 0.f &&
                          release->bounds().x > look->bounds().x &&
                          near(view.wantedHeight(), L::PcLicencesView::kHeader + L::PcLicencesView::kRow + L::PcLicencesView::kGap),
                      "une ligne : Regarder, et Lib\xC3\xA9rer \xC3\xA0 sa droite");
                const gfx::Point lp{look->bounds().x + 10.f, look->bounds().y + 10.f};
                (void)view.dispatch(ui::MouseMove{lp, {}, {}});
                (void)view.dispatch(ui::MouseDown{lp, ui::MouseButton::Left, 1, {}});
                (void)view.dispatch(ui::MouseUp{lp, ui::MouseButton::Left, {}});
                release->clicked->emit();
                check(looks == 1 && got.size() == 2 && got[0].id == st45.licenseId && got[1].kind == K::File, "un clic sur Regarder, puis Lib\xC3\xA9rer : leur ligne");
                Recorder r1;
                view.render(ui::PaintContext{r1, ui::Theme::light(), view.bounds(), 0.0, nullptr});
                check(r1.saw("Les licences de ce PC") && r1.saw(st45.licenseId) && r1.saw(st45.licensedEdition) && r1.saw("Client d'essai"),
                      "dessin\xC3\xA9" "e : le titre, la licence, son \xC3\xA9" "dition, le client");
                view.setRows({}, L::pcLicencesEmptyText(lic::LicenseState{}));
                Recorder r2;
                view.render(ui::PaintContext{r2, ui::Theme::light(), view.bounds(), 0.0, nullptr});
                check(!view.lookButton(0)->visible() && r2.saw("Aucune licence sur ce PC"), "aucune licence : la phrase, pas de boutons");
            }

            // -- la fenetre Licence sur une pile de menus
            const ui::Theme theme = ui::Theme::dark();
            const menu::FrameContext fc{1.0 / 30.0, 1.0, &theme, {1600.f, 900.f}, 1.f};
            menu::MenuManager mm{menu::MenuFactory{}};
            const auto frame = [&] {
                mm.applyPending();
                mm.Update(fc);
            };
            const auto rootOf = [&]() -> ui::Widget* { return mm.top() ? mm.top()->widgetRoot() : nullptr; };
            const auto button = [&](const char* id) -> ui::Button* {
                auto* r = rootOf();
                return r ? dynamic_cast<ui::Button*>(r->findById(id)) : nullptr;
            };
            const auto click = [&](gfx::Point p) {
                (void)mm.HandleEvent(ui::MouseMove{p, {}, {}});
                (void)mm.HandleEvent(ui::MouseDown{p, ui::MouseButton::Left, 1, {}});
                (void)mm.HandleEvent(ui::MouseUp{p, ui::MouseButton::Left, {}});
            };
            app::showLicenceWindow(mm, L::kTabRequest);
            frame();
            ui::Widget* licRoot = rootOf();
            auto* pcs = licRoot ? dynamic_cast<L::PcLicencesView*>(licRoot->findById("licence.demande.ce-pc")) : nullptr;
            // 1.46.0 : plus d'onglets - la rubrique montree (son fil d'Ariane)
            check(mm.depth() == 1 && app::licenceWindowOpen() && app::licenceWindowSection() == L::kTabRequest && pcs &&
                      pcs->rows().size() == 1 && pcs->bounds().w > 0.f && pcs->bounds().h >= L::PcLicencesView::kHeader,
                  "Demander ma licence : << Les licences de ce PC >> en t\xC3\xAAte (la licence de ce PC)");
            if (!pcs) {
                std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
                return 1;
            }
            if (auto* btn = button("licence.demande.ce-pc.regarder.1")) btn->clicked->emit();
            check(app::licenceWindowSection() == L::kTabMine, "Regarder : la rubrique Ma licence");
            app::showLicenceWindow(mm, L::kTabRequest);
            frame();
            if (auto* btn = button("licence.demande.ce-pc.liberer.1")) btn->clicked->emit();
            frame();
            check(mm.depth() == 2 && mm.top() && mm.top()->id() == "dialog.message",
                  "Lib\xC3\xA9rer (une licence par fichier) : la fen\xC3\xAAtre qui dit pourquoi pas d'ici");
            if (auto* btn = button("dialog.message.ok")) btn->clicked->emit();
            frame();
            check(mm.depth() == 1 && rootOf() == licRoot, "ferm\xC3\xA9" "e d'un clic");

            // -- UNE SEULE FENETRE : la reponse de l'editeur arrive pendant que la fenetre Licence est ouverte ; << Voir ma
            //    licence >> la ramene sur Ma licence (avant : une seconde par-dessus, deux clics sur la croix pour sortir)
            const auto answer = [](const char* kind, const char* id, const char* note) {
                L::Answer a;
                a.kind = kind;
                a.licenseId = id;
                a.note = note;
                return a;
            };
            L::queueAnswer(answer(ls::kAnswerAccepted, "XPG-UI-0001", "Bonne route"));
            L::queueAnswer(answer(ls::kAnswerAccepted, "XPG-UI-0001", "Bonne route"));   // le meme evenement, releve deux fois
            const auto accepted = L::takeAnswer();
            check(accepted && !L::takeAnswer(), "la m\xC3\xAAme r\xC3\xA9ponse deux fois dans la file : une seule fen\xC3\xAAtre");
            if (accepted) L::showAnswer(mm, *accepted);
            frame();
            L::queueAnswer(answer(ls::kAnswerRefused, "", "Raison d'essai"));
            check(mm.depth() == 2 && L::answerOpen() && !L::takeAnswer(), "la r\xC3\xA9ponse ouverte : la suivante attend sa fermeture");
            if (auto* btn = button("dialog.message.ok")) btn->clicked->emit();   // Voir ma licence
            frame();
            check(mm.depth() == 1 && !L::answerOpen() && rootOf() == licRoot && app::licenceWindowSection() == L::kTabMine,
                  "Voir ma licence : la fen\xC3\xAAtre Licence d\xC3\xA9j\xC3\xA0 ouverte passe \xC3\xA0 Ma licence - pas une seconde par-dessus (" +
                      std::to_string(mm.depth()) + " fen\xC3\xAAtre(s))");
            const auto refused = L::takeAnswer();
            check(refused && refused->kind == ls::kAnswerRefused, "la r\xC3\xA9ponse suivante, une fois la premi\xC3\xA8re ferm\xC3\xA9" "e");
            app::showLicenceWindow(mm, L::kTabEditions);
            app::showLicenceWindow(mm, L::kTabFeatures);
            frame();
            check(mm.depth() == 1 && rootOf() == licRoot && app::licenceWindowSection() == L::kTabFeatures,
                  "redemand\xC3\xA9" "e deux fois : toujours une seule fen\xC3\xAAtre (sur la derni\xC3\xA8re rubrique)");

            // -- quatre captures (une 2x : 3200 x 2000), leurs legendes, leurs bulles dans le repere 1600 x 1000
            const fs::path shots = base / "captures-1-45";
            check(writeCapture(shots / "api_editor.png", 3200, 2000) && writeCapture(shots / "api_editor-2.png", 1600, 1000) &&
                      writeCapture(shots / "api_editor-3.png", 320, 200) && writeCapture(shots / "api_editor-4.png", 320, 200),
                  "quatre captures d'essai (la premi\xC3\xA8re en 2x : 3200 x 2000)");
            writeFile(shots / "descriptions.json",
                      "{\"format\": \"xpg-fonctions-apercu/1\", \"repere\": {\"l\": 1600, \"h\": 1000},"
                      " \"fonctions\": {\"api_editor\": \"L'\xC3\xA9" "diteur d'essai.\"}, \"captures\": {\"api_editor\": ["
                      "{\"legende\": \"la vue d'ensemble\", \"bulles\": [{\"x\": 100, \"y\": 50, \"l\": 200, \"h\": 100, \"texte\": \"Bulle un : la barre.\"}]},"
                      "{\"legende\": \"le d\xC3\xA9tail\", \"bulles\": [{\"x\": 100, \"y\": 50, \"l\": 200, \"h\": 100, \"texte\": \"Bulle deux.\"}]},"
                      "{\"legende\": \"en petit\", \"repere\": [320, 200], \"bulles\": [{\"x\": 160, \"y\": 100, \"l\": 80, \"h\": 50, \"texte\": \"Bulle trois.\"}]},"
                      "{\"legende\": \"la fin\"}]}}");
            app::licence::setFeatureImagesFolder(U(shots));
            const auto pv = L::featurePreview("api_editor");
            check(pv.shots.size() == 4 && pv.shots[0].refW == 1600.f && pv.shots[0].refH == 1000.f && pv.shots[1].refW == 1600.f && pv.shots[2].refW == 320.f &&
                      pv.shots[2].refH == 200.f && pv.shots[3].caption == "la fin" && pv.shots[0].spots.size() == 1 && pv.shots[3].spots.empty(),
                  "featurePreview : 4 captures, leurs l\xC3\xA9gendes, leurs bulles, leur rep\xC3\xA8re");
            if (pv.shots.size() == 4) {
                // une capture 2x et celle de taille normale : la bulle au meme endroit
                L::CaptureView big("essai.2x"), normal("essai.1x");
                big.setImage(L::capturePng(pv.shots[0].imagePath, "api_editor.png"));
                normal.setImage(L::capturePng(pv.shots[1].imagePath, "api_editor-2.png"));
                big.setSpots(pv.shots[0].spots, pv.shots[0].refW, pv.shots[0].refH);
                normal.setSpots(pv.shots[1].spots, pv.shots[1].refW, pv.shots[1].refH);
                big.setBounds({0, 0, 800, 500});
                normal.setBounds({0, 0, 800, 500});
                const auto ra = big.spotRect(0), rb = normal.spotRect(0);
                check(big.imageWidth() == 3200.f && normal.imageWidth() == 1600.f && ra && rb && near(ra->x, rb->x, 0.01f) && near(ra->y, rb->y, 0.01f) &&
                          near(ra->x, 50.f) && near(ra->y, 25.f) && near(ra->w, 100.f) && near(ra->h, 50.f),
                      "une capture 2x : sa bulle \xC3\xA0 la m\xC3\xAAme place que sur la capture de taille normale (rep\xC3\xA8re 1600 x 1000)");
                check(big.spotAt({100.f, 50.f}) == 0 && big.spotAt({20.f, 20.f}) == -1, "la bulle sous la souris, et rien \xC3\xA0 c\xC3\xB4t\xC3\xA9");
                // le dessin : la copie reduite la plus proche (800 x 500 pour une case de 800 x 500), lissee
                ImageRecorder ir;
                big.render(ui::PaintContext{ir, theme, big.bounds(), 0.0, nullptr});
                const auto drawnSize = [&ir]() {
                    return ir.drawn.empty() ? std::pair<int, int>{0, 0} : ir.sizes[ir.drawn.back().id];
                };
                check(!ir.drawn.empty() && drawnSize() == std::pair<int, int>{800, 500} && near(ir.drawn.back().dst.w, 800.f, 0.5f),
                      "la capture 3200 x 2000 dessin\xC3\xA9" "e en 800 : sa copie 800 x 500 (" + std::to_string(drawnSize().first) + ")");
                for (int i = 0; i < 8; ++i) big.wheelAt({400.f, 250.f}, 1.f);
                ir.clear();
                big.render(ui::PaintContext{ir, theme, big.bounds(), 0.0, nullptr});
                check(big.zoom() == 4.f && drawnSize() == std::pair<int, int>{3200, 2000}, "zoom\xC3\xA9" "e (x 4) : l'image enti\xC3\xA8re");
                check(app::HmiImageCache::smoothLevel(3200, 2000, 450.f, 281.f) == 2 && app::HmiImageCache::smoothLevel(3200, 2000, 1600.f, 1000.f) == 1 &&
                          app::HmiImageCache::smoothLevel(3200, 2000, 1700.f, 1062.f) == 0 && app::HmiImageCache::smoothLevel(960, 600, 1468.f, 917.f) == 0 &&
                          app::HmiImageCache::smoothLevel(0, 0, 10.f, 10.f) == 0,
                      "la copie choisie : la plus petite encore aussi grande que le dessin");
            }
            {   // l'apercu (onglet Fonctionnalites) : les quatre captures, les fleches ; un clic : la capture en grand
                auto* table = dynamic_cast<ui::TableView*>(licRoot->findById("dialog.licence.fonctionnalites"));
                std::size_t row = static_cast<std::size_t>(-1);
                if (table && table->model())
                    for (std::size_t r = 0; r < table->model()->rowCount(); ++r)
                        if (table->model()->cellText(r, 1) == "api_editor") row = r;
                check(row != static_cast<std::size_t>(-1), "l'onglet Fonctionnalit\xC3\xA9s : la ligne api_editor");
                if (table && row != static_cast<std::size_t>(-1)) table->selectModelRows({static_cast<ui::RowIndex>(row)}, true);
                frame();
                auto* cap = dynamic_cast<L::CaptureView*>(licRoot->findById("dialog.licence.fonctionnalites.panneau.apercu.capture"));
                check(cap && cap->hasImage() && cap->spotCount() == 1 && cap->imageWidth() == 3200.f && cap->bounds().w > 0.f,
                      "l'aper\xC3\xA7u : la premi\xC3\xA8re capture (2x), sa bulle");
                const auto next = [&] {
                    if (auto* btn = button("dialog.licence.fonctionnalites.panneau.apercu.suivante")) btn->clicked->emit();
                    frame();
                };
                next();
                next();
                ImageRecorder paint;
                mm.Render(paint, fc);
                check(cap && cap->imageWidth() == 320.f && cap->spotCount() == 1 && paint.saw("Capture 3/4 : en petit"), "la fl\xC3\xA8" "che : << Capture 3/4 : en petit >>");
                next();
                next();   // apres la quatrieme : la premiere
                check(cap && cap->imageWidth() == 3200.f, "apr\xC3\xA8s la quatri\xC3\xA8me, la premi\xC3\xA8re");
                if (cap) {
                    const auto cb = cap->bounds();
                    click({cb.x + cb.w * 0.3f, cb.y + cb.h * 0.8f});
                }
                frame();
                auto* zoom = dynamic_cast<L::CaptureZoomBody*>(rootOf());
                check(mm.depth() == 2 && mm.top()->id() == "dialog.capture" && zoom && zoom->shotCount() == 4 && zoom->shot() == 0 &&
                          zoom->navText() == "Capture 1/4 : la vue d'ensemble" && zoom->image().spotCount() == 1,
                      "un clic sur la capture : en grand, ses quatre captures, sa l\xC3\xA9gende, sa bulle");
                if (zoom) {
                    auto& zv = zoom->image();
                    const auto c0 = zv.spotCenter(0);
                    const auto ir0 = zv.imageRect();
                    // la bulle, en grand : au survol de sa zone (le repere 1600 x 1000 sur l'image 3200 x 2000)
                    check(c0 && near(c0->x, ir0.x + 200.f / 1600.f * ir0.w) && near(c0->y, ir0.y + 100.f / 1000.f * ir0.h),
                          "en grand : la bulle \xC3\xA0 sa place (rep\xC3\xA8re 1600 x 1000, image 3200 x 2000)");
                    if (c0) (void)mm.HandleEvent(ui::MouseMove{*c0, {}, {}});
                    ImageRecorder z1;
                    mm.Render(z1, fc);
                    check(zv.hoveredSpot() == 0 && zv.hovered() && z1.saw("Bulle un : la barre."), "en grand : la souris sur la zone, sa bulle dite");
                    // zoomee (la molette) puis deplacee (glisser) : toujours a sa place
                    if (c0) {
                        zv.wheelAt(*c0, 1.f);
                        zv.wheelAt(*c0, 1.f);
                    }
                    const auto c1 = zv.spotCenter(0);
                    if (c1) (void)mm.HandleEvent(ui::MouseMove{*c1, {}, {}});
                    check(zv.zoom() > 1.5f && c1 && zv.spotAt(*c1) == 0 && zv.hoveredSpot() == 0, "zoom\xC3\xA9" "e : la bulle suit l'image (au survol de sa zone)");
                    const auto ir1 = zv.imageRect();
                    const gfx::Point from{ir1.x + ir1.w * 0.5f, ir1.y + ir1.h * 0.5f};
                    const gfx::Point grab = zv.bounds().contains(from) ? from : gfx::Point{zv.bounds().x + 40.f, zv.bounds().y + 40.f};
                    (void)mm.HandleEvent(ui::MouseMove{grab, {}, {}});
                    (void)mm.HandleEvent(ui::MouseDown{grab, ui::MouseButton::Left, 1, {}});
                    (void)mm.HandleEvent(ui::MouseMove{{grab.x + 60.f, grab.y + 40.f}, {}, {}});
                    (void)mm.HandleEvent(ui::MouseUp{{grab.x + 60.f, grab.y + 40.f}, ui::MouseButton::Left, {}});
                    const auto c2 = zv.spotCenter(0);
                    const auto ir2 = zv.imageRect();
                    check(c2 && near(c2->x - ir2.x, (c1 ? c1->x : 0.f) - ir1.x, 0.01f) && near(c2->y - ir2.y, (c1 ? c1->y : 0.f) - ir1.y, 0.01f) &&
                              (ir2.x != ir1.x || ir2.y != ir1.y),
                          "d\xC3\xA9plac\xC3\xA9" "e : la bulle reste sur sa zone de l'image");
                    if (c2) (void)mm.HandleEvent(ui::MouseMove{*c2, {}, {}});
                    ImageRecorder z2;
                    mm.Render(z2, fc);
                    check(zv.hoveredSpot() == 0 && z2.saw("Bulle un : la barre."), "zoom\xC3\xA9" "e et d\xC3\xA9plac\xC3\xA9" "e : sa bulle dite au survol");
                    (void)mm.HandleEvent(ui::MouseMove{{zv.bounds().x + 2.f, zv.bounds().bottom() - 2.f}, {}, {}});
                    check(zv.hoveredSpot() == -1, "hors de la zone : pas de bulle");
                    // les fleches de la fenetre en grand (le bouton, le clavier)
                    if (auto* btn = button("dialog.capture.suivante")) btn->clicked->emit();
                    frame();
                    check(zoom->shot() == 1 && zoom->navText() == "Capture 2/4 : le d\xC3\xA9tail" && zv.imageWidth() == 1600.f && zv.zoom() == 1.f &&
                              zv.spotCount() == 1,
                          "en grand, la suivante : << Capture 2/4 : le d\xC3\xA9tail >>, ajust\xC3\xA9" "e, sa bulle");
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Right, {}, false});
                    frame();
                    const auto c3 = zv.spotCenter(0);
                    const auto ir3 = zv.imageRect();
                    check(zoom->shot() == 2 && zv.imageWidth() == 320.f && c3 && near(c3->x, ir3.x + 200.f / 320.f * ir3.w) && near(c3->y, ir3.y + 125.f / 200.f * ir3.h),
                          "la fl\xC3\xA8" "che droite : la troisi\xC3\xA8me (son rep\xC3\xA8re \xC3\xA0 elle, 320 x 200)");
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Right, {}, false});
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Right, {}, false});
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Left, {}, false});
                    check(zoom->shot() == 3 && zv.spotCount() == 0 && zoom->navText() == "Capture 4/4 : la fin", "en boucle, dans les deux sens");
                    // la croix : un clic la ferme
                    const auto zf = ui::dialogframe::frameOf(*rootOf());
                    click({zf.right() - 24.f, zf.y + 21.f});
                    frame();
                    check(mm.depth() == 1 && rootOf() == licRoot, "la croix de la capture en grand : un clic");
                }
            }
            {   // la boutique (Demander ma licence, avec un service) : la meme capture, ses fleches
                licensing::service::Address a;
                a.url = "http://127.0.0.1:1";   // un service suppose (rien ne l'appelle ici)
                ls::setAddressForTests(a);
                app::showLicenceWindow(mm, L::kTabRequest);
                frame();
                auto* list = dynamic_cast<ui::TableView*>(licRoot->findById("licence.demande.offre.liste"));
                std::size_t row = static_cast<std::size_t>(-1);
                if (list && list->model())
                    for (std::size_t r = 0; r < list->model()->rowCount(); ++r)
                        if (list->model()->cellText(r, 0) == L::featureLabel("api_editor")) row = r;
                if (list && row != static_cast<std::size_t>(-1)) list->selectModelRows({static_cast<ui::RowIndex>(row)}, true);
                frame();
                auto* shop = dynamic_cast<L::CaptureView*>(licRoot->findById("licence.demande.offre.apercu.capture"));
                check(shop && shop->hasImage() && shop->spotCount() == 1 && shop->bounds().w > 0.f, "la boutique : l'aper\xC3\xA7u de la fonctionnalit\xC3\xA9 (la capture 2x, sa bulle)");
                if (auto* btn = button("licence.demande.offre.apercu.precedente")) btn->clicked->emit();
                frame();
                ImageRecorder sp;
                mm.Render(sp, fc);
                check(shop && shop->imageWidth() == 320.f && sp.saw("Capture 4/4 : la fin") && sp.saw("Les licences de ce PC"),
                      "la boutique : la pr\xC3\xA9" "c\xC3\xA9" "dente (la quatri\xC3\xA8me), et les licences de ce PC en t\xC3\xAAte");
                leftBarOfRequest(mm);   // 1.46.0 : la barre de la partie gauche se tire (une fenetre basse)
                frame();
                ls::setAddressForTests(licensing::service::Address{});
            }
            // ---- 1.46.0 : LE FIL D'ARIANE (plus d'onglets), L'ACCUEIL, MON PROFIL A PART, CHANGER DE FONCTIONNALITE EN GRAND (LES
            //      BARREES COMPRISES), LES CARTES DES OFFRES SANS RIEN DE COUPE ----
            std::printf("-- 1.46.0 : l'accueil et le fil d'Ariane, Mon profil a part, changer de fonctionnalite en grand, les cartes des offres\n");
            {
                // -- le modele : dix rubriques en quatre groupes ; Mon profil n'en est plus une
                std::set<int> tabsSeen;
                for (const auto& s : L::sections()) tabsSeen.insert(s.tab);
                check(L::sections().size() == 10 && L::sectionGroups().size() == 4 && !L::sectionOf(L::kTabProfile) && tabsSeen.size() == 10 &&
                          !tabsSeen.count(L::kTabProfile) && !tabsSeen.count(L::kTabHome),
                      "dix rubriques en quatre groupes (Ma licence, Obtenir ou \xC3\xA9tendre, D\xC3\xA9" "couvrir, R\xC3\xA9glages) ; Mon profil n'en est plus une");
                check(L::sectionByName("demander") == L::kTabRequest && L::sectionByName("Mes achats") == L::kTabPurchases &&
                          L::sectionByName("mes-achats") == L::kTabPurchases && L::sectionByName("fonctionnalit\xC3\xA9s") == L::kTabFeatures &&
                          L::sectionByName("\xC3\x89" "ditions") == L::kTabEditions && L::sectionByName("accueil") == L::kTabHome &&
                          L::sectionByName("-2") == L::kTabHome && L::sectionByName("7") == L::kTabRequest && L::sectionByName("profil") == L::kTabProfile &&
                          L::sectionByName("Ma licence") == L::kTabMine && L::sectionByName("achats") == L::kTabPurchases &&
                          L::sectionByName("nulle part") == L::kTabAuto && L::sectionByName("42") == L::kTabAuto,
                      "une rubrique par son nom (sans accents, le d\xC3\xA9" "but suffit), son num\xC3\xA9ro, l'accueil, le profil");
                check(L::crumbText(L::kTabRequest) == "Licence \xE2\x80\xBA Demander ma licence" && L::crumbText(L::kTabHome) == "Licence",
                      "le fil : << Licence > Demander ma licence >>");

                // -- l'accueil : le resume, les rubriques en groupes
                app::showLicenceWindow(mm, L::kTabHome);
                frame();
                auto* home = dynamic_cast<L::LicenceHome*>(licRoot->findById("licence.accueil"));
                auto* fil = dynamic_cast<L::LicenceCrumbs*>(licRoot->findById("dialog.licence.fil"));
                bool allLinks = home != nullptr;
                for (const auto& s : L::sections()) {
                    auto* l = dynamic_cast<L::SectionLink*>(licRoot->findById(std::string("licence.accueil.") + s.slug));
                    allLinks = allLinks && l && l->visible() && l->tab == s.tab && l->bounds().w > 200.f &&
                               home->groupRect(static_cast<std::size_t>(s.group)).contains({l->bounds().x + 2.f, l->bounds().y + 2.f});
                }
                check(app::licenceWindowSection() == L::kTabHome && home && home->visible() && fil && fil->text() == "Licence" && allLinks &&
                          !licRoot->findById("licence.accueil.profil") && !licRoot->findById("dialog.licence.onglets") && !licRoot->findById("licence.profil"),
                      "l'accueil : les dix rubriques dans leurs groupes, << Licence >> seul dans le fil ; ni bande d'onglets ni Mon profil");
                if (!home || !fil) {
                    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
                    return 1;
                }
                check(home->columns() == 2 && home->contentHeight() <= home->bounds().h + 0.5f,
                      "1600 x 900 : deux colonnes de groupes, tout tient sans d\xC3\xA9" "filer (" + std::to_string(static_cast<int>(home->contentHeight())) + " sur " +
                          std::to_string(static_cast<int>(home->bounds().h)) + ")");
                {
                    ImageRecorder hr;
                    mm.Render(hr, fc);
                    check(hr.saw("Ma licence") && hr.saw("Obtenir ou \xC3\xA9tendre") && hr.saw("D\xC3\xA9" "couvrir") && hr.saw("R\xC3\xA9glages") &&
                              hr.saw("Demander ma licence") && hr.saw("Mes achats") && hr.saw("Client d'essai") && hr.saw(st45.licenseId) && !hr.saw("Mon profil"),
                          "dessin\xC3\xA9 : le r\xC3\xA9sum\xC3\xA9 (le client, la licence), les groupes et leurs rubriques ; pas Mon profil");
                }
                check(has(home->hint(L::kTabFeatures), "/21") && has(home->hint(L::kTabMine), "CUSTOM") && has(home->hint(L::kTabSettings), "registre"),
                      "ce que dit d\xC3\xA9j\xC3\xA0 chaque rubrique : " + home->hint(L::kTabFeatures) + " ; " + home->hint(L::kTabMine));

                // -- chaque numero d'onglet (les notifications, Regarder, les scripts) ouvre sa rubrique, avec son fil
                const std::map<int, const char*> pageIds = {
                    {L::kTabActivation, "licence.activation"},       {L::kTabMine, "dialog.licence.ma-licence"},
                    {L::kTabDevices, "dialog.licence.appareils.panneau"}, {L::kTabFeatures, "dialog.licence.fonctionnalites.panneau"},
                    {L::kTabEditions, "dialog.licence.editions.panneau"}, {L::kTabHistory, "dialog.licence.historique"},
                    {L::kTabSettings, "dialog.licence.parametres"},  {L::kTabRequest, "licence.demande"},
                    {L::kTabOptions, "licence.options"},              {L::kTabPurchases, "licence.achats"}};
                bool each = true;
                std::string wrong;
                for (const auto& [tab, id] : pageIds) {
                    app::showLicenceWindow(mm, tab);
                    frame();
                    bool ok = mm.depth() == 1 && app::licenceWindowSection() == tab && !home->visible() && fil->section() == tab &&
                              fil->text() == L::crumbText(tab) && fil->current().visible();
                    for (const auto& [other, otherId] : pageIds) {
                        auto* w = licRoot->findById(otherId);
                        ok = ok && w && w->visible() == (other == tab) && (other != tab || w->bounds().h > 500.f);
                    }
                    if (!ok) {
                        each = false;
                        wrong += " " + std::to_string(tab);
                    }
                }
                check(each, "chaque num\xC3\xA9ro (kTab*) ouvre sa rubrique seule, << Licence > sa rubrique >> en haut, plus haute qu'un onglet d'avant" + wrong);

                // -- le fil : << Licence >> revient a l'accueil ; une ligne de l'accueil ouvre sa rubrique
                app::showLicenceWindow(mm, L::kTabRequest);
                frame();
                const auto hb = fil->home().bounds();
                click({hb.x + hb.w * 0.5f, hb.y + hb.h * 0.5f});
                frame();
                const bool toHome = app::licenceWindowSection() == L::kTabHome && home->visible();
                if (auto* link = licRoot->findById("licence.accueil.options")) {
                    const auto lb = link->bounds();
                    click({lb.x + 40.f, lb.y + lb.h * 0.5f});
                }
                frame();
                check(toHome && app::licenceWindowSection() == L::kTabOptions && fil->text() == "Licence \xE2\x80\xBA Options",
                      "un clic sur << Licence >> du fil : l'accueil ; un clic sur la ligne Options : sa rubrique et son fil");
                // le dernier segment : le menu des rubriques de son groupe, et l'accueil
                {
                    const auto cb = fil->current().bounds();
                    click({cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f});
                    frame();
                    ImageRecorder mr;
                    mm.Render(mr, fc);
                    const auto& items = fil->siblings().items();
                    std::vector<std::string> labels;
                    std::size_t purchasesAt = items.size();
                    for (std::size_t i = 0; i < items.size(); ++i) {
                        if (items[i].separator || items[i].heading) continue;
                        labels.push_back(items[i].label);
                        if (items[i].id == L::kTabPurchases) purchasesAt = i;
                    }
                    check(fil->siblingsOpen() && !items.empty() && items[0].heading && items[0].label == "Obtenir ou \xC3\xA9tendre" && labels.size() == 5 &&
                              labels[0] == "Demander ma licence" && labels[2] == "Options" && labels[3] == "Mes achats" && has(labels[4], "accueil"),
                          "le menu du fil : son groupe, ses quatre rubriques, l'accueil");
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
                    frame();
                    check(!fil->siblingsOpen() && mm.depth() == 1 && app::licenceWindowSection() == L::kTabOptions, "\xC3\x89" "chap : le menu se ferme, la fen\xC3\xAAtre reste");
                    click({cb.x + cb.w * 0.5f, cb.y + cb.h * 0.5f});
                    frame();
                    mm.Render(mr, fc);
                    if (purchasesAt < items.size()) {
                        const auto ir = fil->siblings().itemRect(purchasesAt);
                        click({ir.x + 30.f, ir.y + ir.h * 0.5f});
                    }
                    frame();
                    check(!fil->siblingsOpen() && app::licenceWindowSection() == L::kTabPurchases, "un clic dans le menu : Mes achats");
                }
                // Retour arriere, Alt+gauche : l'accueil ; dans un champ, Retour arriere efface (on reste)
                (void)mm.HandleEvent(ui::KeyDown{ui::Key::Backspace, {}, false});
                frame();
                const bool backspace = app::licenceWindowSection() == L::kTabHome;
                app::showLicenceWindow(mm, L::kTabEditions);
                frame();
                ui::KeyMods alt;
                alt.alt = true;
                (void)mm.HandleEvent(ui::KeyDown{ui::Key::Left, alt, false});
                frame();
                check(backspace && app::licenceWindowSection() == L::kTabHome, "Retour arri\xC3\xA8re, Alt+gauche : l'accueil");
                {
                    licensing::service::Address a;
                    a.url = "http://127.0.0.1:1";   // un service suppose (rien ne l'appelle ici) : le champ du code
                    ls::setAddressForTests(a);
                    app::showLicenceWindow(mm, L::kTabActivation);
                    frame();
                    auto* code = dynamic_cast<ui::InputText*>(licRoot->findById("licence.code.champ"));
                    if (code) {
                        const auto cr = code->bounds();
                        click({cr.x + 20.f, cr.y + cr.h * 0.5f});
                        (void)mm.HandleEvent(ui::TextInput{"AB"});
                        (void)mm.HandleEvent(ui::KeyDown{ui::Key::Backspace, {}, false});
                        frame();
                    }
                    check(code && code->text() == "A" && app::licenceWindowSection() == L::kTabActivation,
                          "dans le champ du code, Retour arri\xC3\xA8re efface (la rubrique reste) : " + (code ? code->text() : std::string("pas de champ")));
                    if (code) code->setText({});
                }

                // -- MON PROFIL : sa fenetre a part (les appels d'avant : kTabProfile), << Modifier mon profil... >> dans la demande
                app::showLicenceWindow(mm, L::kTabProfile);
                frame();
                check(mm.depth() == 2 && mm.top() && mm.top()->id() == "dialog.profil" && app::profileWindowOpen() && app::licenceWindowSection() == L::kTabActivation,
                      "kTabProfile : la fen\xC3\xAAtre Mon profil, par-dessus ; la fen\xC3\xAAtre Licence reste sur sa rubrique");
                app::showProfileWindow(mm);
                frame();
                check(mm.depth() == 2, "une seule fen\xC3\xAAtre Mon profil");
                (void)mm.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
                frame();
                check(mm.depth() == 1 && !app::profileWindowOpen() && app::licenceWindowOpen() && rootOf() == licRoot, "\xC3\x89" "chap : Mon profil se ferme, la fen\xC3\xAAtre Licence reste");
                {
                    const std::string commun = U(base / "commun-146");
                    setEnvVar("XPG_DOSSIER_COMMUN", commun);   // le profil d'essai (jamais celui de ce PC)
                    app::showLicenceWindow(mm, L::kTabRequest);
                    frame();
                    auto* customer = dynamic_cast<ui::InputText*>(licRoot->findById("licence.demande.client"));
                    auto* contact = dynamic_cast<ui::InputText*>(licRoot->findById("licence.demande.contact"));
                    auto* link = button("licence.demande.profil");
                    if (customer) customer->setText({});
                    if (contact) contact->setText({});
                    check(link && link->visible() && link->bounds().w > 0.f && has(link->text(), "Modifier mon profil"),
                          "la demande : << Modifier mon profil... >> pr\xC3\xA8s du formulaire");
                    if (link) link->clicked->emit();
                    frame();
                    const auto field = [&](const char* id) { return rootOf() ? dynamic_cast<ui::InputText*>(rootOf()->findById(id)) : nullptr; };
                    const bool opened = mm.depth() == 2 && mm.top()->id() == "dialog.profil";
                    if (auto* f = field("licence.profil.societe")) f->setText("Client d'essai Atelier");
                    if (auto* f = field("licence.profil.nom")) f->setText("Camille Essai");
                    if (auto* f = field("licence.profil.courriel")) f->setText("camille.essai@exemple.fr");
                    if (auto* save = button("licence.profil.enregistrer")) save->clicked->emit();
                    frame();
                    check(opened && customer && contact && customer->text() == "Client d'essai Atelier" && contact->text() == "Camille Essai" &&
                              has(L::loadProfile().email, "camille.essai"),
                          "Modifier mon profil... : sa fen\xC3\xAAtre ; enregistr\xC3\xA9, il remplit la demande (" + (customer ? customer->text() : std::string()) + ")");
                    if (auto* f = field("licence.profil.societe")) f->setText("Client d'essai Bis");
                    if (auto* save = button("licence.profil.enregistrer")) save->clicked->emit();
                    frame();
                    const bool followed = customer && customer->text() == "Client d'essai Bis";
                    if (customer) customer->setText("Ma soci\xC3\xA9t\xC3\xA9 d'essai");
                    if (auto* f = field("licence.profil.societe")) f->setText("Client d'essai Ter");
                    if (auto* save = button("licence.profil.enregistrer")) save->clicked->emit();
                    frame();
                    check(followed && customer && customer->text() == "Ma soci\xC3\xA9t\xC3\xA9 d'essai",
                          "le profil modifi\xC3\xA9 suit dans la demande ; ce que tu y as tap\xC3\xA9 toi-m\xC3\xAAme reste");
                    if (auto* shut = button("dialog.profil.fermer")) shut->clicked->emit();
                    frame();
                    check(mm.depth() == 1 && !app::profileWindowOpen(), "Fermer : Mon profil se ferme");
                    setEnvVar("XPG_DOSSIER_COMMUN", {});
                }

                // -- LES BARREES : leurs captures (l'apercu, en grand) ; en grand, on change de fonctionnalite
                check(writeCapture(shots / "hmi_editor.png", 640, 400) && writeCapture(shots / "hmi_editor-2.png", 320, 200) &&
                          writeCapture(shots / "ai_generation.png", 480, 300),
                      "des captures d'essai pour deux fonctionnalit\xC3\xA9s que CUSTOM n'ouvre pas");
                ls::setAddressForTests(licensing::service::Address{});
                app::showLicenceWindow(mm, L::kTabFeatures);
                frame();
                auto* table = dynamic_cast<ui::TableView*>(licRoot->findById("dialog.licence.fonctionnalites"));
                std::size_t row = static_cast<std::size_t>(-1), featureRows = 0;
                if (table && table->model()) {
                    featureRows = table->model()->rowCount();
                    for (std::size_t r = 0; r < featureRows; ++r)
                        if (table->model()->cellText(r, 1) == "hmi_editor") row = r;
                }
                const bool struck = row < featureRows && table->model()->cellStyle(static_cast<ui::RowIndex>(row), 0).locked;
                if (row < featureRows) table->selectModelRows({static_cast<ui::RowIndex>(row)}, true);
                frame();
                auto* cap = dynamic_cast<L::CaptureView*>(licRoot->findById("dialog.licence.fonctionnalites.panneau.apercu.capture"));
                check(struck && cap && cap->hasImage() && cap->imageWidth() == 640.f && cap->bounds().w > 0.f,
                      "une fonctionnalit\xC3\xA9 barr\xC3\xA9" "e (pas dans ta licence) : sa capture dans l'aper\xC3\xA7u");
                if (cap) {
                    const auto cb = cap->bounds();
                    click({cb.x + cb.w * 0.3f, cb.y + cb.h * 0.8f});
                }
                frame();
                auto* zoom = dynamic_cast<L::CaptureZoomBody*>(rootOf());
                check(mm.depth() == 2 && zoom && zoom->feature() == "hmi_editor" && zoom->locked() && zoom->lockedText() == "pas dans ta licence" &&
                          zoom->shotCount() == 2 && zoom->image().imageWidth() == 640.f && zoom->featureCount() == featureRows &&
                          zoom->featureIndex() == static_cast<int>(row),
                      "en grand : la barr\xC3\xA9" "e, ses deux captures, << pas dans ta licence >>, et toute la liste du tableau (" +
                          std::to_string(zoom ? zoom->featureCount() : 0) + ")");
                if (zoom) {
                    ImageRecorder zr;
                    mm.Render(zr, fc);
                    check(zr.saw("pas dans ta licence") && zr.saw("Fonctionnalit\xC3\xA9 " + std::to_string(row + 1) + "/" + std::to_string(featureRows)) &&
                              zoom->featureList() && zoom->featureList()->visible() && zoom->featureList()->items().size() == featureRows &&
                              zoom->featureList()->selectedIndex() == static_cast<int>(row),
                          "en grand : la liste des fonctionnalit\xC3\xA9s, << Fonctionnalit\xC3\xA9 " + std::to_string(row + 1) + "/" + std::to_string(featureRows) +
                              " >>, la marque << pas dans ta licence >>");
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::PageDown, {}, false});
                    frame();
                    const bool down = zoom->featureIndex() == static_cast<int>((row + 1) % featureRows);
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::PageUp, {}, false});
                    frame();
                    check(down && zoom->feature() == "hmi_editor" && mm.depth() == 2, "Page suivante, Page pr\xC3\xA9" "c\xC3\xA9" "dente : la fonctionnalit\xC3\xA9 d'\xC3\xA0 c\xC3\xB4t\xC3\xA9, sans fermer");
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Right, {}, false});
                    check(zoom->shot() == 1 && zoom->image().imageWidth() == 320.f, "les fl\xC3\xA8" "ches : toujours les captures de la fonctionnalit\xC3\xA9");
                    check(zoom->showFeature("api_editor") && !zoom->locked() && zoom->shotCount() == 4 && zoom->shot() == 0 && zoom->image().imageWidth() == 3200.f &&
                              zoom->title().find(L::featureLabel("api_editor")) == 0,
                          "une autre (dans la licence) : ses quatre captures, sans la marque");
                    const int at = zoom->featureIndex();
                    if (auto* btn = button("dialog.capture.fonction-precedente")) btn->clicked->emit();
                    frame();
                    const bool prev = zoom->featureIndex() == static_cast<int>((static_cast<std::size_t>(at) + featureRows - 1) % featureRows);
                    if (auto* btn = button("dialog.capture.fonction-suivante")) btn->clicked->emit();
                    if (zoom->featureList()) zoom->featureList()->pick(static_cast<int>(row));
                    frame();
                    check(prev && zoom->feature() == "hmi_editor" && zoom->locked() && zoom->featureList()->selectedIndex() == static_cast<int>(row),
                          "ses fl\xC3\xA8" "ches et la liste : la pr\xC3\xA9" "c\xC3\xA9" "dente, la suivante, une au choix (la barr\xC3\xA9" "e : ses captures)");
                    check(!zoom->showFeature("pas_une_fonction"), "une fonctionnalit\xC3\xA9 hors de la liste : rien");
                    (void)mm.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
                    frame();
                    check(mm.depth() == 1 && rootOf() == licRoot, "\xC3\x89" "chap : la capture en grand se ferme");
                }
                // la boutique : une offre choisie, ce qu'elle n'a pas (barre) - sa capture, et en grand << pas dans PERSONAL >>
                {
                    licensing::service::Address a;
                    a.url = "http://127.0.0.1:1";
                    ls::setAddressForTests(a);
                    L::setWantedOffer("PERSONAL");
                    app::showLicenceWindow(mm, L::kTabRequest);
                    frame();
                    auto* list = dynamic_cast<ui::TableView*>(licRoot->findById("licence.demande.offre.liste"));
                    std::size_t r2 = static_cast<std::size_t>(-1);
                    if (list && list->model())
                        for (std::size_t r = 0; r < list->model()->rowCount(); ++r)
                            if (list->model()->cellText(r, 0) == L::featureLabel("ai_generation")) r2 = r;
                    const bool out = list && r2 < list->model()->rowCount() && list->model()->cellStyle(static_cast<ui::RowIndex>(r2), 0).locked;
                    if (out) list->selectModelRows({static_cast<ui::RowIndex>(r2)}, true);
                    frame();
                    auto* shop = dynamic_cast<L::CaptureView*>(licRoot->findById("licence.demande.offre.apercu.capture"));
                    check(out && shop && shop->hasImage() && shop->imageWidth() == 480.f, "la boutique : une fonctionnalit\xC3\xA9 pas dans l'offre (barr\xC3\xA9" "e) - sa capture");
                    if (shop) {
                        const auto sb = shop->bounds();
                        click({sb.x + sb.w * 0.4f, sb.y + sb.h * 0.6f});
                    }
                    frame();
                    auto* z2 = dynamic_cast<L::CaptureZoomBody*>(rootOf());
                    check(z2 && z2->feature() == "ai_generation" && z2->locked() && z2->lockedText() == "pas dans PERSONAL" && z2->featureCount() == featureRows,
                          "en grand : << pas dans PERSONAL >>, toute l'offre \xC3\xA0 c\xC3\xB4t\xC3\xA9 (" + (z2 ? z2->lockedText() : std::string()) + ")");
                    if (z2) (void)mm.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
                    frame();
                }

                // -- LES CARTES DES OFFRES : rien de coupe (le prix, la reduction et sa fin, << 1 PC . 40 jours . 10/21 fonctions >>), a la
                //    taille de la fenetre du client (1302 x 807) et a 1600 x 1000. Un registre plus recent : PERSONAL a 50 EUR, reduit.
                {
                    std::string json = readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json");
                    const lic::Seconds until = now + 40 * lic::kDay;
                    const auto at = json.find("\"label\": \"PERSONAL\",");
                    const auto num = json.find("\"number\": 318");
                    check(at != std::string::npos && num != std::string::npos, "le registre d'exemple : PERSONAL, n\xC2\xB0 318");
                    if (at != std::string::npos && num != std::string::npos) {
                        json.insert(at + std::string("\"label\": \"PERSONAL\",").size(),
                                    " \"price\": \"50 \xE2\x82\xAC HT / par an\", \"promo_price\": \"40 \xE2\x82\xAC HT / par an\", \"promo_until\": \"" + lic::isoTime(until) +
                                        "\", \"description\": \"Pour un poste : l'\xC3\xA9" "diteur, la simulation, l'IHM et ses scripts, l'assistant.\",");
                        json.replace(json.find("\"number\": 318"), 13, "\"number\": 319");
                        const auto ed = json.find("\"max_devices\": 2", at);
                        if (ed != std::string::npos) json.replace(ed, 16, "\"max_devices\": 1");
                        const auto du = json.find("\"duration_days\": 365", at);
                        if (du != std::string::npos) json.replace(du, 20, "\"duration_days\": 40");
                    }
                    std::string reg2, why2;
                    check(adm::signRegistry(*key, json, now, reg2, &why2), "le registre n\xC2\xB0 319 sign\xC3\xA9 (" + why2 + ")");
                    writeFile(base / "livre" / "registre.xpgreg", reg2);
                    app::licence::start(o);
                    check(lic::getLicenseState().registryNumber == 319, "le registre n\xC2\xB0 319 adopt\xC3\xA9");
                    // une mesure proche d'une vraie police (0,52 em par caractere) : la mise en page et le dessin disent la meme
                    ui::PlatformServices svc;
                    svc.measureWidth = [](std::string_view t, gfx::FontId f) {
                        std::size_t glyphs = 0;
                        for (char ch : t)
                            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++glyphs;
                        return static_cast<float>(glyphs) * static_cast<float>(f.v) * 0.52f;
                    };
                    svc.lineHeight = [](gfx::FontId f) { return static_cast<float>(f.v) + 4.f; };
                    ui::installPlatformServices(svc);
                    const std::string end = "R\xC3\xA9" "duction jusqu'au " + ls::dayText(static_cast<std::int64_t>(until));
                    for (const gfx::Size size : {gfx::Size{1302.f, 807.f}, gfx::Size{1600.f, 1000.f}}) {
                        const menu::FrameContext fs{1.0 / 30.0, 1.0, &theme, size, 1.f};
                        app::showLicenceWindow(mm, L::kTabRequest);
                        mm.applyPending();
                        mm.Update(fs);
                        mm.Update(fs);
                        UiRecorder cr;
                        mm.Render(cr, fs);
                        const std::string all = cr.joined();
                        bool cut = false;
                        for (const auto& t : cr.texts)
                            if (has(t, "\xE2\x80\xA6") && (has(t, "\xE2\x82\xAC") || has(t, "fonctions") || has(t, "duction") || has(t, "40 jours")))
                                cut = true;
                        const std::string tag = std::to_string(static_cast<int>(size.w)) + " x " + std::to_string(static_cast<int>(size.h));
                        check(cr.saw("50 \xE2\x82\xAC HT / par an") && cr.saw("40 \xE2\x82\xAC HT / par an") && has(all, end) && cr.saw("1 PC") &&
                                  cr.saw("40 jours") && cr.saw("10/21 fonctions") && !cut,
                              tag + " : les cartes des offres enti\xC3\xA8res - le prix, la r\xC3\xA9" "duction, << " + end + " >>, << 1 PC . 40 jours . 10/21 fonctions >>" +
                                  (cut ? std::string(" (COUP\xC3\x89" "ES)") : std::string()));
                    }
                    ui::installPlatformServices(ui::PlatformServices{});
                    ls::setAddressForTests(licensing::service::Address{});
                    mm.applyPending();
                    mm.Update(fc);
                }
            }
            // la croix de la fenetre Licence : un clic, et elle ne revient pas
            const auto lf = ui::dialogframe::frameOf(*licRoot);
            click({lf.right() - 24.f, lf.y + 21.f});
            frame();
            frame();
            check(mm.depth() == 0 && !app::licenceWindowOpen() && !L::takeAnswer(), "la croix de la fen\xC3\xAAtre Licence : un clic, ferm\xC3\xA9" "e (et rien ne la rouvre)");
            // 1.46.0 : kTabAuto avec une licence valable : l'accueil ; Echap ferme la fenetre, comme avant
            app::showLicenceWindow(mm, L::kTabAuto);
            frame();
            check(mm.depth() == 1 && app::licenceWindowSection() == L::kTabHome, "1.46.0 : kTabAuto, une licence valable : l'accueil (le r\xC3\xA9sum\xC3\xA9, les rubriques)");
            (void)mm.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
            frame();
            frame();
            check(mm.depth() == 0 && !app::licenceWindowOpen() && app::licenceWindowSection() == L::kTabAuto, "1.46.0 : \xC3\x89" "chap ferme la fen\xC3\xAAtre Licence");
            {   // 1.46.0 : l'accueil seul - etroit, une colonne ; plus haut que sa case, la molette le fait defiler (les lignes cachees : pas de clic)
                L::LicenceHome h("essai.accueil");
                int opened = L::kTabAuto;
                h.onOpen = [&opened](int t) { opened = t; };
                h.setBounds({0.f, 0.f, 1200.f, 520.f});
                h.layout();
                const bool wide = h.columns() == 2 && h.contentHeight() <= 520.f;
                h.setBounds({0.f, 0.f, 600.f, 300.f});
                h.layout();
                std::size_t hidden = 0;
                for (const auto& s : L::sections())
                    if (auto* l = h.link(s.tab); l && !l->visible()) ++hidden;
                const bool narrow = h.columns() == 1 && h.contentHeight() > 300.f && hidden > 0;
                (void)h.dispatch(ui::MouseWheel{{100.f, 100.f}, 0.f, -20.f, {}});
                h.layout();
                auto* last = h.link(L::kTabSettings);
                check(wide && narrow && h.scroll() > 0.f && last && last->visible() && last->bounds().bottom() <= 300.f + 0.5f,
                      "l'accueil \xC3\xA9troit : une colonne, la molette le fait d\xC3\xA9" "filer jusqu'\xC3\xA0 Param\xC3\xA8tres");
                if (last) last->clicked->emit();
                check(opened == L::kTabSettings, "une ligne : sa rubrique");
            }
            {   // 1.46.0 : la capture en grand seule - changer de fonctionnalite (ses captures lues a la demande), les barrees comprises
                L::CaptureSet set;
                set.feature = "a";
                set.title = "Alpha";
                L::CaptureZoomBody z(set, 0, [] {});
                std::vector<std::string> loaded;
                z.setChoices({{"a", "Alpha", "API Studio", false, {}}, {"b", "Beta", "IHM Studio", true, "pas dans ta licence"}, {"c", "Gamma", "", false, {}}},
                             [&loaded](const L::CaptureChoice& c) {
                                 loaded.push_back(c.id);
                                 L::CaptureSet s;
                                 s.feature = c.id;
                                 s.title = c.label;
                                 return s;
                             });
                const bool first = z.featureCount() == 3 && z.featureIndex() == 0 && !z.locked() && z.featureNavText() == "Fonctionnalit\xC3\xA9 1/3";
                z.stepFeature(1);
                const bool second = z.feature() == "b" && z.locked() && z.lockedText() == "pas dans ta licence" && z.title() == "Beta" &&
                                    z.featureList() && z.featureList()->selectedIndex() == 1;
                z.stepFeature(-2);
                const bool wrapped = z.feature() == "c" && !z.locked() && z.featureNavText() == "Fonctionnalit\xC3\xA9 3/3";
                if (z.featureList()) z.featureList()->pick(0);
                check(first && second && wrapped && z.feature() == "a" && loaded == std::vector<std::string>{"b", "c", "a"} && !z.showFeature("zz"),
                      "la capture en grand : la suivante (barr\xC3\xA9" "e : << pas dans ta licence >>), en boucle, la liste ; chacune lue \xC3\xA0 la demande");
                L::CaptureZoomBody alone(set, 0, [] {});
                alone.setChoices({{"a", "Alpha", "", false, {}}});
                check(alone.featureList() && !alone.featureList()->visible() && alone.featureNavText().empty(), "une seule fonctionnalit\xC3\xA9 : pas de liste");
            }
            app::licence::setFeatureImagesFolder({});
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
