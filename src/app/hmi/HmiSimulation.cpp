#include "HmiSimulation.hpp"
#include "HmiDebugPane.hpp"          // 1.13.0 (lot J2) : Simulation > Debogage
#include "../Disposition.hpp"
#include "../../core/Edition.hpp"   // 1.12.0 : IHM Studio - la simulation de l'IHM seule
#include "../../core/AtomicFile.hpp"   // 1.11.15 : l'instantane de la simulation, ecrit d'un bloc
#include "../../hmi/HmiSimData.hpp"
#include "HmiParamPanes.hpp"   // 1.9 : setPlcTypes, le repere des copies modifiees
#include "../ExportTarget.hpp"
#include "HmiTwinValues.hpp"
#include "HmiEquipmentHost.hpp"          // 1.9 : les choix de la page Simulation, oublies au demarrage
#include "HmiSimMarks.hpp"               // 1.9 : les reperes des lectures simulees
#include "../../hmi/HmiTwin.hpp"
#include "../../hmi/HmiSymbols.hpp"   // 1.11.6 : expandInstances (sur la vue actuelle, arretee)
#include "../../hmi/HmiMarkers.hpp"   // 1.11 (REP-1) : recette et groupe sans les $ de leurs reperes
#include "../../hmi/HmiTypes.hpp"
#include "../../hmi/HmiControls.hpp"
#include "../../hmi/HmiWidgets.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiForms.hpp"
#include "../../hmi/HmiAlarmViews.hpp"
#include "../../hmi/HmiProduction.hpp"
#include "../../hmi/HmiDisplay.hpp"
#include "../../hmi/HmiDesign.hpp"   // 1.12.18 : le projet lit-il SYS.MouseRight ?
#include "../../hmi/HmiLanguages.hpp"
#include "../../hmi/HmiNavigation.hpp"

#include "HmiAssetPanes.hpp"
#include "HmiSimVarTree.hpp"   // 1.11.5 : les variables IHM et API en arbre
#include "HmiIcons.hpp"
#include "HmiSound.hpp"
#include "HmiSystemPainter.hpp"
#include "HmiLoginPainter.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../sim/Runtime.hpp"
#include "../../hmi/HmiComm.hpp"
#include "../../ui/Commands.hpp"
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;

namespace {

// 1.9 : "14:02:31", une heure murale (s depuis 1970), en heure locale.
std::string wallClock(double wall) {
    if (wall <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(wall);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

// 1.9 : l'infobulle d'une lecture simulee - la variable (sa description, sinon le
// libelle de l'objet : "Consigne variateur (Consigne_Variateur)"), son equipement,
// pourquoi l'IHM lit l'esclave et depuis quand.
std::string simReadTip(const hmi::Runtime& rt, const hmi::Project& project, const std::string& path, const std::string& equipment,
                       const std::string& label) {
    std::string what = path;
    const std::string root = path.substr(0, path.find_first_of(".["));
    if (const auto* v = project.variable(root); v && !v->description.empty()) what = v->description + " (" + path + ")";
    else if (!label.empty() && label != path) what = label + " (" + path + ")";
    const auto st = rt.equipmentStatus(equipment);
    const std::string since = st && st->readSince > 0 ? " depuis " + wallClock(st->readSince) : std::string{};
    if (st && st->fallback)
        return what + " : " + equipment + " ne r\xC3\xA9pond pas" + (st->realSilentSince > 0 ? " depuis " + wallClock(st->realSilentSince) : std::string{})
               + " ; l'IHM lit son esclave simul\xC3\xA9" + since + ".";
    // (EquipmentStatus::simulated dit "lu sur un serveur simule" : seulement simule, c'est le projet.)
    if (const auto* eq = project.equipmentByName(equipment); eq && eq->simulated)
        return what + " : " + equipment + " est seulement simul\xC3\xA9 (pas encore livr\xC3\xA9) : l'IHM lit son esclave.";
    if (st && st->chosen) return what + " : l'IHM lit l'esclave simul\xC3\xA9 de " + equipment + " (choisi sur la page Simulation)" + since + ".";
    return what + " : l'IHM lit l'esclave simul\xC3\xA9 de " + equipment + since + ".";
}

// Les valeurs, une ligne par propriete pilotee.
// 1.11.6 : L'ONGLET EXPRESSIONS EN ARBRE (« dans expressions etre en mode treeview aussi »).
// Un objet, puis ses proprietes ; une instance de symbole developpee range ses objets
// sous elle (Aff_1 > Texte > text), a toute profondeur. Un noeud dit combien
// d'expressions il porte (et ses erreurs) ; la recherche (mots ET, "phrase", -exclu)
// garde ce qu'elle trouve et ouvre ses noeuds. Les valeurs se relisent a chaque image ;
// l'arbre ne se refait que si la vue change (les noms et les cles des expressions).
class ExprTree final : public ui::ITableModel {
public:
    struct Row {
        bool        node{false};
        std::string label, path;
        int         depth{0};
        std::size_t value{std::string::npos};   // l'expression (values_) d'une ligne de propriete
        std::size_t count{0}, errors{0};
        bool        open{true};
    };
    explicit ExprTree(const std::vector<hmi::LiveValue>* values) : values_(values) {}

    // L'arbre a-t-il change de forme depuis le dernier rebuild (la vue, ses objets) ?
    [[nodiscard]] bool stale() const { return signature() != sig_; }
    void rebuild(const std::string& query) {
        query_ = query;
        sig_ = signature();
        build();
    }
    void toggle(std::size_t row) {
        if (row >= rows_.size() || !rows_[row].node) return;
        const auto& path = rows_[row].path;
        if (rows_[row].open) closed_.insert(path);
        else closed_.erase(path);
        build();
    }
    void setAllOpen(bool open) {
        closed_.clear();
        if (!open)
            for (const auto& n : nodes_) closed_.insert(n.path);
        build();
    }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t valueAt(std::size_t row) const { return row < rows_.size() ? rows_[row].value : std::string::npos; }

    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* h[] = {"Objet / propri\xC3\xA9t\xC3\xA9", "Expression", "Valeur"};
        return c < 3 ? h[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= rows_.size()) return {};
        const auto& row = rows_[r];
        if (row.node) {
            if (c == 0) return row.label;
            if (c == 2) {
                std::string t = std::to_string(row.count) + (row.count > 1 ? " expressions" : " expression");
                if (row.errors) t += " \xC2\xB7 " + std::to_string(row.errors) + (row.errors > 1 ? " erreurs" : " erreur");
                return t;
            }
            return {};
        }
        if (row.value >= values_->size()) return {};
        const auto& v = (*values_)[row.value];
        if (c == 0) return v.key;
        // 1.12.19 (la regle des cases) : une expression se montre avec son « = » (un texte a trous, tel quel).
        if (c == 1) return v.text || v.expression.empty() || v.expression.front() == '=' ? v.expression : "=" + v.expression;
        // Un texte multiligne tient sur une ligne du tableau.
        std::string out = v.value;
        for (std::size_t at = out.find('\n'); at != std::string::npos; at = out.find('\n', at + 3)) out.replace(at, 1, " / ");
        // Lot 14 : une valeur de l'automate reel qui n'est pas bonne.
        if (v.quality) out += v.quality >= 2 ? "  (mauvaise : " + v.qualityWhy + ")" : "  (ancienne : " + v.qualityWhy + ")";
        return out;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle s;
        if (r >= rows_.size()) return s;
        const auto& row = rows_[r];
        if (c == 0) {
            s.indent = static_cast<float>(row.depth) * 16.f;
            if (row.node) {
                s.expander = row.open ? 1 : 0;
                s.bold = true;
                s.icon = row.depth == 0 ? ui::Icon::Module : ui::Icon::Variable;
            } else {
                s.icon = ui::Icon::Code;
            }
            return s;
        }
        if (row.node) {
            if (c == 2) s.fgTone = row.errors ? ui::Tone::Error : ui::Tone::None;
            return s;
        }
        if (c == 1 && row.value < values_->size()) s.monospace = !(*values_)[row.value].text;   // 1.12.19 : une formule en chasse fixe
        if (c == 2 && row.value < values_->size()) {
            const auto& v = (*values_)[row.value];
            s.bold = true;
            // 1.11 (REP) : une expression a repere se calcule comme les autres (1.10.3 : ambre).
            // 1.12.19 : les couleurs du theme (une faute en rouge, une valeur ancienne en ambre).
            if (v.error) { s.fgTone = ui::Tone::Error; s.icon = ui::Icon::Error; }
            else if (v.quality >= 2) { s.fgTone = ui::Tone::Error; s.icon = ui::Icon::Warning; }
            else if (v.quality == 1) { s.fgTone = ui::Tone::Warning; s.icon = ui::Icon::Warning; }
        }
        return s;
    }
    // L'ordre de l'arbre (pas de tri par colonne : il casserait les noeuds).
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }

private:
    struct Node {
        std::string              label, path;
        int                      parent{-1}, depth{0};
        std::vector<int>         kids;
        std::vector<std::size_t> values;
    };
    [[nodiscard]] std::size_t signature() const {
        std::size_t h = values_->size();
        for (const auto& v : *values_) h = h * 1099511628211ull ^ std::hash<std::string>{}(v.objectName) ^ (std::hash<std::string>{}(v.key) << 1);
        return h;
    }
    void build() {
        nodes_.clear();
        std::map<std::string, int> index;
        std::vector<int> roots;
        for (std::size_t i = 0; i < values_->size(); ++i) {
            const std::string& name = (*values_)[i].objectName;
            std::string prefix;
            int parent = -1;
            std::size_t start = 0;
            int depth = 0;
            while (true) {
                const auto dot = name.find('.', start);
                const std::string piece = name.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
                prefix += (prefix.empty() ? "" : ".") + piece;
                auto it = index.find(prefix);
                if (it == index.end()) {
                    Node n;
                    n.label = piece.empty() ? std::string("(sans nom)") : piece;
                    n.path = prefix;
                    n.parent = parent;
                    n.depth = depth;
                    nodes_.push_back(std::move(n));
                    const int at = static_cast<int>(nodes_.size()) - 1;
                    if (parent >= 0) nodes_[static_cast<std::size_t>(parent)].kids.push_back(at);
                    else roots.push_back(at);
                    it = index.emplace(prefix, at).first;
                }
                parent = it->second;
                ++depth;
                if (dot == std::string::npos) break;
                start = dot + 1;
            }
            nodes_[static_cast<std::size_t>(parent)].values.push_back(i);
        }
        const ui::SearchQuery q(query_);
        const bool searching = !q.empty();
        const auto kept = [&](std::size_t i) {
            if (!searching) return true;
            const auto& v = (*values_)[i];
            return q.matches({v.objectName, v.key, v.expression, v.value});
        };
        // Les comptes (recursifs) : ce qui est garde dessous, et ses erreurs.
        std::vector<std::size_t> count(nodes_.size(), 0), errors(nodes_.size(), 0);
        for (std::size_t k = nodes_.size(); k-- > 0;) {
            for (const auto i : nodes_[k].values)
                if (kept(i)) {
                    ++count[k];
                    errors[k] += (*values_)[i].error ? 1 : 0;
                }
            for (const int c : nodes_[k].kids) {
                count[k] += count[static_cast<std::size_t>(c)];
                errors[k] += errors[static_cast<std::size_t>(c)];
            }
        }
        rows_.clear();
        const std::function<void(int)> walk = [&](int at) {
            const auto& n = nodes_[static_cast<std::size_t>(at)];
            if (count[static_cast<std::size_t>(at)] == 0) return;
            Row r;
            r.node = true;
            r.label = n.label;
            r.path = n.path;
            r.depth = n.depth;
            r.count = count[static_cast<std::size_t>(at)];
            r.errors = errors[static_cast<std::size_t>(at)];
            r.open = searching || closed_.count(n.path) == 0;
            rows_.push_back(r);
            if (!r.open) return;
            for (const auto i : n.values) {
                if (!kept(i)) continue;
                Row v;
                v.depth = n.depth + 1;
                v.value = i;
                v.path = n.path;
                rows_.push_back(v);
            }
            for (const int c : n.kids) walk(c);
        };
        for (const int root : roots) walk(root);
    }

    const std::vector<hmi::LiveValue>* values_;
    std::vector<Node>                  nodes_;
    std::vector<Row>                   rows_;
    std::set<std::string>              closed_;
    std::string                        query_;
    std::size_t                        sig_{0};
};

// 1.11.7 : le mouvement d'une variable IHM <-> le comportement de la case de son esclave (plus bas).
hmi::Behavior       behaviorOf(const hmi::motion::Motion& m, const hmi::twin::ValueRow& r);
hmi::motion::Motion motionOf(const hmi::Behavior& b, const hmi::twin::ValueRow& r);

class TextRows final : public ui::ITableModel {
public:
    using Style = std::function<ui::CellStyle(ui::RowIndex, std::size_t)>;
    TextRows(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, Style style = {})
        : headers_(std::move(headers)), rows_(std::move(rows)), style_(std::move(style)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : ""; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override { return style_ ? style_(r, c) : ui::CellStyle{}; }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    Style style_;
};

// Un chemin tout seul ("Armoires[0].prete") : c'est lui qu'on proposera de forcer.
bool plainPath(const std::string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char ch : s)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.' || ch == '[' || ch == ']'))
            return false;
    return true;
}

gfx::Color faded(gfx::Color c, float a) {
    c.a = static_cast<std::uint8_t>(std::clamp(static_cast<float>(c.a) * a, 0.f, 255.f));
    return c;
}

// Lot 14 : un trait en tirets, un cadre en tirets (la qualite des valeurs).
void dashedLine(gfx::IRenderer& r, gfx::Point a, gfx::Point b, gfx::Color c, float width, float on, float off) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.f) return;
    for (float t = 0.f; t < len; t += on + off) {
        const float t2 = std::min(len, t + on);
        r.line({a.x + dx * t / len, a.y + dy * t / len}, {a.x + dx * t2 / len, a.y + dy * t2 / len}, c, width);
    }
}
void dashedRect(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c, float width) {
    dashedLine(r, {b.x, b.y}, {b.right(), b.y}, c, width, 6.f, 4.f);
    dashedLine(r, {b.right(), b.y}, {b.right(), b.bottom()}, c, width, 6.f, 4.f);
    dashedLine(r, {b.right(), b.bottom()}, {b.x, b.bottom()}, c, width, 6.f, 4.f);
    dashedLine(r, {b.x, b.bottom()}, {b.x, b.y}, c, width, 6.f, 4.f);
}

// Lot 12 : le fil d'Ariane tel que le montre le dessin (la vue courante du
// moteur, son historique, sa vue d'accueil).
std::vector<hmi::Crumb> trailOf(const hmi::Object& o, const hmi::Runtime* rt, const hmi::Project* p) {
    if (!rt || !p) return {};
    const auto* cur = p->view(rt->currentView());
    const auto* home = p->view(rt->homeView());
    return hmi::breadcrumbTrail(*p, o, cur ? cur->name : std::string{}, rt->historyNames(), home ? home->name : std::string{});
}

// Lot 12 : la partie d'un objet de navigation ou de structure sous ce point
// (repere de l'objet) ; "" : aucune.
std::string lot12PartAt(const hmi::View& view, const hmi::Object& o, double w, double h, double lx, double ly,
                        const hmi::Runtime* rt, const hmi::Project* p) {
    switch (o.kind) {
        case hmi::Kind::NavBar: return hmi::navBarHit(o, w, h, lx, ly, hmi::navItems(p, o).size());
        case hmi::Kind::Breadcrumb: return hmi::breadcrumbHit(o, w, h, lx, ly, trailOf(o, rt, p));
        case hmi::Kind::TabContainer: return hmi::tabHit(o, w, h, lx, ly, hmi::tabLabels(o).size());
        case hmi::Kind::ScrollPanel: return hmi::scrollHit(hmi::scrollLayout(view, o), lx, ly);
        case hmi::Kind::CollapsiblePanel: return hmi::collapsibleHit(o, w, h, lx, ly);
        case hmi::Kind::ZoneMap: return hmi::zoneMapHit(o, w, h, lx, ly);
        default: return {};
    }
}

// Lot 12 : la boite (repere de l'objet) d'une partie, pour les scripts.
bool lot12PartBox(const hmi::View& view, const hmi::Object& o, double w, double h, std::string_view part, const hmi::Runtime* rt,
                  const hmi::Project* p, hmi::Box& out) {
    const auto index = [&](std::string_view prefix) {
        return part.rfind(prefix, 0) == 0 ? std::atoi(std::string(part.substr(prefix.size())).c_str()) : -1;
    };
    switch (o.kind) {
        case hmi::Kind::NavBar: {
            const auto l = hmi::navBarLayout(o, w, h, hmi::navItems(p, o).size());
            if (part == "precedent" && l.back.w > 0) { out = l.back; return true; }
            if (part == "suivant" && l.forward.w > 0) { out = l.forward; return true; }
            const int i = index("vue:");
            if (i >= 0 && static_cast<std::size_t>(i) < l.items.size()) { out = l.items[static_cast<std::size_t>(i)]; return true; }
            return false;
        }
        case hmi::Kind::Breadcrumb: {
            const auto trail = trailOf(o, rt, p);
            const auto l = hmi::breadcrumbLayout(o, w, h, trail);
            const int i = index("etape:");
            if (i >= 0 && static_cast<std::size_t>(i) < l.crumbs.size() && l.crumbs[static_cast<std::size_t>(i)].w > 0) {
                out = l.crumbs[static_cast<std::size_t>(i)];
                return true;
            }
            return false;
        }
        case hmi::Kind::TabContainer: {
            const auto l = hmi::tabLayout(o, w, h, hmi::tabLabels(o).size());
            const int i = index("onglet:");
            if (i >= 1 && static_cast<std::size_t>(i) <= l.tabs.size()) { out = l.tabs[static_cast<std::size_t>(i - 1)]; return true; }
            return false;
        }
        case hmi::Kind::ScrollPanel: {
            const auto l = hmi::scrollLayout(view, o);
            if (part.rfind("vbarre:", 0) == 0 && l.vbar.w > 0) {
                const double f = std::clamp(std::atof(std::string(part.substr(7)).c_str()), 0.0, 1.0);
                const double y = l.vthumb.h / 2 + (l.vbar.h - l.vthumb.h) * f;
                out = {l.vbar.x + 1, y - 1, l.vbar.w - 2, 2};
                return true;
            }
            if (part.rfind("hbarre:", 0) == 0 && l.hbar.w > 0) {
                const double f = std::clamp(std::atof(std::string(part.substr(7)).c_str()), 0.0, 1.0);
                const double x = l.hthumb.w / 2 + (l.hbar.w - l.hthumb.w) * f;
                out = {x - 1, l.hbar.y + 1, 2, l.hbar.h - 2};
                return true;
            }
            if (part == "contenu") { out = l.viewport; return true; }
            return false;
        }
        case hmi::Kind::CollapsiblePanel:
            if (part != "entete") return false;
            out = {0, 0, w, hmi::collapsibleHeaderHeight(o)};
            return true;
        case hmi::Kind::ZoneMap: {
            const auto zones = hmi::parseMapZones(o.text("mapZones"));
            const int i = index("zone:");
            if (i < 0 || static_cast<std::size_t>(i) >= zones.size()) return false;
            const auto [cx, cy] = hmi::zoneCenter(zones[static_cast<std::size_t>(i)], w, h);
            out = {cx - 2, cy - 2, 4, 4};
            return true;
        }
        default:
            return false;
    }
}

// 1.10 : UNE PASTILLE DE LA BARRE de Simulation . IHM - un point de couleur (le ton :
// "running", "paused", "stopped", "halted", "off", "" sans point) et un texte court
// ("IHM : en marche", "API : arr\xC3\xAAt\xC3\xA9" "e", "100 %").
class StateChip final : public ui::Widget {
public:
    explicit StateChip(std::string id) : ui::Widget(std::move(id)) {}
    void set(std::string text, std::string tone) {
        if (text == text_ && tone == tone_) return;
        text_ = std::move(text);
        tone_ = std::move(tone);
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {ui::measureWidth(text_, gfx::FontId{13}) + (tone_.empty() ? 16.f : 30.f), 26.f};
        h.minimum = h.preferred;
        return h;
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const gfx::Rect box{b.x, b.y + (b.h - 24.f) / 2.f, b.w, 24.f};
        const gfx::Color tone = tone_ == "running" ? c.ok : tone_ == "paused" ? c.info : tone_ == "halted" ? c.error
                              : tone_ == "stopped" || tone_ == "debug" ? c.warning : c.textMuted;   // 1.13.0 : debug, en pause dans un script
        ctx.r.fillRoundedRect(box, tone_.empty() ? c.border : tone.withAlpha(90), 12.f);
        ctx.r.fillRoundedRect({box.x + 1.f, box.y + 1.f, box.w - 2.f, box.h - 2.f}, c.panelBg, 11.f);
        float x = box.x + 9.f;
        if (!tone_.empty()) {
            if (tone_ == "running") ctx.r.fillRoundedRect({x - 3.f, box.y + 6.f, 14.f, 12.f}, tone.withAlpha(50), 6.f);
            ctx.r.fillRoundedRect({x, box.y + 8.f, 8.f, 8.f}, tone, 4.f);
            x += 15.f;
        }
        const gfx::FontId font = ctx.theme.font.smallUi;
        ctx.r.drawText({x, box.y + (box.h - ctx.r.lineHeight(font)) / 2.f}, text_, font, c.text);
    }
private:
    std::string text_, tone_;
};


// 1.10.3 (demande du client) : les 5 boutons de droite et la liste deroulante de
// droite sont retires de la barre ; leurs commandes passent dans le MENU DU CLIC
// DROIT SUR LA BARRE (une ligne vide : un trait). `where` : leur touche (1.12.18 : rien
// d'autre dans la colonne de droite ; avant, « aussi : la barre du haut »...).
struct BarMenuItem { const char* label; const char* action; const char* where; };
constexpr BarMenuItem kBarMenu[] = {
    {"D\xC3\xA9marrer l'API et l'IHM", "both.start", ""},
    {"Tout arr\xC3\xAAter (IHM et API)", "all.stop", ""},
    {"", "", ""},
    {"Vue de d\xC3\xA9marrage", "hmi.home", ""},
    {"D\xC3\xA9" "connecter", "hmi.logout", ""},
    {"Param\xC3\xA8tres syst\xC3\xA8me", "hmi.system", ""},
    {"Page Simulation", "hmi.simpage", "Ctrl+Alt+S"},
    {"", "", ""},
    {"Forcer\xE2\x80\xA6", "hmi.force", ""},
    {"Tout rel\xC3\xA2" "cher", "hmi.unforce", ""},
    {"Tout acquitter", "hmi.ackall", ""},
    {"", "", ""},
    {"Mesures \xC3\xA0 z\xC3\xA9ro", "hmi.perfreset", ""},
    {"Exporter les mesures (CSV)", "hmi.perfexport", ""},
};

// Un tableau dont le clic droit laisse le volet completer le menu (l'onglet Expressions :
// Tout deplier, Tout replier ; avant la 1.11.24, aussi l'onglet Journal).
class JournalTable final : public ui::TableView {
public:
    using ui::TableView::TableView;
    std::function<void(gfx::Point)> onContextMenu;
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right && onContextMenu) {
            const auto r = ui::TableView::onEvent(ev);      // la ligne choisie, le menu de la table
            if (contextMenu() && contextMenu()->isOpen()) onContextMenu(d->pos);
            return r;
        }
        return ui::TableView::onEvent(ev);
    }
};

// 1.11.6 : la page Expressions - la recherche, puis l'arbre.
class ExprPage final : public ui::Widget {
public:
    ExprPage(std::string id, std::unique_ptr<ui::TableView> table) : ui::Widget(std::move(id)) {
        search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(this->id() + ".search")));
        search_->setPlaceholder("Chercher (objet, propri\xC3\xA9t\xC3\xA9, expression, valeur)");
        table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    }
    [[nodiscard]] ui::InputText& search() noexcept { return *search_; }
protected:
    void onLayout() override {
        const auto b = bounds();
        search_->setBounds({b.x + 8.f, b.y + 5.f, std::min(380.f, std::max(80.f, b.w - 16.f)), 26.f});
        table_->setBounds({b.x, b.y + 36.f, b.w, std::max(0.f, b.h - 36.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override { ctx.r.fillRect({bounds().x, bounds().y, bounds().w, 36.f}, ctx.theme.color.panelBg); }
private:
    ui::InputText* search_{nullptr};
    ui::TableView* table_{nullptr};
};

// 1.12.17 : la page Performances - l'instantane (le tableau) et, au-dessus, le lien vers la
// Vue d'ensemble de Simulation, ou elles sont dans le temps (une mesure par seconde, les lenteurs).
class PerfPage final : public ui::Widget {
public:
    PerfPage(std::string id, std::unique_ptr<ui::TableView> table) : ui::Widget(std::move(id)) {
        auto link = std::make_unique<ui::Button>("Dans le temps \xE2\x80\xBA", this->id() + ".temps");
        link->setStyle(ui::Button::Style::Flat);
        link_ = &static_cast<ui::Button&>(addChild(std::move(link)));
        table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    }
    [[nodiscard]] ui::Button& link() noexcept { return *link_; }
protected:
    void onLayout() override {
        const auto b = bounds();
        const float w = std::min(std::max(0.f, b.w - 16.f), std::max(110.f, link_->sizeHint().preferred.w));
        link_->setBounds({b.x + b.w - 8.f - w, b.y + 4.f, w, 26.f});
        table_->setBounds({b.x, b.y + 34.f, b.w, std::max(0.f, b.h - 34.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.y, b.w, 34.f}, c.panelBg);
        const gfx::FontId font = ctx.theme.font.smallUi;
        const float room = b.w - 24.f - (link_->bounds().w + 8.f);
        if (room > 120.f)
            ctx.r.drawText({b.x + 10.f, b.y + (34.f - ctx.r.lineHeight(font)) / 2.f},
                           room > 330.f ? "Maintenant : le dernier cycle, la moyenne et le maximum depuis le d\xC3\xA9part"
                                        : "Maintenant", font, c.textMuted);
    }
private:
    ui::Button*    link_{nullptr};
    ui::TableView* table_{nullptr};
};

// 1.12.27 : LES LIGNES DE L'ONGLET POURQUOI ? - chaque propriete calculee (en gras), son etat
// retenu, puis ses termes (en retrait) ; les valeurs en erreur en orange.
class WhyRows final : public ui::ITableModel {
public:
    struct Line {
        std::string   a, b, c;
        ui::CellStyle first;
        bool          error{false};
        std::size_t   item{0};
        int           term{-1};      // -1 : la propriete (ou son etat)
        bool          state{false};
    };
    std::vector<Line> lines;
    [[nodiscard]] std::size_t rowCount() const override { return lines.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        return c == 0 ? std::string("Ce qui compte") : c == 1 ? std::string("Formule ou terme") : std::string("Maintenant");
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        if (r >= lines.size()) return {};
        return c == 0 ? lines[r].a : c == 1 ? lines[r].b : lines[r].c;
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        if (r >= lines.size()) return {};
        const auto& l = lines[r];
        if (c == 0) return l.first;
        ui::CellStyle cs;
        if (c == 1 && l.term < 0 && !l.state) cs.monospace = true;
        if (c == 2 && l.error) cs.fgTone = ui::Tone::Warning;
        if (l.state) cs.fgTone = ui::Tone::Accent;
        if (l.term < 0 && !l.state && c == 2) cs.bold = true;
        return cs;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
};

// La page Pourquoi ? : en tete l'objet (ou comment en choisir un) et Copier, puis les lignes.
class WhyPage final : public ui::Widget {
public:
    WhyPage(std::string id, std::unique_ptr<ui::TableView> table) : ui::Widget(std::move(id)) {
        auto copy = std::make_unique<ui::Button>("Copier", this->id() + ".copier");
        copy->setStyle(ui::Button::Style::Flat);
        copy->setTooltip("Le rapport en texte, dans le presse-papiers (un message, un ticket)");
        copy_ = &static_cast<ui::Button&>(addChild(std::move(copy)));
        table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
    }
    [[nodiscard]] ui::Button& copy() noexcept { return *copy_; }
    void setHead(std::string head, bool warn) {
        if (head == head_ && warn == warn_) return;
        head_ = std::move(head);
        warn_ = warn;
        invalidate();
    }
    [[nodiscard]] const std::string& head() const noexcept { return head_; }
protected:
    void onLayout() override {
        const auto b = bounds();
        const float w = std::min(std::max(0.f, b.w - 16.f), std::max(80.f, copy_->sizeHint().preferred.w));
        copy_->setBounds({b.x + b.w - 8.f - w, b.y + 4.f, w, 26.f});
        table_->setBounds({b.x, b.y + 34.f, b.w, std::max(0.f, b.h - 34.f)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        ctx.r.fillRect({b.x, b.y, b.w, 34.f}, c.panelBg);
        const gfx::FontId font = ctx.theme.font.smallUi;
        const float room = b.w - 28.f - copy_->bounds().w;
        if (room > 60.f)
            ctx.r.drawText({b.x + 10.f, b.y + (34.f - ctx.r.lineHeight(font)) / 2.f}, simmark::fitText(ctx.r, head_, font, room), font,
                           warn_ ? c.warning : c.textMuted);
    }
private:
    ui::Button*    copy_{nullptr};
    ui::TableView* table_{nullptr};
    std::string    head_;
    bool           warn_{false};
};

} // namespace

// ------------------------------------------------------------ le dessin ----
HmiLiveCanvas::HmiLiveCanvas(std::string id) : ui::Widget(std::move(id)) {
    setFocusPolicy(true);
    setZoomSupport(ui::ZoomSupport::Native);   // 1.12.30
    // 1.11.23 : la souris sortie du canevas - dehors, et ses boutons relaches (un MouseUp
    // dehors ne revient jamais).
    (void)hoverChanged->connect([this](bool in) {
        if (in || !pointerInside_) return;
        pointerInside_ = false;
        pointerMoved->emit(hmi::kNoId, 0.0, 0.0, hmi::kNoId, false);
    });
}

// ---- 1.11.23 : la souris et le clavier de l'IHM en marche ----------------------------
std::string HmiLiveCanvas::keyToken(ui::Key k) {
    using K = ui::Key;
    switch (k) {
        case K::A: return "A"; case K::B: return "B"; case K::C: return "C"; case K::D: return "D"; case K::E: return "E";
        case K::F: return "F"; case K::G: return "G"; case K::H: return "H"; case K::I: return "I"; case K::J: return "J";
        case K::K: return "K"; case K::L: return "L"; case K::M: return "M"; case K::N: return "N"; case K::O: return "O";
        case K::P: return "P"; case K::Q: return "Q"; case K::R: return "R"; case K::S: return "S"; case K::T: return "T";
        case K::U: return "U"; case K::V: return "V"; case K::W: return "W"; case K::X: return "X"; case K::Y: return "Y";
        case K::Z: return "Z";
        case K::Num0: return "Digit0"; case K::Num1: return "Digit1"; case K::Num2: return "Digit2"; case K::Num3: return "Digit3";
        case K::Num4: return "Digit4"; case K::Num5: return "Digit5"; case K::Num6: return "Digit6"; case K::Num7: return "Digit7";
        case K::Num8: return "Digit8"; case K::Num9: return "Digit9";
        case K::F1: return "F1"; case K::F2: return "F2"; case K::F3: return "F3"; case K::F4: return "F4"; case K::F5: return "F5";
        case K::F6: return "F6"; case K::F7: return "F7"; case K::F8: return "F8"; case K::F9: return "F9"; case K::F10: return "F10";
        case K::F11: return "F11"; case K::F12: return "F12";
        case K::Return: return "Enter"; case K::Escape: return "Escape"; case K::Space: return "Space"; case K::Tab: return "Tab";
        case K::Backspace: return "Backspace"; case K::Delete: return "Delete"; case K::Insert: return "Insert";
        case K::Home: return "Home"; case K::End: return "End"; case K::PageUp: return "PageUp"; case K::PageDown: return "PageDown";
        case K::Up: return "Up"; case K::Down: return "Down"; case K::Left: return "Left"; case K::Right: return "Right";
        // 1.12.2 : les signes (les raccourcis des editeurs) ne sont pas des touches de vue.
        case K::Slash: case K::Period: case K::Comma: case K::Minus: case K::RightBracket:
        case K::Colon: case K::Dollar: case K::Semicolon:
        case K::Plus:      // 1.12.30 : Ctrl + plus, le zoom
        case K::Menu:      // 1.12.18 : la touche Menu ouvre le menu du clic droit, pas une touche de vue
        case K::Unknown: break;
    }
    return {};
}

int HmiLiveCanvas::layerUnder(gfx::Point p) const {
    if (layers_.empty() || vps_.size() != layers_.size() || !bounds().contains(p)) return -1;
    for (auto it = popupRects_.rbegin(); it != popupRects_.rend(); ++it)
        if (it->window.contains(p) && it->layer < layers_.size()) return static_cast<int>(it->layer);
    for (std::size_t k = layers_.size(); k-- > 0;)
        if (!layers_[k].popup) {
            const auto& vp = vps_[k];
            const double x = vp.toViewX(p.x), y = vp.toViewY(p.y);
            const auto& v = layers_[k].view;
            return x >= 0 && y >= 0 && x <= v.width && y <= v.height ? static_cast<int>(k) : -1;
        }
    return -1;
}

void HmiLiveCanvas::notePointer(const ui::InputEvent& ev) {
    const auto moved = [&](gfx::Point p) {
        const int li = layerUnder(p);
        if (li < 0) {
            if (pointerInside_) pointerMoved->emit(hmi::kNoId, 0.0, 0.0, hmi::kNoId, false);
            pointerInside_ = false;
            return;
        }
        const auto& vp = vps_[static_cast<std::size_t>(li)];
        pointerInside_ = true;
        pointerMoved->emit(layers_[static_cast<std::size_t>(li)].view.id, vp.toViewX(p.x), vp.toViewY(p.y),
                           objectAtLayer(static_cast<std::size_t>(li), p), true);
    };
    const auto index = [](ui::MouseButton b) { return b == ui::MouseButton::Left ? 0 : b == ui::MouseButton::Right ? 1 : b == ui::MouseButton::Middle ? 2 : -1; };
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) moved(m->pos);
    else if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (!bounds().contains(d->pos)) return;
        moved(d->pos);
        if (const int b = index(d->button); b >= 0) pointerButton->emit(b, true);
    } else if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        if (const int b = index(u->button); b >= 0) pointerButton->emit(b, false);
    } else if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (bounds().contains(w->pos) && w->dy != 0.f) wheelTurned->emit(w->dy > 0 ? 1.0 : -1.0);
    }
}

bool HmiLiveCanvas::routeKey(const ui::InputEvent& ev) {
    if (!keyHandler_ || !(focused() || station_)) return false;
    if (const auto* f = std::get_if<ui::FocusChange>(&ev); f && !f->gained) {
        keysLost->emit();
        return false;
    }
    const auto* kd = std::get_if<ui::KeyDown>(&ev);
    const auto* ku = std::get_if<ui::KeyUp>(&ev);
    if (!kd && !ku) return false;
    const ui::Key key = kd ? kd->key : ku->key;
    const ui::KeyMods mods = kd ? kd->mods : ku->mods;
    // L'editeur garde les siennes : F8 et Maj+F8 (l'IHM), F11 (plein ecran), Ctrl+Alt+S.
    if (!station_ && (key == ui::Key::F8 || key == ui::Key::F11 || (key == ui::Key::S && mods.ctrl && mods.alt))) return false;
    return keyHandler_(keyToken(key), mods, kd != nullptr, kd ? kd->repeat : false);
}

void HmiLiveCanvas::onFocusChanged(bool gained) {
    if (!gained) keysLost->emit();
}

void HmiLiveCanvas::show(hmi::View evaluated, std::vector<Id> errors) {
    HmiLiveLayer l;
    l.view = std::move(evaluated);
    l.errors = std::move(errors);
    showLayers({std::move(l)}, true);
}

// 1.9 : le bandeau LECTURES SIMULEES ; l'infobulle d'un objet repere.
void HmiLiveCanvas::setSimRibbon(std::string text, std::string right) {
    if (text == simRibbon_ && right == simRibbonRight_) return;
    simRibbon_ = std::move(text);
    simRibbonRight_ = std::move(right);
    invalidate();
}

std::string HmiLiveCanvas::simTipAt(gfx::Point p) const {
    for (auto it = simRects_.rbegin(); it != simRects_.rend(); ++it)
        if (it->first.contains(p)) return it->second;
    return {};
}

void HmiLiveCanvas::showLayers(std::vector<HmiLiveLayer> layers, bool interactive) {
    layers_ = std::move(layers);
    interactive_ = interactive;
    invalidate();
}

// 1.10 : l'IHM arretee - un voile sur la derniere image, et une carte au milieu
// (ses lignes ; puis ses boutons, dont les cadres reviennent dans `rects`).
// 1.11.15 : les boutons sont donnes (l'arret sur modification en a quatre) ; aucun :
// Demarrer l'IHM et Demarrer les deux. Trop larges pour une rangee : plusieurs.
static void paintStoppedNote(const ui::PaintContext& ctx, gfx::Rect b, const std::string& text,
                             const std::vector<HmiLiveCanvas::NoteButton>& given, std::vector<gfx::Rect>& rects,
                             std::vector<std::string>& actions) {
    rects.clear();
    actions.clear();
    if (text.empty()) return;
    static const std::vector<HmiLiveCanvas::NoteButton> kDefault = {
        {"D\xC3\xA9marrer l'IHM", "hmi.start", true}, {"D\xC3\xA9marrer les deux", "both.start", false}};
    static const std::vector<HmiLiveCanvas::NoteButton> kHmiOnly = {{"D\xC3\xA9marrer l'IHM", "hmi.start", true}};   // 1.12.0 : IHM Studio
    const auto& buttons = !given.empty() ? given : core::hasApi() ? kDefault : kHmiOnly;
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, gfx::Color{0, 0, 0, 96});
    const gfx::FontId font = ctx.theme.font.ui;
    float w = 0.f;
    std::vector<std::string> lines;
    for (std::size_t at = 0; at <= text.size();) {
        const auto nl = text.find('\n', at);
        lines.push_back(text.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        w = std::max(w, ctx.r.measure(lines.back(), font).width);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    // Les rangees de boutons : a la suite tant qu'ils tiennent dans la place.
    const float room = std::max(220.f, b.w - 96.f);
    std::vector<std::vector<std::size_t>> rows(1);
    std::vector<float> widths;
    float rowW = 0.f, widest = 0.f;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const float bw = ctx.r.measure(buttons[i].label, font).width + 28.f;
        widths.push_back(bw);
        if (!rows.back().empty() && rowW + 10.f + bw > room) {
            rows.emplace_back();
            rowW = 0.f;
        }
        rowW += (rows.back().empty() ? 0.f : 10.f) + bw;
        rows.back().push_back(i);
        widest = std::max(widest, rowW);
    }
    w = std::min(std::max(w, widest), std::max(w, room));
    const float lh = ctx.r.lineHeight(font) + 6.f;
    const float bh = 32.f;
    const float h = lh * static_cast<float>(lines.size()) + 32.f + static_cast<float>(rows.size()) * (bh + 8.f) + 4.f;
    const gfx::Rect card{b.x + (b.w - w - 48.f) / 2.f, b.y + (b.h - h) / 2.f, w + 48.f, h};
    ctx.r.fillRoundedRect(card, c.border, 10.f);
    ctx.r.fillRoundedRect({card.x + 1.f, card.y + 1.f, card.w - 2.f, card.h - 2.f}, c.panelBg, 9.f);
    float y = card.y + 16.f;
    for (std::size_t i = 0; i < lines.size(); ++i, y += lh) {
        ctx.r.drawText({card.x + 24.f, y}, lines[i], font, i == 0 ? c.text : c.textMuted);
        if (i == 0) ctx.r.drawText({card.x + 24.6f, y}, lines[i], font, c.text);
    }
    y += 6.f;
    rects.assign(buttons.size(), gfx::Rect{});
    for (const auto& row : rows) {
        float x = card.x + 24.f;
        for (const std::size_t i : row) {
            const gfx::Rect r{x, y, widths[i], bh};
            rects[i] = r;
            if (buttons[i].primary) {
                ctx.r.fillRoundedRect(r, c.accent, 6.f);
                ctx.r.drawText({r.x + 14.f, r.y + (bh - ctx.r.lineHeight(font)) / 2.f}, buttons[i].label, font, c.selectionText);
            } else {
                ctx.r.fillRoundedRect(r, c.border, 6.f);
                ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, 5.f);
                ctx.r.drawText({r.x + 14.f, r.y + (bh - ctx.r.lineHeight(font)) / 2.f}, buttons[i].label, font, c.text);
            }
            x += widths[i] + 10.f;
        }
        y += bh + 8.f;
    }
    for (const auto& bt : buttons) actions.push_back(bt.action);
}

// 1.10 : la vue zoomee qui depasse - la petite carte en bas a gauche (la vue
// entiere, la partie montree encadree) et l'indication des gestes en bas a
// droite (comme la maquette 1.10, scene 3).
static void paintZoomMap(const ui::PaintContext& ctx, gfx::Rect b, float viewW, float viewH, gfx::Rect shown) {
    if (viewW <= 0.f || viewH <= 0.f) return;
    const auto& c = ctx.theme.color;
    const float mw = 128.f, mh = mw * viewH / viewW;
    const gfx::Rect map{b.x + 12.f, b.bottom() - mh - 12.f, mw, mh};
    ctx.r.fillRoundedRect({map.x - 1.f, map.y - 1.f, map.w + 2.f, map.h + 2.f}, c.border, 4.f);
    ctx.r.fillRoundedRect(map, c.panelBg.withAlpha(230), 3.f);
    const float k = mw / viewW;
    const gfx::Rect seen{map.x + std::max(0.f, shown.x) * k, map.y + std::max(0.f, shown.y) * k,
                         std::min(viewW - std::max(0.f, shown.x), shown.w) * k, std::min(viewH - std::max(0.f, shown.y), shown.h) * k};
    ctx.r.fillRect(seen, c.accent.withAlpha(60));
    ctx.r.strokeRect(seen, c.accent, 1.5f);
    const std::string hint = "Ctrl+molette : zoom \xC2\xB7 glisser : d\xC3\xA9placer";
    const gfx::FontId font = ctx.theme.font.smallUi;
    const float hw = ctx.r.measure(hint, font).width + 16.f;
    const gfx::Rect pill{b.right() - hw - 12.f, b.bottom() - 34.f, hw, 22.f};
    ctx.r.fillRoundedRect(pill, gfx::Color{0, 0, 0, 150}, 11.f);
    ctx.r.drawText({pill.x + 8.f, pill.y + (pill.h - ctx.r.lineHeight(font)) / 2.f}, hint, font, gfx::Color{255, 255, 255, 230});
}

// 1.10 : le zoom de la vue en cours. Au changement d'echelle, le centre de la place
// garde son point de la vue (le deplacement suit) ; borne au dessin.
void HmiLiveCanvas::setDisplayZoom(float scale) {
    const auto b = bounds();
    zoomDisplayAround(scale <= 0.f ? 0.f : scale / std::max(0.01f, shownZoom_), {b.x + b.w / 2.f, b.y + b.h / 2.f});
    if (scale > 0.f) displayZoom_ = shownZoom_ = std::clamp(scale, kMinDisplayZoom, kMaxDisplayZoom);   // la valeur demandee, exacte
}

void HmiLiveCanvas::zoomDisplayAround(float factor, gfx::Point anchor) {
    const float before = shownZoom_ > 0.f ? shownZoom_ : 1.f;
    if (factor <= 0.f) {             // Ajuster : la vue entiere, au milieu
        displayZoom_ = 0.f;
        displayPan_ = {};
    } else {
        const float next = std::clamp(before * factor, kMinDisplayZoom, kMaxDisplayZoom);
        // Le point de la vue sous `anchor` y reste : o' = a - (a - o) * k, avec o
        // l'origine de la vue (centree + deplacement).
        const auto b = bounds();
        const float k = next / before;
        const float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f;
        const float ox = cx - shownViewW_ * before / 2.f + displayPan_.x;
        const float oy = cy - shownViewH_ * before / 2.f + displayPan_.y;
        float nx = anchor.x - (anchor.x - ox) * k, ny = anchor.y - (anchor.y - oy) * k;
        if (shownSx_ != 1.f || shownSy_ != 1.f) {
            // 1.12.24 : depuis la vue etiree (Remplir), le point sous `anchor` lu sur le dernier dessin.
            nx = anchor.x - static_cast<float>(vp_.toViewX(anchor.x)) * next;
            ny = anchor.y - static_cast<float>(vp_.toViewY(anchor.y)) * next;
        }
        displayPan_ = {nx - (cx - shownViewW_ * next / 2.f), ny - (cy - shownViewH_ * next / 2.f)};
        displayZoom_ = next;
        shownZoom_ = next;
        shownSx_ = shownSy_ = 1.f;   // un zoom choisi : une seule echelle
    }
    displayZoomChanged->emit();
    invalidate();
}

void HmiLiveCanvas::panDisplay(float dx, float dy) {
    displayPan_.x += dx;
    displayPan_.y += dy;
    invalidate();
}

gfx::Point HmiLiveCanvas::baseToScreen(double x, double y) const {
    return {baseOrigin_.x + static_cast<float>(x) * baseZoom_ * baseSx_, baseOrigin_.y + static_cast<float>(y) * baseZoom_ * baseSy_};
}

void HmiLiveCanvas::onPaint(const ui::PaintContext& ctx) {
    // 1.10 (decision 12) : en plein ecran, la vue ne se dessine que dans la passe du
    // dessus (le volet, onPaintOverlay, sans liste de passes) : pas deux fois par image.
    if (ctx.overlays)
        if (const auto* pane = dynamic_cast<const HmiSimulationPane*>(parent() ? parent()->parent() : nullptr); pane && pane->fullScreen())
            return;
    // Lot 13 : le dessin, mesure (l'onglet Performances, SYS.PaintTime).
    struct PaintTimer {
        double&                               ms;
        std::chrono::steady_clock::time_point t0{std::chrono::steady_clock::now()};
        std::size_t&                          count;
        ~PaintTimer() {
            ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++count;
        }
    } timer{lastPaintMs_, std::chrono::steady_clock::now(), paintCount_};
    lastPaintObjects_ = 0;
    for (const auto& l : layers_) lastPaintObjects_ += l.view.objects.size();
    const auto b = bounds();
    ctx.r.fillRect(b, station_ ? gfx::Color{0, 0, 0, 255} : ctx.theme.color.windowBg);
    vps_.clear();
    popupRects_.clear();
    simRects_.clear();
    qualityRects_.clear();
    keyboardRect_ = {};
    keys_.clear();
    if (layers_.empty() || layers_.front().view.width <= 0 || layers_.front().view.height <= 0) {
        paintStoppedNote(ctx, b, stoppedNote_, noteButtons_, noteRects_, noteActions_);   // 1.10 : l'IHM pas encore demarree
        return;
    }
    // CHAQUE VUE AJUSTEE A LA PLACE : pendant une navigation, celle qui part et
    // celle qui arrive peuvent ne pas avoir la meme taille (1280 x 800 vers
    // 1920 x 1080). Lot 8 : une popup est POSEE dans la vue du dessous (sa place
    // est en pixels de cette vue), a la meme echelle.
    const auto& v0 = layers_.front().view;
    const bool whole = wholeScreen();                // 1.12.24 : le plein ecran, le poste
    const float margin = whole ? 0.f : 24.f;          // lot 14 : le poste, sans marge (1.12.24 : le plein ecran aussi)
    // 1.12.24 : REMPLIR L'ECRAN - la vue du dessous etiree jusqu'aux quatre bords. Tout
    // se place a l'echelle commune (la plus petite) dans `fitArea`, puis s'etire de (sx, sy)
    // depuis le coin haut gauche de la place : vps_ le porte, les clics suivent.
    float sx = 1.f, sy = 1.f;
    if (whole && fit_ == hmi::ScreenFit::Fill && (station_ || displayZoom_ <= 0.f)) {
        const float zx = b.w / static_cast<float>(std::max(1, v0.width)), zy = b.h / static_cast<float>(std::max(1, v0.height));
        const float z0 = std::min(zx, zy);
        if (z0 > 0.f) {
            sx = std::max(1.f, zx / z0);
            sy = std::max(1.f, zy / z0);
        }
    }
    const gfx::Rect fitArea{b.x, b.y, b.w / sx, b.h / sy};
    const auto stretchX = [&](float x) { return b.x + (x - b.x) * sx; };
    const auto stretchY = [&](float y) { return b.y + (y - b.y) * sy; };
    // 1.10 : le zoom de la vue en cours (la barre) - 0 : ajustee ; sinon son echelle.
    const auto fit = [&](const hmi::View& v) {
        if (displayZoom_ > 0.f && !station_) return displayZoom_;
        if (whole && fit_ == hmi::ScreenFit::Actual) return 1.f;   // 1.12.24 : Taille reelle
        return std::max(0.05f, std::min((fitArea.w - margin) / static_cast<float>(std::max(1, v.width)),
                                        (fitArea.h - margin) / static_cast<float>(std::max(1, v.height))));
    };
    // 1.10 : une vue plus grande que la place se deplace ; le deplacement reste
    // borne (la vue ne quitte pas la place), nul quand elle tient.
    {
        const float s = fit(v0);
        const float vw = static_cast<float>(v0.width) * s, vh = static_cast<float>(v0.height) * s;
        overflowX_ = vw + margin > fitArea.w + 0.5f;
        overflowY_ = vh + margin > fitArea.h + 0.5f;
        const float mx = overflowX_ ? (vw + margin - fitArea.w) / 2.f : 0.f;
        const float my = overflowY_ ? (vh + margin - fitArea.h) / 2.f : 0.f;
        displayPan_ = {std::clamp(displayPan_.x, -mx, mx), std::clamp(displayPan_.y, -my, my)};
        shownZoom_ = s;
        shownSx_ = sx;
        shownSy_ = sy;
        shownViewW_ = static_cast<float>(v0.width);
        shownViewH_ = static_cast<float>(v0.height);
    }
    // Lot 12 : une transition en cours (deux vues) ne zoome pas.
    bool anim = false;
    for (const auto& l : layers_) anim = anim || (!l.popup && (l.frame.offsetX != 0 || l.frame.offsetY != 0 || l.frame.scale != 1 || l.frame.opacity < 1));
    if (!layers_.front().view.zoomable) zoomView_ = kNoId;
    // Lot 13 : les couleurs telles qu'on les voit (daltonien, jour) - celles du
    // dessin des objets et de la barre de titre des popups.
    const hmi::DisplayOptions shown = runtime_ ? runtime_->displayOptions() : hmi::DisplayOptions{};
    const hmi::DisplayOptions* display = hmi::changesColors(shown) ? &shown : nullptr;
    // 1.12.24 : sur tout l'ecran, le tour de la vue (les bandes) a sa couleur de fond, pas le vide.
    if (whole) {
        gfx::Color own;
        ctx.r.fillRect(b, tryParseColor(v0.background, own) ? own : hmiSeenColor(gfx::Color::rgb(0x20242B), display));
    }
    float baseZoom = fit(layers_.front().view);
    gfx::Rect baseRect{};
    gfx::Point baseOrigin{};
    // Les deplacements (barre, vue zoomable) sont en pixels d'ecran : dans fitArea, divises
    // par l'etirement.
    vp_.zoom = baseZoom;
    vp_.stretchX = sx;
    vp_.stretchY = sy;
    vp_.originX = stretchX(fitArea.x + (fitArea.w - static_cast<float>(v0.width) * baseZoom) / 2.f + displayPan_.x / sx);
    vp_.originY = stretchY(fitArea.y + (fitArea.h - static_cast<float>(v0.height) * baseZoom) / 2.f + displayPan_.y / sy);
    ctx.r.pushClip(b);
    for (std::size_t li = 0; li < layers_.size(); ++li) {
        const auto& layer = layers_[li];
        const auto& v = layer.view;
        float z = 0.f, ox = 0.f, oy = 0.f;
        const float titleH = layer.popup && layer.titleBar ? static_cast<float>(hmi::kPopupTitleHeight) : 0.f;
        if (!layer.popup) {
            z = fit(v);
            // Lot 12 : une vue qu'on peut zoomer, a son zoom et a sa place (molette,
            // fond tire) ; revenue a 100 % quand on change de vue.
            float pan_x = 0.f, pan_y = 0.f;
            if (v.zoomable && !anim) {
                if (zoomView_ != v.id) {
                    zoomView_ = v.id;
                    userZoom_ = 1.f;
                    userPan_ = {};
                }
                z *= userZoom_;
                pan_x = userPan_.x;
                pan_y = userPan_.y;
            }
            ox = fitArea.x + (fitArea.w - static_cast<float>(v.width) * z) / 2.f + (pan_x + displayPan_.x) / sx;
            oy = fitArea.y + (fitArea.h - static_cast<float>(v.height) * z) / 2.f + (pan_y + displayPan_.y) / sy;
            baseZoom = z;
            baseOrigin = {ox, oy};
            baseRect = {stretchX(ox), stretchY(oy), static_cast<float>(v.width) * z * sx, static_cast<float>(v.height) * z * sy};
        } else {
            z = baseZoom;
            if (layer.slot != -1) {      // -2 : une popup qui se ferme, a sa derniere place
                ox = baseOrigin.x + static_cast<float>(layer.px) * z;
                oy = baseOrigin.y + (static_cast<float>(layer.py) + titleH) * z;
            } else {
                ox = fitArea.x + (fitArea.w - static_cast<float>(v.width) * z) / 2.f;
                oy = fitArea.y + (fitArea.h - static_cast<float>(v.height) * z) / 2.f;
            }
            // Le voile : la vue du dessous s'assombrit sous une popup modale.
            if (layer.modal && layer.veil > 0.f)
                ctx.r.fillRect(baseRect, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(std::clamp(layer.veil, 0.f, 1.f) * 150.f)});
        }
        // La transition : decalage (en taille de vue), echelle et angle autour du centre.
        const float w = static_cast<float>(v.width) * z, h = static_cast<float>(v.height) * z;
        const float cx = ox + w / 2.f + static_cast<float>(layer.frame.offsetX) * w;
        const float cy = oy + h / 2.f + static_cast<float>(layer.frame.offsetY) * h;
        // 1.12.24 : a l'ecran, le centre etire, la vue de part et d'autre a l'echelle de chaque axe.
        HmiViewport vp;
        vp.zoom = z * static_cast<float>(layer.frame.scale);
        vp.stretchX = sx;
        vp.stretchY = sy;
        vp.originX = stretchX(cx) - static_cast<float>(v.width) * vp.zoomX() / 2.f;
        vp.originY = stretchY(cy) - static_cast<float>(v.height) * vp.zoomY() / 2.f;
        vp.angle = static_cast<float>(layer.frame.angle);
        vp.pivotX = stretchX(cx);
        vp.pivotY = stretchY(cy);
        vps_.push_back(vp);
        if (layer.frame.opacity <= 0.001) continue;
        const float op = static_cast<float>(layer.frame.opacity);
        HmiPaintOptions opt;
        opt.editor = false;
        opt.time = ctx.time;
        opt.assets = assets_;
        opt.alpha = op;
        opt.runtime = runtime_;
        opt.history = history_;
        opt.project = project_;
        opt.display = display;
        const gfx::Point a = vp.toScreen(0, 0);
        const float vw = static_cast<float>(v.width) * vp.zoomX(), vh = static_cast<float>(v.height) * vp.zoomY();
        const float tb = titleH * vp.zoomY();
        if (layer.popup && vp.angle == 0.f) {
            // Une ombre ; la barre de titre au-dessus du contenu.
            ctx.r.fillRect({a.x + 6, a.y - tb + 8, vw, vh + tb}, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(90 * op)});
            if (tb > 0.f) {
                const gfx::Rect bar{a.x, a.y - tb, vw, tb};
                ctx.r.fillRect(bar, faded(hmiSeenColor(gfx::Color::rgb(0x273142), display), op));
                const auto& f = ctx.theme.font;
                const float gs = std::min(tb - 10.f, 18.f);
                drawHmiGlyph(ctx.r, HmiGlyph::Popup, {bar.x + 8.f, bar.y + (tb - gs) / 2.f, gs, gs}, faded(ctx.theme.color.accent, op));
                const std::string title = layer.title.empty() ? v.name : layer.title;
                const float room = vw - gs - 20.f - (layer.closeButton ? tb : 0.f);
                const auto chars = ctx.r.fitCharacters(title, f.ui, std::max(0.f, room));
                ctx.r.drawText({bar.x + gs + 14.f, bar.y + (tb - ctx.r.lineHeight(f.ui)) / 2.f}, std::string_view(title).substr(0, chars),
                               f.ui, faded(hmiSeenColor(gfx::Color::rgb(0xE6EAF0), display), op));
                gfx::Rect close{};
                if (layer.closeButton) {
                    close = {bar.right() - tb, bar.y, tb, tb};
                    const float m = tb * 0.32f;
                    const gfx::Color xc = faded(hmiSeenColor(gfx::Color::rgb(0xC8D0DC), display), op);
                    ctx.r.line({close.x + m, close.y + m}, {close.right() - m, close.bottom() - m}, xc, 1.6f);
                    ctx.r.line({close.right() - m, close.y + m}, {close.x + m, close.bottom() - m}, xc, 1.6f);
                }
                if (layer.slot >= 0) {
                    PopupRect pr;
                    pr.window = {a.x, a.y - tb, vw, vh + tb};
                    pr.title = {bar.x, bar.y, bar.w - (layer.closeButton ? tb : 0.f), tb};
                    pr.close = close;
                    pr.content = {a.x, a.y, vw, vh};
                    pr.slot = layer.slot;
                    pr.modal = layer.modal;
                    pr.movable = layer.movable;
                    pr.closeOutside = layer.closeOutside;
                    pr.layer = li;
                    popupRects_.push_back(pr);
                }
            } else if (layer.slot >= 0) {
                PopupRect pr;
                pr.window = pr.content = {a.x, a.y, vw, vh};
                pr.slot = layer.slot;
                pr.modal = layer.modal;
                pr.closeOutside = layer.closeOutside;
                pr.layer = li;
                popupRects_.push_back(pr);
            }
        }
        paintHmiView(ctx.r, v, vp, HmiPropertySource{}, ctx.theme, opt);
        if (layer.popup && vp.angle == 0.f)
            ctx.r.strokeRect({a.x, a.y - tb, vw, vh + tb}, faded(ctx.theme.color.accent, op), 1.5f);
        // Un objet que l'utilisateur connecte ne peut pas actionner : un cadenas
        // dans son coin, et l'objet un peu voile.
        for (Id id : layer.locked) {
            const auto* o = v.object(id);
            if (!o || layer.frame.opacity < 0.5) continue;
            const auto box = o->box();
            const auto p0 = vp.toScreen(box.x, box.y), e = vp.toScreen(box.x + box.w, box.y + box.h);
            ctx.r.fillRect({p0.x, p0.y, e.x - p0.x, e.y - p0.y}, gfx::Color{10, 12, 16, 90});
            const float s = std::clamp(std::min(e.x - p0.x, e.y - p0.y) * 0.45f, 12.f, 22.f);
            const gfx::Rect badge{e.x - s - 3.f, p0.y + 3.f, s, s};
            ctx.r.fillRect(badge, gfx::Color{20, 24, 30, 220});
            ctx.r.strokeRect(badge, gfx::Color::rgb(0xF2994A), 1.f);
            drawHmiGlyph(ctx.r, HmiGlyph::Lock, {badge.x + 2.f, badge.y + 2.f, badge.w - 4.f, badge.h - 4.f}, gfx::Color::rgb(0xF2994A));
        }
        // Lot 14 : relie a un automate reel, un objet dont une valeur n'est pas
        // bonne. Ancienne (la liaison est perdue, la derniere valeur reste) : un
        // cadre orange en tirets et une horloge ; mauvaise (jamais lue, refusee,
        // sans adresse, perdue depuis trop longtemps) : l'objet voile, un cadre
        // rouge en tirets et une croix.
        for (const auto& [qid, level] : layer.quality) {
            const auto* o = v.object(qid);
            if (!o || layer.frame.opacity < 0.5) continue;
            const auto box = o->box();
            const auto p0 = vp.toScreen(box.x, box.y), e = vp.toScreen(box.x + box.w, box.y + box.h);
            const gfx::Rect rr{std::min(p0.x, e.x) - 3.f, std::min(p0.y, e.y) - 3.f, std::fabs(e.x - p0.x) + 6.f, std::fabs(e.y - p0.y) + 6.f};
            const gfx::Color col = level >= 2 ? gfx::Color::rgb(0xE5534B) : gfx::Color::rgb(0xF2994A);
            if (level >= 2) ctx.r.fillRect(rr, gfx::Color{14, 16, 20, 125});
            dashedRect(ctx.r, rr, col, 1.6f);
            // Le badge sur le coin haut droit, a cheval : il ne cache ni le libelle
            // (en haut a gauche) ni la valeur.
            const float s = std::clamp(std::min(rr.w, rr.h) * 0.42f, 13.f, 20.f);
            const gfx::Rect badge{rr.right() - s * 0.5f, rr.y - s * 0.5f, s, s};
            ctx.r.fillRoundedRect({badge.x - 1.5f, badge.y - 1.5f, badge.w + 3.f, badge.h + 3.f}, col, s / 2 + 1.5f);
            ctx.r.fillRoundedRect(badge, gfx::Color{20, 24, 30, 240}, s / 2);
            if (level >= 2) {
                const float m = s * 0.3f;
                ctx.r.line({badge.x + m, badge.y + m}, {badge.right() - m, badge.bottom() - m}, col, 2.f);
                ctx.r.line({badge.right() - m, badge.y + m}, {badge.x + m, badge.bottom() - m}, col, 2.f);
            } else {
                drawHmiGlyph(ctx.r, HmiGlyph::Clock, {badge.x + 2.f, badge.y + 2.f, badge.w - 4.f, badge.h - 4.f}, col);
            }
            // 1.11.1 (decision 134, R1111-12) : la marque dit pourquoi et le remede
            // (decision 122), au survol de l'objet ou de sa pastille.
            for (const auto& [tid, tip] : layer.qualityTips)
                if (tid == qid && !tip.empty()) {
                    const gfx::Rect area{rr.x, badge.y, std::max(rr.right(), badge.right()) - rr.x, rr.bottom() - badge.y};
                    qualityRects_.push_back({area, level >= 2 ? "Valeur mauvaise" : "Valeur ancienne", tip});
                    break;
                }
        }
        // 1.9 : LES LECTURES SIMULEES - une valeur lue sur l'esclave simule de son
        // equipement : un cadre violet en tirets (au-dela de celui de la qualite) et
        // une pastille a fiole sur le coin haut droit (a cote de celle de la qualite).
        for (const auto& sr : layer.simulated) {
            const auto* o = v.object(sr.object);
            if (!o || layer.frame.opacity < 0.5) continue;
            const auto box = o->box();
            const auto p0 = vp.toScreen(box.x, box.y), e = vp.toScreen(box.x + box.w, box.y + box.h);
            const gfx::Rect rr{std::min(p0.x, e.x), std::min(p0.y, e.y), std::fabs(e.x - p0.x), std::fabs(e.y - p0.y)};
            const bool q = std::any_of(layer.quality.begin(), layer.quality.end(), [&](const auto& x) { return x.first == sr.object; });
            simmark::drawMark(ctx.r, rr, q, static_cast<float>(layer.frame.opacity));
            simRects_.emplace_back(simmark::markFrame(rr), sr.tip);
        }
        // Un objet dont une expression ne s'evalue pas : un coin rouge, sans le
        // cacher - on doit pouvoir le trouver sur la vue.
        for (Id id : layer.errors) {
            const auto* o = v.object(id);
            if (!o) continue;
            const auto box = o->box();
            const auto p = vp.toScreen(box.x, box.y);
            ctx.r.fillRect({p.x - 3.f, p.y - 3.f, 10.f, 10.f}, faded(gfx::Color::rgb(0xE5534B), static_cast<float>(layer.frame.opacity)));
        }
    }
    baseZoom_ = baseZoom;
    baseSx_ = sx;
    baseSy_ = sy;
    baseOrigin_ = {stretchX(baseOrigin.x), stretchY(baseOrigin.y)};
    screen_ = baseRect;
    // 1.9 : le bandeau LECTURES SIMULEES, en haut de l'ecran de l'IHM, par-dessus la
    // vue ; l'infobulle d'un objet repere sous la souris.
    // Sur le poste : tout en haut, sur toute la largeur (la maquette M4) ; dans
    // l'application : en haut de l'ecran de l'IHM (M3).
    if (!simRibbon_.empty() && screen_.w > 80.f) {
        // Dans l'application, la place au-dessus de l'ecran de l'IHM (la vue centree) :
        // le bandeau s'y pose, colle a l'ecran, sans cacher le haut de la vue.
        const float above = screen_.y - b.y >= simmark::kRibbonH + 2.f ? simmark::kRibbonH : 0.f;
        const gfx::Rect area = station_ ? gfx::Rect{b.x, b.y, b.w, simmark::kRibbonH} : gfx::Rect{screen_.x, screen_.y - above, screen_.w, simmark::kRibbonH};
        simmark::drawRibbon(ctx.r, area, simRibbon_, simRibbonRight_);
    }
    if (const std::string tip = simTipAt(mouse_); !tip.empty())
        simmark::drawTip(ctx.r, mouse_, b, "Lue sur l'esclave simul\xC3\xA9", tip);
    else
        // 1.11.1 (decision 134, R1111-12) : l'infobulle d'une marque de qualite.
        for (auto it = qualityRects_.rbegin(); it != qualityRects_.rend(); ++it)
            if (it->rect.contains(mouse_)) {
                simmark::drawTip(ctx.r, mouse_, b, it->title, it->body);
                break;
            }
    // Lot 13 : le doigt d'un essai - un cercle sur l'objet que le pas vient de
    // toucher, qui grandit et s'efface.
    if (touch_ != hmi::kNoId && touchStrength_ > 0.f) {
        for (std::size_t li = layers_.size(); li-- > 0;) {
            const auto* o = layers_[li].view.object(touch_);
            if (!o || li >= vps_.size()) continue;
            const auto box = o->box();
            const gfx::Point c = vps_[li].toScreen(box.x + box.w / 2, box.y + box.h / 2);
            const float k = touchStrength_;
            const float r1 = 22.f + (1.f - k) * 16.f, r2 = 8.f;
            const gfx::Color accent = ctx.theme.color.accent;
            ctx.r.fillRoundedRect({c.x - r1, c.y - r1, 2 * r1, 2 * r1}, gfx::Color{accent.r, accent.g, accent.b, static_cast<std::uint8_t>(90.f * k)}, r1);
            ctx.r.fillRoundedRect({c.x - r2, c.y - r2, 2 * r2, 2 * r2}, gfx::Color{255, 255, 255, static_cast<std::uint8_t>(200.f * k)}, r2);
            break;
        }
    }
    // 1.12.24 : la fenetre du Reglage PID, par-dessus la vue (sous la connexion et la signature).
    pidLayout_.reset();
    if (runtime_ && runtime_->pidShown()) pidLayout_ = paintPidPanel(ctx.r, ctx.theme, *runtime_, baseRect);
    // Lot 12 : le menu natif de connexion, par-dessus la vue ; le clavier virtuel
    // de son champ vient ensuite, par-dessus lui (le menu se range au-dessus).
    loginRect_ = {};
    loginLayout_.reset();
    if (runtime_ && runtime_->loginShown()) {
        float areaH = baseRect.h;
        const std::string mode = runtime_->keyboardMode();
        if (!mode.empty()) {
            const auto size = hmi::keyboardSize(mode);
            const float kh = std::min(b.h - 16.f, static_cast<float>(size.h));
            areaH = std::max(160.f, b.bottom() - kh - 10.f - baseRect.y - 4.f);
        }
        loginLayout_ = paintLoginMenu(ctx.r, ctx.theme, *runtime_, baseRect, areaH);
        loginTabShown_ = runtime_->loginTab();
        const auto& pl = loginLayout_->panel;
        loginRect_ = {baseRect.x + static_cast<float>(pl.x), baseRect.y + static_cast<float>(pl.y), static_cast<float>(pl.w),
                      static_cast<float>(pl.h)};
    }
    // Lot 13 : le panneau de signature (par-dessus tout, le clavier excepte) et
    // l'avertissement de la deconnexion automatique (en haut de l'ecran).
    signatureRect_ = {};
    signLayout_.reset();
    if (runtime_ && runtime_->signatureShown()) {
        float areaH = baseRect.h;
        const std::string mode = runtime_->keyboardMode();
        if (!mode.empty()) {
            const auto size = hmi::keyboardSize(mode);
            const float kh = std::min(b.h - 16.f, static_cast<float>(size.h));
            areaH = std::max(160.f, b.bottom() - kh - 10.f - baseRect.y - 4.f);
        }
        signLayout_ = paintSignaturePanel(ctx.r, ctx.theme, *runtime_, baseRect, areaH);
        const auto& pl = signLayout_->panel;
        signatureRect_ = {baseRect.x + static_cast<float>(pl.x), baseRect.y + static_cast<float>(pl.y), static_cast<float>(pl.w),
                          static_cast<float>(pl.h)};
    }
    // 1.11.7 : le champ de saisie de l'action Clavier virtuel (au-dessus du clavier).
    promptRect_ = {};
    promptLayout_.reset();
    if (runtime_ && runtime_->promptShown()) {
        float areaH = baseRect.h;
        const std::string mode = runtime_->keyboardMode();
        if (!mode.empty()) {
            const auto size = hmi::keyboardSize(mode);
            const float kh = std::min(b.h - 16.f, static_cast<float>(size.h));
            areaH = std::max(160.f, b.bottom() - kh - 10.f - baseRect.y - 4.f);
        }
        promptLayout_ = paintPromptPanel(ctx.r, ctx.theme, *runtime_, baseRect, areaH);
        const auto& pl = promptLayout_->panel;
        promptRect_ = {baseRect.x + static_cast<float>(pl.x), baseRect.y + static_cast<float>(pl.y), static_cast<float>(pl.w),
                       static_cast<float>(pl.h)};
    }
    warning_ = runtime_ ? paintLogoutWarning(ctx.r, ctx.theme, *runtime_, baseRect) : LogoutWarningRects{};
    // Lot 8 : le clavier virtuel du champ qui a le focus (s'il le demande).
    paintKeyboard(ctx);
    // Lot 10 : ce que l'IHM dessine d'elle-meme, dans son ECRAN (le cadre de la
    // vue courante) : le menu natif Parametres systeme, l'ecran en veille, le
    // voile de la luminosite (sur tout, le menu compris : c'est l'ecran).
    systemRect_ = {};
    systemLayout_.reset();
    if (runtime_) {
        if (runtime_->systemShown()) {
            // 1.9 : la page Simulation - les esclaves, relus a leur rythme.
            const bool simPage = runtime_->systemTab() == hmi::kSimulationTab;
            if (simPage) refreshSimSlaves();
            // 1.9 : la page Simulation prend tout le canevas quand la vue y est reduite
            // (Simuler l'IHM : la vue a l'echelle, a cote des tableaux) ; sur le poste,
            // la vue remplit l'ecran : rien ne change.
            systemScreen_ = baseRect;
            if (simPage && (b.w > baseRect.w + 1.f || b.h > baseRect.h + 1.f)) systemScreen_ = b.inset(6.f, 6.f);
            systemLayout_ = paintSystemMenu(ctx.r, ctx.theme, *runtime_, systemScreen_, simPage ? &simSlaves_ : nullptr);
            systemTabShown_ = runtime_->systemTab();
            const auto& pl = systemLayout_->panel;
            systemRect_ = {systemScreen_.x + static_cast<float>(pl.x), systemScreen_.y + static_cast<float>(pl.y), static_cast<float>(pl.w),
                           static_cast<float>(pl.h)};
        }
        if (runtime_->asleep(runtime_->now())) paintSleepScreen(ctx.r, ctx.theme, baseRect);
        else paintBrightness(ctx.r, baseRect, runtime_->settings().brightness);
    }
    // 1.10 : la vue zoomee qui depasse - la petite carte et l'indication des gestes.
    if (viewOverflows() && stoppedNote_.empty() && !station_ && vp_.zoom > 0.f)
        paintZoomMap(ctx, b, shownViewW_, shownViewH_,
                     {static_cast<float>(vp_.toViewX(b.x)), static_cast<float>(vp_.toViewY(b.y)), b.w / vp_.zoomX(), b.h / vp_.zoomY()});
    paintStoppedNote(ctx, b, stoppedNote_, noteButtons_, noteRects_, noteActions_);   // 1.10 : l'IHM arretee
    ctx.r.popClip();
}

bool HmiLiveCanvas::systemPartRect(std::string_view part, gfx::Rect& out) const {
    if (!systemLayout_) return false;
    const gfx::Rect& sc = systemScreen_;      // 1.9 : la page Simulation peut prendre tout le canevas
    if (part == "dehors") {
        // Un point de l'ecran hors du panneau (en haut a gauche).
        out = {sc.x + 2.f, sc.y + 2.f, std::max(2.f, systemRect_.x - sc.x - 4.f), 8.f};
        return systemRect_.x - sc.x > 6.f;
    }
    hmi::Box b{};
    if (!hmi::systemMenuPartBox(*systemLayout_, systemTabShown_, part, b)) return false;
    out = {sc.x + static_cast<float>(b.x), sc.y + static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)};
    return true;
}

bool HmiLiveCanvas::pidPartRect(std::string_view part, gfx::Rect& out) const {
    if (!pidLayout_) return false;
    for (const auto& [name, r] : pidLayout_->parts)
        if (name == part) {
            out = r;
            return true;
        }
    return false;
}

bool HmiLiveCanvas::signaturePartRect(std::string_view part, gfx::Rect& out) const {
    if (!signLayout_) return false;
    if (part == "dehors") {
        out = {screen_.x + 2.f, screen_.y + 2.f, std::max(2.f, signatureRect_.x - screen_.x - 4.f), 8.f};
        return signatureRect_.x - screen_.x > 6.f;
    }
    hmi::Box b{};
    if (!hmi::signaturePartBox(*signLayout_, part, b)) return false;
    out = {screen_.x + static_cast<float>(b.x), screen_.y + static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)};
    return true;
}

bool HmiLiveCanvas::promptPartRect(std::string_view part, gfx::Rect& out) const {
    if (!promptLayout_) return false;
    hmi::Box b{};
    if (!hmi::promptPartBox(*promptLayout_, part, b)) return false;
    out = {screen_.x + static_cast<float>(b.x), screen_.y + static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)};
    return true;
}

bool HmiLiveCanvas::loginPartRect(std::string_view part, gfx::Rect& out) const {
    if (!loginLayout_) return false;
    if (part == "dehors") {
        out = {screen_.x + 2.f, screen_.y + 2.f, std::max(2.f, loginRect_.x - screen_.x - 4.f), 8.f};
        return loginRect_.x - screen_.x > 6.f;
    }
    hmi::Box b{};
    if (!hmi::loginMenuPartBox(*loginLayout_, loginTabShown_, part, b)) return false;
    out = {screen_.x + static_cast<float>(b.x), screen_.y + static_cast<float>(b.y), static_cast<float>(b.w), static_cast<float>(b.h)};
    return true;
}

std::string HmiLiveCanvas::loginScrollPart(long delta) const {
    if (!runtime_ || !loginLayout_) return {};
    const long maxScroll = static_cast<long>(loginMaxScroll(*runtime_, *loginLayout_));
    const long next = std::clamp(static_cast<long>(runtime_->loginScroll()) + delta, 0L, maxScroll);
    return "defiler:=" + std::to_string(next);
}

void HmiLiveCanvas::pressKeyboard(gfx::Point p) {
    const int k = hmi::keyboardHit(keys_, p.x - keyboardRect_.x, p.y - keyboardRect_.y);
    if (k < 0) return;
    const auto& key = keys_[static_cast<std::size_t>(k)];
    if (key.command == "maj") shift_ = !shift_;
    else if (key.command == "retour") keyTyped->emit(static_cast<int>(hmi::EditKey::Backspace));
    else if (key.command == "entree") keyTyped->emit(static_cast<int>(hmi::EditKey::Enter));
    else if (key.command == "echap") keyTyped->emit(static_cast<int>(hmi::EditKey::Escape));
    else if (key.command == "gauche") keyTyped->emit(static_cast<int>(hmi::EditKey::Left));
    else if (key.command == "droite") keyTyped->emit(static_cast<int>(hmi::EditKey::Right));
    else if (!key.text.empty()) {
        textTyped->emit(key.text);
        shift_ = false;
    }
}

void HmiLiveCanvas::refreshSimSlaves() {
    if (!runtime_) return;
    if (!runtime_->simPageAllowed()) {
        simSlaves_.clear();            // rien des esclaves sans la permission
        simSlavesAt_ = -1;
        return;
    }
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (simSlavesAt_ >= 0 && now - simSlavesAt_ < 0.25 && simSlavesRev_ == runtime_->simRevision()) return;
    simSlaves_ = runtime_->simSlaves(true);
    simSlavesAt_ = now;
    simSlavesRev_ = runtime_->simRevision();
}

std::string HmiLiveCanvas::simListScrollPart(long delta) const {
    if (!runtime_ || !systemLayout_) return {};
    const auto& s = systemLayout_->sim;
    const long next = std::clamp(static_cast<long>(s.listFirst) + delta, 0L, static_cast<long>(s.listMax));
    return "defiler_liste:=" + std::to_string(next);
}

std::string HmiLiveCanvas::systemScrollPart(long delta) const {
    if (!runtime_ || !systemLayout_) return {};
    if (systemTabShown_ == hmi::kSimulationTab) {
        // 1.9 : les lignes de l'esclave choisi.
        const auto& s = systemLayout_->sim;
        const long next = std::clamp(static_cast<long>(s.first) + delta, 0L, static_cast<long>(s.maxScroll));
        return "defiler:=" + std::to_string(next);
    }
    std::size_t maxScroll = 0;
    (void)hmi::diagLines(runtime_->diagnostics(), *systemLayout_, runtime_->systemScroll(), &maxScroll);
    const long next = std::clamp(static_cast<long>(runtime_->systemScroll()) + delta, 0L, static_cast<long>(maxScroll));
    return "defiler:=" + std::to_string(next);
}

Id HmiLiveCanvas::objectAtLayer(std::size_t layer, gfx::Point p) const {
    if (layer >= layers_.size() || vps_.size() != layers_.size()) return kNoId;
    const auto& top = layers_[layer];
    const auto& vp = vps_[layer];
    const double x = vp.toViewX(p.x), y = vp.toViewY(p.y);
    const auto order = top.view.paintOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const auto* o = *it;
        if (o->kind == hmi::Kind::Group || !o->flag("visible", true)) continue;
        const auto box = o->box();
        // Lot 12 : un enfant hors de la page, du contenu d'un panneau (decale) ne se clique pas.
        if (x >= box.x && x <= box.x + box.w && y >= box.y && y <= box.y + box.h && !hmi::clippedAt(top.view, *o, x, y)) return o->id;
    }
    return kNoId;
}

// 1.10.2 : la couche du dessus QUI VIT - pas une popup qui se ferme (place -2,
// dessinee en fondu a sa derniere place : elle n'est plus dans l'IHM). Avant, un
// clic hors de toute popup allait a la derniere couche, c'est-a-dire a la popup
// fermee pendant son fondu : la vue du dessous ne repondait plus (defaut du
// client de 11 h 53, « tout reste fige »).
std::size_t HmiLiveCanvas::liveTopLayer() const {
    for (std::size_t li = layers_.size(); li-- > 0;)
        if (!(layers_[li].popup && layers_[li].slot == -2)) return li;
    return layers_.empty() ? 0 : layers_.size() - 1;
}

Id HmiLiveCanvas::objectAt(gfx::Point p) const {
    if (layers_.empty()) return kNoId;
    return objectAtLayer(liveTopLayer(), p);
}

bool HmiLiveCanvas::objectRect(std::string_view name, gfx::Rect& out) const {
    if (layers_.empty() || vps_.size() != layers_.size()) return false;
    // Du dessus vers le dessous : la popup d'abord (lot 8 : la vue dessous aussi).
    for (std::size_t li = layers_.size(); li-- > 0;) {
        const auto& vp = vps_[li];
        for (const auto& o : layers_[li].view.objects)
            if (o.name == name) {
                const auto box = o.box();
                const auto a = vp.toScreen(box.x, box.y), c = vp.toScreen(box.x + box.w, box.y + box.h);
                out = {a.x, a.y, c.x - a.x, c.y - a.y};
                return true;
            }
    }
    return false;
}

bool HmiLiveCanvas::partRect(std::string_view name, std::string_view part, gfx::Rect& out) const {
    if (layers_.empty() || vps_.size() != layers_.size()) return false;
    for (std::size_t li = layers_.size(); li-- > 0;) {
        const auto& vp = vps_[li];
        for (const auto& o : layers_[li].view.objects) {
            if (o.name != name) continue;
            const auto box = o.box();
            hmi::Box local{};
            bool found = false;
            if (o.kind == hmi::Kind::RecipeManager || o.kind == hmi::Kind::UserManager) {
                const auto l = hmi::recipeManagerLayout(o, box.w, box.h);
                if (part.rfind("bouton:", 0) == 0) {
                    for (const auto& bt : l.buttons)
                        if (bt.label == part.substr(7)) { local = bt.box; found = true; }
                } else if (part.rfind("ligne:", 0) == 0) {
                    const double row = std::atof(std::string(part.substr(6)).c_str());
                    local = {0, l.table.y + l.headerH + l.rowH * row, box.w, l.rowH};
                    found = true;
                }
            } else if (o.kind == hmi::Kind::LoginPanel || o.kind == hmi::Kind::PasswordChange) {
                const auto l = o.kind == hmi::Kind::LoginPanel ? hmi::loginLayout(o, box.w, box.h) : hmi::passwordLayout(o, box.w, box.h);
                if (part == "bouton") { local = l.button; found = true; }
                else if (part == "precedent") { local = l.prev; found = l.prev.w > 0; }
                else if (part == "suivant") { local = l.next; found = l.next.w > 0; }
                else if (part.rfind("champ:", 0) == 0)
                    if (const auto* f = l.field(part.substr(6))) { local = f->box; found = true; }
            } else if (o.kind == hmi::Kind::InputField || o.kind == hmi::Kind::LogoutButton) {
                local = {0, 0, box.w, box.h};
                found = true;
            } else {
                // Lot 9 : les parties des commandes, pour les scripts et les tests.
                const auto index = [&](std::string_view prefix) {
                    return part.rfind(prefix, 0) == 0 ? std::atoi(std::string(part.substr(prefix.size())).c_str()) : -1;
                };
                switch (o.kind) {
                    case hmi::Kind::Selector: {
                        const auto l = hmi::selectorLayout(o, box.w, box.h, hmi::choicesOf(o).size());
                        const int i = index("position:");
                        if (i >= 0 && static_cast<std::size_t>(i) < l.labels.size()) { local = l.labels[static_cast<std::size_t>(i)]; found = true; }
                        else if (part == "suivant" && l.rotary) { local = {l.cx - l.radius * 0.5, l.cy - l.radius * 0.5, l.radius, l.radius}; found = true; }
                        break;
                    }
                    case hmi::Kind::RadioGroup: {
                        const auto boxes = hmi::radioBoxes(o, box.w, box.h, hmi::choicesOf(o).size());
                        const int i = index("option:");
                        if (i >= 0 && static_cast<std::size_t>(i) < boxes.size()) { local = boxes[static_cast<std::size_t>(i)]; found = true; }
                        break;
                    }
                    case hmi::Kind::ComboBox: {
                        if (part == "ouvrir") { local = {0, 0, box.w, box.h}; found = true; break; }
                        std::size_t first = 0;
                        if (!runtime_ || !runtime_->comboOpen(o.id, &first)) break;
                        const auto l = hmi::comboLayout(o, box.w, box.h, hmi::choicesOf(o).size(), first);
                        const int i = index("choix:");
                        if (i >= static_cast<int>(l.first) && static_cast<std::size_t>(i) < l.first + l.rows.size()) {
                            local = l.rows[static_cast<std::size_t>(i) - l.first];
                            found = true;
                        } else if (part == "defiler:-1" && l.up.w > 0) { local = l.up; found = true; }
                        else if (part == "defiler:1" && l.down.w > 0) { local = l.down; found = true; }
                        break;
                    }
                    case hmi::Kind::List: {
                        // 1.12.2 : la liste - ses lignes montrees, ses bandes de defilement.
                        const auto l = hmi::listLayout(o, box.w, box.h, hmi::choicesOf(o).size(), runtime_ ? runtime_->listFirst(o.id) : 0);
                        const int i = index("choix:");
                        if (i >= static_cast<int>(l.first) && static_cast<std::size_t>(i) < l.first + l.rows.size()) {
                            local = l.rows[static_cast<std::size_t>(i) - l.first];
                            found = true;
                        } else if (part == "defiler:-1" && l.up.w > 0) { local = l.up; found = true; }
                        else if (part == "defiler:1" && l.down.w > 0) { local = l.down; found = true; }
                        break;
                    }
                    case hmi::Kind::DocumentViewer: {
                        // 1.12.30 : la visionneuse de documents - un bouton ("doc:next"), un champ ("doc:page",
                        // "doc:zoommenu", "doc:searchfield"), un choix du menu des zooms ("doc:zoom:4"), une vignette
                        // ("doc:thumb:3"), la zone des pages ("doc:area"), telle que le moteur l'a disposee.
                        const auto* d = runtime_ ? runtime_->docViewer(o.id) : nullptr;
                        if (!d) break;
                        const auto& l = d->layout;
                        for (const auto& b : l.buttons)
                            if (b.part == part) { local = b.box; found = true; }
                        if (found) break;
                        if (part == "doc:page") { local = l.pageField; found = l.pageField.w > 0; }
                        else if (part == "doc:zoommenu") { local = l.zoomField; found = l.zoomField.w > 0; }
                        else if (part == "doc:searchfield") { local = l.searchField; found = l.searchField.w > 0; }
                        else if (part == "doc:area") { local = l.area; found = l.area.w > 0; }
                        else if (const int z = index("doc:zoom:"); z >= 0 && static_cast<std::size_t>(z) < l.zoomMenu.size()) {
                            local = l.zoomMenu[static_cast<std::size_t>(z)];
                            found = true;
                        } else if (const int t = index("doc:thumb:"); t >= 1) {
                            const auto thumbs = hmi::docview::thumbsOf(l, d->state, d->sizes);
                            if (static_cast<std::size_t>(t) <= thumbs.size()) {
                                local = thumbs[static_cast<std::size_t>(t) - 1].frame;
                                found = true;
                            }
                        }
                        break;
                    }
                    case hmi::Kind::DateTimePicker: {
                        const auto l = hmi::pickerLayout(o, box.w, box.h);
                        if (part == "maintenant") { local = l.now; found = true; }
                        else if (part == "valider") { local = l.ok; found = true; }
                        for (const auto& f : l.fields) {
                            if (part == "plus:" + f.name) { local = f.up; found = true; }
                            if (part == "moins:" + f.name) { local = f.down; found = true; }
                        }
                        break;
                    }
                    case hmi::Kind::WeeklySchedule: {
                        const auto* ws = runtime_ ? runtime_->schedule(o.id) : nullptr;
                        const auto l = hmi::scheduleLayout(o, box.w, box.h, ws ? ws->slotMinutes : hmi::resolutionMinutes(o.text("resolution", "30 min")));
                        if (part.rfind("case:", 0) == 0) {
                            const std::string rest(part.substr(5));
                            const auto comma = rest.find(',');
                            if (comma != std::string::npos) {
                                const int d = std::atoi(rest.substr(0, comma).c_str()), sl = std::atoi(rest.substr(comma + 1).c_str());
                                local = {l.grid.x + l.cellW * sl, l.grid.y + l.cellH * d, l.cellW, l.cellH};
                                found = d >= 0 && d < 7 && sl >= 0 && sl < l.slots;
                            }
                        } else if (const int d = index("jour:"); d >= 0 && d < 7) {
                            local = {0, l.grid.y + l.cellH * d, l.grid.x, l.cellH};
                            found = true;
                        } else if (const int sl = index("heure:"); sl >= 0 && sl < l.slots) {
                            local = {l.grid.x + l.cellW * sl, 0, l.cellW, l.grid.y};
                            found = true;
                        }
                        break;
                    }
                    case hmi::Kind::Slider:
                    case hmi::Kind::Knob: {
                        // "fraction:0.75" : le point de la course qui donne 75 %.
                        if (part.rfind("fraction:", 0) != 0) break;
                        const double f = std::clamp(std::atof(std::string(part.substr(9)).c_str()), 0.0, 1.0);
                        double x = 0, y = 0;
                        if (o.kind == hmi::Kind::Knob) {
                            const double a = (hmi::kKnobStartDeg + hmi::kKnobSweepDeg * f) * 3.14159265358979323846 / 180.0;
                            const double r = std::min(box.w, box.h) * 0.36;
                            x = box.w / 2 + std::cos(a) * r;
                            y = box.h / 2 + std::sin(a) * r;
                        } else {
                            const auto l = hmi::sliderLayout(o, box.w, box.h);
                            x = l.vertical ? l.track.cx() : l.track.x + l.track.w * f;
                            y = l.vertical ? l.track.bottom() - l.track.h * f : l.track.cy();
                        }
                        local = {x - 1, y - 1, 2, 2};
                        found = true;
                        break;
                    }
                    // 1.12.13 : la case "case:R:C" ou l'en-tete "entete:C" d'un tableau (la mise en page du clic).
                    case hmi::Kind::Table: {
                        hmi::table::Model m;
                        if (!runtime_ || !runtime_->tableShown(layers_[li].view.id, o, m)) break;
                        const bool rows = hmi::table::modeOf(o) == hmi::table::Mode::Rows;
                        const std::size_t first = rows ? runtime_->listFirst(o.id) : 0;
                        std::vector<double> widths = m.widths;
                        if (!m.columns) widths = hmi::columnFractions(o, std::max<std::size_t>(1, m.headers.size()));
                        const std::size_t count = rows ? m.rows.size() : std::min(m.rows.size(), hmi::tableLayout(o, box.w, box.h, 0, 0).fit);
                        found = hmi::tableCellBox(o, box.w, box.h, part, count, first, widths, local);
                        break;
                    }
                    // Lot 11 : les parties des objets des alarmes et de la production.
                    case hmi::Kind::AlarmBanner: {
                        const auto l = hmi::bannerLayout(o, box.w, box.h);
                        if (part == "acquitter" && l.ack.w > 0) { local = l.ack; found = true; }
                        else if (part == "suivante" && l.count.w > 0) { local = l.count; found = true; }
                        break;
                    }
                    case hmi::Kind::AlarmSummary: {
                        const int i = index("zone:");
                        const std::size_t zones = project_ ? hmi::zonesOf(*project_, o.text("groups")).size() : 0;
                        const auto tiles = hmi::summaryTiles(o, box.w, box.h, zones);
                        if (i >= 0 && static_cast<std::size_t>(i) < tiles.size()) { local = tiles[static_cast<std::size_t>(i)]; found = true; }
                        break;
                    }
                    case hmi::Kind::ProductionCounter: {
                        const auto l = hmi::productionLayout(o, box.w, box.h);
                        if (part == "raz" && l.reset.w > 0) { local = l.reset; found = true; }
                        break;
                    }
                    case hmi::Kind::VariableTable: {
                        const auto g = hmi::variableTableLayout(o, box.w, box.h);
                        const int i = index("ligne:");
                        if (i >= 0 && static_cast<std::size_t>(i) < g.visible) { local = g.cell(static_cast<std::size_t>(i), 1); found = true; }
                        break;
                    }
                    case hmi::Kind::RecipeEditor: {
                        const auto l = hmi::recipeEditorLayout(o, box.w, box.h);
                        if (part == "precedent") { local = l.prev; found = true; }
                        else if (part == "suivant") { local = l.next; found = true; }
                        else if (part.rfind("bouton:", 0) == 0) {
                            for (const auto& b : l.buttons) if (b.label == part.substr(7)) { local = b.box; found = true; }
                        } else if (const int i = index("ligne:"); i >= 0 && static_cast<std::size_t>(i) < l.grid.visible) {
                            local = l.grid.cell(static_cast<std::size_t>(i), 1);
                            found = true;
                        }
                        break;
                    }
                    case hmi::Kind::History: {
                        const int i = index("ligne:");
                        const double k = std::clamp(o.number("fontSize", 12), 7.0, 40.0) / 12.0;   // 1.11 (R111)
                        if (i >= 0 && (hmi::kHistoryHeaderH + hmi::kHistoryRowH * (i + 1)) * k <= box.h) {
                            local = {0, (hmi::kHistoryHeaderH + hmi::kHistoryRowH * i) * k, box.w, hmi::kHistoryRowH * k};
                            found = true;
                        }
                        break;
                    }
                    // Lot 12 : la navigation et la structure.
                    case hmi::Kind::NavBar: case hmi::Kind::Breadcrumb: case hmi::Kind::TabContainer: case hmi::Kind::ScrollPanel:
                    case hmi::Kind::CollapsiblePanel: case hmi::Kind::ZoneMap:
                        found = lot12PartBox(layers_[li].view, o, box.w, box.h, part, runtime_, project_, local);
                        break;
                    // Lot 14 : un bouton du diagnostic automate.
                    case hmi::Kind::PlcDiagnostic: {
                        const auto l = hmi::comm::diagnosticLayout(o, box.w, box.h, 0);
                        if (part == "bouton:reconnecter" && l.reconnect.w > 0) {
                            local = l.reconnect;
                            found = true;
                        } else if (part == "bouton:compteurs" && l.reset.w > 0) {
                            local = l.reset;
                            found = true;
                        }
                        break;
                    }
                    // Lot 13 : un bouton du selecteur de theme ("theme:jour", "theme:nuit").
                    case hmi::Kind::ThemeSelector: {
                        const auto l = hmi::languageSelectorLayout(o, box.w, box.h, 2);
                        if (l.buttons.size() == 2 && (part == "theme:jour" || part == "theme:nuit")) {
                            local = l.buttons[part == "theme:jour" ? 0 : 1];
                            found = true;
                        }
                        break;
                    }
                    // Lot 13 : un bouton du selecteur de langue ("langue:1", "langue:suivante").
                    case hmi::Kind::LanguageSelector: {
                        const std::size_t n = project_ ? hmi::languageChoices(o, project_->languages).size() : 0;
                        const auto l = hmi::languageSelectorLayout(o, box.w, box.h, n);
                        if (part == "langue:suivante") {
                            if (l.toggle && !l.buttons.empty()) { local = l.buttons.front(); found = true; }
                        } else if (const int i = index("langue:"); !l.toggle && i >= 0 && static_cast<std::size_t>(i) < l.buttons.size()) {
                            local = l.buttons[static_cast<std::size_t>(i)];
                            found = true;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            if (!found) return false;
            const auto a = vp.toScreen(box.x + local.x, box.y + local.y), c = vp.toScreen(box.x + local.x + local.w, box.y + local.y + local.h);
            out = {a.x, a.y, c.x - a.x, c.y - a.y};
            return true;
        }
    }
    return false;
}

bool HmiLiveCanvas::popupRect(std::string_view viewName, PopupRect& out) const {
    for (auto it = popupRects_.rbegin(); it != popupRects_.rend(); ++it)
        if (it->layer < layers_.size() && layers_[it->layer].view.name == viewName) { out = *it; return true; }
    return false;
}

bool HmiLiveCanvas::keyRect(std::string_view label, gfx::Rect& out) const {
    for (const auto& k : keys_)
        if (k.label == label || (!k.command.empty() && k.command == label)) {
            out = {keyboardRect_.x + static_cast<float>(k.box.x), keyboardRect_.y + static_cast<float>(k.box.y),
                   static_cast<float>(k.box.w), static_cast<float>(k.box.h)};
            return true;
        }
    return false;
}

void HmiLiveCanvas::paintKeyboard(const ui::PaintContext& ctx) {
    if (!runtime_) return;
    const std::string mode = runtime_->keyboardMode();
    if (mode.empty()) return;
    const auto b = bounds();
    const auto size = hmi::keyboardSize(mode);
    const float w = std::min(b.w - 16.f, static_cast<float>(size.w)), h = std::min(b.h - 16.f, static_cast<float>(size.h));
    keyboardRect_ = {b.x + (b.w - w) / 2.f, b.bottom() - h - 10.f, w, h};
    keys_ = hmi::keyboardLayout(mode, w, h, shift_);
    ctx.r.fillRect({keyboardRect_.x + 4, keyboardRect_.y + 6, w, h}, gfx::Color{0, 0, 0, 100});
    ctx.r.fillRoundedRect(keyboardRect_, gfx::Color::rgb(0x1B212B), 8.f);
    ctx.r.strokeRect(keyboardRect_, ctx.theme.color.accent, 1.2f);
    const auto& f = ctx.theme.font;
    for (const auto& k : keys_) {
        const gfx::Rect r{keyboardRect_.x + static_cast<float>(k.box.x), keyboardRect_.y + static_cast<float>(k.box.y),
                          static_cast<float>(k.box.w), static_cast<float>(k.box.h)};
        const bool command = !k.command.empty();
        const bool enter = k.command == "entree";
        ctx.r.fillRoundedRect(r, enter ? ctx.theme.color.accent : command ? gfx::Color::rgb(0x323A47) : gfx::Color::rgb(0x2A313C), 5.f);
        const float tw = ctx.r.measure(k.label, f.ui).width;
        ctx.r.drawText({r.x + (r.w - tw) / 2.f, r.y + (r.h - ctx.r.lineHeight(f.ui)) / 2.f}, k.label, f.ui, gfx::Color::rgb(0xE6EAF0));
    }
}

double HmiLiveCanvas::dragFraction(std::size_t layer, const hmi::Object& o, gfx::Point p) const {
    if (layer >= vps_.size()) return 0;
    const auto& vp = vps_[layer];
    const auto box = o.box();
    const double lx = vp.toViewX(p.x) - box.x, ly = vp.toViewY(p.y) - box.y;
    return o.kind == hmi::Kind::Knob ? hmi::knobFractionAt(box.w, box.h, lx, ly) : hmi::sliderFractionAt(o, box.w, box.h, lx, ly);
}

bool HmiLiveCanvas::comboListHit(gfx::Point p, hmi::Id& object, std::string& part) const {
    if (!runtime_ || layers_.empty() || vps_.size() != layers_.size()) return false;
    const std::size_t li = liveTopLayer();   // 1.10.2 : pas une popup qui se ferme
    const auto& vp = vps_[li];
    for (const auto& o : layers_[li].view.objects) {
        std::size_t first = 0;
        if (o.kind != hmi::Kind::ComboBox || !runtime_->comboOpen(o.id, &first)) continue;
        const auto box = o.box();
        const double lx = vp.toViewX(p.x) - box.x, ly = vp.toViewY(p.y) - box.y;
        object = o.id;
        const std::string hit = hmi::comboHit(o, box.w, box.h, lx, ly, hmi::choicesOf(o).size(), true, first);
        part = hit == "ouvrir" ? std::string{} : hit;
        return true;
    }
    return false;
}

void HmiLiveCanvas::pressObject(std::size_t layer, gfx::Point p, int clickCount) {
    const Id id = objectAtLayer(layer, p);
    const auto* o = id && layer < layers_.size() ? layers_[layer].view.object(id) : nullptr;
    if (o && layer < vps_.size()) {
        const auto& vp = vps_[layer];
        const auto box = o->box();
        const double lx = vp.toViewX(p.x) - box.x, ly = vp.toViewY(p.y) - box.y;
        // 1.12.30 : la visionneuse de documents - un bouton, un champ, une vignette ; dans la zone des pages, un
        // glisser deplace la page (la main), un double-clic passe de l'ajustement a 100 %.
        if (o->kind == hmi::Kind::DocumentViewer) {
            if (runtime_ && runtime_->focusedObject() != kNoId && runtime_->focusedObject() != id) clickedAway->emit(id);
            const std::string part = runtime_ ? runtime_->docPartAt(id, lx, ly) : std::string{};
            if (part == "doc:area") {
                if (clickCount >= 2) {
                    partClicked->emit(id, "doc:dbl:" + hmi::formatNumber(lx) + ":" + hmi::formatNumber(ly));
                    return;
                }
                docDrag_ = id;
                docDragLayer_ = layer;
                docDragLast_ = p;
            }
            if (!part.empty()) partClicked->emit(id, "doc:at:" + hmi::formatNumber(lx) + ":" + hmi::formatNumber(ly));
            return;
        }
        // Un objet a parties : la partie touchee (bouton, ligne, champ).
        if (o->kind == hmi::Kind::RecipeManager || o->kind == hmi::Kind::UserManager) {
            std::size_t rows = 0;
            if (o->kind == hmi::Kind::RecipeManager) {
                const auto* r = project_ ? project_->recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text)) : nullptr;
                rows = r ? r->records.size() : 0;
            } else {
                rows = project_ ? project_->security.users.size() : 0;
            }
            const auto part = hmi::recipeManagerHit(*o, box.w, box.h, lx, ly, rows);
            if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(id);
            if (!part.empty()) partClicked->emit(id, part);
            return;
        }
        // Lot 11 : les objets des alarmes et de la production. Le corps d'un bandeau,
        // une tuile de zone, une ligne d'alarmes, sont aussi un clic (les actions).
        if (o->kind == hmi::Kind::AlarmBanner || o->kind == hmi::Kind::AlarmSummary || o->kind == hmi::Kind::ProductionCounter
            || o->kind == hmi::Kind::VariableTable || o->kind == hmi::Kind::RecipeEditor || o->kind == hmi::Kind::History) {
            std::string part;
            switch (o->kind) {
                case hmi::Kind::AlarmBanner: part = hmi::bannerHit(*o, box.w, box.h, lx, ly); break;
                case hmi::Kind::AlarmSummary:
                    part = hmi::summaryHit(*o, box.w, box.h, lx, ly, project_ ? hmi::zonesOf(*project_, o->text("groups")).size() : 0);
                    break;
                case hmi::Kind::ProductionCounter: part = hmi::productionHit(*o, box.w, box.h, lx, ly); break;
                case hmi::Kind::VariableTable: part = hmi::variableTableHit(*o, box.w, box.h, lx, ly, hmi::variableRows(*o).size()); break;
                case hmi::Kind::RecipeEditor: {
                    const auto* r = project_ ? project_->recipeByName(hmi::markers::strip(o->text("recipe"), hmi::markers::Mode::Text)) : nullptr;
                    part = hmi::recipeEditorHit(*o, box.w, box.h, lx, ly, r ? r->fields.size() : 0);
                    break;
                }
                default: {
                    // Une liste d'alarmes : ses lignes (source alarmes, acquittees, mises de cote).
                    if (!runtime_) break;
                    const std::string src = o->text("source", "alarmes");
                    std::size_t rows = 0;
                    if (src == "alarmes" || src.rfind("acquitt", 0) == 0) rows = hmi::historyAlarmRows(runtime_->alarms(), src, hmi::markers::strip(o->text("group"), hmi::markers::Mode::Text)).size();
                    else if (src.rfind("mises", 0) == 0) rows = hmi::historyShelvedRows(runtime_->shelvedAlarms(), hmi::markers::strip(o->text("group"), hmi::markers::Mode::Text)).size();
                    // 1.11 (R111) : la taille du texte (fontSize) agrandit l'en-tete et les lignes d'autant.
                    const double k = std::clamp(o->number("fontSize", 12), 7.0, 40.0) / 12.0;
                    part = hmi::historyHit(box.w / k, box.h / k, lx / k, ly / k, rows);
                    break;
                }
            }
            const bool form = o->kind == hmi::Kind::VariableTable || o->kind == hmi::Kind::RecipeEditor;
            if (runtime_ && runtime_->focusedObject() != kNoId && (!form || runtime_->focusedObject() != id || part.empty())) clickedAway->emit(id);
            if (!part.empty()) partClicked->emit(id, part);
            const bool alsoClick = o->kind == hmi::Kind::AlarmSummary || o->kind == hmi::Kind::History
                                || ((o->kind == hmi::Kind::AlarmBanner || o->kind == hmi::Kind::ProductionCounter) && part.empty());
            if (!alsoClick) return;
        }
        // Lot 14 : un bouton du diagnostic automate (Reconnecter, Remettre a zero).
        if (o->kind == hmi::Kind::PlcDiagnostic) {
            const std::string part = hmi::comm::diagnosticHit(*o, box.w, box.h, lx, ly, 0);
            if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(id);
            if (!part.empty()) partClicked->emit(id, part);
            return;
        }
        // Lot 13 : un bouton du selecteur de theme (Jour, Nuit).
        if (o->kind == hmi::Kind::ThemeSelector) {
            const std::string part = hmi::themeSelectorHit(*o, box.w, box.h, lx, ly);
            if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(id);
            if (!part.empty()) partClicked->emit(id, part);
            return;
        }
        // Lot 13 : un bouton du selecteur de langue.
        if (o->kind == hmi::Kind::LanguageSelector) {
            const std::size_t n = project_ ? hmi::languageChoices(*o, project_->languages).size() : 0;
            const std::string part = hmi::languageSelectorHit(*o, box.w, box.h, lx, ly, n);
            if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(id);
            if (!part.empty()) partClicked->emit(id, part);
            return;
        }
        // Lot 12 : la navigation et la structure. Un bouton de la barre, une etape,
        // un onglet, un bandeau, une zone ; la barre d'un panneau defilant se tire.
        if (hmi::kindIsNavigation(o->kind) && o->kind != hmi::Kind::Frame) {
            const std::string part = lot12PartAt(layers_[layer].view, *o, box.w, box.h, lx, ly, runtime_, project_);
            if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(id);
            if (!part.empty()) {
                if (o->kind == hmi::Kind::ScrollPanel) {
                    scrollDrag_ = id;
                    scrollDragLayer_ = layer;
                    scrollDragVertical_ = part.rfind("vbarre:", 0) == 0;
                }
                partClicked->emit(id, part);
            }
            // Une zone, le fond d'un panneau : aussi un clic (ses actions).
            const bool alsoClick = o->kind == hmi::Kind::ZoneMap || (part.empty() && o->kind != hmi::Kind::NavBar);
            if (!alsoClick) return;
            if (part.empty() && (o->kind == hmi::Kind::ScrollPanel || o->kind == hmi::Kind::TabContainer
                                 || o->kind == hmi::Kind::CollapsiblePanel)) {
                // Le fond d'une structure : un glisser dessus change de vue (ecran tactile).
                swipeArmed_ = true;
                swipeFrom_ = p;
                swipeLayer_ = layer;
            }
        }
        // 1.12.16 : une courbe pilotable - son bouton Pause / Continuer.
        if (o->kind == hmi::Kind::Trend && runtime_) {
            if (const std::string part = hmi::trendHit(*o, box.w, box.h, lx, ly); !part.empty()) {
                partClicked->emit(id, part);
                return;
            }
        }
        // 1.12.2 : la barre d'un tableau dynamique (rowsFrom) - une page plus haut, plus bas ; ailleurs,
        // le tableau garde ses actions. 1.12.13 : la case (ou l'en-tete) touchee - retenue par le moteur pour le geste
        // qui suit (Clic sur une case, la saisie R/W, la ligne choisie...) ; le clic de l'objet suit, comme avant.
        if (o->kind == hmi::Kind::Table && runtime_) {
            hmi::table::Model m;
            const bool rows = hmi::table::modeOf(*o) == hmi::table::Mode::Rows;
            if (runtime_->tableShown(layers_[layer].view.id, *o, m)) {
                const std::size_t first = rows ? runtime_->listFirst(o->id) : 0;
                if (rows)
                    if (const std::string part = hmi::tableHit(*o, box.w, box.h, lx, ly, m.rows.size(), first); !part.empty()) {
                        partClicked->emit(id, part);
                        return;
                    }
                std::vector<double> widths = m.widths;
                if (!m.columns) widths = hmi::columnFractions(*o, std::max<std::size_t>(1, m.headers.size()));
                // Les cases fixes ne defilent pas : les lignes qui tiennent (pas de barre).
                const std::size_t count = rows ? m.rows.size() : std::min(m.rows.size(), hmi::tableLayout(*o, box.w, box.h, 0, 0).fit);
                if (const std::string part = hmi::tableCellHit(*o, box.w, box.h, lx, ly, count, first, widths); !part.empty())
                    partClicked->emit(id, part);
            }
        }
        // Lot 9 : les commandes a parties (selecteur, boutons radio, liste deroulante,
        // date et heure, programmateur) ; le curseur et le potentiometre se tirent.
        if (o->kind == hmi::Kind::Slider || o->kind == hmi::Kind::Knob) {
            if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(id);
            valueDrag_ = id;
            valueDragLayer_ = layer;
            valueDragged->emit(id, dragFraction(layer, *o, p), false);
            return;
        }
        if (o->kind == hmi::Kind::Selector || o->kind == hmi::Kind::RadioGroup || o->kind == hmi::Kind::ComboBox
            || o->kind == hmi::Kind::DateTimePicker || o->kind == hmi::Kind::WeeklySchedule || o->kind == hmi::Kind::List) {
            std::string part;
            switch (o->kind) {
                case hmi::Kind::Selector: part = hmi::selectorHit(*o, box.w, box.h, lx, ly, hmi::choicesOf(*o).size()); break;
                case hmi::Kind::RadioGroup: part = hmi::radioHit(*o, box.w, box.h, lx, ly, hmi::choicesOf(*o).size()); break;
                case hmi::Kind::ComboBox: part = "ouvrir"; break;
                case hmi::Kind::List:        // 1.12.2 : une ligne (choisie), une bande (defiler)
                    part = hmi::listHit(*o, box.w, box.h, lx, ly, hmi::choicesOf(*o).size(), runtime_ ? runtime_->listFirst(o->id) : 0);
                    break;
                case hmi::Kind::DateTimePicker: part = hmi::pickerHit(*o, box.w, box.h, lx, ly); break;
                default: {
                    const auto* ws = runtime_ ? runtime_->schedule(o->id) : nullptr;
                    const int res = ws ? ws->slotMinutes : hmi::resolutionMinutes(o->text("resolution", "30 min"));
                    part = hmi::scheduleHit(*o, box.w, box.h, lx, ly, res);
                    break;
                }
            }
            if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(id);
            if (!part.empty()) partClicked->emit(id, part);
            return;
        }
        if (o->kind == hmi::Kind::InputField || o->kind == hmi::Kind::LoginPanel || o->kind == hmi::Kind::PasswordChange
            || o->kind == hmi::Kind::LogoutButton) {
            const auto part = hmi::formHit(*o, box.w, box.h, lx, ly);
            if (part.empty()) {
                if (runtime_ && runtime_->focusedObject() != kNoId && runtime_->focusedObject() != id) clickedAway->emit(id);
            } else {
                partClicked->emit(id, part);
            }
            // Le bouton de deconnexion est aussi un bouton : ses actions de clic suivent.
            if (o->kind != hmi::Kind::LogoutButton || o->actions.empty()) return;
        }
    }
    // Un clic ailleurs que dans le champ en saisie : il perd le focus.
    if (runtime_ && runtime_->focusedObject() != kNoId && runtime_->focusedObject() != id) clickedAway->emit(id);
    // Lot 12 : le fond de la vue - un glisser horizontal change de vue ; une vue
    // qu'on peut zoomer, zoomee, se tire.
    if (id == kNoId && layer < layers_.size() && !layers_[layer].popup) {
        if (layers_[layer].view.zoomable && clickCount >= 2) {
            // Double-clic sur le fond : la vue revient a 100 %.
            resetUserZoom();
            viewZoomed->emit(100.0);
        } else if (layers_[layer].view.zoomable && userZoom_ > 1.001f) {
            panning_ = true;
            panGrab_ = p;
            panStart_ = userPan_;
        } else if (viewOverflows()) {
            // 1.10 : la vue zoomee depasse la place - le fond tire la deplace.
            displayPanning_ = true;
            displayGrab_ = p;
            displayPanStart_ = displayPan_;
        } else {
            swipeArmed_ = true;
            swipeFrom_ = p;
            swipeLayer_ = layer;
        }
    }
    down_ = id;
    downLayer_ = layer;
    if (id) {
        pressed->emit(id);
        clicked->emit(id);
        if (clickCount >= 2) doubleClicked->emit(id);
    }
}

bool HmiLiveCanvas::hitAt(gfx::Point p, Id& view, Id& object, std::size_t* layer) const {
    view = object = kNoId;
    const int li = layerUnder(p);
    if (li < 0 || static_cast<std::size_t>(li) >= layers_.size()) return false;
    view = layers_[static_cast<std::size_t>(li)].view.id;
    object = objectAtLayer(static_cast<std::size_t>(li), p);
    if (layer) *layer = static_cast<std::size_t>(li);
    return object != kNoId;
}

ui::EventResult HmiLiveCanvas::onEvent(const ui::InputEvent& ev) {
    // 1.12.18 (planche 16) : LE CLIC DROIT DU MENU - l'hote dit s'il ouvre son menu (sinon il va
    // au projet : SYS.MouseRight). Le menu ne presse pas le bouton du projet, ni son relacher.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right) {
        if (bounds().contains(d->pos) && stoppedNote_.empty() && contextWanted_ && contextWanted_(d->pos, d->mods.shift)) {
            rightForMenu_ = true;
            contextRequested->emit(d->pos);
            return ui::EventResult::Consumed;
        }
        rightForMenu_ = false;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev); u && u->button == ui::MouseButton::Right && rightForMenu_) {
        rightForMenu_ = false;
        return ui::EventResult::Consumed;
    }
    // 1.11.23 : la souris (SYS.Mouse*) d'abord, puis le clavier - une touche qu'un raccourci
    // de la vue prend ne va nulle part ailleurs (ni Echap qui ferme la popup, ni le badge).
    notePointer(ev);
    if (routeKey(ev)) return ui::EventResult::Consumed;
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        // 1.10 : l'IHM arretee - les boutons de la carte ; le reste ne repond pas.
        if (!stoppedNote_.empty() && bounds().contains(d->pos)) {
            if (d->button == ui::MouseButton::Left)
                for (std::size_t i = 0; i < noteRects_.size() && i < noteActions_.size(); ++i)
                    if (noteRects_[i].contains(d->pos)) {
                        stoppedAction->emit(std::string(noteActions_[i]));   // une copie : l'action peut refaire la carte
                        break;
                    }
            return ui::EventResult::Consumed;
        }
        // 1.10 : le bouton du milieu deplace la vue zoomee, ou qu'il appuie.
        if (d->button == ui::MouseButton::Middle && bounds().contains(d->pos) && viewOverflows()) {
            displayPanning_ = true;
            displayGrab_ = d->pos;
            displayPanStart_ = displayPan_;
            return ui::EventResult::Consumed;
        }
        if (!bounds().contains(d->pos) || d->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        // 1.9 : au doigt (pas de survol), l'appui sur un objet repere montre
        // l'infobulle de sa lecture simulee, tant que le doigt reste.
        mouse_ = d->pos;
        if (!simRects_.empty() || !qualityRects_.empty()) invalidate();
        grabFocus();    // le clavier : pour un champ de saisie de la vue
        // Lot 10 : l'ecran en veille - ce toucher le rallume, rien d'autre.
        if (runtime_ && runtime_->asleep(runtime_->now())) {
            wakeRequested->emit();
            return ui::EventResult::Consumed;
        }
        // Lot 13 : le panneau de signature - son clavier virtuel d'abord, puis il
        // prend tous les clics (modal) ; le bandeau de l'avertissement : rester connecte.
        // 1.11.7 : le champ du clavier virtuel d'une action - modal, comme la signature.
        if (runtime_ && runtime_->promptShown()) {
            if (keyboardRect_.contains(d->pos)) {
                pressKeyboard(d->pos);
                return ui::EventResult::Consumed;
            }
            std::string part = "dehors";
            if (promptLayout_) {
                part = hmi::promptHit(*promptLayout_, d->pos.x - screen_.x, d->pos.y - screen_.y);
                if (part.empty()) part = "rien";
            }
            promptPartClicked->emit(part);
            return ui::EventResult::Consumed;
        }
        if (runtime_ && runtime_->signatureShown()) {
            if (keyboardRect_.contains(d->pos)) {
                pressKeyboard(d->pos);
                return ui::EventResult::Consumed;
            }
            std::string part = "rien";
            if (signLayout_) {
                part = hmi::signatureHit(*signLayout_, d->pos.x - screen_.x, d->pos.y - screen_.y);
                if (part.empty()) part = "rien";
            }
            signaturePartClicked->emit(part);
            return ui::EventResult::Consumed;
        }
        if (warning_.bar.w > 0.f && warning_.bar.contains(d->pos)) {
            stayRequested->emit();
            return ui::EventResult::Consumed;
        }
        // Le menu Parametres systeme ouvert : il prend tous les clics ; hors de
        // son panneau, il se ferme.
        if (runtime_ && runtime_->systemShown()) {
            std::string part = "fermer";
            if (systemLayout_) {
                part = hmi::systemMenuHit(*systemLayout_, systemTabShown_, d->pos.x - systemScreen_.x, d->pos.y - systemScreen_.y);
                // Une page de lignes (1.9 : celles de la page Simulation), une carte.
                const long page = static_cast<long>(std::max<std::size_t>(1, systemTabShown_ == hmi::kSimulationTab ? systemLayout_->sim.visible
                                                                                                                   : systemLayout_->diagVisible));
                if (part == "defiler:-1" || part == "defiler:1") part = systemScrollPart(part == "defiler:1" ? page : -page);
                else if (part == "defiler_liste:-1" || part == "defiler_liste:1") part = simListScrollPart(part == "defiler_liste:1" ? 1 : -1);
            }
            if (!part.empty()) systemPartClicked->emit(part);
            return ui::EventResult::Consumed;
        }
        // Lot 12 : le menu de connexion ouvert - son clavier virtuel d'abord (par-
        // dessus), puis le menu prend tous les clics ; hors de son panneau, il se ferme.
        if (runtime_ && runtime_->loginShown()) {
            if (keyboardRect_.contains(d->pos)) {
                pressKeyboard(d->pos);
                return ui::EventResult::Consumed;
            }
            std::string part = "fermer";
            if (loginLayout_) {
                part = hmi::loginMenuHit(*loginLayout_, loginTabShown_, d->pos.x - screen_.x, d->pos.y - screen_.y,
                                         runtime_->loginScroll(), runtime_->loginRows());
                const long page = static_cast<long>(std::max<std::size_t>(1, loginLayout_->rows.size()));
                if (part == "defiler:-1" || part == "defiler:1") part = loginScrollPart(part == "defiler:1" ? page : -page);
                if (part.empty()) part = "rien";
            }
            loginPartClicked->emit(part);
            return ui::EventResult::Consumed;
        }
        // 1.12.24 : la fenetre du Reglage PID - elle prend tous les clics ; dehors, elle se ferme.
        if (runtime_ && runtime_->pidShown()) {
            const std::string part = pidLayout_ ? pidPanelHit(*pidLayout_, d->pos) : std::string("dehors");
            if (!part.empty()) pidPartClicked->emit(part);
            return ui::EventResult::Consumed;
        }
        if (!interactive_) return ui::EventResult::Consumed;   // une transition : on attend
        // Lot 8 : le clavier virtuel d'abord (il est par-dessus tout).
        if (keyboardRect_.contains(d->pos)) {
            pressKeyboard(d->pos);
            return ui::EventResult::Consumed;
        }
        // Lot 9 : une liste deroulante ouverte passe devant tout ; un clic
        // ailleurs la referme (et continue vers ce qu'il touche).
        {
            hmi::Id combo = kNoId;
            std::string part;
            if (comboListHit(d->pos, combo, part)) {
                if (!part.empty()) {
                    partClicked->emit(combo, part);
                    return ui::EventResult::Consumed;
                }
                partClicked->emit(combo, "fermer");
                // Un clic sur la boite de la liste elle-meme : c'est tout (elle se referme).
                const Id under = objectAt(d->pos);
                if (under == combo) return ui::EventResult::Consumed;
            }
        }
        // Les popups, du dessus vers le dessous : leur croix, leur barre de titre
        // (on la tire), leur contenu. Hors d'une popup modale : rien, ou la
        // fermer si elle le demande. Hors d'une popup non modale : dessous.
        for (std::size_t i = popupRects_.size(); i-- > 0;) {
            const auto& pr = popupRects_[i];
            const bool top = i + 1 == popupRects_.size();
            if (pr.window.contains(d->pos)) {
                if (pr.close.w > 0.f && pr.close.contains(d->pos)) {
                    popupCloseRequested->emit(pr.slot, false);
                    return ui::EventResult::Consumed;
                }
                if (!top) popupRaised->emit(pr.slot);
                if (pr.title.w > 0.f && pr.title.contains(d->pos)) {
                    if (pr.movable) {
                        dragSlot_ = pr.slot;
                        dragGrab_ = {d->pos.x - pr.window.x, d->pos.y - pr.window.y};
                    }
                    return ui::EventResult::Consumed;
                }
                pressObject(pr.layer, d->pos, d->clickCount);
                return ui::EventResult::Consumed;
            }
            if (pr.modal) {
                if (pr.closeOutside) popupCloseRequested->emit(pr.slot, false);
                else if (runtime_ && runtime_->focusedObject() != kNoId) clickedAway->emit(hmi::kNoId);
                return ui::EventResult::Consumed;
            }
            // Non modale : un clic dehors la ferme si elle le demande (une aide, un
            // menu), et il continue vers ce qui est dessous.
            if (pr.closeOutside) popupCloseRequested->emit(pr.slot, true);
        }
        // La vue du dessous (la derniere couche qui n'est pas une popup).
        std::size_t base = 0;
        for (std::size_t li = 0; li < layers_.size(); ++li)
            if (!layers_[li].popup) base = li;
        // 1.10.2 : sans popup ouverte, la couche du dessus qui vit (pas la popup qui se
        // ferme, dessinee en fondu par-dessus : avant, le clic allait a elle). Une
        // popup qui arrive en tournant (pas encore de cadre de clic) : on attend.
        const std::size_t live = liveTopLayer();
        if (popupRects_.empty() && live < layers_.size() && layers_[live].popup) return ui::EventResult::Consumed;
        pressObject(popupRects_.empty() ? live : base, d->pos, d->clickCount);
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        // 1.12.30 : sur une visionneuse de documents, la molette fait defiler ses pages (Maj : en largeur ; sur la
        // colonne, les vignettes) ; Ctrl+molette zoome la page autour du curseur, pas la vue.
        if (runtime_ && interactive_ && !layers_.empty() && vps_.size() == layers_.size() && bounds().contains(w->pos)
            && (w->dy != 0 || w->dx != 0) && !runtime_->systemShown() && !runtime_->loginShown()) {
            std::size_t li = liveTopLayer();
            if (!popupRects_.empty()) {
                bool over = false;
                for (auto it = popupRects_.rbegin(); it != popupRects_.rend() && !over; ++it)
                    if (it->window.contains(w->pos)) { li = it->layer; over = true; }
                if (!over)
                    for (std::size_t k = 0; k < layers_.size(); ++k) if (!layers_[k].popup) li = k;
            }
            if (li < layers_.size()) {
                const auto& vp = vps_[li];
                const double x = vp.toViewX(w->pos.x), y = vp.toViewY(w->pos.y);
                const auto order = layers_[li].view.paintOrder();
                for (auto it = order.rbegin(); it != order.rend(); ++it) {
                    const auto* o = *it;
                    if (!o->flag("visible", true) || !o->box().contains(x, y) || hmi::clippedAt(layers_[li].view, *o, x, y)) continue;
                    if (o->kind != hmi::Kind::DocumentViewer) break;   // un autre objet par-dessus : il garde la molette
                    const double lx = x - o->box().x, ly = y - o->box().y;
                    if (w->mods.ctrl) {
                        const double f = std::pow(1.15, std::clamp(static_cast<double>(w->dy), -4.0, 4.0));
                        partClicked->emit(o->id, "doc:zoomat:" + hmi::formatNumber(f) + ":" + hmi::formatNumber(lx) + ":" + hmi::formatNumber(ly));
                    } else {
                        const std::string under = runtime_->docPartAt(o->id, lx, ly);
                        const double dy = -60.0 * std::clamp(static_cast<double>(w->dy), -4.0, 4.0);
                        const double dx = -60.0 * std::clamp(static_cast<double>(w->dx), -4.0, 4.0);
                        if (under.rfind("doc:thumb", 0) == 0) partClicked->emit(o->id, "doc:thumbwheel:" + hmi::formatNumber(dy));
                        else if (w->mods.shift || (w->dy == 0 && w->dx != 0))
                            partClicked->emit(o->id, "doc:wheelx:" + hmi::formatNumber(w->dy != 0 ? dy : dx));
                        else partClicked->emit(o->id, "doc:wheel:" + hmi::formatNumber(dy));
                    }
                    return ui::EventResult::Consumed;
                }
            }
        }
        // 1.10 : Ctrl+molette - le zoom de la vue en cours, autour du curseur.
        if (w->mods.ctrl && bounds().contains(w->pos) && !station_ && w->dy != 0) {
            zoomDisplayAround(std::pow(1.15f, std::clamp(w->dy, -4.f, 4.f)), w->pos);
            return ui::EventResult::Consumed;
        }
        // Lot 12 : le menu de connexion - la molette fait defiler ses listes.
        if (runtime_ && runtime_->loginShown()) {
            if (!bounds().contains(w->pos) || !loginLayout_) return ui::EventResult::Ignored;
            const std::string part = loginScrollPart(w->dy > 0 ? -3 : w->dy < 0 ? 3 : 0);
            if (!part.empty() && w->dy != 0) loginPartClicked->emit(part);
            return ui::EventResult::Consumed;
        }
        // Lot 12 : la molette fait defiler le panneau defilant sous la souris ; sur
        // le fond d'une vue qu'on peut zoomer, elle zoome autour du pointeur.
        if (bounds().contains(w->pos) && !(runtime_ && runtime_->systemShown()) && interactive_ && !layers_.empty()
            && vps_.size() == layers_.size() && w->dy != 0) {
            std::size_t li = liveTopLayer();   // 1.10.2 : pas une popup qui se ferme
            if (!popupRects_.empty()) {
                bool over = false;
                for (auto it = popupRects_.rbegin(); it != popupRects_.rend() && !over; ++it)
                    if (it->window.contains(w->pos)) { li = it->layer; over = true; }
                if (!over)
                    for (std::size_t k = 0; k < layers_.size(); ++k) if (!layers_[k].popup) li = k;
            }
            const auto& vp = vps_[li];
            const double x = vp.toViewX(w->pos.x), y = vp.toViewY(w->pos.y);
            const auto order = layers_[li].view.paintOrder();
            for (auto it = order.rbegin(); it != order.rend(); ++it) {
                const auto* o = *it;
                // 1.12.2 : la liste, le tableau dynamique (rowsFrom) - la molette fait defiler leurs lignes.
                const bool rows = o->kind == hmi::Kind::List
                               || (o->kind == hmi::Kind::Table && o->text("rowsFrom").find_first_not_of(" \t") != std::string::npos);
                if ((o->kind != hmi::Kind::ScrollPanel && !rows) || !o->flag("visible", true)) continue;
                if (!o->box().contains(x, y) || hmi::clippedAt(layers_[li].view, *o, x, y)) continue;
                if (rows) {
                    const int notch = o->kind == hmi::Kind::Table ? 3 : 1;
                    partClicked->emit(o->id, "defiler:" + std::to_string(w->dy > 0 ? -notch : notch));
                    return ui::EventResult::Consumed;
                }
                const double step = std::max(4.0, o->number("wheelStep", 40));
                const auto l = hmi::scrollLayout(layers_[li].view, *o);
                const std::string axis = l.maxY > 0 ? "defiler:" : "defilerx:";
                partClicked->emit(o->id, axis + hmi::formatNumber(w->dy > 0 ? -step : step));
                return ui::EventResult::Consumed;
            }
            if (!layers_[li].popup && layers_[li].view.zoomable) {
                const float before = userZoom_;
                userZoom_ = std::clamp(userZoom_ * std::pow(1.18f, std::clamp(w->dy, -4.f, 4.f)), 1.f, 6.f);
                if (userZoom_ != before) {
                    // Le point sous la souris reste sous la souris.
                    const float k = userZoom_ / before;
                    const gfx::Point centre{bounds().x + bounds().w / 2.f, bounds().y + bounds().h / 2.f};
                    userPan_.x = (userPan_.x - (w->pos.x - centre.x)) * k + (w->pos.x - centre.x);
                    userPan_.y = (userPan_.y - (w->pos.y - centre.y)) * k + (w->pos.y - centre.y);
                    if (userZoom_ <= 1.001f) userPan_ = {};
                    viewZoomed->emit(static_cast<double>(userZoom_) * 100.0);
                    invalidate();
                }
                return ui::EventResult::Consumed;
            }
        }
        // 1.10 : une vue zoomee qui depasse - la molette la deplace (Maj : en largeur).
        if (viewOverflows() && bounds().contains(w->pos) && !(runtime_ && runtime_->systemShown())) {
            const float step = 60.f * std::clamp(w->dy != 0 ? w->dy : w->dx, -4.f, 4.f);
            if (w->mods.shift || (w->dy == 0 && w->dx != 0)) panDisplay(step, 0.f);
            else panDisplay(0.f, step);
            return ui::EventResult::Consumed;
        }
        // Lot 10 : la molette fait defiler le diagnostic ; 1.9 : la page Simulation
        // (la liste des esclaves sous la souris, sinon les lignes de l'esclave choisi).
        if (!runtime_ || !runtime_->systemShown() || !systemLayout_ || systemTabShown_ == 0 || !bounds().contains(w->pos))
            return ui::EventResult::Ignored;
        const auto& list = systemLayout_->sim.list;
        const bool overList = systemTabShown_ == hmi::kSimulationTab && list.w > 0
                              && list.contains(w->pos.x - systemScreen_.x, w->pos.y - systemScreen_.y);
        const std::string part = overList ? simListScrollPart(w->dy > 0 ? -1 : w->dy < 0 ? 1 : 0)
                                          : systemScrollPart(w->dy > 0 ? -3 : w->dy < 0 ? 3 : 0);
        if (!part.empty() && w->dy != 0) systemPartClicked->emit(part);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        // 1.9 : la souris, pour l'infobulle d'une lecture simulee.
        if (!simRects_.empty() || !qualityRects_.empty() || !simTipAt(mouse_).empty()) invalidate();
        mouse_ = m->pos;
        // 1.12.24 : la courbe des Statistiques du Reglage PID suit la souris (la bulle de l'instant).
        if (pidLayout_ && runtime_ && runtime_->pidShown()) {
            const double t = pidPanelHoverTime(*pidLayout_, m->pos);
            if (std::abs(t - pidHoverShown_) > 1e-6) {
                pidHoverShown_ = t;
                pidHovered->emit(t);
            }
        }
        // 1.12.30 : la page d'une visionneuse suit la souris (en pixels de la vue).
        if (docDrag_ != kNoId) {
            if (docDragLayer_ < vps_.size()) {
                const float k = std::max(0.01f, vps_[docDragLayer_].zoom);
                const double dx = (m->pos.x - docDragLast_.x) / k, dy = (m->pos.y - docDragLast_.y) / k;
                if (std::fabs(dx) + std::fabs(dy) >= 0.5) {
                    partClicked->emit(docDrag_, "doc:pan:" + hmi::formatNumber(dx) + ":" + hmi::formatNumber(dy));
                    docDragLast_ = m->pos;
                }
            }
            return ui::EventResult::Consumed;
        }
        // Lot 12 : la barre d'un panneau defilant suit la souris ; une vue zoomee se tire.
        if (scrollDrag_ != kNoId) {
            if (scrollDragLayer_ < layers_.size() && scrollDragLayer_ < vps_.size())
                if (const auto* o = layers_[scrollDragLayer_].view.object(scrollDrag_)) {
                    const auto& vp = vps_[scrollDragLayer_];
                    const auto box = o->box();
                    const auto l = hmi::scrollLayout(layers_[scrollDragLayer_].view, *o);
                    const double lx = vp.toViewX(m->pos.x) - box.x, ly = vp.toViewY(m->pos.y) - box.y;
                    double f = 0;
                    if (scrollDragVertical_) {
                        const double run = l.vbar.h - l.vthumb.h;
                        f = run > 0 ? std::clamp((ly - l.vthumb.h / 2) / run, 0.0, 1.0) : 0.0;
                    } else {
                        const double run = l.hbar.w - l.hthumb.w;
                        f = run > 0 ? std::clamp((lx - l.hthumb.w / 2) / run, 0.0, 1.0) : 0.0;
                    }
                    partClicked->emit(scrollDrag_, std::string(scrollDragVertical_ ? "vbarre:" : "hbarre:") + hmi::formatNumber(f));
                }
            return ui::EventResult::Consumed;
        }
        if (panning_) {
            userPan_ = {panStart_.x + m->pos.x - panGrab_.x, panStart_.y + m->pos.y - panGrab_.y};
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (displayPanning_) {     // 1.10
            displayPan_ = {displayPanStart_.x + m->pos.x - displayGrab_.x, displayPanStart_.y + m->pos.y - displayGrab_.y};
            invalidate();
            return ui::EventResult::Consumed;
        }
        // Lot 9 : la poignee du curseur (ou du potentiometre) suit la souris.
        if (valueDrag_ != kNoId) {
            if (valueDragLayer_ < layers_.size())
                if (const auto* o = layers_[valueDragLayer_].view.object(valueDrag_))
                    valueDragged->emit(valueDrag_, dragFraction(valueDragLayer_, *o, m->pos), false);
            return ui::EventResult::Consumed;
        }
        if (dragSlot_ < 0) return ui::EventResult::Ignored;
        const float x = m->pos.x - dragGrab_.x, y = m->pos.y - dragGrab_.y;
        popupMoved->emit(dragSlot_, (x - baseOrigin_.x) / std::max(0.01f, baseZoom_ * baseSx_), (y - baseOrigin_.y) / std::max(0.01f, baseZoom_ * baseSy_));
        return ui::EventResult::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        // Lot 12 : la fin d'un glisser - la barre d'un panneau, le deplacement d'une
        // vue zoomee ; un glisser horizontal ample sur le fond change de vue.
        if (scrollDrag_ != kNoId) {
            scrollDrag_ = kNoId;
            return ui::EventResult::Consumed;
        }
        if (docDrag_ != kNoId) {     // 1.12.30
            docDrag_ = kNoId;
            return ui::EventResult::Consumed;
        }
        if (panning_) {
            panning_ = false;
            return ui::EventResult::Consumed;
        }
        if (displayPanning_) {     // 1.10
            displayPanning_ = false;
            down_ = kNoId;
            return ui::EventResult::Consumed;
        }
        if (swipeArmed_) {
            swipeArmed_ = false;
            const float dx = u->pos.x - swipeFrom_.x, dy = u->pos.y - swipeFrom_.y;
            const float need = std::max(90.f, screen_.w * 0.18f);
            if (std::fabs(dx) >= need && std::fabs(dy) < std::fabs(dx) * 0.5f) {
                down_ = kNoId;
                swiped->emit(dx < 0 ? 1 : -1);
                return ui::EventResult::Consumed;
            }
        }
        if (valueDrag_ != kNoId) {
            const Id id = valueDrag_;
            valueDrag_ = kNoId;
            if (valueDragLayer_ < layers_.size())
                if (const auto* o = layers_[valueDragLayer_].view.object(id)) valueDragged->emit(id, dragFraction(valueDragLayer_, *o, u->pos), true);
            return ui::EventResult::Consumed;
        }
        if (dragSlot_ >= 0) { dragSlot_ = -1; return ui::EventResult::Consumed; }
        if (down_ == kNoId) return ui::EventResult::Ignored;
        const Id id = down_;
        down_ = kNoId;
        released->emit(id, interactive_ && objectAtLayer(std::min(downLayer_, layers_.empty() ? 0 : layers_.size() - 1), u->pos) == id);
        return ui::EventResult::Consumed;
    }
    // 1.10.2 : Echap ferme la popup du dessus, comme sa croix (meme chemin :
    // popupCloseRequested), quand aucun champ n'a le focus (sinon Echap annule la
    // saisie, plus bas). Une popup sans croix ne se ferme pas ainsi. AVANT le
    // lecteur de badge : avec la connexion par badge (Armoire_Gaz), le canevas
    // qui a le focus (un clic dans la vue) rendait Echap « Ignored » dans le bloc
    // du badge, et la popup restait ouverte (vu dans l'appli, G5_04).
    if (runtime_ && !popupRects_.empty() && runtime_->focusedObject() == kNoId && !runtime_->loginShown() && !runtime_->signatureShown()
        && !runtime_->promptShown())
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && k->mods.none() && !k->repeat) {
            const auto& top = popupRects_.back();
            if (top.close.w > 0.f) {
                popupCloseRequested->emit(top.slot, false);
                return ui::EventResult::Consumed;
            }
        }
    // Lot 13 : un lecteur de badge (hors de tout champ) - ses chiffres et son Entree
    // vont au moteur ; les autres touches suivent leur chemin.
    if (focused() && runtime_ && runtime_->focusedObject() == kNoId && !runtime_->loginShown() && !runtime_->signatureShown()
        && !runtime_->promptShown() && runtime_->badgeListening()) {
        if (const auto* t = std::get_if<ui::TextInput>(&ev)) {
            textTyped->emit(t->utf8);
            return ui::EventResult::Consumed;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Return) {
            keyTyped->emit(static_cast<int>(hmi::EditKey::Enter));
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    // 1.12.30 : la visionneuse de documents qui a le clavier, hors de ses champs : Page precedente / suivante, les
    // fleches, Ctrl+0 (la page entiere), Ctrl+F (chercher) ; + et - arrivent en texte (le moteur zoome).
    if (focused() && runtime_ && runtime_->docFocused() && !runtime_->docTyping() && !runtime_->loginShown() && !runtime_->signatureShown()
        && !runtime_->promptShown())
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            std::string part;
            if (k->key == ui::Key::PageUp && !k->mods.ctrl) part = "doc:key:PageUp";
            else if (k->key == ui::Key::PageDown && !k->mods.ctrl) part = "doc:key:PageDown";
            else if (k->key == ui::Key::Up && !k->mods.ctrl) part = "doc:key:Up";
            else if (k->key == ui::Key::Down && !k->mods.ctrl) part = "doc:key:Down";
            else if (k->key == ui::Key::Num0 && k->mods.ctrl) part = "doc:key:Zero";
            else if (k->key == ui::Key::F && k->mods.ctrl) part = "doc:search";
            if (!part.empty()) {
                partClicked->emit(runtime_->focusedObject(), part);
                return ui::EventResult::Consumed;
            }
        }
    // Lot 8 : le clavier, quand un champ de la vue a le focus (lot 12 : ou que le
    // menu de connexion est ouvert - Echap le ferme, Entree connecte ; lot 13 : ou
    // le panneau de signature).
    if (focused() && runtime_
        && (runtime_->focusedObject() != kNoId || runtime_->loginShown() || runtime_->signatureShown() || runtime_->promptShown())) {
        if (const auto* t = std::get_if<ui::TextInput>(&ev)) {
            textTyped->emit(t->utf8);
            return ui::EventResult::Consumed;
        }
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            int key = -1;
            switch (k->key) {
                case ui::Key::Backspace: key = static_cast<int>(hmi::EditKey::Backspace); break;
                case ui::Key::Delete:    key = static_cast<int>(hmi::EditKey::Delete); break;
                case ui::Key::Return:    key = static_cast<int>(hmi::EditKey::Enter); break;
                case ui::Key::Escape:    key = static_cast<int>(hmi::EditKey::Escape); break;
                case ui::Key::Tab:       key = static_cast<int>(k->mods.shift ? hmi::EditKey::BackTab : hmi::EditKey::Tab); break;
                case ui::Key::Left:      key = static_cast<int>(hmi::EditKey::Left); break;
                case ui::Key::Right:     key = static_cast<int>(hmi::EditKey::Right); break;
                case ui::Key::Home:      key = static_cast<int>(hmi::EditKey::Home); break;
                case ui::Key::End:       key = static_cast<int>(hmi::EditKey::End); break;
                default: break;
            }
            if (key >= 0) {
                keyTyped->emit(key);
                return ui::EventResult::Consumed;
            }
            // Les autres touches (lettres) arrivent en TextInput : on les garde.
            if (!k->mods.ctrl && !k->mods.alt) return ui::EventResult::Consumed;
        }
    }
    // (1.10.2 : Echap qui ferme la popup du dessus est plus haut, avant le badge.)
    return ui::EventResult::Ignored;
}

// -------------------------------------------------------------- le volet ---
// ------------------------------------------ 1.10 (decision 12) : le plein ecran ----
namespace {
std::vector<HmiSimulationPane*>& simPanes() {
    static std::vector<HmiSimulationPane*> panes;
    return panes;
}
} // namespace

// La barre flottante du plein ecran (maquette 1.10, scene 3) : le nom de la vue,
// l'etat de l'IHM, le zoom (-, la valeur, +, Ajuster), Quitter le plein ecran.
// Le dernier enfant du volet : elle voit chaque geste la premiere, ne garde que
// ceux qui la touchent (et Echap, F11). 1.12.25 : montree a l'entree (4 s), puis
// seulement la souris tout en haut de l'ecran (le bord), et tant qu'elle est dessus ;
// elle s'efface 1 s apres. Avant, le moindre mouvement de la souris, ou la souris
// pres du haut, la ramenait : elle couvrait les boutons du bandeau de la vue au
// moment de les toucher.
// 1.12.24 : apres Ajuster, la mise a l'echelle - Remplir l'ecran, Garder les
// proportions, Taille reelle (100 %) ; la choisie est allumee (pas apres un zoom).
class FullScreenBar final : public ui::Widget {
public:
    explicit FullScreenBar(std::string id) : ui::Widget(std::move(id)) {}
    std::function<std::string()>        viewName, zoomText;
    std::function<bool()>               running;
    std::function<void(core::ActionId)> sink;
    std::function<int()>                fitShown;   // 1.12.24 : 0 Remplir, 1 Garder les proportions, 2 Taille reelle ; -1 : aucune
    // 1.12.24 : un message en bas, 3 s (a la place de "comment sortir").
    void say(std::string text) {
        note_ = std::move(text);
        toastUntil_ = now_ + 3.0;
    }
    // 1.10.2 : Echap va d'abord a la vue (fermer la popup du dessus, annuler une
    // saisie) ; il ne quitte le plein ecran que si elle ne le prend pas. La barre
    // est le dernier enfant du volet : sans cela elle prenait Echap avant le canevas.
    std::function<bool(const ui::InputEvent&)> escapeFirst;
    void enter(double now) {
        shownUntil_ = toastUntil_ = now + 4.0;
        alpha_ = 1.f;
        lastNear_ = -100.0;
        mouse_ = {-1.f, -1.f};
        mouseKnown_ = false;
        hover_ = false;
        note_.clear();
    }
    [[nodiscard]] bool shown() const noexcept { return visible() && alpha_ > 0.05f; }
    [[nodiscard]] gfx::Rect part(std::string_view id) const {
        if (id == "barre") return box_;
        for (const auto& p : parts_)
            if (p.id == id) return p.r;
        return {};
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        if (ctx.overlays) return;   // la passe du dessus seulement (comme la vue)
        const double dt = painted_ < 0.0 ? 0.0 : std::clamp(ctx.time - painted_, 0.0, 0.25);
        painted_ = now_ = ctx.time;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const gfx::FontId font = ctx.theme.font.smallUi, bold = ctx.theme.font.uiBold;
        const std::string name = viewName ? viewName() : std::string{};
        const bool on = running && running();
        const std::string state = on ? "IHM en marche" : "IHM arr\xC3\xAAt\xC3\xA9" "e";
        const std::string zoom = zoomText ? zoomText() : std::string{};
        const std::string quit = "Quitter le plein \xC3\xA9" "cran \xC2\xB7 \xC3\x89" "chap";
        const auto width = [&](const std::string& s, gfx::FontId f) { return ctx.r.measure(s, f).width; };
        const float h = 38.f, bh = 26.f, gap = 6.f;
        const float nameW = width(name, bold), stateW = width(state, font) + 30.f, zoomW = std::max(54.f, width(zoom, font) + 18.f);
        const float fitW = width("Ajuster", font) + 20.f, quitW = width(quit, font) + 22.f, btnW = 28.f;
        // 1.12.24 : la mise a l'echelle en toutes lettres ; trop large pour l'ecran, en court.
        static const char* const kFitLong[] = {"Remplir l'\xC3\xA9" "cran", "Garder les proportions", "Taille r\xC3\xA9" "elle (100 %)"};
        static const char* const kFitShort[] = {"Remplir", "Proportions", "100 %"};
        const auto fitsW = [&](const char* const* labels) {
            float s = 2.f * 2.f;   // deux joints de 2 px
            for (int k = 0; k < 3; ++k) s += width(labels[k], font) + 18.f;
            return s;
        };
        const float others = 12.f + nameW + 10.f + stateW + 3.f * gap + btnW + gap + zoomW + gap + btnW + gap + fitW + gap + 3.f * gap + quitW + 8.f;
        const char* const* fitLabels = others + fitsW(kFitLong) <= b.w - 24.f ? kFitLong : kFitShort;
        const float total = others + fitsW(fitLabels);
        const int fit = fitShown ? fitShown() : -1;
        box_ = {b.x + (b.w - total) / 2.f, b.y + 10.f, total, h};
        // Montree : a l'entree ; la souris au bord du haut, ou sur la barre montree (1 s apres).
        const bool edge = mouseKnown_ && mouse_.y <= b.y + kEdge;
        const gfx::Rect keep{box_.x - 12.f, b.y, box_.w + 24.f, box_.bottom() + 12.f - b.y};
        if (edge || hover_ || (alpha_ > 0.05f && mouseKnown_ && keep.contains(mouse_))) lastNear_ = now_;
        const bool want = now_ - lastNear_ < 1.0 || now_ < shownUntil_;
        alpha_ = want ? std::min(1.f, alpha_ + static_cast<float>(dt) * 6.f) : std::max(0.f, alpha_ - static_cast<float>(dt) * 2.5f);
        parts_.clear();
        float x = box_.x + 12.f;
        const float by = box_.y + (h - bh) / 2.f;
        const auto add = [&](std::string id, float w) {
            parts_.push_back({std::move(id), {x, by, w, bh}});
            x += w;
        };
        add("vue", nameW);
        x += 10.f;
        add("ihm", stateW);
        x += 3.f * gap;
        add("zoomOut", btnW);
        x += gap;
        add("zoom", zoomW);
        x += gap;
        add("zoomIn", btnW);
        x += gap;
        add("fit", fitW);
        x += gap;
        static const char* const kFitIds[] = {"fill", "keep", "actual"};
        for (int k = 0; k < 3; ++k) {
            if (k) x += 2.f;
            add(kFitIds[k], width(fitLabels[k], font) + 18.f);
        }
        x += 3.f * gap;
        add("exit", quitW);
        if (alpha_ > 0.01f) {
            const auto fade = [&](gfx::Color col) { return col.withAlpha(static_cast<std::uint8_t>(static_cast<float>(col.a) * alpha_)); };
            const gfx::Color ink{230, 238, 240, 255}, line{90, 112, 120, 255};
            ctx.r.fillRoundedRect({box_.x, box_.y + 4.f, box_.w, box_.h}, fade(gfx::Color{0, 0, 0, 110}), 9.f);   // l'ombre
            ctx.r.fillRoundedRect(box_, fade(line), 8.f);
            ctx.r.fillRoundedRect({box_.x + 1.f, box_.y + 1.f, box_.w - 2.f, box_.h - 2.f}, fade(gfx::Color{0, 32, 41, 235}), 7.f);
            const auto text = [&](const gfx::Rect& r, const std::string& s, gfx::FontId f, gfx::Color col, bool centered) {
                const float tw = width(s, f);
                ctx.r.drawText({centered ? r.x + (r.w - tw) / 2.f : r.x, r.y + (r.h - ctx.r.lineHeight(f)) / 2.f}, s, f, fade(col));
            };
            const auto button = [&](const gfx::Rect& r, gfx::Color border, bool lit = false) {
                const bool over = r.contains(mouse_);
                ctx.r.fillRoundedRect(r, fade(border), 4.f);
                ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f},
                                      fade(lit ? gfx::Color{0, 84, 102, 255} : over ? gfx::Color{20, 64, 76, 255} : gfx::Color{0, 40, 50, 255}), 3.f);
            };
            for (const auto& p : parts_) {
                if (p.id == "vue") text(p.r, name, bold, ink, false);
                else if (p.id == "ihm") {
                    const gfx::Color tone = on ? c.ok : c.warning;
                    ctx.r.fillRoundedRect(p.r, fade(tone.withAlpha(110)), 10.f);
                    ctx.r.fillRoundedRect({p.r.x + 1.f, p.r.y + 1.f, p.r.w - 2.f, p.r.h - 2.f}, fade(gfx::Color{0, 32, 41, 255}), 9.f);
                    ctx.r.fillRoundedRect({p.r.x + 9.f, p.r.y + (bh - 8.f) / 2.f, 8.f, 8.f}, fade(tone), 4.f);
                    text({p.r.x + 22.f, p.r.y, p.r.w - 22.f, p.r.h}, state, font, ink, false);
                } else if (p.id == "zoom") text(p.r, zoom, font, ink, true);
                else if (p.id == "exit") {
                    button(p.r, c.accent);
                    text(p.r, quit, font, c.accent, true);
                } else if (p.id == "fill" || p.id == "keep" || p.id == "actual") {   // 1.12.24
                    const int k = p.id == "fill" ? 0 : p.id == "keep" ? 1 : 2;
                    button(p.r, k == fit ? c.accent : line, k == fit);
                    text(p.r, fitLabels[k], font, ink, true);
                } else {
                    button(p.r, line);
                    text(p.r, p.id == "zoomOut" ? "\xE2\x88\x92" : p.id == "zoomIn" ? "+" : "Ajuster", font, ink, true);
                }
            }
        }
        // A l'entree, en bas : comment sortir (1.12.24 : ou ce que dit say, le choix d'une mise a l'echelle).
        if (now_ < toastUntil_) {
            const std::string msg = !note_.empty() ? note_
                                                   : std::string("Plein \xC3\xA9" "cran : \xC3\x89" "chap ou F11 pour sortir \xC2\xB7 la souris tout en haut : la barre");
            const float tw = width(msg, font) + 28.f;
            const gfx::Rect t{b.x + (b.w - tw) / 2.f, b.bottom() - 64.f, tw, 30.f};
            const auto k = static_cast<float>(std::clamp(toastUntil_ - now_, 0.0, 0.5) * 2.0);
            ctx.r.fillRoundedRect(t, gfx::Color{0, 0, 0, static_cast<std::uint8_t>(190.f * k)}, 15.f);
            ctx.r.drawText({t.x + 14.f, t.y + (t.h - ctx.r.lineHeight(font)) / 2.f}, msg, font,
                           gfx::Color{255, 255, 255, static_cast<std::uint8_t>(240.f * k)});
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        using R = ui::EventResult;
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if ((k->key == ui::Key::Escape || k->key == ui::Key::F11) && k->mods.none() && !k->repeat) {
                if (k->key == ui::Key::Escape && escapeFirst && escapeFirst(ev)) return R::Consumed;
                if (sink) sink("hmi.fullscreen");
                return R::Consumed;
            }
            return R::Ignored;
        }
        const bool live = alpha_ > 0.3f;
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            mouse_ = m->pos;
            mouseKnown_ = true;
            hover_ = live && box_.contains(m->pos);
            return hover_ ? R::Consumed : R::Ignored;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            // Un doigt au bord du haut (un ecran tactile n'a pas de survol) : la barre revient.
            if (!live && d->pos.y <= bounds().y + 8.f) {
                mouse_ = d->pos;
                mouseKnown_ = true;
                lastNear_ = now_;
                return R::Ignored;
            }
            if (!live || !box_.contains(d->pos)) return R::Ignored;
            if (d->button == ui::MouseButton::Left)
                for (const auto& p : parts_) {
                    if (!p.r.contains(d->pos)) continue;
                    const core::ActionId a = p.id == "zoomOut" ? "hmi.zoomOut" : p.id == "zoomIn" ? "hmi.zoomIn" : p.id == "fit" ? "hmi.zoomFit"
                                           : p.id == "zoom" ? "hmi.zoom100" : p.id == "exit" ? "hmi.fullscreen"
                                           : p.id == "fill" ? "hmi.fitFill" : p.id == "keep" ? "hmi.fitKeep" : p.id == "actual" ? "hmi.fitActual" : "";
                    if (!a.empty() && sink) sink(a);
                    break;
                }
            return R::Consumed;
        }
        if (const auto* u = std::get_if<ui::MouseUp>(&ev)) return live && box_.contains(u->pos) ? R::Consumed : R::Ignored;
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) return live && box_.contains(w->pos) ? R::Consumed : R::Ignored;
        return R::Ignored;
    }
private:
    struct Part {
        std::string id;
        gfx::Rect   r;
    };
    std::vector<Part> parts_;
    gfx::Rect         box_{};
    static constexpr float kEdge = 3.f;   // 1.12.25 : le bord du haut qui la ramene (px)
    gfx::Point        mouse_{-1.f, -1.f};
    double            now_{0}, painted_{-1}, lastNear_{-100}, shownUntil_{0}, toastUntil_{0};
    float             alpha_{1.f};
    bool              hover_{false}, mouseKnown_{false};
    std::string       note_;   // 1.12.24 : le message du bas (vide : comment sortir)
};

HmiSimulationPane::HmiSimulationPane(std::string id, hmi::DocumentPtr doc, HmiSimulationHost host)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), host_(std::move(host)) {
    const std::string base = this->id();
    auto bar = std::make_unique<ui::ToolBar>(base + ".bar");
    // 1.10 : LA BARRE DE L'IHM, plus celle de l'automate. L'IHM : son etat, Demarrer,
    // Arreter, Redemarrer ; l'API a cote, son etat et son bouton (le programme a ses
    // commandes dans la barre generale) ; la vue en cours (la liste, Precedente) ; le
    // zoom de la vue ; l'utilisateur ; les reperes.
    hmiChip_ = &bar->addCustom(std::make_unique<StateChip>(base + ".hmiState"));
    hmiStartBtn_ = &bar->addButton("D\xC3\xA9marrer l'IHM", "hmi.start", ui::Icon::Play);
    hmiStartBtn_->setTooltip("D\xC3\xA9marrer l'IHM (F8) : scripts, actions, alarmes, vues. Le programme de l'automate ne d\xC3\xA9marre pas "
                             "avec elle : sans l'API, l'IHM lit et \xC3\xA9" "crit la m\xC3\xA9moire de l'automate simul\xC3\xA9 (sans cycle).");
    hmiStopBtn_ = &bar->addButton("Arr\xC3\xAAter l'IHM", "hmi.stop", ui::Icon::Stop);
    hmiStopBtn_->setTooltip("Arr\xC3\xAAter l'IHM seule (Maj+F8) : l'API continue (ou reste arr\xC3\xAAt\xC3\xA9" "e)");
    auto& restartBtn = bar->addButton("Red\xC3\xA9marrer l'IHM", "hmi.restart", ui::Icon::Refresh);
    restartBtn.setTooltip("Red\xC3\xA9marrer (apr\xC3\xA8s confirmation) : les donn\xC3\xA9" "es de simulation et l'\xC3\xA9tat r\xC3\xA9manent effac\xC3\xA9s, "
                    "variables IHM \xC3\xA0 leur valeur initiale, esclaves simul\xC3\xA9s \xC3\xA0 leur d\xC3\xA9part, scripts et vue de d\xC3\xA9marrage "
                    "(l'API ne bouge pas)");
    // 1.11.15 : Generer et redemarrer - les donnees gardees, seul ce qui a change est refait.
    bar->addButton("G\xC3\xA9n\xC3\xA9rer et red\xC3\xA9marrer", "hmi.buildRestart", ui::Icon::Analyze)
        .setTooltip("G\xC3\xA9n\xC3\xA9rer et red\xC3\xA9marrer : les donn\xC3\xA9" "es de simulation gard\xC3\xA9" "es (l'option R\xC3\xA9manence coch\xC3\xA9" "e), "
                    "seul ce qui a chang\xC3\xA9 est g\xC3\xA9n\xC3\xA9r\xC3\xA9 et compil\xC3\xA9, puis la simulation repart avec ses donn\xC3\xA9" "es "
                    "(pas de remise \xC3\xA0 z\xC3\xA9ro)");
    {
        // 1.11.15 : l'option globale « Conserver les donnees de simulation entre les demarrages ».
        auto keep = std::make_unique<ui::ToggleButton>("R\xC3\xA9manence", base + ".keepData");
        keep->setChecked(host_.keepData && host_.keepData());
        keep->setTooltip("Conserver les donn\xC3\xA9" "es de simulation entre les d\xC3\xA9marrages : \xC3\xA0 l'arr\xC3\xAAt, les variables IHM et la "
                         "m\xC3\xA9moire des esclaves simul\xC3\xA9s sont gard\xC3\xA9" "es (.xpg/simulation du projet) et rendues au d\xC3\xA9marrage "
                         "suivant. Red\xC3\xA9marrer les efface. Jamais le stockage du poste d'exploitation.");
        keepButton_ = &static_cast<ui::ToggleButton&>(bar->addCustom(std::move(keep)));
        links_ += keepButton_->toggled->connect([this](bool on) { setKeepData(on); });
    }
    bar->addSeparator();
    plcChip_ = &bar->addCustom(std::make_unique<StateChip>(base + ".plcState"));
    plcChip_->setTooltip("L'API : le programme de l'automate simul\xC3\xA9. Ses commandes compl\xC3\xA8tes (pause, un cycle) "
                         "sont dans la barre du haut.");
    plcBtn_ = &bar->addButton("D\xC3\xA9marrer l'API", "plc.toggle", ui::Icon::Play);
    // 1.12.0 : IHM Studio n'a pas d'automate - ni sa pastille, ni son bouton.
    if (!core::hasApi()) {
        bar->setSlotHidden(*plcChip_, true);      // la barre decide de la visibilite de ses emplacements
        bar->setSlotHidden(*plcBtn_, true);
        hmiStartBtn_->setTooltip("D\xC3\xA9marrer l'IHM (F8) : scripts, actions, alarmes, vues ; ses variables, les esclaves simul\xC3\xA9s de ses \xC3\xA9quipements.");
        hmiStopBtn_->setTooltip("Arr\xC3\xAAter l'IHM (Maj+F8)");
        restartBtn.setTooltip("Red\xC3\xA9marrer (apr\xC3\xA8s confirmation) : les donn\xC3\xA9" "es de simulation et l'\xC3\xA9tat r\xC3\xA9manent effac\xC3\xA9s, "
                              "variables IHM \xC3\xA0 leur valeur initiale, esclaves simul\xC3\xA9s \xC3\xA0 leur d\xC3\xA9part, scripts et vue de d\xC3\xA9marrage");   // 1.12.14
    }
    bar->addSeparator();
    {
        auto list = std::make_unique<ui::DropDown>(base + ".viewList");
        list->setMaxVisibleRows(16);
        list->setTooltip("La vue en cours : en choisir une autre");
        viewList_ = &static_cast<ui::DropDown&>(bar->addCustom(std::move(list)));
    }
    bar->addButton("Pr\xC3\xA9" "c\xC3\xA9" "dente", "hmi.back", ui::Icon::Undo).setTooltip("Revenir \xC3\xA0 la vue d'avant");
    bar->addSeparator();
    // 1.10 : l'ordre de la maquette (scene 3) - l'utilisateur, les reperes, le zoom et
    // le plein ecran ; puis ce qui deborde au besoin dans le menu de la barre.
    loginBtn_ = &bar->addButton("Utilisateur...", "hmi.login", ui::Icon::User);
    loginBtn_->setTooltip("Changer d'utilisateur : mot de passe, code dynamique ou autorisation par expression");
    bar->addSeparator();
    // 1.9 : les reperes des lectures simulees, pour la seance (le projet les regle :
    // Configuration > Equipements, Reperer les lectures simulees).
    auto marks = std::make_unique<ui::ToggleButton>("Rep\xC3\xA8res", base + ".simMarks");
    marks->setChecked(true);
    marks->setTooltip("Les rep\xC3\xA8res des lectures simul\xC3\xA9" "es (cadre violet, pastille, bandeau) : les masquer pour cette s\xC3\xA9" "ance "
                      "seulement. Le projet les r\xC3\xA8gle dans Configuration \xE2\x80\xBA \xC3\x89quipements.");
    marksButton_ = &static_cast<ui::ToggleButton&>(bar->addCustom(std::move(marks)));
    links_ += marksButton_->toggled->connect([this](bool on) { setSimMarksShown(on); });
    bar->addSeparator();
    bar->addButton("\xE2\x88\x92", "hmi.zoomOut").setTooltip("Zoom arri\xC3\xA8re (Ctrl+molette)");
    zoomChip_ = &bar->addCustom(std::make_unique<StateChip>(base + ".zoom"));
    zoomChip_->setTooltip("Le zoom de la vue en cours. Ctrl+molette : autour du curseur ; une vue plus grande que la place "
                          "se d\xC3\xA9place (fond tir\xC3\xA9, molette, Maj+molette)");
    bar->addButton("+", "hmi.zoomIn").setTooltip("Zoom avant (Ctrl+molette)");
    bar->addButton("Ajuster", "hmi.zoomFit").setTooltip("La vue enti\xC3\xA8re dans la place");
    bar->addButton("100 %", "hmi.zoom100").setTooltip("La vue \xC3\xA0 sa taille r\xC3\xA9" "elle");
    // 1.10 (decision 12) : le simulateur IHM en plein ecran.
    bar->addButton("Plein \xC3\xA9" "cran", "hmi.fullscreen", ui::Icon::Expand)
        .setTooltip("La vue de l'IHM sur tout l'\xC3\xA9" "cran (F11) : Remplir l'\xC3\xA9" "cran, Garder les proportions ou Taille r\xC3\xA9" "elle, "
                    "au choix dans sa barre ; \xC3\x89" "chap ou F11 pour sortir");
    // 1.10.3 (demande du client) : LES 5 BOUTONS DE DROITE ET LA LISTE DEROULANTE DE
    // DROITE (le menu de ce qui debordait) SONT RETIRES. Leurs commandes restent :
    //   Les deux, Tout arreter       la barre du haut de l'application (et la carte
    //                                de l'IHM arretee : Demarrer les deux) ;
    //   Vue de demarrage             la liste des vues ;
    //   Deconnecter                  Utilisateur... ;
    //   Parametres systeme, Page     Ctrl+Alt+S (la page Simulation) ;
    //   Simulation
    //   Forcer..., Tout relacher     l'onglet Simulation . Automate ;
    // et toutes, avec Tout acquitter, Mesures a zero et Exporter les mesures, dans le
    // MENU DU CLIC DROIT SUR LA BARRE (kBarMenu) ; les sessions : simulation-barre.
    bar->setOverflowEnabled(false);
    bar->setTooltip("Clic droit : les autres commandes (D\xC3\xA9marrer les deux, Tout arr\xC3\xAAter, Vue de d\xC3\xA9marrage, "
                    "D\xC3\xA9" "connecter, Param\xC3\xA8tres syst\xC3\xA8me, Forcer, Tout acquitter, les mesures)");
    bar->setActionSink([this](core::ActionId a) { run(a); });
    bar_ = &static_cast<ui::ToolBar&>(addChild(std::move(bar)));

    auto split = std::make_unique<ui::Splitter>(ui::Orientation::Horizontal, base + ".split");
    canvas_ = &static_cast<HmiLiveCanvas&>(split->addPane(std::make_unique<HmiLiveCanvas>(base + ".view"), 0.56f, 240.f));
    canvas_->setAssets(&doc_->project.assets);
    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    {
        // 1.11.6 : l'arbre des expressions (objet, instance, propriete) et sa recherche.
        auto table = std::make_unique<JournalTable>(base + ".values");
        table->setColumns({{"Objet / propri\xC3\xA9t\xC3\xA9", 165.f}, {"Expression", 170.f}, {"Valeur", 140.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        table_ = table.get();
        auto* raw = table.get();
        // 1.12.18 (planche 16) : le menu de la ligne - Forcer... (Entree), Relacher (Suppr), Suivre sur
        // la courbe, Copier la valeur (Ctrl+C), Aller a l'objet, Deplier, Tout deplier, Tout replier.
        (void)raw;
        table->setExtraContextOnly(true);
        table->setExtraContextItems([this] { return exprMenuItems(); });
        links_ += table->contextAction->connect([this](int choice) {
            if (choice >= 1000 && static_cast<std::size_t>(choice - 1000) < viewMenuGo_.size()) runMenuKey(viewMenuGo_[static_cast<std::size_t>(choice - 1000)]);
        });
        table->setKeyHandler([this](const ui::KeyDown& k) {
            auto* tree = static_cast<ExprTree*>(model_.get());
            const auto sel = table_ ? table_->selectedModelRows() : std::vector<ui::RowIndex>{};
            if (!tree || sel.size() != 1) return false;
            const std::size_t vi = tree->valueAt(sel.front());
            if (vi >= values_.size()) return false;
            const auto& lv = values_[vi];
            if (k.key == ui::Key::Delete && k.mods.none() && plainPath(lv.expression)) {
                runMenuKey("relacher:" + lv.expression);
                return true;
            }
            if (k.key == ui::Key::C && k.mods.ctrl && !k.mods.shift && !k.mods.alt) {
                ui::setClipboardText(lv.value);
                return true;
            }
            return false;
        });
        auto page = std::make_unique<ExprPage>(base + ".valuesPage", std::move(table));
        exprSearch_ = &page->search();
        links_ += exprSearch_->textChanged->connect([this](const std::string& t) {
            if (auto* tree = static_cast<ExprTree*>(model_.get())) tree->rebuild(t);
            table_->setModel(model_);
        });
        tabs->addTab(ui::TabControl::Tab{"Expressions", ui::Icon::Code, false, false}, std::move(page));
    }
    // 1.11.24 (demande du client) : plus d'onglets Journal, Alarmes ni Recettes. Le journal de la
    // simulation est dans la Console du panneau du bas (1.11.14), les alarmes dans la vue (ses
    // objets d'alarmes) et dans la Console, les recettes dans leurs objets et l'onglet IHM > Recettes.
    {
        // 1.11.5 : les variables IHM en arbre (une structure a toute profondeur), la recherche,
        // le forcage (le moteur de l'IHM : forcee, elle ignore les ecritures).
        auto tree = std::make_unique<HmiSimVarTree>(base + ".variables", "IHM");
        ihmVars_ = tree.get();
        HmiSimVarTree::Hooks h;
        h.read = [this](const std::string& path) -> std::optional<sim::Value> {
            // 1.12.2 : une variable objet (LIST, VECTOR, MAP, TUPLE) : sa valeur ecrite, [1, 2, 3].
            if (runtime_.richVariable(path)) return sim::Value::text(runtime_.richText(path));
            // 1.11.7 : forcee dans son esclave simule - la valeur forcee tout de suite (la liaison ne
            // la relit qu'a son prochain cycle).
            updateTwinPlaces();
            if (const auto* tp = twinPlace(path))
                if (const auto* e = doc_->project.equipmentByName(tp->equipment))
                    for (const auto& f : e->forcings) {
                        if (!hmi::twin::sameCell(f.address, tp->row.address)) continue;
                        const double eng = tp->row.eng(f.value);
                        const sim::Value forced = tp->row.boolean ? sim::Value::boolean(eng != 0.0) : sim::Value::real(eng);
                        if (const auto* cur = runtime_.variable(path)) {
                            sim::Value v = *cur;
                            v.assignFrom(forced);
                            return v;
                        }
                        return forced;
                    }
            // 1.11.7 : liee a un equipement (un esclave simule), elle se relit sur lui, comme une vue
            // la lit - sans vue qui la montre, sa valeur restait celle du demarrage (forcee a 80 dans
            // l'esclave, l'onglet montrait 0).
            if (runtime_.boundVariable(path)) {
                sim::Value v;
                if (runtime_.environment().read(path, v)) return v;
            }
            if (const auto* v = runtime_.variable(path)) return *v;
            return std::nullopt;
        };
        // 1.11.7 : le forcage commun - une variable liee a un esclave simule se force dans l'esclave.
        h.forced = [this](const std::string& path) {
            if (runtime_.variableForced(path)) return true;
            updateTwinPlaces();
            const auto* tp = twinPlace(path);
            return tp && (twinForced(*tp) || twinBehavior(*tp) != nullptr);
        };
        h.rich = [this](const std::string& path) { return static_cast<bool>(runtime_.richVariable(path)); };
        h.force = [this](const std::string& path, const std::string& text, std::string* why) {
            // 1.12.2 : une variable objet recoit un litteral ([1, 2, 3], ['a' := 1]) : une ecriture,
            // les scripts la changent ensuite (elle n'est pas tenue).
            if (runtime_.richVariable(path)) return runtime_.setRichVariable(path, text, why);
            const auto* cur = runtime_.variable(path);
            if (!cur) {
                if (why) *why = path + " : l'IHM ne tourne pas (le bouton D\xC3\xA9marrer l'IHM)";
                return false;
            }
            // 1.12.19 (la regle des cases) : une valeur d'enumeration se tape par son nom (Auto) ou
            // T_MODE#Auto, comme partout ; le reste par la lecture commune (sim::readTyped).
            std::string bad;
            std::string typed = text;
            if (doc_)
                if (const auto* declared = doc_->project.variable(path); declared && !text.empty())
                    typed = hmi::enumConstantNumber(doc_->project, declared->type, text);
            const auto v = text.empty() ? std::optional<sim::Value>(*cur) : parseTypedValue(typed, cur, &bad);
            if (!v) {
                if (why) *why = path + " : " + bad;
                return false;
            }
            updateTwinPlaces();
            if (const auto* tp = twinPlace(path); tp && twinsCtl_) {
                // La case de l'esclave, en brut (sa mise a l'echelle) : les deux onglets la voient forcee.
                const double eng = v->type() == sim::Type::Bool ? (v->isTruthy() ? 1.0 : 0.0) : v->asReal();
                std::string raw = hmi::twin::numberText(tp->row.raw(eng));
                std::replace(raw.begin(), raw.end(), ',', '.');
                const std::string equipment = tp->equipment, address = tp->row.address;
                return twinsCtl_->setForced(equipment, address, raw, why);
            }
            return runtime_.forceVariable(path, *v, why);
        };
        // 1.11.6 : liberer arrete aussi son mouvement ; 1.11.7 : dans l'esclave aussi.
        h.unforce = [this](const std::string& path) {
            ihmMotions_.erase(path);
            bool done = runtime_.unforceVariable(path);
            updateTwinPlaces();
            if (const auto* tp = twinPlace(path); tp && twinsCtl_) {
                const std::string equipment = tp->equipment, address = tp->row.address;
                const bool forced = twinForced(*tp), animated = twinBehavior(*tp) != nullptr;
                if (forced) done = twinsCtl_->setForced(equipment, address, {}) || done;
                if (animated) done = twinsCtl_->setBehavior(equipment, address, std::nullopt) || done;
            }
            return done;
        };
        h.nodeType = [this](const std::string& path) { return hmi::types::typeOfPath(doc_->project, path); };
        // 1.11.6 : le forcage par type et bornes.
        h.motion = [this](const std::string& path) -> std::optional<hmi::motion::Motion> {
            // 1.11.7 : liee a un esclave simule, son mouvement est celui de la case de l'esclave.
            updateTwinPlaces();
            if (const auto* tp = twinPlace(path))
                if (const auto* b = twinBehavior(*tp)) return motionOf(*b, tp->row);
            const auto it = ihmMotions_.find(path);
            return it == ihmMotions_.end() ? std::nullopt : std::optional<hmi::motion::Motion>(it->second.motion);
        };
        h.setMotion = [this](const std::string& path, const std::optional<hmi::motion::Motion>& m, std::string* why) {
            updateTwinPlaces();
            if (const auto* tp = twinPlace(path); tp && twinsCtl_) {
                const std::string equipment = tp->equipment, address = tp->row.address;
                const auto row = tp->row;
                ihmMotions_.erase(path);
                return twinsCtl_->setBehavior(equipment, address, m ? std::optional<hmi::Behavior>(behaviorOf(*m, row)) : std::nullopt, why);
            }
            if (!m) {
                ihmMotions_.erase(path);
                (void)runtime_.unforceVariable(path);
                return true;
            }
            if (!runtime_.variable(path)) {
                if (why) *why = path + " : l'IHM ne tourne pas (le bouton D\xC3\xA9marrer l'IHM)";
                return false;
            }
            auto& on = ihmMotions_[path];
            on.motion = *m;
            applyMotions();
            return true;
        };
        // 1.12.18 (planche 16) : Suivre sur la courbe - Simulation > Courbes (ihm: : une variable de l'IHM).
        if (host_.follow) h.follow = [this](const std::string& path, std::string* why) { return host_.follow("ihm:" + path, why); };
        tree->setHooks(std::move(h));
        tree->setEmptyText("Aucune variable IHM (Programmation g\xC3\xA9n\xC3\xA9rale \xE2\x80\xBA Variables IHM).");
        links_ += tree->said->connect([this](const std::string& t, bool error) {
            if (status_) status_->setTransientMessage(t, 6.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
        });
        tabs->addTab(ui::TabControl::Tab{"Variables IHM", ui::Icon::Library, false, false}, std::move(tree));
    }
    {
        // 1.11.5 : les variables de l'automate simule, en arbre, avec son forcage (sim::Runtime).
        auto tree = std::make_unique<HmiSimVarTree>(base + ".apiVariables", "API");
        apiVars_ = tree.get();
        const auto plc = [this]() -> sim::Runtime* { return host_.runtime ? host_.runtime() : nullptr; };
        HmiSimVarTree::Hooks h;
        h.read = [plc](const std::string& path) -> std::optional<sim::Value> {
            sim::Value v;
            if (auto* rt = plc(); rt && rt->get(path, v)) return v;
            return std::nullopt;
        };
        h.forced = [plc](const std::string& path) {
            auto* rt = plc();
            return rt && rt->isForced(path);
        };
        h.force = [plc](const std::string& path, const std::string& text, std::string* why) {
            auto* rt = plc();
            sim::Value cur;
            if (!rt || !rt->get(path, cur)) {
                if (why) *why = path + " : la simulation de l'automate n'est pas pr\xC3\xAAte (F9 la lance)";
                return false;
            }
            std::string bad;
            const auto v = text.empty() ? std::optional<sim::Value>(cur) : parseTypedValue(text, &cur, &bad);
            if (!v) {
                if (why) *why = path + " : " + bad;
                return false;
            }
            return rt->force(path, *v);
        };
        h.unforce = [this, plc](const std::string& path) {
            apiMotions_.erase(path);                                // 1.11.6 : son mouvement s'arrete
            auto* rt = plc();
            return rt && rt->unforce(path);
        };
        // 1.11.6 : le forcage par type et bornes.
        h.motion = [this](const std::string& path) -> std::optional<hmi::motion::Motion> {
            const auto it = apiMotions_.find(path);
            return it == apiMotions_.end() ? std::nullopt : std::optional<hmi::motion::Motion>(it->second.motion);
        };
        h.setMotion = [this, plc](const std::string& path, const std::optional<hmi::motion::Motion>& m, std::string* why) {
            auto* rt = plc();
            if (!m) {
                apiMotions_.erase(path);
                if (rt) (void)rt->unforce(path);
                return true;
            }
            sim::Value cur;
            if (!rt || !rt->get(path, cur)) {
                if (why) *why = path + " : la simulation de l'automate n'est pas pr\xC3\xAAte (F9 la lance)";
                return false;
            }
            auto& on = apiMotions_[path];
            on.motion = *m;
            applyMotions();
            return true;
        };
        if (host_.follow) h.follow = [this](const std::string& path, std::string* why) { return host_.follow(path, why); };
        tree->setHooks(std::move(h));
        tree->setEmptyText("La simulation de l'automate n'est pas pr\xC3\xAAte : F9 la lance (l'IHM peut lire un vrai automate, qui ne se force pas ici).");
        links_ += tree->said->connect([this](const std::string& t, bool error) {
            if (status_) status_->setTransientMessage(t, 6.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
        });
        const auto apiTab = tabs->addTab(ui::TabControl::Tab{"Variables API", ui::Icon::LocatedVariable, false, false}, std::move(tree));
        // 1.12.0 : IHM Studio n'a pas d'automate - l'onglet reste (ses rangs aussi), cache.
        if (!core::hasApi()) tabs->setTabHidden(apiTab, true);
    }
    {
        // Lot 13 : les performances - le cycle IHM, les scripts, l'evaluation, le dessin.
        auto table = std::make_unique<ui::TableView>(base + ".perf");
        table->setColumns({{"Mesure", 300.f}, {"Derni\xC3\xA8re", 100.f}, {"Moyenne", 100.f}, {"Maximum", 100.f}, {"Nombre", 96.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        perf_ = table.get();
        // 1.12.17 : au-dessus du tableau, « Dans le temps > » (la Vue d'ensemble de Simulation).
        auto page = std::make_unique<PerfPage>(base + ".perfPage", std::move(table));
        links_ += page->link().clicked->connect([this]() {
            if (host_.goSimCenter) host_.goSimCenter("performances");
        });
        tabs->addTab(ui::TabControl::Tab{"Performances", ui::Icon::Chart, false, false}, std::move(page));
    }
    {
        // Lot 18 : les jumeaux - animer, la zone de mouvement, forcer, pendant la marche.
        auto twins = std::make_unique<HmiTwinValues>(base + ".twins", true);
        twins_ = twins.get();
        twinsCtl_ = std::make_unique<TwinValuesController>(doc_, [this](core::CommandPtr c) {
            if (!c) return;
            if (host_.apply) host_.apply(std::move(c));
            else (void)c->execute();
        });
        twinsCtl_->setHost([this]() -> EquipmentHost* { return host_.equipments ? host_.equipments() : nullptr; });
        twinsCtl_->say = [this](const std::string& t, bool error) {
            if (status_) status_->setTransientMessage(t, 8.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
            if (twins_) twins_->setStatus(t);
        };
        twinsCtl_->attach(*twins_);
        tabs->addTab(ui::TabControl::Tab{"Esclaves simul\xC3\xA9s", ui::Icon::Play, false, false}, std::move(twins));
    }
    {
        // 1.12.27 : Pourquoi ? - ce que montre l'objet choisi (clic droit > Pourquoi ?), et pourquoi.
        auto table = std::make_unique<ui::TableView>(base + ".pourquoi");
        table->setColumns({{"Ce qui compte", 170.f}, {"Formule ou terme", 220.f}, {"Maintenant", 150.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        table->setExtraContextOnly(true);
        table->setExtraContextItems([this] { return whyMenuItems(); });
        links_ += table->contextAction->connect([this](int choice) {
            if (choice >= 1000 && static_cast<std::size_t>(choice - 1000) < viewMenuGo_.size()) runMenuKey(viewMenuGo_[static_cast<std::size_t>(choice - 1000)]);
        });
        whyTable_ = table.get();
        // Le double clic sur un nom lu : le forcer (comme dans Variables IHM).
        links_ += table->activated->connect([this](ui::RowIndex r) {
            const auto* rows = static_cast<const WhyRows*>(whyModel_.get());
            if (!rows || r >= rows->lines.size()) return;
            const auto& l = rows->lines[r];
            if (l.item >= whyReport_.items.size() || l.term < 0) return;
            const auto& item = whyReport_.items[l.item];
            if (static_cast<std::size_t>(l.term) < item.terms.size() && item.terms[static_cast<std::size_t>(l.term)].name)
                runMenuKey("forcer:" + item.terms[static_cast<std::size_t>(l.term)].text);
        });
        auto page = std::make_unique<WhyPage>(base + ".pourquoiPage", std::move(table));
        links_ += page->copy().clicked->connect([this] {
            if (whyObject_ == kNoId) return;
            ui::setClipboardText(hmi::why::toText(whyReport_));
            if (status_) status_->setTransientMessage("Pourquoi ? : le rapport de " + whyReport_.name + " est copi\xC3\xA9.", 5.0,
                                                      ui::StatusBar::Severity::Success);
        });
        whyPage_ = page.get();
        tabs->addTab(ui::TabControl::Tab{"Pourquoi ?", ui::Icon::Help, false, false}, std::move(page));
        whyModel_ = std::make_shared<WhyRows>();
        whyTable_->setModel(whyModel_);
    }
    {
        // 1.13.0 (lot J2) : Debogage - les points d'arret des scripts IHM, la pause, le pas a pas, la pile, les locales.
        auto page = std::make_unique<HmiDebugPane>(base + ".debogage", doc_, [this]() -> hmi::Runtime* { return &runtime_; });
        page->goTo = [this](hmi::Id script, hmi::Id function, int line) {
            if (host_.goToCode) host_.goToCode(script, function, line);
        };
        debugPane_ = page.get();
        tabs->addTab(ui::TabControl::Tab{"D\xC3\xA9" "bogage", ui::Icon::Breakpoint, false, false}, std::move(page));
    }
    // 1.9 : les popups ouvertes et les copies de leurs parametres (HmiParamPanes) - le dernier onglet.
    tabs->addTab(ui::TabControl::Tab{"Popups", ui::Icon::Document, false, false}, hmiparams::makePopupsTab(base + ".popups"));
    tabs->setCurrentIndex(0);
    tabs_ = &static_cast<ui::TabControl&>(split->addPane(std::move(tabs), 0.44f, 220.f));
    ui::cmd::wireSubTabMenu(*tabs_, *this, links_);   // 1.12.18 (bug 7) : choisi, puis Disposition...
    disposition::applyTabs(*tabs_, "sim", true);       // 1.12.3 : Projet > Disposition
    split_ = &static_cast<ui::Splitter&>(addChild(std::move(split)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    // 1.9 : a droite, "Variateur ATV320 et Balance B lus en simule" (violet, la fiole).
    simReads_ = &status_->addIndicator(std::make_unique<simmark::Indicator>(base + ".simReads"), ui::StatusBar::Slot::Right);

    model_ = std::make_shared<ExprTree>(&values_);           // 1.11.6 : en arbre
    table_->setModel(model_);
    links_ += table_->expanderClicked->connect([this](ui::RowIndex r) {
        static_cast<ExprTree*>(model_.get())->toggle(r);
        table_->setModel(model_);
    });

    canvas_->setLive(&runtime_, &doc_->history);
    canvas_->setProject(&doc_->project);
    runtime_.setHistory(&doc_->history);
    hmi::Runtime::Hooks hooks;
    hooks.askLogin = [this](const std::string& login) {
        if (host_.login) host_.login(login);
        else status_->setTransientMessage("Changer d'utilisateur : " + login + " (aucun dialogue ici)", 6.0);
    };
    hooks.playSound = [this](const std::string& name) {
        if (const auto* r = doc_->project.resourceByName(name)) {
            std::string why;
            // Lot 10 : au volume regle dans Parametres systeme.
            const float gain = static_cast<float>(std::clamp(runtime_.settings().volume, 0, 100)) / 100.f;
            // 1.12.3 : mele aux sons qui jouent deja (deux alarmes s'entendent ensemble).
            if (!HmiSoundPlayer::instance().mix(*r, &why, gain)) status_->setTransientMessage("Son " + name + " : " + why, 6.0);
        }
    };
    // 1.12.3 : Silence (Faire taire) et l'arret coupent ce qui joue.
    hooks.stopSounds = [] { HmiSoundPlayer::instance().stopMix(); };
    // Lot 10 : "Redemarrer l'IHM" du menu Parametres systeme - comme le bouton.
    hooks.restart = [this] { restart(); };
    hooks.recipeRequest = [this](const hmi::RecipeRequest& rq) {
        if (host_.recipe) host_.recipe(rq);
        else status_->setTransientMessage("Recette " + rq.recipe + " : " + rq.op + " (aucun dialogue ici)", 6.0);
    };
    hooks.requestResource = [this](const hmi::ResourceRequest& rq) {
        if (host_.resource) host_.resource(rq);
        else status_->setTransientMessage("Demander une ressource : aucun s\xC3\xA9lecteur ici", 6.0);
    };
    hooks.userRequest = [this](const hmi::UserRequest& rq) {
        if (host_.users) host_.users(rq);
        else status_->setTransientMessage("Utilisateurs : " + rq.op + " (aucun dialogue ici)", 6.0);
    };
    // Lot 9 : SYS.PlcRunning, SYS.PlcScanCount... - l'etat du simulateur.
    hooks.plcStatus = [this] { return host_.plc ? host_.plc() : hmi::PlcStatus{}; };
    // Lot 11 : l'export (bouton d'export, action Exporter, IHM_EXPORTER).
    hooks.exportFile = [this](const hmi::ExportRequest& rq, std::string* where) {
        if (!host_.exportFile) {
            if (where) *where = "aucun dossier o\xC3\xB9 \xC3\xA9" "crire ici";
            return false;
        }
        std::string path;
        const bool ok = host_.exportFile(rq, &path);
        if (where) *where = path;
        status_->setTransientMessage(ok ? "Export\xC3\xA9 : " + path + " (" + std::to_string(rq.rows) + " ligne(s), " + rq.format + ")"
                                        : "Export impossible : " + path,
                                     8.0, ok ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Warning);
        return ok;
    };
    hooks.projectFolder = [this] { return host_.projectFolder ? host_.projectFolder() : std::string{}; };   // 1.12.26
    // ---- Lot API 8 : les exports qui demandent ou ----
    //  Un export parti d'un geste de l'operateur, l'option cochee (le moteur en
    //  decide) : l'hote pose la question et ecrit a la reponse ; le moteur
    //  l'apprend (exportAnswered : SYS.LastExport, l'evenement, le journal). Pas
    //  pendant un essai rejoue (il se joue sans s'arreter), ni pour un clic venu
    //  d'un navigateur (personne devant l'ecran) : exports/ sans question.
    hooks.askExport = [this](const hmi::ExportRequest& rq) {
        if (!host_.askExport || scenarioRunning() || exportQuestionsMuted()) return false;
        const std::weak_ptr<char> alive = exportAlive_;
        return host_.askExport(rq, [this, alive, rq](bool ok, const std::string& where) {
            if (alive.expired()) return;          // le volet est parti entretemps
            runtime_.exportAnswered(rq, ok, where, now_);
            if (ok)
                status_->setTransientMessage("Export\xC3\xA9 : " + where + " (" + std::to_string(rq.rows) + " ligne(s), " + rq.format + ")", 8.0,
                                             ui::StatusBar::Severity::Success);
            else
                status_->setTransientMessage(where.empty() ? std::string("Export annul\xC3\xA9 : rien n'est \xC3\xA9" "crit") : "Export impossible : " + where,
                                             8.0, where.empty() ? ui::StatusBar::Severity::Info : ui::StatusBar::Severity::Warning);
            refreshNow();
        });
    };
    // ---- fin Lot API 8 ----
    // Lot 13 : le journal d'audit, ligne a ligne sur le disque.
    hooks.audited = [this](const hmi::AuditEntry& e) {
        if (host_.audit) host_.audit(e);
    };
    hooks.commDemo = host_.commDemo;   // lot 14 : le serveur de demonstration
    hooks.station = host_.station;     // lot 14 : le poste d'exploitation
    hooks.alarmNotice = host_.alarmNotice;       // lot 14 : les notifications
    hooks.notifyStats = host_.notifyStats;
    hooks.reportWritten = host_.reportWritten;   // lot 14 : les rapports a envoyer
    hooks.equipmentLink = host_.equipmentLink;   // lot 15 : les equipements du reseau
    // 1.11.7 : le forcage commun - une variable liee forcee (ou animee) dans son esclave : le
    // forcage passe avant les scripts.
    hooks.boundForced = [this](const std::string& key) {
        updateTwinPlaces();
        const auto* tp = twinPlace(key);
        return tp && (twinForced(*tp) || twinBehavior(*tp) != nullptr);
    };
    hooks.equipmentStatus = host_.equipmentStatus;
    hooks.simSlaves = host_.simSlaves;           // 1.9 : les esclaves simules (la page Simulation)
    hooks.simSlaveCommand = host_.simSlaveCommand;
    hooks.journaled = host_.console;             // 1.11.14 : la Console du panneau du bas
    runtime_.setHooks(std::move(hooks));
    // 1.13.0 (lot J2) : le debogueur des scripts IHM (la simulation de l'editeur ; le poste ne s'arrete jamais).
    runtime_.setDebugger(true);
    HmiDebugSession::instance().bindProject(doc_);
    HmiDebugSession::instance().setRuntime(&runtime_);

    links_ += doc_->changed->connect([this](Id) {
        bound_ = false;
        twinsDirty_ = true;           // lot 18 : les lignes des jumeaux suivent le projet
        noteTwinChanges();            // ... et les courbes marquent ce qui a change
    });
    noteTwinChanges();
    // 1.13.0 (le debogueur) : EN PAUSE, la vue est figee - un geste n'y fait rien, et la barre d'etat le dit une fois.
    const auto frozen = [this] {
        if (!runtime_.paused()) return false;
        if (status_)
            status_->setTransientMessage("L'IHM est en pause (d\xC3\xA9" "bogage) : la vue ne r\xC3\xA9pond pas. F5 continue, F10 fait un pas.", 4.0);
        return true;
    };
    links_ += canvas_->pressed->connect([this, frozen](Id object) {
        if (frozen()) return;
        runtime_.press(object, now_);
        const auto* v = doc_->project.view(runtime_.topView());
        const auto* o = v ? v->object(object) : nullptr;
        // Lot 9 : une commande n'a pas besoin d'action - elle ecrit sa variable ;
        // lot 10 : l'objet Parametres systeme non plus - il ouvre le menu natif.
        if (o && o->actions.empty() && !hmi::kindWritesVariable(o->kind) && o->kind != hmi::Kind::SystemButton
            && o->kind != hmi::Kind::LoginMenuButton && o->kind != hmi::Kind::LanguageSelector && o->kind != hmi::Kind::ThemeSelector
            && o->kind != hmi::Kind::PlcDiagnostic)
            status_->setTransientMessage(o->name + " : aucune action sur cet objet", 4.0);
    });
    links_ += canvas_->released->connect([this](Id object, bool inside) { runtime_.release(object, now_, inside); });
    // 1.11.23 : la souris et le clavier (SYS.Mouse*, SYS.Key*) et les raccourcis des vues.
    links_ += canvas_->pointerMoved->connect([this](Id view, double x, double y, Id object, bool inside) {
        runtime_.pointerMoved(view, x, y, object, inside);
    });
    links_ += canvas_->pointerButton->connect([this](int button, bool down) { runtime_.pointerButton(button, down, now_); });
    links_ += canvas_->wheelTurned->connect([this](double notches) { runtime_.pointerWheel(notches); });
    links_ += canvas_->keysLost->connect([this] {
        runtime_.releaseAllKeys(now_);
        refreshNow();
    });
    canvas_->setKeyHandler([this](const std::string& token, ui::KeyMods mods, bool down, bool repeat) {
        if (token.empty()) {                                   // Ctrl, Maj ou Alt seules
            runtime_.keyModifiers(mods.ctrl, mods.shift, mods.alt);
            return false;
        }
        bool taken = false;
        if (down) {
            hmi::keys::Chord chord;
            chord.key = token;
            chord.ctrl = mods.ctrl;
            chord.shift = mods.shift;
            chord.alt = mods.alt;
            taken = runtime_.keyDown(chord, now_, repeat);
        } else {
            taken = runtime_.keyUp(token, now_);
            runtime_.keyModifiers(mods.ctrl, mods.shift, mods.alt);
        }
        if (taken && !repeat) refreshNow();
        return taken;
    });
    // Lot 6 : le gestionnaire de recettes (une ligne, un bouton) ; les
    // parametres systeme se ferment d'un clic.
    links_ += canvas_->partClicked->connect([this](Id object, const std::string& part) {
        runtime_.objectPart(object, part, now_);
        refreshNow();
    });
    // Lot 8 : les popups (la croix, un clic dehors, tiree, ramenee devant) et la
    // saisie au clavier.
    links_ += canvas_->popupCloseRequested->connect([this](int slot, bool instant) {
        hmi::Transition t;
        t.kind = instant ? hmi::TransitionKind::Instant : hmi::TransitionKind::Fade;
        t.durationMs = instant ? 0 : 200;
        (void)runtime_.closePopupAt(static_cast<std::size_t>(std::max(0, slot)), t, now_);
        refreshNow();
    });
    links_ += canvas_->popupMoved->connect([this](int slot, double x, double y) {
        runtime_.movePopup(static_cast<std::size_t>(std::max(0, slot)), x, y);
        refreshNow();
    });
    links_ += canvas_->popupRaised->connect([this](int slot) {
        (void)runtime_.raisePopup(static_cast<std::size_t>(std::max(0, slot)));
        refreshNow();
    });
    // Lot 12 : glisser pour changer de vue ; le zoom de la vue (SYS.ViewZoom).
    links_ += canvas_->swiped->connect([this](int direction) {
        std::string why;
        if (!runtime_.swipe(direction, now_, &why) && !why.empty()) status_->setTransientMessage("Glisser : " + why, 4.0);
        refreshNow();
    });
    links_ += canvas_->viewZoomed->connect([this](double percent) {
        runtime_.setViewZoom(percent);
        status_->setTransientMessage("Zoom de la vue : " + std::to_string(static_cast<int>(std::lround(percent))) + " %", 3.0);
    });
    // Lot 9 : le curseur et le potentiometre.
    links_ += canvas_->valueDragged->connect([this, frozen](Id object, double fraction, bool commit) {
        if (frozen()) return;
        runtime_.dragValue(object, fraction, commit, now_);
        refreshNow();
    });
    links_ += canvas_->textTyped->connect([this](const std::string& text) {
        runtime_.typeText(text, now_);
        refreshNow();
    });
    links_ += canvas_->keyTyped->connect([this](int key) {
        runtime_.typeKey(static_cast<hmi::EditKey>(key), now_);
        refreshNow();
    });
    links_ += canvas_->clickedAway->connect([this](Id towards) {
        runtime_.unfocus(now_, towards);
        refreshNow();
    });
    // Lot 10 : le menu natif Parametres systeme, l'ecran en veille.
    links_ += canvas_->loginPartClicked->connect([this, frozen](const std::string& part) {
        if (frozen()) return;
        runtime_.loginPart(part, now_);
        refreshNow();
    });
    links_ += canvas_->systemPartClicked->connect([this, frozen](const std::string& part) {
        if (frozen()) return;
        runtime_.systemPart(part, now_);
        refreshNow();
    });
    // 1.12.24 : la fenetre du Reglage PID ; Copier les valeurs passe par le presse-papiers.
    links_ += canvas_->pidPartClicked->connect([this, frozen](const std::string& part) {
        if (frozen()) return;
        runtime_.pidPart(part, now_);
        if (part == "copier" && !runtime_.pidWindow().clipboard.empty()) ui::setClipboardText(runtime_.pidWindow().clipboard);
        refreshNow();
    });
    links_ += canvas_->pidHovered->connect([this](double t) {
        runtime_.pidHover(t);
        canvas_->invalidate();
    });
    // Lot 13 : le panneau de signature ; rester connecte (l'avertissement).
    links_ += canvas_->promptPartClicked->connect([this](const std::string& part) {   // 1.11.7
        runtime_.promptPart(part, now_);
        refreshNow();
    });
    links_ += canvas_->signaturePartClicked->connect([this, frozen](const std::string& part) {
        if (frozen()) return;
        runtime_.signaturePart(part, now_);
        refreshNow();
    });
    links_ += canvas_->stayRequested->connect([this, frozen] {
        if (frozen()) return;
        runtime_.stayConnected(now_);
        status_->setTransientMessage("Toujours connect\xC3\xA9 : la minuterie repart", 4.0, ui::StatusBar::Severity::Success);
        refreshNow();
    });
    links_ += canvas_->wakeRequested->connect([this, frozen] {
        if (frozen()) return;
        (void)runtime_.wake(now_);
        refreshNow();
    });
    links_ += canvas_->doubleClicked->connect([this, frozen](Id object) {
        if (!frozen()) runtime_.doubleClick(object, now_);
    });
    // 1.12.18 (planche 16) : le clic droit sur la vue en marche - le menu de l'objet, sauf quand le
    // projet lit le bouton droit (Maj+clic droit alors) ; sur le poste, Acquitter seulement.
    canvas_->setContextWanted([this](gfx::Point at, bool shift) {
        if (station_) {
            Id view = kNoId, obj = kNoId;
            if (!canvas_->hitAt(at, view, obj)) return false;
            return openViewMenu(at);   // le poste : le menu ne s'ouvre que sur une alarme a acquitter
        }
        if (shift || !projectReadsRightButton()) return true;
        // Le clic droit va au projet : la barre d'etat dit comment avoir le menu.
        if (status_)
            status_->setTransientMessage("Le projet lit le bouton droit de la souris : le clic droit lui revient. "
                                         "Maj+clic droit ouvre le menu.", 5.0);
        return false;
    });
    links_ += canvas_->contextRequested->connect([this](gfx::Point at) {
        if (!station_) (void)openViewMenu(at);
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        auto* tree = static_cast<ExprTree*>(model_.get());
        const std::size_t i = tree->valueAt(r);
        if (i == std::string::npos) {   // 1.11.6 : un noeud se replie ou se deplie
            tree->toggle(r);
            table_->setModel(model_);
            return;
        }
        if (i >= values_.size() || !host_.force) return;
        const auto& e = values_[i].expression;
        host_.force(plainPath(e) ? e : std::string{});
    });
    // Double-clic sur une alarme : l'acquitter ; sur un jeu : l'appliquer (1.11.24 : sans leurs onglets, plus rien).
    if (alarms_) links_ += alarms_->activated->connect([this](ui::RowIndex r) {
        const auto& list = runtime_.alarms();
        if (r >= list.size()) return;
        std::string why;
        const auto n = runtime_.acknowledge(list[r].name, now_, &why);
        status_->setTransientMessage(n ? list[r].name + " acquitt\xC3\xA9" "e" : "Acquittement refus\xC3\xA9 : " + why, 6.0,
                                     n ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Warning);
        refreshNow();
    });
    if (recipes_) links_ += recipes_->activated->connect([this](ui::RowIndex r) {
        if (r >= recipeRows_.size()) return;
        (void)applyRecipe(recipeRows_[r].first, recipeRows_[r].second);
    });
    // 1.10 : la liste des vues de la barre ; le zoom (la valeur dans la barre).
    links_ += viewList_->selectionChanged->connect([this](int i) {
        if (listing_ || i < 0 || static_cast<std::size_t>(i) >= viewIds_.size()) return;
        if (viewIds_[static_cast<std::size_t>(i)] != runtime_.currentView()) goToView(viewIds_[static_cast<std::size_t>(i)]);
    });
    links_ += canvas_->displayZoomChanged->connect([this] { refreshBar(); });
    links_ += canvas_->stoppedAction->connect([this](const std::string& a) { run(a); });
    // 1.10 (decision 12) : la barre flottante du plein ecran, le dernier enfant (devant tout).
    {
        auto fs = std::make_unique<FullScreenBar>(base + ".fullBar");
        fs->viewName = [this] {
            for (const auto& v : doc_->project.views)
                if (v.id == runtime_.currentView()) return v.name;
            return std::string{};
        };
        // 1.12.24 : Remplir etire la vue, une echelle par axe : "150 x 135 %".
        fs->zoomText = [this] {
            const int zx = static_cast<int>(std::lround(canvas_->shownZoomX() * 100.f)), zy = static_cast<int>(std::lround(canvas_->shownZoomY() * 100.f));
            return zx == zy ? std::to_string(zx) + " %" : std::to_string(zx) + " \xC3\x97 " + std::to_string(zy) + " %";
        };
        fs->fitShown = [this] { return canvas_->displayZoom() > 0.f ? -1 : static_cast<int>(screenFit_); };
        fs->running = [this] { return started_; };
        fs->sink = [this](core::ActionId a) { run(a); };
        fs->escapeFirst = [this](const ui::InputEvent& e) { return canvas_->dispatch(e) == ui::EventResult::Consumed; };
        fs->setVisibility(ui::Visibility::Collapsed);
        fsBar_ = &addChild(std::move(fs));
    }
    if (host_.screenFit) screenFit_ = host_.screenFit();   // 1.12.24 : la mise a l'echelle retenue
    simPanes().push_back(this);
    refreshBar();
}

// ------------------------------------------ 1.10 (decision 12) : le plein ecran ----
void HmiSimulationPane::setFullScreen(bool on) {
    if (on == fullScreen_ || (on && station_)) return;
    fullScreen_ = on;
    const auto shown = on ? ui::Visibility::Collapsed : ui::Visibility::Visible;
    bar_->setVisibility(shown);
    status_->setVisibility(shown);
    split_->collapsePane(1, on);
    fsBar_->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (on) {
        zoomBeforeFull_ = canvas_->displayZoom();
        // 1.12.24 : la vue sur tout l'ecran, a la mise a l'echelle retenue (Remplir par defaut).
        if (host_.screenFit) screenFit_ = host_.screenFit();
        canvas_->setScreenFit(screenFit_);
        canvas_->setWholeScreen(true);
        canvas_->setDisplayZoom(0.f);   // ajustee (selon ce choix)
        static_cast<FullScreenBar*>(fsBar_)->enter(now_);
    } else {
        canvas_->setWholeScreen(false);
        canvas_->setDisplayZoom(zoomBeforeFull_);
        status_->setTransientMessage("Sortie du plein \xC3\xA9" "cran", 3.0);
    }
    if (host_.fullScreenWindow) host_.fullScreenWindow(on);
    invalidateLayout();
    invalidate();
}

// 1.12.24 : LA MISE A L'ECHELLE EN PLEIN ECRAN (la barre flottante) - retenue par l'hote ;
// en plein ecran, la vue la prend tout de suite (un zoom choisi s'efface). Le poste suit son
// projet (station.screenFit), pas ce choix.
void HmiSimulationPane::setScreenFit(hmi::ScreenFit fit) {
    if (station_) return;
    screenFit_ = fit;
    if (host_.setScreenFit) host_.setScreenFit(fit);
    canvas_->setScreenFit(fit);
    if (!fullScreen_) return;
    canvas_->setDisplayZoom(0.f);
    const char* what = fit == hmi::ScreenFit::Fill   ? " : la vue \xC3\xA9tir\xC3\xA9" "e jusqu'aux bords de l'\xC3\xA9" "cran"
                       : fit == hmi::ScreenFit::Keep ? " : la vue enti\xC3\xA8re, les bandes \xC3\xA0 sa couleur de fond"
                                                     : " : un pixel de la vue pour un pixel de l'\xC3\xA9" "cran";
    static_cast<FullScreenBar*>(fsBar_)->say(std::string(hmi::screenFitLabel(fit)) + what);
}

bool HmiSimulationPane::fullScreenBarShown() const {
    return fullScreen_ && static_cast<const FullScreenBar*>(fsBar_)->shown();
}

gfx::Rect HmiSimulationPane::fullScreenBarPart(std::string_view part) const {
    return fullScreen_ ? static_cast<const FullScreenBar*>(fsBar_)->part(part) : gfx::Rect{};
}

bool HmiSimulationPane::claimsKey(ui::Key key, const ui::Widget* windowRoot) {
    if (key != ui::Key::F11) return false;
    for (const auto* p : simPanes()) {
        // La fenetre du volet : sa racine ("detached.root" : une fenetre detachee,
        // DetachedWindows) - F11 d'une fenetre ne va qu'aux volets de cette fenetre.
        const ui::Widget* top = p;
        while (top->parent()) top = top->parent();
        if (windowRoot ? top != windowRoot : top->id() == "detached.root") continue;
        if (p->fullScreen_) return true;
        if (!p->station_ && p->debugPane_ && p->runtime_.paused()) return true;   // 1.13.0 : Entrer, pas le plein ecran
        if (p->station_ || !p->host_.fullScreenWindow || p->bounds().empty()) continue;
        bool shown = true;
        for (const ui::Widget* w = p; w && shown; w = w->parent()) shown = w->visible();
        if (shown) return true;
    }
    return false;
}

bool HmiSimulationPane::debugKeyAnywhere(const ui::KeyDown& k, const ui::Widget* windowRoot) {
    if (k.repeat || k.mods.ctrl || k.mods.alt) return false;
    if (!((k.key == ui::Key::F5 && !k.mods.shift) || (k.key == ui::Key::F10 && !k.mods.shift) || k.key == ui::Key::F11)) return false;
    for (auto* p : simPanes()) {
        const ui::Widget* top = p;
        while (top->parent()) top = top->parent();
        if (windowRoot ? top != windowRoot : top->id() == "detached.root") continue;
        if (p->station_ || !p->debugPane_ || !p->runtime_.paused()) continue;
        if (p->debugPane_->debugKey(k.key, k.mods.shift)) return true;
    }
    return false;
}

void HmiSimulationPane::onPaintOverlay(const ui::PaintContext& ctx) {
    if (!fullScreen_) return;
    // La passe du dessus : toute la fenetre, la vue et sa barre flottante.
    ctx.r.fillRect(fullRect_, gfx::Color{0, 0, 0, 255});
    const ui::PaintContext full{ctx.r, ctx.theme, fullRect_, ctx.time, nullptr};
    split_->render(full);
    fsBar_->render(full);
}

ui::EventResult HmiSimulationPane::onEvent(const ui::InputEvent& ev) {
    // 1.10.3 : le clic droit sur la barre du haut - les commandes qui n'y sont plus
    // (kBarMenu). Le menu se cree au premier clic : le dernier enfant, devant tout.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev);
        d && d->button == ui::MouseButton::Right && !fullScreen_ && !station_ && bar_ && bar_->visible()
        && bar_->bounds().contains(d->pos)) {
        auto* menu = dynamic_cast<ui::PopupMenu*>(findById(id() + ".barMenu"));
        if (!menu) {
            menu = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".barMenu")));
            links_ += menu->itemChosen->connect([this](int a) {
                if (a >= 0 && static_cast<std::size_t>(a) < std::size(kBarMenu) && *kBarMenu[a].action) command(kBarMenu[a].action);
            });
        }
        std::vector<ui::PopupMenu::Item> items;
        for (std::size_t i = 0; i < std::size(kBarMenu); ++i) {
            const auto& m = kBarMenu[i];
            // 1.12.0 : IHM Studio - ni l'API (les deux, tout arreter), ni ses forcages.
            const std::string_view action(m.action);
            if (!core::hasApi() && (action == "both.start" || action == "all.stop" || action == "hmi.force" || action == "hmi.unforce")) continue;
            if (!core::hasApi() && !*m.label && i < 3) continue;     // le trait apres « Tout arreter »
            if (!*m.label) items.push_back({{}, {}, {}, ui::Icon::None, true, true, -1});
            else items.push_back({m.label, m.where, {}, ui::Icon::None, true, false, static_cast<int>(i)});
        }
        menu->setItems(std::move(items));
        menu->setBounds(bounds());   // comme le menu de l'inspecteur : sans place, il ne se dessine pas
        gfx::Size surface = ui::surfaceSize();
        if (surface.w <= 0.f || surface.h <= 0.f) surface = {bounds().right() + 400.f, bounds().bottom() + 400.f};
        menu->openAt(d->pos, surface);
        return ui::EventResult::Consumed;
    }
    // 1.10.3 : Ctrl+Alt+S ouvre la page Simulation, comme sur le poste (son bouton a quitte la barre).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev);
        k && k->key == ui::Key::S && k->mods.ctrl && k->mods.alt && !k->mods.shift && !k->repeat && !station_) {
        run("hmi.simpage");
        return ui::EventResult::Consumed;
    }
    // 1.13.0 (lot J2) : en pause dans un script, F5 (Continuer), F10 (Suivant), F11 (Entrer), Maj+F11 (Sortir).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && !k->repeat && !k->mods.ctrl && !k->mods.alt && !station_ && debugPane_
        && runtime_.paused() && (k->key == ui::Key::F5 || k->key == ui::Key::F10 || k->key == ui::Key::F11)
        && debugPane_->debugKey(k->key, k->mods.shift))
        return ui::EventResult::Consumed;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev);
        k && k->key == ui::Key::F11 && k->mods.none() && !k->repeat && !station_ && host_.fullScreenWindow) {
        setFullScreen(!fullScreen_);
        return ui::EventResult::Consumed;
    }
    // 1.10 (tranche 5) : F8 / Maj+F8 dans une fenetre detachee (dans la principale, l'ecran les
    // prend : MainAnalysisScreen::handleShortcut, la barre du haut suit).
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F8 && !k->mods.ctrl && !k->mods.alt && !k->repeat && !station_) {
        const ui::Widget* top = this;
        while (top->parent()) top = top->parent();
        if (top->id() == "detached.root") {
            if (k->mods.shift) stopHmi("Maj+F8");
            else startHmi("F8");
            return ui::EventResult::Consumed;
        }
    }
    // En plein ecran, rien ne passe sous la vue (l'arbre, les onglets, caches dessous).
    if (fullScreen_ && (std::holds_alternative<ui::MouseMove>(ev) || std::holds_alternative<ui::MouseDown>(ev)
                        || std::holds_alternative<ui::MouseUp>(ev) || std::holds_alternative<ui::MouseWheel>(ev)))
        return ui::EventResult::Consumed;
    return ui::EventResult::Ignored;
}

bool HmiSimulationPane::applyRecipe(const std::string& recipe, const std::string& record, std::string* why) {
    ensureStarted(now_);
    std::string reason;
    // Comme l'action "Charger une recette" : la permission Recettes d'abord.
    if (!runtime_.permitted("Recettes")) {
        reason = "permission Recettes refus\xC3\xA9" "e \xC3\xA0 " + (runtime_.userLogin().empty() ? std::string("personne") : runtime_.userLogin());
        if (why) *why = reason;
        status_->setTransientMessage("Recette " + recipe + " : " + reason, 8.0, ui::StatusBar::Severity::Warning);
        return false;
    }
    const bool ok = runtime_.applyRecipe(recipe, record, now_, &reason);
    if (why) *why = reason;
    status_->setTransientMessage(ok ? recipe + " / " + record + " appliqu\xC3\xA9 (toutes les valeurs \xC3\xA9" "crites)"
                                    : recipe + " / " + record + " refus\xC3\xA9 : " + reason + " (rien n'est \xC3\xA9" "crit)",
                                 8.0, ok ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Warning);
    refreshNow();
    return ok;
}

HmiSimulationPane::~HmiSimulationPane() {
    auto& panes = simPanes();
    panes.erase(std::remove(panes.begin(), panes.end(), this), panes.end());
    if (HmiDebugSession::instance().runtime() == &runtime_) HmiDebugSession::instance().setRuntime(nullptr);   // 1.13.0 (lot J2)
    // 1.9 finale : en quittant l'application pendant la marche, l'automate (simule
    // ou relie) et les liaisons des equipements peuvent etre deja detruits ;
    // Runtime::stop les quitte (unfollowAll) : l'IHM s'arrete sans eux.
    if (started_) {
        // 1.11.15 : quitter pendant la marche est un arret - les donnees gardees (l'option
        // et le fichier du demarrage : l'hote est peut-etre deja parti ; les variables
        // seulement, les esclaves simules aussi le sont peut-etre).
        if (keepAtStart_ && !dataFileAtStart_.empty()) {
            hmi::simdata::Snapshot snap;
            snap.date = hmi::simdata::nowStamp();
            snap.session = runtime_.session();
            snap.cells = runtime_.captureData();
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(dataFileAtStart_).parent_path(), ec);
            (void)core::writeFileAtomic(dataFileAtStart_, hmi::simdata::serialize(snap));
        }
        if (station_) finishRetained();   // 1.11.16 : quitter le poste en marche garde ses variables remanentes
        runtime_.bind(&doc_->project, nullptr);
        runtime_.setHooks({});
        runtime_.stop(now_);
        HmiSoundPlayer::instance().stopMix();           // 1.12.3 : ses sons se taisent avec elle
    }
}

void HmiSimulationPane::ensureStarted(double now) {
    sim::Runtime* rt = host_.runtime ? host_.runtime() : nullptr;
    // Lot 14 : relie a un automate reel (Modbus TCP), l'IHM lit la liaison.
    sim::Environment* link = host_.link ? host_.link() : nullptr;
    // 1.10 : L'IHM SEULE - pas d'automate simule prepare : sa memoire se prepare
    // (sans cycle, l'API reste arretee) ; l'IHM y lit et y ecrit.
    // 1.12.14 : IHM Studio n'a pas d'automate - l'IHM ne demande plus sa memoire (chaque demarrage
    // essayait de preparer le programme de MAST, et le disait).
    if (core::hasApi() && !started_ && !rt && !link && host_.transport) {
        host_.transport("sim.prepare");
        rt = host_.runtime ? host_.runtime() : nullptr;
    }
    runtime_.bind(&doc_->project, link ? link : static_cast<sim::Environment*>(rt));
    if (!started_) {
        // 1.11.13 : pas de demarrage sans build valide (l'hote le lance ; buildDone suit).
        if (!askBuild(gateSource_.empty() ? std::string("d\xC3\xA9marrage") : gateSource_)) return;
        gateOpen_ = false;
        blockedNote_.clear();
        started_ = true;
        // 1.9 : les choix de la page Simulation durent jusqu'au redemarrage de l'IHM.
        if (auto* eq = host_.equipments ? host_.equipments() : nullptr) eq->clearRuntimeChoices();
        // 1.9 : les DDT du programme (les membres que capture une copie de parametre).
        runtime_.setPlcTypes(hmiparams::plcTypesOf(hmiparams::program().get()));
        if (station_) loadRetained();                                        // 1.11.16 : le poste, ses variables remanentes
        else loadData();                                                     // 1.11.15 : la remanence de simulation
        runtime_.start(now);
        // 1.12.7 : une valeur calculee au demarrage en erreur arrete le demarrage - la barre le dit (la Console, ou).
        if (!runtime_.running() && !runtime_.startError().empty())
            status_->setTransientMessage("D\xC3\xA9marrage arr\xC3\xAAt\xC3\xA9 : " + runtime_.startError(), 12.0, ui::StatusBar::Severity::Error);
        modifiedStop_ = false;
        if (host_.lifecycle) host_.lifecycle(true, runtime_.session());     // 1.11.14 : les Sorties
        if (station_) reportRetained(); else reportRestore();
    }
}

bool HmiSimulationPane::playScenario(Id id, double pace) {
    const auto* sc = doc_->project.scenario(id);
    if (!sc) return false;
    stopScenario();
    // L'essai part au prochain rafraichissement : a l'heure de l'ecran (un onglet
    // qu'on vient d'ouvrir n'a pas encore la sienne).
    pendingScenario_ = std::make_pair(id, pace);
    hmi::ScenarioReport waiting;
    waiting.scenario = id;
    waiting.name = sc->name;
    waiting.steps.resize(sc->steps.size());
    doc_->reports[id] = std::move(waiting);
    doc_->reported->emit(id);
    return true;
}

void HmiSimulationPane::stopScenario() {
    pendingScenario_.reset();
    if (!scenario_) return;
    scenario_->stop(now_);
    doc_->reports[scenario_->scenario().id] = scenario_->report();
    doc_->reported->emit(scenario_->scenario().id);
    status_->setTransientMessage("Essai " + scenario_->scenario().name + " arr\xC3\xAAt\xC3\xA9 : " + scenario_->report().summary(), 8.0,
                                 ui::StatusBar::Severity::Warning);
    scenario_.reset();
    canvas_->setTouch(kNoId, 0.f);
}

// 1.11.15 : restart() garde son sens d'avant (le poste d'exploitation au lancement, les
// didacticiels) - relancer, les variables a leur valeur initiale, sans rien effacer ni
// demander ; le Redemarrer destructif (l'etat garde, les esclaves simules) est restartClean.
void HmiSimulationPane::restart() {
    if (started_) stopRuntime("red\xC3\xA9marrage");
    modifiedStop_ = false;
    skipRestore_ = true;                 // les valeurs initiales, pas l'etat garde
    layers_.reset();
    ensureStarted(now_);
    refreshPane();
}

void HmiSimulationPane::openSystemMenu(int tab, const std::string& source) {
    ensureStarted(now_);
    runtime_.openSystemMenu(tab, now_, source);
    refreshNow();
}

void HmiSimulationPane::goToView(Id view) {
    // 1.10 : l'IHM arretee - choisir une vue la demarre, et on le dit.
    if (!started_ && !autoStart_ && doc_->project.view(view)) startHmi("choix de la vue " + doc_->project.view(view)->name);
    ensureStarted(now_);
    if (!started_) return;   // 1.11.13 : le build d'abord
    if (!doc_->project.view(view)) return;
    (void)runtime_.navigate(view, hmi::Transition{}, now_);
    refreshPane();
}

void HmiSimulationPane::run(core::ActionId id) {
    const auto& views = doc_->project.views;
    auto rank = [&]() -> std::ptrdiff_t {
        for (std::size_t i = 0; i < views.size(); ++i)
            if (views[i].id == runtime_.currentView()) return static_cast<std::ptrdiff_t>(i);
        return -1;
    };
    // 1.10 (decision 12) : le plein ecran (le bouton, F11, Echap, la barre flottante).
    if (id == "hmi.fullscreen") {
        setFullScreen(!fullScreen_);
        return;
    }
    // 1.12.24 : sa mise a l'echelle (la barre flottante) : Remplir, Garder les proportions, Taille reelle.
    if (id == "hmi.fitFill" || id == "hmi.fitKeep" || id == "hmi.fitActual") {
        setScreenFit(id == "hmi.fitFill" ? hmi::ScreenFit::Fill : id == "hmi.fitKeep" ? hmi::ScreenFit::Keep : hmi::ScreenFit::Actual);
        return;
    }
    // 1.10 : l'IHM arretee - ce qui la suppose en marche le dit, et ne la demarre pas
    // en cachette (Demarrer l'IHM, ou choisir une vue dans la liste).
    if (!started_ && !autoStart_
        && (id == "hmi.home" || id == "hmi.prev" || id == "hmi.next" || id == "hmi.back" || id == "hmi.system" || id == "hmi.simpage"
            || id == "hmi.login" || id == "hmi.logout" || id == "hmi.ackall" || id == "hmi.perfreset")) {
        status_->setTransientMessage("L'IHM est arr\xC3\xAAt\xC3\xA9" "e : D\xC3\xA9marrer l'IHM d'abord", 4.0, ui::StatusBar::Severity::Warning);
        return;
    }
    if (id == "hmi.home") {
        const Id start = doc_->project.config.startView != kNoId ? doc_->project.config.startView
                         : views.empty() ? kNoId : views.front().id;
        goToView(start);
        return;
    }
    if (id == "hmi.prev" || id == "hmi.next") {
        if (views.empty()) return;
        const auto n = static_cast<std::ptrdiff_t>(views.size());
        auto next = rank() < 0 ? 0 : rank();
        // Les ecrans modeles, en-tetes et pieds (lot 6) ne se visitent pas : ils
        // tournent dans les vues qui les empruntent ; les popups (lot 8) non plus :
        // elles s'ouvrent par-dessus ; ni les symboles (lot 10) : ils se posent.
        for (std::ptrdiff_t k = 0; k < n; ++k) {
            next = (next + (id == "hmi.next" ? 1 : n - 1)) % n;
            const auto& role = views[static_cast<std::size_t>(next)].role;
            if (!hmi::isTemplateRole(role) && role != "popup" && role != "symbole") break;
        }
        goToView(views[static_cast<std::size_t>(next)].id);
        return;
    }
    if (id == "hmi.restart") {
        // 1.10 : redemarrer l'IHM la demarre si elle etait arretee ; l'API ne bouge pas.
        // 1.11.15 : destructif (les donnees de simulation, l'etat garde) : la question d'abord.
        const auto go = [this] {
            restartClean("bouton Red\xC3\xA9marrer l'IHM");
            status_->setTransientMessage(core::hasApi() ? "IHM red\xC3\xA9marr\xC3\xA9" "e : valeurs initiales (l'API " + plcStateText() + ")"
                                                        : std::string("IHM red\xC3\xA9marr\xC3\xA9" "e : valeurs initiales"), 4.0);   // 1.12.0
        };
        std::error_code ec;
        const std::string file = host_.dataFile ? host_.dataFile() : std::string{};
        const bool destructive = started_ || (!file.empty() && std::filesystem::exists(file, ec));
        if (host_.confirmRestart && destructive) host_.confirmRestart([go](bool yes) { if (yes) go(); });
        else go();
        return;
    }
    // ---- 1.11.15 : la suite d'un arret sur modification, Generer et redemarrer ----
    if (id == "hmi.buildRestart") { buildAndRestart("G\xC3\xA9n\xC3\xA9rer et red\xC3\xA9marrer"); return; }
    if (id == "hmi.rebuildRestart") {
        modifiedStop_ = false;
        if (host_.rebuild) host_.rebuild("regenerer");
        else buildAndRestart("R\xC3\xA9g\xC3\xA9n\xC3\xA9rer et red\xC3\xA9marrer");
        refreshPane();
        return;
    }
    if (id == "hmi.compile") {
        if (host_.rebuild) host_.rebuild("compiler");
        return;
    }
    if (id == "hmi.cancelRestart") {
        modifiedStop_ = false;
        output(0, "Red\xC3\xA9marrage annul\xC3\xA9 : la simulation reste arr\xC3\xAAt\xC3\xA9" "e (D\xC3\xA9marrer l'IHM la relance).");
        status_->setTransientMessage("Red\xC3\xA9marrage annul\xC3\xA9 : la simulation reste arr\xC3\xAAt\xC3\xA9" "e", 5.0);
        refreshPane();
        return;
    }
    if (id == "hmi.keepData") { setKeepData(!keepData()); return; }
    // ---- 1.10 : l'IHM et l'API, chacune ses commandes ----
    if (id == "hmi.start") { startHmi("bouton D\xC3\xA9marrer l'IHM"); return; }
    if (id == "hmi.stop") { stopHmi("bouton Arr\xC3\xAAter l'IHM"); return; }
    if (id == "hmi.toggle") {
        if (started_) stopHmi("bouton IHM");
        else startHmi("bouton IHM");
        return;
    }
    if (id == "plc.start" || id == "plc.stop" || id == "plc.toggle") {
        const bool running = plcTone() == "running" || plcTone() == "paused";
        const bool start = id == "plc.start" || (id == "plc.toggle" && !running);
        if (host_.transport) host_.transport(start ? "sim.run" : "sim.stop");
        status_->setTransientMessage(std::string(start ? "API d\xC3\xA9marr\xC3\xA9" "e" : "API arr\xC3\xAAt\xC3\xA9" "e (variables \xC3\xA0 leur valeur initiale)")
                                         + " \xE2\x80\x94 l'IHM " + (started_ ? "continue" : "reste arr\xC3\xAAt\xC3\xA9" "e"),
                                     5.0);
        refreshBar();
        return;
    }
    if (id == "both.start") {
        if (host_.transport && plcTone() != "running") host_.transport("sim.run");
        if (!started_) startHmi("bouton Les deux");
        status_->setTransientMessage("API et IHM en marche", 4.0, ui::StatusBar::Severity::Success);
        refreshBar();
        return;
    }
    if (id == "all.stop") {
        if (started_) stopHmi("bouton Tout arr\xC3\xAAter");
        if (host_.transport && plcTone() != "off" && plcTone() != "stopped") host_.transport("sim.stop");
        status_->setTransientMessage("IHM et API arr\xC3\xAAt\xC3\xA9" "es", 4.0);
        refreshBar();
        return;
    }
    if (id == "hmi.back") {
        if (previousView_ != kNoId && doc_->project.view(previousView_)) goToView(previousView_);
        else status_->setTransientMessage("Pas de vue d'avant", 3.0);
        return;
    }
    if (id == "hmi.zoomIn" || id == "hmi.zoomOut") {
        const auto b = canvas_->bounds();
        canvas_->zoomDisplayAround(id == "hmi.zoomIn" ? 1.25f : 0.8f, {b.x + b.w / 2.f, b.y + b.h / 2.f});
        return;
    }
    if (id == "hmi.zoomFit") { canvas_->setDisplayZoom(0.f); return; }
    if (id == "hmi.zoom100") { canvas_->setDisplayZoom(1.f); return; }
    // 1.9 : Parametres systeme, la page Simulation.
    if (id == "hmi.system") { openSystemMenu(0, "bouton Param\xC3\xA8tres syst\xC3\xA8me"); return; }
    if (id == "hmi.simpage") { openSystemMenu(hmi::kSimulationTab, "bouton Page Simulation"); return; }
    if (id == "hmi.login") {
        ensureStarted(now_);
        if (host_.login) host_.login(runtime_.userLogin());
        return;
    }
    if (id == "hmi.logout") {
        ensureStarted(now_);
        runtime_.logout(now_, "bouton D\xC3\xA9" "connecter");
        refreshNow();
        return;
    }
    if (id == "hmi.ackall") {
        ensureStarted(now_);
        std::string why;
        const auto n = runtime_.acknowledge("*", now_, &why);
        status_->setTransientMessage(n ? std::to_string(n) + " alarme(s) acquitt\xC3\xA9" "e(s)" : "Rien acquitt\xC3\xA9 : " + why, 6.0,
                                     n ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Warning);
        refreshNow();
        return;
    }
    if (id == "hmi.perfreset") { resetPerf(); return; }
    // Lot 7 : ou exporter (ExportTarget.hpp) - exports/ par defaut, le bouton ... ailleurs.
    if (id == "hmi.perfexport") {
        if (!askExportTarget("les mesures de la simulation (CSV)", "Fichiers CSV|*.csv", [this] { (void)exportPerf(); })) (void)exportPerf();
        return;
    }
    if (id == "hmi.force") { if (host_.force) host_.force({}); return; }
    if (id == "hmi.unforce") { if (host_.unforceAll) host_.unforceAll(); return; }
    if (host_.transport) host_.transport(id);
}

void HmiSimulationPane::refreshNow() { refreshPane(); }

// ======================================= 1.10 : l'IHM et l'API independantes ===
// 1.11.13 : vrai - on peut demarrer (pas de build demande par l'hote, ou il est valide).
bool HmiSimulationPane::askBuild(const std::string& source) {
    if (!host_.buildGate || gateOpen_) return true;
    if (!waitingBuild_) {
        waitingBuild_ = true;
        gateSource_ = source;
        status_->setTransientMessage("D\xC3\xA9marrage : build de l'IHM\xE2\x80\xA6 (seul ce qui a chang\xC3\xA9 est refait)", 4.0);
        host_.buildGate(source);   // l'hote rappelle buildDone, tout de suite ou a la fin du build
    }
    return false;
}

void HmiSimulationPane::buildDone(bool ok, const std::string& why) {
    if (!waitingBuild_) return;
    waitingBuild_ = false;
    if (!ok) {
        autoStart_ = false;   // arretee : elle le reste jusqu'a Demarrer
        blockedNote_ = "D\xC3\xA9marrage bloqu\xC3\xA9 : " + why + "\nCorrige (panneau du bas, Diagnostics : double-clic, la source), puis D\xC3\xA9marrer l'IHM.";
        runtime_.logAt(hmi::LogLevel::Error, "Simulation", "IHM", "D\xC3\xA9marrage bloqu\xC3\xA9 : " + why);   // 1.11.14 : une erreur de la Console
        status_->setTransientMessage("D\xC3\xA9marrage bloqu\xC3\xA9 : " + why, 10.0, ui::StatusBar::Severity::Error);
        gateSource_.clear();
        refreshPane();
        return;
    }
    gateOpen_ = true;
    const std::string source = gateSource_.empty() ? std::string("build valide") : gateSource_;
    gateSource_.clear();
    startHmi(source);
    gateOpen_ = false;
}

void HmiSimulationPane::startHmi(const std::string& source) {
    if (started_) return;
    if (host_.buildGate && !gateOpen_) {   // 1.11.13 : le build d'abord
        (void)askBuild(source);
        refreshBar();
        return;
    }
    const bool plcPrepared = host_.runtime && host_.runtime();
    ensureStarted(now_);
    if (!started_) return;
    runtime_.log("Simulation", "IHM", "IHM d\xC3\xA9marr\xC3\xA9" "e" + (source.empty() ? std::string{} : " (" + source + ")"));
    // Rien ne demarre l'autre en cachette : ce que l'IHM lit, dit tout de suite.
    const std::string plc = plcStateText();
    // 1.12.0 : IHM Studio - l'IHM seule, ses variables et ses esclaves simules.
    if (!core::hasApi()) {
        status_->setTransientMessage("IHM d\xC3\xA9marr\xC3\xA9" "e : ses variables, les esclaves simul\xC3\xA9s de ses \xC3\xA9quipements", 6.0,
                                     ui::StatusBar::Severity::Success);
        refreshPane();
        return;
    }
    status_->setTransientMessage(plcTone() == "running" ? "IHM d\xC3\xA9marr\xC3\xA9" "e \xE2\x80\x94 l'API tourne"
                                 : !plcPrepared && host_.runtime && host_.runtime()
                                     ? "IHM d\xC3\xA9marr\xC3\xA9" "e seule \xE2\x80\x94 la m\xC3\xA9moire de l'automate simul\xC3\xA9 est pr\xC3\xA9par\xC3\xA9" "e, "
                                       "sans cycle (l'API reste arr\xC3\xAAt\xC3\xA9" "e)"
                                     : "IHM d\xC3\xA9marr\xC3\xA9" "e seule \xE2\x80\x94 l'API " + plc + " : le programme ne tourne pas",
                                 6.0, ui::StatusBar::Severity::Success);
    refreshPane();
}

// ============================ 1.11.15 : le cycle de la simulation, la remanence ===
namespace {
std::string countText(long long n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }
} // namespace

std::string HmiSimulationPane::canvasNote() const { return canvas_ ? canvas_->stoppedNote() : std::string{}; }

void HmiSimulationPane::output(int severity, const std::string& text) {
    if (host_.outputs) host_.outputs(severity, text);
}

void HmiSimulationPane::setKeepData(bool on) {
    if (keepButton_ && keepButton_->checked() != on) {
        keepButton_->setChecked(on);    // le bouton rappelle setKeepData (son signal)
        return;
    }
    if (host_.setKeepData) host_.setKeepData(on);
    keepAtStart_ = on && started_;      // l'arret en quittant suit l'option du moment
    output(0, on ? std::string("R\xC3\xA9manence de simulation activ\xC3\xA9" "e : les donn\xC3\xA9" "es seront gard\xC3\xA9" "es \xC3\xA0 l'arr\xC3\xAAt et rendues au d\xC3\xA9marrage suivant.")
                 : std::string("R\xC3\xA9manence de simulation d\xC3\xA9sactiv\xC3\xA9" "e : le prochain d\xC3\xA9marrage repart des valeurs initiales "
                               "(l'\xC3\xA9tat gard\xC3\xA9 est ignor\xC3\xA9)."));
    status_->setTransientMessage(on ? "R\xC3\xA9manence de simulation : activ\xC3\xA9" "e" : "R\xC3\xA9manence de simulation : d\xC3\xA9sactiv\xC3\xA9" "e", 4.0);
}

bool HmiSimulationPane::saveData(const std::string& why) {
    if (!started_ || !keepData()) return false;
    const std::string file = host_.dataFile ? host_.dataFile() : std::string{};
    if (file.empty()) {
        output(2, "R\xC3\xA9manence de simulation : le projet n'est pas encore enregistr\xC3\xA9 \xE2\x80\x94 rien n'est gard\xC3\xA9.");
        return false;
    }
    hmi::simdata::Snapshot snap;
    snap.date = hmi::simdata::nowStamp();
    snap.session = runtime_.session();
    snap.cells = runtime_.captureData();
    if (auto* eq = host_.equipments ? host_.equipments() : nullptr)
        for (const auto& e : doc_->project.equipments)
            if (auto bank = eq->twinBank(e.name)) snap.twins.push_back(hmi::simdata::TwinMemory{e.id, e.name, bank->snapshot()});
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
    const auto st = core::writeFileAtomic(file, hmi::simdata::serialize(snap));
    if (!st) {
        const std::string reason = "R\xC3\xA9manence de simulation : sauvegarde impossible \xE2\x80\x94 " + st.error().message();
        output(3, reason);
        runtime_.logAt(hmi::LogLevel::Error, "Simulation", "IHM", reason);
        return false;
    }
    output(1, "Donn\xC3\xA9" "es de simulation sauvegard\xC3\xA9" "es (" + why + ") : " + countText(static_cast<long long>(snap.cells.size()), "valeur", "valeurs")
                  + (snap.twins.empty() ? std::string{} : ", " + countText(static_cast<long long>(snap.twins.size()), "esclave simul\xC3\xA9", "esclaves simul\xC3\xA9s"))
                  + " \xE2\x80\x94 " + std::filesystem::path(file).filename().string() + ".");
    return true;
}

void HmiSimulationPane::clearData() {
    const std::string file = host_.dataFile ? host_.dataFile() : std::string{};
    if (file.empty()) return;
    std::error_code ec;
    const bool had = std::filesystem::remove(file, ec);
    std::filesystem::remove(file + ".bak", ec);
    std::filesystem::remove(file + ".tmp", ec);
    if (had) output(1, "\xC3\x89tat r\xC3\xA9manent de simulation supprim\xC3\xA9.");
}

// Avant runtime_.start : l'instantane lu (sa copie de secours s'il est abime), ses variables
// donnees au moteur (rendues apres les valeurs initiales), les esclaves simules recharges.
void HmiSimulationPane::loadData() {
    restoredTwins_ = 0;
    loadedFrom_.clear();
    loadNote_.clear();
    keepAtStart_ = keepData();
    dataFileAtStart_ = host_.dataFile ? host_.dataFile() : std::string{};
    if (skipRestore_) {          // Redemarrer : un depart propre
        skipRestore_ = false;
        return;
    }
    if (!keepAtStart_ || dataFileAtStart_.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(dataFileAtStart_, ec)) {
        loadNote_ = "Aucune donn\xC3\xA9" "e de simulation gard\xC3\xA9" "e : les valeurs initiales.";
        return;
    }
    hmi::simdata::Snapshot snap;
    std::string text, why;
    if (!core::readFileAll(dataFileAtStart_, text) || !hmi::simdata::parse(text, snap, &why)) {
        std::string bak, why2;
        if (core::readFileAll(dataFileAtStart_ + ".bak", bak) && hmi::simdata::parse(bak, snap, &why2)) {
            output(2, "R\xC3\xA9manence de simulation : l'instantan\xC3\xA9 est illisible (" + (why.empty() ? std::string("illisible") : why)
                          + ") \xE2\x80\x94 sa copie de secours est reprise.");
        } else {
            output(2, "R\xC3\xA9manence de simulation : l'instantan\xC3\xA9 est illisible (" + (why.empty() ? std::string("illisible") : why)
                          + ") \xE2\x80\x94 les valeurs initiales.");
            runtime_.logAt(hmi::LogLevel::Warning, "Simulation", "IHM", "Instantan\xC3\xA9 de simulation illisible : " + why);
            return;
        }
    }
    runtime_.setStartData(snap.cells);
    if (auto* eq = host_.equipments ? host_.equipments() : nullptr)
        for (const auto& t : snap.twins)
            for (const auto& e : doc_->project.equipments)
                if (e.id == t.equipment)
                    if (auto bank = eq->twinBank(e.name)) {
                        bank->clear();
                        bank->load(t.memory);
                        ++restoredTwins_;
                    }
    loadedFrom_ = snap.date;
}

// ============================================================ 1.11.16 =====
//  LA REMANENCE D'EXPLOITATION. Au lancement du poste : le stockage lu (sa copie
//  .bak s'il est abime), les cases des variables encore remanentes donnees au
//  moteur (rendues apres les valeurs initiales, avant les scripts de Demarrage).
//  En marche : toutes les 0,25 s, les variables remanentes capturees ; une valeur
//  qui change prend sa date ; au plus une ecriture par seconde ; une ecriture qui
//  echoue est dite au journal et reessayee 5 s plus tard. A l'arret : tout de suite.
namespace {
// 1.12.3 : l'etat des structures du poste, du moteur au stockage et retour.
std::vector<hmi::retain::LayoutEntry> layoutOf(const std::vector<hmi::Runtime::UiState>& ui) {
    std::vector<hmi::retain::LayoutEntry> out;
    out.reserve(ui.size());
    for (const auto& s : ui) out.push_back({s.view, s.object, s.key, s.value});
    return out;
}
std::vector<hmi::Runtime::UiState> uiStateOf(const std::vector<hmi::retain::LayoutEntry>& layout) {
    std::vector<hmi::Runtime::UiState> out;
    out.reserve(layout.size());
    for (const auto& l : layout) out.push_back({l.view, l.object, l.key, l.value});
    return out;
}
} // namespace

// 1.12.3 : l'onglet choisi, les panneaux replies, le defilement - gardes avec les
// variables remanentes (option du poste, cochee) ; faux s'il n'y a rien de nouveau.
bool HmiSimulationPane::captureLayout() {
    std::vector<hmi::retain::LayoutEntry> now = doc_->project.station.keepLayout ? layoutOf(runtime_.uiState())
                                                                                   : std::vector<hmi::retain::LayoutEntry>{};
    if (now == retainStore_.layout) return false;
    retainStore_.layout = std::move(now);
    return true;
}

void HmiSimulationPane::loadRetained() {
    retainStore_ = {};
    retainNote_.clear();
    retainIgnored_ = 0;
    retainPending_ = false;
    retainLastCheck_ = retainLastWrite_ = retainRetryAt_ = retainLockAt_ = -1.0;
    skipRestore_ = false;   // restart() (le lancement du poste) : il rend quand meme ses variables remanentes
    retainLockHeld_ = false;
    retainLockOwner_.clear();
    retainFile_ = host_.retainFile ? host_.retainFile() : std::string{};
    if (retainFile_.empty()) { retainState_ = "pas de stockage (projet jamais enregistr\xC3\xA9)"; return; }
    // Un seul poste ecrit ce stockage : le verrou (rafraichi en marche, rendu a l'arret).
    const auto lock = hmi::retain::acquireLock(retainFile_);
    retainLockHeld_ = lock.held;
    retainLockError_ = !lock.held && !lock.error.empty();
    retainLockOwner_ = lock.held ? std::string{} : retainLockError_ ? lock.error : lock.owner;
    const std::string readOnly = lock.held          ? std::string{}
                               : retainLockError_   ? " ; non \xC3\xA9" "crites : dossier inaccessible (" + lock.error + ")"
                                                    : " ; non \xC3\xA9" "crites : un autre poste \xC3\xA9" "crit ce stockage (" + lock.owner + ")";
    std::error_code ec;
    if (!std::filesystem::exists(retainFile_, ec) && !std::filesystem::exists(retainFile_ + ".bak", ec)) {
        retainState_ = "aucune valeur gard\xC3\xA9" "e : les valeurs initiales" + readOnly;
        return;
    }
    std::string why;
    bool fromBackup = false;
    if (!hmi::retain::load(retainFile_, retainStore_, &why, &fromBackup)) {
        retainStore_ = {};
        retainNote_ = "Stockage des variables r\xC3\xA9manentes illisible (" + (why.empty() ? std::string("illisible") : why)
                      + ") \xE2\x80\x94 les valeurs initiales.";
        retainState_ = "illisible : les valeurs initiales";
        return;
    }
    if (fromBackup)
        retainNote_ = "Stockage des variables r\xC3\xA9manentes ab\xC3\xAEm\xC3\xA9 (" + (why.empty() ? std::string("illisible") : why)
                      + ") \xE2\x80\x94 sa copie de secours est reprise.";
    auto cells = hmi::retain::retainedCells(doc_->project, retainStore_, &retainIgnored_);
    runtime_.setStartData(std::move(cells), "Variables r\xC3\xA9manentes restaur\xC3\xA9" "es");
    if (doc_->project.station.keepLayout) runtime_.setStartUiState(uiStateOf(retainStore_.layout));   // 1.12.3
    retainState_ = std::to_string(retainStore_.entries.size()) + " valeur(s) gard\xC3\xA9" "e(s)"
                   + (retainStore_.date.empty() ? std::string{} : " (\xC3\xA9" "crites le " + retainStore_.date + ")") + readOnly;
}

// Apres runtime_.start (le journal repart a vide au demarrage) : ce que le retour a fait.
void HmiSimulationPane::reportRetained() {
    if (!retainNote_.empty()) runtime_.logAt(hmi::LogLevel::Warning, "R\xC3\xA9manence", "IHM", retainNote_);
    if (!retainFile_.empty() && !retainLockHeld_)
        runtime_.logAt(hmi::LogLevel::Warning, "R\xC3\xA9manence", "IHM",
                       retainLockError_
                           ? "Le dossier des variables r\xC3\xA9manentes est inaccessible (" + retainLockOwner_
                                 + ") : ce poste ne les \xC3\xA9" "crit pas (nouvel essai toutes les 30 s)."
                           : "Un autre poste d'exploitation \xC3\xA9" "crit d\xC3\xA9j\xC3\xA0 les variables r\xC3\xA9manentes de ce projet (" + retainLockOwner_
                                 + ") : ce poste les lit mais ne les \xC3\xA9" "crit pas tant que l'autre tourne.");
    if (retainIgnored_ > 0)
        runtime_.logAt(hmi::LogLevel::Info, "R\xC3\xA9manence", "IHM",
                       std::to_string(retainIgnored_) + " variable(s) gard\xC3\xA9" "e(s) ne sont plus r\xC3\xA9manentes : leur valeur est ignor\xC3\xA9" "e.");
}

void HmiSimulationPane::retainTick() {
    if (!station_ || !started_ || retainFile_.empty()) return;
    if (retainLastCheck_ >= 0.0 && now_ - retainLastCheck_ < 0.25) return;
    retainLastCheck_ = now_;
    // Le verrou : repris des que l'autre poste s'arrete (un essai toutes les 30 s) ;
    // le notre, rafraichi toutes les 60 s (un verrou de 3 minutes est perime).
    if (!retainLockHeld_) {
        if (retainLockAt_ >= 0.0 && now_ - retainLockAt_ < 30.0) return;
        retainLockAt_ = now_;
        const auto lock = hmi::retain::acquireLock(retainFile_);
        if (!lock.held) {
            retainLockError_ = !lock.error.empty();
            retainLockOwner_ = retainLockError_ ? lock.error : lock.owner;
            return;
        }
        retainLockHeld_ = true;
        retainLockError_ = false;
        retainLockOwner_.clear();
        retainState_ = std::to_string(retainStore_.entries.size()) + " valeur(s) gard\xC3\xA9" "e(s) ; le stockage est libre : ce poste y \xC3\xA9" "crit";
        runtime_.logAt(hmi::LogLevel::Info, "R\xC3\xA9manence", "IHM", "Le stockage des variables r\xC3\xA9manentes est libre : ce poste y \xC3\xA9" "crit d\xC3\xA9sormais.");
    } else if (retainLockAt_ < 0.0 || now_ - retainLockAt_ >= 60.0) {
        retainLockAt_ = now_;
        hmi::retain::refreshLock(retainFile_);
    }
    auto cells = runtime_.captureData([](const hmi::Variable& v) { return v.retain; });
    if (hmi::retain::merge(retainStore_, doc_->project, cells, hmi::simdata::nowStamp())) retainPending_ = true;
    if (captureLayout()) retainPending_ = true;                            // 1.12.3
    if (!retainPending_) return;
    if (retainRetryAt_ >= 0.0 && now_ < retainRetryAt_) return;            // apres un echec : 5 s
    if (retainLastWrite_ >= 0.0 && now_ - retainLastWrite_ < 1.0) return;  // au plus une ecriture par seconde
    (void)saveRetained(false);
}

bool HmiSimulationPane::saveRetained(bool force) {
    if (retainFile_.empty()) return true;
    if (!retainLockHeld_) return false;   // un autre poste ecrit ce stockage (retainState le dit)
    if (force && started_) {
        auto cells = runtime_.captureData([](const hmi::Variable& v) { return v.retain; });
        if (hmi::retain::merge(retainStore_, doc_->project, cells, hmi::simdata::nowStamp())) retainPending_ = true;
        if (captureLayout()) retainPending_ = true;                        // 1.12.3
    }
    if (!retainPending_) return true;
    retainStore_.project = doc_->project.config.name;
    retainStore_.date = hmi::simdata::nowStamp();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(retainFile_).parent_path(), ec);
    const auto st = hmi::retain::save(retainFile_, retainStore_);
    if (!st) {
        const std::string msg = st.error().message();
        retainRetryAt_ = now_ + 5.0;
        retainState_ = "\xC3\xA9" "criture en \xC3\xA9" "chec : " + msg;
        if (started_) runtime_.logAt(hmi::LogLevel::Error, "R\xC3\xA9manence", "IHM", "Variables r\xC3\xA9manentes non \xC3\xA9" "crites (" + msg + ") \xE2\x80\x94 nouvel essai dans 5 s.");
        return false;
    }
    retainPending_ = false;
    retainLastWrite_ = now_;
    retainRetryAt_ = -1.0;
    retainState_ = std::to_string(retainStore_.entries.size()) + " valeur(s) gard\xC3\xA9" "e(s), \xC3\xA9" "crites le " + retainStore_.date;
    return true;
}

void HmiSimulationPane::finishRetained() {
    (void)saveRetained(true);
    if (retainLockHeld_) hmi::retain::releaseLock(retainFile_);
    retainLockHeld_ = false;
}

void HmiSimulationPane::reapplyRetained() {
    if (!station_ || !started_ || retainStore_.entries.empty()) return;
    (void)runtime_.applyData(hmi::retain::retainedCells(doc_->project, retainStore_),
                             "Variables r\xC3\xA9manentes rendues apr\xC3\xA8s la reprise (plus r\xC3\xA9" "centes que l'\xC3\xA9tat de reprise)");
    refreshNow();
}

// Apres runtime_.start : ce que le retour a fait, dans les Sorties.
void HmiSimulationPane::reportRestore() {
    if (!keepAtStart_) return;
    if (const auto& r = runtime_.lastRestore()) {
        auto rep = *r;
        rep.twins = restoredTwins_;
        output(rep.incompatible > 0 ? 2 : 1, "Donn\xC3\xA9" "es de simulation restaur\xC3\xA9" "es" + (loadedFrom_.empty() ? std::string{} : " (gard\xC3\xA9" "es le " + loadedFrom_ + ")")
                                                 + " : " + rep.summary() + ".");
        for (const auto& w : rep.warnings) output(2, w);
    } else if (!loadNote_.empty()) {
        output(0, loadNote_);
    }
}

void HmiSimulationPane::stopRuntime(const std::string& why) {
    stopScenario();
    if (station_) finishRetained();                                          // 1.11.16 : avant l'arret
    runtime_.stop(now_, why);
    if (host_.lifecycle) host_.lifecycle(false, runtime_.session());
    started_ = false;
}

void HmiSimulationPane::restartClean(const std::string& source) {
    if (started_) stopRuntime(source);
    clearData();                                         // l'etat garde : efface
    if (auto* eq = host_.equipments ? host_.equipments() : nullptr)
        for (const auto& e : doc_->project.equipments) eq->restartTwin(e.name);   // les esclaves simules a leur depart
    modifiedStop_ = false;
    skipRestore_ = true;                                 // le demarrage qui suit ne rend rien
    autoStart_ = true;
    layers_.reset();
    output(0, "Red\xC3\xA9marrage (" + source + ") : les donn\xC3\xA9" "es de simulation repartent de leurs valeurs initiales.");
    ensureStarted(now_);                                 // le build d'abord (un projet a jour : tout de suite)
    refreshPane();
}

void HmiSimulationPane::buildAndRestart(const std::string& source) {
    modifiedStop_ = false;
    if (started_) {
        (void)saveData(source);                          // l'option cochee : les donnees repartent avec elle
        stopRuntime(source);
    }
    autoStart_ = true;
    layers_.reset();
    startHmi(source);                                    // le build de ce qui a change, puis la simulation
    refreshPane();
}

void HmiSimulationPane::stopForModification(const std::string& what) {
    if (!started_) return;
    const bool kept = saveData("arr\xC3\xAAt sur modification");
    runtime_.logAt(hmi::LogLevel::Warning, "Simulation", "IHM", "La simulation a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e car le projet IHM a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 : " + what);
    stopRuntime("le projet IHM a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9");
    autoStart_ = false;
    modifiedStop_ = true;
    modifiedNote_ = "La simulation a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e car le projet IHM a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9.\n" + what
                  + (kept ? "\nLes donn\xC3\xA9" "es de simulation sont gard\xC3\xA9" "es ; les journaux et les diagnostics restent." : "\nLes journaux et les diagnostics restent.");
    status_->setTransientMessage("La simulation a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e car le projet IHM a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9.", 8.0, ui::StatusBar::Severity::Warning);
    refreshPane();
}

void HmiSimulationPane::stopHmi(const std::string& source) {
    if (!started_) return;
    stopScenario();
    (void)saveData(source.empty() ? std::string("arr\xC3\xAAt") : source);   // 1.11.15 : l'option cochee
    if (station_) finishRetained();                                          // 1.11.16 : le poste
    runtime_.stop(now_, source);                                            // 1.11.14 : une seule ligne, avec la raison
    if (host_.lifecycle) host_.lifecycle(false, runtime_.session());       // 1.11.14 : les Sorties
    started_ = false;
    autoStart_ = false;   // arretee par l'utilisateur : elle le reste (l'onglet ne la relance pas)
    status_->setTransientMessage(core::hasApi() ? "IHM arr\xC3\xAAt\xC3\xA9" "e \xE2\x80\x94 l'API " + plcStateText() : std::string("IHM arr\xC3\xAAt\xC3\xA9" "e"), 5.0);
    refreshPane();
}

std::string HmiSimulationPane::plcTone() const {
    if (!host_.plc) return "off";
    const auto st = host_.plc();
    if (!st.attached) return "off";
    return st.running ? "running" : st.paused ? "paused" : st.halted ? "halted" : "stopped";
}

std::string HmiSimulationPane::plcStateText() const {
    if (host_.link && host_.link()) return "reli\xC3\xA9" "e (automate r\xC3\xA9" "el)";
    if (!host_.plc) return "absente";
    const auto st = host_.plc();
    if (!st.attached) return "arr\xC3\xAAt\xC3\xA9" "e";
    const std::string cycle = " \xC2\xB7 cycle " + std::to_string(st.scans);
    if (st.running) return "en marche" + cycle;
    if (st.paused) return "en pause" + cycle;
    if (st.halted) return "en d\xC3\xA9" "faut" + cycle;
    return "arr\xC3\xAAt\xC3\xA9" "e";
}

void HmiSimulationPane::refreshBar() {
    if (!bar_ || !hmiChip_) return;
    // La vue d'avant (Precedente) : la vue montree change.
    const Id current = started_ ? runtime_.currentView() : kNoId;
    if (current != kNoId && current != shownView_) {
        if (shownView_ != kNoId) previousView_ = shownView_;
        shownView_ = current;
    }
    const std::string tone = plcTone();
    const int zoom = canvas_ ? static_cast<int>(std::lround(canvas_->shownZoom() * 100.f)) : 100;
    std::string user;
    if (started_ && doc_->project.security.enabled)
        user = runtime_.userLogin().empty() ? std::string("Personne") : runtime_.userLogin();
    // La liste des vues : celles qu'on visite (ni modeles, ni popups, ni symboles).
    std::string viewsKey;
    for (const auto& v : doc_->project.views)
        if (!hmi::isTemplateRole(v.role) && v.role != "popup" && v.role != "symbole") viewsKey += v.name + '\n';
    const bool debugPaused = started_ && runtime_.paused();     // 1.13.0 (lot J2) : en pause dans un script
    const std::string key = std::to_string(started_) + (debugPaused ? "d" : "") + tone + plcStateText() + "|" + std::to_string(zoom) + (canvas_ && canvas_->displayZoom() > 0.f ? "z" : "f")
                            + "|" + user + "|" + std::to_string(current) + "|" + viewsKey;
    if (key == barState_) return;
    barState_ = key;
    static_cast<StateChip*>(hmiChip_)->set(debugPaused ? "IHM : en pause (d\xC3\xA9" "bogage)" : started_ ? "IHM : en marche" : "IHM : arr\xC3\xAAt\xC3\xA9" "e",
                                           debugPaused ? "debug" : started_ ? "running" : "stopped");
    static_cast<StateChip*>(plcChip_)->set("API : " + plcStateText(), tone == "off" ? "stopped" : tone);
    // La valeur seule (la barre reste courte) ; ajustee : le dit l'infobulle.
    static_cast<StateChip*>(zoomChip_)->set(std::to_string(zoom) + " %", {});
    zoomChip_->setTooltip(std::string(canvas_ && canvas_->displayZoom() <= 0.f ? "Ajust\xC3\xA9" "e : la vue enti\xC3\xA8re dans la place. " : "")
                          + "Le zoom de la vue en cours. Ctrl+molette : autour du curseur ; une vue plus grande que la place "
                            "se d\xC3\xA9place (fond tir\xC3\xA9, molette, Maj+molette)");
    hmiStartBtn_->setEnabled(!started_);
    hmiStopBtn_->setEnabled(started_);
    const bool plcRunning = tone == "running" || tone == "paused";
    plcBtn_->setText(plcRunning ? "Arr\xC3\xAAter l'API" : "D\xC3\xA9marrer l'API");
    plcBtn_->setIcon(plcRunning ? ui::Icon::Stop : ui::Icon::Play);
    plcBtn_->setTooltip(plcRunning ? "Arr\xC3\xAAte le programme et remet les variables de l'automate \xC3\xA0 leur valeur initiale ; l'IHM continue"
                                   : "Lance le programme de l'automate simul\xC3\xA9 ; l'IHM ne change pas d'\xC3\xA9tat");
    if (loginBtn_) loginBtn_->setText(user.empty() ? std::string("Utilisateur...") : user);
    // La liste des vues, la vue en cours choisie.
    std::vector<ui::DropDown::Item> items;
    viewIds_.clear();
    int selected = -1;
    const Id listed = current != kNoId ? current : shownView_;   // l'IHM arretee : la derniere vue montree
    for (const auto& v : doc_->project.views) {
        if (hmi::isTemplateRole(v.role) || v.role == "popup" || v.role == "symbole") continue;
        if (v.id == listed) selected = static_cast<int>(viewIds_.size());
        viewIds_.push_back(v.id);
        items.push_back({v.name, std::to_string(v.id)});
    }
    listing_ = true;
    if (viewList_->items().size() != items.size() || [&] {
            for (std::size_t i = 0; i < items.size(); ++i)
                if (viewList_->items()[i].label != items[i].label) return true;
            return false;
        }())
        viewList_->setItems(std::move(items));
    viewList_->setSelectedIndex(selected);
    listing_ = false;
    bar_->invalidateLayout();
}

std::string HmiSimulationPane::barText(std::string_view part) const {
    if (part == "ihm") return hmiChip_ ? static_cast<const StateChip*>(hmiChip_)->text() : std::string{};
    if (part == "api") return plcChip_ ? static_cast<const StateChip*>(plcChip_)->text() : std::string{};
    if (part == "zoom") return zoomChip_ ? static_cast<const StateChip*>(zoomChip_)->text() : std::string{};
    if (part == "vue") return viewList_ && viewList_->selectedItem() ? viewList_->selectedItem()->label : std::string{};
    if (part == "utilisateur") return loginBtn_ ? loginBtn_->text() : std::string{};
    if (part == "bouton-api") return plcBtn_ ? plcBtn_->text() : std::string{};
    return {};
}

gfx::Rect HmiSimulationPane::barPartRect(std::string_view part) const {
    const ui::Widget* w = part == "ihm" ? hmiChip_ : part == "api" ? plcChip_ : part == "zoom" ? zoomChip_
                        : part == "vue" ? static_cast<const ui::Widget*>(viewList_) : part == "utilisateur" ? static_cast<const ui::Widget*>(loginBtn_)
                        : part == "bouton-api" ? static_cast<const ui::Widget*>(plcBtn_) : part == "demarrer" ? static_cast<const ui::Widget*>(hmiStartBtn_)
                        : part == "arreter" ? static_cast<const ui::Widget*>(hmiStopBtn_) : nullptr;
    return w ? w->bounds() : gfx::Rect{};
}

std::vector<HmiLiveLayer> HmiLayerBuilder::build(hmi::Runtime& runtime_, const hmi::Project& project, double now_,
                                                 std::vector<hmi::LiveValue>* topValues, Id onlyView) {
    auto& env = runtime_.environment();
    const auto evaluate = [&](Id viewId, std::vector<hmi::LiveValue>* values, std::vector<Id>& errors) -> std::optional<hmi::View> {
        // La vue telle qu'elle tourne : son ecran modele dessous, son en-tete et
        // son pied (lot 6). Reliee quand elle change - la vue ou son modele,
        // modifies dans l'editeur pendant la marche.
        const auto* v = runtime_.composedView(viewId);
        if (!v) return std::nullopt;
        auto it = live_.find(viewId);
        // Lot 13 : la vue preparee (HmiDisplay) - la langue de l'operateur, les
        // unites et formats des variables, la taille des textes, les couleurs, les
        // symboles. Elle se relie quand la vue ou les reglages changent.
        const auto options = runtime_.displayOptions();
        if (!hmi::plainDisplay(project, options)) {
            auto& o = originals_[viewId];
            if (it == live_.end() || !(o.second == options) || !(o.first == *v)) {
                o = {*v, options};
                if (it == live_.end()) it = live_.emplace(viewId, hmi::LiveView{}).first;
                it->second.bind(hmi::displayView(*v, project, options));
            }
        } else {
            originals_.erase(viewId);
            if (it == live_.end()) {
                it = live_.emplace(viewId, hmi::LiveView{}).first;
                it->second.bind(*v);
            } else if (!(it->second.source() == *v)) {
                it->second.bind(*v);
            }
        }
        std::vector<hmi::LiveValue> local;
        // Lot 8 : les parametres de la vue (une popup ouverte pour Pompes[3]).
        auto shown = it->second.evaluate(env, values ? values : &local, runtime_.viewScope(viewId), now_ - runtime_.startedAt());
        for (const auto& lv : values ? *values : local) if (lv.error) errors.push_back(lv.object);
        return shown;
    };

    // Lot 13 : des traductions, des unites changees pendant la marche : tout se relie.
    if (!(languages_ == project.languages) || !(displays_ == project.displays)) {
        languages_ = project.languages;
        displays_ = project.displays;
        originals_.clear();
    }
    std::vector<HmiLiveLayer> layers;
    const auto& anim = runtime_.animation();
    const double p = runtime_.animationProgress(now_);
    const Id top = runtime_.topView();
    const auto layerOf = [&](Id viewId, hmi::Frame frame, bool popup, float veil) {
        std::vector<Id> errors;
        std::vector<hmi::LiveValue> localValues;
        std::vector<hmi::LiveValue>* vals = viewId == top && topValues ? topValues : &localValues;
        auto shown = evaluate(viewId, vals, errors);
        if (!shown) return;
        HmiLiveLayer l;
        l.view = std::move(*shown);
        // Lot 12 : les pages, les panneaux replies, le defilement.
        hmi::layoutStructures(l.view);
        l.errors = std::move(errors);
        l.frame = frame;
        l.popup = popup;
        l.veil = veil;
        // Lot 8 : une popup a sa place, avec sa barre de titre.
        if (popup) {
            const auto& slots = runtime_.popupSlots();
            const auto* pv = project.view(viewId);
            if (pv) {
                auto ps = hmi::popupSettingsOf(*pv);   // une vue ordinaire : sans barre de titre
                ps.title = hmi::translateText(project.languages, runtime_.language(), ps.title);   // lot 13
                l.titleBar = ps.titleBar;
                l.title = ps.title;
                // Un titre a trous : les parametres de la popup ("Armoire {Nom}").
                if (ps.title.find('{') != std::string::npos) {
                    auto t = titles_.find(ps.title);
                    if (t == titles_.end()) t = titles_.emplace(ps.title, hmi::TextTemplate::compile(ps.title)).first;
                    l.title = t->second.render(env, runtime_.viewScope(viewId));
                }
                l.closeButton = ps.titleBar && ps.closeButton;
                l.modal = ps.modal;
                l.movable = ps.titleBar && ps.movable;
                l.closeOutside = ps.closeOutside;
            }
            for (std::size_t k = 0; k < slots.size(); ++k)
                if (slots[k].view == viewId) {
                    l.slot = static_cast<int>(k);
                    l.px = slots[k].x;
                    l.py = slots[k].y;
                    lastPlaces_[viewId] = {slots[k].x, slots[k].y};
                }
            // Une popup qui se ferme : a sa derniere place.
            if (l.slot < 0)
                if (const auto it = lastPlaces_.find(viewId); it != lastPlaces_.end()) {
                    l.slot = -2;
                    l.px = it->second.first;
                    l.py = it->second.second;
                }
        }
        // La securite : ce que l'utilisateur connecte ne peut pas actionner.
        if (project.security.enabled && viewId == top)
            if (const auto* v = runtime_.composedView(viewId))
                for (const auto& o : v->objects) {
                    // 1.9 finale : un objet cache en ce moment (Visible a FAUX, ou dans
                    // une page, un panneau replie, une structure cachee) ne dessine rien,
                    // pas de cadenas non plus.
                    if (const hmi::Object* shownObj = l.view.object(o.id); shownObj && !shownObj->flag("visible", true)) continue;
                    // Lot 9 : une commande qui ecrit sa variable demande aussi Piloter.
                    const bool writes = hmi::kindWritesVariable(o.kind);
                    const bool acts = !o.actions.empty() || writes || o.kind == hmi::Kind::SystemButton    // lot 10 : le menu
                                   || o.kind == hmi::Kind::LoginMenuButton;                                      // lot 12
                    if (acts && !runtime_.objectAllowed(o)) l.locked.push_back(o.id);
                    else if (writes && !runtime_.permitted("Piloter")) l.locked.push_back(o.id);
                }
        if (viewId == top) locked_ = l.locked;
        // Lot 14 : relie a un automate reel, la qualite des valeurs de chaque objet ;
        // lot 15 : et pour les variables IHM liees a un equipement.
        if (runtime_.qualityRelevant()) {
            std::map<Id, std::uint8_t> worst;
            std::map<Id, std::string> tips;   // 1.11.1 (R1111-12) : la raison de la valeur la pire
            for (auto& lv : *vals) {
                auto pit = paths_.find(lv.expression);
                if (pit == paths_.end()) pit = paths_.emplace(lv.expression, hmi::comm::plcPaths(lv.expression)).first;
                for (const auto& path : pit->second) {
                    std::string why;
                    const auto q = runtime_.plcQuality(path, &why);
                    const std::uint8_t level = q == hmi::comm::Quality::Stale ? 1 : q == hmi::comm::Quality::Bad ? 2 : 0;
                    if (level > lv.quality) {
                        lv.quality = level;
                        lv.qualityWhy = path + " : " + why;
                    }
                    // 1.11.1 (decision 134, R1111-12) : une variable de l'automate que la
                    // liaison ne lit pas (sans adresse) n'est pas "non declaree" : elle
                    // l'est, dans l'automate. La case Valeur dit qu'elle n'a pas d'adresse
                    // et n'est lue qu'en simulation ; la raison et le remede suivent
                    // ("(mauvaise : ...)") et sont dans l'infobulle de la marque.
                    if (lv.error && q == hmi::comm::Quality::Bad && why.starts_with("sans adresse Modbus")) {
                        static const std::string kUndeclared = " n'est pas d\xC3\xA9" "clar\xC3\xA9" "e";
                        const std::string& m = lv.value;
                        const auto sameChar = [](char a, char b) {
                            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                        };
                        if (m.size() == path.size() + kUndeclared.size() && m.ends_with(kUndeclared) &&
                            std::equal(path.begin(), path.end(), m.begin(), sameChar))
                            lv.value = path + " n'a pas d'adresse : lue en simulation seulement";
                    }
                }
                auto& w = worst[lv.object];
                if (lv.quality > w) tips[lv.object] = lv.qualityWhy;
                w = std::max(w, lv.quality);
            }
            for (const auto& [qid, level] : worst)
                if (level) {
                    l.quality.emplace_back(qid, level);
                    if (const auto t = tips.find(qid); t != tips.end()) l.qualityTips.emplace_back(qid, t->second);
                }
        }
        // 1.9 : les lectures simulees - un objet dont une variable liee est lue en ce
        // moment sur l'esclave simule de son equipement (les reperes : le projet,
        // station.simMarks, et le bouton Reperes de la seance).
        if (simMarks && project.station.simMarks && !runtime_.simulatedReads().empty()) {
            std::set<Id> seen;
            for (const auto& lv : *vals) {
                if (seen.count(lv.object)) continue;
                auto pit = paths_.find(lv.expression);
                if (pit == paths_.end()) pit = paths_.emplace(lv.expression, hmi::comm::plcPaths(lv.expression)).first;
                for (const auto& path : pit->second) {
                    const std::string eqName = runtime_.slaveReadOf(path);
                    if (eqName.empty()) continue;
                    seen.insert(lv.object);
                    HmiLiveLayer::SimRead sr;
                    sr.object = lv.object;
                    sr.equipment = eqName;
                    sr.path = path;
                    const hmi::Object* shownObj = l.view.object(lv.object);
                    sr.tip = simReadTip(runtime_, project, path, eqName, shownObj ? shownObj->text("label") : std::string{});
                    l.simulated.push_back(std::move(sr));
                    break;
                }
            }
        }
        layers.push_back(std::move(l));
    };
    locked_.clear();
    if (onlyView != kNoId) {
        layerOf(onlyView, hmi::Frame{}, false, 0.f);
        return layers;
    }
    if (anim && !anim->popup) {
        layerOf(anim->from, hmi::outgoingFrame(anim->spec, p), false, 0.f);
        layerOf(anim->to, hmi::incomingFrame(anim->spec, p), false, 0.f);
    } else {
        layerOf(runtime_.currentView(), hmi::Frame{}, false, 0.f);
    }
    for (const Id popupId : runtime_.popups()) {
        hmi::Frame f;
        if (anim && anim->popup && !anim->closing && anim->to == popupId) f = hmi::incomingFrame(anim->spec, p);
        layerOf(popupId, f, true, static_cast<float>(0.45 * f.opacity));
    }
    // 1.10.2 : pas de fondu pour une popup deja rouverte (aussitot, sans transition) :
    // elle serait dessinee deux fois, la copie qui part par-dessus.
    if (anim && anim->popup && anim->closing
        && std::find(runtime_.popups().begin(), runtime_.popups().end(), anim->from) == runtime_.popups().end()) {
        const auto f = hmi::outgoingFrame(anim->spec, p);
        layerOf(anim->from, f, true, static_cast<float>(0.45 * f.opacity));
    }
    return layers;
}

// 1.10.1 (C1) : l'horloge du volet. Avant, now_ = max(now_, now) quelle que soit
// l'horloge : une heure d'une autre base (l'onglet cache avance par l'ecran a celle
// du journal, steady_clock : des jours ; le dessin a celle des images : des
// secondes) laissait now_ a la plus grande, et l'IHM, revenue a l'ecran, ne
// faisait plus un cycle - ni script, ni timer - jusqu'au prochain changement
// d'onglet. Maintenant chaque source a son decalage : nul sur la meme base (le
// max d'avant, exactement) ; ramene a l'heure du volet quand elle retarde de plus
// d'une seconde ou avance de plus d'une heure (une autre base).
void HmiSimulationPane::followClock(double now, ClockSource source) {
    double& offset = clockOffset_[source == ClockSource::Paint ? 0 : 1];
    const double mine = now + offset;
    if (clockSeen_ && (mine < now_ - 1.0 || mine > now_ + kClockLeap)) {
        offset = now_ - now;
        return;
    }
    clockSeen_ = true;
    now_ = std::max(now_, mine);
}

void HmiSimulationPane::refreshAt(double now) {
    followClock(now, ClockSource::Call);
    refreshPane();
}

// 1.13.0 (lot J2) : les points de la seance donnes au moteur (s'ils ont change), ce qu'il en dit rapporte (passages,
// lignes, notes, la pause), le sous-onglet Debogage tenu a jour.
void HmiSimulationPane::debugSync() {
    if (station_) return;
    auto& s = HmiDebugSession::instance();
    s.bindProject(doc_);
    if (s.runtime() != &runtime_) s.setRuntime(&runtime_);
    if (s.revision() != debugPushed_) {
        debugPushed_ = s.revision();
        runtime_.setBreakpoints(s.breakpoints());
    }
    if (runtime_.debugRevision() != debugSeen_) {
        debugSeen_ = runtime_.debugRevision();
        s.pullFrom(runtime_);
    }
    if (debugPane_) debugPane_->refresh(now_);
}

void HmiSimulationPane::refreshPane() {
    debugSync();                                   // 1.13.0 (lot J2) : le debogueur, a chaque image
    // Lot 13 : un essai demande - l'IHM redemarre, et il part maintenant.
    if (pendingScenario_) {
        const auto [id, pace] = *pendingScenario_;
        pendingScenario_.reset();
        if (const auto* sc = doc_->project.scenario(id)) {
            restart();                               // rappelle refreshPane : l'essai n'y est pas encore
            scenario_.emplace(*sc, runtime_, now_, pace, false);
            scenarioShown_ = static_cast<std::size_t>(-1);
            status_->setTransientMessage("Essai " + sc->name + " : " + std::to_string(sc->steps.size()) + " pas, rejou\xC3\xA9s \xC3\xA0 l'\xC3\xA9" "cran",
                                         4.0);
        }
    }
    // 1.10 : l'IHM arretee ne tourne pas - la derniere image reste, sous un voile ;
    // la barre et la ligne d'etat disent les deux etats.
    // 1.11.13 : le demarrage attend son build (la fenetre de progression le suit).
    if (!started_ && waitingBuild_) {
        refreshBar();
        canvas_->setStoppedNote("Build de l'IHM en cours\xE2\x80\xA6\nLa simulation d\xC3\xA9marre d\xC3\xA8s qu'il est valide.");
        status_->setMessage("Build en cours avant le d\xC3\xA9marrage\xE2\x80\xA6", ui::StatusBar::Severity::Info);
        return;
    }
    // 1.11.15 : arretee parce que le projet IHM a change - la carte et ses quatre boutons.
    if (!started_ && modifiedStop_) {
        refreshBar();
        canvas_->setStoppedNote(modifiedNote_, {{"G\xC3\xA9n\xC3\xA9rer et red\xC3\xA9marrer", "hmi.buildRestart", true},
                                                {"R\xC3\xA9g\xC3\xA9n\xC3\xA9rer et red\xC3\xA9marrer", "hmi.rebuildRestart", false},
                                                {"Compiler", "hmi.compile", false},
                                                {"Annuler le red\xC3\xA9marrage", "hmi.cancelRestart", false}});
        status_->setMessage("Arr\xC3\xAAt\xC3\xA9" "e : le projet IHM a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9" + (core::hasApi() ? " \xC2\xB7 API " + plcStateText() : std::string{}),
                            ui::StatusBar::Severity::Warning);
        return;
    }
    if (!started_ && !autoStart_ && !blockedNote_.empty()) {
        refreshBar();
        canvas_->setStoppedNote(blockedNote_);
        status_->setMessage("D\xC3\xA9marrage bloqu\xC3\xA9" + (core::hasApi() ? " \xC2\xB7 API " + plcStateText() : std::string{}), ui::StatusBar::Severity::Error);
        return;
    }
    if (!started_ && !autoStart_) {
        refreshBar();
        // (les mots de la maquette 1.10, scene 3)
        // L'etat de l'API tel qu'il est (en pause, en defaut, l'automate reel), pas seulement "arretee".
        const std::string tone = plcTone();
        const std::string plcLine = host_.link && host_.link() ? "L'API : reli\xC3\xA9" "e \xC3\xA0 l'automate r\xC3\xA9" "el.\n"
                                  : tone == "running"           ? "L'API tourne : ses variables bougent, la vue n'est pas anim\xC3\xA9" "e.\n"
                                  : tone == "paused"            ? "L'API est en pause.\n"
                                  : tone == "halted"            ? "L'API est en d\xC3\xA9" "faut (arr\xC3\xAAt\xC3\xA9" "e sur une erreur).\n"
                                                                : "L'API aussi est arr\xC3\xAAt\xC3\xA9" "e.\n";
        if (!core::hasApi()) {      // 1.12.0 : IHM Studio - pas d'automate
            canvas_->setStoppedNote("L'IHM est arr\xC3\xAAt\xC3\xA9" "e\nD\xC3\xA9marrer l'IHM (barre ci-dessus, ou F8).");
            status_->setMessage("IHM arr\xC3\xAAt\xC3\xA9" "e", ui::StatusBar::Severity::Info);
        } else {
        canvas_->setStoppedNote(std::string("L'IHM est arr\xC3\xAAt\xC3\xA9" "e\n") + plcLine + "D\xC3\xA9marrer l'IHM (barre ci-dessus), ou Les deux.");
        status_->setMessage("IHM arr\xC3\xAAt\xC3\xA9" "e  \xC2\xB7  API " + plcStateText(), ui::StatusBar::Severity::Info);
        }
        return;
    }
    if (!started_ && host_.buildGate && !gateOpen_) {   // 1.11.13 : l'onglet qui s'ouvre demarre par le build
        (void)askBuild("ouverture de l'onglet");
        canvas_->setStoppedNote("Build de l'IHM en cours\xE2\x80\xA6\nLa simulation d\xC3\xA9marre d\xC3\xA8s qu'il est valide.");
        refreshBar();
        return;
    }
    canvas_->setStoppedNote({});
    ensureStarted(now_);
    runtime_.tick(now_);
    retainTick();                                                            // 1.11.16 : le poste garde ses variables remanentes
    // Lot 14 : la communication au journal (liaison etablie, perdue, retrouvee...).
    if (host_.commEvents)
        for (const auto& e : host_.commEvents()) {
            runtime_.log("Communication", "Liaison", e);
            const bool bad = e.find("perdue") != std::string::npos || e.find("injoignable") != std::string::npos
                             || e.find("refus") != std::string::npos;
            status_->setTransientMessage(e, 6.0, bad ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
        }
    // Lot 14 : les notifications au journal (envoyee, en echec, pas envoyee).
    if (host_.notifyEvents)
        for (const auto& e : host_.notifyEvents()) runtime_.log("Notification", "Notifications", e);
    // Lot 13 : l'essai qui se joue - un pas quand c'est son heure ; le rapport
    // dans le Document a chaque pas ; le doigt sur l'objet touche.
    if (scenario_) {
        const bool finished = scenario_->advance(now_);
        const Id sid = scenario_->scenario().id;
        if (scenario_->current() != scenarioShown_ || finished) {
            scenarioShown_ = scenario_->current();
            doc_->reports[sid] = scenario_->report();
            doc_->reported->emit(sid);
        }
        const double since = now_ - scenario_->touchedAt();
        canvas_->setTouch(scenario_->touched(), scenario_->touchedAt() >= 0 && since < 0.9 ? static_cast<float>(1.0 - since / 0.9) : 0.f);
        if (finished) {
            const auto& rep = scenario_->report();
            status_->setTransientMessage("Essai " + scenario_->scenario().name + (rep.ok() ? " r\xC3\xA9ussi : " : " en \xC3\xA9" "chec : ") + rep.summary(),
                                         10.0, rep.ok() ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Warning);
            scenario_.reset();
        }
    }
    if (!bound_) { layers_.reset(); bound_ = true; }
    const auto& anim = runtime_.animation();
    const double p = runtime_.animationProgress(now_);
    const std::size_t before = values_.size();
    const auto evalStart = std::chrono::steady_clock::now();
    auto layers = layers_.build(runtime_, doc_->project, now_, &values_);
    // 1.12.18 : Ajouter aux expressions - les chemins ajoutes, sous « Ajoutees ».
    for (const auto& path : addedExprs_) {
        hmi::LiveValue lv;
        lv.objectName = "Ajout\xC3\xA9" "es";
        lv.key = path;
        lv.expression = path;
        if (const auto e = hmi::Expression::compile(path); e.valid()) {
            if (auto v = e.evaluate(runtime_.environment())) lv.value = hmi::formatValue(*v);
            else {
                lv.value = v.error().message();
                lv.error = true;
            }
        } else {
            lv.value = "illisible";
            lv.error = true;
        }
        values_.push_back(std::move(lv));
    }
    // 1.12.27 : Pourquoi ? - l'objet choisi, recalcule a chaque image quand l'onglet est montre.
    if (whyObject_ != kNoId && tabs_ && tabs_->currentIndex() == TabWhy) refreshWhy();
    // Lot 13 : l'evaluation des expressions, et le dessin d'avant (mesure par le canevas).
    const bool painted = canvas_->paintCount() != paintSeen_;
    paintSeen_ = canvas_->paintCount();
    runtime_.notePerf(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - evalStart).count(),
                      painted ? canvas_->lastPaintMs() : -1.0, values_.size(), canvas_->lastPaintObjects());
    // 1.10.2 (defaut du client, 11 h 53 : « la croix ne ferme pas la popup et tout
    // reste fige ») : seule une NAVIGATION (deux vues qui glissent) fait attendre les
    // clics. Avant, toute transition le faisait, celle d'une popup aussi : la popup
    // fermee (sa croix, Fermer la popup, Echap, Fermer toutes les popups) restait
    // dessinee en fondu tant que l'horloge du volet n'avait pas avance de sa duree, et
    // chaque clic etait avale (HmiLiveCanvas, MouseDown : « une transition : on
    // attend ») - la vue, la croix d'une autre popup, tout. Une popup qui s'ouvre ou se
    // ferme ne bloque plus rien : celle qui se ferme n'a plus de cadre de clic (place
    // -2), celle qui s'ouvre a le sien, a l'endroit ou elle est dessinee.
    if (layers.empty()) { values_.clear(); canvas_->showLayers({}, false); }
    else canvas_->showLayers(std::move(layers), !anim.has_value() || anim->popup);
    // 1.9 : les lectures simulees - le bandeau en haut de l'IHM, la barre d'etat.
    {
        const auto n = simRibbonText().empty() ? std::size_t{0} : runtime_.simulatedReads().size();
        canvas_->setSimRibbon(simRibbonText(), station_ && n ? "poste d'exploitation \xC2\xB7 " + std::to_string(n) + (n > 1 ? " \xC3\xA9quipements" : " \xC3\xA9quipement")
                                                             : std::string{});
    }
    if (simReads_) {
        auto* reads = static_cast<simmark::Indicator*>(simReads_);
        std::string text = simReadsText();
        // Une bascule (l'IHM lit l'esclave, ou de nouveau le vrai) : l'onglet Esclaves
        // simules refait ses sous-titres et sa ligne du bas.
        if (text != reads->text()) twinsDirty_ = true;
        reads->setText(std::move(text));
    }
    // 1.11.6 : l'arbre se refait quand la vue change (ses objets, ses expressions) ; les valeurs se relisent.
    if (auto* tree = static_cast<ExprTree*>(model_.get()); values_.size() != before || tree->stale()) {
        tree->rebuild(exprSearch_ ? exprSearch_->text() : std::string{});
        table_->setModel(model_);
    }
    table_->invalidate();
    updateTables();
    refreshBar();   // 1.10

    std::size_t bad = 0;
    // 1.11 (REP) : une expression a repere se calcule ; plus de "repere non remplace" a part.
    for (const auto& lv : values_) bad += lv.error;
    const auto* current = doc_->project.view(runtime_.currentView());
    std::string line = current ? current->name : std::string("(aucune vue)");
    for (const Id popupId : runtime_.popups())
        if (const auto* pv = doc_->project.view(popupId)) line += "  +  popup " + pv->name;
    line += "  \xC2\xB7  " + std::to_string(values_.size()) + " expression(s)";
    if (bad) line += ", " + std::to_string(bad) + " en erreur";
    if (anim) line += "  \xC2\xB7  transition " + std::string(hmi::transitionLabel(anim->spec.kind)) + " "
                    + std::to_string(static_cast<int>(std::lround(std::clamp(p, 0.0, 1.0) * 100))) + " %";
    if (doc_->project.security.enabled)
        line += "  \xC2\xB7  " + (runtime_.userLogin().empty() ? std::string("personne n'est connect\xC3\xA9")
                                                              : runtime_.userLogin() + " (niveau " + std::to_string(runtime_.level()) + ")");
    if (const auto open = runtime_.unacknowledged())
        line += "  \xC2\xB7  " + std::to_string(open) + " alarme(s) \xC3\xA0 acquitter";
    line += std::string(runtime_.paused() ? "  \xC2\xB7  IHM en pause (d\xC3\xA9" "bogage)" : "  \xC2\xB7  IHM en marche")   // 1.13.0 : un script arrete
            + (core::hasApi() ? "  \xC2\xB7  API " + plcStateText() : std::string{});   // 1.10 : les deux etats ; 1.12.0 : l'IHM seule
    // Lot 13 : l'essai en cours, son pas.
    if (scenario_ && scenario_->current() < scenario_->scenario().steps.size())
        line = "Essai " + scenario_->scenario().name + " : pas " + std::to_string(scenario_->current() + 1) + " / "
             + std::to_string(scenario_->scenario().steps.size()) + " \xE2\x80\x94 " + hmi::describeStep(scenario_->scenario().steps[scenario_->current()])
             + "  \xC2\xB7  " + line;
    status_->setMessage(line, bad || runtime_.highestPriority() == 1 ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Info);
}

void HmiSimulationPane::resetPerf() {
    ensureStarted(now_);
    runtime_.resetPerf();
    perfShownAt_ = -1;
    refreshNow();
    status_->setTransientMessage("Performances : le relev\xC3\xA9 recommence", 4.0);
}

bool HmiSimulationPane::exportPerf(std::string* where) {
    const auto& pf = runtime_.perf();
    auto t = hmi::perfTable(pf, now_ - pf.since, 50);
    t.subtitle = hmi::wallStamp().substr(0, 19) + " - " + doc_->project.config.name + " - " + hmi::perfMs((now_ - pf.since) * 1000.0)
                 + " de relev\xC3\xA9";
    hmi::ExportRequest rq;
    rq.fileName = "performances_" + hmi::wallStamp().substr(0, 10) + ".csv";
    rq.format = "CSV";
    rq.source = "performances";
    rq.rows = t.rows.size();
    const std::string csv = hmi::exportCsv(t);
    rq.data = std::make_shared<const hmi::Bytes>(csv.begin(), csv.end());
    rq.origin = "Simulation";
    std::string path;
    const bool ok = host_.exportFile && host_.exportFile(rq, &path);
    if (where) *where = path;
    status_->setTransientMessage(ok ? "Performances export\xC3\xA9" "es : " + path : "Export impossible" + (path.empty() ? std::string{} : " : " + path),
                                 8.0, ok ? ui::StatusBar::Severity::Success : ui::StatusBar::Severity::Warning);
    return ok;
}

std::size_t HmiSimulationPane::clearJournal() {
    // 1.10.1 : la liste du moteur (le tableau la relit) ; pas l'historique du document.
    const std::size_t n = runtime_.journal().size();
    runtime_.clearJournal();
    journalShown_ = static_cast<std::size_t>(-1);       // le tableau se refait, vide ; le badge s'efface
    updateTables();
    if (status_)
        status_->setTransientMessage("Journal vid\xC3\xA9 (" + std::to_string(n) + (n > 1 ? " lignes)" : " ligne)"), 6.0,
                                     ui::StatusBar::Severity::Success);
    return n;
}

namespace {
// 1.11.6 : la valeur d'un mouvement, dans le type de la variable.
sim::Value motionValue(double v, const sim::Value& like) {
    switch (like.type()) {
        case sim::Type::Bool: return sim::Value::boolean(v >= 0.5);
        case sim::Type::Real: return sim::Value::real(v);
        case sim::Type::String: return sim::Value::text(hmi::formatNumber(v));
        case sim::Type::Unknown: return sim::Value::real(v);
        default:
            if (like.type() == sim::Type::Time) return sim::Value::time(static_cast<std::int64_t>(std::llround(v)));
            return sim::Value::integer(like.type(), static_cast<std::int64_t>(std::llround(v)));
    }
}
} // namespace

// ---------------------------------------------------------- 1.11.7 : le forcage commun ---
namespace {
std::string upperKey(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
// La pente brut / valeur de la variable (sa mise a l'echelle ; sans : 1).
double rawPerEng(const hmi::twin::ValueRow& r) {
    if (!r.scaled || r.engMax == r.engMin) return 1.0;
    return (r.rawMax - r.rawMin) / (r.engMax - r.engMin);
}
std::string stepsConverted(const std::string& steps, const std::function<double(double)>& f) {
    std::string out;
    std::size_t start = 0;
    while (start <= steps.size()) {
        const auto semi = steps.find(';', start);
        std::string piece = steps.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        std::replace(piece.begin(), piece.end(), ',', '.');
        char* end = nullptr;
        const double d = std::strtod(piece.c_str(), &end);
        if (end && end != piece.c_str()) out += (out.empty() ? "" : "; ") + hmi::twin::numberText(f(d));
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return out;
}
// Un mouvement de l'onglet Variables IHM -> le comportement de l'esclave (en brut), et retour.
hmi::Behavior behaviorOf(const hmi::motion::Motion& m, const hmi::twin::ValueRow& r) {
    hmi::Behavior b;
    b.kind = m.kind;
    b.period = m.period;
    b.delay = m.delay;
    const auto raw = [&](double e) { return r.raw(e); };
    switch (m.kind) {
        case hmi::BehaviorKind::Constant: b.a = b.b = raw(m.a); break;
        case hmi::BehaviorKind::Counter: b.a = raw(m.a); b.b = m.b * rawPerEng(r); break;
        case hmi::BehaviorKind::Blink: b.a = 0; b.b = 1; break;
        case hmi::BehaviorKind::Steps: b.source = stepsConverted(m.steps, raw); break;
        default: b.a = raw(m.a); b.b = raw(m.b); break;
    }
    return b;
}
hmi::motion::Motion motionOf(const hmi::Behavior& b, const hmi::twin::ValueRow& r) {
    hmi::motion::Motion m;
    m.kind = b.kind;
    m.period = b.period;
    m.delay = b.delay;
    const auto eng = [&](double x) { return r.eng(x); };
    switch (b.kind) {
        case hmi::BehaviorKind::Counter: m.a = eng(b.a); m.b = b.b / rawPerEng(r); break;
        case hmi::BehaviorKind::Steps: m.steps = stepsConverted(b.source, eng); break;
        default: m.a = eng(b.a); m.b = eng(b.b); break;
    }
    return m;
}
} // namespace

void HmiSimulationPane::updateTwinPlaces() {
    const auto& p = doc_->project;
    std::string sig;
    for (const auto& v : p.programs.variables)
        if (!v.equipment.empty()) sig += v.name + '|' + v.type + '|' + v.equipment + '|' + v.address + ';';
    for (const auto& e : p.equipments) sig += e.name + (e.hasTwin() ? "+" : "-") + ';';
    for (const auto& t : p.programs.types) sig += t.name + '#' + std::to_string(t.members.size()) + ';';
    if (sig == twinPlacesSig_) return;
    twinPlacesSig_ = std::move(sig);
    twinPlaces_.clear();
    for (const auto& e : p.equipments) {
        if (!e.hasTwin()) continue;
        for (auto& r : hmi::twin::valueRows(p, e)) {
            if (r.variable.empty()) continue;
            std::string key = upperKey(r.variable);                             // avant le deplacement (la droite d'un = passe d'abord)
            twinPlaces_[std::move(key)] = TwinPlace{e.name, std::move(r)};
        }
    }
}

const HmiSimulationPane::TwinPlace* HmiSimulationPane::twinPlace(std::string_view path) const {
    const auto it = twinPlaces_.find(upperKey(path));
    return it == twinPlaces_.end() ? nullptr : &it->second;
}

bool HmiSimulationPane::twinForced(const TwinPlace& tp) const {
    const auto* e = doc_->project.equipmentByName(tp.equipment);
    if (!e) return false;
    return std::any_of(e->forcings.begin(), e->forcings.end(), [&](const hmi::Forcing& f) { return hmi::twin::sameCell(f.address, tp.row.address); });
}

const hmi::Behavior* HmiSimulationPane::twinBehavior(const TwinPlace& tp) const {
    const auto* e = doc_->project.equipmentByName(tp.equipment);
    if (!e) return nullptr;
    for (const auto& b : e->behaviors)
        if (b.enabled && hmi::twin::sameCell(b.address, tp.row.address)) return &b;
    return nullptr;
}

void HmiSimulationPane::applyMotions() {
    if (ihmMotions_.empty() && apiMotions_.empty()) return;
    const double t = std::max(0.0, now_ - runtime_.startedAt());
    for (auto it = ihmMotions_.begin(); it != ihmMotions_.end();) {
        const auto* cur = runtime_.variable(it->first);
        if (!cur) { it = ihmMotions_.erase(it); continue; }      // l'IHM s'est arretee : le mouvement aussi
        const double v = hmi::motion::valueAt(it->second.motion, t, it->second.state);
        (void)runtime_.forceVariable(it->first, motionValue(v, *cur));
        ++it;
    }
    auto* rt = host_.runtime ? host_.runtime() : nullptr;
    for (auto it = apiMotions_.begin(); it != apiMotions_.end();) {
        sim::Value cur;
        if (!rt || !rt->get(it->first, cur)) { it = apiMotions_.erase(it); continue; }
        const double v = hmi::motion::valueAt(it->second.motion, t, it->second.state);
        (void)rt->force(it->first, motionValue(v, cur));
        ++it;
    }
}

void HmiSimulationPane::expandExpressions(bool open) {
    if (auto* tree = static_cast<ExprTree*>(model_.get())) tree->setAllOpen(open);
    if (table_) table_->setModel(model_);
}

// ======================================================= 1.12.18 : les menus du clic droit ====
bool HmiSimulationPane::projectReadsRightButton() const {
    if (!doc_) return false;
    hmi::design::FindOptions o;
    o.wholeWord = true;
    return !hmi::design::find(doc_->project, "SYS.MouseRight", {}, o).empty()
        || !hmi::design::find(doc_->project, "SYS.MouseButtons", {}, o).empty();
}

void HmiSimulationPane::addExpression(const std::string& path) {
    if (path.empty() || std::find(addedExprs_.begin(), addedExprs_.end(), path) != addedExprs_.end()) return;
    addedExprs_.push_back(path);
    if (tabs_) tabs_->setCurrentIndex(TabValues);
    refreshNow();
    if (status_) status_->setTransientMessage(path + " ajout\xC3\xA9" "e aux Expressions (sous \xC2\xAB Ajout\xC3\xA9" "es \xC2\xBB).", 6.0,
                                              ui::StatusBar::Severity::Success);
}

void HmiSimulationPane::viewMenuChosen(int id) {
    if (id >= 0 && static_cast<std::size_t>(id) < viewMenuGo_.size()) runMenuKey(viewMenuGo_[static_cast<std::size_t>(id)]);
}

// Une entree d'un menu : "forcer:<chemin>", "relacher:<chemin>", "suivre:<chemin>", "expr:<chemin>",
// "copier:<texte>", "objet:<vue>:<objet>", "pause:<vue>:<objet>", "csv:<objet>", "acquitter:<alarme>",
// "@basculer:<ligne>", "@deplier-tout", "@replier-tout".
void HmiSimulationPane::runMenuKey(const std::string& key) {
    const auto arg = [&key](std::string_view prefix) { return key.rfind(prefix, 0) == 0 ? key.substr(prefix.size()) : std::string{}; };
    const auto say = [this](const std::string& t, bool error) {
        if (status_) status_->setTransientMessage(t, 6.0, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
    };
    // L'arbre qui connait ce chemin : les variables IHM, sinon celles de l'automate.
    const auto treeOf = [this](const std::string& path) -> std::pair<HmiSimVarTree*, std::size_t> {
        if (ihmVars_ && ihmVars_->valueText(path) != "-") return {ihmVars_, TabVariables};
        if (apiVars_ && apiVars_->valueText(path) != "-") return {apiVars_, TabApiVariables};
        return {ihmVars_, TabVariables};
    };
    const auto twoIds = [](const std::string& rest, Id& a, Id& b) {
        const auto colon = rest.find(':');
        if (colon == std::string::npos) return false;
        a = static_cast<Id>(std::strtoull(rest.c_str(), nullptr, 10));
        b = static_cast<Id>(std::strtoull(rest.c_str() + colon + 1, nullptr, 10));
        return true;
    };
    if (const auto p = arg("forcer:"); !p.empty()) {
        const auto [tree, tab] = treeOf(p);
        if (!tree) return;
        if (tabs_) tabs_->setCurrentIndex(tab);
        if (!tree->openValueEditor(p)) say(p + " : pas dans la liste des variables (la recherche, Sur la vue actuelle ?).", true);
    } else if (const auto released = arg("relacher:"); !released.empty()) {
        const auto [tree, tab] = treeOf(released);
        (void)tab;
        if (tree && tree->unforcePath(released)) say(released + " : libre (plus forc\xC3\xA9" "e).", false);
    } else if (const auto followed = arg("suivre:"); !followed.empty()) {
        std::string why;
        const bool hmiVar = ihmVars_ && ihmVars_->valueText(followed) != "-";
        if (host_.follow && host_.follow(hmiVar ? "ihm:" + followed : followed, &why)) say(followed + " suivie dans Simulation \xE2\x80\xBA Courbes.", false);
        else say(why.empty() ? followed + " : pas de courbe ici." : why, true);
    } else if (const auto expr = arg("expr:"); !expr.empty()) {
        addExpression(expr);
    } else if (key.rfind("copier:", 0) == 0) {
        ui::setClipboardText(key.substr(7));
        say("Copi\xC3\xA9 : " + key.substr(7), false);
    } else if (const auto why = arg("pourquoi:"); !why.empty()) {   // 1.12.27
        Id view = kNoId, obj = kNoId;
        if (twoIds(why, view, obj) && !explainObject(view, obj)) say("Pourquoi ? : l'objet n'est plus \xC3\xA0 l'\xC3\xA9" "cran.", true);
    } else if (key.rfind("copier-pourquoi", 0) == 0) {
        ui::setClipboardText(hmi::why::toText(whyReport_));
        say("Pourquoi ? : le rapport de " + whyReport_.name + " est copi\xC3\xA9.", false);
    } else if (const auto rest = arg("objet:"); !rest.empty()) {
        Id view = kNoId, obj = kNoId;
        if (twoIds(rest, view, obj) && host_.goToObject) host_.goToObject(view, obj);
    } else if (const auto pauseArg = arg("pause:"); !pauseArg.empty()) {
        Id view = kNoId, obj = kNoId;
        if (!twoIds(pauseArg, view, obj)) return;
        const bool paused = runtime_.trendPausedAt(view, obj).has_value();
        if (runtime_.setTrendPaused(view, obj, !paused)) say(paused ? "La courbe reprend." : "La courbe est en pause (la simulation continue).", false);
        refreshNow();
    } else if (const auto name = arg("csv:"); !name.empty()) {
        std::string why;
        if (runtime_.exportAsOperator("objet:" + name, name, "CSV", now_, true, &why)) say("Export de " + name + " demand\xC3\xA9.", false);
        else say("Export refus\xC3\xA9 : " + why, true);
    } else if (const auto alarm = arg("acquitter:"); !alarm.empty()) {
        std::string why;
        const auto n = runtime_.acknowledge(alarm, now_, &why);
        say(n ? alarm + " acquitt\xC3\xA9" "e." : "Acquittement refus\xC3\xA9 : " + why, n == 0);
        refreshNow();
    } else if (const auto r = arg("@basculer:"); !r.empty()) {
        if (auto* tree = static_cast<ExprTree*>(model_.get())) tree->toggle(static_cast<std::size_t>(std::strtoull(r.c_str(), nullptr, 10)));
        if (table_) table_->setModel(model_);
    } else if (key == "@deplier-tout" || key == "@replier-tout") {
        expandExpressions(key == "@deplier-tout");
    }
}

// ---- 1.12.27 : Pourquoi ? -------------------------------------------------------
bool HmiSimulationPane::explainObject(Id view, Id object) {
    const auto* v = runtime_.composedView(view);
    if (!v || !v->object(object)) return false;
    whyView_ = view;
    whyObject_ = object;
    // L'objet du projet : un enfant de symbole remonte a son instance (Aller a l'objet).
    whySource_ = object;
    for (int guard = 0; guard < 16 && (whySource_ & 0x80000000u) != 0; ++guard) {
        const auto* p = v->object(whySource_);
        if (!p || p->parent == kNoId) break;
        whySource_ = p->parent;
    }
    whyLive_ = hmi::LiveView{};
    whyGone_ = false;
    if (tabs_) tabs_->setCurrentIndex(TabWhy);
    refreshWhy();
    return true;
}

void HmiSimulationPane::refreshWhy() {
    auto* page = static_cast<WhyPage*>(whyPage_);
    auto* rows = static_cast<WhyRows*>(whyModel_.get());
    if (!page || !rows) return;
    if (whyObject_ == kNoId) {
        page->setHead("Clic droit sur un objet de la vue \xE2\x80\xBA Pourquoi ? : ce qu'il montre, ses formules, la valeur de chaque terme.", false);
        return;
    }
    const auto* v = runtime_.composedView(whyView_);
    const hmi::Object* source = v ? v->object(whyObject_) : nullptr;
    if (!source) {
        whyGone_ = true;
        page->setHead((whyReport_.name.empty() ? std::string("L'objet") : whyReport_.name)
                          + " n'est plus \xC3\xA0 l'\xC3\xA9" "cran (la vue a chang\xC3\xA9) : le dernier rapport reste ci-dessous.", true);
        return;
    }
    whyGone_ = false;
    if (!(whyLive_.source() == *v)) whyLive_.bind(*v);
    std::vector<hmi::LiveValue> values;
    auto& env = runtime_.environment();
    const auto* scope = runtime_.viewScope(whyView_);
    const auto shown = whyLive_.evaluate(env, &values, scope, now_ - runtime_.startedAt());
    const hmi::Object* o = shown.object(whyObject_);
    whyReport_ = hmi::why::explain(o ? *o : *source, values, env, scope);
    const auto forced = [this](const std::string& path) {
        return (ihmVars_ && ihmVars_->isForced(path)) || (apiVars_ && apiVars_->isForced(path));
    };
    std::vector<WhyRows::Line> lines;
    for (std::size_t i = 0; i < whyReport_.items.size(); ++i) {
        const auto& it = whyReport_.items[i];
        WhyRows::Line head;
        head.a = hmiPropertyLabel(source->kind, it.key);
        if (head.a.empty()) head.a = it.key;
        head.b = it.text ? it.formula : "=" + it.formula;
        head.c = it.value;
        head.error = it.error;
        head.item = i;
        head.first.bold = true;
        head.first.icon = it.text ? ui::Icon::Document : ui::Icon::Code;
        lines.push_back(std::move(head));
        if (!it.state.empty()) {
            WhyRows::Line st;
            st.a = "\xE2\x86\x92 \xC3\xA9tat retenu";
            st.b = it.state;
            st.item = i;
            st.state = true;
            st.first.indent = 18.f;
            st.first.fgTone = ui::Tone::Accent;
            lines.push_back(std::move(st));
        }
        for (std::size_t k = 0; k < it.terms.size(); ++k) {
            const auto& t = it.terms[k];
            WhyRows::Line tl;
            tl.a = t.name ? std::string("nom lu") : std::string("terme");
            tl.b = t.text;
            tl.c = t.value + (t.name && forced(t.text) ? std::string("  (forc\xC3\xA9" "e)") : std::string{});
            tl.error = t.error;
            tl.item = i;
            tl.term = static_cast<int>(k);
            tl.first.indent = 18.f;
            tl.first.fgTone = ui::Tone::Muted;
            lines.push_back(std::move(tl));
        }
    }
    const bool changed = lines.size() != rows->lines.size();
    rows->lines = std::move(lines);
    page->setHead(whyReport_.items.empty()
                      ? whyReport_.name + " (" + whyReport_.kind + ") : rien ne se calcule, ses propri\xC3\xA9t\xC3\xA9s sont fixes."
                      : "Pourquoi ? " + whyReport_.name + " (" + whyReport_.kind + ") : " + std::to_string(whyReport_.items.size())
                            + (whyReport_.items.size() > 1 ? " propri\xC3\xA9t\xC3\xA9s calcul\xC3\xA9" "es" : " propri\xC3\xA9t\xC3\xA9 calcul\xC3\xA9" "e")
                            + ", leurs termes maintenant",
                  false);
    if (changed) whyTable_->setModel(whyModel_);
    else whyTable_->invalidate();
}

std::vector<ui::PopupMenu::Item> HmiSimulationPane::whyMenuItems() {
    viewMenuGo_.clear();
    std::vector<ui::PopupMenu::Item> items;
    const auto add = [&](ui::PopupMenu::Item it, std::string go) {
        it.id = 1000 + static_cast<int>(viewMenuGo_.size());
        viewMenuGo_.push_back(std::move(go));
        items.push_back(std::move(it));
    };
    const auto* rows = static_cast<const WhyRows*>(whyModel_.get());
    const auto sel = whyTable_ ? whyTable_->selectedModelRows() : std::vector<ui::RowIndex>{};
    if (rows && sel.size() == 1 && sel.front() < rows->lines.size()) {
        const auto& l = rows->lines[sel.front()];
        if (l.item < whyReport_.items.size()) {
            const auto& it = whyReport_.items[l.item];
            const hmi::why::Term* t = l.term >= 0 && static_cast<std::size_t>(l.term) < it.terms.size() ? &it.terms[static_cast<std::size_t>(l.term)] : nullptr;
            const std::string what = t ? t->text : it.formula;
            const std::string value = t ? t->value : it.value;
            items.push_back(ui::cmd::heading(what.size() > 48 ? what.substr(0, 46) + "\xE2\x80\xA6" : what, value));
            const bool name = t && t->name;
            HmiSimVarTree* vars = nullptr;
            if (name && ihmVars_ && ihmVars_->valueText(t->text) != "-") vars = ihmVars_;
            else if (name && apiVars_ && apiVars_->valueText(t->text) != "-") vars = apiVars_;
            const std::string noName = name ? std::string("pas une variable de la simulation") : std::string("un calcul ne se force pas");
            add(ui::cmd::item(ui::cmd::Id::Forcer, 0, vars != nullptr, noName), "forcer:" + what);
            add(ui::cmd::item(ui::cmd::Id::Relacher, 0, vars && vars->isForced(what), "pas forc\xC3\xA9" "e"), "relacher:" + what);
            {
                auto follow = ui::cmd::item(ui::cmd::Id::SuivreCourbe, 0, name && static_cast<bool>(host_.follow),
                                            name ? std::string("pas de courbe ici") : std::string("un calcul ne se suit pas"));
                follow.note = "dans Simulation \xE2\x80\xBA Courbes";
                add(std::move(follow), "suivre:" + what);
            }
            ui::PopupMenu::Item expr;
            expr.label = "Ajouter aux expressions";
            add(std::move(expr), "expr:" + what);
            add(ui::cmd::item(ui::cmd::Id::CopierValeur, 0), "copier:" + value);
            items.push_back(ui::cmd::separator());
        }
    }
    ui::PopupMenu::Item all;
    all.label = "Copier le rapport";
    all.icon = ui::Icon::Copy;
    all.enabled = whyObject_ != kNoId;
    if (!all.enabled) all.disabledReason = "aucun objet choisi";
    add(std::move(all), "copier-pourquoi");
    ui::PopupMenu::Item go;
    go.label = "Aller \xC3\xA0 l'objet dans l'\xC3\xA9" "diteur";
    go.enabled = whyObject_ != kNoId && static_cast<bool>(host_.goToObject);
    if (!go.enabled) go.disabledReason = "pas ici";
    add(std::move(go), "objet:" + std::to_string(whyView_) + ":" + std::to_string(whySource_));
    return items;
}

bool HmiSimulationPane::openViewMenu(gfx::Point at) {
    Id view = kNoId, obj = kNoId;
    std::size_t layer = 0;
    if (!canvas_ || !canvas_->hitAt(at, view, obj, &layer) || layer >= canvas_->layers().size()) return false;
    const auto& lv = canvas_->layers()[layer].view;
    const hmi::Object* o = nullptr;
    for (const auto& x : lv.objects)
        if (x.id == obj) o = &x;
    if (!o) return false;
    // L'objet du projet : un enfant de symbole remonte a son instance.
    Id source = obj;
    for (int guard = 0; guard < 16 && (source & 0x80000000u) != 0; ++guard) {
        const hmi::Object* p = nullptr;
        for (const auto& x : lv.objects)
            if (x.id == source) p = &x;
        if (!p || p->parent == kNoId) break;
        source = p->parent;
    }
    const auto* projectView = doc_ ? doc_->project.view(view) : nullptr;
    const std::string viewName = projectView ? projectView->name : std::string{};
    viewMenuGo_.clear();
    std::vector<ui::PopupMenu::Item> items;
    const auto add = [&](ui::PopupMenu::Item it, std::string go) {
        it.id = static_cast<int>(viewMenuGo_.size());
        it.shortcut.clear();   // dans la vue en marche, les touches sont au projet
        viewMenuGo_.push_back(std::move(go));
        items.push_back(std::move(it));
    };
    const auto entry = [](std::string label, bool enabled = true, std::string why = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.enabled = enabled;
        if (!enabled) it.disabledReason = std::move(why);
        return it;
    };
    const std::string goObject = "objet:" + std::to_string(view) + ":" + std::to_string(source);
    // Une alarme de l'objet (celle d'un bandeau d'alarmes, ses alarmes d'objet), active ou a acquitter.
    // bannerAlarm ne vaut que pour un bandeau : sur un autre objet (sans groupe) il rendrait la plus grave du projet.
    const hmi::LiveAlarm* alarm = nullptr;
    {
        const auto& list = runtime_.alarms();
        if (o->kind == hmi::Kind::AlarmBanner)
            if (const int i = runtime_.bannerAlarm(view, *o); i >= 0 && static_cast<std::size_t>(i) < list.size()) alarm = &list[static_cast<std::size_t>(i)];
        const std::string group = viewName + "." + o->name;
        for (const auto& a : list)
            if (!alarm && a.objectGroup == group && (a.active || !a.acked)) alarm = &a;
    }
    if (station_) {
        // Le poste : Acquitter seulement, si l'utilisateur en a le droit.
        const bool allowed = !doc_ || !doc_->project.security.enabled || runtime_.permitted("Acquitter");
        if (!alarm || alarm->acked || !alarm->ackRequired || !allowed) return false;
        items.push_back(ui::cmd::heading(alarm->name, alarm->state()));
        add(entry("Acquitter"), "acquitter:" + alarm->name);
    } else {
        // L'alarme d'abord, puis ce que l'objet propose (une courbe garde Pause et Exporter).
        if (alarm) {
            items.push_back(ui::cmd::heading(alarm->name, alarm->state()));
            add(entry("Acquitter", !alarm->acked && alarm->ackRequired, alarm->acked ? "d\xC3\xA9j\xC3\xA0 acquitt\xC3\xA9" "e" : "sans acquittement"),
                "acquitter:" + alarm->name);
            add(entry("Copier le message", !alarm->message.empty(), "pas de message"), "copier:" + alarm->message);
            items.push_back(ui::cmd::separator());
        }
        if (o->kind == hmi::Kind::Trend) {
            const auto pens = hmi::roles::splitList(o->text("variables"));
            items.push_back(ui::cmd::heading(o->name, std::to_string(pens.size()) + (pens.size() > 1 ? " plumes" : " plume")));
            const bool paused = runtime_.trendPausedAt(view, obj).has_value();
            add(entry(paused ? "Continuer" : "Pause", o->flag("pilotable", false), "pas pilotable (Propri\xC3\xA9t\xC3\xA9s : Pilotable)"),
                "pause:" + std::to_string(view) + ":" + std::to_string(obj));
            add(entry("Exporter (CSV)\xE2\x80\xA6"), "csv:" + o->name);
            items.push_back(ui::cmd::separator());
            add(entry("Aller \xC3\xA0 l'objet dans l'\xC3\xA9" "diteur", static_cast<bool>(host_.goToObject), "pas ici"), goObject);
        } else {
            const std::string var = hmi::displayVariableOf(*o);
            if (!var.empty()) {
                const auto pick = [&]() -> HmiSimVarTree* {
                    if (ihmVars_ && ihmVars_->valueText(var) != "-") return ihmVars_;
                    if (apiVars_ && apiVars_->valueText(var) != "-") return apiVars_;
                    return nullptr;
                };
                auto* tree = pick();
                const std::string value = tree ? tree->valueText(var) : std::string{};
                items.push_back(ui::cmd::heading(o->name, var + (value.empty() ? std::string{} : " = " + value)));
                const std::string unknown = "pas une variable de la simulation";
                add(ui::cmd::item(ui::cmd::Id::Forcer, 0, tree != nullptr, unknown, "Forcer " + var + "\xE2\x80\xA6"), "forcer:" + var);
                add(ui::cmd::item(ui::cmd::Id::Relacher, 0, tree && tree->isForced(var), "pas forc\xC3\xA9" "e"), "relacher:" + var);
                {
                    auto follow = ui::cmd::item(ui::cmd::Id::SuivreCourbe, 0, static_cast<bool>(host_.follow), "pas de courbe ici");
                    follow.note = "dans Simulation \xE2\x80\xBA Courbes";
                    add(std::move(follow), "suivre:" + var);
                }
                add(entry("Ajouter aux expressions"), "expr:" + var);
                add(ui::cmd::item(ui::cmd::Id::CopierValeur, 0, !value.empty(), "pas de valeur"), "copier:" + value);
                items.push_back(ui::cmd::separator());
            } else {
                items.push_back(ui::cmd::heading(o->name, std::string(hmi::kindLabel(o->kind))));
            }
            add(entry("Aller \xC3\xA0 l'objet dans l'\xC3\xA9" "diteur", static_cast<bool>(host_.goToObject), "pas ici"), goObject);
        }
    }
    if (!station_) {   // 1.12.27 : Pourquoi ? - ce que montre l'objet, ses formules, la valeur de chaque terme
        auto why = entry("Pourquoi ?");
        why.icon = ui::Icon::Help;
        why.note = "dans l'onglet Pourquoi ?";
        add(std::move(why), "pourquoi:" + std::to_string(view) + ":" + std::to_string(obj));
    }
    if (!viewMenu_) {
        viewMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(id() + ".vueMenu")));
        links_ += viewMenu_->itemChosen->connect([this](int a) { viewMenuChosen(a); });
    }
    viewMenu_->setItems(std::move(items));
    viewMenu_->setBounds(bounds());
    // 1.12.31 : `at` est dans le repere de la vue en marche (un ilot dans la page zoomee) ;
    // le menu est dans celui de la page.
    viewMenu_->openAtSurface(canvas_->toSurface(at));
    return true;
}

std::vector<ui::PopupMenu::Item> HmiSimulationPane::exprMenuItems() {
    viewMenuGo_.clear();
    std::vector<ui::PopupMenu::Item> items;
    const auto add = [&](ui::PopupMenu::Item it, std::string go) {
        it.id = 1000 + static_cast<int>(viewMenuGo_.size());
        viewMenuGo_.push_back(std::move(go));
        items.push_back(std::move(it));
    };
    auto* tree = static_cast<ExprTree*>(model_.get());
    const auto sel = table_ ? table_->selectedModelRows() : std::vector<ui::RowIndex>{};
    if (tree && sel.size() == 1 && sel.front() < tree->rows().size()) {
        const auto r = static_cast<std::size_t>(sel.front());
        const auto& row = tree->rows()[r];
        const std::size_t vi = tree->valueAt(r);
        if (vi < values_.size()) {
            const auto& lv = values_[vi];
            const bool path = plainPath(lv.expression);
            HmiSimVarTree* vars = nullptr;
            if (path && ihmVars_ && ihmVars_->valueText(lv.expression) != "-") vars = ihmVars_;
            else if (path && apiVars_ && apiVars_->valueText(lv.expression) != "-") vars = apiVars_;
            items.push_back(ui::cmd::heading(lv.expression.size() > 48 ? lv.expression.substr(0, 46) + "\xE2\x80\xA6" : lv.expression, lv.value));
            const std::string noPath = path ? std::string("pas une variable de la simulation") : std::string("un calcul ne se force pas");
            add(ui::cmd::item(ui::cmd::Id::Forcer, 0, vars != nullptr, noPath), "forcer:" + lv.expression);
            add(ui::cmd::item(ui::cmd::Id::Relacher, 0, vars && vars->isForced(lv.expression), "pas forc\xC3\xA9" "e"), "relacher:" + lv.expression);
            {
                auto follow = ui::cmd::item(ui::cmd::Id::SuivreCourbe, 0, path && static_cast<bool>(host_.follow),
                                            path ? std::string("pas de courbe ici") : std::string("un calcul ne se suit pas"));
                follow.note = "dans Simulation \xE2\x80\xBA Courbes";
                add(std::move(follow), "suivre:" + lv.expression);
            }
            add(ui::cmd::item(ui::cmd::Id::CopierValeur, 0), "copier:" + lv.value);
            ui::PopupMenu::Item go;
            go.label = "Aller \xC3\xA0 l'objet";
            go.enabled = lv.object != kNoId && static_cast<bool>(host_.goToObject);
            if (!go.enabled) go.disabledReason = "pas d'objet";
            add(std::move(go), "objet:" + std::to_string(runtime_.currentView()) + ":" + std::to_string(lv.object));
        } else {
            items.push_back(ui::cmd::heading(row.label, std::to_string(row.count)));
            add(ui::cmd::item(row.open ? ui::cmd::Id::Replier : ui::cmd::Id::Deplier, 0), "@basculer:" + std::to_string(r));
        }
        items.push_back(ui::cmd::separator());
    }
    add(ui::cmd::item(ui::cmd::Id::ToutDeplier, 0), "@deplier-tout");
    add(ui::cmd::item(ui::cmd::Id::ToutReplier, 0), "@replier-tout");
    return items;
}

void HmiSimulationPane::updateViewFilter() {
    const auto& project = doc_->project;
    const Id current = started_ ? runtime_.currentView() : shownView_;
    std::vector<Id> ids;
    if (current != kNoId) ids.push_back(current);
    if (started_)
        for (const Id p : runtime_.popups()) ids.push_back(p);
    std::string sig;
    std::vector<const hmi::View*> views;
    for (const Id id : ids) {
        const hmi::View* v = started_ ? runtime_.composedView(id) : nullptr;
        if (!v) v = project.view(id);
        if (!v) continue;
        views.push_back(v);
        sig += std::to_string(id) + ':' + std::to_string(v->objects.size()) + ';';
    }
    if (sig == viewFilterSig_ && viewFilter_) return;
    viewFilterSig_ = sig;
    std::vector<std::string> paths;
    std::string name;
    for (const auto* v : views) {
        // Arretee, la vue du projet : ses symboles se developpent ici (en marche, la vue composee l'est deja).
        const hmi::View expanded = started_ ? *v : hmi::expandInstances(project, *v);
        for (auto& p : hmi::viewpaths::ofView(expanded, started_ ? runtime_.viewScope(v->id) : nullptr)) paths.push_back(std::move(p));
        name += (name.empty() ? "" : " + ") + v->name;
    }
    auto filter = std::make_shared<hmi::viewpaths::Filter>();
    filter->set(paths);
    viewFilter_ = filter;
    const auto covers = [filter](std::string_view path) { return filter->covers(path); };
    if (ihmVars_) ihmVars_->setViewFilter(covers, name);
    if (apiVars_) apiVars_->setViewFilter(covers, name);
    if (twinsCtl_) twinsCtl_->setViewFilter(covers);
}

void HmiSimulationPane::updateTables() {
    if (tabs_) hmiparams::updatePopupsTab(*tabs_, runtime_, doc_->project);   // 1.9 : l'onglet Popups
    updateTwinPlaces();                                                         // 1.11.7 : le forcage commun
    updateViewFilter();                                                         // 1.11.6
    applyMotions();                                                             // 1.11.6 : le forcage par type et bornes
    // Lot 13 : les performances, deux fois par seconde (de marche).
    if (perf_ && (perfShownAt_ < 0 || now_ - perfShownAt_ >= 0.5 || now_ < perfShownAt_)) {
        perfShownAt_ = now_;
        const auto& pf = runtime_.perf();
        auto t = hmi::perfTable(pf, now_ - pf.since);
        const bool late = pf.overruns > 0;
        perfModel_ = std::make_shared<TextRows>(t.headers, std::move(t.rows), [late](ui::RowIndex r, std::size_t c) {
            ui::CellStyle st;
            if (r == 2 && c == 1) { st.fgTone = late ? ui::Tone::Warning : ui::Tone::Ok; st.bold = late; }
            if (r >= 8 && c == 0) st.fgTone = ui::Tone::Accent;
            return st;
        });
        perf_->setModel(perfModel_);
        tabs_->setTabBadge(TabPerf, late ? std::to_string(pf.overruns) : std::string{}, ui::Tone::Warning);
    }
    const auto& j = runtime_.journal();
    if (journal_ && (j.size() != journalShown_ || (!j.empty() && journalModel_ && journalModel_->rowCount() && 
        journalModel_->cellText(journalModel_->rowCount() - 1, 3) != j.back().message))) {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> kinds;
        for (const auto& e : j) {
            rows.push_back({e.stamp, e.kind, e.source, e.message});
            kinds.push_back(e.kind);
        }
        journalModel_ = std::make_shared<TextRows>(std::vector<std::string>{"Heure", "Type", "Source", "Message"}, std::move(rows),
                                                   [kinds](ui::RowIndex r, std::size_t c) {
                                                       ui::CellStyle s;
                                                       if (c == 1 && r < kinds.size()) {
                                                           const auto& k = kinds[r];
                                                           s.fgTone = k == "Erreur" ? ui::Tone::Error : k == "Navigation" ? ui::Tone::Accent
                                                                    : k == "Journal" ? ui::Tone::Ok : ui::Tone::Muted;
                                                       }
                                                       return s;
                                                   });
        journal_->setModel(journalModel_);
        if (!j.empty()) journal_->selectModelRows({static_cast<ui::RowIndex>(j.size() - 1)}, false);   // la derniere en vue
        journalShown_ = j.size();
        std::size_t errors = 0;
        for (const auto& e : j) errors += e.kind == "Erreur";
        (void)errors;
    }
    // Les alarmes : refaites quand leur etat change.
    if (alarms_) {
        std::string sig;
        for (const auto& a : runtime_.alarms()) sig += a.name + (a.active ? "1" : "0") + (a.acked ? "1" : "0") + a.message + "|";
        if (!alarmsModel_ || sig != alarmsShown_) {
            alarmsShown_ = sig;
            std::vector<std::vector<std::string>> rows;
            std::vector<int> prio;
            std::vector<bool> open;
            for (const auto& a : runtime_.alarms()) {
                rows.push_back({a.appeared.size() >= 19 ? a.appeared.substr(11, 8) : a.appeared,
                                std::to_string(a.priority) + " - " + std::string(hmi::alarmPriorityLabel(a.priority)), a.name,
                                a.state(), a.message, a.group, a.ackedBy});
                prio.push_back(a.priority);
                open.push_back(!a.acked);
            }
            alarmsModel_ = std::make_shared<TextRows>(
                std::vector<std::string>{"Apparue", "Priorit\xC3\xA9", "Alarme", "\xC3\x89tat", "Message", "Groupe", "Par"}, std::move(rows),
                [prio, open](ui::RowIndex r, std::size_t c) {
                    ui::CellStyle st;
                    if (r >= prio.size()) return st;
                    const auto tone = prio[r] == 1 ? ui::Tone::Error : prio[r] == 2 ? ui::Tone::Warning : prio[r] == 3 ? ui::Tone::Accent : ui::Tone::Muted;
                    if (c == 1) st.fgTone = tone;
                    if (c == 2) { st.icon = ui::Icon::Warning; st.iconTone = tone; st.bold = open[r]; }
                    if (c == 3) { st.bold = open[r]; st.fgTone = open[r] ? ui::Tone::Error : ui::Tone::Ok; }
                    return st;
                });
            alarms_->setModel(alarmsModel_);
            const auto n = runtime_.unacknowledged();
            const int top = runtime_.highestPriority();
            (void)n;
            (void)top;
        }
    }
    // Les recettes : une ligne par jeu, double-clic pour l'appliquer.
    if (recipes_) {
        std::string sig;
        for (const auto& r : doc_->project.recipes)
            for (const auto& rec : r.records) {
                sig += r.name + "/" + rec.name + ":";
                for (const auto& v : rec.values) sig += v + ";";
            }
        if (!recipesModel_ || sig != recipesShown_) {
            recipesShown_ = sig;
            recipeRows_.clear();
            std::vector<std::vector<std::string>> rows;
            for (const auto& r : doc_->project.recipes)
                for (const auto& rec : r.records) {
                    std::string values;
                    for (std::size_t i = 0; i < r.fields.size(); ++i) {
                        const auto& f = r.fields[i];
                        values += (i ? ", " : "") + f.name + " = " + (i < rec.values.size() && !rec.values[i].empty() ? rec.values[i] : "-")
                                + (f.unit.empty() ? "" : " " + f.unit);
                    }
                    rows.push_back({r.name, rec.name, values});
                    recipeRows_.emplace_back(r.name, rec.name);
                }
            recipesModel_ = std::make_shared<TextRows>(std::vector<std::string>{"Recette", "Jeu", "Valeurs"}, std::move(rows),
                                                       [](ui::RowIndex, std::size_t c) {
                                                           ui::CellStyle st;
                                                           st.bold = c == 1;
                                                           return st;
                                                       });
            recipes_->setModel(recipesModel_);

        }
    }
    // 1.11.5 : les arbres des variables - refaits quand les variables changent (les valeurs,
    // elles, se lisent au dessin).
    if (ihmVars_) {
        std::string sig;
        for (const auto& var : doc_->project.programs.variables) sig += var.name + ':' + var.type + ';';
        for (const auto& t : doc_->project.programs.types) {
            sig += t.name + '#' + std::to_string(t.members.size()) + ';';
            for (const auto& m : t.members) sig += m.type + ',';     // 1.12.4 : un membre qui devient une liste
        }
        if (sig != ihmVarsSig_) {
            ihmVarsSig_ = sig;
            std::vector<HmiSimVarTree::Leaf> leaves;
            for (const auto& var : doc_->project.programs.variables) {
                if (!hmi::types::isComposite(var.type) || hmi::types::isRich(var.type)) {   // 1.12.2 : un objet, une ligne
                    leaves.push_back({var.name, var.type});
                    continue;
                }
                std::vector<hmi::types::ObjectMember> objects;
                for (const auto& l : hmi::types::leafVariables(doc_->project, var, nullptr, nullptr, nullptr, &objects))
                    leaves.push_back({l.name, l.type});
                for (const auto& o : objects) leaves.push_back({o.path, o.type});    // 1.12.4 : ses membres objets, une ligne chacun
            }
            ihmVars_->setLeaves(std::move(leaves));
            tabs_->setTabBadge(TabVariables, std::to_string(ihmVars_->leafCount()), ui::Tone::Accent);
        }
    }
    if (apiVars_) {
        auto* rt = host_.runtime ? host_.runtime() : nullptr;
        const std::size_t slots = rt ? rt->slotCount() : 0;
        if (rt != apiRuntime_ || slots != apiSlots_) {
            apiRuntime_ = rt;
            apiSlots_ = slots;
            std::vector<HmiSimVarTree::Leaf> leaves;
            if (rt) {
                for (const auto& n : rt->names()) {
                    sim::Value v;
                    leaves.push_back({n, rt->get(n, v) ? std::string(sim::toString(v.type())) : std::string{}});
                }
            }
            apiVars_->setLeaves(std::move(leaves));
            tabs_->setTabBadge(TabApiVariables, slots ? std::to_string(apiVars_->leafCount()) : std::string{}, ui::Tone::Accent);
        }
    }
}

void HmiSimulationPane::setStationMode(bool on) {
    if (on && fullScreen_) setFullScreen(false);
    station_ = on;
    // 1.13.0 (lot J2) : le poste d'exploitation ne s'arrete jamais sur un point d'arret (IHM_POINT_ARRET n'y fait rien).
    runtime_.setDebugger(!on);
    if (on && HmiDebugSession::instance().runtime() == &runtime_) HmiDebugSession::instance().setRuntime(nullptr);
    const auto shown = on ? ui::Visibility::Collapsed : ui::Visibility::Visible;
    bar_->setVisibility(shown);
    status_->setVisibility(shown);
    split_->collapsePane(1, on);
    canvas_->setStation(on);
    // 1.12.24 : le poste met la vue a l'echelle comme le dit son projet (Remplir par defaut).
    canvas_->setScreenFit(on ? doc_->project.station.screenFit : screenFit_);
    invalidateLayout();
    invalidate();
}

void HmiSimulationPane::onLayout() {
    const auto b = bounds();
    if (fullScreen_) {
        // 1.10 : toute la fenetre (la racine), la vue seule et la barre flottante.
        const ui::Widget* top = this;
        while (top->parent()) top = top->parent();
        fullRect_ = top->bounds();
        // Le partage garde la place de sa poignee (5 px) meme volet droit replie :
        // elle passe hors de la fenetre, la vue a toute la largeur.
        split_->setBounds({fullRect_.x, fullRect_.y, fullRect_.w + 5.f, fullRect_.h});
        fsBar_->setBounds(fullRect_);
        return;
    }
    fsBar_->setBounds({b.x, b.y, 0.f, 0.f});
    if (station_) {
        // 1.12.24 : comme en plein ecran, la poignee du partage (5 px) hors de l'ecran : la vue
        // a toute la largeur (Remplir va jusqu'au bord droit).
        split_->setBounds({b.x, b.y, b.w + 5.f, b.h});
        return;
    }
    const float barH = 38.f, statusH = 24.f;
    bar_->setBounds({b.x, b.y, b.w, barH});
    status_->setBounds({b.x, b.y + b.h - statusH, b.w, statusH});
    split_->setBounds({b.x, b.y + barH, b.w, std::max(0.f, b.h - barH - statusH)});
}

// Lot 18 : une zone de mouvement tiree, un forcage - une marque sur les courbes qui tracent la variable.
void HmiSimulationPane::noteTwinChanges() {
    namespace tw = hmi::twin;
    std::map<std::string, std::pair<std::vector<hmi::Behavior>, std::vector<hmi::Forcing>>> now;
    for (const auto& e : doc_->project.equipments)
        if (e.hasTwin()) now[e.name] = {e.behaviors, e.forcings};
    if (!twinSeenInit_) {
        twinSeen_ = std::move(now);
        twinSeenInit_ = true;
        return;
    }
    const auto band = [](hmi::BehaviorKind k) { return k == hmi::BehaviorKind::Sine || k == hmi::BehaviorKind::Ramp || k == hmi::BehaviorKind::Random; };
    for (const auto& e : doc_->project.equipments) {
        if (!e.hasTwin()) continue;
        const auto it = twinSeen_.find(e.name);
        if (it == twinSeen_.end() || (it->second.first == e.behaviors && it->second.second == e.forcings)) continue;
        const auto& oldB = it->second.first;
        const auto& oldF = it->second.second;
        for (const auto& r : tw::valueRows(doc_->project, e)) {
            if (r.variable.empty()) continue;
            const hmi::Behavior* nb = r.behavior >= 0 ? &e.behaviors[static_cast<std::size_t>(r.behavior)] : nullptr;
            const hmi::Behavior* ob = nullptr;
            for (const auto& b : oldB)
                if (tw::sameCell(b.address, r.address)) ob = &b;
            if (nb && (!ob || ob->a != nb->a || ob->b != nb->b || ob->enabled != nb->enabled || ob->kind != nb->kind)) {
                std::string text;
                if (!nb->enabled) text = "arr\xC3\xAAt\xC3\xA9" "e";
                else if (band(nb->kind)) {
                    text = "zone : ";
                    if (ob && ob->enabled && band(ob->kind)) text += tw::numberText(r.eng(ob->a)) + "-" + tw::numberText(r.eng(ob->b)) + " \xE2\x86\x92 ";
                    text += tw::numberText(r.eng(nb->a)) + "-" + tw::numberText(r.eng(nb->b));
                } else {
                    text = std::string(hmi::behaviorKindLabel(nb->kind));
                }
                runtime_.addTrendMarker(r.variable, text, false);
            }
            const hmi::Forcing* nf = r.forcing >= 0 ? &e.forcings[static_cast<std::size_t>(r.forcing)] : nullptr;
            const hmi::Forcing* of = nullptr;
            for (const auto& f : oldF)
                if (tw::sameCell(f.address, r.address)) of = &f;
            if (nf && (!of || of->value != nf->value)) runtime_.addTrendMarker(r.variable, "forc\xC3\xA9" "e : " + tw::numberText(r.eng(nf->value)), true);
            else if (!nf && of) runtime_.addTrendMarker(r.variable, "d\xC3\xA9" "forc\xC3\xA9" "e", true);
        }
    }
    twinSeen_ = std::move(now);
}

void HmiSimulationPane::onPaint(const ui::PaintContext& ctx) {
    if (fullScreen_) {
        // 1.10 : la fenetre a change de taille (elle passe en plein ecran) : suivre.
        const ui::Widget* top = this;
        while (top->parent()) top = top->parent();
        const auto r = top->bounds();
        if (r.x != fullRect_.x || r.y != fullRect_.y || r.w != fullRect_.w || r.h != fullRect_.h) {
            fullRect_ = r;
            split_->setBounds({r.x, r.y, r.w + 5.f, r.h});   // la poignee hors de la fenetre (onLayout)
            split_->layout();
            fsBar_->setBounds(r);
        }
    }
    ctx.r.fillRect(bounds(), station_ ? gfx::Color{0, 0, 0, 255} : ctx.theme.color.windowBg);
    if (station_) canvas_->setScreenFit(doc_->project.station.screenFit);   // 1.12.24 : le projet a pu changer
    // Chaque image : l'automate et l'IHM avancent entre deux dessins (1.10.1 : a
    // l'horloge du dessin, la source Paint de followClock).
    followClock(ctx.time, ClockSource::Paint);
    refreshPane();
    // Lot 18 : l'onglet Jumeaux - les lignes quand le projet change, le direct dix fois par seconde.
    if (twinsCtl_ && tabs_ && tabs_->currentIndex() == TabTwins && !station_) {
        if (twinsDirty_) {
            twinsDirty_ = false;
            twinsCtl_->refresh();
        }
        if (ctx.time - twinsTick_ >= 0.1) {
            twinsTick_ = ctx.time;
            twinsCtl_->tick();
        }
    }
    invalidate();
}

// ============================================== 1.9 : les lectures simulees ===
void HmiSimulationPane::setSimMarksShown(bool on) {
    if (marksButton_ && marksButton_->checked() != on) marksButton_->setChecked(on);
    if (layers_.simMarks == on) return;
    layers_.simMarks = on;
    if (status_)
        status_->setTransientMessage(on ? std::string("Rep\xC3\xA8res des lectures simul\xC3\xA9" "es : montr\xC3\xA9s")
                                        : std::string("Rep\xC3\xA8res des lectures simul\xC3\xA9" "es : masqu\xC3\xA9s pour cette s\xC3\xA9" "ance"),
                                     4.0);
    if (started_) refreshPane();
}

std::string HmiSimulationPane::simReadsText() const {
    if (!started_) return {};
    const auto reads = runtime_.simulatedReads();
    if (reads.empty()) return {};
    std::string names;
    for (std::size_t i = 0; i < reads.size(); ++i) {
        if (i) names += i + 1 == reads.size() ? " et " : ", ";
        names += reads[i].name;
    }
    return names + (reads.size() > 1 ? " lus en simul\xC3\xA9" : " lu en simul\xC3\xA9");
}

std::string HmiSimulationPane::simRibbonText() const {
    if (!started_ || !layers_.simMarks || !doc_->project.station.simMarks) return {};
    const auto reads = runtime_.simulatedReads();
    std::string text;
    for (const auto& st : reads) {
        if (!text.empty()) text += " \xC2\xB7 ";
        text += st.name;
        // Seulement simule : le projet le dit (EquipmentStatus::simulated : lu sur un serveur simule).
        const auto* eq = doc_->project.equipmentByName(st.name);
        const bool only = eq && eq->simulated;
        // Le poste (la maquette M4) : "Variateur ATV320 : son esclave simule (le vrai ne repond pas depuis 14:02:21)".
        if (station_) {
            text += " : son esclave simul\xC3\xA9";
            if (only) text += " (pas encore livr\xC3\xA9)";
            else if (st.fallback && st.realSilentSince > 0) text += " (le vrai ne r\xC3\xA9pond pas depuis " + wallClock(st.realSilentSince) + ")";
            else if (st.fallback) text += " (le vrai ne r\xC3\xA9pond pas)";
            else if (st.chosen) text += " (choisi par un administrateur)";
            continue;
        }
        if (only) text += " (seulement simul\xC3\xA9)";
        else if (st.fallback)
            text += st.realSilentSince > 0 ? " (le vrai ne r\xC3\xA9pond pas depuis " + wallClock(st.realSilentSince) + ")" : std::string(" (le vrai ne r\xC3\xA9pond pas)");
        else if (st.chosen) text += " (choisi sur la page Simulation)";
        else text += " (l'esclave simul\xC3\xA9)";
    }
    return text;
}

} // namespace app
