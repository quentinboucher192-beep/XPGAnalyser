// app/hmi/HmiEditorStretch.cpp - 1.50.0 : ETIRER dans l'editeur de vues (planche 02).
//  Le sous-menu Etirer (le clic droit d'un objet, l'Explorateur d'objets, Etirer v de la barre d'outils),
//  l'apercu au survol, l'etirement en une commande, la fenetre Etirer... Le moteur : hmi/HmiStretch.hpp.
#include "HmiEditor.hpp"
#include "HmiIcons.hpp"
#include "HmiStretchDialog.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/Commands.hpp"

#include <algorithm>
#include <cmath>

namespace app {

namespace st = hmi::stretch;

namespace {

constexpr int kStretchMargins = 900;   // + HmiEditor::kStretchMenu
constexpr int kStretchDialog = 901;
constexpr int kStretchGuide = 100;     // + le rang du repere
constexpr int kStretchColumn = 500;    // + le rang de la colonne
constexpr unsigned kViewEdges[7] = {st::kLeft, st::kRight, st::kTop, st::kBottom, st::kWidth, st::kHeight, st::kAll};
constexpr unsigned kOneEdge[4] = {st::kLeft, st::kRight, st::kTop, st::kBottom};

std::string num(double v) { return hmi::formatNumber(std::round(v * 10.0) / 10.0); }

} // namespace

st::Plan HmiEditor::stretchPlan(const st::Request& request) const {
    const auto* v = doc_->project.view(viewId_);
    if (!v) {
        st::Plan p;
        p.why = "pas de vue";
        return p;
    }
    return st::plan(*v, canvas_->selection(), request);
}

std::optional<st::Request> HmiEditor::stretchRequestOf(int menuId) const {
    const int d = menuId - kStretchMenu;
    if (d < 0 || d >= 1000) return std::nullopt;
    st::Request r;
    r.keepMargins = stretchMargins_;
    if (d >= 0 && d < 7) {
        r.edges = kViewEdges[d];
        return r;
    }
    if (d >= 10 && d < 14) {
        r.to = st::To::Neighbour;
        r.edges = kOneEdge[d - 10];
        return r;
    }
    if (d >= 20 && d < 24) {
        r.to = st::To::Selection;
        r.edges = kOneEdge[d - 20];
        return r;
    }
    if (d >= kStretchGuide && d < kStretchColumn) {
        r.to = st::To::Guide;
        r.index = d - kStretchGuide;
        return r;
    }
    if (d >= kStretchColumn && d < kStretchColumn + 100) {
        r.to = st::To::Column;
        r.index = d - kStretchColumn;
        return r;
    }
    return std::nullopt;
}

std::vector<ui::PopupMenu::Item> HmiEditor::stretchItems() const {
    namespace cmd = ui::cmd;
    std::vector<ui::PopupMenu::Item> out;
    const auto* v = doc_->project.view(viewId_);
    if (!v) return out;
    const auto entry = [&](std::string label, int d, std::string key = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.id = kStretchMenu + d;
        it.shortcut = std::move(key);
        const auto r = stretchRequestOf(it.id);
        const auto p = r ? stretchPlan(*r) : st::Plan{};
        it.enabled = p.possible();
        if (!it.enabled) it.disabledReason = p.why;
        return it;
    };
    // Un sous-menu : grise quand aucune de ses entrees ne se fait (la raison commune, sinon `fallback`).
    const auto parent = [](std::string label, std::vector<ui::PopupMenu::Item> children, std::string fallback) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        bool any = false, same = true;
        std::string why;
        for (const auto& c : children) {
            any = any || c.enabled;
            if (c.enabled) continue;
            if (why.empty()) why = c.disabledReason;
            else if (why != c.disabledReason) same = false;
        }
        if (!any) {
            it.enabled = false;
            it.disabledReason = same && !why.empty() ? why : std::move(fallback);
        }
        it.children = std::move(children);
        return it;
    };
    out.push_back(cmd::heading("Jusqu'au bord de la vue"));
    out.push_back(entry("Jusqu'au bord gauche", 0, "Ctrl+Alt+Maj+\xE2\x86\x90"));
    out.push_back(entry("Jusqu'au bord droit", 1, "Ctrl+Alt+Maj+\xE2\x86\x92"));
    out.push_back(entry("Jusqu'en haut", 2, "Ctrl+Alt+Maj+\xE2\x86\x91"));
    out.push_back(entry("Jusqu'en bas", 3, "Ctrl+Alt+Maj+\xE2\x86\x93"));
    out.push_back(cmd::separator());
    out.push_back(entry("Toute la largeur de la vue", 4));
    out.push_back(entry("Toute la hauteur de la vue", 5));
    out.push_back(entry("Toute la vue", 6));
    out.push_back(cmd::heading("Jusqu'\xC3\xA0\xE2\x80\xA6"));
    {
        // L'objet voisin : son nom quand un seul objet est choisi (« A droite : Panneau_Etat »).
        const char* words[4] = {"\xC3\x80 gauche", "\xC3\x80 droite", "En haut", "En bas"};
        std::vector<ui::PopupMenu::Item> sides;
        for (int i = 0; i < 4; ++i) {
            auto it = entry(words[i], 10 + i);
            if (it.enabled) {
                const auto p = stretchPlan(*stretchRequestOf(it.id));
                if (!p.neighbour.empty()) it.label += " : " + p.neighbour;
            }
            sides.push_back(std::move(it));
        }
        out.push_back(parent("L'objet voisin", std::move(sides), "aucun objet voisin"));
    }
    {
        std::vector<ui::PopupMenu::Item> sides;
        const char* words[4] = {"Gauche", "Droite", "Haut", "Bas"};
        for (int i = 0; i < 4; ++i) sides.push_back(entry(words[i], 20 + i));
        out.push_back(parent("Le bord de la s\xC3\xA9lection", std::move(sides), "deux objets ou plus"));
    }
    {
        std::vector<ui::PopupMenu::Item> guides;
        for (std::size_t i = 0; i < v->guides.size() && i < 400; ++i) {
            const auto& g = v->guides[i];
            guides.push_back(entry(std::string(g.vertical ? "Vertical, x = " : "Horizontal, y = ") + num(g.position), kStretchGuide + static_cast<int>(i)));
        }
        auto it = parent("Un rep\xC3\xA8re", std::move(guides), "aucun rep\xC3\xA8re de ce c\xC3\xB4t\xC3\xA9");
        if (v->guides.empty()) {
            it.enabled = false;
            it.disabledReason = "aucun rep\xC3\xA8re";
            it.children.push_back(entry("Aucun rep\xC3\xA8re", kStretchGuide));
        }
        out.push_back(std::move(it));
    }
    {
        std::vector<ui::PopupMenu::Item> cols;
        const auto spans = v->columns.visible ? hmi::edit::columnSpans(*v) : std::vector<hmi::edit::ColumnSpan>{};
        for (std::size_t i = 0; i < spans.size() && i < 100; ++i)
            cols.push_back(entry("Colonne " + std::to_string(i + 1) + " (x " + num(spans[i].x) + " \xE2\x86\x92 " + num(spans[i].x + spans[i].w) + ")",
                                 kStretchColumn + static_cast<int>(i)));
        auto it = parent("Une colonne de la grille", std::move(cols), "d\xC3\xA9j\xC3\xA0 l\xC3\xA0");
        if (spans.empty()) {
            // Le pourquoi du moteur : pas de grille montree, ou elle ne tient pas dans la vue.
            it.children.push_back(entry("Colonne 1", kStretchColumn));
            it.enabled = false;
            it.disabledReason = it.children.front().disabledReason;
        }
        out.push_back(std::move(it));
    }
    out.push_back(cmd::separator());
    ui::PopupMenu::Item keep;
    keep.label = "Garder les marges (" + num(st::viewMargin(*v)) + " px)";
    keep.id = kStretchMenu + kStretchMargins;
    keep.checkable = true;
    keep.checked = stretchMargins_;
    out.push_back(std::move(keep));
    ui::PopupMenu::Item dialog;
    dialog.label = "\xC3\x89tirer\xE2\x80\xA6";
    dialog.id = kStretchMenu + kStretchDialog;
    const auto any = stretchPlan(st::Request{});
    const bool allLocked = any.why == "verrouill\xC3\xA9" || any.why == "tous verrouill\xC3\xA9s" || any.why == "rien de choisi";
    dialog.enabled = !allLocked;
    if (allLocked) dialog.disabledReason = any.why;
    out.push_back(std::move(dialog));
    return out;
}

ui::PopupMenu::Item HmiEditor::stretchEntry() const {
    ui::PopupMenu::Item it;
    it.label = "\xC3\x89tirer";
    it.note = "nouveau";
    it.paintIcon = [](const ui::PaintContext& ctx, gfx::Rect r) {
        const float g = std::min(16.f, std::min(r.w, r.h));
        drawHmiGlyph(ctx.r, HmiGlyph::DistributeH, {r.x + (r.w - g) / 2.f, r.y + (r.h - g) / 2.f, g, g}, ctx.theme.color.accent);
    };
    it.children = stretchItems();
    // Un objet verrouille (tous les choisis) : grise, la raison dite.
    const auto p = stretchPlan(st::Request{});
    if (p.why == "verrouill\xC3\xA9" || p.why == "tous verrouill\xC3\xA9s") {
        it.enabled = false;
        it.disabledReason = p.why;
    }
    return it;
}

bool HmiEditor::stretchMenuChosen(int id) {
    const int d = id - kStretchMenu;
    if (d < 0 || d >= 1000) return false;
    stretchHover_ = -1;
    canvas_->setStretchPreview(std::nullopt);
    if (d == kStretchMargins) {
        stretchMargins_ = !stretchMargins_;
        const auto* v = doc_->project.view(viewId_);
        statusBar_->setTransientMessage(stretchMargins_ ? "\xC3\x89tirer : les marges gard\xC3\xA9" "es (" + num(v ? st::viewMargin(*v) : 0.0) + " px)."
                                                        : std::string("\xC3\x89tirer : sans marge, l'objet touche le bord."),
                                        5.0, ui::StatusBar::Severity::Info);
        return true;
    }
    if (d == kStretchDialog) {
        (void)openStretchDialog();
        return true;
    }
    if (const auto r = stretchRequestOf(id)) {
        (void)stretch(*r);
        return true;
    }
    return false;
}

bool HmiEditor::stretch(const st::Request& request, std::string* why) {
    auto& c = *canvas_;
    const auto* v = doc_->project.view(viewId_);
    if (!v) return false;
    const auto plan = stretchPlan(request);
    stretchHover_ = -1;
    c.setStretchPreview(std::nullopt);
    if (!plan.possible()) {
        if (why) *why = plan.why;
        statusBar_->setTransientMessage(st::sentence(*v, request, plan), 6.0, ui::StatusBar::Severity::Warning);
        return false;
    }
    const std::string said = st::sentence(*v, request, plan);
    // UNE commande pour tous les objets choisis (un seul Ctrl+Z), comme Aligner : la pile, la session, l'historique.
    if (!c.edit("\xC3\x89tirer", [&](hmi::Project&, hmi::View& vv) { st::apply(vv, plan); })) {
        if (why) *why = "rien n'a chang\xC3\xA9";
        return false;
    }
    statusBar_->setTransientMessage(said, 8.0, ui::StatusBar::Severity::Success);
    invalidate();
    return true;
}

bool HmiEditor::previewStretch(std::optional<st::Request> request) {
    if (!request) {
        canvas_->setStretchPreview(std::nullopt);
        return false;
    }
    auto plan = stretchPlan(*request);
    const bool ok = plan.possible();
    canvas_->setStretchPreview(ok ? std::optional<st::Plan>(std::move(plan)) : std::nullopt);
    return ok;
}

void HmiEditor::syncStretchPreview() {
    if (stretchHover_ == -2) return;   // la fenetre Etirer... tient l'apercu
    int hovered = -1;
    if (context_ && context_->isOpen()) {
        // La colonne la plus profonde qui a une entree en surbrillance : celle que la souris (ou le clavier) vise.
        for (std::size_t k = context_->columnCount(); k-- > 0;) {
            const int h = context_->highlightedIn(k);
            if (h < 0) continue;
            const auto& items = context_->columnItems(k);
            if (static_cast<std::size_t>(h) < items.size() && items[static_cast<std::size_t>(h)].enabled
                && items[static_cast<std::size_t>(h)].children.empty())
                hovered = items[static_cast<std::size_t>(h)].id;
            break;
        }
    }
    if (hovered == stretchHover_) return;
    stretchHover_ = hovered;
    (void)previewStretch(hovered >= 0 ? stretchRequestOf(hovered) : std::nullopt);
}

void HmiEditor::openStretchMenu() {
    if (!context_ || canvas_->selection().empty()) return;
    const auto* v = doc_->project.view(viewId_);
    if (!v) return;
    menuKind_ = 0;   // le menu de la vue (kMenuCanvas) : canvasMenuChosen fait l'entree choisie
    const auto units = hmi::edit::units(*v, canvas_->selection());
    const auto* one = units.size() == 1 ? v->object(units.front()) : nullptr;
    auto items = stretchItems();
    items.insert(items.begin(), ui::cmd::heading("\xC3\x89tirer", one ? one->name : std::to_string(units.size()) + " objets"));
    context_->setItems(std::move(items));
    const auto r = tools_->rectOf(ActStretch);
    context_->openAtSurface(tools_->toSurface(gfx::Point{r.x, r.bottom() + 2.f}));
}

std::unique_ptr<HmiStretchDialog> HmiEditor::makeStretchDialog() {
    const auto* v = doc_->project.view(viewId_);
    if (!v || canvas_->selection().empty()) return nullptr;
    const auto units = hmi::edit::units(*v, canvas_->selection());
    const auto* one = units.size() == 1 ? v->object(units.front()) : nullptr;
    HmiStretchDialog::Spec spec;
    spec.object = one ? one->name : std::to_string(units.size()) + " objets";
    spec.title = "\xC3\x89tirer " + spec.object;
    spec.viewMargin = st::viewMargin(*v);
    spec.request.keepMargins = stretchMargins_;
    spec.request.margin = stretchMargins_ ? spec.viewMargin : 0.0;
    const std::weak_ptr<bool> alive = alive_;
    spec.preview = [this, alive](const st::Request& r) -> std::pair<bool, std::string> {
        if (alive.expired()) return {false, {}};
        stretchHover_ = -2;   // l'apercu suit la fenetre (plus le menu)
        auto p = stretchPlan(r);
        if (!p.possible()) {
            canvas_->setStretchPreview(std::nullopt);
            return {false, "Rien \xC3\xA0 \xC3\xA9tirer : " + p.why};
        }
        std::string line = p.changes.size() == 1 ? st::sizeText(p.changes.front())
                                                 : std::to_string(p.changes.size()) + " objets, chacun " + st::targetText(*doc_->project.view(viewId_), r, p);
        canvas_->setStretchPreview(std::move(p));
        return {true, line};
    };
    return std::make_unique<HmiStretchDialog>(std::move(spec));
}

bool HmiEditor::openStretchDialog() {
    auto dialog = makeStretchDialog();
    if (!dialog) return false;
    auto* manager = menu::MenuManager::instance();
    if (!manager) {
        stretchDialogDone(false, {});
        statusBar_->setTransientMessage("La fen\xC3\xAAtre \xC3\x89tirer\xE2\x80\xA6 ne s'ouvre pas ici.", 5.0, ui::StatusBar::Severity::Warning);
        return false;
    }
    const std::weak_ptr<bool> alive = alive_;
    manager->ShowDialog(std::move(dialog), [this, alive](const menu::DialogResult& r) {
        if (alive.expired()) return;
        stretchDialogDone(r.accepted(), r.payload);
    });
    return true;
}

void HmiEditor::stretchDialogDone(bool accepted, const std::string& payload) {
    stretchHover_ = -1;
    canvas_->setStretchPreview(std::nullopt);
    if (accepted) (void)stretch(HmiStretchDialog::parse(payload));
}

} // namespace app
