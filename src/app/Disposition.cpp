// =============================================================================
//  app/Disposition.cpp - 1.12.3 : voir Disposition.hpp
// =============================================================================
#include "Disposition.hpp"

#include "../core/Edition.hpp"
#include "../ui/widgets/Containers.hpp"

#include <algorithm>

namespace app::disposition {

namespace {

const std::vector<Choice>& whenPanels() {
    static const std::vector<Choice> v{{"toujours", "Toujours"}, {"edition", "En \xC3\xA9" "dition"}, {"simulation", "En simulation"}};
    return v;
}
const std::vector<Choice>& whenPages() {
    static const std::vector<Choice> v{{"demande", "\xC3\x80 la demande"},
                                       {"projet", "\xC3\x80 l'ouverture du projet"},
                                       {"simulation", "Au d\xC3\xA9marrage de la simulation"}};
    return v;
}

struct Catalog {
    std::vector<Group> groups;
    std::vector<Row>   rows;
};

Catalog build(bool api, bool ihm) {
    Catalog c;
    const std::vector<Choice> pageWhere{{"onglet", "Dans un onglet"}, {"cote", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te"},
                                        {"detache", "Fen\xC3\xAAtre d\xC3\xA9tach\xC3\xA9" "e"}};
    const auto add = [&c](std::string id, std::string label, std::string group, Kind kind, std::string tabTitle = {},
                          std::vector<Choice> where = {}, std::vector<Choice> start = {}) {
        c.rows.push_back(Row{std::move(id), std::move(label), std::move(group), kind, std::move(tabTitle), std::move(where), std::move(start)});
    };
    const auto tab = [&add](std::string id, std::string label, std::string group, std::string title = {}) {
        if (title.empty()) title = label;
        add(std::move(id), std::move(label), std::move(group), Kind::Tab, std::move(title));
    };

    c.groups.push_back({"fenetre", "La fen\xC3\xAAtre", false});
    add("explorateur", "Explorateur du projet", "fenetre", Kind::Panel, {}, {{"gauche", "\xC3\x80 gauche"}, {"droite", "\xC3\x80 droite"}});
    add("documents", "Documents ouverts", "fenetre", Kind::Panel);
    add("bas", "Panneau du bas (Sorties, Console, Diagnostics)", "fenetre", Kind::Panel, {},
        {{"dessous", "Sous l'\xC3\xA9" "diteur"}, {"droite", "\xC3\x80 droite de l'\xC3\xA9" "diteur"}},
        {{"ouvert", "Ouvert"}, {"replie", "Repli\xC3\xA9 (Ctrl+J)"}});
    if (api) {
        // Les panneaux de XPGAnalyser API (Affichage > Panneaux a afficher, d'avant).
        add("configuration", "Configuration de l'automate", "fenetre", Kind::Panel);
        add("rangee", "Rang\xC3\xA9" "e du bas", "fenetre", Kind::Panel);
        add("diagnostics", "Diagnostics", "fenetre", Kind::Panel);
        add("resume", "R\xC3\xA9sum\xC3\xA9 de l'analyse", "fenetre", Kind::Panel);
        add("etatProjet", "\xC3\x89tat du projet", "fenetre", Kind::Panel);
    }
    add("etat", "Bande d'\xC3\xA9tat (en bas)", "fenetre", Kind::Panel);
    add("alternees", "Lignes altern\xC3\xA9" "es dans les listes", "fenetre", Kind::Option);

    c.groups.push_back({"bas", "Panneau du bas", true});
    tab("bas.sorties", "Sorties", "bas");
    tab("bas.console", "Console", "bas");
    tab("bas.diagnostics", "Diagnostics", "bas");

    if (ihm) {
        c.groups.push_back({"insp", "\xC3\x89" "diteur de vue \xE2\x80\xBA inspecteur", true});
        tab("vue.proprietes", "Propri\xC3\xA9t\xC3\xA9s", "insp");
        tab("vue.actions", "Actions", "insp");
        tab("vue.contenu", "Contenu (selon l'objet)", "insp", "Contenu");
        tab("vue.raccourcis", "Raccourcis (de la vue)", "insp", "Raccourcis");

        c.groups.push_back({"sim", "Simulation \xC2\xB7 IHM (panneau de droite)", true});
        tab("sim.expressions", "Expressions", "sim");
        tab("sim.variables", "Variables IHM", "sim");
        tab("sim.performances", "Performances", "sim");
        tab("sim.esclaves", "Esclaves simul\xC3\xA9s", "sim");
        tab("sim.popups", "Popups", "sim");

        c.groups.push_back({"prog", "Programmation g\xC3\xA9n\xC3\xA9rale", true});
        tab("prog.scripts", "Scripts g\xC3\xA9n\xC3\xA9raux", "prog");
        tab("prog.variables", "Variables IHM", "prog");
        tab("prog.types", "Types IHM", "prog");

        c.groups.push_back({"eq", "\xC3\x89quipements", true});
        tab("eq.reseau", "R\xC3\xA9seau du PC", "eq");
        tab("eq.scanner", "Scanner IP", "eq");
        tab("eq.equipements", "\xC3\x89quipements", "eq");
        tab("eq.plan", "Plan d'adressage", "eq");
        tab("eq.carte", "Carte m\xC3\xA9moire", "eq");
        tab("eq.valeurs", "Valeurs simul\xC3\xA9" "es", "eq");
        tab("eq.essai", "Essai", "eq");
        tab("eq.liaisons", "\xC3\x89tat des liaisons", "eq");

        c.groups.push_back({"pages", "Les pages du centre", false});
        add("page.vue", "Les vues (la vue de d\xC3\xA9marrage)", "pages", Kind::Page, {}, pageWhere);
        add("page.simulation", "Simulation \xC2\xB7 IHM", "pages", Kind::Page, {}, pageWhere);
        add("page.programmation", "Programmation g\xC3\xA9n\xC3\xA9rale", "pages", Kind::Page, {}, pageWhere);
        add("page.poste", "Poste d'exploitation", "pages", Kind::Page, {}, pageWhere);
    }
    return c;
}

const Catalog& catalog() {
    // Par edition (les essais en changent) : une fois chacune.
    static std::map<int, Catalog> cache;
    const bool api = core::hasApi(), ihm = core::hasIhm();
    const int key = (api ? 1 : 0) | (ihm ? 2 : 0);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, build(api, ihm)).first;
    return it->second;
}

const Item& noItem() {
    static const Item none;
    return none;
}

// "v=1;q=toujours;ou=droite;dep=replie" <-> Item (sep : ';' dans les reglages, ',' d'un bloc).
std::string itemText(const Item& it, char sep) {
    std::string s = std::string("v=") + (it.shown ? "1" : "0");
    if (!it.when.empty()) s += std::string(1, sep) + "q=" + it.when;
    if (!it.where.empty()) s += std::string(1, sep) + "ou=" + it.where;
    if (!it.start.empty()) s += std::string(1, sep) + "dep=" + it.start;
    return s;
}

bool known(const std::vector<Choice>& list, std::string_view key) {
    return std::any_of(list.begin(), list.end(), [key](const Choice& c) { return c.key == key; });
}

// Une valeur lue, gardee seulement si le catalogue la connait (un reglage abime ne casse rien).
void readItem(std::string_view text, char sep, const Row& r, Item& it) {
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto end = std::min(text.find(sep, at), text.size());
        const std::string_view part = text.substr(at, end - at);
        if (const auto eq = part.find('='); eq != std::string_view::npos) {
            const std::string_view k = part.substr(0, eq), v = part.substr(eq + 1);
            if (k == "v") it.shown = v != "0";
            else if (k == "q" && known(whenChoices(r.kind), v)) it.when = std::string(v);
            else if (k == "ou" && known(r.where, v)) it.where = std::string(v);
            else if (k == "dep" && known(r.start, v)) it.start = std::string(v);
        }
        at = end + 1;
    }
}

Layout& currentRef() {
    static Layout layout = defaults();
    return layout;
}
bool& simulatingRef() {
    static bool on = false;
    return on;
}

} // namespace

const std::vector<Group>& groups() { return catalog().groups; }
const std::vector<Row>& rows() { return catalog().rows; }

const Row* row(std::string_view id) {
    for (const auto& r : rows())
        if (r.id == id) return &r;
    return nullptr;
}

const std::vector<Choice>& whenChoices(Kind kind) {
    static const std::vector<Choice> none;
    if (kind == Kind::Page) return whenPages();
    if (kind == Kind::Option) return none;
    return whenPanels();
}

const Item& Layout::item(std::string_view id) const {
    const auto it = items.find(id);
    return it == items.end() ? noItem() : it->second;
}

Layout defaults() {
    Layout l;
    for (const auto& r : rows()) {
        Item it;
        const auto& when = whenChoices(r.kind);
        if (!when.empty()) it.when = when.front().key;
        if (!r.where.empty()) it.where = r.where.front().key;
        if (!r.start.empty()) it.start = r.start.front().key;
        l.items[r.id] = it;
    }
    // La vue de demarrage s'ouvre avec le projet (ce que fait l'application depuis la 1.12.0).
    if (const auto it = l.items.find("page.vue"); it != l.items.end()) it->second.when = "projet";
    for (const auto& g : groups()) {
        if (!g.tabs) continue;
        for (const auto& r : rows())
            if (r.group == g.id && r.kind == Kind::Tab) { l.startTab[g.id] = r.id; break; }
    }
    return l;
}

std::vector<Choice> presets() {
    std::vector<Choice> out{{"defaut", "Par d\xC3\xA9" "faut"}};
    if (core::hasIhm()) {
        out.push_back({"vues", "Dessin des vues"});
        out.push_back({"miseaupoint", "Mise au point"});
    }
    out.push_back({"large", "\xC3\x89" "cran large"});
    out.push_back({"mienne", "Ma disposition"});
    return out;
}

Layout preset(std::string_view key, const Layout* mine) {
    Layout l = defaults();
    const auto set = [&l](const char* id, auto&& change) {
        if (const auto it = l.items.find(id); it != l.items.end()) change(it->second);
    };
    const auto start = [&l](const char* group, const char* id) {
        if (row(id)) l.startTab[group] = id;
    };
    if (key == "vues") {
        // Dessiner : de la place pour la vue - pas de documents ouverts, les sorties repliees
        // et seulement en simulation ; la simulation dans sa fenetre.
        set("documents", [](Item& i) { i.shown = false; });
        set("bas", [](Item& i) { i.start = "replie"; i.when = "simulation"; });
        set("page.simulation", [](Item& i) { i.where = "detache"; });
    } else if (key == "miseaupoint") {
        // Mettre au point : la simulation a cote de la vue, des son demarrage ; la Console
        // au depart, les performances en simulation seulement.
        set("page.simulation", [](Item& i) { i.where = "cote"; i.when = "simulation"; });
        set("bas.console", [](Item& i) { i.when = "simulation"; });
        set("sim.performances", [](Item& i) { i.when = "simulation"; });
        set("documents", [](Item& i) { i.shown = false; });
        start("bas", "bas.console");
        start("sim", "sim.variables");
    } else if (key == "large") {
        // Un ecran large : le panneau du bas a droite de l'editeur, la simulation a cote.
        set("bas", [](Item& i) { i.where = "droite"; });
        set("page.simulation", [](Item& i) { i.where = "cote"; });
    } else if (key == "mienne") {
        if (mine) return *mine;
    }
    return l;
}

bool shown(const Layout& l, std::string_view id, bool simulating) {
    const auto it = l.items.find(id);
    if (it == l.items.end()) return true;
    const Item& i = it->second;
    if (!i.shown) return false;
    if (i.when == "edition") return !simulating;
    if (i.when == "simulation") return simulating;
    return true;
}

std::string startTitle(const Layout& l, std::string_view group) {
    const auto it = l.startTab.find(group);
    if (it == l.startTab.end()) return {};
    const Row* r = row(it->second);
    return r ? r->tabTitle : std::string{};
}

std::size_t differences(const Layout& a, const Layout& b) {
    std::size_t n = 0;
    for (const auto& r : rows())
        if (!(a.item(r.id) == b.item(r.id))) ++n;
    for (const auto& g : groups()) {
        if (!g.tabs) continue;
        const auto x = a.startTab.find(g.id), y = b.startTab.find(g.id);
        const std::string sx = x == a.startTab.end() ? std::string{} : x->second;
        const std::string sy = y == b.startTab.end() ? std::string{} : y->second;
        if (sx != sy) ++n;
    }
    return n;
}

Layout fromSettings(const Getter& get) {
    Layout l = defaults();
    if (!get) return l;
    for (const auto& r : rows()) {
        const std::string text = get(std::string(kPrefix) + r.id);
        if (!text.empty()) readItem(text, ';', r, l.items[r.id]);
    }
    for (const auto& g : groups()) {
        if (!g.tabs) continue;
        const std::string id = get(std::string(kPrefix) + "depart." + g.id);
        if (const Row* r = row(id); r && r->group == g.id && r->kind == Kind::Tab) l.startTab[g.id] = id;
    }
    return l;
}

void toSettings(const Layout& l, const Setter& set, const std::function<void(const std::string&)>& clear) {
    const Layout base = defaults();
    for (const auto& r : rows()) {
        const std::string key = std::string(kPrefix) + r.id;
        const Item& it = l.item(r.id);
        if (it == base.item(r.id)) {
            if (clear) clear(key);
        } else if (set) {
            set(key, itemText(it, ';'));
        }
    }
    for (const auto& g : groups()) {
        if (!g.tabs) continue;
        const std::string key = std::string(kPrefix) + "depart." + g.id;
        const auto it = l.startTab.find(g.id);
        const auto b = base.startTab.find(g.id);
        if (it == l.startTab.end() || (b != base.startTab.end() && it->second == b->second)) {
            if (clear) clear(key);
        } else if (set) {
            set(key, it->second);
        }
    }
}

std::string serialize(const Layout& l) {
    std::string out;
    for (const auto& r : rows()) {
        if (!out.empty()) out += '|';
        out += r.id + "=" + itemText(l.item(r.id), ',');
    }
    for (const auto& [g, id] : l.startTab) out += "|depart." + g + "=" + id;
    return out;
}

Layout parse(std::string_view text) {
    Layout l = defaults();
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end = std::min(text.find('|', at), text.size());
        const std::string_view part = text.substr(at, end - at);
        at = end + 1;
        const auto eq = part.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view id = part.substr(0, eq), value = part.substr(eq + 1);
        if (id.substr(0, 7) == "depart.") {
            const std::string g(id.substr(7));
            if (const Row* r = row(value); r && r->group == g && r->kind == Kind::Tab) l.startTab[g] = std::string(value);
            continue;
        }
        if (const Row* r = row(id)) readItem(value, ',', *r, l.items[r->id]);
    }
    return l;
}

const Layout& current() { return currentRef(); }

void setCurrent(Layout l) {
    if (l == currentRef()) return;
    currentRef() = std::move(l);
    changed()->emit();
}

bool simulating() { return simulatingRef(); }

void setSimulating(bool on) {
    if (on == simulatingRef()) return;
    simulatingRef() = on;
    changed()->emit();
}

const core::SignalPtr<>& changed() {
    static const core::SignalPtr<> signal = core::Signal<>::create();
    return signal;
}

bool shownNow(std::string_view id) { return shown(current(), id, simulating()); }
std::string startTitleNow(std::string_view group) { return startTitle(current(), group); }

void applyTabs(ui::TabControl& tabs, std::string_view group, bool first) {
    for (const auto& r : rows()) {
        if (r.group != group || r.kind != Kind::Tab) continue;
        for (std::size_t i = 0; i < tabs.tabCount(); ++i)
            if (const auto* t = tabs.tab(i); t && t->title == r.tabTitle) {
                const bool hide = !shownNow(r.id);
                if (tabs.tabHidden(i) != hide) tabs.setTabHidden(i, hide);
            }
    }
    if (!first) return;
    // Le sous-onglet du depart : seulement s'il a ete choisi (celui d'origine : la page garde le sien).
    const auto chosen = current().startTab.find(group);
    const Layout base = defaults();
    const auto origin = base.startTab.find(group);
    if (chosen == current().startTab.end() || (origin != base.startTab.end() && origin->second == chosen->second)) return;
    const std::string want = startTitleNow(group);
    for (std::size_t i = 0; !want.empty() && i < tabs.tabCount(); ++i)
        if (const auto* t = tabs.tab(i); t && t->title == want && !tabs.tabHidden(i)) {
            tabs.setCurrentIndex(i);
            break;
        }
}

} // namespace app::disposition
