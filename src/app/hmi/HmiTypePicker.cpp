// =============================================================================
//  app/hmi/HmiTypePicker.cpp - voir HmiTypePicker.hpp
// =============================================================================
#include "HmiTypePicker.hpp"

#include "../../menu/MenuManager.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../ui/Theme.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app {

namespace tr = hmi::typereg;

namespace {

constexpr gfx::FontId kBody{15};
constexpr gfx::FontId kSmall{13};
constexpr gfx::FontId kMono{14};
constexpr gfx::FontId kTitle{17};
constexpr float       kRow = 26.f;
constexpr char        kSep = '\x1F';
constexpr int         kAll = -2, kRecents = -1;
// Les libelles de la ligne Construire (kSmall) : leur place, fixe comme la mise en page.
constexpr float       kArrayLabel = 104.f, kOfLabel = 30.f, kRefLabel = 150.f, kMapLabel = 112.f;

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (r.measure(s, f).width <= w) return s;
    const std::string dots = "\xE2\x80\xA6";
    const auto n = r.fitCharacters(s, f, std::max(0.f, w - r.measure(dots, f).width));
    return s.substr(0, n) + dots;
}

std::string useLabel(unsigned use) {
    if (use & tr::UseDeclaration) return "une d\xC3\xA9" "claration";
    if (use & tr::UseVariable) return "une variable IHM";
    if (use & tr::UseParameter) return "un param\xC3\xA8tre";
    if (use & tr::UseReturn) return "un retour de fonction";
    if (use & tr::UseOperand) return "un op\xC3\xA9rande";
    return "un type";
}

bool canArray(unsigned use) { return (use & (tr::UseVariable | tr::UseDeclaration | tr::UseParameter | tr::UseReturn)) != 0; }
bool canReference(unsigned use) { return (use & (tr::UseDeclaration | tr::UseReturn)) != 0; }

void paintCheck(const ui::PaintContext& ctx, const gfx::Rect& box, bool on, bool enabled) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    if (on) {
        r.fillRoundedRect(box, enabled ? c.accent : c.textDisabled, 3.f);
        r.line({box.x + 3.5f, box.y + 8.5f}, {box.x + 6.5f, box.y + 11.5f}, c.textInverted, 2.f);
        r.line({box.x + 6.5f, box.y + 11.5f}, {box.x + 12.5f, box.y + 4.5f}, c.textInverted, 2.f);
    } else {
        r.strokeRect(box, enabled ? c.borderStrong : c.border, 1.f);
    }
}

} // namespace

// ============================================================== le corps ====
class HmiTypePicker::Body final : public ui::Widget {
public:
    Body(HmiTypePicker& owner, const Spec& spec) : ui::Widget("dialog.typePicker"), owner_(owner), spec_(spec) {
        search_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.typePicker.chercher")));
        search_->setPlaceholder("Rechercher un type (nom, cat\xC3\xA9gorie, provenance)\xE2\x80\xA6");
        links_ += search_->textChanged->connect([this](const std::string&) {
            sel_ = 0;
            scroll_ = 0.f;
            filter();
        });
        bounds_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("dialog.typePicker.bornes")));
        bounds_->setText("0..9");
        bounds_->setTooltip("0..9 : 10 cases ; 1..4 ; -5..5 ; 0..3, 0..9 : deux dimensions");
        links_ += bounds_->textChanged->connect([this](const std::string&) {
            array_ = true;                                // ecrire des bornes coche le tableau
            invalidate();
        });
        cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler", "dialog.typePicker.annuler")));
        open_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ouvrir la d\xC3\xA9" "finition", "dialog.typePicker.definition")));
        ok_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Choisir", "dialog.typePicker.choisir")));
        ok_->setStyle(ui::Button::Style::Primary);
        open_->setTooltip("Le type IHM dans l'onglet Types IHM, le DDT dans les types d\xC3\xA9riv\xC3\xA9s de l'API");
        links_ += cancel_->clicked->connect([this] { owner_.finish(false); });
        links_ += open_->clicked->connect([this] { owner_.openDefinition(); });
        links_ += ok_->clicked->connect([this] { owner_.choose(); });
        // Le projet relu a chaque changement : un type cree ailleurs apparait aussitot.
        if (spec_.doc) links_ += spec_.doc->changed->connect([this](hmi::Id) { reload(true); });
        reload(false);
        preselect();
    }

    // ---- l'etat ----
    [[nodiscard]] std::vector<std::string> shownNames() const {
        std::vector<std::string> out;
        for (const auto* e : shown_) out.push_back(e->name);
        return out;
    }
    [[nodiscard]] const tr::Entry* selected() const {
        return sel_ >= 0 && static_cast<std::size_t>(sel_) < shown_.size() ? shown_[static_cast<std::size_t>(sel_)] : nullptr;
    }
    // Le type qu'on choisirait : l'entree, puis REF_TO, ARRAY, MAP (comme la maquette).
    [[nodiscard]] std::string result() const {
        const auto* e = selected();
        if (!e) return {};
        std::string n = e->name;
        if (!constructible(e)) return n;
        if (ref_ && canReference(spec_.use)) n = "REF_TO " + n;
        if (array_ && canArray(spec_.use)) n = "ARRAY[" + trimmed(bounds_->text()) + "] OF " + n;
        if (map_ && canReference(spec_.use)) n = "MAP[STRING] OF " + n;
        return n;
    }
    [[nodiscard]] std::string problem() const {
        const auto* e = selected();
        if (!e) return "choisissez un type dans la liste";
        const auto r = reg_->resolve(result(), spec_.use);
        return r.ok ? std::string{} : r.why;
    }
    [[nodiscard]] std::string notice() const { return notice_; }
    [[nodiscard]] std::string elementName() const {
        const auto* e = selected();
        return e ? e->name : std::string{};
    }
    [[nodiscard]] std::string openKey() const {
        const auto* e = selected();
        return e && (e->category == tr::Category::Structure || e->category == tr::Category::Enumeration || e->category == tr::Category::PlcType)
                   ? e->key
                   : std::string{};
    }
    [[nodiscard]] bool searchFocused() const { return search_->focused(); }
    // La ligne choisie est-elle dans la partie visible de la liste (une fois placee) ?
    [[nodiscard]] bool selectionVisible() const {
        if (sel_ < 0 || rowsArea_.h <= 0.f) return false;
        const float top = static_cast<float>(sel_) * kRow;
        return top >= scroll_ && top + kRow <= scroll_ + rowsArea_.h + 0.5f;
    }

    void focusFirst() { owner_.focus().focus(search_); }
    void setSearch(const std::string& t) { search_->setText(t); filter(); }
    bool setCategory(std::string_view label) {
        if (label == "Tous") chip_ = kAll;
        else if (label == "R\xC3\xA9" "cents") chip_ = kRecents;
        else {
            bool found = false;
            for (const auto c : tr::kCategories)
                if (tr::categoryLabel(c) == label) {
                    chip_ = static_cast<int>(c);
                    found = true;
                }
            if (!found) return false;
        }
        sel_ = 0;
        scroll_ = 0.f;
        filter();
        return true;
    }
    bool select(std::string_view name) {
        for (std::size_t i = 0; i < shown_.size(); ++i)
            if (shown_[i]->name == name) {
                sel_ = static_cast<int>(i);
                reveal();
                invalidate();
                return true;
            }
        return false;
    }
    void move(int delta) {
        if (shown_.empty()) return;
        sel_ = std::clamp(sel_ + delta, 0, static_cast<int>(shown_.size()) - 1);
        reveal();
        invalidate();
    }
    void setArray(bool on, const std::string& bounds) {
        if (!bounds.empty()) bounds_->setText(bounds);
        array_ = on;
        invalidate();
    }
    void setReference(bool on) { ref_ = on; invalidate(); }
    void setMap(bool on) { map_ = on; invalidate(); }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(900.f, r.w - 40.f), h = std::min(640.f, r.h - 40.f);
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        search_->setBounds({panel_.x + 16.f, panel_.y + 54.f, panel_.w - 32.f, 28.f});
        bottom_ = panel_.bottom() - 52.f;
        constructY_ = bottom_ - 74.f;
        detailY_ = constructY_ - 64.f;
        list_ = {panel_.x + 16.f, panel_.y + 150.f, panel_.w - 32.f, detailY_ - 8.f - (panel_.y + 150.f)};
        // La ligne Construire : [x] Tableau ARRAY[ [bornes] ] OF   [x] REF_TO (une reference)   [x] MAP[STRING] OF
        float x = panel_.x + 16.f + 96.f;
        arrayBox_ = refBox_ = mapBox_ = {};
        if (canArray(spec_.use)) {
            arrayBox_ = {x, constructY_ + 5.f, 24.f + kArrayLabel, 16.f};
            bounds_->setBounds({x + 24.f + kArrayLabel + 2.f, constructY_, 110.f, 26.f});
            x = bounds_->bounds().right() + 6.f + kOfLabel + 26.f;
        } else {
            bounds_->setBounds({});
        }
        if (canReference(spec_.use)) {
            refBox_ = {x, constructY_ + 5.f, 24.f + kRefLabel, 16.f};
            x += refBox_.w + 26.f;
            mapBox_ = {x, constructY_ + 5.f, 24.f + kMapLabel, 16.f};
        }
        const float bw = 120.f;
        ok_->setBounds({panel_.right() - 16.f - bw, bottom_ + 10.f, bw, 32.f});
        open_->setBounds({ok_->bounds().x - 10.f - 190.f, bottom_ + 10.f, 190.f, 32.f});
        cancel_->setBounds({open_->bounds().x - 10.f - 100.f, bottom_ + 10.f, 100.f, 32.f});
        rowsArea_ = {list_.x + 1.f, list_.y + kRow + 1.f, list_.w - 2.f, list_.h - kRow - 2.f};   // comme onPaint
        if (revealPending_) {
            revealPending_ = false;
            reveal();
        }
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        // ---- le titre ----
        const std::string title = spec_.field.empty() ? std::string("Choisir un type")
                                                      : "Choisir un type \xC2\xB7 \xC2\xAB " + spec_.field + " \xC2\xBB";
        r.drawText({panel_.x + 16.f, panel_.y + 13.f}, fit(r, title, kTitle, panel_.w * 0.62f), kTitle, c.text);
        const std::string forUse = "pour : " + useLabel(spec_.use);
        r.drawText({panel_.right() - 50.f - r.measure(forUse, kSmall).width, panel_.y + 16.f}, forUse, kSmall, c.textMuted);
        close_ = {panel_.right() - 36.f, panel_.y + 10.f, 24.f, 24.f};
        r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, hoverClose_ ? c.text : c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        // ---- les puces ----
        chips_.clear();
        float x = panel_.x + 16.f;
        const float cy = panel_.y + 90.f;
        const auto chip = [&](const std::string& label, int id, std::size_t count) {
            const std::string text = label + (count ? "  " + std::to_string(count) : std::string{});
            const float tw = r.measure(text, kSmall).width + 22.f;
            if (x + tw > panel_.right() - 16.f) return;
            const gfx::Rect b{x, cy, tw, 26.f};
            const bool on = chip_ == id;
            r.fillRoundedRect(b, on ? c.accent.withAlpha(60) : c.headerBg, 6.f);
            if (on) r.strokeRect(b, c.accent, 1.f);
            r.drawText({b.x + 11.f, b.y + (b.h - r.lineHeight(kSmall)) * 0.5f}, text, kSmall, on ? c.text : c.textMuted);
            chips_.push_back({b, id});
            x += tw + 6.f;
        };
        chip("Tous", kAll, 0);
        chip("R\xC3\xA9" "cents", kRecents, 0);
        for (const auto cat : tr::kCategories) {
            const auto n = countIn(cat);
            if (n) chip(std::string(tr::categoryLabel(cat)), static_cast<int>(cat), n);
        }
        // ---- l'avis : le type actuel introuvable ----
        if (!notice_.empty())
            r.drawText({panel_.x + 16.f, panel_.y + 124.f}, fit(r, notice_, kSmall, panel_.w - 32.f), kSmall, c.error);
        // ---- la liste ----
        r.fillRect(list_, c.windowBg);
        r.strokeRect(list_, c.border, 1.f);
        const float cw[] = {0.30f, 0.22f, 0.26f, 0.22f};
        const char* heads[] = {"Type", "Cat\xC3\xA9gorie", "Provenance", "Identifiant"};
        float hx = list_.x + 10.f;
        const gfx::Rect head{list_.x + 1.f, list_.y + 1.f, list_.w - 2.f, kRow};
        r.fillRect(head, c.headerBg);
        for (int k = 0; k < 4; ++k) {
            r.drawText({hx, head.y + (kRow - r.lineHeight(kSmall)) * 0.5f}, heads[k], kSmall, c.textMuted);
            hx += (list_.w - 20.f) * cw[k];
        }
        const gfx::Rect rows{list_.x + 1.f, list_.y + kRow + 1.f, list_.w - 2.f, list_.h - kRow - 2.f};
        rowsArea_ = rows;
        rowRects_.clear();
        r.pushClip(rows);
        const std::size_t first = static_cast<std::size_t>(std::max(0.f, scroll_ / kRow));
        for (std::size_t k = first; k < shown_.size(); ++k) {
            const float y = rows.y + static_cast<float>(k) * kRow - scroll_;
            if (y > rows.bottom()) break;
            const auto* e = shown_[k];
            const gfx::Rect rr{rows.x, y, rows.w, kRow};
            rowRects_.push_back({rr, static_cast<int>(k)});
            if (static_cast<int>(k) == sel_) r.fillRect(rr, c.selectionBg);
            else if (static_cast<int>(k) == hover_) r.fillRect(rr, c.rowAltBg);
            float tx = rr.x + 9.f;
            const float ty = rr.y + (kRow - r.lineHeight(kMono)) * 0.5f;
            const float sy = rr.y + (kRow - r.lineHeight(kSmall)) * 0.5f;
            const gfx::Color text = static_cast<int>(k) == sel_ ? c.selectionText : c.text;
            const gfx::Color muted = static_cast<int>(k) == sel_ ? c.selectionText : c.textMuted;
            r.drawText({tx, ty}, fit(r, e->name, kMono, (rows.w - 20.f) * cw[0] - 8.f), kMono, text);
            tx += (rows.w - 20.f) * cw[0];
            r.drawText({tx, sy}, fit(r, std::string(tr::categoryLabel(e->category)), kSmall, (rows.w - 20.f) * cw[1] - 8.f), kSmall, muted);
            tx += (rows.w - 20.f) * cw[1];
            r.drawText({tx, sy}, fit(r, e->provenance, kSmall, (rows.w - 20.f) * cw[2] - 8.f), kSmall, muted);
            tx += (rows.w - 20.f) * cw[2];
            r.drawText({tx, sy}, fit(r, e->key, kSmall, (rows.w - 20.f) * cw[3] - 8.f), kSmall, muted);
        }
        if (shown_.empty())
            r.drawText({rows.x + 12.f, rows.y + 10.f},
                       fit(r,
                           chip_ == kRecents ? std::string("Aucun type r\xC3\xA9" "cent : ceux que vous choisirez s'ajouteront ici.")
                                             : "Aucun type ne correspond. Un type cr\xC3\xA9\xC3\xA9 dans Types IHM appara\xC3\xAEt ici aussit\xC3\xB4t.",
                           kSmall, rows.w - 24.f),
                       kSmall, c.textMuted);
        r.popClip();
        sbar_.paint(ctx, rows, static_cast<float>(shown_.size()) * kRow, rows.h, scroll_);
        // ---- le detail ----
        paintDetail(ctx);
        // ---- construire ----
        paintConstruct(ctx);
        // ---- le pied ----
        r.fillRect({panel_.x, bottom_, panel_.w, 1.f}, c.border);
        const std::string help = "Entr\xC3\xA9" "e ou double-clic : choisir \xC2\xB7 Haut, Bas : parcourir \xC2\xB7 \xC3\x89" "chap : annuler";
        r.drawText({panel_.x + 16.f, bottom_ + 18.f}, fit(r, help, kSmall, cancel_->bounds().x - panel_.x - 32.f), kSmall, c.textMuted);
        ok_->setEnabled(problem().empty());
        open_->setEnabled(!openKey().empty());
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        {
            float off = scroll_;
            if (sbar_.handle(*this, ev, off)) {
                scroll_ = std::clamp(off, 0.f, maxScroll());
                invalidate();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
            int h = -1;
            for (const auto& [rect, k] : rowRects_)
                if (rect.contains(m->pos) && rowsArea_.contains(m->pos)) h = k;
            const bool hc = close_.contains(m->pos);
            if (h != hover_ || hc != hoverClose_) {
                hover_ = h;
                hoverClose_ = hc;
                invalidate();
            }
            return ui::EventResult::Ignored;
        }
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!list_.contains(w->pos)) return ui::EventResult::Ignored;
            scroll_ = std::clamp(scroll_ - w->dy * kRow * 3.f, 0.f, maxScroll());
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            if (d->button != ui::MouseButton::Left) return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
            if (close_.contains(d->pos)) {
                owner_.finish(false);
                return ui::EventResult::Consumed;
            }
            for (const auto& [rect, id] : chips_)
                if (rect.contains(d->pos)) {
                    chip_ = id;
                    sel_ = 0;
                    scroll_ = 0.f;
                    filter();
                    return ui::EventResult::Consumed;
                }
            if (arrayBox_.w > 0.f && arrayBox_.contains(d->pos)) {
                array_ = !array_;
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (refBox_.w > 0.f && refBox_.contains(d->pos)) {
                ref_ = !ref_;
                invalidate();
                return ui::EventResult::Consumed;
            }
            if (mapBox_.w > 0.f && mapBox_.contains(d->pos)) {
                map_ = !map_;
                invalidate();
                return ui::EventResult::Consumed;
            }
            for (const auto& [rect, k] : rowRects_) {
                if (!rect.contains(d->pos) || !rowsArea_.contains(d->pos)) continue;
                sel_ = k;
                invalidate();
                if (d->clickCount >= 2) owner_.choose();
                return ui::EventResult::Consumed;
            }
            return panel_.contains(d->pos) ? ui::EventResult::Consumed : ui::EventResult::Ignored;
        }
        return ui::EventResult::Ignored;
    }

private:
    static std::string trimmed(std::string_view s) {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return std::string(s.substr(a, b - a));
    }
    // ANY et Aucun ne se construisent pas (ni tableau, ni reference).
    static bool constructible(const tr::Entry* e) {
        return e && e->category != tr::Category::Generic && e->category != tr::Category::Void;
    }

    // Le registre du projet d'aujourd'hui ; `keep` : garder la ligne choisie (par sa cle).
    void reload(bool keep) {
        const std::string was = keep && selected() ? selected()->key : std::string{};
        if (spec_.doc) {
            const hmi::params::PlcTypes* plc = spec_.plc.names ? &spec_.plc : nullptr;
            reg_ = tr::Registry::build(spec_.doc->project, plc);
        } else {
            reg_ = std::make_shared<tr::Registry>();
        }
        usable_ = reg_->usable(spec_.use);
        filter();
        if (!was.empty())
            for (std::size_t i = 0; i < shown_.size(); ++i)
                if (shown_[i]->key == was) sel_ = static_cast<int>(i);
        sel_ = shown_.empty() ? -1 : std::clamp(sel_, 0, static_cast<int>(shown_.size()) - 1);
    }

    // Le type actuel : sa ligne, ses bornes ; introuvable : dit.
    void preselect() {
        const std::string cur = trimmed(spec_.current);
        if (cur.empty()) {
            sel_ = shown_.empty() ? -1 : 0;
            return;
        }
        const auto r = reg_->resolve(cur, tr::UseAll);
        if (r.missing || (!r.ok && !r.entry)) {
            notice_ = "Type actuel \xC2\xAB " + cur + " \xC2\xBB : " + (r.why.empty() ? std::string("introuvable") : r.why)
                      + ". Choisissez son rempla\xC3\xA7" "ant.";
            sel_ = shown_.empty() ? -1 : 0;
            return;
        }
        // un tableau : ses bornes (une dimension) ; une reference, une MAP : cochees
        std::string text = r.text;
        if (text.rfind("MAP[STRING] OF ", 0) == 0) {
            map_ = true;
            text = text.substr(15);
        }
        if (text.rfind("ARRAY[", 0) == 0) {
            const auto close = text.find(']');
            if (close != std::string::npos && text.compare(close, 5, "] OF ") == 0) {
                bounds_->setText(text.substr(6, close - 6));   // "0..9", "0..3, 0..9"
                array_ = true;
                text = text.substr(close + 5);
            }
        }
        if (text.rfind("REF_TO ", 0) == 0) {
            ref_ = true;
            text = text.substr(7);
        }
        if (r.entry && !select(r.entry->name)) {
            // hors de la puce montree : tout montrer
            chip_ = kAll;
            filter();
            (void)select(r.entry->name);
        }
    }

    [[nodiscard]] std::size_t countIn(tr::Category cat) const {
        return static_cast<std::size_t>(std::count_if(usable_.begin(), usable_.end(), [cat](const tr::Entry* e) { return e->category == cat; }));
    }

    void filter() {
        const std::string q = ui::foldForSearch(trimmed(search_ ? search_->text() : std::string{}));
        shown_.clear();
        std::vector<const tr::Entry*> pool;
        if (chip_ == kRecents) {
            for (const auto& n : spec_.recents)
                if (const auto* e = reg_->byName(n); e && e->usable(spec_.use)
                                                    && std::find(pool.begin(), pool.end(), e) == pool.end())
                    pool.push_back(e);
        } else {
            for (const auto* e : usable_)
                if (chip_ == kAll || static_cast<int>(e->category) == chip_) pool.push_back(e);
        }
        for (const auto* e : pool) {
            if (!q.empty()) {
                const std::string hay = e->name + " " + std::string(tr::categoryLabel(e->category)) + " " + e->provenance + " " + e->doc + " " + e->key;
                if (!ui::containsFolded(hay, q)) continue;
            }
            shown_.push_back(e);
        }
        if (sel_ >= static_cast<int>(shown_.size())) sel_ = static_cast<int>(shown_.size()) - 1;
        if (sel_ < 0 && !shown_.empty()) sel_ = 0;
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
        invalidate();
    }

    void reveal() {
        if (sel_ < 0) return;
        // Pas encore placee (le selecteur s'ouvre sur le type actuel) : a la mise en page,
        // quand la hauteur de la liste est connue (sinon la ligne choisie sortait du haut).
        if (list_.h <= 0.f) {
            revealPending_ = true;
            return;
        }
        const float top = static_cast<float>(sel_) * kRow, view = rowsArea_.h > 0.f ? rowsArea_.h : list_.h - kRow;
        if (top < scroll_) scroll_ = top;
        else if (top + kRow > scroll_ + view) scroll_ = top + kRow - view;
        scroll_ = std::clamp(scroll_, 0.f, maxScroll());
    }

    [[nodiscard]] float maxScroll() const {
        const float view = rowsArea_.h > 0.f ? rowsArea_.h : std::max(0.f, list_.h - kRow);
        return std::max(0.f, static_cast<float>(shown_.size()) * kRow - view);
    }

    void paintDetail(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const float x = panel_.x + 16.f, w = panel_.w - 32.f;
        float y = detailY_;
        const auto* e = selected();
        if (!e) return;
        r.drawText({x, y}, fit(r, e->name, kMono, 220.f), kMono, c.text);
        const float dx = x + std::min(220.f, r.measure(e->name, kMono).width) + 12.f;
        r.drawText({dx, y + 1.f}, fit(r, e->doc, kSmall, x + w - dx), kSmall, c.textMuted);
        y += 22.f;
        std::string more;
        if (!e->members.empty()) {
            more = "Membres : ";
            for (std::size_t i = 0; i < e->members.size(); ++i)
                more += (i ? ", " : "") + e->members[i].first + " : " + e->members[i].second;
        } else if (!e->values.empty()) {
            more = "Valeurs : ";
            for (std::size_t i = 0; i < e->values.size(); ++i) more += (i ? ", " : "") + e->values[i];
        } else if (e->numeric.family == tr::Family::Integer || e->numeric.family == tr::Family::Real) {
            more = "Se convertit sans perte vers : ";
            std::string list;
            for (const auto* o : usable_)
                if (o != e && o->category == tr::Category::Elementary && tr::conversion(e->name, o->name).safe())
                    list += (list.empty() ? "" : ", ") + o->name;
            more += list.empty() ? std::string("aucun autre type") : list;
        }
        if (!more.empty()) r.drawText({x, y}, fit(r, more, kSmall, w), kSmall, c.textMuted);
    }

    void paintConstruct(const ui::PaintContext& ctx) {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const float x0 = panel_.x + 16.f;
        float y = constructY_;
        const bool on = constructible(selected());
        if (arrayBox_.w > 0.f || refBox_.w > 0.f) {
            r.drawText({x0, y + 4.f}, "Construire :", kSmall, c.textMuted);
            const auto box = [&](const gfx::Rect& area, bool checked, const std::string& label) {
                paintCheck(ctx, {area.x, area.y, 16.f, 16.f}, checked, on);
                r.drawText({area.x + 24.f, y + 4.f}, label, kSmall, on ? c.text : c.textDisabled);
            };
            if (arrayBox_.w > 0.f) {
                box(arrayBox_, array_, "Tableau ARRAY[");
                r.drawText({bounds_->bounds().right() + 6.f, y + 4.f}, "] OF", kSmall, on ? c.text : c.textDisabled);
            }
            if (refBox_.w > 0.f) box(refBox_, ref_, "REF_TO (une r\xC3\xA9" "f\xC3\xA9rence)");
            if (mapBox_.w > 0.f) box(mapBox_, map_, "MAP[STRING] OF");
            y += 36.f;
        }
        // ---- le resultat ----
        r.drawText({x0, y + 2.f}, "R\xC3\xA9sultat :", kBody, c.text);
        const std::string res = result();
        const float rx = x0 + r.measure("R\xC3\xA9sultat :", kBody).width + 10.f;
        r.drawText({rx, y + 3.f}, fit(r, res.empty() ? std::string("\xE2\x80\x94") : res, kMono, panel_.w * 0.5f), kMono, c.text);
        const std::string bad = problem();
        if (!bad.empty() && !res.empty()) {
            const float px = rx + std::min(panel_.w * 0.5f, r.measure(res, kMono).width) + 14.f;
            r.drawText({px, y + 4.f}, fit(r, bad, kSmall, panel_.right() - 16.f - px), kSmall, c.error);
        }
    }

    HmiTypePicker&                         owner_;
    const Spec&                            spec_;
    std::shared_ptr<const tr::Registry>    reg_;
    std::vector<const tr::Entry*>          usable_, shown_;
    int                                    chip_{kAll};
    int                                    sel_{-1};
    int                                    hover_{-1};
    bool                                   hoverClose_{false};
    bool                                   array_{false}, ref_{false}, map_{false};
    std::string                            notice_;
    float                                  scroll_{0.f}, bottom_{0.f}, constructY_{0.f}, detailY_{0.f};
    bool                                   revealPending_{false};   // la ligne choisie, a montrer des la mise en page
    ui::InputText*                         search_{nullptr};
    ui::InputText*                         bounds_{nullptr};
    ui::Button*                            cancel_{nullptr};
    ui::Button*                            open_{nullptr};
    ui::Button*                            ok_{nullptr};
    gfx::Rect                              panel_{}, list_{}, rowsArea_{}, close_{}, arrayBox_{}, refBox_{}, mapBox_{};
    ui::PaintedScrollBar                   sbar_;
    std::vector<std::pair<gfx::Rect, int>> chips_;
    std::vector<std::pair<gfx::Rect, int>> rowRects_;
    core::ConnectionScope                  links_;
};

// ============================================================== le dialogue ====
HmiTypePicker::HmiTypePicker(Spec spec) : menu::WidgetMenu("dialog.typePicker"), spec_(std::move(spec)) {}
HmiTypePicker::~HmiTypePicker() = default;

menu::MenuTraits HmiTypePicker::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string HmiTypePicker::title() const { return "Choisir un type"; }

core::Status HmiTypePicker::buildUi() {
    auto body = std::make_unique<Body>(*this, spec_);
    body_ = body.get();
    setRoot(std::move(body));
    return core::ok();
}

void HmiTypePicker::onEnter() {
    if (body_) body_->focusFirst();
}

ui::EventResult HmiTypePicker::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && body_) {
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return) {
            choose();
            return ui::EventResult::Consumed;
        }
        if (body_->searchFocused() && (k->key == ui::Key::Down || k->key == ui::Key::Up)) {
            body_->move(k->key == ui::Key::Down ? 1 : -1);
            return ui::EventResult::Consumed;
        }
        if (body_->searchFocused() && (k->key == ui::Key::PageDown || k->key == ui::Key::PageUp)) {
            body_->move(k->key == ui::Key::PageDown ? 10 : -10);
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void HmiTypePicker::setSearch(const std::string& text) { if (body_) body_->setSearch(text); }
bool HmiTypePicker::setCategory(std::string_view label) { return body_ && body_->setCategory(label); }
bool HmiTypePicker::select(std::string_view name) { return body_ && body_->select(name); }
void HmiTypePicker::setArray(bool on, const std::string& bounds) { if (body_) body_->setArray(on, bounds); }
void HmiTypePicker::setReference(bool on) { if (body_) body_->setReference(on); }
void HmiTypePicker::setMap(bool on) { if (body_) body_->setMap(on); }
std::vector<std::string> HmiTypePicker::shownNames() const { return body_ ? body_->shownNames() : std::vector<std::string>{}; }
std::string HmiTypePicker::result() const { return body_ ? body_->result() : std::string{}; }
std::string HmiTypePicker::resultProblem() const { return body_ ? body_->problem() : std::string("pas de s\xC3\xA9lecteur"); }
std::string HmiTypePicker::notice() const { return body_ ? body_->notice() : std::string{}; }
bool HmiTypePicker::selectionVisible() const { return body_ && body_->selectionVisible(); }

void HmiTypePicker::choose() {
    if (!body_ || done_ || !body_->problem().empty()) return;
    answer_.type = body_->result();
    answer_.element = body_->elementName();
    finish(true);
}

void HmiTypePicker::openDefinition() {
    if (!body_ || done_) return;
    answer_.open = body_->openKey();
    if (answer_.open.empty()) return;
    finish(true);
}

void HmiTypePicker::finish(bool ok) {
    if (done_) return;
    done_ = true;
    const std::string payload = answer_.type + kSep + answer_.open + kSep + answer_.element;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, payload});
}

HmiTypePicker::Answer HmiTypePicker::parse(const std::string& payload) {
    Answer a;
    std::vector<std::string> parts{""};
    for (const char ch : payload) {
        if (ch == kSep) parts.emplace_back();
        else parts.back() += ch;
    }
    a.type = parts[0];
    a.open = parts.size() > 1 ? parts[1] : std::string{};
    a.element = parts.size() > 2 ? parts[2] : std::string{};
    return a;
}

namespace typepicker {
namespace {
Host& host() {
    static Host h;
    return h;
}
} // namespace
void setHost(Host h) { host() = std::move(h); }
bool available() { return static_cast<bool>(host()); }
void ask(HmiTypePicker::Spec spec, Done done) {
    if (host()) host()(std::move(spec), std::move(done));
}
} // namespace typepicker

void HmiTypePicker::remember(std::vector<std::string>& recents, const std::string& name) {
    if (name.empty()) return;
    recents.erase(std::remove(recents.begin(), recents.end(), name), recents.end());
    recents.insert(recents.begin(), name);
    if (recents.size() > 8) recents.resize(8);
}

} // namespace app
