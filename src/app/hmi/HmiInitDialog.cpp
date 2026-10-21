// =============================================================================
//  app/hmi/HmiInitDialog.cpp - 1.12.16 (T4) : " Valeur initiale de ... ", en brut
//  ou en code - la fenetre HmiCalcDialog a deux modes (voir HmiCalcDialog.hpp)
// =============================================================================
#include "HmiCalcDialog.hpp"
#include "../../ui/DialogFrame.hpp"   // 1.12.21 : la fenetre se deplace et s'agrandit

#include "../../core/FieldCodec.hpp"
#include "../../hmi/HmiDecl.hpp"
#include "../../menu/MenuManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace app {

namespace iv = initvalue;

namespace {

const gfx::FontId kSmall{13};
const gfx::FontId kButton{16};

float buttonWidth(const std::string& text) { return std::max(64.f, ui::measureWidth(text, kButton) + 28.f); }

std::string trimmed(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Un texte coupe en lignes (les \n d'abord, puis les mots a la largeur).
std::vector<std::string> wrapped(const std::string& s, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::size_t from = 0;
    for (;;) {
        const auto nl = s.find('\n', from);
        const std::string para = s.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        std::string line, word;
        const auto flush = [&] {
            if (word.empty()) return;
            const std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(trial, f) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            word.clear();
        };
        for (const char c : para) {
            if (c == ' ') flush();
            else word += c;
        }
        flush();
        if (!line.empty()) out.push_back(line);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
    return out;
}

// Des locales d'un exemple (une par ligne, sans le point-virgule) : celles de la grille.
std::string localsLines(const std::string& text) {
    std::string out;
    std::size_t from = 0;
    while (from < text.size()) {
        const auto nl = text.find('\n', from);
        std::string line = trimmed(text.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
        from = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty()) continue;
        if (line.back() != ';') line += ';';
        out += line + "\n";
    }
    return out;
}

bool number(const std::string& t, double& v) {
    const std::string s = trimmed(t);
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

} // namespace

// ---------------------------------------------------------------- la table du Brut ----
class HmiCalcDialog::RawModel final : public ui::ITableModel {
public:
    explicit RawModel(iv::Raw& raw) : raw_(raw) {}
    std::set<std::size_t> badRows;      // les lignes d'une faute (en rouge)

    // La case du Brut d'une colonne (-1 : une colonne qui se lit) ; `key` : la cle d'une MAP.
    [[nodiscard]] int cellOf(std::size_t col, bool* key = nullptr) const {
        if (key) *key = false;
        const auto& s = raw_.shape;
        if (raw_.freeText) return col == 0 ? 0 : -1;
        switch (s.kind) {
            case iv::Kind::Map:
                if (col == 0) {
                    if (key) *key = true;
                    return -1;
                }
                return col == 1 ? 0 : -1;
            case iv::Kind::Struct:
            case iv::Kind::Tuple: return col == 2 ? 0 : -1;
            case iv::Kind::Time: return static_cast<int>(col);
            case iv::Kind::Scalar: return col == 1 ? 0 : -1;
            case iv::Kind::Enum: return -1;
            default:   // List, Set, Ring, Array, Matrix : le rang, puis les cases
                if (col == 0) return -1;
                if ((s.kind == iv::Kind::Set || s.kind == iv::Kind::Ring) && !s.structElements && col == 2) return -1;   // la marque
                return static_cast<int>(col) - 1;
        }
    }
    [[nodiscard]] std::vector<std::string> headers() const {
        const auto& s = raw_.shape;
        if (raw_.freeText) return {"Valeur (telle qu'\xC3\xA9" "crite)"};
        const auto cells = iv::cellLabels(raw_);
        switch (s.kind) {
            case iv::Kind::Map: return {"Cl\xC3\xA9 (" + s.key + ")", "Valeur (" + s.element + ")"};
            case iv::Kind::Struct: return {"Membre", "Type", "Valeur"};
            case iv::Kind::Tuple: return {"\xC3\x89l\xC3\xA9ment", "Type", "Valeur"};
            case iv::Kind::Time: return cells;
            case iv::Kind::Scalar: return {"Type", "Valeur"};
            case iv::Kind::Enum: return {"", "Valeur", "Nombre", "Texte"};
            case iv::Kind::Set:
            case iv::Kind::Ring:
                if (!s.structElements) return {"Rang", cells.empty() ? std::string("Valeur") : cells.front(), ""};
                [[fallthrough]];
            default: {
                std::vector<std::string> out{s.kind == iv::Kind::Array || s.kind == iv::Kind::Matrix ? "Indice" : "Rang"};
                out.insert(out.end(), cells.begin(), cells.end());
                return out;
            }
        }
    }
    [[nodiscard]] std::vector<ui::TableView::Column> columns(float width) const {
        const auto h = headers();
        std::vector<ui::TableView::Column> out;
        const auto& s = raw_.shape;
        float fixed = 0.f;
        std::vector<float> want(h.size(), 0.f);
        for (std::size_t c = 0; c < h.size(); ++c) {
            bool key = false;
            const int cell = cellOf(c, &key);
            if (s.kind == iv::Kind::Enum && !raw_.freeText) want[c] = c == 0 ? 28.f : c == 2 ? 80.f : 0.f;
            else if (cell < 0 && !key) want[c] = (s.kind == iv::Kind::Struct || s.kind == iv::Kind::Tuple || s.kind == iv::Kind::Scalar) ? 150.f : 90.f;
            fixed += want[c];
        }
        std::size_t flex = 0;
        for (const float x : want) flex += x == 0.f ? 1 : 0;
        const float each = flex ? std::max(90.f, (width - fixed - 4.f) / static_cast<float>(flex)) : 0.f;
        for (std::size_t c = 0; c < h.size(); ++c)
            out.push_back({h[c], want[c] == 0.f ? each : want[c], 28.f, true, false, true, ui::Align::Start, 0, false});
        return out;
    }

    [[nodiscard]] std::size_t rowCount() const override {
        if (raw_.shape.kind == iv::Kind::Enum && !raw_.freeText) return raw_.shape.options.size();
        return raw_.rows.size();
    }
    [[nodiscard]] std::size_t columnCount() const override { return headers().size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        const auto h = headers();
        return c < h.size() ? h[c] : std::string{};
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        const auto& s = raw_.shape;
        if (s.kind == iv::Kind::Enum && !raw_.freeText) {
            if (r >= s.options.size()) return {};
            const auto& o = s.options[r];
            switch (c) {
                case 0: return static_cast<int>(r) == raw_.choice ? "\xE2\x97\x8F" : "";
                case 1: return "#" + o.name;
                case 2: return std::to_string(o.number);
                default: return o.text;
            }
        }
        if (r >= raw_.rows.size()) return {};
        const auto& row = raw_.rows[r];
        bool key = false;
        const int cell = cellOf(c, &key);
        if (key) return row.key;
        if (cell >= 0) return static_cast<std::size_t>(cell) < row.cells.size() ? row.cells[static_cast<std::size_t>(cell)] : std::string{};
        switch (s.kind) {
            case iv::Kind::Struct:
            case iv::Kind::Tuple:
                if (c == 0) return iv::rowLabel(raw_, r);
                return r < s.fields.size() ? s.fields[r].type : std::string{};
            case iv::Kind::Scalar: return s.type;
            case iv::Kind::Set:
            case iv::Kind::Ring:
                if (c == 2) return iv::rowMark(raw_, r);
                return iv::rowLabel(raw_, r);
            default: return iv::rowLabel(raw_, r);
        }
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        const auto& s = raw_.shape;
        st.monospace = true;
        if (s.kind == iv::Kind::Enum && !raw_.freeText) {
            if (c == 0) st.fgTone = ui::Tone::Accent;
            if (c == 3) {
                st.fgTone = ui::Tone::Muted;
                st.monospace = false;
            }
            return st;
        }
        bool key = false;
        const int cell = cellOf(c, &key);
        if (cell < 0 && !key) {
            st.fgTone = ui::Tone::Muted;
            if ((s.kind == iv::Kind::Struct || s.kind == iv::Kind::Tuple) && c == 1) st.fgTone = ui::Tone::Accent;
            if (s.kind == iv::Kind::Scalar && c == 0) st.fgTone = ui::Tone::Accent;
            if ((s.kind == iv::Kind::Set || s.kind == iv::Kind::Ring) && c == 2) {
                st.fgTone = ui::Tone::Warning;
                st.monospace = false;
            }
            return st;
        }
        if (badRows.count(r)) st.fgTone = ui::Tone::Error;
        if (cell >= 0 && r < raw_.rows.size()) {
            const auto& row = raw_.rows[r];
            const std::string text = static_cast<std::size_t>(cell) < row.cells.size() ? row.cells[static_cast<std::size_t>(cell)] : std::string{};
            if (trimmed(text).empty()) st.placeholder = iv::placeholder(raw_, r, static_cast<std::size_t>(cell));
        }
        if (key && r < raw_.rows.size() && trimmed(raw_.rows[r].key).empty()) st.placeholder = "la cl\xC3\xA9";
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t) const override { return a < b; }
    [[nodiscard]] bool editable(ui::RowIndex r, std::size_t c) const override {
        if (raw_.shape.kind == iv::Kind::Enum && !raw_.freeText) return false;
        if (r >= raw_.rows.size()) return false;
        bool key = false;
        return cellOf(c, &key) >= 0 || key;
    }
    bool setCellText(ui::RowIndex r, std::size_t c, std::string_view text) override {
        std::string v(text);
        for (auto& ch : v)
            if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        bool key = false;
        const int cell = cellOf(c, &key);
        bool ok = false;
        if (key) ok = iv::setKey(raw_, r, v);
        else if (cell >= 0) ok = iv::setCell(raw_, r, static_cast<std::size_t>(cell), v);
        if (ok && edited) edited();
        return ok;
    }
    std::function<void()> edited;

private:
    iv::Raw& raw_;
};

// ---------------------------------------------------------------- le corps ----
namespace {

class InitBody final : public ui::Widget {
public:
    explicit InitBody(std::string title) : title_(std::move(title)) {}
    // Le Code (comme la fenetre du calcul).
    ui::Widget*              code{nullptr};
    ui::Widget*              table{nullptr};
    ui::Widget*              unfold{nullptr};
    const bool*              unfoldShown{nullptr};
    ui::Button*              tabCode{nullptr};
    ui::Button*              tabLocals{nullptr};
    std::vector<ui::Button*> codeBar, localsBar, templates;
    // Le Brut.
    ui::Widget*              rawTable{nullptr};
    std::vector<ui::Button*> rawBar;
    // Le haut et le bas.
    ui::Button*              modeRaw{nullptr};
    ui::Button*              modeCode{nullptr};
    ui::Button*              reopen{nullptr};
    ui::Button*              toCode{nullptr};
    ui::Button*              cancel{nullptr};
    ui::Button*              ok{nullptr};
    const bool*              codeShown{nullptr};
    const bool*              localsShown{nullptr};
    std::string              type, header, refused;
    const std::string*       literal{nullptr};
    const std::vector<iv::Problem>*  problems{nullptr};
    const iv::Explanation*           explanation{nullptr};
    const HmiCalcDialog::Facts*      facts{nullptr};
    std::string              status;
    std::function<void(std::size_t)> problemClicked;

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1240.f, std::max(700.f, r.w - 40.f));
        const float h = std::min(800.f, std::max(500.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        const float x = panel_.x + 16.f;
        side_ = std::clamp(w * 0.32f, 280.f, 400.f);
        leftW_ = w - 32.f - side_ - 14.f;
        topY_ = panel_.y + 34.f + 10.f;
        pillW_ = std::min(leftW_ * 0.5f, ui::measureWidth(type, kSmall) + 22.f);
        float bx = x + pillW_ + 12.f;
        for (auto* b : {modeRaw, modeCode}) {
            if (!b) continue;
            const float bw = buttonWidth(b->text());
            b->setBounds({bx, topY_, bw, 28.f});
            bx += bw + 4.f;
        }
        const bool cs = codeShown && *codeShown;
        if (reopen) {
            reopen->setVisibility(cs ? ui::Visibility::Visible : ui::Visibility::Collapsed);
            const float bw = buttonWidth(reopen->text());
            reopen->setBounds({std::max(bx + 8.f, x + leftW_ - bw), topY_, bw, 28.f});
        }
        const float bottomY = panel_.bottom() - 44.f;
        float y = topY_ + 28.f + 12.f;
        sideBox_ = {x + leftW_ + 14.f, topY_, side_, bottomY - 12.f - topY_};
        const auto show = [](ui::Widget* wd, bool on) {
            if (wd) wd->setVisibility(on ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        };
        const auto bar = [&](std::vector<ui::Button*>& buttons, bool shown, float at) {
            float cx = x;
            for (auto* b : buttons) {
                if (!b) continue;
                if (!shown) {
                    if (b->visibility() == ui::Visibility::Visible) b->setVisibility(ui::Visibility::Hidden);
                    continue;
                }
                if (b->visibility() == ui::Visibility::Hidden) b->setVisibility(ui::Visibility::Visible);
                if (b->visibility() == ui::Visibility::Collapsed) continue;
                const float bw = buttonWidth(b->text());
                if (cx + bw > x + leftW_ + 1.f) {
                    b->setVisibility(ui::Visibility::Hidden);
                    continue;
                }
                b->setBounds({cx, at, bw, 28.f});
                cx += bw + 6.f;
            }
        };
        if (!cs) {
            for (auto* wd : {code, table, unfold}) show(wd, false);
            for (auto* b : {tabCode, tabLocals}) show(b, false);
            bar(codeBar, false, 0.f);
            bar(localsBar, false, 0.f);
            bar(templates, false, 0.f);
            const float litH = 62.f, probH = 58.f;
            const float tableH = std::max(90.f, bottomY - 12.f - probH - litH - 10.f - 28.f - 10.f - y);
            show(rawTable, true);
            if (rawTable) rawTable->setBounds({x, y, leftW_, tableH});
            y += tableH + 10.f;
            bar(rawBar, true, y);
            y += 28.f + 10.f;
            litBox_ = {x, y, leftW_, litH};
            probAt_ = {x, y + litH + 8.f};
        } else {
            show(rawTable, false);
            bar(rawBar, false, 0.f);
            chipsY_ = y;
            chipsX_ = x + ui::measureWidth("Partir d'un exemple :", kSmall) + 10.f;
            float cx = chipsX_;
            for (auto* b : templates) {
                if (!b) continue;
                const float bw = buttonWidth(b->text());
                if (cx + bw > x + leftW_ + 1.f) {
                    b->setVisibility(ui::Visibility::Hidden);
                    continue;
                }
                b->setVisibility(ui::Visibility::Visible);
                b->setBounds({cx, y, bw, 26.f});
                cx += bw + 6.f;
            }
            y += 26.f + 10.f;
            float tx = x;
            for (auto* b : {tabCode, tabLocals}) {
                if (!b) continue;
                show(b, true);
                const float bw = buttonWidth(b->text());
                b->setBounds({tx, y, bw, 28.f});
                tx += bw + 6.f;
            }
            y += 28.f + 8.f;
            const float barY = bottomY - 12.f - 28.f;
            const bool loc = localsShown && *localsShown;
            if (!loc) {
                headerY_ = y;
                y += 22.f;
                const float codeH = std::max(80.f, barY - 8.f - 22.f - y);
                show(code, true);
                if (code) code->setBounds({x, y, leftW_, codeH});
                endY_ = y + codeH + 3.f;
                show(table, false);
                show(unfold, false);
            } else {
                show(code, false);
                const float contentH = std::max(80.f, barY - 8.f - y);
                const bool unfolded = unfoldShown && *unfoldShown;
                const float unfoldH = unfolded ? std::clamp(contentH * 0.42f, 110.f, 200.f) : 0.f;
                show(table, true);
                if (table) table->setBounds({x, y, leftW_, std::max(60.f, contentH - unfoldH - (unfolded ? 8.f : 0.f))});
                show(unfold, unfolded);
                if (unfold) unfold->setBounds({x, y + contentH - unfoldH, leftW_, unfoldH});
            }
            bar(codeBar, !loc, barY);
            bar(localsBar, loc, barY);
        }
        float rx = panel_.right() - 16.f;
        for (auto* b : {ok, cancel, toCode}) {
            if (!b || b->visibility() == ui::Visibility::Collapsed) continue;
            const float bw = buttonWidth(b->text());
            rx -= bw;
            b->setBounds({rx, bottomY, bw, 30.f});
            rx -= 10.f;
        }
        statusAt_ = {x, bottomY + 7.f};
        statusW_ = std::max(60.f, rx - x - 10.f);
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        auto& r = ctx.r;
        const auto bold = ctx.theme.font.uiBold;
        const auto mono = ctx.theme.font.mono;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.fillRect({panel_.x, panel_.y, panel_.w, 34.f}, c.headerBg);
        r.drawText({panel_.x + 12.f, panel_.y + (34.f - r.lineHeight(bold)) * 0.5f}, title_, bold, c.text);
        const float x = panel_.x + 16.f;
        // Le type, dans sa pastille.
        const gfx::Rect pill{x, topY_ + 2.f, pillW_, 24.f};
        r.fillRect(pill, c.headerBg);
        r.strokeRect(pill, c.accent, 1.f);
        r.drawText({pill.x + 10.f, pill.y + (24.f - r.lineHeight(kSmall)) * 0.5f}, type, kSmall, c.accent);
        const bool cs = codeShown && *codeShown;
        if (!cs) {
            // Le litteral enregistre, puis ses problemes.
            r.drawText({litBox_.x, litBox_.y - 2.f}, "Le litt\xC3\xA9ral enregistr\xC3\xA9", kSmall, c.textMuted);
            const gfx::Rect box{litBox_.x, litBox_.y + 16.f, litBox_.w, litBox_.h - 16.f};
            r.fillRect(box, c.headerBg);
            r.strokeRect(box, c.border, 1.f);
            const std::string lit = literal && !literal->empty() ? *literal : std::string("(vide : la valeur du type)");
            float ly = box.y + 5.f;
            std::size_t n = 0;
            for (const auto& l : wrapped(lit, kSmall, box.w - 16.f)) {
                if (n++ >= 2) break;
                r.drawText({box.x + 8.f, ly}, l, mono, literal && !literal->empty() ? c.text : c.textMuted);
                ly += 18.f;
            }
            float py = probAt_.y;
            const float bottom = panel_.bottom() - 44.f - 14.f;
            if (problems && !problems->empty()) {
                for (const auto& p : *problems) {
                    for (const auto& l : wrapped((p.error ? "\xE2\x9C\x95 " : "\xE2\x9A\xA0 ") + p.text, kSmall, leftW_)) {
                        if (py + 17.f > bottom) break;
                        r.drawText({x, py}, l, kSmall, p.error ? c.error : c.warning);
                        py += 17.f;
                    }
                }
            } else if (py + 17.f <= bottom) {
                r.drawText({x, py}, "\xE2\x9C\x93 bon pour " + type, kSmall, c.ok);
            }
        } else {
            r.drawText({x, chipsY_ + (26.f - r.lineHeight(kSmall)) * 0.5f}, "Partir d'un exemple :", kSmall, c.textMuted);
            if (!(localsShown && *localsShown)) {
                r.drawText({x, headerY_ + 2.f}, header, mono, c.accent);
                const float hw = r.measure(header, mono).width;
                r.drawText({x + hw + 14.f, headerY_ + 2.f}, "(* fixe : le type de la valeur *)", kSmall, c.textMuted);
                r.drawText({x, endY_}, "END_FUNCTION", mono, c.accent);
            }
        }
        // La colonne de droite.
        r.fillRect(sideBox_, c.headerBg);
        r.strokeRect(sideBox_, c.border, 1.f);
        problemRects_.clear();
        const float inner = sideBox_.w - 20.f;
        float y = sideBox_.y + 8.f;
        const float bottom = sideBox_.bottom() - 6.f;
        const auto heading = [&](const std::string& t) {
            if (y + 18.f > bottom) return false;
            r.drawText({sideBox_.x + 10.f, y}, t, bold, c.text);
            y += 21.f;
            return true;
        };
        const auto lines = [&](const std::string& t, gfx::Color col, std::size_t max, gfx::FontId f = kSmall) {
            std::size_t n = 0;
            for (const auto& l : wrapped(t, kSmall, inner)) {
                if (y + 17.f > bottom || n++ >= max) break;
                r.drawText({sideBox_.x + 12.f, y}, l, f, col);
                y += 17.f;
            }
            y += 5.f;
        };
        if (cs && facts) {
            const auto& f = *facts;
            if (heading("Aper\xC3\xA7u")) {
                if (!f.error.empty()) lines("Erreur : " + f.error + " - le d\xC3\xA9marrage s'arr\xC3\xAAterait", c.error, 4);
                else if (!f.value.empty()) lines(f.value, c.accent, 5);
                if (!f.note.empty()) lines(f.note, c.warning, 3);
                if (f.error.empty() && f.value.empty() && f.note.empty()) lines("\xE2\x80\x94", c.textMuted, 1);
            }
            if (!f.problems.empty() && heading("Probl\xC3\xA8mes")) {
                for (std::size_t i = 0; i < f.problems.size(); ++i) {
                    const auto& p = f.problems[i];
                    const float top = y;
                    lines(p.text, p.error ? c.error : c.warning, 3);
                    if (p.line > 0) problemRects_.push_back({i, gfx::Rect{sideBox_.x, top, sideBox_.w, y - top}});
                    if (y + 17.f > bottom) break;
                }
            }
        }
        if (!refused.empty() && !cs && heading("Le code")) lines(refused, c.textMuted, 3);
        if (explanation && heading("Le type, expliqu\xC3\xA9")) {
            const auto& e = *explanation;
            std::size_t n = 0;
            for (const auto& t : e.tree) {
                if (y + 17.f > bottom || n++ >= 9) break;
                const float tx = sideBox_.x + 12.f + 14.f * static_cast<float>(t.depth);
                r.drawText({tx, y}, t.label, mono, c.accent);
                const float lw = r.measure(t.label, mono).width;
                const std::string phrase = "\xC2\xB7 " + t.phrase;
                const auto cut = wrapped(phrase, kSmall, std::max(40.f, sideBox_.right() - 10.f - (tx + lw + 8.f)));
                if (!cut.empty()) r.drawText({tx + lw + 8.f, y}, cut.front(), kSmall, c.textMuted);
                y += 17.f;
            }
            y += 4.f;
            lines(e.plain, c.text, 4);
            if (!e.read.empty() && heading("Le lire")) lines(e.read, c.text, 4);
            if (!e.cpp.empty() && heading("En C++")) lines(e.cpp, c.text, 2, mono);
            if (!e.natives.empty() && heading("Les natives qui vont avec")) {
                std::string s;
                for (const auto& nm : e.natives) s += (s.empty() ? "" : "  \xC2\xB7  ") + nm;
                lines(s, c.accent, 3);
            }
            if (cs && !e.code.empty() && heading("Le code")) lines(e.code, c.textMuted, 4);
        }
        if (!status.empty()) {
            const auto cut = wrapped(status, kSmall, statusW_);
            if (!cut.empty()) r.drawText(statusAt_, cut.front(), kSmall, c.textMuted);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && problemClicked)
            for (const auto& [i, box] : problemRects_)
                if (box.contains(d->pos)) {
                    problemClicked(i);
                    return ui::EventResult::Consumed;
                }
        return ui::EventResult::Ignored;
    }

private:
    std::string title_;
    gfx::Rect   panel_{}, sideBox_{}, litBox_{};
    gfx::Point  statusAt_{}, probAt_{};
    float       side_{300.f}, leftW_{500.f}, topY_{0.f}, pillW_{120.f}, chipsY_{0.f}, chipsX_{0.f}, headerY_{0.f}, endY_{0.f}, statusW_{200.f};
    mutable std::vector<std::pair<std::size_t, gfx::Rect>> problemRects_;
};

} // namespace

// ---------------------------------------------------------------- la fenetre ----
HmiCalcDialog::Outcome HmiCalcDialog::outcomeOf(const std::string& payload) {
    Outcome o;
    const auto v = core::splitFields(payload);
    o.calc = v.size() > 0 ? v[0] : std::string{};
    o.locals = v.size() > 1 ? v[1] : std::string{};
    o.code = v.size() > 2 ? v[2] == "code" : true;
    o.value = v.size() > 3 ? v[3] : std::string{};
    o.reopen = v.size() > 4 && !v[4].empty() ? std::atoi(v[4].c_str()) : -1;
    return o;
}

core::Status HmiCalcDialog::buildUnified() {
    const hmi::Project& P = *spec_.project;
    raw_ = iv::load(P, spec_.type, spec_.value);
    templates_ = iv::templates(P, spec_.name, spec_.type, spec_.readable);
    explanation_ = iv::explain(P, spec_.name, spec_.type, spec_.owner, spec_.readable);
    reopen_ = spec_.reopenForced ? 1 : spec_.reopen;
    codeShown_ = spec_.code && spec_.codeRefused.empty();
    // Le code de depart : celui de la declaration calculee ; sinon le premier exemple.
    std::string calc = spec_.calc, locals = spec_.locals;
    if (!spec_.code && !templates_.empty()) {
        calc = templates_.front().calc;
        locals = localsLines(templates_.front().locals);
    }
    setLocalsRows(locals);
    auto body = std::make_unique<InitBody>(spec_.title);
    body->type = spec_.type;
    body->header = iv::header(spec_.name, spec_.type);
    body->refused = spec_.codeRefused;
    const std::string base = spec_.id;
    const auto button = [&](const std::string& text, const std::string& id) {
        return &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>(text, base + "." + id)));
    };
    // Le haut : Brut | Code fx, le moment du calcul.
    modeRaw_ = button("Brut", "mode.brut");
    modeCode_ = button("Code \xC6\x92x", "mode.code");
    links_ += modeRaw_->clicked->connect([this] { (void)showCode(false); });
    links_ += modeCode_->clicked->connect([this] { (void)showCode(true); });
    if (!spec_.codeRefused.empty()) modeCode_->setTooltip(spec_.codeRefused);
    body->modeRaw = modeRaw_;
    body->modeCode = modeCode_;
    if (reopen_ >= 0) {
        reopenButton_ = button("calcul\xC3\xA9" "e au d\xC3\xA9marrage", "moment");
        links_ += reopenButton_->clicked->connect([this] { (void)setReopen(reopen_ != 1); });
        body->reopen = reopenButton_;
    }
    // Les exemples.
    for (std::size_t i = 0; i < templates_.size(); ++i) {
        auto* b = button(templates_[i].label, "exemple." + std::to_string(i));
        b->setStyle(ui::Button::Style::Flat);
        links_ += b->clicked->connect([this, i] { (void)useTemplate(i); });
        templateButtons_.push_back(b);
    }
    body->templates = templateButtons_;
    // Le code, ses onglets, ses locales (comme la fenetre du calcul).
    tabCode_ = button("</> Code", "onglet.code");
    tabLocals_ = button("\xE2\x89\xA1 Locales", "onglet.locales");
    links_ += tabCode_->clicked->connect([this] { showLocals(false); });
    links_ += tabLocals_->clicked->connect([this] { showLocals(true); });
    body->tabCode = tabCode_;
    body->tabLocals = tabLocals_;
    auto e = std::make_unique<ui::MultiLineText>(base + ".code");
    e->setLanguage(ui::Language::StructuredText);
    e->setText(calc);
    e->setReadOnly(false);
    code_ = &static_cast<ui::MultiLineText&>(body->addChild(std::move(e)));
    links_ += code_->textChanged->connect([this](const std::string&) { changed(); });
    if (spec_.assist) spec_.assist(*code_, [this] { return localsText(); });
    body->code = code_;
    auto* run = button("\xE2\x96\xB6 Calculer (F5)", "calculer");
    links_ += run->clicked->connect([this] { recompute(); });
    auto* back = button("Revenir au code d'origine", "revenir");
    links_ += back->clicked->connect([this] { revert(); });
    moveButton_ = button("D\xC3\xA9placer dans Locales", "deplacer");
    links_ += moveButton_->clicked->connect([this] { (void)moveVarBlocks(); });
    body->codeBar = {run, back, moveButton_};
    auto table = std::make_unique<ui::TableView>(base + ".locales");
    table->setModel(localsModel());
    table->setColumns({{"Nom", 150.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                       {"Type", 200.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                       {"Valeur initiale", 200.f, 60.f, true, false, true, ui::Align::Start, 0, false},
                       {"Documentation", 220.f, 60.f, true, false, true, ui::Align::Start, 0, false}});
    table_ = &static_cast<ui::TableView&>(body->addChild(std::move(table)));
    links_ += table_->cellEdited->connect([this](ui::RowIndex, std::size_t, const std::string&, bool accepted) {
        if (accepted) changed();
        refreshUnfold();
    });
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { refreshUnfold(); });
    body->table = table_;
    unfoldModel_ = std::make_shared<HmiUnfoldModel>();
    auto unfold = std::make_unique<ui::TableView>(base + ".locales.deballage");
    unfold->setModel(unfoldModel_);
    unfold->setColumns(HmiUnfoldModel::columns());
    unfold->setVisibility(ui::Visibility::Collapsed);
    unfold_ = &static_cast<ui::TableView&>(body->addChild(std::move(unfold)));
    links_ += unfold_->expanderClicked->connect([this](ui::RowIndex r) { unfoldModel_->toggle(r); });
    body->unfold = unfold_;
    body->unfoldShown = &unfoldShown_;
    links_ += localsModel()->modelReset->connect([this] { refreshUnfold(); });
    auto* add = button("+ Ajouter", "locale.ajouter");
    links_ += add->clicked->connect([this] { (void)addLocal(); });
    auto* remove = button("Supprimer", "locale.supprimer");
    links_ += remove->clicked->connect([this] { (void)removeLocal(selectedLocal()); });
    auto* up = button("Monter", "locale.monter");
    links_ += up->clicked->connect([this] { (void)moveLocal(selectedLocal(), -1); });
    auto* down = button("Descendre", "locale.descendre");
    links_ += down->clicked->connect([this] { (void)moveLocal(selectedLocal(), +1); });
    body->localsBar = {add, remove, up, down};
    // Le Brut : la table faite pour le type, ses gestes.
    rawModel_ = std::make_shared<RawModel>(raw_);
    rawModel_->edited = [this] { rawChanged(); };
    auto rt = std::make_unique<ui::TableView>(base + ".brut");
    rt->setModel(rawModel_);
    rt->setColumns(rawModel_->columns(560.f));
    rt->setSelectionMode(ui::SelectionMode::Single);
    rawTable_ = &static_cast<ui::TableView&>(body->addChild(std::move(rt)));
    links_ += rawTable_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& rows) {
        if (syncing_ || raw_.freeText || raw_.shape.kind != iv::Kind::Enum || rows.empty()) return;
        (void)rawChoose(static_cast<int>(rows.front()));
    });
    body->rawTable = rawTable_;
    const auto selectedRaw = [this]() -> std::size_t {
        if (!rawTable_) return static_cast<std::size_t>(-1);
        const auto rows = rawTable_->selectedModelRows();
        return rows.empty() ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(rows.front());
    };
    const auto& sh = raw_.shape;
    const std::string addLabel = sh.kind == iv::Kind::Map ? "+ Entr\xC3\xA9" "e" : sh.kind == iv::Kind::List ? "+ \xC3\x89l\xC3\xA9ment" : "+ Valeur";
    auto* rAdd = button(addLabel, "brut.ajouter");
    links_ += rAdd->clicked->connect([this] { (void)rawAdd(); });
    auto* rRemove = button("Retirer", "brut.retirer");
    links_ += rRemove->clicked->connect([this, selectedRaw] { (void)rawRemove(selectedRaw()); });
    auto* rUp = button("Monter", "brut.monter");
    links_ += rUp->clicked->connect([this, selectedRaw] { (void)rawMove(selectedRaw(), -1); });
    auto* rDown = button("Descendre", "brut.descendre");
    links_ += rDown->clicked->connect([this, selectedRaw] { (void)rawMove(selectedRaw(), +1); });
    auto* rSort = button("Ranger par cl\xC3\xA9", "brut.ranger");
    links_ += rSort->clicked->connect([this] { (void)rawSortByKey(); });
    auto* rZero = button("Tout \xC3\xA0 0", "brut.zero");
    links_ += rZero->clicked->connect([this] { (void)rawFillZero(); });
    auto* rSeq = button("Suite (comme les deux premi\xC3\xA8res)", "brut.suite");
    links_ += rSeq->clicked->connect([this] {
        double a = 0, b = 1;
        const bool first = raw_.rows.size() > 0 && !raw_.rows[0].cells.empty() && number(raw_.rows[0].cells[0], a);
        double second = 0;
        if (first && raw_.rows.size() > 1 && !raw_.rows[1].cells.empty() && number(raw_.rows[1].cells[0], second)) b = second - a;
        if (!first) a = 0;
        (void)rawSequence(a, b);
    });
    auto* rFree = button("Repartir de l'\xC3\xA9" "diteur du type", "brut.editeur");
    links_ += rFree->clicked->connect([this] { (void)rawLeaveFreeText(); });
    rawBar_ = {rAdd, rRemove, rUp, rDown, rSort, rZero, rSeq, rFree};
    body->rawBar = rawBar_;
    // Le bas.
    toCodeButton_ = button("Passer en code", "encode");
    links_ += toCodeButton_->clicked->connect([this] { (void)toCode(); });
    body->toCode = toCodeButton_;
    body->cancel = button("Annuler", "annuler");
    links_ += body->cancel->clicked->connect([this] { finish(false); });
    body->ok = button("Appliquer", "appliquer");
    body->ok->setStyle(ui::Button::Style::Primary);
    links_ += body->ok->clicked->connect([this] { finish(true); });
    body->facts = &facts_;
    body->localsShown = &localsShown_;
    body->codeShown = &codeShown_;
    body->problems = &rawProblems_;
    body->explanation = &explanation_;
    body->problemClicked = [this](std::size_t i) { (void)goToProblem(i); };
    body_ = body.get();
    setRoot(std::move(body));
    rawChanged();
    recompute();
    refreshButtons();
    focus().clear();
    focus().focus(codeShown_ ? static_cast<ui::Widget*>(code_) : static_cast<ui::Widget*>(rawTable_));
    if (spec_.line > 0 && codeShown_) code_->selectRange(static_cast<std::size_t>(spec_.line - 1), 0, 0);
    return core::ok();
}

std::string HmiCalcDialog::rawLiteral() const { return iv::literal(raw_); }

void HmiCalcDialog::rawChanged() {
    const hmi::Project* P = spec_.project;
    rawProblems_ = P ? iv::problems(*P, raw_) : std::vector<iv::Problem>{};
    if (rawModel_) {
        rawModel_->badRows.clear();
        for (const auto& p : rawProblems_)
            if (p.error && p.row >= 0) rawModel_->badRows.insert(static_cast<std::size_t>(p.row));
        rawModel_->modelReset->emit();
    }
    if (rawTable_ && raw_.shape.kind == iv::Kind::Enum && !raw_.freeText && raw_.choice >= 0) {
        syncing_ = true;
        rawTable_->selectModelRows({static_cast<ui::RowIndex>(raw_.choice)}, false);
        syncing_ = false;
    }
    literal_ = rawLiteral();
    if (auto* b = dynamic_cast<InitBody*>(body_)) b->literal = &literal_;
    refreshButtons();
}

bool HmiCalcDialog::showCode(bool on) {
    if (!unified()) return false;
    if (on && !spec_.codeRefused.empty()) {
        if (auto* b = dynamic_cast<InitBody*>(body_)) {
            b->status = "Pas de code ici : " + spec_.codeRefused;
            b->invalidate();
        }
        return false;
    }
    codeShown_ = on;
    refreshButtons();
    focus().clear();
    focus().focus(on ? (localsShown_ ? static_cast<ui::Widget*>(table_) : static_cast<ui::Widget*>(code_)) : static_cast<ui::Widget*>(rawTable_));
    if (on) recompute();
    return true;
}

bool HmiCalcDialog::setRawCell(std::size_t row, std::size_t cell, const std::string& text) {
    if (!iv::setCell(raw_, row, cell, text)) return false;
    rawChanged();
    return true;
}

bool HmiCalcDialog::setRawKey(std::size_t row, const std::string& text) {
    if (!iv::setKey(raw_, row, text)) return false;
    rawChanged();
    return true;
}

bool HmiCalcDialog::rawAdd() {
    if (!iv::addRow(raw_)) return false;
    rawChanged();
    if (rawTable_) rawTable_->selectModelRows({static_cast<ui::RowIndex>(raw_.rows.size() - 1)});
    return true;
}

bool HmiCalcDialog::rawRemove(std::size_t row) {
    if (!iv::removeRow(raw_, row)) return false;
    rawChanged();
    return true;
}

bool HmiCalcDialog::rawMove(std::size_t row, int delta) {
    if (!iv::moveRow(raw_, row, delta)) return false;
    rawChanged();
    if (rawTable_) rawTable_->selectModelRows({static_cast<ui::RowIndex>(static_cast<long long>(row) + delta)});
    return true;
}

bool HmiCalcDialog::rawSortByKey() {
    if (!iv::sortByKey(raw_)) return false;
    rawChanged();
    return true;
}

bool HmiCalcDialog::rawFillZero() {
    if (!iv::fillZero(raw_)) return false;
    rawChanged();
    return true;
}

bool HmiCalcDialog::rawSequence(double start, double step) {
    if (!iv::sequence(raw_, start, step)) return false;
    rawChanged();
    return true;
}

bool HmiCalcDialog::rawChoose(int option) {
    if (!iv::choose(raw_, option)) return false;
    rawChanged();
    return true;
}

bool HmiCalcDialog::rawLeaveFreeText() {
    if (!iv::leaveFreeText(raw_)) return false;
    if (rawTable_ && rawModel_) rawTable_->setColumns(rawModel_->columns(std::max(300.f, rawTable_->bounds().w)));
    rawChanged();
    return true;
}

bool HmiCalcDialog::toCode() {
    if (!unified() || !spec_.codeRefused.empty()) return showCode(true);
    const std::string code = iv::toCode(rawLiteral());
    if (code_) code_->setText(code);
    setLocalsRows({});
    (void)showCode(true);
    showLocals(false);
    recompute();
    return true;
}

bool HmiCalcDialog::useTemplate(std::size_t index) {
    if (index >= templates_.size()) return false;
    if (code_) code_->setText(templates_[index].calc);
    setLocalsRows(localsLines(templates_[index].locals));
    if (!codeShown_) (void)showCode(true);
    recompute();
    refreshButtons();
    return true;
}

bool HmiCalcDialog::setReopen(bool atOpen) {
    if (reopen_ < 0 || spec_.reopenForced) return false;
    reopen_ = atOpen ? 1 : 0;
    refreshButtons();
    return true;
}

bool HmiCalcDialog::canApply() const {
    if (!unified() || codeShown_) return true;
    return std::none_of(rawProblems_.begin(), rawProblems_.end(), [](const iv::Problem& p) { return p.error; });
}

void HmiCalcDialog::refreshUnified() {
    auto* b = dynamic_cast<InitBody*>(body_);
    if (!b) return;
    if (modeRaw_) modeRaw_->setStyle(codeShown_ ? ui::Button::Style::Flat : ui::Button::Style::Primary);
    if (modeCode_) modeCode_->setStyle(codeShown_ ? ui::Button::Style::Primary : ui::Button::Style::Flat);
    if (reopenButton_) {
        reopenButton_->setText(spec_.reopenForced ? std::string("\xC3\xA0 chaque ouverture (lit ses param\xC3\xA8tres)")
                                                  : reopen_ == 1 ? std::string("calcul\xC3\xA9" "e \xC3\xA0 chaque ouverture \xE2\x96\xBE")
                                                                 : std::string("calcul\xC3\xA9" "e au d\xC3\xA9marrage \xE2\x96\xBE"));
        reopenButton_->setEnabled(!spec_.reopenForced);
        reopenButton_->setTooltip(spec_.reopenForced ? std::string("Le code lit un param\xC3\xA8tre de la popup : il tourne \xC3\xA0 chaque ouverture, d'office.")
                                                     : std::string("Au d\xC3\xA9marrage : une fois. \xC3\x80 chaque ouverture : \xC3\xA0 chaque fois que la vue ou la popup s'ouvre "
                                                                   "(un clic change)."));
    }
    if (toCodeButton_) toCodeButton_->setVisibility(codeShown_ || !spec_.codeRefused.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    // Les gestes du Brut selon le type.
    const auto& sh = raw_.shape;
    const bool free = raw_.freeText;
    const auto want = [](ui::Button* bt, bool on) {
        if (!bt) return;
        const auto v = on ? ui::Visibility::Visible : ui::Visibility::Collapsed;
        if (bt->visibility() == ui::Visibility::Collapsed || !on) bt->setVisibility(v);
    };
    if (rawBar_.size() == 8) {
        const bool sized = iv::resizable(raw_);
        want(rawBar_[0], sized);
        want(rawBar_[1], sized);
        want(rawBar_[2], !free && (sh.kind == iv::Kind::List || sh.kind == iv::Kind::Ring));
        want(rawBar_[3], !free && (sh.kind == iv::Kind::List || sh.kind == iv::Kind::Ring));
        want(rawBar_[4], !free && sh.kind == iv::Kind::Map);
        want(rawBar_[5], !free && (sh.kind == iv::Kind::Array || sh.kind == iv::Kind::Matrix));
        want(rawBar_[6], !free && sh.kind == iv::Kind::Array && !sh.structElements);
        want(rawBar_[7], free);
    }
    if (b->ok) b->ok->setEnabled(canApply());
    std::string st;
    if (!codeShown_) {
        st = "Le litt\xC3\xA9ral ci-dessus est ce qui sera enregistr\xC3\xA9. Double-clic ou F2 : \xC3\xA9" "crire une case ; une case vide vaut la valeur du type.";
        if (!canApply()) st = "Une faute dans le brut : Appliquer attend qu'elle soit corrig\xC3\xA9" "e.";
    } else {
        st = localsShown_ ? std::string("Double-clic ou F2 : \xC3\xA9" "crire une case ; une ligne vide ne compte pas.")
                          : std::string("Le code rend la valeur (RETURN). F5 : calculer ; Ctrl+Espace : l'aide \xC3\xA0 la saisie ; Ctrl+I : choisir une fonction.");
    }
    b->status = st;
    b->invalidateLayout();
    b->invalidate();
}

} // namespace app
