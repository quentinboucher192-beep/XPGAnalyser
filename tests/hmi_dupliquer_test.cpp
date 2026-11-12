// =============================================================================
//  tests/hmi_dupliquer_test.cpp - 1.50.0 (maquette validee, planche 04) : « DUPLIQUER... » POUR 500 REPERES
// -----------------------------------------------------------------------------
//  Le client : « dans dupliquer si j'ai bcp de choses a dupliquer c'est tronque, mieux gerer l'organisation pour que
//  meme si j'ai 500 placeholders ce soit facile a faire, optimiser les remplacements multi profondeur, pouvoir faire
//  varier que certaines profondeurs, utiliser le meme principe que les formules sur les adressages %MW %M ». Ses
//  reponses (12/11) : i = toujours la 1re profondeur, j la 2e, k la 3e ; une adresse ou un nom deja pris : un
//  avertissement ; un indice hors des bornes : bloque.
//
//  Ce que l'essai verifie (les noms sont fictifs : Depart_0_0, Armoires, Compteur) :
//   - les formules : le calcul des adresses (hmi::types) avec i, j, k, n, o ; %MW(1000 + 10 * i + j), %M(...),
//     (o + n) qui garde la zone ; les textes 'Depart {i+1}.{j+1}', {j:02} ; les noms 'Depart_{i}_{j}' ;
//   - les compteurs : une suite, une grille i x j (j varie le plus vite), + k ; i = la profondeur 1, j la 2, k la 3 ;
//     faire varier certaines profondeurs seulement (Voies[.] fixe) ; un repere a part ;
//   - LA NON-REGRESSION : sans rien regler, le plan est celui d'avant (defaultPlan) et Dupliquer fait les memes copies ;
//   - 512 reperes x 31 copies : construire, passer en grille, changer une regle, dessiner - mesure (une borne) ;
//   - avertit / bloque : une adresse deja prise (une variable, une autre copie), un nom deja pris (avertissent) ;
//     un indice hors des bornes, une formule fausse (bloquent) ;
//   - la liste : groupee, comptee, repliable, cherchable, filtrable, VIRTUELLE (peu de lignes dessinees) ;
//   - « Tableau (Excel) » : 512 x 32 cases, seules les visibles dessinees, # et Nom figes au defilement ;
//   - le texte dans son cadre (l'approche de la 1.47 : un renderer qui mesure avec la vraie police et note chaque
//     texte) a plusieurs tailles de fenetre, petites comprises ;
//   - Dupliquer en UNE commande : un Ctrl+Z retire les 31 x 16 objets.
// =============================================================================
#include "../src/app/hmi/HmiDuplicateDialog.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiDuplicate.hpp"
#include "../src/hmi/HmiDuplicateRules.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiTypes.hpp"
#include "../src/platform/FontAtlas.hpp"
#include "../src/ui/DialogFrame.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/Widget.hpp"
#include "../src/ui/widgets/Containers.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0, g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("ECHEC : %s\n", what.c_str());
    }
}

namespace dup = hmi::dup;
using D = app::HmiDuplicateDialog;
using hmi::Id;
using hmi::Kind;

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
    return b;
}
std::string rectText(const gfx::Rect& r) { return "{" + num(r.x) + "," + num(r.y) + " " + num(r.w) + "x" + num(r.h) + "}"; }

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ------------------------------------------------------------ le renderer ----
//  Celui de la 1.47 (session_ui_test), sans l'image : les mesures de la vraie police (FontAtlas), chaque texte
//  dessine note avec son rectangle et la decoupe en cours.
class TextRecorder final : public gfx::IRenderer {
public:
    struct Text {
        gfx::Rect   rect, clip;
        std::string text;
    };
    std::vector<Text> texts;
    void beginFrame(gfx::Color) override {
        texts.clear();
        clips_.clear();
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
    void fillTriangles(const gfx::Vertex*, std::size_t) override {}
    void drawText(gfx::Point at, std::string_view s, gfx::FontId f, gfx::Color) override {
        if (s.empty()) return;
        const gfx::Rect clip = clips_.empty() ? gfx::Rect{-1e6f, -1e6f, 2e6f, 2e6f} : clips_.back();
        texts.push_back({{at.x, at.y, measure(s, f).width, lineHeight(f)}, clip, std::string(s)});
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        gfx::FontAtlas* face = faceFor(f);
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        if (!face) return {static_cast<float>(s.size()) * h * 0.5f, h, h * 0.8f, h * 0.2f};
        gfx::TextMetrics m;
        m.width = face->measure(s);
        m.height = face->lineHeight();
        return m;
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override {
        gfx::FontAtlas* face = faceFor(f);
        return face ? face->lineHeight() : (f.v ? static_cast<float>(f.v) : 16.f);
    }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float maxWidth) const override {
        if (maxWidth <= 0.f) return 0;
        gfx::FontAtlas* face = faceFor(f);
        return face ? face->fitBytes(s, maxWidth) : std::min(s.size(), static_cast<std::size_t>(maxWidth / 8.f));
    }
    [[nodiscard]] bool fontsReady() const { return faceFor(gfx::FontId{16}) != nullptr; }
    gfx::Size size{1920.f, 1080.f};

private:
    gfx::FontAtlas* faceFor(gfx::FontId f) const {
        const int px = std::max(1, static_cast<int>(f.v ? f.v : 16));
        const int key = px * 4 + f.w;
        if (auto it = faces_.find(key); it != faces_.end()) return it->second.ready() ? &it->second : nullptr;
        gfx::FontAtlas atlas;
        for (const auto& path : gfx::interfaceFontCandidates(f.w))
            if (atlas.load(path, static_cast<float>(px))) break;
        auto& kept = faces_.emplace(key, std::move(atlas)).first->second;
        return kept.ready() ? &kept : nullptr;
    }
    std::vector<gfx::Rect> clips_;
    mutable std::map<int, gfx::FontAtlas> faces_;
};

TextRecorder* g_metrics = nullptr;
void installMetrics(TextRecorder& r) {
    g_metrics = &r;
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) { return g_metrics->measure(t, f).width; };
    s.lineHeight = [](gfx::FontId f) { return g_metrics->lineHeight(f); };
    s.surfaceSize = [] { return g_metrics->surfaceSize(); };
    s.clipboardGet = [] { return std::string{}; };
    s.clipboardSet = [](std::string_view) {};
    ui::installPlatformServices(std::move(s));
}

void paintInto(TextRecorder& r, ui::Widget& w, gfx::Size size) {
    r.size = size;
    r.beginFrame({});
    w.setBounds({0.f, 0.f, size.w, size.h});
    w.layout();
    std::vector<ui::Widget*> overlays;
    const auto theme = ui::Theme::xpg();
    w.render(ui::PaintContext{r, theme, {0.f, 0.f, size.w, size.h}, 0.0, &overlays});
}

// Les regles du cadre (1.47) : chaque texte dessine tient dans le cadre en largeur, sa partie visible en hauteur, et
// n'est jamais coupe a droite par une decoupe.
int textsOutside(const TextRecorder& r, const gfx::Rect& frame, const std::string& where, int maxReports = 6) {
    int bad = 0;
    for (const auto& t : r.texts) {
        const gfx::Rect seen = t.rect.intersect(t.clip);
        if (seen.empty()) continue;
        const bool xOk = t.rect.x >= frame.x - 0.5f && t.rect.right() <= frame.right() + 0.5f;
        const bool yOk = seen.y >= frame.y - 0.5f && seen.bottom() <= frame.bottom() + 0.5f;
        const bool clipOk = t.rect.x >= t.clip.x - 0.5f && t.rect.right() <= t.clip.right() + 0.5f;
        if (!xOk || !yOk || !clipOk) {
            if (bad < maxReports)
                std::printf("   %s : << %s >> %s hors de %s (decoupe %s)\n", where.c_str(), t.text.c_str(), rectText(t.rect).c_str(), rectText(frame).c_str(),
                            rectText(t.clip).c_str());
            ++bad;
        }
    }
    return bad;
}

void collectButtons(ui::Widget& w, std::vector<ui::Button*>& out) {
    for (const auto& c : w.children()) {
        if (!c->visible()) continue;
        if (auto* b = dynamic_cast<ui::Button*>(c.get())) out.push_back(b);
        collectButtons(*c, out);
    }
}

// Les boutons visibles d'une page : dans le cadre, sans se chevaucher, leur libelle tient.
int buttonProblems(ui::Widget& root, const gfx::Rect& frame, const std::string& where) {
    std::vector<ui::Button*> bs;
    collectButtons(root, bs);
    int bad = 0;
    for (std::size_t i = 0; i < bs.size(); ++i) {
        const auto b = bs[i]->bounds();
        if (b.w <= 0.f || b.h <= 0.f) continue;
        if (b.x < frame.x - 0.5f || b.y < frame.y - 0.5f || b.right() > frame.right() + 0.5f || b.bottom() > frame.bottom() + 0.5f) {
            std::printf("   %s : le bouton << %s >> %s hors de %s\n", where.c_str(), bs[i]->text().c_str(), rectText(b).c_str(), rectText(frame).c_str());
            ++bad;
        }
        const float tw = ui::measureWidth(bs[i]->text(), gfx::FontId{16});
        if (tw + 12.f > b.w + 0.5f && !bs[i]->compact()) {
            std::printf("   %s : le libelle << %s >> (%s px) deborde du bouton (%s px)\n", where.c_str(), bs[i]->text().c_str(), num(tw).c_str(), num(b.w).c_str());
            ++bad;
        }
        for (std::size_t j = i + 1; j < bs.size(); ++j) {
            const auto o = bs[j]->bounds();
            if (o.w <= 0.f || o.h <= 0.f) continue;
            if (!b.intersect(o).inset(0.5f, 0.5f).empty()) {
                std::printf("   %s : << %s >> et << %s >> se chevauchent\n", where.c_str(), bs[i]->text().c_str(), bs[j]->text().c_str());
                ++bad;
            }
        }
    }
    return bad;
}

// ------------------------------------------------------------- un projet ----
struct Bench {
    std::shared_ptr<hmi::Document> doc = std::make_shared<hmi::Document>();
    Id view{hmi::kNoId};
    Bench() {
        hmi::View v = hmi::makeView(doc->project, "Vue_Armoires");
        v.width = 1920;
        v.height = 1080;
        view = v.id;
        doc->project.views.push_back(v);
    }
    hmi::View& v() { return *doc->project.view(view); }
    void variable(const std::string& name, const std::string& type, const std::string& address = {}) {
        hmi::Variable var;
        var.id = doc->project.allocate();
        var.name = name;
        var.type = type;
        var.address = address;
        doc->project.programs.variables.push_back(var);
    }
    Id object(Kind k, const std::string& name, double x, double y) {
        const Id id = hmi::edit::add(doc->project, v(), k, x, y);
        auto* o = v().object(id);
        o->name = name;
        o->setNumber("w", 20);
        o->setNumber("h", 20);
        return id;
    }
};

std::unique_ptr<D> open(Bench& b, const std::vector<Id>& sel, int copies) {
    D::Spec spec;
    spec.doc = b.doc;
    spec.view = b.view;
    spec.selection = sel;
    spec.copies = copies;
    spec.exists = [](std::string_view) { return true; };
    return std::make_unique<D>(std::move(spec));
}

// ------------------------------------------------------------ les formules ---
void formules() {
    std::printf("-- les formules : le calcul des adresses, avec i, j, k, n, o\n");
    dup::Counters grid;
    grid.mode = dup::CountMode::Grid;
    grid.grid = {{0, 3}, {0, 7}};
    check(grid.places() == 32, "4 armoires x 8 departs = 32 places");
    const auto p1 = dup::placeAt(grid, 1), p8 = dup::placeAt(grid, 8), p31 = dup::placeAt(grid, 31);
    check(p1.i == 0 && p1.j == 1 && p8.i == 1 && p8.j == 0 && p31.i == 3 && p31.j == 7 && p31.n == 31,
          "j varie le plus vite : place 1 = (0, 1), place 8 = (1, 0), place 31 = (3, 7)");
    const dup::Vars at{p31, 5};
    long long v = 0;
    std::string why;
    check(dup::evalInteger("i", grid, at, v) && v == 3 && dup::evalInteger("j", grid, at, v) && v == 7, "i = 3, j = 7");
    check(dup::evalInteger("o + n", grid, at, v) && v == 36, "o + n : 5 + 31 = 36 (le Suivre d'avant)");
    check(dup::evalInteger("5 + 4 * i", grid, at, v) && v == 17, "5 + 4 * i = 17");
    check(dup::evalInteger("(i * 8 + j) MOD 5", grid, at, v) && v == 1, "MOD et parentheses : (3 * 8 + 7) MOD 5 = 1");
    check(!dup::evalInteger("k + 1", grid, at, v, &why) && why.find("k inconnu") != std::string::npos,
          "k sans troisieme compteur : refuse (" + why + ")");
    why.clear();
    check(!dup::evalInteger("10 / (j - 7)", grid, at, v, &why) && why.find("z\xC3\xA9ro") != std::string::npos, "division par zero (" + why + ")");
    check(dup::evalAddress("%MW(1000 + 10 * i + j)", "%MW1000", grid, at) == "%MW1037", "%MW(1000 + 10 * i + j) -> %MW1037");
    check(dup::evalAddress("%M(200 + 8 * i + j)", "%M200", grid, at) == "%M231", "%M(200 + 8 * i + j) -> %M231 (un bit)");
    check(dup::evalAddress("(o + n)", "%M200", grid, {p31, 200}) == "%M231", "(o + n) garde la zone de l'original : %M200 -> %M231");
    check(dup::evalAddress("(o + i)", "%MW10.3", grid, {p31, 10}) == "%MW13.3", "(o + i) garde aussi le bit : %MW10.3 -> %MW13.3");
    check(dup::evalText("'D\xC3\xA9part {i+1}.{j+1}'", grid, at) == "D\xC3\xA9part 4.8", "'Depart {i+1}.{j+1}' -> Depart 4.8");
    check(dup::evalText("'D{i}{j:02}'", grid, {dup::placeAt(grid, 7), 0}) == "D007", "'D{i}{j:02}' -> D007 (deux chiffres)");
    check(dup::evalText("'Depart_{i}_{j}'", grid, at) == "Depart_3_7", "un nom : 'Depart_{i}_{j}' -> Depart_3_7");
    check(dup::evalText("{{i}} {i}", grid, at) == "{i} 3", "{{ et }} : une accolade");
    why.clear();
    check(dup::evalText("'D{i'", grid, at, &why).empty() && !why.empty(), "une accolade qui manque : refuse (" + why + ")");
    // Le calcul est celui de « Modifier l'adresse » (hmi::types) : la meme ecriture.
    check(hmi::types::namedFormulaAddress("%MW(1000 + 10 * i)", {{"i", 2}}) == hmi::types::formulaAddress("%MW(1000 + 10 * i)", {2}),
          "le meme calcul que les adresses par formule (%MW1020)");
    const auto a = dup::addressOf("%MW1000");
    check(a && a->area == "%MW" && a->number == 1000 && !dup::addressOf("Vanne") && !dup::addressOf("%MW") && dup::addressOf("%M12.3"),
          "une adresse ecrite : %MW1000, %M12.3 ; pas Vanne");
}

// ---------------------------------------------------- la non-regression ----
//  Sans rien regler, le plan est celui de defaultPlan (la 1.49) et Dupliquer fait les memes copies.
bool samePlan(const dup::Plan& a, const dup::Plan& b, std::string& diff) {
    if (a.rows.size() != b.rows.size()) {
        diff = std::to_string(a.rows.size()) + " lignes contre " + std::to_string(b.rows.size());
        return false;
    }
    for (std::size_t r = 0; r < a.rows.size(); ++r) {
        const auto& x = a.rows[r];
        const auto& y = b.rows[r];
        if (x.name != y.name) diff = "ligne " + std::to_string(r) + " : nom " + x.name + " / " + y.name;
        else if (x.indices != y.indices) diff = "ligne " + std::to_string(r) + " : indices";
        else {
            // Une valeur vide et une absente se valent (une case vide).
            std::map<std::string, std::string> mx, my;
            for (const auto& [k, val] : x.markers) if (!val.empty()) mx[k] = val;
            for (const auto& [k, val] : y.markers) if (!val.empty()) my[k] = val;
            if (mx != my) diff = "ligne " + std::to_string(r) + " : reperes";
        }
        if (!diff.empty()) return false;
    }
    return true;
}

std::string objectsText(const hmi::View& v) {
    std::string out;
    for (const auto& o : v.objects) {
        out += o.name + "|";
        for (const auto& p : o.props) out += p.key + "=" + p.value + "/" + p.expr + ";";
        out += "\n";
    }
    return out;
}

void nonRegression() {
    std::printf("-- la non-regression : sans rien regler, le plan et les copies d'avant\n");
    struct Case {
        std::string what;
        std::function<std::vector<Id>(Bench&)> make;
        int copies;
    };
    const std::vector<Case> cases = {
        {"$Vanne$, $Repere$ et V[0] hors repere (suivi)", [](Bench& b) {
             b.variable("V", "ARRAY[0..7] OF INT");
             const Id a = b.object(Kind::Rectangle, "V_101", 100, 100);
             b.v().object(a)->setExpr("fill", "=$Vanne$.OUV");
             b.v().object(a)->set("text", "$Repere$");
             b.v().object(a)->setExpr("visible", "V[0].Pos > 0 AND Armoires[0].prete");
             b.v().object(a)->setExpr("opening", "$Pompe$.Vitesse");   // deux reperes vides : pas de preremplissage
             return std::vector<Id>{a};
         }, 5},
        {"des morceaux a indices ($V[1].Ouv$, $V[0]$) et V[0] hors repere (garde)", [](Bench& b) {
             b.variable("V", "ARRAY[0..63] OF REAL");
             const Id a = b.object(Kind::Rectangle, "Valve_1", 130, 90);
             b.v().object(a)->setExpr("value", "$V[1].Ouv$");
             b.v().object(a)->setExpr("label", "$V[0]$.Nom");
             b.v().object(a)->setExpr("opening", "V[0].Pos");
             b.v().object(a)->setExpr("moving", "$V[0].Pos > 10$");
             return std::vector<Id>{a};
         }, 3},
        {"des indices seuls, aux bornes trop courtes (Armoires 0..1, 3 copies : gardes)", [](Bench& b) {
             b.variable("Armoires", "ARRAY[0..1] OF INT");
             const Id a = b.object(Kind::Rectangle, "Cadre", 10, 10);
             b.v().object(a)->setExpr("fill", "Armoires[0] > 0");
             const Id c = b.object(Kind::Rectangle, "Cadre_2", 40, 10);
             b.v().object(c)->setExpr("fill", "T[1, 2] > 0 AND Q[3]");
             return std::vector<Id>{a, c};
         }, 3},
        {"deux dimensions et deux profondeurs dans un repere ($M[1, 2].A[3]$)", [](Bench& b) {
             const Id a = b.object(Kind::Rectangle, "M_1", 10, 10);
             b.v().object(a)->setExpr("fill", "$M[1, 2].A[3]$ > 0");
             b.v().object(a)->setExpr("visible", "W[2]");
             return std::vector<Id>{a};
         }, 4},
        {"rien a remplacer (un objet seul)", [](Bench& b) { return std::vector<Id>{b.object(Kind::Rectangle, "Rect3", 10, 10)}; }, 2},
        {"Remplacer... (0 copie)", [](Bench& b) {
             const Id a = b.object(Kind::Rectangle, "V_7", 10, 10);
             b.v().object(a)->setExpr("fill", "$V[2]$ + $Seuil$");
             return std::vector<Id>{a};
         }, 0},
    };
    for (const auto& c : cases) {
        Bench b;
        const auto sel = c.make(b);
        const auto d = open(b, sel, c.copies);
        const auto sc = dup::scan(b.v(), sel, dup::projectBounds(b.doc->project));
        auto before = dup::defaultPlan(b.v(), sc, c.copies);
        std::string diff;
        check(samePlan(d->plan(), before, diff), c.what + " : le plan d'avant (" + diff + ")");
        // Les copies : les memes objets, champ pour champ.
        if (c.copies > 0 && d->canConfirm()) {
            hmi::Project p1 = b.doc->project, p2 = b.doc->project;
            auto plan1 = before;
            plan1.layout = d->plan().layout;
            (void)dup::apply(p1, *p1.view(b.view), plan1);
            (void)dup::apply(p2, *p2.view(b.view), d->plan());
            check(objectsText(*p1.view(b.view)) == objectsText(*p2.view(b.view)), c.what + " : les memes copies que la 1.49");
        }
    }
    // Les reglages d'avant passent par les regles : Suivre V[0] (decoche / coche), Garder une colonne.
    Bench b;
    b.variable("V", "ARRAY[0..7] OF INT");
    const Id a = b.object(Kind::Rectangle, "V_101", 100, 100);
    b.v().object(a)->setExpr("visible", "V[0].Pos > 0");
    b.v().object(a)->setExpr("fill", "=$Vanne$.OUV");
    const auto d = open(b, {a}, 3);
    const std::string key = dup::indexKey("V", 0);
    check(d->following(0) && d->plan().rows[2].indices.at(key) == 2, "Suivre V[0] coche d'office : la ligne 3 lit V[2]");
    d->setFollow(0, false);
    check(!d->following(0) && d->plan().rows[2].indices.empty(), "decoche : V[0] reste V[0] (sa profondeur : fixe)");
    d->setFollow(0, true);
    const auto* gv = d->findGroup("V");
    check(gv && d->rules().rule(*gv).formula == "o + n" && d->following(0),
          "recoche : la regle de V[.] redevient o + n (" + (gv ? gv->label + " : " + d->rules().rule(*gv).formula : std::string("introuvable")) + ")");
}

// --------------------------------------------------------- 500 reperes ----
struct Big {
    Bench b;
    std::vector<Id> sel;
    std::size_t addressQ0{0};
    Big() {
        b.variable("Armoires", "ARRAY[0..3] OF T_Armoire");
        b.variable("Compteur", "INT", "%MW1037");      // une adresse deja prise (la derniere copie de %MW(1000 + 10 * i + j))
        // 512 reperes sur 16 objets (32 chacun) : 320 Voies[5] (3 profondeurs), 128 Mesures[0..3], 20 Armoires[0] seul,
        // 16 adresses %MW, 28 textes.
        std::vector<std::pair<std::string, bool>> markers;   // (texte du champ, expression)
        for (int k = 0; k < 320; ++k) markers.push_back({"$Armoires[0].Departs[0].Voies[5].S" + std::to_string(k) + "$", true});
        for (int m = 0; m < 4; ++m)
            for (int k = 0; k < 32; ++k) markers.push_back({"$Armoires[0].Departs[0].Mesures[" + std::to_string(m) + "].V" + std::to_string(k) + "$", true});
        for (int k = 0; k < 20; ++k) markers.push_back({"$Armoires[0].Info" + std::to_string(k) + "$", true});
        for (int q = 0; q < 16; ++q) markers.push_back({"$%MW" + std::to_string(1000 + 40 * q) + "$", true});
        for (int k = 0; k < 28; ++k) markers.push_back({"$Titre" + std::to_string(k) + "$", false});
        for (int o = 0; o < 16; ++o) {
            const Id id = b.object(o == 0 ? Kind::Rectangle : Kind::Text, o == 0 ? "Depart_0_0" : "Obj_" + std::to_string(o), 10 + 22 * o, 10);
            sel.push_back(id);
            auto* obj = b.v().object(id);
            for (int k = 0; k < 32; ++k) {
                const auto& [text, expr] = markers[static_cast<std::size_t>(o * 32 + k)];
                const std::string prop = "p" + std::to_string(k);
                if (expr) obj->setExpr(prop, text);
                else obj->set(prop, text);
            }
        }
        // Un nom deja pris dans la vue (hors de la selection) : la copie (2, 3) avertit.
        b.object(Kind::Rectangle, "Depart_2_3", 900, 900);
    }
};

void cinqCents() {
    std::printf("-- 512 reperes x 31 copies : les compteurs, les profondeurs, les formules, la mesure\n");
    Big big;
    auto t0 = std::chrono::steady_clock::now();
    const auto d = open(big.b, big.sel, 31);
    const double tOpen = msSince(t0);
    check(d->scanned().markers.size() == 512, "512 reperes lus (" + std::to_string(d->scanned().markers.size()) + ")");
    check(d->plan().copies() == 31, "une suite de 31 copies");
    // Les groupes : Armoires[.] (1), ...Departs[.] (2), ...Voies[.] et ...Mesures[.] (3), Adresses, Textes, Noms.
    const auto* g1 = d->findGroup("Armoires");
    const auto* g2 = d->findGroup("Departs");
    const auto* g3 = d->findGroup("Voies");
    const auto* g4 = d->findGroup("Mesures");
    check(g1 && g2 && g3 && g4 && g1->depth == 1 && g2->depth == 2 && g3->depth == 3 && g4->depth == 3,
          "les profondeurs : Armoires[.] 1, Departs[.] 2, Voies[.] et Mesures[.] 3");
    check(g1 && g1->markers.size() == 468 && g2 && g2->markers.size() == 448 && g3 && g3->markers.size() == 320 && g4 && g4->markers.size() == 128,
          "les reperes de chaque profondeur : 468, 448, 320, 128 (comme la planche)");
    check(g4 && g4->original == "0..3" && g3 && g3->original == "5", "l'original : Voies 5, Mesures 0..3");
    check(d->findGroup("Adresses") && d->findGroup("Adresses")->markers.size() == 16 && d->findGroup("Textes") && d->findGroup("Textes")->markers.size() == 28,
          "16 adresses, 28 textes");
    // ---- la grille i x j : i = la profondeur 1, j la 2, le reste fixe ----
    t0 = std::chrono::steady_clock::now();
    d->setGrid({{0, 3}, {0, 7}});
    const double tGrid = msSince(t0);
    check(d->plan().copies() == 31 && d->rules().counters.mode == dup::CountMode::Grid, "la grille 4 x 8 : 32 places, l'original et 31 copies");
    check(d->rules().rule(*d->findGroup("Armoires")).formula == "i" && d->rules().rule(*d->findGroup("Departs")).formula == "j"
              && d->rules().rule(*d->findGroup("Voies")).kind == dup::RuleKind::Fixed && d->rules().rule(*d->findGroup("Mesures")).kind == dup::RuleKind::Fixed,
          "i = Armoires[.] (profondeur 1), j = Departs[.] (2) ; Voies[.] et Mesures[.] fixes");
    const auto cellAt = [&](int row, const std::string& marker) { return d->cell(row, marker); };
    check(cellAt(31, "$Armoires[0].Departs[0].Voies[5].S7$") == "Armoires[3].Departs[7].Voies[5].S7"
              && cellAt(9, "$Armoires[0].Departs[0].Voies[5].S7$") == "Armoires[1].Departs[1].Voies[5].S7",
          "Voies[5] fixe : ligne 32 Armoires[3].Departs[7].Voies[5], ligne 10 Armoires[1].Departs[1] (" + cellAt(31, "$Armoires[0].Departs[0].Voies[5].S7$") + ")");
    check(cellAt(31, "$Armoires[0].Departs[0].Mesures[2].V4$") == "Armoires[3].Departs[7].Mesures[2].V4", "Mesures[2] reste 2 (fixe)");
    check(cellAt(5, "$Armoires[0].Info3$") == "Armoires[0].Info3" && cellAt(8, "$Armoires[0].Info3$") == "Armoires[1].Info3",
          "Armoires[0].Info3 (profondeur 1 seule) suit i : ligne 9 Armoires[1]");
    // ---- faire varier une profondeur de plus : Voies[.] = 5 + j ----
    t0 = std::chrono::steady_clock::now();
    check(d->setGroupRule("Voies", {dup::RuleKind::Formula, "o + j"}), "la regle de Voies[.] : o + j");
    const double tRule = msSince(t0);
    check(cellAt(31, "$Armoires[0].Departs[0].Voies[5].S7$") == "Armoires[3].Departs[7].Voies[12].S7", "o + j : Voies[5 + 7] = Voies[12]");
    d->setVaries("Voies", false);
    check(cellAt(31, "$Armoires[0].Departs[0].Voies[5].S7$") == "Armoires[3].Departs[7].Voies[5].S7", "Varie ? fixe : Voies[5] de nouveau");
    // ---- les adresses, les textes, les noms ----
    check(d->setGroupRule("Adresses", {dup::RuleKind::Formula, "%MW(o + 10 * i + j)"}), "les adresses : %MW(o + 10 * i + j)");
    check(cellAt(31, "$%MW1000$") == "%MW1037" && cellAt(31, "$%MW1040$") == "%MW1077", "%MW1000 -> %MW1037, %MW1040 -> %MW1077 (ligne 32)");
    check(d->setGroupRule("Textes", {dup::RuleKind::Formula, "'D\xC3\xA9part {i+1}.{j+1}'"}) && cellAt(31, "$Titre3$") == "D\xC3\xA9part 4.8"
              && cellAt(1, "$Titre3$") == "D\xC3\xA9part 1.2",
          "les textes : 'Depart {i+1}.{j+1}' - ligne 2 Depart 1.2, ligne 32 Depart 4.8");
    check(d->setGroupRule("Noms des objets", {dup::RuleKind::Formula, "'Depart_{i}_{j}'"}) && d->cell(31, "Nom") == "Depart_3_7"
              && d->cell(0, "Nom") == "Depart_0_0",
          "les noms : 'Depart_{i}_{j}' (ligne 32 : Depart_3_7 ; l'original garde le sien)");
    // ---- avertit / bloque ----
    bool nameWarn = false, addrWarn = false;
    for (const auto& c : d->conflicts()) {
        nameWarn = nameWarn || (!c.blocks && c.text.find("Depart_2_3 existe d\xC3\xA9j\xC3\xA0") != std::string::npos && c.text.find("Depart_2_3_2") != std::string::npos);
        addrWarn = addrWarn || (!c.blocks && c.text.find("%MW1037 d\xC3\xA9j\xC3\xA0 prise par Compteur") != std::string::npos);
    }
    check(nameWarn, "un nom deja pris (Depart_2_3) : un avertissement, la copie s'appellera Depart_2_3_2");
    check(addrWarn, "une adresse deja prise (%MW1037, Compteur) : un avertissement");
    check(d->canConfirm(), "deux avertissements : Dupliquer reste permis (" + d->blockingText() + ")");
    check(d->blockingText().find("Les 2 avertissements ne bloquent pas") != std::string::npos, "la note le dit : " + d->blockingText());
    // Un repere a part : Armoires[.] = i + 1 pour S0 - Armoires[4] hors des bornes (0..3) : bloque.
    check(d->setOwnFormula("$Armoires[0].Departs[0].Voies[5].S0$", "i + 1", "Armoires"), "S0 a part : Armoires[.] = i + 1");
    check(cellAt(31, "$Armoires[0].Departs[0].Voies[5].S0$") == "Armoires[4].Departs[7].Voies[5].S0", "a part : ligne 32 Armoires[4]");
    check(!d->canConfirm() && !d->conflicts().empty() && d->conflicts().front().blocks
              && d->conflicts().front().text.find("hors des bornes de Armoires (0..3)") != std::string::npos
              && d->conflicts().front().text.find("lignes 25 \xC3\xA0 32") != std::string::npos,
          "hors des bornes : une faute, en tete des conflits, lignes 25 a 32 (" + (d->conflicts().empty() ? std::string("aucun") : d->conflicts().front().text) + ")");
    const auto st = d->stats();
    check(st.faults == 1 && st.conflicts >= 3, "les totaux : 1 faute, des avertissements (" + std::to_string(st.conflicts) + " conflits)");
    d->resetOwn("$Armoires[0].Departs[0].Voies[5].S0$");
    check(d->canConfirm(), "Comme sa profondeur : la faute part");
    // Une formule fausse bloque aussi (k sans troisieme compteur), puis + un compteur k la rend juste.
    d->setGroupRule("Mesures", {dup::RuleKind::Formula, "k"});
    check(!d->canConfirm() && d->blockingText().find("k inconnu") != std::string::npos, "Mesures[.] = k sans k : bloque (" + d->blockingText() + ")");
    d->setCounterK(true);
    check(d->rules().counters.grid.size() == 3 && d->canConfirm() == (d->plan().copies() <= dup::kMaxCopies), "+ un compteur k : la formule est juste");
    d->setCounterK(false);
    d->setVaries("Mesures", false);
    // Les totaux de l'apercu : 31 copies, 512 reperes, les remplacements (les reperes qui changent, copie par copie).
    std::size_t expected = 0;
    for (std::size_t r = 1; r < d->plan().rows.size(); ++r)
        for (const auto& m : d->scanned().markers) {
            const auto it = d->plan().rows[r].markers.find(dup::markerKey(m.name));
            if (it != d->plan().rows[r].markers.end() && !it->second.empty() && it->second != m.name) expected += m.uses;
        }
    const auto s2 = d->stats();
    check(s2.copies == 31 && s2.places == 32 && s2.markers == 512 && s2.replacements == expected && expected > 15000,
          "l'apercu : 31 copies (32 places), 512 reperes, " + std::to_string(s2.replacements) + " remplacements");
    const auto pr = d->previewRows();
    check(pr.size() == 9 && pr[0] == 0 && pr[4] == 4 && pr[5] == -1 && pr[6] == 29 && pr[8] == 31, "l'apercu : les 5 premieres, l'ecart, les 3 dernieres");
    // ---- la liste : groupee, comptee, repliable, cherchable, filtrable ----
    d->expandAll(true);
    std::size_t groups = 0, rows = 0;
    for (const auto& r : d->listRows()) (r.group ? groups : rows) += 1;
    check(rows == 512 && groups >= 5, "la liste depliee : 512 reperes sous " + std::to_string(groups) + " groupes");
    d->expandAll(false);
    check(std::all_of(d->listRows().begin(), d->listRows().end(), [](const D::ListRow& r) { return r.group; }), "Tout replier : les groupes seuls");
    d->expandAll(true);
    d->setSearch("Info1");
    std::size_t found = 0;
    for (const auto& r : d->listRows()) found += r.group ? 0 : 1;
    check(found == 11, "chercher << Info1 >> : Info1, Info10..Info19 (" + std::to_string(found) + ")");
    d->setSearch({});
    d->setListFilter(D::ListFilter::Addresses);
    found = 0;
    for (const auto& r : d->listRows()) found += r.group ? 0 : 1;
    check(found == 16 && d->listCount(D::ListFilter::Addresses) == 16 && d->listCount(D::ListFilter::Indices) == 468 && d->listCount(D::ListFilter::Texts) == 28,
          "les filtres : Adresses 16, Indices 468, Textes 28");
    d->setListFilter(D::ListFilter::Conflicts);
    found = 0;
    for (const auto& r : d->listRows()) found += r.group ? 0 : 1;
    check(found == d->listCount(D::ListFilter::Conflicts) && found >= 1, "Avec un conflit : " + std::to_string(found) + " (l'adresse prise)");
    d->setListFilter(D::ListFilter::All);
    d->setGrouping(D::Grouping::Object);
    groups = 0;
    for (const auto& r : d->listRows()) groups += r.group ? 1 : 0;
    check(groups == 16, "grouper par objet : 16 groupes");
    d->setGrouping(D::Grouping::Depth);
    // ---- la mesure ----
    std::printf("   mesure (Debug) : ouvrir %.0f ms, la grille 4 x 8 %.0f ms, une regle %.0f ms\n", tOpen, tGrid, tRule);
    check(tOpen < 3000.0 && tGrid < 1500.0 && tRule < 1500.0,
          "512 reperes x 31 copies : ouvrir " + std::to_string(static_cast<int>(tOpen)) + " ms, la grille " + std::to_string(static_cast<int>(tGrid))
              + " ms, une regle " + std::to_string(static_cast<int>(tRule)) + " ms (bornes : 3 000, 1 500, 1 500 en Debug)");
    // ---- Dupliquer en UNE commande ----
    const std::size_t before = big.b.v().objects.size();
    core::CommandStack stack;
    dup::Result res;
    const auto plan = d->plan();
    t0 = std::chrono::steady_clock::now();
    (void)stack.push(hmi::changeProject(big.b.doc, "Dupliquer (31 copies)", [&](hmi::Project& p) { res = dup::apply(p, *p.view(big.b.view), plan); }));
    const double tApply = msSince(t0);
    std::printf("   mesure (Debug) : Dupliquer %.0f ms\n", tApply);
    const auto& after = *big.b.doc->project.view(big.b.view);
    check(after.objects.size() == before + 31 * 16 && res.created.size() == 31 * 16,
          "Dupliquer : 31 x 16 objets (" + std::to_string(after.objects.size() - before) + "), les 16 racines de chaque copie (" + std::to_string(res.created.size()) + ")");
    const hmi::Object* last = nullptr;
    for (const auto& o : after.objects)
        if (o.name == "Depart_3_7") last = &o;
    check(last && last->expr("p7") == "$Armoires[3].Departs[7].Voies[5].S7$", "la copie Depart_3_7 : $Armoires[3].Departs[7].Voies[5].S7$ (ses $ gardes)");
    bool renamed = false;
    for (const auto& o : after.objects) renamed = renamed || o.name == "Depart_2_3_2";
    check(renamed, "le nom pris : la copie s'appelle Depart_2_3_2");
    check(static_cast<bool>(stack.undo()) && big.b.doc->project.view(big.b.view)->objects.size() == before, "un seul Ctrl+Z retire les 496 objets");
    check(static_cast<bool>(stack.redo()) && big.b.doc->project.view(big.b.view)->objects.size() == before + 31 * 16, "Ctrl+Y les rend");
}

// ------------------------------------------------------------ l'ecran ----
void ecran(TextRecorder& r) {
    std::printf("-- l'ecran : le texte dans son cadre, la liste et le tableau virtuels\n");
    Big big;
    const auto d = open(big.b, big.sel, 31);
    check(static_cast<bool>(d->Initialize()), "la fenetre se construit");
    d->setGrid({{0, 3}, {0, 7}});
    d->setGroupRule("Adresses", {dup::RuleKind::Formula, "%MW(o + 10 * i + j)"});
    d->setGroupRule("Textes", {dup::RuleKind::Formula, "'D\xC3\xA9part {i+1}.{j+1}'"});
    d->setGroupRule("Noms des objets", {dup::RuleKind::Formula, "'Depart_{i}_{j}'"});
    ui::Widget* root = d->widgetRoot();
    check(root != nullptr, "la racine");
    if (!root) return;
    const gfx::Size sizes[] = {{1024.f, 700.f}, {1280.f, 800.f}, {1920.f, 1080.f}, {900.f, 620.f}};
    for (const auto& size : sizes) {
        ui::dialogframe::reset();
        for (std::size_t tab = 0; tab < 3; ++tab) {
            d->showTab(tab);
            const auto t0 = std::chrono::steady_clock::now();
            paintInto(r, *root, size);
            const double ms = msSince(t0);
            const gfx::Rect panel = d->panelRect();
            const std::string where = std::to_string(static_cast<int>(size.w)) + " x " + std::to_string(static_cast<int>(size.h)) + ", onglet " + std::to_string(tab + 1);
            check(panel.w > 0.f && panel.right() <= size.w + 0.5f && panel.bottom() <= size.h + 0.5f, where + " : le cadre " + rectText(panel) + " dans la fenetre");
            const int out = textsOutside(r, panel, where);
            check(out == 0, where + " : aucun texte hors de son cadre (" + std::to_string(out) + ")");
            const int bad = buttonProblems(*root, panel, where);
            check(bad == 0, where + " : les boutons dans le cadre, leur libelle tient, sans chevauchement (" + std::to_string(bad) + ")");
            if (size.w == 1280.f) std::printf("   %s : une image en %.0f ms (%zu textes)\n", where.c_str(), ms, r.texts.size());
            if (tab == 0 && size.w == 1280.f) {
                // La liste est virtuelle : 512 reperes, une poignee de lignes dessinees.
                std::size_t markerTexts = 0;
                for (const auto& t : r.texts) markerTexts += t.text.rfind("$Armoires[0]", 0) == 0 || t.text.rfind("$%MW", 0) == 0 || t.text.rfind("$Titre", 0) == 0 ? 1 : 0;
                check(markerTexts > 0 && markerTexts < 80, "la liste virtuelle : " + std::to_string(markerTexts) + " reperes dessines sur 512");
                bool tiles = false;
                for (const auto& t : r.texts) tiles = tiles || t.text == "512";
                check(tiles, "les totaux de l'apercu dessines (512 reperes)");
            }
            if (tab == 2 && size.w == 1280.f) {
                const auto ss = d->sheetStats();
                check(ss.rows == 32 && ss.columns == 513 && ss.drawnCells > 0 && ss.drawnCells < 32 * 40 && ss.frozenColumns == 2,
                      "Tableau (Excel) : 32 x 513, " + std::to_string(ss.drawnCells) + " cases dessinees (les visibles), 2 colonnes figees");
                bool firstHeader = false;
                for (const auto& t : r.texts) firstHeader = firstHeader || t.text.find("S0$") != std::string::npos;
                d->scrollSheet(1.0e7f, 400.f);   // tout au bout (512 colonnes : des dizaines de milliers de pixels)
                paintInto(r, *root, size);
                const auto s2 = d->sheetStats();
                bool name = false, oldHeader = false, lastHeader = false;
                for (const auto& t : r.texts) {
                    name = name || t.text.rfind("Depart_", 0) == 0;
                    oldHeader = oldHeader || t.text.find(".S0$") != std::string::npos;
                    lastHeader = lastHeader || t.text == d->columns().back();
                }
                check(firstHeader && s2.scrollX > 0.f && s2.scrollY > 0.f && name && !oldHeader && lastHeader,
                      std::string(firstHeader ? "" : "[pas de S0 avant] ") + (name ? "" : "[pas de nom] ") + (oldHeader ? "[S0 reste] " : "") + (lastHeader ? "" : "[pas de " + d->columns().back() + "] ") +
                      "le tableau defile (" + num(s2.scrollX) + ", " + num(s2.scrollY) + ") : la colonne Nom reste (figee), la premiere colonne de reperes part, la derniere vient");
                const int out2 = textsOutside(r, d->panelRect(), where + " (defile)");
                check(out2 == 0, where + " (defile) : aucun texte hors de son cadre");
                d->scrollSheet(0.f, 0.f);
            }
        }
    }
    // Le tableau des profondeurs : la regle s'ecrit dans la case (double-clic / F2), comme un tableau du Studio.
    d->showTab(0);
    paintInto(r, *root, {1280.f, 800.f});
    auto* depths = d->depthTable();
    int voies = -1;
    for (std::size_t i = 0; i < d->model().groups.size(); ++i)
        if (d->model().groups[i].tail.rfind("Voies", 0) == 0) voies = static_cast<int>(i);
    check(depths && voies >= 0 && depths->beginCellEdit(static_cast<ui::RowIndex>(voies), 4), "la regle de Voies[.] s'ouvre dans sa case");
    if (depths && voies >= 0) {
        depths->finishCellEdit(true, "o + 2 * i");
        check(d->rules().rule(d->model().groups[static_cast<std::size_t>(voies)]).formula == "o + 2 * i"
                  && d->cell(31, "$Armoires[0].Departs[0].Voies[5].S1$") == "Armoires[3].Departs[7].Voies[11].S1",
              "tapee : o + 2 * i - la ligne 32 lit Voies[11] (" + d->cell(31, "$Armoires[0].Departs[0].Voies[5].S1$") + ")");
    }
    // Une petite fenetre apres une regle changee : toujours dans le cadre.
    ui::dialogframe::reset();
    paintInto(r, *root, {900.f, 620.f});
    check(textsOutside(r, d->panelRect(), "900 x 620 apres la regle") == 0, "900 x 620 apres la regle : dans le cadre");
}

} // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    gfx::setInterfaceFontFolder(root + "/resources/fonts");
    TextRecorder r;
    installMetrics(r);
    if (argc > 2 && std::string(argv[2]) == "--mesure") {   // la mesure seule (un profileur)
        cinqCents();
        return g_failures == 0 ? 0 : 1;
    }
    formules();
    nonRegression();
    cinqCents();
    if (r.fontsReady()) ecran(r);
    else std::printf("SAUTE (l'ecran) : pas de police (%s/resources/fonts)\n", root.c_str());
    std::printf("%d verification(s), %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
