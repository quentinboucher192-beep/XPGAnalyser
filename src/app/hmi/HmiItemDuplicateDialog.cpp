// app/hmi/HmiItemDuplicateDialog.cpp - « Dupliquer... » d'un element (1.12.16).
#include "HmiItemDuplicateDialog.hpp"

#include "../../core/FieldCodec.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace dup = hmi::dup;

namespace {

const gfx::FontId kSmall{13};
const gfx::Color  kAmber{226, 160, 40, 255};

constexpr char kRowSeparator = '\x1e';   // entre deux copies (RS) ; entre deux champs : core::kFieldSeparator (US)

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// Une couleur ecrite #RRGGBB (la pastille de sa case).
bool colorOf(const std::string& t, gfx::Color& out) {
    if (t.size() != 7 || t[0] != '#') return false;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; ++i)
        if ((v[i] = hex(t[static_cast<std::size_t>(i) + 1])) < 0) return false;
    out = gfx::Color{static_cast<std::uint8_t>(v[0] * 16 + v[1]), static_cast<std::uint8_t>(v[2] * 16 + v[3]),
                     static_cast<std::uint8_t>(v[4] * 16 + v[5]), 255};
    return true;
}

} // namespace

// ------------------------------------------------------------------ Grid ---
// Le tableau des copies : l'original en tete (en gris, en lecture), puis une
// ligne de cases par copie ; une colonne par champ, une expression plus large.
class HmiItemDuplicateDialog::Grid final : public ui::Widget {
public:
    explicit Grid(HmiItemDuplicateDialog& d) : d_(d) {}
    std::vector<std::vector<ui::InputText*>> cells;   // [copie - 1][champ]
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {600.f, 30.f + 32.f * static_cast<float>(cells.size() + 1)};
        h.minimum = h.preferred;
        h.stretchX = 1.f;
        return h;
    }
    [[nodiscard]] const dup::ItemField& field(std::size_t k) const { return d_.spec_.fields[d_.columns_[k]]; }
    [[nodiscard]] std::vector<float> widths(float w) const {
        float weight = 0;
        for (std::size_t k = 0; k < d_.columns_.size(); ++k) weight += field(k).kind == dup::ItemField::Kind::Expression ? 2.f : 1.f;
        std::vector<float> out;
        // Au moins 110 px par colonne quand elles tiennent ; beaucoup de colonnes (un tableau large) se partagent la place.
        const float least = std::min(110.f, w / static_cast<float>(std::max<std::size_t>(1, d_.columns_.size())));
        for (std::size_t k = 0; k < d_.columns_.size(); ++k)
            out.push_back(std::max(least, w * (field(k).kind == dup::ItemField::Kind::Expression ? 2.f : 1.f) / std::max(1.f, weight)));
        return out;
    }
protected:
    void onLayout() override {
        const auto b = bounds();
        const auto ws = widths(b.w - 64.f);
        for (std::size_t r = 0; r < cells.size(); ++r) {
            float x = b.x + 60.f;
            for (std::size_t c = 0; c < cells[r].size() && c < ws.size(); ++c) {
                const bool swatch = field(c).kind == dup::ItemField::Kind::Rotate;
                cells[r][c]->setBounds({x + (swatch ? 20.f : 0.f), b.y + 60.f + 32.f * static_cast<float>(r), ws[c] - 8.f - (swatch ? 20.f : 0.f), 28.f});
                x += ws[c];
            }
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto b = bounds();
        const auto& col = ctx.theme.color;
        const auto ws = widths(b.w - 64.f);
        // Les en-tetes, puis l'original (en gris).
        float x = b.x + 60.f;
        for (std::size_t c = 0; c < ws.size(); ++c) {
            ctx.r.pushClip({x, b.y, std::max(0.f, ws[c] - 8.f), 58.f});
            ctx.r.drawText({x, b.y + 4.f}, field(c).label, ctx.theme.font.uiBold, col.text);
            ctx.r.drawText({x + 4.f, b.y + 34.f}, field(c).text.empty() ? std::string("(vide)") : field(c).text, kSmall, col.textMuted);
            ctx.r.popClip();
            x += ws[c];
        }
        ctx.r.drawText({b.x + 6.f, b.y + 34.f}, "original", kSmall, col.textMuted);
        ctx.r.fillRect({b.x, b.y + 56.f, b.w, 1.f}, col.borderStrong);
        for (std::size_t r = 0; r < cells.size(); ++r) {
            const float y = b.y + 60.f + 32.f * static_cast<float>(r);
            if (r % 2 == 1) ctx.r.fillRect({b.x, y - 2.f, b.w, 32.f}, gfx::Color{128, 128, 128, 18});
            ctx.r.drawText({b.x + 6.f, y + 6.f}, "copie " + std::to_string(r + 1), kSmall, col.text);
            // La pastille d'une couleur.
            for (std::size_t c = 0; c < cells[r].size() && c < ws.size(); ++c) {
                if (field(c).kind != dup::ItemField::Kind::Rotate) continue;
                gfx::Color k;
                const auto rr = cells[r][c]->bounds();
                if (colorOf(cells[r][c]->text(), k)) {
                    ctx.r.fillRect({rr.x - 18.f, rr.y + 7.f, 14.f, 14.f}, k);
                    ctx.r.strokeRect({rr.x - 18.f, rr.y + 7.f, 14.f, 14.f}, col.borderStrong, 1.f);
                }
            }
            // Une case vide qui ne l'etait pas dans l'original : ambre.
            for (std::size_t c = 0; c < cells[r].size() && c < ws.size(); ++c)
                if (cells[r][c]->text().empty() && !field(c).text.empty()) {
                    auto rr = cells[r][c]->bounds();
                    ctx.r.strokeRect({rr.x - 2.f, rr.y - 2.f, rr.w + 4.f, rr.h + 4.f}, kAmber, 2.f);
                }
        }
    }
private:
    HmiItemDuplicateDialog& d_;
};

// ------------------------------------------------------------------ Body ---
class HmiItemDuplicateDialog::Body final : public ui::Widget {
public:
    explicit Body(HmiItemDuplicateDialog& d) : d_(d) {}
    std::vector<ui::Button*> buttons;
    gfx::Rect panel{};
protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(980.f, r.w - 40.f), h = std::min(620.f, r.h - 40.f);
        panel = {std::floor((r.w - w) * 0.5f), std::floor((r.h - h) * 0.5f), w, h};
        float y = panel.y + 44.f + 2.f * 20.f;   // ce qui est duplique, les indices
        float x = panel.x + 16.f;
        for (auto* box : d_.followBoxes_) {
            const float bw = std::min(panel.w - 32.f, ui::measureWidth(box->label(), gfx::FontId{16}) + 40.f);
            if (x + bw > panel.x + panel.w - 16.f) { x = panel.x + 16.f; y += 28.f; }
            box->setBounds({x, y, bw, 26.f});
            x += bw + 12.f;
        }
        if (!d_.followBoxes_.empty()) y += 34.f;
        copiesY_ = y;
        if (d_.copiesField_) d_.copiesField_->setBounds({panel.x + 16.f + 64.f, y, 64.f, 28.f});
        y += 40.f;
        const float bottom = panel.y + panel.h - 56.f - 22.f;   // les avertissements au-dessus des boutons
        if (d_.gridPanel_) {
            d_.gridPanel_->setBounds({panel.x + 8.f, y, panel.w - 16.f, std::max(80.f, bottom - y)});
            if (d_.grid_)
                d_.grid_->setBounds({panel.x + 8.f, y - d_.gridPanel_->scrollOffset().y, panel.w - 32.f, d_.grid_->sizeHint().preferred.h});
        }
        warnY_ = bottom + 4.f;
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
        float y = panel.y + 44.f;
        ctx.r.pushClip({panel.x + 12.f, y - 2.f, panel.w - 24.f, 44.f});
        ctx.r.drawText({panel.x + 16.f, y}, d_.spec_.what, ctx.theme.font.uiBold, c.text);
        y += 20.f;
        std::string ix = d_.indices_.empty() ? std::string("Aucun indice de tableau : les noms suivent leur num\xC3\xA9ro (Plume 1, Plume 2\xE2\x80\xA6).")
                                             : "Indices : ";
        for (std::size_t i = 0; i < d_.indices_.size(); ++i) {
            const auto& k = d_.indices_[i];
            ix += (i ? "   " : "") + k.label();
            if (k.bounds) ix += " (" + std::to_string(k.bounds->first) + ".." + std::to_string(k.bounds->second) + ")";
        }
        ctx.r.drawText({panel.x + 16.f, y}, ix, kSmall, c.textMuted);
        ctx.r.popClip();
        ctx.r.drawText({panel.x + 16.f, copiesY_ + 6.f}, "Copies", ctx.theme.font.uiBold, c.text);
        if (d_.copiesField_) {
            const auto r = d_.copiesField_->bounds();
            ctx.r.drawText({r.x + r.w + 12.f, copiesY_ + 6.f},
                           "chacune ins\xC3\xA9r\xC3\xA9" "e apr\xC3\xA8s l'original, dans l'ordre ; une case corrig\xC3\xA9" "e le reste",
                           kSmall, c.textMuted);
        }
        const auto warns = d_.warnings();
        if (!warns.empty()) {
            ctx.r.pushClip({panel.x + 12.f, warnY_ - 2.f, panel.w - 24.f, 22.f});
            std::string all;
            for (std::size_t i = 0; i < warns.size(); ++i) all += (i ? "  \xC2\xB7  " : "") + warns[i];
            ctx.r.drawText({panel.x + 16.f, warnY_}, all, kSmall, kAmber);
            ctx.r.popClip();
        }
    }
private:
    HmiItemDuplicateDialog& d_;
    float                   copiesY_{0}, warnY_{0};
};

// ===================================================================== ======
HmiItemDuplicateDialog::HmiItemDuplicateDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {
    copies_ = std::clamp(spec_.copies, 1, kMaxCopies);
    for (std::size_t i = 0; i < spec_.fields.size(); ++i)
        if (spec_.fields[i].shown) columns_.push_back(i);
    indices_ = dup::itemIndices(spec_.fields, spec_.bounds);
    // Suivre d'office, sauf un indice dont les copies sortiraient des bornes connues.
    for (const auto& k : indices_) follow_.push_back(!k.bounds || dup::followFits(k, copies_));
    recompute();
}

menu::MenuTraits HmiItemDuplicateDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

void HmiItemDuplicateDialog::recompute() {
    const auto fresh = dup::itemCopies(spec_.fields, indices_, follow_, copies_);
    rows_.resize(fresh.size());
    edited_.resize(fresh.size());
    for (std::size_t r = 0; r < fresh.size(); ++r) {
        rows_[r].resize(fresh[r].size());
        edited_[r].resize(fresh[r].size(), false);
        for (std::size_t c = 0; c < fresh[r].size(); ++c)
            if (!edited_[r][c]) rows_[r][c] = fresh[r][c];
    }
    // Les cases a l'ecran : autant de lignes que de copies, et leur texte.
    if (!gridPanel_) return;
    if (!grid_ || grid_->cells.size() != rows_.size()) {
        rebuildCells();
    } else {
        syncing_ = true;
        for (std::size_t r = 0; r < rows_.size(); ++r)
            for (std::size_t k = 0; k < columns_.size() && k < grid_->cells[r].size(); ++k)
                if (grid_->cells[r][k]->text() != rows_[r][columns_[k]]) grid_->cells[r][k]->setText(rows_[r][columns_[k]]);
        syncing_ = false;
    }
    refresh();
}

void HmiItemDuplicateDialog::rebuildCells() {
    if (!gridPanel_) return;
    // La case qui a le focus va disparaitre : le focus ne garde pas de pointeur sur elle.
    if (grid_)
        for (const auto& line : grid_->cells)
            for (const auto* cell : line)
                if (cell == focus().current()) focus().clear();
    cellLinks_.clear();
    auto g = std::make_unique<Grid>(*this);
    grid_ = g.get();
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        std::vector<ui::InputText*> line;
        for (const std::size_t c : columns_) {
            if (c >= rows_[r].size()) continue;
            auto f = std::make_unique<ui::InputText>(spec_.id + ".cell." + std::to_string(r + 1) + "." + std::to_string(c));
            f->setText(rows_[r][c]);
            f->setPlaceholder(spec_.fields[c].text);
            if (spec_.fields[c].kind == dup::ItemField::Kind::Expression && spec_.assist) f->setAssist(spec_.assist);
            auto* raw = &static_cast<ui::InputText&>(g->addChild(std::move(f)));
            cellLinks_ += raw->textChanged->connect([this, r, c](const std::string& t) {
                if (syncing_ || r >= rows_.size() || c >= rows_[r].size()) return;
                rows_[r][c] = t;
                edited_[r][c] = true;
                refresh();
            });
            line.push_back(raw);
        }
        g->cells.push_back(std::move(line));
    }
    gridPanel_->setContent(std::move(g));
    if (body_) body_->invalidateLayout();
}

void HmiItemDuplicateDialog::refresh() {
    if (ok_) {
        const std::string label = confirmLabel();
        if (label != ok_->text()) {
            ok_->setText(label);
            if (body_) body_->invalidateLayout();
        }
        ok_->setEnabled(canConfirm());
    }
    if (body_) body_->invalidate();
    if (grid_) grid_->invalidate();
}

void HmiItemDuplicateDialog::setFollow(std::size_t index, bool on) {
    if (index >= follow_.size() || follow_[index] == on) return;
    follow_[index] = on;
    if (index < followBoxes_.size()) {
        syncing_ = true;
        followBoxes_[index]->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        syncing_ = false;
    }
    recompute();
}

void HmiItemDuplicateDialog::setCopies(int copies) {
    copies = std::clamp(copies, 1, kMaxCopies);
    if (copies == copies_) return;
    copies_ = copies;
    if (copiesField_ && std::atoi(copiesField_->text().c_str()) != copies_) {
        syncing_ = true;
        copiesField_->setText(std::to_string(copies_));
        syncing_ = false;
    }
    recompute();
}

bool HmiItemDuplicateDialog::setCell(int row, std::size_t column, std::string value) {
    if (row < 1 || row > static_cast<int>(rows_.size()) || column >= spec_.fields.size()) return false;
    const auto r = static_cast<std::size_t>(row - 1);
    rows_[r][column] = std::move(value);
    edited_[r][column] = true;
    const auto shown = std::find(columns_.begin(), columns_.end(), column);
    const auto k = static_cast<std::size_t>(shown - columns_.begin());
    if (grid_ && shown != columns_.end() && r < grid_->cells.size() && k < grid_->cells[r].size()) {
        syncing_ = true;
        grid_->cells[r][k]->setText(rows_[r][column]);
        syncing_ = false;
    }
    refresh();
    return true;
}

std::string HmiItemDuplicateDialog::cell(int row, std::size_t column) const {
    if (column >= spec_.fields.size()) return {};
    if (row == 0) return spec_.fields[column].text;
    if (row < 1 || row > static_cast<int>(rows_.size())) return {};
    return rows_[static_cast<std::size_t>(row - 1)][column];
}

std::vector<std::string> HmiItemDuplicateDialog::warnings() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < indices_.size(); ++i) {
        const auto& k = indices_[i];
        if (!following(i) || !k.bounds) continue;
        const long long last = k.value + copies_;
        if (last > k.bounds->second)
            out.push_back(k.path + "[" + std::to_string(last) + "] sort des bornes de " + k.path + " (" + std::to_string(k.bounds->first) + ".."
                          + std::to_string(k.bounds->second) + ") : d\xC3\xA9" "coche Suivre, ou moins de copies");
    }
    int empty = 0;
    for (const auto& row : rows_)
        for (const std::size_t c : columns_) empty += c < row.size() && row[c].empty() && !spec_.fields[c].text.empty();
    if (empty) out.push_back(plural(empty, "case vide", "cases vides") + " (l'original en a une valeur)");
    return out;
}

std::string HmiItemDuplicateDialog::confirmLabel() const {
    return "Dupliquer (" + plural(copies_, "copie", "copies") + ")";
}

bool HmiItemDuplicateDialog::canConfirm() const { return copies_ >= 1 && !rows_.empty(); }

bool HmiItemDuplicateDialog::confirm() {
    if (!canConfirm()) return false;
    finish(true);
    return true;
}

void HmiItemDuplicateDialog::cancel() { finish(false); }

std::string HmiItemDuplicateDialog::encode(const std::vector<std::vector<std::string>>& rows) {
    std::string out;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (r) out.push_back(kRowSeparator);
        out += core::joinFields(rows[r]);
    }
    return out;
}

std::vector<std::vector<std::string>> HmiItemDuplicateDialog::parse(const std::string& payload) {
    std::vector<std::vector<std::string>> out;
    if (payload.empty()) return out;
    std::size_t from = 0;
    while (true) {
        const auto at = payload.find(kRowSeparator, from);
        const std::string row = payload.substr(from, at == std::string::npos ? std::string::npos : at - from);
        std::vector<std::string> fields;
        std::size_t f = 0;
        while (true) {
            const auto sep = row.find(core::kFieldSeparator, f);
            fields.push_back(row.substr(f, sep == std::string::npos ? std::string::npos : sep - f));
            if (sep == std::string::npos) break;
            f = sep + 1;
        }
        out.push_back(std::move(fields));
        if (at == std::string::npos) break;
        from = at + 1;
    }
    return out;
}

void HmiItemDuplicateDialog::finish(bool ok) {
    if (done_) return;
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, ok ? encode(rows_) : std::string{}});
}

core::Status HmiItemDuplicateDialog::buildUi() {
    auto body = std::make_unique<Body>(*this);
    body_ = body.get();
    const std::string base = spec_.id;
    for (std::size_t i = 0; i < indices_.size(); ++i) {
        auto box = std::make_unique<ui::Checkbox>("Suivre " + indices_[i].label() + " (sinon garder)", base + ".follow" + std::to_string(i));
        box->setState(follow_[i] ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
        auto* raw = &static_cast<ui::Checkbox&>(body->addChild(std::move(box)));
        followBoxes_.push_back(raw);
        links_ += raw->stateChanged->connect([this, i](ui::Checkbox::State s) {
            if (!syncing_) setFollow(i, s == ui::Checkbox::State::Checked);
        });
    }
    {
        auto f = std::make_unique<ui::InputText>(base + ".copies");
        f->setText(std::to_string(copies_));
        copiesField_ = &static_cast<ui::InputText&>(body->addChild(std::move(f)));
        links_ += copiesField_->textChanged->connect([this](const std::string& t) {
            if (syncing_ || t.empty()) return;
            setCopies(std::atoi(t.c_str()));
        });
    }
    {
        auto panel = std::make_unique<ui::ScrollablePanel>(base + ".grid");
        panel->setScrollPolicy(false, true);
        gridPanel_ = &static_cast<ui::ScrollablePanel&>(body->addChild(std::move(panel)));
        rebuildCells();
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

ui::EventResult HmiItemDuplicateDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // Ctrl+Entree : « Dupliquer » (Entree choisit dans l'aide d'une case).
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
