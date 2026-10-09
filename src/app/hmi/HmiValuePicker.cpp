// =============================================================================
//  app/hmi/HmiValuePicker.cpp - voir HmiValuePicker.hpp
// =============================================================================
#include "HmiValuePicker.hpp"
#include "../../core/Edition.hpp"   // 1.12.0 : XPGAnalyser IHM - pas de source API

#include "HmiAssist.hpp"
#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiPublicVars.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiTypes.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace app {

namespace {

using Style = ui::PropertyGrid::LegendStyle;
constexpr gfx::FontId kBody{15};
constexpr gfx::FontId kSmall{13};
constexpr gfx::FontId kMono{14};
constexpr gfx::FontId kTitle{17};
constexpr float       kRow = 26.f;
constexpr char        kSep = '\x1F';

unsigned char uc(char c) { return static_cast<unsigned char>(c); }

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(uc(c)));
    return out;
}

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (r.measure(s, f).width <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    const auto n = r.fitCharacters(s, f, std::max(0.f, w - r.measure(dots, f).width));
    return s.substr(0, n) + dots;
}

std::string zoneLong(Style z) {
    switch (z) {
        case Style::Api: return "API (automate)";
        case Style::Hmi: return "IHM";
        case Style::System: return "Syst\xC3\xA8me (SYS.)";
        case Style::Local: return "Symbole / vue";
        default: break;
    }
    return {};
}
std::string zoneShort(Style z) {
    switch (z) {
        case Style::Api: return "API";
        case Style::Hmi: return "IHM";
        case Style::System: return "Syst\xC3\xA8me";
        case Style::Local: return "Symbole / vue";
        case Style::Constant: return "Constante";
        default: break;
    }
    return "Tout";
}

// Un noeud de l'arbre : un groupe (une zone), un dossier, une variable ou un membre.
struct Node {
    std::string      label, path, type, detail;
    Style            zone{Style::Hmi};
    bool             group{false};      // une zone ou un dossier : pas une valeur
    int              parent{-1};
    std::vector<int> children;
};

// LE CATALOGUE : tout ce qu'une valeur peut lire. Un tableau de 16 cases au plus
// montre ses cases ; une structure, ses membres (un niveau) ; plus, la recherche
// et le champ Resultat (l'aide a la saisie) y menent.
std::vector<Node> buildCatalog(const hmi::Project* hp, const hmi::View* view, const domain::Project* plc) {
    std::vector<Node> n;
    const auto add = [&n](int parent, Node node) {
        node.parent = parent;
        const int i = static_cast<int>(n.size());
        n.push_back(std::move(node));
        if (parent >= 0) n[static_cast<std::size_t>(parent)].children.push_back(i);
        return i;
    };
    const auto group = [&](Style z, std::string label) {
        Node g;
        g.label = std::move(label);
        g.zone = z;
        g.group = true;
        return add(-1, std::move(g));
    };
    const auto folder = [&](int parent, Style z, std::string label, std::string detail) {
        Node f;
        f.label = std::move(label);
        f.detail = std::move(detail);
        f.zone = z;
        f.group = true;
        return add(parent, std::move(f));
    };
    const auto leaf = [&](int parent, Style z, std::string label, std::string path, std::string type, std::string detail) {
        Node v;
        v.label = std::move(label);
        v.path = std::move(path);
        v.type = std::move(type);
        v.detail = std::move(detail);
        v.zone = z;
        return add(parent, std::move(v));
    };
    // ---- l'automate : les globales, leurs cases et leurs membres ----
    if (plc && core::hasApi()) {       // 1.12.0 : XPGAnalyser IHM n'en a pas
        const int g = group(Style::Api, zoneLong(Style::Api));
        // Par nom, comme l'editeur de donnees (le fichier les range dans l'ordre de declaration).
        std::vector<std::pair<std::string, const domain::Variable*>> globals;
        for (const auto& var : plc->variables) {
            if (var.scope != domain::VariableScope::Global) continue;
            std::string name(plc->strings.text(var.name));
            if (!name.empty()) globals.emplace_back(std::move(name), &var);
        }
        std::stable_sort(globals.begin(), globals.end(), [](const auto& a, const auto& b) { return lower(a.first) < lower(b.first); });
        for (const auto& [name, at] : globals) {
            const auto& var = *at;
            std::string detail = var.address.raw;
            const std::string comment(plc->strings.text(var.comment));
            if (!comment.empty()) detail += (detail.empty() ? "" : " \xC2\xB7 ") + comment;
            const auto& t = var.type;
            if (t.klass == domain::TypeClass::Array) {
                const std::string element(plc->strings.text(t.elementType));
                const std::string type = "ARRAY[" + std::to_string(t.arrayLow) + ".." + std::to_string(t.arrayHigh) + "] OF " + element;
                const int a = leaf(g, Style::Api, name, name, type, detail);
                if (t.arrayHigh >= t.arrayLow && t.arrayHigh - t.arrayLow < 16)
                    for (std::int64_t i = t.arrayLow; i <= t.arrayHigh; ++i) {
                        const std::string path = name + "[" + std::to_string(i) + "]";
                        const int e = leaf(a, Style::Api, "[" + std::to_string(i) + "]", path, element, {});
                        for (const auto& m : assist::designMembers(*plc, path))   // m.name : le membre seul
                            leaf(e, Style::Api, m.name, path + "." + m.name, m.type, m.comment);
                    }
                continue;
            }
            const int v = leaf(g, Style::Api, name, name, std::string(plc->strings.text(t.name)), detail);
            if (t.klass == domain::TypeClass::Derived || t.klass == domain::TypeClass::FunctionBlock)
                for (const auto& m : assist::designMembers(*plc, name))
                    leaf(v, Style::Api, m.name, name + "." + m.name, m.type, m.comment);
        }
    }
    // ---- l'IHM : ses variables, leurs cases et leurs membres ----
    if (hp) {
        const int g = group(Style::Hmi, zoneLong(Style::Hmi));
        for (const auto& hv : hp->programs.variables) {
            std::string detail = "initiale " + (hv.initial.empty() ? std::string("0") : hv.initial);
            if (!hv.description.empty()) detail += " \xC2\xB7 " + hv.description;
            const int v = leaf(g, Style::Hmi, hv.name, hv.name, hv.type, detail);
            hmi::types::Spec spec;
            if (hmi::types::parseSpec(hv.type, spec) && spec.array()) {
                if (spec.dims == 1 && spec.count(0) <= 16)
                    for (long long i = spec.low[0]; i <= spec.high[0]; ++i)
                        leaf(v, Style::Hmi, "[" + std::to_string(i) + "]", hv.name + "[" + std::to_string(i) + "]", spec.element, {});
            } else {
                for (const auto& m : hmi::types::membersOf(*hp, hv.type))
                    leaf(v, Style::Hmi, m.name, hv.name + "." + m.name, m.type, m.description);
            }
        }
    }
    // ---- SYS. : par domaine ----
    {
        const int g = group(Style::System, zoneLong(Style::System));
        for (std::size_t d = 0; d < hmi::pub::kSysDomainCount; ++d) {
            if (!hmi::pub::sysDomainShown(d)) continue;      // 1.12.0 : XPGAnalyser IHM - ni Automate, ni Communication
            int f = -1;
            for (const auto& sv : hmi::pub::kSysVars) {
                if (sv.domain != static_cast<int>(d)) continue;
                if (f < 0) f = folder(g, Style::System, std::string(hmi::pub::kSysDomains[d]), "syst\xC3\xA8me");
                leaf(f, Style::System, std::string(sv.name), "SYS." + std::string(sv.name), std::string(sv.type), std::string(sv.text));
            }
        }
    }
    // ---- le symbole ou la vue : ses parametres, ses variables publiques ----
    if (view) {
        const int g = group(Style::Local, zoneLong(Style::Local));
        if (!view->params.empty()) {
            const int f = folder(g, Style::Local, "Param\xC3\xA8tres de " + view->name, "param\xC3\xA8tres");
            for (const auto& prm : view->params)
                leaf(f, Style::Local, prm.name, prm.name, prm.type.empty() ? std::string("ANY") : prm.type,
                     prm.description.empty() ? (prm.defaultValue.empty() ? std::string{} : "d\xC3\xA9" "faut : " + prm.defaultValue)
                                             : prm.description);
        }
        const int f = folder(g, Style::Local, view->name, "variables publiques de la vue");
        for (const auto& m : hmi::pub::viewMembers(*view)) leaf(f, Style::Local, m.name, view->name + "." + m.name, m.type, m.text);
        if (hp)
            for (const auto& o : view->objects) {
                if (o.kind != hmi::Kind::SymbolInstance) continue;
                const auto params = hmi::pub::instanceParams(*hp, o);
                if (params.empty()) continue;
                const int fi = folder(g, Style::Local, o.name, "instance");
                for (const auto& ip : params)
                    leaf(fi, Style::Local, ip.name, view->name + "." + o.name + "." + ip.name, ip.type, ip.argument);
            }
    }
    return n;
}

} // namespace

// ================================================================ le corps ====
class HmiValuePicker::Body final : public ui::Widget {
public:
    Body(HmiValuePicker& owner, const Spec& spec) : ui::Widget("dialog.valuePicker"), owner_(owner), spec_(spec) {
        const hmi::Project* hp = spec_.doc ? &spec_.doc->project : nullptr;
        env_.project = hp;
        env_.view = hp ? hp->view(spec_.view) : nullptr;
        env_.plc = spec_.plc.get();
        nodes_ = buildCatalog(hp, env_.view, env_.plc);
        source_ = spec_.source;
        if (source_ == Style::Formula || source_ == Style::Error || source_ == Style::Markers) source_ = Style::Empty;
        markers_ = spec_.source == Style::Markers;
        fx_ = spec_.source == Style::Constant ? false : (markers_ ? spec_.fx : (spec_.text.empty() ? true : spec_.fx));
        search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.valuePicker.chercher")));
        search_->setPlaceholder("Chercher : un nom, un chemin\xE2\x80\xA6");
        links_ += search_->textChanged->connect([this](const std::string&) { rebuild(); });
        result_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.valuePicker.resultat")));
        result_->setText(spec_.text);
        if (spec_.doc) result_->setAssist(assist::fieldAssist(assist::sourcesFor(spec_.doc)));
        links_ += result_->textChanged->connect([this](const std::string&) {
            confirm_ = false;
            classify();
        });
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.valuePicker.annuler")));
        ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Valider", "dialog.valuePicker.valider")));
        ok_->setStyle(ui::Button::Style::Primary);
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += ok_->clicked->connect([this] { owner_.validate(false); });
        // Choisi d'office : la variable de la case, si elle est dans l'arbre.
        if (!spec_.text.empty())
            for (std::size_t i = 0; i < nodes_.size(); ++i)
                if (!nodes_[i].group && nodes_[i].path == spec_.text) sel_ = static_cast<int>(i);
        rebuild();
        classify();
    }

    // ---- l'etat ----
    [[nodiscard]] std::string resultText() const { return result_->text(); }
    [[nodiscard]] bool fx() const noexcept { return fx_; }
    [[nodiscard]] const valuekind::Result& res() const noexcept { return res_; }
    [[nodiscard]] std::size_t shownCount() const noexcept { return shown_; }
    [[nodiscard]] bool confirming() const noexcept { return confirm_; }
    void setConfirming(bool on) { confirm_ = on; invalidate(); }
    [[nodiscard]] bool resultFocused() const { return result_->focused(); }
    [[nodiscard]] bool suggestionsOpen() const { return result_->suggestionsOpen() || search_->suggestionsOpen(); }
    void focusFirst() {
        if (source_ == Style::Constant || markers_) owner_.focus().focus(result_);
        else owner_.focus().focus(search_);
    }

    void setSearch(const std::string& t) { search_->setText(t); rebuild(); }
    void setTypeFilter(bool on) { typeFilter_ = on; rebuild(); }
    void setSource(Style s) {
        source_ = s;
        sel_ = -1;
        if (s == Style::Constant) fx_ = false;
        rebuild();
        classify();
    }
    void setResult(const std::string& text, bool fx) {
        fx_ = fx;
        result_->setText(text);
        confirm_ = false;
        classify();
    }
    bool pick(std::string_view path) {
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            if (!nodes_[i].group && nodes_[i].path == path) {
                sel_ = static_cast<int>(i);
                use(Use::Replace);
                rebuild();
                return true;
            }
        return false;
    }
    bool applyFix(std::size_t d, std::size_t f) {
        if (d >= res_.diags.size() || f >= res_.diags[d].fixes.size()) return false;
        const auto fix = res_.diags[d].fixes[f];
        if (!fix.create.empty()) {
            owner_.answer_.create = fix.create;
            owner_.validate(true);
            return true;
        }
        setResult(fix.value, fix.fx);
        return true;
    }

    [[nodiscard]] std::string payload() const {
        const auto& a = owner_.answer_;
        return result_->text() + kSep + (fx_ ? "1" : "0") + kSep + a.create + kSep + (a.forced ? "1" : "0");
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1080.f, r.w - 40.f), h = std::min(760.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float sw = std::min(280.f, w * 0.3f);
        search_->setBounds({panel_.right() - 16.f - sw, panel_.y + 88.f, sw, 28.f});
        bottom_ = panel_.bottom() - 52.f;
        resultTop_ = bottom_ - 190.f;
        tree_ = {panel_.x + 16.f, panel_.y + 124.f, (panel_.w - 44.f) * 0.6f, resultTop_ - panel_.y - 136.f};
        detail_ = {tree_.right() + 12.f, tree_.y, panel_.right() - 16.f - tree_.right() - 12.f, tree_.h};
        fxBox_ = {panel_.x + 100.f, resultTop_ + 8.f, 34.f, 28.f};
        legendBox_ = {panel_.right() - 16.f - 26.f, resultTop_ + 9.f, 26.f, 26.f};
        result_->setBounds({fxBox_.right() + 8.f, resultTop_ + 8.f, legendBox_.x - 10.f - fxBox_.right() - 8.f, 28.f});
        const float bw = 120.f;
        ok_->setBounds({panel_.right() - 16.f - bw, bottom_ + 10.f, bw, 32.f});
        cancel_->setBounds({ok_->bounds().x - 10.f - 100.f, bottom_ + 10.f, 100.f, 32.f});
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const bool dark = ctx.theme.isDark();
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        // ---- le titre ----
        const std::string title = "Choisir la valeur de \xC2\xAB " + spec_.field + " \xC2\xBB";
        r.drawText({panel_.x + 16.f, panel_.y + 13.f}, fit(r, title, kTitle, panel_.w * 0.6f), kTitle, c.text);
        const std::string exp = "attendu : " + valuekind::expectedLabel(spec_.expected);
        const float ew = r.measure(exp, kSmall).width;
        r.drawText({panel_.right() - 50.f - ew, panel_.y + 16.f}, exp, kSmall, c.textMuted);
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, hoverClose_ ? c.text : c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        // ---- les sources ----
        chips_.clear();
        float x = panel_.x + 16.f;
        const Style sources[] = {Style::Empty, Style::Api, Style::Hmi, Style::System, Style::Local, Style::Constant};
        for (const Style s : sources) {
            if (s == Style::Api && !core::hasApi()) continue;     // 1.12.0 : XPGAnalyser IHM n'a pas d'automate
            const std::string label = zoneShort(s);
            const bool square = s != Style::Empty;
            const float tw = r.measure(label, kSmall).width + (square ? 42.f : 22.f);
            const gfx::Rect chip{x, panel_.y + 52.f, tw, 26.f};
            const bool on = s == source_;
            r.fillRoundedRect(chip, on ? c.accent.withAlpha(60) : c.headerBg, 6.f);
            if (on) r.strokeRect(chip, c.accent, 1.f);
            float tx = chip.x + 11.f;
            if (square) {
                ui::paintLegend(ctx, {chip.x + 6.f, chip.y + 5.f, 16.f, 16.f}, valuekind::legendOf(s));
                tx = chip.x + 28.f;
            }
            r.drawText({tx, chip.y + (chip.h - r.lineHeight(kSmall)) * 0.5f}, label, kSmall, on ? c.text : c.textMuted);
            chips_.push_back({chip, s});
            x += tw + 6.f;
        }
        // ---- le filtre de type, le compte ----
        typeBox_ = {panel_.x + 16.f, panel_.y + 94.f, 16.f, 16.f};
        if (source_ != Style::Constant) {
            if (typeFilter_) {
                r.fillRoundedRect(typeBox_, c.accent, 3.f);
                r.line({typeBox_.x + 3.5f, typeBox_.y + 8.5f}, {typeBox_.x + 6.5f, typeBox_.y + 11.5f}, c.textInverted, 2.f);
                r.line({typeBox_.x + 6.5f, typeBox_.y + 11.5f}, {typeBox_.x + 12.5f, typeBox_.y + 4.5f}, c.textInverted, 2.f);
            } else {
                r.strokeRect(typeBox_, c.borderStrong, 1.f);
            }
            const std::string tl = "Type attendu : " + valuekind::expectedLabel(spec_.expected);
            r.drawText({typeBox_.right() + 8.f, typeBox_.y - 1.f}, tl, kSmall, c.text);
            const float cx = typeBox_.right() + 20.f + r.measure(tl, kSmall).width;
            const std::string count = typeFilter_ ? std::to_string(shown_) + " sur " + std::to_string(total_) + " conviennent"
                                                  : std::to_string(shown_) + " \xC3\xA9l\xC3\xA9ment" + (shown_ > 1 ? "s" : "") + " \xC2\xB7 tout est montr\xC3\xA9";
            r.drawText({cx, typeBox_.y - 1.f}, count, kSmall, c.textMuted);
            const std::string link = typeFilter_ ? "Tout montrer" : "Filtrer sur le type attendu";
            const float lx = cx + r.measure(count, kSmall).width + 12.f;
            const float lw = r.measure(link, kSmall).width;
            allLink_ = {lx, typeBox_.y - 2.f, lw, 20.f};
            r.drawText({lx, typeBox_.y - 1.f}, link, kSmall, c.accent);
            r.line({lx, typeBox_.y + r.lineHeight(kSmall) - 1.f}, {lx + lw, typeBox_.y + r.lineHeight(kSmall) - 1.f}, c.accent, 1.f);
        }
        // ---- l'arbre (ou la constante) ----
        r.fillRect(tree_, c.windowBg);
        r.strokeRect(tree_, c.border, 1.f);
        rowRects_.clear();
        if (source_ == Style::Constant) {
            paintConstant(ctx);
        } else {
            r.pushClip(tree_);
            const std::size_t first = static_cast<std::size_t>(std::max(0.f, scroll_ / kRow));
            for (std::size_t k = first; k < rows_.size(); ++k) {
                const float y = tree_.y + static_cast<float>(k) * kRow - scroll_;
                if (y > tree_.bottom()) break;
                const auto& row = rows_[k];
                const Node& nd = nodes_[static_cast<std::size_t>(row.node)];
                const gfx::Rect rr{tree_.x + 1.f, y, tree_.w - 2.f, kRow};
                rowRects_.push_back({rr, row.node});
                if (row.node == sel_) r.fillRect(rr, c.selectionBg);
                else if (static_cast<int>(k) == hover_) r.fillRect(rr, c.rowAltBg);
                float ix = rr.x + 6.f + static_cast<float>(row.depth) * 16.f;
                if (!nd.children.empty()) {
                    const float ax = ix + 5.f, ay = rr.y + kRow * 0.5f;
                    const gfx::Color ac = c.textMuted;
                    if (row.open) {
                        r.line({ax - 4.f, ay - 2.f}, {ax, ay + 2.f}, ac, 1.5f);
                        r.line({ax, ay + 2.f}, {ax + 4.f, ay - 2.f}, ac, 1.5f);
                    } else {
                        r.line({ax - 2.f, ay - 4.f}, {ax + 2.f, ay}, ac, 1.5f);
                        r.line({ax + 2.f, ay}, {ax - 2.f, ay + 4.f}, ac, 1.5f);
                    }
                }
                ix += 14.f;
                if (nd.group && nd.parent >= 0) {
                    // Un dossier : un petit classeur.
                    r.strokeRect({ix + 1.f, rr.y + 8.f, 14.f, 10.f}, c.textMuted, 1.f);
                } else {
                    ui::paintLegend(ctx, {ix, rr.y + 5.f, 16.f, 16.f}, valuekind::legendOf(nd.zone));
                }
                ix += 22.f;
                const auto font = nd.group ? kBody : kMono;
                const float typeX = rr.right() - 210.f;
                r.drawText({ix, rr.y + (kRow - r.lineHeight(font)) * 0.5f}, fit(r, nd.label, font, typeX - ix - 8.f), font,
                           row.match || nd.group ? c.text : c.textDisabled);
                if (nd.group) {
                    const std::string right = nd.parent < 0 ? std::to_string(row.leaves) : nd.detail;
                    r.drawText({typeX, rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, fit(r, right, kSmall, rr.right() - typeX - 8.f), kSmall,
                               c.textMuted);
                } else {
                    r.drawText({typeX, rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, fit(r, nd.type, kSmall, 180.f), kSmall,
                               row.match ? c.textMuted : c.textDisabled);
                    if (row.match && typeFilter_)
                        r.drawText({rr.right() - 20.f, rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, "\xE2\x9C\x93", kSmall,
                                   ui::legendColor(Style::Hmi, dark));
                }
            }
            if (rows_.empty())
                r.drawText({tree_.x + 14.f, tree_.y + 12.f},
                           typeFilter_ ? "Rien de ce type ici : \xC2\xAB Tout montrer \xC2\xBB, ou tape un nom dans R\xC3\xA9sultat (il se cr\xC3\xA9" "e)."
                                       : "Rien ne correspond \xC3\xA0 la recherche.",
                           kSmall, c.textMuted);
            r.popClip();
            // 1.11.4 : la barre se tire.
            sbar_.paint(ctx, tree_, static_cast<float>(rows_.size()) * kRow, tree_.h, scroll_);
        }
        // ---- le detail ----
        paintDetail(ctx);
        // ---- le resultat ----
        r.fillRect({panel_.x, resultTop_, panel_.w, 1.f}, c.border);
        r.drawText({panel_.x + 16.f, resultTop_ + 13.f}, "R\xC3\xA9sultat", kBody, c.text);
        // Le bouton fx : plein (formule) ou en creux (constante).
        if (fx_) {
            r.fillRoundedRect(fxBox_, c.accent, 8.f);
            r.drawText({fxBox_.x + 9.f, fxBox_.y + (fxBox_.h - r.lineHeight(kBody)) * 0.5f}, "fx", kBody, c.selectionText);
        } else {
            r.fillRoundedRect(fxBox_, c.textMuted, 8.f);
            r.fillRoundedRect({fxBox_.x + 1.f, fxBox_.y + 1.f, fxBox_.w - 2.f, fxBox_.h - 2.f}, c.panelBg, 7.f);
            r.drawText({fxBox_.x + 9.f, fxBox_.y + (fxBox_.h - r.lineHeight(kBody)) * 0.5f}, "fx", kBody, c.textMuted);
        }
        ui::paintLegend(ctx, legendBox_, valuekind::legendOf(res_));
        float y = resultTop_ + 44.f;
        const std::string info = valuekind::summary(res_);
        r.drawText({panel_.x + 16.f, y}, fit(r, info, kSmall, panel_.w - 32.f), kSmall, res_.error() ? c.error : c.textMuted);
        y += 22.f;
        // Les erreurs, et leurs corrections (un clic).
        fixRects_.clear();
        for (std::size_t d = 0; d < res_.diags.size() && d < 3; ++d) {
            const auto& dg = res_.diags[d];
            const gfx::Rect band{panel_.x + 16.f, y, panel_.w - 32.f, 30.f};
            r.fillRect(band, c.error.withAlpha(22));
            r.fillRect({band.x, band.y, 3.f, band.h}, c.error);
            float fx = band.x + 12.f;
            const std::string msg = fit(r, dg.message, kSmall, band.w * 0.45f);
            r.drawText({fx, band.y + (band.h - r.lineHeight(kSmall)) * 0.5f}, msg, kSmall, c.error);
            fx += r.measure(msg, kSmall).width + 14.f;
            for (std::size_t f = 0; f < dg.fixes.size() && f < 4; ++f) {
                const std::string label = fit(r, dg.fixes[f].label, kSmall, 260.f);
                const float lw = r.measure(label, kSmall).width + 16.f;
                if (fx + lw > band.right() - 6.f) break;
                const gfx::Rect chip{fx, band.y + 4.f, lw, 22.f};
                r.fillRoundedRect(chip, c.headerBg, 5.f);
                r.strokeRect(chip, c.border, 1.f);
                r.drawText({chip.x + 8.f, chip.y + (chip.h - r.lineHeight(kSmall)) * 0.5f}, label, kSmall, c.text);
                fixRects_.push_back({chip, {d, f}});
                fx += lw + 6.f;
            }
            y += 34.f;
        }
        if (confirm_) {
            const std::string msg = "La valeur a encore une erreur.";
            r.drawText({panel_.x + 16.f, y + 4.f}, msg, kSmall, c.error);
            const float mx = panel_.x + 26.f + r.measure(msg, kSmall).width;
            const std::string link = "Valider quand m\xC3\xAAme";
            force_ = {mx, y + 1.f, r.measure(link, kSmall).width + 16.f, 22.f};
            r.fillRoundedRect(force_, c.headerBg, 5.f);
            r.strokeRect(force_, c.error, 1.f);
            r.drawText({force_.x + 8.f, force_.y + 3.f}, link, kSmall, c.text);
        } else {
            force_ = {};
        }
        // ---- le pied : le repere, l'aide ----
        r.fillRect({panel_.x, bottom_, panel_.w, 1.f}, c.border);
        const std::string rep = "Ins\xC3\xA9rer un rep\xC3\xA8re $\xE2\x80\xA6$";
        repBox_ = {panel_.x + 16.f, bottom_ + 14.f, r.measure(rep, kSmall).width + 18.f, 24.f};
        r.fillRoundedRect(repBox_, c.headerBg, 5.f);
        r.strokeRect(repBox_, c.border, 1.f);
        r.drawText({repBox_.x + 9.f, repBox_.y + 4.f}, rep, kSmall, c.text);
        const std::string help = "Clic : dans R\xC3\xA9sultat \xC2\xB7 double-clic : prendre et valider \xC2\xB7 Ctrl+Entr\xC3\xA9" "e : valider";
        r.drawText({repBox_.right() + 14.f, bottom_ + 18.f}, fit(r, help, kSmall, cancel_->bounds().x - repBox_.right() - 28.f), kSmall,
                   c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        {
            float off = scroll_;   // 1.11.4 : la barre de defilement se tire
            if (sbar_.handle(*this, ev, off)) {
                scroll_ = std::clamp(off, 0.f, maxScroll());
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            int h = -1;
            for (std::size_t k = 0; k < rowRects_.size(); ++k)
                if (rowRects_[k].first.contains(m->pos)) h = static_cast<int>(k + static_cast<std::size_t>(std::max(0.f, scroll_ / kRow)));
            const bool hc = close_.contains(m->pos);
            if (h != hover_ || hc != hoverClose_) {
                hover_ = h;
                hoverClose_ = hc;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!tree_.contains(w->pos)) return ui::EventResult::Ignored;
            scroll_ = std::clamp(scroll_ - w->dy * kRow * 3.f, 0.f, maxScroll());
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (d->button != ui::MouseButton::Left) return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
            if (close_.contains(d->pos)) { owner_.finish(false); return ui::EventResult::Consumed; }
            for (const auto& [rect, s] : chips_)
                if (rect.contains(d->pos)) { setSource(s); return ui::EventResult::Consumed; }
            if (source_ != Style::Constant && (gfx::Rect{typeBox_.x - 2.f, typeBox_.y - 2.f, 200.f, 20.f}.contains(d->pos) || allLink_.contains(d->pos))) {
                setTypeFilter(!typeFilter_);
                return ui::EventResult::Consumed;
            }
            if (fxBox_.contains(d->pos)) {
                fx_ = !fx_;
                confirm_ = false;
                classify();
                owner_.focus().focus(result_);
                return ui::EventResult::Consumed;
            }
            if (repBox_.contains(d->pos)) {
                use(Use::Marker);
                return ui::EventResult::Consumed;
            }
            if (force_.w > 0.f && force_.contains(d->pos)) {
                owner_.validate(true);
                return ui::EventResult::Consumed;
            }
            for (const auto& [rect, idx] : fixRects_)
                if (rect.contains(d->pos)) {
                    (void)applyFix(idx.first, idx.second);
                    return ui::EventResult::Consumed;
                }
            for (const auto& [rect, act] : actRects_)
                if (rect.contains(d->pos)) {
                    use(act);
                    return ui::EventResult::Consumed;
                }
            for (const auto& [rect, value] : constRects_)
                if (rect.contains(d->pos)) {
                    setResult(value.first, value.second);
                    return ui::EventResult::Consumed;
                }
            for (const auto& [rect, node] : rowRects_) {
                if (!rect.contains(d->pos)) continue;
                const Node& nd = nodes_[static_cast<std::size_t>(node)];
                const bool onChevron = d->pos.x < rect.x + 22.f + static_cast<float>(depthOf(node)) * 16.f;
                if (nd.group || onChevron || (d->clickCount < 2 && !nd.children.empty() && onChevron)) {
                    toggle(node);
                    return ui::EventResult::Consumed;
                }
                sel_ = node;
                if (d->clickCount >= 2) {
                    use(Use::Replace);
                    owner_.validate(false);
                    return ui::EventResult::Consumed;
                }
                // Un clic : dans Resultat, s'il est vide ou n'est qu'une variable (sinon : le detail dit quoi faire).
                const std::string cur = result_->text();
                if (cur.empty() || hmi::isVariablePath(cur) || res_.style == Style::Error) use(Use::Replace);
                invalidate();
                return ui::EventResult::Consumed;
            }
            return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        }
        return ui::EventResult::Ignored;
    }

private:
    enum class Use : std::uint8_t { Replace, Insert, Marker, Convert };
    struct Row {
        int  node;
        int  depth;
        bool open;
        bool match;
        int  leaves;
    };

    [[nodiscard]] int depthOf(int node) const {
        int d = 0;
        for (int p = nodes_[static_cast<std::size_t>(node)].parent; p >= 0; p = nodes_[static_cast<std::size_t>(p)].parent) ++d;
        return d;
    }

    // Les lignes montrees : la source, le type attendu, la recherche ; un noeud reste si lui
    // ou l'un de ses descendants convient.
    void rebuild() {
        const std::string q = lower(search_ ? search_->text() : std::string{});
        matches_.assign(nodes_.size(), false);
        keep_.assign(nodes_.size(), false);
        leaves_.assign(nodes_.size(), 0);
        total_ = 0;
        shown_ = 0;
        for (std::size_t i = nodes_.size(); i-- > 0;) {
            const Node& nd = nodes_[i];
            Style zone = nd.zone;
            if (source_ != Style::Empty && zone != source_) continue;
            if (!nd.group) {
                ++total_;
                const bool okType = !typeFilter_ || valuekind::compatible(spec_.expected, nd.type);
                const bool okText = q.empty() || lower(nd.path).find(q) != std::string::npos;
                matches_[i] = okType && okText;
                if (matches_[i]) ++shown_;
            }
            bool child = false;
            for (const int k : nd.children) {
                if (keep_[static_cast<std::size_t>(k)]) child = true;
                leaves_[i] += leaves_[static_cast<std::size_t>(k)] + (nodes_[static_cast<std::size_t>(k)].group ? 0 : 1);
            }
            keep_[i] = matches_[i] || child;
        }
        rows_.clear();
        const bool filtering = !q.empty() || typeFilter_;
        const std::function<void(int, int)> walk = [&](int i, int depth) {
            const auto idx = static_cast<std::size_t>(i);
            if (!keep_[idx]) return;
            const Node& nd = nodes_[idx];
            bool open = false;
            if (shut_.count(i)) open = false;
            else if (opened_.count(i)) open = true;
            else if (nd.group) open = nd.parent < 0 || filtering;
            else open = filtering && depth < 4 && std::any_of(nd.children.begin(), nd.children.end(), [&](int k) {
                            return keep_[static_cast<std::size_t>(k)];
                        });
            rows_.push_back({i, depth, open, matches_[idx], leaves_[idx]});
            if (open)
                for (const int k : nd.children) walk(k, depth + 1);
        };
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            if (nodes_[i].parent < 0) walk(static_cast<int>(i), 0);
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
        invalidate();
    }

    void toggle(int node) {
        const bool open = std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.node == node && r.open; });
        if (open) {
            shut_.insert(node);
            opened_.erase(node);
        } else {
            opened_.insert(node);
            shut_.erase(node);
        }
        rebuild();
    }

    void classify() {
        res_ = valuekind::classify(env_, result_->text(), fx_, spec_.expected);
        invalidate();
    }

    void use(Use how) {
        const Node* nd = sel_ >= 0 ? &nodes_[static_cast<std::size_t>(sel_)] : nullptr;
        std::string cur = result_->text();
        if (how == Use::Marker) {
            // Le repere : la variable choisie entre $, ou une paire de $ a completer.
            const std::string ins = nd && !nd->group ? "$" + nd->path + "$" : std::string("$$");
            setResult(cur + ins, fx_);
            owner_.focus().focus(result_);
            return;
        }
        if (!nd || nd->group) return;
        if (how == Use::Replace) setResult(nd->path, true);
        else if (how == Use::Insert) setResult(cur + (cur.empty() || cur.back() == ' ' || cur.back() == '(' ? "" : " ") + nd->path, true);
        else if (how == Use::Convert) {
            valuekind::Result one = valuekind::classify(env_, nd->path, true, spec_.expected);
            for (const auto& dg : one.diags)
                for (const auto& f : dg.fixes)
                    if (f.create.empty() && f.fx && f.value != nd->path) { setResult(f.value, true); return; }
            setResult(nd->path, true);
        }
    }

    void paintDetail(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(detail_, c.windowBg);
        r.strokeRect(detail_, c.border, 1.f);
        actRects_.clear();
        float y = detail_.y + 12.f;
        const float x = detail_.x + 12.f, w = detail_.w - 24.f;
        const auto line = [&](const std::string& label, const std::string& value) {
            if (value.empty()) return;
            r.drawText({x, y}, label, kSmall, c.textMuted);
            r.drawText({x + 92.f, y}, fit(r, value, kSmall, w - 92.f), kSmall, c.text);
            y += 20.f;
        };
        const Node* nd = sel_ >= 0 ? &nodes_[static_cast<std::size_t>(sel_)] : nullptr;
        if (source_ == Style::Constant) {
            r.drawText({x, y}, "Constante", kBody, c.text);
            y += 26.f;
            r.drawText({x, y}, fit(r, "Une valeur fixe, convertie en " + valuekind::expectedLabel(spec_.expected) + ".", kSmall, w), kSmall, c.textMuted);
            y += 20.f;
            r.drawText({x, y}, fit(r, "Sans fx : Voiture devient 'Voiture' pour un texte.", kSmall, w), kSmall, c.textMuted);
            return;
        }
        if (!nd || nd->group) {
            r.drawText({x, y}, fit(r, "Choisis une variable dans l'arbre : son type,", kSmall, w), kSmall, c.textMuted);
            y += 20.f;
            r.drawText({x, y}, fit(r, "son adresse, son commentaire s'affichent ici.", kSmall, w), kSmall, c.textMuted);
            y += 30.f;
            r.drawText({x, y}, fit(r, "Le filtre Type attendu ne montre que ce qui convient ;", kSmall, w), kSmall, c.textMuted);
            y += 20.f;
            r.drawText({x, y}, fit(r, "\xC2\xAB Tout montrer \xC2\xBB : API, SYS., les variables IHM\xE2\x80\xA6", kSmall, w), kSmall, c.textMuted);
            return;
        }
        ui::paintLegend(ctx, {x, y, 20.f, 20.f}, valuekind::legendOf(nd->zone));
        r.drawText({x + 28.f, y + 1.f}, fit(r, nd->path, kMono, w - 28.f), kMono, c.text);
        y += 30.f;
        line("Zone", zoneLong(nd->zone));
        line("Type", nd->type);
        line("D\xC3\xA9tail", nd->detail);
        y += 6.f;
        const bool ok = valuekind::compatible(spec_.expected, nd->type);
        const std::string verdict = ok ? "\xE2\x9C\x93 Convient \xC3\xA0 " + valuekind::expectedLabel(spec_.expected) + "."
                                       : nd->type + " ne convient pas \xC3\xA0 " + valuekind::expectedLabel(spec_.expected) + ".";
        r.drawText({x, y}, fit(r, verdict, kSmall, w), kSmall, ok ? ui::legendColor(Style::Hmi, ctx.theme.isDark()) : c.error);
        y += 30.f;
        const auto button = [&](const std::string& label, Use act, bool primary) {
            const float bw = std::min(w, r.measure(label, kSmall).width + 18.f);
            const gfx::Rect b{x, y, bw, 26.f};
            r.fillRoundedRect(b, primary ? c.accent : c.headerBg, 5.f);
            if (!primary) r.strokeRect(b, c.border, 1.f);
            r.drawText({b.x + 9.f, b.y + (b.h - r.lineHeight(kSmall)) * 0.5f}, fit(r, label, kSmall, bw - 14.f), kSmall,
                       primary ? c.selectionText : c.text);
            actRects_.push_back({b, act});
            y += 32.f;
        };
        button("Remplacer le r\xC3\xA9sultat", Use::Replace, true);
        button("Ins\xC3\xA9rer \xC3\xA0 la suite", Use::Insert, false);
        button("Ins\xC3\xA9rer comme rep\xC3\xA8re $\xE2\x80\xA6$", Use::Marker, false);
        if (!ok) button("Convertir pour " + valuekind::expectedLabel(spec_.expected), Use::Convert, false);
    }

    void paintConstant(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        constRects_.clear();
        float y = tree_.y + 14.f;
        const float x = tree_.x + 14.f;
        const std::string e = spec_.expected;
        r.drawText({x, y}, fit(r, "Tape la valeur dans R\xC3\xA9sultat, sans fx : elle est convertie en " + valuekind::expectedLabel(e) + ".", kSmall,
                               tree_.w - 28.f),
                   kSmall, c.textMuted);
        y += 22.f;
        std::vector<std::pair<std::string, bool>> values;
        std::string u;
        for (const char ch : e) u += static_cast<char>(std::toupper(uc(ch)));
        if (u == "BOOL") values = {{"TRUE", false}, {"FALSE", false}};
        else if (u.rfind("ARRAY", 0) == 0) {
            r.drawText({x, y}, fit(r, "Un tableau ne se donne pas en constante : une variable du m\xC3\xAAme type, en fx.", kSmall, tree_.w - 28.f), kSmall,
                       c.textMuted);
            y += 24.f;
            for (const auto& nd : nodes_)
                if (!nd.group && valuekind::compatible(e, nd.type) && values.size() < 6) values.push_back({nd.path, true});
        } else if (env_.project) {
            for (const auto& t : env_.project->programs.types)
                if (t.kind == hmi::HmiTypeKind::Enumeration && t.name == e)
                    for (const auto& v : t.values) values.push_back({v.name, false});
        }
        float cx = x;
        for (const auto& [v, isFx] : values) {
            const float bw = r.measure(v, kSmall).width + 18.f;
            if (cx + bw > tree_.right() - 10.f) { cx = x; y += 30.f; }
            const gfx::Rect b{cx, y, bw, 24.f};
            r.fillRoundedRect(b, c.headerBg, 5.f);
            r.strokeRect(b, c.border, 1.f);
            r.drawText({b.x + 9.f, b.y + 4.f}, v, kSmall, c.text);
            constRects_.push_back({b, {v, isFx}});
            cx += bw + 6.f;
        }
    }

    [[nodiscard]] float maxScroll() const { return std::max(0.f, static_cast<float>(rows_.size()) * kRow - tree_.h); }

    HmiValuePicker&      owner_;
    const Spec&          spec_;
    valuekind::Env       env_;
    std::vector<Node>    nodes_;
    std::vector<bool>    matches_, keep_;
    std::vector<int>     leaves_;
    std::vector<Row>     rows_;
    std::set<int>        opened_, shut_;
    Style                source_{Style::Empty};
    bool                 typeFilter_{true};
    bool                 markers_{false};
    bool                 fx_{true};
    bool                 confirm_{false};
    int                  sel_{-1};
    int                  hover_{-1};
    bool                 hoverClose_{false};
    std::size_t          total_{0}, shown_{0};
    valuekind::Result    res_;
    ui::InputText*       search_{nullptr};
    ui::InputText*       result_{nullptr};
    ui::Button*          cancel_{nullptr};
    ui::Button*          ok_{nullptr};
    gfx::Rect            panel_{}, tree_{}, detail_{}, close_{}, typeBox_{}, allLink_{}, fxBox_{}, legendBox_{}, repBox_{}, force_{};
    float                resultTop_{0.f}, bottom_{0.f}, scroll_{0.f};
    ui::PaintedScrollBar sbar_;   // 1.11.4
    std::vector<std::pair<gfx::Rect, Style>>                                        chips_;
    std::vector<std::pair<gfx::Rect, int>>                                          rowRects_;
    std::vector<std::pair<gfx::Rect, std::pair<std::size_t, std::size_t>>>          fixRects_;
    std::vector<std::pair<gfx::Rect, Use>>                                          actRects_;
    std::vector<std::pair<gfx::Rect, std::pair<std::string, bool>>>                 constRects_;
    core::ConnectionScope links_;
};

// ============================================================== le dialogue ====
HmiValuePicker::HmiValuePicker(Spec spec) : menu::WidgetMenu("dialog.valuePicker"), spec_(std::move(spec)) {}
HmiValuePicker::~HmiValuePicker() = default;

menu::MenuTraits HmiValuePicker::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiValuePicker::title() const { return "Choisir la valeur de \xC2\xAB " + spec_.field + " \xC2\xBB"; }

core::Status HmiValuePicker::buildUi() {
    auto body = std::make_unique<Body>(*this, spec_);
    body_ = body.get();
    setRoot(std::move(body));
    return core::ok();
}

void HmiValuePicker::onEnter() {
    if (body_) body_->focusFirst();
}

ui::EventResult HmiValuePicker::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && body_) {
        if (k->key == ui::Key::Escape && !body_->suggestionsOpen()) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return && (k->mods.ctrl || (body_->resultFocused() && !body_->suggestionsOpen()))) {
            validate(false);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void HmiValuePicker::setSearch(const std::string& text) { if (body_) body_->setSearch(text); }
void HmiValuePicker::setTypeFilter(bool on) { if (body_) body_->setTypeFilter(on); }
void HmiValuePicker::setSource(Style s) { if (body_) body_->setSource(s); }
bool HmiValuePicker::pick(std::string_view path) { return body_ && body_->pick(path); }
void HmiValuePicker::setResult(const std::string& text, bool fx) { if (body_) body_->setResult(text, fx); }
std::string HmiValuePicker::result() const { return body_ ? body_->resultText() : std::string{}; }
bool HmiValuePicker::resultFx() const { return body_ && body_->fx(); }
std::size_t HmiValuePicker::shownCount() const { return body_ ? body_->shownCount() : 0; }
const valuekind::Result& HmiValuePicker::classification() const {
    static const valuekind::Result none;
    return body_ ? body_->res() : none;
}
bool HmiValuePicker::applyFix(std::size_t d, std::size_t f) { return body_ && body_->applyFix(d, f); }

void HmiValuePicker::validate(bool force) {
    if (!body_ || done_) return;
    const auto& res = body_->res();
    if (answer_.create.empty() && !res.unknown.empty() && !force) {
        answer_.create = res.unknown.front();   // un nom inconnu : on le cree (l'hote demande le type et la zone)
        finish(true);
        return;
    }
    if (res.error() && !force && answer_.create.empty()) {
        body_->setConfirming(true);
        return;
    }
    answer_.forced = force && res.error();
    finish(true);
}

void HmiValuePicker::finish(bool ok) {
    if (done_) return;
    done_ = true;
    const std::string payload = body_ ? body_->payload() : std::string{};
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload});
}

HmiValuePicker::Answer HmiValuePicker::parse(const std::string& payload) {
    Answer a;
    std::vector<std::string> parts{""};
    for (const char ch : payload) {
        if (ch == kSep) parts.emplace_back();
        else parts.back() += ch;
    }
    a.text = parts[0];
    a.fx = parts.size() > 1 ? parts[1] == "1" : true;
    a.create = parts.size() > 2 ? parts[2] : std::string{};
    a.forced = parts.size() > 3 && parts[3] == "1";
    return a;
}

} // namespace app
