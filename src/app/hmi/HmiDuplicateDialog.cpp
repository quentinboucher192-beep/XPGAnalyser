// app/hmi/HmiDuplicateDialog.cpp - "Dupliquer..." (1.10.2, chantier D ; 1.50.0 : planche 04, 500 reperes).
#include "HmiDuplicateDialog.hpp"
#include "../../ui/DialogFrame.hpp"   // 1.12.21 : la fenetre se deplace et s'agrandit
#include "../../sim/ValueText.hpp"   // 1.12.19 : la lecture commune d'un nombre
#include "HmiAssist.hpp"           // 1.11.2 (API-V, D13) : le programme installe, ce qu'est API.V
#include "HmiPanels.hpp"           // 1.11 (T2-22) : les libelles de l'inspecteur
#include "HmiParamPanes.hpp"       // 1.11.2 (SYM) : les DDT du programme (les membres d'un parametre structure)

#include "../../hmi/HmiApiVars.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiPopupParams.hpp"   // 1.11.2 (SYM) : typeMembers
#include "../../hmi/HmiSymbols.hpp"       // 1.11.2 (SYM) : isSymbolView
#include "../../menu/MenuManager.hpp"
#include "../../project/MemberTree.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/DataViews.hpp"   // 1.50.0 : les profondeurs, la liste, l'apercu, les conflits
#include "../../ui/widgets/ScrollBar.hpp"   // 1.50.0 : le tableau (Excel) defile

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace dup = hmi::dup;

namespace {

// 1.11.2 (API-V, D13) : les bornes d'un tableau, pour les trois endroits qui les demandent
// (le suivi par defaut d'un indice, la case rouge, Remplir > Tableau...). D'abord les
// variables IHM du projet (V : 0..63, comme avant) ; puis un tableau de l'automate ecrit
// sous API. (API.V, API.Unite.Tab), d'apres ce que l'aide a la saisie en dit (le modele
// d'API-M, sur le programme installe) : une case $API.V[1].Ouv$ au-dela des bornes passe
// au rouge « hors des bornes de API.V (0..63) », comme une variable IHM. Le nom nu d'un
// tableau de l'automate (V[0] sans API.) garde la conduite d'avant (bornes inconnues) ;
// un projet IHM qui a sa propre variable ou vue API la garde (describe le sait).
// 1.11.2 (SYM, decision 240) : dans un symbole, ses parametres passent avant (`view` : la vue
// en cours d'edition ; nul ou pas un symbole : comme avant). Un parametre `Value : ARRAY[0..9]
// OF UINT` a les bornes 0..9 ; `Value[10]` passe au rouge « hors des bornes de Value (0..9) ».
dup::BoundsFn boundsOf(const hmi::DocumentPtr& doc, const hmi::View* view = nullptr);

// 1.11.2 (SYM) : les membres d'un type (un type IHM du projet, ou un DDT du programme de
// l'API) - pour `Armoire.ana.PT1` quand `Armoire : T_Armoire` est un parametre du symbole.
// Les DDT se relevent une fois (la fenetre est modale).
dup::TypeMembers symbolTypeMembers(const hmi::DocumentPtr& doc) {
    auto plcOf = assist::sourcesFor(doc).plc;
    auto memo = std::make_shared<std::optional<hmi::params::PlcTypes>>();
    return [doc, plcOf = std::move(plcOf), memo](std::string_view type) {
        if (!*memo) {
            const auto plc = plcOf ? plcOf() : nullptr;
            *memo = hmiparams::plcTypesOf(plc.get());
        }
        return hmi::params::typeMembers(doc->project, type, **memo);
    };
}

dup::BoundsFn boundsOf(const hmi::DocumentPtr& doc, const hmi::View* view) {
    auto hmiBounds = dup::projectBounds(doc->project);
    auto plcOf = assist::sourcesFor(doc).plc;
    // Une fenetre modale : le programme ne change pas pendant qu'elle est ouverte.
    auto memo = std::make_shared<std::map<std::string, std::optional<dup::Bounds>>>();
    dup::BoundsFn inner = [doc, hmiBounds = std::move(hmiBounds), plcOf = std::move(plcOf), memo](std::string_view path) -> std::optional<dup::Bounds> {
        if (auto b = hmiBounds(path)) return b;
        if (!plcOf || !hmi::apivars::isApiPath(path)) return std::nullopt;
        std::string key(path);
        for (auto& ch : key) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (const auto it = memo->find(key); it != memo->end()) return it->second;
        std::optional<dup::Bounds> out;
        if (const auto plc = plcOf()) {
            const auto d = assist::describe(doc->project, plc.get(), path);
            const auto shape = project::members::parseArray(d.type);
            if (d.found && shape.dims.size() == 1) out = dup::Bounds{shape.dims[0].first, shape.dims[0].second};
        }
        (*memo)[key] = out;
        return out;
    };
    // 1.11.2 (SYM) : dans un symbole, ses parametres d'abord.
    if (view && hmi::isSymbolView(*view)) return dup::symbolParamBounds(*view, std::move(inner), symbolTypeMembers(doc));
    return inner;
}

const gfx::FontId kSmall{13};
const gfx::FontId kText{15};
const gfx::Color  kOrange{230, 140, 30, 255};
const gfx::Color  kBlue{60, 130, 220, 255};

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

std::string markerLabel(const std::string& name) { return "$" + name + "$"; }

// ---- 1.11 (T2-22) : l'avant / apres nomme les proprietes comme l'inspecteur ----
// Les libelles que l'inspecteur affiche pour un objet, selon son genre ("opening" ->
// "Ouverture (%)", "fault" -> "D\xC3\xA9" "faut (expression)", "label" -> "Libell\xC3\xA9").
// Chaque ligne de l'inspecteur dit sa cle par son commit, qu'on ecoute sans rien
// changer (il rend faux) ; une ligne qui ne prend que l'expression est rangee sous
// "cle (expression)". Rien n'est recopie de l'inspecteur : ses libelles font foi.
using Labels = std::map<std::string, std::string>;
Labels inspectorLabels(const hmi::View& view, hmi::Id id, const hmi::Project* project) {
    Labels out;
    std::string key;
    bool expr = false;
    HmiPropertyCommits listen;
    listen.prop = [&](const std::string& k, const std::string&, bool e) {
        key = k;
        expr = e;
        return false;
    };
    for (const auto& cat : hmiPropertyCategories(view, {id}, nullptr, listen, nullptr, project))
        for (const auto& p : cat.properties) {
            if (!p.commit) continue;
            key.clear();
            (void)p.commit("x");
            if (key.empty()) continue;
            std::string name = p.name;
            if (const auto f = name.find("  \xC6\x92"); f != std::string::npos) name.resize(f);   // la marque d'une expression
            out.emplace(expr ? key + " (expression)" : key, std::move(name));
        }
    return out;
}

// L'endroit d'un repere ("opening (expression)") sous le nom de l'inspecteur
// ("Ouverture (%) (expression)") ; une propriete qu'il ne montre pas prend son
// libelle general ; une action ou une alarme garde le sien ("action 1 (cible)").
std::string spotLabel(const Labels& labels, const std::string& where) {
    if (const auto it = labels.find(where); it != labels.end()) return it->second;
    static const std::string kExpr = " (expression)";
    const bool isExpr = where.size() > kExpr.size() && where.compare(where.size() - kExpr.size(), kExpr.size(), kExpr) == 0;
    const std::string key = isExpr ? where.substr(0, where.size() - kExpr.size()) : where;
    std::string label;
    if (const auto it = labels.find(key); it != labels.end()) label = it->second;
    else if (std::string help; !hmiPropertyInfo(key, label, help) || label.empty()) return where;
    return isExpr && label.find("(expression)") == std::string::npos ? label + kExpr : label;
}
// ---- fin 1.11 (T2-22) ----

// Un trait pointille (les cadres des copies dans l'apercu).
void dashed(const ui::PaintContext& ctx, gfx::Rect r, gfx::Color c) {
    const float d = 5.f, g = 3.f;
    for (float x = r.x; x < r.x + r.w; x += d + g) {
        const float w = std::min(d, r.x + r.w - x);
        ctx.r.fillRect({x, r.y, w, 1.5f}, c);
        ctx.r.fillRect({x, r.y + r.h - 1.5f, w, 1.5f}, c);
    }
    for (float y = r.y; y < r.y + r.h; y += d + g) {
        const float h = std::min(d, r.y + r.h - y);
        ctx.r.fillRect({r.x, y, 1.5f, h}, c);
        ctx.r.fillRect({r.x + r.w - 1.5f, y, 1.5f, h}, c);
    }
}

// ---- 1.50.0 (planche 04) : ce que l'ecran dessine tient dans son cadre ----
// Le texte tel qu'il tient dans `w` pixels : entier, sinon raccourci par « ... » (par caracteres entiers) ; jamais
// coupe a un bord (les regles du cadre de la 1.47).
std::string fitText(std::string_view t, gfx::FontId f, float w) {
    if (t.empty() || w <= 0.f) return {};
    if (ui::measureWidth(t, f) <= w) return std::string(t);
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < t.size();) {
        starts.push_back(i);
        std::size_t j = i + 1;
        while (j < t.size() && (static_cast<unsigned char>(t[j]) & 0xC0) == 0x80) ++j;
        i = j;
    }
    static const std::string kEllipsis = "\xE2\x80\xA6";
    std::size_t lo = 0, hi = starts.size() - 1;   // le plus grand nombre de caracteres qui tient avec « ... »
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (ui::measureWidth(std::string(t.substr(0, starts[mid])) + kEllipsis, f) <= w) lo = mid;
        else hi = mid - 1;
    }
    if (lo == 0) return ui::measureWidth(kEllipsis, f) <= w ? kEllipsis : std::string{};
    return std::string(t.substr(0, starts[lo])) + kEllipsis;
}

// Un texte dessine dans sa place (raccourci s'il le faut).
struct Painted {
    gfx::Rect   r;
    std::string text;
    gfx::FontId font{13};
    int         tone{0};   // 0 le texte, 1 gris, 2 l'accent, 3 rouge, 4 ambre
};

gfx::Color toneOf(const ui::Theme& th, int tone) {
    switch (tone) {
    case 1: return th.color.textMuted;
    case 2: return th.color.accent;
    case 3: return th.color.error;
    case 4: return th.color.warning;
    default: return th.color.text;
    }
}

void paintAll(const ui::PaintContext& ctx, const std::vector<Painted>& list) {
    for (const auto& p : list) {
        const std::string t = fitText(p.text, p.font, p.r.w);
        if (t.empty()) continue;
        ctx.r.drawText({p.r.x, p.r.y + std::max(0.f, (p.r.h - ctx.r.lineHeight(p.font)) * 0.5f)}, t, p.font, toneOf(ctx.theme, p.tone));
    }
}

const gfx::FontId kHead{13};
constexpr float kRow = 30.f;      // une ligne d'outils

// Le libelle d'un bouton et sa largeur (le texte, ses marges).
float buttonWidth(const ui::Button* b) { return std::max(40.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 24.f); }

// Le dernier morceau d'un libelle de profondeur sans son crochet : "Departs[.]" -> "Departs".
std::string bare(const std::string& tail) {
    const auto b = tail.find('[');
    return b == std::string::npos ? tail : tail.substr(0, b);
}

// Les indices d'une valeur, en court : "API.Armoires[3].Departs[7].Voies[5].Etat" -> "[3] . [7] . [5]".
std::string indicesShort(const std::string& value) {
    std::string out;
    for (const auto& u : dup::indicesIn(value)) out += (out.empty() ? "[" : " \xC2\xB7 [") + std::to_string(u.value) + "]";
    return out.empty() ? value : out;
}

} // namespace

// ============================================================ les modeles ===
// « Les profondeurs » : une ligne par groupe (les profondeurs d'indice, les adresses, les textes, les variables, les noms).
class HmiDuplicateDialog::DepthModel final : public ui::ITableModel {
public:
    explicit DepthModel(HmiDuplicateDialog& d) : d_(d) {}
    [[nodiscard]] std::size_t rowCount() const override { return d_.model_.groups.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 8; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* kTitles[] = {"Prof.", "Dans le chemin", "Original", "Varie ?", "R\xC3\xA8gle", "Donne (copies)", "Bornes", "Rep\xC3\xA8res"};
        return c < 8 ? kTitles[c] : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t c) const override {
        if (row >= d_.model_.groups.size()) return {};
        const auto& g = d_.model_.groups[row];
        const auto& rule = d_.rules_.rule(g);
        switch (c) {
        case 0: return g.kind == dup::GroupKind::Index ? std::to_string(g.depth) : std::string("\xE2\x80\x94");
        case 1: return g.label + (g.kind == dup::GroupKind::Index && !g.marked ? std::string("  (hors rep\xC3\xA8re)") : std::string{});
        case 2: return g.original;
        case 3: return variesText(g, rule);
        case 4: return ruleText(g, rule);
        case 5: return gives(g);
        case 6: return boundsText(g);
        case 7: return g.kind == dup::GroupKind::Name ? std::string("\xE2\x80\x94") : std::to_string(g.kind == dup::GroupKind::Index && !g.marked ? g.spots : g.markers.size());
        default: return {};
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t c) const override {
        ui::CellStyle st;
        if (row >= d_.model_.groups.size()) return st;
        const auto& g = d_.model_.groups[row];
        const auto& rule = d_.rules_.rule(g);
        if (c == 3) {
            st.bold = rule.kind == dup::RuleKind::Formula || rule.kind == dup::RuleKind::Auto;
            st.fgTone = st.bold ? ui::Tone::Accent : ui::Tone::Muted;
        }
        if (c == 4) {
            st.monospace = rule.kind == dup::RuleKind::Formula;
            if (rule.kind != dup::RuleKind::Formula) st.fgTone = ui::Tone::Muted;
            for (const auto& e : d_.ruleErrors_)
                if (e.where == g.label) st.fgTone = ui::Tone::Error;
        }
        if (c == 6) st.fgTone = boundsTone(g);
        if (c == 2 || c == 5) st.monospace = true;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(ui::RowIndex row, std::size_t c) const override { return row < d_.model_.groups.size() && (c == 3 || c == 4); }
    [[nodiscard]] std::vector<std::string> cellChoices(ui::RowIndex row, std::size_t c) const override {
        if (c != 3 || row >= d_.model_.groups.size()) return {};
        switch (d_.model_.groups[row].kind) {
        case dup::GroupKind::Text:
        case dup::GroupKind::Variable: return {"varie", "fixe", "\xC3\xA0 remplir"};
        case dup::GroupKind::Name: return {"en s\xC3\xA9rie", "varie", "libre"};
        default: return {"varie", "fixe"};
        }
    }
    bool setCellText(ui::RowIndex row, std::size_t c, std::string_view text) override {
        if (row >= d_.model_.groups.size()) return false;
        const auto g = d_.model_.groups[row];
        if (c == 3) {
            if (text == "fixe" || text == "libre") return d_.setGroupRule(g.key, {dup::RuleKind::Fixed, {}});
            if (text == "\xC3\xA0 remplir") return d_.setGroupRule(g.key, {dup::RuleKind::Empty, {}});
            if (text == "en s\xC3\xA9rie") return d_.setGroupRule(g.key, {dup::RuleKind::Auto, {}});
            return d_.setVaries(g.key, true);
        }
        if (c == 4) {
            std::string f(text);
            if (f.empty()) {
                const auto kind = g.kind == dup::GroupKind::Name ? dup::RuleKind::Auto
                                : g.kind == dup::GroupKind::Text || g.kind == dup::GroupKind::Variable ? dup::RuleKind::Empty : dup::RuleKind::Fixed;
                return d_.setGroupRule(g.key, {kind, {}});
            }
            return d_.setGroupRule(g.key, {dup::RuleKind::Formula, std::move(f)});
        }
        return false;
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex row) const override {
        if (row >= d_.model_.groups.size()) return {};
        const auto& g = d_.model_.groups[row];
        std::string t = g.label + " : " + ruleText(g, d_.rules_.rule(g));
        for (const auto& e : d_.ruleErrors_)
            if (e.where == g.label) t += "\n" + e.why;
        return t + "\nLes formules, comme les adresses : i, j, k les compteurs, n le rang, o l'original ; + - * / MOD ( ). "
                   "Un texte : {i+1}, {j:02}.";
    }

private:
    static std::string variesText(const dup::Group& g, const dup::Rule& r) {
        if (g.kind == dup::GroupKind::Name) return r.kind == dup::RuleKind::Auto ? "en s\xC3\xA9rie" : r.kind == dup::RuleKind::Formula ? "varie" : "libre";
        if (r.kind == dup::RuleKind::Empty) return "\xC3\xA0 remplir";
        return r.kind == dup::RuleKind::Formula ? "varie" : "fixe";
    }
    std::string ruleText(const dup::Group& g, const dup::Rule& r) const {
        switch (r.kind) {
        case dup::RuleKind::Formula: return r.formula;
        case dup::RuleKind::Empty: return "les cases (S\xC3\xA9rie, Tableau\xE2\x80\xA6, Coller)";
        case dup::RuleKind::Auto: return "comme aujourd'hui (" + (d_.plan_.rows.size() > 1 ? d_.plan_.rows[1].name + "\xE2\x80\xA6)" : std::string("\xE2\x80\xA6)"));
        default: return g.kind == dup::GroupKind::Name ? std::string("un nom libre") : "= garder " + g.original;
        }
    }
    // La premiere place et la derniere : "0 -> 3", "%MW1000 -> %MW1037".
    std::string gives(const dup::Group& g) const {
        const auto& rows = d_.plan_.rows;
        if (rows.empty()) return {};
        const std::string arrow = " \xE2\x86\x92 ";
        if (g.kind == dup::GroupKind::Index) {
            const auto& rule = d_.rules_.rule(g);
            if (rule.kind != dup::RuleKind::Formula || g.values.empty()) return g.original;
            long long a = 0, b = 0;
            const long long o = g.values.front(), last = static_cast<long long>(rows.size()) - 1;
            if (!dup::evalInteger(rule.formula, d_.rules_.counters, {dup::placeAt(d_.rules_.counters, 0), o}, a)
                || !dup::evalInteger(rule.formula, d_.rules_.counters, {dup::placeAt(d_.rules_.counters, last), o}, b))
                return "formule fausse";
            return std::to_string(a) + arrow + std::to_string(b);
        }
        if (g.kind == dup::GroupKind::Name) return rows.front().name + arrow + (rows.back().name.empty() ? std::string("(libre)") : rows.back().name);
        if (g.markers.empty()) return {};
        const auto key = dup::markerKey(d_.scan_.markers[g.markers.front()].name);
        const auto at = [&](std::size_t r) {
            const auto it = rows[r].markers.find(key);
            return it == rows[r].markers.end() ? std::string{} : it->second;
        };
        const std::string a = at(0), b = at(rows.size() - 1);
        if (b.empty()) return d_.rules_.rule(g).kind == dup::RuleKind::Fixed ? std::string("(garde)") : std::string("(\xC3\xA0 remplir)");
        return (a.empty() ? d_.scan_.markers[g.markers.front()].name : a) + arrow + b;
    }
    [[nodiscard]] std::vector<long long> computed(const dup::Group& g) const {
        std::vector<long long> out;
        const auto& rule = d_.rules_.rule(g);
        if (g.kind != dup::GroupKind::Index || rule.kind != dup::RuleKind::Formula) return out;
        for (const long long o : g.values)
            for (std::size_t p = 0; p < d_.plan_.rows.size(); ++p) {
                long long v = 0;
                if (dup::evalInteger(rule.formula, d_.rules_.counters, {dup::placeAt(d_.rules_.counters, static_cast<long long>(p)), o}, v)) out.push_back(v);
            }
        return out;
    }
    std::string boundsText(const dup::Group& g) const {
        if (g.kind == dup::GroupKind::Index) {
            if (!g.bounds) return "inconnues";
            const std::string b = std::to_string(g.bounds->first) + ".." + std::to_string(g.bounds->second);
            for (const long long v : computed(g))
                if (v < g.bounds->first || v > g.bounds->second) return b + "  hors : " + std::to_string(v);
            return b + "  ok";
        }
        std::size_t n = 0;
        for (const auto& [key, level] : d_.flags_)
            if (level == 1 && isOfGroup(g, key.second)) ++n;
        if (n == 0) return "\xE2\x80\x94";
        return std::to_string(n) + (g.kind == dup::GroupKind::Name ? (n > 1 ? " pris" : " pris") : (n > 1 ? " prises" : " prise"));
    }
    ui::Tone boundsTone(const dup::Group& g) const {
        const std::string t = boundsText(g);
        if (t.find("hors") != std::string::npos) return ui::Tone::Error;
        if (t.find("pris") != std::string::npos) return ui::Tone::Warning;
        if (t.find("ok") != std::string::npos) return ui::Tone::Ok;
        return ui::Tone::Muted;
    }
    bool isOfGroup(const dup::Group& g, const std::string& key) const {
        if (g.kind == dup::GroupKind::Name) return key == "NOM";
        for (const std::size_t mi : g.markers)
            if (dup::markerKey(d_.scan_.markers[mi].name) == key) return true;
        return false;
    }
    HmiDuplicateDialog& d_;
};

// « Les reperes » : la liste groupee (virtuelle : la table ne dessine que les lignes visibles).
class HmiDuplicateDialog::ListModel final : public ui::ITableModel {
public:
    explicit ListModel(HmiDuplicateDialog& d) : d_(d) {}
    [[nodiscard]] std::size_t rowCount() const override { return d_.listRows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 4; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        switch (c) {
        case 0: return "Rep\xC3\xA8re";
        case 1: return "O\xC3\xB9 (objet \xC2\xB7 propri\xC3\xA9t\xC3\xA9)";
        case 2: return "R\xC3\xA8gle";
        default: {
            const auto& rows = d_.plan_.rows;
            if (rows.size() < 2) return "L'original";
            return "Derni\xC3\xA8re copie (" + dup::placeText(d_.rules_.counters, dup::placeAt(d_.rules_.counters, static_cast<long long>(rows.size()) - 1)) + ")";
        }
        }
    }
    [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t c) const override {
        if (row >= d_.listRows_.size()) return {};
        const auto& r = d_.listRows_[row];
        switch (c) {
        case 0: return r.text;
        case 1: return r.where;
        case 2: return r.rule;
        default: return r.last;
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t c) const override {
        ui::CellStyle st;
        if (row >= d_.listRows_.size()) return st;
        const auto& r = d_.listRows_[row];
        if (c == 0) {
            if (r.group) {
                st.bold = true;
                st.expander = r.expanded ? 1 : 0;
                st.badge = std::to_string(r.count);
            } else {
                st.indent = d_.grouping_ == Grouping::None ? 0.f : 22.f;
                st.monospace = true;
                if (r.own) {
                    st.lead = "\xC3\xA0 part";
                    st.leadTone = ui::Tone::Info;
                }
            }
        }
        if (c == 2 && !r.group && !r.own) st.fgTone = ui::Tone::Muted;
        if (c == 3) {
            st.monospace = true;
            if (r.conflict == 2) st.fgTone = ui::Tone::Error;
            else if (r.conflict == 1) st.fgTone = ui::Tone::Warning;
        }
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex row) const override {
        if (row >= d_.listRows_.size()) return {};
        const auto& r = d_.listRows_[row];
        std::string t = r.text + "\n" + r.where + "\n" + r.rule;
        if (!r.last.empty()) t += "\n" + r.last;
        return t + "\nClic droit : Comme sa profondeur, Formule\xE2\x80\xA6, S\xC3\xA9rie (V1{n}), Tableau\xE2\x80\xA6, Coller (Excel), Garder, Vider.";
    }

private:
    HmiDuplicateDialog& d_;
};

// L'apercu : les 5 premieres copies, l'ecart, les 3 dernieres ; i, j (n), le nom, un repere de chaque sorte.
class HmiDuplicateDialog::PreviewModel final : public ui::ITableModel {
public:
    explicit PreviewModel(HmiDuplicateDialog& d) : d_(d) {}
    [[nodiscard]] std::size_t rowCount() const override { return d_.previewIndex_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2 + counters() + d_.samples_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        if (c == 0) return "#";
        const std::size_t k = counters();
        if (c <= k) return d_.rules_.counters.mode == dup::CountMode::Series ? std::string("n") : std::string(c == 1 ? "i" : c == 2 ? "j" : "k");
        if (c == k + 1) return "Nom";
        const std::size_t s = c - k - 2;
        if (s >= d_.samples_.size()) return {};
        const auto& mm = d_.model_.markers[d_.samples_[s]];
        if (mm.kind == dup::GroupKind::Index) {
            std::string h;
            for (const auto& sl : mm.slots) h += (h.empty() ? "" : " \xC2\xB7 ") + bare(d_.model_.groups[sl.group].tail);
            return h;
        }
        if (mm.kind == dup::GroupKind::Address) return mm.address ? mm.address->area : std::string("Adresse");
        return mm.kind == dup::GroupKind::Text ? "Texte" : "$" + d_.scan_.markers[d_.samples_[s]].name + "$";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t c) const override {
        if (row >= d_.previewIndex_.size()) return {};
        const int r = d_.previewIndex_[row];
        const auto P = static_cast<int>(d_.plan_.rows.size());
        if (r < 0) {
            if (c != 0) return {};
            const int first = 6, last = P - 3;
            return "\xE2\x80\xA6 " + std::to_string(last - first + 1) + " copies (" + std::to_string(first) + " \xC3\xA0 " + std::to_string(last)
                 + ") \xC2\xB7 double-clic : tout voir";
        }
        if (c == 0) return r == 0 ? std::string("1 orig.") : std::to_string(r + 1);
        const std::size_t k = counters();
        const auto at = dup::placeAt(d_.rules_.counters, r);
        if (c <= k) {
            if (d_.rules_.counters.mode == dup::CountMode::Series) return std::to_string(at.n);
            return std::to_string(c == 1 ? at.i : c == 2 ? at.j : at.k);
        }
        const auto& row0 = d_.plan_.rows[static_cast<std::size_t>(r)];
        if (c == k + 1) return row0.name.empty() ? std::string("(libre)") : row0.name;
        const std::size_t s = c - k - 2;
        if (s >= d_.samples_.size()) return {};
        const auto& mk = d_.scan_.markers[d_.samples_[s]];
        const auto it = row0.markers.find(dup::markerKey(mk.name));
        std::string v = it == row0.markers.end() ? std::string{} : it->second;
        const bool kept = std::find(d_.ruleKeep_.begin(), d_.ruleKeep_.end(), dup::markerKey(mk.name)) != d_.ruleKeep_.end();
        if (v.empty()) return kept || r == 0 ? indicesShort(mk.name) : std::string("(\xC3\xA0 remplir)");
        return d_.model_.markers[d_.samples_[s]].kind == dup::GroupKind::Index ? indicesShort(v) : v;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t c) const override {
        ui::CellStyle st;
        if (row >= d_.previewIndex_.size()) return st;
        const int r = d_.previewIndex_[row];
        if (r < 0) {
            st.spanRow = c == 0;
            st.fgTone = ui::Tone::Muted;
            return st;
        }
        if (r == 0) st.fgTone = ui::Tone::Muted;
        const std::size_t k = counters();
        if (c > k + 1) {
            st.monospace = true;
            const std::size_t s = c - k - 2;
            if (s < d_.samples_.size()) {
                const int level = d_.conflictOf(r, dup::markerKey(d_.scan_.markers[d_.samples_[s]].name));
                if (level == 2) st.fgTone = ui::Tone::Error;
                else if (level == 1) st.fgTone = ui::Tone::Warning;
            }
        }
        if (c == k + 1) {
            const int level = d_.conflictOf(r, "NOM");
            if (level == 1) st.fgTone = ui::Tone::Warning;
            else if (level == 2) st.fgTone = ui::Tone::Error;
        }
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }

private:
    std::size_t counters() const {
        return d_.rules_.counters.mode == dup::CountMode::Series ? 1 : std::min<std::size_t>(3, d_.rules_.counters.grid.size());
    }
    HmiDuplicateDialog& d_;
};

// Les conflits : les fautes d'abord (elles grisent Dupliquer), puis les avertissements.
class HmiDuplicateDialog::ConflictModel final : public ui::ITableModel {
public:
    explicit ConflictModel(HmiDuplicateDialog& d) : d_(d) {}
    [[nodiscard]] std::size_t rowCount() const override { return d_.conflicts_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 2; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "Conflit" : "Aller \xC3\xA0"; }
    [[nodiscard]] std::string cellText(ui::RowIndex row, std::size_t c) const override {
        if (row >= d_.conflicts_.size()) return {};
        const auto& k = d_.conflicts_[row];
        return c == 0 ? k.text : "ligne " + std::to_string(k.row + 1);
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex row, std::size_t c) const override {
        ui::CellStyle st;
        if (row >= d_.conflicts_.size()) return st;
        const auto& k = d_.conflicts_[row];
        if (c == 0) {
            st.lead = k.blocks ? "faute" : "avertit";
            st.leadTone = k.blocks ? ui::Tone::Error : ui::Tone::Warning;
            st.fgTone = k.blocks ? ui::Tone::Error : ui::Tone::Warning;
        } else
            st.fgTone = ui::Tone::Accent;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex row) const override {
        return row < d_.conflicts_.size() ? d_.conflicts_[row].text + (d_.conflicts_[row].blocks ? "\n(bloque Dupliquer)" : "\n(ne bloque pas)") : std::string{};
    }

private:
    HmiDuplicateDialog& d_;
};

// ================================================================= Band ===
// « Les copies » : une suite n ou une grille i x j (x k), les bornes de chaque compteur, le total ; le reste de la
// ligne (Remplacer aussi dans l'original, Preremplir depuis les bornes) passe a la ligne s'il ne tient pas.
class HmiDuplicateDialog::Band final : public ui::Widget {
public:
    explicit Band(HmiDuplicateDialog& d) : d_(d) {}
    // La hauteur pour `width` (la meme mise en page, sans rien poser).
    float heightFor(float width) { return flow(width, false); }
protected:
    void onLayout() override { (void)flow(bounds().w, true); }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, ctx.theme.color.border);
        paintAll(ctx, labels_);
    }
private:
    struct Item {
        ui::Widget* w{nullptr};
        std::string text;
        gfx::FontId font{13};
        int         tone{0};
        float       width{0};
        bool        breakBefore{false};
    };
    std::vector<Item> items() const {
        std::vector<Item> out;
        const auto label = [&](std::string t, int tone = 0, gfx::FontId f = gfx::FontId{13}) {
            Item it;
            it.width = ui::measureWidth(t, f) + 2.f;
            it.text = std::move(t);
            it.tone = tone;
            it.font = f;
            out.push_back(std::move(it));
        };
        const auto widget = [&](ui::Widget* w, float width) {
            if (!w) return;
            Item it;
            it.w = w;
            it.width = width;
            out.push_back(std::move(it));
        };
        const auto& c = d_.rules_.counters;
        label("LES COPIES", 1, kHead);
        for (auto* m : d_.modeButtons_) widget(m, buttonWidth(m));
        if (c.mode == dup::CountMode::Series) {
            label("Copies");
            widget(d_.copiesField_, 64.f);
        } else {
            static const char* kLetters[] = {"i", "j", "k"};
            for (std::size_t q = 0; q < c.grid.size() && q < 3; ++q) {
                if (q) label("\xC3\x97", 1);
                label(kLetters[q], 2, gfx::FontId{15});
                label("de", 1);
                widget(d_.rangeFields_[q * 2], 56.f);
                label("\xC3\xA0", 1);
                widget(d_.rangeFields_[q * 2 + 1], 56.f);
                std::string what = "(" + std::to_string(c.grid[q].size());
                for (const auto& g : d_.model_.groups)
                    if (g.kind == dup::GroupKind::Index && g.depth == static_cast<int>(q) + 1) {
                        what += " \xC2\xB7 " + bare(g.tail);
                        break;
                    }
                label(what + ")", 1);
            }
            widget(d_.kButton_, buttonWidth(d_.kButton_));
        }
        const long long places = c.places();
        const std::string orig = c.mode == dup::CountMode::Series ? std::string("n = 0") : dup::placeText(c, dup::placeAt(c, 0));
        label("= " + std::to_string(places) + (places > 1 ? " places" : " place") + " : l'original (" + orig + ") et "
                  + std::to_string(std::max<long long>(0, places - 1)) + (places > 2 ? " copies" : " copie"),
              2, gfx::FontId{15});
        if (d_.replaceOriginal_) {
            widget(d_.replaceOriginal_, std::min(320.f, ui::measureWidth(d_.replaceOriginal_->label(), gfx::FontId{16}) + 40.f));
            out.back().breakBefore = true;
        }
        if (c.mode == dup::CountMode::Grid && c.grid.size() > 1)
            label(std::string(c.grid.size() == 3 ? "k" : "j") + " varie le plus vite \xC2\xB7", 1);
        widget(d_.boundsButton_, buttonWidth(d_.boundsButton_));
        return out;
    }
    float flow(float width, bool place) {
        const auto b = bounds();
        const float left = b.x + 10.f, right = b.x + width - 10.f;
        float x = left, y = b.y + 6.f;
        if (place) labels_.clear();
        const auto list = items();
        std::set<ui::Widget*> shown;
        for (const auto& it : list) {
            const float w = std::min(it.width, right - left);
            if (x > left && (x + w > right || it.breakBefore)) {
                x = left;
                y += kRow + 4.f;
            }
            if (place) {
                if (it.w) {
                    it.w->setVisibility(ui::Visibility::Visible);
                    it.w->setBounds({x, y, w, kRow - 2.f});
                    shown.insert(it.w);
                } else
                    labels_.push_back({{x, y, w, kRow - 2.f}, it.text, it.font, it.tone});
            }
            x += w + 8.f;
        }
        if (place) {
            // Ce que cette sorte de compteurs ne montre pas.
            std::vector<ui::Widget*> all(d_.rangeFields_.begin(), d_.rangeFields_.end());
            all.push_back(d_.copiesField_);
            all.push_back(d_.kButton_);
            for (auto* w : all)
                if (w && !shown.count(w)) {
                    w->setVisibility(ui::Visibility::Collapsed);
                    w->setBounds({b.x, b.y, 0.f, 0.f});
                }
        }
        return y + kRow + 6.f - b.y;
    }
    HmiDuplicateDialog& d_;
    std::vector<Painted> labels_;
};

// ----------------------------------------------------------- CopiesPage ---
// A gauche : les profondeurs (une regle par profondeur), puis les reperes (la liste) ; a droite : l'apercu (les
// totaux, les premieres et les dernieres copies, l'avant / apres de la ligne choisie), puis les conflits.
class HmiDuplicateDialog::CopiesPage final : public ui::Widget {
public:
    explicit CopiesPage(HmiDuplicateDialog& d) : d_(d) {}
    std::vector<ui::Widget*> toolbar;     // la recherche, les filtres, Grouper par, Tout deplier / replier
protected:
    void onLayout() override {
        const auto b = bounds();
        texts_.clear();
        tiles_.clear();
        float y = b.y + 2.f;
        if (d_.band_) {
            const float h = d_.band_->heightFor(b.w);
            d_.band_->setBounds({b.x, y, b.w, h});
            y += h + 6.f;
        }
        const float bottom = b.bottom() - 4.f;
        const float gap = 14.f;
        const float leftW = std::floor(std::max(240.f, (b.w - gap) * 0.58f));
        const float lx = b.x + 4.f, rx = lx + leftW + gap, rw = std::max(120.f, b.right() - 4.f - rx);
        const float rowH = 26.f;
        // ---- a gauche : les profondeurs ----
        float ly = y;
        head(lx, ly, leftW, "LES PROFONDEURS", "une r\xC3\xA8gle par indice de chemin ; les rep\xC3\xA8res la suivent");
        ly += 22.f;
        const float avail = std::max(120.f, bottom - ly);
        const float depthH = std::min(static_cast<float>(d_.model_.groups.size()) * rowH + 34.f, std::max(84.f, avail * 0.36f));
        if (d_.depthTable_) d_.depthTable_->setBounds({lx, ly, leftW, depthH});
        ly += depthH + 4.f;
        texts_.push_back({{lx, ly, leftW, 18.f},
                          "Les formules, comme les adresses : i, j, k les compteurs, n le rang, o l'original ; + - * / MOD ( ) ; "
                          "un texte : 'D\xC3\xA9part {i+1}.{j+1}', {j:02}.",
                          gfx::FontId{12}, 1});
        ly += 22.f;
        head(lx, ly, leftW, "LES REP\xC3\x88RES  " + std::to_string(d_.scan_.markers.size()),
             d_.grouping_ == Grouping::Depth ? "group\xC3\xA9s par profondeur ; la liste d\xC3\xA9" "file (rien n'est coup\xC3\xA9)"
                                            : "la liste d\xC3\xA9" "file (rien n'est coup\xC3\xA9)");
        ly += 22.f;
        // La barre de la liste : la recherche, les filtres, Grouper par, Tout deplier / replier (a la ligne s'il le faut).
        {
            float x = lx;
            const float right = lx + leftW;
            const auto put = [&](ui::Widget* w, float width) {
                width = std::min(width, leftW);
                if (x > lx && x + width > right) {
                    x = lx;
                    ly += kRow + 4.f;
                }
                w->setBounds({x, ly, width, kRow - 2.f});
                x += width + 6.f;
            };
            if (d_.searchField_) put(d_.searchField_, std::clamp(leftW * 0.34f, 150.f, 300.f));
            for (auto* f : d_.filterButtons_) put(f, buttonWidth(f));
            const float gw = ui::measureWidth("Grouper par", gfx::FontId{13}) + 4.f;
            if (x > lx && x + gw > right) {
                x = lx;
                ly += kRow + 4.f;
            }
            texts_.push_back({{x, ly, gw, kRow - 2.f}, "Grouper par", gfx::FontId{13}, 1});
            x += gw + 6.f;
            for (auto* g : d_.groupButtons_) put(g, buttonWidth(g));
            for (auto* f : d_.foldButtons_) put(f, buttonWidth(f));
            ly += kRow + 4.f;
        }
        const float noteH = 18.f;
        const float editH = d_.editMode_ ? kRow + 6.f : 0.f;
        const float listH = std::max(60.f, bottom - ly - noteH - editH - 6.f);
        if (d_.listTable_) d_.listTable_->setBounds({lx, ly, leftW, listH});
        ly += listH + 4.f;
        // La regle a part en cours d'ecriture (Formule..., Serie...).
        {
            const bool on = d_.editMode_ != 0;
            float x = lx;
            if (on) {
                std::string who = d_.editing_.empty() ? std::string{} : markerLabel(d_.scan_.markers[d_.editing_.front()].name);
                if (d_.editing_.size() > 1) who += " (+" + std::to_string(d_.editing_.size() - 1) + ")";
                const std::string lead = (d_.editMode_ == 1 ? "R\xC3\xA8gle \xC3\xA0 part de " : "S\xC3\xA9rie pour ") + who + " :";
                const float lw = std::min(leftW * 0.32f, ui::measureWidth(lead, gfx::FontId{13}) + 4.f);
                texts_.push_back({{x, ly, lw, kRow - 2.f}, lead, gfx::FontId{13}, 0});
                x += lw + 6.f;
            }
            const auto put = [&](ui::Widget* w, float width, bool show) {
                if (!w) return;
                w->setVisibility(show ? ui::Visibility::Visible : ui::Visibility::Collapsed);
                if (!show) return;
                width = std::max(30.f, std::min(width, lx + leftW - x));
                w->setBounds({x, ly, width, kRow - 2.f});
                x += width + 6.f;
            };
            const bool depthChoice = on && d_.editMode_ == 1 && d_.ownDepth_ && !d_.editing_.empty()
                                  && d_.model_.markers[d_.editing_.front()].kind == dup::GroupKind::Index;
            put(d_.ownDepth_, 150.f, depthChoice);
            float buttons = 0.f;
            for (auto* bt : d_.ownButtons_) buttons += buttonWidth(bt) + 6.f;
            const float fieldW = std::max(80.f, lx + leftW - x - buttons - (d_.editMode_ == 2 ? 70.f : 0.f));
            put(d_.ownField_, fieldW, on);
            put(d_.ownFrom_, 64.f, on && d_.editMode_ == 2);
            for (auto* bt : d_.ownButtons_) put(bt, buttonWidth(bt), on);
            if (on) ly += kRow + 6.f;
        }
        texts_.push_back({{lx, ly, leftW, noteH},
                          "Clic droit sur un rep\xC3\xA8re ou un groupe : Comme sa profondeur \xC2\xB7 Formule\xE2\x80\xA6 \xC2\xB7 S\xC3\xA9rie (V1{n}) \xC2\xB7 "
                          "Tableau\xE2\x80\xA6 \xC2\xB7 Coller (Excel) \xC2\xB7 Garder \xC2\xB7 Vider \xE2\x80\x94 les \xC2\xAB Remplir \xC2\xBB d'avant, par groupe.",
                          gfx::FontId{12}, 1});
        // ---- a droite : l'apercu ----
        float ry = y;
        head(rx, ry, rw, "APER\xC3\x87U", "recalcul\xC3\xA9 \xC3\xA0 chaque changement");
        ry += 22.f;
        {
            const auto st = d_.stats();
            const std::pair<std::string, std::string> tiles[] = {
                {std::to_string(st.copies), "copies (" + std::to_string(st.places) + " places)"},
                {std::to_string(st.markers), "rep\xC3\xA8res"},
                {std::to_string(st.replacements), "remplacements"},
                {std::to_string(st.conflicts), "conflits (" + plural(st.faults, "faute", "fautes") + ")"}};
            const int perRow = rw >= 4.f * 96.f ? 4 : 2;
            const float tw = (rw - static_cast<float>(perRow - 1) * 8.f) / static_cast<float>(perRow);
            for (int i = 0; i < 4; ++i) {
                const float tx = rx + static_cast<float>(i % perRow) * (tw + 8.f);
                const float ty = ry + static_cast<float>(i / perRow) * 54.f;
                tiles_.push_back({{tx, ty, tw, 48.f}, tiles[i].first, tiles[i].second, i == 3 && st.faults > 0 ? 3 : i == 3 && st.conflicts > 0 ? 4 : 0});
            }
            ry += (perRow == 4 ? 1.f : 2.f) * 54.f + 4.f;
        }
        const float rest = std::max(80.f, bottom - ry);
        const float prevH = std::min(static_cast<float>(d_.previewIndex_.size()) * rowH + 34.f, std::max(80.f, rest * 0.42f));
        if (d_.previewTable_) d_.previewTable_->setBounds({rx, ry, rw, prevH});
        ry += prevH + 6.f;
        // L'avant / apres de la ligne choisie.
        {
            const auto& c = d_.rules_.counters;
            std::string title = "AVANT / APR\xC3\x88S, LIGNE " + std::to_string(d_.chosen_ + 1);
            title += d_.chosen_ == 0 ? std::string(" (l'original)") : " (" + dup::placeText(c, dup::placeAt(c, d_.chosen_)) + ")";
            texts_.push_back({{rx, ry, rw, 20.f}, title, kHead, 1});
            ry += 22.f;
            const auto lines = d_.beforeAfter(d_.chosen_);
            const float room = std::max(0.f, bottom - ry - 22.f - 80.f);
            const std::size_t fit = std::min<std::size_t>(lines.size(), static_cast<std::size_t>(std::max(1.f, std::min(6.f, room / 18.f))));
            for (std::size_t i = 0; i < fit; ++i) {
                texts_.push_back({{rx + 4.f, ry, rw - 4.f, 18.f}, lines[i], gfx::FontId{12}, 1});
                ry += 18.f;
            }
            std::size_t spots = 0;
            for (const auto& m : d_.scan_.markers) spots += m.spots.size();
            for (const auto& k : d_.scan_.indices) spots += k.spots.size();
            if (spots > fit && fit > 0) {
                texts_.push_back({{rx + 4.f, ry, rw - 4.f, 18.f}, "+ " + std::to_string(spots - fit) + " autres, dans l'ordre des objets", gfx::FontId{12}, 1});
                ry += 18.f;
            }
            ry += 6.f;
        }
        {
            std::size_t faults = 0;
            for (const auto& k : d_.conflicts_) faults += k.blocks ? 1 : 0;
            head(rx, ry, rw, "CONFLITS  " + std::to_string(d_.conflicts_.size()), "les fautes d'abord ; un clic m\xC3\xA8ne \xC3\xA0 la ligne");
            ry += 22.f;
            if (d_.conflictTable_) d_.conflictTable_->setBounds({rx, ry, rw, std::max(40.f, bottom - ry)});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        for (const auto& t : tiles_) {
            ctx.r.fillRoundedRect(t.r, c.inputBg, 8.f);
            ctx.r.strokeRect(t.r, c.border, 1.f);
            const std::string big = fitText(t.big, gfx::FontId{20}, t.r.w - 16.f);
            if (!big.empty()) ctx.r.drawText({t.r.x + 8.f, t.r.y + 3.f}, big, gfx::FontId{20}, t.tone ? toneOf(ctx.theme, t.tone) : c.text);
            const std::string small = fitText(t.small, gfx::FontId{12}, t.r.w - 16.f);
            if (!small.empty()) ctx.r.drawText({t.r.x + 8.f, t.r.y + 28.f}, small, gfx::FontId{12}, c.textMuted);
        }
        paintAll(ctx, texts_);
    }
private:
    void head(float x, float y, float w, std::string title, std::string note) {
        const float tw = std::min(w, ui::measureWidth(title, kHead) + 4.f);
        texts_.push_back({{x, y, tw, 20.f}, std::move(title), kHead, 0});
        const float nw = std::min(ui::measureWidth(note, gfx::FontId{12}) + 2.f, std::max(0.f, w - tw - 12.f));
        if (nw > 24.f) texts_.push_back({{x + w - nw, y, nw, 20.f}, std::move(note), gfx::FontId{12}, 1});
    }
    struct Tile {
        gfx::Rect   r;
        std::string big, small;
        int         tone{0};
    };
    HmiDuplicateDialog& d_;
    std::vector<Painted> texts_;
    std::vector<Tile>    tiles_;
};

// ================================================================= Sheet ===
// « Tableau (Excel) » : la grille d'avant - une ligne par copie (l'original en tete), le nom puis une colonne par
// repere. VIRTUELLE : seules les cases visibles se dessinent (512 colonnes x 32 lignes : quelques dizaines de cases
// a l'ecran) ; # et le nom restent figes a gauche quand on defile ; un clic ouvre la case (un seul champ, pose dessus,
// avec l'aide a la saisie) ; Entree valide et descend, Echap annule ; Ctrl+V colle une colonne d'Excel depuis la case.
class HmiDuplicateDialog::Sheet final : public ui::Widget {
public:
    explicit Sheet(HmiDuplicateDialog& d) : ui::Widget("dialog.hmiDuplicate.sheet"), d_(d), vbar_(false), hbar_(true) { setFocusPolicy(true); }
    ui::InputText* editor{nullptr};
    void dirty() {
        widths_.clear();
        invalidate();
    }
    [[nodiscard]] SheetStats stats() const {
        SheetStats s;
        s.rows = d_.plan_.rows.size();
        s.columns = d_.columns().size();
        s.drawnCells = drawn_;
        s.frozenColumns = 2;
        s.scrollX = sx_;
        s.scrollY = sy_;
        return s;
    }
    void scrollTo(float x, float y) {
        measure();
        sx_ = std::clamp(x, 0.f, maxX());
        sy_ = std::clamp(y, 0.f, maxY());
        place();
        invalidate();
    }
    void commit() {
        if (!editor || editRow_ < 0) return;
        const int r = editRow_, c = editCol_;
        editRow_ = editCol_ = -1;
        const std::string text = editor->text();
        editor->setVisibility(ui::Visibility::Collapsed);
        if (r < static_cast<int>(d_.plan_.rows.size()) && c >= 0 && text != d_.cell(r, d_.columns()[static_cast<std::size_t>(c)]))
            d_.setCell(r, d_.columns()[static_cast<std::size_t>(c)], text);
        invalidate();
    }
    void cancel() {
        if (!editor) return;
        editRow_ = editCol_ = -1;
        editor->setVisibility(ui::Visibility::Collapsed);
        invalidate();
    }
protected:
    void onLayout() override { place(); }
    void onPaint(const ui::PaintContext& ctx) override {
        measure();
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const auto cols = d_.columns();
        const auto& rows = d_.plan_.rows;
        drawn_ = 0;
        const gfx::Rect area = body();
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.pushClip(b);
        // La tete : #, Nom (figes), puis les reperes.
        ctx.r.fillRect({b.x, b.y, area.w, kHeadH}, c.headerBg);
        const float frozenRight = area.x + kNumW + width(0);
        const auto cellText = [&](float x, float y, float w, const std::string& t, gfx::FontId f, gfx::Color k) {
            const std::string s = fitText(t, f, w - 12.f);
            if (!s.empty()) ctx.r.drawText({x + 6.f, y + (kRowH - ctx.r.lineHeight(f)) * 0.5f}, s, f, k);
        };
        // Les colonnes visibles (apres les figees).
        std::size_t first = 1;
        float x0 = frozenRight - sx_;
        while (first < cols.size() && x0 + width(first) <= frozenRight) {
            x0 += width(first);
            ++first;
        }
        ctx.r.pushClip({frozenRight, b.y, std::max(0.f, area.right() - frozenRight), area.h + kHeadH});
        {
            float x = x0;
            for (std::size_t col = first; col < cols.size() && x < area.right(); ++col) {
                cellText(x, b.y, width(col), d_.gridHeader(col, width(col) - 12.f), ctx.theme.font.uiBold, c.text);
                ctx.r.fillRect({x + width(col) - 1.f, b.y + 4.f, 1.f, kHeadH - 8.f}, c.border);
                x += width(col);
            }
        }
        ctx.r.popClip();
        cellText(b.x, b.y, kNumW, "#", ctx.theme.font.uiBold, c.textMuted);
        cellText(b.x + kNumW, b.y, width(0), "Nom", ctx.theme.font.uiBold, c.text);
        ctx.r.fillRect({b.x, b.y + kHeadH - 1.f, area.w, 1.f}, c.border);
        // Les lignes visibles.
        const auto firstRow = static_cast<std::size_t>(std::max(0.f, sy_ / kRowH));
        const float y0 = area.y - (sy_ - static_cast<float>(firstRow) * kRowH);
        std::string originalName = rows.empty() ? std::string{} : rows.front().name;
        ctx.r.pushClip(area);
        for (std::size_t r = firstRow; r < rows.size(); ++r) {
            const float y = y0 + static_cast<float>(r - firstRow) * kRowH;
            if (y >= area.bottom()) break;
            if (static_cast<int>(r) == d_.chosen_) ctx.r.fillRect({area.x, y, area.w, kRowH}, gfx::Color{60, 130, 220, 28});
            else if (r % 2 == 1) ctx.r.fillRect({area.x, y, area.w, kRowH}, c.rowAltBg);
            // Les reperes (defilent).
            ctx.r.pushClip({frozenRight, y, std::max(0.f, area.right() - frozenRight), kRowH});
            float x = x0;
            for (std::size_t col = first; col < cols.size() && x < area.right(); ++col) {
                paintCell(ctx, static_cast<int>(r), col, {x, y, width(col), kRowH}, cols, originalName);
                x += width(col);
            }
            ctx.r.popClip();
            // # et le nom (figes).
            ctx.r.fillRect({area.x, y, kNumW + width(0), kRowH}, static_cast<int>(r) == d_.chosen_ ? gfx::Color{60, 130, 220, 40} : c.panelBg);
            cellText(area.x, y, kNumW, r == 0 ? std::string("1 orig.") : std::to_string(r + 1), kSmall, r == 0 ? c.textMuted : c.text);
            paintCell(ctx, static_cast<int>(r), 0, {area.x + kNumW, y, width(0), kRowH}, cols, originalName);
            ctx.r.fillRect({frozenRight - 1.f, y, 1.f, kRowH}, c.borderStrong);
        }
        ctx.r.popClip();
        ctx.r.popClip();
        vbar_.paint(ctx, vTrack(), contentH(), area.h, sy_);
        hbar_.paint(ctx, hTrack(), contentW(), std::max(0.f, area.right() - frozenRight), sx_);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        measure();
        const gfx::Rect area = body();
        const float frozenRight = area.x + kNumW + width(0);
        if (vbar_.handle(*this, ev, vTrack(), contentH(), area.h, sy_) || hbar_.handle(*this, ev, hTrack(), contentW(), std::max(0.f, area.right() - frozenRight), sx_)) {
            place();
            return ui::EventResult::Consumed;
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && bounds().contains(w->pos)) {
            const bool sideways = w->mods.shift || std::fabs(w->dx) > std::fabs(w->dy);
            if (sideways) sx_ = std::clamp(sx_ - (std::fabs(w->dx) > 0.f ? w->dx : w->dy) * 60.f, 0.f, maxX());
            else sy_ = std::clamp(sy_ - w->dy * 3.f * kRowH, 0.f, maxY());
            place();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev); m && m->button == ui::MouseButton::Left && area.contains(m->pos)) {
            if (editor && editor->visible() && editor->bounds().contains(m->pos)) return ui::EventResult::Ignored;
            int row = -1, col = -1;
            if (!hit(m->pos, row, col)) return ui::EventResult::Ignored;
            commit();
            open(row, col);
            return ui::EventResult::Consumed;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            const bool editing = editRow_ >= 0;
            if (editing && k->key == ui::Key::Return && !k->mods.ctrl) {
                const int r = editRow_, c = editCol_;
                commit();
                if (r + 1 < static_cast<int>(d_.plan_.rows.size())) open(r + 1, c);
                return ui::EventResult::Consumed;
            }
            if (editing && k->key == ui::Key::Escape) {
                cancel();
                return ui::EventResult::Consumed;
            }
            if (!editing && k->key == ui::Key::V && k->mods.ctrl && selCol_ >= 0) {
                const auto values = dup::pastedList(ui::clipboardText());
                if (values.empty()) return ui::EventResult::Consumed;
                const int last = selRow_ + static_cast<int>(values.size()) - 1;
                if (last >= static_cast<int>(d_.plan_.rows.size())) d_.setCopies(last);
                for (std::size_t i = 0; i < values.size(); ++i) d_.writeCell(selRow_ + static_cast<int>(i), selCol_, values[i]);
                d_.refresh();
                return ui::EventResult::Consumed;
            }
        }
        return ui::EventResult::Ignored;
    }
private:
    static constexpr float kHeadH = 30.f, kRowH = 30.f, kNumW = 70.f;
    gfx::Rect body() const {
        const auto b = bounds();
        return {b.x, b.y + kHeadH, std::max(0.f, b.w - ui::kScrollBarThickness - 2.f), std::max(0.f, b.h - kHeadH - ui::kScrollBarThickness - 2.f)};
    }
    gfx::Rect vTrack() const {
        const auto b = bounds();
        return {b.right() - ui::kScrollBarThickness, b.y + kHeadH, ui::kScrollBarThickness, std::max(0.f, b.h - kHeadH - ui::kScrollBarThickness)};
    }
    gfx::Rect hTrack() const {
        const auto b = bounds();
        const float fr = b.x + kNumW + width(0);
        return {fr, b.bottom() - ui::kScrollBarThickness, std::max(0.f, b.right() - ui::kScrollBarThickness - fr), ui::kScrollBarThickness};
    }
    // Les largeurs : celle de chaque colonne a la mesure de son en-tete et de ses cases (120 a 420 px), relevee une
    // fois tant que rien ne change (dirty()).
    void measure() {
        const auto cols = d_.columns();
        if (widths_.size() == cols.size()) return;
        widths_.assign(cols.size(), 120.f);
        const gfx::FontId f{16};
        for (std::size_t c = 0; c < cols.size(); ++c) {
            float w = ui::measureWidth(cols[c], f) + 24.f;
            for (std::size_t r = 0; r < d_.plan_.rows.size() && r < 64; ++r) {
                const std::string t = d_.cell(static_cast<int>(r), cols[c]);
                if (!t.empty()) w = std::max(w, ui::measureWidth(t, f) + 24.f);
            }
            widths_[c] = std::clamp(w, 120.f, c == 0 ? 260.f : 420.f);
        }
        total_ = 0.f;
        for (std::size_t c = 1; c < widths_.size(); ++c) total_ += widths_[c];
        sx_ = std::clamp(sx_, 0.f, maxX());
        sy_ = std::clamp(sy_, 0.f, maxY());
    }
    float width(std::size_t c) const { return c < widths_.size() ? widths_[c] : 120.f; }
    float contentH() const { return static_cast<float>(d_.plan_.rows.size()) * kRowH; }
    float contentW() const { return total_; }
    float maxY() const { return std::max(0.f, contentH() - body().h); }
    float maxX() const {
        const auto a = body();
        return std::max(0.f, total_ - std::max(0.f, a.right() - (a.x + kNumW + width(0))));
    }
    bool cellRect(int row, int col, gfx::Rect& out) const {
        const gfx::Rect area = body();
        const float y = area.y + static_cast<float>(row) * kRowH - sy_;
        if (col == 0) {
            out = {area.x + kNumW, y, width(0), kRowH};
            return true;
        }
        float x = area.x + kNumW + width(0) - sx_;
        for (std::size_t c = 1; c < static_cast<std::size_t>(col); ++c) x += width(c);
        out = {x, y, width(static_cast<std::size_t>(col)), kRowH};
        return true;
    }
    bool hit(gfx::Point p, int& row, int& col) const {
        const gfx::Rect area = body();
        row = static_cast<int>((p.y - area.y + sy_) / kRowH);
        if (row < 0 || row >= static_cast<int>(d_.plan_.rows.size())) return false;
        const float frozenRight = area.x + kNumW + width(0);
        if (p.x < area.x + kNumW) return false;
        if (p.x < frozenRight) {
            col = 0;
            return true;
        }
        float x = frozenRight - sx_;
        const auto n = d_.columns().size();
        for (std::size_t c = 1; c < n; ++c) {
            if (p.x >= x && p.x < x + width(c)) {
                col = static_cast<int>(c);
                return true;
            }
            x += width(c);
        }
        return false;
    }
    void open(int row, int col) {
        selRow_ = row;
        selCol_ = col;
        d_.chooseRow(row);
        if (!editor) return;
        // La case amenee en vue.
        const gfx::Rect area = body();
        const float frozenRight = area.x + kNumW + width(0);
        if (static_cast<float>(row) * kRowH < sy_) sy_ = static_cast<float>(row) * kRowH;
        if (static_cast<float>(row + 1) * kRowH > sy_ + area.h) sy_ = std::min(maxY(), static_cast<float>(row + 1) * kRowH - area.h);
        if (col > 0) {
            float left = 0.f;
            for (int c = 1; c < col; ++c) left += width(static_cast<std::size_t>(c));
            const float view = std::max(0.f, area.right() - frozenRight);
            if (left < sx_) sx_ = left;
            if (left + width(static_cast<std::size_t>(col)) > sx_ + view) sx_ = std::min(maxX(), left + width(static_cast<std::size_t>(col)) - view);
        }
        editRow_ = row;
        editCol_ = col;
        const auto cols = d_.columns();
        editor->setText(d_.cell(row, cols[static_cast<std::size_t>(col)]));
        editor->setPlaceholder(col == 0 ? (d_.plan_.rows.empty() ? std::string{} : d_.plan_.rows.front().name) : cols[static_cast<std::size_t>(col)]);
        editor->setAssist(col > 0 ? d_.spec_.assist : ui::InputText::Assist{});
        editor->setVisibility(ui::Visibility::Visible);
        place();
        (void)editor->focusAndSelectAll();
        invalidate();
    }
    void place() {
        if (!editor) return;
        if (editRow_ < 0) {
            editor->setVisibility(ui::Visibility::Collapsed);
            return;
        }
        gfx::Rect r;
        (void)cellRect(editRow_, editCol_, r);
        const gfx::Rect area = body();
        const float frozenRight = area.x + kNumW + width(0);
        const float left = editCol_ == 0 ? r.x : std::max(r.x, frozenRight);
        const float right = std::min(r.right(), area.right());
        const bool shown = r.y >= area.y - 1.f && r.bottom() <= area.bottom() + 1.f && right - left > 20.f;
        editor->setVisibility(shown ? ui::Visibility::Visible : ui::Visibility::Hidden);
        editor->setBounds({left + 2.f, r.y + 1.f, std::max(0.f, right - left - 4.f), kRowH - 2.f});
    }
    void paintCell(const ui::PaintContext& ctx, int r, std::size_t col, gfx::Rect cell, const std::vector<std::string>& cols, const std::string& originalName) {
        ++drawn_;
        const auto& c = ctx.theme.color;
        std::string t = d_.cell(r, cols[col]);
        bool muted = false;
        if (t.empty()) {   // l'AVANT en gris : le nom de l'original, le repere
            t = col == 0 ? originalName : cols[col];
            muted = true;
        }
        const std::string key = col == 0 ? std::string("NOM") : dup::markerKey(d_.scan_.markers[col - 1].name);
        const int level = d_.conflictOf(r, key);
        if (level > 0) {
            const gfx::Color k = level == 2 ? c.error : c.warning;
            ctx.r.strokeRect({cell.x + 2.f, cell.y + 2.f, cell.w - 4.f, cell.h - 4.f}, k, 2.f);
        } else
            ctx.r.strokeRect({cell.x + 2.f, cell.y + 2.f, cell.w - 4.f, cell.h - 4.f}, c.border, 1.f);
        if (editRow_ == r && editCol_ == static_cast<int>(col)) return;
        const std::string s = fitText(t, ctx.theme.font.ui, cell.w - 16.f);
        if (!s.empty()) ctx.r.drawText({cell.x + 8.f, cell.y + (cell.h - ctx.r.lineHeight(ctx.theme.font.ui)) * 0.5f}, s, ctx.theme.font.ui, muted ? c.textMuted : c.text);
    }
    HmiDuplicateDialog& d_;
    ui::EdgeScrollBar   vbar_, hbar_;
    std::vector<float>  widths_;
    float               total_{0.f}, sx_{0.f}, sy_{0.f};
    int                 editRow_{-1}, editCol_{-1}, selRow_{0}, selCol_{-1};
    std::size_t         drawn_{0};
};

// -------------------------------------------------------------- Preview ---
// L'apercu en direct : la vue a l'echelle, ses objets en gris, la selection en
// bleu, les copies en pointilles orange (rouge : hors de la vue ; ambre : sur
// un autre objet).
class HmiDuplicateDialog::Preview final : public ui::Widget {
public:
    explicit Preview(HmiDuplicateDialog& d) : d_(d) {}
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        const auto* v = d_.view();
        if (!v || v->width <= 0 || v->height <= 0) return;
        const float s = std::min((b.w - 16.f) / static_cast<float>(v->width), (b.h - 16.f) / static_cast<float>(v->height));
        const float ox = b.x + (b.w - s * static_cast<float>(v->width)) * 0.5f, oy = b.y + 8.f;
        const auto map = [&](const hmi::Box& x) {
            return gfx::Rect{ox + s * static_cast<float>(x.x), oy + s * static_cast<float>(x.y), std::max(2.f, s * static_cast<float>(x.w)), std::max(2.f, s * static_cast<float>(x.h))};
        };
        ctx.r.fillRect({ox, oy, s * static_cast<float>(v->width), s * static_cast<float>(v->height)}, c.inputBg);
        ctx.r.strokeRect({ox, oy, s * static_cast<float>(v->width), s * static_cast<float>(v->height)}, c.borderStrong, 1.f);
        ctx.r.pushClip(b);
        const std::set<hmi::Id> sel(d_.scan_.units.begin(), d_.scan_.units.end());
        for (const auto& o : v->objects) {
            if (o.parent != hmi::kNoId || sel.count(o.id)) continue;
            ctx.r.fillRect(map(hmi::edit::selectionBounds(*v, {o.id})), gfx::Color{128, 128, 128, 60});
        }
        const auto box = map(d_.scan_.box);
        ctx.r.fillRect(box, gfx::Color{60, 130, 220, 60});
        ctx.r.strokeRect(box, kBlue, 1.5f);
        const int n = d_.plan_.copies();
        const auto offs = dup::offsets(d_.scan_.box, n, d_.plan_.layout);
        const auto out = d_.overflow();
        const auto over = d_.overlap();
        for (int i = 0; i < static_cast<int>(offs.size()); ++i) {
            hmi::Box x = d_.scan_.box;
            x.x += offs[static_cast<std::size_t>(i)].dx;
            x.y += offs[static_cast<std::size_t>(i)].dy;
            const bool isOut = std::find(out.begin(), out.end(), i + 1) != out.end();
            const bool isOver = std::find(over.begin(), over.end(), i + 1) != over.end();
            const auto r = map(x);
            dashed(ctx, r, isOut ? ctx.theme.color.error : isOver ? ctx.theme.color.warning : kOrange);
            if (r.w > 14.f && r.h > 12.f) ctx.r.drawText({r.x + 3.f, r.y + 2.f}, std::to_string(i + 2), kSmall, isOut ? ctx.theme.color.error : kOrange);
        }
        ctx.r.popClip();
    }
private:
    HmiDuplicateDialog& d_;
};

// ----------------------------------------------------------- LayoutPage ---
class HmiDuplicateDialog::LayoutPage final : public ui::Widget {
public:
    explicit LayoutPage(HmiDuplicateDialog& d) : d_(d) {}
    std::vector<ui::RadioButton*> radios;
    Preview*                      preview{nullptr};
protected:
    void onLayout() override {
        const auto b = bounds();
        float x = b.x + 8.f;
        for (auto* r : radios) {
            r->setBounds({x, b.y + 8.f, 130.f, 26.f});
            x += 140.f;
        }
        if (d_.columnsField_) d_.columnsField_->setBounds({x + 70.f, b.y + 8.f, 56.f, 28.f});
        // 1.12.16 : l'espacement en X et en Y, chacun le sien.
        if (d_.spacingField_) d_.spacingField_->setBounds({b.x + 148.f, b.y + 44.f, 64.f, 28.f});
        if (d_.spacingYField_) d_.spacingYField_->setBounds({b.x + 364.f, b.y + 44.f, 64.f, 28.f});
        if (d_.fit_) d_.fit_->setBounds({b.x + 444.f, b.y + 44.f, 190.f, 28.f});
        if (preview) preview->setBounds({b.x + 4.f, b.y + 84.f + 3.f * 20.f, b.w - 8.f, std::max(60.f, b.h - 84.f - 3.f * 20.f - 4.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& c = ctx.theme.color;
        if (d_.columnsField_) {
            const auto r = d_.columnsField_->bounds();
            ctx.r.drawText({r.x - 66.f, r.y + 6.f}, "Colonnes", kSmall, d_.plan_.layout.axis == dup::Axis::Grid ? c.text : c.textMuted);
        }
        const auto& lay = d_.plan_.layout;
        const bool useX = lay.axis != dup::Axis::Y, useY = lay.axis != dup::Axis::X;
        ctx.r.drawText({b.x + 8.f, b.y + 50.f}, "Espacement X (px)", ctx.theme.font.uiBold, useX ? c.text : c.textMuted);
        ctx.r.drawText({b.x + 224.f, b.y + 50.f}, "Espacement Y (px)", ctx.theme.font.uiBold, useY ? c.text : c.textMuted);
        float y = b.y + 84.f;
        const auto px = [](double v) { return std::to_string(static_cast<long>(std::lround(v))); };
        const auto& box = d_.scan_.box;
        std::string stepText;
        if (lay.axis == dup::Axis::X) stepText = px(box.w + lay.gapX()) + " px (largeur " + px(box.w) + " + espacement X " + px(lay.gapX()) + ")";
        else if (lay.axis == dup::Axis::Y) stepText = px(box.h + lay.gapY()) + " px (hauteur " + px(box.h) + " + espacement Y " + px(lay.gapY()) + ")";
        else stepText = px(box.w + lay.gapX()) + " px en X (largeur " + px(box.w) + " + " + px(lay.gapX()) + "), " + px(box.h + lay.gapY())
                      + " px en Y (hauteur " + px(box.h) + " + " + px(lay.gapY()) + ")";
        ctx.r.drawText({b.x + 8.f, y}, "Pas mesur\xC3\xA9 : " + stepText, kSmall, c.textMuted);
        y += 20.f;
        const auto out = d_.overflow();
        if (out.empty()) ctx.r.drawText({b.x + 8.f, y}, "Toutes les copies tiennent dans la vue.", kSmall, c.textMuted);
        else ctx.r.drawText({b.x + 8.f, y}, plural(out.size(), "copie sort", "copies sortent") + " de la vue (en rouge) : \xC2\xAB Ajuster l'espacement \xC2\xBB ou une grille.", kSmall, ctx.theme.color.error);
        y += 20.f;
        const auto over = d_.overlap();
        if (!over.empty()) ctx.r.drawText({b.x + 8.f, y}, plural(over.size(), "copie chevauche", "copies chevauchent") + " un autre objet de la vue (en ambre).", kSmall, ctx.theme.color.warning);
    }
private:
    HmiDuplicateDialog& d_;
};

// ----------------------------------------------------------------- Body ---
class HmiDuplicateDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiDuplicateDialog& d) : d_(d) {}
    std::vector<ui::Button*> buttons;
    gfx::Rect panel{};
protected:
    void onLayout() override {
        const auto r = bounds();
        // 1.50.0 (planche 04) : la fenetre s'agrandit (et se garde : DialogFrame) - la liste, l'apercu, les conflits.
        const float w = std::min(1500.f, r.w - 40.f), h = std::min(940.f, r.h - 40.f);
        panel = ui::dialogframe::place(*this, {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h});
        if (d_.tabs_) d_.tabs_->setBounds({panel.x + 8.f, panel.y + 40.f, panel.w - 16.f, panel.h - 40.f - 56.f});
        float bx = panel.x + panel.w - 16.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(110.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.y + panel.h - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        noteW_ = bx - panel.x - 24.f;
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        const std::string title = fitText(d_.title(), ctx.theme.font.uiBold, panel.w - 24.f - ui::dialogframe::kTitleRightKeep);
        if (!title.empty()) ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, title, ctx.theme.font.uiBold, c.text);
        // Pourquoi "Dupliquer" est gris (ou ce qu'il va faire) : sur une ligne, raccourcie s'il le faut (l'infobulle
        // du bouton et les conflits disent tout).
        const std::string note = fitText(d_.blockingText(), kSmall, noteW_);
        if (!note.empty())
            ctx.r.drawText({panel.x + 16.f, panel.y + panel.h - 37.f}, note, kSmall, d_.canConfirm() ? c.textMuted : ctx.theme.color.error);
    }
private:
    HmiDuplicateDialog& d_;
    float               noteW_{200.f};
};

// ===================================================================== ======
HmiDuplicateDialog::HmiDuplicateDialog(Spec spec) : menu::WidgetMenu("dialog.hmiDuplicate"), spec_(std::move(spec)) {
    const auto* v = view();
    if (!v) return;
    // 1.11.2 (SYM, decision 240 ; la capture du client « Value[0] n'existe pas ») : dans l'editeur
    // d'un symbole, ses parametres sont des chemins qui existent (Name, Value, Value[0]..Value[9],
    // les membres d'un parametre structure) ; leurs bornes sont celles du parametre. Hors d'un
    // symbole, `exists` et les bornes restent ce qu'ils etaient.
    if (hmi::isSymbolView(*v)) {
        spec_.exists = dup::symbolParamExists(*v, std::move(spec_.exists), symbolTypeMembers(spec_.doc));
        for (const auto& name : dup::symbolParamNames(*v)) candidates_.push_back(name);
    }
    // 1.50.0 : 500 reperes x 30 copies - chaque chemin ne se verifie qu'une fois (la fenetre est modale : le
    // programme ne change pas pendant qu'elle est ouverte).
    if (spec_.exists) {
        auto memo = std::make_shared<std::map<std::string, bool, std::less<>>>();
        spec_.exists = [inner = std::move(spec_.exists), memo](std::string_view path) {
            if (const auto it = memo->find(path); it != memo->end()) return it->second;
            const bool found = inner(path);
            memo->emplace(std::string(path), found);
            return found;
        };
    }
    bounds_ = boundsOf(spec_.doc, v);
    scan_ = dup::scan(*v, spec_.selection, bounds_);   // 1.11.2 (D13) : API.V aussi ; (SYM) : les parametres
    plan_ = dup::defaultPlan(*v, scan_, 0);            // la selection, la pose ; les lignes : les regles (plus bas)
    // 1.50.0 (planche 04) : les groupes, les regles d'office (une suite : celles d'avant), les lignes qui en sortent.
    std::string originalName;
    if (!scan_.units.empty())
        if (const auto* o = v->object(scan_.units.front())) originalName = o->name;
    model_ = dup::modelOf(scan_, originalName, bounds_);
    dup::Counters counters;
    counters.copies = std::clamp(spec_.copies, 0, dup::kMaxCopies);   // 1.10.4 : 0 - "Remplacer..." (l'original seulement)
    rules_ = dup::defaultRules(scan_, model_, counters);
    addressOwners_ = dup::projectAddresses(spec_.doc->project);
    recompute(false);
    for (const auto& var : spec_.doc->project.programs.variables) candidates_.push_back(var.name);
    for (const auto& o : v->objects) candidates_.push_back(o.name);
    chosen_ = plan_.rows.size() > 1 ? 1 : 0;
    // 1.10.4 (client-dupliquer.png) : l'objet suit V[0] ailleurs et son repere de
    // type variable est vide - la colonne se preremplit avec V[0], V[1]... ; la
    // note le dit, et "Vider" la remet a blanc.
    if ((prefilled_ = dup::prefill(scan_, plan_))) {
        const auto values = dup::arrayColumn(prefilled_->array, prefilled_->from, plan_.rows.size());
        const std::string key = dup::markerKey(prefilled_->marker);
        for (std::size_t r = 0; r < values.size() && r < plan_.rows.size(); ++r) plan_.rows[r].markers[key] = values[r];
        if (const int c = columnIndex(prefilled_->marker); c > 0) markCells(static_cast<std::size_t>(c - 1), "Tableau " + prefilled_->array);
        const std::string first = prefilled_->array + "[" + std::to_string(prefilled_->from) + "]";
        note_ = markerLabel(prefilled_->marker) + " est pr\xC3\xA9rempli avec " + first + ", " + prefilled_->array + "["
              + std::to_string(prefilled_->from + 1) + "]\xE2\x80\xA6 : l'objet suit " + first
              + " ailleurs. \xC2\xAB Vider \xC2\xBB le remet \xC3\xA0 blanc, \xC2\xAB Tableau\xE2\x80\xA6 \xC2\xBB en choisit un autre.";
    } else {
        // 1.11 (REP) : un repere a indices ($V[1].Ouv$) est prerempli par son morceau, ses
        // indices decales du numero de la copie ; les copies gardent ses $.
        for (const auto& m : scan_.markers) {
            if (!dup::hasIndices(m.name)) continue;
            note_ = markerLabel(m.name) + " est pr\xC3\xA9rempli : copie 1 " + dup::shiftIndices(m.name, 1) + ", copie 2 "
                  + dup::shiftIndices(m.name, 2) + "\xE2\x80\xA6 Les copies gardent les $ ; ce qui n'est pas entre $ ne change pas";
            // 1.11 (REP-4) : la case "Suivre V[0]" (decochee ici) fait varier aussi l'indice hors repere.
            note_ += scan_.indices.empty() ? std::string(".")
                                           : " (\xC2\xAB Suivre " + scan_.indices.front().label() + " \xC2\xBB le fait varier).";
            break;
        }
    }
    validate();
    buildList();
}

// 1.50.0 : les lignes d'apres les regles (hmi::dup::computeRows).
void HmiDuplicateDialog::recompute(bool countersChanged) {
    const auto* v = view();
    if (!v) return;
    const long long places = std::clamp<long long>(rules_.counters.places(), 1, dup::kMaxCopies + 1);
    std::vector<std::string> names;
    if (const auto* ng = model_.group("N"); ng && (rules_.rule(*ng).kind == dup::RuleKind::Auto || rules_.namesByHand))
        names = dup::seriesNames(*v, scan_, static_cast<int>(places - 1));
    ruleKeep_.clear();
    ruleErrors_.clear();
    dup::computeRows(scan_, model_, rules_, names, plan_, &ruleKeep_, &ruleErrors_);
    if (countersChanged) {
        // Comme avant (setCopies) : une case vide d'un repere a ses cases se remplit - la colonne preremplie (V[i]),
        // un morceau a indices (decale) ; un nom tape vide prend la serie.
        for (std::size_t mi = 0; mi < scan_.markers.size(); ++mi) {
            const std::string key = dup::markerKey(scan_.markers[mi].name);
            if (rules_.ownOf(key).kind != dup::OwnKind::Cells) continue;
            const bool pre = prefilled_ && dup::markerKey(prefilled_->marker) == key;
            for (std::size_t r = 1; r < plan_.rows.size(); ++r) {
                auto& cellValue = plan_.rows[r].markers[key];
                if (cellValue.empty() && pre)
                    cellValue = prefilled_->array + "[" + std::to_string(prefilled_->from + static_cast<long long>(r)) + "]" + prefilled_->rest;
                else if (cellValue.empty() && model_.markers[mi].kind == dup::GroupKind::Index)
                    cellValue = dup::ruleValue(scan_, model_, rules_, mi, static_cast<long long>(r));
                if (cellValue.empty()) plan_.rows[r].markers.erase(key);
            }
        }
        if (rules_.namesByHand)
            for (std::size_t r = 1; r < plan_.rows.size() && r < names.size(); ++r)
                if (plan_.rows[r].name.empty()) plan_.rows[r].name = names[r];
    }
    // 1.11 (REP-4) : la case « Suivre » d'un indice hors repere, c'est la regle de sa profondeur.
    followed_.assign(scan_.indices.size(), false);
    for (std::size_t i = 0; i < scan_.indices.size() && i < model_.indexGroup.size(); ++i)
        followed_[i] = rules_.rule(model_.groups[model_.indexGroup[i]]).kind == dup::RuleKind::Formula;
    chosen_ = std::clamp(chosen_, 0, std::max(0, plan_.copies()));
}

void HmiDuplicateDialog::setCounters(const dup::Counters& c) {
    const bool modeChanged = c.mode != rules_.counters.mode;
    if (modeChanged) {
        // Les profondeurs d'indice prennent les regles d'office de la nouvelle sorte (i, j, k ; o + n) ; les adresses,
        // les textes, les noms et les reperes a part gardent les leurs.
        const auto fresh = dup::defaultRules(scan_, model_, c);
        for (const auto& g : model_.groups)
            if (g.kind == dup::GroupKind::Index) rules_.groups[g.key] = fresh.rule(g);
    }
    rules_.counters = c;
    recompute(true);
    syncing_ = true;
    if (copiesField_ && c.mode == dup::CountMode::Series && copiesField_->text() != std::to_string(c.copies)) copiesField_->setText(std::to_string(c.copies));
    for (std::size_t q = 0; q < rangeFields_.size() / 2 && q < c.grid.size(); ++q) {
        const std::string a = std::to_string(c.grid[q].from), b = std::to_string(c.grid[q].to);
        if (rangeFields_[q * 2]->text() != a) rangeFields_[q * 2]->setText(a);
        if (rangeFields_[q * 2 + 1]->text() != b) rangeFields_[q * 2 + 1]->setText(b);
    }
    syncing_ = false;
    rebuildGrid();
    refresh();
}

void HmiDuplicateDialog::setGrid(std::vector<dup::Range> ranges) {
    if (ranges.empty()) ranges.push_back({0, 1});
    if (ranges.size() > 3) ranges.resize(3);
    dup::Counters c = rules_.counters;
    c.mode = dup::CountMode::Grid;
    c.grid = std::move(ranges);
    setCounters(c);
}

void HmiDuplicateDialog::prefillFromBounds() {
    int n = rules_.counters.mode == dup::CountMode::Grid ? static_cast<int>(rules_.counters.grid.size()) : 0;
    if (n == 0) {
        // Les profondeurs qui existent (une, deux) : i, et j s'il y en a une deuxieme.
        for (const auto& g : model_.groups)
            if (g.kind == dup::GroupKind::Index && g.marked) n = std::max(n, std::min(2, g.depth));
        if (n == 0)
            for (const auto& g : model_.groups)
                if (g.kind == dup::GroupKind::Index) n = std::max(n, std::min(2, g.depth));
        n = std::max(1, n);
    }
    setGrid(dup::gridFromBounds(model_, n));
}

void HmiDuplicateDialog::setCounterK(bool on) {
    dup::Counters c = rules_.counters;
    if (c.mode != dup::CountMode::Grid) return;
    if (on && c.grid.size() < 3) {
        while (c.grid.size() < 2) c.grid.push_back({0, 1});
        c.grid.push_back(dup::gridFromBounds(model_, 3).back());
        for (const auto& g : model_.groups)
            if (g.kind == dup::GroupKind::Index && g.depth == 3 && (g.marked || dup::followByDefault(scan_))) rules_.groups[g.key] = {dup::RuleKind::Formula, "k"};
    } else if (!on && c.grid.size() == 3) {
        c.grid.pop_back();
        for (const auto& g : model_.groups)
            if (g.kind == dup::GroupKind::Index && rules_.rule(g).kind == dup::RuleKind::Formula && rules_.rule(g).formula == "k") rules_.groups[g.key] = {};
    } else
        return;
    setCounters(c);
}

const dup::Group* HmiDuplicateDialog::findGroup(std::string_view ref) const {
    // La cle, le libelle ou le dernier morceau, sans la casse, sans blancs, sans le point du milieu.
    const auto norm = [](std::string_view s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s.compare(i, 2, "\xC2\xB7") == 0) { ++i; continue; }
            const char c = s[i];
            if (c == ' ' || c == '*' || c == '.' || c == '$') continue;
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return out;
    };
    const std::string want = norm(ref);
    for (const auto& g : model_.groups)
        if (g.key == ref) return &g;
    for (const auto& g : model_.groups)
        if (norm(g.label) == want || norm(g.tail) == want || norm(bare(g.tail)) == want) return &g;
    return nullptr;
}

bool HmiDuplicateDialog::setGroupRule(std::string_view group, dup::Rule rule) {
    const auto* g = findGroup(group);
    if (!g) return false;
    rules_.groups[g->key] = std::move(rule);
    if (g->kind == dup::GroupKind::Name) rules_.namesByHand = false;   // une regle pour les noms : la colonne Nom la suit
    recompute(false);
    rebuildGrid();
    refresh();
    return true;
}

bool HmiDuplicateDialog::setVaries(std::string_view group, bool varies) {
    const auto* g = findGroup(group);
    if (!g) return false;
    const auto& now = rules_.rule(*g);
    if (!varies) return setGroupRule(g->key, {dup::RuleKind::Fixed, {}});
    if (now.kind == dup::RuleKind::Formula || now.kind == dup::RuleKind::Auto) return true;
    dup::Rule r{dup::RuleKind::Formula, {}};
    switch (g->kind) {
    case dup::GroupKind::Index: {
        const auto fresh = dup::defaultRules(scan_, model_, rules_.counters).rule(*g);
        r.formula = fresh.kind == dup::RuleKind::Formula ? fresh.formula : "o + n";
        break;
    }
    case dup::GroupKind::Address: r = dup::defaultRules(scan_, model_, rules_.counters).rule(*g); break;
    case dup::GroupKind::Name: r = {dup::RuleKind::Auto, {}}; break;
    default: {
        // Un texte : l'original, son dernier nombre suivi ("Depart 1.1" -> 'Depart 1.{1 + n}') ; sans nombre : {n} au bout.
        std::string t = g->original;
        std::size_t e = t.size();
        while (e > 0 && !std::isdigit(static_cast<unsigned char>(t[e - 1]))) --e;
        std::size_t s = e;
        while (s > 0 && std::isdigit(static_cast<unsigned char>(t[s - 1]))) --s;
        if (e > s && e - s < 10) t = t.substr(0, s) + "{" + t.substr(s, e - s) + " + n}" + t.substr(e);
        else t += " {n}";
        r.formula = "'" + t + "'";
        break;
    }
    }
    return setGroupRule(g->key, r);
}

bool HmiDuplicateDialog::setOwnRule(std::string_view marker, dup::Own own) {
    const int c = columnIndex(marker);
    if (c <= 0) return false;
    const std::string key = dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name);
    if (own.kind != dup::OwnKind::Cells && prefilled_ && dup::markerKey(prefilled_->marker) == key) {
        prefilled_.reset();
        note_.clear();
    }
    if (own.kind == dup::OwnKind::Group) rules_.own.erase(key);
    else rules_.own[key] = std::move(own);
    recompute(false);
    rebuildGrid();
    refresh();
    return true;
}

bool HmiDuplicateDialog::resetOwn(std::string_view marker) { return setOwnRule(marker, dup::Own{}); }

bool HmiDuplicateDialog::setOwnFormula(std::string_view marker, std::string formula, std::string_view group) {
    const int c = columnIndex(marker);
    if (c <= 0) return false;
    const auto& mm = model_.markers[static_cast<std::size_t>(c - 1)];
    dup::Own own{dup::OwnKind::Formula, std::move(formula), {}, "\xC3\xA0 part"};
    if (mm.kind == dup::GroupKind::Index) {
        own.group = model_.groups[mm.group].key;
        if (!group.empty()) {
            const auto* g = findGroup(group);
            if (!g) return false;
            own.group = g->key;
        }
    }
    return setOwnRule(marker, std::move(own));
}

void HmiDuplicateDialog::markCells(std::size_t marker, std::string how) {
    if (marker >= scan_.markers.size()) return;
    rules_.own[dup::markerKey(scan_.markers[marker].name)] = dup::Own{dup::OwnKind::Cells, {}, {}, std::move(how)};
}

// 1.10.4 : les bornes des tableaux (V[64]) ; 1.11 (R111) : puis les membres.
void HmiDuplicateDialog::validate() {
    const auto* v = view();
    if (!v) return;
    dup::ValidateOptions opt;
    opt.exists = spec_.exists;             // 1.11.2 (SYM) : dans un symbole, enveloppe de ses parametres (le constructeur)
    opt.bounds = bounds_;                  // 1.10.4 : V[64] hors des bornes ; 1.11.2 (D13) : API.V[64] aussi ; (SYM) : Value[10]
    opt.candidates = candidates_;
    opt.keepColumns = keep_;
    // 1.50.0 : les reperes que les regles gardent (fixe, Garder) : une case vide ne bloque pas.
    opt.keepColumns.insert(opt.keepColumns.end(), ruleKeep_.begin(), ruleKeep_.end());
    check_ = dup::validate(*v, scan_, plan_, opt);
    // 1.11 (R111) : une case rouge qui vise un membre (Armoires[0].sortie.V8) :
    // "veux-tu dire Armoires[0].sorties.V8 ?", comme pour une variable.
    static constexpr std::string_view kMissing = " n'existe pas";
    for (auto& c : check_.cells) {
        if (c.state != dup::CellState::Unknown || !c.suggestions.empty()) continue;
        const std::string value = cell(c.row, c.column);
        if (value.find('.') == std::string::npos || c.message != value + std::string(kMissing)) continue;
        c.suggestions = memberSuggestions(value, spec_.exists, spec_.assist, candidates_);
    }
    // 1.50.0 (reponse 4 du 12/11) : une adresse deja prise avertit (comme « Modifier l'adresse... »).
    warnings_ = dup::addressWarnings(scan_, model_, plan_, addressOwners_);
    // Les cases a signaler : (ligne, repere) -> 1 avertit, 2 bloque (le tableau, l'apercu, la liste).
    flags_.clear();
    const auto flag = [&](const dup::CellCheck& c) {
        if (c.state == dup::CellState::Empty && !c.blocks) return;   // une case vide gardee : rien a dire
        std::string key;
        if (c.column == "Nom") key = "NOM";
        else if (c.column.size() > 2 && c.column.front() == '$' && c.column.back() == '$') key = dup::markerKey(c.column.substr(1, c.column.size() - 2));
        else key = "IDX:" + c.column;
        int& level = flags_[{c.row, key}];
        level = std::max(level, c.blocks ? 2 : 1);
    };
    for (const auto& c : check_.cells) flag(c);
    for (const auto& c : warnings_) flag(c);
    replaced_ = dup::replacements(scan_, plan_);
    buildConflicts();
}

int HmiDuplicateDialog::conflictOf(int row, const std::string& column) const {
    const auto it = flags_.find({row, column});
    return it == flags_.end() ? 0 : it->second;
}

// Les conflits, regroupes (un repere, une sorte : « lignes 25 a 32 »), les fautes d'abord.
void HmiDuplicateDialog::buildConflicts() {
    conflicts_.clear();
    for (const auto& e : ruleErrors_)
        conflicts_.push_back({true, "R\xC3\xA8gle de " + e.where + " : " + e.why, static_cast<int>(e.place), e.where});
    struct Agg {
        bool        blocks{false};
        std::string column, message;
        int         first{0}, last{0};
        std::size_t n{0};
    };
    std::vector<Agg> aggs;
    std::map<std::string, std::size_t> at;
    const auto add = [&](const dup::CellCheck& c) {
        if (c.state == dup::CellState::Empty && !c.blocks) return;
        const std::string key = c.column + "|" + std::to_string(static_cast<int>(c.state)) + "|" + (c.blocks ? "1" : "0");
        const auto it = at.find(key);
        if (it == at.end()) {
            at.emplace(key, aggs.size());
            std::string msg = c.message;
            if (!c.suggestions.empty()) msg += " ; veux-tu dire " + c.suggestions.front() + " ?";
            aggs.push_back({c.blocks, c.column, std::move(msg), c.row, c.row, 1});
            return;
        }
        auto& a = aggs[it->second];
        a.first = std::min(a.first, c.row);
        a.last = std::max(a.last, c.row);
        ++a.n;
    };
    for (const auto& c : check_.cells) add(c);
    for (const auto& c : warnings_) add(c);
    std::stable_sort(aggs.begin(), aggs.end(), [](const Agg& a, const Agg& b) { return a.blocks > b.blocks; });
    for (const auto& a : aggs) {
        std::string t = a.column + " : " + a.message;
        if (a.n > 1) t += " (et " + std::to_string(a.n - 1) + " autres, lignes " + std::to_string(a.first + 1) + " \xC3\xA0 " + std::to_string(a.last + 1) + ")";
        else t += " (ligne " + std::to_string(a.first + 1) + ")";
        conflicts_.push_back({a.blocks, std::move(t), a.first, a.column});
    }
}

HmiDuplicateDialog::Stats HmiDuplicateDialog::stats() const {
    Stats s;
    s.copies = static_cast<std::size_t>(std::max(0, plan_.copies()));
    s.places = plan_.rows.size();
    s.markers = scan_.markers.size();
    s.replacements = replaced_;
    s.conflicts = conflicts_.size();
    for (const auto& c : conflicts_) s.faults += c.blocks ? 1 : 0;
    return s;
}

std::vector<int> HmiDuplicateDialog::previewRows() const {
    std::vector<int> out;
    const int P = static_cast<int>(plan_.rows.size());
    if (P <= 9 || previewAll_) {
        for (int r = 0; r < P; ++r) out.push_back(r);
        return out;
    }
    for (int r = 0; r < 5; ++r) out.push_back(r);
    out.push_back(-1);
    for (int r = P - 3; r < P; ++r) out.push_back(r);
    return out;
}

void HmiDuplicateDialog::showAllPreview(bool all) {
    previewAll_ = all;
    syncWidgets();
}

std::string HmiDuplicateDialog::ruleText(std::size_t mi) const {
    if (mi >= scan_.markers.size()) return {};
    const auto own = rules_.ownOf(dup::markerKey(scan_.markers[mi].name));
    const auto& mm = model_.markers[mi];
    switch (own.kind) {
    case dup::OwnKind::Formula: {
        std::string t = "\xC3\xA0 part : ";
        if (mm.kind == dup::GroupKind::Index)
            if (const auto* g = model_.group(own.group)) t += bare(g->tail) + " : ";
        return t + own.formula;
    }
    case dup::OwnKind::Cells: return "ses cases : " + own.how;
    case dup::OwnKind::Keep: return "garder le rep\xC3\xA8re";
    default: break;
    }
    const auto& rule = rules_.rule(model_.groups[mm.group]);
    if (mm.kind == dup::GroupKind::Index) return "comme sa profondeur";
    if (rule.kind == dup::RuleKind::Empty) return "\xC3\xA0 remplir (comme son groupe)";
    if (rule.kind == dup::RuleKind::Fixed) return "garder (comme son groupe)";
    return "comme son groupe";
}

std::size_t HmiDuplicateDialog::listCount(ListFilter f) const {
    std::size_t n = 0;
    for (std::size_t mi = 0; mi < scan_.markers.size(); ++mi) {
        const auto& mm = model_.markers[mi];
        const std::string key = dup::markerKey(scan_.markers[mi].name);
        switch (f) {
        case ListFilter::All: ++n; break;
        case ListFilter::Indices: n += mm.kind == dup::GroupKind::Index ? 1 : 0; break;
        case ListFilter::Addresses: n += mm.kind == dup::GroupKind::Address ? 1 : 0; break;
        case ListFilter::Texts: n += mm.kind == dup::GroupKind::Text || mm.kind == dup::GroupKind::Variable ? 1 : 0; break;
        case ListFilter::Own: n += rules_.ownOf(key).kind != dup::OwnKind::Group ? 1 : 0; break;
        case ListFilter::Conflicts: {
            bool any = false;
            for (std::size_t r = 0; r < plan_.rows.size() && !any; ++r) any = conflictOf(static_cast<int>(r), key) > 0;
            n += any ? 1 : 0;
            break;
        }
        }
    }
    return n;
}

// La liste des reperes : groupee (par profondeur, par objet) ou a plat, filtree, cherchee ; un groupe replie ne
// garde que sa ligne. La table ne dessine que les lignes visibles : 500 ou 5 000 reperes, pareil.
void HmiDuplicateDialog::buildList() {
    listRows_.clear();
    std::string needle;
    for (const char c : search_) needle += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto lowerOf = [](const std::string& s) {
        std::string out;
        for (const char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    const std::size_t lastRow = plan_.rows.empty() ? 0 : plan_.rows.size() - 1;
    std::map<hmi::Id, Labels> labels;
    const hmi::View* v = view();
    const hmi::Project* project = spec_.doc ? &spec_.doc->project : nullptr;
    const auto whereOf = [&](const dup::MarkerInfo& m) {
        if (m.spots.empty()) return std::string{};
        const auto& s = m.spots.front();
        auto it = labels.find(s.object);
        if (it == labels.end()) it = labels.emplace(s.object, v ? inspectorLabels(*v, s.object, project) : Labels{}).first;
        std::string w = s.objectName + " \xC2\xB7 " + spotLabel(it->second, s.where);
        if (m.spots.size() > 1) w += " (+" + std::to_string(m.spots.size() - 1) + ")";
        return w;
    };
    const auto keep = [&](std::size_t mi) {
        const auto& mm = model_.markers[mi];
        const std::string key = dup::markerKey(scan_.markers[mi].name);
        switch (filter_) {
        case ListFilter::Indices: if (mm.kind != dup::GroupKind::Index) return false; break;
        case ListFilter::Addresses: if (mm.kind != dup::GroupKind::Address) return false; break;
        case ListFilter::Texts: if (mm.kind != dup::GroupKind::Text && mm.kind != dup::GroupKind::Variable) return false; break;
        case ListFilter::Own: if (rules_.ownOf(key).kind == dup::OwnKind::Group) return false; break;
        case ListFilter::Conflicts: {
            bool any = false;
            for (std::size_t r = 0; r < plan_.rows.size() && !any; ++r) any = conflictOf(static_cast<int>(r), key) > 0;
            if (!any) return false;
            break;
        }
        default: break;
        }
        if (needle.empty()) return true;
        if (lowerOf(scan_.markers[mi].name).find(needle) != std::string::npos) return true;
        for (const auto& s : scan_.markers[mi].spots)
            if (lowerOf(s.objectName).find(needle) != std::string::npos || lowerOf(s.where).find(needle) != std::string::npos) return true;
        return false;
    };
    // La faute de la derniere copie, par repere (relevee une fois : 500 reperes, des milliers de cases).
    std::map<std::string, std::string> lastFault;
    for (const auto& c : check_.cells)
        if (c.blocks && c.row == static_cast<int>(lastRow) && c.column.size() > 2 && c.column.front() == '$')
            lastFault.emplace(dup::markerKey(c.column.substr(1, c.column.size() - 2)), c.message);
    const auto markerRow = [&](std::size_t mi) {
        ListRow r;
        r.key = dup::markerKey(scan_.markers[mi].name);
        r.marker = mi;
        r.text = markerLabel(scan_.markers[mi].name);
        r.where = whereOf(scan_.markers[mi]);
        r.rule = ruleText(mi);
        r.own = rules_.ownOf(r.key).kind != dup::OwnKind::Group;
        if (lastRow > 0) {
            const auto it = plan_.rows[lastRow].markers.find(r.key);
            r.last = it == plan_.rows[lastRow].markers.end() || it->second.empty()
                         ? (std::find(ruleKeep_.begin(), ruleKeep_.end(), r.key) != ruleKeep_.end() ? std::string("(garde le rep\xC3\xA8re)") : std::string("(\xC3\xA0 remplir)"))
                         : it->second;
        }
        for (std::size_t q = 0; q < plan_.rows.size(); ++q) r.conflict = std::max(r.conflict, conflictOf(static_cast<int>(q), r.key));
        if (r.conflict && lastRow > 0)
            if (const auto it = lastFault.find(r.key); it != lastFault.end()) r.last += "  \xE2\x80\x94 " + it->second;
        return r;
    };
    // Les groupes : une cle, un titre, ses reperes.
    struct Bucket {
        std::string key, title, rule, last;
        std::vector<std::size_t> markers;
        std::size_t spots{0};
    };
    std::vector<Bucket> buckets;
    if (grouping_ == Grouping::Depth) {
        for (std::size_t gi = 0; gi < model_.groups.size(); ++gi) {
            const auto& g = model_.groups[gi];
            if (g.kind == dup::GroupKind::Name) continue;
            Bucket b;
            b.key = g.key;
            b.title = g.label + (g.kind == dup::GroupKind::Index && !g.marked ? "  (hors rep\xC3\xA8re)" : "");
            const auto& rule = rules_.rule(g);
            b.rule = rule.kind == dup::RuleKind::Formula ? "varie : " + rule.formula
                   : rule.kind == dup::RuleKind::Empty ? std::string("\xC3\xA0 remplir") : std::string("fixe");
            b.spots = g.spots;
            for (std::size_t mi = 0; mi < scan_.markers.size(); ++mi)
                if (model_.markers[mi].group == gi && keep(mi)) b.markers.push_back(mi);
            if (b.markers.empty() && (g.kind != dup::GroupKind::Index || g.marked || filter_ != ListFilter::All || !needle.empty())) continue;
            buckets.push_back(std::move(b));
        }
    } else if (grouping_ == Grouping::Object) {
        std::map<std::string, std::size_t> at;
        for (std::size_t mi = 0; mi < scan_.markers.size(); ++mi) {
            if (!keep(mi)) continue;
            const auto& m = scan_.markers[mi];
            const std::string obj = m.spots.empty() ? std::string("?") : m.spots.front().objectName;
            auto it = at.find(obj);
            if (it == at.end()) {
                it = at.emplace(obj, buckets.size()).first;
                Bucket b;
                b.key = "O:" + obj;
                b.title = obj;
                buckets.push_back(std::move(b));
            }
            buckets[it->second].markers.push_back(mi);
            buckets[it->second].spots += m.spots.size();
        }
    } else {
        for (std::size_t mi = 0; mi < scan_.markers.size(); ++mi)
            if (keep(mi)) listRows_.push_back(markerRow(mi));
        if (listModel_) listModel_->modelReset->emit();
        return;
    }
    for (const auto& b : buckets) {
        ListRow head;
        head.group = true;
        head.key = b.key;
        head.text = b.title;
        head.count = b.markers.size();
        head.where = plural(b.spots, "endroit", "endroits");
        head.rule = b.rule;
        head.expanded = !collapsed_.count(b.key);
        if (lastRow > 0 && !b.markers.empty()) {
            const auto it = plan_.rows[lastRow].markers.find(dup::markerKey(scan_.markers[b.markers.front()].name));
            if (it != plan_.rows[lastRow].markers.end()) head.last = it->second;
        }
        for (const std::size_t mi : b.markers)
            for (std::size_t q = 0; q < plan_.rows.size(); ++q)
                head.conflict = std::max(head.conflict, conflictOf(static_cast<int>(q), dup::markerKey(scan_.markers[mi].name)));
        listRows_.push_back(std::move(head));
        if (collapsed_.count(b.key)) continue;
        for (const std::size_t mi : b.markers) listRows_.push_back(markerRow(mi));
    }
    if (listModel_) listModel_->modelReset->emit();
}

void HmiDuplicateDialog::setSearch(std::string text) {
    search_ = std::move(text);
    if (searchField_ && searchField_->text() != search_) {
        syncing_ = true;
        searchField_->setText(search_);
        syncing_ = false;
    }
    buildList();
}

void HmiDuplicateDialog::setListFilter(ListFilter f) {
    filter_ = f;
    buildList();
    syncWidgets();
}

void HmiDuplicateDialog::setGrouping(Grouping g) {
    grouping_ = g;
    buildList();
    syncWidgets();
}

void HmiDuplicateDialog::expandAll(bool expanded) {
    collapsed_.clear();
    if (!expanded) {
        for (const auto& g : model_.groups) collapsed_.insert(g.key);
        for (const auto& m : scan_.markers)
            if (!m.spots.empty()) collapsed_.insert("O:" + m.spots.front().objectName);
    }
    buildList();
}

void HmiDuplicateDialog::toggleGroup(std::string_view key) {
    const std::string k(key);
    if (!collapsed_.erase(k)) collapsed_.insert(k);
    buildList();
}

std::vector<std::size_t> HmiDuplicateDialog::selectedMarkers() const {
    std::vector<std::size_t> out;
    if (!listTable_) return out;
    std::set<std::size_t> seen;
    for (const auto row : listTable_->selectedModelRows()) {
        if (row >= listRows_.size()) continue;
        const auto& r = listRows_[row];
        if (!r.group) {
            if (seen.insert(r.marker).second) out.push_back(r.marker);
            continue;
        }
        // Un groupe : tous ses reperes (meme replies).
        for (std::size_t mi = 0; mi < scan_.markers.size(); ++mi) {
            const bool in = grouping_ == Grouping::Depth ? model_.groups[model_.markers[mi].group].key == r.key
                                                         : !scan_.markers[mi].spots.empty() && "O:" + scan_.markers[mi].spots.front().objectName == r.key;
            if (in && seen.insert(mi).second) out.push_back(mi);
        }
    }
    return out;
}

void HmiDuplicateDialog::openOwnEditor(int mode) {
    editing_ = selectedMarkers();
    editMode_ = editing_.empty() ? 0 : mode;
    if (editMode_ && ownField_) {
        syncing_ = true;
        const auto own = rules_.ownOf(dup::markerKey(scan_.markers[editing_.front()].name));
        if (mode == 1) {
            const auto& mm = model_.markers[editing_.front()];
            std::string f = own.kind == dup::OwnKind::Formula ? own.formula : std::string{};
            if (f.empty()) {
                const auto& rule = rules_.rule(model_.groups[mm.group]);
                f = rule.kind == dup::RuleKind::Formula ? rule.formula : mm.kind == dup::GroupKind::Index ? std::string("o + n") : std::string{};
            }
            ownField_->setText(f);
            ownField_->setPlaceholder(mm.kind == dup::GroupKind::Address ? "%MW(1000 + 10 * i + j)" : mm.kind == dup::GroupKind::Index ? "o + n" : "'D\xC3\xA9part {i+1}.{j+1}'");
            if (ownDepth_) {
                std::vector<ui::DropDown::Item> items;
                int pick = 0;
                if (mm.kind == dup::GroupKind::Index)
                    for (const auto& sl : mm.slots) {
                        const auto& g = model_.groups[sl.group];
                        if (std::any_of(items.begin(), items.end(), [&](const ui::DropDown::Item& x) { return x.value == g.key; })) continue;
                        if (g.key == (own.kind == dup::OwnKind::Formula ? own.group : model_.groups[mm.group].key)) pick = static_cast<int>(items.size());
                        items.push_back({g.tail, g.key, {}, true});
                    }
                ownDepth_->setItems(std::move(items));
                ownDepth_->setSelectedIndex(pick);
            }
        } else {
            ownField_->setText("V1{n}");
            ownField_->setPlaceholder("V1{n}");
            if (ownFrom_) ownFrom_->setText("02");
        }
        syncing_ = false;
    }
    if (copiesPage_) copiesPage_->invalidateLayout();
    if (editMode_ && ownField_) (void)ownField_->focusAndSelectAll();
}

// Les tableaux, la bande, les onglets suivent le plan.
void HmiDuplicateDialog::syncWidgets() {
    previewIndex_ = previewRows();
    // Les reperes montres dans l'apercu : un de chaque sorte (le plus suivi des reperes a indices d'abord).
    samples_.clear();
    {
        int best = -1;
        std::size_t most = 0;
        for (std::size_t gi = 0; gi < model_.groups.size(); ++gi) {
            const auto& g = model_.groups[gi];
            if (g.kind != dup::GroupKind::Index || !g.marked) continue;
            std::size_t n = 0;
            for (const auto& mm : model_.markers) n += mm.group == gi ? 1 : 0;
            if (n > most) most = n, best = static_cast<int>(gi);
        }
        if (best >= 0)
            for (std::size_t mi = 0; mi < model_.markers.size(); ++mi)
                if (model_.markers[mi].group == static_cast<std::size_t>(best)) { samples_.push_back(mi); break; }
        for (const auto kind : {dup::GroupKind::Address, dup::GroupKind::Text, dup::GroupKind::Variable})
            for (std::size_t mi = 0; mi < model_.markers.size() && samples_.size() < 3; ++mi)
                if (model_.markers[mi].kind == kind) { samples_.push_back(mi); break; }
    }
    if (depthModel_) depthModel_->modelReset->emit();
    if (previewModel_) {
        previewModel_->modelReset->emit();
        if (previewTable_) {
            const std::size_t n = previewModel_->columnCount();
            std::vector<ui::TableView::Column> cols;
            const std::size_t counters = rules_.counters.mode == dup::CountMode::Series ? 1 : std::min<std::size_t>(3, rules_.counters.grid.size());
            for (std::size_t c = 0; c < n; ++c) {
                ui::TableView::Column col;
                col.title = previewModel_->headerText(c);
                col.sortable = false;
                col.filterable = false;
                col.width = c == 0 ? 70.f : c <= counters ? 38.f : c == counters + 1 ? 130.f : 150.f;
                cols.push_back(std::move(col));
            }
            previewTable_->setColumns(std::move(cols));
        }
    }
    if (conflictModel_) conflictModel_->modelReset->emit();
    if (tabs_ && tabs_->tabCount() > 2)
        tabs_->setTabBadge(2, std::to_string(scan_.markers.size()) + " \xC3\x97 " + std::to_string(plan_.rows.size()));
    // La bande : le mode choisi en accent, les filtres et le groupement choisis.
    for (std::size_t i = 0; i < modeButtons_.size(); ++i)
        modeButtons_[i]->setStyle((i == 0) == (rules_.counters.mode == dup::CountMode::Series) ? ui::Button::Style::Primary : ui::Button::Style::Default);
    static const ListFilter kFilters[] = {ListFilter::All, ListFilter::Indices, ListFilter::Addresses, ListFilter::Texts, ListFilter::Own, ListFilter::Conflicts};
    static const char* kNames[] = {"Tous", "Indices", "Adresses", "Textes", "\xC3\x80 part", "Avec un conflit"};
    for (std::size_t i = 0; i < filterButtons_.size() && i < 6; ++i) {
        const std::string label = std::string(kNames[i]) + "  " + std::to_string(listCount(kFilters[i]));
        if (filterButtons_[i]->text() != label) filterButtons_[i]->setText(label);
        filterButtons_[i]->setStyle(filter_ == kFilters[i] ? ui::Button::Style::Primary : ui::Button::Style::Default);
    }
    for (std::size_t i = 0; i < groupButtons_.size(); ++i)
        groupButtons_[i]->setStyle(static_cast<std::size_t>(grouping_) == i ? ui::Button::Style::Primary : ui::Button::Style::Default);
    if (kButton_) kButton_->setText(rules_.counters.grid.size() >= 3 ? "Retirer k" : "+ un compteur k");
    if (band_) band_->invalidateLayout();
    if (copiesPage_) copiesPage_->invalidateLayout();
    if (sheet_) sheet_->invalidate();
}

void HmiDuplicateDialog::rebuildGrid() {
    if (sheet_) sheet_->dirty();
}

HmiDuplicateDialog::SheetStats HmiDuplicateDialog::sheetStats() const { return sheet_ ? sheet_->stats() : SheetStats{}; }

void HmiDuplicateDialog::scrollSheet(float x, float y) {
    if (sheet_) sheet_->scrollTo(x, y);
}

gfx::Rect HmiDuplicateDialog::panelRect() const { return body_ ? body_->panel : gfx::Rect{}; }

namespace {
// Les morceaux d'un chemin, coupes aux points hors crochets : Armoires[0], sortie, V8.
std::vector<std::string> pathParts(std::string_view path) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (const char ch : path) {
        if (ch == '[') ++depth;
        else if (ch == ']' && depth > 0) --depth;
        if (ch == '.' && depth == 0) {
            out.push_back(std::move(cur));
            cur.clear();
            continue;
        }
        cur += ch;
    }
    out.push_back(std::move(cur));
    return out;
}
// "sorties[2]" : "sorties" et "[2]".
std::pair<std::string, std::string> splitIndex(const std::string& part) {
    const auto b = part.find('[');
    if (b == std::string::npos) return {part, std::string{}};
    return {part.substr(0, b), part.substr(b)};
}
} // namespace

std::vector<std::string> HmiDuplicateDialog::memberSuggestions(std::string_view path, const dup::PathExists& exists,
                                                               const ui::InputText::Assist& assist,
                                                               const std::vector<std::string>& variables) {
    std::vector<std::string> out;
    if (!exists) return out;
    const auto parts = pathParts(path);
    if (parts.size() < 2 || std::any_of(parts.begin(), parts.end(), [](const std::string& p) { return p.empty(); })) return out;
    // Les membres d'un chemin qui existe : ce que l'aide a la saisie propose apres "chemin.".
    const auto membersOf = [&](const std::string& at) {
        std::vector<std::string> names;
        if (!assist) return names;
        std::vector<ui::InputText::Suggestion> list;
        std::size_t from = 0;
        assist(at + ".", from, list);
        for (const auto& s : list)
            if (!s.text.empty()) names.push_back(s.text);
        return names;
    };
    // Le debut : le premier morceau s'il existe, sinon les variables les plus proches.
    std::vector<std::string> paths;
    if (exists(parts[0])) paths.push_back(parts[0]);
    else {
        const auto [base, index] = splitIndex(parts[0]);
        for (const auto& near : dup::didYouMean(base, variables))
            if (exists(near + index)) paths.push_back(near + index);
    }
    // Chaque morceau suivant : tel quel s'il existe, sinon les membres les plus proches.
    for (std::size_t i = 1; i < parts.size() && !paths.empty(); ++i) {
        std::vector<std::string> next;
        for (const auto& at : paths) {
            if (exists(at + "." + parts[i])) {
                next.push_back(at + "." + parts[i]);
                continue;
            }
            const auto [base, index] = splitIndex(parts[i]);
            for (const auto& near : dup::didYouMean(base, membersOf(at)))
                if (exists(at + "." + near + index)) next.push_back(at + "." + near + index);
        }
        if (next.size() > 6) next.resize(6);
        paths = std::move(next);
    }
    for (auto& p : paths)
        if (p != path && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(std::move(p));
    if (out.size() > 3) out.resize(3);
    return out;
}

const hmi::View* HmiDuplicateDialog::view() const {
    return spec_.doc ? spec_.doc->project.view(spec_.view) : nullptr;
}

menu::MenuTraits HmiDuplicateDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = false;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiDuplicateDialog::title() const {
    std::string t = "Dupliquer\xE2\x80\xA6";
    if (const auto* v = view(); v && !scan_.units.empty())
        if (const auto* o = v->object(scan_.units.front()))
            t += "  " + o->name + (scan_.units.size() > 1 ? " (+" + std::to_string(scan_.units.size() - 1) + ")" : std::string{});
    return t;
}

std::vector<std::string> HmiDuplicateDialog::columns() const {
    std::vector<std::string> c{"Nom"};
    for (const auto& m : scan_.markers) c.push_back(markerLabel(m.name));
    return c;
}

// 1.11.1 (R1111-7) : un repere long ($API.Tempon_TOR16_I4_OUT[1]$) debordait sur
// l'en-tete suivant, et ses cases (de largeur egale) montraient la fin coupee a
// gauche. Chaque colonne prend ce que demandent son en-tete et ses cases.
std::vector<float> HmiDuplicateDialog::fitColumns(const std::vector<float>& need, float width, float minimum) {
    const std::size_t n = need.size();
    std::vector<float> out(n, minimum);
    if (n == 0) return out;
    std::vector<float> want(n);
    float sum = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        want[i] = std::max(minimum, need[i]);
        sum += want[i];
    }
    if (sum <= width) {   // tout tient : le reste se partage egalement
        const float extra = (width - sum) / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = want[i] + extra;
        return out;
    }
    if (minimum * static_cast<float>(n) >= width) return out;   // rien ne tient : chacune son minimum
    // Les plus larges cedent : un plafond commun, tel que la somme fasse `width`.
    std::vector<float> sorted = want;
    std::sort(sorted.begin(), sorted.end());
    float rest = width, cap = sorted.back();
    for (std::size_t i = 0; i < n; ++i) {
        const float share = rest / static_cast<float>(n - i);
        if (sorted[i] > share) {
            cap = share;
            break;
        }
        rest -= sorted[i];
    }
    for (std::size_t i = 0; i < n; ++i) out[i] = std::min(want[i], cap);
    return out;
}

namespace {
const gfx::FontId kGridFont{16};   // ui et uiBold : la meme taille (pas de face grasse)
}

std::vector<float> HmiDuplicateDialog::gridColumns(float width) const {
    const auto names = columns();
    std::vector<float> need(names.size(), 0.f);
    for (std::size_t c = 0; c < names.size(); ++c) {
        std::string head = names[c];
        if (c > 0 && c - 1 < scan_.markers.size()) head += scan_.markers[c - 1].variable ? "  variable" : "  texte";
        float w = ui::measureWidth(head, kGridFont) + 16.f;   // l'en-tete, et l'ecart avant la suivante
        const std::string key = c > 0 && c - 1 < scan_.markers.size() ? dup::markerKey(scan_.markers[c - 1].name) : std::string{};
        for (const auto& row : plan_.rows) {
            std::string t;
            if (c == 0) t = row.name;
            else if (const auto it = row.markers.find(key); it != row.markers.end()) t = it->second;
            if (t.empty() && c > 0) t = names[c];   // l'AVANT en gris : le repere
            w = std::max(w, ui::measureWidth(t, kGridFont) + 32.f);   // la case : ses marges, le curseur, l'ecart
        }
        need[c] = w;
    }
    return fitColumns(need, width, 120.f);
}

std::string HmiDuplicateDialog::gridHeader(std::size_t c, float width) const {
    const auto names = columns();
    if (c >= names.size()) return {};
    const std::string& name = names[c];
    if (c > 0 && c - 1 < scan_.markers.size()) {
        const std::string full = name + (scan_.markers[c - 1].variable ? "  variable" : "  texte");
        if (ui::measureWidth(full, kGridFont) <= width) return full;
    }
    if (ui::measureWidth(name, kGridFont) <= width) return name;
    // Coupe au milieu, par caracteres entiers : le debut (la variable) et la fin
    // (l'indice qui varie) restent lisibles.
    std::vector<std::string> glyphs;
    for (std::size_t i = 0; i < name.size();) {
        std::size_t j = i + 1;
        while (j < name.size() && (static_cast<unsigned char>(name[j]) & 0xC0) == 0x80) ++j;
        glyphs.push_back(name.substr(i, j - i));
        i = j;
    }
    for (std::size_t keep = glyphs.size(); keep-- > 0;) {
        std::string s;
        const std::size_t left = (keep + 1) / 2, right = keep / 2;
        for (std::size_t i = 0; i < left; ++i) s += glyphs[i];
        s += "\xE2\x80\xA6";
        for (std::size_t i = glyphs.size() - right; i < glyphs.size(); ++i) s += glyphs[i];
        if (ui::measureWidth(s, kGridFont) <= width) return s;
    }
    return "\xE2\x80\xA6";
}

int HmiDuplicateDialog::columnIndex(std::string_view column) const {
    const auto cols = columns();
    for (std::size_t i = 0; i < cols.size(); ++i)
        if (dup::markerKey(cols[i]) == dup::markerKey(column)) return static_cast<int>(i);
    // "Vanne" sans les $ : la colonne du repere.
    for (std::size_t i = 0; i < scan_.markers.size(); ++i)
        if (dup::markerKey(scan_.markers[i].name) == dup::markerKey(column)) return static_cast<int>(i + 1);
    return -1;
}

std::string HmiDuplicateDialog::cell(int row, std::string_view column) const {
    const int c = columnIndex(column);
    if (row < 0 || static_cast<std::size_t>(row) >= plan_.rows.size() || c < 0) return {};
    const auto& r = plan_.rows[static_cast<std::size_t>(row)];
    if (c == 0) return r.name;
    const auto it = r.markers.find(dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name));
    return it == r.markers.end() ? std::string{} : it->second;
}

// Une case, sans revalider (les remplissages en ecrivent beaucoup, puis revalident une fois). 1.50.0 : la colonne
// Nom tapee ne suit plus de regle ; un repere ecrit a ses cases (« a part » : les Remplir d'avant).
void HmiDuplicateDialog::writeCell(int row, int c, std::string value) {
    if (row < 0 || static_cast<std::size_t>(row) >= plan_.rows.size() || c < 0) return;
    auto& r = plan_.rows[static_cast<std::size_t>(row)];
    if (c == 0) {
        rules_.namesByHand = true;
        r.name = std::move(value);
        return;
    }
    const auto mi = static_cast<std::size_t>(c - 1);
    if (mi >= scan_.markers.size()) return;
    const std::string key = dup::markerKey(scan_.markers[mi].name);
    if (rules_.ownOf(key).kind != dup::OwnKind::Cells) markCells(mi, "\xC3\xA0 la main");
    if (value.empty()) r.markers.erase(key);
    else r.markers[key] = std::move(value);
}

bool HmiDuplicateDialog::setCell(int row, std::string_view column, std::string value) {
    const int c = columnIndex(column);
    if (row < 0 || static_cast<std::size_t>(row) >= plan_.rows.size() || c < 0) return false;
    writeCell(row, c, std::move(value));
    chosen_ = row;
    rebuildGrid();
    refresh();
    return true;
}

void HmiDuplicateDialog::setCopies(int copies) {
    if (!view()) return;
    copies = std::clamp(copies, 0, dup::kMaxCopies);   // 1.10.4 : 0 - les reperes de l'original seulement
    if (rules_.counters.mode == dup::CountMode::Series && copies == plan_.copies()) return;
    dup::Counters c = rules_.counters;
    c.mode = dup::CountMode::Series;
    c.copies = copies;
    setCounters(c);
}

bool HmiDuplicateDialog::fillSeries(std::string_view column, std::string_view pattern, std::string_view from, bool fromOriginal) {
    const int c = columnIndex(column);
    if (c < 0 || pattern.empty()) return false;
    // 1.10.4 : la colonne preremplie, remplie autrement : la note s'en va.
    if (c > 0 && prefilled_ && dup::markerKey(prefilled_->marker) == dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name)) {
        prefilled_.reset();
        note_.clear();
    }
    const std::size_t first = fromOriginal ? 0 : 1;
    const auto values = dup::seriesN(pattern, from.empty() ? std::string_view("1") : from, plan_.rows.size() - first);
    for (std::size_t i = 0; i < values.size(); ++i) writeCell(static_cast<int>(first + i), c, values[i]);
    if (c > 0) markCells(static_cast<std::size_t>(c - 1), "S\xC3\xA9rie " + std::string(pattern));
    rebuildGrid();
    refresh();
    return !values.empty();
}

bool HmiDuplicateDialog::fillList(std::string_view column, std::string_view pasted, bool fromOriginal) {
    const int c = columnIndex(column);
    if (c < 0) return false;
    // 1.10.4 : la colonne preremplie, remplie autrement : la note s'en va.
    if (c > 0 && prefilled_ && dup::markerKey(prefilled_->marker) == dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name)) {
        prefilled_.reset();
        note_.clear();
    }
    const auto values = dup::pastedList(pasted);
    if (values.empty()) return false;
    const std::size_t first = fromOriginal ? 0 : 1;
    // Le nombre de copies suit la liste.
    setCopies(static_cast<int>(values.size() + first) - 1);
    for (std::size_t i = 0; i < values.size(); ++i) writeCell(static_cast<int>(first + i), c, values[i]);
    if (c > 0) markCells(static_cast<std::size_t>(c - 1), "Coller (Excel)");
    rebuildGrid();
    refresh();
    return true;
}

std::vector<std::string> HmiDuplicateDialog::arrays() const {
    std::vector<std::string> out;
    if (!spec_.doc) return out;
    // 1.11.2 (SYM) : dans un symbole, ses parametres tableaux en tete (Value (0..9)).
    if (const auto* v = view(); v && hmi::isSymbolView(*v))
        for (const auto& [name, b] : dup::symbolParamArrays(*v))
            out.push_back(name + " (" + std::to_string(b.first) + ".." + std::to_string(b.second) + ")");
    for (const auto& [name, b] : dup::projectArrays(spec_.doc->project))
        out.push_back(name + " (" + std::to_string(b.first) + ".." + std::to_string(b.second) + ")");
    return out;
}

bool HmiDuplicateDialog::fillArray(std::string_view column, std::string_view array) {
    const int c = columnIndex(column);
    if (c <= 0 || array.empty() || !spec_.doc) return false;      // pas la colonne "Nom"
    // "V (0..63)" (la liste) ou "V".
    std::string name(array.substr(0, array.find(" (")));
    // Depuis l'indice de l'original : celui que l'objet suit, sinon la borne basse.
    long long from = 0;
    bool known = false;
    // 1.11 (REP) : un repere qui est un element de ce tableau ($V[1].Ouv$) : depuis son
    // indice, et sa suite (.Ouv) gardee.
    std::string rest;
    if (const auto e = dup::elementRef(scan_.markers[static_cast<std::size_t>(c - 1)].name);
        e && dup::markerKey(e->array) == dup::markerKey(name)) {
        from = e->value;
        rest = e->rest;
        known = true;
    }
    if (!known)
        for (const auto& k : scan_.indices)
            if (dup::markerKey(k.path) == dup::markerKey(name)) { from = k.value; known = true; break; }
    const auto* v = view();
    const auto bounds = bounds_ ? bounds_(name) : std::nullopt;     // 1.11.2 (D13) : API.V (0..63) aussi ; (SYM) : Value (0..9)
    if (!known && bounds) from = bounds->first;
    if (!known && !bounds && !(spec_.exists && spec_.exists(name))) return false;   // pas un tableau connu
    if (bounds) {
        bool written = false;
        // 1.11.2 (SYM) : un parametre du symbole s'ecrit comme dans le symbole.
        if (v && hmi::isSymbolView(*v))
            for (const auto& [n, b] : dup::symbolParamArrays(*v))
                if (dup::markerKey(n) == dup::markerKey(name)) { name = n; written = true; break; }
        if (!written)
            for (const auto& [n, b] : dup::projectArrays(spec_.doc->project))
                if (dup::markerKey(n) == dup::markerKey(name)) name = n;       // ecrit comme dans le projet
    }
    const auto values = dup::arrayColumn(name, from, plan_.rows.size());
    for (std::size_t i = 0; i < values.size(); ++i) writeCell(static_cast<int>(i), c, values[i] + rest);
    markCells(static_cast<std::size_t>(c - 1), "Tableau " + name);
    prefilled_ = dup::Prefill{scan_.markers[static_cast<std::size_t>(c - 1)].name, name, from, rest};
    note_.clear();
    rebuildGrid();
    refresh();
    return true;
}

bool HmiDuplicateDialog::clearColumn(std::string_view column) {
    const int c = columnIndex(column);
    if (c < 0) return false;
    // 1.10.4 : la colonne preremplie se vide entiere (l'original compris), et sa note s'en va.
    const bool prefilledColumn = c > 0 && prefilled_ && dup::markerKey(prefilled_->marker) == dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name);
    if (prefilledColumn) {
        prefilled_.reset();
        note_.clear();
        if (!plan_.rows.empty()) writeCell(0, c, {});
    }
    for (std::size_t r = 1; r < plan_.rows.size(); ++r) writeCell(static_cast<int>(r), c, {});
    if (c > 0) markCells(static_cast<std::size_t>(c - 1), "Vider");
    rebuildGrid();
    refresh();
    return true;
}

void HmiDuplicateDialog::setKeep(std::string_view marker, bool keep) {
    const int c = columnIndex(marker);
    if (c <= 0) return;
    const std::string key = dup::markerKey(scan_.markers[static_cast<std::size_t>(c - 1)].name);
    keep_.erase(std::remove(keep_.begin(), keep_.end(), key), keep_.end());
    if (keep) keep_.push_back(key);
    refresh();
}

// 1.50.0 : « Suivre V[0] » d'avant, c'est la regle de sa profondeur : varie (o + n ; une grille : i, j, k) ou fixe.
void HmiDuplicateDialog::setFollow(std::size_t index, bool follow) {
    if (index >= scan_.indices.size() || index >= model_.indexGroup.size()) return;
    const auto& g = model_.groups[model_.indexGroup[index]];
    if (!follow) {
        rules_.groups[g.key] = {dup::RuleKind::Fixed, {}};
    } else if (rules_.rule(g).kind != dup::RuleKind::Formula) {
        dup::Rule r{dup::RuleKind::Formula, "o + n"};
        if (rules_.counters.mode == dup::CountMode::Grid && g.depth >= 1 && static_cast<std::size_t>(g.depth) <= rules_.counters.grid.size())
            r.formula = g.depth == 1 ? "i" : g.depth == 2 ? "j" : "k";
        rules_.groups[g.key] = r;
    }
    recompute(false);
    rebuildGrid();
    refresh();
}

void HmiDuplicateDialog::chooseRow(int row) {
    chosen_ = std::clamp(row, 0, std::max(0, plan_.copies()));
    if (copiesPage_) copiesPage_->invalidateLayout();
    if (sheet_) sheet_->invalidate();
}

bool HmiDuplicateDialog::canConfirm() const {
    // 1.10.4 : 0 copie ("Remplacer..." de Compiler) - les reperes de l'original.
    const bool something = plan_.copies() > 0 || (plan_.replaceOriginal && !scan_.markers.empty());
    return !done_ && something && !check_.blocked() && ruleErrors_.empty();
}

std::string HmiDuplicateDialog::confirmLabel() const {
    const int n = plan_.copies();
    if (n == 0) return "Remplacer dans l'original";      // 1.10.4
    return "Dupliquer (" + std::to_string(n) + (n > 1 ? " copies)" : " copie)");
}

std::string HmiDuplicateDialog::blockingText() const {
    // 1.50.0 : les avertissements (une adresse, un nom deja pris) ne bloquent pas ; la note le dit.
    std::size_t warns = 0;
    for (const auto& c : conflicts_) warns += c.blocks ? 0 : 1;
    const std::string warnText = warns == 0 ? std::string{}
                               : warns == 1 ? std::string(" L'avertissement ne bloque pas.") : " Les " + std::to_string(warns) + " avertissements ne bloquent pas.";
    if (!ruleErrors_.empty())
        return "Dupliquer est gris : r\xC3\xA8gle de " + ruleErrors_.front().where + " : " + ruleErrors_.front().why + "." + warnText;
    if (!check_.blocked()) {
        std::size_t reps = 0;
        for (const auto& m : scan_.markers) reps += m.uses;
        if (plan_.copies() == 0)      // 1.10.4 : "Remplacer..." de Compiler
            return plan_.replaceOriginal ? "Aucune copie : " + plural(reps, "rep\xC3\xA8re remplac\xC3\xA9", "rep\xC3\xA8res remplac\xC3\xA9s")
                                               + " dans l'original. Ctrl+Z le rend." + warnText
                                         : std::string("Aucune copie, et l'original garde ses rep\xC3\xA8res : coche \xC2\xAB Remplacer aussi dans l'original \xC2\xBB.");
        return plural(static_cast<std::size_t>(plan_.copies()), "copie", "copies") + ", " + plural(reps, "rep\xC3\xA8re", "rep\xC3\xA8res")
             + " par copie. Ctrl+Z retire tout." + warnText;
    }
    const std::size_t empty = check_.count(dup::CellState::Empty);
    std::size_t bad = 0;
    for (const auto& c : check_.cells) bad += c.blocks && c.state != dup::CellState::Empty ? 1 : 0;
    std::size_t emptyBlocking = 0;
    for (const auto& c : check_.cells) emptyBlocking += c.blocks && c.state == dup::CellState::Empty ? 1 : 0;
    (void)empty;
    std::string s;
    if (emptyBlocking) s += plural(emptyBlocking, "case vide", "cases vides");
    if (bad) s += (s.empty() ? "" : ", ") + plural(bad, "case en rouge", "cases en rouge");
    for (const auto& c : check_.cells) {
        if (!c.blocks) continue;
        s += " \xE2\x80\x94 ligne " + std::to_string(c.row + 1) + ", " + c.column + " : " + c.message;
        if (!c.suggestions.empty()) s += " ; veux-tu dire " + c.suggestions.front() + " ?";
        break;
    }
    return s + "." + warnText;
}

void HmiDuplicateDialog::refresh() {
    validate();
    buildList();
    if (ok_) {
        const std::string label = confirmLabel();
        if (label != ok_->text()) ok_->setText(label);
        ok_->setEnabled(canConfirm());
    }
    if (fit_) fit_->setEnabled(!overflow().empty());
    syncWidgets();
    if (body_) {
        body_->invalidateLayout();
        body_->invalidate();
    }
    if (layoutPage_) layoutPage_->invalidate();
}

core::Status HmiDuplicateDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = "dialog.hmiDuplicate";
    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    const auto table = [&](ui::Widget& parent, const std::string& id, std::shared_ptr<ui::ITableModel> model, std::vector<std::pair<std::string, float>> cols) {
        auto t = std::make_unique<ui::TableView>(base + "." + id);
        std::vector<ui::TableView::Column> cs;
        for (auto& [title, w] : cols) {
            ui::TableView::Column c;
            c.title = title;
            c.width = w;
            c.sortable = false;
            c.filterable = false;
            cs.push_back(std::move(c));
        }
        t->setColumns(std::move(cs));
        t->setModel(std::move(model));
        t->setAlternatingRowColors(true);
        return &static_cast<ui::TableView&>(parent.addChild(std::move(t)));
    };
    const auto button = [&](ui::Widget& parent, std::string text, const std::string& id) {
        return &static_cast<ui::Button&>(parent.addChild(std::make_unique<ui::Button>(std::move(text), base + "." + id)));
    };

    // ---- "Les copies et leurs reperes" ----
    auto copies = std::make_unique<CopiesPage>(*this);
    copiesPage_ = copies.get();
    {
        auto band = std::make_unique<Band>(*this);
        band_ = &static_cast<Band&>(copies->addChild(std::move(band)));
        modeButtons_.push_back(button(*band_, "Une suite : n", "series"));
        modeButtons_.push_back(button(*band_, "Une grille : i \xC3\x97 j", "grid"));
        links_ += modeButtons_[0]->clicked->connect([this] { setCopies(std::max(1, plan_.copies())); });
        links_ += modeButtons_[1]->clicked->connect([this] {
            if (rules_.counters.mode != dup::CountMode::Grid) prefillFromBounds();
        });
        auto f = std::make_unique<ui::InputText>(base + ".copies");
        f->setText(std::to_string(plan_.copies()));
        copiesField_ = &static_cast<ui::InputText&>(band_->addChild(std::move(f)));
        links_ += copiesField_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            long long n = 0;                       // 1.12.19 : la lecture commune (1 000 ; pas 12abc)
            if (sim::readInteger(t, n) && n >= 0 && n <= 100000) setCopies(static_cast<int>(n));      // 1.10.4 : 0 - l'original seulement
        });
        for (int q = 0; q < 6; ++q) {
            auto r = std::make_unique<ui::InputText>(base + ".range" + std::to_string(q));
            rangeFields_.push_back(&static_cast<ui::InputText&>(band_->addChild(std::move(r))));
            links_ += rangeFields_.back()->textChanged->connect([this, q](const std::string& t) {
                if (syncing_ || rules_.counters.mode != dup::CountMode::Grid) return;
                long long n = 0;
                if (!sim::readInteger(t, n) || n < -100000 || n > 100000) return;
                auto ranges = rules_.counters.grid;
                const auto at = static_cast<std::size_t>(q / 2);
                if (at >= ranges.size()) return;
                (q % 2 == 0 ? ranges[at].from : ranges[at].to) = n;
                setGrid(std::move(ranges));
            });
        }
        kButton_ = button(*band_, "+ un compteur k", "counterK");
        links_ += kButton_->clicked->connect([this] { setCounterK(rules_.counters.grid.size() < 3); });
        if (!scan_.markers.empty() || !scan_.indices.empty()) {
            auto box = std::make_unique<ui::Checkbox>("Remplacer aussi dans l'original", base + ".original");
            box->setState(plan_.replaceOriginal ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
            replaceOriginal_ = &static_cast<ui::Checkbox&>(band_->addChild(std::move(box)));
            links_ += replaceOriginal_->stateChanged->connect([this](ui::Checkbox::State s) { if (!syncing_) setReplaceOriginal(s == ui::Checkbox::State::Checked); });
        }
        boundsButton_ = button(*band_, "Pr\xC3\xA9remplir depuis les bornes", "bounds");
        boundsButton_->setStyle(ui::Button::Style::Flat);
        links_ += boundsButton_->clicked->connect([this] { prefillFromBounds(); });
    }
    depthModel_ = std::make_shared<DepthModel>(*this);
    depthTable_ = table(*copies, "depths", depthModel_,
                        {{"Prof.", 52.f}, {"Dans le chemin", 210.f}, {"Original", 90.f}, {"Varie ?", 86.f}, {"R\xC3\xA8gle", 200.f},
                         {"Donne (copies)", 150.f}, {"Bornes", 110.f}, {"Rep\xC3\xA8res", 80.f}});
    depthTable_->setDoubleClickEdits(true);
    depthTable_->setF2ClickedColumns({3, 4});
    {
        auto s = std::make_unique<ui::InputText>(base + ".search");
        s->setPlaceholder("Chercher : chemin, rep\xC3\xA8re, objet\xE2\x80\xA6");
        searchField_ = &static_cast<ui::InputText&>(copies->addChild(std::move(s)));
        links_ += searchField_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            search_ = t;
            buildList();
        });
        static const char* kIds[] = {"all", "indices", "addresses", "texts", "own", "conflicts"};
        for (int i = 0; i < 6; ++i) {
            filterButtons_.push_back(button(*copies, "Tous", std::string("filter.") + kIds[i]));
            links_ += filterButtons_.back()->clicked->connect([this, i] { setListFilter(static_cast<ListFilter>(i)); });
        }
        static const char* kGroups[] = {"Profondeur", "Objet", "Rien"};
        for (int i = 0; i < 3; ++i) {
            groupButtons_.push_back(button(*copies, kGroups[i], std::string("group") + std::to_string(i)));
            links_ += groupButtons_.back()->clicked->connect([this, i] { setGrouping(static_cast<Grouping>(i)); });
        }
        foldButtons_.push_back(button(*copies, "Tout d\xC3\xA9plier", "expand"));
        foldButtons_.push_back(button(*copies, "Tout replier", "collapse"));
        links_ += foldButtons_[0]->clicked->connect([this] { expandAll(true); });
        links_ += foldButtons_[1]->clicked->connect([this] { expandAll(false); });
        for (auto* b : foldButtons_) b->setStyle(ui::Button::Style::Flat);
    }
    listModel_ = std::make_shared<ListModel>(*this);
    listTable_ = table(*copies, "markers", listModel_, {{"Rep\xC3\xA8re", 300.f}, {"O\xC3\xB9 (objet \xC2\xB7 propri\xC3\xA9t\xC3\xA9)", 200.f}, {"R\xC3\xA8gle", 170.f}, {"Derni\xC3\xA8re copie", 260.f}});
    listTable_->setSelectionMode(ui::SelectionMode::Extended);
    links_ += listTable_->expanderClicked->connect([this](ui::RowIndex row) {
        if (row < listRows_.size() && listRows_[row].group) toggleGroup(listRows_[row].key);
    });
    // Le clic droit : les Remplir d'avant, pour les reperes choisis (un groupe : tous les siens).
    listTable_->setExtraContextOnly(true);
    listTable_->setExtraContextItems([this] {
        std::vector<ui::PopupMenu::Item> items;
        const auto sel = selectedMarkers();
        const bool any = !sel.empty();
        const auto entry = [&](std::string label, int id) {
            ui::PopupMenu::Item it;
            it.label = std::move(label);
            it.id = id;
            it.enabled = any;
            if (!any) it.disabledReason = "choisis d'abord un rep\xC3\xA8re ou un groupe";
            return it;
        };
        items.push_back(entry("Comme sa profondeur", 1000));
        items.push_back(entry("Formule\xE2\x80\xA6", 1001));
        items.push_back(entry("S\xC3\xA9rie (V1{n})\xE2\x80\xA6", 1002));
        auto tab = entry("Tableau\xE2\x80\xA6", 1003);
        const auto list = arrays();
        for (std::size_t i = 0; i < list.size() && i < 60; ++i) tab.children.push_back(entry(list[i], 1100 + static_cast<int>(i)));
        if (tab.children.empty()) {
            tab.enabled = false;
            tab.disabledReason = "aucun tableau dans le projet";
        }
        items.push_back(std::move(tab));
        items.push_back(entry("Coller (Excel)", 1004));
        items.push_back(entry("Garder", 1005));
        items.push_back(entry("Vider", 1006));
        return items;
    });
    links_ += listTable_->contextAction->connect([this](int id) {
        const auto sel = selectedMarkers();
        if (sel.empty()) return;
        const auto columnOf = [&](std::size_t mi) { return markerLabel(scan_.markers[mi].name); };
        if (id == 1000)
            for (const auto mi : sel) rules_.own.erase(dup::markerKey(scan_.markers[mi].name));
        else if (id == 1001 || id == 1002) {
            openOwnEditor(id == 1001 ? 1 : 2);
            return;
        } else if (id == 1004) {
            const std::string clip = ui::clipboardText();
            for (const auto mi : sel) fillList(columnOf(mi), clip);
            return;
        } else if (id == 1005)
            for (const auto mi : sel) rules_.own[dup::markerKey(scan_.markers[mi].name)] = dup::Own{dup::OwnKind::Keep, {}, {}, "garder"};
        else if (id == 1006) {
            for (const auto mi : sel) clearColumn(columnOf(mi));
            return;
        } else if (id >= 1100) {
            const auto list = arrays();
            const auto at = static_cast<std::size_t>(id - 1100);
            if (at < list.size())
                for (const auto mi : sel) fillArray(columnOf(mi), list[at]);
            return;
        }
        recompute(false);
        rebuildGrid();
        refresh();
    });
    // La regle a part : Formule... (une profondeur, la formule), Serie (le motif, de).
    {
        auto dd = std::make_unique<ui::DropDown>(base + ".ownDepth");
        ownDepth_ = &static_cast<ui::DropDown&>(copies->addChild(std::move(dd)));
        auto f = std::make_unique<ui::InputText>(base + ".ownFormula");
        f->setMonospace(true);
        ownField_ = &static_cast<ui::InputText&>(copies->addChild(std::move(f)));
        auto fr = std::make_unique<ui::InputText>(base + ".ownFrom");
        ownFrom_ = &static_cast<ui::InputText&>(copies->addChild(std::move(fr)));
        ownButtons_.push_back(button(*copies, "Appliquer", "ownApply"));
        ownButtons_.push_back(button(*copies, "Annuler", "ownCancel"));
        ownButtons_[0]->setStyle(ui::Button::Style::Primary);
        const auto apply = [this] {
            const auto who = editing_;
            const int mode = editMode_;
            editMode_ = 0;
            editing_.clear();
            const std::string text = ownField_ ? ownField_->text() : std::string{};
            const auto* depth = ownDepth_ ? ownDepth_->selectedItem() : nullptr;
            for (const auto mi : who) {
                const std::string col = markerLabel(scan_.markers[mi].name);
                if (mode == 2) fillSeries(col, text, ownFrom_ ? ownFrom_->text() : std::string("1"));
                else if (!text.empty()) setOwnFormula(col, text, depth && model_.markers[mi].kind == dup::GroupKind::Index ? depth->value : std::string{});
                else resetOwn(col);
            }
            if (copiesPage_) copiesPage_->invalidateLayout();
        };
        links_ += ownButtons_[0]->clicked->connect(apply);
        links_ += ownField_->editingDone->connect([this, apply](const std::string&) {
            if (editMode_ && !syncing_) apply();
        });
        links_ += ownButtons_[1]->clicked->connect([this] {
            editMode_ = 0;
            editing_.clear();
            if (copiesPage_) copiesPage_->invalidateLayout();
        });
    }
    previewModel_ = std::make_shared<PreviewModel>(*this);
    previewTable_ = table(*copies, "preview", previewModel_, {{"#", 70.f}});
    links_ += previewTable_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (rows.empty() || rows.front() >= previewIndex_.size()) return;
        const int r = previewIndex_[rows.front()];
        if (r >= 0) chooseRow(r);
    });
    links_ += previewTable_->activated->connect([this](ui::RowIndex row) {
        if (row < previewIndex_.size() && previewIndex_[row] < 0) showAllPreview(true);
    });
    conflictModel_ = std::make_shared<ConflictModel>(*this);
    conflictTable_ = table(*copies, "conflicts", conflictModel_, {{"Conflit", 420.f}, {"Aller \xC3\xA0", 90.f}});
    links_ += conflictTable_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (!rows.empty() && rows.front() < conflicts_.size()) chooseRow(conflicts_[rows.front()].row);
    });
    tabs->addTab(ui::TabControl::Tab{"Les copies et leurs rep\xC3\xA8res"}, std::move(copies));

    // ---- "La disposition" ----
    auto lay = std::make_unique<LayoutPage>(*this);
    layoutPage_ = lay.get();
    axis_ = std::make_shared<ui::RadioGroup>();
    const char* axes[] = {"Sur X", "Sur Y", "En grille"};
    for (int i = 0; i < 3; ++i)
        lay->radios.push_back(&static_cast<ui::RadioButton&>(lay->addChild(std::make_unique<ui::RadioButton>(axes[i], axis_, i, base + ".axis" + std::to_string(i)))));
    axis_->setValue(static_cast<int>(plan_.layout.axis));
    links_ += axis_->valueChanged->connect([this](int v) {
        if (syncing_) return;
        auto l = plan_.layout;
        l.axis = static_cast<dup::Axis>(std::clamp(v, 0, 2));
        setLayout(l);
    });
    {
        auto f = std::make_unique<ui::InputText>(base + ".columns");
        f->setText(std::to_string(plan_.layout.columns));
        columnsField_ = &static_cast<ui::InputText&>(lay->addChild(std::move(f)));
        links_ += columnsField_->textChanged->connect([this](const std::string& t) {
            if (syncing_) return;
            long long n = 0;
            if (!sim::readInteger(t, n) || n < 1 || n > 100000) return;
            auto l = plan_.layout;
            l.columns = static_cast<int>(n);
            setLayout(l);
        });
        auto s = std::make_unique<ui::InputText>(base + ".spacing");
        s->setText(std::to_string(static_cast<long>(std::lround(plan_.layout.spacing))));
        spacingField_ = &static_cast<ui::InputText&>(lay->addChild(std::move(s)));
        links_ += spacingField_->textChanged->connect([this](const std::string& t) {
            double d = 0;
            if (syncing_ || !sim::readDecimal(t, d)) return;    // 1.12.19 : 12,5 (avant : 12)
            auto l = plan_.layout;
            if (l.spacingY < 0) l.spacingY = l.gapY();   // 1.12.16 : Y garde le sien
            l.spacing = d;
            setLayout(l);
        });
        auto sy = std::make_unique<ui::InputText>(base + ".spacingY");
        sy->setText(std::to_string(static_cast<long>(std::lround(plan_.layout.gapY()))));
        spacingYField_ = &static_cast<ui::InputText&>(lay->addChild(std::move(sy)));
        links_ += spacingYField_->textChanged->connect([this](const std::string& t) {
            double d = 0;
            if (syncing_ || !sim::readDecimal(t, d)) return;
            auto l = plan_.layout;
            l.spacingY = d;
            setLayout(l);
        });
        fit_ = &static_cast<ui::Button&>(lay->addChild(std::make_unique<ui::Button>("Ajuster l'espacement", base + ".fit")));
        links_ += fit_->clicked->connect([this] { fitSpacing(); });
    }
    lay->preview = &static_cast<Preview&>(lay->addChild(std::make_unique<Preview>(*this)));
    tabs->addTab(ui::TabControl::Tab{"La disposition"}, std::move(lay));

    // ---- 1.50.0 : "Tableau (Excel)" - la grille d'avant, virtuelle ----
    {
        auto sheet = std::make_unique<Sheet>(*this);
        sheet_ = sheet.get();
        auto ed = std::make_unique<ui::InputText>(base + ".cell");
        sheet_->editor = &static_cast<ui::InputText&>(sheet_->addChild(std::move(ed)));
        sheet_->editor->setVisibility(ui::Visibility::Collapsed);
        links_ += sheet_->editor->editingDone->connect([this](const std::string&) {
            if (sheet_) sheet_->commit();
        });
        ui::TabControl::Tab tab{"Tableau (Excel)"};
        tab.badge = std::to_string(scan_.markers.size()) + " \xC3\x97 " + std::to_string(plan_.rows.size());
        tabs->addTab(std::move(tab), std::move(sheet));
    }
    tabs_ = &static_cast<ui::TabControl&>(body->addChild(std::move(tabs)));

    // ---- Annuler, Dupliquer ----
    auto* cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    body->buttons.push_back(cancel);
    links_ += cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(confirmLabel(), base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    body->buttons.push_back(ok_);
    links_ += ok_->clicked->connect([this] { confirm(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

void HmiDuplicateDialog::setReplaceOriginal(bool on) {
    plan_.replaceOriginal = on;
    if (replaceOriginal_ && replaceOriginal_->isChecked() != on)
        replaceOriginal_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    refresh();
}

void HmiDuplicateDialog::setLayout(const hmi::dup::Layout& layout) {
    plan_.layout = layout;
    plan_.layout.columns = std::max(1, plan_.layout.columns);
    plan_.layout.spacing = std::max(0.0, plan_.layout.spacing);
    if (plan_.layout.spacingY >= 0) plan_.layout.spacingY = std::max(0.0, plan_.layout.spacingY);
    syncing_ = true;
    if (axis_) axis_->setValue(static_cast<int>(plan_.layout.axis));
    if (columnsField_) columnsField_->setText(std::to_string(plan_.layout.columns));
    if (spacingField_) {
        spacingField_->setText(std::to_string(static_cast<long>(std::lround(plan_.layout.spacing))));
        spacingField_->setEnabled(plan_.layout.axis != dup::Axis::Y);
    }
    if (spacingYField_) {   // 1.12.16 : l'espacement en Y (le meme qu'en X tant qu'il n'est pas tape)
        spacingYField_->setText(std::to_string(static_cast<long>(std::lround(plan_.layout.gapY()))));
        spacingYField_->setEnabled(plan_.layout.axis != dup::Axis::X);
    }
    syncing_ = false;
    refresh();
}

bool HmiDuplicateDialog::fitSpacing() {
    const auto* v = view();
    if (!v) return false;
    // 1.12.16 : en X, en Y, ou chacun (la grille).
    const auto l = dup::fittingLayout(*v, scan_.box, plan_.copies(), plan_.layout);
    if (!l) return false;
    setLayout(*l);
    return true;
}

std::vector<int> HmiDuplicateDialog::overflow() const {
    const auto* v = view();
    return v ? dup::overflowing(*v, scan_.box, plan_.copies(), plan_.layout) : std::vector<int>{};
}

std::vector<int> HmiDuplicateDialog::overlap() const {
    const auto* v = view();
    return v ? dup::overlapping(*v, scan_.units, plan_.copies(), plan_.layout) : std::vector<int>{};
}

void HmiDuplicateDialog::showTab(std::size_t index) {
    if (tabs_) tabs_->setCurrentIndex(index);
}

std::size_t HmiDuplicateDialog::currentTab() const { return tabs_ ? tabs_->currentIndex() : 0; }

std::vector<std::string> HmiDuplicateDialog::beforeAfter(int row) const {
    std::vector<std::string> out;
    if (row < 0 || static_cast<std::size_t>(row) >= plan_.rows.size()) return out;
    const auto& r = plan_.rows[static_cast<std::size_t>(row)];
    std::set<std::pair<hmi::Id, std::string>> seen;
    std::map<hmi::Id, Labels> labels;     // 1.11 (T2-22) : les libelles de l'inspecteur, une fois par objet
    const hmi::View* v = view();
    const hmi::Project* project = spec_.doc ? &spec_.doc->project : nullptr;
    const auto add = [&](const dup::Spot& s) {
        if (!seen.insert({s.object, s.where}).second || out.size() >= 7) return;
        std::string after = s.text;
        if (row > 0 || plan_.replaceOriginal)
            after = dup::replaceMarkers(dup::followIndices(s.text, r.indices), r.markers, nullptr, nullptr, s.expression);
        auto it = labels.find(s.object);
        if (it == labels.end()) it = labels.emplace(s.object, v ? inspectorLabels(*v, s.object, project) : Labels{}).first;
        out.push_back(s.objectName + " \xC2\xB7 " + spotLabel(it->second, s.where) + " :  " + s.text + "   \xE2\x86\x92   " + after);
    };
    for (const auto& m : scan_.markers)
        for (const auto& s : m.spots) add(s);
    for (const auto& k : scan_.indices)
        for (const auto& s : k.spots) add(s);
    if (out.empty()) out.push_back(row == 0 ? "L'original ne change pas." : "Une copie de l'original, pos\xC3\xA9" "e \xC3\xA0 c\xC3\xB4t\xC3\xA9.");
    return out;
}

ui::EventResult HmiDuplicateDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Ctrl+Entree : "Dupliquer" (Entree choisit dans l'aide d'une case).
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            confirm();
            return ui::EventResult::Consumed;
        }
    }
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        finish(false);
        return ui::EventResult::Consumed;
    }
    return r;
}

bool HmiDuplicateDialog::confirm() {
    if (!canConfirm()) return false;
    finish(true);
    return true;
}

void HmiDuplicateDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    if (ok && spec_.apply) {
        const auto plan = plan_;    // la fenetre se ferme ensuite
        spec_.apply(plan);
    }
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
}

} // namespace app
