// app/hmi/HmiAddressDialog.cpp - « Modifier l'adresse dans toutes les cases... » (1.12.18).
#include "HmiAddressDialog.hpp"

#include "../../hmi/HmiTypes.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/ExprField.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace app {

namespace addr = hmi::addr;

namespace {

const gfx::FontId kSmall{13};
constexpr char    kSeparator = '\x1f';
constexpr float   kRowH = 24.f;

// Les prefixes proposes ; le dernier : un nombre seul (40001, 400001).
const std::vector<std::string> kPrefixes = {"%MW", "%MF", "%MD", "%M", "%IW", "%I", "4x", "3x", "0x", "1x", "HR", "IR", "CO", "DI", ""};
std::string prefixLabel(const std::string& p) { return p.empty() ? std::string("(un nombre : 40001)") : p; }

} // namespace

// --------------------------------------------------------------- Preview ---
// L'apercu : une ligne par case - la case, l'adresse avec la formule, l'adresse d'avant, qui l'a aussi.
class HmiAddressDialog::Preview final : public ui::Widget {
public:
    explicit Preview(HmiAddressDialog& d) : d_(d) {}
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {600.f, kRowH * static_cast<float>(d_.plan_.rows.size() + 1) + 4.f};
        h.minimum = {200.f, kRowH};
        h.stretchX = 1.f;
        return h;
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& col = ctx.theme.color;
        const float x0 = b.x + 8.f, x1 = b.x + b.w * 0.18f, x2 = b.x + b.w * 0.40f, x3 = b.x + b.w * 0.62f;
        ctx.r.drawText({x0, b.y + 4.f}, "Case", ctx.theme.font.uiBold, col.textMuted);
        ctx.r.drawText({x1, b.y + 4.f}, "Adresse", ctx.theme.font.uiBold, col.textMuted);
        ctx.r.drawText({x2, b.y + 4.f}, "Avant", ctx.theme.font.uiBold, col.textMuted);
        ctx.r.fillRect({b.x, b.y + kRowH - 1.f, b.w, 1.f}, col.borderStrong);
        // Seules les lignes a l'ecran (un tableau de 1000 cases ne dessine pas 1000 lignes).
        const auto clip = d_.previewPanel_ ? d_.previewPanel_->bounds() : b;
        const auto& rows = d_.plan_.rows;
        const std::size_t first = static_cast<std::size_t>(std::max(0.f, std::floor((clip.y - b.y - kRowH) / kRowH)));
        const std::size_t last = std::min(rows.size(), first + static_cast<std::size_t>(clip.h / kRowH) + 2);
        for (std::size_t i = first; i < last; ++i) {
            const auto& r = rows[i];
            const float y = b.y + kRowH * static_cast<float>(i + 1);
            if (i % 2 == 1) ctx.r.fillRect({b.x, y, b.w, kRowH}, gfx::Color{128, 128, 128, 18});
            ctx.r.drawText({x0, y + 4.f}, r.cell, ctx.theme.font.ui, col.text);
            ctx.r.drawText({x1, y + 4.f}, r.after.empty() ? std::string("\xE2\x80\x94") : r.after, ctx.theme.font.uiBold,
                           r.after.empty() ? ctx.theme.color.error : (r.after == r.before ? col.text : ctx.theme.color.ok));
            ctx.r.drawText({x2, y + 4.f}, r.before, ctx.theme.font.ui, col.textMuted);
            ctx.r.pushClip({x3, y, b.right() - x3 - 4.f, kRowH});
            if (!r.error.empty()) ctx.r.drawText({x3, y + 5.f}, r.error, kSmall, ctx.theme.color.error);
            else if (!r.sharedWith.empty()) {
                std::string who = r.sharedWith.front();
                if (r.sharedWith.size() > 1) who += " (+" + std::to_string(r.sharedWith.size() - 1) + ")";
                ctx.r.drawText({x3, y + 5.f}, who + " l'a aussi", kSmall, ctx.theme.color.warning);
            } else {
                ctx.r.drawText({x3, y + 5.f}, "libre", kSmall, col.textMuted);
            }
            ctx.r.popClip();
        }
    }
private:
    HmiAddressDialog& d_;
};

// ------------------------------------------------------------------ Body ---
class HmiAddressDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiAddressDialog& d) : d_(d) {}
    std::vector<ui::Button*> buttons;
    gfx::Rect panel{};
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(860.f, r.w - 40.f), h = std::min(640.f, r.h - 40.f);
        panel = {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h};
        const float left = panel.x + 16.f, inner = panel.w - 32.f;
        float y = panel.y + 44.f + 26.f;                     // le titre, la variable
        whatY_ = y;
        y += 22.f;
        if (d_.memberRadio_) {
            d_.memberRadio_->setVisibility(d_.canChooseMember() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            if (d_.canChooseMember()) {
                d_.memberRadio_->setBounds({left, y, inner, 26.f});
                y += 28.f;
            }
        }
        if (d_.wholeRadio_) {
            d_.wholeRadio_->setBounds({left, y, inner, 26.f});
            y += 34.f;
        }
        howY_ = y;
        y += 22.f;
        if (d_.formulaRadio_) d_.formulaRadio_->setBounds({left, y, 220.f, 26.f});
        if (d_.followRadio_) d_.followRadio_->setBounds({left + 230.f, y, 260.f, 26.f});
        y += 34.f;
        fieldY_ = y;
        const bool follow = d_.following_;
        const bool showBit = !follow && d_.shape_.boolInWord;
        const auto show = [](ui::Widget* wd, bool on) {
            if (wd) wd->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        };
        show(d_.prefixBox_, !follow);
        show(d_.expressionField_, !follow);
        show(d_.bitField_, showBit);
        show(d_.startField_, follow);
        if (d_.prefixBox_) d_.prefixBox_->setBounds({left, y, 150.f, 28.f});
        if (d_.expressionField_) d_.expressionField_->setBounds({left + 160.f, y, showBit ? inner - 160.f - 120.f : inner - 160.f, 28.f});
        if (d_.bitField_) d_.bitField_->setBounds({left + inner - 100.f, y, 100.f, 28.f});
        if (d_.startField_) d_.startField_->setBounds({left + 100.f, y, 200.f, 28.f});
        y += 34.f;
        helpY_ = y;
        y += 26.f;
        previewY_ = y;
        y += 22.f;
        const float bottom = panel.y + panel.h - 56.f - 40.f;   // le message au-dessus des boutons
        if (d_.previewPanel_) d_.previewPanel_->setBounds({panel.x + 8.f, y, panel.w - 16.f, std::max(60.f, bottom - y)});
        messageY_ = bottom + 6.f;
        float bx = panel.x + panel.w - 16.f;
        for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
            const float bw = std::max(110.f, ui::measureWidth((*it)->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            (*it)->setBounds({bx, panel.y + panel.h - 44.f, bw, 30.f});
            bx -= 10.f;
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        ctx.r.fillRect(panel, c.panelBg);
        ctx.r.strokeRect(panel, c.borderStrong, 1.f);
        const float th = ctx.theme.metric.headerHeight;
        ctx.r.fillRect({panel.x, panel.y, panel.w, th}, c.headerBg);
        ctx.r.drawText({panel.x + 12.f, panel.y + (th - ctx.r.lineHeight(ctx.theme.font.uiBold)) * 0.5f}, d_.title(), ctx.theme.font.uiBold, c.text);
        const float left = panel.x + 16.f;
        ctx.r.pushClip({panel.x + 8.f, panel.y + th, panel.w - 16.f, panel.h - th});
        ctx.r.drawText({left, panel.y + 44.f}, d_.var_.name + " : " + d_.var_.type + "  \xC2\xB7  " + d_.var_.equipment, ctx.theme.font.uiBold, c.text);
        ctx.r.drawText({left, whatY_}, "Ce qui change", ctx.theme.font.uiBold, c.textMuted);
        ctx.r.drawText({left, howY_}, "Comment", ctx.theme.font.uiBold, c.textMuted);
        if (d_.following_) ctx.r.drawText({left, fieldY_ + 6.f}, "\xC3\x80 partir de", ctx.theme.font.ui, c.text);
        else if (d_.shape_.boolInWord && d_.bitField_) {
            const auto br = d_.bitField_->bounds();
            ctx.r.drawText({br.x - 16.f, fieldY_ + 4.f}, ".", ctx.theme.font.uiBold, c.text);
        }
        // L'aide sous les champs.
        std::string help;
        if (d_.following_) {
            help = d_.followWhy_.empty() ? "La formule : " + d_.formula_ : d_.followWhy_;
            if (d_.followWhy_.empty())
                help += d_.shape_.leaf ? (d_.shape_.boolInWord || d_.shape_.bits ? "  \xC2\xB7  chaque case prend le bit qui suit"
                                                                                  : "  \xC2\xB7  chaque case prend les mots qui suivent")
                                       : "  \xC2\xB7  chaque case enti\xC3\xA8re suit la pr\xC3\xA9" "c\xC3\xA9" "dente (ligne par ligne)";
        } else {
            help = d_.shape_.indices + "  \xC2\xB7  + \xE2\x88\x92 * / MOD ( )";
            if (d_.shape_.dims < 2) help += "  \xC2\xB7  deux dimensions : i et j";
            help += "  \xC2\xB7  la formule : " + d_.formula_;
        }
        ctx.r.drawText({left, helpY_}, help, kSmall, d_.following_ && !d_.followWhy_.empty() ? ctx.theme.color.error : c.textMuted);
        ctx.r.drawText({left, previewY_}, "Aper\xC3\xA7u  \xC2\xB7  " + std::to_string(d_.plan_.rows.size()) + (d_.plan_.rows.size() > 1 ? " cases" : " case"),
                       ctx.theme.font.uiBold, c.textMuted);
        const std::string msg = d_.message();
        if (!msg.empty()) ctx.r.drawText({left, messageY_}, msg, kSmall, d_.plan_.ok() ? ctx.theme.color.warning : ctx.theme.color.error);
        ctx.r.drawText({left, panel.y + panel.h - 36.f}, "Ctrl+Z d\xC3\xA9" "fait les " + std::to_string(d_.plan_.rows.size()) + " cases d'un coup",
                       kSmall, c.textMuted);
        ctx.r.popClip();
    }
private:
    HmiAddressDialog& d_;
    float whatY_{0}, howY_{0}, fieldY_{0}, helpY_{0}, previewY_{0}, messageY_{0};
};

// ======================================================================= ======
HmiAddressDialog::HmiAddressDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    if (spec_.doc)
        if (const auto* v = spec_.doc->project.variableById(spec_.variable)) var_ = *v;
    memberPattern_ = addr::patternOf(spec_.member, false);
    casePattern_ = addr::patternOf(spec_.member, true);
    if (casePattern_.empty()) casePattern_ = "[*]";   // la ligne du tableau : ses cases
    if (memberPattern_.empty()) memberPattern_ = casePattern_;
    whole_ = !canChooseMember();
    if (spec_.doc) {
        memberShape_ = addr::shape(spec_.doc->project, var_, memberPattern_);
        caseShape_ = addr::shape(spec_.doc->project, var_, casePattern_);
    }
    prefixes_ = kPrefixes;
    prefill();
}

menu::MenuTraits HmiAddressDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

void HmiAddressDialog::prefill() {
    shape_ = whole_ ? caseShape_ : memberShape_;
    std::string f;
    // La formule deja posee sur ce motif, sinon celle de la place d'aujourd'hui.
    for (const auto& pl : var_.places)
        if (pl.path == pattern() && hmi::types::isFormula(pl.address)) f = pl.address;
    if (f.empty() && spec_.doc) f = addr::current(spec_.doc->project, var_, pattern());
    std::string p, e, b;
    if (addr::split(f, p, e, b)) {
        prefix_ = p;
        expression_ = e;
        bit_ = b;
    } else {
        expression_ = f;
    }
    if (std::find(prefixes_.begin(), prefixes_.end(), prefix_) == prefixes_.end()) prefixes_.insert(prefixes_.begin(), prefix_);
    start_.clear();
    if (spec_.doc) {
        const auto today = addr::preview(spec_.doc->project, var_, pattern(), f.empty() ? std::string("()") : f);
        if (!today.rows.empty()) start_ = today.rows.front().before;
    }
    recompute();
}

void HmiAddressDialog::recompute() {
    followWhy_.clear();
    if (following_) formula_ = spec_.doc ? addr::following(spec_.doc->project, var_, pattern(), start_, &followWhy_) : std::string{};
    else formula_ = addr::join(prefix_, ui::exprfield::withoutEquals(expression_), shape_.boolInWord ? bit_ : std::string{});   // 1.12.19 : "=1000 + i" accepte
    plan_ = spec_.doc ? addr::preview(spec_.doc->project, var_, pattern(), formula_) : addr::Plan{};
    if (following_ && !followWhy_.empty()) plan_.errors.insert(plan_.errors.begin(), followWhy_);
    refresh();
}

void HmiAddressDialog::refresh() {
    if (ok_) {
        const std::string label = confirmLabel();
        if (label != ok_->text()) {
            ok_->setText(label);
            if (body_) body_->invalidateLayout();
        }
        ok_->setEnabled(canConfirm());
    }
    syncing_ = true;
    if (expressionField_ && expressionField_->text() != expression_) expressionField_->setText(expression_);
    if (bitField_ && bitField_->text() != bit_) bitField_->setText(bit_);
    if (startField_ && startField_->text() != start_) startField_->setText(start_);
    if (prefixBox_) {
        std::vector<ui::DropDown::Item> items;
        for (const auto& p : prefixes_) items.push_back({prefixLabel(p), p});
        if (prefixBox_->items().size() != items.size()) prefixBox_->setItems(items);
        const auto at = std::find(prefixes_.begin(), prefixes_.end(), prefix_);
        if (at != prefixes_.end() && prefixBox_->selectedIndex() != static_cast<int>(at - prefixes_.begin()))
            prefixBox_->setSelectedIndex(static_cast<int>(at - prefixes_.begin()));
    }
    syncing_ = false;
    if (previewPanel_) previewPanel_->invalidateLayout();
    if (preview_) preview_->invalidate();
    if (body_) {
        body_->invalidateLayout();
        body_->invalidate();
    }
}

void HmiAddressDialog::setWhole(bool wholeCases) {
    if (!canChooseMember()) wholeCases = true;
    if (wholeCases == whole_) return;
    whole_ = wholeCases;
    if (whatGroup_) {
        syncing_ = true;
        whatGroup_->setValue(whole_ ? 1 : 0);
        syncing_ = false;
    }
    prefill();
}

void HmiAddressDialog::setFollowing(bool following) {
    if (following == following_) return;
    following_ = following;
    if (howGroup_) {
        syncing_ = true;
        howGroup_->setValue(following_ ? 1 : 0);
        syncing_ = false;
    }
    recompute();
}

void HmiAddressDialog::setPrefix(std::string prefix) {
    prefix_ = std::move(prefix);
    if (std::find(prefixes_.begin(), prefixes_.end(), prefix_) == prefixes_.end()) prefixes_.insert(prefixes_.begin(), prefix_);
    recompute();
}

void HmiAddressDialog::setExpression(std::string expression) {
    expression_ = std::move(expression);
    recompute();
}

void HmiAddressDialog::setBit(std::string bit) {
    bit_ = std::move(bit);
    recompute();
}

void HmiAddressDialog::setStart(std::string start) {
    start_ = std::move(start);
    recompute();
}

std::string HmiAddressDialog::confirmLabel() const {
    const std::size_t n = plan_.rows.size();
    return n == 1 ? std::string("Appliquer \xC3\xA0 la case") : "Appliquer aux " + std::to_string(n) + " cases";
}

std::string HmiAddressDialog::message() const {
    if (!plan_.errors.empty()) return plan_.errors.front();
    if (plan_.shared == 0) return {};
    // 1.12.19 : un chevauchement ne bloque pas - une autre variable garde la sienne, deux cases de la
    // meme variable partagent la place.
    std::string first;
    for (const auto& r : plan_.rows)
        if (!r.sharedWith.empty()) {
            const std::string& who = r.sharedWith.front();
            const bool sameVariable = who.rfind(var_.name, 0) == 0 && who.size() > var_.name.size()
                                   && (who[var_.name.size()] == '[' || who[var_.name.size()] == '.');
            first = sameVariable ? var_.name + r.target + " et " + who + " partagent " + r.after
                                 : var_.name + r.target + " prend " + r.after + " quand m\xC3\xAAme ; " + who + " garde la sienne";
            break;
        }
    return (plan_.shared == 1 ? std::string("1 adresse d\xC3\xA9j\xC3\xA0 prise. ") : std::to_string(plan_.shared) + " adresses d\xC3\xA9j\xC3\xA0 prises. ")
           + first + " : les chevauchements ne bloquent pas, rien n'est effac\xC3\xA9.";
}

bool HmiAddressDialog::confirm() {
    if (!canConfirm()) return false;
    finish(true);
    return true;
}

void HmiAddressDialog::cancel() { finish(false); }

std::string HmiAddressDialog::encode(const std::string& pattern, const std::string& formula) { return pattern + kSeparator + formula; }

bool HmiAddressDialog::parse(const std::string& payload, std::string& pattern, std::string& formula) {
    const auto at = payload.find(kSeparator);
    if (at == std::string::npos) return false;
    pattern = payload.substr(0, at);
    formula = payload.substr(at + 1);
    return !pattern.empty() && !formula.empty();
}

void HmiAddressDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? encode(pattern(), formula_) : std::string{}});
}

core::Status HmiAddressDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    whatGroup_ = std::make_shared<ui::RadioGroup>();
    {
        // "Le membre Retour de chaque case   EXTRACTEURS[i].Retour · INT, 1 mot"
        std::string leafName = memberPattern_;
        if (const auto dot = leafName.rfind('.'); dot != std::string::npos) leafName = leafName.substr(dot + 1);
        memberRadio_ = &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>(
            "Le membre " + leafName + " de chaque case      " + memberShape_.path + "  \xC2\xB7  " + memberShape_.text, whatGroup_, 0, base + ".member")));
        wholeRadio_ = &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>(
            "Les cases enti\xC3\xA8res      " + caseShape_.path + "  \xC2\xB7  " + caseShape_.text, whatGroup_, 1, base + ".whole")));
        whatGroup_->setValue(whole_ ? 1 : 0);
        links_ += whatGroup_->valueChanged->connect([this](int v) {
            if (!syncing_) setWhole(v == 1);
        });
    }
    howGroup_ = std::make_shared<ui::RadioGroup>();
    {
        formulaRadio_ = &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>("Par une formule de i", howGroup_, 0, base + ".formula")));
        followRadio_ = &static_cast<ui::RadioButton&>(body->addChild(std::make_unique<ui::RadioButton>("\xC3\x80 la suite d'une adresse", howGroup_, 1, base + ".follow")));
        howGroup_->setValue(following_ ? 1 : 0);
        links_ += howGroup_->valueChanged->connect([this](int v) {
            if (!syncing_) setFollowing(v == 1);
        });
    }
    {
        auto box = std::make_unique<ui::DropDown>(base + ".prefix");
        prefixBox_ = &static_cast<ui::DropDown&>(body->addChild(std::move(box)));
        links_ += prefixBox_->selectionChanged->connect([this](int i) {
            if (syncing_ || i < 0 || i >= static_cast<int>(prefixes_.size())) return;
            setPrefix(prefixes_[static_cast<std::size_t>(i)]);
        });
        auto e = std::make_unique<ui::InputText>(base + ".expression");
        e->setPlaceholder("1000 + 10 * i");
        expressionField_ = &static_cast<ui::InputText&>(body->addChild(std::move(e)));
        links_ += expressionField_->textChanged->connect([this](const std::string& t) {
            if (!syncing_) setExpression(t);
        });
        auto bt = std::make_unique<ui::InputText>(base + ".bit");
        bt->setPlaceholder("0");
        bitField_ = &static_cast<ui::InputText&>(body->addChild(std::move(bt)));
        links_ += bitField_->textChanged->connect([this](const std::string& t) {
            if (!syncing_) setBit(t);
        });
        auto st = std::make_unique<ui::InputText>(base + ".start");
        st->setPlaceholder("%MW1000");
        startField_ = &static_cast<ui::InputText&>(body->addChild(std::move(st)));
        links_ += startField_->textChanged->connect([this](const std::string& t) {
            if (!syncing_) setStart(t);
        });
    }
    {
        auto panel = std::make_unique<ui::ScrollablePanel>(base + ".preview");
        panel->setScrollPolicy(false, true);
        previewPanel_ = &static_cast<ui::ScrollablePanel&>(body->addChild(std::move(panel)));
        auto pv = std::make_unique<Preview>(*this);
        preview_ = pv.get();
        previewPanel_->setContent(std::move(pv));
    }
    auto* cancelButton = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    body->buttons.push_back(cancelButton);
    links_ += cancelButton->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(confirmLabel(), base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    body->buttons.push_back(ok_);
    links_ += ok_->clicked->connect([this] { confirm(); });
    setRoot(std::move(body));
    refresh();
    return core::ok();
}

ui::EventResult HmiAddressDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Ctrl+Entree : Appliquer.
        if (k->key == ui::Key::Return && k->mods.ctrl) {
            confirm();
            return ui::EventResult::Consumed;
        }
    }
    const auto r = menu::WidgetMenu::HandleEvent(ev);
    if (r == ui::EventResult::Consumed) return r;
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
        finish(false);
        return ui::EventResult::Consumed;
    }
    return r;
}

} // namespace app
