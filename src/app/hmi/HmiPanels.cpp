#include "HmiPanels.hpp"
#include "HmiParamPanes.hpp"   // 1.9 : les parametres des popups
#include "HmiObjectAlarmPanes.hpp"    // 1.9 : la cloche des alarmes par defaut
#include "HmiTreeData.hpp"            // 1.10.3 (Q1103) : l'objet deplie par familles, comme l'arbre
#include "../../hmi/HmiOperators.hpp"   // 1.10.3 (Q1103) : les operateurs du symbole d'une instance (comme l'arbre)
#include "../../hmi/HmiLoginMenu.hpp"
#include "../../hmi/HmiNavigation.hpp"
#include "../../hmi/HmiExport.hpp"
#include "../../hmi/HmiGuide.hpp"
#include "../../hmi/HmiSymbols.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../hmi/HmiDuplicate.hpp"   // 1.10.2 (D) : les modeles d'objets dans la bibliotheque
#include "../../hmi/HmiWidgets.hpp"
#include "../../hmi/HmiAssets.hpp"

#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiExpr.hpp"          // ---- Lot API 8 : les expressions impossibles ----
#include "../../ui/widgets/ExprField.hpp"  // 1.10 (chantier K) : les champs a expression, partout pareils
#include "../../hmi/HmiExprCheck.hpp"     // ---- Lot API 8 : les expressions impossibles ----
#include "../../hmi/HmiPublicVars.hpp"    // ---- Lot API 8 : les expressions impossibles ----
#include "../../hmi/HmiScript.hpp"        // ---- Lot API 8 : les expressions impossibles ----
#include "../../project/RenamePlan.hpp"   // ---- Lot API 8 : les expressions impossibles (PlcTypes) ----
#include "../../hmi/HmiApiVars.hpp"     // 1.11.1 (API-M) : les variables de l'automate sous API.
#include "HmiApiVarsPane.hpp"            // 1.11.1 (API-V, R1111-6) : l'arbre dans la bibliotheque
#include "HmiValueKind.hpp"              // 1.11.3 : le carre de legende de chaque case
#include "../../ui/Shapes.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <array>              // 1.10.4 (K3) : les sections de l'inspecteur
#include <initializer_list>
#include <mutex>                // 1.11.1 (API-M) : le modele API. en cache

namespace app {

// 1.10.4 (K3) : la variable d'un objet ("variable"), sans la section Communication
// (definies plus bas ; declarees ici et dans HmiEditor.cpp, pas dans HmiPanels.hpp
// que vingt en-tetes incluent). Vrai : elle fait pour l'objet ce que sa Valeur ne
// fait pas (une commande l'ecrit, un histogramme la mesure, un onglet la suit...) -
// sa case se montre toujours ; faux : seulement si elle est deja reglee (un projet
// d'avant). Son libelle : "Variable ecrite", "Variable mesuree"...
bool hmiObjectNeedsVariable(hmi::Kind) noexcept;
std::string hmiVariableLabel(hmi::Kind);

using hmi::Id;
using hmi::kNoId;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Une coupe "..." qui ne tombe pas au milieu d'un caractere accentue.
std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (r.measure(s, f).width <= w) return s;
    std::size_t n = r.fitCharacters(s, f, std::max(0.f, w - r.measure("...", f).width));
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n) + "...";
}

// Un champ de saisie qui sait prendre le focus lui-meme : grabFocus est
// protege, et renommer sur place n'a de sens que si l'on peut taper tout de
// suite.
class RenameField final : public ui::InputText {
public:
    using ui::InputText::InputText;
    void focusNow() { grabFocus(); }
};

void textCentred(const ui::PaintContext& ctx, const gfx::Rect& r, const std::string& s, gfx::FontId f, gfx::Color c) {
    const float lh = ctx.r.lineHeight(f);
    ctx.r.drawText({r.x, r.y + (r.h - lh) / 2}, s, f, c);
}

} // namespace

// ================================================================ panneau ===
HmiTitledPanel::HmiTitledPanel(std::string id, std::string title) : ui::Widget(std::move(id)), title_(std::move(title)) {}

ui::Widget& HmiTitledPanel::addRow(ui::WidgetPtr w, float height) {
    rows_.push_back({w.get(), height});
    return addChild(std::move(w));
}
ui::Widget& HmiTitledPanel::setBody(ui::WidgetPtr w) {
    body_ = w.get();
    return addChild(std::move(w));
}
ui::SizeHint HmiTitledPanel::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {240.f, 200.f};
    h.minimum = {120.f, 60.f};
    h.stretchX = h.stretchY = 1.f;
    return h;
}
void HmiTitledPanel::onLayout() {
    const auto b = bounds();
    float y = b.y + kTitle;
    for (auto& r : rows_) {
        r.w->setBounds({b.x + 4, y + 2, b.w - 8, r.h - 4});
        y += r.h;
    }
    if (body_) body_->setBounds({b.x, y, b.w, std::max(0.f, b.y + b.h - y)});
}
void HmiTitledPanel::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    ctx.r.fillRect(b, ctx.theme.color.panelBg);
    ctx.r.fillRect({b.x, b.y, b.w, kTitle}, ctx.theme.color.headerBg);
    ctx.r.fillRect({b.x, b.y + kTitle - 1, b.w, 1}, ctx.theme.color.border);
    const float lh = ctx.r.lineHeight(ctx.theme.font.smallUi);
    ctx.r.drawText({b.x + 10, b.y + (kTitle - lh) / 2}, title_, ctx.theme.font.smallUi, ctx.theme.color.textMuted);
}

// ================================================================== barre ===
HmiToolStrip::HmiToolStrip(std::string id) : ui::Widget(std::move(id)) {}

void HmiToolStrip::add(int action, HmiGlyph glyph, std::string tip, std::string label) {
    Item it;
    it.action = action;
    it.glyph = glyph;
    it.tip = std::move(tip);
    it.label = std::move(label);
    items_.push_back(std::move(it));
    invalidateLayout();
}
void HmiToolStrip::separator() {
    Item it;
    it.separator = true;
    items_.push_back(std::move(it));
}
void HmiToolStrip::setEnabledWhen(int action, std::function<bool()> p) {
    for (auto& it : items_) if (it.action == action && !it.separator) it.enabled = p;
}
void HmiToolStrip::setCheckedWhen(int action, std::function<bool()> p) {
    for (auto& it : items_) if (it.action == action && !it.separator) it.checked = p;
}
void HmiToolStrip::setVisibleWhen(int action, std::function<bool()> p) {
    for (auto& it : items_) if (it.action == action && !it.separator) it.visible = p;
    invalidateLayout();
}
void HmiToolStrip::setText(int action, std::string tip, std::string label) {
    for (auto& it : items_)
        if (it.action == action && !it.separator) {
            it.tip = tip;
            it.label = label;
        }
    invalidateLayout();
    invalidate();
}

ui::SizeHint HmiToolStrip::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {400.f, 36.f};
    h.minimum = {60.f, 36.f};
    h.stretchX = 1.f;
    return h;
}

void HmiToolStrip::layoutItems(float) const {
    const auto b = bounds();
    // Lot 15 : les boutons caches ne prennent pas de place ; un separateur n'est
    // montre qu'entre deux boutons montres.
    std::vector<bool> on(items_.size(), false);
    bool any = false;
    std::size_t pendingSeparator = items_.size();
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i];
        if (it.separator) {
            if (any) pendingSeparator = i;
            continue;
        }
        if (it.visible && !it.visible()) continue;
        if (pendingSeparator < items_.size()) on[pendingSeparator] = true;
        pendingSeparator = items_.size();
        on[i] = true;
        any = true;
    }
    // Lot macros 1 : TROP ETROIT, LES LIBELLES TOMBENT, de la droite vers la
    // gauche : l'icone et l'infobulle restent. Une barre coupee au milieu d'un
    // mot cachait ses derniers boutons.
    for (auto& it : items_) it.compact = false;
    const auto widthOf = [&](const Item& it) {
        float w = 30;
        if (!it.label.empty() && !it.compact) w += ui::measureWidth(it.label, gfx::FontId{13}) + 8;
        return w;
    };
    const auto total = [&] {
        float t = 6;
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (on[i]) t += items_[i].separator ? 8.f : widthOf(items_[i]) + 2.f;
        return t;
    };
    if (b.w > 0.f)
        for (std::size_t i = items_.size(); i-- > 0 && total() > b.w;)
            if (on[i] && !items_[i].separator && !items_[i].label.empty()) items_[i].compact = true;
    float x = b.x + 6;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        auto& it = items_[i];
        if (!on[i]) { it.rect = {b.x, b.y, 0, 0}; continue; }
        if (it.separator) { it.rect = {x + 3, b.y + 7, 1, b.h - 14}; x += 8; continue; }
        const float w = widthOf(it);
        it.rect = {x, b.y + 3, w, b.h - 6};
        x += w + 2;
    }
}

gfx::Rect HmiToolStrip::rectOf(int action) const {
    layoutItems(1);
    for (const auto& it : items_) if (!it.separator && it.action == action) return it.rect;
    return {};
}

int HmiToolStrip::actionByTip(std::string_view tip) const {
    auto lower = [](std::string_view t) {
        std::string out(t);
        for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return out;
    };
    const std::string want = lower(tip);
    // Lot 15 : un outil cache (setVisibleWhen : celui d'un autre onglet) passe
    // apres un outil montre - "Ajouter" est l'outil de la table des adresses
    // quand elle est ouverte, pas "Ajouter un equipement", cache.
    int hidden = -1;
    for (const auto& it : items_)
        if (!it.separator && (lower(it.tip).rfind(want, 0) == 0 || (!it.label.empty() && lower(it.label) == want))) {
            if (it.rect.w > 0) return it.action;
            if (hidden < 0) hidden = it.action;
        }
    return hidden;
}

int HmiToolStrip::itemAt(gfx::Point p) const {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i];
        if (it.separator || it.rect.w <= 0) continue;
        if (p.x >= it.rect.x && p.x < it.rect.x + it.rect.w && p.y >= it.rect.y && p.y < it.rect.y + it.rect.h)
            return static_cast<int>(i);
    }
    return -1;
}

void HmiToolStrip::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    // La souris est partie (les mouvements hors du widget ne lui arrivent
    // pas) : plus de bouton en surbrillance.
    if (!hovered()) hover_ = -1;
    ctx.r.fillRect(b, ctx.theme.color.headerBg);
    ctx.r.fillRect({b.x, b.y + b.h - 1, b.w, 1}, ctx.theme.color.border);
    layoutItems(1);
    ctx.r.pushClip(b);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& it = items_[i];
        if (it.rect.w <= 0) continue;     // cache (lot 15)
        if (it.separator) { ctx.r.fillRect(it.rect, ctx.theme.color.border); continue; }
        const bool enabled = !it.enabled || it.enabled();
        const bool checked = it.checked && it.checked();
        const bool hot = enabled && static_cast<int>(i) == hover_;
        if (checked) ctx.r.fillRoundedRect(it.rect, ctx.theme.color.accent.withAlpha(60), 4);
        else if (hot) ctx.r.fillRoundedRect(it.rect, ctx.theme.color.text.withAlpha(24), 4);
        if (static_cast<int>(i) == pressed_ && enabled) ctx.r.fillRoundedRect(it.rect, ctx.theme.color.accent.withAlpha(90), 4);
        const gfx::Color col = !enabled ? ctx.theme.color.textDisabled : checked ? ctx.theme.color.accent : ctx.theme.color.text;
        drawHmiGlyph(ctx.r, it.glyph, {it.rect.x + 5, it.rect.y + (it.rect.h - 20) / 2, 20, 20}, col);
        if (!it.label.empty() && !it.compact)
            textCentred(ctx, {it.rect.x + 29, it.rect.y, it.rect.w - 30, it.rect.h}, it.label, gfx::FontId{13}, col);
    }
    ctx.r.popClip();
}

ui::EventResult HmiToolStrip::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = itemAt(m->pos);
        if (h != hover_) {
            hover_ = h;
            setTooltip(h >= 0 ? items_[static_cast<std::size_t>(h)].tip : std::string{});
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        if (m->button != ui::MouseButton::Left) return ui::EventResult::Ignored;
        pressed_ = itemAt(m->pos);
        invalidate();
        return pressed_ >= 0 ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseUp>(&ev)) {
        const int was = pressed_;
        pressed_ = -1;
        invalidate();
        if (was >= 0 && itemAt(m->pos) == was) {
            const auto& it = items_[static_cast<std::size_t>(was)];
            if (!it.enabled || it.enabled()) triggered->emit(it.action);
            return ui::EventResult::Consumed;
        }
    }
    return ui::EventResult::Ignored;
}

// ============================================================ explorateur ===
HmiObjectList::HmiObjectList(std::string id) : ui::Widget(std::move(id)) { setFocusPolicy(true); }

ui::SizeHint HmiObjectList::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {220.f, 200.f};
    h.minimum = {120.f, 60.f};
    h.stretchX = 1.f;
    h.stretchY = 1.f;
    return h;
}

void HmiObjectList::beginRename(Id id) {
    if (!view_ || !view_->object(id)) return;
    endRename(false);
    int row = -1;
    for (std::size_t i = 0; i < rows_.size(); ++i) if (rows_[i].id == id && rows_[i].object()) { row = static_cast<int>(i); break; }
    if (row < 0) return;
    auto input = std::make_unique<RenameField>(this->id() + ".rename");
    input->setText(view_->object(id)->name);
    auto* raw = input.get();
    editor_ = &addChild(std::move(input));
    renaming_ = id;
    const auto b = bounds();
    const auto& r = rows_[static_cast<std::size_t>(row)];
    const float x = b.x + 40 + static_cast<float>(r.depth) * 16.f;
    editor_->setBounds({x, b.y + static_cast<float>(row) * rowH_ - scrollY_, std::max(60.f, b.w - (x - b.x) - 50), rowH_});
    raw->editingDone->connect([this](const std::string& text) {
        const Id target = renaming_;
        endRename(false);
        if (target != kNoId) renamed->emit(target, text);
    }).release();
    raw->focusNow();
    invalidate();
}

void HmiObjectList::endRename(bool) {
    if (!editor_) return;
    auto owned = removeChild(*editor_);
    editor_ = nullptr;
    renaming_ = kNoId;
    // Le champ peut etre en train d'emettre : on le laisse mourir apres.
    static std::vector<ui::WidgetPtr> graveyard;
    graveyard.push_back(std::move(owned));
    if (graveyard.size() > 8) graveyard.erase(graveyard.begin());
    invalidate();
}

void HmiObjectList::setView(const hmi::View* v) {
    view_ = v;
    rebuild();
    refreshExpressionBadges();   // ---- Lot API 8 : les expressions impossibles ----
}

// ---- Lot API 8 : les expressions impossibles ----
void HmiObjectList::setExpressionContext(const hmi::Project* project, const domain::Project* plc) {
    // Relus a chaque fois : le programme a pu changer sans changer d'adresse.
    exprPlcNames_ = hmiPlcUpperNames(plc);
    exprPaths_ = hmiPlcPaths(plc);
    exprProject_ = project;
    exprPlc_ = plc;
}

void HmiObjectList::refreshExpressionBadges() {
    exprBadges_.clear();
    if (!view_) return;
    // 1.10.3 (Q1103) : 1 pilote (bleu) ; 3 un repere (ambre, "$" ; 1.11 : une case a
    // repere qui se calcule, sans erreur) ; 2 une expression qui ne se lit pas (rouge),
    // repere ou non. Le plus grave l'emporte.
    const auto worse = [](int a, int b) {
        const auto rank = [](int s) { return s == 2 ? 3 : s == 3 ? 2 : s; };
        return rank(b) > rank(a) ? b : a;
    };
    for (const auto& o : view_->objects) {
        for (const auto& pr : o.props) {
            if (pr.expr.empty()) continue;
            int& state = exprBadges_[o.id];
            if (state == 2) break;
            const bool bad = !hmiExpressionError(*view_, pr.key, pr.expr, exprPlc_, exprProject_, exprPlcNames_.get(), &exprPaths_).empty();
            state = worse(state, bad ? 2 : hmi::dup::hasMarker(pr.expr) ? 3 : 1);
        }
        // Un repere ailleurs (un texte, une action, une alarme de l'objet) se voit aussi.
        if (!hmi::dup::ownMarkers(o).empty()) exprBadges_[o.id] = worse(exprBadges_[o.id], 3);
    }
    // 1.9 (decision 7) : les conditions d'alarmes surchargees de l'objet comptent ;
    // rouge si une alarme generee de l'objet a une condition impossible.
    if (exprProject_)
        for (const auto& o : view_->objects)
            if (const int a = objalarms::conditionBadge(*exprProject_, *view_, o, exprPlc_, exprPlcNames_.get(), &exprPaths_); a > 0) {
                int& state = exprBadges_[o.id];
                state = worse(state, a);
            }
    invalidate();
}
// ---- fin Lot API 8 ----

void HmiObjectList::setSelection(const std::vector<Id>& ids) {
    selection_ = ids;
    invalidate();
}

void HmiObjectList::setFilter(std::string text, std::optional<hmi::Kind> kind) {
    filterText_ = lower(std::move(text));
    filterKind_ = kind;
    scrollY_ = 0;
    rebuild();
}

// 1.10.3 (Q1103) : la "famille" Operateurs d'une instance (les operateurs de son
// symbole, comme le noeud Operateurs de l'arbre), apres les familles de hmitree.
namespace { constexpr int kOperatorsRow = 100; }

void HmiObjectList::rebuild() {
    rows_.clear();
    if (!view_) { invalidate(); return; }
    const bool filtering = !filterText_.empty() || filterKind_.has_value();
    // Lot recherche : la recherche de toutes les listes - chaque mot (ou "phrase")
    // dans le nom, le genre, la variable, le texte affiche ou l'infobulle de
    // l'objet ; aucun -mot exclu ; sans casse ni accents.
    const ui::SearchQuery query(filterText_);
    auto matches = [&](const hmi::Object& o) {
        if (filterKind_ && o.kind != *filterKind_) return false;
        if (query.empty()) return true;
        return query.matches({o.name, hmi::kindLabel(o.kind), o.text("variable"), o.text("text"), o.text("label"), o.text("title")});
    };
    // Un objet est montre s'il correspond, ou si un de ses descendants
    // correspond (on garde le chemin jusqu'a lui).
    std::function<bool(Id)> relevant = [&](Id id) {
        const auto* o = view_->object(id);
        if (!o) return false;
        if (matches(*o)) return true;
        for (Id c : view_->childrenOf(id)) if (relevant(c)) return true;
        return false;
    };
    // 1.10 (chantier O) : les objets qui portent des alarmes (leur noeud Alarmes).
    alarmNodes_.clear();
    if (exprProject_ && hmi::viewGeneratesAlarms(*view_))
        for (const auto& o : view_->objects)
            if (auto node = alarmtree::nodeOf(*exprProject_, *view_, o)) alarmNodes_.emplace(o.id, std::move(*node));
    std::function<void(Id, int)> walk = [&](Id parent, int depth) {
        for (Id id : view_->childrenOf(parent)) {
            const auto* o = view_->object(id);
            if (!o) continue;
            if (filtering && !relevant(id)) continue;
            const bool kids = o->kind == hmi::Kind::Group || !view_->childrenOf(id).empty();
            const auto alarms = alarmNodes_.find(id);
            // 1.10.3 (Q1103) : ses familles, celles de l'arbre (pas pendant une recherche).
            const auto fams = !filtering && exprProject_ ? hmitree::objectFamilies(*exprProject_, *view_, *o)
                                                         : std::vector<hmitree::Family>{};
            const auto* sym = !filtering && exprProject_ ? hmi::symbolOf(*exprProject_, *o) : nullptr;
            const auto* ops = sym && !sym->operators.empty() ? &sym->operators : nullptr;
            const bool group = kids || alarms != alarmNodes_.end() || !fams.empty() || ops;
            rows_.push_back({id, depth, group, !filtering || matches(*o)});
            // Un groupe est deplie au depart ; un autre objet (ses familles, ses alarmes), replie.
            const bool open = kids ? (filtering || !collapsed_.count(id)) : objectsOpen_.count(id) > 0;
            if (!group || !open) continue;
            // Dans l'ordre de l'arbre : Actions, Liens fx, Parametres, [Alarmes],
            // Animations, Reperes, Elements (ses objets, directement : l'explorateur
            // est l'arbre de la vue), Securite. Une famille est repliee au depart.
            const auto family = [&](hmitree::Family f) {
                if (f == hmitree::Family::Elements) { walk(id, depth + 1); return; }
                const int fi = static_cast<int>(f);
                rows_.push_back({id, depth + 1, true, true, -1, fi, -1});
                if (!familiesOpen_.count({id, fi})) return;
                const auto lines = hmitree::familyLines(*exprProject_, *o, f);
                for (std::size_t k = 0; k < lines.size(); ++k)
                    rows_.push_back({id, depth + 2, false, true, -1, fi, static_cast<int>(k)});
            };
            std::size_t next = 0;
            for (; next < fams.size() && fams[next] <= hmitree::Family::Params; ++next) family(fams[next]);
            // Le noeud Alarmes, comme dans l'arbre du projet.
            if (alarms != alarmNodes_.end()) {
                rows_.push_back({id, depth + 1, true, true, 0});
                if (alarmsOpen_.count(id)) {
                    const auto lines = alarmtree::linesOf(alarms->second);
                    for (std::size_t k = 1; k < lines.size(); ++k)
                        rows_.push_back({id, depth + 1 + lines[k].depth, lines[k].group != nullptr, true, static_cast<int>(k)});
                }
            }
            for (; next < fams.size(); ++next) family(fams[next]);
            if (kids && std::find(fams.begin(), fams.end(), hmitree::Family::Elements) == fams.end()) walk(id, depth + 1);
            // Enfin les operateurs de son symbole (une instance), comme dans l'arbre.
            if (ops) {
                rows_.push_back({id, depth + 1, true, true, -1, kOperatorsRow, -1});
                if (familiesOpen_.count({id, kOperatorsRow}))
                    for (std::size_t k = 0; k < ops->size(); ++k)
                        rows_.push_back({id, depth + 2, false, true, -1, kOperatorsRow, static_cast<int>(k)});
            }
        }
    };
    walk(kNoId, 0);
    invalidate();
}

// ---- 1.10 (chantier O) : deplier un objet montre ses alarmes ----
const alarmtree::Group* HmiObjectList::alarmNode(Id object) const {
    const auto it = alarmNodes_.find(object);
    return it == alarmNodes_.end() ? nullptr : &it->second;
}

void HmiObjectList::setAlarmsOpen(Id object, bool open) {
    if (open) {
        objectsOpen_.insert(object);
        alarmsOpen_.insert(object);
        collapsed_.erase(object);
    } else {
        alarmsOpen_.erase(object);
    }
    rebuild();
}

bool HmiObjectList::alarmRowRect(Id object, int line, gfx::Rect& out) const {
    const auto b = bounds();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id != object || rows_[i].alarm != line) continue;
        const float y = b.y + static_cast<float>(i) * rowH_ - scrollY_;
        if (y < b.y || y + rowH_ > b.y + b.h) return false;
        const float x = b.x + 30 + static_cast<float>(rows_[i].depth) * 16.f;
        out = {x, y, std::max(40.f, b.x + b.w - 8.f - x), rowH_};
        return true;
    }
    return false;
}
// ---- fin 1.10 ----

// ---- 1.10.3 (Q1103) : l'objet deplie par familles, comme l'arbre ----
void HmiObjectList::setObjectOpen(Id object, bool open) {
    const auto* o = view_ ? view_->object(object) : nullptr;
    const bool kids = o && (o->kind == hmi::Kind::Group || !view_->childrenOf(object).empty());
    if (kids) { if (open) collapsed_.erase(object); else collapsed_.insert(object); }
    else if (open) objectsOpen_.insert(object);
    else objectsOpen_.erase(object);
    rebuild();
}

void HmiObjectList::setFamilyOpen(Id object, int family, bool open) {
    if (open) familiesOpen_.insert({object, family});
    else familiesOpen_.erase({object, family});
    if (open) setObjectOpen(object, true);
    else rebuild();
}

std::string HmiObjectList::familyRowText(const Row& row) const {
    const auto* o = view_ ? view_->object(row.id) : nullptr;
    if (o && exprProject_ && row.family == kOperatorsRow) {
        const auto* sym = hmi::symbolOf(*exprProject_, *o);
        if (!sym) return {};
        if (row.line < 0) return "Op\xC3\xA9rateurs (" + std::to_string(sym->operators.size()) + ")";
        const auto k = static_cast<std::size_t>(row.line);
        return k < sym->operators.size() ? hmi::operatorSignature(sym->operators[k]) : std::string{};
    }
    if (!o || !exprProject_ || row.family < 0 || row.family >= static_cast<int>(hmitree::Family::Count)) return {};
    const auto fam = static_cast<hmitree::Family>(row.family);
    if (row.line < 0) return hmitree::familyTitle(*exprProject_, *view_, *o, fam);
    const auto lines = hmitree::familyLines(*exprProject_, *o, fam);
    return static_cast<std::size_t>(row.line) < lines.size() ? lines[static_cast<std::size_t>(row.line)].text : std::string{};
}

bool HmiObjectList::familyRowRect(Id object, int family, int line, gfx::Rect& out) const {
    const auto b = bounds();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& r = rows_[i];
        if (r.id != object || r.family != family || r.line != line) continue;
        const float y = b.y + static_cast<float>(i) * rowH_ - scrollY_;
        if (y < b.y || y + rowH_ > b.y + b.h) return false;
        const float x = b.x + 30 + static_cast<float>(r.depth) * 16.f;
        out = {x, y, std::max(40.f, b.x + b.w - 8.f - x), rowH_};
        return true;
    }
    return false;
}

std::vector<std::string> HmiObjectList::familyRowTexts(Id object) const {
    std::vector<std::string> out;
    for (const auto& r : rows_)
        if (r.id == object && r.family >= 0) out.push_back(familyRowText(r));
    return out;
}
// ---- fin 1.10.3 ----

void HmiObjectList::revealSelection() {
    if (selection_.empty() || !view_) return;
    bool changed = false;
    for (Id id : selection_) {
        const auto* o = view_->object(id);
        for (Id p = o ? o->parent : kNoId; p != kNoId;) {
            changed |= collapsed_.erase(p) > 0;
            const auto* po = view_->object(p);
            p = po ? po->parent : kNoId;
        }
    }
    if (changed) rebuild();
    reveal(selection_.front());
}

void HmiObjectList::reveal(Id id) {
    if (!view_) return;
    bool changed = false;
    const auto* o = view_->object(id);
    for (Id p = o ? o->parent : kNoId; p != kNoId;) {
        changed |= collapsed_.erase(p) > 0;
        const auto* po = view_->object(p);
        p = po ? po->parent : kNoId;
    }
    if (changed) rebuild();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id && rows_[i].object()) {
            const float y = static_cast<float>(i) * rowH_;
            const float h = bounds().h;
            if (y < scrollY_) scrollY_ = y;
            else if (y + rowH_ > scrollY_ + h) scrollY_ = y + rowH_ - h;
            invalidate();
            break;
        }
}

int HmiObjectList::rowAt(float y) const {
    const float local = y - bounds().y + scrollY_;
    if (local < 0) return -1;
    const int i = static_cast<int>(local / rowH_);
    return i < static_cast<int>(rows_.size()) ? i : -1;
}

void HmiObjectList::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    if (!hovered()) hover_ = -1;          // la souris est partie
    const auto& c = ctx.theme.color;
    rowH_ = ctx.theme.metric.rowHeight;
    ctx.r.fillRect(b, c.inputBg);
    ctx.r.pushClip(b);
    if (!view_ || rows_.empty()) {
        ctx.r.drawText({b.x + 10, b.y + 10}, view_ && !view_->objects.empty() ? "Aucun objet ne correspond au filtre."
                                                                              : "Vue vide : choisis un objet dans la biblioth\xC3\xA8que.",
                       ctx.theme.font.smallUi, c.textMuted);
        ctx.r.popClip();
        return;
    }
    const auto f = ctx.theme.font.ui;
    const auto small = ctx.theme.font.smallUi;
    const int first = static_cast<int>(scrollY_ / rowH_);
    for (int i = std::max(0, first); i < static_cast<int>(rows_.size()); ++i) {
        const float y = b.y + static_cast<float>(i) * rowH_ - scrollY_;
        if (y > b.y + b.h) break;
        const auto& row = rows_[static_cast<std::size_t>(i)];
        const auto* o = view_->object(row.id);
        if (!o) continue;
        // 1.10 (chantier O) : le noeud Alarmes d'un objet et ses lignes.
        if (row.alarm >= 0) {
            const auto it = alarmNodes_.find(row.id);
            if (it == alarmNodes_.end()) continue;
            const gfx::Rect ar{b.x, y, b.w, rowH_};
            if (i == hover_) ctx.r.fillRect(ar, c.text.withAlpha(16));
            float ax = b.x + 6 + static_cast<float>(row.depth) * 16.f;
            const auto lines = alarmtree::linesOf(it->second);
            const auto* line = static_cast<std::size_t>(row.alarm) < lines.size() ? &lines[static_cast<std::size_t>(row.alarm)] : nullptr;
            if (!line) continue;
            if (line->group) {
                if (row.alarm == 0) {
                    const gfx::Point m{ax + 5, y + rowH_ / 2};
                    if (alarmsOpen_.count(row.id)) ui::shapes::fillPolygon(ctx.r, {{m.x - 4, m.y - 2}, {m.x + 4, m.y - 2}, {m.x, m.y + 3}}, c.warning);
                    else ui::shapes::fillPolygon(ctx.r, {{m.x - 2, m.y - 4}, {m.x + 3, m.y}, {m.x - 2, m.y + 4}}, c.warning);
                }
                ax += 12;
                drawHmiGlyph(ctx.r, HmiGlyph::Bell, {ax, y + (rowH_ - 16) / 2, 16, 16}, row.alarm == 0 ? c.warning : c.textMuted);
                ax += 22;
                textCentred(ctx, {ax, y, std::max(20.f, b.x + b.w - 8 - ax), rowH_}, fit(ctx.r, line->group->label, small, b.x + b.w - 8 - ax), small,
                            row.alarm == 0 ? c.warning : c.textMuted);
                continue;
            }
            const auto& a = *line->alarm;
            ax += 12;
            const unsigned rgb = alarmtree::priorityColor(a.priority);
            const gfx::Rect dot{ax + 4, y + (rowH_ - 9) / 2, 9, 9};
            if (a.active) ctx.r.fillRoundedRect(dot, gfx::Color::rgb(rgb), 4.5f);
            else ctx.r.strokeRect(dot, gfx::Color::rgb(rgb));
            ax += 22;
            const std::string detail = a.detail();
            const float dw = ctx.r.measure(detail, small).width;
            const float nameW = std::max(20.f, b.x + b.w - 12 - ax - dw - 8);
            const std::string name = fit(ctx.r, a.name, f, nameW);
            textCentred(ctx, {ax, y, nameW, rowH_}, name, f, a.active ? c.text : c.textMuted);
            textCentred(ctx, {b.x + b.w - 10 - dw, y, dw + 2, rowH_}, detail, small, a.overridden ? c.info : c.textMuted);
            continue;
        }
        // 1.10.3 (Q1103) : une famille de l'objet et ses lignes, comme dans l'arbre.
        if (row.family >= 0) {
            const gfx::Rect fr{b.x, y, b.w, rowH_};
            if (i == hover_) ctx.r.fillRect(fr, c.text.withAlpha(16));
            float fx = b.x + 6 + static_cast<float>(row.depth) * 16.f;
            const auto fam = static_cast<hmitree::Family>(row.family);
            const bool markers = fam == hmitree::Family::Markers;
            const std::string text = familyRowText(row);
            if (row.line < 0) {
                const gfx::Point m{fx + 5, y + rowH_ / 2};
                if (familiesOpen_.count({row.id, row.family})) ui::shapes::fillPolygon(ctx.r, {{m.x - 4, m.y - 2}, {m.x + 4, m.y - 2}, {m.x, m.y + 3}}, c.textMuted);
                else ui::shapes::fillPolygon(ctx.r, {{m.x - 2, m.y - 4}, {m.x + 3, m.y}, {m.x - 2, m.y + 4}}, c.textMuted);
                fx += 12;
                const bool accent = fam == hmitree::Family::Actions || fam == hmitree::Family::Links || fam == hmitree::Family::Params;
                ui::drawIcon(ctx.r, row.family == kOperatorsRow ? ui::Icon::Folder : hmitree::familyIcon(fam), {fx, y + (rowH_ - 16) / 2, 16, 16},
                             markers ? c.warning : accent ? c.accent : c.textMuted);
                fx += 22;
                textCentred(ctx, {fx, y, std::max(20.f, b.x + b.w - 8 - fx), rowH_}, fit(ctx.r, text, small, b.x + b.w - 8 - fx), small,
                            markers ? c.warning : c.text);
                continue;
            }
            const auto* o2 = view_->object(row.id);
            const auto lines = o2 && exprProject_ ? hmitree::familyLines(*exprProject_, *o2, fam) : std::vector<hmitree::Line>{};
            const auto* line = static_cast<std::size_t>(row.line) < lines.size() ? &lines[static_cast<std::size_t>(row.line)] : nullptr;
            const bool help = line && line->icon == ui::Icon::Info;
            fx += 12;
            ui::drawIcon(ctx.r, line && line->icon != ui::Icon::None ? line->icon : row.family == kOperatorsRow ? ui::Icon::Code : ui::Icon::Play,
                         {fx, y + (rowH_ - 14) / 2, 14, 14},
                         help ? c.textMuted : markers ? c.warning : c.textMuted);
            fx += 20;
            textCentred(ctx, {fx, y, std::max(20.f, b.x + b.w - 8 - fx), rowH_}, fit(ctx.r, text, small, b.x + b.w - 8 - fx), small,
                        help ? c.textMuted : c.text);
            continue;
        }
        const bool sel = std::find(selection_.begin(), selection_.end(), row.id) != selection_.end();
        const gfx::Rect rr{b.x, y, b.w, rowH_};
        if (sel) ctx.r.fillRect(rr, c.selectionBg);
        else if (i == hover_) ctx.r.fillRect(rr, c.text.withAlpha(16));
        const bool hidden = view_->effectivelyHidden(*o), locked = view_->effectivelyLocked(*o);
        const gfx::Color fg = sel ? c.selectionText : !row.match ? c.textDisabled : hidden ? c.textMuted : c.text;
        float x = b.x + 6 + static_cast<float>(row.depth) * 16.f;
        if (row.group) {
            const bool kids = o->kind == hmi::Kind::Group || !view_->childrenOf(row.id).empty();   // 1.10 : sinon, ses alarmes
            const bool open = kids ? (!collapsed_.count(row.id) || !filterText_.empty() || filterKind_) : objectsOpen_.count(row.id) > 0;
            const gfx::Point m{x + 5, y + rowH_ / 2};
            if (open) ui::shapes::fillPolygon(ctx.r, {{m.x - 4, m.y - 2}, {m.x + 4, m.y - 2}, {m.x, m.y + 3}}, fg);
            else ui::shapes::fillPolygon(ctx.r, {{m.x - 2, m.y - 4}, {m.x + 3, m.y}, {m.x - 2, m.y + 4}}, fg);
        }
        x += 12;
        drawHmiGlyph(ctx.r, glyphFor(o->kind), {x, y + (rowH_ - 16) / 2, 16, 16}, sel ? fg : ctx.theme.color.accent);
        x += 22;
        // A droite : le cadenas et l'oeil, toujours visibles quand ils sont
        // "actifs", au survol sinon.
        const float right = b.x + b.w - 8;
        const gfx::Rect eye{right - 18, y + (rowH_ - 16) / 2, 16, 16};
        const gfx::Rect lock{right - 40, y + (rowH_ - 16) / 2, 16, 16};
        const bool showIcons = i == hover_ || sel;
        if (o->hidden || showIcons)
            drawHmiGlyph(ctx.r, o->hidden ? HmiGlyph::EyeOff : HmiGlyph::Eye, eye, o->hidden ? c.warning : fg.withAlpha(150));
        if (o->locked || showIcons)
            drawHmiGlyph(ctx.r, o->locked ? HmiGlyph::Lock : HmiGlyph::Unlock, lock, o->locked ? c.warning : fg.withAlpha(150));
        const std::string type = std::string(hmi::kindLabel(o->kind));
        const float typeW = ctx.r.measure(type, small).width;
        // ---- Lot API 8 : les expressions impossibles ---- un badge "fx" : au moins une
        // propriete pilotee par une expression (rouge : l'une ne se lit pas).
        const auto badge = exprBadges_.find(row.id);
        const bool driven = badge != exprBadges_.end();
        const bool broken = driven && badge->second == 2;
        const bool marked = driven && badge->second == 3;   // 1.10.3 (Q1103) ; 1.11 (REP) : un repere, qui se calcule (ambre sans erreur)
        const float badgeW = driven ? 28.f : 0.f;
        // 1.10 (chantier O) : la cloche et le nombre d'alarmes de l'objet, devant le cadenas.
        const auto* alarms = alarmNode(row.id);
        const std::string alarmCount = alarms ? std::to_string(alarms->count) : std::string{};
        const float bellW = alarms ? 26.f + ctx.r.measure(alarmCount, small).width : 0.f;
        if (alarms) {
            const float bx = lock.x - 6 - bellW;
            drawHmiGlyph(ctx.r, HmiGlyph::Bell, {bx, y + (rowH_ - 14) / 2, 14, 14}, sel ? fg : c.textMuted);
            textCentred(ctx, {bx + 17, y, bellW - 17, rowH_}, alarmCount, small, sel ? fg : c.textMuted);
        }
        const float nameW = std::max(20.f, lock.x - x - typeW - 16 - badgeW - bellW);
        const std::string name = fit(ctx.r, o->name, f, nameW);
        textCentred(ctx, {x, y, nameW, rowH_}, name, f, fg);
        const float nx = x + ctx.r.measure(name, f).width + 8;
        textCentred(ctx, {nx, y, typeW + 4, rowH_}, type, small, sel ? fg.withAlpha(200) : c.textMuted);
        if (driven && nx + typeW + 32.f < lock.x) {
            const gfx::Rect pill{nx + typeW + 8.f, y + (rowH_ - 15.f) * 0.5f, 22.f, 15.f};
            ctx.r.fillRoundedRect(pill, broken ? c.error : marked ? c.warning : c.accent, 7.f);
            const char* mark = marked ? "$" : "fx";
            ctx.r.drawText({pill.x + (pill.w - ctx.r.measure(mark, small).width) * 0.5f, pill.y + (pill.h - ctx.r.lineHeight(small)) * 0.5f},
                           mark, small, c.selectionText);
        }
        // ---- fin Lot API 8 ----
        (void)locked;
    }
    ctx.r.popClip();
}

bool HmiObjectList::rowRect(hmi::Id id, gfx::Rect& out, int part) const {
    const auto b = bounds();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id != id || !rows_[i].object()) continue;
        const float y = b.y + static_cast<float>(i) * rowH_ - scrollY_;
        if (y < b.y || y + rowH_ > b.y + b.h) return false;
        const float right = b.x + b.w - 8;
        if (part == 1)      out = {right - 18, y + (rowH_ - 16) / 2, 16, 16};
        else if (part == 2) out = {right - 40, y + (rowH_ - 16) / 2, 16, 16};
        else                out = {b.x + 30 + static_cast<float>(rows_[i].depth) * 16.f, y, 90, rowH_};
        return true;
    }
    return false;
}

ui::EventResult HmiObjectList::onEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (!focused() || editor_) return ui::EventResult::Ignored;
        // 1.11 (T1, tranche 6) : les touches de l'ecran passent (F7 Compiler, F8 l'IHM, F9, F5...) ;
        // l'explorateur les mangeait toutes : apres un clic sur une alarme, F8 ne demarrait pas l'IHM.
        switch (k->key) {
            case ui::Key::F1: case ui::Key::F3: case ui::Key::F5: case ui::Key::F7: case ui::Key::F8:
            case ui::Key::F9: case ui::Key::F10: case ui::Key::F11: case ui::Key::F12:
                return ui::EventResult::Ignored;
            default: break;
        }
        keyPressed->emit(*k);
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos.y);
        if (h != hover_) { hover_ = h; invalidate(); }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        const float maxScroll = std::max(0.f, static_cast<float>(rows_.size()) * rowH_ - bounds().h);
        scrollY_ = std::clamp(scrollY_ - w->dy * rowH_ * 3, 0.f, maxScroll);
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        grabFocus();
        const int i = rowAt(m->pos.y);
        if (i < 0) {
            selection_.clear();
            invalidate();
            selectionChanged->emit(selection_);
            return ui::EventResult::Consumed;
        }
        const auto& row = rows_[static_cast<std::size_t>(i)];
        const auto b = bounds();
        const float right = b.x + b.w - 8;
        // 1.10 (chantier O) : le noeud Alarmes se deplie ; une alarme choisit
        // l'objet et ouvre la section Alarmes de l'objet sur elle.
        if (row.alarm >= 0) {
            const Id object = row.id;
            if (row.alarm == 0) {
                if (alarmsOpen_.count(object)) alarmsOpen_.erase(object);
                else alarmsOpen_.insert(object);
                rebuild();
                return ui::EventResult::Consumed;
            }
            const auto* node = alarmNode(object);
            if (!node) return ui::EventResult::Consumed;
            const auto lines = alarmtree::linesOf(*node);
            if (static_cast<std::size_t>(row.alarm) >= lines.size() || !lines[static_cast<std::size_t>(row.alarm)].alarm)
                return ui::EventResult::Consumed;
            const alarmtree::Entry entry = *lines[static_cast<std::size_t>(row.alarm)].alarm;   // copie : le signal peut tout refaire
            selection_ = {object};
            anchor_ = i;
            invalidate();
            selectionChanged->emit(selection_);
            alarmActivated->emit(object, entry.localName.empty() ? entry.name : entry.localName, entry.path);
            return ui::EventResult::Consumed;
        }
        // 1.10.3 (Q1103) : une famille se deplie (la fleche, le double-clic, un clic
        // sur une famille repliee) ; un clic choisit l'objet et va ou va l'arbre.
        if (row.family >= 0) {
            const Id object = row.id;
            const int fam = row.family, line = row.line;
            const float fx = b.x + 6 + static_cast<float>(row.depth) * 16.f;
            if (line < 0) {
                const bool arrow = m->pos.x >= fx && m->pos.x < fx + 12;
                const bool isOpen = familiesOpen_.count({object, fam}) > 0;
                if (arrow || m->clickCount >= 2 || !isOpen) {
                    if (isOpen) familiesOpen_.erase({object, fam}); else familiesOpen_.insert({object, fam});
                    rebuild();
                }
                if (arrow || m->clickCount >= 2) return ui::EventResult::Consumed;
            }
            selection_ = {object};
            anchor_ = i;
            invalidate();
            selectionChanged->emit(selection_);
            lineActivated->emit(object, fam, line);
            return ui::EventResult::Consumed;
        }
        if (m->pos.x >= right - 20) { toggleHidden->emit(row.id); return ui::EventResult::Consumed; }
        if (m->pos.x >= right - 42 && m->pos.x < right - 22) { toggleLocked->emit(row.id); return ui::EventResult::Consumed; }
        const float ex = b.x + 6 + static_cast<float>(row.depth) * 16.f;
        if (row.group && m->pos.x >= ex && m->pos.x < ex + 12) {
            const auto* o = view_ ? view_->object(row.id) : nullptr;
            const bool kids = o && (o->kind == hmi::Kind::Group || !view_->childrenOf(row.id).empty());
            if (!kids) {                                  // 1.10 : un objet qui n'a que ses alarmes
                if (objectsOpen_.count(row.id)) objectsOpen_.erase(row.id); else objectsOpen_.insert(row.id);
            } else if (collapsed_.count(row.id)) collapsed_.erase(row.id); else collapsed_.insert(row.id);
            rebuild();
            return ui::EventResult::Consumed;
        }
        if (m->clickCount >= 2) { activated->emit(row.id); return ui::EventResult::Consumed; }
        if (m->mods.ctrl) {
            auto it = std::find(selection_.begin(), selection_.end(), row.id);
            if (it != selection_.end()) selection_.erase(it); else selection_.push_back(row.id);
            anchor_ = i;
        } else if (m->mods.shift && anchor_ >= 0 && anchor_ < static_cast<int>(rows_.size())) {
            selection_.clear();
            for (int k = std::min(anchor_, i); k <= std::max(anchor_, i); ++k)
                if (rows_[static_cast<std::size_t>(k)].object()) selection_.push_back(rows_[static_cast<std::size_t>(k)].id);
        } else {
            selection_ = {row.id};
            anchor_ = i;
        }
        invalidate();
        selectionChanged->emit(selection_);
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        if (editor_ && k->key == ui::Key::Escape) { endRename(false); return ui::EventResult::Consumed; }
        if (k->key == ui::Key::F2 && selection_.size() == 1) { beginRename(selection_.front()); return ui::EventResult::Consumed; }
    }
    return ui::EventResult::Ignored;
}

// ================================================================ calques ===
HmiLayerList::HmiLayerList(std::string id) : ui::Widget(std::move(id)) {}

ui::SizeHint HmiLayerList::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {220.f, 130.f};
    h.minimum = {120.f, 52.f};
    h.stretchX = 1.f;
    h.stretchY = 1.f;
    return h;
}

void HmiLayerList::setView(const hmi::View* v) {
    view_ = v;
    invalidate();
}

void HmiLayerList::beginRename(Id layer) {
    if (!view_) return;
    endRename();
    const auto n = static_cast<int>(view_->layers.size());
    int row = -1;
    for (int k = 0; k < n; ++k)
        if (view_->layers[static_cast<std::size_t>(n - 1 - k)].id == layer) row = k;
    if (row < 0) return;
    auto input = std::make_unique<RenameField>(this->id() + ".rename");
    input->setText(view_->layer(layer)->name);
    auto* raw = input.get();
    editor_ = &addChild(std::move(input));
    renaming_ = layer;
    const auto b = bounds();
    editor_->setBounds({b.x + 54, b.y + static_cast<float>(row) * rowH_, std::max(60.f, b.w - 64), rowH_});
    raw->editingDone->connect([this](const std::string& text) {
        const Id target = renaming_;
        endRename();
        if (target != kNoId) renamed->emit(target, text);
    }).release();
    raw->focusNow();
    invalidate();
}

void HmiLayerList::endRename() {
    if (!editor_) return;
    auto owned = removeChild(*editor_);
    editor_ = nullptr;
    renaming_ = kNoId;
    static std::vector<ui::WidgetPtr> graveyard;
    graveyard.push_back(std::move(owned));
    if (graveyard.size() > 8) graveyard.erase(graveyard.begin());
    invalidate();
}

void HmiLayerList::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    if (!hovered()) hover_ = -1;          // la souris est partie
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.inputBg);
    if (!view_) return;
    ctx.r.pushClip(b);
    // Du haut vers le bas : le calque du dessus en premier, comme on les voit.
    const auto n = static_cast<int>(view_->layers.size());
    for (int k = 0; k < n; ++k) {
        const auto& l = view_->layers[static_cast<std::size_t>(n - 1 - k)];
        const float y = b.y + static_cast<float>(k) * rowH_;
        const gfx::Rect rr{b.x, y, b.w, rowH_};
        const bool active = l.id == view_->activeLayer;
        if (active) ctx.r.fillRect(rr, c.selectionBg);
        else if (k == hover_) ctx.r.fillRect(rr, c.text.withAlpha(16));
        if (active) ctx.r.fillRect({b.x, y, 3, rowH_}, c.accent);
        const gfx::Color fg = active ? c.selectionText : l.visible ? c.text : c.textMuted;
        drawHmiGlyph(ctx.r, l.visible ? HmiGlyph::Eye : HmiGlyph::EyeOff, {b.x + 8, y + (rowH_ - 16) / 2, 16, 16},
                     l.visible ? fg : c.warning);
        drawHmiGlyph(ctx.r, l.locked ? HmiGlyph::Lock : HmiGlyph::Unlock, {b.x + 30, y + (rowH_ - 16) / 2, 16, 16},
                     l.locked ? c.warning : fg.withAlpha(130));
        std::size_t count = 0;
        for (const auto& o : view_->objects) if (o.layer == l.id) ++count;
        const std::string countText = std::to_string(count);
        const float cw = ctx.r.measure(countText, ctx.theme.font.smallUi).width;
        textCentred(ctx, {b.x + 56, y, b.w - 70 - cw, rowH_}, fit(ctx.r, l.name, active ? ctx.theme.font.uiBold : ctx.theme.font.ui,
                                                                   b.w - 76 - cw),
                    active ? ctx.theme.font.uiBold : ctx.theme.font.ui, fg);
        textCentred(ctx, {b.x + b.w - cw - 10, y, cw + 4, rowH_}, countText, ctx.theme.font.smallUi, c.textMuted);
    }
    ctx.r.popClip();
}

bool HmiLayerList::rowRect(hmi::Id layer, gfx::Rect& out, int part) const {
    if (!view_) return false;
    const auto b = bounds();
    const auto n = static_cast<int>(view_->layers.size());
    for (int k = 0; k < n; ++k) {
        if (view_->layers[static_cast<std::size_t>(n - 1 - k)].id != layer) continue;
        const float y = b.y + static_cast<float>(k) * rowH_;
        if (part == 1)      out = {b.x + 8, y + (rowH_ - 16) / 2, 16, 16};
        else if (part == 2) out = {b.x + 30, y + (rowH_ - 16) / 2, 16, 16};
        else                out = {b.x + 60, y, 80, rowH_};
        return y + rowH_ <= b.y + b.h;
    }
    return false;
}

ui::EventResult HmiLayerList::onEvent(const ui::InputEvent& ev) {
    if (!view_) return ui::EventResult::Ignored;
    const auto b = bounds();
    const auto n = static_cast<int>(view_->layers.size());
    auto rowAt = [&](float y) {
        const int k = static_cast<int>((y - b.y) / rowH_);
        return (y < b.y || k >= n) ? -1 : k;
    };
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos.y);
        if (h != hover_) { hover_ = h; invalidate(); }
        return ui::EventResult::Ignored;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        const int k = rowAt(m->pos.y);
        if (k < 0) return ui::EventResult::Ignored;
        const Id id = view_->layers[static_cast<std::size_t>(n - 1 - k)].id;
        if (m->pos.x < b.x + 26) toggleVisible->emit(id);
        else if (m->pos.x < b.x + 50) toggleLocked->emit(id);
        else if (m->clickCount >= 2) rename->emit(id);
        else activate->emit(id);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ================================================================ palette ===
namespace {
struct Category { const char* name; std::vector<hmi::Kind> kinds; };
const std::vector<Category>& categories() {
    static const std::vector<Category> k = {
        {"Formes",    {hmi::Kind::Rectangle, hmi::Kind::Ellipse, hmi::Kind::Line, hmi::Kind::Polygon, hmi::Kind::Text,
                       hmi::Kind::Image}},
        // Lot 9 : les commandes (impulsion, interrupteur, selecteur...) et les
        // afficheurs (numerique, multi-etats, bargraphe, cadran, horloge...).
        {"Commandes", {hmi::Kind::Button, hmi::Kind::PushButton, hmi::Kind::Switch, hmi::Kind::IlluminatedButton,
                       hmi::Kind::Selector, hmi::Kind::Slider, hmi::Kind::Knob, hmi::Kind::InputField, hmi::Kind::ComboBox,
                       hmi::Kind::CheckBox, hmi::Kind::RadioGroup, hmi::Kind::DateTimePicker, hmi::Kind::WeeklySchedule,
                       hmi::Kind::List, hmi::Kind::SystemButton}},
        {"Affichage", {hmi::Kind::Indicator, hmi::Kind::MultiStateIndicator, hmi::Kind::MultiStateText, hmi::Kind::NumericDisplay,
                       hmi::Kind::SevenSegment, hmi::Kind::ProgressBar, hmi::Kind::Bargraph, hmi::Kind::Gauge, hmi::Kind::Dial,
                       hmi::Kind::Thermometer, hmi::Kind::TrendArrow, hmi::Kind::Clock, hmi::Kind::HourMeter,
                       hmi::Kind::Marquee, hmi::Kind::QrCode, hmi::Kind::Table}},
        // Lot 11 : les graphiques, les objets des alarmes, la production.
        {"Graphiques", {hmi::Kind::Trend, hmi::Kind::BarChart, hmi::Kind::XYChart, hmi::Kind::StateChart, hmi::Kind::PieChart,
                        hmi::Kind::RadarChart, hmi::Kind::Histogram}},
        {"Alarmes", {hmi::Kind::History, hmi::Kind::AlarmBanner, hmi::Kind::AlarmCounter, hmi::Kind::AlarmSummary,
                     hmi::Kind::AlarmInstruction, hmi::Kind::AlarmStats}},
        {"Production", {hmi::Kind::ProductionCounter, hmi::Kind::VariableTable, hmi::Kind::RecipeEditor, hmi::Kind::RecipeManager,
                        hmi::Kind::ExportButton}},
        // Lot 10 : les symboles de synoptique - process, stockage, electrique.
        {"Synoptique", {hmi::Kind::Valve, hmi::Kind::ThreeWayValve, hmi::Kind::CheckValve, hmi::Kind::Pump, hmi::Kind::Motor, hmi::Kind::Pipe, hmi::Kind::FlowArrow,
                        hmi::Kind::Fan, hmi::Kind::Compressor, hmi::Kind::HeatExchanger, hmi::Kind::Filter, hmi::Kind::Boiler,
                        hmi::Kind::Mixer, hmi::Kind::Conveyor, hmi::Kind::Cylinder, hmi::Kind::IsaInstrument, hmi::Kind::Tank,
                        hmi::Kind::Silo, hmi::Kind::Hopper, hmi::Kind::GasBottle, hmi::Kind::Transformer, hmi::Kind::CircuitBreaker,
                        hmi::Kind::Disconnector, hmi::Kind::Contactor, hmi::Kind::Lamp}},
        // Lot 12 : la navigation et la structure (le conteneur y rejoint les nouveaux).
        {"Navigation et structure", {hmi::Kind::NavBar, hmi::Kind::Breadcrumb, hmi::Kind::TabContainer, hmi::Kind::Frame,
                                     hmi::Kind::ScrollPanel, hmi::Kind::CollapsiblePanel, hmi::Kind::ZoneMap,
                                     hmi::Kind::LanguageSelector, hmi::Kind::ThemeSelector,  // lot 13 : la langue, le theme
                                     hmi::Kind::Container}},
        // Lot 14 : la communication avec l'automate reel.
        {"Communication", {hmi::Kind::CommStatus, hmi::Kind::PlcDiagnostic}},
        {"M\xC3\xA9" "dias", {hmi::Kind::Video, hmi::Kind::AnimatedImage, hmi::Kind::AnimatedGif}},
        // Lot 8 : se connecter, se deconnecter, voir qui est connecte, changer son
        // mot de passe, gerer les comptes.
        {"Utilisateurs", {hmi::Kind::LoginMenuButton, hmi::Kind::LoginPanel, hmi::Kind::LogoutButton, hmi::Kind::UserInfo,
                          hmi::Kind::PasswordChange, hmi::Kind::UserManager}},
    };
    return k;
}
} // namespace

HmiPalette::HmiPalette(std::string id) : ui::Widget(std::move(id)) {
    // 1.11.1 (R1111-6) : l'arbre des variables de l'automate, montre dans
    // l'onglet Variables des qu'un programme est la (setApiModel).
    apiTree_ = &static_cast<HmiApiVarsView&>(addChild(std::make_unique<HmiApiVarsView>(this->id() + ".apivars")));
    apiTree_->setCompact(true);
    apiTree_->setVisibility(ui::Visibility::Collapsed);
    const auto choose = [this](const ApiVarNode& n) {
        auto info = infoOf(n);
        currentVariable_ = info.name;
        treeChoice_ = info;
        chosenVariable->emit(info);
        invalidate();
    };
    treeLinks_ += apiTree_->picked->connect(choose);
    // Le double-clic (insertName) : la meme chose qu'un clic (il n'y a pas de script ici).
    treeLinks_ += apiTree_->insertName->connect([this, choose](const std::string&) {
        if (const auto* n = apiTree_->selectedNode()) choose(*n);
    });
}

void HmiPalette::onLayout() {
    const auto b = bounds();
    const float helpH = 64.f;
    apiTree_->setBounds({b.x + 2.f, b.y + kModeH, std::max(0.f, b.w - 4.f), std::max(0.f, b.h - kModeH - helpH)});
    apiTree_->setVisibility(treeShown() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
}

void HmiPalette::setApiModel(std::shared_ptr<const hmi::apivars::Model> model) {
    apiModel_ = std::move(model);
    if (apiModel_ && project_) {
        // Les variables IHM, apres les unites : telles que la liste d'avant les donnait.
        std::vector<ApiVarNode> extra;
        ApiVarNode group;
        group.kind = ApiVarNode::Kind::Group;
        group.name = "Variables IHM";
        group.key = "#Variables IHM";
        for (const auto& v : variables_) {
            if (v.plc) continue;
            ApiVarNode n;
            n.kind = v.members.empty() ? ApiVarNode::Kind::Variable : ApiVarNode::Kind::Structure;
            n.name = n.path = n.key = v.name;
            n.type = v.type;
            n.scope = "Variable IHM";
            n.hmi = true;
            for (const auto& m : v.members) {
                ApiVarNode c;
                c.name = m.name;
                c.path = c.key = v.name + "." + m.name;
                c.type = m.type;
                c.scope = "Membre";
                c.hmi = true;
                n.children.push_back(std::move(c));
            }
            group.children.push_back(std::move(n));
        }
        if (!group.children.empty()) extra.push_back(std::move(group));
        (void)bindApiModel(*apiTree_, apiModel_, *project_, std::move(extra));
    }
    invalidateLayout();
    invalidate();
}

// Une ligne de l'arbre, telle que la vue la pose : une globale connue de la
// liste d'avant garde son commentaire (le libelle) et ses membres, avec son nom
// API.… ; une variable d'unite, un membre : leur type, les noms de leurs membres.
hmi::design::VarInfo HmiPalette::infoOf(const ApiVarNode& n) const {
    const std::string bare = n.path.rfind("API.", 0) == 0 ? n.path.substr(4) : n.path;
    for (const auto& v : variables_)
        if (v.name == bare && v.plc != n.hmi) {
            auto info = v;
            info.name = n.path;
            return info;
        }
    hmi::design::VarInfo info;
    info.name = n.path;
    info.type = n.type;
    info.plc = !n.hmi;
    if (n.kind == ApiVarNode::Kind::Structure)
        for (const auto& c : apiTree_->childrenOf(n))
            if (c.kind != ApiVarNode::Kind::More) info.members.push_back({c.name, c.type, {}, !n.hmi, {}});
    return info;
}

ui::SizeHint HmiPalette::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {220.f, 300.f};
    h.minimum = {110.f, 80.f};
    h.stretchX = 1.f;
    h.stretchY = 1.f;
    return h;
}

void HmiPalette::setFilter(std::string text) {
    filter_ = lower(std::move(text));
    scrollY_ = 0;
    invalidate();
}

void HmiPalette::layoutTiles(const ui::Theme*) const {
    tiles_.clear();
    headings_.clear();
    const auto b = bounds();
    const float tileW = 92, tileH = 62, gap = 6;
    const int perRow = std::max(1, static_cast<int>((b.w - 12 + gap) / (tileW + gap)));
    float y = b.y + 6 + kModeH - scrollY_;       // lot 12 : sous les onglets Objets | Variables
    auto section = [&](const std::string& title, const std::vector<hmi::Kind>& kinds) {
        std::vector<hmi::Kind> shown;
        for (auto k : kinds)
            if (filter_.empty() || lower(std::string(hmi::kindLabel(k))).find(filter_) != std::string::npos
                || lower(title).find(filter_) != std::string::npos)
                shown.push_back(k);
        if (shown.empty()) return;
        headings_.emplace_back(title, y);
        y += 22;
        for (std::size_t i = 0; i < shown.size(); ++i) {
            const int col = static_cast<int>(i) % perRow;
            if (i > 0 && col == 0) y += tileH + gap;
            const gfx::Rect r{b.x + 6 + static_cast<float>(col) * (tileW + gap), y, tileW, tileH};
            tiles_.push_back({shown[i], r, {r.x + r.w - 18, r.y + 3, 15, 15}, {}});
        }
        y += tileH + 10;
    };
    std::vector<hmi::Kind> favs(favorites_.begin(), favorites_.end());
    section("Favoris", favs);
    // Lot 10 : les symboles du projet (des vues de role symbole), apres les favoris.
    emptySymbolsY_ = -1.f;
    if (project_) {
        const std::string title = "Symboles du projet";
        std::vector<const hmi::View*> shown;
        for (const auto* sv : hmi::symbolsOf(*project_))
            if (filter_.empty() || lower(sv->name).find(filter_) != std::string::npos || lower(title).find(filter_) != std::string::npos
                || lower(std::string("symbole")).find(filter_) != std::string::npos)
                shown.push_back(sv);
        if (!shown.empty() || filter_.empty()) {
            headings_.emplace_back(title, y);
            y += 22;
            if (shown.empty()) {
                emptySymbolsY_ = y;          // "aucun symbole" : une ligne d'aide a la place des tuiles
                y += 34;
            }
            for (std::size_t i = 0; i < shown.size(); ++i) {
                const int col = static_cast<int>(i) % perRow;
                if (i > 0 && col == 0) y += tileH + gap;
                const gfx::Rect r{b.x + 6 + static_cast<float>(col) * (tileW + gap), y, tileW, tileH};
                tiles_.push_back({hmi::Kind::SymbolInstance, r, {}, shown[i]->name});
            }
            if (!shown.empty()) y += tileH + 10;
        }
    }
    // 1.10.2 (chantier D) : LES MODELES D'OBJETS (les vues du dossier "Modeles
    // d'objets" et de ses sous-dossiers), une tuile par vue, apres les symboles.
    // Glissee dans une vue (ou choisie, puis un clic), elle y copie ses objets et
    // ouvre "Dupliquer..." (HmiCanvas::placeAt). Aucun modele : pas de section.
    if (project_) {
        const std::string title(hmi::dup::kTemplatesFolder);
        std::vector<const hmi::View*> models;
        for (const auto& tv : project_->views)
            if (!hmi::isSymbolView(tv) && hmi::dup::isTemplatesFolder(tv.folder)
                && (filter_.empty() || lower(tv.name).find(filter_) != std::string::npos || lower(title).find(filter_) != std::string::npos))
                models.push_back(&tv);
        if (!models.empty()) {
            headings_.emplace_back(title, y);
            y += 22;
            for (std::size_t i = 0; i < models.size(); ++i) {
                const int col = static_cast<int>(i) % perRow;
                if (i > 0 && col == 0) y += tileH + gap;
                const gfx::Rect r{b.x + 6 + static_cast<float>(col) * (tileW + gap), y, tileW, tileH};
                tiles_.push_back({hmi::Kind::SymbolInstance, r, {}, models[i]->name});
            }
            y += tileH + 10;
        }
    }
    for (const auto& c : categories()) section(c.name, c.kinds);
}

// 1.11 (tutoriels, T1) : la place gardee en bas pour l'apercu. Quand la bibliotheque est
// basse (1600 x 900 : environ 245 px), kModeH + kPreviewH ne laissait pas la place d'une
// tuile : revealKind la posait sous les onglets, tileRect ne l'y voyait jamais, et le
// tutoriel de la vanne ne la trouvait pas. L'apercu ne s'ouvre qu'au survol : on l'ignore.
// (Une fonction du fichier : HmiPanels.hpp est lu par tout app/hmi.)
static float previewRoom(float paletteH, float tileH, float modeH, float previewH) {
    return paletteH - modeH - previewH >= tileH + 10.f ? previewH : 0.f;
}

bool HmiPalette::symbolTileRect(const std::string& name, gfx::Rect& out) const {
    layoutTiles(nullptr);
    const auto b = bounds();
    for (const auto& t : tiles_)
        if (t.symbol == name && t.rect.y >= b.y + kModeH && t.rect.y + t.rect.h <= b.y + b.h - previewRoom(b.h, t.rect.h, kModeH, kPreviewH)) {
            out = t.rect;
            return true;
        }
    return false;
}

void HmiPalette::revealSymbol(const std::string& name) {
    gfx::Rect r;
    if (symbolTileRect(name, r)) return;
    const auto b = bounds();
    for (const auto& t : tiles_)
        if (t.symbol == name) {
            // Sous les onglets Objets | Variables, avec une marge (un calcul en
            // flottants qui tombait pile sur leur bord la laissait cachee).
            scrollY_ = std::max(0.f, scrollY_ + (t.rect.y - b.y) - (kModeH + 10.f));
            invalidate();
            return;
        }
}

bool HmiPalette::tileRect(hmi::Kind kind, gfx::Rect& out) const {
    layoutTiles(nullptr);
    const auto b = bounds();
    for (const auto& t : tiles_)
        if (t.kind == kind && t.symbol.empty() && t.rect.y >= b.y + kModeH
            && t.rect.y + t.rect.h <= b.y + b.h - previewRoom(b.h, t.rect.h, kModeH, kPreviewH)) {
            out = t.rect;
            return true;
        }
    return false;
}

void HmiPalette::revealKind(hmi::Kind kind) {
    gfx::Rect r;
    if (tileRect(kind, r)) return;
    const auto b = bounds();
    for (const auto& t : tiles_)
        if (t.kind == kind && t.symbol.empty()) {
            // Au-dessus de la zone d'apercu : le survol la fait apparaitre. Sous
            // les onglets Objets | Variables, avec une marge.
            scrollY_ = std::max(0.f, scrollY_ + (t.rect.y - b.y) - (kModeH + 10.f));
            invalidate();
            return;
        }
}

bool HmiPalette::modeRect(Mode m, gfx::Rect& out) const {
    const auto b = bounds();
    const float w = (b.w - 12.f) / 2.f;
    out = {b.x + 6.f + (m == Mode::Variables ? w : 0.f), b.y + 4.f, w, kModeH - 8.f};
    return b.w > 20.f;
}

void HmiPalette::setMode(Mode m) {
    if (m == mode_) return;
    mode_ = m;
    hoverKind_.reset();
    hoverSymbol_.clear();
    hoverVariable_.clear();
    if (m == Mode::Variables) variablesWanted->emit();
    invalidateLayout();      // R1111-6 : l'arbre se montre (ou s'en va) avec l'onglet
    invalidate();
}

void HmiPalette::layoutVariables() const {
    varRows_.clear();
    const auto b = bounds();
    float y = b.y + kModeH + 4.f - varScrollY_;
    for (std::size_t i = 0; i < variables_.size(); ++i) {
        const auto& v = variables_[i];
        if (!filter_.empty() && lower(v.name).find(filter_) == std::string::npos && lower(v.type).find(filter_) == std::string::npos
            && lower(v.comment).find(filter_) == std::string::npos)
            continue;
        varRows_.push_back({i, {b.x + 4.f, y, b.w - 8.f, 26.f}});
        y += 28.f;
    }
}

bool HmiPalette::variableRect(const std::string& name, gfx::Rect& out) const {
    if (treeShown()) return apiTree_->nameRect(name, out) || apiTree_->nameRect("API." + name, out);   // R1111-6
    layoutVariables();
    const auto b = bounds();
    for (const auto& r : varRows_)
        if (variables_[r.index].name == name && r.rect.y >= b.y + kModeH && r.rect.y + r.rect.h <= b.y + b.h - 70.f) {
            out = r.rect;
            return true;
        }
    return false;
}

void HmiPalette::revealVariable(const std::string& name) {
    gfx::Rect r;
    if (treeShown()) {                  // R1111-6 : ses dossiers se deplient, la ligne choisie
        if (!apiTree_->selectName(name)) (void)apiTree_->selectName("API." + name);
        return;
    }
    if (variableRect(name, r)) return;
    const auto b = bounds();
    for (const auto& row : varRows_)
        if (variables_[row.index].name == name) {
            varScrollY_ = std::max(0.f, varScrollY_ + (row.rect.y - b.y - kModeH) - 30.f);
            invalidate();
            return;
        }
}

void HmiPalette::paintVariables(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    layoutVariables();
    const float helpH = 64.f;
    const bool tree = treeShown();      // R1111-6 : l'arbre (un enfant) se dessine lui-meme
    ctx.r.pushClip({b.x, b.y + kModeH, b.w, tree ? 0.f : b.h - kModeH - helpH});
    if (variables_.empty() && !tree)
        ctx.r.drawText({b.x + 10, b.y + kModeH + 10}, "Aucune variable : ouvre un programme d'automate,", ctx.theme.font.smallUi,
                       c.textMuted);
    for (const auto& row : varRows_) {
        const auto& v = variables_[row.index];
        const auto& r = row.rect;
        if (r.y + r.h < b.y + kModeH || r.y > b.y + b.h) continue;
        const bool cur = v.name == currentVariable_;
        const bool hot = v.name == hoverVariable_;
        ctx.r.fillRoundedRect(r, cur ? c.accent.withAlpha(70) : hot ? c.text.withAlpha(22) : c.inputBg, 4);
        const auto kind = hmi::design::kindForVariable(v);
        drawHmiGlyph(ctx.r, glyphFor(kind), {r.x + 5, r.y + 5, 16, 16}, cur ? c.accent : v.plc ? c.text : c.warning);
        const std::string right = v.members.empty() ? v.type : v.type + " \xC2\xB7 " + std::to_string(v.members.size()) + (v.members.size() == 1 ? " membre" : " membres");   // 1.11 (R111, T3-11)
        const float rw = std::min(r.w * 0.45f, ctx.r.measure(right, ctx.theme.font.smallUi).width + 4.f);
        const std::string name = fit(ctx.r, v.name, ctx.theme.font.smallUi, r.w - rw - 34.f);
        ctx.r.drawText({r.x + 26, r.y + 5}, name, ctx.theme.font.smallUi, cur ? c.accent : c.text);
        ctx.r.drawText({r.x + r.w - rw - 4, r.y + 5}, fit(ctx.r, right, ctx.theme.font.smallUi, rw), ctx.theme.font.smallUi, c.textMuted);
    }
    ctx.r.popClip();
    // L'aide : ce que devient la variable survolee (ou choisie).
    const gfx::Rect pr{b.x, b.y + b.h - helpH, b.w, helpH};
    ctx.r.fillRect(pr, c.headerBg);
    ctx.r.fillRect({pr.x, pr.y, pr.w, 1}, c.border);
    const hmi::design::VarInfo* shown = nullptr;
    for (const auto& v : variables_)
        if (v.name == (hoverVariable_.empty() ? currentVariable_ : hoverVariable_)) shown = &v;
    if (tree && treeChoice_ && treeChoice_->name == currentVariable_) shown = &*treeChoice_;
    std::string l1 = "Glisse une variable dans la vue (ou clique-la,", l2 = "puis clique dans la vue) : l'objet qui lui va.";
    if (shown) {
        const auto kind = hmi::design::kindForVariable(*shown);
        l1 = shown->name + " : " + shown->type;
        l2 = hmi::design::shapeOf(*shown) == hmi::design::VarShape::Structure
               ? "\xE2\x86\x92 un bouton qui ouvre " + hmi::design::equipmentPopupName(shown->type)
               : "\xE2\x86\x92 " + std::string(hmi::kindLabel(kind));
    }
    ctx.r.drawText({pr.x + 8, pr.y + 10}, fit(ctx.r, l1, ctx.theme.font.smallUi, pr.w - 16), ctx.theme.font.smallUi, c.text);
    ctx.r.drawText({pr.x + 8, pr.y + 34}, fit(ctx.r, l2, ctx.theme.font.smallUi, pr.w - 16), ctx.theme.font.smallUi, c.textMuted);
}

void HmiPalette::onPaint(const ui::PaintContext& ctx) {
    const auto b = bounds();
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(b, c.panelBg);
    // Lot 12 : les onglets Objets | Variables.
    for (const Mode m : {Mode::Objects, Mode::Variables}) {
        gfx::Rect r;
        if (!modeRect(m, r)) continue;
        const bool on = m == mode_;
        ctx.r.fillRoundedRect(r, on ? c.accent.withAlpha(60) : c.inputBg, 4);
        if (on) ctx.r.fillRect({r.x + 4, r.y + r.h - 2, r.w - 8, 2}, c.accent);
        const std::string label = m == Mode::Objects ? "Objets" : "Variables";
        const float lw = ctx.r.measure(label, ctx.theme.font.smallUi).width;
        ctx.r.drawText({r.x + (r.w - lw) / 2, r.y + (r.h - ctx.r.lineHeight(ctx.theme.font.smallUi)) / 2}, label, ctx.theme.font.smallUi,
                       on ? c.text : c.textMuted);
    }
    if (mode_ == Mode::Variables) {
        if (!hovered()) hoverVariable_.clear();
        paintVariables(ctx);
        return;
    }
    // La souris est partie : l'apercu s'en va avec elle.
    if (!hovered()) { hoverKind_.reset(); hoverSymbol_.clear(); }
    layoutTiles(&ctx.theme);
    // Pas de defilement au-dela de la derniere tuile - sauf la hauteur de
    // l'apercu : sans elle, les tuiles du bas restaient sous l'apercu qui
    // apparait justement quand on les survole.
    if (!tiles_.empty()) {
        const float bottom = tiles_.back().rect.y + tiles_.back().rect.h + 10.f;
        const float over = b.y + b.h - kPreviewH - bottom;
        if (over > 0.f && scrollY_ > 0.f) {
            scrollY_ = std::max(0.f, scrollY_ - over);
            layoutTiles(&ctx.theme);
        }
    }
    // L'apercu occupe le bas quand une tuile est survolee.
    const float previewH = hoverKind_ ? kPreviewH : 0.f;
    ctx.r.pushClip({b.x, b.y + kModeH, b.w, b.h - previewH - kModeH});
    for (const auto& [title, y] : headings_)
        ctx.r.drawText({b.x + 8, y + 2}, title, ctx.theme.font.smallUi, c.textMuted);
    if (emptySymbolsY_ >= 0.f) {
        ctx.r.drawText({b.x + 10, emptySymbolsY_}, "Aucun symbole : choisis des objets dans une vue,",
                       ctx.theme.font.smallUi, c.textMuted.withAlpha(170));
        ctx.r.drawText({b.x + 10, emptySymbolsY_ + 15}, "puis \xC2\xAB Cr\xC3\xA9" "er un symbole \xC2\xBB (barre d'outils).",
                       ctx.theme.font.smallUi, c.textMuted.withAlpha(170));
    }
    for (const auto& t : tiles_) {
        if (!t.symbol.empty()) {
            // Lot 10 : un symbole du projet - son dessin, son nom.
            const bool cur = current_ && *current_ == hmi::Kind::SymbolInstance && currentSymbol_ == t.symbol;
            const bool hot = hoverSymbol_ == t.symbol;
            ctx.r.fillRoundedRect(t.rect, cur ? c.accent.withAlpha(70) : hot ? c.text.withAlpha(22) : c.inputBg, 5);
            ctx.r.strokeRect(t.rect, cur ? c.accent : gfx::Color{185, 140, 255, 150}, 1);
            if (const auto* sv = project_ ? project_->viewByName(t.symbol) : nullptr)
                paintHmiSymbolPreview(ctx.r, *project_, *sv, {t.rect.x + 5, t.rect.y + 5, t.rect.w - 10, t.rect.h - 27}, ctx.theme);
            drawHmiGlyph(ctx.r, HmiGlyph::Symbol, {t.rect.x + 3, t.rect.y + t.rect.h - 19, 12, 12}, gfx::Color{185, 140, 255, 220});
            const std::string label = fit(ctx.r, t.symbol, ctx.theme.font.smallUi, t.rect.w - 20);
            const float lw = ctx.r.measure(label, ctx.theme.font.smallUi).width;
            ctx.r.drawText({t.rect.x + 8 + (t.rect.w - 8 - lw) / 2, t.rect.y + t.rect.h - 20}, label, ctx.theme.font.smallUi,
                           cur ? c.accent : c.text);
            if (const auto* sa = project_ ? project_->viewByName(t.symbol) : nullptr)   // 1.9 : ses alarmes
                objalarms::paintBell(ctx, static_cast<int>(sa->alarms.size()), t.rect);
            continue;
        }
        const bool cur = current_ && *current_ == t.kind;
        const bool hot = hoverKind_ && *hoverKind_ == t.kind && hoverSymbol_.empty();
        ctx.r.fillRoundedRect(t.rect, cur ? c.accent.withAlpha(70) : hot ? c.text.withAlpha(22) : c.inputBg, 5);
        ctx.r.strokeRect(t.rect, cur ? c.accent : c.border, 1);
        drawHmiGlyph(ctx.r, glyphFor(t.kind), {t.rect.x + t.rect.w / 2 - 13, t.rect.y + 8, 26, 26}, cur ? c.accent : c.text);
        const std::string label = fit(ctx.r, std::string(hmi::kindLabel(t.kind)), ctx.theme.font.smallUi, t.rect.w - 6);
        const float lw = ctx.r.measure(label, ctx.theme.font.smallUi).width;
        ctx.r.drawText({t.rect.x + (t.rect.w - lw) / 2, t.rect.y + t.rect.h - 20}, label, ctx.theme.font.smallUi,
                       cur ? c.accent : c.text);
        const bool fav = favorites_.count(t.kind) != 0;
        if (fav || hot) drawHmiGlyph(ctx.r, fav ? HmiGlyph::StarFilled : HmiGlyph::Star, t.star, fav ? c.warning : c.textMuted);
        objalarms::paintBell(ctx, objalarms::libraryAlarmCount(t.kind), t.rect);   // 1.9 : les alarmes par defaut (A5)
    }
    ctx.r.popClip();
    if (hoverKind_ && !hoverSymbol_.empty()) {
        // Lot 10 : l'apercu d'un symbole - son dessin, ses parametres.
        const gfx::Rect pr{b.x, b.y + b.h - previewH, b.w, previewH};
        ctx.r.fillRect(pr, c.headerBg);
        ctx.r.fillRect({pr.x, pr.y, pr.w, 1}, c.border);
        const auto* sv = project_ ? project_->viewByName(hoverSymbol_) : nullptr;
        // 1.10.2 (D) : un modele d'objets (dossier "Modeles d'objets") se glisse dans une vue.
        const bool model = sv && !hmi::isSymbolView(*sv) && hmi::dup::isTemplatesFolder(sv->folder);
        std::string head = std::string(model ? "Mod\xC3\xA8le d'objets (glisse-le dans une vue) : " : "Symbole : ") + hoverSymbol_;
        if (sv) {
            head += "  (" + std::to_string(sv->width) + " x " + std::to_string(sv->height);
            if (!sv->params.empty()) {
                std::string names;
                for (const auto& prm : sv->params) names += (names.empty() ? "" : ", ") + prm.name;
                head += " ; " + names;
            }
            head += ")";
        }
        ctx.r.drawText({pr.x + 8, pr.y + 6}, fit(ctx.r, head, ctx.theme.font.smallUi, pr.w - 16), ctx.theme.font.smallUi, c.textMuted);
        const gfx::Rect well{pr.x + 8, pr.y + 24, pr.w - 16, pr.h - 30};
        ctx.r.fillRect(well, gfx::Color::rgb(0x20242B));
        if (sv) {
            ctx.r.pushClip(well);
            paintHmiSymbolPreview(ctx.r, *project_, *sv, {well.x + 6, well.y + 6, well.w - 12, well.h - 12}, ctx.theme);
            ctx.r.popClip();
        }
    } else if (hoverKind_) {
        // L'apercu : l'objet tel qu'il sera pose, dessine par le vrai painter.
        const gfx::Rect pr{b.x, b.y + b.h - previewH, b.w, previewH};
        ctx.r.fillRect(pr, c.headerBg);
        ctx.r.fillRect({pr.x, pr.y, pr.w, 1}, c.border);
        ctx.r.drawText({pr.x + 8, pr.y + 6}, "Aper\xC3\xA7u : " + std::string(hmi::kindLabel(*hoverKind_)),
                       ctx.theme.font.smallUi, c.textMuted);
        hmi::View v;
        v.width = 1;
        v.height = 1;
        hmi::Layer l;
        l.id = 1;
        v.layers.push_back(l);
        auto o = hmi::makeObject(*hoverKind_, 2, "apercu", 0, 0, 1);
        const hmi::Box ob = o.box();
        const double w = std::max(1.0, ob.w), h = std::max(1.0, ob.h);
        const float zoom = static_cast<float>(std::min((pr.w - 24) / w, (pr.h - 34) / h));
        const float z = std::min(zoom, 1.5f);
        HmiViewport vp{static_cast<float>(pr.x + (pr.w - w * z) / 2), static_cast<float>(pr.y + 26 + (pr.h - 30 - h * z) / 2), z};
        if (*hoverKind_ == hmi::Kind::Line) vp.originY = pr.y + pr.h / 2 + 6;
        const gfx::Rect well{pr.x + 8, pr.y + 24, pr.w - 16, pr.h - 30};
        ctx.r.fillRect(well, gfx::Color::rgb(0x20242B));
        ctx.r.pushClip(well);
        HmiPropertySource src;
        paintHmiObject(ctx.r, v, o, vp, src, ctx.theme, {});
        ctx.r.popClip();
    }
}

ui::EventResult HmiPalette::onEvent(const ui::InputEvent& ev) {
    // Lot 12 : les onglets Objets | Variables, puis la liste des variables.
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        for (const Mode mode : {Mode::Objects, Mode::Variables}) {
            gfx::Rect r;
            if (modeRect(mode, r) && r.contains(m->pos)) {
                setMode(mode);
                return ui::EventResult::Consumed;
            }
        }
    }
    if (mode_ == Mode::Variables) {
        if (treeShown()) return ui::EventResult::Ignored;   // R1111-6 : l'arbre (un enfant) a deja vu l'evenement
        layoutVariables();
        auto rowAt = [&](gfx::Point p) -> const VarRow* {
            if (p.y < bounds().y + kModeH || p.y > bounds().y + bounds().h - 64.f) return nullptr;
            for (const auto& r : varRows_) if (r.rect.contains(p)) return &r;
            return nullptr;
        };
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            const auto* r = rowAt(m->pos);
            const std::string name = r ? variables_[r->index].name : std::string{};
            if (name != hoverVariable_) {
                hoverVariable_ = name;
                setTooltip(r ? "Glisse-la dans la vue (ou clique-la, puis clique dans la vue) : un BOOL devient un voyant, un nombre un "
                               "afficheur, un texte un texte \xC3\xA0 trous, une structure un bouton qui ouvre sa popup d'\xC3\xA9quipement "
                               "(g\xC3\xA9n\xC3\xA9r\xC3\xA9" "e au besoin)."
                             : std::string{});
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            varScrollY_ = std::max(0.f, varScrollY_ - w->dy * 40);
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
            const auto* r = rowAt(m->pos);
            if (!r) return ui::EventResult::Ignored;
            currentVariable_ = variables_[r->index].name;
            chosenVariable->emit(variables_[r->index]);
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }
    auto tileAt = [&](gfx::Point p) -> const Tile* {
        for (const auto& t : tiles_)
            if (p.x >= t.rect.x && p.x < t.rect.x + t.rect.w && p.y >= t.rect.y && p.y < t.rect.y + t.rect.h) return &t;
        return nullptr;
    };
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const auto* t = tileAt(m->pos);
        const std::optional<hmi::Kind> k = t ? std::optional<hmi::Kind>(t->kind) : std::nullopt;
        const std::string sym = t ? t->symbol : std::string{};
        if (k != hoverKind_ || sym != hoverSymbol_) {
            hoverKind_ = k;
            hoverSymbol_ = sym;
            if (!sym.empty())
                setTooltip("Clique puis clique dans la vue pour poser une instance de " + sym
                           + ". Ses arguments (propri\xC3\xA9t\xC3\xA9 Arguments) la relient \xC3\xA0 ses variables. "
                             "Double-clic sur une instance : ouvrir le symbole.");
            else
                setTooltip(k ? "Clique puis clique dans la vue pour poser : " + std::string(hmi::kindLabel(*k))
                                 + ". L'\xC3\xA9toile l'ajoute aux favoris. F1 : sa page d'aide, avec un exemple anim\xC3\xA9."
                             : std::string{});
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        scrollY_ = std::max(0.f, scrollY_ - w->dy * 40);
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseDown>(&ev)) {
        const auto* t = tileAt(m->pos);
        if (!t) return ui::EventResult::Ignored;
        if (!t->symbol.empty()) {
            const std::string name = t->symbol;
            current_ = hmi::Kind::SymbolInstance;
            currentSymbol_ = name;
            chosenSymbol->emit(name);
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (m->pos.x >= t->star.x && m->pos.y <= t->star.y + t->star.h) {
            if (favorites_.count(t->kind)) favorites_.erase(t->kind); else favorites_.insert(t->kind);
            favoritesChanged->emit();
            invalidate();
            return ui::EventResult::Consumed;
        }
        current_ = t->kind;
        chosen->emit(t->kind);
        invalidate();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ============================================================= proprietes ===
std::string plcAddressOf(const domain::Project* plc, const std::string& variable) {
    if (!plc || variable.empty()) return {};
    std::size_t end = 0;
    while (end < variable.size() && (std::isalnum(static_cast<unsigned char>(variable[end])) || variable[end] == '_')) ++end;
    const std::string root = variable.substr(0, end);
    if (root.empty()) return {};
    for (const auto& v : plc->variables) {
        if (plc->strings.text(v.name) != root) continue;
        if (v.address.valid()) return v.address.raw;
        return "(non localis\xC3\xA9" "e : " + std::string(plc->strings.text(v.type.name)) + ")";
    }
    return "(inconnue dans le programme)";
}

namespace {

using PG = ui::PropertyGrid;

struct KeyInfo { const char* key; const char* label; PG::ValueType type; std::vector<std::string> choices; const char* help; };

const std::vector<KeyInfo>& keyInfos() {
    static const std::vector<KeyInfo> k = {
        {"x", "X", PG::ValueType::Real, {}, "Position du coin haut-gauche (cadre non tourn\xC3\xA9)."},
        {"y", "Y", PG::ValueType::Real, {}, ""},
        {"w", "Largeur", PG::ValueType::Real, {}, ""},
        {"h", "Hauteur", PG::ValueType::Real, {}, ""},
        {"rot", "Rotation (\xC2\xB0)", PG::ValueType::Real, {}, "Sens horaire, autour du pivot. 45, 90, 180, 270 ou toute valeur."},
        {"pivotX", "Pivot X (0 \xC3\xA0 1)", PG::ValueType::Real, {}, "0,5 = centre. Le pivot reste en place quand on tourne."},
        {"pivotY", "Pivot Y (0 \xC3\xA0 1)", PG::ValueType::Real, {}, ""},
        {"flipH", "Miroir horizontal", PG::ValueType::Boolean, {}, ""},
        {"flipV", "Miroir vertical", PG::ValueType::Boolean, {}, ""},
        {"fill", "Remplissage", PG::ValueType::Color, {}, "#RRGGBB, #RRGGBBAA ou vide (transparent)."},
        {"stroke", "Contour", PG::ValueType::Color, {}, ""},
        {"strokeWidth", "\xC3\x89paisseur du contour", PG::ValueType::Real, {}, ""},
        {"radius", "Rayon des coins", PG::ValueType::Real, {}, ""},
        {"opacity", "Opacit\xC3\xA9 (%)", PG::ValueType::Real, {}, "100 = opaque, 0 = transparent."},
        {"visible", "Visible", PG::ValueType::Boolean, {}, "En marche. Une expression (=Marche AND Defaut) la pilote."},
        {"text", "Texte", PG::ValueType::Text, {}, "Statique, ou dynamique : les trous {Variable} ou {Expression:0.0} sont remplis en marche "
                                                   "(format\xC3\xA9 : {T:0.00}, {N:000}, {P:0.0%}, {H:X4}, {D:t}, {B:Oui|Non}). \\n : une nouvelle ligne."},
        {"font", "Police", PG::ValueType::Enum, {"Sans", "Mono"}, ""},
        {"fontSize", "Taille du texte", PG::ValueType::Real, {}, ""},
        {"textColor", "Couleur du texte", PG::ValueType::Color, {}, ""},
        {"align", "Alignement", PG::ValueType::Enum, {"gauche", "centre", "droite"}, ""},
        {"wrap", "Retour \xC3\xA0 la ligne", PG::ValueType::Boolean, {}, "Multiligne : le texte passe \xC3\xA0 la ligne au bord de l'objet."},
        {"value", "Valeur", PG::ValueType::Text, {}, "La valeur affich\xC3\xA9" "e ; une expression la relie \xC3\xA0 l'automate."},
        {"min", "Minimum", PG::ValueType::Real, {}, ""},
        {"max", "Maximum", PG::ValueType::Real, {}, ""},
        {"unit", "Unit\xC3\xA9", PG::ValueType::Text, {}, ""},
        {"colorOn", "Couleur allum\xC3\xA9", PG::ValueType::Color, {}, ""},
        {"colorOff", "Couleur \xC3\xA9teint", PG::ValueType::Color, {}, ""},
        {"shape", "Forme", PG::ValueType::Enum, {"rond", "carr\xC3\xA9"}, ""},
        {"orientation", "Orientation", PG::ValueType::Enum, {"horizontale", "verticale"}, ""},
        {"background", "Fond", PG::ValueType::Color, {}, ""},
        {"columns", "Colonnes (a;b;c)", PG::ValueType::Text, {}, ""},
        {"rows", "Lignes", PG::ValueType::Integer, {}, ""},
        {"items", "\xC3\x89l\xC3\xA9ments (a;b;c)", PG::ValueType::Text, {}, ""},
        {"image", "Image (ressource)", PG::ValueType::Text, {}, "Une image du gestionnaire de ressources. Une expression la choisit en marche (\xC3\xA9tat graphique) : SEL(Ouvert, 'vanne_fermee.png', 'vanne_ouverte.png')."},
        {"poster", "Image d'attente", PG::ValueType::Text, {}, "L'image montr\xC3\xA9" "e \xC3\xA0 la place de la vid\xC3\xA9o (qui n'est pas d\xC3\xA9" "cod\xC3\xA9" "e)."},
        {"stretch", "\xC3\x89tirement", PG::ValueType::Enum, {"ajuster", "\xC3\xA9tirer", "aucun"}, ""},
        {"video", "Vid\xC3\xA9o (ressource)", PG::ValueType::Text, {}, ""},
        {"source", "Source", PG::ValueType::Enum, hmiHistorySources(),
         "alarmes : les actives ; acquitt\xC3\xA9" "es ; historique : les alarmes termin\xC3\xA9" "es ; \xC3\xA9v\xC3\xA9nements ; "
         "syst\xC3\xA8me : le journal de l'IHM ; audit : le journal d'audit (qui a chang\xC3\xA9 quoi, avant et apr\xC3\xA8s, sign\xC3\xA9 ou non)."},
        {"group", "Groupe d'alarmes (filtre)", PG::ValueType::Text, {}, "Vide : tous les groupes."},
        {"variables", "Plumes : variables trac\xC3\xA9" "es (a;b)", PG::ValueType::Text, {},
         "Une expression par plume, s\xC3\xA9par\xC3\xA9" "es par ; : Armoires[0].ana.PT1.mes; Armoires[1].ana.PT1.mes"},
        {"mode", "Mode", PG::ValueType::Enum, {"temps r\xC3\xA9" "el", "historique"},
         "Temps r\xC3\xA9" "el : mesur\xC3\xA9" "es \xC3\xA0 chaque cycle IHM. Historique : relues dans les mesures archiv\xC3\xA9" "es "
         "(Configuration > Historiques) ou dans un fichier externe."},
        {"duration", "Dur\xC3\xA9" "e affich\xC3\xA9" "e (s)", PG::ValueType::Real, {}, "La fen\xC3\xAAtre de temps de la courbe."},
        {"scale", "\xC3\x89" "chelle", PG::ValueType::Enum, {"fixe", "auto"}, "fixe : Axe Y min / max ; auto : suit les valeurs affich\xC3\xA9" "es."},
        {"ymin", "Axe Y min", PG::ValueType::Real, {}, ""},
        {"ymax", "Axe Y max", PG::ValueType::Real, {}, ""},
        {"colors", "Couleurs des plumes (a;b)", PG::ValueType::Text, {}, "Une couleur par plume, dans l'ordre : #4FA3FF;#F2994A"},
        {"interpolation", "Interpolation", PG::ValueType::Enum, {"lin\xC3\xA9" "aire", "escalier", "points"},
         "lin\xC3\xA9" "aire : les points reli\xC3\xA9s ; escalier : la valeur tenue jusqu'\xC3\xA0 la suivante ; points : seuls."},
        {"legend", "L\xC3\xA9gende", PG::ValueType::Boolean, {}, "Le nom et la derni\xC3\xA8re valeur de chaque plume."},
        {"points", "Points (x,y x,y ...)", PG::ValueType::Text, {}, "Dans le rep\xC3\xA8re de l'objet."},
        {"clip", "Rogner le contenu", PG::ValueType::Boolean, {}, ""},
        {"variable", "Variable API", PG::ValueType::Text, {}, "La variable de l'automate que l'objet repr\xC3\xA9sente."},
        {"refresh", "Rafra\xC3\xAE" "chissement (ms)", PG::ValueType::Integer, {}, ""},
        {"access", "Niveau d'acc\xC3\xA8s", PG::ValueType::Integer, {},
         "0 = tout le monde. Au-dessus : le niveau minimal pour agir (1 op\xC3\xA9rateur, 2 maintenance, 3 superviseur, "
         "4 administrateur). S'applique quand la s\xC3\xA9" "curit\xC3\xA9 est active."},
        {"profile", "Profil utilisateur", PG::ValueType::Enum, {"", "op\xC3\xA9rateur", "maintenance", "superviseur", "administrateur"}, ""},
        {"blink", "Clignotement", PG::ValueType::Boolean, {}, "Une expression (=Alarme) le d\xC3\xA9" "clenche."},
        {"blinkColor", "Couleur de clignotement", PG::ValueType::Color, {}, ""},
        // ---- lot 6
        {"recipe", "Recette", PG::ValueType::Enum, {}, "La recette dont l'objet montre les jeux (Configuration > Recettes)."},
        {"buttons", "Boutons (a;b)", PG::ValueType::Text, {},
         "Ceux \xC3\xA0 montrer, dans l'ordre : Ajouter;Modifier;Supprimer;Appliquer;Lire. Ajouter enregistre les valeurs "
         "de l'installation sous un nouveau nom, Lire les range dans le jeu choisi, Appliquer l'\xC3\xA9" "crit. Tout change le projet."},
        {"states", "\xC3\x89tats", PG::ValueType::ReadOnly, {},
         "Un \xC3\xA9tat par ligne : condition => image1, image2 @ p\xC3\xA9riode. Le premier vrai l'emporte. \xC3\x80 r\xC3\xA9gler dans "
         "l'onglet Contenu."},
        {"period", "P\xC3\xA9riode de d\xC3\xA9" "filement (ms)", PG::ValueType::Integer, {},
         "Le temps de chaque image d'un \xC3\xA9tat qui en a plusieurs (sans p\xC3\xA9riode propre)."},
        {"cells", "Cases", PG::ValueType::ReadOnly, {},
         "Texte, texte \xC3\xA0 trous ({Niveau:0.0} bar) ou =expression. \xC3\x80 r\xC3\xA9gler dans l'onglet Contenu."},
        {"widths", "Largeurs des colonnes (a;b)", PG::ValueType::Text, {}, "Relatives : 2;1;1 = la premi\xC3\xA8re deux fois plus large."},
        {"names", "Noms des plumes (a;b)", PG::ValueType::Text, {}, "Pour la l\xC3\xA9gende ; vide : l'expression."},
        // ---- lot 8 : le champ de saisie ("mode" : voir plus bas, selon l'objet)
        {"format", "Format", PG::ValueType::Text, {},
         "Champ de saisie : 0 (entier), 0.0 (1 d\xC3\xA9" "cimale), 0.00... La valeur montr\xC3\xA9" "e et celle propos\xC3\xA9" "e en saisie."},
        {"placeholder", "Texte d'invite", PG::ValueType::Text, {}, "Le texte p\xC3\xA2le d'un champ vide (\xC2\xAB consigne en bar \xC2\xBB)."},
        {"maxLength", "Longueur maximale", PG::ValueType::Integer, {}, "Texte et mot de passe : le nombre de caract\xC3\xA8res permis."},
        {"keyboard", "Clavier virtuel", PG::ValueType::Enum, {"aucun", "num\xC3\xA9rique", "complet"},
         "Un clavier \xC3\xA0 l'\xC3\xA9" "cran quand le champ prend le focus (\xC3\xA9" "crans tactiles)."},
        {"validateOnExit", "Valider en quittant", PG::ValueType::Boolean, {},
         "Un clic ailleurs \xC3\xA9" "crit la valeur tap\xC3\xA9" "e (sinon il l'annule, comme \xC3\x89" "chap)."},
        // ---- lot 8 : les objets des utilisateurs
        {"userList", "Liste des utilisateurs", PG::ValueType::Boolean, {},
         "Vrai : on choisit l'utilisateur avec < > parmi les comptes actifs. Faux : on tape son identifiant."},
        {"buttonText", "Texte du bouton", PG::ValueType::Text, {}, ""},
        {"afterLogin", "Vue apr\xC3\xA8s connexion", PG::ValueType::Text, {}, "Facultatif : la vue ouverte quand la connexion r\xC3\xA9ussit."},
        {"accent", "Couleur du bouton", PG::ValueType::Color, {}, ""},
        {"showUser", "Afficher l'utilisateur", PG::ValueType::Boolean, {}, "\xC2\xAB Se d\xC3\xA9" "connecter (admin) \xC2\xBB."},
        {"confirm", "Demander confirmation", PG::ValueType::Boolean, {}, "Un premier clic demande \xC2\xAB Confirmer ? \xC2\xBB, le second d\xC3\xA9" "connecte (3 s)."},
        {"display", "Affichage", PG::ValueType::Text, {},
         "Le gabarit : {nom}, {login}, {groupe}, {niveau}, {depuis} (dur\xC3\xA9" "e de connexion), {reste} (avant la d\xC3\xA9" "connexion automatique)."},
        {"empty", "Sans utilisateur", PG::ValueType::Text, {}, "Ce qu'il montre quand personne n'est connect\xC3\xA9."},
        {"icon", "Ic\xC3\xB4ne", PG::ValueType::Boolean, {}, ""},
        {"minLength", "Longueur minimale", PG::ValueType::Integer, {}, "Le nouveau mot de passe : au moins ce nombre de caract\xC3\xA8res."},
        {"requireDigit", "Un chiffre exig\xC3\xA9", PG::ValueType::Boolean, {}, "Le nouveau mot de passe doit contenir au moins un chiffre."},
        // ---- lot 9 : les commandes
        {"confirmMode", "Confirmation", PG::ValueType::Enum, {"aucune", "second clic", "maintien"},
         "Une commande sensible : un second clic dans les 3 s, ou un appui maintenu, avant les actions."},
        {"holdMs", "Dur\xC3\xA9" "e du maintien (ms)", PG::ValueType::Integer, {}, "Confirmation par maintien : le temps d'appui."},
        {"pressedColor", "Couleur appuy\xC3\xA9", PG::ValueType::Color, {}, ""},
        {"inverted", "Impulsion invers\xC3\xA9" "e", PG::ValueType::Boolean, {}, "Faux pendant l'appui, vrai au repos."},
        {"textOn", "Libell\xC3\xA9 vrai", PG::ValueType::Text, {}, ""},
        {"textOff", "Libell\xC3\xA9 faux", PG::ValueType::Text, {}, ""},
        {"state", "Retour d'\xC3\xA9tat (expression)", PG::ValueType::Text, {},
         "Ce que l'objet montre (vide : la variable \xC3\xA9" "crite). Diff\xC3\xA9rent de la commande : une discordance se voit."},
        {"knobColor", "Couleur de la poign\xC3\xA9" "e", PG::ValueType::Color, {}, ""},
        {"operation", "Op\xC3\xA9ration", PG::ValueType::Enum, {"basculer", "impulsion", "mettre \xC3\xA0 1", "mettre \xC3\xA0 0", "aucune"}, ""},
        {"lamp", "Voyant (expression)", PG::ValueType::Text, {}, "Ce qui allume le bouton ; vide : la variable \xC3\xA9" "crite."},
        {"positions", "Positions (a;b;c)", PG::ValueType::Text, {}, ""},
        {"values", "Valeurs \xC3\xA9" "crites (a;b;c)", PG::ValueType::Text, {}, "Vide : le rang (0, 1, 2...)."},
        {"style", "Style", PG::ValueType::Enum, {"rotatif", "boutons"}, ""},
        {"step", "Pas", PG::ValueType::Real, {}, ""},
        {"showValue", "Afficher la valeur", PG::ValueType::Boolean, {}, ""},
        {"ticks", "Graduations", PG::ValueType::Integer, {}, ""},
        {"continuous", "\xC3\x89" "crire en glissant", PG::ValueType::Boolean, {}, "Faux : la valeur est \xC3\xA9" "crite au rel\xC3\xA2" "chement."},
        {"maxVisible", "Lignes visibles", PG::ValueType::Integer, {}, ""},
        {"fields", "Champs", PG::ValueType::Enum, {"date et heure", "date", "heure"}, ""},
        {"seconds", "Secondes", PG::ValueType::Boolean, {}, ""},
        {"schedule", "Plages", PG::ValueType::Text, {}, "Lu-Ve=07:00-12:00,13:30-17:00; Sa=08:00-12:00"},
        {"resolution", "R\xC3\xA9solution", PG::ValueType::Enum, {"1 h", "30 min", "15 min"}, ""},
        {"output", "Sortie (variable BOOL)", PG::ValueType::Text, {}, "Vraie pendant une plage."},
        {"nowColor", "Couleur de l'heure actuelle", PG::ValueType::Color, {}, ""},
        // ---- lot 9 : les afficheurs
        {"label", "Libell\xC3\xA9", PG::ValueType::Text, {}, ""},
        {"lowAlarm", "Alarme basse", PG::ValueType::Text, {}, ""},
        {"low", "Seuil bas", PG::ValueType::Text, {}, ""},
        {"high", "Seuil haut", PG::ValueType::Text, {}, ""},
        {"highAlarm", "Alarme haute", PG::ValueType::Text, {}, ""},
        {"colorWarning", "Couleur des seuils", PG::ValueType::Color, {}, ""},
        {"colorAlarm", "Couleur des alarmes", PG::ValueType::Color, {}, ""},
        {"stateList", "\xC3\x89tats (valeur = texte | couleur)", PG::ValueType::Text, {}, "0 = Arr\xC3\xAAt | #4A5261; 1 = Marche | #2ECC71 | clignote"},
        {"showText", "Afficher le texte de l'\xC3\xA9tat", PG::ValueType::Boolean, {}, ""},
        {"unknownText", "Texte par d\xC3\xA9" "faut", PG::ValueType::Text, {}, ""},
        {"zones", "Zones (de-\xC3\xA0 = couleur)", PG::ValueType::Text, {}, "0-60 = #2ECC71; 60-80 = #F2C94C; 80-100 = #E5534B"},
        {"setpoint", "Consigne", PG::ValueType::Text, {}, ""},
        {"setpointColor", "Couleur de la consigne", PG::ValueType::Color, {}, ""},
        {"needleColor", "Couleur de l'aiguille", PG::ValueType::Color, {}, ""},
        {"clockStyle", "Style", PG::ValueType::Enum, {"analogique", "num\xC3\xA9rique"}, ""},
        {"showDate", "Afficher la date", PG::ValueType::Boolean, {}, ""},
        {"condition", "Compte quand (expression)", PG::ValueType::Text, {}, "Vide : toujours."},
        {"durationFormat", "Format de la dur\xC3\xA9" "e", PG::ValueType::Enum, {"h:mm:ss", "heures", "jours"}, ""},
        {"digits", "Chiffres", PG::ValueType::Integer, {}, ""},
        {"decimals", "D\xC3\xA9" "cimales", PG::ValueType::Integer, {}, ""},
        {"leadingZeros", "Z\xC3\xA9ros \xC3\xA0 gauche", PG::ValueType::Boolean, {}, ""},
        {"window", "Fen\xC3\xAAtre (s)", PG::ValueType::Real, {}, ""},
        {"deadband", "Zone morte", PG::ValueType::Real, {}, ""},
        {"colorUp", "Couleur en hausse", PG::ValueType::Color, {}, ""},
        {"colorDown", "Couleur en baisse", PG::ValueType::Color, {}, ""},
        {"colorSteady", "Couleur stable", PG::ValueType::Color, {}, ""},
        {"speed", "Vitesse (pixels/s)", PG::ValueType::Real, {}, ""},
        {"direction", "Sens", PG::ValueType::Enum, {"gauche", "droite"}, ""},
        {"ecLevel", "Correction d'erreurs", PG::ValueType::Enum, {"L", "M", "Q", "H"}, ""},
        {"margin", "Marge (modules)", PG::ValueType::Integer, {}, ""},
        {"darkColor", "Couleur des modules", PG::ValueType::Color, {}, ""},
        // ---- lot 10 : l'objet Parametres systeme
        // ---- lot 10 : les images recolorables (SVG net a toutes les tailles)
        {"recolor", "Recolorier", PG::ValueType::Color, {},
         "Une couleur qui remplace celles du SVG (vide : ses couleurs). Une expression la choisit en marche : "
         "=SEL(Marche, '#8A94A3', '#2ECC71') (vert en marche, gris \xC3\xA0 l'arr\xC3\xAAt). Une image PNG ou JPEG en est teint\xC3\xA9" "e."},
        {"recolorMode", "Ce qui est recolori\xC3\xA9", PG::ValueType::Enum, {"tout", "remplissages", "contours", "une couleur"},
         "Tout le dessin, ses remplissages, ses contours, ou seulement les formes de la couleur remplac\xC3\xA9" "e."},
        {"recolorFrom", "Couleur remplac\xC3\xA9" "e", PG::ValueType::Color, {}, "Pour \xC2\xAB une couleur \xC2\xBB : celle du SVG qui prend la couleur de Recolorier."},
        // ---- lot 10 : les symboles de synoptique
        {"fault", "D\xC3\xA9" "faut (expression)", PG::ValueType::Text, {},
         "Vrai : le symbole prend la couleur de d\xC3\xA9" "faut et clignote. Une expression : =Pompe_Defaut, =Pression > 9."},
        {"colorFault", "Couleur de d\xC3\xA9" "faut", PG::ValueType::Color, {}, "Celle du symbole en d\xC3\xA9" "faut (elle clignote)."},
        {"labelPosition", "Place du libell\xC3\xA9", PG::ValueType::Enum, {"dessous", "dessus", "aucun"}, "Le libell\xC3\xA9 (P-101, V-12...) sous le symbole, au-dessus, ou cach\xC3\xA9."},
        {"animate", "Animer en marche", PG::ValueType::Boolean, {},
         "En marche (valeur vraie), ce qui tourne tourne (pompe, moteur, ventilateur, m\xC3\xA9langeur) et ce qui coule coule (tuyauterie, convoyeur, fl\xC3\xA8" "che)."},
        {"valveType", "Commande de la vanne", PG::ValueType::Enum, {"manuelle", "motoris\xC3\xA9" "e", "pneumatique", "r\xC3\xA9glante"},
         "Son actionneur, dessin\xC3\xA9 au-dessus : un volant, un moteur, une membrane ; r\xC3\xA9glante : une membrane et son "
         "positionneur (son ouverture : Ouverture (%))."},
        {"fillColor", "Couleur du contenu", PG::ValueType::Color, {}, "Le liquide, le gaz, le grain : ce qui monte avec le niveau."},
        {"fluidColor", "Couleur du fluide", PG::ValueType::Color, {}, "Les traits qui avancent dans le tube quand il coule."},
        {"pipeShape", "Forme du tube", PG::ValueType::Enum, {"droit", "coude", "t\xC3\xA9", "croix"},
         "Droit (horizontal ou vertical selon la bo\xC3\xAEte), coude, t\xC3\xA9, croix : on tourne l'objet pour l'orienter."},
        {"thickness", "\xC3\x89paisseur du tube", PG::ValueType::Real, {}, "En pixels de la vue."},
        {"function", "Fonction (lettres ISA)", PG::ValueType::Text, {},
         "PT : transmetteur de pression ; TIC : r\xC3\xA9gulateur indicateur de temp\xC3\xA9rature ; FT, LT, PI, TT..."},
        {"loop", "Boucle (num\xC3\xA9ro)", PG::ValueType::Text, {}, "Le num\xC3\xA9ro de boucle, sous les lettres : 101."},
        {"mounting", "Montage", PG::ValueType::Enum, {"terrain", "tableau", "tableau arri\xC3\xA8re"},
         "ISA 5.1 : sur le terrain (sans trait), en salle au tableau (un trait), derri\xC3\xA8re le tableau (tiret\xC3\xA9)."},
        {"gasColor", "Couleur de l'ogive (gaz)", PG::ValueType::Color, {},
         "La couleur normalis\xC3\xA9" "e du gaz (EN 1089-3) : azote noir, oxyg\xC3\xA8ne blanc, argon vert fonc\xC3\xA9, CO2 gris, h\xC3\xA9lium brun, hydrog\xC3\xA8ne rouge."},
        {"tab", "Onglet ouvert", PG::ValueType::Enum, {"R\xC3\xA9glages", "Diagnostic", "Simulation"},
         "L'onglet du menu natif Param\xC3\xA8tres syst\xC3\xA8me qu'ouvre le clic : les r\xC3\xA9glages du poste, le diagnostic, "
         "ou la page Simulation (les esclaves simul\xC3\xA9s, permission Administrer)."},
        // lot 10 : l'instance d'un symbole
        {"symbol", "Symbole", PG::ValueType::Enum, {},
         "Le symbole dessin\xC3\xA9 ici (une vue de r\xC3\xB4le Symbole, dossier IHM > Symboles). Le modifier modifie toutes ses instances."},
        {"params", "Arguments", PG::ValueType::Text, {},
         "Ce que re\xC3\xA7oit chaque param\xC3\xA8tre du symbole : Armoire := Armoires[1]; Nom := 'B'. "
         "Un param\xC3\xA8tre absent garde sa valeur par d\xC3\xA9" "faut (celle du symbole)."},
        // ---- lot 11 : les graphiques
        {"xVariable", "Variable X (expression)", PG::ValueType::Text, {}, "L'abscisse : D\xC3\xA9" "bit_m3h. \xC3\x80 chaque cycle, un point (X, Y) par courbe."},
        {"xmin", "Axe X min", PG::ValueType::Real, {}, ""},
        {"xmax", "Axe X max", PG::ValueType::Real, {}, ""},
        {"xLabel", "Titre de l'axe X", PG::ValueType::Text, {}, "D\xC3\xA9" "bit (m\xC2\xB3/h)"},
        {"yLabel", "Titre de l'axe Y", PG::ValueType::Text, {}, "Pression (bar)"},
        {"reference", "Courbe de r\xC3\xA9" "f\xC3\xA9rence (x,y x,y...)", PG::ValueType::Text, {},
         "La courbe attendue (constructeur, essai type), en pointill\xC3\xA9s : 0,90 20,86 40,78 60,64 80,45 100,20."},
        {"referenceColor", "Couleur de la r\xC3\xA9" "f\xC3\xA9rence", PG::ValueType::Color, {}, ""},
        {"tolerance", "Tol\xC3\xA9rance (\xC2\xB1 en Y)", PG::ValueType::Real, {}, "Une bande autour de la r\xC3\xA9" "f\xC3\xA9rence ; 0 : aucune."},
        {"maxPoints", "Points gard\xC3\xA9s", PG::ValueType::Integer, {}, "La trace : les N derniers points (un par cycle IHM)."},
        {"reset", "Remise \xC3\xA0 z\xC3\xA9ro (expression)", PG::ValueType::Text, {},
         "Sur son front montant, la trace repart de z\xC3\xA9ro (un nouvel essai) : Essai_Demarre."},
        {"hole", "Trou central (%)", PG::ValueType::Real, {}, "0 : un camembert ; 50 : un anneau, le total au centre."},
        {"labelMode", "\xC3\x89tiquettes des parts", PG::ValueType::Enum, {"pourcentage", "valeur", "les deux", "aucune"}, ""},
        {"references", "Consigne (valeurs a;b;c)", PG::ValueType::Text, {},
         "Un second polygone, en pointill\xC3\xA9s : les valeurs attendues, une par axe (nombres ou expressions)."},
        {"rings", "Cercles de la toile", PG::ValueType::Integer, {}, ""},
        {"bins", "Classes", PG::ValueType::Integer, {}, "Le nombre de barres entre le minimum et le maximum."},
        {"samplePeriod", "P\xC3\xA9riode de mesure (ms)", PG::ValueType::Integer, {}, "Une mesure toutes les N millisecondes."},
        {"maxSamples", "Mesures gard\xC3\xA9" "es", PG::ValueType::Integer, {}, "Au-del\xC3\xA0, les plus anciennes tombent."},
        {"showStats", "Afficher les statistiques", PG::ValueType::Boolean, {},
         "Le nombre de mesures, la moyenne, l'\xC3\xA9" "cart type ; Cp et Cpk entre les tol\xC3\xA9rances."},
        // ---- lot 11 : les objets des alarmes
        {"show", "Alarme montr\xC3\xA9" "e", PG::ValueType::Enum, {"la plus grave", "la plus r\xC3\xA9" "cente", "d\xC3\xA9" "filement"},
         "La plus grave \xC3\xA0 acquitter ; la derni\xC3\xA8re apparue ; ou chacune \xC3\xA0 son tour."},
        {"ackButton", "Bouton Acquitter", PG::ValueType::Boolean, {}, "Acquitte l'alarme montr\xC3\xA9" "e (permission Acquitter)."},
        {"showCount", "Nombre des autres (+N)", PG::ValueType::Boolean, {}, "Un clic dessus montre l'alarme suivante."},
        {"blinkUnacked", "Clignoter \xC3\xA0 acquitter", PG::ValueType::Boolean, {}, "Tant qu'une alarme attend son acquittement."},
        {"count", "Compter", PG::ValueType::Enum, {"\xC3\xA0 acquitter", "actives", "en cours", "mises de c\xC3\xB4t\xC3\xA9"}, ""},
        {"groups", "Zones (groupes a;b;c)", PG::ValueType::Text, {}, "Vide : tous les groupes des alarmes, dans leur ordre."},
        {"perRow", "Tuiles par ligne", PG::ValueType::Integer, {}, ""},
        {"colorOk", "Couleur d'une zone calme", PG::ValueType::Color, {}, ""},
        {"alarm", "Alarme", PG::ValueType::Enum, {},
         "Vide : l'alarme choisie d'un clic (bandeau, liste, r\xC3\xA9sum\xC3\xA9), sinon la plus grave en cours."},
        {"showMessage", "Afficher le message", PG::ValueType::Boolean, {}, ""},
        {"range", "P\xC3\xA9riode", PG::ValueType::Enum, {"depuis le lancement", "24 h", "7 jours", "tout l'historique"}, ""},
        {"top", "Nombre de lignes", PG::ValueType::Integer, {}, "Les N alarmes les plus fr\xC3\xA9quentes (ou les plus longues)."},
        {"sort", "Classer par", PG::ValueType::Enum, {"nombre", "dur\xC3\xA9" "e"}, ""},
        // ---- lot 11 : la production
        {"good", "Pi\xC3\xA8" "ces bonnes (compteur)", PG::ValueType::Text, {},
         "Le compteur des pi\xC3\xA8" "ces bonnes de l'automate. Il peut repartir de 0 : l'objet suit."},
        {"bad", "Rebuts (compteur)", PG::ValueType::Text, {}, "Le compteur des pi\xC3\xA8" "ces rebut\xC3\xA9" "es."},
        {"running", "En marche (expression)", PG::ValueType::Text, {},
         "Vraie quand la machine produit : le temps de marche (la disponibilit\xC3\xA9). Vide : toujours."},
        {"idealRate", "Cadence nominale (pi\xC3\xA8" "ces/h)", PG::ValueType::Real, {}, "Ce que la machine fait au mieux : la base de la performance."},
        {"target", "Objectif du poste (pi\xC3\xA8" "ces)", PG::ValueType::Real, {}, "0 : sans objectif."},
        {"shifts", "D\xC3\xA9" "buts de poste (hh:mm;...)", PG::ValueType::Text, {},
         "Les compteurs repartent de z\xC3\xA9ro \xC3\xA0 chaque d\xC3\xA9" "but de poste : 06:00;14:00;22:00. Vide : jamais (seulement RAZ)."},
        {"rateWindow", "Fen\xC3\xAAtre de cadence (s)", PG::ValueType::Real, {}, "La cadence : les pi\xC3\xA8" "ces des N derni\xC3\xA8res secondes, par heure."},
        {"showReset", "Bouton RAZ", PG::ValueType::Boolean, {}, "Remet les compteurs du poste \xC3\xA0 z\xC3\xA9ro (permission Piloter)."},
        {"units", "Unit\xC3\xA9s (a;b;c)", PG::ValueType::Text, {}, ""},
        {"writable", "Valeurs modifiables", PG::ValueType::Boolean, {},
         "Un clic sur la valeur d'une variable la modifie (Entr\xC3\xA9" "e \xC3\xA9" "crit ; permission Piloter). Un calcul reste en lecture."},
        {"compare", "Comparer avec l'installation", PG::ValueType::Boolean, {}, "Les colonnes Installation et \xC3\x89" "cart."},
        {"exportSource", "Donn\xC3\xA9" "es export\xC3\xA9" "es", PG::ValueType::Enum, {},
         "Les alarmes en cours, l'historique des alarmes, les \xC3\xA9v\xC3\xA9nements, le journal syst\xC3\xA8me, les mesures "
         "archiv\xC3\xA9" "es, les jeux d'une recette, ou le contenu d'un objet de la vue (courbe, tableau, graphique...)."},
        {"fileFormat", "Format du fichier", PG::ValueType::Enum, {"CSV", "Excel", "PDF"}, ""},
        {"fileName", "Nom du fichier", PG::ValueType::Text, {},
         "Texte \xC3\xA0 trous : alarmes_{SYS.Date}. Propos\xC3\xA9 dans le dossier exports du projet ; l'extension suit le format."},
        // ---- Lot API 8 : les exports qui demandent ou
        {"askWhere", "Demander o\xC3\xB9 enregistrer", PG::ValueType::Boolean, {},
         "Coch\xC3\xA9 (par d\xC3\xA9" "faut) : le clic ouvre un dialogue - exports/ du projet propos\xC3\xA9 (Exporter : comme avant), "
         "le bouton \xE2\x80\xA6 pour un autre dossier ou un autre nom ; au poste d'exploitation, \xC3\xA0 grands boutons. "
         "D\xC3\xA9" "coch\xC3\xA9 : toujours dans exports/, sans question."},
        // ---- lot 11 : la vanne reglante
        {"opening", "Ouverture (%)", PG::ValueType::Text, {},
         "Vanne r\xC3\xA9glante : de 0 \xC3\xA0 100 % (une expression : =Vanne_Position). Vide : tout ou rien."},
        {"moving", "En mouvement (expression)", PG::ValueType::Text, {}, "Vraie : la tige clignote (la vanne bouge)."},
        // ---- 1.10.4 : la vanne 3 voies
        {"valve3Function", "Variante", PG::ValueType::Enum, {"m\xC3\xA9langeuse", "r\xC3\xA9partitrice"},
         "M\xC3\xA9langeuse : les voies 1 et 2 entrent, la voie 3 sort. R\xC3\xA9partitrice : la voie 3 entre, les voies 1 et 2 sortent. "
         "Une fl\xC3\xA8" "che sur chaque voie du passage le montre."},
        {"valve3Bore", "Boisseau", PG::ValueType::Enum, {"T", "L"},
         "En T : il relie 1-2, 1-3 ou 2-3 (dessin\xC3\xA9 avec une branche de plus vers le haut). En L : 1-3 et 2-3 seulement (1-2 n'existe pas). "
         "Toujours : ferm\xC3\xA9" "e."},
        {"positionMode", "Commande de la position", PG::ValueType::Enum, {"entier", "deux bool\xC3\xA9" "ens"},
         "Entier (une variable enti\xC3\xA8re) : la Position donne la voie active - 0 ferm\xC3\xA9" "e, 1 (ou 12) la voie 1-2, 2 (ou 13) la voie 1-3, 3 (ou 23) la voie 2-3. "
         "Deux bool\xC3\xA9" "ens : A seul, 1-2 ; B seul, 1-3 ; A et B, 2-3 ; aucun, ferm\xC3\xA9" "e."},
        {"positionA", "Bool\xC3\xA9" "en A (expression)", PG::ValueType::Text, {},
         "Position par deux bool\xC3\xA9" "ens : A seul, la voie 1-2 ; avec B, la voie 2-3. Une expression : =V3_A."},
        {"positionB", "Bool\xC3\xA9" "en B (expression)", PG::ValueType::Text, {},
         "Position par deux bool\xC3\xA9" "ens : B seul, la voie 1-3 ; avec A, la voie 2-3. Une expression : =V3_B."},
        {"showPorts", "Num\xC3\xA9ros des voies", PG::ValueType::Boolean, {}, "Coch\xC3\xA9 : 1, 2 et 3 \xC3\xA9" "crits pr\xC3\xA8s des voies."},
        {"colorMoving", "Couleur de mouvement", PG::ValueType::Color, {}, "Celle du passage en mouvement (elle clignote)."},
        // ---- lot 12 : la navigation et la structure
        {"views", "Vues (a;b;c)", PG::ValueType::Text, {},
         "Les vues de la barre, dans l'ordre : Vue_Accueil;Vue_Ligne;Vue_Alarmes. Vide : toutes les vues ordinaires du projet."},
        {"labels", "Libell\xC3\xA9s (a;b;c)", PG::ValueType::Text, {}, "Un libell\xC3\xA9 par vue ; vide : le nom de la vue (sans Vue_)."},
        {"tabStyle", "Style", PG::ValueType::Enum, {"onglets", "boutons", "liens"},
         "Onglets pos\xC3\xA9s sur un filet, boutons s\xC3\xA9par\xC3\xA9s, ou liens soulign\xC3\xA9s : la vue courante en surbrillance."},
        {"backForward", "Boutons Pr\xC3\xA9" "c\xC3\xA9" "dent / Suivant", PG::ValueType::Boolean, {},
         "Deux fl\xC3\xA8" "ches en t\xC3\xAAte : l'historique de navigation (estomp\xC3\xA9" "es quand il n'y a rien)."},
        {"transition", "Transition", PG::ValueType::Enum,
         {"Instantan\xC3\xA9" "e", "Fondu", "Glissement", "Zoom", "Rotation"},
         "Le passage d'une vue \xC3\xA0 l'autre ; un glissement vient du c\xC3\xB4t\xC3\xA9 de la vue choisie."},
        {"gap", "Espacement", PG::ValueType::Real, {}, "Entre deux boutons, en pixels."},
        {"activeTextColor", "Couleur du texte actif", PG::ValueType::Color, {}, ""},
        {"buttonColor", "Couleur des boutons", PG::ValueType::Color, {}, ""},
        {"activeColor", "Couleur active", PG::ValueType::Color, {}, "La vue courante, l'onglet montr\xC3\xA9."},
        {"trail", "Chemin", PG::ValueType::Enum, {"hi\xC3\xA9rarchie", "historique"},
         "Hi\xC3\xA9rarchie : la vue courante et ses vues parentes (propri\xC3\xA9t\xC3\xA9 Vue parente de chaque vue). "
         "Historique : les vues visit\xC3\xA9" "es avant elle."},
        {"separator", "S\xC3\xA9parateur", PG::ValueType::Text, {}, "Entre deux \xC3\xA9tapes : \xE2\x80\xBA, >, /"},
        {"maxItems", "\xC3\x89tapes au plus", PG::ValueType::Integer, {}, "Les premi\xC3\xA8res tombent au-del\xC3\xA0."},
        {"home", "Commencer par l'accueil", PG::ValueType::Boolean, {},
         "Hi\xC3\xA9rarchie : la vue d'accueil en t\xC3\xAAte quand la cha\xC3\xAEne n'y remonte pas."},
        {"linkColor", "Couleur des liens", PG::ValueType::Color, {}, ""},
        {"tabs", "Onglets (a;b;c)", PG::ValueType::Text, {}, "Une page par onglet. Les pages se r\xC3\xA8glent aussi dans l'onglet Contenu."},
        // ---- lot 13 : le selecteur de langue
        {"languages", "Langues (a;b;c)", PG::ValueType::Text, {},
         "Les langues propos\xC3\xA9" "es, par leur code, dans l'ordre : fr;en;de. Vide : toutes celles du projet (Configuration > Langues)."},
        {"languageStyle", "Style", PG::ValueType::Enum, {"boutons", "bascule"},
         "Boutons : un par langue, la langue en cours en surbrillance. Bascule : un seul bouton, la langue en cours ; "
         "un clic passe \xC3\xA0 la suivante."},
        {"languageLabel", "Libell\xC3\xA9", PG::ValueType::Enum, {"code et nom", "code", "nom"},
         "Ce qu'on lit sur chaque bouton : EN  English, EN ou English (le nom dans sa langue)."},
        // ---- lot 14 : l'etat de la communication, le diagnostic automate
        {"showAddress", "Montrer l'adresse", PG::ValueType::Boolean, {}, "L'adresse de l'automate (192.168.1.10:502) dans la ligne."},
        {"showTime", "Montrer le temps de r\xC3\xA9ponse", PG::ValueType::Boolean, {}, "Le temps de r\xC3\xA9ponse de la derni\xC3\xA8re requ\xC3\xAAte, en ms."},
        {"compact", "Compact", PG::ValueType::Boolean, {}, "Le voyant et l'\xC3\xA9tat seuls : Connect\xC3\xA9, D\xC3\xA9" "connect\xC3\xA9, Simulateur."},
        {"colorGood", "Couleur connect\xC3\xA9" "e", PG::ValueType::Color, {}, "L'automate r\xC3\xA9pond, toutes les valeurs sont bonnes."},
        {"colorWarn", "Couleur attention", PG::ValueType::Color, {}, "Connexion en cours, valeurs anciennes."},
        {"colorBad", "Couleur coup\xC3\xA9" "e", PG::ValueType::Color, {}, "L'automate est injoignable."},
        {"colorSim", "Couleur simulateur", PG::ValueType::Color, {}, "L'IHM lit le simulateur de l'application."},
        {"showBad", "Variables en d\xC3\xA9" "faut", PG::ValueType::Boolean, {},
         "La liste des variables de qualit\xC3\xA9 ancienne ou mauvaise, avec la raison (adresse ill\xC3\xA9gale, sans adresse...)."},
        {"showButtons", "Boutons", PG::ValueType::Boolean, {}, "Reconnecter, et Remettre \xC3\xA0 z\xC3\xA9ro les compteurs."},
        {"labelColor", "Couleur des libell\xC3\xA9s", PG::ValueType::Color, {}, ""},
        // ---- lot 13 : le selecteur de theme, les unites des variables
        {"themeLabels", "Libell\xC3\xA9s (jour;nuit)", PG::ValueType::Text, {},
         "Les libell\xC3\xA9s des deux boutons : Jour;Nuit (ils se traduisent, Configuration > Langues)."},
        {"varFormat", "Format de la variable", PG::ValueType::Boolean, {},
         "Coch\xC3\xA9 : l'unit\xC3\xA9 et le format de la variable montr\xC3\xA9" "e (Configuration > Unit\xC3\xA9s et formats) remplacent ceux de l'objet. "
         "D\xC3\xA9" "coch\xC3\xA9 : l'objet garde les siens."},
        {"formats", "Formats (a;b;c)", PG::ValueType::Text, {},
         "Un format par ligne (0.0;0;0.00) ; vide : celui de la variable (Configuration > Unit\xC3\xA9s et formats), sinon Format."},
        {"page", "Onglet montr\xC3\xA9", PG::ValueType::Integer, {},
         "De 1 au nombre d'onglets : la page montr\xC3\xA9" "e au d\xC3\xA9marrage (et celle qu'on \xC3\xA9" "dite). En marche : Vue.Objet.Page."},
        {"tabPosition", "Place des onglets", PG::ValueType::Enum, {"haut", "bas"}, ""},
        {"tabHeight", "Hauteur des onglets", PG::ValueType::Real, {}, ""},
        {"tabColor", "Couleur des onglets", PG::ValueType::Color, {}, ""},
        {"tabPage", "Page (conteneur \xC3\xA0 onglets)", PG::ValueType::Integer, {},
         "La page du conteneur \xC3\xA0 onglets o\xC3\xB9 l'objet se montre (1 = la premi\xC3\xA8re)."},
        {"title", "Titre", PG::ValueType::Text, {}, "Texte \xC3\xA0 trous : Pompe {Nom}."},
        {"titleStyle", "Style du titre", PG::ValueType::Enum, {"bordure", "bandeau"},
         "Bordure : le titre coupe la ligne du haut du cadre. Bandeau : une bande color\xC3\xA9" "e en haut."},
        {"titleAlign", "Place du titre", PG::ValueType::Enum, {"gauche", "centre", "droite"}, ""},
        {"titleColor", "Couleur du titre", PG::ValueType::Color, {}, ""},
        {"titleFill", "Couleur du bandeau", PG::ValueType::Color, {}, ""},
        {"scrollDirection", "D\xC3\xA9" "filement", PG::ValueType::Enum, {"verticale", "horizontale", "les deux"}, ""},
        {"contentWidth", "Largeur du contenu", PG::ValueType::Real, {}, "0 : jusqu'au bord de ses objets."},
        {"contentHeight", "Hauteur du contenu", PG::ValueType::Real, {}, "0 : jusqu'au bas de ses objets."},
        {"scrollX", "D\xC3\xA9" "filement X", PG::ValueType::Real, {}, "En marche : Vue.Objet.ScrollX (le lire, l'\xC3\xA9" "crire)."},
        {"scrollY", "D\xC3\xA9" "filement Y", PG::ValueType::Real, {}, "En marche : Vue.Objet.ScrollY."},
        {"wheelStep", "Pas de la molette", PG::ValueType::Real, {}, "En pixels."},
        {"showScrollbar", "Barres de d\xC3\xA9" "filement", PG::ValueType::Boolean, {}, "Elles se tirent ; sans elles, la molette seule."},
        {"barColor", "Couleur de la barre", PG::ValueType::Color, {}, ""},
        {"collapsed", "Repli\xC3\xA9", PG::ValueType::Boolean, {},
         "Seul le bandeau se voit ; dans l'\xC3\xA9" "diteur aussi (d\xC3\xA9" "cocher pour \xC3\xA9" "diter son contenu)."},
        {"pushBelow", "Remonter les objets du dessous", PG::ValueType::Boolean, {},
         "Repli\xC3\xA9, il lib\xC3\xA8re sa place : les objets sous lui (m\xC3\xAAme parent) remontent. Plusieurs panneaux "
         "empil\xC3\xA9s font un accord\xC3\xA9on."},
        {"headerHeight", "Hauteur du bandeau", PG::ValueType::Real, {}, ""},
        {"headerColor", "Couleur du bandeau", PG::ValueType::Color, {}, ""},
        {"mapZones", "Zones", PG::ValueType::ReadOnly, {}, "\xC3\x80 r\xC3\xA9gler dans l'onglet Contenu : nom, points, groupe d'alarmes, vue, couleur."},
        {"showNames", "Noms des zones", PG::ValueType::Boolean, {}, ""},
        {"showCounts", "Nombre d'alarmes", PG::ValueType::Boolean, {}, "Une pastille : les alarmes en cours de la zone."},
        {"selectedColor", "Couleur de la zone choisie", PG::ValueType::Color, {}, "Son contour, apr\xC3\xA8s un clic."},
        {"namedStyle", "Style nomm\xC3\xA9", PG::ValueType::Enum, {},
         "Un style du projet (IHM > Styles) : ses couleurs et sa police viennent de lui. Modifier le style modifie tous les objets "
         "qui le suivent (sauf ce qu'on y a chang\xC3\xA9 \xC3\xA0 la main)."},
        // ---- lot 13 : la signature electronique
        {"signature", "Signature \xC3\xA9lectronique", PG::ValueType::Enum, {"aucune", "simple", "double"},
         "simple : avant que la commande agisse, le signataire retape son mot de passe et donne un motif ; double : un second "
         "compte, d'un niveau suffisant, appose son visa. Le journal d'audit garde qui a sign\xC3\xA9, pourquoi, et ce qui a chang\xC3\xA9."},
        {"signatureReasons", "Motifs propos\xC3\xA9s (a;b)", PG::ValueType::Text, {},
         "Les motifs \xC3\xA0 choisir aux fl\xC3\xA8" "ches dans le panneau : R\xC3\xA9glage;Essai;Maintenance. Vide : le motif se tape."},
        {"signatureLevel", "Niveau du visa", PG::ValueType::Integer, {},
         "Signature double : le niveau minimal du second signataire (3 : superviseur)."},
        // ---- lot 16 : le GIF anime
        {"play", "Lecture", PG::ValueType::Enum, {"en boucle", "une fois", "N fois"},
         "en boucle : sans fin ; une fois ; N fois : Nombre de tours. Le GIF garde son propre rythme (les d\xC3\xA9lais de ses images)."},
        {"start", "D\xC3\xA9part", PG::ValueType::Enum, {"\xC3\xA0 l'affichage", "sur action", "sur condition"},
         "\xC3\xA0 l'affichage : d\xC3\xA8s que la vue s'ouvre ; sur action : Jouer le GIF (une action, IHM_GIF_JOUER) ; "
         "sur condition : joue tant que la condition est vraie, revient \xC3\xA0 la premi\xC3\xA8re image quand elle tombe."},
        {"end", "\xC3\x80 la fin", PG::ValueType::Enum, {"derni\xC3\xA8re image", "premi\xC3\xA8re image", "cach\xC3\xA9"},
         "Ce qui reste \xC3\xA0 l'\xC3\xA9" "cran quand la lecture est finie (une fois, N fois)."},
    };
    return k;
}

// Lot 11 : le libelle d'une propriete pour un genre (les graphiques disent
// "Valeurs", un radar "Axes"...) ; vide : celui de la propriete.
std::string labelForKind(hmi::Kind kind, std::string_view key) {
    using K = hmi::Kind;
    if (key == "variables") {
        switch (kind) {
            case K::BarChart: case K::PieChart: return "Valeurs (expressions a;b)";
            case K::RadarChart: return "Axes (expressions a;b)";
            case K::XYChart: return "Courbes : Y (expressions a;b)";
            case K::StateChart: return "Lignes (expressions a;b)";
            case K::VariableTable: return "Variables (a;b)";
            default: return {};
        }
    }
    const bool chart = hmi::kindIsChart(kind);
    if (key == "names" && (chart || kind == K::VariableTable)) return "Noms (a;b)";
    if (key == "colors" && chart && kind != K::Histogram) return "Couleurs (a;b)";
    if (key == "colors" && (kind == K::Histogram || kind == K::AlarmStats)) return "Couleur des barres";
    if (key == "text" && (chart || kind == K::ProductionCounter)) return "Titre";
    if (key == "variable" && kind == K::Histogram) return "Variable mesur\xC3\xA9" "e";
    if (key == "variable" && kind == K::AlarmSummary) return "Variable de la zone choisie";
    if (key == "low" && kind == K::Histogram) return "Tol\xC3\xA9rance basse (LSL)";
    if (key == "high" && kind == K::Histogram) return "Tol\xC3\xA9rance haute (USL)";
    if (key == "low" && kind == K::ProductionCounter) return "TRS faible sous (%)";
    if (key == "high" && kind == K::ProductionCounter) return "TRS bon au-dessus de (%)";
    if (key == "window" && kind == K::Histogram) return "Fen\xC3\xAAtre (s, 0 : tout)";
    if (key == "empty" && hmi::kindIsAlarmView(kind)) return "Texte sans alarme";
    if (key == "period" && kind == K::AlarmBanner) return "D\xC3\xA9" "filement (ms)";
    if (key == "group" && hmi::kindIsAlarmView(kind)) return "Zone (groupe d'alarmes)";
    if (key == "duration" && kind == K::StateChart) return "Dur\xC3\xA9" "e affich\xC3\xA9" "e (s)";
    // lot 12
    if (key == "variable" && kind == K::TabContainer) return "Variable de l'onglet (INT)";
    if (key == "variable" && kind == K::CollapsiblePanel) return "Variable du repli (BOOL)";
    if (key == "variable" && kind == K::ZoneMap) return "Variable de la zone choisie";
    if (key == "image" && kind == K::ZoneMap) return "Plan (image)";
    if (key == "colorOk" && kind == K::ZoneMap) return "Couleur d'une zone calme";
    if (key == "orientation" && kind == K::NavBar) return "Orientation";
    // 1.10.4 : la vanne 3 voies, avec les mots de la maquette (scene 4).
    if (kind == K::ThreeWayValve) {
        if (key == "value") return "Position";
        if (key == "colorOn") return "Couleur du passage";
        if (key == "colorOff") return "Couleur ferm\xC3\xA9" "e";
    }
    return {};
}

const KeyInfo* infoOf(std::string_view key) {
    for (const auto& k : keyInfos()) if (key == k.key) return &k;
    return nullptr;
}

// 1.10.4 (K3) : la section d'une propriete se decide dans hmiPropertyCategories
// (les sections communes, puis celle du genre) ; plus de table categoryOf.

} // namespace

bool hmiPropertyInfo(std::string_view key, std::string& label, std::string& help) {
    const auto* k = infoOf(key);
    if (!k) return false;
    label = k->label;
    help = k->help ? k->help : "";
    return true;
}

// ---- 1.10 (chantier K) : les champs a expression, partout pareils ----
// Ce qu'attend une propriete de l'inspecteur (l'invite de la case vide, l'aide
// a la saisie) : d'apres son type, et pour un texte d'apres sa cle.
static ui::exprfield::Expect expectedOf(std::string_view key, ui::PropertyGrid::ValueType type) {
    using E = ui::exprfield::Expect;
    using VT = ui::PropertyGrid::ValueType;
    switch (type) {
        case VT::Boolean: return E::Bool;
        case VT::Integer: case VT::Real: return E::Number;
        case VT::Color: return E::Color;
        case VT::Enum: return E::List;
        default: break;
    }
    for (const char* k : {"text", "label", "tooltip", "title", "message", "empty"})
        if (key == k) return E::Template;
    for (const char* k : {"condition", "state", "lamp", "fault", "running", "moving", "reset", "visible", "blink", "auth"})
        if (key == k) return E::Bool;
    for (const char* k : {"view", "targetView", "popup"})
        if (key == k) return E::View;
    return E::Value;
}
// ---- fin 1.10 (chantier K) ----

// ---- Lot API 8 : les expressions impossibles ----
// Le controle immediat d'une expression de propriete (celui de Compiler, pour
// une seule) : "" si elle peut marcher, sinon le premier probleme. Les noms :
// ceux de l'IHM, les variables systeme, les vues, les parametres de la vue et
// les globales de l'automate (sans automate, un nom inconnu passe).
std::string hmiExpressionError(const hmi::View& view, std::string_view key, const std::string& expr,
                               const domain::Project* plc, const hmi::Project* project,
                               const std::set<std::string, std::less<>>* plcUpperNames,
                               const hmi::exprcheck::PlcPaths* plcPaths) {
    // 1.11 (REP) : un champ a repere s'analyse comme les autres (ses $ sont
    // transparents : Expression::compile et exprcheck les lisent sans eux) ; l'ancien
    // $Vanne$ qui n'est pas une variable : l'erreur, et la phrase de Dupliquer.
    const auto e = hmi::Expression::compile(expr);
    if (!project) return e.valid() ? std::string{} : e.error();
    const auto upperOf = [](std::string_view s) {
        std::string u(s);
        for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return u;
    };
    hmi::exprcheck::Context ctx;
    ctx.project = project;
    ctx.view = &view;
    ctx.known = [&](std::string_view root) {
        if (project->variable(root) || hmi::isHmiFunction(root) || hmi::pub::isSysRoot(root) || hmi::pub::viewNamed(*project, root)
            || view.param(root) || !plc)
            return true;
        const std::string u = upperOf(root);
        if (plcUpperNames) return plcUpperNames->count(u) > 0;
        for (const auto& v : plc->variables)
            if (v.scope == domain::VariableScope::Global && upperOf(plc->strings.text(v.name)) == u) return true;
        return false;
    };
    if (plcPaths) ctx.plc = *plcPaths;
    if (!e.valid()) {
        // 1.11 (REP, decision 80) : une expression qui ne s'analyse pas garde la phrase de
        // son ancien repere ($Vanne$ qui n'est pas une variable), comme Compiler.
        for (const auto& m : hmi::dup::markersIn(expr))
            if (const auto roots = hmi::scanRoots(m.name); !roots.empty() && !ctx.known(roots.front()))
                return e.error() + " \xE2\x80\x94 " + std::string(hmi::dup::kMarkerHint);
        return e.error();
    }
    const auto problems = hmi::exprcheck::check(ctx, expr, hmi::exprcheck::wantOf(key));
    return problems.empty() ? std::string{} : problems.front().message + hmi::dup::markerHint(expr, problems.front().unknownName);
}

namespace {
// 1.11.1 (API-M) : le modele des variables de l'automate sous API. (HmiApiVars), en
// cache : refait seulement quand les declarations du programme changent (la
// signature : noms, types, adresses, portees, proprietaires, commentaires, unites,
// DDT) - pas a chaque frappe. Sans le projet IHM : Compiler et Generer y mettent la
// liaison du moment (Model::withLink, HmiCheck.cpp).
std::uint64_t plcDeclarationsSignature(const domain::Project& p) {
    std::uint64_t h = 1469598103934665603ull;
    const auto byte = [&h](unsigned char c) {
        h ^= c;
        h *= 1099511628211ull;
    };
    const auto text = [&](std::string_view s) {
        for (const unsigned char c : s) byte(c);
        byte(0xff);
    };
    const auto number = [&](std::uint64_t n) {
        for (int i = 0; i < 8; ++i) byte(static_cast<unsigned char>((n >> (8 * i)) & 0xffu));
    };
    number(p.variables.size());
    number(p.pous.size());
    number(p.derivedTypes.size());
    for (const auto& v : p.variables) {
        text(p.strings.text(v.name));
        text(p.strings.text(v.type.name));
        text(v.address.raw);
        text(p.strings.text(v.comment));
        number(static_cast<std::uint64_t>(v.scope));
        number(static_cast<std::uint64_t>(v.owner));
        number(v.located ? 1u : 0u);
    }
    for (const auto& u : p.pous) {
        text(p.strings.text(u.name));
        number(static_cast<std::uint64_t>(u.kind));
        number(u.parameters.size());
        number(u.locals.size());
    }
    for (const auto& t : p.derivedTypes) {
        text(p.strings.text(t.name));
        number(t.fields.size());
    }
    return h;
}

std::shared_ptr<const hmi::apivars::Model> hmiApiModel(const domain::Project& plc) {
    static std::mutex guard;
    static std::uint64_t lastSignature = 0;
    static std::shared_ptr<const hmi::apivars::Model> last;
    const auto signature = plcDeclarationsSignature(plc);
    const std::lock_guard<std::mutex> lock(guard);
    if (last && lastSignature == signature) return last;
    hmi::apivars::BuildOptions options;
    options.uses = false;   // la verification n'en a pas besoin (l'arbre d'API-V fait les siens)
    last = std::make_shared<const hmi::apivars::Model>(hmi::apivars::Model::build(plc, nullptr, options));
    lastSignature = signature;
    return last;
}
} // namespace

hmi::exprcheck::PlcPaths hmiPlcPaths(const domain::Project* plc) {
    hmi::exprcheck::PlcPaths out;
    if (!plc) return out;
    out.api = hmiApiModel(*plc);   // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable> se verifient
    const auto upperOf = [](std::string_view s) {
        std::string u(s);
        for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return u;
    };
    auto types = std::make_shared<const project::rename::PlcTypes>(*plc);
    auto ddts = std::make_shared<std::set<std::string, std::less<>>>();
    for (const auto& dt : plc->derivedTypes) ddts->insert(upperOf(plc->strings.text(dt.name)));
    out.rootType = [types](std::string_view root) {
        const auto r = types->root(root);
        return r.is == project::rename::RootIs::Plc ? r.type : std::string{};
    };
    out.memberType = [types](std::string_view type, std::string_view member) { return types->memberType(type, member); };
    out.isStruct = [ddts, upperOf](std::string_view type) { return ddts->count(upperOf(type)) > 0; };
    return out;
}

std::shared_ptr<const std::set<std::string, std::less<>>> hmiPlcUpperNames(const domain::Project* plc) {
    auto names = std::make_shared<std::set<std::string, std::less<>>>();
    if (plc)
        for (const auto& v : plc->variables)
            if (v.scope == domain::VariableScope::Global) {
                std::string u(plc->strings.text(v.name));
                for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                names->insert(std::move(u));
            }
    return names;
}
// ---- fin Lot API 8 ----

std::vector<ui::PropertyGrid::Category> hmiPropertyCategories(const hmi::View& view, const std::vector<Id>& selection,
                                                             const domain::Project* plc, const HmiPropertyCommits& commit,
                                                             const hmi::Assets* assets, const hmi::Project* project) {
    std::vector<PG::Category> out;
    if (selection.empty()) {
        PG::Category v;
        v.name = "Vue";
        auto add = [&](std::string name, std::string value, PG::ValueType t, std::string field, std::string help = {},
                       std::vector<std::string> choices = {}) {
            PG::Property p;
            p.name = std::move(name);
            p.value = std::move(value);
            p.type = t;
            p.description = std::move(help);
            p.enumValues = std::move(choices);
            p.commit = [commit, field](std::string_view s) { return commit.view && commit.view(field, std::string(s)); };
            v.properties.push_back(std::move(p));
        };
        add("Nom", view.name, PG::ValueType::Text, "nom");
        add("Description", view.description, PG::ValueType::Text, "description");
        add("Largeur", std::to_string(view.width), PG::ValueType::Integer, "largeur");
        add("Hauteur", std::to_string(view.height), PG::ValueType::Integer, "hauteur");
        add("Couleur de fond", view.background, PG::ValueType::Color, "fond");
        std::vector<std::string> layers;
        std::string active;
        for (const auto& l : view.layers) {
            layers.push_back(l.name);
            if (l.id == view.activeLayer) active = l.name;
        }
        add("Calque actif", active, PG::ValueType::Enum, "calque_actif", "Les objets pos\xC3\xA9s vont dans ce calque.", layers);
        out.push_back(std::move(v));
        // ---- lot 6 : le role de la vue, son ecran modele, son en-tete et son pied
        if (project) {
            PG::Category m;
            m.name = view.role == "popup" ? std::string("Popup et param\xC3\xA8tres") : std::string("R\xC3\xB4le, mod\xC3\xA8le et param\xC3\xA8tres");
            auto addM = [&](std::string name, std::string value, PG::ValueType t, std::string field, std::string help = {},
                            std::vector<std::string> choices = {}) {
                PG::Property p;
                p.name = std::move(name);
                p.value = std::move(value);
                p.type = t;
                p.description = std::move(help);
                p.enumValues = std::move(choices);
                if (t != PG::ValueType::ReadOnly)
                    p.commit = [commit, field](std::string_view s) { return commit.view && commit.view(field, std::string(s)); };
                m.properties.push_back(std::move(p));
            };
            std::vector<std::string> roles;
            for (const auto r : hmi::kViewRoles) roles.emplace_back(hmi::viewRoleLabel(r));
            addM("R\xC3\xB4le", std::string(hmi::viewRoleLabel(view.role)), PG::ValueType::Enum, "role",
                 "Vue : une vue ordinaire. \xC3\x89" "cran mod\xC3\xA8le : d'autres vues en h\xC3\xA9ritent (il se dessine dessous, "
                 "ses scripts passent en premier). Mod\xC3\xA8le d'en-t\xC3\xAAte / de pied : une bande pos\xC3\xA9" "e en haut / en bas "
                 "des vues qui cochent la case. Popup : une vue faite pour s'ouvrir par-dessus (dossier Popups de l'arbre).",
                 roles);
            const auto namesWithRole = [&](std::string_view role, std::string none) {
                std::vector<std::string> names{std::move(none)};
                for (const auto& other : project->views)
                    if (other.role == role && other.id != view.id) names.push_back(other.name);
                return names;
            };
            const auto nameOf = [&](hmi::Id id, std::string none) {
                const auto* t = id != hmi::kNoId ? project->view(id) : nullptr;
                return t ? t->name : none;
            };
            if (view.role == "vue" || view.role == "modele") {
                addM("\xC3\x89" "cran mod\xC3\xA8le", nameOf(view.templateView, "(aucun)"), PG::ValueType::Enum, "modele",
                     "La vue h\xC3\xA9rite de cet \xC3\xA9" "cran : ses objets dessous (estomp\xC3\xA9s ici), ses scripts et ses "
                     "actions de vue ex\xC3\xA9" "cut\xC3\xA9s avant ceux de la vue.",
                     namesWithRole("modele", "(aucun)"));
                addM("En-t\xC3\xAAte", view.showHeader ? "TRUE" : "FALSE", PG::ValueType::Boolean, "entete",
                     "La bande du mod\xC3\xA8le d'en-t\xC3\xAAte, en haut de la vue, par-dessus son contenu.");
                if (view.showHeader)
                    addM("Mod\xC3\xA8le d'en-t\xC3\xAAte", nameOf(view.header, "(le premier)"), PG::ValueType::Enum, "entete_modele", {},
                         namesWithRole("entete", "(le premier)"));
                addM("Pied de page", view.showFooter ? "TRUE" : "FALSE", PG::ValueType::Boolean, "pied",
                     "La bande du mod\xC3\xA8le de pied de page, en bas de la vue.");
                if (view.showFooter)
                    addM("Mod\xC3\xA8le de pied", nameOf(view.footer, "(le premier)"), PG::ValueType::Enum, "pied_modele", {},
                         namesWithRole("pied", "(le premier)"));
            }
            // Lot 8 : une popup - sa barre de titre, son comportement, sa place.
            if (view.role == "popup") {
                const auto& pp = view.popup;
                addM("Barre de titre", pp.titleBar ? "TRUE" : "FALSE", PG::ValueType::Boolean, "popup_titre",
                     "Une barre en haut de la popup, avec son titre : on la tire pour d\xC3\xA9placer la popup.");
                addM("Titre de la popup", pp.title, PG::ValueType::Text, "popup_libelle", "Vide : le nom de la vue.");
                addM("Modale", pp.modal ? "TRUE" : "FALSE", PG::ValueType::Boolean, "popup_modale",
                     "La vue dessous s'assombrit et ne r\xC3\xA9pond plus tant que la popup est ouverte.");
                addM("D\xC3\xA9pla\xC3\xA7" "able", pp.movable ? "TRUE" : "FALSE", PG::ValueType::Boolean, "popup_deplacable",
                     "Tir\xC3\xA9" "e par sa barre de titre.");
                addM("Bouton fermer", pp.closeButton ? "TRUE" : "FALSE", PG::ValueType::Boolean, "popup_fermer",
                     "La croix de la barre de titre.");
                addM("Fermer au clic dehors", pp.closeOutside ? "TRUE" : "FALSE", PG::ValueType::Boolean, "popup_dehors",
                     "Un clic hors de la popup la ferme.");
                std::vector<std::string> places;
                for (const auto pl : hmi::kPopupPlacements) places.emplace_back(hmi::popupPlacementLabel(pl));
                addM("Position", std::string(hmi::popupPlacementLabel(pp.placement)), PG::ValueType::Enum, "popup_position",
                     "O\xC3\xB9 elle s'ouvre, sauf si l'action en d\xC3\xA9" "cide autrement. \xC2\xAB x,y \xC2\xBB : une place fixe "
                     "dans la vue du dessous.",
                     places);
            }
            // Lot 12 : la navigation - la vue parente (le fil d'Ariane), le zoom en marche.
            if (view.role == "vue") {
                std::vector<std::string> ups{"(aucune)"};
                for (const auto& other : project->views)
                    if (other.role == "vue" && other.id != view.id) ups.push_back(other.name);
                const auto* up = view.upView != hmi::kNoId ? project->view(view.upView) : nullptr;
                addM("Vue parente", up ? up->name : std::string("(aucune)"), PG::ValueType::Enum, "vue_parente",
                     "Celle d'o\xC3\xB9 l'on vient dans la hi\xC3\xA9rarchie des vues : Ligne_1 a pour parente Production, qui a "
                     "Accueil. Le fil d'Ariane la montre (Accueil \xE2\x80\xBA Production \xE2\x80\xBA Ligne 1).",
                     ups);
                addM("Zoom en marche", view.zoomable ? "TRUE" : "FALSE", PG::ValueType::Boolean, "zoom",
                     "Une grande vue (un plan, un synoptique) : en marche, la molette zoome autour de la souris et le fond se tire. "
                     "Double-clic sur le fond : 100 %.");
            }
            // 1.9 : une popup et un symbole ont la sous-section Parametres | Infos (plus bas).
            const bool paramList19 = view.role == "popup" || hmi::isSymbolView(view);
            if (!paramList19) {
                std::string params;
                for (const auto& prm : view.params)
                    params += (params.empty() ? "" : "; ") + prm.name + " := " + prm.defaultValue;
                addM("Param\xC3\xA8tres", params, PG::ValueType::Text, "parametres",
                     "Les noms que l'appelant relie \xC3\xA0 l'ouverture : Moteur := Pompes[0]; Titre := 'Pompe 1'. "
                     "Dans la vue, Moteur.Marche se lit comme Pompes[3].Marche quand on l'ouvre avec Moteur := Pompes[3] ; "
                     "la valeur apr\xC3\xA8s := sert dans l'\xC3\xA9" "diteur et quand l'appelant ne la donne pas.");
            }
            if (hmi::isTemplateRole(view.role)) {
                const auto users = hmi::viewsUsing(*project, view.id);
                std::string names;
                for (std::size_t k = 0; k < users.size() && k < 6; ++k) names += (k ? ", " : "") + users[k]->name;
                if (users.size() > 6) names += "...";
                addM("Utilis\xC3\xA9 par", users.empty() ? std::string("aucune vue") : std::to_string(users.size()) + " vue(s) : " + names,
                     PG::ValueType::ReadOnly, "utilise_par");
            }
            out.push_back(std::move(m));
            if (paramList19)   // 1.9 : la liste typee et les infos (HmiParamPanes)
                for (auto& c : hmiparams::paramCategories(*project, view, plc, commit.view)) out.push_back(std::move(c));
        }
        PG::Category g;
        g.name = "Grille et rep\xC3\xA8res";
        auto addG = [&](std::string name, std::string value, PG::ValueType t, std::string field) {
            PG::Property p;
            p.name = std::move(name);
            p.value = std::move(value);
            p.type = t;
            p.commit = [commit, field](std::string_view s) { return commit.view && commit.view(field, std::string(s)); };
            g.properties.push_back(std::move(p));
        };
        addG("Grille visible", view.grid.visible ? "TRUE" : "FALSE", PG::ValueType::Boolean, "grille");
        addG("Pas de la grille", std::to_string(view.grid.step), PG::ValueType::Integer, "pas");
        addG("Magn\xC3\xA9tisme : grille", view.grid.snapGrid ? "TRUE" : "FALSE", PG::ValueType::Boolean, "mag_grille");
        addG("Magn\xC3\xA9tisme : objets", view.grid.snapObjects ? "TRUE" : "FALSE", PG::ValueType::Boolean, "mag_objets");
        addG("Magn\xC3\xA9tisme : guides", view.grid.snapGuides ? "TRUE" : "FALSE", PG::ValueType::Boolean, "mag_guides");
        addG("Guides", std::to_string(view.guides.size()), PG::ValueType::ReadOnly, "guides");
        out.push_back(std::move(g));
        return out;
    }

    const auto* o = view.object(selection.front());
    if (!o) return out;
    const bool many = selection.size() > 1;

    PG::Category general;
    general.name = many ? "Objet (" + std::to_string(selection.size()) + " s\xC3\xA9lectionn\xC3\xA9s)" : "Objet";
    auto meta = [&](std::string name, std::string value, PG::ValueType t, std::string field, std::string help = {},
                    std::vector<std::string> choices = {}) {
        PG::Property p;
        p.name = std::move(name);
        p.value = std::move(value);
        p.type = t;
        p.description = std::move(help);
        p.enumValues = std::move(choices);
        if (t != PG::ValueType::ReadOnly)
            p.commit = [commit, field](std::string_view s) { return commit.meta && commit.meta(field, std::string(s)); };
        general.properties.push_back(std::move(p));
    };
    if (!many) meta("Nom", o->name, PG::ValueType::Text, "nom", "Lettres, chiffres et _ ; unique dans la vue.");
    meta("Type", std::string(hmi::kindLabel(o->kind)), PG::ValueType::ReadOnly, "type",
         "Le genre de l'objet, choisi dans la biblioth\xC3\xA8que ; il ne change pas (F1 ouvre sa page d'aide).");
    std::vector<std::string> layers;
    std::string layerName;
    for (const auto& l : view.layers) {
        layers.push_back(l.name);
        if (l.id == o->layer) layerName = l.name;
    }
    meta("Calque", layerName, PG::ValueType::Enum, "calque",
         "Le calque de la vue qui porte l'objet : un calque cach\xC3\xA9 ou verrouill\xC3\xA9 le cache ou le verrouille avec lui.", layers);
    meta("Verrouill\xC3\xA9", o->locked ? "TRUE" : "FALSE", PG::ValueType::Boolean, "verrou",
         "Ni pris \xC3\xA0 la souris, ni d\xC3\xA9plac\xC3\xA9. L'explorateur le prend quand m\xC3\xAAme.");
    meta("Cach\xC3\xA9 dans l'\xC3\xA9" "diteur", o->hidden ? "TRUE" : "FALSE", PG::ValueType::Boolean, "cache",
         "Seulement dans l'\xC3\xA9" "diteur. En marche, c'est la propri\xC3\xA9t\xC3\xA9 Visible qui compte.");
    out.push_back(std::move(general));

    // ---- 1.10.4 (K3) : l'inspecteur range ----
    // D'abord les sections COMMUNES a tous les objets, dans le meme ordre pour
    // tous : Objet (ci-dessus), Position et taille, Apparence, Securite - les
    // Alarmes de l'objet suivent (l'editeur les pose apres Securite). Puis UNE
    // section propre a l'objet, au nom de son genre ("Vanne") : ses proprietes
    // dans l'ordre d'un objet neuf de ce genre.
    //  Plus de section Communication : la variable d'un objet va avec lui (la
    //  variable ecrite d'une commande...), son adresse dans l'aide de sa case, et
    //  la cadence "refresh" n'etait lue nulle part (la scrutation se regle dans
    //  les Variables IHM et la table d'echanges). Plus de section Animation : chaque
    //  case prend =expression, "Valeur (expression)" doublait la Valeur.
    // 1.11.2 (SYM, decision 240) : SecParams - les parametres du symbole d'une instance, une ligne chacun.
    enum Section : std::size_t { SecPosition, SecLook, SecSecurity, SecType, SecParams, SecCount };
    struct Row { double rank; PG::Property p; };
    std::array<std::vector<Row>, SecCount> rows;
    const auto rankOf = [](std::initializer_list<const char*> keys, std::string_view key) {
        int i = 0;
        for (const char* k : keys) {
            if (key == k) return i;
            ++i;
        }
        return -1;
    };
    const std::initializer_list<const char*> kPositionKeys{"x", "y", "w", "h", "rot", "pivotX", "pivotY", "flipH", "flipV", "points",
                                                         "tabPage"};
    const std::initializer_list<const char*> kLookKeys{"visible", "opacity", "blink", "blinkColor", "namedStyle", "fill", "background",
                                                     "stroke", "strokeWidth", "radius", "font", "fontSize", "textColor", "align", "wrap"};
    const std::initializer_list<const char*> kSecurityKeys{"access", "profile", "auth", "signature", "signatureReasons", "signatureLevel"};
    // Le nom de la section du genre ; plusieurs objets de genres differents : celle du premier.
    std::string typeName(hmi::kindLabel(o->kind));
    if (many)
        for (Id id : selection)
            if (const auto* other = view.object(id); other && other->kind != o->kind) {
                typeName += " (le premier objet)";
                break;
            }

    // Lot 9 : les proprietes du genre qu'un objet plus ancien n'a pas encore (un
    // bouton d'avant la confirmation) : montrees a leur valeur par defaut,
    // posees a la premiere saisie.
    const hmi::Object fresh = hmi::makeObject(o->kind, hmi::kNoId, {}, 0, 0, hmi::kNoId);
    std::vector<hmi::Prop> allProps = o->props;
    for (const auto& d : fresh.props)
        if (!o->find(d.key)) allProps.push_back(d);
    // 1.10.4 (K3) : le clignotement, pour tout objet (il etait dans Animation, meme absent).
    if (!o->find("blink")) allProps.push_back({"blink", "FALSE", ""});
    // Lot 12 : le style nomme, des que le projet en a (choisi : ses valeurs s'appliquent).
    if (project && !project->styles.empty() && !o->find("namedStyle")) allProps.push_back({"namedStyle", "", ""});
    // Lot 13 : l'unite et le format de la variable (Configuration > Unites et
    // formats) - pour les objets qui montrent une variable avec une unite ou un format.
    {
        const bool charts = o->kind == hmi::Kind::BarChart || o->kind == hmi::Kind::XYChart || o->kind == hmi::Kind::StateChart
                         || o->kind == hmi::Kind::PieChart || o->kind == hmi::Kind::RadarChart || o->kind == hmi::Kind::Histogram;
        if (!charts && (o->find("unit") || o->find("format")) && !o->find("varFormat")) allProps.push_back({"varFormat", "TRUE", ""});
        if (o->kind == hmi::Kind::VariableTable && !o->find("formats")) allProps.push_back({"formats", "", ""});
    }
    // Lot 13 : la signature electronique des commandes (absente : aucune).
    if (hmi::kindSignable(o->kind)) {
        if (!o->find("signature")) allProps.push_back({"signature", "aucune", ""});
        if (!o->find("signatureReasons")) allProps.push_back({"signatureReasons", "", ""});
        if (!o->find("signatureLevel")) allProps.push_back({"signatureLevel", "3", ""});
    }
    // L'ordre de la section du genre : celui d'un objet neuf ; ce qu'il n'a pas, apres.
    const auto freshRank = [&](std::string_view key) -> double {
        for (std::size_t i = 0; i < fresh.props.size(); ++i)
            if (fresh.props[i].key == key) return static_cast<double>(i);
        return -1.0;
    };
    // La variable : en tete de la section du genre (celle qu'ecrit une commande),
    // juste apres la Valeur pour un objet qui montre une valeur.
    const double variableRank = freshRank("value") >= 0 ? freshRank("value") + 0.5 : -1.0;

    std::shared_ptr<const hmi::exprcheck::PlcPaths> exprPaths;   // ---- Lot API 8 : les expressions impossibles (une fois) ----
    for (std::size_t propIndex = 0; propIndex < allProps.size(); ++propIndex) {
        const auto& prop = allProps[propIndex];
        if (prop.key == "auth" || prop.key == "access") continue;   // montrees plus bas (Securite)
        if (prop.key == "refresh") continue;              // 1.10.4 (K3) : jamais lue (gardee dans le fichier)
        // 1.10.4 (K3) : la variable d'un objet qui n'en fait rien de plus que sa Valeur
        // (=expression) ne se montre que si elle est deja reglee (un projet d'avant).
        if (prop.key == "variable" && !hmiObjectNeedsVariable(o->kind) && prop.expr.empty()
            && prop.value.find_first_not_of(" \t") == std::string::npos)
            continue;
        const auto* info = infoOf(prop.key);
        PG::Property p;
        p.name = info ? info->label : prop.key;
        if (!prop.expr.empty()) p.name += "  \xC6\x92";   // f crochet : pilote par une expression
        // ---- Lot API 8 : les expressions impossibles ---- (la pastille fx, rouge si elle ne peut pas marcher)
        if (!prop.expr.empty()) {
            p.expression = prop.expr;
            if (!exprPaths) exprPaths = std::make_shared<const hmi::exprcheck::PlcPaths>(hmiPlcPaths(plc));
            p.exprError = hmiExpressionError(view, prop.key, prop.expr, plc, project, nullptr, exprPaths.get());
        }
        p.value = prop.value;
        p.type = info ? info->type : PG::ValueType::Text;
        if (info) p.enumValues = info->choices;
        // Les ressources du projet, en liste : on choisit une image parmi les
        // images, une police parmi les polices ; la source d'un Tableau parmi
        // les fichiers externes. Une valeur qui n'y est plus reste affichee.
        {
            const auto kindFor = [](std::string_view k) {
                return k == "video" ? hmi::MediaKind::Video : k == "font" ? hmi::MediaKind::Font : hmi::MediaKind::Image;
            };
            if (hmi::citesResource(prop.key)) {
                p.type = PG::ValueType::Enum;
                p.enumValues = prop.key == "font" ? std::vector<std::string>{"Sans", "Mono"} : std::vector<std::string>{""};
                if (assets)
                    for (const auto& r : assets->resources)
                        if (r.kind() == kindFor(prop.key)) p.enumValues.push_back(r.name);
            } else if (prop.key == "source" && (o->kind == hmi::Kind::Table || o->kind == hmi::Kind::Trend)) {
                p.name = o->kind == hmi::Kind::Trend ? "Source historique (fichier externe)" : "Fichier externe";
                p.type = PG::ValueType::Enum;
                p.enumValues = {""};
                if (assets)
                    for (const auto& f : assets->files)
                        if (f.kind != hmi::ExternalKind::Document)   // lot API 8 : un document ne se lit pas en lignes
                            p.enumValues.push_back(f.name);
                if (o->kind == hmi::Kind::Trend)
                    info = nullptr;       // l'aide ci-dessous, pas celle de l'Historique
            } else if (prop.key == "mode" && o->kind == hmi::Kind::InputField) {
                // Lot 8 : le mode d'un champ de saisie (pas celui d'une courbe).
                p.type = PG::ValueType::Enum;
                p.enumValues = {"num\xC3\xA9rique", "texte", "mot de passe"};
            } else if (prop.key == "recipe" && project) {
                p.enumValues = {""};
                for (const auto& r : project->recipes) p.enumValues.push_back(r.name);
            } else if (prop.key == "symbol" && o->kind == hmi::Kind::SymbolInstance && project) {
                // Lot 10 : les symboles du projet.
                p.type = PG::ValueType::Enum;
                p.enumValues = {};
                for (const auto* sv : hmi::symbolsOf(*project)) p.enumValues.push_back(sv->name);
            } else if (prop.key == "alarm" && o->kind == hmi::Kind::AlarmInstruction && project) {
                // Lot 11 : les alarmes du projet (vide : celle qu'on choisit d'un clic).
                p.type = PG::ValueType::Enum;
                p.enumValues = {""};
                for (const auto& a : project->alarms) p.enumValues.push_back(a.name);
            } else if (prop.key == "exportSource" && o->kind == hmi::Kind::ExportButton) {
                // Lot 11 : ce qui s'exporte - les listes, les recettes, les objets de la vue qui ont des donnees.
                p.type = PG::ValueType::Enum;
                p.enumValues.clear();
                for (const auto src : hmi::kExportSources) p.enumValues.emplace_back(src);
                if (project) for (const auto& r : project->recipes) p.enumValues.push_back("recette:" + r.name);
                for (const auto& other : view.objects)
                    if (other.kind == hmi::Kind::Trend || other.kind == hmi::Kind::Table || hmi::kindIsChart(other.kind)
                        || other.kind == hmi::Kind::VariableTable || other.kind == hmi::Kind::ProductionCounter
                        || other.kind == hmi::Kind::AlarmStats || other.kind == hmi::Kind::History)
                        p.enumValues.push_back("objet:" + other.name);
            } else if (prop.key == "tab" && o->kind == hmi::Kind::LoginMenuButton) {
                // Lot 12 : les onglets du menu de connexion (celui qu'ouvre le clic).
                p.type = PG::ValueType::Enum;
                p.enumValues.clear();
                for (const auto& t : hmi::kLoginTabs) p.enumValues.emplace_back(t.label);
                p.name = "Onglet ouvert";
            } else if (prop.key == "namedStyle" && project) {
                // Lot 12 : les styles nommes du projet.
                p.type = PG::ValueType::Enum;
                p.enumValues = {""};
                for (const auto& st : project->styles) p.enumValues.push_back(st.name);
            } else if (prop.key == "group" && (o->kind == hmi::Kind::History || hmi::kindIsAlarmView(o->kind)) && project) {
                p.type = PG::ValueType::Enum;
                p.enumValues = {""};
                for (const auto& a : project->alarms)
                    if (!a.group.empty() && std::find(p.enumValues.begin(), p.enumValues.end(), a.group) == p.enumValues.end())
                        p.enumValues.push_back(a.group);
                // 1.9 : les groupes des objets (Vue.*, Vue.Objet) et les symboles (symbole:Nom) - maquette A4.
                for (const auto& gc : objalarms::groupChoices(*project))
                    if (std::find(p.enumValues.begin(), p.enumValues.end(), gc.value) == p.enumValues.end()) p.enumValues.push_back(gc.value);
            }
            if (p.type == PG::ValueType::Enum && std::find(p.enumValues.begin(), p.enumValues.end(), prop.value) == p.enumValues.end())
                p.enumValues.push_back(prop.value);
        }
        if (prop.key == "cells" && prop.expr.empty()) {
            std::size_t nrows = 0, cols = 0, driven = 0;
            for (const auto& row : hmi::parseCells(prop.value)) {
                ++nrows;
                cols = std::max(cols, row.size());
                for (const auto& c : row) driven += hmi::cellIsExpression(c) || hmi::cellIsTemplate(c);
            }
            p.value = std::to_string(nrows) + " ligne(s) x " + std::to_string(cols) + " colonne(s)"
                    + (driven ? ", " + std::to_string(driven) + " dynamique(s)" : std::string{});
            p.type = PG::ValueType::ReadOnly;
        } else if (prop.key == "mapZones" && prop.expr.empty()) {
            const auto zones = hmi::parseMapZones(prop.value);
            p.value = zones.empty() ? std::string("aucune (onglet Contenu)") : std::to_string(zones.size()) + " zone(s)";
            p.type = PG::ValueType::ReadOnly;
        } else if (prop.key == "states" && prop.expr.empty()) {
            const auto states = hmi::parseImageStates(prop.value);
            p.value = states.empty() ? std::string("aucun (onglet Contenu)") : std::to_string(states.size()) + " \xC3\xA9tat(s)";
            p.type = PG::ValueType::ReadOnly;
        }
        // Lot 8 : la bulle d'aide vient du guide (la page de l'objet, sinon les
        // parametres communs) - la meme description que l'aide F1 et le guide Word.
        std::string help = hmi::guide::paramHelp(hmi::kindKey(o->kind), prop.key);
        if (help.empty()) help = info ? info->help : "";
        if ((o->kind == hmi::Kind::LoginPanel || o->kind == hmi::Kind::PasswordChange) && prop.key == "text") p.name = "Titre";
        if (prop.key == "source" && o->kind == hmi::Kind::Trend)
            help = "Mode historique. Vide : les mesures archiv\xC3\xA9" "es (Configuration > Historiques). Un fichier externe : "
                   "1re colonne le temps (2026-09-22 07:40:12 ou des secondes), puis une colonne par plume (par son nom, "
                   "sinon dans l'ordre).";
        // 1.10.4 (K3) : la variable de l'objet, sous le nom de ce qu'elle fait pour lui ;
        // son adresse automate (l'ancienne ligne de Communication) dans son aide.
        if (prop.key == "variable") {
            p.name = hmiVariableLabel(o->kind) + (prop.expr.empty() ? std::string{} : std::string("  \xC6\x92"));
            if (!hmiObjectNeedsVariable(o->kind))
                help = hmi::kindShowsValue(o->kind)
                           ? std::string("Montr\xC3\xA9" "e quand Valeur n'a pas d'expression (un projet d'avant la 1.10.4). Plus simple : "
                                         "tape =la variable dans Valeur, puis vide cette case.")
                           : std::string("La variable de l'automate que l'objet repr\xC3\xA9sente (un projet d'avant la 1.10.4 ; "
                                         "la liaison avec l'automate se r\xC3\xA8gle dans les Variables IHM). Vide-la si elle ne sert plus.");
            if (plc && prop.value.find_first_not_of(" \t") != std::string::npos)
                help += (help.empty() ? "" : "\n") + std::string("Adresse automate : ") + plcAddressOf(plc, prop.value) + ".";
        }
        if (!prop.expr.empty()) help = "Expression : =" + prop.expr + (help.empty() ? "" : "\n" + help);
        help += (help.empty() ? "" : "\n")
              + std::string("Tape =expression pour la piloter. Pour la retirer : \xE2\x9C\x95 au bout de la ligne, clic droit, "
                            "ou efface le texte.");
        p.description = help;
        const std::string key = prop.key;
        p.commit = [commit, key](std::string_view s) {
            if (!commit.prop) return false;
            // 1.10 (chantier K) : "Revenir a la valeur par defaut" (HmiEditor::commitProp).
            if (s == ui::exprfield::kToDefault) return commit.prop(key, std::string(s), false);
            if (!s.empty() && s.front() == '=') {
                // 1.10 (chantier K) : "=" seul (ou "=  ") retire l'expression.
                std::string e(s.substr(1));
                while (!e.empty() && (e.front() == ' ' || e.front() == '\t')) e.erase(e.begin());
                while (!e.empty() && (e.back() == ' ' || e.back() == '\t')) e.pop_back();
                return commit.prop(key, e, true);
            }
            return commit.prop(key, std::string(s), false);
        };
        if (const std::string special = labelForKind(o->kind, prop.key); !special.empty() && prop.key != "variable")
            p.name = special + (prop.expr.empty() ? std::string{} : std::string("  \xC6\x92"));
        // Lot 16 : le GIF anime - sa lecture, son depart, sa fin ("count", "speed"
        // et "condition" n'y disent pas ce qu'ils disent ailleurs).
        if (o->kind == hmi::Kind::AnimatedGif) {
            const std::string mark = prop.expr.empty() ? std::string{} : std::string("  \xC6\x92");
            const auto choose = [&](std::vector<std::string> values) {
                p.type = PG::ValueType::Enum;
                p.enumValues = std::move(values);
                if (std::find(p.enumValues.begin(), p.enumValues.end(), prop.value) == p.enumValues.end()) p.enumValues.push_back(prop.value);
            };
            if (prop.key == "image") {
                // Seules les ressources GIF (une image fixe ne s'anime pas).
                p.name = "GIF (ressource)" + mark;
                p.enumValues = {""};
                if (assets)
                    for (const auto& r : assets->resources)
                        if (r.format == "GIF") p.enumValues.push_back(r.name);
                if (std::find(p.enumValues.begin(), p.enumValues.end(), prop.value) == p.enumValues.end()) p.enumValues.push_back(prop.value);
            } else if (prop.key == "play") {
                p.name = "Lecture" + mark;
                choose({"en boucle", "une fois", "N fois"});
            } else if (prop.key == "count") {
                p.name = "Nombre de tours (N fois)" + mark;
                p.type = PG::ValueType::Integer;
                p.enumValues.clear();
            } else if (prop.key == "speed") {
                p.name = "Vitesse (%)" + mark;
                p.type = PG::ValueType::Real;
            } else if (prop.key == "start") {
                p.name = "D\xC3\xA9part" + mark;
                choose({"\xC3\xA0 l'affichage", "sur action", "sur condition"});
            } else if (prop.key == "condition") {
                p.name = "Condition (joue tant qu'elle est vraie)" + mark;
                p.type = PG::ValueType::Text;
            } else if (prop.key == "end") {
                p.name = "\xC3\x80 la fin" + mark;
                choose({"derni\xC3\xA8re image", "premi\xC3\xA8re image", "cach\xC3\xA9"});
            }
        }
        // 1.10 (chantier K) : toute propriete modifiable de l'inspecteur accepte "=expression".
        if (p.commit && p.type != PG::ValueType::ReadOnly) ui::exprfield::mark(p, expectedOf(prop.key, p.type), true);
        // La section, et la place dans la section.
        Section section = SecType;
        double rank = 0;
        // 1.11 (R111) : la Taille du texte d'une liste d'alarmes / historique va dans
        // Apparence, comme pour les autres objets (kLookKeys, 1.10.4) - pas d'exception.
        if (const int r = rankOf(kPositionKeys, prop.key); r >= 0) { section = SecPosition; rank = r; }
        else if (const int r2 = rankOf(kLookKeys, prop.key); r2 >= 0) { section = SecLook; rank = r2; }
        else if (const int r3 = rankOf(kSecurityKeys, prop.key); r3 >= 0) { section = SecSecurity; rank = r3 * 10.0; }
        else if (prop.key == "variable") rank = variableRank;
        else if (const double fr = freshRank(prop.key); fr >= 0) rank = fr;
        else rank = 1000.0 + static_cast<double>(propIndex);
        // 1.11.2 (SYM, decision 240 ; la capture du client « Arguments : fx Voiture;50 ») : une instance
        // seule dont le symbole declare des parametres - le champ Arguments d'un seul tenant laisse la
        // place a la section « Parametres du symbole », une ligne par parametre (plus bas). Plusieurs
        // objets choisis, un symbole introuvable ou sans parametre : la ligne Arguments, comme avant.
        const hmi::View* paramsOf = (prop.key == "params" && o->kind == hmi::Kind::SymbolInstance && !many && project)
                                        ? hmi::symbolOf(*project, *o) : nullptr;
        if (paramsOf && paramsOf->params.empty()) paramsOf = nullptr;
        if (!paramsOf) rows[section].push_back({rank, std::move(p)});
        if (paramsOf) {
            // Le symbole, reduit a ses parametres (la ligne le garde apres que l'inspecteur est refait).
            hmi::View shape;
            shape.role = paramsOf->role;
            shape.params = paramsOf->params;
            const auto given = hmi::givenArguments(shape, prop.value);
            const valuekind::Env env{project, &view, plc};
            for (std::size_t i = 0; i < shape.params.size(); ++i) {
                const auto& prm = shape.params[i];
                PG::Property a;
                a.name = prm.name + " \xC2\xB7 " + (prm.type.empty() ? std::string("ANY") : prm.type);
                a.type = PG::ValueType::Text;
                // 1.11.3 (le fx qu'on ne voyait plus ; Voiture refuse) : l'argument tel que le moteur
                // le prend. Une constante ('Voiture', 50, TRUE) se montre sans fx ni apostrophes ; un
                // mot qui n'est pas une variable est une constante du type (Voiture -> 'Voiture') ; le
                // reste (une variable : UINTS, un calcul) est une formule et garde son fx.
                const std::string arg = i < given.size() ? given[i] : std::string{};
                const std::string type = prm.type.empty() ? std::string("ANY") : prm.type;
                const std::string effective = arg.empty() ? arg : hmi::effectiveArgument(project, type, arg);
                const bool formula = !effective.empty() && !hmi::isLiteralArgument(effective);
                if (formula) {
                    a.expression = effective;
                    a.value = effective;
                } else {
                    a.value = hmi::shownLiteral(effective);
                }
                const std::string def = prm.defaultValue.empty() ? std::string("aucune") : prm.defaultValue;
                // Vide : la valeur par defaut du symbole, montree en gris (le texte d'attente de la case).
                if (a.value.empty()) a.placeholder = "(d\xC3\xA9" "faut : " + def + ")";
                std::string tip = prm.description.empty()
                                       ? (prm.type.empty() ? std::string("Le param\xC3\xA8tre accepte tout (ANY) : une variable, un chemin, une valeur.")
                                                           : "Attend : " + prm.type + " - une variable, un chemin (Armoires[1]), une expression ou une valeur.")
                                       : prm.description;
                tip += "\nVide : la valeur par d\xC3\xA9" "faut du symbole (" + def + "). Sans fx : une constante, convertie en " + type
                     + " (Voiture devient 'Voiture') ; un nom de variable reste la variable. Le bouton fx : une variable ou une expression.";
                a.description = std::move(tip);
                const auto res = valuekind::classify(env, formula ? effective : a.value, formula, type);
                if (formula && res.error()) a.exprError = valuekind::firstProblem(res);
                a.legend = valuekind::legendOf(res);
                a.legend->expected = type;
                const std::string current = prop.value;
                a.commit = [commit, shape, current, i, type, project](std::string_view s) {
                    if (!commit.prop) return false;
                    std::string v(s);
                    if (v == ui::exprfield::kToDefault) {
                        v.clear();
                    } else {
                        const std::size_t a0 = v.find_first_not_of(" \t"), a1 = v.find_last_not_of(" \t");
                        v = a0 == std::string::npos ? std::string{} : v.substr(a0, a1 - a0 + 1);
                        if (!v.empty() && v.front() == '=') {
                            // fx : la formule telle quelle (=UINTS : la variable).
                            v.erase(v.begin());
                            const std::size_t b0 = v.find_first_not_of(" \t");
                            v = b0 == std::string::npos ? std::string{} : v.substr(b0);
                        } else if (!v.empty()) {
                            // Sans fx : la constante, convertie dans le type du parametre. Une saisie
                            // inchangee garde ce qui etait ecrit ; un nom de variable reste la variable.
                            const auto before = hmi::givenArguments(shape, current);
                            const std::string was = i < before.size() ? before[i] : std::string{};
                            if (!was.empty() && hmi::isLiteralArgument(was) && hmi::shownLiteral(was) == v) v = was;
                            else v = hmi::effectiveArgument(project, type, v);
                        }
                    }
                    // Une saisie = une commande (un seul Ctrl+Z) : le texte entier de l'instance, en forme nommee.
                    return commit.prop("params", hmi::withArgument(shape, current, i, v), false);
                };
                ui::exprfield::mark(a, expectedOf(prop.key, PG::ValueType::Text), true);
                rows[SecParams].push_back({static_cast<double>(i), std::move(a)});
            }
        }
        // Lot 10 : ce qu'attend le symbole d'une instance - ses parametres, leur
        // valeur par defaut, et ce que l'instance leur donne.
        if (prop.key == "params" && o->kind == hmi::Kind::SymbolInstance) {
            const hmi::View* sv = project ? hmi::symbolOf(*project, *o) : nullptr;
            PG::Property a;
            a.name = "Param\xC3\xA8tres du symbole";
            a.type = PG::ValueType::ReadOnly;
            if (!sv) {
                a.value = "(symbole introuvable)";
            } else if (sv->params.empty()) {
                a.value = "aucun (le symbole ne d\xC3\xA9" "clare rien)";
            } else {
                for (const auto& prm : sv->params)
                    a.value += (a.value.empty() ? "" : "; ") + prm.name + (prm.defaultValue.empty() ? "" : " (" + prm.defaultValue + ")");
            }
            a.description = "D\xC3\xA9" "clar\xC3\xA9s dans les propri\xC3\xA9t\xC3\xA9s du symbole (sa vue : Param\xC3\xA8tres), avec leur "
                            "valeur par d\xC3\xA9" "faut. Double-clic sur l'instance : ouvrir le symbole.";
            // 1.11.2 (SYM) : avec la section, le resume en lecture seule ne sert plus (chaque ligne le dit).
            if (!paramsOf) rows[SecType].push_back({rank + 0.1, std::move(a)});
            if (sv) {
                PG::Property u;
                u.name = "Instances de ce symbole";
                u.type = PG::ValueType::ReadOnly;
                u.value = std::to_string(hmi::instancesOf(*project, sv->name).size());
                u.description = "Dans tout le projet : toutes suivent le symbole.";
                rows[SecType].push_back({rank + 0.2, std::move(u)});
            }
        }
    }
    // LA SECURITE (lot 4), pour tout objet : le niveau d'acces, le profil qui
    // le donne (un groupe d'utilisateurs : son niveau devient celui de
    // l'objet) et l'autorisation par expression. Posees a la premiere saisie.
    {
        auto& sc = rows[SecSecurity];
        // 1.10.4 (K3) : UNE case pour le niveau d'acces - les groupes d'utilisateurs du
        // projet (leur niveau devient celui de l'objet) et les niveaux 1 a 4 ; elle
        // remplace "Niveau d'acces" (un nombre) et "Profil requis" (le groupe), qui
        // ecrivaient tous deux "access".
        const int level = static_cast<int>(o->number("access", 0));
        PG::Property lv;
        lv.name = infoOf("access")->label;
        lv.type = PG::ValueType::Enum;
        lv.enumValues = {"tout le monde (0)"};
        lv.value = lv.enumValues.front();
        std::vector<int> levels{0};
        if (project)
            for (const auto& g : project->security.groups) {
                std::string label = g.name + " (" + std::to_string(g.level) + ")";
                if (g.level == level && level > 0 && lv.value == lv.enumValues.front()) lv.value = label;
                levels.push_back(g.level);
                lv.enumValues.push_back(std::move(label));
            }
        for (int n = 1; n <= 4; ++n)
            if (std::find(levels.begin(), levels.end(), n) == levels.end()) lv.enumValues.push_back("niveau " + std::to_string(n));
        if (level > 0 && lv.value == lv.enumValues.front()) {
            lv.value = "niveau " + std::to_string(level);
            if (std::find(lv.enumValues.begin(), lv.enumValues.end(), lv.value) == lv.enumValues.end()) lv.enumValues.push_back(lv.value);
        }
        if (const auto* ap = o->find("access"); ap && !ap->expr.empty()) lv.expression = ap->expr;   // un projet d'avant : gardee
        lv.description = std::string(infoOf("access")->help)
                       + "\nUn groupe d'utilisateurs : son niveau devient celui de l'objet (Configuration > S\xC3\xA9" "curit\xC3\xA9).";
        lv.commit = [commit](std::string_view v) {
            // "Superviseur (3)" -> 3 ; "niveau 2" -> 2 ; "2" -> 2.
            std::string n;
            const auto open = v.rfind('('), close = v.rfind(')');
            if (open != std::string_view::npos && close != std::string_view::npos && close > open) v = v.substr(open + 1, close - open - 1);
            for (const char ch : v)
                if (std::isdigit(static_cast<unsigned char>(ch))) n += ch;
            if (n.empty()) n = "0";
            return commit.prop && commit.prop("access", n, false);
        };
        sc.push_back({0.0, std::move(lv)});
        PG::Property auth;
        auth.name = "Autorisation (expression)";
        auth.value = o->text("auth");
        auth.type = PG::ValueType::Text;
        auth.description = "L'objet ne r\xC3\xA9pond que si elle est vraie (s\xC3\xA9" "curit\xC3\xA9 active) : "
                           "Cle_Maintenance, Armoires[0].prete AND NOT Defaut. Vide : pas de condition.";
        auth.commit = [commit](std::string_view v) { return commit.prop && commit.prop("auth", std::string(v), false); };
        ui::exprfield::markWhole(auth, ui::exprfield::Expect::Bool);   // 1.10 (chantier K)
        sc.push_back({20.0, std::move(auth)});
    }
    const char* const names[SecCount] = {"Position et taille", "Apparence", "S\xC3\xA9" "curit\xC3\xA9", nullptr,
                                         "Param\xC3\xA8tres du symbole"};
    for (std::size_t s = 0; s < SecCount; ++s) {
        auto& list = rows[s];
        if (list.empty()) continue;
        std::stable_sort(list.begin(), list.end(), [](const Row& a, const Row& b) { return a.rank < b.rank; });
        PG::Category c;
        c.name = names[s] ? std::string(names[s]) : typeName;
        for (auto& r : list) c.properties.push_back(std::move(r.p));
        out.push_back(std::move(c));
    }
    // 1.11.3 : LE CARRE DE LEGENDE de chaque case modifiable qui accepte une formule - d'ou
    // vient la valeur (C, fx, $, A, I, S, V, !). Calcule ici, quand l'inspecteur est refait
    // (une valeur a change), jamais a chaque image. Les parametres d'un symbole ont deja le leur.
    {
        const valuekind::Env env{project, &view, plc};
        for (auto& c : out)
            for (auto& p : c.properties) {
                if (p.legend || !p.commit || p.type == PG::ValueType::ReadOnly || !ui::exprfield::accepts(p)) continue;
                const bool fx = !p.expression.empty();
                const std::string expected = valuekind::expectedOfProperty(p);
                const auto res = valuekind::classify(env, fx ? p.expression : p.value, fx, expected);
                p.legend = valuekind::legendOf(res);
                p.legend->expected = expected;
            }
    }
    return out;
}

void hmiPublishPlcNames(const domain::Project* plc) { hmi::setPlcNames(hmiPlcUpperNames(plc)); }

// 1.10.4 (K3) : les objets dont la variable fait quelque chose que leur Valeur ne
// fait pas : une commande l'ecrit, un champ de saisie aussi, un histogramme la
// mesure, un conteneur a onglets / un panneau repliable / un plan / un resume
// par zone la suivent ou l'ecrivent, un compteur horaire y garde son total.
bool hmiObjectNeedsVariable(hmi::Kind k) noexcept {
    using K = hmi::Kind;
    return hmi::kindWritesVariable(k) || k == K::InputField || k == K::Histogram || k == K::TabContainer || k == K::CollapsiblePanel
        || k == K::ZoneMap || k == K::AlarmSummary || k == K::HourMeter;
}

std::string hmiVariableLabel(hmi::Kind k) {
    if (k == hmi::Kind::InputField || hmi::kindWritesVariable(k)) return "Variable \xC3\xA9" "crite";
    if (const std::string special = labelForKind(k, "variable"); !special.empty()) return special;
    if (k == hmi::Kind::HourMeter) return "Variable du total";
    if (hmi::kindShowsValue(k)) return "Variable montr\xC3\xA9" "e";
    return "Variable li\xC3\xA9" "e";
}

} // namespace app
