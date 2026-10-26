// =============================================================================
//  app/hmi/HmiUnitPanes.cpp - 1.13.0 (lot J6b) : l'interface des unites de mesure
//  (voir HmiUnitPanes.hpp)
// =============================================================================
#include "HmiUnitPanes.hpp"
#include "../../hmi/HmiDisplay.hpp"   // hmi::unitLabel (l'unite comme la simulation la montre)
#include "../../hmi/HmiOperators.hpp" // 1.13.0 (seconde passe) : le nom d'un operateur (ses emplois d'unite)
#include "HmiPaneKit.hpp"
#include "../../core/FieldCodec.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app {

namespace {

using hmikit::prop;
using PG = ui::PropertyGrid;
namespace hu = hmi::units;

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Minuscules, sans accents ni espaces (la recherche du selecteur, l'aide a la saisie) : comme le
// catalogue (hmi::units) compare ses symboles.
std::string fold(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC2 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            if (d == 0xB2) out += '2';
            else if (d == 0xB3) out += '3';
            i += 2;
            continue;
        }
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            static const char* const base = "aaaaaaaceeeeiiiidnooooo.ouuuuy.y";
            out += base[(d >= 0xA0 ? d - 0xA0 : d - 0x80) & 0x1F];
            i += 2;
            continue;
        }
        if (!std::isspace(c)) out += static_cast<char>(std::tolower(c));
        ++i;
    }
    return out;
}

std::string number(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.10g", v);
    return b;
}

// 1.13.0 (lot F3) : un nombre a lire (une formule) - 6 chiffres significatifs au plus, sans exposant ni zeros inutiles
// (0.5555555556 -> 0.555556, 1e-06 -> 0.000001). La fiche garde number() : la valeur exacte, a modifier.
std::string shortNumber(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.6g", v);
    const double r = std::strtod(b, nullptr);
    const double a = std::fabs(r);
    if (a == 0.0) return "0";
    if (a < 1e-9 || a >= 1e12) return b;
    const int decimals = std::max(0, 5 - static_cast<int>(std::floor(std::log10(a))));
    std::snprintf(b, sizeof b, "%.*f", decimals, r);
    std::string s(b);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

// "1 mbar = 0.001 bar" ; une affine : "x degF = x \xC3\x97 0.555556 + 255.372 K", un decalage seul : "x degC = x + 273.15 K".
std::string conversionText(const hu::Catalog& cat, const hmi::UnitDef& u) {
    const auto* f = cat.family(u.family);
    const std::string ref = f ? f->reference : std::string{};
    if (ref.empty() || hu::Catalog().relation(u.symbol, ref) == hu::Rel::Same || fold(u.symbol) == fold(ref))
        return "r\xC3\xA9" "f\xC3\xA9rence de la famille";
    if (u.offset == 0.0) return "1 " + u.symbol + " = " + shortNumber(u.factor) + " " + ref;
    const std::string scaled = shortNumber(u.factor) == "1" ? std::string("x") : "x \xC3\x97 " + shortNumber(u.factor);
    return "x " + u.symbol + " = " + scaled + (u.offset >= 0 ? " + " : " - ") + shortNumber(std::fabs(u.offset)) + " " + ref;
}

bool parseDouble(std::string_view text, double& out) {
    std::string t = trim(text);
    for (auto& c : t)
        if (c == ',') c = '.';
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

bool familyNameOk(std::string_view n) {
    if (n.empty() || std::isdigit(static_cast<unsigned char>(n.front()))) return false;
    return std::all_of(n.begin(), n.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool symbolOk(std::string_view s) {
    if (s.empty() || s.size() > 16) return false;
    for (const char c : s)
        if (std::isspace(static_cast<unsigned char>(c)) || c == '[' || c == ']' || c == '\'' || c == '"' || c == ';') return false;
    return true;
}

} // namespace

// ============================================================ la case Unite ===
namespace unitui {

bool usable(std::string_view type) { return hu::typeTakesUnit(type); }

std::string problem(const hmi::Project& p, std::string_view type, std::string_view text, std::string* canonical) {
    if (canonical) canonical->clear();
    const std::string t = trim(text);
    if (t.empty()) return {};
    if (!hu::typeTakesUnit(type))
        return "une unit\xC3\xA9 ne va que sur un nombre (ici " + (trim(type).empty() ? std::string("sans type") : trim(type)) + ")";
    const hu::Catalog cat(p.config);
    const std::string sym = cat.canonical(t);
    if (sym.empty())
        return "unit\xC3\xA9 inconnue \xC2\xAB " + t + " \xC2\xBB : choisis-la dans la liste (Ctrl+Espace) ou ajoute-la dans "
               "Configuration \xE2\x80\xBA Unit\xC3\xA9s de mesure";
    if (canonical) *canonical = sym;
    return {};
}

std::string displayForm(std::string_view symbol) { return hmi::unitLabel(symbol); }   // la meme que la simulation

std::string describe(const hmi::Project& p, std::string_view symbol) {
    const hu::Catalog cat(p.config);
    const auto* u = cat.unit(symbol);
    if (!u) return trim(symbol).empty() ? std::string("sans unit\xC3\xA9") : "unit\xC3\xA9 inconnue \xC2\xAB " + trim(symbol) + " \xC2\xBB";
    return u->symbol + " \xE2\x80\x94 " + cat.familyLabel(u->family) + " (" + (u->label.empty() ? u->symbol : u->label) + ", "
         + conversionText(cat, *u) + ")";
}

ui::InputText::Assist fieldAssist(std::function<const hmi::Project*()> project, std::string field, std::string type,
                                  std::function<void(const std::string&)> chosen) {
    return [project = std::move(project), field = std::move(field), type = std::move(type), chosen = std::move(chosen)](
               std::string_view before, std::size_t& from, std::vector<ui::InputText::Suggestion>& out) {
        from = 0;                                         // la case entiere : une unite, pas un chemin
        const hmi::Project* p = project ? project() : nullptr;
        const hu::Catalog cat = p ? hu::Catalog(p->config) : hu::Catalog();
        const std::string q = fold(trim(before));
        for (const auto& u : cat.units()) {
            const std::string fam = cat.familyLabel(u.family);
            if (!q.empty() && fold(u.symbol).find(q) == std::string::npos && fold(u.label).find(q) == std::string::npos
                && fold(fam).find(q) == std::string::npos)
                continue;
            ui::InputText::Suggestion s;
            s.text = u.symbol;
            s.detail = fam + " \xC2\xB7 " + (u.label.empty() ? u.symbol : u.label);
            s.icon = ui::Icon::Constant;
            out.push_back(std::move(s));
        }
        if (unitpicker::available()) {
            ui::InputText::Suggestion a;
            a.text = unitpicker::kChoose;
            a.detail = "par famille, avec la recherche";
            a.action = [field, type, chosen](std::string_view typed, ui::ChooseDone done) {
                HmiUnitPicker::Spec spec;
                spec.field = field;
                spec.current = trim(typed);
                spec.type = type;
                spec.recents = unitpicker::recents();
                unitpicker::ask(std::move(spec), [done, chosen](const HmiUnitPicker::Answer& a) {
                    if (a.manage) return;
                    if (chosen) chosen(a.unit);
                    if (done) done(ui::Chosen{a.unit, {}});
                });
            };
            out.push_back(std::move(a));
        }
    };
}

std::vector<std::string> usesOf(const hmi::Project& p, std::string_view symbol) {
    std::vector<std::string> out;
    const hu::Catalog cat(p.config);
    const std::string want = fold(cat.canonical(symbol).empty() ? std::string(symbol) : cat.canonical(symbol));
    if (want.empty()) return out;
    const auto is = [&](const std::string& u) {
        if (trim(u).empty()) return false;
        const std::string c = cat.canonical(u);
        return fold(c.empty() ? u : c) == want;
    };
    const auto decls = [&](const std::vector<hmi::Declaration>& ds, const std::string& owner) {
        for (const auto& d : ds)
            if (is(d.unit)) out.push_back(owner + "." + d.name);
    };
    const auto function = [&](const hmi::HmiFunction& f, const std::string& owner) {
        const std::string name = owner.empty() ? f.name : owner + "." + f.name;
        if (is(f.returnUnit)) out.push_back(name + " (retour)");
        decls(f.decls, name);
    };
    // 1.13.0 (seconde passe) : chaque porteur de declarations (hmi::forEachDeclarations en a la liste) - avec les
    // constantes de vue, les operateurs (d'un type, d'un symbole) et les redefinitions d'une instance.
    const auto operators = [&](const std::vector<hmi::HmiOperator>& ops, const std::string& owner) {
        for (const auto& o : ops) decls(o.decls, owner + "." + hmi::operatorFunctionName(o));
    };
    for (const auto& v : p.programs.variables)
        if (is(v.unit)) out.push_back(v.name);
    for (const auto& t : p.programs.types) {
        for (const auto& m : t.members)
            if (is(m.unit)) out.push_back(t.name + "." + m.name);
        for (const auto& m : t.methods) function(m, t.name);
        operators(t.operators, t.name);
    }
    for (const auto& f : p.programs.functions) function(f, {});
    for (const auto& s : p.programs.scripts) decls(s.decls, s.name);
    for (const auto& v : p.views) {
        decls(v.constants, v.name);
        for (const auto& f : v.functions) function(f, v.name);
        for (const auto& s : v.scripts) decls(s.decls, v.name + "." + s.event);
        operators(v.operators, v.name);
        for (const auto& ob : v.objects)
            for (const auto& fo : ob.functionOverrides) decls(fo.decls, v.name + "." + ob.name + "." + fo.function);
    }
    return out;
}

} // namespace unitui

// ============================================================ le selecteur ====
namespace unitpicker {
namespace {
Host& host() {
    static Host h;
    return h;
}
std::vector<std::string>& recentList() {
    static std::vector<std::string> r;
    return r;
}
} // namespace
void setHost(Host h) { host() = std::move(h); }
bool available() { return static_cast<bool>(host()); }
void ask(HmiUnitPicker::Spec spec, Done done) {
    if (host()) host()(std::move(spec), std::move(done));
}
void remember(const std::string& symbol) {
    if (trim(symbol).empty()) return;
    auto& r = recentList();
    r.erase(std::remove(r.begin(), r.end(), symbol), r.end());
    r.insert(r.begin(), symbol);
    if (r.size() > 8) r.resize(8);
}
const std::vector<std::string>& recents() { return recentList(); }
} // namespace unitpicker

namespace {
const gfx::FontId kSmall{13};
const std::string kAllFamilies = "Toutes les familles";
const std::string kRecent = "R\xC3\xA9" "centes";
} // namespace

class HmiUnitPicker::Body final : public ui::Widget {
public:
    explicit Body(HmiUnitPicker& d) : ui::Widget("dialog.hmiUnitPicker.body"), d_(d) {}
    ui::Button* none{nullptr};
    ui::Button* manage{nullptr};
    ui::Button* cancel{nullptr};
    ui::Button* ok{nullptr};
    gfx::Rect   panel{};
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(820.f, r.w - 40.f);
        const float h = std::min(560.f, r.h - 40.f);
        panel = ui::dialogframe::place(*this, {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h});
        const float left = panel.x + 20.f, inner = panel.w - 40.f;
        float y = panel.y + 44.f + 8.f;
        textY_ = y;
        y += 26.f;
        const float boxW = std::min(260.f, inner * 0.36f);
        if (d_.searchField_) d_.searchField_->setBounds({left, y, inner - boxW - 10.f, 30.f});
        if (d_.familyBox_) d_.familyBox_->setBounds({left + inner - boxW, y, boxW, 30.f});
        y += 40.f;
        const float bottom = panel.y + panel.h - 56.f - 48.f;
        if (d_.table_) d_.table_->setBounds({left, y, inner, std::max(60.f, bottom - y)});
        infoY_ = bottom + 6.f;
        float bx = panel.x + panel.w - 20.f;
        for (ui::Button* b : {ok, cancel, none}) {
            if (!b) continue;
            const float bw = std::max(104.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            b->setBounds({bx, panel.y + panel.h - 46.f, bw, 32.f});
            bx -= 10.f;
        }
        if (manage) {
            const float bw = std::max(150.f, ui::measureWidth(manage->text(), gfx::FontId{16}) + 36.f);
            manage->setBounds({left, panel.y + panel.h - 46.f, bw, 32.f});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const float left = panel.x + 20.f;
        ctx.r.pushClip({panel.x + 8.f, panel.y + th, panel.w - 16.f, panel.h - th});
        ctx.r.drawText({left, textY_}, "Vide : sans unit\xC3\xA9. Une unit\xC3\xA9 ne va que sur un nombre. La recherche ignore les majuscules et les accents.",
                       kSmall, c.textMuted);
        ctx.r.drawText({left, infoY_}, d_.description(), ctx.theme.font.ui, c.text);
        if (const std::string n = d_.notice(); !n.empty()) ctx.r.drawText({left, infoY_ + 22.f}, n, kSmall, c.warning);
        ctx.r.popClip();
    }
private:
    HmiUnitPicker& d_;
    float textY_{0}, infoY_{0};
};

HmiUnitPicker::HmiUnitPicker(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    reload();
    const hu::Catalog cat = spec_.doc ? hu::Catalog(spec_.doc->project.config) : hu::Catalog();
    selected_ = cat.canonical(spec_.current);
    answer_.unit = selected_;
}

HmiUnitPicker::~HmiUnitPicker() = default;

menu::MenuTraits HmiUnitPicker::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiUnitPicker::title() const {
    return "Choisir une unit\xC3\xA9" + (spec_.field.empty() ? std::string{} : " \xE2\x80\x94 " + spec_.field);
}

void HmiUnitPicker::reload() {
    const hu::Catalog cat = spec_.doc ? hu::Catalog(spec_.doc->project.config) : hu::Catalog();
    all_ = cat.units();
    // Par famille (dans l'ordre du catalogue), la reference d'abord.
    std::vector<std::string> order;
    for (const auto& f : cat.families()) order.push_back(f.name);
    std::stable_sort(all_.begin(), all_.end(), [&](const hmi::UnitDef& a, const hmi::UnitDef& b) {
        const auto ia = std::find(order.begin(), order.end(), a.family) - order.begin();
        const auto ib = std::find(order.begin(), order.end(), b.family) - order.begin();
        return ia < ib;
    });
}

std::vector<std::string> HmiUnitPicker::familyChoices() const {
    const hu::Catalog cat = spec_.doc ? hu::Catalog(spec_.doc->project.config) : hu::Catalog();
    std::vector<std::string> out{kAllFamilies, kRecent};
    for (const auto& f : cat.families()) out.push_back(cat.familyLabel(f.name));
    return out;
}

void HmiUnitPicker::refill() {
    const hu::Catalog cat = spec_.doc ? hu::Catalog(spec_.doc->project.config) : hu::Catalog();
    shown_.clear();
    const std::string q = fold(search_);
    std::vector<std::string> recent = spec_.recents;
    for (std::size_t i = 0; i < all_.size(); ++i) {
        const auto& u = all_[i];
        if (family_ == "*") {
            if (std::none_of(recent.begin(), recent.end(), [&](const std::string& r) { return fold(r) == fold(u.symbol); })) continue;
        } else if (!family_.empty() && u.family != family_) {
            continue;
        }
        const std::string fam = cat.familyLabel(u.family);
        if (!q.empty() && fold(u.symbol).find(q) == std::string::npos && fold(u.label).find(q) == std::string::npos
            && fold(fam).find(q) == std::string::npos && fold(u.family).find(q) == std::string::npos)
            continue;
        shown_.push_back(i);
    }
    std::vector<std::vector<std::string>> rows;
    for (const auto i : shown_) {
        const auto& u = all_[i];
        rows.push_back({u.symbol, u.label.empty() ? u.symbol : u.label, cat.familyLabel(u.family), conversionText(cat, u)});
    }
    const std::vector<std::size_t> shown = shown_;
    model_ = std::make_shared<hmikit::Rows>(std::vector<std::string>{"Unit\xC3\xA9", "Nom", "Famille", "Conversion"}, std::move(rows),
                                            [](ui::RowIndex, std::size_t c) {
                                                ui::CellStyle s;
                                                if (c == 0) {
                                                    s.monospace = true;
                                                    s.bold = true;
                                                }
                                                if (c == 3) s.fgTone = ui::Tone::Muted;
                                                return s;
                                            });
    if (!table_) return;
    syncing_ = true;
    table_->setModel(model_);
    // La ligne choisie reste (si elle se montre) ; sinon la premiere qui correspond exactement, puis la premiere.
    int pick = -1;
    for (std::size_t k = 0; k < shown_.size(); ++k)
        if (fold(all_[shown_[k]].symbol) == fold(selected_)) pick = static_cast<int>(k);
    if (pick < 0 && !q.empty())
        for (std::size_t k = 0; k < shown_.size(); ++k)
            if (fold(all_[shown_[k]].symbol) == q) pick = static_cast<int>(k);
    if (pick < 0 && !shown_.empty() && (!q.empty() || selected_.empty() || family_.size())) pick = 0;
    if (pick >= 0) {
        table_->selectModelRows({static_cast<ui::RowIndex>(pick)}, false);
        selected_ = all_[shown_[static_cast<std::size_t>(pick)]].symbol;
    } else {
        table_->selectModelRows({}, false);
    }
    syncing_ = false;
    if (body_) body_->invalidate();
}

void HmiUnitPicker::setSearch(const std::string& text) {
    search_ = text;
    if (searchField_ && searchField_->text() != text) {
        syncing_ = true;
        searchField_->setText(text);
        syncing_ = false;
    }
    refill();
}

bool HmiUnitPicker::setFamily(std::string_view labelOrName) {
    const hu::Catalog cat = spec_.doc ? hu::Catalog(spec_.doc->project.config) : hu::Catalog();
    std::string want;
    int index = -1;
    const auto choices = familyChoices();
    if (fold(labelOrName) == fold(kAllFamilies) || fold(labelOrName) == fold("Toutes")) {
        want.clear();
        index = 0;
    } else if (fold(labelOrName) == fold(kRecent)) {
        want = "*";
        index = 1;
    } else {
        for (std::size_t k = 0; k < cat.families().size(); ++k) {
            const auto& f = cat.families()[k];
            if (fold(f.name) == fold(labelOrName) || fold(cat.familyLabel(f.name)) == fold(labelOrName)) {
                want = f.name;
                index = static_cast<int>(k) + 2;
            }
        }
        if (index < 0) return false;
    }
    family_ = want;
    if (familyBox_ && familyBox_->selectedIndex() != index) {
        syncing_ = true;
        familyBox_->setSelectedIndex(index);
        syncing_ = false;
    }
    refill();
    return true;
}

bool HmiUnitPicker::select(std::string_view symbol) {
    for (std::size_t k = 0; k < shown_.size(); ++k)
        if (fold(all_[shown_[k]].symbol) == fold(symbol)) {
            selected_ = all_[shown_[k]].symbol;
            if (table_) {
                syncing_ = true;
                table_->selectModelRows({static_cast<ui::RowIndex>(k)}, false);
                syncing_ = false;
            }
            if (body_) body_->invalidate();
            return true;
        }
    return false;
}

std::vector<std::string> HmiUnitPicker::shownSymbols() const {
    std::vector<std::string> out;
    for (const auto i : shown_) out.push_back(all_[i].symbol);
    return out;
}

std::string HmiUnitPicker::selected() const {
    for (const auto i : shown_)
        if (fold(all_[i].symbol) == fold(selected_)) return all_[i].symbol;
    return {};
}

std::string HmiUnitPicker::description() const {
    const std::string s = selected();
    if (s.empty()) return shown_.empty() ? std::string("Aucune unit\xC3\xA9 ne correspond.") : std::string("Choisis une unit\xC3\xA9.");
    const hu::Catalog cat = spec_.doc ? hu::Catalog(spec_.doc->project.config) : hu::Catalog();
    const auto* u = cat.unit(s);
    if (!u) return s;
    const auto* f = cat.family(u->family);
    return u->symbol + " : " + (u->label.empty() ? u->symbol : u->label) + " \xE2\x80\x94 " + cat.familyLabel(u->family)
         + (f ? " (r\xC3\xA9" "f\xC3\xA9rence : " + f->reference + ")" : std::string{}) + " \xC2\xB7 " + conversionText(cat, *u);
}

std::string HmiUnitPicker::notice() const {
    if (!trim(spec_.type).empty() && !hu::typeTakesUnit(spec_.type))
        return "Le type " + trim(spec_.type) + " ne prend pas d'unit\xC3\xA9 : seulement un nombre (REAL, INT\xE2\x80\xA6).";
    const hu::Catalog cat = spec_.doc ? hu::Catalog(spec_.doc->project.config) : hu::Catalog();
    if (!trim(spec_.current).empty() && !cat.known(spec_.current))
        return "L'unit\xC3\xA9 actuelle \xC2\xAB " + trim(spec_.current) + " \xC2\xBB n'existe pas (ni fournie, ni du projet).";
    return {};
}

void HmiUnitPicker::choose() {
    const std::string s = selected();
    if (s.empty()) return;
    answer_.unit = s;
    answer_.manage = false;
    unitpicker::remember(s);
    finish(true);
}

void HmiUnitPicker::chooseNone() {
    answer_.unit.clear();
    answer_.manage = false;
    finish(true);
}

void HmiUnitPicker::manage() {
    if (!spec_.canManage) return;
    answer_.manage = true;
    finish(true);
}

bool HmiUnitPicker::manageShown() const noexcept { return spec_.canManage; }

std::string HmiUnitPicker::encode(const Answer& a) { return core::joinFields({a.unit, a.manage ? "1" : "0"}); }

bool HmiUnitPicker::parse(const std::string& payload, Answer& out) {
    const auto v = core::splitFields(payload);
    if (v.empty()) return false;
    out.unit = v[0];
    out.manage = v.size() > 1 && v[1] == "1";
    return true;
}

void HmiUnitPicker::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? encode(answer_) : std::string{}});
}

core::Status HmiUnitPicker::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    searchField_ = &static_cast<ui::InputText&>(body->addChild(std::make_unique<ui::InputText>(base + ".search")));
    searchField_->setPlaceholder("Rechercher une unit\xC3\xA9 (symbole, nom, famille)\xE2\x80\xA6");
    links_ += searchField_->textChanged->connect([this](const std::string& t) {
        if (syncing_) return;
        search_ = t;
        refill();
    });
    links_ += searchField_->editingDone->connect([this](const std::string&) {
        if (!syncing_ && searchField_->focused()) choose();
    });
    familyBox_ = &static_cast<ui::DropDown&>(body->addChild(std::make_unique<ui::DropDown>(base + ".family")));
    {
        std::vector<ui::DropDown::Item> items;
        for (const auto& c : familyChoices()) items.push_back({c, c, {}, true});
        familyBox_->setItems(std::move(items));
        familyBox_->setSelectedIndex(0);
    }
    links_ += familyBox_->selectionChanged->connect([this](int) {
        if (syncing_) return;
        if (const auto* it = familyBox_->selectedItem()) (void)setFamily(it->value);
    });
    auto table = std::make_unique<ui::TableView>(base + ".table");
    table->setColumns({{"Unit\xC3\xA9", 90.f}, {"Nom", 220.f}, {"Famille", 150.f}, {"Conversion", 280.f}});
    table->setSelectionMode(ui::SelectionMode::Single);
    table->setEnterActivates(true);
    table_ = &static_cast<ui::TableView&>(body->addChild(std::move(table)));
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_ || rows.empty() || rows.front() >= shown_.size()) return;
        selected_ = all_[shown_[rows.front()]].symbol;
        if (body_) body_->invalidate();
    });
    links_ += table_->activated->connect([this](ui::RowIndex r) {
        if (r < shown_.size()) {
            selected_ = all_[shown_[r]].symbol;
            choose();
        }
    });
    if (spec_.canManage) {   // 1.13.0 (seconde passe) : pas depuis une fenetre modale (la page s'ouvrirait dessous)
        body->manage = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("G\xC3\xA9rer les unit\xC3\xA9s\xE2\x80\xA6", base + ".manage")));
        body->manage->setTooltip("Configuration \xE2\x80\xBA Unit\xC3\xA9s de mesure : les familles et les unit\xC3\xA9s du projet");
        links_ += body->manage->clicked->connect([this] { manage(); });
    }
    body->none = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Sans unit\xC3\xA9", base + ".none")));
    links_ += body->none->clicked->connect([this] { chooseNone(); });
    body->cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    links_ += body->cancel->clicked->connect([this] { finish(false); });
    body->ok = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Choisir", base + ".ok")));
    body->ok->setStyle(ui::Button::Style::Primary);
    links_ += body->ok->clicked->connect([this] { choose(); });
    // Le projet relu a chaque changement : une unite ajoutee ailleurs apparait aussitot.
    if (spec_.doc) links_ += spec_.doc->changed->connect([this](hmi::Id) {
        reload();
        refill();
    });
    setRoot(std::move(body));
    refill();
    focus().focus(searchField_);
    return core::ok();
}

ui::EventResult HmiUnitPicker::HandleEvent(const ui::InputEvent& ev) {
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return) {
            choose();
            return ui::EventResult::Consumed;
        }
    }
    return r;
}

// ============================================ Configuration > Unites de mesure ===
bool HmiMeasureUnitsPane::builtinUnit(std::string_view symbol) {
    for (const auto& u : hu::builtinUnits())
        if (fold(u.symbol) == fold(symbol)) return true;
    return false;
}

bool HmiMeasureUnitsPane::builtinFamily(std::string_view name) {
    for (const auto& f : hu::builtinFamilies())
        if (fold(f.name) == fold(name)) return true;
    return false;
}

HmiMeasureUnitsPane::HmiMeasureUnitsPane(std::string id, hmi::DocumentPtr doc, Apply apply)
    : ui::Widget(std::move(id)), doc_(std::move(doc)), apply_(std::move(apply)) {
    const std::string base = this->id();
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(AFamily, HmiGlyph::Plus, "Nouvelle famille : une grandeur \xC3\xA0 elle (son nom, son unit\xC3\xA9 de r\xC3\xA9" "f\xC3\xA9rence)", "Famille");
    tools->add(AUnit, HmiGlyph::Plus, "Nouvelle unit\xC3\xA9 dans la famille choisie (son symbole, son facteur vers la r\xC3\xA9" "f\xC3\xA9rence, son d\xC3\xA9" "calage)",
               "Unit\xC3\xA9");
    tools->add(ARemove, HmiGlyph::Delete, "Retirer l'unit\xC3\xA9 (ou la famille) du projet choisie : une unit\xC3\xA9 employ\xC3\xA9" "e ne se retire pas (Ctrl+Z la rend)",
               "Retirer");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setEnabledWhen(AUnit, [this] { return !selectedFamily().empty(); });
    tools_->setEnabledWhen(ARemove, [this] {
        if (focusFamily_) return !selectedFamily().empty() && !builtinFamily(selectedFamily());
        return !selectedUnit().empty() && !builtinUnit(selectedUnit());
    });
    strict_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>(
        "Unit\xC3\xA9s strictes : une affectation de m\xC3\xAAme famille exige EN(\xE2\x80\xA6)", base + ".strict")));
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".familiesPanel", "FAMILLES");
        auto table = std::make_unique<ui::TableView>(base + ".families");
        // 1.13.0 (lot F3) : des largeurs qui laissent lire les titres (Reference, Emplois : plus de "Refer..." ni "Em...").
        table->setColumns({{"Famille", 160.f}, {"R\xC3\xA9" "f\xC3\xA9rence", 120.f}, {"Unit\xC3\xA9s", 80.f, 40.f, true, true, true, ui::Align::End},
                           {"Provenance", 110.f}});
        table->setSelectionMode(ui::SelectionMode::Single);
        families_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        familiesPanel_ = &static_cast<HmiTitledPanel&>(addChild(std::move(panel)));
    }
    {
        auto panel = std::make_unique<HmiTitledPanel>(base + ".unitsPanel", "UNIT\xC3\x89S");
        auto table = std::make_unique<ui::TableView>(base + ".units");
        table->setColumns({{"Unit\xC3\xA9", 90.f}, {"Nom", 200.f}, {"Conversion", 280.f}, {"Provenance", 110.f},
                           {"Emplois", 100.f, 40.f, true, true, true, ui::Align::End}});
        table->setSelectionMode(ui::SelectionMode::Single);
        units_ = &static_cast<ui::TableView&>(panel->setBody(std::move(table)));
        unitsPanel_ = &static_cast<HmiTitledPanel&>(addChild(std::move(panel)));
    }
    auto grid = std::make_unique<ui::PropertyGrid>(base + ".grid");
    grid->setShowDescriptionPane(false);
    grid->setNameColumnRatio(0.36f);
    grid_ = &static_cast<ui::PropertyGrid&>(addChild(std::move(grid)));
    status_ = &static_cast<ui::StatusBar&>(addChild(std::make_unique<ui::StatusBar>(base + ".status")));
    tools_->bindListKeys(*units_, {ARemove, 0, AUnit});

    links_ += tools_->triggered->connect([this](int a) {
        std::string why;
        switch (a) {
            case AFamily: {
                // Un nom et une reference libres (famille, famille2... ; u, u2...) : a changer dans la fiche.
                const hu::Catalog cat(doc_->project.config);
                std::string name = "grandeur", ref = "u";
                for (int k = 2; cat.family(name) && k < 100; ++k) name = "grandeur" + std::to_string(k);
                for (int k = 2; cat.unit(ref) && k < 100; ++k) ref = "u" + std::to_string(k);
                if (!addFamily(name, ref, "Grandeur", &why)) say(why, true);
                break;
            }
            case AUnit: {
                const std::string fam = selectedFamily();
                if (fam.empty()) break;
                const hu::Catalog cat(doc_->project.config);
                hmi::UnitDef u;
                u.family = fam;
                u.symbol = "u";
                for (int k = 2; cat.unit(u.symbol) && k < 100; ++k) u.symbol = "u" + std::to_string(k);
                u.factor = 1.0;
                if (!addUnit(u, &why)) say(why, true);
                break;
            }
            case ARemove:
                if (focusFamily_) {
                    if (!removeFamily(selectedFamily(), &why)) say(why, true);
                } else if (!removeUnit(selectedUnit(), &why)) {
                    say(why, true);
                }
                break;
            default: break;
        }
    });
    links_ += strict_->stateChanged->connect([this](ui::Checkbox::State s) {
        if (syncing_) return;
        (void)setStrict(s == ui::Checkbox::State::Checked);
    });
    links_ += families_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_) return;
        if (!rows.empty() && rows.front() < familyNames_.size()) {
            family_ = familyNames_[rows.front()];
            focusFamily_ = true;
        }
        refreshUnits();
        rebuildProperties();
    });
    links_ += units_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_) return;
        if (!rows.empty()) focusFamily_ = false;
        rebuildProperties();
    });
    links_ += doc_->changed->connect([this](hmi::Id) { refresh(); });
    refresh();
}

void HmiMeasureUnitsPane::say(std::string text, bool error) {
    if (error) ui::PropertyGrid::refuse(text);
    message_ = std::move(text);
    status_->setMessage(message_, error ? ui::StatusBar::Severity::Warning : ui::StatusBar::Severity::Success);
}

bool HmiMeasureUnitsPane::commit(const std::string& label, const std::function<bool(hmi::Project&, std::string*)>& change, std::string* why) {
    std::string reason;
    {
        hmi::Project trial = doc_->project;          // a blanc : un refus ne laisse rien
        if (!change(trial, &reason)) {
            if (why) *why = reason;
            return false;
        }
    }
    auto cmd = hmi::changeProject(doc_, label, [&](hmi::Project& p) { (void)change(p, nullptr); });
    if (cmd) apply_(std::move(cmd));
    return true;
}

std::string HmiMeasureUnitsPane::selectedFamily() const { return family_; }

void HmiMeasureUnitsPane::selectFamily(const std::string& name) {
    for (std::size_t i = 0; i < familyNames_.size(); ++i)
        if (fold(familyNames_[i]) == fold(name)) {
            family_ = familyNames_[i];
            focusFamily_ = true;
            syncing_ = true;
            families_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
            syncing_ = false;
            refreshUnits();
            rebuildProperties();
            return;
        }
}

std::string HmiMeasureUnitsPane::selectedUnit() const {
    const int r = hmikit::selectedRow(*units_);
    return r >= 0 && static_cast<std::size_t>(r) < unitSymbols_.size() ? unitSymbols_[static_cast<std::size_t>(r)] : std::string{};
}

void HmiMeasureUnitsPane::selectUnit(const std::string& symbol) {
    const hu::Catalog cat(doc_->project.config);
    if (const auto* u = cat.unit(symbol); u && fold(u->family) != fold(family_)) selectFamily(u->family);
    for (std::size_t i = 0; i < unitSymbols_.size(); ++i)
        if (fold(unitSymbols_[i]) == fold(symbol)) {
            focusFamily_ = false;
            syncing_ = true;
            units_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
            syncing_ = false;
            rebuildProperties();
            return;
        }
}

void HmiMeasureUnitsPane::refresh() {
    const auto& p = doc_->project;
    const hu::Catalog cat(p.config);
    syncing_ = true;
    strict_->setState(p.config.strictUnits ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    familyNames_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<bool> own;
    for (const auto& f : cat.families()) {
        familyNames_.push_back(f.name);
        const auto n = std::count_if(cat.units().begin(), cat.units().end(), [&](const hmi::UnitDef& u) { return u.family == f.name; });
        const bool builtin = builtinFamily(f.name);
        own.push_back(!builtin);
        rows.push_back({cat.familyLabel(f.name), f.reference, std::to_string(n), builtin ? std::string("fournie") : std::string("du projet")});
    }
    families_->setModel(std::make_shared<hmikit::Rows>(
        std::vector<std::string>{"Famille", "R\xC3\xA9" "f\xC3\xA9rence", "Unit\xC3\xA9s", "Provenance"}, std::move(rows),
        [own](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            const bool mine = r < own.size() && own[r];
            if (c == 0) {
                s.icon = mine ? ui::Icon::Folder : ui::Icon::Lock;
                s.iconTone = mine ? ui::Tone::Accent : ui::Tone::Muted;
                s.bold = mine;
            }
            if (c == 1) s.monospace = true;
            if (c == 3) s.fgTone = mine ? ui::Tone::Accent : ui::Tone::Muted;
            return s;
        }));
    if (family_.empty() || std::none_of(familyNames_.begin(), familyNames_.end(), [&](const std::string& n) { return n == family_; }))
        family_ = familyNames_.empty() ? std::string{} : familyNames_.front();
    for (std::size_t i = 0; i < familyNames_.size(); ++i)
        if (familyNames_[i] == family_) families_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    syncing_ = false;
    refreshUnits();
    rebuildProperties();
    const std::size_t mineU = p.config.units.size(), mineF = p.config.unitFamilies.size();
    if (message_.empty())
        status_->setMessage(std::to_string(cat.units().size()) + " unit\xC3\xA9s en " + std::to_string(cat.families().size()) + " familles \xC2\xB7 "
                                + std::to_string(mineU) + " du projet, " + std::to_string(mineF) + " famille(s) du projet \xC2\xB7 "
                                + (p.config.strictUnits ? "unit\xC3\xA9s strictes" : "l'affectation convertit d'office"),
                            ui::StatusBar::Severity::Info);
    invalidate();
}

void HmiMeasureUnitsPane::refreshUnits() {
    const auto& p = doc_->project;
    const hu::Catalog cat(p.config);
    const std::string keep = selectedUnit();
    unitSymbols_.clear();
    std::vector<std::vector<std::string>> rows;
    std::vector<bool> own;
    for (const auto& u : cat.units()) {
        if (u.family != family_) continue;
        unitSymbols_.push_back(u.symbol);
        const bool builtin = builtinUnit(u.symbol);
        own.push_back(!builtin);
        rows.push_back({u.symbol, u.label.empty() ? u.symbol : u.label, conversionText(cat, u),
                        builtin ? std::string("fournie") : std::string("du projet"), std::to_string(unitui::usesOf(p, u.symbol).size())});
    }
    if (unitsPanel_) unitsPanel_->setTitle("UNIT\xC3\x89S \xE2\x80\x94 " + (family_.empty() ? std::string("aucune famille") : cat.familyLabel(family_)));
    syncing_ = true;
    units_->setModel(std::make_shared<hmikit::Rows>(
        std::vector<std::string>{"Unit\xC3\xA9", "Nom", "Conversion", "Provenance", "Emplois"}, std::move(rows),
        [own](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            const bool mine = r < own.size() && own[r];
            if (c == 0) {
                s.monospace = true;
                s.bold = true;
                s.icon = mine ? ui::Icon::Constant : ui::Icon::Lock;
                s.iconTone = mine ? ui::Tone::Accent : ui::Tone::Muted;
            }
            if (c == 2 || (c == 3 && !mine)) s.fgTone = ui::Tone::Muted;
            if (c == 3 && mine) s.fgTone = ui::Tone::Accent;
            return s;
        }));
    for (std::size_t i = 0; i < unitSymbols_.size(); ++i)
        if (unitSymbols_[i] == keep) units_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    syncing_ = false;
}

void HmiMeasureUnitsPane::rebuildProperties() {
    const auto& p = doc_->project;
    const hu::Catalog cat(p.config);
    std::vector<PG::Category> cats;
    if (focusFamily_ || selectedUnit().empty()) {
        const auto* f = cat.family(family_);
        if (!f) {
            grid_->setCategories({});
            return;
        }
        const bool builtin = builtinFamily(f->name);
        const std::string name = f->name;
        PG::Category c;
        c.name = "Famille";
        c.properties.push_back(prop("Nom", f->name, builtin ? PG::ValueType::ReadOnly : PG::ValueType::Text,
                                    builtin ? std::function<bool(std::string_view)>{} : [this, name](std::string_view v) {
                                        std::string why;
                                        const bool ok = renameFamily(name, std::string(v), &why);
                                        if (!ok) say("Refus\xC3\xA9 : " + why, true);
                                        return ok;
                                    },
                                    "L'identifiant : des minuscules, des chiffres et _ ; ses unit\xC3\xA9s le suivent."));
        c.properties.push_back(prop("Libell\xC3\xA9", cat.familyLabel(f->name), PG::ValueType::Text,
                                    builtin ? std::function<bool(std::string_view)>{} : [this, name](std::string_view v) {
                                        std::string why;
                                        const bool ok = setFamilyLabel(name, std::string(v), &why);
                                        if (!ok) say(why, true);
                                        return ok;
                                    }));
        c.properties.push_back(prop("R\xC3\xA9" "f\xC3\xA9rence", f->reference, PG::ValueType::ReadOnly, {},
                                    "Toutes les unit\xC3\xA9s de la famille se convertissent vers elle (valeur \xC3\x97 facteur + d\xC3\xA9" "calage)."));
        c.properties.push_back(prop("Provenance", builtin ? "fournie (verrouill\xC3\xA9" "e)" : "du projet", PG::ValueType::ReadOnly));
        if (builtin) c.properties.back().locked = true;
        cats.push_back(std::move(c));
        grid_->setCategories(std::move(cats));
        return;
    }
    const auto* u = cat.unit(selectedUnit());
    if (!u) {
        grid_->setCategories({});
        return;
    }
    const bool builtin = builtinUnit(u->symbol);
    const std::string sym = u->symbol;
    const auto edit = [this, sym](int field) {
        return [this, sym, field](std::string_view v) {
            const hu::Catalog c2(doc_->project.config);
            const auto* cur = c2.unit(sym);
            if (!cur) return false;
            hmi::UnitDef after = *cur;
            double d = 0;
            if (field == 0) after.label = trim(v);
            else if (!parseDouble(v, d)) {
                say("Refus\xC3\xA9 : \xC2\xAB " + trim(v) + " \xC2\xBB n'est pas un nombre", true);
                return false;
            } else if (field == 1) after.factor = d;
            else after.offset = d;
            std::string why;
            const bool ok = updateUnit(sym, after, &why);
            if (!ok) say("Refus\xC3\xA9 : " + why, true);
            return ok;
        };
    };
    using Commit = std::function<bool(std::string_view)>;
    PG::Category c;
    c.name = "Unit\xC3\xA9";
    c.properties.push_back(prop("Symbole", u->symbol, builtin ? PG::ValueType::ReadOnly : PG::ValueType::Text,
                                builtin ? Commit{} : Commit{[this, sym](std::string_view v) {
                                    std::string why;
                                    const bool ok = renameUnit(sym, std::string(v), &why);
                                    if (!ok) say("Refus\xC3\xA9 : " + why, true);
                                    return ok;
                                }},
                                "Ce qu'on tape dans la case Unit\xC3\xA9 et dans EN(x, 'symbole'). Une unit\xC3\xA9 du projet que rien n'emploie se renomme ici."));
    c.properties.push_back(prop("Nom", u->label, PG::ValueType::Text, builtin ? Commit{} : edit(0)));
    c.properties.push_back(prop("Famille", cat.familyLabel(u->family), PG::ValueType::ReadOnly));
    c.properties.push_back(prop("Facteur", number(u->factor), PG::ValueType::Text, builtin ? Commit{} : edit(1),
                                "valeur de r\xC3\xA9" "f\xC3\xA9rence = valeur \xC3\x97 facteur + d\xC3\xA9" "calage"));
    c.properties.push_back(prop("D\xC3\xA9" "calage", number(u->offset), PG::ValueType::Text, builtin ? Commit{} : edit(2),
                                "0 le plus souvent ; une temp\xC3\xA9rature : 273,15 pour degC vers K"));
    c.properties.push_back(prop("Conversion", conversionText(cat, *u), PG::ValueType::ReadOnly));
    c.properties.push_back(prop("Provenance", builtin ? "fournie (verrouill\xC3\xA9" "e)" : "du projet", PG::ValueType::ReadOnly));
    if (builtin) c.properties.back().locked = true;
    const auto uses = unitui::usesOf(p, u->symbol);
    c.properties.push_back(prop("Emplois", uses.empty() ? std::string("(aucun)") : std::to_string(uses.size()) + " : " + hmikit::fewOf(uses, 4),
                                PG::ValueType::ReadOnly));
    cats.push_back(std::move(c));
    grid_->setCategories(std::move(cats));
}

bool HmiMeasureUnitsPane::addFamily(const std::string& rawName, const std::string& rawRef, const std::string& label, std::string* why) {
    const std::string name = trim(rawName), ref = trim(rawRef);
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        say("Famille refus\xC3\xA9" "e : " + w, true);
        return false;
    };
    if (!familyNameOk(name)) return refuse("son nom : des minuscules, des chiffres et _ (pas de chiffre en t\xC3\xAAte)");
    const hu::Catalog cat(doc_->project.config);
    if (cat.family(name)) return refuse("la famille \xC2\xAB " + name + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0");
    if (!symbolOk(ref)) return refuse("sa r\xC3\xA9" "f\xC3\xA9rence : un symbole court, sans espace (bar, m3/h)");
    if (const auto* u = cat.unit(ref))
        return refuse("\xC2\xAB " + ref + " \xC2\xBB est d\xC3\xA9j\xC3\xA0 une unit\xC3\xA9 (" + cat.familyLabel(u->family)
                      + ") : la r\xC3\xA9" "f\xC3\xA9rence d'une famille nouvelle est une unit\xC3\xA9 nouvelle");
    const bool ok = commit("Nouvelle famille " + name, [&](hmi::Project& p, std::string*) {
        p.config.unitFamilies.push_back({name, ref, trim(label)});
        p.config.units.push_back({ref, name, 1.0, 0.0, {}});
        return true;
    }, why);
    if (ok) {
        family_ = name;
        selectFamily(name);
        say("Famille " + (trim(label).empty() ? name : trim(label)) + " ajout\xC3\xA9" "e, r\xC3\xA9" "f\xC3\xA9rence " + ref + " (Ctrl+Z la retire).");
    }
    return ok;
}

bool HmiMeasureUnitsPane::setFamilyLabel(const std::string& name, const std::string& label, std::string* why) {
    if (builtinFamily(name)) {
        if (why) *why = "une famille fournie est verrouill\xC3\xA9" "e";
        return false;
    }
    const auto& fams = doc_->project.config.unitFamilies;
    const auto it = std::find_if(fams.begin(), fams.end(), [&](const hmi::UnitFamily& f) { return fold(f.name) == fold(name); });
    if (it == fams.end()) {
        if (why) *why = "famille introuvable";
        return false;
    }
    if (it->label == trim(label)) return true;
    return commit("Libell\xC3\xA9 de la famille " + name, [&](hmi::Project& p, std::string*) {
        for (auto& f : p.config.unitFamilies)
            if (fold(f.name) == fold(name)) f.label = trim(label);
        return true;
    }, why);
}

bool HmiMeasureUnitsPane::renameFamily(const std::string& rawName, const std::string& rawNew, std::string* why) {
    const std::string name = trim(rawName), next = trim(rawNew);
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        return false;
    };
    if (builtinFamily(name)) return refuse("une famille fournie est verrouill\xC3\xA9" "e");
    const hu::Catalog cat(doc_->project.config);
    const auto* f = cat.family(name);
    if (!f) return refuse("famille introuvable");
    if (fold(next) == fold(f->name)) return true;
    if (!familyNameOk(next)) return refuse("son nom : des minuscules, des chiffres et _ (pas de chiffre en t\xC3\xAAte)");
    if (cat.family(next)) return refuse("la famille \xC2\xAB " + next + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0");
    const std::string old = f->name;
    const bool ok = commit("Renommer la famille " + old, [&](hmi::Project& p, std::string*) {
        for (auto& x : p.config.unitFamilies)
            if (fold(x.name) == fold(old)) x.name = next;
        for (auto& u : p.config.units)
            if (fold(u.family) == fold(old)) u.family = next;
        return true;
    }, why);
    if (ok) {
        refresh();
        selectFamily(next);
        say("Famille " + old + " renomm\xC3\xA9" "e " + next + " (Ctrl+Z).");
    }
    return ok;
}

bool HmiMeasureUnitsPane::renameUnit(const std::string& rawSymbol, const std::string& rawNew, std::string* why) {
    const std::string symbol = trim(rawSymbol), next = trim(rawNew);
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        return false;
    };
    if (builtinUnit(symbol)) return refuse("une unit\xC3\xA9 fournie est verrouill\xC3\xA9" "e");
    const hu::Catalog cat(doc_->project.config);
    const auto* u = cat.unit(symbol);
    if (!u) return refuse("unit\xC3\xA9 introuvable");
    if (next == u->symbol) return true;
    if (!symbolOk(next)) return refuse("son symbole : court, sans espace ni crochet (bar, m3/h, %)");
    if (const auto* other = cat.unit(next); other && fold(other->symbol) != fold(u->symbol))
        return refuse("\xC2\xAB " + next + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 (" + cat.familyLabel(other->family) + ")");
    if (const auto uses = unitui::usesOf(doc_->project, u->symbol); !uses.empty())
        return refuse(u->symbol + " est employ\xC3\xA9" "e (" + std::to_string(uses.size()) + " : " + hmikit::fewOf(uses, 3) + ") : change d'abord ces cases");
    const std::string old = u->symbol, family = u->family;
    const bool ok = commit("Renommer l'unit\xC3\xA9 " + old, [&](hmi::Project& p, std::string*) {
        for (auto& x : p.config.units)
            if (x.symbol == old) x.symbol = next;
        for (auto& f : p.config.unitFamilies)
            if (f.reference == old) f.reference = next;
        return true;
    }, why);
    if (ok) {
        refresh();
        selectFamily(family);
        selectUnit(next);
        say("Unit\xC3\xA9 " + old + " renomm\xC3\xA9" "e " + next + " (Ctrl+Z).");
    }
    return ok;
}

bool HmiMeasureUnitsPane::removeFamily(const std::string& name, std::string* why) {
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        return false;
    };
    if (name.empty()) return refuse("aucune famille choisie");
    if (builtinFamily(name)) return refuse("une famille fournie est verrouill\xC3\xA9" "e");
    const auto& p = doc_->project;
    for (const auto& u : p.config.units)
        if (fold(u.family) == fold(name))
            if (const auto uses = unitui::usesOf(p, u.symbol); !uses.empty())
                return refuse("son unit\xC3\xA9 " + u.symbol + " est employ\xC3\xA9" "e (" + hmikit::fewOf(uses, 3) + ")");
    const bool ok = commit("Retirer la famille " + name, [&](hmi::Project& pp, std::string*) {
        auto& fams = pp.config.unitFamilies;
        fams.erase(std::remove_if(fams.begin(), fams.end(), [&](const hmi::UnitFamily& f) { return fold(f.name) == fold(name); }), fams.end());
        auto& us = pp.config.units;
        us.erase(std::remove_if(us.begin(), us.end(), [&](const hmi::UnitDef& u) { return fold(u.family) == fold(name); }), us.end());
        return true;
    }, why);
    if (ok) {
        family_.clear();
        say("Famille " + name + " retir\xC3\xA9" "e (Ctrl+Z la rend).");
    }
    return ok;
}

bool HmiMeasureUnitsPane::addUnit(const hmi::UnitDef& rawUnit, std::string* why) {
    hmi::UnitDef u = rawUnit;
    u.symbol = trim(u.symbol);
    u.label = trim(u.label);
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        say("Unit\xC3\xA9 refus\xC3\xA9" "e : " + w, true);
        return false;
    };
    if (!symbolOk(u.symbol)) return refuse("son symbole : court, sans espace ni crochet (bar, m3/h, %)");
    const hu::Catalog cat(doc_->project.config);
    if (const auto* other = cat.unit(u.symbol))
        return refuse("\xC2\xAB " + u.symbol + " \xC2\xBB existe d\xC3\xA9j\xC3\xA0 (" + cat.familyLabel(other->family) + ")");
    const auto* f = cat.family(u.family);
    if (!f) return refuse("famille inconnue \xC2\xAB " + u.family + " \xC2\xBB");
    u.family = f->name;
    if (!std::isfinite(u.factor) || u.factor == 0.0) return refuse("le facteur doit \xC3\xAAtre un nombre non nul");
    if (!std::isfinite(u.offset)) return refuse("le d\xC3\xA9" "calage doit \xC3\xAAtre un nombre");
    const bool ok = commit("Nouvelle unit\xC3\xA9 " + u.symbol, [&](hmi::Project& p, std::string*) {
        p.config.units.push_back(u);
        return true;
    }, why);
    if (ok) {
        selectFamily(u.family);
        selectUnit(u.symbol);
        say("Unit\xC3\xA9 " + u.symbol + " ajout\xC3\xA9" "e \xC3\xA0 " + cat.familyLabel(u.family) + " (Ctrl+Z la retire).");
    }
    return ok;
}

bool HmiMeasureUnitsPane::updateUnit(const std::string& symbol, const hmi::UnitDef& after, std::string* why) {
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        return false;
    };
    if (builtinUnit(symbol)) return refuse("une unit\xC3\xA9 fournie est verrouill\xC3\xA9" "e (ajoute plut\xC3\xB4t une unit\xC3\xA9 du projet)");
    const auto& us = doc_->project.config.units;
    const auto it = std::find_if(us.begin(), us.end(), [&](const hmi::UnitDef& u) { return fold(u.symbol) == fold(symbol); });
    if (it == us.end()) return refuse("unit\xC3\xA9 introuvable");
    if (!std::isfinite(after.factor) || after.factor == 0.0) return refuse("le facteur doit \xC3\xAAtre un nombre non nul");
    if (!std::isfinite(after.offset)) return refuse("le d\xC3\xA9" "calage doit \xC3\xAAtre un nombre");
    const hu::Catalog cat(doc_->project.config);
    if (const auto* f = cat.family(it->family); f && fold(f->reference) == fold(symbol) && (after.factor != 1.0 || after.offset != 0.0))
        return refuse(symbol + " est la r\xC3\xA9" "f\xC3\xA9rence de sa famille : facteur 1, d\xC3\xA9" "calage 0");
    hmi::UnitDef next = *it;
    next.label = trim(after.label);
    next.factor = after.factor;
    next.offset = after.offset;
    if (next == *it) return true;
    const bool ok = commit("Modifier l'unit\xC3\xA9 " + symbol, [&](hmi::Project& p, std::string*) {
        for (auto& u : p.config.units)
            if (fold(u.symbol) == fold(symbol)) u = next;
        return true;
    }, why);
    if (ok) say("Unit\xC3\xA9 " + symbol + " modifi\xC3\xA9" "e : " + conversionText(hu::Catalog(doc_->project.config), next) + " (Ctrl+Z).");
    return ok;
}

bool HmiMeasureUnitsPane::removeUnit(const std::string& symbol, std::string* why) {
    const auto refuse = [&](std::string w) {
        if (why) *why = w;
        return false;
    };
    if (symbol.empty()) return refuse("aucune unit\xC3\xA9 choisie");
    if (builtinUnit(symbol)) return refuse("une unit\xC3\xA9 fournie est verrouill\xC3\xA9" "e");
    const auto& p = doc_->project;
    const hu::Catalog cat(p.config);
    const auto* u = cat.unit(symbol);
    if (!u) return refuse("unit\xC3\xA9 introuvable");
    if (const auto* f = cat.family(u->family); f && fold(f->reference) == fold(symbol))
        return refuse(symbol + " est la r\xC3\xA9" "f\xC3\xA9rence de " + cat.familyLabel(f->name) + " : retire plut\xC3\xB4t la famille");
    if (const auto uses = unitui::usesOf(p, symbol); !uses.empty())
        return refuse(symbol + " est employ\xC3\xA9" "e (" + std::to_string(uses.size()) + " : " + hmikit::fewOf(uses, 3) + ")");
    const std::string sym = u->symbol;
    const bool ok = commit("Retirer l'unit\xC3\xA9 " + sym, [&](hmi::Project& pp, std::string*) {
        auto& us = pp.config.units;
        us.erase(std::remove_if(us.begin(), us.end(), [&](const hmi::UnitDef& x) { return fold(x.symbol) == fold(sym); }), us.end());
        return true;
    }, why);
    if (ok) say("Unit\xC3\xA9 " + sym + " retir\xC3\xA9" "e (Ctrl+Z la rend).");
    return ok;
}

bool HmiMeasureUnitsPane::setStrict(bool on) {
    if (doc_->project.config.strictUnits == on) return true;
    const bool ok = commit(on ? "Unit\xC3\xA9s strictes" : "Unit\xC3\xA9s : conversion d'office", [&](hmi::Project& p, std::string*) {
        p.config.strictUnits = on;
        return true;
    }, nullptr);
    if (ok)
        say(on ? std::string("Unit\xC3\xA9s strictes : Pression := PressionBasse (mbar vers bar) exige EN(PressionBasse, 'bar') (Ctrl+Z).")
               : std::string("L'affectation de m\xC3\xAAme famille convertit d'office (une info) (Ctrl+Z)."));
    return ok;
}

void HmiMeasureUnitsPane::onLayout() {
    const auto b = bounds();
    constexpr float kTools = 38.f, kStatus = 24.f;
    const float strictW = std::min(460.f, b.w * 0.45f);
    tools_->setBounds({b.x, b.y, std::max(0.f, b.w - strictW - 8.f), kTools});
    strict_->setBounds({b.x + b.w - strictW, b.y + 6.f, strictW - 8.f, 26.f});
    status_->setBounds({b.x, b.y + b.h - kStatus, b.w, kStatus});
    const float top = b.y + kTools, h = std::max(0.f, b.h - kTools - kStatus);
    const float leftW = std::min(470.f, b.w * 0.38f);
    familiesPanel_->setBounds({b.x, top, leftW, h});
    const float rx = b.x + leftW + 4.f, rw = std::max(0.f, b.w - leftW - 4.f);
    const float unitsH = std::floor(h * 0.55f);
    unitsPanel_->setBounds({rx, top, rw, unitsH});
    grid_->setBounds({rx, top + unitsH + 4.f, rw, std::max(0.f, h - unitsH - 4.f)});
}

void HmiMeasureUnitsPane::onPaint(const ui::PaintContext& ctx) { ctx.r.fillRect(bounds(), ctx.theme.color.panelBg); }

} // namespace app
