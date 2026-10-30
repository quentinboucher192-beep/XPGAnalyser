// =============================================================================
//  app/ai/AiPreviewDialog.cpp - voir AiPreviewDialog.hpp
// =============================================================================
#include "AiPreviewDialog.hpp"

#include "AiPanel.hpp"
#include "../hmi/HmiPainter.hpp"
#include "../../hmi/HmiEdit.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/DialogFrame.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace app::ai {

namespace {

const gfx::FontId kTitleFont{20};
const gfx::FontId kText{14};
const gfx::FontId kSmall{13};
constexpr float kMaxW = 1240.f, kMaxH = 800.f;

gfx::Color alpha(gfx::Color c, int a) { return c.withAlpha(static_cast<std::uint8_t>(std::clamp(a, 0, 255))); }

std::string elide(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float width) {
    if (width <= 8.f) return {};
    if (r.measure(text, f).width <= width) return text;
    const std::string dots = "\xE2\x80\xA6";
    const std::size_t n = r.fitCharacters(text, f, std::max(0.f, width - r.measure(dots, f).width));
    return text.substr(0, n) + dots;
}

// Un texte coupe aux mots pour tenir dans `width` (les avertissements et les erreurs se lisent en entier).
std::vector<std::string> wrap(const std::string& text, gfx::FontId f, float width) {
    std::vector<std::string> out;
    std::string line, word;
    const auto push = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > width) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') push();
        else word += c;
    }
    push();
    if (!line.empty()) out.push_back(line);
    if (out.empty()) out.emplace_back();
    return out;
}

// Une ligne de la liste de gauche.
struct Row {
    enum class Kind : std::uint8_t { Heading, Item, Change, Note, Warning, Error } kind{Kind::Note};
    std::string text, right;
    int         what{0};      // un objet : 0 creer, 1 modifier, 2 supprimer
};

} // namespace

// Le corps : le cadre, la liste a gauche, l'apercu a droite, les boutons.
class AiPreviewBody final : public ui::Widget {
public:
    explicit AiPreviewBody(AiPreviewDialog& d) : ui::Widget("ai.apercu.corps"), d_(d) {}
    std::vector<Row> rows;
    float            scroll{0.f};

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(kMaxW, std::max(560.f, r.w - 40.f));
        const float h = std::min(kMaxH, std::max(420.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        const float x = panel_.x + 16.f, right = panel_.right() - 16.f;
        close_ = {panel_.right() - 38.f, panel_.y + 6.f, 28.f, 24.f};
        counts_ = {x, panel_.y + 58.f, right - x, 30.f};
        const float top = counts_.bottom() + 12.f;
        const float bottom = panel_.bottom() - 56.f;
        const float leftW = std::clamp((right - x) * 0.42f, 300.f, 520.f);
        float listTop = top;
        if (d_.variable_ && d_.variable_->visibility() == ui::Visibility::Visible) {
            varLabel_ = {x, top, leftW, 20.f};
            d_.variable_->setBounds({x, top + 22.f, leftW, 28.f});
            listTop = top + 60.f;
        }
        list_ = {x, listTop, leftW, std::max(60.f, bottom - listTop)};
        preview_ = {x + leftW + 16.f, top, right - (x + leftW + 16.f), std::max(80.f, bottom - top - 24.f)};
        legend_ = {preview_.x, preview_.bottom() + 6.f, preview_.w, 18.f};
        float bx = right;
        for (ui::Button* b : {d_.apply_, d_.cancel_}) {
            if (!b) continue;
            const float bw = std::max(110.f, ui::measureWidth(b->text(), gfx::FontId{16}) + 34.f);
            bx -= bw;
            b->setBounds({bx, panel_.bottom() - 44.f, bw, 30.f});
            bx -= 10.f;
        }
        if (d_.edit_) d_.edit_->setBounds({x, panel_.bottom() - 44.f, ui::measureWidth(d_.edit_->text(), gfx::FontId{16}) + 34.f, 30.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRoundedRect(panel_, c.panelBg, 8.f);
        ctx.r.strokeRect(panel_, c.border, 1.f);
        const float x = panel_.x + 16.f;
        const auto& a = d_.panel_.assistant();
        const Proposal* p = a.proposal(d_.message_);
        // Le titre, la croix.
        ui::drawIcon(ctx.r, ui::Icon::Assistant, {x, panel_.y + 12.f, 20.f, 20.f}, c.accent);
        ctx.r.drawText({x + 28.f, panel_.y + 10.f}, "Pr\xC3\xA9visualisation des modifications", kTitleFont, c.text);
        ui::drawIcon(ctx.r, ui::Icon::Close, {close_.x + 6.f, close_.y + 4.f, 16.f, 16.f}, c.textMuted);
        if (!p) return;
        const std::string sub = "Vue \xC2\xAB " + p->view + " \xC2\xBB \xC2\xB7 " + (p->summary.empty() ? std::string("proposition") : p->summary)
                                + " \xC2\xB7 la vue n'est pas modifi\xC3\xA9" "e avant Appliquer";
        ctx.r.drawText({x, panel_.y + 38.f}, elide(ctx.r, sub, kSmall, panel_.w - 32.f), kSmall, c.textMuted);
        // Les comptes : trois tuiles. Perimee (la vue a change, ou n'existe plus) : ceux de la
        // proposition (son texte les annonce), pas ceux d'une validation qui n'a rien pu lire.
        const Check& ck = d_.check_;
        const bool asProposed = ck.stale || ck.viewMissing;
        const struct { const char* label; int n; gfx::Color col; } tiles[] = {
            {"\xC3\x80 cr\xC3\xA9" "er", asProposed ? p->create : ck.create, c.ok},
            {"\xC3\x80 modifier", asProposed ? p->modify : ck.modify, c.info},
            {"\xC3\x80 supprimer", asProposed ? p->remove : ck.remove, c.error}};
        float tx = counts_.x;
        for (const auto& t : tiles) {
            const std::string n = std::to_string(t.n);
            const float tw = ctx.r.measure(n, kTitleFont).width + ctx.r.measure(t.label, kSmall).width + 30.f;
            const gfx::Rect tile{tx, counts_.y, tw, counts_.h};
            ctx.r.fillRoundedRect(tile, alpha(t.n > 0 ? t.col : c.textDisabled, 30), 5.f);
            ctx.r.fillRect({tile.x, tile.y + 4.f, 3.f, tile.h - 8.f}, t.n > 0 ? t.col : c.textDisabled);
            ctx.r.drawText({tile.x + 12.f, tile.y + (tile.h - ctx.r.lineHeight(kTitleFont)) / 2.f}, n, kTitleFont, c.text);
            ctx.r.drawText({tile.x + 18.f + ctx.r.measure(n, kTitleFont).width, tile.y + (tile.h - ctx.r.lineHeight(kSmall)) / 2.f}, t.label, kSmall,
                           c.textMuted);
            tx += tw + 10.f;
        }
        if (ck.stale || ck.viewMissing) {
            ctx.r.drawText({tx + 8.f, counts_.y + (counts_.h - ctx.r.lineHeight(kSmall)) / 2.f},
                           elide(ctx.r, "P\xC3\xA9rim\xC3\xA9" "e : redemande-la (Modifier la demande).", kSmall, counts_.right() - tx - 8.f), kSmall,
                           c.warning);
        }
        if (d_.variable_ && d_.variable_->visibility() == ui::Visibility::Visible) {
            // Le nom introuvable reste dit apres le choix (la validation ne le cite plus : il est remplace).
            const std::string name = "\xC2\xAB " + d_.missingName_ + " \xC2\xBB n'existe pas";
            const bool chosen = !p->variable.use.empty() || p->variable.create;
            const std::string label = !p->variable.use.empty() ? name + " : remplac\xC3\xA9" "e par " + p->variable.use + "."
                                    : p->variable.create    ? name + " : elle sera cr\xC3\xA9\xC3\xA9" "e (" + p->variable.type + ")."
                                                            : name + " : quelle variable utiliser ?";
            ctx.r.drawText({varLabel_.x, varLabel_.y + 2.f}, elide(ctx.r, label, kSmall, varLabel_.w), kSmall, chosen ? c.textMuted : c.error);
        }
        paintList(ctx);
        paintPreview(ctx);
    }

    void paintList(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(list_, c.inputBg);
        ctx.r.strokeRect(list_, c.border, 1.f);
        const float lh = ctx.r.lineHeight(kText) + 6.f;
        const float lhSmall = ctx.r.lineHeight(kSmall) + 3.f;
        // Les hauteurs d'abord (un avertissement, une erreur : sur plusieurs lignes).
        const float w0 = list_.w - 24.f;
        std::vector<std::vector<std::string>> wrapped(rows.size());
        float content = 8.f;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const bool multi = rows[i].kind == Row::Kind::Warning || rows[i].kind == Row::Kind::Error;
            wrapped[i] = multi ? wrap(rows[i].text, kSmall, w0 - 28.f) : std::vector<std::string>{rows[i].text};
            content += multi ? std::max(lh, static_cast<float>(wrapped[i].size()) * lhSmall + 6.f) : lh;
        }
        content_ = content;
        scroll = std::clamp(scroll, 0.f, std::max(0.f, content - list_.h));
        const float space = bar_.space(list_, content, list_.h);
        ctx.r.pushClip(list_);
        float y = list_.y + 4.f - scroll;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const bool multi = row.kind == Row::Kind::Warning || row.kind == Row::Kind::Error;
            const float rh = multi ? std::max(lh, static_cast<float>(wrapped[i].size()) * lhSmall + 6.f) : lh;
            if (y + rh >= list_.y && y <= list_.bottom()) {
                const float ty = y + 3.f;
                const float w = list_.w - space - 12.f;
                switch (row.kind) {
                    case Row::Kind::Heading:
                        ctx.r.fillRect({list_.x + 1.f, y, list_.w - 2.f, lh}, c.rowAltBg);
                        ctx.r.drawText({list_.x + 8.f, ty}, elide(ctx.r, row.text, kSmall, w), kSmall, c.textMuted);
                        break;
                    case Row::Kind::Item: {
                        const gfx::Color col = row.what == 0 ? c.ok : row.what == 1 ? c.info : c.error;
                        const char* tag = row.what == 0 ? "+" : row.what == 1 ? "~" : "\xE2\x88\x92";
                        ctx.r.fillRoundedRect({list_.x + 8.f, y + 4.f, 16.f, lh - 8.f}, alpha(col, 50), 3.f);
                        ctx.r.drawText({list_.x + 12.f, ty}, tag, kText, col);
                        const float rw = ctx.r.measure(row.right, kSmall).width;
                        ctx.r.drawText({list_.x + w - rw, ty + 1.f}, row.right, kSmall, c.textMuted);
                        ctx.r.drawText({list_.x + 30.f, ty}, elide(ctx.r, row.text, kText, w - rw - 34.f), kText, c.text);
                        break;
                    }
                    case Row::Kind::Change:
                        ctx.r.drawText({list_.x + 30.f, ty}, elide(ctx.r, row.text, kSmall, w - 24.f), kSmall, c.textMuted);
                        break;
                    case Row::Kind::Note:
                        ctx.r.drawText({list_.x + 12.f, ty}, elide(ctx.r, row.text, kSmall, w), kSmall, c.text);
                        break;
                    case Row::Kind::Warning:
                    case Row::Kind::Error: {
                        const bool err = row.kind == Row::Kind::Error;
                        ui::drawIcon(ctx.r, err ? ui::Icon::Error : ui::Icon::Warning, {list_.x + 8.f, y + 4.f, 14.f, 14.f}, err ? c.error : c.warning);
                        float ly = ty;
                        for (const auto& l : wrapped[i]) {
                            ctx.r.drawText({list_.x + 28.f, ly}, l, kSmall, c.text);
                            ly += lhSmall;
                        }
                        break;
                    }
                }
            }
            y += rh;
        }
        ctx.r.popClip();
        bar_.paint(ctx, list_, content, list_.h, scroll);
    }

    void paintPreview(const ui::PaintContext& ctx) {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(preview_, c.windowBg);
        ctx.r.strokeRect(preview_, c.border, 1.f);
        const Preview& pv = d_.preview_;
        if (!pv.ok || pv.view.width <= 0 || pv.view.height <= 0) {
            ctx.r.drawText({preview_.x + 12.f, preview_.y + 12.f},
                           elide(ctx.r, pv.why.empty() ? std::string("Pas d'aper\xC3\xA7u.") : "Pas d'aper\xC3\xA7u : " + pv.why, kText, preview_.w - 24.f),
                           kText, c.textMuted);
        } else {
            const auto& v = pv.view;
            const gfx::Rect area{preview_.x + 8.f, preview_.y + 8.f, preview_.w - 16.f, preview_.h - 16.f};
            const float z = std::min(area.w / static_cast<float>(v.width), area.h / static_cast<float>(v.height));
            const HmiViewport vp{area.x + (area.w - static_cast<float>(v.width) * z) / 2.f, area.y + (area.h - static_cast<float>(v.height) * z) / 2.f, z};
            const gfx::Rect page{vp.originX, vp.originY, static_cast<float>(v.width) * z, static_cast<float>(v.height) * z};
            ctx.r.fillRect(page, parseColor(v.background, gfx::Color::rgb(0x20242B)));
            HmiPaintOptions opt;
            const auto doc = d_.panel_.assistant().host().document ? d_.panel_.assistant().host().document() : nullptr;
            if (doc) {
                opt.project = &doc->project;
                opt.assets = &doc->project.assets;
            }
            ctx.r.pushClip(page);
            paintHmiView(ctx.r, v, vp, HmiPropertySource{}, ctx.theme, opt);
            const auto box = [&](hmi::Id id, gfx::Rect& out) {
                for (const auto& o : v.objects)
                    if (o.id == id) {
                        const auto bx = hmi::edit::rotatedBounds(o);
                        const auto p0 = vp.toScreen(bx.x, bx.y);
                        out = {p0.x - 2.f, p0.y - 2.f, static_cast<float>(bx.w) * z + 4.f, static_cast<float>(bx.h) * z + 4.f};
                        return true;
                    }
                return false;
            };
            gfx::Rect r{};
            for (const auto id : pv.touched.created)
                if (box(id, r)) ctx.r.strokeRect(r, c.ok, 2.f);
            for (const auto id : pv.touched.modified)
                if (box(id, r)) ctx.r.strokeRect(r, c.info, 2.f);
            for (const auto id : pv.touched.removed)
                if (box(id, r)) {
                    ctx.r.fillRect(r, alpha(c.error, 40));
                    ctx.r.strokeRect(r, c.error, 2.f);
                    ctx.r.line({r.x, r.y}, {r.right(), r.bottom()}, c.error, 2.f);
                    ctx.r.line({r.right(), r.y}, {r.x, r.bottom()}, c.error, 2.f);
                }
            ctx.r.popClip();
            ctx.r.strokeRect(page, c.border, 1.f);
        }
        // La legende.
        float lx = legend_.x;
        const auto mark = [&](gfx::Color col, const char* text, bool cross) {
            ctx.r.strokeRect({lx, legend_.y + 3.f, 14.f, 11.f}, col, 2.f);
            if (cross) ctx.r.line({lx, legend_.y + 3.f}, {lx + 14.f, legend_.y + 14.f}, col, 1.5f);
            ctx.r.drawText({lx + 20.f, legend_.y}, text, kSmall, c.textMuted);
            lx += 20.f + ctx.r.measure(text, kSmall).width + 18.f;
        };
        mark(c.ok, "nouveau", false);
        mark(c.info, "modifi\xC3\xA9", false);
        mark(c.error, "supprim\xC3\xA9", true);
        ctx.r.drawText({lx, legend_.y}, elide(ctx.r, "Aper\xC3\xA7u : une copie de la vue, dessin\xC3\xA9" "e par les painters du logiciel.", kSmall,
                                              legend_.right() - lx), kSmall, c.textMuted);
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        const float lh = ui::lineHeight(kText) + 6.f;
        const float content = content_;
        if (bar_.handle(*this, ev, list_, content, list_.h, scroll)) return ui::EventResult::Consumed;
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev); w && list_.contains(w->pos)) {
            scroll = std::clamp(scroll - w->dy * 3.f * lh, 0.f, std::max(0.f, content - list_.h));
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && d->button == ui::MouseButton::Left && close_.contains(d->pos)) {
            d_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    AiPreviewDialog&  d_;
    float             content_{0.f};
    gfx::Rect         panel_{}, close_{}, counts_{}, list_{}, preview_{}, legend_{}, varLabel_{};
    ui::EdgeScrollBar bar_;
};

AiPreviewDialog::AiPreviewDialog(AiPanel& panel, int message)
    : menu::WidgetMenu("dialog.ai.apercu"), panel_(panel), message_(message) {}

menu::MenuTraits AiPreviewDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

core::Status AiPreviewDialog::buildUi() {
    const std::string base = id();
    auto body = std::make_unique<AiPreviewBody>(*this);
    auto* b = body.get();
    variable_ = &static_cast<ui::DropDown&>(b->addChild(std::make_unique<ui::DropDown>(base + ".variable")));
    variable_->setVisibility(ui::Visibility::Collapsed);
    links_ += variable_->selectionChanged->connect([this](int i) { if (!syncing_) (void)chooseVariable(i); });
    edit_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Modifier la demande", base + ".modifier")));
    edit_->setTooltip("Remettre la demande dans la saisie pour la reformuler (cette proposition est mise de c\xC3\xB4t\xC3\xA9).");
    links_ += edit_->clicked->connect([this] { editRequest(); });
    cancel_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Annuler", base + ".annuler")));
    cancel_->setTooltip("Refuser la proposition : le projet ne change pas. (\xC3\x89" "chap ferme sans d\xC3\xA9" "cider.)");
    links_ += cancel_->clicked->connect([this] { cancel(); });
    apply_ = &static_cast<ui::Button&>(b->addChild(std::make_unique<ui::Button>("Appliquer", base + ".appliquer")));
    apply_->setStyle(ui::Button::Style::Primary);
    apply_->setTooltip("Modifier le projet, en une seule \xC3\xA9tape d'annulation (Ctrl+Z).");
    links_ += apply_->clicked->connect([this] { (void)apply(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

void AiPreviewDialog::refresh() {
    auto& a = panel_.assistant();
    const Proposal* p = a.proposal(message_);
    if (!p) return;
    check_ = a.check(message_);
    preview_ = a.previewOf(message_);
    auto* body = dynamic_cast<AiPreviewBody*>(widgetRoot());
    // La liste de gauche.
    std::vector<Row> rows;
    const auto heading = [&](std::string t) { rows.push_back({Row::Kind::Heading, std::move(t), {}, 0}); };
    if (!check_.errors.empty()) {
        heading("ERREURS (" + std::to_string(check_.errors.size()) + ") \xE2\x80\x94 rien ne s'applique tant qu'il y en a");
        for (const auto& e : check_.errors) rows.push_back({Row::Kind::Error, e, {}, 0});
    }
    if (!check_.warnings.empty()) {
        heading("AVERTISSEMENTS (" + std::to_string(check_.warnings.size()) + ")");
        for (const auto& w : check_.warnings) rows.push_back({Row::Kind::Warning, w, {}, 0});
    }
    heading("VARIABLES ASSOCI\xC3\x89" "ES");
    if (check_.bindings.empty()) rows.push_back({Row::Kind::Note, "Aucune (l'Assistant n'invente jamais une variable).", {}, 0});
    for (const auto& bnd : check_.bindings) rows.push_back({Row::Kind::Note, bnd, {}, 0});
    if (!check_.missingVariable.empty())
        rows.push_back({Row::Kind::Note, "Introuvable : " + check_.missingVariable + " \xE2\x80\x94 choisis au-dessus.", {}, 0});
    heading("OBJETS CONCERN\xC3\x89S (" + std::to_string(check_.items.size()) + ")");
    for (const auto& it : check_.items) {
        const int what = it.what == Item::What::Create ? 0 : it.what == Item::What::Modify ? 1 : 2;
        rows.push_back({Row::Kind::Item, it.name, std::string(hmi::kindLabel(it.kind)), what});
        std::size_t shown = 0;
        for (const auto& ch : it.changes) {
            if (it.what == Item::What::Create && (ch.key == "x" || ch.key == "y" || ch.key == "w" || ch.key == "h")) continue;
            if (shown++ >= 6) {
                rows.push_back({Row::Kind::Change, "\xE2\x80\xA6", {}, 0});
                break;
            }
            const std::string text = it.what == Item::What::Create ? ch.key + " = " + ch.after
                                                                   : ch.key + " : " + (ch.before.empty() ? std::string("(vide)") : ch.before) + " \xE2\x86\x92 " + ch.after;
            rows.push_back({Row::Kind::Change, text, {}, 0});
        }
    }
    if (body) body->rows = std::move(rows);
    // Le choix de la variable (introuvable) : les plus proches d'abord, puis les autres, puis Creer.
    const bool needChoice = !check_.missingVariable.empty() || !p->variable.use.empty() || p->variable.create;
    if (variable_) {
        syncing_ = true;
        if (needChoice) {
            std::string missing = check_.missingVariable;
            for (const auto& op : p->ops)
                if (op.action == Operation::Action::Bind) missing = op.variable;
            missingName_ = missing;
            variableNames_.clear();
            std::vector<ui::DropDown::Item> items;
            std::set<std::string> seen;
            items.push_back({"Choisir une variable\xE2\x80\xA6", "?"});   // l'invite : rien n'est choisi a la place de l'utilisateur
            variableNames_.push_back("?");
            const auto doc = a.host().document ? a.host().document() : nullptr;
            if (doc) {
                for (const auto& n : nearVariables(doc->project, missing)) {
                    items.push_back({n + "  (proche)", n});
                    variableNames_.push_back(n);
                    seen.insert(n);
                }
                std::vector<std::string> all;
                for (const auto& v : doc->project.programs.variables)
                    if (!seen.count(v.name)) all.push_back(v.name);
                std::sort(all.begin(), all.end());
                for (const auto& n : all) {
                    items.push_back({n, n});
                    variableNames_.push_back(n);
                }
            }
            items.push_back({"Cr\xC3\xA9" "er \xC2\xAB " + missing + " \xC2\xBB (BOOL) dans les variables de l'IHM", "+"});
            variableNames_.push_back({});
            variable_->setItems(std::move(items));
            int sel = 0;
            if (p->variable.create) sel = static_cast<int>(variableNames_.size()) - 1;
            for (std::size_t i = 1; i + 1 < variableNames_.size(); ++i)
                if (!p->variable.use.empty() && variableNames_[i] == p->variable.use) sel = static_cast<int>(i);
            variable_->setSelectedIndex(sel);
            variable_->setVisibility(ui::Visibility::Visible);
        } else {
            variable_->setVisibility(ui::Visibility::Collapsed);
        }
        syncing_ = false;
    }
    if (apply_) apply_->setEnabled(p->state == "proposee" && check_.ok());
    if (cancel_) cancel_->setEnabled(p->state == "proposee");
    key_ = p->id + "|" + p->state + "|" + p->variable.use + (p->variable.create ? "+" : "-");
    if (widgetRoot()) widgetRoot()->invalidateLayout();
}

std::vector<std::string> AiPreviewDialog::variableItems() const {
    std::vector<std::string> out;
    if (variable_)
        for (const auto& it : variable_->items()) out.push_back(it.label);
    return out;
}

bool AiPreviewDialog::chooseVariable(int index) {
    if (index <= 0 || index >= static_cast<int>(variableNames_.size())) return false;   // 0 : l'invite
    VariableChoice choice;
    if (variableNames_[static_cast<std::size_t>(index)].empty()) choice.create = true;
    else choice.use = variableNames_[static_cast<std::size_t>(index)];
    const bool ok = panel_.assistant().chooseVariable(message_, choice);
    panel_.sync();
    refresh();
    return ok;
}

void AiPreviewDialog::Update(const menu::FrameContext& f) {
    menu::WidgetMenu::Update(f);
    const Proposal* p = panel_.assistant().proposal(message_);
    // Appliquee (apres la confirmation d'une suppression), refusee, ou partie : la fenetre se ferme.
    if (!p || p->state != "proposee") {
        finish(p && p->state == "appliquee");
        return;
    }
    const std::string key = p->id + "|" + p->state + "|" + p->variable.use + (p->variable.create ? "+" : "-");
    if (key != key_) refresh();
}

bool AiPreviewDialog::apply() {
    if (done_) return false;
    const bool ok = panel_.applyProposal(message_);   // une suppression multiple : la confirmation d'abord
    if (ok) finish(true);
    else refresh();
    return ok;
}

void AiPreviewDialog::cancel() {
    (void)panel_.refuseProposal(message_);
    finish(false);
}

void AiPreviewDialog::editRequest() {
    panel_.editRequest(message_);
    finish(false);
}

void AiPreviewDialog::close() { finish(false); }

void AiPreviewDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
}

ui::EventResult AiPreviewDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && !(variable_ && variable_->isOpen())) {
        close();
        return ui::EventResult::Consumed;
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

} // namespace app::ai
