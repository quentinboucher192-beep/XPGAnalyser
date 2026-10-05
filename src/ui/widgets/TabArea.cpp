// =============================================================================
//  ui/widgets/TabArea.cpp - le centre en plusieurs groupes d'onglets (lot 7)
// =============================================================================
#include "TabArea.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ui {

namespace {
constexpr float kHandle     = 6.f;     // une poignee entre deux groupes
constexpr float kGrip       = 3.f;     // ... et ce qu'elle deborde pour se saisir sans viser
constexpr float kGap        = 6.f;     // entre deux tuiles de la mosaique
constexpr float kMinW       = 160.f;   // plus etroit, un groupe ne montre plus rien de lisible
constexpr float kMinH       = 110.f;
constexpr float kDragStart  = 6.f;     // un appui qui bouge moins reste un clic
constexpr float kDetachDist = 36.f;    // hors de la zone, assez loin pour que ce soit voulu
constexpr float kEdge       = 0.25f;   // la part d'une page qui fait un bord (le reste : le milieu)

// LES CASES DE LA MOSAIQUE, dans l'ordre des onglets. 1 : tout ; 2 : cote a
// cote ; 3 : un grand a gauche, deux empiles a droite ; puis une grille de
// ceil(sqrt(n)) colonnes (2 x 2, 3 x 2, 3 x 3...) dont la derniere ligne,
// incomplete, partage sa largeur. Une zone plus haute que large : la meme
// chose tournee d'un quart (2 l'un sur l'autre, 3 un grand en haut...).
std::vector<gfx::Rect> mosaicRects(std::size_t n, const gfx::Rect& a, float gap) {
    std::vector<gfx::Rect> out;
    if (n == 0) return out;
    if (a.h > a.w) {
        out = mosaicRects(n, gfx::Rect{a.y, a.x, a.h, a.w}, gap);
        for (auto& r : out) r = {r.y, r.x, r.h, r.w};
        return out;
    }
    const auto cell = [&out](float x, float y, float w, float h) {
        out.push_back({x, y, std::max(0.f, w), std::max(0.f, h)});
    };
    if (n == 1) {
        cell(a.x, a.y, a.w, a.h);
        return out;
    }
    const float half = std::floor((a.w - gap) * 0.5f);
    if (n == 2) {
        cell(a.x, a.y, half, a.h);
        cell(a.x + half + gap, a.y, a.w - half - gap, a.h);
        return out;
    }
    if (n == 3) {
        const float top = std::floor((a.h - gap) * 0.5f);
        cell(a.x, a.y, half, a.h);
        cell(a.x + half + gap, a.y, a.w - half - gap, top);
        cell(a.x + half + gap, a.y + top + gap, a.w - half - gap, a.h - top - gap);
        return out;
    }
    const auto cols = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(n))));
    const std::size_t rows = (n + cols - 1) / cols;
    const float rh = (a.h - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    std::size_t k = 0;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t inRow = std::min(cols, n - k);
        const float cw = (a.w - gap * static_cast<float>(inRow - 1)) / static_cast<float>(inRow);
        for (std::size_t col = 0; col < inRow; ++col, ++k)
            cell(a.x + static_cast<float>(col) * (cw + gap), a.y + static_cast<float>(row) * (rh + gap), cw, rh);
    }
    return out;
}

// Un cadre de `t` pixels, a l'interieur de `r` (le renderer trace des traits
// fins ; les bords d'une zone de depot se voient mieux en plein).
void frame(gfx::IRenderer& r, const gfx::Rect& b, float t, gfx::Color c) {
    r.fillRect({b.x, b.y, b.w, t}, c);
    r.fillRect({b.x, b.bottom() - t, b.w, t}, c);
    r.fillRect({b.x, b.y + t, t, std::max(0.f, b.h - 2.f * t)}, c);
    r.fillRect({b.right() - t, b.y + t, t, std::max(0.f, b.h - 2.f * t)}, c);
}
} // namespace

// =============================================================================
//  La couche du glisser : la derniere enfant, donc la premiere a voir la
//  souris. Transparente (ni survol ni infobulle) tant que rien n'est tire ;
//  des qu'un en-tete est appuye puis tire, ou une poignee saisie, elle prend
//  TOUTE la souris - la passe du dessus, comme une liste ouverte - jusqu'au
//  lacher, et peint les zones de depot au-dessus de tout.
// =============================================================================
class TabArea::Layer final : public Widget {
public:
    Layer(TabArea& owner, std::string id) : Widget(std::move(id)), owner_(owner) {}

    [[nodiscard]] bool requestsOverlayPass() const override { return owner_.capturing(); }
    [[nodiscard]] gfx::Rect eventBounds() const override {
        if (!owner_.capturing()) return bounds();
        constexpr float kFar = 1.0e6f;
        return {-kFar, -kFar, 2.f * kFar, 2.f * kFar};
    }
    // Un appui arme ne cache pas les infobulles du dessous ; un glisser, si.
    [[nodiscard]] bool overlayCovers(gfx::Point) const override {
        return owner_.dragging_ || owner_.resizing_ != nullptr;
    }

protected:
    [[nodiscard]] bool hitTest(gfx::Point) const override { return false; }
    EventResult onEvent(const InputEvent& ev) override { return owner_.layerEvent(ev); }
    void onPaintOverlay(const PaintContext& ctx) override {
        if (owner_.dragging_) owner_.paintDrag(ctx);
    }

private:
    TabArea& owner_;
};

// Un noeud de l'arbre des groupes : une feuille (un groupe) ou une division en
// deux, cote a cote ou l'une sur l'autre.
struct TabArea::Node {
    TabControl*           strip{nullptr};   // une feuille : sa bande
    std::unique_ptr<Node> first, second;    // une division : ses deux cotes
    Node*                 parent{nullptr};
    bool                  stacked{false};   // vrai : first au-dessus de second
    float                 ratio{0.5f};      // la part de first
    gfx::Rect             rect{}, handle{};
    [[nodiscard]] bool leaf() const noexcept { return strip != nullptr; }
};

// ================================================================ vie ====
TabArea::TabArea(std::string id) : Widget(std::move(id)) {
    layer_ = &addChild(std::make_unique<Layer>(*this, this->id() + ".glisser"));
    TabControl* first = makeStrip(false);
    root_ = std::make_unique<Node>();
    root_->strip = first;
    groups_.push_back(first);
    active_ = first;
}

TabArea::~TabArea() = default;

const char* TabArea::modeName(Mode m) noexcept {
    switch (m) {
        case Mode::Groups: return "groupes";
        case Mode::Mosaic: return "mosaique";
        default:           return "onglets";
    }
}

bool TabArea::modeFromName(std::string_view name, Mode& out) noexcept {
    if (name == "onglets" || name == "tabs") { out = Mode::Tabs; return true; }
    if (name == "groupes" || name == "groups" || name == "cote-a-cote") { out = Mode::Groups; return true; }
    if (name == "mosaique" || name == "mosaic") { out = Mode::Mosaic; return true; }
    return false;
}

// ============================================================= bandes ====
TabControl* TabArea::makeStrip(bool tile) {
    auto strip = std::make_unique<TabControl>(id() + (tile ? ".tuile" : ".groupe") + std::to_string(++serial_));
    TabControl* raw = strip.get();
    raw->setTileMode(tile);
    // La couche reste la DERNIERE enfant : c'est ce qui lui fait voir la
    // souris avant les groupes.
    WidgetPtr layer = layer_ ? removeChild(*layer_) : nullptr;
    addChild(std::move(strip));
    if (layer) addChild(std::move(layer));
    wire(raw);
    return raw;
}

void TabArea::wire(TabControl* s) {
    auto& scope = stripLinks_[s];
    scope += s->currentChanged->connect([this, s](std::size_t) {
        if (internal_ > 0) return;
        // Un clic sur un de ses onglets (ou dans sa liste) : c'est la qu'on travaille.
        active_ = s;
        refreshFocus();
        emitCurrentIfChanged();
    });
    scope += s->tabCloseRequested->connect([this, s](std::size_t local) {
        if (const auto g = globalOf(s, local); g != npos) tabCloseRequested->emit(g);
    });
    scope += s->contextMenuRequested->connect([this, s](std::size_t local, gfx::Point at) {
        if (const auto g = globalOf(s, local); g != npos) contextMenuRequested->emit(g, at);
    });
    scope += s->maximizeRequested->connect([this, s](std::size_t local) {
        if (const auto g = globalOf(s, local); g != npos) (void)toggleMaximized(g);
    });
}

void TabArea::retire(TabControl* strip) {
    if (!strip) return;
    if (active_ == strip) active_ = nullptr;
    if (WidgetPtr gone = removeChild(*strip)) retired_.push_back(std::move(gone));
}

void TabArea::purgeRetired() {
    if (retired_.empty()) return;
    for (const auto& w : retired_) stripLinks_.erase(static_cast<const TabControl*>(w.get()));
    retired_.clear();
}

void TabArea::dropGroup(TabControl* strip) {
    const auto at = positionOf(strip);
    if (at == npos) return;
    if (mode_ == Mode::Mosaic) {
        groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(at));
    } else {
        if (groups_.size() <= 1) return;           // le dernier groupe reste, meme vide
        if (Node* leaf = findLeaf(root_.get(), strip)) removeLeaf(leaf);
        rebuildOrder();
    }
    resizing_ = nullptr;                            // l'arbre a change sous la poignee
    retire(strip);
    // Le groupe ou l'on travaillait est parti : celui d'avant (a gauche, au
    // dessus) prend la main, comme apres avoir ferme un onglet.
    ensureActive(at > 0 ? at - 1 : 0);
}

void TabArea::dropIfEmpty(TabControl* strip) {
    if (strip && strip->tabCount() == 0) dropGroup(strip);
}

void TabArea::rebuild(Mode target) {
    const Widget* current = currentPage();
    // Tous les onglets, dans l'ordre global, sortis de leurs bandes.
    std::vector<std::pair<Tab, WidgetPtr>> all;
    ++internal_;
    for (auto* g : groups_)
        while (g->tabCount() > 0) {
            Tab meta = *g->tab(0);
            all.emplace_back(std::move(meta), g->takeTab(0));
        }
    // Revenir a un groupe garde la premiere bande (son identifiant ne change
    // pas pour qui la connait) ; tout le reste part.
    TabControl* keep = target != Mode::Mosaic && !groups_.empty() && !groups_.front()->tileMode() ? groups_.front() : nullptr;
    for (auto* g : groups_)
        if (g != keep) retire(g);
    groups_.clear();
    root_.reset();
    resizing_ = nullptr;
    maximized_ = nullptr;
    active_ = nullptr;
    mode_ = target;
    if (target == Mode::Mosaic) {
        for (auto& [meta, page] : all) {
            TabControl* tile = makeStrip(true);
            (void)tile->addTab(std::move(meta), std::move(page));
            groups_.push_back(tile);
        }
    } else {
        TabControl* one = keep ? keep : makeStrip(false);
        root_ = std::make_unique<Node>();
        root_->strip = one;
        groups_.push_back(one);
        for (auto& [meta, page] : all) (void)one->addTab(std::move(meta), std::move(page));
    }
    // L'onglet ouvert le reste.
    for (auto* g : groups_)
        if (const int at = current ? g->indexOf(current) : -1; at >= 0) {
            g->setCurrentIndex(static_cast<std::size_t>(at));
            active_ = g;
        }
    --internal_;
    ensureActive(0);
}

void TabArea::rebuildOrder() {
    if (mode_ == Mode::Mosaic || !root_) return;
    groups_.clear();
    const auto collect = [this](const auto& self, const Node* n) -> void {
        if (!n) return;
        if (n->leaf()) { groups_.push_back(n->strip); return; }
        self(self, n->first.get());
        self(self, n->second.get());
    };
    collect(collect, root_.get());
}

void TabArea::ensureActive(std::size_t hint) {
    const bool present = active_ && positionOf(active_) != npos;
    if (present && (active_->tabCount() > 0 || tabCount() == 0)) return;
    active_ = nullptr;
    if (groups_.empty()) return;
    const std::size_t n = groups_.size();
    const std::size_t start = std::min(hint, n - 1);
    // Le plus proche qui a des onglets, en s'eloignant de `hint` des deux cotes.
    for (std::size_t d = 0; d < n; ++d) {
        if (d <= start && groups_[start - d]->tabCount() > 0) { active_ = groups_[start - d]; return; }
        if (start + d < n && groups_[start + d]->tabCount() > 0) { active_ = groups_[start + d]; return; }
    }
    active_ = groups_[start];
}

void TabArea::refreshFocus() {
    for (auto* g : groups_) g->setGroupFocus(groups_.size() <= 1 || g == active_);
}

void TabArea::applyVisibility() {
    for (auto* g : groups_) {
        const bool holds = maximized_ != nullptr && g->indexOf(maximized_) >= 0;
        const bool shown = mode_ != Mode::Mosaic || maximized_ == nullptr || holds;
        g->setVisibility(shown ? Visibility::Visible : Visibility::Collapsed);
        g->setTileMaximized(mode_ == Mode::Mosaic && holds);
    }
}

Widget* TabArea::currentPage() const noexcept {
    if (!active_ || active_->tabCount() == 0) return nullptr;
    return active_->page(active_->currentIndex());
}

void TabArea::emitCurrentIfChanged() {
    const Widget* now = currentPage();
    if (now == lastCurrent_) return;
    lastCurrent_ = now;
    if (now) currentChanged->emit(currentIndex());
}

std::size_t TabArea::positionOf(const TabControl* strip) const noexcept {
    for (std::size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i] == strip) return i;
    return npos;
}

std::size_t TabArea::globalOf(const TabControl* strip, std::size_t local) const noexcept {
    std::size_t base = 0;
    for (const auto* g : groups_) {
        if (g == strip) return local < g->tabCount() ? base + local : npos;
        base += g->tabCount();
    }
    return npos;
}

TabControl* TabArea::locate(std::size_t index, std::size_t& local) const noexcept {
    for (auto* g : groups_) {
        const auto n = g->tabCount();
        if (index < n) { local = index; return g; }
        index -= n;
    }
    return nullptr;
}

void TabArea::transfer(TabControl* from, std::size_t local, TabControl* to, std::size_t at) {
    const Tab* t = from ? from->tab(local) : nullptr;
    if (!t || !to) return;
    Tab meta = *t;
    ++internal_;
    WidgetPtr page = from->takeTab(local);
    const auto placed = to->insertTab(at, std::move(meta), std::move(page));
    to->setCurrentIndex(placed);
    --internal_;
}

void TabArea::finishMove(const Widget* page, std::size_t from, TabControl* active) {
    if (active && positionOf(active) != npos) active_ = active;
    ensureActive(0);
    if (const int to = indexOf(page); to >= 0 && static_cast<std::size_t>(to) != from)
        tabMoved->emit(from, static_cast<std::size_t>(to));
    applyVisibility();
    refreshFocus();
    invalidateLayout();
    emitCurrentIfChanged();
}

// ===================================================== l'interface d'onglets ===
std::size_t TabArea::addTab(Tab meta, WidgetPtr page) {
    if (!page) return npos;
    const bool wasEmpty = tabCount() == 0;
    TabControl* target = nullptr;
    if (mode_ == Mode::Mosaic) {
        // Une tuile de plus, a la fin : la mosaique se replace d'elle-meme.
        target = makeStrip(true);
        groups_.push_back(target);
    } else {
        ensureActive(0);
        target = active_ ? active_ : groups_.front();
    }
    ++internal_;
    const auto local = target->addTab(std::move(meta), std::move(page));
    --internal_;
    if (wasEmpty || !active_) active_ = target;
    const auto index = globalOf(target, local);
    tabInserted->emit(index);
    applyVisibility();
    refreshFocus();
    invalidateLayout();
    emitCurrentIfChanged();
    return index;
}

void TabArea::removeTab(std::size_t index) {
    (void)takeTab(index);
}

WidgetPtr TabArea::takeTab(std::size_t index) {
    std::size_t local = 0;
    TabControl* g = locate(index, local);
    if (!g) return nullptr;
    ++internal_;
    WidgetPtr page = g->takeTab(local);
    --internal_;
    if (page && maximized_ == page.get()) maximized_ = nullptr;
    dropIfEmpty(g);            // Groupes : le groupe vide s'en va ; mosaique : la tuile
    ensureActive(0);
    tabRemoved->emit(index);
    applyVisibility();
    refreshFocus();
    invalidateLayout();
    emitCurrentIfChanged();
    return page;
}

void TabArea::setCurrentIndex(std::size_t index) {
    std::size_t local = 0;
    TabControl* g = locate(index, local);
    if (!g) return;
    ++internal_;
    g->setCurrentIndex(local);
    --internal_;
    active_ = g;
    // Une tuile agrandie : l'agrandissement suit l'onglet qu'on montre (sinon
    // l'onglet demande resterait cache derriere).
    if (mode_ == Mode::Mosaic && maximized_ && maximized_ != g->page(local)) maximized_ = g->page(local);
    applyVisibility();
    refreshFocus();
    invalidateLayout();
    emitCurrentIfChanged();
}

void TabArea::setTabModified(std::size_t index, bool modified) {
    std::size_t local = 0;
    if (TabControl* g = locate(index, local)) g->setTabModified(local, modified);
}

void TabArea::setTabBadge(std::size_t index, std::string badge, Tone tone) {
    std::size_t local = 0;
    if (TabControl* g = locate(index, local)) g->setTabBadge(local, std::move(badge), tone);
}

void TabArea::setTabLive(std::size_t index, bool live) {
    std::size_t local = 0;
    if (TabControl* g = locate(index, local)) g->setTabLive(local, live);
}

void TabArea::setTabIcon(std::size_t index, Icon icon) {
    std::size_t local = 0;
    if (TabControl* g = locate(index, local)) g->setTabIcon(local, icon);
}

void TabArea::setTabTitle(std::size_t index, std::string title) {
    std::size_t local = 0;
    if (TabControl* g = locate(index, local)) g->setTabTitle(local, std::move(title));
}

const TabArea::Tab* TabArea::tab(std::size_t index) const noexcept {
    std::size_t local = 0;
    const TabControl* g = locate(index, local);
    return g ? g->tab(local) : nullptr;
}

std::size_t TabArea::currentIndex() const noexcept {
    if (!active_ || active_->tabCount() == 0) return 0;
    const auto at = globalOf(active_, active_->currentIndex());
    return at == npos ? 0 : at;
}

std::size_t TabArea::tabCount() const noexcept {
    std::size_t n = 0;
    for (const auto* g : groups_) n += g->tabCount();
    return n;
}

Widget* TabArea::page(std::size_t index) const noexcept {
    std::size_t local = 0;
    const TabControl* g = locate(index, local);
    return g ? g->page(local) : nullptr;
}

int TabArea::indexOf(const Widget* p) const noexcept {
    if (!p) return -1;
    std::size_t base = 0;
    for (const auto* g : groups_) {
        if (const int at = g->indexOf(p); at >= 0) return static_cast<int>(base) + at;
        base += g->tabCount();
    }
    return -1;
}

bool TabArea::headerRect(std::size_t index, gfx::Rect& out) const noexcept {
    std::size_t local = 0;
    const TabControl* g = locate(index, local);
    // Une tuile cachee derriere une tuile agrandie n'a pas d'en-tete a l'ecran.
    return g && g->visible() && g->headerRect(local, out);
}

void TabArea::requestContextMenu(std::size_t index) {
    std::size_t local = 0;
    if (TabControl* g = locate(index, local)) g->requestContextMenu(local);
}

void TabArea::markTab(std::size_t index, const PopupMenu* menu) {
    std::size_t local = 0;
    if (TabControl* g = locate(index, local)) g->markTab(local, menu);
}

// ============================================================ disposition ===
void TabArea::setMode(Mode m) {
    if (m == mode_) return;
    if (m == Mode::Groups && mode_ == Mode::Tabs) mode_ = m;   // un groupe, qu'on divisera ensuite
    else rebuild(m);
    applyVisibility();
    refreshFocus();
    invalidateLayout();
    modeChanged->emit(mode_);
    emitCurrentIfChanged();
}

std::size_t TabArea::activeGroup() const noexcept {
    const auto at = positionOf(active_);
    return at == npos ? 0 : at;
}

std::size_t TabArea::groupOf(std::size_t index) const noexcept {
    std::size_t local = 0;
    const TabControl* g = locate(index, local);
    return g ? positionOf(g) : npos;
}

TabControl* TabArea::group(std::size_t g) const noexcept {
    return g < groups_.size() ? groups_[g] : nullptr;
}

TabControl* TabArea::stripOf(std::size_t index, std::size_t* local) const noexcept {
    std::size_t l = 0;
    TabControl* g = locate(index, l);
    if (g && local) *local = l;
    return g;
}

gfx::Rect TabArea::groupRect(std::size_t g) const noexcept {
    return g < groups_.size() ? groups_[g]->bounds() : gfx::Rect{};
}

bool TabArea::splitTab(std::size_t index, Side side) {
    const Widget* p = page(index);
    if (!p) return false;
    if (mode_ == Mode::Mosaic) {
        if (tabCount() < 2) return false;
        setMode(Mode::Groups);                 // un seul groupe, dans l'ordre : on divise ensuite
    }
    const int at = indexOf(p);
    if (at < 0) return false;
    const auto g = groupOf(static_cast<std::size_t>(at));
    return g != npos && dockTab(static_cast<std::size_t>(at), g, side);
}

bool TabArea::dockTab(std::size_t index, std::size_t target, Side side) {
    if (mode_ == Mode::Mosaic) return false;   // pas de groupes dans la mosaique
    std::size_t local = 0;
    TabControl* src = locate(index, local);
    TabControl* dst = group(target);
    if (!src || !dst) return false;
    if (src == dst && src->tabCount() < 2) return false;    // seul dans son groupe : rien a laisser
    Node* leaf = findLeaf(root_.get(), dst);
    if (!leaf) return false;
    const Widget* p = src->page(local);
    const bool wasTabs = mode_ == Mode::Tabs;
    mode_ = Mode::Groups;
    TabControl* fresh = makeStrip(false);
    splitLeaf(leaf, fresh, side);
    rebuildOrder();
    transfer(src, local, fresh, npos);
    dropIfEmpty(src);
    finishMove(p, index, fresh);
    if (wasTabs) modeChanged->emit(mode_);
    return true;
}

bool TabArea::moveTabToGroup(std::size_t index, std::size_t target, std::size_t at) {
    std::size_t local = 0;
    TabControl* src = locate(index, local);
    TabControl* dst = group(target);
    if (!src || !dst) return false;
    const Widget* p = src->page(local);
    if (src == dst) {
        // Dans sa propre bande : `at` est une place AVANT de le retirer.
        const auto n = src->tabCount();
        const std::size_t to = at >= n ? n - 1 : (at > local ? at - 1 : at);
        if (to == local) return false;
        src->moveTab(local, to);
        finishMove(p, index, src);
        return true;
    }
    if (mode_ == Mode::Mosaic) return moveTab(index, positionOf(dst));   // une tuile : prendre sa place
    transfer(src, local, dst, at);
    dropIfEmpty(src);
    finishMove(p, index, dst);
    return true;
}

std::size_t TabArea::neighbourGroup(std::size_t index) const noexcept {
    if (mode_ == Mode::Mosaic || groups_.size() < 2) return npos;
    const auto g = groupOf(index);
    if (g == npos) return npos;
    return g + 1 < groups_.size() ? g + 1 : g - 1;
}

bool TabArea::moveTabToNeighbour(std::size_t index) {
    const auto n = neighbourGroup(index);
    return n != npos && moveTabToGroup(index, n);
}

bool TabArea::moveTab(std::size_t from, std::size_t to) {
    const auto count = tabCount();
    if (from >= count || to >= count || from == to) return false;
    std::size_t local = 0;
    TabControl* src = locate(from, local);
    if (!src) return false;
    const Widget* p = src->page(local);
    if (mode_ == Mode::Mosaic) {
        // Une tuile par onglet : l'ordre des tuiles EST l'ordre des onglets.
        TabControl* tile = groups_[from];
        groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(from));
        groups_.insert(groups_.begin() + static_cast<std::ptrdiff_t>(to), tile);
        finishMove(p, from, tile);
        return true;
    }
    std::size_t dstLocal = 0;
    TabControl* dst = locate(to, dstLocal);
    if (!dst) return false;
    if (dst == src) {
        src->moveTab(local, dstLocal);
        finishMove(p, from, src);
        return true;
    }
    // D'un groupe a l'autre : juste avant l'onglet qui est a `to` (juste apres
    // quand on avance), pour arriver exactement a `to`.
    transfer(src, local, dst, to > from ? dstLocal + 1 : dstLocal);
    dropIfEmpty(src);
    finishMove(p, from, dst);
    return true;
}

bool TabArea::toggleMaximized(std::size_t index) {
    if (mode_ != Mode::Mosaic) return false;
    Widget* p = page(index);
    if (!p) return false;
    const bool on = maximized_ != p;
    setCurrentIndex(index);
    maximized_ = on ? p : nullptr;
    applyVisibility();
    refreshFocus();
    invalidateLayout();
    return true;
}

int TabArea::maximizedIndex() const noexcept {
    return maximized_ ? indexOf(maximized_) : -1;
}

// ========================================================= l'arbre des groupes ===
TabArea::Node* TabArea::findLeaf(Node* n, const TabControl* strip) const noexcept {
    if (!n) return nullptr;
    if (n->leaf()) return n->strip == strip ? n : nullptr;
    if (Node* f = findLeaf(n->first.get(), strip)) return f;
    return findLeaf(n->second.get(), strip);
}

std::unique_ptr<TabArea::Node>& TabArea::slotOf(Node* n) {
    if (!n->parent) return root_;
    return n->parent->first.get() == n ? n->parent->first : n->parent->second;
}

void TabArea::splitLeaf(Node* leaf, TabControl* fresh, Side side) {
    auto& slot = slotOf(leaf);
    auto split = std::make_unique<Node>();
    split->parent = leaf->parent;
    split->stacked = side == Side::Top || side == Side::Bottom;
    auto added = std::make_unique<Node>();
    added->strip = fresh;
    std::unique_ptr<Node> old = std::move(slot);
    old->parent = split.get();
    added->parent = split.get();
    if (side == Side::Left || side == Side::Top) {
        split->first = std::move(added);
        split->second = std::move(old);
    } else {
        split->first = std::move(old);
        split->second = std::move(added);
    }
    slot = std::move(split);
}

void TabArea::removeLeaf(Node* leaf) {
    Node* parent = leaf->parent;
    if (!parent) return;                           // la racine seule reste
    std::unique_ptr<Node> sibling = std::move(parent->first.get() == leaf ? parent->second : parent->first);
    auto& slot = slotOf(parent);
    sibling->parent = parent->parent;
    slot = std::move(sibling);                     // la division et la feuille partent
}

void TabArea::layoutNode(Node& n, const gfx::Rect& r) {
    n.rect = r;
    if (n.leaf()) {
        n.strip->setBounds(r);
        n.handle = {};
        return;
    }
    // Les minimums quand il y a la place ; sinon la proportion, telle quelle.
    if (!n.stacked) {
        const float total = std::max(0.f, r.w - kHandle);
        float a = std::floor(total * n.ratio);
        if (total >= 2.f * kMinW) a = std::clamp(a, kMinW, total - kMinW);
        n.handle = {r.x + a, r.y, kHandle, r.h};
        layoutNode(*n.first, {r.x, r.y, a, r.h});
        layoutNode(*n.second, {r.x + a + kHandle, r.y, total - a, r.h});
    } else {
        const float total = std::max(0.f, r.h - kHandle);
        float a = std::floor(total * n.ratio);
        if (total >= 2.f * kMinH) a = std::clamp(a, kMinH, total - kMinH);
        n.handle = {r.x, r.y + a, r.w, kHandle};
        layoutNode(*n.first, {r.x, r.y, r.w, a});
        layoutNode(*n.second, {r.x, r.y + a + kHandle, r.w, total - a});
    }
}

TabArea::Node* TabArea::handleAt(gfx::Point p) const noexcept {
    if (mode_ == Mode::Mosaic || !root_) return nullptr;
    Node* found = nullptr;
    const auto visit = [&found, p](const auto& self, Node* n) -> void {
        if (!n || n->leaf() || found) return;
        gfx::Rect h = n->handle;
        if (!n->stacked) { h.x -= kGrip; h.w += 2.f * kGrip; }
        else             { h.y -= kGrip; h.h += 2.f * kGrip; }
        if (h.contains(p)) { found = n; return; }
        self(self, n->first.get());
        self(self, n->second.get());
    };
    visit(visit, root_.get());
    return found;
}

void TabArea::resizeTo(gfx::Point p) {
    if (!resizing_) return;
    Node& n = *resizing_;
    const float total = n.stacked ? n.rect.h - kHandle : n.rect.w - kHandle;
    if (total <= 1.f) return;
    const float min = n.stacked ? kMinH : kMinW;
    float a = n.stacked ? p.y - n.rect.y - kHandle * 0.5f : p.x - n.rect.x - kHandle * 0.5f;
    const float lo = std::min(min, total * 0.5f), hi = std::max(total - min, total * 0.5f);
    a = std::clamp(a, lo, hi);
    n.ratio = a / total;
    invalidateLayout();
}

std::vector<gfx::Rect> TabArea::splitHandles() const {
    std::vector<gfx::Rect> out;
    if (mode_ == Mode::Mosaic) return out;
    const auto visit = [&out](const auto& self, const Node* n) -> void {
        if (!n || n->leaf()) return;
        out.push_back(n->handle);
        self(self, n->first.get());
        self(self, n->second.get());
    };
    visit(visit, root_.get());
    return out;
}

// ============================================================== mise en page ===
void TabArea::onLayout() {
    const auto area = contentRect();
    if (layer_) layer_->setBounds(bounds());
    if (mode_ == Mode::Mosaic) layoutMosaic(area);
    else if (root_) layoutNode(*root_, area);
}

void TabArea::layoutMosaic(const gfx::Rect& area) {
    if (maximized_) {
        for (auto* g : groups_)
            if (g->indexOf(maximized_) >= 0) g->setBounds(area);
        return;
    }
    const auto cells = mosaicRects(groups_.size(), area, kGap);
    for (std::size_t i = 0; i < groups_.size() && i < cells.size(); ++i) groups_[i]->setBounds(cells[i]);
}

void TabArea::onPaint(const PaintContext& ctx) {
    purgeRetired();                 // ici, aucun evenement n'est en cours
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), c.windowBg);
    // Les poignees : un trait au milieu, l'accent pendant qu'on en tire une.
    const auto handles = [&](const auto& self, const Node* n) -> void {
        if (!n || n->leaf()) return;
        const auto& h = n->handle;
        const bool hot = resizing_ == n;
        ctx.r.fillRect(h, hot ? c.accent : c.windowBg);
        if (!hot) {
            if (!n->stacked) ctx.r.line({h.x + h.w * 0.5f, h.y + 2.f}, {h.x + h.w * 0.5f, h.bottom() - 2.f}, c.border, 1.f);
            else             ctx.r.line({h.x + 2.f, h.y + h.h * 0.5f}, {h.right() - 2.f, h.y + h.h * 0.5f}, c.border, 1.f);
        }
        self(self, n->first.get());
        self(self, n->second.get());
    };
    if (mode_ != Mode::Mosaic) handles(handles, root_.get());
    // La tuile ou l'on travaille : un cadre d'accent dans l'espace autour d'elle.
    if (mode_ == Mode::Mosaic && groups_.size() > 1 && !maximized_ && active_ && active_->visible()) {
        const auto r = active_->bounds();
        const float k = 2.f;
        frame(ctx.r, {r.x - k, r.y - k, r.w + 2.f * k, r.h + 2.f * k}, k, c.accent);
    }
}

// ================================================================= la souris ===
TabControl* TabArea::groupAt(gfx::Point p) const noexcept {
    for (auto* g : groups_)
        if (g->visible() && g->bounds().contains(p)) return g;
    return nullptr;
}

void TabArea::activate(TabControl* strip) {
    if (!strip || strip == active_ || positionOf(strip) == npos) return;
    if (strip->tabCount() == 0 && tabCount() > 0) return;
    active_ = strip;
    refreshFocus();
    emitCurrentIfChanged();
}

EventResult TabArea::layerEvent(const InputEvent& ev) {
    // Une poignee tiree : la souris est a elle jusqu'au lacher.
    if (resizing_) {
        if (const auto* m = std::get_if<MouseMove>(&ev)) {
            resizeTo(m->pos);
            return EventResult::Consumed;
        }
        const auto* k = std::get_if<KeyDown>(&ev);
        if (std::holds_alternative<MouseUp>(ev) || std::holds_alternative<MouseDown>(ev) || (k && k->key == Key::Escape)) {
            resizing_ = nullptr;
            invalidate();
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }
    // Un onglet tire : les zones suivent la souris ; le lacher depose, Echap
    // (ou un autre appui, la fenetre qui perd la main) annule.
    if (dragging_) {
        if (const auto* m = std::get_if<MouseMove>(&ev)) {
            pointer_ = m->pos;
            drop_ = dropAt(m->pos);
            invalidate();
            return EventResult::Consumed;
        }
        if (const auto* u = std::get_if<MouseUp>(&ev)) {
            pointer_ = u->pos;
            drop_ = dropAt(u->pos);
            finishDrag(true);
            return EventResult::Consumed;
        }
        if (std::holds_alternative<MouseDown>(ev)) {
            finishDrag(false);
            return EventResult::Consumed;
        }
        if (std::holds_alternative<MouseWheel>(ev)) return EventResult::Consumed;
        if (const auto* k = std::get_if<KeyDown>(&ev); k && k->key == Key::Escape) {
            finishDrag(false);
            return EventResult::Consumed;
        }
        if (const auto* f = std::get_if<FocusChange>(&ev); f && !f->gained) finishDrag(false);
        return EventResult::Ignored;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        press_ = {};
        if (d->button == MouseButton::Left && d->clickCount == 1)
            if (Node* h = handleAt(d->pos)) {
                resizing_ = h;
                invalidate();
                return EventResult::Consumed;
            }
        TabControl* g = groupAt(d->pos);
        if (!g) return EventResult::Ignored;
        bool onButton = false;
        const int header = g->headerAt(d->pos, &onButton);
        // Le groupe sous l'appui devient celui ou l'on travaille - sauf le clic
        // droit sur un en-tete (son menu vise l'onglet sans l'ouvrir) et le
        // clic du milieu (il ferme). Le clic, lui, continue vers le groupe.
        if (d->button == MouseButton::Left || (d->button == MouseButton::Right && header < 0)) activate(g);
        if (d->button == MouseButton::Left && d->clickCount == 1 && header >= 0 && !onButton)
            press_ = Press{g->page(static_cast<std::size_t>(header)), d->pos, true};
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev); m && press_.armed) {
        const float dx = m->pos.x - press_.at.x, dy = m->pos.y - press_.at.y;
        if (dx * dx + dy * dy < kDragStart * kDragStart) return EventResult::Ignored;
        if (indexOf(press_.page) < 0) {
            press_ = {};
            return EventResult::Ignored;
        }
        dragging_ = true;
        pointer_ = m->pos;
        drop_ = dropAt(m->pos);
        invalidate();
        return EventResult::Consumed;
    }
    if (std::holds_alternative<MouseUp>(ev) && press_.armed) press_ = {};
    return EventResult::Ignored;
}

TabArea::Drop TabArea::dropAt(gfx::Point p) const {
    Drop d;
    const int fromAt = indexOf(press_.page);
    if (fromAt < 0) return d;
    std::size_t local = 0;
    const TabControl* src = locate(static_cast<std::size_t>(fromAt), local);
    if (!src) return d;
    const auto b = bounds();
    if (!b.contains(p)) {
        // Hors de la zone : assez loin, c'est une fenetre a lui qu'on demande.
        const float dx = p.x < b.x ? b.x - p.x : (p.x >= b.right() ? p.x - b.right() : 0.f);
        const float dy = p.y < b.y ? b.y - p.y : (p.y >= b.bottom() ? p.y - b.bottom() : 0.f);
        if (detachEnabled_ && std::max(dx, dy) >= kDetachDist) d.kind = DropKind::Outside;
        return d;
    }
    TabControl* g = groupAt(p);
    if (!g) return d;                              // une poignee, un espace entre deux tuiles
    if (mode_ == Mode::Mosaic) {
        if (g == src) return d;
        d.kind = DropKind::Tile;
        d.group = g;
        d.highlight = g->bounds();
        return d;
    }
    const auto bar = g->barRect();
    if (bar.contains(p)) {
        // Sur une bande : la place entre deux en-tetes (ceux qu'on voit).
        const auto n = g->tabCount();
        std::size_t at = n;
        bool any = false;
        for (std::size_t i = 0; i < n; ++i) {
            gfx::Rect hr;
            if (!g->headerRect(i, hr) || hr.right() <= bar.x || hr.x >= bar.right()) continue;
            any = true;
            if (p.x < hr.x + hr.w * 0.5f) {
                at = i;
                d.caretX = hr.x;
                break;
            }
            at = i + 1;
            d.caretX = hr.right();
        }
        if (!any) d.caretX = bar.x + 4.f;
        if (g == src) {
            const std::size_t to = at > local ? at - 1 : at;
            if (to == local) return d;             // a sa place : rien
        }
        d.kind = DropKind::Strip;
        d.group = g;
        d.at = at;
        d.caretX = std::clamp(d.caretX, bar.x + 2.f, bar.right() - 2.f);
        d.highlight = bar;
        return d;
    }
    // Sur la page : un bord (a cote) ou le milieu (dans ce groupe).
    const auto gb = g->bounds();
    const gfx::Rect r{gb.x, bar.bottom(), gb.w, std::max(1.f, gb.bottom() - bar.bottom())};
    const float fx = (p.x - r.x) / std::max(1.f, r.w), fy = (p.y - r.y) / std::max(1.f, r.h);
    const float dl = fx, dr = 1.f - fx, dt = fy, db = 1.f - fy;
    const float m = std::min({dl, dr, dt, db});
    if (m > kEdge) {
        if (g == src) return d;
        d.kind = DropKind::Centre;
        d.group = g;
        d.highlight = gb;
        return d;
    }
    if (g == src && src->tabCount() < 2) return d;   // seul dans son groupe : rien a laisser
    const Side side = m == dl ? Side::Left : m == dr ? Side::Right : m == dt ? Side::Top : Side::Bottom;
    d.kind = DropKind::Edge;
    d.group = g;
    d.side = side;
    switch (side) {
        case Side::Left:   d.highlight = {gb.x, gb.y, std::floor(gb.w * 0.5f), gb.h}; break;
        case Side::Right:  d.highlight = {gb.x + std::floor(gb.w * 0.5f), gb.y, gb.w - std::floor(gb.w * 0.5f), gb.h}; break;
        case Side::Top:    d.highlight = {gb.x, gb.y, gb.w, std::floor(gb.h * 0.5f)}; break;
        case Side::Bottom: d.highlight = {gb.x, gb.y + std::floor(gb.h * 0.5f), gb.w, gb.h - std::floor(gb.h * 0.5f)}; break;
    }
    return d;
}

void TabArea::finishDrag(bool apply) {
    const Drop d = drop_;
    const Widget* p = press_.page;
    dragging_ = false;
    press_ = {};
    drop_ = {};
    invalidate();
    if (!apply || !p) return;
    const int at = indexOf(p);
    if (at < 0) return;                            // ferme pendant qu'on le tirait
    const auto index = static_cast<std::size_t>(at);
    const auto target = d.group ? positionOf(d.group) : npos;
    switch (d.kind) {
        case DropKind::Strip:   if (target != npos) (void)moveTabToGroup(index, target, d.at); break;
        case DropKind::Centre:  if (target != npos) (void)moveTabToGroup(index, target); break;
        case DropKind::Edge:    if (target != npos) (void)dockTab(index, target, d.side); break;
        case DropKind::Tile:    if (target != npos) (void)moveTab(index, target); break;
        case DropKind::Outside: detachRequested->emit(index); break;
        case DropKind::None:    break;
    }
}

void TabArea::paintDrag(const PaintContext& ctx) const {
    const auto& theme = ctx.theme;
    const auto& c = theme.color;
    // Ce que fera le lacher, en toutes lettres, au milieu de la zone.
    const auto label = [&](const gfx::Rect& r, const std::string& text) {
        const auto font = theme.font.caption;
        const float w = ctx.r.measure(text, font).width + 22.f;
        const float h = ctx.r.lineHeight(font) + 10.f;
        const gfx::Rect pill{r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h};
        ctx.r.fillRoundedRect(pill, c.accent, h * 0.5f);
        ctx.r.drawText({pill.x + 11.f, pill.y + 5.f}, text, font, c.textInverted);
    };
    switch (drop_.kind) {
        case DropKind::Strip: {
            const auto& bar = drop_.highlight;
            ctx.r.fillRect(bar, c.accent.withAlpha(30));
            ctx.r.fillRoundedRect({drop_.caretX - 1.5f, bar.y + 3.f, 3.f, std::max(0.f, bar.h - 6.f)}, c.accent, 1.5f);
            break;
        }
        case DropKind::Edge:
        case DropKind::Centre:
        case DropKind::Tile: {
            const auto& h = drop_.highlight;
            ctx.r.fillRect(h, c.accent.withAlpha(46));
            frame(ctx.r, h, 2.f, c.accent);
            std::string text = "Dans ce groupe";
            if (drop_.kind == DropKind::Tile) text = "\xC3\x80 cette place";
            else if (drop_.kind == DropKind::Edge)
                text = drop_.side == Side::Left ? "\xC3\x80 gauche" : drop_.side == Side::Right ? "\xC3\x80 droite"
                     : drop_.side == Side::Top  ? "En haut" : "En bas";
            label(h, text);
            break;
        }
        case DropKind::Outside:
        case DropKind::None:
            break;
    }
    // L'onglet tire, sous la souris : son icone et son titre.
    const int from = indexOf(press_.page);
    const Tab* t = from >= 0 ? tab(static_cast<std::size_t>(from)) : nullptr;
    if (!t) return;
    const auto font = theme.font.ui;
    const float tw = std::min(ctx.r.measure(t->title, font).width, 240.f);
    const bool icon = t->icon != Icon::None;
    const gfx::Rect chip{pointer_.x + 14.f, pointer_.y + 12.f, tw + 22.f + (icon ? 22.f : 0.f), 28.f};
    ctx.r.fillRoundedRect({chip.x + 2.f, chip.y + 3.f, chip.w, chip.h}, gfx::Color{0, 0, 0, 70}, 6.f);
    ctx.r.fillRoundedRect(chip, drop_.kind == DropKind::None ? c.border : c.accent, 6.f);
    ctx.r.fillRoundedRect({chip.x + 1.f, chip.y + 1.f, chip.w - 2.f, chip.h - 2.f}, c.panelBg, 5.f);
    float x = chip.x + 11.f;
    if (icon) {
        drawIcon(ctx.r, t->icon, {x, chip.y + 6.f, 16.f, 16.f}, c.accent);
        x += 22.f;
    }
    ctx.r.pushClip({x, chip.y, std::max(0.f, chip.right() - 8.f - x), chip.h});
    ctx.r.drawText({x, chip.y + (chip.h - ctx.r.lineHeight(font)) * 0.5f}, t->title, font, c.text);
    ctx.r.popClip();
    if (drop_.kind == DropKind::Outside)
        label({chip.x, chip.bottom() + 6.f, chip.w, 26.f}, "Rel\xC3\xA2" "che : d\xC3\xA9tacher dans une fen\xC3\xAAtre");
}

} // namespace ui
