// tests/listes_test.cpp - 1.47.0 (planches 06, 07, 12) : LES TROIS COMPOSANTS DES
// LISTES DE L'ADMIN, sans l'Admin (ui::FilterBar, ui::ListMenu, le depliage de
// ui::TableView et ui::UnfoldCard), dessines a blanc (un renderer qui note les
// textes).
//
//  1. la barre : chercher (toutes les colonnes, surligne), les puces et leurs
//     nombres (les autres filtres comptent, pas le sien), une periode, << Filtrer
//     sur >> (le clic droit), << + Filtre >>, << 12 sur 128 >>, << Effacer >>,
//     Ctrl+F, l'entonnoir des titres, une ou deux rangees
//  2. les menus de la barre : les cases et leurs nombres, Tout cocher, Aucune,
//     la recherche du menu, les periodes, << + Filtre >>, Mes filtres (nommer,
//     choisir, gerer)
//  3. ce qui est retenu (ui::FilterMemory, un faux reglage) : ce qui est choisi
//     et Mes filtres, relus par une barre neuve ; les separateurs echappes ; une
//     colonne disparue ignoree
//  4. le menu du clic droit : le meme ordre quel que soit l'ordre donne, une
//     raison sous une entree grisee, en rouge avec << ... >>, plusieurs lignes
//     (<< 3 LICENCES CHOISIES >>, << Deplier les 3 >>, << Exporter 3 lignes >>),
//     Copier (le presse-papiers), le menu d'une table (bind)
//  5. le depliage : le chevron, -> et <-, Entree et le double-clic (activated),
//     la place des lignes suivantes, le modele refait (la ligne reste depliee),
//     une ligne disparue oubliee, Tout replier
//  6. la carte d'une ligne depliee : sa hauteur, ses boites, ses liens, ses
//     boutons (des id), la place de la courbe
#include "../src/ui/widgets/FilterBar.hpp"
#include "../src/ui/widgets/FilterMemory.hpp"
#include "../src/ui/widgets/ListMenu.hpp"
#include "../src/ui/widgets/UnfoldCard.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/Theme.hpp"

#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

// ---- dessiner a blanc ----
class TextRenderer final : public gfx::IRenderer {
public:
    void beginFrame(gfx::Color) override {
        texts.clear();
        drawn.clear();
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
        texts += std::string(s) + "\n";
        drawn.push_back({p.x, measure(s, f).width, std::string(s)});
    }
    [[nodiscard]] gfx::TextMetrics measure(std::string_view s, gfx::FontId f) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return {static_cast<float>(chars(s)) * h * 0.5f, h, h * 0.8f, h * 0.2f};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return f.v ? static_cast<float>(f.v) : 16.f; }
    [[nodiscard]] gfx::Size surfaceSize() const override { return {1280.f, 800.f}; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view s, gfx::FontId f, float w) const override {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return std::min(static_cast<std::size_t>(std::max(0.f, w / (h * 0.5f))), s.size());
    }
    static std::size_t chars(std::string_view s) {
        std::size_t n = 0;
        for (char c : s)
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
        return n;
    }
    std::string texts;
    struct Drawn {
        float       x{0}, w{0};
        std::string text;
    };
    std::vector<Drawn> drawn;   // 1.47.0 : ou chaque texte est dessine, et sa largeur (rien ne deborde)

private:
    std::vector<gfx::Rect> clips_;
};

std::string g_clip;

void installMetrics() {
    ui::PlatformServices s;
    s.measureWidth = [](std::string_view t, gfx::FontId f) {
        const float h = f.v ? static_cast<float>(f.v) : 16.f;
        return static_cast<float>(TextRenderer::chars(t)) * h * 0.5f;
    };
    s.lineHeight = [](gfx::FontId f) { return f.v ? static_cast<float>(f.v) : 16.f; };
    s.clipboardGet = [] { return g_clip; };
    s.clipboardSet = [](std::string_view t) { g_clip = std::string(t); };
    s.surfaceSize = [] { return gfx::Size{1280.f, 800.f}; };
    ui::installPlatformServices(std::move(s));
}

const ui::Theme& theme() {
    static const ui::Theme t = ui::Theme::byName("Light");
    return t;
}

// Poser, dessiner (les widgets << par-dessus tout >> aussi : les menus ouverts) ; le texte dessine.
std::string draw(ui::Widget& w, gfx::Rect at) {
    TextRenderer r;
    w.setBounds(at);
    w.layout();
    std::vector<ui::Widget*> overlays;
    ui::PaintContext ctx{r, theme(), {0.f, 0.f, 1280.f, 800.f}, 0.0, &overlays};
    w.render(ctx);
    for (auto* o : overlays) o->paintTopMost(ctx);
    return r.texts;
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// ---- une liste d'essai : des commandes (rien de client : des noms d'essai) ----
struct Order {
    std::string number, date, state, article, client;
    std::int64_t when;
};

constexpr std::int64_t kNow = 1793000000;   // une heure fixe (novembre 2026)
constexpr std::int64_t kDay = 86400;

class Orders final : public ui::ITableModel {
public:
    std::vector<Order> rows;
    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 5; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"N\xC2\xB0", "Date", "\xC3\x89tat", "Article", "Client"};
        return c < 5 ? h[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows.size()) return {};
        const Order& o = rows[r];
        switch (c) {
            case 0: return o.number;
            case 1: return o.date;
            case 2: return o.state;
            case 3: return o.article;
            case 4: return o.client;
            default: return {};
        }
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
};

const std::string kLivrer = "\xC3\x80 livrer", kLivree = "Livr\xC3\xA9" "e", kRevoir = "\xC3\x80 revoir";

std::shared_ptr<Orders> sampleOrders() {
    auto m = std::make_shared<Orders>();
    m->rows = {
        {"214", "auj.", kLivrer, "Exports avanc\xC3\xA9s", "Client d'essai", kNow - 3600},
        {"212", "auj.", kLivree, "PROFESSIONAL 1 an", "Bureau d'\xC3\xA9tudes Exemple", kNow - 7200},
        {"211", "hier", kLivree, "PERSONAL 1 an", "Studio Exemple", kNow - kDay - 100},
        {"210", "hier", kLivrer, "ENTERPRISE 1 an", "Laboratoire d'essai", kNow - kDay - 200},
        {"209", "05/11", kRevoir, "PROFESSIONAL 1 an", "Atelier Exemple", kNow - 5 * kDay},
        {"208", "01/11", kLivree, "PROFESSIONAL 1 an", "Atelier Exemple", kNow - 9 * kDay},
        {"207", "20/10", kLivree, "PERSONAL 1 an", "Soci\xC3\xA9t\xC3\xA9 d'essai", kNow - 21 * kDay},
        {"206", "01/09", kLivree, "PROFESSIONAL 1 an", "Atelier Exemple", kNow - 70 * kDay},
    };
    return m;
}

std::vector<ui::FilterBar::Field> orderFields() {
    using K = ui::FilterBar::Kind;
    std::vector<ui::FilterBar::Field> f(5);
    f[0].title = "N\xC2\xB0";
    f[0].kind = K::Text;
    f[1].title = "Date";
    f[1].kind = K::Period;
    f[1].chip = true;
    f[1].chipLabel = "P\xC3\xA9riode";
    f[2].title = "\xC3\x89tat";
    f[2].chip = true;
    f[2].order = {kLivrer, kRevoir, kLivree};
    f[3].title = "Article";
    f[3].chip = true;
    f[3].noun = "un article";
    f[4].title = "Client";
    return f;
}

// Un faux reglage (FilterMemory) : une table en memoire.
struct FakeStore {
    std::map<std::string, std::string> values;
    void install() {
        ui::FilterMemory::Store s;
        s.read = [this](const std::string& k) {
            const auto it = values.find(k);
            return it == values.end() ? std::string() : it->second;
        };
        s.write = [this](const std::string& k, const std::string& v) {
            if (v.empty()) values.erase(k);
            else values[k] = v;
        };
        ui::FilterMemory::install(std::move(s));
    }
};

// Une page d'essai : la barre au-dessus de la table.
struct Page final : public ui::Widget {
    ui::FilterBar* bar{nullptr};
    ui::TableView* table{nullptr};
    std::shared_ptr<Orders> model;
    Page() : ui::Widget("page") {
        auto t = std::make_unique<ui::TableView>("commandes.table");
        t->setColumns({{"N\xC2\xB0", 60.f}, {"Date", 90.f}, {"\xC3\x89tat", 120.f}, {"Article", 200.f}, {"Client", 220.f}});
        model = sampleOrders();
        t->setModel(model);
        table = static_cast<ui::TableView*>(&addChild(std::move(t)));
        bar = static_cast<ui::FilterBar*>(&addChild(std::make_unique<ui::FilterBar>("commandes", orderFields())));
        bar->setClock([] { return kNow; });
        bar->setNoun("commandes");
        auto m = model;
        bar->attach(*table, [m](ui::RowIndex r, std::size_t f) { return f == 1 && r < m->rows.size() ? m->rows[r].when : 0; });
    }
    void onLayout() override {
        const gfx::Rect b = bounds();
        const float h = bar->heightFor(b.w - 20.f);
        bar->setBounds({b.x + 10.f, b.y + 10.f, b.w - 20.f, h});
        table->setBounds({b.x + 10.f, b.y + 20.f + h, b.w - 20.f, b.h - 30.f - h});
    }
};

std::size_t countOf(const std::vector<std::pair<std::string, std::size_t>>& v, const std::string& value) {
    for (const auto& [k, n] : v)
        if (k == value) return n;
    return 999;
}

// ------------------------------------------------------------------------------------------------ 1
void testBar() {
    std::printf("-- la barre : chercher, les puces, leurs nombres, la periode, Filtrer sur, Effacer, Ctrl+F\n");
    Page page;
    std::string t = draw(page, {0.f, 0.f, 1200.f, 600.f});
    ui::FilterBar& bar = *page.bar;
    check(bar.countText() == "8 sur 8" && has(t, "8") && has(t, " sur 8"), "8 sur 8 : " + bar.countText());
    check(has(t, "P\xC3\xA9riode") && has(t, "\xC3\x89tat") && has(t, "Article") && has(t, "Filtre") && has(t, "Mes filtres") && has(t, "Ctrl+F"),
          "les puces d'office, + Filtre, Mes filtres, Ctrl+F");
    check(!has(t, "Effacer les filtres"), "rien de filtre : pas d'Effacer");
    check(bar.chipWidget(2) && bar.chipWidget(2)->id() == "commandes.puce.etat" && page.findById("commandes.puce.periode") && page.findById("commandes.recherche"),
          "les id : commandes.puce.etat, .puce.periode, .recherche");
    // Chercher : dans toutes les colonnes, sans accents, surligne.
    bar.setSearch("atelier");
    check(page.table->visibleRowCount() == 3 && bar.countText() == "3 sur 8", "<< atelier >> : 3 lignes (" + bar.countText() + ")");
    check(page.table->highlight().text() == "atelier", "la recherche surlignee dans la table");
    t = draw(page, {0.f, 0.f, 1200.f, 600.f});
    check(has(t, "3 trouv\xC3\xA9" "es") && has(t, "Effacer les filtres"), "<< 3 trouvees >> dans le champ, Effacer les filtres");
    bar.setSearch("etudes");
    check(page.table->visibleRowCount() == 1, "<< etudes >> trouve << \xC3\xA9tudes >> (sans accents)");
    bar.setSearch("");
    check(page.table->visibleRowCount() == 8, "recherche effacee : 8");
    // Les puces et leurs nombres.
    auto states = bar.valueCounts(2);
    check(states.size() == 3 && states[0].first == kLivrer && states[1].first == kRevoir && states[2].first == kLivree, "l'Etat dans l'ordre donne (A livrer, A revoir, Livree)");
    check(countOf(states, kLivree) == 5 && countOf(states, kLivrer) == 2, "les nombres : 5 livrees, 2 a livrer");
    bar.setValues(2, {kLivrer, kRevoir});
    check(page.table->visibleRowCount() == 3 && bar.fieldActive(2), "Etat : A livrer, A revoir -> 3 lignes");
    check(bar.chipText(2) == "\xC3\x89tat : " + kLivrer + ", " + kRevoir, "le libelle de la puce : " + bar.chipText(2));
    states = bar.valueCounts(2);
    check(countOf(states, kLivree) == 5, "son propre filtre ne change pas ses nombres (Livree : 5)");
    const auto articles = bar.valueCounts(3);
    check(countOf(articles, "PROFESSIONAL 1 an") == 1 && countOf(articles, "PERSONAL 1 an") == 999, "les autres puces comptent les lignes gardees");
    // Une periode.
    bar.clearField(2);
    check(page.table->visibleRowCount() == 8 && !bar.fieldActive(2), "la croix : l'Etat efface");
    bar.setPeriod(1, 7);
    check(page.table->visibleRowCount() == 5 && bar.chipText(1) == "P\xC3\xA9riode : 7 j", "7 derniers jours : 5 (" + bar.chipText(1) + ")");
    const auto periods = bar.periodCounts(1);
    check(periods.size() == 6 && periods[0].second == 2 && periods[1].second == 5 && periods[2].second == 7 && periods[5].second == 8,
          "les nombres des periodes : aujourd'hui 2, 7 j 5, 30 j 7, tout 8");
    bar.setPeriod(1, 0);
    check(page.table->visibleRowCount() == 8 && !bar.fieldActive(1), "Tout : 8");
    bar.filterOnDay(1, kNow - kDay - 50);
    check(page.table->visibleRowCount() == 2 && has(bar.chipText(1), "le "), "Ce jour (hier) : 2 lignes, << " + bar.chipText(1) + " >>");
    bar.clearAll();
    // Filtrer sur (le clic droit) : une colonne sans puce - la puce apparait, marquee << clic droit >>.
    bar.filterOn(4, "Atelier Exemple");
    check(page.table->visibleRowCount() == 3 && bar.state().picks.size() == 1 && bar.state().picks[0].viaMenu, "Filtrer sur << Client : Atelier Exemple >> : 3");
    t = draw(page, {0.f, 0.f, 1200.f, 600.f});
    check(has(t, "Client : ") && has(t, "Atelier Exemple") && has(t, "clic droit"), "la puce du clic droit, sa marque");
    check(has(t, "3") && has(t, " sur 8") && has(t, "Effacer les filtres"), "3 sur 8, Effacer les filtres");
    // L'entonnoir des titres : violet sur la colonne filtree ; il ouvre le menu de la barre.
    check(page.table->hasFilterBar() && !page.table->filterIconRect(4).empty(), "la table a l'entonnoir de la barre");
    // Effacer les filtres.
    auto* clear = dynamic_cast<ui::Button*>(page.findById("commandes.effacer"));
    if (clear) clear->clicked->emit();
    check(page.table->visibleRowCount() == 8 && bar.state().picks.empty() && !bar.state().active(), "Effacer les filtres : tout revient");
    // + Filtre : une puce sur une autre colonne (sans filtre).
    bar.addChip(4);
    const auto chips = bar.chipFields();
    check(chips.size() == 4 && chips.back() == 4 && page.table->visibleRowCount() == 8, "+ Filtre : la puce Client, sans filtre");
    bar.clearField(4);
    check(bar.chipFields().size() == 3, "sa croix : la puce s'en va");
    // Ctrl+F : la recherche prend le clavier.
    (void)page.dispatch(ui::InputEvent{ui::KeyDown{ui::Key::F, {true, false, false, false}, false}});
    check(bar.searchField().focused(), "Ctrl+F : le champ a le clavier");
    // Une rangee large, deux etroite.
    check(bar.heightFor(1200.f) == 32.f && bar.heightFor(560.f) > 60.f, "une rangee a 1200 px, deux a 560 px");
}

// ------------------------------------------------------------------------------------------------ 2
void testMenus() {
    std::printf("-- les menus de la barre : les cases, Tout cocher, Aucune, la recherche, les periodes, + Filtre, Mes filtres\n");
    FakeStore store;
    store.install();
    Page page;
    (void)draw(page, {0.f, 0.f, 1200.f, 600.f});
    ui::FilterBar& bar = *page.bar;
    // La puce Article : son menu.
    auto* chip = dynamic_cast<ui::Button*>(page.findById("commandes.puce.article"));
    if (chip) chip->clicked->emit();
    ui::FilterBarPopup* pop = bar.popup();
    check(pop && pop->isOpen() && bar.openMenu() == ui::FilterBar::Menu::Chip && bar.menuField() == 3, "un clic sur la puce : son menu");
    std::string t = draw(page, {0.f, 0.f, 1200.f, 600.f});
    check(has(t, "Chercher un article\xE2\x80\xA6") && has(t, "PROFESSIONAL 1 an") && has(t, "Tout cocher") && has(t, "Aucune") && has(t, "sur les 8"),
          "le menu : la recherche, les valeurs, Tout cocher, Aucune, sur les 8");
    std::size_t pro = 99;
    for (std::size_t i = 0; pop && i < pop->spec().rows.size(); ++i)
        if (pop->spec().rows[i].label == "PROFESSIONAL 1 an") pro = i;
    check(pop && pro < pop->spec().rows.size() && pop->spec().rows[pro].count == "4" && pop->spec().rows[pro].mark == 1, "PROFESSIONAL 1 an : une case, 4");
    if (pop && pop->onRow && pro < 99) pop->onRow(pro);
    check(pop && pop->isOpen() && page.table->visibleRowCount() == 4 && pop->spec().rows[pro].on, "cochee : 4 lignes, le menu reste ouvert, la case cochee");
    // La recherche du menu : taper, puis Tout cocher (celles montrees).
    (void)page.dispatch(ui::InputEvent{ui::TextInput{"perso"}});
    check(pop && pop->fieldText() == "perso" && pop->shownRows().size() == 1, "la recherche du menu : << perso >> -> 1 valeur");
    if (pop && pop->onLink) pop->onLink(0);
    check(page.table->visibleRowCount() == 6, "Tout cocher (celles montrees) : PROFESSIONAL + PERSONAL = 6");
    if (pop && pop->onLink) pop->onLink(1);
    check(page.table->visibleRowCount() == 8 && !bar.fieldActive(3), "Aucune : rien ne filtre");
    (void)page.dispatch(ui::InputEvent{ui::KeyDown{ui::Key::Escape, {}, false}});
    check(pop && !pop->isOpen() && bar.openMenu() == ui::FilterBar::Menu::None, "Echap : le menu se ferme");
    // La periode : des choix, un clic ferme.
    bar.openChipMenu(1);
    t = draw(page, {0.f, 0.f, 1200.f, 600.f});
    check(has(t, "7 derniers jours") && has(t, "12 derniers mois") && has(t, "Tout"), "les periodes");
    if (pop && pop->onRow) pop->onRow(2);   // 30 derniers jours
    check(pop && !pop->isOpen() && page.table->visibleRowCount() == 7 && bar.chipText(1) == "P\xC3\xA9riode : 30 j", "30 derniers jours : 7, le menu ferme");
    // + Filtre : les colonnes sans puce.
    bar.openAddMenu();
    bool client = false, number = false;
    for (const auto& r : pop->spec().rows) {
        client = client || r.label == "Client";
        number = number || r.label == "N\xC2\xB0";
    }
    check(client && !number, "+ Filtre propose Client (pas le N\xC2\xB0 : du texte, a chercher)");
    for (std::size_t i = 0; i < pop->spec().rows.size(); ++i)
        if (pop->spec().rows[i].label == "Client") pop->onRow(i);
    check(bar.openMenu() == ui::FilterBar::Menu::Chip && bar.menuField() == 4 && bar.chipFields().back() == 4, "choisie : la puce Client et son menu");
    bar.closeMenu();
    // Mes filtres : enregistrer, choisir, gerer.
    bar.setMemoryKey("commandes");
    bar.setValues(2, {kLivrer});
    bar.openSavedMenu();
    check(pop->isOpen() && has(pop->spec().title, "MES FILTRES \xC2\xB7 COMMANDES") && pop->spec().links.size() == 2, "Mes filtres : le titre, Enregistrer, Gerer");
    pop->onLink(0);   // Enregistrer ce filtre...
    check(pop->spec().nameField && has(pop->fieldText(), "\xC3\x89tat : " + kLivrer), "un nom propose : " + pop->fieldText());
    pop->setFieldText("Commandes \xC3\xA0 livrer");
    pop->onEnter(pop->fieldText());
    check(bar.saved().size() == 1 && bar.saved()[0].name == "Commandes \xC3\xA0 livrer" && bar.currentSavedName() == "Commandes \xC3\xA0 livrer",
          "enregistre, et c'est celui qui est choisi");
    check(!pop->spec().rows.empty() && pop->spec().rows[0].tick && pop->spec().rows[0].count == "2", "dans le menu : coche, 2 commandes");
    bar.closeMenu();
    t = draw(page, {0.f, 0.f, 1200.f, 600.f});
    check(has(t, "Commandes \xC3\xA0 livrer"), "le bouton dit le filtre choisi");
    bar.clearAll();
    check(page.table->visibleRowCount() == 8 && bar.currentSavedName().empty(), "efface : plus de filtre choisi");
    bar.openSavedMenu();
    pop->onRow(0);
    check(page.table->visibleRowCount() == 2 && bar.fieldActive(2) && bar.fieldActive(1) && !pop->isOpen(), "choisi dans Mes filtres : revenu (2 lignes)");
    bar.openSavedMenu();
    pop->onLink(1);   // Gerer...
    check(!pop->spec().rows.empty() && pop->spec().rows[0].removable, "Gerer : une croix");
    pop->onRemove(0);
    check(bar.saved().empty(), "retire");
    bar.closeMenu();
    ui::FilterMemory::install({});
}

// ------------------------------------------------------------------------------------------------ 3
void testMemory() {
    std::printf("-- ce qui est retenu : ce qui est choisi, Mes filtres, relus par une barre neuve\n");
    FakeStore store;
    store.install();
    {
        Page page;
        (void)draw(page, {0.f, 0.f, 1200.f, 600.f});
        page.bar->setMemoryKey("commandes");
        page.bar->setValues(4, {"Atelier; Exemple, |%= d'essai"});   // les separateurs echappes
        page.bar->setValues(2, {kLivree});
        check(page.bar->saveAs("Livr\xC3\xA9" "es d'Atelier"), "enregistre");
        page.bar->setPeriod(1, 30);
        page.bar->setSearch("pro");
    }
    check(store.values.count("commandes") == 1 && store.values.count("commandes.mes") == 1, "deux cles : commandes, commandes.mes");
    {
        Page page;
        page.bar->setMemoryKey("commandes");
        const auto& s = page.bar->state();
        bool client = false;
        for (const auto& p : s.picks) client = client || (p.field == 4 && p.values.size() == 1 && p.values[0] == "Atelier; Exemple, |%= d'essai");
        check(s.search == "pro" && page.bar->searchField().text() == "pro" && client && page.bar->fieldActive(1) && page.bar->fieldActive(2),
              "une barre neuve relit la recherche, les puces (les separateurs intacts), la periode");
        check(page.bar->saved().size() == 1 && page.bar->saved()[0].name == "Livr\xC3\xA9" "es d'Atelier", "et Mes filtres");
    }
    // Une colonne qui n'existe plus : ignoree.
    {
        std::vector<ui::FilterBar::Field> f = orderFields();
        f[4].title = "Soci\xC3\xA9t\xC3\xA9";
        ui::FilterBar bar("autre", f);
        const auto s = bar.decodeState(store.values["commandes"]);
        bool lost = true;
        for (const auto& p : s.picks) lost = lost && p.field != 4;
        check(lost && s.search == "pro", "la colonne Client disparue : son filtre ignore, le reste relu");
    }
    ui::FilterMemory::install({});
}

// Une case a plusieurs valeurs (Field::split : les offres d'une option) ; une liste que sa page remplit (setSource).
void testSplit() {
    std::printf("-- une case a plusieurs valeurs (les offres d'une option), une liste remplie par sa page\n");
    const std::vector<std::vector<std::string>> rows = {{"exports_avances", "PERSONAL\nPROFESSIONAL"}, {"ia_avancee", "ENTERPRISE"}, {"travail", "PROFESSIONAL\nENTERPRISE"}, {"vide", ""}};
    ui::FilterBar bar("options", {{.title = "Identifiant", .kind = ui::FilterBar::Kind::Text}, {.title = "Propos\xC3\xA9" "e \xC3\xA0", .chip = true, .split = "\n"}});
    ui::FilterBar::Source src;
    src.rows = [&rows] { return rows.size(); };
    src.text = [&rows](std::size_t r, std::size_t f) { return rows[r][f]; };
    bar.setSource(std::move(src));
    std::string counts;
    for (const auto& [v, n] : bar.valueCounts(1)) counts += (v.empty() ? std::string("(vide)") : v) + "=" + std::to_string(n) + ";";
    check(counts == "ENTERPRISE=2;PERSONAL=1;PROFESSIONAL=2;(vide)=1;", "chaque valeur comptee une fois par ligne : " + counts);
    bar.filterOn(1, "PROFESSIONAL");
    check(bar.accepts(0) && !bar.accepts(1) && bar.accepts(2) && !bar.accepts(3) && bar.countText() == "2 sur 4", "PROFESSIONAL : les deux lignes qui l'ont");
    bar.setValues(1, {"ENTERPRISE", "PERSONAL"});
    check(bar.accepts(0) && bar.accepts(1) && bar.accepts(2) && !bar.accepts(3), "deux valeurs : l'une ou l'autre");
    bar.setValues(1, {""});
    check(!bar.accepts(0) && bar.accepts(3), "(vide) : la case vide");
    // L'ordre des puces donne par la page (les planches : Type, Qui, Periode - la date est la premiere colonne).
    ui::FilterBar order("historique", {{.title = "Date", .kind = ui::FilterBar::Kind::Period, .chip = true}, {.title = "Type", .chip = true},
                                       {.title = "Licence"}, {.title = "Qui", .chip = true}, {.title = "Criticit\xC3\xA9", .chip = true}});
    order.setChipOrder({1, 3, 0});
    const auto chips = order.chipFields();
    check(chips == std::vector<std::size_t>{1, 3, 0, 4}, "setChipOrder : Type, Qui, Periode, puis les autres dans l'ordre des colonnes");
    order.filterOn(2, "XPG-2026-00412");
    check(order.chipFields() == std::vector<std::size_t>{1, 3, 0, 4, 2}, "une puce ajoutee (Filtrer sur) : a la fin");
    // Etroite (la liste des PC d'une fiche) : rien ne se chevauche, tout reste dans la barre.
    for (const float w : {300.f, 420.f, 900.f}) {
        order.setValues(1, {"R\xC3\xA9vocation"});
        order.setBounds({0.f, 0.f, w, order.heightFor(w)});
        order.layout();
        std::vector<gfx::Rect> boxes;
        for (const auto& ch : order.children())
            if (ch->visible() && !ch->bounds().empty() && ch->id() != "historique.menu" && ch->id() != "historique.recherche.effacer") boxes.push_back(ch->bounds());
        bool ok = true;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            ok = ok && boxes[i].x >= -0.5f && boxes[i].right() <= w + 0.5f;
            for (std::size_t j = i + 1; j < boxes.size(); ++j) {
                const float ox = std::min(boxes[i].right(), boxes[j].right()) - std::max(boxes[i].x, boxes[j].x);
                const float oy = std::min(boxes[i].bottom(), boxes[j].bottom()) - std::max(boxes[i].y, boxes[j].y);
                ok = ok && !(ox > 0.5f && oy > 0.5f);
            }
        }
        check(ok, "barre de " + std::to_string(static_cast<int>(w)) + " px : la recherche, les puces, Effacer, Mes filtres sans chevauchement");
    }
}

// ------------------------------------------------------------------------------------------------ 4
void testMenuRule() {
    std::printf("-- le clic droit : le meme ordre partout, les raisons, plusieurs lignes, Copier, une table\n");
    std::vector<std::string> ran;
    const auto runner = [&ran](std::string n) { return [&ran, n] { ran.push_back(n); }; };
    ui::ListMenu m;
    // Dans le desordre : la regle range.
    m.danger({.label = "R\xC3\xA9voquer", .many = "R\xC3\xA9voquer %n licences", .icon = ui::Icon::Error, .run = runner("revoquer")});
    m.copy("L'identifiant", "XPG-2026-00412", ui::Icon::Copy, "Copier les %n identifiants", "XPG-2026-00412\nXPG-2026-00409");
    m.filterOn({.label = "Ce client : Atelier Exemple", .run = runner("filtrer")});
    m.action({.label = "Prolonger\xE2\x80\xA6", .many = "Prolonger les %n\xE2\x80\xA6", .why = "droit retir\xC3\xA9 le 02/11", .run = runner("prolonger")});
    m.action({.label = "Ajouter un PC\xE2\x80\xA6", .run = runner("pc")});
    m.goTo({.label = "Son PC", .run = runner("pc-aller"), .shortcut = "1"});
    m.exportRows(runner("exporter"));
    m.unfold(false, runner("deplier"));
    m.open("Ouvrir la licence", runner("ouvrir"));
    const auto items = m.items();
    std::vector<std::string> top;
    for (const auto& it : items) top.push_back(it.separator ? std::string("|") : it.label);
    std::string joined;
    for (const auto& s : top) joined += s + ";";
    check(joined == "Ouvrir la licence;D\xC3\xA9plier;|;Prolonger\xE2\x80\xA6;Ajouter un PC\xE2\x80\xA6;|;Aller \xC3\xA0;Filtrer sur;|;Copier;Exporter la ligne (CSV);|;"
                    "R\xC3\xA9voquer\xE2\x80\xA6;",
          "l'ordre de la regle : " + joined);
    const ui::PopupMenu::Item* prolong = nullptr;
    const ui::PopupMenu::Item* revoke = nullptr;
    for (const auto& it : items) {
        if (it.label == "Prolonger\xE2\x80\xA6") prolong = &it;
        if (it.label == "R\xC3\xA9voquer\xE2\x80\xA6") revoke = &it;
    }
    check(prolong && !prolong->enabled && prolong->note == "droit retir\xC3\xA9 le 02/11", "un droit qui manque : grise, la raison dessous");
    check(revoke && revoke->danger, "Revoquer : en rouge, avec << ... >>");
    {   // une raison trop longue pour une ligne du menu : ce qui precede << : >>, sinon les premiers mots
        ui::ListMenu longWhy;
        longWhy.danger({.label = "Supprimer le client", .why = "Une licence en cours (XPG-2025-00031, jusqu'au 06/11/2027) : r\xC3\xA9voque-la ou attends sa fin pour le supprimer.",
                        .run = runner("supprimer")});
        longWhy.action({.label = "Accepter\xE2\x80\xA6", .why = "il manque le droit d'accepter les demandes en ligne de ce poste ; demande-le \xC3\xA0 Camille Essai, l'admin+",
                        .run = runner("accepter")});
        std::string a, b;
        for (const auto& it : longWhy.items()) {
            if (it.label == "Supprimer le client\xE2\x80\xA6") a = it.note;
            if (it.label == "Accepter\xE2\x80\xA6") b = it.note;
        }
        check(a == "Une licence en cours (XPG-2025-00031, jusqu'au 06/11/2027)", "raison longue : avant << : >> (" + a + ")");
        check(b.size() > 3 && b.size() < 80 && b.compare(b.size() - 3, 3, "\xE2\x80\xA6") == 0 && b.rfind("il manque le droit", 0) == 0, "raison longue : les premiers mots et ... (" + b + ")");
    }
    check(items[0].shortcut == "Entr\xC3\xA9" "e" && items[1].shortcut == "\xE2\x86\x92", "Entree, ->");
    check(m.runLabel("Aller \xC3\xA0 > Son PC") && ran.back() == "pc-aller", "Aller a > Son PC");
    check(!m.runLabel("Prolonger\xE2\x80\xA6"), "une entree grisee ne se lance pas");
    check(m.runLabel("Copier > L'identifiant") && g_clip == "XPG-2026-00412", "Copier > L'identifiant : le presse-papiers");
    // Plusieurs lignes : seulement ce qui vaut pour toutes, avec leur nombre.
    ui::ListMenu many({3, "licences", true});
    many.open("Ouvrir la licence", runner("ouvrir"));
    many.unfold(false, runner("deplier"));
    many.action({.label = "Prolonger\xE2\x80\xA6", .many = "Prolonger les %n\xE2\x80\xA6", .run = runner("prolonger")});
    many.action({.label = "Ajouter un PC\xE2\x80\xA6", .run = runner("pc")});
    many.goTo({.label = "Son PC", .run = runner("pc-aller")});
    many.filterOn({.label = "Ce client", .run = runner("filtrer")});
    many.copy("L'identifiant", "A", ui::Icon::Copy, "Copier les %n identifiants", "A\nB\nC");
    many.exportRows(runner("exporter"));
    many.danger({.label = "R\xC3\xA9voquer", .many = "R\xC3\xA9voquer %n licences", .run = runner("revoquer")});
    const auto labels = many.labels();
    std::string all;
    for (const auto& l : labels) all += l + ";";
    check(all == "3 LICENCES CHOISIES;D\xC3\xA9plier les 3;Prolonger les 3\xE2\x80\xA6;Copier les 3 identifiants;Exporter 3 lignes (CSV);R\xC3\xA9voquer 3 licences\xE2\x80\xA6;",
          "plusieurs lignes : " + all);
    check(many.runLabel("Copier les 3 identifiants") && g_clip == "A\nB\nC", "Copier les 3 identifiants");
    // Le menu d'une table : il remplace le menu commun.
    ui::TableView table("t");
    table.setColumns({{"N\xC2\xB0", 60.f}, {"Date", 90.f}, {"\xC3\x89tat", 120.f}, {"Article", 200.f}, {"Client", 220.f}});
    table.setModel(sampleOrders());
    core::ConnectionScope links;
    int built = 0;
    links += ui::ListMenu::bind(table, [&] {
        ++built;
        ui::ListMenu lm({table.selection().size(), "commandes", true});
        lm.open("Ouvrir la commande", runner("ouvrir-commande"));
        lm.exportRows(runner("exporter"));
        return lm;
    });
    (void)draw(table, {0.f, 0.f, 800.f, 400.f});
    table.selectModelRows({1});
    table.openContextMenu({100.f, 100.f});
    const auto* cm = table.contextMenu();
    check(cm && cm->isOpen() && built == 1 && cm->items().size() == 3 && cm->items()[0].label == "Ouvrir la commande", "le menu de la table : les entrees de la page seules");
    if (cm) table.contextAction->emit(cm->items()[0].id);
    check(!ran.empty() && ran.back() == "ouvrir-commande", "choisie : l'action de la page");
}

// ------------------------------------------------------------------------------------------------ 5
void testUnfold() {
    std::printf("-- le depliage : le chevron, -> <-, Entree, la place des lignes, le modele refait, une ligne disparue\n");
    ui::TableView table("commandes.table");
    table.setColumns({{"N\xC2\xB0", 60.f}, {"Date", 90.f}, {"\xC3\x89tat", 120.f}, {"Article", 200.f}, {"Client", 220.f}});
    auto model = sampleOrders();
    table.setModel(model);
    int made = 0;
    table.setUnfold([&](ui::RowIndex r) {
        ++made;
        auto card = std::make_unique<ui::UnfoldCard>("commandes.depliage." + model->rows[r].number);
        ui::UnfoldCard::Box b;
        b.title = "LE CLIENT, LE PAIEMENT";
        b.lines.push_back({.icon = ui::Icon::User, .text = "**" + model->rows[r].client + "**"});
        b.lines.push_back({.text = "pay\xC3\xA9" "e", .check = true});
        card->addBox(std::move(b));
        card->addButton("Le re\xC3\xA7u", "commandes.depliage.recu", ui::Icon::Document, [] {});
        return card;
    });
    table.setEnterActivates(true);
    int activated = -1;
    std::vector<std::pair<ui::RowIndex, bool>> changes;
    core::ConnectionScope links;
    links += table.activated->connect([&](ui::RowIndex r) { activated = static_cast<int>(r); });
    links += table.unfoldChanged->connect([&](ui::RowIndex r, bool on) { changes.emplace_back(r, on); });
    std::string t = draw(table, {0.f, 0.f, 900.f, 600.f});
    gfx::Rect chev, row1Before, row1After, unf;
    check(table.chevronRect(0, chev) && table.rowRect(1, row1Before), "un chevron devant la premiere case");
    (void)table.dispatch(ui::InputEvent{ui::MouseDown{{chev.x + chev.w * 0.5f, chev.y + chev.h * 0.5f}, ui::MouseButton::Left, 1, {}}});
    check(table.isUnfolded(0) && table.unfoldWidget(0) && made == 1 && changes.size() == 1 && changes[0].second, "un clic sur le chevron : depliee");
    check(table.selectedModelRows().size() == 1 && table.selectedModelRows()[0] == 0, "... et choisie");
    t = draw(table, {0.f, 0.f, 900.f, 600.f});
    check(has(t, "LE CLIENT, LE PAIEMENT") && has(t, "Client d'essai") && has(t, "Le re\xC3\xA7u"), "le depliage dessine sous la ligne");
    check(table.unfoldRect(0, unf) && table.rowRect(1, row1After) && row1After.y >= row1Before.y + unf.h - 0.5f && unf.h > 60.f,
          "la ligne suivante descend de la hauteur du depliage");
    check(table.findById("commandes.depliage.recu") != nullptr, "le bouton du depliage a son id");
    // Le modele refait : la ligne reste depliee (son N\xC2\xB0), le widget est refait.
    ui::Widget* before = table.unfoldWidget(0);
    std::swap(model->rows[0], model->rows[1]);
    model->modelReset->emit();
    (void)draw(table, {0.f, 0.f, 900.f, 600.f});
    check(table.isUnfolded(1) && !table.isUnfolded(0) && table.unfoldWidget(1) && table.unfoldWidget(1) != before && made == 2,
          "le modele refait : la ligne n\xC2\xB0 214 suivie (ligne 1), son depliage refait");
    // Les touches.
    table.selectModelRows({2});
    table.takeFocus();
    (void)table.dispatch(ui::InputEvent{ui::KeyDown{ui::Key::Right, {}, false}});
    check(table.isUnfolded(2), "-> deplie la ligne choisie");
    (void)table.dispatch(ui::InputEvent{ui::KeyDown{ui::Key::Left, {}, false}});
    check(!table.isUnfolded(2), "<- la replie");
    (void)table.dispatch(ui::InputEvent{ui::KeyDown{ui::Key::Return, {}, false}});
    check(activated == 2, "Entree : la fiche (activated)");
    // Une ligne disparue : oubliee ; Tout replier.
    model->rows.erase(model->rows.begin() + 1);
    model->modelReset->emit();
    check(table.unfoldedRows().empty(), "la ligne n\xC2\xB0 214 disparue : oubliee");
    table.unfoldAll();
    check(table.unfoldedRows().size() == model->rows.size(), "Tout deplier : " + std::to_string(table.unfoldedRows().size()));
    (void)draw(table, {0.f, 0.f, 900.f, 600.f});
    table.foldAll();
    check(table.unfoldedRows().empty(), "Tout replier");
    // Le double-clic ouvre (il ne deplie pas).
    (void)draw(table, {0.f, 0.f, 900.f, 600.f});
    gfx::Rect r3;
    if (table.rowRect(3, r3)) (void)table.dispatch(ui::InputEvent{ui::MouseDown{{r3.x + 300.f, r3.y + r3.h * 0.5f}, ui::MouseButton::Left, 2, {}}});
    check(activated == 3 && !table.isUnfolded(3), "le double-clic : la fiche");
}

// ------------------------------------------------------------------------------------------------ 6
void testCard() {
    std::printf("-- la carte d'une ligne depliee : sa hauteur, ses liens, ses boutons, la place de la courbe\n");
    ui::UnfoldCard card("journal.depliage");
    int link = 0, person = 0;
    ui::UnfoldCard::Box a;
    a.title = "L'OBJET TOUCH\xC3\x89";
    a.lines.push_back({.icon = ui::Icon::Document, .text = "Licence **XPG-2026-00391**", .link = [&] { ++link; }});
    a.lines.push_back({.text = "sign\xC3\xA9" "e et livr\xC3\xA9" "e par", .person = ui::UnfoldCard::Person{"L\xC3\xA9" "a Essai", gfx::Color::rgb(0x0B7FA3), [&] { ++person; }},
                       .after = "sa fiche"});
    card.addBox(std::move(a));
    ui::UnfoldCard::Box b;
    b.title = "AVANT \xC2\xB7 APR\xC3\x88S";
    b.head = {"CE QUI CHANGE", "AVANT", "APR\xC3\x88S"};
    b.table = {{"Options", "aucune", "Exports avanc\xC3\xA9s"}, {"Montant", "\xE2\x80\x94", "120 \xE2\x82\xAC"}};
    card.addBox(std::move(b));
    bool curve = false;
    card.setCurve("SES 30 JOURS", [&](const ui::PaintContext&, gfx::Rect r) { curve = r.w > 0.f && r.h > 0.f; });
    card.addButton("Ouvrir la licence", "journal.voir", ui::Icon::GoTo, [] {});
    card.addPerson({"L\xC3\xA9" "a Essai", gfx::Color::rgb(0x0B7FA3), [&] { ++person; }}, "journal.admin");
    card.addText("clic droit : le menu", true);
    const float h = card.heightFor(900.f);
    check(h > 120.f && h < 260.f, "une hauteur a la mesure du contenu : " + std::to_string(h));
    const std::string t = draw(card, {0.f, 0.f, 900.f, h});
    check(has(t, "L'OBJET TOUCH\xC3\x89") && has(t, "XPG-2026-00391") && has(t, "CE QUI CHANGE") && has(t, "120 \xE2\x82\xAC") && has(t, "L\xC3\xA9" "a Essai") &&
              has(t, "clic droit : le menu"),
          "les boites, le gras, le tableau, la pastille, le mot de droite");
    check(curve, "la place de la courbe : dessinee par l'hote");
    check(card.clickLink(0, 0) && link == 1 && card.clickLink(0, 1) && person == 1, "les liens d'une ligne, la pastille d'admin");
    auto* open = dynamic_cast<ui::Button*>(card.findById("journal.voir"));
    auto* chip = dynamic_cast<ui::Button*>(card.findById("journal.admin"));
    if (chip) chip->clicked->emit();
    check(open && chip && person == 2, "les boutons de la rangee : des id, cliquables");
    check(has(card.text(), "Licence XPG-2026-00391") && has(card.text(), "Ouvrir la licence"), "text() : sans les **");

    // 1.47.0 : rien ne deborde - un nom d'admin tres long (la cle d'essai), une carte etroite : raccourcis, l'infobulle dit tout.
    const std::string longName = "CLE D'ESSAI XPG-CLE-ESSAI-LICENCES - NE PAS LIVRER (Camille Essai)";
    ui::UnfoldCard narrow("liste.depliage");
    ui::UnfoldCard::Box nb;
    nb.title = "LA SIGNATURE ET TOUT CE QUI VA AVEC, UN TITRE TRES LONG";
    nb.lines.push_back({.text = "sign\xC3\xA9" "e par", .person = ui::UnfoldCard::Person{longName, gfx::Color::rgb(0x5B4BDB), [] {}}, .after = "le 06/11/2026 sur PC-BUREAU-01"});
    nb.lines.push_back({.icon = ui::Icon::Document, .text = "un texte bien trop long pour une boite de cette largeur, vraiment"});
    narrow.addBox(std::move(nb));
    narrow.addButton("Voir tout le journal de cet admin", "liste.voir", ui::Icon::GoTo, [] {});
    narrow.addPerson({longName, gfx::Color::rgb(0x5B4BDB), [] {}}, "liste.admin");
    narrow.addText("registre n\xC2\xB0 318 sign\xC3\xA9 le 06/11/2026 par", true);
    const float nw = 340.f, nh = narrow.heightFor(nw);
    TextRenderer nr;
    nr.beginFrame({});
    narrow.setBounds({0.f, 0.f, nw, nh});
    narrow.layout();
    {
        ui::PaintContext ctx{nr, theme(), {0.f, 0.f, nw, nh}, 0.0};
        narrow.render(ctx);
    }
    bool inside = true, chipCut = false, lineCut = false;
    std::string out;
    for (const auto& d : nr.drawn) {
        if (d.x < -0.5f || d.x + d.w > nw + 0.5f) {
            inside = false;
            out += d.text + " ; ";
        }
        if (d.text.rfind("CLE D'ESSAI", 0) == 0 && d.text != longName && d.text.size() >= 3 && d.text.compare(d.text.size() - 3, 3, "\xE2\x80\xA6") == 0) chipCut = true;
        if (d.text.rfind("un texte bien", 0) == 0 && d.text.find("vraiment") == std::string::npos) lineCut = true;
    }
    check(inside, "carte etroite : aucun texte hors de la carte (" + out + ")");
    check(chipCut && lineCut, "le nom de l'admin et la ligne trop longue : raccourcis par << ... >>");
    bool tipName = false, tipLine = false;
    for (float x = 0.f; x < nw; x += 4.f)
        for (float y = 0.f; y < nh; y += 4.f) {
            const std::string tip = narrow.liveTooltip({x, y});
            tipName = tipName || tip == longName;
            tipLine = tipLine || tip == "un texte bien trop long pour une boite de cette largeur, vraiment";
        }
    check(tipName && tipLine, "sous la souris : le nom entier, la ligne entiere");
    auto* rowChip = dynamic_cast<ui::Button*>(narrow.findById("liste.admin"));
    auto* rowBtn = dynamic_cast<ui::Button*>(narrow.findById("liste.voir"));
    const float rowRight = rowChip && rowBtn ? std::max(rowChip->bounds().right(), rowBtn->bounds().right()) : 1e9f;
    check(rowChip && rowBtn && rowRight <= nw && rowBtn->bounds().right() <= rowChip->bounds().x + 0.5f, "la rangee d'actions tient dans la carte, sans chevauchement");
    check(rowChip && rowChip->hasTooltip() && has(rowChip->liveTooltip({rowChip->bounds().x + 4.f, rowChip->bounds().y + 4.f}), longName),
          "la pastille raccourcie : son nom entier en infobulle");
    // Un bouton trop etroit : raccourci (jamais coupe aux deux bouts), le texte entier en infobulle.
    ui::Button btn("Exporter le fichier de licence\xE2\x80\xA6", "b");
    TextRenderer br;
    br.beginFrame({});
    btn.setBounds({0.f, 0.f, 120.f, 30.f});
    {
        ui::PaintContext ctx{br, theme(), {0.f, 0.f, 120.f, 30.f}, 0.0};
        btn.render(ctx);
    }
    check(!br.drawn.empty() && br.drawn.back().x >= 0.f && br.drawn.back().x + br.drawn.back().w <= 120.5f && btn.elided() &&
              btn.liveTooltip({10.f, 10.f}) == "Exporter le fichier de licence\xE2\x80\xA6",
          "un bouton trop etroit : son texte raccourci, entier en infobulle");
}

} // namespace

int main() {
    installMetrics();
    testBar();
    testMenus();
    testMemory();
    testSplit();
    testMenuRule();
    testUnfold();
    testCard();
    std::printf("%d verifications, %d echec(s)\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
