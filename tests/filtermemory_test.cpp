// tests/filtermemory_test.cpp - Lot API 8 : les filtres retenus d'une seance a
// l'autre (ui::FilterMemory) et la recherche des volets qui n'en avaient pas.
//
//  1. le texte retenu : echapper, relire (les separateurs, les blancs des bords),
//     un texte abime ignore, ce qu'il veut dire en clair
//  2. le crochet : une table ecrit ses filtres, une table neuve (la seance
//     suivante) les relit avant tout dessin - l'entonnoir allume avant ou apres
//     les colonnes ; une colonne deplacee suit son titre
//  3. un filtre qui ne trouve plus sa colonne (renommee) : ignore, sans bruit
//  4. "Tout effacer" : efface et oublie
//  5. deux projets separes (FilterMemory::contextChanged) ; sans projet : rien
//  6. la recherche d'un volet (ui::SearchField) : retenue, relue ; "3 sur 12"
//  7. la recherche des Taches (apikit::searchTwoLevels) et le commentaire qui
//     decrit une sous-routine (apikit::leadingComment)
//  8. la barre des volets de l'API (app::ApiFilterBar) : la recherche et la
//     pastille retenues, une pastille qui arrive apres la relecture, deux
//     projets, un geste qui l'emporte, relue au premier placement
#include "../src/ui/widgets/FilterMemory.hpp"
#include "../src/ui/widgets/SearchField.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/TextSearch.hpp"
#include "../src/app/ApiListKit.hpp"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_fail = 0;
int g_checks = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what);
    }
}

// Les reglages de l'application, en memoire, ranges comme App les range :
// "filtres.<projet>.<id du widget>". Pas de projet : rien n'est lu ni ecrit.
struct FakeSettings {
    std::map<std::string, std::string> values;
    std::string                        project = "A";
    int                                writes = 0;

    [[nodiscard]] std::string prefix() const { return project.empty() ? std::string{} : "filtres." + project + "."; }
    void install() {
        ui::FilterMemory::Store s;
        s.read = [this](const std::string& key) -> std::string {
            if (prefix().empty()) return {};
            const auto it = values.find(prefix() + key);
            return it == values.end() ? std::string{} : it->second;
        };
        s.write = [this](const std::string& key, const std::string& value) {
            if (prefix().empty()) return;
            ++writes;
            if (value.empty()) values.erase(prefix() + key);
            else values[prefix() + key] = value;
        };
        s.forget = [this](bool all) {
            const auto p = all ? std::string("filtres.") : prefix();
            for (auto it = values.lower_bound(p); it != values.end() && it->first.compare(0, p.size(), p) == 0;) it = values.erase(it);
        };
        ui::FilterMemory::install(std::move(s));
    }
    [[nodiscard]] std::string get(const std::string& projectTag, const std::string& key) const {
        const auto it = values.find("filtres." + projectTag + "." + key);
        return it == values.end() ? std::string{} : it->second;
    }
    [[nodiscard]] bool has(const std::string& projectTag, const std::string& key) const {
        return values.count("filtres." + projectTag + "." + key) > 0;
    }
};

// Une table des variables. Les colonnes : dans l'ordre que l'on veut (une
// colonne deplacee, une colonne renommee).
class VarModel final : public ui::ITableModel {
public:
    explicit VarModel(std::vector<std::string> headers) : headers_(std::move(headers)) {}
    [[nodiscard]] std::size_t rowCount() const override { return kRows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return headers_[c]; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        // La donnee suit le titre : "Type" (ou "Genre", son nouveau nom) est toujours le type.
        const auto& row = kRows[r];
        const auto& h = headers_[c];
        if (h == "Nom") return row[0];
        if (h == "Type" || h == "Genre") return row[1];
        return row[2];
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }

private:
    std::vector<std::string> headers_;
    const std::vector<std::vector<std::string>> kRows{{"Pompe_1", "BOOL", "pompe de gavage"},
                                                      {"Vanne_2", "BOOL", "vanne d'isolement"},
                                                      {"Debit", "REAL", "d\xC3\xA9" "bit de la pompe"},
                                                      {"Compteur", "INT", ""}};
};

std::vector<ui::TableView::Column> columnsOf(const std::vector<std::string>& titles) {
    std::vector<ui::TableView::Column> cols;
    for (const auto& t : titles) {
        ui::TableView::Column c;
        c.title = t;
        cols.push_back(c);
    }
    return cols;
}

// Une "seance" : une table neuve, son modele, ses colonnes ; l'entonnoir allume
// avant les colonnes (elles arrivent a la mise en page) ou apres.
std::unique_ptr<ui::TableView> openTable(const std::string& id, const std::vector<std::string>& titles, bool enableFirst) {
    auto t = std::make_unique<ui::TableView>(id);
    t->setModel(std::make_shared<VarModel>(titles));
    if (enableFirst) {
        t->setColumnFiltersEnabled(true);
        t->setColumns(columnsOf(titles));
    } else {
        t->setColumns(columnsOf(titles));
        t->setColumnFiltersEnabled(true);
    }
    return t;
}

ui::ColumnFilter filterOf(std::size_t column, ui::ColumnFilter::Op op, std::string value) {
    ui::ColumnFilter f;
    f.column = column;
    f.op = op;
    f.value = std::move(value);
    return f;
}

const std::string kTable = "api.variables.table";
const std::vector<std::string> kTitles{"Nom", "Type", "Commentaire"};

// ------------------------------------------------------------------------------
void textRetained() {
    std::printf("1. le texte retenu\n");
    using M = ui::FilterMemory;
    const std::string tricky = " a;b,c%d=e|f\tg ";
    const auto esc = M::escape(tricky);
    check(esc.find(';') == std::string::npos && esc.find(',') == std::string::npos && esc.find('=') == std::string::npos
              && esc.find('|') == std::string::npos && esc.front() != ' ' && esc.back() != ' ',
          "echappe : ni separateur, ni '=', ni blanc au bord");
    check(M::unescape(esc) == tricky, "echappe puis relu : le meme texte");

    ui::ColumnFilter only;
    only.column = 1;
    only.list = ui::ColumnFilter::List::Only;
    only.values = {"BOOL", "", "a;b"};
    const auto enc = M::encodeColumns({{"Type", filterOf(1, ui::ColumnFilter::Op::Equals, "BOOL")}, {"Type", only}});
    check(enc.rfind("c1;Type,1,egal,BOOL,,", 0) == 0, "le format : c1;Type,1,egal,BOOL,,");
    const auto dec = M::decodeColumns(enc);
    check(dec.size() == 2 && dec[0].title == "Type" && dec[0].filter.op == ui::ColumnFilter::Op::Equals && dec[0].filter.value == "BOOL",
          "relu : Type = BOOL");
    check(dec.size() == 2 && dec[1].filter.list == ui::ColumnFilter::List::Only && dec[1].filter.values == only.values,
          "relu : la liste des valeurs (une vide, une avec ';')");
    check(M::decodeColumns("c1;Type,x,egal,BOOL,,").empty() && M::decodeColumns("n'importe quoi").empty()
              && M::decodeColumns("c1;Type,1").empty(),
          "un texte abime : ignore");
    check(M::encodeColumns({}).empty(), "aucun filtre : rien a retenir");

    std::string search, chip;
    const std::string situees = "Situ\xC3\xA9" "es";
    check(M::decodeSearch(M::encodeSearch("\"pompe; 1\" -vanne", situees), search, chip) && search == "\"pompe; 1\" -vanne" && chip == situees,
          "la recherche et la pastille : relues");
    check(M::encodeSearch("", "").empty(), "une recherche vide : rien a retenir");
    check(M::describe(enc).rfind("Type = BOOL", 0) == 0, "en clair : Type = BOOL");
    check(M::describe(M::encodeSearch("TON_D", "")) == "recherche \xC2\xAB TON_D \xC2\xBB", "en clair : recherche << TON_D >>");
}

void tableRemembers(FakeSettings& settings) {
    std::printf("2. une table ecrit ses filtres ; la seance suivante les relit\n");
    {
        auto t = openTable(kTable, kTitles, false);
        check(t->columnFilters().empty() && t->visibleRowCount() == 4, "rien de retenu : aucun filtre");
        t->setColumnFilter(filterOf(1, ui::ColumnFilter::Op::Equals, "BOOL"));
        check(t->visibleRowCount() == 2, "Type = BOOL : 2 lignes");
        check(settings.get("A", kTable) == "c1;Type,1,egal,BOOL,,", "ecrit sous l'id de la table, dans le projet A");
    }
    for (const bool enableFirst : {true, false}) {
        const int before = settings.writes;
        auto t = openTable(kTable, kTitles, enableFirst);
        // Relus des l'ouverture, avant tout dessin : rien ne clignote.
        check(t->columnFilters().size() == 1 && t->columnFilters()[0].column == 1 && t->columnFilters()[0].value == "BOOL",
              enableFirst ? "relu (entonnoir avant les colonnes)" : "relu (entonnoir apres les colonnes)");
        check(t->visibleRowCount() == 2, "relu : les 2 lignes BOOL, tout de suite");
        check(settings.writes == before, "relire n'ecrit rien");
    }
    {
        // La colonne Type deplacee (troisieme) : le filtre suit son titre.
        auto t = openTable(kTable, {"Commentaire", "Nom", "Type"}, false);
        check(t->columnFilters().size() == 1 && t->columnFilters()[0].column == 2, "une colonne deplacee : le filtre la suit");
        check(t->visibleRowCount() == 2, "une colonne deplacee : 2 lignes");
    }
    {
        // Une table qui ne retient rien : ni lue ni ecrite.
        auto t = std::make_unique<ui::TableView>(kTable);
        t->setRemembersColumnFilters(false);
        t->setModel(std::make_shared<VarModel>(kTitles));
        t->setColumns(columnsOf(kTitles));
        t->setColumnFiltersEnabled(true);
        check(t->columnFilters().empty(), "une table qui ne retient rien : pas relue");
    }
}

void renamedColumn(FakeSettings& settings) {
    std::printf("3. un filtre qui ne trouve plus sa colonne\n");
    {
        auto t = openTable(kTable, kTitles, false);
        t->setColumnFilter(filterOf(2, ui::ColumnFilter::Op::Contains, "pompe"));
        check(t->columnFilters().size() == 2 && t->visibleRowCount() == 1, "Type = BOOL et Commentaire contient pompe : 1 ligne");
    }
    const auto stored = settings.get("A", kTable);
    const int before = settings.writes;
    auto t = openTable(kTable, {"Nom", "Genre", "Commentaire"}, true);
    check(t->columnFilters().size() == 1 && t->columnFilters()[0].column == 2 && t->columnFilters()[0].value == "pompe",
          "Type renommee Genre : son filtre ignore, celui du commentaire relu");
    check(t->visibleRowCount() == 2, "le filtre du commentaire seul : 2 lignes");
    check(settings.writes == before && settings.get("A", kTable) == stored, "ignore sans bruit : rien d'ecrit ni d'efface");
}

void clearAll(FakeSettings& settings) {
    std::printf("4. Tout effacer : efface et oublie\n");
    {
        auto t = openTable(kTable, kTitles, false);
        check(t->columnFilters().size() == 2, "deux filtres relus");
        t->removeColumnFilter(2);
        check(settings.get("A", kTable) == "c1;Type,1,egal,BOOL,,", "un filtre retire : le reste retenu");
        t->clearColumnFilters();
        check(t->columnFilters().empty() && t->visibleRowCount() == 4, "Tout effacer : toutes les lignes");
        check(!settings.has("A", kTable), "Tout effacer : la cle est oubliee");
    }
    auto t = openTable(kTable, kTitles, false);
    check(t->columnFilters().empty(), "la seance suivante : rien ne revient");
}

void twoProjects(FakeSettings& settings) {
    std::printf("5. deux projets separes\n");
    settings.project = "A";
    auto t = openTable(kTable, kTitles, false);
    t->setColumnFilter(filterOf(1, ui::ColumnFilter::Op::Equals, "BOOL"));
    check(settings.get("A", kTable) == "c1;Type,1,egal,BOOL,,", "projet A : Type = BOOL");

    settings.project = "B";
    ui::FilterMemory::contextChanged(true);
    check(t->columnFilters().empty() && t->visibleRowCount() == 4, "projet B ouvert : la table relit le sien (rien)");
    check(settings.get("A", kTable) == "c1;Type,1,egal,BOOL,,", "projet B ouvert : ce que retient A reste");
    t->setColumnFilter(filterOf(2, ui::ColumnFilter::Op::Contains, "vanne"));
    check(settings.get("B", kTable) == "c1;Commentaire,2,contient,vanne,,", "projet B : son filtre, sous sa cle");
    check(settings.get("A", kTable) == "c1;Type,1,egal,BOOL,,", "projet B : A n'a pas change");

    settings.project = "A";
    ui::FilterMemory::contextChanged(true);
    check(t->columnFilters().size() == 1 && t->columnFilters()[0].column == 1 && t->visibleRowCount() == 2, "retour au projet A : Type = BOOL revient");

    // Le meme projet enregistre ailleurs : ce qui est montre le suit.
    settings.project = "A2";
    ui::FilterMemory::contextChanged(false);
    check(settings.get("A2", kTable) == "c1;Type,1,egal,BOOL,,", "le meme projet ailleurs : reecrit sous la nouvelle cle");

    // Sans projet : rien n'est lu, rien n'est ecrit.
    settings.project = "";
    ui::FilterMemory::contextChanged(true);
    const auto count = settings.values.size();
    t->setColumnFilter(filterOf(0, ui::ColumnFilter::Op::StartsWith, "P"));
    check(settings.values.size() == count, "sans projet : rien d'ecrit");

    // Oublier tous les projets (le crochet "forget").
    settings.project = "A";
    ui::FilterMemory::forget(true);
    check(settings.values.empty(), "tout oublier : plus rien, dans aucun projet");
}

void searchField(FakeSettings& settings) {
    std::printf("6. la recherche d'un volet (Recettes, Utilisateurs, Styles)\n");
    settings.project = "A";
    const std::string id = "hmi.recipes.search";
    {
        ui::SearchField f(id, "Rechercher");
        int changed = 0;
        core::ConnectionScope links;
        links += f.changed->connect([&] { ++changed; });
        f.recall();
        check(f.text().empty() && changed == 0, "rien de retenu : vide, pas de signal");
        f.setText("gaz");
        check(changed == 1 && settings.get("A", id) == "s1;gaz;", "tapee : retenue sous l'id du champ");
        f.setCount(3, 12);
        check(f.countText() == "3 sur 12", "le compte : 3 sur 12");
        f.setText("");
        check(f.countText().empty() && !settings.has("A", id), "effacee : ni compte, ni cle");
        f.setText("\"pompe de\" -vanne");
    }
    ui::SearchField g(id, "Rechercher");
    g.recall();
    check(g.text() == "\"pompe de\" -vanne", "la seance suivante : la recherche revient");
    check(g.query().matches({std::string_view("Pompe de gavage")}) && !g.query().matches({std::string_view("pompe de la vanne")}),
          "la meme recherche partout : \"phrase\", -exclu, sans casse");
    check(ui::SearchQuery("debit").matches({std::string_view("D\xC3\xA9" "bit")}), "sans accents");

    settings.project = "B";
    ui::FilterMemory::contextChanged(true);
    check(g.text().empty(), "un autre projet : sa recherche (aucune)");
    settings.project = "A";
    ui::FilterMemory::contextChanged(true);
    check(g.text() == "\"pompe de\" -vanne", "retour au projet : elle revient");
}

void apiFilterBar(FakeSettings& settings) {
    std::printf("8. la barre des volets de l'API (Taches, Sous-routines, Statistiques)\n");
    settings.project = "A";
    const std::string id = "analysis.api.taches.volet.filtres";
    const std::vector<std::pair<std::string, std::size_t>> chips{{"Toutes", 21}, {"Cycliques", 3}, {"\xC3\x89v\xC3\xA9nements", 2}};
    {
        app::ApiFilterBar bar(id, "Rechercher");
        int changed = 0;
        core::ConnectionScope links;
        links += bar.changed->connect([&] { ++changed; });
        bar.recall();
        check(bar.search().empty() && changed == 0, "barre : rien de retenu, pas de signal");
        bar.setChips(chips);
        bar.setSearch("MAST");
        check(settings.get("A", id) == "s1;MAST;", "barre : la recherche retenue, sous l'id de la barre");
        check(bar.chooseChip("cyc") && bar.current() == 1 && settings.get("A", id) == "s1;MAST;Cycliques", "barre : la pastille retenue");
        check(bar.currentChip() == "Cycliques", "barre : le libelle de la pastille choisie");
        bar.setCount(3, 21);
        check(bar.countText() == "3 sur 21", "barre : 3 sur 21");
        check(bar.chooseChip("Toutes") && settings.get("A", id) == "s1;MAST;", "barre : la premiere pastille n'est pas retenue");
        (void)bar.chooseChip("cyc");
    }
    {
        // La seance suivante : les pastilles arrivent apres la relecture (le volet les calcule).
        app::ApiFilterBar bar(id, "Rechercher");
        int changed = 0;
        core::ConnectionScope links;
        links += bar.changed->connect([&] { ++changed; });
        bar.recall();
        check(bar.search() == "MAST" && changed == 1, "barre relue : MAST, un seul signal");
        bar.setChips(chips);
        check(bar.current() == 1, "barre relue : la pastille Cycliques, arrivee apres");
        settings.project = "B";
        ui::FilterMemory::contextChanged(true);
        check(bar.search().empty() && bar.current() == 0, "barre, projet B : ni recherche ni pastille");
        settings.project = "A";
        ui::FilterMemory::contextChanged(true);
        check(bar.search() == "MAST" && bar.current() == 1, "barre, retour au projet A : les deux reviennent");
    }
    {
        // Un geste (ou une recherche posee par le volet) avant la relecture l'emporte.
        app::ApiFilterBar bar(id, "Rechercher");
        bar.setSearch("SR_");
        bar.setBounds({0, 0, 800, 44});
        bar.layout();
        check(bar.search() == "SR_", "barre : un geste avant la relecture l'emporte");
    }
    {
        // Le volet n'appelle pas recall() : relue au premier placement, avant le premier dessin.
        app::ApiFilterBar bar(id, "Rechercher");
        check(bar.search().empty(), "barre : pas encore placee, pas encore relue");
        bar.setBounds({0, 0, 800, 44});
        bar.layout();
        check(bar.search() == "SR_", "barre : relue au premier placement");
    }
}

void paneSearches() {
    std::printf("7. la recherche des Taches, le commentaire d'une sous-routine\n");
    using app::apikit::searchTwoLevels;
    const std::vector<std::string> mast{"MAST", "cyclique"};
    const std::vector<std::vector<std::string>> entries{{"Gestion_pompes", "SR"}, {"Alarmes", "section"}};
    auto m = searchTwoLevels(ui::SearchQuery(""), mast, entries);
    check(m.parent && m.children == std::vector<bool>{true, true}, "une recherche vide garde tout");
    m = searchTwoLevels(ui::SearchQuery("MAST"), mast, entries);
    check(m.parent && m.children == std::vector<bool>{true, true}, "MAST : la tache et toutes ses entrees");
    m = searchTwoLevels(ui::SearchQuery("mast gestion"), mast, entries);
    check(m.parent && m.children == std::vector<bool>{true, false}, "MAST Gestion : la tache, l'entree Gestion");
    m = searchTwoLevels(ui::SearchQuery("alarmes"), mast, entries);
    check(m.parent && m.children == std::vector<bool>{false, true}, "Alarmes : la tache garde son entree");
    m = searchTwoLevels(ui::SearchQuery("-MAST"), mast, entries);
    check(!m.parent && m.children == std::vector<bool>{false, false}, "-MAST : rien");

    using app::apikit::leadingComment;
    check(leadingComment("(* R\xC3\xA9gulation du d\xC3\xA9" "bit\n   de la pompe *)\nX := 1;") == "R\xC3\xA9gulation du d\xC3\xA9" "bit de la pompe",
          "(* ... *) en tete : une ligne");
    check(leadingComment("  // ligne 1\n// ligne 2\nX := 1;") == "ligne 1 ligne 2", "des lignes // en tete");
    check(leadingComment("X := 1; (* pas en tete *)").empty(), "pas de commentaire en tete : rien");
    check(leadingComment("(* ********** *)").empty(), "un cadre d'etoiles : rien");
}

} // namespace

int main() {
    std::printf("filtermemory_test : les filtres retenus d'une seance a l'autre\n");
    textRetained();
    {
        // Sans memoire branchee (un outil, un test) : rien n'est lu ni ecrit.
        ui::FilterMemory::install({});
        auto t = openTable(kTable, kTitles, false);
        t->setColumnFilter(filterOf(1, ui::ColumnFilter::Op::Equals, "BOOL"));
        check(!ui::FilterMemory::installed() && ui::FilterMemory::read(kTable).empty(), "sans crochet : rien");
    }
    FakeSettings settings;
    settings.install();
    tableRemembers(settings);
    renamedColumn(settings);
    clearAll(settings);
    twoProjects(settings);
    searchField(settings);
    paneSearches();
    apiFilterBar(settings);
    ui::FilterMemory::install({});
    std::printf("%d verification(s), %d echec(s)\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
