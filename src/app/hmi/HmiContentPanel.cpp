#include "HmiContentPanel.hpp"
#include "../../core/Edition.hpp"   // 1.12.2 : XPGAnalyser IHM n'a pas d'automate

#include "HmiAssetPanes.hpp"
#include "HmiAssist.hpp"
#include "HmiIcons.hpp"
#include "HmiObjectAlarmPanes.hpp"    // 1.12.7 (lot D) : les groupes des alarmes des objets
#include "HmiPainter.hpp"
#include "../../hmi/HmiChoiceLists.hpp"   // 1.12.7 (lot D) : les colonnes choisies d'une source
#include "../../hmi/HmiForms.hpp"     // 1.12.7 (lot D) : les boutons de la gestion des utilisateurs
#include "../../hmi/HmiTypeForms.hpp" // 1.12.7 (lot D) : les colonnes que donne le type d'une source
#include "../../hmi/HmiMarkers.hpp"   // 1.11 (REP-1) : la recette sans les $ de ses reperes
#include "../../hmi/HmiRoles.hpp"     // 1.12.7 (lot D) : le decoupeur unique
#include "../../hmi/HmiExpr.hpp"
#include "../../hmi/HmiMedia.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../hmi/HmiControls.hpp"
#include "../../hmi/HmiEnums.hpp"     // 1.12.7 (lot E) : les etats, les onglets depuis une enumeration
#include "../../hmi/HmiWidgets.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/ExprField.hpp"   // 1.10 (chantier K) : les champs a expression, partout pareils

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app {

using hmi::Id;
using hmi::kNoId;
using PG = ui::PropertyGrid;

namespace {

enum Act : int { AAdd = 1, ARemove, AUp, ADown, AAddColumn, ARemoveColumn };

class Rows final : public ui::ITableModel {
public:
    Rows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, ui::Icon icon)
        : headers_(std::move(headers)), rows_(std::move(rows)), icon_(icon) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string{}; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows_.size()) return {};
        if (c == 0) return std::to_string(r + 1);
        return c - 1 < rows_[r].size() ? rows_[r][c - 1] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t c) const override {
        ui::CellStyle s;
        if (c == 1) s.icon = icon_;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    ui::Icon                              icon_;
};

// ------------------------------------------------------------- un tableau ---
struct TableData {
    std::vector<std::string>              headers, widths;
    std::vector<std::vector<std::string>> rows;
    [[nodiscard]] std::size_t columns() const { return headers.size(); }
};

TableData readTable(const hmi::Object& o) {
    TableData t;
    t.headers = hmi::splitSemicolons(o.text("columns"));
    t.widths = hmi::splitSemicolons(o.text("widths"));
    t.rows = hmi::parseCells(o.text("cells"));
    // Pas encore de cases : les lignes vides que dessine le tableau.
    if (t.rows.empty() && o.text("cells").empty()) {
        const int n = std::clamp(static_cast<int>(o.number("rows", 0)), 0, 200);
        t.rows.assign(static_cast<std::size_t>(n), {});
    }
    std::size_t n = t.headers.size();
    for (const auto& r : t.rows) n = std::max(n, r.size());
    n = std::max<std::size_t>(1, n);
    while (t.headers.size() < n) t.headers.push_back("Colonne " + std::to_string(t.headers.size() + 1));
    for (auto& r : t.rows) r.resize(n);
    if (!t.widths.empty()) t.widths.resize(n, "1");
    return t;
}

void writeTable(hmi::Object& o, const TableData& t) {
    o.set("columns", hmi::joinSemicolons(t.headers));
    o.set("cells", hmi::formatCells(t.rows));
    o.set("widths", hmi::joinSemicolons(t.widths));
    o.setNumber("rows", static_cast<double>(t.rows.size()));
}

std::string rowSummary(const std::vector<std::string>& cells) {
    std::string s;
    for (std::size_t k = 0; k < cells.size(); ++k) s += (k ? "  |  " : "") + (cells[k].empty() ? std::string("-") : cells[k]);
    return s;
}

// --------------------------------------------------------------- des plumes -
struct Pens {
    std::vector<std::string> exprs, names, colors;
    // 1.12.5 (lot A, D11) : les listes paralleles du tableau de variables - elles suivent leurs
    // lignes (avant : retirer ou deplacer une ligne laissait son unite et son format a la place).
    std::vector<std::string> units, formats;
};

void eraseAt(std::vector<std::string>& v, std::size_t i) {
    if (i < v.size()) v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
}
void swapAt(std::vector<std::string>& v, std::size_t a, std::size_t b) {
    if (v.empty()) return;
    if (std::max(a, b) >= v.size()) v.resize(std::max(a, b) + 1);
    std::swap(v[a], v[b]);
}

Pens readPens(const hmi::Object& o) {
    Pens p;
    p.exprs = hmi::splitSemicolons(o.text("variables"));
    p.names = hmi::splitSemicolons(o.text("names"));
    p.colors = hmi::splitSemicolons(o.text("colors"));
    p.units = hmi::splitSemicolons(o.text("units"));
    p.formats = hmi::splitSemicolons(o.text("formats"));
    p.names.resize(p.exprs.size());
    const auto& palette = hmiTrendPalette();
    while (p.colors.size() < p.exprs.size()) p.colors.push_back(palette[p.colors.size() % palette.size()]);
    return p;
}

void writePens(hmi::Object& o, Pens p) {
    o.set("variables", hmi::joinSemicolons(p.exprs));
    const bool named = std::any_of(p.names.begin(), p.names.end(), [](const std::string& n) { return !n.empty(); });
    o.set("names", named ? hmi::joinSemicolons(p.names) : std::string{});
    // Les couleurs des plumes, puis celles qui attendent les plumes suivantes.
    const auto& palette = hmiTrendPalette();
    if (p.colors.size() < p.exprs.size() + 1) p.colors.push_back(palette[p.exprs.size() % palette.size()]);
    o.set("colors", hmi::joinSemicolons(p.colors));
    // 1.12.5 (D11) : les unites et les formats d'un tableau de variables, ligne par ligne.
    if (o.find("units") || !p.units.empty()) {
        if (p.units.size() > p.exprs.size()) p.units.resize(p.exprs.size());
        o.set("units", hmi::joinSemicolons(p.units));
    }
    if (o.find("formats") || !p.formats.empty()) {
        if (p.formats.size() > p.exprs.size()) p.formats.resize(p.exprs.size());
        o.set("formats", hmi::joinSemicolons(p.formats));
    }
}

std::string imagesText(const std::vector<std::string>& images) {
    std::string s;
    for (std::size_t i = 0; i < images.size(); ++i) s += (i ? ", " : "") + images[i];
    return s;
}

// La derniere suite de chiffres d'un nom : "En-tete 3" -> 3 ; "2 - Valeur" -> 2.
int numberIn(std::string_view s, bool last) {
    int value = -1;
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] >= '0' && s[i] <= '9') {
            int v = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
            value = v;
            if (!last) return value;
        } else {
            ++i;
        }
    }
    return value;
}

// Lot 11 : le nom d'un element de la liste, selon l'objet (une plume, une barre...).
std::string penWord(const hmi::Object* o) {
    if (!o) return "Plume";
    switch (o->kind) {
        case hmi::Kind::BarChart: return "Barre";
        case hmi::Kind::PieChart: return "Part";
        case hmi::Kind::RadarChart: return "Axe";
        case hmi::Kind::XYChart: return "Courbe";
        case hmi::Kind::StateChart: return "Ligne";
        case hmi::Kind::VariableTable: return "Variable";
        default: return "Plume";
    }
}

// ------------------------------------------------ 1.12.3 : les etats -------
// Les etats d'un voyant ou d'un texte multi-etats, et comment l'etat se choisit : selon la
// valeur (la case Valeur), ou selon des conditions - la Valeur est alors leur chaine
// "(C1) ? 1 : (C2) ? 2 : 0", relue ici, et les etats 1..N (plus le « sinon », *).
struct StateTable {
    std::vector<hmi::StateEntry> states;
    bool                         conditions{false};
    std::vector<std::string>     conds;       // une par etat numerote, dans l'ordre de la liste
};

StateTable readStates(const hmi::Object& o) {
    StateTable t;
    t.states = hmi::parseStateList(o.text("stateList"));
    const auto* v = o.find("value");
    std::vector<std::string> conds;
    if (v && !v->expr.empty() && hmi::parseConditionChain(v->expr, conds)) {
        std::size_t numbered = 0;
        bool ordered = true;
        for (const auto& st : t.states) {
            if (st.match == "*") continue;
            if (st.match != std::to_string(++numbered)) { ordered = false; break; }
        }
        if (ordered && numbered == conds.size()) {
            t.conditions = true;
            t.conds = std::move(conds);
        }
    }
    return t;
}

void writeStates(hmi::Object& o, StateTable t) {
    if (t.conditions) {
        std::size_t k = 0;
        for (auto& st : t.states)
            if (st.match != "*") st.match = std::to_string(++k);
        t.conds.resize(k, "FALSE");
        o.setExpr("value", hmi::conditionChain(t.conds));
    }
    o.set("stateList", hmi::formatStateList(t.states));
}

// La condition d'une ligne (les etats numerotes, dans l'ordre) ; -1 : le « sinon ».
int conditionIndexOf(const StateTable& t, std::size_t row) {
    if (row >= t.states.size() || t.states[row].match == "*") return -1;
    int k = 0;
    for (std::size_t r = 0; r < row; ++r) k += t.states[r].match != "*";
    return k;
}

// Une valeur libre pour un etat de plus : le plus grand nombre + 1.
std::string nextStateValue(const StateTable& t) {
    double top = -1;
    for (const auto& st : t.states) {
        double x = 0;
        // Une plage (10..20) compte par sa borne haute.
        const auto dots = st.match.find("..");
        const std::string last = dots == std::string::npos ? st.match : st.match.substr(dots + 2);
        if (hmi::parseNumber(last, x)) top = std::max(top, x);
    }
    return hmi::formatNumber(std::floor(top) + 1);
}

std::string stateDetail(const hmi::StateEntry& st) {
    std::string d = st.text;
    if (!st.color.empty()) d += "  \xC2\xB7  " + st.color;
    if (st.blink) d += "  \xC2\xB7  clignote";
    return d;
}

// --------------------------------------------------------- lot 12 : la barre -
struct NavList {
    std::vector<std::string> views, labels;
    bool                     automatic{false};   // "views" vide : les vues ordinaires du projet
};

NavList readNav(const hmi::Project& p, const hmi::Object& o) {
    NavList n;
    n.automatic = hmi::listItems(o.text("views")).empty();
    for (const auto& it : hmi::navItems(&p, o)) {
        n.views.push_back(it.view);
        n.labels.push_back(it.label == hmi::viewCaption(it.view) ? std::string{} : it.label);
    }
    return n;
}

void writeNav(hmi::Object& o, const NavList& n) {
    o.set("views", hmi::joinSemicolons(n.views));
    const bool named = std::any_of(n.labels.begin(), n.labels.end(), [](const std::string& l) { return !l.empty(); });
    o.set("labels", named ? hmi::joinSemicolons(n.labels) : std::string{});
}

// Un texte sans les blancs du bord.
std::string trimmed(std::string v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    return v;
}
// Un morceau qui se glisse dans une liste a;b;c sans la couper : le decoupeur unique n'y trouve
// qu'un element (un ';' dans une chaine 'a;b' ou un repere $A;B$ passe ; un ';' nu, non).
bool fitsInList(std::string_view text) { return hmi::roles::splitList(text, ';').size() <= 1; }

// ---------------------------------------------- 1.12.7 (lot D) : les choix ---
// Les choix d'un selecteur ("positions") ou d'une liste ("items") et la valeur ecrite de chacun
// ("values", au meme rang ; vide : le rang), une ligne par choix - comme le moteur les lit
// (choicesOf : un libelle vide ne compte pas). Chaque case garde ses reperes tels quels.
struct ChoiceRows {
    std::vector<std::string> labels, values;   // values : autant que labels (vide : le rang)
};

const char* choiceLabelsKey(const hmi::Object& o) { return o.kind == hmi::Kind::Selector ? "positions" : "items"; }

ChoiceRows readChoices(const hmi::Object& o) {
    ChoiceRows c;
    c.labels = hmi::listItems(o.text(choiceLabelsKey(o)));
    if (const std::string raw = o.text("values"); !raw.empty()) c.values = hmi::roles::splitList(raw, ';');
    c.values.resize(c.labels.size());
    return c;
}

void writeChoices(hmi::Object& o, ChoiceRows c) {
    o.set(choiceLabelsKey(o), hmi::joinSemicolons(c.labels));
    c.values.resize(c.labels.size());
    while (!c.values.empty() && c.values.back().empty()) c.values.pop_back();   // les rangs de la fin : rien a ecrire
    o.set("values", hmi::joinSemicolons(c.values));
}

// ---------------------------------- 1.12.7 (lot D) : les zones d'une echelle ---
// "de-a = couleur" (bargraphe, cadran), lu en texte : une borne garde ce qu'on a tape (un
// nombre, 60 %, un repere $Bas$) ; reecrite "de..a = couleur" (le tiret d'un nombre negatif ne
// trompe plus personne).
struct BandRow {
    std::string from, to, color;
};

std::vector<BandRow> readBands(const hmi::Object& o) {
    std::vector<BandRow> out;
    for (const auto& entry : hmi::listItems(o.text("zones"))) {
        BandRow b;
        const auto eq = entry.find('=');
        const std::string range = trimmed(entry.substr(0, eq == std::string::npos ? entry.size() : eq));
        b.color = eq == std::string::npos ? std::string{} : trimmed(entry.substr(eq + 1));
        if (const auto dots = range.find(".."); dots != std::string::npos) {
            b.from = trimmed(range.substr(0, dots));
            b.to = trimmed(range.substr(dots + 2));
        } else {
            b.from = range;
            for (std::size_t k = 1; k < range.size(); ++k) {
                if (range[k] != '-') continue;
                std::size_t p = k;
                while (p > 0 && range[p - 1] == ' ') --p;
                const char before = p > 0 ? range[p - 1] : ' ';
                if (std::isdigit(static_cast<unsigned char>(before)) || before == '.' || before == '%' || before == '$') {
                    b.from = trimmed(range.substr(0, k));
                    b.to = trimmed(range.substr(k + 1));
                    break;
                }
            }
        }
        out.push_back(std::move(b));
    }
    return out;
}

void writeBands(hmi::Object& o, const std::vector<BandRow>& bands) {
    std::vector<std::string> parts;
    for (const auto& b : bands) parts.push_back(b.from + ".." + b.to + " = " + b.color);
    o.set("zones", hmi::joinSemicolons(parts));
}

// ------------------------------------ 1.12.7 (lot D) : les plages d'un programme ---
// "Lu-Ve=07:00-12:00,13:30-17:00; Sa=08:00-12:00" : une ligne par plage (jours, de, a). Reecrit
// une entree par ligne ("Lu-Ve=07:00-12:00; Lu-Ve=13:30-17:00") : le programmateur les additionne.
struct SlotRow {
    std::string days, from, to;
};

std::vector<SlotRow> readSlots(const hmi::Object& o) {
    std::vector<SlotRow> out;
    for (const auto& entry : hmi::listItems(o.text("schedule"))) {
        const auto eq = entry.find('=');
        const std::string days = trimmed(entry.substr(0, eq == std::string::npos ? entry.size() : eq));
        const std::string ranges = eq == std::string::npos ? std::string{} : entry.substr(eq + 1);
        bool any = false;
        for (const auto& r : hmi::roles::splitList(ranges, ',')) {
            if (r.empty()) continue;
            SlotRow s;
            s.days = days;
            const auto dash = r.find('-');
            s.from = trimmed(r.substr(0, dash == std::string::npos ? r.size() : dash));
            s.to = dash == std::string::npos ? std::string{} : trimmed(r.substr(dash + 1));
            out.push_back(std::move(s));
            any = true;
        }
        if (!any) out.push_back({days, {}, {}});
    }
    return out;
}

void writeSlots(hmi::Object& o, const std::vector<SlotRow>& slots) {
    std::vector<std::string> parts;
    for (const auto& s : slots) parts.push_back(s.days + "=" + s.from + "-" + s.to);
    o.set("schedule", hmi::joinSemicolons(parts));
}

// "07:00", "7h30", "7" -> minutes ; -1 : illisible (comme le programmateur).
int slotMinutes(std::string_view text) {
    std::string t = trimmed(std::string(text));
    for (auto& c : t) if (c == 'h' || c == 'H') c = ':';
    int h = 0, m = 0;
    char colon = 0;
    const int n = std::sscanf(t.c_str(), "%d%c%d", &h, &colon, &m);
    if (n == 1) m = 0;
    else if (n == 2 && colon == ':') m = 0;
    else if (n != 3 || colon != ':') return -1;
    if (h < 0 || h > 24 || m < 0 || m > 59 || (h == 24 && m != 0)) return -1;
    return h * 60 + m;
}

// Ce que devient une plage a la resolution du programmateur (hmi::scheduleRoundings, la lecture de
// Generer) : "" si elle tombe juste ; sinon « arrondie a 07:00-07:30 » ou « disparait ».
std::string slotRounding(const SlotRow& s, int res) {
    const int a = slotMinutes(s.from), b = slotMinutes(s.to);
    if (a < 0 || b < 0 || b <= a) return "illisible (07:00-12:00)";
    const auto r = hmi::scheduleRoundings(s.days + "=" + s.from + "-" + s.to, res);
    if (r.empty()) return {};
    const std::string grain = res == 60 ? std::string("1 h") : std::to_string(res) + " min";
    if (r.front().gone) return "dispara\xC3\xAEt : plus courte qu'une case (" + grain + ")";
    return "arrondie \xC3\xA0 " + r.front().becomes + " (r\xC3\xA9solution " + grain + ")";
}

// ------------------------------- 1.12.7 (lot D) : les colonnes d'une source ---
// Un tableau dont les lignes viennent d'une variable (Lignes depuis) : les colonnes que donne son type,
// et celles qu'on a choisies (sourceColumns : lesquelles, dans quel ordre, leur titre, leur format).
struct SourceCols {
    std::vector<std::string>                 available;   // les colonnes de la source, dans l'ordre
    std::vector<hmi::choices::SourceColumn>  chosen;      // vide : toutes, sous leur nom
    [[nodiscard]] const hmi::choices::SourceColumn* find(std::string_view member) const {
        for (const auto& c : chosen)
            if (c.member == member) return &c;
        return nullptr;
    }
    // Les lignes : les colonnes choisies dans leur ordre, puis les autres de la source.
    [[nodiscard]] std::vector<std::string> rows() const {
        if (chosen.empty()) return available;
        std::vector<std::string> out;
        for (const auto& c : chosen) out.push_back(c.member);
        for (const auto& a : available)
            if (!find(a)) out.push_back(a);
        return out;
    }
    // Tout est montre, dans l'ordre, sous son nom : la propriete se vide (comme avant).
    [[nodiscard]] bool plain() const {
        if (chosen.size() != available.size()) return false;
        for (std::size_t i = 0; i < chosen.size(); ++i)
            if (chosen[i].member != available[i] || !chosen[i].title.empty() || !chosen[i].format.empty()) return false;
        return true;
    }
};

std::string rowsSourceOf(const hmi::Object& o) { return trimmed(hmi::markers::strip(o.text("rowsFrom"))); }

SourceCols readSourceCols(const hmi::Project& p, const hmi::Object& o) {
    SourceCols c;
    if (const auto* var = p.variable(rowsSourceOf(o))) c.available = hmi::typeform::columnsOf(p, var->type);
    c.chosen = hmi::choices::parseSourceColumns(o.text(hmi::choices::kSourceColumnsKey));
    return c;
}

void writeSourceCols(hmi::Object& o, SourceCols c) {
    o.set(hmi::choices::kSourceColumnsKey, c.plain() ? std::string{} : hmi::choices::formatSourceColumns(c.chosen));
}

// ------------------------------------------ 1.12.7 (lot D) : les cases a cocher ---
// Ce qui se propose (les langues du projet, les boutons de la gestion des utilisateurs, les
// groupes d'alarmes et ceux des objets) et ce que la propriete porte. Ce qui a ete tape et ne
// se propose pas (un repere $Zone$, un groupe d'une autre version) reste, coche, a la fin.
struct CheckRows {
    std::string              key;          // la propriete : "languages", "buttons", "groups"
    std::vector<std::string> offered;      // dans leur ordre
    std::vector<std::string> details;      // une ligne d'aide par proposition
    std::vector<std::string> chosen;       // ce que porte la propriete, dans son ordre
    bool                     emptyIsAll{false};   // vide : toutes (langues, zones)
    [[nodiscard]] bool has(std::string_view item) const {
        return std::find(chosen.begin(), chosen.end(), item) != chosen.end();
    }
    // Les lignes montrees : les propositions, puis ce qui a ete tape en plus.
    [[nodiscard]] std::vector<std::string> rows() const {
        auto out = offered;
        for (const auto& c : chosen)
            if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
        return out;
    }
};

CheckRows readChecks(const hmi::Project& p, const hmi::Object& o) {
    CheckRows c;
    switch (o.kind) {
        case hmi::Kind::LanguageSelector:
            c.key = "languages";
            c.emptyIsAll = true;
            for (const auto& l : p.languages.list) {
                c.offered.push_back(l.code);
                c.details.push_back(l.name.empty() ? l.code : l.name);
            }
            break;
        case hmi::Kind::UserManager:
            c.key = "buttons";
            for (const auto b : hmi::kUserButtons) {
                c.offered.emplace_back(b);
                c.details.emplace_back("bouton");
            }
            break;
        case hmi::Kind::AlarmSummary:
            c.key = "groups";
            c.emptyIsAll = true;
            for (const auto& a : p.alarms)
                if (!a.group.empty() && std::find(c.offered.begin(), c.offered.end(), a.group) == c.offered.end()) {
                    c.offered.push_back(a.group);
                    c.details.emplace_back("groupe du projet");
                }
            for (const auto& gc : objalarms::groupChoices(p))
                if (!gc.value.empty() && std::find(c.offered.begin(), c.offered.end(), gc.value) == c.offered.end()) {
                    c.offered.push_back(gc.value);
                    c.details.push_back(gc.section + (gc.detail.empty() ? std::string{} : "  \xC2\xB7  " + gc.detail));
                }
            break;
        default: return c;
    }
    for (auto& item : hmi::listItems(o.text(c.key))) c.chosen.push_back(std::move(item));
    return c;
}

// La propriete avec `item` coche ou non : les propositions cochees dans leur ordre, puis ce
// qui a ete tape en plus. Faux : rien ne change, ou la derniere case d'une liste « vide : toutes ».
bool toggleCheck(hmi::Object& o, const CheckRows& c, const std::string& item, bool on) {
    if (c.has(item) == on) return false;
    std::vector<std::string> next;
    for (const auto& r : c.rows()) {
        const bool keep = r == item ? on : c.has(r);
        if (keep) next.push_back(r);
    }
    if (next.empty() && c.emptyIsAll) return false;   // vide voudrait dire « toutes »
    o.set(c.key, hmi::joinSemicolons(next));
    return true;
}

// ------------------------------- 1.12.7 (lot E) : depuis une enumeration ---
// Ce que propose « Depuis une enumeration » : les enumerations du projet, puis les variables IHM d'un type
// enumere (choisie, la variable pilote aussi l'objet). `variablesOnly` : une image animee, dont les
// conditions comparent une variable.
std::vector<std::string> enumSources(const hmi::Project& p, bool variablesOnly) {
    std::vector<std::string> out{""};
    if (!variablesOnly)
        for (const auto* e : hmi::enumerations(p)) out.push_back(e->name);
    for (const auto& var : p.programs.variables)
        if (hmi::findEnumeration(p, var.type)) out.push_back(var.name);
    return out;
}
// L'enumeration d'un choix (son nom, ou le type de la variable choisie ; `variable` : la variable).
const hmi::HmiType* enumOf(const hmi::Project& p, std::string_view chosen, std::string* variable) {
    if (variable) variable->clear();
    if (const auto* e = hmi::findEnumeration(p, chosen)) return e;
    if (const auto* var = p.variable(chosen))
        if (const auto* e = hmi::findEnumeration(p, var->type)) {
            if (variable) *variable = var->name;
            return e;
        }
    return nullptr;
}

} // namespace

HmiContentPanel::HmiContentPanel(std::string id, hmi::DocumentPtr doc, Id view, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), view_(view), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(AAdd, HmiGlyph::Plus, "Ajouter (une ligne, une plume, un \xC3\xA9tat)", "Ajouter");
    tools->add(ARemove, HmiGlyph::Delete, "Retirer l'\xC3\xA9l\xC3\xA9ment choisi");
    tools->add(AUp, HmiGlyph::Up, "Monter l'\xC3\xA9l\xC3\xA9ment (l'ordre compte : le premier \xC3\xA9tat vrai l'emporte)");
    tools->add(ADown, HmiGlyph::Down, "Descendre l'\xC3\xA9l\xC3\xA9ment");
    tools->separator();
    tools->add(AAddColumn, HmiGlyph::Table, "Ajouter une colonne au tableau", "Colonne");
    tools->add(ARemoveColumn, HmiGlyph::Delete, "Retirer la colonne (celle de la derni\xC3\xA8re case \xC3\xA9" "dit\xC3\xA9" "e, sinon la derni\xC3\xA8re)");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    const auto editable = [this] {
        const auto m = mode();
        return m == Mode::Table || m == Mode::Trend || m == Mode::AnimatedImage || m == Mode::Zones || m == Mode::Tabs || m == Mode::NavItems
            || m == Mode::States || m == Mode::Choices || m == Mode::Bands || m == Mode::Schedule || m == Mode::SourceColumns;   // 1.12.7 (lot D)
    };
    tools_->setEnabledWhen(AAdd, editable);
    tools_->setEnabledWhen(ARemove, [this, editable] { return editable() && selectedIndex() >= 0; });
    tools_->setEnabledWhen(AUp, [this, editable] { return editable() && selectedIndex() > 0; });
    tools_->setEnabledWhen(ADown, [this, editable] {
        return editable() && selectedIndex() >= 0 && selectedIndex() + 1 < static_cast<int>(count());
    });
    tools_->setEnabledWhen(AAddColumn, [this] { return mode() == Mode::Table; });
    tools_->setEnabledWhen(ARemoveColumn, [this] {
        const auto* o = current();
        return mode() == Mode::Table && o && readTable(*o).columns() > 1;
    });
    auto table = std::make_unique<ui::TableView>(base + ".list");
    table->setColumns({{"#", 40.f, 32.f, false, false, true, ui::Align::End}, {"\xC3\x89l\xC3\xA9ment", 300.f}, {"D\xC3\xA9tail", 260.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setShowDescriptionPane(true);
    grid->setNameColumnRatio(0.42f);
    const auto src = assist::sourcesFor(doc_);
    grid->setFieldAssist([this, basic = assist::gridAssist(src), cell = assist::templateOrExpression(src)](
                             std::string_view cat, const PG::Property& p) -> ui::InputText::Assist {
        // Une case d'une ligne ("2 - Valeur") : sa colonne est retenue (Retirer la
        // colonne), et l'aide est celle d'un texte a trous ou d'une expression.
        if (mode() == Mode::Table && cat.rfind("Ligne", 0) == 0) {
            lastColumn_ = numberIn(p.name, false) - 1;
            return cell;
        }
        if (mode() == Mode::Table && cat == "Colonnes") lastColumn_ = numberIn(p.name, true) - 1;
        return basic(cat, p);
    });
    grid->setPaletteColors([doc = doc_] { return doc ? assist::projectColors(doc->project) : std::vector<std::string>{}; });
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));

    links_ += tools_->triggered->connect([this](int a) {
        const int sel = selectedIndex();
        switch (a) {
            case AAdd: if (addItem()) selectIndex(static_cast<int>(count()) - 1); break;
            case ARemove: (void)removeItem(sel); break;
            case AUp: if (moveItem(sel, -1)) selectIndex(sel - 1); break;
            case ADown: if (moveItem(sel, +1)) selectIndex(sel + 1); break;
            case AAddColumn: (void)addColumn(); break;
            case ARemoveColumn: (void)removeColumn(lastColumn_); break;
            default: break;
        }
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        selected_ = rows.empty() ? -1 : static_cast<int>(rows.front());
        rebuildGrid();
    });
    refresh();
}

const hmi::Object* HmiContentPanel::current() const {
    const auto* v = doc_->project.view(view_);
    return v && object_ != kNoId ? v->object(object_) : nullptr;
}

HmiContentPanel::Mode HmiContentPanel::mode() const {
    const auto* o = current();
    if (!o) return Mode::None;
    switch (o->kind) {
        // 1.12.7 (lot D) : un tableau dont les lignes viennent d'une source - ses colonnes.
        case hmi::Kind::Table:         return rowsSourceOf(*o).empty() ? Mode::Table : Mode::SourceColumns;
        case hmi::Kind::Trend:         return Mode::Trend;
        case hmi::Kind::AnimatedImage: return Mode::AnimatedImage;
        case hmi::Kind::RecipeManager: return Mode::RecipeManager;
        // Lot 11 : les listes d'expressions des graphiques et du tableau de
        // variables se reglent comme les plumes d'une courbe.
        case hmi::Kind::BarChart: case hmi::Kind::PieChart: case hmi::Kind::RadarChart: case hmi::Kind::XYChart:
        case hmi::Kind::VariableTable:
            return Mode::Trend;
        // 1.12.7 (lot D) : un chronogramme montre ses lignes, ou ses etats (le tableau du multi-etats).
        case hmi::Kind::StateChart:    return chartStates_ ? Mode::States : Mode::Trend;
        // 1.12.7 (lot D) : les choix, les zones d'une echelle, les plages, les cases a cocher.
        case hmi::Kind::Selector: case hmi::Kind::ComboBox: case hmi::Kind::RadioGroup: case hmi::Kind::List:
            return Mode::Choices;
        case hmi::Kind::Bargraph: case hmi::Kind::Dial:
            return Mode::Bands;
        case hmi::Kind::WeeklySchedule: return Mode::Schedule;
        case hmi::Kind::LanguageSelector: case hmi::Kind::UserManager: case hmi::Kind::AlarmSummary:
            return Mode::Checks;
        // Lot 12
        case hmi::Kind::ZoneMap:       return Mode::Zones;
        case hmi::Kind::TabContainer:  return Mode::Tabs;
        case hmi::Kind::NavBar:        return Mode::NavItems;
        // 1.12.3 : les etats, une ligne chacun.
        case hmi::Kind::MultiStateIndicator: case hmi::Kind::MultiStateText:
            return Mode::States;
        // 1.10.4 (K3) : ce qui tient d'autres objets - ses objets.
        case hmi::Kind::Group: case hmi::Kind::Container: case hmi::Kind::Frame: case hmi::Kind::ScrollPanel:
        case hmi::Kind::CollapsiblePanel:
            return Mode::Members;
        default:                       return Mode::None;
    }
}

std::size_t HmiContentPanel::count() const {
    const auto* o = current();
    if (!o) return 0;
    switch (mode()) {
        case Mode::Table:         return readTable(*o).rows.size();
        case Mode::Trend:         return readPens(*o).exprs.size();
        case Mode::AnimatedImage: return hmi::parseImageStates(o->text("states")).size();
        case Mode::RecipeManager: {
            const auto* r = doc_->project.recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text));
            return r ? r->records.size() : 0;
        }
        case Mode::Zones:    return hmi::parseMapZones(o->text("mapZones")).size();
        case Mode::States:   return hmi::parseStateList(o->text("stateList")).size();
        case Mode::Choices:  return readChoices(*o).labels.size();          // 1.12.7 (lot D)
        case Mode::Bands:    return readBands(*o).size();
        case Mode::Schedule: return readSlots(*o).size();
        case Mode::Checks:   return readChecks(doc_->project, *o).rows().size();
        case Mode::SourceColumns: return readSourceCols(doc_->project, *o).rows().size();
        case Mode::Tabs:     return hmi::tabLabels(*o).size();
        case Mode::NavItems: return hmi::navItems(&doc_->project, *o).size();
        case Mode::Members: {
            std::size_t n = 0;
            if (const auto* v = doc_->project.view(view_))
                for (const auto& c : v->objects) n += c.parent == o->id ? 1u : 0u;
            return n;
        }
        case Mode::None: break;
    }
    return 0;
}

void HmiContentPanel::setObject(Id object) {
    if (object == object_) { refresh(); return; }
    object_ = object;
    selected_ = -1;
    lastColumn_ = -1;
    chartStates_ = false;   // 1.12.7 : un autre chronogramme montre d'abord ses lignes
    refresh();
    if (count() > 0 && mode() != Mode::RecipeManager) selectIndex(0);
}

int HmiContentPanel::selectedIndex() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(count())) return -1;
    return selected_;
}

void HmiContentPanel::selectIndex(int index) {
    if (index < 0 || index >= static_cast<int>(count())) return;
    hmiSelectModelRow(*table_, static_cast<ui::RowIndex>(index));
    selected_ = index;
    rebuildGrid();
}

void HmiContentPanel::refresh() {
    const auto* o = current();
    std::vector<std::string> headers{"#", "\xC3\x89l\xC3\xA9ment", "D\xC3\xA9tail"};
    std::vector<std::vector<std::string>> rows;
    ui::Icon icon = ui::Icon::Document;
    if (o) {
        switch (mode()) {
            case Mode::Table: {
                const auto t = readTable(*o);
                headers = {"#", "Ligne", "Cases dynamiques"};
                icon = ui::Icon::AnimationTable;
                for (const auto& r : t.rows) {
                    std::size_t driven = 0;
                    for (const auto& c : r) driven += hmi::cellIsExpression(c) || hmi::cellIsTemplate(c);
                    rows.push_back({rowSummary(r), driven ? std::to_string(driven) : std::string{}});
                }
                break;
            }
            case Mode::Trend: {
                const auto p = readPens(*o);
                headers = {"#", penWord(o), "Expression"};
                icon = ui::Icon::Chart;
                for (std::size_t i = 0; i < p.exprs.size(); ++i) {
                    std::string detail = p.exprs[i];
                    // 1.12.7 (lot D) : un tableau de variables montre l'unite et le format de chaque ligne.
                    if (o->kind == hmi::Kind::VariableTable) {
                        if (i < p.units.size() && !p.units[i].empty()) detail += "  \xC2\xB7  " + p.units[i];
                        if (i < p.formats.size() && !p.formats[i].empty()) detail += "  \xC2\xB7  " + p.formats[i];
                    }
                    rows.push_back({p.names[i].empty() ? p.exprs[i] : p.names[i], std::move(detail)});
                }
                break;
            }
            case Mode::AnimatedImage: {
                headers = {"#", "Condition", "Images"};
                icon = ui::Icon::Play;
                for (const auto& st : hmi::parseImageStates(o->text("states")))
                    rows.push_back({st.condition.empty() ? std::string("TRUE") : st.condition,
                                    imagesText(st.images) + (st.periodMs > 0 ? "  @ " + std::to_string(st.periodMs) + " ms" : std::string{})});
                break;
            }
            case Mode::RecipeManager: {
                headers = {"#", "Jeu", "Valeurs"};
                icon = ui::Icon::AnimationTable;
                if (const auto* r = doc_->project.recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text)))
                    for (const auto& rec : r->records) {
                        std::string values;
                        for (std::size_t k = 0; k < rec.values.size(); ++k)
                            values += (k ? "; " : "") + hmi::unescapeText(rec.values[k]);
                        rows.push_back({rec.name, values});
                    }
                break;
            }
            case Mode::Zones: {
                headers = {"#", "Zone", "Groupe  \xC2\xB7  vue"};
                icon = ui::Icon::Screen;
                for (const auto& z : hmi::parseMapZones(o->text("mapZones")))
                    rows.push_back({z.name, z.alarmGroup() + (z.view.empty() ? std::string{} : "  \xC2\xB7  \xE2\x86\x92 " + z.view)});
                break;
            }
            case Mode::Tabs: {
                headers = {"#", "Page", "Objets"};
                icon = ui::Icon::Folder;
                const auto* v = doc_->project.view(view_);
                const auto labels = hmi::tabLabels(*o);
                for (std::size_t k = 0; k < labels.size(); ++k) {
                    std::size_t n = 0;
                    if (v)
                        for (const auto& c : v->objects)
                            if (c.parent == o->id && hmi::tabPageOf(c) == static_cast<int>(k) + 1) ++n;
                    rows.push_back({labels[k], std::to_string(n)});
                }
                break;
            }
            case Mode::NavItems: {
                headers = {"#", "Vue", "Libell\xC3\xA9"};
                icon = ui::Icon::Screen;
                for (const auto& it : hmi::navItems(&doc_->project, *o)) rows.push_back({it.view, it.label});
                break;
            }
            case Mode::States: {
                const auto t = readStates(*o);
                headers = {"#", t.conditions ? "Quand (la premi\xC3\xA8re vraie gagne)" : "Quand la valeur vaut", "Texte  \xC2\xB7  couleur"};
                icon = ui::Icon::Info;
                for (std::size_t r = 0; r < t.states.size(); ++r) {
                    const auto& st = t.states[r];
                    const int k = conditionIndexOf(t, r);
                    std::string when = st.match == "*" ? std::string("sinon") : t.conditions && k >= 0 ? t.conds[static_cast<std::size_t>(k)] : st.match;
                    rows.push_back({std::move(when), stateDetail(st)});
                }
                break;
            }
            // 1.12.7 (lot D) : les choix, les zones, les plages, les cases a cocher.
            case Mode::Choices: {
                const auto c = readChoices(*o);
                headers = {"#", "Libell\xC3\xA9", "Valeur \xC3\xA9" "crite"};
                icon = ui::Icon::Info;
                for (std::size_t k = 0; k < c.labels.size(); ++k)
                    rows.push_back({c.labels[k], c.values[k].empty() ? "(rang " + std::to_string(k) + ")" : c.values[k]});
                break;
            }
            case Mode::Bands: {
                headers = {"#", "De \xE2\x80\xA6 \xC3\xA0", "Couleur"};
                icon = ui::Icon::Chart;
                for (const auto& b : readBands(*o)) rows.push_back({b.from + " \xE2\x80\xA6 " + b.to, b.color});
                break;
            }
            case Mode::Schedule: {
                headers = {"#", "Jours", "De \xE2\x80\xA6 \xC3\xA0"};
                icon = ui::Icon::Play;
                const int res = hmi::resolutionMinutes(o->text("resolution", "30 min"));
                for (const auto& sl : readSlots(*o)) {
                    const std::string round = slotRounding(sl, res);
                    rows.push_back({sl.days, sl.from + " \xE2\x80\xA6 " + sl.to + (round.empty() ? std::string{} : "  (" + round + ")")});
                }
                break;
            }
            case Mode::SourceColumns: {
                const auto c = readSourceCols(doc_->project, *o);
                headers = {"#", "Colonne de " + rowsSourceOf(*o), "Montr\xC3\xA9" "e"};
                icon = ui::Icon::AnimationTable;
                for (const auto& r : c.rows()) {
                    const auto* ch = c.chosen.empty() ? nullptr : c.find(r);
                    std::string shown = c.chosen.empty() || ch ? std::string("oui") : std::string("cach\xC3\xA9" "e");
                    if (ch && !ch->title.empty()) shown += "  \xC2\xB7  " + ch->title;
                    if (ch && !ch->format.empty()) shown += "  \xC2\xB7  " + ch->format;
                    rows.push_back({r, std::move(shown)});
                }
                break;
            }
            case Mode::Checks: {
                const auto c = readChecks(doc_->project, *o);
                headers = {"#", o->kind == hmi::Kind::UserManager ? "Bouton" : o->kind == hmi::Kind::AlarmSummary ? "Zone" : "Langue", "Montr\xC3\xA9" "e"};
                icon = ui::Icon::Info;
                const bool all = c.emptyIsAll && c.chosen.empty();
                const auto list = c.rows();
                for (std::size_t k = 0; k < list.size(); ++k) {
                    const bool typed = k >= c.offered.size();
                    std::string shown = all || c.has(list[k]) ? std::string("oui") : std::string{};
                    if (typed) shown += shown.empty() ? "tap\xC3\xA9" "e" : "  \xC2\xB7  tap\xC3\xA9" "e";
                    else if (k < c.details.size() && !c.details[k].empty()) shown += (shown.empty() ? "" : "  \xC2\xB7  ") + c.details[k];
                    rows.push_back({list[k], shown});
                }
                break;
            }
            case Mode::Members: {
                headers = {"#", "Objet", "Type"};
                icon = ui::Icon::Folder;
                if (const auto* v = doc_->project.view(view_))
                    for (const auto& c : v->objects)
                        if (c.parent == o->id) rows.push_back({c.name, std::string(hmi::kindLabel(c.kind))});
                break;
            }
            case Mode::None: break;
        }
    }
    const std::size_t n = rows.size();
    // Les titres des colonnes suivent ce qu'on montre (Ligne, Plume, Condition...).
    table_->setColumns({{headers[0], 40.f, 32.f, false, false, true, ui::Align::End}, {headers[1], 300.f}, {headers[2], 240.f}});
    model_ = std::make_shared<Rows>(std::move(headers), std::move(rows), icon);
    const int keep = selected_;
    table_->setModel(model_);
    if (keep >= 0 && keep < static_cast<int>(n)) {
        hmiSelectModelRow(*table_, static_cast<ui::RowIndex>(keep));
        selected_ = keep;
    } else {
        selected_ = -1;
    }
    rebuildGrid();
    if (n != lastCount_) {
        lastCount_ = n;
        countChanged->emit(n);
    }
    invalidate();
}

bool HmiContentPanel::change(const std::string& label, const std::function<bool(hmi::Object&)>& fn) {
    const Id object = object_;
    bool done = false;
    auto cmd = hmi::changeView(doc_, view_, label, [&](hmi::Project&, hmi::View& v) {
        if (auto* o = v.object(object)) done = fn(*o);
    });
    if (!cmd || !done) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiContentPanel::changeInView(const std::string& label, const std::function<bool(hmi::View&, hmi::Object&)>& fn) {
    const Id object = object_;
    bool done = false;
    auto cmd = hmi::changeView(doc_, view_, label, [&](hmi::Project&, hmi::View& v) {
        if (auto* o = v.object(object)) done = fn(v, *o);
    });
    if (!cmd || !done) return false;
    apply_(std::move(cmd));
    refresh();
    return true;
}

bool HmiContentPanel::addItem() {
    switch (mode()) {
        case Mode::States:
            return change("Ajouter un \xC3\xA9tat", [](hmi::Object& o) {
                auto t = readStates(o);
                hmi::StateEntry st;
                st.match = t.conditions ? std::string("0") : nextStateValue(t);
                st.text = "\xC3\x89tat " + std::to_string(t.states.size() + 1);
                st.color = hmiTrendPalette()[t.states.size() % hmiTrendPalette().size()];
                // Avant le « sinon » (il reste le dernier) ; selon des conditions, une condition de plus.
                auto at = std::find_if(t.states.begin(), t.states.end(), [](const hmi::StateEntry& e) { return e.match == "*"; });
                if (t.conditions) t.conds.push_back("FALSE");
                t.states.insert(at, std::move(st));
                writeStates(o, std::move(t));
                return true;
            });
        case Mode::Table:
            return change("Ajouter une ligne", [](hmi::Object& o) {
                auto t = readTable(o);
                t.rows.emplace_back(t.columns());
                writeTable(o, t);
                return true;
            });
        case Mode::Trend:
            return change("Ajouter : " + penWord(current()), [](hmi::Object& o) {
                auto p = readPens(o);
                p.exprs.emplace_back("0");
                p.names.push_back(penWord(&o) + " " + std::to_string(p.exprs.size()));
                const auto& palette = hmiTrendPalette();
                if (p.colors.size() < p.exprs.size()) p.colors.push_back(palette[(p.exprs.size() - 1) % palette.size()]);
                writePens(o, std::move(p));
                return true;
            });
        case Mode::AnimatedImage:
            return change("Ajouter un \xC3\xA9tat", [](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                hmi::ImageState st;
                st.condition = "TRUE";
                states.push_back(st);
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
        // Lot 12
        case Mode::Zones:
            return change("Ajouter une zone", [](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                hmi::MapZone z;
                z.name = "Zone " + std::to_string(zones.size() + 1);
                const double x = 8 + static_cast<double>(zones.size() % 4) * 22, y = 10 + static_cast<double>(zones.size() / 4 % 3) * 28;
                z.points = {{x, y}, {x + 18, y}, {x + 18, y + 22}, {x, y + 22}};
                zones.push_back(std::move(z));
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
        case Mode::Tabs:
            return change("Ajouter une page", [](hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                labels.push_back("Page " + std::to_string(labels.size() + 1));
                o.set("tabs", hmi::joinSemicolons(labels));
                return true;
            });
        // 1.12.7 (lot D) : un choix, une zone, une plage de plus.
        case Mode::Choices:
            return change("Ajouter un choix", [](hmi::Object& o) {
                auto c = readChoices(o);
                c.labels.push_back("Choix " + std::to_string(c.labels.size() + 1));
                c.values.emplace_back();
                writeChoices(o, std::move(c));
                return true;
            });
        case Mode::Bands:
            return change("Ajouter une zone", [](hmi::Object& o) {
                auto bands = readBands(o);
                BandRow b;
                b.from = bands.empty() ? hmi::formatNumber(o.number("min", 0)) : bands.back().to;
                b.to = hmi::formatNumber(o.number("max", 100));
                b.color = hmiTrendPalette()[bands.size() % hmiTrendPalette().size()];
                bands.push_back(std::move(b));
                writeBands(o, bands);
                return true;
            });
        case Mode::Schedule:
            return change("Ajouter une plage", [](hmi::Object& o) {
                auto slots = readSlots(o);
                slots.push_back({"Lu-Ve", "08:00", "12:00"});
                writeSlots(o, slots);
                return true;
            });
        case Mode::NavItems:
            return change("Ajouter une vue \xC3\xA0 la barre", [this](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                std::string next;
                for (const auto& v : doc_->project.views)
                    if (v.role == "vue" && std::find(n.views.begin(), n.views.end(), v.name) == n.views.end()) { next = v.name; break; }
                if (next.empty() && !doc_->project.views.empty()) next = doc_->project.views.front().name;
                if (next.empty()) return false;
                n.views.push_back(next);
                n.labels.emplace_back();
                writeNav(o, n);
                return true;
            });
        default: return false;
    }
}

bool HmiContentPanel::removeItem(int index) {
    if (index < 0 || index >= static_cast<int>(count())) return false;
    const auto i = static_cast<std::size_t>(index);
    bool ok = false;
    switch (mode()) {
        case Mode::States:
            ok = change("Retirer un \xC3\xA9tat", [i](hmi::Object& o) {
                auto t = readStates(o);
                if (i >= t.states.size()) return false;
                if (const int k = conditionIndexOf(t, i); t.conditions && k >= 0) t.conds.erase(t.conds.begin() + k);
                t.states.erase(t.states.begin() + static_cast<std::ptrdiff_t>(i));
                writeStates(o, std::move(t));
                return true;
            });
            break;
        case Mode::Table:
            ok = change("Retirer une ligne", [i](hmi::Object& o) {
                auto t = readTable(o);
                t.rows.erase(t.rows.begin() + static_cast<std::ptrdiff_t>(i));
                writeTable(o, t);
                return true;
            });
            break;
        case Mode::Trend:
            ok = change("Retirer une plume", [i](hmi::Object& o) {
                auto p = readPens(o);
                p.exprs.erase(p.exprs.begin() + static_cast<std::ptrdiff_t>(i));
                p.names.erase(p.names.begin() + static_cast<std::ptrdiff_t>(i));
                if (i < p.colors.size()) p.colors.erase(p.colors.begin() + static_cast<std::ptrdiff_t>(i));
                eraseAt(p.units, i);      // 1.12.5 (D11)
                eraseAt(p.formats, i);
                writePens(o, std::move(p));
                return true;
            });
            break;
        case Mode::AnimatedImage:
            ok = change("Retirer un \xC3\xA9tat", [i](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                states.erase(states.begin() + static_cast<std::ptrdiff_t>(i));
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
            break;
        // Lot 12
        case Mode::Zones:
            ok = change("Retirer une zone", [i](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                zones.erase(zones.begin() + static_cast<std::ptrdiff_t>(i));
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
            break;
        case Mode::Tabs:
            // Les objets de la page retiree passent sur la page d'avant ; les suivants
            // gardent leur page (renumerotee).
            ok = changeInView("Retirer une page", [i](hmi::View& v, hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                if (labels.size() <= 1) return false;
                const int gone = static_cast<int>(i) + 1;
                labels.erase(labels.begin() + static_cast<std::ptrdiff_t>(i));
                o.set("tabs", hmi::joinSemicolons(labels));
                for (auto& c : v.objects) {
                    if (c.parent != o.id) continue;
                    const int pg = hmi::tabPageOf(c);
                    if (pg == gone) c.setNumber("tabPage", std::max(1, gone - 1));
                    else if (pg > gone) c.setNumber("tabPage", pg - 1);
                }
                o.setNumber("page", std::clamp(static_cast<int>(o.number("page", 1)), 1, static_cast<int>(labels.size())));
                return true;
            });
            break;
        // 1.12.7 (lot D) : le libelle et sa valeur partent ensemble.
        case Mode::Choices:
            ok = change("Retirer un choix", [i](hmi::Object& o) {
                auto c = readChoices(o);
                if (i >= c.labels.size()) return false;
                eraseAt(c.labels, i);
                eraseAt(c.values, i);
                writeChoices(o, std::move(c));
                return true;
            });
            break;
        case Mode::Bands:
            ok = change("Retirer une zone", [i](hmi::Object& o) {
                auto bands = readBands(o);
                if (i >= bands.size()) return false;
                bands.erase(bands.begin() + static_cast<std::ptrdiff_t>(i));
                writeBands(o, bands);
                return true;
            });
            break;
        case Mode::Schedule:
            ok = change("Retirer une plage", [i](hmi::Object& o) {
                auto slots = readSlots(o);
                if (i >= slots.size()) return false;
                slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(i));
                writeSlots(o, slots);
                return true;
            });
            break;
        case Mode::NavItems:
            ok = change("Retirer une vue de la barre", [this, i](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                if (i >= n.views.size()) return false;
                n.views.erase(n.views.begin() + static_cast<std::ptrdiff_t>(i));
                n.labels.erase(n.labels.begin() + static_cast<std::ptrdiff_t>(i));
                writeNav(o, n);
                return true;
            });
            break;
        default: break;
    }
    if (ok) {
        selected_ = -1;
        if (count() > 0) selectIndex(std::min(index, static_cast<int>(count()) - 1));
        else rebuildGrid();
    }
    return ok;
}

bool HmiContentPanel::moveItem(int index, int delta) {
    const int to = index + delta;
    if (index < 0 || to < 0 || index >= static_cast<int>(count()) || to >= static_cast<int>(count())) return false;
    const auto a = static_cast<std::size_t>(index), b = static_cast<std::size_t>(to);
    switch (mode()) {
        case Mode::States:
            return change("Ordonner les \xC3\xA9tats", [a, b](hmi::Object& o) {
                auto t = readStates(o);
                if (a >= t.states.size() || b >= t.states.size()) return false;
                const int ka = conditionIndexOf(t, a), kb = conditionIndexOf(t, b);
                if (t.conditions && ka >= 0 && kb >= 0) std::swap(t.conds[static_cast<std::size_t>(ka)], t.conds[static_cast<std::size_t>(kb)]);
                std::swap(t.states[a], t.states[b]);
                writeStates(o, std::move(t));
                return true;
            });
        case Mode::Table:
            return change("Ordonner les lignes", [a, b](hmi::Object& o) {
                auto t = readTable(o);
                std::swap(t.rows[a], t.rows[b]);
                writeTable(o, t);
                return true;
            });
        case Mode::Trend:
            return change("Ordonner les plumes", [a, b](hmi::Object& o) {
                auto p = readPens(o);
                std::swap(p.exprs[a], p.exprs[b]);
                std::swap(p.names[a], p.names[b]);
                if (b < p.colors.size() && a < p.colors.size()) std::swap(p.colors[a], p.colors[b]);
                swapAt(p.units, a, b);    // 1.12.5 (D11)
                swapAt(p.formats, a, b);
                writePens(o, std::move(p));
                return true;
            });
        case Mode::AnimatedImage:
            return change("Ordonner les \xC3\xA9tats", [a, b](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                std::swap(states[a], states[b]);
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
        // Lot 12
        case Mode::Zones:
            return change("Ordonner les zones", [a, b](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                std::swap(zones[a], zones[b]);
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
        case Mode::Tabs:
            // Une page deplacee emmene ses objets.
            return changeInView("Ordonner les pages", [a, b](hmi::View& v, hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                std::swap(labels[a], labels[b]);
                o.set("tabs", hmi::joinSemicolons(labels));
                const int pa = static_cast<int>(a) + 1, pb = static_cast<int>(b) + 1;
                for (auto& c : v.objects) {
                    if (c.parent != o.id) continue;
                    const int pg = hmi::tabPageOf(c);
                    if (pg == pa) c.setNumber("tabPage", pb);
                    else if (pg == pb) c.setNumber("tabPage", pa);
                }
                const int shown = hmi::shownTabPage(o);
                if (shown == pa) o.setNumber("page", pb);
                else if (shown == pb) o.setNumber("page", pa);
                return true;
            });
        // 1.12.7 (lot D) : l'ordre des colonnes montrees d'une source.
        case Mode::SourceColumns:
            return change("Ordonner les colonnes", [this, a, b](hmi::Object& o) {
                auto c = readSourceCols(doc_->project, o);
                if (c.chosen.empty())
                    for (const auto& av : c.available) c.chosen.push_back({av, {}, {}});
                if (std::max(a, b) >= c.chosen.size()) return false;   // une colonne cachee ne se deplace pas
                std::swap(c.chosen[a], c.chosen[b]);
                writeSourceCols(o, std::move(c));
                return true;
            });
        // 1.12.7 (lot D)
        case Mode::Choices:
            return change("Ordonner les choix", [a, b](hmi::Object& o) {
                auto c = readChoices(o);
                if (std::max(a, b) >= c.labels.size()) return false;
                std::swap(c.labels[a], c.labels[b]);
                std::swap(c.values[a], c.values[b]);
                writeChoices(o, std::move(c));
                return true;
            });
        case Mode::Bands:
            return change("Ordonner les zones", [a, b](hmi::Object& o) {
                auto bands = readBands(o);
                if (std::max(a, b) >= bands.size()) return false;
                std::swap(bands[a], bands[b]);
                writeBands(o, bands);
                return true;
            });
        case Mode::Schedule:
            return change("Ordonner les plages", [a, b](hmi::Object& o) {
                auto slots = readSlots(o);
                if (std::max(a, b) >= slots.size()) return false;
                std::swap(slots[a], slots[b]);
                writeSlots(o, slots);
                return true;
            });
        case Mode::NavItems:
            return change("Ordonner les vues de la barre", [this, a, b](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                std::swap(n.views[a], n.views[b]);
                std::swap(n.labels[a], n.labels[b]);
                writeNav(o, n);
                return true;
            });
        default: return false;
    }
}

bool HmiContentPanel::addColumn() {
    if (mode() != Mode::Table) return false;
    return change("Ajouter une colonne", [](hmi::Object& o) {
        auto t = readTable(o);
        t.headers.push_back("Colonne " + std::to_string(t.headers.size() + 1));
        for (auto& r : t.rows) r.emplace_back();
        if (!t.widths.empty()) t.widths.emplace_back("1");
        writeTable(o, t);
        return true;
    });
}

bool HmiContentPanel::removeColumn(int column) {
    if (mode() != Mode::Table) return false;
    const auto* o = current();
    if (!o) return false;
    const auto n = static_cast<int>(readTable(*o).columns());
    if (n <= 1) return false;
    const auto k = static_cast<std::size_t>(column >= 0 && column < n ? column : n - 1);
    const bool ok = change("Retirer une colonne", [k](hmi::Object& obj) {
        auto t = readTable(obj);
        t.headers.erase(t.headers.begin() + static_cast<std::ptrdiff_t>(k));
        for (auto& r : t.rows) if (k < r.size()) r.erase(r.begin() + static_cast<std::ptrdiff_t>(k));
        if (k < t.widths.size()) t.widths.erase(t.widths.begin() + static_cast<std::ptrdiff_t>(k));
        writeTable(obj, t);
        return true;
    });
    if (ok) lastColumn_ = -1;
    return ok;
}

bool HmiContentPanel::setField(int index, const std::string& field, const std::string& value) {
    const auto i = static_cast<std::size_t>(std::max(0, index));
    const auto colOf = [&](std::string_view prefix) { return static_cast<std::size_t>(std::max(0, std::atoi(field.c_str() + prefix.size()))); };
    // 1.12.7 (lot E, § 2.3) : « depuis l'enumeration » - un etat, une image, un onglet par valeur.
    if (field == "fromEnum") {
        std::string variable;
        const auto* e = enumOf(doc_->project, value, &variable);
        if (!e || e->values.empty()) return false;
        const auto m = mode();
        const auto& palette = hmiTrendPalette();
        if (m == Mode::States) {
            return change("\xC3\x89tats depuis " + e->name, [e, variable, &palette](hmi::Object& o) {
                auto t = readStates(o);
                if (t.conditions) return false;          // selon des conditions : chaque etat a la sienne
                const bool chart = o.kind == hmi::Kind::StateChart;
                std::vector<hmi::StateEntry> states;
                for (std::size_t k = 0; k < e->values.size(); ++k) {
                    const auto& v = e->values[k];
                    hmi::StateEntry st;
                    // un multi-etats reconnait la valeur par son nom (Auto) ; un chronogramme trace des nombres
                    st.match = chart ? std::to_string(v.value) : v.name;
                    st.text = v.text.empty() ? v.name : v.text;
                    std::replace(st.text.begin(), st.text.end(), ';', ',');
                    std::replace(st.text.begin(), st.text.end(), '|', '/');
                    st.color = palette[k % palette.size()];
                    states.push_back(std::move(st));
                }
                for (const auto& old : t.states)
                    if (old.match == "*") states.push_back(old);   // le « sinon » reste le dernier
                t.states = std::move(states);
                writeStates(o, std::move(t));
                if (!variable.empty() && !chart)
                    if (const auto* val = o.find("value"); !val || (val->expr.empty() && trimmed(val->value).empty()) || (val->expr.empty() && val->value == "0"))
                        o.setExpr("value", variable);
                return true;
            });
        }
        if (m == Mode::AnimatedImage) {
            if (variable.empty()) return false;          // une condition compare une variable
            return change("\xC3\x89tats depuis " + e->name, [e, variable](hmi::Object& o) {
                std::vector<hmi::ImageState> states;
                for (const auto& v : e->values) {
                    hmi::ImageState st;
                    st.condition = variable + " = " + e->name + "#" + v.name;
                    states.push_back(std::move(st));
                }
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
        }
        if (m == Mode::Tabs) {
            return change("Onglets depuis " + e->name, [e, variable](hmi::Object& o) {
                std::vector<std::string> labels;
                for (const auto& v : e->values) {
                    std::string l = v.text.empty() ? v.name : v.text;
                    std::replace(l.begin(), l.end(), ';', ',');
                    labels.push_back(std::move(l));
                }
                o.set("tabs", hmi::joinSemicolons(labels));
                if (!variable.empty()) o.set("variable", variable);   // la page suit la valeur (sans decalage)
                return true;
            });
        }
        return false;
    }
    // 1.12.7 (lot D) : un chronogramme montre ses lignes ou ses etats (rien ne change dans l'objet).
    if (field == "show") {
        const auto* o = current();
        if (!o || o->kind != hmi::Kind::StateChart) return false;
        const bool states = value.find("tat") != std::string::npos;
        if (states == chartStates_) return false;
        chartStates_ = states;
        selected_ = -1;
        refresh();
        if (count() > 0) selectIndex(0);
        return true;
    }
    switch (mode()) {
        case Mode::States: {
            if (field == "mode") {
                if (const auto* o = current(); o && o->kind == hmi::Kind::StateChart) return false;   // 1.12.7 : pas de Valeur
                const bool wantConditions = value.find("condition") != std::string::npos;
                return change(wantConditions ? "\xC3\x89tats selon des conditions" : "\xC3\x89tats selon la valeur", [wantConditions](hmi::Object& o) {
                    auto t = readStates(o);
                    if (t.conditions == wantConditions) return false;
                    if (wantConditions) {
                        // Chaque etat devient « la valeur d'avant vaut sa valeur » : rien ne change en marche.
                        const auto* v = o.find("value");
                        const std::string source = v ? (!v->expr.empty() ? v->expr : v->value) : std::string("0");
                        t.conds.clear();
                        for (const auto& st : t.states)
                            if (st.match != "*")
                                t.conds.push_back(st.match.find("..") != std::string::npos
                                                      ? "(" + source + ") ENTRE " + st.match.substr(0, st.match.find(".."))
                                                            + " ET " + st.match.substr(st.match.find("..") + 2)
                                                      : "(" + source + ") = " + st.match);
                        t.conditions = true;
                    } else {
                        // Les etats gardent leurs nombres (1, 2...) ; la Valeur attend sa variable.
                        o.setExpr("value", "");
                        o.set("value", "0");
                        t.conditions = false;
                    }
                    writeStates(o, std::move(t));
                    return true;
                });
            }
            std::string why;
            if ((field == "match" || field == "text" || field == "color") && !hmi::stateFieldFits(value, &why)) return false;
            return change("R\xC3\xA9gler un \xC3\xA9tat", [i, field, value](hmi::Object& o) {
                auto t = readStates(o);
                if (i >= t.states.size()) return false;
                auto& st = t.states[i];
                std::string v = value;
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
                if (field == "match") {
                    if (v.empty() || t.conditions) return false;
                    st.match = v;
                } else if (field == "condition") {
                    const int k = conditionIndexOf(t, i);
                    if (!t.conditions || k < 0) return false;
                    t.conds[static_cast<std::size_t>(k)] = v.empty() ? std::string("FALSE") : v;
                } else if (field == "text") {
                    st.text = v;
                } else if (field == "color") {
                    st.color = v;
                } else if (field == "blink") {
                    st.blink = hmi::parseBool(v, false);
                } else if (field == "otherwise") {
                    // Le « sinon » : present (le dernier) ou absent.
                    const bool on = hmi::parseBool(v, false);
                    const auto at = std::find_if(t.states.begin(), t.states.end(), [](const hmi::StateEntry& e) { return e.match == "*"; });
                    if (on == (at != t.states.end())) return false;
                    if (on) {
                        hmi::StateEntry e;
                        e.match = "*";
                        e.text = "?";
                        e.color = "#4A5261";
                        t.states.push_back(std::move(e));
                    } else {
                        t.states.erase(at);
                    }
                } else {
                    return false;
                }
                writeStates(o, std::move(t));
                return true;
            });
        }
        case Mode::Table: {
            if (field.rfind("cell:", 0) == 0) {
                const auto k = colOf("cell:");
                return change("Modifier une case", [i, k, value](hmi::Object& o) {
                    auto t = readTable(o);
                    if (i >= t.rows.size() || k >= t.columns() || t.rows[i][k] == value) return false;
                    t.rows[i][k] = value;
                    writeTable(o, t);
                    return true;
                });
            }
            if (field.rfind("header:", 0) == 0 || field.rfind("width:", 0) == 0) {
                const bool header = field[0] == 'h';
                const auto k = colOf(header ? "header:" : "width:");
                if (!header) {
                    double w = 0;
                    if (!hmi::parseNumber(value, w) || w <= 0) return false;
                }
                return change(header ? "Nommer une colonne" : "Largeur de colonne", [k, header, value](hmi::Object& o) {
                    auto t = readTable(o);
                    if (k >= t.columns()) return false;
                    if (header) {
                        if (t.headers[k] == value) return false;
                        t.headers[k] = value;
                    } else {
                        if (t.widths.empty()) t.widths.assign(t.columns(), "1");
                        if (t.widths[k] == value) return false;
                        t.widths[k] = value;
                    }
                    writeTable(o, t);
                    return true;
                });
            }
            return false;
        }
        case Mode::Trend:
            return change("R\xC3\xA9gler une plume", [i, field, value](hmi::Object& o) {
                auto p = readPens(o);
                if (i >= p.exprs.size()) return false;
                if (field == "name") p.names[i] = value;
                else if (field == "expression") {
                    if (value.empty()) return false;
                    p.exprs[i] = value;
                } else if (field == "color") {
                    while (p.colors.size() <= i) p.colors.push_back(hmiTrendPalette()[p.colors.size() % hmiTrendPalette().size()]);
                    p.colors[i] = value;
                } else if ((field == "unit" || field == "format") && o.kind == hmi::Kind::VariableTable) {
                    // 1.12.7 (lot D) : l'unite et le format de la ligne, a leur rang (les listes paralleles
                    // units et formats, que le moteur lit) ; un ';' nu couperait la liste.
                    if (!fitsInList(value)) return false;
                    auto& list = field == "unit" ? p.units : p.formats;
                    if (list.size() < p.exprs.size()) list.resize(p.exprs.size());
                    if (list[i] == value) return false;
                    list[i] = value;
                    while (!list.empty() && list.back().empty()) list.pop_back();
                } else return false;
                writePens(o, std::move(p));
                return true;
            });
        case Mode::AnimatedImage: {
            if (field == "default") return change("Image par d\xC3\xA9" "faut", [value](hmi::Object& o) { o.set("image", value); return true; });
            if (field == "objectPeriod") {
                double ms = 0;
                if (!hmi::parseNumber(value, ms) || ms < 20) return false;
                return change("P\xC3\xA9riode de d\xC3\xA9" "filement", [ms](hmi::Object& o) { o.setNumber("period", ms); return true; });
            }
            return change("R\xC3\xA9gler un \xC3\xA9tat", [i, field, value](hmi::Object& o) {
                auto states = hmi::parseImageStates(o.text("states"));
                if (i >= states.size()) return false;
                auto& st = states[i];
                if (field == "condition") st.condition = value.empty() ? std::string("TRUE") : value;
                else if (field == "images") {
                    st.images.clear();
                    for (const auto& part : hmi::splitSemicolons([&] {
                             std::string v = value;
                             std::replace(v.begin(), v.end(), ',', ';');
                             return v;
                         }()))
                        if (!part.empty()) st.images.push_back(part);
                } else if (field == "addImage") {
                    if (value.empty()) return false;
                    st.images.push_back(value);
                } else if (field == "period") {
                    double ms = 0;
                    if (!hmi::parseNumber(value, ms) || ms < 0) return false;
                    st.periodMs = static_cast<int>(ms);
                } else return false;
                o.set("states", hmi::formatImageStates(states));
                return true;
            });
        }
        case Mode::RecipeManager: {
            if (field == "recipe") return change("Recette du gestionnaire", [value](hmi::Object& o) { o.set("recipe", value); return true; });
            if (field.rfind("button:", 0) == 0) {
                const std::string name = field.substr(7);
                const bool on = hmi::parseBool(value, true);
                return change("Boutons du gestionnaire", [name, on](hmi::Object& o) {
                    const auto* p = o.find("buttons");
                    auto shown = p ? hmi::splitSemicolons(p->value)
                                   : std::vector<std::string>(std::begin(hmi::kRecipeButtons), std::end(hmi::kRecipeButtons));
                    const bool has = std::find(shown.begin(), shown.end(), name) != shown.end();
                    if (has == on) return false;
                    // L'ordre reste celui de la barre : Ajouter, Modifier, Supprimer, Appliquer, Lire.
                    std::vector<std::string> next;
                    for (const auto b : hmi::kRecipeButtons) {
                        const std::string label(b);
                        const bool keep = label == name ? on : std::find(shown.begin(), shown.end(), label) != shown.end();
                        if (keep) next.push_back(label);
                    }
                    o.set("buttons", hmi::joinSemicolons(next));
                    return true;
                });
            }
            return false;
        }
        // Lot 12
        case Mode::Zones:
            return change("R\xC3\xA9gler une zone", [i, field, value](hmi::Object& o) {
                auto zones = hmi::parseMapZones(o.text("mapZones"));
                if (i >= zones.size()) return false;
                auto& z = zones[i];
                if (field == "name") {
                    if (value.empty() || value.find('|') != std::string::npos) return false;
                    z.name = value;
                } else if (field == "points") {
                    auto pts = hmi::parseZonePoints(value);
                    if (pts.size() < 3) return false;
                    z.points = std::move(pts);
                } else if (field == "group") z.group = value;
                else if (field == "view") z.view = value;
                else if (field == "color") z.color = value;
                else return false;
                o.set("mapZones", hmi::formatMapZones(zones));
                return true;
            });
        case Mode::Tabs:
            return change("Nommer une page", [i, value](hmi::Object& o) {
                auto labels = hmi::tabLabels(o);
                if (i >= labels.size() || value.empty() || value.find(';') != std::string::npos || labels[i] == value) return false;
                labels[i] = value;
                o.set("tabs", hmi::joinSemicolons(labels));
                return true;
            });
        case Mode::NavItems:
            return change("R\xC3\xA9gler la barre de navigation", [this, i, field, value](hmi::Object& o) {
                auto n = readNav(doc_->project, o);
                if (i >= n.views.size() || value.find(';') != std::string::npos) return false;
                if (field == "view") {
                    // 1.12.7 (lot D) : une vue du projet, ou un repere ($Vue_Ligne$) dont le nom sera
                    // remplace par Dupliquer (Generer le verifie sans ses $).
                    if (value.empty() || (!doc_->project.viewByName(value) && hmi::markers::find(value, hmi::markers::Mode::Text).empty()))
                        return false;
                    n.views[i] = value;
                } else if (field == "label") {
                    n.labels[i] = value == hmi::viewCaption(n.views[i]) ? std::string{} : value;
                } else return false;
                writeNav(o, n);
                return true;
            });
        // 1.12.7 (lot D) : un choix - son libelle (affiche) et sa valeur (ecrite ; vide : le rang).
        case Mode::Choices:
            return change("R\xC3\xA9gler un choix", [i, field, value](hmi::Object& o) {
                auto c = readChoices(o);
                if (i >= c.labels.size()) return false;
                const std::string v = trimmed(value);
                if (!fitsInList(v)) return false;
                if (field == "label") {
                    if (v.empty() || c.labels[i] == v) return false;   // un libelle vide ne compte pas (le rang glisserait)
                    c.labels[i] = v;
                } else if (field == "value") {
                    if (c.values[i] == v) return false;
                    c.values[i] = v;
                } else return false;
                writeChoices(o, std::move(c));
                return true;
            });
        case Mode::Bands:
            return change("R\xC3\xA9gler une zone", [i, field, value](hmi::Object& o) {
                auto bands = readBands(o);
                if (i >= bands.size()) return false;
                const std::string v = trimmed(value);
                if (!fitsInList(v) || v.find('=') != std::string::npos || v.find("..") != std::string::npos) return false;
                auto& b = bands[i];
                std::string* target = field == "from" ? &b.from : field == "to" ? &b.to : field == "color" ? &b.color : nullptr;
                if (!target || *target == v || (v.empty() && field != "color")) return false;
                *target = v;
                writeBands(o, bands);
                return true;
            });
        case Mode::Schedule:
            return change("R\xC3\xA9gler une plage", [i, field, value](hmi::Object& o) {
                auto slots = readSlots(o);
                if (i >= slots.size()) return false;
                const std::string v = trimmed(value);
                if (v.empty() || !fitsInList(v) || v.find('=') != std::string::npos) return false;
                auto& sl = slots[i];
                if (field == "days") {
                    if (sl.days == v) return false;
                    // Les jours se relisent comme le programmateur les lit (Lu-Ve, Sa,Di, tous).
                    hmi::WeekSchedule probe;
                    if (!hmi::parseSchedule(v + "=08:00-09:00", 60, probe)) return false;
                    sl.days = v;
                } else if (field == "from" || field == "to") {
                    if (slotMinutes(v) < 0) return false;
                    std::string& t = field == "from" ? sl.from : sl.to;
                    if (t == v) return false;
                    t = v;
                } else return false;
                writeSlots(o, slots);
                return true;
            });
        case Mode::SourceColumns:
            return change("R\xC3\xA9gler une colonne", [this, i, field, value](hmi::Object& o) {
                auto c = readSourceCols(doc_->project, o);
                const auto rows = c.rows();
                if (i >= rows.size()) return false;
                const std::string member = rows[i];
                if (c.chosen.empty())
                    for (const auto& av : c.available) c.chosen.push_back({av, {}, {}});
                const auto at = std::find_if(c.chosen.begin(), c.chosen.end(), [&](const hmi::choices::SourceColumn& x) { return x.member == member; });
                const std::string v = trimmed(value);
                if (field == "shown") {
                    const bool on = hmi::parseBool(v, true);
                    if (on == (at != c.chosen.end())) return false;
                    if (on) c.chosen.push_back({member, {}, {}});
                    else {
                        if (c.chosen.size() <= 1) return false;   // vide voudrait dire « toutes »
                        c.chosen.erase(at);
                    }
                } else if (field == "title" || field == "format") {
                    if (!fitsInList(v) || v.find('|') != std::string::npos || (field == "title" && v.find('=') != std::string::npos)) return false;
                    auto it = at;
                    if (it == c.chosen.end()) {   // une colonne cachee qu'on regle se montre
                        c.chosen.push_back({member, {}, {}});
                        it = c.chosen.end() - 1;
                    }
                    std::string& target = field == "title" ? it->title : it->format;
                    if (target == v) return false;
                    target = v;
                } else return false;
                writeSourceCols(o, std::move(c));
                return true;
            });
        case Mode::Checks: {
            if (field.rfind("check:", 0) != 0) return false;
            const std::string item = field.substr(6);
            const bool on = hmi::parseBool(value, true);
            return change("Cocher", [this, item, on](hmi::Object& o) {
                const auto c = readChecks(doc_->project, o);
                // Une liste vide qui vaut « toutes » : decocher l'une garde les autres.
                if (c.emptyIsAll && c.chosen.empty()) {
                    if (on) return false;
                    CheckRows all = c;
                    all.chosen = c.offered;
                    return toggleCheck(o, all, item, false);
                }
                return toggleCheck(o, c, item, on);
            });
        }
        case Mode::Members:          // 1.10.4 (K3) : en lecture
        case Mode::None: break;
    }
    return false;
}

void HmiContentPanel::rebuildGrid() {
    const auto* o = current();
    const auto readOnly = [](std::string name, std::string value, std::string help = {}) {
        PG::Property p;
        p.name = std::move(name);
        p.value = std::move(value);
        p.type = PG::ValueType::ReadOnly;
        p.description = std::move(help);
        return p;
    };
    const auto editable = [this](std::string name, std::string value, PG::ValueType t, int index, std::string field,
                                 std::string help = {}, std::vector<std::string> choices = {}) {
        PG::Property p;
        p.name = std::move(name);
        p.value = std::move(value);
        p.type = t;
        p.description = std::move(help);
        p.enumValues = std::move(choices);
        p.commit = [this, index, field](std::string_view v) { return setField(index, field, std::string(v)); };
        return p;
    };
    // 1.12.7 (lot D) : ce que montre la liste d'un chronogramme - ses lignes ou ses etats.
    const auto stateChartToggle = [&] {
        PG::Category show;
        show.name = "Montrer";
        show.properties.push_back(editable("Montrer", chartStates_ ? "les \xC3\xA9tats" : "les lignes", PG::ValueType::Enum, -1, "show",
                                           "Les lignes (une expression chacune) ou les \xC3\xA9tats (valeur, texte, couleur) que "
                                           "toutes les lignes partagent.",
                                           {"les lignes", "les \xC3\xA9tats"}));
        return show;
    };
    // 1.12.7 (lot E) : « Depuis une enumeration » - un etat (une image, un onglet) par valeur.
    const auto fromEnumCell = [&](bool variablesOnly) {
        auto choices = enumSources(doc_->project, variablesOnly);
        return editable("Depuis une \xC3\xA9num\xC3\xA9ration", "", PG::ValueType::Enum, -1, "fromEnum",
                        variablesOnly ? std::string("Une variable d'un type \xC3\xA9num\xC3\xA9r\xC3\xA9 : un \xC3\xA9tat par valeur, sa condition "
                                                    "\xC3\xA9" "crite (Etat = T_ETAT#Marche) ; les images se choisissent ensuite.")
                                      : std::string("Une \xC3\xA9num\xC3\xA9ration du projet (ou une variable de ce type, qui devient la "
                                                    "Valeur) : un \xC3\xA9tat par valeur, son texte, une couleur \xC3\xA0 changer. Remplace "
                                                    "les \xC3\xA9tats (Ctrl+Z les rend)."),
                        std::move(choices));
    };
    std::vector<PG::Category> cats;
    const Mode m = mode();
    if (!o || m == Mode::None) {
        PG::Category info;
        info.name = "Contenu";
        info.properties.push_back(readOnly(o ? o->name : std::string("Rien de choisi"),
                                           o ? std::string(hmi::kindLabel(o->kind)) + " : rien \xC3\xA0 r\xC3\xA9gler ici" : std::string{},
                                           "L'onglet Contenu r\xC3\xA8gle les lignes et colonnes d'un Tableau, les plumes d'une "
                                           "Courbe, les \xC3\xA9tats d'une Image anim\xC3\xA9" "e et la recette d'un Gestionnaire de recettes ; "
                                           "il montre les objets d'un Groupe ou d'un conteneur. Un objet sans contenu n'a pas l'onglet."));
        cats.push_back(std::move(info));
        grid_->setCategories(std::move(cats));
        return;
    }
    const int index = selectedIndex();
    const auto i = static_cast<std::size_t>(std::max(0, index));
    switch (m) {
        case Mode::Table: {
            const auto t = readTable(*o);
            if (index >= 0) {
                PG::Category row;
                row.name = "Ligne " + std::to_string(index + 1);
                for (std::size_t k = 0; k < t.columns(); ++k) {
                    auto cell = editable(std::to_string(k + 1) + " - " + t.headers[k], t.rows[i][k], PG::ValueType::Text,
                                         index, "cell:" + std::to_string(k),
                                         "Un texte ; un texte \xC3\xA0 trous : Niveau {Cuve.niveau:0.0} % ; "
                                         "ou =expression : =Debit * 3.6. \\n : une nouvelle ligne n'existe pas "
                                         "dans une case.");
                    // 1.10 (chantier K) : la case de partout - "=expression" montre son "="
                    // et sa pastille ; la retirer (X, clic droit, "=" seul) vide la case.
                    ui::exprfield::mark(cell, ui::exprfield::Expect::Template);
                    if (!cell.value.empty() && cell.value.front() == '=') cell.expression = cell.value.substr(1);
                    cell.commit = [inner = std::move(cell.commit)](std::string_view v) {
                        return inner(v == "=" ? std::string_view{} : v);
                    };
                    row.properties.push_back(std::move(cell));
                }
                cats.push_back(std::move(row));
            } else {
                PG::Category info;
                info.name = "Lignes";
                info.properties.push_back(readOnly("Lignes", std::to_string(t.rows.size()),
                                                   "Choisis une ligne pour r\xC3\xA9gler ses cases ; \xC2\xAB Ajouter \xC2\xBB en cr\xC3\xA9" "e une."));
                cats.push_back(std::move(info));
            }
            PG::Category columns;
            columns.name = "Colonnes";
            for (std::size_t k = 0; k < t.columns(); ++k) {
                columns.properties.push_back(editable("En-t\xC3\xAAte " + std::to_string(k + 1), t.headers[k], PG::ValueType::Text, -1,
                                                      "header:" + std::to_string(k)));
                columns.properties.push_back(editable("Largeur " + std::to_string(k + 1), k < t.widths.size() ? t.widths[k] : std::string("1"),
                                                      PG::ValueType::Real, -1, "width:" + std::to_string(k),
                                                      "Relative : 2 = deux fois plus large qu'une colonne \xC3\xA0 1."));
            }
            cats.push_back(std::move(columns));
            break;
        }
        case Mode::Trend: {
            const auto p = readPens(*o);
            if (o->kind == hmi::Kind::StateChart) cats.push_back(stateChartToggle());   // 1.12.7 (lot D)
            PG::Category pen;
            if (index >= 0) {
                pen.name = penWord(o) + " " + std::to_string(index + 1);
                pen.properties.push_back(editable("Nom", p.names[i], PG::ValueType::Text, index, "name",
                                                  "Ce que dit la l\xC3\xA9gende ; vide : l'expression."));
                auto expr = editable("Expression", p.exprs[i], PG::ValueType::Text, index, "expression",
                                     core::hasApi() ? "Une variable IHM ou de l'automate, ou une expression : Armoires[0].ana.PT1.mes * 10"
                                                    : "Une variable IHM, ou une expression : Armoires[0].ana.PT1.mes * 10");   // 1.12.2
                // 1.10 (chantier K) : la pastille, l'invite et l'aide des nombres ; "=" tape
                // retire. Une plume ne se vide pas (\xC2\xAB Retirer \xC2\xBB l'enleve) : pas de X.
                ui::exprfield::markWhole(expr, ui::exprfield::Expect::Number, false);
                pen.properties.push_back(std::move(expr));
                // 1.12.2 : le chronogramme colore ses lignes par leurs etats (sa liste d'etats) : la
                // case Couleur d'une de ses lignes ne faisait rien.
                if (o->kind != hmi::Kind::StateChart && o->kind != hmi::Kind::VariableTable)
                    pen.properties.push_back(editable("Couleur", i < p.colors.size() ? p.colors[i] : std::string{}, PG::ValueType::Color, index,
                                                      "color"));
                // 1.12.7 (lot D) : l'unite et le format de la ligne d'un tableau de variables (avant :
                // les listes units et formats de l'inspecteur, a aligner a la main).
                if (o->kind == hmi::Kind::VariableTable) {
                    pen.properties.push_back(editable("Unit\xC3\xA9", i < p.units.size() ? p.units[i] : std::string{}, PG::ValueType::Text,
                                                      index, "unit",
                                                      "Vide : celle de la variable (Configuration > Unit\xC3\xA9s et formats), sinon aucune."));
                    pen.properties.push_back(editable("Format", i < p.formats.size() ? p.formats[i] : std::string{}, PG::ValueType::Text,
                                                      index, "format",
                                                      "0.0, 0.00, 0 ; vide : celui de la variable, sinon le Format de l'objet."));
                }
            } else {
                pen.name = penWord(o) + "s";
                pen.properties.push_back(readOnly(penWord(o) + "s", std::to_string(p.exprs.size()),
                                                  "Plusieurs plumes dans la m\xC3\xAAme courbe : \xC2\xAB Ajouter \xC2\xBB, puis leur expression."));
            }
            cats.push_back(std::move(pen));
            break;
        }
        case Mode::AnimatedImage: {
            const auto states = hmi::parseImageStates(o->text("states"));
            std::vector<std::string> images{""};
            for (const auto& r : doc_->project.assets.resources)
                if (r.kind() == hmi::MediaKind::Image) images.push_back(r.name);
            if (index >= 0 && i < states.size()) {
                const auto& st = states[i];
                PG::Category state;
                state.name = "\xC3\x89tat " + std::to_string(index + 1);
                auto cond = editable("Condition", st.condition, PG::ValueType::Text, index, "condition",
                                     "Une expression vraie ou fausse : Pompe_Marche AND NOT Defaut. Le premier "
                                     "\xC3\xA9tat vrai l'emporte.");
                // 1.10 (chantier K) : la case de partout ; vide (X) : TRUE, toujours vraie.
                ui::exprfield::markWhole(cond, ui::exprfield::Expect::Bool, st.condition != "TRUE");
                state.properties.push_back(std::move(cond));
                state.properties.push_back(editable("Images (a, b)", imagesText(st.images), PG::ValueType::Text, index, "images",
                                                    "Plusieurs images d\xC3\xA9" "filent \xC3\xA0 la p\xC3\xA9riode de l'\xC3\xA9tat."));
                state.properties.push_back(editable("Ajouter une image", "", PG::ValueType::Enum, index, "addImage",
                                                    "Une image du gestionnaire de ressources, ajout\xC3\xA9" "e \xC3\xA0 la fin.", images));
                state.properties.push_back(editable("P\xC3\xA9riode (ms)", std::to_string(st.periodMs), PG::ValueType::Integer, index,
                                                    "period", "0 : celle de l'objet."));
                cats.push_back(std::move(state));
            }
            PG::Category def;
            def.name = "Sans \xC3\xA9tat vrai";
            std::string current = o->text("image");
            if (std::find(images.begin(), images.end(), current) == images.end()) images.push_back(current);
            def.properties.push_back(editable("Image par d\xC3\xA9" "faut", current, PG::ValueType::Enum, -1, "default",
                                              "Montr\xC3\xA9" "e quand aucune condition n'est vraie.", images));
            def.properties.push_back(editable("P\xC3\xA9riode de l'objet (ms)", hmi::formatNumber(o->number("period", 500)),
                                              PG::ValueType::Integer, -1, "objectPeriod"));
            def.properties.push_back(readOnly("\xC3\x89tats", std::to_string(states.size())));
            def.properties.push_back(fromEnumCell(true));   // 1.12.7 (lot E)
            cats.push_back(std::move(def));
            break;
        }
        case Mode::States: {
            const auto t = readStates(*o);
            if (o->kind == hmi::Kind::StateChart) {
                // 1.12.7 (lot D) : les etats d'un chronogramme (stateList), comme ceux d'un multi-etats ;
                // chaque ligne du chronogramme les lit selon sa valeur.
                cats.push_back(stateChartToggle());
                PG::Category how;
                how.name = "\xC3\x89tats";
                const bool otherwise = std::any_of(t.states.begin(), t.states.end(), [](const hmi::StateEntry& e) { return e.match == "*"; });
                how.properties.push_back(editable("Un \xC3\xA9tat \xC2\xAB sinon \xC2\xBB", otherwise ? "TRUE" : "FALSE", PG::ValueType::Boolean, -1,
                                                  "otherwise", "Montr\xC3\xA9 quand aucun autre ne vaut (la ligne * ou \xC2\xAB sinon \xC2\xBB)."));
                how.properties.push_back(fromEnumCell(false));   // 1.12.7 (lot E)
                cats.push_back(std::move(how));
                if (index >= 0 && i < t.states.size()) {
                    const auto& st = t.states[i];
                    PG::Category state;
                    state.name = "\xC3\x89tat " + std::to_string(index + 1);
                    if (st.match == "*") state.properties.push_back(readOnly("Quand", "sinon", "Quand aucun autre \xC3\xA9tat ne vaut."));
                    else
                        state.properties.push_back(editable("Quand la valeur vaut", st.match, PG::ValueType::Text, index, "match",
                                                            "Un nombre (2), un intervalle (10..20), TRUE / FALSE, un texte ('Auto'), "
                                                            "le nom d'une valeur d'\xC3\xA9num\xC3\xA9ration (Auto), ou * : tout le reste."));
                    state.properties.push_back(editable("Texte", st.text, PG::ValueType::Text, index, "text",
                                                        "Ce que la ligne \xC3\xA9" "crit ; sans \xC2\xAB ; \xC2\xBB ni \xC2\xAB | \xC2\xBB."));
                    state.properties.push_back(editable("Couleur", st.color, PG::ValueType::Color, index, "color"));
                    cats.push_back(std::move(state));
                }
                break;
            }
            PG::Category how;
            how.name = "Choisir l'\xC3\xA9tat";
            how.properties.push_back(editable("Choisir l'\xC3\xA9tat", t.conditions ? "selon des conditions" : "selon la valeur",
                                              PG::ValueType::Enum, -1, "mode",
                                              "Selon la valeur : la case Valeur (Propri\xC3\xA9t\xC3\xA9s) donne un nombre, un texte, TRUE ; "
                                              "l'\xC3\xA9tat qui vaut l'emporte (0, 1, 10..20, 'Auto' ; * : tout le reste). Selon des "
                                              "conditions : une condition par \xC3\xA9tat (Defaut, Marche AND NOT Defaut) - la premi\xC3\xA8re "
                                              "vraie gagne ; la Valeur est \xC3\xA9" "crite pour toi.",
                                              {"selon la valeur", "selon des conditions"}));
            const auto* v = o->find("value");
            const std::string shown = v ? (!v->expr.empty() ? "=" + v->expr : v->value) : std::string{};
            how.properties.push_back(readOnly(t.conditions ? "Valeur \xC3\xA9" "crite" : "Valeur", shown,
                                              t.conditions ? "La cha\xC3\xAEne des conditions, \xC3\xA9" "crite et relue par l'onglet : la modifier "
                                                             "\xC3\xA0 la main la rend \xC3\xA0 la valeur."
                                                           : "La variable ou l'expression qui choisit l'\xC3\xA9tat : elle se r\xC3\xA8gle dans "
                                                             "Propri\xC3\xA9t\xC3\xA9s (Valeur, =Etat_Pompe)."));
            const bool otherwise = std::any_of(t.states.begin(), t.states.end(), [](const hmi::StateEntry& e) { return e.match == "*"; });
            how.properties.push_back(editable("Un \xC3\xA9tat \xC2\xAB sinon \xC2\xBB", otherwise ? "TRUE" : "FALSE", PG::ValueType::Boolean, -1,
                                              "otherwise", "Montr\xC3\xA9 quand aucun autre ne vaut (la ligne * ou \xC2\xAB sinon \xC2\xBB)."));
            if (!t.conditions) how.properties.push_back(fromEnumCell(false));   // 1.12.7 (lot E)
            cats.push_back(std::move(how));
            if (index >= 0 && i < t.states.size()) {
                const auto& st = t.states[i];
                PG::Category state;
                state.name = "\xC3\x89tat " + std::to_string(index + 1);
                const int k = conditionIndexOf(t, i);
                if (st.match == "*") {
                    state.properties.push_back(readOnly("Quand", "sinon", "Quand aucun autre \xC3\xA9tat ne vaut."));
                } else if (t.conditions && k >= 0) {
                    auto cond = editable("Condition", t.conds[static_cast<std::size_t>(k)], PG::ValueType::Text, index, "condition",
                                         "Une expression vraie ou fausse : Defaut, Marche AND NOT Defaut, Niveau > 80. La "
                                         "premi\xC3\xA8re vraie gagne (\xE2\x86\x91\xE2\x86\x93 : l'ordre).");
                    ui::exprfield::markWhole(cond, ui::exprfield::Expect::Bool, false);
                    state.properties.push_back(std::move(cond));
                } else {
                    state.properties.push_back(editable("Quand la valeur vaut", st.match, PG::ValueType::Text, index, "match",
                                                        "Un nombre (2), un intervalle (10..20), TRUE / FALSE, un texte ('Auto'), "
                                                        "ou * : tout le reste."));
                }
                state.properties.push_back(editable("Texte", st.text, PG::ValueType::Text, index, "text",
                                                    "Ce que l'objet \xC3\xA9" "crit ; sans \xC2\xAB ; \xC2\xBB ni \xC2\xAB | \xC2\xBB."));
                state.properties.push_back(editable("Couleur", st.color, PG::ValueType::Color, index, "color", "Vide : la couleur de l'objet."));
                state.properties.push_back(editable("Clignote", st.blink ? "TRUE" : "FALSE", PG::ValueType::Boolean, index, "blink"));
                cats.push_back(std::move(state));
            }
            break;
        }
        case Mode::RecipeManager: {
            PG::Category rc;
            rc.name = "Recette";
            std::vector<std::string> recipes{""};
            for (const auto& r : doc_->project.recipes) recipes.push_back(r.name);
            const std::string chosen = o->text("recipe");
            if (std::find(recipes.begin(), recipes.end(), chosen) == recipes.end()) recipes.push_back(chosen);
            auto recipeCell = editable("Recette", chosen, PG::ValueType::Enum, -1, "recipe",
                                       "Les jeux de cette recette, un par ligne, une colonne par \xC3\xA9l\xC3\xA9ment.", recipes);
            recipeCell.typeIn = true;   // 1.12.7 (lot D) : un repere $Recette$ se tape aussi
            rc.properties.push_back(std::move(recipeCell));
            const auto* bp = o->find("buttons");
            const auto shown = bp ? hmi::splitSemicolons(bp->value)
                                  : std::vector<std::string>(std::begin(hmi::kRecipeButtons), std::end(hmi::kRecipeButtons));
            static const char* kHelp[] = {
                "Un nouveau jeu : son nom demand\xC3\xA9, les valeurs de l'installation propos\xC3\xA9" "es.",
                "Le jeu choisi : son nom et ses valeurs, dans un dialogue.",
                "Le jeu choisi, apr\xC3\xA8s confirmation.",
                "\xC3\x89" "crire le jeu choisi dans l'installation (tout ou rien, bornes v\xC3\xA9rifi\xC3\xA9" "es).",
                "Ranger dans le jeu choisi les valeurs lues dans l'installation."};
            std::size_t k = 0;
            for (const auto b : hmi::kRecipeButtons) {
                const std::string label(b);
                const bool on = std::find(shown.begin(), shown.end(), label) != shown.end();
                rc.properties.push_back(editable("Bouton " + label, on ? "TRUE" : "FALSE", PG::ValueType::Boolean, -1, "button:" + label,
                                                 std::string(kHelp[k++]) + " Sous s\xC3\xA9" "curit\xC3\xA9 : permission Recettes."));
            }
            rc.properties.push_back(readOnly("Jeux", std::to_string(count()),
                                             "Ils se modifient dans Configuration > Recettes, ou en marche avec les boutons de l'objet "
                                             "(le projet change, Ctrl+Z le reprend)."));
            cats.push_back(std::move(rc));
            break;
        }
        // Lot 12 : les zones d'un plan.
        case Mode::Zones: {
            const auto zones = hmi::parseMapZones(o->text("mapZones"));
            std::vector<std::string> groups{""}, views{""};
            for (const auto& a : doc_->project.alarms)
                if (!a.group.empty() && std::find(groups.begin(), groups.end(), a.group) == groups.end()) groups.push_back(a.group);
            // 1.12.7 (lot D) : les groupes des alarmes des objets (Vue.*, Vue.Objet, symbole:Nom), que le filtre comprend.
            for (const auto& gc : objalarms::groupChoices(doc_->project))
                if (!gc.value.empty() && std::find(groups.begin(), groups.end(), gc.value) == groups.end()) groups.push_back(gc.value);
            for (const auto& v : doc_->project.views)
                if (v.role == "vue" || v.role == "popup") views.push_back(v.name);
            if (index >= 0 && i < zones.size()) {
                const auto& z = zones[i];
                PG::Category zc;
                zc.name = "Zone " + std::to_string(index + 1);
                zc.properties.push_back(editable("Nom", z.name, PG::ValueType::Text, index, "name", "\xC3\x89" "crit au centre de la zone."));
                zc.properties.push_back(editable("Points (x,y en %)", hmi::formatZonePoints(z.points), PG::ValueType::Text, index, "points",
                                                 "Les sommets, en % de la largeur et de la hauteur du plan : 10,20 40,20 40,60 10,60. "
                                                 "Un rectangle : x,y,l,h (10,20,30,40)."));
                auto g = groups;
                if (std::find(g.begin(), g.end(), z.group) == g.end()) g.push_back(z.group);
                auto groupCell = editable("Groupe d'alarmes", z.group, PG::ValueType::Enum, index, "group",
                                          "Ses alarmes colorent la zone ; vide : le groupe du m\xC3\xAAme nom que la zone.", g);
                groupCell.typeIn = true;   // 1.12.7 (lot D) : un repere $Zone$ se tape aussi
                zc.properties.push_back(std::move(groupCell));
                auto vv = views;
                if (std::find(vv.begin(), vv.end(), z.view) == vv.end()) vv.push_back(z.view);
                auto viewCell = editable("Vue ouverte d'un clic", z.view, PG::ValueType::Enum, index, "view",
                                         "Vide : le clic choisit la zone sans changer de vue.", vv);
                viewCell.typeIn = true;
                zc.properties.push_back(std::move(viewCell));
                zc.properties.push_back(editable("Couleur", z.color, PG::ValueType::Color, index, "color",
                                                 "Sa couleur sans alarme ; vide : Couleur d'une zone calme."));
                cats.push_back(std::move(zc));
            } else {
                PG::Category info;
                info.name = "Zones";
                info.properties.push_back(readOnly("Zones", std::to_string(zones.size()),
                                                   "\xC2\xAB Ajouter \xC2\xBB pose une zone rectangulaire ; ses points se r\xC3\xA8glent ici."));
                cats.push_back(std::move(info));
            }
            break;
        }
        // Lot 12 : les pages d'un conteneur a onglets.
        case Mode::Tabs: {
            const auto labels = hmi::tabLabels(*o);
            PG::Category pc;
            pc.name = index >= 0 ? "Page " + std::to_string(index + 1) : std::string("Pages");
            if (index >= 0 && i < labels.size())
                pc.properties.push_back(editable("Libell\xC3\xA9", labels[i], PG::ValueType::Text, index, "label",
                                                 "Le texte de l'onglet. Un clic sur l'onglet, dans l'\xC3\xA9" "diteur, montre la page."));
            pc.properties.push_back(readOnly("Pages", std::to_string(labels.size()),
                                             "Un objet pos\xC3\xA9 sur le conteneur va dans la page montr\xC3\xA9" "e ; retirer ou "
                                             "d\xC3\xA9placer une page emm\xC3\xA8ne ses objets."));
            pc.properties.push_back(fromEnumCell(false));   // 1.12.7 (lot E) : un onglet par valeur, la variable de ce type
            cats.push_back(std::move(pc));
            break;
        }
        // Lot 12 : les vues d'une barre de navigation.
        case Mode::NavItems: {
            const auto n = readNav(doc_->project, *o);
            PG::Category nc;
            nc.name = index >= 0 ? "Bouton " + std::to_string(index + 1) : std::string("Vues");
            if (index >= 0 && i < n.views.size()) {
                std::vector<std::string> views;
                for (const auto& v : doc_->project.views)
                    if (v.role == "vue") views.push_back(v.name);
                if (std::find(views.begin(), views.end(), n.views[i]) == views.end()) views.push_back(n.views[i]);
                auto viewCell = editable("Vue", n.views[i], PG::ValueType::Enum, index, "view", "La vue ouverte d'un clic.", views);
                viewCell.typeIn = true;   // 1.12.7 (lot D) : un repere $Vue$ se tape aussi (montre en ambre)
                nc.properties.push_back(std::move(viewCell));
                nc.properties.push_back(editable("Libell\xC3\xA9", n.labels[i].empty() ? hmi::viewCaption(n.views[i]) : n.labels[i],
                                                 PG::ValueType::Text, index, "label", "Vide : le nom de la vue (sans Vue_, _ en espaces)."));
            }
            nc.properties.push_back(readOnly("Vues", std::to_string(n.views.size()) + (n.automatic ? "  (automatique)" : ""),
                                             "Automatique : toutes les vues ordinaires du projet, dans leur ordre. \xC2\xAB Ajouter \xC2\xBB "
                                             "fixe la liste."));
            cats.push_back(std::move(nc));
            break;
        }
        // 1.12.7 (lot D) : les choix - le libelle et la valeur ecrite, une ligne par choix.
        case Mode::Choices: {
            const auto c = readChoices(*o);
            if (const std::string from = hmi::itemsSourceOf(*o); !from.empty()) {
                PG::Category src;
                src.name = "\xC3\x89l\xC3\xA9ments depuis";
                src.properties.push_back(readOnly("Source", from,
                                                  "Les choix viennent de cette source (\xC3\x89l\xC3\xA9ments depuis, dans Propri\xC3\xA9t\xC3\xA9s) : "
                                                  "les lignes ci-dessous ne servent pas tant qu'elle est r\xC3\xA9gl\xC3\xA9" "e."));
                cats.push_back(std::move(src));
            }
            if (index >= 0 && i < c.labels.size()) {
                PG::Category ch;
                ch.name = "Choix " + std::to_string(index + 1);
                ch.properties.push_back(editable("Libell\xC3\xA9", c.labels[i], PG::ValueType::Text, index, "label",
                                                 "Ce que montre l'objet ; un texte, un rep\xC3\xA8re $Mode$. Pas de \xC2\xAB ; \xC2\xBB seul "
                                                 "(il couperait la liste)."));
                auto value = editable("Valeur \xC3\xA9" "crite", c.values[i], PG::ValueType::Text, index, "value",
                                      "Ce que le choix \xC3\xA9" "crit dans la variable : 2, 'Azote', TRUE, T_MODE#Auto, Auto (le nom "
                                      "d'une valeur d'\xC3\xA9num\xC3\xA9ration). Vide : le rang (" + std::to_string(i) + "), ou le "
                                      "libell\xC3\xA9 pour une STRING.");
                value.placeholder = "(rang " + std::to_string(i) + ")";
                ch.properties.push_back(std::move(value));
                cats.push_back(std::move(ch));
            } else {
                PG::Category info;
                info.name = "Choix";
                info.properties.push_back(readOnly("Choix", std::to_string(c.labels.size()),
                                                   "Une ligne par choix : son libell\xC3\xA9 et la valeur qu'il \xC3\xA9" "crit. "
                                                   "\xC2\xAB Ajouter \xC2\xBB en cr\xC3\xA9" "e un ; \xE2\x86\x91\xE2\x86\x93 l'ordre."));
                cats.push_back(std::move(info));
            }
            break;
        }
        // 1.12.7 (lot D) : les zones de couleur d'un bargraphe, d'un cadran.
        case Mode::Bands: {
            const auto bands = readBands(*o);
            if (index >= 0 && i < bands.size()) {
                PG::Category zc;
                zc.name = "Zone " + std::to_string(index + 1);
                const std::string bound = "Une valeur (60), ou un % de l'\xC3\xA9" "chelle (60 %) qui suit Min et Max.";
                zc.properties.push_back(editable("De", bands[i].from, PG::ValueType::Text, index, "from", bound));
                zc.properties.push_back(editable("\xC3\x80", bands[i].to, PG::ValueType::Text, index, "to", bound));
                zc.properties.push_back(editable("Couleur", bands[i].color, PG::ValueType::Color, index, "color"));
                cats.push_back(std::move(zc));
            }
            PG::Category info;
            info.name = "Zones";
            info.properties.push_back(readOnly("\xC3\x89" "chelle", hmi::formatNumber(o->number("min", 0)) + " \xE2\x80\xA6 "
                                                                    + hmi::formatNumber(o->number("max", 100)),
                                               "Min et Max (Propri\xC3\xA9t\xC3\xA9s). Une zone en % les suit quand ils changent."));
            info.properties.push_back(readOnly("Zones", std::to_string(bands.size())));
            cats.push_back(std::move(info));
            break;
        }
        // 1.12.7 (lot D) : les plages d'un programmateur, et ce que la resolution en fait.
        case Mode::Schedule: {
            const auto slots = readSlots(*o);
            const int res = hmi::resolutionMinutes(o->text("resolution", "30 min"));
            if (index >= 0 && i < slots.size()) {
                PG::Category sc;
                sc.name = "Plage " + std::to_string(index + 1);
                sc.properties.push_back(editable("Jours", slots[i].days, PG::ValueType::Text, index, "days",
                                                 "Lu, Ma, Me, Je, Ve, Sa, Di ; Lu-Ve ; Sa,Di ; tous."));
                sc.properties.push_back(editable("De", slots[i].from, PG::ValueType::Text, index, "from", "07:00, 7h30."));
                sc.properties.push_back(editable("\xC3\x80", slots[i].to, PG::ValueType::Text, index, "to", "12:00 ; 24:00 : minuit."));
                const std::string round = slotRounding(slots[i], res);
                sc.properties.push_back(readOnly("En marche", round.empty() ? std::string("telle quelle") : round,
                                                 "Le programmateur compte en cases de la r\xC3\xA9solution : une plage s'arrondit \xC3\xA0 "
                                                 "la case la plus proche."));
                cats.push_back(std::move(sc));
            }
            PG::Category info;
            info.name = "Plages";
            info.properties.push_back(readOnly("R\xC3\xA9solution", res == 60 ? std::string("1 h") : std::to_string(res) + " min",
                                               "R\xC3\xA9solution (Propri\xC3\xA9t\xC3\xA9s) : la taille d'une case."));
            info.properties.push_back(readOnly("Plages", std::to_string(slots.size()),
                                               "Elles servent tant que la variable du programme est vide ; ensuite, la variable garde "
                                               "ce que l'op\xC3\xA9rateur a coch\xC3\xA9."));
            cats.push_back(std::move(info));
            break;
        }
        // 1.12.7 (lot D, § 2.4) : les colonnes d'une source - lesquelles, leur titre, leur format.
        case Mode::SourceColumns: {
            const auto c = readSourceCols(doc_->project, *o);
            const auto rows = c.rows();
            PG::Category src;
            src.name = "Lignes depuis";
            src.properties.push_back(readOnly("Source", rowsSourceOf(*o),
                                              "Une ligne par \xC3\xA9l\xC3\xA9ment de la variable (Lignes depuis, dans Propri\xC3\xA9t\xC3\xA9s) : "
                                              "les cases du tableau ne servent pas. Ici, ses colonnes : lesquelles se montrent, dans "
                                              "quel ordre (\xE2\x86\x91\xE2\x86\x93), sous quel titre, \xC3\xA0 quel format."));
            src.properties.push_back(readOnly("Colonnes", c.chosen.empty() ? std::to_string(rows.size()) + " (toutes)"
                                                                           : std::to_string(c.chosen.size()) + " sur " + std::to_string(rows.size())));
            cats.push_back(std::move(src));
            if (index >= 0 && i < rows.size()) {
                const auto* ch = c.find(rows[i]);
                PG::Category cc;
                cc.name = "Colonne " + rows[i];
                cc.properties.push_back(editable("Montr\xC3\xA9" "e", c.chosen.empty() || ch ? "TRUE" : "FALSE", PG::ValueType::Boolean, index, "shown",
                                                 "Cach\xC3\xA9" "e, elle ne se montre pas (la source garde son membre)."));
                cc.properties.push_back(editable("Titre", ch ? ch->title : std::string{}, PG::ValueType::Text, index, "title",
                                                 "Vide : le nom du membre (" + rows[i] + ")."));
                cc.properties.push_back(editable("Format", ch ? ch->format : std::string{}, PG::ValueType::Text, index, "format",
                                                 "Pour un nombre : 0, 0.0, 0.00 ; vide : tel quel."));
                cats.push_back(std::move(cc));
            }
            break;
        }
        // 1.12.7 (lot D) : les cases a cocher (langues, boutons, zones).
        case Mode::Checks: {
            const auto c = readChecks(doc_->project, *o);
            PG::Category cc;
            const bool all = c.emptyIsAll && c.chosen.empty();
            cc.name = o->kind == hmi::Kind::UserManager ? "Boutons" : o->kind == hmi::Kind::AlarmSummary ? "Zones" : "Langues";
            if (c.emptyIsAll)
                cc.properties.push_back(readOnly(o->kind == hmi::Kind::AlarmSummary ? "Toutes les zones" : "Toutes les langues",
                                                 all ? "oui (liste vide)" : "non",
                                                 "Rien de coch\xC3\xA9 : toutes, dans l'ordre du projet. D\xC3\xA9" "cocher une case "
                                                 "garde les autres."));
            const auto list = c.rows();
            for (std::size_t k = 0; k < list.size(); ++k) {
                const bool typed = k >= c.offered.size();
                std::string help = typed ? std::string("Tap\xC3\xA9 dans Propri\xC3\xA9t\xC3\xA9s, pas propos\xC3\xA9 par le projet "
                                                       "(un rep\xC3\xA8re $\xE2\x80\xA6$ est gard\xC3\xA9 tel quel).")
                                         : k < c.details.size() ? c.details[k] : std::string{};
                cc.properties.push_back(editable(list[k], all || c.has(list[k]) ? "TRUE" : "FALSE", PG::ValueType::Boolean, -1,
                                                 "check:" + list[k], std::move(help)));
            }
            cats.push_back(std::move(cc));
            break;
        }
        case Mode::Members: {
            // 1.10.4 (K3) : ce que l'objet tient - en lecture ; on regle un objet en le choisissant.
            std::vector<const hmi::Object*> members;
            if (const auto* v = doc_->project.view(view_))
                for (const auto& c : v->objects)
                    if (c.parent == o->id) members.push_back(&c);
            const std::string how = o->kind == hmi::Kind::Group
                ? std::string("Ses objets se choisissent dans l'explorateur d'objets, ou d'un double-clic dans le groupe. "
                              "Dégrouper les rend libres.")
                : std::string("Ce qu'on pose dedans devient son enfant et le suit ; un double-clic y entre.");
            if (index >= 0 && i < members.size()) {
                const auto& member = *members[i];
                PG::Category mc;
                mc.name = member.name;
                mc.properties.push_back(readOnly("Type", std::string(hmi::kindLabel(member.kind))));
                mc.properties.push_back(readOnly("Position", hmi::formatNumber(member.number("x", 0)) + " ; " + hmi::formatNumber(member.number("y", 0))));
                mc.properties.push_back(readOnly("Taille", hmi::formatNumber(member.number("w", 0)) + " x " + hmi::formatNumber(member.number("h", 0))));
                mc.properties.push_back(readOnly("Réglages", "choisis-le", how));
                cats.push_back(std::move(mc));
            }
            PG::Category info;
            info.name = "Contenu";
            info.properties.push_back(readOnly("Objets", std::to_string(members.size()), how));
            cats.push_back(std::move(info));
            break;
        }
        case Mode::None: break;
    }
    grid_->setCategories(std::move(cats));
}

void HmiContentPanel::onLayout() {
    const auto b = bounds();
    tools_->setBounds({b.x, b.y, b.w, 34});
    const float listH = std::clamp(b.h * 0.34f, 70.f, 190.f);
    table_->setBounds({b.x, b.y + 34, b.w, listH});
    grid_->setBounds({b.x, b.y + 38 + listH, b.w, std::max(0.f, b.h - 38 - listH)});
}

void HmiContentPanel::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
