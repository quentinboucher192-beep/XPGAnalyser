// =============================================================================
//  app/hmi/HmiSimVarTree.cpp - 1.11.5 : les variables IHM et API de la simulation,
//  en arbre, avec le forcage (voir HmiSimVarTree.hpp)
// =============================================================================
#include "HmiSimVarTree.hpp"
#include "../../sim/ValueText.hpp"   // 1.12.19 : la lecture d'une valeur tapee, partout la meme
#include "../../hmi/HmiSymbols.hpp"  // 1.12.19 : shownLiteral (la valeur forcee, telle qu'elle a ete lue)

#include "../../ui/Commands.hpp"

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
// Les colonnes, en part de la largeur des lignes (1.11.6 : la colonne Mouvement).
constexpr float kColType = 0.40f, kColValue = 0.52f, kColMotion = 0.69f, kColForce = 0.885f;

std::string trimmed(std::string s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
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

// 1.12.19 (la regle des cases) : la lecture commune (sim/ValueText) - avant, T#5s valait 5 ms,
// 12.7 s'arrondissait et 40000 se repliait en -25536 dans un INT, sans rien dire.
std::optional<sim::Value> parseTypedValue(const std::string& text, const sim::Value* current, std::string* why) {
    sim::Value v;
    std::string bad;
    if (!sim::readTyped(text, current ? current->type() : sim::Type::Unknown, v, bad)) {
        if (why) *why = bad;
        return std::nullopt;
    }
    return v;
}

HmiSimVarTree::HmiSimVarTree(std::string id, std::string what) : ui::Widget(std::move(id)), what_(std::move(what)) {
    setZoomSupport(ui::ZoomSupport::Scaled);   // 1.12.30 : le zoom de la page
    setFocusPolicy(true);   // 1.12.18 : Entree, Suppr, Ctrl+C sur la ligne courante
    const std::string base = this->id();
    search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>(base + ".search")));
    search_->setPlaceholder("Chercher une variable " + what_ + " (nom, membre, type)");
    links_ += search_->textChanged->connect([this](const std::string& t) { setSearch(t); });
    // 1.11.6 : sur la vue actuelle.
    viewBox_ = &static_cast<ui::Checkbox&>(addChild(std::make_unique<ui::Checkbox>("Sur la vue actuelle", base + ".view")));
    viewBox_->setTooltip("Seulement les variables " + what_ + " que lit la vue montr\xC3\xA9" "e (et ses popups ouvertes) : "
                         "ses objets, ses textes, ses actions, ses scripts, et ceux de ses symboles, \xC3\xA0 toute profondeur.");
    links_ += viewBox_->stateChanged->connect([this](ui::Checkbox::State st) { setOnlyView(st == ui::Checkbox::State::Checked); });
    // 1.11.6 : le menu du clic droit.
    ctxMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".context")));
    links_ += ctxMenu_->itemChosen->connect([this](int item) { (void)contextAction(ctxPath_, item); });
    // 1.11.6 : le choix du mouvement (la cellule Mouvement).
    motionMenu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".motion")));
    links_ += motionMenu_->itemChosen->connect([this](int item) { motionChosen(motionPath_, item); });
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

void HmiSimVarTree::setViewFilter(std::function<bool(std::string_view)> covers, std::string viewName) {
    covers_ = std::move(covers);
    viewName_ = std::move(viewName);
    if (onlyView_) {
        scroll_ = 0.f;
        rebuildRows();
    }
    invalidate();
}

bool HmiSimVarTree::shownLeaf(const Node& n) const {
    if (!n.leaf) return false;
    if (!query_.empty() && !ui::SearchQuery(query_).matches({n.path, n.type})) return false;
    if (onlyView_ && covers_ && !covers_(n.path)) return false;
    return true;
}

std::vector<std::string> HmiSimVarTree::leavesUnder(const std::string& path) const {
    std::vector<std::string> out;
    int at = -1;
    for (std::size_t k = 0; k < nodes_.size() && at < 0; ++k)
        if (nodes_[k].path == path) at = static_cast<int>(k);
    if (at < 0) return out;
    const std::function<void(int)> walk = [&](int i) {
        const auto& n = nodes_[static_cast<std::size_t>(i)];
        if (n.leaf) {
            if (shownLeaf(n)) out.push_back(n.path);
            return;
        }
        for (const int c : n.children) walk(c);
    };
    walk(at);
    return out;
}

void HmiSimVarTree::setAllOpen(bool open) {
    for (const auto& n : nodes_)
        if (!n.leaf) open_[n.path] = open;
    rebuildRows();
}

void HmiSimVarTree::motionChosen(const std::string& path, int item) {
    if (item == 0) (void)applyMotion(path, std::nullopt);
    else if (item == 100) (void)openMotionEditor(path);
    else if (item >= 1 && static_cast<std::size_t>(item) <= motionKinds_.size()) {
        if (setMotionKind(path, hmi::behaviorKindKey(motionKinds_[static_cast<std::size_t>(item - 1)]))) (void)openMotionEditor(path);
    }
}

bool HmiSimVarTree::contextAction(const std::string& path, int item) {
    const Node* node = nullptr;
    for (const auto& n : nodes_)
        if (n.path == path) node = &n;
    if (!node) return false;
    if (item >= kMotionBase && item < kMotionBase + 200) {   // 1.12.18 : Mouvement > ...
        motionKinds_ = hmi::motion::kindsFor(node->type == "BOOL");
        motionChosen(path, item - kMotionBase);
        return true;
    }
    switch (item) {
        case MForceTo: return openValueEditor(path);
        case MFollow: {
            std::string why;
            if (hooks_.follow && hooks_.follow(path, &why)) {
                say(path + " suivie dans Simulation \xE2\x80\xBA Courbes.", false);
                return true;
            }
            if (!why.empty()) say(why, true);
            return false;
        }
        case MCopyValue: {
            const std::string v = valueText(path);
            ui::setClipboardText(v);
            say("Valeur copi\xC3\xA9" "e : " + v, false);
            return true;
        }
        case MExpandAll: setAllOpen(true); return true;
        case MCollapseAll: setAllOpen(false); return true;
        case MExpand:
            if (node->leaf) return false;
            setOpen(path, true);
            return true;
        case MCollapse: {
            // Une variable : son noeud se replie.
            const Node* target = node;
            if (node->leaf) {
                if (node->parent < 0) return false;
                target = &nodes_[static_cast<std::size_t>(node->parent)];
            }
            const std::string p = target->path;
            setOpen(p, false);
            return true;
        }
        case MForce: case MUnforce: {
            const bool on = item == MForce;
            const auto leaves = leavesUnder(path);
            if (on && leaves.size() > kMaxForceAtOnce) {
                say(path + " : " + std::to_string(leaves.size()) + " valeurs, c'est trop d'un coup (" + std::to_string(kMaxForceAtOnce)
                        + " au plus) - forcez un n\xC5\x93ud plus petit.",
                    true);
                return false;
            }
            std::size_t done = 0;
            std::string why;
            for (const auto& lf : leaves) {
                const bool forced = hooks_.forced && hooks_.forced(lf);
                if (forced == on) continue;
                std::string w;
                const bool ok = on ? hooks_.force && hooks_.force(lf, {}, &w) : hooks_.unforce && hooks_.unforce(lf);
                if (ok) ++done;
                else if (why.empty()) why = w.empty() ? lf + " refus\xC3\xA9" "e" : w;
            }
            invalidate();
            if (node->leaf) {
                if (done) say(path + (on ? " forc\xC3\xA9" "e \xC3\xA0 sa valeur du moment." : " : libre (plus forc\xC3\xA9" "e)."), false);
                else if (!why.empty()) say(why, true);
                return done > 0;
            }
            std::string text = path + " : " + std::to_string(done) + (done > 1 ? " variables " : " variable ")
                             + (on ? (done > 1 ? "forc\xC3\xA9" "es \xC3\xA0 leur valeur du moment" : "forc\xC3\xA9" "e \xC3\xA0 sa valeur du moment")
                                   : (done > 1 ? "lib\xC3\xA9r\xC3\xA9" "es" : "lib\xC3\xA9r\xC3\xA9" "e"));
            if (!why.empty()) text += " (" + why + ")";
            say(text + ".", done == 0);
            return done > 0;
        }
        default: return false;
    }
}

bool HmiSimVarTree::openContextMenu(const std::string& path, gfx::Point at) {
    if (!ctxMenu_) return false;
    const Node* node = nullptr;
    for (const auto& n : nodes_)
        if (n.path == path) node = &n;
    if (!node) return false;
    const auto leaves = node->leaf ? std::vector<std::string>{path} : leavesUnder(path);
    std::size_t forced = 0;
    if (hooks_.forced)
        for (const auto& lf : leaves) forced += hooks_.forced(lf) ? 1 : 0;
    const std::size_t free = leaves.size() - forced;
    const bool open = !node->leaf && std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.path == path && r.open; });
    std::vector<ui::PopupMenu::Item> items;
    current_ = path;
    if (node->leaf) {
        // 1.12.18 (planche 16) : une variable - sa valeur en tete, forcer, relacher, le mouvement,
        // la courbe, copier.
        const bool forcedHere = forced > 0;
        items.push_back(ui::cmd::heading(path, valueText(path)));
        items.push_back(ui::cmd::item(ui::cmd::Id::Forcer, MForceTo, static_cast<bool>(hooks_.force), "pas de for\xC3\xA7" "age ici", "Forcer \xC3\xA0\xE2\x80\xA6"));
        ui::PopupMenu::Item here;
        here.label = "Forcer \xC3\xA0 sa valeur";
        here.id = MForce;
        here.enabled = !forcedHere && static_cast<bool>(hooks_.force);
        if (!here.enabled) here.disabledReason = forcedHere ? std::string("d\xC3\xA9j\xC3\xA0 forc\xC3\xA9" "e") : std::string("pas de for\xC3\xA7" "age ici");
        items.push_back(std::move(here));
        items.push_back(ui::cmd::item(ui::cmd::Id::Relacher, MUnforce, forcedHere && static_cast<bool>(hooks_.unforce), "pas forc\xC3\xA9" "e"));
        if (hooks_.setMotion) {
            const auto mo = hooks_.motion ? hooks_.motion(path) : std::nullopt;
            ui::PopupMenu::Item motion;
            motion.label = "Mouvement";
            ui::PopupMenu::Item none;
            none.label = "Aucun (libre)";
            none.id = kMotionBase;
            none.radio = true;
            none.checked = !mo.has_value();
            motion.children.push_back(std::move(none));
            const auto kinds = hmi::motion::kindsFor(node->type == "BOOL");
            for (std::size_t k = 0; k < kinds.size(); ++k) {
                ui::PopupMenu::Item it;
                it.label = std::string(hmi::behaviorKindLabel(kinds[k]));
                it.id = kMotionBase + static_cast<int>(k) + 1;
                it.radio = true;
                it.checked = mo && mo->kind == kinds[k];
                motion.children.push_back(std::move(it));
            }
            motion.children.push_back(ui::cmd::separator());
            ui::PopupMenu::Item limits;
            limits.label = "Les bornes\xE2\x80\xA6";
            limits.id = kMotionBase + 100;
            limits.enabled = mo.has_value();
            if (!limits.enabled) limits.disabledReason = "choisir d'abord un mouvement";
            motion.children.push_back(std::move(limits));
            items.push_back(std::move(motion));
        }
        if (hooks_.follow) {
            auto follow = ui::cmd::item(ui::cmd::Id::SuivreCourbe, MFollow);
            follow.note = "dans Simulation \xE2\x80\xBA Courbes";
            items.push_back(std::move(follow));
        }
        items.push_back(ui::cmd::item(ui::cmd::Id::CopierValeur, MCopyValue));
        items.push_back(ui::cmd::separator());
        items.push_back(ui::cmd::item(ui::cmd::Id::ToutDeplier, MExpandAll));
        ctxPath_ = path;
        ctxMenu_->setItems(std::move(items));
        auto* root = rootWidget();
        const auto sb = root ? root->bounds() : bounds();
        ctxMenu_->openAt(at, {sb.w, sb.h});
        return true;
    }
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = path;
    head.shortcut = std::to_string(leaves.size()) + (leaves.size() > 1 ? " valeurs" : " valeur");
    items.push_back(head);
    const auto item = [&](int id, std::string label, bool enabled, std::string why = {}) {
        ui::PopupMenu::Item it;
        it.id = id;
        it.label = std::move(label);
        it.enabled = enabled;
        it.disabledReason = std::move(why);
        items.push_back(std::move(it));
    };
    item(MExpandAll, "Tout d\xC3\xA9plier", true);
    item(MExpand, "D\xC3\xA9plier", !node->leaf && !open, node->leaf ? "une variable ne se d\xC3\xA9plie pas" : "d\xC3\xA9j\xC3\xA0 d\xC3\xA9pli\xC3\xA9");
    item(MCollapse, node->leaf ? "Replier (son n\xC5\x93ud)" : "Replier", node->leaf ? node->parent >= 0 : open,
         node->leaf ? "elle n'a pas de n\xC5\x93ud" : "d\xC3\xA9j\xC3\xA0 repli\xC3\xA9");
    item(MCollapseAll, "Tout replier", true);
    {
        ui::PopupMenu::Item sep;
        sep.separator = true;
        items.push_back(sep);
    }
    const bool tooMany = !node->leaf && free > kMaxForceAtOnce;
    item(MForce, node->leaf ? "Forcer (\xC3\xA0 sa valeur)" : "Forcer les " + std::to_string(free) + " libres (\xC3\xA0 leur valeur)",
         free > 0 && !tooMany && static_cast<bool>(hooks_.force),
         tooMany ? "trop de valeurs d'un coup : un n\xC5\x93ud plus petit" : "rien de libre \xC3\xA0 forcer");
    item(MUnforce, node->leaf ? "Rel\xC3\xA2" "cher" : "Rel\xC3\xA2" "cher les " + std::to_string(forced), forced > 0 && static_cast<bool>(hooks_.unforce),
         "rien de forc\xC3\xA9");
    ctxPath_ = path;
    ctxMenu_->setItems(std::move(items));
    auto* root = rootWidget();
    const auto sb = root ? root->bounds() : bounds();
    ctxMenu_->openAt(at, {sb.w, sb.h});
    return true;
}

void HmiSimVarTree::setOnlyView(bool on) {
    if (viewBox_ && viewBox_->isChecked() != on) viewBox_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    if (onlyView_ == on) return;
    onlyView_ = on;
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
    const bool viewOnly = onlyView_ && static_cast<bool>(covers_);      // 1.11.6
    const bool filtering = searching || viewOnly;
    if (filtering) {
        keep.assign(nodes_.size(), 0);
        // Les noeuds sont crees avant leurs enfants : a rebours, un enfant garde son parent.
        for (std::size_t i = nodes_.size(); i-- > 0;) {
            const auto& n = nodes_[i];
            if (n.leaf && (!searching || q.matches({n.path, n.type})) && (!viewOnly || covers_(n.path))) keep[i] = 1;
            if (keep[i] && n.parent >= 0) keep[static_cast<std::size_t>(n.parent)] = 1;
        }
    }
    // Sur la vue actuelle, les noeuds gardes s'ouvrent d'office (sauf replies a la main).
    const auto openNode = [&](const std::string& path) {
        if (searching) return true;
        if (!viewOnly) return isOpen(path);
        const auto it = open_.find(path);
        return it == open_.end() || it->second;
    };
    const std::function<void(int)> walk = [&](int at) {
        const auto& n = nodes_[static_cast<std::size_t>(at)];
        if (filtering && !keep[static_cast<std::size_t>(at)]) return;
        Row r;
        r.node = at;
        r.label = n.label;
        r.path = n.path;
        r.type = n.type;
        r.depth = n.depth;
        r.leaf = n.leaf;
        r.count = n.count;
        r.open = !n.leaf && openNode(n.path);
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
    const float x = row.x + row.w * kColForce;
    return {x, row.y + (kRowH - 14.f) * 0.5f, 14.f, 14.f};
}

void HmiSimVarTree::onLayout() {
    const auto b = bounds();
    // La place de la case « Sur la vue actuelle » et du compte a droite de la recherche.
    if (search_) search_->setBounds({b.x + 8.f, b.y + 5.f, std::min(340.f, std::max(80.f, b.w - 16.f - 190.f - 170.f)), 26.f});
    if (viewBox_ && search_) viewBox_->setBounds({search_->bounds().right() + 10.f, b.y + 7.f, 160.f, 22.f});
    if (ctxMenu_) ctxMenu_->setBounds(b);   // sinon il n'est jamais dessine (la passe du dessus)
    if (motionMenu_) motionMenu_->setBounds(b);
    if (!editPath_.empty()) {
        gfx::Rect r{};
        if (rowRect(editPath_, r)) edit_->setBounds(editRect(r));
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
    // 1.11.7 : la valeur tapee (forcee dans un esclave simule, la variable ne la relit qu'au cycle suivant).
    // 1.12.19 (la regle des cases) : telle qu'elle a ete LUE (2,5 -> 2.5, 5 s -> T#5s), pas le texte tape.
    std::string now = text;
    // Une variable objet (LIST, MAP...) est ecrite, pas tenue : la case Forcer reste decochee.
    const bool held = !(hooks_.rich && hooks_.rich(path));
    std::optional<sim::Value> cur;
    if (held && hooks_.read) cur = hooks_.read(path);
    if (held && !now.empty()) {
        if (const auto v = parseTypedValue(now, cur ? &*cur : nullptr, nullptr)) now = hmi::shownLiteral(sim::fixedText(*v));
    } else if (held && cur) {
        now = hmi::shownLiteral(sim::fixedText(*cur));
    }
    say(path + (held ? " forc\xC3\xA9" "e" : " \xC3\xA9" "crite") + (now.empty() ? std::string{} : " \xC3\xA0 " + now)
            + (held ? " (d\xC3\xA9" "cocher Forcer la rend libre)." : " (une variable objet n'est pas tenue : le programme peut la changer)."),
        false);
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
            editMotion_ = false;
            std::string now;
            if (hooks_.read)
                if (const auto v = hooks_.read(path)) now = hmi::formatValue(*v);
            edit_->setText(now);
            edit_->setPlaceholder("la valeur forc\xC3\xA9" "e");
            edit_->setBounds(editRect(rr));
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
    const bool motion = editMotion_;
    editPath_.clear();
    editMotion_ = false;
    edit_->setVisibility(ui::Visibility::Collapsed);
    invalidate();
    if (!apply || text.empty()) return;
    if (!motion) {
        (void)forcePath(path, text);
        return;
    }
    // 1.11.6 : les bornes du mouvement.
    auto mo = hooks_.motion ? hooks_.motion(path) : std::nullopt;
    if (!mo) return;
    std::string why;
    if (!hmi::motion::parse(text, *mo, &why)) {
        say(path + " : " + why + " - rien n'est chang\xC3\xA9.", true);
        return;
    }
    (void)applyMotion(path, mo);
}

gfx::Rect HmiSimVarTree::editRect(const gfx::Rect& row) const {
    if (editMotion_) return {row.x + row.w * kColMotion, row.y + 1.f, std::max(110.f, row.w * (kColForce - kColMotion) - 4.f), kRowH - 2.f};
    return {row.x + row.w * kColValue, row.y + 1.f, std::max(80.f, row.w * (kColMotion - kColValue) - 4.f), kRowH - 2.f};
}

// ------------------------------------------------- 1.11.6 : le mouvement d'une variable ---
bool HmiSimVarTree::applyMotion(const std::string& path, const std::optional<hmi::motion::Motion>& m) {
    if (!hooks_.setMotion) return false;
    std::string why;
    if (!hooks_.setMotion(path, m, &why)) {
        say(why.empty() ? path + " : mouvement refus\xC3\xA9" : why, true);
        return false;
    }
    say(m ? path + " : " + hmi::motion::text(*m) + " (forc\xC3\xA9" "e \xC3\xA0 chaque cycle ; d\xC3\xA9" "cocher Forcer l'arr\xC3\xAAte)."
          : path + " : le mouvement s'arr\xC3\xAAte, la variable est libre.",
        false);
    invalidate();
    return true;
}

bool HmiSimVarTree::setMotionKind(const std::string& path, std::string_view kind) {
    const Node* node = nullptr;
    for (const auto& n : nodes_)
        if (n.path == path && n.leaf) node = &n;
    if (!node || !hooks_.setMotion) return false;
    if (kind.empty() || kind == "aucun") return applyMotion(path, std::nullopt);
    const auto k = hmi::behaviorKindFrom(kind);
    if (!k) return false;
    const bool boolean = node->type == "BOOL";
    const auto allowed = hmi::motion::kindsFor(boolean);
    if (std::find(allowed.begin(), allowed.end(), *k) == allowed.end()) {
        say(path + " : \xC2\xAB " + std::string(hmi::behaviorKindLabel(*k)) + " \xC2\xBB ne va pas \xC3\xA0 un BOOL.", true);
        return false;
    }
    // Le meme genre : garder ses bornes ; un autre : autour de la valeur du moment.
    auto mo = hooks_.motion ? hooks_.motion(path) : std::nullopt;
    if (!mo || mo->kind != *k) {
        double current = 0;
        if (hooks_.read)
            if (const auto v = hooks_.read(path)) current = v->type() == sim::Type::Bool ? (v->isTruthy() ? 1 : 0) : v->asReal();
        mo = hmi::motion::defaultFor(*k, boolean, current);
    }
    return applyMotion(path, mo);
}

bool HmiSimVarTree::openMotionMenu(const std::string& path, gfx::Point at) {
    const Node* node = nullptr;
    for (const auto& n : nodes_)
        if (n.path == path && n.leaf) node = &n;
    if (!node || !motionMenu_) return false;
    const auto mo = hooks_.motion ? hooks_.motion(path) : std::nullopt;
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = path;
    head.shortcut = mo ? hmi::motion::text(*mo) : std::string("libre");
    items.push_back(head);
    ui::PopupMenu::Item none;
    none.label = "Aucun (libre)";
    none.id = 0;
    none.enabled = mo.has_value();
    none.disabledReason = "elle n'a pas de mouvement";
    none.radio = true;               // 1.12.18 : le mouvement en cours se coche (plus de « actuel » a droite)
    none.checked = !mo.has_value();
    items.push_back(none);
    motionKinds_ = hmi::motion::kindsFor(node->type == "BOOL");
    for (std::size_t k = 0; k < motionKinds_.size(); ++k) {
        ui::PopupMenu::Item it;
        it.label = std::string(hmi::behaviorKindLabel(motionKinds_[k]));
        it.radio = true;
        it.checked = mo && mo->kind == motionKinds_[k];
        it.id = static_cast<int>(k) + 1;
        items.push_back(it);
    }
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    ui::PopupMenu::Item limits;
    limits.label = "Les bornes\xE2\x80\xA6";
    limits.id = 100;
    limits.enabled = mo.has_value();
    limits.disabledReason = "choisir d'abord un mouvement";
    items.push_back(limits);
    motionPath_ = path;
    motionMenu_->setItems(std::move(items));
    auto* root = rootWidget();
    const auto sb = root ? root->bounds() : bounds();
    motionMenu_->openAt(at, {sb.w, sb.h});
    return true;
}

bool HmiSimVarTree::openMotionEditor(const std::string& path) {
    const auto mo = hooks_.motion ? hooks_.motion(path) : std::nullopt;
    if (!mo) {
        say(path + " : choisir d'abord un mouvement (un clic sur la cellule).", true);
        return false;
    }
    gfx::Rect rr{};
    if (!rowRect(path, rr)) (void)reveal(path);
    if (!rowRect(path, rr)) return false;
    editPath_ = path;
    editMotion_ = true;
    edit_->setText(hmi::motion::editText(*mo));
    edit_->setPlaceholder(mo->kind == hmi::BehaviorKind::Constant ? "la valeur"
                          : mo->kind == hmi::BehaviorKind::Blink  ? "\xC3\xA0 1 pendant ; p\xC3\xA9riode (s)"
                          : mo->kind == hmi::BehaviorKind::Steps  ? "1; 5; 3"
                          : mo->kind == hmi::BehaviorKind::Counter ? "d\xC3\xA9part ; pas ; p\xC3\xA9riode"
                                                                   : "min ; max ; p\xC3\xA9riode (s)");
    edit_->setBounds(editRect(rr));
    edit_->setVisibility(ui::Visibility::Visible);
    static_cast<ValueField*>(edit_)->start();
    invalidate();
    return true;
}

std::string HmiSimVarTree::motionText(const std::string& path) const {
    const auto mo = hooks_.motion ? hooks_.motion(path) : std::nullopt;
    return mo ? hmi::motion::text(*mo) : std::string{};
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
        if (!query_.empty() || (onlyView_ && covers_)) {
            std::size_t shown = 0;
            for (const auto& r : rows_) shown += r.leaf ? 1 : 0;
            t = std::to_string(shown) + " sur " + t;
        }
        if (forced) t += " \xC2\xB7 " + std::to_string(forced) + (forced > 1 ? " forc\xC3\xA9" "es" : " forc\xC3\xA9" "e");
        const float sx = viewBox_ ? viewBox_->bounds().right() + 8.f : search_ ? search_->bounds().right() + 12.f : b.x + 8.f;
        g.drawText({sx, b.y + (kBarH - g.lineHeight(kSmall)) * 0.5f}, fit(g, t, kSmall, b.right() - sx - 8.f), kSmall,
                   forced ? th.color.warning : th.color.textMuted);
    }
    const auto a = listArea();
    const float content = static_cast<float>(rows_.size()) * kRowH;
    const float space = bar_.space(a, content, a.h);
    const float w = a.w - space;
    const float xType = a.x + w * kColType, xValue = a.x + w * kColValue, xMotion = a.x + w * kColMotion, xForce = a.x + w * kColForce;
    // Les titres.
    {
        const float hy = b.y + kBarH;
        g.fillRect({b.x, hy, b.w, kHeadH}, th.color.headerBg);
        const float ty = hy + (kHeadH - g.lineHeight(kSmall)) * 0.5f;
        // Chaque titre tient dans sa colonne (le volet peut etre etroit).
        g.drawText({a.x + 10.f, ty}, fit(g, "Variable " + what_, kSmall, xType - a.x - 16.f), kSmall, th.color.textMuted);
        g.drawText({xType, ty}, fit(g, "Type", kSmall, xValue - xType - 6.f), kSmall, th.color.textMuted);
        g.drawText({xValue, ty}, fit(g, "Valeur (double-clic : forcer \xC3\xA0)", kSmall, xMotion - xValue - 8.f), kSmall,
                   th.color.textMuted);
        if (hooks_.setMotion)
            g.drawText({xMotion, ty}, fit(g, "Mouvement (clic : type, double-clic : bornes)", kSmall, xForce - xMotion - 8.f), kSmall,
                       th.color.textMuted);
        g.drawText({xForce, ty}, fit(g, "Forcer", kSmall, a.x + w - xForce - 4.f), kSmall, th.color.textMuted);
    }
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content - a.h));
    g.pushClip(a);
    if (rows_.empty()) {
        const std::string t = !query_.empty() ? std::string("Rien ne correspond \xC3\xA0 la recherche.")
                            : onlyView_ && covers_ && leaves_ > 0
                                ? "La vue " + (viewName_.empty() ? std::string("montr\xC3\xA9" "e") : viewName_) + " ne lit aucune variable " + what_ + "."
                                : emptyText_;
        g.drawText({a.x + 14.f, a.y + 12.f}, fit(g, t, kUi, a.w - 28.f), kUi, th.color.textMuted);
    }
    const auto first = static_cast<std::size_t>(std::max(0.f, scroll_ / kRowH));
    for (std::size_t i = first; i < rows_.size(); ++i) {
        const gfx::Rect r{a.x, a.y + static_cast<float>(i) * kRowH - scroll_, w, kRowH};
        if (r.y > a.bottom()) break;
        const auto& row = rows_[i];
        if (static_cast<int>(i) == hover_) g.fillRect(r, th.brand.hover);
        else if (!row.leaf) g.fillRect(r, th.color.headerBg.withAlpha(90));
        // 1.12.18 : la ligne courante (Entree, Suppr, Ctrl+C la visent), quand l'arbre a le clavier.
        if (focused() && !current_.empty() && row.path == current_) g.strokeRect(r, th.color.accent, 1.f);
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
            g.drawText({xValue, ty}, fit(g, v, kSmall, (hooks_.setMotion ? xMotion : xForce) - xValue - 8.f), kSmall,
                       forced ? th.color.warning : th.color.text);
        }
        // 1.11.6 : le mouvement (type, bornes) - "sinus 20 -> 80 · 10 s" ; sinon un tiret et la fleche du choix.
        if (hooks_.setMotion && !(editMotion_ && editPath_ == row.path)) {
            const auto mo = hooks_.motion ? hooks_.motion(row.path) : std::nullopt;
            const std::string t = mo ? hmi::motion::text(*mo) : std::string("\xE2\x80\x94");
            g.drawText({xMotion, ty}, fit(g, t, kSmall, xForce - xMotion - 22.f), kSmall, mo ? th.color.accent : th.color.textMuted);
            if (static_cast<int>(i) == hover_) {
                const float cx = xForce - 14.f, cy = r.y + kRowH * 0.5f;
                g.line({cx - 4, cy - 2}, {cx, cy + 2}, th.color.textMuted, 1.3f);
                g.line({cx, cy + 2}, {cx + 4, cy - 2}, th.color.textMuted, 1.3f);
            }
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
    // 1.12.18 (planche 16) : les touches du menu, sur la ligne courante.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && !current_.empty() && !k->repeat) {
        if (k->key == ui::Key::Return && k->mods.none()) return contextAction(current_, MForceTo) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        if (k->key == ui::Key::Delete && k->mods.none()) {
            (void)unforcePath(current_);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::C && k->mods.ctrl && !k->mods.shift && !k->mods.alt) {
            (void)contextAction(current_, MCopyValue);
            return ui::EventResult::Consumed;
        }
    }
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
    // 1.11.6 : le clic droit sur une ligne.
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Right) {
        const int i = rowAt(d->pos);
        if (i < 0) return ui::EventResult::Ignored;
        return openContextMenu(rows_[static_cast<std::size_t>(i)].path, d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left) {
        const int i = rowAt(d->pos);
        if (i < 0) return ui::EventResult::Ignored;
        const Row row = rows_[static_cast<std::size_t>(i)];
        current_ = row.path;   // 1.12.18 : le clavier vise cette ligne
        grabFocus();
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
        // 1.11.6 : la cellule du mouvement - un clic : le type ; un double-clic : les bornes.
        if (hooks_.setMotion && d->pos.x >= r.x + w * kColMotion && d->pos.x < r.x + w * kColForce - 2.f) {
            if (d->clickCount >= 2) (void)openMotionEditor(row.path);
            else (void)openMotionMenu(row.path, d->pos);
            return ui::EventResult::Consumed;
        }
        if (d->clickCount >= 2 && d->pos.x >= r.x + w * kColValue) {
            (void)openValueEditor(row.path);
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
