// =============================================================================
//  app/hmi/HmiSimVarTree.cpp - 1.11.5 : les variables IHM et API de la simulation,
//  en arbre, avec le forcage (voir HmiSimVarTree.hpp)
// =============================================================================
#include "HmiSimVarTree.hpp"

#include "../../hmi/HmiExpr.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace app {

namespace {

constexpr float kBarH = 36.f;    // la recherche
constexpr float kHeadH = 24.f;   // les titres des colonnes
constexpr float kRowH = 24.f;
constexpr float kIndent = 14.f;

std::string trimmed(std::string s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string upperOf(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Les morceaux d'un chemin : Four1.Vannes[1].Position -> Four1, Vannes, [1], Position.
std::vector<std::string> pieces(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : path) {
        if (ch == '.') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (ch == '[') {
            if (!cur.empty()) out.push_back(cur);
            cur = "[";
        } else if (ch == ']') {
            cur += ']';
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    if (out.empty()) out.push_back(path);
    return out;
}

std::string fit(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(text, f).width <= w) return text;
    std::string t = text;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Le champ de la valeur : il prend le focus a l'ouverture ; Echap l'annule.
class ValueField final : public ui::InputText {
public:
    using ui::InputText::InputText;
    bool cancelled{false};
    void start() {
        cancelled = false;
        grabFocus();
        (void)ui::InputText::onEvent(ui::KeyDown{ui::Key::End, {}, false});
        (void)ui::InputText::onEvent(ui::KeyDown{ui::Key::Home, ui::KeyMods{false, true, false, false}, false});
    }
protected:
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && focused()) cancelled = true;
        return ui::InputText::onEvent(ev);
    }
};

} // namespace

std::optional<sim::Value> parseTypedValue(std::string text, const sim::Value* current) {
    text = trimmed(std::move(text));
    if (text.empty()) return std::nullopt;
    const auto u = upperOf(text);
    const auto type = current ? current->type() : sim::Type::Unknown;
    const bool yes = u == "TRUE" || u == "VRAI" || u == "ON" || u == "OUI";
    const bool no = u == "FALSE" || u == "FAUX" || u == "OFF" || u == "NON";
    if (type == sim::Type::Bool) {
        if (yes || u == "1") return sim::Value::boolean(true);
        if (no || u == "0") return sim::Value::boolean(false);
        return std::nullopt;
    }
    if (yes) return sim::Value::boolean(true);
    if (no) return sim::Value::boolean(false);
    if (u.rfind("T#", 0) == 0) return sim::Value::time(std::atoll(text.c_str() + 2));
    if (type == sim::Type::String || text.front() == '\'') {
        std::string s = text;
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
        return sim::Value::text(s);
    }
    std::string num = text;
    std::replace(num.begin(), num.end(), ',', '.');
    char* end = nullptr;
    const double d = std::strtod(num.c_str(), &end);
    if (!end || *end != '\0') return std::nullopt;
    if (type == sim::Type::Real || (type == sim::Type::Unknown && num.find('.') != std::string::npos)) return sim::Value::real(d);
    if (sim::isInteger(type)) return sim::Value::integer(type, static_cast<std::int64_t>(std::llround(d)));
    if (type == sim::Type::Time) return sim::Value::time(static_cast<std::int64_t>(std::llround(d)));
    return sim::Value::integer(sim::Type::DInt, static_cast<std::int64_t>(std::llround(d)));
}

HmiSimVarTree::HmiSimVarTree(std::string id, std::string what) : ui::Widget(std::move(id)), what_(std::move(what)) {
    const std::string base = this->id();
    search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".search")));
    search_->setPlaceholder("Chercher une variable " + what_ + " (nom, membre, type)");
    links_ += search_->textChanged->connect([this](const std::string& t) { setSearch(t); });
    auto field = std::make_unique<ValueField>(base + ".value");
    field->setVisibility(ui::Visibility::Collapsed);
    auto* raw = field.get();
    edit_ = &static_cast<ui::InputText&>(addChild(std::move(field)));
    links_ += raw->editingDone->connect([this, raw](const std::string&) {
        if (editPath_.empty()) return;   // deja ferme (Entree, puis la perte du focus)
        closeValueEditor(!raw->cancelled);
    });
}

void HmiSimVarTree::setLeaves(std::vector<Leaf> leaves) {
    nodes_.clear();
    roots_.clear();
    leaves_ = leaves.size();
    std::map<std::string, int> index;
    for (const auto& lf : leaves) {
        const auto ps = pieces(lf.path);
        std::string prefix;
        int parent = -1;
        std::vector<int> chain;
        for (std::size_t k = 0; k < ps.size(); ++k) {
            if (k > 0 && ps[k].front() != '[') prefix += '.';
            prefix += ps[k];
            auto it = index.find(prefix);
            if (it == index.end()) {
                Node n;
                n.label = ps[k];
                n.path = prefix;
                n.parent = parent;
                n.depth = static_cast<int>(k);
                n.leaf = k + 1 == ps.size();
                if (n.leaf) n.type = lf.type;
                else if (hooks_.nodeType) n.type = hooks_.nodeType(prefix);
                nodes_.push_back(std::move(n));
                const int at = static_cast<int>(nodes_.size()) - 1;
                if (parent >= 0) nodes_[static_cast<std::size_t>(parent)].children.push_back(at);
                else roots_.push_back(at);
                it = index.emplace(prefix, at).first;
            }
            parent = it->second;
            chain.push_back(parent);
        }
        for (const int c : chain) ++nodes_[static_cast<std::size_t>(c)].count;
    }
    rebuildRows();
}

void HmiSimVarTree::setSearch(const std::string& text) {
    if (query_ == text) return;
    query_ = text;
    if (search_ && search_->text() != text) search_->setText(text);
    scroll_ = 0.f;
    rebuildRows();
}

void HmiSimVarTree::setOpen(const std::string& path, bool open) {
    open_[path] = open;
    rebuildRows();
}

bool HmiSimVarTree::isOpen(const std::string& path) const {
    const auto it = open_.find(path);
    return it != open_.end() && it->second;
}

void HmiSimVarTree::rebuildRows() {
    rows_.clear();
    const ui::SearchQuery q(query_);
    std::vector<char> keep;
    const bool searching = !q.empty();
    if (searching) {
        keep.assign(nodes_.size(), 0);
        // Les noeuds sont crees avant leurs enfants : a rebours, un enfant garde son parent.
        for (std::size_t i = nodes_.size(); i-- > 0;) {
            const auto& n = nodes_[i];
            if (n.leaf && q.matches({n.path, n.type})) keep[i] = 1;
            if (keep[i] && n.parent >= 0) keep[static_cast<std::size_t>(n.parent)] = 1;
        }
    }
    const std::function<void(int)> walk = [&](int at) {
        const auto& n = nodes_[static_cast<std::size_t>(at)];
        if (searching && !keep[static_cast<std::size_t>(at)]) return;
        Row r;
        r.node = at;
        r.label = n.label;
        r.path = n.path;
        r.type = n.type;
        r.depth = n.depth;
        r.leaf = n.leaf;
        r.count = n.count;
        r.open = !n.leaf && (searching || isOpen(n.path));
        rows_.push_back(r);
        if (r.open)
            for (const int c : n.children) walk(c);
    };
    for (const int root : roots_) walk(root);
    invalidate();
}

gfx::Rect HmiSimVarTree::listArea() const {
    const auto b = bounds();
    return {b.x, b.y + kBarH + kHeadH, b.w, std::max(0.f, b.h - kBarH - kHeadH)};
}

int HmiSimVarTree::rowAt(gfx::Point p) const {
    const auto a = listArea();
    if (!a.contains(p)) return -1;
    const int i = static_cast<int>((p.y - a.y + scroll_) / kRowH);
    return i >= 0 && static_cast<std::size_t>(i) < rows_.size() ? i : -1;
}

std::string HmiSimVarTree::valueText(const std::string& path) const {
    if (hooks_.read)
        if (const auto v = hooks_.read(path)) return hmi::formatValue(*v);
    return "-";
}

bool HmiSimVarTree::rowRect(const std::string& path, gfx::Rect& out) const {
    const auto a = listArea();
    // La largeur des lignes dessinees : sans la place de la barre de defilement (comme
    // onPaint et onEvent - avant, la case Forcer d'une longue liste etait visee a cote).
    const float w = a.w - bar_.space(a, static_cast<float>(rows_.size()) * kRowH, a.h);
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].path == path) {
            out = {a.x, a.y + static_cast<float>(i) * kRowH - scroll_, w, kRowH};
            return out.y >= a.y - 0.5f && out.bottom() <= a.bottom() + 0.5f;
        }
    return false;
}

gfx::Rect HmiSimVarTree::forceBox(const gfx::Rect& row) const {
    const float x = row.x + row.w * 0.84f;
    return {x, row.y + (kRowH - 14.f) * 0.5f, 14.f, 14.f};
}

void HmiSimVarTree::onLayout() {
    const auto b = bounds();
    // La place du compte a droite de la recherche (« 86 sur 228 variables · 2 forcees »).
    if (search_) search_->setBounds({b.x + 8.f, b.y + 5.f, std::min(380.f, std::max(80.f, b.w - 16.f - 190.f)), 26.f});
    if (!editPath_.empty()) {
        gfx::Rect r{};
        if (rowRect(editPath_, r)) edit_->setBounds({r.x + r.w * 0.62f, r.y + 1.f, std::max(80.f, r.w * 0.2f), kRowH - 2.f});
        else closeValueEditor(false);
    }
}

bool HmiSimVarTree::forcePath(const std::string& path, const std::string& text) {
    if (!hooks_.force) return false;
    std::string why;
    if (!hooks_.force(path, text, &why)) {
        say(why.empty() ? path + " : for\xC3\xA7" "age refus\xC3\xA9" : why, true);
        return false;
    }
    std::string now;
    if (hooks_.read)
        if (const auto v = hooks_.read(path)) now = hmi::formatValue(*v);
    say(path + " forc\xC3\xA9" "e" + (now.empty() ? std::string{} : " \xC3\xA0 " + now) + " (d\xC3\xA9" "cocher Forcer la rend libre).", false);
    invalidate();
    return true;
}

bool HmiSimVarTree::unforcePath(const std::string& path) {
    if (!hooks_.unforce || !hooks_.unforce(path)) return false;
    say(path + " : libre (plus forc\xC3\xA9" "e).", false);
    invalidate();
    return true;
}

bool HmiSimVarTree::reveal(const std::string& path) {
    // Ses noeuds replies s'ouvrent d'abord (Armoires > [1] > ana > PT1).
    for (std::size_t k = 0; k < nodes_.size(); ++k) {
        if (nodes_[k].path != path) continue;
        bool changed = false;
        for (int p = nodes_[k].parent; p >= 0; p = nodes_[static_cast<std::size_t>(p)].parent)
            if (!isOpen(nodes_[static_cast<std::size_t>(p)].path)) {
                open_[nodes_[static_cast<std::size_t>(p)].path] = true;
                changed = true;
            }
        if (changed) rebuildRows();
        break;
    }
    const auto a = listArea();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].path == path) {
            const float top = static_cast<float>(i) * kRowH;
            if (top < scroll_ || top + kRowH > scroll_ + a.h) scroll_ = std::max(0.f, top - a.h * 0.4f);
            invalidate();
            return true;
        }
    return false;
}

bool HmiSimVarTree::openValueEditor(const std::string& path) {
    for (const auto& r : rows_)
        if (r.path == path && r.leaf) {
            gfx::Rect rr{};
            if (!rowRect(path, rr)) (void)reveal(path);
            if (!rowRect(path, rr)) return false;
            editPath_ = path;
            std::string now;
            if (hooks_.read)
                if (const auto v = hooks_.read(path)) now = hmi::formatValue(*v);
            edit_->setText(now);
            edit_->setPlaceholder("la valeur forc\xC3\xA9" "e");
            edit_->setBounds({rr.x + rr.w * 0.62f, rr.y + 1.f, std::max(80.f, rr.w * 0.2f), kRowH - 2.f});
            edit_->setVisibility(ui::Visibility::Visible);
            static_cast<ValueField*>(edit_)->start();
            invalidate();
            return true;
        }
    return false;
}

ui::InputText* HmiSimVarTree::valueEditor() noexcept { return editPath_.empty() ? nullptr : edit_; }

void HmiSimVarTree::closeValueEditor(bool apply) {
    if (editPath_.empty()) return;
    const std::string path = editPath_, text = trimmed(edit_->text());
    editPath_.clear();
    edit_->setVisibility(ui::Visibility::Collapsed);
    invalidate();
    if (apply && !text.empty()) (void)forcePath(path, text);
}

void HmiSimVarTree::onPaint(const ui::PaintContext& ctx) {
    auto& g = ctx.r;
    const auto& th = ctx.theme;
    const auto b = bounds();
    const auto kSmall = th.font.smallUi;
    const auto kUi = th.font.ui;
    g.fillRect(b, th.color.windowBg);
    // La barre de la recherche, et le compte.
    g.fillRect({b.x, b.y, b.w, kBarH}, th.color.panelBg);
    {
        std::size_t forced = 0;
        if (hooks_.forced)
            for (const auto& n : nodes_)
                if (n.leaf && hooks_.forced(n.path)) ++forced;
        std::string t = std::to_string(leaves_) + (leaves_ > 1 ? " variables" : " variable");
        if (!query_.empty()) {
            std::size_t shown = 0;
            for (const auto& r : rows_) shown += r.leaf ? 1 : 0;
            t = std::to_string(shown) + " sur " + t;
        }
        if (forced) t += " \xC2\xB7 " + std::to_string(forced) + (forced > 1 ? " forc\xC3\xA9" "es" : " forc\xC3\xA9" "e");
        const float sx = search_ ? search_->bounds().right() + 12.f : b.x + 8.f;
        g.drawText({sx, b.y + (kBarH - g.lineHeight(kSmall)) * 0.5f}, fit(g, t, kSmall, b.right() - sx - 8.f), kSmall,
                   forced ? th.color.warning : th.color.textMuted);
    }
    const auto a = listArea();
    const float content = static_cast<float>(rows_.size()) * kRowH;
    const float space = bar_.space(a, content, a.h);
    const float w = a.w - space;
    const float xType = a.x + w * 0.46f, xValue = a.x + w * 0.62f, xForce = a.x + w * 0.84f;
    // Les titres.
    {
        const float hy = b.y + kBarH;
        g.fillRect({b.x, hy, b.w, kHeadH}, th.color.headerBg);
        const float ty = hy + (kHeadH - g.lineHeight(kSmall)) * 0.5f;
        // Chaque titre tient dans sa colonne (le volet peut etre etroit).
        g.drawText({a.x + 10.f, ty}, fit(g, "Variable " + what_, kSmall, xType - a.x - 16.f), kSmall, th.color.textMuted);
        g.drawText({xType, ty}, fit(g, "Type", kSmall, xValue - xType - 6.f), kSmall, th.color.textMuted);
        g.drawText({xValue, ty}, fit(g, "Valeur (double-clic : forcer \xC3\xA0)", kSmall, xForce - xValue - 8.f), kSmall,
                   th.color.textMuted);
        g.drawText({xForce, ty}, fit(g, "Forcer", kSmall, a.x + w - xForce - 4.f), kSmall, th.color.textMuted);
    }
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content - a.h));
    g.pushClip(a);
    if (rows_.empty()) {
        const std::string t = !query_.empty() ? std::string("Rien ne correspond \xC3\xA0 la recherche.") : emptyText_;
        g.drawText({a.x + 14.f, a.y + 12.f}, fit(g, t, kUi, a.w - 28.f), kUi, th.color.textMuted);
    }
    const auto first = static_cast<std::size_t>(std::max(0.f, scroll_ / kRowH));
    for (std::size_t i = first; i < rows_.size(); ++i) {
        const gfx::Rect r{a.x, a.y + static_cast<float>(i) * kRowH - scroll_, w, kRowH};
        if (r.y > a.bottom()) break;
        const auto& row = rows_[i];
        if (static_cast<int>(i) == hover_) g.fillRect(r, th.brand.hover);
        else if (!row.leaf) g.fillRect(r, th.color.headerBg.withAlpha(90));
        g.line({r.x, r.bottom() - 0.5f}, {r.right(), r.bottom() - 0.5f}, th.color.border.withAlpha(90), 1.f);
        const float indent = std::min(static_cast<float>(row.depth) * kIndent, w * 0.25f);
        const float ax = r.x + 8.f + indent;
        const float ty = r.y + (kRowH - g.lineHeight(kSmall)) * 0.5f;
        if (!row.leaf) {
            const float ay = r.y + (kRowH - 12.f) * 0.5f;
            if (row.open) {
                g.line({ax, ay + 3}, {ax + 5, ay + 9}, th.color.text, 1.4f);
                g.line({ax + 5, ay + 9}, {ax + 10, ay + 3}, th.color.text, 1.4f);
            } else {
                g.line({ax + 2, ay + 1}, {ax + 8, ay + 6}, th.color.text, 1.4f);
                g.line({ax + 8, ay + 6}, {ax + 2, ay + 11}, th.color.text, 1.4f);
            }
            g.drawText({ax + 16.f, ty}, fit(g, row.label, th.font.uiBold, xType - ax - 22.f), th.font.uiBold, th.color.text);
            g.drawText({xType, ty}, fit(g, row.type, kSmall, xValue - xType - 8.f), kSmall, th.color.textMuted);
            g.drawText({xValue, ty}, std::to_string(row.count) + (row.count > 1 ? " valeurs" : " valeur"), kSmall, th.color.textMuted);
            continue;
        }
        const bool forced = hooks_.forced && hooks_.forced(row.path);
        g.drawText({ax + 16.f, ty}, fit(g, row.label, kUi, xType - ax - 22.f), kUi, th.color.text);
        g.drawText({xType, ty}, fit(g, row.type, kSmall, xValue - xType - 8.f), kSmall, th.color.textMuted);
        if (editPath_ != row.path) {
            std::string v = "-";
            if (hooks_.read)
                if (const auto val = hooks_.read(row.path)) v = hmi::formatValue(*val);
            if (forced) v += "  (forc\xC3\xA9" "e)";
            g.drawText({xValue, ty}, fit(g, v, kSmall, xForce - xValue - 8.f), kSmall, forced ? th.color.warning : th.color.text);
        }
        const gfx::Rect box = forceBox(r);
        g.fillRect(box, th.color.inputBg);
        g.strokeRect(box, forced ? th.color.warning : th.color.borderStrong, 1.f);
        if (forced) {
            g.line({box.x + 3, box.y + 7}, {box.x + 6, box.y + 10}, th.color.warning, 1.8f);
            g.line({box.x + 6, box.y + 10}, {box.x + 11, box.y + 4}, th.color.warning, 1.8f);
        }
    }
    g.popClip();
    bar_.paint(ctx, a, content, a.h, scroll_);
}

ui::EventResult HmiSimVarTree::onEvent(const ui::InputEvent& ev) {
    const auto a = listArea();
    {
        float off = scroll_;
        if (bar_.handle(*this, ev, a, static_cast<float>(rows_.size()) * kRowH, a.h, off)) {
            scroll_ = off;
            invalidateLayout();
            return ui::EventResult::Consumed;
        }
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (!a.contains(w->pos)) return ui::EventResult::Ignored;
        scroll_ = std::clamp(scroll_ - w->dy * kRowH * 3.f, 0.f, std::max(0.f, static_cast<float>(rows_.size()) * kRowH - a.h));
        invalidateLayout();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        const int i = rowAt(d->pos);
        if (i < 0) return ui::EventResult::Ignored;
        const Row row = rows_[static_cast<std::size_t>(i)];
        if (!row.leaf) {
            setOpen(row.path, !row.open);
            return ui::EventResult::Consumed;
        }
        const float w = a.w - bar_.space(a, static_cast<float>(rows_.size()) * kRowH, a.h);
        const gfx::Rect r{a.x, a.y + static_cast<float>(i) * kRowH - scroll_, w, kRowH};
        if (forceBox(r).contains(d->pos)) {
            const bool forced = hooks_.forced && hooks_.forced(row.path);
            if (forced) (void)unforcePath(row.path);
            else (void)forcePath(row.path);
            return ui::EventResult::Consumed;
        }
        if (d->clickCount >= 2 && d->pos.x >= r.x + w * 0.62f) {
            (void)openValueEditor(row.path);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
