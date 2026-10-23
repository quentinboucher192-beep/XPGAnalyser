// =============================================================================
//  app/hmi/HmiLanguageCompare.cpp - 1.12.30 : les langues cote a cote
//  (voir HmiLanguageCompare.hpp)
// =============================================================================
#include "HmiLanguageCompare.hpp"
#include "HmiIcons.hpp"
#include "HmiImages.hpp"          // hmiTextMeasure : la mesure de Generer
#include "HmiPanels.hpp"          // HmiToolStrip

#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../hmi/HmiLanguages.hpp"
#include "../../hmi/HmiQuality.hpp"
#include "../../hmi/HmiTemplates.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/Zoom.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace app {

using hmi::Id;
using hmi::kNoId;

namespace {
constexpr float kBar = 36.f;       // la barre du haut
constexpr float kGap = 10.f;       // entre deux colonnes
constexpr float kHead = 30.f;      // l'en-tete d'une colonne
constexpr float kInset = 8.f;      // la vue, dans sa colonne
constexpr int kMenuSelect = 1, kMenuTranslate = 2, kMenuCopy = 3;
const gfx::Color kRed = gfx::Color::rgb(0xE5484D);
}

HmiLanguageCompare::HmiLanguageCompare(std::string id, hmi::DocumentPtr doc) : ui::Widget(std::move(id)), doc_(std::move(doc)) {
    setFocusPolicy(true);
    setZoomSupport(ui::ZoomSupport::Native);     // l'echelle commune : celle de la puce du bandeau bas
    const std::string base = this->id();
    auto views = std::make_unique<ui::DropDown>(base + ".vue");
    views->setTooltip("La vue montr\xC3\xA9" "e dans chaque langue");
    views_ = &static_cast<ui::DropDown&>(addChild(std::move(views)));
    auto tools = std::make_unique<HmiToolStrip>(base + ".tools");
    tools->add(TPseudo, HmiGlyph::Language,
               "La pseudo-langue : chaque texte allong\xC3\xA9 de 40 %, ses lettres accentu\xC3\xA9" "es, entre crochets - ce qui va "
               "d\xC3\xA9" "border, avant la traduction (jamais enregistr\xC3\xA9" "e comme une langue)", "Pseudo (+40 %)");
    tools->separator();
    tools->add(TTwo, HmiGlyph::LanguagesSide, "Deux langues c\xC3\xB4te \xC3\xA0 c\xC3\xB4te", "Par 2");
    tools->add(TThree, HmiGlyph::LanguagesSide, "Trois langues c\xC3\xB4te \xC3\xA0 c\xC3\xB4te", "Par 3");
    tools->add(TFour, HmiGlyph::LanguagesSide, "Quatre langues c\xC3\xB4te \xC3\xA0 c\xC3\xB4te", "Par 4");
    tools->separator();
    tools->add(TPrev, HmiGlyph::Backward, "Les langues d'avant (Maj + molette)");
    tools->add(TNext, HmiGlyph::Forward, "Les langues suivantes (Maj + molette)");
    tools->add(TFit, HmiGlyph::ZoomFit, "Ajuster : la vue enti\xC3\xA8re dans chaque colonne (Ctrl + molette : zoomer)", "Ajuster");
    tools_ = &static_cast<HmiToolStrip&>(addChild(std::move(tools)));
    tools_->setCheckedWhen(TPseudo, [this] { return pseudo_; });
    tools_->setCheckedWhen(TTwo, [this] { return perRow_ == 2; });
    tools_->setCheckedWhen(TThree, [this] { return perRow_ == 3; });
    tools_->setCheckedWhen(TFour, [this] { return perRow_ == 4; });
    tools_->setEnabledWhen(TPrev, [this] { return first_ > 0; });
    tools_->setEnabledWhen(TNext, [this] { return first_ + static_cast<std::size_t>(perRow_) < columns().size(); });
    tools_->setCheckedWhen(TFit, [this] { return fit_; });
    links_ += tools_->triggered->connect([this](int a) {
        switch (a) {
            case TPseudo: setPseudo(!pseudo_); break;
            case TTwo: setPerRow(2); break;
            case TThree: setPerRow(3); break;
            case TFour: setPerRow(4); break;
            case TPrev: scrollLanguages(-1); break;
            case TNext: scrollLanguages(1); break;
            case TFit: fitAll(); break;
            default: break;
        }
    });
    links_ += views_->selectionChanged->connect([this](int i) {
        if (i >= 0 && static_cast<std::size_t>(i) < viewIds_.size()) setView(viewIds_[static_cast<std::size_t>(i)]);
    });
    menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>(base + ".marque")));
    links_ += menu_->itemChosen->connect([this](int a) { chooseMark(a); });
    if (doc_)
        links_ += doc_->changed->connect([this](Id) {
            dirty_ = true;
            refreshViews();
            invalidate();
        });
    refreshViews();
}

void HmiLanguageCompare::refreshViews() {
    if (!doc_ || !views_) return;
    std::vector<ui::DropDown::Item> items;
    viewIds_.clear();
    int selected = -1;
    for (const auto& v : doc_->project.views) {
        std::string label = v.name;
        if (v.role != "vue") label += " (" + std::string(hmi::viewRoleLabel(v.role)) + ")";
        if (v.id == view_) selected = static_cast<int>(viewIds_.size());
        ui::DropDown::Item it;
        it.label = label;
        it.value = std::to_string(static_cast<unsigned long long>(v.id));
        items.push_back(std::move(it));
        viewIds_.push_back(v.id);
    }
    views_->setItems(std::move(items));
    if (selected < 0 && !viewIds_.empty()) {
        selected = 0;
        view_ = viewIds_.front();
        dirty_ = true;
    }
    views_->setSelectedIndex(selected);
}

void HmiLanguageCompare::setView(Id view) {
    if (view == view_) return;
    view_ = view;
    dirty_ = true;
    scrollX_ = scrollY_ = 0.f;
    refreshViews();
    invalidate();
}

void HmiLanguageCompare::setPseudo(bool on) {
    if (on == pseudo_) return;
    pseudo_ = on;
    dirty_ = true;
    invalidate();
}

void HmiLanguageCompare::setPerRow(int n) {
    perRow_ = std::clamp(n, 2, 4);
    const auto count = columns().size();
    if (first_ + static_cast<std::size_t>(perRow_) > count) first_ = count > static_cast<std::size_t>(perRow_) ? count - static_cast<std::size_t>(perRow_) : 0;
    invalidate();
}

void HmiLanguageCompare::scrollLanguages(int step) {
    const auto count = columns().size();
    const auto last = count > static_cast<std::size_t>(perRow_) ? count - static_cast<std::size_t>(perRow_) : 0;
    if (step < 0) first_ = first_ > 0 ? first_ - 1 : 0;
    else first_ = std::min(last, first_ + 1);
    invalidate();
}

void HmiLanguageCompare::setZoom(float z) {
    fit_ = false;
    scale_ = std::clamp(z, minZoom(), maxZoom());
    invalidate();
}

void HmiLanguageCompare::fitAll() {
    fit_ = true;
    scrollX_ = scrollY_ = 0.f;
    invalidate();
}

// ------------------------------------------------------------- les colonnes ---
const std::vector<HmiLanguageCompare::Column>& HmiLanguageCompare::columns() {
    if (dirty_) rebuild();
    return columns_;
}

void HmiLanguageCompare::rebuild() {
    dirty_ = false;
    columns_.clear();
    if (!doc_) return;
    const auto& p = doc_->project;
    const auto* v = p.view(view_);
    if (!v) return;
    // La vue telle qu'elle se montrera : son ecran modele, son en-tete et son pied compris.
    const hmi::View shown = hmi::templateChain(p, *v).empty() && !hmi::headerOf(p, *v) && !hmi::footerOf(p, *v) ? *v : hmi::compose(p, *v);
    const auto opt = hmi::qualityOptions(p, hmiTextMeasure(p));
    // Les objets qui debordent : ceux de la vue (un objet emprunte au modele se corrige dans le modele).
    const auto keep = [&](std::vector<hmi::Issue> issues) {
        std::vector<hmi::Issue> out;
        std::set<Id> seen;
        for (auto& i : issues)
            if (i.object != kNoId && seen.insert(i.object).second) out.push_back(std::move(i));
        return out;
    };
    const auto& langs = p.languages;
    for (std::size_t i = 0; i < langs.list.size(); ++i) {
        Column c;
        c.code = langs.list[i].code;
        c.name = langs.list[i].name.empty() ? hmi::languageName(c.code) : langs.list[i].name;
        c.source = i == 0;
        c.view = c.source ? shown : hmi::translatedView(shown, langs, c.code);
        c.overflows = keep(hmi::textOverflows(p, c.view, opt));
        columns_.push_back(std::move(c));
    }
    if (pseudo_) {
        const auto withPseudo = hmi::withPseudo(p);
        Column c;
        c.code = std::string(hmi::kPseudoCode);
        c.name = hmi::pseudoName();
        c.pseudo = true;
        c.view = hmi::translatedView(shown, withPseudo, c.code);
        c.overflows = keep(hmi::textOverflows(p, c.view, opt));
        columns_.push_back(std::move(c));
    }
    const auto count = columns_.size();
    if (first_ + static_cast<std::size_t>(perRow_) > count) first_ = count > static_cast<std::size_t>(perRow_) ? count - static_cast<std::size_t>(perRow_) : 0;
}

// ---------------------------------------------------------------- la page ---
gfx::Rect HmiLanguageCompare::boardRect() const noexcept {
    const auto r = bounds();
    return {r.x, r.y + kBar, r.w, std::max(0.f, r.h - kBar)};
}

void HmiLanguageCompare::onLayout() {
    const auto r = bounds();
    const float viewsW = std::min(300.f, std::max(160.f, r.w * 0.22f));
    views_->setBounds({r.x + 8.f, r.y + 5.f, viewsW, kBar - 10.f});
    tools_->setBounds({r.x + 16.f + viewsW, r.y + 2.f, std::max(0.f, r.w - viewsW - 24.f), kBar - 4.f});
    menu_->setBounds(r);
}

void HmiLanguageCompare::onPaint(const ui::PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto& c = th.color;
    const auto r = bounds();
    ctx.r.fillRect(r, c.panelBg);
    ctx.r.fillRect({r.x, r.y, r.w, kBar}, c.headerBg);
    ctx.r.fillRect({r.x, r.y + kBar - 1.f, r.w, 1.f}, c.border);
    marks_.clear();
    const auto& cols = columns();
    const auto board = boardRect();
    const auto* src = doc_ ? doc_->project.view(view_) : nullptr;
    if (!src || cols.empty()) {
        ctx.r.drawText({board.x + 16.f, board.y + 16.f}, "Aucune vue \xC3\xA0 montrer.", th.font.ui, c.textMuted);
        return;
    }
    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(perRow_), cols.size());
    const float colW = std::max(80.f, (board.w - kGap * static_cast<float>(n + 1)) / static_cast<float>(n));
    const float colH = std::max(60.f, board.h - 2.f * kGap);
    const float vw = static_cast<float>(std::max(1, src->width)), vh = static_cast<float>(std::max(1, src->height));
    lastFit_ = std::clamp(std::min((colW - 2.f * kInset) / vw, (colH - kHead - 2.f * kInset) / vh), 0.05f, 4.f);
    const float scale = fit_ ? lastFit_ : scale_;
    // Le defilement (zoome au-dela de la place) : le meme dans toutes les colonnes.
    const float maxX = std::max(0.f, vw * scale - (colW - 2.f * kInset)), maxY = std::max(0.f, vh * scale - (colH - kHead - 2.f * kInset));
    scrollX_ = std::clamp(scrollX_, 0.f, maxX);
    scrollY_ = std::clamp(scrollY_, 0.f, maxY);
    HmiPropertySource statics;
    HmiPaintOptions opt;
    opt.editor = true;
    opt.assets = &doc_->project.assets;
    opt.project = &doc_->project;
    const auto f = th.font;
    for (std::size_t k = 0; k < n && first_ + k < cols.size(); ++k) {
        const std::size_t ci = first_ + k;
        const auto& col = cols[ci];
        const gfx::Rect panel{board.x + kGap + static_cast<float>(k) * (colW + kGap), board.y + kGap, colW, colH};
        ctx.r.fillRoundedRect(panel, c.windowBg, 6.f);
        ctx.r.strokeRect(panel, c.border, 1.f);
        // L'en-tete : la langue, son code ; combien de textes debordent.
        const gfx::Rect head{panel.x, panel.y, panel.w, kHead};
        ctx.r.fillRect({head.x + 1.f, head.bottom() - 1.f, head.w - 2.f, 1.f}, c.border);
        const float lh = ctx.r.lineHeight(f.ui);
        std::string title = col.name + (col.pseudo ? std::string{} : "  (" + col.code + ")");
        if (col.source) title += "  \xC2\xB7 langue du projet";
        const std::size_t over = col.overflows.size();
        const std::string count = over == 0 ? std::string("aucun d\xC3\xA9" "bordement")
                                            : std::to_string(over) + (over > 1 ? " textes d\xC3\xA9" "bordent" : " texte d\xC3\xA9" "borde");
        const float cw = ctx.r.measure(count, f.smallUi).width + 14.f, ch = ctx.r.lineHeight(f.smallUi) + 4.f;
        const gfx::Rect pill{head.right() - cw - 8.f, head.y + (kHead - ch) / 2.f, cw, ch};
        const float titleRoom = std::max(0.f, pill.x - head.x - 18.f);
        const auto keep = ctx.r.fitCharacters(title, f.ui, titleRoom);
        ctx.r.drawText({head.x + 10.f, head.y + (kHead - lh) / 2.f}, std::string_view(title).substr(0, keep), f.ui,
                       col.pseudo ? c.accent : c.text);
        const gfx::Color pc = over == 0 ? th.tone(ui::Tone::Ok, c.ok) : kRed;
        ctx.r.fillRoundedRect(pill, pc, ch / 2.f);
        ctx.r.drawText({pill.x + 7.f, pill.y + 2.f}, count, f.smallUi, gfx::Color::rgb(0xFFFFFF));
        // La vue, a l'echelle commune.
        const gfx::Rect body{panel.x + 1.f, head.bottom(), panel.w - 2.f, panel.h - kHead - 1.f};
        ctx.r.pushClip(body);
        HmiViewport vp{body.x + kInset - scrollX_, body.y + kInset - scrollY_, scale};
        ctx.r.fillRect({vp.originX, vp.originY, vw * scale, vh * scale}, parseColor(col.view.background, gfx::Color::rgb(0x20242B)));
        paintHmiView(ctx.r, col.view, vp, statics, th, opt);
        ctx.r.strokeRect({vp.originX - 1.f, vp.originY - 1.f, vw * scale + 2.f, vh * scale + 2.f}, c.borderStrong, 1.f);
        // Les debordements : un cadre rouge et une pastille "!" (un clic : le menu).
        for (const auto& issue : col.overflows) {
            const auto* o = col.view.object(issue.object);
            if (!o) continue;
            const hmi::Box b = hmi::edit::rotatedBounds(*o);
            const gfx::Point a = vp.toScreen(b.x, b.y), z = vp.toScreen(b.right(), b.bottom());
            const gfx::Rect box{a.x - 3.f, a.y - 3.f, z.x - a.x + 6.f, z.y - a.y + 6.f};
            ctx.r.strokeRect(box, kRed, 2.f);
            const gfx::Rect dot{box.right() - 7.f, box.y - 7.f, 14.f, 14.f};
            ctx.r.fillRoundedRect(dot, kRed, 7.f);
            ctx.r.fillRect({dot.x + 6.f, dot.y + 3.f, 2.f, 5.f}, gfx::Color::rgb(0xFFFFFF));
            ctx.r.fillRect({dot.x + 6.f, dot.y + 9.5f, 2.f, 2.f}, gfx::Color::rgb(0xFFFFFF));
            marks_.push_back({ci, issue.object, issue.message, box.intersect(body)});
        }
        ctx.r.popClip();
    }
    // Plus de langues que de colonnes : ou on en est.
    if (cols.size() > n) {
        const std::string where = "langues " + std::to_string(first_ + 1) + " \xC3\xA0 " + std::to_string(first_ + n) + " sur "
                                + std::to_string(cols.size()) + "  \xC2\xB7  Maj + molette";
        const float ww = ctx.r.measure(where, f.smallUi).width;
        ctx.r.drawText({board.right() - ww - 12.f, board.bottom() - ctx.r.lineHeight(f.smallUi) - 2.f}, where, f.smallUi, c.textMuted);
    }
}

int HmiLanguageCompare::markAt(gfx::Point p) const {
    for (std::size_t i = marks_.size(); i-- > 0;)
        if (marks_[i].rect.contains(p)) return static_cast<int>(i);
    return -1;
}

std::string HmiLanguageCompare::liveTooltip(gfx::Point mouse) const {
    if (const int m = markAt(mouse); m >= 0) {
        const auto& mk = marks_[static_cast<std::size_t>(m)];
        const auto* o = mk.column < columns_.size() ? columns_[mk.column].view.object(mk.object) : nullptr;
        return (o ? o->name + " : " : std::string{}) + mk.message + "\nUn clic : le choisir dans l'\xC3\xA9" "diteur, ou aller \xC3\xA0 sa traduction.";
    }
    return Widget::liveTooltip(mouse);
}

bool HmiLanguageCompare::openMarkMenu(std::size_t index) {
    if (index >= marks_.size() || !menu_ || !doc_) return false;
    menuMark_ = index;
    const auto& mk = marks_[index];
    const auto& col = columns_[mk.column];
    const auto* o = col.view.object(mk.object);
    const std::string name = o ? o->name : std::string("l'objet");
    std::vector<ui::PopupMenu::Item> items;
    ui::PopupMenu::Item head;
    head.heading = true;
    head.label = name;
    head.shortcut = col.name;
    items.push_back(head);
    ui::PopupMenu::Item pick;
    pick.label = "Choisir " + name + " dans l'\xC3\xA9" "diteur";
    pick.id = kMenuSelect;
    pick.enabled = static_cast<bool>(hosts_.selectInEditor);
    pick.disabledReason = "pas d'\xC3\xA9" "diteur ici";
    items.push_back(pick);
    ui::PopupMenu::Item tr;
    tr.label = "Aller \xC3\xA0 sa traduction (IHM \xE2\x80\xBA Langues)";
    tr.id = kMenuTranslate;
    tr.enabled = !col.source && !col.pseudo && static_cast<bool>(hosts_.openTranslation);
    tr.disabledReason = col.source ? "la langue du projet : raccourcis le texte, ou agrandis l'objet"
                      : col.pseudo ? "la pseudo-langue ne se traduit pas : elle montre ce qui risque de d\xC3\xA9" "border"
                                   : "pas ici";
    items.push_back(tr);
    ui::PopupMenu::Item sep;
    sep.separator = true;
    items.push_back(sep);
    ui::PopupMenu::Item copy;
    copy.label = "Copier le message";
    copy.id = kMenuCopy;
    items.push_back(copy);
    menu_->setItems(std::move(items));
    const auto surface = ui::surfaceSize();
    menu_->openAt({mk.rect.x + 4.f, mk.rect.bottom() + 2.f},
                  surface.w > 0.f ? surface : gfx::Size{bounds().right() + 400.f, bounds().bottom() + 400.f});
    return true;
}

void HmiLanguageCompare::chooseMark(int action) {
    if (menuMark_ >= marks_.size() || !doc_) return;
    const auto mk = marks_[menuMark_];
    const auto& col = columns_[mk.column];
    if (action == kMenuSelect) {
        if (hosts_.selectInEditor) hosts_.selectInEditor(view_, mk.object);
        return;
    }
    if (action == kMenuTranslate) {
        // Le texte d'origine de l'objet (dans la vue telle qu'ecrite) : la ligne de IHM > Langues.
        const auto* v = doc_->project.view(view_);
        const auto* o = v ? v->object(mk.object) : nullptr;
        if (hosts_.openTranslation) hosts_.openTranslation(col.code, o ? o->text("text") : std::string{});
        return;
    }
    if (action == kMenuCopy) {
        const auto* o = col.view.object(mk.object);
        ui::setClipboardText(col.name + " - " + (o ? o->name + " : " : std::string{}) + mk.message);
        if (hosts_.status) hosts_.status("Message copi\xC3\xA9");
    }
}

ui::EventResult HmiLanguageCompare::onEvent(const ui::InputEvent& ev) {
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && boardRect().contains(w->pos)) {
        if (w->mods.ctrl && w->dy != 0.f) {
            setZoom(ui::zoomStep(zoom(), w->dy > 0.f ? 1 : -1, minZoom(), maxZoom()));
            return ui::EventResult::Consumed;
        }
        if (w->mods.shift && w->dy != 0.f) {
            scrollLanguages(w->dy > 0.f ? -1 : 1);
            return ui::EventResult::Consumed;
        }
        scrollY_ -= w->dy * 48.f;
        scrollX_ -= w->dx * 48.f;
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && boardRect().contains(d->pos)) {
        grabFocus();
        if (const int m = markAt(d->pos); m >= 0) {
            (void)openMarkMenu(static_cast<std::size_t>(m));
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

} // namespace app
